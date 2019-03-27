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
 *	This file contains pointer vector definitions.  Pointer Vector provides
 *  dynmically resizable array functionality.
 */

#ifndef _CL_PTR_VECTOR_H_
#define _CL_PTR_VECTOR_H_

#include <complib/cl_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Pointer Vector
* NAME
*	Pointer Vector
*
* DESCRIPTION
*	The Pointer Vector is a self-sizing array of pointers. Like a traditonal
*	array, a pointer vector allows efficient constant time access to elements
*	with a specified index.  A pointer vector grows transparently as the
*	user adds elements to the array.
*
*	The cl_pointer vector_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_ptr_vector_t
*
*	Callbacks:
*		cl_pfn_ptr_vec_apply_t, cl_pfn_ptr_vec_find_t
*
*	Item Manipulation:
*		cl_ptr_vector_set, cl_ptr_vector_obj
*
*	Initialization:
*		cl_ptr_vector_construct, cl_ptr_vector_init, cl_ptr_vector_destroy
*
*	Manipulation:
*		cl_ptr_vector_get_capacity, cl_ptr_vector_set_capacity,
*		cl_ptr_vector_get_size, cl_ptr_vector_set_size, cl_ptr_vector_set_min_size
*		cl_ptr_vector_get_ptr, cl_ptr_vector_get, cl_ptr_vector_at, cl_ptr_vector_set
*
*	Search:
*		cl_ptr_vector_find_from_start, cl_ptr_vector_find_from_end
*		cl_ptr_vector_apply_func
*********/
/****d* Component Library: Pointer Vector/cl_pfn_ptr_vec_apply_t
* NAME
*	cl_pfn_ptr_vec_apply_t
*
* DESCRIPTION
*	The cl_pfn_ptr_vec_apply_t function type defines the prototype for
*	functions used to iterate elements in a pointer vector.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_ptr_vec_apply_t) (IN const size_t index,
			    IN void *const element, IN void *context);
/*
* PARAMETERS
*	index
*		[in] Index of the element.
*
*	p_element
*		[in] Pointer to an element at the specified index in the pointer vector.
*
*	context
*		[in] Context provided in a call to cl_ptr_vector_apply_func.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function passed by users as a parameter to the cl_ptr_vector_apply_func
*	function.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_apply_func
*********/

/****d* Component Library: Pointer Vector/cl_pfn_ptr_vec_find_t
* NAME
*	cl_pfn_ptr_vec_find_t
*
* DESCRIPTION
*	The cl_pfn_ptr_vec_find_t function type defines the prototype for
*	functions used to find elements in a pointer vector.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_ptr_vec_find_t) (IN const size_t index,
			      IN const void *const element, IN void *context);
/*
* PARAMETERS
*	index
*		[in] Index of the element.
*
*	p_element
*		[in] Pointer to an element at the specified index in the
*		pointer vector.
*
*	context
*		[in] Context provided in a call to cl_ptr_vector_find_from_start or
*		cl_ptr_vector_find_from_end.
*
* RETURN VALUES
*	Return CL_SUCCESS if the element was found. This stops pointer vector
*	iteration.
*
*	CL_NOT_FOUND to continue the pointer vector iteration.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the
*	cl_ptr_vector_find_from_start and cl_ptr_vector_find_from_end functions.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_find_from_start, cl_ptr_vector_find_from_end
*********/

/****s* Component Library: Pointer Vector/cl_ptr_vector_t
* NAME
*	cl_ptr_vector_t
*
* DESCRIPTION
*	Pointer Vector structure.
*
*	The cl_ptr_vector_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_ptr_vector {
	size_t size;
	size_t grow_size;
	size_t capacity;
	const void **p_ptr_array;
	cl_state_t state;
} cl_ptr_vector_t;
/*
* FIELDS
*	size
*		 Number of elements successfully initialized in the pointer vector.
*
*	grow_size
*		 Number of elements to allocate when growing.
*
*	capacity
*		 total # of elements allocated.
*
*	alloc_list
*		 List of allocations.
*
*	p_ptr_array
*		 Internal array of pointers to elements.
*
*	state
*		State of the pointer vector.
*
* SEE ALSO
*	Pointer Vector
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_construct
* NAME
*	cl_ptr_vector_construct
*
* DESCRIPTION
*	The cl_ptr_vector_construct function constructs a pointer vector.
*
* SYNOPSIS
*/
void cl_ptr_vector_construct(IN cl_ptr_vector_t * const p_vector);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_ptr_vector_destroy without first calling
*	cl_ptr_vector_init.
*
*	Calling cl_ptr_vector_construct is a prerequisite to calling any other
*	pointer vector function except cl_ptr_vector_init.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_init, cl_ptr_vector_destroy
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_init
* NAME
*	cl_ptr_vector_init
*
* DESCRIPTION
*	The cl_ptr_vector_init function initializes a pointer vector for use.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_init(IN cl_ptr_vector_t * const p_vector,
		   IN const size_t min_size, IN const size_t grow_size);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure to inititalize.
