/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2011 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2008 Xsigo Systems Inc.  All rights reserved.
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2010 HNR Consulting. All rights reserved.
 * Copyright (C) 2012-2013 Tokyo Institute of Technology. All rights reserved.
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
 *    Implementation of osm_mcast_mgr_t.
 * This file implements the Multicast Manager object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_MCAST_MGR_C
#include <opensm/osm_opensm.h>
#include <opensm/osm_sm.h>
#include <opensm/osm_multicast.h>
#include <opensm/osm_node.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_mcast_mgr.h>

static osm_mcast_work_obj_t *mcast_work_obj_new(IN osm_port_t * p_port)
{
	osm_mcast_work_obj_t *p_obj;

	/*
	   clean allocated memory to avoid assertion when trying to insert to
	   qlist.
	   see cl_qlist_insert_tail(): CL_ASSERT(p_list_item->p_list != p_list)
	 */
	p_obj = malloc(sizeof(*p_obj));
	if (p_obj) {
		memset(p_obj, 0, sizeof(*p_obj));
		p_obj->p_port = p_port;
	}

	return p_obj;
}

static void mcast_work_obj_delete(IN osm_mcast_work_obj_t * p_wobj)
{
	free(p_wobj);
}

int osm_mcast_make_port_list_and_map(cl_qlist_t * list, cl_qmap_t * map,
				     osm_mgrp_box_t * mbox)
{
	cl_map_item_t *map_item;
	cl_list_item_t *list_item;
	osm_mgrp_t *mgrp;
	osm_mcm_port_t *mcm_port;
	osm_mcast_work_obj_t *wobj;

	cl_qmap_init(map);
	cl_qlist_init(list);

	for (list_item = cl_qlist_head(&mbox->mgrp_list);
	     list_item != cl_qlist_end(&mbox->mgrp_list);
	     list_item = cl_qlist_next(list_item)) {
		mgrp = cl_item_obj(list_item, mgrp, list_item);
		for (map_item = cl_qmap_head(&mgrp->mcm_port_tbl);
		     map_item != cl_qmap_end(&mgrp->mcm_port_tbl);
		     map_item = cl_qmap_next(map_item)) {
			/* Acquire the port object for this port guid, then
			   create the new worker object to build the list. */
			mcm_port = cl_item_obj(map_item, mcm_port, map_item);
			if (cl_qmap_get(map, mcm_port->port->guid) !=
			    cl_qmap_end(map))
				continue;
			wobj = mcast_work_obj_new(mcm_port->port);
			if (!wobj)
				return -1;
			cl_qlist_insert_tail(list, &wobj->list_item);
			cl_qmap_insert(map, mcm_port->port->guid,
				       &wobj->map_item);
		}
	}
	return 0;
}

void osm_mcast_drop_port_list(cl_qlist_t * list)
{
	while (cl_qlist_count(list))
		mcast_work_obj_delete((osm_mcast_work_obj_t *)
				      cl_qlist_remove_head(list));
}

void osm_purge_mtree(osm_sm_t * sm, IN osm_mgrp_box_t * mbox)
{
	OSM_LOG_ENTER(sm->p_log);

	if (mbox->root)
		osm_mtree_destroy(mbox->root);
	mbox->root = NULL;

	OSM_LOG_EXIT(sm->p_log);
}

static void create_mgrp_switch_map(cl_qmap_t * m, cl_qlist_t * port_list)
{
	osm_mcast_work_obj_t *wobj;
	osm_port_t *port;
	osm_switch_t *sw;
	ib_net64_t guid;
	cl_list_item_t *i;

	cl_qmap_init(m);
	for (i = cl_qlist_head(port_list); i != cl_qlist_end(port_list);
	     i = cl_qlist_next(i)) {
		wobj = cl_item_obj(i, wobj, list_item);
		port = wobj->p_port;
		if (port->p_node->sw) {
			sw = port->p_node->sw;
			sw->is_mc_member = 1;
		} else if (port->p_physp->p_remote_physp) {
			sw = port->p_physp->p_remote_physp->p_node->sw;
			sw->num_of_mcm++;
		} else
			continue;
		guid = osm_node_get_node_guid(sw->p_node);
		if (cl_qmap_get(m, guid) == cl_qmap_end(m))
			cl_qmap_insert(m, guid, &sw->mgrp_item);
	}
}

static void destroy_mgrp_switch_map(cl_qmap_t * m)
{
	osm_switch_t *sw;
	cl_map_item_t *i;

	for (i = cl_qmap_head(m); i != cl_qmap_end(m); i = cl_qmap_next(i)) {
		sw = cl_item_obj(i, sw, mgrp_item);
		sw->num_of_mcm = 0;
		sw->is_mc_member = 0;
	}
	cl_qmap_remove_all(m);
}

/**********************************************************************
 Calculate the maximal "min hops" from the given switch to any
 of the group HCAs
 **********************************************************************/
