// This file is part of HermesCommon
//
// Copyright (c) 2009 hp-FEM group at the University of Nevada, Reno (UNR).
// Email: hpfem-group@unr.edu, home page: http://www.hpfem.org/.
//
// Hermes2D is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published
// by the Free Software Foundation; either version 2 of the License,
// or (at your option) any later version.
//
// Hermes2D is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Hermes2D; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
/*! \file petsc_solver.cpp
\brief PETSc solver interface.
*/
#include "config.h"
#ifdef WITH_PETSC
#include "petsc_solver.h"
#include "callstack.h"
#include "common.h"
#include "util/memory_handling.h"

/// \todo Check #ifdef WITH_MPI and use the parallel methods from PETSc accordingly.

namespace Hermes
{
  namespace Algebra
  {
    static int num_petsc_objects = 0;

#ifdef PETSC_USE_COMPLEX
    inline void vec_get_value(Vec x, PetscInt ni, const PetscInt ix[], std::complex<double> y[])
    {
      VecGetValues(x, ni, ix, y);
    }

    void vec_get_value(Vec x, PetscInt ni, const PetscInt ix[], double y[])
    {
      PetscScalar *py = malloc_with_check<PetscScalar>(ni);
      VecGetValues(x, ni, ix, py);
      for (int i = 0; i < ni; i++)y[i] = py[i].real();
      free_with_check(py);
    }
#else
    inline void vec_get_value(Vec x, PetscInt ni, const PetscInt ix[], double y[])
    {
      VecGetValues(x, ni, ix, y);
    }
    inline void vec_get_value(Vec x, PetscInt ni, const PetscInt ix[], std::complex<double> y[])
    {
      throw(Exceptions::Exception("PETSc with complex numbers support required."));
    }
#endif

    int remove_petsc_object()
    {
      PetscBool petsc_initialized, petsc_finalized;
      int ierr = PetscFinalized(&petsc_finalized); CHKERRQ(ierr);
      ierr = PetscInitialized(&petsc_initialized); CHKERRQ(ierr);
      if (petsc_finalized == PETSC_TRUE || petsc_initialized == PETSC_FALSE)
        // This should never happen here.
        return -1;

      if (--num_petsc_objects == 0)
      {
        int ierr = PetscFinalize();
        CHKERRQ(ierr);
        //FIXME this->info("PETSc finalized. No more PETSc usage allowed until application restart.");
      }
    }

    int add_petsc_object()
    {
      int ierr;
      PetscBool petsc_initialized, petsc_finalized;
      ierr = PetscFinalized(&petsc_finalized); CHKERRQ(ierr);

      if (petsc_finalized == PETSC_TRUE)
        throw Hermes::Exceptions::Exception("PETSc cannot be used once it has been finalized. You must restart the application.");

      ierr = PetscInitialized(&petsc_initialized); CHKERRQ(ierr);

      if (petsc_initialized != PETSC_TRUE)
      {
        ierr = PetscInitializeNoArguments();
        CHKERRQ(ierr);
      }

      num_petsc_objects++;
    }

    template<typename Scalar>
    PetscMatrix<Scalar>::PetscMatrix()
    {
      inited = false;
      add_petsc_object();
    }

