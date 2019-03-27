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
 *	Declaration of the quick composite pool.  The quick composite pool
 *	manages a pool of composite objects.  A composite object is an object
 *	that is made of multiple sub objects.
 *	It can grow to meet demand, limited only by system memory.
 */

#ifndef _CL_QUICK_COMPOSITE_POOL_H_
#define _CL_QUICK_COMPOSITE_POOL_H_

#include <complib/cl_types.h>
#include <complib/cl_qlist.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Quick Composite Pool
* NAME
*	Quick Composite Pool
*
* DESCRIPTION
*	The Quick Composite Pool provides a self-contained and self-sustaining
*	pool of user defined composite objects.
*
*	A composite object is an object that is composed of one or more
*	sub-objects, each of which needs to be treated separately for
*	initialization. Objects can be retrieved from the pool as long as there
*	is memory in the system.
*
*	To aid in object oriented design, the Quick Composite Pool provides users
*	the ability to specify callbacks that are invoked for each object for
*	construction, initialization, and destruction. Constructor and destructor
*	callback functions may not fail.
*
*	A Quick Composite Pool does not return memory to the system as the user
*	returns objects to the pool. The only method of returning memory to the
*	system is to destroy the pool.
*
*	The Quick Composite Pool operates on cl_pool_item_t structures that
*	describe composite objects. This provides for more efficient memory use.
*	If using a cl_pool_item_t is not desired, the Composite Pool provides
*	similar functionality but operates on opaque objects.
*
*	The Quick Composit Pool functions operate on a cl_qcpool_t structure
*	which should be treated as opaque and should be manipulated only through
*	the provided functions.
*
* SEE ALSO
*	Structures:
*		cl_qcpool_t, cl_pool_item_t
*
*	Callbacks:
*		cl_pfn_qcpool_init_t, cl_pfn_qcpool_dtor_t
*
*	Initialization/Destruction:
*		cl_qcpool_construct, cl_qcpool_init, cl_qcpool_destroy
*
*	Manipulation:
*		cl_qcpool_get, cl_qcpool_put, cl_qcpool_put_list, cl_qcpool_grow
*
*	Attributes:
*		cl_is_qcpool_inited, cl_qcpool_count
*********/
/****s* Component Library: Quick Composite Pool/cl_pool_item_t
* NAME
*	cl_pool_item_t
*
* DESCRIPTION
*	The cl_pool_item_t structure is used by pools to store objects.
*
* SYNOPSIS
*/
typedef struct _cl_pool_item {
	cl_list_item_t list_item;
#ifdef _DEBUG_
	/* Pointer to the owner pool used for sanity checks. */
	struct _cl_qcpool *p_pool;
#endif
} cl_pool_item_t;
/*
* FIELDS
*	list_item
*		Used internally by the pool. Users should not use this field.
*
*	p_pool
*		Used internally by the pool in debug builds to check for consistency.
*
* NOTES
*	The pool item structure is defined in such a way as to safely allow
*	users to cast from a pool item to a list item for storing items
*	retrieved from a quick pool in a quick list.
*
* SEE ALSO
*	Quick Composite Pool, cl_list_item_t
*********/

/****i* Component Library: Quick List/cl_pool_obj_t
* NAME
*	cl_pool_obj_t
*
* DESCRIPTION
*	The cl_pool_obj_t structure is used by pools to store objects.
*
* SYNOPSIS
*/
typedef struct _cl_pool_obj {
	/* The pool item must be the first item to allow casting. */
	cl_pool_item_t pool_item;
	const void *p_object;
} cl_pool_obj_t;
/*
* FIELDS
*	pool_item
*		Used internally by the pool. Users should not use this field.
*
*	p_object
*		Pointer to the user's object being stored in the pool.
*
* NOTES
*	The pool object structure is used by non-quick pools to store object.
*
* SEE ALSO
*	cl_pool_item_t
*********/

/****d* Component Library: Quick Composite Pool/cl_pfn_qcpool_init_t
* NAME
*	cl_pfn_qcpool_init_t
*
* DESCRIPTION
*	The cl_pfn_qcpool_init_t function type defines the prototype for
*	functions used as initializer for objects being allocated by a
*	quick composite pool.
*
* SYNOPSIS
*/
typedef cl_status_t
    (*cl_pfn_qcpool_init_t) (IN void **const p_comp_array,
			     IN const uint32_t num_components,
			     IN void *context,
			     OUT cl_pool_item_t ** const pp_pool_item);
