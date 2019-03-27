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
 *	Implementation of the grow pools.  The grow pools manage a pool of objects.
 *	The pools can grow to meet demand, limited only by system memory.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_qcomppool.h>
#include <complib/cl_comppool.h>
#include <complib/cl_qpool.h>
#include <complib/cl_pool.h>
#include <complib/cl_math.h>

/*
 * IMPLEMENTATION OF QUICK COMPOSITE POOL
 */
void cl_qcpool_construct(IN cl_qcpool_t * const p_pool)
{
	CL_ASSERT(p_pool);

	memset(p_pool, 0, sizeof(cl_qcpool_t));

	p_pool->state = CL_UNINITIALIZED;
}

cl_status_t cl_qcpool_init(IN cl_qcpool_t * const p_pool,
			   IN const size_t min_size, IN const size_t max_size,
			   IN const size_t grow_size,
			   IN const size_t * const component_sizes,
			   IN const uint32_t num_components,
			   IN cl_pfn_qcpool_init_t pfn_initializer OPTIONAL,
			   IN cl_pfn_qcpool_dtor_t pfn_destructor OPTIONAL,
			   IN const void *const context)
{
	cl_status_t status;
	uint32_t i;

	CL_ASSERT(p_pool);
	/* Must have a minimum of 1 component. */
	CL_ASSERT(num_components);
	/* A component size array is required. */
	CL_ASSERT(component_sizes);
	/*
	 * If no initializer is provided, the first component must be large
	 * enough to hold a pool item.
	 */
	CL_ASSERT(pfn_initializer ||
		  (component_sizes[0] >= sizeof(cl_pool_item_t)));

	cl_qcpool_construct(p_pool);

	if (num_components > 1 && !pfn_initializer)
		return (CL_INVALID_SETTING);

	if (max_size && max_size < min_size)
		return (CL_INVALID_SETTING);

	/*
	 * Allocate the array of component sizes and component pointers all
	 * in one allocation.
	 */
	p_pool->component_sizes = (size_t *) malloc((sizeof(size_t) +
						     sizeof(void *)) *
						    num_components);

	if (!p_pool->component_sizes)
		return (CL_INSUFFICIENT_MEMORY);
	else
		memset(p_pool->component_sizes, 0,
		       (sizeof(size_t) + sizeof(void *)) * num_components);

	/* Calculate the pointer to the array of pointers, used for callbacks. */
	p_pool->p_components =
	    (void **)(p_pool->component_sizes + num_components);

	/* Copy the user's sizes into our array for future use. */
	memcpy(p_pool->component_sizes, component_sizes,
	       sizeof(component_sizes[0]) * num_components);

	/* Store the number of components per object. */
	p_pool->num_components = num_components;

	/* Round up and store the size of the components. */
	for (i = 0; i < num_components; i++) {
		/*
		 * We roundup each component size so that all components
		 * are aligned on a natural boundary.
		 */
		p_pool->component_sizes[i] =
		    ROUNDUP(p_pool->component_sizes[i], sizeof(uintptr_t));
	}

	p_pool->max_objects = max_size ? max_size : ~(size_t) 0;
	p_pool->grow_size = grow_size;

	/* Store callback function pointers. */
	p_pool->pfn_init = pfn_initializer;	/* may be NULL */
	p_pool->pfn_dtor = pfn_destructor;	/* may be NULL */
	p_pool->context = context;

	cl_qlist_init(&p_pool->alloc_list);

	cl_qlist_init(&p_pool->free_list);

	/*
	 * We are now initialized.  We change the initialized flag before
	 * growing since the grow function asserts that we are initialized.
	 */
	p_pool->state = CL_INITIALIZED;

	/* Allocate the minimum number of objects as requested. */
	if (!min_size)
		return (CL_SUCCESS);

	status = cl_qcpool_grow(p_pool, min_size);
	/* Trap for error and cleanup if necessary. */
	if (status != CL_SUCCESS)
		cl_qcpool_destroy(p_pool);

	return (status);
}

