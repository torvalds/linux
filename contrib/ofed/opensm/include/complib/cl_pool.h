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
 *	Declaration of the pool.
 *	The pool manages a pool of objects.
 *	The pool can grow to meet demand, limited only by system memory.
 */

#ifndef _CL_POOL_H_
#define _CL_POOL_H_

#include <complib/cl_qcomppool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Pool
* NAME
*	Pool
*
* DESCRIPTION
*	The pool provides a self-contained and self-sustaining pool
*	of user defined objects.
*
*	To aid in object oriented design, the pool provides the user
*	the ability to specify callbacks that are invoked for each object for
*	construction, initialization, and destruction. Constructor and destructor
*	callback functions may not fail.
*
*	A pool does not return memory to the system as the user returns
*	objects to the pool. The only method of returning memory to the system is
*	to destroy the pool.
*
*	The Pool functions operate on a cl_pool_t structure which should be treated
*	as opaque and should be manipulated only through the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_pool_t
*
*	Callbacks:
*		cl_pfn_pool_init_t, cl_pfn_pool_dtor_t
*
*	Initialization/Destruction:
*		cl_pool_construct, cl_pool_init, cl_pool_destroy
*
*	Manipulation:
*		cl_pool_get, cl_pool_put, cl_pool_grow
*
*	Attributes:
*		cl_is_pool_inited, cl_pool_count
*********/
/****d* Component Library: Pool/cl_pfn_pool_init_t
* NAME
*	cl_pfn_pool_init_t
*
* DESCRIPTION
*	The cl_pfn_pool_init_t function type defines the prototype for
*	functions used as initializers for objects being allocated by a
*	pool.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_pool_init_t) (IN void *const p_object, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object to initialize.
*
*	context
*		[in] Context provided in a call to cl_pool_init.
*
* RETURN VALUES
*	Return CL_SUCCESS to indicates that initialization of the object
*	was successful and initialization of further objects may continue.
*
*	Other cl_status_t values will be returned by cl_pool_init
*	and cl_pool_grow.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_pool_init function.
*
*	The initializer is invoked once per allocated object, allowing the user
*	to trap initialization failures. Returning a status other than CL_SUCCESS
*	aborts a grow operation, initiated either through cl_pool_init or
*	cl_pool_grow, and causes the initiating function to fail.
*	Any non-CL_SUCCESS status will be returned by the function that initiated
*	the grow operation.
*
* SEE ALSO
*	Pool, cl_pool_init, cl_pool_grow
*********/

/****d* Component Library: Pool/cl_pfn_pool_dtor_t
* NAME
*	cl_pfn_pool_dtor_t
*
* DESCRIPTION
*	The cl_pfn_pool_dtor_t function type defines the prototype for
*	functions used as destructor for objects being deallocated by a
*	pool.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_pool_dtor_t) (IN void *const p_object, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object to destruct.
*
*	context
*		[in] Context provided in the call to cl_pool_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_pool_init function.
*
*	The destructor is invoked once per allocated object, allowing the user
*	to perform any necessary cleanup. Users should not attempt to deallocate
*	the memory for the object, as the pool manages object
*	allocation and deallocation.
*
* SEE ALSO
*	Pool, cl_pool_init
*********/

/****s* Component Library: Pool/cl_pool_t
* NAME
*	cl_pool_t
*
* DESCRIPTION
*	pool structure.
*
*	The cl_pool_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_pool {
	cl_qcpool_t qcpool;
	cl_pfn_pool_init_t pfn_init;
	cl_pfn_pool_dtor_t pfn_dtor;
	const void *context;
} cl_pool_t;
/*
* FIELDS
*	qcpool
*		Quick composite pool that manages all objects.
*
*	pfn_init
*		Pointer to the user's initializer callback, used by the pool
*		to translate the quick composite pool's initializer callback to
*		a pool initializer callback.
*
*	pfn_dtor
*		Pointer to the user's destructor callback, used by the pool
*		to translate the quick composite pool's destructor callback to
*		a pool destructor callback.
*
*	context
*		User's provided context for callback functions, used by the pool
*		to when invoking callbacks.
*
* SEE ALSO
*	Pool
*********/

