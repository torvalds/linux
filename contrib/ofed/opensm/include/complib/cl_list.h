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
 *	Declaration of list.
 */

#ifndef _CL_LIST_H_
#define _CL_LIST_H_

#include <complib/cl_qlist.h>
#include <complib/cl_qpool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/List
* NAME
*	List
*
* DESCRIPTION
*	List stores objects in a doubly linked list.
*
*	Unlike quick list, users pass pointers to the object being stored, rather
*	than to a cl_list_item_t structure.  Insertion operations on a list can
*	fail, and callers should trap for such failures.
*
*	Use quick list in situations where insertion failures cannot be tolerated.
*
*	List is not thread safe, and users must provide serialization.
*
*	The list functions operates on a cl_list_t structure which should be
*	treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Types:
*		cl_list_iterator_t
*
*	Structures:
*		cl_list_t
*
*	Callbacks:
*		cl_pfn_list_apply_t, cl_pfn_list_find_t
*
*	Initialization/Destruction:
*		cl_list_construct, cl_list_init, cl_list_destroy
*
*	Iteration:
*		cl_list_next, cl_list_prev, cl_list_head, cl_list_tail,
*		cl_list_end
*
*	Manipulation:
*		cl_list_insert_head, cl_list_insert_tail,
*		cl_list_insert_array_head, cl_list_insert_array_tail,
*		cl_list_insert_prev, cl_list_insert_next,
*		cl_list_remove_head, cl_list_remove_tail,
*		cl_list_remove_object, cl_list_remove_item, cl_list_remove_all
*
*	Search:
*		cl_is_object_in_list, cl_list_find_from_head, cl_list_find_from_tail,
*		cl_list_apply_func
*
*	Attributes:
*		cl_list_count, cl_is_list_empty, cl_is_list_inited
*********/
/****s* Component Library: List/cl_list_t
* NAME
*	cl_list_t
*
* DESCRIPTION
*	List structure.
*
*	The cl_list_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_list {
	cl_qlist_t list;
	cl_qpool_t list_item_pool;
} cl_list_t;
/*
* FIELDS
*	list
*		Quick list of items stored in the list.
*
*	list_item_pool
*		Quick pool of list objects for storing objects in the quick list.
*
* SEE ALSO
*	List
*********/

/****d* Component Library: List/cl_list_iterator_t
* NAME
*	cl_list_iterator_t
*
* DESCRIPTION
*	Iterator type used to walk a list.
*
* SYNOPSIS
*/
typedef const cl_list_item_t *cl_list_iterator_t;
/*
* NOTES
*	The iterator should be treated as opaque to prevent corrupting the list.
*
* SEE ALSO
*	List, cl_list_head, cl_list_tail, cl_list_next, cl_list_prev,
*	cl_list_obj
*********/

/****d* Component Library: List/cl_pfn_list_apply_t
* NAME
*	cl_pfn_list_apply_t
*
* DESCRIPTION
*	The cl_pfn_list_apply_t function type defines the prototype for functions
*	used to iterate objects in a list.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_list_apply_t) (IN void *const p_object, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object stored in a list.
*
*	context
*		[in] Context provided in a call to cl_list_apply_func.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_list_apply_func
*	function.
*
* SEE ALSO
*	List, cl_list_apply_func
*********/

/****d* Component Library: List/cl_pfn_list_find_t
* NAME
*	cl_pfn_list_find_t
*
* DESCRIPTION
*	The cl_pfn_list_find_t function type defines the prototype for functions
*	used to find objects in a list.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_list_find_t) (IN const void *const p_object, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object stored in a list.
*
*	context
*		[in] Context provided in a call to ListFindFromHead or ListFindFromTail.
*
* RETURN VALUES
*	Return CL_SUCCESS if the desired item was found.  This stops list iteration.
*
*	Return CL_NOT_FOUND to continue the list iteration.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_list_find_from_head
*	and cl_list_find_from_tail functions.
*
* SEE ALSO
*	List, cl_list_find_from_head, cl_list_find_from_tail
*********/

