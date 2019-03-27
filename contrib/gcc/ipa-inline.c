/* Inlining decision heuristics.
   Copyright (C) 2003, 2004 Free Software Foundation, Inc.
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

/*  Inlining decision heuristics

    We separate inlining decisions from the inliner itself and store it
    inside callgraph as so called inline plan.  Refer to cgraph.c
    documentation about particular representation of inline plans in the
    callgraph.

    There are three major parts of this file:

    cgraph_mark_inline implementation

      This function allows to mark given call inline and performs necessary
      modifications of cgraph (production of the clones and updating overall
      statistics)

    inlining heuristics limits

      These functions allow to check that particular inlining is allowed
      by the limits specified by user (allowed function growth, overall unit
      growth and so on).

    inlining heuristics

      This is implementation of IPA pass aiming to get as much of benefit
      from inlining obeying the limits checked above.

      The implementation of particular heuristics is separated from
      the rest of code to make it easier to replace it with more complicated
      implementation in the future.  The rest of inlining code acts as a
      library aimed to modify the callgraph and verify that the parameters
      on code size growth fits.

      To mark given call inline, use cgraph_mark_inline function, the
      verification is performed by cgraph_default_inline_p and
      cgraph_check_inline_limits.

      The heuristics implements simple knapsack style algorithm ordering
      all functions by their "profitability" (estimated by code size growth)
      and inlining them in priority order.

      cgraph_decide_inlining implements heuristics taking whole callgraph
      into account, while cgraph_decide_inlining_incrementally considers
      only one function at a time and is used in non-unit-at-a-time mode.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "tree-inline.h"
#include "langhooks.h"
#include "flags.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "timevar.h"
#include "params.h"
#include "fibheap.h"
#include "intl.h"
#include "tree-pass.h"
#include "hashtab.h"
#include "coverage.h"
#include "ggc.h"

/* Statistics we collect about inlining algorithm.  */
static int ncalls_inlined;
static int nfunctions_inlined;
static int initial_insns;
static int overall_insns;
static int max_insns;
static gcov_type max_count;

/* Estimate size of the function after inlining WHAT into TO.  */

static int
cgraph_estimate_size_after_inlining (int times, struct cgraph_node *to,
				     struct cgraph_node *what)
{
  int size;
  tree fndecl = what->decl, arg;
  int call_insns = PARAM_VALUE (PARAM_INLINE_CALL_COST);

  for (arg = DECL_ARGUMENTS (fndecl); arg; arg = TREE_CHAIN (arg))
    call_insns += estimate_move_cost (TREE_TYPE (arg));
  size = (what->global.insns - call_insns) * times + to->global.insns;
  gcc_assert (size >= 0);
  return size;
}

/* E is expected to be an edge being inlined.  Clone destination node of
   the edge and redirect it to the new clone.
   DUPLICATE is used for bookkeeping on whether we are actually creating new
   clones or re-using node originally representing out-of-line function call.
   */
void
cgraph_clone_inlined_nodes (struct cgraph_edge *e, bool duplicate, bool update_original)
{
  if (duplicate)
    {
      /* We may eliminate the need for out-of-line copy to be output.
	 In that case just go ahead and re-use it.  */
      if (!e->callee->callers->next_caller
	  && !e->callee->needed
	  && flag_unit_at_a_time)
	{
	  gcc_assert (!e->callee->global.inlined_to);
	  if (DECL_SAVED_TREE (e->callee->decl))
	    overall_insns -= e->callee->global.insns, nfunctions_inlined++;
	  duplicate = false;
	}
      else
	{
	  struct cgraph_node *n;
	  n = cgraph_clone_node (e->callee, e->count, e->loop_nest, 
				 update_original);
	  cgraph_redirect_edge_callee (e, n);
	}
    }

  if (e->caller->global.inlined_to)
    e->callee->global.inlined_to = e->caller->global.inlined_to;
  else
    e->callee->global.inlined_to = e->caller;

  /* Recursively clone all bodies.  */
  for (e = e->callee->callees; e; e = e->next_callee)
    if (!e->inline_failed)
      cgraph_clone_inlined_nodes (e, duplicate, update_original);
}

/* Mark edge E as inlined and update callgraph accordingly. 
   UPDATE_ORIGINAL specify whether profile of original function should be
   updated. */

void
cgraph_mark_inline_edge (struct cgraph_edge *e, bool update_original)
{
  int old_insns = 0, new_insns = 0;
  struct cgraph_node *to = NULL, *what;

  if (e->callee->inline_decl)
    cgraph_redirect_edge_callee (e, cgraph_node (e->callee->inline_decl));

  gcc_assert (e->inline_failed);
  e->inline_failed = NULL;

  if (!e->callee->global.inlined && flag_unit_at_a_time)
    DECL_POSSIBLY_INLINED (e->callee->decl) = true;
  e->callee->global.inlined = true;

  cgraph_clone_inlined_nodes (e, true, update_original);

  what = e->callee;

  /* Now update size of caller and all functions caller is inlined into.  */
  for (;e && !e->inline_failed; e = e->caller->callers)
    {
      old_insns = e->caller->global.insns;
      new_insns = cgraph_estimate_size_after_inlining (1, e->caller,
						       what);
      gcc_assert (new_insns >= 0);
      to = e->caller;
      to->global.insns = new_insns;
    }
  gcc_assert (what->global.inlined_to == to);
  if (new_insns > old_insns)
    overall_insns += new_insns - old_insns;
  ncalls_inlined++;
}

