/* Natural loop discovery code for GNU compiler.
   Copyright (C) 2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.

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
#include "hard-reg-set.h"
#include "obstack.h"
#include "function.h"
#include "basic-block.h"
#include "toplev.h"
#include "cfgloop.h"
#include "flags.h"
#include "tree.h"
#include "tree-flow.h"

/* Ratio of frequencies of edges so that one of more latch edges is
   considered to belong to inner loop with same header.  */
#define HEAVY_EDGE_RATIO 8

#define HEADER_BLOCK(B) (* (int *) (B)->aux)
#define LATCH_EDGE(E) (*(int *) (E)->aux)

static void flow_loops_cfg_dump (const struct loops *, FILE *);
static int flow_loop_level_compute (struct loop *);
static void flow_loops_level_compute (struct loops *);
static void establish_preds (struct loop *);
static void canonicalize_loop_headers (void);
static bool glb_enum_p (basic_block, void *);

/* Dump loop related CFG information.  */

static void
flow_loops_cfg_dump (const struct loops *loops, FILE *file)
{
  int i;
  basic_block bb;

  if (! loops->num || ! file)
    return;

  FOR_EACH_BB (bb)
    {
      edge succ;
      edge_iterator ei;

      fprintf (file, ";; %d succs { ", bb->index);
      FOR_EACH_EDGE (succ, ei, bb->succs)
	fprintf (file, "%d ", succ->dest->index);
      fprintf (file, "}\n");
    }

  /* Dump the DFS node order.  */
  if (loops->cfg.dfs_order)
    {
      fputs (";; DFS order: ", file);
      for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
	fprintf (file, "%d ", loops->cfg.dfs_order[i]);

      fputs ("\n", file);
    }

  /* Dump the reverse completion node order.  */
  if (loops->cfg.rc_order)
    {
      fputs (";; RC order: ", file);
      for (i = NUM_FIXED_BLOCKS; i < n_basic_blocks; i++)
	fprintf (file, "%d ", loops->cfg.rc_order[i]);

      fputs ("\n", file);
    }
}

/* Return nonzero if the nodes of LOOP are a subset of OUTER.  */

bool
flow_loop_nested_p (const struct loop *outer, const struct loop *loop)
{
  return (loop->depth > outer->depth
	 && loop->pred[outer->depth] == outer);
}

/* Returns the loop such that LOOP is nested DEPTH (indexed from zero)
   loops within LOOP.  */

struct loop *
superloop_at_depth (struct loop *loop, unsigned depth)
{
  gcc_assert (depth <= (unsigned) loop->depth);

  if (depth == (unsigned) loop->depth)
    return loop;

  return loop->pred[depth];
}

/* Dump the loop information specified by LOOP to the stream FILE
   using auxiliary dump callback function LOOP_DUMP_AUX if non null.  */

void
flow_loop_dump (const struct loop *loop, FILE *file,
		void (*loop_dump_aux) (const struct loop *, FILE *, int),
		int verbose)
{
  basic_block *bbs;
  unsigned i;

  if (! loop || ! loop->header)
    return;

  fprintf (file, ";;\n;; Loop %d\n", loop->num);

  fprintf (file, ";;  header %d, latch %d\n",
	   loop->header->index, loop->latch->index);
  fprintf (file, ";;  depth %d, level %d, outer %ld\n",
	   loop->depth, loop->level,
	   (long) (loop->outer ? loop->outer->num : -1));

  fprintf (file, ";;  nodes:");
  bbs = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    fprintf (file, " %d", bbs[i]->index);
  free (bbs);
  fprintf (file, "\n");

  if (loop_dump_aux)
    loop_dump_aux (loop, file, verbose);
}

/* Dump the loop information specified by LOOPS to the stream FILE,
   using auxiliary dump callback function LOOP_DUMP_AUX if non null.  */

void
flow_loops_dump (const struct loops *loops, FILE *file, void (*loop_dump_aux) (const struct loop *, FILE *, int), int verbose)
{
  int i;
  int num_loops;

  num_loops = loops->num;
  if (! num_loops || ! file)
    return;

  fprintf (file, ";; %d loops found\n", num_loops);

  for (i = 0; i < num_loops; i++)
    {
      struct loop *loop = loops->parray[i];

      if (!loop)
	continue;

      flow_loop_dump (loop, file, loop_dump_aux, verbose);
    }

  if (verbose)
    flow_loops_cfg_dump (loops, file);
}

/* Free data allocated for LOOP.  */
void
flow_loop_free (struct loop *loop)
{
  if (loop->pred)
    free (loop->pred);
  free (loop);
}

