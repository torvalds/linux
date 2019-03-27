/* Lower complex number operations to scalar operations.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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
#include "real.h"
#include "flags.h"
#include "tree-flow.h"
#include "tree-gimple.h"
#include "tree-iterator.h"
#include "tree-pass.h"
#include "tree-ssa-propagate.h"
#include "diagnostic.h"


/* For each complex ssa name, a lattice value.  We're interested in finding
   out whether a complex number is degenerate in some way, having only real
   or only complex parts.  */

typedef enum
{
  UNINITIALIZED = 0,
  ONLY_REAL = 1,
  ONLY_IMAG = 2,
  VARYING = 3
} complex_lattice_t;

#define PAIR(a, b)  ((a) << 2 | (b))

DEF_VEC_I(complex_lattice_t);
DEF_VEC_ALLOC_I(complex_lattice_t, heap);

static VEC(complex_lattice_t, heap) *complex_lattice_values;

/* For each complex variable, a pair of variables for the components exists in
   the hashtable.  */
static htab_t complex_variable_components;

/* For each complex SSA_NAME, a pair of ssa names for the components.  */
static VEC(tree, heap) *complex_ssa_name_components;

/* Lookup UID in the complex_variable_components hashtable and return the
   associated tree.  */
static tree 
cvc_lookup (unsigned int uid)
{
  struct int_tree_map *h, in;
  in.uid = uid;
  h = htab_find_with_hash (complex_variable_components, &in, uid);
  return h ? h->to : NULL;
}
 
/* Insert the pair UID, TO into the complex_variable_components hashtable.  */

static void 
cvc_insert (unsigned int uid, tree to)
{ 
  struct int_tree_map *h;
  void **loc;

  h = XNEW (struct int_tree_map);
  h->uid = uid;
  h->to = to;
  loc = htab_find_slot_with_hash (complex_variable_components, h,
				  uid, INSERT);
  *(struct int_tree_map **) loc = h;
}

/* Return true if T is not a zero constant.  In the case of real values,
   we're only interested in +0.0.  */

static int
some_nonzerop (tree t)
{
  int zerop = false;

  if (TREE_CODE (t) == REAL_CST)
    zerop = REAL_VALUES_IDENTICAL (TREE_REAL_CST (t), dconst0);
  else if (TREE_CODE (t) == INTEGER_CST)
    zerop = integer_zerop (t);

  return !zerop;
}

/* Compute a lattice value from T.  It may be a gimple_val, or, as a 
   special exception, a COMPLEX_EXPR.  */

static complex_lattice_t
find_lattice_value (tree t)
{
  tree real, imag;
  int r, i;
  complex_lattice_t ret;

  switch (TREE_CODE (t))
    {
    case SSA_NAME:
      return VEC_index (complex_lattice_t, complex_lattice_values,
			SSA_NAME_VERSION (t));

    case COMPLEX_CST:
      real = TREE_REALPART (t);
      imag = TREE_IMAGPART (t);
      break;

    case COMPLEX_EXPR:
      real = TREE_OPERAND (t, 0);
      imag = TREE_OPERAND (t, 1);
      break;

    default:
      gcc_unreachable ();
    }

  r = some_nonzerop (real);
  i = some_nonzerop (imag);
  ret = r*ONLY_REAL + i*ONLY_IMAG;

  /* ??? On occasion we could do better than mapping 0+0i to real, but we
     certainly don't want to leave it UNINITIALIZED, which eventually gets
     mapped to VARYING.  */
  if (ret == UNINITIALIZED)
    ret = ONLY_REAL;

  return ret;
}

/* Determine if LHS is something for which we're interested in seeing
   simulation results.  */

static bool
is_complex_reg (tree lhs)
{
  return TREE_CODE (TREE_TYPE (lhs)) == COMPLEX_TYPE && is_gimple_reg (lhs);
}

/* Mark the incoming parameters to the function as VARYING.  */

static void
init_parameter_lattice_values (void)
{
  tree parm;

  for (parm = DECL_ARGUMENTS (cfun->decl); parm ; parm = TREE_CHAIN (parm))
    if (is_complex_reg (parm) && var_ann (parm) != NULL)
      {
	tree ssa_name = default_def (parm);
	VEC_replace (complex_lattice_t, complex_lattice_values,
		     SSA_NAME_VERSION (ssa_name), VARYING);
      }
}

/* Initialize DONT_SIMULATE_AGAIN for each stmt and phi.  Return false if
   we found no statements we want to simulate, and thus there's nothing for
   the entire pass to do.  */

static bool
init_dont_simulate_again (void)
{
  basic_block bb;
  block_stmt_iterator bsi;
  tree phi;
  bool saw_a_complex_op = false;

  FOR_EACH_BB (bb)
    {
      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	DONT_SIMULATE_AGAIN (phi) = !is_complex_reg (PHI_RESULT (phi));

      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	{
	  tree orig_stmt, stmt, rhs = NULL;
	  bool dsa;

	  orig_stmt = stmt = bsi_stmt (bsi);

	  /* Most control-altering statements must be initially 
	     simulated, else we won't cover the entire cfg.  */
	  dsa = !stmt_ends_bb_p (stmt);

	  switch (TREE_CODE (stmt))
	    {
	    case RETURN_EXPR:
	      /* We don't care what the lattice value of <retval> is,
		 since it's never used as an input to another computation.  */
	      dsa = true;
	      stmt = TREE_OPERAND (stmt, 0);
	      if (!stmt || TREE_CODE (stmt) != MODIFY_EXPR)
		break;
	      /* FALLTHRU */

	    case MODIFY_EXPR:
	      dsa = !is_complex_reg (TREE_OPERAND (stmt, 0));
	      rhs = TREE_OPERAND (stmt, 1);
	      break;

	    case COND_EXPR:
	      rhs = TREE_OPERAND (stmt, 0);
	      break;

	    default:
	      break;
	    }

	  if (rhs)
	    switch (TREE_CODE (rhs))
	      {
	      case EQ_EXPR:
	      case NE_EXPR:
		rhs = TREE_OPERAND (rhs, 0);
		/* FALLTHRU */

	      case PLUS_EXPR:
	      case MINUS_EXPR:
	      case MULT_EXPR:
	      case TRUNC_DIV_EXPR:
	      case CEIL_DIV_EXPR:
	      case FLOOR_DIV_EXPR:
	      case ROUND_DIV_EXPR:
	      case RDIV_EXPR:
	      case NEGATE_EXPR:
	      case CONJ_EXPR:
		if (TREE_CODE (TREE_TYPE (rhs)) == COMPLEX_TYPE)
		  saw_a_complex_op = true;
		break;

	      default:
		break;
	      }

	  DONT_SIMULATE_AGAIN (orig_stmt) = dsa;
	}
    }

  return saw_a_complex_op;
}


