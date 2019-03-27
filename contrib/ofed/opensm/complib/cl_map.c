/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	Implementation of quick map, a binary tree where the caller always
 *	provides all necessary storage.
 *
 */

/*****************************************************************************
*
* Map
*
* Map is an associative array.  By providing a key, the caller can retrieve
* an object from the map.  All objects in the map have an associated key,
* as specified by the caller when the object was inserted into the map.
* In addition to random access, the caller can traverse the map much like
* a linked list, either forwards from the first object or backwards from
* the last object.  The objects in the map are always traversed in
* order since the nodes are stored sorted.
*
* This implementation of Map uses a red black tree verified against
* Cormen-Leiserson-Rivest text, McGraw-Hill Edition, fourteenth
* printing, 1994.
*
*****************************************************************************/

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <complib/cl_qmap.h>
#include <complib/cl_map.h>
#include <complib/cl_fleximap.h>

/******************************************************************************
 IMPLEMENTATION OF QUICK MAP
******************************************************************************/

/*
 * Get the root.
 */
static inline cl_map_item_t *__cl_map_root(IN const cl_qmap_t * const p_map)
{
	CL_ASSERT(p_map);
	return (p_map->root.p_left);
}

/*
 * Returns whether a given item is on the left of its parent.
 */
static boolean_t __cl_map_is_left_child(IN const cl_map_item_t * const p_item)
{
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_up);
	CL_ASSERT(p_item->p_up != p_item);

	return (p_item->p_up->p_left == p_item);
}

/*
 * Retrieve the pointer to the parent's pointer to an item.
 */
static cl_map_item_t **__cl_map_get_parent_ptr_to_item(IN cl_map_item_t *
						       const p_item)
{
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_up);
	CL_ASSERT(p_item->p_up != p_item);

	if (__cl_map_is_left_child(p_item))
		return (&p_item->p_up->p_left);

	CL_ASSERT(p_item->p_up->p_right == p_item);
	return (&p_item->p_up->p_right);
}

/*
 * Rotate a node to the left.  This rotation affects the least number of links
 * between nodes and brings the level of C up by one while increasing the depth
 * of A one.  Note that the links to/from W, X, Y, and Z are not affected.
 *
 *	    R				      R
 *	    |				      |
 *	    A				      C
 *	  /   \			        /   \
 *	W       C			  A       Z
 *	       / \			 / \
 *	      B   Z			W   B
 *	     / \			   / \
 *	    X   Y			  X   Y
 */
static void __cl_map_rot_left(IN cl_qmap_t * const p_map,
			      IN cl_map_item_t * const p_item)
{
	cl_map_item_t **pp_root;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_right != &p_map->nil);

	pp_root = __cl_map_get_parent_ptr_to_item(p_item);

	/* Point R to C instead of A. */
	*pp_root = p_item->p_right;
	/* Set C's parent to R. */
	(*pp_root)->p_up = p_item->p_up;

	/* Set A's right to B */
	p_item->p_right = (*pp_root)->p_left;
	/*
	 * Set B's parent to A.  We trap for B being NIL since the
	 * caller may depend on NIL not changing.
	 */
	if ((*pp_root)->p_left != &p_map->nil)
		(*pp_root)->p_left->p_up = p_item;

	/* Set C's left to A. */
	(*pp_root)->p_left = p_item;
	/* Set A's parent to C. */
	p_item->p_up = *pp_root;
}

/*
 * Rotate a node to the right.  This rotation affects the least number of links
 * between nodes and brings the level of A up by one while increasing the depth
 * of C one.  Note that the links to/from W, X, Y, and Z are not affected.
 *
 *	        R				     R
 *	        |				     |
 *	        C				     A
 *	      /   \				   /   \
 *	    A       Z			 W       C
 *	   / \    				        / \
 *	  W   B   				       B   Z
 *	     / \				      / \
 *	    X   Y				     X   Y
 */
static void __cl_map_rot_right(IN cl_qmap_t * const p_map,
			       IN cl_map_item_t * const p_item)
{
	cl_map_item_t **pp_root;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_left != &p_map->nil);

	/* Point R to A instead of C. */
	pp_root = __cl_map_get_parent_ptr_to_item(p_item);
	(*pp_root) = p_item->p_left;
	/* Set A's parent to R. */
	(*pp_root)->p_up = p_item->p_up;

	/* Set C's left to B */
	p_item->p_left = (*pp_root)->p_right;
	/*
	 * Set B's parent to C.  We trap for B being NIL since the
	 * caller may depend on NIL not changing.
	 */
	if ((*pp_root)->p_right != &p_map->nil)
		(*pp_root)->p_right->p_up = p_item;

	/* Set A's right to C. */
	(*pp_root)->p_right = p_item;
	/* Set C's parent to A. */
	p_item->p_up = *pp_root;
}

void cl_qmap_init(IN cl_qmap_t * const p_map)
{
	CL_ASSERT(p_map);

	memset(p_map, 0, sizeof(cl_qmap_t));

	/* special setup for the root node */
	p_map->root.p_up = &p_map->root;
	p_map->root.p_left = &p_map->nil;
	p_map->root.p_right = &p_map->nil;
	p_map->root.color = CL_MAP_BLACK;

	/* Setup the node used as terminator for all leaves. */
	p_map->nil.p_up = &p_map->nil;
	p_map->nil.p_left = &p_map->nil;
	p_map->nil.p_right = &p_map->nil;
	p_map->nil.color = CL_MAP_BLACK;

	p_map->state = CL_INITIALIZED;

	cl_qmap_remove_all(p_map);
}

cl_map_item_t *cl_qmap_get(IN const cl_qmap_t * const p_map,
			   IN const uint64_t key)
{
	cl_map_item_t *p_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	p_item = __cl_map_root(p_map);

	while (p_item != &p_map->nil) {
		if (key == p_item->key)
			break;	/* just right */

		if (key < p_item->key)
			p_item = p_item->p_left;	/* too small */
		else
			p_item = p_item->p_right;	/* too big */
	}

	return (p_item);
}

cl_map_item_t *cl_qmap_get_next(IN const cl_qmap_t * const p_map,
				IN const uint64_t key)
{
	cl_map_item_t *p_item;
	cl_map_item_t *p_item_found;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	p_item = __cl_map_root(p_map);
	p_item_found = (cl_map_item_t *) & p_map->nil;

	while (p_item != &p_map->nil) {
		if (key < p_item->key) {
			p_item_found = p_item;
			p_item = p_item->p_left;
		} else {
			p_item = p_item->p_right;
		}
	}

	return (p_item_found);
}