/****f* Component Library: List/cl_list_construct
* NAME
*	cl_list_construct
*
* DESCRIPTION
*	The cl_list_construct function constructs a list.
*
* SYNOPSIS
*/
void cl_list_construct(IN cl_list_t * const p_list);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to cl_list_t object whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_list_init, cl_list_destroy and cl_is_list_inited.
*
*	Calling cl_list_construct is a prerequisite to calling any other
*	list function except cl_list_init.
*
* SEE ALSO
*	List, cl_list_init, cl_list_destroy, cl_is_list_inited
*********/

/****f* Component Library: List/cl_is_list_inited
* NAME
*	cl_is_list_inited
*
* DESCRIPTION
*	The cl_is_list_inited function returns whether a list was
*	initialized successfully.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_list_inited(IN const cl_list_t * const p_list)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_list);
	/*
	 * The pool is the last thing initialized.  If it is initialized, the
	 * list is initialized too.
	 */
	return (cl_is_qpool_inited(&p_list->list_item_pool));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure whose initilization state
*		to check.
*
* RETURN VALUES
*	TRUE if the list was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a list to determine if invoking
*	member functions is appropriate.
*
* SEE ALSO
*	List
*********/

/****f* Component Library: List/cl_list_init
* NAME
*	cl_list_init
*
* DESCRIPTION
*	The cl_list_init function initializes a list for use.
*
* SYNOPSIS
*/
cl_status_t
cl_list_init(IN cl_list_t * const p_list, IN const size_t min_items);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to cl_list_t structure to initialize.
*
*	min_items
*		[in] Minimum number of items that can be stored.  All necessary
*		allocations to allow storing the minimum number of items is performed
*		at initialization time.
*
* RETURN VALUES
*	CL_SUCCESS if the list was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for initialization.
*
* NOTES
*	The list will always be able to store at least as many items as specified
*	by the min_items parameter.
*
* SEE ALSO
*	List, cl_list_construct, cl_list_destroy, cl_list_insert_head,
*	cl_list_insert_tail, cl_list_remove_head, cl_list_remove_tail
*********/

/****f* Component Library: List/cl_list_destroy
* NAME
*	cl_list_destroy
*
* DESCRIPTION
*	The cl_list_destroy function destroys a list.
*
* SYNOPSIS
*/
void cl_list_destroy(IN cl_list_t * const p_list);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to cl_list_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_list_destroy does not affect any of the objects stored in the list,
*	but does release all memory allocated internally.  Further operations
*	should not be attempted on the list after cl_list_destroy is invoked.
*
*	This function should only be called after a call to cl_list_construct
*	or cl_list_init.
*
*	In debug builds, cl_list_destroy asserts if the list is not empty.
*
* SEE ALSO
*	List, cl_list_construct, cl_list_init
*********/

/****f* Component Library: List/cl_is_list_empty
* NAME
*	cl_is_list_empty
*
* DESCRIPTION
*	The cl_is_list_empty function returns whether a list is empty.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_list_empty(IN const cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	return (cl_is_qlist_empty(&p_list->list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure.
*
* RETURN VALUES
*	TRUE if the specified list is empty.
*
*	FALSE otherwise.
*
* SEE ALSO
*	List, cl_list_count, cl_list_remove_all
*********/

/****f* Component Library: List/cl_list_insert_head
* NAME
*	cl_list_insert_head
*
* DESCRIPTION
*	The cl_list_insert_head function inserts an object at the head of a list.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_list_insert_head(IN cl_list_t * const p_list, IN const void *const p_object)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* Get a list item to add to the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qpool_get(&p_list->list_item_pool);
	if (!p_pool_obj)
		return (CL_INSUFFICIENT_MEMORY);

	p_pool_obj->p_object = p_object;
	cl_qlist_insert_head(&p_list->list, &p_pool_obj->pool_item.list_item);
	return (CL_SUCCESS);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the object.
*
*	p_object
*		[in] Pointer to an object to insert into the list.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* NOTES
*	Inserts the specified object at the head of the list.  List insertion
*	operations are guaranteed to work for the minimum number of items as
*	specified in cl_list_init by the min_items parameter.
*
* SEE ALSO
*	List, cl_list_insert_tail, cl_list_insert_array_head,
*	cl_list_insert_array_tail, cl_list_insert_prev, cl_list_insert_next,
*	cl_list_remove_head
*********/