/* Free all the memory allocated for LOOPS.  */

void
flow_loops_free (struct loops *loops)
{
  if (loops->parray)
    {
      unsigned i;

      gcc_assert (loops->num);

      /* Free the loop descriptors.  */
      for (i = 0; i < loops->num; i++)
	{
	  struct loop *loop = loops->parray[i];

	  if (!loop)
	    continue;

	  flow_loop_free (loop);
	}

      free (loops->parray);
      loops->parray = NULL;

      if (loops->cfg.dfs_order)
	free (loops->cfg.dfs_order);
      if (loops->cfg.rc_order)
	free (loops->cfg.rc_order);

    }
}

/* Find the nodes contained within the LOOP with header HEADER.
   Return the number of nodes within the loop.  */

int
flow_loop_nodes_find (basic_block header, struct loop *loop)
{
  basic_block *stack;
  int sp;
  int num_nodes = 1;

  header->loop_father = loop;
  header->loop_depth = loop->depth;

  if (loop->latch->loop_father != loop)
    {
      stack = XNEWVEC (basic_block, n_basic_blocks);
      sp = 0;
      num_nodes++;
      stack[sp++] = loop->latch;
      loop->latch->loop_father = loop;
      loop->latch->loop_depth = loop->depth;

      while (sp)
	{
	  basic_block node;
	  edge e;
	  edge_iterator ei;

	  node = stack[--sp];

	  FOR_EACH_EDGE (e, ei, node->preds)
	    {
	      basic_block ancestor = e->src;

	      if (ancestor != ENTRY_BLOCK_PTR
		  && ancestor->loop_father != loop)
		{
		  ancestor->loop_father = loop;
		  ancestor->loop_depth = loop->depth;
		  num_nodes++;
		  stack[sp++] = ancestor;
		}
	    }
	}
      free (stack);
    }
  return num_nodes;
}

/* For each loop in the lOOPS tree that has just a single exit
   record the exit edge.  */

void
mark_single_exit_loops (struct loops *loops)
{
  basic_block bb;
  edge e;
  struct loop *loop;
  unsigned i;

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (loop)
	loop->single_exit = NULL;
    }

  FOR_EACH_BB (bb)
    {
      edge_iterator ei;
      if (bb->loop_father == loops->tree_root)
	continue;
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (e->dest == EXIT_BLOCK_PTR)
	    continue;

	  if (flow_bb_inside_loop_p (bb->loop_father, e->dest))
	    continue;

	  for (loop = bb->loop_father;
	       loop != e->dest->loop_father;
	       loop = loop->outer)
	    {
	      /* If we have already seen an exit, mark this by the edge that
		 surely does not occur as any exit.  */
	      if (loop->single_exit)
		loop->single_exit = single_succ_edge (ENTRY_BLOCK_PTR);
	      else
		loop->single_exit = e;
	    }
	}
    }

  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;

      if (loop->single_exit == single_succ_edge (ENTRY_BLOCK_PTR))
	loop->single_exit = NULL;
    }

  loops->state |= LOOPS_HAVE_MARKED_SINGLE_EXITS;
}

static void
establish_preds (struct loop *loop)
{
  struct loop *ploop, *father = loop->outer;

  loop->depth = father->depth + 1;

  /* Remember the current loop depth if it is the largest seen so far.  */
  cfun->max_loop_depth = MAX (cfun->max_loop_depth, loop->depth);

  if (loop->pred)
    free (loop->pred);
  loop->pred = XNEWVEC (struct loop *, loop->depth);
  memcpy (loop->pred, father->pred, sizeof (struct loop *) * father->depth);
  loop->pred[father->depth] = father;

  for (ploop = loop->inner; ploop; ploop = ploop->next)
    establish_preds (ploop);
}

/* Add LOOP to the loop hierarchy tree where FATHER is father of the
   added loop.  If LOOP has some children, take care of that their
   pred field will be initialized correctly.  */

void
flow_loop_tree_node_add (struct loop *father, struct loop *loop)
{
  loop->next = father->inner;
  father->inner = loop;
  loop->outer = father;

  establish_preds (loop);
}

/* Remove LOOP from the loop hierarchy tree.  */

void
flow_loop_tree_node_remove (struct loop *loop)
{
  struct loop *prev, *father;

  father = loop->outer;
  loop->outer = NULL;

  /* Remove loop from the list of sons.  */
  if (father->inner == loop)
    father->inner = loop->next;
  else
    {
      for (prev = father->inner; prev->next != loop; prev = prev->next);
      prev->next = loop->next;
    }

  loop->depth = -1;
  free (loop->pred);
  loop->pred = NULL;
}

