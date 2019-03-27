/* real.c - software floating point emulation.
   Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Stephen L. Moshier (moshier@world.std.com).
   Re-written by Richard Henderson <rth@redhat.com>

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
#include "tree.h"
#include "toplev.h"
#include "real.h"
#include "tm_p.h"
#include "dfp.h"

/* The floating point model used internally is not exactly IEEE 754
   compliant, and close to the description in the ISO C99 standard,
   section 5.2.4.2.2 Characteristics of floating types.

   Specifically

	x = s * b^e * \sum_{k=1}^p f_k * b^{-k}

	where
		s = sign (+- 1)
		b = base or radix, here always 2
		e = exponent
		p = precision (the number of base-b digits in the significand)
		f_k = the digits of the significand.

   We differ from typical IEEE 754 encodings in that the entire
   significand is fractional.  Normalized significands are in the
   range [0.5, 1.0).

   A requirement of the model is that P be larger than the largest
   supported target floating-point type by at least 2 bits.  This gives
   us proper rounding when we truncate to the target type.  In addition,
   E must be large enough to hold the smallest supported denormal number
   in a normalized form.

   Both of these requirements are easily satisfied.  The largest target
   significand is 113 bits; we store at least 160.  The smallest
   denormal number fits in 17 exponent bits; we store 27.

   Note that the decimal string conversion routines are sensitive to
   rounding errors.  Since the raw arithmetic routines do not themselves
   have guard digits or rounding, the computation of 10**exp can
   accumulate more than a few digits of error.  The previous incarnation
   of real.c successfully used a 144-bit fraction; given the current
   layout of REAL_VALUE_TYPE we're forced to expand to at least 160 bits.

   Target floating point models that use base 16 instead of base 2
   (i.e. IBM 370), are handled during round_for_format, in which we
   canonicalize the exponent to be a multiple of 4 (log2(16)), and
   adjust the significand to match.  */


/* Used to classify two numbers simultaneously.  */
#define CLASS2(A, B)  ((A) << 2 | (B))

#if HOST_BITS_PER_LONG != 64 && HOST_BITS_PER_LONG != 32
 #error "Some constant folding done by hand to avoid shift count warnings"
#endif

static void get_zero (REAL_VALUE_TYPE *, int);
static void get_canonical_qnan (REAL_VALUE_TYPE *, int);
static void get_canonical_snan (REAL_VALUE_TYPE *, int);
static void get_inf (REAL_VALUE_TYPE *, int);
static bool sticky_rshift_significand (REAL_VALUE_TYPE *,
				       const REAL_VALUE_TYPE *, unsigned int);
static void rshift_significand (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
				unsigned int);
static void lshift_significand (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
				unsigned int);
static void lshift_significand_1 (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);
static bool add_significands (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *,
			      const REAL_VALUE_TYPE *);
static bool sub_significands (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
			      const REAL_VALUE_TYPE *, int);
static void neg_significand (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);
static int cmp_significands (const REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);
static int cmp_significand_0 (const REAL_VALUE_TYPE *);
static void set_significand_bit (REAL_VALUE_TYPE *, unsigned int);
static void clear_significand_bit (REAL_VALUE_TYPE *, unsigned int);
static bool test_significand_bit (REAL_VALUE_TYPE *, unsigned int);
static void clear_significand_below (REAL_VALUE_TYPE *, unsigned int);
static bool div_significands (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
			      const REAL_VALUE_TYPE *);
static void normalize (REAL_VALUE_TYPE *);

static bool do_add (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
		    const REAL_VALUE_TYPE *, int);
static bool do_multiply (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
			 const REAL_VALUE_TYPE *);
static bool do_divide (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *,
		       const REAL_VALUE_TYPE *);
static int do_compare (const REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *, int);
static void do_fix_trunc (REAL_VALUE_TYPE *, const REAL_VALUE_TYPE *);

static unsigned long rtd_divmod (REAL_VALUE_TYPE *, REAL_VALUE_TYPE *);

static const REAL_VALUE_TYPE * ten_to_ptwo (int);
static const REAL_VALUE_TYPE * ten_to_mptwo (int);
static const REAL_VALUE_TYPE * real_digit (int);
static void times_pten (REAL_VALUE_TYPE *, int);

static void round_for_format (const struct real_format *, REAL_VALUE_TYPE *);

/* Initialize R with a positive zero.  */

static inline void
get_zero (REAL_VALUE_TYPE *r, int sign)
{
  memset (r, 0, sizeof (*r));
  r->sign = sign;
}

/* Initialize R with the canonical quiet NaN.  */

static inline void
get_canonical_qnan (REAL_VALUE_TYPE *r, int sign)
{
  memset (r, 0, sizeof (*r));
  r->cl = rvc_nan;
  r->sign = sign;
  r->canonical = 1;
}

static inline void
get_canonical_snan (REAL_VALUE_TYPE *r, int sign)
{
  memset (r, 0, sizeof (*r));
  r->cl = rvc_nan;
  r->sign = sign;
  r->signalling = 1;
  r->canonical = 1;
}

static inline void
get_inf (REAL_VALUE_TYPE *r, int sign)
{
  memset (r, 0, sizeof (*r));
  r->cl = rvc_inf;
  r->sign = sign;
}


/* Right-shift the significand of A by N bits; put the result in the
   significand of R.  If any one bits are shifted out, return true.  */

static bool
sticky_rshift_significand (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
			   unsigned int n)
{
  unsigned long sticky = 0;
  unsigned int i, ofs = 0;

  if (n >= HOST_BITS_PER_LONG)
    {
      for (i = 0, ofs = n / HOST_BITS_PER_LONG; i < ofs; ++i)
	sticky |= a->sig[i];
      n &= HOST_BITS_PER_LONG - 1;
    }

  if (n != 0)
    {
      sticky |= a->sig[ofs] & (((unsigned long)1 << n) - 1);
      for (i = 0; i < SIGSZ; ++i)
	{
	  r->sig[i]
	    = (((ofs + i >= SIGSZ ? 0 : a->sig[ofs + i]) >> n)
	       | ((ofs + i + 1 >= SIGSZ ? 0 : a->sig[ofs + i + 1])
		  << (HOST_BITS_PER_LONG - n)));
	}
    }
  else
    {
      for (i = 0; ofs + i < SIGSZ; ++i)
	r->sig[i] = a->sig[ofs + i];
      for (; i < SIGSZ; ++i)
	r->sig[i] = 0;
    }

  return sticky != 0;
}

/* Right-shift the significand of A by N bits; put the result in the
   significand of R.  */

static void
rshift_significand (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
		    unsigned int n)
{
  unsigned int i, ofs = n / HOST_BITS_PER_LONG;

  n &= HOST_BITS_PER_LONG - 1;
  if (n != 0)
    {
      for (i = 0; i < SIGSZ; ++i)
	{
	  r->sig[i]
	    = (((ofs + i >= SIGSZ ? 0 : a->sig[ofs + i]) >> n)
	       | ((ofs + i + 1 >= SIGSZ ? 0 : a->sig[ofs + i + 1])
		  << (HOST_BITS_PER_LONG - n)));
	}
    }
  else
    {
      for (i = 0; ofs + i < SIGSZ; ++i)
	r->sig[i] = a->sig[ofs + i];
      for (; i < SIGSZ; ++i)
	r->sig[i] = 0;
    }
}

/* Left-shift the significand of A by N bits; put the result in the
   significand of R.  */

static void
lshift_significand (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
		    unsigned int n)
{
  unsigned int i, ofs = n / HOST_BITS_PER_LONG;

  n &= HOST_BITS_PER_LONG - 1;
  if (n == 0)
    {
      for (i = 0; ofs + i < SIGSZ; ++i)
	r->sig[SIGSZ-1-i] = a->sig[SIGSZ-1-i-ofs];
      for (; i < SIGSZ; ++i)
	r->sig[SIGSZ-1-i] = 0;
    }
  else
    for (i = 0; i < SIGSZ; ++i)
      {
	r->sig[SIGSZ-1-i]
	  = (((ofs + i >= SIGSZ ? 0 : a->sig[SIGSZ-1-i-ofs]) << n)
	     | ((ofs + i + 1 >= SIGSZ ? 0 : a->sig[SIGSZ-1-i-ofs-1])
		>> (HOST_BITS_PER_LONG - n)));
      }
}

/* Likewise, but N is specialized to 1.  */

static inline void
lshift_significand_1 (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a)
{
  unsigned int i;

  for (i = SIGSZ - 1; i > 0; --i)
    r->sig[i] = (a->sig[i] << 1) | (a->sig[i-1] >> (HOST_BITS_PER_LONG - 1));
  r->sig[0] = a->sig[0] << 1;
}

/* Add the significands of A and B, placing the result in R.  Return
   true if there was carry out of the most significant word.  */

static inline bool
add_significands (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
		  const REAL_VALUE_TYPE *b)
{
  bool carry = false;
  int i;

  for (i = 0; i < SIGSZ; ++i)
    {
      unsigned long ai = a->sig[i];
      unsigned long ri = ai + b->sig[i];

      if (carry)
	{
	  carry = ri < ai;
	  carry |= ++ri == 0;
	}
      else
	carry = ri < ai;

      r->sig[i] = ri;
    }

  return carry;
}

/* Subtract the significands of A and B, placing the result in R.  CARRY is
   true if there's a borrow incoming to the least significant word.
   Return true if there was borrow out of the most significant word.  */

static inline bool
sub_significands (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
		  const REAL_VALUE_TYPE *b, int carry)
{
  int i;

  for (i = 0; i < SIGSZ; ++i)
    {
      unsigned long ai = a->sig[i];
      unsigned long ri = ai - b->sig[i];

      if (carry)
	{
	  carry = ri > ai;
	  carry |= ~--ri == 0;
	}
      else
	carry = ri > ai;

      r->sig[i] = ri;
    }

  return carry;
}

/* Negate the significand A, placing the result in R.  */

static inline void
neg_significand (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a)
{
  bool carry = true;
  int i;

  for (i = 0; i < SIGSZ; ++i)
    {
      unsigned long ri, ai = a->sig[i];

      if (carry)
	{
	  if (ai)
	    {
	      ri = -ai;
	      carry = false;
	    }
	  else
	    ri = ai;
	}
      else
	ri = ~ai;

      r->sig[i] = ri;
    }
}

/* Compare significands.  Return tri-state vs zero.  */

static inline int
cmp_significands (const REAL_VALUE_TYPE *a, const REAL_VALUE_TYPE *b)
{
  int i;

  for (i = SIGSZ - 1; i >= 0; --i)
    {
      unsigned long ai = a->sig[i];
      unsigned long bi = b->sig[i];

      if (ai > bi)
	return 1;
      if (ai < bi)
	return -1;
    }

  return 0;
}

/* Return true if A is nonzero.  */

static inline int
cmp_significand_0 (const REAL_VALUE_TYPE *a)
{
  int i;

  for (i = SIGSZ - 1; i >= 0; --i)
    if (a->sig[i])
      return 1;

  return 0;
}

/* Set bit N of the significand of R.  */

static inline void
set_significand_bit (REAL_VALUE_TYPE *r, unsigned int n)
{
  r->sig[n / HOST_BITS_PER_LONG]
    |= (unsigned long)1 << (n % HOST_BITS_PER_LONG);
}

/* Clear bit N of the significand of R.  */

static inline void
clear_significand_bit (REAL_VALUE_TYPE *r, unsigned int n)
{
  r->sig[n / HOST_BITS_PER_LONG]
    &= ~((unsigned long)1 << (n % HOST_BITS_PER_LONG));
}

/* Test bit N of the significand of R.  */

static inline bool
test_significand_bit (REAL_VALUE_TYPE *r, unsigned int n)
{
  /* ??? Compiler bug here if we return this expression directly.
     The conversion to bool strips the "&1" and we wind up testing
     e.g. 2 != 0 -> true.  Seen in gcc version 3.2 20020520.  */
  int t = (r->sig[n / HOST_BITS_PER_LONG] >> (n % HOST_BITS_PER_LONG)) & 1;
  return t;
}

/* Clear bits 0..N-1 of the significand of R.  */

static void
clear_significand_below (REAL_VALUE_TYPE *r, unsigned int n)
{
  int i, w = n / HOST_BITS_PER_LONG;

  for (i = 0; i < w; ++i)
    r->sig[i] = 0;

  r->sig[w] &= ~(((unsigned long)1 << (n % HOST_BITS_PER_LONG)) - 1);
}

/* Divide the significands of A and B, placing the result in R.  Return
   true if the division was inexact.  */

static inline bool
div_significands (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
		  const REAL_VALUE_TYPE *b)
{
  REAL_VALUE_TYPE u;
  int i, bit = SIGNIFICAND_BITS - 1;
  unsigned long msb, inexact;

  u = *a;
  memset (r->sig, 0, sizeof (r->sig));

  msb = 0;
  goto start;
  do
    {
      msb = u.sig[SIGSZ-1] & SIG_MSB;
      lshift_significand_1 (&u, &u);
    start:
      if (msb || cmp_significands (&u, b) >= 0)
	{
	  sub_significands (&u, &u, b, 0);
	  set_significand_bit (r, bit);
	}
    }
  while (--bit >= 0);

  for (i = 0, inexact = 0; i < SIGSZ; i++)
    inexact |= u.sig[i];

  return inexact != 0;
}

/* Adjust the exponent and significand of R such that the most
   significant bit is set.  We underflow to zero and overflow to
   infinity here, without denormals.  (The intermediate representation
   exponent is large enough to handle target denormals normalized.)  */

static void
normalize (REAL_VALUE_TYPE *r)
{
  int shift = 0, exp;
  int i, j;

  if (r->decimal)
    return;

  /* Find the first word that is nonzero.  */
  for (i = SIGSZ - 1; i >= 0; i--)
    if (r->sig[i] == 0)
      shift += HOST_BITS_PER_LONG;
    else
      break;

  /* Zero significand flushes to zero.  */
  if (i < 0)
    {
      r->cl = rvc_zero;
      SET_REAL_EXP (r, 0);
      return;
    }

  /* Find the first bit that is nonzero.  */
  for (j = 0; ; j++)
    if (r->sig[i] & ((unsigned long)1 << (HOST_BITS_PER_LONG - 1 - j)))
      break;
  shift += j;

  if (shift > 0)
    {
      exp = REAL_EXP (r) - shift;
      if (exp > MAX_EXP)
	get_inf (r, r->sign);
      else if (exp < -MAX_EXP)
	get_zero (r, r->sign);
      else
	{
	  SET_REAL_EXP (r, exp);
	  lshift_significand (r, r, shift);
	}
    }
}

/* Calculate R = A + (SUBTRACT_P ? -B : B).  Return true if the
   result may be inexact due to a loss of precision.  */

