/* Functions to determine/estimate number of iterations of a loop.
   Copyright (C) 2004, 2005, 2006, 2007 Free Software Foundation, Inc.
   
This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.
   
GCC is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
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
#include "rtl.h"
#include "tm_p.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "output.h"
#include "diagnostic.h"
#include "intl.h"
#include "tree-flow.h"
#include "tree-dump.h"
#include "cfgloop.h"
#include "tree-pass.h"
#include "ggc.h"
#include "tree-chrec.h"
#include "tree-scalar-evolution.h"
#include "tree-data-ref.h"
#include "params.h"
#include "flags.h"
#include "toplev.h"
#include "tree-inline.h"

#define SWAP(X, Y) do { void *tmp = (X); (X) = (Y); (Y) = tmp; } while (0)


/*

   Analysis of number of iterations of an affine exit test.

*/

/* Returns true if ARG is either NULL_TREE or constant zero.  Unlike
   integer_zerop, it does not care about overflow flags.  */

bool
zero_p (tree arg)
{
  if (!arg)
    return true;

  if (TREE_CODE (arg) != INTEGER_CST)
    return false;

  return (TREE_INT_CST_LOW (arg) == 0 && TREE_INT_CST_HIGH (arg) == 0);
}

/* Returns true if ARG a nonzero constant.  Unlike integer_nonzerop, it does
   not care about overflow flags.  */

static bool
nonzero_p (tree arg)
{
  if (!arg)
    return false;

  if (TREE_CODE (arg) != INTEGER_CST)
    return false;

  return (TREE_INT_CST_LOW (arg) != 0 || TREE_INT_CST_HIGH (arg) != 0);
}

/* Returns inverse of X modulo 2^s, where MASK = 2^s-1.  */

static tree
inverse (tree x, tree mask)
{
  tree type = TREE_TYPE (x);
  tree rslt;
  unsigned ctr = tree_floor_log2 (mask);

  if (TYPE_PRECISION (type) <= HOST_BITS_PER_WIDE_INT)
    {
      unsigned HOST_WIDE_INT ix;
      unsigned HOST_WIDE_INT imask;
      unsigned HOST_WIDE_INT irslt = 1;

      gcc_assert (cst_and_fits_in_hwi (x));
      gcc_assert (cst_and_fits_in_hwi (mask));

      ix = int_cst_value (x);
      imask = int_cst_value (mask);

      for (; ctr; ctr--)
	{
	  irslt *= ix;
	  ix *= ix;
	}
      irslt &= imask;

      rslt = build_int_cst_type (type, irslt);
    }
  else
    {
      rslt = build_int_cst (type, 1);
      for (; ctr; ctr--)
	{
	  rslt = int_const_binop (MULT_EXPR, rslt, x, 0);
	  x = int_const_binop (MULT_EXPR, x, x, 0);
	}
      rslt = int_const_binop (BIT_AND_EXPR, rslt, mask, 0);
    }

  return rslt;
}

/* Determines number of iterations of loop whose ending condition
   is IV <> FINAL.  TYPE is the type of the iv.  The number of
   iterations is stored to NITER.  NEVER_INFINITE is true if
   we know that the exit must be taken eventually, i.e., that the IV
   ever reaches the value FINAL (we derived this earlier, and possibly set
   NITER->assumptions to make sure this is the case).  */

static bool
number_of_iterations_ne (tree type, affine_iv *iv, tree final,
			 struct tree_niter_desc *niter, bool never_infinite)
{
  tree niter_type = unsigned_type_for (type);
  tree s, c, d, bits, assumption, tmp, bound;

  niter->control = *iv;
  niter->bound = final;
  niter->cmp = NE_EXPR;

  /* Rearrange the terms so that we get inequality s * i <> c, with s
     positive.  Also cast everything to the unsigned type.  */
  if (tree_int_cst_sign_bit (iv->step))
    {
      s = fold_convert (niter_type,
			fold_build1 (NEGATE_EXPR, type, iv->step));
      c = fold_build2 (MINUS_EXPR, niter_type,
		       fold_convert (niter_type, iv->base),
		       fold_convert (niter_type, final));
    }
  else
    {
      s = fold_convert (niter_type, iv->step);
      c = fold_build2 (MINUS_EXPR, niter_type,
		       fold_convert (niter_type, final),
		       fold_convert (niter_type, iv->base));
    }

  /* First the trivial cases -- when the step is 1.  */
  if (integer_onep (s))
    {
      niter->niter = c;
      return true;
    }

  /* Let nsd (step, size of mode) = d.  If d does not divide c, the loop
     is infinite.  Otherwise, the number of iterations is
     (inverse(s/d) * (c/d)) mod (size of mode/d).  */
  bits = num_ending_zeros (s);
  bound = build_low_bits_mask (niter_type,
			       (TYPE_PRECISION (niter_type)
				- tree_low_cst (bits, 1)));

  d = fold_binary_to_constant (LSHIFT_EXPR, niter_type,
			       build_int_cst (niter_type, 1), bits);
  s = fold_binary_to_constant (RSHIFT_EXPR, niter_type, s, bits);

  if (!never_infinite)
    {
      /* If we cannot assume that the loop is not infinite, record the
	 assumptions for divisibility of c.  */
      assumption = fold_build2 (FLOOR_MOD_EXPR, niter_type, c, d);
      assumption = fold_build2 (EQ_EXPR, boolean_type_node,
				assumption, build_int_cst (niter_type, 0));
      if (!nonzero_p (assumption))
	niter->assumptions = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
					  niter->assumptions, assumption);
    }
      
  c = fold_build2 (EXACT_DIV_EXPR, niter_type, c, d);
  tmp = fold_build2 (MULT_EXPR, niter_type, c, inverse (s, bound));
  niter->niter = fold_build2 (BIT_AND_EXPR, niter_type, tmp, bound);
  return true;
}

/* Checks whether we can determine the final value of the control variable
   of the loop with ending condition IV0 < IV1 (computed in TYPE).
   DELTA is the difference IV1->base - IV0->base, STEP is the absolute value
   of the step.  The assumptions necessary to ensure that the computation
   of the final value does not overflow are recorded in NITER.  If we
   find the final value, we adjust DELTA and return TRUE.  Otherwise
   we return false.  */

static bool
number_of_iterations_lt_to_ne (tree type, affine_iv *iv0, affine_iv *iv1,
			       struct tree_niter_desc *niter,
			       tree *delta, tree step)
{
  tree niter_type = TREE_TYPE (step);
  tree mod = fold_build2 (FLOOR_MOD_EXPR, niter_type, *delta, step);
  tree tmod;
  tree assumption = boolean_true_node, bound, noloop;

  if (TREE_CODE (mod) != INTEGER_CST)
    return false;
  if (nonzero_p (mod))
    mod = fold_build2 (MINUS_EXPR, niter_type, step, mod);
  tmod = fold_convert (type, mod);

  if (nonzero_p (iv0->step))
    {
      /* The final value of the iv is iv1->base + MOD, assuming that this
	 computation does not overflow, and that
	 iv0->base <= iv1->base + MOD.  */
      if (!iv1->no_overflow && !zero_p (mod))
	{
	  bound = fold_build2 (MINUS_EXPR, type,
			       TYPE_MAX_VALUE (type), tmod);
	  assumption = fold_build2 (LE_EXPR, boolean_type_node,
				    iv1->base, bound);
	  if (zero_p (assumption))
	    return false;
	}
      noloop = fold_build2 (GT_EXPR, boolean_type_node,
			    iv0->base,
			    fold_build2 (PLUS_EXPR, type,
					 iv1->base, tmod));
    }
  else
    {
      /* The final value of the iv is iv0->base - MOD, assuming that this
	 computation does not overflow, and that
	 iv0->base - MOD <= iv1->base. */
      if (!iv0->no_overflow && !zero_p (mod))
	{
	  bound = fold_build2 (PLUS_EXPR, type,
			       TYPE_MIN_VALUE (type), tmod);
	  assumption = fold_build2 (GE_EXPR, boolean_type_node,
				    iv0->base, bound);
	  if (zero_p (assumption))
	    return false;
	}
      noloop = fold_build2 (GT_EXPR, boolean_type_node,
			    fold_build2 (MINUS_EXPR, type,
					 iv0->base, tmod),
			    iv1->base);
    }

  if (!nonzero_p (assumption))
    niter->assumptions = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
				      niter->assumptions,
				      assumption);
  if (!zero_p (noloop))
    niter->may_be_zero = fold_build2 (TRUTH_OR_EXPR, boolean_type_node,
				      niter->may_be_zero,
				      noloop);
  *delta = fold_build2 (PLUS_EXPR, niter_type, *delta, mod);
  return true;
}