/* Helper function to compute loop nesting depth and enclosed loop level
   for the natural loop specified by LOOP.  Returns the loop level.  */

static int
flow_loop_level_compute (struct loop *loop)
{
  struct loop *inner;
  int level = 1;

  if (! loop)
    return 0;

  /* Traverse loop tree assigning depth and computing level as the
     maximum level of all the inner loops of this loop.  The loop
     level is equivalent to the height of the loop in the loop tree
     and corresponds to the number of enclosed loop levels (including
     itself).  */
  for (inner = loop->inner; inner; inner = inner->next)
    {
      int ilevel = flow_loop_level_compute (inner) + 1;

      if (ilevel > level)
	level = ilevel;
    }

  loop->level = level;
  return level;
}

/* Compute the loop nesting depth and enclosed loop level for the loop
   hierarchy tree specified by LOOPS.  Return the maximum enclosed loop
   level.  */

static void
flow_loops_level_compute (struct loops *loops)
{
  flow_loop_level_compute (loops->tree_root);
}

/* A callback to update latch and header info for basic block JUMP created
   by redirecting an edge.  */

static void
update_latch_info (basic_block jump)
{
  alloc_aux_for_block (jump, sizeof (int));
  HEADER_BLOCK (jump) = 0;
  alloc_aux_for_edge (single_pred_edge (jump), sizeof (int));
  LATCH_EDGE (single_pred_edge (jump)) = 0;
  set_immediate_dominator (CDI_DOMINATORS, jump, single_pred (jump));
}

/* A callback for make_forwarder block, to redirect all edges except for
   MFB_KJ_EDGE to the entry part.  E is the edge for that we should decide
   whether to redirect it.  */

static edge mfb_kj_edge;
static bool
mfb_keep_just (edge e)
{
  return e != mfb_kj_edge;
}

/* A callback for make_forwarder block, to redirect the latch edges into an
   entry part.  E is the edge for that we should decide whether to redirect
   it.  */

static bool
mfb_keep_nonlatch (edge e)
{
  return LATCH_EDGE (e);
}

/* Takes care of merging natural loops with shared headers.  */

static void
canonicalize_loop_headers (void)
{
  basic_block header;
  edge e;

  alloc_aux_for_blocks (sizeof (int));
  alloc_aux_for_edges (sizeof (int));

  /* Split blocks so that each loop has only single latch.  */
  FOR_EACH_BB (header)
    {
      edge_iterator ei;
      int num_latches = 0;
      int have_abnormal_edge = 0;

      FOR_EACH_EDGE (e, ei, header->preds)
	{
	  basic_block latch = e->src;

	  if (e->flags & EDGE_ABNORMAL)
	    have_abnormal_edge = 1;

	  if (latch != ENTRY_BLOCK_PTR
	      && dominated_by_p (CDI_DOMINATORS, latch, header))
	    {
	      num_latches++;
	      LATCH_EDGE (e) = 1;
	    }
	}
      if (have_abnormal_edge)
	HEADER_BLOCK (header) = 0;
      else
	HEADER_BLOCK (header) = num_latches;
    }

  if (HEADER_BLOCK (single_succ (ENTRY_BLOCK_PTR)))
    {
      basic_block bb;

      /* We could not redirect edges freely here. On the other hand,
	 we can simply split the edge from entry block.  */
      bb = split_edge (single_succ_edge (ENTRY_BLOCK_PTR));

      alloc_aux_for_edge (single_succ_edge (bb), sizeof (int));
      LATCH_EDGE (single_succ_edge (bb)) = 0;
      alloc_aux_for_block (bb, sizeof (int));
      HEADER_BLOCK (bb) = 0;
    }

  FOR_EACH_BB (header)
    {
      int max_freq, is_heavy;
      edge heavy, tmp_edge;
      edge_iterator ei;

      if (HEADER_BLOCK (header) <= 1)
	continue;

      /* Find a heavy edge.  */
      is_heavy = 1;
      heavy = NULL;
      max_freq = 0;
      FOR_EACH_EDGE (e, ei, header->preds)
	if (LATCH_EDGE (e) &&
	    EDGE_FREQUENCY (e) > max_freq)
	  max_freq = EDGE_FREQUENCY (e);
      FOR_EACH_EDGE (e, ei, header->preds)
	if (LATCH_EDGE (e) &&
	    EDGE_FREQUENCY (e) >= max_freq / HEAVY_EDGE_RATIO)
	  {
	    if (heavy)
	      {
		is_heavy = 0;
		break;
	      }
	    else
	      heavy = e;
	  }

      if (is_heavy)
	{
	  /* Split out the heavy edge, and create inner loop for it.  */
	  mfb_kj_edge = heavy;
	  tmp_edge = make_forwarder_block (header, mfb_keep_just,
					   update_latch_info);
	  alloc_aux_for_block (tmp_edge->dest, sizeof (int));
	  HEADER_BLOCK (tmp_edge->dest) = 1;
	  alloc_aux_for_edge (tmp_edge, sizeof (int));
	  LATCH_EDGE (tmp_edge) = 0;
	  HEADER_BLOCK (header)--;
	}

      if (HEADER_BLOCK (header) > 1)
	{
	  /* Create a new latch block.  */
	  tmp_edge = make_forwarder_block (header, mfb_keep_nonlatch,
					   update_latch_info);
	  alloc_aux_for_block (tmp_edge->dest, sizeof (int));
	  HEADER_BLOCK (tmp_edge->src) = 0;
	  HEADER_BLOCK (tmp_edge->dest) = 1;
	  alloc_aux_for_edge (tmp_edge, sizeof (int));
	  LATCH_EDGE (tmp_edge) = 1;
	}
    }

  free_aux_for_blocks ();
  free_aux_for_edges ();

#ifdef ENABLE_CHECKING
  verify_dominators (CDI_DOMINATORS);
#endif
}