/*
* PARAMETERS
*	p_comp_array
*		[in] Pointer to the first entry in an array of pointers, each of
*		which points to a component that makes up a composite object.
*
*	num_components
*		[in] Number of components that in the component array.
*
*	context
*		[in] Context provided in a call to cl_qcpool_init.
*
*	pp_pool_item
*		[out] Users should set this pointer to reference the cl_pool_item_t
*		structure that represents the composite object.  This pointer must
*		not be NULL if the function returns CL_SUCCESS.
*
* RETURN VALUE
*	Return CL_SUCCESS to indicate that initialization of the object
*	was successful and that initialization of further objects may continue.
*
*	Other cl_status_t values will be returned by cl_qcpool_init
*	and cl_qcpool_grow.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as a parameter to the
*	cl_qcpool_init function.
*
*	The initializer is invoked once per allocated object, allowing the user
*	to chain components to form a composite object and perform any necessary
*	initialization.  Returning a status other than CL_SUCCESS aborts a grow
*	operation, initiated either through cl_qcpool_init or cl_qcpool_grow,
*	and causes the initiating function to fail.  Any non-CL_SUCCESS status
*	will be returned by the function that initiated the grow operation.
*
*	All memory for the requested number of components is pre-allocated.  Users
*	should include space in one of their components for the cl_pool_item_t
*	structure that will represent the composite object to avoid having to
*	allocate that structure in the initialization callback.  Alternatively,
*	users may specify an additional component for the cl_pool_item_t structure.
*
*	When later performing a cl_qcpool_get call, the return value is a pointer
*	to the cl_pool_item_t returned by this function in the pp_pool_item
*	parameter. Users must set pp_pool_item to a valid pointer to the
*	cl_pool_item_t representing the object if they return CL_SUCCESS.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_init
*********/

/****d* Component Library: Quick Composite Pool/cl_pfn_qcpool_dtor_t
* NAME
*	cl_pfn_qcpool_dtor_t
*
* DESCRIPTION
*	The cl_pfn_qcpool_dtor_t function type defines the prototype for
*	functions used as destructor for objects being deallocated by a
*	quick composite pool.
*
* SYNOPSIS
*/
typedef void
 (*cl_pfn_qcpool_dtor_t) (IN const cl_pool_item_t * const p_pool_item,
			  IN void *context);
/*
* PARAMETERS
*	p_pool_item
*		[in] Pointer to a cl_pool_item_t structure representing an object.
*
*	context
*		[in] Context provided in a call to cl_qcpool_init.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	This function type is provided as function prototype reference for
*	the function provided by the user as an optional parameter to the
*	cl_qcpool_init function.
*
*	The destructor is invoked once per allocated object, allowing the user
*	to perform any necessary cleanup. Users should not attempt to deallocate
*	the memory for the composite object, as the quick composite pool manages
*	object allocation and deallocation.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_init
*********/