/* Add assertions to NITER that ensure that the control variable of the loop
   with ending condition IV0 < IV1 does not overflow.  Types of IV0 and IV1
   are TYPE.  Returns false if we can prove that there is an overflow, true
   otherwise.  STEP is the absolute value of the step.  */

static bool
assert_no_overflow_lt (tree type, affine_iv *iv0, affine_iv *iv1,
		       struct tree_niter_desc *niter, tree step)
{
  tree bound, d, assumption, diff;
  tree niter_type = TREE_TYPE (step);

  if (nonzero_p (iv0->step))
    {
      /* for (i = iv0->base; i < iv1->base; i += iv0->step) */
      if (iv0->no_overflow)
	return true;

      /* If iv0->base is a constant, we can determine the last value before
	 overflow precisely; otherwise we conservatively assume
	 MAX - STEP + 1.  */

      if (TREE_CODE (iv0->base) == INTEGER_CST)
	{
	  d = fold_build2 (MINUS_EXPR, niter_type,
			   fold_convert (niter_type, TYPE_MAX_VALUE (type)),
			   fold_convert (niter_type, iv0->base));
	  diff = fold_build2 (FLOOR_MOD_EXPR, niter_type, d, step);
	}
      else
	diff = fold_build2 (MINUS_EXPR, niter_type, step,
			    build_int_cst (niter_type, 1));
      bound = fold_build2 (MINUS_EXPR, type,
			   TYPE_MAX_VALUE (type), fold_convert (type, diff));
      assumption = fold_build2 (LE_EXPR, boolean_type_node,
				iv1->base, bound);
    }
  else
    {
      /* for (i = iv1->base; i > iv0->base; i += iv1->step) */
      if (iv1->no_overflow)
	return true;

      if (TREE_CODE (iv1->base) == INTEGER_CST)
	{
	  d = fold_build2 (MINUS_EXPR, niter_type,
			   fold_convert (niter_type, iv1->base),
			   fold_convert (niter_type, TYPE_MIN_VALUE (type)));
	  diff = fold_build2 (FLOOR_MOD_EXPR, niter_type, d, step);
	}
      else
	diff = fold_build2 (MINUS_EXPR, niter_type, step,
			    build_int_cst (niter_type, 1));
      bound = fold_build2 (PLUS_EXPR, type,
			   TYPE_MIN_VALUE (type), fold_convert (type, diff));
      assumption = fold_build2 (GE_EXPR, boolean_type_node,
				iv0->base, bound);
    }

  if (zero_p (assumption))
    return false;
  if (!nonzero_p (assumption))
    niter->assumptions = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
				      niter->assumptions, assumption);
    
  iv0->no_overflow = true;
  iv1->no_overflow = true;
  return true;
}

/* Add an assumption to NITER that a loop whose ending condition
   is IV0 < IV1 rolls.  TYPE is the type of the control iv.  */

static void
assert_loop_rolls_lt (tree type, affine_iv *iv0, affine_iv *iv1,
		      struct tree_niter_desc *niter)
{
  tree assumption = boolean_true_node, bound, diff;
  tree mbz, mbzl, mbzr;

  if (nonzero_p (iv0->step))
    {
      diff = fold_build2 (MINUS_EXPR, type,
			  iv0->step, build_int_cst (type, 1));

      /* We need to know that iv0->base >= MIN + iv0->step - 1.  Since
	 0 address never belongs to any object, we can assume this for
	 pointers.  */
      if (!POINTER_TYPE_P (type))
	{
	  bound = fold_build2 (PLUS_EXPR, type,
			       TYPE_MIN_VALUE (type), diff);
	  assumption = fold_build2 (GE_EXPR, boolean_type_node,
				    iv0->base, bound);
	}

      /* And then we can compute iv0->base - diff, and compare it with
	 iv1->base.  */      
      mbzl = fold_build2 (MINUS_EXPR, type, iv0->base, diff);
      mbzr = iv1->base;
    }
  else
    {
      diff = fold_build2 (PLUS_EXPR, type,
			  iv1->step, build_int_cst (type, 1));

      if (!POINTER_TYPE_P (type))
	{
	  bound = fold_build2 (PLUS_EXPR, type,
			       TYPE_MAX_VALUE (type), diff);
	  assumption = fold_build2 (LE_EXPR, boolean_type_node,
				    iv1->base, bound);
	}

      mbzl = iv0->base;
      mbzr = fold_build2 (MINUS_EXPR, type, iv1->base, diff);
    }

  mbz = fold_build2 (GT_EXPR, boolean_type_node, mbzl, mbzr);

  if (!nonzero_p (assumption))
    niter->assumptions = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
				      niter->assumptions, assumption);
  if (!zero_p (mbz))
    niter->may_be_zero = fold_build2 (TRUTH_OR_EXPR, boolean_type_node,
				      niter->may_be_zero, mbz);
}

/* Determines number of iterations of loop whose ending condition
   is IV0 < IV1.  TYPE is the type of the iv.  The number of
   iterations is stored to NITER.  */

static bool
number_of_iterations_lt (tree type, affine_iv *iv0, affine_iv *iv1,
			 struct tree_niter_desc *niter,
			 bool never_infinite ATTRIBUTE_UNUSED)
{
  tree niter_type = unsigned_type_for (type);
  tree delta, step, s;

  if (nonzero_p (iv0->step))
    {
      niter->control = *iv0;
      niter->cmp = LT_EXPR;
      niter->bound = iv1->base;
    }
  else
    {
      niter->control = *iv1;
      niter->cmp = GT_EXPR;
      niter->bound = iv0->base;
    }

  delta = fold_build2 (MINUS_EXPR, niter_type,
		       fold_convert (niter_type, iv1->base),
		       fold_convert (niter_type, iv0->base));

  /* First handle the special case that the step is +-1.  */
  if ((iv0->step && integer_onep (iv0->step)
       && zero_p (iv1->step))
      || (iv1->step && integer_all_onesp (iv1->step)
	  && zero_p (iv0->step)))
    {
      /* for (i = iv0->base; i < iv1->base; i++)

	 or

	 for (i = iv1->base; i > iv0->base; i--).
	     
	 In both cases # of iterations is iv1->base - iv0->base, assuming that
	 iv1->base >= iv0->base.  */
      niter->may_be_zero = fold_build2 (LT_EXPR, boolean_type_node,
					iv1->base, iv0->base);
      niter->niter = delta;
      return true;
    }

  if (nonzero_p (iv0->step))
    step = fold_convert (niter_type, iv0->step);
  else
    step = fold_convert (niter_type,
			 fold_build1 (NEGATE_EXPR, type, iv1->step));

  /* If we can determine the final value of the control iv exactly, we can
     transform the condition to != comparison.  In particular, this will be
     the case if DELTA is constant.  */
  if (number_of_iterations_lt_to_ne (type, iv0, iv1, niter, &delta, step))
    {
      affine_iv zps;

      zps.base = build_int_cst (niter_type, 0);
      zps.step = step;
      /* number_of_iterations_lt_to_ne will add assumptions that ensure that
	 zps does not overflow.  */
      zps.no_overflow = true;

      return number_of_iterations_ne (type, &zps, delta, niter, true);
    }

  /* Make sure that the control iv does not overflow.  */
  if (!assert_no_overflow_lt (type, iv0, iv1, niter, step))
    return false;

  /* We determine the number of iterations as (delta + step - 1) / step.  For
     this to work, we must know that iv1->base >= iv0->base - step + 1,
     otherwise the loop does not roll.  */
  assert_loop_rolls_lt (type, iv0, iv1, niter);

  s = fold_build2 (MINUS_EXPR, niter_type,
		   step, build_int_cst (niter_type, 1));
  delta = fold_build2 (PLUS_EXPR, niter_type, delta, s);
  niter->niter = fold_build2 (FLOOR_DIV_EXPR, niter_type, delta, step);
  return true;
}

/* Determines number of iterations of loop whose ending condition
   is IV0 <= IV1.  TYPE is the type of the iv.  The number of
   iterations is stored to NITER.  NEVER_INFINITE is true if
   we know that this condition must eventually become false (we derived this
   earlier, and possibly set NITER->assumptions to make sure this
   is the case).  */

