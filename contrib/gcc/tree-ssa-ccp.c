/* Conditional constant propagation pass for the GNU compiler.
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Adapted from original RTL SSA-CCP by Daniel Berlin <dberlin@dberlin.org>
   Adapted to GIMPLE trees by Diego Novillo <dnovillo@redhat.com>

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

/* Conditional constant propagation (CCP) is based on the SSA
   propagation engine (tree-ssa-propagate.c).  Constant assignments of
   the form VAR = CST are propagated from the assignments into uses of
   VAR, which in turn may generate new constants.  The simulation uses
   a four level lattice to keep track of constant values associated
   with SSA names.  Given an SSA name V_i, it may take one of the
   following values:

   	UNINITIALIZED	->  This is the default starting value.  V_i
			    has not been processed yet.

	UNDEFINED	->  V_i is a local variable whose definition
			    has not been processed yet.  Therefore we
			    don't yet know if its value is a constant
			    or not.

	CONSTANT	->  V_i has been found to hold a constant
			    value C.

	VARYING		->  V_i cannot take a constant value, or if it
			    does, it is not possible to determine it
			    at compile time.

   The core of SSA-CCP is in ccp_visit_stmt and ccp_visit_phi_node:

   1- In ccp_visit_stmt, we are interested in assignments whose RHS
      evaluates into a constant and conditional jumps whose predicate
      evaluates into a boolean true or false.  When an assignment of
      the form V_i = CONST is found, V_i's lattice value is set to
      CONSTANT and CONST is associated with it.  This causes the
      propagation engine to add all the SSA edges coming out the
      assignment into the worklists, so that statements that use V_i
      can be visited.

      If the statement is a conditional with a constant predicate, we
      mark the outgoing edges as executable or not executable
      depending on the predicate's value.  This is then used when
      visiting PHI nodes to know when a PHI argument can be ignored.
      

   2- In ccp_visit_phi_node, if all the PHI arguments evaluate to the
      same constant C, then the LHS of the PHI is set to C.  This
      evaluation is known as the "meet operation".  Since one of the
      goals of this evaluation is to optimistically return constant
      values as often as possible, it uses two main short cuts:

      - If an argument is flowing in through a non-executable edge, it
	is ignored.  This is useful in cases like this:

			if (PRED)
			  a_9 = 3;
			else
			  a_10 = 100;
			a_11 = PHI (a_9, a_10)

	If PRED is known to always evaluate to false, then we can
	assume that a_11 will always take its value from a_10, meaning
	that instead of consider it VARYING (a_9 and a_10 have
	different values), we can consider it CONSTANT 100.

      - If an argument has an UNDEFINED value, then it does not affect
	the outcome of the meet operation.  If a variable V_i has an
	UNDEFINED value, it means that either its defining statement
	hasn't been visited yet or V_i has no defining statement, in
	which case the original symbol 'V' is being used
	uninitialized.  Since 'V' is a local variable, the compiler
	may assume any initial value for it.


   After propagation, every variable V_i that ends up with a lattice
   value of CONSTANT will have the associated constant value in the
   array CONST_VAL[i].VALUE.  That is fed into substitute_and_fold for
   final substitution and folding.


   Constant propagation in stores and loads (STORE-CCP)
   ----------------------------------------------------

   While CCP has all the logic to propagate constants in GIMPLE
   registers, it is missing the ability to associate constants with
   stores and loads (i.e., pointer dereferences, structures and
   global/aliased variables).  We don't keep loads and stores in
   SSA, but we do build a factored use-def web for them (in the
   virtual operands).

   For instance, consider the following code fragment:

	  struct A a;
	  const int B = 42;

	  void foo (int i)
	  {
	    if (i > 10)
	      a.a = 42;
	    else
	      {
		a.b = 21;
		a.a = a.b + 21;
	      }

	    if (a.a != B)
	      never_executed ();
	  }

   We should be able to deduce that the predicate 'a.a != B' is always
   false.  To achieve this, we associate constant values to the SSA
   names in the V_MAY_DEF and V_MUST_DEF operands for each store.
   Additionally, since we also glob partial loads/stores with the base
   symbol, we also keep track of the memory reference where the
   constant value was stored (in the MEM_REF field of PROP_VALUE_T).
   For instance,

        # a_5 = V_MAY_DEF <a_4>
        a.a = 2;

        # VUSE <a_5>
        x_3 = a.b;

   In the example above, CCP will associate value '2' with 'a_5', but
   it would be wrong to replace the load from 'a.b' with '2', because
   '2' had been stored into a.a.

   To support STORE-CCP, it is necessary to add a new value to the
   constant propagation lattice.  When evaluating a load for a memory
   reference we can no longer assume a value of UNDEFINED if we
   haven't seen a preceding store to the same memory location.
   Consider, for instance global variables:

   	int A;

   	foo (int i)
  	{
	  if (i_3 > 10)
	    A_4 = 3;
          # A_5 = PHI (A_4, A_2);

	  # VUSE <A_5>
	  A.0_6 = A;

	  return A.0_6;
	}

   The value of A_2 cannot be assumed to be UNDEFINED, as it may have
   been defined outside of foo.  If we were to assume it UNDEFINED, we
   would erroneously optimize the above into 'return 3;'.  Therefore,
   when doing STORE-CCP, we introduce a fifth lattice value
   (UNKNOWN_VAL), which overrides any other value when computing the
   meet operation in PHI nodes.

   Though STORE-CCP is not too expensive, it does have to do more work
   than regular CCP, so it is only enabled at -O2.  Both regular CCP
   and STORE-CCP use the exact same algorithm.  The only distinction
   is that when doing STORE-CCP, the boolean variable DO_STORE_CCP is
   set to true.  This affects the evaluation of statements and PHI
   nodes.

   References:

     Constant propagation with conditional branches,
     Wegman and Zadeck, ACM TOPLAS 13(2):181-210.

     Building an Optimizing Compiler,
     Robert Morgan, Butterworth-Heinemann, 1998, Section 8.9.

     Advanced Compiler Design and Implementation,
     Steven Muchnick, Morgan Kaufmann, 1997, Section 12.6  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "flags.h"
#include "rtl.h"
#include "tm_p.h"
#include "ggc.h"
#include "basic-block.h"
#include "output.h"
#include "expr.h"
#include "function.h"
#include "diagnostic.h"
#include "timevar.h"
#include "tree-dump.h"
#include "tree-flow.h"
#include "tree-pass.h"
#include "tree-ssa-propagate.h"
#include "langhooks.h"
#include "target.h"
#include "toplev.h"


/* Possible lattice values.  */
typedef enum
{
  UNINITIALIZED = 0,
  UNDEFINED,
  UNKNOWN_VAL,
  CONSTANT,
  VARYING
} ccp_lattice_t;

/* Array of propagated constant values.  After propagation,
   CONST_VAL[I].VALUE holds the constant value for SSA_NAME(I).  If
   the constant is held in an SSA name representing a memory store
   (i.e., a V_MAY_DEF or V_MUST_DEF), CONST_VAL[I].MEM_REF will
   contain the actual memory reference used to store (i.e., the LHS of
   the assignment doing the store).  */
static prop_value_t *const_val;

/* True if we are also propagating constants in stores and loads.  */
static bool do_store_ccp;

/* Dump constant propagation value VAL to file OUTF prefixed by PREFIX.  */

static void
dump_lattice_value (FILE *outf, const char *prefix, prop_value_t val)
{
  switch (val.lattice_val)
    {
    case UNINITIALIZED:
      fprintf (outf, "%sUNINITIALIZED", prefix);
      break;
    case UNDEFINED:
      fprintf (outf, "%sUNDEFINED", prefix);
      break;
    case VARYING:
      fprintf (outf, "%sVARYING", prefix);
      break;
    case UNKNOWN_VAL:
      fprintf (outf, "%sUNKNOWN_VAL", prefix);
      break;
    case CONSTANT:
      fprintf (outf, "%sCONSTANT ", prefix);
      print_generic_expr (outf, val.value, dump_flags);
      break;
    default:
      gcc_unreachable ();
    }
}


/* Print lattice value VAL to stderr.  */

void debug_lattice_value (prop_value_t val);

void
debug_lattice_value (prop_value_t val)
{
  dump_lattice_value (stderr, "", val);
  fprintf (stderr, "\n");
}


/* The regular is_gimple_min_invariant does a shallow test of the object.
   It assumes that full gimplification has happened, or will happen on the
   object.  For a value coming from DECL_INITIAL, this is not true, so we
   have to be more strict ourselves.  */

static bool
ccp_decl_initial_min_invariant (tree t)
{
  if (!is_gimple_min_invariant (t))
    return false;
  if (TREE_CODE (t) == ADDR_EXPR)
    {
      /* Inline and unroll is_gimple_addressable.  */
      while (1)
	{
	  t = TREE_OPERAND (t, 0);
	  if (is_gimple_id (t))
	    return true;
	  if (!handled_component_p (t))
	    return false;
	}
    }
  return true;
}


/* Compute a default value for variable VAR and store it in the
   CONST_VAL array.  The following rules are used to get default
   values:

   1- Global and static variables that are declared constant are
      considered CONSTANT.

   2- Any other value is considered UNDEFINED.  This is useful when
      considering PHI nodes.  PHI arguments that are undefined do not
      change the constant value of the PHI node, which allows for more
      constants to be propagated.

   3- If SSA_NAME_VALUE is set and it is a constant, its value is
      used.

   4- Variables defined by statements other than assignments and PHI
      nodes are considered VARYING.

   5- Variables that are not GIMPLE registers are considered
      UNKNOWN_VAL, which is really a stronger version of UNDEFINED.
      It's used to avoid the short circuit evaluation implied by
      UNDEFINED in ccp_lattice_meet.  */