#ifdef OSM_VENDOR_INTF_ANAFA
static float mcast_mgr_compute_avg_hops(osm_sm_t * sm, cl_qmap_t * m,
					const osm_switch_t * this_sw)
{
	float avg_hops = 0;
	uint32_t hops = 0;
	uint32_t num_ports = 0;
	uint16_t lid;
	uint32_t least_hops;
	cl_map_item_t *i;
	osm_switch_t *sw;

	OSM_LOG_ENTER(sm->p_log);

	for (i = cl_qmap_head(m); i != cl_qmap_end(m); i = cl_qmap_next(i)) {
		sw = cl_item_obj(i, sw, mcast_item);
		lid = cl_ntoh16(osm_node_get_base_lid(sw->p_node, 0));
		least_hops = osm_switch_get_least_hops(this_sw, lid);
		/* for all host that are MC members and attached to the switch,
		   we should add the (least_hops + 1) * number_of_such_hosts.
		   If switch itself is in the MC, we should add the least_hops only */
		hops += (least_hops + 1) * sw->num_of_mcm +
		    least_hops * sw->is_mc_member;
		num_ports += sw->num_of_mcm + sw->is_mc_member;
	}

	/* We shouldn't be here if there aren't any ports in the group. */
	CL_ASSERT(num_ports);

	avg_hops = (float)(hops / num_ports);

	OSM_LOG_EXIT(sm->p_log);
	return avg_hops;
}
#else
static float mcast_mgr_compute_max_hops(osm_sm_t * sm, cl_qmap_t * m,
					const osm_switch_t * this_sw)
{
	uint32_t max_hops = 0, hops;
	uint16_t lid;
	cl_map_item_t *i;
	osm_switch_t *sw;

	OSM_LOG_ENTER(sm->p_log);

	/*
	   For each member of the multicast group, compute the
	   number of hops to its base LID.
	 */
	for (i = cl_qmap_head(m); i != cl_qmap_end(m); i = cl_qmap_next(i)) {
		sw = cl_item_obj(i, sw, mgrp_item);
		lid = cl_ntoh16(osm_node_get_base_lid(sw->p_node, 0));
		hops = osm_switch_get_least_hops(this_sw, lid);
		if (!sw->is_mc_member)
			hops += 1;
		if (hops > max_hops)
			max_hops = hops;
	}

	/* Note that at this point we might get (max_hops == 0),
	   which means that there's only one member in the mcast
	   group, and it's the current switch */

	OSM_LOG_EXIT(sm->p_log);
	return (float)max_hops;
}
#endif

/**********************************************************************
   This function attempts to locate the optimal switch for the
   center of the spanning tree.  The current algorithm chooses
   a switch with the lowest average hop count to the members
   of the multicast group.
**********************************************************************/
static osm_switch_t *mcast_mgr_find_optimal_switch(osm_sm_t * sm,
						   cl_qlist_t * list)
{
	cl_qmap_t mgrp_sw_map;
	cl_qmap_t *p_sw_tbl;
	osm_switch_t *p_sw, *p_best_sw = NULL;
	float hops = 0;
	float best_hops = 10000;	/* any big # will do */

	OSM_LOG_ENTER(sm->p_log);

	p_sw_tbl = &sm->p_subn->sw_guid_tbl;

	create_mgrp_switch_map(&mgrp_sw_map, list);
	for (p_sw = (osm_switch_t *) cl_qmap_head(p_sw_tbl);
	     p_sw != (osm_switch_t *) cl_qmap_end(p_sw_tbl);
	     p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item)) {
		if (!osm_switch_supports_mcast(p_sw))
			continue;

#ifdef OSM_VENDOR_INTF_ANAFA
		hops = mcast_mgr_compute_avg_hops(sm, &mgrp_sw_map, p_sw);
#else
		hops = mcast_mgr_compute_max_hops(sm, &mgrp_sw_map, p_sw);
#endif

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Switch 0x%016" PRIx64 ", hops = %f\n",
			cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)), hops);

		if (hops < best_hops) {
			p_best_sw = p_sw;
			best_hops = hops;
		}
	}

	if (p_best_sw)
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Best switch is 0x%" PRIx64 " (%s), hops = %f\n",
			cl_ntoh64(osm_node_get_node_guid(p_best_sw->p_node)),
			p_best_sw->p_node->print_desc, best_hops);
	else
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"No multicast capable switches detected\n");

	destroy_mgrp_switch_map(&mgrp_sw_map);
	OSM_LOG_EXIT(sm->p_log);
	return p_best_sw;
}

/**********************************************************************
   This function returns the existing or optimal root switch for the tree.
**********************************************************************/
osm_switch_t *osm_mcast_mgr_find_root_switch(osm_sm_t * sm, cl_qlist_t *list)
{
	osm_switch_t *p_sw = NULL;

	OSM_LOG_ENTER(sm->p_log);

	/*
	   We always look for the best multicast tree root switch.
	   Otherwise since we always start with a a single join
	   the root will be always on the first switch attached to it.
	   - Very bad ...
	 */
	p_sw = mcast_mgr_find_optimal_switch(sm, list);

	OSM_LOG_EXIT(sm->p_log);
	return p_sw;
}

