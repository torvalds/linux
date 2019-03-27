/* Control flow graph analysis code for GNU compiler.
   Copyright (C) 1987, 1988, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

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

/* This file contains various simple utilities to analyze the CFG.  */
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "obstack.h"
#include "hard-reg-set.h"
#include "basic-block.h"
#include "insn-config.h"
#include "recog.h"
#include "toplev.h"
#include "tm_p.h"
#include "timevar.h"

/* Store the data structures necessary for depth-first search.  */
struct depth_first_search_dsS {
  /* stack for backtracking during the algorithm */
  basic_block *stack;

  /* number of edges in the stack.  That is, positions 0, ..., sp-1
     have edges.  */
  unsigned int sp;

  /* record of basic blocks already seen by depth-first search */
  sbitmap visited_blocks;
};
typedef struct depth_first_search_dsS *depth_first_search_ds;

static void flow_dfs_compute_reverse_init (depth_first_search_ds);
static void flow_dfs_compute_reverse_add_bb (depth_first_search_ds,
					     basic_block);
static basic_block flow_dfs_compute_reverse_execute (depth_first_search_ds,
						     basic_block);
static void flow_dfs_compute_reverse_finish (depth_first_search_ds);
static bool flow_active_insn_p (rtx);

/* Like active_insn_p, except keep the return value clobber around
   even after reload.  */

static bool
flow_active_insn_p (rtx insn)
{
  if (active_insn_p (insn))
    return true;

  /* A clobber of the function return value exists for buggy
     programs that fail to return a value.  Its effect is to
     keep the return value from being live across the entire
     function.  If we allow it to be skipped, we introduce the
     possibility for register lifetime confusion.  */
  if (GET_CODE (PATTERN (insn)) == CLOBBER
      && REG_P (XEXP (PATTERN (insn), 0))
      && REG_FUNCTION_VALUE_P (XEXP (PATTERN (insn), 0)))
    return true;

  return false;
}

/* Return true if the block has no effect and only forwards control flow to
   its single destination.  */

bool
forwarder_block_p (basic_block bb)
{
  rtx insn;

  if (bb == EXIT_BLOCK_PTR || bb == ENTRY_BLOCK_PTR
      || !single_succ_p (bb))
    return false;

  for (insn = BB_HEAD (bb); insn != BB_END (bb); insn = NEXT_INSN (insn))
    if (INSN_P (insn) && flow_active_insn_p (insn))
      return false;

  return (!INSN_P (insn)
	  || (JUMP_P (insn) && simplejump_p (insn))
	  || !flow_active_insn_p (insn));
}

/* Return nonzero if we can reach target from src by falling through.  */

bool
can_fallthru (basic_block src, basic_block target)
{
  rtx insn = BB_END (src);
  rtx insn2;
  edge e;
  edge_iterator ei;

  if (target == EXIT_BLOCK_PTR)
    return true;
  if (src->next_bb != target)
    return 0;
  FOR_EACH_EDGE (e, ei, src->succs)
    if (e->dest == EXIT_BLOCK_PTR
	&& e->flags & EDGE_FALLTHRU)
      return 0;

  insn2 = BB_HEAD (target);
  if (insn2 && !active_insn_p (insn2))
    insn2 = next_active_insn (insn2);

  /* ??? Later we may add code to move jump tables offline.  */
  return next_active_insn (insn) == insn2;
}

/* Return nonzero if we could reach target from src by falling through,
   if the target was made adjacent.  If we already have a fall-through
   edge to the exit block, we can't do that.  */
bool
could_fall_through (basic_block src, basic_block target)
{
  edge e;
  edge_iterator ei;

  if (target == EXIT_BLOCK_PTR)
    return true;
  FOR_EACH_EDGE (e, ei, src->succs)
    if (e->dest == EXIT_BLOCK_PTR
	&& e->flags & EDGE_FALLTHRU)
      return 0;
  return true;
}

/* Mark the back edges in DFS traversal.
   Return nonzero if a loop (natural or otherwise) is present.
   Inspired by Depth_First_Search_PP described in:

     Advanced Compiler Design and Implementation
     Steven Muchnick
     Morgan Kaufmann, 1997

   and heavily borrowed from pre_and_rev_post_order_compute.  */