void cl_qmap_apply_func(IN const cl_qmap_t * const p_map,
			IN cl_pfn_qmap_apply_t pfn_func,
			IN const void *const context)
{
	cl_map_item_t *p_map_item;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(pfn_func);

	p_map_item = cl_qmap_head(p_map);
	while (p_map_item != cl_qmap_end(p_map)) {
		pfn_func(p_map_item, (void *)context);
		p_map_item = cl_qmap_next(p_map_item);
	}
}

/*
 * Balance a tree starting at a given item back to the root.
 */
static void __cl_map_ins_bal(IN cl_qmap_t * const p_map,
			     IN cl_map_item_t * p_item)
{
	cl_map_item_t *p_grand_uncle;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item != &p_map->root);

	while (p_item->p_up->color == CL_MAP_RED) {
		if (__cl_map_is_left_child(p_item->p_up)) {
			p_grand_uncle = p_item->p_up->p_up->p_right;
			CL_ASSERT(p_grand_uncle);
			if (p_grand_uncle->color == CL_MAP_RED) {
				p_grand_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_BLACK;
				p_item->p_up->p_up->color = CL_MAP_RED;
				p_item = p_item->p_up->p_up;
				continue;
			}

			if (!__cl_map_is_left_child(p_item)) {
				p_item = p_item->p_up;
				__cl_map_rot_left(p_map, p_item);
			}
			p_item->p_up->color = CL_MAP_BLACK;
			p_item->p_up->p_up->color = CL_MAP_RED;
			__cl_map_rot_right(p_map, p_item->p_up->p_up);
		} else {
			p_grand_uncle = p_item->p_up->p_up->p_left;
			CL_ASSERT(p_grand_uncle);
			if (p_grand_uncle->color == CL_MAP_RED) {
				p_grand_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_BLACK;
				p_item->p_up->p_up->color = CL_MAP_RED;
				p_item = p_item->p_up->p_up;
				continue;
			}

			if (__cl_map_is_left_child(p_item)) {
				p_item = p_item->p_up;
				__cl_map_rot_right(p_map, p_item);
			}
			p_item->p_up->color = CL_MAP_BLACK;
			p_item->p_up->p_up->color = CL_MAP_RED;
			__cl_map_rot_left(p_map, p_item->p_up->p_up);
		}
	}
}

cl_map_item_t *cl_qmap_insert(IN cl_qmap_t * const p_map,
			      IN const uint64_t key,
			      IN cl_map_item_t * const p_item)
{
	cl_map_item_t *p_insert_at, *p_comp_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(p_item);
	CL_ASSERT(p_map->root.p_up == &p_map->root);
	CL_ASSERT(p_map->root.color != CL_MAP_RED);
	CL_ASSERT(p_map->nil.color != CL_MAP_RED);

	p_item->p_left = &p_map->nil;
	p_item->p_right = &p_map->nil;
	p_item->key = key;
	p_item->color = CL_MAP_RED;

	/* Find the insertion location. */
	p_insert_at = &p_map->root;
	p_comp_item = __cl_map_root(p_map);

	while (p_comp_item != &p_map->nil) {
		p_insert_at = p_comp_item;

		if (key == p_insert_at->key)
			return (p_insert_at);

		/* Traverse the tree until the correct insertion point is found. */
		if (key < p_insert_at->key)
			p_comp_item = p_insert_at->p_left;
		else
			p_comp_item = p_insert_at->p_right;
	}

	CL_ASSERT(p_insert_at != &p_map->nil);
	CL_ASSERT(p_comp_item == &p_map->nil);
	/* Insert the item. */
	if (p_insert_at == &p_map->root) {
		p_insert_at->p_left = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(&p_map->nil.pool_item.list_item,
				      &p_item->pool_item.list_item);
	} else if (key < p_insert_at->key) {
		p_insert_at->p_left = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(&p_insert_at->pool_item.list_item,
				      &p_item->pool_item.list_item);
	} else {
		p_insert_at->p_right = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(p_insert_at->pool_item.list_item.p_next,
				      &p_item->pool_item.list_item);
	}
	/* Increase the count. */
	p_map->count++;

	p_item->p_up = p_insert_at;

	/*
	 * We have added depth to this section of the tree.
	 * Rebalance as necessary as we retrace our path through the tree
	 * and update colors.
	 */
	__cl_map_ins_bal(p_map, p_item);

	__cl_map_root(p_map)->color = CL_MAP_BLACK;

	/*
	 * Note that it is not necessary to re-color the nil node black because all
	 * red color assignments are made via the p_up pointer, and nil is never
	 * set as the value of a p_up pointer.
	 */

#ifdef _DEBUG_
	/* Set the pointer to the map in the map item for consistency checking. */
	p_item->p_map = p_map;
#endif

	return (p_item);
}

static void __cl_map_del_bal(IN cl_qmap_t * const p_map,
			     IN cl_map_item_t * p_item)
{
	cl_map_item_t *p_uncle;

	while ((p_item->color != CL_MAP_RED) && (p_item->p_up != &p_map->root)) {
		if (__cl_map_is_left_child(p_item)) {
			p_uncle = p_item->p_up->p_right;

			if (p_uncle->color == CL_MAP_RED) {
				p_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_RED;
				__cl_map_rot_left(p_map, p_item->p_up);
				p_uncle = p_item->p_up->p_right;
			}

			if (p_uncle->p_right->color != CL_MAP_RED) {
				if (p_uncle->p_left->color != CL_MAP_RED) {
					p_uncle->color = CL_MAP_RED;
					p_item = p_item->p_up;
					continue;
				}

				p_uncle->p_left->color = CL_MAP_BLACK;
				p_uncle->color = CL_MAP_RED;
				__cl_map_rot_right(p_map, p_uncle);
				p_uncle = p_item->p_up->p_right;
			}
			p_uncle->color = p_item->p_up->color;
			p_item->p_up->color = CL_MAP_BLACK;
			p_uncle->p_right->color = CL_MAP_BLACK;
			__cl_map_rot_left(p_map, p_item->p_up);
			break;
		} else {
			p_uncle = p_item->p_up->p_left;

			if (p_uncle->color == CL_MAP_RED) {
				p_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_RED;
				__cl_map_rot_right(p_map, p_item->p_up);
				p_uncle = p_item->p_up->p_left;
			}

			if (p_uncle->p_left->color != CL_MAP_RED) {
				if (p_uncle->p_right->color != CL_MAP_RED) {
					p_uncle->color = CL_MAP_RED;
					p_item = p_item->p_up;
					continue;
				}

				p_uncle->p_right->color = CL_MAP_BLACK;
				p_uncle->color = CL_MAP_RED;
				__cl_map_rot_left(p_map, p_uncle);
				p_uncle = p_item->p_up->p_left;
			}
			p_uncle->color = p_item->p_up->color;
			p_item->p_up->color = CL_MAP_BLACK;
			p_uncle->p_left->color = CL_MAP_BLACK;
			__cl_map_rot_right(p_map, p_item->p_up);
			break;
		}
	}
	p_item->color = CL_MAP_BLACK;
}

