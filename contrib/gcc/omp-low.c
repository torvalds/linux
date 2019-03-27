/* Lowering pass for OpenMP directives.  Converts OpenMP directives
   into explicit calls to the runtime library (libgomp) and data
   marshalling to implement data sharing and copying clauses.
   Contributed by Diego Novillo <dnovillo@redhat.com>

   Copyright (C) 2005, 2006 Free Software Foundation, Inc.

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
#include "rtl.h"
#include "tree-gimple.h"
#include "tree-inline.h"
#include "langhooks.h"
#include "diagnostic.h"
#include "tree-flow.h"
#include "timevar.h"
#include "flags.h"
#include "function.h"
#include "expr.h"
#include "toplev.h"
#include "tree-pass.h"
#include "ggc.h"
#include "except.h"


/* Lowering of OpenMP parallel and workshare constructs proceeds in two 
   phases.  The first phase scans the function looking for OMP statements
   and then for variables that must be replaced to satisfy data sharing
   clauses.  The second phase expands code for the constructs, as well as
   re-gimplifying things when variables have been replaced with complex
   expressions.

   Final code generation is done by pass_expand_omp.  The flowgraph is
   scanned for parallel regions which are then moved to a new
   function, to be invoked by the thread library.  */

/* Context structure.  Used to store information about each parallel
   directive in the code.  */

typedef struct omp_context
{
  /* This field must be at the beginning, as we do "inheritance": Some
     callback functions for tree-inline.c (e.g., omp_copy_decl)
     receive a copy_body_data pointer that is up-casted to an
     omp_context pointer.  */
  copy_body_data cb;

  /* The tree of contexts corresponding to the encountered constructs.  */
  struct omp_context *outer;
  tree stmt;

  /* Map variables to fields in a structure that allows communication 
     between sending and receiving threads.  */
  splay_tree field_map;
  tree record_type;
  tree sender_decl;
  tree receiver_decl;

  /* A chain of variables to add to the top-level block surrounding the
     construct.  In the case of a parallel, this is in the child function.  */
  tree block_vars;

  /* What to do with variables with implicitly determined sharing
     attributes.  */
  enum omp_clause_default_kind default_kind;

  /* Nesting depth of this context.  Used to beautify error messages re
     invalid gotos.  The outermost ctx is depth 1, with depth 0 being
     reserved for the main body of the function.  */
  int depth;

  /* True if this parallel directive is nested within another.  */
  bool is_nested;
} omp_context;


/* A structure describing the main elements of a parallel loop.  */

struct omp_for_data
{
  tree v, n1, n2, step, chunk_size, for_stmt;
  enum tree_code cond_code;
  tree pre;
  bool have_nowait, have_ordered;
  enum omp_clause_schedule_kind sched_kind;
};


static splay_tree all_contexts;
static int parallel_nesting_level;
struct omp_region *root_omp_region;

static void scan_omp (tree *, omp_context *);
static void lower_omp (tree *, omp_context *);
static tree lookup_decl_in_outer_ctx (tree, omp_context *);
static tree maybe_lookup_decl_in_outer_ctx (tree, omp_context *);

/* Find an OpenMP clause of type KIND within CLAUSES.  */

static tree
find_omp_clause (tree clauses, enum omp_clause_code kind)
{
  for (; clauses ; clauses = OMP_CLAUSE_CHAIN (clauses))
    if (OMP_CLAUSE_CODE (clauses) == kind)
      return clauses;

  return NULL_TREE;
}

/* Return true if CTX is for an omp parallel.  */

static inline bool
is_parallel_ctx (omp_context *ctx)
{
  return TREE_CODE (ctx->stmt) == OMP_PARALLEL;
}


/* Return true if REGION is a combined parallel+workshare region.  */

static inline bool
is_combined_parallel (struct omp_region *region)
{
  return region->is_combined_parallel;
}


/* Extract the header elements of parallel loop FOR_STMT and store
   them into *FD.  */

static void
extract_omp_for_data (tree for_stmt, struct omp_for_data *fd)
{
  tree t;

  fd->for_stmt = for_stmt;
  fd->pre = NULL;

  t = OMP_FOR_INIT (for_stmt);
  gcc_assert (TREE_CODE (t) == MODIFY_EXPR);
  fd->v = TREE_OPERAND (t, 0);
  gcc_assert (DECL_P (fd->v));
  gcc_assert (TREE_CODE (TREE_TYPE (fd->v)) == INTEGER_TYPE);
  fd->n1 = TREE_OPERAND (t, 1);

  t = OMP_FOR_COND (for_stmt);
  fd->cond_code = TREE_CODE (t);
  gcc_assert (TREE_OPERAND (t, 0) == fd->v);
  fd->n2 = TREE_OPERAND (t, 1);
  switch (fd->cond_code)
    {
    case LT_EXPR:
    case GT_EXPR:
      break;
    case LE_EXPR:
      fd->n2 = fold_build2 (PLUS_EXPR, TREE_TYPE (fd->n2), fd->n2,
			   build_int_cst (TREE_TYPE (fd->n2), 1));
      fd->cond_code = LT_EXPR;
      break;
    case GE_EXPR:
      fd->n2 = fold_build2 (MINUS_EXPR, TREE_TYPE (fd->n2), fd->n2,
			   build_int_cst (TREE_TYPE (fd->n2), 1));
      fd->cond_code = GT_EXPR;
      break;
    default:
      gcc_unreachable ();
    }

  t = OMP_FOR_INCR (fd->for_stmt);
  gcc_assert (TREE_CODE (t) == MODIFY_EXPR);
  gcc_assert (TREE_OPERAND (t, 0) == fd->v);
  t = TREE_OPERAND (t, 1);
  gcc_assert (TREE_OPERAND (t, 0) == fd->v);
  switch (TREE_CODE (t))
    {
    case PLUS_EXPR:
      fd->step = TREE_OPERAND (t, 1);
      break;
    case MINUS_EXPR:
      fd->step = TREE_OPERAND (t, 1);
      fd->step = fold_build1 (NEGATE_EXPR, TREE_TYPE (fd->step), fd->step);
      break;
    default:
      gcc_unreachable ();
    }

  fd->have_nowait = fd->have_ordered = false;
  fd->sched_kind = OMP_CLAUSE_SCHEDULE_STATIC;
  fd->chunk_size = NULL_TREE;

  for (t = OMP_FOR_CLAUSES (for_stmt); t ; t = OMP_CLAUSE_CHAIN (t))
    switch (OMP_CLAUSE_CODE (t))
      {
      case OMP_CLAUSE_NOWAIT:
	fd->have_nowait = true;
	break;
      case OMP_CLAUSE_ORDERED:
	fd->have_ordered = true;
	break;
      case OMP_CLAUSE_SCHEDULE:
	fd->sched_kind = OMP_CLAUSE_SCHEDULE_KIND (t);
	fd->chunk_size = OMP_CLAUSE_SCHEDULE_CHUNK_EXPR (t);
	break;
      default:
	break;
      }

  if (fd->sched_kind == OMP_CLAUSE_SCHEDULE_RUNTIME)
    gcc_assert (fd->chunk_size == NULL);
  else if (fd->chunk_size == NULL)
    {
      /* We only need to compute a default chunk size for ordered
	 static loops and dynamic loops.  */
      if (fd->sched_kind != OMP_CLAUSE_SCHEDULE_STATIC || fd->have_ordered)
	fd->chunk_size = (fd->sched_kind == OMP_CLAUSE_SCHEDULE_STATIC)
			 ? integer_zero_node : integer_one_node;
    }
}


/* Given two blocks PAR_ENTRY_BB and WS_ENTRY_BB such that WS_ENTRY_BB
   is the immediate dominator of PAR_ENTRY_BB, return true if there
   are no data dependencies that would prevent expanding the parallel
   directive at PAR_ENTRY_BB as a combined parallel+workshare region.

   When expanding a combined parallel+workshare region, the call to
   the child function may need additional arguments in the case of
   OMP_FOR regions.  In some cases, these arguments are computed out
   of variables passed in from the parent to the child via 'struct
   .omp_data_s'.  For instance:

	#pragma omp parallel for schedule (guided, i * 4)
	for (j ...)

   Is lowered into:

   	# BLOCK 2 (PAR_ENTRY_BB)
	.omp_data_o.i = i;
	#pragma omp parallel [child fn: bar.omp_fn.0 ( ..., D.1598)
	
	# BLOCK 3 (WS_ENTRY_BB)
	.omp_data_i = &.omp_data_o;
	D.1667 = .omp_data_i->i;
	D.1598 = D.1667 * 4;
	#pragma omp for schedule (guided, D.1598)

   When we outline the parallel region, the call to the child function
   'bar.omp_fn.0' will need the value D.1598 in its argument list, but
   that value is computed *after* the call site.  So, in principle we
   cannot do the transformation.

   To see whether the code in WS_ENTRY_BB blocks the combined
   parallel+workshare call, we collect all the variables used in the
   OMP_FOR header check whether they appear on the LHS of any
   statement in WS_ENTRY_BB.  If so, then we cannot emit the combined
   call.

   FIXME.  If we had the SSA form built at this point, we could merely
   hoist the code in block 3 into block 2 and be done with it.  But at
   this point we don't have dataflow information and though we could
   hack something up here, it is really not worth the aggravation.  */

static bool
workshare_safe_to_combine_p (basic_block par_entry_bb, basic_block ws_entry_bb)
{
  struct omp_for_data fd;
  tree par_stmt, ws_stmt;

  par_stmt = last_stmt (par_entry_bb);
  ws_stmt = last_stmt (ws_entry_bb);

  if (TREE_CODE (ws_stmt) == OMP_SECTIONS)
    return true;

  gcc_assert (TREE_CODE (ws_stmt) == OMP_FOR);

  extract_omp_for_data (ws_stmt, &fd);

  /* FIXME.  We give up too easily here.  If any of these arguments
     are not constants, they will likely involve variables that have
     been mapped into fields of .omp_data_s for sharing with the child
     function.  With appropriate data flow, it would be possible to
     see through this.  */
  if (!is_gimple_min_invariant (fd.n1)
      || !is_gimple_min_invariant (fd.n2)
      || !is_gimple_min_invariant (fd.step)
      || (fd.chunk_size && !is_gimple_min_invariant (fd.chunk_size)))
    return false;

  return true;
}


/* Collect additional arguments needed to emit a combined
   parallel+workshare call.  WS_STMT is the workshare directive being
   expanded.  */

static tree
get_ws_args_for (tree ws_stmt)
{
  tree t;

  if (TREE_CODE (ws_stmt) == OMP_FOR)
    {
      struct omp_for_data fd;
      tree ws_args;

      extract_omp_for_data (ws_stmt, &fd);

      ws_args = NULL_TREE;
      if (fd.chunk_size)
	{
	  t = fold_convert (long_integer_type_node, fd.chunk_size);
	  ws_args = tree_cons (NULL, t, ws_args);
	}

      t = fold_convert (long_integer_type_node, fd.step);
      ws_args = tree_cons (NULL, t, ws_args);

      t = fold_convert (long_integer_type_node, fd.n2);
      ws_args = tree_cons (NULL, t, ws_args);

      t = fold_convert (long_integer_type_node, fd.n1);
      ws_args = tree_cons (NULL, t, ws_args);

      return ws_args;
    }
  else if (TREE_CODE (ws_stmt) == OMP_SECTIONS)
    {
      basic_block bb = bb_for_stmt (ws_stmt);
      t = build_int_cst (unsigned_type_node, EDGE_COUNT (bb->succs));
      t = tree_cons (NULL, t, NULL);
      return t;
    }

  gcc_unreachable ();
}


/* Discover whether REGION is a combined parallel+workshare region.  */

static void
determine_parallel_type (struct omp_region *region)
{
  basic_block par_entry_bb, par_exit_bb;
  basic_block ws_entry_bb, ws_exit_bb;

  if (region == NULL || region->inner == NULL
      || region->exit == NULL || region->inner->exit == NULL)
    return;

  /* We only support parallel+for and parallel+sections.  */
  if (region->type != OMP_PARALLEL
      || (region->inner->type != OMP_FOR
	  && region->inner->type != OMP_SECTIONS))
    return;

  /* Check for perfect nesting PAR_ENTRY_BB -> WS_ENTRY_BB and
     WS_EXIT_BB -> PAR_EXIT_BB.  */
  par_entry_bb = region->entry;
  par_exit_bb = region->exit;
  ws_entry_bb = region->inner->entry;
  ws_exit_bb = region->inner->exit;

  if (single_succ (par_entry_bb) == ws_entry_bb
      && single_succ (ws_exit_bb) == par_exit_bb
      && workshare_safe_to_combine_p (par_entry_bb, ws_entry_bb)
      && (OMP_PARALLEL_COMBINED (last_stmt (par_entry_bb))
	  || (last_and_only_stmt (ws_entry_bb)
	      && last_and_only_stmt (par_exit_bb))))
    {
      tree ws_stmt = last_stmt (ws_entry_bb);

      if (region->inner->type == OMP_FOR)
	{
	  /* If this is a combined parallel loop, we need to determine
	     whether or not to use the combined library calls.  There
	     are two cases where we do not apply the transformation:
	     static loops and any kind of ordered loop.  In the first
	     case, we already open code the loop so there is no need
	     to do anything else.  In the latter case, the combined
	     parallel loop call would still need extra synchronization
	     to implement ordered semantics, so there would not be any
	     gain in using the combined call.  */
	  tree clauses = OMP_FOR_CLAUSES (ws_stmt);
	  tree c = find_omp_clause (clauses, OMP_CLAUSE_SCHEDULE);
	  if (c == NULL
	      || OMP_CLAUSE_SCHEDULE_KIND (c) == OMP_CLAUSE_SCHEDULE_STATIC
	      || find_omp_clause (clauses, OMP_CLAUSE_ORDERED))
	    {
	      region->is_combined_parallel = false;
	      region->inner->is_combined_parallel = false;
	      return;
	    }
	}

      region->is_combined_parallel = true;
      region->inner->is_combined_parallel = true;
      region->ws_args = get_ws_args_for (ws_stmt);
    }
}


/* Return true if EXPR is variable sized.  */

static inline bool
is_variable_sized (tree expr)
{
  return !TREE_CONSTANT (TYPE_SIZE_UNIT (TREE_TYPE (expr)));
}

/* Return true if DECL is a reference type.  */

static inline bool
is_reference (tree decl)
{
  return lang_hooks.decls.omp_privatize_by_reference (decl);
}

/* Lookup variables in the decl or field splay trees.  The "maybe" form
   allows for the variable form to not have been entered, otherwise we
   assert that the variable must have been entered.  */

static inline tree
lookup_decl (tree var, omp_context *ctx)
{
  splay_tree_node n;
  n = splay_tree_lookup (ctx->cb.decl_map, (splay_tree_key) var);
  return (tree) n->value;
}

static inline tree
maybe_lookup_decl (tree var, omp_context *ctx)
{
  splay_tree_node n;
  n = splay_tree_lookup (ctx->cb.decl_map, (splay_tree_key) var);
  return n ? (tree) n->value : NULL_TREE;
}

static inline tree
lookup_field (tree var, omp_context *ctx)
{
  splay_tree_node n;
  n = splay_tree_lookup (ctx->field_map, (splay_tree_key) var);
  return (tree) n->value;
}

static inline tree
maybe_lookup_field (tree var, omp_context *ctx)
{
  splay_tree_node n;
  n = splay_tree_lookup (ctx->field_map, (splay_tree_key) var);
  return n ? (tree) n->value : NULL_TREE;
}

/* Return true if DECL should be copied by pointer.  SHARED_P is true
   if DECL is to be shared.  */

static bool
use_pointer_for_field (tree decl, bool shared_p)
{
  if (AGGREGATE_TYPE_P (TREE_TYPE (decl)))
    return true;

  /* We can only use copy-in/copy-out semantics for shared variables
     when we know the value is not accessible from an outer scope.  */
  if (shared_p)
    {
      /* ??? Trivially accessible from anywhere.  But why would we even
	 be passing an address in this case?  Should we simply assert
	 this to be false, or should we have a cleanup pass that removes
	 these from the list of mappings?  */
      if (TREE_STATIC (decl) || DECL_EXTERNAL (decl))
	return true;

      /* For variables with DECL_HAS_VALUE_EXPR_P set, we cannot tell
	 without analyzing the expression whether or not its location
	 is accessible to anyone else.  In the case of nested parallel
	 regions it certainly may be.  */
      if (TREE_CODE (decl) != RESULT_DECL && DECL_HAS_VALUE_EXPR_P (decl))
	return true;

      /* Do not use copy-in/copy-out for variables that have their
	 address taken.  */
      if (TREE_ADDRESSABLE (decl))
	return true;
    }

  return false;
}

/* Construct a new automatic decl similar to VAR.  */

static tree
omp_copy_decl_2 (tree var, tree name, tree type, omp_context *ctx)
{
  tree copy = build_decl (VAR_DECL, name, type);

  TREE_ADDRESSABLE (copy) = TREE_ADDRESSABLE (var);
  DECL_COMPLEX_GIMPLE_REG_P (copy) = DECL_COMPLEX_GIMPLE_REG_P (var);
  DECL_ARTIFICIAL (copy) = DECL_ARTIFICIAL (var);
  DECL_IGNORED_P (copy) = DECL_IGNORED_P (var);
  TREE_USED (copy) = 1;
  DECL_CONTEXT (copy) = current_function_decl;
  DECL_SEEN_IN_BIND_EXPR_P (copy) = 1;

  TREE_CHAIN (copy) = ctx->block_vars;
  ctx->block_vars = copy;

  return copy;
}

static tree
omp_copy_decl_1 (tree var, omp_context *ctx)
{
  return omp_copy_decl_2 (var, DECL_NAME (var), TREE_TYPE (var), ctx);
}

/* Build tree nodes to access the field for VAR on the receiver side.  */

static tree
build_receiver_ref (tree var, bool by_ref, omp_context *ctx)
{
  tree x, field = lookup_field (var, ctx);

  /* If the receiver record type was remapped in the child function,
     remap the field into the new record type.  */
  x = maybe_lookup_field (field, ctx);
  if (x != NULL)
    field = x;

  x = build_fold_indirect_ref (ctx->receiver_decl);
  x = build3 (COMPONENT_REF, TREE_TYPE (field), x, field, NULL);
  if (by_ref)
    x = build_fold_indirect_ref (x);

  return x;
}

/* Build tree nodes to access VAR in the scope outer to CTX.  In the case
   of a parallel, this is a component reference; for workshare constructs
   this is some variable.  */

static tree
build_outer_var_ref (tree var, omp_context *ctx)
{
  tree x;

  if (is_global_var (maybe_lookup_decl_in_outer_ctx (var, ctx)))
    x = var;
  else if (is_variable_sized (var))
    {
      x = TREE_OPERAND (DECL_VALUE_EXPR (var), 0);
      x = build_outer_var_ref (x, ctx);
      x = build_fold_indirect_ref (x);
    }
  else if (is_parallel_ctx (ctx))
    {
      bool by_ref = use_pointer_for_field (var, false);
      x = build_receiver_ref (var, by_ref, ctx);
    }
  else if (ctx->outer)
    x = lookup_decl (var, ctx->outer);
  else if (is_reference (var))
    /* This can happen with orphaned constructs.  If var is reference, it is
       possible it is shared and as such valid.  */
    x = var;
  else
    gcc_unreachable ();

  if (is_reference (var))
    x = build_fold_indirect_ref (x);

  return x;
}