bool
mark_dfs_back_edges (void)
{
  edge_iterator *stack;
  int *pre;
  int *post;
  int sp;
  int prenum = 1;
  int postnum = 1;
  sbitmap visited;
  bool found = false;

  /* Allocate the preorder and postorder number arrays.  */
  pre = XCNEWVEC (int, last_basic_block);
  post = XCNEWVEC (int, last_basic_block);

  /* Allocate stack for back-tracking up CFG.  */
  stack = XNEWVEC (edge_iterator, n_basic_blocks + 1);
  sp = 0;

  /* Allocate bitmap to track nodes that have been visited.  */
  visited = sbitmap_alloc (last_basic_block);

  /* None of the nodes in the CFG have been visited yet.  */
  sbitmap_zero (visited);

  /* Push the first edge on to the stack.  */
  stack[sp++] = ei_start (ENTRY_BLOCK_PTR->succs);

  while (sp)
    {
      edge_iterator ei;
      basic_block src;
      basic_block dest;

      /* Look at the edge on the top of the stack.  */
      ei = stack[sp - 1];
      src = ei_edge (ei)->src;
      dest = ei_edge (ei)->dest;
      ei_edge (ei)->flags &= ~EDGE_DFS_BACK;

      /* Check if the edge destination has been visited yet.  */
      if (dest != EXIT_BLOCK_PTR && ! TEST_BIT (visited, dest->index))
	{
	  /* Mark that we have visited the destination.  */
	  SET_BIT (visited, dest->index);

	  pre[dest->index] = prenum++;
	  if (EDGE_COUNT (dest->succs) > 0)
	    {
	      /* Since the DEST node has been visited for the first
		 time, check its successors.  */
	      stack[sp++] = ei_start (dest->succs);
	    }
	  else
	    post[dest->index] = postnum++;
	}
      else
	{
	  if (dest != EXIT_BLOCK_PTR && src != ENTRY_BLOCK_PTR
	      && pre[src->index] >= pre[dest->index]
	      && post[dest->index] == 0)
	    ei_edge (ei)->flags |= EDGE_DFS_BACK, found = true;

	  if (ei_one_before_end_p (ei) && src != ENTRY_BLOCK_PTR)
	    post[src->index] = postnum++;

	  if (!ei_one_before_end_p (ei))
	    ei_next (&stack[sp - 1]);
	  else
	    sp--;
	}
    }

  free (pre);
  free (post);
  free (stack);
  sbitmap_free (visited);

  return found;
}

/* Set the flag EDGE_CAN_FALLTHRU for edges that can be fallthru.  */

void
set_edge_can_fallthru_flag (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    {
      edge e;
      edge_iterator ei;

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  e->flags &= ~EDGE_CAN_FALLTHRU;

	  /* The FALLTHRU edge is also CAN_FALLTHRU edge.  */
	  if (e->flags & EDGE_FALLTHRU)
	    e->flags |= EDGE_CAN_FALLTHRU;
	}

      /* If the BB ends with an invertible condjump all (2) edges are
	 CAN_FALLTHRU edges.  */
      if (EDGE_COUNT (bb->succs) != 2)
	continue;
      if (!any_condjump_p (BB_END (bb)))
	continue;
      if (!invert_jump (BB_END (bb), JUMP_LABEL (BB_END (bb)), 0))
	continue;
      invert_jump (BB_END (bb), JUMP_LABEL (BB_END (bb)), 0);
      EDGE_SUCC (bb, 0)->flags |= EDGE_CAN_FALLTHRU;
      EDGE_SUCC (bb, 1)->flags |= EDGE_CAN_FALLTHRU;
    }
}

/* Find unreachable blocks.  An unreachable block will have 0 in
   the reachable bit in block->flags.  A nonzero value indicates the
   block is reachable.  */