/****f* Component Library: List/cl_list_insert_tail
* NAME
*	cl_list_insert_tail
*
* DESCRIPTION
*	The cl_list_insert_tail function inserts an object at the tail of a list.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_list_insert_tail(IN cl_list_t * const p_list, IN const void *const p_object)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* Get a list item to add to the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qpool_get(&p_list->list_item_pool);
	if (!p_pool_obj)
		return (CL_INSUFFICIENT_MEMORY);

	p_pool_obj->p_object = p_object;
	cl_qlist_insert_tail(&p_list->list, &p_pool_obj->pool_item.list_item);
	return (CL_SUCCESS);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the object.
*
*	p_object
*		[in] Pointer to an object to insert into the list.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* NOTES
*	Inserts the specified object at the tail of the list.  List insertion
*	operations are guaranteed to work for the minimum number of items as
*	specified in cl_list_init by the min_items parameter.
*
* SEE ALSO
*	List, cl_list_insert_head, cl_list_insert_array_head,
*	cl_list_insert_array_tail, cl_list_insert_prev, cl_list_insert_next,
*	cl_list_remove_tail
*********/

/****f* Component Library: List/cl_list_insert_array_head
* NAME
*	cl_list_insert_array_head
*
* DESCRIPTION:
*	The cl_list_insert_array_head function inserts an array of objects
*	at the head of a list.
*
* SYNOPSIS
*/
cl_status_t
cl_list_insert_array_head(IN cl_list_t * const p_list,
			  IN const void *const p_array,
			  IN uint32_t item_count, IN const uint32_t item_size);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the objects.
*
*	p_array
*		[in] Pointer to the first object in an array.
*
*	item_count
*		[in] Number of objects in the array.
*
*	item_size
*		[in] Size of the objects added to the list.  This is the stride in the
*		array from one object to the next.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* NOTES
*	Inserts all objects in the array to the head of the list, preserving the
*	ordering of the objects.  If not successful, no items are added.
*	List insertion operations are guaranteed to work for the minimum number
*	of items as specified in cl_list_init by the min_items parameter.
*
* SEE ALSO
*	List, cl_list_insert_array_tail, cl_list_insert_head, cl_list_insert_tail,
*	cl_list_insert_prev, cl_list_insert_next
*********/

/****f* Component Library: List/cl_list_insert_array_tail
* NAME
*	cl_list_insert_array_tail
*
* DESCRIPTION
*	The cl_list_insert_array_tail function inserts an array of objects
*	at the tail of a list.
*
* SYNOPSIS
*/
cl_status_t
cl_list_insert_array_tail(IN cl_list_t * const p_list,
			  IN const void *const p_array,
			  IN uint32_t item_count, IN const uint32_t item_size);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the objects.
*
*	p_array
*		[in] Pointer to the first object in an array.
*
*	item_count
*		[in] Number of objects in the array.
*
*	item_size
*		[in] Size of the objects added to the list.  This is the stride in the
*		array from one object to the next.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* NOTES
*	Inserts all objects in the array to the tail of the list, preserving the
*	ordering of the objects.  If not successful, no items are added.
*	List insertion operations are guaranteed to work for the minimum number
*	of items as specified in cl_list_init by the min_items parameter.
*
* SEE ALSO
*	List, cl_list_insert_array_head, cl_list_insert_head, cl_list_insert_tail,
*	cl_list_insert_prev, cl_list_insert_next
*********/