/* Evaluate statement STMT against the complex lattice defined above.  */

static enum ssa_prop_result
complex_visit_stmt (tree stmt, edge *taken_edge_p ATTRIBUTE_UNUSED,
		    tree *result_p)
{
  complex_lattice_t new_l, old_l, op1_l, op2_l;
  unsigned int ver;
  tree lhs, rhs;

  if (TREE_CODE (stmt) != MODIFY_EXPR)
    return SSA_PROP_VARYING;

  lhs = TREE_OPERAND (stmt, 0);
  rhs = TREE_OPERAND (stmt, 1);

  /* These conditions should be satisfied due to the initial filter
     set up in init_dont_simulate_again.  */
  gcc_assert (TREE_CODE (lhs) == SSA_NAME);
  gcc_assert (TREE_CODE (TREE_TYPE (lhs)) == COMPLEX_TYPE);

  *result_p = lhs;
  ver = SSA_NAME_VERSION (lhs);
  old_l = VEC_index (complex_lattice_t, complex_lattice_values, ver);

  switch (TREE_CODE (rhs))
    {
    case SSA_NAME:
    case COMPLEX_EXPR:
    case COMPLEX_CST:
      new_l = find_lattice_value (rhs);
      break;

    case PLUS_EXPR:
    case MINUS_EXPR:
      op1_l = find_lattice_value (TREE_OPERAND (rhs, 0));
      op2_l = find_lattice_value (TREE_OPERAND (rhs, 1));

      /* We've set up the lattice values such that IOR neatly
	 models addition.  */
      new_l = op1_l | op2_l;
      break;

    case MULT_EXPR:
    case RDIV_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
      op1_l = find_lattice_value (TREE_OPERAND (rhs, 0));
      op2_l = find_lattice_value (TREE_OPERAND (rhs, 1));

      /* Obviously, if either varies, so does the result.  */
      if (op1_l == VARYING || op2_l == VARYING)
	new_l = VARYING;
      /* Don't prematurely promote variables if we've not yet seen
	 their inputs.  */
      else if (op1_l == UNINITIALIZED)
	new_l = op2_l;
      else if (op2_l == UNINITIALIZED)
	new_l = op1_l;
      else
	{
	  /* At this point both numbers have only one component. If the
	     numbers are of opposite kind, the result is imaginary,
	     otherwise the result is real. The add/subtract translates
	     the real/imag from/to 0/1; the ^ performs the comparison.  */
	  new_l = ((op1_l - ONLY_REAL) ^ (op2_l - ONLY_REAL)) + ONLY_REAL;

	  /* Don't allow the lattice value to flip-flop indefinitely.  */
	  new_l |= old_l;
	}
      break;

    case NEGATE_EXPR:
    case CONJ_EXPR:
      new_l = find_lattice_value (TREE_OPERAND (rhs, 0));
      break;

    default:
      new_l = VARYING;
      break;
    }

  /* If nothing changed this round, let the propagator know.  */
  if (new_l == old_l)
    return SSA_PROP_NOT_INTERESTING;

  VEC_replace (complex_lattice_t, complex_lattice_values, ver, new_l);
  return new_l == VARYING ? SSA_PROP_VARYING : SSA_PROP_INTERESTING;
}

/* Evaluate a PHI node against the complex lattice defined above.  */

static enum ssa_prop_result
complex_visit_phi (tree phi)
{
  complex_lattice_t new_l, old_l;
  unsigned int ver;
  tree lhs;
  int i;

  lhs = PHI_RESULT (phi);

  /* This condition should be satisfied due to the initial filter
     set up in init_dont_simulate_again.  */
  gcc_assert (TREE_CODE (TREE_TYPE (lhs)) == COMPLEX_TYPE);

  /* We've set up the lattice values such that IOR neatly models PHI meet.  */
  new_l = UNINITIALIZED;
  for (i = PHI_NUM_ARGS (phi) - 1; i >= 0; --i)
    new_l |= find_lattice_value (PHI_ARG_DEF (phi, i));

  ver = SSA_NAME_VERSION (lhs);
  old_l = VEC_index (complex_lattice_t, complex_lattice_values, ver);

  if (new_l == old_l)
    return SSA_PROP_NOT_INTERESTING;

  VEC_replace (complex_lattice_t, complex_lattice_values, ver, new_l);
  return new_l == VARYING ? SSA_PROP_VARYING : SSA_PROP_INTERESTING;
}

/* Create one backing variable for a complex component of ORIG.  */

static tree
create_one_component_var (tree type, tree orig, const char *prefix,
			  const char *suffix, enum tree_code code)
{
  tree r = create_tmp_var (type, prefix);
  add_referenced_var (r);

  DECL_SOURCE_LOCATION (r) = DECL_SOURCE_LOCATION (orig);
  DECL_ARTIFICIAL (r) = 1;

  if (DECL_NAME (orig) && !DECL_IGNORED_P (orig))
    {
      const char *name = IDENTIFIER_POINTER (DECL_NAME (orig));
      tree inner_type;

      DECL_NAME (r) = get_identifier (ACONCAT ((name, suffix, NULL)));

      inner_type = TREE_TYPE (TREE_TYPE (orig));
      SET_DECL_DEBUG_EXPR (r, build1 (code, type, orig));
      DECL_DEBUG_EXPR_IS_FROM (r) = 1;
      DECL_IGNORED_P (r) = 0;
      TREE_NO_WARNING (r) = TREE_NO_WARNING (orig);
    }
  else
    {
      DECL_IGNORED_P (r) = 1;
      TREE_NO_WARNING (r) = 1;
    }

  return r;
}

/* Retrieve a value for a complex component of VAR.  */

