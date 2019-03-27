/* Generic routines for manipulating PHIs
   Copyright (C) 2003, 2005 Free Software Foundation, Inc.

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
#include "tree.h"
#include "rtl.h"
#include "varray.h"
#include "ggc.h"
#include "basic-block.h"
#include "tree-flow.h"
#include "toplev.h"

/* Rewriting a function into SSA form can create a huge number of PHIs
   many of which may be thrown away shortly after their creation if jumps
   were threaded through PHI nodes.

   While our garbage collection mechanisms will handle this situation, it
   is extremely wasteful to create nodes and throw them away, especially
   when the nodes can be reused.

   For PR 8361, we can significantly reduce the number of nodes allocated
   and thus the total amount of memory allocated by managing PHIs a
   little.  This additionally helps reduce the amount of work done by the
   garbage collector.  Similar results have been seen on a wider variety
   of tests (such as the compiler itself).

   Right now we maintain our free list on a per-function basis.  It may
   or may not make sense to maintain the free list for the duration of
   a compilation unit.

   We could also use a zone allocator for these objects since they have
   a very well defined lifetime.  If someone wants to experiment with that
   this is the place to try it.

   PHI nodes have different sizes, so we can't have a single list of all
   the PHI nodes as it would be too expensive to walk down that list to
   find a PHI of a suitable size.

   Instead we have an array of lists of free PHI nodes.  The array is
   indexed by the number of PHI alternatives that PHI node can hold.
   Except for the last array member, which holds all remaining PHI
   nodes.

   So to find a free PHI node, we compute its index into the free PHI
   node array and see if there are any elements with an exact match.
   If so, then we are done.  Otherwise, we test the next larger size
   up and continue until we are in the last array element.

   We do not actually walk members of the last array element.  While it
   might allow us to pick up a few reusable PHI nodes, it could potentially
   be very expensive if the program has released a bunch of large PHI nodes,
   but keeps asking for even larger PHI nodes.  Experiments have shown that
   walking the elements of the last array entry would result in finding less
   than .1% additional reusable PHI nodes.

   Note that we can never have less than two PHI argument slots.  Thus,
   the -2 on all the calculations below.  */

#define NUM_BUCKETS 10
static GTY ((deletable (""))) tree free_phinodes[NUM_BUCKETS - 2];
static unsigned long free_phinode_count;

static int ideal_phi_node_len (int);
static void resize_phi_node (tree *, int);

#ifdef GATHER_STATISTICS
unsigned int phi_nodes_reused;
unsigned int phi_nodes_created;
#endif

/* Initialize management of PHIs.  */

void
init_phinodes (void)
{
  int i;

  for (i = 0; i < NUM_BUCKETS - 2; i++)
    free_phinodes[i] = NULL;
  free_phinode_count = 0;
}

/* Finalize management of PHIs.  */

void
fini_phinodes (void)
{
  int i;

  for (i = 0; i < NUM_BUCKETS - 2; i++)
    free_phinodes[i] = NULL;
  free_phinode_count = 0;
}

/* Dump some simple statistics regarding the re-use of PHI nodes.  */

#ifdef GATHER_STATISTICS
void
phinodes_print_statistics (void)
{
  fprintf (stderr, "PHI nodes allocated: %u\n", phi_nodes_created);
  fprintf (stderr, "PHI nodes reused: %u\n", phi_nodes_reused);
}
#endif

/* Allocate a PHI node with at least LEN arguments.  If the free list
   happens to contain a PHI node with LEN arguments or more, return
   that one.  */

static inline tree
allocate_phi_node (int len)
{
  tree phi;
  int bucket = NUM_BUCKETS - 2;
  int size = (sizeof (struct tree_phi_node)
	      + (len - 1) * sizeof (struct phi_arg_d));

  if (free_phinode_count)
    for (bucket = len - 2; bucket < NUM_BUCKETS - 2; bucket++)
      if (free_phinodes[bucket])
	break;

  /* If our free list has an element, then use it.  */
  if (bucket < NUM_BUCKETS - 2
      && PHI_ARG_CAPACITY (free_phinodes[bucket]) >= len)
    {
      free_phinode_count--;
      phi = free_phinodes[bucket];
      free_phinodes[bucket] = PHI_CHAIN (free_phinodes[bucket]);
#ifdef GATHER_STATISTICS
      phi_nodes_reused++;
#endif
    }
  else
    {
      phi = ggc_alloc (size);
#ifdef GATHER_STATISTICS
      phi_nodes_created++;
      tree_node_counts[(int) phi_kind]++;
      tree_node_sizes[(int) phi_kind] += size;
#endif
    }

  return phi;
}

/* Given LEN, the original number of requested PHI arguments, return
   a new, "ideal" length for the PHI node.  The "ideal" length rounds
   the total size of the PHI node up to the next power of two bytes.

   Rounding up will not result in wasting any memory since the size request
   will be rounded up by the GC system anyway.  [ Note this is not entirely
   true since the original length might have fit on one of the special
   GC pages. ]  By rounding up, we may avoid the need to reallocate the
   PHI node later if we increase the number of arguments for the PHI.  */

