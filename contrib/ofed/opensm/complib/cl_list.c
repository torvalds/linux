/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2009 Mellanox Technologies LTD. All rights reserved.
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
 *	Implementation of quick list, and list.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <complib/cl_qlist.h>
#include <complib/cl_list.h>

#define FREE_ITEM_GROW_SIZE		10

/******************************************************************************
 IMPLEMENTATION OF QUICK LIST
******************************************************************************/
void cl_qlist_insert_array_head(IN cl_qlist_t * const p_list,
				IN cl_list_item_t * const p_array,
				IN uint32_t item_count,
				IN const uint32_t item_size)
{
	cl_list_item_t *p_item;

	CL_ASSERT(p_list);
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	CL_ASSERT(p_array);
	CL_ASSERT(item_size >= sizeof(cl_list_item_t));
	CL_ASSERT(item_count);

	/*
	 * To add items from the array to the list in the same order as
	 * the elements appear in the array, we add them starting with
	 * the last one first.  Locate the last item.
	 */
	p_item = (cl_list_item_t *) ((uint8_t *) p_array +
				     (item_size * (item_count - 1)));

	/* Continue to add all items to the list. */
	while (item_count--) {
		cl_qlist_insert_head(p_list, p_item);

		/* Get the next object to add to the list. */
		p_item = (cl_list_item_t *) ((uint8_t *) p_item - item_size);
	}
}

void cl_qlist_insert_array_tail(IN cl_qlist_t * const p_list,
				IN cl_list_item_t * const p_array,
				IN uint32_t item_count,
				IN const uint32_t item_size)
{
	cl_list_item_t *p_item;

	CL_ASSERT(p_list);
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	CL_ASSERT(p_array);
	CL_ASSERT(item_size >= sizeof(cl_list_item_t));
	CL_ASSERT(item_count);

	/* Set the first item to add to the list. */
	p_item = p_array;

	/* Continue to add all items to the list. */
	while (item_count--) {
		cl_qlist_insert_tail(p_list, p_item);

		/* Get the next object to add to the list. */
		p_item = (cl_list_item_t *) ((uint8_t *) p_item + item_size);
	}
}

void cl_qlist_insert_list_head(IN cl_qlist_t * const p_dest_list,
			       IN cl_qlist_t * const p_src_list)
{
#if defined( _DEBUG_ )
	cl_list_item_t *p_item;
#endif

	CL_ASSERT(p_dest_list);
	CL_ASSERT(p_src_list);
	CL_ASSERT(p_dest_list->state == CL_INITIALIZED);
	CL_ASSERT(p_src_list->state == CL_INITIALIZED);

	/*
	 * Is the src list empty?
	 * We must have this check here for code below to work.
	 */
	if (cl_is_qlist_empty(p_src_list))
		return;

#if defined( _DEBUG_ )
	/* Check that all items in the source list belong there. */
	p_item = cl_qlist_head(p_src_list);
	while (p_item != cl_qlist_end(p_src_list)) {
		/* All list items in the source list must point to it. */
		CL_ASSERT(p_item->p_list == p_src_list);
		/* Point them all to the destination list. */
		p_item->p_list = p_dest_list;
		p_item = cl_qlist_next(p_item);
	}
#endif

	/* Chain the destination list to the tail of the source list. */
	cl_qlist_tail(p_src_list)->p_next = cl_qlist_head(p_dest_list);
	cl_qlist_head(p_dest_list)->p_prev = cl_qlist_tail(p_src_list);

	/*
	 * Update the head of the destination list to the head of
	 * the source list.
	 */
	p_dest_list->end.p_next = cl_qlist_head(p_src_list);
	cl_qlist_head(p_src_list)->p_prev = &p_dest_list->end;

	/*
	 * Update the count of the destination to reflect the source items having
	 * been added.
	 */
	p_dest_list->count += p_src_list->count;

	/* Update source list to reflect being empty. */
	__cl_qlist_reset(p_src_list);
}