/* Build tree nodes to access the field for VAR on the sender side.  */

static tree
build_sender_ref (tree var, omp_context *ctx)
{
  tree field = lookup_field (var, ctx);
  return build3 (COMPONENT_REF, TREE_TYPE (field),
		 ctx->sender_decl, field, NULL);
}

/* Add a new field for VAR inside the structure CTX->SENDER_DECL.  */

static void
install_var_field (tree var, bool by_ref, omp_context *ctx)
{
  tree field, type;

  gcc_assert (!splay_tree_lookup (ctx->field_map, (splay_tree_key) var));

  type = TREE_TYPE (var);
  if (by_ref)
    type = build_pointer_type (type);

  field = build_decl (FIELD_DECL, DECL_NAME (var), type);

  /* Remember what variable this field was created for.  This does have a
     side effect of making dwarf2out ignore this member, so for helpful
     debugging we clear it later in delete_omp_context.  */
  DECL_ABSTRACT_ORIGIN (field) = var;

  insert_field_into_struct (ctx->record_type, field);

  splay_tree_insert (ctx->field_map, (splay_tree_key) var,
		     (splay_tree_value) field);
}

static tree
install_var_local (tree var, omp_context *ctx)
{
  tree new_var = omp_copy_decl_1 (var, ctx);
  insert_decl_map (&ctx->cb, var, new_var);
  return new_var;
}

/* Adjust the replacement for DECL in CTX for the new context.  This means
   copying the DECL_VALUE_EXPR, and fixing up the type.  */

static void
fixup_remapped_decl (tree decl, omp_context *ctx, bool private_debug)
{
  tree new_decl, size;

  new_decl = lookup_decl (decl, ctx);

  TREE_TYPE (new_decl) = remap_type (TREE_TYPE (decl), &ctx->cb);

  if ((!TREE_CONSTANT (DECL_SIZE (new_decl)) || private_debug)
      && DECL_HAS_VALUE_EXPR_P (decl))
    {
      tree ve = DECL_VALUE_EXPR (decl);
      walk_tree (&ve, copy_body_r, &ctx->cb, NULL);
      SET_DECL_VALUE_EXPR (new_decl, ve);
      DECL_HAS_VALUE_EXPR_P (new_decl) = 1;
    }

  if (!TREE_CONSTANT (DECL_SIZE (new_decl)))
    {
      size = remap_decl (DECL_SIZE (decl), &ctx->cb);
      if (size == error_mark_node)
	size = TYPE_SIZE (TREE_TYPE (new_decl));
      DECL_SIZE (new_decl) = size;

      size = remap_decl (DECL_SIZE_UNIT (decl), &ctx->cb);
      if (size == error_mark_node)
	size = TYPE_SIZE_UNIT (TREE_TYPE (new_decl));
      DECL_SIZE_UNIT (new_decl) = size;
    }
}

/* The callback for remap_decl.  Search all containing contexts for a
   mapping of the variable; this avoids having to duplicate the splay
   tree ahead of time.  We know a mapping doesn't already exist in the
   given context.  Create new mappings to implement default semantics.  */

static tree
omp_copy_decl (tree var, copy_body_data *cb)
{
  omp_context *ctx = (omp_context *) cb;
  tree new_var;

  if (TREE_CODE (var) == LABEL_DECL)
    {
      new_var = create_artificial_label ();
      DECL_CONTEXT (new_var) = current_function_decl;
      insert_decl_map (&ctx->cb, var, new_var);
      return new_var;
    }

  while (!is_parallel_ctx (ctx))
    {
      ctx = ctx->outer;
      if (ctx == NULL)
	return var;
      new_var = maybe_lookup_decl (var, ctx);
      if (new_var)
	return new_var;
    }

  if (is_global_var (var) || decl_function_context (var) != ctx->cb.src_fn)
    return var;

  return error_mark_node;
}


/* Return the parallel region associated with STMT.  */

/* Debugging dumps for parallel regions.  */
void dump_omp_region (FILE *, struct omp_region *, int);
void debug_omp_region (struct omp_region *);
void debug_all_omp_regions (void);

/* Dump the parallel region tree rooted at REGION.  */

void
dump_omp_region (FILE *file, struct omp_region *region, int indent)
{
  fprintf (file, "%*sbb %d: %s\n", indent, "", region->entry->index,
	   tree_code_name[region->type]);

  if (region->inner)
    dump_omp_region (file, region->inner, indent + 4);

  if (region->cont)
    {
      fprintf (file, "%*sbb %d: OMP_CONTINUE\n", indent, "",
	       region->cont->index);
    }
    
  if (region->exit)
    fprintf (file, "%*sbb %d: OMP_RETURN\n", indent, "",
	     region->exit->index);
  else
    fprintf (file, "%*s[no exit marker]\n", indent, "");

  if (region->next)
    dump_omp_region (file, region->next, indent);
}

void
debug_omp_region (struct omp_region *region)
{
  dump_omp_region (stderr, region, 0);
}

void
debug_all_omp_regions (void)
{
  dump_omp_region (stderr, root_omp_region, 0);
}


/* Create a new parallel region starting at STMT inside region PARENT.  */

struct omp_region *
new_omp_region (basic_block bb, enum tree_code type, struct omp_region *parent)
{
  struct omp_region *region = xcalloc (1, sizeof (*region));

  region->outer = parent;
  region->entry = bb;
  region->type = type;

  if (parent)
    {
      /* This is a nested region.  Add it to the list of inner
	 regions in PARENT.  */
      region->next = parent->inner;
      parent->inner = region;
    }
  else
    {
      /* This is a toplevel region.  Add it to the list of toplevel
	 regions in ROOT_OMP_REGION.  */
      region->next = root_omp_region;
      root_omp_region = region;
    }

  return region;
}

/* Release the memory associated with the region tree rooted at REGION.  */

static void
free_omp_region_1 (struct omp_region *region)
{
  struct omp_region *i, *n;

  for (i = region->inner; i ; i = n)
    {
      n = i->next;
      free_omp_region_1 (i);
    }

  free (region);
}

/* Release the memory for the entire omp region tree.  */

void
free_omp_regions (void)
{
  struct omp_region *r, *n;
  for (r = root_omp_region; r ; r = n)
    {
      n = r->next;
      free_omp_region_1 (r);
    }
  root_omp_region = NULL;
}


/* Create a new context, with OUTER_CTX being the surrounding context.  */

static omp_context *
new_omp_context (tree stmt, omp_context *outer_ctx)
{
  omp_context *ctx = XCNEW (omp_context);

  splay_tree_insert (all_contexts, (splay_tree_key) stmt,
		     (splay_tree_value) ctx);
  ctx->stmt = stmt;

  if (outer_ctx)
    {
      ctx->outer = outer_ctx;
      ctx->cb = outer_ctx->cb;
      ctx->cb.block = NULL;
      ctx->depth = outer_ctx->depth + 1;
    }
  else
    {
      ctx->cb.src_fn = current_function_decl;
      ctx->cb.dst_fn = current_function_decl;
      ctx->cb.src_node = cgraph_node (current_function_decl);
      ctx->cb.dst_node = ctx->cb.src_node;
      ctx->cb.src_cfun = cfun;
      ctx->cb.copy_decl = omp_copy_decl;
      ctx->cb.eh_region = -1;
      ctx->cb.transform_call_graph_edges = CB_CGE_MOVE;
      ctx->depth = 1;
    }

  ctx->cb.decl_map = splay_tree_new (splay_tree_compare_pointers, 0, 0);

  return ctx;
}

/* Destroy a omp_context data structures.  Called through the splay tree
   value delete callback.  */

static void
delete_omp_context (splay_tree_value value)
{
  omp_context *ctx = (omp_context *) value;

  splay_tree_delete (ctx->cb.decl_map);

  if (ctx->field_map)
    splay_tree_delete (ctx->field_map);

  /* We hijacked DECL_ABSTRACT_ORIGIN earlier.  We need to clear it before
     it produces corrupt debug information.  */
  if (ctx->record_type)
    {
      tree t;
      for (t = TYPE_FIELDS (ctx->record_type); t ; t = TREE_CHAIN (t))
	DECL_ABSTRACT_ORIGIN (t) = NULL;
    }

  XDELETE (ctx);
}

/* Fix up RECEIVER_DECL with a type that has been remapped to the child
   context.  */

static void
fixup_child_record_type (omp_context *ctx)
{
  tree f, type = ctx->record_type;

  /* ??? It isn't sufficient to just call remap_type here, because
     variably_modified_type_p doesn't work the way we expect for
     record types.  Testing each field for whether it needs remapping
     and creating a new record by hand works, however.  */
  for (f = TYPE_FIELDS (type); f ; f = TREE_CHAIN (f))
    if (variably_modified_type_p (TREE_TYPE (f), ctx->cb.src_fn))
      break;
  if (f)
    {
      tree name, new_fields = NULL;

      type = lang_hooks.types.make_type (RECORD_TYPE);
      name = DECL_NAME (TYPE_NAME (ctx->record_type));
      name = build_decl (TYPE_DECL, name, type);
      TYPE_NAME (type) = name;

      for (f = TYPE_FIELDS (ctx->record_type); f ; f = TREE_CHAIN (f))
	{
	  tree new_f = copy_node (f);
	  DECL_CONTEXT (new_f) = type;
	  TREE_TYPE (new_f) = remap_type (TREE_TYPE (f), &ctx->cb);
	  TREE_CHAIN (new_f) = new_fields;
	  new_fields = new_f;

	  /* Arrange to be able to look up the receiver field
	     given the sender field.  */
	  splay_tree_insert (ctx->field_map, (splay_tree_key) f,
			     (splay_tree_value) new_f);
	}
      TYPE_FIELDS (type) = nreverse (new_fields);
      layout_type (type);
    }

  TREE_TYPE (ctx->receiver_decl) = build_pointer_type (type);
}

/* Instantiate decls as necessary in CTX to satisfy the data sharing
   specified by CLAUSES.  */

