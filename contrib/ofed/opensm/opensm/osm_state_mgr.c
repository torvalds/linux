/*
 * Copyright (c) 2009 Sun Microsystems, Inc. All rights reserved.
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2015 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
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
 *    Implementation of osm_state_mgr_t.
 * This file implements the State Manager object.
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <complib/cl_passivelock.h>
#include <complib/cl_debug.h>
#include <complib/cl_qmap.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_STATE_MGR_C
#include <opensm/osm_sm.h>
#include <opensm/osm_madw.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_msgdef.h>
#include <opensm/osm_node.h>
#include <opensm/osm_port.h>
#include <vendor/osm_vendor_api.h>
#include <opensm/osm_inform.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_congestion_control.h>
#include <opensm/osm_db.h>
#include <opensm/osm_service.h>
#include <opensm/osm_guid.h>

extern void osm_drop_mgr_process(IN osm_sm_t * sm);
extern int osm_qos_setup(IN osm_opensm_t * p_osm);
extern int osm_pkey_mgr_process(IN osm_opensm_t * p_osm);
extern int osm_mcast_mgr_process(IN osm_sm_t * sm, boolean_t config_all);
extern int osm_link_mgr_process(IN osm_sm_t * sm, IN uint8_t state);
extern void osm_guid_mgr_process(IN osm_sm_t * sm);

static void state_mgr_up_msg(IN const osm_sm_t * sm)
{
	/*
	 * This message should be written only once - when the
	 * SM moves to Master state and the subnet is up for
	 * the first time.
	 */
	osm_log_v2(sm->p_log, sm->p_subn->first_time_master_sweep ?
		   OSM_LOG_SYS : OSM_LOG_INFO, FILE_ID, "SUBNET UP\n");

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			sm->p_subn->opt.sweep_interval ?
			"SUBNET UP" : "SUBNET UP (sweep disabled)");
}

static void state_mgr_reset_node_count(IN cl_map_item_t * p_map_item,
				       IN void *context)
{
	osm_node_t *p_node = (osm_node_t *) p_map_item;

	p_node->discovery_count = 0;

	memset(p_node->physp_discovered, 0, sizeof(uint8_t) * p_node->physp_tbl_size);
}

static void state_mgr_reset_port_count(IN cl_map_item_t * p_map_item,
				       IN void *context)
{
	osm_port_t *p_port = (osm_port_t *) p_map_item;

	p_port->discovery_count = 0;
}

static void state_mgr_reset_switch_count(IN cl_map_item_t * p_map_item,
					 IN void *context)
{
	osm_switch_t *p_sw = (osm_switch_t *) p_map_item;

	if (p_sw->max_lid_ho != 0)
		p_sw->need_update = 1;
}

static void state_mgr_get_sw_info(IN cl_map_item_t * p_object, IN void *context)
{
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	osm_dr_path_t *p_dr_path;
	osm_madw_context_t mad_context;
	osm_switch_t *const p_sw = (osm_switch_t *) p_object;
	osm_sm_t *sm = context;
	ib_api_status_t status;

	OSM_LOG_ENTER(sm->p_log);

	p_node = p_sw->p_node;
	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);

	memset(&mad_context, 0, sizeof(mad_context));

	mad_context.si_context.node_guid = osm_node_get_node_guid(p_node);
	mad_context.si_context.set_method = FALSE;
	mad_context.si_context.light_sweep = TRUE;
	mad_context.si_context.lft_top_change = FALSE;

	status = osm_req_get(sm, p_dr_path, IB_MAD_ATTR_SWITCH_INFO, 0,
			     FALSE, ib_port_info_get_m_key(&p_physp->port_info),
			     OSM_MSG_LIGHT_SWEEP_FAIL, &mad_context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3304: "
			"Request for SwitchInfo from 0x%" PRIx64 " failed (%s)\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)),
			ib_get_err_str(status));

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 Initiate a remote port info request for the given physical port
 **********************************************************************/
static void state_mgr_get_remote_port_info(IN osm_sm_t * sm,
					   IN osm_physp_t * p_physp)
{
	osm_dr_path_t *p_dr_path;
	osm_dr_path_t rem_node_dr_path;
	osm_madw_context_t mad_context;
	ib_api_status_t status;

	OSM_LOG_ENTER(sm->p_log);

	/* generate a dr path leaving on the physp to the remote node */
	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);
	memcpy(&rem_node_dr_path, p_dr_path, sizeof(osm_dr_path_t));
	if (osm_dr_path_extend(&rem_node_dr_path, osm_physp_get_port_num(p_physp))) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 332D: "
			"DR path with hop count %d couldn't be extended "
			"so skipping PortInfo query\n",
			p_dr_path->hop_count);
		goto Exit;
	}

	memset(&mad_context, 0, sizeof(mad_context));

	mad_context.pi_context.node_guid =
	    osm_node_get_node_guid(osm_physp_get_node_ptr(p_physp));
	mad_context.pi_context.port_guid = p_physp->port_guid;
	mad_context.pi_context.set_method = FALSE;
	mad_context.pi_context.light_sweep = TRUE;
	mad_context.pi_context.active_transition = FALSE;
	mad_context.pi_context.client_rereg = FALSE;

	/* note that with some negative logic - if the query failed it means
	 * that there is no point in going to heavy sweep */
	status = osm_req_get(sm, &rem_node_dr_path, IB_MAD_ATTR_PORT_INFO, 0,
			     TRUE, 0, CL_DISP_MSGID_NONE, &mad_context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 332E: "
			"Request for remote PortInfo from 0x%" PRIx64 " failed (%s)\n",
			cl_ntoh64(p_physp->port_guid), ib_get_err_str(status));

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 Initiates a thorough sweep of the subnet.
 Used when there is suspicion that something on the subnet has changed.
**********************************************************************/
static ib_api_status_t state_mgr_sweep_hop_0(IN osm_sm_t * sm)
{
	ib_api_status_t status;
	osm_dr_path_t dr_path;
	osm_bind_handle_t h_bind;
	uint8_t path_array[IB_SUBNET_PATH_HOPS_MAX];

	OSM_LOG_ENTER(sm->p_log);

	memset(path_array, 0, sizeof(path_array));

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(&sm->mad_ctrl);
	if (h_bind != OSM_BIND_INVALID_HANDLE) {
		OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
				"INITIATING HEAVY SWEEP");
		/*
		 * Start the sweep by clearing the port counts, then
		 * get our own NodeInfo at 0 hops.
		 */
		CL_PLOCK_ACQUIRE(sm->p_lock);

		cl_qmap_apply_func(&sm->p_subn->node_guid_tbl,
				   state_mgr_reset_node_count, sm);

		cl_qmap_apply_func(&sm->p_subn->port_guid_tbl,
				   state_mgr_reset_port_count, sm);

		cl_qmap_apply_func(&sm->p_subn->sw_guid_tbl,
				   state_mgr_reset_switch_count, sm);

		/* Set the in_sweep_hop_0 flag in subn to be TRUE.
		 * This will indicate the sweeping not to continue beyond the
		 * the current node.
		 * This is relevant for the case of SM on switch, since in the
		 * switch info we need to signal somehow not to continue
		 * the sweeping. */
		sm->p_subn->in_sweep_hop_0 = TRUE;

		CL_PLOCK_RELEASE(sm->p_lock);

		osm_dr_path_init(&dr_path, 0, path_array);
		CL_PLOCK_ACQUIRE(sm->p_lock);
		status = osm_req_get(sm, &dr_path, IB_MAD_ATTR_NODE_INFO, 0,
				     TRUE, 0, CL_DISP_MSGID_NONE, NULL);
		CL_PLOCK_RELEASE(sm->p_lock);
		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3305: "
				"Request for NodeInfo failed (%s)\n",
				ib_get_err_str(status));
	} else {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"No bound ports. Deferring sweep...\n");
		status = IB_INVALID_STATE;
	}

	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
 Clear out all existing port lid assignments
