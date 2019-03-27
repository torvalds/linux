/* Generic dominator tree walker
   Copyright (C) 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Diego Novillo <dnovillo@redhat.com>

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

typedef void *void_p;
DEF_VEC_P(void_p);
DEF_VEC_ALLOC_P(void_p,heap);

/* This is the main data structure for the dominator walker.  It provides
   the callback hooks as well as a convenient place to hang block local
   data and pass-global data.  */

struct dom_walk_data
{
  /* This is the direction of the dominator tree we want to walk.  i.e.,
     if it is set to CDI_DOMINATORS, then we walk the dominator tree,
     if it is set to CDI_POST_DOMINATORS, then we walk the post
     dominator tree.  */
  ENUM_BITFIELD (cdi_direction) dom_direction : 2;

  /* Nonzero if the statement walker should walk the statements from
     last to first within a basic block instead of first to last.

     Note this affects both statement walkers.  We haven't yet needed
     to use the second statement walker for anything, so it's hard to
     predict if we really need the ability to select their direction
     independently.  */
  BOOL_BITFIELD walk_stmts_backward : 1;

  /* Function to initialize block local data.

     Note that the dominator walker infrastructure may provide a new
     fresh, and zero'd block local data structure, or it may re-use an
     existing block local data structure.

     If the block local structure has items such as virtual arrays, then
     that allows your optimizer to re-use those arrays rather than
     creating new ones.  */
  void (*initialize_block_local_data) (struct dom_walk_data *,
				       basic_block, bool);

  /* Function to call before the statement walk occurring before the
     recursive walk of the dominator children. 

     This typically initializes a block local data and pushes that
     data onto BLOCK_DATA_STACK.  */
  void (*before_dom_children_before_stmts) (struct dom_walk_data *,
					    basic_block);

  /* Function to call to walk statements before the recursive walk
     of the dominator children.  */
  void (*before_dom_children_walk_stmts) (struct dom_walk_data *,
					  basic_block, block_stmt_iterator);

  /* Function to call after the statement walk occurring before the
     recursive walk of the dominator children.  */
  void (*before_dom_children_after_stmts) (struct dom_walk_data *,
					   basic_block);

  /* Function to call before the statement walk occurring after the
     recursive walk of the dominator children.  */
  void (*after_dom_children_before_stmts) (struct dom_walk_data *,
					   basic_block);

  /* Function to call to walk statements after the recursive walk
     of the dominator children.  */
  void (*after_dom_children_walk_stmts) (struct dom_walk_data *,
					 basic_block, block_stmt_iterator);

  /* Function to call after the statement walk occurring after the
     recursive walk of the dominator children. 

     This typically finalizes any block local data and pops
     that data from BLOCK_DATA_STACK.  */
  void (*after_dom_children_after_stmts) (struct dom_walk_data *,
					  basic_block);

  /* Global data for a walk through the dominator tree.  */
  void *global_data;

  /* Stack of any data we need to keep on a per-block basis.

     If you have no local data, then BLOCK_DATA_STACK will be NULL.  */
  VEC(void_p,heap) *block_data_stack;

  /* Size of the block local data.   If this is zero, then it is assumed
     you have no local data and thus no BLOCK_DATA_STACK as well.  */
  size_t block_local_data_size;

  /* From here below are private data.  Please do not use this
     information/data outside domwalk.c.  */

  /* Stack of available block local structures.  */
  VEC(void_p,heap) *free_block_data;

  /* Interesting blocks to process.  If this field is not NULL, this
     set is used to determine which blocks to walk.  If we encounter
     block I in the dominator traversal, but block I is not present in
     INTERESTING_BLOCKS, then none of the callback functions are
     invoked on it.  This is useful when a particular traversal wants
     to filter out non-interesting blocks from the dominator tree.  */
  sbitmap interesting_blocks;
};

void walk_dominator_tree (struct dom_walk_data *, basic_block);
void init_walk_dominator_tree (struct dom_walk_data *);
void fini_walk_dominator_tree (struct dom_walk_data *);
