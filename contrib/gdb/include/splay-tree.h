/* A splay-tree datatype.  
   Copyright 1998, 1999, 2000, 2002 Free Software Foundation, Inc.
   Contributed by Mark Mitchell (mark@markmitchell.com).

This file is part of GCC.
   
GCC is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  */

/* For an easily readable description of splay-trees, see:

     Lewis, Harry R. and Denenberg, Larry.  Data Structures and Their
     Algorithms.  Harper-Collins, Inc.  1991.  

   The major feature of splay trees is that all basic tree operations
   are amortized O(log n) time for a tree with n nodes.  */

#ifndef _SPLAY_TREE_H
#define _SPLAY_TREE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "ansidecl.h"

#ifndef GTY
#define GTY(X)
#endif

/* Use typedefs for the key and data types to facilitate changing
   these types, if necessary.  These types should be sufficiently wide
   that any pointer or scalar can be cast to these types, and then
   cast back, without loss of precision.  */
typedef unsigned long int splay_tree_key;
typedef unsigned long int splay_tree_value;

/* Forward declaration for a node in the tree.  */
typedef struct splay_tree_node_s *splay_tree_node;

/* The type of a function which compares two splay-tree keys.  The
   function should return values as for qsort.  */
typedef int (*splay_tree_compare_fn) PARAMS((splay_tree_key, splay_tree_key));

/* The type of a function used to deallocate any resources associated
   with the key.  */
typedef void (*splay_tree_delete_key_fn) PARAMS((splay_tree_key));

/* The type of a function used to deallocate any resources associated
   with the value.  */
typedef void (*splay_tree_delete_value_fn) PARAMS((splay_tree_value));

/* The type of a function used to iterate over the tree.  */
typedef int (*splay_tree_foreach_fn) PARAMS((splay_tree_node, void*));

/* The type of a function used to allocate memory for tree root and
   node structures.  The first argument is the number of bytes needed;
   the second is a data pointer the splay tree functions pass through
   to the allocator.  This function must never return zero.  */
typedef PTR (*splay_tree_allocate_fn) PARAMS((int, void *));

/* The type of a function used to free memory allocated using the
   corresponding splay_tree_allocate_fn.  The first argument is the
   memory to be freed; the latter is a data pointer the splay tree
   functions pass through to the freer.  */
typedef void (*splay_tree_deallocate_fn) PARAMS((void *, void *));

/* The nodes in the splay tree.  */
struct splay_tree_node_s GTY(())
{
  /* The key.  */
  splay_tree_key GTY ((use_param1 (""))) key;

  /* The value.  */
  splay_tree_value GTY ((use_param2 (""))) value;

  /* The left and right children, respectively.  */
  splay_tree_node GTY ((use_params (""))) left;
  splay_tree_node GTY ((use_params (""))) right;
};

/* The splay tree itself.  */
struct splay_tree_s GTY(())
{
  /* The root of the tree.  */
  splay_tree_node GTY ((use_params (""))) root;

  /* The comparision function.  */
  splay_tree_compare_fn comp;

  /* The deallocate-key function.  NULL if no cleanup is necessary.  */
  splay_tree_delete_key_fn delete_key;

  /* The deallocate-value function.  NULL if no cleanup is necessary.  */
  splay_tree_delete_value_fn delete_value;

  /* Allocate/free functions, and a data pointer to pass to them.  */
  splay_tree_allocate_fn allocate;
  splay_tree_deallocate_fn deallocate;
  PTR GTY((skip (""))) allocate_data;

};
typedef struct splay_tree_s *splay_tree;

extern splay_tree splay_tree_new        PARAMS((splay_tree_compare_fn,
					        splay_tree_delete_key_fn,
					        splay_tree_delete_value_fn));
extern splay_tree splay_tree_new_with_allocator
                                        PARAMS((splay_tree_compare_fn,
					        splay_tree_delete_key_fn,
					        splay_tree_delete_value_fn,
                                                splay_tree_allocate_fn,
                                                splay_tree_deallocate_fn,
                                                void *));
extern void splay_tree_delete           PARAMS((splay_tree));
extern splay_tree_node splay_tree_insert          
		                        PARAMS((splay_tree,
					        splay_tree_key,
					        splay_tree_value));
extern void splay_tree_remove		PARAMS((splay_tree,
						splay_tree_key));
extern splay_tree_node splay_tree_lookup   
                                        PARAMS((splay_tree,
					        splay_tree_key));
extern splay_tree_node splay_tree_predecessor
                                        PARAMS((splay_tree,
						splay_tree_key));
extern splay_tree_node splay_tree_successor
                                        PARAMS((splay_tree,
						splay_tree_key));
extern splay_tree_node splay_tree_max
                                        PARAMS((splay_tree));
extern splay_tree_node splay_tree_min
                                        PARAMS((splay_tree));
extern int splay_tree_foreach           PARAMS((splay_tree,
					        splay_tree_foreach_fn,
					        void*));
extern int splay_tree_compare_ints      PARAMS((splay_tree_key,
						splay_tree_key));
extern int splay_tree_compare_pointers  PARAMS((splay_tree_key,
						splay_tree_key));
					       
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _SPLAY_TREE_H */