void cl_qmap_remove_item(IN cl_qmap_t * const p_map,
			 IN cl_map_item_t * const p_item)
{
	cl_map_item_t *p_child, *p_del_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(p_item);

	if (p_item == cl_qmap_end(p_map))
		return;

	/* must be checked after comparing to cl_qmap_end, since
	   the end is not a valid item. */
	CL_ASSERT(p_item->p_map == p_map);

	if ((p_item->p_right == &p_map->nil) || (p_item->p_left == &p_map->nil)) {
		/* The item being removed has children on at most on side. */
		p_del_item = p_item;
	} else {
		/*
		 * The item being removed has children on both side.
		 * We select the item that will replace it.  After removing
		 * the substitute item and rebalancing, the tree will have the
		 * correct topology.  Exchanging the substitute for the item
		 * will finalize the removal.
		 */
		p_del_item = cl_qmap_next(p_item);
		CL_ASSERT(p_del_item != &p_map->nil);
	}

	/* Remove the item from the list. */
	__cl_primitive_remove(&p_item->pool_item.list_item);
	/* Decrement the item count. */
	p_map->count--;

	/* Get the pointer to the new root's child, if any. */
	if (p_del_item->p_left != &p_map->nil)
		p_child = p_del_item->p_left;
	else
		p_child = p_del_item->p_right;

	/*
	 * This assignment may modify the parent pointer of the nil node.
	 * This is inconsequential.
	 */
	p_child->p_up = p_del_item->p_up;
	(*__cl_map_get_parent_ptr_to_item(p_del_item)) = p_child;

	if (p_del_item->color != CL_MAP_RED)
		__cl_map_del_bal(p_map, p_child);

	/*
	 * Note that the splicing done below does not need to occur before
	 * the tree is balanced, since the actual topology changes are made by the
	 * preceding code.  The topology is preserved by the color assignment made
	 * below (reader should be reminded that p_del_item == p_item in some cases).
	 */
	if (p_del_item != p_item) {
		/*
		 * Finalize the removal of the specified item by exchanging it with
		 * the substitute which we removed above.
		 */
		p_del_item->p_up = p_item->p_up;
		p_del_item->p_left = p_item->p_left;
		p_del_item->p_right = p_item->p_right;
		(*__cl_map_get_parent_ptr_to_item(p_item)) = p_del_item;
		p_item->p_right->p_up = p_del_item;
		p_item->p_left->p_up = p_del_item;
		p_del_item->color = p_item->color;
	}

	CL_ASSERT(p_map->nil.color != CL_MAP_RED);

#ifdef _DEBUG_
	/* Clear the pointer to the map since the item has been removed. */
	p_item->p_map = NULL;
#endif
}

cl_map_item_t *cl_qmap_remove(IN cl_qmap_t * const p_map, IN const uint64_t key)
{
	cl_map_item_t *p_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	/* Seek the node with the specified key */
	p_item = cl_qmap_get(p_map, key);

	cl_qmap_remove_item(p_map, p_item);

	return (p_item);
}

void cl_qmap_merge(OUT cl_qmap_t * const p_dest_map,
		   IN OUT cl_qmap_t * const p_src_map)
{
	cl_map_item_t *p_item, *p_item2, *p_next;

	CL_ASSERT(p_dest_map);
	CL_ASSERT(p_src_map);

	p_item = cl_qmap_head(p_src_map);

	while (p_item != cl_qmap_end(p_src_map)) {
		p_next = cl_qmap_next(p_item);

		/* Remove the item from its current map. */
		cl_qmap_remove_item(p_src_map, p_item);
		/* Insert the item into the destination map. */
		p_item2 =
		    cl_qmap_insert(p_dest_map, cl_qmap_key(p_item), p_item);
		/* Check that the item was successfully inserted. */
		if (p_item2 != p_item) {
			/* Put the item in back in the source map. */
			p_item2 =
			    cl_qmap_insert(p_src_map, cl_qmap_key(p_item),
					   p_item);
			CL_ASSERT(p_item2 == p_item);
		}
		p_item = p_next;
	}
}

static void __cl_qmap_delta_move(IN OUT cl_qmap_t * const p_dest,
				 IN OUT cl_qmap_t * const p_src,
				 IN OUT cl_map_item_t ** const pp_item)
{
	cl_map_item_t __attribute__((__unused__)) *p_temp;
	cl_map_item_t *p_next;

	/*
	 * Get the next item so that we can ensure that pp_item points to
	 * a valid item upon return from the function.
	 */
	p_next = cl_qmap_next(*pp_item);
	/* Move the old item from its current map the the old map. */
	cl_qmap_remove_item(p_src, *pp_item);
	p_temp = cl_qmap_insert(p_dest, cl_qmap_key(*pp_item), *pp_item);
	/* We should never have duplicates. */
	CL_ASSERT(p_temp == *pp_item);
	/* Point pp_item to a valid item in the source map. */
	(*pp_item) = p_next;
}

