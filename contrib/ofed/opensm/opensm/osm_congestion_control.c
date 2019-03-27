/*
 * Copyright (c) 2006-2009 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2009 HNR Consulting. All rights reserved.
 * Copyright (c) 2012 Lawrence Livermore National Lab.  All rights reserved.
 * Copyright (c) 2014 Mellanox Technologies LTD. All rights reserved.
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
 *    OSM Congestion Control configuration implementation
 *
 * Author:
 *    Albert Chu, LLNL
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdlib.h>
#include <string.h>

#include <iba/ib_types.h>
#include <complib/cl_debug.h>
#include <opensm/osm_file_ids.h>
#define FILE_ID OSM_FILE_CONGESTION_CONTROL_C
#include <opensm/osm_subnet.h>
#include <opensm/osm_opensm.h>
#include <opensm/osm_log.h>
#include <opensm/osm_subnet.h>
#include <opensm/osm_congestion_control.h>

#define CONGESTION_CONTROL_INITIAL_TID_VALUE 0x7A93

static void cc_mad_post(osm_congestion_control_t *p_cc,
			osm_madw_t *p_madw,
			osm_node_t *p_node,
			osm_physp_t *p_physp,
			ib_net16_t attr_id,
			ib_net32_t attr_mod)
{
	osm_subn_opt_t *p_opt = &p_cc->subn->opt;
	ib_cc_mad_t *p_cc_mad;
	uint8_t port;

	OSM_LOG_ENTER(p_cc->log);

	port = osm_physp_get_port_num(p_physp);

	p_cc_mad = osm_madw_get_cc_mad_ptr(p_madw);

	p_cc_mad->header.base_ver = 1;
	p_cc_mad->header.mgmt_class = IB_MCLASS_CC;
	p_cc_mad->header.class_ver = 2;
	p_cc_mad->header.method = IB_MAD_METHOD_SET;
	p_cc_mad->header.status = 0;
	p_cc_mad->header.class_spec = 0;
	p_cc_mad->header.trans_id =
		cl_hton64((uint64_t) cl_atomic_inc(&p_cc->trans_id) &
			  (uint64_t) (0xFFFFFFFF));
	if (p_cc_mad->header.trans_id == 0)
		p_cc_mad->header.trans_id =
			cl_hton64((uint64_t) cl_atomic_inc(&p_cc->trans_id) &
				  (uint64_t) (0xFFFFFFFF));
	p_cc_mad->header.attr_id = attr_id;
	p_cc_mad->header.resv = 0;
	p_cc_mad->header.attr_mod = attr_mod;

	p_cc_mad->cc_key = p_opt->cc_key;

	memset(p_cc_mad->log_data, '\0', IB_CC_LOG_DATA_SIZE);

	p_madw->mad_addr.dest_lid = osm_node_get_base_lid(p_node, port);
	p_madw->mad_addr.addr_type.gsi.remote_qp = IB_QP1;
	p_madw->mad_addr.addr_type.gsi.remote_qkey =
		cl_hton32(IB_QP1_WELL_KNOWN_Q_KEY);
	p_madw->resp_expected = TRUE;
	p_madw->fail_msg = CL_DISP_MSGID_NONE;

	p_madw->context.cc_context.node_guid = osm_node_get_node_guid(p_node);
	p_madw->context.cc_context.port_guid = osm_physp_get_port_guid(p_physp);
	p_madw->context.cc_context.port = port;
	p_madw->context.cc_context.mad_method = IB_MAD_METHOD_SET;
	p_madw->context.cc_context.attr_mod = attr_mod;

	cl_spinlock_acquire(&p_cc->mad_queue_lock);
	cl_atomic_inc(&p_cc->outstanding_mads);
	cl_qlist_insert_tail(&p_cc->mad_queue, &p_madw->list_item);
	cl_spinlock_release(&p_cc->mad_queue_lock);

	cl_event_signal(&p_cc->cc_poller_wakeup);

	OSM_LOG_EXIT(p_cc->log);
}

static void cc_setup_mad_data(osm_sm_t * p_sm)
{
	osm_congestion_control_t *p_cc = &p_sm->p_subn->p_osm->cc;
	osm_subn_opt_t *p_opt = &p_sm->p_subn->opt;
	uint16_t ccti_limit;
	int i;

	/* Switch Congestion Setting */
	p_cc->sw_cong_setting.control_map = p_opt->cc_sw_cong_setting_control_map;

	memcpy(p_cc->sw_cong_setting.victim_mask,
	       p_opt->cc_sw_cong_setting_victim_mask,
	       IB_CC_PORT_MASK_DATA_SIZE);

	memcpy(p_cc->sw_cong_setting.credit_mask,
	       p_opt->cc_sw_cong_setting_credit_mask,
	       IB_CC_PORT_MASK_DATA_SIZE);

	/* threshold is 4 bits, takes up upper nibble of byte */
	p_cc->sw_cong_setting.threshold_resv = (p_opt->cc_sw_cong_setting_threshold << 4);

	p_cc->sw_cong_setting.packet_size = p_opt->cc_sw_cong_setting_packet_size;

	/* cs threshold is 4 bits, takes up upper nibble of short */
	p_cc->sw_cong_setting.cs_threshold_resv =
		cl_hton16(p_opt->cc_sw_cong_setting_credit_starvation_threshold << 12);

	p_cc->sw_cong_setting.cs_return_delay =
		cl_hton16(p_opt->cc_sw_cong_setting_credit_starvation_return_delay.shift << 14
			  | p_opt->cc_sw_cong_setting_credit_starvation_return_delay.multiplier);

	p_cc->sw_cong_setting.marking_rate = p_opt->cc_sw_cong_setting_marking_rate;

	/* CA Congestion Setting */
	p_cc->ca_cong_setting.port_control = p_opt->cc_ca_cong_setting_port_control;
	p_cc->ca_cong_setting.control_map = p_opt->cc_ca_cong_setting_control_map;

	for (i = 0; i < IB_CA_CONG_ENTRY_DATA_SIZE; i++) {
		ib_ca_cong_entry_t *p_entry;

		p_entry = &p_cc->ca_cong_setting.entry_list[i];

		p_entry->ccti_timer = p_opt->cc_ca_cong_entries[i].ccti_timer;
		p_entry->ccti_increase = p_opt->cc_ca_cong_entries[i].ccti_increase;
		p_entry->trigger_threshold = p_opt->cc_ca_cong_entries[i].trigger_threshold;
		p_entry->ccti_min = p_opt->cc_ca_cong_entries[i].ccti_min;
		p_entry->resv0 = 0;
		p_entry->resv1 = 0;
	}

	/* Congestion Control Table */

	/* if no entries, we will always send at least 1 mad to set ccti_limit = 0 */
	if (!p_opt->cc_cct.entries_len)
		p_cc->cc_tbl_mads = 1;
	else {
		p_cc->cc_tbl_mads = p_opt->cc_cct.entries_len - 1;
		p_cc->cc_tbl_mads /= IB_CC_TBL_ENTRY_LIST_MAX;
		p_cc->cc_tbl_mads += 1;
	}

	CL_ASSERT(p_cc->cc_tbl_mads <= OSM_CCT_ENTRY_MAD_BLOCKS);

	if (!p_opt->cc_cct.entries_len)
		ccti_limit = 0;
	else
		ccti_limit = p_opt->cc_cct.entries_len - 1;

	for (i = 0; i < p_cc->cc_tbl_mads; i++) {
		int j;

		p_cc->cc_tbl[i].ccti_limit = cl_hton16(ccti_limit);
		p_cc->cc_tbl[i].resv = 0;

		memset(p_cc->cc_tbl[i].entry_list,
		       '\0',
		       sizeof(p_cc->cc_tbl[i].entry_list));

		if (!ccti_limit)
			break;

		for (j = 0; j < IB_CC_TBL_ENTRY_LIST_MAX; j++) {
			int k;

			k = (i * IB_CC_TBL_ENTRY_LIST_MAX) + j;
			p_cc->cc_tbl[i].entry_list[j].shift_multiplier =
				cl_hton16(p_opt->cc_cct.entries[k].shift << 14
					  | p_opt->cc_cct.entries[k].multiplier);
		}
	}
}

