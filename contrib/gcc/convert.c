/* Utility routines for data type conversion for GCC.
   Copyright (C) 1987, 1988, 1991, 1992, 1993, 1994, 1995, 1997, 1998,
   2000, 2001, 2002, 2003, 2004, 2005, 2006 Free Software Foundation, Inc.

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


/* These routines are somewhat language-independent utility function
   intended to be called by the language-specific convert () functions.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "convert.h"
#include "toplev.h"
#include "langhooks.h"
#include "real.h"

/* Convert EXPR to some pointer or reference type TYPE.
   EXPR must be pointer, reference, integer, enumeral, or literal zero;
   in other cases error is called.  */

tree
convert_to_pointer (tree type, tree expr)
{
  if (TREE_TYPE (expr) == type)
    return expr;

  if (integer_zerop (expr))
    {
      tree t = build_int_cst (type, 0);
      if (TREE_OVERFLOW (expr) || TREE_CONSTANT_OVERFLOW (expr))
	t = force_fit_type (t, 0, TREE_OVERFLOW (expr),
			    TREE_CONSTANT_OVERFLOW (expr));
      return t;
    }

  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return fold_build1 (NOP_EXPR, type, expr);

    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      if (TYPE_PRECISION (TREE_TYPE (expr)) != POINTER_SIZE)
	expr = fold_build1 (NOP_EXPR,
			     lang_hooks.types.type_for_size (POINTER_SIZE, 0),
			    expr);
      return fold_build1 (CONVERT_EXPR, type, expr);

	/* APPLE LOCAL begin blocks (C++ ck) */
    case BLOCK_POINTER_TYPE:
	/* APPLE LOCAL begin radar 5809099 */
	if (objc_is_id (type)
		|| (TREE_CODE (type) == POINTER_TYPE && VOID_TYPE_P (TREE_TYPE (type))))
	/* APPLE LOCAL end radar 5809099 */
		return fold_build1 (NOP_EXPR, type, expr);
	/* APPLE LOCAL end blocks (C++ ck) */
      default:
	error ("cannot convert to a pointer type");
	return convert_to_pointer (type, integer_zero_node);
    }
}

/* APPLE LOCAL begin blocks (C++ ck) */
tree
convert_to_block_pointer (tree type, tree expr)
{
  if (TREE_TYPE (expr) == type)
      return expr;
  
  if (integer_zerop (expr))
    {
      tree t = build_int_cst (type, 0);
      if (TREE_OVERFLOW (expr) || TREE_CONSTANT_OVERFLOW (expr))
	t = force_fit_type (t, 0, TREE_OVERFLOW (expr),
						TREE_CONSTANT_OVERFLOW (expr));
      return t;
    }
  
  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case BLOCK_POINTER_TYPE:
	return fold_build1 (NOP_EXPR, type, expr);
	
    case INTEGER_TYPE:
	if (TYPE_PRECISION (TREE_TYPE (expr)) != POINTER_SIZE)
		expr = fold_build1 (NOP_EXPR,
					lang_hooks.types.type_for_size (POINTER_SIZE, 0),
					expr);
	return fold_build1 (CONVERT_EXPR, type, expr);
	
    case POINTER_TYPE:
	/* APPLE LOCAL radar 5809099 */
	if (objc_is_id (TREE_TYPE (expr)) || VOID_TYPE_P (TREE_TYPE (TREE_TYPE (expr))))
		return build1 (NOP_EXPR, type, expr);
	/* fall thru */
	
      default:
	error ("cannot convert to a block pointer type");
	return convert_to_block_pointer (type, integer_zero_node);
    }
}

/* APPLE LOCAL end blocks (C++ ck) */

