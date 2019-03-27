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
 * 	Definition of interface for the TS Vendor
 *	   This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_VENDOR_TS_H_
#define _OSM_VENDOR_TS_H_

#undef IN
#undef OUT
#include <vapi_types.h>
#include <evapi.h>
#include <ib/ts_api_ng/useraccess/include/ts_ib_useraccess.h>
#define IN
#define OUT
#include "iba/ib_types.h"
#include "iba/ib_al.h"
#include <complib/cl_thread.h>
#include <complib/cl_types_osd.h>
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
/****s* OpenSM: Vendor TS/osm_bind_handle_t
 * NAME
 *   osm_bind_handle_t
 *
 * DESCRIPTION
 * 	handle returned by the vendor transport bind call.
 *
 * SYNOPSIS
 */
typedef void *osm_bind_handle_t;
/*
**********/
#define OSM_DEFAULT_RETRY_COUNT 3

/****s* OpenSM: Vendor osm_ts_bind_info_t
 * NAME
 *   osm_ts_bind_info_t
 *
 * DESCRIPTION
 * 	Handle to the result of binding a class callbacks .
 *
 * SYNOPSIS
 */
typedef struct _osm_ts_bind_info {
	int ul_dev_fd;
	VAPI_hca_hndl_t hca_hndl;
	struct _osm_vendor *p_vend;
	void *client_context;
	uint8_t port_num;
	void *rcv_callback;
	void *send_err_callback;
	struct _osm_mad_pool *p_osm_pool;
	cl_thread_t poller;
} osm_ts_bind_info_t;
/*
 * FIELDS
 *	ul_dev_file_hdl
 *		the file handle to be used for sending the MADs
 *
 * hca_hndl
 *     Handle to the HCA provided by the underlying VAPI
 *
 *	p_vend
 *		Pointer to the vendor object.
 *
 *	client_context
 *		User's context passed during osm_bind
 *
 *  hca_id
 *     HCA Id we bind to.
 *
 *	port_num
 *		Port number (within the HCA) of the bound port.
 *
 *	rcv_callback
 *		OSM Callback function to be called on receive of MAD.
 *
 *  send_err_callback
 *     OSM Callback to be called on send error.
 *
 *  p_osm_pool
 *     Points to the MAD pool used by OSM
 *
 *  poller
 *     A thread reading from the device file handle
 *
 * SEE ALSO
 *********/

/****h* OpenSM/Vendor TS
 * NAME
 *	Vendor TS
 *
 * DESCRIPTION
 *
 *	The Vendor TS object is thread safe.
 *
 *	This object should be treated as opaque and should be
 *	manipulated only through the provided functions.
 *
 *
 * AUTHOR
 *
 *
 *********/

/****s* OpenSM: Vendor TS/osm_ca_info_t
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

/***** OpenSM: Vendor TS/osm_vendor_t
 * NAME
 *  osm_vendor_t
 *
 * DESCRIPTION
 * 	The structure defining a TS vendor
 *
 * SYNOPSIS
 */
typedef struct _osm_vendor {
	osm_log_t *p_log;
	uint32_t ca_count;
	osm_ca_info_t *p_ca_info;
	uint32_t timeout;
	struct _osm_transaction_mgr *p_transaction_mgr;
	osm_ts_bind_info_t smi_bind;
	osm_ts_bind_info_t gsi_bind;
} osm_vendor_t;

/*
 * FIELDS
 *	h_al
 *		Handle returned by TS open call .
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
 *	timeout
 *		Transaction timeout time in milliseconds.
 *
 *  p_transaction_mgr
 *     Pointer to Transaction Manager.
 *
 *  smi_bind
 *     Bind information for handling SMI MADs
 *
 *  gsi_bind
 *     Bind information for GSI MADs
 *
 * SEE ALSO
 *********/

/****f* OpenSM: Vendor TS/CA Info/osm_ca_info_get_port_guid
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

/****f* OpenSM: Vendor TS/CA Info/osm_ca_info_get_num_ports
 * NAME
 *	osm_ca_info_get_num_ports
 *
 * DESCRIPTION
 *	Returns the number of ports of the given ca_info
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
 *	Returns the number of CA ports
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* OpenSM: SM Vendor/osm_vendor_get_guid_ca_and_port
 * NAME
 *	osm_vendor_get_guid_ca_and_port
 *
 * DESCRIPTION
 * Given the vendor obj and a guid
 * return the ca id and port number that have that guid
 *
 * SYNOPSIS
 */
ib_api_status_t
osm_vendor_get_guid_ca_and_port(IN osm_vendor_t * const p_vend,
				IN ib_net64_t const guid,
				OUT VAPI_hca_hndl_t * p_hca_hndl,
				OUT VAPI_hca_id_t * p_hca_id,
				OUT uint32_t * p_port_num);

/*
 * PARAMETERS
 *	p_vend
 *		[in] Pointer to an osm_vendor_t object.
 *
 *	guid
 *		[in] The guid to search for.
 *
 *	p_hca_id
 *		[out] The HCA Id (VAPI_hca_id_t *) that the port is found on.
 *
 *	p_port_num
 *		[out] Pointer to a port number arg to be filled with the port number with the given guid.
 *
 * RETURN VALUES
 *	IB_SUCCESS on SUCCESS
 *  IB_INVALID_GUID if the guid is notfound on any Local HCA Port
 *
 * NOTES
 *
 * SEE ALSO
 *********/

/****f* OpenSM: Vendor TS/osm_vendor_get_all_port_attr
 * NAME
 *	osm_vendor_get_all_port_attr
 *
 * DESCRIPTION
 * Fill in the array of port_attr with all available ports on ALL the
 * avilable CAs on this machine.
 * ALSO -
 * UPDATE THE VENDOR OBJECT LIST OF CA_INFO STRUCTS
 *
 * SYNOPSIS
 */
ib_api_status_t osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
					     IN ib_port_attr_t *
					     const p_attr_array,
					     IN uint32_t * const p_num_ports);

/*
 * PARAMETERS
 *	p_vend
 *		[in] Pointer to an osm_vendor_t object.
 *
 *	p_attr_array
 *		[out] Pre-allocated array of port attributes to be filled in
 *
 *	p_num_ports
 *		[out] The size of the given array. Filled in by the actual numberof ports found.
 *
 * RETURN VALUES
 *	IB_SUCCESS if OK
 *  IB_INSUFFICIENT_MEMORY if not enough place for all ports was provided.
 *
 * NOTES
 *
 * SEE ALSO
 *********/

#define OSM_BIND_INVALID_HANDLE 0

/****s* OpenSM: Vendor TS/osm_vend_wrap_t
 * NAME
 *   TS Vendor MAD Wrapper
 *
 * DESCRIPTION
 *	TS specific MAD wrapper. TS transport layer uses this for
 *	housekeeping.
 *
 * SYNOPSIS
 *********/
typedef struct _osm_vend_wrap_t {
	uint32_t size;
	osm_bind_handle_t h_bind;
	ib_mad_t *p_mad_buf;
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
#endif				/* _OSM_VENDOR_TS_H_ */
