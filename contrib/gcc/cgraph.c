/* Callgraph handling code.
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Jan Hubicka

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

/*  This file contains basic routines manipulating call graph and variable pool

The callgraph:

    The call-graph is data structure designed for intra-procedural optimization
    but it is also used in non-unit-at-a-time compilation to allow easier code
    sharing.

    The call-graph consist of nodes and edges represented via linked lists.
    Each function (external or not) corresponds to the unique node (in
    contrast to tree DECL nodes where we can have multiple nodes for each
    function).

    The mapping from declarations to call-graph nodes is done using hash table
    based on DECL_ASSEMBLER_NAME, so it is essential for assembler name to
    not change once the declaration is inserted into the call-graph.
    The call-graph nodes are created lazily using cgraph_node function when
    called for unknown declaration.

    When built, there is one edge for each direct call.  It is possible that
    the reference will be later optimized out.  The call-graph is built
    conservatively in order to make conservative data flow analysis possible.

    The callgraph at the moment does not represent indirect calls or calls
    from other compilation unit.  Flag NEEDED is set for each node that may
    be accessed in such an invisible way and it shall be considered an
    entry point to the callgraph.

    Interprocedural information:

      Callgraph is place to store data needed for interprocedural optimization.
      All data structures are divided into three components: local_info that
      is produced while analyzing the function, global_info that is result
      of global walking of the callgraph on the end of compilation and
      rtl_info used by RTL backend to propagate data from already compiled
      functions to their callers.

    Inlining plans:

      The function inlining information is decided in advance and maintained
      in the callgraph as so called inline plan.
      For each inlined call, the callee's node is cloned to represent the
      new function copy produced by inliner.
      Each inlined call gets a unique corresponding clone node of the callee
      and the data structure is updated while inlining is performed, so
      the clones are eliminated and their callee edges redirected to the
      caller.

      Each edge has "inline_failed" field.  When the field is set to NULL,
      the call will be inlined.  When it is non-NULL it contains a reason
      why inlining wasn't performed.


The varpool data structure:

    Varpool is used to maintain variables in similar manner as call-graph
    is used for functions.  Most of the API is symmetric replacing cgraph
    function prefix by cgraph_varpool  */


#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-inline.h"
#include "langhooks.h"
#include "hashtab.h"
#include "toplev.h"
#include "flags.h"
#include "ggc.h"
#include "debug.h"
#include "target.h"
#include "basic-block.h"
#include "cgraph.h"
#include "varray.h"
#include "output.h"
#include "intl.h"
#include "tree-gimple.h"
#include "tree-dump.h"

static void cgraph_node_remove_callers (struct cgraph_node *node);
static inline void cgraph_edge_remove_caller (struct cgraph_edge *e);
static inline void cgraph_edge_remove_callee (struct cgraph_edge *e);

/* Hash table used to convert declarations into nodes.  */
static GTY((param_is (struct cgraph_node))) htab_t cgraph_hash;

/* The linked list of cgraph nodes.  */
struct cgraph_node *cgraph_nodes;

/* Queue of cgraph nodes scheduled to be lowered.  */
struct cgraph_node *cgraph_nodes_queue;

/* Queue of cgraph nodes scheduled to be expanded.  This is a
   secondary queue used during optimization to accommodate passes that
   may generate new functions that need to be optimized and expanded.  */
struct cgraph_node *cgraph_expand_queue;

/* Number of nodes in existence.  */
int cgraph_n_nodes;

/* Maximal uid used in cgraph nodes.  */
int cgraph_max_uid;

/* Set when whole unit has been analyzed so we can access global info.  */
bool cgraph_global_info_ready = false;

/* Set when the cgraph is fully build and the basic flags are computed.  */
bool cgraph_function_flags_ready = false;

/* Hash table used to convert declarations into nodes.  */
static GTY((param_is (struct cgraph_varpool_node))) htab_t cgraph_varpool_hash;

/* Queue of cgraph nodes scheduled to be lowered and output.  */
struct cgraph_varpool_node *cgraph_varpool_nodes_queue, *cgraph_varpool_first_unanalyzed_node;

/* The linked list of cgraph varpool nodes.  */
struct cgraph_varpool_node *cgraph_varpool_nodes;

/* End of the varpool queue.  */
struct cgraph_varpool_node *cgraph_varpool_last_needed_node;

/* Linked list of cgraph asm nodes.  */
struct cgraph_asm_node *cgraph_asm_nodes;

/* Last node in cgraph_asm_nodes.  */
static GTY(()) struct cgraph_asm_node *cgraph_asm_last_node;

/* The order index of the next cgraph node to be created.  This is
   used so that we can sort the cgraph nodes in order by when we saw
   them, to support -fno-toplevel-reorder.  */
int cgraph_order;

static hashval_t hash_node (const void *);
static int eq_node (const void *, const void *);