/* Avoid any floating point extensions from EXP.  */
tree
strip_float_extensions (tree exp)
{
  tree sub, expt, subt;

  /*  For floating point constant look up the narrowest type that can hold
      it properly and handle it like (type)(narrowest_type)constant.
      This way we can optimize for instance a=a*2.0 where "a" is float
      but 2.0 is double constant.  */
  if (TREE_CODE (exp) == REAL_CST)
    {
      REAL_VALUE_TYPE orig;
      tree type = NULL;

      orig = TREE_REAL_CST (exp);
      if (TYPE_PRECISION (TREE_TYPE (exp)) > TYPE_PRECISION (float_type_node)
	  && exact_real_truncate (TYPE_MODE (float_type_node), &orig))
	type = float_type_node;
      else if (TYPE_PRECISION (TREE_TYPE (exp))
	       > TYPE_PRECISION (double_type_node)
	       && exact_real_truncate (TYPE_MODE (double_type_node), &orig))
	type = double_type_node;
      if (type)
	return build_real (type, real_value_truncate (TYPE_MODE (type), orig));
    }

  if (TREE_CODE (exp) != NOP_EXPR
      && TREE_CODE (exp) != CONVERT_EXPR)
    return exp;

  sub = TREE_OPERAND (exp, 0);
  subt = TREE_TYPE (sub);
  expt = TREE_TYPE (exp);

  if (!FLOAT_TYPE_P (subt))
    return exp;

  if (TYPE_PRECISION (subt) > TYPE_PRECISION (expt))
    return exp;

  return strip_float_extensions (sub);
}


/* Convert EXPR to some floating-point type TYPE.

   EXPR must be float, integer, or enumeral;
   in other cases error is called.  */

