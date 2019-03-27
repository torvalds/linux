/* Functions to analyze and validate GIMPLE trees.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>
   Rewritten by Jason Merrill <jason@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "ggc.h"
#include "tm.h"
#include "tree.h"
#include "tree-gimple.h"
#include "tree-flow.h"
#include "output.h"
#include "rtl.h"
#include "expr.h"
#include "bitmap.h"

/* For the definitive definition of GIMPLE, see doc/tree-ssa.texi.  */

/* Validation of GIMPLE expressions.  */

/* Return true if T is a GIMPLE RHS for an assignment to a temporary.  */

bool
is_gimple_formal_tmp_rhs (tree t)
{
  enum tree_code code = TREE_CODE (t);

  switch (TREE_CODE_CLASS (code))
    {
    case tcc_unary:
    case tcc_binary:
    case tcc_comparison:
      return true;

    default:
      break;
    }

  switch (code)
    {
    case TRUTH_NOT_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_XOR_EXPR:
    case ADDR_EXPR:
    case CALL_EXPR:
    case CONSTRUCTOR:
    case COMPLEX_EXPR:
    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
    case COMPLEX_CST:
    case VECTOR_CST:
    case OBJ_TYPE_REF:
    case ASSERT_EXPR:
      return true;

    default:
      break;
    }

  return is_gimple_lvalue (t) || is_gimple_val (t);
}

/* Returns true iff T is a valid RHS for an assignment to a renamed
   user -- or front-end generated artificial -- variable.  */

bool
is_gimple_reg_rhs (tree t)
{
  /* If the RHS of the MODIFY_EXPR may throw or make a nonlocal goto
     and the LHS is a user variable, then we need to introduce a formal
     temporary.  This way the optimizers can determine that the user
     variable is only modified if evaluation of the RHS does not throw.

     Don't force a temp of a non-renamable type; the copy could be
     arbitrarily expensive.  Instead we will generate a V_MAY_DEF for
     the assignment.  */

  if (is_gimple_reg_type (TREE_TYPE (t))
      && ((TREE_CODE (t) == CALL_EXPR && TREE_SIDE_EFFECTS (t))
	  || tree_could_throw_p (t)))
    return false;

  return is_gimple_formal_tmp_rhs (t);
}

/* Returns true iff T is a valid RHS for an assignment to an un-renamed
   LHS, or for a call argument.  */

bool
is_gimple_mem_rhs (tree t)
{
  /* If we're dealing with a renamable type, either source or dest must be
     a renamed variable.  Also force a temporary if the type doesn't need
     to be stored in memory, since it's cheap and prevents erroneous
     tailcalls (PR 17526).  */
  if (is_gimple_reg_type (TREE_TYPE (t))
      || (TYPE_MODE (TREE_TYPE (t)) != BLKmode
	  && (TREE_CODE (t) != CALL_EXPR
              || ! aggregate_value_p (t, t))))
    return is_gimple_val (t);
  else
    return is_gimple_formal_tmp_rhs (t);
}

/* Returns the appropriate RHS predicate for this LHS.  */

gimple_predicate
rhs_predicate_for (tree lhs)
{
  if (is_gimple_formal_tmp_var (lhs))
    return is_gimple_formal_tmp_rhs;
  else if (is_gimple_reg (lhs))
    return is_gimple_reg_rhs;
  else
    return is_gimple_mem_rhs;
}

/*  Return true if T is a valid LHS for a GIMPLE assignment expression.  */

bool
is_gimple_lvalue (tree t)
{
  return (is_gimple_addressable (t)
	  || TREE_CODE (t) == WITH_SIZE_EXPR
	  /* These are complex lvalues, but don't have addresses, so they
	     go here.  */
	  || TREE_CODE (t) == BIT_FIELD_REF);
}

/*  Return true if T is a GIMPLE condition.  */

bool
is_gimple_condexpr (tree t)
{
  return (is_gimple_val (t) || COMPARISON_CLASS_P (t));
}

/*  Return true if T is something whose address can be taken.  */

bool
is_gimple_addressable (tree t)
{
  return (is_gimple_id (t) || handled_component_p (t)
	  || INDIRECT_REF_P (t));
}

/* Return true if T is function invariant.  Or rather a restricted
   form of function invariant.  */

bool
is_gimple_min_invariant (tree t)
{
  switch (TREE_CODE (t))
    {
    case ADDR_EXPR:
      return TREE_INVARIANT (t);

    case INTEGER_CST:
    case REAL_CST:
    case STRING_CST:
    case COMPLEX_CST:
    case VECTOR_CST:
      return true;

    default:
      return false;
    }
}

/* Return true if T looks like a valid GIMPLE statement.  */