*
*	min_size
*		[in] Initial number of elements.
*
*	grow_size
*		[in] Number of elements to allocate when incrementally growing
*		the pointer vector.  A value of zero disables automatic growth.
*
* RETURN VALUES
*	CL_SUCCESS if the pointer vector was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if the initialization failed.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_construct, cl_ptr_vector_destroy,
*	cl_ptr_vector_set, cl_ptr_vector_get, cl_ptr_vector_at
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_destroy
* NAME
*	cl_ptr_vector_destroy
*
* DESCRIPTION
*	The cl_ptr_vector_destroy function destroys a pointer vector.
*
* SYNOPSIS
*/
void cl_ptr_vector_destroy(IN cl_ptr_vector_t * const p_vector);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_ptr_vector_destroy frees all memory allocated for the pointer vector.
*
*	This function should only be called after a call to cl_ptr_vector_construct
*	or cl_ptr_vector_init.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_construct, cl_ptr_vector_init
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_get_capacity
* NAME
*	cl_ptr_vector_get_capacity
*
* DESCRIPTION
*	The cl_ptr_vector_get_capacity function returns the capacity of
*	a pointer vector.
*
* SYNOPSIS
*/
static inline size_t
cl_ptr_vector_get_capacity(IN const cl_ptr_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	return (p_vector->capacity);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose capacity to return.
*
* RETURN VALUE
*	Capacity, in elements, of the pointer vector.
*
* NOTES
*	The capacity is the number of elements that the pointer vector can store,
*	and can be greater than the number of elements stored. To get the number
*	of elements stored in the pointer vector, use cl_ptr_vector_get_size.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_set_capacity, cl_ptr_vector_get_size
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_get_size
* NAME
*	cl_ptr_vector_get_size
*
* DESCRIPTION
*	The cl_ptr_vector_get_size function returns the size of a pointer vector.
*
* SYNOPSIS
*/
static inline uint32_t
cl_ptr_vector_get_size(IN const cl_ptr_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	return ((uint32_t) p_vector->size);

}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose size to return.
*
* RETURN VALUE
*	Size, in elements, of the pointer vector.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_set_size, cl_ptr_vector_get_capacity
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_get
* NAME
*	cl_ptr_vector_get
*
* DESCRIPTION
*	The cl_ptr_vector_get function returns the pointer stored in a
*	pointer vector at a specified index.
*
* SYNOPSIS
*/
static inline void *cl_ptr_vector_get(IN const cl_ptr_vector_t * const p_vector,
				      IN const size_t index)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(p_vector->size > index);

	return ((void *)p_vector->p_ptr_array[index]);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure from which to get an
*		element.
*
*	index
*		[in] Index of the element.
*
* RETURN VALUE
*	Value of the pointer stored at the specified index.
*
* NOTES
*	cl_ptr_vector_get provides constant access times regardless of the index.
*
*	cl_ptr_vector_get does not perform boundary checking. Callers are
*	responsible for providing an index that is within the range of the pointer
*	vector.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_at, cl_ptr_vector_set, cl_ptr_vector_get_size
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_at
* NAME
*	cl_ptr_vector_at
*
* DESCRIPTION
*	The cl_ptr_vector_at function copies an element stored in a pointer
*	vector at a specified index, performing boundary checks.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_at(IN const cl_ptr_vector_t * const p_vector,
		 IN const size_t index, OUT void **const p_element);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure from which to get a copy of
*		an element.
*
*	index
*		[in] Index of the element.
*
*	p_element
*		[out] Pointer to storage for the pointer element. Contains a copy of
*		the desired pointer upon successful completion of the call.
*
* RETURN VALUES
*	CL_SUCCESS if an element was found at the specified index.
*
*	CL_INVALID_SETTING if the index was out of range.
*
* NOTES
*	cl_ptr_vector_at provides constant time access regardless of
*	the index, and performs boundary checking on the pointer vector.
*
*	Upon success, the p_element parameter contains a copy of the
*	desired element.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_get
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_set
* NAME
*	cl_ptr_vector_set
*
* DESCRIPTION
*	The cl_ptr_vector_set function sets the element at the specified index.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_set(IN cl_ptr_vector_t * const p_vector,
		  IN const size_t index, IN const void *const element);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure into which to store
*		an element.
*
*	index
*		[in] Index of the element.
*
*	element
*		[in] Pointer to store in the pointer vector.
*
* RETURN VALUES
*	CL_SUCCESS if the element was successfully set.
*
*	CL_INSUFFICIENT_MEMORY if the pointer vector could not be resized to
*	accommodate the new element.
*
* NOTES
*	cl_ptr_vector_set grows the pointer vector as needed to accommodate
*	the new element, unless the grow_size parameter passed into the
*	cl_ptr_vector_init function was zero.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_get
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_insert
* NAME
*	cl_ptr_vector_insert
*
* DESCRIPTION
*	The cl_ptr_vector_insert function inserts an element into a pointer vector.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_ptr_vector_insert(IN cl_ptr_vector_t * const p_vector,
		     IN const void *const element,
		     OUT size_t * const p_index OPTIONAL)
{
	cl_status_t status;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	status = cl_ptr_vector_set(p_vector, p_vector->size, element);
	if (status == CL_SUCCESS && p_index)
		*p_index = p_vector->size - 1;

	return (status);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure into which to store
*		an element.
*
*	element
*		[in] Pointer to store in the pointer vector.
*
*	p_index
*		[out] Pointer to the index of the element.  Valid only if
*		insertion was successful.
*
* RETURN VALUES
*	CL_SUCCESS if the element was successfully inserted.
*
*	CL_INSUFFICIENT_MEMORY if the pointer vector could not be resized to
*	accommodate the new element.
*
* NOTES
*	cl_ptr_vector_insert places the new element at the end of
*	the pointer vector.
*
*	cl_ptr_vector_insert grows the pointer vector as needed to accommodate
*	the new element, unless the grow_size parameter passed into the
*	cl_ptr_vector_init function was zero.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_remove, cl_ptr_vector_set
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_remove
* NAME
*	cl_ptr_vector_remove
*
* DESCRIPTION
*	The cl_ptr_vector_remove function removes and returns the pointer stored
*	in a pointer vector at a specified index.  Items beyond the removed item
*	are shifted down and the size of the pointer vector is decremented.
*
* SYNOPSIS
*/
void *cl_ptr_vector_remove(IN cl_ptr_vector_t * const p_vector,
			   IN const size_t index);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure from which to get an
*		element.
*
*	index
*		[in] Index of the element.
*
* RETURN VALUE
*	Value of the pointer stored at the specified index.
*
* NOTES
*	cl_ptr_vector_get does not perform boundary checking. Callers are
*	responsible for providing an index that is within the range of the pointer
*	vector.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_insert, cl_ptr_vector_get_size
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_set_capacity
* NAME
*	cl_ptr_vector_set_capacity
*
* DESCRIPTION
*	The cl_ptr_vector_set_capacity function reserves memory in a
*	pointer vector for a specified number of pointers.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_set_capacity(IN cl_ptr_vector_t * const p_vector,
			   IN const size_t new_capacity);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose capacity to set.
*
*	new_capacity
*		[in] Total number of elements for which the pointer vector should
*		allocate memory.
*
* RETURN VALUES
*	CL_SUCCESS if the capacity was successfully set.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to satisfy the
*	operation. The pointer vector is left unchanged.
*
* NOTES
*	cl_ptr_vector_set_capacity increases the capacity of the pointer vector.
*	It does not change the size of the pointer vector. If the requested
*	capacity is less than the current capacity, the pointer vector is left
*	unchanged.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_get_capacity, cl_ptr_vector_set_size,
*	cl_ptr_vector_set_min_size
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_set_size
* NAME
*	cl_ptr_vector_set_size
*
* DESCRIPTION
*	The cl_ptr_vector_set_size function resizes a pointer vector, either
*	increasing or decreasing its size.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_set_size(IN cl_ptr_vector_t * const p_vector,
		       IN const size_t size);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose size to set.
*
*	size
*		[in] Number of elements desired in the pointer vector.
*
* RETURN VALUES
*	CL_SUCCESS if the size of the pointer vector was set successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to complete the
*	operation. The pointer vector is left unchanged.
*
* NOTES
*	cl_ptr_vector_set_size sets the pointer vector to the specified size.
*	If size is smaller than the current size of the pointer vector, the size
*	is reduced.
*
*	This function can only fail if size is larger than the current capacity.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_get_size, cl_ptr_vector_set_min_size,
*	cl_ptr_vector_set_capacity
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_set_min_size
* NAME
*	cl_ptr_vector_set_min_size
*
* DESCRIPTION
*	The cl_ptr_vector_set_min_size function resizes a pointer vector to a
*	specified size if the pointer vector is smaller than the specified size.
*
* SYNOPSIS
*/
cl_status_t
cl_ptr_vector_set_min_size(IN cl_ptr_vector_t * const p_vector,
			   IN const size_t min_size);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose minimum size to set.
*
*	min_size
*		[in] Minimum number of elements that the pointer vector should contain.
*
* RETURN VALUES
*	CL_SUCCESS if the pointer vector size is greater than or equal to min_size.
*	This could indicate that the pointer vector's capacity was increased to
*	min_size or that the pointer vector was already of sufficient size.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to resize the
*	pointer vector.  The pointer vector is left unchanged.
*
* NOTES
*	If min_size is smaller than the current size of the pointer vector,
*	the pointer vector is unchanged. The pointer vector is unchanged if the
*	size could not be changed due to insufficient memory being available to
*	perform the operation.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_get_size, cl_ptr_vector_set_size,
*	cl_ptr_vector_set_capacity
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_apply_func
* NAME
*	cl_ptr_vector_apply_func
*
* DESCRIPTION
*	The cl_ptr_vector_apply_func function invokes a specified function for
*	every element in a pointer vector.
*
* SYNOPSIS
*/
void
cl_ptr_vector_apply_func(IN const cl_ptr_vector_t * const p_vector,
			 IN cl_pfn_ptr_vec_apply_t pfn_callback,
			 IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure whose elements to iterate.
*
*	pfn_callback
*		[in] Function invoked for every element in the array.
*		See the cl_pfn_ptr_vec_apply_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_ptr_vector_apply_func invokes the specified function for every element
*	in the pointer vector, starting from the beginning of the pointer vector.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_find_from_start, cl_ptr_vector_find_from_end,
*	cl_pfn_ptr_vec_apply_t
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_find_from_start
* NAME
*	cl_ptr_vector_find_from_start
*
* DESCRIPTION
*	The cl_ptr_vector_find_from_start function uses a specified function to
*	search for elements in a pointer vector starting from the lowest index.
*
* SYNOPSIS
*/
size_t
cl_ptr_vector_find_from_start(IN const cl_ptr_vector_t * const p_vector,
			      IN cl_pfn_ptr_vec_find_t pfn_callback,
			      IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure to inititalize.
*
*	pfn_callback
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_ptr_vec_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUES
*	Index of the element, if found.
*
*	Size of the pointer vector if the element was not found.
*
* NOTES
*	cl_ptr_vector_find_from_start does not remove the found element from
*	the pointer vector. The index of the element is returned when the function
*	provided by the pfn_callback parameter returns CL_SUCCESS.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_find_from_end, cl_ptr_vector_apply_func,
*	cl_pfn_ptr_vec_find_t
*********/

/****f* Component Library: Pointer Vector/cl_ptr_vector_find_from_end
* NAME
*	cl_ptr_vector_find_from_end
*
* DESCRIPTION
*	The cl_ptr_vector_find_from_end function uses a specified function to
*	search for elements in a pointer vector starting from the highest index.
*
* SYNOPSIS
*/
size_t
cl_ptr_vector_find_from_end(IN const cl_ptr_vector_t * const p_vector,
			    IN cl_pfn_ptr_vec_find_t pfn_callback,
			    IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_ptr_vector_t structure to inititalize.
*
*	pfn_callback
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_ptr_vec_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUES
*	Index of the element, if found.
*
*	Size of the pointer vector if the element was not found.
*
* NOTES
*	cl_ptr_vector_find_from_end does not remove the found element from
*	the pointer vector. The index of the element is returned when the function
*	provided by the pfn_callback parameter returns CL_SUCCESS.
*
* SEE ALSO
*	Pointer Vector, cl_ptr_vector_find_from_start, cl_ptr_vector_apply_func,
*	cl_pfn_ptr_vec_find_t
*********/

END_C_DECLS
#endif				/* _CL_PTR_VECTOR_H_ */