**********************************************************************/
static ib_api_status_t state_mgr_clean_known_lids(IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;
	cl_ptr_vector_t *p_vec = &(sm->p_subn->port_lid_tbl);
	uint32_t i;

	OSM_LOG_ENTER(sm->p_log);

	/* we need a lock here! */
	CL_PLOCK_ACQUIRE(sm->p_lock);

	for (i = 0; i < cl_ptr_vector_get_size(p_vec); i++)
		cl_ptr_vector_set(p_vec, i, NULL);

	CL_PLOCK_RELEASE(sm->p_lock);

	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
 Clear SA cache
**********************************************************************/
static ib_api_status_t state_mgr_sa_clean(IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;
	cl_qmap_t *p_port_guid_tbl;
	osm_assigned_guids_t *p_assigned_guids, *p_next_assigned_guids;
	osm_alias_guid_t *p_alias_guid, *p_next_alias_guid;
	osm_mcm_port_t *mcm_port;
	osm_subn_t * p_subn;
	osm_port_t *p_port;
	osm_infr_t *p_infr;
	osm_svcr_t *p_svcr;

	OSM_LOG_ENTER(sm->p_log);

	p_subn = sm->p_subn;

	/* we need a lock here! */
	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);

	if (p_subn->opt.drop_event_subscriptions) {
		/* Clean InformInfo records */
		p_infr = (osm_infr_t *) cl_qlist_remove_head(&p_subn->sa_infr_list);
		while (p_infr !=
		       (osm_infr_t *) cl_qlist_end(&p_subn->sa_infr_list)) {
			osm_infr_delete(p_infr);
			p_infr = (osm_infr_t *) cl_qlist_remove_head(&p_subn->sa_infr_list);
		}

		/* For now, treat Service Records in same category as InformInfos */
		/* Clean Service records */
		p_svcr = (osm_svcr_t *) cl_qlist_remove_head(&p_subn->sa_sr_list);
		while (p_svcr !=
		       (osm_svcr_t *) cl_qlist_end(&p_subn->sa_sr_list)) {
			osm_svcr_delete(p_svcr);
			p_svcr = (osm_svcr_t *) cl_qlist_remove_head(&p_subn->sa_sr_list);
		}
	}

	/* Clean Multicast member list on each port */
	p_port_guid_tbl = &p_subn->port_guid_tbl;
	for (p_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	     p_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl);
	     p_port = (osm_port_t *) cl_qmap_next(&p_port->map_item)) {
		while (!cl_is_qlist_empty(&p_port->mcm_list)) {
			mcm_port = cl_item_obj(cl_qlist_head(&p_port->mcm_list),
					       mcm_port, list_item);
			osm_mgrp_delete_port(p_subn, sm->p_log, mcm_port->mgrp,
					     p_port);
		}
		/* Hack - clean alias guid table from physp */
		free(p_port->p_physp->p_guids);
		p_port->p_physp->p_guids = NULL;
	}

	/* Clean Alias Guid work objects */
	while (cl_qlist_count(&p_subn->alias_guid_list))
		osm_guid_work_obj_delete((osm_guidinfo_work_obj_t *)
			cl_qlist_remove_head(&p_subn->alias_guid_list));

	/* Clean Assigned GUIDs table */
	p_next_assigned_guids = (osm_assigned_guids_t *)
				cl_qmap_head(&p_subn->assigned_guids_tbl);
	while (p_next_assigned_guids !=
	       (osm_assigned_guids_t *) cl_qmap_end(&p_subn->assigned_guids_tbl)) {
		p_assigned_guids = p_next_assigned_guids;
		p_next_assigned_guids = (osm_assigned_guids_t *)
					cl_qmap_next(&p_assigned_guids->map_item);
		cl_qmap_remove_item(&p_subn->assigned_guids_tbl,
				    &p_assigned_guids->map_item);
		osm_assigned_guids_delete(&p_assigned_guids);
        }

	/* Clean Alias GUIDs table */
	p_next_alias_guid = (osm_alias_guid_t *)
			    cl_qmap_head(&p_subn->alias_port_guid_tbl);
	while (p_next_alias_guid !=
	       (osm_alias_guid_t *) cl_qmap_end(&p_subn->alias_port_guid_tbl)) {
		p_alias_guid = p_next_alias_guid;
		p_next_alias_guid = (osm_alias_guid_t *)
				    cl_qmap_next(&p_alias_guid->map_item);
		if (osm_alias_guid_get_alias_guid(p_alias_guid) !=
		    osm_alias_guid_get_base_guid(p_alias_guid)) {
			/* Clean if it's not base port GUID */
			cl_qmap_remove_item(&p_subn->alias_port_guid_tbl,
					    &p_alias_guid->map_item);
			osm_alias_guid_delete(&p_alias_guid);
		}
	}

	p_subn->p_osm->sa.dirty = TRUE;

	CL_PLOCK_RELEASE(sm->p_lock);
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
 Notifies the transport layer that the local LID has changed,
 which give it a chance to update address vectors, etc..
**********************************************************************/
static ib_api_status_t state_mgr_notify_lid_change(IN osm_sm_t * sm)
{
	ib_api_status_t status;
	osm_bind_handle_t h_bind;

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(&sm->mad_ctrl);
	if (h_bind == OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3306: "
			"No bound ports\n");
		status = IB_ERROR;
		goto Exit;
	}

	/*
	 * Notify the transport layer that we changed the local LID.
	 */
	status = osm_vendor_local_lid_change(h_bind);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3307: "
			"Vendor LID update failed (%s)\n",
			ib_get_err_str(status));

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
 Returns true if the SM port is down.
 The SM's port object must exist in the port_guid table.
**********************************************************************/
static boolean_t state_mgr_is_sm_port_down(IN osm_sm_t * sm)
{
	ib_net64_t port_guid;
	osm_port_t *p_port;
	osm_physp_t *p_physp;
	uint8_t state;

	OSM_LOG_ENTER(sm->p_log);

	port_guid = sm->p_subn->sm_port_guid;

	/*
	 * If we don't know our own port guid yet, assume the port is down.
	 */
	if (port_guid == 0) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3308: "
			"SM port GUID unknown\n");
		state = IB_LINK_DOWN;
		goto Exit;
	}

	CL_ASSERT(port_guid);

	CL_PLOCK_ACQUIRE(sm->p_lock);
	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);
	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3309: "
			"SM port with GUID:%016" PRIx64 " is unknown\n",
			cl_ntoh64(port_guid));
		state = IB_LINK_DOWN;
		CL_PLOCK_RELEASE(sm->p_lock);
		goto Exit;
	}

	p_physp = p_port->p_physp;

	CL_ASSERT(p_physp);

	if (p_port->p_node->sw &&
	    !ib_switch_info_is_enhanced_port0(&p_port->p_node->sw->switch_info))
		state = IB_LINK_ACTIVE;	/* base SP0 */
	else
		state = osm_physp_get_port_state(p_physp);

	if (!p_port->discovery_count) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 330A: Failed to discover SM port\n");
		state = IB_LINK_DOWN;
	}

	CL_PLOCK_RELEASE(sm->p_lock);

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return (state == IB_LINK_DOWN);
}

