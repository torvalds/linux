/* Integer matrix math routines
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dberlin@dberlin.org>.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "lambda.h"

static void lambda_matrix_get_column (lambda_matrix, int, int, 
				      lambda_vector);

/* Allocate a matrix of M rows x  N cols.  */

lambda_matrix
lambda_matrix_new (int m, int n)
{
  lambda_matrix mat;
  int i;

  mat = ggc_alloc (m * sizeof (lambda_vector));
  
  for (i = 0; i < m; i++)
    mat[i] = lambda_vector_new (n);

  return mat;
}

/* Copy the elements of M x N matrix MAT1 to MAT2.  */

void
lambda_matrix_copy (lambda_matrix mat1, lambda_matrix mat2,
		    int m, int n)
{
  int i;

  for (i = 0; i < m; i++)
    lambda_vector_copy (mat1[i], mat2[i], n);
}

/* Store the N x N identity matrix in MAT.  */

void
lambda_matrix_id (lambda_matrix mat, int size)
{
  int i, j;

  for (i = 0; i < size; i++)
    for (j = 0; j < size; j++)
      mat[i][j] = (i == j) ? 1 : 0;
}

/* Return true if MAT is the identity matrix of SIZE */

bool
lambda_matrix_id_p (lambda_matrix mat, int size)
{
  int i, j;
  for (i = 0; i < size; i++)
    for (j = 0; j < size; j++)
      {
	if (i == j)
	  {
	    if (mat[i][j] != 1)
	      return false;
	  }
	else
	  {
	    if (mat[i][j] != 0)
	      return false;
	  }
      }
  return true;
}

/* Negate the elements of the M x N matrix MAT1 and store it in MAT2.  */

void
lambda_matrix_negate (lambda_matrix mat1, lambda_matrix mat2, int m, int n)
{
  int i;

  for (i = 0; i < m; i++)
    lambda_vector_negate (mat1[i], mat2[i], n);
}

/* Take the transpose of matrix MAT1 and store it in MAT2.
   MAT1 is an M x N matrix, so MAT2 must be N x M.  */

void
lambda_matrix_transpose (lambda_matrix mat1, lambda_matrix mat2, int m, int n)
{
  int i, j;

  for (i = 0; i < n; i++)
    for (j = 0; j < m; j++)
      mat2[i][j] = mat1[j][i];
}


/* Add two M x N matrices together: MAT3 = MAT1+MAT2.  */

void
lambda_matrix_add (lambda_matrix mat1, lambda_matrix mat2,
		   lambda_matrix mat3, int m, int n)
{
  int i;

  for (i = 0; i < m; i++)
    lambda_vector_add (mat1[i], mat2[i], mat3[i], n);
}

/* MAT3 = CONST1 * MAT1 + CONST2 * MAT2.  All matrices are M x N.  */

void
lambda_matrix_add_mc (lambda_matrix mat1, int const1,
		      lambda_matrix mat2, int const2,
		      lambda_matrix mat3, int m, int n)
{
  int i;

  for (i = 0; i < m; i++)
    lambda_vector_add_mc (mat1[i], const1, mat2[i], const2, mat3[i], n);
}

/* Multiply two matrices: MAT3 = MAT1 * MAT2.
   MAT1 is an M x R matrix, and MAT2 is R x N.  The resulting MAT2
   must therefore be M x N.  */

void
lambda_matrix_mult (lambda_matrix mat1, lambda_matrix mat2,
		    lambda_matrix mat3, int m, int r, int n)
{

  int i, j, k;

  for (i = 0; i < m; i++)
    {
      for (j = 0; j < n; j++)
	{
	  mat3[i][j] = 0;
	  for (k = 0; k < r; k++)
	    mat3[i][j] += mat1[i][k] * mat2[k][j];
	}
    }
}

/* Get column COL from the matrix MAT and store it in VEC.  MAT has
   N rows, so the length of VEC must be N.  */

static void
lambda_matrix_get_column (lambda_matrix mat, int n, int col,
			  lambda_vector vec)
{
  int i;

  for (i = 0; i < n; i++)
    vec[i] = mat[i][col];
}

/* Delete rows r1 to r2 (not including r2).  */

void
lambda_matrix_delete_rows (lambda_matrix mat, int rows, int from, int to)
{
  int i;
  int dist;
  dist = to - from;

  for (i = to; i < rows; i++)
    mat[i - dist] = mat[i];

  for (i = rows - dist; i < rows; i++)
    mat[i] = NULL;
}

/* Swap rows R1 and R2 in matrix MAT.  */