tree
convert_to_real (tree type, tree expr)
{
  enum built_in_function fcode = builtin_mathfn_code (expr);
  tree itype = TREE_TYPE (expr);

  /* Disable until we figure out how to decide whether the functions are
     present in runtime.  */
  /* Convert (float)sqrt((double)x) where x is float into sqrtf(x) */
  if (optimize
      && (TYPE_MODE (type) == TYPE_MODE (double_type_node)
          || TYPE_MODE (type) == TYPE_MODE (float_type_node)))
    {
      switch (fcode)
        {
#define CASE_MATHFN(FN) case BUILT_IN_##FN: case BUILT_IN_##FN##L:
	  CASE_MATHFN (ACOS)
	  CASE_MATHFN (ACOSH)
	  CASE_MATHFN (ASIN)
	  CASE_MATHFN (ASINH)
	  CASE_MATHFN (ATAN)
	  CASE_MATHFN (ATANH)
	  CASE_MATHFN (CBRT)
	  CASE_MATHFN (COS)
	  CASE_MATHFN (COSH)
	  CASE_MATHFN (ERF)
	  CASE_MATHFN (ERFC)
	  CASE_MATHFN (EXP)
	  CASE_MATHFN (EXP10)
	  CASE_MATHFN (EXP2)
	  CASE_MATHFN (EXPM1)
	  CASE_MATHFN (FABS)
	  CASE_MATHFN (GAMMA)
	  CASE_MATHFN (J0)
	  CASE_MATHFN (J1)
	  CASE_MATHFN (LGAMMA)
	  CASE_MATHFN (LOG)
	  CASE_MATHFN (LOG10)
	  CASE_MATHFN (LOG1P)
	  CASE_MATHFN (LOG2)
	  CASE_MATHFN (LOGB)
	  CASE_MATHFN (POW10)
	  CASE_MATHFN (SIN)
	  CASE_MATHFN (SINH)
	  CASE_MATHFN (SQRT)
	  CASE_MATHFN (TAN)
	  CASE_MATHFN (TANH)
	  CASE_MATHFN (TGAMMA)
	  CASE_MATHFN (Y0)
	  CASE_MATHFN (Y1)
#undef CASE_MATHFN
	    {
	      tree arg0 = strip_float_extensions (TREE_VALUE (TREE_OPERAND (expr, 1)));
	      tree newtype = type;

	      /* We have (outertype)sqrt((innertype)x).  Choose the wider mode from
		 the both as the safe type for operation.  */
	      if (TYPE_PRECISION (TREE_TYPE (arg0)) > TYPE_PRECISION (type))
		newtype = TREE_TYPE (arg0);

	      /* Be careful about integer to fp conversions.
		 These may overflow still.  */
	      if (FLOAT_TYPE_P (TREE_TYPE (arg0))
		  && TYPE_PRECISION (newtype) < TYPE_PRECISION (itype)
		  && (TYPE_MODE (newtype) == TYPE_MODE (double_type_node)
		      || TYPE_MODE (newtype) == TYPE_MODE (float_type_node)))
	        {
		  tree arglist;
		  tree fn = mathfn_built_in (newtype, fcode);

		  if (fn)
		  {
		    arglist = build_tree_list (NULL_TREE, fold (convert_to_real (newtype, arg0)));
		    expr = build_function_call_expr (fn, arglist);
		    if (newtype == type)
		      return expr;
		  }
		}
	    }
	default:
	  break;
	}
    }
  if (optimize
      && (((fcode == BUILT_IN_FLOORL
	   || fcode == BUILT_IN_CEILL
	   || fcode == BUILT_IN_ROUNDL
	   || fcode == BUILT_IN_RINTL
	   || fcode == BUILT_IN_TRUNCL
	   || fcode == BUILT_IN_NEARBYINTL)
	  && (TYPE_MODE (type) == TYPE_MODE (double_type_node)
	      || TYPE_MODE (type) == TYPE_MODE (float_type_node)))
	  || ((fcode == BUILT_IN_FLOOR
	       || fcode == BUILT_IN_CEIL
	       || fcode == BUILT_IN_ROUND
	       || fcode == BUILT_IN_RINT
	       || fcode == BUILT_IN_TRUNC
	       || fcode == BUILT_IN_NEARBYINT)
	      && (TYPE_MODE (type) == TYPE_MODE (float_type_node)))))
    {
      tree fn = mathfn_built_in (type, fcode);

      if (fn)
	{
	  tree arg
	    = strip_float_extensions (TREE_VALUE (TREE_OPERAND (expr, 1)));

	  /* Make sure (type)arg0 is an extension, otherwise we could end up
	     changing (float)floor(double d) into floorf((float)d), which is
	     incorrect because (float)d uses round-to-nearest and can round
	     up to the next integer.  */
	  if (TYPE_PRECISION (type) >= TYPE_PRECISION (TREE_TYPE (arg)))
	    return
	      build_function_call_expr (fn,
					build_tree_list (NULL_TREE,
					  fold (convert_to_real (type, arg))));
	}
    }

  /* Propagate the cast into the operation.  */
  if (itype != type && FLOAT_TYPE_P (type))
    switch (TREE_CODE (expr))
      {
	/* Convert (float)-x into -(float)x.  This is safe for
	   round-to-nearest rounding mode.  */
	case ABS_EXPR:
	case NEGATE_EXPR:
	  if (!flag_rounding_math
	      && TYPE_PRECISION (type) < TYPE_PRECISION (TREE_TYPE (expr)))
	    return build1 (TREE_CODE (expr), type,
			   fold (convert_to_real (type,
						  TREE_OPERAND (expr, 0))));
	  break;
	/* Convert (outertype)((innertype0)a+(innertype1)b)
	   into ((newtype)a+(newtype)b) where newtype
	   is the widest mode from all of these.  */
	case PLUS_EXPR:
	case MINUS_EXPR:
	case MULT_EXPR:
	case RDIV_EXPR:
	   {
	     tree arg0 = strip_float_extensions (TREE_OPERAND (expr, 0));
	     tree arg1 = strip_float_extensions (TREE_OPERAND (expr, 1));

	     if (FLOAT_TYPE_P (TREE_TYPE (arg0))
		 && FLOAT_TYPE_P (TREE_TYPE (arg1)))
	       {
		  tree newtype = type;

		  if (TYPE_MODE (TREE_TYPE (arg0)) == SDmode
		      || TYPE_MODE (TREE_TYPE (arg1)) == SDmode)
		    newtype = dfloat32_type_node;
		  if (TYPE_MODE (TREE_TYPE (arg0)) == DDmode
		      || TYPE_MODE (TREE_TYPE (arg1)) == DDmode)
		    newtype = dfloat64_type_node;
		  if (TYPE_MODE (TREE_TYPE (arg0)) == TDmode
		      || TYPE_MODE (TREE_TYPE (arg1)) == TDmode)
                    newtype = dfloat128_type_node;
		  if (newtype == dfloat32_type_node
		      || newtype == dfloat64_type_node
		      || newtype == dfloat128_type_node)
		    {
		      expr = build2 (TREE_CODE (expr), newtype,
				     fold (convert_to_real (newtype, arg0)),
				     fold (convert_to_real (newtype, arg1)));
		      if (newtype == type)
			return expr;
		      break;
		    }

		  if (TYPE_PRECISION (TREE_TYPE (arg0)) > TYPE_PRECISION (newtype))
		    newtype = TREE_TYPE (arg0);
		  if (TYPE_PRECISION (TREE_TYPE (arg1)) > TYPE_PRECISION (newtype))
		    newtype = TREE_TYPE (arg1);
		  if (TYPE_PRECISION (newtype) < TYPE_PRECISION (itype))
		    {
		      expr = build2 (TREE_CODE (expr), newtype,
				     fold (convert_to_real (newtype, arg0)),
				     fold (convert_to_real (newtype, arg1)));
		      if (newtype == type)
			return expr;
		    }
	       }
	   }
	  break;
	default:
	  break;
      }

  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case REAL_TYPE:
      /* Ignore the conversion if we don't need to store intermediate
	 results and neither type is a decimal float.  */
      return build1 ((flag_float_store
		     || DECIMAL_FLOAT_TYPE_P (type)
		     || DECIMAL_FLOAT_TYPE_P (itype))
		     ? CONVERT_EXPR : NOP_EXPR, type, expr);

    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      return build1 (FLOAT_EXPR, type, expr);

    case COMPLEX_TYPE:
      return convert (type,
		      fold_build1 (REALPART_EXPR,
				   TREE_TYPE (TREE_TYPE (expr)), expr));

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      error ("pointer value used where a floating point value was expected");
      return convert_to_real (type, integer_zero_node);

    default:
      error ("aggregate value used where a float was expected");
      return convert_to_real (type, integer_zero_node);
    }
}

