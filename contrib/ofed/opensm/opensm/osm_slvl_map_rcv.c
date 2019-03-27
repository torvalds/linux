/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of osm_slvl_rcv_t.
 * This object represents the SLtoVL Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_SLVL_MAP_RCV_C
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_sm.h>

/*
 * WE ONLY RECEIVE GET or SET responses
 */
void osm_slvl_rcv_process(IN void *context, IN void *p_data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = p_data;
	ib_slvl_table_t *p_slvl_tbl;
	ib_smp_t *p_smp;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	osm_slvl_context_t *p_context;
	ib_net64_t port_guid;
	ib_net64_t node_guid;
	uint32_t attr_mod;
	uint8_t startinport, endinport, startoutport, endoutport;
	uint8_t in_port, out_port;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_context = osm_madw_get_slvl_context_ptr(p_madw);
	p_slvl_tbl = ib_smp_get_payload_ptr(p_smp);

	port_guid = p_context->port_guid;
	node_guid = p_context->node_guid;

	CL_ASSERT(p_smp->attr_id == IB_MAD_ATTR_SLVL_TABLE);

	if (!sm->p_subn->opt.suppress_sl2vl_mad_status_errors &&
	    ib_smp_get_status(p_smp)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(ib_smp_get_status(p_smp)));
		goto Exit2;
	}

	cl_plock_excl_acquire(sm->p_lock);
	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);

	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 2C06: "
			"No port object for port with GUID 0x%" PRIx64
			"\n\t\t\t\tfor parent node GUID 0x%" PRIx64
			", TID 0x%" PRIx64 "\n", cl_ntoh64(port_guid),
			cl_ntoh64(node_guid), cl_ntoh64(p_smp->trans_id));
		goto Exit;
	}

	p_node = p_port->p_node;
	CL_ASSERT(p_node);

	/* in case of a non switch node the attr modifier should be ignored */
	if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH) {
		unsigned num_ports = osm_node_get_num_physp(p_node) - 1;
		attr_mod = cl_ntoh32(p_smp->attr_mod);

		if (attr_mod & 0x10000) {
			startoutport = ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info) ? 0 : 1;
			endoutport = osm_node_get_num_physp(p_node) - 1;
		} else
			startoutport = endoutport = attr_mod & 0xff;

		if (attr_mod & 0x20000) {
			startinport = ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info) ? 0 : 1;
			endinport = osm_node_get_num_physp(p_node) - 1;
		} else
			startinport = endinport = (attr_mod >> 8) & 0xff;

		if (startinport > num_ports || startoutport > num_ports) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 2C07"
				"Invalid attribute modifier 0x%x received in"
				" response from switch 0x%" PRIx64 "\n",
				cl_ntoh32(attr_mod), cl_ntoh64(node_guid));
			goto Exit;
		}

	} else {
		startoutport = endoutport = p_port->p_physp->port_num;
		startinport = endinport = 0;
	}

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Received SLtoVL GetResp"
		" in_port_num %u out_port_num %u with GUID 0x%" PRIx64
		" for parent node GUID 0x%" PRIx64 ", TID 0x%" PRIx64 "\n",
		startinport == endinport ? startinport : 0xff,
		startoutport == endoutport ? startoutport : 0xff,
		cl_ntoh64(port_guid), cl_ntoh64(node_guid),
		cl_ntoh64(p_smp->trans_id));

	osm_dump_slvl_map_table_v2(sm->p_log, port_guid,
				   startinport == endinport ? startinport : 0xff,
				   startoutport == endoutport ? startoutport : 0xff,
				   p_slvl_tbl, FILE_ID, OSM_LOG_DEBUG);

	for (out_port = startoutport; out_port <= endoutport; out_port++) {
		p_physp = osm_node_get_physp_ptr(p_node, out_port);
		for (in_port = startinport; in_port <= endinport; in_port++)
			osm_physp_set_slvl_tbl(p_physp, p_slvl_tbl, in_port);
	}

Exit:
	cl_plock_release(sm->p_lock);

Exit2:
	OSM_LOG_EXIT(sm->p_log);
}