static prop_value_t
get_default_value (tree var)
{
  tree sym = SSA_NAME_VAR (var);
  prop_value_t val = { UNINITIALIZED, NULL_TREE, NULL_TREE };

  if (!do_store_ccp && !is_gimple_reg (var))
    {
      /* Short circuit for regular CCP.  We are not interested in any
	 non-register when DO_STORE_CCP is false.  */
      val.lattice_val = VARYING;
    }
  else if (SSA_NAME_VALUE (var)
	   && is_gimple_min_invariant (SSA_NAME_VALUE (var)))
    {
      val.lattice_val = CONSTANT;
      val.value = SSA_NAME_VALUE (var);
    }
  else if (TREE_STATIC (sym)
	   && TREE_READONLY (sym)
	   && !MTAG_P (sym)
	   && DECL_INITIAL (sym)
	   && ccp_decl_initial_min_invariant (DECL_INITIAL (sym)))
    {
      /* Globals and static variables declared 'const' take their
	 initial value.  */
      val.lattice_val = CONSTANT;
      val.value = DECL_INITIAL (sym);
      val.mem_ref = sym;
    }
  else
    {
      tree stmt = SSA_NAME_DEF_STMT (var);

      if (IS_EMPTY_STMT (stmt))
	{
	  /* Variables defined by an empty statement are those used
	     before being initialized.  If VAR is a local variable, we
	     can assume initially that it is UNDEFINED.  If we are
	     doing STORE-CCP, function arguments and non-register
	     variables are initially UNKNOWN_VAL, because we cannot
	     discard the value incoming from outside of this function
	     (see ccp_lattice_meet for details).  */
	  if (is_gimple_reg (sym) && TREE_CODE (sym) != PARM_DECL)
	    val.lattice_val = UNDEFINED;
	  else if (do_store_ccp)
	    val.lattice_val = UNKNOWN_VAL;
	  else
	    val.lattice_val = VARYING;
	}
      else if (TREE_CODE (stmt) == MODIFY_EXPR
	       || TREE_CODE (stmt) == PHI_NODE)
	{
	  /* Any other variable defined by an assignment or a PHI node
	     is considered UNDEFINED (or UNKNOWN_VAL if VAR is not a
	     GIMPLE register).  */
	  val.lattice_val = is_gimple_reg (sym) ? UNDEFINED : UNKNOWN_VAL;
	}
      else
	{
	  /* Otherwise, VAR will never take on a constant value.  */
	  val.lattice_val = VARYING;
	}
    }

  return val;
}


/* Get the constant value associated with variable VAR.  If
   MAY_USE_DEFAULT_P is true, call get_default_value on variables that
   have the lattice value UNINITIALIZED.  */

static prop_value_t *
get_value (tree var, bool may_use_default_p)
{
  prop_value_t *val = &const_val[SSA_NAME_VERSION (var)];
  if (may_use_default_p && val->lattice_val == UNINITIALIZED)
    *val = get_default_value (var);

  return val;
}


/* Set the value for variable VAR to NEW_VAL.  Return true if the new
   value is different from VAR's previous value.  */

static bool
set_lattice_value (tree var, prop_value_t new_val)
{
  prop_value_t *old_val = get_value (var, false);

  /* Lattice transitions must always be monotonically increasing in
     value.  We allow two exceptions:
     
     1- If *OLD_VAL and NEW_VAL are the same, return false to
	inform the caller that this was a non-transition.

     2- If we are doing store-ccp (i.e., DOING_STORE_CCP is true),
	allow CONSTANT->UNKNOWN_VAL.  The UNKNOWN_VAL state is a
	special type of UNDEFINED state which prevents the short
	circuit evaluation of PHI arguments (see ccp_visit_phi_node
	and ccp_lattice_meet).  */
  gcc_assert (old_val->lattice_val <= new_val.lattice_val
              || (old_val->lattice_val == new_val.lattice_val
		  && old_val->value == new_val.value
		  && old_val->mem_ref == new_val.mem_ref)
	      || (do_store_ccp
		  && old_val->lattice_val == CONSTANT
		  && new_val.lattice_val == UNKNOWN_VAL));

  if (old_val->lattice_val != new_val.lattice_val)
    {
      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  dump_lattice_value (dump_file, "Lattice value changed to ", new_val);
	  fprintf (dump_file, ".  %sdding SSA edges to worklist.\n",
	           new_val.lattice_val != UNDEFINED ? "A" : "Not a");
	}

      *old_val = new_val;

      /* Transitions UNINITIALIZED -> UNDEFINED are never interesting
	 for propagation purposes.  In these cases return false to
	 avoid doing useless work.  */
      return (new_val.lattice_val != UNDEFINED);
    }

  return false;
}


/* Return the likely CCP lattice value for STMT.

   If STMT has no operands, then return CONSTANT.

   Else if any operands of STMT are undefined, then return UNDEFINED.

   Else if any operands of STMT are constants, then return CONSTANT.

   Else return VARYING.  */

static ccp_lattice_t
likely_value (tree stmt)
{
  bool found_constant;
  stmt_ann_t ann;
  tree use;
  ssa_op_iter iter;

  ann = stmt_ann (stmt);

  /* If the statement has volatile operands, it won't fold to a
     constant value.  */
  if (ann->has_volatile_ops)
    return VARYING;

  /* If we are not doing store-ccp, statements with loads
     and/or stores will never fold into a constant.  */
  if (!do_store_ccp
      && !ZERO_SSA_OPERANDS (stmt, SSA_OP_ALL_VIRTUALS))
    return VARYING;


  /* A CALL_EXPR is assumed to be varying.  NOTE: This may be overly
     conservative, in the presence of const and pure calls.  */
  if (get_call_expr_in (stmt) != NULL_TREE)
    return VARYING;

  /* Anything other than assignments and conditional jumps are not
     interesting for CCP.  */
  if (TREE_CODE (stmt) != MODIFY_EXPR
      && TREE_CODE (stmt) != COND_EXPR
      && TREE_CODE (stmt) != SWITCH_EXPR)
    return VARYING;

  if (is_gimple_min_invariant (get_rhs (stmt)))
    return CONSTANT;

  found_constant = false;
  FOR_EACH_SSA_TREE_OPERAND (use, stmt, iter, SSA_OP_USE|SSA_OP_VUSE)
    {
      prop_value_t *val = get_value (use, true);

      if (val->lattice_val == VARYING)
	return VARYING;

      if (val->lattice_val == UNKNOWN_VAL)
	{
	  /* UNKNOWN_VAL is invalid when not doing STORE-CCP.  */
	  gcc_assert (do_store_ccp);
	  return UNKNOWN_VAL;
	}

      if (val->lattice_val == CONSTANT)
	found_constant = true;
    }

  if (found_constant
      || ZERO_SSA_OPERANDS (stmt, SSA_OP_USE)
      || ZERO_SSA_OPERANDS (stmt, SSA_OP_VUSE))
    return CONSTANT;

  return UNDEFINED;
}


/* Initialize local data structures for CCP.  */

static void
ccp_initialize (void)
{
  basic_block bb;

  const_val = XNEWVEC (prop_value_t, num_ssa_names);
  memset (const_val, 0, num_ssa_names * sizeof (*const_val));

  /* Initialize simulation flags for PHI nodes and statements.  */
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;

      for (i = bsi_start (bb); !bsi_end_p (i); bsi_next (&i))
        {
	  bool is_varying = false;
	  tree stmt = bsi_stmt (i);

	  if (likely_value (stmt) == VARYING)

	    {
	      tree def;
	      ssa_op_iter iter;

	      /* If the statement will not produce a constant, mark
		 all its outputs VARYING.  */
	      FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_ALL_DEFS)
		get_value (def, false)->lattice_val = VARYING;

	      /* Never mark conditional jumps with DONT_SIMULATE_AGAIN,
		 otherwise the propagator will never add the outgoing
		 control edges.  */
	      if (TREE_CODE (stmt) != COND_EXPR
		  && TREE_CODE (stmt) != SWITCH_EXPR)
		is_varying = true;
	    }

	  DONT_SIMULATE_AGAIN (stmt) = is_varying;
	}
    }

  /* Now process PHI nodes.  */
  FOR_EACH_BB (bb)
    {
      tree phi;

      for (phi = phi_nodes (bb); phi; phi = PHI_CHAIN (phi))
	{
	  int i;
	  tree arg;
	  prop_value_t *val = get_value (PHI_RESULT (phi), false);

	  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
	    {
	      arg = PHI_ARG_DEF (phi, i);

	      if (TREE_CODE (arg) == SSA_NAME
		  && get_value (arg, false)->lattice_val == VARYING)
		{
		  val->lattice_val = VARYING;
		  break;
		}
	    }

	  DONT_SIMULATE_AGAIN (phi) = (val->lattice_val == VARYING);
	}
    }
}


/* Do final substitution of propagated values, cleanup the flowgraph and
   free allocated storage.  */

static void
ccp_finalize (void)
{
  /* Perform substitutions based on the known constant values.  */
  substitute_and_fold (const_val, false);

  free (const_val);
}


/* Compute the meet operator between *VAL1 and *VAL2.  Store the result
   in VAL1.

   		any  M UNDEFINED   = any
		any  M UNKNOWN_VAL = UNKNOWN_VAL
		any  M VARYING     = VARYING
		Ci   M Cj	   = Ci		if (i == j)
		Ci   M Cj	   = VARYING	if (i != j)

   Lattice values UNKNOWN_VAL and UNDEFINED are similar but have
   different semantics at PHI nodes.  Both values imply that we don't
   know whether the variable is constant or not.  However, UNKNOWN_VAL
   values override all others.  For instance, suppose that A is a
   global variable:

		+------+
		|      |
		|     / \
		|    /   \
		|   |  A_1 = 4
		|    \   /
		|     \ /    
		| A_3 = PHI (A_2, A_1)
		| ... = A_3
		|    |
		+----+

   If the edge into A_2 is not executable, the first visit to A_3 will
   yield the constant 4.  But the second visit to A_3 will be with A_2
   in state UNKNOWN_VAL.  We can no longer conclude that A_3 is 4
   because A_2 may have been set in another function.  If we had used
   the lattice value UNDEFINED, we would have had wrongly concluded
   that A_3 is 4.  */
   

static void
ccp_lattice_meet (prop_value_t *val1, prop_value_t *val2)
{
  if (val1->lattice_val == UNDEFINED)
    {
      /* UNDEFINED M any = any   */
      *val1 = *val2;
    }
  else if (val2->lattice_val == UNDEFINED)
    {
      /* any M UNDEFINED = any
         Nothing to do.  VAL1 already contains the value we want.  */
      ;
    }
  else if (val1->lattice_val == UNKNOWN_VAL
           || val2->lattice_val == UNKNOWN_VAL)
    {
      /* UNKNOWN_VAL values are invalid if we are not doing STORE-CCP.  */
      gcc_assert (do_store_ccp);

      /* any M UNKNOWN_VAL = UNKNOWN_VAL.  */
      val1->lattice_val = UNKNOWN_VAL;
      val1->value = NULL_TREE;
      val1->mem_ref = NULL_TREE;
    }
  else if (val1->lattice_val == VARYING
           || val2->lattice_val == VARYING)
    {
      /* any M VARYING = VARYING.  */
      val1->lattice_val = VARYING;
      val1->value = NULL_TREE;
      val1->mem_ref = NULL_TREE;
    }
  else if (val1->lattice_val == CONSTANT
	   && val2->lattice_val == CONSTANT
	   && simple_cst_equal (val1->value, val2->value) == 1
	   && (!do_store_ccp
	       || (val1->mem_ref && val2->mem_ref
		   && operand_equal_p (val1->mem_ref, val2->mem_ref, 0))))
    {
      /* Ci M Cj = Ci		if (i == j)
	 Ci M Cj = VARYING	if (i != j)

         If these two values come from memory stores, make sure that
	 they come from the same memory reference.  */
      val1->lattice_val = CONSTANT;
      val1->value = val1->value;
      val1->mem_ref = val1->mem_ref;
    }
  else
    {
      /* Any other combination is VARYING.  */
      val1->lattice_val = VARYING;
      val1->value = NULL_TREE;
      val1->mem_ref = NULL_TREE;
    }
}


