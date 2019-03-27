/* Simple data type for positive real numbers for the GNU compiler.
   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.

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

/* This library supports positive real numbers and 0;
   inf and nan are NOT supported.
   It is written to be simple and fast.

   Value of sreal is
	x = sig * 2 ^ exp
   where
	sig = significant
	  (for < 64-bit machines sig = sig_lo + sig_hi * 2 ^ SREAL_PART_BITS)
	exp = exponent

   One HOST_WIDE_INT is used for the significant on 64-bit (and more than
   64-bit) machines,
   otherwise two HOST_WIDE_INTs are used for the significant.
   Only a half of significant bits is used (in normalized sreals) so that we do
   not have problems with overflow, for example when c->sig = a->sig * b->sig.
   So the precision for 64-bit and 32-bit machines is 32-bit.

   Invariant: The numbers are normalized before and after each call of sreal_*.

   Normalized sreals:
   All numbers (except zero) meet following conditions:
	 SREAL_MIN_SIG <= sig && sig <= SREAL_MAX_SIG
	-SREAL_MAX_EXP <= exp && exp <= SREAL_MAX_EXP

   If the number would be too large, it is set to upper bounds of these
   conditions.

   If the number is zero or would be too small it meets following conditions:
	sig == 0 && exp == -SREAL_MAX_EXP
*/

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "sreal.h"

static inline void copy (sreal *, sreal *);
static inline void shift_right (sreal *, int);
static void normalize (sreal *);

/* Print the content of struct sreal.  */

void
dump_sreal (FILE *file, sreal *x)
{
#if SREAL_PART_BITS < 32
  fprintf (file, "((" HOST_WIDE_INT_PRINT_UNSIGNED " * 2^16 + "
	   HOST_WIDE_INT_PRINT_UNSIGNED ") * 2^%d)",
	   x->sig_hi, x->sig_lo, x->exp);
#else
  fprintf (file, "(" HOST_WIDE_INT_PRINT_UNSIGNED " * 2^%d)", x->sig, x->exp);
#endif
}

/* Copy the sreal number.  */

static inline void
copy (sreal *r, sreal *a)
{
#if SREAL_PART_BITS < 32
  r->sig_lo = a->sig_lo;
  r->sig_hi = a->sig_hi;
#else
  r->sig = a->sig;
#endif
  r->exp = a->exp;
}

/* Shift X right by S bits.  Needed: 0 < S <= SREAL_BITS.
   When the most significant bit shifted out is 1, add 1 to X (rounding).  */

static inline void
shift_right (sreal *x, int s)
{
  gcc_assert (s > 0);
  gcc_assert (s <= SREAL_BITS);
  /* Exponent should never be so large because shift_right is used only by
     sreal_add and sreal_sub ant thus the number cannot be shifted out from
     exponent range.  */
  gcc_assert (x->exp + s <= SREAL_MAX_EXP);

  x->exp += s;

#if SREAL_PART_BITS < 32
  if (s > SREAL_PART_BITS)
    {
      s -= SREAL_PART_BITS;
      x->sig_hi += (uhwi) 1 << (s - 1);
      x->sig_lo = x->sig_hi >> s;
      x->sig_hi = 0;
    }
  else
    {
      x->sig_lo += (uhwi) 1 << (s - 1);
      if (x->sig_lo & ((uhwi) 1 << SREAL_PART_BITS))
	{
	  x->sig_hi++;
	  x->sig_lo -= (uhwi) 1 << SREAL_PART_BITS;
	}
      x->sig_lo >>= s;
      x->sig_lo |= (x->sig_hi & (((uhwi) 1 << s) - 1)) << (SREAL_PART_BITS - s);
      x->sig_hi >>= s;
    }
#else
  x->sig += (uhwi) 1 << (s - 1);
  x->sig >>= s;
#endif
}

/* Normalize *X.  */