void
lambda_matrix_row_exchange (lambda_matrix mat, int r1, int r2)
{
  lambda_vector row;

  row = mat[r1];
  mat[r1] = mat[r2];
  mat[r2] = row;
}

/* Add a multiple of row R1 of matrix MAT with N columns to row R2:
   R2 = R2 + CONST1 * R1.  */

void
lambda_matrix_row_add (lambda_matrix mat, int n, int r1, int r2, int const1)
{
  int i;

  if (const1 == 0)
    return;

  for (i = 0; i < n; i++)
    mat[r2][i] += const1 * mat[r1][i];
}

/* Negate row R1 of matrix MAT which has N columns.  */

void
lambda_matrix_row_negate (lambda_matrix mat, int n, int r1)
{
  lambda_vector_negate (mat[r1], mat[r1], n);
}

/* Multiply row R1 of matrix MAT with N columns by CONST1.  */

void
lambda_matrix_row_mc (lambda_matrix mat, int n, int r1, int const1)
{
  int i;

  for (i = 0; i < n; i++)
    mat[r1][i] *= const1;
}

/* Exchange COL1 and COL2 in matrix MAT. M is the number of rows.  */

void
lambda_matrix_col_exchange (lambda_matrix mat, int m, int col1, int col2)
{
  int i;
  int tmp;
  for (i = 0; i < m; i++)
    {
      tmp = mat[i][col1];
      mat[i][col1] = mat[i][col2];
      mat[i][col2] = tmp;
    }
}

/* Add a multiple of column C1 of matrix MAT with M rows to column C2:
   C2 = C2 + CONST1 * C1.  */

void
lambda_matrix_col_add (lambda_matrix mat, int m, int c1, int c2, int const1)
{
  int i;

  if (const1 == 0)
    return;

  for (i = 0; i < m; i++)
    mat[i][c2] += const1 * mat[i][c1];
}

/* Negate column C1 of matrix MAT which has M rows.  */

void
lambda_matrix_col_negate (lambda_matrix mat, int m, int c1)
{
  int i;

  for (i = 0; i < m; i++)
    mat[i][c1] *= -1;
}

/* Multiply column C1 of matrix MAT with M rows by CONST1.  */

void
lambda_matrix_col_mc (lambda_matrix mat, int m, int c1, int const1)
{
  int i;

  for (i = 0; i < m; i++)
    mat[i][c1] *= const1;
}

/* Compute the inverse of the N x N matrix MAT and store it in INV.

   We don't _really_ compute the inverse of MAT.  Instead we compute
   det(MAT)*inv(MAT), and we return det(MAT) to the caller as the function
   result.  This is necessary to preserve accuracy, because we are dealing
   with integer matrices here.

   The algorithm used here is a column based Gauss-Jordan elimination on MAT
   and the identity matrix in parallel.  The inverse is the result of applying
   the same operations on the identity matrix that reduce MAT to the identity
   matrix.

   When MAT is a 2 x 2 matrix, we don't go through the whole process, because
   it is easily inverted by inspection and it is a very common case.  */

static int lambda_matrix_inverse_hard (lambda_matrix, lambda_matrix, int);

int
lambda_matrix_inverse (lambda_matrix mat, lambda_matrix inv, int n)
{
  if (n == 2)
    {
      int a, b, c, d, det;
      a = mat[0][0];
      b = mat[1][0];
      c = mat[0][1];
      d = mat[1][1];      
      inv[0][0] =  d;
      inv[0][1] = -c;
      inv[1][0] = -b;
      inv[1][1] =  a;
      det = (a * d - b * c);
      if (det < 0)
	{
	  det *= -1;
	  inv[0][0] *= -1;
	  inv[1][0] *= -1;
	  inv[0][1] *= -1;
	  inv[1][1] *= -1;
	}
      return det;
    }
  else
    return lambda_matrix_inverse_hard (mat, inv, n);
}

/* If MAT is not a special case, invert it the hard way.  */