static bool
number_of_iterations_le (tree type, affine_iv *iv0, affine_iv *iv1,
			 struct tree_niter_desc *niter, bool never_infinite)
{
  tree assumption;

  /* Say that IV0 is the control variable.  Then IV0 <= IV1 iff
     IV0 < IV1 + 1, assuming that IV1 is not equal to the greatest
     value of the type.  This we must know anyway, since if it is
     equal to this value, the loop rolls forever.  */

  if (!never_infinite)
    {
      if (nonzero_p (iv0->step))
	assumption = fold_build2 (NE_EXPR, boolean_type_node,
				  iv1->base, TYPE_MAX_VALUE (type));
      else
	assumption = fold_build2 (NE_EXPR, boolean_type_node,
				  iv0->base, TYPE_MIN_VALUE (type));

      if (zero_p (assumption))
	return false;
      if (!nonzero_p (assumption))
	niter->assumptions = fold_build2 (TRUTH_AND_EXPR, boolean_type_node,
					  niter->assumptions, assumption);
    }

  if (nonzero_p (iv0->step))
    iv1->base = fold_build2 (PLUS_EXPR, type,
			     iv1->base, build_int_cst (type, 1));
  else
    iv0->base = fold_build2 (MINUS_EXPR, type,
			     iv0->base, build_int_cst (type, 1));
  return number_of_iterations_lt (type, iv0, iv1, niter, never_infinite);
}

/* Determine the number of iterations according to condition (for staying
   inside loop) which compares two induction variables using comparison
   operator CODE.  The induction variable on left side of the comparison
   is IV0, the right-hand side is IV1.  Both induction variables must have
   type TYPE, which must be an integer or pointer type.  The steps of the
   ivs must be constants (or NULL_TREE, which is interpreted as constant zero).

   ONLY_EXIT is true if we are sure this is the only way the loop could be
   exited (including possibly non-returning function calls, exceptions, etc.)
   -- in this case we can use the information whether the control induction
   variables can overflow or not in a more efficient way.
   
   The results (number of iterations and assumptions as described in
   comments at struct tree_niter_desc in tree-flow.h) are stored to NITER.
   Returns false if it fails to determine number of iterations, true if it
   was determined (possibly with some assumptions).  */

static bool
number_of_iterations_cond (tree type, affine_iv *iv0, enum tree_code code,
			   affine_iv *iv1, struct tree_niter_desc *niter,
			   bool only_exit)
{
  bool never_infinite;

  /* The meaning of these assumptions is this:
     if !assumptions
       then the rest of information does not have to be valid
     if may_be_zero then the loop does not roll, even if
       niter != 0.  */
  niter->assumptions = boolean_true_node;
  niter->may_be_zero = boolean_false_node;
  niter->niter = NULL_TREE;
  niter->additional_info = boolean_true_node;

  niter->bound = NULL_TREE;
  niter->cmp = ERROR_MARK;

  /* Make < comparison from > ones, and for NE_EXPR comparisons, ensure that
     the control variable is on lhs.  */
  if (code == GE_EXPR || code == GT_EXPR
      || (code == NE_EXPR && zero_p (iv0->step)))
    {
      SWAP (iv0, iv1);
      code = swap_tree_comparison (code);
    }

  if (!only_exit)
    {
      /* If this is not the only possible exit from the loop, the information
	 that the induction variables cannot overflow as derived from
	 signedness analysis cannot be relied upon.  We use them e.g. in the
	 following way:  given loop for (i = 0; i <= n; i++), if i is
	 signed, it cannot overflow, thus this loop is equivalent to
	 for (i = 0; i < n + 1; i++);  however, if n == MAX, but the loop
	 is exited in some other way before i overflows, this transformation
	 is incorrect (the new loop exits immediately).  */
      iv0->no_overflow = false;
      iv1->no_overflow = false;
    }

  if (POINTER_TYPE_P (type))
    {
      /* Comparison of pointers is undefined unless both iv0 and iv1 point
	 to the same object.  If they do, the control variable cannot wrap
	 (as wrap around the bounds of memory will never return a pointer
	 that would be guaranteed to point to the same object, even if we
	 avoid undefined behavior by casting to size_t and back).  The
	 restrictions on pointer arithmetics and comparisons of pointers
	 ensure that using the no-overflow assumptions is correct in this
	 case even if ONLY_EXIT is false.  */
      iv0->no_overflow = true;
      iv1->no_overflow = true;
    }

  /* If the control induction variable does not overflow, the loop obviously
     cannot be infinite.  */
  if (!zero_p (iv0->step) && iv0->no_overflow)
    never_infinite = true;
  else if (!zero_p (iv1->step) && iv1->no_overflow)
    never_infinite = true;
  else
    never_infinite = false;

  /* We can handle the case when neither of the sides of the comparison is
     invariant, provided that the test is NE_EXPR.  This rarely occurs in
     practice, but it is simple enough to manage.  */
  if (!zero_p (iv0->step) && !zero_p (iv1->step))
    {
      if (code != NE_EXPR)
	return false;

      iv0->step = fold_binary_to_constant (MINUS_EXPR, type,
					   iv0->step, iv1->step);
      iv0->no_overflow = false;
      iv1->step = NULL_TREE;
      iv1->no_overflow = true;
    }

  /* If the result of the comparison is a constant,  the loop is weird.  More
     precise handling would be possible, but the situation is not common enough
     to waste time on it.  */
  if (zero_p (iv0->step) && zero_p (iv1->step))
    return false;

  /* Ignore loops of while (i-- < 10) type.  */
  if (code != NE_EXPR)
    {
      if (iv0->step && tree_int_cst_sign_bit (iv0->step))
	return false;

      if (!zero_p (iv1->step) && !tree_int_cst_sign_bit (iv1->step))
	return false;
    }

  /* If the loop exits immediately, there is nothing to do.  */
  if (zero_p (fold_build2 (code, boolean_type_node, iv0->base, iv1->base)))
    {
      niter->niter = build_int_cst (unsigned_type_for (type), 0);
      return true;
    }

  /* OK, now we know we have a senseful loop.  Handle several cases, depending
     on what comparison operator is used.  */
  switch (code)
    {
    case NE_EXPR:
      gcc_assert (zero_p (iv1->step));
      return number_of_iterations_ne (type, iv0, iv1->base, niter, never_infinite);
    case LT_EXPR:
      return number_of_iterations_lt (type, iv0, iv1, niter, never_infinite);
    case LE_EXPR:
      return number_of_iterations_le (type, iv0, iv1, niter, never_infinite);
    default:
      gcc_unreachable ();
    }
}

/* Substitute NEW for OLD in EXPR and fold the result.  */

static tree
simplify_replace_tree (tree expr, tree old, tree new)
{
  unsigned i, n;
  tree ret = NULL_TREE, e, se;

  if (!expr)
    return NULL_TREE;

  if (expr == old
      || operand_equal_p (expr, old, 0))
    return unshare_expr (new);

  if (!EXPR_P (expr))
    return expr;

  n = TREE_CODE_LENGTH (TREE_CODE (expr));
  for (i = 0; i < n; i++)
    {
      e = TREE_OPERAND (expr, i);
      se = simplify_replace_tree (e, old, new);
      if (e == se)
	continue;

      if (!ret)
	ret = copy_node (expr);

      TREE_OPERAND (ret, i) = se;
    }

  return (ret ? fold (ret) : expr);
}

/* Expand definitions of ssa names in EXPR as long as they are simple
   enough, and return the new expression.  */

tree
expand_simple_operations (tree expr)
{
  unsigned i, n;
  tree ret = NULL_TREE, e, ee, stmt;
  enum tree_code code;

  if (expr == NULL_TREE)
    return expr;

  if (is_gimple_min_invariant (expr))
    return expr;

  code = TREE_CODE (expr);
  if (IS_EXPR_CODE_CLASS (TREE_CODE_CLASS (code)))
    {
      n = TREE_CODE_LENGTH (code);
      for (i = 0; i < n; i++)
	{
	  e = TREE_OPERAND (expr, i);
	  ee = expand_simple_operations (e);
	  if (e == ee)
	    continue;

	  if (!ret)
	    ret = copy_node (expr);

	  TREE_OPERAND (ret, i) = ee;
	}

      if (!ret)
	return expr;

      fold_defer_overflow_warnings ();
      ret = fold (ret);
      fold_undefer_and_ignore_overflow_warnings ();
      return ret;
    }

  if (TREE_CODE (expr) != SSA_NAME)
    return expr;

  stmt = SSA_NAME_DEF_STMT (expr);
  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return expr;

  e = TREE_OPERAND (stmt, 1);
  if (/* Casts are simple.  */
      TREE_CODE (e) != NOP_EXPR
      && TREE_CODE (e) != CONVERT_EXPR
      /* Copies are simple.  */
      && TREE_CODE (e) != SSA_NAME
      /* Assignments of invariants are simple.  */
      && !is_gimple_min_invariant (e)
      /* And increments and decrements by a constant are simple.  */
      && !((TREE_CODE (e) == PLUS_EXPR
	    || TREE_CODE (e) == MINUS_EXPR)
	   && is_gimple_min_invariant (TREE_OPERAND (e, 1))))
    return expr;

  return expand_simple_operations (e);
}