static void
scan_sharing_clauses (tree clauses, omp_context *ctx)
{
  tree c, decl;
  bool scan_array_reductions = false;

  for (c = clauses; c; c = OMP_CLAUSE_CHAIN (c))
    {
      bool by_ref;

      switch (OMP_CLAUSE_CODE (c))
	{
	case OMP_CLAUSE_PRIVATE:
	  decl = OMP_CLAUSE_DECL (c);
	  if (!is_variable_sized (decl))
	    install_var_local (decl, ctx);
	  break;

	case OMP_CLAUSE_SHARED:
	  gcc_assert (is_parallel_ctx (ctx));
	  decl = OMP_CLAUSE_DECL (c);
	  gcc_assert (!is_variable_sized (decl));
	  by_ref = use_pointer_for_field (decl, true);
	  /* Global variables don't need to be copied,
	     the receiver side will use them directly.  */
	  if (is_global_var (maybe_lookup_decl_in_outer_ctx (decl, ctx)))
	    break;
	  if (! TREE_READONLY (decl)
	      || TREE_ADDRESSABLE (decl)
	      || by_ref
	      || is_reference (decl))
	    {
	      install_var_field (decl, by_ref, ctx);
	      install_var_local (decl, ctx);
	      break;
	    }
	  /* We don't need to copy const scalar vars back.  */
	  OMP_CLAUSE_SET_CODE (c, OMP_CLAUSE_FIRSTPRIVATE);
	  goto do_private;

	case OMP_CLAUSE_LASTPRIVATE:
	  /* Let the corresponding firstprivate clause create
	     the variable.  */
	  if (OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE (c))
	    break;
	  /* FALLTHRU */

	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_REDUCTION:
	  decl = OMP_CLAUSE_DECL (c);
	do_private:
	  if (is_variable_sized (decl))
	    break;
	  else if (is_parallel_ctx (ctx)
		   && ! is_global_var (maybe_lookup_decl_in_outer_ctx (decl,
								       ctx)))
	    {
	      by_ref = use_pointer_for_field (decl, false);
	      install_var_field (decl, by_ref, ctx);
	    }
	  install_var_local (decl, ctx);
	  break;

	case OMP_CLAUSE_COPYPRIVATE:
	  if (ctx->outer)
	    scan_omp (&OMP_CLAUSE_DECL (c), ctx->outer);
	  /* FALLTHRU */

	case OMP_CLAUSE_COPYIN:
	  decl = OMP_CLAUSE_DECL (c);
	  by_ref = use_pointer_for_field (decl, false);
	  install_var_field (decl, by_ref, ctx);
	  break;

	case OMP_CLAUSE_DEFAULT:
	  ctx->default_kind = OMP_CLAUSE_DEFAULT_KIND (c);
	  break;

	case OMP_CLAUSE_IF:
	case OMP_CLAUSE_NUM_THREADS:
	case OMP_CLAUSE_SCHEDULE:
	  if (ctx->outer)
	    scan_omp (&OMP_CLAUSE_OPERAND (c, 0), ctx->outer);
	  break;

	case OMP_CLAUSE_NOWAIT:
	case OMP_CLAUSE_ORDERED:
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  for (c = clauses; c; c = OMP_CLAUSE_CHAIN (c))
    {
      switch (OMP_CLAUSE_CODE (c))
	{
	case OMP_CLAUSE_LASTPRIVATE:
	  /* Let the corresponding firstprivate clause create
	     the variable.  */
	  if (OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE (c))
	    break;
	  /* FALLTHRU */

	case OMP_CLAUSE_PRIVATE:
	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_REDUCTION:
	  decl = OMP_CLAUSE_DECL (c);
	  if (is_variable_sized (decl))
	    install_var_local (decl, ctx);
	  fixup_remapped_decl (decl, ctx,
			       OMP_CLAUSE_CODE (c) == OMP_CLAUSE_PRIVATE
			       && OMP_CLAUSE_PRIVATE_DEBUG (c));
	  if (OMP_CLAUSE_CODE (c) == OMP_CLAUSE_REDUCTION
	      && OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
	    scan_array_reductions = true;
	  break;

	case OMP_CLAUSE_SHARED:
	  decl = OMP_CLAUSE_DECL (c);
	  if (! is_global_var (maybe_lookup_decl_in_outer_ctx (decl, ctx)))
	    fixup_remapped_decl (decl, ctx, false);
	  break;

	case OMP_CLAUSE_COPYPRIVATE:
	case OMP_CLAUSE_COPYIN:
	case OMP_CLAUSE_DEFAULT:
	case OMP_CLAUSE_IF:
	case OMP_CLAUSE_NUM_THREADS:
	case OMP_CLAUSE_SCHEDULE:
	case OMP_CLAUSE_NOWAIT:
	case OMP_CLAUSE_ORDERED:
	  break;

	default:
	  gcc_unreachable ();
	}
    }

  if (scan_array_reductions)
    for (c = clauses; c; c = OMP_CLAUSE_CHAIN (c))
      if (OMP_CLAUSE_CODE (c) == OMP_CLAUSE_REDUCTION
	  && OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
	{
	  scan_omp (&OMP_CLAUSE_REDUCTION_INIT (c), ctx);
	  scan_omp (&OMP_CLAUSE_REDUCTION_MERGE (c), ctx);
	}
}

/* Create a new name for omp child function.  Returns an identifier.  */

static GTY(()) unsigned int tmp_ompfn_id_num;

static tree
create_omp_child_function_name (void)
{
  tree name = DECL_ASSEMBLER_NAME (current_function_decl);
  size_t len = IDENTIFIER_LENGTH (name);
  char *tmp_name, *prefix;

  prefix = alloca (len + sizeof ("_omp_fn"));
  memcpy (prefix, IDENTIFIER_POINTER (name), len);
  strcpy (prefix + len, "_omp_fn");
#ifndef NO_DOT_IN_LABEL
  prefix[len] = '.';
#elif !defined NO_DOLLAR_IN_LABEL
  prefix[len] = '$';
#endif
  ASM_FORMAT_PRIVATE_NAME (tmp_name, prefix, tmp_ompfn_id_num++);
  return get_identifier (tmp_name);
}

/* Build a decl for the omp child function.  It'll not contain a body
   yet, just the bare decl.  */

static void
create_omp_child_function (omp_context *ctx)
{
  tree decl, type, name, t;

  name = create_omp_child_function_name ();
  type = build_function_type_list (void_type_node, ptr_type_node, NULL_TREE);

  decl = build_decl (FUNCTION_DECL, name, type);
  decl = lang_hooks.decls.pushdecl (decl);

  ctx->cb.dst_fn = decl;

  TREE_STATIC (decl) = 1;
  TREE_USED (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  DECL_IGNORED_P (decl) = 0;
  TREE_PUBLIC (decl) = 0;
  DECL_UNINLINABLE (decl) = 1;
  DECL_EXTERNAL (decl) = 0;
  DECL_CONTEXT (decl) = NULL_TREE;
  DECL_INITIAL (decl) = make_node (BLOCK);

  t = build_decl (RESULT_DECL, NULL_TREE, void_type_node);
  DECL_ARTIFICIAL (t) = 1;
  DECL_IGNORED_P (t) = 1;
  DECL_RESULT (decl) = t;

  t = build_decl (PARM_DECL, get_identifier (".omp_data_i"), ptr_type_node);
  DECL_ARTIFICIAL (t) = 1;
  DECL_ARG_TYPE (t) = ptr_type_node;
  DECL_CONTEXT (t) = current_function_decl;
  TREE_USED (t) = 1;
  DECL_ARGUMENTS (decl) = t;
  ctx->receiver_decl = t;

  /* Allocate memory for the function structure.  The call to 
     allocate_struct_function clobbers CFUN, so we need to restore
     it afterward.  */
  allocate_struct_function (decl);
  DECL_SOURCE_LOCATION (decl) = EXPR_LOCATION (ctx->stmt);
  cfun->function_end_locus = EXPR_LOCATION (ctx->stmt);
  cfun = ctx->cb.src_cfun;
}


/* Scan an OpenMP parallel directive.  */

static void
scan_omp_parallel (tree *stmt_p, omp_context *outer_ctx)
{
  omp_context *ctx;
  tree name;

  /* Ignore parallel directives with empty bodies, unless there
     are copyin clauses.  */
  if (optimize > 0
      && empty_body_p (OMP_PARALLEL_BODY (*stmt_p))
      && find_omp_clause (OMP_CLAUSES (*stmt_p), OMP_CLAUSE_COPYIN) == NULL)
    {
      *stmt_p = build_empty_stmt ();
      return;
    }

  ctx = new_omp_context (*stmt_p, outer_ctx);
  if (parallel_nesting_level > 1)
    ctx->is_nested = true;
  ctx->field_map = splay_tree_new (splay_tree_compare_pointers, 0, 0);
  ctx->default_kind = OMP_CLAUSE_DEFAULT_SHARED;
  ctx->record_type = lang_hooks.types.make_type (RECORD_TYPE);
  name = create_tmp_var_name (".omp_data_s");
  name = build_decl (TYPE_DECL, name, ctx->record_type);
  TYPE_NAME (ctx->record_type) = name;
  create_omp_child_function (ctx);
  OMP_PARALLEL_FN (*stmt_p) = ctx->cb.dst_fn;

  scan_sharing_clauses (OMP_PARALLEL_CLAUSES (*stmt_p), ctx);
  scan_omp (&OMP_PARALLEL_BODY (*stmt_p), ctx);

  if (TYPE_FIELDS (ctx->record_type) == NULL)
    ctx->record_type = ctx->receiver_decl = NULL;
  else
    {
      layout_type (ctx->record_type);
      fixup_child_record_type (ctx);
    }
}


/* Scan an OpenMP loop directive.  */

static void
scan_omp_for (tree *stmt_p, omp_context *outer_ctx)
{
  omp_context *ctx;
  tree stmt;

  stmt = *stmt_p;
  ctx = new_omp_context (stmt, outer_ctx);

  scan_sharing_clauses (OMP_FOR_CLAUSES (stmt), ctx);

  scan_omp (&OMP_FOR_PRE_BODY (stmt), ctx);
  scan_omp (&OMP_FOR_INIT (stmt), ctx);
  scan_omp (&OMP_FOR_COND (stmt), ctx);
  scan_omp (&OMP_FOR_INCR (stmt), ctx);
  scan_omp (&OMP_FOR_BODY (stmt), ctx);
}

/* Scan an OpenMP sections directive.  */

static void
scan_omp_sections (tree *stmt_p, omp_context *outer_ctx)
{
  tree stmt;
  omp_context *ctx;

  stmt = *stmt_p;
  ctx = new_omp_context (stmt, outer_ctx);
  scan_sharing_clauses (OMP_SECTIONS_CLAUSES (stmt), ctx);
  scan_omp (&OMP_SECTIONS_BODY (stmt), ctx);
}

/* Scan an OpenMP single directive.  */

static void
scan_omp_single (tree *stmt_p, omp_context *outer_ctx)
{
  tree stmt = *stmt_p;
  omp_context *ctx;
  tree name;

  ctx = new_omp_context (stmt, outer_ctx);
  ctx->field_map = splay_tree_new (splay_tree_compare_pointers, 0, 0);
  ctx->record_type = lang_hooks.types.make_type (RECORD_TYPE);
  name = create_tmp_var_name (".omp_copy_s");
  name = build_decl (TYPE_DECL, name, ctx->record_type);
  TYPE_NAME (ctx->record_type) = name;

  scan_sharing_clauses (OMP_SINGLE_CLAUSES (stmt), ctx);
  scan_omp (&OMP_SINGLE_BODY (stmt), ctx);

  if (TYPE_FIELDS (ctx->record_type) == NULL)
    ctx->record_type = NULL;
  else
    layout_type (ctx->record_type);
}


/* Check OpenMP nesting restrictions.  */
static void
check_omp_nesting_restrictions (tree t, omp_context *ctx)
{
  switch (TREE_CODE (t))
    {
    case OMP_FOR:
    case OMP_SECTIONS:
    case OMP_SINGLE:
      for (; ctx != NULL; ctx = ctx->outer)
	switch (TREE_CODE (ctx->stmt))
	  {
	  case OMP_FOR:
	  case OMP_SECTIONS:
	  case OMP_SINGLE:
	  case OMP_ORDERED:
	  case OMP_MASTER:
	    warning (0, "work-sharing region may not be closely nested inside "
			"of work-sharing, critical, ordered or master region");
	    return;
	  case OMP_PARALLEL:
	    return;
	  default:
	    break;
	  }
      break;
    case OMP_MASTER:
      for (; ctx != NULL; ctx = ctx->outer)
	switch (TREE_CODE (ctx->stmt))
	  {
	  case OMP_FOR:
	  case OMP_SECTIONS:
	  case OMP_SINGLE:
	    warning (0, "master region may not be closely nested inside "
			"of work-sharing region");
	    return;
	  case OMP_PARALLEL:
	    return;
	  default:
	    break;
	  }
      break;
    case OMP_ORDERED:
      for (; ctx != NULL; ctx = ctx->outer)
	switch (TREE_CODE (ctx->stmt))
	  {
	  case OMP_CRITICAL:
	    warning (0, "ordered region may not be closely nested inside "
			"of critical region");
	    return;
	  case OMP_FOR:
	    if (find_omp_clause (OMP_CLAUSES (ctx->stmt),
				 OMP_CLAUSE_ORDERED) == NULL)
	      warning (0, "ordered region must be closely nested inside "
			  "a loop region with an ordered clause");
	    return;
	  case OMP_PARALLEL:
	    return;
	  default:
	    break;
	  }
      break;
    case OMP_CRITICAL:
      for (; ctx != NULL; ctx = ctx->outer)
	if (TREE_CODE (ctx->stmt) == OMP_CRITICAL
	    && OMP_CRITICAL_NAME (t) == OMP_CRITICAL_NAME (ctx->stmt))
	  {
	    warning (0, "critical region may not be nested inside a critical "
			"region with the same name");
	    return;
	  }
      break;
    default:
      break;
    }
}


/* Callback for walk_stmts used to scan for OpenMP directives at TP.  */

static tree
scan_omp_1 (tree *tp, int *walk_subtrees, void *data)
{
  struct walk_stmt_info *wi = data;
  omp_context *ctx = wi->info;
  tree t = *tp;

  if (EXPR_HAS_LOCATION (t))
    input_location = EXPR_LOCATION (t);

  /* Check the OpenMP nesting restrictions.  */
  if (OMP_DIRECTIVE_P (t) && ctx != NULL)
    check_omp_nesting_restrictions (t, ctx);

  *walk_subtrees = 0;
  switch (TREE_CODE (t))
    {
    case OMP_PARALLEL:
      parallel_nesting_level++;
      scan_omp_parallel (tp, ctx);
      parallel_nesting_level--;
      break;

    case OMP_FOR:
      scan_omp_for (tp, ctx);
      break;

    case OMP_SECTIONS:
      scan_omp_sections (tp, ctx);
      break;

    case OMP_SINGLE:
      scan_omp_single (tp, ctx);
      break;

    case OMP_SECTION:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
      ctx = new_omp_context (*tp, ctx);
      scan_omp (&OMP_BODY (*tp), ctx);
      break;

    case BIND_EXPR:
      {
	tree var;
	*walk_subtrees = 1;

	for (var = BIND_EXPR_VARS (t); var ; var = TREE_CHAIN (var))
	  insert_decl_map (&ctx->cb, var, var);
      }
      break;

    case VAR_DECL:
    case PARM_DECL:
    case LABEL_DECL:
    case RESULT_DECL:
      if (ctx)
	*tp = remap_decl (t, &ctx->cb);
      break;

    default:
      if (ctx && TYPE_P (t))
	*tp = remap_type (t, &ctx->cb);
      else if (!DECL_P (t))
	*walk_subtrees = 1;
      break;
    }

  return NULL_TREE;
}


/* Scan all the statements starting at STMT_P.  CTX contains context
   information about the OpenMP directives and clauses found during
   the scan.  */

static void
scan_omp (tree *stmt_p, omp_context *ctx)
{
  location_t saved_location;
  struct walk_stmt_info wi;

  memset (&wi, 0, sizeof (wi));
  wi.callback = scan_omp_1;
  wi.info = ctx;
  wi.want_bind_expr = (ctx != NULL);
  wi.want_locations = true;

  saved_location = input_location;
  walk_stmts (&wi, stmt_p);
  input_location = saved_location;
}

/* Re-gimplification and code generation routines.  */

/* Build a call to GOMP_barrier.  */

static void
build_omp_barrier (tree *stmt_list)
{
  tree t;

  t = built_in_decls[BUILT_IN_GOMP_BARRIER];
  t = build_function_call_expr (t, NULL);
  gimplify_and_add (t, stmt_list);
}

/* If a context was created for STMT when it was scanned, return it.  */

static omp_context *
maybe_lookup_ctx (tree stmt)
{
  splay_tree_node n;
  n = splay_tree_lookup (all_contexts, (splay_tree_key) stmt);
  return n ? (omp_context *) n->value : NULL;
}


/* Find the mapping for DECL in CTX or the immediately enclosing
   context that has a mapping for DECL.

   If CTX is a nested parallel directive, we may have to use the decl
   mappings created in CTX's parent context.  Suppose that we have the
   following parallel nesting (variable UIDs showed for clarity):

	iD.1562 = 0;
     	#omp parallel shared(iD.1562)		-> outer parallel
	  iD.1562 = iD.1562 + 1;

	  #omp parallel shared (iD.1562)	-> inner parallel
	     iD.1562 = iD.1562 - 1;

   Each parallel structure will create a distinct .omp_data_s structure
   for copying iD.1562 in/out of the directive:

  	outer parallel		.omp_data_s.1.i -> iD.1562
	inner parallel		.omp_data_s.2.i -> iD.1562

   A shared variable mapping will produce a copy-out operation before
   the parallel directive and a copy-in operation after it.  So, in
   this case we would have:

  	iD.1562 = 0;
	.omp_data_o.1.i = iD.1562;
	#omp parallel shared(iD.1562)		-> outer parallel
	  .omp_data_i.1 = &.omp_data_o.1
	  .omp_data_i.1->i = .omp_data_i.1->i + 1;

	  .omp_data_o.2.i = iD.1562;		-> **
	  #omp parallel shared(iD.1562)		-> inner parallel
	    .omp_data_i.2 = &.omp_data_o.2
	    .omp_data_i.2->i = .omp_data_i.2->i - 1;


    ** This is a problem.  The symbol iD.1562 cannot be referenced
       inside the body of the outer parallel region.  But since we are
       emitting this copy operation while expanding the inner parallel
       directive, we need to access the CTX structure of the outer
       parallel directive to get the correct mapping:

	  .omp_data_o.2.i = .omp_data_i.1->i

    Since there may be other workshare or parallel directives enclosing
    the parallel directive, it may be necessary to walk up the context
    parent chain.  This is not a problem in general because nested
    parallelism happens only rarely.  */

static tree
lookup_decl_in_outer_ctx (tree decl, omp_context *ctx)
{
  tree t;
  omp_context *up;

  gcc_assert (ctx->is_nested);

  for (up = ctx->outer, t = NULL; up && t == NULL; up = up->outer)
    t = maybe_lookup_decl (decl, up);

  gcc_assert (t || is_global_var (decl));

  return t ? t : decl;
}


/* Similar to lookup_decl_in_outer_ctx, but return DECL if not found
   in outer contexts.  */

static tree
maybe_lookup_decl_in_outer_ctx (tree decl, omp_context *ctx)
{
  tree t = NULL;
  omp_context *up;

  if (ctx->is_nested)
    for (up = ctx->outer, t = NULL; up && t == NULL; up = up->outer)
      t = maybe_lookup_decl (decl, up);

  return t ? t : decl;
}


/* Construct the initialization value for reduction CLAUSE.  */

tree
omp_reduction_init (tree clause, tree type)
{
  switch (OMP_CLAUSE_REDUCTION_CODE (clause))
    {
    case PLUS_EXPR:
    case MINUS_EXPR:
    case BIT_IOR_EXPR:
    case BIT_XOR_EXPR:
    case TRUTH_OR_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_XOR_EXPR:
    case NE_EXPR:
      return fold_convert (type, integer_zero_node);

    case MULT_EXPR:
    case TRUTH_AND_EXPR:
    case TRUTH_ANDIF_EXPR:
    case EQ_EXPR:
      return fold_convert (type, integer_one_node);

    case BIT_AND_EXPR:
      return fold_convert (type, integer_minus_one_node);

    case MAX_EXPR:
      if (SCALAR_FLOAT_TYPE_P (type))
	{
	  REAL_VALUE_TYPE max, min;
	  if (HONOR_INFINITIES (TYPE_MODE (type)))
	    {
	      real_inf (&max);
	      real_arithmetic (&min, NEGATE_EXPR, &max, NULL);
	    }
	  else
	    real_maxval (&min, 1, TYPE_MODE (type));
	  return build_real (type, min);
	}
      else
	{
	  gcc_assert (INTEGRAL_TYPE_P (type));
	  return TYPE_MIN_VALUE (type);
	}

    case MIN_EXPR:
      if (SCALAR_FLOAT_TYPE_P (type))
	{
	  REAL_VALUE_TYPE max;
	  if (HONOR_INFINITIES (TYPE_MODE (type)))
	    real_inf (&max);
	  else
	    real_maxval (&max, 0, TYPE_MODE (type));
	  return build_real (type, max);
	}
      else
	{
	  gcc_assert (INTEGRAL_TYPE_P (type));
	  return TYPE_MAX_VALUE (type);
	}

    default:
      gcc_unreachable ();
    }
}

/* Generate code to implement the input clauses, FIRSTPRIVATE and COPYIN,
   from the receiver (aka child) side and initializers for REFERENCE_TYPE
   private variables.  Initialization statements go in ILIST, while calls
   to destructors go in DLIST.  */

static void
lower_rec_input_clauses (tree clauses, tree *ilist, tree *dlist,
			 omp_context *ctx)
{
  tree_stmt_iterator diter;
  tree c, dtor, copyin_seq, x, args, ptr;
  bool copyin_by_ref = false;
  bool lastprivate_firstprivate = false;
  int pass;

  *dlist = alloc_stmt_list ();
  diter = tsi_start (*dlist);
  copyin_seq = NULL;

  /* Do all the fixed sized types in the first pass, and the variable sized
     types in the second pass.  This makes sure that the scalar arguments to
     the variable sized types are processed before we use them in the 
     variable sized operations.  */
  for (pass = 0; pass < 2; ++pass)
    {
      for (c = clauses; c ; c = OMP_CLAUSE_CHAIN (c))
	{
	  enum omp_clause_code c_kind = OMP_CLAUSE_CODE (c);
	  tree var, new_var;
	  bool by_ref;

	  switch (c_kind)
	    {
	    case OMP_CLAUSE_PRIVATE:
	      if (OMP_CLAUSE_PRIVATE_DEBUG (c))
		continue;
	      break;
	    case OMP_CLAUSE_SHARED:
	      if (maybe_lookup_decl (OMP_CLAUSE_DECL (c), ctx) == NULL)
		{
		  gcc_assert (is_global_var (OMP_CLAUSE_DECL (c)));
		  continue;
		}
	    case OMP_CLAUSE_FIRSTPRIVATE:
	    case OMP_CLAUSE_COPYIN:
	    case OMP_CLAUSE_REDUCTION:
	      break;
	    case OMP_CLAUSE_LASTPRIVATE:
	      if (OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE (c))
		{
		  lastprivate_firstprivate = true;
		  if (pass != 0)
		    continue;
		}
	      break;
	    default:
	      continue;
	    }

	  new_var = var = OMP_CLAUSE_DECL (c);
	  if (c_kind != OMP_CLAUSE_COPYIN)
	    new_var = lookup_decl (var, ctx);

	  if (c_kind == OMP_CLAUSE_SHARED || c_kind == OMP_CLAUSE_COPYIN)
	    {
	      if (pass != 0)
		continue;
	    }
	  else if (is_variable_sized (var))
	    {
	      /* For variable sized types, we need to allocate the
		 actual storage here.  Call alloca and store the
		 result in the pointer decl that we created elsewhere.  */
	      if (pass == 0)
		continue;

	      ptr = DECL_VALUE_EXPR (new_var);
	      gcc_assert (TREE_CODE (ptr) == INDIRECT_REF);
	      ptr = TREE_OPERAND (ptr, 0);
	      gcc_assert (DECL_P (ptr));

	      x = TYPE_SIZE_UNIT (TREE_TYPE (new_var));
	      args = tree_cons (NULL, x, NULL);
	      x = built_in_decls[BUILT_IN_ALLOCA];
	      x = build_function_call_expr (x, args);
	      x = fold_convert (TREE_TYPE (ptr), x);
	      x = build2 (MODIFY_EXPR, void_type_node, ptr, x);
	      gimplify_and_add (x, ilist);
	    }
	  else if (is_reference (var))
	    {
	      /* For references that are being privatized for Fortran,
		 allocate new backing storage for the new pointer
		 variable.  This allows us to avoid changing all the
		 code that expects a pointer to something that expects
		 a direct variable.  Note that this doesn't apply to
		 C++, since reference types are disallowed in data
		 sharing clauses there, except for NRV optimized
		 return values.  */
	      if (pass == 0)
		continue;

	      x = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (new_var)));
	      if (TREE_CONSTANT (x))
		{
		  const char *name = NULL;
		  if (DECL_NAME (var))
		    name = IDENTIFIER_POINTER (DECL_NAME (new_var));

		  x = create_tmp_var_raw (TREE_TYPE (TREE_TYPE (new_var)),
					  name);
		  gimple_add_tmp_var (x);
		  x = build_fold_addr_expr_with_type (x, TREE_TYPE (new_var));
		}
	      else
		{
		  args = tree_cons (NULL, x, NULL);
		  x = built_in_decls[BUILT_IN_ALLOCA];
		  x = build_function_call_expr (x, args);
		  x = fold_convert (TREE_TYPE (new_var), x);
		}

	      x = build2 (MODIFY_EXPR, void_type_node, new_var, x);
	      gimplify_and_add (x, ilist);

	      new_var = build_fold_indirect_ref (new_var);
	    }
	  else if (c_kind == OMP_CLAUSE_REDUCTION
		   && OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
	    {
	      if (pass == 0)
		continue;
	    }
	  else if (pass != 0)
	    continue;

	  switch (OMP_CLAUSE_CODE (c))
	    {
	    case OMP_CLAUSE_SHARED:
	      /* Shared global vars are just accessed directly.  */
	      if (is_global_var (new_var))
		break;
	      /* Set up the DECL_VALUE_EXPR for shared variables now.  This
		 needs to be delayed until after fixup_child_record_type so
		 that we get the correct type during the dereference.  */
	      by_ref = use_pointer_for_field (var, true);
	      x = build_receiver_ref (var, by_ref, ctx);
	      SET_DECL_VALUE_EXPR (new_var, x);
	      DECL_HAS_VALUE_EXPR_P (new_var) = 1;

	      /* ??? If VAR is not passed by reference, and the variable
		 hasn't been initialized yet, then we'll get a warning for
		 the store into the omp_data_s structure.  Ideally, we'd be
		 able to notice this and not store anything at all, but 
		 we're generating code too early.  Suppress the warning.  */
	      if (!by_ref)
		TREE_NO_WARNING (var) = 1;
	      break;

	    case OMP_CLAUSE_LASTPRIVATE:
	      if (OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE (c))
		break;
	      /* FALLTHRU */

	    case OMP_CLAUSE_PRIVATE:
	      x = lang_hooks.decls.omp_clause_default_ctor (c, new_var);
	      if (x)
		gimplify_and_add (x, ilist);
	      /* FALLTHRU */

	    do_dtor:
	      x = lang_hooks.decls.omp_clause_dtor (c, new_var);
	      if (x)
		{
		  dtor = x;
		  gimplify_stmt (&dtor);
		  tsi_link_before (&diter, dtor, TSI_SAME_STMT);
		}
	      break;

	    case OMP_CLAUSE_FIRSTPRIVATE:
	      x = build_outer_var_ref (var, ctx);
	      x = lang_hooks.decls.omp_clause_copy_ctor (c, new_var, x);
	      gimplify_and_add (x, ilist);
	      goto do_dtor;
	      break;

	    case OMP_CLAUSE_COPYIN:
	      by_ref = use_pointer_for_field (var, false);
	      x = build_receiver_ref (var, by_ref, ctx);
	      x = lang_hooks.decls.omp_clause_assign_op (c, new_var, x);
	      append_to_statement_list (x, &copyin_seq);
	      copyin_by_ref |= by_ref;
	      break;

	    case OMP_CLAUSE_REDUCTION:
	      if (OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
		{
		  gimplify_and_add (OMP_CLAUSE_REDUCTION_INIT (c), ilist);
		  OMP_CLAUSE_REDUCTION_INIT (c) = NULL;
		}
	      else
		{
		  x = omp_reduction_init (c, TREE_TYPE (new_var));
		  gcc_assert (TREE_CODE (TREE_TYPE (new_var)) != ARRAY_TYPE);
		  x = build2 (MODIFY_EXPR, void_type_node, new_var, x);
		  gimplify_and_add (x, ilist);
		}
	      break;

	    default:
	      gcc_unreachable ();
	    }
	}
    }

  /* The copyin sequence is not to be executed by the main thread, since
     that would result in self-copies.  Perhaps not visible to scalars,
     but it certainly is to C++ operator=.  */
  if (copyin_seq)
    {
      x = built_in_decls[BUILT_IN_OMP_GET_THREAD_NUM];
      x = build_function_call_expr (x, NULL);
      x = build2 (NE_EXPR, boolean_type_node, x,
		  build_int_cst (TREE_TYPE (x), 0));
      x = build3 (COND_EXPR, void_type_node, x, copyin_seq, NULL);
      gimplify_and_add (x, ilist);
    }

  /* If any copyin variable is passed by reference, we must ensure the
     master thread doesn't modify it before it is copied over in all
     threads.  Similarly for variables in both firstprivate and
     lastprivate clauses we need to ensure the lastprivate copying
     happens after firstprivate copying in all threads.  */
  if (copyin_by_ref || lastprivate_firstprivate)
    build_omp_barrier (ilist);
}


/* Generate code to implement the LASTPRIVATE clauses.  This is used for
   both parallel and workshare constructs.  PREDICATE may be NULL if it's
   always true.   */

static void
lower_lastprivate_clauses (tree clauses, tree predicate, tree *stmt_list,
			    omp_context *ctx)
{
  tree sub_list, x, c;

  /* Early exit if there are no lastprivate clauses.  */
  clauses = find_omp_clause (clauses, OMP_CLAUSE_LASTPRIVATE);
  if (clauses == NULL)
    {
      /* If this was a workshare clause, see if it had been combined
	 with its parallel.  In that case, look for the clauses on the
	 parallel statement itself.  */
      if (is_parallel_ctx (ctx))
	return;

      ctx = ctx->outer;
      if (ctx == NULL || !is_parallel_ctx (ctx))
	return;

      clauses = find_omp_clause (OMP_PARALLEL_CLAUSES (ctx->stmt),
				 OMP_CLAUSE_LASTPRIVATE);
      if (clauses == NULL)
	return;
    }

  sub_list = alloc_stmt_list ();

  for (c = clauses; c ; c = OMP_CLAUSE_CHAIN (c))
    {
      tree var, new_var;

      if (OMP_CLAUSE_CODE (c) != OMP_CLAUSE_LASTPRIVATE)
	continue;

      var = OMP_CLAUSE_DECL (c);
      new_var = lookup_decl (var, ctx);

      x = build_outer_var_ref (var, ctx);
      if (is_reference (var))
	new_var = build_fold_indirect_ref (new_var);
      x = lang_hooks.decls.omp_clause_assign_op (c, x, new_var);
      append_to_statement_list (x, &sub_list);
    }

  if (predicate)
    x = build3 (COND_EXPR, void_type_node, predicate, sub_list, NULL);
  else
    x = sub_list;

  gimplify_and_add (x, stmt_list);
}


/* Generate code to implement the REDUCTION clauses.  */

static void
lower_reduction_clauses (tree clauses, tree *stmt_list, omp_context *ctx)
{
  tree sub_list = NULL, x, c;
  int count = 0;

  /* First see if there is exactly one reduction clause.  Use OMP_ATOMIC
     update in that case, otherwise use a lock.  */
  for (c = clauses; c && count < 2; c = OMP_CLAUSE_CHAIN (c))
    if (OMP_CLAUSE_CODE (c) == OMP_CLAUSE_REDUCTION)
      {
	if (OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
	  {
	    /* Never use OMP_ATOMIC for array reductions.  */
	    count = -1;
	    break;
	  }
	count++;
      }

  if (count == 0)
    return;

  for (c = clauses; c ; c = OMP_CLAUSE_CHAIN (c))
    {
      tree var, ref, new_var;
      enum tree_code code;

      if (OMP_CLAUSE_CODE (c) != OMP_CLAUSE_REDUCTION)
	continue;

      var = OMP_CLAUSE_DECL (c);
      new_var = lookup_decl (var, ctx);
      if (is_reference (var))
	new_var = build_fold_indirect_ref (new_var);
      ref = build_outer_var_ref (var, ctx);
      code = OMP_CLAUSE_REDUCTION_CODE (c);

      /* reduction(-:var) sums up the partial results, so it acts
	 identically to reduction(+:var).  */
      if (code == MINUS_EXPR)
        code = PLUS_EXPR;

      if (count == 1)
	{
	  tree addr = build_fold_addr_expr (ref);

	  addr = save_expr (addr);
	  ref = build1 (INDIRECT_REF, TREE_TYPE (TREE_TYPE (addr)), addr);
	  x = fold_build2 (code, TREE_TYPE (ref), ref, new_var);
	  x = build2 (OMP_ATOMIC, void_type_node, addr, x);
	  gimplify_and_add (x, stmt_list);
	  return;
	}

      if (OMP_CLAUSE_REDUCTION_PLACEHOLDER (c))
	{
	  tree placeholder = OMP_CLAUSE_REDUCTION_PLACEHOLDER (c);

	  if (is_reference (var))
	    ref = build_fold_addr_expr (ref);
	  SET_DECL_VALUE_EXPR (placeholder, ref);
	  DECL_HAS_VALUE_EXPR_P (placeholder) = 1;
	  gimplify_and_add (OMP_CLAUSE_REDUCTION_MERGE (c), &sub_list);
	  OMP_CLAUSE_REDUCTION_MERGE (c) = NULL;
	  OMP_CLAUSE_REDUCTION_PLACEHOLDER (c) = NULL;
	}
      else
	{
	  x = build2 (code, TREE_TYPE (ref), ref, new_var);
	  ref = build_outer_var_ref (var, ctx);
	  x = build2 (MODIFY_EXPR, void_type_node, ref, x);
	  append_to_statement_list (x, &sub_list);
	}
    }

  x = built_in_decls[BUILT_IN_GOMP_ATOMIC_START];
  x = build_function_call_expr (x, NULL);
  gimplify_and_add (x, stmt_list);

  gimplify_and_add (sub_list, stmt_list);

  x = built_in_decls[BUILT_IN_GOMP_ATOMIC_END];
  x = build_function_call_expr (x, NULL);
  gimplify_and_add (x, stmt_list);
}


/* Generate code to implement the COPYPRIVATE clauses.  */

static void
lower_copyprivate_clauses (tree clauses, tree *slist, tree *rlist,
			    omp_context *ctx)
{
  tree c;

  for (c = clauses; c ; c = OMP_CLAUSE_CHAIN (c))
    {
      tree var, ref, x;
      bool by_ref;

      if (OMP_CLAUSE_CODE (c) != OMP_CLAUSE_COPYPRIVATE)
	continue;

      var = OMP_CLAUSE_DECL (c);
      by_ref = use_pointer_for_field (var, false);

      ref = build_sender_ref (var, ctx);
      x = (ctx->is_nested) ? lookup_decl_in_outer_ctx (var, ctx) : var;
      x = by_ref ? build_fold_addr_expr (x) : x;
      x = build2 (MODIFY_EXPR, void_type_node, ref, x);
      gimplify_and_add (x, slist);

      ref = build_receiver_ref (var, by_ref, ctx);
      if (is_reference (var))
	{
	  ref = build_fold_indirect_ref (ref);
	  var = build_fold_indirect_ref (var);
	}
      x = lang_hooks.decls.omp_clause_assign_op (c, var, ref);
      gimplify_and_add (x, rlist);
    }
}


/* Generate code to implement the clauses, FIRSTPRIVATE, COPYIN, LASTPRIVATE,
   and REDUCTION from the sender (aka parent) side.  */

static void
lower_send_clauses (tree clauses, tree *ilist, tree *olist, omp_context *ctx)
{
  tree c;

  for (c = clauses; c ; c = OMP_CLAUSE_CHAIN (c))
    {
      tree val, ref, x, var;
      bool by_ref, do_in = false, do_out = false;

      switch (OMP_CLAUSE_CODE (c))
	{
	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_COPYIN:
	case OMP_CLAUSE_LASTPRIVATE:
	case OMP_CLAUSE_REDUCTION:
	  break;
	default:
	  continue;
	}

      var = val = OMP_CLAUSE_DECL (c);
      if (ctx->is_nested)
	var = lookup_decl_in_outer_ctx (val, ctx);

      if (OMP_CLAUSE_CODE (c) != OMP_CLAUSE_COPYIN
	  && is_global_var (var))
	continue;
      if (is_variable_sized (val))
	continue;
      by_ref = use_pointer_for_field (val, false);

      switch (OMP_CLAUSE_CODE (c))
	{
	case OMP_CLAUSE_FIRSTPRIVATE:
	case OMP_CLAUSE_COPYIN:
	  do_in = true;
	  break;

	case OMP_CLAUSE_LASTPRIVATE:
	  if (by_ref || is_reference (val))
	    {
	      if (OMP_CLAUSE_LASTPRIVATE_FIRSTPRIVATE (c))
		continue;
	      do_in = true;
	    }
	  else
	    do_out = true;
	  break;

	case OMP_CLAUSE_REDUCTION:
	  do_in = true;
	  do_out = !(by_ref || is_reference (val));
	  break;

	default:
	  gcc_unreachable ();
	}

      if (do_in)
	{
	  ref = build_sender_ref (val, ctx);
	  x = by_ref ? build_fold_addr_expr (var) : var;
	  x = build2 (MODIFY_EXPR, void_type_node, ref, x);
	  gimplify_and_add (x, ilist);
	}

      if (do_out)
	{
	  ref = build_sender_ref (val, ctx);
	  x = build2 (MODIFY_EXPR, void_type_node, var, ref);
	  gimplify_and_add (x, olist);
	}
    }
}

/* Generate code to implement SHARED from the sender (aka parent) side.
   This is trickier, since OMP_PARALLEL_CLAUSES doesn't list things that
   got automatically shared.  */

static void
lower_send_shared_vars (tree *ilist, tree *olist, omp_context *ctx)
{
  tree var, ovar, nvar, f, x;

  if (ctx->record_type == NULL)
    return;

  for (f = TYPE_FIELDS (ctx->record_type); f ; f = TREE_CHAIN (f))
    {
      ovar = DECL_ABSTRACT_ORIGIN (f);
      nvar = maybe_lookup_decl (ovar, ctx);
      if (!nvar || !DECL_HAS_VALUE_EXPR_P (nvar))
	continue;

      var = ovar;

      /* If CTX is a nested parallel directive.  Find the immediately
	 enclosing parallel or workshare construct that contains a
	 mapping for OVAR.  */
      if (ctx->is_nested)
	var = lookup_decl_in_outer_ctx (ovar, ctx);

      if (use_pointer_for_field (ovar, true))
	{
	  x = build_sender_ref (ovar, ctx);
	  var = build_fold_addr_expr (var);
	  x = build2 (MODIFY_EXPR, void_type_node, x, var);
	  gimplify_and_add (x, ilist);
	}
      else
	{
	  x = build_sender_ref (ovar, ctx);
	  x = build2 (MODIFY_EXPR, void_type_node, x, var);
	  gimplify_and_add (x, ilist);

	  x = build_sender_ref (ovar, ctx);
	  x = build2 (MODIFY_EXPR, void_type_node, var, x);
	  gimplify_and_add (x, olist);
	}
    }
}

/* Build the function calls to GOMP_parallel_start etc to actually 
   generate the parallel operation.  REGION is the parallel region
   being expanded.  BB is the block where to insert the code.  WS_ARGS
   will be set if this is a call to a combined parallel+workshare
   construct, it contains the list of additional arguments needed by
   the workshare construct.  */

static void
expand_parallel_call (struct omp_region *region, basic_block bb,
		      tree entry_stmt, tree ws_args)
{
  tree t, args, val, cond, c, list, clauses;
  block_stmt_iterator si;
  int start_ix;

  clauses = OMP_PARALLEL_CLAUSES (entry_stmt);
  push_gimplify_context ();

  /* Determine what flavor of GOMP_parallel_start we will be
     emitting.  */
  start_ix = BUILT_IN_GOMP_PARALLEL_START;
  if (is_combined_parallel (region))
    {
      switch (region->inner->type)
	{
	case OMP_FOR:
	  start_ix = BUILT_IN_GOMP_PARALLEL_LOOP_STATIC_START
		     + region->inner->sched_kind;
	  break;
	case OMP_SECTIONS:
	  start_ix = BUILT_IN_GOMP_PARALLEL_SECTIONS_START;
	  break;
	default:
	  gcc_unreachable ();
	}
    }

  /* By default, the value of NUM_THREADS is zero (selected at run time)
     and there is no conditional.  */
  cond = NULL_TREE;
  val = build_int_cst (unsigned_type_node, 0);

  c = find_omp_clause (clauses, OMP_CLAUSE_IF);
  if (c)
    cond = OMP_CLAUSE_IF_EXPR (c);

  c = find_omp_clause (clauses, OMP_CLAUSE_NUM_THREADS);
  if (c)
    val = OMP_CLAUSE_NUM_THREADS_EXPR (c);

  /* Ensure 'val' is of the correct type.  */
  val = fold_convert (unsigned_type_node, val);

  /* If we found the clause 'if (cond)', build either
     (cond != 0) or (cond ? val : 1u).  */
  if (cond)
    {
      block_stmt_iterator si;

      cond = gimple_boolify (cond);

      if (integer_zerop (val))
	val = build2 (EQ_EXPR, unsigned_type_node, cond,
		      build_int_cst (TREE_TYPE (cond), 0));
      else
	{
	  basic_block cond_bb, then_bb, else_bb;
	  edge e;
	  tree t, then_lab, else_lab, tmp;

	  tmp = create_tmp_var (TREE_TYPE (val), NULL);
	  e = split_block (bb, NULL);
	  cond_bb = e->src;
	  bb = e->dest;
	  remove_edge (e);

	  then_bb = create_empty_bb (cond_bb);
	  else_bb = create_empty_bb (then_bb);
	  then_lab = create_artificial_label ();
	  else_lab = create_artificial_label ();

	  t = build3 (COND_EXPR, void_type_node,
		      cond,
		      build_and_jump (&then_lab),
		      build_and_jump (&else_lab));

	  si = bsi_start (cond_bb);
	  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);

	  si = bsi_start (then_bb);
	  t = build1 (LABEL_EXPR, void_type_node, then_lab);
	  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);
	  t = build2 (MODIFY_EXPR, void_type_node, tmp, val);
	  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);

	  si = bsi_start (else_bb);
	  t = build1 (LABEL_EXPR, void_type_node, else_lab);
	  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);
	  t = build2 (MODIFY_EXPR, void_type_node, tmp, 
	              build_int_cst (unsigned_type_node, 1));
	  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);

	  make_edge (cond_bb, then_bb, EDGE_TRUE_VALUE);
	  make_edge (cond_bb, else_bb, EDGE_FALSE_VALUE);
	  make_edge (then_bb, bb, EDGE_FALLTHRU);
	  make_edge (else_bb, bb, EDGE_FALLTHRU);

	  val = tmp;
	}

      list = NULL_TREE;
      val = get_formal_tmp_var (val, &list);
      si = bsi_start (bb);
      bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);
    }

  list = NULL_TREE;
  args = tree_cons (NULL, val, NULL);
  t = OMP_PARALLEL_DATA_ARG (entry_stmt);
  if (t == NULL)
    t = null_pointer_node;
  else
    t = build_fold_addr_expr (t);
  args = tree_cons (NULL, t, args);
  t = build_fold_addr_expr (OMP_PARALLEL_FN (entry_stmt));
  args = tree_cons (NULL, t, args);

  if (ws_args)
    args = chainon (args, ws_args);

  t = built_in_decls[start_ix];
  t = build_function_call_expr (t, args);
  gimplify_and_add (t, &list);

  t = OMP_PARALLEL_DATA_ARG (entry_stmt);
  if (t == NULL)
    t = null_pointer_node;
  else
    t = build_fold_addr_expr (t);
  args = tree_cons (NULL, t, NULL);
  t = build_function_call_expr (OMP_PARALLEL_FN (entry_stmt), args);
  gimplify_and_add (t, &list);

  t = built_in_decls[BUILT_IN_GOMP_PARALLEL_END];
  t = build_function_call_expr (t, NULL);
  gimplify_and_add (t, &list);

  si = bsi_last (bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  pop_gimplify_context (NULL_TREE);
}


/* If exceptions are enabled, wrap *STMT_P in a MUST_NOT_THROW catch
   handler.  This prevents programs from violating the structured
   block semantics with throws.  */

static void
maybe_catch_exception (tree *stmt_p)
{
  tree f, t;

  if (!flag_exceptions)
    return;

  if (lang_protect_cleanup_actions)
    t = lang_protect_cleanup_actions ();
  else
    {
      t = built_in_decls[BUILT_IN_TRAP];
      t = build_function_call_expr (t, NULL);
    }
  f = build2 (EH_FILTER_EXPR, void_type_node, NULL, NULL);
  EH_FILTER_MUST_NOT_THROW (f) = 1;
  gimplify_and_add (t, &EH_FILTER_FAILURE (f));
  
  t = build2 (TRY_CATCH_EXPR, void_type_node, *stmt_p, NULL);
  append_to_statement_list (f, &TREE_OPERAND (t, 1));

  *stmt_p = NULL;
  append_to_statement_list (t, stmt_p);
}

/* Chain all the DECLs in LIST by their TREE_CHAIN fields.  */

static tree
list2chain (tree list)
{
  tree t;

  for (t = list; t; t = TREE_CHAIN (t))
    {
      tree var = TREE_VALUE (t);
      if (TREE_CHAIN (t))
	TREE_CHAIN (var) = TREE_VALUE (TREE_CHAIN (t));
      else
	TREE_CHAIN (var) = NULL_TREE;
    }

  return list ? TREE_VALUE (list) : NULL_TREE;
}


/* Remove barriers in REGION->EXIT's block.  Note that this is only
   valid for OMP_PARALLEL regions.  Since the end of a parallel region
   is an implicit barrier, any workshare inside the OMP_PARALLEL that
   left a barrier at the end of the OMP_PARALLEL region can now be
   removed.  */

static void
remove_exit_barrier (struct omp_region *region)
{
  block_stmt_iterator si;
  basic_block exit_bb;
  edge_iterator ei;
  edge e;
  tree t;

  exit_bb = region->exit;

  /* If the parallel region doesn't return, we don't have REGION->EXIT
     block at all.  */
  if (! exit_bb)
    return;

  /* The last insn in the block will be the parallel's OMP_RETURN.  The
     workshare's OMP_RETURN will be in a preceding block.  The kinds of
     statements that can appear in between are extremely limited -- no
     memory operations at all.  Here, we allow nothing at all, so the
     only thing we allow to precede this OMP_RETURN is a label.  */
  si = bsi_last (exit_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_RETURN);
  bsi_prev (&si);
  if (!bsi_end_p (si) && TREE_CODE (bsi_stmt (si)) != LABEL_EXPR)
    return;

  FOR_EACH_EDGE (e, ei, exit_bb->preds)
    {
      si = bsi_last (e->src);
      if (bsi_end_p (si))
	continue;
      t = bsi_stmt (si);
      if (TREE_CODE (t) == OMP_RETURN)
	OMP_RETURN_NOWAIT (t) = 1;
    }
}

static void
remove_exit_barriers (struct omp_region *region)
{
  if (region->type == OMP_PARALLEL)
    remove_exit_barrier (region);

  if (region->inner)
    {
      region = region->inner;
      remove_exit_barriers (region);
      while (region->next)
	{
	  region = region->next;
	  remove_exit_barriers (region);
	}
    }
}

/* Expand the OpenMP parallel directive starting at REGION.  */

static void
expand_omp_parallel (struct omp_region *region)
{
  basic_block entry_bb, exit_bb, new_bb;
  struct function *child_cfun, *saved_cfun;
  tree child_fn, block, t, ws_args;
  block_stmt_iterator si;
  tree entry_stmt;
  edge e;
  bool do_cleanup_cfg = false;

  entry_stmt = last_stmt (region->entry);
  child_fn = OMP_PARALLEL_FN (entry_stmt);
  child_cfun = DECL_STRUCT_FUNCTION (child_fn);
  saved_cfun = cfun;

  entry_bb = region->entry;
  exit_bb = region->exit;

  if (is_combined_parallel (region))
    ws_args = region->ws_args;
  else
    ws_args = NULL_TREE;

  if (child_cfun->cfg)
    {
      /* Due to inlining, it may happen that we have already outlined
	 the region, in which case all we need to do is make the
	 sub-graph unreachable and emit the parallel call.  */
      edge entry_succ_e, exit_succ_e;
      block_stmt_iterator si;

      entry_succ_e = single_succ_edge (entry_bb);

      si = bsi_last (entry_bb);
      gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_PARALLEL);
      bsi_remove (&si, true);

      new_bb = entry_bb;
      remove_edge (entry_succ_e);
      if (exit_bb)
	{
	  exit_succ_e = single_succ_edge (exit_bb);
	  make_edge (new_bb, exit_succ_e->dest, EDGE_FALLTHRU);
	}
      do_cleanup_cfg = true;
    }
  else
    {
      /* If the parallel region needs data sent from the parent
	 function, then the very first statement (except possible
	 tree profile counter updates) of the parallel body
	 is a copy assignment .OMP_DATA_I = &.OMP_DATA_O.  Since
	 &.OMP_DATA_O is passed as an argument to the child function,
	 we need to replace it with the argument as seen by the child
	 function.

	 In most cases, this will end up being the identity assignment
	 .OMP_DATA_I = .OMP_DATA_I.  However, if the parallel body had
	 a function call that has been inlined, the original PARM_DECL
	 .OMP_DATA_I may have been converted into a different local
	 variable.  In which case, we need to keep the assignment.  */
      if (OMP_PARALLEL_DATA_ARG (entry_stmt))
	{
	  basic_block entry_succ_bb = single_succ (entry_bb);
	  block_stmt_iterator si;

	  for (si = bsi_start (entry_succ_bb); ; bsi_next (&si))
	    {
	      tree stmt, arg;

	      gcc_assert (!bsi_end_p (si));
	      stmt = bsi_stmt (si);
	      if (TREE_CODE (stmt) != MODIFY_EXPR)
		continue;

	      arg = TREE_OPERAND (stmt, 1);
	      STRIP_NOPS (arg);
	      if (TREE_CODE (arg) == ADDR_EXPR
		  && TREE_OPERAND (arg, 0)
		     == OMP_PARALLEL_DATA_ARG (entry_stmt))
		{
		  if (TREE_OPERAND (stmt, 0) == DECL_ARGUMENTS (child_fn))
		    bsi_remove (&si, true);
		  else
		    TREE_OPERAND (stmt, 1) = DECL_ARGUMENTS (child_fn);
		  break;
		}
	    }
	}

      /* Declare local variables needed in CHILD_CFUN.  */
      block = DECL_INITIAL (child_fn);
      BLOCK_VARS (block) = list2chain (child_cfun->unexpanded_var_list);
      DECL_SAVED_TREE (child_fn) = single_succ (entry_bb)->stmt_list;

      /* Reset DECL_CONTEXT on locals and function arguments.  */
      for (t = BLOCK_VARS (block); t; t = TREE_CHAIN (t))
	DECL_CONTEXT (t) = child_fn;

      for (t = DECL_ARGUMENTS (child_fn); t; t = TREE_CHAIN (t))
	DECL_CONTEXT (t) = child_fn;

      /* Split ENTRY_BB at OMP_PARALLEL so that it can be moved to the
	 child function.  */
      si = bsi_last (entry_bb);
      t = bsi_stmt (si);
      gcc_assert (t && TREE_CODE (t) == OMP_PARALLEL);
      bsi_remove (&si, true);
      e = split_block (entry_bb, t);
      entry_bb = e->dest;
      single_succ_edge (entry_bb)->flags = EDGE_FALLTHRU;

      /* Move the parallel region into CHILD_CFUN.  We need to reset
	 dominance information because the expansion of the inner
	 regions has invalidated it.  */
      free_dominance_info (CDI_DOMINATORS);
      new_bb = move_sese_region_to_fn (child_cfun, entry_bb, exit_bb);
      if (exit_bb)
	single_succ_edge (new_bb)->flags = EDGE_FALLTHRU;
      cgraph_add_new_function (child_fn);

      /* Convert OMP_RETURN into a RETURN_EXPR.  */
      if (exit_bb)
	{
	  si = bsi_last (exit_bb);
	  gcc_assert (!bsi_end_p (si)
		      && TREE_CODE (bsi_stmt (si)) == OMP_RETURN);
	  t = build1 (RETURN_EXPR, void_type_node, NULL);
	  bsi_insert_after (&si, t, BSI_SAME_STMT);
	  bsi_remove (&si, true);
	}
    }

  /* Emit a library call to launch the children threads.  */
  expand_parallel_call (region, new_bb, entry_stmt, ws_args);

  if (do_cleanup_cfg)
    {
      /* Clean up the unreachable sub-graph we created above.  */
      free_dominance_info (CDI_DOMINATORS);
      free_dominance_info (CDI_POST_DOMINATORS);
      cleanup_tree_cfg ();
    }
}