static int
lambda_matrix_inverse_hard (lambda_matrix mat, lambda_matrix inv, int n)
{
  lambda_vector row;
  lambda_matrix temp;
  int i, j;
  int determinant;

  temp = lambda_matrix_new (n, n);
  lambda_matrix_copy (mat, temp, n, n);
  lambda_matrix_id (inv, n);

  /* Reduce TEMP to a lower triangular form, applying the same operations on
     INV which starts as the identity matrix.  N is the number of rows and
     columns.  */
  for (j = 0; j < n; j++)
    {
      row = temp[j];

      /* Make every element in the current row positive.  */
      for (i = j; i < n; i++)
	if (row[i] < 0)
	  {
	    lambda_matrix_col_negate (temp, n, i);
	    lambda_matrix_col_negate (inv, n, i);
	  }

      /* Sweep the upper triangle.  Stop when only the diagonal element in the
	 current row is nonzero.  */
      while (lambda_vector_first_nz (row, n, j + 1) < n)
	{
	  int min_col = lambda_vector_min_nz (row, n, j);
	  lambda_matrix_col_exchange (temp, n, j, min_col);
	  lambda_matrix_col_exchange (inv, n, j, min_col);

	  for (i = j + 1; i < n; i++)
	    {
	      int factor;

	      factor = -1 * row[i];
	      if (row[j] != 1)
		factor /= row[j];

	      lambda_matrix_col_add (temp, n, j, i, factor);
	      lambda_matrix_col_add (inv, n, j, i, factor);
	    }
	}
    }

  /* Reduce TEMP from a lower triangular to the identity matrix.  Also compute
     the determinant, which now is simply the product of the elements on the
     diagonal of TEMP.  If one of these elements is 0, the matrix has 0 as an
     eigenvalue so it is singular and hence not invertible.  */
  determinant = 1;
  for (j = n - 1; j >= 0; j--)
    {
      int diagonal;

      row = temp[j];
      diagonal = row[j];

      /* The matrix must not be singular.  */
      gcc_assert (diagonal);

      determinant = determinant * diagonal;

      /* If the diagonal is not 1, then multiply the each row by the
         diagonal so that the middle number is now 1, rather than a
         rational.  */
      if (diagonal != 1)
	{
	  for (i = 0; i < j; i++)
	    lambda_matrix_col_mc (inv, n, i, diagonal);
	  for (i = j + 1; i < n; i++)
	    lambda_matrix_col_mc (inv, n, i, diagonal);

	  row[j] = diagonal = 1;
	}

      /* Sweep the lower triangle column wise.  */
      for (i = j - 1; i >= 0; i--)
	{
	  if (row[i])
	    {
	      int factor = -row[i];
	      lambda_matrix_col_add (temp, n, j, i, factor);
	      lambda_matrix_col_add (inv, n, j, i, factor);
	    }

	}
    }

  return determinant;
}

/* Decompose a N x N matrix MAT to a product of a lower triangular H
   and a unimodular U matrix such that MAT = H.U.  N is the size of
   the rows of MAT.  */

void
lambda_matrix_hermite (lambda_matrix mat, int n,
		       lambda_matrix H, lambda_matrix U)
{
  lambda_vector row;
  int i, j, factor, minimum_col;

  lambda_matrix_copy (mat, H, n, n);
  lambda_matrix_id (U, n);

  for (j = 0; j < n; j++)
    {
      row = H[j];

      /* Make every element of H[j][j..n] positive.  */
      for (i = j; i < n; i++)
	{
	  if (row[i] < 0)
	    {
	      lambda_matrix_col_negate (H, n, i);
	      lambda_vector_negate (U[i], U[i], n);
	    }
	}

      /* Stop when only the diagonal element is nonzero.  */
      while (lambda_vector_first_nz (row, n, j + 1) < n)
	{
	  minimum_col = lambda_vector_min_nz (row, n, j);
	  lambda_matrix_col_exchange (H, n, j, minimum_col);
	  lambda_matrix_row_exchange (U, j, minimum_col);

	  for (i = j + 1; i < n; i++)
	    {
	      factor = row[i] / row[j];
	      lambda_matrix_col_add (H, n, j, i, -1 * factor);
	      lambda_matrix_row_add (U, n, i, j, factor);
	    }
	}
    }
}

/* Given an M x N integer matrix A, this function determines an M x
   M unimodular matrix U, and an M x N echelon matrix S such that
   "U.A = S".  This decomposition is also known as "right Hermite".
   
   Ref: Algorithm 2.1 page 33 in "Loop Transformations for
   Restructuring Compilers" Utpal Banerjee.  */

void
lambda_matrix_right_hermite (lambda_matrix A, int m, int n,
			     lambda_matrix S, lambda_matrix U)
{
  int i, j, i0 = 0;

  lambda_matrix_copy (A, S, m, n);
  lambda_matrix_id (U, m);

  for (j = 0; j < n; j++)
    {
      if (lambda_vector_first_nz (S[j], m, i0) < m)
	{
	  ++i0;
	  for (i = m - 1; i >= i0; i--)
	    {
	      while (S[i][j] != 0)
		{
		  int sigma, factor, a, b;

		  a = S[i-1][j];
		  b = S[i][j];
		  sigma = (a * b < 0) ? -1: 1;
		  a = abs (a);
		  b = abs (b);
		  factor = sigma * (a / b);

		  lambda_matrix_row_add (S, n, i, i-1, -factor);
		  lambda_matrix_row_exchange (S, i, i-1);

		  lambda_matrix_row_add (U, m, i, i-1, -factor);
		  lambda_matrix_row_exchange (U, i, i-1);
		}
	    }
	}
    }
}