void cl_qcpool_destroy(IN cl_qcpool_t * const p_pool)
{
	/* CL_ASSERT that a non-NULL pointer was provided. */
	CL_ASSERT(p_pool);
	/* CL_ASSERT that we are in a valid state (not uninitialized memory). */
	CL_ASSERT(cl_is_state_valid(p_pool->state));

	if (p_pool->state == CL_INITIALIZED) {
		/*
		 * Assert if the user hasn't put everything back in the pool
		 * before destroying it
		 * if they haven't, then most likely they are still using memory
		 * that will be freed, and the destructor will not be called!
		 */
#ifdef _DEBUG_
		/* but we do not want "free" version to assert on this one */
		CL_ASSERT(cl_qcpool_count(p_pool) == p_pool->num_objects);
#endif
		/* call the user's destructor for each object in the pool */
		if (p_pool->pfn_dtor) {
			while (!cl_is_qlist_empty(&p_pool->free_list)) {
				p_pool->pfn_dtor((cl_pool_item_t *)
						 cl_qlist_remove_head(&p_pool->
								      free_list),
						 (void *)p_pool->context);
			}
		} else {
			cl_qlist_remove_all(&p_pool->free_list);
		}

		/* Free all allocated memory blocks. */
		while (!cl_is_qlist_empty(&p_pool->alloc_list))
			free(cl_qlist_remove_head(&p_pool->alloc_list));

		if (p_pool->component_sizes) {
			free(p_pool->component_sizes);
			p_pool->component_sizes = NULL;
		}
	}

	p_pool->state = CL_UNINITIALIZED;
}

cl_status_t cl_qcpool_grow(IN cl_qcpool_t * const p_pool, IN size_t obj_count)
{
	cl_status_t status = CL_SUCCESS;
	uint8_t *p_objects;
	cl_pool_item_t *p_pool_item;
	uint32_t i;
	size_t obj_size;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);
	CL_ASSERT(obj_count);

	/* Validate that growth is possible. */
	if (p_pool->num_objects == p_pool->max_objects)
		return (CL_INSUFFICIENT_MEMORY);

	/* Cap the growth to the desired maximum. */
	if (obj_count > (p_pool->max_objects - p_pool->num_objects))
		obj_count = p_pool->max_objects - p_pool->num_objects;

	/* Calculate the size of an object. */
	obj_size = 0;
	for (i = 0; i < p_pool->num_components; i++)
		obj_size += p_pool->component_sizes[i];

	/* Allocate the buffer for the new objects. */
	p_objects = (uint8_t *)
	    malloc(sizeof(cl_list_item_t) + (obj_size * obj_count));

	/* Make sure the allocation succeeded. */
	if (!p_objects)
		return (CL_INSUFFICIENT_MEMORY);
	else
		memset(p_objects, 0,
		       sizeof(cl_list_item_t) + (obj_size * obj_count));

	/* Insert the allocation in our list. */
	cl_qlist_insert_tail(&p_pool->alloc_list, (cl_list_item_t *) p_objects);
	p_objects += sizeof(cl_list_item_t);

	/* initialize the new elements and add them to the free list */
	while (obj_count--) {
		/* Setup the array of components for the current object. */
		p_pool->p_components[0] = p_objects;
		for (i = 1; i < p_pool->num_components; i++) {
			/* Calculate the pointer to the next component. */
			p_pool->p_components[i] =
			    (uint8_t *) p_pool->p_components[i - 1] +
			    p_pool->component_sizes[i - 1];
		}

		/*
		 * call the user's initializer
		 * this can fail!
		 */
		if (p_pool->pfn_init) {
			p_pool_item = NULL;
			status = p_pool->pfn_init(p_pool->p_components,
						  p_pool->num_components,
						  (void *)p_pool->context,
						  &p_pool_item);
			if (status != CL_SUCCESS) {
				/*
				 * User initialization failed
				 * we may have only grown the pool by some partial amount
				 * Invoke the destructor for the object that failed
				 * initialization.
				 */
				if (p_pool->pfn_dtor)
					p_pool->pfn_dtor(p_pool_item,
							 (void *)p_pool->
							 context);

				/* Return the user's status. */
				return (status);
			}
			CL_ASSERT(p_pool_item);
		} else {
			/*
			 * If no initializer is provided, assume that the pool item
			 * is stored at the beginning of the first component.
			 */
			p_pool_item =
			    (cl_pool_item_t *) p_pool->p_components[0];
		}

#ifdef _DEBUG_
		/*
		 * Set the pool item's pool pointer to this pool so that we can
		 * check that items get returned to the correct pool.
		 */
		p_pool_item->p_pool = p_pool;
#endif

		/* Insert the new item in the free list, traping for failure. */
		cl_qlist_insert_head(&p_pool->free_list,
				     &p_pool_item->list_item);

		p_pool->num_objects++;

		/* move the pointer to the next item */
		p_objects += obj_size;
	}

	return (status);
}

