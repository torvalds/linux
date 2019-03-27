/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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
 *	Declaration of quick list.
 */

#ifndef _CL_QUICK_LIST_H_
#define _CL_QUICK_LIST_H_

#include <complib/cl_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Quick List
* NAME
*	Quick List
*
* DESCRIPTION
*	Quick list implements a doubly linked that stores user provided
*	cl_list_item_t structures.
*	Quick list does not allocate any memory, and can therefore not fail any
*	operations.  Quick list can therefore be useful in minimizing the error
*	paths in code.
*
*	Quick list is not thread safe, and users must provide serialization when
*	adding and removing items from the list. Note that it is possible to
*	walk a quick list while simultaneously adding to it.
*
*	The Quick List functions operate on a cl_qlist_t structure which should be
*	treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_qlist_t, cl_list_item_t, cl_list_obj_t
*
*	Callbacks:
*		cl_pfn_qlist_apply_t, cl_pfn_qlist_find_t
*
*	Item Manipulation:
*		cl_qlist_set_obj, cl_qlist_obj
*
*	Initialization:
*		cl_qlist_init
*
*	Iteration:
*		cl_qlist_next, cl_qlist_prev, cl_qlist_head, cl_qlist_tail,
*		cl_qlist_end
*
*	Manipulation:
*		cl_qlist_insert_head, cl_qlist_insert_tail,
*		cl_qlist_insert_list_head, cl_qlist_insert_list_tail,
*		cl_qlist_insert_array_head, cl_qlist_insert_array_tail,
*		cl_qlist_insert_prev, cl_qlist_insert_next,
*		cl_qlist_remove_head, cl_qlist_remove_tail,
*		cl_qlist_remove_item, cl_qlist_remove_all
*
*	Search:
*		cl_is_item_in_qlist, cl_qlist_find_next, cl_qlist_find_prev,
*		cl_qlist_find_from_head, cl_qlist_find_from_tail
*		cl_qlist_apply_func, cl_qlist_move_items
*
*	Attributes:
*		cl_qlist_count, cl_is_qlist_empty
*********/
/****s* Component Library: Quick List/cl_list_item_t
* NAME
*	cl_list_item_t
*
* DESCRIPTION
*	The cl_list_item_t structure is used by lists to store objects.
*
* SYNOPSIS
*/
typedef struct _cl_list_item {
	struct _cl_list_item *p_next;
	struct _cl_list_item *p_prev;
#ifdef _DEBUG_
	struct _cl_qlist *p_list;
#endif
} cl_list_item_t;
/*
* FIELDS
*	p_next
*		Used internally by the list. Users should not use this field.
*
*	p_prev
*		Used internally by the list. Users should not use this field.
*
* SEE ALSO
*	Quick List
*********/

#define cl_item_obj(item_ptr, obj_ptr, item_field) (typeof(obj_ptr)) \
	((void *)item_ptr - (unsigned long)&((typeof(obj_ptr))0)->item_field)


/****s* Component Library: Quick List/cl_list_obj_t
* NAME
*	cl_list_obj_t
*
* DESCRIPTION
*	The cl_list_obj_t structure is used by lists to store objects.
*
* SYNOPSIS
*/
typedef struct _cl_list_obj {
	cl_list_item_t list_item;
	const void *p_object;	/* User's context */
} cl_list_obj_t;
/*
* FIELDS
*	list_item
*		Used internally by the list. Users should not use this field.
*
*	p_object
*		User defined context. Users should not access this field directly.
*		Use cl_qlist_set_obj and cl_qlist_obj to set and retrieve the value
*		of this field.
*
* NOTES
*	Users can use the cl_qlist_set_obj and cl_qlist_obj functions to store
*	and retrieve context information in the list item.
*
* SEE ALSO
*	Quick List, cl_qlist_set_obj, cl_qlist_obj, cl_list_item_t
*********/

/****s* Component Library: Quick List/cl_qlist_t
* NAME
*	cl_qlist_t
*
* DESCRIPTION
*	Quick list structure.
*
*	The cl_qlist_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_qlist {
	cl_list_item_t end;
	size_t count;
	cl_state_t state;
} cl_qlist_t;
/*
* FIELDS
*	end
*		List item used to mark the end of the list.
*
*	count
*		Number of items in the list.
*
*	state
*		State of the quick list.
*
* SEE ALSO
*	Quick List
*********/