/* A subroutine of expand_omp_for.  Generate code for a parallel
   loop with any schedule.  Given parameters:

	for (V = N1; V cond N2; V += STEP) BODY;

   where COND is "<" or ">", we generate pseudocode

	more = GOMP_loop_foo_start (N1, N2, STEP, CHUNK, &istart0, &iend0);
	if (more) goto L0; else goto L3;
    L0:
	V = istart0;
	iend = iend0;
    L1:
	BODY;
	V += STEP;
	if (V cond iend) goto L1; else goto L2;
    L2:
	if (GOMP_loop_foo_next (&istart0, &iend0)) goto L0; else goto L3;
    L3:

    If this is a combined omp parallel loop, instead of the call to
    GOMP_loop_foo_start, we emit 'goto L3'.  */

static void
expand_omp_for_generic (struct omp_region *region,
			struct omp_for_data *fd,
			enum built_in_function start_fn,
			enum built_in_function next_fn)
{
  tree l0, l1, l2 = NULL, l3 = NULL;
  tree type, istart0, iend0, iend;
  tree t, args, list;
  basic_block entry_bb, cont_bb, exit_bb, l0_bb, l1_bb;
  basic_block l2_bb = NULL, l3_bb = NULL;
  block_stmt_iterator si;
  bool in_combined_parallel = is_combined_parallel (region);

