/* Calculate (post)dominators in slightly super-linear time.
   Copyright (C) 2000, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Michael Matz (matz@ifh.de).

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This file implements the well known algorithm from Lengauer and Tarjan
   to compute the dominators in a control flow graph.  A basic block D is said
   to dominate another block X, when all paths from the entry node of the CFG
   to X go also over D.  The dominance relation is a transitive reflexive
   relation and its minimal transitive reduction is a tree, called the
   dominator tree.  So for each block X besides the entry block exists a
   block I(X), called the immediate dominator of X, which is the parent of X
   in the dominator tree.

   The algorithm computes this dominator tree implicitly by computing for
   each block its immediate dominator.  We use tree balancing and path
   compression, so it's the O(e*a(e,v)) variant, where a(e,v) is the very
   slowly growing functional inverse of the Ackerman function.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "hard-reg-set.h"
#include "obstack.h"
#include "basic-block.h"
#include "toplev.h"
#include "et-forest.h"
#include "timevar.h"

/* Whether the dominators and the postdominators are available.  */
enum dom_state dom_computed[2];

/* We name our nodes with integers, beginning with 1.  Zero is reserved for
   'undefined' or 'end of list'.  The name of each node is given by the dfs
   number of the corresponding basic block.  Please note, that we include the
   artificial ENTRY_BLOCK (or EXIT_BLOCK in the post-dom case) in our lists to
   support multiple entry points.  Its dfs number is of course 1.  */

/* Type of Basic Block aka. TBB */
typedef unsigned int TBB;

/* We work in a poor-mans object oriented fashion, and carry an instance of
   this structure through all our 'methods'.  It holds various arrays
   reflecting the (sub)structure of the flowgraph.  Most of them are of type
   TBB and are also indexed by TBB.  */

struct dom_info
{
  /* The parent of a node in the DFS tree.  */
  TBB *dfs_parent;
  /* For a node x key[x] is roughly the node nearest to the root from which
     exists a way to x only over nodes behind x.  Such a node is also called
     semidominator.  */
  TBB *key;
  /* The value in path_min[x] is the node y on the path from x to the root of
     the tree x is in with the smallest key[y].  */
  TBB *path_min;
  /* bucket[x] points to the first node of the set of nodes having x as key.  */
  TBB *bucket;
  /* And next_bucket[x] points to the next node.  */
  TBB *next_bucket;
  /* After the algorithm is done, dom[x] contains the immediate dominator
     of x.  */
  TBB *dom;

  /* The following few fields implement the structures needed for disjoint
     sets.  */
  /* set_chain[x] is the next node on the path from x to the representant
     of the set containing x.  If set_chain[x]==0 then x is a root.  */
  TBB *set_chain;
  /* set_size[x] is the number of elements in the set named by x.  */
  unsigned int *set_size;
  /* set_child[x] is used for balancing the tree representing a set.  It can
     be understood as the next sibling of x.  */
  TBB *set_child;

  /* If b is the number of a basic block (BB->index), dfs_order[b] is the
     number of that node in DFS order counted from 1.  This is an index
     into most of the other arrays in this structure.  */
  TBB *dfs_order;
  /* If x is the DFS-index of a node which corresponds with a basic block,
     dfs_to_bb[x] is that basic block.  Note, that in our structure there are
     more nodes that basic blocks, so only dfs_to_bb[dfs_order[bb->index]]==bb
     is true for every basic block bb, but not the opposite.  */
  basic_block *dfs_to_bb;

  /* This is the next free DFS number when creating the DFS tree.  */
  unsigned int dfsnum;
  /* The number of nodes in the DFS tree (==dfsnum-1).  */
  unsigned int nodes;

  /* Blocks with bits set here have a fake edge to EXIT.  These are used
     to turn a DFS forest into a proper tree.  */
  bitmap fake_exit_edge;
};

static void init_dom_info (struct dom_info *, enum cdi_direction);
static void free_dom_info (struct dom_info *);
static void calc_dfs_tree_nonrec (struct dom_info *, basic_block,
				  enum cdi_direction);
static void calc_dfs_tree (struct dom_info *, enum cdi_direction);
static void compress (struct dom_info *, TBB);
static TBB eval (struct dom_info *, TBB);
static void link_roots (struct dom_info *, TBB, TBB);
static void calc_idoms (struct dom_info *, enum cdi_direction);
void debug_dominance_info (enum cdi_direction);

