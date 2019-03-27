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
 *    Implementation of osm_req_t.
 * This object represents the generic attribute requester.
 * This object is part of the opensm family of objects.
 *
 */

/*
  Next available error code: 0x300
*/

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef OSM_VENDOR_INTF_AL

#include <stdlib.h>
#include <string.h>
#include <complib/cl_qlist.h>
#include <complib/cl_thread.h>
#include <complib/cl_math.h>
#include <complib/cl_debug.h>
#include <iba/ib_types.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_mad_pool.h>
#include <vendor/osm_vendor_api.h>

/****s* OpenSM: Vendor AL/osm_al_bind_info_t
 * NAME
 *   osm_al_bind_info_t
 *
 * DESCRIPTION
 *    Structure containing bind information.
 *
 * SYNOPSIS
 */
typedef struct _osm_al_bind_info {
	osm_vendor_t *p_vend;
	void *client_context;
	ib_qp_handle_t h_qp;
	ib_mad_svc_handle_t h_svc;
	uint8_t port_num;
	ib_pool_key_t pool_key;
	osm_vend_mad_recv_callback_t rcv_callback;
	osm_vend_mad_send_err_callback_t send_err_callback;
	osm_mad_pool_t *p_osm_pool;
	ib_av_handle_t h_dr_av;

} osm_al_bind_info_t;
/*
 * FIELDS
 * p_vend
 *    Pointer to the vendor object.
 *
 * client_context
 *    User's context passed during osm_bind
 *
 * h_qp
 *    Handle the QP for this bind.
 *
 * h_qp_svc
 *    Handle the QP mad service for this bind.
 *
 * port_num
 *    Port number (within the HCA) of the bound port.
 *
 * pool_key
 *    Pool key returned by all for this QP.
 *
 * h_dr_av
 *    Address vector handle used for all directed route SMPs.
 *
 * SEE ALSO
 *********/

inline static ib_api_status_t
__osm_al_convert_wcs(IN ib_wc_status_t const wc_status)
{
	switch (wc_status) {
	case IB_WCS_SUCCESS:
		return (IB_SUCCESS);

	case IB_WCS_TIMEOUT_RETRY_ERR:
		return (IB_TIMEOUT);

	default:
		return (IB_ERROR);
	}
}

static void __osm_al_ca_err_callback(IN ib_async_event_rec_t * p_async_rec)
{
	osm_vendor_t *p_vend = (osm_vendor_t *) p_async_rec->context;
	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"__osm_al_ca_err_callback: ERR 3B01: "
		"Event on channel adapter (%s).\n",
		ib_get_async_event_str(p_async_rec->code));

	OSM_LOG_EXIT(p_vend->p_log);
}

static void __osm_al_ca_destroy_callback(IN void *context)
{
	osm_al_bind_info_t *p_bind = (osm_al_bind_info_t *) context;
	osm_vendor_t *p_vend = p_bind->p_vend;
	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_INFO,
		"__osm_al_ca_destroy_callback: "
		"Closing local channel adapter.\n");

	OSM_LOG_EXIT(p_vend->p_log);
}

static void __osm_al_err_callback(IN ib_async_event_rec_t * p_async_rec)
{
	osm_al_bind_info_t *p_bind =
	    (osm_al_bind_info_t *) p_async_rec->context;
	osm_vendor_t *p_vend = p_bind->p_vend;
	OSM_LOG_ENTER(p_vend->p_log);

	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"__osm_al_err_callback: ERR 3B02: "
		"Error on QP (%s).\n",
		ib_get_async_event_str(p_async_rec->code));

	OSM_LOG_EXIT(p_vend->p_log);
}