void cl_qlist_insert_list_tail(IN cl_qlist_t * const p_dest_list,
			       IN cl_qlist_t * const p_src_list)
{
#if defined( _DEBUG_ )
	cl_list_item_t *p_item;
#endif

	CL_ASSERT(p_dest_list);
	CL_ASSERT(p_src_list);
	CL_ASSERT(p_dest_list->state == CL_INITIALIZED);
	CL_ASSERT(p_src_list->state == CL_INITIALIZED);

	/*
	 * Is the src list empty?
	 * We must have this check here for code below to work.
	 */
	if (cl_is_qlist_empty(p_src_list))
		return;

#if defined( _DEBUG_ )
	/* Check that all items in the source list belong there. */
	p_item = cl_qlist_head(p_src_list);
	while (p_item != cl_qlist_end(p_src_list)) {
		/* All list items in the source list must point to it. */
		CL_ASSERT(p_item->p_list == p_src_list);
		/* Point them all to the destination list. */
		p_item->p_list = p_dest_list;
		p_item = cl_qlist_next(p_item);
	}
#endif

	/* Chain the source list to the tail of the destination list. */
	cl_qlist_tail(p_dest_list)->p_next = cl_qlist_head(p_src_list);
	cl_qlist_head(p_src_list)->p_prev = cl_qlist_tail(p_dest_list);

	/*
	 * Update the tail of the destination list to the tail of
	 * the source list.
	 */
	p_dest_list->end.p_prev = cl_qlist_tail(p_src_list);
	cl_qlist_tail(p_src_list)->p_next = &p_dest_list->end;

	/*
	 * Update the count of the destination to reflect the source items having
	 * been added.
	 */
	p_dest_list->count += p_src_list->count;

	/* Update source list to reflect being empty. */
	__cl_qlist_reset(p_src_list);
}

boolean_t cl_is_item_in_qlist(IN const cl_qlist_t * const p_list,
			      IN const cl_list_item_t * const p_list_item)
{
	const cl_list_item_t *p_temp;

	CL_ASSERT(p_list);
	CL_ASSERT(p_list_item);
	CL_ASSERT(p_list->state == CL_INITIALIZED);

	/* Traverse looking for a match */
	p_temp = cl_qlist_head(p_list);
	while (p_temp != cl_qlist_end(p_list)) {
		if (p_temp == p_list_item) {
			CL_ASSERT(p_list_item->p_list == p_list);
			return (TRUE);
		}

		p_temp = cl_qlist_next(p_temp);
	}

	return (FALSE);
}

cl_list_item_t *cl_qlist_find_next(IN const cl_qlist_t * const p_list,
				   IN const cl_list_item_t * const p_list_item,
				   IN cl_pfn_qlist_find_t pfn_func,
				   IN const void *const context)
{
	cl_list_item_t *p_found_item;

	CL_ASSERT(p_list);
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	CL_ASSERT(p_list_item);
	CL_ASSERT(p_list_item->p_list == p_list);
	CL_ASSERT(pfn_func);

	p_found_item = cl_qlist_next(p_list_item);

	/* The user provided a compare function */
	while (p_found_item != cl_qlist_end(p_list)) {
		CL_ASSERT(p_found_item->p_list == p_list);

		if (pfn_func(p_found_item, (void *)context) == CL_SUCCESS)
			break;

		p_found_item = cl_qlist_next(p_found_item);
	}

	/* No match */
	return (p_found_item);
}

cl_list_item_t *cl_qlist_find_prev(IN const cl_qlist_t * const p_list,
				   IN const cl_list_item_t * const p_list_item,
				   IN cl_pfn_qlist_find_t pfn_func,
				   IN const void *const context)
{
	cl_list_item_t *p_found_item;

	CL_ASSERT(p_list);
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	CL_ASSERT(p_list_item);
	CL_ASSERT(p_list_item->p_list == p_list);
	CL_ASSERT(pfn_func);

	p_found_item = cl_qlist_prev(p_list_item);

	/* The user provided a compare function */
	while (p_found_item != cl_qlist_end(p_list)) {
		CL_ASSERT(p_found_item->p_list == p_list);

		if (pfn_func(p_found_item, (void *)context) == CL_SUCCESS)
			break;

		p_found_item = cl_qlist_prev(p_found_item);
	}

	/* No match */
	return (p_found_item);
}

