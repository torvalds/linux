/* Convert tree expression to rtl instructions, for GNU compiler.
   Copyright (C) 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "function.h"
#include "insn-config.h"
#include "insn-attr.h"
/* Include expr.h after insn-config.h so we get HAVE_conditional_move.  */
#include "expr.h"
#include "optabs.h"
#include "langhooks.h"
#include "ggc.h"

static bool prefer_and_bit_test (enum machine_mode, int);
static void do_jump_by_parts_greater (tree, int, rtx, rtx);
static void do_jump_by_parts_equality (tree, rtx, rtx);
static void do_compare_and_jump	(tree, enum rtx_code, enum rtx_code, rtx,
				 rtx);

/* At the start of a function, record that we have no previously-pushed
   arguments waiting to be popped.  */

void
init_pending_stack_adjust (void)
{
  pending_stack_adjust = 0;
}

/* Discard any pending stack adjustment.  This avoid relying on the
   RTL optimizers to remove useless adjustments when we know the
   stack pointer value is dead.  */
void
discard_pending_stack_adjust (void)
{
  stack_pointer_delta -= pending_stack_adjust;
  pending_stack_adjust = 0;
}

/* When exiting from function, if safe, clear out any pending stack adjust
   so the adjustment won't get done.

   Note, if the current function calls alloca, then it must have a
   frame pointer regardless of the value of flag_omit_frame_pointer.  */

void
clear_pending_stack_adjust (void)
{
  if (optimize > 0
      && (! flag_omit_frame_pointer || current_function_calls_alloca)
      && EXIT_IGNORE_STACK
      && ! (DECL_INLINE (current_function_decl) && ! flag_no_inline))
    discard_pending_stack_adjust ();
}

/* Pop any previously-pushed arguments that have not been popped yet.  */

void
do_pending_stack_adjust (void)
{
  if (inhibit_defer_pop == 0)
    {
      if (pending_stack_adjust != 0)
        adjust_stack (GEN_INT (pending_stack_adjust));
      pending_stack_adjust = 0;
    }
}

/* Expand conditional expressions.  */

/* Generate code to evaluate EXP and jump to LABEL if the value is zero.
   LABEL is an rtx of code CODE_LABEL, in this function and all the
   functions here.  */

void
jumpifnot (tree exp, rtx label)
{
  do_jump (exp, label, NULL_RTX);
}

/* Generate code to evaluate EXP and jump to LABEL if the value is nonzero.  */

void
jumpif (tree exp, rtx label)
{
  do_jump (exp, NULL_RTX, label);
}

/* Used internally by prefer_and_bit_test.  */

static GTY(()) rtx and_reg;
static GTY(()) rtx and_test;
static GTY(()) rtx shift_test;

/* Compare the relative costs of "(X & (1 << BITNUM))" and "(X >> BITNUM) & 1",
   where X is an arbitrary register of mode MODE.  Return true if the former
   is preferred.  */

static bool
prefer_and_bit_test (enum machine_mode mode, int bitnum)
{
  if (and_test == 0)
    {
      /* Set up rtxes for the two variations.  Use NULL as a placeholder
	 for the BITNUM-based constants.  */
      and_reg = gen_rtx_REG (mode, FIRST_PSEUDO_REGISTER);
      and_test = gen_rtx_AND (mode, and_reg, NULL);
      shift_test = gen_rtx_AND (mode, gen_rtx_ASHIFTRT (mode, and_reg, NULL),
				const1_rtx);
    }
  else
    {
      /* Change the mode of the previously-created rtxes.  */
      PUT_MODE (and_reg, mode);
      PUT_MODE (and_test, mode);
      PUT_MODE (shift_test, mode);
      PUT_MODE (XEXP (shift_test, 0), mode);
    }

  /* Fill in the integers.  */
  XEXP (and_test, 1) = GEN_INT ((unsigned HOST_WIDE_INT) 1 << bitnum);
  XEXP (XEXP (shift_test, 0), 1) = GEN_INT (bitnum);

  return (rtx_cost (and_test, IF_THEN_ELSE)
	  <= rtx_cost (shift_test, IF_THEN_ELSE));
}

/* Generate code to evaluate EXP and jump to IF_FALSE_LABEL if
   the result is zero, or IF_TRUE_LABEL if the result is one.
   Either of IF_FALSE_LABEL and IF_TRUE_LABEL may be zero,
   meaning fall through in that case.

   do_jump always does any pending stack adjust except when it does not
   actually perform a jump.  An example where there is no jump
   is when EXP is `(foo (), 0)' and IF_FALSE_LABEL is null.  */