static ib_api_status_t cc_send_sw_cong_setting(osm_sm_t * p_sm,
					       osm_node_t *p_node)
{
	osm_congestion_control_t *p_cc = &p_sm->p_subn->p_osm->cc;
	unsigned force_update;
	osm_physp_t *p_physp;
	osm_madw_t *p_madw = NULL;
	ib_cc_mad_t *p_cc_mad = NULL;
	ib_sw_cong_setting_t *p_sw_cong_setting = NULL;

	OSM_LOG_ENTER(p_sm->p_log);

	p_physp = osm_node_get_physp_ptr(p_node, 0);

	force_update = p_physp->need_update || p_sm->p_subn->need_update;

	if (!force_update
	    && !memcmp(&p_cc->sw_cong_setting,
		       &p_physp->cc.sw.sw_cong_setting,
		       sizeof(p_cc->sw_cong_setting)))
		return IB_SUCCESS;

	p_madw = osm_mad_pool_get(p_cc->mad_pool, p_cc->bind_handle,
				  MAD_BLOCK_SIZE, NULL);
	if (p_madw == NULL) {
		OSM_LOG(p_sm->p_log, OSM_LOG_ERROR, "ERR C101: "
			"failed to allocate mad\n");
		return IB_INSUFFICIENT_MEMORY;
	}

	p_cc_mad = osm_madw_get_cc_mad_ptr(p_madw);

	p_sw_cong_setting = ib_cc_mad_get_mgt_data_ptr(p_cc_mad);

	memcpy(p_sw_cong_setting,
	       &p_cc->sw_cong_setting,
	       sizeof(p_cc->sw_cong_setting));

	cc_mad_post(p_cc, p_madw, p_node, p_physp,
		    IB_MAD_ATTR_SW_CONG_SETTING, 0);

	OSM_LOG_EXIT(p_sm->p_log);

	return IB_SUCCESS;
}