/* Tries to simplify EXPR using the condition COND.  Returns the simplified
   expression (or EXPR unchanged, if no simplification was possible).  */

static tree
tree_simplify_using_condition_1 (tree cond, tree expr)
{
  bool changed;
  tree e, te, e0, e1, e2, notcond;
  enum tree_code code = TREE_CODE (expr);

  if (code == INTEGER_CST)
    return expr;

  if (code == TRUTH_OR_EXPR
      || code == TRUTH_AND_EXPR
      || code == COND_EXPR)
    {
      changed = false;

      e0 = tree_simplify_using_condition_1 (cond, TREE_OPERAND (expr, 0));
      if (TREE_OPERAND (expr, 0) != e0)
	changed = true;

      e1 = tree_simplify_using_condition_1 (cond, TREE_OPERAND (expr, 1));
      if (TREE_OPERAND (expr, 1) != e1)
	changed = true;

      if (code == COND_EXPR)
	{
	  e2 = tree_simplify_using_condition_1 (cond, TREE_OPERAND (expr, 2));
	  if (TREE_OPERAND (expr, 2) != e2)
	    changed = true;
	}
      else
	e2 = NULL_TREE;

      if (changed)
	{
	  if (code == COND_EXPR)
	    expr = fold_build3 (code, boolean_type_node, e0, e1, e2);
	  else
	    expr = fold_build2 (code, boolean_type_node, e0, e1);
	}

      return expr;
    }

  /* In case COND is equality, we may be able to simplify EXPR by copy/constant
     propagation, and vice versa.  Fold does not handle this, since it is
     considered too expensive.  */
  if (TREE_CODE (cond) == EQ_EXPR)
    {
      e0 = TREE_OPERAND (cond, 0);
      e1 = TREE_OPERAND (cond, 1);

      /* We know that e0 == e1.  Check whether we cannot simplify expr
	 using this fact.  */
      e = simplify_replace_tree (expr, e0, e1);
      if (zero_p (e) || nonzero_p (e))
	return e;

      e = simplify_replace_tree (expr, e1, e0);
      if (zero_p (e) || nonzero_p (e))
	return e;
    }
  if (TREE_CODE (expr) == EQ_EXPR)
    {
      e0 = TREE_OPERAND (expr, 0);
      e1 = TREE_OPERAND (expr, 1);

      /* If e0 == e1 (EXPR) implies !COND, then EXPR cannot be true.  */
      e = simplify_replace_tree (cond, e0, e1);
      if (zero_p (e))
	return e;
      e = simplify_replace_tree (cond, e1, e0);
      if (zero_p (e))
	return e;
    }
  if (TREE_CODE (expr) == NE_EXPR)
    {
      e0 = TREE_OPERAND (expr, 0);
      e1 = TREE_OPERAND (expr, 1);

      /* If e0 == e1 (!EXPR) implies !COND, then EXPR must be true.  */
      e = simplify_replace_tree (cond, e0, e1);
      if (zero_p (e))
	return boolean_true_node;
      e = simplify_replace_tree (cond, e1, e0);
      if (zero_p (e))
	return boolean_true_node;
    }

  te = expand_simple_operations (expr);

  /* Check whether COND ==> EXPR.  */
  notcond = invert_truthvalue (cond);
  e = fold_binary (TRUTH_OR_EXPR, boolean_type_node, notcond, te);
  if (nonzero_p (e))
    return e;

  /* Check whether COND ==> not EXPR.  */
  e = fold_binary (TRUTH_AND_EXPR, boolean_type_node, cond, te);
  if (e && zero_p (e))
    return e;

  return expr;
}

/* Tries to simplify EXPR using the condition COND.  Returns the simplified
   expression (or EXPR unchanged, if no simplification was possible).
   Wrapper around tree_simplify_using_condition_1 that ensures that chains
   of simple operations in definitions of ssa names in COND are expanded,
   so that things like casts or incrementing the value of the bound before
   the loop do not cause us to fail.  */

static tree
tree_simplify_using_condition (tree cond, tree expr)
{
  cond = expand_simple_operations (cond);

  return tree_simplify_using_condition_1 (cond, expr);
}

/* The maximum number of dominator BBs we search for conditions
   of loop header copies we use for simplifying a conditional
   expression.  */
#define MAX_DOMINATORS_TO_WALK 8

/* Tries to simplify EXPR using the conditions on entry to LOOP.
   Record the conditions used for simplification to CONDS_USED.
   Returns the simplified expression (or EXPR unchanged, if no
   simplification was possible).*/

static tree
simplify_using_initial_conditions (struct loop *loop, tree expr,
				   tree *conds_used)
{
  edge e;
  basic_block bb;
  tree exp, cond;
  int cnt = 0;

  if (TREE_CODE (expr) == INTEGER_CST)
    return expr;

  /* Limit walking the dominators to avoid quadraticness in
     the number of BBs times the number of loops in degenerate
     cases.  */
  for (bb = loop->header;
       bb != ENTRY_BLOCK_PTR && cnt < MAX_DOMINATORS_TO_WALK;
       bb = get_immediate_dominator (CDI_DOMINATORS, bb))
    {
      if (!single_pred_p (bb))
	continue;
      e = single_pred_edge (bb);

      if (!(e->flags & (EDGE_TRUE_VALUE | EDGE_FALSE_VALUE)))
	continue;

      cond = COND_EXPR_COND (last_stmt (e->src));
      if (e->flags & EDGE_FALSE_VALUE)
	cond = invert_truthvalue (cond);
      exp = tree_simplify_using_condition (cond, expr);

      if (exp != expr)
	*conds_used = fold_build2 (TRUTH_AND_EXPR,
				   boolean_type_node,
				   *conds_used,
				   cond);

      expr = exp;
      ++cnt;
    }

  return expr;
}

/* Tries to simplify EXPR using the evolutions of the loop invariants
   in the superloops of LOOP.  Returns the simplified expression
   (or EXPR unchanged, if no simplification was possible).  */

static tree
simplify_using_outer_evolutions (struct loop *loop, tree expr)
{
  enum tree_code code = TREE_CODE (expr);
  bool changed;
  tree e, e0, e1, e2;

  if (is_gimple_min_invariant (expr))
    return expr;

  if (code == TRUTH_OR_EXPR
      || code == TRUTH_AND_EXPR
      || code == COND_EXPR)
    {
      changed = false;

      e0 = simplify_using_outer_evolutions (loop, TREE_OPERAND (expr, 0));
      if (TREE_OPERAND (expr, 0) != e0)
	changed = true;

      e1 = simplify_using_outer_evolutions (loop, TREE_OPERAND (expr, 1));
      if (TREE_OPERAND (expr, 1) != e1)
	changed = true;

      if (code == COND_EXPR)
	{
	  e2 = simplify_using_outer_evolutions (loop, TREE_OPERAND (expr, 2));
	  if (TREE_OPERAND (expr, 2) != e2)
	    changed = true;
	}
      else
	e2 = NULL_TREE;

      if (changed)
	{
	  if (code == COND_EXPR)
	    expr = fold_build3 (code, boolean_type_node, e0, e1, e2);
	  else
	    expr = fold_build2 (code, boolean_type_node, e0, e1);
	}

      return expr;
    }

  e = instantiate_parameters (loop, expr);
  if (is_gimple_min_invariant (e))
    return e;

  return expr;
}

/* Returns true if EXIT is the only possible exit from LOOP.  */

static bool
loop_only_exit_p (struct loop *loop, edge exit)
{
  basic_block *body;
  block_stmt_iterator bsi;
  unsigned i;
  tree call;

  if (exit != loop->single_exit)
    return false;

  body = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    {
      for (bsi = bsi_start (body[0]); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  call = get_call_expr_in (bsi_stmt (bsi));
	  if (call && TREE_SIDE_EFFECTS (call))
	    {
	      free (body);
	      return false;
	    }
	}
    }

  free (body);
  return true;
}