/* Keeps track of the*/
static unsigned n_bbs_in_dom_tree[2];

/* Helper macro for allocating and initializing an array,
   for aesthetic reasons.  */
#define init_ar(var, type, num, content)			\
  do								\
    {								\
      unsigned int i = 1;    /* Catch content == i.  */		\
      if (! (content))						\
	(var) = XCNEWVEC (type, num);				\
      else							\
	{							\
	  (var) = XNEWVEC (type, (num));			\
	  for (i = 0; i < num; i++)				\
	    (var)[i] = (content);				\
	}							\
    }								\
  while (0)

/* Allocate all needed memory in a pessimistic fashion (so we round up).
   This initializes the contents of DI, which already must be allocated.  */

static void
init_dom_info (struct dom_info *di, enum cdi_direction dir)
{
  unsigned int num = n_basic_blocks;
  init_ar (di->dfs_parent, TBB, num, 0);
  init_ar (di->path_min, TBB, num, i);
  init_ar (di->key, TBB, num, i);
  init_ar (di->dom, TBB, num, 0);

  init_ar (di->bucket, TBB, num, 0);
  init_ar (di->next_bucket, TBB, num, 0);

  init_ar (di->set_chain, TBB, num, 0);
  init_ar (di->set_size, unsigned int, num, 1);
  init_ar (di->set_child, TBB, num, 0);

  init_ar (di->dfs_order, TBB, (unsigned int) last_basic_block + 1, 0);
  init_ar (di->dfs_to_bb, basic_block, num, 0);

  di->dfsnum = 1;
  di->nodes = 0;

  di->fake_exit_edge = dir ? BITMAP_ALLOC (NULL) : NULL;
}

#undef init_ar

/* Free all allocated memory in DI, but not DI itself.  */

static void
free_dom_info (struct dom_info *di)
{
  free (di->dfs_parent);
  free (di->path_min);
  free (di->key);
  free (di->dom);
  free (di->bucket);
  free (di->next_bucket);
  free (di->set_chain);
  free (di->set_size);
  free (di->set_child);
  free (di->dfs_order);
  free (di->dfs_to_bb);
  BITMAP_FREE (di->fake_exit_edge);
}

/* The nonrecursive variant of creating a DFS tree.  DI is our working
   structure, BB the starting basic block for this tree and REVERSE
   is true, if predecessors should be visited instead of successors of a
   node.  After this is done all nodes reachable from BB were visited, have
   assigned their dfs number and are linked together to form a tree.  */

static void
calc_dfs_tree_nonrec (struct dom_info *di, basic_block bb,
		      enum cdi_direction reverse)
{
  /* We call this _only_ if bb is not already visited.  */
  edge e;
  TBB child_i, my_i = 0;
  edge_iterator *stack;
  edge_iterator ei, einext;
  int sp;
  /* Start block (ENTRY_BLOCK_PTR for forward problem, EXIT_BLOCK for backward
     problem).  */
  basic_block en_block;
  /* Ending block.  */
  basic_block ex_block;

  stack = XNEWVEC (edge_iterator, n_basic_blocks + 1);
  sp = 0;

  /* Initialize our border blocks, and the first edge.  */
  if (reverse)
    {
      ei = ei_start (bb->preds);
      en_block = EXIT_BLOCK_PTR;
      ex_block = ENTRY_BLOCK_PTR;
    }
  else
    {
      ei = ei_start (bb->succs);
      en_block = ENTRY_BLOCK_PTR;
      ex_block = EXIT_BLOCK_PTR;
    }

