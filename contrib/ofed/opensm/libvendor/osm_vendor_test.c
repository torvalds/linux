/*
 * Copyright (c) 2004-2008 Voltaire, Inc. All rights reserved.
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
 *    Implementation of vendor specific transport interface.
 *  This is the "Test" vendor which allows compilation and some
 *  testing without a real vendor interface.
 * These objects are part of the opensm family of objects.
 *
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#ifdef OSM_VENDOR_INTF_TEST

#include <stdlib.h>
#include <string.h>
#include <opensm/osm_log.h>
#include <vendor/osm_vendor_test.h>
#include <vendor/osm_vendor_api.h>

void osm_vendor_construct(IN osm_vendor_t * const p_vend)
{
	memset(p_vend, 0, sizeof(*p_vend));
}

void osm_vendor_destroy(IN osm_vendor_t * const p_vend)
{
	UNUSED_PARAM(p_vend);
}

void osm_vendor_delete(IN osm_vendor_t ** const pp_vend)
{
	CL_ASSERT(pp_vend);

	osm_vendor_destroy(*pp_vend);
	free(*pp_vend);
	*pp_vend = NULL;
}

ib_api_status_t
osm_vendor_init(IN osm_vendor_t * const p_vend,
		IN osm_log_t * const p_log, IN const uint32_t timeout)
{
	OSM_LOG_ENTER(p_log);

	CL_ASSERT(p_vend);
	CL_ASSERT(p_log);

	p_vend->p_log = p_log;
	p_vend->timeout = timeout;
	OSM_LOG_EXIT(p_log);
	return (IB_SUCCESS);
}

osm_vendor_t *osm_vendor_new(IN osm_log_t * const p_log,
			     IN const uint32_t timeout)
{
	ib_api_status_t status;
	osm_vendor_t *p_vend;
	OSM_LOG_ENTER(p_log);

	CL_ASSERT(p_log);

	p_vend = malloc(sizeof(*p_vend));
	if (p_vend != NULL) {
		memset(p_vend, 0, sizeof(*p_vend));

		status = osm_vendor_init(p_vend, p_log, timeout);
		if (status != IB_SUCCESS) {
			osm_vendor_delete(&p_vend);
		}
	}

	OSM_LOG_EXIT(p_log);
	return (p_vend);
}

ib_mad_t *osm_vendor_get(IN osm_bind_handle_t h_bind,
			 IN const uint32_t size,
			 IN osm_vend_wrap_t * const p_vend_wrap)
{
	osm_vendor_t *p_vend;
	ib_mad_t *p_mad;
	OSM_LOG_ENTER(h_bind->p_vend->p_log);

	UNUSED_PARAM(p_vend_wrap);

	p_vend = h_bind->p_vend;

	/*
	   Simply malloc the MAD off the heap.
	 */
	p_mad = (ib_mad_t *) malloc(size);

	osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
		"osm_vendor_get: " "MAD %p.\n", p_mad);

	if (p_mad)
		memset(p_mad, 0, size);

	OSM_LOG_EXIT(p_vend->p_log);
	return (p_mad);
}

void
osm_vendor_put(IN osm_bind_handle_t h_bind,
	       IN osm_vend_wrap_t * const p_vend_wrap,
	       IN ib_mad_t * const p_mad)
{
	osm_vendor_t *p_vend;

	OSM_LOG_ENTER(h_bind->p_vend->p_log);

	UNUSED_PARAM(p_vend_wrap);

	p_vend = h_bind->p_vend;

	osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
		"osm_vendor_put: " "MAD %p.\n", p_mad);

	/*
	   Return the MAD to the heap.
	 */
	free(p_mad);

	OSM_LOG_EXIT(p_vend->p_log);
}

ib_api_status_t
osm_vendor_send(IN osm_bind_handle_t h_bind,
		IN osm_vend_wrap_t * const p_vend_wrap,
		IN osm_mad_addr_t * const p_mad_addr,
		IN ib_mad_t * const p_mad,
		IN void *transaction_context, IN boolean_t const resp_expected)
{
	osm_vendor_t *p_vend = h_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	UNUSED_PARAM(p_vend_wrap);
	UNUSED_PARAM(p_mad_addr);
	UNUSED_PARAM(transaction_context);
	UNUSED_PARAM(resp_expected);

	osm_log(p_vend->p_log, OSM_LOG_VERBOSE,
		"osm_vendor_send: " "MAD %p.\n", p_mad);

	OSM_LOG_EXIT(p_vend->p_log);
	return (IB_SUCCESS);
}

osm_bind_handle_t
osm_vendor_bind(IN osm_vendor_t * const p_vend,
		IN osm_bind_info_t * const p_bind_info,
		IN osm_mad_pool_t * const p_mad_pool,
		IN osm_vend_mad_recv_callback_t mad_recv_callback,
		IN void *context)
{
	osm_bind_handle_t h_bind;

	OSM_LOG_ENTER(p_vend->p_log);

	CL_ASSERT(p_vend);
	CL_ASSERT(p_bind_info);
	CL_ASSERT(p_mad_pool);
	CL_ASSERT(mad_recv_callback);
	CL_ASSERT(context);

	UNUSED_PARAM(p_vend);
	UNUSED_PARAM(p_mad_pool);
	UNUSED_PARAM(mad_recv_callback);
	UNUSED_PARAM(context);

	h_bind = (osm_bind_handle_t) malloc(sizeof(*h_bind));
	if (h_bind != NULL) {
		memset(h_bind, 0, sizeof(*h_bind));
		h_bind->p_vend = p_vend;
		h_bind->port_guid = p_bind_info->port_guid;
		h_bind->mad_class = p_bind_info->mad_class;
		h_bind->class_version = p_bind_info->class_version;
		h_bind->is_responder = p_bind_info->is_responder;
		h_bind->is_trap_processor = p_bind_info->is_trap_processor;
		h_bind->is_report_processor = p_bind_info->is_report_processor;
		h_bind->send_q_size = p_bind_info->send_q_size;
		h_bind->recv_q_size = p_bind_info->recv_q_size;
	}

	OSM_LOG_EXIT(p_vend->p_log);
	return (h_bind);
}

ib_api_status_t
osm_vendor_get_ports(IN osm_vendor_t * const p_vend,
		     IN ib_net64_t * const p_guids,
		     IN uint32_t * const num_guids)
{
	OSM_LOG_ENTER(p_vend->p_log);

	*p_guids = CL_NTOH64(0x0000000000001234);
	*num_guids = 1;

	OSM_LOG_EXIT(p_vend->p_log);
	return (IB_SUCCESS);
}

ib_api_status_t osm_vendor_local_lid_change(IN osm_bind_handle_t h_bind)
{
	osm_vendor_t *p_vend = h_bind->p_vend;

	OSM_LOG_ENTER(p_vend->p_log);

	OSM_LOG_EXIT(p_vend->p_log);

	return (IB_SUCCESS);
}

void osm_vendor_set_debug(IN osm_vendor_t * const p_vend, IN int32_t level)
{

}

#endif				/* OSM_VENDOR_INTF_TEST */