/* Stores description of number of iterations of LOOP derived from
   EXIT (an exit edge of the LOOP) in NITER.  Returns true if some
   useful information could be derived (and fields of NITER has
   meaning described in comments at struct tree_niter_desc
   declaration), false otherwise.  If WARN is true and
   -Wunsafe-loop-optimizations was given, warn if the optimizer is going to use
   potentially unsafe assumptions.  */

bool
number_of_iterations_exit (struct loop *loop, edge exit,
			   struct tree_niter_desc *niter,
			   bool warn)
{
  tree stmt, cond, type;
  tree op0, op1;
  enum tree_code code;
  affine_iv iv0, iv1;

  if (!dominated_by_p (CDI_DOMINATORS, loop->latch, exit->src))
    return false;

  niter->assumptions = boolean_false_node;
  stmt = last_stmt (exit->src);
  if (!stmt || TREE_CODE (stmt) != COND_EXPR)
    return false;

  /* We want the condition for staying inside loop.  */
  cond = COND_EXPR_COND (stmt);
  if (exit->flags & EDGE_TRUE_VALUE)
    cond = invert_truthvalue (cond);

  code = TREE_CODE (cond);
  switch (code)
    {
    case GT_EXPR:
    case GE_EXPR:
    case NE_EXPR:
    case LT_EXPR:
    case LE_EXPR:
      break;

    default:
      return false;
    }
  
  op0 = TREE_OPERAND (cond, 0);
  op1 = TREE_OPERAND (cond, 1);
  type = TREE_TYPE (op0);

  if (TREE_CODE (type) != INTEGER_TYPE
      && !POINTER_TYPE_P (type))
    return false;
     
  if (!simple_iv (loop, stmt, op0, &iv0, false))
    return false;
  if (!simple_iv (loop, stmt, op1, &iv1, false))
    return false;

  /* We don't want to see undefined signed overflow warnings while
     computing the nmber of iterations.  */
  fold_defer_overflow_warnings ();

  iv0.base = expand_simple_operations (iv0.base);
  iv1.base = expand_simple_operations (iv1.base);
  if (!number_of_iterations_cond (type, &iv0, code, &iv1, niter,
				  loop_only_exit_p (loop, exit)))
    {
      fold_undefer_and_ignore_overflow_warnings ();
      return false;
    }

  if (optimize >= 3)
    {
      niter->assumptions = simplify_using_outer_evolutions (loop,
							    niter->assumptions);
      niter->may_be_zero = simplify_using_outer_evolutions (loop,
							    niter->may_be_zero);
      niter->niter = simplify_using_outer_evolutions (loop, niter->niter);
    }

  niter->additional_info = boolean_true_node;
  niter->assumptions
	  = simplify_using_initial_conditions (loop,
					       niter->assumptions,
					       &niter->additional_info);
  niter->may_be_zero
	  = simplify_using_initial_conditions (loop,
					       niter->may_be_zero,
					       &niter->additional_info);

  fold_undefer_and_ignore_overflow_warnings ();

  if (integer_onep (niter->assumptions))
    return true;

  /* With -funsafe-loop-optimizations we assume that nothing bad can happen.
     But if we can prove that there is overflow or some other source of weird
     behavior, ignore the loop even with -funsafe-loop-optimizations.  */
  if (integer_zerop (niter->assumptions))
    return false;

  if (flag_unsafe_loop_optimizations)
    niter->assumptions = boolean_true_node;

  if (warn)
    {
      const char *wording;
      location_t loc = EXPR_LOCATION (stmt);
  
      /* We can provide a more specific warning if one of the operator is
	 constant and the other advances by +1 or -1.  */
      if (!zero_p (iv1.step)
	  ? (zero_p (iv0.step)
	     && (integer_onep (iv1.step) || integer_all_onesp (iv1.step)))
	  : (iv0.step
	     && (integer_onep (iv0.step) || integer_all_onesp (iv0.step))))
        wording =
          flag_unsafe_loop_optimizations
          ? N_("assuming that the loop is not infinite")
          : N_("cannot optimize possibly infinite loops");
      else
	wording = 
	  flag_unsafe_loop_optimizations
	  ? N_("assuming that the loop counter does not overflow")
	  : N_("cannot optimize loop, the loop counter may overflow");

      if (LOCATION_LINE (loc) > 0)
	warning (OPT_Wunsafe_loop_optimizations, "%H%s", &loc, gettext (wording));
      else
	warning (OPT_Wunsafe_loop_optimizations, "%s", gettext (wording));
    }

  return flag_unsafe_loop_optimizations;
}

/* Try to determine the number of iterations of LOOP.  If we succeed,
   expression giving number of iterations is returned and *EXIT is
   set to the edge from that the information is obtained.  Otherwise
   chrec_dont_know is returned.  */

tree
find_loop_niter (struct loop *loop, edge *exit)
{
  unsigned n_exits, i;
  edge *exits = get_loop_exit_edges (loop, &n_exits);
  edge ex;
  tree niter = NULL_TREE, aniter;
  struct tree_niter_desc desc;

  *exit = NULL;
  for (i = 0; i < n_exits; i++)
    {
      ex = exits[i];
      if (!just_once_each_iteration_p (loop, ex->src))
	continue;

      if (!number_of_iterations_exit (loop, ex, &desc, false))
	continue;

      if (nonzero_p (desc.may_be_zero))
	{
	  /* We exit in the first iteration through this exit.
	     We won't find anything better.  */
	  niter = build_int_cst (unsigned_type_node, 0);
	  *exit = ex;
	  break;
	}

      if (!zero_p (desc.may_be_zero))
	continue;

      aniter = desc.niter;

      if (!niter)
	{
	  /* Nothing recorded yet.  */
	  niter = aniter;
	  *exit = ex;
	  continue;
	}

      /* Prefer constants, the lower the better.  */
      if (TREE_CODE (aniter) != INTEGER_CST)
	continue;

      if (TREE_CODE (niter) != INTEGER_CST)
	{
	  niter = aniter;
	  *exit = ex;
	  continue;
	}

      if (tree_int_cst_lt (aniter, niter))
	{
	  niter = aniter;
	  *exit = ex;
	  continue;
	}
    }
  free (exits);

  return niter ? niter : chrec_dont_know;
}

/*

   Analysis of a number of iterations of a loop by a brute-force evaluation.

*/

/* Bound on the number of iterations we try to evaluate.  */

#define MAX_ITERATIONS_TO_TRACK \
  ((unsigned) PARAM_VALUE (PARAM_MAX_ITERATIONS_TO_TRACK))

/* Returns the loop phi node of LOOP such that ssa name X is derived from its
   result by a chain of operations such that all but exactly one of their
   operands are constants.  */

static tree
chain_of_csts_start (struct loop *loop, tree x)
{
  tree stmt = SSA_NAME_DEF_STMT (x);
  tree use;
  basic_block bb = bb_for_stmt (stmt);

  if (!bb
      || !flow_bb_inside_loop_p (loop, bb))
    return NULL_TREE;
  
  if (TREE_CODE (stmt) == PHI_NODE)
    {
      if (bb == loop->header)
	return stmt;

      return NULL_TREE;
    }

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return NULL_TREE;

  if (!ZERO_SSA_OPERANDS (stmt, SSA_OP_ALL_VIRTUALS))
    return NULL_TREE;
  if (SINGLE_SSA_DEF_OPERAND (stmt, SSA_OP_DEF) == NULL_DEF_OPERAND_P)
    return NULL_TREE;

  use = SINGLE_SSA_TREE_OPERAND (stmt, SSA_OP_USE);
  if (use == NULL_USE_OPERAND_P)
    return NULL_TREE;

  return chain_of_csts_start (loop, use);
}

/* Determines whether the expression X is derived from a result of a phi node
   in header of LOOP such that

   * the derivation of X consists only from operations with constants
   * the initial value of the phi node is constant
   * the value of the phi node in the next iteration can be derived from the
     value in the current iteration by a chain of operations with constants.
   
   If such phi node exists, it is returned.  If X is a constant, X is returned
   unchanged.  Otherwise NULL_TREE is returned.  */