/* Given an M x N integer matrix A, this function determines an M x M
   unimodular matrix V, and an M x N echelon matrix S such that "A =
   V.S".  This decomposition is also known as "left Hermite".
   
   Ref: Algorithm 2.2 page 36 in "Loop Transformations for
   Restructuring Compilers" Utpal Banerjee.  */

void
lambda_matrix_left_hermite (lambda_matrix A, int m, int n,
			     lambda_matrix S, lambda_matrix V)
{
  int i, j, i0 = 0;

  lambda_matrix_copy (A, S, m, n);
  lambda_matrix_id (V, m);

  for (j = 0; j < n; j++)
    {
      if (lambda_vector_first_nz (S[j], m, i0) < m)
	{
	  ++i0;
	  for (i = m - 1; i >= i0; i--)
	    {
	      while (S[i][j] != 0)
		{
		  int sigma, factor, a, b;

		  a = S[i-1][j];
		  b = S[i][j];
		  sigma = (a * b < 0) ? -1: 1;
		  a = abs (a);
      b = abs (b);
		  factor = sigma * (a / b);

		  lambda_matrix_row_add (S, n, i, i-1, -factor);
		  lambda_matrix_row_exchange (S, i, i-1);

		  lambda_matrix_col_add (V, m, i-1, i, factor);
		  lambda_matrix_col_exchange (V, m, i, i-1);
		}
	    }
	}
    }
}

/* When it exists, return the first nonzero row in MAT after row
   STARTROW.  Otherwise return rowsize.  */

int
lambda_matrix_first_nz_vec (lambda_matrix mat, int rowsize, int colsize,
			    int startrow)
{
  int j;
  bool found = false;

  for (j = startrow; (j < rowsize) && !found; j++)
    {
      if ((mat[j] != NULL)
	  && (lambda_vector_first_nz (mat[j], colsize, startrow) < colsize))
	found = true;
    }

  if (found)
    return j - 1;
  return rowsize;
}

/* Calculate the projection of E sub k to the null space of B.  */

void
lambda_matrix_project_to_null (lambda_matrix B, int rowsize,
			       int colsize, int k, lambda_vector x)
{
  lambda_matrix M1, M2, M3, I;
  int determinant;

  /* Compute c(I-B^T inv(B B^T) B) e sub k.  */

  /* M1 is the transpose of B.  */
  M1 = lambda_matrix_new (colsize, colsize);
  lambda_matrix_transpose (B, M1, rowsize, colsize);

  /* M2 = B * B^T */
  M2 = lambda_matrix_new (colsize, colsize);
  lambda_matrix_mult (B, M1, M2, rowsize, colsize, rowsize);

  /* M3 = inv(M2) */
  M3 = lambda_matrix_new (colsize, colsize);
  determinant = lambda_matrix_inverse (M2, M3, rowsize);

  /* M2 = B^T (inv(B B^T)) */
  lambda_matrix_mult (M1, M3, M2, colsize, rowsize, rowsize);

  /* M1 = B^T (inv(B B^T)) B */
  lambda_matrix_mult (M2, B, M1, colsize, rowsize, colsize);
  lambda_matrix_negate (M1, M1, colsize, colsize);

  I = lambda_matrix_new (colsize, colsize);
  lambda_matrix_id (I, colsize);

  lambda_matrix_add_mc (I, determinant, M1, 1, M2, colsize, colsize);

  lambda_matrix_get_column (M2, colsize, k - 1, x);

}

/* Multiply a vector VEC by a matrix MAT.
   MAT is an M*N matrix, and VEC is a vector with length N.  The result
   is stored in DEST which must be a vector of length M.  */

void
lambda_matrix_vector_mult (lambda_matrix matrix, int m, int n,
			   lambda_vector vec, lambda_vector dest)
{
  int i, j;

  lambda_vector_clear (dest, m);
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
      dest[i] += matrix[i][j] * vec[j];
}

/* Print out an M x N matrix MAT to OUTFILE.  */

void
print_lambda_matrix (FILE * outfile, lambda_matrix matrix, int m, int n)
{
  int i;

  for (i = 0; i < m; i++)
    print_lambda_vector (outfile, matrix[i], n);
  fprintf (outfile, "\n");
}