bool
is_gimple_stmt (tree t)
{
  enum tree_code code = TREE_CODE (t);

  switch (code)
    {
    case NOP_EXPR:
      /* The only valid NOP_EXPR is the empty statement.  */
      return IS_EMPTY_STMT (t);

    case BIND_EXPR:
    case COND_EXPR:
      /* These are only valid if they're void.  */
      return TREE_TYPE (t) == NULL || VOID_TYPE_P (TREE_TYPE (t));

    case SWITCH_EXPR:
    case GOTO_EXPR:
    case RETURN_EXPR:
    case LABEL_EXPR:
    case CASE_LABEL_EXPR:
    case TRY_CATCH_EXPR:
    case TRY_FINALLY_EXPR:
    case EH_FILTER_EXPR:
    case CATCH_EXPR:
    case ASM_EXPR:
    case RESX_EXPR:
    case PHI_NODE:
    case STATEMENT_LIST:
    case OMP_PARALLEL:
    case OMP_FOR:
    case OMP_SECTIONS:
    case OMP_SECTION:
    case OMP_SINGLE:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
    case OMP_RETURN:
    case OMP_CONTINUE:
      /* These are always void.  */
      return true;

    case CALL_EXPR:
    case MODIFY_EXPR:
      /* These are valid regardless of their type.  */
      return true;

    default:
      return false;
    }
}

/* Return true if T is a variable.  */

bool
is_gimple_variable (tree t)
{
  return (TREE_CODE (t) == VAR_DECL
	  || TREE_CODE (t) == PARM_DECL
	  || TREE_CODE (t) == RESULT_DECL
	  || TREE_CODE (t) == SSA_NAME);
}

/*  Return true if T is a GIMPLE identifier (something with an address).  */

bool
is_gimple_id (tree t)
{
  return (is_gimple_variable (t)
	  || TREE_CODE (t) == FUNCTION_DECL
	  || TREE_CODE (t) == LABEL_DECL
	  || TREE_CODE (t) == CONST_DECL
	  /* Allow string constants, since they are addressable.  */
	  || TREE_CODE (t) == STRING_CST);
}

/* Return true if TYPE is a suitable type for a scalar register variable.  */

bool
is_gimple_reg_type (tree type)
{
  return !AGGREGATE_TYPE_P (type);
}

/* Return true if T is a non-aggregate register variable.  */

bool
is_gimple_reg (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);

  if (MTAG_P (t))
    return false;

  if (!is_gimple_variable (t))
    return false;

  if (!is_gimple_reg_type (TREE_TYPE (t)))
    return false;

  /* A volatile decl is not acceptable because we can't reuse it as
     needed.  We need to copy it into a temp first.  */
  if (TREE_THIS_VOLATILE (t))
    return false;

  /* We define "registers" as things that can be renamed as needed,
     which with our infrastructure does not apply to memory.  */
  if (needs_to_live_in_memory (t))
    return false;

  /* Hard register variables are an interesting case.  For those that
     are call-clobbered, we don't know where all the calls are, since
     we don't (want to) take into account which operations will turn
     into libcalls at the rtl level.  For those that are call-saved,
     we don't currently model the fact that calls may in fact change
     global hard registers, nor do we examine ASM_CLOBBERS at the tree
     level, and so miss variable changes that might imply.  All around,
     it seems safest to not do too much optimization with these at the
     tree level at all.  We'll have to rely on the rtl optimizers to
     clean this up, as there we've got all the appropriate bits exposed.  */
  if (TREE_CODE (t) == VAR_DECL && DECL_HARD_REGISTER (t))
    return false;

  /* Complex values must have been put into ssa form.  That is, no 
     assignments to the individual components.  */
  if (TREE_CODE (TREE_TYPE (t)) == COMPLEX_TYPE)
    return DECL_COMPLEX_GIMPLE_REG_P (t);

  return true;
}


/* Returns true if T is a GIMPLE formal temporary variable.  */

bool
is_gimple_formal_tmp_var (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    return true;

  return TREE_CODE (t) == VAR_DECL && DECL_GIMPLE_FORMAL_TEMP_P (t);
}

/* Returns true if T is a GIMPLE formal temporary register variable.  */

bool
is_gimple_formal_tmp_reg (tree t)
{
  /* The intent of this is to get hold of a value that won't change.
     An SSA_NAME qualifies no matter if its of a user variable or not.  */
  if (TREE_CODE (t) == SSA_NAME)
    return true;

  /* We don't know the lifetime characteristics of user variables.  */
  if (!is_gimple_formal_tmp_var (t))
    return false;

  /* Finally, it must be capable of being placed in a register.  */
  return is_gimple_reg (t);
}

/* Return true if T is a GIMPLE variable whose address is not needed.  */