static int mcast_mgr_set_mft_block(osm_sm_t * sm, IN osm_switch_t * p_sw,
				   uint32_t block_num, uint32_t position)
{
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	osm_dr_path_t *p_path;
	osm_madw_context_t context;
	ib_api_status_t status;
	uint32_t block_id_ho;
	osm_mcast_tbl_t *p_tbl;
	ib_net16_t block[IB_MCAST_BLOCK_SIZE];
	int ret = 0;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_path = osm_physp_get_dr_path_ptr(p_physp);

	/*
	   Send multicast forwarding table blocks to the switch
	   as long as the switch indicates it has blocks needing
	   configuration.
	 */

	context.mft_context.node_guid = osm_node_get_node_guid(p_node);
	context.mft_context.set_method = TRUE;

	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);

	if (osm_mcast_tbl_get_block(p_tbl, (uint16_t) block_num,
				    (uint8_t) position, block)) {
		block_id_ho = block_num + (position << 28);

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Writing MFT block %u position %u to switch 0x%" PRIx64
			"\n", block_num, position,
			cl_ntoh64(context.mft_context.node_guid));

		status = osm_req_set(sm, p_path, (void *)block, sizeof(block),
				     IB_MAD_ATTR_MCAST_FWD_TBL,
				     cl_hton32(block_id_ho), FALSE,
				     ib_port_info_get_m_key(&p_physp->port_info),
				     CL_DISP_MSGID_NONE, &context);
		if (status != IB_SUCCESS) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A02: "
				"Sending multicast fwd. tbl. block 0x%X to %s "
				"failed (%s)\n", block_id_ho,
				p_node->print_desc, ib_get_err_str(status));
			ret = -1;
		}
	}

	OSM_LOG_EXIT(sm->p_log);
	return ret;
}

/**********************************************************************
  This is part of the recursive function to compute the paths in the
  spanning tree that emanate from this switch.  On input, the p_list
  contains the group members that must be routed from this switch.
**********************************************************************/
static void mcast_mgr_subdivide(osm_sm_t * sm, uint16_t mlid_ho,
				osm_switch_t * p_sw, cl_qlist_t * p_list,
				cl_qlist_t * list_array, uint8_t array_size)
{
	uint8_t port_num;
	boolean_t ignore_existing;
	osm_mcast_work_obj_t *p_wobj;

	OSM_LOG_ENTER(sm->p_log);

	/*
	   For Multicast Groups, we don't want to count on previous
	   configurations - since we can easily generate a storm
	   by loops.
	 */
	ignore_existing = TRUE;

	/*
	   Subdivide the set of ports into non-overlapping subsets
	   that will be routed to other switches.
	 */
	while ((p_wobj =
		(osm_mcast_work_obj_t *) cl_qlist_remove_head(p_list)) !=
	       (osm_mcast_work_obj_t *) cl_qlist_end(p_list)) {
		port_num =
		    osm_switch_recommend_mcast_path(p_sw, p_wobj->p_port,
						    mlid_ho, ignore_existing);
		if (port_num == OSM_NO_PATH) {
			/*
			   This typically occurs if the switch does not support
			   multicast and the multicast tree must branch at this
			   switch.
			 */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A03: "
				"Error routing MLID 0x%X through switch 0x%"
				PRIx64 " %s\n"
				"\t\t\t\tNo multicast paths from this switch "
				"for port with LID %u\n", mlid_ho,
				cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)),
				p_sw->p_node->print_desc,
				cl_ntoh16(osm_port_get_base_lid
					  (p_wobj->p_port)));
			mcast_work_obj_delete(p_wobj);
			continue;
		}

		if (port_num >= array_size) {
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A04: "
				"Error routing MLID 0x%X through switch 0x%"
				PRIx64 " %s\n"
				"\t\t\t\tNo multicast paths from this switch "
				"to port with LID %u\n", mlid_ho,
				cl_ntoh64(osm_node_get_node_guid(p_sw->p_node)),
				p_sw->p_node->print_desc,
				cl_ntoh16(osm_port_get_base_lid
					  (p_wobj->p_port)));
			mcast_work_obj_delete(p_wobj);
			continue;
		}

		cl_qlist_insert_tail(&list_array[port_num], &p_wobj->list_item);
	}

	OSM_LOG_EXIT(sm->p_log);
}