static bool
do_add (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
	const REAL_VALUE_TYPE *b, int subtract_p)
{
  int dexp, sign, exp;
  REAL_VALUE_TYPE t;
  bool inexact = false;

  /* Determine if we need to add or subtract.  */
  sign = a->sign;
  subtract_p = (sign ^ b->sign) ^ subtract_p;

  switch (CLASS2 (a->cl, b->cl))
    {
    case CLASS2 (rvc_zero, rvc_zero):
      /* -0 + -0 = -0, -0 - +0 = -0; all other cases yield +0.  */
      get_zero (r, sign & !subtract_p);
      return false;

    case CLASS2 (rvc_zero, rvc_normal):
    case CLASS2 (rvc_zero, rvc_inf):
    case CLASS2 (rvc_zero, rvc_nan):
      /* 0 + ANY = ANY.  */
    case CLASS2 (rvc_normal, rvc_nan):
    case CLASS2 (rvc_inf, rvc_nan):
    case CLASS2 (rvc_nan, rvc_nan):
      /* ANY + NaN = NaN.  */
    case CLASS2 (rvc_normal, rvc_inf):
      /* R + Inf = Inf.  */
      *r = *b;
      r->sign = sign ^ subtract_p;
      return false;

    case CLASS2 (rvc_normal, rvc_zero):
    case CLASS2 (rvc_inf, rvc_zero):
    case CLASS2 (rvc_nan, rvc_zero):
      /* ANY + 0 = ANY.  */
    case CLASS2 (rvc_nan, rvc_normal):
    case CLASS2 (rvc_nan, rvc_inf):
      /* NaN + ANY = NaN.  */
    case CLASS2 (rvc_inf, rvc_normal):
      /* Inf + R = Inf.  */
      *r = *a;
      return false;

    case CLASS2 (rvc_inf, rvc_inf):
      if (subtract_p)
	/* Inf - Inf = NaN.  */
	get_canonical_qnan (r, 0);
      else
	/* Inf + Inf = Inf.  */
	*r = *a;
      return false;

    case CLASS2 (rvc_normal, rvc_normal):
      break;

    default:
      gcc_unreachable ();
    }

  /* Swap the arguments such that A has the larger exponent.  */
  dexp = REAL_EXP (a) - REAL_EXP (b);
  if (dexp < 0)
    {
      const REAL_VALUE_TYPE *t;
      t = a, a = b, b = t;
      dexp = -dexp;
      sign ^= subtract_p;
    }
  exp = REAL_EXP (a);

  /* If the exponents are not identical, we need to shift the
     significand of B down.  */
  if (dexp > 0)
    {
      /* If the exponents are too far apart, the significands
	 do not overlap, which makes the subtraction a noop.  */
      if (dexp >= SIGNIFICAND_BITS)
	{
	  *r = *a;
	  r->sign = sign;
	  return true;
	}

      inexact |= sticky_rshift_significand (&t, b, dexp);
      b = &t;
    }

  if (subtract_p)
    {
      if (sub_significands (r, a, b, inexact))
	{
	  /* We got a borrow out of the subtraction.  That means that
	     A and B had the same exponent, and B had the larger
	     significand.  We need to swap the sign and negate the
	     significand.  */
	  sign ^= 1;
	  neg_significand (r, r);
	}
    }
  else
    {
      if (add_significands (r, a, b))
	{
	  /* We got carry out of the addition.  This means we need to
	     shift the significand back down one bit and increase the
	     exponent.  */
	  inexact |= sticky_rshift_significand (r, r, 1);
	  r->sig[SIGSZ-1] |= SIG_MSB;
	  if (++exp > MAX_EXP)
	    {
	      get_inf (r, sign);
	      return true;
	    }
	}
    }

  r->cl = rvc_normal;
  r->sign = sign;
  SET_REAL_EXP (r, exp);
  /* Zero out the remaining fields.  */
  r->signalling = 0;
  r->canonical = 0;
  r->decimal = 0;

  /* Re-normalize the result.  */
  normalize (r);

  /* Special case: if the subtraction results in zero, the result
     is positive.  */
  if (r->cl == rvc_zero)
    r->sign = 0;
  else
    r->sig[0] |= inexact;

  return inexact;
}

/* Calculate R = A * B.  Return true if the result may be inexact.  */

static bool
do_multiply (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
	     const REAL_VALUE_TYPE *b)
{
  REAL_VALUE_TYPE u, t, *rr;
  unsigned int i, j, k;
  int sign = a->sign ^ b->sign;
  bool inexact = false;

  switch (CLASS2 (a->cl, b->cl))
    {
    case CLASS2 (rvc_zero, rvc_zero):
    case CLASS2 (rvc_zero, rvc_normal):
    case CLASS2 (rvc_normal, rvc_zero):
      /* +-0 * ANY = 0 with appropriate sign.  */
      get_zero (r, sign);
      return false;

    case CLASS2 (rvc_zero, rvc_nan):
    case CLASS2 (rvc_normal, rvc_nan):
    case CLASS2 (rvc_inf, rvc_nan):
    case CLASS2 (rvc_nan, rvc_nan):
      /* ANY * NaN = NaN.  */
      *r = *b;
      r->sign = sign;
      return false;

    case CLASS2 (rvc_nan, rvc_zero):
    case CLASS2 (rvc_nan, rvc_normal):
    case CLASS2 (rvc_nan, rvc_inf):
      /* NaN * ANY = NaN.  */
      *r = *a;
      r->sign = sign;
      return false;

    case CLASS2 (rvc_zero, rvc_inf):
    case CLASS2 (rvc_inf, rvc_zero):
      /* 0 * Inf = NaN */
      get_canonical_qnan (r, sign);
      return false;

    case CLASS2 (rvc_inf, rvc_inf):
    case CLASS2 (rvc_normal, rvc_inf):
    case CLASS2 (rvc_inf, rvc_normal):
      /* Inf * Inf = Inf, R * Inf = Inf */
      get_inf (r, sign);
      return false;

    case CLASS2 (rvc_normal, rvc_normal):
      break;

    default:
      gcc_unreachable ();
    }

  if (r == a || r == b)
    rr = &t;
  else
    rr = r;
  get_zero (rr, 0);

  /* Collect all the partial products.  Since we don't have sure access
     to a widening multiply, we split each long into two half-words.

     Consider the long-hand form of a four half-word multiplication:

		 A  B  C  D
	      *  E  F  G  H
	     --------------
	        DE DF DG DH
	     CE CF CG CH
	  BE BF BG BH
       AE AF AG AH

     We construct partial products of the widened half-word products
     that are known to not overlap, e.g. DF+DH.  Each such partial
     product is given its proper exponent, which allows us to sum them
     and obtain the finished product.  */

  for (i = 0; i < SIGSZ * 2; ++i)
    {
      unsigned long ai = a->sig[i / 2];
      if (i & 1)
	ai >>= HOST_BITS_PER_LONG / 2;
      else
	ai &= ((unsigned long)1 << (HOST_BITS_PER_LONG / 2)) - 1;

      if (ai == 0)
	continue;

      for (j = 0; j < 2; ++j)
	{
	  int exp = (REAL_EXP (a) - (2*SIGSZ-1-i)*(HOST_BITS_PER_LONG/2)
		     + (REAL_EXP (b) - (1-j)*(HOST_BITS_PER_LONG/2)));

	  if (exp > MAX_EXP)
	    {
	      get_inf (r, sign);
	      return true;
	    }
	  if (exp < -MAX_EXP)
	    {
	      /* Would underflow to zero, which we shouldn't bother adding.  */
	      inexact = true;
	      continue;
	    }

	  memset (&u, 0, sizeof (u));
	  u.cl = rvc_normal;
	  SET_REAL_EXP (&u, exp);

	  for (k = j; k < SIGSZ * 2; k += 2)
	    {
	      unsigned long bi = b->sig[k / 2];
	      if (k & 1)
		bi >>= HOST_BITS_PER_LONG / 2;
	      else
		bi &= ((unsigned long)1 << (HOST_BITS_PER_LONG / 2)) - 1;

	      u.sig[k / 2] = ai * bi;
	    }

	  normalize (&u);
	  inexact |= do_add (rr, rr, &u, 0);
	}
    }

  rr->sign = sign;
  if (rr != r)
    *r = t;

  return inexact;
}

/* Calculate R = A / B.  Return true if the result may be inexact.  */

static bool
do_divide (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a,
	   const REAL_VALUE_TYPE *b)
{
  int exp, sign = a->sign ^ b->sign;
  REAL_VALUE_TYPE t, *rr;
  bool inexact;

  switch (CLASS2 (a->cl, b->cl))
    {
    case CLASS2 (rvc_zero, rvc_zero):
      /* 0 / 0 = NaN.  */
    case CLASS2 (rvc_inf, rvc_inf):
      /* Inf / Inf = NaN.  */
      get_canonical_qnan (r, sign);
      return false;

    case CLASS2 (rvc_zero, rvc_normal):
    case CLASS2 (rvc_zero, rvc_inf):
      /* 0 / ANY = 0.  */
    case CLASS2 (rvc_normal, rvc_inf):
      /* R / Inf = 0.  */
      get_zero (r, sign);
      return false;

    case CLASS2 (rvc_normal, rvc_zero):
      /* R / 0 = Inf.  */
    case CLASS2 (rvc_inf, rvc_zero):
      /* Inf / 0 = Inf.  */
      get_inf (r, sign);
      return false;

    case CLASS2 (rvc_zero, rvc_nan):
    case CLASS2 (rvc_normal, rvc_nan):
    case CLASS2 (rvc_inf, rvc_nan):
    case CLASS2 (rvc_nan, rvc_nan):
      /* ANY / NaN = NaN.  */
      *r = *b;
      r->sign = sign;
      return false;

    case CLASS2 (rvc_nan, rvc_zero):
    case CLASS2 (rvc_nan, rvc_normal):
    case CLASS2 (rvc_nan, rvc_inf):
      /* NaN / ANY = NaN.  */
      *r = *a;
      r->sign = sign;
      return false;

    case CLASS2 (rvc_inf, rvc_normal):
      /* Inf / R = Inf.  */
      get_inf (r, sign);
      return false;

    case CLASS2 (rvc_normal, rvc_normal):
      break;

    default:
      gcc_unreachable ();
    }

  if (r == a || r == b)
    rr = &t;
  else
    rr = r;

  /* Make sure all fields in the result are initialized.  */
  get_zero (rr, 0);
  rr->cl = rvc_normal;
  rr->sign = sign;

  exp = REAL_EXP (a) - REAL_EXP (b) + 1;
  if (exp > MAX_EXP)
    {
      get_inf (r, sign);
      return true;
    }
  if (exp < -MAX_EXP)
    {
      get_zero (r, sign);
      return true;
    }
  SET_REAL_EXP (rr, exp);

  inexact = div_significands (rr, a, b);

  /* Re-normalize the result.  */
  normalize (rr);
  rr->sig[0] |= inexact;

  if (rr != r)
    *r = t;

  return inexact;
}

/* Return a tri-state comparison of A vs B.  Return NAN_RESULT if
   one of the two operands is a NaN.  */

static int
do_compare (const REAL_VALUE_TYPE *a, const REAL_VALUE_TYPE *b,
	    int nan_result)
{
  int ret;

  switch (CLASS2 (a->cl, b->cl))
    {
    case CLASS2 (rvc_zero, rvc_zero):
      /* Sign of zero doesn't matter for compares.  */
      return 0;

    case CLASS2 (rvc_inf, rvc_zero):
    case CLASS2 (rvc_inf, rvc_normal):
    case CLASS2 (rvc_normal, rvc_zero):
      return (a->sign ? -1 : 1);

    case CLASS2 (rvc_inf, rvc_inf):
      return -a->sign - -b->sign;

    case CLASS2 (rvc_zero, rvc_normal):
    case CLASS2 (rvc_zero, rvc_inf):
    case CLASS2 (rvc_normal, rvc_inf):
      return (b->sign ? 1 : -1);

    case CLASS2 (rvc_zero, rvc_nan):
    case CLASS2 (rvc_normal, rvc_nan):
    case CLASS2 (rvc_inf, rvc_nan):
    case CLASS2 (rvc_nan, rvc_nan):
    case CLASS2 (rvc_nan, rvc_zero):
    case CLASS2 (rvc_nan, rvc_normal):
    case CLASS2 (rvc_nan, rvc_inf):
      return nan_result;

    case CLASS2 (rvc_normal, rvc_normal):
      break;

    default:
      gcc_unreachable ();
    }

  if (a->sign != b->sign)
    return -a->sign - -b->sign;

  if (a->decimal || b->decimal)
    return decimal_do_compare (a, b, nan_result);

  if (REAL_EXP (a) > REAL_EXP (b))
    ret = 1;
  else if (REAL_EXP (a) < REAL_EXP (b))
    ret = -1;
  else
    ret = cmp_significands (a, b);

  return (a->sign ? -ret : ret);
}

/* Return A truncated to an integral value toward zero.  */

static void
do_fix_trunc (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *a)
{
  *r = *a;

  switch (r->cl)
    {
    case rvc_zero:
    case rvc_inf:
    case rvc_nan:
      break;

    case rvc_normal:
      if (r->decimal)
	{
	  decimal_do_fix_trunc (r, a);
	  return;
	}
      if (REAL_EXP (r) <= 0)
	get_zero (r, r->sign);
      else if (REAL_EXP (r) < SIGNIFICAND_BITS)
	clear_significand_below (r, SIGNIFICAND_BITS - REAL_EXP (r));
      break;

    default:
      gcc_unreachable ();
    }
}

/* Perform the binary or unary operation described by CODE.
   For a unary operation, leave OP1 NULL.  This function returns
   true if the result may be inexact due to loss of precision.  */

bool
real_arithmetic (REAL_VALUE_TYPE *r, int icode, const REAL_VALUE_TYPE *op0,
		 const REAL_VALUE_TYPE *op1)
{
  enum tree_code code = icode;

  if (op0->decimal || (op1 && op1->decimal))
    return decimal_real_arithmetic (r, icode, op0, op1);

  switch (code)
    {
    case PLUS_EXPR:
      return do_add (r, op0, op1, 0);

    case MINUS_EXPR:
      return do_add (r, op0, op1, 1);

    case MULT_EXPR:
      return do_multiply (r, op0, op1);

    case RDIV_EXPR:
      return do_divide (r, op0, op1);

    case MIN_EXPR:
      if (op1->cl == rvc_nan)
	*r = *op1;
      else if (do_compare (op0, op1, -1) < 0)
	*r = *op0;
      else
	*r = *op1;
      break;

    case MAX_EXPR:
      if (op1->cl == rvc_nan)
	*r = *op1;
      else if (do_compare (op0, op1, 1) < 0)
	*r = *op1;
      else
	*r = *op0;
      break;

    case NEGATE_EXPR:
      *r = *op0;
      r->sign ^= 1;
      break;

    case ABS_EXPR:
      *r = *op0;
      r->sign = 0;
      break;

    case FIX_TRUNC_EXPR:
      do_fix_trunc (r, op0);
      break;

    default:
      gcc_unreachable ();
    }
  return false;
}

/* Legacy.  Similar, but return the result directly.  */

REAL_VALUE_TYPE
real_arithmetic2 (int icode, const REAL_VALUE_TYPE *op0,
		  const REAL_VALUE_TYPE *op1)
{
  REAL_VALUE_TYPE r;
  real_arithmetic (&r, icode, op0, op1);
  return r;
}

bool
real_compare (int icode, const REAL_VALUE_TYPE *op0,
	      const REAL_VALUE_TYPE *op1)
{
  enum tree_code code = icode;

  switch (code)
    {
    case LT_EXPR:
      return do_compare (op0, op1, 1) < 0;
    case LE_EXPR:
      return do_compare (op0, op1, 1) <= 0;
    case GT_EXPR:
      return do_compare (op0, op1, -1) > 0;
    case GE_EXPR:
      return do_compare (op0, op1, -1) >= 0;
    case EQ_EXPR:
      return do_compare (op0, op1, -1) == 0;
    case NE_EXPR:
      return do_compare (op0, op1, -1) != 0;
    case UNORDERED_EXPR:
      return op0->cl == rvc_nan || op1->cl == rvc_nan;
    case ORDERED_EXPR:
      return op0->cl != rvc_nan && op1->cl != rvc_nan;
    case UNLT_EXPR:
      return do_compare (op0, op1, -1) < 0;
    case UNLE_EXPR:
      return do_compare (op0, op1, -1) <= 0;
    case UNGT_EXPR:
      return do_compare (op0, op1, 1) > 0;
    case UNGE_EXPR:
      return do_compare (op0, op1, 1) >= 0;
    case UNEQ_EXPR:
      return do_compare (op0, op1, 0) == 0;
    case LTGT_EXPR:
      return do_compare (op0, op1, 0) != 0;

    default:
      gcc_unreachable ();
    }
}

/* Return floor log2(R).  */

int
real_exponent (const REAL_VALUE_TYPE *r)
{
  switch (r->cl)
    {
    case rvc_zero:
      return 0;
    case rvc_inf:
    case rvc_nan:
      return (unsigned int)-1 >> 1;
    case rvc_normal:
      return REAL_EXP (r);
    default:
      gcc_unreachable ();
    }
}

/* R = OP0 * 2**EXP.  */

void
real_ldexp (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *op0, int exp)
{
  *r = *op0;
  switch (r->cl)
    {
    case rvc_zero:
    case rvc_inf:
    case rvc_nan:
      break;

    case rvc_normal:
      exp += REAL_EXP (op0);
      if (exp > MAX_EXP)
	get_inf (r, r->sign);
      else if (exp < -MAX_EXP)
	get_zero (r, r->sign);
      else
	SET_REAL_EXP (r, exp);
      break;

    default:
      gcc_unreachable ();
    }
}

/* Determine whether a floating-point value X is infinite.  */

bool
real_isinf (const REAL_VALUE_TYPE *r)
{
  return (r->cl == rvc_inf);
}

/* Determine whether a floating-point value X is a NaN.  */