/* Loop through the PHI_NODE's parameters for BLOCK and compare their
   lattice values to determine PHI_NODE's lattice value.  The value of a
   PHI node is determined calling ccp_lattice_meet with all the arguments
   of the PHI node that are incoming via executable edges.  */

static enum ssa_prop_result
ccp_visit_phi_node (tree phi)
{
  int i;
  prop_value_t *old_val, new_val;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting PHI node: ");
      print_generic_expr (dump_file, phi, dump_flags);
    }

  old_val = get_value (PHI_RESULT (phi), false);
  switch (old_val->lattice_val)
    {
    case VARYING:
      return SSA_PROP_VARYING;

    case CONSTANT:
      new_val = *old_val;
      break;

    case UNKNOWN_VAL:
      /* To avoid the default value of UNKNOWN_VAL overriding
         that of its possible constant arguments, temporarily
	 set the PHI node's default lattice value to be 
	 UNDEFINED.  If the PHI node's old value was UNKNOWN_VAL and
	 the new value is UNDEFINED, then we prevent the invalid
	 transition by not calling set_lattice_value.  */
      gcc_assert (do_store_ccp);

      /* FALLTHRU  */

    case UNDEFINED:
    case UNINITIALIZED:
      new_val.lattice_val = UNDEFINED;
      new_val.value = NULL_TREE;
      new_val.mem_ref = NULL_TREE;
      break;

    default:
      gcc_unreachable ();
    }

  for (i = 0; i < PHI_NUM_ARGS (phi); i++)
    {
      /* Compute the meet operator over all the PHI arguments flowing
	 through executable edges.  */
      edge e = PHI_ARG_EDGE (phi, i);

      if (dump_file && (dump_flags & TDF_DETAILS))
	{
	  fprintf (dump_file,
	      "\n    Argument #%d (%d -> %d %sexecutable)\n",
	      i, e->src->index, e->dest->index,
	      (e->flags & EDGE_EXECUTABLE) ? "" : "not ");
	}

      /* If the incoming edge is executable, Compute the meet operator for
	 the existing value of the PHI node and the current PHI argument.  */
      if (e->flags & EDGE_EXECUTABLE)
	{
	  tree arg = PHI_ARG_DEF (phi, i);
	  prop_value_t arg_val;

	  if (is_gimple_min_invariant (arg))
	    {
	      arg_val.lattice_val = CONSTANT;
	      arg_val.value = arg;
	      arg_val.mem_ref = NULL_TREE;
	    }
	  else
	    arg_val = *(get_value (arg, true));

	  ccp_lattice_meet (&new_val, &arg_val);

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "\t");
	      print_generic_expr (dump_file, arg, dump_flags);
	      dump_lattice_value (dump_file, "\tValue: ", arg_val);
	      fprintf (dump_file, "\n");
	    }

	  if (new_val.lattice_val == VARYING)
	    break;
	}
    }

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      dump_lattice_value (dump_file, "\n    PHI node value: ", new_val);
      fprintf (dump_file, "\n\n");
    }

  /* Check for an invalid change from UNKNOWN_VAL to UNDEFINED.  */
  if (do_store_ccp
      && old_val->lattice_val == UNKNOWN_VAL
      && new_val.lattice_val == UNDEFINED)
    return SSA_PROP_NOT_INTERESTING;

  /* Otherwise, make the transition to the new value.  */
  if (set_lattice_value (PHI_RESULT (phi), new_val))
    {
      if (new_val.lattice_val == VARYING)
	return SSA_PROP_VARYING;
      else
	return SSA_PROP_INTERESTING;
    }
  else
    return SSA_PROP_NOT_INTERESTING;
}


/* CCP specific front-end to the non-destructive constant folding
   routines.

   Attempt to simplify the RHS of STMT knowing that one or more
   operands are constants.

   If simplification is possible, return the simplified RHS,
   otherwise return the original RHS.  */

static tree
ccp_fold (tree stmt)
{
  tree rhs = get_rhs (stmt);
  enum tree_code code = TREE_CODE (rhs);
  enum tree_code_class kind = TREE_CODE_CLASS (code);
  tree retval = NULL_TREE;

  if (TREE_CODE (rhs) == SSA_NAME)
    {
      /* If the RHS is an SSA_NAME, return its known constant value,
	 if any.  */
      return get_value (rhs, true)->value;
    }
  else if (do_store_ccp && stmt_makes_single_load (stmt))
    {
      /* If the RHS is a memory load, see if the VUSEs associated with
	 it are a valid constant for that memory load.  */
      prop_value_t *val = get_value_loaded_by (stmt, const_val);
      if (val && val->mem_ref)
	{
	  if (operand_equal_p (val->mem_ref, rhs, 0))
	    return val->value;

	  /* If RHS is extracting REALPART_EXPR or IMAGPART_EXPR of a
	     complex type with a known constant value, return it.  */
	  if ((TREE_CODE (rhs) == REALPART_EXPR
	       || TREE_CODE (rhs) == IMAGPART_EXPR)
	      && operand_equal_p (val->mem_ref, TREE_OPERAND (rhs, 0), 0))
	    return fold_build1 (TREE_CODE (rhs), TREE_TYPE (rhs), val->value);
	}
      return NULL_TREE;
    }

  /* Unary operators.  Note that we know the single operand must
     be a constant.  So this should almost always return a
     simplified RHS.  */
  if (kind == tcc_unary)
    {
      /* Handle unary operators which can appear in GIMPLE form.  */
      tree op0 = TREE_OPERAND (rhs, 0);

      /* Simplify the operand down to a constant.  */
      if (TREE_CODE (op0) == SSA_NAME)
	{
	  prop_value_t *val = get_value (op0, true);
	  if (val->lattice_val == CONSTANT)
	    op0 = get_value (op0, true)->value;
	}

      if ((code == NOP_EXPR || code == CONVERT_EXPR)
	  && tree_ssa_useless_type_conversion_1 (TREE_TYPE (rhs),
		  				 TREE_TYPE (op0)))
	return op0;
      return fold_unary (code, TREE_TYPE (rhs), op0);
    }

  /* Binary and comparison operators.  We know one or both of the
     operands are constants.  */
  else if (kind == tcc_binary
           || kind == tcc_comparison
           || code == TRUTH_AND_EXPR
           || code == TRUTH_OR_EXPR
           || code == TRUTH_XOR_EXPR)
    {
      /* Handle binary and comparison operators that can appear in
         GIMPLE form.  */
      tree op0 = TREE_OPERAND (rhs, 0);
      tree op1 = TREE_OPERAND (rhs, 1);

      /* Simplify the operands down to constants when appropriate.  */
      if (TREE_CODE (op0) == SSA_NAME)
	{
	  prop_value_t *val = get_value (op0, true);
	  if (val->lattice_val == CONSTANT)
	    op0 = val->value;
	}

      if (TREE_CODE (op1) == SSA_NAME)
	{
	  prop_value_t *val = get_value (op1, true);
	  if (val->lattice_val == CONSTANT)
	    op1 = val->value;
	}

      return fold_binary (code, TREE_TYPE (rhs), op0, op1);
    }

  /* We may be able to fold away calls to builtin functions if their
     arguments are constants.  */
  else if (code == CALL_EXPR
	   && TREE_CODE (TREE_OPERAND (rhs, 0)) == ADDR_EXPR
	   && (TREE_CODE (TREE_OPERAND (TREE_OPERAND (rhs, 0), 0))
	       == FUNCTION_DECL)
	   && DECL_BUILT_IN (TREE_OPERAND (TREE_OPERAND (rhs, 0), 0)))
    {
      if (!ZERO_SSA_OPERANDS (stmt, SSA_OP_USE))
	{
	  tree *orig, var;
	  tree fndecl, arglist;
	  size_t i = 0;
	  ssa_op_iter iter;
	  use_operand_p var_p;

	  /* Preserve the original values of every operand.  */
	  orig = XNEWVEC (tree,  NUM_SSA_OPERANDS (stmt, SSA_OP_USE));
	  FOR_EACH_SSA_TREE_OPERAND (var, stmt, iter, SSA_OP_USE)
	    orig[i++] = var;

	  /* Substitute operands with their values and try to fold.  */
	  replace_uses_in (stmt, NULL, const_val);
	  fndecl = get_callee_fndecl (rhs);
	  arglist = TREE_OPERAND (rhs, 1);
	  retval = fold_builtin (fndecl, arglist, false);

	  /* Restore operands to their original form.  */
	  i = 0;
	  FOR_EACH_SSA_USE_OPERAND (var_p, stmt, iter, SSA_OP_USE)
	    SET_USE (var_p, orig[i++]);
	  free (orig);
	}
    }
  else
    return rhs;

  /* If we got a simplified form, see if we need to convert its type.  */
  if (retval)
    return fold_convert (TREE_TYPE (rhs), retval);

  /* No simplification was possible.  */
  return rhs;
}


/* Return the tree representing the element referenced by T if T is an
   ARRAY_REF or COMPONENT_REF into constant aggregates.  Return
   NULL_TREE otherwise.  */