static void
__osm_al_send_callback(IN void *mad_svc_context, IN ib_mad_element_t * p_elem)
{
	osm_al_bind_info_t *const p_bind =
	    (osm_al_bind_info_t *) mad_svc_context;
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_madw_t *const p_madw = (osm_madw_t *) p_elem->context1;
	osm_vend_wrap_t *const p_vw = osm_madw_get_vend_ptr(p_madw);
	ib_mad_t *p_mad;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	CL_ASSERT(p_vw->h_av);

	/*
	   Destroy the address vector as necessary.
	 */
	if (p_vw->h_av != p_bind->h_dr_av) {
		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_al_send_callback: "
				"Destroying av handle %p.\n", p_vw->h_av);
		}

		ib_destroy_av(p_vw->h_av);
	}

	p_mad = ib_get_mad_buf(p_elem);

	if (p_elem->resp_expected) {
		/*
		   If the send was unsuccessful, notify the user
		   for MADs that were expecting a response.
		   A NULL mad wrapper parameter is the user's clue
		   that the transaction turned sour.

		   Otherwise, do nothing for successful sends when a
		   reponse is expected.  The mad will be returned to the
		   pool later.
		 */
		p_madw->status = __osm_al_convert_wcs(p_elem->status);
		if (p_elem->status != IB_WCS_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"__osm_al_send_callback: "
				"MAD completed with work queue error: %s.\n",
				ib_get_wc_status_str(p_elem->status));
			/*
			   Return any wrappers to the pool that may have been
			   pre-emptively allocated to handle a receive.
			 */
			if (p_vw->p_resp_madw) {
				osm_mad_pool_put(p_bind->p_osm_pool,
						 p_vw->p_resp_madw);
				p_vw->p_resp_madw = NULL;
			}

			p_bind->send_err_callback(p_bind->client_context,
						  p_madw);
		}
	} else {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"__osm_al_send_callback: "
			"Returning MAD to pool, TID = 0x%" PRIx64 ".\n",
			cl_ntoh64(p_mad->trans_id));
		osm_mad_pool_put(p_bind->p_osm_pool, p_madw);
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
}

static void
__osm_al_rcv_callback(IN void *mad_svc_context, IN ib_mad_element_t * p_elem)
{
	osm_al_bind_info_t *const p_bind =
	    (osm_al_bind_info_t *) mad_svc_context;
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_madw_t *p_old_madw;
	osm_madw_t *p_new_madw;
	osm_vend_wrap_t *p_old_vw;
	osm_vend_wrap_t *p_new_vw;
	ib_mad_t *p_new_mad;
	osm_mad_addr_t mad_addr;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_elem->context1 == NULL);
	CL_ASSERT(p_elem->context2 == NULL);

	p_new_mad = ib_get_mad_buf(p_elem);

	/*
	   In preperation for initializing the new mad wrapper,
	   Initialize the mad_addr structure for the received wire MAD.
	 */
	mad_addr.dest_lid = p_elem->remote_lid;
	mad_addr.path_bits = p_elem->path_bits;

	/* TO DO - figure out which #define to use for the 2.5 Gb rate... */
	mad_addr.static_rate = 0;

	if (p_new_mad->mgmt_class == IB_MCLASS_SUBN_LID ||
	    p_new_mad->mgmt_class == IB_MCLASS_SUBN_DIR) {
		mad_addr.addr_type.smi.source_lid = p_elem->remote_lid;
	} else {
		mad_addr.addr_type.gsi.remote_qp = p_elem->remote_qp;
		mad_addr.addr_type.gsi.remote_qkey = p_elem->remote_qkey;
		mad_addr.addr_type.gsi.pkey_ix = p_elem->pkey_index;
		mad_addr.addr_type.gsi.service_level = p_elem->remote_sl;
		mad_addr.addr_type.gsi.global_route = FALSE;
	}

	/*
	   If this MAD is a response to a previous request,
	   then grab our pre-allocated MAD wrapper.
	   Otherwise, allocate a new MAD wrapper.
	 */
	if (ib_mad_is_response(p_new_mad)) {
		CL_ASSERT(p_elem->send_context1 != NULL);
		CL_ASSERT(p_elem->send_context2 == NULL);

		p_old_madw = (osm_madw_t *) p_elem->send_context1;
		p_old_vw = osm_madw_get_vend_ptr(p_old_madw);
		p_new_madw = p_old_vw->p_resp_madw;

		CL_ASSERT(p_new_madw);

		osm_madw_init(p_new_madw, p_bind, p_elem->size, &mad_addr);
		osm_madw_set_mad(p_new_madw, p_new_mad);
	} else {
		CL_ASSERT(p_elem->send_context1 == NULL);
		CL_ASSERT(p_elem->send_context2 == NULL);

		p_new_madw = osm_mad_pool_get_wrapper(p_bind->p_osm_pool,
						      p_bind, p_elem->size,
						      p_new_mad, &mad_addr);
	}

	CL_ASSERT(p_new_madw);
	p_new_vw = osm_madw_get_vend_ptr(p_new_madw);

	p_new_vw->h_bind = p_bind;
	p_new_vw->size = p_elem->size;
	p_new_vw->p_elem = p_elem;
	p_new_vw->h_av = 0;
	p_new_vw->p_resp_madw = NULL;

	osm_log(p_vend->p_log, OSM_LOG_DEBUG,
		"__osm_al_rcv_callback: "
		"Calling receive callback function %p.\n",
		p_bind->rcv_callback);

	p_bind->rcv_callback(p_new_madw, p_bind->client_context,
			     p_elem->send_context1);

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	ib_api_status_t status;
	OSM_LOG_ENTER(p_log);

	p_vend->p_log = p_log;

	/*
	   Open our instance of AL.
	 */
	status = ib_open_al(&p_vend->h_al);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_init: ERR 3B03: "
			"Error opening AL (%s).\n", ib_get_err_str(status));

		goto Exit;
	}

	p_vend->timeout = timeout;