bool
real_isnan (const REAL_VALUE_TYPE *r)
{
  return (r->cl == rvc_nan);
}

/* Determine whether a floating-point value X is negative.  */

bool
real_isneg (const REAL_VALUE_TYPE *r)
{
  return r->sign;
}

/* Determine whether a floating-point value X is minus zero.  */

bool
real_isnegzero (const REAL_VALUE_TYPE *r)
{
  return r->sign && r->cl == rvc_zero;
}

/* Compare two floating-point objects for bitwise identity.  */

bool
real_identical (const REAL_VALUE_TYPE *a, const REAL_VALUE_TYPE *b)
{
  int i;

  if (a->cl != b->cl)
    return false;
  if (a->sign != b->sign)
    return false;

  switch (a->cl)
    {
    case rvc_zero:
    case rvc_inf:
      return true;

    case rvc_normal:
      if (a->decimal != b->decimal)
        return false;
      if (REAL_EXP (a) != REAL_EXP (b))
	return false;
      break;

    case rvc_nan:
      if (a->signalling != b->signalling)
	return false;
      /* The significand is ignored for canonical NaNs.  */
      if (a->canonical || b->canonical)
	return a->canonical == b->canonical;
      break;

    default:
      gcc_unreachable ();
    }

  for (i = 0; i < SIGSZ; ++i)
    if (a->sig[i] != b->sig[i])
      return false;

  return true;
}

/* Try to change R into its exact multiplicative inverse in machine
   mode MODE.  Return true if successful.  */

bool
exact_real_inverse (enum machine_mode mode, REAL_VALUE_TYPE *r)
{
  const REAL_VALUE_TYPE *one = real_digit (1);
  REAL_VALUE_TYPE u;
  int i;

  if (r->cl != rvc_normal)
    return false;

  /* Check for a power of two: all significand bits zero except the MSB.  */
  for (i = 0; i < SIGSZ-1; ++i)
    if (r->sig[i] != 0)
      return false;
  if (r->sig[SIGSZ-1] != SIG_MSB)
    return false;

  /* Find the inverse and truncate to the required mode.  */
  do_divide (&u, one, r);
  real_convert (&u, mode, &u);

  /* The rounding may have overflowed.  */
  if (u.cl != rvc_normal)
    return false;
  for (i = 0; i < SIGSZ-1; ++i)
    if (u.sig[i] != 0)
      return false;
  if (u.sig[SIGSZ-1] != SIG_MSB)
    return false;

  *r = u;
  return true;
}

/* Render R as an integer.  */

HOST_WIDE_INT
real_to_integer (const REAL_VALUE_TYPE *r)
{
  unsigned HOST_WIDE_INT i;

  switch (r->cl)
    {
    case rvc_zero:
    underflow:
      return 0;

    case rvc_inf:
    case rvc_nan:
    overflow:
      i = (unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1);
      if (!r->sign)
	i--;
      return i;

    case rvc_normal:
      if (r->decimal)
	return decimal_real_to_integer (r);

      if (REAL_EXP (r) <= 0)
	goto underflow;
      /* Only force overflow for unsigned overflow.  Signed overflow is
	 undefined, so it doesn't matter what we return, and some callers
	 expect to be able to use this routine for both signed and
	 unsigned conversions.  */
      if (REAL_EXP (r) > HOST_BITS_PER_WIDE_INT)
	goto overflow;

      if (HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG)
	i = r->sig[SIGSZ-1];
      else 
	{
	  gcc_assert (HOST_BITS_PER_WIDE_INT == 2 * HOST_BITS_PER_LONG);
	  i = r->sig[SIGSZ-1];
	  i = i << (HOST_BITS_PER_LONG - 1) << 1;
	  i |= r->sig[SIGSZ-2];
	}

      i >>= HOST_BITS_PER_WIDE_INT - REAL_EXP (r);

      if (r->sign)
	i = -i;
      return i;

    default:
      gcc_unreachable ();
    }
}

/* Likewise, but to an integer pair, HI+LOW.  */

void
real_to_integer2 (HOST_WIDE_INT *plow, HOST_WIDE_INT *phigh,
		  const REAL_VALUE_TYPE *r)
{
  REAL_VALUE_TYPE t;
  HOST_WIDE_INT low, high;
  int exp;

  switch (r->cl)
    {
    case rvc_zero:
    underflow:
      low = high = 0;
      break;

    case rvc_inf:
    case rvc_nan:
    overflow:
      high = (unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1);
      if (r->sign)
	low = 0;
      else
	{
	  high--;
	  low = -1;
	}
      break;

    case rvc_normal:
      if (r->decimal)
	{ 
	  decimal_real_to_integer2 (plow, phigh, r);
	  return;
	}
	
      exp = REAL_EXP (r);
      if (exp <= 0)
	goto underflow;
      /* Only force overflow for unsigned overflow.  Signed overflow is
	 undefined, so it doesn't matter what we return, and some callers
	 expect to be able to use this routine for both signed and
	 unsigned conversions.  */
      if (exp > 2*HOST_BITS_PER_WIDE_INT)
	goto overflow;

      rshift_significand (&t, r, 2*HOST_BITS_PER_WIDE_INT - exp);
      if (HOST_BITS_PER_WIDE_INT == HOST_BITS_PER_LONG)
	{
	  high = t.sig[SIGSZ-1];
	  low = t.sig[SIGSZ-2];
	}
      else 
	{
	  gcc_assert (HOST_BITS_PER_WIDE_INT == 2*HOST_BITS_PER_LONG);
	  high = t.sig[SIGSZ-1];
	  high = high << (HOST_BITS_PER_LONG - 1) << 1;
	  high |= t.sig[SIGSZ-2];

	  low = t.sig[SIGSZ-3];
	  low = low << (HOST_BITS_PER_LONG - 1) << 1;
	  low |= t.sig[SIGSZ-4];
	}

      if (r->sign)
	{
	  if (low == 0)
	    high = -high;
	  else
	    low = -low, high = ~high;
	}
      break;

    default:
      gcc_unreachable ();
    }

  *plow = low;
  *phigh = high;
}

/* A subroutine of real_to_decimal.  Compute the quotient and remainder
   of NUM / DEN.  Return the quotient and place the remainder in NUM.
   It is expected that NUM / DEN are close enough that the quotient is
   small.  */

static unsigned long
rtd_divmod (REAL_VALUE_TYPE *num, REAL_VALUE_TYPE *den)
{
  unsigned long q, msb;
  int expn = REAL_EXP (num), expd = REAL_EXP (den);

  if (expn < expd)
    return 0;

  q = msb = 0;
  goto start;
  do
    {
      msb = num->sig[SIGSZ-1] & SIG_MSB;
      q <<= 1;
      lshift_significand_1 (num, num);
    start:
      if (msb || cmp_significands (num, den) >= 0)
	{
	  sub_significands (num, num, den, 0);
	  q |= 1;
	}
    }
  while (--expn >= expd);

  SET_REAL_EXP (num, expd);
  normalize (num);

  return q;
}

/* Render R as a decimal floating point constant.  Emit DIGITS significant
   digits in the result, bounded by BUF_SIZE.  If DIGITS is 0, choose the
   maximum for the representation.  If CROP_TRAILING_ZEROS, strip trailing
   zeros.  */

#define M_LOG10_2	0.30102999566398119521

void
real_to_decimal (char *str, const REAL_VALUE_TYPE *r_orig, size_t buf_size,
		 size_t digits, int crop_trailing_zeros)
{
  const REAL_VALUE_TYPE *one, *ten;
  REAL_VALUE_TYPE r, pten, u, v;
  int dec_exp, cmp_one, digit;
  size_t max_digits;
  char *p, *first, *last;
  bool sign;

  r = *r_orig;
  switch (r.cl)
    {
    case rvc_zero:
      strcpy (str, (r.sign ? "-0.0" : "0.0"));
      return;
    case rvc_normal:
      break;
    case rvc_inf:
      strcpy (str, (r.sign ? "-Inf" : "+Inf"));
      return;
    case rvc_nan:
      /* ??? Print the significand as well, if not canonical?  */
      strcpy (str, (r.sign ? "-NaN" : "+NaN"));
      return;
    default:
      gcc_unreachable ();
    }

  if (r.decimal)
    {
      decimal_real_to_decimal (str, &r, buf_size, digits, crop_trailing_zeros);
      return;
    }

  /* Bound the number of digits printed by the size of the representation.  */
  max_digits = SIGNIFICAND_BITS * M_LOG10_2;
  if (digits == 0 || digits > max_digits)
    digits = max_digits;

  /* Estimate the decimal exponent, and compute the length of the string it
     will print as.  Be conservative and add one to account for possible
     overflow or rounding error.  */
  dec_exp = REAL_EXP (&r) * M_LOG10_2;
  for (max_digits = 1; dec_exp ; max_digits++)
    dec_exp /= 10;

  /* Bound the number of digits printed by the size of the output buffer.  */
  max_digits = buf_size - 1 - 1 - 2 - max_digits - 1;
  gcc_assert (max_digits <= buf_size);
  if (digits > max_digits)
    digits = max_digits;

  one = real_digit (1);
  ten = ten_to_ptwo (0);

  sign = r.sign;
  r.sign = 0;

  dec_exp = 0;
  pten = *one;

  cmp_one = do_compare (&r, one, 0);
  if (cmp_one > 0)
    {
      int m;

      /* Number is greater than one.  Convert significand to an integer
	 and strip trailing decimal zeros.  */

      u = r;
      SET_REAL_EXP (&u, SIGNIFICAND_BITS - 1);

      /* Largest M, such that 10**2**M fits within SIGNIFICAND_BITS.  */
      m = floor_log2 (max_digits);

      /* Iterate over the bits of the possible powers of 10 that might
	 be present in U and eliminate them.  That is, if we find that
	 10**2**M divides U evenly, keep the division and increase
	 DEC_EXP by 2**M.  */
      do
	{
	  REAL_VALUE_TYPE t;

	  do_divide (&t, &u, ten_to_ptwo (m));
	  do_fix_trunc (&v, &t);
	  if (cmp_significands (&v, &t) == 0)
	    {
	      u = t;
	      dec_exp += 1 << m;
	    }
	}
      while (--m >= 0);

      /* Revert the scaling to integer that we performed earlier.  */
      SET_REAL_EXP (&u, REAL_EXP (&u) + REAL_EXP (&r)
		    - (SIGNIFICAND_BITS - 1));
      r = u;

      /* Find power of 10.  Do this by dividing out 10**2**M when
	 this is larger than the current remainder.  Fill PTEN with
	 the power of 10 that we compute.  */
      if (REAL_EXP (&r) > 0)
	{
	  m = floor_log2 ((int)(REAL_EXP (&r) * M_LOG10_2)) + 1;
	  do
	    {
	      const REAL_VALUE_TYPE *ptentwo = ten_to_ptwo (m);
	      if (do_compare (&u, ptentwo, 0) >= 0)
	        {
	          do_divide (&u, &u, ptentwo);
	          do_multiply (&pten, &pten, ptentwo);
	          dec_exp += 1 << m;
	        }
	    }
          while (--m >= 0);
	}
      else
	/* We managed to divide off enough tens in the above reduction
	   loop that we've now got a negative exponent.  Fall into the
	   less-than-one code to compute the proper value for PTEN.  */
	cmp_one = -1;
    }
  if (cmp_one < 0)
    {
      int m;

      /* Number is less than one.  Pad significand with leading
	 decimal zeros.  */

      v = r;
      while (1)
	{
	  /* Stop if we'd shift bits off the bottom.  */
	  if (v.sig[0] & 7)
	    break;

	  do_multiply (&u, &v, ten);

	  /* Stop if we're now >= 1.  */
	  if (REAL_EXP (&u) > 0)
	    break;

	  v = u;
	  dec_exp -= 1;
	}
      r = v;

      /* Find power of 10.  Do this by multiplying in P=10**2**M when
	 the current remainder is smaller than 1/P.  Fill PTEN with the
	 power of 10 that we compute.  */
      m = floor_log2 ((int)(-REAL_EXP (&r) * M_LOG10_2)) + 1;
      do
	{
	  const REAL_VALUE_TYPE *ptentwo = ten_to_ptwo (m);
	  const REAL_VALUE_TYPE *ptenmtwo = ten_to_mptwo (m);

	  if (do_compare (&v, ptenmtwo, 0) <= 0)
	    {
	      do_multiply (&v, &v, ptentwo);
	      do_multiply (&pten, &pten, ptentwo);
	      dec_exp -= 1 << m;
	    }
	}
      while (--m >= 0);

      /* Invert the positive power of 10 that we've collected so far.  */
      do_divide (&pten, one, &pten);
    }

  p = str;
  if (sign)
    *p++ = '-';
  first = p++;

  /* At this point, PTEN should contain the nearest power of 10 smaller
     than R, such that this division produces the first digit.

     Using a divide-step primitive that returns the complete integral
     remainder avoids the rounding error that would be produced if
     we were to use do_divide here and then simply multiply by 10 for
     each subsequent digit.  */

  digit = rtd_divmod (&r, &pten);

  /* Be prepared for error in that division via underflow ...  */
  if (digit == 0 && cmp_significand_0 (&r))
    {
      /* Multiply by 10 and try again.  */
      do_multiply (&r, &r, ten);
      digit = rtd_divmod (&r, &pten);
      dec_exp -= 1;
      gcc_assert (digit != 0);
    }

  /* ... or overflow.  */
  if (digit == 10)
    {
      *p++ = '1';
      if (--digits > 0)
	*p++ = '0';
      dec_exp += 1;
    }
  else
    {
      gcc_assert (digit <= 10);
      *p++ = digit + '0';
    }

  /* Generate subsequent digits.  */
  while (--digits > 0)
    {
      do_multiply (&r, &r, ten);
      digit = rtd_divmod (&r, &pten);
      *p++ = digit + '0';
    }
  last = p;

  /* Generate one more digit with which to do rounding.  */
  do_multiply (&r, &r, ten);
  digit = rtd_divmod (&r, &pten);

  /* Round the result.  */
  if (digit == 5)
    {
      /* Round to nearest.  If R is nonzero there are additional
	 nonzero digits to be extracted.  */
      if (cmp_significand_0 (&r))
	digit++;
      /* Round to even.  */
      else if ((p[-1] - '0') & 1)
	digit++;
    }
  if (digit > 5)
    {
      while (p > first)
	{
	  digit = *--p;
	  if (digit == '9')
	    *p = '0';
	  else
	    {
	      *p = digit + 1;
	      break;
	    }
	}

      /* Carry out of the first digit.  This means we had all 9's and
	 now have all 0's.  "Prepend" a 1 by overwriting the first 0.  */
      if (p == first)
	{
	  first[1] = '1';
	  dec_exp++;
	}
    }

  /* Insert the decimal point.  */
  first[0] = first[1];
  first[1] = '.';

  /* If requested, drop trailing zeros.  Never crop past "1.0".  */
  if (crop_trailing_zeros)
    while (last > first + 3 && last[-1] == '0')
      last--;

  /* Append the exponent.  */
  sprintf (last, "e%+d", dec_exp);
}

/* Render R as a hexadecimal floating point constant.  Emit DIGITS
   significant digits in the result, bounded by BUF_SIZE.  If DIGITS is 0,
   choose the maximum for the representation.  If CROP_TRAILING_ZEROS,
   strip trailing zeros.  */

void
real_to_hexadecimal (char *str, const REAL_VALUE_TYPE *r, size_t buf_size,
		     size_t digits, int crop_trailing_zeros)
{
  int i, j, exp = REAL_EXP (r);
  char *p, *first;
  char exp_buf[16];
  size_t max_digits;

  switch (r->cl)
    {
    case rvc_zero:
      exp = 0;
      break;
    case rvc_normal:
      break;
    case rvc_inf:
      strcpy (str, (r->sign ? "-Inf" : "+Inf"));
      return;
    case rvc_nan:
      /* ??? Print the significand as well, if not canonical?  */
      strcpy (str, (r->sign ? "-NaN" : "+NaN"));
      return;
    default:
      gcc_unreachable ();
    }

  if (r->decimal)
    {
      /* Hexadecimal format for decimal floats is not interesting. */
      strcpy (str, "N/A");
      return;
    }

  if (digits == 0)
    digits = SIGNIFICAND_BITS / 4;

  /* Bound the number of digits printed by the size of the output buffer.  */

  sprintf (exp_buf, "p%+d", exp);
  max_digits = buf_size - strlen (exp_buf) - r->sign - 4 - 1;
  gcc_assert (max_digits <= buf_size);
  if (digits > max_digits)
    digits = max_digits;

  p = str;
  if (r->sign)
    *p++ = '-';
  *p++ = '0';
  *p++ = 'x';
  *p++ = '0';
  *p++ = '.';
  first = p;

  for (i = SIGSZ - 1; i >= 0; --i)
    for (j = HOST_BITS_PER_LONG - 4; j >= 0; j -= 4)
      {
	*p++ = "0123456789abcdef"[(r->sig[i] >> j) & 15];
	if (--digits == 0)
	  goto out;
      }

 out:
  if (crop_trailing_zeros)
    while (p > first + 1 && p[-1] == '0')
      p--;

  sprintf (p, "p%+d", exp);
}

