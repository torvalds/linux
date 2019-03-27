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
 *	Declaration of flexi map, a binary tree where the caller always provides
 *	all necessary storage.
 */

#ifndef _CL_FLEXIMAP_H_
#define _CL_FLEXIMAP_H_

#include <complib/cl_qmap.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Flexi Map
* NAME
*	Flexi Map
*
* DESCRIPTION
*	Flexi map implements a binary tree that stores user provided cl_fmap_item_t
*	structures.  Each item stored in a flexi map has a unique user defined
*	key (duplicates are not allowed).  Flexi map provides the ability to
*	efficiently search for an item given a key.  Flexi map allows user
*	defined keys of any size.  Storage for keys and a comparison function
*	are provided by users to allow flexi map to store items with arbitrary
*	key values.
*
*	Flexi map does not allocate any memory, and can therefore not fail
*	any operations due to insufficient memory.  Flexi map can thus be useful
*	in minimizing the error paths in code.
*
*	Flexi map is not thread safe, and users must provide serialization when
*	adding and removing items from the map.
*
*	The flexi map functions operate on a cl_fmap_t structure which should
*	be treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_fmap_t, cl_fmap_item_t
*
*	Callbacks:
*		cl_pfn_fmap_apply_t
*
*	Item Manipulation:
*		cl_fmap_key
*
*	Initialization:
*		cl_fmap_init
*
*	Iteration:
*		cl_fmap_end, cl_fmap_head, cl_fmap_tail, cl_fmap_next, cl_fmap_prev
*
*	Manipulation:
*		cl_fmap_insert, cl_fmap_get, cl_fmap_remove_item, cl_fmap_remove,
*		cl_fmap_remove_all, cl_fmap_merge, cl_fmap_delta, cl_fmap_get_next
*
*	Search:
*		cl_fmap_apply_func
*
*	Attributes:
*		cl_fmap_count, cl_is_fmap_empty,
*********/
/****s* Component Library: Flexi Map/cl_fmap_item_t
* NAME
*	cl_fmap_item_t
*
* DESCRIPTION
*	The cl_fmap_item_t structure is used by maps to store objects.
*
*	The cl_fmap_item_t structure should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_fmap_item {
	/* Must be first to allow casting. */
	cl_pool_item_t pool_item;
	struct _cl_fmap_item *p_left;
	struct _cl_fmap_item *p_right;
	struct _cl_fmap_item *p_up;
	cl_map_color_t color;
	const void *p_key;
#ifdef _DEBUG_
	struct _cl_fmap *p_map;
#endif
} cl_fmap_item_t;
/*
* FIELDS
*	pool_item
*		Used to store the item in a doubly linked list, allowing more
*		efficient map traversal.
*
*	p_left
*		Pointer to the map item that is a child to the left of the node.
*
*	p_right
*		Pointer to the map item that is a child to the right of the node.
*
*	p_up
*		Pointer to the map item that is the parent of the node.
*
*	p_nil
*		Pointer to the map's NIL item, used as a terminator for leaves.
*		The NIL sentinel is in the cl_fmap_t structure.
*
*	color
*		Indicates whether a node is red or black in the map.
*
*	p_key
*		Pointer to the value that uniquely represents a node in a map.  This
*		pointer is set by calling cl_fmap_insert and can be retrieved by
*		calling cl_fmap_key.
*
* NOTES
*	None of the fields of this structure should be manipulated by users, as
*	they are crititcal to the proper operation of the map in which they
*	are stored.
*
*	To allow storing items in either a quick list, a quick pool, or a flexi
*	map, the map implementation guarantees that the map item can be safely
*	cast to a pool item used for storing an object in a quick pool, or cast
*	to a list item used for storing an object in a quick list.  This removes
*	the need to embed a flexi map item, a list item, and a pool item in
*	objects that need to be stored in a quick list, a quick pool, and a
*	flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_insert, cl_fmap_key, cl_pool_item_t, cl_list_item_t
*********/

/****d* Component Library: Flexi Map/cl_pfn_fmap_cmp_t
* NAME
*	cl_pfn_fmap_cmp_t
*
* DESCRIPTION
*	The cl_pfn_fmap_cmp_t function type defines the prototype for functions
*	used to compare item keys in a flexi map.
*
* SYNOPSIS
*/
typedef int
    (*cl_pfn_fmap_cmp_t) (IN const void *const p_key1,
			  IN const void *const p_key2);