void
do_jump (tree exp, rtx if_false_label, rtx if_true_label)
{
  enum tree_code code = TREE_CODE (exp);
  rtx temp;
  int i;
  tree type;
  enum machine_mode mode;
  rtx drop_through_label = 0;

  switch (code)
    {
    case ERROR_MARK:
      break;

    case INTEGER_CST:
      temp = integer_zerop (exp) ? if_false_label : if_true_label;
      if (temp)
        emit_jump (temp);
      break;

#if 0
      /* This is not true with #pragma weak  */
    case ADDR_EXPR:
      /* The address of something can never be zero.  */
      if (if_true_label)
        emit_jump (if_true_label);
      break;
#endif

    case NOP_EXPR:
      if (TREE_CODE (TREE_OPERAND (exp, 0)) == COMPONENT_REF
          || TREE_CODE (TREE_OPERAND (exp, 0)) == BIT_FIELD_REF
          || TREE_CODE (TREE_OPERAND (exp, 0)) == ARRAY_REF
          || TREE_CODE (TREE_OPERAND (exp, 0)) == ARRAY_RANGE_REF)
        goto normal;
    case CONVERT_EXPR:
      /* If we are narrowing the operand, we have to do the compare in the
         narrower mode.  */
      if ((TYPE_PRECISION (TREE_TYPE (exp))
           < TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (exp, 0)))))
        goto normal;
    case NON_LVALUE_EXPR:
    case ABS_EXPR:
    case NEGATE_EXPR:
    case LROTATE_EXPR:
    case RROTATE_EXPR:
      /* These cannot change zero->nonzero or vice versa.  */
      do_jump (TREE_OPERAND (exp, 0), if_false_label, if_true_label);
      break;

    case BIT_AND_EXPR:
      /* fold_single_bit_test() converts (X & (1 << C)) into (X >> C) & 1.
	 See if the former is preferred for jump tests and restore it
	 if so.  */
      if (integer_onep (TREE_OPERAND (exp, 1)))
	{
	  tree exp0 = TREE_OPERAND (exp, 0);
	  rtx set_label, clr_label;

	  /* Strip narrowing integral type conversions.  */
	  while ((TREE_CODE (exp0) == NOP_EXPR
		  || TREE_CODE (exp0) == CONVERT_EXPR
		  || TREE_CODE (exp0) == NON_LVALUE_EXPR)
		 && TREE_OPERAND (exp0, 0) != error_mark_node
		 && TYPE_PRECISION (TREE_TYPE (exp0))
		    <= TYPE_PRECISION (TREE_TYPE (TREE_OPERAND (exp0, 0))))
	    exp0 = TREE_OPERAND (exp0, 0);

	  /* "exp0 ^ 1" inverts the sense of the single bit test.  */
	  if (TREE_CODE (exp0) == BIT_XOR_EXPR
	      && integer_onep (TREE_OPERAND (exp0, 1)))
	    {
	      exp0 = TREE_OPERAND (exp0, 0);
	      clr_label = if_true_label;
	      set_label = if_false_label;
	    }
	  else
	    {
	      clr_label = if_false_label;
	      set_label = if_true_label;
	    }

	  if (TREE_CODE (exp0) == RSHIFT_EXPR)
	    {
	      tree arg = TREE_OPERAND (exp0, 0);
	      tree shift = TREE_OPERAND (exp0, 1);
	      tree argtype = TREE_TYPE (arg);
	      if (TREE_CODE (shift) == INTEGER_CST
		  && compare_tree_int (shift, 0) >= 0
		  && compare_tree_int (shift, HOST_BITS_PER_WIDE_INT) < 0
		  && prefer_and_bit_test (TYPE_MODE (argtype),
					  TREE_INT_CST_LOW (shift)))
		{
		  HOST_WIDE_INT mask = (HOST_WIDE_INT) 1
				       << TREE_INT_CST_LOW (shift);
		  do_jump (build2 (BIT_AND_EXPR, argtype, arg,
				   build_int_cst_type (argtype, mask)),
			   clr_label, set_label);
		  break;
		}
	    }
	}

      /* If we are AND'ing with a small constant, do this comparison in the
         smallest type that fits.  If the machine doesn't have comparisons
         that small, it will be converted back to the wider comparison.
         This helps if we are testing the sign bit of a narrower object.
         combine can't do this for us because it can't know whether a
         ZERO_EXTRACT or a compare in a smaller mode exists, but we do.  */

      if (! SLOW_BYTE_ACCESS
          && TREE_CODE (TREE_OPERAND (exp, 1)) == INTEGER_CST
          && TYPE_PRECISION (TREE_TYPE (exp)) <= HOST_BITS_PER_WIDE_INT
          && (i = tree_floor_log2 (TREE_OPERAND (exp, 1))) >= 0
          && (mode = mode_for_size (i + 1, MODE_INT, 0)) != BLKmode
          && (type = lang_hooks.types.type_for_mode (mode, 1)) != 0
          && TYPE_PRECISION (type) < TYPE_PRECISION (TREE_TYPE (exp))
          && (cmp_optab->handlers[(int) TYPE_MODE (type)].insn_code
              != CODE_FOR_nothing))
        {
          do_jump (fold_convert (type, exp), if_false_label, if_true_label);
          break;
        }
      goto normal;

    case TRUTH_NOT_EXPR:
      do_jump (TREE_OPERAND (exp, 0), if_true_label, if_false_label);
      break;

    case COND_EXPR:
      {
	rtx label1 = gen_label_rtx ();
	if (!if_true_label || !if_false_label)
	  {
	    drop_through_label = gen_label_rtx ();
	    if (!if_true_label)
	      if_true_label = drop_through_label;
	    if (!if_false_label)
	      if_false_label = drop_through_label;
	  }

        do_pending_stack_adjust ();
        do_jump (TREE_OPERAND (exp, 0), label1, NULL_RTX);
        do_jump (TREE_OPERAND (exp, 1), if_false_label, if_true_label);
        emit_label (label1);
        do_jump (TREE_OPERAND (exp, 2), if_false_label, if_true_label);
	break;
      }

    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case COMPOUND_EXPR:
      /* Lowered by gimplify.c.  */
      gcc_unreachable ();

    case COMPONENT_REF:
    case BIT_FIELD_REF:
    case ARRAY_REF:
    case ARRAY_RANGE_REF:
      {
        HOST_WIDE_INT bitsize, bitpos;
        int unsignedp;
        enum machine_mode mode;
        tree type;
        tree offset;
        int volatilep = 0;

        /* Get description of this reference.  We don't actually care
           about the underlying object here.  */
        get_inner_reference (exp, &bitsize, &bitpos, &offset, &mode,
                             &unsignedp, &volatilep, false);

        type = lang_hooks.types.type_for_size (bitsize, unsignedp);
        if (! SLOW_BYTE_ACCESS
            && type != 0 && bitsize >= 0
            && TYPE_PRECISION (type) < TYPE_PRECISION (TREE_TYPE (exp))
            && (cmp_optab->handlers[(int) TYPE_MODE (type)].insn_code
		!= CODE_FOR_nothing))
          {
            do_jump (fold_convert (type, exp), if_false_label, if_true_label);
            break;
          }
        goto normal;
      }

    case EQ_EXPR:
      {
        tree inner_type = TREE_TYPE (TREE_OPERAND (exp, 0));

        gcc_assert (GET_MODE_CLASS (TYPE_MODE (inner_type))
		    != MODE_COMPLEX_FLOAT);
	gcc_assert (GET_MODE_CLASS (TYPE_MODE (inner_type))
		    != MODE_COMPLEX_INT);
	
        if (integer_zerop (TREE_OPERAND (exp, 1)))
          do_jump (TREE_OPERAND (exp, 0), if_true_label, if_false_label);
        else if (GET_MODE_CLASS (TYPE_MODE (inner_type)) == MODE_INT
                 && !can_compare_p (EQ, TYPE_MODE (inner_type), ccp_jump))
          do_jump_by_parts_equality (exp, if_false_label, if_true_label);
        else
          do_compare_and_jump (exp, EQ, EQ, if_false_label, if_true_label);
        break;
      }

    case MINUS_EXPR:
      /* Nonzero iff operands of minus differ.  */
      exp = build2 (NE_EXPR, TREE_TYPE (exp),
		    TREE_OPERAND (exp, 0),
		    TREE_OPERAND (exp, 1));
      /* FALLTHRU */
    case NE_EXPR:
      {
        tree inner_type = TREE_TYPE (TREE_OPERAND (exp, 0));

        gcc_assert (GET_MODE_CLASS (TYPE_MODE (inner_type))
		    != MODE_COMPLEX_FLOAT);
	gcc_assert (GET_MODE_CLASS (TYPE_MODE (inner_type))
		    != MODE_COMPLEX_INT);
	
        if (integer_zerop (TREE_OPERAND (exp, 1)))
          do_jump (TREE_OPERAND (exp, 0), if_false_label, if_true_label);
        else if (GET_MODE_CLASS (TYPE_MODE (inner_type)) == MODE_INT
           && !can_compare_p (NE, TYPE_MODE (inner_type), ccp_jump))
          do_jump_by_parts_equality (exp, if_true_label, if_false_label);
        else
          do_compare_and_jump (exp, NE, NE, if_false_label, if_true_label);
        break;
      }

    case LT_EXPR:
      mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
      if (GET_MODE_CLASS (mode) == MODE_INT
          && ! can_compare_p (LT, mode, ccp_jump))
        do_jump_by_parts_greater (exp, 1, if_false_label, if_true_label);
      else
        do_compare_and_jump (exp, LT, LTU, if_false_label, if_true_label);
      break;

    case LE_EXPR:
      mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
      if (GET_MODE_CLASS (mode) == MODE_INT
          && ! can_compare_p (LE, mode, ccp_jump))
        do_jump_by_parts_greater (exp, 0, if_true_label, if_false_label);
      else
        do_compare_and_jump (exp, LE, LEU, if_false_label, if_true_label);
      break;

    case GT_EXPR:
      mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
      if (GET_MODE_CLASS (mode) == MODE_INT
          && ! can_compare_p (GT, mode, ccp_jump))
        do_jump_by_parts_greater (exp, 0, if_false_label, if_true_label);
      else
        do_compare_and_jump (exp, GT, GTU, if_false_label, if_true_label);
      break;

    case GE_EXPR:
      mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
      if (GET_MODE_CLASS (mode) == MODE_INT
          && ! can_compare_p (GE, mode, ccp_jump))
        do_jump_by_parts_greater (exp, 1, if_true_label, if_false_label);
      else
        do_compare_and_jump (exp, GE, GEU, if_false_label, if_true_label);
      break;

    case UNORDERED_EXPR:
    case ORDERED_EXPR:
      {
        enum rtx_code cmp, rcmp;
        int do_rev;

        if (code == UNORDERED_EXPR)
          cmp = UNORDERED, rcmp = ORDERED;
        else
          cmp = ORDERED, rcmp = UNORDERED;
        mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));

        do_rev = 0;
        if (! can_compare_p (cmp, mode, ccp_jump)
            && (can_compare_p (rcmp, mode, ccp_jump)
          /* If the target doesn't provide either UNORDERED or ORDERED
             comparisons, canonicalize on UNORDERED for the library.  */
          || rcmp == UNORDERED))
          do_rev = 1;

        if (! do_rev)
          do_compare_and_jump (exp, cmp, cmp, if_false_label, if_true_label);
        else
          do_compare_and_jump (exp, rcmp, rcmp, if_true_label, if_false_label);
      }
      break;

    {
      enum rtx_code rcode1;
      enum tree_code tcode1, tcode2;

      case UNLT_EXPR:
        rcode1 = UNLT;
        tcode1 = UNORDERED_EXPR;
        tcode2 = LT_EXPR;
        goto unordered_bcc;
      case UNLE_EXPR:
        rcode1 = UNLE;
        tcode1 = UNORDERED_EXPR;
        tcode2 = LE_EXPR;
        goto unordered_bcc;
      case UNGT_EXPR:
        rcode1 = UNGT;
        tcode1 = UNORDERED_EXPR;
        tcode2 = GT_EXPR;
        goto unordered_bcc;
      case UNGE_EXPR:
        rcode1 = UNGE;
        tcode1 = UNORDERED_EXPR;
        tcode2 = GE_EXPR;
        goto unordered_bcc;
      case UNEQ_EXPR:
        rcode1 = UNEQ;
        tcode1 = UNORDERED_EXPR;
        tcode2 = EQ_EXPR;
        goto unordered_bcc;
      case LTGT_EXPR:
	/* It is ok for LTGT_EXPR to trap when the result is unordered,
	   so expand to (a < b) || (a > b).  */
        rcode1 = LTGT;
        tcode1 = LT_EXPR;
        tcode2 = GT_EXPR;
        goto unordered_bcc;

      unordered_bcc:
        mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
        if (can_compare_p (rcode1, mode, ccp_jump))
          do_compare_and_jump (exp, rcode1, rcode1, if_false_label,
                               if_true_label);
        else
          {
            tree op0 = save_expr (TREE_OPERAND (exp, 0));
            tree op1 = save_expr (TREE_OPERAND (exp, 1));
            tree cmp0, cmp1;

            /* If the target doesn't support combined unordered
               compares, decompose into two comparisons.  */
	    if (if_true_label == 0)
	      drop_through_label = if_true_label = gen_label_rtx ();
	      
            cmp0 = fold_build2 (tcode1, TREE_TYPE (exp), op0, op1);
            cmp1 = fold_build2 (tcode2, TREE_TYPE (exp), op0, op1);
	    do_jump (cmp0, 0, if_true_label);
	    do_jump (cmp1, if_false_label, if_true_label);
          }
      }
      break;

    case TRUTH_AND_EXPR:
      /* High branch cost, expand as the bitwise AND of the conditions.
	 Do the same if the RHS has side effects, because we're effectively
	 turning a TRUTH_AND_EXPR into a TRUTH_ANDIF_EXPR.  */
      if (BRANCH_COST >= 4 || TREE_SIDE_EFFECTS (TREE_OPERAND (exp, 1)))
	goto normal;

      if (if_false_label == NULL_RTX)
        {
	  drop_through_label = gen_label_rtx ();
          do_jump (TREE_OPERAND (exp, 0), drop_through_label, NULL_RTX);
          do_jump (TREE_OPERAND (exp, 1), NULL_RTX, if_true_label);
	}
      else
	{
	  do_jump (TREE_OPERAND (exp, 0), if_false_label, NULL_RTX);
          do_jump (TREE_OPERAND (exp, 1), if_false_label, if_true_label);
	}
      break;

    case TRUTH_OR_EXPR:
      /* High branch cost, expand as the bitwise OR of the conditions.
	 Do the same if the RHS has side effects, because we're effectively
	 turning a TRUTH_OR_EXPR into a TRUTH_ORIF_EXPR.  */
      if (BRANCH_COST >= 4 || TREE_SIDE_EFFECTS (TREE_OPERAND (exp, 1)))
	goto normal;

      if (if_true_label == NULL_RTX)
	{
          drop_through_label = gen_label_rtx ();
          do_jump (TREE_OPERAND (exp, 0), NULL_RTX, drop_through_label);
          do_jump (TREE_OPERAND (exp, 1), if_false_label, NULL_RTX);
	}
      else
	{
          do_jump (TREE_OPERAND (exp, 0), NULL_RTX, if_true_label);
          do_jump (TREE_OPERAND (exp, 1), if_false_label, if_true_label);
	}
      break;

      /* Special case:
          __builtin_expect (<test>, 0)	and
          __builtin_expect (<test>, 1)

         We need to do this here, so that <test> is not converted to a SCC
         operation on machines that use condition code registers and COMPARE
         like the PowerPC, and then the jump is done based on whether the SCC
         operation produced a 1 or 0.  */
    case CALL_EXPR:
      /* Check for a built-in function.  */
      {
	tree fndecl = get_callee_fndecl (exp);
	tree arglist = TREE_OPERAND (exp, 1);

	if (fndecl
	    && DECL_BUILT_IN_CLASS (fndecl) == BUILT_IN_NORMAL
	    && DECL_FUNCTION_CODE (fndecl) == BUILT_IN_EXPECT
	    && arglist != NULL_TREE
	    && TREE_CHAIN (arglist) != NULL_TREE)
	  {
	    rtx seq = expand_builtin_expect_jump (exp, if_false_label,
						  if_true_label);

	    if (seq != NULL_RTX)
	      {
		emit_insn (seq);
		return;
	      }
	  }
      }
 
      /* Fall through and generate the normal code.  */
    default:
    normal:
      temp = expand_normal (exp);
      do_pending_stack_adjust ();
      /* The RTL optimizers prefer comparisons against pseudos.  */
      if (GET_CODE (temp) == SUBREG)
	{
	  /* Compare promoted variables in their promoted mode.  */
	  if (SUBREG_PROMOTED_VAR_P (temp)
	      && REG_P (XEXP (temp, 0)))
	    temp = XEXP (temp, 0);
	  else
	    temp = copy_to_reg (temp);
	}
      do_compare_rtx_and_jump (temp, CONST0_RTX (GET_MODE (temp)),
			       NE, TYPE_UNSIGNED (TREE_TYPE (exp)),
			       GET_MODE (temp), NULL_RTX,
			       if_false_label, if_true_label);
    }

  if (drop_through_label)
    {
      do_pending_stack_adjust ();
      emit_label (drop_through_label);
    }
}

