/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_ni_rcv_t.
 * This object represents the NodeInfo Receiver object.
 * This object is part of the opensm family of objects.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_qmap.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_NODE_INFO_RCV_C
#include <opensm/osm_madw.h>
#include <opensm/osm_log.h>
#include <opensm/osm_node.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_router.h>
#include <opensm/osm_mad_pool.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_ucast_mgr.h>
#include <opensm/osm_db_pack.h>

static void report_duplicated_guid(IN osm_sm_t * sm, osm_physp_t * p_physp,
				   osm_node_t * p_neighbor_node,
				   const uint8_t port_num)
{
	osm_physp_t *p_old, *p_new;
	osm_dr_path_t path;

	p_old = p_physp->p_remote_physp;
	p_new = osm_node_get_physp_ptr(p_neighbor_node, port_num);

	OSM_LOG(sm->p_log, OSM_LOG_SYS | OSM_LOG_ERROR, "ERR 0D01: "
		"Found duplicated node GUID.\n"
		"Node 0x%" PRIx64 " port %u is reachable from remote node "
		"0x%" PRIx64 " port %u and remote node 0x%" PRIx64 " port %u.\n"
		"Paths are:\n",
		cl_ntoh64(p_physp->p_node->node_info.node_guid),
		p_physp->port_num,
		p_old ? cl_ntoh64(p_old->p_node->node_info.node_guid) : 0,
		p_old ? p_old->port_num : 0,
		p_new ? cl_ntoh64(p_new->p_node->node_info.node_guid) : 0,
		p_new ? p_new->port_num : 0);

	osm_dump_dr_path_v2(sm->p_log, osm_physp_get_dr_path_ptr(p_physp),
			    FILE_ID, OSM_LOG_ERROR);

	path = *osm_physp_get_dr_path_ptr(p_new);
	if (osm_dr_path_extend(&path, port_num))
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D05: "
			"DR path with hop count %d couldn't be extended\n",
			path.hop_count);
	osm_dump_dr_path_v2(sm->p_log, &path, FILE_ID, OSM_LOG_ERROR);
}