/* Mark all calls of EDGE->CALLEE inlined into EDGE->CALLER.
   Return following unredirected edge in the list of callers
   of EDGE->CALLEE  */

static struct cgraph_edge *
cgraph_mark_inline (struct cgraph_edge *edge)
{
  struct cgraph_node *to = edge->caller;
  struct cgraph_node *what = edge->callee;
  struct cgraph_edge *e, *next;
  int times = 0;

  /* Look for all calls, mark them inline and clone recursively
     all inlined functions.  */
  for (e = what->callers; e; e = next)
    {
      next = e->next_caller;
      if (e->caller == to && e->inline_failed)
	{
          cgraph_mark_inline_edge (e, true);
	  if (e == edge)
	    edge = next;
	  times++;
	}
    }
  gcc_assert (times);
  return edge;
}

/* Estimate the growth caused by inlining NODE into all callees.  */

static int
cgraph_estimate_growth (struct cgraph_node *node)
{
  int growth = 0;
  struct cgraph_edge *e;
  if (node->global.estimated_growth != INT_MIN)
    return node->global.estimated_growth;

  for (e = node->callers; e; e = e->next_caller)
    if (e->inline_failed)
      growth += (cgraph_estimate_size_after_inlining (1, e->caller, node)
		 - e->caller->global.insns);

  /* ??? Wrong for self recursive functions or cases where we decide to not
     inline for different reasons, but it is not big deal as in that case
     we will keep the body around, but we will also avoid some inlining.  */
  if (!node->needed && !DECL_EXTERNAL (node->decl))
    growth -= node->global.insns;

  node->global.estimated_growth = growth;
  return growth;
}

/* Return false when inlining WHAT into TO is not good idea
   as it would cause too large growth of function bodies.  
   When ONE_ONLY is true, assume that only one call site is going
   to be inlined, otherwise figure out how many call sites in
   TO calls WHAT and verify that all can be inlined.
   */

static bool
cgraph_check_inline_limits (struct cgraph_node *to, struct cgraph_node *what,
			    const char **reason, bool one_only)
{
  int times = 0;
  struct cgraph_edge *e;
  int newsize;
  int limit;

  if (one_only)
    times = 1;
  else
    for (e = to->callees; e; e = e->next_callee)
      if (e->callee == what)
	times++;

  if (to->global.inlined_to)
    to = to->global.inlined_to;

  /* When inlining large function body called once into small function,
     take the inlined function as base for limiting the growth.  */
  if (to->local.self_insns > what->local.self_insns)
    limit = to->local.self_insns;
  else
    limit = what->local.self_insns;

  limit += limit * PARAM_VALUE (PARAM_LARGE_FUNCTION_GROWTH) / 100;

  /* Check the size after inlining against the function limits.  But allow
     the function to shrink if it went over the limits by forced inlining.  */
  newsize = cgraph_estimate_size_after_inlining (times, to, what);
  if (newsize >= to->global.insns
      && newsize > PARAM_VALUE (PARAM_LARGE_FUNCTION_INSNS)
      && newsize > limit)
    {
      if (reason)
        *reason = N_("--param large-function-growth limit reached");
      return false;
    }
  return true;
}

/* Return true when function N is small enough to be inlined.  */

bool
cgraph_default_inline_p (struct cgraph_node *n, const char **reason)
{
  tree decl = n->decl;

  if (n->inline_decl)
    decl = n->inline_decl;
  if (!DECL_INLINE (decl))
    {
      if (reason)
	*reason = N_("function not inlinable");
      return false;
    }

  if (!DECL_STRUCT_FUNCTION (decl)->cfg)
    {
      if (reason)
	*reason = N_("function body not available");
      return false;
    }

  if (DECL_DECLARED_INLINE_P (decl))
    {
      if (n->global.insns >= MAX_INLINE_INSNS_SINGLE)
	{
	  if (reason)
	    *reason = N_("--param max-inline-insns-single limit reached");
	  return false;
	}
    }
  else
    {
      if (n->global.insns >= MAX_INLINE_INSNS_AUTO)
	{
	  if (reason)
	    *reason = N_("--param max-inline-insns-auto limit reached");
	  return false;
	}
    }

  return true;
}

/* Return true when inlining WHAT would create recursive inlining.
   We call recursive inlining all cases where same function appears more than
   once in the single recursion nest path in the inline graph.  */

