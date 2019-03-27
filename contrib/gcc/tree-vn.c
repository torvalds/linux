/* Value Numbering routines for tree expressions.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Daniel Berlin <dan@dberlin.org>, Steven Bosscher
   <stevenb@suse.de> and Diego Novillo <dnovillo@redhat.com>

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
#include "tm.h"
#include "ggc.h"
#include "tree.h"
#include "tree-flow.h"
#include "hashtab.h"
#include "langhooks.h"
#include "tree-pass.h"
#include "tree-dump.h"
#include "diagnostic.h"

/* The value table that maps expressions to values.  */
static htab_t value_table;

/* Map expressions to values.  These are simple pairs of expressions
   and the values they represent.  To find the value represented by
   an expression, we use a hash table where the elements are {e,v}
   pairs, and the expression is the key.  */
typedef struct val_expr_pair_d
{
  /* Value handle.  */
  tree v;

  /* Associated expression.  */
  tree e;

  /* For comparing virtual uses in E.  */
  VEC (tree, gc) *vuses;

  /* E's hash value.  */
  hashval_t hashcode;
} *val_expr_pair_t;

static void set_value_handle (tree e, tree v);


/* Create and return a new value handle node of type TYPE.  */

static tree
make_value_handle (tree type)
{
  static unsigned int id = 0;
  tree vh;

  vh = build0 (VALUE_HANDLE, type);
  VALUE_HANDLE_ID (vh) = id++;
  return vh;
}


/* Given an expression EXPR, compute a hash value number using the
   code of the expression, its real operands and virtual operands (if
   any).
   
   VAL can be used to iterate by passing previous value numbers (it is
   used by iterative_hash_expr).  */

hashval_t
vn_compute (tree expr, hashval_t val)
{
  /* EXPR must not be a statement.  We are only interested in value
     numbering expressions on the RHS of assignments.  */
  gcc_assert (expr);
  gcc_assert (!expr->common.ann
	      || expr->common.ann->common.type != STMT_ANN);

  val = iterative_hash_expr (expr, val);
  return val;
}

/* Compare two expressions E1 and E2 and return true if they are
   equal.  */

bool
expressions_equal_p (tree e1, tree e2)
{
  tree te1, te2;
  
  if (e1 == e2)
    return true;

  te1 = TREE_TYPE (e1);
  te2 = TREE_TYPE (e2);

  if (TREE_CODE (e1) == TREE_LIST && TREE_CODE (e2) == TREE_LIST)
    {
      tree lop1 = e1;
      tree lop2 = e2;
      for (lop1 = e1, lop2 = e2;
	   lop1 || lop2;
	   lop1 = TREE_CHAIN (lop1), lop2 = TREE_CHAIN (lop2))
	{
	  if (!lop1 || !lop2)
	    return false;
	  if (!expressions_equal_p (TREE_VALUE (lop1), TREE_VALUE (lop2)))
	    return false;
	}
      return true;

    }
  else if (TREE_CODE (e1) == TREE_CODE (e2) 
	   && (te1 == te2 || lang_hooks.types_compatible_p (te1, te2))
	   && operand_equal_p (e1, e2, OEP_PURE_SAME))
    return true;

  return false;
}


/* Hash a {v,e} pair that is pointed to by P.
   The hashcode is cached in the val_expr_pair, so we just return
   that.  */

static hashval_t
val_expr_pair_hash (const void *p)
{
  const val_expr_pair_t ve = (val_expr_pair_t) p;
  return ve->hashcode;
}


/* Given two val_expr_pair_t's, return true if they represent the same
   expression, false otherwise.
   P1 and P2 should point to the val_expr_pair_t's to be compared.  */

static int
val_expr_pair_expr_eq (const void *p1, const void *p2)
{
  int i;
  tree vuse1;
  const val_expr_pair_t ve1 = (val_expr_pair_t) p1;
  const val_expr_pair_t ve2 = (val_expr_pair_t) p2;

  if (! expressions_equal_p (ve1->e, ve2->e))
    return false;

  if (ve1->vuses == ve2->vuses)
    return true;

  if (VEC_length (tree, ve1->vuses) != VEC_length (tree, ve2->vuses))
    return false;

  for (i = 0; VEC_iterate (tree, ve1->vuses, i, vuse1); i++)
    {
      if (VEC_index (tree, ve2->vuses, i) != vuse1)
	return false;
    }
  return true;
}


/* Set the value handle for expression E to value V.  */
   