/* Initialize all the parallel_p fields of the loops structure to true.  */

static void
initialize_loops_parallel_p (struct loops *loops)
{
  unsigned int i;

  for (i = 0; i < loops->num; i++)
    {
      struct loop *loop = loops->parray[i];
      loop->parallel_p = true;
    }
}

/* Find all the natural loops in the function and save in LOOPS structure and
   recalculate loop_depth information in basic block structures.
   Return the number of natural loops found.  */

int
flow_loops_find (struct loops *loops)
{
  int b;
  int num_loops;
  edge e;
  sbitmap headers;
  int *dfs_order;
  int *rc_order;
  basic_block header;
  basic_block bb;

  memset (loops, 0, sizeof *loops);

  /* We are going to recount the maximum loop depth,
     so throw away the last count.  */
  cfun->max_loop_depth = 0;

  /* Taking care of this degenerate case makes the rest of
     this code simpler.  */
  if (n_basic_blocks == NUM_FIXED_BLOCKS)
    return 0;

  dfs_order = NULL;
  rc_order = NULL;

  /* Ensure that the dominators are computed.  */
  calculate_dominance_info (CDI_DOMINATORS);

  /* Join loops with shared headers.  */
  canonicalize_loop_headers ();

  /* Count the number of loop headers.  This should be the
     same as the number of natural loops.  */
  headers = sbitmap_alloc (last_basic_block);
  sbitmap_zero (headers);

  num_loops = 0;
  FOR_EACH_BB (header)
    {
      edge_iterator ei;
      int more_latches = 0;

      header->loop_depth = 0;

      /* If we have an abnormal predecessor, do not consider the
	 loop (not worth the problems).  */
      FOR_EACH_EDGE (e, ei, header->preds)
	if (e->flags & EDGE_ABNORMAL)
	  break;
      if (e)
	continue;

      FOR_EACH_EDGE (e, ei, header->preds)
	{
	  basic_block latch = e->src;

	  gcc_assert (!(e->flags & EDGE_ABNORMAL));

	  /* Look for back edges where a predecessor is dominated
	     by this block.  A natural loop has a single entry
	     node (header) that dominates all the nodes in the
	     loop.  It also has single back edge to the header
	     from a latch node.  */
	  if (latch != ENTRY_BLOCK_PTR
	      && dominated_by_p (CDI_DOMINATORS, latch, header))
	    {
	      /* Shared headers should be eliminated by now.  */
	      gcc_assert (!more_latches);
	      more_latches = 1;
	      SET_BIT (headers, header->index);
	      num_loops++;
	    }
	}
    }

  /* Allocate loop structures.  */
  loops->parray = XCNEWVEC (struct loop *, num_loops + 1);

  /* Dummy loop containing whole function.  */
  loops->parray[0] = XCNEW (struct loop);
  loops->parray[0]->next = NULL;
  loops->parray[0]->inner = NULL;
  loops->parray[0]->outer = NULL;
  loops->parray[0]->depth = 0;
  loops->parray[0]->pred = NULL;
  loops->parray[0]->num_nodes = n_basic_blocks;
  loops->parray[0]->latch = EXIT_BLOCK_PTR;
  loops->parray[0]->header = ENTRY_BLOCK_PTR;
  ENTRY_BLOCK_PTR->loop_father = loops->parray[0];
  EXIT_BLOCK_PTR->loop_father = loops->parray[0];

  loops->tree_root = loops->parray[0];

  /* Find and record information about all the natural loops
     in the CFG.  */
  loops->num = 1;
  FOR_EACH_BB (bb)
    bb->loop_father = loops->tree_root;

  if (num_loops)
    {
      /* Compute depth first search order of the CFG so that outer
	 natural loops will be found before inner natural loops.  */
      dfs_order = XNEWVEC (int, n_basic_blocks);
      rc_order = XNEWVEC (int, n_basic_blocks);
      pre_and_rev_post_order_compute (dfs_order, rc_order, false);

      /* Save CFG derived information to avoid recomputing it.  */
      loops->cfg.dfs_order = dfs_order;
      loops->cfg.rc_order = rc_order;

      num_loops = 1;

      for (b = 0; b < n_basic_blocks - NUM_FIXED_BLOCKS; b++)
	{
	  struct loop *loop;
	  edge_iterator ei;

	  /* Search the nodes of the CFG in reverse completion order
	     so that we can find outer loops first.  */
	  if (!TEST_BIT (headers, rc_order[b]))
	    continue;

	  header = BASIC_BLOCK (rc_order[b]);

	  loop = loops->parray[num_loops] = XCNEW (struct loop);

	  loop->header = header;
	  loop->num = num_loops;
	  num_loops++;

	  /* Look for the latch for this header block.  */
	  FOR_EACH_EDGE (e, ei, header->preds)
	    {
	      basic_block latch = e->src;

	      if (latch != ENTRY_BLOCK_PTR
		  && dominated_by_p (CDI_DOMINATORS, latch, header))
		{
		  loop->latch = latch;
		  break;
		}
	    }

	  flow_loop_tree_node_add (header->loop_father, loop);
	  loop->num_nodes = flow_loop_nodes_find (loop->header, loop);
	}

      /* Assign the loop nesting depth and enclosed loop level for each
	 loop.  */
      flow_loops_level_compute (loops);

      loops->num = num_loops;
      initialize_loops_parallel_p (loops);
    }

  sbitmap_free (headers);

  loops->state = 0;
#ifdef ENABLE_CHECKING
  verify_flow_info ();
  verify_loop_structure (loops);
#endif

  return loops->num;
}