cl_pool_item_t *cl_qcpool_get(IN cl_qcpool_t * const p_pool)
{
	cl_list_item_t *p_list_item;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);

	if (cl_is_qlist_empty(&p_pool->free_list)) {
		/*
		 * No object is available.
		 * Return NULL if the user does not want automatic growth.
		 */
		if (!p_pool->grow_size)
			return (NULL);

		/* We ran out of elements.  Get more */
		cl_qcpool_grow(p_pool, p_pool->grow_size);
		/*
		 * We may not have gotten everything we wanted but we might have
		 * gotten something.
		 */
		if (cl_is_qlist_empty(&p_pool->free_list))
			return (NULL);
	}

	p_list_item = cl_qlist_remove_head(&p_pool->free_list);
	/* OK, at this point we have an object */
	CL_ASSERT(p_list_item != cl_qlist_end(&p_pool->free_list));
	return ((cl_pool_item_t *) p_list_item);
}

cl_pool_item_t *cl_qcpool_get_tail(IN cl_qcpool_t * const p_pool)
{
	cl_list_item_t *p_list_item;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);

	if (cl_is_qlist_empty(&p_pool->free_list)) {
		/*
		 * No object is available.
		 * Return NULL if the user does not want automatic growth.
		 */
		if (!p_pool->grow_size)
			return (NULL);

		/* We ran out of elements.  Get more */
		cl_qcpool_grow(p_pool, p_pool->grow_size);
		/*
		 * We may not have gotten everything we wanted but we might have
		 * gotten something.
		 */
		if (cl_is_qlist_empty(&p_pool->free_list))
			return (NULL);
	}

	p_list_item = cl_qlist_remove_tail(&p_pool->free_list);
	/* OK, at this point we have an object */
	CL_ASSERT(p_list_item != cl_qlist_end(&p_pool->free_list));
	return ((cl_pool_item_t *) p_list_item);
}

/*
 * IMPLEMENTATION OF QUICK GROW POOL
 */

/*
 * Callback to translate quick composite to quick grow pool
 * initializer callback.
 */
static cl_status_t __cl_qpool_init_cb(IN void **const p_comp_array,
				      IN const uint32_t num_components,
				      IN void *const context,
				      OUT cl_pool_item_t ** const pp_pool_item)
{
	cl_qpool_t *p_pool = (cl_qpool_t *) context;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->pfn_init);
	CL_ASSERT(num_components == 1);

	UNUSED_PARAM(num_components);

	return (p_pool->pfn_init(p_comp_array[0], (void *)p_pool->context,
				 pp_pool_item));
}

/*
 * Callback to translate quick composite to quick grow pool
 * destructor callback.
 */
static void __cl_qpool_dtor_cb(IN const cl_pool_item_t * const p_pool_item,
			       IN void *const context)
{
	cl_qpool_t *p_pool = (cl_qpool_t *) context;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->pfn_dtor);

	p_pool->pfn_dtor(p_pool_item, (void *)p_pool->context);
}

void cl_qpool_construct(IN cl_qpool_t * const p_pool)
{
	memset(p_pool, 0, sizeof(cl_qpool_t));

	cl_qcpool_construct(&p_pool->qcpool);
}