/**********************************************************************
 Sweeps the node 1 hop away.
 This sets off a "chain reaction" that causes discovery of the subnet.
 Used when there is suspicion that something on the subnet has changed.
**********************************************************************/
static ib_api_status_t state_mgr_sweep_hop_1(IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_madw_context_t context;
	osm_node_t *p_node;
	osm_port_t *p_port;
	osm_dr_path_t hop_1_path;
	ib_net64_t port_guid;
	uint8_t port_num;
	uint8_t path_array[IB_SUBNET_PATH_HOPS_MAX];
	uint8_t num_ports;
	osm_physp_t *p_ext_physp;

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * First, get our own port and node objects.
	 */
	port_guid = sm->p_subn->sm_port_guid;

	CL_ASSERT(port_guid);

	/* Set the in_sweep_hop_0 flag in subn to be FALSE.
	 * This will indicate the sweeping to continue beyond the
	 * the current node.
	 * This is relevant for the case of SM on switch, since in the
	 * switch info we need to signal that the sweeping should
	 * continue through the switch. */
	sm->p_subn->in_sweep_hop_0 = FALSE;

	p_port = osm_get_port_by_guid(sm->p_subn, port_guid);
	if (!p_port) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3310: "
			"No SM port object\n");
		status = IB_ERROR;
		goto Exit;
	}

	p_node = p_port->p_node;
	CL_ASSERT(p_node);

	port_num = ib_node_info_get_local_port_num(&p_node->node_info);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Probing hop 1 on local port %u\n", port_num);

	memset(path_array, 0, sizeof(path_array));
	/* the hop_1 operations depend on the type of our node.
	 * Currently - legal nodes that can host SM are SW and CA */
	switch (osm_node_get_type(p_node)) {
	case IB_NODE_TYPE_CA:
	case IB_NODE_TYPE_ROUTER:
		memset(&context, 0, sizeof(context));
		context.ni_context.node_guid = osm_node_get_node_guid(p_node);
		context.ni_context.port_num = port_num;

		path_array[1] = port_num;

		osm_dr_path_init(&hop_1_path, 1, path_array);
		CL_PLOCK_ACQUIRE(sm->p_lock);
		status = osm_req_get(sm, &hop_1_path, IB_MAD_ATTR_NODE_INFO, 0,
				     TRUE, 0, CL_DISP_MSGID_NONE, &context);
		CL_PLOCK_RELEASE(sm->p_lock);
		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3311: "
				"Request for NodeInfo failed (%s)\n",
				ib_get_err_str(status));
		break;

	case IB_NODE_TYPE_SWITCH:
		/* Need to go over all the ports of the switch, and send a
		 * node_info from them. This doesn't include the port 0 of the
		 * switch, which hosts the SM.
		 * Note: We'll send another switchInfo on port 0, since if no
		 * ports are connected, we still want to get some response, and
		 * have the subnet come up.
		 */
		num_ports = osm_node_get_num_physp(p_node);
		for (port_num = 1; port_num < num_ports; port_num++) {
			/* go through the port only if the port is not DOWN */
			p_ext_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (p_ext_physp && ib_port_info_get_port_state
			    (&(p_ext_physp->port_info)) > IB_LINK_DOWN) {
				memset(&context, 0, sizeof(context));
				context.ni_context.node_guid =
				    osm_node_get_node_guid(p_node);
				context.ni_context.port_num = port_num;

				path_array[1] = port_num;
				osm_dr_path_init(&hop_1_path, 1, path_array);
				CL_PLOCK_ACQUIRE(sm->p_lock);
				status = osm_req_get(sm, &hop_1_path,
						     IB_MAD_ATTR_NODE_INFO, 0,
						     TRUE, 0,
						     CL_DISP_MSGID_NONE,
						     &context);
				CL_PLOCK_RELEASE(sm->p_lock);
				if (status != IB_SUCCESS)
					OSM_LOG(sm->p_log, OSM_LOG_ERROR,
						"ERR 3312: "
						"Request for NodeInfo failed (%s)\n",
						ib_get_err_str(status));
			}
		}
		break;

	default:
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 3313: Unknown node type %d (%s)\n",
			osm_node_get_type(p_node), p_node->print_desc);
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

static void query_sm_info(cl_map_item_t * item, void *cxt)
{
	osm_madw_context_t context;
	osm_remote_sm_t *r_sm = cl_item_obj(item, r_sm, map_item);
	osm_sm_t *sm = cxt;
	ib_api_status_t ret;
	osm_port_t *p_port;

	p_port= osm_get_port_by_guid(sm->p_subn, r_sm->smi.guid);
	if (p_port == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3340: "
			"No port object on given sm object\n");
		return;
        }

	context.smi_context.port_guid = r_sm->smi.guid;
	context.smi_context.set_method = FALSE;
	context.smi_context.light_sweep = TRUE;

	ret = osm_req_get(sm, osm_physp_get_dr_path_ptr(p_port->p_physp),
			  IB_MAD_ATTR_SM_INFO, 0, FALSE,
			  ib_port_info_get_m_key(&p_port->p_physp->port_info),
			  CL_DISP_MSGID_NONE, &context);
	if (ret != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3314: "
			"Failure requesting SMInfo (%s)\n",
			ib_get_err_str(ret));
}

static void state_mgr_reset_state_change_bit(IN cl_map_item_t * obj,
					     IN void *context)
{
	osm_madw_context_t mad_context;
	osm_switch_t *p_sw = (osm_switch_t *) obj;
	osm_sm_t *sm = context;
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	osm_dr_path_t *p_path;
	ib_api_status_t status;
	ib_switch_info_t si;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(p_node);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_path = osm_physp_get_dr_path_ptr(p_physp);

	if (!ib_switch_info_get_state_change(&p_sw->switch_info))
		goto exit;

	si = p_sw->switch_info;

	ib_switch_info_state_change_set(&si);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Resetting PortStateChange on switch GUID 0x%016" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	mad_context.si_context.light_sweep = FALSE;
	mad_context.si_context.node_guid = osm_node_get_node_guid(p_node);
	mad_context.si_context.set_method = TRUE;
	mad_context.si_context.lft_top_change = FALSE;

	status = osm_req_set(sm, p_path, (uint8_t *) &si,
			     sizeof(si), IB_MAD_ATTR_SWITCH_INFO,
			     0, FALSE,
			     ib_port_info_get_m_key(&p_physp->port_info),
			     CL_DISP_MSGID_NONE, &mad_context);

	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 332A: "
			"Sending SwitchInfo attribute failed (%s)\n",
			ib_get_err_str(status));

exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void state_mgr_update_node_desc(IN cl_map_item_t * obj, IN void *context)
{
	osm_madw_context_t mad_context;
	osm_node_t *p_node = (osm_node_t *) obj;
	osm_sm_t *sm = context;
	osm_physp_t *p_physp = NULL;
	unsigned i, num_ports;
	ib_api_status_t status;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_node);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Updating NodeDesc for 0x%016" PRIx64 "\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	/* get a physp to request from. */
	num_ports = osm_node_get_num_physp(p_node);
	for (i = 0; i < num_ports; i++)
		if ((p_physp = osm_node_get_physp_ptr(p_node, i)))
			break;

	if (!p_physp) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 331C: "
			"Failed to find any valid physical port object.\n");
		goto exit;
	}

	mad_context.nd_context.node_guid = osm_node_get_node_guid(p_node);

	status = osm_req_get(sm, osm_physp_get_dr_path_ptr(p_physp),
			     IB_MAD_ATTR_NODE_DESC, 0, FALSE,
			     ib_port_info_get_m_key(&p_physp->port_info),
			     CL_DISP_MSGID_NONE, &mad_context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR,
			"ERR 331B: Failure initiating NodeDescription request "
			"(%s) to 0x%016" PRIx64 "\n", ib_get_err_str(status),
			cl_ntoh64(osm_node_get_node_guid(p_node)));

exit:
	OSM_LOG_EXIT(sm->p_log);
}

void osm_reset_switch_state_change_bit(IN osm_opensm_t *osm)
{
	CL_PLOCK_ACQUIRE(&osm->lock);
	cl_qmap_apply_func(&osm->subn.sw_guid_tbl, state_mgr_reset_state_change_bit,
			   &osm->sm);
	CL_PLOCK_RELEASE(&osm->lock);
}