/* Return nonzero if basic block BB belongs to LOOP.  */
bool
flow_bb_inside_loop_p (const struct loop *loop, const basic_block bb)
{
  struct loop *source_loop;

  if (bb == ENTRY_BLOCK_PTR || bb == EXIT_BLOCK_PTR)
    return 0;

  source_loop = bb->loop_father;
  return loop == source_loop || flow_loop_nested_p (loop, source_loop);
}

/* Enumeration predicate for get_loop_body.  */
static bool
glb_enum_p (basic_block bb, void *glb_header)
{
  return bb != (basic_block) glb_header;
}

/* Gets basic blocks of a LOOP.  Header is the 0-th block, rest is in dfs
   order against direction of edges from latch.  Specially, if
   header != latch, latch is the 1-st block.  */
basic_block *
get_loop_body (const struct loop *loop)
{
  basic_block *tovisit, bb;
  unsigned tv = 0;

  gcc_assert (loop->num_nodes);

  tovisit = XCNEWVEC (basic_block, loop->num_nodes);
  tovisit[tv++] = loop->header;

  if (loop->latch == EXIT_BLOCK_PTR)
    {
      /* There may be blocks unreachable from EXIT_BLOCK.  */
      gcc_assert (loop->num_nodes == (unsigned) n_basic_blocks);
      FOR_EACH_BB (bb)
	tovisit[tv++] = bb;
      tovisit[tv++] = EXIT_BLOCK_PTR;
    }
  else if (loop->latch != loop->header)
    {
      tv = dfs_enumerate_from (loop->latch, 1, glb_enum_p,
			       tovisit + 1, loop->num_nodes - 1,
			       loop->header) + 1;
    }

  gcc_assert (tv == loop->num_nodes);
  return tovisit;
}

/* Fills dominance descendants inside LOOP of the basic block BB into
   array TOVISIT from index *TV.  */