static bool
cgraph_recursive_inlining_p (struct cgraph_node *to,
			     struct cgraph_node *what,
			     const char **reason)
{
  bool recursive;
  if (to->global.inlined_to)
    recursive = what->decl == to->global.inlined_to->decl;
  else
    recursive = what->decl == to->decl;
  /* Marking recursive function inline has sane semantic and thus we should
     not warn on it.  */
  if (recursive && reason)
    *reason = (what->local.disregard_inline_limits
	       ? N_("recursive inlining") : "");
  return recursive;
}

/* Return true if the call can be hot.  */
static bool
cgraph_maybe_hot_edge_p (struct cgraph_edge *edge)
{
  if (profile_info && flag_branch_probabilities
      && (edge->count
	  <= profile_info->sum_max / PARAM_VALUE (HOT_BB_COUNT_FRACTION)))
    return false;
  return true;
}

/* A cost model driving the inlining heuristics in a way so the edges with
   smallest badness are inlined first.  After each inlining is performed
   the costs of all caller edges of nodes affected are recomputed so the
   metrics may accurately depend on values such as number of inlinable callers
   of the function or function body size.

   With profiling we use number of executions of each edge to drive the cost.
   We also should distinguish hot and cold calls where the cold calls are
   inlined into only when code size is overall improved.  
   */

static int
cgraph_edge_badness (struct cgraph_edge *edge)
{
  if (max_count)
    {
      int growth =
	cgraph_estimate_size_after_inlining (1, edge->caller, edge->callee);
      growth -= edge->caller->global.insns;

      /* Always prefer inlining saving code size.  */
      if (growth <= 0)
	return INT_MIN - growth;
      return ((int)((double)edge->count * INT_MIN / max_count)) / growth;
    }
  else
  {
    int nest = MIN (edge->loop_nest, 8);
    int badness = cgraph_estimate_growth (edge->callee) * 256;

    /* Decrease badness if call is nested.  */
    if (badness > 0)    
      badness >>= nest;
    else
      badness <<= nest;

    /* Make recursive inlining happen always after other inlining is done.  */
    if (cgraph_recursive_inlining_p (edge->caller, edge->callee, NULL))
      return badness + 1;
    else
      return badness;
  }
}

/* Recompute heap nodes for each of caller edge.  */

static void
update_caller_keys (fibheap_t heap, struct cgraph_node *node,
		    bitmap updated_nodes)
{
  struct cgraph_edge *edge;
  const char *failed_reason;

  if (!node->local.inlinable || node->local.disregard_inline_limits
      || node->global.inlined_to)
    return;
  if (bitmap_bit_p (updated_nodes, node->uid))
    return;
  bitmap_set_bit (updated_nodes, node->uid);
  node->global.estimated_growth = INT_MIN;

  if (!node->local.inlinable)
    return;
  /* Prune out edges we won't inline into anymore.  */
  if (!cgraph_default_inline_p (node, &failed_reason))
    {
      for (edge = node->callers; edge; edge = edge->next_caller)
	if (edge->aux)
	  {
	    fibheap_delete_node (heap, edge->aux);
	    edge->aux = NULL;
	    if (edge->inline_failed)
	      edge->inline_failed = failed_reason;
	  }
      return;
    }

  for (edge = node->callers; edge; edge = edge->next_caller)
    if (edge->inline_failed)
      {
	int badness = cgraph_edge_badness (edge);
	if (edge->aux)
	  {
	    fibnode_t n = edge->aux;
	    gcc_assert (n->data == edge);
	    if (n->key == badness)
	      continue;

	    /* fibheap_replace_key only increase the keys.  */
	    if (fibheap_replace_key (heap, n, badness))
	      continue;
	    fibheap_delete_node (heap, edge->aux);
	  }
	edge->aux = fibheap_insert (heap, badness, edge);
      }
}

/* Recompute heap nodes for each of caller edges of each of callees.  */

static void
update_callee_keys (fibheap_t heap, struct cgraph_node *node,
		    bitmap updated_nodes)
{
  struct cgraph_edge *e;
  node->global.estimated_growth = INT_MIN;

  for (e = node->callees; e; e = e->next_callee)
    if (e->inline_failed)
      update_caller_keys (heap, e->callee, updated_nodes);
    else if (!e->inline_failed)
      update_callee_keys (heap, e->callee, updated_nodes);
}

/* Enqueue all recursive calls from NODE into priority queue depending on
   how likely we want to recursively inline the call.  */

static void
lookup_recursive_calls (struct cgraph_node *node, struct cgraph_node *where,
			fibheap_t heap)
{
  static int priority;
  struct cgraph_edge *e;
  for (e = where->callees; e; e = e->next_callee)
    if (e->callee == node)
      {
	/* When profile feedback is available, prioritize by expected number
	   of calls.  Without profile feedback we maintain simple queue
	   to order candidates via recursive depths.  */
        fibheap_insert (heap,
			!max_count ? priority++
		        : -(e->count / ((max_count + (1<<24) - 1) / (1<<24))),
		        e);
      }
  for (e = where->callees; e; e = e->next_callee)
    if (!e->inline_failed)
      lookup_recursive_calls (node, e->callee, heap);
}