  type = TREE_TYPE (fd->v);

  istart0 = create_tmp_var (long_integer_type_node, ".istart0");
  iend0 = create_tmp_var (long_integer_type_node, ".iend0");
  iend = create_tmp_var (type, NULL);
  TREE_ADDRESSABLE (istart0) = 1;
  TREE_ADDRESSABLE (iend0) = 1;

  gcc_assert ((region->cont != NULL) ^ (region->exit == NULL));

  entry_bb = region->entry;
  l0_bb = create_empty_bb (entry_bb);
  l1_bb = single_succ (entry_bb);

  l0 = tree_block_label (l0_bb);
  l1 = tree_block_label (l1_bb);

  cont_bb = region->cont;
  exit_bb = region->exit;
  if (cont_bb)
    {
      l2_bb = create_empty_bb (cont_bb);
      l3_bb = single_succ (cont_bb);

      l2 = tree_block_label (l2_bb);
      l3 = tree_block_label (l3_bb);
    }

  si = bsi_last (entry_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_FOR);
  if (!in_combined_parallel)
    {
      /* If this is not a combined parallel loop, emit a call to
	 GOMP_loop_foo_start in ENTRY_BB.  */
      list = alloc_stmt_list ();
      t = build_fold_addr_expr (iend0);
      args = tree_cons (NULL, t, NULL);
      t = build_fold_addr_expr (istart0);
      args = tree_cons (NULL, t, args);
      if (fd->chunk_size)
	{
	  t = fold_convert (long_integer_type_node, fd->chunk_size);
	  args = tree_cons (NULL, t, args);
	}
      t = fold_convert (long_integer_type_node, fd->step);
      args = tree_cons (NULL, t, args);
      t = fold_convert (long_integer_type_node, fd->n2);
      args = tree_cons (NULL, t, args);
      t = fold_convert (long_integer_type_node, fd->n1);
      args = tree_cons (NULL, t, args);
      t = build_function_call_expr (built_in_decls[start_fn], args);
      t = get_formal_tmp_var (t, &list);
      if (cont_bb)
	{
	  t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&l0),
		      build_and_jump (&l3));
	  append_to_statement_list (t, &list);
	}
      bsi_insert_after (&si, list, BSI_SAME_STMT);
    }
  bsi_remove (&si, true);

  /* Iteration setup for sequential loop goes in L0_BB.  */
  list = alloc_stmt_list ();
  t = fold_convert (type, istart0);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);

  t = fold_convert (type, iend0);
  t = build2 (MODIFY_EXPR, void_type_node, iend, t);
  gimplify_and_add (t, &list);

  si = bsi_start (l0_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* Handle the rare case where BODY doesn't ever return.  */
  if (cont_bb == NULL)
    {
      remove_edge (single_succ_edge (entry_bb));
      make_edge (entry_bb, l0_bb, EDGE_FALLTHRU);
      make_edge (l0_bb, l1_bb, EDGE_FALLTHRU);
      return;
    }

  /* Code to control the increment and predicate for the sequential
     loop goes in the first half of EXIT_BB (we split EXIT_BB so
     that we can inherit all the edges going out of the loop
     body).  */
  list = alloc_stmt_list ();

  t = build2 (PLUS_EXPR, type, fd->v, fd->step);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);
  
  t = build2 (fd->cond_code, boolean_type_node, fd->v, iend);
  t = get_formal_tmp_var (t, &list);
  t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&l1),
	      build_and_jump (&l2));
  append_to_statement_list (t, &list);

  si = bsi_last (cont_bb);
  bsi_insert_after (&si, list, BSI_SAME_STMT);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_CONTINUE);
  bsi_remove (&si, true);

  /* Emit code to get the next parallel iteration in L2_BB.  */
  list = alloc_stmt_list ();

  t = build_fold_addr_expr (iend0);
  args = tree_cons (NULL, t, NULL);
  t = build_fold_addr_expr (istart0);
  args = tree_cons (NULL, t, args);
  t = build_function_call_expr (built_in_decls[next_fn], args);
  t = get_formal_tmp_var (t, &list);
  t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&l0),
	      build_and_jump (&l3));
  append_to_statement_list (t, &list);
  
  si = bsi_start (l2_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* Add the loop cleanup function.  */
  si = bsi_last (exit_bb);
  if (OMP_RETURN_NOWAIT (bsi_stmt (si)))
    t = built_in_decls[BUILT_IN_GOMP_LOOP_END_NOWAIT];
  else
    t = built_in_decls[BUILT_IN_GOMP_LOOP_END];
  t = build_function_call_expr (t, NULL);
  bsi_insert_after (&si, t, BSI_SAME_STMT);
  bsi_remove (&si, true);

  /* Connect the new blocks.  */
  remove_edge (single_succ_edge (entry_bb));
  if (in_combined_parallel)
    make_edge (entry_bb, l2_bb, EDGE_FALLTHRU);
  else
    {
      make_edge (entry_bb, l0_bb, EDGE_TRUE_VALUE);
      make_edge (entry_bb, l3_bb, EDGE_FALSE_VALUE);
    }

  make_edge (l0_bb, l1_bb, EDGE_FALLTHRU);

  remove_edge (single_succ_edge (cont_bb));
  make_edge (cont_bb, l1_bb, EDGE_TRUE_VALUE);
  make_edge (cont_bb, l2_bb, EDGE_FALSE_VALUE);

  make_edge (l2_bb, l0_bb, EDGE_TRUE_VALUE);
  make_edge (l2_bb, l3_bb, EDGE_FALSE_VALUE);
}


/* A subroutine of expand_omp_for.  Generate code for a parallel
   loop with static schedule and no specified chunk size.  Given
   parameters:

	for (V = N1; V cond N2; V += STEP) BODY;

   where COND is "<" or ">", we generate pseudocode

	if (cond is <)
	  adj = STEP - 1;
	else
	  adj = STEP + 1;
	n = (adj + N2 - N1) / STEP;
	q = n / nthreads;
	q += (q * nthreads != n);
	s0 = q * threadid;
	e0 = min(s0 + q, n);
	if (s0 >= e0) goto L2; else goto L0;
    L0:
	V = s0 * STEP + N1;
	e = e0 * STEP + N1;
    L1:
	BODY;
	V += STEP;
	if (V cond e) goto L1;
    L2:
*/

static void
expand_omp_for_static_nochunk (struct omp_region *region,
			       struct omp_for_data *fd)
{
  tree l0, l1, l2, n, q, s0, e0, e, t, nthreads, threadid;
  tree type, list;
  basic_block entry_bb, exit_bb, seq_start_bb, body_bb, cont_bb;
  basic_block fin_bb;
  block_stmt_iterator si;

  type = TREE_TYPE (fd->v);

  entry_bb = region->entry;
  seq_start_bb = create_empty_bb (entry_bb);
  body_bb = single_succ (entry_bb);
  cont_bb = region->cont;
  fin_bb = single_succ (cont_bb);
  exit_bb = region->exit;

  l0 = tree_block_label (seq_start_bb);
  l1 = tree_block_label (body_bb);
  l2 = tree_block_label (fin_bb);

  /* Iteration space partitioning goes in ENTRY_BB.  */
  list = alloc_stmt_list ();

  t = built_in_decls[BUILT_IN_OMP_GET_NUM_THREADS];
  t = build_function_call_expr (t, NULL);
  t = fold_convert (type, t);
  nthreads = get_formal_tmp_var (t, &list);
  
  t = built_in_decls[BUILT_IN_OMP_GET_THREAD_NUM];
  t = build_function_call_expr (t, NULL);
  t = fold_convert (type, t);
  threadid = get_formal_tmp_var (t, &list);

  fd->n1 = fold_convert (type, fd->n1);
  if (!is_gimple_val (fd->n1))
    fd->n1 = get_formal_tmp_var (fd->n1, &list);

  fd->n2 = fold_convert (type, fd->n2);
  if (!is_gimple_val (fd->n2))
    fd->n2 = get_formal_tmp_var (fd->n2, &list);

  fd->step = fold_convert (type, fd->step);
  if (!is_gimple_val (fd->step))
    fd->step = get_formal_tmp_var (fd->step, &list);

  t = build_int_cst (type, (fd->cond_code == LT_EXPR ? -1 : 1));
  t = fold_build2 (PLUS_EXPR, type, fd->step, t);
  t = fold_build2 (PLUS_EXPR, type, t, fd->n2);
  t = fold_build2 (MINUS_EXPR, type, t, fd->n1);
  t = fold_build2 (TRUNC_DIV_EXPR, type, t, fd->step);
  t = fold_convert (type, t);
  if (is_gimple_val (t))
    n = t;
  else
    n = get_formal_tmp_var (t, &list);

  t = build2 (TRUNC_DIV_EXPR, type, n, nthreads);
  q = get_formal_tmp_var (t, &list);

  t = build2 (MULT_EXPR, type, q, nthreads);
  t = build2 (NE_EXPR, type, t, n);
  t = build2 (PLUS_EXPR, type, q, t);
  q = get_formal_tmp_var (t, &list);

  t = build2 (MULT_EXPR, type, q, threadid);
  s0 = get_formal_tmp_var (t, &list);

  t = build2 (PLUS_EXPR, type, s0, q);
  t = build2 (MIN_EXPR, type, t, n);
  e0 = get_formal_tmp_var (t, &list);

  t = build2 (GE_EXPR, boolean_type_node, s0, e0);
  t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&l2),
	      build_and_jump (&l0));
  append_to_statement_list (t, &list);

  si = bsi_last (entry_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_FOR);
  bsi_insert_after (&si, list, BSI_SAME_STMT);
  bsi_remove (&si, true);

  /* Setup code for sequential iteration goes in SEQ_START_BB.  */
  list = alloc_stmt_list ();

  t = fold_convert (type, s0);
  t = build2 (MULT_EXPR, type, t, fd->step);
  t = build2 (PLUS_EXPR, type, t, fd->n1);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);

  t = fold_convert (type, e0);
  t = build2 (MULT_EXPR, type, t, fd->step);
  t = build2 (PLUS_EXPR, type, t, fd->n1);
  e = get_formal_tmp_var (t, &list);

  si = bsi_start (seq_start_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* The code controlling the sequential loop replaces the OMP_CONTINUE.  */
  list = alloc_stmt_list ();

  t = build2 (PLUS_EXPR, type, fd->v, fd->step);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);

  t = build2 (fd->cond_code, boolean_type_node, fd->v, e);
  t = get_formal_tmp_var (t, &list);
  t = build3 (COND_EXPR, void_type_node, t, build_and_jump (&l1),
	      build_and_jump (&l2));
  append_to_statement_list (t, &list);

  si = bsi_last (cont_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_CONTINUE);
  bsi_insert_after (&si, list, BSI_SAME_STMT);
  bsi_remove (&si, true);

  /* Replace the OMP_RETURN with a barrier, or nothing.  */
  si = bsi_last (exit_bb);
  if (!OMP_RETURN_NOWAIT (bsi_stmt (si)))
    {
      list = alloc_stmt_list ();
      build_omp_barrier (&list);
      bsi_insert_after (&si, list, BSI_SAME_STMT);
    }
  bsi_remove (&si, true);

  /* Connect all the blocks.  */
  make_edge (seq_start_bb, body_bb, EDGE_FALLTHRU);

  remove_edge (single_succ_edge (entry_bb));
  make_edge (entry_bb, fin_bb, EDGE_TRUE_VALUE);
  make_edge (entry_bb, seq_start_bb, EDGE_FALSE_VALUE);

  make_edge (cont_bb, body_bb, EDGE_TRUE_VALUE);
  find_edge (cont_bb, fin_bb)->flags = EDGE_FALSE_VALUE;
}