static void requery_dup_node_info(IN osm_sm_t * sm, osm_physp_t * p_physp,
				  unsigned count)
{
	osm_madw_context_t context;
	osm_dr_path_t path;
	cl_status_t status;

	if (!p_physp->p_remote_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D0D: "
			"DR path couldn't be extended due to NULL remote physp\n");
		return;
	}

	path = *osm_physp_get_dr_path_ptr(p_physp->p_remote_physp);
	if (osm_dr_path_extend(&path, p_physp->p_remote_physp->port_num)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D08: "
			"DR path with hop count %d couldn't be extended\n",
			path.hop_count);
		return;
	}

	context.ni_context.node_guid =
	    p_physp->p_remote_physp->p_node->node_info.port_guid;
	context.ni_context.port_num = p_physp->p_remote_physp->port_num;
	context.ni_context.dup_node_guid = p_physp->p_node->node_info.node_guid;
	context.ni_context.dup_port_num = p_physp->port_num;
	context.ni_context.dup_count = count;

	status = osm_req_get(sm, &path, IB_MAD_ATTR_NODE_INFO, 0,
			     TRUE, 0, CL_DISP_MSGID_NONE, &context);

	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D02: "
			"Failure initiating NodeInfo request (%s)\n",
			ib_get_err_str(status));
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_set_links(IN osm_sm_t * sm, osm_node_t * p_node,
			     const uint8_t port_num,
			     const osm_ni_context_t * p_ni_context)
{
	osm_node_t *p_neighbor_node;
	osm_physp_t *p_physp, *p_remote_physp;

	OSM_LOG_ENTER(sm->p_log);

	/*
	   A special case exists in which the node we're trying to
	   link is our own node.  In this case, the guid value in
	   the ni_context will be zero.
	 */
	if (p_ni_context->node_guid == 0) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Nothing to link for our own node 0x%" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		goto _exit;
	}

	p_neighbor_node = osm_get_node_by_guid(sm->p_subn,
					       p_ni_context->node_guid);
	if (PF(!p_neighbor_node)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D10: "
			"Unexpected removal of neighbor node 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni_context->node_guid));
		goto _exit;
	}

	/* When setting the link, ports on both
	   sides of the link should be initialized */
	CL_ASSERT(osm_node_link_has_valid_ports(p_node, port_num,
						p_neighbor_node,
						p_ni_context->port_num));

	if (osm_node_link_exists(p_node, port_num,
				 p_neighbor_node, p_ni_context->port_num)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Link already exists\n");
		goto _exit;
	}

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	if (!p_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD0E: "
			"Failed to find physp for port %d of Node GUID 0x%"
			PRIx64 "\n", port_num,
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		goto _exit;
	}

	/*
	 * If the link went UP, after we already discovered it, we shouldn't
	 * set the link between the ports and resweep.
	 */
	if (osm_physp_get_port_state(p_physp) == IB_LINK_DOWN &&
	    p_node->physp_discovered[port_num]) {
		/* Link down on another side. Don't create a link*/
		p_node->physp_discovered[port_num] = 0;
		sm->p_subn->force_heavy_sweep = TRUE;
		goto _exit;
	}

	if (osm_node_has_any_link(p_node, port_num) &&
	    sm->p_subn->force_heavy_sweep == FALSE &&
	    (!p_ni_context->dup_count ||
	     (p_ni_context->dup_node_guid == osm_node_get_node_guid(p_node) &&
	      p_ni_context->dup_port_num == port_num))) {
		/*
		   Uh oh...
		   This could be reconnected ports, but also duplicated GUID
		   (2 nodes have the same guid) or a 12x link with lane reversal
		   that is not configured correctly.
		   We will try to recover by querying NodeInfo again.
		   In order to catch even fast port moving to new location(s)
		   and back we will count up to 5.
		   Some crazy reconnections (newly created switch loop right
		   before targeted CA) will not be catched this way. So in worst
		   case - report GUID duplication and request new discovery.
		   When switch node is targeted NodeInfo querying will be done
		   in opposite order, this is much stronger check, unfortunately
		   it is impossible with CAs.
		 */
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (!p_physp) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD0F: "
				"Failed to find physp for port %d of Node GUID 0x%"
				PRIx64 "\n", port_num,
				cl_ntoh64(osm_node_get_node_guid(p_node)));
			goto _exit;
		}

		if (p_ni_context->dup_count > 5) {
			report_duplicated_guid(sm, p_physp, p_neighbor_node,
					       p_ni_context->port_num);
			sm->p_subn->force_heavy_sweep = TRUE;
		} else if (p_node->sw)
			requery_dup_node_info(sm, p_physp->p_remote_physp,
					      p_ni_context->dup_count + 1);
		else
			requery_dup_node_info(sm, p_physp,
					      p_ni_context->dup_count + 1);
	}

	/*
	   When there are only two nodes with exact same guids (connected back
	   to back) - the previous check for duplicated guid will not catch
	   them. But the link will be from the port to itself...
	   Enhanced Port 0 is an exception to this
	 */
	if (osm_node_get_node_guid(p_node) == p_ni_context->node_guid &&
	    port_num == p_ni_context->port_num &&
	    port_num != 0 && cl_qmap_count(&sm->p_subn->sw_guid_tbl) == 0) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Duplicate GUID found by link from a port to itself:"
			"node 0x%" PRIx64 ", port number %u\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)), port_num);
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (!p_physp) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD1D: "
				"Failed to find physp for port %d of Node GUID 0x%"
				PRIx64 "\n", port_num,
				cl_ntoh64(osm_node_get_node_guid(p_node)));
			goto _exit;
		}

		osm_dump_dr_path_v2(sm->p_log, osm_physp_get_dr_path_ptr(p_physp),
				    FILE_ID, OSM_LOG_VERBOSE);

		if (sm->p_subn->opt.exit_on_fatal == TRUE) {
			osm_log_v2(sm->p_log, OSM_LOG_SYS, FILE_ID,
				   "Errors on subnet. Duplicate GUID found "
				   "by link from a port to itself. "
				   "See verbose opensm.log for more details\n");
			exit(1);
		}
	}

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Creating new link between:\n\t\t\t\tnode 0x%" PRIx64
		", port number %u and\n\t\t\t\tnode 0x%" PRIx64
		", port number %u\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)), port_num,
		cl_ntoh64(p_ni_context->node_guid), p_ni_context->port_num);

	if (sm->ucast_mgr.cache_valid)
		osm_ucast_cache_check_new_link(&sm->ucast_mgr, p_node, port_num,
					       p_neighbor_node,
					       p_ni_context->port_num);

	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	p_remote_physp = osm_node_get_physp_ptr(p_neighbor_node,
						p_ni_context->port_num);
	if (!p_physp || !p_remote_physp)
		goto _exit;

	osm_node_link(p_node, port_num, p_neighbor_node, p_ni_context->port_num);

	osm_db_neighbor_set(sm->p_subn->p_neighbor,
			    cl_ntoh64(osm_physp_get_port_guid(p_physp)),
			    port_num,
			    cl_ntoh64(osm_physp_get_port_guid(p_remote_physp)),
			    p_ni_context->port_num);
	osm_db_neighbor_set(sm->p_subn->p_neighbor,
			    cl_ntoh64(osm_physp_get_port_guid(p_remote_physp)),
			    p_ni_context->port_num,
			    cl_ntoh64(osm_physp_get_port_guid(p_physp)),
			    port_num);

_exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void ni_rcv_get_port_info(IN osm_sm_t * sm, IN osm_node_t * node,
				 IN const osm_madw_t * madw)
{
	osm_madw_context_t context;
	osm_physp_t *physp;
	ib_node_info_t *ni;
	unsigned port;
	ib_api_status_t status;
	int mlnx_epi_supported = 0;

	ni = ib_smp_get_payload_ptr(osm_madw_get_smp_ptr(madw));

	port = ib_node_info_get_local_port_num(ni);

	if (sm->p_subn->opt.fdr10)
		mlnx_epi_supported = is_mlnx_ext_port_info_supported(
						ib_node_info_get_vendor_id(ni),
						ni->device_id);

	physp = osm_node_get_physp_ptr(node, port);
	if (!physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD1E: "
			"Failed to find physp for port %d of Node GUID 0x%"
			PRIx64 "\n", port,
			cl_ntoh64(osm_node_get_node_guid(node)));
		return;
	}

	context.pi_context.node_guid = osm_node_get_node_guid(node);
	context.pi_context.port_guid = osm_physp_get_port_guid(physp);
	context.pi_context.set_method = FALSE;
	context.pi_context.light_sweep = FALSE;
	context.pi_context.active_transition = FALSE;
	context.pi_context.client_rereg = FALSE;

	status = osm_req_get(sm, osm_physp_get_dr_path_ptr(physp),
			     IB_MAD_ATTR_PORT_INFO, cl_hton32(port),
			     TRUE, 0, CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD02: "
			"Failure initiating PortInfo request (%s)\n",
			ib_get_err_str(status));
	if (mlnx_epi_supported) {
		status = osm_req_get(sm,
				     osm_physp_get_dr_path_ptr(physp),
				     IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO,
				     cl_hton32(port),
				     TRUE, 0, CL_DISP_MSGID_NONE, &context);
		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D0B: "
				"Failure initiating MLNX ExtPortInfo request (%s)\n",
				ib_get_err_str(status));
	}
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
void osm_req_get_node_desc(IN osm_sm_t * sm, osm_physp_t * p_physp)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_madw_context_t context;

	OSM_LOG_ENTER(sm->p_log);

	context.nd_context.node_guid =
	    osm_node_get_node_guid(osm_physp_get_node_ptr(p_physp));

	status = osm_req_get(sm, osm_physp_get_dr_path_ptr(p_physp),
			     IB_MAD_ATTR_NODE_DESC, 0, TRUE, 0,
			     CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D03: "
			"Failure initiating NodeDescription request (%s)\n",
			ib_get_err_str(status));

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_get_node_desc(IN osm_sm_t * sm, IN osm_node_t * p_node,
				 IN const osm_madw_t * p_madw)
{
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	uint8_t port_num;
	osm_physp_t *p_physp = NULL;

	OSM_LOG_ENTER(sm->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);
	port_num = ib_node_info_get_local_port_num(p_ni);

	/*
	   Request PortInfo & NodeDescription attributes for the port
	   that responded to the NodeInfo attribute.
	   Because this is a channel adapter or router, we are
	   not allowed to request PortInfo for the other ports.
	   Set the context union properly, so the recipient
	   knows which node & port are relevant.
	 */
	p_physp = osm_node_get_physp_ptr(p_node, port_num);
	if (!p_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR OD1F: "
			"Failed to find physp for port %d of Node GUID 0x%"
			PRIx64 "\n", port_num,
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		return;
	}

	osm_req_get_node_desc(sm, p_physp);

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_process_new_ca_or_router(IN osm_sm_t * sm,
					    IN osm_node_t * p_node,
					    IN const osm_madw_t * p_madw)
{
	OSM_LOG_ENTER(sm->p_log);

	ni_rcv_get_port_info(sm, p_node, p_madw);

	/*
	   A node guid of 0 is the corner case that indicates
	   we discovered our own node.  Initialize the subnet
	   object with the SM's own port guid.
	 */
	if (osm_madw_get_ni_context_ptr(p_madw)->node_guid == 0)
		sm->p_subn->sm_port_guid = p_node->node_info.port_guid;

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_process_existing_ca_or_router(IN osm_sm_t * sm,
						 IN osm_node_t * p_node,
						 IN const osm_madw_t * p_madw)
{
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_port_t *p_port;
	osm_port_t *p_port_check;
	uint8_t port_num;
	osm_dr_path_t *p_dr_path;
	osm_alias_guid_t *p_alias_guid, *p_alias_guid_check;

	OSM_LOG_ENTER(sm->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);
	port_num = ib_node_info_get_local_port_num(p_ni);

	/*
	   Determine if we have encountered this node through a
	   previously undiscovered port.  If so, build the new
	   port object.
	 */
	p_port = osm_get_port_by_guid(sm->p_subn, p_ni->port_guid);
	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Creating new port object with GUID 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->port_guid));

		osm_node_init_physp(p_node, port_num, p_madw);

		p_port = osm_port_new(p_ni, p_node);
		if (PF(p_port == NULL)) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D04: "
				"Unable to create new port object\n");
			goto Exit;
		}

		/*
		   Add the new port object to the database.
		 */
		p_port_check =
		    (osm_port_t *) cl_qmap_insert(&sm->p_subn->port_guid_tbl,
						  p_ni->port_guid,
						  &p_port->map_item);
		if (PF(p_port_check != p_port)) {
			/*
			   We should never be here!
			   Somehow, this port GUID already exists in the table.
			 */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D12: "
				"Port 0x%" PRIx64 " already in the database!\n",
				cl_ntoh64(p_ni->port_guid));

			osm_port_delete(&p_port);
			goto Exit;
		}

		p_alias_guid = osm_alias_guid_new(p_ni->port_guid,
						  p_port);
		if (PF(!p_alias_guid)) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D11: "
				"alias guid memory allocation failed"
				" for port GUID 0x%" PRIx64 "\n",
				cl_ntoh64(p_ni->port_guid));
			goto alias_done;
		}

		/* insert into alias guid table */
		p_alias_guid_check =
			(osm_alias_guid_t *) cl_qmap_insert(&sm->p_subn->alias_port_guid_tbl,
							    p_alias_guid->alias_guid,
							    &p_alias_guid->map_item);
		if (p_alias_guid_check != p_alias_guid) {
			/* alias GUID is a duplicate */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D13: "
				"Duplicate alias port GUID 0x%" PRIx64 "\n",
				cl_ntoh64(p_ni->port_guid));
			osm_alias_guid_delete(&p_alias_guid);
			osm_port_delete(&p_port);
			goto Exit;
		}