/* Returns a hash code for P.  */

static hashval_t
hash_node (const void *p)
{
  const struct cgraph_node *n = (const struct cgraph_node *) p;
  return (hashval_t) DECL_UID (n->decl);
}

/* Returns nonzero if P1 and P2 are equal.  */

static int
eq_node (const void *p1, const void *p2)
{
  const struct cgraph_node *n1 = (const struct cgraph_node *) p1;
  const struct cgraph_node *n2 = (const struct cgraph_node *) p2;
  return DECL_UID (n1->decl) == DECL_UID (n2->decl);
}

/* Allocate new callgraph node and insert it into basic data structures.  */
static struct cgraph_node *
cgraph_create_node (void)
{
  struct cgraph_node *node;

  node = GGC_CNEW (struct cgraph_node);
  node->next = cgraph_nodes;
  node->uid = cgraph_max_uid++;
  node->order = cgraph_order++;
  if (cgraph_nodes)
    cgraph_nodes->previous = node;
  node->previous = NULL;
  node->global.estimated_growth = INT_MIN;
  cgraph_nodes = node;
  cgraph_n_nodes++;
  return node;
}

/* Return cgraph node assigned to DECL.  Create new one when needed.  */
struct cgraph_node *
cgraph_node (tree decl)
{
  struct cgraph_node key, *node, **slot;

  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL);

  if (!cgraph_hash)
    cgraph_hash = htab_create_ggc (10, hash_node, eq_node, NULL);

  key.decl = decl;

  slot = (struct cgraph_node **) htab_find_slot (cgraph_hash, &key, INSERT);

  if (*slot)
    {
      node = *slot;
      if (!node->master_clone)
	node->master_clone = node;
      return node;
    }

  node = cgraph_create_node ();
  node->decl = decl;
  *slot = node;
  if (DECL_CONTEXT (decl) && TREE_CODE (DECL_CONTEXT (decl)) == FUNCTION_DECL)
    {
      node->origin = cgraph_node (DECL_CONTEXT (decl));
      node->next_nested = node->origin->nested;
      node->origin->nested = node;
      node->master_clone = node;
    }
  return node;
}

/* Insert already constructed node into hashtable.  */

void
cgraph_insert_node_to_hashtable (struct cgraph_node *node)
{
  struct cgraph_node **slot;

  slot = (struct cgraph_node **) htab_find_slot (cgraph_hash, node, INSERT);

  gcc_assert (!*slot);
  *slot = node;
}

/* Compare ASMNAME with the DECL_ASSEMBLER_NAME of DECL.  */

static bool
decl_assembler_name_equal (tree decl, tree asmname)
{
  tree decl_asmname = DECL_ASSEMBLER_NAME (decl);

  if (decl_asmname == asmname)
    return true;

  /* If the target assembler name was set by the user, things are trickier.
     We have a leading '*' to begin with.  After that, it's arguable what
     is the correct thing to do with -fleading-underscore.  Arguably, we've
     historically been doing the wrong thing in assemble_alias by always
     printing the leading underscore.  Since we're not changing that, make
     sure user_label_prefix follows the '*' before matching.  */
  if (IDENTIFIER_POINTER (decl_asmname)[0] == '*')
    {
      const char *decl_str = IDENTIFIER_POINTER (decl_asmname) + 1;
      size_t ulp_len = strlen (user_label_prefix);

      if (ulp_len == 0)
	;
      else if (strncmp (decl_str, user_label_prefix, ulp_len) == 0)
	decl_str += ulp_len;
      else
	return false;

      return strcmp (decl_str, IDENTIFIER_POINTER (asmname)) == 0;
    }

  return false;
}


/* Return the cgraph node that has ASMNAME for its DECL_ASSEMBLER_NAME.
   Return NULL if there's no such node.  */

struct cgraph_node *
cgraph_node_for_asm (tree asmname)
{
  struct cgraph_node *node;

  for (node = cgraph_nodes; node ; node = node->next)
    if (decl_assembler_name_equal (node->decl, asmname))
      return node;

  return NULL;
}

/* Returns a hash value for X (which really is a die_struct).  */

static hashval_t
edge_hash (const void *x)
{
  return htab_hash_pointer (((struct cgraph_edge *) x)->call_stmt);
}

/* Return nonzero if decl_id of die_struct X is the same as UID of decl *Y.  */

static int
edge_eq (const void *x, const void *y)
{
  return ((struct cgraph_edge *) x)->call_stmt == y;
}