static tree
get_component_var (tree var, bool imag_p)
{
  size_t decl_index = DECL_UID (var) * 2 + imag_p;
  tree ret = cvc_lookup (decl_index);

  if (ret == NULL)
    {
      ret = create_one_component_var (TREE_TYPE (TREE_TYPE (var)), var,
				      imag_p ? "CI" : "CR",
				      imag_p ? "$imag" : "$real",
				      imag_p ? IMAGPART_EXPR : REALPART_EXPR);
      cvc_insert (decl_index, ret);
    }

  return ret;
}

/* Retrieve a value for a complex component of SSA_NAME.  */

static tree
get_component_ssa_name (tree ssa_name, bool imag_p)
{
  complex_lattice_t lattice = find_lattice_value (ssa_name);
  size_t ssa_name_index;
  tree ret;

  if (lattice == (imag_p ? ONLY_REAL : ONLY_IMAG))
    {
      tree inner_type = TREE_TYPE (TREE_TYPE (ssa_name));
      if (SCALAR_FLOAT_TYPE_P (inner_type))
	return build_real (inner_type, dconst0);
      else
	return build_int_cst (inner_type, 0);
    }

  ssa_name_index = SSA_NAME_VERSION (ssa_name) * 2 + imag_p;
  ret = VEC_index (tree, complex_ssa_name_components, ssa_name_index);
  if (ret == NULL)
    {
      ret = get_component_var (SSA_NAME_VAR (ssa_name), imag_p);
      ret = make_ssa_name (ret, NULL);

      /* Copy some properties from the original.  In particular, whether it
	 is used in an abnormal phi, and whether it's uninitialized.  */
      SSA_NAME_OCCURS_IN_ABNORMAL_PHI (ret)
	= SSA_NAME_OCCURS_IN_ABNORMAL_PHI (ssa_name);
      if (TREE_CODE (SSA_NAME_VAR (ssa_name)) == VAR_DECL
	  && IS_EMPTY_STMT (SSA_NAME_DEF_STMT (ssa_name)))
	{
	  SSA_NAME_DEF_STMT (ret) = SSA_NAME_DEF_STMT (ssa_name);
	  set_default_def (SSA_NAME_VAR (ret), ret);
	}

      VEC_replace (tree, complex_ssa_name_components, ssa_name_index, ret);
    }

  return ret;
}

/* Set a value for a complex component of SSA_NAME, return a STMT_LIST of
   stuff that needs doing.  */

static tree
set_component_ssa_name (tree ssa_name, bool imag_p, tree value)
{
  complex_lattice_t lattice = find_lattice_value (ssa_name);
  size_t ssa_name_index;
  tree comp, list, last;

  /* We know the value must be zero, else there's a bug in our lattice
     analysis.  But the value may well be a variable known to contain
     zero.  We should be safe ignoring it.  */
  if (lattice == (imag_p ? ONLY_REAL : ONLY_IMAG))
    return NULL;

  /* If we've already assigned an SSA_NAME to this component, then this
     means that our walk of the basic blocks found a use before the set.
     This is fine.  Now we should create an initialization for the value
     we created earlier.  */
  ssa_name_index = SSA_NAME_VERSION (ssa_name) * 2 + imag_p;
  comp = VEC_index (tree, complex_ssa_name_components, ssa_name_index);
  if (comp)
    ;

  /* If we've nothing assigned, and the value we're given is already stable,
     then install that as the value for this SSA_NAME.  This preemptively
     copy-propagates the value, which avoids unnecessary memory allocation.  */
  else if (is_gimple_min_invariant (value))
    {
      VEC_replace (tree, complex_ssa_name_components, ssa_name_index, value);
      return NULL;
    }
  else if (TREE_CODE (value) == SSA_NAME
	   && !SSA_NAME_OCCURS_IN_ABNORMAL_PHI (ssa_name))
    {
      /* Replace an anonymous base value with the variable from cvc_lookup.
	 This should result in better debug info.  */
      if (DECL_IGNORED_P (SSA_NAME_VAR (value))
	  && !DECL_IGNORED_P (SSA_NAME_VAR (ssa_name)))
	{
	  comp = get_component_var (SSA_NAME_VAR (ssa_name), imag_p);
	  replace_ssa_name_symbol (value, comp);
	}

      VEC_replace (tree, complex_ssa_name_components, ssa_name_index, value);
      return NULL;
    }

  /* Finally, we need to stabilize the result by installing the value into
     a new ssa name.  */
  else
    comp = get_component_ssa_name (ssa_name, imag_p);
  
  /* Do all the work to assign VALUE to COMP.  */
  value = force_gimple_operand (value, &list, false, NULL);
  last = build2 (MODIFY_EXPR, TREE_TYPE (comp), comp, value);
  append_to_statement_list (last, &list);

  gcc_assert (SSA_NAME_DEF_STMT (comp) == NULL);
  SSA_NAME_DEF_STMT (comp) = last;

  return list;
}

/* Extract the real or imaginary part of a complex variable or constant.
   Make sure that it's a proper gimple_val and gimplify it if not.
   Emit any new code before BSI.  */

static tree
extract_component (block_stmt_iterator *bsi, tree t, bool imagpart_p,
		   bool gimple_p)
{
  switch (TREE_CODE (t))
    {
    case COMPLEX_CST:
      return imagpart_p ? TREE_IMAGPART (t) : TREE_REALPART (t);

    case COMPLEX_EXPR:
      return TREE_OPERAND (t, imagpart_p);

    case VAR_DECL:
    case RESULT_DECL:
    case PARM_DECL:
    case INDIRECT_REF:
    case COMPONENT_REF:
    case ARRAY_REF:
      {
	tree inner_type = TREE_TYPE (TREE_TYPE (t));

	t = build1 ((imagpart_p ? IMAGPART_EXPR : REALPART_EXPR),
		    inner_type, unshare_expr (t));

	if (gimple_p)
	  t = gimplify_val (bsi, inner_type, t);

	return t;
      }

    case SSA_NAME:
      return get_component_ssa_name (t, imagpart_p);

    default:
      gcc_unreachable ();
    }
}

/* Update the complex components of the ssa name on the lhs of STMT.  */