/* Initialize R from a decimal or hexadecimal string.  The string is
   assumed to have been syntax checked already.  */

void
real_from_string (REAL_VALUE_TYPE *r, const char *str)
{
  int exp = 0;
  bool sign = false;

  get_zero (r, 0);

  if (*str == '-')
    {
      sign = true;
      str++;
    }
  else if (*str == '+')
    str++;

  if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X'))
    {
      /* Hexadecimal floating point.  */
      int pos = SIGNIFICAND_BITS - 4, d;

      str += 2;

      while (*str == '0')
	str++;
      while (1)
	{
	  d = hex_value (*str);
	  if (d == _hex_bad)
	    break;
	  if (pos >= 0)
	    {
	      r->sig[pos / HOST_BITS_PER_LONG]
		|= (unsigned long) d << (pos % HOST_BITS_PER_LONG);
	      pos -= 4;
	    }
	  else if (d)
	    /* Ensure correct rounding by setting last bit if there is
	       a subsequent nonzero digit.  */
	    r->sig[0] |= 1;
	  exp += 4;
	  str++;
	}
      if (*str == '.')
	{
	  str++;
	  if (pos == SIGNIFICAND_BITS - 4)
	    {
	      while (*str == '0')
		str++, exp -= 4;
	    }
	  while (1)
	    {
	      d = hex_value (*str);
	      if (d == _hex_bad)
		break;
	      if (pos >= 0)
		{
		  r->sig[pos / HOST_BITS_PER_LONG]
		    |= (unsigned long) d << (pos % HOST_BITS_PER_LONG);
		  pos -= 4;
		}
	      else if (d)
		/* Ensure correct rounding by setting last bit if there is
		   a subsequent nonzero digit.  */
		r->sig[0] |= 1;
	      str++;
	    }
	}

      /* If the mantissa is zero, ignore the exponent.  */
      if (!cmp_significand_0 (r))
	goto underflow;

      if (*str == 'p' || *str == 'P')
	{
	  bool exp_neg = false;

	  str++;
	  if (*str == '-')
	    {
	      exp_neg = true;
	      str++;
	    }
	  else if (*str == '+')
	    str++;

	  d = 0;
	  while (ISDIGIT (*str))
	    {
	      d *= 10;
	      d += *str - '0';
	      if (d > MAX_EXP)
		{
		  /* Overflowed the exponent.  */
		  if (exp_neg)
		    goto underflow;
		  else
		    goto overflow;
		}
	      str++;
	    }
	  if (exp_neg)
	    d = -d;

	  exp += d;
	}

      r->cl = rvc_normal;
      SET_REAL_EXP (r, exp);

      normalize (r);
    }
  else
    {
      /* Decimal floating point.  */
      const REAL_VALUE_TYPE *ten = ten_to_ptwo (0);
      int d;

      while (*str == '0')
	str++;
      while (ISDIGIT (*str))
	{
	  d = *str++ - '0';
	  do_multiply (r, r, ten);
	  if (d)
	    do_add (r, r, real_digit (d), 0);
	}
      if (*str == '.')
	{
	  str++;
	  if (r->cl == rvc_zero)
	    {
	      while (*str == '0')
		str++, exp--;
	    }
	  while (ISDIGIT (*str))
	    {
	      d = *str++ - '0';
	      do_multiply (r, r, ten);
	      if (d)
	        do_add (r, r, real_digit (d), 0);
	      exp--;
	    }
	}

      /* If the mantissa is zero, ignore the exponent.  */
      if (r->cl == rvc_zero)
	goto underflow;

      if (*str == 'e' || *str == 'E')
	{
	  bool exp_neg = false;

	  str++;
	  if (*str == '-')
	    {
	      exp_neg = true;
	      str++;
	    }
	  else if (*str == '+')
	    str++;

	  d = 0;
	  while (ISDIGIT (*str))
	    {
	      d *= 10;
	      d += *str - '0';
	      if (d > MAX_EXP)
		{
		  /* Overflowed the exponent.  */
		  if (exp_neg)
		    goto underflow;
		  else
		    goto overflow;
		}
	      str++;
	    }
	  if (exp_neg)
	    d = -d;
	  exp += d;
	}

      if (exp)
	times_pten (r, exp);
    }

  r->sign = sign;
  return;

 underflow:
  get_zero (r, sign);
  return;

 overflow:
  get_inf (r, sign);
  return;
}

/* Legacy.  Similar, but return the result directly.  */

REAL_VALUE_TYPE
real_from_string2 (const char *s, enum machine_mode mode)
{
  REAL_VALUE_TYPE r;

  real_from_string (&r, s);
  if (mode != VOIDmode)
    real_convert (&r, mode, &r);

  return r;
}

/* Initialize R from string S and desired MODE. */

void 
real_from_string3 (REAL_VALUE_TYPE *r, const char *s, enum machine_mode mode)
{
  if (DECIMAL_FLOAT_MODE_P (mode))
    decimal_real_from_string (r, s);
  else
    real_from_string (r, s);

  if (mode != VOIDmode)
    real_convert (r, mode, r);  
} 

/* Initialize R from the integer pair HIGH+LOW.  */

void
real_from_integer (REAL_VALUE_TYPE *r, enum machine_mode mode,
		   unsigned HOST_WIDE_INT low, HOST_WIDE_INT high,
		   int unsigned_p)
{
  if (low == 0 && high == 0)
    get_zero (r, 0);
  else
    {
      memset (r, 0, sizeof (*r));
      r->cl = rvc_normal;
      r->sign = high < 0 && !unsigned_p;
      SET_REAL_EXP (r, 2 * HOST_BITS_PER_WIDE_INT);

      if (r->sign)
	{
	  high = ~high;
	  if (low == 0)
	    high += 1;
	  else
	    low = -low;
	}

      if (HOST_BITS_PER_LONG == HOST_BITS_PER_WIDE_INT)
	{
	  r->sig[SIGSZ-1] = high;
	  r->sig[SIGSZ-2] = low;
	}
      else
	{
	  gcc_assert (HOST_BITS_PER_LONG*2 == HOST_BITS_PER_WIDE_INT);
	  r->sig[SIGSZ-1] = high >> (HOST_BITS_PER_LONG - 1) >> 1;
	  r->sig[SIGSZ-2] = high;
	  r->sig[SIGSZ-3] = low >> (HOST_BITS_PER_LONG - 1) >> 1;
	  r->sig[SIGSZ-4] = low;
	}

      normalize (r);
    }

  if (mode != VOIDmode)
    real_convert (r, mode, r);
}

/* Returns 10**2**N.  */

static const REAL_VALUE_TYPE *
ten_to_ptwo (int n)
{
  static REAL_VALUE_TYPE tens[EXP_BITS];

  gcc_assert (n >= 0);
  gcc_assert (n < EXP_BITS);

  if (tens[n].cl == rvc_zero)
    {
      if (n < (HOST_BITS_PER_WIDE_INT == 64 ? 5 : 4))
	{
	  HOST_WIDE_INT t = 10;
	  int i;

	  for (i = 0; i < n; ++i)
	    t *= t;

	  real_from_integer (&tens[n], VOIDmode, t, 0, 1);
	}
      else
	{
	  const REAL_VALUE_TYPE *t = ten_to_ptwo (n - 1);
	  do_multiply (&tens[n], t, t);
	}
    }

  return &tens[n];
}

/* Returns 10**(-2**N).  */

static const REAL_VALUE_TYPE *
ten_to_mptwo (int n)
{
  static REAL_VALUE_TYPE tens[EXP_BITS];

  gcc_assert (n >= 0);
  gcc_assert (n < EXP_BITS);

  if (tens[n].cl == rvc_zero)
    do_divide (&tens[n], real_digit (1), ten_to_ptwo (n));

  return &tens[n];
}

/* Returns N.  */

static const REAL_VALUE_TYPE *
real_digit (int n)
{
  static REAL_VALUE_TYPE num[10];

  gcc_assert (n >= 0);
  gcc_assert (n <= 9);

  if (n > 0 && num[n].cl == rvc_zero)
    real_from_integer (&num[n], VOIDmode, n, 0, 1);

  return &num[n];
}

/* Multiply R by 10**EXP.  */

static void
times_pten (REAL_VALUE_TYPE *r, int exp)
{
  REAL_VALUE_TYPE pten, *rr;
  bool negative = (exp < 0);
  int i;

  if (negative)
    {
      exp = -exp;
      pten = *real_digit (1);
      rr = &pten;
    }
  else
    rr = r;

  for (i = 0; exp > 0; ++i, exp >>= 1)
    if (exp & 1)
      do_multiply (rr, rr, ten_to_ptwo (i));

  if (negative)
    do_divide (r, r, &pten);
}

/* Fills R with +Inf.  */

void
real_inf (REAL_VALUE_TYPE *r)
{
  get_inf (r, 0);
}

/* Fills R with a NaN whose significand is described by STR.  If QUIET,
   we force a QNaN, else we force an SNaN.  The string, if not empty,
   is parsed as a number and placed in the significand.  Return true
   if the string was successfully parsed.  */

bool
real_nan (REAL_VALUE_TYPE *r, const char *str, int quiet,
	  enum machine_mode mode)
{
  const struct real_format *fmt;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);

  if (*str == 0)
    {
      if (quiet)
	get_canonical_qnan (r, 0);
      else
	get_canonical_snan (r, 0);
    }
  else
    {
      int base = 10, d;

      memset (r, 0, sizeof (*r));
      r->cl = rvc_nan;

      /* Parse akin to strtol into the significand of R.  */

      while (ISSPACE (*str))
	str++;
      if (*str == '-')
	str++;
      else if (*str == '+')
	str++;
      if (*str == '0')
	{
	  str++;
	  if (*str == 'x' || *str == 'X')
	    {
	      base = 16;
	      str++;
	    }
	  else
	    base = 8;
	}

      while ((d = hex_value (*str)) < base)
	{
	  REAL_VALUE_TYPE u;

	  switch (base)
	    {
	    case 8:
	      lshift_significand (r, r, 3);
	      break;
	    case 16:
	      lshift_significand (r, r, 4);
	      break;
	    case 10:
	      lshift_significand_1 (&u, r);
	      lshift_significand (r, r, 3);
	      add_significands (r, r, &u);
	      break;
	    default:
	      gcc_unreachable ();
	    }

	  get_zero (&u, 0);
	  u.sig[0] = d;
	  add_significands (r, r, &u);

	  str++;
	}

      /* Must have consumed the entire string for success.  */
      if (*str != 0)
	return false;

      /* Shift the significand into place such that the bits
	 are in the most significant bits for the format.  */
      lshift_significand (r, r, SIGNIFICAND_BITS - fmt->pnan);

      /* Our MSB is always unset for NaNs.  */
      r->sig[SIGSZ-1] &= ~SIG_MSB;

      /* Force quiet or signalling NaN.  */
      r->signalling = !quiet;
    }

  return true;
}

/* Fills R with the largest finite value representable in mode MODE.
   If SIGN is nonzero, R is set to the most negative finite value.  */

void
real_maxval (REAL_VALUE_TYPE *r, int sign, enum machine_mode mode)
{
  const struct real_format *fmt;
  int np2;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);
  memset (r, 0, sizeof (*r));
  
  if (fmt->b == 10)
    decimal_real_maxval (r, sign, mode);
  else
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, fmt->emax * fmt->log2_b);

      np2 = SIGNIFICAND_BITS - fmt->p * fmt->log2_b;
      memset (r->sig, -1, SIGSZ * sizeof (unsigned long));
      clear_significand_below (r, np2);

      if (fmt->pnan < fmt->p)
	/* This is an IBM extended double format made up of two IEEE
	   doubles.  The value of the long double is the sum of the
	   values of the two parts.  The most significant part is
	   required to be the value of the long double rounded to the
	   nearest double.  Rounding means we need a slightly smaller
	   value for LDBL_MAX.  */
        clear_significand_bit (r, SIGNIFICAND_BITS - fmt->pnan);
    }
}

/* Fills R with 2**N.  */

void
real_2expN (REAL_VALUE_TYPE *r, int n)
{
  memset (r, 0, sizeof (*r));

  n++;
  if (n > MAX_EXP)
    r->cl = rvc_inf;
  else if (n < -MAX_EXP)
    ;
  else
    {
      r->cl = rvc_normal;
      SET_REAL_EXP (r, n);
      r->sig[SIGSZ-1] = SIG_MSB;
    }
}


static void
round_for_format (const struct real_format *fmt, REAL_VALUE_TYPE *r)
{
  int p2, np2, i, w;
  unsigned long sticky;
  bool guard, lsb;
  int emin2m1, emax2;

  if (r->decimal)
    {
      if (fmt->b == 10)
	{
	  decimal_round_for_format (fmt, r);
	  return;
	}
      /* FIXME. We can come here via fp_easy_constant
	 (e.g. -O0 on '_Decimal32 x = 1.0 + 2.0dd'), but have not
	 investigated whether this convert needs to be here, or
	 something else is missing. */
      decimal_real_convert (r, DFmode, r);
    }

  p2 = fmt->p * fmt->log2_b;
  emin2m1 = (fmt->emin - 1) * fmt->log2_b;
  emax2 = fmt->emax * fmt->log2_b;

  np2 = SIGNIFICAND_BITS - p2;
  switch (r->cl)
    {
    underflow:
      get_zero (r, r->sign);
    case rvc_zero:
      if (!fmt->has_signed_zero)
	r->sign = 0;
      return;

    overflow:
      get_inf (r, r->sign);
    case rvc_inf:
      return;

    case rvc_nan:
      clear_significand_below (r, np2);
      return;

    case rvc_normal:
      break;

    default:
      gcc_unreachable ();
    }

  /* If we're not base2, normalize the exponent to a multiple of
     the true base.  */
  if (fmt->log2_b != 1)
    {
      int shift;

      gcc_assert (fmt->b != 10);
      shift = REAL_EXP (r) & (fmt->log2_b - 1);
      if (shift)
	{
	  shift = fmt->log2_b - shift;
	  r->sig[0] |= sticky_rshift_significand (r, r, shift);
	  SET_REAL_EXP (r, REAL_EXP (r) + shift);
	}
    }

  /* Check the range of the exponent.  If we're out of range,
     either underflow or overflow.  */
  if (REAL_EXP (r) > emax2)
    goto overflow;
  else if (REAL_EXP (r) <= emin2m1)
    {
      int diff;

      if (!fmt->has_denorm)
	{
	  /* Don't underflow completely until we've had a chance to round.  */
	  if (REAL_EXP (r) < emin2m1)
	    goto underflow;
	}
      else
	{
	  diff = emin2m1 - REAL_EXP (r) + 1;
	  if (diff > p2)
	    goto underflow;

	  /* De-normalize the significand.  */
	  r->sig[0] |= sticky_rshift_significand (r, r, diff);
	  SET_REAL_EXP (r, REAL_EXP (r) + diff);
	}
    }

  /* There are P2 true significand bits, followed by one guard bit,
     followed by one sticky bit, followed by stuff.  Fold nonzero
     stuff into the sticky bit.  */

  sticky = 0;
  for (i = 0, w = (np2 - 1) / HOST_BITS_PER_LONG; i < w; ++i)
    sticky |= r->sig[i];
  sticky |=
    r->sig[w] & (((unsigned long)1 << ((np2 - 1) % HOST_BITS_PER_LONG)) - 1);

  guard = test_significand_bit (r, np2 - 1);
  lsb = test_significand_bit (r, np2);

  /* Round to even.  */
  if (guard && (sticky || lsb))
    {
      REAL_VALUE_TYPE u;
      get_zero (&u, 0);
      set_significand_bit (&u, np2);

      if (add_significands (r, r, &u))
	{
	  /* Overflow.  Means the significand had been all ones, and
	     is now all zeros.  Need to increase the exponent, and
	     possibly re-normalize it.  */
	  SET_REAL_EXP (r, REAL_EXP (r) + 1);
	  if (REAL_EXP (r) > emax2)
	    goto overflow;
	  r->sig[SIGSZ-1] = SIG_MSB;

	  if (fmt->log2_b != 1)
	    {
	      int shift = REAL_EXP (r) & (fmt->log2_b - 1);
	      if (shift)
		{
		  shift = fmt->log2_b - shift;
		  rshift_significand (r, r, shift);
		  SET_REAL_EXP (r, REAL_EXP (r) + shift);
		  if (REAL_EXP (r) > emax2)
		    goto overflow;
		}
	    }
	}
    }

  /* Catch underflow that we deferred until after rounding.  */
  if (REAL_EXP (r) <= emin2m1)
    goto underflow;

  /* Clear out trailing garbage.  */
  clear_significand_below (r, np2);
}