/****f* Component Library: List/cl_list_insert_next
* NAME
*	cl_list_insert_next
*
* DESCRIPTION
*	The cl_list_insert_next function inserts an object in a list after
*	the object associated with a given iterator.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_list_insert_next(IN cl_list_t * const p_list,
		    IN cl_list_iterator_t iterator,
		    IN const void *const p_object)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* Get a list item to add to the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qpool_get(&p_list->list_item_pool);
	if (!p_pool_obj)
		return (CL_INSUFFICIENT_MEMORY);

	p_pool_obj->p_object = p_object;
	cl_qlist_insert_next(&p_list->list, (cl_list_item_t *) iterator,
			     &p_pool_obj->pool_item.list_item);
	return (CL_SUCCESS);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the object.
*
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev.
*
*	p_object
*		[in] Pointer to an object to insert into the list.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* SEE ALSO
*	List, cl_list_insert_prev, cl_list_insert_head, cl_list_insert_tail,
*	cl_list_insert_array_head, cl_list_insert_array_tail
*********/

/****f* Component Library: List/cl_list_insert_prev
* NAME
*	cl_list_insert_prev
*
* DESCRIPTION
*	The cl_list_insert_prev function inserts an object in a list before
*	the object associated with a given iterator.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_list_insert_prev(IN cl_list_t * const p_list,
		    IN cl_list_iterator_t iterator,
		    IN const void *const p_object)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* Get a list item to add to the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qpool_get(&p_list->list_item_pool);
	if (!p_pool_obj)
		return (CL_INSUFFICIENT_MEMORY);

	p_pool_obj->p_object = p_object;
	cl_qlist_insert_prev(&p_list->list, (cl_list_item_t *) iterator,
			     &p_pool_obj->pool_item.list_item);
	return (CL_SUCCESS);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure into which to insert the object.
*
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev.
*
*	p_object
*		[in] Pointer to an object to insert into the list.
*
* RETURN VALUES
*	CL_SUCCESS if the insertion was successful.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory for the insertion.
*
* SEE ALSO
*	List, cl_list_insert_next, cl_list_insert_head, cl_list_insert_tail,
*	cl_list_insert_array_head, cl_list_insert_array_tail
*********/