/* Find callgraph nodes closing a circle in the graph.  The
   resulting hashtab can be used to avoid walking the circles.
   Uses the cgraph nodes ->aux field which needs to be zero
   before and will be zero after operation.  */

static void
cgraph_find_cycles (struct cgraph_node *node, htab_t cycles)
{
  struct cgraph_edge *e;

  if (node->aux)
    {
      void **slot;
      slot = htab_find_slot (cycles, node, INSERT);
      if (!*slot)
	{
	  if (dump_file)
	    fprintf (dump_file, "Cycle contains %s\n", cgraph_node_name (node));
	  *slot = node;
	}
      return;
    }

  node->aux = node;
  for (e = node->callees; e; e = e->next_callee)
    cgraph_find_cycles (e->callee, cycles); 
  node->aux = 0;
}

/* Flatten the cgraph node.  We have to be careful in recursing
   as to not run endlessly in circles of the callgraph.
   We do so by using a hashtab of cycle entering nodes as generated
   by cgraph_find_cycles.  */

static void
cgraph_flatten_node (struct cgraph_node *node, htab_t cycles)
{
  struct cgraph_edge *e;

  for (e = node->callees; e; e = e->next_callee)
    {
      /* Inline call, if possible, and recurse.  Be sure we are not
	 entering callgraph circles here.  */
      if (e->inline_failed
	  && e->callee->local.inlinable
	  && !cgraph_recursive_inlining_p (node, e->callee,
				  	   &e->inline_failed)
	  && !htab_find (cycles, e->callee))
	{
	  if (dump_file)
    	    fprintf (dump_file, " inlining %s", cgraph_node_name (e->callee));
          cgraph_mark_inline_edge (e, true);
	  cgraph_flatten_node (e->callee, cycles);
	}
      else if (dump_file)
	fprintf (dump_file, " !inlining %s", cgraph_node_name (e->callee));
    }
}

/* Decide on recursive inlining: in the case function has recursive calls,
   inline until body size reaches given argument.  */

static bool
cgraph_decide_recursive_inlining (struct cgraph_node *node)
{
  int limit = PARAM_VALUE (PARAM_MAX_INLINE_INSNS_RECURSIVE_AUTO);
  int max_depth = PARAM_VALUE (PARAM_MAX_INLINE_RECURSIVE_DEPTH_AUTO);
  int probability = PARAM_VALUE (PARAM_MIN_INLINE_RECURSIVE_PROBABILITY);
  fibheap_t heap;
  struct cgraph_edge *e;
  struct cgraph_node *master_clone, *next;
  int depth = 0;
  int n = 0;

  if (DECL_DECLARED_INLINE_P (node->decl))
    {
      limit = PARAM_VALUE (PARAM_MAX_INLINE_INSNS_RECURSIVE);
      max_depth = PARAM_VALUE (PARAM_MAX_INLINE_RECURSIVE_DEPTH);
    }

  /* Make sure that function is small enough to be considered for inlining.  */
  if (!max_depth
      || cgraph_estimate_size_after_inlining (1, node, node)  >= limit)
    return false;
  heap = fibheap_new ();
  lookup_recursive_calls (node, node, heap);
  if (fibheap_empty (heap))
    {
      fibheap_delete (heap);
      return false;
    }

  if (dump_file)
    fprintf (dump_file, 
	     "  Performing recursive inlining on %s\n",
	     cgraph_node_name (node));

  /* We need original clone to copy around.  */
  master_clone = cgraph_clone_node (node, node->count, 1, false);
  master_clone->needed = true;
  for (e = master_clone->callees; e; e = e->next_callee)
    if (!e->inline_failed)
      cgraph_clone_inlined_nodes (e, true, false);

  /* Do the inlining and update list of recursive call during process.  */
  while (!fibheap_empty (heap)
	 && (cgraph_estimate_size_after_inlining (1, node, master_clone)
	     <= limit))
    {
      struct cgraph_edge *curr = fibheap_extract_min (heap);
      struct cgraph_node *cnode;

      depth = 1;
      for (cnode = curr->caller;
	   cnode->global.inlined_to; cnode = cnode->callers->caller)
	if (node->decl == curr->callee->decl)
	  depth++;
      if (depth > max_depth)
	{
          if (dump_file)
	    fprintf (dump_file, 
		     "   maxmal depth reached\n");
	  continue;
	}

      if (max_count)
	{
          if (!cgraph_maybe_hot_edge_p (curr))
	    {
	      if (dump_file)
		fprintf (dump_file, "   Not inlining cold call\n");
	      continue;
	    }
          if (curr->count * 100 / node->count < probability)
	    {
	      if (dump_file)
		fprintf (dump_file, 
			 "   Probability of edge is too small\n");
	      continue;
	    }
	}

      if (dump_file)
	{
	  fprintf (dump_file, 
		   "   Inlining call of depth %i", depth);
	  if (node->count)
	    {
	      fprintf (dump_file, " called approx. %.2f times per call",
		       (double)curr->count / node->count);
	    }
	  fprintf (dump_file, "\n");
	}
      cgraph_redirect_edge_callee (curr, master_clone);
      cgraph_mark_inline_edge (curr, false);
      lookup_recursive_calls (node, curr->callee, heap);
      n++;
    }
  if (!fibheap_empty (heap) && dump_file)
    fprintf (dump_file, "    Recursive inlining growth limit met.\n");

  fibheap_delete (heap);
  if (dump_file)
    fprintf (dump_file, 
	     "\n   Inlined %i times, body grown from %i to %i insns\n", n,
	     master_clone->global.insns, node->global.insns);

  /* Remove master clone we used for inlining.  We rely that clones inlined
     into master clone gets queued just before master clone so we don't
     need recursion.  */
  for (node = cgraph_nodes; node != master_clone;
       node = next)
    {
      next = node->next;
      if (node->global.inlined_to == master_clone)
	cgraph_remove_node (node);
    }
  cgraph_remove_node (master_clone);
  /* FIXME: Recursive inlining actually reduces number of calls of the
     function.  At this place we should probably walk the function and
     inline clones and compensate the counts accordingly.  This probably
     doesn't matter much in practice.  */
  return n > 0;
}

