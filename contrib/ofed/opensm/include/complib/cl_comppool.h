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
 *	Declaration of the composite pool.
 *	The composite pool managers a pool of composite objects.  A composite object is an object
 *	that is made of multiple sub objects.
 *	The pool can grow to meet demand, limited only by system memory.
 */

#ifndef _CL_COMP_POOL_H_
#define _CL_COMP_POOL_H_

#include <complib/cl_qcomppool.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Composite Pool
* NAME
*	Composite Pool
*
* DESCRIPTION
*	The Composite Pool provides a self-contained and self-sustaining pool of
*	user defined composite objects.
*
*	A composite object is an object that is composed of one or more
*	sub-objects, each of which needs to be treated separately for
*	initialization. Objects can be retrieved from the pool as long as there
*	is memory in the system.
*
*	To aid in object oriented design, the composite pool provides the user
*	the ability to specify callbacks that are invoked for each object for
*	construction, initialization, and destruction. Constructor and destructor
*	callback functions may not fail.
*
*	A composite pool does not return memory to the system as the user returns
*	objects to the pool. The only method of returning memory to the system is
*	to destroy the pool.
*
*	The composite pool functions operates on a cl_cpool_t structure which
*	should be treated as opaque and should be manipulated only through the
*	provided functions.
*
* SEE ALSO
*	Structures:
*		cl_cpool_t
*
*	Callbacks:
*		cl_pfn_cpool_init_t, cl_pfn_cpool_dtor_t
*
*	Initialization/Destruction:
*		cl_cpool_construct, cl_cpool_init, cl_cpool_destroy
*
*	Manipulation:
*		cl_cpool_get, cl_cpool_put, cl_cpool_grow
*
*	Attributes:
*		cl_is_cpool_inited, cl_cpool_count
*********/
/****d* Component Library: Composite Pool/cl_pfn_cpool_init_t
* NAME
*	cl_pfn_cpool_init_t
*
* DESCRIPTION
*	The cl_pfn_cpool_init_t function type defines the prototype for
*	functions used as initializers for objects being allocated by a
*	composite pool.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_cpool_init_t) (IN void **const p_comp_array,
			    IN const uint32_t num_components, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object to initialize.
*
*	context
*		[in] Context provided in a call to cl_cpool_init.
*
* RETURN VALUES
*	Return CL_SUCCESS to indicates that initialization of the object
*	was successful and that initialization of further objects may continue.
*
*	Other cl_status_t values will be returned by cl_cpool_init
*	and cl_cpool_grow.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_cpool_init function.
*
*	The initializer is invoked once per allocated object, allowing the user
*	to chain components to form a composite object and perform any necessary
*	initialization.  Returning a status other than CL_SUCCESS aborts a grow
*	operation, initiated either through cl_cpool_init or cl_cpool_grow, and
*	causes the initiating function to fail.  Any non-CL_SUCCESS status will
*	be returned by the function that initiated the grow operation.
*
*	All memory for the requested number of components is pre-allocated.
*
*	When later performing a cl_cpool_get call, the return value is a pointer
*	to the first component.
*
* SEE ALSO
*	Composite Pool, cl_cpool_init, cl_cpool_grow
*********/

/****d* Component Library: Composite Pool/cl_pfn_cpool_dtor_t
* NAME
*	cl_pfn_cpool_dtor_t
*
* DESCRIPTION
*	The cl_pfn_cpool_dtor_t function type defines the prototype for
*	functions used as destructor for objects being deallocated by a
*	composite pool.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_cpool_dtor_t) (IN void *const p_object, IN void *context);
/*
* PARAMETERS
*	p_object
*		[in] Pointer to an object to destruct.
*
*	context
*		[in] Context provided in the call to cl_cpool_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_cpool_init function.
*
*	The destructor is invoked once per allocated object, allowing the user
*	to perform any necessary cleanup. Users should not attempt to deallocate
*	the memory for the composite object, as the composite pool manages
*	object allocation and deallocation.
*
* SEE ALSO
*	Composite Pool, cl_cpool_init
*********/