static void mcast_mgr_purge_list(osm_sm_t * sm, uint16_t mlid, cl_qlist_t * list)
{
	if (OSM_LOG_IS_ACTIVE_V2(sm->p_log, OSM_LOG_ERROR)) {
		osm_mcast_work_obj_t *wobj;
		cl_list_item_t *i;
		for (i = cl_qlist_head(list); i != cl_qlist_end(list);
		     i = cl_qlist_next(i)) {
			wobj = cl_item_obj(i, wobj, list_item);
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A06: "
				"Unable to route MLID 0x%X for port 0x%" PRIx64 "\n",
				mlid, cl_ntoh64(osm_port_get_guid(wobj->p_port)));
		}
	}
	osm_mcast_drop_port_list(list);
}

/**********************************************************************
  This is the recursive function to compute the paths in the spanning
  tree that emanate from this switch.  On input, the p_list contains
  the group members that must be routed from this switch.

  The function returns the newly created mtree node element.
**********************************************************************/
static osm_mtree_node_t *mcast_mgr_branch(osm_sm_t * sm, uint16_t mlid_ho,
					  osm_switch_t * p_sw,
					  cl_qlist_t * p_list, uint8_t depth,
					  uint8_t upstream_port,
					  uint8_t * p_max_depth)
{
	uint8_t max_children;
	osm_mtree_node_t *p_mtn = NULL;
	cl_qlist_t *list_array = NULL;
	uint8_t i;
	ib_net64_t node_guid;
	osm_mcast_work_obj_t *p_wobj;
	cl_qlist_t *p_port_list;
	size_t count;
	osm_mcast_tbl_t *p_tbl;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);
	CL_ASSERT(p_list);
	CL_ASSERT(p_max_depth);

	node_guid = osm_node_get_node_guid(p_sw->p_node);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Routing MLID 0x%X through switch 0x%" PRIx64
		" %s, %u nodes at depth %u\n",
		mlid_ho, cl_ntoh64(node_guid), p_sw->p_node->print_desc,
		cl_qlist_count(p_list), depth);

	CL_ASSERT(cl_qlist_count(p_list) > 0);

	depth++;

	if (depth >= 64) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A21: "
			"Maximal hops number is reached for MLID 0x%x."
			" Break processing\n", mlid_ho);
		mcast_mgr_purge_list(sm, mlid_ho, p_list);
		goto Exit;
	}

	if (depth > *p_max_depth) {
		CL_ASSERT(depth == *p_max_depth + 1);
		*p_max_depth = depth;
	}

	if (osm_switch_supports_mcast(p_sw) == FALSE) {
		/*
		   This switch doesn't do multicast.  Clean-up.
		 */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A14: "
			"Switch 0x%" PRIx64 " %s does not support multicast\n",
			cl_ntoh64(node_guid), p_sw->p_node->print_desc);

		/*
		   Deallocate all the work objects on this branch of the tree.
		 */
		mcast_mgr_purge_list(sm, mlid_ho, p_list);
		goto Exit;
	}

	p_mtn = osm_mtree_node_new(p_sw);
	if (p_mtn == NULL) {
		/*
		   We are unable to continue routing down this
		   leg of the tree.  Clean-up.
		 */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A15: "
			"Insufficient memory to build multicast tree\n");

		/*
		   Deallocate all the work objects on this branch of the tree.
		 */
		mcast_mgr_purge_list(sm, mlid_ho, p_list);
		goto Exit;
	}

	max_children = osm_mtree_node_get_max_children(p_mtn);

	CL_ASSERT(max_children > 1);

	/*
	   Prepare an empty list for each port in the switch.
	   TO DO - this list array could probably be moved
	   inside the switch element to save on malloc thrashing.
	 */
	list_array = malloc(sizeof(cl_qlist_t) * max_children);
	if (list_array == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A16: "
			"Unable to allocate list array\n");
		mcast_mgr_purge_list(sm, mlid_ho, p_list);
		osm_mtree_destroy(p_mtn);
		p_mtn = NULL;
		goto Exit;
	}

	memset(list_array, 0, sizeof(cl_qlist_t) * max_children);

	for (i = 0; i < max_children; i++)
		cl_qlist_init(&list_array[i]);

	mcast_mgr_subdivide(sm, mlid_ho, p_sw, p_list, list_array, max_children);

	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);

	/*
	   Add the upstream port to the forwarding table unless
	   we're at the root of the spanning tree.
	 */
	if (depth > 1) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Adding upstream port %u\n", upstream_port);

		CL_ASSERT(upstream_port);
		osm_mcast_tbl_set(p_tbl, mlid_ho, upstream_port);
	}

	/*
	   For each port that was allocated some routes,
	   recurse into this function to continue building the tree
	   if the node on the other end of that port is another switch.
	   Otherwise, the node is an endpoint, and we've found a leaf
	   of the tree.  Mark leaves with our special pointer value.
	 */

	for (i = 0; i < max_children; i++) {
		const osm_physp_t *p_physp;
		const osm_physp_t *p_remote_physp;
		osm_node_t *p_node;
		const osm_node_t *p_remote_node;

		p_port_list = &list_array[i];

		count = cl_qlist_count(p_port_list);

		/*
		   There should be no children routed through the upstream port!
		 */
		CL_ASSERT(upstream_port == 0 || i != upstream_port ||
			  (i == upstream_port && count == 0));

		if (count == 0)
			continue;	/* No routes down this port. */

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Routing %zu destinations via switch port %u\n",
			count, i);

		if (i == 0) {
			/* This means we are adding the switch to the MC group.
			   We do not need to continue looking at the remote
			   port, just needed to add the port to the table */
			CL_ASSERT(count == 1);

			osm_mcast_tbl_set(p_tbl, mlid_ho, i);

			p_wobj = (osm_mcast_work_obj_t *)
			    cl_qlist_remove_head(p_port_list);
			mcast_work_obj_delete(p_wobj);
			continue;
		}

		p_node = p_sw->p_node;
		p_remote_node = osm_node_get_remote_node(p_node, i, NULL);
		if (!p_remote_node) {
			/*
			 * If we reached here, it means the minhop table has
			 * invalid entries that leads to disconnected ports.
			 *
			 * A possible reason for the code to reach here is
			 * that ucast cache is enabled, and a leaf switch that
			 * is used as a non-leaf switch in a multicast has been
			 * removed from the fabric.
			 *
			 * When it happens, we should invalidate the cache
			 * and force rerouting of the fabric.
			 */

			OSM_LOG(sm->p_log, OSM_LOG_ERROR,
				"ERR 0A1E: Tried to route MLID 0x%X through "
				"disconnected switch 0x%" PRIx64 " port %d\n",
				mlid_ho, cl_ntoh64(node_guid), i);

			/* Free memory */
			mcast_mgr_purge_list(sm, mlid_ho, p_port_list);

			/* Invalidate ucast cache */
			if (sm->ucast_mgr.p_subn->opt.use_ucast_cache &&
			    sm->ucast_mgr.cache_valid) {
				OSM_LOG(sm->p_log, OSM_LOG_INFO,
					"Unicast Cache will be invalidated due "
					"to multicast routing errors\n");
				osm_ucast_cache_invalidate(&sm->ucast_mgr);
				sm->p_subn->force_heavy_sweep = TRUE;
			}

			continue;
		}

		/*
		   This port routes frames for this mcast group.  Therefore,
		   set the appropriate bit in the multicast forwarding
		   table for this switch.
		 */
		osm_mcast_tbl_set(p_tbl, mlid_ho, i);

		if (osm_node_get_type(p_remote_node) == IB_NODE_TYPE_SWITCH) {
			/*
			   Acquire a pointer to the remote switch then recurse.
			 */
			CL_ASSERT(p_remote_node->sw);

			p_physp = osm_node_get_physp_ptr(p_node, i);
			CL_ASSERT(p_physp);

			p_remote_physp = osm_physp_get_remote(p_physp);
			CL_ASSERT(p_remote_physp);

			p_mtn->child_array[i] =
			    mcast_mgr_branch(sm, mlid_ho, p_remote_node->sw,
					     p_port_list, depth,
					     osm_physp_get_port_num
					     (p_remote_physp), p_max_depth);
		} else {
			/*
			   The neighbor node is not a switch, so this
			   must be a leaf.
			 */
			CL_ASSERT(count == 1);

			p_mtn->child_array[i] = OSM_MTREE_LEAF;
			p_wobj = (osm_mcast_work_obj_t *)
			    cl_qlist_remove_head(p_port_list);

			CL_ASSERT(cl_is_qlist_empty(p_port_list));

			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
				"Found leaf for port 0x%016" PRIx64
				" on switch port %u\n",
				cl_ntoh64(osm_port_get_guid(p_wobj->p_port)),
				i);
			mcast_work_obj_delete(p_wobj);
		}
	}

	free(list_array);