Exit:
	OSM_LOG_EXIT(p_log);
	return (status);
}

osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	ib_api_status_t status;
	osm_vendor_t *p_vend;

	OSM_LOG_ENTER(p_log);

	p_vend = malloc(sizeof(*p_vend));
	if (p_vend == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_new: ERR 3B04: "
			"Unable to allocate vendor object.\n");
		goto Exit;
	}

	memset(p_vend, 0, sizeof(*p_vend));

	status = osm_vendor_init(p_vend, p_log, timeout);
	if (status != IB_SUCCESS) {
		free(p_vend);
		p_vend = NULL;
	}

Exit:
	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	/* TO DO - fill this in */
	ib_close_al((*pp_vend)->h_al);
	free(*pp_vend);
	*pp_vend = NULL;
}

static ib_api_status_t
__osm_ca_info_init(IN osm_vendor_t * const p_vend,
		   IN osm_ca_info_t * const p_ca_info,
		   IN const ib_net64_t ca_guid)
{
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	p_ca_info->guid = ca_guid;

	if (osm_log_is_active(p_vend->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
			"__osm_ca_info_init: "
			"Querying CA 0x%" PRIx64 ".\n", cl_ntoh64(ca_guid));
	}

	status = ib_query_ca_by_guid(p_vend->h_al, ca_guid, NULL,
				     &p_ca_info->attr_size);
	if ((status != IB_INSUFFICIENT_MEMORY) && (status != IB_SUCCESS)) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 3B05: "
			"Unexpected status getting CA attributes (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	CL_ASSERT(p_ca_info->attr_size);

	p_ca_info->p_attr = malloc(p_ca_info->attr_size);
	if (p_ca_info->p_attr == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 3B06: "
			"Unable to allocate attribute storage.\n");
		goto Exit;
	}

	status = ib_query_ca_by_guid(p_vend->h_al, ca_guid, p_ca_info->p_attr,
				     &p_ca_info->attr_size);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_ca_info_init: ERR 3B07: "
			"Unexpected status getting CA attributes (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void
osm_ca_info_destroy(IN osm_vendor_t * const p_vend,
		    IN osm_ca_info_t * const p_ca_info)
{
	OSM_LOG_ENTER(p_vend->p_log);

	if (p_ca_info->p_attr)
		free(p_ca_info->p_attr);

	free(p_ca_info);

	OSM_LOG_EXIT(p_vend->p_log);
}

osm_ca_info_t *osm_ca_info_new(IN osm_vendor_t * const p_vend,
			       IN const ib_net64_t ca_guid)
{
	ib_api_status_t status;
	osm_ca_info_t *p_ca_info;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(ca_guid);

	p_ca_info = malloc(sizeof(*p_ca_info));
	if (p_ca_info == NULL)
		goto Exit;

	memset(p_ca_info, 0, sizeof(*p_ca_info));

	status = __osm_ca_info_init(p_vend, p_ca_info, ca_guid);
	if (status != IB_SUCCESS) {
		osm_ca_info_destroy(p_vend, p_ca_info);
		p_ca_info = NULL;
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (p_ca_info);
}

static ib_api_status_t
__osm_vendor_get_ca_guids(IN osm_vendor_t * const p_vend,
			  IN ib_net64_t ** const p_guids,
			  IN unsigned * const p_num_guids)
{
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_guids);
	CL_ASSERT(p_num_guids);

	status = ib_get_ca_guids(p_vend->h_al, NULL, p_num_guids);
	if ((status != IB_INSUFFICIENT_MEMORY) && (status != IB_SUCCESS)) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_guids: ERR 3B08: "
			"Unexpected status getting CA GUID array (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	if (*p_num_guids == 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_guids: ERR 3B09: "
			"No available channel adapters.\n");
		status = IB_INSUFFICIENT_RESOURCES;
		goto Exit;
	}

	*p_guids = malloc(*p_num_guids * sizeof(**p_guids));
	if (*p_guids == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_get_ca_guids: ERR 3B10: "
			"Unable to allocate CA GUID array.\n");
		goto Exit;
	}

	status = ib_get_ca_guids(p_vend->h_al, *p_guids, p_num_guids);
	CL_ASSERT(*p_num_guids);

	if (osm_log_is_active(p_vend->p_log, OSM_LOG_VERBOSE)) {
		osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
			"__osm_vendor_get_ca_guids: "
			"Detected %u local channel adapters.\n", *p_num_guids);
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

