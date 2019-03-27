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
 *	Declaration of the quick pool.
 *	The quick pool manages a pool of objects.
 *	The pool can grow to meet demand, limited only by system memory.
 */

#ifndef _CL_QUICK_POOL_H_
#define _CL_QUICK_POOL_H_

#include <complib/cl_qcomppool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Quick Pool
* NAME
*	Quick Pool
*
* DESCRIPTION
*	The quick pool provides a self-contained and self-sustaining pool
*	of user defined objects.
*
*	To aid in object oriented design, the quick pool provides the user
*	the ability to specify callbacks that are invoked for each object for
*	construction, initialization, and destruction. Constructor and destructor
*	callback functions may not fail.
*
*	A quick pool does not return memory to the system as the user returns
*	objects to the pool. The only method of returning memory to the system is
*	to destroy the pool.
*
*	The quick pool operates on cl_pool_item_t structures that describe
*	objects. This can provides for more efficient memory use and operation.
*	If using a cl_pool_item_t is not desired, the Pool provides similar
*	functionality but operates on opaque objects.
*
*	The quick pool functions operates on a cl_qpool_t structure which should
*	be treated as opaque and should be manipulated only through the provided
*	functions.
*
* SEE ALSO
*	Structures:
*		cl_qpool_t, cl_pool_item_t
*
*	Callbacks:
*		cl_pfn_qpool_init_t, cl_pfn_qpool_dtor_t
*
*	Initialization/Destruction:
*		cl_qpool_construct, cl_qpool_init, cl_qpool_destroy
*
*	Manipulation:
*		cl_qpool_get, cl_qpool_put, cl_qpool_put_list, cl_qpool_grow
*
*	Attributes:
*		cl_is_qpool_inited, cl_qpool_count
*********/
/****d* Component Library: Quick Pool/cl_pfn_qpool_init_t
* NAME
*	cl_pfn_qpool_init_t
*
* DESCRIPTION
*	The cl_pfn_qpool_init_t function type defines the prototype for
*	functions used as constructor for objects being allocated by a
*	quick pool.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_qpool_init_t) (IN void *const p_object,
			    IN void *context,
			    OUT cl_pool_item_t ** const pp_pool_item);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object to initialize.
*
*	context
*		[in] Context provided in a call to cl_qpool_init.
*
* RETURN VALUES
*	Return CL_SUCCESS to indicate that initialization of the object
*	was successful and that initialization of further objects may continue.
*
*	Other cl_status_t values will be returned by cl_qcpool_init
*	and cl_qcpool_grow.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_qpool_init function.
*
*	The initializer is invoked once per allocated object, allowing the user
*	to perform any necessary initialization.  Returning a status other than
*	CL_SUCCESS aborts a grow operation, initiated either through cl_qcpool_init
*	or cl_qcpool_grow, causing the initiating function to fail.
*	Any non-CL_SUCCESS status will be returned by the function that initiated
*	the grow operation.
*
*	All memory for the object is pre-allocated.  Users should include space in
*	their objects for the cl_pool_item_t structure that will represent the
*	object to avoid having to allocate that structure in the initialization
*	callback.
*
*	When later performing a cl_qcpool_get call, the return value is a pointer
*	to the cl_pool_item_t returned by this function in the pp_pool_item
*	parameter.  Users must set pp_pool_item to a valid pointer to the
*	cl_pool_item_t representing the object if they return CL_SUCCESS.
*
* SEE ALSO
*	Quick Pool, cl_qpool_init
*********/

/****d* Component Library: Quick Pool/cl_pfn_qpool_dtor_t
* NAME
*	cl_pfn_qpool_dtor_t
*
* DESCRIPTION
*	The cl_pfn_qpool_dtor_t function type defines the prototype for
*	functions used as destructor for objects being deallocated by a
*	quick pool.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_qpool_dtor_t) (IN const cl_pool_item_t * const p_pool_item,
			 IN void *context);
/*
* PARAMETERS
*	p_pool_item
*		[in] Pointer to a cl_pool_item_t structure representing an object.
*
*	context
*		[in] Context provided in a call to cl_qpool_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_qpool_init function.
*
*	The destructor is invoked once per allocated object, allowing the user
*	to perform any necessary cleanup. Users should not attempt to deallocate
*	the memory for the object, as the quick pool manages object
*	allocation and deallocation.
*
* SEE ALSO
*	Quick Pool, cl_qpool_init
*********/