  /* When the stack is empty we break out of this loop.  */
  while (1)
    {
      basic_block bn;

      /* This loop traverses edges e in depth first manner, and fills the
         stack.  */
      while (!ei_end_p (ei))
	{
	  e = ei_edge (ei);

	  /* Deduce from E the current and the next block (BB and BN), and the
	     next edge.  */
	  if (reverse)
	    {
	      bn = e->src;

	      /* If the next node BN is either already visited or a border
	         block the current edge is useless, and simply overwritten
	         with the next edge out of the current node.  */
	      if (bn == ex_block || di->dfs_order[bn->index])
		{
		  ei_next (&ei);
		  continue;
		}
	      bb = e->dest;
	      einext = ei_start (bn->preds);
	    }
	  else
	    {
	      bn = e->dest;
	      if (bn == ex_block || di->dfs_order[bn->index])
		{
		  ei_next (&ei);
		  continue;
		}
	      bb = e->src;
	      einext = ei_start (bn->succs);
	    }

	  gcc_assert (bn != en_block);

	  /* Fill the DFS tree info calculatable _before_ recursing.  */
	  if (bb != en_block)
	    my_i = di->dfs_order[bb->index];
	  else
	    my_i = di->dfs_order[last_basic_block];
	  child_i = di->dfs_order[bn->index] = di->dfsnum++;
	  di->dfs_to_bb[child_i] = bn;
	  di->dfs_parent[child_i] = my_i;

	  /* Save the current point in the CFG on the stack, and recurse.  */
	  stack[sp++] = ei;
	  ei = einext;
	}

      if (!sp)
	break;
      ei = stack[--sp];

      /* OK.  The edge-list was exhausted, meaning normally we would
         end the recursion.  After returning from the recursive call,
         there were (may be) other statements which were run after a
         child node was completely considered by DFS.  Here is the
         point to do it in the non-recursive variant.
         E.g. The block just completed is in e->dest for forward DFS,
         the block not yet completed (the parent of the one above)
         in e->src.  This could be used e.g. for computing the number of
         descendants or the tree depth.  */
      ei_next (&ei);
    }
  free (stack);
}

/* The main entry for calculating the DFS tree or forest.  DI is our working
   structure and REVERSE is true, if we are interested in the reverse flow
   graph.  In that case the result is not necessarily a tree but a forest,
   because there may be nodes from which the EXIT_BLOCK is unreachable.  */

static void
calc_dfs_tree (struct dom_info *di, enum cdi_direction reverse)
{
  /* The first block is the ENTRY_BLOCK (or EXIT_BLOCK if REVERSE).  */
  basic_block begin = reverse ? EXIT_BLOCK_PTR : ENTRY_BLOCK_PTR;
  di->dfs_order[last_basic_block] = di->dfsnum;
  di->dfs_to_bb[di->dfsnum] = begin;
  di->dfsnum++;

  calc_dfs_tree_nonrec (di, begin, reverse);

  if (reverse)
    {
      /* In the post-dom case we may have nodes without a path to EXIT_BLOCK.
         They are reverse-unreachable.  In the dom-case we disallow such
         nodes, but in post-dom we have to deal with them.

	 There are two situations in which this occurs.  First, noreturn
	 functions.  Second, infinite loops.  In the first case we need to
	 pretend that there is an edge to the exit block.  In the second
	 case, we wind up with a forest.  We need to process all noreturn
	 blocks before we know if we've got any infinite loops.  */

      basic_block b;
      bool saw_unconnected = false;

      FOR_EACH_BB_REVERSE (b)
	{
	  if (EDGE_COUNT (b->succs) > 0)
	    {
	      if (di->dfs_order[b->index] == 0)
		saw_unconnected = true;
	      continue;
	    }
	  bitmap_set_bit (di->fake_exit_edge, b->index);
	  di->dfs_order[b->index] = di->dfsnum;
	  di->dfs_to_bb[di->dfsnum] = b;
	  di->dfs_parent[di->dfsnum] = di->dfs_order[last_basic_block];
	  di->dfsnum++;
	  calc_dfs_tree_nonrec (di, b, reverse);
	}

      if (saw_unconnected)
	{
	  FOR_EACH_BB_REVERSE (b)
	    {
	      if (di->dfs_order[b->index])
		continue;
	      bitmap_set_bit (di->fake_exit_edge, b->index);
	      di->dfs_order[b->index] = di->dfsnum;
	      di->dfs_to_bb[di->dfsnum] = b;
	      di->dfs_parent[di->dfsnum] = di->dfs_order[last_basic_block];
	      di->dfsnum++;
	      calc_dfs_tree_nonrec (di, b, reverse);
	    }
	}
    }

  di->nodes = di->dfsnum - 1;

  /* This aborts e.g. when there is _no_ path from ENTRY to EXIT at all.  */
  gcc_assert (di->nodes == (unsigned int) n_basic_blocks - 1);
}