Exit:
	OSM_LOG_EXIT(sm->p_log);
	return p_mtn;
}

static ib_api_status_t mcast_mgr_build_spanning_tree(osm_sm_t * sm,
						     osm_mgrp_box_t * mbox)
{
	cl_qlist_t port_list;
	cl_qmap_t port_map;
	uint32_t num_ports;
	osm_switch_t *p_sw;
	ib_api_status_t status = IB_SUCCESS;
	uint8_t max_depth = 0;

	OSM_LOG_ENTER(sm->p_log);

	/*
	   TO DO - for now, just blow away the old tree.
	   In the future we'll need to construct the tree based
	   on multicast forwarding table information if the user wants to
	   preserve existing multicast routes.
	 */
	osm_purge_mtree(sm, mbox);

	/* build the first "subset" containing all member ports */
	if (osm_mcast_make_port_list_and_map(&port_list, &port_map, mbox)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A10: "
			"Insufficient memory to make port list\n");
		status = IB_ERROR;
		goto Exit;
	}

	num_ports = cl_qlist_count(&port_list);
	if (num_ports < 2) {
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"MLID 0x%X has %u members - nothing to do\n",
			mbox->mlid, num_ports);
		osm_mcast_drop_port_list(&port_list);
		goto Exit;
	}

	/*
	   This function builds the single spanning tree recursively.
	   At each stage, the ports to be reached are divided into
	   non-overlapping subsets of member ports that can be reached through
	   a given switch port.  Construction then moves down each
	   branch, and the process starts again with each branch computing
	   for its own subset of the member ports.

	   The maximum recursion depth is at worst the maximum hop count in the
	   subnet, which is spec limited to 64.
	 */

	/*
	   Locate the switch around which to create the spanning
	   tree for this multicast group.
	 */
	p_sw = osm_mcast_mgr_find_root_switch(sm, &port_list);
	if (p_sw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A08: "
			"Unable to locate a suitable switch for group 0x%X\n",
			mbox->mlid);
		osm_mcast_drop_port_list(&port_list);
		status = IB_ERROR;
		goto Exit;
	}

	mbox->root = mcast_mgr_branch(sm, mbox->mlid, p_sw, &port_list, 0, 0,
				      &max_depth);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Configured MLID 0x%X for %u ports, max tree depth = %u\n",
		mbox->mlid, num_ports, max_depth);
Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