/****s* Component Library: Quick Composite Pool/cl_qcpool_t
* NAME
*	cl_qcpool_t
*
* DESCRIPTION
*	Quick composite pool structure.
*
*	The cl_qcpool_t structure should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct _cl_qcpool {
	uint32_t num_components;
	size_t *component_sizes;
	void **p_components;
	size_t num_objects;
	size_t max_objects;
	size_t grow_size;
	cl_pfn_qcpool_init_t pfn_init;
	cl_pfn_qcpool_dtor_t pfn_dtor;
	const void *context;
	cl_qlist_t free_list;
	cl_qlist_t alloc_list;
	cl_state_t state;
} cl_qcpool_t;
/*
* FIELDS
*	num_components
*		Number of components per object.
*
*	component_sizes
*		Array of sizes, one for each component.
*
*	p_components
*		Array of pointers to components, used for the constructor callback.
*
*	num_objects
*		Number of objects managed by the pool
*
*	grow_size
*		Number of objects to add when automatically growing the pool.
*
*	pfn_init
*		Pointer to the user's initializer callback to invoke when initializing
*		new objects.
*
*	pfn_dtor
*		Pointer to the user's destructor callback to invoke before deallocating
*		memory allocated for objects.
*
*	context
*		User's provided context for callback functions, used by the pool
*		when invoking callbacks.
*
*	free_list
*		Quick list of objects available.
*
*	alloc_list
*		Quick list used to store information about allocations.
*
*	state
*		State of the pool.
*
* SEE ALSO
*	Quick Composite Pool
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_construct
* NAME
*	cl_qcpool_construct
*
* DESCRIPTION
*	The cl_qcpool_construct function constructs a quick composite pool.
*
* SYNOPSIS
*/
void cl_qcpool_construct(IN cl_qcpool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure whose state to initialize.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling cl_qcpool_init, cl_qcpool_destroy, cl_is_qcpool_inited.
*
*	Calling cl_qcpool_construct is a prerequisite to calling any other
*	quick composite pool function except cl_qcpool_init.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_init, cl_qcpool_destroy,
*	cl_is_qcpool_inited
*********/

/****f* Component Library: Quick Composite Pool/cl_is_qcpool_inited
* NAME
*	cl_is_qcpool_inited
*
* DESCRIPTION
*	The cl_is_qcpool_inited function returns whether a quick composite pool was
*	successfully initialized.
*
* SYNOPSIS
*/
static inline uint32_t cl_is_qcpool_inited(IN const cl_qcpool_t * const p_pool)
{
	/* CL_ASSERT that a non-null pointer is provided. */
	CL_ASSERT(p_pool);
	/* CL_ASSERT that the pool is not in some invalid state. */
	CL_ASSERT(cl_is_state_valid(p_pool->state));

	return (p_pool->state == CL_INITIALIZED);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure to check.
*
* RETURN VALUES
*	TRUE if the quick composite pool was initialized successfully.
*
*	FALSE otherwise.
*
* NOTES
*	Allows checking the state of a quick composite pool to determine if
*	invoking member functions is appropriate.
*
* SEE ALSO
*	Quick Composite Pool
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_init
* NAME
*	cl_qcpool_init
*
* DESCRIPTION
*	The cl_qcpool_init function initializes a quick composite pool for use.
*
* SYNOPSIS
*/
cl_status_t
cl_qcpool_init(IN cl_qcpool_t * const p_pool,
	       IN const size_t min_size,
	       IN const size_t max_size,
	       IN const size_t grow_size,
	       IN const size_t * const component_sizes,
	       IN const uint32_t num_components,
	       IN cl_pfn_qcpool_init_t pfn_initializer OPTIONAL,
	       IN cl_pfn_qcpool_dtor_t pfn_destructor OPTIONAL,
	       IN const void *const context);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure to initialize.
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
*		[in] Initializer callback to invoke for every new object when growing
*		the pool. This parameter may be NULL only if the objects stored in
*		the quick composite pool consist of only one component. If NULL, the
*		pool assumes the cl_pool_item_t structure describing objects is
*		located at the head of each object. See the cl_pfn_qcpool_init_t
*		function type declaration for details about the callback function.
*
*	pfn_destructor
*		[in] Destructor callback to invoke for every object before memory for
*		that object is freed. This parameter is optional and may be NULL.
*		See the cl_pfn_qcpool_dtor_t function type declaration for details
*		about the callback function.
*
*	context
*		[in] Value to pass to the callback functions to provide context.
*
* RETURN VALUES
*	CL_SUCCESS if the quick composite pool was initialized successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to initialize the
*	quick composite pool.
*
*	CL_INVALID_SETTING if a NULL constructor was provided for composite objects
*	consisting of more than one component.  Also returns CL_INVALID_SETTING if
*	the maximum size is non-zero and less than the minimum size.
*
*	Other cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter.
*
*	If initialization fails, the pool is left in a destroyed state.  Callers
*	may still safely call cl_qcpool_destroy.
*
* NOTES
*	cl_qcpool_init initializes, and if necessary, grows the pool to
*	the capacity desired.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_construct, cl_qcpool_destroy,
*	cl_qcpool_get, cl_qcpool_put, cl_qcpool_grow,
*	cl_qcpool_count, cl_pfn_qcpool_init_t, cl_pfn_qcpool_dtor_t
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_destroy
* NAME
*	cl_qcpool_destroy
*
* DESCRIPTION
*	The cl_qcpool_destroy function destroys a quick composite pool.
*
* SYNOPSIS
*/
void cl_qcpool_destroy(IN cl_qcpool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	All memory allocated for composite objects is freed. The destructor
*	callback, if any, will be invoked for every allocated object. Further
*	operations on the composite pool should not be attempted after
*	cl_qcpool_destroy is invoked.
*
*	This function should only be called after a call to
*	cl_qcpool_construct or cl_qcpool_init.
*
*	In a debug build, cl_qcpool_destroy asserts that all objects are in
*	the pool.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_construct, cl_qcpool_init
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_count
* NAME
*	cl_qcpool_count
*
* DESCRIPTION
*	The cl_qcpool_count function returns the number of available objects
*	in a quick composite pool.
*
* SYNOPSIS
*/
static inline size_t cl_qcpool_count(IN cl_qcpool_t * const p_pool)
{
	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);

	return (cl_qlist_count(&p_pool->free_list));
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure for which the number of
*		available objects is requested.
*
* RETURN VALUE
*	Returns the number of objects available in the specified
*	quick composite pool.
*
* SEE ALSO
*	Quick Composite Pool
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_get
* NAME
*	cl_qcpool_get
*
* DESCRIPTION
*	The cl_qcpool_get function retrieves an object from a
*	quick composite pool.
*
* SYNOPSIS
*/
cl_pool_item_t *cl_qcpool_get(IN cl_qcpool_t * const p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure from which to retrieve
*		an object.
*
* RETURN VALUES
*	Returns a pointer to a cl_pool_item_t for a composite object.
*
*	Returns NULL if the pool is empty and can not be grown automatically.
*
* NOTES
*	cl_qcpool_get returns the object at the head of the pool. If the pool is
*	empty, it is automatically grown to accommodate this request unless the
*	grow_size parameter passed to the cl_qcpool_init function was zero.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_get_tail, cl_qcpool_put,
*	cl_qcpool_grow, cl_qcpool_count
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_put
* NAME
*	cl_qcpool_put
*
* DESCRIPTION
*	The cl_qcpool_put function returns an object to a quick composite pool.
*
* SYNOPSIS
*/
static inline void
cl_qcpool_put(IN cl_qcpool_t * const p_pool,
	      IN cl_pool_item_t * const p_pool_item)
{
	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);
	CL_ASSERT(p_pool_item);
	/* Make sure items being returned came from the specified pool. */
	CL_ASSERT(p_pool_item->p_pool == p_pool);

	/* return this lil' doggy to the pool */
	cl_qlist_insert_head(&p_pool->free_list, &p_pool_item->list_item);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure to which to return
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
*	cl_qcpool_put places the returned object at the head of the pool.
*
*	The object specified by the p_pool_item parameter must have been
*	retrieved from the pool by a previous call to cl_qcpool_get.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_put_tail, cl_qcpool_get
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_put_list
* NAME
*	cl_qcpool_put_list
*
* DESCRIPTION
*	The cl_qcpool_put_list function returns a list of objects to the head of
*	a quick composite pool.
*
* SYNOPSIS
*/
static inline void
cl_qcpool_put_list(IN cl_qcpool_t * const p_pool, IN cl_qlist_t * const p_list)
{
#ifdef _DEBUG_
	cl_list_item_t *p_item;
#endif

	CL_ASSERT(p_pool);
	CL_ASSERT(p_pool->state == CL_INITIALIZED);
	CL_ASSERT(p_list);

#ifdef _DEBUG_
	/* Chech that all items in the list came from this pool. */
	p_item = cl_qlist_head(p_list);
	while (p_item != cl_qlist_end(p_list)) {
		CL_ASSERT(((cl_pool_item_t *) p_item)->p_pool == p_pool);
		p_item = cl_qlist_next(p_item);
	}
#endif

	/* return these lil' doggies to the pool */
	cl_qlist_insert_list_head(&p_pool->free_list, p_list);
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure to which to return
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
*	cl_qcpool_put_list places the returned objects at the head of the pool.
*
*	The objects in the list specified by the p_list parameter must have been
*	retrieved from the pool by a previous call to cl_qcpool_get.
*
* SEE ALSO
*	Quick Composite Pool, cl_qcpool_put, cl_qcpool_put_tail, cl_qcpool_get
*********/

/****f* Component Library: Quick Composite Pool/cl_qcpool_grow
* NAME
*	cl_qcpool_grow
*
* DESCRIPTION
*	The cl_qcpool_grow function grows a quick composite pool by
*	the specified number of objects.
*
* SYNOPSIS
*/
cl_status_t cl_qcpool_grow(IN cl_qcpool_t * const p_pool, IN size_t obj_count);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a cl_qcpool_t structure whose capacity to grow.
*
*	obj_count
*		[in] Number of objects by which to grow the pool.
*
* RETURN VALUES
*	CL_SUCCESS if the quick composite pool grew successfully.
*
*	CL_INSUFFICIENT_MEMORY if there was not enough memory to grow the
*	quick composite pool.
*
*	cl_status_t value returned by optional initialization callback function
*	specified by the pfn_initializer parameter passed to the
*	cl_qcpool_init function.
*
* NOTES
*	It is not necessary to call cl_qcpool_grow if the pool is
*	configured to grow automatically.
*
* SEE ALSO
*	Quick Composite Pool
*********/

END_C_DECLS
#endif				/* _CL_QUICK_COMPOSITE_POOL_H_ */