void osm_update_node_desc(IN osm_opensm_t *osm)
{
	CL_PLOCK_ACQUIRE(&osm->lock);
	cl_qmap_apply_func(&osm->subn.node_guid_tbl, state_mgr_update_node_desc,
			   &osm->sm);
	CL_PLOCK_RELEASE(&osm->lock);
}

/**********************************************************************
 During a light sweep, check each node to see if the node description
 is valid and if not issue a ND query.
**********************************************************************/
static void state_mgr_get_node_desc(IN cl_map_item_t * obj, IN void *context)
{
	osm_node_t *p_node = (osm_node_t *) obj;
	osm_sm_t *sm = context;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_node);

	if (p_node->print_desc
	    && strcmp(p_node->print_desc, OSM_NODE_DESC_UNKNOWN))
		/* if ND is valid, do nothing */
		goto exit;

	OSM_LOG(sm->p_log, OSM_LOG_ERROR,
		"ERR 3319: Unknown node description for node GUID "
		"0x%016" PRIx64 ".  Reissuing ND query\n",
		cl_ntoh64(osm_node_get_node_guid(p_node)));

	state_mgr_update_node_desc(obj, context);

exit:
	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 Initiates a lightweight sweep of the subnet.
 Used during normal sweeps after the subnet is up.
**********************************************************************/
static ib_api_status_t state_mgr_light_sweep_start(IN osm_sm_t * sm)
{
	ib_api_status_t status = IB_SUCCESS;
	osm_bind_handle_t h_bind;
	cl_qmap_t *p_sw_tbl;
	cl_map_item_t *p_next;
	osm_node_t *p_node;
	osm_physp_t *p_physp;
	uint8_t port_num;

	OSM_LOG_ENTER(sm->p_log);

	p_sw_tbl = &sm->p_subn->sw_guid_tbl;

	/*
	 * First, get the bind handle.
	 */
	h_bind = osm_sm_mad_ctrl_get_bind_handle(&sm->mad_ctrl);
	if (h_bind == OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"No bound ports. Deferring sweep...\n");
		status = IB_INVALID_STATE;
		goto _exit;
	}

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE, "INITIATING LIGHT SWEEP");
	CL_PLOCK_ACQUIRE(sm->p_lock);
	cl_qmap_apply_func(p_sw_tbl, state_mgr_get_sw_info, sm);
	CL_PLOCK_RELEASE(sm->p_lock);

	CL_PLOCK_ACQUIRE(sm->p_lock);
	cl_qmap_apply_func(&sm->p_subn->node_guid_tbl, state_mgr_get_node_desc,
			   sm);
	CL_PLOCK_RELEASE(sm->p_lock);

	/* now scan the list of physical ports that were not down but have no remote port */
	CL_PLOCK_ACQUIRE(sm->p_lock);
	p_next = cl_qmap_head(&sm->p_subn->node_guid_tbl);
	while (p_next != cl_qmap_end(&sm->p_subn->node_guid_tbl)) {
		p_node = (osm_node_t *) p_next;
		p_next = cl_qmap_next(p_next);

		for (port_num = 1; port_num < osm_node_get_num_physp(p_node);
		     port_num++) {
			p_physp = osm_node_get_physp_ptr(p_node, port_num);
			if (p_physp && (osm_physp_get_port_state(p_physp) !=
					IB_LINK_DOWN)
			    && !osm_physp_get_remote(p_physp)) {
				OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3315: "
					"Unknown remote side for node 0x%016"
					PRIx64
					" (%s) port %u. Adding to light sweep sampling list\n",
					cl_ntoh64(osm_node_get_node_guid
						  (p_node)),
					p_node->print_desc, port_num);

				osm_dump_dr_path_v2(sm->p_log,
						    osm_physp_get_dr_path_ptr
						    (p_physp), FILE_ID, OSM_LOG_ERROR);

				state_mgr_get_remote_port_info(sm, p_physp);
			}
		}
	}

	cl_qmap_apply_func(&sm->p_subn->sm_guid_tbl, query_sm_info, sm);

	CL_PLOCK_RELEASE(sm->p_lock);

_exit:
	OSM_LOG_EXIT(sm->p_log);
	return status;
}

/**********************************************************************
 * Go over all the remote SMs (as updated in the sm_guid_tbl).
 * Find if there is a remote sm that is a master SM.
 * If there is a remote master SM - return a pointer to it,
 * else - return NULL.
 **********************************************************************/
static osm_remote_sm_t *state_mgr_exists_other_master_sm(IN osm_sm_t * sm)
{
	cl_qmap_t *p_sm_tbl;
	osm_remote_sm_t *p_sm;
	osm_remote_sm_t *p_sm_res = NULL;
	osm_node_t *p_node;

	OSM_LOG_ENTER(sm->p_log);

	p_sm_tbl = &sm->p_subn->sm_guid_tbl;

	/* go over all the remote SMs */
	for (p_sm = (osm_remote_sm_t *) cl_qmap_head(p_sm_tbl);
	     p_sm != (osm_remote_sm_t *) cl_qmap_end(p_sm_tbl);
	     p_sm = (osm_remote_sm_t *) cl_qmap_next(&p_sm->map_item)) {
		/* If the sm is in MASTER state - return a pointer to it */
		p_node = osm_get_node_by_guid(sm->p_subn, p_sm->smi.guid);
		if (ib_sminfo_get_state(&p_sm->smi) == IB_SMINFO_STATE_MASTER) {
			OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
				"Found remote master SM with guid:0x%016" PRIx64
				" (node %s)\n", cl_ntoh64(p_sm->smi.guid),
				p_node ? p_node->print_desc : "UNKNOWN");
			p_sm_res = p_sm;
			goto Exit;
		}
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
	return p_sm_res;
}

/**********************************************************************
 * Go over all remote SMs (as updated in the sm_guid_tbl).
 * Find the one with the highest priority and lowest guid.
 * Compare this SM to the local SM. If the local SM is higher -
 * return NULL, if the remote SM is higher - return a pointer to it.
 **********************************************************************/
static osm_remote_sm_t *state_mgr_get_highest_sm(IN osm_sm_t * sm)
{
	cl_qmap_t *p_sm_tbl;
	osm_remote_sm_t *p_sm = NULL;
	osm_remote_sm_t *p_highest_sm;
	uint8_t highest_sm_priority;
	ib_net64_t highest_sm_guid;
	osm_node_t *p_node;

	OSM_LOG_ENTER(sm->p_log);

	p_sm_tbl = &sm->p_subn->sm_guid_tbl;

	/* Start with the local sm as the standard */
	p_highest_sm = NULL;
	highest_sm_priority = sm->p_subn->opt.sm_priority;
	highest_sm_guid = sm->p_subn->sm_port_guid;

	/* go over all the remote SMs */
	for (p_sm = (osm_remote_sm_t *) cl_qmap_head(p_sm_tbl);
	     p_sm != (osm_remote_sm_t *) cl_qmap_end(p_sm_tbl);
	     p_sm = (osm_remote_sm_t *) cl_qmap_next(&p_sm->map_item)) {

		/* If the sm is in NOTACTIVE state - continue */
		if (ib_sminfo_get_state(&p_sm->smi) ==
		    IB_SMINFO_STATE_NOTACTIVE)
			continue;

		if (osm_sm_is_greater_than(ib_sminfo_get_priority(&p_sm->smi),
					   p_sm->smi.guid, highest_sm_priority,
					   highest_sm_guid)) {
			/* the new p_sm is with higher priority - update the highest_sm */
			/* to this sm */
			p_highest_sm = p_sm;
			highest_sm_priority =
			    ib_sminfo_get_priority(&p_sm->smi);
			highest_sm_guid = p_sm->smi.guid;
		}
	}

	if (p_highest_sm != NULL) {
		p_node = osm_get_node_by_guid(sm->p_subn, p_highest_sm->smi.guid);
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Found higher priority SM with guid: %016" PRIx64 " (node %s)\n",
			cl_ntoh64(p_highest_sm->smi.guid),
			p_node ? p_node->print_desc : "UNKNOWN");
	}
	OSM_LOG_EXIT(sm->p_log);
	return p_highest_sm;
}