cl_status_t cl_qpool_init(IN cl_qpool_t * const p_pool,
			  IN const size_t min_size, IN const size_t max_size,
			  IN const size_t grow_size,
			  IN const size_t object_size,
			  IN cl_pfn_qpool_init_t pfn_initializer OPTIONAL,
			  IN cl_pfn_qpool_dtor_t pfn_destructor OPTIONAL,
			  IN const void *const context)
{
	cl_status_t status;

	CL_ASSERT(p_pool);

	p_pool->pfn_init = pfn_initializer;	/* may be NULL */
	p_pool->pfn_dtor = pfn_destructor;	/* may be NULL */
	p_pool->context = context;

	status = cl_qcpool_init(&p_pool->qcpool, min_size, max_size, grow_size,
				&object_size, 1,
				pfn_initializer ? __cl_qpool_init_cb : NULL,
				pfn_destructor ? __cl_qpool_dtor_cb : NULL,
				p_pool);

	return (status);
}

/*
 * IMPLEMENTATION OF COMPOSITE POOL
 */

/*
 * Callback to translate quick composite to compsite pool
 * initializer callback.
 */
static cl_status_t __cl_cpool_init_cb(IN void **const p_comp_array,
				      IN const uint32_t num_components,
				      IN void *const context,
				      OUT cl_pool_item_t ** const pp_pool_item)
{
	cl_cpool_t *p_pool = (cl_cpool_t *) context;
	cl_pool_obj_t *p_pool_obj;
	cl_status_t status = CL_SUCCESS;

	CL_ASSERT(p_pool);

	/*
	 * Set our pointer to the list item, which is stored at the beginning of
	 * the first component.
	 */
	p_pool_obj = (cl_pool_obj_t *) p_comp_array[0];
	/* Set the pool item pointer for the caller. */
	*pp_pool_item = &p_pool_obj->pool_item;

	/* Calculate the pointer to the user's first component. */
	p_comp_array[0] = ((uint8_t *) p_comp_array[0]) + sizeof(cl_pool_obj_t);

	/*
	 * Set the object pointer in the pool object to point to the first of the
	 * user's components.
	 */
	p_pool_obj->p_object = p_comp_array[0];

	/* Invoke the user's constructor callback. */
	if (p_pool->pfn_init) {
		status = p_pool->pfn_init(p_comp_array, num_components,
					  (void *)p_pool->context);
	}

	return (status);
}

/*
 * Callback to translate quick composite to composite pool
 * destructor callback.
 */
static void __cl_cpool_dtor_cb(IN const cl_pool_item_t * const p_pool_item,
			       IN void *const context)
{
	cl_cpool_t *p_pool = (cl_cpool_t *) context;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->pfn_dtor);
	CL_ASSERT(((cl_pool_obj_t *) p_pool_item)->p_object);

	/* Invoke the user's destructor callback. */
	p_pool->pfn_dtor((void *)((cl_pool_obj_t *) p_pool_item)->p_object,
			 (void *)p_pool->context);
}

void cl_cpool_construct(IN cl_cpool_t * const p_pool)
{
	CL_ASSERT(p_pool);

	memset(p_pool, 0, sizeof(cl_cpool_t));

	cl_qcpool_construct(&p_pool->qcpool);
}

cl_status_t cl_cpool_init(IN cl_cpool_t * const p_pool,
			  IN const size_t min_size, IN const size_t max_size,
			  IN const size_t grow_size,
			  IN size_t * const component_sizes,
			  IN const uint32_t num_components,
			  IN cl_pfn_cpool_init_t pfn_initializer OPTIONAL,
			  IN cl_pfn_cpool_dtor_t pfn_destructor OPTIONAL,
			  IN const void *const context)
{
	cl_status_t status;

	CL_ASSERT(p_pool);
	CL_ASSERT(num_components);
	CL_ASSERT(component_sizes);

	/* Add the size of the pool object to the first component. */
	component_sizes[0] += sizeof(cl_pool_obj_t);

	/* Store callback function pointers. */
	p_pool->pfn_init = pfn_initializer;	/* may be NULL */
	p_pool->pfn_dtor = pfn_destructor;	/* may be NULL */
	p_pool->context = context;

	status = cl_qcpool_init(&p_pool->qcpool, min_size, max_size, grow_size,
				component_sizes, num_components,
				__cl_cpool_init_cb,
				pfn_destructor ? __cl_cpool_dtor_cb : NULL,
				p_pool);

	/* Restore the original value of the first component. */
	component_sizes[0] -= sizeof(cl_pool_obj_t);

	return (status);
}

