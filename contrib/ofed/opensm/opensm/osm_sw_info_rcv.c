/*
 * Copyright (c) 2004-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005,2008 Mellanox Technologies LTD. All rights reserved.
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
 *    Implementation of osm_si_rcv_t.
 * This object represents the SwitchInfo Receiver object.
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
#define FILE_ID OSM_FILE_SW_INFO_RCV_C
#include <opensm/osm_log.h>
#include <opensm/osm_switch.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_helper.h>
#include <opensm/osm_opensm.h>

#if 0
/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void si_rcv_get_fwd_tbl(IN osm_sm_t * sm, IN osm_switch_t * p_sw)
{
	osm_madw_context_t context;
	osm_dr_path_t *p_dr_path;
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	uint32_t block_id_ho;
	uint32_t max_block_id_ho;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	context.lft_context.node_guid = osm_node_get_node_guid(p_node);
	context.lft_context.set_method = FALSE;

	max_block_id_ho = osm_switch_get_max_block_id_in_use(p_sw);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);

	for (block_id_ho = 0; block_id_ho <= max_block_id_ho; block_id_ho++) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Retrieving FT block %u\n", block_id_ho);

		status = osm_req_get(sm, p_dr_path, IB_MAD_ATTR_LIN_FWD_TBL,
				     cl_hton32(block_id_ho), TRUE, 0,
				     CL_DISP_MSGID_NONE, &context);
		if (status != IB_SUCCESS)
			/* continue the loop despite the error */
			OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3603: "
				"Failure initiating PortInfo request (%s)\n",
				ib_get_err_str(status));
	}

	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
 The plock must be held before calling this function.
**********************************************************************/
static void si_rcv_get_mcast_fwd_tbl(IN osm_sm_t * sm, IN osm_switch_t * p_sw)
{
	osm_madw_context_t context;
	osm_dr_path_t *p_dr_path;
	osm_physp_t *p_physp;
	osm_node_t *p_node;
	osm_mcast_tbl_t *p_tbl;
	uint32_t block_id_ho;
	uint32_t max_block_id_ho;
	uint32_t position;
	uint32_t max_position;
	uint32_t attr_mod_ho;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_sw);

	p_node = p_sw->p_node;

	CL_ASSERT(osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH);

	if (osm_switch_get_mcast_fwd_tbl_size(p_sw) == 0) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Multicast not supported by switch 0x%016" PRIx64 "\n",
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		goto Exit;
	}

	context.mft_context.node_guid = osm_node_get_node_guid(p_node);
	context.mft_context.set_method = FALSE;

	p_tbl = osm_switch_get_mcast_tbl_ptr(p_sw);
	max_block_id_ho = osm_mcast_tbl_get_max_block(p_tbl);

	if (max_block_id_ho > IB_MCAST_MAX_BLOCK_ID) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3609: "
			"Out-of-range mcast block size = %u on switch 0x%016"
			PRIx64 "\n", max_block_id_ho,
			cl_ntoh64(osm_node_get_node_guid(p_node)));
		goto Exit;
	}

	max_position = osm_mcast_tbl_get_max_position(p_tbl);

	CL_ASSERT(max_position <= IB_MCAST_POSITION_MAX);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Max MFT block = %u, Max position = %u\n", max_block_id_ho,
		max_position);

	p_physp = osm_node_get_physp_ptr(p_node, 0);
	p_dr_path = osm_physp_get_dr_path_ptr(p_physp);

	for (block_id_ho = 0; block_id_ho <= max_block_id_ho; block_id_ho++) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"Retrieving MFT block %u\n", block_id_ho);

		for (position = 0; position <= max_position; position++) {
			OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
				"Retrieving MFT position %u\n", position);

			attr_mod_ho =
			    block_id_ho | position << IB_MCAST_POSITION_SHIFT;
			status =
			    osm_req_get(sm, p_dr_path,
					IB_MAD_ATTR_MCAST_FWD_TBL,
					cl_hton32(attr_mod_ho), TRUE, 0,
					CL_DISP_MSGID_NONE, &context);
			if (status != IB_SUCCESS)
				/* continue the loop despite the error */
				OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3607: "
					"Failure initiating PortInfo request (%s)\n",
					ib_get_err_str(status));
		}
	}

Exit:
	OSM_LOG_EXIT(sm->p_log);
}
#endif