/**********************************************************************
 * Send SubnSet(SMInfo) SMP with HANDOVER attribute to the
 * remote_sm indicated.
 **********************************************************************/
static void state_mgr_send_handover(IN osm_sm_t * sm, IN osm_remote_sm_t * p_sm)
{
	uint8_t payload[IB_SMP_DATA_SIZE];
	ib_sm_info_t *p_smi = (ib_sm_info_t *) payload;
	osm_madw_context_t context;
	const osm_port_t *p_port;
	ib_api_status_t status;

	OSM_LOG_ENTER(sm->p_log);

	/*
	 * Send a query of SubnSet(SMInfo) HANDOVER to the remote sm given.
	 */

	memset(&context, 0, sizeof(context));
	p_port = osm_get_port_by_guid(sm->p_subn, p_sm->smi.guid);
	if (p_port == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3316: "
			"No port object on given remote_sm object\n");
		goto Exit;
	}

	/* update the master_guid in the sm_state_mgr object according to */
	/* the guid of the port where the new Master SM should reside. */
	OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
		"Handing over mastership. Updating sm_state_mgr master_guid: %016"
		PRIx64 " (node %s)\n", cl_ntoh64(p_port->guid),
		p_port->p_node ? p_port->p_node->print_desc : "UNKNOWN");
	sm->master_sm_guid = p_port->guid;

	context.smi_context.port_guid = p_port->guid;
	context.smi_context.set_method = TRUE;

	memset(payload, 0, sizeof(payload));
	p_smi->guid = sm->p_subn->sm_port_guid;
	p_smi->act_count = cl_hton32(sm->p_subn->p_osm->stats.qp0_mads_sent);
	p_smi->pri_state = (uint8_t) (sm->p_subn->sm_state |
				      sm->p_subn->opt.sm_priority << 4);
	p_smi->sm_key = sm->p_subn->opt.sm_key;

	CL_PLOCK_ACQUIRE(sm->p_lock);
	status = osm_req_set(sm, osm_physp_get_dr_path_ptr(p_port->p_physp),
			     payload, sizeof(payload), IB_MAD_ATTR_SM_INFO,
			     IB_SMINFO_ATTR_MOD_HANDOVER, FALSE,
			     ib_port_info_get_m_key(&p_port->p_physp->port_info),
			     CL_DISP_MSGID_NONE, &context);
	CL_PLOCK_RELEASE(sm->p_lock);

	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3317: "
			"Failure requesting SMInfo (%s), remote SM at 0x%"
			PRIx64 " (node %s)\n",
			ib_get_err_str(status), cl_ntoh64(p_port->guid),
			p_port->p_node ? p_port->p_node->print_desc : "UNKNOWN");

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 * Send Trap 64 on all new ports.
 **********************************************************************/
static void state_mgr_report_new_ports(IN osm_sm_t * sm)
{
	ib_gid_t port_gid;
	ib_mad_notice_attr_t notice;
	ib_api_status_t status;
	ib_net64_t port_guid;
	cl_map_item_t *p_next;
	osm_port_t *p_port;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;

	OSM_LOG_ENTER(sm->p_log);

	CL_PLOCK_ACQUIRE(sm->p_lock);
	p_next = cl_qmap_head(&sm->p_subn->port_guid_tbl);
	while (p_next != cl_qmap_end(&sm->p_subn->port_guid_tbl)) {
		p_port = (osm_port_t *) p_next;
		p_next = cl_qmap_next(p_next);

		if (!p_port->is_new)
			continue;

		port_guid = osm_port_get_guid(p_port);
		/* issue a notice - trap 64 (SM_GID_IN_SERVICE_TRAP) */

		/* details of the notice */
		notice.generic_type = 0x80 | IB_NOTICE_TYPE_SUBN_MGMT;	/* is generic subn mgt type */
		ib_notice_set_prod_type_ho(&notice, 4);	/* A Class Manager generator */
		/* endport becomes reachable */
		notice.g_or_v.generic.trap_num = CL_HTON16(SM_GID_IN_SERVICE_TRAP); /* 64 */
		/* The sm_base_lid is saved in network order already. */
		notice.issuer_lid = sm->p_subn->sm_base_lid;
		/* following C14-72.1.1 and table 119 p739 */
		/* we need to provide the GID */
		port_gid.unicast.prefix = sm->p_subn->opt.subnet_prefix;
		port_gid.unicast.interface_id = port_guid;
		memcpy(&(notice.data_details.ntc_64_67.gid), &(port_gid),
		       sizeof(ib_gid_t));

		/* According to page 653 - the issuer gid in this case of trap
		 * is the SM gid, since the SM is the initiator of this trap. */
		notice.issuer_gid.unicast.prefix =
		    sm->p_subn->opt.subnet_prefix;
		notice.issuer_gid.unicast.interface_id =
		    sm->p_subn->sm_port_guid;

		status = osm_report_notice(sm->p_log, sm->p_subn, &notice);
		if (status != IB_SUCCESS)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3318: "
				"Error sending trap reports on GUID:0x%016"
				PRIx64 " (%s)\n", port_gid.unicast.interface_id,
				ib_get_err_str(status));
		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);
		OSM_LOG(sm->p_log, OSM_LOG_INFO,
			"Discovered new port with GUID:0x%016" PRIx64
			" LID range [%u,%u] of node: %s\n",
			cl_ntoh64(port_gid.unicast.interface_id),
			min_lid_ho, max_lid_ho,
			p_port->p_node ? p_port->p_node->
			print_desc : "UNKNOWN");

		p_port->is_new = 0;
	}
	CL_PLOCK_RELEASE(sm->p_lock);

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 * Make sure that the lid_port_tbl of the subnet has only the ports
 * that are recognized, and in the correct lid place. There could be
 * errors if we wanted to assign a certain port with lid X, but that
 * request didn't reach the port. In this case port_lid_tbl will have
 * the port under lid X, though the port isn't updated with this lid.
 * We will run a new heavy sweep (since there were errors in the
 * initialization), but here we'll clean the database from incorrect
 * information.
 **********************************************************************/
