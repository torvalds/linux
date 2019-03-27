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
 *	This file contains vector definitions.  Vector provides dynmically
 *	resizable array functionality.  Objects in a Vector are not relocated
 *	when the array is resized.
 */

#ifndef _CL_VECTOR_H_
#define _CL_VECTOR_H_

#include <complib/cl_qlist.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Vector
* NAME
*	Vector
*
* DESCRIPTION
*	The Vector is a self-sizing array. Like a traditonal array, a vector
*	allows efficient constant time access to elements with a specified index.
*	A vector grows transparently as the user adds elements to the array.
*
*	As the vector grows in size, it does not relocate existing elements in
*	memory. This allows using pointers to elements stored in a Vector.
*
*	Users can supply an initializer functions that allow a vector to ensure
*	that new items added to the vector are properly initialized. A vector
*	calls the initializer function on a per object basis when growing the
*	array. The initializer is optional.
*
*	The initializer function can fail, and returns a cl_status_t. The vector
*	will call the destructor function, if provided, for an element that
*	failed initialization. If an initializer fails, a vector does not call
*	the initializer for objects in the remainder of the new memory allocation.
*
*	The cl_vector_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_vector_t
*
*	Callbacks:
*		cl_pfn_vec_init_t, cl_pfn_vec_dtor_t, cl_pfn_vec_apply_t,
*		cl_pfn_vec_find_t
*
*	Item Manipulation:
*		cl_vector_set_obj, cl_vector_obj
*
*	Initialization:
*		cl_vector_construct, cl_vector_init, cl_vector_destroy
*
*	Manipulation:
*		cl_vector_get_capacity, cl_vector_set_capacity,
*		cl_vector_get_size, cl_vector_set_size, cl_vector_set_min_size
*		cl_vector_get_ptr, cl_vector_get, cl_vector_at, cl_vector_set
*
*	Search:
*		cl_vector_find_from_start, cl_vector_find_from_end
*		cl_vector_apply_func
*********/
/****d* Component Library: Vector/cl_pfn_vec_init_t
* NAME
*	cl_pfn_vec_init_t
*
* DESCRIPTION
*	The cl_pfn_vec_init_t function type defines the prototype for functions
*	used as initializer for elements being allocated by a vector.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_vec_init_t) (IN void *const p_element, IN void *context);
/*
* PARAMETERS
*	p_element
*		[in] Pointer to an element being added to a vector.
*
*	context
*		[in] Context provided in a call to cl_vector_init.
*
* RETURN VALUES
*	Return CL_SUCCESS to indicate that the element was initialized successfully.
*
*	Other cl_status_t values will be returned by the cl_vector_init,
*	cl_vector_set_size, and cl_vector_set_min_size functions.
*
*	In situations where the vector's size needs to grows in order to satisfy
*	a call to cl_vector_set, a non-successful status returned by the
*	initializer callback causes the growth to stop.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the initializer function provided by users as an optional parameter to
*	the cl_vector_init function.
*
* SEE ALSO
*	Vector, cl_vector_init
*********/

/****d* Component Library: Vector/cl_pfn_vec_dtor_t
* NAME
*	cl_pfn_vec_dtor_t
*
* DESCRIPTION
*	The cl_pfn_vec_dtor_t function type defines the prototype for functions
*	used as destructor for elements being deallocated from a vector.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_vec_dtor_t) (IN void *const p_element, IN void *context);
/*
* PARAMETERS
*	p_element
*		[in] Pointer to an element being deallocated from a vector.
*
*	context
*		[in] Context provided in a call to cl_vector_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the destructor function provided by users as an optional parameter to
*	the cl_vector_init function.
*
* SEE ALSO
*	Vector, cl_vector_init
*********/

/****d* Component Library: Vector/cl_pfn_vec_apply_t
* NAME
*	cl_pfn_vec_apply_t
*
* DESCRIPTION
*	The cl_pfn_vec_apply_t function type defines the prototype for functions
*	used to iterate elements in a vector.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_vec_apply_t) (IN const size_t index,
			IN void *const p_element, IN void *context);
/*
* PARAMETERS
*	index
*		[in] Index of the element.
*
*	p_element
*		[in] Pointer to an element at the specified index in the vector.
*
*	context
*		[in] Context provided in a call to cl_vector_apply_func.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function passed by users as a parameter to the cl_vector_apply_func
*	function.
*
* SEE ALSO
*	Vector, cl_vector_apply_func
*********/