/****d* Component Library: Quick List/cl_pfn_qlist_apply_t
* NAME
*	cl_pfn_qlist_apply_t
*
* DESCRIPTION
*	The cl_pfn_qlist_apply_t function type defines the prototype for functions
*	used to iterate items in a quick list.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_qlist_apply_t) (IN cl_list_item_t * const p_list_item,
			  IN void *context);
/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure.
*
*	context
*		[in] Value passed to the callback function.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_qlist_apply_func
*	function.
*
* SEE ALSO
*	Quick List, cl_qlist_apply_func
*********/

/****d* Component Library: Quick List/cl_pfn_qlist_find_t
* NAME
*	cl_pfn_qlist_find_t
*
* DESCRIPTION
*	The cl_pfn_qlist_find_t function type defines the prototype for functions
*	used to find items in a quick list.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_qlist_find_t) (IN const cl_list_item_t * const p_list_item,
			    IN void *context);
/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to a cl_list_item_t.
*
*	context
*		[in] Value passed to the callback function.
*
* RETURN VALUES
*	Return CL_SUCCESS if the desired item was found. This stops list iteration.
*
*	Return CL_NOT_FOUND to continue list iteration.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_qlist_find_from_head,
*	cl_qlist_find_from_tail, cl_qlist_find_next, and cl_qlist_find_prev
*	functions.
*
* SEE ALSO
*	Quick List, cl_qlist_find_from_head, cl_qlist_find_from_tail,
*	cl_qlist_find_next, cl_qlist_find_prev
*********/

/****i* Component Library: Quick List/__cl_primitive_insert
* NAME
*	__cl_primitive_insert
*
* DESCRIPTION
*	Add a new item in front of the specified item.  This is a low level
*	function for use internally by the queuing routines.
*
* SYNOPSIS
*/
static inline void
__cl_primitive_insert(IN cl_list_item_t * const p_list_item,
		      IN cl_list_item_t * const p_new_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_new_item);

	p_new_item->p_next = p_list_item;
	p_new_item->p_prev = p_list_item->p_prev;
	p_list_item->p_prev = p_new_item;
	p_new_item->p_prev->p_next = p_new_item;
}

/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to cl_list_item_t to insert in front of
*
*	p_new_item
*		[in] Pointer to cl_list_item_t to add
*
* RETURN VALUE
*	This function does not return a value.
*********/

/****i* Component Library: Quick List/__cl_primitive_remove
* NAME
*	__cl_primitive_remove
*
* DESCRIPTION
*	Remove an item from a list.  This is a low level routine
*	for use internally by the queuing routines.
*
* SYNOPSIS
*/
static inline void __cl_primitive_remove(IN cl_list_item_t * const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);

	/* set the back pointer */
	p_list_item->p_next->p_prev = p_list_item->p_prev;
	/* set the next pointer */
	p_list_item->p_prev->p_next = p_list_item->p_next;

	/* if we're debugging, spruce up the pointers to help find bugs */
#if defined( _DEBUG_ )
	if (p_list_item != p_list_item->p_next) {
		p_list_item->p_next = NULL;
		p_list_item->p_prev = NULL;
	}
#endif				/* defined( _DEBUG_ ) */
}

/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to cl_list_item_t to remove
*
* RETURN VALUE
*	This function does not return a value.
*********/

/*
 * Declaration of quick list functions
 */

/****f* Component Library: Quick List/cl_qlist_set_obj
* NAME
*	cl_qlist_set_obj
*
* DESCRIPTION
*	The cl_qlist_set_obj function sets the object stored in a list object.
*
* SYNOPSIS
*/
static inline void
cl_qlist_set_obj(IN cl_list_obj_t * const p_list_obj,
		 IN const void *const p_object)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_obj);
	p_list_obj->p_object = p_object;
}

/*
* PARAMETERS
*	p_list_obj
*		[in] Pointer to a cl_list_obj_t structure.
*
*	p_object
*		[in] User defined context.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Quick List, cl_qlist_obj
*********/

/****f* Component Library: Quick List/cl_qlist_obj
* NAME
*	cl_qlist_obj
*
* DESCRIPTION
*	The cl_qlist_set_obj function returns the object stored in a list object.
*
* SYNOPSIS
*/
static inline void *cl_qlist_obj(IN const cl_list_obj_t * const p_list_obj)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_obj);

	return ((void *)p_list_obj->p_object);
}

/*
* PARAMETERS
*	p_list_obj
*		[in] Pointer to a cl_list_obj_t structure.
*
* RETURN VALUE
*	Returns the value of the object pointer stored in the list object.
*
* SEE ALSO
*	Quick List, cl_qlist_set_obj
*********/