void cl_qmap_delta(IN OUT cl_qmap_t * const p_map1,
		   IN OUT cl_qmap_t * const p_map2,
		   OUT cl_qmap_t * const p_new, OUT cl_qmap_t * const p_old)
{
	cl_map_item_t *p_item1, *p_item2;
	uint64_t key1, key2;

	CL_ASSERT(p_map1);
	CL_ASSERT(p_map2);
	CL_ASSERT(p_new);
	CL_ASSERT(p_old);
	CL_ASSERT(cl_is_qmap_empty(p_new));
	CL_ASSERT(cl_is_qmap_empty(p_old));

	p_item1 = cl_qmap_head(p_map1);
	p_item2 = cl_qmap_head(p_map2);

	while (p_item1 != cl_qmap_end(p_map1) && p_item2 != cl_qmap_end(p_map2)) {
		key1 = cl_qmap_key(p_item1);
		key2 = cl_qmap_key(p_item2);
		if (key1 < key2) {
			/* We found an old item. */
			__cl_qmap_delta_move(p_old, p_map1, &p_item1);
		} else if (key1 > key2) {
			/* We found a new item. */
			__cl_qmap_delta_move(p_new, p_map2, &p_item2);
		} else {
			/* Move both forward since they have the same key. */
			p_item1 = cl_qmap_next(p_item1);
			p_item2 = cl_qmap_next(p_item2);
		}
	}

	/* Process the remainder if the end of either source map was reached. */
	while (p_item2 != cl_qmap_end(p_map2))
		__cl_qmap_delta_move(p_new, p_map2, &p_item2);

	while (p_item1 != cl_qmap_end(p_map1))
		__cl_qmap_delta_move(p_old, p_map1, &p_item1);
}

/******************************************************************************
 IMPLEMENTATION OF MAP
******************************************************************************/

#define MAP_GROW_SIZE 32

void cl_map_construct(IN cl_map_t * const p_map)
{
	CL_ASSERT(p_map);

	cl_qpool_construct(&p_map->pool);
}

cl_status_t cl_map_init(IN cl_map_t * const p_map, IN const uint32_t min_items)
{
	uint32_t grow_size;

	CL_ASSERT(p_map);

	cl_qmap_init(&p_map->qmap);

	/*
	 * We will grow by min_items/8 items at a time, with a minimum of
	 * MAP_GROW_SIZE.
	 */
	grow_size = min_items >> 3;
	if (grow_size < MAP_GROW_SIZE)
		grow_size = MAP_GROW_SIZE;

	return (cl_qpool_init(&p_map->pool, min_items, 0, grow_size,
			      sizeof(cl_map_obj_t), NULL, NULL, NULL));
}

void cl_map_destroy(IN cl_map_t * const p_map)
{
	CL_ASSERT(p_map);

	cl_qpool_destroy(&p_map->pool);
}

void *cl_map_insert(IN cl_map_t * const p_map,
		    IN const uint64_t key, IN const void *const p_object)
{
	cl_map_obj_t *p_map_obj, *p_obj_at_key;

	CL_ASSERT(p_map);

	p_map_obj = (cl_map_obj_t *) cl_qpool_get(&p_map->pool);

	if (!p_map_obj)
		return (NULL);

	cl_qmap_set_obj(p_map_obj, p_object);

	p_obj_at_key =
	    (cl_map_obj_t *) cl_qmap_insert(&p_map->qmap, key,
					    &p_map_obj->item);

	/* Return the item to the pool if insertion failed. */
	if (p_obj_at_key != p_map_obj)
		cl_qpool_put(&p_map->pool, &p_map_obj->item.pool_item);

	return (cl_qmap_obj(p_obj_at_key));
}

void *cl_map_get(IN const cl_map_t * const p_map, IN const uint64_t key)
{
	cl_map_item_t *p_item;

	CL_ASSERT(p_map);

	p_item = cl_qmap_get(&p_map->qmap, key);

	if (p_item == cl_qmap_end(&p_map->qmap))
		return (NULL);

	return (cl_qmap_obj(PARENT_STRUCT(p_item, cl_map_obj_t, item)));
}

void *cl_map_get_next(IN const cl_map_t * const p_map, IN const uint64_t key)
{
	cl_map_item_t *p_item;

	CL_ASSERT(p_map);

	p_item = cl_qmap_get_next(&p_map->qmap, key);

	if (p_item == cl_qmap_end(&p_map->qmap))
		return (NULL);

	return (cl_qmap_obj(PARENT_STRUCT(p_item, cl_map_obj_t, item)));
}

void cl_map_remove_item(IN cl_map_t * const p_map,
			IN const cl_map_iterator_t itor)
{
	CL_ASSERT(itor->p_map == &p_map->qmap);

	if (itor == cl_map_end(p_map))
		return;

	cl_qmap_remove_item(&p_map->qmap, (cl_map_item_t *) itor);
	cl_qpool_put(&p_map->pool, &((cl_map_item_t *) itor)->pool_item);
}

void *cl_map_remove(IN cl_map_t * const p_map, IN const uint64_t key)
{
	cl_map_item_t *p_item;
	void *p_obj;

	CL_ASSERT(p_map);

	p_item = cl_qmap_remove(&p_map->qmap, key);

	if (p_item == cl_qmap_end(&p_map->qmap))
		return (NULL);

	p_obj = cl_qmap_obj((cl_map_obj_t *) p_item);
	cl_qpool_put(&p_map->pool, &p_item->pool_item);

	return (p_obj);
}

void cl_map_remove_all(IN cl_map_t * const p_map)
{
	cl_map_item_t *p_item;

	CL_ASSERT(p_map);

	/* Return all map items to the pool. */
	while (!cl_is_qmap_empty(&p_map->qmap)) {
		p_item = cl_qmap_head(&p_map->qmap);
		cl_qmap_remove_item(&p_map->qmap, p_item);
		cl_qpool_put(&p_map->pool, &p_item->pool_item);

		if (!cl_is_qmap_empty(&p_map->qmap)) {
			p_item = cl_qmap_tail(&p_map->qmap);
			cl_qmap_remove_item(&p_map->qmap, p_item);
			cl_qpool_put(&p_map->pool, &p_item->pool_item);
		}
	}
}

cl_status_t cl_map_merge(OUT cl_map_t * const p_dest_map,
			 IN OUT cl_map_t * const p_src_map)
{
	cl_status_t status = CL_SUCCESS;
	cl_map_iterator_t itor, next;
	uint64_t key;
	void *p_obj, *p_obj2;

	CL_ASSERT(p_dest_map);
	CL_ASSERT(p_src_map);

	itor = cl_map_head(p_src_map);
	while (itor != cl_map_end(p_src_map)) {
		next = cl_map_next(itor);

		p_obj = cl_map_obj(itor);
		key = cl_map_key(itor);

		cl_map_remove_item(p_src_map, itor);

		/* Insert the object into the destination map. */
		p_obj2 = cl_map_insert(p_dest_map, key, p_obj);
		/* Trap for failure. */
		if (p_obj != p_obj2) {
			if (!p_obj2)
				status = CL_INSUFFICIENT_MEMORY;
			/* Put the object back in the source map.  This must succeed. */
			p_obj2 = cl_map_insert(p_src_map, key, p_obj);
			CL_ASSERT(p_obj == p_obj2);
			/* If the failure was due to insufficient memory, return. */
			if (status != CL_SUCCESS)
				return (status);
		}
		itor = next;
	}

	return (CL_SUCCESS);
}