void
find_unreachable_blocks (void)
{
  edge e;
  edge_iterator ei;
  basic_block *tos, *worklist, bb;

  tos = worklist = XNEWVEC (basic_block, n_basic_blocks);

  /* Clear all the reachability flags.  */

  FOR_EACH_BB (bb)
    bb->flags &= ~BB_REACHABLE;

  /* Add our starting points to the worklist.  Almost always there will
     be only one.  It isn't inconceivable that we might one day directly
     support Fortran alternate entry points.  */

  FOR_EACH_EDGE (e, ei, ENTRY_BLOCK_PTR->succs)
    {
      *tos++ = e->dest;

      /* Mark the block reachable.  */
      e->dest->flags |= BB_REACHABLE;
    }

  /* Iterate: find everything reachable from what we've already seen.  */

  while (tos != worklist)
    {
      basic_block b = *--tos;

      FOR_EACH_EDGE (e, ei, b->succs)
	{
	  basic_block dest = e->dest;

	  if (!(dest->flags & BB_REACHABLE))
	    {
	      *tos++ = dest;
	      dest->flags |= BB_REACHABLE;
	    }
	}
    }

  free (worklist);
}

/* Functions to access an edge list with a vector representation.
   Enough data is kept such that given an index number, the
   pred and succ that edge represents can be determined, or
   given a pred and a succ, its index number can be returned.
   This allows algorithms which consume a lot of memory to
   represent the normally full matrix of edge (pred,succ) with a
   single indexed vector,  edge (EDGE_INDEX (pred, succ)), with no
   wasted space in the client code due to sparse flow graphs.  */

/* This functions initializes the edge list. Basically the entire
   flowgraph is processed, and all edges are assigned a number,
   and the data structure is filled in.  */

struct edge_list *
create_edge_list (void)
{
  struct edge_list *elist;
  edge e;
  int num_edges;
  int block_count;
  basic_block bb;
  edge_iterator ei;

  block_count = n_basic_blocks; /* Include the entry and exit blocks.  */

  num_edges = 0;

  /* Determine the number of edges in the flow graph by counting successor
     edges on each basic block.  */
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      num_edges += EDGE_COUNT (bb->succs);
    }

  elist = XNEW (struct edge_list);
  elist->num_blocks = block_count;
  elist->num_edges = num_edges;
  elist->index_to_edge = XNEWVEC (edge, num_edges);

  num_edges = 0;

  /* Follow successors of blocks, and register these edges.  */
  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    FOR_EACH_EDGE (e, ei, bb->succs)
      elist->index_to_edge[num_edges++] = e;

  return elist;
}

/* This function free's memory associated with an edge list.  */

void
free_edge_list (struct edge_list *elist)
{
  if (elist)
    {
      free (elist->index_to_edge);
      free (elist);
    }
}

/* This function provides debug output showing an edge list.  */

void
print_edge_list (FILE *f, struct edge_list *elist)
{
  int x;

  fprintf (f, "Compressed edge list, %d BBs + entry & exit, and %d edges\n",
	   elist->num_blocks, elist->num_edges);

  for (x = 0; x < elist->num_edges; x++)
    {
      fprintf (f, " %-4d - edge(", x);
      if (INDEX_EDGE_PRED_BB (elist, x) == ENTRY_BLOCK_PTR)
	fprintf (f, "entry,");
      else
	fprintf (f, "%d,", INDEX_EDGE_PRED_BB (elist, x)->index);

      if (INDEX_EDGE_SUCC_BB (elist, x) == EXIT_BLOCK_PTR)
	fprintf (f, "exit)\n");
      else
	fprintf (f, "%d)\n", INDEX_EDGE_SUCC_BB (elist, x)->index);
    }
}

/* This function provides an internal consistency check of an edge list,
   verifying that all edges are present, and that there are no
   extra edges.  */