static void
update_complex_components (block_stmt_iterator *bsi, tree stmt, tree r, tree i)
{
  tree lhs = TREE_OPERAND (stmt, 0);
  tree list;

  list = set_component_ssa_name (lhs, false, r);
  if (list)
    bsi_insert_after (bsi, list, BSI_CONTINUE_LINKING);

  list = set_component_ssa_name (lhs, true, i);
  if (list)
    bsi_insert_after (bsi, list, BSI_CONTINUE_LINKING);
}

static void
update_complex_components_on_edge (edge e, tree lhs, tree r, tree i)
{
  tree list;

  list = set_component_ssa_name (lhs, false, r);
  if (list)
    bsi_insert_on_edge (e, list);

  list = set_component_ssa_name (lhs, true, i);
  if (list)
    bsi_insert_on_edge (e, list);
}

/* Update an assignment to a complex variable in place.  */

static void
update_complex_assignment (block_stmt_iterator *bsi, tree r, tree i)
{
  tree stmt, mod;
  tree type;

  mod = stmt = bsi_stmt (*bsi);
  if (TREE_CODE (stmt) == RETURN_EXPR)
    mod = TREE_OPERAND (mod, 0);
  else if (in_ssa_p)
    update_complex_components (bsi, stmt, r, i);
  
  type = TREE_TYPE (TREE_OPERAND (mod, 1));
  TREE_OPERAND (mod, 1) = build2 (COMPLEX_EXPR, type, r, i);
  update_stmt (stmt);
}

/* Generate code at the entry point of the function to initialize the
   component variables for a complex parameter.  */

static void
update_parameter_components (void)
{
  edge entry_edge = single_succ_edge (ENTRY_BLOCK_PTR);
  tree parm;

  for (parm = DECL_ARGUMENTS (cfun->decl); parm ; parm = TREE_CHAIN (parm))
    {
      tree type = TREE_TYPE (parm);
      tree ssa_name, r, i;

      if (TREE_CODE (type) != COMPLEX_TYPE || !is_gimple_reg (parm))
	continue;

      type = TREE_TYPE (type);
      ssa_name = default_def (parm);
      if (!ssa_name)
	continue;

      r = build1 (REALPART_EXPR, type, ssa_name);
      i = build1 (IMAGPART_EXPR, type, ssa_name);
      update_complex_components_on_edge (entry_edge, ssa_name, r, i);
    }
}

/* Generate code to set the component variables of a complex variable
   to match the PHI statements in block BB.  */

static void
update_phi_components (basic_block bb)
{
  tree phi;

  for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
    if (is_complex_reg (PHI_RESULT (phi)))
      {
	tree lr, li, pr = NULL, pi = NULL;
	unsigned int i, n;

	lr = get_component_ssa_name (PHI_RESULT (phi), false);
	if (TREE_CODE (lr) == SSA_NAME)
	  {
	    pr = create_phi_node (lr, bb);
	    SSA_NAME_DEF_STMT (lr) = pr;
	  }

	li = get_component_ssa_name (PHI_RESULT (phi), true);
	if (TREE_CODE (li) == SSA_NAME)
	  {
	    pi = create_phi_node (li, bb);
	    SSA_NAME_DEF_STMT (li) = pi;
	  }
	
	for (i = 0, n = PHI_NUM_ARGS (phi); i < n; ++i)
	  {
	    tree comp, arg = PHI_ARG_DEF (phi, i);
	    if (pr)
	      {
		comp = extract_component (NULL, arg, false, false);
		SET_PHI_ARG_DEF (pr, i, comp);
	      }
	    if (pi)
	      {
		comp = extract_component (NULL, arg, true, false);
		SET_PHI_ARG_DEF (pi, i, comp);
	      }
	  }
      }
}

/* Mark each virtual op in STMT for ssa update.  */

static void
update_all_vops (tree stmt)
{
  ssa_op_iter iter;
  tree sym;

  FOR_EACH_SSA_TREE_OPERAND (sym, stmt, iter, SSA_OP_ALL_VIRTUALS)
    {
      if (TREE_CODE (sym) == SSA_NAME)
	sym = SSA_NAME_VAR (sym);
      mark_sym_for_renaming (sym);
    }
}

/* Expand a complex move to scalars.  */

static void
expand_complex_move (block_stmt_iterator *bsi, tree stmt, tree type,
		     tree lhs, tree rhs)
{
  tree inner_type = TREE_TYPE (type);
  tree r, i;

  if (TREE_CODE (lhs) == SSA_NAME)
    {
      if (is_ctrl_altering_stmt (bsi_stmt (*bsi)))
	{
	  edge_iterator ei;
	  edge e;

	  /* The value is not assigned on the exception edges, so we need not
	     concern ourselves there.  We do need to update on the fallthru
	     edge.  Find it.  */
	  FOR_EACH_EDGE (e, ei, bsi->bb->succs)
	    if (e->flags & EDGE_FALLTHRU)
	      goto found_fallthru;
	  gcc_unreachable ();
	found_fallthru:

	  r = build1 (REALPART_EXPR, inner_type, lhs);
	  i = build1 (IMAGPART_EXPR, inner_type, lhs);
	  update_complex_components_on_edge (e, lhs, r, i);
	}
      else if (TREE_CODE (rhs) == CALL_EXPR || TREE_SIDE_EFFECTS (rhs))
	{
	  r = build1 (REALPART_EXPR, inner_type, lhs);
	  i = build1 (IMAGPART_EXPR, inner_type, lhs);
	  update_complex_components (bsi, stmt, r, i);
	}
      else
	{
	  update_all_vops (bsi_stmt (*bsi));
	  r = extract_component (bsi, rhs, 0, true);
	  i = extract_component (bsi, rhs, 1, true);
	  update_complex_assignment (bsi, r, i);
	}
    }
  else if (TREE_CODE (rhs) == SSA_NAME && !TREE_SIDE_EFFECTS (lhs))
    {
      tree x;

      r = extract_component (bsi, rhs, 0, false);
      i = extract_component (bsi, rhs, 1, false);

      x = build1 (REALPART_EXPR, inner_type, unshare_expr (lhs));
      x = build2 (MODIFY_EXPR, inner_type, x, r);
      bsi_insert_before (bsi, x, BSI_SAME_STMT);

      if (stmt == bsi_stmt (*bsi))
	{
	  x = build1 (IMAGPART_EXPR, inner_type, unshare_expr (lhs));
	  TREE_OPERAND (stmt, 0) = x;
	  TREE_OPERAND (stmt, 1) = i;
	  TREE_TYPE (stmt) = inner_type;
	}
      else
	{
	  x = build1 (IMAGPART_EXPR, inner_type, unshare_expr (lhs));
	  x = build2 (MODIFY_EXPR, inner_type, x, i);
	  bsi_insert_before (bsi, x, BSI_SAME_STMT);

	  stmt = bsi_stmt (*bsi);
	  gcc_assert (TREE_CODE (stmt) == RETURN_EXPR);
	  TREE_OPERAND (stmt, 0) = lhs;
	}

      update_all_vops (stmt);
      update_stmt (stmt);
    }
}