static ib_api_status_t cc_send_ca_cong_setting(osm_sm_t * p_sm,
					       osm_node_t *p_node,
					       osm_physp_t *p_physp)
{
	osm_congestion_control_t *p_cc = &p_sm->p_subn->p_osm->cc;
	unsigned force_update;
	osm_madw_t *p_madw = NULL;
	ib_cc_mad_t *p_cc_mad = NULL;
	ib_ca_cong_setting_t *p_ca_cong_setting = NULL;

	OSM_LOG_ENTER(p_sm->p_log);

	force_update = p_physp->need_update || p_sm->p_subn->need_update;

	if (!force_update
	    && !memcmp(&p_cc->ca_cong_setting,
		       &p_physp->cc.ca.ca_cong_setting,
		       sizeof(p_cc->ca_cong_setting)))
		return IB_SUCCESS;

	p_madw = osm_mad_pool_get(p_cc->mad_pool, p_cc->bind_handle,
				  MAD_BLOCK_SIZE, NULL);
	if (p_madw == NULL) {
		OSM_LOG(p_sm->p_log, OSM_LOG_ERROR, "ERR C102: "
			"failed to allocate mad\n");
		return IB_INSUFFICIENT_MEMORY;
	}

	p_cc_mad = osm_madw_get_cc_mad_ptr(p_madw);

	p_ca_cong_setting = ib_cc_mad_get_mgt_data_ptr(p_cc_mad);

	memcpy(p_ca_cong_setting,
	       &p_cc->ca_cong_setting,
	       sizeof(p_cc->ca_cong_setting));

	cc_mad_post(p_cc, p_madw, p_node, p_physp,
		    IB_MAD_ATTR_CA_CONG_SETTING, 0);

	OSM_LOG_EXIT(p_sm->p_log);

	return IB_SUCCESS;
}