static tree
fold_const_aggregate_ref (tree t)
{
  prop_value_t *value;
  tree base, ctor, idx, field;
  unsigned HOST_WIDE_INT cnt;
  tree cfield, cval;

  switch (TREE_CODE (t))
    {
    case ARRAY_REF:
      /* Get a CONSTRUCTOR.  If BASE is a VAR_DECL, get its
	 DECL_INITIAL.  If BASE is a nested reference into another
	 ARRAY_REF or COMPONENT_REF, make a recursive call to resolve
	 the inner reference.  */
      base = TREE_OPERAND (t, 0);
      switch (TREE_CODE (base))
	{
	case VAR_DECL:
	  if (!TREE_READONLY (base)
	      || TREE_CODE (TREE_TYPE (base)) != ARRAY_TYPE
	      || !targetm.binds_local_p (base))
	    return NULL_TREE;

	  ctor = DECL_INITIAL (base);
	  break;

	case ARRAY_REF:
	case COMPONENT_REF:
	  ctor = fold_const_aggregate_ref (base);
	  break;

	default:
	  return NULL_TREE;
	}

      if (ctor == NULL_TREE
	  || (TREE_CODE (ctor) != CONSTRUCTOR
	      && TREE_CODE (ctor) != STRING_CST)
	  || !TREE_STATIC (ctor))
	return NULL_TREE;

      /* Get the index.  If we have an SSA_NAME, try to resolve it
	 with the current lattice value for the SSA_NAME.  */
      idx = TREE_OPERAND (t, 1);
      switch (TREE_CODE (idx))
	{
	case SSA_NAME:
	  if ((value = get_value (idx, true))
	      && value->lattice_val == CONSTANT
	      && TREE_CODE (value->value) == INTEGER_CST)
	    idx = value->value;
	  else
	    return NULL_TREE;
	  break;

	case INTEGER_CST:
	  break;

	default:
	  return NULL_TREE;
	}

      /* Fold read from constant string.  */
      if (TREE_CODE (ctor) == STRING_CST)
	{
	  if ((TYPE_MODE (TREE_TYPE (t))
	       == TYPE_MODE (TREE_TYPE (TREE_TYPE (ctor))))
	      && (GET_MODE_CLASS (TYPE_MODE (TREE_TYPE (TREE_TYPE (ctor))))
	          == MODE_INT)
	      && GET_MODE_SIZE (TYPE_MODE (TREE_TYPE (TREE_TYPE (ctor)))) == 1
	      && compare_tree_int (idx, TREE_STRING_LENGTH (ctor)) < 0)
	    return build_int_cst (TREE_TYPE (t), (TREE_STRING_POINTER (ctor)
					          [TREE_INT_CST_LOW (idx)]));
	  return NULL_TREE;
	}

      /* Whoo-hoo!  I'll fold ya baby.  Yeah!  */
      FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (ctor), cnt, cfield, cval)
	if (tree_int_cst_equal (cfield, idx))
	  return cval;
      break;

    case COMPONENT_REF:
      /* Get a CONSTRUCTOR.  If BASE is a VAR_DECL, get its
	 DECL_INITIAL.  If BASE is a nested reference into another
	 ARRAY_REF or COMPONENT_REF, make a recursive call to resolve
	 the inner reference.  */
      base = TREE_OPERAND (t, 0);
      switch (TREE_CODE (base))
	{
	case VAR_DECL:
	  if (!TREE_READONLY (base)
	      || TREE_CODE (TREE_TYPE (base)) != RECORD_TYPE
	      || !targetm.binds_local_p (base))
	    return NULL_TREE;

	  ctor = DECL_INITIAL (base);
	  break;

	case ARRAY_REF:
	case COMPONENT_REF:
	  ctor = fold_const_aggregate_ref (base);
	  break;

	default:
	  return NULL_TREE;
	}

      if (ctor == NULL_TREE
	  || TREE_CODE (ctor) != CONSTRUCTOR
	  || !TREE_STATIC (ctor))
	return NULL_TREE;

      field = TREE_OPERAND (t, 1);

      FOR_EACH_CONSTRUCTOR_ELT (CONSTRUCTOR_ELTS (ctor), cnt, cfield, cval)
	if (cfield == field
	    /* FIXME: Handle bit-fields.  */
	    && ! DECL_BIT_FIELD (cfield))
	  return cval;
      break;

    case REALPART_EXPR:
    case IMAGPART_EXPR:
      {
	tree c = fold_const_aggregate_ref (TREE_OPERAND (t, 0));
	if (c && TREE_CODE (c) == COMPLEX_CST)
	  return fold_build1 (TREE_CODE (t), TREE_TYPE (t), c);
	break;
      }
    
    default:
      break;
    }

  return NULL_TREE;
}
  
/* Evaluate statement STMT.  */

static prop_value_t
evaluate_stmt (tree stmt)
{
  prop_value_t val;
  tree simplified = NULL_TREE;
  ccp_lattice_t likelyvalue = likely_value (stmt);
  bool is_constant;

  val.mem_ref = NULL_TREE;

  fold_defer_overflow_warnings ();

  /* If the statement is likely to have a CONSTANT result, then try
     to fold the statement to determine the constant value.  */
  if (likelyvalue == CONSTANT)
    simplified = ccp_fold (stmt);
  /* If the statement is likely to have a VARYING result, then do not
     bother folding the statement.  */
  if (likelyvalue == VARYING)
    simplified = get_rhs (stmt);
  /* If the statement is an ARRAY_REF or COMPONENT_REF into constant
     aggregates, extract the referenced constant.  Otherwise the
     statement is likely to have an UNDEFINED value, and there will be
     nothing to do.  Note that fold_const_aggregate_ref returns
     NULL_TREE if the first case does not match.  */
  else if (!simplified)
    simplified = fold_const_aggregate_ref (get_rhs (stmt));

  is_constant = simplified && is_gimple_min_invariant (simplified);

  fold_undefer_overflow_warnings (is_constant, stmt, 0);

  if (is_constant)
    {
      /* The statement produced a constant value.  */
      val.lattice_val = CONSTANT;
      val.value = simplified;
    }
  else
    {
      /* The statement produced a nonconstant value.  If the statement
	 had UNDEFINED operands, then the result of the statement
	 should be UNDEFINED.  Otherwise, the statement is VARYING.  */
      if (likelyvalue == UNDEFINED || likelyvalue == UNKNOWN_VAL)
	val.lattice_val = likelyvalue;
      else
	val.lattice_val = VARYING;

      val.value = NULL_TREE;
    }

  return val;
}


/* Visit the assignment statement STMT.  Set the value of its LHS to the
   value computed by the RHS and store LHS in *OUTPUT_P.  If STMT
   creates virtual definitions, set the value of each new name to that
   of the RHS (if we can derive a constant out of the RHS).  */

static enum ssa_prop_result
visit_assignment (tree stmt, tree *output_p)
{
  prop_value_t val;
  tree lhs, rhs;
  enum ssa_prop_result retval;

  lhs = TREE_OPERAND (stmt, 0);
  rhs = TREE_OPERAND (stmt, 1);

  if (TREE_CODE (rhs) == SSA_NAME)
    {
      /* For a simple copy operation, we copy the lattice values.  */
      prop_value_t *nval = get_value (rhs, true);
      val = *nval;
    }
  else if (do_store_ccp && stmt_makes_single_load (stmt))
    {
      /* Same as above, but the RHS is not a gimple register and yet
         has a known VUSE.  If STMT is loading from the same memory
	 location that created the SSA_NAMEs for the virtual operands,
	 we can propagate the value on the RHS.  */
      prop_value_t *nval = get_value_loaded_by (stmt, const_val);

      if (nval && nval->mem_ref
	  && operand_equal_p (nval->mem_ref, rhs, 0))
	val = *nval;
      else
	val = evaluate_stmt (stmt);
    }
  else
    /* Evaluate the statement.  */
      val = evaluate_stmt (stmt);

  /* If the original LHS was a VIEW_CONVERT_EXPR, modify the constant
     value to be a VIEW_CONVERT_EXPR of the old constant value.

     ??? Also, if this was a definition of a bitfield, we need to widen
     the constant value into the type of the destination variable.  This
     should not be necessary if GCC represented bitfields properly.  */
  {
    tree orig_lhs = TREE_OPERAND (stmt, 0);

    if (TREE_CODE (orig_lhs) == VIEW_CONVERT_EXPR
	&& val.lattice_val == CONSTANT)
      {
	tree w = fold_unary (VIEW_CONVERT_EXPR,
			     TREE_TYPE (TREE_OPERAND (orig_lhs, 0)),
			     val.value);

	orig_lhs = TREE_OPERAND (orig_lhs, 0);
	if (w && is_gimple_min_invariant (w))
	  val.value = w;
	else
	  {
	    val.lattice_val = VARYING;
	    val.value = NULL;
	  }
      }

    if (val.lattice_val == CONSTANT
	&& TREE_CODE (orig_lhs) == COMPONENT_REF
	&& DECL_BIT_FIELD (TREE_OPERAND (orig_lhs, 1)))
      {
	tree w = widen_bitfield (val.value, TREE_OPERAND (orig_lhs, 1),
				 orig_lhs);

	if (w && is_gimple_min_invariant (w))
	  val.value = w;
	else
	  {
	    val.lattice_val = VARYING;
	    val.value = NULL_TREE;
	    val.mem_ref = NULL_TREE;
	  }
      }
  }

  retval = SSA_PROP_NOT_INTERESTING;

  /* Set the lattice value of the statement's output.  */
  if (TREE_CODE (lhs) == SSA_NAME)
    {
      /* If STMT is an assignment to an SSA_NAME, we only have one
	 value to set.  */
      if (set_lattice_value (lhs, val))
	{
	  *output_p = lhs;
	  if (val.lattice_val == VARYING)
	    retval = SSA_PROP_VARYING;
	  else
	    retval = SSA_PROP_INTERESTING;
	}
    }
  else if (do_store_ccp && stmt_makes_single_store (stmt))
    {
      /* Otherwise, set the names in V_MAY_DEF/V_MUST_DEF operands
	 to the new constant value and mark the LHS as the memory
	 reference associated with VAL.  */
      ssa_op_iter i;
      tree vdef;
      bool changed;

      /* Stores cannot take on an UNDEFINED value.  */
      if (val.lattice_val == UNDEFINED)
	val.lattice_val = UNKNOWN_VAL;      

      /* Mark VAL as stored in the LHS of this assignment.  */
      val.mem_ref = lhs;

      /* Set the value of every VDEF to VAL.  */
      changed = false;
      FOR_EACH_SSA_TREE_OPERAND (vdef, stmt, i, SSA_OP_VIRTUAL_DEFS)
	changed |= set_lattice_value (vdef, val);
      
      /* Note that for propagation purposes, we are only interested in
	 visiting statements that load the exact same memory reference
	 stored here.  Those statements will have the exact same list
	 of virtual uses, so it is enough to set the output of this
	 statement to be its first virtual definition.  */
      *output_p = first_vdef (stmt);
      if (changed)
	{
	  if (val.lattice_val == VARYING)
	    retval = SSA_PROP_VARYING;
	  else 
	    retval = SSA_PROP_INTERESTING;
	}
    }

  return retval;
}


/* Visit the conditional statement STMT.  Return SSA_PROP_INTERESTING
   if it can determine which edge will be taken.  Otherwise, return
   SSA_PROP_VARYING.  */

static enum ssa_prop_result
visit_cond_stmt (tree stmt, edge *taken_edge_p)
{
  prop_value_t val;
  basic_block block;

  block = bb_for_stmt (stmt);
  val = evaluate_stmt (stmt);

  /* Find which edge out of the conditional block will be taken and add it
     to the worklist.  If no single edge can be determined statically,
     return SSA_PROP_VARYING to feed all the outgoing edges to the
     propagation engine.  */
  *taken_edge_p = val.value ? find_taken_edge (block, val.value) : 0;
  if (*taken_edge_p)
    return SSA_PROP_INTERESTING;
  else
    return SSA_PROP_VARYING;
}


