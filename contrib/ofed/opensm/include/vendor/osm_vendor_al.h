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
 * 	Declaration of osm_mad_wrapper_t.
 *	This object represents the context wrapper for OpenSM MAD processing.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_VENDOR_AL_H_
#define _OSM_VENDOR_AL_H_

#include <iba/ib_types.h>
#include <iba/ib_al.h>
#include <complib/cl_qlist.h>
#include <complib/cl_thread.h>
#include <opensm/osm_base.h>
#include <opensm/osm_log.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* OpenSM/Vendor AL
* NAME
*	Vendor AL
*
* DESCRIPTION
*
*	The Vendor AL object is thread safe.
*
*	This object should be treated as opaque and should be
*	manipulated only through the provided functions.
*
*	Enable various hacks to compensate for bugs in external code...
*
*
* AUTHOR
*
*
*********/
/****h* OpenSM/Vendor Access Layer (AL)
* NAME
*	Vendor AL
*
* DESCRIPTION
*	This file is the vendor specific file for the AL Infiniband API.
*
* AUTHOR
*	Steve King, Intel
*
*********/
#define OSM_AL_SQ_SGE 256
#define OSM_AL_RQ_SGE 256
#define OSM_DEFAULT_RETRY_COUNT 3
/* AL supports RMPP */
#define VENDOR_RMPP_SUPPORT 1
/****s* OpenSM: Vendor AL/osm_ca_info_t
* NAME
*   osm_ca_info_t
*
* DESCRIPTION
* 	Structure containing information about local Channle Adapters.
*
* SYNOPSIS
*/
typedef struct _osm_ca_info {
	ib_net64_t guid;
	size_t attr_size;
	ib_ca_attr_t *p_attr;

} osm_ca_info_t;
/*
* FIELDS
*	guid
*		Node GUID of the local CA.
*
*	attr_size
*		Size of the CA attributes for this CA.
*
*	p_attr
*		Pointer to dynamicly allocated CA Attribute structure.
*
* SEE ALSO
*********/

/****f* OpenSM: CA Info/osm_ca_info_get_num_ports
* NAME
*	osm_ca_info_get_num_ports
*
* DESCRIPTION
*	Returns the number of ports owned by this CA.
*
* SYNOPSIS
*/
static inline uint8_t
osm_ca_info_get_num_ports(IN const osm_ca_info_t * const p_ca_info)
{
	return (p_ca_info->p_attr->num_ports);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
* RETURN VALUE
*	Returns the number of ports owned by this CA.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: CA Info/osm_ca_info_get_port_guid
* NAME
*	osm_ca_info_get_port_guid
*
* DESCRIPTION
*	Returns the port GUID of the specified port owned by this CA.
*
* SYNOPSIS
*/
static inline ib_net64_t
osm_ca_info_get_port_guid(IN const osm_ca_info_t * const p_ca_info,
			  IN const uint8_t index)
{
	return (p_ca_info->p_attr->p_port_attr[index].port_guid);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
*	index
*		[in] Port "index" for which to retrieve the port GUID.
*		The index is the offset into the ca's internal array
*		of port attributes.
*
* RETURN VALUE
*	Returns the port GUID of the specified port owned by this CA.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: CA Info/osm_ca_info_get_port_num
* NAME
*	osm_ca_info_get_port_num
*
* DESCRIPTION
*	Returns the port number of the specified port owned by this CA.
*	Port numbers start with 1 for HCA's.
*
* SYNOPSIS
*/
static inline uint8_t
osm_ca_info_get_port_num(IN const osm_ca_info_t * const p_ca_info,
			 IN const uint8_t index)
{
	return (p_ca_info->p_attr->p_port_attr[index].port_num);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
*	index
*		[in] Port "index" for which to retrieve the port GUID.
*		The index is the offset into the ca's internal array
*		of port attributes.
*
* RETURN VALUE
*	Returns the port GUID of the specified port owned by this CA.
*
* NOTES
*
* SEE ALSO
*********/

/****f* OpenSM: CA Info/osm_ca_info_get_ca_guid
* NAME
*	osm_ca_info_get_ca_guid
*
* DESCRIPTION
*	Returns the GUID of the specified CA.
*
* SYNOPSIS
*/
static inline ib_net64_t
osm_ca_info_get_ca_guid(IN const osm_ca_info_t * const p_ca_info)
{
	return (p_ca_info->p_attr->ca_guid);
}

/*
* PARAMETERS
*	p_ca_info
*		[in] Pointer to a CA Info object.
*
* RETURN VALUE
*	Returns the GUID of the specified CA.
*
* NOTES
*
* SEE ALSO
*********/

/****s* OpenSM: Vendor AL/osm_bind_handle_t
* NAME
*   osm_bind_handle_t
*
* DESCRIPTION
* 	handle returned by the vendor transport bind call.
*
* SYNOPSIS
*/
typedef struct _osm_vendor {
	ib_al_handle_t h_al;
	osm_log_t *p_log;
	uint32_t ca_count;
	osm_ca_info_t *p_ca_info;
	uint32_t timeout;
	ib_ca_handle_t h_ca;
	ib_pd_handle_t h_pd;

} osm_vendor_t;
/*
* FIELDS
*	h_al
*		Handle returned by AL open call (ib_open_al).
*
*	p_log
*		Pointer to the log object.
*
*	ca_count
*		Number of CA's in the array pointed to by p_ca_info.
*
*	p_ca_info
*		Pointer to dynamically allocated array of CA info objects.
*
*	h_pool
*		MAD Pool handle returned by ib_create_mad_pool at init time.
*
*	timeout
*		Transaction timeout time in milliseconds.
*
* SEE ALSO
*********/

#define OSM_BIND_INVALID_HANDLE 0

/****s* OpenSM: Vendor AL/osm_bind_handle_t
* NAME
*   osm_bind_handle_t
*
* DESCRIPTION
* 	handle returned by the vendor transport bind call.
*
* SYNOPSIS
*/
typedef void *osm_bind_handle_t;
/***********/

/****s* OpenSM/osm_vend_wrap_t
* NAME
*   AL Vendor MAD Wrapper
*
* DESCRIPTION
*	AL specific MAD wrapper. AL transport layer uses this for
*	housekeeping.
*
* SYNOPSIS
*********/
typedef struct _osm_vend_wrap_t {
	uint32_t size;
	osm_bind_handle_t h_bind;
	ib_mad_element_t *p_elem;
	ib_av_handle_t h_av;
	void *p_resp_madw;

} osm_vend_wrap_t;
/*
* FIELDS
*	size
*		Size of the allocated MAD
*
*	h_bind
*		Bind handle used on this transaction
*
*	p_elem
*		Pointer to the mad element structure associated with
*		this mad.
*
*	h_av
*		Address vector handle used for this transaction.
*
*	p_resp_madw
*		Pointer to the mad wrapper structure used to hold the pending
*		reponse to the mad, if any.  If a response is expected, the
*		wrapper for the reponse is allocated during the send call.
*
* SEE ALSO
*********/

END_C_DECLS
#endif				/* _OSM_VENDOR_AL_H_ */
