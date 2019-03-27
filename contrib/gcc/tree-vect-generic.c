/* Lower vector operations to scalar operations.
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
#include "tree.h"
#include "tm.h"
#include "rtl.h"
#include "expr.h"
#include "insn-codes.h"
#include "diagnostic.h"
#include "optabs.h"
#include "machmode.h"
#include "langhooks.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "flags.h"
#include "ggc.h"


/* Build a constant of type TYPE, made of VALUE's bits replicated
   every TYPE_SIZE (INNER_TYPE) bits to fit TYPE's precision.  */
static tree
build_replicated_const (tree type, tree inner_type, HOST_WIDE_INT value)
{
  int width = tree_low_cst (TYPE_SIZE (inner_type), 1);
  int n = HOST_BITS_PER_WIDE_INT / width;
  unsigned HOST_WIDE_INT low, high, mask;
  tree ret;

  gcc_assert (n);

  if (width == HOST_BITS_PER_WIDE_INT)
    low = value;
  else
    {
      mask = ((HOST_WIDE_INT)1 << width) - 1;
      low = (unsigned HOST_WIDE_INT) ~0 / mask * (value & mask);
    }

  if (TYPE_PRECISION (type) < HOST_BITS_PER_WIDE_INT)
    low &= ((HOST_WIDE_INT)1 << TYPE_PRECISION (type)) - 1, high = 0;
  else if (TYPE_PRECISION (type) == HOST_BITS_PER_WIDE_INT)
    high = 0;
  else if (TYPE_PRECISION (type) == 2 * HOST_BITS_PER_WIDE_INT)
    high = low;
  else
    gcc_unreachable ();

  ret = build_int_cst_wide (type, low, high);
  return ret;
}

static GTY(()) tree vector_inner_type;
static GTY(()) tree vector_last_type;
static GTY(()) int vector_last_nunits;

/* Return a suitable vector types made of SUBPARTS units each of mode
   "word_mode" (the global variable).  */
static tree
build_word_mode_vector_type (int nunits)
{
  if (!vector_inner_type)
    vector_inner_type = lang_hooks.types.type_for_mode (word_mode, 1);
  else if (vector_last_nunits == nunits)
    {
      gcc_assert (TREE_CODE (vector_last_type) == VECTOR_TYPE);
      return vector_last_type;
    }

  /* We build a new type, but we canonicalize it nevertheless,
     because it still saves some memory.  */
  vector_last_nunits = nunits;
  vector_last_type = type_hash_canon (nunits,
				      build_vector_type (vector_inner_type,
							 nunits));
  return vector_last_type;
}

typedef tree (*elem_op_func) (block_stmt_iterator *,
			      tree, tree, tree, tree, tree, enum tree_code);

static inline tree
tree_vec_extract (block_stmt_iterator *bsi, tree type,
		  tree t, tree bitsize, tree bitpos)
{
  if (bitpos)
    return gimplify_build3 (bsi, BIT_FIELD_REF, type, t, bitsize, bitpos);
  else
    return gimplify_build1 (bsi, VIEW_CONVERT_EXPR, type, t);
}

static tree
do_unop (block_stmt_iterator *bsi, tree inner_type, tree a,
	 tree b ATTRIBUTE_UNUSED, tree bitpos, tree bitsize,
	 enum tree_code code)
{
  a = tree_vec_extract (bsi, inner_type, a, bitsize, bitpos);
  return gimplify_build1 (bsi, code, inner_type, a);
}

static tree
do_binop (block_stmt_iterator *bsi, tree inner_type, tree a, tree b,
	  tree bitpos, tree bitsize, enum tree_code code)
{
  a = tree_vec_extract (bsi, inner_type, a, bitsize, bitpos);
  b = tree_vec_extract (bsi, inner_type, b, bitsize, bitpos);
  return gimplify_build2 (bsi, code, inner_type, a, b);
}

/* Expand vector addition to scalars.  This does bit twiddling
   in order to increase parallelism:

   a + b = (((int) a & 0x7f7f7f7f) + ((int) b & 0x7f7f7f7f)) ^
           (a ^ b) & 0x80808080

   a - b =  (((int) a | 0x80808080) - ((int) b & 0x7f7f7f7f)) ^
            (a ^ ~b) & 0x80808080

   -b = (0x80808080 - ((int) b & 0x7f7f7f7f)) ^ (~b & 0x80808080)

   This optimization should be done only if 4 vector items or more
   fit into a word.  */
static tree
do_plus_minus (block_stmt_iterator *bsi, tree word_type, tree a, tree b,
	       tree bitpos ATTRIBUTE_UNUSED, tree bitsize ATTRIBUTE_UNUSED,
	       enum tree_code code)
{
  tree inner_type = TREE_TYPE (TREE_TYPE (a));
  unsigned HOST_WIDE_INT max;
  tree low_bits, high_bits, a_low, b_low, result_low, signs;