/****d* Component Library: Vector/cl_pfn_vec_find_t
* NAME
*	cl_pfn_vec_find_t
*
* DESCRIPTION
*	The cl_pfn_vec_find_t function type defines the prototype for functions
*	used to find elements in a vector.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_vec_find_t) (IN const size_t index,
			  IN const void *const p_element, IN void *context);
/*
* PARAMETERS
*	index
*		[in] Index of the element.
*
*	p_element
*		[in] Pointer to an element at the specified index in the vector.
*
*	context
*		[in] Context provided in a call to cl_vector_find_from_start or
*		cl_vector_find_from_end.
*
* RETURN VALUES
*	Return CL_SUCCESS if the element was found. This stops vector iteration.
*
*	CL_NOT_FOUND to continue the vector iteration.
*
* NOTES
*	This function type is provided as function prototype reference for the
*	function provided by users as a parameter to the cl_vector_find_from_start
*	and cl_vector_find_from_end functions.
*
* SEE ALSO
*	Vector, cl_vector_find_from_start, cl_vector_find_from_end
*********/

/****i* Component Library: Vector/cl_pfn_vec_copy_t
* NAME
*	cl_pfn_vec_copy_t
*
* DESCRIPTION
*	The cl_pfn_vec_copy_t function type defines the prototype for functions
*	used to copy elements in a vector.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_vec_copy_t) (IN void *const p_dest,
		       IN const void *const p_src, IN const size_t size);
/*
* PARAMETERS
*	p_dest
*		[in] Pointer to the destination buffer into which to copy p_src.
*
*	p_src
*		[in] Pointer to the destination buffer from which to copy.
*
*	size
*		[in] Number of bytes to copy.
*
* RETURN VALUE
*	This function does not return a value.
*
* SEE ALSO
*	Vector
*********/

/****s* Component Library: Vector/cl_vector_t
* NAME
*	cl_vector_t
*
* DESCRIPTION
*	Vector structure.
*
*	The cl_vector_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_vector {
	size_t size;
	size_t grow_size;
	size_t capacity;
	size_t element_size;
	cl_pfn_vec_init_t pfn_init;
	cl_pfn_vec_dtor_t pfn_dtor;
	cl_pfn_vec_copy_t pfn_copy;
	const void *context;
	cl_qlist_t alloc_list;
	void **p_ptr_array;
	cl_state_t state;
} cl_vector_t;
/*
* FIELDS
*	size
*		 Number of elements successfully initialized in the vector.
*
*	grow_size
*		 Number of elements to allocate when growing.
*
*	capacity
*		 total # of elements allocated.
*
*	element_size
*		 Size of each element.
*
*	pfn_init
*		 User supplied element initializer.
*
*	pfn_dtor
*		 User supplied element destructor.
*
*	pfn_copy
*		 Copy operator.
*
*	context
*		 User context for callbacks.
*
*	alloc_list
*		 List of allocations.
*
*	p_ptr_array
*		 Internal array of pointers to elements.
*
*	state
*		State of the vector.
*
* SEE ALSO
*	Vector
*********/

/****f* Component Library: Vector/cl_vector_construct
* NAME
*	cl_vector_construct
*
* DESCRIPTION
*	The cl_vector_construct function constructs a vector.
*
* SYNOPSIS
*/
void cl_vector_construct(IN cl_vector_t * const p_vector);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_vector_destroy without first calling cl_vector_init.
*
*	Calling cl_vector_construct is a prerequisite to calling any other
*	vector function except cl_vector_init.
*
* SEE ALSO
*	Vector, cl_vector_init, cl_vector_destroy
*********/