/****f* OpenSM: CA Info/osm_ca_info_get_pi_ptr
 * NAME
 * osm_ca_info_get_pi_ptr
 *
 * DESCRIPTION
 * Returns a pointer to the port attribute of the specified port
 * owned by this CA.
 *
 * SYNOPSIS
 */
static ib_port_attr_t *__osm_ca_info_get_port_attr_ptr(IN const osm_ca_info_t *
						       const p_ca_info,
						       IN const uint8_t index)
{
	return (&p_ca_info->p_attr->p_port_attr[index]);
}

/*
 * PARAMETERS
 * p_ca_info
 *    [in] Pointer to a CA Info object.
 *
 * index
 *    [in] Port "index" for which to retrieve the port attribute.
 *    The index is the offset into the ca's internal array
 *    of port attributes.
 *
 * RETURN VALUE
 * Returns a pointer to the port attribute of the specified port
 * owned by this CA.
 *
 * NOTES
 *
 * SEE ALSO
 *********/

ib_api_status_t
osm_vendor_get_all_port_attr(IN osm_vendor_t * const p_vend,
			     IN ib_port_attr_t * const p_attr_array,
			     IN uint32_t * const p_num_ports)
{
	ib_api_status_t status;

	uint32_t ca;
	unsigned ca_count;
	uint32_t port_count = 0;
	uint8_t port_num;
	uint32_t total_ports = 0;
	ib_net64_t *p_ca_guid = NULL;
	osm_ca_info_t *p_ca_info;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);
	CL_ASSERT(p_vend->p_ca_info == NULL);

	/*
	   1) Determine the number of CA's
	   2) Allocate an array big enough to hold the ca info objects.
	   3) Call again to retrieve the guids.
	 */
	status = __osm_vendor_get_ca_guids(p_vend, &p_ca_guid, &ca_count);

	p_vend->p_ca_info = malloc(ca_count * sizeof(*p_vend->p_ca_info));
	if (p_vend->p_ca_info == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get_all_port_attr: ERR 3B11: "
			"Unable to allocate CA information array.\n");
		goto Exit;
	}

	memset(p_vend->p_ca_info, 0, ca_count * sizeof(*p_vend->p_ca_info));
	p_vend->ca_count = ca_count;

	/*
	   For each CA, retrieve the port info attributes
	 */
	for (ca = 0; ca < ca_count; ca++) {
		p_ca_info = &p_vend->p_ca_info[ca];

		status = __osm_ca_info_init(p_vend, p_ca_info, p_ca_guid[ca]);

		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_get_all_port_attr: ERR 3B12: "
				"Unable to initialize CA Info object (%s).\n",
				ib_get_err_str(status));
		}

		total_ports += osm_ca_info_get_num_ports(p_ca_info);
	}

	/*
	   If the user supplied enough storage, return the port guids,
	   otherwise, return the appropriate error.
	 */
	if (*p_num_ports >= total_ports) {
		for (ca = 0; ca < ca_count; ca++) {
			uint32_t num_ports;

			p_ca_info = &p_vend->p_ca_info[ca];

			num_ports = osm_ca_info_get_num_ports(p_ca_info);

			for (port_num = 0; port_num < num_ports; port_num++) {
				p_attr_array[port_count] =
				    *__osm_ca_info_get_port_attr_ptr(p_ca_info,
								     port_num);
				port_count++;
			}
		}
	} else {
		status = IB_INSUFFICIENT_MEMORY;
	}

	*p_num_ports = total_ports;

Exit:
	if (p_ca_guid)
		free(p_ca_guid);

	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

