/*
 * Copyright (c) 2011 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_mlnx_epi_rcv_t.
 * This object represents the MLNX ExtendedPortInfo Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <stdlib.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MLNX_EXT_PORT_INFO_RCV_C
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_opensm.h>

void osm_mlnx_epi_rcv_process(IN void *context, IN void *data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = data;
	ib_mlnx_ext_port_info_t *p_pi;
	ib_smp_t *p_smp;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	osm_pi_context_t *p_context;
	ib_net64_t port_guid, node_guid;
	uint8_t port_num, portnum, start_port = 1;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(sm);
	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_context = osm_madw_get_pi_context_ptr(p_madw);
	p_pi = ib_smp_get_payload_ptr(p_smp);

	CL_ASSERT(p_smp->attr_id == IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO);

	port_num = (uint8_t) cl_ntoh32(p_smp->attr_mod);

	port_guid = p_context->port_guid;
	node_guid = p_context->node_guid;

	osm_dump_mlnx_ext_port_info_v2(sm->p_log, node_guid, port_guid, port_num,
				       p_pi, FILE_ID, OSM_LOG_DEBUG);

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);
	if (!p_port) {
		CL_PLOCK_RELEASE(sm->p_lock);
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0F06: "
			"No port object for port with GUID 0x%" PRIx64
			"\n\t\t\t\tfor parent node GUID 0x%" PRIx64
			", TID 0x%" PRIx64 "\n",
			cl_ntoh64(port_guid),
			cl_ntoh64(node_guid), cl_ntoh64(p_smp->trans_id));
		goto Exit;
	}

	p_node = p_port->p_node;
	CL_ASSERT(p_node);

	if (!(cl_ntoh16(p_smp->status) & 0x7fff)) {
		if (port_num != 255) {
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			CL_ASSERT(p_physp);
			p_physp->ext_port_info = *p_pi;
		} else {
			/* Handle all ports on set/set resp */
			if (p_node->sw &&
			    ib_switch_info_is_enhanced_port0(&p_node->sw->switch_info))
				start_port = 0;

			for (portnum = start_port;
			     portnum < osm_node_get_num_physp(p_node);
			     portnum++) {
				p_physp = osm_node_get_physp_ptr(p_node, portnum);
				CL_ASSERT(p_physp);
				p_physp->ext_port_info = *p_pi;
			}
		}
	} else
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(p_smp->status) & 0x7fff);

	CL_PLOCK_RELEASE(sm->p_lock);

Exit:
	/*
	   Release the lock before jumping here!!
	 */
	OSM_LOG_EXIT(sm->p_log);
}