/****s* Component Library: Composite Pool/cl_cpool_t
* NAME
*	cl_cpool_t
*
* DESCRIPTION
*	Composite pool structure.
*
*	The cl_cpool_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_cpool {
	cl_qcpool_t qcpool;
	cl_pfn_cpool_init_t pfn_init;
	cl_pfn_cpool_dtor_t pfn_dtor;
	const void *context;
} cl_cpool_t;
/*
* FIELDS
*	qcpool
*		Quick composite pool that manages all objects.
*
*	pfn_init
*		Pointer to the user's initializer callback, used by the pool
*		to translate the quick composite pool's initializer callback to
*		a composite pool initializer callback.
*
*	pfn_dtor
*		Pointer to the user's destructor callback, used by the pool
*		to translate the quick composite pool's destructor callback to
*		a composite pool destructor callback.
*
*	context
*		User's provided context for callback functions, used by the pool
*		to when invoking callbacks.
*
* SEE ALSO
*	Composite Pool
*********/

/****f* Component Library: Composite Pool/cl_cpool_construct
* NAME
*	cl_cpool_construct
*
* DESCRIPTION
*	The cl_cpool_construct function constructs a composite pool.
*
* SYNOPSIS
*/
void cl_cpool_construct(IN cl_cpool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_pool_init, cl_cpool_destroy, cl_is_cpool_inited.
*
*	Calling cl_cpool_construct is a prerequisite to calling any other
*	composite pool function except cl_cpool_init.
*
* SEE ALSO
*	Composite Pool, cl_cpool_init, cl_cpool_destroy, cl_is_cpool_inited
*********/

/****f* Component Library: Composite Pool/cl_is_cpool_inited
* NAME
*	cl_is_cpool_inited
*
* DESCRIPTION
*	The cl_is_cpool_inited function returns whether a composite pool was
*	successfully initialized.
*
* SYNOPSIS
*/
static inline boolean_t cl_is_cpool_inited(IN const cl_cpool_t * const p_pool)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_pool);
	return (cl_is_qcpool_inited(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure whose initialization state
*		to check.
*
* RETURN VALUES
*	TRUE if the composite pool was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a composite pool to determine if invoking
*	member functions is appropriate.
*
* SEE ALSO
*	Composite Pool
*********/

/****f* Component Library: Composite Pool/cl_cpool_init
* NAME
*	cl_cpool_init
*
* DESCRIPTION
*	The cl_cpool_init function initializes a composite pool for use.
*
* SYNOPSIS
*/
cl_status_t
cl_cpool_init(IN cl_cpool_t * const p_pool,
	      IN const size_t min_size,
	      IN const size_t max_size,
	      IN const size_t grow_size,
	      IN size_t * const component_sizes,
	      IN const uint32_t num_components,
	      IN cl_pfn_cpool_init_t pfn_initializer OPTIONAL,
	      IN cl_pfn_cpool_dtor_t pfn_destructor OPTIONAL,
	      IN const void *const context);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure to initialize.
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
*	component_sizes
*		[in] Pointer to the first entry in an array of sizes describing,
*		in order, the sizes of the components that make up a composite object.
*
*	num_components
*		[in] Number of components that make up a composite object.
*
*	pfn_initializer
*		[in] Initialization callback to invoke for every new object when
*		growing the pool. This parameter may be NULL only if the objects
*		stored in the composite pool consist of only one component.
*		See the cl_pfn_cpool_init function type declaration for details
*		about the callback function.
*
*	pfn_destructor
*		[in] Destructor callback to invoke for every object before memory for
*		that object is freed. This parameter is optional and may be NULL.
*		See the cl_pfn_cpool_dtor function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the composite pool was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize the
*	composite pool.
*
*	CL_INVALID_SETTING if a NULL constructor was provided for composite objects
*	consisting of more than one component.  Also returns CL_INVALID_SETTING if
*	the maximum size is non-zero and less than the minimum size.
*
*	Other cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter.
*
* NOTES
*	cl_cpool_init initializes, and if necessary, grows the pool to
*	the capacity desired.
*
* SEE ALSO
*	Composite Pool, cl_cpool_construct, cl_cpool_destroy,
*	cl_cpool_get, cl_cpool_put, cl_cpool_grow,
*	cl_cpool_count, cl_pfn_cpool_ctor_t, cl_pfn_cpool_init_t,
*	cl_pfn_cpool_dtor_t
*********/

/****f* Component Library: Composite Pool/cl_cpool_destroy
* NAME
*	cl_cpool_destroy
*
* DESCRIPTION
*	The cl_cpool_destroy function destroys a composite pool.
*
* SYNOPSIS
*/
static inline void cl_cpool_destroy(IN cl_cpool_t * const p_pool)
{
	CL_ASSERT(p_pool);

	cl_qcpool_destroy(&p_pool->qcpool);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	All memory allocated for composite objects is freed. The destructor
*	callback, if any, will be invoked for every allocated object. Further
*	operations on the composite pool should not be attempted after
*	cl_cpool_destroy is invoked.
*
*	This function should only be called after a call to cl_cpool_construct.
*
*	In a debug build, cl_cpool_destroy asserts that all objects are in
*	the pool.
*
* SEE ALSO
*	Composite Pool, cl_cpool_construct, cl_cpool_init
*********/

/****f* Component Library: Composite Pool/cl_cpool_count
* NAME
*	cl_cpool_count
*
* DESCRIPTION
*	The cl_cpool_count function returns the number of available objects
*	in a composite pool.
*
* SYNOPSIS
*/
static inline size_t cl_cpool_count(IN cl_cpool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_count(&p_pool->qcpool));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure for which the number of
*		available objects is requested.
*
* RETURN VALUE
*	Returns the number of objects available in the specified
*	composite pool.
*
* SEE ALSO
*	Composite Pool
*********/

/****f* Component Library: Composite Pool/cl_cpool_get
* NAME
*	cl_cpool_get
*
* DESCRIPTION
*	The cl_cpool_get function retrieves an object from a
*	composite pool.
*
* SYNOPSIS
*/
static inline void *cl_cpool_get(IN cl_cpool_t * const p_pool)
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
*		[in] Pointer to a cl_cpool_t structure from which to retrieve
*		an object.
*
* RETURN VALUES
*	Returns a pointer to the first component of a composite object.
*
*	Returns NULL if the pool is empty and can not be grown automatically.
*
* NOTES
*	cl_cpool_get returns the object at the head of the pool. If the pool is
*	empty, it is automatically grown to accommodate this request unless the
*	grow_size parameter passed to the cl_cpool_init function was zero.
*
* SEE ALSO
*	Composite Pool, cl_cpool_get_tail, cl_cpool_put, cl_cpool_grow,
*	cl_cpool_count
*********/

/****f* Component Library: Composite Pool/cl_cpool_put
* NAME
*	cl_cpool_put
*
* DESCRIPTION
*	The cl_cpool_put function returns an object to a composite pool.
*
* SYNOPSIS
*/
static inline void
cl_cpool_put(IN cl_cpool_t * const p_pool, IN void *const p_object)
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
*		[in] Pointer to a cl_cpool_t structure to which to return
*		an object.
*
*	p_object
*		[in] Pointer to the first component of an object to return to the pool.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	cl_cpool_put places the returned object at the head of the pool.
*
*	The object specified by the p_object parameter must have been
*	retrieved from the pool by a previous call to cl_cpool_get.
*
* SEE ALSO
*	Composite Pool, cl_cpool_put_tail, cl_cpool_get
*********/

/****f* Component Library: Composite Pool/cl_cpool_grow
* NAME
*	cl_cpool_grow
*
* DESCRIPTION
*	The cl_cpool_grow function grows a composite pool by
*	the specified number of objects.
*
* SYNOPSIS
*/
static inline cl_status_t
cl_cpool_grow(IN cl_cpool_t * const p_pool, IN const uint32_t obj_count)
{
	CL_ASSERT(p_pool);
	return (cl_qcpool_grow(&p_pool->qcpool, obj_count));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_cpool_t structure whose capacity to grow.
*
*	obj_count
*		[in] Number of objects by which to grow the pool.
*
* RETURN VALUES
*	CL_SUCCESS if the composite pool grew successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to grow the
*	composite pool.
*
*	cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter passed to the
*	cl_cpool_init function.
*
* NOTES
*	It is not necessary to call cl_cpool_grow if the pool is
*	configured to grow automatically.
*
* SEE ALSO
*	Composite Pool
*********/

END_C_DECLS
#endif				/* _CL_COMP_POOL_H_ */