/* Compress the path from V to the root of its set and update path_min at the
   same time.  After compress(di, V) set_chain[V] is the root of the set V is
   in and path_min[V] is the node with the smallest key[] value on the path
   from V to that root.  */

static void
compress (struct dom_info *di, TBB v)
{
  /* Btw. It's not worth to unrecurse compress() as the depth is usually not
     greater than 5 even for huge graphs (I've not seen call depth > 4).
     Also performance wise compress() ranges _far_ behind eval().  */
  TBB parent = di->set_chain[v];
  if (di->set_chain[parent])
    {
      compress (di, parent);
      if (di->key[di->path_min[parent]] < di->key[di->path_min[v]])
	di->path_min[v] = di->path_min[parent];
      di->set_chain[v] = di->set_chain[parent];
    }
}

/* Compress the path from V to the set root of V if needed (when the root has
   changed since the last call).  Returns the node with the smallest key[]
   value on the path from V to the root.  */

static inline TBB
eval (struct dom_info *di, TBB v)
{
  /* The representant of the set V is in, also called root (as the set
     representation is a tree).  */
  TBB rep = di->set_chain[v];

  /* V itself is the root.  */
  if (!rep)
    return di->path_min[v];

  /* Compress only if necessary.  */
  if (di->set_chain[rep])
    {
      compress (di, v);
      rep = di->set_chain[v];
    }

  if (di->key[di->path_min[rep]] >= di->key[di->path_min[v]])
    return di->path_min[v];
  else
    return di->path_min[rep];
}

/* This essentially merges the two sets of V and W, giving a single set with
   the new root V.  The internal representation of these disjoint sets is a
   balanced tree.  Currently link(V,W) is only used with V being the parent
   of W.  */

static void
link_roots (struct dom_info *di, TBB v, TBB w)
{
  TBB s = w;

  /* Rebalance the tree.  */
  while (di->key[di->path_min[w]] < di->key[di->path_min[di->set_child[s]]])
    {
      if (di->set_size[s] + di->set_size[di->set_child[di->set_child[s]]]
	  >= 2 * di->set_size[di->set_child[s]])
	{
	  di->set_chain[di->set_child[s]] = s;
	  di->set_child[s] = di->set_child[di->set_child[s]];
	}
      else
	{
	  di->set_size[di->set_child[s]] = di->set_size[s];
	  s = di->set_chain[s] = di->set_child[s];
	}
    }

  di->path_min[s] = di->path_min[w];
  di->set_size[v] += di->set_size[w];
  if (di->set_size[v] < 2 * di->set_size[w])
    {
      TBB tmp = s;
      s = di->set_child[v];
      di->set_child[v] = tmp;
    }

  /* Merge all subtrees.  */
  while (s)
    {
      di->set_chain[s] = v;
      s = di->set_child[s];
    }
}

/* This calculates the immediate dominators (or post-dominators if REVERSE is
   true).  DI is our working structure and should hold the DFS forest.
   On return the immediate dominator to node V is in di->dom[V].  */