/* Expand complex addition to scalars:
	a + b = (ar + br) + i(ai + bi)
	a - b = (ar - br) + i(ai + bi)
*/

static void
expand_complex_addition (block_stmt_iterator *bsi, tree inner_type,
			 tree ar, tree ai, tree br, tree bi,
			 enum tree_code code,
			 complex_lattice_t al, complex_lattice_t bl)
{
  tree rr, ri;

  switch (PAIR (al, bl))
    {
    case PAIR (ONLY_REAL, ONLY_REAL):
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = ai;
      break;

    case PAIR (ONLY_REAL, ONLY_IMAG):
      rr = ar;
      if (code == MINUS_EXPR)
	ri = gimplify_build2 (bsi, MINUS_EXPR, inner_type, ai, bi);
      else
	ri = bi;
      break;

    case PAIR (ONLY_IMAG, ONLY_REAL):
      if (code == MINUS_EXPR)
	rr = gimplify_build2 (bsi, MINUS_EXPR, inner_type, ar, br);
      else
	rr = br;
      ri = ai;
      break;

    case PAIR (ONLY_IMAG, ONLY_IMAG):
      rr = ar;
      ri = gimplify_build2 (bsi, code, inner_type, ai, bi);
      break;

    case PAIR (VARYING, ONLY_REAL):
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = ai;
      break;

    case PAIR (VARYING, ONLY_IMAG):
      rr = ar;
      ri = gimplify_build2 (bsi, code, inner_type, ai, bi);
      break;

    case PAIR (ONLY_REAL, VARYING):
      if (code == MINUS_EXPR)
	goto general;
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = bi;
      break;

    case PAIR (ONLY_IMAG, VARYING):
      if (code == MINUS_EXPR)
	goto general;
      rr = br;
      ri = gimplify_build2 (bsi, code, inner_type, ai, bi);
      break;

    case PAIR (VARYING, VARYING):
    general:
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = gimplify_build2 (bsi, code, inner_type, ai, bi);
      break;

    default:
      gcc_unreachable ();
    }

  update_complex_assignment (bsi, rr, ri);
}

/* Expand a complex multiplication or division to a libcall to the c99
   compliant routines.  */

static void
expand_complex_libcall (block_stmt_iterator *bsi, tree ar, tree ai,
			tree br, tree bi, enum tree_code code)
{
  enum machine_mode mode;
  enum built_in_function bcode;
  tree args, fn, stmt, type;

  args = tree_cons (NULL, bi, NULL);
  args = tree_cons (NULL, br, args);
  args = tree_cons (NULL, ai, args);
  args = tree_cons (NULL, ar, args);

  stmt = bsi_stmt (*bsi);
  type = TREE_TYPE (TREE_OPERAND (stmt, 1));

  mode = TYPE_MODE (type);
  gcc_assert (GET_MODE_CLASS (mode) == MODE_COMPLEX_FLOAT);
  if (code == MULT_EXPR)
    bcode = BUILT_IN_COMPLEX_MUL_MIN + mode - MIN_MODE_COMPLEX_FLOAT;
  else if (code == RDIV_EXPR)
    bcode = BUILT_IN_COMPLEX_DIV_MIN + mode - MIN_MODE_COMPLEX_FLOAT;
  else
    gcc_unreachable ();
  fn = built_in_decls[bcode];

  TREE_OPERAND (stmt, 1)
    = build3 (CALL_EXPR, type, build_fold_addr_expr (fn), args, NULL);
  update_stmt (stmt);

  if (in_ssa_p)
    {
      tree lhs = TREE_OPERAND (stmt, 0);
      type = TREE_TYPE (type);
      update_complex_components (bsi, stmt,
				 build1 (REALPART_EXPR, type, lhs),
				 build1 (IMAGPART_EXPR, type, lhs));
    }
}

/* Expand complex multiplication to scalars:
	a * b = (ar*br - ai*bi) + i(ar*bi + br*ai)
*/

static void
expand_complex_multiplication (block_stmt_iterator *bsi, tree inner_type,
			       tree ar, tree ai, tree br, tree bi,
			       complex_lattice_t al, complex_lattice_t bl)
{
  tree rr, ri;

  if (al < bl)
    {
      complex_lattice_t tl;
      rr = ar, ar = br, br = rr;
      ri = ai, ai = bi, bi = ri;
      tl = al, al = bl, bl = tl;
    }

  switch (PAIR (al, bl))
    {
    case PAIR (ONLY_REAL, ONLY_REAL):
      rr = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, br);
      ri = ai;
      break;

    case PAIR (ONLY_IMAG, ONLY_REAL):
      rr = ar;
      if (TREE_CODE (ai) == REAL_CST
	  && REAL_VALUES_IDENTICAL (TREE_REAL_CST (ai), dconst1))
	ri = br;
      else
	ri = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, br);
      break;

    case PAIR (ONLY_IMAG, ONLY_IMAG):
      rr = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, bi);
      rr = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, rr);
      ri = ar;
      break;

    case PAIR (VARYING, ONLY_REAL):
      rr = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, br);
      ri = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, br);
      break;

    case PAIR (VARYING, ONLY_IMAG):
      rr = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, bi);
      rr = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, rr);
      ri = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, bi);
      break;

    case PAIR (VARYING, VARYING):
      if (flag_complex_method == 2 && SCALAR_FLOAT_TYPE_P (inner_type))
	{
	  expand_complex_libcall (bsi, ar, ai, br, bi, MULT_EXPR);
	  return;
	}
      else
	{
	  tree t1, t2, t3, t4;

	  t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, br);
	  t2 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, bi);
	  t3 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, bi);

	  /* Avoid expanding redundant multiplication for the common
	     case of squaring a complex number.  */
	  if (ar == br && ai == bi)
	    t4 = t3;
	  else
	    t4 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, br);

	  rr = gimplify_build2 (bsi, MINUS_EXPR, inner_type, t1, t2);
	  ri = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t3, t4);
	}
      break;

    default:
      gcc_unreachable ();
    }

  update_complex_assignment (bsi, rr, ri);
}