/****f* Component Library: Vector/cl_vector_init
* NAME
*	cl_vector_init
*
* DESCRIPTION
*	The cl_vector_init function initializes a vector for use.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_init(IN cl_vector_t * const p_vector,
	       IN const size_t min_size,
	       IN const size_t grow_size,
	       IN const size_t element_size,
	       IN cl_pfn_vec_init_t pfn_init OPTIONAL,
	       IN cl_pfn_vec_dtor_t pfn_dtor OPTIONAL,
	       IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure to inititalize.
*
*	min_size
*		[in] Initial number of elements.
*
*	grow_size
*		[in] Number of elements to allocate when incrementally growing
*		the vector.  A value of zero disables automatic growth.
*
*	element_size
*		[in] Size of each element.
*
*	pfn_init
*		[in] Initializer callback to invoke for every new element.
*		See the cl_pfn_vec_init_t function type declaration for details about
*		the callback function.
*
*	pfn_dtor
*		[in] Destructor callback to invoke for elements being deallocated.
*		See the cl_pfn_vec_dtor_t function type declaration for details about
*		the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the vector was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if the initialization failed.
*
*	cl_status_t value returned by optional initializer function specified by
*	the pfn_init parameter.
*
* NOTES
*	The constructor and initializer functions, if any, are invoked for every
*	new element in the array.
*
* SEE ALSO
*	Vector, cl_vector_construct, cl_vector_destroy, cl_vector_set,
*	cl_vector_get, cl_vector_get_ptr, cl_vector_at
*********/

/****f* Component Library: Vector/cl_vector_destroy
* NAME
*	cl_vector_destroy
*
* DESCRIPTION
*	The cl_vector_destroy function destroys a vector.
*
* SYNOPSIS
*/
void cl_vector_destroy(IN cl_vector_t * const p_vector);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_vector_destroy frees all memory allocated for the vector. The vector
*	is left initialized to a zero capacity and size.
*
*	This function should only be called after a call to cl_vector_construct
*	or cl_vector_init.
*
* SEE ALSO
*	Vector, cl_vector_construct, cl_vector_init
*********/