/* Return callgraph edge representing CALL_EXPR statement.  */
struct cgraph_edge *
cgraph_edge (struct cgraph_node *node, tree call_stmt)
{
  struct cgraph_edge *e, *e2;
  int n = 0;

  if (node->call_site_hash)
    return htab_find_with_hash (node->call_site_hash, call_stmt,
      				htab_hash_pointer (call_stmt));

  /* This loop may turn out to be performance problem.  In such case adding
     hashtables into call nodes with very many edges is probably best
     solution.  It is not good idea to add pointer into CALL_EXPR itself
     because we want to make possible having multiple cgraph nodes representing
     different clones of the same body before the body is actually cloned.  */
  for (e = node->callees; e; e= e->next_callee)
    {
      if (e->call_stmt == call_stmt)
	break;
      n++;
    }
  if (n > 100)
    {
      node->call_site_hash = htab_create_ggc (120, edge_hash, edge_eq, NULL);
      for (e2 = node->callees; e2; e2 = e2->next_callee)
	{
          void **slot;
	  slot = htab_find_slot_with_hash (node->call_site_hash,
					   e2->call_stmt,
					   htab_hash_pointer (e2->call_stmt),
					   INSERT);
	  gcc_assert (!*slot);
	  *slot = e2;
	}
    }
  return e;
}

/* Change call_smtt of edge E to NEW_STMT.  */
void
cgraph_set_call_stmt (struct cgraph_edge *e, tree new_stmt)
{
  if (e->caller->call_site_hash)
    {
      htab_remove_elt_with_hash (e->caller->call_site_hash,
				 e->call_stmt,
				 htab_hash_pointer (e->call_stmt));
    }
  e->call_stmt = new_stmt;
  if (e->caller->call_site_hash)
    {
      void **slot;
      slot = htab_find_slot_with_hash (e->caller->call_site_hash,
				       e->call_stmt,
				       htab_hash_pointer
				       (e->call_stmt), INSERT);
      gcc_assert (!*slot);
      *slot = e;
    }
}

/* Create edge from CALLER to CALLEE in the cgraph.  */

struct cgraph_edge *
cgraph_create_edge (struct cgraph_node *caller, struct cgraph_node *callee,
		    tree call_stmt, gcov_type count, int nest)
{
  struct cgraph_edge *edge = GGC_NEW (struct cgraph_edge);
#ifdef ENABLE_CHECKING
  struct cgraph_edge *e;

  for (e = caller->callees; e; e = e->next_callee)
    gcc_assert (e->call_stmt != call_stmt);
#endif

  gcc_assert (get_call_expr_in (call_stmt));

  if (!DECL_SAVED_TREE (callee->decl))
    edge->inline_failed = N_("function body not available");
  else if (callee->local.redefined_extern_inline)
    edge->inline_failed = N_("redefined extern inline functions are not "
			     "considered for inlining");
  else if (callee->local.inlinable)
    edge->inline_failed = N_("function not considered for inlining");
  else
    edge->inline_failed = N_("function not inlinable");

  edge->aux = NULL;

  edge->caller = caller;
  edge->callee = callee;
  edge->call_stmt = call_stmt;
  edge->prev_caller = NULL;
  edge->next_caller = callee->callers;
  if (callee->callers)
    callee->callers->prev_caller = edge;
  edge->prev_callee = NULL;
  edge->next_callee = caller->callees;
  if (caller->callees)
    caller->callees->prev_callee = edge;
  caller->callees = edge;
  callee->callers = edge;
  edge->count = count;
  edge->loop_nest = nest;
  if (caller->call_site_hash)
    {
      void **slot;
      slot = htab_find_slot_with_hash (caller->call_site_hash,
				       edge->call_stmt,
				       htab_hash_pointer
					 (edge->call_stmt),
				       INSERT);
      gcc_assert (!*slot);
      *slot = edge;
    }
  return edge;
}

/* Remove the edge E from the list of the callers of the callee.  */

static inline void
cgraph_edge_remove_callee (struct cgraph_edge *e)
{
  if (e->prev_caller)
    e->prev_caller->next_caller = e->next_caller;
  if (e->next_caller)
    e->next_caller->prev_caller = e->prev_caller;
  if (!e->prev_caller)
    e->callee->callers = e->next_caller;
}

/* Remove the edge E from the list of the callees of the caller.  */

static inline void
cgraph_edge_remove_caller (struct cgraph_edge *e)
{
  if (e->prev_callee)
    e->prev_callee->next_callee = e->next_callee;
  if (e->next_callee)
    e->next_callee->prev_callee = e->prev_callee;
  if (!e->prev_callee)
    e->caller->callees = e->next_callee;
  if (e->caller->call_site_hash)
    htab_remove_elt_with_hash (e->caller->call_site_hash,
			       e->call_stmt,
	  		       htab_hash_pointer (e->call_stmt));
}

/* Remove the edge E in the cgraph.  */

void
cgraph_remove_edge (struct cgraph_edge *e)
{
  /* Remove from callers list of the callee.  */
  cgraph_edge_remove_callee (e);

  /* Remove from callees list of the callers.  */
  cgraph_edge_remove_caller (e);
}