/**********************************************************************
   Lock must be held on entry to this function.
**********************************************************************/
static void si_rcv_process_new(IN osm_sm_t * sm, IN osm_node_t * p_node,
			       IN const osm_madw_t * p_madw)
{
	osm_switch_t *p_sw;
	osm_switch_t *p_check;
	ib_switch_info_t *p_si;
	ib_smp_t *p_smp;
	cl_qmap_t *p_sw_guid_tbl;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_sw_guid_tbl = &sm->p_subn->sw_guid_tbl;
	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_si = ib_smp_get_payload_ptr(p_smp);

	osm_dump_switch_info_v2(sm->p_log, p_si, FILE_ID, OSM_LOG_DEBUG);

	p_sw = osm_switch_new(p_node, p_madw);
	if (p_sw == NULL) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3608: "
			"Unable to allocate new switch object\n");
		goto Exit;
	}

	/* set subnet max mlid to the minimum MulticastFDBCap of all switches */
	if (p_si->mcast_cap &&
	    cl_ntoh16(p_si->mcast_cap) + IB_LID_MCAST_START_HO - 1 <
	    sm->p_subn->max_mcast_lid_ho) {
		sm->p_subn->max_mcast_lid_ho = cl_ntoh16(p_si->mcast_cap) +
			IB_LID_MCAST_START_HO - 1;
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Subnet max multicast lid is 0x%X\n",
			sm->p_subn->max_mcast_lid_ho);
	}

	/* set subnet max unicast lid to the minimum LinearFDBCap of all switches */
	if (cl_ntoh16(p_si->lin_cap) < sm->p_subn->max_ucast_lid_ho) {
		sm->p_subn->max_ucast_lid_ho = cl_ntoh16(p_si->lin_cap);
		OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
			"Subnet max unicast lid is 0x%X\n",
			sm->p_subn->max_ucast_lid_ho);
	}

	p_check = (osm_switch_t *) cl_qmap_insert(p_sw_guid_tbl,
						  osm_node_get_node_guid
						  (p_node), &p_sw->map_item);
	if (p_check != p_sw) {
		/* This shouldn't happen since we hold the lock! */
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3605: "
			"Unable to add new switch object to database\n");
		osm_switch_delete(&p_sw);
		goto Exit;
	}

	p_node->sw = p_sw;

	/* Update the switch info according to the info we just received. */
	osm_switch_set_switch_info(p_sw, p_si);

#if 0
	/* Don't bother retrieving the current unicast and multicast tables
	   from the switches.  The current version of SM does
	   not support silent take-over of an existing multicast
	   configuration.

	   Gathering the multicast tables can also generate large amounts
	   of extra subnet-init traffic.

	   The code to retrieve the tables was fully debugged. */

	si_rcv_get_fwd_tbl(sm, p_sw);
	if (!sm->p_subn->opt.disable_multicast)
		si_rcv_get_mcast_fwd_tbl(sm, p_sw);
#endif

Exit:
	OSM_LOG_EXIT(sm->p_log);
}

/**********************************************************************
   Lock must be held on entry to this function.
   Return 1 if the caller is expected to send a change_detected event.
   this can not be done internally as the event needs the lock...
**********************************************************************/
static boolean_t si_rcv_process_existing(IN osm_sm_t * sm,
					 IN osm_node_t * p_node,
					 IN const osm_madw_t * p_madw)
{
	osm_switch_t *p_sw = p_node->sw;
	ib_switch_info_t *p_si;
	osm_si_context_t *p_si_context;
	ib_smp_t *p_smp;
	osm_epi_lft_change_event_t lft_change;
	boolean_t is_change_detected = FALSE;

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_si = ib_smp_get_payload_ptr(p_smp);
	p_si_context = osm_madw_get_si_context_ptr(p_madw);

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG, "Received logical %cetResp()\n",
		p_si_context->set_method ? 'S' : 'G');

	osm_switch_set_switch_info(p_sw, p_si);

	if (p_si_context->light_sweep == TRUE && !p_si_context->set_method) {
		/* If state changed bit is on the mad was returned with an
		   error - signal a change to the state manager. */
		if (ib_smp_get_status(p_smp) != 0) {
			OSM_LOG(sm->p_log, OSM_LOG_VERBOSE,
				"GetResp() received with error in light sweep. "
				"Commencing heavy sweep\n");
			is_change_detected = TRUE;
		} else if (ib_switch_info_get_state_change(p_si)) {
			osm_dump_switch_info_v2(sm->p_log, p_si, FILE_ID, OSM_LOG_DEBUG);
			is_change_detected = TRUE;
		}
	}

	if (sm->p_subn->first_time_master_sweep == FALSE &&
	    p_si_context->set_method && p_si_context->lft_top_change) {
		lft_change.p_sw = p_sw;
		lft_change.flags = LFT_CHANGED_LFT_TOP;
		lft_change.lft_top = cl_ntoh16(p_si->lin_top);
		lft_change.block_num = 0;
		osm_opensm_report_event(sm->p_subn->p_osm,
					OSM_EVENT_ID_LFT_CHANGE,
					&lft_change);
	}

	OSM_LOG_EXIT(sm->p_log);
	return is_change_detected;
}