static tree
get_base_for (struct loop *loop, tree x)
{
  tree phi, init, next;

  if (is_gimple_min_invariant (x))
    return x;

  phi = chain_of_csts_start (loop, x);
  if (!phi)
    return NULL_TREE;

  init = PHI_ARG_DEF_FROM_EDGE (phi, loop_preheader_edge (loop));
  next = PHI_ARG_DEF_FROM_EDGE (phi, loop_latch_edge (loop));

  if (TREE_CODE (next) != SSA_NAME)
    return NULL_TREE;

  if (!is_gimple_min_invariant (init))
    return NULL_TREE;

  if (chain_of_csts_start (loop, next) != phi)
    return NULL_TREE;

  return phi;
}

/* Given an expression X, then 
 
   * if X is NULL_TREE, we return the constant BASE.
   * otherwise X is a SSA name, whose value in the considered loop is derived
     by a chain of operations with constant from a result of a phi node in
     the header of the loop.  Then we return value of X when the value of the
     result of this phi node is given by the constant BASE.  */

static tree
get_val_for (tree x, tree base)
{
  tree stmt, nx, val;
  use_operand_p op;
  ssa_op_iter iter;

  gcc_assert (is_gimple_min_invariant (base));

  if (!x)
    return base;

  stmt = SSA_NAME_DEF_STMT (x);
  if (TREE_CODE (stmt) == PHI_NODE)
    return base;

  FOR_EACH_SSA_USE_OPERAND (op, stmt, iter, SSA_OP_USE)
    {
      nx = USE_FROM_PTR (op);
      val = get_val_for (nx, base);
      SET_USE (op, val);
      val = fold (TREE_OPERAND (stmt, 1));
      SET_USE (op, nx);
      /* only iterate loop once.  */
      return val;
    }

  /* Should never reach here.  */
  gcc_unreachable();
}

/* Tries to count the number of iterations of LOOP till it exits by EXIT
   by brute force -- i.e. by determining the value of the operands of the
   condition at EXIT in first few iterations of the loop (assuming that
   these values are constant) and determining the first one in that the
   condition is not satisfied.  Returns the constant giving the number
   of the iterations of LOOP if successful, chrec_dont_know otherwise.  */

tree
loop_niter_by_eval (struct loop *loop, edge exit)
{
  tree cond, cnd, acnd;
  tree op[2], val[2], next[2], aval[2], phi[2];
  unsigned i, j;
  enum tree_code cmp;

  cond = last_stmt (exit->src);
  if (!cond || TREE_CODE (cond) != COND_EXPR)
    return chrec_dont_know;

  cnd = COND_EXPR_COND (cond);
  if (exit->flags & EDGE_TRUE_VALUE)
    cnd = invert_truthvalue (cnd);

  cmp = TREE_CODE (cnd);
  switch (cmp)
    {
    case EQ_EXPR:
    case NE_EXPR:
    case GT_EXPR:
    case GE_EXPR:
    case LT_EXPR:
    case LE_EXPR:
      for (j = 0; j < 2; j++)
	op[j] = TREE_OPERAND (cnd, j);
      break;

    default:
      return chrec_dont_know;
    }

  for (j = 0; j < 2; j++)
    {
      phi[j] = get_base_for (loop, op[j]);
      if (!phi[j])
	return chrec_dont_know;
    }

  for (j = 0; j < 2; j++)
    {
      if (TREE_CODE (phi[j]) == PHI_NODE)
	{
	  val[j] = PHI_ARG_DEF_FROM_EDGE (phi[j], loop_preheader_edge (loop));
	  next[j] = PHI_ARG_DEF_FROM_EDGE (phi[j], loop_latch_edge (loop));
	}
      else
	{
	  val[j] = phi[j];
	  next[j] = NULL_TREE;
	  op[j] = NULL_TREE;
	}
    }

  /* Don't issue signed overflow warnings.  */
  fold_defer_overflow_warnings ();

  for (i = 0; i < MAX_ITERATIONS_TO_TRACK; i++)
    {
      for (j = 0; j < 2; j++)
	aval[j] = get_val_for (op[j], val[j]);

      acnd = fold_binary (cmp, boolean_type_node, aval[0], aval[1]);
      if (acnd && zero_p (acnd))
	{
	  fold_undefer_and_ignore_overflow_warnings ();
	  if (dump_file && (dump_flags & TDF_DETAILS))
	    fprintf (dump_file,
		     "Proved that loop %d iterates %d times using brute force.\n",
		     loop->num, i);
	  return build_int_cst (unsigned_type_node, i);
	}

      for (j = 0; j < 2; j++)
	{
	  val[j] = get_val_for (next[j], val[j]);
	  if (!is_gimple_min_invariant (val[j]))
	    {
	      fold_undefer_and_ignore_overflow_warnings ();
	      return chrec_dont_know;
	    }
	}
    }

  fold_undefer_and_ignore_overflow_warnings ();

  return chrec_dont_know;
}

/* Finds the exit of the LOOP by that the loop exits after a constant
   number of iterations and stores the exit edge to *EXIT.  The constant
   giving the number of iterations of LOOP is returned.  The number of
   iterations is determined using loop_niter_by_eval (i.e. by brute force
   evaluation).  If we are unable to find the exit for that loop_niter_by_eval
   determines the number of iterations, chrec_dont_know is returned.  */

tree
find_loop_niter_by_eval (struct loop *loop, edge *exit)
{
  unsigned n_exits, i;
  edge *exits = get_loop_exit_edges (loop, &n_exits);
  edge ex;
  tree niter = NULL_TREE, aniter;

  *exit = NULL;
  for (i = 0; i < n_exits; i++)
    {
      ex = exits[i];
      if (!just_once_each_iteration_p (loop, ex->src))
	continue;

      aniter = loop_niter_by_eval (loop, ex);
      if (chrec_contains_undetermined (aniter))
	continue;

      if (niter
	  && !tree_int_cst_lt (aniter, niter))
	continue;

      niter = aniter;
      *exit = ex;
    }
  free (exits);

  return niter ? niter : chrec_dont_know;
}

/*

   Analysis of upper bounds on number of iterations of a loop.

*/

/* Returns true if we can prove that COND ==> VAL >= 0.  */

static bool
implies_nonnegative_p (tree cond, tree val)
{
  tree type = TREE_TYPE (val);
  tree compare;

  if (tree_expr_nonnegative_p (val))
    return true;

  if (nonzero_p (cond))
    return false;

  compare = fold_build2 (GE_EXPR,
			 boolean_type_node, val, build_int_cst (type, 0));
  compare = tree_simplify_using_condition_1 (cond, compare);

  return nonzero_p (compare);
}

/* Returns true if we can prove that COND ==> A >= B.  */

static bool
implies_ge_p (tree cond, tree a, tree b)
{
  tree compare = fold_build2 (GE_EXPR, boolean_type_node, a, b);

  if (nonzero_p (compare))
    return true;

  if (nonzero_p (cond))
    return false;

  compare = tree_simplify_using_condition_1 (cond, compare);

  return nonzero_p (compare);
}

/* Returns a constant upper bound on the value of expression VAL.  VAL
   is considered to be unsigned.  If its type is signed, its value must
   be nonnegative.
   
   The condition ADDITIONAL must be satisfied (for example, if VAL is
   "(unsigned) n" and ADDITIONAL is "n > 0", then we can derive that
   VAL is at most (unsigned) MAX_INT).  */
 