/* Evaluate statement STMT.  If the statement produces an output value and
   its evaluation changes the lattice value of its output, return
   SSA_PROP_INTERESTING and set *OUTPUT_P to the SSA_NAME holding the
   output value.
   
   If STMT is a conditional branch and we can determine its truth
   value, set *TAKEN_EDGE_P accordingly.  If STMT produces a varying
   value, return SSA_PROP_VARYING.  */

static enum ssa_prop_result
ccp_visit_stmt (tree stmt, edge *taken_edge_p, tree *output_p)
{
  tree def;
  ssa_op_iter iter;

  if (dump_file && (dump_flags & TDF_DETAILS))
    {
      fprintf (dump_file, "\nVisiting statement:\n");
      print_generic_stmt (dump_file, stmt, dump_flags);
      fprintf (dump_file, "\n");
    }

  if (TREE_CODE (stmt) == MODIFY_EXPR)
    {
      /* If the statement is an assignment that produces a single
	 output value, evaluate its RHS to see if the lattice value of
	 its output has changed.  */
      return visit_assignment (stmt, output_p);
    }
  else if (TREE_CODE (stmt) == COND_EXPR || TREE_CODE (stmt) == SWITCH_EXPR)
    {
      /* If STMT is a conditional branch, see if we can determine
	 which branch will be taken.  */
      return visit_cond_stmt (stmt, taken_edge_p);
    }

  /* Any other kind of statement is not interesting for constant
     propagation and, therefore, not worth simulating.  */
  if (dump_file && (dump_flags & TDF_DETAILS))
    fprintf (dump_file, "No interesting values produced.  Marked VARYING.\n");

  /* Definitions made by statements other than assignments to
     SSA_NAMEs represent unknown modifications to their outputs.
     Mark them VARYING.  */
  FOR_EACH_SSA_TREE_OPERAND (def, stmt, iter, SSA_OP_ALL_DEFS)
    {
      prop_value_t v = { VARYING, NULL_TREE, NULL_TREE };
      set_lattice_value (def, v);
    }

  return SSA_PROP_VARYING;
}


/* Main entry point for SSA Conditional Constant Propagation.  */

static void
execute_ssa_ccp (bool store_ccp)
{
  do_store_ccp = store_ccp;
  ccp_initialize ();
  ssa_propagate (ccp_visit_stmt, ccp_visit_phi_node);
  ccp_finalize ();
}


static unsigned int
do_ssa_ccp (void)
{
  execute_ssa_ccp (false);
  return 0;
}


static bool
gate_ccp (void)
{
  return flag_tree_ccp != 0;
}


struct tree_opt_pass pass_ccp = 
{
  "ccp",				/* name */
  gate_ccp,				/* gate */
  do_ssa_ccp,				/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_CCP,				/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  PROP_smt_usage,			/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_cleanup_cfg | TODO_dump_func | TODO_update_ssa
    | TODO_ggc_collect | TODO_verify_ssa
    | TODO_verify_stmts | TODO_update_smt_usage, /* todo_flags_finish */
  0					/* letter */
};


static unsigned int
do_ssa_store_ccp (void)
{
  /* If STORE-CCP is not enabled, we just run regular CCP.  */
  execute_ssa_ccp (flag_tree_store_ccp != 0);
  return 0;
}

static bool
gate_store_ccp (void)
{
  /* STORE-CCP is enabled only with -ftree-store-ccp, but when
     -fno-tree-store-ccp is specified, we should run regular CCP.
     That's why the pass is enabled with either flag.  */
  return flag_tree_store_ccp != 0 || flag_tree_ccp != 0;
}


struct tree_opt_pass pass_store_ccp = 
{
  "store_ccp",				/* name */
  gate_store_ccp,			/* gate */
  do_ssa_store_ccp,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_TREE_STORE_CCP,			/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  PROP_smt_usage,			/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func | TODO_update_ssa
    | TODO_ggc_collect | TODO_verify_ssa
    | TODO_cleanup_cfg
    | TODO_verify_stmts | TODO_update_smt_usage, /* todo_flags_finish */
  0					/* letter */
};

/* Given a constant value VAL for bitfield FIELD, and a destination
   variable VAR, return VAL appropriately widened to fit into VAR.  If
   FIELD is wider than HOST_WIDE_INT, NULL is returned.  */

tree
widen_bitfield (tree val, tree field, tree var)
{
  unsigned HOST_WIDE_INT var_size, field_size;
  tree wide_val;
  unsigned HOST_WIDE_INT mask;
  unsigned int i;

  /* We can only do this if the size of the type and field and VAL are
     all constants representable in HOST_WIDE_INT.  */
  if (!host_integerp (TYPE_SIZE (TREE_TYPE (var)), 1)
      || !host_integerp (DECL_SIZE (field), 1)
      || !host_integerp (val, 0))
    return NULL_TREE;

  var_size = tree_low_cst (TYPE_SIZE (TREE_TYPE (var)), 1);
  field_size = tree_low_cst (DECL_SIZE (field), 1);

  /* Give up if either the bitfield or the variable are too wide.  */
  if (field_size > HOST_BITS_PER_WIDE_INT || var_size > HOST_BITS_PER_WIDE_INT)
    return NULL_TREE;

  gcc_assert (var_size >= field_size);

  /* If the sign bit of the value is not set or the field's type is unsigned,
     just mask off the high order bits of the value.  */
  if (DECL_UNSIGNED (field)
      || !(tree_low_cst (val, 0) & (((HOST_WIDE_INT)1) << (field_size - 1))))
    {
      /* Zero extension.  Build a mask with the lower 'field_size' bits
	 set and a BIT_AND_EXPR node to clear the high order bits of
	 the value.  */
      for (i = 0, mask = 0; i < field_size; i++)
	mask |= ((HOST_WIDE_INT) 1) << i;

      wide_val = fold_build2 (BIT_AND_EXPR, TREE_TYPE (var), val, 
			      build_int_cst (TREE_TYPE (var), mask));
    }
  else
    {
      /* Sign extension.  Create a mask with the upper 'field_size'
	 bits set and a BIT_IOR_EXPR to set the high order bits of the
	 value.  */
      for (i = 0, mask = 0; i < (var_size - field_size); i++)
	mask |= ((HOST_WIDE_INT) 1) << (var_size - i - 1);

      wide_val = fold_build2 (BIT_IOR_EXPR, TREE_TYPE (var), val,
			      build_int_cst (TREE_TYPE (var), mask));
    }

  return wide_val;
}


/* A subroutine of fold_stmt_r.  Attempts to fold *(A+O) to A[X].
   BASE is an array type.  OFFSET is a byte displacement.  ORIG_TYPE
   is the desired result type.  */

static tree
maybe_fold_offset_to_array_ref (tree base, tree offset, tree orig_type)
{
  tree min_idx, idx, elt_offset = integer_zero_node;
  tree array_type, elt_type, elt_size;

  /* If BASE is an ARRAY_REF, we can pick up another offset (this time
     measured in units of the size of elements type) from that ARRAY_REF).
     We can't do anything if either is variable.

     The case we handle here is *(&A[N]+O).  */
  if (TREE_CODE (base) == ARRAY_REF)
    {
      tree low_bound = array_ref_low_bound (base);

      elt_offset = TREE_OPERAND (base, 1);
      if (TREE_CODE (low_bound) != INTEGER_CST
	  || TREE_CODE (elt_offset) != INTEGER_CST)
	return NULL_TREE;

      elt_offset = int_const_binop (MINUS_EXPR, elt_offset, low_bound, 0);
      base = TREE_OPERAND (base, 0);
    }

  /* Ignore stupid user tricks of indexing non-array variables.  */
  array_type = TREE_TYPE (base);
  if (TREE_CODE (array_type) != ARRAY_TYPE)
    return NULL_TREE;
  elt_type = TREE_TYPE (array_type);
  if (!lang_hooks.types_compatible_p (orig_type, elt_type))
    return NULL_TREE;
	
  /* If OFFSET and ELT_OFFSET are zero, we don't care about the size of the
     element type (so we can use the alignment if it's not constant).
     Otherwise, compute the offset as an index by using a division.  If the
     division isn't exact, then don't do anything.  */
  elt_size = TYPE_SIZE_UNIT (elt_type);
  if (integer_zerop (offset))
    {
      if (TREE_CODE (elt_size) != INTEGER_CST)
	elt_size = size_int (TYPE_ALIGN (elt_type));

      idx = integer_zero_node;
    }
  else
    {
      unsigned HOST_WIDE_INT lquo, lrem;
      HOST_WIDE_INT hquo, hrem;

      if (TREE_CODE (elt_size) != INTEGER_CST
	  || div_and_round_double (TRUNC_DIV_EXPR, 1,
				   TREE_INT_CST_LOW (offset),
				   TREE_INT_CST_HIGH (offset),
				   TREE_INT_CST_LOW (elt_size),
				   TREE_INT_CST_HIGH (elt_size),
				   &lquo, &hquo, &lrem, &hrem)
	  || lrem || hrem)
	return NULL_TREE;

      idx = build_int_cst_wide (NULL_TREE, lquo, hquo);
    }

  /* Assume the low bound is zero.  If there is a domain type, get the
     low bound, if any, convert the index into that type, and add the
     low bound.  */
  min_idx = integer_zero_node;
  if (TYPE_DOMAIN (array_type))
    {
      if (TYPE_MIN_VALUE (TYPE_DOMAIN (array_type)))
	min_idx = TYPE_MIN_VALUE (TYPE_DOMAIN (array_type));
      else
	min_idx = fold_convert (TYPE_DOMAIN (array_type), min_idx);

      if (TREE_CODE (min_idx) != INTEGER_CST)
	return NULL_TREE;

      idx = fold_convert (TYPE_DOMAIN (array_type), idx);
      elt_offset = fold_convert (TYPE_DOMAIN (array_type), elt_offset);
    }

  if (!integer_zerop (min_idx))
    idx = int_const_binop (PLUS_EXPR, idx, min_idx, 0);
  if (!integer_zerop (elt_offset))
    idx = int_const_binop (PLUS_EXPR, idx, elt_offset, 0);

  return build4 (ARRAY_REF, orig_type, base, idx, NULL_TREE, NULL_TREE);
}


/* A subroutine of fold_stmt_r.  Attempts to fold *(S+O) to S.X.
   BASE is a record type.  OFFSET is a byte displacement.  ORIG_TYPE
   is the desired result type.  */
/* ??? This doesn't handle class inheritance.  */