static void si_rcv_get_sp0_info(IN osm_sm_t * sm, IN osm_node_t * node)
{
	osm_madw_context_t context;
	osm_physp_t *physp;
	ib_api_status_t status;
	int mlnx_epi_supported = 0;

	physp = osm_node_get_physp_ptr(node, 0);

	context.pi_context.node_guid = osm_node_get_node_guid(node);
	context.pi_context.port_guid = osm_physp_get_port_guid(physp);
	context.pi_context.set_method = FALSE;
	context.pi_context.light_sweep = FALSE;
	context.pi_context.active_transition = FALSE;
	context.pi_context.client_rereg = FALSE;

	status = osm_req_get(sm, osm_physp_get_dr_path_ptr(physp),
			     IB_MAD_ATTR_PORT_INFO, 0, TRUE, 0,
			     CL_DISP_MSGID_NONE, &context);
	if (status != IB_SUCCESS)
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3611: "
			"Failure initiating PortInfo request (%s)\n",
			ib_get_err_str(status));

	if (ib_switch_info_is_enhanced_port0(&node->sw->switch_info) &&
	    sm->p_subn->opt.fdr10) {
		mlnx_epi_supported = is_mlnx_ext_port_info_supported(
						ib_node_info_get_vendor_id(&node->node_info),
						node->node_info.device_id);
		if (mlnx_epi_supported) {
			status = osm_req_get(sm,
					     osm_physp_get_dr_path_ptr(physp),
					     IB_MAD_ATTR_MLNX_EXTENDED_PORT_INFO,
					     0, TRUE, 0,
					     CL_DISP_MSGID_NONE, &context);
			if (status != IB_SUCCESS)
				OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3616: "
					"Failure initiating MLNX ExtPortInfo request (%s)\n",
					ib_get_err_str(status));
		}
	}

}

void osm_si_rcv_process(IN void *context, IN void *data)
{
	osm_sm_t *sm = context;
	osm_madw_t *p_madw = data;
	ib_switch_info_t *p_si;
	ib_smp_t *p_smp;
	osm_node_t *p_node;
	ib_net64_t node_guid;
	osm_si_context_t *p_context;

	CL_ASSERT(sm);

	OSM_LOG_ENTER(sm->p_log);

	CL_ASSERT(p_madw);

	p_smp = osm_madw_get_smp_ptr(p_madw);
	p_si = ib_smp_get_payload_ptr(p_smp);
	p_context = osm_madw_get_si_context_ptr(p_madw);
	node_guid = p_context->node_guid;

	OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
		"Switch GUID 0x%016" PRIx64 ", TID 0x%" PRIx64 "\n",
		cl_ntoh64(node_guid), cl_ntoh64(p_smp->trans_id));

	if (ib_smp_get_status(p_smp)) {
		OSM_LOG(sm->p_log, OSM_LOG_DEBUG,
			"MAD status 0x%x received\n",
			cl_ntoh16(ib_smp_get_status(p_smp)));
		goto Exit2;
	}

	CL_PLOCK_EXCL_ACQUIRE(sm->p_lock);

	p_node = osm_get_node_by_guid(sm->p_subn, node_guid);
	if (!p_node) {
		CL_PLOCK_RELEASE(sm->p_lock);
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3606: "
			"SwitchInfo received for nonexistent node "
			"with GUID 0x%" PRIx64 "\n", cl_ntoh64(node_guid));
		goto Exit;
	}

	/* Hack for bad value in Mellanox switch */
	if (cl_ntoh16(p_si->lin_top) > IB_LID_UCAST_END_HO) {
		OSM_LOG(sm->p_log, OSM_LOG_ERROR, "ERR 3610: "
			"\n\t\t\t\tBad LinearFDBTop value = 0x%X "
			"on switch 0x%" PRIx64
			"\n\t\t\t\tForcing internal correction to 0x%X\n",
			cl_ntoh16(p_si->lin_top),
			cl_ntoh64(osm_node_get_node_guid(p_node)), 0);
		p_si->lin_top = 0;
	}

	/* Acquire the switch object for this switch. */
	if (!p_node->sw) {
		si_rcv_process_new(sm, p_node, p_madw);
		/* A new switch was found during the sweep so we need
		   to ignore the current LFT settings. */
		sm->p_subn->ignore_existing_lfts = TRUE;
	} else if (si_rcv_process_existing(sm, p_node, p_madw))
		/* we might get back a request for signaling change was detected */
		sm->p_subn->force_heavy_sweep = TRUE;

	if (p_context->light_sweep || p_context->set_method)
		goto Exit;

	si_rcv_get_sp0_info(sm, p_node);

Exit:
	CL_PLOCK_RELEASE(sm->p_lock);
Exit2:
	OSM_LOG_EXIT(sm->p_log);
}