static void
normalize (sreal *x)
{
#if SREAL_PART_BITS < 32
  int shift;
  HOST_WIDE_INT mask;

  if (x->sig_lo == 0 && x->sig_hi == 0)
    {
      x->exp = -SREAL_MAX_EXP;
    }
  else if (x->sig_hi < SREAL_MIN_SIG)
    {
      if (x->sig_hi == 0)
	{
	  /* Move lower part of significant to higher part.  */
	  x->sig_hi = x->sig_lo;
	  x->sig_lo = 0;
	  x->exp -= SREAL_PART_BITS;
	}
      shift = 0;
      while (x->sig_hi < SREAL_MIN_SIG)
	{
	  x->sig_hi <<= 1;
	  x->exp--;
	  shift++;
	}
      /* Check underflow.  */
      if (x->exp < -SREAL_MAX_EXP)
	{
	  x->exp = -SREAL_MAX_EXP;
	  x->sig_hi = 0;
	  x->sig_lo = 0;
	}
      else if (shift)
	{
	  mask = (1 << SREAL_PART_BITS) - (1 << (SREAL_PART_BITS - shift));
	  x->sig_hi |= (x->sig_lo & mask) >> (SREAL_PART_BITS - shift);
	  x->sig_lo = (x->sig_lo << shift) & (((uhwi) 1 << SREAL_PART_BITS) - 1);
	}
    }
  else if (x->sig_hi > SREAL_MAX_SIG)
    {
      unsigned HOST_WIDE_INT tmp = x->sig_hi;

      /* Find out how many bits will be shifted.  */
      shift = 0;
      do
	{
	  tmp >>= 1;
	  shift++;
	}
      while (tmp > SREAL_MAX_SIG);

      /* Round the number.  */
      x->sig_lo += (uhwi) 1 << (shift - 1);

      x->sig_lo >>= shift;
      x->sig_lo += ((x->sig_hi & (((uhwi) 1 << shift) - 1))
		    << (SREAL_PART_BITS - shift));
      x->sig_hi >>= shift;
      x->exp += shift;
      if (x->sig_lo & ((uhwi) 1 << SREAL_PART_BITS))
	{
	  x->sig_lo -= (uhwi) 1 << SREAL_PART_BITS;
	  x->sig_hi++;
	  if (x->sig_hi > SREAL_MAX_SIG)
	    {
	      /* x->sig_hi was SREAL_MAX_SIG before increment
		 so now last bit is zero.  */
	      x->sig_hi >>= 1;
	      x->sig_lo >>= 1;
	      x->exp++;
	    }
	}

      /* Check overflow.  */
      if (x->exp > SREAL_MAX_EXP)
	{
	  x->exp = SREAL_MAX_EXP;
	  x->sig_hi = SREAL_MAX_SIG;
	  x->sig_lo = SREAL_MAX_SIG;
	}
    }
#else
  if (x->sig == 0)
    {
      x->exp = -SREAL_MAX_EXP;
    }
  else if (x->sig < SREAL_MIN_SIG)
    {
      do
	{
	  x->sig <<= 1;
	  x->exp--;
	}
      while (x->sig < SREAL_MIN_SIG);

      /* Check underflow.  */
      if (x->exp < -SREAL_MAX_EXP)
	{
	  x->exp = -SREAL_MAX_EXP;
	  x->sig = 0;
	}
    }
  else if (x->sig > SREAL_MAX_SIG)
    {
      int last_bit;
      do
	{
	  last_bit = x->sig & 1;
	  x->sig >>= 1;
	  x->exp++;
	}
      while (x->sig > SREAL_MAX_SIG);

      /* Round the number.  */
      x->sig += last_bit;
      if (x->sig > SREAL_MAX_SIG)
	{
	  x->sig >>= 1;
	  x->exp++;
	}

      /* Check overflow.  */
      if (x->exp > SREAL_MAX_EXP)
	{
	  x->exp = SREAL_MAX_EXP;
	  x->sig = SREAL_MAX_SIG;
	}
    }
#endif
}

/* Set *R to SIG * 2 ^ EXP.  Return R.  */

sreal *
sreal_init (sreal *r, unsigned HOST_WIDE_INT sig, signed int exp)
{
#if SREAL_PART_BITS < 32
  r->sig_lo = 0;
  r->sig_hi = sig;
  r->exp = exp - 16;
#else
  r->sig = sig;
  r->exp = exp;
#endif
  normalize (r);
  return r;
}