static tree
maybe_fold_offset_to_component_ref (tree record_type, tree base, tree offset,
				    tree orig_type, bool base_is_ptr)
{
  tree f, t, field_type, tail_array_field, field_offset;

  if (TREE_CODE (record_type) != RECORD_TYPE
      && TREE_CODE (record_type) != UNION_TYPE
      && TREE_CODE (record_type) != QUAL_UNION_TYPE)
    return NULL_TREE;

  /* Short-circuit silly cases.  */
  if (lang_hooks.types_compatible_p (record_type, orig_type))
    return NULL_TREE;

  tail_array_field = NULL_TREE;
  for (f = TYPE_FIELDS (record_type); f ; f = TREE_CHAIN (f))
    {
      int cmp;

      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      if (DECL_BIT_FIELD (f))
	continue;

      field_offset = byte_position (f);
      if (TREE_CODE (field_offset) != INTEGER_CST)
	continue;

      /* ??? Java creates "interesting" fields for representing base classes.
	 They have no name, and have no context.  With no context, we get into
	 trouble with nonoverlapping_component_refs_p.  Skip them.  */
      if (!DECL_FIELD_CONTEXT (f))
	continue;

      /* The previous array field isn't at the end.  */
      tail_array_field = NULL_TREE;

      /* Check to see if this offset overlaps with the field.  */
      cmp = tree_int_cst_compare (field_offset, offset);
      if (cmp > 0)
	continue;

      field_type = TREE_TYPE (f);

      /* Here we exactly match the offset being checked.  If the types match,
	 then we can return that field.  */
      if (cmp == 0
	  && lang_hooks.types_compatible_p (orig_type, field_type))
	{
	  if (base_is_ptr)
	    base = build1 (INDIRECT_REF, record_type, base);
	  t = build3 (COMPONENT_REF, field_type, base, f, NULL_TREE);
	  return t;
	}
      
      /* Don't care about offsets into the middle of scalars.  */
      if (!AGGREGATE_TYPE_P (field_type))
	continue;

      /* Check for array at the end of the struct.  This is often
	 used as for flexible array members.  We should be able to
	 turn this into an array access anyway.  */
      if (TREE_CODE (field_type) == ARRAY_TYPE)
	tail_array_field = f;

      /* Check the end of the field against the offset.  */
      if (!DECL_SIZE_UNIT (f)
	  || TREE_CODE (DECL_SIZE_UNIT (f)) != INTEGER_CST)
	continue;
      t = int_const_binop (MINUS_EXPR, offset, field_offset, 1);
      if (!tree_int_cst_lt (t, DECL_SIZE_UNIT (f)))
	continue;

      /* If we matched, then set offset to the displacement into
	 this field.  */
      offset = t;
      goto found;
    }

  if (!tail_array_field)
    return NULL_TREE;

  f = tail_array_field;
  field_type = TREE_TYPE (f);
  offset = int_const_binop (MINUS_EXPR, offset, byte_position (f), 1);

 found:
  /* If we get here, we've got an aggregate field, and a possibly 
     nonzero offset into them.  Recurse and hope for a valid match.  */
  if (base_is_ptr)
    base = build1 (INDIRECT_REF, record_type, base);
  base = build3 (COMPONENT_REF, field_type, base, f, NULL_TREE);

  t = maybe_fold_offset_to_array_ref (base, offset, orig_type);
  if (t)
    return t;
  return maybe_fold_offset_to_component_ref (field_type, base, offset,
					     orig_type, false);
}


/* A subroutine of fold_stmt_r.  Attempt to simplify *(BASE+OFFSET).
   Return the simplified expression, or NULL if nothing could be done.  */

static tree
maybe_fold_stmt_indirect (tree expr, tree base, tree offset)
{
  tree t;

  /* We may well have constructed a double-nested PLUS_EXPR via multiple
     substitutions.  Fold that down to one.  Remove NON_LVALUE_EXPRs that
     are sometimes added.  */
  base = fold (base);
  STRIP_TYPE_NOPS (base);
  TREE_OPERAND (expr, 0) = base;

  /* One possibility is that the address reduces to a string constant.  */
  t = fold_read_from_constant_string (expr);
  if (t)
    return t;

  /* Add in any offset from a PLUS_EXPR.  */
  if (TREE_CODE (base) == PLUS_EXPR)
    {
      tree offset2;

      offset2 = TREE_OPERAND (base, 1);
      if (TREE_CODE (offset2) != INTEGER_CST)
	return NULL_TREE;
      base = TREE_OPERAND (base, 0);

      offset = int_const_binop (PLUS_EXPR, offset, offset2, 1);
    }

  if (TREE_CODE (base) == ADDR_EXPR)
    {
      /* Strip the ADDR_EXPR.  */
      base = TREE_OPERAND (base, 0);

      /* Fold away CONST_DECL to its value, if the type is scalar.  */
      if (TREE_CODE (base) == CONST_DECL
	  && ccp_decl_initial_min_invariant (DECL_INITIAL (base)))
	return DECL_INITIAL (base);

      /* Try folding *(&B+O) to B[X].  */
      t = maybe_fold_offset_to_array_ref (base, offset, TREE_TYPE (expr));
      if (t)
	return t;

      /* Try folding *(&B+O) to B.X.  */
      t = maybe_fold_offset_to_component_ref (TREE_TYPE (base), base, offset,
					      TREE_TYPE (expr), false);
      if (t)
	return t;

      /* Fold *&B to B.  We can only do this if EXPR is the same type
	 as BASE.  We can't do this if EXPR is the element type of an array
	 and BASE is the array.  */
      if (integer_zerop (offset)
	  && lang_hooks.types_compatible_p (TREE_TYPE (base),
					    TREE_TYPE (expr)))
	return base;
    }
  else
    {
      /* We can get here for out-of-range string constant accesses, 
	 such as "_"[3].  Bail out of the entire substitution search
	 and arrange for the entire statement to be replaced by a
	 call to __builtin_trap.  In all likelihood this will all be
	 constant-folded away, but in the meantime we can't leave with
	 something that get_expr_operands can't understand.  */

      t = base;
      STRIP_NOPS (t);
      if (TREE_CODE (t) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (t, 0)) == STRING_CST)
	{
	  /* FIXME: Except that this causes problems elsewhere with dead
	     code not being deleted, and we die in the rtl expanders 
	     because we failed to remove some ssa_name.  In the meantime,
	     just return zero.  */
	  /* FIXME2: This condition should be signaled by
	     fold_read_from_constant_string directly, rather than 
	     re-checking for it here.  */
	  return integer_zero_node;
	}

      /* Try folding *(B+O) to B->X.  Still an improvement.  */
      if (POINTER_TYPE_P (TREE_TYPE (base)))
	{
          t = maybe_fold_offset_to_component_ref (TREE_TYPE (TREE_TYPE (base)),
						  base, offset,
						  TREE_TYPE (expr), true);
	  if (t)
	    return t;
	}
    }

  /* Otherwise we had an offset that we could not simplify.  */
  return NULL_TREE;
}


/* A subroutine of fold_stmt_r.  EXPR is a PLUS_EXPR.

   A quaint feature extant in our address arithmetic is that there
   can be hidden type changes here.  The type of the result need
   not be the same as the type of the input pointer.

   What we're after here is an expression of the form
	(T *)(&array + const)
   where the cast doesn't actually exist, but is implicit in the
   type of the PLUS_EXPR.  We'd like to turn this into
	&array[x]
   which may be able to propagate further.  */

static tree
maybe_fold_stmt_addition (tree expr)
{
  tree op0 = TREE_OPERAND (expr, 0);
  tree op1 = TREE_OPERAND (expr, 1);
  tree ptr_type = TREE_TYPE (expr);
  tree ptd_type;
  tree t;
  bool subtract = (TREE_CODE (expr) == MINUS_EXPR);

  /* We're only interested in pointer arithmetic.  */
  if (!POINTER_TYPE_P (ptr_type))
    return NULL_TREE;
  /* Canonicalize the integral operand to op1.  */
  if (INTEGRAL_TYPE_P (TREE_TYPE (op0)))
    {
      if (subtract)
	return NULL_TREE;
      t = op0, op0 = op1, op1 = t;
    }
  /* It had better be a constant.  */
  if (TREE_CODE (op1) != INTEGER_CST)
    return NULL_TREE;
  /* The first operand should be an ADDR_EXPR.  */
  if (TREE_CODE (op0) != ADDR_EXPR)
    return NULL_TREE;
  op0 = TREE_OPERAND (op0, 0);

  /* If the first operand is an ARRAY_REF, expand it so that we can fold
     the offset into it.  */
  while (TREE_CODE (op0) == ARRAY_REF)
    {
      tree array_obj = TREE_OPERAND (op0, 0);
      tree array_idx = TREE_OPERAND (op0, 1);
      tree elt_type = TREE_TYPE (op0);
      tree elt_size = TYPE_SIZE_UNIT (elt_type);
      tree min_idx;

      if (TREE_CODE (array_idx) != INTEGER_CST)
	break;
      if (TREE_CODE (elt_size) != INTEGER_CST)
	break;

      /* Un-bias the index by the min index of the array type.  */
      min_idx = TYPE_DOMAIN (TREE_TYPE (array_obj));
      if (min_idx)
	{
	  min_idx = TYPE_MIN_VALUE (min_idx);
	  if (min_idx)
	    {
	      if (TREE_CODE (min_idx) != INTEGER_CST)
		break;

	      array_idx = fold_convert (TREE_TYPE (min_idx), array_idx);
	      if (!integer_zerop (min_idx))
		array_idx = int_const_binop (MINUS_EXPR, array_idx,
					     min_idx, 0);
	    }
	}

      /* Convert the index to a byte offset.  */
      array_idx = fold_convert (sizetype, array_idx);
      array_idx = int_const_binop (MULT_EXPR, array_idx, elt_size, 0);

      /* Update the operands for the next round, or for folding.  */
      /* If we're manipulating unsigned types, then folding into negative
	 values can produce incorrect results.  Particularly if the type
	 is smaller than the width of the pointer.  */
      if (subtract
	  && TYPE_UNSIGNED (TREE_TYPE (op1))
	  && tree_int_cst_lt (array_idx, op1))
	return NULL;
      op1 = int_const_binop (subtract ? MINUS_EXPR : PLUS_EXPR,
			     array_idx, op1, 0);
      subtract = false;
      op0 = array_obj;
    }

  /* If we weren't able to fold the subtraction into another array reference,
     canonicalize the integer for passing to the array and component ref
     simplification functions.  */
  if (subtract)
    {
      if (TYPE_UNSIGNED (TREE_TYPE (op1)))
	return NULL;
      op1 = fold_unary (NEGATE_EXPR, TREE_TYPE (op1), op1);
      /* ??? In theory fold should always produce another integer.  */
      if (op1 == NULL || TREE_CODE (op1) != INTEGER_CST)
	return NULL;
    }

  ptd_type = TREE_TYPE (ptr_type);

  /* At which point we can try some of the same things as for indirects.  */
  t = maybe_fold_offset_to_array_ref (op0, op1, ptd_type);
  if (!t)
    t = maybe_fold_offset_to_component_ref (TREE_TYPE (op0), op0, op1,
					    ptd_type, false);
  if (t)
    t = build1 (ADDR_EXPR, ptr_type, t);

  return t;
}