/****f* Component Library: Pool/cl_pool_construct
* NAME
*	cl_pool_construct
*
* DESCRIPTION
*	The cl_pool_construct function constructs a pool.
*
* SYNOPSIS
*/
void cl_pool_construct(IN cl_pool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_pool_init, cl_pool_destroy, and cl_is_pool_inited.
*
*	Calling cl_pool_construct is a prerequisite to calling any other
*	pool function except cl_pool_init.
*
* SEE ALSO
*	Pool, cl_pool_init, cl_pool_destroy, cl_is_pool_inited
*********/

/****f* Component Library: Pool/cl_is_pool_inited
* NAME
*	cl_is_pool_inited
*
* DESCRIPTION
*	The cl_is_pool_inited function returns whether a pool was successfully
*	initialized.
*
* SYNOPSIS
*/
static inline uint32_t cl_is_pool_inited(IN const cl_pool_t * const p_pool)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_pool);
	return (cl_is_qcpool_inited(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure whose initialization state
*		to check.
*
* RETURN VALUES
*	TRUE if the pool was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a pool to determine if invoking member
*	functions is appropriate.
*
* SEE ALSO
*	Pool
*********/

/****f* Component Library: Pool/cl_pool_init
* NAME
*	cl_pool_init
*
* DESCRIPTION
*	The cl_pool_init function initializes a pool for use.
*
* SYNOPSIS
*/
cl_status_t
cl_pool_init(IN cl_pool_t * const p_pool,
	     IN const size_t min_count,
	     IN const size_t max_count,
	     IN const size_t grow_size,
	     IN const size_t object_size,
	     IN cl_pfn_pool_init_t pfn_initializer OPTIONAL,
	     IN cl_pfn_pool_dtor_t pfn_destructor OPTIONAL,
	     IN const void *const context);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure to initialize.
*
*	min_count
*		[in] Minimum number of objects that the pool should support. All
*		necessary allocations to allow storing the minimum number of items
*		are performed at initialization time, and all necessary callbacks
*		invoked.
*
*	max_count
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
*		growing the pool. This parameter is optional and may be NULL.
*		See the cl_pfn_pool_init_t function type declaration for details
*		about the callback function.
*
*	pfn_destructor
*		[in] Destructor callback to invoke for every object before memory for
*		that object is freed. This parameter is optional and may be NULL.
*		See the cl_pfn_pool_dtor_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the pool was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize the
*	pool.
*
*	CL_INVALID_SETTING if a the maximum size is non-zero and less than the
*	minimum size.
*
*	Other cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter.
*
* NOTES
*	cl_pool_init initializes, and if necessary, grows the pool to
*	the capacity desired.
*
* SEE ALSO
*	Pool, cl_pool_construct, cl_pool_destroy,
*	cl_pool_get, cl_pool_put, cl_pool_grow,
*	cl_pool_count, cl_pfn_pool_init_t, cl_pfn_pool_dtor_t
*********/

/****f* Component Library: Pool/cl_pool_destroy
* NAME
*	cl_pool_destroy
*
* DESCRIPTION
*	The cl_pool_destroy function destroys a pool.
*
* SYNOPSIS
*/
static inline void cl_pool_destroy(IN cl_pool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	cl_qcpool_destroy(&p_pool->qcpool);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	All memory allocated for objects is freed. The destructor callback,
*	if any, will be invoked for every allocated object. Further operations
*	on the pool should not be attempted after cl_pool_destroy
*	is invoked.
*
*	This function should only be called after a call to
*	cl_pool_construct or cl_pool_init.
*
*	In a debug build, cl_pool_destroy asserts that all objects are in
*	the pool.
*
* SEE ALSO
*	Pool, cl_pool_construct, cl_pool_init
*********/

/****f* Component Library: Pool/cl_pool_count
* NAME
*	cl_pool_count
*
* DESCRIPTION
*	The cl_pool_count function returns the number of available objects
*	in a pool.
*
* SYNOPSIS
*/
static inline size_t cl_pool_count(IN cl_pool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_count(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure for which the number of
*		available objects is requested.
*
* RETURN VALUE
*	Returns the number of objects available in the specified pool.
*
* SEE ALSO
*	Pool
*********/

/****f* Component Library: Pool/cl_pool_get
* NAME
*	cl_pool_get
*
* DESCRIPTION
*	The cl_pool_get function retrieves an object from a pool.
*
* SYNOPSIS
*/
static inline void *cl_pool_get(IN cl_pool_t * const p_pool)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_pool);

	p_pool_obj = (cl_pool_obj_t *) cl_qcpool_get(&p_pool->qcpool);
	if (!p_pool_obj)
		return (NULL);

	CL_ASSERT(p_pool_obj->p_object);
	return ((void *)p_pool_obj->p_object);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure from which to retrieve
*		an object.
*
* RETURN VALUES
*	Returns a pointer to an object.
*
*	Returns NULL if the pool is empty and can not be grown automatically.
*
* NOTES
*	cl_pool_get returns the object at the head of the pool. If the pool is
*	empty, it is automatically grown to accommodate this request unless the
*	grow_size parameter passed to the cl_pool_init function was zero.
*
* SEE ALSO
*	Pool, cl_pool_get_tail, cl_pool_put, cl_pool_grow, cl_pool_count
*********/

/****f* Component Library: Pool/cl_pool_put
* NAME
*	cl_pool_put
*
* DESCRIPTION
*	The cl_pool_put function returns an object to a pool.
*
* SYNOPSIS
*/
static inline void
cl_pool_put(IN cl_pool_t * const p_pool, IN void *const p_object)
{
	cl_pool_obj_t *p_pool_obj;

	CL_ASSERT(p_pool);
	CL_ASSERT(p_object);

	/* Calculate the offset to the list object representing this object. */
	p_pool_obj = (cl_pool_obj_t *)
	    (((uint8_t *) p_object) - sizeof(cl_pool_obj_t));

	/* good sanity check */
	CL_ASSERT(p_pool_obj->p_object == p_object);

	cl_qcpool_put(&p_pool->qcpool, &p_pool_obj->pool_item);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure to which to return
*		an object.
*
*	p_object
*		[in] Pointer to an object to return to the pool.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_pool_put places the returned object at the head of the pool.
*
*	The object specified by the p_object parameter must have been
*	retrieved from the pool by a previous call to cl_pool_get.
*
* SEE ALSO
*	Pool, cl_pool_put_tail, cl_pool_get
*********/

/****f* Component Library: Pool/cl_pool_grow
* NAME
*	cl_pool_grow
*
* DESCRIPTION
*	The cl_pool_grow function grows a pool by
*	the specified number of objects.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_pool_grow(IN cl_pool_t * const p_pool, IN const size_t obj_count)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_grow(&p_pool->qcpool, obj_count));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_pool_t structure whose capacity to grow.
*
*	obj_count
*		[in] Number of objects by which to grow the pool.
*
* RETURN VALUES
*	CL_SUCCESS if the pool grew successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to grow the
*	pool.
*
*	cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter passed to the
*	cl_pool_init function.
*
* NOTES
*	It is not necessary to call cl_pool_grow if the pool is
*	configured to grow automatically.
*
* SEE ALSO
*	Pool
*********/

END_C_DECLS
#endif				/* _CL_POOL_H_ */