static int
ideal_phi_node_len (int len)
{
  size_t size, new_size;
  int log2, new_len;

  /* We do not support allocations of less than two PHI argument slots.  */
  if (len < 2)
    len = 2;

  /* Compute the number of bytes of the original request.  */
  size = sizeof (struct tree_phi_node) + (len - 1) * sizeof (struct phi_arg_d);

  /* Round it up to the next power of two.  */
  log2 = ceil_log2 (size);
  new_size = 1 << log2;

  /* Now compute and return the number of PHI argument slots given an
     ideal size allocation.  */
  new_len = len + (new_size - size) / sizeof (struct phi_arg_d);
  return new_len;
}


/* Return a PHI node with LEN argument slots for variable VAR.  */

static tree
make_phi_node (tree var, int len)
{
  tree phi;
  int capacity, i;

  capacity = ideal_phi_node_len (len);

  phi = allocate_phi_node (capacity);

  /* We need to clear the entire PHI node, including the argument
     portion, because we represent a "missing PHI argument" by placing
     NULL_TREE in PHI_ARG_DEF.  */
  memset (phi, 0, (sizeof (struct tree_phi_node) - sizeof (struct phi_arg_d)
		   + sizeof (struct phi_arg_d) * len));
  TREE_SET_CODE (phi, PHI_NODE);
  PHI_NUM_ARGS (phi) = len;
  PHI_ARG_CAPACITY (phi) = capacity;
  TREE_TYPE (phi) = TREE_TYPE (var);
  if (TREE_CODE (var) == SSA_NAME)
    SET_PHI_RESULT (phi, var);
  else
    SET_PHI_RESULT (phi, make_ssa_name (var, phi));

  for (i = 0; i < capacity; i++)
    {
      use_operand_p  imm;
      imm = &(PHI_ARG_IMM_USE_NODE (phi, i));
      imm->use = &(PHI_ARG_DEF_TREE (phi, i));
      imm->prev = NULL;
      imm->next = NULL;
      imm->stmt = phi;
    }
  return phi;
}

/* We no longer need PHI, release it so that it may be reused.  */

void
release_phi_node (tree phi)
{
  int bucket;
  int len = PHI_ARG_CAPACITY (phi);
  int x;

  for (x = 0; x < PHI_NUM_ARGS (phi); x++)
    {
      use_operand_p  imm;
      imm = &(PHI_ARG_IMM_USE_NODE (phi, x));
      delink_imm_use (imm);
    }

  bucket = len > NUM_BUCKETS - 1 ? NUM_BUCKETS - 1 : len;
  bucket -= 2;
  PHI_CHAIN (phi) = free_phinodes[bucket];
  free_phinodes[bucket] = phi;
  free_phinode_count++;
}

/* Resize an existing PHI node.  The only way is up.  Return the
   possibly relocated phi.  */

static void
resize_phi_node (tree *phi, int len)
{
  int old_size, i;
  tree new_phi;

  gcc_assert (len > PHI_ARG_CAPACITY (*phi));

  /* The garbage collector will not look at the PHI node beyond the
     first PHI_NUM_ARGS elements.  Therefore, all we have to copy is a
     portion of the PHI node currently in use.  */
  old_size = (sizeof (struct tree_phi_node)
	     + (PHI_NUM_ARGS (*phi) - 1) * sizeof (struct phi_arg_d));

  new_phi = allocate_phi_node (len);

  memcpy (new_phi, *phi, old_size);

  for (i = 0; i < PHI_NUM_ARGS (new_phi); i++)
    {
      use_operand_p imm, old_imm;
      imm = &(PHI_ARG_IMM_USE_NODE (new_phi, i));
      old_imm = &(PHI_ARG_IMM_USE_NODE (*phi, i));
      imm->use = &(PHI_ARG_DEF_TREE (new_phi, i));
      relink_imm_use_stmt (imm, old_imm, new_phi);
    }

  PHI_ARG_CAPACITY (new_phi) = len;

  for (i = PHI_NUM_ARGS (new_phi); i < len; i++)
    {
      use_operand_p imm;
      imm = &(PHI_ARG_IMM_USE_NODE (new_phi, i));
      imm->use = &(PHI_ARG_DEF_TREE (new_phi, i));
      imm->prev = NULL;
      imm->next = NULL;
      imm->stmt = new_phi;
    }


  *phi = new_phi;
}

/* Reserve PHI arguments for a new edge to basic block BB.  */

void
reserve_phi_args_for_new_edge (basic_block bb)
{
  tree *loc;
  int len = EDGE_COUNT (bb->preds);
  int cap = ideal_phi_node_len (len + 4);

  for (loc = &(bb->phi_nodes);
       *loc;
       loc = &PHI_CHAIN (*loc))
    {
      if (len > PHI_ARG_CAPACITY (*loc))
	{
	  tree old_phi = *loc;

	  resize_phi_node (loc, cap);

	  /* The result of the phi is defined by this phi node.  */
	  SSA_NAME_DEF_STMT (PHI_RESULT (*loc)) = *loc;

	  release_phi_node (old_phi);
	}

      /* We represent a "missing PHI argument" by placing NULL_TREE in
	 the corresponding slot.  If PHI arguments were added
	 immediately after an edge is created, this zeroing would not
	 be necessary, but unfortunately this is not the case.  For
	 example, the loop optimizer duplicates several basic blocks,
	 redirects edges, and then fixes up PHI arguments later in
	 batch.  */
      SET_PHI_ARG_DEF (*loc, len - 1, NULL_TREE);

      PHI_NUM_ARGS (*loc)++;
    }
}