/* Redirect callee of E to N.  The function does not update underlying
   call expression.  */

void
cgraph_redirect_edge_callee (struct cgraph_edge *e, struct cgraph_node *n)
{
  /* Remove from callers list of the current callee.  */
  cgraph_edge_remove_callee (e);

  /* Insert to callers list of the new callee.  */
  e->prev_caller = NULL;
  if (n->callers)
    n->callers->prev_caller = e;
  e->next_caller = n->callers;
  n->callers = e;
  e->callee = n;
}

/* Remove all callees from the node.  */

void
cgraph_node_remove_callees (struct cgraph_node *node)
{
  struct cgraph_edge *e;

  /* It is sufficient to remove the edges from the lists of callers of
     the callees.  The callee list of the node can be zapped with one
     assignment.  */
  for (e = node->callees; e; e = e->next_callee)
    cgraph_edge_remove_callee (e);
  node->callees = NULL;
  if (node->call_site_hash)
    {
      htab_delete (node->call_site_hash);
      node->call_site_hash = NULL;
    }
}

/* Remove all callers from the node.  */

static void
cgraph_node_remove_callers (struct cgraph_node *node)
{
  struct cgraph_edge *e;

  /* It is sufficient to remove the edges from the lists of callees of
     the callers.  The caller list of the node can be zapped with one
     assignment.  */
  for (e = node->callers; e; e = e->next_caller)
    cgraph_edge_remove_caller (e);
  node->callers = NULL;
}

/* Remove the node from cgraph.  */

void
cgraph_remove_node (struct cgraph_node *node)
{
  void **slot;
  bool kill_body = false;

  cgraph_node_remove_callers (node);
  cgraph_node_remove_callees (node);
  /* Incremental inlining access removed nodes stored in the postorder list.
     */
  node->needed = node->reachable = false;
  while (node->nested)
    cgraph_remove_node (node->nested);
  if (node->origin)
    {
      struct cgraph_node **node2 = &node->origin->nested;

      while (*node2 != node)
	node2 = &(*node2)->next_nested;
      *node2 = node->next_nested;
    }
  if (node->previous)
    node->previous->next = node->next;
  else
    cgraph_nodes = node->next;
  if (node->next)
    node->next->previous = node->previous;
  node->next = NULL;
  node->previous = NULL;
  slot = htab_find_slot (cgraph_hash, node, NO_INSERT);
  if (*slot == node)
    {
      if (node->next_clone)
      {
	struct cgraph_node *new_node = node->next_clone;
	struct cgraph_node *n;

	/* Make the next clone be the master clone */
	for (n = new_node; n; n = n->next_clone)
	  n->master_clone = new_node;

	*slot = new_node;
	node->next_clone->prev_clone = NULL;
      }
      else
	{
	  htab_clear_slot (cgraph_hash, slot);
	  kill_body = true;
	}
    }
  else
    {
      node->prev_clone->next_clone = node->next_clone;
      if (node->next_clone)
	node->next_clone->prev_clone = node->prev_clone;
    }

  /* While all the clones are removed after being proceeded, the function
     itself is kept in the cgraph even after it is compiled.  Check whether
     we are done with this body and reclaim it proactively if this is the case.
     */
  if (!kill_body && *slot)
    {
      struct cgraph_node *n = (struct cgraph_node *) *slot;
      if (!n->next_clone && !n->global.inlined_to
	  && (cgraph_global_info_ready
	      && (TREE_ASM_WRITTEN (n->decl) || DECL_EXTERNAL (n->decl))))
	kill_body = true;
    }

  if (kill_body && flag_unit_at_a_time)
    {
      DECL_SAVED_TREE (node->decl) = NULL;
      DECL_STRUCT_FUNCTION (node->decl) = NULL;
      DECL_INITIAL (node->decl) = error_mark_node;
    }
  node->decl = NULL;
  if (node->call_site_hash)
    {
      htab_delete (node->call_site_hash);
      node->call_site_hash = NULL;
    }
  cgraph_n_nodes--;
  /* Do not free the structure itself so the walk over chain can continue.  */
}

/* Notify finalize_compilation_unit that given node is reachable.  */

void
cgraph_mark_reachable_node (struct cgraph_node *node)
{
  if (!node->reachable && node->local.finalized)
    {
      notice_global_symbol (node->decl);
      node->reachable = 1;
      gcc_assert (!cgraph_global_info_ready);

      node->next_needed = cgraph_nodes_queue;
      cgraph_nodes_queue = node;
    }
}

/* Likewise indicate that a node is needed, i.e. reachable via some
   external means.  */

void
cgraph_mark_needed_node (struct cgraph_node *node)
{
  node->needed = 1;
  cgraph_mark_reachable_node (node);
}

/* Return local info for the compiled function.  */