static void
calc_idoms (struct dom_info *di, enum cdi_direction reverse)
{
  TBB v, w, k, par;
  basic_block en_block;
  edge_iterator ei, einext;

  if (reverse)
    en_block = EXIT_BLOCK_PTR;
  else
    en_block = ENTRY_BLOCK_PTR;

  /* Go backwards in DFS order, to first look at the leafs.  */
  v = di->nodes;
  while (v > 1)
    {
      basic_block bb = di->dfs_to_bb[v];
      edge e;

      par = di->dfs_parent[v];
      k = v;

      ei = (reverse) ? ei_start (bb->succs) : ei_start (bb->preds);

      if (reverse)
	{
	  /* If this block has a fake edge to exit, process that first.  */
	  if (bitmap_bit_p (di->fake_exit_edge, bb->index))
	    {
	      einext = ei;
	      einext.index = 0;
	      goto do_fake_exit_edge;
	    }
	}

      /* Search all direct predecessors for the smallest node with a path
         to them.  That way we have the smallest node with also a path to
         us only over nodes behind us.  In effect we search for our
         semidominator.  */
      while (!ei_end_p (ei))
	{
	  TBB k1;
	  basic_block b;

	  e = ei_edge (ei);
	  b = (reverse) ? e->dest : e->src;
	  einext = ei;
	  ei_next (&einext);

	  if (b == en_block)
	    {
	    do_fake_exit_edge:
	      k1 = di->dfs_order[last_basic_block];
	    }
	  else
	    k1 = di->dfs_order[b->index];

	  /* Call eval() only if really needed.  If k1 is above V in DFS tree,
	     then we know, that eval(k1) == k1 and key[k1] == k1.  */
	  if (k1 > v)
	    k1 = di->key[eval (di, k1)];
	  if (k1 < k)
	    k = k1;

	  ei = einext;
	}

      di->key[v] = k;
      link_roots (di, par, v);
      di->next_bucket[v] = di->bucket[k];
      di->bucket[k] = v;

      /* Transform semidominators into dominators.  */
      for (w = di->bucket[par]; w; w = di->next_bucket[w])
	{
	  k = eval (di, w);
	  if (di->key[k] < di->key[w])
	    di->dom[w] = k;
	  else
	    di->dom[w] = par;
	}
      /* We don't need to cleanup next_bucket[].  */
      di->bucket[par] = 0;
      v--;
    }

  /* Explicitly define the dominators.  */
  di->dom[1] = 0;
  for (v = 2; v <= di->nodes; v++)
    if (di->dom[v] != di->key[v])
      di->dom[v] = di->dom[di->dom[v]];
}

/* Assign dfs numbers starting from NUM to NODE and its sons.  */

static void
assign_dfs_numbers (struct et_node *node, int *num)
{
  struct et_node *son;

  node->dfs_num_in = (*num)++;

  if (node->son)
    {
      assign_dfs_numbers (node->son, num);
      for (son = node->son->right; son != node->son; son = son->right)
	assign_dfs_numbers (son, num);
    }

  node->dfs_num_out = (*num)++;
}

/* Compute the data necessary for fast resolving of dominator queries in a
   static dominator tree.  */

static void
compute_dom_fast_query (enum cdi_direction dir)
{
  int num = 0;
  basic_block bb;

  gcc_assert (dom_info_available_p (dir));

  if (dom_computed[dir] == DOM_OK)
    return;

  FOR_ALL_BB (bb)
    {
      if (!bb->dom[dir]->father)
	assign_dfs_numbers (bb->dom[dir], &num);
    }

  dom_computed[dir] = DOM_OK;
}

/* The main entry point into this module.  DIR is set depending on whether
   we want to compute dominators or postdominators.  */

void
calculate_dominance_info (enum cdi_direction dir)
{
  struct dom_info di;
  basic_block b;

  if (dom_computed[dir] == DOM_OK)
    return;

  timevar_push (TV_DOMINANCE);
  if (!dom_info_available_p (dir))
    {
      gcc_assert (!n_bbs_in_dom_tree[dir]);

      FOR_ALL_BB (b)
	{
	  b->dom[dir] = et_new_tree (b);
	}
      n_bbs_in_dom_tree[dir] = n_basic_blocks;

      init_dom_info (&di, dir);
      calc_dfs_tree (&di, dir);
      calc_idoms (&di, dir);

      FOR_EACH_BB (b)
	{
	  TBB d = di.dom[di.dfs_order[b->index]];

	  if (di.dfs_to_bb[d])
	    et_set_father (b->dom[dir], di.dfs_to_bb[d]->dom[dir]);
	}

      free_dom_info (&di);
      dom_computed[dir] = DOM_NO_FAST_QUERY;
    }

  compute_dom_fast_query (dir);

  timevar_pop (TV_DOMINANCE);
}

/* Free dominance information for direction DIR.  */
void
free_dominance_info (enum cdi_direction dir)
{
  basic_block bb;

  if (!dom_info_available_p (dir))
    return;

  FOR_ALL_BB (bb)
    {
      et_free_tree_force (bb->dom[dir]);
      bb->dom[dir] = NULL;
    }
  et_free_pools ();

  n_bbs_in_dom_tree[dir] = 0;

  dom_computed[dir] = DOM_NONE;
}