/* Expand complex division to scalars, straightforward algorithm.
	a / b = ((ar*br + ai*bi)/t) + i((ai*br - ar*bi)/t)
	    t = br*br + bi*bi
*/

static void
expand_complex_div_straight (block_stmt_iterator *bsi, tree inner_type,
			     tree ar, tree ai, tree br, tree bi,
			     enum tree_code code)
{
  tree rr, ri, div, t1, t2, t3;

  t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, br, br);
  t2 = gimplify_build2 (bsi, MULT_EXPR, inner_type, bi, bi);
  div = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, t2);

  t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, br);
  t2 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, bi);
  t3 = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, t2);
  rr = gimplify_build2 (bsi, code, inner_type, t3, div);

  t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, br);
  t2 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, bi);
  t3 = gimplify_build2 (bsi, MINUS_EXPR, inner_type, t1, t2);
  ri = gimplify_build2 (bsi, code, inner_type, t3, div);

  update_complex_assignment (bsi, rr, ri);
}

/* Expand complex division to scalars, modified algorithm to minimize
   overflow with wide input ranges.  */

static void
expand_complex_div_wide (block_stmt_iterator *bsi, tree inner_type,
			 tree ar, tree ai, tree br, tree bi,
			 enum tree_code code)
{
  tree rr, ri, ratio, div, t1, t2, tr, ti, cond;
  basic_block bb_cond, bb_true, bb_false, bb_join;

  /* Examine |br| < |bi|, and branch.  */
  t1 = gimplify_build1 (bsi, ABS_EXPR, inner_type, br);
  t2 = gimplify_build1 (bsi, ABS_EXPR, inner_type, bi);
  cond = fold_build2 (LT_EXPR, boolean_type_node, t1, t2);
  STRIP_NOPS (cond);

  bb_cond = bb_true = bb_false = bb_join = NULL;
  rr = ri = tr = ti = NULL;
  if (!TREE_CONSTANT (cond))
    {
      edge e;

      cond = build3 (COND_EXPR, void_type_node, cond, NULL_TREE, NULL_TREE);
      bsi_insert_before (bsi, cond, BSI_SAME_STMT);

      /* Split the original block, and create the TRUE and FALSE blocks.  */
      e = split_block (bsi->bb, cond);
      bb_cond = e->src;
      bb_join = e->dest;
      bb_true = create_empty_bb (bb_cond);
      bb_false = create_empty_bb (bb_true);

      t1 = build1 (GOTO_EXPR, void_type_node, tree_block_label (bb_true));
      t2 = build1 (GOTO_EXPR, void_type_node, tree_block_label (bb_false));
      COND_EXPR_THEN (cond) = t1;
      COND_EXPR_ELSE (cond) = t2;

      /* Wire the blocks together.  */
      e->flags = EDGE_TRUE_VALUE;
      redirect_edge_succ (e, bb_true);
      make_edge (bb_cond, bb_false, EDGE_FALSE_VALUE);
      make_edge (bb_true, bb_join, EDGE_FALLTHRU);
      make_edge (bb_false, bb_join, EDGE_FALLTHRU);

      /* Update dominance info.  Note that bb_join's data was
         updated by split_block.  */
      if (dom_info_available_p (CDI_DOMINATORS))
        {
          set_immediate_dominator (CDI_DOMINATORS, bb_true, bb_cond);
          set_immediate_dominator (CDI_DOMINATORS, bb_false, bb_cond);
        }

      rr = make_rename_temp (inner_type, NULL);
      ri = make_rename_temp (inner_type, NULL);
    }

  /* In the TRUE branch, we compute
      ratio = br/bi;
      div = (br * ratio) + bi;
      tr = (ar * ratio) + ai;
      ti = (ai * ratio) - ar;
      tr = tr / div;
      ti = ti / div;  */
  if (bb_true || integer_nonzerop (cond))
    {
      if (bb_true)
	{
	  *bsi = bsi_last (bb_true);
	  bsi_insert_after (bsi, build_empty_stmt (), BSI_NEW_STMT);
	}

      ratio = gimplify_build2 (bsi, code, inner_type, br, bi);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, br, ratio);
      div = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, bi);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, ratio);
      tr = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, ai);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, ratio);
      ti = gimplify_build2 (bsi, MINUS_EXPR, inner_type, t1, ar);

      tr = gimplify_build2 (bsi, code, inner_type, tr, div);
      ti = gimplify_build2 (bsi, code, inner_type, ti, div);

     if (bb_true)
       {
	 t1 = build2 (MODIFY_EXPR, inner_type, rr, tr);
	 bsi_insert_before (bsi, t1, BSI_SAME_STMT);
	 t1 = build2 (MODIFY_EXPR, inner_type, ri, ti);
	 bsi_insert_before (bsi, t1, BSI_SAME_STMT);
	 bsi_remove (bsi, true);
       }
    }

  /* In the FALSE branch, we compute
      ratio = d/c;
      divisor = (d * ratio) + c;
      tr = (b * ratio) + a;
      ti = b - (a * ratio);
      tr = tr / div;
      ti = ti / div;  */
  if (bb_false || integer_zerop (cond))
    {
      if (bb_false)
	{
	  *bsi = bsi_last (bb_false);
	  bsi_insert_after (bsi, build_empty_stmt (), BSI_NEW_STMT);
	}

      ratio = gimplify_build2 (bsi, code, inner_type, bi, br);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, bi, ratio);
      div = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, br);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ai, ratio);
      tr = gimplify_build2 (bsi, PLUS_EXPR, inner_type, t1, ar);

      t1 = gimplify_build2 (bsi, MULT_EXPR, inner_type, ar, ratio);
      ti = gimplify_build2 (bsi, MINUS_EXPR, inner_type, ai, t1);

      tr = gimplify_build2 (bsi, code, inner_type, tr, div);
      ti = gimplify_build2 (bsi, code, inner_type, ti, div);

     if (bb_false)
       {
	 t1 = build2 (MODIFY_EXPR, inner_type, rr, tr);
	 bsi_insert_before (bsi, t1, BSI_SAME_STMT);
	 t1 = build2 (MODIFY_EXPR, inner_type, ri, ti);
	 bsi_insert_before (bsi, t1, BSI_SAME_STMT);
	 bsi_remove (bsi, true);
       }
    }

  if (bb_join)
    *bsi = bsi_start (bb_join);
  else
    rr = tr, ri = ti;

  update_complex_assignment (bsi, rr, ri);
}