/****s* Component Library: Quick Pool/cl_qpool_t
* NAME
*	cl_qpool_t
*
* DESCRIPTION
*	Quick pool structure.
*
*	The cl_qpool_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_qpool {
	cl_qcpool_t qcpool;
	cl_pfn_qpool_init_t pfn_init;
	cl_pfn_qpool_dtor_t pfn_dtor;
	const void *context;
} cl_qpool_t;
/*
* FIELDS
*	qcpool
*		Quick composite pool that manages all objects.
*
*	pfn_init
*		Pointer to the user's initializer callback, used by the pool
*		to translate the quick composite pool's initializer callback to
*		a quick pool initializer callback.
*
*	pfn_dtor
*		Pointer to the user's destructor callback, used by the pool
*		to translate the quick composite pool's destructor callback to
*		a quick pool destructor callback.
*
*	context
*		User's provided context for callback functions, used by the pool
*		to when invoking callbacks.
*
* SEE ALSO
*	Quick Pool
*********/

/****f* Component Library: Quick Pool/cl_qpool_construct
* NAME
*	cl_qpool_construct
*
* DESCRIPTION
*	The cl_qpool_construct function constructs a quick pool.
*
* SYNOPSIS
*/
void cl_qpool_construct(IN cl_qpool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_qpool_init, cl_qpool_destroy, cl_is_qpool_inited.
*
*	Calling cl_qpool_construct is a prerequisite to calling any other
*	quick pool function except cl_pool_init.
*
* SEE ALSO
*	Quick Pool, cl_qpool_init, cl_qpool_destroy, cl_is_qpool_inited.
*********/

/****f* Component Library: Quick Pool/cl_is_qpool_inited
* NAME
*	cl_is_qpool_inited
*
* DESCRIPTION
*	The cl_is_qpool_inited function returns whether a quick pool was
*	successfully initialized.
*
* SYNOPSIS
*/
static inline uint32_t cl_is_qpool_inited(IN const cl_qpool_t * const p_pool)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_pool);
	return (cl_is_qcpool_inited(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure whose initialization state
*		to check.
*
* RETURN VALUES
*	TRUE if the quick pool was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a quick pool to determine if
*	invoking member functions is appropriate.
*
* SEE ALSO
*	Quick Pool
*********/

/****f* Component Library: Quick Pool/cl_qpool_init
* NAME
*	cl_qpool_init
*
* DESCRIPTION
*	The cl_qpool_init function initializes a quick pool for use.
*
* SYNOPSIS
*/
cl_status_t
cl_qpool_init(IN cl_qpool_t * const p_pool,
	      IN const size_t min_size,
	      IN const size_t max_size,
	      IN const size_t grow_size,
	      IN const size_t object_size,
	      IN cl_pfn_qpool_init_t pfn_initializer OPTIONAL,
	      IN cl_pfn_qpool_dtor_t pfn_destructor OPTIONAL,
	      IN const void *const context);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure to initialize.
*
*	min_size
*		[in] Minimum number of objects that the pool should support. All
*		necessary allocations to allow storing the minimum number of items
*		are performed at initialization time, and all necessary callbacks
*		successfully invoked.
*
*	max_size
*		[in] Maximum number of objects to which the pool is allowed to grow.
*		A value of zero specifies no maximum.
*
*	grow_size
*		[in] Number of objects to allocate when incrementally growing the pool.
*		A value of zero disables automatic growth.
*
*	object_size
*		[in] Size, in bytes, of each object.
*
*	pfn_initializer
*		[in] Initialization callback to invoke for every new object when
*		growing the pool. This parameter is optional and may be NULL. If NULL,
*		the pool assumes the cl_pool_item_t structure describing objects is
*		located at the head of each object. See the cl_pfn_qpool_init_t
*		function type declaration for details about the callback function.
*
*	pfn_destructor
*		[in] Destructor callback to invoke for every object before memory for
*		that object is freed. This parameter is optional and may be NULL.
*		See the cl_pfn_qpool_dtor_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the quick pool was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize the
*	quick pool.
*
*	CL_INVALID_SETTING if a the maximum size is non-zero and less than the
*	minimum size.
*
*	Other cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter.
*
* NOTES
*	cl_qpool_init initializes, and if necessary, grows the pool to
*	the capacity desired.
*
* SEE ALSO
*	Quick Pool, cl_qpool_construct, cl_qpool_destroy,
*	cl_qpool_get, cl_qpool_put, cl_qpool_grow,
*	cl_qpool_count, cl_pfn_qpool_init_t, cl_pfn_qpool_init_t,
*	cl_pfn_qpool_dtor_t
*********/

/****f* Component Library: Quick Pool/cl_qpool_destroy
* NAME
*	cl_qpool_destroy
*
* DESCRIPTION
*	The cl_qpool_destroy function destroys a quick pool.
*
* SYNOPSIS
*/
static inline void cl_qpool_destroy(IN cl_qpool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	cl_qcpool_destroy(&p_pool->qcpool);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	All memory allocated for objects is freed. The destructor callback,
*	if any, will be invoked for every allocated object. Further operations
*	on the pool should not be attempted after cl_qpool_destroy
*	is invoked.
*
*	This function should only be called after a call to
*	cl_qpool_construct or cl_qpool_init.
*
*	In a debug build, cl_qpool_destroy asserts that all objects are in
*	the pool.
*
* SEE ALSO
*	Quick Pool, cl_qpool_construct, cl_qpool_init
*********/

/****f* Component Library: Quick Pool/cl_qpool_count
* NAME
*	cl_qpool_count
*
* DESCRIPTION
*	The cl_qpool_count function returns the number of available objects
*	in a quick pool.
*
* SYNOPSIS
*/
static inline size_t cl_qpool_count(IN cl_qpool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_count(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure for which the number of
*		available objects is requested.
*
* RETURN VALUE
*	Returns the number of objects available in the specified quick pool.
*
* SEE ALSO
*	Quick Pool
*********/

/****f* Component Library: Quick Pool/cl_qpool_get
* NAME
*	cl_qpool_get
*
* DESCRIPTION
*	The cl_qpool_get function retrieves an object from a
*	quick pool.
*
* SYNOPSIS
*/
static inline cl_pool_item_t *cl_qpool_get(IN cl_qpool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_get(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure from which to retrieve
*		an object.
*
* RETURN VALUES
*	Returns a pointer to a cl_pool_item_t for an object.
*
*	Returns NULL if the pool is empty and can not be grown automatically.
*
* NOTES
*	cl_qpool_get returns the object at the head of the pool. If the pool is
*	empty, it is automatically grown to accommodate this request unless the
*	grow_size parameter passed to the cl_qpool_init function was zero.
*
* SEE ALSO
*	Quick Pool, cl_qpool_get_tail, cl_qpool_put, cl_qpool_grow, cl_qpool_count
*********/

/****f* Component Library: Quick Pool/cl_qpool_put
* NAME
*	cl_qpool_put
*
* DESCRIPTION
*	The cl_qpool_put function returns an object to the head of a quick pool.
*
* SYNOPSIS
*/
static inline void
cl_qpool_put(IN cl_qpool_t * const p_pool,
	     IN cl_pool_item_t * const p_pool_item)
{
	CL_ASSERT(p_pool);
	cl_qcpool_put(&p_pool->qcpool, p_pool_item);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure to which to return
*		an object.
*
*	p_pool_item
*		[in] Pointer to a cl_pool_item_t structure for the object
*		being returned.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_qpool_put places the returned object at the head of the pool.
*
*	The object specified by the p_pool_item parameter must have been
*	retrieved from the pool by a previous call to cl_qpool_get.
*
* SEE ALSO
*	Quick Pool, cl_qpool_put_tail, cl_qpool_get
*********/

/****f* Component Library: Quick Pool/cl_qpool_put_list
* NAME
*	cl_qpool_put_list
*
* DESCRIPTION
*	The cl_qpool_put_list function returns a list of objects to the head
*	of a quick pool.
*
* SYNOPSIS
*/
static inline void
cl_qpool_put_list(IN cl_qpool_t * const p_pool, IN cl_qlist_t * const p_list)
{
	CL_ASSERT(p_pool);
	cl_qcpool_put_list(&p_pool->qcpool, p_list);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure to which to return
*		a list of objects.
*
*	p_list
*		[in] Pointer to a cl_qlist_t structure for the list of objects
*		being returned.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_qpool_put_list places the returned objects at the head of the pool.
*
*	The objects in the list specified by the p_list parameter must have been
*	retrieved from the pool by a previous call to cl_qpool_get.
*
* SEE ALSO
*	Quick Pool, cl_qpool_put, cl_qpool_put_tail, cl_qpool_get
*********/

/****f* Component Library: Quick Pool/cl_qpool_grow
* NAME
*	cl_qpool_grow
*
* DESCRIPTION
*	The cl_qpool_grow function grows a quick pool by
*	the specified number of objects.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_qpool_grow(IN cl_qpool_t * const p_pool, IN const size_t obj_count)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_grow(&p_pool->qcpool, obj_count));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qpool_t structure whose capacity to grow.
*
*	obj_count
*		[in] Number of objects by which to grow the pool.
*
* RETURN VALUES
*	CL_SUCCESS if the quick pool grew successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to grow the
*	quick pool.
*
*	cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter passed to the
*	cl_qpool_init function.
*
* NOTES
*	It is not necessary to call cl_qpool_grow if the pool is
*	configured to grow automatically.
*
* SEE ALSO
*	Quick Pool
*********/

END_C_DECLS
#endif				/* _CL_QUICK_POOL_H_ */
