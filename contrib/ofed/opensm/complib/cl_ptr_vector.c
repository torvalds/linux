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
#include <complib/cl_ptr_vector.h>

void cl_ptr_vector_construct(IN cl_ptr_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);

	memset(p_vector, 0, sizeof(cl_ptr_vector_t));

	p_vector->state = CL_UNINITIALIZED;
}

cl_status_t cl_ptr_vector_init(IN cl_ptr_vector_t * const p_vector,
			       IN const size_t min_size,
			       IN const size_t grow_size)
{
	cl_status_t status = CL_SUCCESS;

	CL_ASSERT(p_vector);

	cl_ptr_vector_construct(p_vector);

	p_vector->grow_size = grow_size;

	/*
	 * Set the state to initialized so that the call to set_size
	 * doesn't assert.
	 */
	p_vector->state = CL_INITIALIZED;

	/* get the storage needed by the user */
	if (min_size) {
		status = cl_ptr_vector_set_size(p_vector, min_size);
		if (status != CL_SUCCESS)
			cl_ptr_vector_destroy(p_vector);
	}

	return (status);
}

void cl_ptr_vector_destroy(IN cl_ptr_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(cl_is_state_valid(p_vector->state));

	/* Call the user's destructor for each element in the array. */
	if (p_vector->state == CL_INITIALIZED) {
		/* Destroy the page vector. */
		if (p_vector->p_ptr_array) {
			free((void *)p_vector->p_ptr_array);
			p_vector->p_ptr_array = NULL;
		}
	}

	p_vector->state = CL_UNINITIALIZED;
}

cl_status_t cl_ptr_vector_at(IN const cl_ptr_vector_t * const p_vector,
			     IN const size_t index, OUT void **const p_element)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	/* Range check */
	if (index >= p_vector->size)
		return (CL_INVALID_PARAMETER);

	*p_element = cl_ptr_vector_get(p_vector, index);
	return (CL_SUCCESS);
}

cl_status_t cl_ptr_vector_set(IN cl_ptr_vector_t * const p_vector,
			      IN const size_t index,
			      IN const void *const element)
{
	cl_status_t status;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	/* Determine if the vector has room for this element. */
	if (index >= p_vector->size) {
		/* Resize to accomodate the given index. */
		status = cl_ptr_vector_set_size(p_vector, index + 1);

		/* Check for failure on or before the given index. */
		if ((status != CL_SUCCESS) && (p_vector->size < index))
			return (status);
	}

	/* At this point, the array is guaranteed to be big enough */
	p_vector->p_ptr_array[index] = element;

	return (CL_SUCCESS);
}

void *cl_ptr_vector_remove(IN cl_ptr_vector_t * const p_vector,
			   IN const size_t index)
{
	size_t src;
	const void *element;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(p_vector->size > index);

	/* Store a copy of the element to return. */
	element = p_vector->p_ptr_array[index];
	/* Shift all items above the removed item down. */
	if (index < --p_vector->size) {
		for (src = index; src < p_vector->size; src++)
			p_vector->p_ptr_array[src] =
			    p_vector->p_ptr_array[src + 1];
	}
	/* Clear the entry for the element just outside of the new upper bound. */
	p_vector->p_ptr_array[p_vector->size] = NULL;

	return ((void *)element);
}

cl_status_t cl_ptr_vector_set_capacity(IN cl_ptr_vector_t * const p_vector,
				       IN const size_t new_capacity)
{
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
		free((void *)p_vector->p_ptr_array);
	}

	/* Set the new array. */
	p_vector->p_ptr_array = p_new_ptr_array;

	/* Update the vector with the new capactity. */
	p_vector->capacity = new_capacity;

	return (CL_SUCCESS);
}

cl_status_t cl_ptr_vector_set_size(IN cl_ptr_vector_t * const p_vector,
				   IN const size_t size)
{
	cl_status_t status;
	size_t new_capacity;

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

		status = cl_ptr_vector_set_capacity(p_vector, new_capacity);
		if (status != CL_SUCCESS)
			return (status);
	}

	p_vector->size = size;
	return (CL_SUCCESS);
}

cl_status_t cl_ptr_vector_set_min_size(IN cl_ptr_vector_t * const p_vector,
				       IN const size_t min_size)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	if (min_size > p_vector->size) {
		/* We have to resize the array */
		return (cl_ptr_vector_set_size(p_vector, min_size));
	}

	/* We didn't have to do anything */
	return (CL_SUCCESS);
}

void cl_ptr_vector_apply_func(IN const cl_ptr_vector_t * const p_vector,
			      IN cl_pfn_ptr_vec_apply_t pfn_callback,
			      IN const void *const context)
{
	size_t i;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	for (i = 0; i < p_vector->size; i++)
		pfn_callback(i, (void *)p_vector->p_ptr_array[i],
			     (void *)context);
}

size_t cl_ptr_vector_find_from_start(IN const cl_ptr_vector_t * const p_vector,
				     IN cl_pfn_ptr_vec_find_t pfn_callback,
				     IN const void *const context)
{
	size_t i;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	for (i = 0; i < p_vector->size; i++) {
		/* Invoke the callback */
		if (pfn_callback(i, (void *)p_vector->p_ptr_array[i],
				 (void *)context) == CL_SUCCESS) {
			break;
		}
	}
	return (i);
}

size_t cl_ptr_vector_find_from_end(IN const cl_ptr_vector_t * const p_vector,
				   IN cl_pfn_ptr_vec_find_t pfn_callback,
				   IN const void *const context)
{
	size_t i;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(pfn_callback);

	i = p_vector->size;

	while (i) {
		/* Invoke the callback for the current element. */
		i--;
		if (pfn_callback(i, (void *)p_vector->p_ptr_array[i],
				 (void *)context) == CL_SUCCESS) {
			return (i);
		}
	}

	return (p_vector->size);
}