static ib_api_status_t cc_send_cct(osm_sm_t * p_sm,
				   osm_node_t *p_node,
				   osm_physp_t *p_physp)
{
	osm_congestion_control_t *p_cc = &p_sm->p_subn->p_osm->cc;
	unsigned force_update;
	osm_madw_t *p_madw = NULL;
	ib_cc_mad_t *p_cc_mad = NULL;
	ib_cc_tbl_t *p_cc_tbl = NULL;
	unsigned int index = 0;

	OSM_LOG_ENTER(p_sm->p_log);

	force_update = p_physp->need_update || p_sm->p_subn->need_update;

	for (index = 0; index < p_cc->cc_tbl_mads; index++) {
		if (!force_update
		    && !memcmp(&p_cc->cc_tbl[index],
			       &p_physp->cc.ca.cc_tbl[index],
			       sizeof(p_cc->cc_tbl[index])))
			continue;

		p_madw = osm_mad_pool_get(p_cc->mad_pool, p_cc->bind_handle,
					  MAD_BLOCK_SIZE, NULL);
		if (p_madw == NULL) {
			OSM_LOG(p_sm->p_log, OSM_LOG_ERROR, "ERR C103: "
				"failed to allocate mad\n");
			return IB_INSUFFICIENT_MEMORY;
		}

		p_cc_mad = osm_madw_get_cc_mad_ptr(p_madw);

		p_cc_tbl = (ib_cc_tbl_t *)ib_cc_mad_get_mgt_data_ptr(p_cc_mad);

		memcpy(p_cc_tbl,
		       &p_cc->cc_tbl[index],
		       sizeof(p_cc->cc_tbl[index]));

		cc_mad_post(p_cc, p_madw, p_node, p_physp,
			    IB_MAD_ATTR_CC_TBL, cl_hton32(index));
	}

	OSM_LOG_EXIT(p_sm->p_log);

	return IB_SUCCESS;
}

int osm_congestion_control_setup(struct osm_opensm *p_osm)
{
	cl_qmap_t *p_tbl;
	cl_map_item_t *p_next;
	int ret = 0;

	if (!p_osm->subn.opt.congestion_control)
		return 0;

	OSM_LOG_ENTER(&p_osm->log);

	/*
	 * Do nothing unless the most recent routing attempt was successful.
	 */
	if (!p_osm->routing_engine_used)
		return 0;

	cc_setup_mad_data(&p_osm->sm);

	cl_plock_acquire(&p_osm->lock);

	p_tbl = &p_osm->subn.port_guid_tbl;
	p_next = cl_qmap_head(p_tbl);
	while (p_next != cl_qmap_end(p_tbl)) {
		osm_port_t *p_port = (osm_port_t *) p_next;
		osm_node_t *p_node = p_port->p_node;
		ib_api_status_t status;

		p_next = cl_qmap_next(p_next);

		if (p_port->cc_unavailable_flag)
			continue;

		if (osm_node_get_type(p_node) == IB_NODE_TYPE_SWITCH) {
			status = cc_send_sw_cong_setting(&p_osm->sm, p_node);
			if (status != IB_SUCCESS)
				ret = -1;
		} else if (osm_node_get_type(p_node) == IB_NODE_TYPE_CA) {
			status = cc_send_ca_cong_setting(&p_osm->sm,
							 p_node,
							 p_port->p_physp);
			if (status != IB_SUCCESS)
				ret = -1;

			status = cc_send_cct(&p_osm->sm,
					     p_node,
					     p_port->p_physp);
			if (status != IB_SUCCESS)
				ret = -1;
		}
	}

	cl_plock_release(&p_osm->lock);

	OSM_LOG_EXIT(&p_osm->log);

	return ret;
}

