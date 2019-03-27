/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
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
 *	Declaration of map, a binary tree.
 */

#ifndef _CL_MAP_H_
#define _CL_MAP_H_

#include <complib/cl_qmap.h>
#include <complib/cl_qpool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Map
* NAME
*	Map
*
* DESCRIPTION
*	Map implements a binary tree that stores user objects.  Each item stored
*	in a map has a unique 64-bit key (duplicates are not allowed).  Map
*	provides the ability to efficiently search for an item given a key.
*
*	Map may allocate memory when inserting objects, and can therefore fail
*	operations due to insufficient memory.  Use quick map in situations
*	where such insertion failures cannot be tolerated.
*
*	Map is not thread safe, and users must provide serialization when adding
*	and removing items from the map.
*
*	The map functions operates on a cl_map_t structure which should be treated
*	as opaque and should be manipulated only through the provided functions.
*
* SEE ALSO
*	Types:
*		cl_map_iterator_t
*
*	Structures:
*		cl_map_t, cl_map_item_t, cl_map_obj_t
*
*	Item Manipulation:
*		cl_map_obj, cl_map_key
*
*	Initialization:
*		cl_map_construct, cl_map_init, cl_map_destroy
*
*	Iteration:
*		cl_map_end, cl_map_head, cl_map_tail, cl_map_next, cl_map_prev
*
*	Manipulation
*		cl_map_insert, cl_map_get, cl_map_remove_item, cl_map_remove,
*		cl_map_remove_all, cl_map_merge, cl_map_delta, cl_map_get_next
*
*	Attributes:
*		cl_map_count, cl_is_map_empty, cl_is_map_inited
*********/
/****s* Component Library: Map/cl_map_t
* NAME
*	cl_map_t
*
* DESCRIPTION
*	Quick map structure.
*
*	The cl_map_t structure should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_map {
	cl_qmap_t qmap;
	cl_qpool_t pool;
} cl_map_t;
/*
* FIELDS
*	qmap
*		Quick map object that maintains the map.
*
*	pool
*		Pool of cl_map_obj_t structures used to store user objects
*		in the map.
*
* SEE ALSO
*	Map, cl_map_obj_t
*********/

/****d* Component Library: Map/cl_map_iterator_t
* NAME
*	cl_map_iterator_t
*
* DESCRIPTION
*	Iterator type used to walk a map.
*
* SYNOPSIS
*/
typedef const cl_map_item_t *cl_map_iterator_t;
/*
* NOTES
*	The iterator should be treated as opaque to prevent corrupting the map.
*
* SEE ALSO
*	Map, cl_map_head, cl_map_tail, cl_map_next, cl_map_prev, cl_map_key
*********/