/* Convert EXPR to some integer (or enum) type TYPE.

   EXPR must be pointer, integer, discrete (enum, char, or bool), float, or
   vector; in other cases error is called.

   The result of this is always supposed to be a newly created tree node
   not in use in any existing structure.  */

tree
convert_to_integer (tree type, tree expr)
{
  enum tree_code ex_form = TREE_CODE (expr);
  tree intype = TREE_TYPE (expr);
  unsigned int inprec = TYPE_PRECISION (intype);
  unsigned int outprec = TYPE_PRECISION (type);

  /* An INTEGER_TYPE cannot be incomplete, but an ENUMERAL_TYPE can
     be.  Consider `enum E = { a, b = (enum E) 3 };'.  */
  if (!COMPLETE_TYPE_P (type))
    {
      error ("conversion to incomplete type");
      return error_mark_node;
    }

  /* Convert e.g. (long)round(d) -> lround(d).  */
  /* If we're converting to char, we may encounter differing behavior
     between converting from double->char vs double->long->char.
     We're in "undefined" territory but we prefer to be conservative,
     so only proceed in "unsafe" math mode.  */
  if (optimize
      && (flag_unsafe_math_optimizations
	  || (long_integer_type_node
	      && outprec >= TYPE_PRECISION (long_integer_type_node))))
    {
      tree s_expr = strip_float_extensions (expr);
      tree s_intype = TREE_TYPE (s_expr);
      const enum built_in_function fcode = builtin_mathfn_code (s_expr);
      tree fn = 0;
      
      switch (fcode)
        {
	CASE_FLT_FN (BUILT_IN_CEIL):
	  /* Only convert in ISO C99 mode.  */
	  if (!TARGET_C99_FUNCTIONS)
	    break;
	  if (outprec < TYPE_PRECISION (long_integer_type_node)
	      || (outprec == TYPE_PRECISION (long_integer_type_node)
		  && !TYPE_UNSIGNED (type)))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LCEIL);
	  else if (outprec == TYPE_PRECISION (long_long_integer_type_node)
		   && !TYPE_UNSIGNED (type))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LLCEIL);
	  break;

	CASE_FLT_FN (BUILT_IN_FLOOR):
	  /* Only convert in ISO C99 mode.  */
	  if (!TARGET_C99_FUNCTIONS)
	    break;
	  if (outprec < TYPE_PRECISION (long_integer_type_node)
	      || (outprec == TYPE_PRECISION (long_integer_type_node)
		  && !TYPE_UNSIGNED (type)))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LFLOOR);
	  else if (outprec == TYPE_PRECISION (long_long_integer_type_node)
		   && !TYPE_UNSIGNED (type))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LLFLOOR);
	  break;

	CASE_FLT_FN (BUILT_IN_ROUND):
	  if (outprec < TYPE_PRECISION (long_integer_type_node)
	      || (outprec == TYPE_PRECISION (long_integer_type_node)
		  && !TYPE_UNSIGNED (type)))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LROUND);
	  else if (outprec == TYPE_PRECISION (long_long_integer_type_node)
		   && !TYPE_UNSIGNED (type))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LLROUND);
	  break;

	CASE_FLT_FN (BUILT_IN_NEARBYINT):
	  /* Only convert nearbyint* if we can ignore math exceptions.  */
	  if (flag_trapping_math)
	    break;
	  /* ... Fall through ...  */
	CASE_FLT_FN (BUILT_IN_RINT):
	  if (outprec < TYPE_PRECISION (long_integer_type_node)
	      || (outprec == TYPE_PRECISION (long_integer_type_node)
		  && !TYPE_UNSIGNED (type)))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LRINT);
	  else if (outprec == TYPE_PRECISION (long_long_integer_type_node)
		   && !TYPE_UNSIGNED (type))
	    fn = mathfn_built_in (s_intype, BUILT_IN_LLRINT);
	  break;

	CASE_FLT_FN (BUILT_IN_TRUNC):
	  {
	    tree arglist = TREE_OPERAND (s_expr, 1);
	    return convert_to_integer (type, TREE_VALUE (arglist));
	  }

	default:
	  break;
	}
      
      if (fn)
        {
	  tree arglist = TREE_OPERAND (s_expr, 1);
	  tree newexpr = build_function_call_expr (fn, arglist);
	  return convert_to_integer (type, newexpr);
	}
    }

  switch (TREE_CODE (intype))
    {
    case POINTER_TYPE:
    case REFERENCE_TYPE:
    /* APPLE LOCAL radar 6035389 */
    case BLOCK_POINTER_TYPE:
      if (integer_zerop (expr))
	return build_int_cst (type, 0);

      /* Convert to an unsigned integer of the correct width first,
	 and from there widen/truncate to the required type.  */
      expr = fold_build1 (CONVERT_EXPR,
			  lang_hooks.types.type_for_size (POINTER_SIZE, 0),
			  expr);
      return fold_convert (type, expr);

    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      /* If this is a logical operation, which just returns 0 or 1, we can
	 change the type of the expression.  */

      if (TREE_CODE_CLASS (ex_form) == tcc_comparison)
	{
	  expr = copy_node (expr);
	  TREE_TYPE (expr) = type;
	  return expr;
	}

      /* If we are widening the type, put in an explicit conversion.
	 Similarly if we are not changing the width.  After this, we know
	 we are truncating EXPR.  */

      else if (outprec >= inprec)
	{
	  enum tree_code code;
	  tree tem;

	  /* If the precision of the EXPR's type is K bits and the
	     destination mode has more bits, and the sign is changing,
	     it is not safe to use a NOP_EXPR.  For example, suppose
	     that EXPR's type is a 3-bit unsigned integer type, the
	     TYPE is a 3-bit signed integer type, and the machine mode
	     for the types is 8-bit QImode.  In that case, the
	     conversion necessitates an explicit sign-extension.  In
	     the signed-to-unsigned case the high-order bits have to
	     be cleared.  */
	  if (TYPE_UNSIGNED (type) != TYPE_UNSIGNED (TREE_TYPE (expr))
	      && (TYPE_PRECISION (TREE_TYPE (expr))
		  != GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (expr)))))
	    code = CONVERT_EXPR;
	  else
	    code = NOP_EXPR;

	  tem = fold_unary (code, type, expr);
	  if (tem)
	    return tem;

	  tem = build1 (code, type, expr);
	  TREE_NO_WARNING (tem) = 1;
	  return tem;
	}

      /* If TYPE is an enumeral type or a type with a precision less
	 than the number of bits in its mode, do the conversion to the
	 type corresponding to its mode, then do a nop conversion
	 to TYPE.  */
      else if (TREE_CODE (type) == ENUMERAL_TYPE
	       || outprec != GET_MODE_BITSIZE (TYPE_MODE (type)))
	return build1 (NOP_EXPR, type,
		       convert (lang_hooks.types.type_for_mode
				(TYPE_MODE (type), TYPE_UNSIGNED (type)),
				expr));

      /* Here detect when we can distribute the truncation down past some
	 arithmetic.  For example, if adding two longs and converting to an
	 int, we can equally well convert both to ints and then add.
	 For the operations handled here, such truncation distribution
	 is always safe.
	 It is desirable in these cases:
	 1) when truncating down to full-word from a larger size
	 2) when truncating takes no work.
	 3) when at least one operand of the arithmetic has been extended
	 (as by C's default conversions).  In this case we need two conversions
	 if we do the arithmetic as already requested, so we might as well
	 truncate both and then combine.  Perhaps that way we need only one.

	 Note that in general we cannot do the arithmetic in a type
	 shorter than the desired result of conversion, even if the operands
	 are both extended from a shorter type, because they might overflow
	 if combined in that type.  The exceptions to this--the times when
	 two narrow values can be combined in their narrow type even to
	 make a wider result--are handled by "shorten" in build_binary_op.  */

      switch (ex_form)
	{
	case RSHIFT_EXPR:
	  /* We can pass truncation down through right shifting
	     when the shift count is a nonpositive constant.  */
	  if (TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST
	      && tree_int_cst_sgn (TREE_OPERAND (expr, 1)) <= 0)
	    goto trunc1;
	  break;

	case LSHIFT_EXPR:
	  /* We can pass truncation down through left shifting
	     when the shift count is a nonnegative constant and
	     the target type is unsigned.  */
	  if (TREE_CODE (TREE_OPERAND (expr, 1)) == INTEGER_CST
	      && tree_int_cst_sgn (TREE_OPERAND (expr, 1)) >= 0
	      && TYPE_UNSIGNED (type)
	      && TREE_CODE (TYPE_SIZE (type)) == INTEGER_CST)
	    {
	      /* If shift count is less than the width of the truncated type,
		 really shift.  */
	      if (tree_int_cst_lt (TREE_OPERAND (expr, 1), TYPE_SIZE (type)))
		/* In this case, shifting is like multiplication.  */
		goto trunc1;
	      else
		{
		  /* If it is >= that width, result is zero.
		     Handling this with trunc1 would give the wrong result:
		     (int) ((long long) a << 32) is well defined (as 0)
		     but (int) a << 32 is undefined and would get a
		     warning.  */

		  tree t = build_int_cst (type, 0);

		  /* If the original expression had side-effects, we must
		     preserve it.  */
		  if (TREE_SIDE_EFFECTS (expr))
		    return build2 (COMPOUND_EXPR, type, expr, t);
		  else
		    return t;
		}
	    }
	  break;

	case MAX_EXPR:
	case MIN_EXPR:
	case MULT_EXPR:
	  {
	    tree arg0 = get_unwidened (TREE_OPERAND (expr, 0), type);
	    tree arg1 = get_unwidened (TREE_OPERAND (expr, 1), type);

	    /* Don't distribute unless the output precision is at least as big
	       as the actual inputs.  Otherwise, the comparison of the
	       truncated values will be wrong.  */
	    if (outprec >= TYPE_PRECISION (TREE_TYPE (arg0))
		&& outprec >= TYPE_PRECISION (TREE_TYPE (arg1))
		/* If signedness of arg0 and arg1 don't match,
		   we can't necessarily find a type to compare them in.  */
		&& (TYPE_UNSIGNED (TREE_TYPE (arg0))
		    == TYPE_UNSIGNED (TREE_TYPE (arg1))))
	      goto trunc1;
	    break;
	  }

	case PLUS_EXPR:
	case MINUS_EXPR:
	case BIT_AND_EXPR:
	case BIT_IOR_EXPR:
	case BIT_XOR_EXPR:
	trunc1:
	  {
	    tree arg0 = get_unwidened (TREE_OPERAND (expr, 0), type);
	    tree arg1 = get_unwidened (TREE_OPERAND (expr, 1), type);

	    if (outprec >= BITS_PER_WORD
		|| TRULY_NOOP_TRUNCATION (outprec, inprec)
		|| inprec > TYPE_PRECISION (TREE_TYPE (arg0))
		|| inprec > TYPE_PRECISION (TREE_TYPE (arg1)))
	      {
		/* Do the arithmetic in type TYPEX,
		   then convert result to TYPE.  */
		tree typex = type;

		/* Can't do arithmetic in enumeral types
		   so use an integer type that will hold the values.  */
		if (TREE_CODE (typex) == ENUMERAL_TYPE)
		  typex = lang_hooks.types.type_for_size
		    (TYPE_PRECISION (typex), TYPE_UNSIGNED (typex));

		/* But now perhaps TYPEX is as wide as INPREC.
		   In that case, do nothing special here.
		   (Otherwise would recurse infinitely in convert.  */
		if (TYPE_PRECISION (typex) != inprec)
		  {
		    /* Don't do unsigned arithmetic where signed was wanted,
		       or vice versa.
		       Exception: if both of the original operands were
		       unsigned then we can safely do the work as unsigned.
		       Exception: shift operations take their type solely
		       from the first argument.
		       Exception: the LSHIFT_EXPR case above requires that
		       we perform this operation unsigned lest we produce
		       signed-overflow undefinedness.
		       And we may need to do it as unsigned
		       if we truncate to the original size.  */
		    if (TYPE_UNSIGNED (TREE_TYPE (expr))
			|| (TYPE_UNSIGNED (TREE_TYPE (arg0))
			    && (TYPE_UNSIGNED (TREE_TYPE (arg1))
				|| ex_form == LSHIFT_EXPR
				|| ex_form == RSHIFT_EXPR
				|| ex_form == LROTATE_EXPR
				|| ex_form == RROTATE_EXPR))
			|| ex_form == LSHIFT_EXPR
			/* If we have !flag_wrapv, and either ARG0 or
			   ARG1 is of a signed type, we have to do
			   PLUS_EXPR or MINUS_EXPR in an unsigned
			   type.  Otherwise, we would introduce
			   signed-overflow undefinedness.  */
			|| ((!TYPE_OVERFLOW_WRAPS (TREE_TYPE (arg0))
			     || !TYPE_OVERFLOW_WRAPS (TREE_TYPE (arg1)))
			    && (ex_form == PLUS_EXPR
				|| ex_form == MINUS_EXPR)))
		      typex = lang_hooks.types.unsigned_type (typex);
		    else
		      typex = lang_hooks.types.signed_type (typex);
		    return convert (type,
				    fold_build2 (ex_form, typex,
						 convert (typex, arg0),
						 convert (typex, arg1)));
		  }
	      }
	  }
	  break;

	case NEGATE_EXPR:
	case BIT_NOT_EXPR:
	  /* This is not correct for ABS_EXPR,
	     since we must test the sign before truncation.  */
	  {
	    tree typex;

	    /* Don't do unsigned arithmetic where signed was wanted,
	       or vice versa.  */
	    if (TYPE_UNSIGNED (TREE_TYPE (expr)))
	      typex = lang_hooks.types.unsigned_type (type);
	    else
	      typex = lang_hooks.types.signed_type (type);
	    return convert (type,
			    fold_build1 (ex_form, typex,
					 convert (typex,
						  TREE_OPERAND (expr, 0))));
	  }

	case NOP_EXPR:
	  /* Don't introduce a
	     "can't convert between vector values of different size" error.  */
	  if (TREE_CODE (TREE_TYPE (TREE_OPERAND (expr, 0))) == VECTOR_TYPE
	      && (GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (TREE_OPERAND (expr, 0))))
		  != GET_MODE_SIZE (TYPE_MODE (type))))
	    break;
	  /* If truncating after truncating, might as well do all at once.
	     If truncating after extending, we may get rid of wasted work.  */
	  return convert (type, get_unwidened (TREE_OPERAND (expr, 0), type));

	case COND_EXPR:
	  /* It is sometimes worthwhile to push the narrowing down through
	     the conditional and never loses.  */
	  return fold_build3 (COND_EXPR, type, TREE_OPERAND (expr, 0),
			      convert (type, TREE_OPERAND (expr, 1)),
			      convert (type, TREE_OPERAND (expr, 2)));

	default:
	  break;
	}

      return build1 (CONVERT_EXPR, type, expr);

    case REAL_TYPE:
      return build1 (FIX_TRUNC_EXPR, type, expr);

    case COMPLEX_TYPE:
      return convert (type,
		      fold_build1 (REALPART_EXPR,
				   TREE_TYPE (TREE_TYPE (expr)), expr));

    case VECTOR_TYPE:
      if (!tree_int_cst_equal (TYPE_SIZE (type), TYPE_SIZE (TREE_TYPE (expr))))
	{
	  error ("can't convert between vector values of different size");
	  return error_mark_node;
	}
      return build1 (VIEW_CONVERT_EXPR, type, expr);

    default:
      error ("aggregate value used where an integer was expected");
      return convert (type, integer_zero_node);
    }
}