static void state_mgr_check_tbl_consistency(IN osm_sm_t * sm)
{
	cl_qmap_t *p_port_guid_tbl;
	osm_port_t *p_port;
	osm_port_t *p_next_port;
	cl_ptr_vector_t *p_port_lid_tbl;
	size_t max_lid, ref_size, curr_size, lid;
	osm_port_t *p_port_ref, *p_port_stored;
	cl_ptr_vector_t ref_port_lid_tbl;
	uint16_t min_lid_ho;
	uint16_t max_lid_ho;
	uint16_t lid_ho;

	OSM_LOG_ENTER(sm->p_log);

	if (sm->lid_mgr.dirty == FALSE)
		goto Exit;

	sm->lid_mgr.dirty = FALSE;

	cl_ptr_vector_construct(&ref_port_lid_tbl);
	cl_ptr_vector_init(&ref_port_lid_tbl,
			   cl_ptr_vector_get_size(&sm->p_subn->port_lid_tbl),
			   OSM_SUBNET_VECTOR_GROW_SIZE);

	p_port_guid_tbl = &sm->p_subn->port_guid_tbl;

	/* Let's go over all the ports according to port_guid_tbl,
	 * and add the port to a reference port_lid_tbl. */
	p_next_port = (osm_port_t *) cl_qmap_head(p_port_guid_tbl);
	while (p_next_port != (osm_port_t *) cl_qmap_end(p_port_guid_tbl)) {
		p_port = p_next_port;
		p_next_port =
		    (osm_port_t *) cl_qmap_next(&p_next_port->map_item);

		osm_port_get_lid_range_ho(p_port, &min_lid_ho, &max_lid_ho);
		for (lid_ho = min_lid_ho; lid_ho <= max_lid_ho; lid_ho++)
			cl_ptr_vector_set(&ref_port_lid_tbl, lid_ho, p_port);
	}

	p_port_lid_tbl = &sm->p_subn->port_lid_tbl;

	ref_size = cl_ptr_vector_get_size(&ref_port_lid_tbl);
	curr_size = cl_ptr_vector_get_size(p_port_lid_tbl);
	/* They should be the same, but compare it anyway */
	max_lid = (ref_size > curr_size) ? ref_size : curr_size;

	for (lid = 1; lid < max_lid; lid++) {
		p_port_ref = NULL;
		p_port_stored = NULL;
		cl_ptr_vector_at(p_port_lid_tbl, lid, (void *)&p_port_stored);
		cl_ptr_vector_at(&ref_port_lid_tbl, lid, (void *)&p_port_ref);

		if (p_port_stored == p_port_ref)
			/* This is the "good" case - both entries are the
			 * same for this lid. Nothing to do. */
			continue;

		if (p_port_ref == NULL) {
			/* There is an object in the subnet database for this
			 * lid, but no such object exists in the reference
			 * port_list_tbl. This can occur if we wanted to assign
			 * a certain port with some lid (different than the one
			 * pre-assigned to it), and the port didn't get the
			 * PortInfo Set request. Due to this, the port is
			 * updated with its original lid in our database
			 * rather than the new lid we wanted to give it. */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3322: "
				"lid %zu is wrongly assigned to port 0x%016"
				PRIx64 " (\'%s\' port %u) in port_lid_tbl\n",
				lid,
				cl_ntoh64(osm_port_get_guid(p_port_stored)),
				p_port_stored->p_node->print_desc,
				p_port_stored->p_physp->port_num);
		} else if (p_port_stored == NULL)
			/* There is an object in the new database, but no
			 * object in our subnet database. This is the matching
			 * case of the prior check - the port still has its
			 * original lid. */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3323: "
				"port 0x%016" PRIx64 " (\'%s\' port %u)"
				" exists in new port_lid_tbl under lid %zu,"
				" but missing in subnet port_lid_tbl db\n",
				cl_ntoh64(osm_port_get_guid(p_port_ref)),
				p_port_ref->p_node->print_desc,
				p_port_ref->p_physp->port_num, lid);
		else {
			/* if we reached here then p_port_stored != p_port_ref.
			 * We were trying to set a lid to p_port_stored, but
			 * it didn't reach it, and p_port_ref also didn't get
			 * the lid update. */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3324: "
				"lid %zu has port 0x%016" PRIx64
				" (\'%s\' port %u) in new port_lid_tbl db, "
				"and port 0x%016" PRIx64 " (\'%s\' port %u)"
				" in subnet port_lid_tbl db\n", lid,
				cl_ntoh64(osm_port_get_guid(p_port_ref)),
				p_port_ref->p_node->print_desc,
				p_port_ref->p_physp->port_num,
				cl_ntoh64(osm_port_get_guid(p_port_stored)),
				p_port_stored->p_node->print_desc,
				p_port_stored->p_physp->port_num);
		}

		/*
		 * Clear the lid of the port in order to ignore it
		 *  in routing phase
		 */
		if (p_port_stored) {
			OSM_LOG(sm->p_log, OSM_LOG_INFO, "Clearing Lid for "
				"port 0x%016" PRIx64 "\n",
				cl_ntoh64(osm_port_get_guid(p_port_stored)));
			osm_port_clear_base_lid(p_port_stored);
			cl_ptr_vector_set(p_port_lid_tbl, lid, NULL);
		}

		/* Make sure we'll do another heavy sweep. */
		sm->p_subn->subnet_initialization_error = TRUE;
	}

	cl_ptr_vector_destroy(&ref_port_lid_tbl);

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

static void check_switch_lft(cl_map_item_t * item, void *log)
{
	osm_switch_t *sw = (osm_switch_t *) item;

	if (!sw->new_lft)
		return;

	if (memcmp(sw->lft, sw->new_lft, sw->max_lid_ho + 1))
		osm_log_v2(log, OSM_LOG_ERROR, FILE_ID, "ERR 331D: "
			   "LFT of switch 0x%016" PRIx64 " (%s) is not up to date\n",
			   cl_ntoh64(sw->p_node->node_info.node_guid),
			   sw->p_node->print_desc);
}

int wait_for_pending_transactions(osm_stats_t * stats)
{
#ifdef HAVE_LIBPTHREAD
	pthread_mutex_lock(&stats->mutex);
	while (stats->qp0_mads_outstanding && !osm_exit_flag)
		pthread_cond_wait(&stats->cond, &stats->mutex);
	pthread_mutex_unlock(&stats->mutex);
#else
	while (1) {
		unsigned count = stats->qp0_mads_outstanding;
		if (!count || osm_exit_flag)
			break;
		cl_event_wait_on(&stats->event, EVENT_NO_TIMEOUT, TRUE);
	}
#endif
	return osm_exit_flag;
}