/* Create a new PHI node for variable VAR at basic block BB.  */

tree
create_phi_node (tree var, basic_block bb)
{
  tree phi;

  phi = make_phi_node (var, EDGE_COUNT (bb->preds));

  /* Add the new PHI node to the list of PHI nodes for block BB.  */
  PHI_CHAIN (phi) = phi_nodes (bb);
  bb->phi_nodes = phi;

  /* Associate BB to the PHI node.  */
  set_bb_for_stmt (phi, bb);

  return phi;
}

/* Add a new argument to PHI node PHI.  DEF is the incoming reaching
   definition and E is the edge through which DEF reaches PHI.  The new
   argument is added at the end of the argument list.
   If PHI has reached its maximum capacity, add a few slots.  In this case,
   PHI points to the reallocated phi node when we return.  */

void
add_phi_arg (tree phi, tree def, edge e)
{
  basic_block bb = e->dest;

  gcc_assert (bb == bb_for_stmt (phi));

  /* We resize PHI nodes upon edge creation.  We should always have
     enough room at this point.  */
  gcc_assert (PHI_NUM_ARGS (phi) <= PHI_ARG_CAPACITY (phi));

  /* We resize PHI nodes upon edge creation.  We should always have
     enough room at this point.  */
  gcc_assert (e->dest_idx < (unsigned int) PHI_NUM_ARGS (phi));

  /* Copy propagation needs to know what object occur in abnormal
     PHI nodes.  This is a convenient place to record such information.  */
  if (e->flags & EDGE_ABNORMAL)
    {
      SSA_NAME_OCCURS_IN_ABNORMAL_PHI (def) = 1;
      SSA_NAME_OCCURS_IN_ABNORMAL_PHI (PHI_RESULT (phi)) = 1;
    }

  SET_PHI_ARG_DEF (phi, e->dest_idx, def);
}

/* Remove the Ith argument from PHI's argument list.  This routine
   implements removal by swapping the last alternative with the
   alternative we want to delete and then shrinking the vector, which
   is consistent with how we remove an edge from the edge vector.  */

static void
remove_phi_arg_num (tree phi, int i)
{
  int num_elem = PHI_NUM_ARGS (phi);

  gcc_assert (i < num_elem);


  /* Delink the item which is being removed.  */
  delink_imm_use (&(PHI_ARG_IMM_USE_NODE (phi, i)));

  /* If it is not the last element, move the last element
     to the element we want to delete, resetting all the links. */
  if (i != num_elem - 1)
    {
      use_operand_p old_p, new_p;
      old_p = &PHI_ARG_IMM_USE_NODE (phi, num_elem - 1);
      new_p = &PHI_ARG_IMM_USE_NODE (phi, i);
      /* Set use on new node, and link into last element's place.  */
      *(new_p->use) = *(old_p->use);
      relink_imm_use (new_p, old_p);
    }

  /* Shrink the vector and return.  Note that we do not have to clear
     PHI_ARG_DEF because the garbage collector will not look at those
     elements beyond the first PHI_NUM_ARGS elements of the array.  */
  PHI_NUM_ARGS (phi)--;
}

/* Remove all PHI arguments associated with edge E.  */

void
remove_phi_args (edge e)
{
  tree phi;

  for (phi = phi_nodes (e->dest); phi; phi = PHI_CHAIN (phi))
    remove_phi_arg_num (phi, e->dest_idx);
}

/* Remove PHI node PHI from basic block BB.  If PREV is non-NULL, it is
   used as the node immediately before PHI in the linked list.  */

void
remove_phi_node (tree phi, tree prev)
{
  tree *loc;

  if (prev)
    {
      loc = &PHI_CHAIN (prev);
    }
  else
    {
      for (loc = &(bb_for_stmt (phi)->phi_nodes);
	   *loc != phi;
	   loc = &PHI_CHAIN (*loc))
	;
    }

  /* Remove PHI from the chain.  */
  *loc = PHI_CHAIN (phi);

  /* If we are deleting the PHI node, then we should release the
     SSA_NAME node so that it can be reused.  */
  release_phi_node (phi);
  release_ssa_name (PHI_RESULT (phi));
}


/* Reverse the order of PHI nodes in the chain PHI.
   Return the new head of the chain (old last PHI node).  */

tree
phi_reverse (tree phi)
{
  tree prev = NULL_TREE, next;
  for (; phi; phi = next)
    {
      next = PHI_CHAIN (phi);
      PHI_CHAIN (phi) = prev;
      prev = phi;
    }
  return prev;
}

#include "gt-tree-phinodes.h"