/* Set inline_failed for all callers of given function to REASON.  */

static void
cgraph_set_inline_failed (struct cgraph_node *node, const char *reason)
{
  struct cgraph_edge *e;

  if (dump_file)
    fprintf (dump_file, "Inlining failed: %s\n", reason);
  for (e = node->callers; e; e = e->next_caller)
    if (e->inline_failed)
      e->inline_failed = reason;
}

/* We use greedy algorithm for inlining of small functions:
   All inline candidates are put into prioritized heap based on estimated
   growth of the overall number of instructions and then update the estimates.

   INLINED and INLINED_CALEES are just pointers to arrays large enough
   to be passed to cgraph_inlined_into and cgraph_inlined_callees.  */

static void
cgraph_decide_inlining_of_small_functions (void)
{
  struct cgraph_node *node;
  struct cgraph_edge *edge;
  const char *failed_reason;
  fibheap_t heap = fibheap_new ();
  bitmap updated_nodes = BITMAP_ALLOC (NULL);

  if (dump_file)
    fprintf (dump_file, "\nDeciding on smaller functions:\n");

  /* Put all inline candidates into the heap.  */

  for (node = cgraph_nodes; node; node = node->next)
    {
      if (!node->local.inlinable || !node->callers
	  || node->local.disregard_inline_limits)
	continue;
      if (dump_file)
	fprintf (dump_file, "Considering inline candidate %s.\n", cgraph_node_name (node));

      node->global.estimated_growth = INT_MIN;
      if (!cgraph_default_inline_p (node, &failed_reason))
	{
	  cgraph_set_inline_failed (node, failed_reason);
	  continue;
	}

      for (edge = node->callers; edge; edge = edge->next_caller)
	if (edge->inline_failed)
	  {
	    gcc_assert (!edge->aux);
	    edge->aux = fibheap_insert (heap, cgraph_edge_badness (edge), edge);
	  }
    }
  while (overall_insns <= max_insns && (edge = fibheap_extract_min (heap)))
    {
      int old_insns = overall_insns;
      struct cgraph_node *where;
      int growth =
	cgraph_estimate_size_after_inlining (1, edge->caller, edge->callee);

      growth -= edge->caller->global.insns;

      if (dump_file)
	{
	  fprintf (dump_file, 
		   "\nConsidering %s with %i insns\n",
		   cgraph_node_name (edge->callee),
		   edge->callee->global.insns);
	  fprintf (dump_file, 
		   " to be inlined into %s\n"
		   " Estimated growth after inlined into all callees is %+i insns.\n"
		   " Estimated badness is %i.\n",
		   cgraph_node_name (edge->caller),
		   cgraph_estimate_growth (edge->callee),
		   cgraph_edge_badness (edge));
	  if (edge->count)
	    fprintf (dump_file," Called "HOST_WIDEST_INT_PRINT_DEC"x\n", edge->count);
	}
      gcc_assert (edge->aux);
      edge->aux = NULL;
      if (!edge->inline_failed)
	continue;

      /* When not having profile info ready we don't weight by any way the
         position of call in procedure itself.  This means if call of
	 function A from function B seems profitable to inline, the recursive
	 call of function A in inline copy of A in B will look profitable too
	 and we end up inlining until reaching maximal function growth.  This
	 is not good idea so prohibit the recursive inlining.

	 ??? When the frequencies are taken into account we might not need this
	 restriction.   */
      if (!max_count)
	{
	  where = edge->caller;
	  while (where->global.inlined_to)
	    {
	      if (where->decl == edge->callee->decl)
		break;
	      where = where->callers->caller;
	    }
	  if (where->global.inlined_to)
	    {
	      edge->inline_failed
		= (edge->callee->local.disregard_inline_limits ? N_("recursive inlining") : "");
	      if (dump_file)
		fprintf (dump_file, " inline_failed:Recursive inlining performed only for function itself.\n");
	      continue;
	    }
	}

      if (!cgraph_maybe_hot_edge_p (edge) && growth > 0)
	{
          if (!cgraph_recursive_inlining_p (edge->caller, edge->callee,
				            &edge->inline_failed))
	    {
	      edge->inline_failed = 
		N_("call is unlikely");
	      if (dump_file)
		fprintf (dump_file, " inline_failed:%s.\n", edge->inline_failed);
	    }
	  continue;
	}
      if (!cgraph_default_inline_p (edge->callee, &edge->inline_failed))
	{
          if (!cgraph_recursive_inlining_p (edge->caller, edge->callee,
				            &edge->inline_failed))
	    {
	      if (dump_file)
		fprintf (dump_file, " inline_failed:%s.\n", edge->inline_failed);
	    }
	  continue;
	}
      if (cgraph_recursive_inlining_p (edge->caller, edge->callee,
				       &edge->inline_failed))
	{
	  where = edge->caller;
	  if (where->global.inlined_to)
	    where = where->global.inlined_to;
	  if (!cgraph_decide_recursive_inlining (where))
	    continue;
          update_callee_keys (heap, where, updated_nodes);
	}
      else
	{
	  struct cgraph_node *callee;
	  if (!cgraph_check_inline_limits (edge->caller, edge->callee,
					   &edge->inline_failed, true))
	    {
	      if (dump_file)
		fprintf (dump_file, " Not inlining into %s:%s.\n",
			 cgraph_node_name (edge->caller), edge->inline_failed);
	      continue;
	    }
	  callee = edge->callee;
	  cgraph_mark_inline_edge (edge, true);
	  update_callee_keys (heap, callee, updated_nodes);
	}
      where = edge->caller;
      if (where->global.inlined_to)
	where = where->global.inlined_to;

      /* Our profitability metric can depend on local properties
	 such as number of inlinable calls and size of the function body.
	 After inlining these properties might change for the function we
	 inlined into (since it's body size changed) and for the functions
	 called by function we inlined (since number of it inlinable callers
	 might change).  */
      update_caller_keys (heap, where, updated_nodes);
      bitmap_clear (updated_nodes);

      if (dump_file)
	{
	  fprintf (dump_file, 
		   " Inlined into %s which now has %i insns,"
		   "net change of %+i insns.\n",
		   cgraph_node_name (edge->caller),
		   edge->caller->global.insns,
		   overall_insns - old_insns);
	}
    }
  while ((edge = fibheap_extract_min (heap)) != NULL)
    {
      gcc_assert (edge->aux);
      edge->aux = NULL;
      if (!edge->callee->local.disregard_inline_limits && edge->inline_failed
          && !cgraph_recursive_inlining_p (edge->caller, edge->callee,
				           &edge->inline_failed))
	edge->inline_failed = N_("--param inline-unit-growth limit reached");
    }
  fibheap_delete (heap);
  BITMAP_FREE (updated_nodes);
}