int osm_congestion_control_wait_pending_transactions(struct osm_opensm *p_osm)
{
	osm_congestion_control_t *cc = &p_osm->cc;

	if (!p_osm->subn.opt.congestion_control)
		return 0;

	while (1) {
		unsigned count = cc->outstanding_mads;
		if (!count || osm_exit_flag)
			break;
		cl_event_wait_on(&cc->outstanding_mads_done_event,
				 EVENT_NO_TIMEOUT,
				 TRUE);
	}

	return osm_exit_flag;
}

static inline void decrement_outstanding_mads(osm_congestion_control_t *p_cc)
{
	uint32_t outstanding;

	outstanding = cl_atomic_dec(&p_cc->outstanding_mads);
	if (!outstanding)
		cl_event_signal(&p_cc->outstanding_mads_done_event);

	cl_atomic_dec(&p_cc->outstanding_mads_on_wire);
	cl_event_signal(&p_cc->sig_mads_on_wire_continue);
}

static void cc_rcv_mad(void *context, void *data)
{
	osm_congestion_control_t *p_cc = context;
	osm_opensm_t *p_osm = p_cc->osm;
	osm_madw_t *p_madw = data;
	ib_cc_mad_t *p_cc_mad;
	osm_madw_context_t *p_mad_context = &p_madw->context;
	ib_mad_t *p_mad = osm_madw_get_mad_ptr(p_madw);
	ib_net64_t node_guid = p_mad_context->cc_context.node_guid;
	ib_net64_t port_guid = p_mad_context->cc_context.port_guid;
	uint8_t port = p_mad_context->cc_context.port;
	osm_port_t *p_port;

	OSM_LOG_ENTER(p_cc->log);

	OSM_LOG(p_cc->log, OSM_LOG_VERBOSE,
		"Processing received MAD status 0x%x for "
		"attr ID %u mod 0x%x node 0x%" PRIx64 " port %u\n",
		cl_ntoh16(p_mad->status), cl_ntoh16(p_mad->attr_id),
		cl_ntoh32(p_mad_context->cc_context.attr_mod),
		cl_ntoh64(node_guid), port);

	p_cc_mad = osm_madw_get_cc_mad_ptr(p_madw);

	cl_plock_acquire(&p_osm->lock);

	p_port = osm_get_port_by_guid(p_cc->subn, port_guid);
	if (!p_port) {
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C109: "
			"Port GUID 0x%" PRIx64 " not in table\n",
			cl_ntoh64(port_guid));
		cl_plock_release(&p_osm->lock);
		goto Exit;
	}

	p_port->cc_timeout_count = 0;

	if (p_cc_mad->header.status) {
		if (p_cc_mad->header.status & IB_MAD_STATUS_UNSUP_CLASS_VER
		    || p_cc_mad->header.status & IB_MAD_STATUS_UNSUP_METHOD
		    || p_cc_mad->header.status & IB_MAD_STATUS_UNSUP_METHOD_ATTR)
			p_port->cc_unavailable_flag = TRUE;
		cl_plock_release(&p_osm->lock);
		goto Exit;
	}
	else
		p_port->cc_unavailable_flag = FALSE;

	if (p_cc_mad->header.attr_id == IB_MAD_ATTR_SW_CONG_SETTING) {
		ib_sw_cong_setting_t *p_sw_cong_setting;

		p_sw_cong_setting = ib_cc_mad_get_mgt_data_ptr(p_cc_mad);
		p_port->p_physp->cc.sw.sw_cong_setting = *p_sw_cong_setting;
	}
	else if (p_cc_mad->header.attr_id == IB_MAD_ATTR_CA_CONG_SETTING) {
		ib_ca_cong_setting_t *p_ca_cong_setting;

		p_ca_cong_setting = ib_cc_mad_get_mgt_data_ptr(p_cc_mad);
		p_port->p_physp->cc.ca.ca_cong_setting = *p_ca_cong_setting;
	}
	else if (p_cc_mad->header.attr_id == IB_MAD_ATTR_CC_TBL) {
		ib_net32_t attr_mod = p_mad_context->cc_context.attr_mod;
		uint32_t index = cl_ntoh32(attr_mod);
		ib_cc_tbl_t *p_cc_tbl;

		p_cc_tbl = ib_cc_mad_get_mgt_data_ptr(p_cc_mad);
		p_port->p_physp->cc.ca.cc_tbl[index] = *p_cc_tbl;
	}
	else
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C10A: "
			"Unexpected MAD attribute ID %u received\n",
			cl_ntoh16(p_cc_mad->header.attr_id));

	cl_plock_release(&p_osm->lock);