/*
 * IMPLEMENTATION OF GROW POOL
 */

/*
 * Callback to translate quick composite to grow pool constructor callback.
 */
static cl_status_t __cl_pool_init_cb(IN void **const pp_obj,
				     IN const uint32_t count,
				     IN void *const context,
				     OUT cl_pool_item_t ** const pp_pool_item)
{
	cl_pool_t *p_pool = (cl_pool_t *) context;
	cl_pool_obj_t *p_pool_obj;
	cl_status_t status = CL_SUCCESS;

	CL_ASSERT(p_pool);
	CL_ASSERT(pp_obj);
	CL_ASSERT(count == 1);

	UNUSED_PARAM(count);

	/*
	 * Set our pointer to the list item, which is stored at the beginning of
	 * the first component.
	 */
	p_pool_obj = (cl_pool_obj_t *) * pp_obj;
	*pp_pool_item = &p_pool_obj->pool_item;

	/* Calculate the pointer to the user's first component. */
	*pp_obj = ((uint8_t *) * pp_obj) + sizeof(cl_pool_obj_t);

	/*
	 * Set the object pointer in the pool item to point to the first of the
	 * user's components.
	 */
	p_pool_obj->p_object = *pp_obj;

	/* Invoke the user's constructor callback. */
	if (p_pool->pfn_init)
		status = p_pool->pfn_init(*pp_obj, (void *)p_pool->context);

	return (status);
}

/*
 * Callback to translate quick composite to grow pool destructor callback.
 */
static void __cl_pool_dtor_cb(IN const cl_pool_item_t * const p_pool_item,
			      IN void *const context)
{
	cl_pool_t *p_pool = (cl_pool_t *) context;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->pfn_dtor);
	CL_ASSERT(((cl_pool_obj_t *) p_pool_item)->p_object);

	/* Invoke the user's destructor callback. */
	p_pool->pfn_dtor((void *)((cl_pool_obj_t *) p_pool_item)->p_object,
			 (void *)p_pool->context);
}

void cl_pool_construct(IN cl_pool_t * const p_pool)
{
	CL_ASSERT(p_pool);

	memset(p_pool, 0, sizeof(cl_pool_t));

	cl_qcpool_construct(&p_pool->qcpool);
}

cl_status_t cl_pool_init(IN cl_pool_t * const p_pool, IN const size_t min_size,
			 IN const size_t max_size, IN const size_t grow_size,
			 IN const size_t object_size,
			 IN cl_pfn_pool_init_t pfn_initializer OPTIONAL,
			 IN cl_pfn_pool_dtor_t pfn_destructor OPTIONAL,
			 IN const void *const context)
{
	cl_status_t status;
	size_t total_size;

	CL_ASSERT(p_pool);

	/* Add the size of the list item to the first component. */
	total_size = object_size + sizeof(cl_pool_obj_t);

	/* Store callback function pointers. */
	p_pool->pfn_init = pfn_initializer;	/* may be NULL */
	p_pool->pfn_dtor = pfn_destructor;	/* may be NULL */
	p_pool->context = context;

	/*
	 * We need an initializer in all cases for quick composite pool, since
	 * the user pointer must be manipulated to hide the prefixed cl_pool_obj_t.
	 */
	status = cl_qcpool_init(&p_pool->qcpool, min_size, max_size, grow_size,
				&total_size, 1, __cl_pool_init_cb,
				pfn_destructor ? __cl_pool_dtor_cb : NULL,
				p_pool);

	return (status);
}