/* Compare OP0 with OP1, word at a time, in mode MODE.
   UNSIGNEDP says to do unsigned comparison.
   Jump to IF_TRUE_LABEL if OP0 is greater, IF_FALSE_LABEL otherwise.  */

static void
do_jump_by_parts_greater_rtx (enum machine_mode mode, int unsignedp, rtx op0,
			      rtx op1, rtx if_false_label, rtx if_true_label)
{
  int nwords = (GET_MODE_SIZE (mode) / UNITS_PER_WORD);
  rtx drop_through_label = 0;
  int i;

  if (! if_true_label || ! if_false_label)
    drop_through_label = gen_label_rtx ();
  if (! if_true_label)
    if_true_label = drop_through_label;
  if (! if_false_label)
    if_false_label = drop_through_label;

  /* Compare a word at a time, high order first.  */
  for (i = 0; i < nwords; i++)
    {
      rtx op0_word, op1_word;

      if (WORDS_BIG_ENDIAN)
        {
          op0_word = operand_subword_force (op0, i, mode);
          op1_word = operand_subword_force (op1, i, mode);
        }
      else
        {
          op0_word = operand_subword_force (op0, nwords - 1 - i, mode);
          op1_word = operand_subword_force (op1, nwords - 1 - i, mode);
        }

      /* All but high-order word must be compared as unsigned.  */
      do_compare_rtx_and_jump (op0_word, op1_word, GT,
                               (unsignedp || i > 0), word_mode, NULL_RTX,
                               NULL_RTX, if_true_label);

      /* Consider lower words only if these are equal.  */
      do_compare_rtx_and_jump (op0_word, op1_word, NE, unsignedp, word_mode,
                               NULL_RTX, NULL_RTX, if_false_label);
    }

  if (if_false_label)
    emit_jump (if_false_label);
  if (drop_through_label)
    emit_label (drop_through_label);
}