static void
set_value_handle (tree e, tree v)
{
  if (TREE_CODE (e) == SSA_NAME)
    SSA_NAME_VALUE (e) = v;
  else if (EXPR_P (e) || DECL_P (e) || TREE_CODE (e) == TREE_LIST
	   || TREE_CODE (e) == CONSTRUCTOR)
    get_tree_common_ann (e)->value_handle = v;
  else
    /* Do nothing.  Constants are their own value handles.  */
    gcc_assert (is_gimple_min_invariant (e));
}

/* Copy the virtual uses from STMT into a newly allocated VEC(tree),
   and return the VEC(tree).  */

static VEC (tree, gc) *
copy_vuses_from_stmt (tree stmt)
{
  ssa_op_iter iter;
  tree vuse;
  VEC (tree, gc) *vuses = NULL;

  if (!stmt)
    return NULL;

  FOR_EACH_SSA_TREE_OPERAND (vuse, stmt, iter, SSA_OP_VUSE)
    VEC_safe_push (tree, gc, vuses, vuse);

  return vuses;
}

/* Place for shared_vuses_from_stmt to shove vuses.  */
static VEC (tree, gc) *shared_lookup_vuses;

/* Copy the virtual uses from STMT into SHARED_LOOKUP_VUSES.
   This function will overwrite the current SHARED_LOOKUP_VUSES
   variable.  */

static VEC (tree, gc) *
shared_vuses_from_stmt (tree stmt)
{
  ssa_op_iter iter;
  tree vuse;

  if (!stmt)
    return NULL;

  VEC_truncate (tree, shared_lookup_vuses, 0);

  FOR_EACH_SSA_TREE_OPERAND (vuse, stmt, iter, SSA_OP_VUSE)
    VEC_safe_push (tree, gc, shared_lookup_vuses, vuse);

  if (VEC_length (tree, shared_lookup_vuses) > 1)
    sort_vuses (shared_lookup_vuses);

  return shared_lookup_vuses;
}

/* Insert EXPR into VALUE_TABLE with value VAL, and add expression
   EXPR to the value set for value VAL.  */

void
vn_add (tree expr, tree val)
{
  vn_add_with_vuses (expr, val, NULL);
}

/* Insert EXPR into VALUE_TABLE with value VAL, and add expression
   EXPR to the value set for value VAL.  VUSES represents the virtual
   use operands associated with EXPR.  It is used when computing a
   hash value for EXPR.  */

void
vn_add_with_vuses (tree expr, tree val, VEC (tree, gc) *vuses)
{
  void **slot;
  val_expr_pair_t new_pair;
  
  new_pair = XNEW (struct val_expr_pair_d);
  new_pair->e = expr;
  new_pair->v = val;
  new_pair->vuses = vuses;
  new_pair->hashcode = vn_compute (expr, 0);
  slot = htab_find_slot_with_hash (value_table, new_pair, new_pair->hashcode,
				   INSERT);
  if (*slot)
    free (*slot);
  *slot = (void *) new_pair;

  set_value_handle (expr, val);
  if (TREE_CODE (val) == VALUE_HANDLE)
    add_to_value (val, expr);
}


/* Search in VALUE_TABLE for an existing instance of expression EXPR,
   and return its value, or NULL if none has been set.  STMT
   represents the stmt associated with EXPR.  It is used when computing the 
   hash value for EXPR.  */

tree
vn_lookup (tree expr, tree stmt)
{
  return vn_lookup_with_vuses (expr, shared_vuses_from_stmt (stmt));
}

/* Search in VALUE_TABLE for an existing instance of expression EXPR,
   and return its value, or NULL if none has been set.  VUSES is the
   list of virtual use operands associated with EXPR.  It is used when
   computing the hash value for EXPR.  */

tree
vn_lookup_with_vuses (tree expr, VEC (tree, gc) *vuses)
{
  void **slot;
  struct val_expr_pair_d vep = {NULL, NULL, NULL, 0};

  /* Constants are their own value.  */
  if (is_gimple_min_invariant (expr))
    return expr;

  vep.e = expr;
  vep.vuses = vuses;
  vep.hashcode = vn_compute (expr, 0);
  slot = htab_find_slot_with_hash (value_table, &vep, vep.hashcode, NO_INSERT);
  if (!slot)
    return NULL_TREE;
  else
    return ((val_expr_pair_t) *slot)->v;
}


/* A comparison function for use in qsort to compare vuses.  Simply
   subtracts version numbers.  */

