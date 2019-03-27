/* Fold a constant sub-tree into a single node for C-compiler
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

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

/*@@ This file should be rewritten to use an arbitrary precision
  @@ representation for "struct tree_int_cst" and "struct tree_real_cst".
  @@ Perhaps the routines could also be used for bc/dc, and made a lib.
  @@ The routines that translate from the ap rep should
  @@ warn if precision et. al. is lost.
  @@ This would also make life easier when this technology is used
  @@ for cross-compilers.  */

/* The entry points in this file are fold, size_int_wide, size_binop
   and force_fit_type.

   fold takes a tree as argument and returns a simplified tree.

   size_binop takes a tree code for an arithmetic operation
   and two operands that are trees, and produces a tree for the
   result, assuming the type comes from `sizetype'.

   size_int takes an integer value, and creates a tree constant
   with type from `sizetype'.

   force_fit_type takes a constant, an overflowable flag and prior
   overflow indicators.  It forces the value to fit the type and sets
   TREE_OVERFLOW and TREE_CONSTANT_OVERFLOW as appropriate.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "flags.h"
#include "tree.h"
#include "real.h"
#include "rtl.h"
#include "expr.h"
#include "tm_p.h"
#include "toplev.h"
#include "intl.h"
#include "ggc.h"
#include "hashtab.h"
#include "langhooks.h"
#include "md5.h"

/* Non-zero if we are folding constants inside an initializer; zero
   otherwise.  */
int folding_initializer = 0;

/* The following constants represent a bit based encoding of GCC's
   comparison operators.  This encoding simplifies transformations
   on relational comparison operators, such as AND and OR.  */
enum comparison_code {
  COMPCODE_FALSE = 0,
  COMPCODE_LT = 1,
  COMPCODE_EQ = 2,
  COMPCODE_LE = 3,
  COMPCODE_GT = 4,
  COMPCODE_LTGT = 5,
  COMPCODE_GE = 6,
  COMPCODE_ORD = 7,
  COMPCODE_UNORD = 8,
  COMPCODE_UNLT = 9,
  COMPCODE_UNEQ = 10,
  COMPCODE_UNLE = 11,
  COMPCODE_UNGT = 12,
  COMPCODE_NE = 13,
  COMPCODE_UNGE = 14,
  COMPCODE_TRUE = 15
};

static void encode (HOST_WIDE_INT *, unsigned HOST_WIDE_INT, HOST_WIDE_INT);
static void decode (HOST_WIDE_INT *, unsigned HOST_WIDE_INT *, HOST_WIDE_INT *);
static bool negate_mathfn_p (enum built_in_function);
static bool negate_expr_p (tree);
static tree negate_expr (tree);
static tree split_tree (tree, enum tree_code, tree *, tree *, tree *, int);
static tree associate_trees (tree, tree, enum tree_code, tree);
static tree const_binop (enum tree_code, tree, tree, int);
static enum comparison_code comparison_to_compcode (enum tree_code);
static enum tree_code compcode_to_comparison (enum comparison_code);
static tree combine_comparisons (enum tree_code, enum tree_code,
				 enum tree_code, tree, tree, tree);
static int truth_value_p (enum tree_code);
static int operand_equal_for_comparison_p (tree, tree, tree);
static int twoval_comparison_p (tree, tree *, tree *, int *);
static tree eval_subst (tree, tree, tree, tree, tree);
static tree pedantic_omit_one_operand (tree, tree, tree);
static tree distribute_bit_expr (enum tree_code, tree, tree, tree);
static tree make_bit_field_ref (tree, tree, int, int, int);
static tree optimize_bit_field_compare (enum tree_code, tree, tree, tree);
static tree decode_field_reference (tree, HOST_WIDE_INT *, HOST_WIDE_INT *,
				    enum machine_mode *, int *, int *,
				    tree *, tree *);
static int all_ones_mask_p (tree, int);
static tree sign_bit_p (tree, tree);
static int simple_operand_p (tree);
static tree range_binop (enum tree_code, tree, tree, int, tree, int);
static tree range_predecessor (tree);
static tree range_successor (tree);
static tree make_range (tree, int *, tree *, tree *, bool *);
static tree build_range_check (tree, tree, int, tree, tree);
static int merge_ranges (int *, tree *, tree *, int, tree, tree, int, tree,
			 tree);
static tree fold_range_test (enum tree_code, tree, tree, tree);
static tree fold_cond_expr_with_comparison (tree, tree, tree, tree);
static tree unextend (tree, int, int, tree);
static tree fold_truthop (enum tree_code, tree, tree, tree);
static tree optimize_minmax_comparison (enum tree_code, tree, tree, tree);
static tree extract_muldiv (tree, tree, enum tree_code, tree, bool *);
static tree extract_muldiv_1 (tree, tree, enum tree_code, tree, bool *);
static int multiple_of_p (tree, tree, tree);
static tree fold_binary_op_with_conditional_arg (enum tree_code, tree,
						 tree, tree,
						 tree, tree, int);
static bool fold_real_zero_addition_p (tree, tree, int);
static tree fold_mathfn_compare (enum built_in_function, enum tree_code,
				 tree, tree, tree);
static tree fold_inf_compare (enum tree_code, tree, tree, tree);
static tree fold_div_compare (enum tree_code, tree, tree, tree);
static bool reorder_operands_p (tree, tree);
static tree fold_negate_const (tree, tree);
static tree fold_not_const (tree, tree);
static tree fold_relational_const (enum tree_code, tree, tree, tree);
static int native_encode_expr (tree, unsigned char *, int);
static tree native_interpret_expr (tree, unsigned char *, int);


/* We know that A1 + B1 = SUM1, using 2's complement arithmetic and ignoring
   overflow.  Suppose A, B and SUM have the same respective signs as A1, B1,
   and SUM1.  Then this yields nonzero if overflow occurred during the
   addition.

   Overflow occurs if A and B have the same sign, but A and SUM differ in
   sign.  Use `^' to test whether signs differ, and `< 0' to isolate the
   sign.  */
#define OVERFLOW_SUM_SIGN(a, b, sum) ((~((a) ^ (b)) & ((a) ^ (sum))) < 0)

/* To do constant folding on INTEGER_CST nodes requires two-word arithmetic.
   We do that by representing the two-word integer in 4 words, with only
   HOST_BITS_PER_WIDE_INT / 2 bits stored in each word, as a positive
   number.  The value of the word is LOWPART + HIGHPART * BASE.  */

#define LOWPART(x) \
  ((x) & (((unsigned HOST_WIDE_INT) 1 << (HOST_BITS_PER_WIDE_INT / 2)) - 1))
#define HIGHPART(x) \
  ((unsigned HOST_WIDE_INT) (x) >> HOST_BITS_PER_WIDE_INT / 2)
#define BASE ((unsigned HOST_WIDE_INT) 1 << HOST_BITS_PER_WIDE_INT / 2)

/* Unpack a two-word integer into 4 words.
   LOW and HI are the integer, as two `HOST_WIDE_INT' pieces.
   WORDS points to the array of HOST_WIDE_INTs.  */

static void
encode (HOST_WIDE_INT *words, unsigned HOST_WIDE_INT low, HOST_WIDE_INT hi)
{
  words[0] = LOWPART (low);
  words[1] = HIGHPART (low);
  words[2] = LOWPART (hi);
  words[3] = HIGHPART (hi);
}

/* Pack an array of 4 words into a two-word integer.
   WORDS points to the array of words.
   The integer is stored into *LOW and *HI as two `HOST_WIDE_INT' pieces.  */

static void
decode (HOST_WIDE_INT *words, unsigned HOST_WIDE_INT *low,
	HOST_WIDE_INT *hi)
{
  *low = words[0] + words[1] * BASE;
  *hi = words[2] + words[3] * BASE;
}

/* T is an INT_CST node.  OVERFLOWABLE indicates if we are interested
   in overflow of the value, when >0 we are only interested in signed
   overflow, for <0 we are interested in any overflow.  OVERFLOWED
   indicates whether overflow has already occurred.  CONST_OVERFLOWED
   indicates whether constant overflow has already occurred.  We force
   T's value to be within range of T's type (by setting to 0 or 1 all
   the bits outside the type's range).  We set TREE_OVERFLOWED if,
  	OVERFLOWED is nonzero,
	or OVERFLOWABLE is >0 and signed overflow occurs
	or OVERFLOWABLE is <0 and any overflow occurs
   We set TREE_CONSTANT_OVERFLOWED if,
        CONST_OVERFLOWED is nonzero
	or we set TREE_OVERFLOWED.
  We return either the original T, or a copy.  */

tree
force_fit_type (tree t, int overflowable,
		bool overflowed, bool overflowed_const)
{
  unsigned HOST_WIDE_INT low;
  HOST_WIDE_INT high;
  unsigned int prec;
  int sign_extended_type;

  gcc_assert (TREE_CODE (t) == INTEGER_CST);

  low = TREE_INT_CST_LOW (t);
  high = TREE_INT_CST_HIGH (t);

  if (POINTER_TYPE_P (TREE_TYPE (t))
      || TREE_CODE (TREE_TYPE (t)) == OFFSET_TYPE)
    prec = POINTER_SIZE;
  else
    prec = TYPE_PRECISION (TREE_TYPE (t));
  /* Size types *are* sign extended.  */
  sign_extended_type = (!TYPE_UNSIGNED (TREE_TYPE (t))
			|| (TREE_CODE (TREE_TYPE (t)) == INTEGER_TYPE
			    && TYPE_IS_SIZETYPE (TREE_TYPE (t))));

  /* First clear all bits that are beyond the type's precision.  */

  if (prec >= 2 * HOST_BITS_PER_WIDE_INT)
    ;
  else if (prec > HOST_BITS_PER_WIDE_INT)
    high &= ~((HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT));
  else
    {
      high = 0;
      if (prec < HOST_BITS_PER_WIDE_INT)
	low &= ~((HOST_WIDE_INT) (-1) << prec);
    }

  if (!sign_extended_type)
    /* No sign extension */;
  else if (prec >= 2 * HOST_BITS_PER_WIDE_INT)
    /* Correct width already.  */;
  else if (prec > HOST_BITS_PER_WIDE_INT)
    {
      /* Sign extend top half? */
      if (high & ((unsigned HOST_WIDE_INT)1
		  << (prec - HOST_BITS_PER_WIDE_INT - 1)))
	high |= (HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT);
    }
  else if (prec == HOST_BITS_PER_WIDE_INT)
    {
      if ((HOST_WIDE_INT)low < 0)
	high = -1;
    }
  else
    {
      /* Sign extend bottom half? */
      if (low & ((unsigned HOST_WIDE_INT)1 << (prec - 1)))
	{
	  high = -1;
	  low |= (HOST_WIDE_INT)(-1) << prec;
	}
    }

  /* If the value changed, return a new node.  */
  if (overflowed || overflowed_const
      || low != TREE_INT_CST_LOW (t) || high != TREE_INT_CST_HIGH (t))
    {
      t = build_int_cst_wide (TREE_TYPE (t), low, high);

      if (overflowed
	  || overflowable < 0
	  || (overflowable > 0 && sign_extended_type))
	{
	  t = copy_node (t);
	  TREE_OVERFLOW (t) = 1;
	  TREE_CONSTANT_OVERFLOW (t) = 1;
	}
      else if (overflowed_const)
	{
	  t = copy_node (t);
	  TREE_CONSTANT_OVERFLOW (t) = 1;
	}
    }

  return t;
}

/* Add two doubleword integers with doubleword result.
   Return nonzero if the operation overflows according to UNSIGNED_P.
   Each argument is given as two `HOST_WIDE_INT' pieces.
   One argument is L1 and H1; the other, L2 and H2.
   The value is stored as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

int
add_double_with_sign (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
		      unsigned HOST_WIDE_INT l2, HOST_WIDE_INT h2,
		      unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv,
		      bool unsigned_p)
{
  unsigned HOST_WIDE_INT l;
  HOST_WIDE_INT h;

  l = l1 + l2;
  h = h1 + h2 + (l < l1);

  *lv = l;
  *hv = h;

  if (unsigned_p)
    return (unsigned HOST_WIDE_INT) h < (unsigned HOST_WIDE_INT) h1;
  else
    return OVERFLOW_SUM_SIGN (h1, h2, h);
}

/* Negate a doubleword integer with doubleword result.
   Return nonzero if the operation overflows, assuming it's signed.
   The argument is given as two `HOST_WIDE_INT' pieces in L1 and H1.
   The value is stored as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

int
neg_double (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
	    unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv)
{
  if (l1 == 0)
    {
      *lv = 0;
      *hv = - h1;
      return (*hv & h1) < 0;
    }
  else
    {
      *lv = -l1;
      *hv = ~h1;
      return 0;
    }
}

/* Multiply two doubleword integers with doubleword result.
   Return nonzero if the operation overflows according to UNSIGNED_P.
   Each argument is given as two `HOST_WIDE_INT' pieces.
   One argument is L1 and H1; the other, L2 and H2.
   The value is stored as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

int
mul_double_with_sign (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
		      unsigned HOST_WIDE_INT l2, HOST_WIDE_INT h2,
		      unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv,
		      bool unsigned_p)
{
  HOST_WIDE_INT arg1[4];
  HOST_WIDE_INT arg2[4];
  HOST_WIDE_INT prod[4 * 2];
  unsigned HOST_WIDE_INT carry;
  int i, j, k;
  unsigned HOST_WIDE_INT toplow, neglow;
  HOST_WIDE_INT tophigh, neghigh;

  encode (arg1, l1, h1);
  encode (arg2, l2, h2);

  memset (prod, 0, sizeof prod);

  for (i = 0; i < 4; i++)
    {
      carry = 0;
      for (j = 0; j < 4; j++)
	{
	  k = i + j;
	  /* This product is <= 0xFFFE0001, the sum <= 0xFFFF0000.  */
	  carry += arg1[i] * arg2[j];
	  /* Since prod[p] < 0xFFFF, this sum <= 0xFFFFFFFF.  */
	  carry += prod[k];
	  prod[k] = LOWPART (carry);
	  carry = HIGHPART (carry);
	}
      prod[i + 4] = carry;
    }

  decode (prod, lv, hv);
  decode (prod + 4, &toplow, &tophigh);

  /* Unsigned overflow is immediate.  */
  if (unsigned_p)
    return (toplow | tophigh) != 0;

  /* Check for signed overflow by calculating the signed representation of the
     top half of the result; it should agree with the low half's sign bit.  */
  if (h1 < 0)
    {
      neg_double (l2, h2, &neglow, &neghigh);
      add_double (neglow, neghigh, toplow, tophigh, &toplow, &tophigh);
    }
  if (h2 < 0)
    {
      neg_double (l1, h1, &neglow, &neghigh);
      add_double (neglow, neghigh, toplow, tophigh, &toplow, &tophigh);
    }
  return (*hv < 0 ? ~(toplow & tophigh) : toplow | tophigh) != 0;
}

/* Shift the doubleword integer in L1, H1 left by COUNT places
   keeping only PREC bits of result.
   Shift right if COUNT is negative.
   ARITH nonzero specifies arithmetic shifting; otherwise use logical shift.
   Store the value as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

void
lshift_double (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
	       HOST_WIDE_INT count, unsigned int prec,
	       unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv, int arith)
{
  unsigned HOST_WIDE_INT signmask;

  if (count < 0)
    {
      rshift_double (l1, h1, -count, prec, lv, hv, arith);
      return;
    }

  if (SHIFT_COUNT_TRUNCATED)
    count %= prec;

  if (count >= 2 * HOST_BITS_PER_WIDE_INT)
    {
      /* Shifting by the host word size is undefined according to the
	 ANSI standard, so we must handle this as a special case.  */
      *hv = 0;
      *lv = 0;
    }
  else if (count >= HOST_BITS_PER_WIDE_INT)
    {
      *hv = l1 << (count - HOST_BITS_PER_WIDE_INT);
      *lv = 0;
    }
  else
    {
      *hv = (((unsigned HOST_WIDE_INT) h1 << count)
	     | (l1 >> (HOST_BITS_PER_WIDE_INT - count - 1) >> 1));
      *lv = l1 << count;
    }

  /* Sign extend all bits that are beyond the precision.  */

  signmask = -((prec > HOST_BITS_PER_WIDE_INT
		? ((unsigned HOST_WIDE_INT) *hv
		   >> (prec - HOST_BITS_PER_WIDE_INT - 1))
		: (*lv >> (prec - 1))) & 1);

  if (prec >= 2 * HOST_BITS_PER_WIDE_INT)
    ;
  else if (prec >= HOST_BITS_PER_WIDE_INT)
    {
      *hv &= ~((HOST_WIDE_INT) (-1) << (prec - HOST_BITS_PER_WIDE_INT));
      *hv |= signmask << (prec - HOST_BITS_PER_WIDE_INT);
    }
  else
    {
      *hv = signmask;
      *lv &= ~((unsigned HOST_WIDE_INT) (-1) << prec);
      *lv |= signmask << prec;
    }
}

/* Shift the doubleword integer in L1, H1 right by COUNT places
   keeping only PREC bits of result.  COUNT must be positive.
   ARITH nonzero specifies arithmetic shifting; otherwise use logical shift.
   Store the value as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

void
rshift_double (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
	       HOST_WIDE_INT count, unsigned int prec,
	       unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv,
	       int arith)
{
  unsigned HOST_WIDE_INT signmask;

  signmask = (arith
	      ? -((unsigned HOST_WIDE_INT) h1 >> (HOST_BITS_PER_WIDE_INT - 1))
	      : 0);

  if (SHIFT_COUNT_TRUNCATED)
    count %= prec;

  if (count >= 2 * HOST_BITS_PER_WIDE_INT)
    {
      /* Shifting by the host word size is undefined according to the
	 ANSI standard, so we must handle this as a special case.  */
      *hv = 0;
      *lv = 0;
    }
  else if (count >= HOST_BITS_PER_WIDE_INT)
    {
      *hv = 0;
      *lv = (unsigned HOST_WIDE_INT) h1 >> (count - HOST_BITS_PER_WIDE_INT);
    }
  else
    {
      *hv = (unsigned HOST_WIDE_INT) h1 >> count;
      *lv = ((l1 >> count)
	     | ((unsigned HOST_WIDE_INT) h1 << (HOST_BITS_PER_WIDE_INT - count - 1) << 1));
    }

  /* Zero / sign extend all bits that are beyond the precision.  */

  if (count >= (HOST_WIDE_INT)prec)
    {
      *hv = signmask;
      *lv = signmask;
    }
  else if ((prec - count) >= 2 * HOST_BITS_PER_WIDE_INT)
    ;
  else if ((prec - count) >= HOST_BITS_PER_WIDE_INT)
    {
      *hv &= ~((HOST_WIDE_INT) (-1) << (prec - count - HOST_BITS_PER_WIDE_INT));
      *hv |= signmask << (prec - count - HOST_BITS_PER_WIDE_INT);
    }
  else
    {
      *hv = signmask;
      *lv &= ~((unsigned HOST_WIDE_INT) (-1) << (prec - count));
      *lv |= signmask << (prec - count);
    }
}

/* Rotate the doubleword integer in L1, H1 left by COUNT places
   keeping only PREC bits of result.
   Rotate right if COUNT is negative.
   Store the value as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

void
lrotate_double (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
		HOST_WIDE_INT count, unsigned int prec,
		unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv)
{
  unsigned HOST_WIDE_INT s1l, s2l;
  HOST_WIDE_INT s1h, s2h;

  count %= prec;
  if (count < 0)
    count += prec;

  lshift_double (l1, h1, count, prec, &s1l, &s1h, 0);
  rshift_double (l1, h1, prec - count, prec, &s2l, &s2h, 0);
  *lv = s1l | s2l;
  *hv = s1h | s2h;
}

/* Rotate the doubleword integer in L1, H1 left by COUNT places
   keeping only PREC bits of result.  COUNT must be positive.
   Store the value as two `HOST_WIDE_INT' pieces in *LV and *HV.  */

void
rrotate_double (unsigned HOST_WIDE_INT l1, HOST_WIDE_INT h1,
		HOST_WIDE_INT count, unsigned int prec,
		unsigned HOST_WIDE_INT *lv, HOST_WIDE_INT *hv)
{
  unsigned HOST_WIDE_INT s1l, s2l;
  HOST_WIDE_INT s1h, s2h;

  count %= prec;
  if (count < 0)
    count += prec;

  rshift_double (l1, h1, count, prec, &s1l, &s1h, 0);
  lshift_double (l1, h1, prec - count, prec, &s2l, &s2h, 0);
  *lv = s1l | s2l;
  *hv = s1h | s2h;
}

/* Divide doubleword integer LNUM, HNUM by doubleword integer LDEN, HDEN
   for a quotient (stored in *LQUO, *HQUO) and remainder (in *LREM, *HREM).
   CODE is a tree code for a kind of division, one of
   TRUNC_DIV_EXPR, FLOOR_DIV_EXPR, CEIL_DIV_EXPR, ROUND_DIV_EXPR
   or EXACT_DIV_EXPR
   It controls how the quotient is rounded to an integer.
   Return nonzero if the operation overflows.
   UNS nonzero says do unsigned division.  */

int
div_and_round_double (enum tree_code code, int uns,
		      unsigned HOST_WIDE_INT lnum_orig, /* num == numerator == dividend */
		      HOST_WIDE_INT hnum_orig,
		      unsigned HOST_WIDE_INT lden_orig, /* den == denominator == divisor */
		      HOST_WIDE_INT hden_orig,
		      unsigned HOST_WIDE_INT *lquo,
		      HOST_WIDE_INT *hquo, unsigned HOST_WIDE_INT *lrem,
		      HOST_WIDE_INT *hrem)
{
  int quo_neg = 0;
  HOST_WIDE_INT num[4 + 1];	/* extra element for scaling.  */
  HOST_WIDE_INT den[4], quo[4];
  int i, j;
  unsigned HOST_WIDE_INT work;
  unsigned HOST_WIDE_INT carry = 0;
  unsigned HOST_WIDE_INT lnum = lnum_orig;
  HOST_WIDE_INT hnum = hnum_orig;
  unsigned HOST_WIDE_INT lden = lden_orig;
  HOST_WIDE_INT hden = hden_orig;
  int overflow = 0;

  if (hden == 0 && lden == 0)
    overflow = 1, lden = 1;

  /* Calculate quotient sign and convert operands to unsigned.  */
  if (!uns)
    {
      if (hnum < 0)
	{
	  quo_neg = ~ quo_neg;
	  /* (minimum integer) / (-1) is the only overflow case.  */
	  if (neg_double (lnum, hnum, &lnum, &hnum)
	      && ((HOST_WIDE_INT) lden & hden) == -1)
	    overflow = 1;
	}
      if (hden < 0)
	{
	  quo_neg = ~ quo_neg;
	  neg_double (lden, hden, &lden, &hden);
	}
    }

  if (hnum == 0 && hden == 0)
    {				/* single precision */
      *hquo = *hrem = 0;
      /* This unsigned division rounds toward zero.  */
      *lquo = lnum / lden;
      goto finish_up;
    }

  if (hnum == 0)
    {				/* trivial case: dividend < divisor */
      /* hden != 0 already checked.  */
      *hquo = *lquo = 0;
      *hrem = hnum;
      *lrem = lnum;
      goto finish_up;
    }

  memset (quo, 0, sizeof quo);

  memset (num, 0, sizeof num);	/* to zero 9th element */
  memset (den, 0, sizeof den);

  encode (num, lnum, hnum);
  encode (den, lden, hden);

  /* Special code for when the divisor < BASE.  */
  if (hden == 0 && lden < (unsigned HOST_WIDE_INT) BASE)
    {
      /* hnum != 0 already checked.  */
      for (i = 4 - 1; i >= 0; i--)
	{
	  work = num[i] + carry * BASE;
	  quo[i] = work / lden;
	  carry = work % lden;
	}
    }
  else
    {
      /* Full double precision division,
	 with thanks to Don Knuth's "Seminumerical Algorithms".  */
      int num_hi_sig, den_hi_sig;
      unsigned HOST_WIDE_INT quo_est, scale;

      /* Find the highest nonzero divisor digit.  */
      for (i = 4 - 1;; i--)
	if (den[i] != 0)
	  {
	    den_hi_sig = i;
	    break;
	  }

      /* Insure that the first digit of the divisor is at least BASE/2.
	 This is required by the quotient digit estimation algorithm.  */

      scale = BASE / (den[den_hi_sig] + 1);
      if (scale > 1)
	{		/* scale divisor and dividend */
	  carry = 0;
	  for (i = 0; i <= 4 - 1; i++)
	    {
	      work = (num[i] * scale) + carry;
	      num[i] = LOWPART (work);
	      carry = HIGHPART (work);
	    }

	  num[4] = carry;
	  carry = 0;
	  for (i = 0; i <= 4 - 1; i++)
	    {
	      work = (den[i] * scale) + carry;
	      den[i] = LOWPART (work);
	      carry = HIGHPART (work);
	      if (den[i] != 0) den_hi_sig = i;
	    }
	}

      num_hi_sig = 4;

      /* Main loop */
      for (i = num_hi_sig - den_hi_sig - 1; i >= 0; i--)
	{
	  /* Guess the next quotient digit, quo_est, by dividing the first
	     two remaining dividend digits by the high order quotient digit.
	     quo_est is never low and is at most 2 high.  */
	  unsigned HOST_WIDE_INT tmp;

	  num_hi_sig = i + den_hi_sig + 1;
	  work = num[num_hi_sig] * BASE + num[num_hi_sig - 1];
	  if (num[num_hi_sig] != den[den_hi_sig])
	    quo_est = work / den[den_hi_sig];
	  else
	    quo_est = BASE - 1;

	  /* Refine quo_est so it's usually correct, and at most one high.  */
	  tmp = work - quo_est * den[den_hi_sig];
	  if (tmp < BASE
	      && (den[den_hi_sig - 1] * quo_est
		  > (tmp * BASE + num[num_hi_sig - 2])))
	    quo_est--;

	  /* Try QUO_EST as the quotient digit, by multiplying the
	     divisor by QUO_EST and subtracting from the remaining dividend.
	     Keep in mind that QUO_EST is the I - 1st digit.  */

	  carry = 0;
	  for (j = 0; j <= den_hi_sig; j++)
	    {
	      work = quo_est * den[j] + carry;
	      carry = HIGHPART (work);
	      work = num[i + j] - LOWPART (work);
	      num[i + j] = LOWPART (work);
	      carry += HIGHPART (work) != 0;
	    }

	  /* If quo_est was high by one, then num[i] went negative and
	     we need to correct things.  */
	  if (num[num_hi_sig] < (HOST_WIDE_INT) carry)
	    {
	      quo_est--;
	      carry = 0;		/* add divisor back in */
	      for (j = 0; j <= den_hi_sig; j++)
		{
		  work = num[i + j] + den[j] + carry;
		  carry = HIGHPART (work);
		  num[i + j] = LOWPART (work);
		}

	      num [num_hi_sig] += carry;
	    }

	  /* Store the quotient digit.  */
	  quo[i] = quo_est;
	}
    }

  decode (quo, lquo, hquo);

 finish_up:
  /* If result is negative, make it so.  */
  if (quo_neg)
    neg_double (*lquo, *hquo, lquo, hquo);

  /* Compute trial remainder:  rem = num - (quo * den)  */
  mul_double (*lquo, *hquo, lden_orig, hden_orig, lrem, hrem);
  neg_double (*lrem, *hrem, lrem, hrem);
  add_double (lnum_orig, hnum_orig, *lrem, *hrem, lrem, hrem);

  switch (code)
    {
    case TRUNC_DIV_EXPR:
    case TRUNC_MOD_EXPR:	/* round toward zero */
    case EXACT_DIV_EXPR:	/* for this one, it shouldn't matter */
      return overflow;

    case FLOOR_DIV_EXPR:
    case FLOOR_MOD_EXPR:	/* round toward negative infinity */
      if (quo_neg && (*lrem != 0 || *hrem != 0))   /* ratio < 0 && rem != 0 */
	{
	  /* quo = quo - 1;  */
	  add_double (*lquo, *hquo, (HOST_WIDE_INT) -1, (HOST_WIDE_INT)  -1,
		      lquo, hquo);
	}
      else
	return overflow;
      break;

    case CEIL_DIV_EXPR:
    case CEIL_MOD_EXPR:		/* round toward positive infinity */
      if (!quo_neg && (*lrem != 0 || *hrem != 0))  /* ratio > 0 && rem != 0 */
	{
	  add_double (*lquo, *hquo, (HOST_WIDE_INT) 1, (HOST_WIDE_INT) 0,
		      lquo, hquo);
	}
      else
	return overflow;
      break;

    case ROUND_DIV_EXPR:
    case ROUND_MOD_EXPR:	/* round to closest integer */
      {
	unsigned HOST_WIDE_INT labs_rem = *lrem;
	HOST_WIDE_INT habs_rem = *hrem;
	unsigned HOST_WIDE_INT labs_den = lden, ltwice;
	HOST_WIDE_INT habs_den = hden, htwice;

	/* Get absolute values.  */
	if (*hrem < 0)
	  neg_double (*lrem, *hrem, &labs_rem, &habs_rem);
	if (hden < 0)
	  neg_double (lden, hden, &labs_den, &habs_den);

	/* If (2 * abs (lrem) >= abs (lden)) */
	mul_double ((HOST_WIDE_INT) 2, (HOST_WIDE_INT) 0,
		    labs_rem, habs_rem, &ltwice, &htwice);

	if (((unsigned HOST_WIDE_INT) habs_den
	     < (unsigned HOST_WIDE_INT) htwice)
	    || (((unsigned HOST_WIDE_INT) habs_den
		 == (unsigned HOST_WIDE_INT) htwice)
		&& (labs_den < ltwice)))
	  {
	    if (*hquo < 0)
	      /* quo = quo - 1;  */
	      add_double (*lquo, *hquo,
			  (HOST_WIDE_INT) -1, (HOST_WIDE_INT) -1, lquo, hquo);
	    else
	      /* quo = quo + 1; */
	      add_double (*lquo, *hquo, (HOST_WIDE_INT) 1, (HOST_WIDE_INT) 0,
			  lquo, hquo);
	  }
	else
	  return overflow;
      }
      break;

    default:
      gcc_unreachable ();
    }

  /* Compute true remainder:  rem = num - (quo * den)  */
  mul_double (*lquo, *hquo, lden_orig, hden_orig, lrem, hrem);
  neg_double (*lrem, *hrem, lrem, hrem);
  add_double (lnum_orig, hnum_orig, *lrem, *hrem, lrem, hrem);
  return overflow;
}

/* If ARG2 divides ARG1 with zero remainder, carries out the division
   of type CODE and returns the quotient.
   Otherwise returns NULL_TREE.  */

static tree
div_if_zero_remainder (enum tree_code code, tree arg1, tree arg2)
{
  unsigned HOST_WIDE_INT int1l, int2l;
  HOST_WIDE_INT int1h, int2h;
  unsigned HOST_WIDE_INT quol, reml;
  HOST_WIDE_INT quoh, remh;
  tree type = TREE_TYPE (arg1);
  int uns = TYPE_UNSIGNED (type);

  int1l = TREE_INT_CST_LOW (arg1);
  int1h = TREE_INT_CST_HIGH (arg1);
  int2l = TREE_INT_CST_LOW (arg2);
  int2h = TREE_INT_CST_HIGH (arg2);

  div_and_round_double (code, uns, int1l, int1h, int2l, int2h,
		  	&quol, &quoh, &reml, &remh);
  if (remh != 0 || reml != 0)
    return NULL_TREE;

  return build_int_cst_wide (type, quol, quoh);
}

/* This is non-zero if we should defer warnings about undefined
   overflow.  This facility exists because these warnings are a
   special case.  The code to estimate loop iterations does not want
   to issue any warnings, since it works with expressions which do not
   occur in user code.  Various bits of cleanup code call fold(), but
   only use the result if it has certain characteristics (e.g., is a
   constant); that code only wants to issue a warning if the result is
   used.  */

static int fold_deferring_overflow_warnings;

/* If a warning about undefined overflow is deferred, this is the
   warning.  Note that this may cause us to turn two warnings into
   one, but that is fine since it is sufficient to only give one
   warning per expression.  */

static const char* fold_deferred_overflow_warning;

/* If a warning about undefined overflow is deferred, this is the
   level at which the warning should be emitted.  */

static enum warn_strict_overflow_code fold_deferred_overflow_code;

/* Start deferring overflow warnings.  We could use a stack here to
   permit nested calls, but at present it is not necessary.  */

void
fold_defer_overflow_warnings (void)
{
  ++fold_deferring_overflow_warnings;
}

/* Stop deferring overflow warnings.  If there is a pending warning,
   and ISSUE is true, then issue the warning if appropriate.  STMT is
   the statement with which the warning should be associated (used for
   location information); STMT may be NULL.  CODE is the level of the
   warning--a warn_strict_overflow_code value.  This function will use
   the smaller of CODE and the deferred code when deciding whether to
   issue the warning.  CODE may be zero to mean to always use the
   deferred code.  */

void
fold_undefer_overflow_warnings (bool issue, tree stmt, int code)
{
  const char *warnmsg;
  location_t locus;

  gcc_assert (fold_deferring_overflow_warnings > 0);
  --fold_deferring_overflow_warnings;
  if (fold_deferring_overflow_warnings > 0)
    {
      if (fold_deferred_overflow_warning != NULL
	  && code != 0
	  && code < (int) fold_deferred_overflow_code)
	fold_deferred_overflow_code = code;
      return;
    }

  warnmsg = fold_deferred_overflow_warning;
  fold_deferred_overflow_warning = NULL;

  if (!issue || warnmsg == NULL)
    return;

  /* Use the smallest code level when deciding to issue the
     warning.  */
  if (code == 0 || code > (int) fold_deferred_overflow_code)
    code = fold_deferred_overflow_code;

  if (!issue_strict_overflow_warning (code))
    return;

  if (stmt == NULL_TREE || !EXPR_HAS_LOCATION (stmt))
    locus = input_location;
  else
    locus = EXPR_LOCATION (stmt);
  warning (OPT_Wstrict_overflow, "%H%s", &locus, warnmsg);
}

/* Stop deferring overflow warnings, ignoring any deferred
   warnings.  */

void
fold_undefer_and_ignore_overflow_warnings (void)
{
  fold_undefer_overflow_warnings (false, NULL_TREE, 0);
}

/* Whether we are deferring overflow warnings.  */

bool
fold_deferring_overflow_warnings_p (void)
{
  return fold_deferring_overflow_warnings > 0;
}

/* This is called when we fold something based on the fact that signed
   overflow is undefined.  */

static void
fold_overflow_warning (const char* gmsgid, enum warn_strict_overflow_code wc)
{
  gcc_assert (!flag_wrapv && !flag_trapv);
  if (fold_deferring_overflow_warnings > 0)
    {
      if (fold_deferred_overflow_warning == NULL
	  || wc < fold_deferred_overflow_code)
	{
	  fold_deferred_overflow_warning = gmsgid;
	  fold_deferred_overflow_code = wc;
	}
    }
  else if (issue_strict_overflow_warning (wc))
    warning (OPT_Wstrict_overflow, "%s", gmsgid);
}

/* Return true if the built-in mathematical function specified by CODE
   is odd, i.e. -f(x) == f(-x).  */

static bool
negate_mathfn_p (enum built_in_function code)
{
  switch (code)
    {
    CASE_FLT_FN (BUILT_IN_ASIN):
    CASE_FLT_FN (BUILT_IN_ASINH):
    CASE_FLT_FN (BUILT_IN_ATAN):
    CASE_FLT_FN (BUILT_IN_ATANH):
    CASE_FLT_FN (BUILT_IN_CBRT):
    CASE_FLT_FN (BUILT_IN_SIN):
    CASE_FLT_FN (BUILT_IN_SINH):
    CASE_FLT_FN (BUILT_IN_TAN):
    CASE_FLT_FN (BUILT_IN_TANH):
      return true;

    default:
      break;
    }
  return false;
}

/* Check whether we may negate an integer constant T without causing
   overflow.  */

bool
may_negate_without_overflow_p (tree t)
{
  unsigned HOST_WIDE_INT val;
  unsigned int prec;
  tree type;

  gcc_assert (TREE_CODE (t) == INTEGER_CST);

  type = TREE_TYPE (t);
  if (TYPE_UNSIGNED (type))
    return false;

  prec = TYPE_PRECISION (type);
  if (prec > HOST_BITS_PER_WIDE_INT)
    {
      if (TREE_INT_CST_LOW (t) != 0)
	return true;
      prec -= HOST_BITS_PER_WIDE_INT;
      val = TREE_INT_CST_HIGH (t);
    }
  else
    val = TREE_INT_CST_LOW (t);
  if (prec < HOST_BITS_PER_WIDE_INT)
    val &= ((unsigned HOST_WIDE_INT) 1 << prec) - 1;
  return val != ((unsigned HOST_WIDE_INT) 1 << (prec - 1));
}

/* Determine whether an expression T can be cheaply negated using
   the function negate_expr without introducing undefined overflow.  */

static bool
negate_expr_p (tree t)
{
  tree type;

  if (t == 0)
    return false;

  type = TREE_TYPE (t);

  STRIP_SIGN_NOPS (t);
  switch (TREE_CODE (t))
    {
    case INTEGER_CST:
      if (TYPE_OVERFLOW_WRAPS (type))
	return true;

      /* Check that -CST will not overflow type.  */
      return may_negate_without_overflow_p (t);
    case BIT_NOT_EXPR:
      return (INTEGRAL_TYPE_P (type)
	      && TYPE_OVERFLOW_WRAPS (type));

    case REAL_CST:
    case NEGATE_EXPR:
      return true;

    case COMPLEX_CST:
      return negate_expr_p (TREE_REALPART (t))
	     && negate_expr_p (TREE_IMAGPART (t));

    case PLUS_EXPR:
      if (FLOAT_TYPE_P (type) && !flag_unsafe_math_optimizations)
	return false;
      /* -(A + B) -> (-B) - A.  */
      if (negate_expr_p (TREE_OPERAND (t, 1))
	  && reorder_operands_p (TREE_OPERAND (t, 0),
				 TREE_OPERAND (t, 1)))
	return true;
      /* -(A + B) -> (-A) - B.  */
      return negate_expr_p (TREE_OPERAND (t, 0));

    case MINUS_EXPR:
      /* We can't turn -(A-B) into B-A when we honor signed zeros.  */
      return (! FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations)
	     && reorder_operands_p (TREE_OPERAND (t, 0),
				    TREE_OPERAND (t, 1));

    case MULT_EXPR:
      if (TYPE_UNSIGNED (TREE_TYPE (t)))
        break;

      /* Fall through.  */

    case RDIV_EXPR:
      if (! HONOR_SIGN_DEPENDENT_ROUNDING (TYPE_MODE (TREE_TYPE (t))))
	return negate_expr_p (TREE_OPERAND (t, 1))
	       || negate_expr_p (TREE_OPERAND (t, 0));
      break;

    case TRUNC_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case EXACT_DIV_EXPR:
      /* In general we can't negate A / B, because if A is INT_MIN and
	 B is 1, we may turn this into INT_MIN / -1 which is undefined
	 and actually traps on some architectures.  But if overflow is
	 undefined, we can negate, because - (INT_MIN / 1) is an
	 overflow.  */
      if (INTEGRAL_TYPE_P (TREE_TYPE (t))
	  && !TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (t)))
        break;
      return negate_expr_p (TREE_OPERAND (t, 1))
             || negate_expr_p (TREE_OPERAND (t, 0));

    case NOP_EXPR:
      /* Negate -((double)float) as (double)(-float).  */
      if (TREE_CODE (type) == REAL_TYPE)
	{
	  tree tem = strip_float_extensions (t);
	  if (tem != t)
	    return negate_expr_p (tem);
	}
      break;

    case CALL_EXPR:
      /* Negate -f(x) as f(-x).  */
      if (negate_mathfn_p (builtin_mathfn_code (t)))
	return negate_expr_p (TREE_VALUE (TREE_OPERAND (t, 1)));
      break;

    case RSHIFT_EXPR:
      /* Optimize -((int)x >> 31) into (unsigned)x >> 31.  */
      if (TREE_CODE (TREE_OPERAND (t, 1)) == INTEGER_CST)
	{
	  tree op1 = TREE_OPERAND (t, 1);
	  if (TREE_INT_CST_HIGH (op1) == 0
	      && (unsigned HOST_WIDE_INT) (TYPE_PRECISION (type) - 1)
		 == TREE_INT_CST_LOW (op1))
	    return true;
	}
      break;

    default:
      break;
    }
  return false;
}

/* Given T, an expression, return a folded tree for -T or NULL_TREE, if no
   simplification is possible.
   If negate_expr_p would return true for T, NULL_TREE will never be
   returned.  */

static tree
fold_negate_expr (tree t)
{
  tree type = TREE_TYPE (t);
  tree tem;

  switch (TREE_CODE (t))
    {
    /* Convert - (~A) to A + 1.  */
    case BIT_NOT_EXPR:
      if (INTEGRAL_TYPE_P (type))
        return fold_build2 (PLUS_EXPR, type, TREE_OPERAND (t, 0),
                            build_int_cst (type, 1));
      break;
      
    case INTEGER_CST:
      tem = fold_negate_const (t, type);
      if (!TREE_OVERFLOW (tem)
	  || !TYPE_OVERFLOW_TRAPS (type))
	return tem;
      break;

    case REAL_CST:
      tem = fold_negate_const (t, type);
      /* Two's complement FP formats, such as c4x, may overflow.  */
      if (! TREE_OVERFLOW (tem) || ! flag_trapping_math)
	return tem;
      break;

    case COMPLEX_CST:
      {
	tree rpart = negate_expr (TREE_REALPART (t));
	tree ipart = negate_expr (TREE_IMAGPART (t));

	if ((TREE_CODE (rpart) == REAL_CST
	     && TREE_CODE (ipart) == REAL_CST)
	    || (TREE_CODE (rpart) == INTEGER_CST
		&& TREE_CODE (ipart) == INTEGER_CST))
	  return build_complex (type, rpart, ipart);
      }
      break;

    case NEGATE_EXPR:
      return TREE_OPERAND (t, 0);

    case PLUS_EXPR:
      if (! FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations)
	{
	  /* -(A + B) -> (-B) - A.  */
	  if (negate_expr_p (TREE_OPERAND (t, 1))
	      && reorder_operands_p (TREE_OPERAND (t, 0),
				     TREE_OPERAND (t, 1)))
	    {
	      tem = negate_expr (TREE_OPERAND (t, 1));
	      return fold_build2 (MINUS_EXPR, type,
				  tem, TREE_OPERAND (t, 0));
	    }

	  /* -(A + B) -> (-A) - B.  */
	  if (negate_expr_p (TREE_OPERAND (t, 0)))
	    {
	      tem = negate_expr (TREE_OPERAND (t, 0));
	      return fold_build2 (MINUS_EXPR, type,
				  tem, TREE_OPERAND (t, 1));
	    }
	}
      break;

    case MINUS_EXPR:
      /* - (A - B) -> B - A  */
      if ((! FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations)
	  && reorder_operands_p (TREE_OPERAND (t, 0), TREE_OPERAND (t, 1)))
	return fold_build2 (MINUS_EXPR, type,
			    TREE_OPERAND (t, 1), TREE_OPERAND (t, 0));
      break;

    case MULT_EXPR:
      if (TYPE_UNSIGNED (type))
        break;

      /* Fall through.  */

    case RDIV_EXPR:
      if (! HONOR_SIGN_DEPENDENT_ROUNDING (TYPE_MODE (type)))
	{
	  tem = TREE_OPERAND (t, 1);
	  if (negate_expr_p (tem))
	    return fold_build2 (TREE_CODE (t), type,
				TREE_OPERAND (t, 0), negate_expr (tem));
	  tem = TREE_OPERAND (t, 0);
	  if (negate_expr_p (tem))
	    return fold_build2 (TREE_CODE (t), type,
				negate_expr (tem), TREE_OPERAND (t, 1));
	}
      break;

    case TRUNC_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case EXACT_DIV_EXPR:
      /* In general we can't negate A / B, because if A is INT_MIN and
	 B is 1, we may turn this into INT_MIN / -1 which is undefined
	 and actually traps on some architectures.  But if overflow is
	 undefined, we can negate, because - (INT_MIN / 1) is an
	 overflow.  */
      if (!INTEGRAL_TYPE_P (type) || TYPE_OVERFLOW_UNDEFINED (type))
        {
	  const char * const warnmsg = G_("assuming signed overflow does not "
					  "occur when negating a division");
          tem = TREE_OPERAND (t, 1);
          if (negate_expr_p (tem))
	    {
	      if (INTEGRAL_TYPE_P (type)
		  && (TREE_CODE (tem) != INTEGER_CST
		      || integer_onep (tem)))
		fold_overflow_warning (warnmsg, WARN_STRICT_OVERFLOW_MISC);
	      return fold_build2 (TREE_CODE (t), type,
				  TREE_OPERAND (t, 0), negate_expr (tem));
	    }
          tem = TREE_OPERAND (t, 0);
          if (negate_expr_p (tem))
	    {
	      if (INTEGRAL_TYPE_P (type)
		  && (TREE_CODE (tem) != INTEGER_CST
		      || tree_int_cst_equal (tem, TYPE_MIN_VALUE (type))))
		fold_overflow_warning (warnmsg, WARN_STRICT_OVERFLOW_MISC);
	      return fold_build2 (TREE_CODE (t), type,
				  negate_expr (tem), TREE_OPERAND (t, 1));
	    }
        }
      break;

    case NOP_EXPR:
      /* Convert -((double)float) into (double)(-float).  */
      if (TREE_CODE (type) == REAL_TYPE)
	{
	  tem = strip_float_extensions (t);
	  if (tem != t && negate_expr_p (tem))
	    return negate_expr (tem);
	}
      break;

    case CALL_EXPR:
      /* Negate -f(x) as f(-x).  */
      if (negate_mathfn_p (builtin_mathfn_code (t))
	  && negate_expr_p (TREE_VALUE (TREE_OPERAND (t, 1))))
	{
	  tree fndecl, arg, arglist;

	  fndecl = get_callee_fndecl (t);
	  arg = negate_expr (TREE_VALUE (TREE_OPERAND (t, 1)));
	  arglist = build_tree_list (NULL_TREE, arg);
	  return build_function_call_expr (fndecl, arglist);
	}
      break;

    case RSHIFT_EXPR:
      /* Optimize -((int)x >> 31) into (unsigned)x >> 31.  */
      if (TREE_CODE (TREE_OPERAND (t, 1)) == INTEGER_CST)
	{
	  tree op1 = TREE_OPERAND (t, 1);
	  if (TREE_INT_CST_HIGH (op1) == 0
	      && (unsigned HOST_WIDE_INT) (TYPE_PRECISION (type) - 1)
		 == TREE_INT_CST_LOW (op1))
	    {
	      tree ntype = TYPE_UNSIGNED (type)
			   ? lang_hooks.types.signed_type (type)
			   : lang_hooks.types.unsigned_type (type);
	      tree temp = fold_convert (ntype, TREE_OPERAND (t, 0));
	      temp = fold_build2 (RSHIFT_EXPR, ntype, temp, op1);
	      return fold_convert (type, temp);
	    }
	}
      break;

    default:
      break;
    }

  return NULL_TREE;
}

/* Like fold_negate_expr, but return a NEGATE_EXPR tree, if T can not be
   negated in a simpler way.  Also allow for T to be NULL_TREE, in which case
   return NULL_TREE. */

static tree
negate_expr (tree t)
{
  tree type, tem;

  if (t == NULL_TREE)
    return NULL_TREE;

  type = TREE_TYPE (t);
  STRIP_SIGN_NOPS (t);

  tem = fold_negate_expr (t);
  if (!tem)
    tem = build1 (NEGATE_EXPR, TREE_TYPE (t), t);
  return fold_convert (type, tem);
}

/* Split a tree IN into a constant, literal and variable parts that could be
   combined with CODE to make IN.  "constant" means an expression with
   TREE_CONSTANT but that isn't an actual constant.  CODE must be a
   commutative arithmetic operation.  Store the constant part into *CONP,
   the literal in *LITP and return the variable part.  If a part isn't
   present, set it to null.  If the tree does not decompose in this way,
   return the entire tree as the variable part and the other parts as null.

   If CODE is PLUS_EXPR we also split trees that use MINUS_EXPR.  In that
   case, we negate an operand that was subtracted.  Except if it is a
   literal for which we use *MINUS_LITP instead.

   If NEGATE_P is true, we are negating all of IN, again except a literal
   for which we use *MINUS_LITP instead.

   If IN is itself a literal or constant, return it as appropriate.

   Note that we do not guarantee that any of the three values will be the
   same type as IN, but they will have the same signedness and mode.  */

static tree
split_tree (tree in, enum tree_code code, tree *conp, tree *litp,
	    tree *minus_litp, int negate_p)
{
  tree var = 0;

  *conp = 0;
  *litp = 0;
  *minus_litp = 0;

  /* Strip any conversions that don't change the machine mode or signedness.  */
  STRIP_SIGN_NOPS (in);

  if (TREE_CODE (in) == INTEGER_CST || TREE_CODE (in) == REAL_CST)
    *litp = in;
  else if (TREE_CODE (in) == code
	   || (! FLOAT_TYPE_P (TREE_TYPE (in))
	       /* We can associate addition and subtraction together (even
		  though the C standard doesn't say so) for integers because
		  the value is not affected.  For reals, the value might be
		  affected, so we can't.  */
	       && ((code == PLUS_EXPR && TREE_CODE (in) == MINUS_EXPR)
		   || (code == MINUS_EXPR && TREE_CODE (in) == PLUS_EXPR))))
    {
      tree op0 = TREE_OPERAND (in, 0);
      tree op1 = TREE_OPERAND (in, 1);
      int neg1_p = TREE_CODE (in) == MINUS_EXPR;
      int neg_litp_p = 0, neg_conp_p = 0, neg_var_p = 0;

      /* First see if either of the operands is a literal, then a constant.  */
      if (TREE_CODE (op0) == INTEGER_CST || TREE_CODE (op0) == REAL_CST)
	*litp = op0, op0 = 0;
      else if (TREE_CODE (op1) == INTEGER_CST || TREE_CODE (op1) == REAL_CST)
	*litp = op1, neg_litp_p = neg1_p, op1 = 0;

      if (op0 != 0 && TREE_CONSTANT (op0))
	*conp = op0, op0 = 0;
      else if (op1 != 0 && TREE_CONSTANT (op1))
	*conp = op1, neg_conp_p = neg1_p, op1 = 0;

      /* If we haven't dealt with either operand, this is not a case we can
	 decompose.  Otherwise, VAR is either of the ones remaining, if any.  */
      if (op0 != 0 && op1 != 0)
	var = in;
      else if (op0 != 0)
	var = op0;
      else
	var = op1, neg_var_p = neg1_p;

      /* Now do any needed negations.  */
      if (neg_litp_p)
	*minus_litp = *litp, *litp = 0;
      if (neg_conp_p)
	*conp = negate_expr (*conp);
      if (neg_var_p)
	var = negate_expr (var);
    }
  else if (TREE_CONSTANT (in))
    *conp = in;
  else
    var = in;

  if (negate_p)
    {
      if (*litp)
	*minus_litp = *litp, *litp = 0;
      else if (*minus_litp)
	*litp = *minus_litp, *minus_litp = 0;
      *conp = negate_expr (*conp);
      var = negate_expr (var);
    }

  return var;
}

/* Re-associate trees split by the above function.  T1 and T2 are either
   expressions to associate or null.  Return the new expression, if any.  If
   we build an operation, do it in TYPE and with CODE.  */

static tree
associate_trees (tree t1, tree t2, enum tree_code code, tree type)
{
  if (t1 == 0)
    return t2;
  else if (t2 == 0)
    return t1;

  /* If either input is CODE, a PLUS_EXPR, or a MINUS_EXPR, don't
     try to fold this since we will have infinite recursion.  But do
     deal with any NEGATE_EXPRs.  */
  if (TREE_CODE (t1) == code || TREE_CODE (t2) == code
      || TREE_CODE (t1) == MINUS_EXPR || TREE_CODE (t2) == MINUS_EXPR)
    {
      if (code == PLUS_EXPR)
	{
	  if (TREE_CODE (t1) == NEGATE_EXPR)
	    return build2 (MINUS_EXPR, type, fold_convert (type, t2),
			   fold_convert (type, TREE_OPERAND (t1, 0)));
	  else if (TREE_CODE (t2) == NEGATE_EXPR)
	    return build2 (MINUS_EXPR, type, fold_convert (type, t1),
			   fold_convert (type, TREE_OPERAND (t2, 0)));
	  else if (integer_zerop (t2))
	    return fold_convert (type, t1);
	}
      else if (code == MINUS_EXPR)
	{
	  if (integer_zerop (t2))
	    return fold_convert (type, t1);
	}

      return build2 (code, type, fold_convert (type, t1),
		     fold_convert (type, t2));
    }

  return fold_build2 (code, type, fold_convert (type, t1),
		      fold_convert (type, t2));
}

/* Combine two integer constants ARG1 and ARG2 under operation CODE
   to produce a new constant.  Return NULL_TREE if we don't know how
   to evaluate CODE at compile-time.

   If NOTRUNC is nonzero, do not truncate the result to fit the data type.  */

tree
int_const_binop (enum tree_code code, tree arg1, tree arg2, int notrunc)
{
  unsigned HOST_WIDE_INT int1l, int2l;
  HOST_WIDE_INT int1h, int2h;
  unsigned HOST_WIDE_INT low;
  HOST_WIDE_INT hi;
  unsigned HOST_WIDE_INT garbagel;
  HOST_WIDE_INT garbageh;
  tree t;
  tree type = TREE_TYPE (arg1);
  int uns = TYPE_UNSIGNED (type);
  int is_sizetype
    = (TREE_CODE (type) == INTEGER_TYPE && TYPE_IS_SIZETYPE (type));
  int overflow = 0;

  int1l = TREE_INT_CST_LOW (arg1);
  int1h = TREE_INT_CST_HIGH (arg1);
  int2l = TREE_INT_CST_LOW (arg2);
  int2h = TREE_INT_CST_HIGH (arg2);

  switch (code)
    {
    case BIT_IOR_EXPR:
      low = int1l | int2l, hi = int1h | int2h;
      break;

    case BIT_XOR_EXPR:
      low = int1l ^ int2l, hi = int1h ^ int2h;
      break;

    case BIT_AND_EXPR:
      low = int1l & int2l, hi = int1h & int2h;
      break;

    case RSHIFT_EXPR:
      int2l = -int2l;
    case LSHIFT_EXPR:
      /* It's unclear from the C standard whether shifts can overflow.
	 The following code ignores overflow; perhaps a C standard
	 interpretation ruling is needed.  */
      lshift_double (int1l, int1h, int2l, TYPE_PRECISION (type),
		     &low, &hi, !uns);
      break;

    case RROTATE_EXPR:
      int2l = - int2l;
    case LROTATE_EXPR:
      lrotate_double (int1l, int1h, int2l, TYPE_PRECISION (type),
		      &low, &hi);
      break;

    case PLUS_EXPR:
      overflow = add_double (int1l, int1h, int2l, int2h, &low, &hi);
      break;

    case MINUS_EXPR:
      neg_double (int2l, int2h, &low, &hi);
      add_double (int1l, int1h, low, hi, &low, &hi);
      overflow = OVERFLOW_SUM_SIGN (hi, int2h, int1h);
      break;

    case MULT_EXPR:
      overflow = mul_double (int1l, int1h, int2l, int2h, &low, &hi);
      break;

    case TRUNC_DIV_EXPR:
    case FLOOR_DIV_EXPR: case CEIL_DIV_EXPR:
    case EXACT_DIV_EXPR:
      /* This is a shortcut for a common special case.  */
      if (int2h == 0 && (HOST_WIDE_INT) int2l > 0
	  && ! TREE_CONSTANT_OVERFLOW (arg1)
	  && ! TREE_CONSTANT_OVERFLOW (arg2)
	  && int1h == 0 && (HOST_WIDE_INT) int1l >= 0)
	{
	  if (code == CEIL_DIV_EXPR)
	    int1l += int2l - 1;

	  low = int1l / int2l, hi = 0;
	  break;
	}

      /* ... fall through ...  */

    case ROUND_DIV_EXPR:
      if (int2h == 0 && int2l == 0)
	return NULL_TREE;
      if (int2h == 0 && int2l == 1)
	{
	  low = int1l, hi = int1h;
	  break;
	}
      if (int1l == int2l && int1h == int2h
	  && ! (int1l == 0 && int1h == 0))
	{
	  low = 1, hi = 0;
	  break;
	}
      overflow = div_and_round_double (code, uns, int1l, int1h, int2l, int2h,
				       &low, &hi, &garbagel, &garbageh);
      break;

    case TRUNC_MOD_EXPR:
    case FLOOR_MOD_EXPR: case CEIL_MOD_EXPR:
      /* This is a shortcut for a common special case.  */
      if (int2h == 0 && (HOST_WIDE_INT) int2l > 0
	  && ! TREE_CONSTANT_OVERFLOW (arg1)
	  && ! TREE_CONSTANT_OVERFLOW (arg2)
	  && int1h == 0 && (HOST_WIDE_INT) int1l >= 0)
	{
	  if (code == CEIL_MOD_EXPR)
	    int1l += int2l - 1;
	  low = int1l % int2l, hi = 0;
	  break;
	}

      /* ... fall through ...  */

    case ROUND_MOD_EXPR:
      if (int2h == 0 && int2l == 0)
	return NULL_TREE;
      overflow = div_and_round_double (code, uns,
				       int1l, int1h, int2l, int2h,
				       &garbagel, &garbageh, &low, &hi);
      break;

    case MIN_EXPR:
    case MAX_EXPR:
      if (uns)
	low = (((unsigned HOST_WIDE_INT) int1h
		< (unsigned HOST_WIDE_INT) int2h)
	       || (((unsigned HOST_WIDE_INT) int1h
		    == (unsigned HOST_WIDE_INT) int2h)
		   && int1l < int2l));
      else
	low = (int1h < int2h
	       || (int1h == int2h && int1l < int2l));

      if (low == (code == MIN_EXPR))
	low = int1l, hi = int1h;
      else
	low = int2l, hi = int2h;
      break;

    default:
      return NULL_TREE;
    }

  t = build_int_cst_wide (TREE_TYPE (arg1), low, hi);

  if (notrunc)
    {
      /* Propagate overflow flags ourselves.  */
      if (((!uns || is_sizetype) && overflow)
	  | TREE_OVERFLOW (arg1) | TREE_OVERFLOW (arg2))
	{
	  t = copy_node (t);
	  TREE_OVERFLOW (t) = 1;
	  TREE_CONSTANT_OVERFLOW (t) = 1;
	}
      else if (TREE_CONSTANT_OVERFLOW (arg1) | TREE_CONSTANT_OVERFLOW (arg2))
	{
	  t = copy_node (t);
	  TREE_CONSTANT_OVERFLOW (t) = 1;
	}
    }
  else
    t = force_fit_type (t, 1,
			((!uns || is_sizetype) && overflow)
			| TREE_OVERFLOW (arg1) | TREE_OVERFLOW (arg2),
			TREE_CONSTANT_OVERFLOW (arg1)
			| TREE_CONSTANT_OVERFLOW (arg2));

  return t;
}

/* Combine two constants ARG1 and ARG2 under operation CODE to produce a new
   constant.  We assume ARG1 and ARG2 have the same data type, or at least
   are the same kind of constant and the same machine mode.  Return zero if
   combining the constants is not allowed in the current operating mode.

   If NOTRUNC is nonzero, do not truncate the result to fit the data type.  */

static tree
const_binop (enum tree_code code, tree arg1, tree arg2, int notrunc)
{
  /* Sanity check for the recursive cases.  */
  if (!arg1 || !arg2)
    return NULL_TREE;

  STRIP_NOPS (arg1);
  STRIP_NOPS (arg2);

  if (TREE_CODE (arg1) == INTEGER_CST)
    return int_const_binop (code, arg1, arg2, notrunc);

  if (TREE_CODE (arg1) == REAL_CST)
    {
      enum machine_mode mode;
      REAL_VALUE_TYPE d1;
      REAL_VALUE_TYPE d2;
      REAL_VALUE_TYPE value;
      REAL_VALUE_TYPE result;
      bool inexact;
      tree t, type;

      /* The following codes are handled by real_arithmetic.  */
      switch (code)
	{
	case PLUS_EXPR:
	case MINUS_EXPR:
	case MULT_EXPR:
	case RDIV_EXPR:
	case MIN_EXPR:
	case MAX_EXPR:
	  break;

	default:
	  return NULL_TREE;
	}

      d1 = TREE_REAL_CST (arg1);
      d2 = TREE_REAL_CST (arg2);

      type = TREE_TYPE (arg1);
      mode = TYPE_MODE (type);

      /* Don't perform operation if we honor signaling NaNs and
	 either operand is a NaN.  */
      if (HONOR_SNANS (mode)
	  && (REAL_VALUE_ISNAN (d1) || REAL_VALUE_ISNAN (d2)))
	return NULL_TREE;

      /* Don't perform operation if it would raise a division
	 by zero exception.  */
      if (code == RDIV_EXPR
	  && REAL_VALUES_EQUAL (d2, dconst0)
	  && (flag_trapping_math || ! MODE_HAS_INFINITIES (mode)))
	return NULL_TREE;

      /* If either operand is a NaN, just return it.  Otherwise, set up
	 for floating-point trap; we return an overflow.  */
      if (REAL_VALUE_ISNAN (d1))
	return arg1;
      else if (REAL_VALUE_ISNAN (d2))
	return arg2;

      inexact = real_arithmetic (&value, code, &d1, &d2);
      real_convert (&result, mode, &value);

      /* Don't constant fold this floating point operation if
	 the result has overflowed and flag_trapping_math.  */
      if (flag_trapping_math
	  && MODE_HAS_INFINITIES (mode)
	  && REAL_VALUE_ISINF (result)
	  && !REAL_VALUE_ISINF (d1)
	  && !REAL_VALUE_ISINF (d2))
	return NULL_TREE;

      /* Don't constant fold this floating point operation if the
	 result may dependent upon the run-time rounding mode and
	 flag_rounding_math is set, or if GCC's software emulation
	 is unable to accurately represent the result.  */
      if ((flag_rounding_math
	   || (REAL_MODE_FORMAT_COMPOSITE_P (mode)
	       && !flag_unsafe_math_optimizations))
	  && (inexact || !real_identical (&result, &value)))
	return NULL_TREE;

      t = build_real (type, result);

      TREE_OVERFLOW (t) = TREE_OVERFLOW (arg1) | TREE_OVERFLOW (arg2);
      TREE_CONSTANT_OVERFLOW (t)
	= TREE_OVERFLOW (t)
	  | TREE_CONSTANT_OVERFLOW (arg1)
	  | TREE_CONSTANT_OVERFLOW (arg2);
      return t;
    }

  if (TREE_CODE (arg1) == COMPLEX_CST)
    {
      tree type = TREE_TYPE (arg1);
      tree r1 = TREE_REALPART (arg1);
      tree i1 = TREE_IMAGPART (arg1);
      tree r2 = TREE_REALPART (arg2);
      tree i2 = TREE_IMAGPART (arg2);
      tree real, imag;

      switch (code)
	{
	case PLUS_EXPR:
	case MINUS_EXPR:
	  real = const_binop (code, r1, r2, notrunc);
	  imag = const_binop (code, i1, i2, notrunc);
	  break;

	case MULT_EXPR:
	  real = const_binop (MINUS_EXPR,
			      const_binop (MULT_EXPR, r1, r2, notrunc),
			      const_binop (MULT_EXPR, i1, i2, notrunc),
			      notrunc);
	  imag = const_binop (PLUS_EXPR,
			      const_binop (MULT_EXPR, r1, i2, notrunc),
			      const_binop (MULT_EXPR, i1, r2, notrunc),
			      notrunc);
	  break;

	case RDIV_EXPR:
	  {
	    tree magsquared
	      = const_binop (PLUS_EXPR,
			     const_binop (MULT_EXPR, r2, r2, notrunc),
			     const_binop (MULT_EXPR, i2, i2, notrunc),
			     notrunc);
	    tree t1
	      = const_binop (PLUS_EXPR,
			     const_binop (MULT_EXPR, r1, r2, notrunc),
			     const_binop (MULT_EXPR, i1, i2, notrunc),
			     notrunc);
	    tree t2
	      = const_binop (MINUS_EXPR,
			     const_binop (MULT_EXPR, i1, r2, notrunc),
			     const_binop (MULT_EXPR, r1, i2, notrunc),
			     notrunc);

	    if (INTEGRAL_TYPE_P (TREE_TYPE (r1)))
	      code = TRUNC_DIV_EXPR;

	    real = const_binop (code, t1, magsquared, notrunc);
	    imag = const_binop (code, t2, magsquared, notrunc);
	  }
	  break;

	default:
	  return NULL_TREE;
	}

      if (real && imag)
	return build_complex (type, real, imag);
    }

  return NULL_TREE;
}

/* Create a size type INT_CST node with NUMBER sign extended.  KIND
   indicates which particular sizetype to create.  */

tree
size_int_kind (HOST_WIDE_INT number, enum size_type_kind kind)
{
  return build_int_cst (sizetype_tab[(int) kind], number);
}

/* Combine operands OP1 and OP2 with arithmetic operation CODE.  CODE
   is a tree code.  The type of the result is taken from the operands.
   Both must be the same type integer type and it must be a size type.
   If the operands are constant, so is the result.  */

tree
size_binop (enum tree_code code, tree arg0, tree arg1)
{
  tree type = TREE_TYPE (arg0);

  if (arg0 == error_mark_node || arg1 == error_mark_node)
    return error_mark_node;

  gcc_assert (TREE_CODE (type) == INTEGER_TYPE && TYPE_IS_SIZETYPE (type)
	      && type == TREE_TYPE (arg1));

  /* Handle the special case of two integer constants faster.  */
  if (TREE_CODE (arg0) == INTEGER_CST && TREE_CODE (arg1) == INTEGER_CST)
    {
      /* And some specific cases even faster than that.  */
      if (code == PLUS_EXPR && integer_zerop (arg0))
	return arg1;
      else if ((code == MINUS_EXPR || code == PLUS_EXPR)
	       && integer_zerop (arg1))
	return arg0;
      else if (code == MULT_EXPR && integer_onep (arg0))
	return arg1;

      /* Handle general case of two integer constants.  */
      return int_const_binop (code, arg0, arg1, 0);
    }

  return fold_build2 (code, type, arg0, arg1);
}

/* Given two values, either both of sizetype or both of bitsizetype,
   compute the difference between the two values.  Return the value
   in signed type corresponding to the type of the operands.  */

tree
size_diffop (tree arg0, tree arg1)
{
  tree type = TREE_TYPE (arg0);
  tree ctype;

  gcc_assert (TREE_CODE (type) == INTEGER_TYPE && TYPE_IS_SIZETYPE (type)
	      && type == TREE_TYPE (arg1));

  /* If the type is already signed, just do the simple thing.  */
  if (!TYPE_UNSIGNED (type))
    return size_binop (MINUS_EXPR, arg0, arg1);

  ctype = type == bitsizetype ? sbitsizetype : ssizetype;

  /* If either operand is not a constant, do the conversions to the signed
     type and subtract.  The hardware will do the right thing with any
     overflow in the subtraction.  */
  if (TREE_CODE (arg0) != INTEGER_CST || TREE_CODE (arg1) != INTEGER_CST)
    return size_binop (MINUS_EXPR, fold_convert (ctype, arg0),
		       fold_convert (ctype, arg1));

  /* If ARG0 is larger than ARG1, subtract and return the result in CTYPE.
     Otherwise, subtract the other way, convert to CTYPE (we know that can't
     overflow) and negate (which can't either).  Special-case a result
     of zero while we're here.  */
  if (tree_int_cst_equal (arg0, arg1))
    return build_int_cst (ctype, 0);
  else if (tree_int_cst_lt (arg1, arg0))
    return fold_convert (ctype, size_binop (MINUS_EXPR, arg0, arg1));
  else
    return size_binop (MINUS_EXPR, build_int_cst (ctype, 0),
		       fold_convert (ctype, size_binop (MINUS_EXPR,
							arg1, arg0)));
}

/* A subroutine of fold_convert_const handling conversions of an
   INTEGER_CST to another integer type.  */

static tree
fold_convert_const_int_from_int (tree type, tree arg1)
{
  tree t;

  /* Given an integer constant, make new constant with new type,
     appropriately sign-extended or truncated.  */
  t = build_int_cst_wide (type, TREE_INT_CST_LOW (arg1),
			  TREE_INT_CST_HIGH (arg1));

  t = force_fit_type (t,
		      /* Don't set the overflow when
		      	 converting a pointer  */
		      !POINTER_TYPE_P (TREE_TYPE (arg1)),
		      (TREE_INT_CST_HIGH (arg1) < 0
		       && (TYPE_UNSIGNED (type)
			   < TYPE_UNSIGNED (TREE_TYPE (arg1))))
		      | TREE_OVERFLOW (arg1),
		      TREE_CONSTANT_OVERFLOW (arg1));

  return t;
}

/* A subroutine of fold_convert_const handling conversions a REAL_CST
   to an integer type.  */

static tree
fold_convert_const_int_from_real (enum tree_code code, tree type, tree arg1)
{
  int overflow = 0;
  tree t;

  /* The following code implements the floating point to integer
     conversion rules required by the Java Language Specification,
     that IEEE NaNs are mapped to zero and values that overflow
     the target precision saturate, i.e. values greater than
     INT_MAX are mapped to INT_MAX, and values less than INT_MIN
     are mapped to INT_MIN.  These semantics are allowed by the
     C and C++ standards that simply state that the behavior of
     FP-to-integer conversion is unspecified upon overflow.  */

  HOST_WIDE_INT high, low;
  REAL_VALUE_TYPE r;
  REAL_VALUE_TYPE x = TREE_REAL_CST (arg1);

  switch (code)
    {
    case FIX_TRUNC_EXPR:
      real_trunc (&r, VOIDmode, &x);
      break;

    case FIX_CEIL_EXPR:
      real_ceil (&r, VOIDmode, &x);
      break;

    case FIX_FLOOR_EXPR:
      real_floor (&r, VOIDmode, &x);
      break;

    case FIX_ROUND_EXPR:
      real_round (&r, VOIDmode, &x);
      break;

    default:
      gcc_unreachable ();
    }

  /* If R is NaN, return zero and show we have an overflow.  */
  if (REAL_VALUE_ISNAN (r))
    {
      overflow = 1;
      high = 0;
      low = 0;
    }

  /* See if R is less than the lower bound or greater than the
     upper bound.  */

  if (! overflow)
    {
      tree lt = TYPE_MIN_VALUE (type);
      REAL_VALUE_TYPE l = real_value_from_int_cst (NULL_TREE, lt);
      if (REAL_VALUES_LESS (r, l))
	{
	  overflow = 1;
	  high = TREE_INT_CST_HIGH (lt);
	  low = TREE_INT_CST_LOW (lt);
	}
    }

  if (! overflow)
    {
      tree ut = TYPE_MAX_VALUE (type);
      if (ut)
	{
	  REAL_VALUE_TYPE u = real_value_from_int_cst (NULL_TREE, ut);
	  if (REAL_VALUES_LESS (u, r))
	    {
	      overflow = 1;
	      high = TREE_INT_CST_HIGH (ut);
	      low = TREE_INT_CST_LOW (ut);
	    }
	}
    }

  if (! overflow)
    REAL_VALUE_TO_INT (&low, &high, r);

  t = build_int_cst_wide (type, low, high);

  t = force_fit_type (t, -1, overflow | TREE_OVERFLOW (arg1),
		      TREE_CONSTANT_OVERFLOW (arg1));
  return t;
}

/* A subroutine of fold_convert_const handling conversions a REAL_CST
   to another floating point type.  */

static tree
fold_convert_const_real_from_real (tree type, tree arg1)
{
  REAL_VALUE_TYPE value;
  tree t;

  real_convert (&value, TYPE_MODE (type), &TREE_REAL_CST (arg1));
  t = build_real (type, value);

  TREE_OVERFLOW (t) = TREE_OVERFLOW (arg1);
  TREE_CONSTANT_OVERFLOW (t)
    = TREE_OVERFLOW (t) | TREE_CONSTANT_OVERFLOW (arg1);
  return t;
}

/* Attempt to fold type conversion operation CODE of expression ARG1 to
   type TYPE.  If no simplification can be done return NULL_TREE.  */

static tree
fold_convert_const (enum tree_code code, tree type, tree arg1)
{
  if (TREE_TYPE (arg1) == type)
    return arg1;

  if (POINTER_TYPE_P (type) || INTEGRAL_TYPE_P (type))
    {
      if (TREE_CODE (arg1) == INTEGER_CST)
	return fold_convert_const_int_from_int (type, arg1);
      else if (TREE_CODE (arg1) == REAL_CST)
	return fold_convert_const_int_from_real (code, type, arg1);
    }
  else if (TREE_CODE (type) == REAL_TYPE)
    {
      if (TREE_CODE (arg1) == INTEGER_CST)
	return build_real_from_int_cst (type, arg1);
      if (TREE_CODE (arg1) == REAL_CST)
	return fold_convert_const_real_from_real (type, arg1);
    }
  return NULL_TREE;
}

/* Construct a vector of zero elements of vector type TYPE.  */

static tree
build_zero_vector (tree type)
{
  tree elem, list;
  int i, units;

  elem = fold_convert_const (NOP_EXPR, TREE_TYPE (type), integer_zero_node);
  units = TYPE_VECTOR_SUBPARTS (type);
  
  list = NULL_TREE;
  for (i = 0; i < units; i++)
    list = tree_cons (NULL_TREE, elem, list);
  return build_vector (type, list);
}

/* Convert expression ARG to type TYPE.  Used by the middle-end for
   simple conversions in preference to calling the front-end's convert.  */

tree
fold_convert (tree type, tree arg)
{
  tree orig = TREE_TYPE (arg);
  tree tem;

  if (type == orig)
    return arg;

  if (TREE_CODE (arg) == ERROR_MARK
      || TREE_CODE (type) == ERROR_MARK
      || TREE_CODE (orig) == ERROR_MARK)
    return error_mark_node;

  if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (orig)
      || lang_hooks.types_compatible_p (TYPE_MAIN_VARIANT (type),
					TYPE_MAIN_VARIANT (orig)))
    return fold_build1 (NOP_EXPR, type, arg);

  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE: case ENUMERAL_TYPE: case BOOLEAN_TYPE:
    case POINTER_TYPE: case REFERENCE_TYPE:
      /* APPLE LOCAL blocks 5862465 */
    case BLOCK_POINTER_TYPE:
    case OFFSET_TYPE:
      if (TREE_CODE (arg) == INTEGER_CST)
	{
	  tem = fold_convert_const (NOP_EXPR, type, arg);
	  if (tem != NULL_TREE)
	    return tem;
	}
      if (INTEGRAL_TYPE_P (orig) || POINTER_TYPE_P (orig)
	  || TREE_CODE (orig) == OFFSET_TYPE)
        return fold_build1 (NOP_EXPR, type, arg);
      if (TREE_CODE (orig) == COMPLEX_TYPE)
	{
	  tem = fold_build1 (REALPART_EXPR, TREE_TYPE (orig), arg);
	  return fold_convert (type, tem);
	}
      gcc_assert (TREE_CODE (orig) == VECTOR_TYPE
		  && tree_int_cst_equal (TYPE_SIZE (type), TYPE_SIZE (orig)));
      return fold_build1 (NOP_EXPR, type, arg);

    case REAL_TYPE:
      if (TREE_CODE (arg) == INTEGER_CST)
	{
	  tem = fold_convert_const (FLOAT_EXPR, type, arg);
	  if (tem != NULL_TREE)
	    return tem;
	}
      else if (TREE_CODE (arg) == REAL_CST)
	{
	  tem = fold_convert_const (NOP_EXPR, type, arg);
	  if (tem != NULL_TREE)
	    return tem;
	}

      switch (TREE_CODE (orig))
	{
	case INTEGER_TYPE:
	case BOOLEAN_TYPE: case ENUMERAL_TYPE:
	case POINTER_TYPE: case REFERENCE_TYPE:
	  return fold_build1 (FLOAT_EXPR, type, arg);

	case REAL_TYPE:
	  return fold_build1 (NOP_EXPR, type, arg);

	case COMPLEX_TYPE:
	  tem = fold_build1 (REALPART_EXPR, TREE_TYPE (orig), arg);
	  return fold_convert (type, tem);

	default:
	  gcc_unreachable ();
	}

    case COMPLEX_TYPE:
      switch (TREE_CODE (orig))
	{
	case INTEGER_TYPE:
	case BOOLEAN_TYPE: case ENUMERAL_TYPE:
	case POINTER_TYPE: case REFERENCE_TYPE:
	case REAL_TYPE:
	  return build2 (COMPLEX_EXPR, type,
			 fold_convert (TREE_TYPE (type), arg),
			 fold_convert (TREE_TYPE (type), integer_zero_node));
	case COMPLEX_TYPE:
	  {
	    tree rpart, ipart;

	    if (TREE_CODE (arg) == COMPLEX_EXPR)
	      {
		rpart = fold_convert (TREE_TYPE (type), TREE_OPERAND (arg, 0));
		ipart = fold_convert (TREE_TYPE (type), TREE_OPERAND (arg, 1));
		return fold_build2 (COMPLEX_EXPR, type, rpart, ipart);
	      }

	    arg = save_expr (arg);
	    rpart = fold_build1 (REALPART_EXPR, TREE_TYPE (orig), arg);
	    ipart = fold_build1 (IMAGPART_EXPR, TREE_TYPE (orig), arg);
	    rpart = fold_convert (TREE_TYPE (type), rpart);
	    ipart = fold_convert (TREE_TYPE (type), ipart);
	    return fold_build2 (COMPLEX_EXPR, type, rpart, ipart);
	  }

	default:
	  gcc_unreachable ();
	}

    case VECTOR_TYPE:
      if (integer_zerop (arg))
	return build_zero_vector (type);
      gcc_assert (tree_int_cst_equal (TYPE_SIZE (type), TYPE_SIZE (orig)));
      gcc_assert (INTEGRAL_TYPE_P (orig) || POINTER_TYPE_P (orig)
		  || TREE_CODE (orig) == VECTOR_TYPE);
      return fold_build1 (VIEW_CONVERT_EXPR, type, arg);

    case VOID_TYPE:
      return fold_build1 (NOP_EXPR, type, fold_ignored_result (arg));

    default:
      gcc_unreachable ();
    }
}

/* Return false if expr can be assumed not to be an lvalue, true
   otherwise.  */

static bool
maybe_lvalue_p (tree x)
{
  /* We only need to wrap lvalue tree codes.  */
  switch (TREE_CODE (x))
  {
  case VAR_DECL:
  case PARM_DECL:
  case RESULT_DECL:
  case LABEL_DECL:
  case FUNCTION_DECL:
  case SSA_NAME:

  case COMPONENT_REF:
  case INDIRECT_REF:
  case ALIGN_INDIRECT_REF:
  case MISALIGNED_INDIRECT_REF:
  case ARRAY_REF:
  case ARRAY_RANGE_REF:
  case BIT_FIELD_REF:
  case OBJ_TYPE_REF:

  case REALPART_EXPR:
  case IMAGPART_EXPR:
  case PREINCREMENT_EXPR:
  case PREDECREMENT_EXPR:
  case SAVE_EXPR:
  case TRY_CATCH_EXPR:
  case WITH_CLEANUP_EXPR:
  case COMPOUND_EXPR:
  case MODIFY_EXPR:
  case TARGET_EXPR:
  case COND_EXPR:
  case BIND_EXPR:
  case MIN_EXPR:
  case MAX_EXPR:
    break;

  default:
    /* Assume the worst for front-end tree codes.  */
    if ((int)TREE_CODE (x) >= NUM_TREE_CODES)
      break;
    return false;
  }

  return true;
}

/* Return an expr equal to X but certainly not valid as an lvalue.  */

tree
non_lvalue (tree x)
{
  /* While we are in GIMPLE, NON_LVALUE_EXPR doesn't mean anything to
     us.  */
  if (in_gimple_form)
    return x;

  if (! maybe_lvalue_p (x))
    return x;
  return build1 (NON_LVALUE_EXPR, TREE_TYPE (x), x);
}

/* Nonzero means lvalues are limited to those valid in pedantic ANSI C.
   Zero means allow extended lvalues.  */

int pedantic_lvalues;

/* When pedantic, return an expr equal to X but certainly not valid as a
   pedantic lvalue.  Otherwise, return X.  */

static tree
pedantic_non_lvalue (tree x)
{
  if (pedantic_lvalues)
    return non_lvalue (x);
  else
    return x;
}

/* Given a tree comparison code, return the code that is the logical inverse
   of the given code.  It is not safe to do this for floating-point
   comparisons, except for NE_EXPR and EQ_EXPR, so we receive a machine mode
   as well: if reversing the comparison is unsafe, return ERROR_MARK.  */

enum tree_code
invert_tree_comparison (enum tree_code code, bool honor_nans)
{
  if (honor_nans && flag_trapping_math)
    return ERROR_MARK;

  switch (code)
    {
    case EQ_EXPR:
      return NE_EXPR;
    case NE_EXPR:
      return EQ_EXPR;
    case GT_EXPR:
      return honor_nans ? UNLE_EXPR : LE_EXPR;
    case GE_EXPR:
      return honor_nans ? UNLT_EXPR : LT_EXPR;
    case LT_EXPR:
      return honor_nans ? UNGE_EXPR : GE_EXPR;
    case LE_EXPR:
      return honor_nans ? UNGT_EXPR : GT_EXPR;
    case LTGT_EXPR:
      return UNEQ_EXPR;
    case UNEQ_EXPR:
      return LTGT_EXPR;
    case UNGT_EXPR:
      return LE_EXPR;
    case UNGE_EXPR:
      return LT_EXPR;
    case UNLT_EXPR:
      return GE_EXPR;
    case UNLE_EXPR:
      return GT_EXPR;
    case ORDERED_EXPR:
      return UNORDERED_EXPR;
    case UNORDERED_EXPR:
      return ORDERED_EXPR;
    default:
      gcc_unreachable ();
    }
}

/* Similar, but return the comparison that results if the operands are
   swapped.  This is safe for floating-point.  */

enum tree_code
swap_tree_comparison (enum tree_code code)
{
  switch (code)
    {
    case EQ_EXPR:
    case NE_EXPR:
    case ORDERED_EXPR:
    case UNORDERED_EXPR:
    case LTGT_EXPR:
    case UNEQ_EXPR:
      return code;
    case GT_EXPR:
      return LT_EXPR;
    case GE_EXPR:
      return LE_EXPR;
    case LT_EXPR:
      return GT_EXPR;
    case LE_EXPR:
      return GE_EXPR;
    case UNGT_EXPR:
      return UNLT_EXPR;
    case UNGE_EXPR:
      return UNLE_EXPR;
    case UNLT_EXPR:
      return UNGT_EXPR;
    case UNLE_EXPR:
      return UNGE_EXPR;
    default:
      gcc_unreachable ();
    }
}


/* Convert a comparison tree code from an enum tree_code representation
   into a compcode bit-based encoding.  This function is the inverse of
   compcode_to_comparison.  */

static enum comparison_code
comparison_to_compcode (enum tree_code code)
{
  switch (code)
    {
    case LT_EXPR:
      return COMPCODE_LT;
    case EQ_EXPR:
      return COMPCODE_EQ;
    case LE_EXPR:
      return COMPCODE_LE;
    case GT_EXPR:
      return COMPCODE_GT;
    case NE_EXPR:
      return COMPCODE_NE;
    case GE_EXPR:
      return COMPCODE_GE;
    case ORDERED_EXPR:
      return COMPCODE_ORD;
    case UNORDERED_EXPR:
      return COMPCODE_UNORD;
    case UNLT_EXPR:
      return COMPCODE_UNLT;
    case UNEQ_EXPR:
      return COMPCODE_UNEQ;
    case UNLE_EXPR:
      return COMPCODE_UNLE;
    case UNGT_EXPR:
      return COMPCODE_UNGT;
    case LTGT_EXPR:
      return COMPCODE_LTGT;
    case UNGE_EXPR:
      return COMPCODE_UNGE;
    default:
      gcc_unreachable ();
    }
}

/* Convert a compcode bit-based encoding of a comparison operator back
   to GCC's enum tree_code representation.  This function is the
   inverse of comparison_to_compcode.  */

static enum tree_code
compcode_to_comparison (enum comparison_code code)
{
  switch (code)
    {
    case COMPCODE_LT:
      return LT_EXPR;
    case COMPCODE_EQ:
      return EQ_EXPR;
    case COMPCODE_LE:
      return LE_EXPR;
    case COMPCODE_GT:
      return GT_EXPR;
    case COMPCODE_NE:
      return NE_EXPR;
    case COMPCODE_GE:
      return GE_EXPR;
    case COMPCODE_ORD:
      return ORDERED_EXPR;
    case COMPCODE_UNORD:
      return UNORDERED_EXPR;
    case COMPCODE_UNLT:
      return UNLT_EXPR;
    case COMPCODE_UNEQ:
      return UNEQ_EXPR;
    case COMPCODE_UNLE:
      return UNLE_EXPR;
    case COMPCODE_UNGT:
      return UNGT_EXPR;
    case COMPCODE_LTGT:
      return LTGT_EXPR;
    case COMPCODE_UNGE:
      return UNGE_EXPR;
    default:
      gcc_unreachable ();
    }
}

/* Return a tree for the comparison which is the combination of
   doing the AND or OR (depending on CODE) of the two operations LCODE
   and RCODE on the identical operands LL_ARG and LR_ARG.  Take into account
   the possibility of trapping if the mode has NaNs, and return NULL_TREE
   if this makes the transformation invalid.  */

tree
combine_comparisons (enum tree_code code, enum tree_code lcode,
		     enum tree_code rcode, tree truth_type,
		     tree ll_arg, tree lr_arg)
{
  bool honor_nans = HONOR_NANS (TYPE_MODE (TREE_TYPE (ll_arg)));
  enum comparison_code lcompcode = comparison_to_compcode (lcode);
  enum comparison_code rcompcode = comparison_to_compcode (rcode);
  enum comparison_code compcode;

  switch (code)
    {
    case TRUTH_AND_EXPR: case TRUTH_ANDIF_EXPR:
      compcode = lcompcode & rcompcode;
      break;

    case TRUTH_OR_EXPR: case TRUTH_ORIF_EXPR:
      compcode = lcompcode | rcompcode;
      break;

    default:
      return NULL_TREE;
    }

  if (!honor_nans)
    {
      /* Eliminate unordered comparisons, as well as LTGT and ORD
	 which are not used unless the mode has NaNs.  */
      compcode &= ~COMPCODE_UNORD;
      if (compcode == COMPCODE_LTGT)
	compcode = COMPCODE_NE;
      else if (compcode == COMPCODE_ORD)
	compcode = COMPCODE_TRUE;
    }
   else if (flag_trapping_math)
     {
	/* Check that the original operation and the optimized ones will trap
	   under the same condition.  */
	bool ltrap = (lcompcode & COMPCODE_UNORD) == 0
		     && (lcompcode != COMPCODE_EQ)
		     && (lcompcode != COMPCODE_ORD);
	bool rtrap = (rcompcode & COMPCODE_UNORD) == 0
		     && (rcompcode != COMPCODE_EQ)
		     && (rcompcode != COMPCODE_ORD);
	bool trap = (compcode & COMPCODE_UNORD) == 0
		    && (compcode != COMPCODE_EQ)
		    && (compcode != COMPCODE_ORD);

        /* In a short-circuited boolean expression the LHS might be
	   such that the RHS, if evaluated, will never trap.  For
	   example, in ORD (x, y) && (x < y), we evaluate the RHS only
	   if neither x nor y is NaN.  (This is a mixed blessing: for
	   example, the expression above will never trap, hence
	   optimizing it to x < y would be invalid).  */
        if ((code == TRUTH_ORIF_EXPR && (lcompcode & COMPCODE_UNORD))
            || (code == TRUTH_ANDIF_EXPR && !(lcompcode & COMPCODE_UNORD)))
          rtrap = false;

        /* If the comparison was short-circuited, and only the RHS
	   trapped, we may now generate a spurious trap.  */
	if (rtrap && !ltrap
	    && (code == TRUTH_ANDIF_EXPR || code == TRUTH_ORIF_EXPR))
	  return NULL_TREE;

	/* If we changed the conditions that cause a trap, we lose.  */
	if ((ltrap || rtrap) != trap)
	  return NULL_TREE;
      }

  if (compcode == COMPCODE_TRUE)
    return constant_boolean_node (true, truth_type);
  else if (compcode == COMPCODE_FALSE)
    return constant_boolean_node (false, truth_type);
  else
    return fold_build2 (compcode_to_comparison (compcode),
			truth_type, ll_arg, lr_arg);
}

/* Return nonzero if CODE is a tree code that represents a truth value.  */

static int
truth_value_p (enum tree_code code)
{
  return (TREE_CODE_CLASS (code) == tcc_comparison
	  || code == TRUTH_AND_EXPR || code == TRUTH_ANDIF_EXPR
	  || code == TRUTH_OR_EXPR || code == TRUTH_ORIF_EXPR
	  || code == TRUTH_XOR_EXPR || code == TRUTH_NOT_EXPR);
}

/* Return nonzero if two operands (typically of the same tree node)
   are necessarily equal.  If either argument has side-effects this
   function returns zero.  FLAGS modifies behavior as follows:

   If OEP_ONLY_CONST is set, only return nonzero for constants.
   This function tests whether the operands are indistinguishable;
   it does not test whether they are equal using C's == operation.
   The distinction is important for IEEE floating point, because
   (1) -0.0 and 0.0 are distinguishable, but -0.0==0.0, and
   (2) two NaNs may be indistinguishable, but NaN!=NaN.

   If OEP_ONLY_CONST is unset, a VAR_DECL is considered equal to itself
   even though it may hold multiple values during a function.
   This is because a GCC tree node guarantees that nothing else is
   executed between the evaluation of its "operands" (which may often
   be evaluated in arbitrary order).  Hence if the operands themselves
   don't side-effect, the VAR_DECLs, PARM_DECLs etc... must hold the
   same value in each operand/subexpression.  Hence leaving OEP_ONLY_CONST
   unset means assuming isochronic (or instantaneous) tree equivalence.
   Unless comparing arbitrary expression trees, such as from different
   statements, this flag can usually be left unset.

   If OEP_PURE_SAME is set, then pure functions with identical arguments
   are considered the same.  It is used when the caller has other ways
   to ensure that global memory is unchanged in between.  */

int
operand_equal_p (tree arg0, tree arg1, unsigned int flags)
{
  /* If either is ERROR_MARK, they aren't equal.  */
  if (TREE_CODE (arg0) == ERROR_MARK || TREE_CODE (arg1) == ERROR_MARK)
    return 0;

  /* If both types don't have the same signedness, then we can't consider
     them equal.  We must check this before the STRIP_NOPS calls
     because they may change the signedness of the arguments.  */
  if (TYPE_UNSIGNED (TREE_TYPE (arg0)) != TYPE_UNSIGNED (TREE_TYPE (arg1)))
    return 0;

  /* If both types don't have the same precision, then it is not safe
     to strip NOPs.  */
  if (TYPE_PRECISION (TREE_TYPE (arg0)) != TYPE_PRECISION (TREE_TYPE (arg1)))
    return 0;

  STRIP_NOPS (arg0);
  STRIP_NOPS (arg1);

  /* In case both args are comparisons but with different comparison
     code, try to swap the comparison operands of one arg to produce
     a match and compare that variant.  */
  if (TREE_CODE (arg0) != TREE_CODE (arg1)
      && COMPARISON_CLASS_P (arg0)
      && COMPARISON_CLASS_P (arg1))
    {
      enum tree_code swap_code = swap_tree_comparison (TREE_CODE (arg1));

      if (TREE_CODE (arg0) == swap_code)
	return operand_equal_p (TREE_OPERAND (arg0, 0),
			        TREE_OPERAND (arg1, 1), flags)
	       && operand_equal_p (TREE_OPERAND (arg0, 1),
				   TREE_OPERAND (arg1, 0), flags);
    }

  if (TREE_CODE (arg0) != TREE_CODE (arg1)
      /* This is needed for conversions and for COMPONENT_REF.
	 Might as well play it safe and always test this.  */
      || TREE_CODE (TREE_TYPE (arg0)) == ERROR_MARK
      || TREE_CODE (TREE_TYPE (arg1)) == ERROR_MARK
      || TYPE_MODE (TREE_TYPE (arg0)) != TYPE_MODE (TREE_TYPE (arg1)))
    return 0;

  /* If ARG0 and ARG1 are the same SAVE_EXPR, they are necessarily equal.
     We don't care about side effects in that case because the SAVE_EXPR
     takes care of that for us. In all other cases, two expressions are
     equal if they have no side effects.  If we have two identical
     expressions with side effects that should be treated the same due
     to the only side effects being identical SAVE_EXPR's, that will
     be detected in the recursive calls below.  */
  if (arg0 == arg1 && ! (flags & OEP_ONLY_CONST)
      && (TREE_CODE (arg0) == SAVE_EXPR
	  || (! TREE_SIDE_EFFECTS (arg0) && ! TREE_SIDE_EFFECTS (arg1))))
    return 1;

  /* Next handle constant cases, those for which we can return 1 even
     if ONLY_CONST is set.  */
  if (TREE_CONSTANT (arg0) && TREE_CONSTANT (arg1))
    switch (TREE_CODE (arg0))
      {
      case INTEGER_CST:
	return (! TREE_CONSTANT_OVERFLOW (arg0)
		&& ! TREE_CONSTANT_OVERFLOW (arg1)
		&& tree_int_cst_equal (arg0, arg1));

      case REAL_CST:
	return (! TREE_CONSTANT_OVERFLOW (arg0)
		&& ! TREE_CONSTANT_OVERFLOW (arg1)
		&& REAL_VALUES_IDENTICAL (TREE_REAL_CST (arg0),
					  TREE_REAL_CST (arg1)));

      case VECTOR_CST:
	{
	  tree v1, v2;

	  if (TREE_CONSTANT_OVERFLOW (arg0)
	      || TREE_CONSTANT_OVERFLOW (arg1))
	    return 0;

	  v1 = TREE_VECTOR_CST_ELTS (arg0);
	  v2 = TREE_VECTOR_CST_ELTS (arg1);
	  while (v1 && v2)
	    {
	      if (!operand_equal_p (TREE_VALUE (v1), TREE_VALUE (v2),
				    flags))
		return 0;
	      v1 = TREE_CHAIN (v1);
	      v2 = TREE_CHAIN (v2);
	    }

	  return v1 == v2;
	}

      case COMPLEX_CST:
	return (operand_equal_p (TREE_REALPART (arg0), TREE_REALPART (arg1),
				 flags)
		&& operand_equal_p (TREE_IMAGPART (arg0), TREE_IMAGPART (arg1),
				    flags));

      case STRING_CST:
	return (TREE_STRING_LENGTH (arg0) == TREE_STRING_LENGTH (arg1)
		&& ! memcmp (TREE_STRING_POINTER (arg0),
			      TREE_STRING_POINTER (arg1),
			      TREE_STRING_LENGTH (arg0)));

      case ADDR_EXPR:
	return operand_equal_p (TREE_OPERAND (arg0, 0), TREE_OPERAND (arg1, 0),
				0);
      default:
	break;
      }

  if (flags & OEP_ONLY_CONST)
    return 0;

/* Define macros to test an operand from arg0 and arg1 for equality and a
   variant that allows null and views null as being different from any
   non-null value.  In the latter case, if either is null, the both
   must be; otherwise, do the normal comparison.  */
#define OP_SAME(N) operand_equal_p (TREE_OPERAND (arg0, N),	\
				    TREE_OPERAND (arg1, N), flags)

#define OP_SAME_WITH_NULL(N)				\
  ((!TREE_OPERAND (arg0, N) || !TREE_OPERAND (arg1, N))	\
   ? TREE_OPERAND (arg0, N) == TREE_OPERAND (arg1, N) : OP_SAME (N))

  switch (TREE_CODE_CLASS (TREE_CODE (arg0)))
    {
    case tcc_unary:
      /* Two conversions are equal only if signedness and modes match.  */
      switch (TREE_CODE (arg0))
        {
        case NOP_EXPR:
        case CONVERT_EXPR:
        case FIX_CEIL_EXPR:
        case FIX_TRUNC_EXPR:
        case FIX_FLOOR_EXPR:
        case FIX_ROUND_EXPR:
	  if (TYPE_UNSIGNED (TREE_TYPE (arg0))
	      != TYPE_UNSIGNED (TREE_TYPE (arg1)))
	    return 0;
	  break;
	default:
	  break;
	}

      return OP_SAME (0);


    case tcc_comparison:
    case tcc_binary:
      if (OP_SAME (0) && OP_SAME (1))
	return 1;

      /* For commutative ops, allow the other order.  */
      return (commutative_tree_code (TREE_CODE (arg0))
	      && operand_equal_p (TREE_OPERAND (arg0, 0),
				  TREE_OPERAND (arg1, 1), flags)
	      && operand_equal_p (TREE_OPERAND (arg0, 1),
				  TREE_OPERAND (arg1, 0), flags));

    case tcc_reference:
      /* If either of the pointer (or reference) expressions we are
	 dereferencing contain a side effect, these cannot be equal.  */
      if (TREE_SIDE_EFFECTS (arg0)
	  || TREE_SIDE_EFFECTS (arg1))
	return 0;

      switch (TREE_CODE (arg0))
	{
	case INDIRECT_REF:
	case ALIGN_INDIRECT_REF:
	case MISALIGNED_INDIRECT_REF:
	case REALPART_EXPR:
	case IMAGPART_EXPR:
	  return OP_SAME (0);

	case ARRAY_REF:
	case ARRAY_RANGE_REF:
	  /* Operands 2 and 3 may be null.
	     Compare the array index by value if it is constant first as we
	     may have different types but same value here.  */
	  return (OP_SAME (0)
		  && (tree_int_cst_equal (TREE_OPERAND (arg0, 1),
					  TREE_OPERAND (arg1, 1))
		      || OP_SAME (1))
		  && OP_SAME_WITH_NULL (2)
		  && OP_SAME_WITH_NULL (3));

	case COMPONENT_REF:
	  /* Handle operand 2 the same as for ARRAY_REF.  Operand 0
	     may be NULL when we're called to compare MEM_EXPRs.  */
	  return OP_SAME_WITH_NULL (0)
		 && OP_SAME (1)
		 && OP_SAME_WITH_NULL (2);

	case BIT_FIELD_REF:
	  return OP_SAME (0) && OP_SAME (1) && OP_SAME (2);

	default:
	  return 0;
	}

    case tcc_expression:
      switch (TREE_CODE (arg0))
	{
	case ADDR_EXPR:
	case TRUTH_NOT_EXPR:
	  return OP_SAME (0);

	case TRUTH_ANDIF_EXPR:
	case TRUTH_ORIF_EXPR:
	  return OP_SAME (0) && OP_SAME (1);

	case TRUTH_AND_EXPR:
	case TRUTH_OR_EXPR:
	case TRUTH_XOR_EXPR:
	  if (OP_SAME (0) && OP_SAME (1))
	    return 1;

	  /* Otherwise take into account this is a commutative operation.  */
	  return (operand_equal_p (TREE_OPERAND (arg0, 0),
				   TREE_OPERAND (arg1, 1), flags)
		  && operand_equal_p (TREE_OPERAND (arg0, 1),
				      TREE_OPERAND (arg1, 0), flags));

	case CALL_EXPR:
	  /* If the CALL_EXPRs call different functions, then they
	     clearly can not be equal.  */
	  if (!OP_SAME (0))
	    return 0;

	  {
	    unsigned int cef = call_expr_flags (arg0);
	    if (flags & OEP_PURE_SAME)
	      cef &= ECF_CONST | ECF_PURE;
	    else
	      cef &= ECF_CONST;
	    if (!cef)
	      return 0;
	  }

	  /* Now see if all the arguments are the same.  operand_equal_p
	     does not handle TREE_LIST, so we walk the operands here
	     feeding them to operand_equal_p.  */
	  arg0 = TREE_OPERAND (arg0, 1);
	  arg1 = TREE_OPERAND (arg1, 1);
	  while (arg0 && arg1)
	    {
	      if (! operand_equal_p (TREE_VALUE (arg0), TREE_VALUE (arg1),
				     flags))
		return 0;

	      arg0 = TREE_CHAIN (arg0);
	      arg1 = TREE_CHAIN (arg1);
	    }

	  /* If we get here and both argument lists are exhausted
	     then the CALL_EXPRs are equal.  */
	  return ! (arg0 || arg1);

	default:
	  return 0;
	}

    case tcc_declaration:
      /* Consider __builtin_sqrt equal to sqrt.  */
      return (TREE_CODE (arg0) == FUNCTION_DECL
	      && DECL_BUILT_IN (arg0) && DECL_BUILT_IN (arg1)
	      && DECL_BUILT_IN_CLASS (arg0) == DECL_BUILT_IN_CLASS (arg1)
	      && DECL_FUNCTION_CODE (arg0) == DECL_FUNCTION_CODE (arg1));

    default:
      return 0;
    }

#undef OP_SAME
#undef OP_SAME_WITH_NULL
}

/* Similar to operand_equal_p, but see if ARG0 might have been made by
   shorten_compare from ARG1 when ARG1 was being compared with OTHER.

   When in doubt, return 0.  */

static int
operand_equal_for_comparison_p (tree arg0, tree arg1, tree other)
{
  int unsignedp1, unsignedpo;
  tree primarg0, primarg1, primother;
  unsigned int correct_width;

  if (operand_equal_p (arg0, arg1, 0))
    return 1;

  if (! INTEGRAL_TYPE_P (TREE_TYPE (arg0))
      || ! INTEGRAL_TYPE_P (TREE_TYPE (arg1)))
    return 0;

  /* Discard any conversions that don't change the modes of ARG0 and ARG1
     and see if the inner values are the same.  This removes any
     signedness comparison, which doesn't matter here.  */
  primarg0 = arg0, primarg1 = arg1;
  STRIP_NOPS (primarg0);
  STRIP_NOPS (primarg1);
  if (operand_equal_p (primarg0, primarg1, 0))
    return 1;

  /* Duplicate what shorten_compare does to ARG1 and see if that gives the
     actual comparison operand, ARG0.

     First throw away any conversions to wider types
     already present in the operands.  */

  primarg1 = get_narrower (arg1, &unsignedp1);
  primother = get_narrower (other, &unsignedpo);

  correct_width = TYPE_PRECISION (TREE_TYPE (arg1));
  if (unsignedp1 == unsignedpo
      && TYPE_PRECISION (TREE_TYPE (primarg1)) < correct_width
      && TYPE_PRECISION (TREE_TYPE (primother)) < correct_width)
    {
      tree type = TREE_TYPE (arg0);

      /* Make sure shorter operand is extended the right way
	 to match the longer operand.  */
      primarg1 = fold_convert (lang_hooks.types.signed_or_unsigned_type
			       (unsignedp1, TREE_TYPE (primarg1)), primarg1);

      if (operand_equal_p (arg0, fold_convert (type, primarg1), 0))
	return 1;
    }

  return 0;
}

/* See if ARG is an expression that is either a comparison or is performing
   arithmetic on comparisons.  The comparisons must only be comparing
   two different values, which will be stored in *CVAL1 and *CVAL2; if
   they are nonzero it means that some operands have already been found.
   No variables may be used anywhere else in the expression except in the
   comparisons.  If SAVE_P is true it means we removed a SAVE_EXPR around
   the expression and save_expr needs to be called with CVAL1 and CVAL2.

   If this is true, return 1.  Otherwise, return zero.  */

static int
twoval_comparison_p (tree arg, tree *cval1, tree *cval2, int *save_p)
{
  enum tree_code code = TREE_CODE (arg);
  enum tree_code_class class = TREE_CODE_CLASS (code);

  /* We can handle some of the tcc_expression cases here.  */
  if (class == tcc_expression && code == TRUTH_NOT_EXPR)
    class = tcc_unary;
  else if (class == tcc_expression
	   && (code == TRUTH_ANDIF_EXPR || code == TRUTH_ORIF_EXPR
	       || code == COMPOUND_EXPR))
    class = tcc_binary;

  else if (class == tcc_expression && code == SAVE_EXPR
	   && ! TREE_SIDE_EFFECTS (TREE_OPERAND (arg, 0)))
    {
      /* If we've already found a CVAL1 or CVAL2, this expression is
	 two complex to handle.  */
      if (*cval1 || *cval2)
	return 0;

      class = tcc_unary;
      *save_p = 1;
    }

  switch (class)
    {
    case tcc_unary:
      return twoval_comparison_p (TREE_OPERAND (arg, 0), cval1, cval2, save_p);

    case tcc_binary:
      return (twoval_comparison_p (TREE_OPERAND (arg, 0), cval1, cval2, save_p)
	      && twoval_comparison_p (TREE_OPERAND (arg, 1),
				      cval1, cval2, save_p));

    case tcc_constant:
      return 1;

    case tcc_expression:
      if (code == COND_EXPR)
	return (twoval_comparison_p (TREE_OPERAND (arg, 0),
				     cval1, cval2, save_p)
		&& twoval_comparison_p (TREE_OPERAND (arg, 1),
					cval1, cval2, save_p)
		&& twoval_comparison_p (TREE_OPERAND (arg, 2),
					cval1, cval2, save_p));
      return 0;

    case tcc_comparison:
      /* First see if we can handle the first operand, then the second.  For
	 the second operand, we know *CVAL1 can't be zero.  It must be that
	 one side of the comparison is each of the values; test for the
	 case where this isn't true by failing if the two operands
	 are the same.  */

      if (operand_equal_p (TREE_OPERAND (arg, 0),
			   TREE_OPERAND (arg, 1), 0))
	return 0;

      if (*cval1 == 0)
	*cval1 = TREE_OPERAND (arg, 0);
      else if (operand_equal_p (*cval1, TREE_OPERAND (arg, 0), 0))
	;
      else if (*cval2 == 0)
	*cval2 = TREE_OPERAND (arg, 0);
      else if (operand_equal_p (*cval2, TREE_OPERAND (arg, 0), 0))
	;
      else
	return 0;

      if (operand_equal_p (*cval1, TREE_OPERAND (arg, 1), 0))
	;
      else if (*cval2 == 0)
	*cval2 = TREE_OPERAND (arg, 1);
      else if (operand_equal_p (*cval2, TREE_OPERAND (arg, 1), 0))
	;
      else
	return 0;

      return 1;

    default:
      return 0;
    }
}

/* ARG is a tree that is known to contain just arithmetic operations and
   comparisons.  Evaluate the operations in the tree substituting NEW0 for
   any occurrence of OLD0 as an operand of a comparison and likewise for
   NEW1 and OLD1.  */

static tree
eval_subst (tree arg, tree old0, tree new0, tree old1, tree new1)
{
  tree type = TREE_TYPE (arg);
  enum tree_code code = TREE_CODE (arg);
  enum tree_code_class class = TREE_CODE_CLASS (code);

  /* We can handle some of the tcc_expression cases here.  */
  if (class == tcc_expression && code == TRUTH_NOT_EXPR)
    class = tcc_unary;
  else if (class == tcc_expression
	   && (code == TRUTH_ANDIF_EXPR || code == TRUTH_ORIF_EXPR))
    class = tcc_binary;

  switch (class)
    {
    case tcc_unary:
      return fold_build1 (code, type,
			  eval_subst (TREE_OPERAND (arg, 0),
				      old0, new0, old1, new1));

    case tcc_binary:
      return fold_build2 (code, type,
			  eval_subst (TREE_OPERAND (arg, 0),
				      old0, new0, old1, new1),
			  eval_subst (TREE_OPERAND (arg, 1),
				      old0, new0, old1, new1));

    case tcc_expression:
      switch (code)
	{
	case SAVE_EXPR:
	  return eval_subst (TREE_OPERAND (arg, 0), old0, new0, old1, new1);

	case COMPOUND_EXPR:
	  return eval_subst (TREE_OPERAND (arg, 1), old0, new0, old1, new1);

	case COND_EXPR:
	  return fold_build3 (code, type,
			      eval_subst (TREE_OPERAND (arg, 0),
					  old0, new0, old1, new1),
			      eval_subst (TREE_OPERAND (arg, 1),
					  old0, new0, old1, new1),
			      eval_subst (TREE_OPERAND (arg, 2),
					  old0, new0, old1, new1));
	default:
	  break;
	}
      /* Fall through - ???  */

    case tcc_comparison:
      {
	tree arg0 = TREE_OPERAND (arg, 0);
	tree arg1 = TREE_OPERAND (arg, 1);

	/* We need to check both for exact equality and tree equality.  The
	   former will be true if the operand has a side-effect.  In that
	   case, we know the operand occurred exactly once.  */

	if (arg0 == old0 || operand_equal_p (arg0, old0, 0))
	  arg0 = new0;
	else if (arg0 == old1 || operand_equal_p (arg0, old1, 0))
	  arg0 = new1;

	if (arg1 == old0 || operand_equal_p (arg1, old0, 0))
	  arg1 = new0;
	else if (arg1 == old1 || operand_equal_p (arg1, old1, 0))
	  arg1 = new1;

	return fold_build2 (code, type, arg0, arg1);
      }

    default:
      return arg;
    }
}

/* Return a tree for the case when the result of an expression is RESULT
   converted to TYPE and OMITTED was previously an operand of the expression
   but is now not needed (e.g., we folded OMITTED * 0).

   If OMITTED has side effects, we must evaluate it.  Otherwise, just do
   the conversion of RESULT to TYPE.  */

tree
omit_one_operand (tree type, tree result, tree omitted)
{
  tree t = fold_convert (type, result);

  if (TREE_SIDE_EFFECTS (omitted))
    return build2 (COMPOUND_EXPR, type, fold_ignored_result (omitted), t);

  return non_lvalue (t);
}

/* Similar, but call pedantic_non_lvalue instead of non_lvalue.  */

static tree
pedantic_omit_one_operand (tree type, tree result, tree omitted)
{
  tree t = fold_convert (type, result);

  if (TREE_SIDE_EFFECTS (omitted))
    return build2 (COMPOUND_EXPR, type, fold_ignored_result (omitted), t);

  return pedantic_non_lvalue (t);
}

/* Return a tree for the case when the result of an expression is RESULT
   converted to TYPE and OMITTED1 and OMITTED2 were previously operands
   of the expression but are now not needed.

   If OMITTED1 or OMITTED2 has side effects, they must be evaluated.
   If both OMITTED1 and OMITTED2 have side effects, OMITTED1 is
   evaluated before OMITTED2.  Otherwise, if neither has side effects,
   just do the conversion of RESULT to TYPE.  */

tree
omit_two_operands (tree type, tree result, tree omitted1, tree omitted2)
{
  tree t = fold_convert (type, result);

  if (TREE_SIDE_EFFECTS (omitted2))
    t = build2 (COMPOUND_EXPR, type, omitted2, t);
  if (TREE_SIDE_EFFECTS (omitted1))
    t = build2 (COMPOUND_EXPR, type, omitted1, t);

  return TREE_CODE (t) != COMPOUND_EXPR ? non_lvalue (t) : t;
}


/* Return a simplified tree node for the truth-negation of ARG.  This
   never alters ARG itself.  We assume that ARG is an operation that
   returns a truth value (0 or 1).

   FIXME: one would think we would fold the result, but it causes
   problems with the dominator optimizer.  */

tree
fold_truth_not_expr (tree arg)
{
  tree type = TREE_TYPE (arg);
  enum tree_code code = TREE_CODE (arg);

  /* If this is a comparison, we can simply invert it, except for
     floating-point non-equality comparisons, in which case we just
     enclose a TRUTH_NOT_EXPR around what we have.  */

  if (TREE_CODE_CLASS (code) == tcc_comparison)
    {
      tree op_type = TREE_TYPE (TREE_OPERAND (arg, 0));
      if (FLOAT_TYPE_P (op_type)
	  && flag_trapping_math
	  && code != ORDERED_EXPR && code != UNORDERED_EXPR
	  && code != NE_EXPR && code != EQ_EXPR)
	return NULL_TREE;
      else
	{
	  code = invert_tree_comparison (code,
					 HONOR_NANS (TYPE_MODE (op_type)));
	  if (code == ERROR_MARK)
	    return NULL_TREE;
	  else
	    return build2 (code, type,
			   TREE_OPERAND (arg, 0), TREE_OPERAND (arg, 1));
	}
    }

  switch (code)
    {
    case INTEGER_CST:
      return constant_boolean_node (integer_zerop (arg), type);

    case TRUTH_AND_EXPR:
      return build2 (TRUTH_OR_EXPR, type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)),
		     invert_truthvalue (TREE_OPERAND (arg, 1)));

    case TRUTH_OR_EXPR:
      return build2 (TRUTH_AND_EXPR, type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)),
		     invert_truthvalue (TREE_OPERAND (arg, 1)));

    case TRUTH_XOR_EXPR:
      /* Here we can invert either operand.  We invert the first operand
	 unless the second operand is a TRUTH_NOT_EXPR in which case our
	 result is the XOR of the first operand with the inside of the
	 negation of the second operand.  */

      if (TREE_CODE (TREE_OPERAND (arg, 1)) == TRUTH_NOT_EXPR)
	return build2 (TRUTH_XOR_EXPR, type, TREE_OPERAND (arg, 0),
		       TREE_OPERAND (TREE_OPERAND (arg, 1), 0));
      else
	return build2 (TRUTH_XOR_EXPR, type,
		       invert_truthvalue (TREE_OPERAND (arg, 0)),
		       TREE_OPERAND (arg, 1));

    case TRUTH_ANDIF_EXPR:
      return build2 (TRUTH_ORIF_EXPR, type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)),
		     invert_truthvalue (TREE_OPERAND (arg, 1)));

    case TRUTH_ORIF_EXPR:
      return build2 (TRUTH_ANDIF_EXPR, type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)),
		     invert_truthvalue (TREE_OPERAND (arg, 1)));

    case TRUTH_NOT_EXPR:
      return TREE_OPERAND (arg, 0);

    case COND_EXPR:
      {
	tree arg1 = TREE_OPERAND (arg, 1);
	tree arg2 = TREE_OPERAND (arg, 2);
	/* A COND_EXPR may have a throw as one operand, which
	   then has void type.  Just leave void operands
	   as they are.  */
	return build3 (COND_EXPR, type, TREE_OPERAND (arg, 0),
		       VOID_TYPE_P (TREE_TYPE (arg1))
		       ? arg1 : invert_truthvalue (arg1),
		       VOID_TYPE_P (TREE_TYPE (arg2))
		       ? arg2 : invert_truthvalue (arg2));
      }

    case COMPOUND_EXPR:
      return build2 (COMPOUND_EXPR, type, TREE_OPERAND (arg, 0),
		     invert_truthvalue (TREE_OPERAND (arg, 1)));

    case NON_LVALUE_EXPR:
      return invert_truthvalue (TREE_OPERAND (arg, 0));

    case NOP_EXPR:
      if (TREE_CODE (TREE_TYPE (arg)) == BOOLEAN_TYPE)
	return build1 (TRUTH_NOT_EXPR, type, arg);

    case CONVERT_EXPR:
    case FLOAT_EXPR:
      return build1 (TREE_CODE (arg), type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)));

    case BIT_AND_EXPR:
      if (!integer_onep (TREE_OPERAND (arg, 1)))
	break;
      return build2 (EQ_EXPR, type, arg,
		     build_int_cst (type, 0));

    case SAVE_EXPR:
      return build1 (TRUTH_NOT_EXPR, type, arg);

    case CLEANUP_POINT_EXPR:
      return build1 (CLEANUP_POINT_EXPR, type,
		     invert_truthvalue (TREE_OPERAND (arg, 0)));

    default:
      break;
    }

  return NULL_TREE;
}

/* Return a simplified tree node for the truth-negation of ARG.  This
   never alters ARG itself.  We assume that ARG is an operation that
   returns a truth value (0 or 1).

   FIXME: one would think we would fold the result, but it causes
   problems with the dominator optimizer.  */

tree
invert_truthvalue (tree arg)
{
  tree tem;

  if (TREE_CODE (arg) == ERROR_MARK)
    return arg;

  tem = fold_truth_not_expr (arg);
  if (!tem)
    tem = build1 (TRUTH_NOT_EXPR, TREE_TYPE (arg), arg);

  return tem;
}

/* Given a bit-wise operation CODE applied to ARG0 and ARG1, see if both
   operands are another bit-wise operation with a common input.  If so,
   distribute the bit operations to save an operation and possibly two if
   constants are involved.  For example, convert
	(A | B) & (A | C) into A | (B & C)
   Further simplification will occur if B and C are constants.

   If this optimization cannot be done, 0 will be returned.  */

static tree
distribute_bit_expr (enum tree_code code, tree type, tree arg0, tree arg1)
{
  tree common;
  tree left, right;

  if (TREE_CODE (arg0) != TREE_CODE (arg1)
      || TREE_CODE (arg0) == code
      || (TREE_CODE (arg0) != BIT_AND_EXPR
	  && TREE_CODE (arg0) != BIT_IOR_EXPR))
    return 0;

  if (operand_equal_p (TREE_OPERAND (arg0, 0), TREE_OPERAND (arg1, 0), 0))
    {
      common = TREE_OPERAND (arg0, 0);
      left = TREE_OPERAND (arg0, 1);
      right = TREE_OPERAND (arg1, 1);
    }
  else if (operand_equal_p (TREE_OPERAND (arg0, 0), TREE_OPERAND (arg1, 1), 0))
    {
      common = TREE_OPERAND (arg0, 0);
      left = TREE_OPERAND (arg0, 1);
      right = TREE_OPERAND (arg1, 0);
    }
  else if (operand_equal_p (TREE_OPERAND (arg0, 1), TREE_OPERAND (arg1, 0), 0))
    {
      common = TREE_OPERAND (arg0, 1);
      left = TREE_OPERAND (arg0, 0);
      right = TREE_OPERAND (arg1, 1);
    }
  else if (operand_equal_p (TREE_OPERAND (arg0, 1), TREE_OPERAND (arg1, 1), 0))
    {
      common = TREE_OPERAND (arg0, 1);
      left = TREE_OPERAND (arg0, 0);
      right = TREE_OPERAND (arg1, 0);
    }
  else
    return 0;

  return fold_build2 (TREE_CODE (arg0), type, common,
		      fold_build2 (code, type, left, right));
}

/* Knowing that ARG0 and ARG1 are both RDIV_EXPRs, simplify a binary operation
   with code CODE.  This optimization is unsafe.  */
static tree
distribute_real_division (enum tree_code code, tree type, tree arg0, tree arg1)
{
  bool mul0 = TREE_CODE (arg0) == MULT_EXPR;
  bool mul1 = TREE_CODE (arg1) == MULT_EXPR;

  /* (A / C) +- (B / C) -> (A +- B) / C.  */
  if (mul0 == mul1
      && operand_equal_p (TREE_OPERAND (arg0, 1),
		       TREE_OPERAND (arg1, 1), 0))
    return fold_build2 (mul0 ? MULT_EXPR : RDIV_EXPR, type,
			fold_build2 (code, type,
				     TREE_OPERAND (arg0, 0),
				     TREE_OPERAND (arg1, 0)),
			TREE_OPERAND (arg0, 1));

  /* (A / C1) +- (A / C2) -> A * (1 / C1 +- 1 / C2).  */
  if (operand_equal_p (TREE_OPERAND (arg0, 0),
		       TREE_OPERAND (arg1, 0), 0)
      && TREE_CODE (TREE_OPERAND (arg0, 1)) == REAL_CST
      && TREE_CODE (TREE_OPERAND (arg1, 1)) == REAL_CST)
    {
      REAL_VALUE_TYPE r0, r1;
      r0 = TREE_REAL_CST (TREE_OPERAND (arg0, 1));
      r1 = TREE_REAL_CST (TREE_OPERAND (arg1, 1));
      if (!mul0)
	real_arithmetic (&r0, RDIV_EXPR, &dconst1, &r0);
      if (!mul1)
        real_arithmetic (&r1, RDIV_EXPR, &dconst1, &r1);
      real_arithmetic (&r0, code, &r0, &r1);
      return fold_build2 (MULT_EXPR, type,
			  TREE_OPERAND (arg0, 0),
			  build_real (type, r0));
    }

  return NULL_TREE;
}

/* Return a BIT_FIELD_REF of type TYPE to refer to BITSIZE bits of INNER
   starting at BITPOS.  The field is unsigned if UNSIGNEDP is nonzero.  */

static tree
make_bit_field_ref (tree inner, tree type, int bitsize, int bitpos,
		    int unsignedp)
{
  tree result;

  if (bitpos == 0)
    {
      tree size = TYPE_SIZE (TREE_TYPE (inner));
      if ((INTEGRAL_TYPE_P (TREE_TYPE (inner))
	   || POINTER_TYPE_P (TREE_TYPE (inner)))
	  && host_integerp (size, 0) 
	  && tree_low_cst (size, 0) == bitsize)
	return fold_convert (type, inner);
    }

  result = build3 (BIT_FIELD_REF, type, inner,
		   size_int (bitsize), bitsize_int (bitpos));

  BIT_FIELD_REF_UNSIGNED (result) = unsignedp;

  return result;
}

/* Optimize a bit-field compare.

   There are two cases:  First is a compare against a constant and the
   second is a comparison of two items where the fields are at the same
   bit position relative to the start of a chunk (byte, halfword, word)
   large enough to contain it.  In these cases we can avoid the shift
   implicit in bitfield extractions.

   For constants, we emit a compare of the shifted constant with the
   BIT_AND_EXPR of a mask and a byte, halfword, or word of the operand being
   compared.  For two fields at the same position, we do the ANDs with the
   similar mask and compare the result of the ANDs.

   CODE is the comparison code, known to be either NE_EXPR or EQ_EXPR.
   COMPARE_TYPE is the type of the comparison, and LHS and RHS
   are the left and right operands of the comparison, respectively.

   If the optimization described above can be done, we return the resulting
   tree.  Otherwise we return zero.  */

static tree
optimize_bit_field_compare (enum tree_code code, tree compare_type,
			    tree lhs, tree rhs)
{
  HOST_WIDE_INT lbitpos, lbitsize, rbitpos, rbitsize, nbitpos, nbitsize;
  tree type = TREE_TYPE (lhs);
  tree signed_type, unsigned_type;
  int const_p = TREE_CODE (rhs) == INTEGER_CST;
  enum machine_mode lmode, rmode, nmode;
  int lunsignedp, runsignedp;
  int lvolatilep = 0, rvolatilep = 0;
  tree linner, rinner = NULL_TREE;
  tree mask;
  tree offset;

  /* Get all the information about the extractions being done.  If the bit size
     if the same as the size of the underlying object, we aren't doing an
     extraction at all and so can do nothing.  We also don't want to
     do anything if the inner expression is a PLACEHOLDER_EXPR since we
     then will no longer be able to replace it.  */
  linner = get_inner_reference (lhs, &lbitsize, &lbitpos, &offset, &lmode,
				&lunsignedp, &lvolatilep, false);
  if (linner == lhs || lbitsize == GET_MODE_BITSIZE (lmode) || lbitsize < 0
      || offset != 0 || TREE_CODE (linner) == PLACEHOLDER_EXPR)
    return 0;

 if (!const_p)
   {
     /* If this is not a constant, we can only do something if bit positions,
	sizes, and signedness are the same.  */
     rinner = get_inner_reference (rhs, &rbitsize, &rbitpos, &offset, &rmode,
				   &runsignedp, &rvolatilep, false);

     if (rinner == rhs || lbitpos != rbitpos || lbitsize != rbitsize
	 || lunsignedp != runsignedp || offset != 0
	 || TREE_CODE (rinner) == PLACEHOLDER_EXPR)
       return 0;
   }

  /* See if we can find a mode to refer to this field.  We should be able to,
     but fail if we can't.  */
  nmode = get_best_mode (lbitsize, lbitpos,
			 const_p ? TYPE_ALIGN (TREE_TYPE (linner))
			 : MIN (TYPE_ALIGN (TREE_TYPE (linner)),
				TYPE_ALIGN (TREE_TYPE (rinner))),
			 word_mode, lvolatilep || rvolatilep);
  if (nmode == VOIDmode)
    return 0;

  /* Set signed and unsigned types of the precision of this mode for the
     shifts below.  */
  signed_type = lang_hooks.types.type_for_mode (nmode, 0);
  unsigned_type = lang_hooks.types.type_for_mode (nmode, 1);

  /* Compute the bit position and size for the new reference and our offset
     within it. If the new reference is the same size as the original, we
     won't optimize anything, so return zero.  */
  nbitsize = GET_MODE_BITSIZE (nmode);
  nbitpos = lbitpos & ~ (nbitsize - 1);
  lbitpos -= nbitpos;
  if (nbitsize == lbitsize)
    return 0;

  if (BYTES_BIG_ENDIAN)
    lbitpos = nbitsize - lbitsize - lbitpos;

  /* Make the mask to be used against the extracted field.  */
  mask = build_int_cst (unsigned_type, -1);
  mask = force_fit_type (mask, 0, false, false);
  mask = fold_convert (unsigned_type, mask);
  mask = const_binop (LSHIFT_EXPR, mask, size_int (nbitsize - lbitsize), 0);
  mask = const_binop (RSHIFT_EXPR, mask,
		      size_int (nbitsize - lbitsize - lbitpos), 0);

  if (! const_p)
    /* If not comparing with constant, just rework the comparison
       and return.  */
    return build2 (code, compare_type,
		   build2 (BIT_AND_EXPR, unsigned_type,
			   make_bit_field_ref (linner, unsigned_type,
					       nbitsize, nbitpos, 1),
			   mask),
		   build2 (BIT_AND_EXPR, unsigned_type,
			   make_bit_field_ref (rinner, unsigned_type,
					       nbitsize, nbitpos, 1),
			   mask));

  /* Otherwise, we are handling the constant case. See if the constant is too
     big for the field.  Warn and return a tree of for 0 (false) if so.  We do
     this not only for its own sake, but to avoid having to test for this
     error case below.  If we didn't, we might generate wrong code.

     For unsigned fields, the constant shifted right by the field length should
     be all zero.  For signed fields, the high-order bits should agree with
     the sign bit.  */

  if (lunsignedp)
    {
      if (! integer_zerop (const_binop (RSHIFT_EXPR,
					fold_convert (unsigned_type, rhs),
					size_int (lbitsize), 0)))
	{
	  warning (0, "comparison is always %d due to width of bit-field",
		   code == NE_EXPR);
	  return constant_boolean_node (code == NE_EXPR, compare_type);
	}
    }
  else
    {
      tree tem = const_binop (RSHIFT_EXPR, fold_convert (signed_type, rhs),
			      size_int (lbitsize - 1), 0);
      if (! integer_zerop (tem) && ! integer_all_onesp (tem))
	{
	  warning (0, "comparison is always %d due to width of bit-field",
		   code == NE_EXPR);
	  return constant_boolean_node (code == NE_EXPR, compare_type);
	}
    }

  /* Single-bit compares should always be against zero.  */
  if (lbitsize == 1 && ! integer_zerop (rhs))
    {
      code = code == EQ_EXPR ? NE_EXPR : EQ_EXPR;
      rhs = build_int_cst (type, 0);
    }

  /* Make a new bitfield reference, shift the constant over the
     appropriate number of bits and mask it with the computed mask
     (in case this was a signed field).  If we changed it, make a new one.  */
  lhs = make_bit_field_ref (linner, unsigned_type, nbitsize, nbitpos, 1);
  if (lvolatilep)
    {
      TREE_SIDE_EFFECTS (lhs) = 1;
      TREE_THIS_VOLATILE (lhs) = 1;
    }

  rhs = const_binop (BIT_AND_EXPR,
		     const_binop (LSHIFT_EXPR,
				  fold_convert (unsigned_type, rhs),
				  size_int (lbitpos), 0),
		     mask, 0);

  return build2 (code, compare_type,
		 build2 (BIT_AND_EXPR, unsigned_type, lhs, mask),
		 rhs);
}

/* Subroutine for fold_truthop: decode a field reference.

   If EXP is a comparison reference, we return the innermost reference.

   *PBITSIZE is set to the number of bits in the reference, *PBITPOS is
   set to the starting bit number.

   If the innermost field can be completely contained in a mode-sized
   unit, *PMODE is set to that mode.  Otherwise, it is set to VOIDmode.

   *PVOLATILEP is set to 1 if the any expression encountered is volatile;
   otherwise it is not changed.

   *PUNSIGNEDP is set to the signedness of the field.

   *PMASK is set to the mask used.  This is either contained in a
   BIT_AND_EXPR or derived from the width of the field.

   *PAND_MASK is set to the mask found in a BIT_AND_EXPR, if any.

   Return 0 if this is not a component reference or is one that we can't
   do anything with.  */

static tree
decode_field_reference (tree exp, HOST_WIDE_INT *pbitsize,
			HOST_WIDE_INT *pbitpos, enum machine_mode *pmode,
			int *punsignedp, int *pvolatilep,
			tree *pmask, tree *pand_mask)
{
  tree outer_type = 0;
  tree and_mask = 0;
  tree mask, inner, offset;
  tree unsigned_type;
  unsigned int precision;

  /* All the optimizations using this function assume integer fields.
     There are problems with FP fields since the type_for_size call
     below can fail for, e.g., XFmode.  */
  if (! INTEGRAL_TYPE_P (TREE_TYPE (exp)))
    return 0;

  /* We are interested in the bare arrangement of bits, so strip everything
     that doesn't affect the machine mode.  However, record the type of the
     outermost expression if it may matter below.  */
  if (TREE_CODE (exp) == NOP_EXPR
      || TREE_CODE (exp) == CONVERT_EXPR
      || TREE_CODE (exp) == NON_LVALUE_EXPR)
    outer_type = TREE_TYPE (exp);
  STRIP_NOPS (exp);

  if (TREE_CODE (exp) == BIT_AND_EXPR)
    {
      and_mask = TREE_OPERAND (exp, 1);
      exp = TREE_OPERAND (exp, 0);
      STRIP_NOPS (exp); STRIP_NOPS (and_mask);
      if (TREE_CODE (and_mask) != INTEGER_CST)
	return 0;
    }

  inner = get_inner_reference (exp, pbitsize, pbitpos, &offset, pmode,
			       punsignedp, pvolatilep, false);
  if ((inner == exp && and_mask == 0)
      || *pbitsize < 0 || offset != 0
      || TREE_CODE (inner) == PLACEHOLDER_EXPR)
    return 0;

  /* If the number of bits in the reference is the same as the bitsize of
     the outer type, then the outer type gives the signedness. Otherwise
     (in case of a small bitfield) the signedness is unchanged.  */
  if (outer_type && *pbitsize == TYPE_PRECISION (outer_type))
    *punsignedp = TYPE_UNSIGNED (outer_type);

  /* Compute the mask to access the bitfield.  */
  unsigned_type = lang_hooks.types.type_for_size (*pbitsize, 1);
  precision = TYPE_PRECISION (unsigned_type);

  mask = build_int_cst (unsigned_type, -1);
  mask = force_fit_type (mask, 0, false, false);

  mask = const_binop (LSHIFT_EXPR, mask, size_int (precision - *pbitsize), 0);
  mask = const_binop (RSHIFT_EXPR, mask, size_int (precision - *pbitsize), 0);

  /* Merge it with the mask we found in the BIT_AND_EXPR, if any.  */
  if (and_mask != 0)
    mask = fold_build2 (BIT_AND_EXPR, unsigned_type,
			fold_convert (unsigned_type, and_mask), mask);

  *pmask = mask;
  *pand_mask = and_mask;
  return inner;
}

/* Return nonzero if MASK represents a mask of SIZE ones in the low-order
   bit positions.  */

static int
all_ones_mask_p (tree mask, int size)
{
  tree type = TREE_TYPE (mask);
  unsigned int precision = TYPE_PRECISION (type);
  tree tmask;

  tmask = build_int_cst (lang_hooks.types.signed_type (type), -1);
  tmask = force_fit_type (tmask, 0, false, false);

  return
    tree_int_cst_equal (mask,
			const_binop (RSHIFT_EXPR,
				     const_binop (LSHIFT_EXPR, tmask,
						  size_int (precision - size),
						  0),
				     size_int (precision - size), 0));
}

/* Subroutine for fold: determine if VAL is the INTEGER_CONST that
   represents the sign bit of EXP's type.  If EXP represents a sign
   or zero extension, also test VAL against the unextended type.
   The return value is the (sub)expression whose sign bit is VAL,
   or NULL_TREE otherwise.  */

static tree
sign_bit_p (tree exp, tree val)
{
  unsigned HOST_WIDE_INT mask_lo, lo;
  HOST_WIDE_INT mask_hi, hi;
  int width;
  tree t;

  /* Tree EXP must have an integral type.  */
  t = TREE_TYPE (exp);
  if (! INTEGRAL_TYPE_P (t))
    return NULL_TREE;

  /* Tree VAL must be an integer constant.  */
  if (TREE_CODE (val) != INTEGER_CST
      || TREE_CONSTANT_OVERFLOW (val))
    return NULL_TREE;

  width = TYPE_PRECISION (t);
  if (width > HOST_BITS_PER_WIDE_INT)
    {
      hi = (unsigned HOST_WIDE_INT) 1 << (width - HOST_BITS_PER_WIDE_INT - 1);
      lo = 0;

      mask_hi = ((unsigned HOST_WIDE_INT) -1
		 >> (2 * HOST_BITS_PER_WIDE_INT - width));
      mask_lo = -1;
    }
  else
    {
      hi = 0;
      lo = (unsigned HOST_WIDE_INT) 1 << (width - 1);

      mask_hi = 0;
      mask_lo = ((unsigned HOST_WIDE_INT) -1
		 >> (HOST_BITS_PER_WIDE_INT - width));
    }

  /* We mask off those bits beyond TREE_TYPE (exp) so that we can
     treat VAL as if it were unsigned.  */
  if ((TREE_INT_CST_HIGH (val) & mask_hi) == hi
      && (TREE_INT_CST_LOW (val) & mask_lo) == lo)
    return exp;

  /* Handle extension from a narrower type.  */
  if (TREE_CODE (exp) == NOP_EXPR
      && TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (exp, 0))) < width)
    return sign_bit_p (TREE_OPERAND (exp, 0), val);

  return NULL_TREE;
}

/* Subroutine for fold_truthop: determine if an operand is simple enough
   to be evaluated unconditionally.  */

static int
simple_operand_p (tree exp)
{
  /* Strip any conversions that don't change the machine mode.  */
  STRIP_NOPS (exp);

  return (CONSTANT_CLASS_P (exp)
	  || TREE_CODE (exp) == SSA_NAME
	  || (DECL_P (exp)
	      && ! TREE_ADDRESSABLE (exp)
	      && ! TREE_THIS_VOLATILE (exp)
	      && ! DECL_NONLOCAL (exp)
	      /* Don't regard global variables as simple.  They may be
		 allocated in ways unknown to the compiler (shared memory,
		 #pragma weak, etc).  */
	      && ! TREE_PUBLIC (exp)
	      && ! DECL_EXTERNAL (exp)
	      /* Loading a static variable is unduly expensive, but global
		 registers aren't expensive.  */
	      && (! TREE_STATIC (exp) || DECL_REGISTER (exp))));
}

/* The following functions are subroutines to fold_range_test and allow it to
   try to change a logical combination of comparisons into a range test.

   For example, both
	X == 2 || X == 3 || X == 4 || X == 5
   and
	X >= 2 && X <= 5
   are converted to
	(unsigned) (X - 2) <= 3

   We describe each set of comparisons as being either inside or outside
   a range, using a variable named like IN_P, and then describe the
   range with a lower and upper bound.  If one of the bounds is omitted,
   it represents either the highest or lowest value of the type.

   In the comments below, we represent a range by two numbers in brackets
   preceded by a "+" to designate being inside that range, or a "-" to
   designate being outside that range, so the condition can be inverted by
   flipping the prefix.  An omitted bound is represented by a "-".  For
   example, "- [-, 10]" means being outside the range starting at the lowest
   possible value and ending at 10, in other words, being greater than 10.
   The range "+ [-, -]" is always true and hence the range "- [-, -]" is
   always false.

   We set up things so that the missing bounds are handled in a consistent
   manner so neither a missing bound nor "true" and "false" need to be
   handled using a special case.  */

/* Return the result of applying CODE to ARG0 and ARG1, but handle the case
   of ARG0 and/or ARG1 being omitted, meaning an unlimited range. UPPER0_P
   and UPPER1_P are nonzero if the respective argument is an upper bound
   and zero for a lower.  TYPE, if nonzero, is the type of the result; it
   must be specified for a comparison.  ARG1 will be converted to ARG0's
   type if both are specified.  */

static tree
range_binop (enum tree_code code, tree type, tree arg0, int upper0_p,
	     tree arg1, int upper1_p)
{
  tree tem;
  int result;
  int sgn0, sgn1;

  /* If neither arg represents infinity, do the normal operation.
     Else, if not a comparison, return infinity.  Else handle the special
     comparison rules. Note that most of the cases below won't occur, but
     are handled for consistency.  */

  if (arg0 != 0 && arg1 != 0)
    {
      tem = fold_build2 (code, type != 0 ? type : TREE_TYPE (arg0),
			 arg0, fold_convert (TREE_TYPE (arg0), arg1));
      STRIP_NOPS (tem);
      return TREE_CODE (tem) == INTEGER_CST ? tem : 0;
    }

  if (TREE_CODE_CLASS (code) != tcc_comparison)
    return 0;

  /* Set SGN[01] to -1 if ARG[01] is a lower bound, 1 for upper, and 0
     for neither.  In real maths, we cannot assume open ended ranges are
     the same. But, this is computer arithmetic, where numbers are finite.
     We can therefore make the transformation of any unbounded range with
     the value Z, Z being greater than any representable number. This permits
     us to treat unbounded ranges as equal.  */
  sgn0 = arg0 != 0 ? 0 : (upper0_p ? 1 : -1);
  sgn1 = arg1 != 0 ? 0 : (upper1_p ? 1 : -1);
  switch (code)
    {
    case EQ_EXPR:
      result = sgn0 == sgn1;
      break;
    case NE_EXPR:
      result = sgn0 != sgn1;
      break;
    case LT_EXPR:
      result = sgn0 < sgn1;
      break;
    case LE_EXPR:
      result = sgn0 <= sgn1;
      break;
    case GT_EXPR:
      result = sgn0 > sgn1;
      break;
    case GE_EXPR:
      result = sgn0 >= sgn1;
      break;
    default:
      gcc_unreachable ();
    }

  return constant_boolean_node (result, type);
}

/* Given EXP, a logical expression, set the range it is testing into
   variables denoted by PIN_P, PLOW, and PHIGH.  Return the expression
   actually being tested.  *PLOW and *PHIGH will be made of the same
   type as the returned expression.  If EXP is not a comparison, we
   will most likely not be returning a useful value and range.  Set
   *STRICT_OVERFLOW_P to true if the return value is only valid
   because signed overflow is undefined; otherwise, do not change
   *STRICT_OVERFLOW_P.  */

static tree
make_range (tree exp, int *pin_p, tree *plow, tree *phigh,
	    bool *strict_overflow_p)
{
  enum tree_code code;
  tree arg0 = NULL_TREE, arg1 = NULL_TREE;
  tree exp_type = NULL_TREE, arg0_type = NULL_TREE;
  int in_p, n_in_p;
  tree low, high, n_low, n_high;

  /* Start with simply saying "EXP != 0" and then look at the code of EXP
     and see if we can refine the range.  Some of the cases below may not
     happen, but it doesn't seem worth worrying about this.  We "continue"
     the outer loop when we've changed something; otherwise we "break"
     the switch, which will "break" the while.  */

  in_p = 0;
  low = high = build_int_cst (TREE_TYPE (exp), 0);

  while (1)
    {
      code = TREE_CODE (exp);
      exp_type = TREE_TYPE (exp);

      if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code)))
	{
	  if (TREE_CODE_LENGTH (code) > 0)
	    arg0 = TREE_OPERAND (exp, 0);
	  if (TREE_CODE_CLASS (code) == tcc_comparison
	      || TREE_CODE_CLASS (code) == tcc_unary
	      || TREE_CODE_CLASS (code) == tcc_binary)
	    arg0_type = TREE_TYPE (arg0);
	  if (TREE_CODE_CLASS (code) == tcc_binary
	      || TREE_CODE_CLASS (code) == tcc_comparison
	      || (TREE_CODE_CLASS (code) == tcc_expression
		  && TREE_CODE_LENGTH (code) > 1))
	    arg1 = TREE_OPERAND (exp, 1);
	}

      switch (code)
	{
	case TRUTH_NOT_EXPR:
	  in_p = ! in_p, exp = arg0;
	  continue;

	case EQ_EXPR: case NE_EXPR:
	case LT_EXPR: case LE_EXPR: case GE_EXPR: case GT_EXPR:
	  /* We can only do something if the range is testing for zero
	     and if the second operand is an integer constant.  Note that
	     saying something is "in" the range we make is done by
	     complementing IN_P since it will set in the initial case of
	     being not equal to zero; "out" is leaving it alone.  */
	  if (low == 0 || high == 0
	      || ! integer_zerop (low) || ! integer_zerop (high)
	      || TREE_CODE (arg1) != INTEGER_CST)
	    break;

	  switch (code)
	    {
	    case NE_EXPR:  /* - [c, c]  */
	      low = high = arg1;
	      break;
	    case EQ_EXPR:  /* + [c, c]  */
	      in_p = ! in_p, low = high = arg1;
	      break;
	    case GT_EXPR:  /* - [-, c] */
	      low = 0, high = arg1;
	      break;
	    case GE_EXPR:  /* + [c, -] */
	      in_p = ! in_p, low = arg1, high = 0;
	      break;
	    case LT_EXPR:  /* - [c, -] */
	      low = arg1, high = 0;
	      break;
	    case LE_EXPR:  /* + [-, c] */
	      in_p = ! in_p, low = 0, high = arg1;
	      break;
	    default:
	      gcc_unreachable ();
	    }

	  /* If this is an unsigned comparison, we also know that EXP is
	     greater than or equal to zero.  We base the range tests we make
	     on that fact, so we record it here so we can parse existing
	     range tests.  We test arg0_type since often the return type
	     of, e.g. EQ_EXPR, is boolean.  */
	  if (TYPE_UNSIGNED (arg0_type) && (low == 0 || high == 0))
	    {
	      if (! merge_ranges (&n_in_p, &n_low, &n_high,
				  in_p, low, high, 1,
				  build_int_cst (arg0_type, 0),
				  NULL_TREE))
		break;

	      in_p = n_in_p, low = n_low, high = n_high;

	      /* If the high bound is missing, but we have a nonzero low
		 bound, reverse the range so it goes from zero to the low bound
		 minus 1.  */
	      if (high == 0 && low && ! integer_zerop (low))
		{
		  in_p = ! in_p;
		  high = range_binop (MINUS_EXPR, NULL_TREE, low, 0,
				      integer_one_node, 0);
		  low = build_int_cst (arg0_type, 0);
		}
	    }

	  exp = arg0;
	  continue;

	case NEGATE_EXPR:
	  /* (-x) IN [a,b] -> x in [-b, -a]  */
	  n_low = range_binop (MINUS_EXPR, exp_type,
			       build_int_cst (exp_type, 0),
			       0, high, 1);
	  n_high = range_binop (MINUS_EXPR, exp_type,
				build_int_cst (exp_type, 0),
				0, low, 0);
	  low = n_low, high = n_high;
	  exp = arg0;
	  continue;

	case BIT_NOT_EXPR:
	  /* ~ X -> -X - 1  */
	  exp = build2 (MINUS_EXPR, exp_type, negate_expr (arg0),
			build_int_cst (exp_type, 1));
	  continue;

	case PLUS_EXPR:  case MINUS_EXPR:
	  if (TREE_CODE (arg1) != INTEGER_CST)
	    break;

	  /* If flag_wrapv and ARG0_TYPE is signed, then we cannot
	     move a constant to the other side.  */
	  if (!TYPE_UNSIGNED (arg0_type)
	      && !TYPE_OVERFLOW_UNDEFINED (arg0_type))
	    break;

	  /* If EXP is signed, any overflow in the computation is undefined,
	     so we don't worry about it so long as our computations on
	     the bounds don't overflow.  For unsigned, overflow is defined
	     and this is exactly the right thing.  */
	  n_low = range_binop (code == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR,
			       arg0_type, low, 0, arg1, 0);
	  n_high = range_binop (code == MINUS_EXPR ? PLUS_EXPR : MINUS_EXPR,
				arg0_type, high, 1, arg1, 0);
	  if ((n_low != 0 && TREE_OVERFLOW (n_low))
	      || (n_high != 0 && TREE_OVERFLOW (n_high)))
	    break;

	  if (TYPE_OVERFLOW_UNDEFINED (arg0_type))
	    *strict_overflow_p = true;

	  /* Check for an unsigned range which has wrapped around the maximum
	     value thus making n_high < n_low, and normalize it.  */
	  if (n_low && n_high && tree_int_cst_lt (n_high, n_low))
	    {
	      low = range_binop (PLUS_EXPR, arg0_type, n_high, 0,
				 integer_one_node, 0);
	      high = range_binop (MINUS_EXPR, arg0_type, n_low, 0,
				  integer_one_node, 0);

	      /* If the range is of the form +/- [ x+1, x ], we won't
		 be able to normalize it.  But then, it represents the
		 whole range or the empty set, so make it
		 +/- [ -, - ].  */
	      if (tree_int_cst_equal (n_low, low)
		  && tree_int_cst_equal (n_high, high))
		low = high = 0;
	      else
		in_p = ! in_p;
	    }
	  else
	    low = n_low, high = n_high;

	  exp = arg0;
	  continue;

	case NOP_EXPR:  case NON_LVALUE_EXPR:  case CONVERT_EXPR:
	  if (TYPE_PRECISION (arg0_type) > TYPE_PRECISION (exp_type))
	    break;

	  if (! INTEGRAL_TYPE_P (arg0_type)
	      || (low != 0 && ! int_fits_type_p (low, arg0_type))
	      || (high != 0 && ! int_fits_type_p (high, arg0_type)))
	    break;

	  n_low = low, n_high = high;

	  if (n_low != 0)
	    n_low = fold_convert (arg0_type, n_low);

	  if (n_high != 0)
	    n_high = fold_convert (arg0_type, n_high);


	  /* If we're converting arg0 from an unsigned type, to exp,
	     a signed type,  we will be doing the comparison as unsigned.
	     The tests above have already verified that LOW and HIGH
	     are both positive.

	     So we have to ensure that we will handle large unsigned
	     values the same way that the current signed bounds treat
	     negative values.  */

	  if (!TYPE_UNSIGNED (exp_type) && TYPE_UNSIGNED (arg0_type))
	    {
	      tree high_positive;
	      tree equiv_type = lang_hooks.types.type_for_mode
		(TYPE_MODE (arg0_type), 1);

	      /* A range without an upper bound is, naturally, unbounded.
		 Since convert would have cropped a very large value, use
		 the max value for the destination type.  */
	      high_positive
		= TYPE_MAX_VALUE (equiv_type) ? TYPE_MAX_VALUE (equiv_type)
		: TYPE_MAX_VALUE (arg0_type);

	      if (TYPE_PRECISION (exp_type) == TYPE_PRECISION (arg0_type))
		high_positive = fold_build2 (RSHIFT_EXPR, arg0_type,
					     fold_convert (arg0_type,
							   high_positive),
					     fold_convert (arg0_type,
							   integer_one_node));

	      /* If the low bound is specified, "and" the range with the
		 range for which the original unsigned value will be
		 positive.  */
	      if (low != 0)
		{
		  if (! merge_ranges (&n_in_p, &n_low, &n_high,
				      1, n_low, n_high, 1,
				      fold_convert (arg0_type,
						    integer_zero_node),
				      high_positive))
		    break;

		  in_p = (n_in_p == in_p);
		}
	      else
		{
		  /* Otherwise, "or" the range with the range of the input
		     that will be interpreted as negative.  */
		  if (! merge_ranges (&n_in_p, &n_low, &n_high,
				      0, n_low, n_high, 1,
				      fold_convert (arg0_type,
						    integer_zero_node),
				      high_positive))
		    break;

		  in_p = (in_p != n_in_p);
		}
	    }

	  exp = arg0;
	  low = n_low, high = n_high;
	  continue;

	default:
	  break;
	}

      break;
    }

  /* If EXP is a constant, we can evaluate whether this is true or false.  */
  if (TREE_CODE (exp) == INTEGER_CST)
    {
      in_p = in_p == (integer_onep (range_binop (GE_EXPR, integer_type_node,
						 exp, 0, low, 0))
		      && integer_onep (range_binop (LE_EXPR, integer_type_node,
						    exp, 1, high, 1)));
      low = high = 0;
      exp = 0;
    }

  *pin_p = in_p, *plow = low, *phigh = high;
  return exp;
}

/* Given a range, LOW, HIGH, and IN_P, an expression, EXP, and a result
   type, TYPE, return an expression to test if EXP is in (or out of, depending
   on IN_P) the range.  Return 0 if the test couldn't be created.  */

static tree
build_range_check (tree type, tree exp, int in_p, tree low, tree high)
{
  tree etype = TREE_TYPE (exp);
  tree value;

#ifdef HAVE_canonicalize_funcptr_for_compare
  /* Disable this optimization for function pointer expressions
     on targets that require function pointer canonicalization.  */
  if (HAVE_canonicalize_funcptr_for_compare
      && TREE_CODE (etype) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (etype)) == FUNCTION_TYPE)
    return NULL_TREE;
#endif

  if (! in_p)
    {
      value = build_range_check (type, exp, 1, low, high);
      if (value != 0)
        return invert_truthvalue (value);

      return 0;
    }

  if (low == 0 && high == 0)
    return build_int_cst (type, 1);

  if (low == 0)
    return fold_build2 (LE_EXPR, type, exp,
			fold_convert (etype, high));

  if (high == 0)
    return fold_build2 (GE_EXPR, type, exp,
			fold_convert (etype, low));

  if (operand_equal_p (low, high, 0))
    return fold_build2 (EQ_EXPR, type, exp,
			fold_convert (etype, low));

  if (integer_zerop (low))
    {
      if (! TYPE_UNSIGNED (etype))
	{
	  etype = lang_hooks.types.unsigned_type (etype);
	  high = fold_convert (etype, high);
	  exp = fold_convert (etype, exp);
	}
      return build_range_check (type, exp, 1, 0, high);
    }

  /* Optimize (c>=1) && (c<=127) into (signed char)c > 0.  */
  if (integer_onep (low) && TREE_CODE (high) == INTEGER_CST)
    {
      unsigned HOST_WIDE_INT lo;
      HOST_WIDE_INT hi;
      int prec;

      prec = TYPE_PRECISION (etype);
      if (prec <= HOST_BITS_PER_WIDE_INT)
	{
	  hi = 0;
	  lo = ((unsigned HOST_WIDE_INT) 1 << (prec - 1)) - 1;
	}
      else
	{
	  hi = ((HOST_WIDE_INT) 1 << (prec - HOST_BITS_PER_WIDE_INT - 1)) - 1;
	  lo = (unsigned HOST_WIDE_INT) -1;
	}

      if (TREE_INT_CST_HIGH (high) == hi && TREE_INT_CST_LOW (high) == lo)
	{
	  if (TYPE_UNSIGNED (etype))
	    {
	      etype = lang_hooks.types.signed_type (etype);
	      exp = fold_convert (etype, exp);
	    }
	  return fold_build2 (GT_EXPR, type, exp,
			      build_int_cst (etype, 0));
	}
    }

  /* Optimize (c>=low) && (c<=high) into (c-low>=0) && (c-low<=high-low).
     This requires wrap-around arithmetics for the type of the expression.  */
  switch (TREE_CODE (etype))
    {
    case INTEGER_TYPE:
      /* There is no requirement that LOW be within the range of ETYPE
	 if the latter is a subtype.  It must, however, be within the base
	 type of ETYPE.  So be sure we do the subtraction in that type.  */
      if (TREE_TYPE (etype))
	etype = TREE_TYPE (etype);
      break;

    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      etype = lang_hooks.types.type_for_size (TYPE_PRECISION (etype),
					      TYPE_UNSIGNED (etype));
      break;

    default:
      break;
    }

  /* If we don't have wrap-around arithmetics upfront, try to force it.  */
  if (TREE_CODE (etype) == INTEGER_TYPE
      && !TYPE_OVERFLOW_WRAPS (etype))
    {
      tree utype, minv, maxv;

      /* Check if (unsigned) INT_MAX + 1 == (unsigned) INT_MIN
	 for the type in question, as we rely on this here.  */
      utype = lang_hooks.types.unsigned_type (etype);
      maxv = fold_convert (utype, TYPE_MAX_VALUE (etype));
      maxv = range_binop (PLUS_EXPR, NULL_TREE, maxv, 1,
			  integer_one_node, 1);
      minv = fold_convert (utype, TYPE_MIN_VALUE (etype));

      if (integer_zerop (range_binop (NE_EXPR, integer_type_node,
				      minv, 1, maxv, 1)))
	etype = utype;
      else
	return 0;
    }

  high = fold_convert (etype, high);
  low = fold_convert (etype, low);
  exp = fold_convert (etype, exp);

  value = const_binop (MINUS_EXPR, high, low, 0);

  if (value != 0 && !TREE_OVERFLOW (value))
    return build_range_check (type,
			      fold_build2 (MINUS_EXPR, etype, exp, low),
			      1, build_int_cst (etype, 0), value);

  return 0;
}

/* Return the predecessor of VAL in its type, handling the infinite case.  */

static tree
range_predecessor (tree val)
{
  tree type = TREE_TYPE (val);

  if (INTEGRAL_TYPE_P (type)
      && operand_equal_p (val, TYPE_MIN_VALUE (type), 0))
    return 0;
  else
    return range_binop (MINUS_EXPR, NULL_TREE, val, 0, integer_one_node, 0);
}

/* Return the successor of VAL in its type, handling the infinite case.  */

static tree
range_successor (tree val)
{
  tree type = TREE_TYPE (val);

  if (INTEGRAL_TYPE_P (type)
      && operand_equal_p (val, TYPE_MAX_VALUE (type), 0))
    return 0;
  else
    return range_binop (PLUS_EXPR, NULL_TREE, val, 0, integer_one_node, 0);
}

/* Given two ranges, see if we can merge them into one.  Return 1 if we
   can, 0 if we can't.  Set the output range into the specified parameters.  */

static int
merge_ranges (int *pin_p, tree *plow, tree *phigh, int in0_p, tree low0,
	      tree high0, int in1_p, tree low1, tree high1)
{
  int no_overlap;
  int subset;
  int temp;
  tree tem;
  int in_p;
  tree low, high;
  int lowequal = ((low0 == 0 && low1 == 0)
		  || integer_onep (range_binop (EQ_EXPR, integer_type_node,
						low0, 0, low1, 0)));
  int highequal = ((high0 == 0 && high1 == 0)
		   || integer_onep (range_binop (EQ_EXPR, integer_type_node,
						 high0, 1, high1, 1)));

  /* Make range 0 be the range that starts first, or ends last if they
     start at the same value.  Swap them if it isn't.  */
  if (integer_onep (range_binop (GT_EXPR, integer_type_node,
				 low0, 0, low1, 0))
      || (lowequal
	  && integer_onep (range_binop (GT_EXPR, integer_type_node,
					high1, 1, high0, 1))))
    {
      temp = in0_p, in0_p = in1_p, in1_p = temp;
      tem = low0, low0 = low1, low1 = tem;
      tem = high0, high0 = high1, high1 = tem;
    }

  /* Now flag two cases, whether the ranges are disjoint or whether the
     second range is totally subsumed in the first.  Note that the tests
     below are simplified by the ones above.  */
  no_overlap = integer_onep (range_binop (LT_EXPR, integer_type_node,
					  high0, 1, low1, 0));
  subset = integer_onep (range_binop (LE_EXPR, integer_type_node,
				      high1, 1, high0, 1));

  /* We now have four cases, depending on whether we are including or
     excluding the two ranges.  */
  if (in0_p && in1_p)
    {
      /* If they don't overlap, the result is false.  If the second range
	 is a subset it is the result.  Otherwise, the range is from the start
	 of the second to the end of the first.  */
      if (no_overlap)
	in_p = 0, low = high = 0;
      else if (subset)
	in_p = 1, low = low1, high = high1;
      else
	in_p = 1, low = low1, high = high0;
    }

  else if (in0_p && ! in1_p)
    {
      /* If they don't overlap, the result is the first range.  If they are
	 equal, the result is false.  If the second range is a subset of the
	 first, and the ranges begin at the same place, we go from just after
	 the end of the second range to the end of the first.  If the second
	 range is not a subset of the first, or if it is a subset and both
	 ranges end at the same place, the range starts at the start of the
	 first range and ends just before the second range.
	 Otherwise, we can't describe this as a single range.  */
      if (no_overlap)
	in_p = 1, low = low0, high = high0;
      else if (lowequal && highequal)
	in_p = 0, low = high = 0;
      else if (subset && lowequal)
	{
	  low = range_successor (high1);
	  high = high0;
	  in_p = 1;
	  if (low == 0)
	    {
	      /* We are in the weird situation where high0 > high1 but
		 high1 has no successor.  Punt.  */
	      return 0;
	    }
	}
      else if (! subset || highequal)
	{
	  low = low0;
	  high = range_predecessor (low1);
	  in_p = 1;
	  if (high == 0)
	    {
	      /* low0 < low1 but low1 has no predecessor.  Punt.  */
	      return 0;
	    }
	}
      else
	return 0;
    }

  else if (! in0_p && in1_p)
    {
      /* If they don't overlap, the result is the second range.  If the second
	 is a subset of the first, the result is false.  Otherwise,
	 the range starts just after the first range and ends at the
	 end of the second.  */
      if (no_overlap)
	in_p = 1, low = low1, high = high1;
      else if (subset || highequal)
	in_p = 0, low = high = 0;
      else
	{
	  low = range_successor (high0);
	  high = high1;
	  in_p = 1;
	  if (low == 0)
	    {
	      /* high1 > high0 but high0 has no successor.  Punt.  */
	      return 0;
	    }
	}
    }

  else
    {
      /* The case where we are excluding both ranges.  Here the complex case
	 is if they don't overlap.  In that case, the only time we have a
	 range is if they are adjacent.  If the second is a subset of the
	 first, the result is the first.  Otherwise, the range to exclude
	 starts at the beginning of the first range and ends at the end of the
	 second.  */
      if (no_overlap)
	{
	  if (integer_onep (range_binop (EQ_EXPR, integer_type_node,
					 range_successor (high0),
					 1, low1, 0)))
	    in_p = 0, low = low0, high = high1;
	  else
	    {
	      /* Canonicalize - [min, x] into - [-, x].  */
	      if (low0 && TREE_CODE (low0) == INTEGER_CST)
		switch (TREE_CODE (TREE_TYPE (low0)))
		  {
		  case ENUMERAL_TYPE:
		    if (TYPE_PRECISION (TREE_TYPE (low0))
			!= GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (low0))))
		      break;
		    /* FALLTHROUGH */
		  case INTEGER_TYPE:
		    if (tree_int_cst_equal (low0,
					    TYPE_MIN_VALUE (TREE_TYPE (low0))))
		      low0 = 0;
		    break;
		  case POINTER_TYPE:
		    if (TYPE_UNSIGNED (TREE_TYPE (low0))
			&& integer_zerop (low0))
		      low0 = 0;
		    break;
		  default:
		    break;
		  }

	      /* Canonicalize - [x, max] into - [x, -].  */
	      if (high1 && TREE_CODE (high1) == INTEGER_CST)
		switch (TREE_CODE (TREE_TYPE (high1)))
		  {
		  case ENUMERAL_TYPE:
		    if (TYPE_PRECISION (TREE_TYPE (high1))
			!= GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (high1))))
		      break;
		    /* FALLTHROUGH */
		  case INTEGER_TYPE:
		    if (tree_int_cst_equal (high1,
					    TYPE_MAX_VALUE (TREE_TYPE (high1))))
		      high1 = 0;
		    break;
		  case POINTER_TYPE:
		    if (TYPE_UNSIGNED (TREE_TYPE (high1))
			&& integer_zerop (range_binop (PLUS_EXPR, NULL_TREE,
						       high1, 1,
						       integer_one_node, 1)))
		      high1 = 0;
		    break;
		  default:
		    break;
		  }

	      /* The ranges might be also adjacent between the maximum and
	         minimum values of the given type.  For
	         - [{min,-}, x] and - [y, {max,-}] ranges where x + 1 < y
	         return + [x + 1, y - 1].  */
	      if (low0 == 0 && high1 == 0)
	        {
		  low = range_successor (high0);
		  high = range_predecessor (low1);
		  if (low == 0 || high == 0)
		    return 0;

		  in_p = 1;
		}
	      else
		return 0;
	    }
	}
      else if (subset)
	in_p = 0, low = low0, high = high0;
      else
	in_p = 0, low = low0, high = high1;
    }

  *pin_p = in_p, *plow = low, *phigh = high;
  return 1;
}


/* Subroutine of fold, looking inside expressions of the form
   A op B ? A : C, where ARG0, ARG1 and ARG2 are the three operands
   of the COND_EXPR.  This function is being used also to optimize
   A op B ? C : A, by reversing the comparison first.

   Return a folded expression whose code is not a COND_EXPR
   anymore, or NULL_TREE if no folding opportunity is found.  */

static tree
fold_cond_expr_with_comparison (tree type, tree arg0, tree arg1, tree arg2)
{
  enum tree_code comp_code = TREE_CODE (arg0);
  tree arg00 = TREE_OPERAND (arg0, 0);
  tree arg01 = TREE_OPERAND (arg0, 1);
  tree arg1_type = TREE_TYPE (arg1);
  tree tem;

  STRIP_NOPS (arg1);
  STRIP_NOPS (arg2);

  /* If we have A op 0 ? A : -A, consider applying the following
     transformations:

     A == 0? A : -A    same as -A
     A != 0? A : -A    same as A
     A >= 0? A : -A    same as abs (A)
     A > 0?  A : -A    same as abs (A)
     A <= 0? A : -A    same as -abs (A)
     A < 0?  A : -A    same as -abs (A)

     None of these transformations work for modes with signed
     zeros.  If A is +/-0, the first two transformations will
     change the sign of the result (from +0 to -0, or vice
     versa).  The last four will fix the sign of the result,
     even though the original expressions could be positive or
     negative, depending on the sign of A.

     Note that all these transformations are correct if A is
     NaN, since the two alternatives (A and -A) are also NaNs.  */
  if ((FLOAT_TYPE_P (TREE_TYPE (arg01))
       ? real_zerop (arg01)
       : integer_zerop (arg01))
      && ((TREE_CODE (arg2) == NEGATE_EXPR
	   && operand_equal_p (TREE_OPERAND (arg2, 0), arg1, 0))
	     /* In the case that A is of the form X-Y, '-A' (arg2) may
	        have already been folded to Y-X, check for that. */
	  || (TREE_CODE (arg1) == MINUS_EXPR
	      && TREE_CODE (arg2) == MINUS_EXPR
	      && operand_equal_p (TREE_OPERAND (arg1, 0),
				  TREE_OPERAND (arg2, 1), 0)
	      && operand_equal_p (TREE_OPERAND (arg1, 1),
				  TREE_OPERAND (arg2, 0), 0))))
    switch (comp_code)
      {
      case EQ_EXPR:
      case UNEQ_EXPR:
	tem = fold_convert (arg1_type, arg1);
	return pedantic_non_lvalue (fold_convert (type, negate_expr (tem)));
      case NE_EXPR:
      case LTGT_EXPR:
	return pedantic_non_lvalue (fold_convert (type, arg1));
      case UNGE_EXPR:
      case UNGT_EXPR:
	if (flag_trapping_math)
	  break;
	/* Fall through.  */
      case GE_EXPR:
      case GT_EXPR:
	if (TYPE_UNSIGNED (TREE_TYPE (arg1)))
	  arg1 = fold_convert (lang_hooks.types.signed_type
			       (TREE_TYPE (arg1)), arg1);
	tem = fold_build1 (ABS_EXPR, TREE_TYPE (arg1), arg1);
	return pedantic_non_lvalue (fold_convert (type, tem));
      case UNLE_EXPR:
      case UNLT_EXPR:
	if (flag_trapping_math)
	  break;
      case LE_EXPR:
      case LT_EXPR:
	if (TYPE_UNSIGNED (TREE_TYPE (arg1)))
	  arg1 = fold_convert (lang_hooks.types.signed_type
			       (TREE_TYPE (arg1)), arg1);
	tem = fold_build1 (ABS_EXPR, TREE_TYPE (arg1), arg1);
	return negate_expr (fold_convert (type, tem));
      default:
	gcc_assert (TREE_CODE_CLASS (comp_code) == tcc_comparison);
	break;
      }

  /* A != 0 ? A : 0 is simply A, unless A is -0.  Likewise
     A == 0 ? A : 0 is always 0 unless A is -0.  Note that
     both transformations are correct when A is NaN: A != 0
     is then true, and A == 0 is false.  */

  if (integer_zerop (arg01) && integer_zerop (arg2))
    {
      if (comp_code == NE_EXPR)
	return pedantic_non_lvalue (fold_convert (type, arg1));
      else if (comp_code == EQ_EXPR)
	return build_int_cst (type, 0);
    }

  /* Try some transformations of A op B ? A : B.

     A == B? A : B    same as B
     A != B? A : B    same as A
     A >= B? A : B    same as max (A, B)
     A > B?  A : B    same as max (B, A)
     A <= B? A : B    same as min (A, B)
     A < B?  A : B    same as min (B, A)

     As above, these transformations don't work in the presence
     of signed zeros.  For example, if A and B are zeros of
     opposite sign, the first two transformations will change
     the sign of the result.  In the last four, the original
     expressions give different results for (A=+0, B=-0) and
     (A=-0, B=+0), but the transformed expressions do not.

     The first two transformations are correct if either A or B
     is a NaN.  In the first transformation, the condition will
     be false, and B will indeed be chosen.  In the case of the
     second transformation, the condition A != B will be true,
     and A will be chosen.

     The conversions to max() and min() are not correct if B is
     a number and A is not.  The conditions in the original
     expressions will be false, so all four give B.  The min()
     and max() versions would give a NaN instead.  */
  if (operand_equal_for_comparison_p (arg01, arg2, arg00)
      /* Avoid these transformations if the COND_EXPR may be used
	 as an lvalue in the C++ front-end.  PR c++/19199.  */
      && (in_gimple_form
	  || (strcmp (lang_hooks.name, "GNU C++") != 0
	      && strcmp (lang_hooks.name, "GNU Objective-C++") != 0)
	  || ! maybe_lvalue_p (arg1)
	  || ! maybe_lvalue_p (arg2)))
    {
      tree comp_op0 = arg00;
      tree comp_op1 = arg01;
      tree comp_type = TREE_TYPE (comp_op0);

      /* Avoid adding NOP_EXPRs in case this is an lvalue.  */
      if (TYPE_MAIN_VARIANT (comp_type) == TYPE_MAIN_VARIANT (type))
	{
	  comp_type = type;
	  comp_op0 = arg1;
	  comp_op1 = arg2;
	}

      switch (comp_code)
	{
	case EQ_EXPR:
	  return pedantic_non_lvalue (fold_convert (type, arg2));
	case NE_EXPR:
	  return pedantic_non_lvalue (fold_convert (type, arg1));
	case LE_EXPR:
	case LT_EXPR:
	case UNLE_EXPR:
	case UNLT_EXPR:
	  /* In C++ a ?: expression can be an lvalue, so put the
	     operand which will be used if they are equal first
	     so that we can convert this back to the
	     corresponding COND_EXPR.  */
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1))))
	    {
	      comp_op0 = fold_convert (comp_type, comp_op0);
	      comp_op1 = fold_convert (comp_type, comp_op1);
	      tem = (comp_code == LE_EXPR || comp_code == UNLE_EXPR)
		    ? fold_build2 (MIN_EXPR, comp_type, comp_op0, comp_op1)
		    : fold_build2 (MIN_EXPR, comp_type, comp_op1, comp_op0);
	      return pedantic_non_lvalue (fold_convert (type, tem));
	    }
	  break;
	case GE_EXPR:
	case GT_EXPR:
	case UNGE_EXPR:
	case UNGT_EXPR:
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1))))
	    {
	      comp_op0 = fold_convert (comp_type, comp_op0);
	      comp_op1 = fold_convert (comp_type, comp_op1);
	      tem = (comp_code == GE_EXPR || comp_code == UNGE_EXPR)
		    ? fold_build2 (MAX_EXPR, comp_type, comp_op0, comp_op1)
		    : fold_build2 (MAX_EXPR, comp_type, comp_op1, comp_op0);
	      return pedantic_non_lvalue (fold_convert (type, tem));
	    }
	  break;
	case UNEQ_EXPR:
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1))))
	    return pedantic_non_lvalue (fold_convert (type, arg2));
	  break;
	case LTGT_EXPR:
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1))))
	    return pedantic_non_lvalue (fold_convert (type, arg1));
	  break;
	default:
	  gcc_assert (TREE_CODE_CLASS (comp_code) == tcc_comparison);
	  break;
	}
    }

  /* If this is A op C1 ? A : C2 with C1 and C2 constant integers,
     we might still be able to simplify this.  For example,
     if C1 is one less or one more than C2, this might have started
     out as a MIN or MAX and been transformed by this function.
     Only good for INTEGER_TYPEs, because we need TYPE_MAX_VALUE.  */

  if (INTEGRAL_TYPE_P (type)
      && TREE_CODE (arg01) == INTEGER_CST
      && TREE_CODE (arg2) == INTEGER_CST)
    switch (comp_code)
      {
      case EQ_EXPR:
	/* We can replace A with C1 in this case.  */
	arg1 = fold_convert (type, arg01);
	return fold_build3 (COND_EXPR, type, arg0, arg1, arg2);

      case LT_EXPR:
	/* If C1 is C2 + 1, this is min(A, C2).  */
	if (! operand_equal_p (arg2, TYPE_MAX_VALUE (type),
			       OEP_ONLY_CONST)
	    && operand_equal_p (arg01,
				const_binop (PLUS_EXPR, arg2,
					     integer_one_node, 0),
				OEP_ONLY_CONST))
	  return pedantic_non_lvalue (fold_build2 (MIN_EXPR,
						   type, arg1, arg2));
	break;

      case LE_EXPR:
	/* If C1 is C2 - 1, this is min(A, C2).  */
	if (! operand_equal_p (arg2, TYPE_MIN_VALUE (type),
			       OEP_ONLY_CONST)
	    && operand_equal_p (arg01,
				const_binop (MINUS_EXPR, arg2,
					     integer_one_node, 0),
				OEP_ONLY_CONST))
	  return pedantic_non_lvalue (fold_build2 (MIN_EXPR,
						   type, arg1, arg2));
	break;

      case GT_EXPR:
	/* If C1 is C2 - 1, this is max(A, C2).  */
	if (! operand_equal_p (arg2, TYPE_MIN_VALUE (type),
			       OEP_ONLY_CONST)
	    && operand_equal_p (arg01,
				const_binop (MINUS_EXPR, arg2,
					     integer_one_node, 0),
				OEP_ONLY_CONST))
	  return pedantic_non_lvalue (fold_build2 (MAX_EXPR,
						   type, arg1, arg2));
	break;

      case GE_EXPR:
	/* If C1 is C2 + 1, this is max(A, C2).  */
	if (! operand_equal_p (arg2, TYPE_MAX_VALUE (type),
			       OEP_ONLY_CONST)
	    && operand_equal_p (arg01,
				const_binop (PLUS_EXPR, arg2,
					     integer_one_node, 0),
				OEP_ONLY_CONST))
	  return pedantic_non_lvalue (fold_build2 (MAX_EXPR,
						   type, arg1, arg2));
	break;
      case NE_EXPR:
	break;
      default:
	gcc_unreachable ();
      }

  return NULL_TREE;
}



#ifndef LOGICAL_OP_NON_SHORT_CIRCUIT
#define LOGICAL_OP_NON_SHORT_CIRCUIT (BRANCH_COST >= 2)
#endif

/* EXP is some logical combination of boolean tests.  See if we can
   merge it into some range test.  Return the new tree if so.  */

static tree
fold_range_test (enum tree_code code, tree type, tree op0, tree op1)
{
  int or_op = (code == TRUTH_ORIF_EXPR
	       || code == TRUTH_OR_EXPR);
  int in0_p, in1_p, in_p;
  tree low0, low1, low, high0, high1, high;
  bool strict_overflow_p = false;
  tree lhs = make_range (op0, &in0_p, &low0, &high0, &strict_overflow_p);
  tree rhs = make_range (op1, &in1_p, &low1, &high1, &strict_overflow_p);
  tree tem;
  const char * const warnmsg = G_("assuming signed overflow does not occur "
				  "when simplifying range test");

  /* If this is an OR operation, invert both sides; we will invert
     again at the end.  */
  if (or_op)
    in0_p = ! in0_p, in1_p = ! in1_p;

  /* If both expressions are the same, if we can merge the ranges, and we
     can build the range test, return it or it inverted.  If one of the
     ranges is always true or always false, consider it to be the same
     expression as the other.  */
  if ((lhs == 0 || rhs == 0 || operand_equal_p (lhs, rhs, 0))
      && merge_ranges (&in_p, &low, &high, in0_p, low0, high0,
		       in1_p, low1, high1)
      && 0 != (tem = (build_range_check (type,
					 lhs != 0 ? lhs
					 : rhs != 0 ? rhs : integer_zero_node,
					 in_p, low, high))))
    {
      if (strict_overflow_p)
	fold_overflow_warning (warnmsg, WARN_STRICT_OVERFLOW_COMPARISON);
      return or_op ? invert_truthvalue (tem) : tem;
    }

  /* On machines where the branch cost is expensive, if this is a
     short-circuited branch and the underlying object on both sides
     is the same, make a non-short-circuit operation.  */
  else if (LOGICAL_OP_NON_SHORT_CIRCUIT
	   && lhs != 0 && rhs != 0
	   && (code == TRUTH_ANDIF_EXPR
	       || code == TRUTH_ORIF_EXPR)
	   && operand_equal_p (lhs, rhs, 0))
    {
      /* If simple enough, just rewrite.  Otherwise, make a SAVE_EXPR
	 unless we are at top level or LHS contains a PLACEHOLDER_EXPR, in
	 which cases we can't do this.  */
      if (simple_operand_p (lhs))
	return build2 (code == TRUTH_ANDIF_EXPR
		       ? TRUTH_AND_EXPR : TRUTH_OR_EXPR,
		       type, op0, op1);

      else if (lang_hooks.decls.global_bindings_p () == 0
	       && ! CONTAINS_PLACEHOLDER_P (lhs))
	{
	  tree common = save_expr (lhs);

	  if (0 != (lhs = build_range_check (type, common,
					     or_op ? ! in0_p : in0_p,
					     low0, high0))
	      && (0 != (rhs = build_range_check (type, common,
						 or_op ? ! in1_p : in1_p,
						 low1, high1))))
	    {
	      if (strict_overflow_p)
		fold_overflow_warning (warnmsg,
				       WARN_STRICT_OVERFLOW_COMPARISON);
	      return build2 (code == TRUTH_ANDIF_EXPR
			     ? TRUTH_AND_EXPR : TRUTH_OR_EXPR,
			     type, lhs, rhs);
	    }
	}
    }

  return 0;
}

/* Subroutine for fold_truthop: C is an INTEGER_CST interpreted as a P
   bit value.  Arrange things so the extra bits will be set to zero if and
   only if C is signed-extended to its full width.  If MASK is nonzero,
   it is an INTEGER_CST that should be AND'ed with the extra bits.  */

static tree
unextend (tree c, int p, int unsignedp, tree mask)
{
  tree type = TREE_TYPE (c);
  int modesize = GET_MODE_BITSIZE (TYPE_MODE (type));
  tree temp;

  if (p == modesize || unsignedp)
    return c;

  /* We work by getting just the sign bit into the low-order bit, then
     into the high-order bit, then sign-extend.  We then XOR that value
     with C.  */
  temp = const_binop (RSHIFT_EXPR, c, size_int (p - 1), 0);
  temp = const_binop (BIT_AND_EXPR, temp, size_int (1), 0);

  /* We must use a signed type in order to get an arithmetic right shift.
     However, we must also avoid introducing accidental overflows, so that
     a subsequent call to integer_zerop will work.  Hence we must
     do the type conversion here.  At this point, the constant is either
     zero or one, and the conversion to a signed type can never overflow.
     We could get an overflow if this conversion is done anywhere else.  */
  if (TYPE_UNSIGNED (type))
    temp = fold_convert (lang_hooks.types.signed_type (type), temp);

  temp = const_binop (LSHIFT_EXPR, temp, size_int (modesize - 1), 0);
  temp = const_binop (RSHIFT_EXPR, temp, size_int (modesize - p - 1), 0);
  if (mask != 0)
    temp = const_binop (BIT_AND_EXPR, temp,
			fold_convert (TREE_TYPE (c), mask), 0);
  /* If necessary, convert the type back to match the type of C.  */
  if (TYPE_UNSIGNED (type))
    temp = fold_convert (type, temp);

  return fold_convert (type, const_binop (BIT_XOR_EXPR, c, temp, 0));
}

/* Find ways of folding logical expressions of LHS and RHS:
   Try to merge two comparisons to the same innermost item.
   Look for range tests like "ch >= '0' && ch <= '9'".
   Look for combinations of simple terms on machines with expensive branches
   and evaluate the RHS unconditionally.

   For example, if we have p->a == 2 && p->b == 4 and we can make an
   object large enough to span both A and B, we can do this with a comparison
   against the object ANDed with the a mask.

   If we have p->a == q->a && p->b == q->b, we may be able to use bit masking
   operations to do this with one comparison.

   We check for both normal comparisons and the BIT_AND_EXPRs made this by
   function and the one above.

   CODE is the logical operation being done.  It can be TRUTH_ANDIF_EXPR,
   TRUTH_AND_EXPR, TRUTH_ORIF_EXPR, or TRUTH_OR_EXPR.

   TRUTH_TYPE is the type of the logical operand and LHS and RHS are its
   two operands.

   We return the simplified tree or 0 if no optimization is possible.  */

static tree
fold_truthop (enum tree_code code, tree truth_type, tree lhs, tree rhs)
{
  /* If this is the "or" of two comparisons, we can do something if
     the comparisons are NE_EXPR.  If this is the "and", we can do something
     if the comparisons are EQ_EXPR.  I.e.,
	(a->b == 2 && a->c == 4) can become (a->new == NEW).

     WANTED_CODE is this operation code.  For single bit fields, we can
     convert EQ_EXPR to NE_EXPR so we need not reject the "wrong"
     comparison for one-bit fields.  */

  enum tree_code wanted_code;
  enum tree_code lcode, rcode;
  tree ll_arg, lr_arg, rl_arg, rr_arg;
  tree ll_inner, lr_inner, rl_inner, rr_inner;
  HOST_WIDE_INT ll_bitsize, ll_bitpos, lr_bitsize, lr_bitpos;
  HOST_WIDE_INT rl_bitsize, rl_bitpos, rr_bitsize, rr_bitpos;
  HOST_WIDE_INT xll_bitpos, xlr_bitpos, xrl_bitpos, xrr_bitpos;
  HOST_WIDE_INT lnbitsize, lnbitpos, rnbitsize, rnbitpos;
  int ll_unsignedp, lr_unsignedp, rl_unsignedp, rr_unsignedp;
  enum machine_mode ll_mode, lr_mode, rl_mode, rr_mode;
  enum machine_mode lnmode, rnmode;
  tree ll_mask, lr_mask, rl_mask, rr_mask;
  tree ll_and_mask, lr_and_mask, rl_and_mask, rr_and_mask;
  tree l_const, r_const;
  tree lntype, rntype, result;
  int first_bit, end_bit;
  int volatilep;
  tree orig_lhs = lhs, orig_rhs = rhs;
  enum tree_code orig_code = code;

  /* Start by getting the comparison codes.  Fail if anything is volatile.
     If one operand is a BIT_AND_EXPR with the constant one, treat it as if
     it were surrounded with a NE_EXPR.  */

  if (TREE_SIDE_EFFECTS (lhs) || TREE_SIDE_EFFECTS (rhs))
    return 0;

  lcode = TREE_CODE (lhs);
  rcode = TREE_CODE (rhs);

  if (lcode == BIT_AND_EXPR && integer_onep (TREE_OPERAND (lhs, 1)))
    {
      lhs = build2 (NE_EXPR, truth_type, lhs,
		    build_int_cst (TREE_TYPE (lhs), 0));
      lcode = NE_EXPR;
    }

  if (rcode == BIT_AND_EXPR && integer_onep (TREE_OPERAND (rhs, 1)))
    {
      rhs = build2 (NE_EXPR, truth_type, rhs,
		    build_int_cst (TREE_TYPE (rhs), 0));
      rcode = NE_EXPR;
    }

  if (TREE_CODE_CLASS (lcode) != tcc_comparison
      || TREE_CODE_CLASS (rcode) != tcc_comparison)
    return 0;

  ll_arg = TREE_OPERAND (lhs, 0);
  lr_arg = TREE_OPERAND (lhs, 1);
  rl_arg = TREE_OPERAND (rhs, 0);
  rr_arg = TREE_OPERAND (rhs, 1);

  /* Simplify (x<y) && (x==y) into (x<=y) and related optimizations.  */
  if (simple_operand_p (ll_arg)
      && simple_operand_p (lr_arg))
    {
      tree result;
      if (operand_equal_p (ll_arg, rl_arg, 0)
          && operand_equal_p (lr_arg, rr_arg, 0))
	{
          result = combine_comparisons (code, lcode, rcode,
					truth_type, ll_arg, lr_arg);
	  if (result)
	    return result;
	}
      else if (operand_equal_p (ll_arg, rr_arg, 0)
               && operand_equal_p (lr_arg, rl_arg, 0))
	{
          result = combine_comparisons (code, lcode,
					swap_tree_comparison (rcode),
					truth_type, ll_arg, lr_arg);
	  if (result)
	    return result;
	}
    }

  code = ((code == TRUTH_AND_EXPR || code == TRUTH_ANDIF_EXPR)
	  ? TRUTH_AND_EXPR : TRUTH_OR_EXPR);

  /* If the RHS can be evaluated unconditionally and its operands are
     simple, it wins to evaluate the RHS unconditionally on machines
     with expensive branches.  In this case, this isn't a comparison
     that can be merged.  Avoid doing this if the RHS is a floating-point
     comparison since those can trap.  */

  if (BRANCH_COST >= 2
      && ! FLOAT_TYPE_P (TREE_TYPE (rl_arg))
      && simple_operand_p (rl_arg)
      && simple_operand_p (rr_arg))
    {
      /* Convert (a != 0) || (b != 0) into (a | b) != 0.  */
      if (code == TRUTH_OR_EXPR
	  && lcode == NE_EXPR && integer_zerop (lr_arg)
	  && rcode == NE_EXPR && integer_zerop (rr_arg)
	  && TREE_TYPE (ll_arg) == TREE_TYPE (rl_arg))
	return build2 (NE_EXPR, truth_type,
		       build2 (BIT_IOR_EXPR, TREE_TYPE (ll_arg),
			       ll_arg, rl_arg),
		       build_int_cst (TREE_TYPE (ll_arg), 0));

      /* Convert (a == 0) && (b == 0) into (a | b) == 0.  */
      if (code == TRUTH_AND_EXPR
	  && lcode == EQ_EXPR && integer_zerop (lr_arg)
	  && rcode == EQ_EXPR && integer_zerop (rr_arg)
	  && TREE_TYPE (ll_arg) == TREE_TYPE (rl_arg))
	return build2 (EQ_EXPR, truth_type,
		       build2 (BIT_IOR_EXPR, TREE_TYPE (ll_arg),
			       ll_arg, rl_arg),
		       build_int_cst (TREE_TYPE (ll_arg), 0));

      if (LOGICAL_OP_NON_SHORT_CIRCUIT)
	{
	  if (code != orig_code || lhs != orig_lhs || rhs != orig_rhs)
	    return build2 (code, truth_type, lhs, rhs);
	  return NULL_TREE;
	}
    }

  /* See if the comparisons can be merged.  Then get all the parameters for
     each side.  */

  if ((lcode != EQ_EXPR && lcode != NE_EXPR)
      || (rcode != EQ_EXPR && rcode != NE_EXPR))
    return 0;

  volatilep = 0;
  ll_inner = decode_field_reference (ll_arg,
				     &ll_bitsize, &ll_bitpos, &ll_mode,
				     &ll_unsignedp, &volatilep, &ll_mask,
				     &ll_and_mask);
  lr_inner = decode_field_reference (lr_arg,
				     &lr_bitsize, &lr_bitpos, &lr_mode,
				     &lr_unsignedp, &volatilep, &lr_mask,
				     &lr_and_mask);
  rl_inner = decode_field_reference (rl_arg,
				     &rl_bitsize, &rl_bitpos, &rl_mode,
				     &rl_unsignedp, &volatilep, &rl_mask,
				     &rl_and_mask);
  rr_inner = decode_field_reference (rr_arg,
				     &rr_bitsize, &rr_bitpos, &rr_mode,
				     &rr_unsignedp, &volatilep, &rr_mask,
				     &rr_and_mask);

  /* It must be true that the inner operation on the lhs of each
     comparison must be the same if we are to be able to do anything.
     Then see if we have constants.  If not, the same must be true for
     the rhs's.  */
  if (volatilep || ll_inner == 0 || rl_inner == 0
      || ! operand_equal_p (ll_inner, rl_inner, 0))
    return 0;

  if (TREE_CODE (lr_arg) == INTEGER_CST
      && TREE_CODE (rr_arg) == INTEGER_CST)
    l_const = lr_arg, r_const = rr_arg;
  else if (lr_inner == 0 || rr_inner == 0
	   || ! operand_equal_p (lr_inner, rr_inner, 0))
    return 0;
  else
    l_const = r_const = 0;

  /* If either comparison code is not correct for our logical operation,
     fail.  However, we can convert a one-bit comparison against zero into
     the opposite comparison against that bit being set in the field.  */

  wanted_code = (code == TRUTH_AND_EXPR ? EQ_EXPR : NE_EXPR);
  if (lcode != wanted_code)
    {
      if (l_const && integer_zerop (l_const) && integer_pow2p (ll_mask))
	{
	  /* Make the left operand unsigned, since we are only interested
	     in the value of one bit.  Otherwise we are doing the wrong
	     thing below.  */
	  ll_unsignedp = 1;
	  l_const = ll_mask;
	}
      else
	return 0;
    }

  /* This is analogous to the code for l_const above.  */
  if (rcode != wanted_code)
    {
      if (r_const && integer_zerop (r_const) && integer_pow2p (rl_mask))
	{
	  rl_unsignedp = 1;
	  r_const = rl_mask;
	}
      else
	return 0;
    }

  /* After this point all optimizations will generate bit-field
     references, which we might not want.  */
  if (! lang_hooks.can_use_bit_fields_p ())
    return 0;

  /* See if we can find a mode that contains both fields being compared on
     the left.  If we can't, fail.  Otherwise, update all constants and masks
     to be relative to a field of that size.  */
  first_bit = MIN (ll_bitpos, rl_bitpos);
  end_bit = MAX (ll_bitpos + ll_bitsize, rl_bitpos + rl_bitsize);
  lnmode = get_best_mode (end_bit - first_bit, first_bit,
			  TYPE_ALIGN (TREE_TYPE (ll_inner)), word_mode,
			  volatilep);
  if (lnmode == VOIDmode)
    return 0;

  lnbitsize = GET_MODE_BITSIZE (lnmode);
  lnbitpos = first_bit & ~ (lnbitsize - 1);
  lntype = lang_hooks.types.type_for_size (lnbitsize, 1);
  xll_bitpos = ll_bitpos - lnbitpos, xrl_bitpos = rl_bitpos - lnbitpos;

  if (BYTES_BIG_ENDIAN)
    {
      xll_bitpos = lnbitsize - xll_bitpos - ll_bitsize;
      xrl_bitpos = lnbitsize - xrl_bitpos - rl_bitsize;
    }

  ll_mask = const_binop (LSHIFT_EXPR, fold_convert (lntype, ll_mask),
			 size_int (xll_bitpos), 0);
  rl_mask = const_binop (LSHIFT_EXPR, fold_convert (lntype, rl_mask),
			 size_int (xrl_bitpos), 0);

  if (l_const)
    {
      l_const = fold_convert (lntype, l_const);
      l_const = unextend (l_const, ll_bitsize, ll_unsignedp, ll_and_mask);
      l_const = const_binop (LSHIFT_EXPR, l_const, size_int (xll_bitpos), 0);
      if (! integer_zerop (const_binop (BIT_AND_EXPR, l_const,
					fold_build1 (BIT_NOT_EXPR,
						     lntype, ll_mask),
					0)))
	{
	  warning (0, "comparison is always %d", wanted_code == NE_EXPR);

	  return constant_boolean_node (wanted_code == NE_EXPR, truth_type);
	}
    }
  if (r_const)
    {
      r_const = fold_convert (lntype, r_const);
      r_const = unextend (r_const, rl_bitsize, rl_unsignedp, rl_and_mask);
      r_const = const_binop (LSHIFT_EXPR, r_const, size_int (xrl_bitpos), 0);
      if (! integer_zerop (const_binop (BIT_AND_EXPR, r_const,
					fold_build1 (BIT_NOT_EXPR,
						     lntype, rl_mask),
					0)))
	{
	  warning (0, "comparison is always %d", wanted_code == NE_EXPR);

	  return constant_boolean_node (wanted_code == NE_EXPR, truth_type);
	}
    }

  /* If the right sides are not constant, do the same for it.  Also,
     disallow this optimization if a size or signedness mismatch occurs
     between the left and right sides.  */
  if (l_const == 0)
    {
      if (ll_bitsize != lr_bitsize || rl_bitsize != rr_bitsize
	  || ll_unsignedp != lr_unsignedp || rl_unsignedp != rr_unsignedp
	  /* Make sure the two fields on the right
	     correspond to the left without being swapped.  */
	  || ll_bitpos - rl_bitpos != lr_bitpos - rr_bitpos)
	return 0;

      first_bit = MIN (lr_bitpos, rr_bitpos);
      end_bit = MAX (lr_bitpos + lr_bitsize, rr_bitpos + rr_bitsize);
      rnmode = get_best_mode (end_bit - first_bit, first_bit,
			      TYPE_ALIGN (TREE_TYPE (lr_inner)), word_mode,
			      volatilep);
      if (rnmode == VOIDmode)
	return 0;

      rnbitsize = GET_MODE_BITSIZE (rnmode);
      rnbitpos = first_bit & ~ (rnbitsize - 1);
      rntype = lang_hooks.types.type_for_size (rnbitsize, 1);
      xlr_bitpos = lr_bitpos - rnbitpos, xrr_bitpos = rr_bitpos - rnbitpos;

      if (BYTES_BIG_ENDIAN)
	{
	  xlr_bitpos = rnbitsize - xlr_bitpos - lr_bitsize;
	  xrr_bitpos = rnbitsize - xrr_bitpos - rr_bitsize;
	}

      lr_mask = const_binop (LSHIFT_EXPR, fold_convert (rntype, lr_mask),
			     size_int (xlr_bitpos), 0);
      rr_mask = const_binop (LSHIFT_EXPR, fold_convert (rntype, rr_mask),
			     size_int (xrr_bitpos), 0);

      /* Make a mask that corresponds to both fields being compared.
	 Do this for both items being compared.  If the operands are the
	 same size and the bits being compared are in the same position
	 then we can do this by masking both and comparing the masked
	 results.  */
      ll_mask = const_binop (BIT_IOR_EXPR, ll_mask, rl_mask, 0);
      lr_mask = const_binop (BIT_IOR_EXPR, lr_mask, rr_mask, 0);
      if (lnbitsize == rnbitsize && xll_bitpos == xlr_bitpos)
	{
	  lhs = make_bit_field_ref (ll_inner, lntype, lnbitsize, lnbitpos,
				    ll_unsignedp || rl_unsignedp);
	  if (! all_ones_mask_p (ll_mask, lnbitsize))
	    lhs = build2 (BIT_AND_EXPR, lntype, lhs, ll_mask);

	  rhs = make_bit_field_ref (lr_inner, rntype, rnbitsize, rnbitpos,
				    lr_unsignedp || rr_unsignedp);
	  if (! all_ones_mask_p (lr_mask, rnbitsize))
	    rhs = build2 (BIT_AND_EXPR, rntype, rhs, lr_mask);

	  return build2 (wanted_code, truth_type, lhs, rhs);
	}

      /* There is still another way we can do something:  If both pairs of
	 fields being compared are adjacent, we may be able to make a wider
	 field containing them both.

	 Note that we still must mask the lhs/rhs expressions.  Furthermore,
	 the mask must be shifted to account for the shift done by
	 make_bit_field_ref.  */
      if ((ll_bitsize + ll_bitpos == rl_bitpos
	   && lr_bitsize + lr_bitpos == rr_bitpos)
	  || (ll_bitpos == rl_bitpos + rl_bitsize
	      && lr_bitpos == rr_bitpos + rr_bitsize))
	{
	  tree type;

	  lhs = make_bit_field_ref (ll_inner, lntype, ll_bitsize + rl_bitsize,
				    MIN (ll_bitpos, rl_bitpos), ll_unsignedp);
	  rhs = make_bit_field_ref (lr_inner, rntype, lr_bitsize + rr_bitsize,
				    MIN (lr_bitpos, rr_bitpos), lr_unsignedp);

	  ll_mask = const_binop (RSHIFT_EXPR, ll_mask,
				 size_int (MIN (xll_bitpos, xrl_bitpos)), 0);
	  lr_mask = const_binop (RSHIFT_EXPR, lr_mask,
				 size_int (MIN (xlr_bitpos, xrr_bitpos)), 0);

	  /* Convert to the smaller type before masking out unwanted bits.  */
	  type = lntype;
	  if (lntype != rntype)
	    {
	      if (lnbitsize > rnbitsize)
		{
		  lhs = fold_convert (rntype, lhs);
		  ll_mask = fold_convert (rntype, ll_mask);
		  type = rntype;
		}
	      else if (lnbitsize < rnbitsize)
		{
		  rhs = fold_convert (lntype, rhs);
		  lr_mask = fold_convert (lntype, lr_mask);
		  type = lntype;
		}
	    }

	  if (! all_ones_mask_p (ll_mask, ll_bitsize + rl_bitsize))
	    lhs = build2 (BIT_AND_EXPR, type, lhs, ll_mask);

	  if (! all_ones_mask_p (lr_mask, lr_bitsize + rr_bitsize))
	    rhs = build2 (BIT_AND_EXPR, type, rhs, lr_mask);

	  return build2 (wanted_code, truth_type, lhs, rhs);
	}

      return 0;
    }

  /* Handle the case of comparisons with constants.  If there is something in
     common between the masks, those bits of the constants must be the same.
     If not, the condition is always false.  Test for this to avoid generating
     incorrect code below.  */
  result = const_binop (BIT_AND_EXPR, ll_mask, rl_mask, 0);
  if (! integer_zerop (result)
      && simple_cst_equal (const_binop (BIT_AND_EXPR, result, l_const, 0),
			   const_binop (BIT_AND_EXPR, result, r_const, 0)) != 1)
    {
      if (wanted_code == NE_EXPR)
	{
	  warning (0, "%<or%> of unmatched not-equal tests is always 1");
	  return constant_boolean_node (true, truth_type);
	}
      else
	{
	  warning (0, "%<and%> of mutually exclusive equal-tests is always 0");
	  return constant_boolean_node (false, truth_type);
	}
    }

  /* Construct the expression we will return.  First get the component
     reference we will make.  Unless the mask is all ones the width of
     that field, perform the mask operation.  Then compare with the
     merged constant.  */
  result = make_bit_field_ref (ll_inner, lntype, lnbitsize, lnbitpos,
			       ll_unsignedp || rl_unsignedp);

  ll_mask = const_binop (BIT_IOR_EXPR, ll_mask, rl_mask, 0);
  if (! all_ones_mask_p (ll_mask, lnbitsize))
    result = build2 (BIT_AND_EXPR, lntype, result, ll_mask);

  return build2 (wanted_code, truth_type, result,
		 const_binop (BIT_IOR_EXPR, l_const, r_const, 0));
}

/* Optimize T, which is a comparison of a MIN_EXPR or MAX_EXPR with a
   constant.  */

static tree
optimize_minmax_comparison (enum tree_code code, tree type, tree op0, tree op1)
{
  tree arg0 = op0;
  enum tree_code op_code;
  tree comp_const = op1;
  tree minmax_const;
  int consts_equal, consts_lt;
  tree inner;

  STRIP_SIGN_NOPS (arg0);

  op_code = TREE_CODE (arg0);
  minmax_const = TREE_OPERAND (arg0, 1);
  consts_equal = tree_int_cst_equal (minmax_const, comp_const);
  consts_lt = tree_int_cst_lt (minmax_const, comp_const);
  inner = TREE_OPERAND (arg0, 0);

  /* If something does not permit us to optimize, return the original tree.  */
  if ((op_code != MIN_EXPR && op_code != MAX_EXPR)
      || TREE_CODE (comp_const) != INTEGER_CST
      || TREE_CONSTANT_OVERFLOW (comp_const)
      || TREE_CODE (minmax_const) != INTEGER_CST
      || TREE_CONSTANT_OVERFLOW (minmax_const))
    return NULL_TREE;

  /* Now handle all the various comparison codes.  We only handle EQ_EXPR
     and GT_EXPR, doing the rest with recursive calls using logical
     simplifications.  */
  switch (code)
    {
    case NE_EXPR:  case LT_EXPR:  case LE_EXPR:
      {
	tree tem = optimize_minmax_comparison (invert_tree_comparison (code, false),
					  type, op0, op1);
	if (tem)
	  return invert_truthvalue (tem);
	return NULL_TREE;
      }

    case GE_EXPR:
      return
	fold_build2 (TRUTH_ORIF_EXPR, type,
		     optimize_minmax_comparison
		     (EQ_EXPR, type, arg0, comp_const),
		     optimize_minmax_comparison
		     (GT_EXPR, type, arg0, comp_const));

    case EQ_EXPR:
      if (op_code == MAX_EXPR && consts_equal)
	/* MAX (X, 0) == 0  ->  X <= 0  */
	return fold_build2 (LE_EXPR, type, inner, comp_const);

      else if (op_code == MAX_EXPR && consts_lt)
	/* MAX (X, 0) == 5  ->  X == 5   */
	return fold_build2 (EQ_EXPR, type, inner, comp_const);

      else if (op_code == MAX_EXPR)
	/* MAX (X, 0) == -1  ->  false  */
	return omit_one_operand (type, integer_zero_node, inner);

      else if (consts_equal)
	/* MIN (X, 0) == 0  ->  X >= 0  */
	return fold_build2 (GE_EXPR, type, inner, comp_const);

      else if (consts_lt)
	/* MIN (X, 0) == 5  ->  false  */
	return omit_one_operand (type, integer_zero_node, inner);

      else
	/* MIN (X, 0) == -1  ->  X == -1  */
	return fold_build2 (EQ_EXPR, type, inner, comp_const);

    case GT_EXPR:
      if (op_code == MAX_EXPR && (consts_equal || consts_lt))
	/* MAX (X, 0) > 0  ->  X > 0
	   MAX (X, 0) > 5  ->  X > 5  */
	return fold_build2 (GT_EXPR, type, inner, comp_const);

      else if (op_code == MAX_EXPR)
	/* MAX (X, 0) > -1  ->  true  */
	return omit_one_operand (type, integer_one_node, inner);

      else if (op_code == MIN_EXPR && (consts_equal || consts_lt))
	/* MIN (X, 0) > 0  ->  false
	   MIN (X, 0) > 5  ->  false  */
	return omit_one_operand (type, integer_zero_node, inner);

      else
	/* MIN (X, 0) > -1  ->  X > -1  */
	return fold_build2 (GT_EXPR, type, inner, comp_const);

    default:
      return NULL_TREE;
    }
}

/* T is an integer expression that is being multiplied, divided, or taken a
   modulus (CODE says which and what kind of divide or modulus) by a
   constant C.  See if we can eliminate that operation by folding it with
   other operations already in T.  WIDE_TYPE, if non-null, is a type that
   should be used for the computation if wider than our type.

   For example, if we are dividing (X * 8) + (Y * 16) by 4, we can return
   (X * 2) + (Y * 4).  We must, however, be assured that either the original
   expression would not overflow or that overflow is undefined for the type
   in the language in question.

   We also canonicalize (X + 7) * 4 into X * 4 + 28 in the hope that either
   the machine has a multiply-accumulate insn or that this is part of an
   addressing calculation.

   If we return a non-null expression, it is an equivalent form of the
   original computation, but need not be in the original type.

   We set *STRICT_OVERFLOW_P to true if the return values depends on
   signed overflow being undefined.  Otherwise we do not change
   *STRICT_OVERFLOW_P.  */

static tree
extract_muldiv (tree t, tree c, enum tree_code code, tree wide_type,
		bool *strict_overflow_p)
{
  /* To avoid exponential search depth, refuse to allow recursion past
     three levels.  Beyond that (1) it's highly unlikely that we'll find
     something interesting and (2) we've probably processed it before
     when we built the inner expression.  */

  static int depth;
  tree ret;

  if (depth > 3)
    return NULL;

  depth++;
  ret = extract_muldiv_1 (t, c, code, wide_type, strict_overflow_p);
  depth--;

  return ret;
}

static tree
extract_muldiv_1 (tree t, tree c, enum tree_code code, tree wide_type,
		  bool *strict_overflow_p)
{
  tree type = TREE_TYPE (t);
  enum tree_code tcode = TREE_CODE (t);
  tree ctype = (wide_type != 0 && (GET_MODE_SIZE (TYPE_MODE (wide_type))
				   > GET_MODE_SIZE (TYPE_MODE (type)))
		? wide_type : type);
  tree t1, t2;
  int same_p = tcode == code;
  tree op0 = NULL_TREE, op1 = NULL_TREE;
  bool sub_strict_overflow_p;

  /* Don't deal with constants of zero here; they confuse the code below.  */
  if (integer_zerop (c))
    return NULL_TREE;

  if (TREE_CODE_CLASS (tcode) == tcc_unary)
    op0 = TREE_OPERAND (t, 0);

  if (TREE_CODE_CLASS (tcode) == tcc_binary)
    op0 = TREE_OPERAND (t, 0), op1 = TREE_OPERAND (t, 1);

  /* Note that we need not handle conditional operations here since fold
     already handles those cases.  So just do arithmetic here.  */
  switch (tcode)
    {
    case INTEGER_CST:
      /* For a constant, we can always simplify if we are a multiply
	 or (for divide and modulus) if it is a multiple of our constant.  */
      if (code == MULT_EXPR
	  || integer_zerop (const_binop (TRUNC_MOD_EXPR, t, c, 0)))
	return const_binop (code, fold_convert (ctype, t),
			    fold_convert (ctype, c), 0);
      break;

    case CONVERT_EXPR:  case NON_LVALUE_EXPR:  case NOP_EXPR:
      /* If op0 is an expression ...  */
      if ((COMPARISON_CLASS_P (op0)
	   || UNARY_CLASS_P (op0)
	   || BINARY_CLASS_P (op0)
	   || EXPRESSION_CLASS_P (op0))
	  /* ... and is unsigned, and its type is smaller than ctype,
	     then we cannot pass through as widening.  */
	  && ((TYPE_UNSIGNED (TREE_TYPE (op0))
	       && ! (TREE_CODE (TREE_TYPE (op0)) == INTEGER_TYPE
		     && TYPE_IS_SIZETYPE (TREE_TYPE (op0)))
	       && (GET_MODE_SIZE (TYPE_MODE (ctype))
	           > GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (op0)))))
	      /* ... or this is a truncation (t is narrower than op0),
		 then we cannot pass through this narrowing.  */
	      || (GET_MODE_SIZE (TYPE_MODE (type))
		  < GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (op0))))
	      /* ... or signedness changes for division or modulus,
		 then we cannot pass through this conversion.  */
	      || (code != MULT_EXPR
		  && (TYPE_UNSIGNED (ctype)
		      != TYPE_UNSIGNED (TREE_TYPE (op0))))))
	break;

      /* Pass the constant down and see if we can make a simplification.  If
	 we can, replace this expression with the inner simplification for
	 possible later conversion to our or some other type.  */
      if ((t2 = fold_convert (TREE_TYPE (op0), c)) != 0
	  && TREE_CODE (t2) == INTEGER_CST
	  && ! TREE_CONSTANT_OVERFLOW (t2)
	  && (0 != (t1 = extract_muldiv (op0, t2, code,
					 code == MULT_EXPR
					 ? ctype : NULL_TREE,
					 strict_overflow_p))))
	return t1;
      break;

    case ABS_EXPR:
      /* If widening the type changes it from signed to unsigned, then we
         must avoid building ABS_EXPR itself as unsigned.  */
      if (TYPE_UNSIGNED (ctype) && !TYPE_UNSIGNED (type))
        {
          tree cstype = (*lang_hooks.types.signed_type) (ctype);
          if ((t1 = extract_muldiv (op0, c, code, cstype, strict_overflow_p))
	      != 0)
            {
              t1 = fold_build1 (tcode, cstype, fold_convert (cstype, t1));
              return fold_convert (ctype, t1);
            }
          break;
        }
      /* If the constant is negative, we cannot simplify this.  */
      if (tree_int_cst_sgn (c) == -1)
	break;
      /* FALLTHROUGH */
    case NEGATE_EXPR:
      if ((t1 = extract_muldiv (op0, c, code, wide_type, strict_overflow_p))
	  != 0)
	return fold_build1 (tcode, ctype, fold_convert (ctype, t1));
      break;

    case MIN_EXPR:  case MAX_EXPR:
      /* If widening the type changes the signedness, then we can't perform
	 this optimization as that changes the result.  */
      if (TYPE_UNSIGNED (ctype) != TYPE_UNSIGNED (type))
	break;

      /* MIN (a, b) / 5 -> MIN (a / 5, b / 5)  */
      sub_strict_overflow_p = false;
      if ((t1 = extract_muldiv (op0, c, code, wide_type,
				&sub_strict_overflow_p)) != 0
	  && (t2 = extract_muldiv (op1, c, code, wide_type,
				   &sub_strict_overflow_p)) != 0)
	{
	  if (tree_int_cst_sgn (c) < 0)
	    tcode = (tcode == MIN_EXPR ? MAX_EXPR : MIN_EXPR);
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;
	  return fold_build2 (tcode, ctype, fold_convert (ctype, t1),
			      fold_convert (ctype, t2));
	}
      break;

    case LSHIFT_EXPR:  case RSHIFT_EXPR:
      /* If the second operand is constant, this is a multiplication
	 or floor division, by a power of two, so we can treat it that
	 way unless the multiplier or divisor overflows.  Signed
	 left-shift overflow is implementation-defined rather than
	 undefined in C90, so do not convert signed left shift into
	 multiplication.  */
      if (TREE_CODE (op1) == INTEGER_CST
	  && (tcode == RSHIFT_EXPR || TYPE_UNSIGNED (TREE_TYPE (op0)))
	  /* const_binop may not detect overflow correctly,
	     so check for it explicitly here.  */
	  && TYPE_PRECISION (TREE_TYPE (size_one_node)) > TREE_INT_CST_LOW (op1)
	  && TREE_INT_CST_HIGH (op1) == 0
	  && 0 != (t1 = fold_convert (ctype,
				      const_binop (LSHIFT_EXPR,
						   size_one_node,
						   op1, 0)))
	  && ! TREE_OVERFLOW (t1))
	return extract_muldiv (build2 (tcode == LSHIFT_EXPR
				       ? MULT_EXPR : FLOOR_DIV_EXPR,
				       ctype, fold_convert (ctype, op0), t1),
			       c, code, wide_type, strict_overflow_p);
      break;

    case PLUS_EXPR:  case MINUS_EXPR:
      /* See if we can eliminate the operation on both sides.  If we can, we
	 can return a new PLUS or MINUS.  If we can't, the only remaining
	 cases where we can do anything are if the second operand is a
	 constant.  */
      sub_strict_overflow_p = false;
      t1 = extract_muldiv (op0, c, code, wide_type, &sub_strict_overflow_p);
      t2 = extract_muldiv (op1, c, code, wide_type, &sub_strict_overflow_p);
      if (t1 != 0 && t2 != 0
	  && (code == MULT_EXPR
	      /* If not multiplication, we can only do this if both operands
		 are divisible by c.  */
	      || (multiple_of_p (ctype, op0, c)
	          && multiple_of_p (ctype, op1, c))))
	{
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;
	  return fold_build2 (tcode, ctype, fold_convert (ctype, t1),
			      fold_convert (ctype, t2));
	}

      /* If this was a subtraction, negate OP1 and set it to be an addition.
	 This simplifies the logic below.  */
      if (tcode == MINUS_EXPR)
	tcode = PLUS_EXPR, op1 = negate_expr (op1);

      if (TREE_CODE (op1) != INTEGER_CST)
	break;

      /* If either OP1 or C are negative, this optimization is not safe for
	 some of the division and remainder types while for others we need
	 to change the code.  */
      if (tree_int_cst_sgn (op1) < 0 || tree_int_cst_sgn (c) < 0)
	{
	  if (code == CEIL_DIV_EXPR)
	    code = FLOOR_DIV_EXPR;
	  else if (code == FLOOR_DIV_EXPR)
	    code = CEIL_DIV_EXPR;
	  else if (code != MULT_EXPR
		   && code != CEIL_MOD_EXPR && code != FLOOR_MOD_EXPR)
	    break;
	}

      /* If it's a multiply or a division/modulus operation of a multiple
         of our constant, do the operation and verify it doesn't overflow.  */
      if (code == MULT_EXPR
	  || integer_zerop (const_binop (TRUNC_MOD_EXPR, op1, c, 0)))
	{
	  op1 = const_binop (code, fold_convert (ctype, op1),
			     fold_convert (ctype, c), 0);
	  /* We allow the constant to overflow with wrapping semantics.  */
	  if (op1 == 0
	      || (TREE_OVERFLOW (op1) && !TYPE_OVERFLOW_WRAPS (ctype)))
	    break;
	}
      else
	break;

      /* If we have an unsigned type is not a sizetype, we cannot widen
	 the operation since it will change the result if the original
	 computation overflowed.  */
      if (TYPE_UNSIGNED (ctype)
	  && ! (TREE_CODE (ctype) == INTEGER_TYPE && TYPE_IS_SIZETYPE (ctype))
	  && ctype != type)
	break;

      /* If we were able to eliminate our operation from the first side,
	 apply our operation to the second side and reform the PLUS.  */
      if (t1 != 0 && (TREE_CODE (t1) != code || code == MULT_EXPR))
	return fold_build2 (tcode, ctype, fold_convert (ctype, t1), op1);

      /* The last case is if we are a multiply.  In that case, we can
	 apply the distributive law to commute the multiply and addition
	 if the multiplication of the constants doesn't overflow.  */
      if (code == MULT_EXPR)
	return fold_build2 (tcode, ctype,
			    fold_build2 (code, ctype,
					 fold_convert (ctype, op0),
					 fold_convert (ctype, c)),
			    op1);

      break;

    case MULT_EXPR:
      /* We have a special case here if we are doing something like
	 (C * 8) % 4 since we know that's zero.  */
      if ((code == TRUNC_MOD_EXPR || code == CEIL_MOD_EXPR
	   || code == FLOOR_MOD_EXPR || code == ROUND_MOD_EXPR)
	  && TREE_CODE (TREE_OPERAND (t, 1)) == INTEGER_CST
	  && integer_zerop (const_binop (TRUNC_MOD_EXPR, op1, c, 0)))
	return omit_one_operand (type, integer_zero_node, op0);

      /* ... fall through ...  */

    case TRUNC_DIV_EXPR:  case CEIL_DIV_EXPR:  case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:  case EXACT_DIV_EXPR:
      /* If we can extract our operation from the LHS, do so and return a
	 new operation.  Likewise for the RHS from a MULT_EXPR.  Otherwise,
	 do something only if the second operand is a constant.  */
      if (same_p
	  && (t1 = extract_muldiv (op0, c, code, wide_type,
				   strict_overflow_p)) != 0)
	return fold_build2 (tcode, ctype, fold_convert (ctype, t1),
			    fold_convert (ctype, op1));
      else if (tcode == MULT_EXPR && code == MULT_EXPR
	       && (t1 = extract_muldiv (op1, c, code, wide_type,
					strict_overflow_p)) != 0)
	return fold_build2 (tcode, ctype, fold_convert (ctype, op0),
			    fold_convert (ctype, t1));
      else if (TREE_CODE (op1) != INTEGER_CST)
	return 0;

      /* If these are the same operation types, we can associate them
	 assuming no overflow.  */
      if (tcode == code
	  && 0 != (t1 = const_binop (MULT_EXPR, fold_convert (ctype, op1),
				     fold_convert (ctype, c), 0))
	  && ! TREE_OVERFLOW (t1))
	return fold_build2 (tcode, ctype, fold_convert (ctype, op0), t1);

      /* If these operations "cancel" each other, we have the main
	 optimizations of this pass, which occur when either constant is a
	 multiple of the other, in which case we replace this with either an
	 operation or CODE or TCODE.

	 If we have an unsigned type that is not a sizetype, we cannot do
	 this since it will change the result if the original computation
	 overflowed.  */
      if ((TYPE_OVERFLOW_UNDEFINED (ctype)
	   || (TREE_CODE (ctype) == INTEGER_TYPE && TYPE_IS_SIZETYPE (ctype)))
	  && ((code == MULT_EXPR && tcode == EXACT_DIV_EXPR)
	      || (tcode == MULT_EXPR
		  && code != TRUNC_MOD_EXPR && code != CEIL_MOD_EXPR
		  && code != FLOOR_MOD_EXPR && code != ROUND_MOD_EXPR)))
	{
	  if (integer_zerop (const_binop (TRUNC_MOD_EXPR, op1, c, 0)))
	    {
	      if (TYPE_OVERFLOW_UNDEFINED (ctype))
		*strict_overflow_p = true;
	      return fold_build2 (tcode, ctype, fold_convert (ctype, op0),
				  fold_convert (ctype,
						const_binop (TRUNC_DIV_EXPR,
							     op1, c, 0)));
	    }
	  else if (integer_zerop (const_binop (TRUNC_MOD_EXPR, c, op1, 0)))
	    {
	      if (TYPE_OVERFLOW_UNDEFINED (ctype))
		*strict_overflow_p = true;
	      return fold_build2 (code, ctype, fold_convert (ctype, op0),
				  fold_convert (ctype,
						const_binop (TRUNC_DIV_EXPR,
							     c, op1, 0)));
	    }
	}
      break;

    default:
      break;
    }

  return 0;
}

/* Return a node which has the indicated constant VALUE (either 0 or
   1), and is of the indicated TYPE.  */

tree
constant_boolean_node (int value, tree type)
{
  if (type == integer_type_node)
    return value ? integer_one_node : integer_zero_node;
  else if (type == boolean_type_node)
    return value ? boolean_true_node : boolean_false_node;
  else
    return build_int_cst (type, value);
}


/* Return true if expr looks like an ARRAY_REF and set base and
   offset to the appropriate trees.  If there is no offset,
   offset is set to NULL_TREE.  Base will be canonicalized to
   something you can get the element type from using
   TREE_TYPE (TREE_TYPE (base)).  Offset will be the offset
   in bytes to the base.  */

static bool
extract_array_ref (tree expr, tree *base, tree *offset)
{
  /* One canonical form is a PLUS_EXPR with the first
     argument being an ADDR_EXPR with a possible NOP_EXPR
     attached.  */
  if (TREE_CODE (expr) == PLUS_EXPR)
    {
      tree op0 = TREE_OPERAND (expr, 0);
      tree inner_base, dummy1;
      /* Strip NOP_EXPRs here because the C frontends and/or
	 folders present us (int *)&x.a + 4B possibly.  */
      STRIP_NOPS (op0);
      if (extract_array_ref (op0, &inner_base, &dummy1))
	{
	  *base = inner_base;
	  if (dummy1 == NULL_TREE)
	    *offset = TREE_OPERAND (expr, 1);
	  else
	    *offset = fold_build2 (PLUS_EXPR, TREE_TYPE (expr),
				   dummy1, TREE_OPERAND (expr, 1));
	  return true;
	}
    }
  /* Other canonical form is an ADDR_EXPR of an ARRAY_REF,
     which we transform into an ADDR_EXPR with appropriate
     offset.  For other arguments to the ADDR_EXPR we assume
     zero offset and as such do not care about the ADDR_EXPR
     type and strip possible nops from it.  */
  else if (TREE_CODE (expr) == ADDR_EXPR)
    {
      tree op0 = TREE_OPERAND (expr, 0);
      if (TREE_CODE (op0) == ARRAY_REF)
	{
	  tree idx = TREE_OPERAND (op0, 1);
	  *base = TREE_OPERAND (op0, 0);
	  *offset = fold_build2 (MULT_EXPR, TREE_TYPE (idx), idx,
				 array_ref_element_size (op0)); 
	}
      else
	{
	  /* Handle array-to-pointer decay as &a.  */
	  if (TREE_CODE (TREE_TYPE (op0)) == ARRAY_TYPE)
	    *base = TREE_OPERAND (expr, 0);
	  else
	    *base = expr;
	  *offset = NULL_TREE;
	}
      return true;
    }
  /* The next canonical form is a VAR_DECL with POINTER_TYPE.  */
  else if (SSA_VAR_P (expr)
	   && TREE_CODE (TREE_TYPE (expr)) == POINTER_TYPE)
    {
      *base = expr;
      *offset = NULL_TREE;
      return true;
    }

  return false;
}


/* Transform `a + (b ? x : y)' into `b ? (a + x) : (a + y)'.
   Transform, `a + (x < y)' into `(x < y) ? (a + 1) : (a + 0)'.  Here
   CODE corresponds to the `+', COND to the `(b ? x : y)' or `(x < y)'
   expression, and ARG to `a'.  If COND_FIRST_P is nonzero, then the
   COND is the first argument to CODE; otherwise (as in the example
   given here), it is the second argument.  TYPE is the type of the
   original expression.  Return NULL_TREE if no simplification is
   possible.  */

static tree
fold_binary_op_with_conditional_arg (enum tree_code code,
				     tree type, tree op0, tree op1,
				     tree cond, tree arg, int cond_first_p)
{
  tree cond_type = cond_first_p ? TREE_TYPE (op0) : TREE_TYPE (op1);
  tree arg_type = cond_first_p ? TREE_TYPE (op1) : TREE_TYPE (op0);
  tree test, true_value, false_value;
  tree lhs = NULL_TREE;
  tree rhs = NULL_TREE;

  /* This transformation is only worthwhile if we don't have to wrap
     arg in a SAVE_EXPR, and the operation can be simplified on at least
     one of the branches once its pushed inside the COND_EXPR.  */
  if (!TREE_CONSTANT (arg))
    return NULL_TREE;

  if (TREE_CODE (cond) == COND_EXPR)
    {
      test = TREE_OPERAND (cond, 0);
      true_value = TREE_OPERAND (cond, 1);
      false_value = TREE_OPERAND (cond, 2);
      /* If this operand throws an expression, then it does not make
	 sense to try to perform a logical or arithmetic operation
	 involving it.  */
      if (VOID_TYPE_P (TREE_TYPE (true_value)))
	lhs = true_value;
      if (VOID_TYPE_P (TREE_TYPE (false_value)))
	rhs = false_value;
    }
  else
    {
      tree testtype = TREE_TYPE (cond);
      test = cond;
      true_value = constant_boolean_node (true, testtype);
      false_value = constant_boolean_node (false, testtype);
    }

  arg = fold_convert (arg_type, arg);
  if (lhs == 0)
    {
      true_value = fold_convert (cond_type, true_value);
      if (cond_first_p)
	lhs = fold_build2 (code, type, true_value, arg);
      else
	lhs = fold_build2 (code, type, arg, true_value);
    }
  if (rhs == 0)
    {
      false_value = fold_convert (cond_type, false_value);
      if (cond_first_p)
	rhs = fold_build2 (code, type, false_value, arg);
      else
	rhs = fold_build2 (code, type, arg, false_value);
    }

  test = fold_build3 (COND_EXPR, type, test, lhs, rhs);
  return fold_convert (type, test);
}


/* Subroutine of fold() that checks for the addition of +/- 0.0.

   If !NEGATE, return true if ADDEND is +/-0.0 and, for all X of type
   TYPE, X + ADDEND is the same as X.  If NEGATE, return true if X -
   ADDEND is the same as X.

   X + 0 and X - 0 both give X when X is NaN, infinite, or nonzero
   and finite.  The problematic cases are when X is zero, and its mode
   has signed zeros.  In the case of rounding towards -infinity,
   X - 0 is not the same as X because 0 - 0 is -0.  In other rounding
   modes, X + 0 is not the same as X because -0 + 0 is 0.  */

static bool
fold_real_zero_addition_p (tree type, tree addend, int negate)
{
  if (!real_zerop (addend))
    return false;

  /* Don't allow the fold with -fsignaling-nans.  */
  if (HONOR_SNANS (TYPE_MODE (type)))
    return false;

  /* Allow the fold if zeros aren't signed, or their sign isn't important.  */
  if (!HONOR_SIGNED_ZEROS (TYPE_MODE (type)))
    return true;

  /* Treat x + -0 as x - 0 and x - -0 as x + 0.  */
  if (TREE_CODE (addend) == REAL_CST
      && REAL_VALUE_MINUS_ZERO (TREE_REAL_CST (addend)))
    negate = !negate;

  /* The mode has signed zeros, and we have to honor their sign.
     In this situation, there is only one case we can return true for.
     X - 0 is the same as X unless rounding towards -infinity is
     supported.  */
  return negate && !HONOR_SIGN_DEPENDENT_ROUNDING (TYPE_MODE (type));
}

/* Subroutine of fold() that checks comparisons of built-in math
   functions against real constants.

   FCODE is the DECL_FUNCTION_CODE of the built-in, CODE is the comparison
   operator: EQ_EXPR, NE_EXPR, GT_EXPR, LT_EXPR, GE_EXPR or LE_EXPR.  TYPE
   is the type of the result and ARG0 and ARG1 are the operands of the
   comparison.  ARG1 must be a TREE_REAL_CST.

   The function returns the constant folded tree if a simplification
   can be made, and NULL_TREE otherwise.  */

static tree
fold_mathfn_compare (enum built_in_function fcode, enum tree_code code,
		     tree type, tree arg0, tree arg1)
{
  REAL_VALUE_TYPE c;

  if (BUILTIN_SQRT_P (fcode))
    {
      tree arg = TREE_VALUE (TREE_OPERAND (arg0, 1));
      enum machine_mode mode = TYPE_MODE (TREE_TYPE (arg0));

      c = TREE_REAL_CST (arg1);
      if (REAL_VALUE_NEGATIVE (c))
	{
	  /* sqrt(x) < y is always false, if y is negative.  */
	  if (code == EQ_EXPR || code == LT_EXPR || code == LE_EXPR)
	    return omit_one_operand (type, integer_zero_node, arg);

	  /* sqrt(x) > y is always true, if y is negative and we
	     don't care about NaNs, i.e. negative values of x.  */
	  if (code == NE_EXPR || !HONOR_NANS (mode))
	    return omit_one_operand (type, integer_one_node, arg);

	  /* sqrt(x) > y is the same as x >= 0, if y is negative.  */
	  return fold_build2 (GE_EXPR, type, arg,
			      build_real (TREE_TYPE (arg), dconst0));
	}
      else if (code == GT_EXPR || code == GE_EXPR)
	{
	  REAL_VALUE_TYPE c2;

	  REAL_ARITHMETIC (c2, MULT_EXPR, c, c);
	  real_convert (&c2, mode, &c2);

	  if (REAL_VALUE_ISINF (c2))
	    {
	      /* sqrt(x) > y is x == +Inf, when y is very large.  */
	      if (HONOR_INFINITIES (mode))
		return fold_build2 (EQ_EXPR, type, arg,
				    build_real (TREE_TYPE (arg), c2));

	      /* sqrt(x) > y is always false, when y is very large
		 and we don't care about infinities.  */
	      return omit_one_operand (type, integer_zero_node, arg);
	    }

	  /* sqrt(x) > c is the same as x > c*c.  */
	  return fold_build2 (code, type, arg,
			      build_real (TREE_TYPE (arg), c2));
	}
      else if (code == LT_EXPR || code == LE_EXPR)
	{
	  REAL_VALUE_TYPE c2;

	  REAL_ARITHMETIC (c2, MULT_EXPR, c, c);
	  real_convert (&c2, mode, &c2);

	  if (REAL_VALUE_ISINF (c2))
	    {
	      /* sqrt(x) < y is always true, when y is a very large
		 value and we don't care about NaNs or Infinities.  */
	      if (! HONOR_NANS (mode) && ! HONOR_INFINITIES (mode))
		return omit_one_operand (type, integer_one_node, arg);

	      /* sqrt(x) < y is x != +Inf when y is very large and we
		 don't care about NaNs.  */
	      if (! HONOR_NANS (mode))
		return fold_build2 (NE_EXPR, type, arg,
				    build_real (TREE_TYPE (arg), c2));

	      /* sqrt(x) < y is x >= 0 when y is very large and we
		 don't care about Infinities.  */
	      if (! HONOR_INFINITIES (mode))
		return fold_build2 (GE_EXPR, type, arg,
				    build_real (TREE_TYPE (arg), dconst0));

	      /* sqrt(x) < y is x >= 0 && x != +Inf, when y is large.  */
	      if (lang_hooks.decls.global_bindings_p () != 0
		  || CONTAINS_PLACEHOLDER_P (arg))
		return NULL_TREE;

	      arg = save_expr (arg);
	      return fold_build2 (TRUTH_ANDIF_EXPR, type,
				  fold_build2 (GE_EXPR, type, arg,
					       build_real (TREE_TYPE (arg),
							   dconst0)),
				  fold_build2 (NE_EXPR, type, arg,
					       build_real (TREE_TYPE (arg),
							   c2)));
	    }

	  /* sqrt(x) < c is the same as x < c*c, if we ignore NaNs.  */
	  if (! HONOR_NANS (mode))
	    return fold_build2 (code, type, arg,
				build_real (TREE_TYPE (arg), c2));

	  /* sqrt(x) < c is the same as x >= 0 && x < c*c.  */
	  if (lang_hooks.decls.global_bindings_p () == 0
	      && ! CONTAINS_PLACEHOLDER_P (arg))
	    {
	      arg = save_expr (arg);
	      return fold_build2 (TRUTH_ANDIF_EXPR, type,
				  fold_build2 (GE_EXPR, type, arg,
					       build_real (TREE_TYPE (arg),
							   dconst0)),
				  fold_build2 (code, type, arg,
					       build_real (TREE_TYPE (arg),
							   c2)));
	    }
	}
    }

  return NULL_TREE;
}

/* Subroutine of fold() that optimizes comparisons against Infinities,
   either +Inf or -Inf.

   CODE is the comparison operator: EQ_EXPR, NE_EXPR, GT_EXPR, LT_EXPR,
   GE_EXPR or LE_EXPR.  TYPE is the type of the result and ARG0 and ARG1
   are the operands of the comparison.  ARG1 must be a TREE_REAL_CST.

   The function returns the constant folded tree if a simplification
   can be made, and NULL_TREE otherwise.  */

static tree
fold_inf_compare (enum tree_code code, tree type, tree arg0, tree arg1)
{
  enum machine_mode mode;
  REAL_VALUE_TYPE max;
  tree temp;
  bool neg;

  mode = TYPE_MODE (TREE_TYPE (arg0));

  /* For negative infinity swap the sense of the comparison.  */
  neg = REAL_VALUE_NEGATIVE (TREE_REAL_CST (arg1));
  if (neg)
    code = swap_tree_comparison (code);

  switch (code)
    {
    case GT_EXPR:
      /* x > +Inf is always false, if with ignore sNANs.  */
      if (HONOR_SNANS (mode))
        return NULL_TREE;
      return omit_one_operand (type, integer_zero_node, arg0);

    case LE_EXPR:
      /* x <= +Inf is always true, if we don't case about NaNs.  */
      if (! HONOR_NANS (mode))
	return omit_one_operand (type, integer_one_node, arg0);

      /* x <= +Inf is the same as x == x, i.e. isfinite(x).  */
      if (lang_hooks.decls.global_bindings_p () == 0
	  && ! CONTAINS_PLACEHOLDER_P (arg0))
	{
	  arg0 = save_expr (arg0);
	  return fold_build2 (EQ_EXPR, type, arg0, arg0);
	}
      break;

    case EQ_EXPR:
    case GE_EXPR:
      /* x == +Inf and x >= +Inf are always equal to x > DBL_MAX.  */
      real_maxval (&max, neg, mode);
      return fold_build2 (neg ? LT_EXPR : GT_EXPR, type,
			  arg0, build_real (TREE_TYPE (arg0), max));

    case LT_EXPR:
      /* x < +Inf is always equal to x <= DBL_MAX.  */
      real_maxval (&max, neg, mode);
      return fold_build2 (neg ? GE_EXPR : LE_EXPR, type,
			  arg0, build_real (TREE_TYPE (arg0), max));

    case NE_EXPR:
      /* x != +Inf is always equal to !(x > DBL_MAX).  */
      real_maxval (&max, neg, mode);
      if (! HONOR_NANS (mode))
	return fold_build2 (neg ? GE_EXPR : LE_EXPR, type,
			    arg0, build_real (TREE_TYPE (arg0), max));

      /* The transformation below creates non-gimple code and thus is
	 not appropriate if we are in gimple form.  */
      if (in_gimple_form)
	return NULL_TREE;

      temp = fold_build2 (neg ? LT_EXPR : GT_EXPR, type,
			  arg0, build_real (TREE_TYPE (arg0), max));
      return fold_build1 (TRUTH_NOT_EXPR, type, temp);

    default:
      break;
    }

  return NULL_TREE;
}

/* Subroutine of fold() that optimizes comparisons of a division by
   a nonzero integer constant against an integer constant, i.e.
   X/C1 op C2.

   CODE is the comparison operator: EQ_EXPR, NE_EXPR, GT_EXPR, LT_EXPR,
   GE_EXPR or LE_EXPR.  TYPE is the type of the result and ARG0 and ARG1
   are the operands of the comparison.  ARG1 must be a TREE_REAL_CST.

   The function returns the constant folded tree if a simplification
   can be made, and NULL_TREE otherwise.  */

static tree
fold_div_compare (enum tree_code code, tree type, tree arg0, tree arg1)
{
  tree prod, tmp, hi, lo;
  tree arg00 = TREE_OPERAND (arg0, 0);
  tree arg01 = TREE_OPERAND (arg0, 1);
  unsigned HOST_WIDE_INT lpart;
  HOST_WIDE_INT hpart;
  bool unsigned_p = TYPE_UNSIGNED (TREE_TYPE (arg0));
  bool neg_overflow;
  int overflow;

  /* We have to do this the hard way to detect unsigned overflow.
     prod = int_const_binop (MULT_EXPR, arg01, arg1, 0);  */
  overflow = mul_double_with_sign (TREE_INT_CST_LOW (arg01),
				   TREE_INT_CST_HIGH (arg01),
				   TREE_INT_CST_LOW (arg1),
				   TREE_INT_CST_HIGH (arg1),
				   &lpart, &hpart, unsigned_p);
  prod = build_int_cst_wide (TREE_TYPE (arg00), lpart, hpart);
  prod = force_fit_type (prod, -1, overflow, false);
  neg_overflow = false;

  if (unsigned_p)
    {
      tmp = int_const_binop (MINUS_EXPR, arg01, integer_one_node, 0);
      lo = prod;

      /* Likewise hi = int_const_binop (PLUS_EXPR, prod, tmp, 0).  */
      overflow = add_double_with_sign (TREE_INT_CST_LOW (prod),
				       TREE_INT_CST_HIGH (prod),
				       TREE_INT_CST_LOW (tmp),
				       TREE_INT_CST_HIGH (tmp),
				       &lpart, &hpart, unsigned_p);
      hi = build_int_cst_wide (TREE_TYPE (arg00), lpart, hpart);
      hi = force_fit_type (hi, -1, overflow | TREE_OVERFLOW (prod),
			   TREE_CONSTANT_OVERFLOW (prod));
    }
  else if (tree_int_cst_sgn (arg01) >= 0)
    {
      tmp = int_const_binop (MINUS_EXPR, arg01, integer_one_node, 0);
      switch (tree_int_cst_sgn (arg1))
	{
	case -1:
	  neg_overflow = true;
	  lo = int_const_binop (MINUS_EXPR, prod, tmp, 0);
	  hi = prod;
	  break;

	case  0:
	  lo = fold_negate_const (tmp, TREE_TYPE (arg0));
	  hi = tmp;
	  break;

	case  1:
          hi = int_const_binop (PLUS_EXPR, prod, tmp, 0);
	  lo = prod;
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    {
      /* A negative divisor reverses the relational operators.  */
      code = swap_tree_comparison (code);

      tmp = int_const_binop (PLUS_EXPR, arg01, integer_one_node, 0);
      switch (tree_int_cst_sgn (arg1))
	{
	case -1:
	  hi = int_const_binop (MINUS_EXPR, prod, tmp, 0);
	  lo = prod;
	  break;

	case  0:
	  hi = fold_negate_const (tmp, TREE_TYPE (arg0));
	  lo = tmp;
	  break;

	case  1:
	  neg_overflow = true;
	  lo = int_const_binop (PLUS_EXPR, prod, tmp, 0);
	  hi = prod;
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  switch (code)
    {
    case EQ_EXPR:
      if (TREE_OVERFLOW (lo) && TREE_OVERFLOW (hi))
	return omit_one_operand (type, integer_zero_node, arg00);
      if (TREE_OVERFLOW (hi))
	return fold_build2 (GE_EXPR, type, arg00, lo);
      if (TREE_OVERFLOW (lo))
	return fold_build2 (LE_EXPR, type, arg00, hi);
      return build_range_check (type, arg00, 1, lo, hi);

    case NE_EXPR:
      if (TREE_OVERFLOW (lo) && TREE_OVERFLOW (hi))
	return omit_one_operand (type, integer_one_node, arg00);
      if (TREE_OVERFLOW (hi))
	return fold_build2 (LT_EXPR, type, arg00, lo);
      if (TREE_OVERFLOW (lo))
	return fold_build2 (GT_EXPR, type, arg00, hi);
      return build_range_check (type, arg00, 0, lo, hi);

    case LT_EXPR:
      if (TREE_OVERFLOW (lo))
	{
	  tmp = neg_overflow ? integer_zero_node : integer_one_node;
	  return omit_one_operand (type, tmp, arg00);
	}
      return fold_build2 (LT_EXPR, type, arg00, lo);

    case LE_EXPR:
      if (TREE_OVERFLOW (hi))
	{
	  tmp = neg_overflow ? integer_zero_node : integer_one_node;
	  return omit_one_operand (type, tmp, arg00);
	}
      return fold_build2 (LE_EXPR, type, arg00, hi);

    case GT_EXPR:
      if (TREE_OVERFLOW (hi))
	{
	  tmp = neg_overflow ? integer_one_node : integer_zero_node;
	  return omit_one_operand (type, tmp, arg00);
	}
      return fold_build2 (GT_EXPR, type, arg00, hi);

    case GE_EXPR:
      if (TREE_OVERFLOW (lo))
	{
	  tmp = neg_overflow ? integer_one_node : integer_zero_node;
	  return omit_one_operand (type, tmp, arg00);
	}
      return fold_build2 (GE_EXPR, type, arg00, lo);

    default:
      break;
    }

  return NULL_TREE;
}


/* If CODE with arguments ARG0 and ARG1 represents a single bit
   equality/inequality test, then return a simplified form of the test
   using a sign testing.  Otherwise return NULL.  TYPE is the desired
   result type.  */

static tree
fold_single_bit_test_into_sign_test (enum tree_code code, tree arg0, tree arg1,
				     tree result_type)
{
  /* If this is testing a single bit, we can optimize the test.  */
  if ((code == NE_EXPR || code == EQ_EXPR)
      && TREE_CODE (arg0) == BIT_AND_EXPR && integer_zerop (arg1)
      && integer_pow2p (TREE_OPERAND (arg0, 1)))
    {
      /* If we have (A & C) != 0 where C is the sign bit of A, convert
	 this into A < 0.  Similarly for (A & C) == 0 into A >= 0.  */
      tree arg00 = sign_bit_p (TREE_OPERAND (arg0, 0), TREE_OPERAND (arg0, 1));

      if (arg00 != NULL_TREE
	  /* This is only a win if casting to a signed type is cheap,
	     i.e. when arg00's type is not a partial mode.  */
	  && TYPE_PRECISION (TREE_TYPE (arg00))
	     == GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (arg00))))
	{
	  tree stype = lang_hooks.types.signed_type (TREE_TYPE (arg00));
	  return fold_build2 (code == EQ_EXPR ? GE_EXPR : LT_EXPR,
			      result_type, fold_convert (stype, arg00),
			      build_int_cst (stype, 0));
	}
    }

  return NULL_TREE;
}

/* If CODE with arguments ARG0 and ARG1 represents a single bit
   equality/inequality test, then return a simplified form of
   the test using shifts and logical operations.  Otherwise return
   NULL.  TYPE is the desired result type.  */

tree
fold_single_bit_test (enum tree_code code, tree arg0, tree arg1,
		      tree result_type)
{
  /* If this is testing a single bit, we can optimize the test.  */
  if ((code == NE_EXPR || code == EQ_EXPR)
      && TREE_CODE (arg0) == BIT_AND_EXPR && integer_zerop (arg1)
      && integer_pow2p (TREE_OPERAND (arg0, 1)))
    {
      tree inner = TREE_OPERAND (arg0, 0);
      tree type = TREE_TYPE (arg0);
      int bitnum = tree_log2 (TREE_OPERAND (arg0, 1));
      enum machine_mode operand_mode = TYPE_MODE (type);
      int ops_unsigned;
      tree signed_type, unsigned_type, intermediate_type;
      tree tem;

      /* First, see if we can fold the single bit test into a sign-bit
	 test.  */
      tem = fold_single_bit_test_into_sign_test (code, arg0, arg1,
						 result_type);
      if (tem)
	return tem;

      /* Otherwise we have (A & C) != 0 where C is a single bit,
	 convert that into ((A >> C2) & 1).  Where C2 = log2(C).
	 Similarly for (A & C) == 0.  */

      /* If INNER is a right shift of a constant and it plus BITNUM does
	 not overflow, adjust BITNUM and INNER.  */
      if (TREE_CODE (inner) == RSHIFT_EXPR
	  && TREE_CODE (TREE_OPERAND (inner, 1)) == INTEGER_CST
	  && TREE_INT_CST_HIGH (TREE_OPERAND (inner, 1)) == 0
	  && bitnum < TYPE_PRECISION (type)
	  && 0 > compare_tree_int (TREE_OPERAND (inner, 1),
				   bitnum - TYPE_PRECISION (type)))
	{
	  bitnum += TREE_INT_CST_LOW (TREE_OPERAND (inner, 1));
	  inner = TREE_OPERAND (inner, 0);
	}

      /* If we are going to be able to omit the AND below, we must do our
	 operations as unsigned.  If we must use the AND, we have a choice.
	 Normally unsigned is faster, but for some machines signed is.  */
#ifdef LOAD_EXTEND_OP
      ops_unsigned = (LOAD_EXTEND_OP (operand_mode) == SIGN_EXTEND 
		      && !flag_syntax_only) ? 0 : 1;
#else
      ops_unsigned = 1;
#endif

      signed_type = lang_hooks.types.type_for_mode (operand_mode, 0);
      unsigned_type = lang_hooks.types.type_for_mode (operand_mode, 1);
      intermediate_type = ops_unsigned ? unsigned_type : signed_type;
      inner = fold_convert (intermediate_type, inner);

      if (bitnum != 0)
	inner = build2 (RSHIFT_EXPR, intermediate_type,
			inner, size_int (bitnum));

      if (code == EQ_EXPR)
	inner = fold_build2 (BIT_XOR_EXPR, intermediate_type,
			     inner, integer_one_node);

      /* Put the AND last so it can combine with more things.  */
      inner = build2 (BIT_AND_EXPR, intermediate_type,
		      inner, integer_one_node);

      /* Make sure to return the proper type.  */
      inner = fold_convert (result_type, inner);

      return inner;
    }
  return NULL_TREE;
}

/* Check whether we are allowed to reorder operands arg0 and arg1,
   such that the evaluation of arg1 occurs before arg0.  */

static bool
reorder_operands_p (tree arg0, tree arg1)
{
  if (! flag_evaluation_order)
      return true;
  if (TREE_CONSTANT (arg0) || TREE_CONSTANT (arg1))
    return true;
  return ! TREE_SIDE_EFFECTS (arg0)
	 && ! TREE_SIDE_EFFECTS (arg1);
}

/* Test whether it is preferable two swap two operands, ARG0 and
   ARG1, for example because ARG0 is an integer constant and ARG1
   isn't.  If REORDER is true, only recommend swapping if we can
   evaluate the operands in reverse order.  */

bool
tree_swap_operands_p (tree arg0, tree arg1, bool reorder)
{
  STRIP_SIGN_NOPS (arg0);
  STRIP_SIGN_NOPS (arg1);

  if (TREE_CODE (arg1) == INTEGER_CST)
    return 0;
  if (TREE_CODE (arg0) == INTEGER_CST)
    return 1;

  if (TREE_CODE (arg1) == REAL_CST)
    return 0;
  if (TREE_CODE (arg0) == REAL_CST)
    return 1;

  if (TREE_CODE (arg1) == COMPLEX_CST)
    return 0;
  if (TREE_CODE (arg0) == COMPLEX_CST)
    return 1;

  if (TREE_CONSTANT (arg1))
    return 0;
  if (TREE_CONSTANT (arg0))
    return 1;

  if (optimize_size)
    return 0;

  if (reorder && flag_evaluation_order
      && (TREE_SIDE_EFFECTS (arg0) || TREE_SIDE_EFFECTS (arg1)))
    return 0;

  if (DECL_P (arg1))
    return 0;
  if (DECL_P (arg0))
    return 1;

  /* It is preferable to swap two SSA_NAME to ensure a canonical form
     for commutative and comparison operators.  Ensuring a canonical
     form allows the optimizers to find additional redundancies without
     having to explicitly check for both orderings.  */
  if (TREE_CODE (arg0) == SSA_NAME
      && TREE_CODE (arg1) == SSA_NAME
      && SSA_NAME_VERSION (arg0) > SSA_NAME_VERSION (arg1))
    return 1;

  return 0;
}

/* Fold comparison ARG0 CODE ARG1 (with result in TYPE), where
   ARG0 is extended to a wider type.  */

static tree
fold_widened_comparison (enum tree_code code, tree type, tree arg0, tree arg1)
{
  tree arg0_unw = get_unwidened (arg0, NULL_TREE);
  tree arg1_unw;
  tree shorter_type, outer_type;
  tree min, max;
  bool above, below;

  if (arg0_unw == arg0)
    return NULL_TREE;
  shorter_type = TREE_TYPE (arg0_unw);

#ifdef HAVE_canonicalize_funcptr_for_compare
  /* Disable this optimization if we're casting a function pointer
     type on targets that require function pointer canonicalization.  */
  if (HAVE_canonicalize_funcptr_for_compare
      && TREE_CODE (shorter_type) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (shorter_type)) == FUNCTION_TYPE)
    return NULL_TREE;
#endif

  if (TYPE_PRECISION (TREE_TYPE (arg0)) <= TYPE_PRECISION (shorter_type))
    return NULL_TREE;

  arg1_unw = get_unwidened (arg1, NULL_TREE);

  /* If possible, express the comparison in the shorter mode.  */
  if ((code == EQ_EXPR || code == NE_EXPR
       || TYPE_UNSIGNED (TREE_TYPE (arg0)) == TYPE_UNSIGNED (shorter_type))
      && (TREE_TYPE (arg1_unw) == shorter_type
	  || (TYPE_PRECISION (shorter_type)
	      >= TYPE_PRECISION (TREE_TYPE (arg1_unw)))
	  || (TREE_CODE (arg1_unw) == INTEGER_CST
	      && (TREE_CODE (shorter_type) == INTEGER_TYPE
		  || TREE_CODE (shorter_type) == BOOLEAN_TYPE)
	      && int_fits_type_p (arg1_unw, shorter_type))))
    return fold_build2 (code, type, arg0_unw,
		       fold_convert (shorter_type, arg1_unw));

  if (TREE_CODE (arg1_unw) != INTEGER_CST
      || TREE_CODE (shorter_type) != INTEGER_TYPE
      || !int_fits_type_p (arg1_unw, shorter_type))
    return NULL_TREE;

  /* If we are comparing with the integer that does not fit into the range
     of the shorter type, the result is known.  */
  outer_type = TREE_TYPE (arg1_unw);
  min = lower_bound_in_type (outer_type, shorter_type);
  max = upper_bound_in_type (outer_type, shorter_type);

  above = integer_nonzerop (fold_relational_const (LT_EXPR, type,
						   max, arg1_unw));
  below = integer_nonzerop (fold_relational_const (LT_EXPR, type,
						   arg1_unw, min));

  switch (code)
    {
    case EQ_EXPR:
      if (above || below)
	return omit_one_operand (type, integer_zero_node, arg0);
      break;

    case NE_EXPR:
      if (above || below)
	return omit_one_operand (type, integer_one_node, arg0);
      break;

    case LT_EXPR:
    case LE_EXPR:
      if (above)
	return omit_one_operand (type, integer_one_node, arg0);
      else if (below)
	return omit_one_operand (type, integer_zero_node, arg0);

    case GT_EXPR:
    case GE_EXPR:
      if (above)
	return omit_one_operand (type, integer_zero_node, arg0);
      else if (below)
	return omit_one_operand (type, integer_one_node, arg0);

    default:
      break;
    }

  return NULL_TREE;
}

/* Fold comparison ARG0 CODE ARG1 (with result in TYPE), where for
   ARG0 just the signedness is changed.  */

static tree
fold_sign_changed_comparison (enum tree_code code, tree type,
			      tree arg0, tree arg1)
{
  tree arg0_inner, tmp;
  tree inner_type, outer_type;

  if (TREE_CODE (arg0) != NOP_EXPR
      && TREE_CODE (arg0) != CONVERT_EXPR)
    return NULL_TREE;

  outer_type = TREE_TYPE (arg0);
  arg0_inner = TREE_OPERAND (arg0, 0);
  inner_type = TREE_TYPE (arg0_inner);

#ifdef HAVE_canonicalize_funcptr_for_compare
  /* Disable this optimization if we're casting a function pointer
     type on targets that require function pointer canonicalization.  */
  if (HAVE_canonicalize_funcptr_for_compare
      && TREE_CODE (inner_type) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (inner_type)) == FUNCTION_TYPE)
    return NULL_TREE;
#endif

  if (TYPE_PRECISION (inner_type) != TYPE_PRECISION (outer_type))
    return NULL_TREE;

  if (TREE_CODE (arg1) != INTEGER_CST
      && !((TREE_CODE (arg1) == NOP_EXPR
	    || TREE_CODE (arg1) == CONVERT_EXPR)
	   && TREE_TYPE (TREE_OPERAND (arg1, 0)) == inner_type))
    return NULL_TREE;

  if (TYPE_UNSIGNED (inner_type) != TYPE_UNSIGNED (outer_type)
      && code != NE_EXPR
      && code != EQ_EXPR)
    return NULL_TREE;

  if (TREE_CODE (arg1) == INTEGER_CST)
    {
      tmp = build_int_cst_wide (inner_type,
				TREE_INT_CST_LOW (arg1),
				TREE_INT_CST_HIGH (arg1));
      arg1 = force_fit_type (tmp, 0,
			     TREE_OVERFLOW (arg1),
			     TREE_CONSTANT_OVERFLOW (arg1));
    }
  else
    arg1 = fold_convert (inner_type, arg1);

  return fold_build2 (code, type, arg0_inner, arg1);
}

/* Tries to replace &a[idx] CODE s * delta with &a[idx CODE delta], if s is
   step of the array.  Reconstructs s and delta in the case of s * delta
   being an integer constant (and thus already folded).
   ADDR is the address. MULT is the multiplicative expression.
   If the function succeeds, the new address expression is returned.  Otherwise
   NULL_TREE is returned.  */

static tree
try_move_mult_to_index (enum tree_code code, tree addr, tree op1)
{
  tree s, delta, step;
  tree ref = TREE_OPERAND (addr, 0), pref;
  tree ret, pos;
  tree itype;

  /* Canonicalize op1 into a possibly non-constant delta
     and an INTEGER_CST s.  */
  if (TREE_CODE (op1) == MULT_EXPR)
    {
      tree arg0 = TREE_OPERAND (op1, 0), arg1 = TREE_OPERAND (op1, 1);

      STRIP_NOPS (arg0);
      STRIP_NOPS (arg1);
  
      if (TREE_CODE (arg0) == INTEGER_CST)
        {
          s = arg0;
          delta = arg1;
        }
      else if (TREE_CODE (arg1) == INTEGER_CST)
        {
          s = arg1;
          delta = arg0;
        }
      else
        return NULL_TREE;
    }
  else if (TREE_CODE (op1) == INTEGER_CST)
    {
      delta = op1;
      s = NULL_TREE;
    }
  else
    {
      /* Simulate we are delta * 1.  */
      delta = op1;
      s = integer_one_node;
    }

  for (;; ref = TREE_OPERAND (ref, 0))
    {
      if (TREE_CODE (ref) == ARRAY_REF)
	{
	  itype = TYPE_DOMAIN (TREE_TYPE (TREE_OPERAND (ref, 0)));
	  if (! itype)
	    continue;

	  step = array_ref_element_size (ref);
	  if (TREE_CODE (step) != INTEGER_CST)
	    continue;

	  if (s)
	    {
	      if (! tree_int_cst_equal (step, s))
                continue;
	    }
	  else
	    {
	      /* Try if delta is a multiple of step.  */
	      tree tmp = div_if_zero_remainder (EXACT_DIV_EXPR, delta, step);
	      if (! tmp)
		continue;
	      delta = tmp;
	    }

	  break;
	}

      if (!handled_component_p (ref))
	return NULL_TREE;
    }

  /* We found the suitable array reference.  So copy everything up to it,
     and replace the index.  */

  pref = TREE_OPERAND (addr, 0);
  ret = copy_node (pref);
  pos = ret;

  while (pref != ref)
    {
      pref = TREE_OPERAND (pref, 0);
      TREE_OPERAND (pos, 0) = copy_node (pref);
      pos = TREE_OPERAND (pos, 0);
    }

  TREE_OPERAND (pos, 1) = fold_build2 (code, itype,
				       fold_convert (itype,
						     TREE_OPERAND (pos, 1)),
				       fold_convert (itype, delta));

  return fold_build1 (ADDR_EXPR, TREE_TYPE (addr), ret);
}


/* Fold A < X && A + 1 > Y to A < X && A >= Y.  Normally A + 1 > Y
   means A >= Y && A != MAX, but in this case we know that
   A < X <= MAX.  INEQ is A + 1 > Y, BOUND is A < X.  */

static tree
fold_to_nonsharp_ineq_using_bound (tree ineq, tree bound)
{
  tree a, typea, type = TREE_TYPE (ineq), a1, diff, y;

  if (TREE_CODE (bound) == LT_EXPR)
    a = TREE_OPERAND (bound, 0);
  else if (TREE_CODE (bound) == GT_EXPR)
    a = TREE_OPERAND (bound, 1);
  else
    return NULL_TREE;

  typea = TREE_TYPE (a);
  if (!INTEGRAL_TYPE_P (typea)
      && !POINTER_TYPE_P (typea))
    return NULL_TREE;

  if (TREE_CODE (ineq) == LT_EXPR)
    {
      a1 = TREE_OPERAND (ineq, 1);
      y = TREE_OPERAND (ineq, 0);
    }
  else if (TREE_CODE (ineq) == GT_EXPR)
    {
      a1 = TREE_OPERAND (ineq, 0);
      y = TREE_OPERAND (ineq, 1);
    }
  else
    return NULL_TREE;

  if (TREE_TYPE (a1) != typea)
    return NULL_TREE;

  diff = fold_build2 (MINUS_EXPR, typea, a1, a);
  if (!integer_onep (diff))
    return NULL_TREE;

  return fold_build2 (GE_EXPR, type, a, y);
}

/* Fold a sum or difference of at least one multiplication.
   Returns the folded tree or NULL if no simplification could be made.  */

static tree
fold_plusminus_mult_expr (enum tree_code code, tree type, tree arg0, tree arg1)
{
  tree arg00, arg01, arg10, arg11;
  tree alt0 = NULL_TREE, alt1 = NULL_TREE, same;

  /* (A * C) +- (B * C) -> (A+-B) * C.
     (A * C) +- A -> A * (C+-1).
     We are most concerned about the case where C is a constant,
     but other combinations show up during loop reduction.  Since
     it is not difficult, try all four possibilities.  */

  if (TREE_CODE (arg0) == MULT_EXPR)
    {
      arg00 = TREE_OPERAND (arg0, 0);
      arg01 = TREE_OPERAND (arg0, 1);
    }
  else
    {
      arg00 = arg0;
      arg01 = build_one_cst (type);
    }
  if (TREE_CODE (arg1) == MULT_EXPR)
    {
      arg10 = TREE_OPERAND (arg1, 0);
      arg11 = TREE_OPERAND (arg1, 1);
    }
  else
    {
      arg10 = arg1;
      arg11 = build_one_cst (type);
    }
  same = NULL_TREE;

  if (operand_equal_p (arg01, arg11, 0))
    same = arg01, alt0 = arg00, alt1 = arg10;
  else if (operand_equal_p (arg00, arg10, 0))
    same = arg00, alt0 = arg01, alt1 = arg11;
  else if (operand_equal_p (arg00, arg11, 0))
    same = arg00, alt0 = arg01, alt1 = arg10;
  else if (operand_equal_p (arg01, arg10, 0))
    same = arg01, alt0 = arg00, alt1 = arg11;

  /* No identical multiplicands; see if we can find a common
     power-of-two factor in non-power-of-two multiplies.  This
     can help in multi-dimensional array access.  */
  else if (host_integerp (arg01, 0)
	   && host_integerp (arg11, 0))
    {
      HOST_WIDE_INT int01, int11, tmp;
      bool swap = false;
      tree maybe_same;
      int01 = TREE_INT_CST_LOW (arg01);
      int11 = TREE_INT_CST_LOW (arg11);

      /* Move min of absolute values to int11.  */
      if ((int01 >= 0 ? int01 : -int01)
	  < (int11 >= 0 ? int11 : -int11))
        {
	  tmp = int01, int01 = int11, int11 = tmp;
	  alt0 = arg00, arg00 = arg10, arg10 = alt0;
	  maybe_same = arg01;
	  swap = true;
	}
      else
	maybe_same = arg11;

      if (exact_log2 (int11) > 0 && int01 % int11 == 0)
        {
	  alt0 = fold_build2 (MULT_EXPR, TREE_TYPE (arg00), arg00,
			      build_int_cst (TREE_TYPE (arg00),
					     int01 / int11));
	  alt1 = arg10;
	  same = maybe_same;
	  if (swap)
	    maybe_same = alt0, alt0 = alt1, alt1 = maybe_same;
	}
    }

  if (same)
    return fold_build2 (MULT_EXPR, type,
			fold_build2 (code, type,
				     fold_convert (type, alt0),
				     fold_convert (type, alt1)),
			fold_convert (type, same));

  return NULL_TREE;
}

/* Subroutine of native_encode_expr.  Encode the INTEGER_CST
   specified by EXPR into the buffer PTR of length LEN bytes.
   Return the number of bytes placed in the buffer, or zero
   upon failure.  */

static int
native_encode_int (tree expr, unsigned char *ptr, int len)
{
  tree type = TREE_TYPE (expr);
  int total_bytes = GET_MODE_SIZE (TYPE_MODE (type));
  int byte, offset, word, words;
  unsigned char value;

  if (total_bytes > len)
    return 0;
  words = total_bytes / UNITS_PER_WORD;

  for (byte = 0; byte < total_bytes; byte++)
    {
      int bitpos = byte * BITS_PER_UNIT;
      if (bitpos < HOST_BITS_PER_WIDE_INT)
	value = (unsigned char) (TREE_INT_CST_LOW (expr) >> bitpos);
      else
	value = (unsigned char) (TREE_INT_CST_HIGH (expr)
				 >> (bitpos - HOST_BITS_PER_WIDE_INT));

      if (total_bytes > UNITS_PER_WORD)
	{
	  word = byte / UNITS_PER_WORD;
	  if (WORDS_BIG_ENDIAN)
	    word = (words - 1) - word;
	  offset = word * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    offset += (UNITS_PER_WORD - 1) - (byte % UNITS_PER_WORD);
	  else
	    offset += byte % UNITS_PER_WORD;
	}
      else
	offset = BYTES_BIG_ENDIAN ? (total_bytes - 1) - byte : byte;
      ptr[offset] = value;
    }
  return total_bytes;
}


/* Subroutine of native_encode_expr.  Encode the REAL_CST
   specified by EXPR into the buffer PTR of length LEN bytes.
   Return the number of bytes placed in the buffer, or zero
   upon failure.  */

static int
native_encode_real (tree expr, unsigned char *ptr, int len)
{
  tree type = TREE_TYPE (expr);
  int total_bytes = GET_MODE_SIZE (TYPE_MODE (type));
  int byte, offset, word, words, bitpos;
  unsigned char value;

  /* There are always 32 bits in each long, no matter the size of
     the hosts long.  We handle floating point representations with
     up to 192 bits.  */
  long tmp[6];

  if (total_bytes > len)
    return 0;
  words = 32 / UNITS_PER_WORD;

  real_to_target (tmp, TREE_REAL_CST_PTR (expr), TYPE_MODE (type));

  for (bitpos = 0; bitpos < total_bytes * BITS_PER_UNIT;
       bitpos += BITS_PER_UNIT)
    {
      byte = (bitpos / BITS_PER_UNIT) & 3;
      value = (unsigned char) (tmp[bitpos / 32] >> (bitpos & 31));

      if (UNITS_PER_WORD < 4)
	{
	  word = byte / UNITS_PER_WORD;
	  if (WORDS_BIG_ENDIAN)
	    word = (words - 1) - word;
	  offset = word * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    offset += (UNITS_PER_WORD - 1) - (byte % UNITS_PER_WORD);
	  else
	    offset += byte % UNITS_PER_WORD;
	}
      else
	offset = BYTES_BIG_ENDIAN ? 3 - byte : byte;
      ptr[offset + ((bitpos / BITS_PER_UNIT) & ~3)] = value;
    }
  return total_bytes;
}

/* Subroutine of native_encode_expr.  Encode the COMPLEX_CST
   specified by EXPR into the buffer PTR of length LEN bytes.
   Return the number of bytes placed in the buffer, or zero
   upon failure.  */

static int
native_encode_complex (tree expr, unsigned char *ptr, int len)
{
  int rsize, isize;
  tree part;

  part = TREE_REALPART (expr);
  rsize = native_encode_expr (part, ptr, len);
  if (rsize == 0)
    return 0;
  part = TREE_IMAGPART (expr);
  isize = native_encode_expr (part, ptr+rsize, len-rsize);
  if (isize != rsize)
    return 0;
  return rsize + isize;
}


/* Subroutine of native_encode_expr.  Encode the VECTOR_CST
   specified by EXPR into the buffer PTR of length LEN bytes.
   Return the number of bytes placed in the buffer, or zero
   upon failure.  */

static int
native_encode_vector (tree expr, unsigned char *ptr, int len)
{
  int i, size, offset, count;
  tree itype, elem, elements;

  offset = 0;
  elements = TREE_VECTOR_CST_ELTS (expr);
  count = TYPE_VECTOR_SUBPARTS (TREE_TYPE (expr));
  itype = TREE_TYPE (TREE_TYPE (expr));
  size = GET_MODE_SIZE (TYPE_MODE (itype));
  for (i = 0; i < count; i++)
    {
      if (elements)
	{
	  elem = TREE_VALUE (elements);
	  elements = TREE_CHAIN (elements);
	}
      else
	elem = NULL_TREE;

      if (elem)
	{
	  if (native_encode_expr (elem, ptr+offset, len-offset) != size)
	    return 0;
	}
      else
	{
	  if (offset + size > len)
	    return 0;
	  memset (ptr+offset, 0, size);
	}
      offset += size;
    }
  return offset;
}


/* Subroutine of fold_view_convert_expr.  Encode the INTEGER_CST,
   REAL_CST, COMPLEX_CST or VECTOR_CST specified by EXPR into the
   buffer PTR of length LEN bytes.  Return the number of bytes
   placed in the buffer, or zero upon failure.  */

static int
native_encode_expr (tree expr, unsigned char *ptr, int len)
{
  switch (TREE_CODE (expr))
    {
    case INTEGER_CST:
      return native_encode_int (expr, ptr, len);

    case REAL_CST:
      return native_encode_real (expr, ptr, len);

    case COMPLEX_CST:
      return native_encode_complex (expr, ptr, len);

    case VECTOR_CST:
      return native_encode_vector (expr, ptr, len);

    default:
      return 0;
    }
}


/* Subroutine of native_interpret_expr.  Interpret the contents of
   the buffer PTR of length LEN as an INTEGER_CST of type TYPE.
   If the buffer cannot be interpreted, return NULL_TREE.  */

static tree
native_interpret_int (tree type, unsigned char *ptr, int len)
{
  int total_bytes = GET_MODE_SIZE (TYPE_MODE (type));
  int byte, offset, word, words;
  unsigned char value;
  unsigned int HOST_WIDE_INT lo = 0;
  HOST_WIDE_INT hi = 0;

  if (total_bytes > len)
    return NULL_TREE;
  if (total_bytes * BITS_PER_UNIT > 2 * HOST_BITS_PER_WIDE_INT)
    return NULL_TREE;
  words = total_bytes / UNITS_PER_WORD;

  for (byte = 0; byte < total_bytes; byte++)
    {
      int bitpos = byte * BITS_PER_UNIT;
      if (total_bytes > UNITS_PER_WORD)
	{
	  word = byte / UNITS_PER_WORD;
	  if (WORDS_BIG_ENDIAN)
	    word = (words - 1) - word;
	  offset = word * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    offset += (UNITS_PER_WORD - 1) - (byte % UNITS_PER_WORD);
	  else
	    offset += byte % UNITS_PER_WORD;
	}
      else
	offset = BYTES_BIG_ENDIAN ? (total_bytes - 1) - byte : byte;
      value = ptr[offset];

      if (bitpos < HOST_BITS_PER_WIDE_INT)
	lo |= (unsigned HOST_WIDE_INT) value << bitpos;
      else
	hi |= (unsigned HOST_WIDE_INT) value
	      << (bitpos - HOST_BITS_PER_WIDE_INT);
    }

  return force_fit_type (build_int_cst_wide (type, lo, hi),
			 0, false, false);
}


/* Subroutine of native_interpret_expr.  Interpret the contents of
   the buffer PTR of length LEN as a REAL_CST of type TYPE.
   If the buffer cannot be interpreted, return NULL_TREE.  */

static tree
native_interpret_real (tree type, unsigned char *ptr, int len)
{
  enum machine_mode mode = TYPE_MODE (type);
  int total_bytes = GET_MODE_SIZE (mode);
  int byte, offset, word, words, bitpos;
  unsigned char value;
  /* There are always 32 bits in each long, no matter the size of
     the hosts long.  We handle floating point representations with
     up to 192 bits.  */
  REAL_VALUE_TYPE r;
  long tmp[6];

  total_bytes = GET_MODE_SIZE (TYPE_MODE (type));
  if (total_bytes > len || total_bytes > 24)
    return NULL_TREE;
  words = 32 / UNITS_PER_WORD;

  memset (tmp, 0, sizeof (tmp));
  for (bitpos = 0; bitpos < total_bytes * BITS_PER_UNIT;
       bitpos += BITS_PER_UNIT)
    {
      byte = (bitpos / BITS_PER_UNIT) & 3;
      if (UNITS_PER_WORD < 4)
	{
	  word = byte / UNITS_PER_WORD;
	  if (WORDS_BIG_ENDIAN)
	    word = (words - 1) - word;
	  offset = word * UNITS_PER_WORD;
	  if (BYTES_BIG_ENDIAN)
	    offset += (UNITS_PER_WORD - 1) - (byte % UNITS_PER_WORD);
	  else
	    offset += byte % UNITS_PER_WORD;
	}
      else
	offset = BYTES_BIG_ENDIAN ? 3 - byte : byte;
      value = ptr[offset + ((bitpos / BITS_PER_UNIT) & ~3)];

      tmp[bitpos / 32] |= (unsigned long)value << (bitpos & 31);
    }

  real_from_target (&r, tmp, mode);
  return build_real (type, r);
}


/* Subroutine of native_interpret_expr.  Interpret the contents of
   the buffer PTR of length LEN as a COMPLEX_CST of type TYPE.
   If the buffer cannot be interpreted, return NULL_TREE.  */

static tree
native_interpret_complex (tree type, unsigned char *ptr, int len)
{
  tree etype, rpart, ipart;
  int size;

  etype = TREE_TYPE (type);
  size = GET_MODE_SIZE (TYPE_MODE (etype));
  if (size * 2 > len)
    return NULL_TREE;
  rpart = native_interpret_expr (etype, ptr, size);
  if (!rpart)
    return NULL_TREE;
  ipart = native_interpret_expr (etype, ptr+size, size);
  if (!ipart)
    return NULL_TREE;
  return build_complex (type, rpart, ipart);
}


/* Subroutine of native_interpret_expr.  Interpret the contents of
   the buffer PTR of length LEN as a VECTOR_CST of type TYPE.
   If the buffer cannot be interpreted, return NULL_TREE.  */

static tree
native_interpret_vector (tree type, unsigned char *ptr, int len)
{
  tree etype, elem, elements;
  int i, size, count;

  etype = TREE_TYPE (type);
  size = GET_MODE_SIZE (TYPE_MODE (etype));
  count = TYPE_VECTOR_SUBPARTS (type);
  if (size * count > len)
    return NULL_TREE;

  elements = NULL_TREE;
  for (i = count - 1; i >= 0; i--)
    {
      elem = native_interpret_expr (etype, ptr+(i*size), size);
      if (!elem)
	return NULL_TREE;
      elements = tree_cons (NULL_TREE, elem, elements);
    }
  return build_vector (type, elements);
}


/* Subroutine of fold_view_convert_expr.  Interpret the contents of
   the buffer PTR of length LEN as a constant of type TYPE.  For
   INTEGRAL_TYPE_P we return an INTEGER_CST, for SCALAR_FLOAT_TYPE_P
   we return a REAL_CST, etc...  If the buffer cannot be interpreted,
   return NULL_TREE.  */

static tree
native_interpret_expr (tree type, unsigned char *ptr, int len)
{
  switch (TREE_CODE (type))
    {
    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      return native_interpret_int (type, ptr, len);

    case REAL_TYPE:
      return native_interpret_real (type, ptr, len);

    case COMPLEX_TYPE:
      return native_interpret_complex (type, ptr, len);

    case VECTOR_TYPE:
      return native_interpret_vector (type, ptr, len);

    default:
      return NULL_TREE;
    }
}


/* Fold a VIEW_CONVERT_EXPR of a constant expression EXPR to type
   TYPE at compile-time.  If we're unable to perform the conversion
   return NULL_TREE.  */

static tree
fold_view_convert_expr (tree type, tree expr)
{
  /* We support up to 512-bit values (for V8DFmode).  */
  unsigned char buffer[64];
  int len;

  /* Check that the host and target are sane.  */
  if (CHAR_BIT != 8 || BITS_PER_UNIT != 8)
    return NULL_TREE;

  len = native_encode_expr (expr, buffer, sizeof (buffer));
  if (len == 0)
    return NULL_TREE;

  return native_interpret_expr (type, buffer, len);
}


/* Fold a unary expression of code CODE and type TYPE with operand
   OP0.  Return the folded expression if folding is successful.
   Otherwise, return NULL_TREE.  */

tree
fold_unary (enum tree_code code, tree type, tree op0)
{
  tree tem;
  tree arg0;
  enum tree_code_class kind = TREE_CODE_CLASS (code);

  gcc_assert (IS_EXPR_CODE_CLASS (kind)
	      && TREE_CODE_LENGTH (code) == 1);

  arg0 = op0;
  if (arg0)
    {
      if (code == NOP_EXPR || code == CONVERT_EXPR
	  || code == FLOAT_EXPR || code == ABS_EXPR)
	{
	  /* Don't use STRIP_NOPS, because signedness of argument type
	     matters.  */
	  STRIP_SIGN_NOPS (arg0);
	}
      else
	{
	  /* Strip any conversions that don't change the mode.  This
	     is safe for every expression, except for a comparison
	     expression because its signedness is derived from its
	     operands.

	     Note that this is done as an internal manipulation within
	     the constant folder, in order to find the simplest
	     representation of the arguments so that their form can be
	     studied.  In any cases, the appropriate type conversions
	     should be put back in the tree that will get out of the
	     constant folder.  */
	  STRIP_NOPS (arg0);
	}
    }

  if (TREE_CODE_CLASS (code) == tcc_unary)
    {
      if (TREE_CODE (arg0) == COMPOUND_EXPR)
	return build2 (COMPOUND_EXPR, type, TREE_OPERAND (arg0, 0),
		       fold_build1 (code, type, TREE_OPERAND (arg0, 1)));
      else if (TREE_CODE (arg0) == COND_EXPR)
	{
	  tree arg01 = TREE_OPERAND (arg0, 1);
	  tree arg02 = TREE_OPERAND (arg0, 2);
	  if (! VOID_TYPE_P (TREE_TYPE (arg01)))
	    arg01 = fold_build1 (code, type, arg01);
	  if (! VOID_TYPE_P (TREE_TYPE (arg02)))
	    arg02 = fold_build1 (code, type, arg02);
	  tem = fold_build3 (COND_EXPR, type, TREE_OPERAND (arg0, 0),
			     arg01, arg02);

	  /* If this was a conversion, and all we did was to move into
	     inside the COND_EXPR, bring it back out.  But leave it if
	     it is a conversion from integer to integer and the
	     result precision is no wider than a word since such a
	     conversion is cheap and may be optimized away by combine,
	     while it couldn't if it were outside the COND_EXPR.  Then return
	     so we don't get into an infinite recursion loop taking the
	     conversion out and then back in.  */

	  if ((code == NOP_EXPR || code == CONVERT_EXPR
	       || code == NON_LVALUE_EXPR)
	      && TREE_CODE (tem) == COND_EXPR
	      && TREE_CODE (TREE_OPERAND (tem, 1)) == code
	      && TREE_CODE (TREE_OPERAND (tem, 2)) == code
	      && ! VOID_TYPE_P (TREE_OPERAND (tem, 1))
	      && ! VOID_TYPE_P (TREE_OPERAND (tem, 2))
	      && (TREE_TYPE (TREE_OPERAND (TREE_OPERAND (tem, 1), 0))
		  == TREE_TYPE (TREE_OPERAND (TREE_OPERAND (tem, 2), 0)))
	      && (! (INTEGRAL_TYPE_P (TREE_TYPE (tem))
		     && (INTEGRAL_TYPE_P
			 (TREE_TYPE (TREE_OPERAND (TREE_OPERAND (tem, 1), 0))))
		     && TYPE_PRECISION (TREE_TYPE (tem)) <= BITS_PER_WORD)
		  || flag_syntax_only))
	    tem = build1 (code, type,
			  build3 (COND_EXPR,
				  TREE_TYPE (TREE_OPERAND
					     (TREE_OPERAND (tem, 1), 0)),
				  TREE_OPERAND (tem, 0),
				  TREE_OPERAND (TREE_OPERAND (tem, 1), 0),
				  TREE_OPERAND (TREE_OPERAND (tem, 2), 0)));
	  return tem;
	}
      else if (COMPARISON_CLASS_P (arg0))
	{
	  if (TREE_CODE (type) == BOOLEAN_TYPE)
	    {
	      arg0 = copy_node (arg0);
	      TREE_TYPE (arg0) = type;
	      return arg0;
	    }
	  else if (TREE_CODE (type) != INTEGER_TYPE)
	    return fold_build3 (COND_EXPR, type, arg0,
				fold_build1 (code, type,
					     integer_one_node),
				fold_build1 (code, type,
					     integer_zero_node));
	}
   }

  switch (code)
    {
    case NOP_EXPR:
    case FLOAT_EXPR:
    case CONVERT_EXPR:
    case FIX_TRUNC_EXPR:
    case FIX_CEIL_EXPR:
    case FIX_FLOOR_EXPR:
    case FIX_ROUND_EXPR:
      if (TREE_TYPE (op0) == type)
	return op0;
      
      /* If we have (type) (a CMP b) and type is an integral type, return
         new expression involving the new type.  */
      if (COMPARISON_CLASS_P (op0) && INTEGRAL_TYPE_P (type))
	return fold_build2 (TREE_CODE (op0), type, TREE_OPERAND (op0, 0),
			    TREE_OPERAND (op0, 1));

      /* Handle cases of two conversions in a row.  */
      if (TREE_CODE (op0) == NOP_EXPR
	  || TREE_CODE (op0) == CONVERT_EXPR)
	{
	  tree inside_type = TREE_TYPE (TREE_OPERAND (op0, 0));
	  tree inter_type = TREE_TYPE (op0);
	  int inside_int = INTEGRAL_TYPE_P (inside_type);
	  int inside_ptr = POINTER_TYPE_P (inside_type);
	  int inside_float = FLOAT_TYPE_P (inside_type);
	  int inside_vec = TREE_CODE (inside_type) == VECTOR_TYPE;
	  unsigned int inside_prec = TYPE_PRECISION (inside_type);
	  int inside_unsignedp = TYPE_UNSIGNED (inside_type);
	  int inter_int = INTEGRAL_TYPE_P (inter_type);
	  int inter_ptr = POINTER_TYPE_P (inter_type);
	  int inter_float = FLOAT_TYPE_P (inter_type);
	  int inter_vec = TREE_CODE (inter_type) == VECTOR_TYPE;
	  unsigned int inter_prec = TYPE_PRECISION (inter_type);
	  int inter_unsignedp = TYPE_UNSIGNED (inter_type);
	  int final_int = INTEGRAL_TYPE_P (type);
	  int final_ptr = POINTER_TYPE_P (type);
	  int final_float = FLOAT_TYPE_P (type);
	  int final_vec = TREE_CODE (type) == VECTOR_TYPE;
	  unsigned int final_prec = TYPE_PRECISION (type);
	  int final_unsignedp = TYPE_UNSIGNED (type);

	  /* In addition to the cases of two conversions in a row
	     handled below, if we are converting something to its own
	     type via an object of identical or wider precision, neither
	     conversion is needed.  */
	  if (TYPE_MAIN_VARIANT (inside_type) == TYPE_MAIN_VARIANT (type)
	      && (((inter_int || inter_ptr) && final_int)
		  || (inter_float && final_float))
	      && inter_prec >= final_prec)
	    return fold_build1 (code, type, TREE_OPERAND (op0, 0));

	  /* Likewise, if the intermediate and final types are either both
	     float or both integer, we don't need the middle conversion if
	     it is wider than the final type and doesn't change the signedness
	     (for integers).  Avoid this if the final type is a pointer
	     since then we sometimes need the inner conversion.  Likewise if
	     the outer has a precision not equal to the size of its mode.  */
	  if ((((inter_int || inter_ptr) && (inside_int || inside_ptr))
	       || (inter_float && inside_float)
	       || (inter_vec && inside_vec))
	      && inter_prec >= inside_prec
	      && (inter_float || inter_vec
		  || inter_unsignedp == inside_unsignedp)
	      && ! (final_prec != GET_MODE_BITSIZE (TYPE_MODE (type))
		    && TYPE_MODE (type) == TYPE_MODE (inter_type))
	      && ! final_ptr
	      && (! final_vec || inter_prec == inside_prec))
	    return fold_build1 (code, type, TREE_OPERAND (op0, 0));

	  /* If we have a sign-extension of a zero-extended value, we can
	     replace that by a single zero-extension.  */
	  if (inside_int && inter_int && final_int
	      && inside_prec < inter_prec && inter_prec < final_prec
	      && inside_unsignedp && !inter_unsignedp)
	    return fold_build1 (code, type, TREE_OPERAND (op0, 0));

	  /* Two conversions in a row are not needed unless:
	     - some conversion is floating-point (overstrict for now), or
	     - some conversion is a vector (overstrict for now), or
	     - the intermediate type is narrower than both initial and
	       final, or
	     - the intermediate type and innermost type differ in signedness,
	       and the outermost type is wider than the intermediate, or
	     - the initial type is a pointer type and the precisions of the
	       intermediate and final types differ, or
	     - the final type is a pointer type and the precisions of the
	       initial and intermediate types differ.
	     - the final type is a pointer type and the initial type not
	     - the initial type is a pointer to an array and the final type
	       not.  */
	  /* Java pointer type conversions generate checks in some
	     cases, so we explicitly disallow this optimization.  */
	  if (! inside_float && ! inter_float && ! final_float
	      && ! inside_vec && ! inter_vec && ! final_vec
	      && (inter_prec >= inside_prec || inter_prec >= final_prec)
	      && ! (inside_int && inter_int
		    && inter_unsignedp != inside_unsignedp
		    && inter_prec < final_prec)
	      && ((inter_unsignedp && inter_prec > inside_prec)
		  == (final_unsignedp && final_prec > inter_prec))
	      && ! (inside_ptr && inter_prec != final_prec)
	      && ! (final_ptr && inside_prec != inter_prec)
	      && ! (final_prec != GET_MODE_BITSIZE (TYPE_MODE (type))
		    && TYPE_MODE (type) == TYPE_MODE (inter_type))
	      && final_ptr == inside_ptr
	      && ! (inside_ptr
		    && TREE_CODE (TREE_TYPE (inside_type)) == ARRAY_TYPE
		    && TREE_CODE (TREE_TYPE (type)) != ARRAY_TYPE)
	      && ! ((strcmp (lang_hooks.name, "GNU Java") == 0)
		    && final_ptr))
	    return fold_build1 (code, type, TREE_OPERAND (op0, 0));
	}

      /* Handle (T *)&A.B.C for A being of type T and B and C
	 living at offset zero.  This occurs frequently in
	 C++ upcasting and then accessing the base.  */
      if (TREE_CODE (op0) == ADDR_EXPR
	  && POINTER_TYPE_P (type)
	  && handled_component_p (TREE_OPERAND (op0, 0)))
        {
	  HOST_WIDE_INT bitsize, bitpos;
	  tree offset;
	  enum machine_mode mode;
	  int unsignedp, volatilep;
          tree base = TREE_OPERAND (op0, 0);
	  base = get_inner_reference (base, &bitsize, &bitpos, &offset,
				      &mode, &unsignedp, &volatilep, false);
	  /* If the reference was to a (constant) zero offset, we can use
	     the address of the base if it has the same base type
	     as the result type.  */
	  if (! offset && bitpos == 0
	      && TYPE_MAIN_VARIANT (TREE_TYPE (type))
		  == TYPE_MAIN_VARIANT (TREE_TYPE (base)))
	    return fold_convert (type, build_fold_addr_expr (base));
        }

      if (TREE_CODE (op0) == MODIFY_EXPR
	  && TREE_CONSTANT (TREE_OPERAND (op0, 1))
	  /* Detect assigning a bitfield.  */
	  && !(TREE_CODE (TREE_OPERAND (op0, 0)) == COMPONENT_REF
	       && DECL_BIT_FIELD (TREE_OPERAND (TREE_OPERAND (op0, 0), 1))))
	{
	  /* Don't leave an assignment inside a conversion
	     unless assigning a bitfield.  */
	  tem = fold_build1 (code, type, TREE_OPERAND (op0, 1));
	  /* First do the assignment, then return converted constant.  */
	  tem = build2 (COMPOUND_EXPR, TREE_TYPE (tem), op0, tem);
	  TREE_NO_WARNING (tem) = 1;
	  TREE_USED (tem) = 1;
	  return tem;
	}

      /* Convert (T)(x & c) into (T)x & (T)c, if c is an integer
	 constants (if x has signed type, the sign bit cannot be set
	 in c).  This folds extension into the BIT_AND_EXPR.  */
      if (INTEGRAL_TYPE_P (type)
	  && TREE_CODE (type) != BOOLEAN_TYPE
	  && TREE_CODE (op0) == BIT_AND_EXPR
	  && TREE_CODE (TREE_OPERAND (op0, 1)) == INTEGER_CST)
	{
	  tree and = op0;
	  tree and0 = TREE_OPERAND (and, 0), and1 = TREE_OPERAND (and, 1);
	  int change = 0;

	  if (TYPE_UNSIGNED (TREE_TYPE (and))
	      || (TYPE_PRECISION (type)
		  <= TYPE_PRECISION (TREE_TYPE (and))))
	    change = 1;
	  else if (TYPE_PRECISION (TREE_TYPE (and1))
		   <= HOST_BITS_PER_WIDE_INT
		   && host_integerp (and1, 1))
	    {
	      unsigned HOST_WIDE_INT cst;

	      cst = tree_low_cst (and1, 1);
	      cst &= (HOST_WIDE_INT) -1
		     << (TYPE_PRECISION (TREE_TYPE (and1)) - 1);
	      change = (cst == 0);
#ifdef LOAD_EXTEND_OP
	      if (change
		  && !flag_syntax_only
		  && (LOAD_EXTEND_OP (TYPE_MODE (TREE_TYPE (and0)))
		      == ZERO_EXTEND))
		{
		  tree uns = lang_hooks.types.unsigned_type (TREE_TYPE (and0));
		  and0 = fold_convert (uns, and0);
		  and1 = fold_convert (uns, and1);
		}
#endif
	    }
	  if (change)
	    {
	      tem = build_int_cst_wide (type, TREE_INT_CST_LOW (and1),
					TREE_INT_CST_HIGH (and1));
	      tem = force_fit_type (tem, 0, TREE_OVERFLOW (and1),
				    TREE_CONSTANT_OVERFLOW (and1));
	      return fold_build2 (BIT_AND_EXPR, type,
				  fold_convert (type, and0), tem);
	    }
	}

      /* Convert (T1)((T2)X op Y) into (T1)X op Y, for pointer types T1 and
	 T2 being pointers to types of the same size.  */
      if (POINTER_TYPE_P (type)
	  && BINARY_CLASS_P (arg0)
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == NOP_EXPR
	  && POINTER_TYPE_P (TREE_TYPE (TREE_OPERAND (arg0, 0))))
	{
	  tree arg00 = TREE_OPERAND (arg0, 0);
	  tree t0 = type;
	  tree t1 = TREE_TYPE (arg00);
	  tree tt0 = TREE_TYPE (t0);
	  tree tt1 = TREE_TYPE (t1);
	  tree s0 = TYPE_SIZE (tt0);
	  tree s1 = TYPE_SIZE (tt1);

	  if (s0 && s1 && operand_equal_p (s0, s1, OEP_ONLY_CONST))
	    return build2 (TREE_CODE (arg0), t0, fold_convert (t0, arg00),
			   TREE_OPERAND (arg0, 1));
	}

      /* Convert (T1)(~(T2)X) into ~(T1)X if T1 and T2 are integral types
	 of the same precision, and X is a integer type not narrower than
	 types T1 or T2, i.e. the cast (T2)X isn't an extension.  */
      if (INTEGRAL_TYPE_P (type)
	  && TREE_CODE (op0) == BIT_NOT_EXPR
	  && INTEGRAL_TYPE_P (TREE_TYPE (op0))
	  && (TREE_CODE (TREE_OPERAND (op0, 0)) == NOP_EXPR
	      || TREE_CODE (TREE_OPERAND (op0, 0)) == CONVERT_EXPR)
	  && TYPE_PRECISION (type) == TYPE_PRECISION (TREE_TYPE (op0)))
	{
	  tem = TREE_OPERAND (TREE_OPERAND (op0, 0), 0);
	  if (INTEGRAL_TYPE_P (TREE_TYPE (tem))
	      && TYPE_PRECISION (type) <= TYPE_PRECISION (TREE_TYPE (tem)))
	    return fold_build1 (BIT_NOT_EXPR, type, fold_convert (type, tem));
	}

      tem = fold_convert_const (code, type, op0);
      return tem ? tem : NULL_TREE;

    case VIEW_CONVERT_EXPR:
      if (TREE_CODE (op0) == VIEW_CONVERT_EXPR)
	return fold_build1 (VIEW_CONVERT_EXPR, type, TREE_OPERAND (op0, 0));
      return fold_view_convert_expr (type, op0);

    case NEGATE_EXPR:
      tem = fold_negate_expr (arg0);
      if (tem)
	return fold_convert (type, tem);
      return NULL_TREE;

    case ABS_EXPR:
      if (TREE_CODE (arg0) == INTEGER_CST || TREE_CODE (arg0) == REAL_CST)
	return fold_abs_const (arg0, type);
      else if (TREE_CODE (arg0) == NEGATE_EXPR)
	return fold_build1 (ABS_EXPR, type, TREE_OPERAND (arg0, 0));
      /* Convert fabs((double)float) into (double)fabsf(float).  */
      else if (TREE_CODE (arg0) == NOP_EXPR
	       && TREE_CODE (type) == REAL_TYPE)
	{
	  tree targ0 = strip_float_extensions (arg0);
	  if (targ0 != arg0)
	    return fold_convert (type, fold_build1 (ABS_EXPR,
						    TREE_TYPE (targ0),
						    targ0));
	}
      /* ABS_EXPR<ABS_EXPR<x>> = ABS_EXPR<x> even if flag_wrapv is on.  */
      else if (TREE_CODE (arg0) == ABS_EXPR)
	return arg0;
      else if (tree_expr_nonnegative_p (arg0))
	return arg0;

      /* Strip sign ops from argument.  */
      if (TREE_CODE (type) == REAL_TYPE)
	{
	  tem = fold_strip_sign_ops (arg0);
	  if (tem)
	    return fold_build1 (ABS_EXPR, type, fold_convert (type, tem));
	}
      return NULL_TREE;

    case CONJ_EXPR:
      if (TREE_CODE (TREE_TYPE (arg0)) != COMPLEX_TYPE)
	return fold_convert (type, arg0);
      if (TREE_CODE (arg0) == COMPLEX_EXPR)
	{
	  tree itype = TREE_TYPE (type);
	  tree rpart = fold_convert (itype, TREE_OPERAND (arg0, 0));
	  tree ipart = fold_convert (itype, TREE_OPERAND (arg0, 1));
	  return fold_build2 (COMPLEX_EXPR, type, rpart, negate_expr (ipart));
	}
      if (TREE_CODE (arg0) == COMPLEX_CST)
	{
	  tree itype = TREE_TYPE (type);
	  tree rpart = fold_convert (itype, TREE_REALPART (arg0));
	  tree ipart = fold_convert (itype, TREE_IMAGPART (arg0));
	  return build_complex (type, rpart, negate_expr (ipart));
	}
      if (TREE_CODE (arg0) == CONJ_EXPR)
	return fold_convert (type, TREE_OPERAND (arg0, 0));
      return NULL_TREE;

    case BIT_NOT_EXPR:
      if (TREE_CODE (arg0) == INTEGER_CST)
        return fold_not_const (arg0, type);
      else if (TREE_CODE (arg0) == BIT_NOT_EXPR)
	return TREE_OPERAND (arg0, 0);
      /* Convert ~ (-A) to A - 1.  */
      else if (INTEGRAL_TYPE_P (type) && TREE_CODE (arg0) == NEGATE_EXPR)
	return fold_build2 (MINUS_EXPR, type, TREE_OPERAND (arg0, 0),
			    build_int_cst (type, 1));
      /* Convert ~ (A - 1) or ~ (A + -1) to -A.  */
      else if (INTEGRAL_TYPE_P (type)
	       && ((TREE_CODE (arg0) == MINUS_EXPR
		    && integer_onep (TREE_OPERAND (arg0, 1)))
		   || (TREE_CODE (arg0) == PLUS_EXPR
		       && integer_all_onesp (TREE_OPERAND (arg0, 1)))))
	return fold_build1 (NEGATE_EXPR, type, TREE_OPERAND (arg0, 0));
      /* Convert ~(X ^ Y) to ~X ^ Y or X ^ ~Y if ~X or ~Y simplify.  */
      else if (TREE_CODE (arg0) == BIT_XOR_EXPR
	       && (tem = fold_unary (BIT_NOT_EXPR, type,
			       	     fold_convert (type,
					     	   TREE_OPERAND (arg0, 0)))))
	return fold_build2 (BIT_XOR_EXPR, type, tem,
			    fold_convert (type, TREE_OPERAND (arg0, 1)));
      else if (TREE_CODE (arg0) == BIT_XOR_EXPR
	       && (tem = fold_unary (BIT_NOT_EXPR, type,
			       	     fold_convert (type,
					     	   TREE_OPERAND (arg0, 1)))))
	return fold_build2 (BIT_XOR_EXPR, type,
			    fold_convert (type, TREE_OPERAND (arg0, 0)), tem);

      return NULL_TREE;

    case TRUTH_NOT_EXPR:
      /* The argument to invert_truthvalue must have Boolean type.  */
      if (TREE_CODE (TREE_TYPE (arg0)) != BOOLEAN_TYPE)
          arg0 = fold_convert (boolean_type_node, arg0);

      /* Note that the operand of this must be an int
	 and its values must be 0 or 1.
	 ("true" is a fixed value perhaps depending on the language,
	 but we don't handle values other than 1 correctly yet.)  */
      tem = fold_truth_not_expr (arg0);
      if (!tem)
	return NULL_TREE;
      return fold_convert (type, tem);

    case REALPART_EXPR:
      if (TREE_CODE (TREE_TYPE (arg0)) != COMPLEX_TYPE)
	return fold_convert (type, arg0);
      if (TREE_CODE (arg0) == COMPLEX_EXPR)
	return omit_one_operand (type, TREE_OPERAND (arg0, 0),
				 TREE_OPERAND (arg0, 1));
      if (TREE_CODE (arg0) == COMPLEX_CST)
	return fold_convert (type, TREE_REALPART (arg0));
      if (TREE_CODE (arg0) == PLUS_EXPR || TREE_CODE (arg0) == MINUS_EXPR)
	{
	  tree itype = TREE_TYPE (TREE_TYPE (arg0));
	  tem = fold_build2 (TREE_CODE (arg0), itype,
			     fold_build1 (REALPART_EXPR, itype,
					  TREE_OPERAND (arg0, 0)),
			     fold_build1 (REALPART_EXPR, itype,
					  TREE_OPERAND (arg0, 1)));
	  return fold_convert (type, tem);
	}
      if (TREE_CODE (arg0) == CONJ_EXPR)
	{
	  tree itype = TREE_TYPE (TREE_TYPE (arg0));
	  tem = fold_build1 (REALPART_EXPR, itype, TREE_OPERAND (arg0, 0));
	  return fold_convert (type, tem);
	}
      return NULL_TREE;

    case IMAGPART_EXPR:
      if (TREE_CODE (TREE_TYPE (arg0)) != COMPLEX_TYPE)
	return fold_convert (type, integer_zero_node);
      if (TREE_CODE (arg0) == COMPLEX_EXPR)
	return omit_one_operand (type, TREE_OPERAND (arg0, 1),
				 TREE_OPERAND (arg0, 0));
      if (TREE_CODE (arg0) == COMPLEX_CST)
	return fold_convert (type, TREE_IMAGPART (arg0));
      if (TREE_CODE (arg0) == PLUS_EXPR || TREE_CODE (arg0) == MINUS_EXPR)
	{
	  tree itype = TREE_TYPE (TREE_TYPE (arg0));
	  tem = fold_build2 (TREE_CODE (arg0), itype,
			     fold_build1 (IMAGPART_EXPR, itype,
					  TREE_OPERAND (arg0, 0)),
			     fold_build1 (IMAGPART_EXPR, itype,
					  TREE_OPERAND (arg0, 1)));
	  return fold_convert (type, tem);
	}
      if (TREE_CODE (arg0) == CONJ_EXPR)
	{
	  tree itype = TREE_TYPE (TREE_TYPE (arg0));
	  tem = fold_build1 (IMAGPART_EXPR, itype, TREE_OPERAND (arg0, 0));
	  return fold_convert (type, negate_expr (tem));
	}
      return NULL_TREE;

    default:
      return NULL_TREE;
    } /* switch (code) */
}

/* Fold a binary expression of code CODE and type TYPE with operands
   OP0 and OP1, containing either a MIN-MAX or a MAX-MIN combination.
   Return the folded expression if folding is successful.  Otherwise,
   return NULL_TREE.  */

static tree
fold_minmax (enum tree_code code, tree type, tree op0, tree op1)
{
  enum tree_code compl_code;

  if (code == MIN_EXPR)
    compl_code = MAX_EXPR;
  else if (code == MAX_EXPR)
    compl_code = MIN_EXPR;
  else
    gcc_unreachable ();

  /* MIN (MAX (a, b), b) == b.  */
  if (TREE_CODE (op0) == compl_code
      && operand_equal_p (TREE_OPERAND (op0, 1), op1, 0))
    return omit_one_operand (type, op1, TREE_OPERAND (op0, 0));

  /* MIN (MAX (b, a), b) == b.  */
  if (TREE_CODE (op0) == compl_code
      && operand_equal_p (TREE_OPERAND (op0, 0), op1, 0)
      && reorder_operands_p (TREE_OPERAND (op0, 1), op1))
    return omit_one_operand (type, op1, TREE_OPERAND (op0, 1));

  /* MIN (a, MAX (a, b)) == a.  */
  if (TREE_CODE (op1) == compl_code
      && operand_equal_p (op0, TREE_OPERAND (op1, 0), 0)
      && reorder_operands_p (op0, TREE_OPERAND (op1, 1)))
    return omit_one_operand (type, op0, TREE_OPERAND (op1, 1));

  /* MIN (a, MAX (b, a)) == a.  */
  if (TREE_CODE (op1) == compl_code
      && operand_equal_p (op0, TREE_OPERAND (op1, 1), 0)
      && reorder_operands_p (op0, TREE_OPERAND (op1, 0)))
    return omit_one_operand (type, op0, TREE_OPERAND (op1, 0));

  return NULL_TREE;
}

/* Subroutine of fold_binary.  This routine performs all of the
   transformations that are common to the equality/inequality
   operators (EQ_EXPR and NE_EXPR) and the ordering operators
   (LT_EXPR, LE_EXPR, GE_EXPR and GT_EXPR).  Callers other than
   fold_binary should call fold_binary.  Fold a comparison with
   tree code CODE and type TYPE with operands OP0 and OP1.  Return
   the folded comparison or NULL_TREE.  */

static tree
fold_comparison (enum tree_code code, tree type, tree op0, tree op1)
{
  tree arg0, arg1, tem;

  arg0 = op0;
  arg1 = op1;

  STRIP_SIGN_NOPS (arg0);
  STRIP_SIGN_NOPS (arg1);

  tem = fold_relational_const (code, type, arg0, arg1);
  if (tem != NULL_TREE)
    return tem;

  /* If one arg is a real or integer constant, put it last.  */
  if (tree_swap_operands_p (arg0, arg1, true))
    return fold_build2 (swap_tree_comparison (code), type, op1, op0);

  /* Transform comparisons of the form X +- C1 CMP C2 to X CMP C2 +- C1.  */
  if ((TREE_CODE (arg0) == PLUS_EXPR || TREE_CODE (arg0) == MINUS_EXPR)
      && (TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
	  && !TREE_OVERFLOW (TREE_OPERAND (arg0, 1))
	  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
      && (TREE_CODE (arg1) == INTEGER_CST
	  && !TREE_OVERFLOW (arg1)))
    {
      tree const1 = TREE_OPERAND (arg0, 1);
      tree const2 = arg1;
      tree variable = TREE_OPERAND (arg0, 0);
      tree lhs;
      int lhs_add;
      lhs_add = TREE_CODE (arg0) != PLUS_EXPR;

      lhs = fold_build2 (lhs_add ? PLUS_EXPR : MINUS_EXPR,
			 TREE_TYPE (arg1), const2, const1);
      if (TREE_CODE (lhs) == TREE_CODE (arg1)
	  && (TREE_CODE (lhs) != INTEGER_CST
	      || !TREE_OVERFLOW (lhs)))
	{
	  fold_overflow_warning (("assuming signed overflow does not occur "
				  "when changing X +- C1 cmp C2 to "
				  "X cmp C1 +- C2"),
				 WARN_STRICT_OVERFLOW_COMPARISON);
	  return fold_build2 (code, type, variable, lhs);
	}
    }

  /* If this is a comparison of two exprs that look like an ARRAY_REF of the
     same object, then we can fold this to a comparison of the two offsets in
     signed size type.  This is possible because pointer arithmetic is
     restricted to retain within an object and overflow on pointer differences
     is undefined as of 6.5.6/8 and /9 with respect to the signed ptrdiff_t.

     We check flag_wrapv directly because pointers types are unsigned,
     and therefore TYPE_OVERFLOW_WRAPS returns true for them.  That is
     normally what we want to avoid certain odd overflow cases, but
     not here.  */
  if (POINTER_TYPE_P (TREE_TYPE (arg0))
      && !flag_wrapv
      && !TYPE_OVERFLOW_TRAPS (TREE_TYPE (arg0)))
    {
      tree base0, offset0, base1, offset1;

      if (extract_array_ref (arg0, &base0, &offset0)
	  && extract_array_ref (arg1, &base1, &offset1)
	  && operand_equal_p (base0, base1, 0))
        {
	  tree signed_size_type_node;
	  signed_size_type_node = signed_type_for (size_type_node);

	  /* By converting to signed size type we cover middle-end pointer
	     arithmetic which operates on unsigned pointer types of size
	     type size and ARRAY_REF offsets which are properly sign or
	     zero extended from their type in case it is narrower than
	     size type.  */
	  if (offset0 == NULL_TREE)
	    offset0 = build_int_cst (signed_size_type_node, 0);
	  else
	    offset0 = fold_convert (signed_size_type_node, offset0);
	  if (offset1 == NULL_TREE)
	    offset1 = build_int_cst (signed_size_type_node, 0);
	  else
	    offset1 = fold_convert (signed_size_type_node, offset1);

	  return fold_build2 (code, type, offset0, offset1);
	}
    }

  if (FLOAT_TYPE_P (TREE_TYPE (arg0)))
    {
      tree targ0 = strip_float_extensions (arg0);
      tree targ1 = strip_float_extensions (arg1);
      tree newtype = TREE_TYPE (targ0);

      if (TYPE_PRECISION (TREE_TYPE (targ1)) > TYPE_PRECISION (newtype))
	newtype = TREE_TYPE (targ1);

      /* Fold (double)float1 CMP (double)float2 into float1 CMP float2.  */
      if (TYPE_PRECISION (newtype) < TYPE_PRECISION (TREE_TYPE (arg0)))
	return fold_build2 (code, type, fold_convert (newtype, targ0),
			    fold_convert (newtype, targ1));

      /* (-a) CMP (-b) -> b CMP a  */
      if (TREE_CODE (arg0) == NEGATE_EXPR
	  && TREE_CODE (arg1) == NEGATE_EXPR)
	return fold_build2 (code, type, TREE_OPERAND (arg1, 0),
			    TREE_OPERAND (arg0, 0));

      if (TREE_CODE (arg1) == REAL_CST)
	{
	  REAL_VALUE_TYPE cst;
	  cst = TREE_REAL_CST (arg1);

	  /* (-a) CMP CST -> a swap(CMP) (-CST)  */
	  if (TREE_CODE (arg0) == NEGATE_EXPR)
	    return fold_build2 (swap_tree_comparison (code), type,
				TREE_OPERAND (arg0, 0),
				build_real (TREE_TYPE (arg1),
					    REAL_VALUE_NEGATE (cst)));

	  /* IEEE doesn't distinguish +0 and -0 in comparisons.  */
	  /* a CMP (-0) -> a CMP 0  */
	  if (REAL_VALUE_MINUS_ZERO (cst))
	    return fold_build2 (code, type, arg0,
				build_real (TREE_TYPE (arg1), dconst0));

	  /* x != NaN is always true, other ops are always false.  */
	  if (REAL_VALUE_ISNAN (cst)
	      && ! HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg1))))
	    {
	      tem = (code == NE_EXPR) ? integer_one_node : integer_zero_node;
	      return omit_one_operand (type, tem, arg0);
	    }

	  /* Fold comparisons against infinity.  */
	  if (REAL_VALUE_ISINF (cst))
	    {
	      tem = fold_inf_compare (code, type, arg0, arg1);
	      if (tem != NULL_TREE)
		return tem;
	    }
	}

      /* If this is a comparison of a real constant with a PLUS_EXPR
	 or a MINUS_EXPR of a real constant, we can convert it into a
	 comparison with a revised real constant as long as no overflow
	 occurs when unsafe_math_optimizations are enabled.  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg1) == REAL_CST
	  && (TREE_CODE (arg0) == PLUS_EXPR
	      || TREE_CODE (arg0) == MINUS_EXPR)
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == REAL_CST
	  && 0 != (tem = const_binop (TREE_CODE (arg0) == PLUS_EXPR
				      ? MINUS_EXPR : PLUS_EXPR,
				      arg1, TREE_OPERAND (arg0, 1), 0))
	  && ! TREE_CONSTANT_OVERFLOW (tem))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0), tem);

      /* Likewise, we can simplify a comparison of a real constant with
         a MINUS_EXPR whose first operand is also a real constant, i.e.
         (c1 - x) < c2 becomes x > c1-c2.  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg1) == REAL_CST
	  && TREE_CODE (arg0) == MINUS_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == REAL_CST
	  && 0 != (tem = const_binop (MINUS_EXPR, TREE_OPERAND (arg0, 0),
				      arg1, 0))
	  && ! TREE_CONSTANT_OVERFLOW (tem))
	return fold_build2 (swap_tree_comparison (code), type,
			    TREE_OPERAND (arg0, 1), tem);

      /* Fold comparisons against built-in math functions.  */
      if (TREE_CODE (arg1) == REAL_CST
	  && flag_unsafe_math_optimizations
	  && ! flag_errno_math)
	{
	  enum built_in_function fcode = builtin_mathfn_code (arg0);

	  if (fcode != END_BUILTINS)
	    {
	      tem = fold_mathfn_compare (fcode, code, type, arg0, arg1);
	      if (tem != NULL_TREE)
		return tem;
	    }
	}
    }

  /* Convert foo++ == CONST into ++foo == CONST + INCR.  */
  if (TREE_CONSTANT (arg1)
      && (TREE_CODE (arg0) == POSTINCREMENT_EXPR
	  || TREE_CODE (arg0) == POSTDECREMENT_EXPR)
      /* This optimization is invalid for ordered comparisons
         if CONST+INCR overflows or if foo+incr might overflow.
	 This optimization is invalid for floating point due to rounding.
	 For pointer types we assume overflow doesn't happen.  */
      && (POINTER_TYPE_P (TREE_TYPE (arg0))
	  || (INTEGRAL_TYPE_P (TREE_TYPE (arg0))
	      && (code == EQ_EXPR || code == NE_EXPR))))
    {
      tree varop, newconst;

      if (TREE_CODE (arg0) == POSTINCREMENT_EXPR)
	{
	  newconst = fold_build2 (PLUS_EXPR, TREE_TYPE (arg0),
				  arg1, TREE_OPERAND (arg0, 1));
	  varop = build2 (PREINCREMENT_EXPR, TREE_TYPE (arg0),
			  TREE_OPERAND (arg0, 0),
			  TREE_OPERAND (arg0, 1));
	}
      else
	{
	  newconst = fold_build2 (MINUS_EXPR, TREE_TYPE (arg0),
				  arg1, TREE_OPERAND (arg0, 1));
	  varop = build2 (PREDECREMENT_EXPR, TREE_TYPE (arg0),
			  TREE_OPERAND (arg0, 0),
			  TREE_OPERAND (arg0, 1));
	}


      /* If VAROP is a reference to a bitfield, we must mask
	 the constant by the width of the field.  */
      if (TREE_CODE (TREE_OPERAND (varop, 0)) == COMPONENT_REF
	  && DECL_BIT_FIELD (TREE_OPERAND (TREE_OPERAND (varop, 0), 1))
	  && host_integerp (DECL_SIZE (TREE_OPERAND
					 (TREE_OPERAND (varop, 0), 1)), 1))
	{
	  tree fielddecl = TREE_OPERAND (TREE_OPERAND (varop, 0), 1);
	  HOST_WIDE_INT size = tree_low_cst (DECL_SIZE (fielddecl), 1);
	  tree folded_compare, shift;

	  /* First check whether the comparison would come out
	     always the same.  If we don't do that we would
	     change the meaning with the masking.  */
	  folded_compare = fold_build2 (code, type,
					TREE_OPERAND (varop, 0), arg1);
	  if (TREE_CODE (folded_compare) == INTEGER_CST)
	    return omit_one_operand (type, folded_compare, varop);

	  shift = build_int_cst (NULL_TREE,
				 TYPE_PRECISION (TREE_TYPE (varop)) - size);
	  shift = fold_convert (TREE_TYPE (varop), shift);
	  newconst = fold_build2 (LSHIFT_EXPR, TREE_TYPE (varop),
				  newconst, shift);
	  newconst = fold_build2 (RSHIFT_EXPR, TREE_TYPE (varop),
				  newconst, shift);
	}

      return fold_build2 (code, type, varop, newconst);
    }

  if (TREE_CODE (TREE_TYPE (arg0)) == INTEGER_TYPE
      && (TREE_CODE (arg0) == NOP_EXPR
	  || TREE_CODE (arg0) == CONVERT_EXPR))
    {
      /* If we are widening one operand of an integer comparison,
	 see if the other operand is similarly being widened.  Perhaps we
	 can do the comparison in the narrower type.  */
      tem = fold_widened_comparison (code, type, arg0, arg1);
      if (tem)
	return tem;

      /* Or if we are changing signedness.  */
      tem = fold_sign_changed_comparison (code, type, arg0, arg1);
      if (tem)
	return tem;
    }

  /* If this is comparing a constant with a MIN_EXPR or a MAX_EXPR of a
     constant, we can simplify it.  */
  if (TREE_CODE (arg1) == INTEGER_CST
      && (TREE_CODE (arg0) == MIN_EXPR
	  || TREE_CODE (arg0) == MAX_EXPR)
      && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
    {
      tem = optimize_minmax_comparison (code, type, op0, op1);
      if (tem)
	return tem;
    }

  /* Simplify comparison of something with itself.  (For IEEE
     floating-point, we can only do some of these simplifications.)  */
  if (operand_equal_p (arg0, arg1, 0))
    {
      switch (code)
	{
	case EQ_EXPR:
	  if (! FLOAT_TYPE_P (TREE_TYPE (arg0))
	      || ! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0))))
	    return constant_boolean_node (1, type);
	  break;

	case GE_EXPR:
	case LE_EXPR:
	  if (! FLOAT_TYPE_P (TREE_TYPE (arg0))
	      || ! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0))))
	    return constant_boolean_node (1, type);
	  return fold_build2 (EQ_EXPR, type, arg0, arg1);

	case NE_EXPR:
	  /* For NE, we can only do this simplification if integer
	     or we don't honor IEEE floating point NaNs.  */
	  if (FLOAT_TYPE_P (TREE_TYPE (arg0))
	      && HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0))))
	    break;
	  /* ... fall through ...  */
	case GT_EXPR:
	case LT_EXPR:
	  return constant_boolean_node (0, type);
	default:
	  gcc_unreachable ();
	}
    }

  /* If we are comparing an expression that just has comparisons
     of two integer values, arithmetic expressions of those comparisons,
     and constants, we can simplify it.  There are only three cases
     to check: the two values can either be equal, the first can be
     greater, or the second can be greater.  Fold the expression for
     those three values.  Since each value must be 0 or 1, we have
     eight possibilities, each of which corresponds to the constant 0
     or 1 or one of the six possible comparisons.

     This handles common cases like (a > b) == 0 but also handles
     expressions like  ((x > y) - (y > x)) > 0, which supposedly
     occur in macroized code.  */

  if (TREE_CODE (arg1) == INTEGER_CST && TREE_CODE (arg0) != INTEGER_CST)
    {
      tree cval1 = 0, cval2 = 0;
      int save_p = 0;

      if (twoval_comparison_p (arg0, &cval1, &cval2, &save_p)
	  /* Don't handle degenerate cases here; they should already
	     have been handled anyway.  */
	  && cval1 != 0 && cval2 != 0
	  && ! (TREE_CONSTANT (cval1) && TREE_CONSTANT (cval2))
	  && TREE_TYPE (cval1) == TREE_TYPE (cval2)
	  && INTEGRAL_TYPE_P (TREE_TYPE (cval1))
	  && TYPE_MAX_VALUE (TREE_TYPE (cval1))
	  && TYPE_MAX_VALUE (TREE_TYPE (cval2))
	  && ! operand_equal_p (TYPE_MIN_VALUE (TREE_TYPE (cval1)),
				TYPE_MAX_VALUE (TREE_TYPE (cval2)), 0))
	{
	  tree maxval = TYPE_MAX_VALUE (TREE_TYPE (cval1));
	  tree minval = TYPE_MIN_VALUE (TREE_TYPE (cval1));

	  /* We can't just pass T to eval_subst in case cval1 or cval2
	     was the same as ARG1.  */

	  tree high_result
		= fold_build2 (code, type,
			       eval_subst (arg0, cval1, maxval,
					   cval2, minval),
			       arg1);
	  tree equal_result
		= fold_build2 (code, type,
			       eval_subst (arg0, cval1, maxval,
					   cval2, maxval),
			       arg1);
	  tree low_result
		= fold_build2 (code, type,
			       eval_subst (arg0, cval1, minval,
					   cval2, maxval),
			       arg1);

	  /* All three of these results should be 0 or 1.  Confirm they are.
	     Then use those values to select the proper code to use.  */

	  if (TREE_CODE (high_result) == INTEGER_CST
	      && TREE_CODE (equal_result) == INTEGER_CST
	      && TREE_CODE (low_result) == INTEGER_CST)
	    {
	      /* Make a 3-bit mask with the high-order bit being the
		 value for `>', the next for '=', and the low for '<'.  */
	      switch ((integer_onep (high_result) * 4)
		      + (integer_onep (equal_result) * 2)
		      + integer_onep (low_result))
		{
		case 0:
		  /* Always false.  */
		  return omit_one_operand (type, integer_zero_node, arg0);
		case 1:
		  code = LT_EXPR;
		  break;
		case 2:
		  code = EQ_EXPR;
		  break;
		case 3:
		  code = LE_EXPR;
		  break;
		case 4:
		  code = GT_EXPR;
		  break;
		case 5:
		  code = NE_EXPR;
		  break;
		case 6:
		  code = GE_EXPR;
		  break;
		case 7:
		  /* Always true.  */
		  return omit_one_operand (type, integer_one_node, arg0);
		}

	      if (save_p)
		return save_expr (build2 (code, type, cval1, cval2));
	      return fold_build2 (code, type, cval1, cval2);
	    }
	}
    }

  /* Fold a comparison of the address of COMPONENT_REFs with the same
     type and component to a comparison of the address of the base
     object.  In short, &x->a OP &y->a to x OP y and
     &x->a OP &y.a to x OP &y  */
  if (TREE_CODE (arg0) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (arg0, 0)) == COMPONENT_REF
      && TREE_CODE (arg1) == ADDR_EXPR
      && TREE_CODE (TREE_OPERAND (arg1, 0)) == COMPONENT_REF)
    {
      tree cref0 = TREE_OPERAND (arg0, 0);
      tree cref1 = TREE_OPERAND (arg1, 0);
      if (TREE_OPERAND (cref0, 1) == TREE_OPERAND (cref1, 1))
	{
	  tree op0 = TREE_OPERAND (cref0, 0);
	  tree op1 = TREE_OPERAND (cref1, 0);
	  return fold_build2 (code, type,
			      build_fold_addr_expr (op0),
			      build_fold_addr_expr (op1));
	}
    }

  /* We can fold X/C1 op C2 where C1 and C2 are integer constants
     into a single range test.  */
  if ((TREE_CODE (arg0) == TRUNC_DIV_EXPR
       || TREE_CODE (arg0) == EXACT_DIV_EXPR)
      && TREE_CODE (arg1) == INTEGER_CST
      && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
      && !integer_zerop (TREE_OPERAND (arg0, 1))
      && !TREE_OVERFLOW (TREE_OPERAND (arg0, 1))
      && !TREE_OVERFLOW (arg1))
    {
      tem = fold_div_compare (code, type, arg0, arg1);
      if (tem != NULL_TREE)
	return tem;
    }

  return NULL_TREE;
}


/* Subroutine of fold_binary.  Optimize complex multiplications of the
   form z * conj(z), as pow(realpart(z),2) + pow(imagpart(z),2).  The
   argument EXPR represents the expression "z" of type TYPE.  */

static tree
fold_mult_zconjz (tree type, tree expr)
{
  tree itype = TREE_TYPE (type);
  tree rpart, ipart, tem;

  if (TREE_CODE (expr) == COMPLEX_EXPR)
    {
      rpart = TREE_OPERAND (expr, 0);
      ipart = TREE_OPERAND (expr, 1);
    }
  else if (TREE_CODE (expr) == COMPLEX_CST)
    {
      rpart = TREE_REALPART (expr);
      ipart = TREE_IMAGPART (expr);
    }
  else
    {
      expr = save_expr (expr);
      rpart = fold_build1 (REALPART_EXPR, itype, expr);
      ipart = fold_build1 (IMAGPART_EXPR, itype, expr);
    }

  rpart = save_expr (rpart);
  ipart = save_expr (ipart);
  tem = fold_build2 (PLUS_EXPR, itype,
		     fold_build2 (MULT_EXPR, itype, rpart, rpart),
		     fold_build2 (MULT_EXPR, itype, ipart, ipart));
  return fold_build2 (COMPLEX_EXPR, type, tem,
		      fold_convert (itype, integer_zero_node));
}


/* Fold a binary expression of code CODE and type TYPE with operands
   OP0 and OP1.  Return the folded expression if folding is
   successful.  Otherwise, return NULL_TREE.  */

tree
fold_binary (enum tree_code code, tree type, tree op0, tree op1)
{
  enum tree_code_class kind = TREE_CODE_CLASS (code);
  tree arg0, arg1, tem;
  tree t1 = NULL_TREE;
  bool strict_overflow_p;

  gcc_assert (IS_EXPR_CODE_CLASS (kind)
	      && TREE_CODE_LENGTH (code) == 2
	      && op0 != NULL_TREE
	      && op1 != NULL_TREE);

  arg0 = op0;
  arg1 = op1;

  /* Strip any conversions that don't change the mode.  This is
     safe for every expression, except for a comparison expression
     because its signedness is derived from its operands.  So, in
     the latter case, only strip conversions that don't change the
     signedness.

     Note that this is done as an internal manipulation within the
     constant folder, in order to find the simplest representation
     of the arguments so that their form can be studied.  In any
     cases, the appropriate type conversions should be put back in
     the tree that will get out of the constant folder.  */

  if (kind == tcc_comparison)
    {
      STRIP_SIGN_NOPS (arg0);
      STRIP_SIGN_NOPS (arg1);
    }
  else
    {
      STRIP_NOPS (arg0);
      STRIP_NOPS (arg1);
    }

  /* Note that TREE_CONSTANT isn't enough: static var addresses are
     constant but we can't do arithmetic on them.  */
  if ((TREE_CODE (arg0) == INTEGER_CST && TREE_CODE (arg1) == INTEGER_CST)
      || (TREE_CODE (arg0) == REAL_CST && TREE_CODE (arg1) == REAL_CST)
      || (TREE_CODE (arg0) == COMPLEX_CST && TREE_CODE (arg1) == COMPLEX_CST)
      || (TREE_CODE (arg0) == VECTOR_CST && TREE_CODE (arg1) == VECTOR_CST))
    {
      if (kind == tcc_binary)
	tem = const_binop (code, arg0, arg1, 0);
      else if (kind == tcc_comparison)
	tem = fold_relational_const (code, type, arg0, arg1);
      else
	tem = NULL_TREE;

      if (tem != NULL_TREE)
	{
	  if (TREE_TYPE (tem) != type)
	    tem = fold_convert (type, tem);
	  return tem;
	}
    }

  /* If this is a commutative operation, and ARG0 is a constant, move it
     to ARG1 to reduce the number of tests below.  */
  if (commutative_tree_code (code)
      && tree_swap_operands_p (arg0, arg1, true))
    return fold_build2 (code, type, op1, op0);

  /* ARG0 is the first operand of EXPR, and ARG1 is the second operand.

     First check for cases where an arithmetic operation is applied to a
     compound, conditional, or comparison operation.  Push the arithmetic
     operation inside the compound or conditional to see if any folding
     can then be done.  Convert comparison to conditional for this purpose.
     The also optimizes non-constant cases that used to be done in
     expand_expr.

     Before we do that, see if this is a BIT_AND_EXPR or a BIT_IOR_EXPR,
     one of the operands is a comparison and the other is a comparison, a
     BIT_AND_EXPR with the constant 1, or a truth value.  In that case, the
     code below would make the expression more complex.  Change it to a
     TRUTH_{AND,OR}_EXPR.  Likewise, convert a similar NE_EXPR to
     TRUTH_XOR_EXPR and an EQ_EXPR to the inversion of a TRUTH_XOR_EXPR.  */

  if ((code == BIT_AND_EXPR || code == BIT_IOR_EXPR
       || code == EQ_EXPR || code == NE_EXPR)
      && ((truth_value_p (TREE_CODE (arg0))
	   && (truth_value_p (TREE_CODE (arg1))
	       || (TREE_CODE (arg1) == BIT_AND_EXPR
		   && integer_onep (TREE_OPERAND (arg1, 1)))))
	  || (truth_value_p (TREE_CODE (arg1))
	      && (truth_value_p (TREE_CODE (arg0))
		  || (TREE_CODE (arg0) == BIT_AND_EXPR
		      && integer_onep (TREE_OPERAND (arg0, 1)))))))
    {
      tem = fold_build2 (code == BIT_AND_EXPR ? TRUTH_AND_EXPR
			 : code == BIT_IOR_EXPR ? TRUTH_OR_EXPR
			 : TRUTH_XOR_EXPR,
			 boolean_type_node,
			 fold_convert (boolean_type_node, arg0),
			 fold_convert (boolean_type_node, arg1));

      if (code == EQ_EXPR)
	tem = invert_truthvalue (tem);

      return fold_convert (type, tem);
    }

  if (TREE_CODE_CLASS (code) == tcc_binary
      || TREE_CODE_CLASS (code) == tcc_comparison)
    {
      if (TREE_CODE (arg0) == COMPOUND_EXPR)
	return build2 (COMPOUND_EXPR, type, TREE_OPERAND (arg0, 0),
		       fold_build2 (code, type,
				    TREE_OPERAND (arg0, 1), op1));
      if (TREE_CODE (arg1) == COMPOUND_EXPR
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 0)))
	return build2 (COMPOUND_EXPR, type, TREE_OPERAND (arg1, 0),
		       fold_build2 (code, type,
				    op0, TREE_OPERAND (arg1, 1)));

      if (TREE_CODE (arg0) == COND_EXPR || COMPARISON_CLASS_P (arg0))
	{
	  tem = fold_binary_op_with_conditional_arg (code, type, op0, op1,
						     arg0, arg1, 
						     /*cond_first_p=*/1);
	  if (tem != NULL_TREE)
	    return tem;
	}

      if (TREE_CODE (arg1) == COND_EXPR || COMPARISON_CLASS_P (arg1))
	{
	  tem = fold_binary_op_with_conditional_arg (code, type, op0, op1,
						     arg1, arg0, 
					             /*cond_first_p=*/0);
	  if (tem != NULL_TREE)
	    return tem;
	}
    }

  switch (code)
    {
    case PLUS_EXPR:
      /* A + (-B) -> A - B */
      if (TREE_CODE (arg1) == NEGATE_EXPR)
	return fold_build2 (MINUS_EXPR, type,
			    fold_convert (type, arg0),
			    fold_convert (type, TREE_OPERAND (arg1, 0)));
      /* (-A) + B -> B - A */
      if (TREE_CODE (arg0) == NEGATE_EXPR
	  && reorder_operands_p (TREE_OPERAND (arg0, 0), arg1))
	return fold_build2 (MINUS_EXPR, type,
			    fold_convert (type, arg1),
			    fold_convert (type, TREE_OPERAND (arg0, 0)));
      /* Convert ~A + 1 to -A.  */
      if (INTEGRAL_TYPE_P (type)
	  && TREE_CODE (arg0) == BIT_NOT_EXPR
	  && integer_onep (arg1))
	return fold_build1 (NEGATE_EXPR, type, TREE_OPERAND (arg0, 0));

      /* Handle (A1 * C1) + (A2 * C2) with A1, A2 or C1, C2 being the
	 same or one.  */
      if ((TREE_CODE (arg0) == MULT_EXPR
	   || TREE_CODE (arg1) == MULT_EXPR)
	  && (!FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations))
        {
	  tree tem = fold_plusminus_mult_expr (code, type, arg0, arg1);
	  if (tem)
	    return tem;
	}

      if (! FLOAT_TYPE_P (type))
	{
	  if (integer_zerop (arg1))
	    return non_lvalue (fold_convert (type, arg0));

	  /* If we are adding two BIT_AND_EXPR's, both of which are and'ing
	     with a constant, and the two constants have no bits in common,
	     we should treat this as a BIT_IOR_EXPR since this may produce more
	     simplifications.  */
	  if (TREE_CODE (arg0) == BIT_AND_EXPR
	      && TREE_CODE (arg1) == BIT_AND_EXPR
	      && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
	      && TREE_CODE (TREE_OPERAND (arg1, 1)) == INTEGER_CST
	      && integer_zerop (const_binop (BIT_AND_EXPR,
					     TREE_OPERAND (arg0, 1),
					     TREE_OPERAND (arg1, 1), 0)))
	    {
	      code = BIT_IOR_EXPR;
	      goto bit_ior;
	    }

	  /* Reassociate (plus (plus (mult) (foo)) (mult)) as
	     (plus (plus (mult) (mult)) (foo)) so that we can
	     take advantage of the factoring cases below.  */
	  if (((TREE_CODE (arg0) == PLUS_EXPR
		|| TREE_CODE (arg0) == MINUS_EXPR)
	       && TREE_CODE (arg1) == MULT_EXPR)
	      || ((TREE_CODE (arg1) == PLUS_EXPR
		   || TREE_CODE (arg1) == MINUS_EXPR)
		  && TREE_CODE (arg0) == MULT_EXPR))
	    {
	      tree parg0, parg1, parg, marg;
	      enum tree_code pcode;

	      if (TREE_CODE (arg1) == MULT_EXPR)
		parg = arg0, marg = arg1;
	      else
		parg = arg1, marg = arg0;
	      pcode = TREE_CODE (parg);
	      parg0 = TREE_OPERAND (parg, 0);
	      parg1 = TREE_OPERAND (parg, 1);
	      STRIP_NOPS (parg0);
	      STRIP_NOPS (parg1);

	      if (TREE_CODE (parg0) == MULT_EXPR
		  && TREE_CODE (parg1) != MULT_EXPR)
		return fold_build2 (pcode, type,
				    fold_build2 (PLUS_EXPR, type,
						 fold_convert (type, parg0),
						 fold_convert (type, marg)),
				    fold_convert (type, parg1));
	      if (TREE_CODE (parg0) != MULT_EXPR
		  && TREE_CODE (parg1) == MULT_EXPR)
		return fold_build2 (PLUS_EXPR, type,
				    fold_convert (type, parg0),
				    fold_build2 (pcode, type,
						 fold_convert (type, marg),
						 fold_convert (type,
							       parg1)));
	    }

	  /* Try replacing &a[i1] + c * i2 with &a[i1 + i2], if c is step
	     of the array.  Loop optimizer sometimes produce this type of
	     expressions.  */
	  if (TREE_CODE (arg0) == ADDR_EXPR)
	    {
	      tem = try_move_mult_to_index (PLUS_EXPR, arg0, arg1);
	      if (tem)
		return fold_convert (type, tem);
	    }
	  else if (TREE_CODE (arg1) == ADDR_EXPR)
	    {
	      tem = try_move_mult_to_index (PLUS_EXPR, arg1, arg0);
	      if (tem)
		return fold_convert (type, tem);
	    }
	}
      else
	{
	  /* See if ARG1 is zero and X + ARG1 reduces to X.  */
	  if (fold_real_zero_addition_p (TREE_TYPE (arg0), arg1, 0))
	    return non_lvalue (fold_convert (type, arg0));

	  /* Likewise if the operands are reversed.  */
	  if (fold_real_zero_addition_p (TREE_TYPE (arg1), arg0, 0))
	    return non_lvalue (fold_convert (type, arg1));

	  /* Convert X + -C into X - C.  */
	  if (TREE_CODE (arg1) == REAL_CST
	      && REAL_VALUE_NEGATIVE (TREE_REAL_CST (arg1)))
	    {
	      tem = fold_negate_const (arg1, type);
	      if (!TREE_OVERFLOW (arg1) || !flag_trapping_math)
		return fold_build2 (MINUS_EXPR, type,
				    fold_convert (type, arg0),
				    fold_convert (type, tem));
	    }

          if (flag_unsafe_math_optimizations
	      && (TREE_CODE (arg0) == RDIV_EXPR || TREE_CODE (arg0) == MULT_EXPR)
	      && (TREE_CODE (arg1) == RDIV_EXPR || TREE_CODE (arg1) == MULT_EXPR)
	      && (tem = distribute_real_division (code, type, arg0, arg1)))
	    return tem;

	  /* Convert x+x into x*2.0.  */
	  if (operand_equal_p (arg0, arg1, 0)
	      && SCALAR_FLOAT_TYPE_P (type))
	    return fold_build2 (MULT_EXPR, type, arg0,
				build_real (type, dconst2));

          /* Convert a + (b*c + d*e) into (a + b*c) + d*e.  */
          if (flag_unsafe_math_optimizations
              && TREE_CODE (arg1) == PLUS_EXPR
              && TREE_CODE (arg0) != MULT_EXPR)
            {
              tree tree10 = TREE_OPERAND (arg1, 0);
              tree tree11 = TREE_OPERAND (arg1, 1);
              if (TREE_CODE (tree11) == MULT_EXPR
		  && TREE_CODE (tree10) == MULT_EXPR)
                {
                  tree tree0;
                  tree0 = fold_build2 (PLUS_EXPR, type, arg0, tree10);
                  return fold_build2 (PLUS_EXPR, type, tree0, tree11);
                }
            }
          /* Convert (b*c + d*e) + a into b*c + (d*e +a).  */
          if (flag_unsafe_math_optimizations
              && TREE_CODE (arg0) == PLUS_EXPR
              && TREE_CODE (arg1) != MULT_EXPR)
            {
              tree tree00 = TREE_OPERAND (arg0, 0);
              tree tree01 = TREE_OPERAND (arg0, 1);
              if (TREE_CODE (tree01) == MULT_EXPR
		  && TREE_CODE (tree00) == MULT_EXPR)
                {
                  tree tree0;
                  tree0 = fold_build2 (PLUS_EXPR, type, tree01, arg1);
                  return fold_build2 (PLUS_EXPR, type, tree00, tree0);
                }
            }
	}

     bit_rotate:
      /* (A << C1) + (A >> C2) if A is unsigned and C1+C2 is the size of A
	 is a rotate of A by C1 bits.  */
      /* (A << B) + (A >> (Z - B)) if A is unsigned and Z is the size of A
	 is a rotate of A by B bits.  */
      {
	enum tree_code code0, code1;
	code0 = TREE_CODE (arg0);
	code1 = TREE_CODE (arg1);
	if (((code0 == RSHIFT_EXPR && code1 == LSHIFT_EXPR)
	     || (code1 == RSHIFT_EXPR && code0 == LSHIFT_EXPR))
	    && operand_equal_p (TREE_OPERAND (arg0, 0),
			        TREE_OPERAND (arg1, 0), 0)
	    && TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (arg0, 0))))
	  {
	    tree tree01, tree11;
	    enum tree_code code01, code11;

	    tree01 = TREE_OPERAND (arg0, 1);
	    tree11 = TREE_OPERAND (arg1, 1);
	    STRIP_NOPS (tree01);
	    STRIP_NOPS (tree11);
	    code01 = TREE_CODE (tree01);
	    code11 = TREE_CODE (tree11);
	    if (code01 == INTEGER_CST
		&& code11 == INTEGER_CST
		&& TREE_INT_CST_HIGH (tree01) == 0
		&& TREE_INT_CST_HIGH (tree11) == 0
		&& ((TREE_INT_CST_LOW (tree01) + TREE_INT_CST_LOW (tree11))
		    == TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (arg0, 0)))))
	      return build2 (LROTATE_EXPR, type, TREE_OPERAND (arg0, 0),
			     code0 == LSHIFT_EXPR ? tree01 : tree11);
	    else if (code11 == MINUS_EXPR)
	      {
		tree tree110, tree111;
		tree110 = TREE_OPERAND (tree11, 0);
		tree111 = TREE_OPERAND (tree11, 1);
		STRIP_NOPS (tree110);
		STRIP_NOPS (tree111);
		if (TREE_CODE (tree110) == INTEGER_CST
		    && 0 == compare_tree_int (tree110,
					      TYPE_PRECISION
					      (TREE_TYPE (TREE_OPERAND
							  (arg0, 0))))
		    && operand_equal_p (tree01, tree111, 0))
		  return build2 ((code0 == LSHIFT_EXPR
				  ? LROTATE_EXPR
				  : RROTATE_EXPR),
				 type, TREE_OPERAND (arg0, 0), tree01);
	      }
	    else if (code01 == MINUS_EXPR)
	      {
		tree tree010, tree011;
		tree010 = TREE_OPERAND (tree01, 0);
		tree011 = TREE_OPERAND (tree01, 1);
		STRIP_NOPS (tree010);
		STRIP_NOPS (tree011);
		if (TREE_CODE (tree010) == INTEGER_CST
		    && 0 == compare_tree_int (tree010,
					      TYPE_PRECISION
					      (TREE_TYPE (TREE_OPERAND
							  (arg0, 0))))
		    && operand_equal_p (tree11, tree011, 0))
		  return build2 ((code0 != LSHIFT_EXPR
				  ? LROTATE_EXPR
				  : RROTATE_EXPR),
				 type, TREE_OPERAND (arg0, 0), tree11);
	      }
	  }
      }

    associate:
      /* In most languages, can't associate operations on floats through
	 parentheses.  Rather than remember where the parentheses were, we
	 don't associate floats at all, unless the user has specified
	 -funsafe-math-optimizations.  */

      if (! FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations)
	{
	  tree var0, con0, lit0, minus_lit0;
	  tree var1, con1, lit1, minus_lit1;
	  bool ok = true;

	  /* Split both trees into variables, constants, and literals.  Then
	     associate each group together, the constants with literals,
	     then the result with variables.  This increases the chances of
	     literals being recombined later and of generating relocatable
	     expressions for the sum of a constant and literal.  */
	  var0 = split_tree (arg0, code, &con0, &lit0, &minus_lit0, 0);
	  var1 = split_tree (arg1, code, &con1, &lit1, &minus_lit1,
			     code == MINUS_EXPR);

	  /* With undefined overflow we can only associate constants
	     with one variable.  */
	  if ((POINTER_TYPE_P (type)
	       || (INTEGRAL_TYPE_P (type)
		   && !(TYPE_UNSIGNED (type) || flag_wrapv)))
	      && var0 && var1)
	    {
	      tree tmp0 = var0;
	      tree tmp1 = var1;

	      if (TREE_CODE (tmp0) == NEGATE_EXPR)
	        tmp0 = TREE_OPERAND (tmp0, 0);
	      if (TREE_CODE (tmp1) == NEGATE_EXPR)
	        tmp1 = TREE_OPERAND (tmp1, 0);
	      /* The only case we can still associate with two variables
		 is if they are the same, modulo negation.  */
	      if (!operand_equal_p (tmp0, tmp1, 0))
	        ok = false;
	    }

	  /* Only do something if we found more than two objects.  Otherwise,
	     nothing has changed and we risk infinite recursion.  */
	  if (ok
	      && (2 < ((var0 != 0) + (var1 != 0)
		       + (con0 != 0) + (con1 != 0)
		       + (lit0 != 0) + (lit1 != 0)
		       + (minus_lit0 != 0) + (minus_lit1 != 0))))
	    {
	      /* Recombine MINUS_EXPR operands by using PLUS_EXPR.  */
	      if (code == MINUS_EXPR)
		code = PLUS_EXPR;

	      var0 = associate_trees (var0, var1, code, type);
	      con0 = associate_trees (con0, con1, code, type);
	      lit0 = associate_trees (lit0, lit1, code, type);
	      minus_lit0 = associate_trees (minus_lit0, minus_lit1, code, type);

	      /* Preserve the MINUS_EXPR if the negative part of the literal is
		 greater than the positive part.  Otherwise, the multiplicative
		 folding code (i.e extract_muldiv) may be fooled in case
		 unsigned constants are subtracted, like in the following
		 example: ((X*2 + 4) - 8U)/2.  */
	      if (minus_lit0 && lit0)
		{
		  if (TREE_CODE (lit0) == INTEGER_CST
		      && TREE_CODE (minus_lit0) == INTEGER_CST
		      && tree_int_cst_lt (lit0, minus_lit0))
		    {
		      minus_lit0 = associate_trees (minus_lit0, lit0,
						    MINUS_EXPR, type);
		      lit0 = 0;
		    }
		  else
		    {
		      lit0 = associate_trees (lit0, minus_lit0,
					      MINUS_EXPR, type);
		      minus_lit0 = 0;
		    }
		}
	      if (minus_lit0)
		{
		  if (con0 == 0)
		    return fold_convert (type,
					 associate_trees (var0, minus_lit0,
							  MINUS_EXPR, type));
		  else
		    {
		      con0 = associate_trees (con0, minus_lit0,
					      MINUS_EXPR, type);
		      return fold_convert (type,
					   associate_trees (var0, con0,
							    PLUS_EXPR, type));
		    }
		}

	      con0 = associate_trees (con0, lit0, code, type);
	      return fold_convert (type, associate_trees (var0, con0,
							  code, type));
	    }
	}

      return NULL_TREE;

    case MINUS_EXPR:
      /* A - (-B) -> A + B */
      if (TREE_CODE (arg1) == NEGATE_EXPR)
	return fold_build2 (PLUS_EXPR, type, arg0, TREE_OPERAND (arg1, 0));
      /* (-A) - B -> (-B) - A  where B is easily negated and we can swap.  */
      if (TREE_CODE (arg0) == NEGATE_EXPR
	  && (FLOAT_TYPE_P (type)
	      || (INTEGRAL_TYPE_P (type) && flag_wrapv && !flag_trapv))
	  && negate_expr_p (arg1)
	  && reorder_operands_p (arg0, arg1))
	return fold_build2 (MINUS_EXPR, type, negate_expr (arg1),
			    TREE_OPERAND (arg0, 0));
      /* Convert -A - 1 to ~A.  */
      if (INTEGRAL_TYPE_P (type)
	  && TREE_CODE (arg0) == NEGATE_EXPR
	  && integer_onep (arg1))
	return fold_build1 (BIT_NOT_EXPR, type,
			    fold_convert (type, TREE_OPERAND (arg0, 0)));

      /* Convert -1 - A to ~A.  */
      if (INTEGRAL_TYPE_P (type)
	  && integer_all_onesp (arg0))
	return fold_build1 (BIT_NOT_EXPR, type, arg1);

      if (! FLOAT_TYPE_P (type))
	{
	  if (integer_zerop (arg0))
	    return negate_expr (fold_convert (type, arg1));
	  if (integer_zerop (arg1))
	    return non_lvalue (fold_convert (type, arg0));

	  /* Fold A - (A & B) into ~B & A.  */
	  if (!TREE_SIDE_EFFECTS (arg0)
	      && TREE_CODE (arg1) == BIT_AND_EXPR)
	    {
	      if (operand_equal_p (arg0, TREE_OPERAND (arg1, 1), 0))
		return fold_build2 (BIT_AND_EXPR, type,
				    fold_build1 (BIT_NOT_EXPR, type,
						 TREE_OPERAND (arg1, 0)),
				    arg0);
	      if (operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
		return fold_build2 (BIT_AND_EXPR, type,
				    fold_build1 (BIT_NOT_EXPR, type,
						 TREE_OPERAND (arg1, 1)),
				    arg0);
	    }

	  /* Fold (A & ~B) - (A & B) into (A ^ B) - B, where B is
	     any power of 2 minus 1.  */
	  if (TREE_CODE (arg0) == BIT_AND_EXPR
	      && TREE_CODE (arg1) == BIT_AND_EXPR
	      && operand_equal_p (TREE_OPERAND (arg0, 0),
				  TREE_OPERAND (arg1, 0), 0))
	    {
	      tree mask0 = TREE_OPERAND (arg0, 1);
	      tree mask1 = TREE_OPERAND (arg1, 1);
	      tree tem = fold_build1 (BIT_NOT_EXPR, type, mask0);

	      if (operand_equal_p (tem, mask1, 0))
		{
		  tem = fold_build2 (BIT_XOR_EXPR, type,
				     TREE_OPERAND (arg0, 0), mask1);
		  return fold_build2 (MINUS_EXPR, type, tem, mask1);
		}
	    }
	}

      /* See if ARG1 is zero and X - ARG1 reduces to X.  */
      else if (fold_real_zero_addition_p (TREE_TYPE (arg0), arg1, 1))
	return non_lvalue (fold_convert (type, arg0));

      /* (ARG0 - ARG1) is the same as (-ARG1 + ARG0).  So check whether
	 ARG0 is zero and X + ARG0 reduces to X, since that would mean
	 (-ARG1 + ARG0) reduces to -ARG1.  */
      else if (fold_real_zero_addition_p (TREE_TYPE (arg1), arg0, 0))
	return negate_expr (fold_convert (type, arg1));

      /* Fold &x - &x.  This can happen from &x.foo - &x.
	 This is unsafe for certain floats even in non-IEEE formats.
	 In IEEE, it is unsafe because it does wrong for NaNs.
	 Also note that operand_equal_p is always false if an operand
	 is volatile.  */

      if ((! FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations)
	  && operand_equal_p (arg0, arg1, 0))
	return fold_convert (type, integer_zero_node);

      /* A - B -> A + (-B) if B is easily negatable.  */
      if (negate_expr_p (arg1)
	  && ((FLOAT_TYPE_P (type)
               /* Avoid this transformation if B is a positive REAL_CST.  */
	       && (TREE_CODE (arg1) != REAL_CST
		   ||  REAL_VALUE_NEGATIVE (TREE_REAL_CST (arg1))))
	      || (INTEGRAL_TYPE_P (type) && flag_wrapv && !flag_trapv)))
	return fold_build2 (PLUS_EXPR, type,
			    fold_convert (type, arg0),
			    fold_convert (type, negate_expr (arg1)));

      /* Try folding difference of addresses.  */
      {
	HOST_WIDE_INT diff;

	if ((TREE_CODE (arg0) == ADDR_EXPR
	     || TREE_CODE (arg1) == ADDR_EXPR)
	    && ptr_difference_const (arg0, arg1, &diff))
	  return build_int_cst_type (type, diff);
      }

      /* Fold &a[i] - &a[j] to i-j.  */
      if (TREE_CODE (arg0) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == ARRAY_REF
	  && TREE_CODE (arg1) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (arg1, 0)) == ARRAY_REF)
        {
	  tree aref0 = TREE_OPERAND (arg0, 0);
	  tree aref1 = TREE_OPERAND (arg1, 0);
	  if (operand_equal_p (TREE_OPERAND (aref0, 0),
			       TREE_OPERAND (aref1, 0), 0))
	    {
	      tree op0 = fold_convert (type, TREE_OPERAND (aref0, 1));
	      tree op1 = fold_convert (type, TREE_OPERAND (aref1, 1));
	      tree esz = array_ref_element_size (aref0);
	      tree diff = build2 (MINUS_EXPR, type, op0, op1);
	      return fold_build2 (MULT_EXPR, type, diff,
			          fold_convert (type, esz));
			          
	    }
	}

      /* Try replacing &a[i1] - c * i2 with &a[i1 - i2], if c is step
	 of the array.  Loop optimizer sometimes produce this type of
	 expressions.  */
      if (TREE_CODE (arg0) == ADDR_EXPR)
	{
	  tem = try_move_mult_to_index (MINUS_EXPR, arg0, arg1);
	  if (tem)
	    return fold_convert (type, tem);
	}

      if (flag_unsafe_math_optimizations
	  && (TREE_CODE (arg0) == RDIV_EXPR || TREE_CODE (arg0) == MULT_EXPR)
	  && (TREE_CODE (arg1) == RDIV_EXPR || TREE_CODE (arg1) == MULT_EXPR)
	  && (tem = distribute_real_division (code, type, arg0, arg1)))
	return tem;

      /* Handle (A1 * C1) - (A2 * C2) with A1, A2 or C1, C2 being the
	 same or one.  */
      if ((TREE_CODE (arg0) == MULT_EXPR
	   || TREE_CODE (arg1) == MULT_EXPR)
	  && (!FLOAT_TYPE_P (type) || flag_unsafe_math_optimizations))
        {
	  tree tem = fold_plusminus_mult_expr (code, type, arg0, arg1);
	  if (tem)
	    return tem;
	}

      goto associate;

    case MULT_EXPR:
      /* (-A) * (-B) -> A * B  */
      if (TREE_CODE (arg0) == NEGATE_EXPR && negate_expr_p (arg1))
	return fold_build2 (MULT_EXPR, type,
			    fold_convert (type, TREE_OPERAND (arg0, 0)),
			    fold_convert (type, negate_expr (arg1)));
      if (TREE_CODE (arg1) == NEGATE_EXPR && negate_expr_p (arg0))
	return fold_build2 (MULT_EXPR, type,
			    fold_convert (type, negate_expr (arg0)),
			    fold_convert (type, TREE_OPERAND (arg1, 0)));

      if (! FLOAT_TYPE_P (type))
	{
	  if (integer_zerop (arg1))
	    return omit_one_operand (type, arg1, arg0);
	  if (integer_onep (arg1))
	    return non_lvalue (fold_convert (type, arg0));
	  /* Transform x * -1 into -x.  */
	  if (integer_all_onesp (arg1))
	    return fold_convert (type, negate_expr (arg0));

	  /* (a * (1 << b)) is (a << b)  */
	  if (TREE_CODE (arg1) == LSHIFT_EXPR
	      && integer_onep (TREE_OPERAND (arg1, 0)))
	    return fold_build2 (LSHIFT_EXPR, type, arg0,
				TREE_OPERAND (arg1, 1));
	  if (TREE_CODE (arg0) == LSHIFT_EXPR
	      && integer_onep (TREE_OPERAND (arg0, 0)))
	    return fold_build2 (LSHIFT_EXPR, type, arg1,
				TREE_OPERAND (arg0, 1));

	  strict_overflow_p = false;
	  if (TREE_CODE (arg1) == INTEGER_CST
	      && 0 != (tem = extract_muldiv (op0,
					     fold_convert (type, arg1),
					     code, NULL_TREE,
					     &strict_overflow_p)))
	    {
	      if (strict_overflow_p)
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when simplifying "
					"multiplication"),
				       WARN_STRICT_OVERFLOW_MISC);
	      return fold_convert (type, tem);
	    }

	  /* Optimize z * conj(z) for integer complex numbers.  */
	  if (TREE_CODE (arg0) == CONJ_EXPR
	      && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	    return fold_mult_zconjz (type, arg1);
	  if (TREE_CODE (arg1) == CONJ_EXPR
	      && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	    return fold_mult_zconjz (type, arg0);
	}
      else
	{
	  /* Maybe fold x * 0 to 0.  The expressions aren't the same
	     when x is NaN, since x * 0 is also NaN.  Nor are they the
	     same in modes with signed zeros, since multiplying a
	     negative value by 0 gives -0, not +0.  */
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0)))
	      && !HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (arg0)))
	      && real_zerop (arg1))
	    return omit_one_operand (type, arg1, arg0);
	  /* In IEEE floating point, x*1 is not equivalent to x for snans.  */
	  if (!HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg0)))
	      && real_onep (arg1))
	    return non_lvalue (fold_convert (type, arg0));

	  /* Transform x * -1.0 into -x.  */
	  if (!HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg0)))
	      && real_minus_onep (arg1))
	    return fold_convert (type, negate_expr (arg0));

	  /* Convert (C1/X)*C2 into (C1*C2)/X.  */
	  if (flag_unsafe_math_optimizations
	      && TREE_CODE (arg0) == RDIV_EXPR
	      && TREE_CODE (arg1) == REAL_CST
	      && TREE_CODE (TREE_OPERAND (arg0, 0)) == REAL_CST)
	    {
	      tree tem = const_binop (MULT_EXPR, TREE_OPERAND (arg0, 0),
				      arg1, 0);
	      if (tem)
		return fold_build2 (RDIV_EXPR, type, tem,
				    TREE_OPERAND (arg0, 1));
	    }

          /* Strip sign operations from X in X*X, i.e. -Y*-Y -> Y*Y.  */
	  if (operand_equal_p (arg0, arg1, 0))
	    {
	      tree tem = fold_strip_sign_ops (arg0);
	      if (tem != NULL_TREE)
		{
		  tem = fold_convert (type, tem);
		  return fold_build2 (MULT_EXPR, type, tem, tem);
		}
	    }

	  /* Optimize z * conj(z) for floating point complex numbers.
	     Guarded by flag_unsafe_math_optimizations as non-finite
	     imaginary components don't produce scalar results.  */
	  if (flag_unsafe_math_optimizations
	      && TREE_CODE (arg0) == CONJ_EXPR
	      && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	    return fold_mult_zconjz (type, arg1);
	  if (flag_unsafe_math_optimizations
	      && TREE_CODE (arg1) == CONJ_EXPR
	      && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	    return fold_mult_zconjz (type, arg0);

	  if (flag_unsafe_math_optimizations)
	    {
	      enum built_in_function fcode0 = builtin_mathfn_code (arg0);
	      enum built_in_function fcode1 = builtin_mathfn_code (arg1);

	      /* Optimizations of root(...)*root(...).  */
	      if (fcode0 == fcode1 && BUILTIN_ROOT_P (fcode0))
		{
		  tree rootfn, arg, arglist;
		  tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
		  tree arg10 = TREE_VALUE (TREE_OPERAND (arg1, 1));

		  /* Optimize sqrt(x)*sqrt(x) as x.  */
		  if (BUILTIN_SQRT_P (fcode0)
		      && operand_equal_p (arg00, arg10, 0)
		      && ! HONOR_SNANS (TYPE_MODE (type)))
		    return arg00;

	          /* Optimize root(x)*root(y) as root(x*y).  */
		  rootfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		  arg = fold_build2 (MULT_EXPR, type, arg00, arg10);
		  arglist = build_tree_list (NULL_TREE, arg);
		  return build_function_call_expr (rootfn, arglist);
		}

	      /* Optimize expN(x)*expN(y) as expN(x+y).  */
	      if (fcode0 == fcode1 && BUILTIN_EXPONENT_P (fcode0))
		{
		  tree expfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		  tree arg = fold_build2 (PLUS_EXPR, type,
					  TREE_VALUE (TREE_OPERAND (arg0, 1)),
					  TREE_VALUE (TREE_OPERAND (arg1, 1)));
		  tree arglist = build_tree_list (NULL_TREE, arg);
		  return build_function_call_expr (expfn, arglist);
		}

	      /* Optimizations of pow(...)*pow(...).  */
	      if ((fcode0 == BUILT_IN_POW && fcode1 == BUILT_IN_POW)
		  || (fcode0 == BUILT_IN_POWF && fcode1 == BUILT_IN_POWF)
		  || (fcode0 == BUILT_IN_POWL && fcode1 == BUILT_IN_POWL))
		{
		  tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
		  tree arg01 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg0,
								     1)));
		  tree arg10 = TREE_VALUE (TREE_OPERAND (arg1, 1));
		  tree arg11 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg1,
								     1)));

		  /* Optimize pow(x,y)*pow(z,y) as pow(x*z,y).  */
		  if (operand_equal_p (arg01, arg11, 0))
		    {
		      tree powfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		      tree arg = fold_build2 (MULT_EXPR, type, arg00, arg10);
		      tree arglist = tree_cons (NULL_TREE, arg,
						build_tree_list (NULL_TREE,
								 arg01));
		      return build_function_call_expr (powfn, arglist);
		    }

		  /* Optimize pow(x,y)*pow(x,z) as pow(x,y+z).  */
		  if (operand_equal_p (arg00, arg10, 0))
		    {
		      tree powfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		      tree arg = fold_build2 (PLUS_EXPR, type, arg01, arg11);
		      tree arglist = tree_cons (NULL_TREE, arg00,
						build_tree_list (NULL_TREE,
								 arg));
		      return build_function_call_expr (powfn, arglist);
		    }
		}

	      /* Optimize tan(x)*cos(x) as sin(x).  */
	      if (((fcode0 == BUILT_IN_TAN && fcode1 == BUILT_IN_COS)
		   || (fcode0 == BUILT_IN_TANF && fcode1 == BUILT_IN_COSF)
		   || (fcode0 == BUILT_IN_TANL && fcode1 == BUILT_IN_COSL)
		   || (fcode0 == BUILT_IN_COS && fcode1 == BUILT_IN_TAN)
		   || (fcode0 == BUILT_IN_COSF && fcode1 == BUILT_IN_TANF)
		   || (fcode0 == BUILT_IN_COSL && fcode1 == BUILT_IN_TANL))
		  && operand_equal_p (TREE_VALUE (TREE_OPERAND (arg0, 1)),
				      TREE_VALUE (TREE_OPERAND (arg1, 1)), 0))
		{
		  tree sinfn = mathfn_built_in (type, BUILT_IN_SIN);

		  if (sinfn != NULL_TREE)
		    return build_function_call_expr (sinfn,
						     TREE_OPERAND (arg0, 1));
		}

	      /* Optimize x*pow(x,c) as pow(x,c+1).  */
	      if (fcode1 == BUILT_IN_POW
		  || fcode1 == BUILT_IN_POWF
		  || fcode1 == BUILT_IN_POWL)
		{
		  tree arg10 = TREE_VALUE (TREE_OPERAND (arg1, 1));
		  tree arg11 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg1,
								     1)));
		  if (TREE_CODE (arg11) == REAL_CST
		      && ! TREE_CONSTANT_OVERFLOW (arg11)
		      && operand_equal_p (arg0, arg10, 0))
		    {
		      tree powfn = TREE_OPERAND (TREE_OPERAND (arg1, 0), 0);
		      REAL_VALUE_TYPE c;
		      tree arg, arglist;

		      c = TREE_REAL_CST (arg11);
		      real_arithmetic (&c, PLUS_EXPR, &c, &dconst1);
		      arg = build_real (type, c);
		      arglist = build_tree_list (NULL_TREE, arg);
		      arglist = tree_cons (NULL_TREE, arg0, arglist);
		      return build_function_call_expr (powfn, arglist);
		    }
		}

	      /* Optimize pow(x,c)*x as pow(x,c+1).  */
	      if (fcode0 == BUILT_IN_POW
		  || fcode0 == BUILT_IN_POWF
		  || fcode0 == BUILT_IN_POWL)
		{
		  tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
		  tree arg01 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg0,
								     1)));
		  if (TREE_CODE (arg01) == REAL_CST
		      && ! TREE_CONSTANT_OVERFLOW (arg01)
		      && operand_equal_p (arg1, arg00, 0))
		    {
		      tree powfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		      REAL_VALUE_TYPE c;
		      tree arg, arglist;

		      c = TREE_REAL_CST (arg01);
		      real_arithmetic (&c, PLUS_EXPR, &c, &dconst1);
		      arg = build_real (type, c);
		      arglist = build_tree_list (NULL_TREE, arg);
		      arglist = tree_cons (NULL_TREE, arg1, arglist);
		      return build_function_call_expr (powfn, arglist);
		    }
		}

	      /* Optimize x*x as pow(x,2.0), which is expanded as x*x.  */
	      if (! optimize_size
		  && operand_equal_p (arg0, arg1, 0))
		{
		  tree powfn = mathfn_built_in (type, BUILT_IN_POW);

		  if (powfn)
		    {
		      tree arg = build_real (type, dconst2);
		      tree arglist = build_tree_list (NULL_TREE, arg);
		      arglist = tree_cons (NULL_TREE, arg0, arglist);
		      return build_function_call_expr (powfn, arglist);
		    }
		}
	    }
	}
      goto associate;

    case BIT_IOR_EXPR:
    bit_ior:
      if (integer_all_onesp (arg1))
	return omit_one_operand (type, arg1, arg0);
      if (integer_zerop (arg1))
	return non_lvalue (fold_convert (type, arg0));
      if (operand_equal_p (arg0, arg1, 0))
	return non_lvalue (fold_convert (type, arg0));

      /* ~X | X is -1.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && INTEGRAL_TYPE_P (TREE_TYPE (arg1))
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	{
	  t1 = build_int_cst (type, -1);
	  t1 = force_fit_type (t1, 0, false, false);
	  return omit_one_operand (type, t1, arg1);
	}

      /* X | ~X is -1.  */
      if (TREE_CODE (arg1) == BIT_NOT_EXPR
	  && INTEGRAL_TYPE_P (TREE_TYPE (arg0))
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	{
	  t1 = build_int_cst (type, -1);
	  t1 = force_fit_type (t1, 0, false, false);
	  return omit_one_operand (type, t1, arg0);
	}

      /* Canonicalize (X & C1) | C2.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	{
	  unsigned HOST_WIDE_INT hi1, lo1, hi2, lo2, mlo, mhi;
	  int width = TYPE_PRECISION (type);
	  hi1 = TREE_INT_CST_HIGH (TREE_OPERAND (arg0, 1));
	  lo1 = TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1));
	  hi2 = TREE_INT_CST_HIGH (arg1);
	  lo2 = TREE_INT_CST_LOW (arg1);

	  /* If (C1&C2) == C1, then (X&C1)|C2 becomes (X,C2).  */
	  if ((hi1 & hi2) == hi1 && (lo1 & lo2) == lo1)
	    return omit_one_operand (type, arg1, TREE_OPERAND (arg0, 0));

	  if (width > HOST_BITS_PER_WIDE_INT)
	    {
	      mhi = (unsigned HOST_WIDE_INT) -1 
		    >> (2 * HOST_BITS_PER_WIDE_INT - width);
	      mlo = -1;
	    }
	  else
	    {
	      mhi = 0;
	      mlo = (unsigned HOST_WIDE_INT) -1
		    >> (HOST_BITS_PER_WIDE_INT - width);
	    }

	  /* If (C1|C2) == ~0 then (X&C1)|C2 becomes X|C2.  */
	  if ((~(hi1 | hi2) & mhi) == 0 && (~(lo1 | lo2) & mlo) == 0)
	    return fold_build2 (BIT_IOR_EXPR, type,
				TREE_OPERAND (arg0, 0), arg1);

	  /* Minimize the number of bits set in C1, i.e. C1 := C1 & ~C2.  */
	  hi1 &= mhi;
	  lo1 &= mlo;
	  if ((hi1 & ~hi2) != hi1 || (lo1 & ~lo2) != lo1)
	    return fold_build2 (BIT_IOR_EXPR, type,
				fold_build2 (BIT_AND_EXPR, type,
					     TREE_OPERAND (arg0, 0),
					     build_int_cst_wide (type,
								 lo1 & ~lo2,
								 hi1 & ~hi2)),
				arg1);
	}

      /* (X & Y) | Y is (X, Y).  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	return omit_one_operand (type, arg1, TREE_OPERAND (arg0, 0));
      /* (X & Y) | X is (Y, X).  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && reorder_operands_p (TREE_OPERAND (arg0, 1), arg1))
	return omit_one_operand (type, arg1, TREE_OPERAND (arg0, 1));
      /* X | (X & Y) is (Y, X).  */
      if (TREE_CODE (arg1) == BIT_AND_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 1)))
	return omit_one_operand (type, arg0, TREE_OPERAND (arg1, 1));
      /* X | (Y & X) is (Y, X).  */
      if (TREE_CODE (arg1) == BIT_AND_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 1), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 0)))
	return omit_one_operand (type, arg0, TREE_OPERAND (arg1, 0));

      t1 = distribute_bit_expr (code, type, arg0, arg1);
      if (t1 != NULL_TREE)
	return t1;

      /* Convert (or (not arg0) (not arg1)) to (not (and (arg0) (arg1))).

	 This results in more efficient code for machines without a NAND
	 instruction.  Combine will canonicalize to the first form
	 which will allow use of NAND instructions provided by the
	 backend if they exist.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && TREE_CODE (arg1) == BIT_NOT_EXPR)
	{
	  return fold_build1 (BIT_NOT_EXPR, type,
			      build2 (BIT_AND_EXPR, type,
				      TREE_OPERAND (arg0, 0),
				      TREE_OPERAND (arg1, 0)));
	}

      /* See if this can be simplified into a rotate first.  If that
	 is unsuccessful continue in the association code.  */
      goto bit_rotate;

    case BIT_XOR_EXPR:
      if (integer_zerop (arg1))
	return non_lvalue (fold_convert (type, arg0));
      if (integer_all_onesp (arg1))
	return fold_build1 (BIT_NOT_EXPR, type, arg0);
      if (operand_equal_p (arg0, arg1, 0))
	return omit_one_operand (type, integer_zero_node, arg0);

      /* ~X ^ X is -1.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && INTEGRAL_TYPE_P (TREE_TYPE (arg1))
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	{
	  t1 = build_int_cst (type, -1);
	  t1 = force_fit_type (t1, 0, false, false);
	  return omit_one_operand (type, t1, arg1);
	}

      /* X ^ ~X is -1.  */
      if (TREE_CODE (arg1) == BIT_NOT_EXPR
	  && INTEGRAL_TYPE_P (TREE_TYPE (arg0))
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	{
	  t1 = build_int_cst (type, -1);
	  t1 = force_fit_type (t1, 0, false, false);
	  return omit_one_operand (type, t1, arg0);
	}

      /* If we are XORing two BIT_AND_EXPR's, both of which are and'ing
         with a constant, and the two constants have no bits in common,
	 we should treat this as a BIT_IOR_EXPR since this may produce more
	 simplifications.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (arg1) == BIT_AND_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg1, 1)) == INTEGER_CST
	  && integer_zerop (const_binop (BIT_AND_EXPR,
					 TREE_OPERAND (arg0, 1),
					 TREE_OPERAND (arg1, 1), 0)))
	{
	  code = BIT_IOR_EXPR;
	  goto bit_ior;
	}

      /* (X | Y) ^ X -> Y & ~ X*/
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
          && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
        {
	  tree t2 = TREE_OPERAND (arg0, 1);
	  t1 = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (arg1),
			    arg1);
	  t1 = fold_build2 (BIT_AND_EXPR, type, fold_convert (type, t2),
			    fold_convert (type, t1));
	  return t1;
	}

      /* (Y | X) ^ X -> Y & ~ X*/
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
          && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
        {
	  tree t2 = TREE_OPERAND (arg0, 0);
	  t1 = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (arg1),
			    arg1);
	  t1 = fold_build2 (BIT_AND_EXPR, type, fold_convert (type, t2),
			    fold_convert (type, t1));
	  return t1;
	}

      /* X ^ (X | Y) -> Y & ~ X*/
      if (TREE_CODE (arg1) == BIT_IOR_EXPR
          && operand_equal_p (TREE_OPERAND (arg1, 0), arg0, 0))
        {
	  tree t2 = TREE_OPERAND (arg1, 1);
	  t1 = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (arg0),
			    arg0);
	  t1 = fold_build2 (BIT_AND_EXPR, type, fold_convert (type, t2),
			    fold_convert (type, t1));
	  return t1;
	}

      /* X ^ (Y | X) -> Y & ~ X*/
      if (TREE_CODE (arg1) == BIT_IOR_EXPR
          && operand_equal_p (TREE_OPERAND (arg1, 1), arg0, 0))
        {
	  tree t2 = TREE_OPERAND (arg1, 0);
	  t1 = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (arg0),
			    arg0);
	  t1 = fold_build2 (BIT_AND_EXPR, type, fold_convert (type, t2),
			    fold_convert (type, t1));
	  return t1;
	}
	
      /* Convert ~X ^ ~Y to X ^ Y.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && TREE_CODE (arg1) == BIT_NOT_EXPR)
	return fold_build2 (code, type,
			    fold_convert (type, TREE_OPERAND (arg0, 0)),
			    fold_convert (type, TREE_OPERAND (arg1, 0)));

      /* Fold (X & 1) ^ 1 as (X & 1) == 0.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && integer_onep (TREE_OPERAND (arg0, 1))
	  && integer_onep (arg1))
	return fold_build2 (EQ_EXPR, type, arg0,
			    build_int_cst (TREE_TYPE (arg0), 0));

      /* Fold (X & Y) ^ Y as ~X & Y.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg0, 0));
	  return fold_build2 (BIT_AND_EXPR, type, 
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg1));
	}
      /* Fold (X & Y) ^ X as ~Y & X.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && reorder_operands_p (TREE_OPERAND (arg0, 1), arg1))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg0, 1));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg1));
	}
      /* Fold X ^ (X & Y) as X & ~Y.  */
      if (TREE_CODE (arg1) == BIT_AND_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg1, 1));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_convert (type, arg0),
			      fold_build1 (BIT_NOT_EXPR, type, tem));
	}
      /* Fold X ^ (Y & X) as ~Y & X.  */
      if (TREE_CODE (arg1) == BIT_AND_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 1), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 0)))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg1, 0));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg0));
	}

      /* See if this can be simplified into a rotate first.  If that
	 is unsuccessful continue in the association code.  */
      goto bit_rotate;

    case BIT_AND_EXPR:
      if (integer_all_onesp (arg1))
	return non_lvalue (fold_convert (type, arg0));
      if (integer_zerop (arg1))
	return omit_one_operand (type, arg1, arg0);
      if (operand_equal_p (arg0, arg1, 0))
	return non_lvalue (fold_convert (type, arg0));

      /* ~X & X is always zero.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	return omit_one_operand (type, integer_zero_node, arg1);

      /* X & ~X is always zero.  */
      if (TREE_CODE (arg1) == BIT_NOT_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	return omit_one_operand (type, integer_zero_node, arg0);

      /* Canonicalize (X | C1) & C2 as (X & C2) | (C1 & C2).  */
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	return fold_build2 (BIT_IOR_EXPR, type,
			    fold_build2 (BIT_AND_EXPR, type,
					 TREE_OPERAND (arg0, 0), arg1),
			    fold_build2 (BIT_AND_EXPR, type,
					 TREE_OPERAND (arg0, 1), arg1));

      /* (X | Y) & Y is (X, Y).  */
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	return omit_one_operand (type, arg1, TREE_OPERAND (arg0, 0));
      /* (X | Y) & X is (Y, X).  */
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && reorder_operands_p (TREE_OPERAND (arg0, 1), arg1))
	return omit_one_operand (type, arg1, TREE_OPERAND (arg0, 1));
      /* X & (X | Y) is (Y, X).  */
      if (TREE_CODE (arg1) == BIT_IOR_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 1)))
	return omit_one_operand (type, arg0, TREE_OPERAND (arg1, 1));
      /* X & (Y | X) is (Y, X).  */
      if (TREE_CODE (arg1) == BIT_IOR_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 1), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 0)))
	return omit_one_operand (type, arg0, TREE_OPERAND (arg1, 0));

      /* Fold (X ^ 1) & 1 as (X & 1) == 0.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && integer_onep (TREE_OPERAND (arg0, 1))
	  && integer_onep (arg1))
	{
	  tem = TREE_OPERAND (arg0, 0);
	  return fold_build2 (EQ_EXPR, type,
			      fold_build2 (BIT_AND_EXPR, TREE_TYPE (tem), tem,
					   build_int_cst (TREE_TYPE (tem), 1)),
			      build_int_cst (TREE_TYPE (tem), 0));
	}
      /* Fold ~X & 1 as (X & 1) == 0.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && integer_onep (arg1))
	{
	  tem = TREE_OPERAND (arg0, 0);
	  return fold_build2 (EQ_EXPR, type,
			      fold_build2 (BIT_AND_EXPR, TREE_TYPE (tem), tem,
					   build_int_cst (TREE_TYPE (tem), 1)),
			      build_int_cst (TREE_TYPE (tem), 0));
	}

      /* Fold (X ^ Y) & Y as ~X & Y.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg0, 0));
	  return fold_build2 (BIT_AND_EXPR, type, 
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg1));
	}
      /* Fold (X ^ Y) & X as ~Y & X.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && reorder_operands_p (TREE_OPERAND (arg0, 1), arg1))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg0, 1));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg1));
	}
      /* Fold X & (X ^ Y) as X & ~Y.  */
      if (TREE_CODE (arg1) == BIT_XOR_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg1, 1));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_convert (type, arg0),
			      fold_build1 (BIT_NOT_EXPR, type, tem));
	}
      /* Fold X & (Y ^ X) as ~Y & X.  */
      if (TREE_CODE (arg1) == BIT_XOR_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 1), 0)
	  && reorder_operands_p (arg0, TREE_OPERAND (arg1, 0)))
	{
	  tem = fold_convert (type, TREE_OPERAND (arg1, 0));
	  return fold_build2 (BIT_AND_EXPR, type,
			      fold_build1 (BIT_NOT_EXPR, type, tem),
			      fold_convert (type, arg0));
	}

      t1 = distribute_bit_expr (code, type, arg0, arg1);
      if (t1 != NULL_TREE)
	return t1;
      /* Simplify ((int)c & 0377) into (int)c, if c is unsigned char.  */
      if (TREE_CODE (arg1) == INTEGER_CST && TREE_CODE (arg0) == NOP_EXPR
	  && TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (arg0, 0))))
	{
	  unsigned int prec
	    = TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (arg0, 0)));

	  if (prec < BITS_PER_WORD && prec < HOST_BITS_PER_WIDE_INT
	      && (~TREE_INT_CST_LOW (arg1)
		  & (((HOST_WIDE_INT) 1 << prec) - 1)) == 0)
	    return fold_convert (type, TREE_OPERAND (arg0, 0));
	}

      /* Convert (and (not arg0) (not arg1)) to (not (or (arg0) (arg1))).

	 This results in more efficient code for machines without a NOR
	 instruction.  Combine will canonicalize to the first form
	 which will allow use of NOR instructions provided by the
	 backend if they exist.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && TREE_CODE (arg1) == BIT_NOT_EXPR)
	{
	  return fold_build1 (BIT_NOT_EXPR, type,
			      build2 (BIT_IOR_EXPR, type,
				      TREE_OPERAND (arg0, 0),
				      TREE_OPERAND (arg1, 0)));
	}

      goto associate;

    case RDIV_EXPR:
      /* Don't touch a floating-point divide by zero unless the mode
	 of the constant can represent infinity.  */
      if (TREE_CODE (arg1) == REAL_CST
	  && !MODE_HAS_INFINITIES (TYPE_MODE (TREE_TYPE (arg1)))
	  && real_zerop (arg1))
	return NULL_TREE;

      /* Optimize A / A to 1.0 if we don't care about
	 NaNs or Infinities.  Skip the transformation
	 for non-real operands.  */
      if (SCALAR_FLOAT_TYPE_P (TREE_TYPE (arg0))
	  && ! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0)))
	  && ! HONOR_INFINITIES (TYPE_MODE (TREE_TYPE (arg0)))
	  && operand_equal_p (arg0, arg1, 0))
	{
	  tree r = build_real (TREE_TYPE (arg0), dconst1);

	  return omit_two_operands (type, r, arg0, arg1);
	}

      /* The complex version of the above A / A optimization.  */
      if (COMPLEX_FLOAT_TYPE_P (TREE_TYPE (arg0))
	  && operand_equal_p (arg0, arg1, 0))
	{
	  tree elem_type = TREE_TYPE (TREE_TYPE (arg0));
	  if (! HONOR_NANS (TYPE_MODE (elem_type))
	      && ! HONOR_INFINITIES (TYPE_MODE (elem_type)))
	    {
	      tree r = build_real (elem_type, dconst1);
	      /* omit_two_operands will call fold_convert for us.  */
	      return omit_two_operands (type, r, arg0, arg1);
	    }
	}

      /* (-A) / (-B) -> A / B  */
      if (TREE_CODE (arg0) == NEGATE_EXPR && negate_expr_p (arg1))
	return fold_build2 (RDIV_EXPR, type,
			    TREE_OPERAND (arg0, 0),
			    negate_expr (arg1));
      if (TREE_CODE (arg1) == NEGATE_EXPR && negate_expr_p (arg0))
	return fold_build2 (RDIV_EXPR, type,
			    negate_expr (arg0),
			    TREE_OPERAND (arg1, 0));

      /* In IEEE floating point, x/1 is not equivalent to x for snans.  */
      if (!HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg0)))
	  && real_onep (arg1))
	return non_lvalue (fold_convert (type, arg0));

      /* In IEEE floating point, x/-1 is not equivalent to -x for snans.  */
      if (!HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg0)))
	  && real_minus_onep (arg1))
	return non_lvalue (fold_convert (type, negate_expr (arg0)));

      /* If ARG1 is a constant, we can convert this to a multiply by the
	 reciprocal.  This does not have the same rounding properties,
	 so only do this if -funsafe-math-optimizations.  We can actually
	 always safely do it if ARG1 is a power of two, but it's hard to
	 tell if it is or not in a portable manner.  */
      if (TREE_CODE (arg1) == REAL_CST)
	{
	  if (flag_unsafe_math_optimizations
	      && 0 != (tem = const_binop (code, build_real (type, dconst1),
					  arg1, 0)))
	    return fold_build2 (MULT_EXPR, type, arg0, tem);
	  /* Find the reciprocal if optimizing and the result is exact.  */
	  if (optimize)
	    {
	      REAL_VALUE_TYPE r;
	      r = TREE_REAL_CST (arg1);
	      if (exact_real_inverse (TYPE_MODE(TREE_TYPE(arg0)), &r))
		{
		  tem = build_real (type, r);
		  return fold_build2 (MULT_EXPR, type,
				      fold_convert (type, arg0), tem);
		}
	    }
	}
      /* Convert A/B/C to A/(B*C).  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg0) == RDIV_EXPR)
	return fold_build2 (RDIV_EXPR, type, TREE_OPERAND (arg0, 0),
			    fold_build2 (MULT_EXPR, type,
					 TREE_OPERAND (arg0, 1), arg1));

      /* Convert A/(B/C) to (A/B)*C.  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg1) == RDIV_EXPR)
	return fold_build2 (MULT_EXPR, type,
			    fold_build2 (RDIV_EXPR, type, arg0,
					 TREE_OPERAND (arg1, 0)),
			    TREE_OPERAND (arg1, 1));

      /* Convert C1/(X*C2) into (C1/C2)/X.  */
      if (flag_unsafe_math_optimizations
	  && TREE_CODE (arg1) == MULT_EXPR
	  && TREE_CODE (arg0) == REAL_CST
	  && TREE_CODE (TREE_OPERAND (arg1, 1)) == REAL_CST)
	{
	  tree tem = const_binop (RDIV_EXPR, arg0,
				  TREE_OPERAND (arg1, 1), 0);
	  if (tem)
	    return fold_build2 (RDIV_EXPR, type, tem,
				TREE_OPERAND (arg1, 0));
	}

      if (flag_unsafe_math_optimizations)
	{
	  enum built_in_function fcode0 = builtin_mathfn_code (arg0);
	  enum built_in_function fcode1 = builtin_mathfn_code (arg1);

	  /* Optimize sin(x)/cos(x) as tan(x).  */
	  if (((fcode0 == BUILT_IN_SIN && fcode1 == BUILT_IN_COS)
	       || (fcode0 == BUILT_IN_SINF && fcode1 == BUILT_IN_COSF)
	       || (fcode0 == BUILT_IN_SINL && fcode1 == BUILT_IN_COSL))
	      && operand_equal_p (TREE_VALUE (TREE_OPERAND (arg0, 1)),
				  TREE_VALUE (TREE_OPERAND (arg1, 1)), 0))
	    {
	      tree tanfn = mathfn_built_in (type, BUILT_IN_TAN);

	      if (tanfn != NULL_TREE)
		return build_function_call_expr (tanfn,
						 TREE_OPERAND (arg0, 1));
	    }

	  /* Optimize cos(x)/sin(x) as 1.0/tan(x).  */
	  if (((fcode0 == BUILT_IN_COS && fcode1 == BUILT_IN_SIN)
	       || (fcode0 == BUILT_IN_COSF && fcode1 == BUILT_IN_SINF)
	       || (fcode0 == BUILT_IN_COSL && fcode1 == BUILT_IN_SINL))
	      && operand_equal_p (TREE_VALUE (TREE_OPERAND (arg0, 1)),
				  TREE_VALUE (TREE_OPERAND (arg1, 1)), 0))
	    {
	      tree tanfn = mathfn_built_in (type, BUILT_IN_TAN);

	      if (tanfn != NULL_TREE)
		{
		  tree tmp = TREE_OPERAND (arg0, 1);
		  tmp = build_function_call_expr (tanfn, tmp);
		  return fold_build2 (RDIV_EXPR, type,
				      build_real (type, dconst1), tmp);
		}
	    }

 	  /* Optimize sin(x)/tan(x) as cos(x) if we don't care about
	     NaNs or Infinities.  */
 	  if (((fcode0 == BUILT_IN_SIN && fcode1 == BUILT_IN_TAN)
 	       || (fcode0 == BUILT_IN_SINF && fcode1 == BUILT_IN_TANF)
 	       || (fcode0 == BUILT_IN_SINL && fcode1 == BUILT_IN_TANL)))
	    {
	      tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
	      tree arg01 = TREE_VALUE (TREE_OPERAND (arg1, 1));

	      if (! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg00)))
		  && ! HONOR_INFINITIES (TYPE_MODE (TREE_TYPE (arg00)))
		  && operand_equal_p (arg00, arg01, 0))
		{
		  tree cosfn = mathfn_built_in (type, BUILT_IN_COS);

		  if (cosfn != NULL_TREE)
		    return build_function_call_expr (cosfn,
						     TREE_OPERAND (arg0, 1));
		}
	    }

 	  /* Optimize tan(x)/sin(x) as 1.0/cos(x) if we don't care about
	     NaNs or Infinities.  */
 	  if (((fcode0 == BUILT_IN_TAN && fcode1 == BUILT_IN_SIN)
 	       || (fcode0 == BUILT_IN_TANF && fcode1 == BUILT_IN_SINF)
 	       || (fcode0 == BUILT_IN_TANL && fcode1 == BUILT_IN_SINL)))
	    {
	      tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
	      tree arg01 = TREE_VALUE (TREE_OPERAND (arg1, 1));

	      if (! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg00)))
		  && ! HONOR_INFINITIES (TYPE_MODE (TREE_TYPE (arg00)))
		  && operand_equal_p (arg00, arg01, 0))
		{
		  tree cosfn = mathfn_built_in (type, BUILT_IN_COS);

		  if (cosfn != NULL_TREE)
		    {
		      tree tmp = TREE_OPERAND (arg0, 1);
		      tmp = build_function_call_expr (cosfn, tmp);
		      return fold_build2 (RDIV_EXPR, type,
					  build_real (type, dconst1),
					  tmp);
		    }
		}
	    }

	  /* Optimize pow(x,c)/x as pow(x,c-1).  */
	  if (fcode0 == BUILT_IN_POW
	      || fcode0 == BUILT_IN_POWF
	      || fcode0 == BUILT_IN_POWL)
	    {
	      tree arg00 = TREE_VALUE (TREE_OPERAND (arg0, 1));
	      tree arg01 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg0, 1)));
	      if (TREE_CODE (arg01) == REAL_CST
		  && ! TREE_CONSTANT_OVERFLOW (arg01)
		  && operand_equal_p (arg1, arg00, 0))
		{
		  tree powfn = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
		  REAL_VALUE_TYPE c;
		  tree arg, arglist;

		  c = TREE_REAL_CST (arg01);
		  real_arithmetic (&c, MINUS_EXPR, &c, &dconst1);
		  arg = build_real (type, c);
		  arglist = build_tree_list (NULL_TREE, arg);
		  arglist = tree_cons (NULL_TREE, arg1, arglist);
		  return build_function_call_expr (powfn, arglist);
		}
	    }

	  /* Optimize x/expN(y) into x*expN(-y).  */
	  if (BUILTIN_EXPONENT_P (fcode1))
	    {
	      tree expfn = TREE_OPERAND (TREE_OPERAND (arg1, 0), 0);
	      tree arg = negate_expr (TREE_VALUE (TREE_OPERAND (arg1, 1)));
	      tree arglist = build_tree_list (NULL_TREE,
					      fold_convert (type, arg));
	      arg1 = build_function_call_expr (expfn, arglist);
	      return fold_build2 (MULT_EXPR, type, arg0, arg1);
	    }

	  /* Optimize x/pow(y,z) into x*pow(y,-z).  */
	  if (fcode1 == BUILT_IN_POW
	      || fcode1 == BUILT_IN_POWF
	      || fcode1 == BUILT_IN_POWL)
	    {
	      tree powfn = TREE_OPERAND (TREE_OPERAND (arg1, 0), 0);
	      tree arg10 = TREE_VALUE (TREE_OPERAND (arg1, 1));
	      tree arg11 = TREE_VALUE (TREE_CHAIN (TREE_OPERAND (arg1, 1)));
	      tree neg11 = fold_convert (type, negate_expr (arg11));
	      tree arglist = tree_cons(NULL_TREE, arg10,
				       build_tree_list (NULL_TREE, neg11));
	      arg1 = build_function_call_expr (powfn, arglist);
	      return fold_build2 (MULT_EXPR, type, arg0, arg1);
	    }
	}
      return NULL_TREE;

    case TRUNC_DIV_EXPR:
    case FLOOR_DIV_EXPR:
      /* Simplify A / (B << N) where A and B are positive and B is
	 a power of 2, to A >> (N + log2(B)).  */
      strict_overflow_p = false;
      if (TREE_CODE (arg1) == LSHIFT_EXPR
	  && (TYPE_UNSIGNED (type)
	      || tree_expr_nonnegative_warnv_p (arg0, &strict_overflow_p)))
	{
	  tree sval = TREE_OPERAND (arg1, 0);
	  if (integer_pow2p (sval) && tree_int_cst_sgn (sval) > 0)
	    {
	      tree sh_cnt = TREE_OPERAND (arg1, 1);
	      unsigned long pow2 = exact_log2 (TREE_INT_CST_LOW (sval));

	      if (strict_overflow_p)
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when simplifying A / (B << N)"),
				       WARN_STRICT_OVERFLOW_MISC);

	      sh_cnt = fold_build2 (PLUS_EXPR, TREE_TYPE (sh_cnt),
				    sh_cnt, build_int_cst (NULL_TREE, pow2));
	      return fold_build2 (RSHIFT_EXPR, type,
				  fold_convert (type, arg0), sh_cnt);
	    }
	}
      /* Fall thru */

    case ROUND_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case EXACT_DIV_EXPR:
      if (integer_onep (arg1))
	return non_lvalue (fold_convert (type, arg0));
      if (integer_zerop (arg1))
	return NULL_TREE;
      /* X / -1 is -X.  */
      if (!TYPE_UNSIGNED (type)
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_INT_CST_LOW (arg1) == (unsigned HOST_WIDE_INT) -1
	  && TREE_INT_CST_HIGH (arg1) == -1)
	return fold_convert (type, negate_expr (arg0));

      /* Convert -A / -B to A / B when the type is signed and overflow is
	 undefined.  */
      if ((!INTEGRAL_TYPE_P (type) || TYPE_OVERFLOW_UNDEFINED (type))
	  && TREE_CODE (arg0) == NEGATE_EXPR
	  && negate_expr_p (arg1))
	{
	  if (INTEGRAL_TYPE_P (type))
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when distributing negation across "
				    "division"),
				   WARN_STRICT_OVERFLOW_MISC);
	  return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			      negate_expr (arg1));
	}
      if ((!INTEGRAL_TYPE_P (type) || TYPE_OVERFLOW_UNDEFINED (type))
	  && TREE_CODE (arg1) == NEGATE_EXPR
	  && negate_expr_p (arg0))
	{
	  if (INTEGRAL_TYPE_P (type))
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when distributing negation across "
				    "division"),
				   WARN_STRICT_OVERFLOW_MISC);
	  return fold_build2 (code, type, negate_expr (arg0),
			      TREE_OPERAND (arg1, 0));
	}

      /* If arg0 is a multiple of arg1, then rewrite to the fastest div
	 operation, EXACT_DIV_EXPR.

	 Note that only CEIL_DIV_EXPR and FLOOR_DIV_EXPR are rewritten now.
	 At one time others generated faster code, it's not clear if they do
	 after the last round to changes to the DIV code in expmed.c.  */
      if ((code == CEIL_DIV_EXPR || code == FLOOR_DIV_EXPR)
	  && multiple_of_p (type, arg0, arg1))
	return fold_build2 (EXACT_DIV_EXPR, type, arg0, arg1);

      strict_overflow_p = false;
      if (TREE_CODE (arg1) == INTEGER_CST
	  && 0 != (tem = extract_muldiv (op0, arg1, code, NULL_TREE,
					 &strict_overflow_p)))
	{
	  if (strict_overflow_p)
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when simplifying division"),
				   WARN_STRICT_OVERFLOW_MISC);
	  return fold_convert (type, tem);
	}

      return NULL_TREE;

    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case TRUNC_MOD_EXPR:
      /* X % 1 is always zero, but be sure to preserve any side
	 effects in X.  */
      if (integer_onep (arg1))
	return omit_one_operand (type, integer_zero_node, arg0);

      /* X % 0, return X % 0 unchanged so that we can get the
	 proper warnings and errors.  */
      if (integer_zerop (arg1))
	return NULL_TREE;

      /* 0 % X is always zero, but be sure to preserve any side
	 effects in X.  Place this after checking for X == 0.  */
      if (integer_zerop (arg0))
	return omit_one_operand (type, integer_zero_node, arg1);

      /* X % -1 is zero.  */
      if (!TYPE_UNSIGNED (type)
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_INT_CST_LOW (arg1) == (unsigned HOST_WIDE_INT) -1
	  && TREE_INT_CST_HIGH (arg1) == -1)
	return omit_one_operand (type, integer_zero_node, arg0);

      /* Optimize TRUNC_MOD_EXPR by a power of two into a BIT_AND_EXPR,
         i.e. "X % C" into "X & (C - 1)", if X and C are positive.  */
      strict_overflow_p = false;
      if ((code == TRUNC_MOD_EXPR || code == FLOOR_MOD_EXPR)
	  && (TYPE_UNSIGNED (type)
	      || tree_expr_nonnegative_warnv_p (arg0, &strict_overflow_p)))
	{
	  tree c = arg1;
	  /* Also optimize A % (C << N)  where C is a power of 2,
	     to A & ((C << N) - 1).  */
	  if (TREE_CODE (arg1) == LSHIFT_EXPR)
	    c = TREE_OPERAND (arg1, 0);

	  if (integer_pow2p (c) && tree_int_cst_sgn (c) > 0)
	    {
	      tree mask = fold_build2 (MINUS_EXPR, TREE_TYPE (arg1),
				       arg1, integer_one_node);
	      if (strict_overflow_p)
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when simplifying "
					"X % (power of two)"),
				       WARN_STRICT_OVERFLOW_MISC);
	      return fold_build2 (BIT_AND_EXPR, type,
				  fold_convert (type, arg0),
				  fold_convert (type, mask));
	    }
	}

      /* X % -C is the same as X % C.  */
      if (code == TRUNC_MOD_EXPR
	  && !TYPE_UNSIGNED (type)
	  && TREE_CODE (arg1) == INTEGER_CST
	  && !TREE_CONSTANT_OVERFLOW (arg1)
	  && TREE_INT_CST_HIGH (arg1) < 0
	  && !TYPE_OVERFLOW_TRAPS (type)
	  /* Avoid this transformation if C is INT_MIN, i.e. C == -C.  */
	  && !sign_bit_p (arg1, arg1))
	return fold_build2 (code, type, fold_convert (type, arg0),
			    fold_convert (type, negate_expr (arg1)));

      /* X % -Y is the same as X % Y.  */
      if (code == TRUNC_MOD_EXPR
	  && !TYPE_UNSIGNED (type)
	  && TREE_CODE (arg1) == NEGATE_EXPR
	  && !TYPE_OVERFLOW_TRAPS (type))
	return fold_build2 (code, type, fold_convert (type, arg0),
			    fold_convert (type, TREE_OPERAND (arg1, 0)));

      if (TREE_CODE (arg1) == INTEGER_CST
	  && 0 != (tem = extract_muldiv (op0, arg1, code, NULL_TREE,
					 &strict_overflow_p)))
	{
	  if (strict_overflow_p)
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when simplifying modulos"),
				   WARN_STRICT_OVERFLOW_MISC);
	  return fold_convert (type, tem);
	}

      return NULL_TREE;

    case LROTATE_EXPR:
    case RROTATE_EXPR:
      if (integer_all_onesp (arg0))
	return omit_one_operand (type, arg0, arg1);
      goto shift;

    case RSHIFT_EXPR:
      /* Optimize -1 >> x for arithmetic right shifts.  */
      if (integer_all_onesp (arg0) && !TYPE_UNSIGNED (type))
	return omit_one_operand (type, arg0, arg1);
      /* ... fall through ...  */

    case LSHIFT_EXPR:
    shift:
      if (integer_zerop (arg1))
	return non_lvalue (fold_convert (type, arg0));
      if (integer_zerop (arg0))
	return omit_one_operand (type, arg0, arg1);

      /* Since negative shift count is not well-defined,
	 don't try to compute it in the compiler.  */
      if (TREE_CODE (arg1) == INTEGER_CST && tree_int_cst_sgn (arg1) < 0)
	return NULL_TREE;

      /* Turn (a OP c1) OP c2 into a OP (c1+c2).  */
      if (TREE_CODE (op0) == code && host_integerp (arg1, false)
	  && TREE_INT_CST_LOW (arg1) < TYPE_PRECISION (type)
	  && host_integerp (TREE_OPERAND (arg0, 1), false)
	  && TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1)) < TYPE_PRECISION (type))
	{
	  HOST_WIDE_INT low = (TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1))
			       + TREE_INT_CST_LOW (arg1));

	  /* Deal with a OP (c1 + c2) being undefined but (a OP c1) OP c2
	     being well defined.  */
	  if (low >= TYPE_PRECISION (type))
	    {
	      if (code == LROTATE_EXPR || code == RROTATE_EXPR)
	        low = low % TYPE_PRECISION (type);
	      else if (TYPE_UNSIGNED (type) || code == LSHIFT_EXPR)
	        return build_int_cst (type, 0);
	      else
		low = TYPE_PRECISION (type) - 1;
	    }

	  return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			      build_int_cst (type, low));
	}

      /* Transform (x >> c) << c into x & (-1<<c), or transform (x << c) >> c
         into x & ((unsigned)-1 >> c) for unsigned types.  */
      if (((code == LSHIFT_EXPR && TREE_CODE (arg0) == RSHIFT_EXPR)
           || (TYPE_UNSIGNED (type)
	       && code == RSHIFT_EXPR && TREE_CODE (arg0) == LSHIFT_EXPR))
	  && host_integerp (arg1, false)
	  && TREE_INT_CST_LOW (arg1) < TYPE_PRECISION (type)
	  && host_integerp (TREE_OPERAND (arg0, 1), false)
	  && TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1)) < TYPE_PRECISION (type))
	{
	  HOST_WIDE_INT low0 = TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1));
	  HOST_WIDE_INT low1 = TREE_INT_CST_LOW (arg1);
	  tree lshift;
	  tree arg00;

	  if (low0 == low1)
	    {
	      arg00 = fold_convert (type, TREE_OPERAND (arg0, 0));

	      lshift = build_int_cst (type, -1);
	      lshift = int_const_binop (code, lshift, arg1, 0);

	      return fold_build2 (BIT_AND_EXPR, type, arg00, lshift);
	    }
	}

      /* Rewrite an LROTATE_EXPR by a constant into an
	 RROTATE_EXPR by a new constant.  */
      if (code == LROTATE_EXPR && TREE_CODE (arg1) == INTEGER_CST)
	{
	  tree tem = build_int_cst (NULL_TREE,
				    GET_MODE_BITSIZE (TYPE_MODE (type)));
	  tem = fold_convert (TREE_TYPE (arg1), tem);
	  tem = const_binop (MINUS_EXPR, tem, arg1, 0);
	  return fold_build2 (RROTATE_EXPR, type, arg0, tem);
	}

      /* If we have a rotate of a bit operation with the rotate count and
	 the second operand of the bit operation both constant,
	 permute the two operations.  */
      if (code == RROTATE_EXPR && TREE_CODE (arg1) == INTEGER_CST
	  && (TREE_CODE (arg0) == BIT_AND_EXPR
	      || TREE_CODE (arg0) == BIT_IOR_EXPR
	      || TREE_CODE (arg0) == BIT_XOR_EXPR)
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	return fold_build2 (TREE_CODE (arg0), type,
			    fold_build2 (code, type,
					 TREE_OPERAND (arg0, 0), arg1),
			    fold_build2 (code, type,
					 TREE_OPERAND (arg0, 1), arg1));

      /* Two consecutive rotates adding up to the width of the mode can
	 be ignored.  */
      if (code == RROTATE_EXPR && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (arg0) == RROTATE_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
	  && TREE_INT_CST_HIGH (arg1) == 0
	  && TREE_INT_CST_HIGH (TREE_OPERAND (arg0, 1)) == 0
	  && ((TREE_INT_CST_LOW (arg1)
	       + TREE_INT_CST_LOW (TREE_OPERAND (arg0, 1)))
	      == (unsigned int) GET_MODE_BITSIZE (TYPE_MODE (type))))
	return TREE_OPERAND (arg0, 0);

      return NULL_TREE;

    case MIN_EXPR:
      if (operand_equal_p (arg0, arg1, 0))
	return omit_one_operand (type, arg0, arg1);
      if (INTEGRAL_TYPE_P (type)
	  && operand_equal_p (arg1, TYPE_MIN_VALUE (type), OEP_ONLY_CONST))
	return omit_one_operand (type, arg1, arg0);
      tem = fold_minmax (MIN_EXPR, type, arg0, arg1);
      if (tem)
	return tem;
      goto associate;

    case MAX_EXPR:
      if (operand_equal_p (arg0, arg1, 0))
	return omit_one_operand (type, arg0, arg1);
      if (INTEGRAL_TYPE_P (type)
	  && TYPE_MAX_VALUE (type)
	  && operand_equal_p (arg1, TYPE_MAX_VALUE (type), OEP_ONLY_CONST))
	return omit_one_operand (type, arg1, arg0);
      tem = fold_minmax (MAX_EXPR, type, arg0, arg1);
      if (tem)
	return tem;
      goto associate;

    case TRUTH_ANDIF_EXPR:
      /* Note that the operands of this must be ints
	 and their values must be 0 or 1.
	 ("true" is a fixed value perhaps depending on the language.)  */
      /* If first arg is constant zero, return it.  */
      if (integer_zerop (arg0))
	return fold_convert (type, arg0);
    case TRUTH_AND_EXPR:
      /* If either arg is constant true, drop it.  */
      if (TREE_CODE (arg0) == INTEGER_CST && ! integer_zerop (arg0))
	return non_lvalue (fold_convert (type, arg1));
      if (TREE_CODE (arg1) == INTEGER_CST && ! integer_zerop (arg1)
	  /* Preserve sequence points.  */
	  && (code != TRUTH_ANDIF_EXPR || ! TREE_SIDE_EFFECTS (arg0)))
	return non_lvalue (fold_convert (type, arg0));
      /* If second arg is constant zero, result is zero, but first arg
	 must be evaluated.  */
      if (integer_zerop (arg1))
	return omit_one_operand (type, arg1, arg0);
      /* Likewise for first arg, but note that only the TRUTH_AND_EXPR
	 case will be handled here.  */
      if (integer_zerop (arg0))
	return omit_one_operand (type, arg0, arg1);

      /* !X && X is always false.  */
      if (TREE_CODE (arg0) == TRUTH_NOT_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	return omit_one_operand (type, integer_zero_node, arg1);
      /* X && !X is always false.  */
      if (TREE_CODE (arg1) == TRUTH_NOT_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	return omit_one_operand (type, integer_zero_node, arg0);

      /* A < X && A + 1 > Y ==> A < X && A >= Y.  Normally A + 1 > Y
	 means A >= Y && A != MAX, but in this case we know that
	 A < X <= MAX.  */

      if (!TREE_SIDE_EFFECTS (arg0)
	  && !TREE_SIDE_EFFECTS (arg1))
	{
	  tem = fold_to_nonsharp_ineq_using_bound (arg0, arg1);
	  if (tem && !operand_equal_p (tem, arg0, 0))
	    return fold_build2 (code, type, tem, arg1);

	  tem = fold_to_nonsharp_ineq_using_bound (arg1, arg0);
	  if (tem && !operand_equal_p (tem, arg1, 0))
	    return fold_build2 (code, type, arg0, tem);
	}

    truth_andor:
      /* We only do these simplifications if we are optimizing.  */
      if (!optimize)
	return NULL_TREE;

      /* Check for things like (A || B) && (A || C).  We can convert this
	 to A || (B && C).  Note that either operator can be any of the four
	 truth and/or operations and the transformation will still be
	 valid.   Also note that we only care about order for the
	 ANDIF and ORIF operators.  If B contains side effects, this
	 might change the truth-value of A.  */
      if (TREE_CODE (arg0) == TREE_CODE (arg1)
	  && (TREE_CODE (arg0) == TRUTH_ANDIF_EXPR
	      || TREE_CODE (arg0) == TRUTH_ORIF_EXPR
	      || TREE_CODE (arg0) == TRUTH_AND_EXPR
	      || TREE_CODE (arg0) == TRUTH_OR_EXPR)
	  && ! TREE_SIDE_EFFECTS (TREE_OPERAND (arg0, 1)))
	{
	  tree a00 = TREE_OPERAND (arg0, 0);
	  tree a01 = TREE_OPERAND (arg0, 1);
	  tree a10 = TREE_OPERAND (arg1, 0);
	  tree a11 = TREE_OPERAND (arg1, 1);
	  int commutative = ((TREE_CODE (arg0) == TRUTH_OR_EXPR
			      || TREE_CODE (arg0) == TRUTH_AND_EXPR)
			     && (code == TRUTH_AND_EXPR
				 || code == TRUTH_OR_EXPR));

	  if (operand_equal_p (a00, a10, 0))
	    return fold_build2 (TREE_CODE (arg0), type, a00,
				fold_build2 (code, type, a01, a11));
	  else if (commutative && operand_equal_p (a00, a11, 0))
	    return fold_build2 (TREE_CODE (arg0), type, a00,
				fold_build2 (code, type, a01, a10));
	  else if (commutative && operand_equal_p (a01, a10, 0))
	    return fold_build2 (TREE_CODE (arg0), type, a01,
				fold_build2 (code, type, a00, a11));

	  /* This case if tricky because we must either have commutative
	     operators or else A10 must not have side-effects.  */

	  else if ((commutative || ! TREE_SIDE_EFFECTS (a10))
		   && operand_equal_p (a01, a11, 0))
	    return fold_build2 (TREE_CODE (arg0), type,
				fold_build2 (code, type, a00, a10),
				a01);
	}

      /* See if we can build a range comparison.  */
      if (0 != (tem = fold_range_test (code, type, op0, op1)))
	return tem;

      /* Check for the possibility of merging component references.  If our
	 lhs is another similar operation, try to merge its rhs with our
	 rhs.  Then try to merge our lhs and rhs.  */
      if (TREE_CODE (arg0) == code
	  && 0 != (tem = fold_truthop (code, type,
				       TREE_OPERAND (arg0, 1), arg1)))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0), tem);

      if ((tem = fold_truthop (code, type, arg0, arg1)) != 0)
	return tem;

      return NULL_TREE;

    case TRUTH_ORIF_EXPR:
      /* Note that the operands of this must be ints
	 and their values must be 0 or true.
	 ("true" is a fixed value perhaps depending on the language.)  */
      /* If first arg is constant true, return it.  */
      if (TREE_CODE (arg0) == INTEGER_CST && ! integer_zerop (arg0))
	return fold_convert (type, arg0);
    case TRUTH_OR_EXPR:
      /* If either arg is constant zero, drop it.  */
      if (TREE_CODE (arg0) == INTEGER_CST && integer_zerop (arg0))
	return non_lvalue (fold_convert (type, arg1));
      if (TREE_CODE (arg1) == INTEGER_CST && integer_zerop (arg1)
	  /* Preserve sequence points.  */
	  && (code != TRUTH_ORIF_EXPR || ! TREE_SIDE_EFFECTS (arg0)))
	return non_lvalue (fold_convert (type, arg0));
      /* If second arg is constant true, result is true, but we must
	 evaluate first arg.  */
      if (TREE_CODE (arg1) == INTEGER_CST && ! integer_zerop (arg1))
	return omit_one_operand (type, arg1, arg0);
      /* Likewise for first arg, but note this only occurs here for
	 TRUTH_OR_EXPR.  */
      if (TREE_CODE (arg0) == INTEGER_CST && ! integer_zerop (arg0))
	return omit_one_operand (type, arg0, arg1);

      /* !X || X is always true.  */
      if (TREE_CODE (arg0) == TRUTH_NOT_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	return omit_one_operand (type, integer_one_node, arg1);
      /* X || !X is always true.  */
      if (TREE_CODE (arg1) == TRUTH_NOT_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	return omit_one_operand (type, integer_one_node, arg0);

      goto truth_andor;

    case TRUTH_XOR_EXPR:
      /* If the second arg is constant zero, drop it.  */
      if (integer_zerop (arg1))
	return non_lvalue (fold_convert (type, arg0));
      /* If the second arg is constant true, this is a logical inversion.  */
      if (integer_onep (arg1))
	{
	  /* Only call invert_truthvalue if operand is a truth value.  */
	  if (TREE_CODE (TREE_TYPE (arg0)) != BOOLEAN_TYPE)
	    tem = fold_build1 (TRUTH_NOT_EXPR, TREE_TYPE (arg0), arg0);
	  else
	    tem = invert_truthvalue (arg0);
	  return non_lvalue (fold_convert (type, tem));
	}
      /* Identical arguments cancel to zero.  */
      if (operand_equal_p (arg0, arg1, 0))
	return omit_one_operand (type, integer_zero_node, arg0);

      /* !X ^ X is always true.  */
      if (TREE_CODE (arg0) == TRUTH_NOT_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0))
	return omit_one_operand (type, integer_one_node, arg1);

      /* X ^ !X is always true.  */
      if (TREE_CODE (arg1) == TRUTH_NOT_EXPR
	  && operand_equal_p (arg0, TREE_OPERAND (arg1, 0), 0))
	return omit_one_operand (type, integer_one_node, arg0);

      return NULL_TREE;

    case EQ_EXPR:
    case NE_EXPR:
      tem = fold_comparison (code, type, op0, op1);
      if (tem != NULL_TREE)
	return tem;

      /* bool_var != 0 becomes bool_var. */
      if (TREE_CODE (TREE_TYPE (arg0)) == BOOLEAN_TYPE && integer_zerop (arg1)
          && code == NE_EXPR)
        return non_lvalue (fold_convert (type, arg0));

      /* bool_var == 1 becomes bool_var. */
      if (TREE_CODE (TREE_TYPE (arg0)) == BOOLEAN_TYPE && integer_onep (arg1)
          && code == EQ_EXPR)
        return non_lvalue (fold_convert (type, arg0));

      /* bool_var != 1 becomes !bool_var. */
      if (TREE_CODE (TREE_TYPE (arg0)) == BOOLEAN_TYPE && integer_onep (arg1)
          && code == NE_EXPR)
        return fold_build1 (TRUTH_NOT_EXPR, type, arg0);

      /* bool_var == 0 becomes !bool_var. */
      if (TREE_CODE (TREE_TYPE (arg0)) == BOOLEAN_TYPE && integer_zerop (arg1)
          && code == EQ_EXPR)
        return fold_build1 (TRUTH_NOT_EXPR, type, arg0);

      /*  ~a != C becomes a != ~C where C is a constant.  Likewise for ==.  */
      if (TREE_CODE (arg0) == BIT_NOT_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST)
	{
	  tree cmp_type = TREE_TYPE (TREE_OPERAND (arg0, 0));
	  return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			      fold_build1 (BIT_NOT_EXPR, cmp_type, 
					   fold_convert (cmp_type, arg1)));
	}

      /* If this is an equality comparison of the address of a non-weak
	 object against zero, then we know the result.  */
      if (TREE_CODE (arg0) == ADDR_EXPR
	  && VAR_OR_FUNCTION_DECL_P (TREE_OPERAND (arg0, 0))
	  && ! DECL_WEAK (TREE_OPERAND (arg0, 0))
	  && integer_zerop (arg1))
	return constant_boolean_node (code != EQ_EXPR, type);

      /* If this is an equality comparison of the address of two non-weak,
	 unaliased symbols neither of which are extern (since we do not
	 have access to attributes for externs), then we know the result.  */
      if (TREE_CODE (arg0) == ADDR_EXPR
	  && VAR_OR_FUNCTION_DECL_P (TREE_OPERAND (arg0, 0))
	  && ! DECL_WEAK (TREE_OPERAND (arg0, 0))
	  && ! lookup_attribute ("alias",
				 DECL_ATTRIBUTES (TREE_OPERAND (arg0, 0)))
	  && ! DECL_EXTERNAL (TREE_OPERAND (arg0, 0))
	  && TREE_CODE (arg1) == ADDR_EXPR
	  && VAR_OR_FUNCTION_DECL_P (TREE_OPERAND (arg1, 0))
	  && ! DECL_WEAK (TREE_OPERAND (arg1, 0))
	  && ! lookup_attribute ("alias",
				 DECL_ATTRIBUTES (TREE_OPERAND (arg1, 0)))
	  && ! DECL_EXTERNAL (TREE_OPERAND (arg1, 0)))
	{
	  /* We know that we're looking at the address of two
	     non-weak, unaliased, static _DECL nodes.

	     It is both wasteful and incorrect to call operand_equal_p
	     to compare the two ADDR_EXPR nodes.  It is wasteful in that
	     all we need to do is test pointer equality for the arguments
	     to the two ADDR_EXPR nodes.  It is incorrect to use
	     operand_equal_p as that function is NOT equivalent to a
	     C equality test.  It can in fact return false for two
	     objects which would test as equal using the C equality
	     operator.  */
	  bool equal = TREE_OPERAND (arg0, 0) == TREE_OPERAND (arg1, 0);
	  return constant_boolean_node (equal
				        ? code == EQ_EXPR : code != EQ_EXPR,
				        type);
	}

      /* If this is an EQ or NE comparison of a constant with a PLUS_EXPR or
	 a MINUS_EXPR of a constant, we can convert it into a comparison with
	 a revised constant as long as no overflow occurs.  */
      if (TREE_CODE (arg1) == INTEGER_CST
	  && (TREE_CODE (arg0) == PLUS_EXPR
	      || TREE_CODE (arg0) == MINUS_EXPR)
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
	  && 0 != (tem = const_binop (TREE_CODE (arg0) == PLUS_EXPR
				      ? MINUS_EXPR : PLUS_EXPR,
				      fold_convert (TREE_TYPE (arg0), arg1),
				      TREE_OPERAND (arg0, 1), 0))
	  && ! TREE_CONSTANT_OVERFLOW (tem))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0), tem);

      /* Similarly for a NEGATE_EXPR.  */
      if (TREE_CODE (arg0) == NEGATE_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && 0 != (tem = negate_expr (arg1))
	  && TREE_CODE (tem) == INTEGER_CST
	  && ! TREE_CONSTANT_OVERFLOW (tem))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0), tem);

      /* If we have X - Y == 0, we can convert that to X == Y and similarly
	 for !=.  Don't do this for ordered comparisons due to overflow.  */
      if (TREE_CODE (arg0) == MINUS_EXPR
	  && integer_zerop (arg1))
	return fold_build2 (code, type,
			    TREE_OPERAND (arg0, 0), TREE_OPERAND (arg0, 1));

      /* Convert ABS_EXPR<x> == 0 or ABS_EXPR<x> != 0 to x == 0 or x != 0.  */
      if (TREE_CODE (arg0) == ABS_EXPR
	  && (integer_zerop (arg1) || real_zerop (arg1)))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0), arg1);

      /* If this is an EQ or NE comparison with zero and ARG0 is
	 (1 << foo) & bar, convert it to (bar >> foo) & 1.  Both require
	 two operations, but the latter can be done in one less insn
	 on machines that have only two-operand insns or on which a
	 constant cannot be the first operand.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && integer_zerop (arg1))
	{
	  tree arg00 = TREE_OPERAND (arg0, 0);
	  tree arg01 = TREE_OPERAND (arg0, 1);
	  if (TREE_CODE (arg00) == LSHIFT_EXPR
	      && integer_onep (TREE_OPERAND (arg00, 0)))
	    {
	      tree tem = fold_build2 (RSHIFT_EXPR, TREE_TYPE (arg00),
				      arg01, TREE_OPERAND (arg00, 1));
	      tem = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg0), tem,
				 build_int_cst (TREE_TYPE (arg0), 1));
	      return fold_build2 (code, type,
				  fold_convert (TREE_TYPE (arg1), tem), arg1);
	    }
	  else if (TREE_CODE (arg01) == LSHIFT_EXPR
		   && integer_onep (TREE_OPERAND (arg01, 0)))
	    {
	      tree tem = fold_build2 (RSHIFT_EXPR, TREE_TYPE (arg01),
				      arg00, TREE_OPERAND (arg01, 1));
	      tem = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg0), tem,
				 build_int_cst (TREE_TYPE (arg0), 1));
	      return fold_build2 (code, type,
				  fold_convert (TREE_TYPE (arg1), tem), arg1);
	    }
	}

      /* If this is an NE or EQ comparison of zero against the result of a
	 signed MOD operation whose second operand is a power of 2, make
	 the MOD operation unsigned since it is simpler and equivalent.  */
      if (integer_zerop (arg1)
	  && !TYPE_UNSIGNED (TREE_TYPE (arg0))
	  && (TREE_CODE (arg0) == TRUNC_MOD_EXPR
	      || TREE_CODE (arg0) == CEIL_MOD_EXPR
	      || TREE_CODE (arg0) == FLOOR_MOD_EXPR
	      || TREE_CODE (arg0) == ROUND_MOD_EXPR)
	  && integer_pow2p (TREE_OPERAND (arg0, 1)))
	{
	  tree newtype = lang_hooks.types.unsigned_type (TREE_TYPE (arg0));
	  tree newmod = fold_build2 (TREE_CODE (arg0), newtype,
				     fold_convert (newtype,
						   TREE_OPERAND (arg0, 0)),
				     fold_convert (newtype,
						   TREE_OPERAND (arg0, 1)));

	  return fold_build2 (code, type, newmod,
			      fold_convert (newtype, arg1));
	}

      /* Fold ((X >> C1) & C2) == 0 and ((X >> C1) & C2) != 0 where
	 C1 is a valid shift constant, and C2 is a power of two, i.e.
	 a single bit.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == RSHIFT_EXPR
	  && TREE_CODE (TREE_OPERAND (TREE_OPERAND (arg0, 0), 1))
	     == INTEGER_CST
	  && integer_pow2p (TREE_OPERAND (arg0, 1))
	  && integer_zerop (arg1))
	{
	  tree itype = TREE_TYPE (arg0);
	  unsigned HOST_WIDE_INT prec = TYPE_PRECISION (itype);
	  tree arg001 = TREE_OPERAND (TREE_OPERAND (arg0, 0), 1);

	  /* Check for a valid shift count.  */
	  if (TREE_INT_CST_HIGH (arg001) == 0
	      && TREE_INT_CST_LOW (arg001) < prec)
	    {
	      tree arg01 = TREE_OPERAND (arg0, 1);
	      tree arg000 = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
	      unsigned HOST_WIDE_INT log2 = tree_log2 (arg01);
	      /* If (C2 << C1) doesn't overflow, then ((X >> C1) & C2) != 0
		 can be rewritten as (X & (C2 << C1)) != 0.  */
	      if ((log2 + TREE_INT_CST_LOW (arg001)) < prec)
		{
		  tem = fold_build2 (LSHIFT_EXPR, itype, arg01, arg001);
		  tem = fold_build2 (BIT_AND_EXPR, itype, arg000, tem);
		  return fold_build2 (code, type, tem, arg1);
		}
	      /* Otherwise, for signed (arithmetic) shifts,
		 ((X >> C1) & C2) != 0 is rewritten as X < 0, and
		 ((X >> C1) & C2) == 0 is rewritten as X >= 0.  */
	      else if (!TYPE_UNSIGNED (itype))
		return fold_build2 (code == EQ_EXPR ? GE_EXPR : LT_EXPR, type,
				    arg000, build_int_cst (itype, 0));
	      /* Otherwise, of unsigned (logical) shifts,
		 ((X >> C1) & C2) != 0 is rewritten as (X,false), and
		 ((X >> C1) & C2) == 0 is rewritten as (X,true).  */
	      else
		return omit_one_operand (type,
					 code == EQ_EXPR ? integer_one_node
							 : integer_zero_node,
					 arg000);
	    }
	}

      /* If this is an NE comparison of zero with an AND of one, remove the
	 comparison since the AND will give the correct value.  */
      if (code == NE_EXPR
	  && integer_zerop (arg1)
	  && TREE_CODE (arg0) == BIT_AND_EXPR
	  && integer_onep (TREE_OPERAND (arg0, 1)))
	return fold_convert (type, arg0);

      /* If we have (A & C) == C where C is a power of 2, convert this into
	 (A & C) != 0.  Similarly for NE_EXPR.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && integer_pow2p (TREE_OPERAND (arg0, 1))
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	return fold_build2 (code == EQ_EXPR ? NE_EXPR : EQ_EXPR, type,
			    arg0, fold_convert (TREE_TYPE (arg0),
						integer_zero_node));

      /* If we have (A & C) != 0 or (A & C) == 0 and C is the sign
	 bit, then fold the expression into A < 0 or A >= 0.  */
      tem = fold_single_bit_test_into_sign_test (code, arg0, arg1, type);
      if (tem)
	return tem;

      /* If we have (A & C) == D where D & ~C != 0, convert this into 0.
	 Similarly for NE_EXPR.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	{
	  tree notc = fold_build1 (BIT_NOT_EXPR,
				   TREE_TYPE (TREE_OPERAND (arg0, 1)),
				   TREE_OPERAND (arg0, 1));
	  tree dandnotc = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg0),
				       arg1, notc);
	  tree rslt = code == EQ_EXPR ? integer_zero_node : integer_one_node;
	  if (integer_nonzerop (dandnotc))
	    return omit_one_operand (type, rslt, arg0);
	}

      /* If we have (A | C) == D where C & ~D != 0, convert this into 0.
	 Similarly for NE_EXPR.  */
      if (TREE_CODE (arg0) == BIT_IOR_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	{
	  tree notd = fold_build1 (BIT_NOT_EXPR, TREE_TYPE (arg1), arg1);
	  tree candnotd = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg0),
				       TREE_OPERAND (arg0, 1), notd);
	  tree rslt = code == EQ_EXPR ? integer_zero_node : integer_one_node;
	  if (integer_nonzerop (candnotd))
	    return omit_one_operand (type, rslt, arg0);
	}

      /* If this is a comparison of a field, we may be able to simplify it.  */
      if (((TREE_CODE (arg0) == COMPONENT_REF
	    && lang_hooks.can_use_bit_fields_p ())
	   || TREE_CODE (arg0) == BIT_FIELD_REF)
	  /* Handle the constant case even without -O
	     to make sure the warnings are given.  */
	  && (optimize || TREE_CODE (arg1) == INTEGER_CST))
	{
	  t1 = optimize_bit_field_compare (code, type, arg0, arg1);
	  if (t1)
	    return t1;
	}

      /* Optimize comparisons of strlen vs zero to a compare of the
	 first character of the string vs zero.  To wit,
		strlen(ptr) == 0   =>  *ptr == 0
		strlen(ptr) != 0   =>  *ptr != 0
	 Other cases should reduce to one of these two (or a constant)
	 due to the return value of strlen being unsigned.  */
      if (TREE_CODE (arg0) == CALL_EXPR
	  && integer_zerop (arg1))
	{
	  tree fndecl = get_callee_fndecl (arg0);
	  tree arglist;

	  if (fndecl
	      && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL
	      && DECL_FUNCTION_CODE (fndecl) == BUILT_IN_STRLEN
	      && (arglist = TREE_OPERAND (arg0, 1))
	      && TREE_CODE (TREE_TYPE (TREE_VALUE (arglist))) == POINTER_TYPE
	      && ! TREE_CHAIN (arglist))
	    {
	      tree iref = build_fold_indirect_ref (TREE_VALUE (arglist));
	      return fold_build2 (code, type, iref,
				  build_int_cst (TREE_TYPE (iref), 0));
	    }
	}

      /* Fold (X >> C) != 0 into X < 0 if C is one less than the width
	 of X.  Similarly fold (X >> C) == 0 into X >= 0.  */
      if (TREE_CODE (arg0) == RSHIFT_EXPR
	  && integer_zerop (arg1)
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	{
	  tree arg00 = TREE_OPERAND (arg0, 0);
	  tree arg01 = TREE_OPERAND (arg0, 1);
	  tree itype = TREE_TYPE (arg00);
	  if (TREE_INT_CST_HIGH (arg01) == 0
	      && TREE_INT_CST_LOW (arg01)
		 == (unsigned HOST_WIDE_INT) (TYPE_PRECISION (itype) - 1))
	    {
	      if (TYPE_UNSIGNED (itype))
		{
		  itype = lang_hooks.types.signed_type (itype);
		  arg00 = fold_convert (itype, arg00);
		}
	      return fold_build2 (code == EQ_EXPR ? GE_EXPR : LT_EXPR,
				  type, arg00, build_int_cst (itype, 0));
	    }
	}

      /* (X ^ Y) == 0 becomes X == Y, and (X ^ Y) != 0 becomes X != Y.  */
      if (integer_zerop (arg1)
	  && TREE_CODE (arg0) == BIT_XOR_EXPR)
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			    TREE_OPERAND (arg0, 1));

      /* (X ^ Y) == Y becomes X == 0.  We know that Y has no side-effects.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 1), arg1, 0))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			    build_int_cst (TREE_TYPE (arg1), 0));
      /* Likewise (X ^ Y) == X becomes Y == 0.  X has no side-effects.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && reorder_operands_p (TREE_OPERAND (arg0, 1), arg1))
	return fold_build2 (code, type, TREE_OPERAND (arg0, 1),
			    build_int_cst (TREE_TYPE (arg1), 0));

      /* (X ^ C1) op C2 can be rewritten as X op (C1 ^ C2).  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST)
	return fold_build2 (code, type, TREE_OPERAND (arg0, 0),
			    fold_build2 (BIT_XOR_EXPR, TREE_TYPE (arg1),
					 TREE_OPERAND (arg0, 1), arg1));

      /* Fold (~X & C) == 0 into (X & C) != 0 and (~X & C) != 0 into
	 (X & C) == 0 when C is a single bit.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == BIT_NOT_EXPR
	  && integer_zerop (arg1)
	  && integer_pow2p (TREE_OPERAND (arg0, 1)))
	{
	  tem = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg0),
			     TREE_OPERAND (TREE_OPERAND (arg0, 0), 0),
			     TREE_OPERAND (arg0, 1));
	  return fold_build2 (code == EQ_EXPR ? NE_EXPR : EQ_EXPR,
			      type, tem, arg1);
	}

      /* Fold ((X & C) ^ C) eq/ne 0 into (X & C) ne/eq 0, when the
	 constant C is a power of two, i.e. a single bit.  */
      if (TREE_CODE (arg0) == BIT_XOR_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == BIT_AND_EXPR
	  && integer_zerop (arg1)
	  && integer_pow2p (TREE_OPERAND (arg0, 1))
	  && operand_equal_p (TREE_OPERAND (TREE_OPERAND (arg0, 0), 1),
			      TREE_OPERAND (arg0, 1), OEP_ONLY_CONST))
	{
	  tree arg00 = TREE_OPERAND (arg0, 0);
	  return fold_build2 (code == EQ_EXPR ? NE_EXPR : EQ_EXPR, type,
			      arg00, build_int_cst (TREE_TYPE (arg00), 0));
	}

      /* Likewise, fold ((X ^ C) & C) eq/ne 0 into (X & C) ne/eq 0,
	 when is C is a power of two, i.e. a single bit.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == BIT_XOR_EXPR
	  && integer_zerop (arg1)
	  && integer_pow2p (TREE_OPERAND (arg0, 1))
	  && operand_equal_p (TREE_OPERAND (TREE_OPERAND (arg0, 0), 1),
			      TREE_OPERAND (arg0, 1), OEP_ONLY_CONST))
	{
	  tree arg000 = TREE_OPERAND (TREE_OPERAND (arg0, 0), 0);
	  tem = fold_build2 (BIT_AND_EXPR, TREE_TYPE (arg000),
			     arg000, TREE_OPERAND (arg0, 1));
	  return fold_build2 (code == EQ_EXPR ? NE_EXPR : EQ_EXPR, type,
			      tem, build_int_cst (TREE_TYPE (tem), 0));
	}

      if (integer_zerop (arg1)
	  && tree_expr_nonzero_p (arg0))
        {
	  tree res = constant_boolean_node (code==NE_EXPR, type);
	  return omit_one_operand (type, res, arg0);
	}
      return NULL_TREE;

    case LT_EXPR:
    case GT_EXPR:
    case LE_EXPR:
    case GE_EXPR:
      tem = fold_comparison (code, type, op0, op1);
      if (tem != NULL_TREE)
	return tem;

      /* Transform comparisons of the form X +- C CMP X.  */
      if ((TREE_CODE (arg0) == PLUS_EXPR || TREE_CODE (arg0) == MINUS_EXPR)
	  && operand_equal_p (TREE_OPERAND (arg0, 0), arg1, 0)
	  && ((TREE_CODE (TREE_OPERAND (arg0, 1)) == REAL_CST
	       && !HONOR_SNANS (TYPE_MODE (TREE_TYPE (arg0))))
	      || (TREE_CODE (TREE_OPERAND (arg0, 1)) == INTEGER_CST
		  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))))
	{
	  tree arg01 = TREE_OPERAND (arg0, 1);
	  enum tree_code code0 = TREE_CODE (arg0);
	  int is_positive;

	  if (TREE_CODE (arg01) == REAL_CST)
	    is_positive = REAL_VALUE_NEGATIVE (TREE_REAL_CST (arg01)) ? -1 : 1;
	  else
	    is_positive = tree_int_cst_sgn (arg01);

	  /* (X - c) > X becomes false.  */
	  if (code == GT_EXPR
	      && ((code0 == MINUS_EXPR && is_positive >= 0)
		  || (code0 == PLUS_EXPR && is_positive <= 0)))
	    {
	      if (TREE_CODE (arg01) == INTEGER_CST
		  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when assuming that (X - c) > X "
					"is always false"),
				       WARN_STRICT_OVERFLOW_ALL);
	      return constant_boolean_node (0, type);
	    }

	  /* Likewise (X + c) < X becomes false.  */
	  if (code == LT_EXPR
	      && ((code0 == PLUS_EXPR && is_positive >= 0)
		  || (code0 == MINUS_EXPR && is_positive <= 0)))
	    {
	      if (TREE_CODE (arg01) == INTEGER_CST
		  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when assuming that "
					"(X + c) < X is always false"),
				       WARN_STRICT_OVERFLOW_ALL);
	      return constant_boolean_node (0, type);
	    }

	  /* Convert (X - c) <= X to true.  */
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1)))
	      && code == LE_EXPR
	      && ((code0 == MINUS_EXPR && is_positive >= 0)
		  || (code0 == PLUS_EXPR && is_positive <= 0)))
	    {
	      if (TREE_CODE (arg01) == INTEGER_CST
		  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when assuming that "
					"(X - c) <= X is always true"),
				       WARN_STRICT_OVERFLOW_ALL);
	      return constant_boolean_node (1, type);
	    }

	  /* Convert (X + c) >= X to true.  */
	  if (!HONOR_NANS (TYPE_MODE (TREE_TYPE (arg1)))
	      && code == GE_EXPR
	      && ((code0 == PLUS_EXPR && is_positive >= 0)
		  || (code0 == MINUS_EXPR && is_positive <= 0)))
	    {
	      if (TREE_CODE (arg01) == INTEGER_CST
		  && TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		fold_overflow_warning (("assuming signed overflow does not "
					"occur when assuming that "
					"(X + c) >= X is always true"),
				       WARN_STRICT_OVERFLOW_ALL);
	      return constant_boolean_node (1, type);
	    }

	  if (TREE_CODE (arg01) == INTEGER_CST)
	    {
	      /* Convert X + c > X and X - c < X to true for integers.  */
	      if (code == GT_EXPR
	          && ((code0 == PLUS_EXPR && is_positive > 0)
		      || (code0 == MINUS_EXPR && is_positive < 0)))
		{
		  if (TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		    fold_overflow_warning (("assuming signed overflow does "
					    "not occur when assuming that "
					    "(X + c) > X is always true"),
					   WARN_STRICT_OVERFLOW_ALL);
		  return constant_boolean_node (1, type);
		}

	      if (code == LT_EXPR
	          && ((code0 == MINUS_EXPR && is_positive > 0)
		      || (code0 == PLUS_EXPR && is_positive < 0)))
		{
		  if (TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		    fold_overflow_warning (("assuming signed overflow does "
					    "not occur when assuming that "
					    "(X - c) < X is always true"),
					   WARN_STRICT_OVERFLOW_ALL);
		  return constant_boolean_node (1, type);
		}

	      /* Convert X + c <= X and X - c >= X to false for integers.  */
	      if (code == LE_EXPR
	          && ((code0 == PLUS_EXPR && is_positive > 0)
		      || (code0 == MINUS_EXPR && is_positive < 0)))
		{
		  if (TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		    fold_overflow_warning (("assuming signed overflow does "
					    "not occur when assuming that "
					    "(X + c) <= X is always false"),
					   WARN_STRICT_OVERFLOW_ALL);
		  return constant_boolean_node (0, type);
		}

	      if (code == GE_EXPR
	          && ((code0 == MINUS_EXPR && is_positive > 0)
		      || (code0 == PLUS_EXPR && is_positive < 0)))
		{
		  if (TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (arg1)))
		    fold_overflow_warning (("assuming signed overflow does "
					    "not occur when assuming that "
					    "(X - c) >= X is always true"),
					   WARN_STRICT_OVERFLOW_ALL);
		  return constant_boolean_node (0, type);
		}
	    }
	}

      /* Change X >= C to X > (C - 1) and X < C to X <= (C - 1) if C > 0.
	 This transformation affects the cases which are handled in later
	 optimizations involving comparisons with non-negative constants.  */
      if (TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (arg0) != INTEGER_CST
	  && tree_int_cst_sgn (arg1) > 0)
	{
	  if (code == GE_EXPR)
	    {
	      arg1 = const_binop (MINUS_EXPR, arg1,
			          build_int_cst (TREE_TYPE (arg1), 1), 0);
	      return fold_build2 (GT_EXPR, type, arg0,
				  fold_convert (TREE_TYPE (arg0), arg1));
	    }
	  if (code == LT_EXPR)
	    {
	      arg1 = const_binop (MINUS_EXPR, arg1,
			          build_int_cst (TREE_TYPE (arg1), 1), 0);
	      return fold_build2 (LE_EXPR, type, arg0,
				  fold_convert (TREE_TYPE (arg0), arg1));
	    }
	}

      /* Comparisons with the highest or lowest possible integer of
	 the specified size will have known values.  */
      {
	int width = GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (arg1)));

	if (TREE_CODE (arg1) == INTEGER_CST
	    && ! TREE_CONSTANT_OVERFLOW (arg1)
	    && width <= 2 * HOST_BITS_PER_WIDE_INT
	    && (INTEGRAL_TYPE_P (TREE_TYPE (arg1))
		|| POINTER_TYPE_P (TREE_TYPE (arg1))))
	  {
	    HOST_WIDE_INT signed_max_hi;
	    unsigned HOST_WIDE_INT signed_max_lo;
	    unsigned HOST_WIDE_INT max_hi, max_lo, min_hi, min_lo;

	    if (width <= HOST_BITS_PER_WIDE_INT)
	      {
		signed_max_lo = ((unsigned HOST_WIDE_INT) 1 << (width - 1))
				- 1;
		signed_max_hi = 0;
		max_hi = 0;

		if (TYPE_UNSIGNED (TREE_TYPE (arg1)))
		  {
		    max_lo = ((unsigned HOST_WIDE_INT) 2 << (width - 1)) - 1;
		    min_lo = 0;
		    min_hi = 0;
		  }
		else
		  {
		    max_lo = signed_max_lo;
		    min_lo = ((unsigned HOST_WIDE_INT) -1 << (width - 1));
		    min_hi = -1;
		  }
	      }
	    else
	      {
		width -= HOST_BITS_PER_WIDE_INT;
		signed_max_lo = -1;
		signed_max_hi = ((unsigned HOST_WIDE_INT) 1 << (width - 1))
				- 1;
		max_lo = -1;
		min_lo = 0;

		if (TYPE_UNSIGNED (TREE_TYPE (arg1)))
		  {
		    max_hi = ((unsigned HOST_WIDE_INT) 2 << (width - 1)) - 1;
		    min_hi = 0;
		  }
		else
		  {
		    max_hi = signed_max_hi;
		    min_hi = ((unsigned HOST_WIDE_INT) -1 << (width - 1));
		  }
	      }

	    if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (arg1) == max_hi
		&& TREE_INT_CST_LOW (arg1) == max_lo)
	      switch (code)
		{
		case GT_EXPR:
		  return omit_one_operand (type, integer_zero_node, arg0);

		case GE_EXPR:
		  return fold_build2 (EQ_EXPR, type, op0, op1);

		case LE_EXPR:
		  return omit_one_operand (type, integer_one_node, arg0);

		case LT_EXPR:
		  return fold_build2 (NE_EXPR, type, op0, op1);

		/* The GE_EXPR and LT_EXPR cases above are not normally
		   reached because of previous transformations.  */

		default:
		  break;
		}
	    else if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (arg1)
		     == max_hi
		     && TREE_INT_CST_LOW (arg1) == max_lo - 1)
	      switch (code)
		{
		case GT_EXPR:
		  arg1 = const_binop (PLUS_EXPR, arg1, integer_one_node, 0);
		  return fold_build2 (EQ_EXPR, type,
				      fold_convert (TREE_TYPE (arg1), arg0),
				      arg1);
		case LE_EXPR:
		  arg1 = const_binop (PLUS_EXPR, arg1, integer_one_node, 0);
		  return fold_build2 (NE_EXPR, type,
				      fold_convert (TREE_TYPE (arg1), arg0),
				      arg1);
		default:
		  break;
		}
	    else if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (arg1)
		     == min_hi
		     && TREE_INT_CST_LOW (arg1) == min_lo)
	      switch (code)
		{
		case LT_EXPR:
		  return omit_one_operand (type, integer_zero_node, arg0);

		case LE_EXPR:
		  return fold_build2 (EQ_EXPR, type, op0, op1);

		case GE_EXPR:
		  return omit_one_operand (type, integer_one_node, arg0);

		case GT_EXPR:
		  return fold_build2 (NE_EXPR, type, op0, op1);

		default:
		  break;
		}
	    else if ((unsigned HOST_WIDE_INT) TREE_INT_CST_HIGH (arg1)
		     == min_hi
		     && TREE_INT_CST_LOW (arg1) == min_lo + 1)
	      switch (code)
		{
		case GE_EXPR:
		  arg1 = const_binop (MINUS_EXPR, arg1, integer_one_node, 0);
		  return fold_build2 (NE_EXPR, type,
				      fold_convert (TREE_TYPE (arg1), arg0),
				      arg1);
		case LT_EXPR:
		  arg1 = const_binop (MINUS_EXPR, arg1, integer_one_node, 0);
		  return fold_build2 (EQ_EXPR, type,
				      fold_convert (TREE_TYPE (arg1), arg0),
				      arg1);
		default:
		  break;
		}

	    else if (!in_gimple_form
		     && TREE_INT_CST_HIGH (arg1) == signed_max_hi
		     && TREE_INT_CST_LOW (arg1) == signed_max_lo
		     && TYPE_UNSIGNED (TREE_TYPE (arg1))
		     /* signed_type does not work on pointer types.  */
		     && INTEGRAL_TYPE_P (TREE_TYPE (arg1)))
	      {
		/* The following case also applies to X < signed_max+1
		   and X >= signed_max+1 because previous transformations.  */
		if (code == LE_EXPR || code == GT_EXPR)
		  {
		    tree st;
		    st = lang_hooks.types.signed_type (TREE_TYPE (arg1));
		    return fold_build2 (code == LE_EXPR ? GE_EXPR : LT_EXPR,
					type, fold_convert (st, arg0),
					build_int_cst (st, 0));
		  }
	      }
	  }
      }

      /* If we are comparing an ABS_EXPR with a constant, we can
	 convert all the cases into explicit comparisons, but they may
	 well not be faster than doing the ABS and one comparison.
	 But ABS (X) <= C is a range comparison, which becomes a subtraction
	 and a comparison, and is probably faster.  */
      if (code == LE_EXPR
	  && TREE_CODE (arg1) == INTEGER_CST
	  && TREE_CODE (arg0) == ABS_EXPR
	  && ! TREE_SIDE_EFFECTS (arg0)
	  && (0 != (tem = negate_expr (arg1)))
	  && TREE_CODE (tem) == INTEGER_CST
	  && ! TREE_CONSTANT_OVERFLOW (tem))
	return fold_build2 (TRUTH_ANDIF_EXPR, type,
			    build2 (GE_EXPR, type,
				    TREE_OPERAND (arg0, 0), tem),
			    build2 (LE_EXPR, type,
				    TREE_OPERAND (arg0, 0), arg1));

      /* Convert ABS_EXPR<x> >= 0 to true.  */
      strict_overflow_p = false;
      if (code == GE_EXPR
	  && (integer_zerop (arg1)
	      || (! HONOR_NANS (TYPE_MODE (TREE_TYPE (arg0)))
		  && real_zerop (arg1)))
	  && tree_expr_nonnegative_warnv_p (arg0, &strict_overflow_p))
	{
	  if (strict_overflow_p)
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when simplifying comparison of "
				    "absolute value and zero"),
				   WARN_STRICT_OVERFLOW_CONDITIONAL);
	  return omit_one_operand (type, integer_one_node, arg0);
	}

      /* Convert ABS_EXPR<x> < 0 to false.  */
      strict_overflow_p = false;
      if (code == LT_EXPR
	  && (integer_zerop (arg1) || real_zerop (arg1))
	  && tree_expr_nonnegative_warnv_p (arg0, &strict_overflow_p))
	{
	  if (strict_overflow_p)
	    fold_overflow_warning (("assuming signed overflow does not occur "
				    "when simplifying comparison of "
				    "absolute value and zero"),
				   WARN_STRICT_OVERFLOW_CONDITIONAL);
	  return omit_one_operand (type, integer_zero_node, arg0);
	}

      /* If X is unsigned, convert X < (1 << Y) into X >> Y == 0
	 and similarly for >= into !=.  */
      if ((code == LT_EXPR || code == GE_EXPR)
	  && TYPE_UNSIGNED (TREE_TYPE (arg0))
	  && TREE_CODE (arg1) == LSHIFT_EXPR
	  && integer_onep (TREE_OPERAND (arg1, 0)))
	return build2 (code == LT_EXPR ? EQ_EXPR : NE_EXPR, type,
		       build2 (RSHIFT_EXPR, TREE_TYPE (arg0), arg0,
			       TREE_OPERAND (arg1, 1)),
		       build_int_cst (TREE_TYPE (arg0), 0));

      if ((code == LT_EXPR || code == GE_EXPR)
	  && TYPE_UNSIGNED (TREE_TYPE (arg0))
	  && (TREE_CODE (arg1) == NOP_EXPR
	      || TREE_CODE (arg1) == CONVERT_EXPR)
	  && TREE_CODE (TREE_OPERAND (arg1, 0)) == LSHIFT_EXPR
	  && integer_onep (TREE_OPERAND (TREE_OPERAND (arg1, 0), 0)))
	return
	  build2 (code == LT_EXPR ? EQ_EXPR : NE_EXPR, type,
		  fold_convert (TREE_TYPE (arg0),
				build2 (RSHIFT_EXPR, TREE_TYPE (arg0), arg0,
					TREE_OPERAND (TREE_OPERAND (arg1, 0),
						      1))),
		  build_int_cst (TREE_TYPE (arg0), 0));

      return NULL_TREE;

    case UNORDERED_EXPR:
    case ORDERED_EXPR:
    case UNLT_EXPR:
    case UNLE_EXPR:
    case UNGT_EXPR:
    case UNGE_EXPR:
    case UNEQ_EXPR:
    case LTGT_EXPR:
      if (TREE_CODE (arg0) == REAL_CST && TREE_CODE (arg1) == REAL_CST)
	{
	  t1 = fold_relational_const (code, type, arg0, arg1);
	  if (t1 != NULL_TREE)
	    return t1;
	}

      /* If the first operand is NaN, the result is constant.  */
      if (TREE_CODE (arg0) == REAL_CST
	  && REAL_VALUE_ISNAN (TREE_REAL_CST (arg0))
	  && (code != LTGT_EXPR || ! flag_trapping_math))
	{
	  t1 = (code == ORDERED_EXPR || code == LTGT_EXPR)
	       ? integer_zero_node
	       : integer_one_node;
	  return omit_one_operand (type, t1, arg1);
	}

      /* If the second operand is NaN, the result is constant.  */
      if (TREE_CODE (arg1) == REAL_CST
	  && REAL_VALUE_ISNAN (TREE_REAL_CST (arg1))
	  && (code != LTGT_EXPR || ! flag_trapping_math))
	{
	  t1 = (code == ORDERED_EXPR || code == LTGT_EXPR)
	       ? integer_zero_node
	       : integer_one_node;
	  return omit_one_operand (type, t1, arg0);
	}

      /* Simplify unordered comparison of something with itself.  */
      if ((code == UNLE_EXPR || code == UNGE_EXPR || code == UNEQ_EXPR)
	  && operand_equal_p (arg0, arg1, 0))
	return constant_boolean_node (1, type);

      if (code == LTGT_EXPR
	  && !flag_trapping_math
	  && operand_equal_p (arg0, arg1, 0))
	return constant_boolean_node (0, type);

      /* Fold (double)float1 CMP (double)float2 into float1 CMP float2.  */
      {
	tree targ0 = strip_float_extensions (arg0);
	tree targ1 = strip_float_extensions (arg1);
	tree newtype = TREE_TYPE (targ0);

	if (TYPE_PRECISION (TREE_TYPE (targ1)) > TYPE_PRECISION (newtype))
	  newtype = TREE_TYPE (targ1);

	if (TYPE_PRECISION (newtype) < TYPE_PRECISION (TREE_TYPE (arg0)))
	  return fold_build2 (code, type, fold_convert (newtype, targ0),
			      fold_convert (newtype, targ1));
      }

      return NULL_TREE;

    case COMPOUND_EXPR:
      /* When pedantic, a compound expression can be neither an lvalue
	 nor an integer constant expression.  */
      if (TREE_SIDE_EFFECTS (arg0) || TREE_CONSTANT (arg1))
	return NULL_TREE;
      /* Don't let (0, 0) be null pointer constant.  */
      tem = integer_zerop (arg1) ? build1 (NOP_EXPR, type, arg1)
				 : fold_convert (type, arg1);
      return pedantic_non_lvalue (tem);

    case COMPLEX_EXPR:
      if ((TREE_CODE (arg0) == REAL_CST
	   && TREE_CODE (arg1) == REAL_CST)
	  || (TREE_CODE (arg0) == INTEGER_CST
	      && TREE_CODE (arg1) == INTEGER_CST))
	return build_complex (type, arg0, arg1);
      return NULL_TREE;

    case ASSERT_EXPR:
      /* An ASSERT_EXPR should never be passed to fold_binary.  */
      gcc_unreachable ();

    default:
      return NULL_TREE;
    } /* switch (code) */
}

/* Callback for walk_tree, looking for LABEL_EXPR.
   Returns tree TP if it is LABEL_EXPR. Otherwise it returns NULL_TREE.
   Do not check the sub-tree of GOTO_EXPR.  */

static tree
contains_label_1 (tree *tp,
                  int *walk_subtrees,
                  void *data ATTRIBUTE_UNUSED)
{
  switch (TREE_CODE (*tp))
    {
    case LABEL_EXPR:
      return *tp;
    case GOTO_EXPR:
      *walk_subtrees = 0;
    /* no break */
    default:
      return NULL_TREE;
    }
}

/* Checks whether the sub-tree ST contains a label LABEL_EXPR which is
   accessible from outside the sub-tree. Returns NULL_TREE if no
   addressable label is found.  */

static bool
contains_label_p (tree st)
{
  return (walk_tree (&st, contains_label_1 , NULL, NULL) != NULL_TREE);
}

/* Fold a ternary expression of code CODE and type TYPE with operands
   OP0, OP1, and OP2.  Return the folded expression if folding is
   successful.  Otherwise, return NULL_TREE.  */

tree
fold_ternary (enum tree_code code, tree type, tree op0, tree op1, tree op2)
{
  tree tem;
  tree arg0 = NULL_TREE, arg1 = NULL_TREE;
  enum tree_code_class kind = TREE_CODE_CLASS (code);

  gcc_assert (IS_EXPR_CODE_CLASS (kind)
	      && TREE_CODE_LENGTH (code) == 3);

  /* Strip any conversions that don't change the mode.  This is safe
     for every expression, except for a comparison expression because
     its signedness is derived from its operands.  So, in the latter
     case, only strip conversions that don't change the signedness.

     Note that this is done as an internal manipulation within the
     constant folder, in order to find the simplest representation of
     the arguments so that their form can be studied.  In any cases,
     the appropriate type conversions should be put back in the tree
     that will get out of the constant folder.  */
  if (op0)
    {
      arg0 = op0;
      STRIP_NOPS (arg0);
    }

  if (op1)
    {
      arg1 = op1;
      STRIP_NOPS (arg1);
    }

  switch (code)
    {
    case COMPONENT_REF:
      if (TREE_CODE (arg0) == CONSTRUCTOR
	  && ! type_contains_placeholder_p (TREE_TYPE (arg0)))
	{
	  unsigned HOST_WIDE_INT idx;
	  tree field, value;
	  FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (arg0), idx, field, value)
	    if (field == arg1)
	      return value;
	}
      return NULL_TREE;

    case COND_EXPR:
      /* Pedantic ANSI C says that a conditional expression is never an lvalue,
	 so all simple results must be passed through pedantic_non_lvalue.  */
      if (TREE_CODE (arg0) == INTEGER_CST)
	{
	  tree unused_op = integer_zerop (arg0) ? op1 : op2;
	  tem = integer_zerop (arg0) ? op2 : op1;
	  /* Only optimize constant conditions when the selected branch
	     has the same type as the COND_EXPR.  This avoids optimizing
             away "c ? x : throw", where the throw has a void type.
             Avoid throwing away that operand which contains label.  */
          if ((!TREE_SIDE_EFFECTS (unused_op)
               || !contains_label_p (unused_op))
              && (! VOID_TYPE_P (TREE_TYPE (tem))
                  || VOID_TYPE_P (type)))
	    return pedantic_non_lvalue (tem);
	  return NULL_TREE;
	}
      if (operand_equal_p (arg1, op2, 0))
	return pedantic_omit_one_operand (type, arg1, arg0);

      /* If we have A op B ? A : C, we may be able to convert this to a
	 simpler expression, depending on the operation and the values
	 of B and C.  Signed zeros prevent all of these transformations,
	 for reasons given above each one.

         Also try swapping the arguments and inverting the conditional.  */
      if (COMPARISON_CLASS_P (arg0)
	  && operand_equal_for_comparison_p (TREE_OPERAND (arg0, 0),
					     arg1, TREE_OPERAND (arg0, 1))
	  && !HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (arg1))))
	{
	  tem = fold_cond_expr_with_comparison (type, arg0, op1, op2);
	  if (tem)
	    return tem;
	}

      if (COMPARISON_CLASS_P (arg0)
	  && operand_equal_for_comparison_p (TREE_OPERAND (arg0, 0),
					     op2,
					     TREE_OPERAND (arg0, 1))
	  && !HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (op2))))
	{
	  tem = fold_truth_not_expr (arg0);
	  if (tem && COMPARISON_CLASS_P (tem))
	    {
	      tem = fold_cond_expr_with_comparison (type, tem, op2, op1);
	      if (tem)
		return tem;
	    }
	}

      /* If the second operand is simpler than the third, swap them
	 since that produces better jump optimization results.  */
      if (truth_value_p (TREE_CODE (arg0))
	  && tree_swap_operands_p (op1, op2, false))
	{
	  /* See if this can be inverted.  If it can't, possibly because
	     it was a floating-point inequality comparison, don't do
	     anything.  */
	  tem = fold_truth_not_expr (arg0);
	  if (tem)
	    return fold_build3 (code, type, tem, op2, op1);
	}

      /* Convert A ? 1 : 0 to simply A.  */
      if (integer_onep (op1)
	  && integer_zerop (op2)
	  /* If we try to convert OP0 to our type, the
	     call to fold will try to move the conversion inside
	     a COND, which will recurse.  In that case, the COND_EXPR
	     is probably the best choice, so leave it alone.  */
	  && type == TREE_TYPE (arg0))
	return pedantic_non_lvalue (arg0);

      /* Convert A ? 0 : 1 to !A.  This prefers the use of NOT_EXPR
	 over COND_EXPR in cases such as floating point comparisons.  */
      if (integer_zerop (op1)
	  && integer_onep (op2)
	  && truth_value_p (TREE_CODE (arg0)))
	return pedantic_non_lvalue (fold_convert (type,
						  invert_truthvalue (arg0)));

      /* A < 0 ? <sign bit of A> : 0 is simply (A & <sign bit of A>).  */
      if (TREE_CODE (arg0) == LT_EXPR
	  && integer_zerop (TREE_OPERAND (arg0, 1))
	  && integer_zerop (op2)
	  && (tem = sign_bit_p (TREE_OPERAND (arg0, 0), arg1)))
	{
	  /* sign_bit_p only checks ARG1 bits within A's precision.
	     If <sign bit of A> has wider type than A, bits outside
	     of A's precision in <sign bit of A> need to be checked.
	     If they are all 0, this optimization needs to be done
	     in unsigned A's type, if they are all 1 in signed A's type,
	     otherwise this can't be done.  */
	  if (TYPE_PRECISION (TREE_TYPE (tem))
	      < TYPE_PRECISION (TREE_TYPE (arg1))
	      && TYPE_PRECISION (TREE_TYPE (tem))
		 < TYPE_PRECISION (type))
	    {
	      unsigned HOST_WIDE_INT mask_lo;
	      HOST_WIDE_INT mask_hi;
	      int inner_width, outer_width;
	      tree tem_type;

	      inner_width = TYPE_PRECISION (TREE_TYPE (tem));
	      outer_width = TYPE_PRECISION (TREE_TYPE (arg1));
	      if (outer_width > TYPE_PRECISION (type))
		outer_width = TYPE_PRECISION (type);

	      if (outer_width > HOST_BITS_PER_WIDE_INT)
		{
		  mask_hi = ((unsigned HOST_WIDE_INT) -1
			     >> (2 * HOST_BITS_PER_WIDE_INT - outer_width));
		  mask_lo = -1;
		}
	      else
		{
		  mask_hi = 0;
		  mask_lo = ((unsigned HOST_WIDE_INT) -1
			     >> (HOST_BITS_PER_WIDE_INT - outer_width));
		}
	      if (inner_width > HOST_BITS_PER_WIDE_INT)
		{
		  mask_hi &= ~((unsigned HOST_WIDE_INT) -1
			       >> (HOST_BITS_PER_WIDE_INT - inner_width));
		  mask_lo = 0;
		}
	      else
		mask_lo &= ~((unsigned HOST_WIDE_INT) -1
			     >> (HOST_BITS_PER_WIDE_INT - inner_width));

	      if ((TREE_INT_CST_HIGH (arg1) & mask_hi) == mask_hi
		  && (TREE_INT_CST_LOW (arg1) & mask_lo) == mask_lo)
		{
		  tem_type = lang_hooks.types.signed_type (TREE_TYPE (tem));
		  tem = fold_convert (tem_type, tem);
		}
	      else if ((TREE_INT_CST_HIGH (arg1) & mask_hi) == 0
		       && (TREE_INT_CST_LOW (arg1) & mask_lo) == 0)
		{
		  tem_type = lang_hooks.types.unsigned_type (TREE_TYPE (tem));
		  tem = fold_convert (tem_type, tem);
		}
	      else
		tem = NULL;
	    }

	  if (tem)
	    return fold_convert (type,
				 fold_build2 (BIT_AND_EXPR,
					      TREE_TYPE (tem), tem,
					      fold_convert (TREE_TYPE (tem),
							    arg1)));
	}

      /* (A >> N) & 1 ? (1 << N) : 0 is simply A & (1 << N).  A & 1 was
	 already handled above.  */
      if (TREE_CODE (arg0) == BIT_AND_EXPR
	  && integer_onep (TREE_OPERAND (arg0, 1))
	  && integer_zerop (op2)
	  && integer_pow2p (arg1))
	{
	  tree tem = TREE_OPERAND (arg0, 0);
	  STRIP_NOPS (tem);
	  if (TREE_CODE (tem) == RSHIFT_EXPR
              && TREE_CODE (TREE_OPERAND (tem, 1)) == INTEGER_CST
              && (unsigned HOST_WIDE_INT) tree_log2 (arg1) ==
	         TREE_INT_CST_LOW (TREE_OPERAND (tem, 1)))
	    return fold_build2 (BIT_AND_EXPR, type,
				TREE_OPERAND (tem, 0), arg1);
	}

      /* A & N ? N : 0 is simply A & N if N is a power of two.  This
	 is probably obsolete because the first operand should be a
	 truth value (that's why we have the two cases above), but let's
	 leave it in until we can confirm this for all front-ends.  */
      if (integer_zerop (op2)
	  && TREE_CODE (arg0) == NE_EXPR
	  && integer_zerop (TREE_OPERAND (arg0, 1))
	  && integer_pow2p (arg1)
	  && TREE_CODE (TREE_OPERAND (arg0, 0)) == BIT_AND_EXPR
	  && operand_equal_p (TREE_OPERAND (TREE_OPERAND (arg0, 0), 1),
			      arg1, OEP_ONLY_CONST))
	return pedantic_non_lvalue (fold_convert (type,
						  TREE_OPERAND (arg0, 0)));

      /* Convert A ? B : 0 into A && B if A and B are truth values.  */
      if (integer_zerop (op2)
	  && truth_value_p (TREE_CODE (arg0))
	  && truth_value_p (TREE_CODE (arg1)))
	return fold_build2 (TRUTH_ANDIF_EXPR, type,
			    fold_convert (type, arg0),
			    arg1);

      /* Convert A ? B : 1 into !A || B if A and B are truth values.  */
      if (integer_onep (op2)
	  && truth_value_p (TREE_CODE (arg0))
	  && truth_value_p (TREE_CODE (arg1)))
	{
	  /* Only perform transformation if ARG0 is easily inverted.  */
	  tem = fold_truth_not_expr (arg0);
	  if (tem)
	    return fold_build2 (TRUTH_ORIF_EXPR, type,
				fold_convert (type, tem),
				arg1);
	}

      /* Convert A ? 0 : B into !A && B if A and B are truth values.  */
      if (integer_zerop (arg1)
	  && truth_value_p (TREE_CODE (arg0))
	  && truth_value_p (TREE_CODE (op2)))
	{
	  /* Only perform transformation if ARG0 is easily inverted.  */
	  tem = fold_truth_not_expr (arg0);
	  if (tem)
	    return fold_build2 (TRUTH_ANDIF_EXPR, type,
				fold_convert (type, tem),
				op2);
	}

      /* Convert A ? 1 : B into A || B if A and B are truth values.  */
      if (integer_onep (arg1)
	  && truth_value_p (TREE_CODE (arg0))
	  && truth_value_p (TREE_CODE (op2)))
	return fold_build2 (TRUTH_ORIF_EXPR, type,
			    fold_convert (type, arg0),
			    op2);

      return NULL_TREE;

    case CALL_EXPR:
      /* Check for a built-in function.  */
      if (TREE_CODE (op0) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (op0, 0)) == FUNCTION_DECL
	  && DECL_BUILT_IN (TREE_OPERAND (op0, 0)))
	return fold_builtin (TREE_OPERAND (op0, 0), op1, false);
      return NULL_TREE;

    case BIT_FIELD_REF:
      if (TREE_CODE (arg0) == VECTOR_CST
	  && type == TREE_TYPE (TREE_TYPE (arg0))
	  && host_integerp (arg1, 1)
	  && host_integerp (op2, 1))
	{
	  unsigned HOST_WIDE_INT width = tree_low_cst (arg1, 1);
	  unsigned HOST_WIDE_INT idx = tree_low_cst (op2, 1);

	  if (width != 0
	      && simple_cst_equal (arg1, TYPE_SIZE (type)) == 1
	      && (idx % width) == 0
	      && (idx = idx / width)
		 < TYPE_VECTOR_SUBPARTS (TREE_TYPE (arg0)))
	    {
	      tree elements = TREE_VECTOR_CST_ELTS (arg0);
	      while (idx-- > 0 && elements)
		elements = TREE_CHAIN (elements);
	      if (elements)
		return TREE_VALUE (elements);
	      else
		return fold_convert (type, integer_zero_node);
	    }
	}
      return NULL_TREE;

    default:
      return NULL_TREE;
    } /* switch (code) */
}

/* Perform constant folding and related simplification of EXPR.
   The related simplifications include x*1 => x, x*0 => 0, etc.,
   and application of the associative law.
   NOP_EXPR conversions may be removed freely (as long as we
   are careful not to change the type of the overall expression).
   We cannot simplify through a CONVERT_EXPR, FIX_EXPR or FLOAT_EXPR,
   but we can constant-fold them if they have constant operands.  */

#ifdef ENABLE_FOLD_CHECKING
# define fold(x) fold_1 (x)
static tree fold_1 (tree);
static
#endif
tree
fold (tree expr)
{
  const tree t = expr;
  enum tree_code code = TREE_CODE (t);
  enum tree_code_class kind = TREE_CODE_CLASS (code);
  tree tem;

  /* Return right away if a constant.  */
  if (kind == tcc_constant)
    return t;

  if (IS_EXPR_CODE_CLASS (kind))
    {
      tree type = TREE_TYPE (t);
      tree op0, op1, op2;

      switch (TREE_CODE_LENGTH (code))
	{
	case 1:
	  op0 = TREE_OPERAND (t, 0);
	  tem = fold_unary (code, type, op0);
	  return tem ? tem : expr;
	case 2:
	  op0 = TREE_OPERAND (t, 0);
	  op1 = TREE_OPERAND (t, 1);
	  tem = fold_binary (code, type, op0, op1);
	  return tem ? tem : expr;
	case 3:
	  op0 = TREE_OPERAND (t, 0);
	  op1 = TREE_OPERAND (t, 1);
	  op2 = TREE_OPERAND (t, 2);
	  tem = fold_ternary (code, type, op0, op1, op2);
	  return tem ? tem : expr;
	default:
	  break;
	}
    }

  switch (code)
    {
    case CONST_DECL:
      return fold (DECL_INITIAL (t));

    default:
      return t;
    } /* switch (code) */
}

#ifdef ENABLE_FOLD_CHECKING
#undef fold

static void fold_checksum_tree (tree, struct md5_ctx *, htab_t);
static void fold_check_failed (tree, tree);
void print_fold_checksum (tree);

/* When --enable-checking=fold, compute a digest of expr before
   and after actual fold call to see if fold did not accidentally
   change original expr.  */

tree
fold (tree expr)
{
  tree ret;
  struct md5_ctx ctx;
  unsigned char checksum_before[16], checksum_after[16];
  htab_t ht;

  ht = htab_create (32, htab_hash_pointer, htab_eq_pointer, NULL);
  md5_init_ctx (&ctx);
  fold_checksum_tree (expr, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before);
  htab_empty (ht);

  ret = fold_1 (expr);

  md5_init_ctx (&ctx);
  fold_checksum_tree (expr, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after);
  htab_delete (ht);

  if (memcmp (checksum_before, checksum_after, 16))
    fold_check_failed (expr, ret);

  return ret;
}

void
print_fold_checksum (tree expr)
{
  struct md5_ctx ctx;
  unsigned char checksum[16], cnt;
  htab_t ht;

  ht = htab_create (32, htab_hash_pointer, htab_eq_pointer, NULL);
  md5_init_ctx (&ctx);
  fold_checksum_tree (expr, &ctx, ht);
  md5_finish_ctx (&ctx, checksum);
  htab_delete (ht);
  for (cnt = 0; cnt < 16; ++cnt)
    fprintf (stderr, "%02x", checksum[cnt]);
  putc ('\n', stderr);
}

static void
fold_check_failed (tree expr ATTRIBUTE_UNUSED, tree ret ATTRIBUTE_UNUSED)
{
  internal_error ("fold check: original tree changed by fold");
}

static void
fold_checksum_tree (tree expr, struct md5_ctx *ctx, htab_t ht)
{
  void **slot;
  enum tree_code code;
  struct tree_function_decl buf;
  int i, len;
  
recursive_label:

  gcc_assert ((sizeof (struct tree_exp) + 5 * sizeof (tree)
	       <= sizeof (struct tree_function_decl))
	      && sizeof (struct tree_type) <= sizeof (struct tree_function_decl));
  if (expr == NULL)
    return;
  slot = htab_find_slot (ht, expr, INSERT);
  if (*slot != NULL)
    return;
  *slot = expr;
  code = TREE_CODE (expr);
  if (TREE_CODE_CLASS (code) == tcc_declaration
      && DECL_ASSEMBLER_NAME_SET_P (expr))
    {
      /* Allow DECL_ASSEMBLER_NAME to be modified.  */
      memcpy ((char *) &buf, expr, tree_size (expr));
      expr = (tree) &buf;
      SET_DECL_ASSEMBLER_NAME (expr, NULL);
    }
  else if (TREE_CODE_CLASS (code) == tcc_type
	   && (TYPE_POINTER_TO (expr) || TYPE_REFERENCE_TO (expr)
	       || TYPE_CACHED_VALUES_P (expr)
	       || TYPE_CONTAINS_PLACEHOLDER_INTERNAL (expr)))
    {
      /* Allow these fields to be modified.  */
      memcpy ((char *) &buf, expr, tree_size (expr));
      expr = (tree) &buf;
      TYPE_CONTAINS_PLACEHOLDER_INTERNAL (expr) = 0;
      TYPE_POINTER_TO (expr) = NULL;
      TYPE_REFERENCE_TO (expr) = NULL;
      if (TYPE_CACHED_VALUES_P (expr))
	{
	  TYPE_CACHED_VALUES_P (expr) = 0;
	  TYPE_CACHED_VALUES (expr) = NULL;
	}
    }
  md5_process_bytes (expr, tree_size (expr), ctx);
  fold_checksum_tree (TREE_TYPE (expr), ctx, ht);
  if (TREE_CODE_CLASS (code) != tcc_type
      && TREE_CODE_CLASS (code) != tcc_declaration
      && code != TREE_LIST)
    fold_checksum_tree (TREE_CHAIN (expr), ctx, ht);
  switch (TREE_CODE_CLASS (code))
    {
    case tcc_constant:
      switch (code)
	{
	case STRING_CST:
	  md5_process_bytes (TREE_STRING_POINTER (expr),
			     TREE_STRING_LENGTH (expr), ctx);
	  break;
	case COMPLEX_CST:
	  fold_checksum_tree (TREE_REALPART (expr), ctx, ht);
	  fold_checksum_tree (TREE_IMAGPART (expr), ctx, ht);
	  break;
	case VECTOR_CST:
	  fold_checksum_tree (TREE_VECTOR_CST_ELTS (expr), ctx, ht);
	  break;
	default:
	  break;
	}
      break;
    case tcc_exceptional:
      switch (code)
	{
	case TREE_LIST:
	  fold_checksum_tree (TREE_PURPOSE (expr), ctx, ht);
	  fold_checksum_tree (TREE_VALUE (expr), ctx, ht);
	  expr = TREE_CHAIN (expr);
	  goto recursive_label;
	  break;
	case TREE_VEC:
	  for (i = 0; i < TREE_VEC_LENGTH (expr); ++i)
	    fold_checksum_tree (TREE_VEC_ELT (expr, i), ctx, ht);
	  break;
	default:
	  break;
	}
      break;
    case tcc_expression:
    case tcc_reference:
    case tcc_comparison:
    case tcc_unary:
    case tcc_binary:
    case tcc_statement:
      len = TREE_CODE_LENGTH (code);
      for (i = 0; i < len; ++i)
	fold_checksum_tree (TREE_OPERAND (expr, i), ctx, ht);
      break;
    case tcc_declaration:
      fold_checksum_tree (DECL_NAME (expr), ctx, ht);
      fold_checksum_tree (DECL_CONTEXT (expr), ctx, ht);
      if (CODE_CONTAINS_STRUCT (TREE_CODE (expr), TS_DECL_COMMON))
	{
	  fold_checksum_tree (DECL_SIZE (expr), ctx, ht);
	  fold_checksum_tree (DECL_SIZE_UNIT (expr), ctx, ht);
	  fold_checksum_tree (DECL_INITIAL (expr), ctx, ht);
	  fold_checksum_tree (DECL_ABSTRACT_ORIGIN (expr), ctx, ht);
	  fold_checksum_tree (DECL_ATTRIBUTES (expr), ctx, ht);
	}
      if (CODE_CONTAINS_STRUCT (TREE_CODE (expr), TS_DECL_WITH_VIS))
	fold_checksum_tree (DECL_SECTION_NAME (expr), ctx, ht);
	  
      if (CODE_CONTAINS_STRUCT (TREE_CODE (expr), TS_DECL_NON_COMMON))
	{
	  fold_checksum_tree (DECL_VINDEX (expr), ctx, ht);
	  fold_checksum_tree (DECL_RESULT_FLD (expr), ctx, ht);
	  fold_checksum_tree (DECL_ARGUMENT_FLD (expr), ctx, ht);
	}
      break;
    case tcc_type:
      if (TREE_CODE (expr) == ENUMERAL_TYPE)
        fold_checksum_tree (TYPE_VALUES (expr), ctx, ht);
      fold_checksum_tree (TYPE_SIZE (expr), ctx, ht);
      fold_checksum_tree (TYPE_SIZE_UNIT (expr), ctx, ht);
      fold_checksum_tree (TYPE_ATTRIBUTES (expr), ctx, ht);
      fold_checksum_tree (TYPE_NAME (expr), ctx, ht);
      if (INTEGRAL_TYPE_P (expr)
          || SCALAR_FLOAT_TYPE_P (expr))
	{
	  fold_checksum_tree (TYPE_MIN_VALUE (expr), ctx, ht);
	  fold_checksum_tree (TYPE_MAX_VALUE (expr), ctx, ht);
	}
      fold_checksum_tree (TYPE_MAIN_VARIANT (expr), ctx, ht);
      if (TREE_CODE (expr) == RECORD_TYPE
	  || TREE_CODE (expr) == UNION_TYPE
	  || TREE_CODE (expr) == QUAL_UNION_TYPE)
	fold_checksum_tree (TYPE_BINFO (expr), ctx, ht);
      fold_checksum_tree (TYPE_CONTEXT (expr), ctx, ht);
      break;
    default:
      break;
    }
}

#endif

/* Fold a unary tree expression with code CODE of type TYPE with an
   operand OP0.  Return a folded expression if successful.  Otherwise,
   return a tree expression with code CODE of type TYPE with an
   operand OP0.  */

tree
fold_build1_stat (enum tree_code code, tree type, tree op0 MEM_STAT_DECL)
{
  tree tem;
#ifdef ENABLE_FOLD_CHECKING
  unsigned char checksum_before[16], checksum_after[16];
  struct md5_ctx ctx;
  htab_t ht;

  ht = htab_create (32, htab_hash_pointer, htab_eq_pointer, NULL);
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before);
  htab_empty (ht);
#endif
  
  tem = fold_unary (code, type, op0);
  if (!tem)
    tem = build1_stat (code, type, op0 PASS_MEM_STAT);
  
#ifdef ENABLE_FOLD_CHECKING
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after);
  htab_delete (ht);

  if (memcmp (checksum_before, checksum_after, 16))
    fold_check_failed (op0, tem);
#endif
  return tem;
}

/* Fold a binary tree expression with code CODE of type TYPE with
   operands OP0 and OP1.  Return a folded expression if successful.
   Otherwise, return a tree expression with code CODE of type TYPE
   with operands OP0 and OP1.  */

tree
fold_build2_stat (enum tree_code code, tree type, tree op0, tree op1
		  MEM_STAT_DECL)
{
  tree tem;
#ifdef ENABLE_FOLD_CHECKING
  unsigned char checksum_before_op0[16],
                checksum_before_op1[16],
		checksum_after_op0[16],
		checksum_after_op1[16];
  struct md5_ctx ctx;
  htab_t ht;

  ht = htab_create (32, htab_hash_pointer, htab_eq_pointer, NULL);
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before_op0);
  htab_empty (ht);

  md5_init_ctx (&ctx);
  fold_checksum_tree (op1, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before_op1);
  htab_empty (ht);
#endif

  tem = fold_binary (code, type, op0, op1);
  if (!tem)
    tem = build2_stat (code, type, op0, op1 PASS_MEM_STAT);
  
#ifdef ENABLE_FOLD_CHECKING
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after_op0);
  htab_empty (ht);

  if (memcmp (checksum_before_op0, checksum_after_op0, 16))
    fold_check_failed (op0, tem);
  
  md5_init_ctx (&ctx);
  fold_checksum_tree (op1, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after_op1);
  htab_delete (ht);

  if (memcmp (checksum_before_op1, checksum_after_op1, 16))
    fold_check_failed (op1, tem);
#endif
  return tem;
}

/* Fold a ternary tree expression with code CODE of type TYPE with
   operands OP0, OP1, and OP2.  Return a folded expression if
   successful.  Otherwise, return a tree expression with code CODE of
   type TYPE with operands OP0, OP1, and OP2.  */

tree
fold_build3_stat (enum tree_code code, tree type, tree op0, tree op1, tree op2
	     MEM_STAT_DECL)
{
  tree tem;
#ifdef ENABLE_FOLD_CHECKING
  unsigned char checksum_before_op0[16],
                checksum_before_op1[16],
                checksum_before_op2[16],
		checksum_after_op0[16],
		checksum_after_op1[16],
		checksum_after_op2[16];
  struct md5_ctx ctx;
  htab_t ht;

  ht = htab_create (32, htab_hash_pointer, htab_eq_pointer, NULL);
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before_op0);
  htab_empty (ht);

  md5_init_ctx (&ctx);
  fold_checksum_tree (op1, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before_op1);
  htab_empty (ht);

  md5_init_ctx (&ctx);
  fold_checksum_tree (op2, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_before_op2);
  htab_empty (ht);
#endif
  
  tem = fold_ternary (code, type, op0, op1, op2);
  if (!tem)
    tem =  build3_stat (code, type, op0, op1, op2 PASS_MEM_STAT);
      
#ifdef ENABLE_FOLD_CHECKING
  md5_init_ctx (&ctx);
  fold_checksum_tree (op0, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after_op0);
  htab_empty (ht);

  if (memcmp (checksum_before_op0, checksum_after_op0, 16))
    fold_check_failed (op0, tem);
  
  md5_init_ctx (&ctx);
  fold_checksum_tree (op1, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after_op1);
  htab_empty (ht);

  if (memcmp (checksum_before_op1, checksum_after_op1, 16))
    fold_check_failed (op1, tem);
  
  md5_init_ctx (&ctx);
  fold_checksum_tree (op2, &ctx, ht);
  md5_finish_ctx (&ctx, checksum_after_op2);
  htab_delete (ht);

  if (memcmp (checksum_before_op2, checksum_after_op2, 16))
    fold_check_failed (op2, tem);
#endif
  return tem;
}

/* Perform constant folding and related simplification of initializer
   expression EXPR.  These behave identically to "fold_buildN" but ignore
   potential run-time traps and exceptions that fold must preserve.  */

#define START_FOLD_INIT \
  int saved_signaling_nans = flag_signaling_nans;\
  int saved_trapping_math = flag_trapping_math;\
  int saved_rounding_math = flag_rounding_math;\
  int saved_trapv = flag_trapv;\
  int saved_folding_initializer = folding_initializer;\
  flag_signaling_nans = 0;\
  flag_trapping_math = 0;\
  flag_rounding_math = 0;\
  flag_trapv = 0;\
  folding_initializer = 1;

#define END_FOLD_INIT \
  flag_signaling_nans = saved_signaling_nans;\
  flag_trapping_math = saved_trapping_math;\
  flag_rounding_math = saved_rounding_math;\
  flag_trapv = saved_trapv;\
  folding_initializer = saved_folding_initializer;

tree
fold_build1_initializer (enum tree_code code, tree type, tree op)
{
  tree result;
  START_FOLD_INIT;

  result = fold_build1 (code, type, op);

  END_FOLD_INIT;
  return result;
}

tree
fold_build2_initializer (enum tree_code code, tree type, tree op0, tree op1)
{
  tree result;
  START_FOLD_INIT;

  result = fold_build2 (code, type, op0, op1);

  END_FOLD_INIT;
  return result;
}

tree
fold_build3_initializer (enum tree_code code, tree type, tree op0, tree op1,
			 tree op2)
{
  tree result;
  START_FOLD_INIT;

  result = fold_build3 (code, type, op0, op1, op2);

  END_FOLD_INIT;
  return result;
}

#undef START_FOLD_INIT
#undef END_FOLD_INIT

/* Determine if first argument is a multiple of second argument.  Return 0 if
   it is not, or we cannot easily determined it to be.

   An example of the sort of thing we care about (at this point; this routine
   could surely be made more general, and expanded to do what the *_DIV_EXPR's
   fold cases do now) is discovering that

     SAVE_EXPR (I) * SAVE_EXPR (J * 8)

   is a multiple of

     SAVE_EXPR (J * 8)

   when we know that the two SAVE_EXPR (J * 8) nodes are the same node.

   This code also handles discovering that

     SAVE_EXPR (I) * SAVE_EXPR (J * 8)

   is a multiple of 8 so we don't have to worry about dealing with a
   possible remainder.

   Note that we *look* inside a SAVE_EXPR only to determine how it was
   calculated; it is not safe for fold to do much of anything else with the
   internals of a SAVE_EXPR, since it cannot know when it will be evaluated
   at run time.  For example, the latter example above *cannot* be implemented
   as SAVE_EXPR (I) * J or any variant thereof, since the value of J at
   evaluation time of the original SAVE_EXPR is not necessarily the same at
   the time the new expression is evaluated.  The only optimization of this
   sort that would be valid is changing

     SAVE_EXPR (I) * SAVE_EXPR (SAVE_EXPR (J) * 8)

   divided by 8 to

     SAVE_EXPR (I) * SAVE_EXPR (J)

   (where the same SAVE_EXPR (J) is used in the original and the
   transformed version).  */

static int
multiple_of_p (tree type, tree top, tree bottom)
{
  if (operand_equal_p (top, bottom, 0))
    return 1;

  if (TREE_CODE (type) != INTEGER_TYPE)
    return 0;

  switch (TREE_CODE (top))
    {
    case BIT_AND_EXPR:
      /* Bitwise and provides a power of two multiple.  If the mask is
	 a multiple of BOTTOM then TOP is a multiple of BOTTOM.  */
      if (!integer_pow2p (bottom))
	return 0;
      /* FALLTHRU */

    case MULT_EXPR:
      return (multiple_of_p (type, TREE_OPERAND (top, 0), bottom)
	      || multiple_of_p (type, TREE_OPERAND (top, 1), bottom));

    case PLUS_EXPR:
    case MINUS_EXPR:
      return (multiple_of_p (type, TREE_OPERAND (top, 0), bottom)
	      && multiple_of_p (type, TREE_OPERAND (top, 1), bottom));

    case LSHIFT_EXPR:
      if (TREE_CODE (TREE_OPERAND (top, 1)) == INTEGER_CST)
	{
	  tree op1, t1;

	  op1 = TREE_OPERAND (top, 1);
	  /* const_binop may not detect overflow correctly,
	     so check for it explicitly here.  */
	  if (TYPE_PRECISION (TREE_TYPE (size_one_node))
	      > TREE_INT_CST_LOW (op1)
	      && TREE_INT_CST_HIGH (op1) == 0
	      && 0 != (t1 = fold_convert (type,
					  const_binop (LSHIFT_EXPR,
						       size_one_node,
						       op1, 0)))
	      && ! TREE_OVERFLOW (t1))
	    return multiple_of_p (type, t1, bottom);
	}
      return 0;

    case NOP_EXPR:
      /* Can't handle conversions from non-integral or wider integral type.  */
      if ((TREE_CODE (TREE_TYPE (TREE_OPERAND (top, 0))) != INTEGER_TYPE)
	  || (TYPE_PRECISION (type)
	      < TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (top, 0)))))
	return 0;

      /* .. fall through ...  */

    case SAVE_EXPR:
      return multiple_of_p (type, TREE_OPERAND (top, 0), bottom);

    case INTEGER_CST:
      if (TREE_CODE (bottom) != INTEGER_CST
	  || (TYPE_UNSIGNED (type)
	      && (tree_int_cst_sgn (top) < 0
		  || tree_int_cst_sgn (bottom) < 0)))
	return 0;
      return integer_zerop (const_binop (TRUNC_MOD_EXPR,
					 top, bottom, 0));

    default:
      return 0;
    }
}

/* Return true if `t' is known to be non-negative.  If the return
   value is based on the assumption that signed overflow is undefined,
   set *STRICT_OVERFLOW_P to true; otherwise, don't change
   *STRICT_OVERFLOW_P.  */

int
tree_expr_nonnegative_warnv_p (tree t, bool *strict_overflow_p)
{
  if (t == error_mark_node)
    return 0;

  if (TYPE_UNSIGNED (TREE_TYPE (t)))
    return 1;

  switch (TREE_CODE (t))
    {
    case SSA_NAME:
      /* Query VRP to see if it has recorded any information about
	 the range of this object.  */
      return ssa_name_nonnegative_p (t);

    case ABS_EXPR:
      /* We can't return 1 if flag_wrapv is set because
	 ABS_EXPR<INT_MIN> = INT_MIN.  */
      if (!INTEGRAL_TYPE_P (TREE_TYPE (t)))
	return 1;
      if (TYPE_OVERFLOW_UNDEFINED (TREE_TYPE (t)))
	{
	  *strict_overflow_p = true;
	  return 1;
	}
      break;

    case INTEGER_CST:
      return tree_int_cst_sgn (t) >= 0;

    case REAL_CST:
      return ! REAL_VALUE_NEGATIVE (TREE_REAL_CST (t));

    case PLUS_EXPR:
      if (FLOAT_TYPE_P (TREE_TYPE (t)))
	return (tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					       strict_overflow_p)
		&& tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						  strict_overflow_p));

      /* zero_extend(x) + zero_extend(y) is non-negative if x and y are
	 both unsigned and at least 2 bits shorter than the result.  */
      if (TREE_CODE (TREE_TYPE (t)) == INTEGER_TYPE
	  && TREE_CODE (TREE_OPERAND (t, 0)) == NOP_EXPR
	  && TREE_CODE (TREE_OPERAND (t, 1)) == NOP_EXPR)
	{
	  tree inner1 = TREE_TYPE (TREE_OPERAND (TREE_OPERAND (t, 0), 0));
	  tree inner2 = TREE_TYPE (TREE_OPERAND (TREE_OPERAND (t, 1), 0));
	  if (TREE_CODE (inner1) == INTEGER_TYPE && TYPE_UNSIGNED (inner1)
	      && TREE_CODE (inner2) == INTEGER_TYPE && TYPE_UNSIGNED (inner2))
	    {
	      unsigned int prec = MAX (TYPE_PRECISION (inner1),
				       TYPE_PRECISION (inner2)) + 1;
	      return prec < TYPE_PRECISION (TREE_TYPE (t));
	    }
	}
      break;

    case MULT_EXPR:
      if (FLOAT_TYPE_P (TREE_TYPE (t)))
	{
	  /* x * x for floating point x is always non-negative.  */
	  if (operand_equal_p (TREE_OPERAND (t, 0), TREE_OPERAND (t, 1), 0))
	    return 1;
	  return (tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
						 strict_overflow_p)
		  && tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						    strict_overflow_p));
	}

      /* zero_extend(x) * zero_extend(y) is non-negative if x and y are
	 both unsigned and their total bits is shorter than the result.  */
      if (TREE_CODE (TREE_TYPE (t)) == INTEGER_TYPE
	  && TREE_CODE (TREE_OPERAND (t, 0)) == NOP_EXPR
	  && TREE_CODE (TREE_OPERAND (t, 1)) == NOP_EXPR)
	{
	  tree inner1 = TREE_TYPE (TREE_OPERAND (TREE_OPERAND (t, 0), 0));
	  tree inner2 = TREE_TYPE (TREE_OPERAND (TREE_OPERAND (t, 1), 0));
	  if (TREE_CODE (inner1) == INTEGER_TYPE && TYPE_UNSIGNED (inner1)
	      && TREE_CODE (inner2) == INTEGER_TYPE && TYPE_UNSIGNED (inner2))
	    return TYPE_PRECISION (inner1) + TYPE_PRECISION (inner2)
		   < TYPE_PRECISION (TREE_TYPE (t));
	}
      return 0;

    case BIT_AND_EXPR:
    case MAX_EXPR:
      return (tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					     strict_overflow_p)
	      || tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						strict_overflow_p));

    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case MIN_EXPR:
    case RDIV_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
      return (tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					     strict_overflow_p)
	      && tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						strict_overflow_p));

    case TRUNC_MOD_EXPR:
    case CEIL_MOD_EXPR:
    case FLOOR_MOD_EXPR:
    case ROUND_MOD_EXPR:
    case SAVE_EXPR:
    case NON_LVALUE_EXPR:
    case FLOAT_EXPR:
    case FIX_TRUNC_EXPR:
      return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					    strict_overflow_p);

    case COMPOUND_EXPR:
    case MODIFY_EXPR:
      return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
					    strict_overflow_p);

    case BIND_EXPR:
      return tree_expr_nonnegative_warnv_p (expr_last (TREE_OPERAND (t, 1)),
					    strict_overflow_p);

    case COND_EXPR:
      return (tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
					     strict_overflow_p)
	      && tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 2),
						strict_overflow_p));

    case NOP_EXPR:
      {
	tree inner_type = TREE_TYPE (TREE_OPERAND (t, 0));
	tree outer_type = TREE_TYPE (t);

	if (TREE_CODE (outer_type) == REAL_TYPE)
	  {
	    if (TREE_CODE (inner_type) == REAL_TYPE)
	      return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
						    strict_overflow_p);
	    if (TREE_CODE (inner_type) == INTEGER_TYPE)
	      {
		if (TYPE_UNSIGNED (inner_type))
		  return 1;
		return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
						      strict_overflow_p);
	      }
	  }
	else if (TREE_CODE (outer_type) == INTEGER_TYPE)
	  {
	    if (TREE_CODE (inner_type) == REAL_TYPE)
	      return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t,0),
						    strict_overflow_p);
	    if (TREE_CODE (inner_type) == INTEGER_TYPE)
	      return TYPE_PRECISION (inner_type) < TYPE_PRECISION (outer_type)
		      && TYPE_UNSIGNED (inner_type);
	  }
      }
      break;

    case TARGET_EXPR:
      {
	tree temp = TARGET_EXPR_SLOT (t);
	t = TARGET_EXPR_INITIAL (t);

	/* If the initializer is non-void, then it's a normal expression
	   that will be assigned to the slot.  */
	if (!VOID_TYPE_P (t))
	  return tree_expr_nonnegative_warnv_p (t, strict_overflow_p);

	/* Otherwise, the initializer sets the slot in some way.  One common
	   way is an assignment statement at the end of the initializer.  */
	while (1)
	  {
	    if (TREE_CODE (t) == BIND_EXPR)
	      t = expr_last (BIND_EXPR_BODY (t));
	    else if (TREE_CODE (t) == TRY_FINALLY_EXPR
		     || TREE_CODE (t) == TRY_CATCH_EXPR)
	      t = expr_last (TREE_OPERAND (t, 0));
	    else if (TREE_CODE (t) == STATEMENT_LIST)
	      t = expr_last (t);
	    else
	      break;
	  }
	if (TREE_CODE (t) == MODIFY_EXPR
	    && TREE_OPERAND (t, 0) == temp)
	  return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						strict_overflow_p);

	return 0;
      }

    case CALL_EXPR:
      {
	tree fndecl = get_callee_fndecl (t);
	tree arglist = TREE_OPERAND (t, 1);
	if (fndecl && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL)
	  switch (DECL_FUNCTION_CODE (fndecl))
	    {
	    CASE_FLT_FN (BUILT_IN_ACOS):
	    CASE_FLT_FN (BUILT_IN_ACOSH):
	    CASE_FLT_FN (BUILT_IN_CABS):
	    CASE_FLT_FN (BUILT_IN_COSH):
	    CASE_FLT_FN (BUILT_IN_ERFC):
	    CASE_FLT_FN (BUILT_IN_EXP):
	    CASE_FLT_FN (BUILT_IN_EXP10):
	    CASE_FLT_FN (BUILT_IN_EXP2):
	    CASE_FLT_FN (BUILT_IN_FABS):
	    CASE_FLT_FN (BUILT_IN_FDIM):
	    CASE_FLT_FN (BUILT_IN_HYPOT):
	    CASE_FLT_FN (BUILT_IN_POW10):
	    CASE_INT_FN (BUILT_IN_FFS):
	    CASE_INT_FN (BUILT_IN_PARITY):
	    CASE_INT_FN (BUILT_IN_POPCOUNT):
	    case BUILT_IN_BSWAP32:
	    case BUILT_IN_BSWAP64:
	      /* Always true.  */
	      return 1;

	    CASE_FLT_FN (BUILT_IN_SQRT):
	      /* sqrt(-0.0) is -0.0.  */
	      if (!HONOR_SIGNED_ZEROS (TYPE_MODE (TREE_TYPE (t))))
		return 1;
	      return tree_expr_nonnegative_warnv_p (TREE_VALUE (arglist),
						    strict_overflow_p);

	    CASE_FLT_FN (BUILT_IN_ASINH):
	    CASE_FLT_FN (BUILT_IN_ATAN):
	    CASE_FLT_FN (BUILT_IN_ATANH):
	    CASE_FLT_FN (BUILT_IN_CBRT):
	    CASE_FLT_FN (BUILT_IN_CEIL):
	    CASE_FLT_FN (BUILT_IN_ERF):
	    CASE_FLT_FN (BUILT_IN_EXPM1):
	    CASE_FLT_FN (BUILT_IN_FLOOR):
	    CASE_FLT_FN (BUILT_IN_FMOD):
	    CASE_FLT_FN (BUILT_IN_FREXP):
	    CASE_FLT_FN (BUILT_IN_LCEIL):
	    CASE_FLT_FN (BUILT_IN_LDEXP):
	    CASE_FLT_FN (BUILT_IN_LFLOOR):
	    CASE_FLT_FN (BUILT_IN_LLCEIL):
	    CASE_FLT_FN (BUILT_IN_LLFLOOR):
	    CASE_FLT_FN (BUILT_IN_LLRINT):
	    CASE_FLT_FN (BUILT_IN_LLROUND):
	    CASE_FLT_FN (BUILT_IN_LRINT):
	    CASE_FLT_FN (BUILT_IN_LROUND):
	    CASE_FLT_FN (BUILT_IN_MODF):
	    CASE_FLT_FN (BUILT_IN_NEARBYINT):
	    CASE_FLT_FN (BUILT_IN_POW):
	    CASE_FLT_FN (BUILT_IN_RINT):
	    CASE_FLT_FN (BUILT_IN_ROUND):
	    CASE_FLT_FN (BUILT_IN_SIGNBIT):
	    CASE_FLT_FN (BUILT_IN_SINH):
	    CASE_FLT_FN (BUILT_IN_TANH):
	    CASE_FLT_FN (BUILT_IN_TRUNC):
	      /* True if the 1st argument is nonnegative.  */
	      return tree_expr_nonnegative_warnv_p (TREE_VALUE (arglist),
						    strict_overflow_p);

	    CASE_FLT_FN (BUILT_IN_FMAX):
	      /* True if the 1st OR 2nd arguments are nonnegative.  */
	      return (tree_expr_nonnegative_warnv_p (TREE_VALUE (arglist),
						     strict_overflow_p)
		      || (tree_expr_nonnegative_warnv_p
			  (TREE_VALUE (TREE_CHAIN (arglist)),
			   strict_overflow_p)));

	    CASE_FLT_FN (BUILT_IN_FMIN):
	      /* True if the 1st AND 2nd arguments are nonnegative.  */
	      return (tree_expr_nonnegative_warnv_p (TREE_VALUE (arglist),
						     strict_overflow_p)
		      && (tree_expr_nonnegative_warnv_p
			  (TREE_VALUE (TREE_CHAIN (arglist)),
			   strict_overflow_p)));

	    CASE_FLT_FN (BUILT_IN_COPYSIGN):
	      /* True if the 2nd argument is nonnegative.  */
	      return (tree_expr_nonnegative_warnv_p
		      (TREE_VALUE (TREE_CHAIN (arglist)),
		       strict_overflow_p));

	    default:
	      break;
	    }
      }

      /* ... fall through ...  */

    default:
      {
	tree type = TREE_TYPE (t);
	if ((TYPE_PRECISION (type) != 1 || TYPE_UNSIGNED (type))
	    && truth_value_p (TREE_CODE (t)))
	  /* Truth values evaluate to 0 or 1, which is nonnegative unless we
             have a signed:1 type (where the value is -1 and 0).  */
	  return true;
      }
    }

  /* We don't know sign of `t', so be conservative and return false.  */
  return 0;
}

/* Return true if `t' is known to be non-negative.  Handle warnings
   about undefined signed overflow.  */

int
tree_expr_nonnegative_p (tree t)
{
  int ret;
  bool strict_overflow_p;

  strict_overflow_p = false;
  ret = tree_expr_nonnegative_warnv_p (t, &strict_overflow_p);
  if (strict_overflow_p)
    fold_overflow_warning (("assuming signed overflow does not occur when "
			    "determining that expression is always "
			    "non-negative"),
			   WARN_STRICT_OVERFLOW_MISC);
  return ret;
}

/* Return true when T is an address and is known to be nonzero.
   For floating point we further ensure that T is not denormal.
   Similar logic is present in nonzero_address in rtlanal.h.

   If the return value is based on the assumption that signed overflow
   is undefined, set *STRICT_OVERFLOW_P to true; otherwise, don't
   change *STRICT_OVERFLOW_P.  */

bool
tree_expr_nonzero_warnv_p (tree t, bool *strict_overflow_p)
{
  tree type = TREE_TYPE (t);
  bool sub_strict_overflow_p;

  /* Doing something useful for floating point would need more work.  */
  if (!INTEGRAL_TYPE_P (type) && !POINTER_TYPE_P (type))
    return false;

  switch (TREE_CODE (t))
    {
    case SSA_NAME:
      /* Query VRP to see if it has recorded any information about
	 the range of this object.  */
      return ssa_name_nonzero_p (t);

    case ABS_EXPR:
      return tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					strict_overflow_p);

    case INTEGER_CST:
      /* We used to test for !integer_zerop here.  This does not work correctly
	 if TREE_CONSTANT_OVERFLOW (t).  */
      return (TREE_INT_CST_LOW (t) != 0
	      || TREE_INT_CST_HIGH (t) != 0);

    case PLUS_EXPR:
      if (TYPE_OVERFLOW_UNDEFINED (type))
	{
	  /* With the presence of negative values it is hard
	     to say something.  */
	  sub_strict_overflow_p = false;
	  if (!tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					      &sub_strict_overflow_p)
	      || !tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						 &sub_strict_overflow_p))
	    return false;
	  /* One of operands must be positive and the other non-negative.  */
	  /* We don't set *STRICT_OVERFLOW_P here: even if this value
	     overflows, on a twos-complement machine the sum of two
	     nonnegative numbers can never be zero.  */
	  return (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					     strict_overflow_p)
	          || tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
						strict_overflow_p));
	}
      break;

    case MULT_EXPR:
      if (TYPE_OVERFLOW_UNDEFINED (type))
	{
	  if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					 strict_overflow_p)
	      && tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					    strict_overflow_p))
	    {
	      *strict_overflow_p = true;
	      return true;
	    }
	}
      break;

    case NOP_EXPR:
      {
	tree inner_type = TREE_TYPE (TREE_OPERAND (t, 0));
	tree outer_type = TREE_TYPE (t);

	return (TYPE_PRECISION (outer_type) >= TYPE_PRECISION (inner_type)
		&& tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					      strict_overflow_p));
      }
      break;

   case ADDR_EXPR:
      {
	tree base = get_base_address (TREE_OPERAND (t, 0));

	if (!base)
	  return false;

	/* Weak declarations may link to NULL.  */
	if (VAR_OR_FUNCTION_DECL_P (base))
	  return !DECL_WEAK (base);

	/* Constants are never weak.  */
	if (CONSTANT_CLASS_P (base))
	  return true;

	return false;
      }

    case COND_EXPR:
      sub_strict_overflow_p = false;
      if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
				     &sub_strict_overflow_p)
	  && tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 2),
					&sub_strict_overflow_p))
	{
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;
	  return true;
	}
      break;

    case MIN_EXPR:
      sub_strict_overflow_p = false;
      if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
				     &sub_strict_overflow_p)
	  && tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					&sub_strict_overflow_p))
	{
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;
	}
      break;

    case MAX_EXPR:
      sub_strict_overflow_p = false;
      if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
				     &sub_strict_overflow_p))
	{
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;

	  /* When both operands are nonzero, then MAX must be too.  */
	  if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					 strict_overflow_p))
	    return true;

	  /* MAX where operand 0 is positive is positive.  */
	  return tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 0),
					       strict_overflow_p);
	}
      /* MAX where operand 1 is positive is positive.  */
      else if (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					  &sub_strict_overflow_p)
	       && tree_expr_nonnegative_warnv_p (TREE_OPERAND (t, 1),
						 &sub_strict_overflow_p))
	{
	  if (sub_strict_overflow_p)
	    *strict_overflow_p = true;
	  return true;
	}
      break;

    case COMPOUND_EXPR:
    case MODIFY_EXPR:
    case BIND_EXPR:
      return tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					strict_overflow_p);

    case SAVE_EXPR:
    case NON_LVALUE_EXPR:
      return tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					strict_overflow_p);

    case BIT_IOR_EXPR:
      return (tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 1),
					strict_overflow_p)
	      || tree_expr_nonzero_warnv_p (TREE_OPERAND (t, 0),
					    strict_overflow_p));

    case CALL_EXPR:
      return alloca_call_p (t);

    default:
      break;
    }
  return false;
}

/* Return true when T is an address and is known to be nonzero.
   Handle warnings about undefined signed overflow.  */

bool
tree_expr_nonzero_p (tree t)
{
  bool ret, strict_overflow_p;

  strict_overflow_p = false;
  ret = tree_expr_nonzero_warnv_p (t, &strict_overflow_p);
  if (strict_overflow_p)
    fold_overflow_warning (("assuming signed overflow does not occur when "
			    "determining that expression is always "
			    "non-zero"),
			   WARN_STRICT_OVERFLOW_MISC);
  return ret;
}

/* Given the components of a binary expression CODE, TYPE, OP0 and OP1,
   attempt to fold the expression to a constant without modifying TYPE,
   OP0 or OP1.

   If the expression could be simplified to a constant, then return
   the constant.  If the expression would not be simplified to a
   constant, then return NULL_TREE.  */

tree
fold_binary_to_constant (enum tree_code code, tree type, tree op0, tree op1)
{
  tree tem = fold_binary (code, type, op0, op1);
  return (tem && TREE_CONSTANT (tem)) ? tem : NULL_TREE;
}

/* Given the components of a unary expression CODE, TYPE and OP0,
   attempt to fold the expression to a constant without modifying
   TYPE or OP0.

   If the expression could be simplified to a constant, then return
   the constant.  If the expression would not be simplified to a
   constant, then return NULL_TREE.  */

tree
fold_unary_to_constant (enum tree_code code, tree type, tree op0)
{
  tree tem = fold_unary (code, type, op0);
  return (tem && TREE_CONSTANT (tem)) ? tem : NULL_TREE;
}

/* If EXP represents referencing an element in a constant string
   (either via pointer arithmetic or array indexing), return the
   tree representing the value accessed, otherwise return NULL.  */

tree
fold_read_from_constant_string (tree exp)
{
  if ((TREE_CODE (exp) == INDIRECT_REF
       || TREE_CODE (exp) == ARRAY_REF)
      && TREE_CODE (TREE_TYPE (exp)) == INTEGER_TYPE)
    {
      tree exp1 = TREE_OPERAND (exp, 0);
      tree index;
      tree string;

      if (TREE_CODE (exp) == INDIRECT_REF)
	string = string_constant (exp1, &index);
      else
	{
	  tree low_bound = array_ref_low_bound (exp);
	  index = fold_convert (sizetype, TREE_OPERAND (exp, 1));

	  /* Optimize the special-case of a zero lower bound.

	     We convert the low_bound to sizetype to avoid some problems
	     with constant folding.  (E.g. suppose the lower bound is 1,
	     and its mode is QI.  Without the conversion,l (ARRAY
	     +(INDEX-(unsigned char)1)) becomes ((ARRAY+(-(unsigned char)1))
	     +INDEX), which becomes (ARRAY+255+INDEX).  Opps!)  */
	  if (! integer_zerop (low_bound))
	    index = size_diffop (index, fold_convert (sizetype, low_bound));

	  string = exp1;
	}

      if (string
	  && TYPE_MODE (TREE_TYPE (exp)) == TYPE_MODE (TREE_TYPE (TREE_TYPE (string)))
	  && TREE_CODE (string) == STRING_CST
	  && TREE_CODE (index) == INTEGER_CST
	  && compare_tree_int (index, TREE_STRING_LENGTH (string)) < 0
	  && (GET_MODE_CLASS (TYPE_MODE (TREE_TYPE (TREE_TYPE (string))))
	      == MODE_INT)
	  && (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (TREE_TYPE (string)))) == 1))
	return fold_convert (TREE_TYPE (exp),
			     build_int_cst (NULL_TREE,
					    (TREE_STRING_POINTER (string)
					     [TREE_INT_CST_LOW (index)])));
    }
  return NULL;
}

/* Return the tree for neg (ARG0) when ARG0 is known to be either
   an integer constant or real constant.

   TYPE is the type of the result.  */

static tree
fold_negate_const (tree arg0, tree type)
{
  tree t = NULL_TREE;

  switch (TREE_CODE (arg0))
    {
    case INTEGER_CST:
      {
	unsigned HOST_WIDE_INT low;
	HOST_WIDE_INT high;
	int overflow = neg_double (TREE_INT_CST_LOW (arg0),
				   TREE_INT_CST_HIGH (arg0),
				   &low, &high);
	t = build_int_cst_wide (type, low, high);
	t = force_fit_type (t, 1,
			    (overflow | TREE_OVERFLOW (arg0))
			    && !TYPE_UNSIGNED (type),
			    TREE_CONSTANT_OVERFLOW (arg0));
	break;
      }

    case REAL_CST:
      t = build_real (type, REAL_VALUE_NEGATE (TREE_REAL_CST (arg0)));
      break;

    default:
      gcc_unreachable ();
    }

  return t;
}

/* Return the tree for abs (ARG0) when ARG0 is known to be either
   an integer constant or real constant.

   TYPE is the type of the result.  */

tree
fold_abs_const (tree arg0, tree type)
{
  tree t = NULL_TREE;

  switch (TREE_CODE (arg0))
    {
    case INTEGER_CST:
      /* If the value is unsigned, then the absolute value is
	 the same as the ordinary value.  */
      if (TYPE_UNSIGNED (type))
	t = arg0;
      /* Similarly, if the value is non-negative.  */
      else if (INT_CST_LT (integer_minus_one_node, arg0))
	t = arg0;
      /* If the value is negative, then the absolute value is
	 its negation.  */
      else
	{
	  unsigned HOST_WIDE_INT low;
	  HOST_WIDE_INT high;
	  int overflow = neg_double (TREE_INT_CST_LOW (arg0),
				     TREE_INT_CST_HIGH (arg0),
				     &low, &high);
	  t = build_int_cst_wide (type, low, high);
	  t = force_fit_type (t, -1, overflow | TREE_OVERFLOW (arg0),
			      TREE_CONSTANT_OVERFLOW (arg0));
	}
      break;

    case REAL_CST:
      if (REAL_VALUE_NEGATIVE (TREE_REAL_CST (arg0)))
	t = build_real (type, REAL_VALUE_NEGATE (TREE_REAL_CST (arg0)));
      else
	t =  arg0;
      break;

    default:
      gcc_unreachable ();
    }

  return t;
}

/* Return the tree for not (ARG0) when ARG0 is known to be an integer
   constant.  TYPE is the type of the result.  */

static tree
fold_not_const (tree arg0, tree type)
{
  tree t = NULL_TREE;

  gcc_assert (TREE_CODE (arg0) == INTEGER_CST);

  t = build_int_cst_wide (type,
			  ~ TREE_INT_CST_LOW (arg0),
			  ~ TREE_INT_CST_HIGH (arg0));
  t = force_fit_type (t, 0, TREE_OVERFLOW (arg0),
		      TREE_CONSTANT_OVERFLOW (arg0));

  return t;
}

/* Given CODE, a relational operator, the target type, TYPE and two
   constant operands OP0 and OP1, return the result of the
   relational operation.  If the result is not a compile time
   constant, then return NULL_TREE.  */

static tree
fold_relational_const (enum tree_code code, tree type, tree op0, tree op1)
{
  int result, invert;

  /* From here on, the only cases we handle are when the result is
     known to be a constant.  */

  if (TREE_CODE (op0) == REAL_CST && TREE_CODE (op1) == REAL_CST)
    {
      const REAL_VALUE_TYPE *c0 = TREE_REAL_CST_PTR (op0);
      const REAL_VALUE_TYPE *c1 = TREE_REAL_CST_PTR (op1);

      /* Handle the cases where either operand is a NaN.  */
      if (real_isnan (c0) || real_isnan (c1))
	{
	  switch (code)
	    {
	    case EQ_EXPR:
	    case ORDERED_EXPR:
	      result = 0;
	      break;

	    case NE_EXPR:
	    case UNORDERED_EXPR:
	    case UNLT_EXPR:
	    case UNLE_EXPR:
	    case UNGT_EXPR:
	    case UNGE_EXPR:
	    case UNEQ_EXPR:
              result = 1;
	      break;

	    case LT_EXPR:
	    case LE_EXPR:
	    case GT_EXPR:
	    case GE_EXPR:
	    case LTGT_EXPR:
	      if (flag_trapping_math)
		return NULL_TREE;
	      result = 0;
	      break;

	    default:
	      gcc_unreachable ();
	    }

	  return constant_boolean_node (result, type);
	}

      return constant_boolean_node (real_compare (code, c0, c1), type);
    }

  /* Handle equality/inequality of complex constants.  */
  if (TREE_CODE (op0) == COMPLEX_CST && TREE_CODE (op1) == COMPLEX_CST)
    {
      tree rcond = fold_relational_const (code, type,
					  TREE_REALPART (op0),
					  TREE_REALPART (op1));
      tree icond = fold_relational_const (code, type,
					  TREE_IMAGPART (op0),
					  TREE_IMAGPART (op1));
      if (code == EQ_EXPR)
	return fold_build2 (TRUTH_ANDIF_EXPR, type, rcond, icond);
      else if (code == NE_EXPR)
	return fold_build2 (TRUTH_ORIF_EXPR, type, rcond, icond);
      else
	return NULL_TREE;
    }

  /* From here on we only handle LT, LE, GT, GE, EQ and NE.

     To compute GT, swap the arguments and do LT.
     To compute GE, do LT and invert the result.
     To compute LE, swap the arguments, do LT and invert the result.
     To compute NE, do EQ and invert the result.

     Therefore, the code below must handle only EQ and LT.  */

  if (code == LE_EXPR || code == GT_EXPR)
    {
      tree tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_tree_comparison (code);
    }

  /* Note that it is safe to invert for real values here because we
     have already handled the one case that it matters.  */

  invert = 0;
  if (code == NE_EXPR || code == GE_EXPR)
    {
      invert = 1;
      code = invert_tree_comparison (code, false);
    }

  /* Compute a result for LT or EQ if args permit;
     Otherwise return T.  */
  if (TREE_CODE (op0) == INTEGER_CST && TREE_CODE (op1) == INTEGER_CST)
    {
      if (code == EQ_EXPR)
	result = tree_int_cst_equal (op0, op1);
      else if (TYPE_UNSIGNED (TREE_TYPE (op0)))
	result = INT_CST_LT_UNSIGNED (op0, op1);
      else
	result = INT_CST_LT (op0, op1);
    }
  else
    return NULL_TREE;

  if (invert)
    result ^= 1;
  return constant_boolean_node (result, type);
}

/* Build an expression for the a clean point containing EXPR with type TYPE.
   Don't build a cleanup point expression for EXPR which don't have side
   effects.  */

tree
fold_build_cleanup_point_expr (tree type, tree expr)
{
  /* If the expression does not have side effects then we don't have to wrap
     it with a cleanup point expression.  */
  if (!TREE_SIDE_EFFECTS (expr))
    return expr;

  /* If the expression is a return, check to see if the expression inside the
     return has no side effects or the right hand side of the modify expression
     inside the return. If either don't have side effects set we don't need to
     wrap the expression in a cleanup point expression.  Note we don't check the
     left hand side of the modify because it should always be a return decl.  */
  if (TREE_CODE (expr) == RETURN_EXPR)
    {
      tree op = TREE_OPERAND (expr, 0);
      if (!op || !TREE_SIDE_EFFECTS (op))
        return expr;
      op = TREE_OPERAND (op, 1);
      if (!TREE_SIDE_EFFECTS (op))
        return expr;
    }
  
  return build1 (CLEANUP_POINT_EXPR, type, expr);
}

/* Build an expression for the address of T.  Folds away INDIRECT_REF to
   avoid confusing the gimplify process.  */

tree
build_fold_addr_expr_with_type (tree t, tree ptrtype)
{
  /* The size of the object is not relevant when talking about its address.  */
  if (TREE_CODE (t) == WITH_SIZE_EXPR)
    t = TREE_OPERAND (t, 0);

  /* Note: doesn't apply to ALIGN_INDIRECT_REF */
  if (TREE_CODE (t) == INDIRECT_REF
      || TREE_CODE (t) == MISALIGNED_INDIRECT_REF)
    {
      t = TREE_OPERAND (t, 0);
      if (TREE_TYPE (t) != ptrtype)
	t = build1 (NOP_EXPR, ptrtype, t);
    }
  else
    {
      tree base = t;

      while (handled_component_p (base))
	base = TREE_OPERAND (base, 0);
      if (DECL_P (base))
	TREE_ADDRESSABLE (base) = 1;

      t = build1 (ADDR_EXPR, ptrtype, t);
    }

  return t;
}

tree
build_fold_addr_expr (tree t)
{
  return build_fold_addr_expr_with_type (t, build_pointer_type (TREE_TYPE (t)));
}

/* Given a pointer value OP0 and a type TYPE, return a simplified version
   of an indirection through OP0, or NULL_TREE if no simplification is
   possible.  */

tree
fold_indirect_ref_1 (tree type, tree op0)
{
  tree sub = op0;
  tree subtype;

  STRIP_NOPS (sub);
  subtype = TREE_TYPE (sub);
  if (!POINTER_TYPE_P (subtype))
    return NULL_TREE;

  if (TREE_CODE (sub) == ADDR_EXPR)
    {
      tree op = TREE_OPERAND (sub, 0);
      tree optype = TREE_TYPE (op);
      /* *&CONST_DECL -> to the value of the const decl.  */
      if (TREE_CODE (op) == CONST_DECL)
	return DECL_INITIAL (op);
      /* *&p => p;  make sure to handle *&"str"[cst] here.  */
      if (type == optype)
	{
	  tree fop = fold_read_from_constant_string (op);
	  if (fop)
	    return fop;
	  else
	    return op;
	}
      /* *(foo *)&fooarray => fooarray[0] */
      else if (TREE_CODE (optype) == ARRAY_TYPE
	       && type == TREE_TYPE (optype))
	{
	  tree type_domain = TYPE_DOMAIN (optype);
	  tree min_val = size_zero_node;
	  if (type_domain && TYPE_MIN_VALUE (type_domain))
	    min_val = TYPE_MIN_VALUE (type_domain);
	  return build4 (ARRAY_REF, type, op, min_val, NULL_TREE, NULL_TREE);
	}
      /* *(foo *)&complexfoo => __real__ complexfoo */
      else if (TREE_CODE (optype) == COMPLEX_TYPE
	       && type == TREE_TYPE (optype))
	return fold_build1 (REALPART_EXPR, type, op);
    }

  /* ((foo*)&complexfoo)[1] => __imag__ complexfoo */
  if (TREE_CODE (sub) == PLUS_EXPR
      && TREE_CODE (TREE_OPERAND (sub, 1)) == INTEGER_CST)
    {
      tree op00 = TREE_OPERAND (sub, 0);
      tree op01 = TREE_OPERAND (sub, 1);
      tree op00type;

      STRIP_NOPS (op00);
      op00type = TREE_TYPE (op00);
      if (TREE_CODE (op00) == ADDR_EXPR
 	  && TREE_CODE (TREE_TYPE (op00type)) == COMPLEX_TYPE
	  && type == TREE_TYPE (TREE_TYPE (op00type)))
	{
	  tree size = TYPE_SIZE_UNIT (type);
	  if (tree_int_cst_equal (size, op01))
	    return fold_build1 (IMAGPART_EXPR, type, TREE_OPERAND (op00, 0));
	}
    }
  
  /* *(foo *)fooarrptr => (*fooarrptr)[0] */
  if (TREE_CODE (TREE_TYPE (subtype)) == ARRAY_TYPE
      && type == TREE_TYPE (TREE_TYPE (subtype)))
    {
      tree type_domain;
      tree min_val = size_zero_node;
      sub = build_fold_indirect_ref (sub);
      type_domain = TYPE_DOMAIN (TREE_TYPE (sub));
      if (type_domain && TYPE_MIN_VALUE (type_domain))
	min_val = TYPE_MIN_VALUE (type_domain);
      return build4 (ARRAY_REF, type, sub, min_val, NULL_TREE, NULL_TREE);
    }

  return NULL_TREE;
}

/* Builds an expression for an indirection through T, simplifying some
   cases.  */

tree
build_fold_indirect_ref (tree t)
{
  tree type = TREE_TYPE (TREE_TYPE (t));
  tree sub = fold_indirect_ref_1 (type, t);

  if (sub)
    return sub;
  else
    return build1 (INDIRECT_REF, type, t);
}

/* Given an INDIRECT_REF T, return either T or a simplified version.  */

tree
fold_indirect_ref (tree t)
{
  tree sub = fold_indirect_ref_1 (TREE_TYPE (t), TREE_OPERAND (t, 0));

  if (sub)
    return sub;
  else
    return t;
}

/* Strip non-trapping, non-side-effecting tree nodes from an expression
   whose result is ignored.  The type of the returned tree need not be
   the same as the original expression.  */

tree
fold_ignored_result (tree t)
{
  if (!TREE_SIDE_EFFECTS (t))
    return integer_zero_node;

  for (;;)
    switch (TREE_CODE_CLASS (TREE_CODE (t)))
      {
      case tcc_unary:
	t = TREE_OPERAND (t, 0);
	break;

      case tcc_binary:
      case tcc_comparison:
	if (!TREE_SIDE_EFFECTS (TREE_OPERAND (t, 1)))
	  t = TREE_OPERAND (t, 0);
	else if (!TREE_SIDE_EFFECTS (TREE_OPERAND (t, 0)))
	  t = TREE_OPERAND (t, 1);
	else
	  return t;
	break;

      case tcc_expression:
	switch (TREE_CODE (t))
	  {
	  case COMPOUND_EXPR:
	    if (TREE_SIDE_EFFECTS (TREE_OPERAND (t, 1)))
	      return t;
	    t = TREE_OPERAND (t, 0);
	    break;

	  case COND_EXPR:
	    if (TREE_SIDE_EFFECTS (TREE_OPERAND (t, 1))
		|| TREE_SIDE_EFFECTS (TREE_OPERAND (t, 2)))
	      return t;
	    t = TREE_OPERAND (t, 0);
	    break;

	  default:
	    return t;
	  }
	break;

      default:
	return t;
      }
}

/* Return the value of VALUE, rounded up to a multiple of DIVISOR.
   This can only be applied to objects of a sizetype.  */

tree
round_up (tree value, int divisor)
{
  tree div = NULL_TREE;

  gcc_assert (divisor > 0);
  if (divisor == 1)
    return value;

  /* See if VALUE is already a multiple of DIVISOR.  If so, we don't
     have to do anything.  Only do this when we are not given a const,
     because in that case, this check is more expensive than just
     doing it.  */
  if (TREE_CODE (value) != INTEGER_CST)
    {
      div = build_int_cst (TREE_TYPE (value), divisor);

      if (multiple_of_p (TREE_TYPE (value), value, div))
	return value;
    }

  /* If divisor is a power of two, simplify this to bit manipulation.  */
  if (divisor == (divisor & -divisor))
    {
      tree t;

      t = build_int_cst (TREE_TYPE (value), divisor - 1);
      value = size_binop (PLUS_EXPR, value, t);
      t = build_int_cst (TREE_TYPE (value), -divisor);
      value = size_binop (BIT_AND_EXPR, value, t);
    }
  else
    {
      if (!div)
	div = build_int_cst (TREE_TYPE (value), divisor);
      value = size_binop (CEIL_DIV_EXPR, value, div);
      value = size_binop (MULT_EXPR, value, div);
    }

  return value;
}

/* Likewise, but round down.  */

tree
round_down (tree value, int divisor)
{
  tree div = NULL_TREE;

  gcc_assert (divisor > 0);
  if (divisor == 1)
    return value;

  /* See if VALUE is already a multiple of DIVISOR.  If so, we don't
     have to do anything.  Only do this when we are not given a const,
     because in that case, this check is more expensive than just
     doing it.  */
  if (TREE_CODE (value) != INTEGER_CST)
    {
      div = build_int_cst (TREE_TYPE (value), divisor);

      if (multiple_of_p (TREE_TYPE (value), value, div))
	return value;
    }

  /* If divisor is a power of two, simplify this to bit manipulation.  */
  if (divisor == (divisor & -divisor))
    {
      tree t;

      t = build_int_cst (TREE_TYPE (value), -divisor);
      value = size_binop (BIT_AND_EXPR, value, t);
    }
  else
    {
      if (!div)
	div = build_int_cst (TREE_TYPE (value), divisor);
      value = size_binop (FLOOR_DIV_EXPR, value, div);
      value = size_binop (MULT_EXPR, value, div);
    }

  return value;
}

/* Returns the pointer to the base of the object addressed by EXP and
   extracts the information about the offset of the access, storing it
   to PBITPOS and POFFSET.  */

static tree
split_address_to_core_and_offset (tree exp,
				  HOST_WIDE_INT *pbitpos, tree *poffset)
{
  tree core;
  enum machine_mode mode;
  int unsignedp, volatilep;
  HOST_WIDE_INT bitsize;

  if (TREE_CODE (exp) == ADDR_EXPR)
    {
      core = get_inner_reference (TREE_OPERAND (exp, 0), &bitsize, pbitpos,
				  poffset, &mode, &unsignedp, &volatilep,
				  false);
      core = build_fold_addr_expr (core);
    }
  else
    {
      core = exp;
      *pbitpos = 0;
      *poffset = NULL_TREE;
    }

  return core;
}

/* Returns true if addresses of E1 and E2 differ by a constant, false
   otherwise.  If they do, E1 - E2 is stored in *DIFF.  */

bool
ptr_difference_const (tree e1, tree e2, HOST_WIDE_INT *diff)
{
  tree core1, core2;
  HOST_WIDE_INT bitpos1, bitpos2;
  tree toffset1, toffset2, tdiff, type;

  core1 = split_address_to_core_and_offset (e1, &bitpos1, &toffset1);
  core2 = split_address_to_core_and_offset (e2, &bitpos2, &toffset2);

  if (bitpos1 % BITS_PER_UNIT != 0
      || bitpos2 % BITS_PER_UNIT != 0
      || !operand_equal_p (core1, core2, 0))
    return false;

  if (toffset1 && toffset2)
    {
      type = TREE_TYPE (toffset1);
      if (type != TREE_TYPE (toffset2))
	toffset2 = fold_convert (type, toffset2);

      tdiff = fold_build2 (MINUS_EXPR, type, toffset1, toffset2);
      if (!cst_and_fits_in_hwi (tdiff))
	return false;

      *diff = int_cst_value (tdiff);
    }
  else if (toffset1 || toffset2)
    {
      /* If only one of the offsets is non-constant, the difference cannot
	 be a constant.  */
      return false;
    }
  else
    *diff = 0;

  *diff += (bitpos1 - bitpos2) / BITS_PER_UNIT;
  return true;
}

/* Simplify the floating point expression EXP when the sign of the
   result is not significant.  Return NULL_TREE if no simplification
   is possible.  */

tree
fold_strip_sign_ops (tree exp)
{
  tree arg0, arg1;

  switch (TREE_CODE (exp))
    {
    case ABS_EXPR:
    case NEGATE_EXPR:
      arg0 = fold_strip_sign_ops (TREE_OPERAND (exp, 0));
      return arg0 ? arg0 : TREE_OPERAND (exp, 0);

    case MULT_EXPR:
    case RDIV_EXPR:
      if (HONOR_SIGN_DEPENDENT_ROUNDING (TYPE_MODE (TREE_TYPE (exp))))
	return NULL_TREE;
      arg0 = fold_strip_sign_ops (TREE_OPERAND (exp, 0));
      arg1 = fold_strip_sign_ops (TREE_OPERAND (exp, 1));
      if (arg0 != NULL_TREE || arg1 != NULL_TREE)
	return fold_build2 (TREE_CODE (exp), TREE_TYPE (exp),
			    arg0 ? arg0 : TREE_OPERAND (exp, 0),
			    arg1 ? arg1 : TREE_OPERAND (exp, 1));
      break;

    default:
      break;
    }
  return NULL_TREE;
}