/*
* PARAMETERS
*	p_key1
*		[in] Pointer to the first of two keys to compare.
*
*	p_key2
*		[in] Pointer to the second of two keys to compare.
*
* RETURN VALUE
*	Returns 0 if the keys match.
*	Returns less than 0 if *p_key1 is less than *p_key2.
*	Returns greater than 0 if *p_key1 is greater than *p_key2.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_fmap_init function.
*
* SEE ALSO
*	Flexi Map, cl_fmap_init
*********/

/****s* Component Library: Flexi Map/cl_fmap_t
* NAME
*	cl_fmap_t
*
* DESCRIPTION
*	Flexi map structure.
*
*	The cl_fmap_t structure should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_fmap {
	cl_fmap_item_t root;
	cl_fmap_item_t nil;
	cl_state_t state;
	size_t count;
	cl_pfn_fmap_cmp_t pfn_compare;
} cl_fmap_t;
/*
* PARAMETERS
*	root
*		Map item that serves as root of the map.  The root is set up to
*		always have itself as parent.  The left pointer is set to point
*		to the item at the root.
*
*	nil
*		Map item that serves as terminator for all leaves, as well as
*		providing the list item used as quick list for storing map items
*		in a list for faster traversal.
*
*	state
*		State of the map, used to verify that operations are permitted.
*
*	count
*		Number of items in the map.
*
*	pfn_compare
*		Pointer to a compare function to invoke to compare the keys of
*		items in the map.
*
* SEE ALSO
*	Flexi Map, cl_pfn_fmap_cmp_t
*********/

/****d* Component Library: Flexi Map/cl_pfn_fmap_apply_t
* NAME
*	cl_pfn_fmap_apply_t
*
* DESCRIPTION
*	The cl_pfn_fmap_apply_t function type defines the prototype for
*	functions used to iterate items in a flexi map.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_fmap_apply_t) (IN cl_fmap_item_t * const p_map_item,
			 IN void *context);
/*
* PARAMETERS
*	p_map_item
*		[in] Pointer to a cl_fmap_item_t structure.
*
*	context
*		[in] Value passed to the callback function.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_fmap_apply_func
*	function.
*
* SEE ALSO
*	Flexi Map, cl_fmap_apply_func
*********/

