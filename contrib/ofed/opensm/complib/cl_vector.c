/*
 * Copyright (c) 2004-2006 Voltaire, Inc. All rights reserved.
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
 *	This file contains ivector and isvector implementations.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <complib/cl_vector.h>

/*
 * Define the maximum size for array pages in an cl_vector_t.
 * This size is in objects, not bytes.
 */
#define SVEC_MAX_PAGE_SIZE 0x1000

/*
 * cl_vector_copy_general
 *
 * Description:
 *	copy operator used when size of the user object doesn't fit one of the
 *	other optimized copy functions.
 *
 * Inputs:
 *	p_src - source for copy
 *
 * Outputs:
 *	p_dest - destination for copy
 *
 * Returns:
 *	None
 *
 */
static void cl_vector_copy_general(OUT void *const p_dest,
				   IN const void *const p_src,
				   IN const size_t size)
{
	memcpy(p_dest, p_src, size);
}

/*
 * cl_vector_copy8
 *
 * Description:
 *	copy operator used when the user structure is only 8 bits long.
 *
 * Inputs:
 *	p_src - source for copy
 *
 * Outputs:
 *	p_dest - destination for copy
 *
 * Returns:
 *	None
 *
 */
static void cl_vector_copy8(OUT void *const p_dest,
			    IN const void *const p_src, IN const size_t size)
{
	CL_ASSERT(size == sizeof(uint8_t));
	UNUSED_PARAM(size);

	*(uint8_t *) p_dest = *(uint8_t *) p_src;
}

/*
 * cl_vector_copy16
 *
 * Description:
 *	copy operator used when the user structure is only 16 bits long.
 *
 * Inputs:
 *	p_src - source for copy
 *
 * Outputs:
 *	p_dest - destination for copy
 *
 * Returns:
 *	None
 *
 */
void cl_vector_copy16(OUT void *const p_dest,
		      IN const void *const p_src, IN const size_t size)
{
	CL_ASSERT(size == sizeof(uint16_t));
	UNUSED_PARAM(size);

	*(uint16_t *) p_dest = *(uint16_t *) p_src;
}

/*
 * cl_vector_copy32
 *
 * Description:
 *	copy operator used when the user structure is only 32 bits long.
 *
 * Inputs:
 *	p_src - source for copy
 *
 * Outputs:
 *	p_dest - destination for copy
 *
 * Returns:
 *	None
 *
 */
void cl_vector_copy32(OUT void *const p_dest,
		      IN const void *const p_src, IN const size_t size)
{
	CL_ASSERT(size == sizeof(uint32_t));
	UNUSED_PARAM(size);

	*(uint32_t *) p_dest = *(uint32_t *) p_src;
}

/*
 * cl_vector_copy64
 *
 * Description:
 *	copy operator used when the user structure is only 64 bits long.
 *
 * Inputs:
 *	p_src - source for copy
 *
 * Outputs:
 *	p_dest - destination for copy
 *
 * Returns:
 *	None
 *
 */
void cl_vector_copy64(OUT void *const p_dest,
		      IN const void *const p_src, IN const size_t size)
{
	CL_ASSERT(size == sizeof(uint64_t));
	UNUSED_PARAM(size);

	*(uint64_t *) p_dest = *(uint64_t *) p_src;
}

void cl_vector_construct(IN cl_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);

	memset(p_vector, 0, sizeof(cl_vector_t));

	p_vector->state = CL_UNINITIALIZED;
}

cl_status_t cl_vector_init(IN cl_vector_t * const p_vector,
			   IN const size_t min_size, IN const size_t grow_size,
			   IN const size_t element_size,
			   IN cl_pfn_vec_init_t pfn_init OPTIONAL,
			   IN cl_pfn_vec_dtor_t pfn_dtor OPTIONAL,
			   IN const void *const context)
{
	cl_status_t status = CL_SUCCESS;

	CL_ASSERT(p_vector);
	CL_ASSERT(element_size);

	cl_vector_construct(p_vector);

	p_vector->grow_size = grow_size;
	p_vector->element_size = element_size;
	p_vector->pfn_init = pfn_init;
	p_vector->pfn_dtor = pfn_dtor;
	p_vector->context = context;

	/*
	 * Try to choose a smart copy operator
	 * someday, we could simply let the users pass one in
	 */
	switch (element_size) {
	case sizeof(uint8_t):
		p_vector->pfn_copy = cl_vector_copy8;
		break;

	case sizeof(uint16_t):
		p_vector->pfn_copy = cl_vector_copy16;
		break;

	case sizeof(uint32_t):
		p_vector->pfn_copy = cl_vector_copy32;
		break;

	case sizeof(uint64_t):
		p_vector->pfn_copy = cl_vector_copy64;
		break;

	default:
		p_vector->pfn_copy = cl_vector_copy_general;
		break;
	}

	/*
	 * Set the state to initialized so that the call to set_size
	 * doesn't assert.
	 */
	p_vector->state = CL_INITIALIZED;

	/* Initialize the allocation list */
	cl_qlist_init(&p_vector->alloc_list);

	/* get the storage needed by the user */
	if (min_size) {
		status = cl_vector_set_size(p_vector, min_size);
		if (status != CL_SUCCESS)
			cl_vector_destroy(p_vector);
	}

	return (status);
}