ib_net64_t
osm_vendor_get_ca_guid(IN osm_vendor_t * const p_vend,
		       IN const ib_net64_t port_guid)
{
	uint8_t index;
	uint8_t num_ports;
	uint32_t num_guids = 0;
	osm_ca_info_t *p_ca_info;
	uint32_t ca;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(port_guid);
	/*
	   First, locate the HCA that owns this port.
	 */
	if (p_vend->p_ca_info == NULL) {
		/*
		   Initialize the osm_ca_info_t array which allows
		   us to match port GUID to CA.
		 */
		osm_vendor_get_all_port_attr(p_vend, NULL, &num_guids);
	}

	CL_ASSERT(p_vend->p_ca_info);
	CL_ASSERT(p_vend->ca_count);

	for (ca = 0; ca < p_vend->ca_count; ca++) {
		p_ca_info = &p_vend->p_ca_info[ca];

		num_ports = osm_ca_info_get_num_ports(p_ca_info);
		CL_ASSERT(num_ports);

		for (index = 0; index < num_ports; index++) {
			if (port_guid ==
			    osm_ca_info_get_port_guid(p_ca_info, index)) {
				OSM_LOG_EXIT(p_vend->p_log);
				return (osm_ca_info_get_ca_guid(p_ca_info));
			}
		}
	}

	/*
	   No local CA owns this guid!
	 */
	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_get_ca_guid: ERR 3B13: "
		"Unable to determine CA guid.\n");

	OSM_LOG_EXIT(p_vend->p_log);
	return (0);
}

uint8_t
osm_vendor_get_port_num(IN osm_vendor_t * const p_vend,
			IN const ib_net64_t port_guid)
{
	uint8_t index;
	uint8_t num_ports;
	uint32_t num_guids = 0;
	osm_ca_info_t *p_ca_info;
	uint32_t ca;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(port_guid);
	/*
	   First, locate the HCA that owns this port.
	 */
	if (p_vend->p_ca_info == NULL) {
		/*
		   Initialize the osm_ca_info_t array which allows
		   us to match port GUID to CA.
		 */
		osm_vendor_get_all_port_attr(p_vend, NULL, &num_guids);
	}

	CL_ASSERT(p_vend->p_ca_info);
	CL_ASSERT(p_vend->ca_count);

	for (ca = 0; ca < p_vend->ca_count; ca++) {
		p_ca_info = &p_vend->p_ca_info[ca];

		num_ports = osm_ca_info_get_num_ports(p_ca_info);
		CL_ASSERT(num_ports);

		for (index = 0; index < num_ports; index++) {
			if (port_guid ==
			    osm_ca_info_get_port_guid(p_ca_info, index)) {
				OSM_LOG_EXIT(p_vend->p_log);
				return (osm_ca_info_get_port_num
					(p_ca_info, index));
			}
		}
	}

	/*
	   No local CA owns this guid!
	 */
	osm_log(p_vend->p_log, OSM_LOG_ERROR,
		"osm_vendor_get_port_num: ERR 3B30: "
		"Unable to determine CA guid.\n");

	OSM_LOG_EXIT(p_vend->p_log);
	return (0);
}