/****f* Component Library: Map/cl_map_count
* NAME
*	cl_map_count
*
* DESCRIPTION
*	The cl_map_count function returns the number of items stored
*	in a map.
*
* SYNOPSIS
*/
static inline size_t cl_map_count(IN const cl_map_t * const p_map)
{
	CL_ASSERT(p_map);
	return (cl_qmap_count(&p_map->qmap));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map whose item count to return.
*
* RETURN VALUE
*	Returns the number of items stored in the map.
*
* SEE ALSO
*	Map, cl_is_map_empty
*********/

/****f* Component Library: Map/cl_is_map_empty
* NAME
*	cl_is_map_empty
*
* DESCRIPTION
*	The cl_is_map_empty function returns whether a map is empty.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_map_empty(IN const cl_map_t * const p_map)
{
	CL_ASSERT(p_map);
	return (cl_is_qmap_empty(&p_map->qmap));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map to test for emptiness.
*
* RETURN VALUES
*	TRUE if the map is empty.
*
*	FALSE otherwise.
*
* SEE ALSO
*	Map, cl_map_count, cl_map_remove_all
*********/

/****f* Component Library: Map/cl_map_key
* NAME
*	cl_map_key
*
* DESCRIPTION
*	The cl_map_key function retrieves the key value of a map item.
*
* SYNOPSIS
*/
static inline uint64_t cl_map_key(IN const cl_map_iterator_t itor)
{
	return (cl_qmap_key(itor));
}

/*
* PARAMETERS
*	itor
*		[in] Iterator for the item whose key to return.
*
* RETURN VALUE
*	Returns the 64-bit key value for the specified iterator.
*
* NOTES
*	The iterator specified by the itor parameter must have been retrived by
*	a previous call to cl_map_head, cl_map_tail, cl_map_next, or cl_map_prev.
*
*	The key value is set in a call to cl_map_insert.
*
* SEE ALSO
*	Map, cl_map_insert, cl_map_head, cl_map_tail, cl_map_next, cl_map_prev
*********/

/****f* Component Library: Map/cl_map_construct
* NAME
*	cl_map_construct
*
* DESCRIPTION
*	The cl_map_construct function constructs a map.
*
* SYNOPSIS
*/
void cl_map_construct(IN cl_map_t * const p_map);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_map_t structure to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_map_init, cl_map_destroy, and cl_is_map_inited.
*
*	Calling cl_map_construct is a prerequisite to calling any other
*	map function except cl_map_init.
*
* SEE ALSO
*	Map, cl_map_init, cl_map_destroy, cl_is_map_inited
*********/

/****f* Component Library: Event/cl_is_map_inited
* NAME
*	cl_is_map_inited
*
* DESCRIPTION
*	The cl_is_map_inited function returns whether a map was
*	successfully initialized.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_map_inited(IN const cl_map_t * const p_map)
{
	/*
	 * The map's pool of map items is the last thing initialized.
	 * We can therefore use it to test for initialization.
	 */
	return (cl_is_qpool_inited(&p_map->pool));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_map_t structure whose initialization state
*		to check.
*
* RETURN VALUES
*	TRUE if the map was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a map to determine if invoking
*	member functions is appropriate.
*
* SEE ALSO
*	Map
*********/

/****f* Component Library: Map/cl_map_init
* NAME
*	cl_map_init
*
* DESCRIPTION
*	The cl_map_init function initialized a map for use.
*
* SYNOPSIS
*/
cl_status_t cl_map_init(IN cl_map_t * const p_map, IN const uint32_t min_items);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_map_t structure to initialize.
*
*	min_items
*		[in] Minimum number of items that can be stored.  All necessary
*		allocations to allow storing the minimum number of items is
*		performed at initialization time.
*
* RETURN VALUES
*	CL_SUCCESS if the map was initialized successfully.
*
* NOTES
*	Allows calling map manipulation functions.
*
* SEE ALSO
*	Map, cl_map_destroy, cl_map_insert, cl_map_remove
*********/

/****f* Component Library: Map/cl_map_destroy
* NAME
*	cl_map_destroy
*
* DESCRIPTION
*	The cl_map_destroy function destroys a map.
*
* SYNOPSIS
*/
void cl_map_destroy(IN cl_map_t * const p_map);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified map. Further
*	operations should not be attempted on the map. cl_map_destroy does
*	not affect any of the objects stored in the map.
*	This function should only be called after a call to cl_map_construct.
*
*	In debug builds, cl_map_destroy asserts that the map is empty.
*
* SEE ALSO
*	Map, cl_map_construct, cl_map_init
*********/

/****f* Component Library: Map/cl_map_end
* NAME
*	cl_map_end
*
* DESCRIPTION
*	The cl_map_end function returns the iterator for the end of a map.
*
* SYNOPSIS
*/
static inline cl_map_iterator_t cl_map_end(IN const cl_map_t * const p_map)
{
	CL_ASSERT(p_map);
	return (cl_qmap_end(&p_map->qmap));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_map_t structure whose end to return.
*
* RETURN VALUE
*	Iterator for the end of the map.
*
* NOTES
*	cl_map_end is useful for determining the validity of map items returned
*	by cl_map_head, cl_map_tail, cl_map_next, cl_map_prev.  If the iterator
*	by any of these functions compares to the end, the end of the map was
*	encoutered.
*	When using cl_map_head or cl_map_tail, this condition indicates that
*	the map is empty.
*
* SEE ALSO
*	Map, cl_qmap_head, cl_qmap_tail, cl_qmap_next, cl_qmap_prev
*********/

/****f* Component Library: Map/cl_map_head
* NAME
*	cl_map_head
*
* DESCRIPTION
*	The cl_map_head function returns the map item with the lowest key
*	value stored in a map.
*
* SYNOPSIS
*/
static inline cl_map_iterator_t cl_map_head(IN const cl_map_t * const p_map)
{
	CL_ASSERT(p_map);
	return (cl_qmap_head(&p_map->qmap));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map whose item with the lowest key is returned.
*
* RETURN VALUES
*	Iterator for the object with the lowest key in the map.
*
*	Iterator for the map end if the map was empty.
*
* NOTES
*	cl_map_head does not remove the object from the map.
*
* SEE ALSO
*	Map, cl_map_tail, cl_map_next, cl_map_prev, cl_map_end
*********/

/****f* Component Library: Map/cl_map_tail
* NAME
*	cl_map_tail
*
* DESCRIPTION
*	The cl_map_tail function returns the map item with the highest key
*	value stored in a map.
*
* SYNOPSIS
*/
static inline cl_map_iterator_t cl_map_tail(IN const cl_map_t * const p_map)
{
	CL_ASSERT(p_map);
	return (cl_qmap_tail(&p_map->qmap));
}

/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map whose item with the highest key
*		is returned.
*
* RETURN VALUES
*	Iterator for the object with the highest key in the map.
*
*	Iterator for the map end if the map was empty.
*
* NOTES
*	cl_map_end does no remove the object from the map.
*
* SEE ALSO
*	Map, cl_map_head, cl_map_next, cl_map_prev, cl_map_end
*********/

/****f* Component Library: Map/cl_map_next
* NAME
*	cl_map_next
*
* DESCRIPTION
*	The cl_map_next function returns the map item with the next higher
*	key value than a specified map item.
*
* SYNOPSIS
*/
static inline cl_map_iterator_t cl_map_next(IN const cl_map_iterator_t itor)
{
	CL_ASSERT(itor);
	return (cl_qmap_next(itor));
}

/*
* PARAMETERS
*	itor
*		[in] Iterator for an object in a map whose successor to return.
*
* RETURN VALUES
*	Iterator for the object with the next higher key value in a map.
*
*	Iterator for the map end if the specified object was the last item in
*	the map.
*
* NOTES
*	The iterator must have been retrieved by a previous call to cl_map_head,
*	cl_map_tail, cl_map_next, or cl_map_prev.
*
* SEE ALSO
*	Map, cl_map_head, cl_map_tail, cl_map_prev, cl_map_end
*********/

/****f* Component Library: Map/cl_map_prev
* NAME
*	cl_map_prev
*
* DESCRIPTION
*	The cl_map_prev function returns the map item with the next lower
*	key value than a precified map item.
*
* SYNOPSIS
*/
static inline cl_map_iterator_t cl_map_prev(IN const cl_map_iterator_t itor)
{
	CL_ASSERT(itor);
	return (cl_qmap_prev(itor));
}

/*
* PARAMETERS
*	itor
*		[in] Iterator for an object in a map whose predecessor to return.
*
* RETURN VALUES
*	Iterator for the object with the next lower key value in a map.
*
*	Iterator for the map end if the specified object was the first item in
*	the map.
*
* NOTES
*	The iterator must have been retrieved by a previous call to cl_map_head,
*	cl_map_tail, cl_map_next, or cl_map_prev.
*
* SEE ALSO
*	Map, cl_map_head, cl_map_tail, cl_map_next, cl_map_end
*********/

/****f* Component Library: Map/cl_map_insert
* NAME
*	cl_map_insert
*
* DESCRIPTION
*	The cl_map_insert function inserts a map item into a map.
*
* SYNOPSIS
*/
void *cl_map_insert(IN cl_map_t * const p_map,
		    IN const uint64_t key, IN const void *const p_object);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map into which to add the item.
*
*	key
*		[in] Value to associate with the object.
*
*	p_object
*		[in] Pointer to an object to insert into the map.
*
* RETURN VALUES
*	Pointer to the object in the map with the specified key after the call
*	completes.
*
*	NULL if there was not enough memory to insert the desired item.
*
* NOTES
*	Insertion operations may cause the map to rebalance.
*
*	If the map already contains an object already with the specified key,
*	that object will not be replaced and the pointer to that object is
*	returned.
*
* SEE ALSO
*	Map, cl_map_remove, cl_map_item_t
*********/

/****f* Component Library: Map/cl_map_get
* NAME
*	cl_map_get
*
* DESCRIPTION
*	The cl_map_get function returns the object associated with a key.
*
* SYNOPSIS
*/
void *cl_map_get(IN const cl_map_t * const p_map, IN const uint64_t key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map from which to retrieve the object with
*		the specified key.
*
*	key
*		[in] Key value used to search for the desired object.
*
* RETURN VALUES
*	Pointer to the object with the desired key value.
*
*	NULL if there was no item with the desired key value stored in
*	the map.
*
* NOTES
*	cl_map_get does not remove the item from the map.
*
* SEE ALSO
*	Map, cl_map_remove, cl_map_get_next
*********/

/****f* Component Library: Map/cl_map_get_next
* NAME
*	cl_map_get_next
*
* DESCRIPTION
*	The cl_qmap_get_next function returns the first object associated with a
*	key > the key specified.
*
* SYNOPSIS
*/
void *cl_map_get_next(IN const cl_map_t * const p_map, IN const uint64_t key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map from which to retrieve the object with
*		the specified key.
*
*	key
*		[in] Key value used to search for the desired object.
*
* RETURN VALUES
*	Pointer to the first object with a key > the desired key value.
*
*	NULL if there was no item with a key > the desired key
*	value stored in the map.
*
* NOTES
*	cl_map_get does not remove the item from the map.
*
* SEE ALSO
*	Map, cl_map_remove, cl_map_get
*********/

/****f* Component Library: Map/cl_map_remove_item
* NAME
*	cl_map_remove_item
*
* DESCRIPTION
*	The cl_map_remove_item function removes the specified map item
*	from a map.
*
* SYNOPSIS
*/
void
cl_map_remove_item(IN cl_map_t * const p_map, IN const cl_map_iterator_t itor);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map from which to remove the object associated
*		with the specified iterator.
*
*	itor
*		[in] Iterator for an object to remove from its map.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Removes the object associated with the specifid iterator from its map.
*
*	The specified iterator is no longer valid after the call completes.
*
*	The iterator must have been retrieved by a previous call to cl_map_head,
*	cl_map_tail, cl_map_next, or cl_map_prev.
*
* SEE ALSO
*	Map, cl_map_remove, cl_map_remove_all, cl_map_insert, cl_map_head,
*	cl_map_tail, cl_map_next, cl_map_prev
*********/

/****f* Component Library: Map/cl_map_remove
* NAME
*	cl_map_remove
*
* DESCRIPTION
*	The cl_map_remove function removes the map item with the specified key
*	from a map.
*
* SYNOPSIS
*/
void *cl_map_remove(IN cl_map_t * const p_map, IN const uint64_t key);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a cl_map_t structure from which to remove the
*		item with the specified key.
*
*	key
*		[in] Key value used to search for the object to remove.
*
* RETURN VALUES
*	Pointer to the object associated with the specified key if
*	it was found and removed.
*
*	NULL if no object with the specified key exists in the map.
*
* SEE ALSO
*	Map, cl_map_remove_item, cl_map_remove_all, cl_map_insert
*********/

/****f* Component Library: Map/cl_map_remove_all
* NAME
*	cl_map_remove_all
*
* DESCRIPTION
*	The cl_map_remove_all function removes all objects from a map,
*	leaving it empty.
*
* SYNOPSIS
*/
void cl_map_remove_all(IN cl_map_t * const p_map);
/*
* PARAMETERS
*	p_map
*		[in] Pointer to a map to empty.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Map, cl_map_remove, cl_map_remove_item
*********/

/****f* Component Library: Map/cl_map_obj
* NAME
*	cl_map_obj
*
* DESCRIPTION
*	The cl_map_obj function returns the object associated with an iterator.
*
* SYNOPSIS
*/
static inline void *cl_map_obj(IN const cl_map_iterator_t itor)
{
	return (cl_qmap_obj(PARENT_STRUCT(itor, cl_map_obj_t, item)));
}

/*
* PARAMETERS
*	itor
*		[in] Iterator whose object to return.
*
* RETURN VALUES
*	Returns the value of the object pointer associated with the iterator.
*
*	The iterator must have been retrieved by a previous call to cl_map_head,
*	cl_map_tail, cl_map_next, or cl_map_prev.
*
* SEE ALSO
*	Map, cl_map_head, cl_map_tail, cl_map_next, cl_map_prev
*********/

/****f* Component Library: Map/cl_map_merge
* NAME
*	cl_map_merge
*
* DESCRIPTION
*	The cl_map_merge function moves all items from one map to another,
*	excluding duplicates.
*
* SYNOPSIS
*/
cl_status_t
cl_map_merge(OUT cl_map_t * const p_dest_map,
	     IN OUT cl_map_t * const p_src_map);
/*
* PARAMETERS
*	p_dest_map
*		[out] Pointer to a cl_map_t structure to which items should be added.
*
*	p_src_map
*		[in/out] Pointer to a cl_map_t structure whose items to add
*		to p_dest_map.
*
* RETURN VALUES
*	CL_SUCCESS if the operation succeeded.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the operation
*	to succeed.
*
* NOTES
*	Items are evaluated based on their keys only.
*
*	Upon return from cl_map_merge, the map referenced by p_src_map contains
*	all duplicate items.
*
* SEE ALSO
*	Map, cl_map_delta
*********/

/****f* Component Library: Map/cl_map_delta
* NAME
*	cl_map_delta
*
* DESCRIPTION
*	The cl_map_delta function computes the differences between two maps.
*
* SYNOPSIS
*/
cl_status_t
cl_map_delta(IN OUT cl_map_t * const p_map1,
	     IN OUT cl_map_t * const p_map2,
	     OUT cl_map_t * const p_new, OUT cl_map_t * const p_old);
/*
* PARAMETERS
*	p_map1
*		[in/out] Pointer to the first of two cl_map_t structures whose
*		differences to compute.
*
*	p_map2
*		[in/out] Pointer to the second of two cl_map_t structures whose
*		differences to compute.
*
*	p_new
*		[out] Pointer to an empty cl_map_t structure that contains the
*		items unique to p_map2 upon return from the function.
*
*	p_old
*		[out] Pointer to an empty cl_map_t structure that contains the
*		items unique to p_map1 upon return from the function.
*
* RETURN VALUES
*	CL_SUCCESS if the operation succeeded.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the operation
*	to succeed.
*
* NOTES
*	Items are evaluated based on their keys.  Items that exist in both
*	p_map1 and p_map2 remain in their respective maps.  Items that
*	exist only p_map1 are moved to p_old.  Likewise, items that exist only
*	in p_map2 are moved to p_new.  This function can be useful in evaluating
*	changes between two maps.
*
*	Both maps pointed to by p_new and p_old must be empty on input.
*
*	Upon failure, all input maps are restored to their original state.
*
* SEE ALSO
*	Map, cl_map_merge
*********/

END_C_DECLS
#endif				/* _CL_MAP_H_ */