    template<typename Scalar>
    PetscMatrix<Scalar>::~PetscMatrix()
    {
      free();
      remove_petsc_object();
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::alloc()
    {
      assert(this->pages != nullptr);

      // calc nnz
      int *nnz_array = malloc_with_check<int>(this->size);

      // fill in nnz_array
      int aisize = this->get_num_indices();
      int *ai = malloc_with_check<int>(aisize);

      // sort the indices and remove duplicities, insert into ai
      int pos = 0;
      for (unsigned int i = 0; i < this->size; i++)
      {
        nnz_array[i] = this->sort_and_store_indices(this->pages[i], ai + pos, ai + aisize);
        pos += nnz_array[i];
      }
      // stote the number of nonzeros
      nnz = pos;
      free_with_check(this->pages);
      free_with_check(ai);

      //
      MatCreateSeqAIJ(PETSC_COMM_SELF, this->size, this->size, 0, nnz_array, &matrix);
      //  MatSetOption(matrix, MAT_ROW_ORIENTED);
      //  MatSetOption(matrix, MAT_ROWS_SORTED);
      free_with_check(nnz_array);

      inited = true;
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::free()
    {
      if (inited) MatDestroy(&matrix);
      inited = false;
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::finish()
    {
      MatAssemblyBegin(matrix, MAT_FINAL_ASSEMBLY);
      MatAssemblyEnd(matrix, MAT_FINAL_ASSEMBLY);
    }

    template<>
    double PetscMatrix<double>::get(unsigned int m, unsigned int n) const
    {
      PetscScalar pv;
      MatGetValues(matrix, 1, (PetscInt*)&m, 1, (PetscInt*)&n, &pv);
      return PetscRealPart(pv);
    }

#ifdef PETSC_USE_COMPLEX
    template<>
    std::complex<double> PetscMatrix<std::complex<double> >::get(unsigned int m, unsigned int n) const
    {
      std::complex<double> v = 0.0;
      MatGetValues(matrix, 1, (PetscInt*)&m, 1, (PetscInt*)&n, &v);
      return v;
    }
#else
    template<>
    std::complex<double> PetscMatrix<std::complex<double> >::get(unsigned int m, unsigned int n) const
    {
      throw(Exceptions::Exception("PETSc with complex numbers support required."));
    }
#endif

    template<typename Scalar>
    void PetscMatrix<Scalar>::zero()
    {
      MatZeroEntries(matrix);
    }

#ifdef PETSC_USE_COMPLEX
    inline PetscScalar to_petsc(double x)
    {
      return std::complex<double>(x, 0);
    }

    inline PetscScalar to_petsc(std::complex<double> x)
    {
      return x;
    }
#else
    inline PetscScalar to_petsc(double x)
    {
      return x;
    }
    inline PetscScalar to_petsc(std::complex<double> x)
    {
      throw(Exceptions::Exception("PETSc with complex numbers support required."));
    }
#endif

    template<typename Scalar>
    void PetscMatrix<Scalar>::add(unsigned int m, unsigned int n, Scalar v)
    {
      if (v != 0.0)
      {    // ignore zero values.
#pragma omp critical (PetscMatrix_add)
        MatSetValue(matrix, (PetscInt)m, (PetscInt)n, to_petsc(v), ADD_VALUES);
      }
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::export_to_file(const char *filename, const char *var_name, MatrixExportFormat fmt, char* number_format)
    {
      throw Exceptions::MethodNotImplementedException("PetscVector<double>::export_to_file");
      /*
      switch (fmt)
      {
      case DF_MATLAB_SPARSE: //only to stdout
      PetscViewer  viewer = PETSC_VIEWER_STDOUT_SELF;
      PetscViewerSetFormat(viewer, PETSC_VIEWER_ASCII_MATLAB);
      MatView(matrix, viewer);
      return true;
      }
      */
    }

    template<typename Scalar>
    unsigned int PetscMatrix<Scalar>::get_nnz() const
    {
      return nnz;
    }

    template<typename Scalar>
    double PetscMatrix<Scalar>::get_fill_in() const
    {
      return (double)nnz / ((double)this->size*this->size);
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::add_petsc_matrix(PetscMatrix<Scalar>* mat)
    {
      //matrix = 1*mat + matrix (matrix and mat have different nonzero structure)
      MatAXPY(matrix, 1, mat->matrix, DIFFERENT_NONZERO_PATTERN);
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::add_sparse_matrix(SparseMatrix<Scalar>* mat)
    {
      PetscMatrix<Scalar>* mat_petsc = (PetscMatrix<Scalar>*)mat;
      if (mat_petsc)
        this->add_petsc_matrix(mat_petsc);
      else
        SparseMatrix<Scalar>::add_sparse_matrix(mat);
    }

    // Multiplies matrix with a Scalar.

    template<typename Scalar>
    void PetscMatrix<Scalar>::multiply_with_Scalar(Scalar value)
    {
      MatScale(matrix, to_petsc(value));
    }

    template<typename Scalar>
    void PetscMatrix<Scalar>::create(unsigned int size, unsigned int nnz, int* ap, int* ai, Scalar* ax)
    {
      this->size = size;
      this->nnz = nnz;
      PetscScalar* pax = malloc_with_check<PetscScalar>(nnz, this);
      for (unsigned i = 0; i < nnz; i++)
        pax[i] = to_petsc(ax[i]);
      MatCreateSeqAIJWithArrays(PETSC_COMM_SELF, size, size, ap, ai, pax, &matrix);
      delete pax;
    }

    template<typename Scalar>
    PetscMatrix<Scalar>* PetscMatrix<Scalar>::duplicate() const
    {
      PetscMatrix<Scalar>*ptscmatrix = new PetscMatrix<Scalar>();
      MatDuplicate(matrix, MAT_COPY_VALUES, &(ptscmatrix->matrix));
      ptscmatrix->size = this->size;
      ptscmatrix->nnz = nnz;
      return ptscmatrix;
    }

    template<typename Scalar>
    PetscVector<Scalar>::PetscVector()
    {
      inited = false;
      add_petsc_object();
    }

    template<typename Scalar>
    PetscVector<Scalar>::~PetscVector()
    {
      free();
      remove_petsc_object();
    }

    template<typename Scalar>
    void PetscVector<Scalar>::alloc(unsigned int n)
    {
      free();
      this->size = n;
      VecCreateSeq(PETSC_COMM_SELF, this->size, &vec);
      inited = true;
    }

    template<typename Scalar>
    void PetscVector<Scalar>::free()
    {
      if (inited) VecDestroy(&vec);
      inited = false;
    }

    template<typename Scalar>
    void PetscVector<Scalar>::finish()
    {
      VecAssemblyBegin(vec);
      VecAssemblyEnd(vec);
    }

    template<>
    double PetscVector<double>::get(unsigned int idx) const
    {
      PetscScalar py;
      VecGetValues(vec, 1, (PetscInt*)&idx, &py);
      return PetscRealPart(py);
    }
#ifdef PETSC_USE_COMPLEX
    template<>
    std::complex<double> PetscVector<std::complex<double> >::get(unsigned int idx) const
    {
      std::complex<double> y = 0;
      VecGetValues(vec, 1, (PetscInt*)&idx, &y);
      return y;
    }
#else
    template<>
    std::complex<double> PetscVector<std::complex<double> >::get(unsigned int idx) const
    {
      throw(Exceptions::Exception("PETSc with complex numbers support required."));
    }
#endif

    template<typename Scalar>
    void PetscVector<Scalar>::extract(Scalar *v) const
    {
      int *idx = malloc_with_check<int>(this->size);
      for (unsigned int i = 0; i < this->size; i++) idx[i] = i;
      vec_get_value(vec, this->size, idx, v);
      free_with_check(idx);
    }

    template<typename Scalar>
    void PetscVector<Scalar>::zero()
    {
      VecZeroEntries(vec);
    }

    template<typename Scalar>
    Vector<Scalar>* PetscVector<Scalar>::change_sign()
    {
      PetscScalar* y = malloc_with_check<PetscScalar>(this->size);
      int *idx = malloc_with_check<int>(this->size);
      for (unsigned int i = 0; i < this->size; i++) idx[i] = i;
      VecGetValues(vec, this->size, idx, y);
      for (unsigned int i = 0; i < this->size; i++) y[i] *= -1.;
      VecSetValues(vec, this->size, idx, y, INSERT_VALUES);
      free_with_check(y);
      free_with_check(idx);
      return this;
    }

    template<typename Scalar>
    void PetscVector<Scalar>::set(unsigned int idx, Scalar y)
    {
      VecSetValue(vec, idx, to_petsc(y), INSERT_VALUES);
    }

    template<typename Scalar>
    void PetscVector<Scalar>::add(unsigned int idx, Scalar y)
    {
#pragma omp critical (PetscVector_add)
      VecSetValue(vec, idx, to_petsc(y), ADD_VALUES);
    }

    template<typename Scalar>
    void PetscVector<Scalar>::add(unsigned int n, unsigned int *idx, Scalar *y)
    {
      PetscScalar py;
      for (unsigned int i = 0; i < n; i++)
      {
        VecSetValue(vec, idx[i], to_petsc(y[i]), ADD_VALUES);
      }
    }

    template<typename Scalar>
    Vector<Scalar>* PetscVector<Scalar>::add_vector(Vector<Scalar>* vec)
    {
      assert(this->get_size() == vec->get_size());
      for (unsigned int i = 0; i < this->get_size(); i++)
        this->add(i, vec->get(i));
      return this;
    }

    template<typename Scalar>
    Vector<Scalar>* PetscVector<Scalar>::add_vector(Scalar* vec)
    {
      for (unsigned int i = 0; i < this->get_size(); i++)
        this->add(i, vec[i]);
      return this;
    }

    template<typename Scalar>
    void PetscVector<Scalar>::export_to_file(const char *filename, const char *var_name, MatrixExportFormat fmt, char* number_format)
    {
      throw Exceptions::MethodNotImplementedException("PetscVector<double>::export_to_file");
      /*
      switch (fmt)
      {
      case DF_MATLAB_SPARSE: //only to stdout
      PetscViewer  viewer = PETSC_VIEWER_STDOUT_SELF;
      PetscViewerSetFormat(viewer, PETSC_VIEWER_ASCII_MATLAB);
      VecView(vec, viewer);
      return true;
      }
      */
    }

    template class HERMES_API PetscMatrix < double > ;
    template class HERMES_API PetscMatrix < std::complex<double> > ;
    template class HERMES_API PetscVector < double > ;
    template class HERMES_API PetscVector < std::complex<double> > ;
  }
  namespace Solvers
  {
    template<typename Scalar>
    PetscLinearMatrixSolver<Scalar>::PetscLinearMatrixSolver(PetscMatrix<Scalar> *mat, PetscVector<Scalar> *rhs)
      : DirectSolver<Scalar>(mat, rhs), m(mat), rhs(rhs)
    {
      add_petsc_object();
    }

    template<typename Scalar>
    PetscLinearMatrixSolver<Scalar>::~PetscLinearMatrixSolver()
    {
      remove_petsc_object();
    }

    template<typename Scalar>
    int PetscLinearMatrixSolver<Scalar>::get_matrix_size()
    {
      return m->get_size();
    }

    template<typename Scalar>
    void PetscLinearMatrixSolver<Scalar>::solve()
    {
      assert(m != nullptr);
      assert(rhs != nullptr);

      PetscErrorCode ec;
      KSP ksp;
      Vec x;

      this->tick();

      KSPCreate(PETSC_COMM_WORLD, &ksp);

      KSPSetOperators(ksp, m->matrix, m->matrix, DIFFERENT_NONZERO_PATTERN);
      KSPSetFromOptions(ksp);
      VecDuplicate(rhs->vec, &x);

      ec = KSPSolve(ksp, rhs->vec, x);
      //FIXME if (ec) return false;

      this->tick();
      this->time = this->accumulated();

      // allocate memory for solution vector
      free_with_check(this->sln);
      this->sln = malloc_with_check<Scalar>(m->get_size());
      memset(this->sln, 0, m->get_size() * sizeof(Scalar));

      // index map vector (basic serial code uses the map sln[i] = x[i] for all dofs.
      int *idx = malloc_with_check<int>(m->get_size());
      for (unsigned int i = 0; i < m->get_size(); i++) idx[i] = i;

      // copy solution to the output solution vector
      vec_get_value(x, m->get_size(), idx, this->sln);
      free_with_check(idx);

      KSPDestroy(&ksp);
      VecDestroy(&x);
    }

    template class HERMES_API PetscLinearMatrixSolver < double > ;
    template class HERMES_API PetscLinearMatrixSolver < std::complex<double> > ;
  }
}
#endif