#if 0
/* unused */
void osm_mcast_mgr_set_table(osm_sm_t * sm, IN const osm_mgrp_t * p_mgrp,
			     IN const osm_mtree_node_t * p_mtn)
{
	uint8_t i;
	uint8_t max_children;
	osm_mtree_node_t *p_child_mtn;
	uint16_t mlid_ho;
	osm_mcast_tbl_t *p_tbl;
	osm_switch_t *p_sw;

	OSM_LOG_ENTER(sm->p_log);

	mlid_ho = cl_ntoh16(osm_mgrp_get_mlid(p_mgrp));
	p_sw = osm_mtree_node_get_switch_ptr(p_mtn);

	CL_ASSERT(p_sw);

	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Configuring MLID 0x%X on switch 0x%" PRIx64 "\n",
		mlid_ho, osm_node_get_node_guid(p_sw->p_node));

	/*
	   For every child of this tree node, set the corresponding
	   bit in the switch's mcast table.
	 */
	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);
	max_children = osm_mtree_node_get_max_children(p_mtn);

	CL_ASSERT(max_children <= osm_switch_get_num_ports(p_sw));

	osm_mcast_tbl_clear_mlid(p_tbl, mlid_ho);

	for (i = 0; i < max_children; i++) {
		p_child_mtn = osm_mtree_node_get_child(p_mtn, i);
		if (p_child_mtn == NULL)
			continue;

		osm_mcast_tbl_set(p_tbl, mlid_ho, i);
	}

	OSM_LOG_EXIT(sm->p_log);
}
#endif

static void mcast_mgr_clear(osm_sm_t * sm, uint16_t mlid)
{
	osm_switch_t *p_sw;
	cl_qmap_t *p_sw_tbl;
	osm_mcast_tbl_t *p_mcast_tbl;

	OSM_LOG_ENTER(sm->p_log);

	/* Walk the switches and clear the routing entries for this MLID. */
	p_sw_tbl = &sm->p_subn->sw_guid_tbl;
	p_sw = (osm_switch_t *) cl_qmap_head(p_sw_tbl);
	while (p_sw != (osm_switch_t *) cl_qmap_end(p_sw_tbl)) {
		p_mcast_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);
		osm_mcast_tbl_clear_mlid(p_mcast_tbl, mlid);
		p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
	}

	OSM_LOG_EXIT(sm->p_log);
}