/* Extend or truncate to a new mode.  */

void
real_convert (REAL_VALUE_TYPE *r, enum machine_mode mode,
	      const REAL_VALUE_TYPE *a)
{
  const struct real_format *fmt;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);

  *r = *a;

  if (a->decimal || fmt->b == 10)
    decimal_real_convert (r, mode, a);

  round_for_format (fmt, r);

  /* round_for_format de-normalizes denormals.  Undo just that part.  */
  if (r->cl == rvc_normal)
    normalize (r);
}

/* Legacy.  Likewise, except return the struct directly.  */

REAL_VALUE_TYPE
real_value_truncate (enum machine_mode mode, REAL_VALUE_TYPE a)
{
  REAL_VALUE_TYPE r;
  real_convert (&r, mode, &a);
  return r;
}

/* Return true if truncating to MODE is exact.  */

bool
exact_real_truncate (enum machine_mode mode, const REAL_VALUE_TYPE *a)
{
  const struct real_format *fmt;
  REAL_VALUE_TYPE t;
  int emin2m1;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);

  /* Don't allow conversion to denormals.  */
  emin2m1 = (fmt->emin - 1) * fmt->log2_b;
  if (REAL_EXP (a) <= emin2m1)
    return false;

  /* After conversion to the new mode, the value must be identical.  */
  real_convert (&t, mode, a);
  return real_identical (&t, a);
}

/* Write R to the given target format.  Place the words of the result
   in target word order in BUF.  There are always 32 bits in each
   long, no matter the size of the host long.

   Legacy: return word 0 for implementing REAL_VALUE_TO_TARGET_SINGLE.  */

long
real_to_target_fmt (long *buf, const REAL_VALUE_TYPE *r_orig,
		    const struct real_format *fmt)
{
  REAL_VALUE_TYPE r;
  long buf1;

  r = *r_orig;
  round_for_format (fmt, &r);

  if (!buf)
    buf = &buf1;
  (*fmt->encode) (fmt, buf, &r);

  return *buf;
}

/* Similar, but look up the format from MODE.  */

long
real_to_target (long *buf, const REAL_VALUE_TYPE *r, enum machine_mode mode)
{
  const struct real_format *fmt;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);

  return real_to_target_fmt (buf, r, fmt);
}

/* Read R from the given target format.  Read the words of the result
   in target word order in BUF.  There are always 32 bits in each
   long, no matter the size of the host long.  */

void
real_from_target_fmt (REAL_VALUE_TYPE *r, const long *buf,
		      const struct real_format *fmt)
{
  (*fmt->decode) (fmt, r, buf);
}

/* Similar, but look up the format from MODE.  */

void
real_from_target (REAL_VALUE_TYPE *r, const long *buf, enum machine_mode mode)
{
  const struct real_format *fmt;

  fmt = REAL_MODE_FORMAT (mode);
  gcc_assert (fmt);

  (*fmt->decode) (fmt, r, buf);
}

/* Return the number of bits of the largest binary value that the
   significand of MODE will hold.  */
/* ??? Legacy.  Should get access to real_format directly.  */

int
significand_size (enum machine_mode mode)
{
  const struct real_format *fmt;

  fmt = REAL_MODE_FORMAT (mode);
  if (fmt == NULL)
    return 0;

  if (fmt->b == 10)
    {
      /* Return the size in bits of the largest binary value that can be
	 held by the decimal coefficient for this mode.  This is one more
	 than the number of bits required to hold the largest coefficient
	 of this mode.  */
      double log2_10 = 3.3219281;
      return fmt->p * log2_10;
    }
  return fmt->p * fmt->log2_b;
}

/* Return a hash value for the given real value.  */
/* ??? The "unsigned int" return value is intended to be hashval_t,
   but I didn't want to pull hashtab.h into real.h.  */

unsigned int
real_hash (const REAL_VALUE_TYPE *r)
{
  unsigned int h;
  size_t i;

  h = r->cl | (r->sign << 2);
  switch (r->cl)
    {
    case rvc_zero:
    case rvc_inf:
      return h;

    case rvc_normal:
      h |= REAL_EXP (r) << 3;
      break;

    case rvc_nan:
      if (r->signalling)
	h ^= (unsigned int)-1;
      if (r->canonical)
	return h;
      break;

    default:
      gcc_unreachable ();
    }

  if (sizeof(unsigned long) > sizeof(unsigned int))
    for (i = 0; i < SIGSZ; ++i)
      {
	unsigned long s = r->sig[i];
	h ^= s ^ (s >> (HOST_BITS_PER_LONG / 2));
      }
  else
    for (i = 0; i < SIGSZ; ++i)
      h ^= r->sig[i];

  return h;
}

/* IEEE single-precision format.  */

static void encode_ieee_single (const struct real_format *fmt,
				long *, const REAL_VALUE_TYPE *);
static void decode_ieee_single (const struct real_format *,
				REAL_VALUE_TYPE *, const long *);

static void
encode_ieee_single (const struct real_format *fmt, long *buf,
		    const REAL_VALUE_TYPE *r)
{
  unsigned long image, sig, exp;
  unsigned long sign = r->sign;
  bool denormal = (r->sig[SIGSZ-1] & SIG_MSB) == 0;

  image = sign << 31;
  sig = (r->sig[SIGSZ-1] >> (HOST_BITS_PER_LONG - 24)) & 0x7fffff;

  switch (r->cl)
    {
    case rvc_zero:
      break;

    case rvc_inf:
      if (fmt->has_inf)
	image |= 255 << 23;
      else
	image |= 0x7fffffff;
      break;

    case rvc_nan:
      if (fmt->has_nans)
	{
	  if (r->canonical)
	    sig = 0;
	  if (r->signalling == fmt->qnan_msb_set)
	    sig &= ~(1 << 22);
	  else
	    sig |= 1 << 22;
	  /* We overload qnan_msb_set here: it's only clear for
	     mips_ieee_single, which wants all mantissa bits but the
	     quiet/signalling one set in canonical NaNs (at least
	     Quiet ones).  */
	  if (r->canonical && !fmt->qnan_msb_set)
	    sig |= (1 << 22) - 1;
	  else if (sig == 0)
	    sig = 1 << 21;

	  image |= 255 << 23;
	  image |= sig;
	}
      else
	image |= 0x7fffffff;
      break;

    case rvc_normal:
      /* Recall that IEEE numbers are interpreted as 1.F x 2**exp,
	 whereas the intermediate representation is 0.F x 2**exp.
	 Which means we're off by one.  */
      if (denormal)
	exp = 0;
      else
      exp = REAL_EXP (r) + 127 - 1;
      image |= exp << 23;
      image |= sig;
      break;

    default:
      gcc_unreachable ();
    }

  buf[0] = image;
}

static void
decode_ieee_single (const struct real_format *fmt, REAL_VALUE_TYPE *r,
		    const long *buf)
{
  unsigned long image = buf[0] & 0xffffffff;
  bool sign = (image >> 31) & 1;
  int exp = (image >> 23) & 0xff;

  memset (r, 0, sizeof (*r));
  image <<= HOST_BITS_PER_LONG - 24;
  image &= ~SIG_MSB;

  if (exp == 0)
    {
      if (image && fmt->has_denorm)
	{
	  r->cl = rvc_normal;
	  r->sign = sign;
	  SET_REAL_EXP (r, -126);
	  r->sig[SIGSZ-1] = image << 1;
	  normalize (r);
	}
      else if (fmt->has_signed_zero)
	r->sign = sign;
    }
  else if (exp == 255 && (fmt->has_nans || fmt->has_inf))
    {
      if (image)
	{
	  r->cl = rvc_nan;
	  r->sign = sign;
	  r->signalling = (((image >> (HOST_BITS_PER_LONG - 2)) & 1)
			   ^ fmt->qnan_msb_set);
	  r->sig[SIGSZ-1] = image;
	}
      else
	{
	  r->cl = rvc_inf;
	  r->sign = sign;
	}
    }
  else
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, exp - 127 + 1);
      r->sig[SIGSZ-1] = image | SIG_MSB;
    }
}

const struct real_format ieee_single_format =
  {
    encode_ieee_single,
    decode_ieee_single,
    2,
    1,
    24,
    24,
    -125,
    128,
    31,
    31,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format mips_single_format =
  {
    encode_ieee_single,
    decode_ieee_single,
    2,
    1,
    24,
    24,
    -125,
    128,
    31,
    31,
    true,
    true,
    true,
    true,
    false
  };


/* IEEE double-precision format.  */

static void encode_ieee_double (const struct real_format *fmt,
				long *, const REAL_VALUE_TYPE *);
static void decode_ieee_double (const struct real_format *,
				REAL_VALUE_TYPE *, const long *);

static void
encode_ieee_double (const struct real_format *fmt, long *buf,
		    const REAL_VALUE_TYPE *r)
{
  unsigned long image_lo, image_hi, sig_lo, sig_hi, exp;
  bool denormal = (r->sig[SIGSZ-1] & SIG_MSB) == 0;

  image_hi = r->sign << 31;
  image_lo = 0;

  if (HOST_BITS_PER_LONG == 64)
    {
      sig_hi = r->sig[SIGSZ-1];
      sig_lo = (sig_hi >> (64 - 53)) & 0xffffffff;
      sig_hi = (sig_hi >> (64 - 53 + 1) >> 31) & 0xfffff;
    }
  else
    {
      sig_hi = r->sig[SIGSZ-1];
      sig_lo = r->sig[SIGSZ-2];
      sig_lo = (sig_hi << 21) | (sig_lo >> 11);
      sig_hi = (sig_hi >> 11) & 0xfffff;
    }

  switch (r->cl)
    {
    case rvc_zero:
      break;

    case rvc_inf:
      if (fmt->has_inf)
	image_hi |= 2047 << 20;
      else
	{
	  image_hi |= 0x7fffffff;
	  image_lo = 0xffffffff;
	}
      break;

    case rvc_nan:
      if (fmt->has_nans)
	{
	  if (r->canonical)
	    sig_hi = sig_lo = 0;
	  if (r->signalling == fmt->qnan_msb_set)
	    sig_hi &= ~(1 << 19);
	  else
	    sig_hi |= 1 << 19;
	  /* We overload qnan_msb_set here: it's only clear for
	     mips_ieee_single, which wants all mantissa bits but the
	     quiet/signalling one set in canonical NaNs (at least
	     Quiet ones).  */
	  if (r->canonical && !fmt->qnan_msb_set)
	    {
	      sig_hi |= (1 << 19) - 1;
	      sig_lo = 0xffffffff;
	    }
	  else if (sig_hi == 0 && sig_lo == 0)
	    sig_hi = 1 << 18;

	  image_hi |= 2047 << 20;
	  image_hi |= sig_hi;
	  image_lo = sig_lo;
	}
      else
	{
	  image_hi |= 0x7fffffff;
	  image_lo = 0xffffffff;
	}
      break;

    case rvc_normal:
      /* Recall that IEEE numbers are interpreted as 1.F x 2**exp,
	 whereas the intermediate representation is 0.F x 2**exp.
	 Which means we're off by one.  */
      if (denormal)
	exp = 0;
      else
	exp = REAL_EXP (r) + 1023 - 1;
      image_hi |= exp << 20;
      image_hi |= sig_hi;
      image_lo = sig_lo;
      break;

    default:
      gcc_unreachable ();
    }

  if (FLOAT_WORDS_BIG_ENDIAN)
    buf[0] = image_hi, buf[1] = image_lo;
  else
    buf[0] = image_lo, buf[1] = image_hi;
}

static void
decode_ieee_double (const struct real_format *fmt, REAL_VALUE_TYPE *r,
		    const long *buf)
{
  unsigned long image_hi, image_lo;
  bool sign;
  int exp;

  if (FLOAT_WORDS_BIG_ENDIAN)
    image_hi = buf[0], image_lo = buf[1];
  else
    image_lo = buf[0], image_hi = buf[1];
  image_lo &= 0xffffffff;
  image_hi &= 0xffffffff;

  sign = (image_hi >> 31) & 1;
  exp = (image_hi >> 20) & 0x7ff;

  memset (r, 0, sizeof (*r));

  image_hi <<= 32 - 21;
  image_hi |= image_lo >> 21;
  image_hi &= 0x7fffffff;
  image_lo <<= 32 - 21;

  if (exp == 0)
    {
      if ((image_hi || image_lo) && fmt->has_denorm)
	{
	  r->cl = rvc_normal;
	  r->sign = sign;
	  SET_REAL_EXP (r, -1022);
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      image_hi = (image_hi << 1) | (image_lo >> 31);
	      image_lo <<= 1;
	      r->sig[SIGSZ-1] = image_hi;
	      r->sig[SIGSZ-2] = image_lo;
	    }
	  else
	    {
	      image_hi = (image_hi << 31 << 2) | (image_lo << 1);
	      r->sig[SIGSZ-1] = image_hi;
	    }
	  normalize (r);
	}
      else if (fmt->has_signed_zero)
	r->sign = sign;
    }
  else if (exp == 2047 && (fmt->has_nans || fmt->has_inf))
    {
      if (image_hi || image_lo)
	{
	  r->cl = rvc_nan;
	  r->sign = sign;
	  r->signalling = ((image_hi >> 30) & 1) ^ fmt->qnan_msb_set;
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      r->sig[SIGSZ-1] = image_hi;
	      r->sig[SIGSZ-2] = image_lo;
	    }
	  else
	    r->sig[SIGSZ-1] = (image_hi << 31 << 1) | image_lo;
	}
      else
	{
	  r->cl = rvc_inf;
	  r->sign = sign;
	}
    }
  else
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, exp - 1023 + 1);
      if (HOST_BITS_PER_LONG == 32)
	{
	  r->sig[SIGSZ-1] = image_hi | SIG_MSB;
	  r->sig[SIGSZ-2] = image_lo;
	}
      else
	r->sig[SIGSZ-1] = (image_hi << 31 << 1) | image_lo | SIG_MSB;
    }
}