  max = GET_MODE_MASK (TYPE_MODE (inner_type));
  low_bits = build_replicated_const (word_type, inner_type, max >> 1);
  high_bits = build_replicated_const (word_type, inner_type, max & ~(max >> 1));

  a = tree_vec_extract (bsi, word_type, a, bitsize, bitpos);
  b = tree_vec_extract (bsi, word_type, b, bitsize, bitpos);

  signs = gimplify_build2 (bsi, BIT_XOR_EXPR, word_type, a, b);
  b_low = gimplify_build2 (bsi, BIT_AND_EXPR, word_type, b, low_bits);
  if (code == PLUS_EXPR)
    a_low = gimplify_build2 (bsi, BIT_AND_EXPR, word_type, a, low_bits);
  else
    {
      a_low = gimplify_build2 (bsi, BIT_IOR_EXPR, word_type, a, high_bits);
      signs = gimplify_build1 (bsi, BIT_NOT_EXPR, word_type, signs);
    }

  signs = gimplify_build2 (bsi, BIT_AND_EXPR, word_type, signs, high_bits);
  result_low = gimplify_build2 (bsi, code, word_type, a_low, b_low);
  return gimplify_build2 (bsi, BIT_XOR_EXPR, word_type, result_low, signs);
}

static tree
do_negate (block_stmt_iterator *bsi, tree word_type, tree b,
	   tree unused ATTRIBUTE_UNUSED, tree bitpos ATTRIBUTE_UNUSED,
	   tree bitsize ATTRIBUTE_UNUSED,
	   enum tree_code code ATTRIBUTE_UNUSED)
{
  tree inner_type = TREE_TYPE (TREE_TYPE (b));
  HOST_WIDE_INT max;
  tree low_bits, high_bits, b_low, result_low, signs;

  max = GET_MODE_MASK (TYPE_MODE (inner_type));
  low_bits = build_replicated_const (word_type, inner_type, max >> 1);
  high_bits = build_replicated_const (word_type, inner_type, max & ~(max >> 1));

  b = tree_vec_extract (bsi, word_type, b, bitsize, bitpos);

  b_low = gimplify_build2 (bsi, BIT_AND_EXPR, word_type, b, low_bits);
  signs = gimplify_build1 (bsi, BIT_NOT_EXPR, word_type, b);
  signs = gimplify_build2 (bsi, BIT_AND_EXPR, word_type, signs, high_bits);
  result_low = gimplify_build2 (bsi, MINUS_EXPR, word_type, high_bits, b_low);
  return gimplify_build2 (bsi, BIT_XOR_EXPR, word_type, result_low, signs);
}

/* Expand a vector operation to scalars, by using many operations
   whose type is the vector type's inner type.  */
static tree
expand_vector_piecewise (block_stmt_iterator *bsi, elem_op_func f,
			 tree type, tree inner_type,
			 tree a, tree b, enum tree_code code)
{
  VEC(constructor_elt,gc) *v;
  tree part_width = TYPE_SIZE (inner_type);
  tree index = bitsize_int (0);
  int nunits = TYPE_VECTOR_SUBPARTS (type);
  int delta = tree_low_cst (part_width, 1)
	      / tree_low_cst (TYPE_SIZE (TREE_TYPE (type)), 1);
  int i;

  v = VEC_alloc(constructor_elt, gc, (nunits + delta - 1) / delta);
  for (i = 0; i < nunits;
       i += delta, index = int_const_binop (PLUS_EXPR, index, part_width, 0))
    {
      tree result = f (bsi, inner_type, a, b, index, part_width, code);
      constructor_elt *ce = VEC_quick_push (constructor_elt, v, NULL);
      ce->index = NULL_TREE;
      ce->value = result;
    }

  return build_constructor (type, v);
}

/* Expand a vector operation to scalars with the freedom to use
   a scalar integer type, or to use a different size for the items
   in the vector type.  */
static tree
expand_vector_parallel (block_stmt_iterator *bsi, elem_op_func f, tree type,
			tree a, tree b,
			enum tree_code code)
{
  tree result, compute_type;
  enum machine_mode mode;
  int n_words = tree_low_cst (TYPE_SIZE_UNIT (type), 1) / UNITS_PER_WORD;