static void
fill_sons_in_loop (const struct loop *loop, basic_block bb,
		   basic_block *tovisit, int *tv)
{
  basic_block son, postpone = NULL;

  tovisit[(*tv)++] = bb;
  for (son = first_dom_son (CDI_DOMINATORS, bb);
       son;
       son = next_dom_son (CDI_DOMINATORS, son))
    {
      if (!flow_bb_inside_loop_p (loop, son))
	continue;

      if (dominated_by_p (CDI_DOMINATORS, loop->latch, son))
	{
	  postpone = son;
	  continue;
	}
      fill_sons_in_loop (loop, son, tovisit, tv);
    }

  if (postpone)
    fill_sons_in_loop (loop, postpone, tovisit, tv);
}

/* Gets body of a LOOP (that must be different from the outermost loop)
   sorted by dominance relation.  Additionally, if a basic block s dominates
   the latch, then only blocks dominated by s are be after it.  */

basic_block *
get_loop_body_in_dom_order (const struct loop *loop)
{
  basic_block *tovisit;
  int tv;

  gcc_assert (loop->num_nodes);

  tovisit = XCNEWVEC (basic_block, loop->num_nodes);

  gcc_assert (loop->latch != EXIT_BLOCK_PTR);

  tv = 0;
  fill_sons_in_loop (loop, loop->header, tovisit, &tv);

  gcc_assert (tv == (int) loop->num_nodes);

  return tovisit;
}

/* Get body of a LOOP in breadth first sort order.  */

basic_block *
get_loop_body_in_bfs_order (const struct loop *loop)
{
  basic_block *blocks;
  basic_block bb;
  bitmap visited;
  unsigned int i = 0;
  unsigned int vc = 1;

  gcc_assert (loop->num_nodes);
  gcc_assert (loop->latch != EXIT_BLOCK_PTR);

  blocks = XCNEWVEC (basic_block, loop->num_nodes);
  visited = BITMAP_ALLOC (NULL);

  bb = loop->header;
  while (i < loop->num_nodes)
    {
      edge e;
      edge_iterator ei;

      if (!bitmap_bit_p (visited, bb->index))
	{
	  /* This basic block is now visited */
	  bitmap_set_bit (visited, bb->index);
	  blocks[i++] = bb;
	}

      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (flow_bb_inside_loop_p (loop, e->dest))
	    {
	      if (!bitmap_bit_p (visited, e->dest->index))
		{
		  bitmap_set_bit (visited, e->dest->index);
		  blocks[i++] = e->dest;
		}
	    }
	}

      gcc_assert (i >= vc);

      bb = blocks[vc++];
    }

  BITMAP_FREE (visited);
  return blocks;
}

/* Gets exit edges of a LOOP, returning their number in N_EDGES.  */
edge *
get_loop_exit_edges (const struct loop *loop, unsigned int *num_edges)
{
  edge *edges, e;
  unsigned i, n;
  basic_block * body;
  edge_iterator ei;

  gcc_assert (loop->latch != EXIT_BLOCK_PTR);

  body = get_loop_body (loop);
  n = 0;
  for (i = 0; i < loop->num_nodes; i++)
    FOR_EACH_EDGE (e, ei, body[i]->succs)
      if (!flow_bb_inside_loop_p (loop, e->dest))
	n++;
  edges = XNEWVEC (edge, n);
  *num_edges = n;
  n = 0;
  for (i = 0; i < loop->num_nodes; i++)
    FOR_EACH_EDGE (e, ei, body[i]->succs)
      if (!flow_bb_inside_loop_p (loop, e->dest))
	edges[n++] = e;
  free (body);

  return edges;
}

/* Counts the number of conditional branches inside LOOP.  */

unsigned
num_loop_branches (const struct loop *loop)
{
  unsigned i, n;
  basic_block * body;

  gcc_assert (loop->latch != EXIT_BLOCK_PTR);

  body = get_loop_body (loop);
  n = 0;
  for (i = 0; i < loop->num_nodes; i++)
    if (EDGE_COUNT (body[i]->succs) >= 2)
      n++;
  free (body);

  return n;
}

/* Adds basic block BB to LOOP.  */
void
add_bb_to_loop (basic_block bb, struct loop *loop)
{
   int i;

   bb->loop_father = loop;
   bb->loop_depth = loop->depth;
   loop->num_nodes++;
   for (i = 0; i < loop->depth; i++)
     loop->pred[i]->num_nodes++;
 }