/****f* Component Library: List/cl_list_remove_head
* NAME
*	cl_list_remove_head
*
* DESCRIPTION
*	The cl_list_remove_head function removes an object from the head of a list.
*
* SYNOPSIS
*/
static inline void *cl_list_remove_head(IN cl_list_t * const p_list)
{
	cl_pool_obj_t *p_pool_obj;
	void *p_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* See if the list is empty. */
	if (cl_is_qlist_empty(&p_list->list))
		return (NULL);

	/* Get the item at the head of the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qlist_remove_head(&p_list->list);

	p_obj = (void *)p_pool_obj->p_object;
	/* Place the pool item back into the pool. */
	cl_qpool_put(&p_list->list_item_pool, &p_pool_obj->pool_item);

	return (p_obj);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure from which to remove an object.
*
* RETURN VALUES
*	Returns the pointer to the object formerly at the head of the list.
*
*	NULL if the list was empty.
*
* SEE ALSO
*	List, cl_list_remove_tail, cl_list_remove_all, cl_list_remove_object,
*	cl_list_remove_item, cl_list_insert_head
*********/

/****f* Component Library: List/cl_list_remove_tail
* NAME
*	cl_list_remove_tail
*
* DESCRIPTION
*	The cl_list_remove_tail function removes an object from the tail of a list.
*
* SYNOPSIS
*/
static inline void *cl_list_remove_tail(IN cl_list_t * const p_list)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* See if the list is empty. */
	if (cl_is_qlist_empty(&p_list->list))
		return (NULL);

	/* Get the item at the head of the list. */
	p_pool_obj = (cl_pool_obj_t *) cl_qlist_remove_tail(&p_list->list);

	/* Place the list item back into the pool. */
	cl_qpool_put(&p_list->list_item_pool, &p_pool_obj->pool_item);

	return ((void *)p_pool_obj->p_object);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure from which to remove an object.
*
* RETURN VALUES
*	Returns the pointer to the object formerly at the tail of the list.
*
*	NULL if the list was empty.
*
* SEE ALSO
*	List, cl_list_remove_head, cl_list_remove_all, cl_list_remove_object,
*	cl_list_remove_item, cl_list_insert_head
*********/

/****f* Component Library: List/cl_list_remove_all
* NAME
*	cl_list_remove_all
*
* DESCRIPTION
*	The cl_list_remove_all function removes all objects from a list,
*	leaving it empty.
*
* SYNOPSIS
*/
static inline void cl_list_remove_all(IN cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* Return all the list items to the pool. */
	cl_qpool_put_list(&p_list->list_item_pool, &p_list->list);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure from which to remove all objects.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	List, cl_list_remove_head, cl_list_remove_tail, cl_list_remove_object,
*	cl_list_remove_item
*********/

/****f* Component Library: List/cl_list_remove_object
* NAME
*	cl_list_remove_object
*
* DESCRIPTION
*	The cl_list_remove_object function removes a specific object from a list.
*
* SYNOPSIS
*/
cl_status_t
cl_list_remove_object(IN cl_list_t * const p_list,
		      IN const void *const p_object);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure from which to remove the object.
*
*	p_object
*		[in] Pointer to an object to remove from the list.
*
* RETURN VALUES
*	CL_SUCCESS if the object was removed.
*
*	CL_NOT_FOUND if the object was not found in the list.
*
* NOTES
*	Removes the first occurrence of an object from a list.
*
* SEE ALSO
*	List, cl_list_remove_item, cl_list_remove_head, cl_list_remove_tail,
*	cl_list_remove_all
*********/

/****f* Component Library: List/cl_list_remove_item
* NAME
*	cl_list_remove_item
*
* DESCRIPTION
*	The cl_list_remove_item function removes an object from the head of a list.
*
* SYNOPSIS
*/
static inline void
cl_list_remove_item(IN cl_list_t * const p_list, IN cl_list_iterator_t iterator)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	cl_qlist_remove_item(&p_list->list, (cl_list_item_t *) iterator);

	/* Place the list item back into the pool. */
	cl_qpool_put(&p_list->list_item_pool, (cl_pool_item_t *) iterator);
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure from which to remove the item.
*
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	List, cl_list_remove_object, cl_list_remove_head, cl_list_remove_tail,
*	cl_list_remove_all
*********/

/****f* Component Library: List/cl_is_object_in_list
* NAME
*	cl_is_object_in_list
*
* DESCRIPTION
*	The cl_is_object_in_list function returns whether an object
*	is stored in a list.
*
* SYNOPSIS
*/
boolean_t
cl_is_object_in_list(IN const cl_list_t * const p_list,
		     IN const void *const p_object);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure in which to look for the object.
*
*	p_object
*		[in] Pointer to an object stored in a list.
*
* RETURN VALUES
*	TRUE if p_object was found in the list.
*
*	FALSE otherwise.
*
* SEE ALSO
*	List
*********/

/****f* Component Library: List/cl_list_end
* NAME
*	cl_list_end
*
* DESCRIPTION
*	The cl_list_end function returns returns the list iterator for
*	the end of a list.
*
* SYNOPSIS
*/
static inline cl_list_iterator_t cl_list_end(IN const cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	return (cl_qlist_end(&p_list->list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure for which the iterator for the
*		object at the head is to be returned.
*
* RETURN VALUE
*	cl_list_iterator_t for the end of the list.
*
* NOTES
*	Use cl_list_obj to retrieve the object associated with the
*	returned cl_list_iterator_t.
*
* SEE ALSO
*	List, cl_list_head, cl_list_tail, cl_list_next, cl_list_prev,
*	cl_list_obj
*********/

/****f* Component Library: List/cl_list_head
* NAME
*	cl_list_head
*
* DESCRIPTION
*	The cl_list_head function returns returns a list iterator for
*	the head of a list.
*
* SYNOPSIS
*/
static inline cl_list_iterator_t cl_list_head(IN const cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	return (cl_qlist_head(&p_list->list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure for which the iterator for the
*		object at the head is to be returned.
*
* RETURN VALUES
*	cl_list_iterator_t for the head of the list.
*
*	cl_list_iterator_t for the end of the list if the list is empty.
*
* NOTES
*	Use cl_list_obj to retrieve the object associated with the
*	returned cl_list_iterator_t.
*
* SEE ALSO
*	List, cl_list_tail, cl_list_next, cl_list_prev, cl_list_end,
*	cl_list_obj
*********/

/****f* Component Library: List/cl_list_tail
* NAME
*	cl_list_tail
*
* DESCRIPTION
*	The cl_list_tail function returns returns a list iterator for
*	the tail of a list.
*
* SYNOPSIS
*/
static inline cl_list_iterator_t cl_list_tail(IN const cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	return (cl_qlist_tail(&p_list->list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure for which the iterator for the
*		object at the tail is to be returned.
*
* RETURN VALUES
*	cl_list_iterator_t for the tail of the list.
*
*	cl_list_iterator_t for the end of the list if the list is empty.
*
* NOTES
*	Use cl_list_obj to retrieve the object associated with the
*
*	returned cl_list_iterator_t.
*
* SEE ALSO
*	List, cl_list_head, cl_list_next, cl_list_prev, cl_list_end,
*	cl_list_obj
*********/

/****f* Component Library: List/cl_list_next
* NAME
*	cl_list_next
*
* DESCRIPTION
*	The cl_list_next function returns a list iterator for the object stored
*	in a list after the object associated with a given list iterator.
*
* SYNOPSIS
*/
static inline cl_list_iterator_t cl_list_next(IN cl_list_iterator_t iterator)
{
	CL_ASSERT(iterator);

	return (cl_qlist_next(iterator));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure for which the iterator for the
*		next object is to be returned.
*
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev.
*
* RETURN VALUES
*	cl_list_iterator_t for the object following the object associated with
*	the list iterator specified by the iterator parameter.
*
*	cl_list_iterator_t for the end of the list if the list is empty.
*
* NOTES
*	Use cl_list_obj to retrieve the object associated with the
*	returned cl_list_iterator_t.
*
* SEE ALSO
*	List, cl_list_prev, cl_list_head, cl_list_tail, cl_list_end,
*	cl_list_obj
*********/

/****f* Component Library: List/cl_list_prev
* NAME
*	cl_list_prev
*
* DESCRIPTION
*	The cl_list_prev function returns a list iterator for the object stored
*	in a list before the object associated with a given list iterator.
*
* SYNOPSIS
*/
static inline cl_list_iterator_t cl_list_prev(IN cl_list_iterator_t iterator)
{
	CL_ASSERT(iterator);

	return (cl_qlist_prev(iterator));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure for which the iterator for the
*		next object is to be returned.
*
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev.
*
* RETURN VALUES
*	cl_list_iterator_t for the object preceding the object associated with
*	the list iterator specified by the iterator parameter.
*
*	cl_list_iterator_t for the end of the list if the list is empty.
*
* NOTES
*	Use cl_list_obj to retrieve the object associated with the
*	returned cl_list_iterator_t.
*
* SEE ALSO
*	List, cl_list_next, cl_list_head, cl_list_tail, cl_list_end,
*	cl_list_obj
*********/

/****f* Component Library: List/cl_list_obj
* NAME
*	cl_list_obj
*
* DESCRIPTION
*	The cl_list_obj function returns the object associated
*	with a list iterator.
*
* SYNOPSIS
*/
static inline void *cl_list_obj(IN cl_list_iterator_t iterator)
{
	CL_ASSERT(iterator);

	return ((void *)((cl_pool_obj_t *) iterator)->p_object);
}

/*
* PARAMETERS
*	iterator
*		[in] cl_list_iterator_t returned by a previous call to cl_list_head,
*		cl_list_tail, cl_list_next, or cl_list_prev whose object is requested.
*
* RETURN VALUE
*	Pointer to the object associated with the list iterator specified
*	by the iterator parameter.
*
* SEE ALSO
*	List, cl_list_head, cl_list_tail, cl_list_next, cl_list_prev
*********/

/****f* Component Library: List/cl_list_find_from_head
* NAME
*	cl_list_find_from_head
*
* DESCRIPTION
*	The cl_list_find_from_head function uses a specified function
*	to search for an object starting from the head of a list.
*
* SYNOPSIS
*/
cl_list_iterator_t
cl_list_find_from_head(IN const cl_list_t * const p_list,
		       IN cl_pfn_list_find_t pfn_func,
		       IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure to search.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_list_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	Returns the iterator for the object if found.
*
*	Returns the iterator for the list end otherwise.
*
* NOTES
*	cl_list_find_from_head does not remove the found object from
*	the list.  The iterator for the object is returned when the function
*	provided by the pfn_func parameter returns CL_SUCCESS.  The function
*	specified by the pfn_func parameter must not perform any list
*	operations as these would corrupt the list.
*
* SEE ALSO
*	List, cl_list_find_from_tail, cl_list_apply_func_t,
*	cl_pfn_list_find_t
*********/

/****f* Component Library: List/cl_list_find_from_tail
* NAME
*	cl_list_find_from_tail
*
* DESCRIPTION
*	The cl_list_find_from_tail function uses a specified function
*	to search for an object starting from the tail of a list.
*
* SYNOPSIS
*/
cl_list_iterator_t
cl_list_find_from_tail(IN const cl_list_t * const p_list,
		       IN cl_pfn_list_find_t pfn_func,
		       IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure to search.
*
*	pfn_func
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_list_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	Returns the iterator for the object if found.
*
*	Returns the iterator for the list end otherwise.
*
* NOTES
*	cl_list_find_from_tail does not remove the found object from
*	the list.  The iterator for the object is returned when the function
*	provided by the pfn_func parameter returns CL_SUCCESS.  The function
*	specified by the pfn_func parameter must not perform any list
*	operations as these would corrupt the list.
*
* SEE ALSO
*	List, cl_list_find_from_head, cl_list_apply_func_t,
*	cl_pfn_list_find_t
*********/

/****f* Component Library: List/cl_list_apply_func
* NAME
*	cl_list_apply_func
*
* DESCRIPTION
*	The cl_list_apply_func function executes a specified function for every
*	object stored in a list.
*
* SYNOPSIS
*/
void
cl_list_apply_func(IN const cl_list_t * const p_list,
		   IN cl_pfn_list_apply_t pfn_func,
		   IN const void *const context);
/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure to iterate.
*
*	pfn_func
*		[in] Function invoked for every item in a list.
*		See the cl_pfn_list_apply_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_list_apply_func invokes the specified callback function for every
*	object stored in the list, starting from the head.  The function specified
*	by the pfn_func parameter must not perform any list operations as these
*	would corrupt the list.
*
* SEE ALSO
*	List, cl_list_find_from_head, cl_list_find_from_tail,
*	cl_pfn_list_apply_t
*********/

/****f* Component Library: List/cl_list_count
* NAME
*	cl_list_count
*
* DESCRIPTION
*	The cl_list_count function returns the number of objects stored in a list.
*
* SYNOPSIS
*/
static inline size_t cl_list_count(IN const cl_list_t * const p_list)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	return (cl_qlist_count(&p_list->list));
}

/*
* PARAMETERS
*	p_list
*		[in] Pointer to a cl_list_t structure whose object to count.
*
* RETURN VALUES
*	Number of objects stored in the specified list.
*
* SEE ALSO
*	List
*********/

END_C_DECLS
#endif				/* _CL_LIST_H_ */
