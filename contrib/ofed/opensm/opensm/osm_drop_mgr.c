/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
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
 *    Implementation of osm_drop_mgr_t.
 * This object represents the Drop Manager object.
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
#include <complib/cl_ptr_vector.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_DROP_MGR_C
#include <opensm/osm_sm.h>
#include <opensm/osm_router.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_node.h>
#include <opensm/osm_guid.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_remote_sm.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_ucast_mgr.h>

static void drop_mgr_remove_router(osm_sm_t * sm, IN const ib_net64_t portguid)
{
	osm_router_t *p_rtr;
	cl_qmap_t *p_rtr_guid_tbl;

	p_rtr_guid_tbl = &sm->p_subn->rtr_guid_tbl;
	p_rtr = (osm_router_t *) cl_qmap_remove(p_rtr_guid_tbl, portguid);
	if (p_rtr != (osm_router_t *) cl_qmap_end(p_rtr_guid_tbl)) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Cleaned router for port guid 0x%016" PRIx64 "\n",
			cl_ntoh64(portguid));
		osm_router_delete(&p_rtr);
	}
}

static void drop_mgr_clean_physp(osm_sm_t * sm, IN osm_physp_t * p_physp)
{
	osm_physp_t *p_remote_physp;
	osm_port_t *p_remote_port;

	p_remote_physp = osm_physp_get_remote(p_physp);
	if (p_remote_physp) {
		p_remote_port = osm_get_port_by_guid(sm->p_subn,
						     p_remote_physp->port_guid);

		if (p_remote_port) {
			/* Let's check if this is a case of link that is lost
			   (both ports weren't recognized), or a "hiccup" in the
			   subnet - in which case the remote port was
			   recognized, and its state is ACTIVE.
			   If this is just a "hiccup" - force a heavy sweep in
			   the next sweep. We don't want to lose that part of
			   the subnet. */
			if (p_remote_port->discovery_count &&
			    osm_physp_get_port_state(p_remote_physp) ==
			    IB_LINK_ACTIVE) {
				OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
					"Forcing new heavy sweep. Remote "
					"port 0x%016" PRIx64 " port num: %u "
					"was recognized in ACTIVE state\n",
					cl_ntoh64(p_remote_physp->port_guid),
					p_remote_physp->port_num);
				sm->p_subn->force_heavy_sweep = TRUE;
			}

			/* If the remote node is ca or router - need to remove
			   the remote port, since it is no longer reachable.
			   This can be done if we reset the discovery count
			   of the remote port. */
			if (!p_remote_physp->p_node->sw &&
                            p_remote_physp->port_guid != sm->p_subn->sm_port_guid) {
				p_remote_port->discovery_count = 0;
				OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
					"Resetting discovery count of node: "
					"0x%016" PRIx64 " port num:%u\n",
					cl_ntoh64(osm_node_get_node_guid
						  (p_remote_physp->p_node)),
					p_remote_physp->port_num);
			}
		}

		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Unlinking local node 0x%016" PRIx64 ", port %u"
			"\n\t\t\t\tand remote node 0x%016" PRIx64
			", port %u\n",
			cl_ntoh64(osm_node_get_node_guid(p_physp->p_node)),
			p_physp->port_num,
			cl_ntoh64(osm_node_get_node_guid
				  (p_remote_physp->p_node)),
			p_remote_physp->port_num);

		if (sm->ucast_mgr.cache_valid)
			osm_ucast_cache_add_link(&sm->ucast_mgr, p_physp,
						 p_remote_physp);

		osm_physp_unlink(p_physp, p_remote_physp);

	}

	/* Make port as undiscovered */
	p_physp->p_node->physp_discovered[p_physp->port_num] = 0;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Clearing node 0x%016" PRIx64 " physical port number %u\n",
		cl_ntoh64(osm_node_get_node_guid(p_physp->p_node)),
		p_physp->port_num);

	osm_physp_destroy(p_physp);
}