const struct real_format ieee_double_format =
  {
    encode_ieee_double,
    decode_ieee_double,
    2,
    1,
    53,
    53,
    -1021,
    1024,
    63,
    63,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format mips_double_format =
  {
    encode_ieee_double,
    decode_ieee_double,
    2,
    1,
    53,
    53,
    -1021,
    1024,
    63,
    63,
    true,
    true,
    true,
    true,
    false
  };


/* IEEE extended real format.  This comes in three flavors: Intel's as
   a 12 byte image, Intel's as a 16 byte image, and Motorola's.  Intel
   12- and 16-byte images may be big- or little endian; Motorola's is
   always big endian.  */

/* Helper subroutine which converts from the internal format to the
   12-byte little-endian Intel format.  Functions below adjust this
   for the other possible formats.  */
static void
encode_ieee_extended (const struct real_format *fmt, long *buf,
		      const REAL_VALUE_TYPE *r)
{
  unsigned long image_hi, sig_hi, sig_lo;
  bool denormal = (r->sig[SIGSZ-1] & SIG_MSB) == 0;

  image_hi = r->sign << 15;
  sig_hi = sig_lo = 0;

  switch (r->cl)
    {
    case rvc_zero:
      break;

    case rvc_inf:
      if (fmt->has_inf)
	{
	  image_hi |= 32767;

	  /* Intel requires the explicit integer bit to be set, otherwise
	     it considers the value a "pseudo-infinity".  Motorola docs
	     say it doesn't care.  */
	  sig_hi = 0x80000000;
	}
      else
	{
	  image_hi |= 32767;
	  sig_lo = sig_hi = 0xffffffff;
	}
      break;

    case rvc_nan:
      if (fmt->has_nans)
	{
	  image_hi |= 32767;
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      sig_hi = r->sig[SIGSZ-1];
	      sig_lo = r->sig[SIGSZ-2];
	    }
	  else
	    {
	      sig_lo = r->sig[SIGSZ-1];
	      sig_hi = sig_lo >> 31 >> 1;
	      sig_lo &= 0xffffffff;
	    }
	  if (r->signalling == fmt->qnan_msb_set)
	    sig_hi &= ~(1 << 30);
	  else
	    sig_hi |= 1 << 30;
	  if ((sig_hi & 0x7fffffff) == 0 && sig_lo == 0)
	    sig_hi = 1 << 29;

	  /* Intel requires the explicit integer bit to be set, otherwise
	     it considers the value a "pseudo-nan".  Motorola docs say it
	     doesn't care.  */
	  sig_hi |= 0x80000000;
	}
      else
	{
	  image_hi |= 32767;
	  sig_lo = sig_hi = 0xffffffff;
	}
      break;

    case rvc_normal:
      {
	int exp = REAL_EXP (r);

	/* Recall that IEEE numbers are interpreted as 1.F x 2**exp,
	   whereas the intermediate representation is 0.F x 2**exp.
	   Which means we're off by one.

	   Except for Motorola, which consider exp=0 and explicit
	   integer bit set to continue to be normalized.  In theory
	   this discrepancy has been taken care of by the difference
	   in fmt->emin in round_for_format.  */

	if (denormal)
	  exp = 0;
	else
	  {
	    exp += 16383 - 1;
	    gcc_assert (exp >= 0);
	  }
	image_hi |= exp;

	if (HOST_BITS_PER_LONG == 32)
	  {
	    sig_hi = r->sig[SIGSZ-1];
	    sig_lo = r->sig[SIGSZ-2];
	  }
	else
	  {
	    sig_lo = r->sig[SIGSZ-1];
	    sig_hi = sig_lo >> 31 >> 1;
	    sig_lo &= 0xffffffff;
	  }
      }
      break;

    default:
      gcc_unreachable ();
    }

  buf[0] = sig_lo, buf[1] = sig_hi, buf[2] = image_hi;
}

/* Convert from the internal format to the 12-byte Motorola format
   for an IEEE extended real.  */
static void
encode_ieee_extended_motorola (const struct real_format *fmt, long *buf,
			       const REAL_VALUE_TYPE *r)
{
  long intermed[3];
  encode_ieee_extended (fmt, intermed, r);

  /* Motorola chips are assumed always to be big-endian.  Also, the
     padding in a Motorola extended real goes between the exponent and
     the mantissa.  At this point the mantissa is entirely within
     elements 0 and 1 of intermed, and the exponent entirely within
     element 2, so all we have to do is swap the order around, and
     shift element 2 left 16 bits.  */
  buf[0] = intermed[2] << 16;
  buf[1] = intermed[1];
  buf[2] = intermed[0];
}

/* Convert from the internal format to the 12-byte Intel format for
   an IEEE extended real.  */
static void
encode_ieee_extended_intel_96 (const struct real_format *fmt, long *buf,
			       const REAL_VALUE_TYPE *r)
{
  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      /* All the padding in an Intel-format extended real goes at the high
	 end, which in this case is after the mantissa, not the exponent.
	 Therefore we must shift everything down 16 bits.  */
      long intermed[3];
      encode_ieee_extended (fmt, intermed, r);
      buf[0] = ((intermed[2] << 16) | ((unsigned long)(intermed[1] & 0xFFFF0000) >> 16));
      buf[1] = ((intermed[1] << 16) | ((unsigned long)(intermed[0] & 0xFFFF0000) >> 16));
      buf[2] =  (intermed[0] << 16);
    }
  else
    /* encode_ieee_extended produces what we want directly.  */
    encode_ieee_extended (fmt, buf, r);
}

/* Convert from the internal format to the 16-byte Intel format for
   an IEEE extended real.  */
static void
encode_ieee_extended_intel_128 (const struct real_format *fmt, long *buf,
				const REAL_VALUE_TYPE *r)
{
  /* All the padding in an Intel-format extended real goes at the high end.  */
  encode_ieee_extended_intel_96 (fmt, buf, r);
  buf[3] = 0;
}

/* As above, we have a helper function which converts from 12-byte
   little-endian Intel format to internal format.  Functions below
   adjust for the other possible formats.  */
static void
decode_ieee_extended (const struct real_format *fmt, REAL_VALUE_TYPE *r,
		      const long *buf)
{
  unsigned long image_hi, sig_hi, sig_lo;
  bool sign;
  int exp;

  sig_lo = buf[0], sig_hi = buf[1], image_hi = buf[2];
  sig_lo &= 0xffffffff;
  sig_hi &= 0xffffffff;
  image_hi &= 0xffffffff;

  sign = (image_hi >> 15) & 1;
  exp = image_hi & 0x7fff;

  memset (r, 0, sizeof (*r));

  if (exp == 0)
    {
      if ((sig_hi || sig_lo) && fmt->has_denorm)
	{
	  r->cl = rvc_normal;
	  r->sign = sign;

	  /* When the IEEE format contains a hidden bit, we know that
	     it's zero at this point, and so shift up the significand
	     and decrease the exponent to match.  In this case, Motorola
	     defines the explicit integer bit to be valid, so we don't
	     know whether the msb is set or not.  */
	  SET_REAL_EXP (r, fmt->emin);
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      r->sig[SIGSZ-1] = sig_hi;
	      r->sig[SIGSZ-2] = sig_lo;
	    }
	  else
	    r->sig[SIGSZ-1] = (sig_hi << 31 << 1) | sig_lo;

	  normalize (r);
	}
      else if (fmt->has_signed_zero)
	r->sign = sign;
    }
  else if (exp == 32767 && (fmt->has_nans || fmt->has_inf))
    {
      /* See above re "pseudo-infinities" and "pseudo-nans".
	 Short summary is that the MSB will likely always be
	 set, and that we don't care about it.  */
      sig_hi &= 0x7fffffff;

      if (sig_hi || sig_lo)
	{
	  r->cl = rvc_nan;
	  r->sign = sign;
	  r->signalling = ((sig_hi >> 30) & 1) ^ fmt->qnan_msb_set;
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      r->sig[SIGSZ-1] = sig_hi;
	      r->sig[SIGSZ-2] = sig_lo;
	    }
	  else
	    r->sig[SIGSZ-1] = (sig_hi << 31 << 1) | sig_lo;
	}
      else
	{
	  r->cl = rvc_inf;
	  r->sign = sign;
	}
    }
  else
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, exp - 16383 + 1);
      if (HOST_BITS_PER_LONG == 32)
	{
	  r->sig[SIGSZ-1] = sig_hi;
	  r->sig[SIGSZ-2] = sig_lo;
	}
      else
	r->sig[SIGSZ-1] = (sig_hi << 31 << 1) | sig_lo;
    }
}

/* Convert from the internal format to the 12-byte Motorola format
   for an IEEE extended real.  */
static void
decode_ieee_extended_motorola (const struct real_format *fmt, REAL_VALUE_TYPE *r,
			       const long *buf)
{
  long intermed[3];

  /* Motorola chips are assumed always to be big-endian.  Also, the
     padding in a Motorola extended real goes between the exponent and
     the mantissa; remove it.  */
  intermed[0] = buf[2];
  intermed[1] = buf[1];
  intermed[2] = (unsigned long)buf[0] >> 16;

  decode_ieee_extended (fmt, r, intermed);
}

/* Convert from the internal format to the 12-byte Intel format for
   an IEEE extended real.  */
static void
decode_ieee_extended_intel_96 (const struct real_format *fmt, REAL_VALUE_TYPE *r,
			       const long *buf)
{
  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      /* All the padding in an Intel-format extended real goes at the high
	 end, which in this case is after the mantissa, not the exponent.
	 Therefore we must shift everything up 16 bits.  */
      long intermed[3];

      intermed[0] = (((unsigned long)buf[2] >> 16) | (buf[1] << 16));
      intermed[1] = (((unsigned long)buf[1] >> 16) | (buf[0] << 16));
      intermed[2] =  ((unsigned long)buf[0] >> 16);

      decode_ieee_extended (fmt, r, intermed);
    }
  else
    /* decode_ieee_extended produces what we want directly.  */
    decode_ieee_extended (fmt, r, buf);
}

/* Convert from the internal format to the 16-byte Intel format for
   an IEEE extended real.  */
static void
decode_ieee_extended_intel_128 (const struct real_format *fmt, REAL_VALUE_TYPE *r,
				const long *buf)
{
  /* All the padding in an Intel-format extended real goes at the high end.  */
  decode_ieee_extended_intel_96 (fmt, r, buf);
}

const struct real_format ieee_extended_motorola_format =
  {
    encode_ieee_extended_motorola,
    decode_ieee_extended_motorola,
    2,
    1,
    64,
    64,
    -16382,
    16384,
    95,
    95,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format ieee_extended_intel_96_format =
  {
    encode_ieee_extended_intel_96,
    decode_ieee_extended_intel_96,
    2,
    1,
    64,
    64,
    -16381,
    16384,
    79,
    79,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format ieee_extended_intel_128_format =
  {
    encode_ieee_extended_intel_128,
    decode_ieee_extended_intel_128,
    2,
    1,
    64,
    64,
    -16381,
    16384,
    79,
    79,
    true,
    true,
    true,
    true,
    true
  };

/* The following caters to i386 systems that set the rounding precision
   to 53 bits instead of 64, e.g. FreeBSD.  */
const struct real_format ieee_extended_intel_96_round_53_format =
  {
    encode_ieee_extended_intel_96,
    decode_ieee_extended_intel_96,
    2,
    1,
    53,
    53,
    -16381,
    16384,
    79,
    79,
    true,
    true,
    true,
    true,
    true
  };

/* IBM 128-bit extended precision format: a pair of IEEE double precision
   numbers whose sum is equal to the extended precision value.  The number
   with greater magnitude is first.  This format has the same magnitude
   range as an IEEE double precision value, but effectively 106 bits of
   significand precision.  Infinity and NaN are represented by their IEEE
   double precision value stored in the first number, the second number is
   +0.0 or -0.0 for Infinity and don't-care for NaN.  */

static void encode_ibm_extended (const struct real_format *fmt,
				 long *, const REAL_VALUE_TYPE *);
static void decode_ibm_extended (const struct real_format *,
				 REAL_VALUE_TYPE *, const long *);

static void
encode_ibm_extended (const struct real_format *fmt, long *buf,
		     const REAL_VALUE_TYPE *r)
{
  REAL_VALUE_TYPE u, normr, v;
  const struct real_format *base_fmt;

  base_fmt = fmt->qnan_msb_set ? &ieee_double_format : &mips_double_format;

  /* Renormlize R before doing any arithmetic on it.  */
  normr = *r;
  if (normr.cl == rvc_normal)
    normalize (&normr);

  /* u = IEEE double precision portion of significand.  */
  u = normr;
  round_for_format (base_fmt, &u);
  encode_ieee_double (base_fmt, &buf[0], &u);

  if (u.cl == rvc_normal)
    {
      do_add (&v, &normr, &u, 1);
      /* Call round_for_format since we might need to denormalize.  */
      round_for_format (base_fmt, &v);
      encode_ieee_double (base_fmt, &buf[2], &v);
    }
  else
    {
      /* Inf, NaN, 0 are all representable as doubles, so the
	 least-significant part can be 0.0.  */
      buf[2] = 0;
      buf[3] = 0;
    }
}

static void
decode_ibm_extended (const struct real_format *fmt ATTRIBUTE_UNUSED, REAL_VALUE_TYPE *r,
		     const long *buf)
{
  REAL_VALUE_TYPE u, v;
  const struct real_format *base_fmt;

  base_fmt = fmt->qnan_msb_set ? &ieee_double_format : &mips_double_format;
  decode_ieee_double (base_fmt, &u, &buf[0]);

  if (u.cl != rvc_zero && u.cl != rvc_inf && u.cl != rvc_nan)
    {
      decode_ieee_double (base_fmt, &v, &buf[2]);
      do_add (r, &u, &v, 0);
    }
  else
    *r = u;
}

const struct real_format ibm_extended_format =
  {
    encode_ibm_extended,
    decode_ibm_extended,
    2,
    1,
    53 + 53,
    53,
    -1021 + 53,
    1024,
    127,
    -1,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format mips_extended_format =
  {
    encode_ibm_extended,
    decode_ibm_extended,
    2,
    1,
    53 + 53,
    53,
    -1021 + 53,
    1024,
    127,
    -1,
    true,
    true,
    true,
    true,
    false
  };


/* IEEE quad precision format.  */

static void encode_ieee_quad (const struct real_format *fmt,
			      long *, const REAL_VALUE_TYPE *);
static void decode_ieee_quad (const struct real_format *,
			      REAL_VALUE_TYPE *, const long *);

static void
encode_ieee_quad (const struct real_format *fmt, long *buf,
		  const REAL_VALUE_TYPE *r)
{
  unsigned long image3, image2, image1, image0, exp;
  bool denormal = (r->sig[SIGSZ-1] & SIG_MSB) == 0;
  REAL_VALUE_TYPE u;

  image3 = r->sign << 31;
  image2 = 0;
  image1 = 0;
  image0 = 0;

  rshift_significand (&u, r, SIGNIFICAND_BITS - 113);

  switch (r->cl)
    {
    case rvc_zero:
      break;

    case rvc_inf:
      if (fmt->has_inf)
	image3 |= 32767 << 16;
      else
	{
	  image3 |= 0x7fffffff;
	  image2 = 0xffffffff;
	  image1 = 0xffffffff;
	  image0 = 0xffffffff;
	}
      break;

    case rvc_nan:
      if (fmt->has_nans)
	{
	  image3 |= 32767 << 16;

	  if (r->canonical)
	    {
	      /* Don't use bits from the significand.  The
		 initialization above is right.  */
	    }
	  else if (HOST_BITS_PER_LONG == 32)
	    {
	      image0 = u.sig[0];
	      image1 = u.sig[1];
	      image2 = u.sig[2];
	      image3 |= u.sig[3] & 0xffff;
	    }
	  else
	    {
	      image0 = u.sig[0];
	      image1 = image0 >> 31 >> 1;
	      image2 = u.sig[1];
	      image3 |= (image2 >> 31 >> 1) & 0xffff;
	      image0 &= 0xffffffff;
	      image2 &= 0xffffffff;
	    }
	  if (r->signalling == fmt->qnan_msb_set)
	    image3 &= ~0x8000;
	  else
	    image3 |= 0x8000;
	  /* We overload qnan_msb_set here: it's only clear for
	     mips_ieee_single, which wants all mantissa bits but the
	     quiet/signalling one set in canonical NaNs (at least
	     Quiet ones).  */
	  if (r->canonical && !fmt->qnan_msb_set)
	    {
	      image3 |= 0x7fff;
	      image2 = image1 = image0 = 0xffffffff;
	    }
	  else if (((image3 & 0xffff) | image2 | image1 | image0) == 0)
	    image3 |= 0x4000;
	}
      else
	{
	  image3 |= 0x7fffffff;
	  image2 = 0xffffffff;
	  image1 = 0xffffffff;
	  image0 = 0xffffffff;
	}
      break;

    case rvc_normal:
      /* Recall that IEEE numbers are interpreted as 1.F x 2**exp,
	 whereas the intermediate representation is 0.F x 2**exp.
	 Which means we're off by one.  */
      if (denormal)
	exp = 0;
      else
	exp = REAL_EXP (r) + 16383 - 1;
      image3 |= exp << 16;

      if (HOST_BITS_PER_LONG == 32)
	{
	  image0 = u.sig[0];
	  image1 = u.sig[1];
	  image2 = u.sig[2];
	  image3 |= u.sig[3] & 0xffff;
	}
      else
	{
	  image0 = u.sig[0];
	  image1 = image0 >> 31 >> 1;
	  image2 = u.sig[1];
	  image3 |= (image2 >> 31 >> 1) & 0xffff;
	  image0 &= 0xffffffff;
	  image2 &= 0xffffffff;
	}
      break;

    default:
      gcc_unreachable ();
    }

  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      buf[0] = image3;
      buf[1] = image2;
      buf[2] = image1;
      buf[3] = image0;
    }
  else
    {
      buf[0] = image0;
      buf[1] = image1;
      buf[2] = image2;
      buf[3] = image3;
    }
}

static void
decode_ieee_quad (const struct real_format *fmt, REAL_VALUE_TYPE *r,
		  const long *buf)
{
  unsigned long image3, image2, image1, image0;
  bool sign;
  int exp;

  if (FLOAT_WORDS_BIG_ENDIAN)
    {
      image3 = buf[0];
      image2 = buf[1];
      image1 = buf[2];
      image0 = buf[3];
    }
  else
    {
      image0 = buf[0];
      image1 = buf[1];
      image2 = buf[2];
      image3 = buf[3];
    }
  image0 &= 0xffffffff;
  image1 &= 0xffffffff;
  image2 &= 0xffffffff;

  sign = (image3 >> 31) & 1;
  exp = (image3 >> 16) & 0x7fff;
  image3 &= 0xffff;

  memset (r, 0, sizeof (*r));

  if (exp == 0)
    {
      if ((image3 | image2 | image1 | image0) && fmt->has_denorm)
	{
	  r->cl = rvc_normal;
	  r->sign = sign;

	  SET_REAL_EXP (r, -16382 + (SIGNIFICAND_BITS - 112));
	  if (HOST_BITS_PER_LONG == 32)
	    {
	      r->sig[0] = image0;
	      r->sig[1] = image1;
	      r->sig[2] = image2;
	      r->sig[3] = image3;
	    }
	  else
	    {
	      r->sig[0] = (image1 << 31 << 1) | image0;
	      r->sig[1] = (image3 << 31 << 1) | image2;
	    }

	  normalize (r);
	}
      else if (fmt->has_signed_zero)
	r->sign = sign;
    }
  else if (exp == 32767 && (fmt->has_nans || fmt->has_inf))
    {
      if (image3 | image2 | image1 | image0)
	{
	  r->cl = rvc_nan;
	  r->sign = sign;
	  r->signalling = ((image3 >> 15) & 1) ^ fmt->qnan_msb_set;

	  if (HOST_BITS_PER_LONG == 32)
	    {
	      r->sig[0] = image0;
	      r->sig[1] = image1;
	      r->sig[2] = image2;
	      r->sig[3] = image3;
	    }
	  else
	    {
	      r->sig[0] = (image1 << 31 << 1) | image0;
	      r->sig[1] = (image3 << 31 << 1) | image2;
	    }
	  lshift_significand (r, r, SIGNIFICAND_BITS - 113);
	}
      else
	{
	  r->cl = rvc_inf;
	  r->sign = sign;
	}
    }
  else
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, exp - 16383 + 1);

      if (HOST_BITS_PER_LONG == 32)
	{
	  r->sig[0] = image0;
	  r->sig[1] = image1;
	  r->sig[2] = image2;
	  r->sig[3] = image3;
	}
      else
	{
	  r->sig[0] = (image1 << 31 << 1) | image0;
	  r->sig[1] = (image3 << 31 << 1) | image2;
	}
      lshift_significand (r, r, SIGNIFICAND_BITS - 113);
      r->sig[SIGSZ-1] |= SIG_MSB;
    }
}

