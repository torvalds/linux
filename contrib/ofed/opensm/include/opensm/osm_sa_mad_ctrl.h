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
 * 	Declaration of osm_sa_mad_ctrl_t.
 *	This object represents a controller that receives the IBA SA
 *	attributes from a node.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_SA_MAD_CTRL_H_
#define _OSM_SA_MAD_CTRL_H_

#include <complib/cl_dispatcher.h>
#include <opensm/osm_stats.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/SA MAD Controller
* NAME
*	SA MAD Controller
*
* DESCRIPTION
*	The SA MAD Controller object encapsulates
*	the information	needed to receive MADs from the transport layer.
*
*	The SA MAD Controller object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
* AUTHOR
*	Ranjit Pandit, Intel
*
*********/

struct osm_sa;
/****s* OpenSM: SA MAD Controller/osm_sa_mad_ctrl_t
* NAME
*	osm_sa_mad_ctrl_t
*
* DESCRIPTION
*	SA MAD Controller structure.
*
*	This object should be treated as opaque and should
*	be manipulated only through the provided functions.
*
* SYNOPSIS
*/
typedef struct osm_sa_mad_ctrl {
	struct osm_sa *sa;
	osm_log_t *p_log;
	osm_mad_pool_t *p_mad_pool;
	osm_vendor_t *p_vendor;
	osm_bind_handle_t h_bind;
	cl_dispatcher_t *p_disp;
	cl_dispatcher_t *p_set_disp;
	cl_disp_reg_handle_t h_disp;
	cl_disp_reg_handle_t h_set_disp;
	osm_stats_t *p_stats;
	osm_subn_t *p_subn;
} osm_sa_mad_ctrl_t;
/*
* FIELDS
*	sa
*		Pointer to the SA object.
*
*	p_log
*		Pointer to the log object.
*
*	p_mad_pool
*		Pointer to the MAD pool.
*
*	p_vendor
*		Pointer to the vendor specific interfaces object.
*
*	h_bind
*		Bind handle returned by the transport layer.
*
*	p_disp
*		Pointer to the Dispatcher.
*
*	p_set_disp
*		Pointer to the Dispatcher for Set requests.
*
*	h_disp
*		Handle returned from dispatcher registration.
*
*	h_set_disp
*		Handle returned from Set requests dispatcher registration.
*
*	p_stats
*		Pointer to the OpenSM statistics block.
*
* SEE ALSO
*	SA MAD Controller object
*	SA MADr object
*********/