  /* We have three strategies.  If the type is already correct, just do
     the operation an element at a time.  Else, if the vector is wider than
     one word, do it a word at a time; finally, if the vector is smaller
     than one word, do it as a scalar.  */
  if (TYPE_MODE (TREE_TYPE (type)) == word_mode)
     return expand_vector_piecewise (bsi, f,
				     type, TREE_TYPE (type),
				     a, b, code);
  else if (n_words > 1)
    {
      tree word_type = build_word_mode_vector_type (n_words);
      result = expand_vector_piecewise (bsi, f,
				        word_type, TREE_TYPE (word_type),
					a, b, code);
      result = gimplify_val (bsi, word_type, result);
    }
  else
    {
      /* Use a single scalar operation with a mode no wider than word_mode.  */
      mode = mode_for_size (tree_low_cst (TYPE_SIZE (type), 1), MODE_INT, 0);
      compute_type = lang_hooks.types.type_for_mode (mode, 1);
      result = f (bsi, compute_type, a, b, NULL_TREE, NULL_TREE, code);
    }

  return result;
}

/* Expand a vector operation to scalars; for integer types we can use
   special bit twiddling tricks to do the sums a word at a time, using
   function F_PARALLEL instead of F.  These tricks are done only if
   they can process at least four items, that is, only if the vector
   holds at least four items and if a word can hold four items.  */
static tree
expand_vector_addition (block_stmt_iterator *bsi,
			elem_op_func f, elem_op_func f_parallel,
			tree type, tree a, tree b, enum tree_code code)
{
  int parts_per_word = UNITS_PER_WORD
	  	       / tree_low_cst (TYPE_SIZE_UNIT (TREE_TYPE (type)), 1);

  if (INTEGRAL_TYPE_P (TREE_TYPE (type))
      && parts_per_word >= 4
      && TYPE_VECTOR_SUBPARTS (type) >= 4)
    return expand_vector_parallel (bsi, f_parallel,
				   type, a, b, code);
  else
    return expand_vector_piecewise (bsi, f,
				    type, TREE_TYPE (type),
				    a, b, code);
}

static tree
expand_vector_operation (block_stmt_iterator *bsi, tree type, tree compute_type,
			 tree rhs, enum tree_code code)
{
  enum machine_mode compute_mode = TYPE_MODE (compute_type);

  /* If the compute mode is not a vector mode (hence we are not decomposing
     a BLKmode vector to smaller, hardware-supported vectors), we may want
     to expand the operations in parallel.  */
  if (GET_MODE_CLASS (compute_mode) != MODE_VECTOR_INT
      && GET_MODE_CLASS (compute_mode) != MODE_VECTOR_FLOAT)
    switch (code)
      {
      case PLUS_EXPR:
      case MINUS_EXPR:
        if (!TYPE_OVERFLOW_TRAPS (type))
          return expand_vector_addition (bsi, do_binop, do_plus_minus, type,
		      		         TREE_OPERAND (rhs, 0),
					 TREE_OPERAND (rhs, 1), code);
	break;

      case NEGATE_EXPR:
        if (!TYPE_OVERFLOW_TRAPS (type))
          return expand_vector_addition (bsi, do_unop, do_negate, type,
		      		         TREE_OPERAND (rhs, 0),
					 NULL_TREE, code);
	break;

      case BIT_AND_EXPR:
      case BIT_IOR_EXPR:
      case BIT_XOR_EXPR:
        return expand_vector_parallel (bsi, do_binop, type,
		      		       TREE_OPERAND (rhs, 0),
				       TREE_OPERAND (rhs, 1), code);

      case BIT_NOT_EXPR:
        return expand_vector_parallel (bsi, do_unop, type,
		      		       TREE_OPERAND (rhs, 0),
				       NULL_TREE, code);

      default:
	break;
      }

  if (TREE_CODE_CLASS (code) == tcc_unary)
    return expand_vector_piecewise (bsi, do_unop, type, compute_type,
				    TREE_OPERAND (rhs, 0),
				    NULL_TREE, code);
  else
    return expand_vector_piecewise (bsi, do_binop, type, compute_type,
				    TREE_OPERAND (rhs, 0),
				    TREE_OPERAND (rhs, 1), code);
}

/* Return a type for the widest vector mode whose components are of mode
   INNER_MODE, or NULL_TREE if none is found.  */
static tree
type_for_widest_vector_mode (enum machine_mode inner_mode, optab op)
{
  enum machine_mode best_mode = VOIDmode, mode;
  int best_nunits = 0;

  if (SCALAR_FLOAT_MODE_P (inner_mode))
    mode = MIN_MODE_VECTOR_FLOAT;
  else
    mode = MIN_MODE_VECTOR_INT;

  for (; mode != VOIDmode; mode = GET_MODE_WIDER_MODE (mode))
    if (GET_MODE_INNER (mode) == inner_mode
        && GET_MODE_NUNITS (mode) > best_nunits
	&& op->handlers[mode].insn_code != CODE_FOR_nothing)
      best_mode = mode, best_nunits = GET_MODE_NUNITS (mode);

  if (best_mode == VOIDmode)
    return NULL_TREE;
  else
    return lang_hooks.types.type_for_mode (best_mode, 1);
}