struct cgraph_local_info *
cgraph_local_info (tree decl)
{
  struct cgraph_node *node;

  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL);
  node = cgraph_node (decl);
  return &node->local;
}

/* Return local info for the compiled function.  */

struct cgraph_global_info *
cgraph_global_info (tree decl)
{
  struct cgraph_node *node;

  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL && cgraph_global_info_ready);
  node = cgraph_node (decl);
  return &node->global;
}

/* Return local info for the compiled function.  */

struct cgraph_rtl_info *
cgraph_rtl_info (tree decl)
{
  struct cgraph_node *node;

  gcc_assert (TREE_CODE (decl) == FUNCTION_DECL);
  node = cgraph_node (decl);
  if (decl != current_function_decl
      && !TREE_ASM_WRITTEN (node->decl))
    return NULL;
  return &node->rtl;
}

/* Return name of the node used in debug output.  */
const char *
cgraph_node_name (struct cgraph_node *node)
{
  return lang_hooks.decl_printable_name (node->decl, 2);
}

/* Return name of the node used in debug output.  */
static const char *
cgraph_varpool_node_name (struct cgraph_varpool_node *node)
{
  return lang_hooks.decl_printable_name (node->decl, 2);
}

/* Names used to print out the availability enum.  */
static const char * const availability_names[] =
  {"unset", "not_available", "overwrittable", "available", "local"};

/* Dump given cgraph node.  */
void
dump_cgraph_node (FILE *f, struct cgraph_node *node)
{
  struct cgraph_edge *edge;
  fprintf (f, "%s/%i:", cgraph_node_name (node), node->uid);
  if (node->global.inlined_to)
    fprintf (f, " (inline copy in %s/%i)",
	     cgraph_node_name (node->global.inlined_to),
	     node->global.inlined_to->uid);
  if (cgraph_function_flags_ready)
    fprintf (f, " availability:%s",
	     availability_names [cgraph_function_body_availability (node)]);
  if (node->master_clone && node->master_clone->uid != node->uid)
    fprintf (f, "(%i)", node->master_clone->uid);
  if (node->count)
    fprintf (f, " executed "HOST_WIDEST_INT_PRINT_DEC"x",
	     (HOST_WIDEST_INT)node->count);
  if (node->local.self_insns)
    fprintf (f, " %i insns", node->local.self_insns);
  if (node->global.insns && node->global.insns != node->local.self_insns)
    fprintf (f, " (%i after inlining)", node->global.insns);
  if (node->origin)
    fprintf (f, " nested in: %s", cgraph_node_name (node->origin));
  if (node->needed)
    fprintf (f, " needed");
  else if (node->reachable)
    fprintf (f, " reachable");
  if (DECL_SAVED_TREE (node->decl))
    fprintf (f, " tree");
  if (node->output)
    fprintf (f, " output");
  if (node->local.local)
    fprintf (f, " local");
  if (node->local.externally_visible)
    fprintf (f, " externally_visible");
  if (node->local.finalized)
    fprintf (f, " finalized");
  if (node->local.disregard_inline_limits)
    fprintf (f, " always_inline");
  else if (node->local.inlinable)
    fprintf (f, " inlinable");
  if (node->local.redefined_extern_inline)
    fprintf (f, " redefined_extern_inline");
  if (TREE_ASM_WRITTEN (node->decl))
    fprintf (f, " asm_written");

  fprintf (f, "\n  called by: ");
  for (edge = node->callers; edge; edge = edge->next_caller)
    {
      fprintf (f, "%s/%i ", cgraph_node_name (edge->caller),
	       edge->caller->uid);
      if (edge->count)
	fprintf (f, "("HOST_WIDEST_INT_PRINT_DEC"x) ",
		 (HOST_WIDEST_INT)edge->count);
      if (!edge->inline_failed)
	fprintf(f, "(inlined) ");
    }

  fprintf (f, "\n  calls: ");
  for (edge = node->callees; edge; edge = edge->next_callee)
    {
      fprintf (f, "%s/%i ", cgraph_node_name (edge->callee),
	       edge->callee->uid);
      if (!edge->inline_failed)
	fprintf(f, "(inlined) ");
      if (edge->count)
	fprintf (f, "("HOST_WIDEST_INT_PRINT_DEC"x) ",
		 (HOST_WIDEST_INT)edge->count);
      if (edge->loop_nest)
	fprintf (f, "(nested in %i loops) ", edge->loop_nest);
    }
  fprintf (f, "\n");
}

/* Dump the callgraph.  */

void
dump_cgraph (FILE *f)
{
  struct cgraph_node *node;

  fprintf (f, "callgraph:\n\n");
  for (node = cgraph_nodes; node; node = node->next)
    dump_cgraph_node (f, node);
}