alias_done:
		/* If we are a master, then this means the port is new on the subnet.
		   Mark it as new - need to send trap 64 for these ports.
		   The condition that we are master is true, since if we are in discovering
		   state (meaning we woke up from standby or we are just initializing),
		   then these ports may be new to us, but are not new on the subnet.
		   If we are master, then the subnet as we know it is the updated one,
		   and any new ports we encounter should cause trap 64. C14-72.1.1 */
		if (sm->p_subn->sm_state == IB_SMINFO_STATE_MASTER)
			p_port->is_new = 1;

	} else {
		osm_physp_t *p_physp = osm_node_get_physp_ptr(p_node, port_num);

		if (PF(p_physp == NULL)) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D1C: "
				"No physical port found for node GUID 0x%"
				PRIx64 " port %u. Might be duplicate port GUID\n",
				cl_ntoh64(p_node->node_info.node_guid),
				port_num);
			goto Exit;
		}

		/*
		   Update the DR Path to the port,
		   in case the old one is no longer available.
		 */
		p_dr_path = osm_physp_get_dr_path_ptr(p_physp);

		osm_dr_path_init(p_dr_path, p_smp->hop_count,
				 p_smp->initial_path);
	}

	ni_rcv_get_port_info(sm, p_node, p_madw);

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void ni_rcv_process_switch(IN osm_sm_t * sm, IN osm_node_t * p_node,
				  IN const osm_madw_t * p_madw)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_physp_t *p_physp;
	osm_madw_context_t context;
	osm_dr_path_t *path;
	ib_smp_t *p_smp;

	OSM_LOG_ENTER(sm->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	/* update DR path of already initialized switch port 0 */
	path = osm_physp_get_dr_path_ptr(p_physp);
	osm_dr_path_init(path, p_smp->hop_count, p_smp->initial_path);

	context.si_context.node_guid = osm_node_get_node_guid(p_node);
	context.si_context.set_method = FALSE;
	context.si_context.light_sweep = FALSE;
	context.si_context.lft_top_change = FALSE;

	/* Request a SwitchInfo attribute */
	status = osm_req_get(sm, path, IB_MAD_ATTR_SWITCH_INFO, 0, TRUE, 0,
			     CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		/* continue despite error */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D06: "
			"Failure initiating SwitchInfo request (%s)\n",
			ib_get_err_str(status));

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_process_existing_switch(IN osm_sm_t * sm,
					   IN osm_node_t * p_node,
					   IN const osm_madw_t * p_madw)
{
	OSM_LOG_ENTER(sm->p_log);

	/*
	   If this switch has already been probed during this sweep,
	   then don't bother reprobing it.
	 */
	if (p_node->discovery_count == 1)
		ni_rcv_process_switch(sm, p_node, p_madw);

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_process_new_switch(IN osm_sm_t * sm, IN osm_node_t * p_node,
				      IN const osm_madw_t * p_madw)
{
	OSM_LOG_ENTER(sm->p_log);

	ni_rcv_process_switch(sm, p_node, p_madw);

	/*
	   A node guid of 0 is the corner case that indicates
	   we discovered our own node.  Initialize the subnet
	   object with the SM's own port guid.
	 */
	if (osm_madw_get_ni_context_ptr(p_madw)->node_guid == 0)
		sm->p_subn->sm_port_guid = p_node->node_info.port_guid;

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must NOT be held before calling this function.
**********************************************************************/
static void ni_rcv_process_new(IN osm_sm_t * sm, IN const osm_madw_t * p_madw)
{
	osm_node_t *p_node;
	osm_node_t *p_node_check;
	osm_port_t *p_port;
	osm_port_t *p_port_check;
	osm_router_t *p_rtr = NULL;
	osm_router_t *p_rtr_check;
	cl_qmap_t *p_rtr_guid_tbl;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_ni_context_t *p_ni_context;
	osm_alias_guid_t *p_alias_guid, *p_alias_guid_check;
	uint8_t port_num;

	OSM_LOG_ENTER(sm->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);
	p_ni_context = osm_madw_get_ni_context_ptr(p_madw);
	port_num = ib_node_info_get_local_port_num(p_ni);

	osm_dump_smp_dr_path_v2(sm->p_log, p_smp, FILE_ID, OSM_LOG_VERBOSE);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Discovered new %s node,"
		"\n\t\t\t\tGUID 0x%" PRIx64 ", TID 0x%" PRIx64 "\n",
		ib_get_node_type_str(p_ni->node_type),
		cl_ntoh64(p_ni->node_guid), cl_ntoh64(p_smp->trans_id));

	if (PF(port_num > p_ni->num_ports)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D0A: "
			"New %s node GUID 0x%" PRIx64 "is non-compliant and "
			"is being ignored since the "
			"local port num %u > num ports %u\n",
			ib_get_node_type_str(p_ni->node_type),
			cl_ntoh64(p_ni->node_guid), port_num,
			p_ni->num_ports);
		goto Exit;
	}

	p_node = osm_node_new(p_madw);
	if (PF(p_node == NULL)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D07: "
			"Unable to create new node object\n");
		goto Exit;
	}

	/*
	   Create a new port object to represent this node's physical
	   ports in the port table.
	 */
	p_port = osm_port_new(p_ni, p_node);
	if (PF(p_port == NULL)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D14: "
			"Unable to create new port object\n");
		osm_node_delete(&p_node);
		goto Exit;
	}

	/*
	   Add the new port object to the database.
	 */
	p_port_check =
	    (osm_port_t *) cl_qmap_insert(&sm->p_subn->port_guid_tbl,
					  p_ni->port_guid, &p_port->map_item);
	if (PF(p_port_check != p_port)) {
		/*
		   We should never be here!
		   Somehow, this port GUID already exists in the table.
		 */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D15: "
			"Duplicate Port GUID 0x%" PRIx64
			"! Found by the two directed routes:\n",
			cl_ntoh64(p_ni->port_guid));
		osm_dump_dr_path_v2(sm->p_log,
				    osm_physp_get_dr_path_ptr(p_port->p_physp),
				    FILE_ID, OSM_LOG_ERROR);
		osm_dump_dr_path_v2(sm->p_log,
				    osm_physp_get_dr_path_ptr(p_port_check->
							   p_physp),
				    FILE_ID, OSM_LOG_ERROR);
		osm_port_delete(&p_port);
		osm_node_delete(&p_node);
		goto Exit;
	}

	p_alias_guid = osm_alias_guid_new(p_ni->port_guid,
					  p_port);
	if (PF(!p_alias_guid)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D18: "
			"alias guid memory allocation failed"
			" for port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->port_guid));
		goto alias_done2;
	}

	/* insert into alias guid table */
	p_alias_guid_check =
		(osm_alias_guid_t *) cl_qmap_insert(&sm->p_subn->alias_port_guid_tbl,
						    p_alias_guid->alias_guid,
						    &p_alias_guid->map_item);
	if (p_alias_guid_check != p_alias_guid) {
		/* alias GUID is a duplicate */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D19: "
			"Duplicate alias port GUID 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->port_guid));
		osm_alias_guid_delete(&p_alias_guid);
	}