Exit:
	decrement_outstanding_mads(p_cc);
	osm_mad_pool_put(p_cc->mad_pool, p_madw);
	OSM_LOG_EXIT(p_cc->log);
}

static void cc_poller_send(osm_congestion_control_t *p_cc,
			   osm_madw_t *p_madw)
{
	osm_subn_opt_t *p_opt = &p_cc->subn->opt;
	ib_api_status_t status;
	cl_status_t sts;
	osm_madw_context_t mad_context = p_madw->context;

	status = osm_vendor_send(p_cc->bind_handle, p_madw, TRUE);
	if (status == IB_SUCCESS) {
		cl_atomic_inc(&p_cc->outstanding_mads_on_wire);
		while (p_cc->outstanding_mads_on_wire >
		       (int32_t)p_opt->cc_max_outstanding_mads) {
wait:
			sts = cl_event_wait_on(&p_cc->sig_mads_on_wire_continue,
					       EVENT_NO_TIMEOUT, TRUE);
			if (sts != CL_SUCCESS)
				goto wait;
		}
	} else
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C104: "
			"send failed to node 0x%" PRIx64 "port %u\n",
			cl_ntoh64(mad_context.cc_context.node_guid),
			mad_context.cc_context.port);
}

static void cc_poller(void *p_ptr)
{
	osm_congestion_control_t *p_cc = p_ptr;
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_cc->log);

	if (p_cc->thread_state == OSM_THREAD_STATE_NONE)
		p_cc->thread_state = OSM_THREAD_STATE_RUN;

	while (p_cc->thread_state == OSM_THREAD_STATE_RUN) {
		cl_spinlock_acquire(&p_cc->mad_queue_lock);

		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_cc->mad_queue);

		cl_spinlock_release(&p_cc->mad_queue_lock);

		if (p_madw != (osm_madw_t *) cl_qlist_end(&p_cc->mad_queue))
			cc_poller_send(p_cc, p_madw);
		else
			cl_event_wait_on(&p_cc->cc_poller_wakeup,
					 EVENT_NO_TIMEOUT, TRUE);
	}

	OSM_LOG_EXIT(p_cc->log);
}

ib_api_status_t osm_congestion_control_init(osm_congestion_control_t * p_cc,
					    struct osm_opensm *p_osm,
					    const osm_subn_opt_t * p_opt)
{
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(&p_osm->log);

	memset(p_cc, 0, sizeof(*p_cc));

	p_cc->osm = p_osm;
	p_cc->subn = &p_osm->subn;
	p_cc->sm = &p_osm->sm;
	p_cc->log = &p_osm->log;
	p_cc->mad_pool = &p_osm->mad_pool;
	p_cc->trans_id = CONGESTION_CONTROL_INITIAL_TID_VALUE;
	p_cc->vendor = p_osm->p_vendor;

	p_cc->cc_disp_h = cl_disp_register(&p_osm->disp, OSM_MSG_MAD_CC,
					   cc_rcv_mad, p_cc);
	if (p_cc->cc_disp_h == CL_DISP_INVALID_HANDLE)
		goto Exit;

	cl_qlist_init(&p_cc->mad_queue);

	status = cl_spinlock_init(&p_cc->mad_queue_lock);
	if (status != IB_SUCCESS)
		goto Exit;

	cl_event_construct(&p_cc->cc_poller_wakeup);
	status = cl_event_init(&p_cc->cc_poller_wakeup, FALSE);
	if (status != IB_SUCCESS)
		goto Exit;

	cl_event_construct(&p_cc->outstanding_mads_done_event);
	status = cl_event_init(&p_cc->outstanding_mads_done_event, FALSE);
	if (status != IB_SUCCESS)
		goto Exit;

	cl_event_construct(&p_cc->sig_mads_on_wire_continue);
	status = cl_event_init(&p_cc->sig_mads_on_wire_continue, FALSE);
	if (status != IB_SUCCESS)
		goto Exit;

	p_cc->thread_state = OSM_THREAD_STATE_NONE;

	status = cl_thread_init(&p_cc->cc_poller, cc_poller, p_cc,
				"cc poller");
	if (status != IB_SUCCESS)
		goto Exit;

	status = IB_SUCCESS;
Exit:
	OSM_LOG_EXIT(p_cc->log);
	return status;
}