static inline void __cl_qlist_reset(IN cl_qlist_t * const p_list)
{
	/* Point the end item to itself. */
	p_list->end.p_next = &p_list->end;
	p_list->end.p_prev = &p_list->end;
#if defined( _DEBUG_ )
	p_list->end.p_list = p_list;
#endif

	/* Clear the count. */
	p_list->count = 0;
}

/****f* Component Library: Quick List/cl_qlist_init
* NAME
*	cl_qlist_init
*
* DESCRIPTION
*	The cl_qlist_init function initializes a quick list.
*
* SYNOPSIS
*/
static inline void cl_qlist_init(IN cl_qlist_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);

	p_list->state = CL_INITIALIZED;

	/* Reset the quick list data structure. */
	__cl_qlist_reset(p_list);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure to initialize.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	Allows calling quick list manipulation functions.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_head, cl_qlist_insert_tail,
*	cl_qlist_remove_head, cl_qlist_remove_tail
*********/

/****f* Component Library: Quick List/cl_qlist_count
* NAME
*	cl_qlist_count
*
* DESCRIPTION
*	The cl_qlist_count function returns the number of list items stored
*	in a quick list.
*
* SYNOPSIS
*/
static inline uint32_t cl_qlist_count(IN const cl_qlist_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	return ((uint32_t) p_list->count);

}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUE
*	Number of items in the list.  This function iterates though the quick
*	list to count the items.
*
* SEE ALSO
*	Quick List, cl_is_qlist_empty
*********/

/****f* Component Library: Quick List/cl_is_qlist_empty
* NAME
*	cl_is_qlist_empty
*
* DESCRIPTION
*	The cl_is_qlist_empty function returns whether a quick list is empty.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_qlist_empty(IN const cl_qlist_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	return (!cl_qlist_count(p_list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUES
*	TRUE if the specified quick list is empty.
*
*	FALSE otherwise.
*
* SEE ALSO
*	Quick List, cl_qlist_count, cl_qlist_remove_all
*********/

/****f* Component Library: Quick List/cl_qlist_next
* NAME
*	cl_qlist_next
*
* DESCRIPTION
*	The cl_qlist_next function returns a pointer to the list item following
*	a given list item in a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_next(IN const cl_list_item_t *
					    const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list_item->p_list->state == CL_INITIALIZED);

	/* Return the next item. */
	return (p_list_item->p_next);
}

/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to the cl_list_item_t whose successor to return.
*
* Returns:
*	Pointer to the list item following the list item specified by
*	the p_list_item parameter in the quick list.
*
*	Pointer to the list end if p_list_item was at the tail of the list.
*
* SEE ALSO
*	Quick List, cl_qlist_head, cl_qlist_tail, cl_qlist_prev, cl_qlist_end,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_prev
* NAME
*	cl_qlist_prev
*
* DESCRIPTION
*	The cl_qlist_prev function returns a poirter to the list item preceding
*	a given list item in a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_prev(IN const cl_list_item_t *
					    const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list_item->p_list->state == CL_INITIALIZED);

	/* Return the previous item. */
	return (p_list_item->p_prev);
}