/* Decide on the inlining.  We do so in the topological order to avoid
   expenses on updating data structures.  */

static unsigned int
cgraph_decide_inlining (void)
{
  struct cgraph_node *node;
  int nnodes;
  struct cgraph_node **order =
    XCNEWVEC (struct cgraph_node *, cgraph_n_nodes);
  int old_insns = 0;
  int i;

  timevar_push (TV_INLINE_HEURISTICS);
  max_count = 0;
  for (node = cgraph_nodes; node; node = node->next)
    if (node->analyzed && (node->needed || node->reachable))
      {
	struct cgraph_edge *e;

	/* At the moment, no IPA passes change function bodies before inlining.
	   Save some time by not recomputing function body sizes if early inlining
	   already did so.  */
	if (!flag_early_inlining)
	  node->local.self_insns = node->global.insns
	     = estimate_num_insns (node->decl);

	initial_insns += node->local.self_insns;
	gcc_assert (node->local.self_insns == node->global.insns);
	for (e = node->callees; e; e = e->next_callee)
	  if (max_count < e->count)
	    max_count = e->count;
      }
  overall_insns = initial_insns;
  gcc_assert (!max_count || (profile_info && flag_branch_probabilities));

  max_insns = overall_insns;
  if (max_insns < PARAM_VALUE (PARAM_LARGE_UNIT_INSNS))
    max_insns = PARAM_VALUE (PARAM_LARGE_UNIT_INSNS);

  max_insns = ((HOST_WIDEST_INT) max_insns
	       * (100 + PARAM_VALUE (PARAM_INLINE_UNIT_GROWTH)) / 100);

  nnodes = cgraph_postorder (order);

  if (dump_file)
    fprintf (dump_file,
	     "\nDeciding on inlining.  Starting with %i insns.\n",
	     initial_insns);

  for (node = cgraph_nodes; node; node = node->next)
    node->aux = 0;

  if (dump_file)
    fprintf (dump_file, "\nInlining always_inline functions:\n");

  /* In the first pass mark all always_inline edges.  Do this with a priority
     so none of our later choices will make this impossible.  */
  for (i = nnodes - 1; i >= 0; i--)
    {
      struct cgraph_edge *e, *next;

      node = order[i];

      /* Handle nodes to be flattened, but don't update overall unit size.  */
      if (lookup_attribute ("flatten", DECL_ATTRIBUTES (node->decl)) != NULL)
        {
	  int old_overall_insns = overall_insns;
	  htab_t cycles;
  	  if (dump_file)
    	    fprintf (dump_file,
	     	     "Flattening %s\n", cgraph_node_name (node));
	  cycles = htab_create (7, htab_hash_pointer, htab_eq_pointer, NULL);
	  cgraph_find_cycles (node, cycles);
	  cgraph_flatten_node (node, cycles);
	  htab_delete (cycles);
	  overall_insns = old_overall_insns;
	  /* We don't need to consider always_inline functions inside the flattened
	     function anymore.  */
	  continue;
        }

      if (!node->local.disregard_inline_limits)
	continue;
      if (dump_file)
	fprintf (dump_file,
		 "\nConsidering %s %i insns (always inline)\n",
		 cgraph_node_name (node), node->global.insns);
      old_insns = overall_insns;
      for (e = node->callers; e; e = next)
	{
	  next = e->next_caller;
	  if (!e->inline_failed)
	    continue;
	  if (cgraph_recursive_inlining_p (e->caller, e->callee,
				  	   &e->inline_failed))
	    continue;
	  cgraph_mark_inline_edge (e, true);
	  if (dump_file)
	    fprintf (dump_file, 
		     " Inlined into %s which now has %i insns.\n",
		     cgraph_node_name (e->caller),
		     e->caller->global.insns);
	}
      if (dump_file)
	fprintf (dump_file, 
		 " Inlined for a net change of %+i insns.\n",
		 overall_insns - old_insns);
    }

  if (!flag_really_no_inline)
    cgraph_decide_inlining_of_small_functions ();

  if (!flag_really_no_inline
      && flag_inline_functions_called_once)
    {
      if (dump_file)
	fprintf (dump_file, "\nDeciding on functions called once:\n");

      /* And finally decide what functions are called once.  */

      for (i = nnodes - 1; i >= 0; i--)
	{
	  node = order[i];

	  if (node->callers && !node->callers->next_caller && !node->needed
	      && node->local.inlinable && node->callers->inline_failed
	      && !DECL_EXTERNAL (node->decl) && !DECL_COMDAT (node->decl))
	    {
	      bool ok = true;
	      struct cgraph_node *node1;

	      /* Verify that we won't duplicate the caller.  */
	      for (node1 = node->callers->caller;
		   node1->callers && !node1->callers->inline_failed
		   && ok; node1 = node1->callers->caller)
		if (node1->callers->next_caller || node1->needed)
		  ok = false;
	      if (ok)
		{
		  if (dump_file)
		    {
		      fprintf (dump_file,
			       "\nConsidering %s %i insns.\n",
			       cgraph_node_name (node), node->global.insns);
		      fprintf (dump_file,
			       " Called once from %s %i insns.\n",
			       cgraph_node_name (node->callers->caller),
			       node->callers->caller->global.insns);
		    }

		  old_insns = overall_insns;

		  if (cgraph_check_inline_limits (node->callers->caller, node,
					  	  NULL, false))
		    {
		      cgraph_mark_inline (node->callers);
		      if (dump_file)
			fprintf (dump_file,
				 " Inlined into %s which now has %i insns"
				 " for a net change of %+i insns.\n",
				 cgraph_node_name (node->callers->caller),
				 node->callers->caller->global.insns,
				 overall_insns - old_insns);
		    }
		  else
		    {
		      if (dump_file)
			fprintf (dump_file,
				 " Inline limit reached, not inlined.\n");
		    }
		}
	    }
	}
    }

  if (dump_file)
    fprintf (dump_file,
	     "\nInlined %i calls, eliminated %i functions, "
	     "%i insns turned to %i insns.\n\n",
	     ncalls_inlined, nfunctions_inlined, initial_insns,
	     overall_insns);
  free (order);
  timevar_pop (TV_INLINE_HEURISTICS);
  return 0;
}