static void cc_mad_recv_callback(osm_madw_t * p_madw, void *bind_context,
				 osm_madw_t * p_req_madw)
{
	osm_congestion_control_t *p_cc = bind_context;

	OSM_LOG_ENTER(p_cc->log);

	CL_ASSERT(p_madw);

	/* HACK - should be extended when supporting CC traps */
	CL_ASSERT(p_req_madw != NULL);

	osm_madw_copy_context(p_madw, p_req_madw);
	osm_mad_pool_put(p_cc->mad_pool, p_req_madw);

	/* Do not decrement outstanding mads here, do it in the dispatcher */

	if (cl_disp_post(p_cc->cc_disp_h, OSM_MSG_MAD_CC,
			 p_madw, NULL, NULL) != CL_SUCCESS) {
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C105: "
			"Congestion Control Dispatcher post failed\n");
		osm_mad_pool_put(p_cc->mad_pool, p_madw);
	}

	OSM_LOG_EXIT(p_cc->log);
}

static void cc_mad_send_err_callback(void *bind_context,
				     osm_madw_t * p_madw)
{
	osm_congestion_control_t *p_cc = bind_context;
	osm_madw_context_t *p_madw_context = &p_madw->context;
	osm_opensm_t *p_osm = p_cc->osm;
	uint64_t node_guid = p_madw_context->cc_context.node_guid;
	uint64_t port_guid = p_madw_context->cc_context.port_guid;
	uint8_t port = p_madw_context->cc_context.port;
	osm_port_t *p_port;
	int log_flag = 1;

	OSM_LOG_ENTER(p_cc->log);

	cl_plock_acquire(&p_osm->lock);

	p_port = osm_get_port_by_guid(p_cc->subn, port_guid);
	if (!p_port) {
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C10B: "
			"Port GUID 0x%" PRIx64 " not in table\n",
			cl_ntoh64(port_guid));
		cl_plock_release(&p_osm->lock);
		goto Exit;
	}

	/* If timed out before, don't bothering logging again
	 * we assume no CC support
	 */
	if (p_madw->status == IB_TIMEOUT
	    && p_port->cc_timeout_count)
		log_flag = 0;

	if (log_flag)
		OSM_LOG(p_cc->log, OSM_LOG_ERROR, "ERR C106: MAD Error (%s): "
			"attr id = %u LID %u GUID 0x%016" PRIx64 " port %u "
			"TID 0x%" PRIx64 "\n",
			ib_get_err_str(p_madw->status),
			p_madw->p_mad->attr_id,
			cl_ntoh16(p_madw->mad_addr.dest_lid),
			cl_ntoh64(node_guid),
			port,
			cl_ntoh64(p_madw->p_mad->trans_id));

	if (p_madw->status == IB_TIMEOUT) {
		p_port->cc_timeout_count++;
		if (p_port->cc_timeout_count > OSM_CC_TIMEOUT_COUNT_THRESHOLD
		    && !p_port->cc_unavailable_flag) {
			p_port->cc_unavailable_flag = TRUE;
			p_port->cc_timeout_count = 0;
		}
	} else
		p_cc->subn->subnet_initialization_error = TRUE;

	cl_plock_release(&p_osm->lock);