/* Expand complex division to scalars.  */

static void
expand_complex_division (block_stmt_iterator *bsi, tree inner_type,
			 tree ar, tree ai, tree br, tree bi,
			 enum tree_code code,
			 complex_lattice_t al, complex_lattice_t bl)
{
  tree rr, ri;

  switch (PAIR (al, bl))
    {
    case PAIR (ONLY_REAL, ONLY_REAL):
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = ai;
      break;

    case PAIR (ONLY_REAL, ONLY_IMAG):
      rr = ai;
      ri = gimplify_build2 (bsi, code, inner_type, ar, bi);
      ri = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, ri);
      break;

    case PAIR (ONLY_IMAG, ONLY_REAL):
      rr = ar;
      ri = gimplify_build2 (bsi, code, inner_type, ai, br);
      break;

    case PAIR (ONLY_IMAG, ONLY_IMAG):
      rr = gimplify_build2 (bsi, code, inner_type, ai, bi);
      ri = ar;
      break;

    case PAIR (VARYING, ONLY_REAL):
      rr = gimplify_build2 (bsi, code, inner_type, ar, br);
      ri = gimplify_build2 (bsi, code, inner_type, ai, br);
      break;

    case PAIR (VARYING, ONLY_IMAG):
      rr = gimplify_build2 (bsi, code, inner_type, ai, bi);
      ri = gimplify_build2 (bsi, code, inner_type, ar, bi);
      ri = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, ri);

    case PAIR (ONLY_REAL, VARYING):
    case PAIR (ONLY_IMAG, VARYING):
    case PAIR (VARYING, VARYING):
      switch (flag_complex_method)
	{
	case 0:
	  /* straightforward implementation of complex divide acceptable.  */
	  expand_complex_div_straight (bsi, inner_type, ar, ai, br, bi, code);
	  break;

	case 2:
	  if (SCALAR_FLOAT_TYPE_P (inner_type))
	    {
	      expand_complex_libcall (bsi, ar, ai, br, bi, code);
	      break;
	    }
	  /* FALLTHRU */

	case 1:
	  /* wide ranges of inputs must work for complex divide.  */
	  expand_complex_div_wide (bsi, inner_type, ar, ai, br, bi, code);
	  break;

	default:
	  gcc_unreachable ();
	}
      return;

    default:
      gcc_unreachable ();
    }

  update_complex_assignment (bsi, rr, ri);
}

/* Expand complex negation to scalars:
	-a = (-ar) + i(-ai)
*/

static void
expand_complex_negation (block_stmt_iterator *bsi, tree inner_type,
			 tree ar, tree ai)
{
  tree rr, ri;

  rr = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, ar);
  ri = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, ai);

  update_complex_assignment (bsi, rr, ri);
}

/* Expand complex conjugate to scalars:
	~a = (ar) + i(-ai)
*/

static void
expand_complex_conjugate (block_stmt_iterator *bsi, tree inner_type,
			  tree ar, tree ai)
{
  tree ri;

  ri = gimplify_build1 (bsi, NEGATE_EXPR, inner_type, ai);

  update_complex_assignment (bsi, ar, ri);
}

/* Expand complex comparison (EQ or NE only).  */

static void
expand_complex_comparison (block_stmt_iterator *bsi, tree ar, tree ai,
			   tree br, tree bi, enum tree_code code)
{
  tree cr, ci, cc, stmt, expr, type;

  cr = gimplify_build2 (bsi, code, boolean_type_node, ar, br);
  ci = gimplify_build2 (bsi, code, boolean_type_node, ai, bi);
  cc = gimplify_build2 (bsi,
			(code == EQ_EXPR ? TRUTH_AND_EXPR : TRUTH_OR_EXPR),
			boolean_type_node, cr, ci);

  stmt = expr = bsi_stmt (*bsi);

  switch (TREE_CODE (stmt))
    {
    case RETURN_EXPR:
      expr = TREE_OPERAND (stmt, 0);
      /* FALLTHRU */
    case MODIFY_EXPR:
      type = TREE_TYPE (TREE_OPERAND (expr, 1));
      TREE_OPERAND (expr, 1) = fold_convert (type, cc);
      break;
    case COND_EXPR:
      TREE_OPERAND (stmt, 0) = cc;
      break;
    default:
      gcc_unreachable ();
    }

  update_stmt (stmt);
}

/* Process one statement.  If we identify a complex operation, expand it.  */