#if 0
/* TO DO - make this real -- at least update spanning tree */
/**********************************************************************
   Lock must be held on entry.
**********************************************************************/
ib_api_status_t osm_mcast_mgr_process_single(osm_sm_t * sm,
					     IN ib_net16_t const mlid,
					     IN ib_net64_t const port_guid,
					     IN uint8_t const join_state)
{
	uint8_t port_num;
	uint16_t mlid_ho;
	ib_net64_t sw_guid;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	osm_physp_t *p_remote_physp;
	osm_node_t *p_remote_node;
	osm_mcast_tbl_t *p_mcast_tbl;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(mlid);
	CL_ASSERT(port_guid);

	mlid_ho = cl_ntoh16(mlid);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Attempting to add port 0x%" PRIx64 " to MLID 0x%X, "
		"\n\t\t\t\tjoin state = 0x%X\n",
		cl_ntoh64(port_guid), mlid_ho, join_state);

	/*
	   Acquire the Port object.
	 */
	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);
	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A01: "
			"Unable to acquire port object for 0x%" PRIx64 "\n",
			cl_ntoh64(port_guid));
		status = IB_ERROR;
		goto Exit;
	}

	p_physp = p_port->p_physp;
	if (p_physp == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A05: "
			"Unable to acquire phsyical port object for 0x%" PRIx64
			"\n", cl_ntoh64(port_guid));
		status = IB_ERROR;
		goto Exit;
	}

	p_remote_physp = osm_physp_get_remote(p_physp);
	if (p_remote_physp == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A11: "
			"Unable to acquire remote phsyical port object "
			"for 0x%" PRIx64 "\n", cl_ntoh64(port_guid));
		status = IB_ERROR;
		goto Exit;
	}

	p_remote_node = osm_physp_get_node_ptr(p_remote_physp);

	CL_ASSERT(p_remote_node);

	sw_guid = osm_node_get_node_guid(p_remote_node);

	if (osm_node_get_type(p_remote_node) != IB_NODE_TYPE_SWITCH) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A22: "
			"Remote node not a switch node 0x%" PRIx64 "\n",
			cl_ntoh64(sw_guid));
		status = IB_ERROR;
		goto Exit;
	}

	if (!p_remote_node->sw) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A12: "
			"No switch object 0x%" PRIx64 "\n", cl_ntoh64(sw_guid));
		status = IB_ERROR;
		goto Exit;
	}

	if (osm_switch_is_in_mcast_tree(p_remote_node->sw, mlid_ho)) {
		/*
		   We're in luck. The switch attached to this port
		   is already in the multicast group, so we can just
		   add the specified port as a new leaf of the tree.
		 */
		if (join_state & (IB_JOIN_STATE_FULL | IB_JOIN_STATE_NON)) {
			/*
			   This node wants to receive multicast frames.
			   Get the switch port number to which the new member port
			   is attached, then configure this single mcast table.
			 */
			port_num = osm_physp_get_port_num(p_remote_physp);
			CL_ASSERT(port_num);

			p_mcast_tbl =
			    osm_switch_get_mcast_tbl_ptr(p_remote_node->sw);
			osm_mcast_tbl_set(p_mcast_tbl, mlid_ho, port_num);
		} else {
			if (join_state & IB_JOIN_STATE_SEND_ONLY)
				OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
					"Success.  Nothing to do for send"
					"only member\n");
			else {
				OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A13: "
					"Unknown join state 0x%X\n",
					join_state);
				status = IB_ERROR;
				goto Exit;
			}
		}
	} else
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Unable to add port\n");

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}
#endif

/**********************************************************************
 Process the entire group.
 NOTE : The lock should be held externally!
 **********************************************************************/
static ib_api_status_t mcast_mgr_process_mlid(osm_sm_t * sm, uint16_t mlid)
{
	ib_api_status_t status = IB_SUCCESS;
	struct osm_routing_engine *re = sm->p_subn->p_osm->routing_engine_used;
	osm_mgrp_box_t *mbox;

	OSM_LOG_ENTER(sm->p_log);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Processing multicast group with mlid 0x%X\n", mlid);

	/* Clear the multicast tables to start clean, then build
	   the spanning tree which sets the mcast table bits for each
	   port in the group. */
	mcast_mgr_clear(sm, mlid);

	mbox = osm_get_mbox_by_mlid(sm->p_subn, cl_hton16(mlid));
	if (mbox) {
		if (re && re->mcast_build_stree)
			status = re->mcast_build_stree(re->context, mbox);
		else
			status = mcast_mgr_build_spanning_tree(sm, mbox);

		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A17: "
				"Unable to create spanning tree (%s) for mlid "
				"0x%x\n", ib_get_err_str(status), mlid);
	}

	OSM_LOG_EXIT(sm->p_log);
	return status;
}

static void mcast_mgr_set_mfttop(IN osm_sm_t * sm, IN osm_switch_t * p_sw)
{
	osm_node_t *p_node;
	osm_dr_path_t *p_path;
	osm_physp_t *p_physp;
	osm_mcast_tbl_t *p_tbl;
	osm_madw_context_t context;
	ib_api_status_t status;
	ib_switch_info_t si;
	ib_net16_t mcast_top;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_path = osm_physp_get_dr_path_ptr(p_physp);
	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);

	if (sm->p_subn->opt.use_mfttop &&
	    p_physp->port_info.capability_mask & IB_PORT_CAP_HAS_MCAST_FDB_TOP) {
		/*
		   Set the top of the multicast forwarding table.
		 */
		si = p_sw->switch_info;
		if (sm->p_subn->first_time_master_sweep == TRUE)
			mcast_top = cl_hton16(sm->mlids_init_max);
		else {
			if (p_tbl->max_block_in_use == -1)
				mcast_top = cl_hton16(IB_LID_MCAST_START_HO - 1);
			else
				mcast_top = cl_hton16(IB_LID_MCAST_START_HO +
						      (p_tbl->max_block_in_use + 1) * IB_MCAST_BLOCK_SIZE - 1);
		}
		if (mcast_top == si.mcast_top)
			return;

		si.mcast_top = mcast_top;

		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Setting switch MFT top to MLID 0x%x\n",
			cl_ntoh16(si.mcast_top));

		context.si_context.light_sweep = FALSE;
		context.si_context.node_guid = osm_node_get_node_guid(p_node);
		context.si_context.set_method = TRUE;
		context.si_context.lft_top_change = FALSE;

		status = osm_req_set(sm, p_path, (uint8_t *) & si,
				     sizeof(si), IB_MAD_ATTR_SWITCH_INFO,
				     0, FALSE,
				     ib_port_info_get_m_key(&p_physp->port_info),
				     CL_DISP_MSGID_NONE, &context);

		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 0A1B: "
				"Sending SwitchInfo attribute failed (%s)\n",
				ib_get_err_str(status));
	}
}