/* Given a comparison expression EXP for values too wide to be compared
   with one insn, test the comparison and jump to the appropriate label.
   The code of EXP is ignored; we always test GT if SWAP is 0,
   and LT if SWAP is 1.  */

static void
do_jump_by_parts_greater (tree exp, int swap, rtx if_false_label,
			  rtx if_true_label)
{
  rtx op0 = expand_normal (TREE_OPERAND (exp, swap));
  rtx op1 = expand_normal (TREE_OPERAND (exp, !swap));
  enum machine_mode mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
  int unsignedp = TYPE_UNSIGNED (TREE_TYPE (TREE_OPERAND (exp, 0)));

  do_jump_by_parts_greater_rtx (mode, unsignedp, op0, op1, if_false_label,
				if_true_label);
}

/* Jump according to whether OP0 is 0.  We assume that OP0 has an integer
   mode, MODE, that is too wide for the available compare insns.  Either
   Either (but not both) of IF_TRUE_LABEL and IF_FALSE_LABEL may be NULL_RTX
   to indicate drop through.  */

static void
do_jump_by_parts_zero_rtx (enum machine_mode mode, rtx op0,
			   rtx if_false_label, rtx if_true_label)
{
  int nwords = GET_MODE_SIZE (mode) / UNITS_PER_WORD;
  rtx part;
  int i;
  rtx drop_through_label = 0;

  /* The fastest way of doing this comparison on almost any machine is to
     "or" all the words and compare the result.  If all have to be loaded
     from memory and this is a very wide item, it's possible this may
     be slower, but that's highly unlikely.  */

  part = gen_reg_rtx (word_mode);
  emit_move_insn (part, operand_subword_force (op0, 0, GET_MODE (op0)));
  for (i = 1; i < nwords && part != 0; i++)
    part = expand_binop (word_mode, ior_optab, part,
                         operand_subword_force (op0, i, GET_MODE (op0)),
                         part, 1, OPTAB_WIDEN);

  if (part != 0)
    {
      do_compare_rtx_and_jump (part, const0_rtx, EQ, 1, word_mode,
                               NULL_RTX, if_false_label, if_true_label);

      return;
    }

  /* If we couldn't do the "or" simply, do this with a series of compares.  */
  if (! if_false_label)
    drop_through_label = if_false_label = gen_label_rtx ();

  for (i = 0; i < nwords; i++)
    do_compare_rtx_and_jump (operand_subword_force (op0, i, GET_MODE (op0)),
                             const0_rtx, EQ, 1, word_mode, NULL_RTX,
                             if_false_label, NULL_RTX);

  if (if_true_label)
    emit_jump (if_true_label);

  if (drop_through_label)
    emit_label (drop_through_label);
}