/****f* Component Library: Vector/cl_vector_get_capacity
* NAME
*	cl_vector_get_capacity
*
* DESCRIPTION
*	The cl_vector_get_capacity function returns the capacity of a vector.
*
* SYNOPSIS
*/
static inline size_t
cl_vector_get_capacity(IN const cl_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	return (p_vector->capacity);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose capacity to return.
*
* RETURN VALUE
*	Capacity, in elements, of the vector.
*
* NOTES
*	The capacity is the number of elements that the vector can store, and
*	can be greater than the number of elements stored. To get the number of
*	elements stored in the vector, use cl_vector_get_size.
*
* SEE ALSO
*	Vector, cl_vector_set_capacity, cl_vector_get_size
*********/

/****f* Component Library: Vector/cl_vector_get_size
* NAME
*	cl_vector_get_size
*
* DESCRIPTION
*	The cl_vector_get_size function returns the size of a vector.
*
* SYNOPSIS
*/
static inline size_t cl_vector_get_size(IN const cl_vector_t * const p_vector)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	return (p_vector->size);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose size to return.
*
* RETURN VALUE
*	Size, in elements, of the vector.
*
* SEE ALSO
*	Vector, cl_vector_set_size, cl_vector_get_capacity
*********/

/****f* Component Library: Vector/cl_vector_get_ptr
* NAME
*	cl_vector_get_ptr
*
* DESCRIPTION
*	The cl_vector_get_ptr function returns a pointer to an element
*	stored in a vector at a specified index.
*
* SYNOPSIS
*/
static inline void *cl_vector_get_ptr(IN const cl_vector_t * const p_vector,
				      IN const size_t index)
{
	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);

	return (p_vector->p_ptr_array[index]);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure from which to get a
*		pointer to an element.
*
*	index
*		[in] Index of the element.
*
* RETURN VALUE
*	Pointer to the element stored at specified index.
*
* NOTES
*	cl_vector_get_ptr provides constant access times regardless of the index.
*
*	cl_vector_get_ptr does not perform boundary checking. Callers are
*	responsible for providing an index that is within the range of the vector.
*
* SEE ALSO
*	Vector, cl_vector_get, cl_vector_at, cl_vector_set, cl_vector_get_size
*********/

/****f* Component Library: Vector/cl_vector_get
* NAME
*	cl_vector_get
*
* DESCRIPTION
*	The cl_vector_get function copies an element stored in a vector at a
*	specified index.
*
* SYNOPSIS
*/
static inline void
cl_vector_get(IN const cl_vector_t * const p_vector,
	      IN const size_t index, OUT void *const p_element)
{
	void *p_src;

	CL_ASSERT(p_vector);
	CL_ASSERT(p_vector->state == CL_INITIALIZED);
	CL_ASSERT(p_element);

	/* Get a pointer to the element. */
	p_src = cl_vector_get_ptr(p_vector, index);
	p_vector->pfn_copy(p_src, p_element, p_vector->element_size);
}

/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure from which to get a copy of
*		an element.
*
*	index
*		[in] Index of the element.
*
*	p_element
*		[out] Pointer to storage for the element. Contains a copy of the
*		desired element upon successful completion of the call.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_vector_get provides constant time access regardless of the index.
*
*	cl_vector_get does not perform boundary checking on the vector, and
*	callers are responsible for providing an index that is within the range
*	of the vector. To access elements after performing boundary checks,
*	use cl_vector_at.
*
*	The p_element parameter contains a copy of the desired element upon
*	return from this function.
*
* SEE ALSO
*	Vector, cl_vector_get_ptr, cl_vector_at
*********/

/****f* Component Library: Vector/cl_vector_at
* NAME
*	cl_vector_at
*
* DESCRIPTION
*	The cl_vector_at function copies an element stored in a vector at a
*	specified index, performing boundary checks.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_at(IN const cl_vector_t * const p_vector,
	     IN const size_t index, OUT void *const p_element);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure from which to get a copy of
*		an element.
*
*	index
*		[in] Index of the element.
*
*	p_element
*		[out] Pointer to storage for the element. Contains a copy of the
*		desired element upon successful completion of the call.
*
* RETURN VALUES
*	CL_SUCCESS if an element was found at the specified index.
*
*	CL_INVALID_SETTING if the index was out of range.
*
* NOTES
*	cl_vector_at provides constant time access regardless of the index, and
*	performs boundary checking on the vector.
*
*	Upon success, the p_element parameter contains a copy of the desired element.
*
* SEE ALSO
*	Vector, cl_vector_get, cl_vector_get_ptr
*********/

/****f* Component Library: Vector/cl_vector_set
* NAME
*	cl_vector_set
*
* DESCRIPTION
*	The cl_vector_set function sets the element at the specified index.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_set(IN cl_vector_t * const p_vector,
	      IN const size_t index, IN void *const p_element);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure into which to store
*		an element.
*
*	index
*		[in] Index of the element.
*
*	p_element
*		[in] Pointer to an element to store in the vector.
*
* RETURN VALUES
*	CL_SUCCESS if the element was successfully set.
*
*	CL_INSUFFICIENT_MEMORY if the vector could not be resized to accommodate
*	the new element.
*
* NOTES
*	cl_vector_set grows the vector as needed to accommodate the new element,
*	unless the grow_size parameter passed into the cl_vector_init function
*	was zero.
*
* SEE ALSO
*	Vector, cl_vector_get
*********/

/****f* Component Library: Vector/cl_vector_set_capacity
* NAME
*	cl_vector_set_capacity
*
* DESCRIPTION
*	The cl_vector_set_capacity function reserves memory in a vector for a
*	specified number of elements.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_set_capacity(IN cl_vector_t * const p_vector,
		       IN const size_t new_capacity);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose capacity to set.
*
*	new_capacity
*		[in] Total number of elements for which the vector should
*		allocate memory.
*
* RETURN VALUES
*	CL_SUCCESS if the capacity was successfully set.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to satisfy the
*	operation. The vector is left unchanged.
*
* NOTES
*	cl_vector_set_capacity increases the capacity of the vector. It does
*	not change the size of the vector. If the requested capacity is less
*	than the current capacity, the vector is left unchanged.
*
* SEE ALSO
*	Vector, cl_vector_get_capacity, cl_vector_set_size,
*	cl_vector_set_min_size
*********/

/****f* Component Library: Vector/cl_vector_set_size
* NAME
*	cl_vector_set_size
*
* DESCRIPTION
*	The cl_vector_set_size function resizes a vector, either increasing or
*	decreasing its size.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_set_size(IN cl_vector_t * const p_vector, IN const size_t size);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose size to set.
*
*	size
*		[in] Number of elements desired in the vector.
*
* RETURN VALUES
*	CL_SUCCESS if the size of the vector was set successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to complete the
*	operation. The vector is left unchanged.
*
* NOTES
*	cl_vector_set_size sets the vector to the specified size. If size is
*	smaller than the current size of the vector, the size is reduced.
*	The destructor function, if any, will be invoked for all elements that
*	are above size. Likewise, the constructor and initializer, if any, will
*	be invoked for all new elements.
*
*	This function can only fail if size is larger than the current capacity.
*
* SEE ALSO
*	Vector, cl_vector_get_size, cl_vector_set_min_size,
*	cl_vector_set_capacity
*********/

/****f* Component Library: Vector/cl_vector_set_min_size
* NAME
*	cl_vector_set_min_size
*
* DESCRIPTION
*	The cl_vector_set_min_size function resizes a vector to a specified size
*	if the vector is smaller than the specified size.
*
* SYNOPSIS
*/
cl_status_t
cl_vector_set_min_size(IN cl_vector_t * const p_vector,
		       IN const size_t min_size);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose minimum size to set.
*
*	min_size
*		[in] Minimum number of elements that the vector should contain.
*
* RETURN VALUES
*	CL_SUCCESS if the vector size is greater than or equal to min_size.  This
*	could indicate that the vector's capacity was increased to min_size or
*	that the vector was already of sufficient size.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to resize the vector.
*	The vector is left unchanged.
*
* NOTES
*	If min_size is smaller than the current size of the vector, the vector is
*	unchanged. The vector is unchanged if the size could not be changed due
*	to insufficient memory being available to perform the operation.
*
* SEE ALSO
*	Vector, cl_vector_get_size, cl_vector_set_size, cl_vector_set_capacity
*********/

/****f* Component Library: Vector/cl_vector_apply_func
* NAME
*	cl_vector_apply_func
*
* DESCRIPTION
*	The cl_vector_apply_func function invokes a specified function for every
*	element in a vector.
*
* SYNOPSIS
*/
void
cl_vector_apply_func(IN const cl_vector_t * const p_vector,
		     IN cl_pfn_vec_apply_t pfn_callback,
		     IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure whose elements to iterate.
*
*	pfn_callback
*		[in] Function invoked for every element in the array.
*		See the cl_pfn_vec_apply_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_vector_apply_func invokes the specified function for every element
*	in the vector, starting from the beginning of the vector.
*
* SEE ALSO
*	Vector, cl_vector_find_from_start, cl_vector_find_from_end,
*	cl_pfn_vec_apply_t
*********/

/****f* Component Library: Vector/cl_vector_find_from_start
* NAME
*	cl_vector_find_from_start
*
* DESCRIPTION
*	The cl_vector_find_from_start function uses a specified function to
*	search for elements in a vector starting from the lowest index.
*
* SYNOPSIS
*/
size_t
cl_vector_find_from_start(IN const cl_vector_t * const p_vector,
			  IN cl_pfn_vec_find_t pfn_callback,
			  IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure to inititalize.
*
*	pfn_callback
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_vec_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUES
*	Index of the element, if found.
*
*	Size of the vector if the element was not found.
*
* NOTES
*	cl_vector_find_from_start does not remove the found element from
*	the vector. The index of the element is returned when the function
*	provided by the pfn_callback parameter returns CL_SUCCESS.
*
* SEE ALSO
*	Vector, cl_vector_find_from_end, cl_vector_apply_func, cl_pfn_vec_find_t
*********/

/****f* Component Library: Vector/cl_vector_find_from_end
* NAME
*	cl_vector_find_from_end
*
* DESCRIPTION
*	The cl_vector_find_from_end function uses a specified function to search
*	for elements in a vector starting from the highest index.
*
* SYNOPSIS
*/
size_t
cl_vector_find_from_end(IN const cl_vector_t * const p_vector,
			IN cl_pfn_vec_find_t pfn_callback,
			IN const void *const context);
/*
* PARAMETERS
*	p_vector
*		[in] Pointer to a cl_vector_t structure to inititalize.
*
*	pfn_callback
*		[in] Function invoked to determine if a match was found.
*		See the cl_pfn_vec_find_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback function.
*
* RETURN VALUES
*	Index of the element, if found.
*
*	Size of the vector if the element was not found.
*
* NOTES
*	cl_vector_find_from_end does not remove the found element from
*	the vector. The index of the element is returned when the function
*	provided by the pfn_callback parameter returns CL_SUCCESS.
*
* SEE ALSO
*	Vector, cl_vector_find_from_start, cl_vector_apply_func,
*	cl_pfn_vec_find_t
*********/

END_C_DECLS
#endif				/* _CL_VECTOR_H_ */