void cl_qlist_apply_func(IN const cl_qlist_t * const p_list,
			 IN cl_pfn_qlist_apply_t pfn_func,
			 IN const void *const context)
{
	cl_list_item_t *p_list_item;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_list);
	CL_ASSERT(p_list->state == CL_INITIALIZED);
	CL_ASSERT(pfn_func);

	p_list_item = cl_qlist_head(p_list);
	while (p_list_item != cl_qlist_end(p_list)) {
		pfn_func(p_list_item, (void *)context);
		p_list_item = cl_qlist_next(p_list_item);
	}
}

void cl_qlist_move_items(IN cl_qlist_t * const p_src_list,
			 IN cl_qlist_t * const p_dest_list,
			 IN cl_pfn_qlist_find_t pfn_func,
			 IN const void *const context)
{
	cl_list_item_t *p_current_item, *p_next;

	CL_ASSERT(p_src_list);
	CL_ASSERT(p_dest_list);
	CL_ASSERT(p_src_list->state == CL_INITIALIZED);
	CL_ASSERT(p_dest_list->state == CL_INITIALIZED);
	CL_ASSERT(pfn_func);

	p_current_item = cl_qlist_head(p_src_list);

	while (p_current_item != cl_qlist_end(p_src_list)) {
		/* Before we do anything, get a pointer to the next item. */
		p_next = cl_qlist_next(p_current_item);

		if (pfn_func(p_current_item, (void *)context) == CL_SUCCESS) {
			/* Move the item from one list to the other. */
			cl_qlist_remove_item(p_src_list, p_current_item);
			cl_qlist_insert_tail(p_dest_list, p_current_item);
		}
		p_current_item = p_next;
	}
}

/******************************************************************************
 IMPLEMENTATION OF LIST
******************************************************************************/
void cl_list_construct(IN cl_list_t * const p_list)
{
	CL_ASSERT(p_list);

	cl_qpool_construct(&p_list->list_item_pool);
}

cl_status_t cl_list_init(IN cl_list_t * const p_list, IN const size_t min_items)
{
	uint32_t grow_size;

	CL_ASSERT(p_list);
	cl_qlist_init(&p_list->list);

	/*
	 * We will grow by min_items/8 items at a time, with a minimum of
	 * FREE_ITEM_GROW_SIZE.
	 */
	grow_size = (uint32_t) min_items >> 3;
	if (grow_size < FREE_ITEM_GROW_SIZE)
		grow_size = FREE_ITEM_GROW_SIZE;

	/* Initialize the pool of list items. */
	return (cl_qpool_init(&p_list->list_item_pool, min_items, 0, grow_size,
			      sizeof(cl_pool_obj_t), NULL, NULL, NULL));
}

void cl_list_destroy(IN cl_list_t * const p_list)
{
	CL_ASSERT(p_list);

	cl_qpool_destroy(&p_list->list_item_pool);
}

static cl_status_t cl_list_find_cb(IN const cl_list_item_t * const p_list_item,
				   IN void *const context)
{
	CL_ASSERT(p_list_item);

	if (cl_list_obj(p_list_item) == context)
		return (CL_SUCCESS);

	return (CL_NOT_FOUND);
}

cl_status_t cl_list_remove_object(IN cl_list_t * const p_list,
				  IN const void *const p_object)
{
	cl_list_item_t *p_list_item;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	/* find the item in question */
	p_list_item =
	    cl_qlist_find_from_head(&p_list->list, cl_list_find_cb, p_object);
	if (p_list_item != cl_qlist_end(&p_list->list)) {
		/* remove this item */
		cl_qlist_remove_item(&p_list->list, p_list_item);
		cl_qpool_put(&p_list->list_item_pool,
			     (cl_pool_item_t *) p_list_item);
		return (CL_SUCCESS);
	}
	return (CL_NOT_FOUND);
}