static void __cl_map_revert(IN OUT cl_map_t * const p_map1,
			    IN OUT cl_map_t * const p_map2,
			    IN OUT cl_map_t * const p_new,
			    IN OUT cl_map_t * const p_old)
{
	cl_status_t __attribute__((__unused__)) status;

	/* Restore the initial state. */
	status = cl_map_merge(p_map1, p_old);
	CL_ASSERT(status == CL_SUCCESS);
	status = cl_map_merge(p_map2, p_new);
	CL_ASSERT(status == CL_SUCCESS);
}

static cl_status_t __cl_map_delta_move(OUT cl_map_t * const p_dest,
				       IN OUT cl_map_t * const p_src,
				       IN OUT cl_map_iterator_t * const p_itor)
{
	cl_map_iterator_t next;
	void *p_obj, *p_obj2;
	uint64_t key;

	/* Get a valid iterator so we can continue the loop. */
	next = cl_map_next(*p_itor);
	/* Get the pointer to the object for insertion. */
	p_obj = cl_map_obj(*p_itor);
	/* Get the key for the object. */
	key = cl_map_key(*p_itor);
	/* Move the object. */
	cl_map_remove_item(p_src, *p_itor);
	p_obj2 = cl_map_insert(p_dest, key, p_obj);
	/* Check for failure. We should never get a duplicate. */
	if (!p_obj2) {
		p_obj2 = cl_map_insert(p_src, key, p_obj);
		CL_ASSERT(p_obj2 == p_obj);
		return (CL_INSUFFICIENT_MEMORY);
	}

	/* We should never get a duplicate */
	CL_ASSERT(p_obj == p_obj2);
	/* Update the iterator so that it is valid. */
	(*p_itor) = next;

	return (CL_SUCCESS);
}

cl_status_t cl_map_delta(IN OUT cl_map_t * const p_map1,
			 IN OUT cl_map_t * const p_map2,
			 OUT cl_map_t * const p_new, OUT cl_map_t * const p_old)
{
	cl_map_iterator_t itor1, itor2;
	uint64_t key1, key2;
	cl_status_t status;

	CL_ASSERT(p_map1);
	CL_ASSERT(p_map2);
	CL_ASSERT(p_new);
	CL_ASSERT(p_old);
	CL_ASSERT(cl_is_map_empty(p_new));
	CL_ASSERT(cl_is_map_empty(p_old));

	itor1 = cl_map_head(p_map1);
	itor2 = cl_map_head(p_map2);

	/*
	 * Note that the check is for the end, since duplicate items will remain
	 * in their respective maps.
	 */
	while (itor1 != cl_map_end(p_map1) && itor2 != cl_map_end(p_map2)) {
		key1 = cl_map_key(itor1);
		key2 = cl_map_key(itor2);
		if (key1 < key2) {
			status = __cl_map_delta_move(p_old, p_map1, &itor1);
			/* Check for failure. */
			if (status != CL_SUCCESS) {
				/* Restore the initial state. */
				__cl_map_revert(p_map1, p_map2, p_new, p_old);
				/* Return the failure status. */
				return (status);
			}
		} else if (key1 > key2) {
			status = __cl_map_delta_move(p_new, p_map2, &itor2);
			if (status != CL_SUCCESS) {
				/* Restore the initial state. */
				__cl_map_revert(p_map1, p_map2, p_new, p_old);
				/* Return the failure status. */
				return (status);
			}
		} else {
			/* Move both forward since they have the same key. */
			itor1 = cl_map_next(itor1);
			itor2 = cl_map_next(itor2);
		}
	}

	/* Process the remainder if either source map is empty. */
	while (itor2 != cl_map_end(p_map2)) {
		status = __cl_map_delta_move(p_new, p_map2, &itor2);
		if (status != CL_SUCCESS) {
			/* Restore the initial state. */
			__cl_map_revert(p_map1, p_map2, p_new, p_old);
			/* Return the failure status. */
			return (status);
		}
	}

	while (itor1 != cl_map_end(p_map1)) {
		status = __cl_map_delta_move(p_old, p_map1, &itor1);
		if (status != CL_SUCCESS) {
			/* Restore the initial state. */
			__cl_map_revert(p_map1, p_map2, p_new, p_old);
			/* Return the failure status. */
			return (status);
		}
	}

	return (CL_SUCCESS);
}

/******************************************************************************
 IMPLEMENTATION OF FLEXI MAP
******************************************************************************/

/*
 * Get the root.
 */
static inline cl_fmap_item_t *__cl_fmap_root(IN const cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	return (p_map->root.p_left);
}

/*
 * Returns whether a given item is on the left of its parent.
 */
static boolean_t __cl_fmap_is_left_child(IN const cl_fmap_item_t * const p_item)
{
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_up);
	CL_ASSERT(p_item->p_up != p_item);

	return (p_item->p_up->p_left == p_item);
}

/*
 * Retrieve the pointer to the parent's pointer to an item.
 */
static cl_fmap_item_t **__cl_fmap_get_parent_ptr_to_item(IN cl_fmap_item_t *
							 const p_item)
{
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_up);
	CL_ASSERT(p_item->p_up != p_item);

	if (__cl_fmap_is_left_child(p_item))
		return (&p_item->p_up->p_left);

	CL_ASSERT(p_item->p_up->p_right == p_item);
	return (&p_item->p_up->p_right);
}

/*
 * Rotate a node to the left.  This rotation affects the least number of links
 * between nodes and brings the level of C up by one while increasing the depth
 * of A one.  Note that the links to/from W, X, Y, and Z are not affected.
 *
 *	    R				      R
 *	    |				      |
 *	    A				      C
 *	  /   \			        /   \
 *	W       C			  A       Z
 *	       / \			 / \
 *	      B   Z			W   B
 *	     / \			   / \
 *	    X   Y			  X   Y
 */