alias_done2:
	/* If we are a master, then this means the port is new on the subnet.
	   Mark it as new - need to send trap 64 on these ports.
	   The condition that we are master is true, since if we are in discovering
	   state (meaning we woke up from standby or we are just initializing),
	   then these ports may be new to us, but are not new on the subnet.
	   If we are master, then the subnet as we know it is the updated one,
	   and any new ports we encounter should cause trap 64. C14-72.1.1 */
	if (sm->p_subn->sm_state == IB_SMINFO_STATE_MASTER)
		p_port->is_new = 1;

	/* If there were RouterInfo or other router attribute,
	   this would be elsewhere */
	if (p_ni->node_type == IB_NODE_TYPE_ROUTER) {
		if (PF((p_rtr = osm_router_new(p_port)) == NULL))
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D1A: "
				"Unable to create new router object\n");
		else {
			p_rtr_guid_tbl = &sm->p_subn->rtr_guid_tbl;
			p_rtr_check =
			    (osm_router_t *) cl_qmap_insert(p_rtr_guid_tbl,
							    p_ni->port_guid,
							    &p_rtr->map_item);
			if (PF(p_rtr_check != p_rtr))
				OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D1B: "
					"Unable to add port GUID:0x%016" PRIx64
					" to router table\n",
					cl_ntoh64(p_ni->port_guid));
		}
	}

	p_node_check =
	    (osm_node_t *) cl_qmap_insert(&sm->p_subn->node_guid_tbl,
					  p_ni->node_guid, &p_node->map_item);
	if (PF(p_node_check != p_node)) {
		/*
		   This node must have been inserted by another thread.
		   This is unexpected, but is not an error.
		   We can simply clean-up, since the other thread will
		   see this processing through to completion.
		 */
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Discovery race detected at node 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->node_guid));
		osm_node_delete(&p_node);
		p_node = p_node_check;
		ni_rcv_set_links(sm, p_node, port_num, p_ni_context);
		goto Exit;
	} else
		ni_rcv_set_links(sm, p_node, port_num, p_ni_context);

	p_node->discovery_count++;
	ni_rcv_get_node_desc(sm, p_node, p_madw);

	switch (p_ni->node_type) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		ni_rcv_process_new_ca_or_router(sm, p_node, p_madw);
		break;
	case IB_NODE_TYPE_SWITCH:
		ni_rcv_process_new_switch(sm, p_node, p_madw);
		break;
	default:
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D16: "
			"Unknown node type %u with GUID 0x%" PRIx64 "\n",
			p_ni->node_type, cl_ntoh64(p_ni->node_guid));
		break;
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void ni_rcv_process_existing(IN osm_sm_t * sm, IN osm_node_t * p_node,
				    IN const osm_madw_t * p_madw)
{
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_ni_context_t *p_ni_context;
	uint8_t port_num;

	OSM_LOG_ENTER(sm->p_log);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);
	p_ni_context = osm_madw_get_ni_context_ptr(p_madw);
	port_num = ib_node_info_get_local_port_num(p_ni);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Rediscovered %s node 0x%" PRIx64 " TID 0x%" PRIx64
		", discovered %u times already\n",
		ib_get_node_type_str(p_ni->node_type),
		cl_ntoh64(p_ni->node_guid),
		cl_ntoh64(p_smp->trans_id), p_node->discovery_count);

	if (PF(port_num > p_ni->num_ports)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D0C: "
			"Existing %s node GUID 0x%" PRIx64 "is non-compliant "
			"and is being ignored since the "
			"local port num %u > num ports %u\n",
			ib_get_node_type_str(p_ni->node_type),
			cl_ntoh64(p_ni->node_guid), port_num,
			p_ni->num_ports);
		goto Exit;
	}

	/*
	   If we haven't already encountered this existing node
	   on this particular sweep, then process further.
	 */
	p_node->discovery_count++;

	switch (p_ni->node_type) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		ni_rcv_process_existing_ca_or_router(sm, p_node, p_madw);
		break;

	case IB_NODE_TYPE_SWITCH:
		ni_rcv_process_existing_switch(sm, p_node, p_madw);
		break;

	default:
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D09: "
			"Unknown node type %u with GUID 0x%" PRIx64 "\n",
			p_ni->node_type, cl_ntoh64(p_ni->node_guid));
		break;
	}

	if ( p_ni->sys_guid != p_node->node_info.sys_guid) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Updated SysImageGUID: 0x%"
			PRIx64 " for node 0x%" PRIx64 "\n",
			cl_ntoh64(p_ni->sys_guid),
			cl_ntoh64(p_ni->node_guid));
	}
	ni_rcv_set_links(sm, p_node, port_num, p_ni_context);
	p_node->node_info = *p_ni;

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