/****f* OpenSM: SA MAD Controller/osm_sa_mad_ctrl_construct
* NAME
*	osm_sa_mad_ctrl_construct
*
* DESCRIPTION
*	This function constructs a SA MAD Controller object.
*
* SYNOPSIS
*/
void osm_sa_mad_ctrl_construct(IN osm_sa_mad_ctrl_t * p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to a SA MAD Controller
*		object to construct.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Allows calling osm_sa_mad_ctrl_init, and osm_sa_mad_ctrl_destroy.
*
*	Calling osm_sa_mad_ctrl_construct is a prerequisite to calling any other
*	method except osm_sa_mad_ctrl_init.
*
* SEE ALSO
*	SA MAD Controller object, osm_sa_mad_ctrl_init,
*	osm_sa_mad_ctrl_destroy
*********/

/****f* OpenSM: SA MAD Controller/osm_sa_mad_ctrl_destroy
* NAME
*	osm_sa_mad_ctrl_destroy
*
* DESCRIPTION
*	The osm_sa_mad_ctrl_destroy function destroys the object, releasing
*	all resources.
*
* SYNOPSIS
*/
void osm_sa_mad_ctrl_destroy(IN osm_sa_mad_ctrl_t * p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to the object to destroy.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	Performs any necessary cleanup of the specified
*	SA MAD Controller object.
*	Further operations should not be attempted on the destroyed object.
*	This function should only be called after a call to
*	osm_sa_mad_ctrl_construct or osm_sa_mad_ctrl_init.
*
* SEE ALSO
*	SA MAD Controller object, osm_sa_mad_ctrl_construct,
*	osm_sa_mad_ctrl_init
*********/

/****f* OpenSM: SA MAD Controller/osm_sa_mad_ctrl_init
* NAME
*	osm_sa_mad_ctrl_init
*
* DESCRIPTION
*	The osm_sa_mad_ctrl_init function initializes a
*	SA MAD Controller object for use.
*
* SYNOPSIS
*/
ib_api_status_t osm_sa_mad_ctrl_init(IN osm_sa_mad_ctrl_t * p_ctrl,
				     IN struct osm_sa * sa,
				     IN osm_mad_pool_t * p_mad_pool,
				     IN osm_vendor_t * p_vendor,
				     IN osm_subn_t * p_subn,
				     IN osm_log_t * p_log,
				     IN osm_stats_t * p_stats,
				     IN cl_dispatcher_t * p_disp,
				     IN cl_dispatcher_t * p_set_disp);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_sa_mad_ctrl_t object to initialize.
*
*	sa
*		[in] Pointer to the SA object.
*
*	p_mad_pool
*		[in] Pointer to the MAD pool.
*
*	p_vendor
*		[in] Pointer to the vendor specific interfaces object.
*
*	p_log
*		[in] Pointer to the log object.
*
*	p_stats
*		[in] Pointer to the OpenSM stastics block.
*
*	p_disp
*		[in] Pointer to the OpenSM central Dispatcher.
*
*	p_set_disp
*		[in] Pointer to the OpenSM Dispatcher for Set requests.
*
* RETURN VALUES
*	IB_SUCCESS if the SA MAD Controller object was initialized
*	successfully.
*
* NOTES
*	Allows calling other SA MAD Controller methods.
*
* SEE ALSO
*	SA MAD Controller object, osm_sa_mad_ctrl_construct,
*	osm_sa_mad_ctrl_destroy
*********/

/****f* OpenSM: SA/osm_sa_mad_ctrl_bind
* NAME
*	osm_sa_mad_ctrl_bind
*
* DESCRIPTION
*	Binds the SA MAD Controller object to a port guid.
*
* SYNOPSIS
*/
ib_api_status_t osm_sa_mad_ctrl_bind(IN osm_sa_mad_ctrl_t * p_ctrl,
				     IN ib_net64_t port_guid);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_sa_mad_ctrl_t object to initialize.
*
*	port_guid
*		[in] Local port GUID with which to bind.
*
*
* RETURN VALUES
*	None
*
* NOTES
*	A given SA MAD Controller object can only be bound to one
*	port at a time.
*
* SEE ALSO
*********/

/****f* OpenSM: SA/osm_sa_mad_ctrl_unbind
* NAME
*	osm_sa_mad_ctrl_unbind
*
* DESCRIPTION
*	Un-Binds the SA MAD Controller object from the IB port
*
* SYNOPSIS
*/
ib_api_status_t osm_sa_mad_ctrl_unbind(IN osm_sa_mad_ctrl_t * p_ctrl);
/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_sa_mad_ctrl_t object to initialize.
*
* RETURN VALUES
*	None
*
* NOTES
*	A given SA MAD Controller should be previously bound to IB
*	port.
*
* SEE ALSO
*********/

/****f* OpenSM: SA/osm_sa_mad_ctrl_get_bind_handle
* NAME
*	osm_sa_mad_ctrl_get_bind_handle
*
* DESCRIPTION
*	Returns the bind handle.
*
* SYNOPSIS
*/
static inline osm_bind_handle_t
osm_sa_mad_ctrl_get_bind_handle(IN const osm_sa_mad_ctrl_t * p_ctrl)
{
	return p_ctrl->h_bind;
}

/*
* PARAMETERS
*	p_ctrl
*		[in] Pointer to an osm_sa_mad_ctrl_t object.
*
* RETURN VALUES
*	Returns the bind handle, which may be OSM_BIND_INVALID_HANDLE
*	if no port has been bound.
*
* NOTES
*	A given SA MAD Controller object can only be bound to one
*	port at a time.
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_SA_MAD_CTRL_H_ */