/* Return integer value of *R.  */

HOST_WIDE_INT
sreal_to_int (sreal *r)
{
#if SREAL_PART_BITS < 32
  if (r->exp <= -SREAL_BITS)
    return 0;
  if (r->exp >= 0)
    return MAX_HOST_WIDE_INT;
  return ((r->sig_hi << SREAL_PART_BITS) + r->sig_lo) >> -r->exp;
#else
  if (r->exp <= -SREAL_BITS)
    return 0;
  if (r->exp >= SREAL_PART_BITS)
    return MAX_HOST_WIDE_INT;
  if (r->exp > 0)
    return r->sig << r->exp;
  if (r->exp < 0)
    return r->sig >> -r->exp;
  return r->sig;
#endif
}

/* Compare *A and *B. Return -1 if *A < *B, 1 if *A > *B and 0 if *A == *B.  */

int
sreal_compare (sreal *a, sreal *b)
{
  if (a->exp > b->exp)
    return 1;
  if (a->exp < b->exp)
    return -1;
#if SREAL_PART_BITS < 32
  if (a->sig_hi > b->sig_hi)
    return 1;
  if (a->sig_hi < b->sig_hi)
    return -1;
  if (a->sig_lo > b->sig_lo)
    return 1;
  if (a->sig_lo < b->sig_lo)
    return -1;
#else
  if (a->sig > b->sig)
    return 1;
  if (a->sig < b->sig)
    return -1;
#endif
  return 0;
}

/* *R = *A + *B.  Return R.  */

sreal *
sreal_add (sreal *r, sreal *a, sreal *b)
{
  int dexp;
  sreal tmp;
  sreal *bb;

  if (sreal_compare (a, b) < 0)
    {
      sreal *swap;
      swap = a;
      a = b;
      b = swap;
    }

  dexp = a->exp - b->exp;
  r->exp = a->exp;
  if (dexp > SREAL_BITS)
    {
#if SREAL_PART_BITS < 32
      r->sig_hi = a->sig_hi;
      r->sig_lo = a->sig_lo;
#else
      r->sig = a->sig;
#endif
      return r;
    }

  if (dexp == 0)
    bb = b;
  else
    {
      copy (&tmp, b);
      shift_right (&tmp, dexp);
      bb = &tmp;
    }

#if SREAL_PART_BITS < 32
  r->sig_hi = a->sig_hi + bb->sig_hi;
  r->sig_lo = a->sig_lo + bb->sig_lo;
  if (r->sig_lo & ((uhwi) 1 << SREAL_PART_BITS))
    {
      r->sig_hi++;
      r->sig_lo -= (uhwi) 1 << SREAL_PART_BITS;
    }
#else
  r->sig = a->sig + bb->sig;
#endif
  normalize (r);
  return r;
}

/* *R = *A - *B.  Return R.  */

sreal *
sreal_sub (sreal *r, sreal *a, sreal *b)
{
  int dexp;
  sreal tmp;
  sreal *bb;

  gcc_assert (sreal_compare (a, b) >= 0);

  dexp = a->exp - b->exp;
  r->exp = a->exp;
  if (dexp > SREAL_BITS)
    {
#if SREAL_PART_BITS < 32
      r->sig_hi = a->sig_hi;
      r->sig_lo = a->sig_lo;
#else
      r->sig = a->sig;
#endif
      return r;
    }
  if (dexp == 0)
    bb = b;
  else
    {
      copy (&tmp, b);
      shift_right (&tmp, dexp);
      bb = &tmp;
    }

#if SREAL_PART_BITS < 32
  if (a->sig_lo < bb->sig_lo)
    {
      r->sig_hi = a->sig_hi - bb->sig_hi - 1;
      r->sig_lo = a->sig_lo + ((uhwi) 1 << SREAL_PART_BITS) - bb->sig_lo;
    }
  else
    {
      r->sig_hi = a->sig_hi - bb->sig_hi;
      r->sig_lo = a->sig_lo - bb->sig_lo;
    }
#else
  r->sig = a->sig - bb->sig;
#endif
  normalize (r);
  return r;
}