/* A subroutine of expand_omp_for.  Generate code for a parallel
   loop with static schedule and a specified chunk size.  Given
   parameters:

	for (V = N1; V cond N2; V += STEP) BODY;

   where COND is "<" or ">", we generate pseudocode

	if (cond is <)
	  adj = STEP - 1;
	else
	  adj = STEP + 1;
	n = (adj + N2 - N1) / STEP;
	trip = 0;
    L0:
	s0 = (trip * nthreads + threadid) * CHUNK;
	e0 = min(s0 + CHUNK, n);
	if (s0 < n) goto L1; else goto L4;
    L1:
	V = s0 * STEP + N1;
	e = e0 * STEP + N1;
    L2:
	BODY;
	V += STEP;
	if (V cond e) goto L2; else goto L3;
    L3:
	trip += 1;
	goto L0;
    L4:
*/

static void
expand_omp_for_static_chunk (struct omp_region *region, struct omp_for_data *fd)
{
  tree l0, l1, l2, l3, l4, n, s0, e0, e, t;
  tree trip, nthreads, threadid;
  tree type;
  basic_block entry_bb, exit_bb, body_bb, seq_start_bb, iter_part_bb;
  basic_block trip_update_bb, cont_bb, fin_bb;
  tree list;
  block_stmt_iterator si;

  type = TREE_TYPE (fd->v);

  entry_bb = region->entry;
  iter_part_bb = create_empty_bb (entry_bb);
  seq_start_bb = create_empty_bb (iter_part_bb);
  body_bb = single_succ (entry_bb);
  cont_bb = region->cont;
  trip_update_bb = create_empty_bb (cont_bb);
  fin_bb = single_succ (cont_bb);
  exit_bb = region->exit;

  l0 = tree_block_label (iter_part_bb);
  l1 = tree_block_label (seq_start_bb);
  l2 = tree_block_label (body_bb);
  l3 = tree_block_label (trip_update_bb);
  l4 = tree_block_label (fin_bb);

  /* Trip and adjustment setup goes in ENTRY_BB.  */
  list = alloc_stmt_list ();

  t = built_in_decls[BUILT_IN_OMP_GET_NUM_THREADS];
  t = build_function_call_expr (t, NULL);
  t = fold_convert (type, t);
  nthreads = get_formal_tmp_var (t, &list);
  
  t = built_in_decls[BUILT_IN_OMP_GET_THREAD_NUM];
  t = build_function_call_expr (t, NULL);
  t = fold_convert (type, t);
  threadid = get_formal_tmp_var (t, &list);

  fd->n1 = fold_convert (type, fd->n1);
  if (!is_gimple_val (fd->n1))
    fd->n1 = get_formal_tmp_var (fd->n1, &list);

  fd->n2 = fold_convert (type, fd->n2);
  if (!is_gimple_val (fd->n2))
    fd->n2 = get_formal_tmp_var (fd->n2, &list);

  fd->step = fold_convert (type, fd->step);
  if (!is_gimple_val (fd->step))
    fd->step = get_formal_tmp_var (fd->step, &list);

  fd->chunk_size = fold_convert (type, fd->chunk_size);
  if (!is_gimple_val (fd->chunk_size))
    fd->chunk_size = get_formal_tmp_var (fd->chunk_size, &list);

  t = build_int_cst (type, (fd->cond_code == LT_EXPR ? -1 : 1));
  t = fold_build2 (PLUS_EXPR, type, fd->step, t);
  t = fold_build2 (PLUS_EXPR, type, t, fd->n2);
  t = fold_build2 (MINUS_EXPR, type, t, fd->n1);
  t = fold_build2 (TRUNC_DIV_EXPR, type, t, fd->step);
  t = fold_convert (type, t);
  if (is_gimple_val (t))
    n = t;
  else
    n = get_formal_tmp_var (t, &list);

  t = build_int_cst (type, 0);
  trip = get_initialized_tmp_var (t, &list, NULL);

  si = bsi_last (entry_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_FOR);
  bsi_insert_after (&si, list, BSI_SAME_STMT);
  bsi_remove (&si, true);

  /* Iteration space partitioning goes in ITER_PART_BB.  */
  list = alloc_stmt_list ();

  t = build2 (MULT_EXPR, type, trip, nthreads);
  t = build2 (PLUS_EXPR, type, t, threadid);
  t = build2 (MULT_EXPR, type, t, fd->chunk_size);
  s0 = get_formal_tmp_var (t, &list);

  t = build2 (PLUS_EXPR, type, s0, fd->chunk_size);
  t = build2 (MIN_EXPR, type, t, n);
  e0 = get_formal_tmp_var (t, &list);

  t = build2 (LT_EXPR, boolean_type_node, s0, n);
  t = build3 (COND_EXPR, void_type_node, t,
	      build_and_jump (&l1), build_and_jump (&l4));
  append_to_statement_list (t, &list);

  si = bsi_start (iter_part_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* Setup code for sequential iteration goes in SEQ_START_BB.  */
  list = alloc_stmt_list ();

  t = fold_convert (type, s0);
  t = build2 (MULT_EXPR, type, t, fd->step);
  t = build2 (PLUS_EXPR, type, t, fd->n1);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);

  t = fold_convert (type, e0);
  t = build2 (MULT_EXPR, type, t, fd->step);
  t = build2 (PLUS_EXPR, type, t, fd->n1);
  e = get_formal_tmp_var (t, &list);

  si = bsi_start (seq_start_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* The code controlling the sequential loop goes in CONT_BB,
     replacing the OMP_CONTINUE.  */
  list = alloc_stmt_list ();

  t = build2 (PLUS_EXPR, type, fd->v, fd->step);
  t = build2 (MODIFY_EXPR, void_type_node, fd->v, t);
  gimplify_and_add (t, &list);

  t = build2 (fd->cond_code, boolean_type_node, fd->v, e);
  t = get_formal_tmp_var (t, &list);
  t = build3 (COND_EXPR, void_type_node, t,
	      build_and_jump (&l2), build_and_jump (&l3));
  append_to_statement_list (t, &list);
  
  si = bsi_last (cont_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_CONTINUE);
  bsi_insert_after (&si, list, BSI_SAME_STMT);
  bsi_remove (&si, true);

  /* Trip update code goes into TRIP_UPDATE_BB.  */
  list = alloc_stmt_list ();

  t = build_int_cst (type, 1);
  t = build2 (PLUS_EXPR, type, trip, t);
  t = build2 (MODIFY_EXPR, void_type_node, trip, t);
  gimplify_and_add (t, &list);

  si = bsi_start (trip_update_bb);
  bsi_insert_after (&si, list, BSI_CONTINUE_LINKING);

  /* Replace the OMP_RETURN with a barrier, or nothing.  */
  si = bsi_last (exit_bb);
  if (!OMP_RETURN_NOWAIT (bsi_stmt (si)))
    {
      list = alloc_stmt_list ();
      build_omp_barrier (&list);
      bsi_insert_after (&si, list, BSI_SAME_STMT);
    }
  bsi_remove (&si, true);

  /* Connect the new blocks.  */
  remove_edge (single_succ_edge (entry_bb));
  make_edge (entry_bb, iter_part_bb, EDGE_FALLTHRU);

  make_edge (iter_part_bb, seq_start_bb, EDGE_TRUE_VALUE);
  make_edge (iter_part_bb, fin_bb, EDGE_FALSE_VALUE);

  make_edge (seq_start_bb, body_bb, EDGE_FALLTHRU);

  remove_edge (single_succ_edge (cont_bb));
  make_edge (cont_bb, body_bb, EDGE_TRUE_VALUE);
  make_edge (cont_bb, trip_update_bb, EDGE_FALSE_VALUE);

  make_edge (trip_update_bb, iter_part_bb, EDGE_FALLTHRU);
}


/* Expand the OpenMP loop defined by REGION.  */

static void
expand_omp_for (struct omp_region *region)
{
  struct omp_for_data fd;

  push_gimplify_context ();

  extract_omp_for_data (last_stmt (region->entry), &fd);
  region->sched_kind = fd.sched_kind;

  if (fd.sched_kind == OMP_CLAUSE_SCHEDULE_STATIC
      && !fd.have_ordered
      && region->cont
      && region->exit)
    {
      if (fd.chunk_size == NULL)
	expand_omp_for_static_nochunk (region, &fd);
      else
	expand_omp_for_static_chunk (region, &fd);
    }
  else
    {
      int fn_index = fd.sched_kind + fd.have_ordered * 4;
      int start_ix = BUILT_IN_GOMP_LOOP_STATIC_START + fn_index;
      int next_ix = BUILT_IN_GOMP_LOOP_STATIC_NEXT + fn_index;
      expand_omp_for_generic (region, &fd, start_ix, next_ix);
    }

  pop_gimplify_context (NULL);
}


/* Expand code for an OpenMP sections directive.  In pseudo code, we generate

	v = GOMP_sections_start (n);
    L0:
	switch (v)
	  {
	  case 0:
	    goto L2;
	  case 1:
	    section 1;
	    goto L1;
	  case 2:
	    ...
	  case n:
	    ...
	  default:
	    abort ();
	  }
    L1:
	v = GOMP_sections_next ();
	goto L0;
    L2:
	reduction;

    If this is a combined parallel sections, replace the call to
    GOMP_sections_start with 'goto L1'.  */

static void
expand_omp_sections (struct omp_region *region)
{
  tree label_vec, l0, l1, l2, t, u, v, sections_stmt;
  unsigned i, len;
  basic_block entry_bb, exit_bb, l0_bb, l1_bb, l2_bb, default_bb;
  block_stmt_iterator si;
  struct omp_region *inner;
  edge e;

  entry_bb = region->entry;
  l0_bb = create_empty_bb (entry_bb);
  l0 = tree_block_label (l0_bb);

  gcc_assert ((region->cont != NULL) ^ (region->exit == NULL));
  l1_bb = region->cont;
  if (l1_bb)
    {
      l2_bb = single_succ (l1_bb);
      default_bb = create_empty_bb (l1_bb->prev_bb);

      l1 = tree_block_label (l1_bb);
    }
  else
    {
      l2_bb = create_empty_bb (l0_bb);
      default_bb = l2_bb;

      l1 = NULL;
    }
  l2 = tree_block_label (l2_bb);

  exit_bb = region->exit;

  v = create_tmp_var (unsigned_type_node, ".section");

  /* We will build a switch() with enough cases for all the
     OMP_SECTION regions, a '0' case to handle the end of more work
     and a default case to abort if something goes wrong.  */
  len = EDGE_COUNT (entry_bb->succs);
  label_vec = make_tree_vec (len + 2);

  /* The call to GOMP_sections_start goes in ENTRY_BB, replacing the
     OMP_SECTIONS statement.  */
  si = bsi_last (entry_bb);
  sections_stmt = bsi_stmt (si);
  gcc_assert (TREE_CODE (sections_stmt) == OMP_SECTIONS);
  if (!is_combined_parallel (region))
    {
      /* If we are not inside a combined parallel+sections region,
	 call GOMP_sections_start.  */
      t = build_int_cst (unsigned_type_node, len);
      t = tree_cons (NULL, t, NULL);
      u = built_in_decls[BUILT_IN_GOMP_SECTIONS_START];
      t = build_function_call_expr (u, t);
      t = build2 (MODIFY_EXPR, void_type_node, v, t);
      bsi_insert_after (&si, t, BSI_SAME_STMT);
    }
  bsi_remove (&si, true);

  /* The switch() statement replacing OMP_SECTIONS goes in L0_BB.  */
  si = bsi_start (l0_bb);

  t = build3 (SWITCH_EXPR, void_type_node, v, NULL, label_vec);
  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);

  t = build3 (CASE_LABEL_EXPR, void_type_node,
	      build_int_cst (unsigned_type_node, 0), NULL, l2);
  TREE_VEC_ELT (label_vec, 0) = t;
  make_edge (l0_bb, l2_bb, 0);

  /* Convert each OMP_SECTION into a CASE_LABEL_EXPR.  */
  for (inner = region->inner, i = 1; inner; inner = inner->next, ++i)
    {
      basic_block s_entry_bb, s_exit_bb;

      s_entry_bb = inner->entry;
      s_exit_bb = inner->exit;

      t = tree_block_label (s_entry_bb);
      u = build_int_cst (unsigned_type_node, i);
      u = build3 (CASE_LABEL_EXPR, void_type_node, u, NULL, t);
      TREE_VEC_ELT (label_vec, i) = u;

      si = bsi_last (s_entry_bb);
      gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_SECTION);
      gcc_assert (i < len || OMP_SECTION_LAST (bsi_stmt (si)));
      bsi_remove (&si, true);

      e = single_pred_edge (s_entry_bb);
      e->flags = 0;
      redirect_edge_pred (e, l0_bb);

      single_succ_edge (s_entry_bb)->flags = EDGE_FALLTHRU;

      if (s_exit_bb == NULL)
	continue;

      si = bsi_last (s_exit_bb);
      gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_RETURN);
      bsi_remove (&si, true);

      single_succ_edge (s_exit_bb)->flags = EDGE_FALLTHRU;
    }

  /* Error handling code goes in DEFAULT_BB.  */
  t = tree_block_label (default_bb);
  u = build3 (CASE_LABEL_EXPR, void_type_node, NULL, NULL, t);
  TREE_VEC_ELT (label_vec, len + 1) = u;
  make_edge (l0_bb, default_bb, 0);

  si = bsi_start (default_bb);
  t = built_in_decls[BUILT_IN_TRAP];
  t = build_function_call_expr (t, NULL);
  bsi_insert_after (&si, t, BSI_CONTINUE_LINKING);

  /* Code to get the next section goes in L1_BB.  */
  if (l1_bb)
    {
      si = bsi_last (l1_bb);
      gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_CONTINUE);

      t = built_in_decls[BUILT_IN_GOMP_SECTIONS_NEXT];
      t = build_function_call_expr (t, NULL);
      t = build2 (MODIFY_EXPR, void_type_node, v, t);
      bsi_insert_after (&si, t, BSI_SAME_STMT);
      bsi_remove (&si, true);
    }

  /* Cleanup function replaces OMP_RETURN in EXIT_BB.  */
  if (exit_bb)
    {
      si = bsi_last (exit_bb);
      if (OMP_RETURN_NOWAIT (bsi_stmt (si)))
	t = built_in_decls[BUILT_IN_GOMP_SECTIONS_END_NOWAIT];
      else
	t = built_in_decls[BUILT_IN_GOMP_SECTIONS_END];
      t = build_function_call_expr (t, NULL);
      bsi_insert_after (&si, t, BSI_SAME_STMT);
      bsi_remove (&si, true);
    }

  /* Connect the new blocks.  */
  if (is_combined_parallel (region))
    {
      /* If this was a combined parallel+sections region, we did not
	 emit a GOMP_sections_start in the entry block, so we just
	 need to jump to L1_BB to get the next section.  */
      make_edge (entry_bb, l1_bb, EDGE_FALLTHRU);
    }
  else
    make_edge (entry_bb, l0_bb, EDGE_FALLTHRU);

  if (l1_bb)
    {
      e = single_succ_edge (l1_bb);
      redirect_edge_succ (e, l0_bb);
      e->flags = EDGE_FALLTHRU;
    }
}


/* Expand code for an OpenMP single directive.  We've already expanded
   much of the code, here we simply place the GOMP_barrier call.  */

static void
expand_omp_single (struct omp_region *region)
{
  basic_block entry_bb, exit_bb;
  block_stmt_iterator si;
  bool need_barrier = false;

  entry_bb = region->entry;
  exit_bb = region->exit;

  si = bsi_last (entry_bb);
  /* The terminal barrier at the end of a GOMP_single_copy sequence cannot
     be removed.  We need to ensure that the thread that entered the single
     does not exit before the data is copied out by the other threads.  */
  if (find_omp_clause (OMP_SINGLE_CLAUSES (bsi_stmt (si)),
		       OMP_CLAUSE_COPYPRIVATE))
    need_barrier = true;
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_SINGLE);
  bsi_remove (&si, true);
  single_succ_edge (entry_bb)->flags = EDGE_FALLTHRU;

  si = bsi_last (exit_bb);
  if (!OMP_RETURN_NOWAIT (bsi_stmt (si)) || need_barrier)
    {
      tree t = alloc_stmt_list ();
      build_omp_barrier (&t);
      bsi_insert_after (&si, t, BSI_SAME_STMT);
    }
  bsi_remove (&si, true);
  single_succ_edge (exit_bb)->flags = EDGE_FALLTHRU;
}


/* Generic expansion for OpenMP synchronization directives: master,
   ordered and critical.  All we need to do here is remove the entry
   and exit markers for REGION.  */

static void
expand_omp_synch (struct omp_region *region)
{
  basic_block entry_bb, exit_bb;
  block_stmt_iterator si;

  entry_bb = region->entry;
  exit_bb = region->exit;

  si = bsi_last (entry_bb);
  gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_SINGLE
	      || TREE_CODE (bsi_stmt (si)) == OMP_MASTER
	      || TREE_CODE (bsi_stmt (si)) == OMP_ORDERED
	      || TREE_CODE (bsi_stmt (si)) == OMP_CRITICAL);
  bsi_remove (&si, true);
  single_succ_edge (entry_bb)->flags = EDGE_FALLTHRU;

  if (exit_bb)
    {
      si = bsi_last (exit_bb);
      gcc_assert (TREE_CODE (bsi_stmt (si)) == OMP_RETURN);
      bsi_remove (&si, true);
      single_succ_edge (exit_bb)->flags = EDGE_FALLTHRU;
    }
}


/* Expand the parallel region tree rooted at REGION.  Expansion
   proceeds in depth-first order.  Innermost regions are expanded
   first.  This way, parallel regions that require a new function to
   be created (e.g., OMP_PARALLEL) can be expanded without having any
   internal dependencies in their body.  */

static void
expand_omp (struct omp_region *region)
{
  while (region)
    {
      if (region->inner)
	expand_omp (region->inner);

      switch (region->type)
	{
	case OMP_PARALLEL:
	  expand_omp_parallel (region);
	  break;

	case OMP_FOR:
	  expand_omp_for (region);
	  break;

	case OMP_SECTIONS:
	  expand_omp_sections (region);
	  break;

	case OMP_SECTION:
	  /* Individual omp sections are handled together with their
	     parent OMP_SECTIONS region.  */
	  break;

	case OMP_SINGLE:
	  expand_omp_single (region);
	  break;

	case OMP_MASTER:
	case OMP_ORDERED:
	case OMP_CRITICAL:
	  expand_omp_synch (region);
	  break;

	default:
	  gcc_unreachable ();
	}

      region = region->next;
    }
}


/* Helper for build_omp_regions.  Scan the dominator tree starting at
   block BB.  PARENT is the region that contains BB.  */