/* Test for the equality of two RTX expressions OP0 and OP1 in mode MODE,
   where MODE is an integer mode too wide to be compared with one insn.
   Either (but not both) of IF_TRUE_LABEL and IF_FALSE_LABEL may be NULL_RTX
   to indicate drop through.  */

static void
do_jump_by_parts_equality_rtx (enum machine_mode mode, rtx op0, rtx op1,
			       rtx if_false_label, rtx if_true_label)
{
  int nwords = (GET_MODE_SIZE (mode) / UNITS_PER_WORD);
  rtx drop_through_label = 0;
  int i;

  if (op1 == const0_rtx)
    {
      do_jump_by_parts_zero_rtx (mode, op0, if_false_label, if_true_label);
      return;
    }
  else if (op0 == const0_rtx)
    {
      do_jump_by_parts_zero_rtx (mode, op1, if_false_label, if_true_label);
      return;
    }

  if (! if_false_label)
    drop_through_label = if_false_label = gen_label_rtx ();

  for (i = 0; i < nwords; i++)
    do_compare_rtx_and_jump (operand_subword_force (op0, i, mode),
                             operand_subword_force (op1, i, mode),
                             EQ, 0, word_mode, NULL_RTX,
			     if_false_label, NULL_RTX);

  if (if_true_label)
    emit_jump (if_true_label);
  if (drop_through_label)
    emit_label (drop_through_label);
}