/* Return the immediate dominator of basic block BB.  */
basic_block
get_immediate_dominator (enum cdi_direction dir, basic_block bb)
{
  struct et_node *node = bb->dom[dir];

  gcc_assert (dom_computed[dir]);

  if (!node->father)
    return NULL;

  return node->father->data;
}

/* Set the immediate dominator of the block possibly removing
   existing edge.  NULL can be used to remove any edge.  */
inline void
set_immediate_dominator (enum cdi_direction dir, basic_block bb,
			 basic_block dominated_by)
{
  struct et_node *node = bb->dom[dir];

  gcc_assert (dom_computed[dir]);

  if (node->father)
    {
      if (node->father->data == dominated_by)
	return;
      et_split (node);
    }

  if (dominated_by)
    et_set_father (node, dominated_by->dom[dir]);

  if (dom_computed[dir] == DOM_OK)
    dom_computed[dir] = DOM_NO_FAST_QUERY;
}

/* Store all basic blocks immediately dominated by BB into BBS and return
   their number.  */
int
get_dominated_by (enum cdi_direction dir, basic_block bb, basic_block **bbs)
{
  int n;
  struct et_node *node = bb->dom[dir], *son = node->son, *ason;

  gcc_assert (dom_computed[dir]);

  if (!son)
    {
      *bbs = NULL;
      return 0;
    }

  for (ason = son->right, n = 1; ason != son; ason = ason->right)
    n++;

  *bbs = XNEWVEC (basic_block, n);
  (*bbs)[0] = son->data;
  for (ason = son->right, n = 1; ason != son; ason = ason->right)
    (*bbs)[n++] = ason->data;

  return n;
}

/* Find all basic blocks that are immediately dominated (in direction DIR)
   by some block between N_REGION ones stored in REGION, except for blocks
   in the REGION itself.  The found blocks are stored to DOMS and their number
   is returned.  */

unsigned
get_dominated_by_region (enum cdi_direction dir, basic_block *region,
			 unsigned n_region, basic_block *doms)
{
  unsigned n_doms = 0, i;
  basic_block dom;

  for (i = 0; i < n_region; i++)
    region[i]->flags |= BB_DUPLICATED;
  for (i = 0; i < n_region; i++)
    for (dom = first_dom_son (dir, region[i]);
	 dom;
	 dom = next_dom_son (dir, dom))
      if (!(dom->flags & BB_DUPLICATED))
	doms[n_doms++] = dom;
  for (i = 0; i < n_region; i++)
    region[i]->flags &= ~BB_DUPLICATED;

  return n_doms;
}

/* Redirect all edges pointing to BB to TO.  */
void
redirect_immediate_dominators (enum cdi_direction dir, basic_block bb,
			       basic_block to)
{
  struct et_node *bb_node = bb->dom[dir], *to_node = to->dom[dir], *son;

  gcc_assert (dom_computed[dir]);

  if (!bb_node->son)
    return;

  while (bb_node->son)
    {
      son = bb_node->son;

      et_split (son);
      et_set_father (son, to_node);
    }

  if (dom_computed[dir] == DOM_OK)
    dom_computed[dir] = DOM_NO_FAST_QUERY;
}

/* Find first basic block in the tree dominating both BB1 and BB2.  */
basic_block
nearest_common_dominator (enum cdi_direction dir, basic_block bb1, basic_block bb2)
{
  gcc_assert (dom_computed[dir]);

  if (!bb1)
    return bb2;
  if (!bb2)
    return bb1;

  return et_nca (bb1->dom[dir], bb2->dom[dir])->data;
}


/* Find the nearest common dominator for the basic blocks in BLOCKS,
   using dominance direction DIR.  */

basic_block
nearest_common_dominator_for_set (enum cdi_direction dir, bitmap blocks)
{
  unsigned i, first;
  bitmap_iterator bi;
  basic_block dom;
  
  first = bitmap_first_set_bit (blocks);
  dom = BASIC_BLOCK (first);
  EXECUTE_IF_SET_IN_BITMAP (blocks, 0, i, bi)
    if (dom != BASIC_BLOCK (i))
      dom = nearest_common_dominator (dir, dom, BASIC_BLOCK (i));

  return dom;
}