/****f* Component Library: Flexi Map/cl_fmap_count
* NAME
*	cl_fmap_count
*
* DESCRIPTION
*	The cl_fmap_count function returns the number of items stored
*	in a flexi map.
*
* SYNOPSIS
*/
static inline size_t cl_fmap_count(IN const cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	return (p_map->count);
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure whose item count to return.
*
* RETURN VALUE
*	Returns the number of items stored in the map.
*
* SEE ALSO
*	Flexi Map, cl_is_fmap_empty
*********/

/****f* Component Library: Flexi Map/cl_is_fmap_empty
* NAME
*	cl_is_fmap_empty
*
* DESCRIPTION
*	The cl_is_fmap_empty function returns whether a flexi map is empty.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_fmap_empty(IN const cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	return (p_map->count == 0);
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure to test for emptiness.
*
* RETURN VALUES
*	TRUE if the flexi map is empty.
*
*	FALSE otherwise.
*
* SEE ALSO
*	Flexi Map, cl_fmap_count, cl_fmap_remove_all
*********/

/****f* Component Library: Flexi Map/cl_fmap_key
* NAME
*	cl_fmap_key
*
* DESCRIPTION
*	The cl_fmap_key function retrieves the key value of a map item.
*
* SYNOPSIS
*/
static inline const void *cl_fmap_key(IN const cl_fmap_item_t * const p_item)
{
	CL_ASSERT(p_item);
	return (p_item->p_key);
}

/*
* PARAMETERS
*	p_item
*		[in] Pointer to a map item whose key value to return.
*
* RETURN VALUE
*	Returns the a pointer to the key value for the specified map item.
*	The key value should not be modified to insure proper flexi map operation.
*
* NOTES
*	The key value is set in a call to cl_fmap_insert.
*
* SEE ALSO
*	Flexi Map, cl_fmap_insert
*********/

/****f* Component Library: Flexi Map/cl_fmap_init
* NAME
*	cl_fmap_init
*
* DESCRIPTION
*	The cl_fmap_init function initialized a flexi map for use.
*
* SYNOPSIS
*/
void cl_fmap_init(IN cl_fmap_t * const p_map, IN cl_pfn_fmap_cmp_t pfn_compare);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure to initialize.
*
*	pfn_compare
*		[in] Pointer to the compare function used to compare keys.
*		See the cl_pfn_fmap_cmp_t function type declaration for details
*		about the callback function.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	Allows calling flexi map manipulation functions.
*
* SEE ALSO
*	Flexi Map, cl_fmap_insert, cl_fmap_remove
*********/

/****f* Component Library: Flexi Map/cl_fmap_end
* NAME
*	cl_fmap_end
*
* DESCRIPTION
*	The cl_fmap_end function returns the end of a flexi map.
*
* SYNOPSIS
*/
static inline const cl_fmap_item_t *cl_fmap_end(IN const cl_fmap_t *
						const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	/* Nil is the end of the map. */
	return (&p_map->nil);
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure whose end to return.
*
* RETURN VALUE
*	Pointer to the end of the map.
*
* NOTES
*	cl_fmap_end is useful for determining the validity of map items returned
*	by cl_fmap_head, cl_fmap_tail, cl_fmap_next, or cl_fmap_prev.  If the
*	map item pointer returned by any of these functions compares to the end,
*	the end of the map was encoutered.
*	When using cl_fmap_head or cl_fmap_tail, this condition indicates that
*	the map is empty.
*
* SEE ALSO
*	Flexi Map, cl_fmap_head, cl_fmap_tail, cl_fmap_next, cl_fmap_prev
*********/

/****f* Component Library: Flexi Map/cl_fmap_head
* NAME
*	cl_fmap_head
*
* DESCRIPTION
*	The cl_fmap_head function returns the map item with the lowest key
*	value stored in a flexi map.
*
* SYNOPSIS
*/
static inline cl_fmap_item_t *cl_fmap_head(IN const cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	return ((cl_fmap_item_t *) p_map->nil.pool_item.list_item.p_next);
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure whose item with the lowest
*		key is returned.
*
* RETURN VALUES
*	Pointer to the map item with the lowest key in the flexi map.
*
*	Pointer to the map end if the flexi map was empty.
*
* NOTES
*	cl_fmap_head does not remove the item from the map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_tail, cl_fmap_next, cl_fmap_prev, cl_fmap_end,
*	cl_fmap_item_t
*********/

/****f* Component Library: Flexi Map/cl_fmap_tail
* NAME
*	cl_fmap_tail
*
* DESCRIPTION
*	The cl_fmap_tail function returns the map item with the highest key
*	value stored in a flexi map.
*
* SYNOPSIS
*/
static inline cl_fmap_item_t *cl_fmap_tail(IN const cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);
	return ((cl_fmap_item_t *) p_map->nil.pool_item.list_item.p_prev);
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure whose item with the highest key
*		is returned.
*
* RETURN VALUES
*	Pointer to the map item with the highest key in the flexi map.
*
*	Pointer to the map end if the flexi map was empty.
*
* NOTES
*	cl_fmap_end does not remove the item from the map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_head, cl_fmap_next, cl_fmap_prev, cl_fmap_end,
*	cl_fmap_item_t
*********/

/****f* Component Library: Flexi Map/cl_fmap_next
* NAME
*	cl_fmap_next
*
* DESCRIPTION
*	The cl_fmap_next function returns the map item with the next higher
*	key value than a specified map item.
*
* SYNOPSIS
*/
static inline cl_fmap_item_t *cl_fmap_next(IN const cl_fmap_item_t *
					   const p_item)
{
	CL_ASSERT(p_item);
	return ((cl_fmap_item_t *) p_item->pool_item.list_item.p_next);
}

/*
* PARAMETERS
*	p_item
*		[in] Pointer to a map item whose successor to return.
*
* RETURN VALUES
*	Pointer to the map item with the next higher key value in a flexi map.
*
*	Pointer to the map end if the specified item was the last item in
*	the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_head, cl_fmap_tail, cl_fmap_prev, cl_fmap_end,
*	cl_fmap_item_t
*********/

/****f* Component Library: Flexi Map/cl_fmap_prev
* NAME
*	cl_fmap_prev
*
* DESCRIPTION
*	The cl_fmap_prev function returns the map item with the next lower
*	key value than a precified map item.
*
* SYNOPSIS
*/
static inline cl_fmap_item_t *cl_fmap_prev(IN const cl_fmap_item_t *
					   const p_item)
{
	CL_ASSERT(p_item);
	return ((cl_fmap_item_t *) p_item->pool_item.list_item.p_prev);
}

/*
* PARAMETERS
*	p_item
*		[in] Pointer to a map item whose predecessor to return.
*
* RETURN VALUES
*	Pointer to the map item with the next lower key value in a flexi map.
*
*	Pointer to the map end if the specifid item was the first item in
*	the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_head, cl_fmap_tail, cl_fmap_next, cl_fmap_end,
*	cl_fmap_item_t
*********/

/****f* Component Library: Flexi Map/cl_fmap_insert
* NAME
*	cl_fmap_insert
*
* DESCRIPTION
*	The cl_fmap_insert function inserts a map item into a flexi map.
*
* SYNOPSIS
*/
cl_fmap_item_t *cl_fmap_insert(IN cl_fmap_t * const p_map,
			       IN const void *const p_key,
			       IN cl_fmap_item_t * const p_item);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure into which to add the item.
*
*	p_key
*		[in] Pointer to the key value to assign to the item.  Storage
*		for the key must be persistant, as only the pointer is stored.
*		Users are responsible for maintaining the validity of key
*		pointers while they are in use.
*
*	p_item
*		[in] Pointer to a cl_fmap_item_t stucture to insert into the flexi map.
*
* RETURN VALUE
*	Pointer to the item in the map with the specified key.  If insertion
*	was successful, this is the pointer to the item.  If an item with the
*	specified key already exists in the map, the pointer to that item is
*	returned.
*
* NOTES
*	Insertion operations may cause the flexi map to rebalance.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_item_t
*********/

/****f* Component Library: Flexi Map/cl_fmap_match
* NAME
*	cl_fmap_match
*
* DESCRIPTION
*	The cl_fmap_match function returns the map item matching a key.
*
* SYNOPSIS
*/
cl_fmap_item_t *cl_fmap_match(IN const cl_fmap_t * const p_map,
			      IN const void *const p_key,
			      IN cl_pfn_fmap_cmp_t pfn_compare);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure from which to retrieve the
*		item with the specified key.
*
*	p_key
*		[in] Pointer to a key value used to search for the desired map item.
*
*	pfn_compare
*		[in] Pointer to a compare function to invoke to compare the
*		keys of items in the map. Passing NULL here makes such call
*		to be equivalent to using cl_fmap_get().
*
* RETURN VALUES
*	Pointer to the map item matching the desired key value.
*
*	Pointer to the map end if there was no item matching the desired key
*	value stored in the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_get
*********/

/****f* Component Library: Flexi Map/cl_fmap_get
* NAME
*	cl_fmap_get
*
* DESCRIPTION
*	The cl_fmap_get function returns the map item associated with a key.
*
* SYNOPSIS
*/
cl_fmap_item_t *cl_fmap_get(IN const cl_fmap_t * const p_map,
			    IN const void *const p_key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure from which to retrieve the
*		item with the specified key.
*
*	p_key
*		[in] Pointer to a key value used to search for the desired map item.
*
* RETURN VALUES
*	Pointer to the map item with the desired key value.
*
*	Pointer to the map end if there was no item with the desired key value
*	stored in the flexi map.
*
* NOTES
*	cl_fmap_get does not remove the item from the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_get_next
*********/

/****f* Component Library: Flexi Map/cl_fmap_get_next
* NAME
*	cl_fmap_get_next
*
* DESCRIPTION
*	The cl_fmap_get_next function returns the first map item associated with
*	a key > the key specified.
*
* SYNOPSIS
*/
cl_fmap_item_t *cl_fmap_get_next(IN const cl_fmap_t * const p_map,
				 IN const void *const p_key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure from which to retrieve the
*		item with the specified key.
*
*	p_key
*		[in] Pointer to a key value used to search for the desired map item.
*
* RETURN VALUES
*	Pointer to the first map item with a key > the  desired key value.
*
*	Pointer to the map end if there was no item with a key > the desired key
*	value stored in the flexi map.
*
* NOTES
*	cl_fmap_get_next does not remove the item from the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_get
*********/

/****f* Component Library: Flexi Map/cl_fmap_remove_item
* NAME
*	cl_fmap_remove_item
*
* DESCRIPTION
*	The cl_fmap_remove_item function removes the specified map item
*	from a flexi map.
*
* SYNOPSIS
*/
void
cl_fmap_remove_item(IN cl_fmap_t * const p_map,
		    IN cl_fmap_item_t * const p_item);
/*
* PARAMETERS
*	p_item
*		[in] Pointer to a map item to remove from its flexi map.
*
* RETURN VALUES
*	This function does not return a value.
*
*	In a debug build, cl_fmap_remove_item asserts that the item being
*	removed is in the specified map.
*
* NOTES
*	Removes the map item pointed to by p_item from its flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_remove_all, cl_fmap_insert
*********/

/****f* Component Library: Flexi Map/cl_fmap_remove
* NAME
*	cl_fmap_remove
*
* DESCRIPTION
*	The cl_fmap_remove function removes the map item with the specified key
*	from a flexi map.
*
* SYNOPSIS
*/
cl_fmap_item_t *cl_fmap_remove(IN cl_fmap_t * const p_map,
			       IN const void *const p_key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure from which to remove the
*		item with the specified key.
*
*	p_key
*		[in] Pointer to the key value used to search for the map item
*		to remove.
*
* RETURN VALUES
*	Pointer to the removed map item if it was found.
*
*	Pointer to the map end if no item with the specified key exists in the
*	flexi map.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove_item, cl_fmap_remove_all, cl_fmap_insert
*********/

/****f* Component Library: Flexi Map/cl_fmap_remove_all
* NAME
*	cl_fmap_remove_all
*
* DESCRIPTION
*	The cl_fmap_remove_all function removes all items in a flexi map,
*	leaving it empty.
*
* SYNOPSIS
*/
static inline void cl_fmap_remove_all(IN cl_fmap_t * const p_map)
{
	CL_ASSERT(p_map);
	CL_ASSERT(p_map->state == CL_INITIALIZED);

	p_map->root.p_left = &p_map->nil;
	p_map->nil.pool_item.list_item.p_next = &p_map->nil.pool_item.list_item;
	p_map->nil.pool_item.list_item.p_prev = &p_map->nil.pool_item.list_item;
	p_map->count = 0;
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure to empty.
*
* RETURN VALUES
*	This function does not return a value.
*
* SEE ALSO
*	Flexi Map, cl_fmap_remove, cl_fmap_remove_item
*********/

/****f* Component Library: Flexi Map/cl_fmap_merge
* NAME
*	cl_fmap_merge
*
* DESCRIPTION
*	The cl_fmap_merge function moves all items from one map to another,
*	excluding duplicates.
*
* SYNOPSIS
*/
void
cl_fmap_merge(OUT cl_fmap_t * const p_dest_map,
	      IN OUT cl_fmap_t * const p_src_map);
/*
* PARAMETERS
*	p_dest_map
*		[out] Pointer to a cl_fmap_t structure to which items should be added.
*
*	p_src_map
*		[in/out] Pointer to a cl_fmap_t structure whose items to add
*		to p_dest_map.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	Items are evaluated based on their keys only.
*
*	Upon return from cl_fmap_merge, the flexi map referenced by p_src_map
*	contains all duplicate items.
*
* SEE ALSO
*	Flexi Map, cl_fmap_delta
*********/

/****f* Component Library: Flexi Map/cl_fmap_delta
* NAME
*	cl_fmap_delta
*
* DESCRIPTION
*	The cl_fmap_delta function computes the differences between two maps.
*
* SYNOPSIS
*/
void
cl_fmap_delta(IN OUT cl_fmap_t * const p_map1,
	      IN OUT cl_fmap_t * const p_map2,
	      OUT cl_fmap_t * const p_new, OUT cl_fmap_t * const p_old);
/*
* PARAMETERS
*	p_map1
*		[in/out] Pointer to the first of two cl_fmap_t structures whose
*		differences to compute.
*
*	p_map2
*		[in/out] Pointer to the second of two cl_fmap_t structures whose
*		differences to compute.
*
*	p_new
*		[out] Pointer to an empty cl_fmap_t structure that contains the
*		items unique to p_map2 upon return from the function.
*
*	p_old
*		[out] Pointer to an empty cl_fmap_t structure that contains the
*		items unique to p_map1 upon return from the function.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	Items are evaluated based on their keys.  Items that exist in both
*	p_map1 and p_map2 remain in their respective maps.  Items that
*	exist only p_map1 are moved to p_old.  Likewise, items that exist only
*	in p_map2 are moved to p_new.  This function can be useful in evaluating
*	changes between two maps.
*
*	Both maps pointed to by p_new and p_old must be empty on input.  This
*	requirement removes the possibility of failures.
*
* SEE ALSO
*	Flexi Map, cl_fmap_merge
*********/

/****f* Component Library: Flexi Map/cl_fmap_apply_func
* NAME
*	cl_fmap_apply_func
*
* DESCRIPTION
*	The cl_fmap_apply_func function executes a specified function
*	for every item stored in a flexi map.
*
* SYNOPSIS
*/
void
cl_fmap_apply_func(IN const cl_fmap_t * const p_map,
		   IN cl_pfn_fmap_apply_t pfn_func,
		   IN const void *const context);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_fmap_t structure.
*
*	pfn_func
*		[in] Function invoked for every item in the flexi map.
*		See the cl_pfn_fmap_apply_t function type declaration for
*		details about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	The function provided must not perform any map operations, as these
*	would corrupt the flexi map.
*
* SEE ALSO
*	Flexi Map, cl_pfn_fmap_apply_t
*********/

END_C_DECLS
#endif				/* _CL_FLEXIMAP_H_ */