static double_int
derive_constant_upper_bound (tree val, tree additional)
{
  tree type = TREE_TYPE (val);
  tree op0, op1, subtype, maxt;
  double_int bnd, max, mmax, cst;

  if (INTEGRAL_TYPE_P (type))
    maxt = TYPE_MAX_VALUE (type);
  else
    maxt = upper_bound_in_type (type, type);

  max = tree_to_double_int (maxt);

  switch (TREE_CODE (val))
    {
    case INTEGER_CST:
      return tree_to_double_int (val);

    case NOP_EXPR:
    case CONVERT_EXPR:
      op0 = TREE_OPERAND (val, 0);
      subtype = TREE_TYPE (op0);
      if (!TYPE_UNSIGNED (subtype)
	  /* If TYPE is also signed, the fact that VAL is nonnegative implies
	     that OP0 is nonnegative.  */
	  && TYPE_UNSIGNED (type)
	  && !implies_nonnegative_p (additional, op0))
	{
	  /* If we cannot prove that the casted expression is nonnegative,
	     we cannot establish more useful upper bound than the precision
	     of the type gives us.  */
	  return max;
	}

      /* We now know that op0 is an nonnegative value.  Try deriving an upper
	 bound for it.  */
      bnd = derive_constant_upper_bound (op0, additional);

      /* If the bound does not fit in TYPE, max. value of TYPE could be
	 attained.  */
      if (double_int_ucmp (max, bnd) < 0)
	return max;

      return bnd;

    case PLUS_EXPR:
    case MINUS_EXPR:
      op0 = TREE_OPERAND (val, 0);
      op1 = TREE_OPERAND (val, 1);

      if (TREE_CODE (op1) != INTEGER_CST
	  || !implies_nonnegative_p (additional, op0))
	return max;

      /* Canonicalize to OP0 - CST.  Consider CST to be signed, in order to
	 choose the most logical way how to treat this constant regardless
	 of the signedness of the type.  */
      cst = tree_to_double_int (op1);
      cst = double_int_sext (cst, TYPE_PRECISION (type));
      if (TREE_CODE (val) == PLUS_EXPR)
	cst = double_int_neg (cst);

      bnd = derive_constant_upper_bound (op0, additional);

      if (double_int_negative_p (cst))
	{
	  cst = double_int_neg (cst);
	  /* Avoid CST == 0x80000...  */
	  if (double_int_negative_p (cst))
	    return max;;

	  /* OP0 + CST.  We need to check that
	     BND <= MAX (type) - CST.  */

	  mmax = double_int_add (max, double_int_neg (cst));
	  if (double_int_ucmp (bnd, mmax) > 0)
	    return max;

	  return double_int_add (bnd, cst);
	}
      else
	{
	  /* OP0 - CST, where CST >= 0.

	     If TYPE is signed, we have already verified that OP0 >= 0, and we
	     know that the result is nonnegative.  This implies that
	     VAL <= BND - CST.

	     If TYPE is unsigned, we must additionally know that OP0 >= CST,
	     otherwise the operation underflows.
	   */

	  /* This should only happen if the type is unsigned; however, for
	     programs that use overflowing signed arithmetics even with
	     -fno-wrapv, this condition may also be true for signed values.  */
	  if (double_int_ucmp (bnd, cst) < 0)
	    return max;

	  if (TYPE_UNSIGNED (type)
	      && !implies_ge_p (additional,
				op0, double_int_to_tree (type, cst)))
	    return max;

	  bnd = double_int_add (bnd, double_int_neg (cst));
	}

      return bnd;

    case FLOOR_DIV_EXPR:
    case EXACT_DIV_EXPR:
      op0 = TREE_OPERAND (val, 0);
      op1 = TREE_OPERAND (val, 1);
      if (TREE_CODE (op1) != INTEGER_CST
	  || tree_int_cst_sign_bit (op1))
	return max;

      bnd = derive_constant_upper_bound (op0, additional);
      return double_int_udiv (bnd, tree_to_double_int (op1), FLOOR_DIV_EXPR);

    default: 
      return max;
    }
}

/* Records that AT_STMT is executed at most BOUND times in LOOP.  The
   additional condition ADDITIONAL is recorded with the bound.  */

void
record_estimate (struct loop *loop, tree bound, tree additional, tree at_stmt)
{
  struct nb_iter_bound *elt = xmalloc (sizeof (struct nb_iter_bound));
  double_int i_bound = derive_constant_upper_bound (bound, additional);
  tree c_bound = double_int_to_tree (unsigned_type_for (TREE_TYPE (bound)),
				     i_bound);

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "Statements after ");
      print_generic_expr (dump_file, at_stmt, TDF_SLIM);
      fprintf (dump_file, " are executed at most ");
      print_generic_expr (dump_file, bound, TDF_SLIM);
      fprintf (dump_file, " (bounded by ");
      print_generic_expr (dump_file, c_bound, TDF_SLIM);
      fprintf (dump_file, ") times in loop %d.\n", loop->num);
    }

  elt->bound = c_bound;
  elt->at_stmt = at_stmt;
  elt->next = loop->bounds;
  loop->bounds = elt;
}

/* Initialize LOOP->ESTIMATED_NB_ITERATIONS with the lowest safe
   approximation of the number of iterations for LOOP.  */

static void
compute_estimated_nb_iterations (struct loop *loop)
{
  struct nb_iter_bound *bound;
  
  for (bound = loop->bounds; bound; bound = bound->next)
    {
      if (TREE_CODE (bound->bound) != INTEGER_CST)
	continue;

      /* Update only when there is no previous estimation, or when the current
	 estimation is smaller.  */
      if (chrec_contains_undetermined (loop->estimated_nb_iterations)
	  || tree_int_cst_lt (bound->bound, loop->estimated_nb_iterations))
	loop->estimated_nb_iterations = bound->bound;
    }
}

/* The following analyzers are extracting informations on the bounds
   of LOOP from the following undefined behaviors:

   - data references should not access elements over the statically
     allocated size,

   - signed variables should not overflow when flag_wrapv is not set.
*/

static void
infer_loop_bounds_from_undefined (struct loop *loop)
{
  unsigned i;
  basic_block bb, *bbs;
  block_stmt_iterator bsi;
  
  bbs = get_loop_body (loop);

  for (i = 0; i < loop->num_nodes; i++)
    {
      bb = bbs[i];

      /* If BB is not executed in each iteration of the loop, we cannot
	 use the operations in it to infer reliable upper bound on the
	 # of iterations of the loop.  */
      if (!dominated_by_p (CDI_DOMINATORS, loop->latch, bb))
	continue;

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
        {
	  tree stmt = bsi_stmt (bsi);

	  switch (TREE_CODE (stmt))
	    {
	    case MODIFY_EXPR:
	      {
		tree op0 = TREE_OPERAND (stmt, 0);
		tree op1 = TREE_OPERAND (stmt, 1);

		/* For each array access, analyze its access function
		   and record a bound on the loop iteration domain.  */
		if (TREE_CODE (op1) == ARRAY_REF 
		    && !array_ref_contains_indirect_ref (op1))
		  estimate_iters_using_array (stmt, op1);

		if (TREE_CODE (op0) == ARRAY_REF 
		    && !array_ref_contains_indirect_ref (op0))
		  estimate_iters_using_array (stmt, op0);

		/* For each signed type variable in LOOP, analyze its
		   scalar evolution and record a bound of the loop
		   based on the type's ranges.  */
		else if (!flag_wrapv && TREE_CODE (op0) == SSA_NAME)
		  {
		    tree init, step, diff, estimation;
		    tree scev = instantiate_parameters 
		      (loop, analyze_scalar_evolution (loop, op0));
		    tree type = chrec_type (scev);

		    if (chrec_contains_undetermined (scev)
			|| TYPE_OVERFLOW_WRAPS (type))
		      break;

		    init = initial_condition_in_loop_num (scev, loop->num);
		    step = evolution_part_in_loop_num (scev, loop->num);

		    if (init == NULL_TREE
			|| step == NULL_TREE
			|| TREE_CODE (init) != INTEGER_CST
			|| TREE_CODE (step) != INTEGER_CST
			|| TYPE_MIN_VALUE (type) == NULL_TREE
			|| TYPE_MAX_VALUE (type) == NULL_TREE)
		      break;

		    if (integer_nonzerop (step))
		      {
			tree utype;

			if (tree_int_cst_lt (step, integer_zero_node))
			  diff = fold_build2 (MINUS_EXPR, type, init,
					      TYPE_MIN_VALUE (type));
			else
			  diff = fold_build2 (MINUS_EXPR, type,
					      TYPE_MAX_VALUE (type), init);

			utype = unsigned_type_for (type);
			estimation = fold_build2 (CEIL_DIV_EXPR, type, diff,
						  step);
			record_estimate (loop,
					 fold_convert (utype, estimation),
					 boolean_true_node, stmt);
		      }
		  }

		break;
	      }

	    case CALL_EXPR:
	      {
		tree args;

		for (args = TREE_OPERAND (stmt, 1); args;
		     args = TREE_CHAIN (args))
		  if (TREE_CODE (TREE_VALUE (args)) == ARRAY_REF
		      && !array_ref_contains_indirect_ref (TREE_VALUE (args)))
		    estimate_iters_using_array (stmt, TREE_VALUE (args));

		break;
	      }

	    default:
	      break;
	    }
	}
    }

  compute_estimated_nb_iterations (loop);
  free (bbs);
}

/* Records estimates on numbers of iterations of LOOP.  */