bool
is_gimple_non_addressable (tree t)
{
  if (TREE_CODE (t) == SSA_NAME)
    t = SSA_NAME_VAR (t);

  return (is_gimple_variable (t) && ! needs_to_live_in_memory (t));
}

/* Return true if T is a GIMPLE rvalue, i.e. an identifier or a constant.  */

bool
is_gimple_val (tree t)
{
  /* Make loads from volatiles and memory vars explicit.  */
  if (is_gimple_variable (t)
      && is_gimple_reg_type (TREE_TYPE (t))
      && !is_gimple_reg (t))
    return false;

  /* FIXME make these decls.  That can happen only when we expose the
     entire landing-pad construct at the tree level.  */
  if (TREE_CODE (t) == EXC_PTR_EXPR || TREE_CODE (t) == FILTER_EXPR)
    return 1;

  return (is_gimple_variable (t) || is_gimple_min_invariant (t));
}

/* Similarly, but accept hard registers as inputs to asm statements.  */

bool
is_gimple_asm_val (tree t)
{
  if (TREE_CODE (t) == VAR_DECL && DECL_HARD_REGISTER (t))
    return true;

  return is_gimple_val (t);
}

/* Return true if T is a GIMPLE minimal lvalue.  */

bool
is_gimple_min_lval (tree t)
{
  return (is_gimple_id (t)
	  || TREE_CODE (t) == INDIRECT_REF);
}

/* Return true if T is a typecast operation.  */

bool
is_gimple_cast (tree t)
{
  return (TREE_CODE (t) == NOP_EXPR
	  || TREE_CODE (t) == CONVERT_EXPR
          || TREE_CODE (t) == FIX_TRUNC_EXPR
          || TREE_CODE (t) == FIX_CEIL_EXPR
          || TREE_CODE (t) == FIX_FLOOR_EXPR
          || TREE_CODE (t) == FIX_ROUND_EXPR);
}

/* Return true if T is a valid op0 of a CALL_EXPR.  */

bool
is_gimple_call_addr (tree t)
{
  return (TREE_CODE (t) == OBJ_TYPE_REF
	  || is_gimple_val (t));
}

/* If T makes a function call, return the corresponding CALL_EXPR operand.
   Otherwise, return NULL_TREE.  */

tree
get_call_expr_in (tree t)
{
  if (TREE_CODE (t) == MODIFY_EXPR)
    t = TREE_OPERAND (t, 1);
  if (TREE_CODE (t) == WITH_SIZE_EXPR)
    t = TREE_OPERAND (t, 0);
  if (TREE_CODE (t) == CALL_EXPR)
    return t;
  return NULL_TREE;
}

/* Given a memory reference expression T, return its base address.
   The base address of a memory reference expression is the main
   object being referenced.  For instance, the base address for
   'array[i].fld[j]' is 'array'.  You can think of this as stripping
   away the offset part from a memory address.

   This function calls handled_component_p to strip away all the inner
   parts of the memory reference until it reaches the base object.  */

tree
get_base_address (tree t)
{
  while (handled_component_p (t))
    t = TREE_OPERAND (t, 0);
  
  if (SSA_VAR_P (t)
      || TREE_CODE (t) == STRING_CST
      || TREE_CODE (t) == CONSTRUCTOR
      || INDIRECT_REF_P (t))
    return t;
  else
    return NULL_TREE;
}

void
recalculate_side_effects (tree t)
{
  enum tree_code code = TREE_CODE (t);
  int len = TREE_CODE_LENGTH (code);
  int i;

  switch (TREE_CODE_CLASS (code))
    {
    case tcc_expression:
      switch (code)
	{
	case INIT_EXPR:
	case MODIFY_EXPR:
	case VA_ARG_EXPR:
	case PREDECREMENT_EXPR:
	case PREINCREMENT_EXPR:
	case POSTDECREMENT_EXPR:
	case POSTINCREMENT_EXPR:
	  /* All of these have side-effects, no matter what their
	     operands are.  */
	  return;

	default:
	  break;
	}
      /* Fall through.  */

    case tcc_comparison:  /* a comparison expression */
    case tcc_unary:       /* a unary arithmetic expression */
    case tcc_binary:      /* a binary arithmetic expression */
    case tcc_reference:   /* a reference */
      TREE_SIDE_EFFECTS (t) = TREE_THIS_VOLATILE (t);
      for (i = 0; i < len; ++i)
	{
	  tree op = TREE_OPERAND (t, i);
	  if (op && TREE_SIDE_EFFECTS (op))
	    TREE_SIDE_EFFECTS (t) = 1;
	}
      break;

    default:
      /* Can never be used with non-expressions.  */
      gcc_unreachable ();
   }
}