/* For passing state through walk_tree into fold_stmt_r and its
   children.  */

struct fold_stmt_r_data
{
  tree stmt;
  bool *changed_p;
  bool *inside_addr_expr_p;
};

/* Subroutine of fold_stmt called via walk_tree.  We perform several
   simplifications of EXPR_P, mostly having to do with pointer arithmetic.  */

static tree
fold_stmt_r (tree *expr_p, int *walk_subtrees, void *data)
{
  struct fold_stmt_r_data *fold_stmt_r_data = (struct fold_stmt_r_data *) data;
  bool *inside_addr_expr_p = fold_stmt_r_data->inside_addr_expr_p;
  bool *changed_p = fold_stmt_r_data->changed_p;
  tree expr = *expr_p, t;

  /* ??? It'd be nice if walk_tree had a pre-order option.  */
  switch (TREE_CODE (expr))
    {
    case INDIRECT_REF:
      t = walk_tree (&TREE_OPERAND (expr, 0), fold_stmt_r, data, NULL);
      if (t)
	return t;
      *walk_subtrees = 0;

      t = maybe_fold_stmt_indirect (expr, TREE_OPERAND (expr, 0),
				    integer_zero_node);
      break;

      /* ??? Could handle more ARRAY_REFs here, as a variant of INDIRECT_REF.
	 We'd only want to bother decomposing an existing ARRAY_REF if
	 the base array is found to have another offset contained within.
	 Otherwise we'd be wasting time.  */
    case ARRAY_REF:
      /* If we are not processing expressions found within an
	 ADDR_EXPR, then we can fold constant array references.  */
      if (!*inside_addr_expr_p)
	t = fold_read_from_constant_string (expr);
      else
	t = NULL;
      break;

    case ADDR_EXPR:
      *inside_addr_expr_p = true;
      t = walk_tree (&TREE_OPERAND (expr, 0), fold_stmt_r, data, NULL);
      *inside_addr_expr_p = false;
      if (t)
	return t;
      *walk_subtrees = 0;

      /* Set TREE_INVARIANT properly so that the value is properly
	 considered constant, and so gets propagated as expected.  */
      if (*changed_p)
        recompute_tree_invariant_for_addr_expr (expr);
      return NULL_TREE;

    case PLUS_EXPR:
    case MINUS_EXPR:
      t = walk_tree (&TREE_OPERAND (expr, 0), fold_stmt_r, data, NULL);
      if (t)
	return t;
      t = walk_tree (&TREE_OPERAND (expr, 1), fold_stmt_r, data, NULL);
      if (t)
	return t;
      *walk_subtrees = 0;

      t = maybe_fold_stmt_addition (expr);
      break;

    case COMPONENT_REF:
      t = walk_tree (&TREE_OPERAND (expr, 0), fold_stmt_r, data, NULL);
      if (t)
        return t;
      *walk_subtrees = 0;

      /* Make sure the FIELD_DECL is actually a field in the type on the lhs.
	 We've already checked that the records are compatible, so we should
	 come up with a set of compatible fields.  */
      {
	tree expr_record = TREE_TYPE (TREE_OPERAND (expr, 0));
	tree expr_field = TREE_OPERAND (expr, 1);

        if (DECL_FIELD_CONTEXT (expr_field) != TYPE_MAIN_VARIANT (expr_record))
	  {
	    expr_field = find_compatible_field (expr_record, expr_field);
	    TREE_OPERAND (expr, 1) = expr_field;
	  }
      }
      break;

    case TARGET_MEM_REF:
      t = maybe_fold_tmr (expr);
      break;

    case COND_EXPR:
      if (COMPARISON_CLASS_P (TREE_OPERAND (expr, 0)))
        {
	  tree op0 = TREE_OPERAND (expr, 0);
	  tree tem;
	  bool set;

	  fold_defer_overflow_warnings ();
	  tem = fold_binary (TREE_CODE (op0), TREE_TYPE (op0),
			     TREE_OPERAND (op0, 0),
			     TREE_OPERAND (op0, 1));
	  set = tem && is_gimple_condexpr (tem);
	  fold_undefer_overflow_warnings (set, fold_stmt_r_data->stmt, 0);
	  if (set)
	    TREE_OPERAND (expr, 0) = tem;
	  t = expr;
          break;
        }

    default:
      return NULL_TREE;
    }

  if (t)
    {
      *expr_p = t;
      *changed_p = true;
    }

  return NULL_TREE;
}


/* Return the string length, maximum string length or maximum value of
   ARG in LENGTH.
   If ARG is an SSA name variable, follow its use-def chains.  If LENGTH
   is not NULL and, for TYPE == 0, its value is not equal to the length
   we determine or if we are unable to determine the length or value,
   return false.  VISITED is a bitmap of visited variables.
   TYPE is 0 if string length should be returned, 1 for maximum string
   length and 2 for maximum value ARG can have.  */

static bool
get_maxval_strlen (tree arg, tree *length, bitmap visited, int type)
{
  tree var, def_stmt, val;
  
  if (TREE_CODE (arg) != SSA_NAME)
    {
      if (type == 2)
	{
	  val = arg;
	  if (TREE_CODE (val) != INTEGER_CST
	      || tree_int_cst_sgn (val) < 0)
	    return false;
	}
      else
	val = c_strlen (arg, 1);
      if (!val)
	return false;

      if (*length)
	{
	  if (type > 0)
	    {
	      if (TREE_CODE (*length) != INTEGER_CST
		  || TREE_CODE (val) != INTEGER_CST)
		return false;

	      if (tree_int_cst_lt (*length, val))
		*length = val;
	      return true;
	    }
	  else if (simple_cst_equal (val, *length) != 1)
	    return false;
	}

      *length = val;
      return true;
    }

  /* If we were already here, break the infinite cycle.  */
  if (bitmap_bit_p (visited, SSA_NAME_VERSION (arg)))
    return true;
  bitmap_set_bit (visited, SSA_NAME_VERSION (arg));

  var = arg;
  def_stmt = SSA_NAME_DEF_STMT (var);

  switch (TREE_CODE (def_stmt))
    {
      case MODIFY_EXPR:
	{
	  tree rhs;

	  /* The RHS of the statement defining VAR must either have a
	     constant length or come from another SSA_NAME with a constant
	     length.  */
	  rhs = TREE_OPERAND (def_stmt, 1);
	  STRIP_NOPS (rhs);
	  return get_maxval_strlen (rhs, length, visited, type);
	}

      case PHI_NODE:
	{
	  /* All the arguments of the PHI node must have the same constant
	     length.  */
	  int i;

	  for (i = 0; i < PHI_NUM_ARGS (def_stmt); i++)
	    {
	      tree arg = PHI_ARG_DEF (def_stmt, i);

	      /* If this PHI has itself as an argument, we cannot
		 determine the string length of this argument.  However,
		 if we can find a constant string length for the other
		 PHI args then we can still be sure that this is a
		 constant string length.  So be optimistic and just
		 continue with the next argument.  */
	      if (arg == PHI_RESULT (def_stmt))
		continue;

	      if (!get_maxval_strlen (arg, length, visited, type))
		return false;
	    }

	  return true;
	}

      default:
	break;
    }


  return false;
}


/* Fold builtin call FN in statement STMT.  If it cannot be folded into a
   constant, return NULL_TREE.  Otherwise, return its constant value.  */

static tree
ccp_fold_builtin (tree stmt, tree fn)
{
  tree result, val[3];
  tree callee, arglist, a;
  int arg_mask, i, type;
  bitmap visited;
  bool ignore;

  ignore = TREE_CODE (stmt) != MODIFY_EXPR;

  /* First try the generic builtin folder.  If that succeeds, return the
     result directly.  */
  callee = get_callee_fndecl (fn);
  arglist = TREE_OPERAND (fn, 1);
  result = fold_builtin (callee, arglist, ignore);
  if (result)
    {
      if (ignore)
	STRIP_NOPS (result);
      return result;
    }

  /* Ignore MD builtins.  */
  if (DECL_BUILT_IN_CLASS (callee) == BUILT_IN_MD)
    return NULL_TREE;

  /* If the builtin could not be folded, and it has no argument list,
     we're done.  */
  if (!arglist)
    return NULL_TREE;

  /* Limit the work only for builtins we know how to simplify.  */
  switch (DECL_FUNCTION_CODE (callee))
    {
    case BUILT_IN_STRLEN:
    case BUILT_IN_FPUTS:
    case BUILT_IN_FPUTS_UNLOCKED:
      arg_mask = 1;
      type = 0;
      break;
    case BUILT_IN_STRCPY:
    case BUILT_IN_STRNCPY:
      arg_mask = 2;
      type = 0;
      break;
    case BUILT_IN_MEMCPY_CHK:
    case BUILT_IN_MEMPCPY_CHK:
    case BUILT_IN_MEMMOVE_CHK:
    case BUILT_IN_MEMSET_CHK:
    case BUILT_IN_STRNCPY_CHK:
      arg_mask = 4;
      type = 2;
      break;
    case BUILT_IN_STRCPY_CHK:
    case BUILT_IN_STPCPY_CHK:
      arg_mask = 2;
      type = 1;
      break;
    case BUILT_IN_SNPRINTF_CHK:
    case BUILT_IN_VSNPRINTF_CHK:
      arg_mask = 2;
      type = 2;
      break;
    default:
      return NULL_TREE;
    }

  /* Try to use the dataflow information gathered by the CCP process.  */
  visited = BITMAP_ALLOC (NULL);

  memset (val, 0, sizeof (val));
  for (i = 0, a = arglist;
       arg_mask;
       i++, arg_mask >>= 1, a = TREE_CHAIN (a))
    if (arg_mask & 1)
      {
	bitmap_clear (visited);
	if (!get_maxval_strlen (TREE_VALUE (a), &val[i], visited, type))
	  val[i] = NULL_TREE;
      }

  BITMAP_FREE (visited);

  result = NULL_TREE;
  switch (DECL_FUNCTION_CODE (callee))
    {
    case BUILT_IN_STRLEN:
      if (val[0])
	{
	  tree new = fold_convert (TREE_TYPE (fn), val[0]);

	  /* If the result is not a valid gimple value, or not a cast
	     of a valid gimple value, then we can not use the result.  */
	  if (is_gimple_val (new)
	      || (is_gimple_cast (new)
		  && is_gimple_val (TREE_OPERAND (new, 0))))
	    return new;
	}
      break;

    case BUILT_IN_STRCPY:
      if (val[1] && is_gimple_val (val[1]))
	result = fold_builtin_strcpy (callee, arglist, val[1]);
      break;

    case BUILT_IN_STRNCPY:
      if (val[1] && is_gimple_val (val[1]))
	result = fold_builtin_strncpy (callee, arglist, val[1]);
      break;

    case BUILT_IN_FPUTS:
      result = fold_builtin_fputs (arglist,
				   TREE_CODE (stmt) != MODIFY_EXPR, 0,
				   val[0]);
      break;

    case BUILT_IN_FPUTS_UNLOCKED:
      result = fold_builtin_fputs (arglist,
				   TREE_CODE (stmt) != MODIFY_EXPR, 1,
				   val[0]);
      break;

    case BUILT_IN_MEMCPY_CHK:
    case BUILT_IN_MEMPCPY_CHK:
    case BUILT_IN_MEMMOVE_CHK:
    case BUILT_IN_MEMSET_CHK:
      if (val[2] && is_gimple_val (val[2]))
	result = fold_builtin_memory_chk (callee, arglist, val[2], ignore,
					  DECL_FUNCTION_CODE (callee));
      break;

    case BUILT_IN_STRCPY_CHK:
    case BUILT_IN_STPCPY_CHK:
      if (val[1] && is_gimple_val (val[1]))
	result = fold_builtin_stxcpy_chk (callee, arglist, val[1], ignore,
					  DECL_FUNCTION_CODE (callee));
      break;

    case BUILT_IN_STRNCPY_CHK:
      if (val[2] && is_gimple_val (val[2]))
	result = fold_builtin_strncpy_chk (arglist, val[2]);
      break;

    case BUILT_IN_SNPRINTF_CHK:
    case BUILT_IN_VSNPRINTF_CHK:
      if (val[1] && is_gimple_val (val[1]))
	result = fold_builtin_snprintf_chk (arglist, val[1],
					    DECL_FUNCTION_CODE (callee));
      break;

    default:
      gcc_unreachable ();
    }

  if (result && ignore)
    result = fold_ignored_result (result);
  return result;
}