void
verify_edge_list (FILE *f, struct edge_list *elist)
{
  int pred, succ, index;
  edge e;
  basic_block bb, p, s;
  edge_iterator ei;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  pred = e->src->index;
	  succ = e->dest->index;
	  index = EDGE_INDEX (elist, e->src, e->dest);
	  if (index == EDGE_INDEX_NO_EDGE)
	    {
	      fprintf (f, "*p* No index for edge from %d to %d\n", pred, succ);
	      continue;
	    }

	  if (INDEX_EDGE_PRED_BB (elist, index)->index != pred)
	    fprintf (f, "*p* Pred for index %d should be %d not %d\n",
		     index, pred, INDEX_EDGE_PRED_BB (elist, index)->index);
	  if (INDEX_EDGE_SUCC_BB (elist, index)->index != succ)
	    fprintf (f, "*p* Succ for index %d should be %d not %d\n",
		     index, succ, INDEX_EDGE_SUCC_BB (elist, index)->index);
	}
    }

  /* We've verified that all the edges are in the list, now lets make sure
     there are no spurious edges in the list.  */

  FOR_BB_BETWEEN (p, ENTRY_BLOCK_PTR, EXIT_BLOCK_PTR, next_bb)
    FOR_BB_BETWEEN (s, ENTRY_BLOCK_PTR->next_bb, NULL, next_bb)
      {
	int found_edge = 0;

	FOR_EACH_EDGE (e, ei, p->succs)
	  if (e->dest == s)
	    {
	      found_edge = 1;
	      break;
	    }

	FOR_EACH_EDGE (e, ei, s->preds)
	  if (e->src == p)
	    {
	      found_edge = 1;
	      break;
	    }

	if (EDGE_INDEX (elist, p, s)
	    == EDGE_INDEX_NO_EDGE && found_edge != 0)
	  fprintf (f, "*** Edge (%d, %d) appears to not have an index\n",
		   p->index, s->index);
	if (EDGE_INDEX (elist, p, s)
	    != EDGE_INDEX_NO_EDGE && found_edge == 0)
	  fprintf (f, "*** Edge (%d, %d) has index %d, but there is no edge\n",
		   p->index, s->index, EDGE_INDEX (elist, p, s));
      }
}

/* Given PRED and SUCC blocks, return the edge which connects the blocks.
   If no such edge exists, return NULL.  */

edge
find_edge (basic_block pred, basic_block succ)
{
  edge e;
  edge_iterator ei;

  if (EDGE_COUNT (pred->succs) <= EDGE_COUNT (succ->preds))
    {
      FOR_EACH_EDGE (e, ei, pred->succs)
	if (e->dest == succ)
	  return e;
    }
  else
    {
      FOR_EACH_EDGE (e, ei, succ->preds)
	if (e->src == pred)
	  return e;
    }

  return NULL;
}

/* This routine will determine what, if any, edge there is between
   a specified predecessor and successor.  */

int
find_edge_index (struct edge_list *edge_list, basic_block pred, basic_block succ)
{
  int x;

  for (x = 0; x < NUM_EDGES (edge_list); x++)
    if (INDEX_EDGE_PRED_BB (edge_list, x) == pred
	&& INDEX_EDGE_SUCC_BB (edge_list, x) == succ)
      return x;

  return (EDGE_INDEX_NO_EDGE);
}

/* Dump the list of basic blocks in the bitmap NODES.  */

void
flow_nodes_print (const char *str, const sbitmap nodes, FILE *file)
{
  unsigned int node = 0;
  sbitmap_iterator sbi;

  if (! nodes)
    return;

  fprintf (file, "%s { ", str);
  EXECUTE_IF_SET_IN_SBITMAP (nodes, 0, node, sbi)
    fprintf (file, "%d ", node);
  fputs ("}\n", file);
}

/* Dump the list of edges in the array EDGE_LIST.  */

void
flow_edge_list_print (const char *str, const edge *edge_list, int num_edges, FILE *file)
{
  int i;

  if (! edge_list)
    return;

  fprintf (file, "%s { ", str);
  for (i = 0; i < num_edges; i++)
    fprintf (file, "%d->%d ", edge_list[i]->src->index,
	     edge_list[i]->dest->index);

  fputs ("}\n", file);
}


/* This routine will remove any fake predecessor edges for a basic block.
   When the edge is removed, it is also removed from whatever successor
   list it is in.  */

