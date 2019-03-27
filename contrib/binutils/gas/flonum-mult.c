/* flonum_mult.c - multiply two flonums
   Copyright 1987, 1990, 1991, 1992, 1995, 2000, 2002, 2003
   Free Software Foundation, Inc.

   This file is part of Gas, the GNU Assembler.

   The GNU assembler is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY.  No author or distributor
   accepts responsibility to anyone for the consequences of using it
   or for whether it serves any particular purpose or works at all,
   unless he says so in writing.  Refer to the GNU Assembler General
   Public License for full details.

   Everyone is granted permission to copy, modify and redistribute
   the GNU Assembler, but only under the conditions described in the
   GNU Assembler General Public License.  A copy of this license is
   supposed to have been given to you along with the GNU Assembler
   so you can know your rights and responsibilities.  It should be
   in a file named COPYING.  Among other things, the copyright
   notice and this notice must be preserved on all copies.  */

#include "ansidecl.h"
#include "flonum.h"

/*	plan for a . b => p(roduct)

	+-------+-------+-/   /-+-------+-------+
	| a	| a	|  ...	| a	| a	|
	|  A	|  A-1	|	|  1	|  0	|
	+-------+-------+-/   /-+-------+-------+

	+-------+-------+-/   /-+-------+-------+
	| b	| b	|  ...	| b	| b	|
	|  B	|  B-1	|	|  1	|  0	|
	+-------+-------+-/   /-+-------+-------+

	+-------+-------+-/   /-+-------+-/   /-+-------+-------+
	| p	| p	|  ...	| p	|  ...	| p	| p	|
	|  A+B+1|  A+B	|	|  N	|	|  1	|  0	|
	+-------+-------+-/   /-+-------+-/   /-+-------+-------+

	/^\
	(carry) a .b	   ...	    |	   ...	 a .b	 a .b
	A  B 		    |		  0  1	  0  0
	|
	...	    |	   ...	 a .b
	|		  1  0
	|
	|	   ...
	|
	|
	|
	|		  ___
	|		  \
	+-----  P  =   >  a .b
	N	  /__  i  j

	N = 0 ... A+B

	for all i,j where i+j=N
	[i,j integers > 0]

	a[], b[], p[] may not intersect.
	Zero length factors signify 0 significant bits: treat as 0.0.
	0.0 factors do the right thing.
	Zero length product OK.

	I chose the ForTran accent "foo[bar]" instead of the C accent "*garply"
	because I felt the ForTran way was more intuitive. The C way would
	probably yield better code on most C compilers. Dean Elsner.
	(C style also gives deeper insight [to me] ... oh well ...)  */

void
flonum_multip (const FLONUM_TYPE *a, const FLONUM_TYPE *b,
	       FLONUM_TYPE *product)
{
  int size_of_a;		/* 0 origin  */
  int size_of_b;		/* 0 origin  */
  int size_of_product;		/* 0 origin  */
  int size_of_sum;		/* 0 origin  */
  int extra_product_positions;	/* 1 origin  */
  unsigned long work;
  unsigned long carry;
  long exponent;
  LITTLENUM_TYPE *q;
  long significant;		/* TRUE when we emit a non-0 littlenum  */
  /* ForTran accent follows.  */
  int P;			/* Scan product low-order -> high.  */
  int N;			/* As in sum above.  */
  int A;			/* Which [] of a?  */
  int B;			/* Which [] of b?  */

  if ((a->sign != '-' && a->sign != '+')
      || (b->sign != '-' && b->sign != '+'))
    {
      /* Got to fail somehow.  Any suggestions?  */
      product->sign = 0;
      return;
    }
  product->sign = (a->sign == b->sign) ? '+' : '-';
  size_of_a = a->leader - a->low;
  size_of_b = b->leader - b->low;
  exponent = a->exponent + b->exponent;
  size_of_product = product->high - product->low;
  size_of_sum = size_of_a + size_of_b;
  extra_product_positions = size_of_product - size_of_sum;
  if (extra_product_positions < 0)
    {
      P = extra_product_positions;	/* P < 0  */
      exponent -= extra_product_positions;	/* Increases exponent.  */
    }
  else
    {
      P = 0;
    }
  carry = 0;
  significant = 0;
  for (N = 0; N <= size_of_sum; N++)
    {
      work = carry;
      carry = 0;
      for (A = 0; A <= N; A++)
	{
	  B = N - A;
	  if (A <= size_of_a && B <= size_of_b && B >= 0)
	    {
#ifdef TRACE
	      printf ("a:low[%d.]=%04x b:low[%d.]=%04x work_before=%08x\n",
		      A, a->low[A], B, b->low[B], work);
#endif
	      /* Watch out for sign extension!  Without the casts, on
		 the DEC Alpha, the multiplication result is *signed*
		 int, which gets sign-extended to convert to the
		 unsigned long!  */
	      work += (unsigned long) a->low[A] * (unsigned long) b->low[B];
	      carry += work >> LITTLENUM_NUMBER_OF_BITS;
	      work &= LITTLENUM_MASK;
#ifdef TRACE
	      printf ("work=%08x carry=%04x\n", work, carry);
#endif
	    }
	}
      significant |= work;
      if (significant || P < 0)
	{
	  if (P >= 0)
	    {
	      product->low[P] = work;
#ifdef TRACE
	      printf ("P=%d. work[p]:=%04x\n", P, work);
#endif
	    }
	  P++;
	}
      else
	{
	  extra_product_positions++;
	  exponent++;
	}
    }
  /* [P]-> position # size_of_sum + 1.
     This is where 'carry' should go.  */
#ifdef TRACE
  printf ("final carry =%04x\n", carry);
#endif
  if (carry)
    {
      if (extra_product_positions > 0)
	product->low[P] = carry;
      else
	{
	  /* No room at high order for carry littlenum.  */
	  /* Shift right 1 to make room for most significant littlenum.  */
	  exponent++;
	  P--;
	  for (q = product->low + P; q >= product->low; q--)
	    {
	      work = *q;
	      *q = carry;
	      carry = work;
	    }
	}
    }
  else
    P--;
  product->leader = product->low + P;
  product->exponent = exponent;
}