/* Decide on the inlining.  We do so in the topological order to avoid
   expenses on updating data structures.  */

bool
cgraph_decide_inlining_incrementally (struct cgraph_node *node, bool early)
{
  struct cgraph_edge *e;
  bool inlined = false;
  const char *failed_reason;

  /* First of all look for always inline functions.  */
  for (e = node->callees; e; e = e->next_callee)
    if (e->callee->local.disregard_inline_limits
	&& e->inline_failed
        && !cgraph_recursive_inlining_p (node, e->callee, &e->inline_failed)
	/* ??? It is possible that renaming variable removed the function body
	   in duplicate_decls. See gcc.c-torture/compile/20011119-2.c  */
	&& (DECL_SAVED_TREE (e->callee->decl) || e->callee->inline_decl))
      {
        if (dump_file && early)
	  {
	    fprintf (dump_file, "  Early inlining %s",
		     cgraph_node_name (e->callee));
	    fprintf (dump_file, " into %s\n", cgraph_node_name (node));
	  }
	cgraph_mark_inline (e);
	inlined = true;
      }

  /* Now do the automatic inlining.  */
  if (!flag_really_no_inline)
    for (e = node->callees; e; e = e->next_callee)
      if (e->callee->local.inlinable
	  && e->inline_failed
	  && !e->callee->local.disregard_inline_limits
	  && !cgraph_recursive_inlining_p (node, e->callee, &e->inline_failed)
	  && (!early
	      || (cgraph_estimate_size_after_inlining (1, e->caller, e->callee)
	          <= e->caller->global.insns))
	  && cgraph_check_inline_limits (node, e->callee, &e->inline_failed,
	    				 false)
	  && (DECL_SAVED_TREE (e->callee->decl) || e->callee->inline_decl))
	{
	  if (cgraph_default_inline_p (e->callee, &failed_reason))
	    {
	      if (dump_file && early)
		{
		  fprintf (dump_file, "  Early inlining %s",
			   cgraph_node_name (e->callee));
		  fprintf (dump_file, " into %s\n", cgraph_node_name (node));
		}
	      cgraph_mark_inline (e);
	      inlined = true;
	    }
	  else if (!early)
	    e->inline_failed = failed_reason;
	}
  if (early && inlined)
    {
      push_cfun (DECL_STRUCT_FUNCTION (node->decl));
      tree_register_cfg_hooks ();
      current_function_decl = node->decl;
      optimize_inline_calls (current_function_decl);
      node->local.self_insns = node->global.insns;
      current_function_decl = NULL;
      pop_cfun ();
    }
  return inlined;
}