static ib_api_status_t
__osm_vendor_open_ca(IN osm_vendor_t * const p_vend,
		     IN const ib_net64_t port_guid)
{
	ib_net64_t ca_guid;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	ca_guid = osm_vendor_get_ca_guid(p_vend, port_guid);
	if (ca_guid == 0) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_open_ca: ERR 3B31: "
			"Bad port GUID value 0x%" PRIx64 ".\n",
			cl_ntoh64(port_guid));
		status = IB_ERROR;
		goto Exit;
	}

	osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
		"__osm_vendor_open_ca: "
		"Opening HCA 0x%" PRIx64 ".\n", cl_ntoh64(ca_guid));

	status = ib_open_ca(p_vend->h_al,
			    ca_guid,
			    __osm_al_ca_err_callback, p_vend, &p_vend->h_ca);

	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_open_ca: ERR 3B15: "
			"Unable to open CA (%s).\n", ib_get_err_str(status));
		goto Exit;
	}

	CL_ASSERT(p_vend->h_ca);

	status = ib_alloc_pd(p_vend->h_ca, IB_PDT_ALIAS, p_vend, &p_vend->h_pd);

	if (status != IB_SUCCESS) {
		ib_close_ca(p_vend->h_ca, __osm_al_ca_destroy_callback);
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"__osm_vendor_open_ca: ERR 3B16: "
			"Unable to allocate protection domain (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	CL_ASSERT(p_vend->h_pd);

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

static void
__osm_vendor_init_av(IN const osm_al_bind_info_t * p_bind,
		     IN ib_av_attr_t * p_av)
{
	memset(p_av, 0, sizeof(*p_av));
	p_av->port_num = p_bind->port_num;
	p_av->dlid = IB_LID_PERMISSIVE;
}

osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_user_bind,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN osm_vend_mad_send_err_callback_t send_err_callback,
		IN void *context)
{
	ib_net64_t port_guid;
	osm_al_bind_info_t *p_bind = 0;
	ib_api_status_t status;
	ib_qp_create_t qp_create;
	ib_mad_svc_t mad_svc;
	ib_av_attr_t av;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_user_bind);
	CL_ASSERT(p_mad_pool);
	CL_ASSERT(mad_recv_callback);
	CL_ASSERT(send_err_callback);

	port_guid = p_user_bind->port_guid;

	osm_log(p_vend->p_log, OSM_LOG_INFO,
		"osm_vendor_bind: "
		"Binding to port 0x%" PRIx64 ".\n", cl_ntoh64(port_guid));

	if (p_vend->h_ca == 0) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_bind: "
			"Opening CA that owns port 0x%" PRIx64 ".\n",
			port_guid);

		status = __osm_vendor_open_ca(p_vend, port_guid);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_bind: ERR 3B17: "
				"Unable to Open CA (%s).\n",
				ib_get_err_str(status));
			goto Exit;
		}
	}

	p_bind = malloc(sizeof(*p_bind));
	if (p_bind == NULL) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3B18: "
			"Unable to allocate internal bind object.\n");
		goto Exit;
	}

	memset(p_bind, 0, sizeof(*p_bind));
	p_bind->p_vend = p_vend;
	p_bind->client_context = context;
	p_bind->port_num = osm_vendor_get_port_num(p_vend, port_guid);
	p_bind->rcv_callback = mad_recv_callback;
	p_bind->send_err_callback = send_err_callback;
	p_bind->p_osm_pool = p_mad_pool;

	CL_ASSERT(p_bind->port_num);

	/*
	   Get the proper QP.
	 */
	memset(&qp_create, 0, sizeof(qp_create));

	switch (p_user_bind->mad_class) {
	case IB_MCLASS_SUBN_LID:
	case IB_MCLASS_SUBN_DIR:
		qp_create.qp_type = IB_QPT_QP0_ALIAS;
		break;

	case IB_MCLASS_SUBN_ADM:
	default:
		qp_create.qp_type = IB_QPT_QP1_ALIAS;
		break;
	}

	qp_create.sq_depth = p_user_bind->send_q_size;
	qp_create.rq_depth = p_user_bind->recv_q_size;
	qp_create.sq_sge = OSM_AL_SQ_SGE;
	qp_create.rq_sge = OSM_AL_RQ_SGE;

	status = ib_get_spl_qp(p_vend->h_pd,
			       port_guid,
			       &qp_create,
			       p_bind,
			       __osm_al_err_callback,
			       &p_bind->pool_key, &p_bind->h_qp);

	if (status != IB_SUCCESS) {
		free(p_bind);
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3B19: "
			"Unable to get QP handle (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	CL_ASSERT(p_bind->h_qp);
	CL_ASSERT(p_bind->pool_key);

	memset(&mad_svc, 0, sizeof(mad_svc));

	mad_svc.mad_svc_context = p_bind;
	mad_svc.pfn_mad_send_cb = __osm_al_send_callback;
	mad_svc.pfn_mad_recv_cb = __osm_al_rcv_callback;
	mad_svc.mgmt_class = p_user_bind->mad_class;
	mad_svc.mgmt_version = p_user_bind->class_version;
	mad_svc.support_unsol = p_user_bind->is_responder;
	mad_svc.method_array[IB_MAD_METHOD_GET] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_SET] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_DELETE] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_TRAP] = TRUE;
	mad_svc.method_array[IB_MAD_METHOD_GETTABLE] = TRUE;

	status = ib_reg_mad_svc(p_bind->h_qp, &mad_svc, &p_bind->h_svc);

	if (status != IB_SUCCESS) {
		free(p_bind);
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3B21: "
			"Unable to register QP0 MAD service (%s).\n",
			ib_get_err_str(status));
		goto Exit;
	}

	__osm_vendor_init_av(p_bind, &av);

	status = ib_create_av(p_vend->h_pd, &av, &p_bind->h_dr_av);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_bind: ERR 3B22: "
			"Unable to create address vector (%s).\n",
			ib_get_err_str(status));

		goto Exit;
	}

	if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_bind: "
			"Allocating av handle %p.\n", p_bind->h_dr_av);
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return ((osm_bind_handle_t) p_bind);
}

ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t mad_size,
			 IN osm_vend_wrap_t * const p_vw)
{
	ib_mad_t *p_mad;
	osm_al_bind_info_t *p_bind = (osm_al_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);

	p_vw->size = mad_size;
	p_vw->h_bind = h_bind;

	/*
	   Retrieve a MAD element from the pool and give the user direct
	   access to its buffer.
	 */
	status = ib_get_mad(p_bind->pool_key, mad_size, &p_vw->p_elem);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_get: ERR 3B25: "
			"Unable to acquire MAD (%s).\n",
			ib_get_err_str(status));

		p_mad = NULL;
		goto Exit;
	}

	CL_ASSERT(p_vw->p_elem);
	p_mad = ib_get_mad_buf(p_vw->p_elem);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_get: "
			"Acquired MAD %p, size = %u.\n", p_mad, mad_size);
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (p_mad);
}