static void drop_mgr_remove_port(osm_sm_t * sm, IN osm_port_t * p_port)
{
	ib_net64_t port_guid;
	osm_port_t *p_port_check;
	cl_qmap_t *p_alias_guid_tbl;
	cl_qmap_t *p_sm_guid_tbl;
	osm_mcm_port_t *mcm_port;
	cl_ptr_vector_t *p_port_lid_tbl;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	uint16_t lid_ho;
	osm_node_t *p_node;
	osm_remote_sm_t *p_sm;
	osm_alias_guid_t *p_alias_guid, *p_alias_guid_check;
	osm_guidinfo_work_obj_t *wobj;
	cl_list_item_t *item, *next_item;
	ib_gid_t port_gid;
	ib_mad_notice_attr_t notice;
	ib_api_status_t status;

	OSM_LOG_ENTER(sm->p_log);

	port_guid = osm_port_get_guid(p_port);
	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Unreachable port 0x%016" PRIx64 "\n", cl_ntoh64(port_guid));

	p_port_check =
	    (osm_port_t *) cl_qmap_get(&sm->p_subn->port_guid_tbl, port_guid);
	if (p_port_check != p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0101: "
			"Port 0x%016" PRIx64 " not in guid table\n",
			cl_ntoh64(port_guid));
		goto Exit;
	}

	/* issue a notice - trap 65 (SM_GID_OUT_OF_SERVICE_TRAP) */
	/* details of the notice */
	notice.generic_type = 0x80 | IB_NOTICE_TYPE_SUBN_MGMT;	/* is generic subn mgt type */
	ib_notice_set_prod_type_ho(&notice, 4);	/* A class manager generator */
	/* endport ceases to be reachable */
	notice.g_or_v.generic.trap_num = CL_HTON16(SM_GID_OUT_OF_SERVICE_TRAP); /* 65 */
	/* The sm_base_lid is saved in network order already. */
	notice.issuer_lid = sm->p_subn->sm_base_lid;
	/* following C14-72.1.2 and table 119 p725 */
	/* we need to provide the GID */
	port_gid.unicast.prefix = sm->p_subn->opt.subnet_prefix;
	port_gid.unicast.interface_id = port_guid;
	memcpy(&(notice.data_details.ntc_64_67.gid),
	       &(port_gid), sizeof(ib_gid_t));

	/* According to page 653 - the issuer gid in this case of trap
	   is the SM gid, since the SM is the initiator of this trap. */
	notice.issuer_gid.unicast.prefix = sm->p_subn->opt.subnet_prefix;
	notice.issuer_gid.unicast.interface_id = sm->p_subn->sm_port_guid;

	status = osm_report_notice(sm->p_log, sm->p_subn, &notice);
	if (status != IB_SUCCESS) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0103: "
			"Error sending trap reports (%s)\n",
			ib_get_err_str(status));
	}

	next_item = cl_qlist_head(&sm->p_subn->alias_guid_list);
	while (next_item != cl_qlist_end(&sm->p_subn->alias_guid_list)) {
		item = next_item;
		next_item = cl_qlist_next(item);
		wobj = cl_item_obj(item, wobj, list_item);
		if (wobj->p_port == p_port) {
			cl_qlist_remove_item(&sm->p_subn->alias_guid_list,
					     &wobj->list_item);
			osm_guid_work_obj_delete(wobj);
		}
	}

	while (!cl_is_qlist_empty(&p_port->mcm_list)) {
		mcm_port = cl_item_obj(cl_qlist_head(&p_port->mcm_list),
				       mcm_port, list_item);
		osm_mgrp_delete_port(sm->p_subn, sm->p_log, mcm_port->mgrp,
				     p_port);
	}

	p_alias_guid_tbl = &sm->p_subn->alias_port_guid_tbl;
	p_alias_guid_check = (osm_alias_guid_t *) cl_qmap_head(p_alias_guid_tbl);
	while (p_alias_guid_check != (osm_alias_guid_t *) cl_qmap_end(p_alias_guid_tbl)) {
		if (p_alias_guid_check->p_base_port == p_port)
			p_alias_guid = p_alias_guid_check;
		else
			p_alias_guid = NULL;
		p_alias_guid_check = (osm_alias_guid_t *) cl_qmap_next(&p_alias_guid_check->map_item);
		if (p_alias_guid) {
			cl_qmap_remove_item(p_alias_guid_tbl,
					    &p_alias_guid->map_item);
			osm_alias_guid_delete(&p_alias_guid);
		}
	}

	cl_qmap_remove(&sm->p_subn->port_guid_tbl, port_guid);

	p_sm_guid_tbl = &sm->p_subn->sm_guid_tbl;
	p_sm = (osm_remote_sm_t *) cl_qmap_remove(p_sm_guid_tbl, port_guid);
	if (p_sm != (osm_remote_sm_t *) cl_qmap_end(p_sm_guid_tbl)) {
		/* need to remove this item */
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Cleaned SM for port guid 0x%016" PRIx64 "\n",
			cl_ntoh64(port_guid));
		free(p_sm);
	}

	drop_mgr_remove_router(sm, port_guid);

	osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Clearing abandoned LID range [%u,%u]\n",
		min_lid_ho, max_lid_ho);

	p_port_lid_tbl = &sm->p_subn->port_lid_tbl;
	for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++)
		cl_ptr_vector_set(p_port_lid_tbl, lid_ho, NULL);

	drop_mgr_clean_physp(sm, p_port->p_physp);

	/* Delete event forwarding subscriptions */
	if (sm->p_subn->opt.drop_event_subscriptions) {
		if (osm_infr_remove_subscriptions(sm->p_subn, sm->p_log, port_guid)
		    == CL_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			    "Removed event subscriptions for port 0x%016" PRIx64 "\n",
			    cl_ntoh64(port_guid));
	}

	/* initialize the p_node - may need to get node_desc later */
	p_node = p_port->p_node;

	osm_port_delete(&p_port);

	OSM_LOG(sm->p_log, OSM_LOG_INFO,
		"Removed port with GUID:0x%016" PRIx64
		" LID range [%u, %u] of node:%s\n",
		cl_ntoh64(port_gid.unicast.interface_id),
		min_lid_ho, max_lid_ho,
		p_node ? p_node->print_desc : "UNKNOWN");

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void drop_mgr_remove_switch(osm_sm_t * sm, IN osm_node_t * p_node)
{
	osm_switch_t *p_sw;
	cl_qmap_t *p_sw_guid_tbl;
	ib_net64_t node_guid;

	OSM_LOG_ENTER(sm->p_log);

	node_guid = osm_node_get_node_guid(p_node);
	p_sw_guid_tbl = &sm->p_subn->sw_guid_tbl;

	p_sw = (osm_switch_t *) cl_qmap_remove(p_sw_guid_tbl, node_guid);
	if (p_sw == (osm_switch_t *) cl_qmap_end(p_sw_guid_tbl)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0102: "
			"Node 0x%016" PRIx64 " not in switch table\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
	} else {
		p_node->sw = NULL;
		osm_switch_delete(&p_sw);
	}

	OSM_LOG_EXIT(sm->p_log);
}