static void __cl_fmap_rot_left(IN cl_fmap_t * const p_map,
			       IN cl_fmap_item_t * const p_item)
{
	cl_fmap_item_t **pp_root;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_right != &p_map->nil);

	pp_root = __cl_fmap_get_parent_ptr_to_item(p_item);

	/* Point R to C instead of A. */
	*pp_root = p_item->p_right;
	/* Set C's parent to R. */
	(*pp_root)->p_up = p_item->p_up;

	/* Set A's right to B */
	p_item->p_right = (*pp_root)->p_left;
	/*
	 * Set B's parent to A.  We trap for B being NIL since the
	 * caller may depend on NIL not changing.
	 */
	if ((*pp_root)->p_left != &p_map->nil)
		(*pp_root)->p_left->p_up = p_item;

	/* Set C's left to A. */
	(*pp_root)->p_left = p_item;
	/* Set A's parent to C. */
	p_item->p_up = *pp_root;
}

/*
 * Rotate a node to the right.  This rotation affects the least number of links
 * between nodes and brings the level of A up by one while increasing the depth
 * of C one.  Note that the links to/from W, X, Y, and Z are not affected.
 *
 *	        R				     R
 *	        |				     |
 *	        C				     A
 *	      /   \				   /   \
 *	    A       Z			 W       C
 *	   / \    				        / \
 *	  W   B   				       B   Z
 *	     / \				      / \
 *	    X   Y				     X   Y
 */
static void __cl_fmap_rot_right(IN cl_fmap_t * const p_map,
				IN cl_fmap_item_t * const p_item)
{
	cl_fmap_item_t **pp_root;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_left != &p_map->nil);

	/* Point R to A instead of C. */
	pp_root = __cl_fmap_get_parent_ptr_to_item(p_item);
	(*pp_root) = p_item->p_left;
	/* Set A's parent to R. */
	(*pp_root)->p_up = p_item->p_up;

	/* Set C's left to B */
	p_item->p_left = (*pp_root)->p_right;
	/*
	 * Set B's parent to C.  We trap for B being NIL since the
	 * caller may depend on NIL not changing.
	 */
	if ((*pp_root)->p_right != &p_map->nil)
		(*pp_root)->p_right->p_up = p_item;

	/* Set A's right to C. */
	(*pp_root)->p_right = p_item;
	/* Set C's parent to A. */
	p_item->p_up = *pp_root;
}

void cl_fmap_init(IN cl_fmap_t * const p_map, IN cl_pfn_fmap_cmp_t pfn_compare)
{
	CL_ASSERT(p_map);
	CL_ASSERT(pfn_compare);

	memset(p_map, 0, sizeof(cl_fmap_t));

	/* special setup for the root node */
	p_map->root.p_up = &p_map->root;
	p_map->root.p_left = &p_map->nil;
	p_map->root.p_right = &p_map->nil;
	p_map->root.color = CL_MAP_BLACK;

	/* Setup the node used as terminator for all leaves. */
	p_map->nil.p_up = &p_map->nil;
	p_map->nil.p_left = &p_map->nil;
	p_map->nil.p_right = &p_map->nil;
	p_map->nil.color = CL_MAP_BLACK;

	/* Store the compare function pointer. */
	p_map->pfn_compare = pfn_compare;

	p_map->state = CL_INITIALIZED;

	cl_fmap_remove_all(p_map);
}

cl_fmap_item_t *cl_fmap_match(IN const cl_fmap_t * const p_map,
			      IN const void *const p_key,
			      IN cl_pfn_fmap_cmp_t pfn_compare)
{
	cl_fmap_item_t *p_item;
	int cmp;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	p_item = __cl_fmap_root(p_map);

	while (p_item != &p_map->nil) {
		cmp = pfn_compare ? pfn_compare(p_key, p_item->p_key) :
			p_map->pfn_compare(p_key, p_item->p_key);

		if (!cmp)
			break;	/* just right */

		if (cmp < 0)
			p_item = p_item->p_left;	/* too small */
		else
			p_item = p_item->p_right;	/* too big */
	}

	return (p_item);
}

cl_fmap_item_t *cl_fmap_get(IN const cl_fmap_t * const p_map,
			    IN const void *const p_key)
{
	return cl_fmap_match(p_map, p_key, p_map->pfn_compare);
}

cl_fmap_item_t *cl_fmap_get_next(IN const cl_fmap_t * const p_map,
				 IN const void *const p_key)
{
	cl_fmap_item_t *p_item;
	cl_fmap_item_t *p_item_found;
	int cmp;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	p_item = __cl_fmap_root(p_map);
	p_item_found = (cl_fmap_item_t *) & p_map->nil;

	while (p_item != &p_map->nil) {
		cmp = p_map->pfn_compare(p_key, p_item->p_key);

		if (cmp < 0) {
			p_item_found = p_item;
			p_item = p_item->p_left;	/* too small */
		} else {
			p_item = p_item->p_right;	/* too big or match */
		}
	}

	return (p_item_found);
}

void cl_fmap_apply_func(IN const cl_fmap_t * const p_map,
			IN cl_pfn_fmap_apply_t pfn_func,
			IN const void *const context)
{
	cl_fmap_item_t *p_fmap_item;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(pfn_func);

	p_fmap_item = cl_fmap_head(p_map);
	while (p_fmap_item != cl_fmap_end(p_map)) {
		pfn_func(p_fmap_item, (void *)context);
		p_fmap_item = cl_fmap_next(p_fmap_item);
	}
}

/*
 * Balance a tree starting at a given item back to the root.
 */