static void
build_omp_regions_1 (basic_block bb, struct omp_region *parent)
{
  block_stmt_iterator si;
  tree stmt;
  basic_block son;

  si = bsi_last (bb);
  if (!bsi_end_p (si) && OMP_DIRECTIVE_P (bsi_stmt (si)))
    {
      struct omp_region *region;
      enum tree_code code;

      stmt = bsi_stmt (si);
      code = TREE_CODE (stmt);

      if (code == OMP_RETURN)
	{
	  /* STMT is the return point out of region PARENT.  Mark it
	     as the exit point and make PARENT the immediately
	     enclosing region.  */
	  gcc_assert (parent);
	  region = parent;
	  region->exit = bb;
	  parent = parent->outer;

	  /* If REGION is a parallel region, determine whether it is
	     a combined parallel+workshare region.  */
	  if (region->type == OMP_PARALLEL)
	    determine_parallel_type (region);
	}
      else if (code == OMP_CONTINUE)
	{
	  gcc_assert (parent);
	  parent->cont = bb;
	}
      else
	{
	  /* Otherwise, this directive becomes the parent for a new
	     region.  */
	  region = new_omp_region (bb, code, parent);
	  parent = region;
	}
    }

  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    build_omp_regions_1 (son, parent);
}


/* Scan the CFG and build a tree of OMP regions.  Return the root of
   the OMP region tree.  */

static void
build_omp_regions (void)
{
  gcc_assert (root_omp_region == NULL);
  calculate_dominance_info (CDI_DOMINATORS);
  build_omp_regions_1 (ENTRY_BLOCK_PTR, NULL);
}


/* Main entry point for expanding OMP-GIMPLE into runtime calls.  */

static unsigned int
execute_expand_omp (void)
{
  build_omp_regions ();

  if (!root_omp_region)
    return 0;

  if (dump_file)
    {
      fprintf (dump_file, "\nOMP region tree\n\n");
      dump_omp_region (dump_file, root_omp_region, 0);
      fprintf (dump_file, "\n");
    }

  remove_exit_barriers (root_omp_region);

  expand_omp (root_omp_region);

  free_dominance_info (CDI_DOMINATORS);
  free_dominance_info (CDI_POST_DOMINATORS);
  cleanup_tree_cfg ();

  free_omp_regions ();

  return 0;
}

static bool
gate_expand_omp (void)
{
  return flag_openmp != 0 && errorcount == 0;
}

struct tree_opt_pass pass_expand_omp = 
{
  "ompexp",				/* name */
  gate_expand_omp,			/* gate */
  execute_expand_omp,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_any,			/* properties_required */
  PROP_gimple_lomp,			/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};

/* Routines to lower OpenMP directives into OMP-GIMPLE.  */

/* Lower the OpenMP sections directive in *STMT_P.  */

static void
lower_omp_sections (tree *stmt_p, omp_context *ctx)
{
  tree new_stmt, stmt, body, bind, block, ilist, olist, new_body;
  tree t, dlist;
  tree_stmt_iterator tsi;
  unsigned i, len;

  stmt = *stmt_p;

  push_gimplify_context ();

  dlist = NULL;
  ilist = NULL;
  lower_rec_input_clauses (OMP_SECTIONS_CLAUSES (stmt), &ilist, &dlist, ctx);

  tsi = tsi_start (OMP_SECTIONS_BODY (stmt));
  for (len = 0; !tsi_end_p (tsi); len++, tsi_next (&tsi))
    continue;

  tsi = tsi_start (OMP_SECTIONS_BODY (stmt));
  body = alloc_stmt_list ();
  for (i = 0; i < len; i++, tsi_next (&tsi))
    {
      omp_context *sctx;
      tree sec_start, sec_end;

      sec_start = tsi_stmt (tsi);
      sctx = maybe_lookup_ctx (sec_start);
      gcc_assert (sctx);

      append_to_statement_list (sec_start, &body);

      lower_omp (&OMP_SECTION_BODY (sec_start), sctx);
      append_to_statement_list (OMP_SECTION_BODY (sec_start), &body);
      OMP_SECTION_BODY (sec_start) = NULL;

      if (i == len - 1)
	{
	  tree l = alloc_stmt_list ();
	  lower_lastprivate_clauses (OMP_SECTIONS_CLAUSES (stmt), NULL,
				     &l, ctx);
	  append_to_statement_list (l, &body);
	  OMP_SECTION_LAST (sec_start) = 1;
	}
      
      sec_end = make_node (OMP_RETURN);
      append_to_statement_list (sec_end, &body);
    }

  block = make_node (BLOCK);
  bind = build3 (BIND_EXPR, void_type_node, NULL, body, block);

  olist = NULL_TREE;
  lower_reduction_clauses (OMP_SECTIONS_CLAUSES (stmt), &olist, ctx);

  pop_gimplify_context (NULL_TREE);
  record_vars_into (ctx->block_vars, ctx->cb.dst_fn);

  new_stmt = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  TREE_SIDE_EFFECTS (new_stmt) = 1;

  new_body = alloc_stmt_list ();
  append_to_statement_list (ilist, &new_body);
  append_to_statement_list (stmt, &new_body);
  append_to_statement_list (bind, &new_body);

  t = make_node (OMP_CONTINUE);
  append_to_statement_list (t, &new_body);

  append_to_statement_list (olist, &new_body);
  append_to_statement_list (dlist, &new_body);

  maybe_catch_exception (&new_body);

  t = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (t) = !!find_omp_clause (OMP_SECTIONS_CLAUSES (stmt),
					     OMP_CLAUSE_NOWAIT);
  append_to_statement_list (t, &new_body);

  BIND_EXPR_BODY (new_stmt) = new_body;
  OMP_SECTIONS_BODY (stmt) = NULL;

  *stmt_p = new_stmt;
}


/* A subroutine of lower_omp_single.  Expand the simple form of
   an OMP_SINGLE, without a copyprivate clause:

     	if (GOMP_single_start ())
	  BODY;
	[ GOMP_barrier (); ]	-> unless 'nowait' is present.

  FIXME.  It may be better to delay expanding the logic of this until
  pass_expand_omp.  The expanded logic may make the job more difficult
  to a synchronization analysis pass.  */

static void
lower_omp_single_simple (tree single_stmt, tree *pre_p)
{
  tree t;

  t = built_in_decls[BUILT_IN_GOMP_SINGLE_START];
  t = build_function_call_expr (t, NULL);
  t = build3 (COND_EXPR, void_type_node, t,
	      OMP_SINGLE_BODY (single_stmt), NULL);
  gimplify_and_add (t, pre_p);
}


/* A subroutine of lower_omp_single.  Expand the simple form of
   an OMP_SINGLE, with a copyprivate clause:

	#pragma omp single copyprivate (a, b, c)

   Create a new structure to hold copies of 'a', 'b' and 'c' and emit:

      {
	if ((copyout_p = GOMP_single_copy_start ()) == NULL)
	  {
	    BODY;
	    copyout.a = a;
	    copyout.b = b;
	    copyout.c = c;
	    GOMP_single_copy_end (&copyout);
	  }
	else
	  {
	    a = copyout_p->a;
	    b = copyout_p->b;
	    c = copyout_p->c;
	  }
	GOMP_barrier ();
      }

  FIXME.  It may be better to delay expanding the logic of this until
  pass_expand_omp.  The expanded logic may make the job more difficult
  to a synchronization analysis pass.  */

static void
lower_omp_single_copy (tree single_stmt, tree *pre_p, omp_context *ctx)
{
  tree ptr_type, t, args, l0, l1, l2, copyin_seq;

  ctx->sender_decl = create_tmp_var (ctx->record_type, ".omp_copy_o");

  ptr_type = build_pointer_type (ctx->record_type);
  ctx->receiver_decl = create_tmp_var (ptr_type, ".omp_copy_i");

  l0 = create_artificial_label ();
  l1 = create_artificial_label ();
  l2 = create_artificial_label ();

  t = built_in_decls[BUILT_IN_GOMP_SINGLE_COPY_START];
  t = build_function_call_expr (t, NULL);
  t = fold_convert (ptr_type, t);
  t = build2 (MODIFY_EXPR, void_type_node, ctx->receiver_decl, t);
  gimplify_and_add (t, pre_p);

  t = build2 (EQ_EXPR, boolean_type_node, ctx->receiver_decl,
	      build_int_cst (ptr_type, 0));
  t = build3 (COND_EXPR, void_type_node, t,
	      build_and_jump (&l0), build_and_jump (&l1));
  gimplify_and_add (t, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, l0);
  gimplify_and_add (t, pre_p);

  append_to_statement_list (OMP_SINGLE_BODY (single_stmt), pre_p);

  copyin_seq = NULL;
  lower_copyprivate_clauses (OMP_SINGLE_CLAUSES (single_stmt), pre_p,
			      &copyin_seq, ctx);

  t = build_fold_addr_expr (ctx->sender_decl);
  args = tree_cons (NULL, t, NULL);
  t = built_in_decls[BUILT_IN_GOMP_SINGLE_COPY_END];
  t = build_function_call_expr (t, args);
  gimplify_and_add (t, pre_p);

  t = build_and_jump (&l2);
  gimplify_and_add (t, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, l1);
  gimplify_and_add (t, pre_p);

  append_to_statement_list (copyin_seq, pre_p);

  t = build1 (LABEL_EXPR, void_type_node, l2);
  gimplify_and_add (t, pre_p);
}


/* Expand code for an OpenMP single directive.  */

static void
lower_omp_single (tree *stmt_p, omp_context *ctx)
{
  tree t, bind, block, single_stmt = *stmt_p, dlist;

  push_gimplify_context ();

  block = make_node (BLOCK);
  *stmt_p = bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, block);
  TREE_SIDE_EFFECTS (bind) = 1;

  lower_rec_input_clauses (OMP_SINGLE_CLAUSES (single_stmt),
			   &BIND_EXPR_BODY (bind), &dlist, ctx);
  lower_omp (&OMP_SINGLE_BODY (single_stmt), ctx);

  append_to_statement_list (single_stmt, &BIND_EXPR_BODY (bind));

  if (ctx->record_type)
    lower_omp_single_copy (single_stmt, &BIND_EXPR_BODY (bind), ctx);
  else
    lower_omp_single_simple (single_stmt, &BIND_EXPR_BODY (bind));

  OMP_SINGLE_BODY (single_stmt) = NULL;

  append_to_statement_list (dlist, &BIND_EXPR_BODY (bind));

  maybe_catch_exception (&BIND_EXPR_BODY (bind));

  t = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (t) = !!find_omp_clause (OMP_SINGLE_CLAUSES (single_stmt),
					     OMP_CLAUSE_NOWAIT);
  append_to_statement_list (t, &BIND_EXPR_BODY (bind));

  pop_gimplify_context (bind);

  BIND_EXPR_VARS (bind) = chainon (BIND_EXPR_VARS (bind), ctx->block_vars);
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind);
}


/* Expand code for an OpenMP master directive.  */

static void
lower_omp_master (tree *stmt_p, omp_context *ctx)
{
  tree bind, block, stmt = *stmt_p, lab = NULL, x;

  push_gimplify_context ();

  block = make_node (BLOCK);
  *stmt_p = bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, block);
  TREE_SIDE_EFFECTS (bind) = 1;

  append_to_statement_list (stmt, &BIND_EXPR_BODY (bind));

  x = built_in_decls[BUILT_IN_OMP_GET_THREAD_NUM];
  x = build_function_call_expr (x, NULL);
  x = build2 (EQ_EXPR, boolean_type_node, x, integer_zero_node);
  x = build3 (COND_EXPR, void_type_node, x, NULL, build_and_jump (&lab));
  gimplify_and_add (x, &BIND_EXPR_BODY (bind));

  lower_omp (&OMP_MASTER_BODY (stmt), ctx);
  maybe_catch_exception (&OMP_MASTER_BODY (stmt));
  append_to_statement_list (OMP_MASTER_BODY (stmt), &BIND_EXPR_BODY (bind));
  OMP_MASTER_BODY (stmt) = NULL;

  x = build1 (LABEL_EXPR, void_type_node, lab);
  gimplify_and_add (x, &BIND_EXPR_BODY (bind));

  x = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (x) = 1;
  append_to_statement_list (x, &BIND_EXPR_BODY (bind));

  pop_gimplify_context (bind);

  BIND_EXPR_VARS (bind) = chainon (BIND_EXPR_VARS (bind), ctx->block_vars);
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind);
}


/* Expand code for an OpenMP ordered directive.  */

static void
lower_omp_ordered (tree *stmt_p, omp_context *ctx)
{
  tree bind, block, stmt = *stmt_p, x;

  push_gimplify_context ();

  block = make_node (BLOCK);
  *stmt_p = bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, block);
  TREE_SIDE_EFFECTS (bind) = 1;

  append_to_statement_list (stmt, &BIND_EXPR_BODY (bind));

  x = built_in_decls[BUILT_IN_GOMP_ORDERED_START];
  x = build_function_call_expr (x, NULL);
  gimplify_and_add (x, &BIND_EXPR_BODY (bind));

  lower_omp (&OMP_ORDERED_BODY (stmt), ctx);
  maybe_catch_exception (&OMP_ORDERED_BODY (stmt));
  append_to_statement_list (OMP_ORDERED_BODY (stmt), &BIND_EXPR_BODY (bind));
  OMP_ORDERED_BODY (stmt) = NULL;

  x = built_in_decls[BUILT_IN_GOMP_ORDERED_END];
  x = build_function_call_expr (x, NULL);
  gimplify_and_add (x, &BIND_EXPR_BODY (bind));

  x = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (x) = 1;
  append_to_statement_list (x, &BIND_EXPR_BODY (bind));

  pop_gimplify_context (bind);

  BIND_EXPR_VARS (bind) = chainon (BIND_EXPR_VARS (bind), ctx->block_vars);
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind);
}


/* Gimplify an OMP_CRITICAL statement.  This is a relatively simple
   substitution of a couple of function calls.  But in the NAMED case,
   requires that languages coordinate a symbol name.  It is therefore
   best put here in common code.  */

static GTY((param1_is (tree), param2_is (tree)))
  splay_tree critical_name_mutexes;

static void
lower_omp_critical (tree *stmt_p, omp_context *ctx)
{
  tree bind, block, stmt = *stmt_p;
  tree t, lock, unlock, name;

  name = OMP_CRITICAL_NAME (stmt);
  if (name)
    {
      tree decl, args;
      splay_tree_node n;

      if (!critical_name_mutexes)
	critical_name_mutexes
	  = splay_tree_new_ggc (splay_tree_compare_pointers);

      n = splay_tree_lookup (critical_name_mutexes, (splay_tree_key) name);
      if (n == NULL)
	{
	  char *new_str;

	  decl = create_tmp_var_raw (ptr_type_node, NULL);

	  new_str = ACONCAT ((".gomp_critical_user_",
			      IDENTIFIER_POINTER (name), NULL));
	  DECL_NAME (decl) = get_identifier (new_str);
	  TREE_PUBLIC (decl) = 1;
	  TREE_STATIC (decl) = 1;
	  DECL_COMMON (decl) = 1;
	  DECL_ARTIFICIAL (decl) = 1;
	  DECL_IGNORED_P (decl) = 1;
	  cgraph_varpool_finalize_decl (decl);

	  splay_tree_insert (critical_name_mutexes, (splay_tree_key) name,
			     (splay_tree_value) decl);
	}
      else
	decl = (tree) n->value;

      args = tree_cons (NULL, build_fold_addr_expr (decl), NULL);
      lock = built_in_decls[BUILT_IN_GOMP_CRITICAL_NAME_START];
      lock = build_function_call_expr (lock, args);

      args = tree_cons (NULL, build_fold_addr_expr (decl), NULL);
      unlock = built_in_decls[BUILT_IN_GOMP_CRITICAL_NAME_END];
      unlock = build_function_call_expr (unlock, args);
    }
  else
    {
      lock = built_in_decls[BUILT_IN_GOMP_CRITICAL_START];
      lock = build_function_call_expr (lock, NULL);

      unlock = built_in_decls[BUILT_IN_GOMP_CRITICAL_END];
      unlock = build_function_call_expr (unlock, NULL);
    }

  push_gimplify_context ();

  block = make_node (BLOCK);
  *stmt_p = bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, block);
  TREE_SIDE_EFFECTS (bind) = 1;

  append_to_statement_list (stmt, &BIND_EXPR_BODY (bind));

  gimplify_and_add (lock, &BIND_EXPR_BODY (bind));

  lower_omp (&OMP_CRITICAL_BODY (stmt), ctx);
  maybe_catch_exception (&OMP_CRITICAL_BODY (stmt));
  append_to_statement_list (OMP_CRITICAL_BODY (stmt), &BIND_EXPR_BODY (bind));
  OMP_CRITICAL_BODY (stmt) = NULL;

  gimplify_and_add (unlock, &BIND_EXPR_BODY (bind));

  t = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (t) = 1;
  append_to_statement_list (t, &BIND_EXPR_BODY (bind));

  pop_gimplify_context (bind);
  BIND_EXPR_VARS (bind) = chainon (BIND_EXPR_VARS (bind), ctx->block_vars);
  BLOCK_VARS (block) = BIND_EXPR_VARS (bind);
}


/* A subroutine of lower_omp_for.  Generate code to emit the predicate
   for a lastprivate clause.  Given a loop control predicate of (V
   cond N2), we gate the clause on (!(V cond N2)).  The lowered form
   is appended to *DLIST, iterator initialization is appended to
   *BODY_P.  */

static void
lower_omp_for_lastprivate (struct omp_for_data *fd, tree *body_p,
			   tree *dlist, struct omp_context *ctx)
{
  tree clauses, cond, stmts, vinit, t;
  enum tree_code cond_code;
  
  cond_code = fd->cond_code;
  cond_code = cond_code == LT_EXPR ? GE_EXPR : LE_EXPR;

  /* When possible, use a strict equality expression.  This can let VRP
     type optimizations deduce the value and remove a copy.  */
  if (host_integerp (fd->step, 0))
    {
      HOST_WIDE_INT step = TREE_INT_CST_LOW (fd->step);
      if (step == 1 || step == -1)
	cond_code = EQ_EXPR;
    }

  cond = build2 (cond_code, boolean_type_node, fd->v, fd->n2);

  clauses = OMP_FOR_CLAUSES (fd->for_stmt);
  stmts = NULL;
  lower_lastprivate_clauses (clauses, cond, &stmts, ctx);
  if (stmts != NULL)
    {
      append_to_statement_list (stmts, dlist);

      /* Optimize: v = 0; is usually cheaper than v = some_other_constant.  */
      vinit = fd->n1;
      if (cond_code == EQ_EXPR
	  && host_integerp (fd->n2, 0)
	  && ! integer_zerop (fd->n2))
	vinit = build_int_cst (TREE_TYPE (fd->v), 0);

      /* Initialize the iterator variable, so that threads that don't execute
	 any iterations don't execute the lastprivate clauses by accident.  */
      t = build2 (MODIFY_EXPR, void_type_node, fd->v, vinit);
      gimplify_and_add (t, body_p);
    }
}


/* Lower code for an OpenMP loop directive.  */