const struct real_format ieee_quad_format =
  {
    encode_ieee_quad,
    decode_ieee_quad,
    2,
    1,
    113,
    113,
    -16381,
    16384,
    127,
    127,
    true,
    true,
    true,
    true,
    true
  };

const struct real_format mips_quad_format =
  {
    encode_ieee_quad,
    decode_ieee_quad,
    2,
    1,
    113,
    113,
    -16381,
    16384,
    127,
    127,
    true,
    true,
    true,
    true,
    false
  };

/* Descriptions of VAX floating point formats can be found beginning at

   http://h71000.www7.hp.com/doc/73FINAL/4515/4515pro_013.html#f_floating_point_format

   The thing to remember is that they're almost IEEE, except for word
   order, exponent bias, and the lack of infinities, nans, and denormals.

   We don't implement the H_floating format here, simply because neither
   the VAX or Alpha ports use it.  */

static void encode_vax_f (const struct real_format *fmt,
			  long *, const REAL_VALUE_TYPE *);
static void decode_vax_f (const struct real_format *,
			  REAL_VALUE_TYPE *, const long *);
static void encode_vax_d (const struct real_format *fmt,
			  long *, const REAL_VALUE_TYPE *);
static void decode_vax_d (const struct real_format *,
			  REAL_VALUE_TYPE *, const long *);
static void encode_vax_g (const struct real_format *fmt,
			  long *, const REAL_VALUE_TYPE *);
static void decode_vax_g (const struct real_format *,
			  REAL_VALUE_TYPE *, const long *);

static void
encode_vax_f (const struct real_format *fmt ATTRIBUTE_UNUSED, long *buf,
	      const REAL_VALUE_TYPE *r)
{
  unsigned long sign, exp, sig, image;

  sign = r->sign << 15;

  switch (r->cl)
    {
    case rvc_zero:
      image = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      image = 0xffff7fff | sign;
      break;

    case rvc_normal:
      sig = (r->sig[SIGSZ-1] >> (HOST_BITS_PER_LONG - 24)) & 0x7fffff;
      exp = REAL_EXP (r) + 128;

      image = (sig << 16) & 0xffff0000;
      image |= sign;
      image |= exp << 7;
      image |= sig >> 16;
      break;

    default:
      gcc_unreachable ();
    }

  buf[0] = image;
}

static void
decode_vax_f (const struct real_format *fmt ATTRIBUTE_UNUSED,
	      REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long image = buf[0] & 0xffffffff;
  int exp = (image >> 7) & 0xff;

  memset (r, 0, sizeof (*r));

  if (exp != 0)
    {
      r->cl = rvc_normal;
      r->sign = (image >> 15) & 1;
      SET_REAL_EXP (r, exp - 128);

      image = ((image & 0x7f) << 16) | ((image >> 16) & 0xffff);
      r->sig[SIGSZ-1] = (image << (HOST_BITS_PER_LONG - 24)) | SIG_MSB;
    }
}

static void
encode_vax_d (const struct real_format *fmt ATTRIBUTE_UNUSED, long *buf,
	      const REAL_VALUE_TYPE *r)
{
  unsigned long image0, image1, sign = r->sign << 15;

  switch (r->cl)
    {
    case rvc_zero:
      image0 = image1 = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      image0 = 0xffff7fff | sign;
      image1 = 0xffffffff;
      break;

    case rvc_normal:
      /* Extract the significand into straight hi:lo.  */
      if (HOST_BITS_PER_LONG == 64)
	{
	  image0 = r->sig[SIGSZ-1];
	  image1 = (image0 >> (64 - 56)) & 0xffffffff;
	  image0 = (image0 >> (64 - 56 + 1) >> 31) & 0x7fffff;
	}
      else
	{
	  image0 = r->sig[SIGSZ-1];
	  image1 = r->sig[SIGSZ-2];
	  image1 = (image0 << 24) | (image1 >> 8);
	  image0 = (image0 >> 8) & 0xffffff;
	}

      /* Rearrange the half-words of the significand to match the
	 external format.  */
      image0 = ((image0 << 16) | (image0 >> 16)) & 0xffff007f;
      image1 = ((image1 << 16) | (image1 >> 16)) & 0xffffffff;

      /* Add the sign and exponent.  */
      image0 |= sign;
      image0 |= (REAL_EXP (r) + 128) << 7;
      break;

    default:
      gcc_unreachable ();
    }

  if (FLOAT_WORDS_BIG_ENDIAN)
    buf[0] = image1, buf[1] = image0;
  else
    buf[0] = image0, buf[1] = image1;
}

static void
decode_vax_d (const struct real_format *fmt ATTRIBUTE_UNUSED,
	      REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long image0, image1;
  int exp;

  if (FLOAT_WORDS_BIG_ENDIAN)
    image1 = buf[0], image0 = buf[1];
  else
    image0 = buf[0], image1 = buf[1];
  image0 &= 0xffffffff;
  image1 &= 0xffffffff;

  exp = (image0 >> 7) & 0xff;

  memset (r, 0, sizeof (*r));

  if (exp != 0)
    {
      r->cl = rvc_normal;
      r->sign = (image0 >> 15) & 1;
      SET_REAL_EXP (r, exp - 128);

      /* Rearrange the half-words of the external format into
	 proper ascending order.  */
      image0 = ((image0 & 0x7f) << 16) | ((image0 >> 16) & 0xffff);
      image1 = ((image1 & 0xffff) << 16) | ((image1 >> 16) & 0xffff);

      if (HOST_BITS_PER_LONG == 64)
	{
	  image0 = (image0 << 31 << 1) | image1;
	  image0 <<= 64 - 56;
	  image0 |= SIG_MSB;
	  r->sig[SIGSZ-1] = image0;
	}
      else
	{
	  r->sig[SIGSZ-1] = image0;
	  r->sig[SIGSZ-2] = image1;
	  lshift_significand (r, r, 2*HOST_BITS_PER_LONG - 56);
	  r->sig[SIGSZ-1] |= SIG_MSB;
	}
    }
}

static void
encode_vax_g (const struct real_format *fmt ATTRIBUTE_UNUSED, long *buf,
	      const REAL_VALUE_TYPE *r)
{
  unsigned long image0, image1, sign = r->sign << 15;

  switch (r->cl)
    {
    case rvc_zero:
      image0 = image1 = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      image0 = 0xffff7fff | sign;
      image1 = 0xffffffff;
      break;

    case rvc_normal:
      /* Extract the significand into straight hi:lo.  */
      if (HOST_BITS_PER_LONG == 64)
	{
	  image0 = r->sig[SIGSZ-1];
	  image1 = (image0 >> (64 - 53)) & 0xffffffff;
	  image0 = (image0 >> (64 - 53 + 1) >> 31) & 0xfffff;
	}
      else
	{
	  image0 = r->sig[SIGSZ-1];
	  image1 = r->sig[SIGSZ-2];
	  image1 = (image0 << 21) | (image1 >> 11);
	  image0 = (image0 >> 11) & 0xfffff;
	}

      /* Rearrange the half-words of the significand to match the
	 external format.  */
      image0 = ((image0 << 16) | (image0 >> 16)) & 0xffff000f;
      image1 = ((image1 << 16) | (image1 >> 16)) & 0xffffffff;

      /* Add the sign and exponent.  */
      image0 |= sign;
      image0 |= (REAL_EXP (r) + 1024) << 4;
      break;

    default:
      gcc_unreachable ();
    }

  if (FLOAT_WORDS_BIG_ENDIAN)
    buf[0] = image1, buf[1] = image0;
  else
    buf[0] = image0, buf[1] = image1;
}

static void
decode_vax_g (const struct real_format *fmt ATTRIBUTE_UNUSED,
	      REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long image0, image1;
  int exp;

  if (FLOAT_WORDS_BIG_ENDIAN)
    image1 = buf[0], image0 = buf[1];
  else
    image0 = buf[0], image1 = buf[1];
  image0 &= 0xffffffff;
  image1 &= 0xffffffff;

  exp = (image0 >> 4) & 0x7ff;

  memset (r, 0, sizeof (*r));

  if (exp != 0)
    {
      r->cl = rvc_normal;
      r->sign = (image0 >> 15) & 1;
      SET_REAL_EXP (r, exp - 1024);

      /* Rearrange the half-words of the external format into
	 proper ascending order.  */
      image0 = ((image0 & 0xf) << 16) | ((image0 >> 16) & 0xffff);
      image1 = ((image1 & 0xffff) << 16) | ((image1 >> 16) & 0xffff);

      if (HOST_BITS_PER_LONG == 64)
	{
	  image0 = (image0 << 31 << 1) | image1;
	  image0 <<= 64 - 53;
	  image0 |= SIG_MSB;
	  r->sig[SIGSZ-1] = image0;
	}
      else
	{
	  r->sig[SIGSZ-1] = image0;
	  r->sig[SIGSZ-2] = image1;
	  lshift_significand (r, r, 64 - 53);
	  r->sig[SIGSZ-1] |= SIG_MSB;
	}
    }
}

const struct real_format vax_f_format =
  {
    encode_vax_f,
    decode_vax_f,
    2,
    1,
    24,
    24,
    -127,
    127,
    15,
    15,
    false,
    false,
    false,
    false,
    false
  };

const struct real_format vax_d_format =
  {
    encode_vax_d,
    decode_vax_d,
    2,
    1,
    56,
    56,
    -127,
    127,
    15,
    15,
    false,
    false,
    false,
    false,
    false
  };

const struct real_format vax_g_format =
  {
    encode_vax_g,
    decode_vax_g,
    2,
    1,
    53,
    53,
    -1023,
    1023,
    15,
    15,
    false,
    false,
    false,
    false,
    false
  };

/* A good reference for these can be found in chapter 9 of
   "ESA/390 Principles of Operation", IBM document number SA22-7201-01.
   An on-line version can be found here:

   http://publibz.boulder.ibm.com/cgi-bin/bookmgr_OS390/BOOKS/DZ9AR001/9.1?DT=19930923083613
*/

static void encode_i370_single (const struct real_format *fmt,
				long *, const REAL_VALUE_TYPE *);
static void decode_i370_single (const struct real_format *,
				REAL_VALUE_TYPE *, const long *);
static void encode_i370_double (const struct real_format *fmt,
				long *, const REAL_VALUE_TYPE *);
static void decode_i370_double (const struct real_format *,
				REAL_VALUE_TYPE *, const long *);

static void
encode_i370_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
		    long *buf, const REAL_VALUE_TYPE *r)
{
  unsigned long sign, exp, sig, image;

  sign = r->sign << 31;

  switch (r->cl)
    {
    case rvc_zero:
      image = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      image = 0x7fffffff | sign;
      break;

    case rvc_normal:
      sig = (r->sig[SIGSZ-1] >> (HOST_BITS_PER_LONG - 24)) & 0xffffff;
      exp = ((REAL_EXP (r) / 4) + 64) << 24;
      image = sign | exp | sig;
      break;

    default:
      gcc_unreachable ();
    }

  buf[0] = image;
}

static void
decode_i370_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
		    REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long sign, sig, image = buf[0];
  int exp;

  sign = (image >> 31) & 1;
  exp = (image >> 24) & 0x7f;
  sig = image & 0xffffff;

  memset (r, 0, sizeof (*r));

  if (exp || sig)
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, (exp - 64) * 4);
      r->sig[SIGSZ-1] = sig << (HOST_BITS_PER_LONG - 24);
      normalize (r);
    }
}

static void
encode_i370_double (const struct real_format *fmt ATTRIBUTE_UNUSED,
		    long *buf, const REAL_VALUE_TYPE *r)
{
  unsigned long sign, exp, image_hi, image_lo;

  sign = r->sign << 31;

  switch (r->cl)
    {
    case rvc_zero:
      image_hi = image_lo = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      image_hi = 0x7fffffff | sign;
      image_lo = 0xffffffff;
      break;

    case rvc_normal:
      if (HOST_BITS_PER_LONG == 64)
	{
	  image_hi = r->sig[SIGSZ-1];
	  image_lo = (image_hi >> (64 - 56)) & 0xffffffff;
	  image_hi = (image_hi >> (64 - 56 + 1) >> 31) & 0xffffff;
	}
      else
	{
	  image_hi = r->sig[SIGSZ-1];
	  image_lo = r->sig[SIGSZ-2];
	  image_lo = (image_lo >> 8) | (image_hi << 24);
	  image_hi >>= 8;
	}

      exp = ((REAL_EXP (r) / 4) + 64) << 24;
      image_hi |= sign | exp;
      break;

    default:
      gcc_unreachable ();
    }

  if (FLOAT_WORDS_BIG_ENDIAN)
    buf[0] = image_hi, buf[1] = image_lo;
  else
    buf[0] = image_lo, buf[1] = image_hi;
}