static void
remove_fake_predecessors (basic_block bb)
{
  edge e;
  edge_iterator ei;

  for (ei = ei_start (bb->preds); (e = ei_safe_edge (ei)); )
    {
      if ((e->flags & EDGE_FAKE) == EDGE_FAKE)
	remove_edge (e);
      else
	ei_next (&ei);
    }
}

/* This routine will remove all fake edges from the flow graph.  If
   we remove all fake successors, it will automatically remove all
   fake predecessors.  */

void
remove_fake_edges (void)
{
  basic_block bb;

  FOR_BB_BETWEEN (bb, ENTRY_BLOCK_PTR->next_bb, NULL, next_bb)
    remove_fake_predecessors (bb);
}

/* This routine will remove all fake edges to the EXIT_BLOCK.  */

void
remove_fake_exit_edges (void)
{
  remove_fake_predecessors (EXIT_BLOCK_PTR);
}


/* This function will add a fake edge between any block which has no
   successors, and the exit block. Some data flow equations require these
   edges to exist.  */

void
add_noreturn_fake_exit_edges (void)
{
  basic_block bb;

  FOR_EACH_BB (bb)
    if (EDGE_COUNT (bb->succs) == 0)
      make_single_succ_edge (bb, EXIT_BLOCK_PTR, EDGE_FAKE);
}

/* This function adds a fake edge between any infinite loops to the
   exit block.  Some optimizations require a path from each node to
   the exit node.

   See also Morgan, Figure 3.10, pp. 82-83.

   The current implementation is ugly, not attempting to minimize the
   number of inserted fake edges.  To reduce the number of fake edges
   to insert, add fake edges from _innermost_ loops containing only
   nodes not reachable from the exit block.  */

void
connect_infinite_loops_to_exit (void)
{
  basic_block unvisited_block = EXIT_BLOCK_PTR;
  struct depth_first_search_dsS dfs_ds;

  /* Perform depth-first search in the reverse graph to find nodes
     reachable from the exit block.  */
  flow_dfs_compute_reverse_init (&dfs_ds);
  flow_dfs_compute_reverse_add_bb (&dfs_ds, EXIT_BLOCK_PTR);

  /* Repeatedly add fake edges, updating the unreachable nodes.  */
  while (1)
    {
      unvisited_block = flow_dfs_compute_reverse_execute (&dfs_ds,
							  unvisited_block);
      if (!unvisited_block)
	break;

      make_edge (unvisited_block, EXIT_BLOCK_PTR, EDGE_FAKE);
      flow_dfs_compute_reverse_add_bb (&dfs_ds, unvisited_block);
    }

  flow_dfs_compute_reverse_finish (&dfs_ds);
  return;
}

/* Compute reverse top sort order.  
   This is computing a post order numbering of the graph.  */

int
post_order_compute (int *post_order, bool include_entry_exit)
{
  edge_iterator *stack;
  int sp;
  int post_order_num = 0;
  sbitmap visited;

  if (include_entry_exit)
    post_order[post_order_num++] = EXIT_BLOCK;

  /* Allocate stack for back-tracking up CFG.  */
  stack = XNEWVEC (edge_iterator, n_basic_blocks + 1);
  sp = 0;

  /* Allocate bitmap to track nodes that have been visited.  */
  visited = sbitmap_alloc (last_basic_block);

  /* None of the nodes in the CFG have been visited yet.  */
  sbitmap_zero (visited);

  /* Push the first edge on to the stack.  */
  stack[sp++] = ei_start (ENTRY_BLOCK_PTR->succs);

  while (sp)
    {
      edge_iterator ei;
      basic_block src;
      basic_block dest;

      /* Look at the edge on the top of the stack.  */
      ei = stack[sp - 1];
      src = ei_edge (ei)->src;
      dest = ei_edge (ei)->dest;

      /* Check if the edge destination has been visited yet.  */
      if (dest != EXIT_BLOCK_PTR && ! TEST_BIT (visited, dest->index))
	{
	  /* Mark that we have visited the destination.  */
	  SET_BIT (visited, dest->index);

	  if (EDGE_COUNT (dest->succs) > 0)
	    /* Since the DEST node has been visited for the first
	       time, check its successors.  */
	    stack[sp++] = ei_start (dest->succs);
	  else
	    post_order[post_order_num++] = dest->index;
	}
      else
	{
	  if (ei_one_before_end_p (ei) && src != ENTRY_BLOCK_PTR)
	   post_order[post_order_num++] = src->index;

	  if (!ei_one_before_end_p (ei))
	    ei_next (&stack[sp - 1]);
	  else
	    sp--;
	}
    }

  if (include_entry_exit)
    post_order[post_order_num++] = ENTRY_BLOCK;

  free (stack);
  sbitmap_free (visited);
  return post_order_num;
}