static void
expand_complex_operations_1 (block_stmt_iterator *bsi)
{
  tree stmt = bsi_stmt (*bsi);
  tree rhs, type, inner_type;
  tree ac, ar, ai, bc, br, bi;
  complex_lattice_t al, bl;
  enum tree_code code;

  switch (TREE_CODE (stmt))
    {
    case RETURN_EXPR:
      stmt = TREE_OPERAND (stmt, 0);
      if (!stmt)
	return;
      if (TREE_CODE (stmt) != MODIFY_EXPR)
	return;
      /* FALLTHRU */

    case MODIFY_EXPR:
      rhs = TREE_OPERAND (stmt, 1);
      break;

    case COND_EXPR:
      rhs = TREE_OPERAND (stmt, 0);
      break;

    default:
      return;
    }

  type = TREE_TYPE (rhs);
  code = TREE_CODE (rhs);

  /* Initial filter for operations we handle.  */
  switch (code)
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
    case MULT_EXPR:
    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case RDIV_EXPR:
    case NEGATE_EXPR:
    case CONJ_EXPR:
      if (TREE_CODE (type) != COMPLEX_TYPE)
	return;
      inner_type = TREE_TYPE (type);
      break;

    case EQ_EXPR:
    case NE_EXPR:
      inner_type = TREE_TYPE (TREE_OPERAND (rhs, 1));
      if (TREE_CODE (inner_type) != COMPLEX_TYPE)
	return;
      break;

    default:
      {
	tree lhs = TREE_OPERAND (stmt, 0);
	tree rhs = TREE_OPERAND (stmt, 1);

	if (TREE_CODE (type) == COMPLEX_TYPE)
	  expand_complex_move (bsi, stmt, type, lhs, rhs);
	else if ((TREE_CODE (rhs) == REALPART_EXPR
		  || TREE_CODE (rhs) == IMAGPART_EXPR)
		 && TREE_CODE (TREE_OPERAND (rhs, 0)) == SSA_NAME)
	  {
	    TREE_OPERAND (stmt, 1)
	      = extract_component (bsi, TREE_OPERAND (rhs, 0),
				   TREE_CODE (rhs) == IMAGPART_EXPR, false);
	    update_stmt (stmt);
	  }
      }
      return;
    }

  /* Extract the components of the two complex values.  Make sure and
     handle the common case of the same value used twice specially.  */
  ac = TREE_OPERAND (rhs, 0);
  ar = extract_component (bsi, ac, 0, true);
  ai = extract_component (bsi, ac, 1, true);

  if (TREE_CODE_CLASS (code) == tcc_unary)
    bc = br = bi = NULL;
  else
    {
      bc = TREE_OPERAND (rhs, 1);
      if (ac == bc)
	br = ar, bi = ai;
      else
	{
	  br = extract_component (bsi, bc, 0, true);
	  bi = extract_component (bsi, bc, 1, true);
	}
    }

  if (in_ssa_p)
    {
      al = find_lattice_value (ac);
      if (al == UNINITIALIZED)
	al = VARYING;

      if (TREE_CODE_CLASS (code) == tcc_unary)
	bl = UNINITIALIZED;
      else if (ac == bc)
	bl = al;
      else
	{
	  bl = find_lattice_value (bc);
	  if (bl == UNINITIALIZED)
	    bl = VARYING;
	}
    }
  else
    al = bl = VARYING;

  switch (code)
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
      expand_complex_addition (bsi, inner_type, ar, ai, br, bi, code, al, bl);
      break;

    case MULT_EXPR:
      expand_complex_multiplication (bsi, inner_type, ar, ai, br, bi, al, bl);
      break;

    case TRUNC_DIV_EXPR:
    case CEIL_DIV_EXPR:
    case FLOOR_DIV_EXPR:
    case ROUND_DIV_EXPR:
    case RDIV_EXPR:
      expand_complex_division (bsi, inner_type, ar, ai, br, bi, code, al, bl);
      break;
      
    case NEGATE_EXPR:
      expand_complex_negation (bsi, inner_type, ar, ai);
      break;

    case CONJ_EXPR:
      expand_complex_conjugate (bsi, inner_type, ar, ai);
      break;

    case EQ_EXPR:
    case NE_EXPR:
      expand_complex_comparison (bsi, ar, ai, br, bi, code);
      break;

    default:
      gcc_unreachable ();
    }
}


/* Entry point for complex operation lowering during optimization.  */

static unsigned int
tree_lower_complex (void)
{
  int old_last_basic_block;
  block_stmt_iterator bsi;
  basic_block bb;

  if (!init_dont_simulate_again ())
    return 0;

  complex_lattice_values = VEC_alloc (complex_lattice_t, heap, num_ssa_names);
  VEC_safe_grow (complex_lattice_t, heap,
		 complex_lattice_values, num_ssa_names);
  memset (VEC_address (complex_lattice_t, complex_lattice_values), 0,
	  num_ssa_names * sizeof(complex_lattice_t));

  init_parameter_lattice_values ();
  ssa_propagate (complex_visit_stmt, complex_visit_phi);

  complex_variable_components = htab_create (10,  int_tree_map_hash,
					     int_tree_map_eq, free);

  complex_ssa_name_components = VEC_alloc (tree, heap, 2*num_ssa_names);
  VEC_safe_grow (tree, heap, complex_ssa_name_components, 2*num_ssa_names);
  memset (VEC_address (tree, complex_ssa_name_components), 0,
	  2 * num_ssa_names * sizeof(tree));

  update_parameter_components ();

  /* ??? Ideally we'd traverse the blocks in breadth-first order.  */
  old_last_basic_block = last_basic_block;
  FOR_EACH_BB (bb)
    {
      if (bb->index >= old_last_basic_block)
	continue;
      update_phi_components (bb);
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	expand_complex_operations_1 (&bsi);
    }

  bsi_commit_edge_inserts ();

  htab_delete (complex_variable_components);
  VEC_free (tree, heap, complex_ssa_name_components);
  VEC_free (complex_lattice_t, heap, complex_lattice_values);
  return 0;
}

struct tree_opt_pass pass_lower_complex = 
{
  "cplxlower",				/* name */
  0,					/* gate */
  tree_lower_complex,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_ssa,				/* properties_required */
  0,					/* properties_provided */
  PROP_smt_usage,                       /* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_ggc_collect
  | TODO_update_smt_usage
  | TODO_update_ssa
  | TODO_verify_stmts,		        /* todo_flags_finish */
  0					/* letter */
};


/* Entry point for complex operation lowering without optimization.  */

static unsigned int
tree_lower_complex_O0 (void)
{
  int old_last_basic_block = last_basic_block;
  block_stmt_iterator bsi;
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      if (bb->index >= old_last_basic_block)
	continue;
      for (bsi = bsi_start (bb); !bsi_end_p (bsi); bsi_next (&bsi))
	expand_complex_operations_1 (&bsi);
    }
  return 0;
}

static bool
gate_no_optimization (void)
{
  /* With errors, normal optimization passes are not run.  If we don't
     lower complex operations at all, rtl expansion will abort.  */
  return optimize == 0 || sorrycount || errorcount;
}

struct tree_opt_pass pass_lower_complex_O0 = 
{
  "cplxlower0",				/* name */
  gate_no_optimization,			/* gate */
  tree_lower_complex_O0,		/* execute */
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