boolean_t cl_is_object_in_list(IN const cl_list_t * const p_list,
			       IN const void *const p_object)
{
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));

	return (cl_qlist_find_from_head
		(&p_list->list, cl_list_find_cb, p_object)
		!= cl_qlist_end(&p_list->list));
}

cl_status_t cl_list_insert_array_head(IN cl_list_t * const p_list,
				      IN const void *const p_array,
				      IN uint32_t item_count,
				      IN const uint32_t item_size)
{
	cl_status_t status;
	void *p_object;
	uint32_t items_remain = item_count;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	CL_ASSERT(p_array);
	CL_ASSERT(item_size);
	CL_ASSERT(item_count);

	/*
	 * To add items from the array to the list in the same order as
	 * the elements appear in the array, we add them starting with
	 * the last one first.  Locate the last item.
	 */
	p_object = ((uint8_t *) p_array + (item_size * (item_count - 1)));

	/* Continue to add all items to the list. */
	while (items_remain--) {
		status = cl_list_insert_head(p_list, p_object);
		if (status != CL_SUCCESS) {
			/* Remove all items that have been inserted. */
			while (items_remain++ < (item_count - 1))
				cl_list_remove_head(p_list);
			return (status);
		}

		/* Get the next object to add to the list. */
		p_object = ((uint8_t *) p_object - item_size);
	}

	return (CL_SUCCESS);
}

cl_status_t cl_list_insert_array_tail(IN cl_list_t * const p_list,
				      IN const void *const p_array,
				      IN uint32_t item_count,
				      IN const uint32_t item_size)
{
	cl_status_t status;
	void *p_object;
	uint32_t items_remain = item_count;

	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	CL_ASSERT(p_array);
	CL_ASSERT(item_size);
	CL_ASSERT(item_count);

	/* Set the first item to add to the list. */
	p_object = (void *)p_array;

	/* Continue to add all items to the list. */
	while (items_remain--) {
		status = cl_list_insert_tail(p_list, p_object);
		if (status != CL_SUCCESS) {
			/* Remove all items that have been inserted. */
			while (items_remain++ < (item_count - 1))
				cl_list_remove_tail(p_list);
			return (status);
		}

		/* Get the next object to add to the list. */
		p_object = ((uint8_t *) p_object + item_size);
	}

	return (CL_SUCCESS);
}

cl_list_iterator_t cl_list_find_from_head(IN const cl_list_t * const p_list,
					  IN cl_pfn_list_find_t pfn_func,
					  IN const void *const context)
{
	cl_status_t status;
	cl_list_iterator_t itor;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	CL_ASSERT(pfn_func);

	itor = cl_list_head(p_list);

	while (itor != cl_list_end(p_list)) {
		status = pfn_func(cl_list_obj(itor), (void *)context);
		if (status == CL_SUCCESS)
			break;

		itor = cl_list_next(itor);
	}

	/* no match */
	return (itor);
}

cl_list_iterator_t cl_list_find_from_tail(IN const cl_list_t * const p_list,
					  IN cl_pfn_list_find_t pfn_func,
					  IN const void *const context)
{
	cl_status_t status;
	cl_list_iterator_t itor;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	CL_ASSERT(pfn_func);

	itor = cl_list_tail(p_list);

	while (itor != cl_list_end(p_list)) {
		status = pfn_func(cl_list_obj(itor), (void *)context);
		if (status == CL_SUCCESS)
			break;

		itor = cl_list_prev(itor);
	}

	/* no match */
	return (itor);
}

void cl_list_apply_func(IN const cl_list_t * const p_list,
			IN cl_pfn_list_apply_t pfn_func,
			IN const void *const context)
{
	cl_list_iterator_t itor;

	/* Note that context can have any arbitrary value. */
	CL_ASSERT(p_list);
	CL_ASSERT(cl_is_qpool_inited(&p_list->list_item_pool));
	CL_ASSERT(pfn_func);

	itor = cl_list_head(p_list);

	while (itor != cl_list_end(p_list)) {
		pfn_func(cl_list_obj(itor), (void *)context);

		itor = cl_list_next(itor);
	}
}
