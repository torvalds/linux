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
 * 	Declaration of osm_mad_pool_t.
 *	This object represents a pool of management datagram (MAD) objects.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_MAD_POOL_H_
#define _OSM_MAD_POOL_H_

#include <iba/ib_types.h>
#include <complib/cl_atomic.h>
#include <opensm/osm_base.h>
#include <opensm/osm_madw.h>
#include <vendor/osm_vendor.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/MAD Pool
* NAME
*	MAD Pool
*
* DESCRIPTION
*	The MAD Pool encapsulates the information needed by the
*	OpenSM to manage a pool of MAD objects.  The OpenSM allocates
*	one MAD Pool per IBA subnet.
*
*	The MAD Pool is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Steve King, Intel
*
*********/
/****s* OpenSM: MAD Pool/osm_mad_pool_t
* NAME
*	osm_mad_pool_t
*
* DESCRIPTION
*	MAD Pool structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_mad_pool {
	atomic32_t mads_out;
} osm_mad_pool_t;
/*
* FIELDS
*	mads_out
*		Running total of the number of MADs outstanding.
*
* SEE ALSO
*	MAD Pool
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_construct
* NAME
*	osm_mad_pool_construct
*
* DESCRIPTION
*	This function constructs a MAD Pool.
*
* SYNOPSIS
*/
void osm_mad_pool_construct(IN osm_mad_pool_t * p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a MAD Pool to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_mad_pool_init, osm_mad_pool_destroy
*
*	Calling osm_mad_pool_construct is a prerequisite to calling any other
*	method except osm_mad_pool_init.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_init, osm_mad_pool_destroy
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_destroy
* NAME
*	osm_mad_pool_destroy
*
* DESCRIPTION
*	The osm_mad_pool_destroy function destroys a node, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_mad_pool_destroy(IN osm_mad_pool_t * p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to a MAD Pool to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified MAD Pool.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to osm_mad_pool_construct or
*	osm_mad_pool_init.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_construct, osm_mad_pool_init
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_init
* NAME
*	osm_mad_pool_init
*
* DESCRIPTION
*	The osm_mad_pool_init function initializes a MAD Pool for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_mad_pool_init(IN osm_mad_pool_t * p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object to initialize.
*
* RETURN VALUES
*	CL_SUCCESS if the MAD Pool was initialized successfully.
*
* NOTES
*	Allows calling other MAD Pool methods.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_construct, osm_mad_pool_destroy
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_get
* NAME
*	osm_mad_pool_get
*
* DESCRIPTION
*	Gets a MAD wrapper and wire MAD from the pool.
*
* SYNOPSIS
*/
osm_madw_t *osm_mad_pool_get(IN osm_mad_pool_t * p_pool,
			     IN osm_bind_handle_t h_bind,
			     IN uint32_t total_size,
			     IN const osm_mad_addr_t * p_mad_addr);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object.
*
*	h_bind
*		[in] Handle returned from osm_vendor_bind() call to the
*		port over which this mad will be sent.
*
*	total_size
*		[in] Total size, including MAD header of the requested MAD.
*
*	p_mad_addr
*		[in] Pointer to the MAD address structure.  This parameter
*		may be NULL for directed route MADs.
*
* RETURN VALUES
*	Returns a pointer to a MAD wrapper containing the MAD.
*	A return value of NULL means no MADs are available.
*
* NOTES
*	The MAD must eventually be returned to the pool with a call to
*	osm_mad_pool_put.
*
*	The osm_mad_pool_construct or osm_mad_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_put
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_put
* NAME
*	osm_mad_pool_put
*
* DESCRIPTION
*	Returns a MAD to the pool.
*
* SYNOPSIS
*/
void osm_mad_pool_put(IN osm_mad_pool_t * p_pool, IN osm_madw_t * p_madw);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object.
*
*	p_madw
*		[in] Pointer to a MAD Wrapper for a MAD that was previously
*		retrieved from the pool.
*
* RETURN VALUES
*	This function does not return a value.
*
* NOTES
*	The osm_mad_pool_construct or osm_mad_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_get
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_get_wrapper
* NAME
*	osm_mad_pool_get_wrapper
*
* DESCRIPTION
*	Gets a only MAD wrapper from the pool (no wire MAD).
*
* SYNOPSIS
*/
osm_madw_t *osm_mad_pool_get_wrapper(IN osm_mad_pool_t * p_pool,
				     IN osm_bind_handle_t h_bind,
				     IN uint32_t total_size,
				     IN const ib_mad_t * p_mad,
				     IN const osm_mad_addr_t * p_mad_addr);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object.
*
*	h_bind
*		[in] Handle returned from osm_vendor_bind() call to the
*		port for which this mad wrapper will be used.
*
*	total_size
*		[in] Total size, including MAD header of the MAD that will
*		be attached to this wrapper.
*
*	p_mad
*		[in] Pointer to the MAD to attach to this wrapper.
*
*	p_mad_addr
*		[in] Pointer to the MAD address structure.  This parameter
*		may be NULL for directed route MADs.
*
* RETURN VALUES
*	Returns a pointer to a MAD wrapper.
*	A return value of NULL means no MAD wrappers are available.
*
* NOTES
*	The MAD must eventually be returned to the pool with a call to
*	osm_mad_pool_put.
*
*	The osm_mad_pool_construct or osm_mad_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_put
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_get_wrapper_raw
* NAME
*	osm_mad_pool_get_wrapper_raw
*
* DESCRIPTION
*	Gets a only an uninitialized MAD wrapper from the pool (no wire MAD).
*
* SYNOPSIS
*/
osm_madw_t *osm_mad_pool_get_wrapper_raw(IN osm_mad_pool_t * p_pool);
/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object.
*
* RETURN VALUES
*	Returns a pointer to a MAD wrapper.
*	A return value of NULL means no MAD wrappers are available.
*
* NOTES
*	The MAD must eventually be returned to the pool with a call to
*	osm_mad_pool_put.
*
*	The osm_mad_pool_construct or osm_mad_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_put
*********/

/****f* OpenSM: MAD Pool/osm_mad_pool_get_outstanding
* NAME
*	osm_mad_pool_get_count
*
* DESCRIPTION
*	Returns the running count of MADs currently outstanding from the pool.
*
* SYNOPSIS
*/
static inline uint32_t
osm_mad_pool_get_outstanding(IN const osm_mad_pool_t * p_pool)
{
	return p_pool->mads_out;
}

/*
* PARAMETERS
*	p_pool
*		[in] Pointer to an osm_mad_pool_t object.
*
* RETURN VALUES
*	Returns the running count of MADs currently outstanding from the pool.
*
* NOTES
*	The osm_mad_pool_construct or osm_mad_pool_init must be called before
*	using this function.
*
* SEE ALSO
*	MAD Pool, osm_mad_pool_get
*********/

END_C_DECLS
#endif				/* _OSM_MAD_POOL_H_ */