/*  Given a dominator tree, we can determine whether one thing
    dominates another in constant time by using two DFS numbers:

    1. The number for when we visit a node on the way down the tree
    2. The number for when we visit a node on the way back up the tree

    You can view these as bounds for the range of dfs numbers the
    nodes in the subtree of the dominator tree rooted at that node
    will contain.
    
    The dominator tree is always a simple acyclic tree, so there are
    only three possible relations two nodes in the dominator tree have
    to each other:
    
    1. Node A is above Node B (and thus, Node A dominates node B)

     A
     |
     C
    / \
   B   D


   In the above case, DFS_Number_In of A will be <= DFS_Number_In of
   B, and DFS_Number_Out of A will be >= DFS_Number_Out of B.  This is
   because we must hit A in the dominator tree *before* B on the walk
   down, and we will hit A *after* B on the walk back up
   
   2. Node A is below node B (and thus, node B dominates node A)
   
   
     B
     |
     A
    / \
   C   D

   In the above case, DFS_Number_In of A will be >= DFS_Number_In of
   B, and DFS_Number_Out of A will be <= DFS_Number_Out of B.
   
   This is because we must hit A in the dominator tree *after* B on
   the walk down, and we will hit A *before* B on the walk back up
   
   3. Node A and B are siblings (and thus, neither dominates the other)

     C
     |
     D
    / \
   A   B

   In the above case, DFS_Number_In of A will *always* be <=
   DFS_Number_In of B, and DFS_Number_Out of A will *always* be <=
   DFS_Number_Out of B.  This is because we will always finish the dfs
   walk of one of the subtrees before the other, and thus, the dfs
   numbers for one subtree can't intersect with the range of dfs
   numbers for the other subtree.  If you swap A and B's position in
   the dominator tree, the comparison changes direction, but the point
   is that both comparisons will always go the same way if there is no
   dominance relationship.

   Thus, it is sufficient to write

   A_Dominates_B (node A, node B)
   {
     return DFS_Number_In(A) <= DFS_Number_In(B) 
            && DFS_Number_Out (A) >= DFS_Number_Out(B);
   }

   A_Dominated_by_B (node A, node B)
   {
     return DFS_Number_In(A) >= DFS_Number_In(A)
            && DFS_Number_Out (A) <= DFS_Number_Out(B);
   }  */

/* Return TRUE in case BB1 is dominated by BB2.  */
bool
dominated_by_p (enum cdi_direction dir, basic_block bb1, basic_block bb2)
{ 
  struct et_node *n1 = bb1->dom[dir], *n2 = bb2->dom[dir];

  gcc_assert (dom_computed[dir]);

  if (dom_computed[dir] == DOM_OK)
    return (n1->dfs_num_in >= n2->dfs_num_in
  	    && n1->dfs_num_out <= n2->dfs_num_out);

  return et_below (n1, n2);
}

/* Returns the entry dfs number for basic block BB, in the direction DIR.  */

unsigned
bb_dom_dfs_in (enum cdi_direction dir, basic_block bb)
{
  struct et_node *n = bb->dom[dir];

  gcc_assert (dom_computed[dir] == DOM_OK);
  return n->dfs_num_in;
}

/* Returns the exit dfs number for basic block BB, in the direction DIR.  */

unsigned
bb_dom_dfs_out (enum cdi_direction dir, basic_block bb)
{
  struct et_node *n = bb->dom[dir];

  gcc_assert (dom_computed[dir] == DOM_OK);
  return n->dfs_num_out;
}

/* Verify invariants of dominator structure.  */
void
verify_dominators (enum cdi_direction dir)
{
  int err = 0;
  basic_block bb;

  gcc_assert (dom_info_available_p (dir));

  FOR_EACH_BB (bb)
    {
      basic_block dom_bb;
      basic_block imm_bb;

      dom_bb = recount_dominator (dir, bb);
      imm_bb = get_immediate_dominator (dir, bb);
      if (dom_bb != imm_bb)
	{
	  if ((dom_bb == NULL) || (imm_bb == NULL))
	    error ("dominator of %d status unknown", bb->index);
	  else
	    error ("dominator of %d should be %d, not %d",
		   bb->index, dom_bb->index, imm_bb->index);
	  err = 1;
	}
    }

  if (dir == CDI_DOMINATORS)
    {
      FOR_EACH_BB (bb)
	{
	  if (!dominated_by_p (dir, bb, ENTRY_BLOCK_PTR))
	    {
	      error ("ENTRY does not dominate bb %d", bb->index);
	      err = 1;
	    }
	}
    }

  gcc_assert (!err);
}

