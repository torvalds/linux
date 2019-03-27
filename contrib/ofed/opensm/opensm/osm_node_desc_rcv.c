/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2007 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2013 Oracle and/or its affiliates. All rights reserved.
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
 *    Implementation of osm_nd_rcv_t.
 * This object represents the NodeDescription Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_NODE_DESC_RCV_C
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_subnet.h>

static void nd_rcv_process_nd(IN osm_sm_t * sm, IN osm_node_t * p_node,
			      IN const ib_node_desc_t * p_nd)
{
	char *tmp_desc;
	char print_desc[IB_NODE_DESCRIPTION_SIZE + 1];

	OSM_LOG_ENTER(sm->p_log);

	memcpy(&p_node->node_desc.description, p_nd, sizeof(*p_nd));

	/* also set up a printable version */
	memcpy(print_desc, p_nd, sizeof(*p_nd));
	print_desc[IB_NODE_DESCRIPTION_SIZE] = '\0';
	tmp_desc = remap_node_name(sm->p_subn->p_osm->node_name_map,
				   cl_ntoh64(osm_node_get_node_guid(p_node)),
				   print_desc);

	/* make a copy for this node to "own" */
	if (p_node->print_desc)
		free(p_node->print_desc);
	p_node->print_desc = tmp_desc;

#ifdef ENABLE_OSM_PERF_MGR
	/* update the perfmgr entry if available */
	osm_perfmgr_update_nodename(&sm->p_subn->p_osm->perfmgr,
				cl_ntoh64(osm_node_get_node_guid(p_node)),
				p_node->print_desc);
#endif				/* ENABLE_OSM_PERF_MGR */

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Node 0x%" PRIx64 ", Description = %s\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)), p_node->print_desc);

	OSM_LOG_EXIT(sm->p_log);
}

void osm_nd_rcv_process(IN void *context, IN void *data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = data;
	ib_node_desc_t *p_nd;
	ib_smp_t *p_smp;
	osm_node_t *p_node;
	ib_net64_t node_guid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	if (ib_smp_get_status(p_smp)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(ib_smp_get_status(p_smp)));
		goto Exit;
	}

	p_nd = ib_smp_get_payload_ptr(p_smp);

	/* Acquire the node object and add the node description. */
	node_guid = osm_madw_get_nd_context_ptr(p_madw)->node_guid;
	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
	p_node = osm_get_node_by_guid(sm->p_subn, node_guid);
	if (!p_node)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0B01: "
			"NodeDescription received for nonexistent node "
			"0x%" PRIx64 "\n", cl_ntoh64(node_guid));
	else
		nd_rcv_process_nd(sm, p_node, p_nd);

	CL_PLOCK_RELEASE(sm->p_lock);
Exit:
	OSM_LOG_EXIT(sm->p_log);
}