void
osm_vendor_put(IN osm_bind_handle_t h_bind, IN osm_vend_wrap_t * const p_vw)
{
	osm_al_bind_info_t *p_bind = (osm_al_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw);
	CL_ASSERT(p_vw->p_elem);
	CL_ASSERT(p_vw->h_bind == h_bind);

	if (osm_log_get_level(p_vend->p_log) >= OSM_LOG_DEBUG) {
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_put: "
			"Retiring MAD %p.\n", ib_get_mad_buf(p_vw->p_elem));
	}

	status = ib_put_mad(p_vw->p_elem);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_put: ERR 3B26: "
			"Unable to retire MAD (%s).\n", ib_get_err_str(status));
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_madw_t * const p_madw, IN boolean_t const resp_expected)
{
	osm_al_bind_info_t *const p_bind = h_bind;
	osm_vendor_t *const p_vend = p_bind->p_vend;
	osm_vend_wrap_t *const p_vw = osm_madw_get_vend_ptr(p_madw);
	osm_mad_addr_t *const p_mad_addr = osm_madw_get_mad_addr_ptr(p_madw);
	ib_mad_t *const p_mad = osm_madw_get_mad_ptr(p_madw);
	ib_api_status_t status;
	ib_mad_element_t *p_elem;
	ib_av_attr_t av;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vw->h_bind == h_bind);
	CL_ASSERT(p_vw->p_elem);

	p_elem = p_vw->p_elem;

	/*
	   If a response is expected to this MAD, then preallocate
	   a mad wrapper to contain the wire MAD received in the
	   response.  Allocating a wrapper here allows for easier
	   failure paths than after we already received the wire mad.
	 */
	if (resp_expected) {
		p_vw->p_resp_madw =
		    osm_mad_pool_get_wrapper_raw(p_bind->p_osm_pool);
		if (p_vw->p_resp_madw == NULL) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_send: ERR 3B27: "
				"Unable to allocate MAD wrapper.\n");
			status = IB_INSUFFICIENT_RESOURCES;
			goto Exit;
		}
	} else
		p_vw->p_resp_madw = NULL;

	/*
	   For all sends other than directed route SM MADs,
	   acquire an address vector for the destination.
	 */
	if (p_mad->mgmt_class != IB_MCLASS_SUBN_DIR) {
		memset(&av, 0, sizeof(av));
		av.port_num = p_bind->port_num;
		av.dlid = p_mad_addr->dest_lid;
		av.static_rate = p_mad_addr->static_rate;
		av.path_bits = p_mad_addr->path_bits;

		if ((p_mad->mgmt_class != IB_MCLASS_SUBN_LID) &&
		    (p_mad->mgmt_class != IB_MCLASS_SUBN_DIR)) {
			av.sl = p_mad_addr->addr_type.gsi.service_level;

			if (p_mad_addr->addr_type.gsi.global_route) {
				av.grh_valid = TRUE;
				/* ANIL */
				/* av.grh = p_mad_addr->addr_type.gsi.grh_info; */
			}
		}

		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_vendor_send: "
				"av.port_num 0x%X, "
				"av.dlid 0x%X, "
				"av.static_rate   %d, "
				"av.path_bits %d.\n",
				av.port_num, cl_ntoh16(av.dlid),
				av.static_rate, av.path_bits);
		}

		status = ib_create_av(p_vend->h_pd, &av, &p_vw->h_av);
		if (status != IB_SUCCESS) {
			osm_log(p_vend->p_log, OSM_LOG_ERROR,
				"osm_vendor_send: ERR 3B28: "
				"Unable to create address vector (%s).\n",
				ib_get_err_str(status));

			if (p_vw->p_resp_madw)
				osm_mad_pool_put(p_bind->p_osm_pool,
						 p_vw->p_resp_madw);
			goto Exit;
		}

		if (osm_log_is_active(p_vend->p_log, OSM_LOG_DEBUG)) {
			osm_log(p_vend->p_log, OSM_LOG_DEBUG,
				"osm_vendor_send: "
				"Allocating av handle %p.\n", p_vw->h_av);
		}
	} else {
		p_vw->h_av = p_bind->h_dr_av;
	}

	p_elem->h_av = p_vw->h_av;

	p_elem->context1 = p_madw;
	p_elem->context2 = NULL;

	p_elem->immediate_data = 0;
	p_elem->p_grh = NULL;
	p_elem->resp_expected = resp_expected;
	p_elem->retry_cnt = OSM_DEFAULT_RETRY_COUNT;

	p_elem->send_opt = IB_SEND_OPT_SIGNALED;
	p_elem->timeout_ms = p_vend->timeout;

	/* Completion information. */
	p_elem->status = 0;	/* Not trusting AL */

	if ((p_mad->mgmt_class == IB_MCLASS_SUBN_LID) ||
	    (p_mad->mgmt_class == IB_MCLASS_SUBN_DIR)) {
		p_elem->remote_qp = 0;
		p_elem->remote_qkey = 0;
	} else {
		p_elem->remote_qp = p_mad_addr->addr_type.gsi.remote_qp;
		p_elem->remote_qkey = p_mad_addr->addr_type.gsi.remote_qkey;
		osm_log(p_vend->p_log, OSM_LOG_DEBUG,
			"osm_vendor_send: "
			"remote qp = 0x%X, remote qkey = 0x%X.\n",
			cl_ntoh32(p_elem->remote_qp),
			cl_ntoh32(p_elem->remote_qkey));
	}

	status = ib_send_mad(p_bind->h_svc, p_elem, NULL);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_send: ERR 3B29: "
			"Send failed (%s).\n", ib_get_err_str(status));
		if (p_vw->p_resp_madw)
			osm_mad_pool_put(p_bind->p_osm_pool, p_vw->p_resp_madw);
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind)
{
	osm_al_bind_info_t *p_bind = (osm_al_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	ib_av_attr_t av;
	ib_api_status_t status;

	OSM_LOG_ENTER(p_vend->p_log);

	/*
	   The only thing we need to do is refresh the directed
	   route address vector.
	 */
	__osm_vendor_init_av(p_bind, &av);

	status = ib_destroy_av(p_bind->h_dr_av);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_local_lid_change: ERR 3B32: "
			"Unable to destroy address vector (%s).\n",
			ib_get_err_str(status));

		goto Exit;
	}

	status = ib_create_av(p_vend->h_pd, &av, &p_bind->h_dr_av);
	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_local_lid_change: ERR 3B33: "
			"Unable to create address vector (%s).\n",
			ib_get_err_str(status));

		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_vend->p_log);
	return (status);
}

void osm_vendor_set_sm(IN osm_bind_handle_t h_bind, IN boolean_t is_sm_val)
{
	osm_al_bind_info_t *p_bind = (osm_al_bind_info_t *) h_bind;
	osm_vendor_t *p_vend = p_bind->p_vend;
	ib_api_status_t status;
	ib_port_attr_mod_t attr_mod;

	OSM_LOG_ENTER(p_vend->p_log);

	memset(&attr_mod, 0, sizeof(attr_mod));

	attr_mod.cap.sm = is_sm_val;

	status = ib_modify_ca(p_vend->h_ca, p_bind->port_num,
			      IB_CA_MOD_IS_SM, &attr_mod);

	if (status != IB_SUCCESS) {
		osm_log(p_vend->p_log, OSM_LOG_ERROR,
			"osm_vendor_set_sm: ERR 3B34: "
			"Unable set 'IS_SM' bit to:%u in port attributes (%s).\n",
			is_sm_val, ib_get_err_str(status));
	}

	OSM_LOG_EXIT(p_vend->p_log);
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{

}

#endif				/* OSM_VENDOR_INTF_AL */