static void
decode_i370_double (const struct real_format *fmt ATTRIBUTE_UNUSED,
		    REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long sign, image_hi, image_lo;
  int exp;

  if (FLOAT_WORDS_BIG_ENDIAN)
    image_hi = buf[0], image_lo = buf[1];
  else
    image_lo = buf[0], image_hi = buf[1];

  sign = (image_hi >> 31) & 1;
  exp = (image_hi >> 24) & 0x7f;
  image_hi &= 0xffffff;
  image_lo &= 0xffffffff;

  memset (r, 0, sizeof (*r));

  if (exp || image_hi || image_lo)
    {
      r->cl = rvc_normal;
      r->sign = sign;
      SET_REAL_EXP (r, (exp - 64) * 4 + (SIGNIFICAND_BITS - 56));

      if (HOST_BITS_PER_LONG == 32)
	{
	  r->sig[0] = image_lo;
	  r->sig[1] = image_hi;
	}
      else
	r->sig[0] = image_lo | (image_hi << 31 << 1);

      normalize (r);
    }
}

const struct real_format i370_single_format =
  {
    encode_i370_single,
    decode_i370_single,
    16,
    4,
    6,
    6,
    -64,
    63,
    31,
    31,
    false,
    false,
    false, /* ??? The encoding does allow for "unnormals".  */
    false, /* ??? The encoding does allow for "unnormals".  */
    false
  };

const struct real_format i370_double_format =
  {
    encode_i370_double,
    decode_i370_double,
    16,
    4,
    14,
    14,
    -64,
    63,
    63,
    63,
    false,
    false,
    false, /* ??? The encoding does allow for "unnormals".  */
    false, /* ??? The encoding does allow for "unnormals".  */
    false
  };

/* Encode real R into a single precision DFP value in BUF.  */
static void
encode_decimal_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
                       long *buf ATTRIBUTE_UNUSED, 
		       const REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED)
{
  encode_decimal32 (fmt, buf, r);
}

/* Decode a single precision DFP value in BUF into a real R.  */
static void 
decode_decimal_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
		       REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED, 
		       const long *buf ATTRIBUTE_UNUSED)
{
  decode_decimal32 (fmt, r, buf);
}

/* Encode real R into a double precision DFP value in BUF.  */
static void 
encode_decimal_double (const struct real_format *fmt ATTRIBUTE_UNUSED,
		       long *buf ATTRIBUTE_UNUSED, 
		       const REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED)
{
  encode_decimal64 (fmt, buf, r);
}

/* Decode a double precision DFP value in BUF into a real R.  */
static void 
decode_decimal_double (const struct real_format *fmt ATTRIBUTE_UNUSED,
		       REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED, 
		       const long *buf ATTRIBUTE_UNUSED)
{
  decode_decimal64 (fmt, r, buf);
}

/* Encode real R into a quad precision DFP value in BUF.  */
static void 
encode_decimal_quad (const struct real_format *fmt ATTRIBUTE_UNUSED,
		     long *buf ATTRIBUTE_UNUSED,
		     const REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED)
{
  encode_decimal128 (fmt, buf, r);
}

/* Decode a quad precision DFP value in BUF into a real R.  */
static void 
decode_decimal_quad (const struct real_format *fmt ATTRIBUTE_UNUSED,
		     REAL_VALUE_TYPE *r ATTRIBUTE_UNUSED,
		     const long *buf ATTRIBUTE_UNUSED)
{
  decode_decimal128 (fmt, r, buf);
}

/* Single precision decimal floating point (IEEE 754R). */
const struct real_format decimal_single_format =
  {
    encode_decimal_single,
    decode_decimal_single,
    10, 
    1,  /* log10 */
    7,
    7,
    -95,
    96,
    31,
    31,
    true,
    true,
    true,
    true, 
    true
  };

/* Double precision decimal floating point (IEEE 754R). */
const struct real_format decimal_double_format =
  {
    encode_decimal_double,
    decode_decimal_double,
    10,
    1,  /* log10 */
    16,
    16,
    -383,
    384,
    63,
    63,
    true,
    true,
    true,
    true,
    true
  };

/* Quad precision decimal floating point (IEEE 754R). */
const struct real_format decimal_quad_format =
  {
    encode_decimal_quad,
    decode_decimal_quad,
    10,
    1,  /* log10 */
    34,
    34,
    -6143,
    6144,
    127,
    127,
    true,
    true,
    true, 
    true, 
    true
  };

/* The "twos-complement" c4x format is officially defined as

	x = s(~s).f * 2**e

   This is rather misleading.  One must remember that F is signed.
   A better description would be

	x = -1**s * ((s + 1 + .f) * 2**e

   So if we have a (4 bit) fraction of .1000 with a sign bit of 1,
   that's -1 * (1+1+(-.5)) == -1.5.  I think.

   The constructions here are taken from Tables 5-1 and 5-2 of the
   TMS320C4x User's Guide wherein step-by-step instructions for
   conversion from IEEE are presented.  That's close enough to our
   internal representation so as to make things easy.

   See http://www-s.ti.com/sc/psheets/spru063c/spru063c.pdf  */

static void encode_c4x_single (const struct real_format *fmt,
			       long *, const REAL_VALUE_TYPE *);
static void decode_c4x_single (const struct real_format *,
			       REAL_VALUE_TYPE *, const long *);
static void encode_c4x_extended (const struct real_format *fmt,
				 long *, const REAL_VALUE_TYPE *);
static void decode_c4x_extended (const struct real_format *,
				 REAL_VALUE_TYPE *, const long *);

static void
encode_c4x_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
		   long *buf, const REAL_VALUE_TYPE *r)
{
  unsigned long image, exp, sig;

  switch (r->cl)
    {
    case rvc_zero:
      exp = -128;
      sig = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      exp = 127;
      sig = 0x800000 - r->sign;
      break;

    case rvc_normal:
      exp = REAL_EXP (r) - 1;
      sig = (r->sig[SIGSZ-1] >> (HOST_BITS_PER_LONG - 24)) & 0x7fffff;
      if (r->sign)
	{
	  if (sig)
	    sig = -sig;
	  else
	    exp--;
	  sig |= 0x800000;
	}
      break;

    default:
      gcc_unreachable ();
    }

  image = ((exp & 0xff) << 24) | (sig & 0xffffff);
  buf[0] = image;
}

static void
decode_c4x_single (const struct real_format *fmt ATTRIBUTE_UNUSED,
		   REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long image = buf[0];
  unsigned long sig;
  int exp, sf;

  exp = (((image >> 24) & 0xff) ^ 0x80) - 0x80;
  sf = ((image & 0xffffff) ^ 0x800000) - 0x800000;

  memset (r, 0, sizeof (*r));

  if (exp != -128)
    {
      r->cl = rvc_normal;

      sig = sf & 0x7fffff;
      if (sf < 0)
	{
	  r->sign = 1;
	  if (sig)
	    sig = -sig;
	  else
	    exp++;
	}
      sig = (sig << (HOST_BITS_PER_LONG - 24)) | SIG_MSB;

      SET_REAL_EXP (r, exp + 1);
      r->sig[SIGSZ-1] = sig;
    }
}

static void
encode_c4x_extended (const struct real_format *fmt ATTRIBUTE_UNUSED,
		     long *buf, const REAL_VALUE_TYPE *r)
{
  unsigned long exp, sig;

  switch (r->cl)
    {
    case rvc_zero:
      exp = -128;
      sig = 0;
      break;

    case rvc_inf:
    case rvc_nan:
      exp = 127;
      sig = 0x80000000 - r->sign;
      break;

    case rvc_normal:
      exp = REAL_EXP (r) - 1;

      sig = r->sig[SIGSZ-1];
      if (HOST_BITS_PER_LONG == 64)
	sig = sig >> 1 >> 31;
      sig &= 0x7fffffff;

      if (r->sign)
	{
	  if (sig)
	    sig = -sig;
	  else
	    exp--;
	  sig |= 0x80000000;
	}
      break;

    default:
      gcc_unreachable ();
    }

  exp = (exp & 0xff) << 24;
  sig &= 0xffffffff;

  if (FLOAT_WORDS_BIG_ENDIAN)
    buf[0] = exp, buf[1] = sig;
  else
    buf[0] = sig, buf[0] = exp;
}

static void
decode_c4x_extended (const struct real_format *fmt ATTRIBUTE_UNUSED,
		     REAL_VALUE_TYPE *r, const long *buf)
{
  unsigned long sig;
  int exp, sf;

  if (FLOAT_WORDS_BIG_ENDIAN)
    exp = buf[0], sf = buf[1];
  else
    sf = buf[0], exp = buf[1];

  exp = (((exp >> 24) & 0xff) & 0x80) - 0x80;
  sf = ((sf & 0xffffffff) ^ 0x80000000) - 0x80000000;

  memset (r, 0, sizeof (*r));

  if (exp != -128)
    {
      r->cl = rvc_normal;

      sig = sf & 0x7fffffff;
      if (sf < 0)
	{
	  r->sign = 1;
	  if (sig)
	    sig = -sig;
	  else
	    exp++;
	}
      if (HOST_BITS_PER_LONG == 64)
	sig = sig << 1 << 31;
      sig |= SIG_MSB;

      SET_REAL_EXP (r, exp + 1);
      r->sig[SIGSZ-1] = sig;
    }
}

const struct real_format c4x_single_format =
  {
    encode_c4x_single,
    decode_c4x_single,
    2,
    1,
    24,
    24,
    -126,
    128,
    23,
    -1,
    false,
    false,
    false,
    false,
    false
  };

const struct real_format c4x_extended_format =
  {
    encode_c4x_extended,
    decode_c4x_extended,
    2,
    1,
    32,
    32,
    -126,
    128,
    31,
    -1,
    false,
    false,
    false,
    false,
    false
  };


/* A synthetic "format" for internal arithmetic.  It's the size of the
   internal significand minus the two bits needed for proper rounding.
   The encode and decode routines exist only to satisfy our paranoia
   harness.  */

static void encode_internal (const struct real_format *fmt,
			     long *, const REAL_VALUE_TYPE *);
static void decode_internal (const struct real_format *,
			     REAL_VALUE_TYPE *, const long *);

static void
encode_internal (const struct real_format *fmt ATTRIBUTE_UNUSED, long *buf,
		 const REAL_VALUE_TYPE *r)
{
  memcpy (buf, r, sizeof (*r));
}

static void
decode_internal (const struct real_format *fmt ATTRIBUTE_UNUSED,
		 REAL_VALUE_TYPE *r, const long *buf)
{
  memcpy (r, buf, sizeof (*r));
}

const struct real_format real_internal_format =
  {
    encode_internal,
    decode_internal,
    2,
    1,
    SIGNIFICAND_BITS - 2,
    SIGNIFICAND_BITS - 2,
    -MAX_EXP,
    MAX_EXP,
    -1,
    -1,
    true,
    true,
    false,
    true,
    true
  };

/* Calculate the square root of X in mode MODE, and store the result
   in R.  Return TRUE if the operation does not raise an exception.
   For details see "High Precision Division and Square Root",
   Alan H. Karp and Peter Markstein, HP Lab Report 93-93-42, June
   1993.  http://www.hpl.hp.com/techreports/93/HPL-93-42.pdf.  */

bool
real_sqrt (REAL_VALUE_TYPE *r, enum machine_mode mode,
	   const REAL_VALUE_TYPE *x)
{
  static REAL_VALUE_TYPE halfthree;
  static bool init = false;
  REAL_VALUE_TYPE h, t, i;
  int iter, exp;

  /* sqrt(-0.0) is -0.0.  */
  if (real_isnegzero (x))
    {
      *r = *x;
      return false;
    }

  /* Negative arguments return NaN.  */
  if (real_isneg (x))
    {
      get_canonical_qnan (r, 0);
      return false;
    }

  /* Infinity and NaN return themselves.  */
  if (real_isinf (x) || real_isnan (x))
    {
      *r = *x;
      return false;
    }

  if (!init)
    {
      do_add (&halfthree, &dconst1, &dconsthalf, 0);
      init = true;
    }

  /* Initial guess for reciprocal sqrt, i.  */
  exp = real_exponent (x);
  real_ldexp (&i, &dconst1, -exp/2);

  /* Newton's iteration for reciprocal sqrt, i.  */
  for (iter = 0; iter < 16; iter++)
    {
      /* i(n+1) = i(n) * (1.5 - 0.5*i(n)*i(n)*x).  */
      do_multiply (&t, x, &i);
      do_multiply (&h, &t, &i);
      do_multiply (&t, &h, &dconsthalf);
      do_add (&h, &halfthree, &t, 1);
      do_multiply (&t, &i, &h);

      /* Check for early convergence.  */
      if (iter >= 6 && real_identical (&i, &t))
	break;

      /* ??? Unroll loop to avoid copying.  */
      i = t;
    }

  /* Final iteration: r = i*x + 0.5*i*x*(1.0 - i*(i*x)).  */
  do_multiply (&t, x, &i);
  do_multiply (&h, &t, &i);
  do_add (&i, &dconst1, &h, 1);
  do_multiply (&h, &t, &i);
  do_multiply (&i, &dconsthalf, &h);
  do_add (&h, &t, &i, 0);

  /* ??? We need a Tuckerman test to get the last bit.  */

  real_convert (r, mode, &h);
  return true;
}

/* Calculate X raised to the integer exponent N in mode MODE and store
   the result in R.  Return true if the result may be inexact due to
   loss of precision.  The algorithm is the classic "left-to-right binary
   method" described in section 4.6.3 of Donald Knuth's "Seminumerical
   Algorithms", "The Art of Computer Programming", Volume 2.  */

bool
real_powi (REAL_VALUE_TYPE *r, enum machine_mode mode,
	   const REAL_VALUE_TYPE *x, HOST_WIDE_INT n)
{
  unsigned HOST_WIDE_INT bit;
  REAL_VALUE_TYPE t;
  bool inexact = false;
  bool init = false;
  bool neg;
  int i;

  if (n == 0)
    {
      *r = dconst1;
      return false;
    }
  else if (n < 0)
    {
      /* Don't worry about overflow, from now on n is unsigned.  */
      neg = true;
      n = -n;
    }
  else
    neg = false;

  t = *x;
  bit = (unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT - 1);
  for (i = 0; i < HOST_BITS_PER_WIDE_INT; i++)
    {
      if (init)
	{
	  inexact |= do_multiply (&t, &t, &t);
	  if (n & bit)
	    inexact |= do_multiply (&t, &t, x);
	}
      else if (n & bit)
	init = true;
      bit >>= 1;
    }

  if (neg)
    inexact |= do_divide (&t, &dconst1, &t);

  real_convert (r, mode, &t);
  return inexact;
}

/* Round X to the nearest integer not larger in absolute value, i.e.
   towards zero, placing the result in R in mode MODE.  */

void
real_trunc (REAL_VALUE_TYPE *r, enum machine_mode mode,
	    const REAL_VALUE_TYPE *x)
{
  do_fix_trunc (r, x);
  if (mode != VOIDmode)
    real_convert (r, mode, r);
}

/* Round X to the largest integer not greater in value, i.e. round
   down, placing the result in R in mode MODE.  */

void
real_floor (REAL_VALUE_TYPE *r, enum machine_mode mode,
	    const REAL_VALUE_TYPE *x)
{
  REAL_VALUE_TYPE t;

  do_fix_trunc (&t, x);
  if (! real_identical (&t, x) && x->sign)
    do_add (&t, &t, &dconstm1, 0);
  if (mode != VOIDmode)
    real_convert (r, mode, &t);
  else
    *r = t;
}

/* Round X to the smallest integer not less then argument, i.e. round
   up, placing the result in R in mode MODE.  */

void
real_ceil (REAL_VALUE_TYPE *r, enum machine_mode mode,
	   const REAL_VALUE_TYPE *x)
{
  REAL_VALUE_TYPE t;

  do_fix_trunc (&t, x);
  if (! real_identical (&t, x) && ! x->sign)
    do_add (&t, &t, &dconst1, 0);
  if (mode != VOIDmode)
    real_convert (r, mode, &t);
  else
    *r = t;
}

/* Round X to the nearest integer, but round halfway cases away from
   zero.  */

void
real_round (REAL_VALUE_TYPE *r, enum machine_mode mode,
	    const REAL_VALUE_TYPE *x)
{
  do_add (r, x, &dconsthalf, x->sign);
  do_fix_trunc (r, r);
  if (mode != VOIDmode)
    real_convert (r, mode, r);
}

/* Set the sign of R to the sign of X.  */

void
real_copysign (REAL_VALUE_TYPE *r, const REAL_VALUE_TYPE *x)
{
  r->sign = x->sign;
}