/* Remove basic block BB from loops.  */
void
remove_bb_from_loops (basic_block bb)
{
   int i;
   struct loop *loop = bb->loop_father;

   loop->num_nodes--;
   for (i = 0; i < loop->depth; i++)
     loop->pred[i]->num_nodes--;
   bb->loop_father = NULL;
   bb->loop_depth = 0;
}

/* Finds nearest common ancestor in loop tree for given loops.  */
struct loop *
find_common_loop (struct loop *loop_s, struct loop *loop_d)
{
  if (!loop_s) return loop_d;
  if (!loop_d) return loop_s;

  if (loop_s->depth < loop_d->depth)
    loop_d = loop_d->pred[loop_s->depth];
  else if (loop_s->depth > loop_d->depth)
    loop_s = loop_s->pred[loop_d->depth];

  while (loop_s != loop_d)
    {
      loop_s = loop_s->outer;
      loop_d = loop_d->outer;
    }
  return loop_s;
}

/* Cancels the LOOP; it must be innermost one.  */

static void
cancel_loop (struct loops *loops, struct loop *loop)
{
  basic_block *bbs;
  unsigned i;

  gcc_assert (!loop->inner);

  /* Move blocks up one level (they should be removed as soon as possible).  */
  bbs = get_loop_body (loop);
  for (i = 0; i < loop->num_nodes; i++)
    bbs[i]->loop_father = loop->outer;

  /* Remove the loop from structure.  */
  flow_loop_tree_node_remove (loop);

  /* Remove loop from loops array.  */
  loops->parray[loop->num] = NULL;

  /* Free loop data.  */
  flow_loop_free (loop);
}

/* Cancels LOOP and all its subloops.  */
void
cancel_loop_tree (struct loops *loops, struct loop *loop)
{
  while (loop->inner)
    cancel_loop_tree (loops, loop->inner);
  cancel_loop (loops, loop);
}

/* Checks that LOOPS are all right:
     -- sizes of loops are all right
     -- results of get_loop_body really belong to the loop
     -- loop header have just single entry edge and single latch edge
     -- loop latches have only single successor that is header of their loop
     -- irreducible loops are correctly marked
  */