static void
lower_omp_for (tree *stmt_p, omp_context *ctx)
{
  tree t, stmt, ilist, dlist, new_stmt, *body_p, *rhs_p;
  struct omp_for_data fd;

  stmt = *stmt_p;

  push_gimplify_context ();

  lower_omp (&OMP_FOR_PRE_BODY (stmt), ctx);
  lower_omp (&OMP_FOR_BODY (stmt), ctx);

  /* Move declaration of temporaries in the loop body before we make
     it go away.  */
  if (TREE_CODE (OMP_FOR_BODY (stmt)) == BIND_EXPR)
    record_vars_into (BIND_EXPR_VARS (OMP_FOR_BODY (stmt)), ctx->cb.dst_fn);

  new_stmt = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  TREE_SIDE_EFFECTS (new_stmt) = 1;
  body_p = &BIND_EXPR_BODY (new_stmt);

  /* The pre-body and input clauses go before the lowered OMP_FOR.  */
  ilist = NULL;
  dlist = NULL;
  append_to_statement_list (OMP_FOR_PRE_BODY (stmt), body_p);
  lower_rec_input_clauses (OMP_FOR_CLAUSES (stmt), body_p, &dlist, ctx);

  /* Lower the header expressions.  At this point, we can assume that
     the header is of the form:

     	#pragma omp for (V = VAL1; V {<|>|<=|>=} VAL2; V = V [+-] VAL3)

     We just need to make sure that VAL1, VAL2 and VAL3 are lowered
     using the .omp_data_s mapping, if needed.  */
  rhs_p = &TREE_OPERAND (OMP_FOR_INIT (stmt), 1);
  if (!is_gimple_min_invariant (*rhs_p))
    *rhs_p = get_formal_tmp_var (*rhs_p, body_p);

  rhs_p = &TREE_OPERAND (OMP_FOR_COND (stmt), 1);
  if (!is_gimple_min_invariant (*rhs_p))
    *rhs_p = get_formal_tmp_var (*rhs_p, body_p);

  rhs_p = &TREE_OPERAND (TREE_OPERAND (OMP_FOR_INCR (stmt), 1), 1);
  if (!is_gimple_min_invariant (*rhs_p))
    *rhs_p = get_formal_tmp_var (*rhs_p, body_p);

  /* Once lowered, extract the bounds and clauses.  */
  extract_omp_for_data (stmt, &fd);

  lower_omp_for_lastprivate (&fd, body_p, &dlist, ctx);

  append_to_statement_list (stmt, body_p);

  append_to_statement_list (OMP_FOR_BODY (stmt), body_p);

  t = make_node (OMP_CONTINUE);
  append_to_statement_list (t, body_p);

  /* After the loop, add exit clauses.  */
  lower_reduction_clauses (OMP_FOR_CLAUSES (stmt), body_p, ctx);
  append_to_statement_list (dlist, body_p);

  maybe_catch_exception (body_p);

  /* Region exit marker goes at the end of the loop body.  */
  t = make_node (OMP_RETURN);
  OMP_RETURN_NOWAIT (t) = fd.have_nowait;
  append_to_statement_list (t, body_p);

  pop_gimplify_context (NULL_TREE);
  record_vars_into (ctx->block_vars, ctx->cb.dst_fn);

  OMP_FOR_BODY (stmt) = NULL_TREE;
  OMP_FOR_PRE_BODY (stmt) = NULL_TREE;
  *stmt_p = new_stmt;
}

/* Callback for walk_stmts.  Check if *TP only contains OMP_FOR
   or OMP_PARALLEL.  */

static tree
check_combined_parallel (tree *tp, int *walk_subtrees, void *data)
{
  struct walk_stmt_info *wi = data;
  int *info = wi->info;

  *walk_subtrees = 0;
  switch (TREE_CODE (*tp))
    {
    case OMP_FOR:
    case OMP_SECTIONS:
      *info = *info == 0 ? 1 : -1;
      break;
    default:
      *info = -1;
      break;
    }
  return NULL;
}

/* Lower the OpenMP parallel directive in *STMT_P.  CTX holds context
   information for the directive.  */

static void
lower_omp_parallel (tree *stmt_p, omp_context *ctx)
{
  tree clauses, par_bind, par_body, new_body, bind;
  tree olist, ilist, par_olist, par_ilist;
  tree stmt, child_fn, t;

  stmt = *stmt_p;

  clauses = OMP_PARALLEL_CLAUSES (stmt);
  par_bind = OMP_PARALLEL_BODY (stmt);
  par_body = BIND_EXPR_BODY (par_bind);
  child_fn = ctx->cb.dst_fn;
  if (!OMP_PARALLEL_COMBINED (stmt))
    {
      struct walk_stmt_info wi;
      int ws_num = 0;

      memset (&wi, 0, sizeof (wi));
      wi.callback = check_combined_parallel;
      wi.info = &ws_num;
      wi.val_only = true;
      walk_stmts (&wi, &par_bind);
      if (ws_num == 1)
	OMP_PARALLEL_COMBINED (stmt) = 1;
    }

  push_gimplify_context ();

  par_olist = NULL_TREE;
  par_ilist = NULL_TREE;
  lower_rec_input_clauses (clauses, &par_ilist, &par_olist, ctx);
  lower_omp (&par_body, ctx);
  lower_reduction_clauses (clauses, &par_olist, ctx);

  /* Declare all the variables created by mapping and the variables
     declared in the scope of the parallel body.  */
  record_vars_into (ctx->block_vars, child_fn);
  record_vars_into (BIND_EXPR_VARS (par_bind), child_fn);

  if (ctx->record_type)
    {
      ctx->sender_decl = create_tmp_var (ctx->record_type, ".omp_data_o");
      OMP_PARALLEL_DATA_ARG (stmt) = ctx->sender_decl;
    }

  olist = NULL_TREE;
  ilist = NULL_TREE;
  lower_send_clauses (clauses, &ilist, &olist, ctx);
  lower_send_shared_vars (&ilist, &olist, ctx);

  /* Once all the expansions are done, sequence all the different
     fragments inside OMP_PARALLEL_BODY.  */
  bind = build3 (BIND_EXPR, void_type_node, NULL, NULL, NULL);
  append_to_statement_list (ilist, &BIND_EXPR_BODY (bind));

  new_body = alloc_stmt_list ();

  if (ctx->record_type)
    {
      t = build_fold_addr_expr (ctx->sender_decl);
      /* fixup_child_record_type might have changed receiver_decl's type.  */
      t = fold_convert (TREE_TYPE (ctx->receiver_decl), t);
      t = build2 (MODIFY_EXPR, void_type_node, ctx->receiver_decl, t);
      append_to_statement_list (t, &new_body);
    }

  append_to_statement_list (par_ilist, &new_body);
  append_to_statement_list (par_body, &new_body);
  append_to_statement_list (par_olist, &new_body);
  maybe_catch_exception (&new_body);
  t = make_node (OMP_RETURN);
  append_to_statement_list (t, &new_body);
  OMP_PARALLEL_BODY (stmt) = new_body;

  append_to_statement_list (stmt, &BIND_EXPR_BODY (bind));
  append_to_statement_list (olist, &BIND_EXPR_BODY (bind));

  *stmt_p = bind;

  pop_gimplify_context (NULL_TREE);
}


/* Pass *TP back through the gimplifier within the context determined by WI.
   This handles replacement of DECL_VALUE_EXPR, as well as adjusting the 
   flags on ADDR_EXPR.  */

static void
lower_regimplify (tree *tp, struct walk_stmt_info *wi)
{
  enum gimplify_status gs;
  tree pre = NULL;

  if (wi->is_lhs)
    gs = gimplify_expr (tp, &pre, NULL, is_gimple_lvalue, fb_lvalue);
  else if (wi->val_only)
    gs = gimplify_expr (tp, &pre, NULL, is_gimple_val, fb_rvalue);
  else
    gs = gimplify_expr (tp, &pre, NULL, is_gimple_formal_tmp_var, fb_rvalue);
  gcc_assert (gs == GS_ALL_DONE);

  if (pre)
    tsi_link_before (&wi->tsi, pre, TSI_SAME_STMT);
}

/* Copy EXP into a temporary.  Insert the initialization statement before TSI.  */

static tree
init_tmp_var (tree exp, tree_stmt_iterator *tsi)
{
  tree t, stmt;

  t = create_tmp_var (TREE_TYPE (exp), NULL);
  if (TREE_CODE (TREE_TYPE (t)) == COMPLEX_TYPE)
    DECL_COMPLEX_GIMPLE_REG_P (t) = 1;
  stmt = build2 (MODIFY_EXPR, TREE_TYPE (t), t, exp);
  SET_EXPR_LOCUS (stmt, EXPR_LOCUS (tsi_stmt (*tsi)));
  tsi_link_before (tsi, stmt, TSI_SAME_STMT);

  return t;
}

/* Similarly, but copy from the temporary and insert the statement
   after the iterator.  */

static tree
save_tmp_var (tree exp, tree_stmt_iterator *tsi)
{
  tree t, stmt;

  t = create_tmp_var (TREE_TYPE (exp), NULL);
  if (TREE_CODE (TREE_TYPE (t)) == COMPLEX_TYPE)
    DECL_COMPLEX_GIMPLE_REG_P (t) = 1;
  stmt = build2 (MODIFY_EXPR, TREE_TYPE (t), exp, t);
  SET_EXPR_LOCUS (stmt, EXPR_LOCUS (tsi_stmt (*tsi)));
  tsi_link_after (tsi, stmt, TSI_SAME_STMT);

  return t;
}

/* Callback for walk_stmts.  Lower the OpenMP directive pointed by TP.  */

static tree
lower_omp_1 (tree *tp, int *walk_subtrees, void *data)
{
  struct walk_stmt_info *wi = data;
  omp_context *ctx = wi->info;
  tree t = *tp;

  /* If we have issued syntax errors, avoid doing any heavy lifting.
     Just replace the OpenMP directives with a NOP to avoid
     confusing RTL expansion.  */
  if (errorcount && OMP_DIRECTIVE_P (*tp))
    {
      *tp = build_empty_stmt ();
      return NULL_TREE;
    }

  *walk_subtrees = 0;
  switch (TREE_CODE (*tp))
    {
    case OMP_PARALLEL:
      ctx = maybe_lookup_ctx (t);
      lower_omp_parallel (tp, ctx);
      break;

    case OMP_FOR:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_for (tp, ctx);
      break;

    case OMP_SECTIONS:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_sections (tp, ctx);
      break;

    case OMP_SINGLE:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_single (tp, ctx);
      break;

    case OMP_MASTER:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_master (tp, ctx);
      break;

    case OMP_ORDERED:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_ordered (tp, ctx);
      break;

    case OMP_CRITICAL:
      ctx = maybe_lookup_ctx (t);
      gcc_assert (ctx);
      lower_omp_critical (tp, ctx);
      break;

    case VAR_DECL:
      if (ctx && DECL_HAS_VALUE_EXPR_P (t))
	{
	  lower_regimplify (&t, wi);
	  if (wi->val_only)
	    {
	      if (wi->is_lhs)
		t = save_tmp_var (t, &wi->tsi);
	      else
		t = init_tmp_var (t, &wi->tsi);
	    }
	  *tp = t;
	}
      break;

    case ADDR_EXPR:
      if (ctx)
	lower_regimplify (tp, wi);
      break;

    case ARRAY_REF:
    case ARRAY_RANGE_REF:
    case REALPART_EXPR:
    case IMAGPART_EXPR:
    case COMPONENT_REF:
    case VIEW_CONVERT_EXPR:
      if (ctx)
	lower_regimplify (tp, wi);
      break;

    case INDIRECT_REF:
      if (ctx)
	{
	  wi->is_lhs = false;
	  wi->val_only = true;
	  lower_regimplify (&TREE_OPERAND (t, 0), wi);
	}
      break;

    default:
      if (!TYPE_P (t) && !DECL_P (t))
	*walk_subtrees = 1;
      break;
    }

  return NULL_TREE;
}

static void
lower_omp (tree *stmt_p, omp_context *ctx)
{
  struct walk_stmt_info wi;

  memset (&wi, 0, sizeof (wi));
  wi.callback = lower_omp_1;
  wi.info = ctx;
  wi.val_only = true;
  wi.want_locations = true;

  walk_stmts (&wi, stmt_p);
}

/* Main entry point.  */

static unsigned int
execute_lower_omp (void)
{
  all_contexts = splay_tree_new (splay_tree_compare_pointers, 0,
				 delete_omp_context);

  scan_omp (&DECL_SAVED_TREE (current_function_decl), NULL);
  gcc_assert (parallel_nesting_level == 0);

  if (all_contexts->root)
    lower_omp (&DECL_SAVED_TREE (current_function_decl), NULL);

  if (all_contexts)
    {
      splay_tree_delete (all_contexts);
      all_contexts = NULL;
    }
  return 0;
}

static bool
gate_lower_omp (void)
{
  return flag_openmp != 0;
}

struct tree_opt_pass pass_lower_omp = 
{
  "omplower",				/* name */
  gate_lower_omp,			/* gate */
  execute_lower_omp,			/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  0,					/* tv_id */
  PROP_gimple_any,			/* properties_required */
  PROP_gimple_lomp,			/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_func,			/* todo_flags_finish */
  0					/* letter */
};

/* The following is a utility to diagnose OpenMP structured block violations.
   It is not part of the "omplower" pass, as that's invoked too late.  It
   should be invoked by the respective front ends after gimplification.  */

static splay_tree all_labels;

/* Check for mismatched contexts and generate an error if needed.  Return
   true if an error is detected.  */

static bool
diagnose_sb_0 (tree *stmt_p, tree branch_ctx, tree label_ctx)
{
  bool exit_p = true;

  if ((label_ctx ? TREE_VALUE (label_ctx) : NULL) == branch_ctx)
    return false;

  /* Try to avoid confusing the user by producing and error message
     with correct "exit" or "enter" verbage.  We prefer "exit"
     unless we can show that LABEL_CTX is nested within BRANCH_CTX.  */
  if (branch_ctx == NULL)
    exit_p = false;
  else
    {
      while (label_ctx)
	{
	  if (TREE_VALUE (label_ctx) == branch_ctx)
	    {
	      exit_p = false;
	      break;
	    }
	  label_ctx = TREE_CHAIN (label_ctx);
	}
    }

  if (exit_p)
    error ("invalid exit from OpenMP structured block");
  else
    error ("invalid entry to OpenMP structured block");

  *stmt_p = build_empty_stmt ();
  return true;
}

/* Pass 1: Create a minimal tree of OpenMP structured blocks, and record
   where in the tree each label is found.  */

static tree
diagnose_sb_1 (tree *tp, int *walk_subtrees, void *data)
{
  struct walk_stmt_info *wi = data;
  tree context = (tree) wi->info;
  tree inner_context;
  tree t = *tp;

  *walk_subtrees = 0;
  switch (TREE_CODE (t))
    {
    case OMP_PARALLEL:
    case OMP_SECTIONS:
    case OMP_SINGLE:
      walk_tree (&OMP_CLAUSES (t), diagnose_sb_1, wi, NULL);
      /* FALLTHRU */
    case OMP_SECTION:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
      /* The minimal context here is just a tree of statements.  */
      inner_context = tree_cons (NULL, t, context);
      wi->info = inner_context;
      walk_stmts (wi, &OMP_BODY (t));
      wi->info = context;
      break;

    case OMP_FOR:
      walk_tree (&OMP_FOR_CLAUSES (t), diagnose_sb_1, wi, NULL);
      inner_context = tree_cons (NULL, t, context);
      wi->info = inner_context;
      walk_tree (&OMP_FOR_INIT (t), diagnose_sb_1, wi, NULL);
      walk_tree (&OMP_FOR_COND (t), diagnose_sb_1, wi, NULL);
      walk_tree (&OMP_FOR_INCR (t), diagnose_sb_1, wi, NULL);
      walk_stmts (wi, &OMP_FOR_PRE_BODY (t));
      walk_stmts (wi, &OMP_FOR_BODY (t));
      wi->info = context;
      break;

    case LABEL_EXPR:
      splay_tree_insert (all_labels, (splay_tree_key) LABEL_EXPR_LABEL (t),
			 (splay_tree_value) context);
      break;

    default:
      break;
    }

  return NULL_TREE;
}

/* Pass 2: Check each branch and see if its context differs from that of
   the destination label's context.  */

static tree
diagnose_sb_2 (tree *tp, int *walk_subtrees, void *data)
{
  struct walk_stmt_info *wi = data;
  tree context = (tree) wi->info;
  splay_tree_node n;
  tree t = *tp;

  *walk_subtrees = 0;
  switch (TREE_CODE (t))
    {
    case OMP_PARALLEL:
    case OMP_SECTIONS:
    case OMP_SINGLE:
      walk_tree (&OMP_CLAUSES (t), diagnose_sb_2, wi, NULL);
      /* FALLTHRU */
    case OMP_SECTION:
    case OMP_MASTER:
    case OMP_ORDERED:
    case OMP_CRITICAL:
      wi->info = t;
      walk_stmts (wi, &OMP_BODY (t));
      wi->info = context;
      break;

    case OMP_FOR:
      walk_tree (&OMP_FOR_CLAUSES (t), diagnose_sb_2, wi, NULL);
      wi->info = t;
      walk_tree (&OMP_FOR_INIT (t), diagnose_sb_2, wi, NULL);
      walk_tree (&OMP_FOR_COND (t), diagnose_sb_2, wi, NULL);
      walk_tree (&OMP_FOR_INCR (t), diagnose_sb_2, wi, NULL);
      walk_stmts (wi, &OMP_FOR_PRE_BODY (t));
      walk_stmts (wi, &OMP_FOR_BODY (t));
      wi->info = context;
      break;

    case GOTO_EXPR:
      {
	tree lab = GOTO_DESTINATION (t);
	if (TREE_CODE (lab) != LABEL_DECL)
	  break;

	n = splay_tree_lookup (all_labels, (splay_tree_key) lab);
	diagnose_sb_0 (tp, context, n ? (tree) n->value : NULL_TREE);
      }
      break;

    case SWITCH_EXPR:
      {
	tree vec = SWITCH_LABELS (t);
	int i, len = TREE_VEC_LENGTH (vec);
	for (i = 0; i < len; ++i)
	  {
	    tree lab = CASE_LABEL (TREE_VEC_ELT (vec, i));
	    n = splay_tree_lookup (all_labels, (splay_tree_key) lab);
	    if (diagnose_sb_0 (tp, context, (tree) n->value))
	      break;
	  }
      }
      break;

    case RETURN_EXPR:
      diagnose_sb_0 (tp, context, NULL_TREE);
      break;

    default:
      break;
    }

  return NULL_TREE;
}

void
diagnose_omp_structured_block_errors (tree fndecl)
{
  tree save_current = current_function_decl;
  struct walk_stmt_info wi;

  current_function_decl = fndecl;

  all_labels = splay_tree_new (splay_tree_compare_pointers, 0, 0);

  memset (&wi, 0, sizeof (wi));
  wi.callback = diagnose_sb_1;
  walk_stmts (&wi, &DECL_SAVED_TREE (fndecl));

  memset (&wi, 0, sizeof (wi));
  wi.callback = diagnose_sb_2;
  wi.want_locations = true;
  wi.want_return_expr = true;
  walk_stmts (&wi, &DECL_SAVED_TREE (fndecl));

  splay_tree_delete (all_labels);
  all_labels = NULL;

  current_function_decl = save_current;
}

#include "gt-omp-low.h"