void osm_ni_rcv_process(IN void *context, IN void *data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = data;
	ib_node_info_t *p_ni;
	ib_smp_t *p_smp;
	osm_node_t *p_node;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_ni = ib_smp_get_payload_ptr(p_smp);

	CL_ASSERT(p_smp->attr_id == IB_MAD_ATTR_NODE_INFO);

	if (PF(p_ni->node_guid == 0)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D16: "
			"Got Zero Node GUID! Found on the directed route:\n");
		osm_dump_smp_dr_path_v2(sm->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		goto Exit;
	}

	if (PF(p_ni->port_guid == 0)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0D17: "
			"Got Zero Port GUID! Found on the directed route:\n");
		osm_dump_smp_dr_path_v2(sm->p_log, p_smp, FILE_ID, OSM_LOG_ERROR);
		goto Exit;
	}

	if (ib_smp_get_status(p_smp)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(ib_smp_get_status(p_smp)));
		goto Exit;
	}

	/*
	   Determine if this node has already been discovered,
	   and process accordingly.
	   During processing of this node, hold the shared lock.
	 */

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
	p_node = osm_get_node_by_guid(sm->p_subn, p_ni->node_guid);

	osm_dump_node_info_v2(sm->p_log, p_ni, FILE_ID, OSM_LOG_DEBUG);

	if (!p_node)
		ni_rcv_process_new(sm, p_madw);
	else
		ni_rcv_process_existing(sm, p_node, p_madw);

	CL_PLOCK_RELEASE(sm->p_lock);

Exit:
	OSM_LOG_EXIT(sm->p_log);
}