/* When inlining shall be performed.  */
static bool
cgraph_gate_inlining (void)
{
  return flag_inline_trees;
}

struct tree_opt_pass pass_ipa_inline = 
{
  "inline",				/* name */
  cgraph_gate_inlining,			/* gate */
  cgraph_decide_inlining,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_INTEGRATION,			/* tv_id */
  0,	                                /* properties_required */
  PROP_cfg,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_cgraph | TODO_dump_func,	/* todo_flags_finish */
  0					/* letter */
};

/* Because inlining might remove no-longer reachable nodes, we need to
   keep the array visible to garbage collector to avoid reading collected
   out nodes.  */
static int nnodes;
static GTY ((length ("nnodes"))) struct cgraph_node **order;

/* Do inlining of small functions.  Doing so early helps profiling and other
   passes to be somewhat more effective and avoids some code duplication in
   later real inlining pass for testcases with very many function calls.  */
static unsigned int
cgraph_early_inlining (void)
{
  struct cgraph_node *node;
  int i;

  if (sorrycount || errorcount)
    return 0;
#ifdef ENABLE_CHECKING
  for (node = cgraph_nodes; node; node = node->next)
    gcc_assert (!node->aux);
#endif

  order = ggc_alloc (sizeof (*order) * cgraph_n_nodes);
  nnodes = cgraph_postorder (order);
  for (i = nnodes - 1; i >= 0; i--)
    {
      node = order[i];
      if (node->analyzed && (node->needed || node->reachable))
        node->local.self_insns = node->global.insns
	  = estimate_num_insns (node->decl);
    }
  for (i = nnodes - 1; i >= 0; i--)
    {
      node = order[i];
      if (node->analyzed && node->local.inlinable
	  && (node->needed || node->reachable)
	  && node->callers)
	{
	  if (cgraph_decide_inlining_incrementally (node, true))
	    ggc_collect ();
	}
    }
  cgraph_remove_unreachable_nodes (true, dump_file);
#ifdef ENABLE_CHECKING
  for (node = cgraph_nodes; node; node = node->next)
    gcc_assert (!node->global.inlined_to);
#endif
  ggc_free (order);
  order = NULL;
  nnodes = 0;
  return 0;
}

/* When inlining shall be performed.  */
static bool
cgraph_gate_early_inlining (void)
{
  return flag_inline_trees && flag_early_inlining;
}

struct tree_opt_pass pass_early_ipa_inline = 
{
  "einline",	 			/* name */
  cgraph_gate_early_inlining,		/* gate */
  cgraph_early_inlining,		/* execute */
  NULL,					/* sub */
  NULL,					/* next */
  0,					/* static_pass_number */
  TV_INTEGRATION,			/* tv_id */
  0,	                                /* properties_required */
  PROP_cfg,				/* properties_provided */
  0,					/* properties_destroyed */
  0,					/* todo_flags_start */
  TODO_dump_cgraph | TODO_dump_func,	/* todo_flags_finish */
  0					/* letter */
};

#include "gt-ipa-inline.h"