/* Compute the depth first search order and store in the array
  PRE_ORDER if nonzero, marking the nodes visited in VISITED.  If
  REV_POST_ORDER is nonzero, return the reverse completion number for each
  node.  Returns the number of nodes visited.  A depth first search
  tries to get as far away from the starting point as quickly as
  possible. 

  pre_order is a really a preorder numbering of the graph.
  rev_post_order is really a reverse postorder numbering of the graph.
 */

int
pre_and_rev_post_order_compute (int *pre_order, int *rev_post_order, 
				bool include_entry_exit)
{
  edge_iterator *stack;
  int sp;
  int pre_order_num = 0;
  int rev_post_order_num = n_basic_blocks - 1;
  sbitmap visited;

  /* Allocate stack for back-tracking up CFG.  */
  stack = XNEWVEC (edge_iterator, n_basic_blocks + 1);
  sp = 0;

  if (include_entry_exit)
    {
      if (pre_order)
	pre_order[pre_order_num] = ENTRY_BLOCK;
      pre_order_num++;
      if (rev_post_order)
	rev_post_order[rev_post_order_num--] = ENTRY_BLOCK;
    }
  else 
    rev_post_order_num -= NUM_FIXED_BLOCKS;

  /* Allocate bitmap to track nodes that have been visited.  */
  visited = sbitmap_alloc (last_basic_block);

  /* None of the nodes in the CFG have been visited yet.  */
  sbitmap_zero (visited);

  /* Push the first edge on to the stack.  */
  stack[sp++] = ei_start (ENTRY_BLOCK_PTR->succs);

  while (sp)
    {
      edge_iterator ei;
      basic_block src;
      basic_block dest;

      /* Look at the edge on the top of the stack.  */
      ei = stack[sp - 1];
      src = ei_edge (ei)->src;
      dest = ei_edge (ei)->dest;

      /* Check if the edge destination has been visited yet.  */
      if (dest != EXIT_BLOCK_PTR && ! TEST_BIT (visited, dest->index))
	{
	  /* Mark that we have visited the destination.  */
	  SET_BIT (visited, dest->index);

	  if (pre_order)
	    pre_order[pre_order_num] = dest->index;

	  pre_order_num++;

	  if (EDGE_COUNT (dest->succs) > 0)
	    /* Since the DEST node has been visited for the first
	       time, check its successors.  */
	    stack[sp++] = ei_start (dest->succs);
	  else if (rev_post_order)
	    /* There are no successors for the DEST node so assign
	       its reverse completion number.  */
	    rev_post_order[rev_post_order_num--] = dest->index;
	}
      else
	{
	  if (ei_one_before_end_p (ei) && src != ENTRY_BLOCK_PTR
	      && rev_post_order)
	    /* There are no more successors for the SRC node
	       so assign its reverse completion number.  */
	    rev_post_order[rev_post_order_num--] = src->index;

	  if (!ei_one_before_end_p (ei))
	    ei_next (&stack[sp - 1]);
	  else
	    sp--;
	}
    }

  free (stack);
  sbitmap_free (visited);

  if (include_entry_exit)
    {
      if (pre_order)
	pre_order[pre_order_num] = EXIT_BLOCK;
      pre_order_num++;
      if (rev_post_order)
	rev_post_order[rev_post_order_num--] = EXIT_BLOCK;
      /* The number of nodes visited should be the number of blocks.  */
      gcc_assert (pre_order_num == n_basic_blocks);
    }
  else
    /* The number of nodes visited should be the number of blocks minus
       the entry and exit blocks which are not visited here.  */
    gcc_assert (pre_order_num == n_basic_blocks - NUM_FIXED_BLOCKS);

  return pre_order_num;
}