static void __cl_fmap_ins_bal(IN cl_fmap_t * const p_map,
			      IN cl_fmap_item_t * p_item)
{
	cl_fmap_item_t *p_grand_uncle;

	CL_ASSERT(p_map);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item != &p_map->root);

	while (p_item->p_up->color == CL_MAP_RED) {
		if (__cl_fmap_is_left_child(p_item->p_up)) {
			p_grand_uncle = p_item->p_up->p_up->p_right;
			CL_ASSERT(p_grand_uncle);
			if (p_grand_uncle->color == CL_MAP_RED) {
				p_grand_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_BLACK;
				p_item->p_up->p_up->color = CL_MAP_RED;
				p_item = p_item->p_up->p_up;
				continue;
			}

			if (!__cl_fmap_is_left_child(p_item)) {
				p_item = p_item->p_up;
				__cl_fmap_rot_left(p_map, p_item);
			}
			p_item->p_up->color = CL_MAP_BLACK;
			p_item->p_up->p_up->color = CL_MAP_RED;
			__cl_fmap_rot_right(p_map, p_item->p_up->p_up);
		} else {
			p_grand_uncle = p_item->p_up->p_up->p_left;
			CL_ASSERT(p_grand_uncle);
			if (p_grand_uncle->color == CL_MAP_RED) {
				p_grand_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_BLACK;
				p_item->p_up->p_up->color = CL_MAP_RED;
				p_item = p_item->p_up->p_up;
				continue;
			}

			if (__cl_fmap_is_left_child(p_item)) {
				p_item = p_item->p_up;
				__cl_fmap_rot_right(p_map, p_item);
			}
			p_item->p_up->color = CL_MAP_BLACK;
			p_item->p_up->p_up->color = CL_MAP_RED;
			__cl_fmap_rot_left(p_map, p_item->p_up->p_up);
		}
	}
}

cl_fmap_item_t *cl_fmap_insert(IN cl_fmap_t * const p_map,
			       IN const void *const p_key,
			       IN cl_fmap_item_t * const p_item)
{
	cl_fmap_item_t *p_insert_at, *p_comp_item;
	int cmp = 0;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(p_item);
	CL_ASSERT(p_map->root.p_up == &p_map->root);
	CL_ASSERT(p_map->root.color != CL_MAP_RED);
	CL_ASSERT(p_map->nil.color != CL_MAP_RED);

	p_item->p_left = &p_map->nil;
	p_item->p_right = &p_map->nil;
	p_item->p_key = p_key;
	p_item->color = CL_MAP_RED;

	/* Find the insertion location. */
	p_insert_at = &p_map->root;
	p_comp_item = __cl_fmap_root(p_map);

	while (p_comp_item != &p_map->nil) {
		p_insert_at = p_comp_item;

		cmp = p_map->pfn_compare(p_key, p_insert_at->p_key);

		if (!cmp)
			return (p_insert_at);

		/* Traverse the tree until the correct insertion point is found. */
		if (cmp < 0)
			p_comp_item = p_insert_at->p_left;
		else
			p_comp_item = p_insert_at->p_right;
	}

	CL_ASSERT(p_insert_at != &p_map->nil);
	CL_ASSERT(p_comp_item == &p_map->nil);
	/* Insert the item. */
	if (p_insert_at == &p_map->root) {
		p_insert_at->p_left = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(&p_map->nil.pool_item.list_item,
				      &p_item->pool_item.list_item);
	} else if (cmp < 0) {
		p_insert_at->p_left = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(&p_insert_at->pool_item.list_item,
				      &p_item->pool_item.list_item);
	} else {
		p_insert_at->p_right = p_item;
		/*
		 * Primitive insert places the new item in front of
		 * the existing item.
		 */
		__cl_primitive_insert(p_insert_at->pool_item.list_item.p_next,
				      &p_item->pool_item.list_item);
	}
	/* Increase the count. */
	p_map->count++;

	p_item->p_up = p_insert_at;

	/*
	 * We have added depth to this section of the tree.
	 * Rebalance as necessary as we retrace our path through the tree
	 * and update colors.
	 */
	__cl_fmap_ins_bal(p_map, p_item);

	__cl_fmap_root(p_map)->color = CL_MAP_BLACK;

	/*
	 * Note that it is not necessary to re-color the nil node black because all
	 * red color assignments are made via the p_up pointer, and nil is never
	 * set as the value of a p_up pointer.
	 */

#ifdef _DEBUG_
	/* Set the pointer to the map in the map item for consistency checking. */
	p_item->p_map = p_map;
#endif

	return (p_item);
}

static void __cl_fmap_del_bal(IN cl_fmap_t * const p_map,
			      IN cl_fmap_item_t * p_item)
{
	cl_fmap_item_t *p_uncle;

	while ((p_item->color != CL_MAP_RED) && (p_item->p_up != &p_map->root)) {
		if (__cl_fmap_is_left_child(p_item)) {
			p_uncle = p_item->p_up->p_right;

			if (p_uncle->color == CL_MAP_RED) {
				p_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_RED;
				__cl_fmap_rot_left(p_map, p_item->p_up);
				p_uncle = p_item->p_up->p_right;
			}

			if (p_uncle->p_right->color != CL_MAP_RED) {
				if (p_uncle->p_left->color != CL_MAP_RED) {
					p_uncle->color = CL_MAP_RED;
					p_item = p_item->p_up;
					continue;
				}

				p_uncle->p_left->color = CL_MAP_BLACK;
				p_uncle->color = CL_MAP_RED;
				__cl_fmap_rot_right(p_map, p_uncle);
				p_uncle = p_item->p_up->p_right;
			}
			p_uncle->color = p_item->p_up->color;
			p_item->p_up->color = CL_MAP_BLACK;
			p_uncle->p_right->color = CL_MAP_BLACK;
			__cl_fmap_rot_left(p_map, p_item->p_up);
			break;
		} else {
			p_uncle = p_item->p_up->p_left;

			if (p_uncle->color == CL_MAP_RED) {
				p_uncle->color = CL_MAP_BLACK;
				p_item->p_up->color = CL_MAP_RED;
				__cl_fmap_rot_right(p_map, p_item->p_up);
				p_uncle = p_item->p_up->p_left;
			}

			if (p_uncle->p_left->color != CL_MAP_RED) {
				if (p_uncle->p_right->color != CL_MAP_RED) {
					p_uncle->color = CL_MAP_RED;
					p_item = p_item->p_up;
					continue;
				}

				p_uncle->p_right->color = CL_MAP_BLACK;
				p_uncle->color = CL_MAP_RED;
				__cl_fmap_rot_left(p_map, p_uncle);
				p_uncle = p_item->p_up->p_left;
			}
			p_uncle->color = p_item->p_up->color;
			p_item->p_up->color = CL_MAP_BLACK;
			p_uncle->p_left->color = CL_MAP_BLACK;
			__cl_fmap_rot_right(p_map, p_item->p_up);
			break;
		}
	}
	p_item->color = CL_MAP_BLACK;
}