/* Dump given cgraph node.  */
void
dump_cgraph_varpool_node (FILE *f, struct cgraph_varpool_node *node)
{
  fprintf (f, "%s:", cgraph_varpool_node_name (node));
  fprintf (f, " availability:%s",
	   cgraph_function_flags_ready
	   ? availability_names[cgraph_variable_initializer_availability (node)]
	   : "not-ready");
  if (DECL_INITIAL (node->decl))
    fprintf (f, " initialized");
  if (node->needed)
    fprintf (f, " needed");
  if (node->analyzed)
    fprintf (f, " analyzed");
  if (node->finalized)
    fprintf (f, " finalized");
  if (node->output)
    fprintf (f, " output");
  if (node->externally_visible)
    fprintf (f, " externally_visible");
  fprintf (f, "\n");
}

/* Dump the callgraph.  */

void
dump_varpool (FILE *f)
{
  struct cgraph_varpool_node *node;

  fprintf (f, "variable pool:\n\n");
  for (node = cgraph_varpool_nodes; node; node = node->next_needed)
    dump_cgraph_varpool_node (f, node);
}

/* Returns a hash code for P.  */

static hashval_t
hash_varpool_node (const void *p)
{
  const struct cgraph_varpool_node *n = (const struct cgraph_varpool_node *) p;
  return (hashval_t) DECL_UID (n->decl);
}

/* Returns nonzero if P1 and P2 are equal.  */

static int
eq_varpool_node (const void *p1, const void *p2)
{
  const struct cgraph_varpool_node *n1 =
    (const struct cgraph_varpool_node *) p1;
  const struct cgraph_varpool_node *n2 =
    (const struct cgraph_varpool_node *) p2;
  return DECL_UID (n1->decl) == DECL_UID (n2->decl);
}

/* Return cgraph_varpool node assigned to DECL.  Create new one when needed.  */
struct cgraph_varpool_node *
cgraph_varpool_node (tree decl)
{
  struct cgraph_varpool_node key, *node, **slot;

  gcc_assert (DECL_P (decl) && TREE_CODE (decl) != FUNCTION_DECL);

  if (!cgraph_varpool_hash)
    cgraph_varpool_hash = htab_create_ggc (10, hash_varpool_node,
					   eq_varpool_node, NULL);
  key.decl = decl;
  slot = (struct cgraph_varpool_node **)
    htab_find_slot (cgraph_varpool_hash, &key, INSERT);
  if (*slot)
    return *slot;
  node = GGC_CNEW (struct cgraph_varpool_node);
  node->decl = decl;
  node->order = cgraph_order++;
  node->next = cgraph_varpool_nodes;
  cgraph_varpool_nodes = node;
  *slot = node;
  return node;
}

struct cgraph_varpool_node *
cgraph_varpool_node_for_asm (tree asmname)
{
  struct cgraph_varpool_node *node;

  for (node = cgraph_varpool_nodes; node ; node = node->next)
    if (decl_assembler_name_equal (node->decl, asmname))
      return node;

  return NULL;
}

/* Set the DECL_ASSEMBLER_NAME and update cgraph hashtables.  */
void
change_decl_assembler_name (tree decl, tree name)
{
  if (!DECL_ASSEMBLER_NAME_SET_P (decl))
    {
      SET_DECL_ASSEMBLER_NAME (decl, name);
      return;
    }
  if (name == DECL_ASSEMBLER_NAME (decl))
    return;

  if (TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl))
      && DECL_RTL_SET_P (decl))
    warning (0, "%D renamed after being referenced in assembly", decl);

  SET_DECL_ASSEMBLER_NAME (decl, name);
}

/* Helper function for finalization code - add node into lists so it will
   be analyzed and compiled.  */
void
cgraph_varpool_enqueue_needed_node (struct cgraph_varpool_node *node)
{
  if (cgraph_varpool_last_needed_node)
    cgraph_varpool_last_needed_node->next_needed = node;
  cgraph_varpool_last_needed_node = node;
  node->next_needed = NULL;
  if (!cgraph_varpool_nodes_queue)
    cgraph_varpool_nodes_queue = node;
  if (!cgraph_varpool_first_unanalyzed_node)
    cgraph_varpool_first_unanalyzed_node = node;
  notice_global_symbol (node->decl);
}

/* Reset the queue of needed nodes.  */
void
cgraph_varpool_reset_queue (void)
{
  cgraph_varpool_last_needed_node = NULL;
  cgraph_varpool_nodes_queue = NULL;
  cgraph_varpool_first_unanalyzed_node = NULL;
}

/* Notify finalize_compilation_unit that given node is reachable
   or needed.  */
void
cgraph_varpool_mark_needed_node (struct cgraph_varpool_node *node)
{
  if (!node->needed && node->finalized
      && !TREE_ASM_WRITTEN (node->decl))
    cgraph_varpool_enqueue_needed_node (node);
  node->needed = 1;
}