/* Compute the depth first search order on the _reverse_ graph and
   store in the array DFS_ORDER, marking the nodes visited in VISITED.
   Returns the number of nodes visited.

   The computation is split into three pieces:

   flow_dfs_compute_reverse_init () creates the necessary data
   structures.

   flow_dfs_compute_reverse_add_bb () adds a basic block to the data
   structures.  The block will start the search.

   flow_dfs_compute_reverse_execute () continues (or starts) the
   search using the block on the top of the stack, stopping when the
   stack is empty.

   flow_dfs_compute_reverse_finish () destroys the necessary data
   structures.

   Thus, the user will probably call ..._init(), call ..._add_bb() to
   add a beginning basic block to the stack, call ..._execute(),
   possibly add another bb to the stack and again call ..._execute(),
   ..., and finally call _finish().  */

/* Initialize the data structures used for depth-first search on the
   reverse graph.  If INITIALIZE_STACK is nonzero, the exit block is
   added to the basic block stack.  DATA is the current depth-first
   search context.  If INITIALIZE_STACK is nonzero, there is an
   element on the stack.  */

static void
flow_dfs_compute_reverse_init (depth_first_search_ds data)
{
  /* Allocate stack for back-tracking up CFG.  */
  data->stack = XNEWVEC (basic_block, n_basic_blocks);
  data->sp = 0;

  /* Allocate bitmap to track nodes that have been visited.  */
  data->visited_blocks = sbitmap_alloc (last_basic_block);

  /* None of the nodes in the CFG have been visited yet.  */
  sbitmap_zero (data->visited_blocks);

  return;
}

/* Add the specified basic block to the top of the dfs data
   structures.  When the search continues, it will start at the
   block.  */

static void
flow_dfs_compute_reverse_add_bb (depth_first_search_ds data, basic_block bb)
{
  data->stack[data->sp++] = bb;
  SET_BIT (data->visited_blocks, bb->index);
}

/* Continue the depth-first search through the reverse graph starting with the
   block at the stack's top and ending when the stack is empty.  Visited nodes
   are marked.  Returns an unvisited basic block, or NULL if there is none
   available.  */

static basic_block
flow_dfs_compute_reverse_execute (depth_first_search_ds data,
				  basic_block last_unvisited)
{
  basic_block bb;
  edge e;
  edge_iterator ei;

  while (data->sp > 0)
    {
      bb = data->stack[--data->sp];

      /* Perform depth-first search on adjacent vertices.  */
      FOR_EACH_EDGE (e, ei, bb->preds)
	if (!TEST_BIT (data->visited_blocks, e->src->index))
	  flow_dfs_compute_reverse_add_bb (data, e->src);
    }

  /* Determine if there are unvisited basic blocks.  */
  FOR_BB_BETWEEN (bb, last_unvisited, NULL, prev_bb)
    if (!TEST_BIT (data->visited_blocks, bb->index))
      return bb;

  return NULL;
}

/* Destroy the data structures needed for depth-first search on the
   reverse graph.  */

static void
flow_dfs_compute_reverse_finish (depth_first_search_ds data)
{
  free (data->stack);
  sbitmap_free (data->visited_blocks);
}

/* Performs dfs search from BB over vertices satisfying PREDICATE;
   if REVERSE, go against direction of edges.  Returns number of blocks
   found and their list in RSLT.  RSLT can contain at most RSLT_MAX items.  */