void cl_vector_destroy(IN cl_vector_t * const p_vector)
{
	size_t i;
	void *p_element;

	CL_ASSERT(p_vector);
	CL_ASSERT(cl_is_state_valid(p_vector->state));

	/* Call the user's destructor for each element in the array. */
	if (p_vector->state == CL_INITIALIZED) {
		if (p_vector->pfn_dtor) {
			for (i = 0; i < p_vector->size; i++) {
				p_element = p_vector->p_ptr_array[i];
				/* Sanity check! */
				CL_ASSERT(p_element);
				p_vector->pfn_dtor(p_element,
						   (void *)p_vector->context);
			}
		}

		/* Deallocate the pages */
		while (!cl_is_qlist_empty(&p_vector->alloc_list))
			free(cl_qlist_remove_head(&p_vector->alloc_list));

		/* Destroy the page vector. */
		if (p_vector->p_ptr_array) {
			free(p_vector->p_ptr_array);
			p_vector->p_ptr_array = NULL;
		}
	}

	p_vector->state = CL_UNINITIALIZED;
}

cl_status_t cl_vector_at(IN const cl_vector_t * const p_vector,
			 IN const size_t index, OUT void *const p_element)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	/* Range check */
	if (index >= p_vector->size)
		return (CL_INVALID_PARAMETER);

	cl_vector_get(p_vector, index, p_element);
	return (CL_SUCCESS);
}

cl_status_t cl_vector_set(IN cl_vector_t * const p_vector,
			  IN const size_t index, IN void *const p_element)
{
	cl_status_t status;
	void *p_dest;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(p_element);

	/* Determine if the vector has room for this element. */
	if (index >= p_vector->size) {
		/* Resize to accomodate the given index. */
		status = cl_vector_set_size(p_vector, index + 1);

		/* Check for failure on or before the given index. */
		if ((status != CL_SUCCESS) && (p_vector->size < index))
			return (status);
	}

	/* At this point, the array is guaranteed to be big enough */
	p_dest = cl_vector_get_ptr(p_vector, index);
	/* Sanity check! */
	CL_ASSERT(p_dest);

	/* Copy the data into the array */
	p_vector->pfn_copy(p_dest, p_element, p_vector->element_size);

	return (CL_SUCCESS);
}

cl_status_t cl_vector_set_capacity(IN cl_vector_t * const p_vector,
				   IN const size_t new_capacity)
{
	size_t new_elements;
	size_t alloc_size;
	size_t i;
	cl_list_item_t *p_buf;
	void *p_new_ptr_array;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	/* Do we have to do anything here? */
	if (new_capacity <= p_vector->capacity) {
		/* Nope */
		return (CL_SUCCESS);
	}

	/* Allocate our pointer array. */
	p_new_ptr_array = malloc(new_capacity * sizeof(void *));
	if (!p_new_ptr_array)
		return (CL_INSUFFICIENT_MEMORY);
	else
		memset(p_new_ptr_array, 0, new_capacity * sizeof(void *));

	if (p_vector->p_ptr_array) {
		/* Copy the old pointer array into the new. */
		memcpy(p_new_ptr_array, p_vector->p_ptr_array,
		       p_vector->capacity * sizeof(void *));

		/* Free the old pointer array. */
		free(p_vector->p_ptr_array);
	}

	/* Set the new array. */
	p_vector->p_ptr_array = p_new_ptr_array;

	/*
	 * We have to add capacity to the array.  Determine how many
	 * elements to add.
	 */
	new_elements = new_capacity - p_vector->capacity;
	/* Determine the allocation size for the new array elements. */
	alloc_size = new_elements * p_vector->element_size;

	p_buf = (cl_list_item_t *) malloc(alloc_size + sizeof(cl_list_item_t));
	if (!p_buf)
		return (CL_INSUFFICIENT_MEMORY);
	else
		memset(p_buf, 0, alloc_size + sizeof(cl_list_item_t));

	cl_qlist_insert_tail(&p_vector->alloc_list, p_buf);
	/* Advance the buffer pointer past the list item. */
	p_buf++;

	for (i = p_vector->capacity; i < new_capacity; i++) {
		p_vector->p_ptr_array[i] = p_buf;
		/* Move the buffer pointer to the next element. */
		p_buf = (void *)(((uint8_t *) p_buf) + p_vector->element_size);
	}

	/* Update the vector with the new capactity. */
	p_vector->capacity = new_capacity;

	return (CL_SUCCESS);
}