static boolean_t drop_mgr_process_node(osm_sm_t * sm, IN osm_node_t * p_node)
{
	osm_physp_t *p_physp;
	osm_port_t *p_port;
	osm_node_t *p_node_check;
	uint32_t port_num;
	uint32_t max_ports;
	ib_net64_t port_guid;
	boolean_t return_val = FALSE;

	OSM_LOG_ENTER(sm->p_log);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Unreachable node 0x%016" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	if (sm->ucast_mgr.cache_valid)
		osm_ucast_cache_add_node(&sm->ucast_mgr, p_node);

	/*
	   Delete all the logical and physical port objects
	   associated with this node.
	 */
	max_ports = osm_node_get_num_physp(p_node);
	for (port_num = 0; port_num < max_ports; port_num++) {
		p_physp = osm_node_get_physp_ptr(p_node, port_num);
		if (p_physp) {
			port_guid = osm_physp_get_port_guid(p_physp);

			p_port = osm_get_port_by_guid(sm->p_subn, port_guid);

			if (p_port)
				drop_mgr_remove_port(sm, p_port);
			else
				drop_mgr_clean_physp(sm, p_physp);
		}
	}

	return_val = TRUE;

	if (p_node->sw)
		drop_mgr_remove_switch(sm, p_node);

	p_node_check =
	    (osm_node_t *) cl_qmap_remove(&sm->p_subn->node_guid_tbl,
					  osm_node_get_node_guid(p_node));
	if (p_node_check != p_node) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0105: "
			"Node 0x%016" PRIx64 " not in guid table\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
	}

	/* free memory allocated to node */
	osm_node_delete(&p_node);

	OSM_LOG_EXIT(sm->p_log);
	return return_val;
}