static void do_sweep(osm_sm_t * sm)
{
	ib_api_status_t status;
	osm_remote_sm_t *p_remote_sm;
	unsigned config_parsed = 0;

	if (sm->p_subn->force_first_time_master_sweep) {
		sm->p_subn->force_heavy_sweep = TRUE;
		sm->p_subn->coming_out_of_standby = TRUE;
		sm->p_subn->first_time_master_sweep = TRUE;
		sm->p_subn->force_first_time_master_sweep = FALSE;
	}

	/* after subnet initialization error, run heavy sweep */
	if (sm->p_subn->subnet_initialization_error)
		sm->p_subn->force_heavy_sweep = TRUE;

	if (sm->p_subn->force_heavy_sweep) {
		if (osm_subn_rescan_conf_files(sm->p_subn) < 0)
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 331A: "
				"osm_subn_rescan_conf_file failed\n");
		else
			config_parsed = 1;
	}

	if (sm->p_subn->sm_state != IB_SMINFO_STATE_MASTER &&
	    sm->p_subn->sm_state != IB_SMINFO_STATE_DISCOVERING)
		return;

	if (sm->p_subn->coming_out_of_standby) {
		/*
		 * Need to force re-write of sm_base_lid to all ports
		 * to do that we want all the ports to be considered
		 * foreign
		 */
		state_mgr_clean_known_lids(sm);

		/*
		 * Need to clean SA cache when state changes to STANDBY
		 * after handover.
		 */
		state_mgr_sa_clean(sm);

		/*
		 * Need to reconfigure LFTs, PKEYs, and QoS on all switches
		 * when coming out of STANDBY
		 */
		sm->p_subn->need_update = 1;
	}

	sm->master_sm_found = 0;

	/*
	 * If we already have switches, then try a light sweep.
	 * Otherwise, this is probably our first discovery pass
	 * or we are connected in loopback. In both cases do a
	 * heavy sweep.
	 * Note: If we are connected in loopback we want a heavy
	 * sweep, since we will not be getting any traps if there is
	 * a lost connection.
	 */
	/*  if we are in DISCOVERING state - this means it is either in
	 *  initializing or wake up from STANDBY - run the heavy sweep */
	if (cl_qmap_count(&sm->p_subn->sw_guid_tbl)
	    && sm->p_subn->sm_state != IB_SMINFO_STATE_DISCOVERING
	    && sm->p_subn->opt.force_heavy_sweep == FALSE
	    && sm->p_subn->force_heavy_sweep == FALSE
	    && sm->p_subn->force_reroute == FALSE
	    && sm->p_subn->subnet_initialization_error == FALSE
	    && (state_mgr_light_sweep_start(sm) == IB_SUCCESS)) {
		if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
			return;
		if (!sm->p_subn->force_heavy_sweep) {
			if (sm->p_subn->opt.sa_db_dump &&
			    !osm_sa_db_file_dump(sm->p_subn->p_osm))
				osm_opensm_report_event(sm->p_subn->p_osm,
							OSM_EVENT_ID_SA_DB_DUMPED,
							NULL);
			OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
					"LIGHT SWEEP COMPLETE");
			return;
		}
	}

	/*
	 * Unicast cache should be invalidated when subnet re-route is
	 * requested, and when OpenSM comes out of standby state.
	 */
	if (sm->p_subn->opt.use_ucast_cache &&
	    (sm->p_subn->force_reroute || sm->p_subn->coming_out_of_standby))
		osm_ucast_cache_invalidate(&sm->ucast_mgr);

	/*
	 * If we don't need to do a heavy sweep and we want to do a reroute,
	 * just reroute only.
	 */
	if (cl_qmap_count(&sm->p_subn->sw_guid_tbl)
	    && sm->p_subn->sm_state != IB_SMINFO_STATE_DISCOVERING
	    && sm->p_subn->opt.force_heavy_sweep == FALSE
	    && sm->p_subn->force_heavy_sweep == FALSE
	    && sm->p_subn->force_reroute == TRUE
	    && sm->p_subn->subnet_initialization_error == FALSE) {
		/* Reset flag */
		sm->p_subn->force_reroute = FALSE;

		/* Re-program the switches fully */
		sm->p_subn->ignore_existing_lfts = TRUE;

		if (osm_ucast_mgr_process(&sm->ucast_mgr)) {
			OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
					"REROUTE FAILED");
			return;
		}
		osm_qos_setup(sm->p_subn->p_osm);

		/* Reset flag */
		sm->p_subn->ignore_existing_lfts = FALSE;

		if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
			return;

		osm_congestion_control_setup(sm->p_subn->p_osm);

		if (osm_congestion_control_wait_pending_transactions(sm->p_subn->p_osm))
			return;

		if (!sm->p_subn->subnet_initialization_error) {
			OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
					"REROUTE COMPLETE");
			osm_opensm_report_event(sm->p_subn->p_osm,
						OSM_EVENT_ID_UCAST_ROUTING_DONE,
						(void *) UCAST_ROUTING_REROUTE);
			return;
		}
	}

	osm_opensm_report_event(sm->p_subn->p_osm,
				OSM_EVENT_ID_HEAVY_SWEEP_START, NULL);

	/* go to heavy sweep */