static void
estimate_numbers_of_iterations_loop (struct loop *loop)
{
  edge *exits;
  tree niter, type;
  unsigned i, n_exits;
  struct tree_niter_desc niter_desc;

  /* Give up if we already have tried to compute an estimation.  */
  if (loop->estimated_nb_iterations == chrec_dont_know
      /* Or when we already have an estimation.  */
      || (loop->estimated_nb_iterations != NULL_TREE
	  && TREE_CODE (loop->estimated_nb_iterations) == INTEGER_CST))
    return;
  else
    loop->estimated_nb_iterations = chrec_dont_know;

  exits = get_loop_exit_edges (loop, &n_exits);
  for (i = 0; i < n_exits; i++)
    {
      if (!number_of_iterations_exit (loop, exits[i], &niter_desc, false))
	continue;

      niter = niter_desc.niter;
      type = TREE_TYPE (niter);
      if (!zero_p (niter_desc.may_be_zero)
	  && !nonzero_p (niter_desc.may_be_zero))
	niter = build3 (COND_EXPR, type, niter_desc.may_be_zero,
			build_int_cst (type, 0),
			niter);
      record_estimate (loop, niter,
		       niter_desc.additional_info,
		       last_stmt (exits[i]->src));
    }
  free (exits);
  
  if (chrec_contains_undetermined (loop->estimated_nb_iterations))
    infer_loop_bounds_from_undefined (loop);
}

/* Records estimates on numbers of iterations of LOOPS.  */

void
estimate_numbers_of_iterations (struct loops *loops)
{
  unsigned i;
  struct loop *loop;

  /* We don't want to issue signed overflow warnings while getting
     loop iteration estimates.  */
  fold_defer_overflow_warnings ();

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (loop)
	estimate_numbers_of_iterations_loop (loop);
    }

  fold_undefer_and_ignore_overflow_warnings ();
}

/* Returns true if statement S1 dominates statement S2.  */

static bool
stmt_dominates_stmt_p (tree s1, tree s2)
{
  basic_block bb1 = bb_for_stmt (s1), bb2 = bb_for_stmt (s2);

  if (!bb1
      || s1 == s2)
    return true;

  if (bb1 == bb2)
    {
      block_stmt_iterator bsi;

      for (bsi = bsi_start (bb1); bsi_stmt (bsi) != s2; bsi_next (&bsi))
	if (bsi_stmt (bsi) == s1)
	  return true;

      return false;
    }

  return dominated_by_p (CDI_DOMINATORS, bb2, bb1);
}

/* Returns true when we can prove that the number of executions of
   STMT in the loop is at most NITER, according to the fact
   that the statement NITER_BOUND->at_stmt is executed at most
   NITER_BOUND->bound times.  */

static bool
n_of_executions_at_most (tree stmt,
			 struct nb_iter_bound *niter_bound, 
			 tree niter)
{
  tree cond;
  tree bound = niter_bound->bound;
  tree bound_type = TREE_TYPE (bound);
  tree nit_type = TREE_TYPE (niter);
  enum tree_code cmp;

  gcc_assert (TYPE_UNSIGNED (bound_type)
	      && TYPE_UNSIGNED (nit_type)
	      && is_gimple_min_invariant (bound));
  if (TYPE_PRECISION (nit_type) > TYPE_PRECISION (bound_type))
    bound = fold_convert (nit_type, bound);
  else
    niter = fold_convert (bound_type, niter);

  /* After the statement niter_bound->at_stmt we know that anything is
     executed at most BOUND times.  */
  if (stmt && stmt_dominates_stmt_p (niter_bound->at_stmt, stmt))
    cmp = GE_EXPR;
  /* Before the statement niter_bound->at_stmt we know that anything
     is executed at most BOUND + 1 times.  */
  else
    cmp = GT_EXPR;

  cond = fold_binary (cmp, boolean_type_node, niter, bound);
  return nonzero_p (cond);
}

/* Returns true if the arithmetics in TYPE can be assumed not to wrap.  */

bool
nowrap_type_p (tree type)
{
  if (INTEGRAL_TYPE_P (type)
      && TYPE_OVERFLOW_UNDEFINED (type))
    return true;

  if (POINTER_TYPE_P (type))
    return true;

  return false;
}

/* Return false only when the induction variable BASE + STEP * I is
   known to not overflow: i.e. when the number of iterations is small
   enough with respect to the step and initial condition in order to
   keep the evolution confined in TYPEs bounds.  Return true when the
   iv is known to overflow or when the property is not computable.
 
   USE_OVERFLOW_SEMANTICS is true if this function should assume that
   the rules for overflow of the given language apply (e.g., that signed
   arithmetics in C does not overflow).  */

bool
scev_probably_wraps_p (tree base, tree step, 
		       tree at_stmt, struct loop *loop,
		       bool use_overflow_semantics)
{
  struct nb_iter_bound *bound;
  tree delta, step_abs;
  tree unsigned_type, valid_niter;
  tree type = TREE_TYPE (step);

  /* FIXME: We really need something like
     http://gcc.gnu.org/ml/gcc-patches/2005-06/msg02025.html.

     We used to test for the following situation that frequently appears
     during address arithmetics:
	 
       D.1621_13 = (long unsigned intD.4) D.1620_12;
       D.1622_14 = D.1621_13 * 8;
       D.1623_15 = (doubleD.29 *) D.1622_14;

     And derived that the sequence corresponding to D_14
     can be proved to not wrap because it is used for computing a
     memory access; however, this is not really the case -- for example,
     if D_12 = (unsigned char) [254,+,1], then D_14 has values
     2032, 2040, 0, 8, ..., but the code is still legal.  */

  if (chrec_contains_undetermined (base)
      || chrec_contains_undetermined (step)
      || TREE_CODE (step) != INTEGER_CST)
    return true;

  if (zero_p (step))
    return false;

  /* If we can use the fact that signed and pointer arithmetics does not
     wrap, we are done.  */
  if (use_overflow_semantics && nowrap_type_p (type))
    return false;

  /* Don't issue signed overflow warnings.  */
  fold_defer_overflow_warnings ();

  /* Otherwise, compute the number of iterations before we reach the
     bound of the type, and verify that the loop is exited before this
     occurs.  */
  unsigned_type = unsigned_type_for (type);
  base = fold_convert (unsigned_type, base);

  if (tree_int_cst_sign_bit (step))
    {
      tree extreme = fold_convert (unsigned_type,
				   lower_bound_in_type (type, type));
      delta = fold_build2 (MINUS_EXPR, unsigned_type, base, extreme);
      step_abs = fold_build1 (NEGATE_EXPR, unsigned_type,
			      fold_convert (unsigned_type, step));
    }
  else
    {
      tree extreme = fold_convert (unsigned_type,
				   upper_bound_in_type (type, type));
      delta = fold_build2 (MINUS_EXPR, unsigned_type, extreme, base);
      step_abs = fold_convert (unsigned_type, step);
    }

  valid_niter = fold_build2 (FLOOR_DIV_EXPR, unsigned_type, delta, step_abs);

  estimate_numbers_of_iterations_loop (loop);
  for (bound = loop->bounds; bound; bound = bound->next)
    {
      if (n_of_executions_at_most (at_stmt, bound, valid_niter))
	{
	  fold_undefer_and_ignore_overflow_warnings ();
	  return false;
	}
    }

  fold_undefer_and_ignore_overflow_warnings ();

  /* At this point we still don't have a proof that the iv does not
     overflow: give up.  */
  return true;
}

/* Frees the information on upper bounds on numbers of iterations of LOOP.  */

void
free_numbers_of_iterations_estimates_loop (struct loop *loop)
{
  struct nb_iter_bound *bound, *next;

  loop->nb_iterations = NULL;
  loop->estimated_nb_iterations = NULL;
  for (bound = loop->bounds; bound; bound = next)
    {
      next = bound->next;
      free (bound);
    }

  loop->bounds = NULL;
}

/* Frees the information on upper bounds on numbers of iterations of LOOPS.  */

void
free_numbers_of_iterations_estimates (struct loops *loops)
{
  unsigned i;
  struct loop *loop;

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (loop)
	free_numbers_of_iterations_estimates_loop (loop);
    }
}

/* Substitute value VAL for ssa name NAME inside expressions held
   at LOOP.  */

void
substitute_in_loop_info (struct loop *loop, tree name, tree val)
{
  loop->nb_iterations = simplify_replace_tree (loop->nb_iterations, name, val);
  loop->estimated_nb_iterations
	  = simplify_replace_tree (loop->estimated_nb_iterations, name, val);
}