/* Given an EQ_EXPR expression EXP for values too wide to be compared
   with one insn, test the comparison and jump to the appropriate label.  */

static void
do_jump_by_parts_equality (tree exp, rtx if_false_label, rtx if_true_label)
{
  rtx op0 = expand_normal (TREE_OPERAND (exp, 0));
  rtx op1 = expand_normal (TREE_OPERAND (exp, 1));
  enum machine_mode mode = TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp, 0)));
  do_jump_by_parts_equality_rtx (mode, op0, op1, if_false_label,
				 if_true_label);
}

/* Generate code for a comparison of OP0 and OP1 with rtx code CODE.
   MODE is the machine mode of the comparison, not of the result.
   (including code to compute the values to be compared) and set CC0
   according to the result.  The decision as to signed or unsigned
   comparison must be made by the caller.

   We force a stack adjustment unless there are currently
   things pushed on the stack that aren't yet used.

   If MODE is BLKmode, SIZE is an RTX giving the size of the objects being
   compared.  */

rtx
compare_from_rtx (rtx op0, rtx op1, enum rtx_code code, int unsignedp,
		  enum machine_mode mode, rtx size)
{
  rtx tem;

  /* If one operand is constant, make it the second one.  Only do this
     if the other operand is not constant as well.  */

  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_condition (code);
    }

  do_pending_stack_adjust ();

  code = unsignedp ? unsigned_condition (code) : code;
  tem = simplify_relational_operation (code, VOIDmode, mode, op0, op1);
  if (tem)
    {
      if (CONSTANT_P (tem))
	return tem;

      if (COMPARISON_P (tem))
	{
	  code = GET_CODE (tem);
	  op0 = XEXP (tem, 0);
	  op1 = XEXP (tem, 1);
	  mode = GET_MODE (op0);
	  unsignedp = (code == GTU || code == LTU
		       || code == GEU || code == LEU);
	}
    }

  emit_cmp_insn (op0, op1, code, size, mode, unsignedp);