int
dfs_enumerate_from (basic_block bb, int reverse,
		    bool (*predicate) (basic_block, void *),
		    basic_block *rslt, int rslt_max, void *data)
{
  basic_block *st, lbb;
  int sp = 0, tv = 0;
  unsigned size;

  /* A bitmap to keep track of visited blocks.  Allocating it each time
     this function is called is not possible, since dfs_enumerate_from
     is often used on small (almost) disjoint parts of cfg (bodies of
     loops), and allocating a large sbitmap would lead to quadratic
     behavior.  */
  static sbitmap visited;
  static unsigned v_size;

#define MARK_VISITED(BB) (SET_BIT (visited, (BB)->index)) 
#define UNMARK_VISITED(BB) (RESET_BIT (visited, (BB)->index)) 
#define VISITED_P(BB) (TEST_BIT (visited, (BB)->index)) 

  /* Resize the VISITED sbitmap if necessary.  */
  size = last_basic_block; 
  if (size < 10)
    size = 10;

  if (!visited)
    {

      visited = sbitmap_alloc (size);
      sbitmap_zero (visited);
      v_size = size;
    }
  else if (v_size < size)
    {
      /* Ensure that we increase the size of the sbitmap exponentially.  */
      if (2 * v_size > size)
	size = 2 * v_size;

      visited = sbitmap_resize (visited, size, 0);
      v_size = size;
    }

  st = XCNEWVEC (basic_block, rslt_max);
  rslt[tv++] = st[sp++] = bb;
  MARK_VISITED (bb);
  while (sp)
    {
      edge e;
      edge_iterator ei;
      lbb = st[--sp];
      if (reverse)
	{
	  FOR_EACH_EDGE (e, ei, lbb->preds)
	    if (!VISITED_P (e->src) && predicate (e->src, data))
	      {
		gcc_assert (tv != rslt_max);
		rslt[tv++] = st[sp++] = e->src;
		MARK_VISITED (e->src);
	      }
	}
      else
	{
	  FOR_EACH_EDGE (e, ei, lbb->succs)
	    if (!VISITED_P (e->dest) && predicate (e->dest, data))
	      {
		gcc_assert (tv != rslt_max);
		rslt[tv++] = st[sp++] = e->dest;
		MARK_VISITED (e->dest);
	      }
	}
    }
  free (st);
  for (sp = 0; sp < tv; sp++)
    UNMARK_VISITED (rslt[sp]);
  return tv;
#undef MARK_VISITED
#undef UNMARK_VISITED
#undef VISITED_P
}


/* Compute dominance frontiers, ala Harvey, Ferrante, et al.

   This algorithm can be found in Timothy Harvey's PhD thesis, at
   http://www.cs.rice.edu/~harv/dissertation.pdf in the section on iterative
   dominance algorithms.

   First, we identify each join point, j (any node with more than one
   incoming edge is a join point).

   We then examine each predecessor, p, of j and walk up the dominator tree
   starting at p.

   We stop the walk when we reach j's immediate dominator - j is in the
   dominance frontier of each of  the nodes in the walk, except for j's
   immediate dominator. Intuitively, all of the rest of j's dominators are
   shared by j's predecessors as well.
   Since they dominate j, they will not have j in their dominance frontiers.

   The number of nodes touched by this algorithm is equal to the size
   of the dominance frontiers, no more, no less.
*/


static void
compute_dominance_frontiers_1 (bitmap *frontiers)
{
  edge p;
  edge_iterator ei;
  basic_block b;
  FOR_EACH_BB (b)
    {
      if (EDGE_COUNT (b->preds) >= 2)
	{
	  FOR_EACH_EDGE (p, ei, b->preds)
	    {
	      basic_block runner = p->src;
	      basic_block domsb;
	      if (runner == ENTRY_BLOCK_PTR)
		continue;

	      domsb = get_immediate_dominator (CDI_DOMINATORS, b);
	      while (runner != domsb)
		{
		  if (bitmap_bit_p (frontiers[runner->index], b->index))
		    break;
		  bitmap_set_bit (frontiers[runner->index],
				  b->index);
		  runner = get_immediate_dominator (CDI_DOMINATORS,
						    runner);
		}
	    }
	}
    }
}


void
compute_dominance_frontiers (bitmap *frontiers)
{
  timevar_push (TV_DOM_FRONTIERS);

  compute_dominance_frontiers_1 (frontiers);

  timevar_pop (TV_DOM_FRONTIERS);
}