repeat_discovery:

	/* First of all - unset all flags */
	sm->p_subn->force_heavy_sweep = FALSE;
	sm->p_subn->force_reroute = FALSE;
	sm->p_subn->subnet_initialization_error = FALSE;

	/* Reset tracking values in case limiting component got removed
	 * from fabric. */
	sm->p_subn->min_ca_mtu = IB_MAX_MTU;
	sm->p_subn->min_ca_rate = IB_PATH_RECORD_RATE_300_GBS;
	sm->p_subn->min_data_vls = IB_MAX_NUM_VLS - 1;
	sm->p_subn->min_sw_data_vls = IB_MAX_NUM_VLS - 1;

	/* rescan configuration updates */
	if (!config_parsed && osm_subn_rescan_conf_files(sm->p_subn) < 0)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 331A: "
			"osm_subn_rescan_conf_file failed\n");

	if (sm->p_subn->sm_state != IB_SMINFO_STATE_MASTER)
		sm->p_subn->need_update = 1;

	status = state_mgr_sweep_hop_0(sm);
	if (status != IB_SUCCESS ||
	    wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	if (state_mgr_is_sm_port_down(sm) == TRUE) {
		if (sm->p_subn->last_sm_port_state) {
			sm->p_subn->last_sm_port_state = 0;
			state_mgr_sa_clean(sm);
			osm_log_v2(sm->p_log, OSM_LOG_SYS, FILE_ID,
				   "SM port is down\n");
			OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
					"SM PORT DOWN");
		}

		/* Run the drop manager - we want to clear all records */
		osm_drop_mgr_process(sm);

		/* Move to DISCOVERING state */
		if (sm->p_subn->sm_state != IB_SMINFO_STATE_DISCOVERING)
			osm_sm_state_mgr_process(sm, OSM_SM_SIGNAL_DISCOVER);
		osm_opensm_report_event(sm->p_subn->p_osm,
					OSM_EVENT_ID_STATE_CHANGE, NULL);
		return;
	} else {
		if (!sm->p_subn->last_sm_port_state) {
			sm->p_subn->last_sm_port_state = 1;
			osm_log_v2(sm->p_log, OSM_LOG_SYS, FILE_ID,
				   "SM port is up\n");
			OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
					"SM PORT UP");
		}
	}

	status = state_mgr_sweep_hop_1(sm);
	if (status != IB_SUCCESS ||
	    wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	/* discovery completed - check other sm presence */
	if (sm->master_sm_found) {
		/*
		 * Call the sm_state_mgr with signal
		 * MASTER_OR_HIGHER_SM_DETECTED_DONE
		 */
		osm_sm_state_mgr_process(sm,
					 OSM_SM_SIGNAL_MASTER_OR_HIGHER_SM_DETECTED);
		OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
				"ENTERING STANDBY STATE");
		/* notify master SM about us */
		osm_send_trap144(sm, 0);
		osm_opensm_report_event(sm->p_subn->p_osm,
					OSM_EVENT_ID_STATE_CHANGE, NULL);
		return;
	}

	/* if new sweep requested - don't bother with the rest */
	if (sm->p_subn->force_heavy_sweep) {
		config_parsed = 0;
		goto repeat_discovery;
	}

	osm_opensm_report_event(sm->p_subn->p_osm,
				OSM_EVENT_ID_HEAVY_SWEEP_DONE, NULL);

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE, "HEAVY SWEEP COMPLETE");

	osm_drop_mgr_process(sm);

	/* If we are MASTER - get the highest remote_sm, and
	 * see if it is higher than our local sm.
	 */
	if (sm->p_subn->sm_state == IB_SMINFO_STATE_MASTER) {
		p_remote_sm = state_mgr_get_highest_sm(sm);
		if (p_remote_sm != NULL) {
			/* report new ports (trap 64) before leaving MASTER */
			state_mgr_report_new_ports(sm);

			/* need to handover the mastership
			 * to the remote sm, and move to standby */
			state_mgr_send_handover(sm, p_remote_sm);
			osm_sm_state_mgr_process(sm,
						 OSM_SM_SIGNAL_HANDOVER_SENT);
			return;
		} else {
			/* We are the highest sm - check to see if there is
			 * a remote SM that is in master state. */
			p_remote_sm = state_mgr_exists_other_master_sm(sm);
			if (p_remote_sm != NULL) {
				/* There is a remote SM that is master.
				 * need to wait for that SM to relinquish control
				 * of its portion of the subnet. C14-60.2.1.
				 * Also - need to start polling on that SM. */
				CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);
				sm->polling_sm_guid = p_remote_sm->smi.guid;
				CL_PLOCK_RELEASE(sm->p_lock);
				osm_sm_state_mgr_process(sm,
							 OSM_SM_SIGNAL_WAIT_FOR_HANDOVER);
				return;
			} else if (sm->polling_sm_guid) {
				/* Stop polling SM if it's not found */
				osm_sm_state_mgr_process(sm,
							 OSM_SM_SIGNAL_POLLING_TIMEOUT);
				return;
			}
		}
	}

	/*
	 * If we are not MASTER already - this means that we are
	 * in discovery state. call osm_sm_state_mgr with signal
	 * DISCOVERY_COMPLETED
	 */
	if (sm->p_subn->sm_state == IB_SMINFO_STATE_DISCOVERING)
		osm_sm_state_mgr_process(sm, OSM_SM_SIGNAL_DISCOVERY_COMPLETED);

	osm_reset_switch_state_change_bit(sm->p_subn->p_osm);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	osm_pkey_mgr_process(sm->p_subn->p_osm);

	/* try to restore SA DB (this should be before lid_mgr
	   because we may want to disable clients reregistration
	   when SA DB is restored) */
	osm_sa_db_file_load(sm->p_subn->p_osm);

	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"PKEY setup completed - STARTING SM LID CONFIG");

	osm_lid_mgr_process_sm(&sm->lid_mgr);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"SM LID ASSIGNMENT COMPLETE - STARTING SUBNET LID CONFIG");
	state_mgr_notify_lid_change(sm);

	osm_lid_mgr_process_subnet(&sm->lid_mgr);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	/* At this point we need to check the consistency of
	 * the port_lid_tbl under the subnet. There might be
	 * errors in it if PortInfo Set requests didn't reach
	 * their destination. */
	state_mgr_check_tbl_consistency(sm);

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"LID ASSIGNMENT COMPLETE - STARTING SWITCH TABLE CONFIG");

	/*
	 * Proceed with unicast forwarding table configuration; if it fails
	 * return early to wait for a trap or the next sweep interval.
	 */

	if (!sm->ucast_mgr.cache_valid ||
	    osm_ucast_cache_process(&sm->ucast_mgr)) {
		if (osm_ucast_mgr_process(&sm->ucast_mgr)) {
			osm_ucast_cache_invalidate(&sm->ucast_mgr);
			return;
		}
	}

	osm_qos_setup(sm->p_subn->p_osm);

	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	/* We are done setting all LFTs so clear the ignore existing.
	 * From now on, as long as we are still master, we want to
	 * take into account these lfts. */
	sm->p_subn->ignore_existing_lfts = FALSE;

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"SWITCHES CONFIGURED FOR UNICAST");
	osm_opensm_report_event(sm->p_subn->p_osm,
				OSM_EVENT_ID_UCAST_ROUTING_DONE,
				(void *) UCAST_ROUTING_HEAVY_SWEEP);

	if (!sm->p_subn->opt.disable_multicast) {
		osm_mcast_mgr_process(sm, TRUE);
		if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
			return;
		OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
				"SWITCHES CONFIGURED FOR MULTICAST");
	}

	osm_guid_mgr_process(sm);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;
	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE, "ALIAS GUIDS CONFIGURED");

	/*
	 * The LINK_PORTS state is required since we cannot count on
	 * the port state change MADs to succeed. This is an artifact
	 * of the spec defining state change from state X to state X
	 * as an error. The hardware then is not required to process
	 * other parameters provided by the Set(PortInfo) Packet.
	 */

	osm_link_mgr_process(sm, IB_LINK_NO_CHANGE);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"LINKS PORTS CONFIGURED - SET LINKS TO ARMED STATE");

	osm_link_mgr_process(sm, IB_LINK_ARMED);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_VERBOSE,
			"LINKS ARMED - SET LINKS TO ACTIVE STATE");

	osm_link_mgr_process(sm, IB_LINK_ACTIVE);
	if (wait_for_pending_transactions(&sm->p_subn->p_osm->stats))
		return;

	/*
	 * The sweep completed!
	 */

	/* Now do GSI configuration */

	osm_congestion_control_setup(sm->p_subn->p_osm);

	if (osm_congestion_control_wait_pending_transactions(sm->p_subn->p_osm))
		return;

	/*
	 * Send trap 64 on newly discovered endports
	 */
	state_mgr_report_new_ports(sm);

	/* check switch lft buffers assignments */
	cl_qmap_apply_func(&sm->p_subn->sw_guid_tbl, check_switch_lft,
			   sm->p_log);

	/* in any case we zero this flag */
	sm->p_subn->coming_out_of_standby = FALSE;
	sm->p_subn->first_time_master_sweep = FALSE;

	/* If there were errors - then the subnet is not really up */
	if (sm->p_subn->subnet_initialization_error == TRUE) {
		osm_log_v2(sm->p_log, OSM_LOG_SYS, FILE_ID,
			   "Errors during initialization\n");
		OSM_LOG_MSG_BOX(sm->p_log, OSM_LOG_ERROR,
				"ERRORS DURING INITIALIZATION");
	} else {
		sm->p_subn->need_update = 0;
		osm_dump_all(sm->p_subn->p_osm);
		state_mgr_up_msg(sm);

		if ((OSM_LOG_IS_ACTIVE_V2(sm->p_log, OSM_LOG_VERBOSE) ||
		    sm->p_subn->opt.sa_db_dump) &&
		    !osm_sa_db_file_dump(sm->p_subn->p_osm))
			osm_opensm_report_event(sm->p_subn->p_osm,
						OSM_EVENT_ID_SA_DB_DUMPED,
						NULL);
	}

	/*
	 * Finally signal the subnet up event
	 */
	cl_event_signal(&sm->subnet_up_event);

	/* if we got a signal to force heavy sweep or errors
	 * in the middle of the sweep - try another sweep. */
	if (sm->p_subn->force_heavy_sweep)
		osm_sm_signal(sm, OSM_SIGNAL_SWEEP);

	/* Write a new copy of our persistent guid2mkey database */
	osm_db_store(sm->p_subn->p_g2m, sm->p_subn->opt.fsync_high_avail_files);
	osm_db_store(sm->p_subn->p_neighbor,
		     sm->p_subn->opt.fsync_high_avail_files);

	osm_opensm_report_event(sm->p_subn->p_osm, OSM_EVENT_ID_SUBNET_UP,
				NULL);
}

static void do_process_mgrp_queue(osm_sm_t * sm)
{
	if (sm->p_subn->sm_state != IB_SMINFO_STATE_MASTER)
		return;
	if (!sm->p_subn->opt.disable_multicast) {
		osm_mcast_mgr_process(sm, FALSE);
		wait_for_pending_transactions(&sm->p_subn->p_osm->stats);
	}
}

static void do_process_guid_queue(osm_sm_t *sm)
{
	osm_guid_mgr_process(sm);
	wait_for_pending_transactions(&sm->p_subn->p_osm->stats);
}

void osm_state_mgr_process(IN osm_sm_t * sm, IN osm_signal_t signal)
{
	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Received signal %s in state %s\n",
		osm_get_sm_signal_str(signal),
		osm_get_sm_mgr_state_str(sm->p_subn->sm_state));

	switch (signal) {
	case OSM_SIGNAL_SWEEP:
		if (!sm->p_subn->sweeping_enabled) {
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "sweeping disabled - "
				"ignoring signal %s in state %s\n",
				osm_get_sm_signal_str(signal),
				osm_get_sm_mgr_state_str(sm->p_subn->sm_state));
		} else
			do_sweep(sm);
		break;
	case OSM_SIGNAL_IDLE_TIME_PROCESS_REQUEST:
		do_process_mgrp_queue(sm);
		break;
	case OSM_SIGNAL_GUID_PROCESS_REQUEST:
		do_process_guid_queue(sm);
		break;
	default:
		CL_ASSERT(FALSE);
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3320: "
			"Invalid SM signal %u\n", signal);
		break;
	}

	OSM_LOG_EXIT(sm->p_log);
}