/* Determine immediate dominator (or postdominator, according to DIR) of BB,
   assuming that dominators of other blocks are correct.  We also use it to
   recompute the dominators in a restricted area, by iterating it until it
   reaches a fixed point.  */

basic_block
recount_dominator (enum cdi_direction dir, basic_block bb)
{
  basic_block dom_bb = NULL;
  edge e;
  edge_iterator ei;

  gcc_assert (dom_computed[dir]);

  if (dir == CDI_DOMINATORS)
    {
      FOR_EACH_EDGE (e, ei, bb->preds)
	{
	  /* Ignore the predecessors that either are not reachable from
	     the entry block, or whose dominator was not determined yet.  */
	  if (!dominated_by_p (dir, e->src, ENTRY_BLOCK_PTR))
	    continue;

	  if (!dominated_by_p (dir, e->src, bb))
	    dom_bb = nearest_common_dominator (dir, dom_bb, e->src);
	}
    }
  else
    {
      FOR_EACH_EDGE (e, ei, bb->succs)
	{
	  if (!dominated_by_p (dir, e->dest, bb))
	    dom_bb = nearest_common_dominator (dir, dom_bb, e->dest);
	}
    }

  return dom_bb;
}

/* Iteratively recount dominators of BBS. The change is supposed to be local
   and not to grow further.  */
void
iterate_fix_dominators (enum cdi_direction dir, basic_block *bbs, int n)
{
  int i, changed = 1;
  basic_block old_dom, new_dom;

  gcc_assert (dom_computed[dir]);

  for (i = 0; i < n; i++)
    set_immediate_dominator (dir, bbs[i], NULL);

  while (changed)
    {
      changed = 0;
      for (i = 0; i < n; i++)
	{
	  old_dom = get_immediate_dominator (dir, bbs[i]);
	  new_dom = recount_dominator (dir, bbs[i]);
	  if (old_dom != new_dom)
	    {
	      changed = 1;
	      set_immediate_dominator (dir, bbs[i], new_dom);
	    }
	}
    }

  for (i = 0; i < n; i++)
    gcc_assert (get_immediate_dominator (dir, bbs[i]));
}

void
add_to_dominance_info (enum cdi_direction dir, basic_block bb)
{
  gcc_assert (dom_computed[dir]);
  gcc_assert (!bb->dom[dir]);

  n_bbs_in_dom_tree[dir]++;
  
  bb->dom[dir] = et_new_tree (bb);

  if (dom_computed[dir] == DOM_OK)
    dom_computed[dir] = DOM_NO_FAST_QUERY;
}

void
delete_from_dominance_info (enum cdi_direction dir, basic_block bb)
{
  gcc_assert (dom_computed[dir]);

  et_free_tree (bb->dom[dir]);
  bb->dom[dir] = NULL;
  n_bbs_in_dom_tree[dir]--;

  if (dom_computed[dir] == DOM_OK)
    dom_computed[dir] = DOM_NO_FAST_QUERY;
}

/* Returns the first son of BB in the dominator or postdominator tree
   as determined by DIR.  */

basic_block
first_dom_son (enum cdi_direction dir, basic_block bb)
{
  struct et_node *son = bb->dom[dir]->son;

  return son ? son->data : NULL;
}

/* Returns the next dominance son after BB in the dominator or postdominator
   tree as determined by DIR, or NULL if it was the last one.  */

basic_block
next_dom_son (enum cdi_direction dir, basic_block bb)
{
  struct et_node *next = bb->dom[dir]->right;

  return next->father->son == next ? NULL : next->data;
}

/* Returns true if dominance information for direction DIR is available.  */

bool
dom_info_available_p (enum cdi_direction dir)
{
  return dom_computed[dir] != DOM_NONE;
}

void
debug_dominance_info (enum cdi_direction dir)
{
  basic_block bb, bb2;
  FOR_EACH_BB (bb)
    if ((bb2 = get_immediate_dominator (dir, bb)))
      fprintf (stderr, "%i %i\n", bb->index, bb2->index);
}