/*
* PARAMETERS
*	p_list_item
*		[in] Pointer to the cl_list_item_t whose predecessor to return.
*
* Returns:
*	Pointer to the list item preceding the list item specified by
*	the p_list_item parameter in the quick list.
*
*	Pointer to the list end if p_list_item was at the tail of the list.
*
* SEE ALSO
*	Quick List, cl_qlist_head, cl_qlist_tail, cl_qlist_next, cl_qlist_end,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_head
* NAME
*	cl_qlist_head
*
* DESCRIPTION
*	The cl_qlist_head function returns the list item at
*	the head of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_head(IN const cl_qlist_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	return (cl_qlist_next(&p_list->end));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUES
*	Pointer to the list item at the head of the quick list.
*
*	Pointer to the list end if the list was empty.
*
* NOTES
*	cl_qlist_head does not remove the item from the list.
*
* SEE ALSO
*	Quick List, cl_qlist_tail, cl_qlist_next, cl_qlist_prev, cl_qlist_end,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_tail
* NAME
*	cl_qlist_tail
*
* DESCRIPTION
*	The cl_qlist_tail function returns the list item at
*	the tail of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_tail(IN const cl_qlist_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	return (cl_qlist_prev(&p_list->end));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUES
*	Pointer to the list item at the tail of the quick list.
*
*	Pointer to the list end if the list was empty.
*
* NOTES
*	cl_qlist_tail does not remove the item from the list.
*
* SEE ALSO
*	Quick List, cl_qlist_head, cl_qlist_next, cl_qlist_prev, cl_qlist_end,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_end
* NAME
*	cl_qlist_end
*
* DESCRIPTION
*	The cl_qlist_end function returns the end of a quick list.
*
* SYNOPSIS
*/
static inline const cl_list_item_t *cl_qlist_end(IN const cl_qlist_t *
						 const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	return (&p_list->end);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUE
*	Pointer to the end of the list.
*
* NOTES
*	cl_qlist_end is useful for determining the validity of list items returned
*	by cl_qlist_head, cl_qlist_tail, cl_qlist_next, cl_qlist_prev, as well as
*	the cl_qlist_find functions.  If the list item pointer returned by any of
*	these functions compares to the end, the end of the list was encoutered.
*	When using cl_qlist_head or cl_qlist_tail, this condition indicates that
*	the list is empty.
*
* SEE ALSO
*	Quick List, cl_qlist_head, cl_qlist_tail, cl_qlist_next, cl_qlist_prev,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_head
* NAME
*	cl_qlist_insert_head
*
* DESCRIPTION
*	The cl_qlist_insert_head function inserts a list item at the
*	head of a quick list.
*
* SYNOPSIS
*/
static inline void
cl_qlist_insert_head(IN cl_qlist_t * const p_list,
		     IN cl_list_item_t * const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	/*
	 * The list item must not already be part of the list.  Note that this
	 * assertion may fail if an uninitialized list item happens to have its
	 * list pointer equal to the specified list.  The chances of this
	 * happening are acceptable in light of the value of this check.
	 */
	CL_ASSERT(p_list_item->p_list != p_list);

#if defined( _DEBUG_ )
	p_list_item->p_list = p_list;
#endif

	/* Insert before the head. */
	__cl_primitive_insert(cl_qlist_head(p_list), p_list_item);

	p_list->count++;
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to insert the object.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure to add.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	In debug builds, cl_qlist_insert_head asserts that the specified list item
*	is not already in the list.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_tail, cl_qlist_insert_list_head,
*	cl_qlist_insert_list_tail, cl_qlist_insert_array_head,
*	cl_qlist_insert_array_tail, cl_qlist_insert_prev, cl_qlist_insert_next,
*	cl_qlist_remove_head, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_tail
* NAME
*	cl_qlist_insert_tail
*
* DESCRIPTION
*	The cl_qlist_insert_tail function inserts a list item at the tail
*	of a quick list.
*
* SYNOPSIS
*/
static inline void
cl_qlist_insert_tail(IN cl_qlist_t * const p_list,
		     IN cl_list_item_t * const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	/*
	 * The list item must not already be part of the list.  Note that this
	 * assertion may fail if an uninitialized list item happens to have its
	 * list pointer equal to the specified list.  The chances of this
	 * happening are acceptable in light of the value of this check.
	 */
	CL_ASSERT(p_list_item->p_list != p_list);

#if defined( _DEBUG_ )
	p_list_item->p_list = p_list;
#endif

	/*
	 * Put the new element in front of the end which is the same
	 * as being at the tail
	 */
	__cl_primitive_insert(&p_list->end, p_list_item);

	p_list->count++;
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to insert the object.
*
*	p_list_item
*		[in] Pointer to cl_list_item_t structure to add.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	In debug builds, cl_qlist_insert_tail asserts that the specified list item
*	is not already in the list.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_head, cl_qlist_insert_list_head,
*	cl_qlist_insert_list_tail, cl_qlist_insert_array_head,
*	cl_qlist_insert_array_tail, cl_qlist_insert_prev, cl_qlist_insert_next,
*	cl_qlist_remove_tail, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_list_head
* NAME
*	cl_qlist_insert_list_head
*
* DESCRIPTION
*	The cl_qlist_insert_list_head function merges two quick lists by
*	inserting one at the head of the other.
*
* SYNOPSIS
*/
void
cl_qlist_insert_list_head(IN cl_qlist_t * const p_dest_list,
			  IN cl_qlist_t * const p_src_list);
/*
* PARAMETERS
*	p_dest_list
*		[in] Pointer to destination quicklist object.
*
*	p_src_list
*		[in] Pointer to quicklist to add.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts all list items in the source list to the head of the
*	destination list. The ordering of the list items is preserved.
*
*	The list pointed to by the p_src_list parameter is empty when
*	the call returns.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_list_tail, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_array_head,
*	cl_qlist_insert_array_tail, cl_qlist_insert_prev, cl_qlist_insert_next,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_list_tail
* NAME
*	cl_qlist_insert_list_tail
*
* DESCRIPTION
*	The cl_qlist_insert_list_tail function merges two quick lists by
*	inserting one at the tail of the other.
*
* SYNOPSIS
*/
void
cl_qlist_insert_list_tail(IN cl_qlist_t * const p_dest_list,
			  IN cl_qlist_t * const p_src_list);
/*
* PARAMETERS
*	p_dest_list
*		[in] Pointer to destination quicklist object
*
*	p_src_list
*		[in] Pointer to quicklist to add
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts all list items in the source list to the tail of the
*	destination list. The ordering of the list items is preserved.
*
*	The list pointed to by the p_src_list parameter is empty when
*	the call returns.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_list_head, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_array_head,
*	cl_qlist_insert_array_tail, cl_qlist_insert_prev, cl_qlist_insert_next,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_array_head
* NAME
*	cl_qlist_insert_array_head
*
* DESCRIPTION
*	The cl_qlist_insert_array_head function inserts an array of list items
*	at the head of a quick list.
*
* SYNOPSIS
*/
void
cl_qlist_insert_array_head(IN cl_qlist_t * const p_list,
			   IN cl_list_item_t * const p_array,
			   IN uint32_t item_count, IN const uint32_t item_size);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to insert
*		the objects.
*
*	p_array
*		[in] Pointer to the first list item in an array of cl_list_item_t
*		structures.
*
*	item_count
*		[in] Number of cl_list_item_t structures in the array.
*
*	item_size
*		[in] Size of the items added to the list. This is the stride in the
*		array from one cl_list_item_t structure to the next.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts all the list items in the array specified by the p_array parameter
*	to the head of the quick list specified by the p_list parameter,
*	preserving ordering of the list items.
*
*	The array pointer passed into the function points to the cl_list_item_t
*	in the first element of the caller's element array.  There is no
*	restriction on where the element is stored in the parent structure.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_array_tail, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_list_head, cl_qlist_insert_list_tail,
*	cl_qlist_insert_prev, cl_qlist_insert_next, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_array_tail
* NAME
*	cl_qlist_insert_array_tail
*
* DESCRIPTION
*	The cl_qlist_insert_array_tail function inserts an array of list items
*	at the tail of a quick list.
*
* SYNOPSIS
*/
void
cl_qlist_insert_array_tail(IN cl_qlist_t * const p_list,
			   IN cl_list_item_t * const p_array,
			   IN uint32_t item_count, IN const uint32_t item_size);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to insert
*		the objects.
*
*	p_array
*		[in] Pointer to the first list item in an array of cl_list_item_t
*		structures.
*
*	item_count
*		[in] Number of cl_list_item_t structures in the array.
*
*	item_size
*		[in] Size of the items added to the list. This is the stride in the
*		array from one cl_list_item_t structure to the next.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts all the list items in the array specified by the p_array parameter
*	to the tail of the quick list specified by the p_list parameter,
*	preserving ordering of the list items.
*
*	The array pointer passed into the function points to the cl_list_item_t
*	in the first element of the caller's element array.  There is no
*	restriction on where the element is stored in the parent structure.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_array_head, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_list_head, cl_qlist_insert_list_tail,
*	cl_qlist_insert_prev, cl_qlist_insert_next, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_prev
* NAME
*	cl_qlist_insert_prev
*
* DESCRIPTION
*	The cl_qlist_insert_prev function inserts a list item before a
*	specified list item in a quick list.
*
* SYNOPSIS
*/
static inline void
cl_qlist_insert_prev(IN cl_qlist_t * const p_list,
		     IN cl_list_item_t * const p_list_item,
		     IN cl_list_item_t * const p_new_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_new_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	/*
	 * The list item must not already be part of the list.  Note that this
	 * assertion may fail if an uninitialized list item happens to have its
	 * list pointer equal to the specified list.  The chances of this
	 * happening are acceptable in light of the value of this check.
	 */
	CL_ASSERT(p_new_item->p_list != p_list);

#if defined( _DEBUG_ )
	p_new_item->p_list = p_list;
#endif

	__cl_primitive_insert(p_list_item, p_new_item);

	p_list->count++;
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to add the new item.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure.
*
*	p_new_item
*		[in] Pointer to a cl_list_item_t structure to add to the quick list.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts the new list item before the list item specified by p_list_item.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_next, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_list_head, cl_qlist_insert_list_tail,
*	cl_qlist_insert_array_head, cl_qlist_insert_array_tail, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_insert_next
* NAME
*	cl_qlist_insert_next
*
* DESCRIPTION
*	The cl_qlist_insert_next function inserts a list item after a specified
*	list item in a quick list.
*
* SYNOPSIS
*/
static inline void
cl_qlist_insert_next(IN cl_qlist_t * const p_list,
		     IN cl_list_item_t * const p_list_item,
		     IN cl_list_item_t * const p_new_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_new_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	/*
	 * The list item must not already be part of the list.  Note that this
	 * assertion may fail if an uninitialized list item happens to have its
	 * list pointer equal to the specified list.  The chances of this
	 * happening are acceptable in light of the value of this check.
	 */
	CL_ASSERT(p_new_item->p_list != p_list);

#if defined( _DEBUG_ )
	p_new_item->p_list = p_list;
#endif

	__cl_primitive_insert(cl_qlist_next(p_list_item), p_new_item);

	p_list->count++;
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure into which to add the new item.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure.
*
*	p_new_item
*		[in] Pointer to a cl_list_item_t structure to add to the quick list.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Inserts the new list item after the list item specified by p_list_item.
*	The list item specified by p_list_item must be in the quick list.
*
* SEE ALSO
*	Quick List, cl_qlist_insert_prev, cl_qlist_insert_head,
*	cl_qlist_insert_tail, cl_qlist_insert_list_head, cl_qlist_insert_list_tail,
*	cl_qlist_insert_array_head, cl_qlist_insert_array_tail, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_remove_head
* NAME
*	cl_qlist_remove_head
*
* DESCRIPTION
*	The cl_qlist_remove_head function removes and returns the list item
*	at the head of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_remove_head(IN cl_qlist_t * const p_list)
{
	cl_list_item_t *p_item;

	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	p_item = cl_qlist_head(p_list);
	/* CL_ASSERT that the list item is part of the list. */
	CL_ASSERT(p_item->p_list == p_list);

	if (p_item == cl_qlist_end(p_list))
		return (p_item);

#if defined( _DEBUG_ )
	/* Clear the item's link to the list. */
	p_item->p_list = NULL;
#endif

	__cl_primitive_remove(p_item);

	p_list->count--;

	return (p_item);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUES
*	Returns a pointer to the list item formerly at the head of the quick list.
*
*	Pointer to the list end if the list was empty.
*
* SEE ALSO
*	Quick List, cl_qlist_remove_tail, cl_qlist_remove_all, cl_qlist_remove_item,
*	cl_qlist_end, cl_qlist_head, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_remove_tail
* NAME
*	cl_qlist_remove_tail
*
* DESCRIPTION
*	The cl_qlist_remove_tail function removes and returns the list item
*	at the tail of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_remove_tail(IN cl_qlist_t * const p_list)
{
	cl_list_item_t *p_item;

	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	p_item = cl_qlist_tail(p_list);
	/* CL_ASSERT that the list item is part of the list. */
	CL_ASSERT(p_item->p_list == p_list);

	if (p_item == cl_qlist_end(p_list))
		return (p_item);

#if defined( _DEBUG_ )
	/* Clear the item's link to the list. */
	p_item->p_list = NULL;
#endif

	__cl_primitive_remove(p_item);

	p_list->count--;

	return (p_item);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUES
*	Returns a pointer to the list item formerly at the tail of the quick list.
*
*	Pointer to the list end if the list was empty.
*
* SEE ALSO
*	Quick List, cl_qlist_remove_head, cl_qlist_remove_all, cl_qlist_remove_item,
*	cl_qlist_end, cl_qlist_tail, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_remove_item
* NAME
*	cl_qlist_remove_item
*
* DESCRIPTION
*	The cl_qlist_remove_item function removes a specific list item from a quick list.
*
* SYNOPSIS
*/
static inline void
cl_qlist_remove_item(IN cl_qlist_t * const p_list,
		     IN cl_list_item_t * const p_list_item)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list_item);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	/* CL_ASSERT that the list item is part of the list. */
	CL_ASSERT(p_list_item->p_list == p_list);

	if (p_list_item == cl_qlist_end(p_list))
		return;

#if defined( _DEBUG_ )
	/* Clear the item's link to the list. */
	p_list_item->p_list = NULL;
#endif

	__cl_primitive_remove(p_list_item);

	p_list->count--;
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure from which to remove the item.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure to remove.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Removes the list item pointed to by the p_list_item parameter from
*	its list.
*
* SEE ALSO
*	Quick List, cl_qlist_remove_head, cl_qlist_remove_tail, cl_qlist_remove_all,
*	cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_remove_all
* NAME
*	cl_qlist_remove_all
*
* DESCRIPTION
*	The cl_qlist_remove_all function removes all items from a quick list.
*
* SYNOPSIS
*/
static inline void cl_qlist_remove_all(IN cl_qlist_t * const p_list)
{
#if defined( _DEBUG_ )
	cl_list_item_t *p_list_item;

	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	p_list_item = cl_qlist_head(p_list);
	while (p_list_item != cl_qlist_end(p_list)) {
		p_list_item = cl_qlist_next(p_list_item);
		cl_qlist_prev(p_list_item)->p_list = NULL;
	}
#endif

	__cl_qlist_reset(p_list);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Quick List, cl_qlist_remove_head, cl_qlist_remove_tail,
*	cl_qlist_remove_item, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_is_item_in_qlist
* NAME
*	cl_is_item_in_qlist
*
* DESCRIPTION
*	The cl_is_item_in_qlist function checks for the presence of a
*	list item in a quick list.
*
* SYNOPSIS
*/
boolean_t
cl_is_item_in_qlist(IN const cl_qlist_t * const p_list,
		    IN const cl_list_item_t * const p_list_item);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
*	p_list_item
*		[in] Pointer to the cl_list_item_t to find.
*
* RETURN VALUES
*	TRUE if the list item was found in the quick list.
*
*	FALSE otherwise.
*
* SEE ALSO
*	Quick List, cl_qlist_remove_item, cl_list_item_t
*********/

/****f* Component Library: Quick List/cl_qlist_find_next
* NAME
*	cl_qlist_find_next
*
* DESCRIPTION
*	The cl_qlist_find_next function invokes a specified function to
*	search for an item, starting from a given list item.
*
* SYNOPSIS
*/
cl_list_item_t *cl_qlist_find_next(IN const cl_qlist_t * const p_list,
				   IN const cl_list_item_t * const p_list_item,
				   IN cl_pfn_qlist_find_t pfn_func,
				   IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure in which to search.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure from which to start the search.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_qlist_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context if a
*		callback function is provided, or value compared to the quick list's
*		list items.
*
* Returns:
*	Pointer to the list item, if found.
*
*	p_list_item if not found.
*
* NOTES
*	cl_qlist_find_next does not remove list items from the list.
*	The list item is returned when the function specified by the pfn_func
*	parameter returns CL_SUCCESS.  The list item from which the search starts is
*	excluded from the search.
*
*	The function provided by the pfn_func must not perform any list operations,
*	as these would corrupt the list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_prev, cl_qlist_find_from_head,
*	cl_qlist_find_from_tail, cl_qlist_end, cl_qlist_apply_func,
*	cl_qlist_move_items, cl_list_item_t, cl_pfn_qlist_find_t
*********/

/****f* Component Library: Quick List/cl_qlist_find_prev
* NAME
*	cl_qlist_find_prev
*
* DESCRIPTION
*	The cl_qlist_find_prev function invokes a specified function to
*	search backward for an item, starting from a given list item.
*
* SYNOPSIS
*/
cl_list_item_t *cl_qlist_find_prev(IN const cl_qlist_t * const p_list,
				   IN const cl_list_item_t * const p_list_item,
				   IN cl_pfn_qlist_find_t pfn_func,
				   IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure in which to search.
*
*	p_list_item
*		[in] Pointer to a cl_list_item_t structure from which to start the search.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_qlist_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context if a
*		callback function is provided, or value compared to the quick list's
*		list items.
*
* Returns:
*	Pointer to the list item, if found.
*
*	p_list_item if not found.
*
* NOTES
*	cl_qlist_find_prev does not remove list items from the list.
*	The list item is returned when the function specified by the pfn_func
*	parameter returns CL_SUCCESS.  The list item from which the search starts is
*	excluded from the search.
*
*	The function provided by the pfn_func must not perform any list operations,
*	as these would corrupt the list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_next, cl_qlist_find_from_head,
*	cl_qlist_find_from_tail, cl_qlist_end, cl_qlist_apply_func,
*	cl_qlist_move_items, cl_list_item_t, cl_pfn_qlist_find_t
*********/

/****f* Component Library: Quick List/cl_qlist_find_from_head
* NAME
*	cl_qlist_find_from_head
*
* DESCRIPTION
*	The cl_qlist_find_from_head function invokes a specified function to
*	search for an item, starting at the head of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_find_from_head(IN const cl_qlist_t *
						      const p_list,
						      IN cl_pfn_qlist_find_t
						      pfn_func,
						      IN const void *const
						      context)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	/* CL_ASSERT that a find function is provided. */
	CL_ASSERT(pfn_func);

	return (cl_qlist_find_next(p_list, cl_qlist_end(p_list), pfn_func,
				   context));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_qlist_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context if a
*		callback function is provided, or value compared to the quick list's
*		list items.
*
* Returns:
*	Pointer to the list item, if found.
*
*	Pointer to the list end otherwise
*
* NOTES
*	cl_qlist_find_from_head does not remove list items from the list.
*	The list item is returned when the function specified by the pfn_func
*	parameter returns CL_SUCCESS.
*
*	The function provided by the pfn_func parameter must not perform any list
*	operations, as these would corrupt the list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_from_tail, cl_qlist_find_next, cl_qlist_find_prev,
*	cl_qlist_end, cl_qlist_apply_func, cl_qlist_move_items, cl_list_item_t,
*	cl_pfn_qlist_find_t
*********/

/****f* Component Library: Quick List/cl_qlist_find_from_tail
* NAME
*	cl_qlist_find_from_tail
*
* DESCRIPTION
*	The cl_qlist_find_from_tail function invokes a specified function to
*	search for an item, starting at the tail of a quick list.
*
* SYNOPSIS
*/
static inline cl_list_item_t *cl_qlist_find_from_tail(IN const cl_qlist_t *
						      const p_list,
						      IN cl_pfn_qlist_find_t
						      pfn_func,
						      IN const void *const
						      context)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/* CL_ASSERT that the list was initialized. */
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	/* CL_ASSERT that a find function is provided. */
	CL_ASSERT(pfn_func);

	return (cl_qlist_find_prev(p_list, cl_qlist_end(p_list), pfn_func,
				   context));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_qlist_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context if a
*		callback function is provided, or value compared to the quick list's
*		list items.
*
* Returns:
*	Pointer to the list item, if found.
*
*	Pointer to the list end otherwise
*
* NOTES
*	cl_qlist_find_from_tail does not remove list items from the list.
*	The list item is returned when the function specified by the pfn_func
*	parameter returns CL_SUCCESS.
*
*	The function provided by the pfn_func parameter must not perform any list
*	operations, as these would corrupt the list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_from_head, cl_qlist_find_next, cl_qlist_find_prev,
*	cl_qlist_apply_func, cl_qlist_end, cl_qlist_move_items, cl_list_item_t,
*	cl_pfn_qlist_find_t
*********/

/****f* Component Library: Quick List/cl_qlist_apply_func
* NAME
*	cl_qlist_apply_func
*
* DESCRIPTION
*	The cl_qlist_apply_func function executes a specified function
*	for every list item stored in a quick list.
*
* SYNOPSIS
*/
void
cl_qlist_apply_func(IN const cl_qlist_t * const p_list,
		    IN cl_pfn_qlist_apply_t pfn_func,
		    IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_qlist_t structure.
*
*	pfn_func
*		[in] Function invoked for every item in the quick list.
*		See the cl_pfn_qlist_apply_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	The function provided must not perform any list operations, as these
*	would corrupt the quick list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_from_head, cl_qlist_find_from_tail,
*	cl_qlist_move_items, cl_pfn_qlist_apply_t
*********/

/****f* Component Library: Quick List/cl_qlist_move_items
* NAME
*	cl_qlist_move_items
*
* DESCRIPTION
*	The cl_qlist_move_items function moves list items from one list to
*	another based on the return value of a user supplied function.
*
* SYNOPSIS
*/
void
cl_qlist_move_items(IN cl_qlist_t * const p_src_list,
		    IN cl_qlist_t * const p_dest_list,
		    IN cl_pfn_qlist_find_t pfn_func,
		    IN const void *const context);
/*
* PARAMETERS
*	p_src_list
*		[in] Pointer to a cl_qlist_t structure from which
*		list items are removed.
*
*	p_dest_list
*		[in] Pointer to a cl_qlist_t structure to which the source
*		list items are added.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_qlist_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	If the function specified by the pfn_func parameter returns CL_SUCCESS,
*	the related list item is removed from p_src_list and inserted at the tail
*	of the p_dest_list.
*
*	The cl_qlist_move_items function continues iterating through p_src_list
*	from the last item moved, allowing multiple items to be located and moved
*	in a single list iteration.
*
*	The function specified by pfn_func must not perform any list operations,
*	as these would corrupt the list.
*
* SEE ALSO
*	Quick List, cl_qlist_find_from_head, cl_qlist_find_from_tail,
*	cl_qlist_apply_func, cl_pfn_qlist_find_t
*********/

END_C_DECLS
#endif				/* _CL_QUICK_LIST_H_ */