static void drop_mgr_check_switch_node(osm_sm_t * sm, IN osm_node_t * p_node)
{
	ib_net64_t node_guid;
	osm_physp_t *p_physp, *p_remote_physp;
	osm_node_t *p_remote_node;
	osm_port_t *p_port;
	ib_net64_t port_guid;
	uint8_t port_num, remote_port_num;

	OSM_LOG_ENTER(sm->p_log);

	node_guid = osm_node_get_node_guid(p_node);

	/* Make sure we have a switch object for this node */
	if (!p_node->sw) {
		/* We do not have switch info for this node */
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Node 0x%016" PRIx64 " no switch in table\n",
			cl_ntoh64(node_guid));

		drop_mgr_process_node(sm, p_node);
		goto Exit;
	}

	/* Make sure we have a port object for port zero */
	p_physp = osm_node_get_physp_ptr(p_node, 0);
	if (!p_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Node 0x%016" PRIx64 " no valid physical port 0\n",
			cl_ntoh64(node_guid));

		drop_mgr_process_node(sm, p_node);
		goto Exit;
	}

	port_guid = osm_physp_get_port_guid(p_physp);

	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);

	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Node 0x%016" PRIx64 " has no port object\n",
			cl_ntoh64(node_guid));

		drop_mgr_process_node(sm, p_node);
		goto Exit;
	}

	if (!p_node->physp_discovered[0]) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Node 0x%016" PRIx64 " port has discovery count zero\n",
			cl_ntoh64(node_guid));

		drop_mgr_process_node(sm, p_node);
		goto Exit;
	}

	/*
	 * Unlink all ports that havn't been discovered during the last sweep.
	 * Optimization: Skip the check if discovered all the ports of the switch.
	 */
	if (p_port->discovery_count < p_node->physp_tbl_size) {
		for (port_num = 1; port_num < p_node->physp_tbl_size; port_num++) {
			if (!p_node->physp_discovered[port_num]) {
				p_physp = osm_node_get_physp_ptr(p_node, port_num);
				if (!p_physp)
					continue;
				p_remote_physp = osm_physp_get_remote(p_physp);
				if (!p_remote_physp)
					continue;

				p_remote_node =
				    osm_physp_get_node_ptr(p_remote_physp);
				remote_port_num =
				    osm_physp_get_port_num(p_remote_physp);

				OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
					"Unlinking local node 0x%" PRIx64
					", port %u"
					"\n\t\t\t\tand remote node 0x%" PRIx64
					", port %u due to missing PortInfo\n",
					cl_ntoh64(osm_node_get_node_guid
						  (p_node)), port_num,
					cl_ntoh64(osm_node_get_node_guid
						  (p_remote_node)),
					remote_port_num);

				if (sm->ucast_mgr.cache_valid)
					osm_ucast_cache_add_link(&sm->ucast_mgr,
								 p_physp,
								 p_remote_physp);

				osm_node_unlink(p_node, (uint8_t) port_num,
						p_remote_node,
						(uint8_t) remote_port_num);
			}
		}
	}
Exit:
	OSM_LOG_EXIT(sm->p_log);
	return;
}