#if HAVE_cc0
  return gen_rtx_fmt_ee (code, VOIDmode, cc0_rtx, const0_rtx);
#else
  return gen_rtx_fmt_ee (code, VOIDmode, op0, op1);
#endif
}

/* Like do_compare_and_jump but expects the values to compare as two rtx's.
   The decision as to signed or unsigned comparison must be made by the caller.

   If MODE is BLKmode, SIZE is an RTX giving the size of the objects being
   compared.  */

void
do_compare_rtx_and_jump (rtx op0, rtx op1, enum rtx_code code, int unsignedp,
			 enum machine_mode mode, rtx size, rtx if_false_label,
			 rtx if_true_label)
{
  rtx tem;
  int dummy_true_label = 0;

  /* Reverse the comparison if that is safe and we want to jump if it is
     false.  */
  if (! if_true_label && ! FLOAT_MODE_P (mode))
    {
      if_true_label = if_false_label;
      if_false_label = 0;
      code = reverse_condition (code);
    }

  /* If one operand is constant, make it the second one.  Only do this
     if the other operand is not constant as well.  */

  if (swap_commutative_operands_p (op0, op1))
    {
      tem = op0;
      op0 = op1;
      op1 = tem;
      code = swap_condition (code);
    }

  do_pending_stack_adjust ();

  code = unsignedp ? unsigned_condition (code) : code;
  if (0 != (tem = simplify_relational_operation (code, mode, VOIDmode,
						 op0, op1)))
    {
      if (CONSTANT_P (tem))
	{
	  rtx label = (tem == const0_rtx || tem == CONST0_RTX (mode))
		      ? if_false_label : if_true_label;
	  if (label)
	    emit_jump (label);
	  return;
	}

      code = GET_CODE (tem);
      mode = GET_MODE (tem);
      op0 = XEXP (tem, 0);
      op1 = XEXP (tem, 1);
      unsignedp = (code == GTU || code == LTU || code == GEU || code == LEU);
    }


  if (! if_true_label)
    {
      dummy_true_label = 1;
      if_true_label = gen_label_rtx ();
    }

  if (GET_MODE_CLASS (mode) == MODE_INT
      && ! can_compare_p (code, mode, ccp_jump))
    {
      switch (code)
	{
	case LTU:
	  do_jump_by_parts_greater_rtx (mode, 1, op1, op0,
					if_false_label, if_true_label);
	  break;

	case LEU:
	  do_jump_by_parts_greater_rtx (mode, 1, op0, op1,
					if_true_label, if_false_label);
	  break;

	case GTU:
	  do_jump_by_parts_greater_rtx (mode, 1, op0, op1,
					if_false_label, if_true_label);
	  break;

	case GEU:
	  do_jump_by_parts_greater_rtx (mode, 1, op1, op0,
					if_true_label, if_false_label);
	  break;

	case LT:
	  do_jump_by_parts_greater_rtx (mode, 0, op1, op0,
					if_false_label, if_true_label);
	  break;

	case LE:
	  do_jump_by_parts_greater_rtx (mode, 0, op0, op1,
					if_true_label, if_false_label);
	  break;

	case GT:
	  do_jump_by_parts_greater_rtx (mode, 0, op0, op1,
					if_false_label, if_true_label);
	  break;

	case GE:
	  do_jump_by_parts_greater_rtx (mode, 0, op1, op0,
					if_true_label, if_false_label);
	  break;

	case EQ:
	  do_jump_by_parts_equality_rtx (mode, op0, op1, if_false_label,
					 if_true_label);
	  break;

	case NE:
	  do_jump_by_parts_equality_rtx (mode, op0, op1, if_true_label,
					 if_false_label);
	  break;

	default:
	  gcc_unreachable ();
	}
    }
  else
    emit_cmp_and_jump_insns (op0, op1, code, size, mode, unsignedp,
			     if_true_label);

  if (if_false_label)
    emit_jump (if_false_label);
  if (dummy_true_label)
    emit_label (if_true_label);
}