static int
vuses_compare (const void *pa, const void *pb)
{
  const tree vusea = *((const tree *)pa);
  const tree vuseb = *((const tree *)pb);
  int sn = SSA_NAME_VERSION (vusea) - SSA_NAME_VERSION (vuseb);

  return sn;
}

/* Print out the "Created value <x> for <Y>" statement to the
   dump_file.
   This is factored because both versions of lookup use it, and it
   obscures the real work going on in those functions.  */

static void
print_creation_to_file (tree v, tree expr, VEC (tree, gc) *vuses)
{
  fprintf (dump_file, "Created value ");
  print_generic_expr (dump_file, v, dump_flags);
  fprintf (dump_file, " for ");
  print_generic_expr (dump_file, expr, dump_flags);
  
  if (vuses && VEC_length (tree, vuses) != 0)
    {
      size_t i;
      tree vuse;
      
      fprintf (dump_file, " vuses: (");
      for (i = 0; VEC_iterate (tree, vuses, i, vuse); i++)
	{
	  print_generic_expr (dump_file, vuse, dump_flags);
	  if (VEC_length (tree, vuses) - 1 != i)
	    fprintf (dump_file, ",");
	}
      fprintf (dump_file, ")");
    }		   
  fprintf (dump_file, "\n");
}

/* Like vn_lookup, but creates a new value for expression EXPR, if
   EXPR doesn't already have a value.  Return the existing/created
   value for EXPR.  STMT represents the stmt associated with EXPR.  It
   is used when computing the VUSES for EXPR.  */

tree
vn_lookup_or_add (tree expr, tree stmt)
{
  tree v = vn_lookup (expr, stmt);
  if (v == NULL_TREE)
    {
      VEC(tree,gc) *vuses;

      v = make_value_handle (TREE_TYPE (expr));
      vuses = copy_vuses_from_stmt (stmt);
      sort_vuses (vuses);

      if (dump_file && (dump_flags & TDF_DETAILS))
	print_creation_to_file (v, expr, vuses);

      VALUE_HANDLE_VUSES (v) = vuses;
      vn_add_with_vuses (expr, v, vuses);
    }

  set_value_handle (expr, v);

  return v;
}

/* Sort the VUSE array so that we can do equality comparisons
   quicker on two vuse vecs.  */

void 
sort_vuses (VEC (tree,gc) *vuses)
{
  if (VEC_length (tree, vuses) > 1)
    qsort (VEC_address (tree, vuses),
	   VEC_length (tree, vuses),
	   sizeof (tree),
	   vuses_compare);
}

/* Like vn_lookup, but creates a new value for expression EXPR, if
   EXPR doesn't already have a value.  Return the existing/created
   value for EXPR.  STMT represents the stmt associated with EXPR.  It is used
   when computing the hash value for EXPR.  */

tree
vn_lookup_or_add_with_vuses (tree expr, VEC (tree, gc) *vuses)
{
  tree v = vn_lookup_with_vuses (expr, vuses);
  if (v == NULL_TREE)
    {
      v = make_value_handle (TREE_TYPE (expr));
      sort_vuses (vuses);

      if (dump_file && (dump_flags & TDF_DETAILS))
	print_creation_to_file (v, expr, vuses);

      VALUE_HANDLE_VUSES (v) = vuses;
      vn_add_with_vuses (expr, v, vuses);
    }

  set_value_handle (expr, v);

  return v;
}



/* Get the value handle of EXPR.  This is the only correct way to get
   the value handle for a "thing".  If EXPR does not have a value
   handle associated, it returns NULL_TREE.  
   NB: If EXPR is min_invariant, this function is *required* to return EXPR.  */

tree
get_value_handle (tree expr)
{

  if (is_gimple_min_invariant (expr))
    return expr;

  if (TREE_CODE (expr) == SSA_NAME)
    return SSA_NAME_VALUE (expr);
  else if (EXPR_P (expr) || DECL_P (expr) || TREE_CODE (expr) == TREE_LIST
	   || TREE_CODE (expr) == CONSTRUCTOR)
    {
      tree_ann_common_t ann = tree_common_ann (expr);
      return ((ann) ? ann->value_handle : NULL_TREE);
    }
  else
    gcc_unreachable ();
}


/* Initialize data structures used in value numbering.  */

void
vn_init (void)
{
  value_table = htab_create (511, val_expr_pair_hash,
			     val_expr_pair_expr_eq, free);
  shared_lookup_vuses = NULL;
}


/* Delete data used for value numbering.  */

void
vn_delete (void)
{
  htab_delete (value_table);
  VEC_free (tree, gc, shared_lookup_vuses);
  value_table = NULL;
}