void cl_fmap_remove_item(IN cl_fmap_t * const p_map,
			 IN cl_fmap_item_t * const p_item)
{
	cl_fmap_item_t *p_child, *p_del_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	CL_ASSERT(p_item);
	CL_ASSERT(p_item->p_map == p_map);

	if (p_item == cl_fmap_end(p_map))
		return;

	if ((p_item->p_right == &p_map->nil) || (p_item->p_left == &p_map->nil)) {
		/* The item being removed has children on at most on side. */
		p_del_item = p_item;
	} else {
		/*
		 * The item being removed has children on both side.
		 * We select the item that will replace it.  After removing
		 * the substitute item and rebalancing, the tree will have the
		 * correct topology.  Exchanging the substitute for the item
		 * will finalize the removal.
		 */
		p_del_item = cl_fmap_next(p_item);
		CL_ASSERT(p_del_item != &p_map->nil);
	}

	/* Remove the item from the list. */
	__cl_primitive_remove(&p_item->pool_item.list_item);
	/* Decrement the item count. */
	p_map->count--;

	/* Get the pointer to the new root's child, if any. */
	if (p_del_item->p_left != &p_map->nil)
		p_child = p_del_item->p_left;
	else
		p_child = p_del_item->p_right;

	/*
	 * This assignment may modify the parent pointer of the nil node.
	 * This is inconsequential.
	 */
	p_child->p_up = p_del_item->p_up;
	(*__cl_fmap_get_parent_ptr_to_item(p_del_item)) = p_child;

	if (p_del_item->color != CL_MAP_RED)
		__cl_fmap_del_bal(p_map, p_child);

	/*
	 * Note that the splicing done below does not need to occur before
	 * the tree is balanced, since the actual topology changes are made by the
	 * preceding code.  The topology is preserved by the color assignment made
	 * below (reader should be reminded that p_del_item == p_item in some cases).
	 */
	if (p_del_item != p_item) {
		/*
		 * Finalize the removal of the specified item by exchanging it with
		 * the substitute which we removed above.
		 */
		p_del_item->p_up = p_item->p_up;
		p_del_item->p_left = p_item->p_left;
		p_del_item->p_right = p_item->p_right;
		(*__cl_fmap_get_parent_ptr_to_item(p_item)) = p_del_item;
		p_item->p_right->p_up = p_del_item;
		p_item->p_left->p_up = p_del_item;
		p_del_item->color = p_item->color;
	}

	CL_ASSERT(p_map->nil.color != CL_MAP_RED);

#ifdef _DEBUG_
	/* Clear the pointer to the map since the item has been removed. */
	p_item->p_map = NULL;
#endif
}

cl_fmap_item_t *cl_fmap_remove(IN cl_fmap_t * const p_map,
			       IN const void *const p_key)
{
	cl_fmap_item_t *p_item;

	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	/* Seek the node with the specified key */
	p_item = cl_fmap_get(p_map, p_key);

	cl_fmap_remove_item(p_map, p_item);

	return (p_item);
}

void cl_fmap_merge(OUT cl_fmap_t * const p_dest_map,
		   IN OUT cl_fmap_t * const p_src_map)
{
	cl_fmap_item_t *p_item, *p_item2, *p_next;

	CL_ASSERT(p_dest_map);
	CL_ASSERT(p_src_map);

	p_item = cl_fmap_head(p_src_map);

	while (p_item != cl_fmap_end(p_src_map)) {
		p_next = cl_fmap_next(p_item);

		/* Remove the item from its current map. */
		cl_fmap_remove_item(p_src_map, p_item);
		/* Insert the item into the destination map. */
		p_item2 =
		    cl_fmap_insert(p_dest_map, cl_fmap_key(p_item), p_item);
		/* Check that the item was successfully inserted. */
		if (p_item2 != p_item) {
			/* Put the item in back in the source map. */
			p_item2 =
			    cl_fmap_insert(p_src_map, cl_fmap_key(p_item),
					   p_item);
			CL_ASSERT(p_item2 == p_item);
		}
		p_item = p_next;
	}
}

static void __cl_fmap_delta_move(IN OUT cl_fmap_t * const p_dest,
				 IN OUT cl_fmap_t * const p_src,
				 IN OUT cl_fmap_item_t ** const pp_item)
{
	cl_fmap_item_t __attribute__((__unused__)) *p_temp;
	cl_fmap_item_t *p_next;

	/*
	 * Get the next item so that we can ensure that pp_item points to
	 * a valid item upon return from the function.
	 */
	p_next = cl_fmap_next(*pp_item);
	/* Move the old item from its current map the the old map. */
	cl_fmap_remove_item(p_src, *pp_item);
	p_temp = cl_fmap_insert(p_dest, cl_fmap_key(*pp_item), *pp_item);
	/* We should never have duplicates. */
	CL_ASSERT(p_temp == *pp_item);
	/* Point pp_item to a valid item in the source map. */
	(*pp_item) = p_next;
}

void cl_fmap_delta(IN OUT cl_fmap_t * const p_map1,
		   IN OUT cl_fmap_t * const p_map2,
		   OUT cl_fmap_t * const p_new, OUT cl_fmap_t * const p_old)
{
	cl_fmap_item_t *p_item1, *p_item2;
	int cmp;

	CL_ASSERT(p_map1);
	CL_ASSERT(p_map2);
	CL_ASSERT(p_new);
	CL_ASSERT(p_old);
	CL_ASSERT(cl_is_fmap_empty(p_new));
	CL_ASSERT(cl_is_fmap_empty(p_old));

	p_item1 = cl_fmap_head(p_map1);
	p_item2 = cl_fmap_head(p_map2);

	while (p_item1 != cl_fmap_end(p_map1) && p_item2 != cl_fmap_end(p_map2)) {
		cmp = p_map1->pfn_compare(cl_fmap_key(p_item1),
					  cl_fmap_key(p_item2));
		if (cmp < 0) {
			/* We found an old item. */
			__cl_fmap_delta_move(p_old, p_map1, &p_item1);
		} else if (cmp > 0) {
			/* We found a new item. */
			__cl_fmap_delta_move(p_new, p_map2, &p_item2);
		} else {
			/* Move both forward since they have the same key. */
			p_item1 = cl_fmap_next(p_item1);
			p_item2 = cl_fmap_next(p_item2);
		}
	}

	/* Process the remainder if the end of either source map was reached. */
	while (p_item2 != cl_fmap_end(p_map2))
		__cl_fmap_delta_move(p_new, p_map2, &p_item2);

	while (p_item1 != cl_fmap_end(p_map1))
		__cl_fmap_delta_move(p_old, p_map1, &p_item1);
}