/* Determine if variable DECL is needed.  That is, visible to something
   either outside this translation unit, something magic in the system
   configury, or (if not doing unit-at-a-time) to something we haven't
   seen yet.  */

bool
decide_is_variable_needed (struct cgraph_varpool_node *node, tree decl)
{
  /* If the user told us it is used, then it must be so.  */
  if (node->externally_visible)
    return true;
  if (!flag_unit_at_a_time
      && lookup_attribute ("used", DECL_ATTRIBUTES (decl)))
    return true;

  /* ??? If the assembler name is set by hand, it is possible to assemble
     the name later after finalizing the function and the fact is noticed
     in assemble_name then.  This is arguably a bug.  */
  if (DECL_ASSEMBLER_NAME_SET_P (decl)
      && TREE_SYMBOL_REFERENCED (DECL_ASSEMBLER_NAME (decl)))
    return true;

  /* If we decided it was needed before, but at the time we didn't have
     the definition available, then it's still needed.  */
  if (node->needed)
    return true;

  /* Externally visible variables must be output.  The exception is
     COMDAT variables that must be output only when they are needed.  */
  if (TREE_PUBLIC (decl) && !flag_whole_program && !DECL_COMDAT (decl)
      && !DECL_EXTERNAL (decl))
    return true;

  /* When not reordering top level variables, we have to assume that
     we are going to keep everything.  */
  if (flag_unit_at_a_time && flag_toplevel_reorder)
    return false;

  /* We want to emit COMDAT variables only when absolutely necessary.  */
  if (DECL_COMDAT (decl))
    return false;
  return true;
}

void
cgraph_varpool_finalize_decl (tree decl)
{
  struct cgraph_varpool_node *node = cgraph_varpool_node (decl);

  /* The first declaration of a variable that comes through this function
     decides whether it is global (in C, has external linkage)
     or local (in C, has internal linkage).  So do nothing more
     if this function has already run.  */
  if (node->finalized)
    {
      if (cgraph_global_info_ready || (!flag_unit_at_a_time && !flag_openmp))
	cgraph_varpool_assemble_pending_decls ();
      return;
    }
  if (node->needed)
    cgraph_varpool_enqueue_needed_node (node);
  node->finalized = true;

  if (decide_is_variable_needed (node, decl))
    cgraph_varpool_mark_needed_node (node);
  /* Since we reclaim unreachable nodes at the end of every language
     level unit, we need to be conservative about possible entry points
     there.  */
  else if (TREE_PUBLIC (decl) && !DECL_COMDAT (decl) && !DECL_EXTERNAL (decl))
    cgraph_varpool_mark_needed_node (node);
  if (cgraph_global_info_ready || (!flag_unit_at_a_time && !flag_openmp))
    cgraph_varpool_assemble_pending_decls ();
}

/* Add a top-level asm statement to the list.  */

struct cgraph_asm_node *
cgraph_add_asm_node (tree asm_str)
{
  struct cgraph_asm_node *node;

  node = GGC_CNEW (struct cgraph_asm_node);
  node->asm_str = asm_str;
  node->order = cgraph_order++;
  node->next = NULL;
  if (cgraph_asm_nodes == NULL)
    cgraph_asm_nodes = node;
  else
    cgraph_asm_last_node->next = node;
  cgraph_asm_last_node = node;
  return node;
}

/* Return true when the DECL can possibly be inlined.  */
bool
cgraph_function_possibly_inlined_p (tree decl)
{
  if (!cgraph_global_info_ready)
    return (DECL_INLINE (decl) && !flag_really_no_inline);
  return DECL_POSSIBLY_INLINED (decl);
}

/* Create clone of E in the node N represented by CALL_EXPR the callgraph.  */
struct cgraph_edge *
cgraph_clone_edge (struct cgraph_edge *e, struct cgraph_node *n,
		   tree call_stmt, gcov_type count_scale, int loop_nest,
		   bool update_original)
{
  struct cgraph_edge *new;

  new = cgraph_create_edge (n, e->callee, call_stmt,
			    e->count * count_scale / REG_BR_PROB_BASE,
			    e->loop_nest + loop_nest);

  new->inline_failed = e->inline_failed;
  if (update_original)
    {
      e->count -= new->count;
      if (e->count < 0)
	e->count = 0;
    }
  return new;
}

/* Create node representing clone of N executed COUNT times.  Decrease
   the execution counts from original node too.

   When UPDATE_ORIGINAL is true, the counts are subtracted from the original
   function's profile to reflect the fact that part of execution is handled
   by node.  */