/* Convert EXPR to the complex type TYPE in the usual ways.  */

tree
convert_to_complex (tree type, tree expr)
{
  tree subtype = TREE_TYPE (type);

  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case REAL_TYPE:
    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
    case BOOLEAN_TYPE:
      return build2 (COMPLEX_EXPR, type, convert (subtype, expr),
		     convert (subtype, integer_zero_node));

    case COMPLEX_TYPE:
      {
	tree elt_type = TREE_TYPE (TREE_TYPE (expr));

	if (TYPE_MAIN_VARIANT (elt_type) == TYPE_MAIN_VARIANT (subtype))
	  return expr;
	else if (TREE_CODE (expr) == COMPLEX_EXPR)
	  return fold_build2 (COMPLEX_EXPR, type,
			      convert (subtype, TREE_OPERAND (expr, 0)),
			      convert (subtype, TREE_OPERAND (expr, 1)));
	else
	  {
	    expr = save_expr (expr);
	    return
	      fold_build2 (COMPLEX_EXPR, type,
			   convert (subtype,
				    fold_build1 (REALPART_EXPR,
						 TREE_TYPE (TREE_TYPE (expr)),
						 expr)),
			   convert (subtype,
				    fold_build1 (IMAGPART_EXPR,
						 TREE_TYPE (TREE_TYPE (expr)),
						 expr)));
	  }
      }

    case POINTER_TYPE:
    case REFERENCE_TYPE:
      error ("pointer value used where a complex was expected");
      return convert_to_complex (type, integer_zero_node);

    default:
      error ("aggregate value used where a complex was expected");
      return convert_to_complex (type, integer_zero_node);
    }
}

/* Convert EXPR to the vector type TYPE in the usual ways.  */

tree
convert_to_vector (tree type, tree expr)
{
  switch (TREE_CODE (TREE_TYPE (expr)))
    {
    case INTEGER_TYPE:
    case VECTOR_TYPE:
      if (!tree_int_cst_equal (TYPE_SIZE (type), TYPE_SIZE (TREE_TYPE (expr))))
	{
	  error ("can't convert between vector values of different size");
	  return error_mark_node;
	}
      return build1 (VIEW_CONVERT_EXPR, type, expr);

    default:
      error ("can't convert value to a vector");
      return error_mark_node;
    }
}