/* Fold the statement pointed to by STMT_P.  In some cases, this function may
   replace the whole statement with a new one.  Returns true iff folding
   makes any changes.  */

bool
fold_stmt (tree *stmt_p)
{
  tree rhs, result, stmt;
  struct fold_stmt_r_data fold_stmt_r_data;
  bool changed = false;
  bool inside_addr_expr = false;

  stmt = *stmt_p;

  fold_stmt_r_data.stmt = stmt;
  fold_stmt_r_data.changed_p = &changed;
  fold_stmt_r_data.inside_addr_expr_p = &inside_addr_expr;

  /* If we replaced constants and the statement makes pointer dereferences,
     then we may need to fold instances of *&VAR into VAR, etc.  */
  if (walk_tree (stmt_p, fold_stmt_r, &fold_stmt_r_data, NULL))
    {
      *stmt_p
	= build_function_call_expr (implicit_built_in_decls[BUILT_IN_TRAP],
				    NULL);
      return true;
    }

  rhs = get_rhs (stmt);
  if (!rhs)
    return changed;
  result = NULL_TREE;

  if (TREE_CODE (rhs) == CALL_EXPR)
    {
      tree callee;

      /* Check for builtins that CCP can handle using information not
	 available in the generic fold routines.  */
      callee = get_callee_fndecl (rhs);
      if (callee && DECL_BUILT_IN (callee))
	result = ccp_fold_builtin (stmt, rhs);
      else
	{
	  /* Check for resolvable OBJ_TYPE_REF.  The only sorts we can resolve
	     here are when we've propagated the address of a decl into the
	     object slot.  */
	  /* ??? Should perhaps do this in fold proper.  However, doing it
	     there requires that we create a new CALL_EXPR, and that requires
	     copying EH region info to the new node.  Easier to just do it
	     here where we can just smash the call operand. Also
	     CALL_EXPR_RETURN_SLOT_OPT needs to be handled correctly and
	     copied, fold_ternary does not have not information. */
	  callee = TREE_OPERAND (rhs, 0);
	  if (TREE_CODE (callee) == OBJ_TYPE_REF
	      && lang_hooks.fold_obj_type_ref
	      && TREE_CODE (OBJ_TYPE_REF_OBJECT (callee)) == ADDR_EXPR
	      && DECL_P (TREE_OPERAND
			 (OBJ_TYPE_REF_OBJECT (callee), 0)))
	    {
	      tree t;

	      /* ??? Caution: Broken ADDR_EXPR semantics means that
		 looking at the type of the operand of the addr_expr
		 can yield an array type.  See silly exception in
		 check_pointer_types_r.  */

	      t = TREE_TYPE (TREE_TYPE (OBJ_TYPE_REF_OBJECT (callee)));
	      t = lang_hooks.fold_obj_type_ref (callee, t);
	      if (t)
		{
		  TREE_OPERAND (rhs, 0) = t;
		  changed = true;
		}
	    }
	}
    }

  /* If we couldn't fold the RHS, hand over to the generic fold routines.  */
  if (result == NULL_TREE)
    result = fold (rhs);

  /* Strip away useless type conversions.  Both the NON_LVALUE_EXPR that
     may have been added by fold, and "useless" type conversions that might
     now be apparent due to propagation.  */
  STRIP_USELESS_TYPE_CONVERSION (result);

  if (result != rhs)
    changed |= set_rhs (stmt_p, result);

  return changed;
}

/* Perform the minimal folding on statement STMT.  Only operations like
   *&x created by constant propagation are handled.  The statement cannot
   be replaced with a new one.  */

bool
fold_stmt_inplace (tree stmt)
{
  tree old_stmt = stmt, rhs, new_rhs;
  struct fold_stmt_r_data fold_stmt_r_data;
  bool changed = false;
  bool inside_addr_expr = false;

  fold_stmt_r_data.stmt = stmt;
  fold_stmt_r_data.changed_p = &changed;
  fold_stmt_r_data.inside_addr_expr_p = &inside_addr_expr;

  walk_tree (&stmt, fold_stmt_r, &fold_stmt_r_data, NULL);
  gcc_assert (stmt == old_stmt);

  rhs = get_rhs (stmt);
  if (!rhs || rhs == stmt)
    return changed;

  new_rhs = fold (rhs);
  STRIP_USELESS_TYPE_CONVERSION (new_rhs);
  if (new_rhs == rhs)
    return changed;

  changed |= set_rhs (&stmt, new_rhs);
  gcc_assert (stmt == old_stmt);

  return changed;
}

/* Convert EXPR into a GIMPLE value suitable for substitution on the
   RHS of an assignment.  Insert the necessary statements before
   iterator *SI_P.  */

static tree
convert_to_gimple_builtin (block_stmt_iterator *si_p, tree expr)
{
  tree_stmt_iterator ti;
  tree stmt = bsi_stmt (*si_p);
  tree tmp, stmts = NULL;

  push_gimplify_context ();
  tmp = get_initialized_tmp_var (expr, &stmts, NULL);
  pop_gimplify_context (NULL);

  if (EXPR_HAS_LOCATION (stmt))
    annotate_all_with_locus (&stmts, EXPR_LOCATION (stmt));

  /* The replacement can expose previously unreferenced variables.  */
  for (ti = tsi_start (stmts); !tsi_end_p (ti); tsi_next (&ti))
    {
      tree new_stmt = tsi_stmt (ti);
      find_new_referenced_vars (tsi_stmt_ptr (ti));
      bsi_insert_before (si_p, new_stmt, BSI_NEW_STMT);
      mark_new_vars_to_rename (bsi_stmt (*si_p));
      bsi_next (si_p);
    }

  return tmp;
}


/* A simple pass that attempts to fold all builtin functions.  This pass
   is run after we've propagated as many constants as we can.  */

static unsigned int
execute_fold_all_builtins (void)
{
  bool cfg_changed = false;
  basic_block bb;
  FOR_EACH_BB (bb)
    {
      block_stmt_iterator i;
      for (i = bsi_start (bb); !bsi_end_p (i); )
	{
	  tree *stmtp = bsi_stmt_ptr (i);
	  tree old_stmt = *stmtp;
	  tree call = get_rhs (*stmtp);
	  tree callee, result;
	  enum built_in_function fcode;

	  if (!call || TREE_CODE (call) != CALL_EXPR)
	    {
	      bsi_next (&i);
	      continue;
	    }
	  callee = get_callee_fndecl (call);
	  if (!callee || DECL_BUILT_IN_CLASS (callee) != BUILT_IN_NORMAL)
	    {
	      bsi_next (&i);
	      continue;
	    }
	  fcode = DECL_FUNCTION_CODE (callee);

	  result = ccp_fold_builtin (*stmtp, call);
	  if (!result)
	    switch (DECL_FUNCTION_CODE (callee))
	      {
	      case BUILT_IN_CONSTANT_P:
		/* Resolve __builtin_constant_p.  If it hasn't been
		   folded to integer_one_node by now, it's fairly
		   certain that the value simply isn't constant.  */
		result = integer_zero_node;
		break;

	      default:
		bsi_next (&i);
		continue;
	      }

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "Simplified\n  ");
	      print_generic_stmt (dump_file, *stmtp, dump_flags);
	    }

	  if (!set_rhs (stmtp, result))
	    {
	      result = convert_to_gimple_builtin (&i, result);
	      if (result)
		{
		  bool ok = set_rhs (stmtp, result);
		  
		  gcc_assert (ok);
		}
	    }
	  mark_new_vars_to_rename (*stmtp);
	  if (maybe_clean_or_replace_eh_stmt (old_stmt, *stmtp)
	      && tree_purge_dead_eh_edges (bb))
	    cfg_changed = true;

	  if (dump_file && (dump_flags & TDF_DETAILS))
	    {
	      fprintf (dump_file, "to\n  ");
	      print_generic_stmt (dump_file, *stmtp, dump_flags);
	      fprintf (dump_file, "\n");
	    }

	  /* Retry the same statement if it changed into another
	     builtin, there might be new opportunities now.  */
	  call = get_rhs (*stmtp);
	  if (!call || TREE_CODE (call) != CALL_EXPR)
	    {
	      bsi_next (&i);
	      continue;
	    }
	  callee = get_callee_fndecl (call);
	  if (!callee
	      || DECL_BUILT_IN_CLASS (callee) != BUILT_IN_NORMAL
	      || DECL_FUNCTION_CODE (callee) == fcode)
	    bsi_next (&i);
	}
    }

  /* Delete unreachable blocks.  */
  if (cfg_changed)
    cleanup_tree_cfg ();
  return 0;
}


struct tree_opt_pass pass_fold_builtins = 
{
  "fab",				/* name */
  NULL,					/* gate */
  execute_fold_all_builtins,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_cfg | PROP_ssa | PROP_alias,	/* properties_required */
  0,					/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func
    | TODO_verify_ssa
    | TODO_update_ssa,			/* todo_flags_finish */
  0					/* letter */
};