void
verify_loop_structure (struct loops *loops)
{
  unsigned *sizes, i, j;
  sbitmap irreds;
  basic_block *bbs, bb;
  struct loop *loop;
  int err = 0;
  edge e;

  /* Check sizes.  */
  sizes = XCNEWVEC (unsigned, loops->num);
  sizes[0] = 2;

  FOR_EACH_BB (bb)
    for (loop = bb->loop_father; loop; loop = loop->outer)
      sizes[loop->num]++;

  for (i = 0; i < loops->num; i++)
    {
      if (!loops->parray[i])
	continue;

      if (loops->parray[i]->num_nodes != sizes[i])
	{
	  error ("size of loop %d should be %d, not %d",
		   i, sizes[i], loops->parray[i]->num_nodes);
	  err = 1;
	}
    }

  /* Check get_loop_body.  */
  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;
      bbs = get_loop_body (loop);

      for (j = 0; j < loop->num_nodes; j++)
	if (!flow_bb_inside_loop_p (loop, bbs[j]))
	  {
	    error ("bb %d do not belong to loop %d",
		    bbs[j]->index, i);
	    err = 1;
	  }
      free (bbs);
    }

  /* Check headers and latches.  */
  for (i = 1; i < loops->num; i++)
    {
      loop = loops->parray[i];
      if (!loop)
	continue;

      if ((loops->state & LOOPS_HAVE_PREHEADERS)
	  && EDGE_COUNT (loop->header->preds) != 2)
	{
	  error ("loop %d's header does not have exactly 2 entries", i);
	  err = 1;
	}
      if (loops->state & LOOPS_HAVE_SIMPLE_LATCHES)
	{
	  if (!single_succ_p (loop->latch))
	    {
	      error ("loop %d's latch does not have exactly 1 successor", i);
	      err = 1;
	    }
	  if (single_succ (loop->latch) != loop->header)
	    {
	      error ("loop %d's latch does not have header as successor", i);
	      err = 1;
	    }
	  if (loop->latch->loop_father != loop)
	    {
	      error ("loop %d's latch does not belong directly to it", i);
	      err = 1;
	    }
	}
      if (loop->header->loop_father != loop)
	{
	  error ("loop %d's header does not belong directly to it", i);
	  err = 1;
	}
      if ((loops->state & LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS)
	  && (loop_latch_edge (loop)->flags & EDGE_IRREDUCIBLE_LOOP))
	{
	  error ("loop %d's latch is marked as part of irreducible region", i);
	  err = 1;
	}
    }

  /* Check irreducible loops.  */
  if (loops->state & LOOPS_HAVE_MARKED_IRREDUCIBLE_REGIONS)
    {
      /* Record old info.  */
      irreds = sbitmap_alloc (last_basic_block);
      FOR_EACH_BB (bb)
	{
	  edge_iterator ei;
	  if (bb->flags & BB_IRREDUCIBLE_LOOP)
	    SET_BIT (irreds, bb->index);
	  else
	    RESET_BIT (irreds, bb->index);
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    if (e->flags & EDGE_IRREDUCIBLE_LOOP)
	      e->flags |= EDGE_ALL_FLAGS + 1;
	}

      /* Recount it.  */
      mark_irreducible_loops (loops);

      /* Compare.  */
      FOR_EACH_BB (bb)
	{
	  edge_iterator ei;

	  if ((bb->flags & BB_IRREDUCIBLE_LOOP)
	      && !TEST_BIT (irreds, bb->index))
	    {
	      error ("basic block %d should be marked irreducible", bb->index);
	      err = 1;
	    }
	  else if (!(bb->flags & BB_IRREDUCIBLE_LOOP)
	      && TEST_BIT (irreds, bb->index))
	    {
	      error ("basic block %d should not be marked irreducible", bb->index);
	      err = 1;
	    }
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      if ((e->flags & EDGE_IRREDUCIBLE_LOOP)
		  && !(e->flags & (EDGE_ALL_FLAGS + 1)))
		{
		  error ("edge from %d to %d should be marked irreducible",
			 e->src->index, e->dest->index);
		  err = 1;
		}
	      else if (!(e->flags & EDGE_IRREDUCIBLE_LOOP)
		       && (e->flags & (EDGE_ALL_FLAGS + 1)))
		{
		  error ("edge from %d to %d should not be marked irreducible",
			 e->src->index, e->dest->index);
		  err = 1;
		}
	      e->flags &= ~(EDGE_ALL_FLAGS + 1);
	    }
	}
      free (irreds);
    }

  /* Check the single_exit.  */
  if (loops->state & LOOPS_HAVE_MARKED_SINGLE_EXITS)
    {
      memset (sizes, 0, sizeof (unsigned) * loops->num);
      FOR_EACH_BB (bb)
	{
	  edge_iterator ei;
	  if (bb->loop_father == loops->tree_root)
	    continue;
	  FOR_EACH_EDGE (e, ei, bb->succs)
	    {
	      if (e->dest == EXIT_BLOCK_PTR)
		continue;

	      if (flow_bb_inside_loop_p (bb->loop_father, e->dest))
		continue;

	      for (loop = bb->loop_father;
		   loop != e->dest->loop_father;
		   loop = loop->outer)
		{
		  sizes[loop->num]++;
		  if (loop->single_exit
		      && loop->single_exit != e)
		    {
		      error ("wrong single exit %d->%d recorded for loop %d",
			     loop->single_exit->src->index,
			     loop->single_exit->dest->index,
			     loop->num);
		      error ("right exit is %d->%d",
			     e->src->index, e->dest->index);
		      err = 1;
		    }
		}
	    }
	}

      for (i = 1; i < loops->num; i++)
	{
	  loop = loops->parray[i];
	  if (!loop)
	    continue;

	  if (sizes[i] == 1
	      && !loop->single_exit)
	    {
	      error ("single exit not recorded for loop %d", loop->num);
	      err = 1;
	    }

	  if (sizes[i] != 1
	      && loop->single_exit)
	    {
	      error ("loop %d should not have single exit (%d -> %d)",
		     loop->num,
		     loop->single_exit->src->index,
		     loop->single_exit->dest->index);
	      err = 1;
	    }
	}
    }

  gcc_assert (!err);

  free (sizes);
}

/* Returns latch edge of LOOP.  */
edge
loop_latch_edge (const struct loop *loop)
{
  return find_edge (loop->latch, loop->header);
}

/* Returns preheader edge of LOOP.  */
edge
loop_preheader_edge (const struct loop *loop)
{
  edge e;
  edge_iterator ei;

  FOR_EACH_EDGE (e, ei, loop->header->preds)
    if (e->src != loop->latch)
      break;

  return e;
}

/* Returns true if E is an exit of LOOP.  */

bool
loop_exit_edge_p (const struct loop *loop, edge e)
{
  return (flow_bb_inside_loop_p (loop, e->src)
	  && !flow_bb_inside_loop_p (loop, e->dest));
}