Exit:
	osm_mad_pool_put(p_cc->mad_pool, p_madw);

	decrement_outstanding_mads(p_cc);

	OSM_LOG_EXIT(p_cc->log);
}

ib_api_status_t osm_congestion_control_bind(osm_congestion_control_t * p_cc,
					    ib_net64_t port_guid)
{
	osm_bind_info_t bind_info;
	ib_api_status_t status = IB_SUCCESS;

	OSM_LOG_ENTER(p_cc->log);

	bind_info.port_guid = p_cc->port_guid = port_guid;
	bind_info.mad_class = IB_MCLASS_CC;
	bind_info.class_version = 2;
	bind_info.is_responder = FALSE;
	bind_info.is_report_processor = FALSE;
	bind_info.is_trap_processor = FALSE;
	bind_info.recv_q_size = OSM_SM_DEFAULT_QP1_RCV_SIZE;
	bind_info.send_q_size = OSM_SM_DEFAULT_QP1_SEND_SIZE;
	bind_info.timeout = p_cc->subn->opt.transaction_timeout;
	bind_info.retries = p_cc->subn->opt.transaction_retries;

	OSM_LOG(p_cc->log, OSM_LOG_VERBOSE,
		"Binding to port GUID 0x%" PRIx64 "\n", cl_ntoh64(port_guid));

	p_cc->bind_handle = osm_vendor_bind(p_cc->vendor, &bind_info,
					    p_cc->mad_pool,
					    cc_mad_recv_callback,
					    cc_mad_send_err_callback, p_cc);

	if (p_cc->bind_handle == OSM_BIND_INVALID_HANDLE) {
		status = IB_ERROR;
		OSM_LOG(p_cc->log, OSM_LOG_ERROR,
			"ERR C107: Vendor specific bind failed (%s)\n",
			ib_get_err_str(status));
		goto Exit;
	}

Exit:
	OSM_LOG_EXIT(p_cc->log);
	return status;
}

void osm_congestion_control_shutdown(osm_congestion_control_t * p_cc)
{
	OSM_LOG_ENTER(p_cc->log);
	if (p_cc->bind_handle == OSM_BIND_INVALID_HANDLE) {
		OSM_LOG(p_cc->log, OSM_LOG_ERROR,
			"ERR C108: No previous bind\n");
		goto Exit;
	}
	cl_disp_unregister(p_cc->cc_disp_h);
Exit:
	OSM_LOG_EXIT(p_cc->log);
}

void osm_congestion_control_destroy(osm_congestion_control_t * p_cc)
{
	osm_madw_t *p_madw;

	OSM_LOG_ENTER(p_cc->log);

	p_cc->thread_state = OSM_THREAD_STATE_EXIT;

	cl_event_signal(&p_cc->sig_mads_on_wire_continue);
	cl_event_signal(&p_cc->cc_poller_wakeup);

	cl_thread_destroy(&p_cc->cc_poller);

	cl_spinlock_acquire(&p_cc->mad_queue_lock);

	while (!cl_is_qlist_empty(&p_cc->mad_queue)) {
		p_madw = (osm_madw_t *) cl_qlist_remove_head(&p_cc->mad_queue);
		osm_mad_pool_put(p_cc->mad_pool, p_madw);
	}

	cl_spinlock_release(&p_cc->mad_queue_lock);

	cl_spinlock_destroy(&p_cc->mad_queue_lock);

	cl_event_destroy(&p_cc->cc_poller_wakeup);
	cl_event_destroy(&p_cc->outstanding_mads_done_event);
	cl_event_destroy(&p_cc->sig_mads_on_wire_continue);

	OSM_LOG_EXIT(p_cc->log);
}