struct cgraph_node *
cgraph_clone_node (struct cgraph_node *n, gcov_type count, int loop_nest,
		   bool update_original)
{
  struct cgraph_node *new = cgraph_create_node ();
  struct cgraph_edge *e;
  gcov_type count_scale;

  new->decl = n->decl;
  new->origin = n->origin;
  if (new->origin)
    {
      new->next_nested = new->origin->nested;
      new->origin->nested = new;
    }
  new->analyzed = n->analyzed;
  new->local = n->local;
  new->global = n->global;
  new->rtl = n->rtl;
  new->master_clone = n->master_clone;
  new->count = count;
  if (n->count)
    count_scale = new->count * REG_BR_PROB_BASE / n->count;
  else
    count_scale = 0;
  if (update_original)
    {
      n->count -= count;
      if (n->count < 0)
	n->count = 0;
    }

  for (e = n->callees;e; e=e->next_callee)
    cgraph_clone_edge (e, new, e->call_stmt, count_scale, loop_nest,
		       update_original);

  new->next_clone = n->next_clone;
  new->prev_clone = n;
  n->next_clone = new;
  if (new->next_clone)
    new->next_clone->prev_clone = new;

  return new;
}

/* Return true if N is an master_clone, (see cgraph_master_clone).  */

bool
cgraph_is_master_clone (struct cgraph_node *n)
{
  return (n == cgraph_master_clone (n));
}

struct cgraph_node *
cgraph_master_clone (struct cgraph_node *n)
{
  enum availability avail = cgraph_function_body_availability (n);

  if (avail == AVAIL_NOT_AVAILABLE || avail == AVAIL_OVERWRITABLE)
    return NULL;

  if (!n->master_clone)
    n->master_clone = cgraph_node (n->decl);

  return n->master_clone;
}

/* NODE is no longer nested function; update cgraph accordingly.  */
void
cgraph_unnest_node (struct cgraph_node *node)
{
  struct cgraph_node **node2 = &node->origin->nested;
  gcc_assert (node->origin);

  while (*node2 != node)
    node2 = &(*node2)->next_nested;
  *node2 = node->next_nested;
  node->origin = NULL;
}

/* Return function availability.  See cgraph.h for description of individual
   return values.  */
enum availability
cgraph_function_body_availability (struct cgraph_node *node)
{
  enum availability avail;
  gcc_assert (cgraph_function_flags_ready);
  if (!node->analyzed)
    avail = AVAIL_NOT_AVAILABLE;
  else if (node->local.local)
    avail = AVAIL_LOCAL;
  else if (node->local.externally_visible)
    avail = AVAIL_AVAILABLE;

  /* If the function can be overwritten, return OVERWRITABLE.  Take
     care at least of two notable extensions - the COMDAT functions
     used to share template instantiations in C++ (this is symmetric
     to code cp_cannot_inline_tree_fn and probably shall be shared and
     the inlinability hooks completely eliminated).

     ??? Does the C++ one definition rule allow us to always return
     AVAIL_AVAILABLE here?  That would be good reason to preserve this
     hook Similarly deal with extern inline functions - this is again
     necessary to get C++ shared functions having keyed templates
     right and in the C extension documentation we probably should
     document the requirement of both versions of function (extern
     inline and offline) having same side effect characteristics as
     good optimization is what this optimization is about.  */

  else if (!(*targetm.binds_local_p) (node->decl)
	   && !DECL_COMDAT (node->decl) && !DECL_EXTERNAL (node->decl))
    avail = AVAIL_OVERWRITABLE;
  else avail = AVAIL_AVAILABLE;

  return avail;
}

/* Return variable availability.  See cgraph.h for description of individual
   return values.  */
enum availability
cgraph_variable_initializer_availability (struct cgraph_varpool_node *node)
{
  gcc_assert (cgraph_function_flags_ready);
  if (!node->finalized)
    return AVAIL_NOT_AVAILABLE;
  if (!TREE_PUBLIC (node->decl))
    return AVAIL_AVAILABLE;
  /* If the variable can be overwritten, return OVERWRITABLE.  Takes
     care of at least two notable extensions - the COMDAT variables
     used to share template instantiations in C++.  */
  if (!(*targetm.binds_local_p) (node->decl) && !DECL_COMDAT (node->decl))
    return AVAIL_OVERWRITABLE;
  return AVAIL_AVAILABLE;
}


/* Add the function FNDECL to the call graph.  FNDECL is assumed to be
   in low GIMPLE form and ready to be processed by cgraph_finalize_function.

   When operating in unit-at-a-time, a new callgraph node is added to
   CGRAPH_EXPAND_QUEUE, which is processed after all the original
   functions in the call graph .

   When not in unit-at-a-time, the new callgraph node is added to
   CGRAPH_NODES_QUEUE for cgraph_assemble_pending_functions to
   process.  */

void
cgraph_add_new_function (tree fndecl)
{
  struct cgraph_node *n = cgraph_node (fndecl);
  n->next_needed = cgraph_expand_queue;
  cgraph_expand_queue = n;
}

#include "gt-cgraph.h"