/* *R = *A * *B.  Return R.  */

sreal *
sreal_mul (sreal *r, sreal *a, sreal *b)
{
#if SREAL_PART_BITS < 32
  if (a->sig_hi < SREAL_MIN_SIG || b->sig_hi < SREAL_MIN_SIG)
    {
      r->sig_lo = 0;
      r->sig_hi = 0;
      r->exp = -SREAL_MAX_EXP;
    }
  else
    {
      unsigned HOST_WIDE_INT tmp1, tmp2, tmp3;
      if (sreal_compare (a, b) < 0)
	{
	  sreal *swap;
	  swap = a;
	  a = b;
	  b = swap;
	}

      r->exp = a->exp + b->exp + SREAL_PART_BITS;

      tmp1 = a->sig_lo * b->sig_lo;
      tmp2 = a->sig_lo * b->sig_hi;
      tmp3 = a->sig_hi * b->sig_lo + (tmp1 >> SREAL_PART_BITS);

      r->sig_hi = a->sig_hi * b->sig_hi;
      r->sig_hi += (tmp2 >> SREAL_PART_BITS) + (tmp3 >> SREAL_PART_BITS);
      tmp2 &= ((uhwi) 1 << SREAL_PART_BITS) - 1;
      tmp3 &= ((uhwi) 1 << SREAL_PART_BITS) - 1;
      tmp1 = tmp2 + tmp3;

      r->sig_lo = tmp1 & (((uhwi) 1 << SREAL_PART_BITS) - 1);
      r->sig_hi += tmp1 >> SREAL_PART_BITS;

      normalize (r);
    }
#else
  if (a->sig < SREAL_MIN_SIG || b->sig < SREAL_MIN_SIG)
    {
      r->sig = 0;
      r->exp = -SREAL_MAX_EXP;
    }
  else
    {
      r->sig = a->sig * b->sig;
      r->exp = a->exp + b->exp;
      normalize (r);
    }
#endif
  return r;
}

/* *R = *A / *B.  Return R.  */

sreal *
sreal_div (sreal *r, sreal *a, sreal *b)
{
#if SREAL_PART_BITS < 32
  unsigned HOST_WIDE_INT tmp, tmp1, tmp2;

  gcc_assert (b->sig_hi >= SREAL_MIN_SIG);
  if (a->sig_hi < SREAL_MIN_SIG)
    {
      r->sig_hi = 0;
      r->sig_lo = 0;
      r->exp = -SREAL_MAX_EXP;
    }
  else
    {
      /* Since division by the whole number is pretty ugly to write
	 we are dividing by first 3/4 of bits of number.  */

      tmp1 = (a->sig_hi << SREAL_PART_BITS) + a->sig_lo;
      tmp2 = ((b->sig_hi << (SREAL_PART_BITS / 2))
	      + (b->sig_lo >> (SREAL_PART_BITS / 2)));
      if (b->sig_lo & ((uhwi) 1 << ((SREAL_PART_BITS / 2) - 1)))
	tmp2++;

      r->sig_lo = 0;
      tmp = tmp1 / tmp2;
      tmp1 = (tmp1 % tmp2) << (SREAL_PART_BITS / 2);
      r->sig_hi = tmp << SREAL_PART_BITS;

      tmp = tmp1 / tmp2;
      tmp1 = (tmp1 % tmp2) << (SREAL_PART_BITS / 2);
      r->sig_hi += tmp << (SREAL_PART_BITS / 2);

      tmp = tmp1 / tmp2;
      r->sig_hi += tmp;

      r->exp = a->exp - b->exp - SREAL_BITS - SREAL_PART_BITS / 2;
      normalize (r);
    }
#else
  gcc_assert (b->sig != 0);
  r->sig = (a->sig << SREAL_PART_BITS) / b->sig;
  r->exp = a->exp - b->exp - SREAL_PART_BITS;
  normalize (r);
#endif
  return r;
}