cl_status_t cl_vector_set_size(IN cl_vector_t * const p_vector,
			       IN const size_t size)
{
	cl_status_t status;
	size_t new_capacity;
	size_t index;
	void *p_element;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	/* Check to see if the requested size is the same as the existing size. */
	if (size == p_vector->size)
		return (CL_SUCCESS);

	/* Determine if the vector has room for this element. */
	if (size >= p_vector->capacity) {
		if (!p_vector->grow_size)
			return (CL_INSUFFICIENT_MEMORY);

		/* Calculate the new capacity, taking into account the grow size. */
		new_capacity = size;
		if (size % p_vector->grow_size) {
			/* Round up to nearest grow_size boundary. */
			new_capacity += p_vector->grow_size -
			    (size % p_vector->grow_size);
		}

		status = cl_vector_set_capacity(p_vector, new_capacity);
		if (status != CL_SUCCESS)
			return (status);
	}

	/* Are we growing the array and need to invoke an initializer callback? */
	if (size > p_vector->size && p_vector->pfn_init) {
		for (index = p_vector->size; index < size; index++) {
			/* Get a pointer to this element */
			p_element = cl_vector_get_ptr(p_vector, index);

			/* Call the user's initializer and trap failures. */
			status =
			    p_vector->pfn_init(p_element,
					       (void *)p_vector->context);
			if (status != CL_SUCCESS) {
				/* Call the destructor for this object */
				if (p_vector->pfn_dtor)
					p_vector->pfn_dtor(p_element,
							   (void *)p_vector->
							   context);

				/* Return the failure status to the caller. */
				return (status);
			}

			/* The array just grew by one element */
			p_vector->size++;
		}
	} else if (p_vector->pfn_dtor) {
		/* The array is shrinking and there is a destructor to invoke. */
		for (index = size; index < p_vector->size; index++) {
			/* compute the address of the new elements */
			p_element = cl_vector_get_ptr(p_vector, index);
			/* call the user's destructor */
			p_vector->pfn_dtor(p_element,
					   (void *)p_vector->context);
		}
	}

	p_vector->size = size;
	return (CL_SUCCESS);
}

cl_status_t cl_vector_set_min_size(IN cl_vector_t * const p_vector,
				   IN const size_t min_size)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	if (min_size > p_vector->size) {
		/* We have to resize the array */
		return (cl_vector_set_size(p_vector, min_size));
	}

	/* We didn't have to do anything */
	return (CL_SUCCESS);
}

void cl_vector_apply_func(IN const cl_vector_t * const p_vector,
			  IN cl_pfn_vec_apply_t pfn_callback,
			  IN const void *const context)
{
	size_t i;
	void *p_element;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	for (i = 0; i < p_vector->size; i++) {
		p_element = cl_vector_get_ptr(p_vector, i);
		pfn_callback(i, p_element, (void *)context);
	}
}

size_t cl_vector_find_from_start(IN const cl_vector_t * const p_vector,
				 IN cl_pfn_vec_find_t pfn_callback,
				 IN const void *const context)
{
	size_t i;
	void *p_element;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	for (i = 0; i < p_vector->size; i++) {
		p_element = cl_vector_get_ptr(p_vector, i);
		/* Invoke the callback */
		if (pfn_callback(i, p_element, (void *)context) == CL_SUCCESS)
			break;
	}
	return (i);
}

size_t cl_vector_find_from_end(IN const cl_vector_t * const p_vector,
			       IN cl_pfn_vec_find_t pfn_callback,
			       IN const void *const context)
{
	size_t i;
	void *p_element;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	i = p_vector->size;

	while (i) {
		/* Get a pointer to the element in the array. */
		p_element = cl_vector_get_ptr(p_vector, --i);
		CL_ASSERT(p_element);

		/* Invoke the callback for the current element. */
		if (pfn_callback(i, p_element, (void *)context) == CL_SUCCESS)
			return (i);
	}

	return (p_vector->size);
}