/* Generate code for a comparison expression EXP (including code to compute
   the values to be compared) and a conditional jump to IF_FALSE_LABEL and/or
   IF_TRUE_LABEL.  One of the labels can be NULL_RTX, in which case the
   generated code will drop through.
   SIGNED_CODE should be the rtx operation for this comparison for
   signed data; UNSIGNED_CODE, likewise for use if data is unsigned.

   We force a stack adjustment unless there are currently
   things pushed on the stack that aren't yet used.  */

static void
do_compare_and_jump (tree exp, enum rtx_code signed_code,
		     enum rtx_code unsigned_code, rtx if_false_label,
		     rtx if_true_label)
{
  rtx op0, op1;
  tree type;
  enum machine_mode mode;
  int unsignedp;
  enum rtx_code code;

  /* Don't crash if the comparison was erroneous.  */
  op0 = expand_normal (TREE_OPERAND (exp, 0));
  if (TREE_CODE (TREE_OPERAND (exp, 0)) == ERROR_MARK)
    return;

  op1 = expand_normal (TREE_OPERAND (exp, 1));
  if (TREE_CODE (TREE_OPERAND (exp, 1)) == ERROR_MARK)
    return;

  type = TREE_TYPE (TREE_OPERAND (exp, 0));
  mode = TYPE_MODE (type);
  if (TREE_CODE (TREE_OPERAND (exp, 0)) == INTEGER_CST
      && (TREE_CODE (TREE_OPERAND (exp, 1)) != INTEGER_CST
          || (GET_MODE_BITSIZE (mode)
              > GET_MODE_BITSIZE (TYPE_MODE (TREE_TYPE (TREE_OPERAND (exp,
                                                                      1)))))))
    {
      /* op0 might have been replaced by promoted constant, in which
         case the type of second argument should be used.  */
      type = TREE_TYPE (TREE_OPERAND (exp, 1));
      mode = TYPE_MODE (type);
    }
  unsignedp = TYPE_UNSIGNED (type);
  code = unsignedp ? unsigned_code : signed_code;

#ifdef HAVE_canonicalize_funcptr_for_compare
  /* If function pointers need to be "canonicalized" before they can
     be reliably compared, then canonicalize them.
     Only do this if *both* sides of the comparison are function pointers.
     If one side isn't, we want a noncanonicalized comparison.  See PR
     middle-end/17564.  */
  if (HAVE_canonicalize_funcptr_for_compare
      && TREE_CODE (TREE_TYPE (TREE_OPERAND (exp, 0))) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (TREE_TYPE (TREE_OPERAND (exp, 0))))
          == FUNCTION_TYPE
      && TREE_CODE (TREE_TYPE (TREE_OPERAND (exp, 1))) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (TREE_TYPE (TREE_OPERAND (exp, 1))))
          == FUNCTION_TYPE)
    {
      rtx new_op0 = gen_reg_rtx (mode);
      rtx new_op1 = gen_reg_rtx (mode);

      emit_insn (gen_canonicalize_funcptr_for_compare (new_op0, op0));
      op0 = new_op0;

      emit_insn (gen_canonicalize_funcptr_for_compare (new_op1, op1));
      op1 = new_op1;
    }
#endif

  do_compare_rtx_and_jump (op0, op1, code, unsignedp, mode,
                           ((mode == BLKmode)
                            ? expr_size (TREE_OPERAND (exp, 0)) : NULL_RTX),
                           if_false_label, if_true_label);
}

#include "gt-dojump.h"