void osm_drop_mgr_process(osm_sm_t * sm)
{
	cl_qmap_t *p_node_guid_tbl, *p_port_guid_tbl;
	osm_port_t *p_port, *p_next_port;
	osm_node_t *p_node, *p_next_node;
	int max_ports, port_num;
	osm_physp_t *p_physp;
	ib_net64_t port_guid;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	p_node_guid_tbl = &sm->p_subn->node_guid_tbl;
	p_port_guid_tbl = &sm->p_subn->port_guid_tbl;

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);

	p_next_node = (osm_node_t *) cl_qmap_head(p_node_guid_tbl);
	while (p_next_node != (osm_node_t *) cl_qmap_end(p_node_guid_tbl)) {
		p_node = p_next_node;
		p_next_node =
		    (osm_node_t *) cl_qmap_next(&p_next_node->map_item);

		CL_ASSERT(cl_qmap_key(&p_node->map_item) ==
			  osm_node_get_node_guid(p_node));

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Checking node 0x%016" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));

		/*
		   Check if this node was discovered during the last sweep.
		   If not, it is unreachable in the current subnet, and
		   should therefore be removed from the subnet object.
		 */
		if (p_node->discovery_count == 0)
			drop_mgr_process_node(sm, p_node);
		else {
			/*
			 * We want to preserve the configured pkey indexes,
			 * so if we don't receive GetResp P_KeyTable for some block,
			 * do the following:
			 *   1. Drop node if the node is sw and got timeout for port 0.
			 *   2. Drop node if node is HCA/RTR.
			 *   3. Drop only physp if got timeout for sw when the port isn't 0.
			 * We'll set error during initialization in order to
			 * cause an immediate heavy sweep and try to get the
			 * configured P_KeyTable again.
			 */
			if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH)
				port_num = 0;
			else
				port_num = 1;
			max_ports = osm_node_get_num_physp(p_node);
			for (; port_num < max_ports; port_num++) {
				p_physp = osm_node_get_physp_ptr(p_node, port_num);
				if (!p_physp || p_physp->pkeys.rcv_blocks_cnt == 0)
					continue;
				p_physp->pkeys.rcv_blocks_cnt = 0;
				p_physp->need_update = 2;
				sm->p_subn->subnet_initialization_error = TRUE;
				port_guid = osm_physp_get_port_guid(p_physp);
				p_port = osm_get_port_by_guid(sm->p_subn, port_guid);
				CL_ASSERT(p_port);
				if (p_node->physp_discovered[port_num]) {
					p_node->physp_discovered[port_num] = 0;
					p_port->discovery_count--;
				}
			}
		}
	}

	/*
	   Go over all the nodes. If the node is a switch - make sure
	   there is also a switch record for it, and a portInfo record for
	   port zero of of the node.
	   If not - this means that there was some error in getting the data
	   of this node. Drop the node.
	 */
	p_next_node = (osm_node_t *) cl_qmap_head(p_node_guid_tbl);
	while (p_next_node != (osm_node_t *) cl_qmap_end(p_node_guid_tbl)) {
		p_node = p_next_node;
		p_next_node =
		    (osm_node_t *) cl_qmap_next(&p_next_node->map_item);

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Checking full discovery of node 0x%016" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));

		if (osm_node_get_type(p_node) != IB_NODE_TYPE_SWITCH)
			continue;

		/* We are handling a switch node */
		drop_mgr_check_switch_node(sm, p_node);
	}

	p_next_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	while (p_next_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port =
		    (osm_port_t *) cl_qmap_next(&p_next_port->map_item);

		CL_ASSERT(cl_qmap_key(&p_port->map_item) ==
			  osm_port_get_guid(p_port));

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Checking port 0x%016" PRIx64 "\n",
			cl_ntoh64(osm_port_get_guid(p_port)));

		/*
		   If the port is unreachable, remove it from the guid table.
		 */
		if (p_port->discovery_count == 0)
			drop_mgr_remove_port(sm, p_port);
	}

	CL_PLOCK_RELEASE(sm->p_lock);
	OSM_LOG_EXIT(sm->p_log);
}