/* Process one statement.  If we identify a vector operation, expand it.  */

static void
expand_vector_operations_1 (block_stmt_iterator *bsi)
{
  tree stmt = bsi_stmt (*bsi);
  tree *p_lhs, *p_rhs, lhs, rhs, type, compute_type;
  enum tree_code code;
  enum machine_mode compute_mode;
  optab op;

  switch (TREE_CODE (stmt))
    {
    case RETURN_EXPR:
      stmt = TREE_OPERAND (stmt, 0);
      if (!stmt || TREE_CODE (stmt) != MODIFY_EXPR)
	return;

      /* FALLTHRU */

    case MODIFY_EXPR:
      p_lhs = &TREE_OPERAND (stmt, 0);
      p_rhs = &TREE_OPERAND (stmt, 1);
      lhs = *p_lhs;
      rhs = *p_rhs;
      break;

    default:
      return;
    }

  type = TREE_TYPE (rhs);
  if (TREE_CODE (type) != VECTOR_TYPE)
    return;

  code = TREE_CODE (rhs);
  if (TREE_CODE_CLASS (code) != tcc_unary
      && TREE_CODE_CLASS (code) != tcc_binary)
    return;

  if (code == NOP_EXPR || code == VIEW_CONVERT_EXPR)
    return;
  
  gcc_assert (code != CONVERT_EXPR);
  op = optab_for_tree_code (code, type);

  /* For widening vector operations, the relevant type is of the arguments,
     not the widened result.  */
  if (code == WIDEN_SUM_EXPR)
    type = TREE_TYPE (TREE_OPERAND (rhs, 0));

  /* Optabs will try converting a negation into a subtraction, so
     look for it as well.  TODO: negation of floating-point vectors
     might be turned into an exclusive OR toggling the sign bit.  */
  if (op == NULL
      && code == NEGATE_EXPR
      && INTEGRAL_TYPE_P (TREE_TYPE (type)))
    op = optab_for_tree_code (MINUS_EXPR, type);

  /* For very wide vectors, try using a smaller vector mode.  */
  compute_type = type;
  if (TYPE_MODE (type) == BLKmode && op)
    {
      tree vector_compute_type
        = type_for_widest_vector_mode (TYPE_MODE (TREE_TYPE (type)), op);
      if (vector_compute_type != NULL_TREE)
        compute_type = vector_compute_type;
    }

  /* If we are breaking a BLKmode vector into smaller pieces,
     type_for_widest_vector_mode has already looked into the optab,
     so skip these checks.  */
  if (compute_type == type)
    {
      compute_mode = TYPE_MODE (compute_type);
      if ((GET_MODE_CLASS (compute_mode) == MODE_VECTOR_INT
	   || GET_MODE_CLASS (compute_mode) == MODE_VECTOR_FLOAT)
          && op != NULL
	  && op->handlers[compute_mode].insn_code != CODE_FOR_nothing)
	return;
      else
	/* There is no operation in hardware, so fall back to scalars.  */
	compute_type = TREE_TYPE (type);
    }

  gcc_assert (code != VEC_LSHIFT_EXPR && code != VEC_RSHIFT_EXPR);
  rhs = expand_vector_operation (bsi, type, compute_type, rhs, code);
  if (lang_hooks.types_compatible_p (TREE_TYPE (lhs), TREE_TYPE (rhs)))
    *p_rhs = rhs;
  else
    *p_rhs = gimplify_build1 (bsi, VIEW_CONVERT_EXPR, TREE_TYPE (lhs), rhs);

  mark_stmt_modified (bsi_stmt (*bsi));
}

/* Use this to lower vector operations introduced by the vectorizer,
   if it may need the bit-twiddling tricks implemented in this file.  */

static bool
gate_expand_vector_operations (void)
{
  return flag_tree_vectorize != 0;
}

static unsigned int
expand_vector_operations (void)
{
  block_stmt_iterator bsi;
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  expand_vector_operations_1 (&bsi);
	  update_stmt_if_modified (bsi_stmt (bsi));
	}
    }
  return 0;
}

struct tree_opt_pass pass_lower_vector = 
{
  "veclower",				/* name */
  0,					/* gate */
  expand_vector_operations,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect
    | TODO_verify_stmts,		/* todo_flags_finish */
  0					/* letter */
};

struct tree_opt_pass pass_lower_vector_ssa = 
{
  "veclower2",				/* name */
  gate_expand_vector_operations,	/* gate */
  expand_vector_operations,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg,				/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_update_ssa	/* todo_flags_finish */
    | TODO_verify_ssa
    | TODO_verify_stmts | TODO_verify_flow,
  0					/* letter */
};

#include "gt-tree-vect-generic.h"