static int mcast_mgr_set_mftables(osm_sm_t * sm)
{
	cl_qmap_t *p_sw_tbl = &sm->p_subn->sw_guid_tbl;
	osm_switch_t *p_sw;
	osm_mcast_tbl_t *p_tbl;
	int block_notdone, ret = 0;
	int16_t block_num, max_block = -1;

	p_sw = (osm_switch_t *) cl_qmap_head(p_sw_tbl);
	while (p_sw != (osm_switch_t *) cl_qmap_end(p_sw_tbl)) {
		p_sw->mft_block_num = 0;
		p_sw->mft_position = 0;
		p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);
		if (osm_mcast_tbl_get_max_block_in_use(p_tbl) > max_block)
			max_block = osm_mcast_tbl_get_max_block_in_use(p_tbl);
		mcast_mgr_set_mfttop(sm, p_sw);
		p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
	}

	/* Stripe the MFT blocks across the switches */
	for (block_num = 0; block_num <= max_block; block_num++) {
		block_notdone = 1;
		while (block_notdone) {
			block_notdone = 0;
			p_sw = (osm_switch_t *) cl_qmap_head(p_sw_tbl);
			while (p_sw != (osm_switch_t *) cl_qmap_end(p_sw_tbl)) {
				if (p_sw->mft_block_num == block_num) {
					block_notdone = 1;
					if (mcast_mgr_set_mft_block(sm, p_sw,
								    p_sw->mft_block_num,
								    p_sw->mft_position))
						ret = -1;
					p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);
					if (++p_sw->mft_position > p_tbl->max_position) {
						p_sw->mft_position = 0;
						p_sw->mft_block_num++;
					}
				}
				p_sw = (osm_switch_t *) cl_qmap_next(&p_sw->map_item);
			}
		}
	}

	return ret;
}

static int alloc_mfts(osm_sm_t * sm)
{
	int i;
	cl_map_item_t *item;
	osm_switch_t *p_sw;

	for (i = sm->p_subn->max_mcast_lid_ho - IB_LID_MCAST_START_HO; i >= 0;
	     i--)
		if (sm->p_subn->mboxes[i])
			break;
	if (i < 0)
		return 0;

	/* Now, walk switches and (re)allocate multicast tables */
	for (item = cl_qmap_head(&sm->p_subn->sw_guid_tbl);
	     item != cl_qmap_end(&sm->p_subn->sw_guid_tbl);
	     item = cl_qmap_next(item)) {
		p_sw = (osm_switch_t *) item;
		if (osm_mcast_tbl_realloc(&p_sw->mcast_tbl, i))
			return -1;
	}
	return 0;
}

/**********************************************************************
  This is the function that is invoked during idle time and sweep to
  handle the process request for mcast groups where join/leave/delete
  was required.
 **********************************************************************/
int osm_mcast_mgr_process(osm_sm_t * sm, boolean_t config_all)
{
	int ret = 0;
	unsigned i;
	unsigned max_mlid;

	OSM_LOG_ENTER(sm->p_log);

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);

	/* If there are no switches in the subnet we have nothing to do. */
	if (cl_qmap_count(&sm->p_subn->sw_guid_tbl) == 0) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"No switches in subnet. Nothing to do\n");
		goto exit;
	}

	if (alloc_mfts(sm)) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 0A09: alloc_mfts failed\n");
		ret = -1;
		goto exit;
	}

	max_mlid = config_all ? sm->p_subn->max_mcast_lid_ho
			- IB_LID_MCAST_START_HO : sm->mlids_req_max;
	for (i = 0; i <= max_mlid; i++) {
		if (sm->mlids_req[i] ||
		    (config_all && sm->p_subn->mboxes[i])) {
			sm->mlids_req[i] = 0;
			mcast_mgr_process_mlid(sm, i + IB_LID_MCAST_START_HO);
		}
	}

	sm->mlids_req_max = 0;

	ret = mcast_mgr_set_mftables(sm);

	osm_dump_mcast_routes(sm->p_subn->p_osm);

exit:
	CL_PLOCK_RELEASE(sm->p_lock);
	OSM_LOG_EXIT(sm->p_log);
	return ret;
}
