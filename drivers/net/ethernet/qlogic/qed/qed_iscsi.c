// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_iscsi_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_iscsi.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include "qed_reg_addr.h"

struct qed_iscsi_conn {
	struct list_head list_entry;
	bool free_on_delete;

	u16 conn_id;
	u32 icid;
	u32 fw_cid;

	u8 layer_code;
	u8 offl_flags;
	u8 connect_mode;
	u32 initial_ack;
	dma_addr_t sq_pbl_addr;
	struct qed_chain r2tq;
	struct qed_chain xhq;
	struct qed_chain uhq;

	struct tcp_upload_params *tcp_upload_params_virt_addr;
	dma_addr_t tcp_upload_params_phys_addr;
	struct scsi_terminate_extra_params *queue_cnts_virt_addr;
	dma_addr_t queue_cnts_phys_addr;
	dma_addr_t syn_phy_addr;

	u16 syn_ip_payload_length;
	u8 local_mac[6];
	u8 remote_mac[6];
	u16 vlan_id;
	u16 tcp_flags;
	u8 ip_version;
	u32 remote_ip[4];
	u32 local_ip[4];
	u8 ka_max_probe_cnt;
	u8 dup_ack_theshold;
	u32 rcv_next;
	u32 snd_una;
	u32 snd_next;
	u32 snd_max;
	u32 snd_wnd;
	u32 rcv_wnd;
	u32 snd_wl1;
	u32 cwnd;
	u32 ss_thresh;
	u16 srtt;
	u16 rtt_var;
	u32 ts_recent;
	u32 ts_recent_age;
	u32 total_rt;
	u32 ka_timeout_delta;
	u32 rt_timeout_delta;
	u8 dup_ack_cnt;
	u8 snd_wnd_probe_cnt;
	u8 ka_probe_cnt;
	u8 rt_cnt;
	u32 flow_label;
	u32 ka_timeout;
	u32 ka_interval;
	u32 max_rt_time;
	u32 initial_rcv_wnd;
	u8 ttl;
	u8 tos_or_tc;
	u16 remote_port;
	u16 local_port;
	u16 mss;
	u8 snd_wnd_scale;
	u8 rcv_wnd_scale;
	u16 da_timeout_value;
	u8 ack_frequency;

	u8 update_flag;
	u8 default_cq;
	u32 max_seq_size;
	u32 max_recv_pdu_length;
	u32 max_send_pdu_length;
	u32 first_seq_length;
	u32 exp_stat_sn;
	u32 stat_sn;
	u16 physical_q0;
	u16 physical_q1;
	u8 abortive_dsconnect;
};

static int qed_iscsi_async_event(struct qed_hwfn *p_hwfn, u8 fw_event_code,
				 __le16 echo, union event_ring_data *data,
				 u8 fw_return_code)
{
	if (p_hwfn->p_iscsi_info->event_cb) {
		struct qed_iscsi_info *p_iscsi = p_hwfn->p_iscsi_info;

		return p_iscsi->event_cb(p_iscsi->event_context,
					 fw_event_code, data);
	} else {
		DP_NOTICE(p_hwfn, "iSCSI async completion is not set\n");
		return -EINVAL;
	}
}

static int
qed_sp_iscsi_func_start(struct qed_hwfn *p_hwfn,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_addr,
			void *event_context, iscsi_event_cb_t async_event_cb)
{
	struct iscsi_init_ramrod_params *p_ramrod = NULL;
	struct scsi_init_func_queues *p_queue = NULL;
	struct qed_iscsi_pf_params *p_params = NULL;
	struct iscsi_spe_func_init *p_init = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = 0;
	u32 dval;
	u16 val;
	u8 i;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_INIT_FUNC,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iscsi_init;
	p_init = &p_ramrod->iscsi_init_spe;
	p_params = &p_hwfn->pf_params.iscsi_pf_params;
	p_queue = &p_init->q_params;

	/* Sanity */
	if (p_params->num_queues > p_hwfn->hw_info.feat_num[QED_ISCSI_CQ]) {
		DP_ERR(p_hwfn,
		       "Cannot satisfy CQ amount. Queues requested %d, CQs available %d. Aborting function start\n",
		       p_params->num_queues,
		       p_hwfn->hw_info.feat_num[QED_ISCSI_CQ]);
		qed_sp_destroy_request(p_hwfn, p_ent);
		return -EINVAL;
	}

	val = p_params->half_way_close_timeout;
	p_init->half_way_close_timeout = cpu_to_le16(val);
	p_init->num_sq_pages_in_ring = p_params->num_sq_pages_in_ring;
	p_init->num_r2tq_pages_in_ring = p_params->num_r2tq_pages_in_ring;
	p_init->num_uhq_pages_in_ring = p_params->num_uhq_pages_in_ring;
	p_init->ll2_rx_queue_id =
	    p_hwfn->hw_info.resc_start[QED_LL2_RAM_QUEUE] +
	    p_params->ll2_ooo_queue_id;

	p_init->func_params.log_page_size = p_params->log_page_size;
	val = p_params->num_tasks;
	p_init->func_params.num_tasks = cpu_to_le16(val);
	p_init->debug_mode.flags = p_params->debug_mode;

	DMA_REGPAIR_LE(p_queue->glbl_q_params_addr,
		       p_params->glbl_q_params_addr);

	val = p_params->cq_num_entries;
	p_queue->cq_num_entries = cpu_to_le16(val);
	val = p_params->cmdq_num_entries;
	p_queue->cmdq_num_entries = cpu_to_le16(val);
	p_queue->num_queues = p_params->num_queues;
	dval = (u8)p_hwfn->hw_info.resc_start[QED_CMDQS_CQS];
	p_queue->queue_relative_offset = (u8)dval;
	p_queue->cq_sb_pi = p_params->gl_rq_pi;
	p_queue->cmdq_sb_pi = p_params->gl_cmd_pi;

	for (i = 0; i < p_params->num_queues; i++) {
		val = qed_get_igu_sb_id(p_hwfn, i);
		p_queue->cq_cmdq_sb_num_arr[i] = cpu_to_le16(val);
	}

	p_queue->bdq_resource_id = (u8)RESC_START(p_hwfn, QED_BDQ);

	DMA_REGPAIR_LE(p_queue->bdq_pbl_base_address[BDQ_ID_RQ],
		       p_params->bdq_pbl_base_addr[BDQ_ID_RQ]);
	p_queue->bdq_pbl_num_entries[BDQ_ID_RQ] =
	    p_params->bdq_pbl_num_entries[BDQ_ID_RQ];
	val = p_params->bdq_xoff_threshold[BDQ_ID_RQ];
	p_queue->bdq_xoff_threshold[BDQ_ID_RQ] = cpu_to_le16(val);
	val = p_params->bdq_xon_threshold[BDQ_ID_RQ];
	p_queue->bdq_xon_threshold[BDQ_ID_RQ] = cpu_to_le16(val);

	DMA_REGPAIR_LE(p_queue->bdq_pbl_base_address[BDQ_ID_IMM_DATA],
		       p_params->bdq_pbl_base_addr[BDQ_ID_IMM_DATA]);
	p_queue->bdq_pbl_num_entries[BDQ_ID_IMM_DATA] =
	    p_params->bdq_pbl_num_entries[BDQ_ID_IMM_DATA];
	val = p_params->bdq_xoff_threshold[BDQ_ID_IMM_DATA];
	p_queue->bdq_xoff_threshold[BDQ_ID_IMM_DATA] = cpu_to_le16(val);
	val = p_params->bdq_xon_threshold[BDQ_ID_IMM_DATA];
	p_queue->bdq_xon_threshold[BDQ_ID_IMM_DATA] = cpu_to_le16(val);
	val = p_params->rq_buffer_size;
	p_queue->rq_buffer_size = cpu_to_le16(val);
	if (p_params->is_target) {
		SET_FIELD(p_queue->q_validity,
			  SCSI_INIT_FUNC_QUEUES_RQ_VALID, 1);
		if (p_queue->bdq_pbl_num_entries[BDQ_ID_IMM_DATA])
			SET_FIELD(p_queue->q_validity,
				  SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID, 1);
		SET_FIELD(p_queue->q_validity,
			  SCSI_INIT_FUNC_QUEUES_CMD_VALID, 1);
	} else {
		SET_FIELD(p_queue->q_validity,
			  SCSI_INIT_FUNC_QUEUES_RQ_VALID, 1);
	}
	p_ramrod->tcp_init.two_msl_timer = cpu_to_le32(p_params->two_msl_timer);
	val = p_params->tx_sws_timer;
	p_ramrod->tcp_init.tx_sws_timer = cpu_to_le16(val);
	p_ramrod->tcp_init.max_fin_rt = p_params->max_fin_rt;

	p_hwfn->p_iscsi_info->event_context = event_context;
	p_hwfn->p_iscsi_info->event_cb = async_event_cb;

	qed_spq_register_async_cb(p_hwfn, PROTOCOLID_ISCSI,
				  qed_iscsi_async_event);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_iscsi_conn_offload(struct qed_hwfn *p_hwfn,
				     struct qed_iscsi_conn *p_conn,
				     enum spq_mode comp_mode,
				     struct qed_spq_comp_cb *p_comp_addr)
{
	struct iscsi_spe_conn_offload *p_ramrod = NULL;
	struct tcp_offload_params_opt2 *p_tcp2 = NULL;
	struct tcp_offload_params *p_tcp = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	dma_addr_t r2tq_pbl_addr;
	dma_addr_t xhq_pbl_addr;
	dma_addr_t uhq_pbl_addr;
	u16 physical_q;
	__le16 tmp;
	int rc = 0;
	u32 dval;
	u16 wval;
	u16 *p;
	u8 i;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_OFFLOAD_CONN,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iscsi_conn_offload;

	/* Transmission PQ is the first of the PF */
	physical_q = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_conn->physical_q0 = physical_q;
	p_ramrod->iscsi.physical_q0 = cpu_to_le16(physical_q);

	/* iSCSI Pure-ACK PQ */
	physical_q = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_ACK);
	p_conn->physical_q1 = physical_q;
	p_ramrod->iscsi.physical_q1 = cpu_to_le16(physical_q);

	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);

	DMA_REGPAIR_LE(p_ramrod->iscsi.sq_pbl_addr, p_conn->sq_pbl_addr);

	r2tq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->r2tq);
	DMA_REGPAIR_LE(p_ramrod->iscsi.r2tq_pbl_addr, r2tq_pbl_addr);

	xhq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->xhq);
	DMA_REGPAIR_LE(p_ramrod->iscsi.xhq_pbl_addr, xhq_pbl_addr);

	uhq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->uhq);
	DMA_REGPAIR_LE(p_ramrod->iscsi.uhq_pbl_addr, uhq_pbl_addr);

	p_ramrod->iscsi.initial_ack = cpu_to_le32(p_conn->initial_ack);
	p_ramrod->iscsi.flags = p_conn->offl_flags;
	p_ramrod->iscsi.default_cq = p_conn->default_cq;
	p_ramrod->iscsi.stat_sn = cpu_to_le32(p_conn->stat_sn);

	if (!GET_FIELD(p_ramrod->iscsi.flags,
		       ISCSI_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B)) {
		p_tcp = &p_ramrod->tcp;

		p = (u16 *)p_conn->local_mac;
		tmp = cpu_to_le16(get_unaligned_be16(p));
		p_tcp->local_mac_addr_hi = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 1));
		p_tcp->local_mac_addr_mid = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 2));
		p_tcp->local_mac_addr_lo = tmp;

		p = (u16 *)p_conn->remote_mac;
		tmp = cpu_to_le16(get_unaligned_be16(p));
		p_tcp->remote_mac_addr_hi = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 1));
		p_tcp->remote_mac_addr_mid = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 2));
		p_tcp->remote_mac_addr_lo = tmp;

		p_tcp->vlan_id = cpu_to_le16(p_conn->vlan_id);

		p_tcp->flags = cpu_to_le16(p_conn->tcp_flags);
		p_tcp->ip_version = p_conn->ip_version;
		for (i = 0; i < 4; i++) {
			dval = p_conn->remote_ip[i];
			p_tcp->remote_ip[i] = cpu_to_le32(dval);
			dval = p_conn->local_ip[i];
			p_tcp->local_ip[i] = cpu_to_le32(dval);
		}
		p_tcp->ka_max_probe_cnt = p_conn->ka_max_probe_cnt;
		p_tcp->dup_ack_theshold = p_conn->dup_ack_theshold;

		p_tcp->rcv_next = cpu_to_le32(p_conn->rcv_next);
		p_tcp->snd_una = cpu_to_le32(p_conn->snd_una);
		p_tcp->snd_next = cpu_to_le32(p_conn->snd_next);
		p_tcp->snd_max = cpu_to_le32(p_conn->snd_max);
		p_tcp->snd_wnd = cpu_to_le32(p_conn->snd_wnd);
		p_tcp->rcv_wnd = cpu_to_le32(p_conn->rcv_wnd);
		p_tcp->snd_wl1 = cpu_to_le32(p_conn->snd_wl1);
		p_tcp->cwnd = cpu_to_le32(p_conn->cwnd);
		p_tcp->ss_thresh = cpu_to_le32(p_conn->ss_thresh);
		p_tcp->srtt = cpu_to_le16(p_conn->srtt);
		p_tcp->rtt_var = cpu_to_le16(p_conn->rtt_var);
		p_tcp->ts_recent = cpu_to_le32(p_conn->ts_recent);
		p_tcp->ts_recent_age = cpu_to_le32(p_conn->ts_recent_age);
		p_tcp->total_rt = cpu_to_le32(p_conn->total_rt);
		dval = p_conn->ka_timeout_delta;
		p_tcp->ka_timeout_delta = cpu_to_le32(dval);
		dval = p_conn->rt_timeout_delta;
		p_tcp->rt_timeout_delta = cpu_to_le32(dval);
		p_tcp->dup_ack_cnt = p_conn->dup_ack_cnt;
		p_tcp->snd_wnd_probe_cnt = p_conn->snd_wnd_probe_cnt;
		p_tcp->ka_probe_cnt = p_conn->ka_probe_cnt;
		p_tcp->rt_cnt = p_conn->rt_cnt;
		p_tcp->flow_label = cpu_to_le32(p_conn->flow_label);
		p_tcp->ka_timeout = cpu_to_le32(p_conn->ka_timeout);
		p_tcp->ka_interval = cpu_to_le32(p_conn->ka_interval);
		p_tcp->max_rt_time = cpu_to_le32(p_conn->max_rt_time);
		dval = p_conn->initial_rcv_wnd;
		p_tcp->initial_rcv_wnd = cpu_to_le32(dval);
		p_tcp->ttl = p_conn->ttl;
		p_tcp->tos_or_tc = p_conn->tos_or_tc;
		p_tcp->remote_port = cpu_to_le16(p_conn->remote_port);
		p_tcp->local_port = cpu_to_le16(p_conn->local_port);
		p_tcp->mss = cpu_to_le16(p_conn->mss);
		p_tcp->snd_wnd_scale = p_conn->snd_wnd_scale;
		p_tcp->rcv_wnd_scale = p_conn->rcv_wnd_scale;
		wval = p_conn->da_timeout_value;
		p_tcp->da_timeout_value = cpu_to_le16(wval);
		p_tcp->ack_frequency = p_conn->ack_frequency;
		p_tcp->connect_mode = p_conn->connect_mode;
	} else {
		p_tcp2 =
		    &((struct iscsi_spe_conn_offload_option2 *)p_ramrod)->tcp;

		p = (u16 *)p_conn->local_mac;
		tmp = cpu_to_le16(get_unaligned_be16(p));
		p_tcp2->local_mac_addr_hi = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 1));
		p_tcp2->local_mac_addr_mid = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 2));
		p_tcp2->local_mac_addr_lo = tmp;

		p = (u16 *)p_conn->remote_mac;
		tmp = cpu_to_le16(get_unaligned_be16(p));
		p_tcp2->remote_mac_addr_hi = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 1));
		p_tcp2->remote_mac_addr_mid = tmp;
		tmp = cpu_to_le16(get_unaligned_be16(p + 2));
		p_tcp2->remote_mac_addr_lo = tmp;

		p_tcp2->vlan_id = cpu_to_le16(p_conn->vlan_id);
		p_tcp2->flags = cpu_to_le16(p_conn->tcp_flags);

		p_tcp2->ip_version = p_conn->ip_version;
		for (i = 0; i < 4; i++) {
			dval = p_conn->remote_ip[i];
			p_tcp2->remote_ip[i] = cpu_to_le32(dval);
			dval = p_conn->local_ip[i];
			p_tcp2->local_ip[i] = cpu_to_le32(dval);
		}

		p_tcp2->flow_label = cpu_to_le32(p_conn->flow_label);
		p_tcp2->ttl = p_conn->ttl;
		p_tcp2->tos_or_tc = p_conn->tos_or_tc;
		p_tcp2->remote_port = cpu_to_le16(p_conn->remote_port);
		p_tcp2->local_port = cpu_to_le16(p_conn->local_port);
		p_tcp2->mss = cpu_to_le16(p_conn->mss);
		p_tcp2->rcv_wnd_scale = p_conn->rcv_wnd_scale;
		p_tcp2->connect_mode = p_conn->connect_mode;
		wval = p_conn->syn_ip_payload_length;
		p_tcp2->syn_ip_payload_length = cpu_to_le16(wval);
		p_tcp2->syn_phy_addr_lo = DMA_LO_LE(p_conn->syn_phy_addr);
		p_tcp2->syn_phy_addr_hi = DMA_HI_LE(p_conn->syn_phy_addr);
		p_tcp2->cwnd = cpu_to_le32(p_conn->cwnd);
		p_tcp2->ka_max_probe_cnt = p_conn->ka_probe_cnt;
		p_tcp2->ka_timeout = cpu_to_le32(p_conn->ka_timeout);
		p_tcp2->max_rt_time = cpu_to_le32(p_conn->max_rt_time);
		p_tcp2->ka_interval = cpu_to_le32(p_conn->ka_interval);
	}

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_iscsi_conn_update(struct qed_hwfn *p_hwfn,
				    struct qed_iscsi_conn *p_conn,
				    enum spq_mode comp_mode,
				    struct qed_spq_comp_cb *p_comp_addr)
{
	struct iscsi_conn_update_ramrod_params *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;
	u32 dval;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_UPDATE_CONN,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iscsi_conn_update;

	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	p_ramrod->flags = p_conn->update_flag;
	p_ramrod->max_seq_size = cpu_to_le32(p_conn->max_seq_size);
	dval = p_conn->max_recv_pdu_length;
	p_ramrod->max_recv_pdu_length = cpu_to_le32(dval);
	dval = p_conn->max_send_pdu_length;
	p_ramrod->max_send_pdu_length = cpu_to_le32(dval);
	dval = p_conn->first_seq_length;
	p_ramrod->first_seq_length = cpu_to_le32(dval);
	p_ramrod->exp_stat_sn = cpu_to_le32(p_conn->exp_stat_sn);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_sp_iscsi_mac_update(struct qed_hwfn *p_hwfn,
			struct qed_iscsi_conn *p_conn,
			enum spq_mode comp_mode,
			struct qed_spq_comp_cb *p_comp_addr)
{
	struct iscsi_spe_conn_mac_update *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;
	u8 ucval;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_MAC_UPDATE,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iscsi_conn_mac_update;

	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	ucval = p_conn->remote_mac[1];
	((u8 *)(&p_ramrod->remote_mac_addr_hi))[0] = ucval;
	ucval = p_conn->remote_mac[0];
	((u8 *)(&p_ramrod->remote_mac_addr_hi))[1] = ucval;
	ucval = p_conn->remote_mac[3];
	((u8 *)(&p_ramrod->remote_mac_addr_mid))[0] = ucval;
	ucval = p_conn->remote_mac[2];
	((u8 *)(&p_ramrod->remote_mac_addr_mid))[1] = ucval;
	ucval = p_conn->remote_mac[5];
	((u8 *)(&p_ramrod->remote_mac_addr_lo))[0] = ucval;
	ucval = p_conn->remote_mac[4];
	((u8 *)(&p_ramrod->remote_mac_addr_lo))[1] = ucval;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_iscsi_conn_terminate(struct qed_hwfn *p_hwfn,
				       struct qed_iscsi_conn *p_conn,
				       enum spq_mode comp_mode,
				       struct qed_spq_comp_cb *p_comp_addr)
{
	struct iscsi_spe_conn_termination *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_TERMINATION_CONN,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.iscsi_conn_terminate;

	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	p_ramrod->abortive = p_conn->abortive_dsconnect;

	DMA_REGPAIR_LE(p_ramrod->query_params_addr,
		       p_conn->tcp_upload_params_phys_addr);
	DMA_REGPAIR_LE(p_ramrod->queue_cnts_addr, p_conn->queue_cnts_phys_addr);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_iscsi_conn_clear_sq(struct qed_hwfn *p_hwfn,
				      struct qed_iscsi_conn *p_conn,
				      enum spq_mode comp_mode,
				      struct qed_spq_comp_cb *p_comp_addr)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = -EINVAL;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_CLEAR_SQ,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_iscsi_func_stop(struct qed_hwfn *p_hwfn,
				  enum spq_mode comp_mode,
				  struct qed_spq_comp_cb *p_comp_addr)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = 0;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 ISCSI_RAMROD_CMD_ID_DESTROY_FUNC,
				 PROTOCOLID_ISCSI, &init_data);
	if (rc)
		return rc;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	qed_spq_unregister_async_cb(p_hwfn, PROTOCOLID_ISCSI);
	return rc;
}

static void __iomem *qed_iscsi_get_db_addr(struct qed_hwfn *p_hwfn, u32 cid)
{
	return (u8 __iomem *)p_hwfn->doorbells +
			     qed_db_addr(cid, DQ_DEMS_LEGACY);
}

static void __iomem *qed_iscsi_get_primary_bdq_prod(struct qed_hwfn *p_hwfn,
						    u8 bdq_id)
{
	if (RESC_NUM(p_hwfn, QED_BDQ)) {
		return (u8 __iomem *)p_hwfn->regview +
		       GTT_BAR0_MAP_REG_MSDM_RAM +
		       MSTORM_SCSI_BDQ_EXT_PROD_OFFSET(RESC_START(p_hwfn,
								  QED_BDQ),
						       bdq_id);
	} else {
		DP_NOTICE(p_hwfn, "BDQ is not allocated!\n");
		return NULL;
	}
}

static void __iomem *qed_iscsi_get_secondary_bdq_prod(struct qed_hwfn *p_hwfn,
						      u8 bdq_id)
{
	if (RESC_NUM(p_hwfn, QED_BDQ)) {
		return (u8 __iomem *)p_hwfn->regview +
		       GTT_BAR0_MAP_REG_TSDM_RAM +
		       TSTORM_SCSI_BDQ_EXT_PROD_OFFSET(RESC_START(p_hwfn,
								  QED_BDQ),
						       bdq_id);
	} else {
		DP_NOTICE(p_hwfn, "BDQ is not allocated!\n");
		return NULL;
	}
}

static int qed_iscsi_setup_connection(struct qed_iscsi_conn *p_conn)
{
	if (!p_conn->queue_cnts_virt_addr)
		goto nomem;
	memset(p_conn->queue_cnts_virt_addr, 0,
	       sizeof(*p_conn->queue_cnts_virt_addr));

	if (!p_conn->tcp_upload_params_virt_addr)
		goto nomem;
	memset(p_conn->tcp_upload_params_virt_addr, 0,
	       sizeof(*p_conn->tcp_upload_params_virt_addr));

	if (!p_conn->r2tq.p_virt_addr)
		goto nomem;
	qed_chain_pbl_zero_mem(&p_conn->r2tq);

	if (!p_conn->uhq.p_virt_addr)
		goto nomem;
	qed_chain_pbl_zero_mem(&p_conn->uhq);

	if (!p_conn->xhq.p_virt_addr)
		goto nomem;
	qed_chain_pbl_zero_mem(&p_conn->xhq);

	return 0;
nomem:
	return -ENOMEM;
}

static int qed_iscsi_allocate_connection(struct qed_hwfn *p_hwfn,
					 struct qed_iscsi_conn **p_out_conn)
{
	struct scsi_terminate_extra_params *p_q_cnts = NULL;
	struct qed_iscsi_pf_params *p_params = NULL;
	struct qed_chain_init_params params = {
		.mode		= QED_CHAIN_MODE_PBL,
		.intended_use	= QED_CHAIN_USE_TO_CONSUME_PRODUCE,
		.cnt_type	= QED_CHAIN_CNT_TYPE_U16,
	};
	struct tcp_upload_params *p_tcp = NULL;
	struct qed_iscsi_conn *p_conn = NULL;
	int rc = 0;

	/* Try finding a free connection that can be used */
	spin_lock_bh(&p_hwfn->p_iscsi_info->lock);
	if (!list_empty(&p_hwfn->p_iscsi_info->free_list))
		p_conn = list_first_entry(&p_hwfn->p_iscsi_info->free_list,
					  struct qed_iscsi_conn, list_entry);
	if (p_conn) {
		list_del(&p_conn->list_entry);
		spin_unlock_bh(&p_hwfn->p_iscsi_info->lock);
		*p_out_conn = p_conn;
		return 0;
	}
	spin_unlock_bh(&p_hwfn->p_iscsi_info->lock);

	/* Need to allocate a new connection */
	p_params = &p_hwfn->pf_params.iscsi_pf_params;

	p_conn = kzalloc(sizeof(*p_conn), GFP_KERNEL);
	if (!p_conn)
		return -ENOMEM;

	p_q_cnts = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				      sizeof(*p_q_cnts),
				      &p_conn->queue_cnts_phys_addr,
				      GFP_KERNEL);
	if (!p_q_cnts)
		goto nomem_queue_cnts_param;
	p_conn->queue_cnts_virt_addr = p_q_cnts;

	p_tcp = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				   sizeof(*p_tcp),
				   &p_conn->tcp_upload_params_phys_addr,
				   GFP_KERNEL);
	if (!p_tcp)
		goto nomem_upload_param;
	p_conn->tcp_upload_params_virt_addr = p_tcp;

	params.num_elems = p_params->num_r2tq_pages_in_ring *
			   QED_CHAIN_PAGE_SIZE / sizeof(struct iscsi_wqe);
	params.elem_size = sizeof(struct iscsi_wqe);

	rc = qed_chain_alloc(p_hwfn->cdev, &p_conn->r2tq, &params);
	if (rc)
		goto nomem_r2tq;

	params.num_elems = p_params->num_uhq_pages_in_ring *
			   QED_CHAIN_PAGE_SIZE / sizeof(struct iscsi_uhqe);
	params.elem_size = sizeof(struct iscsi_uhqe);

	rc = qed_chain_alloc(p_hwfn->cdev, &p_conn->uhq, &params);
	if (rc)
		goto nomem_uhq;

	params.elem_size = sizeof(struct iscsi_xhqe);

	rc = qed_chain_alloc(p_hwfn->cdev, &p_conn->xhq, &params);
	if (rc)
		goto nomem;

	p_conn->free_on_delete = true;
	*p_out_conn = p_conn;
	return 0;

nomem:
	qed_chain_free(p_hwfn->cdev, &p_conn->uhq);
nomem_uhq:
	qed_chain_free(p_hwfn->cdev, &p_conn->r2tq);
nomem_r2tq:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct tcp_upload_params),
			  p_conn->tcp_upload_params_virt_addr,
			  p_conn->tcp_upload_params_phys_addr);
nomem_upload_param:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct scsi_terminate_extra_params),
			  p_conn->queue_cnts_virt_addr,
			  p_conn->queue_cnts_phys_addr);
nomem_queue_cnts_param:
	kfree(p_conn);

	return -ENOMEM;
}

static int qed_iscsi_acquire_connection(struct qed_hwfn *p_hwfn,
					struct qed_iscsi_conn *p_in_conn,
					struct qed_iscsi_conn **p_out_conn)
{
	struct qed_iscsi_conn *p_conn = NULL;
	int rc = 0;
	u32 icid;

	spin_lock_bh(&p_hwfn->p_iscsi_info->lock);
	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_ISCSI, &icid);
	spin_unlock_bh(&p_hwfn->p_iscsi_info->lock);
	if (rc)
		return rc;

	/* Use input connection or allocate a new one */
	if (p_in_conn)
		p_conn = p_in_conn;
	else
		rc = qed_iscsi_allocate_connection(p_hwfn, &p_conn);

	if (!rc)
		rc = qed_iscsi_setup_connection(p_conn);

	if (rc) {
		spin_lock_bh(&p_hwfn->p_iscsi_info->lock);
		qed_cxt_release_cid(p_hwfn, icid);
		spin_unlock_bh(&p_hwfn->p_iscsi_info->lock);
		return rc;
	}

	p_conn->icid = icid;
	p_conn->conn_id = (u16)icid;
	p_conn->fw_cid = (p_hwfn->hw_info.opaque_fid << 16) | icid;

	*p_out_conn = p_conn;

	return rc;
}

static void qed_iscsi_release_connection(struct qed_hwfn *p_hwfn,
					 struct qed_iscsi_conn *p_conn)
{
	spin_lock_bh(&p_hwfn->p_iscsi_info->lock);
	list_add_tail(&p_conn->list_entry, &p_hwfn->p_iscsi_info->free_list);
	qed_cxt_release_cid(p_hwfn, p_conn->icid);
	spin_unlock_bh(&p_hwfn->p_iscsi_info->lock);
}

static void qed_iscsi_free_connection(struct qed_hwfn *p_hwfn,
				      struct qed_iscsi_conn *p_conn)
{
	qed_chain_free(p_hwfn->cdev, &p_conn->xhq);
	qed_chain_free(p_hwfn->cdev, &p_conn->uhq);
	qed_chain_free(p_hwfn->cdev, &p_conn->r2tq);
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct tcp_upload_params),
			  p_conn->tcp_upload_params_virt_addr,
			  p_conn->tcp_upload_params_phys_addr);
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  sizeof(struct scsi_terminate_extra_params),
			  p_conn->queue_cnts_virt_addr,
			  p_conn->queue_cnts_phys_addr);
	kfree(p_conn);
}

int qed_iscsi_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_iscsi_info *p_iscsi_info;

	p_iscsi_info = kzalloc(sizeof(*p_iscsi_info), GFP_KERNEL);
	if (!p_iscsi_info)
		return -ENOMEM;

	INIT_LIST_HEAD(&p_iscsi_info->free_list);

	p_hwfn->p_iscsi_info = p_iscsi_info;
	return 0;
}

void qed_iscsi_setup(struct qed_hwfn *p_hwfn)
{
	spin_lock_init(&p_hwfn->p_iscsi_info->lock);
}

void qed_iscsi_free(struct qed_hwfn *p_hwfn)
{
	struct qed_iscsi_conn *p_conn = NULL;

	if (!p_hwfn->p_iscsi_info)
		return;

	while (!list_empty(&p_hwfn->p_iscsi_info->free_list)) {
		p_conn = list_first_entry(&p_hwfn->p_iscsi_info->free_list,
					  struct qed_iscsi_conn, list_entry);
		if (p_conn) {
			list_del(&p_conn->list_entry);
			qed_iscsi_free_connection(p_hwfn, p_conn);
		}
	}

	kfree(p_hwfn->p_iscsi_info);
	p_hwfn->p_iscsi_info = NULL;
}

static void _qed_iscsi_get_tstats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct tstorm_iscsi_stats_drv tstats;
	u32 tstats_addr;

	memset(&tstats, 0, sizeof(tstats));
	tstats_addr = BAR0_MAP_REG_TSDM_RAM +
		      TSTORM_ISCSI_RX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &tstats, tstats_addr, sizeof(tstats));

	p_stats->iscsi_rx_bytes_cnt =
	    HILO_64_REGPAIR(tstats.iscsi_rx_bytes_cnt);
	p_stats->iscsi_rx_packet_cnt =
	    HILO_64_REGPAIR(tstats.iscsi_rx_packet_cnt);
	p_stats->iscsi_rx_new_ooo_isle_events_cnt =
	    HILO_64_REGPAIR(tstats.iscsi_rx_new_ooo_isle_events_cnt);
	p_stats->iscsi_cmdq_threshold_cnt =
	    le32_to_cpu(tstats.iscsi_cmdq_threshold_cnt);
	p_stats->iscsi_rq_threshold_cnt =
	    le32_to_cpu(tstats.iscsi_rq_threshold_cnt);
	p_stats->iscsi_immq_threshold_cnt =
	    le32_to_cpu(tstats.iscsi_immq_threshold_cnt);
}

static void _qed_iscsi_get_mstats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct mstorm_iscsi_stats_drv mstats;
	u32 mstats_addr;

	memset(&mstats, 0, sizeof(mstats));
	mstats_addr = BAR0_MAP_REG_MSDM_RAM +
		      MSTORM_ISCSI_RX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &mstats, mstats_addr, sizeof(mstats));

	p_stats->iscsi_rx_dropped_pdus_task_not_valid =
	    HILO_64_REGPAIR(mstats.iscsi_rx_dropped_pdus_task_not_valid);
}

static void _qed_iscsi_get_ustats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct ustorm_iscsi_stats_drv ustats;
	u32 ustats_addr;

	memset(&ustats, 0, sizeof(ustats));
	ustats_addr = BAR0_MAP_REG_USDM_RAM +
		      USTORM_ISCSI_RX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &ustats, ustats_addr, sizeof(ustats));

	p_stats->iscsi_rx_data_pdu_cnt =
	    HILO_64_REGPAIR(ustats.iscsi_rx_data_pdu_cnt);
	p_stats->iscsi_rx_r2t_pdu_cnt =
	    HILO_64_REGPAIR(ustats.iscsi_rx_r2t_pdu_cnt);
	p_stats->iscsi_rx_total_pdu_cnt =
	    HILO_64_REGPAIR(ustats.iscsi_rx_total_pdu_cnt);
}

static void _qed_iscsi_get_xstats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct xstorm_iscsi_stats_drv xstats;
	u32 xstats_addr;

	memset(&xstats, 0, sizeof(xstats));
	xstats_addr = BAR0_MAP_REG_XSDM_RAM +
		      XSTORM_ISCSI_TX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &xstats, xstats_addr, sizeof(xstats));

	p_stats->iscsi_tx_go_to_slow_start_event_cnt =
	    HILO_64_REGPAIR(xstats.iscsi_tx_go_to_slow_start_event_cnt);
	p_stats->iscsi_tx_fast_retransmit_event_cnt =
	    HILO_64_REGPAIR(xstats.iscsi_tx_fast_retransmit_event_cnt);
}

static void _qed_iscsi_get_ystats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct ystorm_iscsi_stats_drv ystats;
	u32 ystats_addr;

	memset(&ystats, 0, sizeof(ystats));
	ystats_addr = BAR0_MAP_REG_YSDM_RAM +
		      YSTORM_ISCSI_TX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &ystats, ystats_addr, sizeof(ystats));

	p_stats->iscsi_tx_data_pdu_cnt =
	    HILO_64_REGPAIR(ystats.iscsi_tx_data_pdu_cnt);
	p_stats->iscsi_tx_r2t_pdu_cnt =
	    HILO_64_REGPAIR(ystats.iscsi_tx_r2t_pdu_cnt);
	p_stats->iscsi_tx_total_pdu_cnt =
	    HILO_64_REGPAIR(ystats.iscsi_tx_total_pdu_cnt);
}

static void _qed_iscsi_get_pstats(struct qed_hwfn *p_hwfn,
				  struct qed_ptt *p_ptt,
				  struct qed_iscsi_stats *p_stats)
{
	struct pstorm_iscsi_stats_drv pstats;
	u32 pstats_addr;

	memset(&pstats, 0, sizeof(pstats));
	pstats_addr = BAR0_MAP_REG_PSDM_RAM +
		      PSTORM_ISCSI_TX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &pstats, pstats_addr, sizeof(pstats));

	p_stats->iscsi_tx_bytes_cnt =
	    HILO_64_REGPAIR(pstats.iscsi_tx_bytes_cnt);
	p_stats->iscsi_tx_packet_cnt =
	    HILO_64_REGPAIR(pstats.iscsi_tx_packet_cnt);
}

static int qed_iscsi_get_stats(struct qed_hwfn *p_hwfn,
			       struct qed_iscsi_stats *stats)
{
	struct qed_ptt *p_ptt;

	memset(stats, 0, sizeof(*stats));

	p_ptt = qed_ptt_acquire(p_hwfn);
	if (!p_ptt) {
		DP_ERR(p_hwfn, "Failed to acquire ptt\n");
		return -EAGAIN;
	}

	_qed_iscsi_get_tstats(p_hwfn, p_ptt, stats);
	_qed_iscsi_get_mstats(p_hwfn, p_ptt, stats);
	_qed_iscsi_get_ustats(p_hwfn, p_ptt, stats);

	_qed_iscsi_get_xstats(p_hwfn, p_ptt, stats);
	_qed_iscsi_get_ystats(p_hwfn, p_ptt, stats);
	_qed_iscsi_get_pstats(p_hwfn, p_ptt, stats);

	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

struct qed_hash_iscsi_con {
	struct hlist_node node;
	struct qed_iscsi_conn *con;
};

static int qed_fill_iscsi_dev_info(struct qed_dev *cdev,
				   struct qed_dev_iscsi_info *info)
{
	struct qed_hwfn *hwfn = QED_AFFIN_HWFN(cdev);

	int rc;

	memset(info, 0, sizeof(*info));
	rc = qed_fill_dev_info(cdev, &info->common);

	info->primary_dbq_rq_addr =
	    qed_iscsi_get_primary_bdq_prod(hwfn, BDQ_ID_RQ);
	info->secondary_bdq_rq_addr =
	    qed_iscsi_get_secondary_bdq_prod(hwfn, BDQ_ID_RQ);

	info->num_cqs = FEAT_NUM(hwfn, QED_ISCSI_CQ);

	return rc;
}

static void qed_register_iscsi_ops(struct qed_dev *cdev,
				   struct qed_iscsi_cb_ops *ops, void *cookie)
{
	cdev->protocol_ops.iscsi = ops;
	cdev->ops_cookie = cookie;
}

static struct qed_hash_iscsi_con *qed_iscsi_get_hash(struct qed_dev *cdev,
						     u32 handle)
{
	struct qed_hash_iscsi_con *hash_con = NULL;

	if (!(cdev->flags & QED_FLAG_STORAGE_STARTED))
		return NULL;

	hash_for_each_possible(cdev->connections, hash_con, node, handle) {
		if (hash_con->con->icid == handle)
			break;
	}

	if (!hash_con || (hash_con->con->icid != handle))
		return NULL;

	return hash_con;
}

static int qed_iscsi_stop(struct qed_dev *cdev)
{
	int rc;

	if (!(cdev->flags & QED_FLAG_STORAGE_STARTED)) {
		DP_NOTICE(cdev, "iscsi already stopped\n");
		return 0;
	}

	if (!hash_empty(cdev->connections)) {
		DP_NOTICE(cdev,
			  "Can't stop iscsi - not all connections were returned\n");
		return -EINVAL;
	}

	/* Stop the iscsi */
	rc = qed_sp_iscsi_func_stop(QED_AFFIN_HWFN(cdev), QED_SPQ_MODE_EBLOCK,
				    NULL);
	cdev->flags &= ~QED_FLAG_STORAGE_STARTED;

	return rc;
}

static int qed_iscsi_start(struct qed_dev *cdev,
			   struct qed_iscsi_tid *tasks,
			   void *event_context,
			   iscsi_event_cb_t async_event_cb)
{
	int rc;
	struct qed_tid_mem *tid_info;

	if (cdev->flags & QED_FLAG_STORAGE_STARTED) {
		DP_NOTICE(cdev, "iscsi already started;\n");
		return 0;
	}

	rc = qed_sp_iscsi_func_start(QED_AFFIN_HWFN(cdev), QED_SPQ_MODE_EBLOCK,
				     NULL, event_context, async_event_cb);
	if (rc) {
		DP_NOTICE(cdev, "Failed to start iscsi\n");
		return rc;
	}

	cdev->flags |= QED_FLAG_STORAGE_STARTED;
	hash_init(cdev->connections);

	if (!tasks)
		return 0;

	tid_info = kzalloc(sizeof(*tid_info), GFP_KERNEL);

	if (!tid_info) {
		qed_iscsi_stop(cdev);
		return -ENOMEM;
	}

	rc = qed_cxt_get_tid_mem_info(QED_AFFIN_HWFN(cdev), tid_info);
	if (rc) {
		DP_NOTICE(cdev, "Failed to gather task information\n");
		qed_iscsi_stop(cdev);
		kfree(tid_info);
		return rc;
	}

	/* Fill task information */
	tasks->size = tid_info->tid_size;
	tasks->num_tids_per_block = tid_info->num_tids_per_block;
	memcpy(tasks->blocks, tid_info->blocks,
	       MAX_TID_BLOCKS_ISCSI * sizeof(u8 *));

	kfree(tid_info);

	return 0;
}

static int qed_iscsi_acquire_conn(struct qed_dev *cdev,
				  u32 *handle,
				  u32 *fw_cid, void __iomem **p_doorbell)
{
	struct qed_hash_iscsi_con *hash_con;
	int rc;

	/* Allocate a hashed connection */
	hash_con = kzalloc(sizeof(*hash_con), GFP_ATOMIC);
	if (!hash_con)
		return -ENOMEM;

	/* Acquire the connection */
	rc = qed_iscsi_acquire_connection(QED_AFFIN_HWFN(cdev), NULL,
					  &hash_con->con);
	if (rc) {
		DP_NOTICE(cdev, "Failed to acquire Connection\n");
		kfree(hash_con);
		return rc;
	}

	/* Added the connection to hash table */
	*handle = hash_con->con->icid;
	*fw_cid = hash_con->con->fw_cid;
	hash_add(cdev->connections, &hash_con->node, *handle);

	if (p_doorbell)
		*p_doorbell = qed_iscsi_get_db_addr(QED_AFFIN_HWFN(cdev),
						    *handle);

	return 0;
}

static int qed_iscsi_release_conn(struct qed_dev *cdev, u32 handle)
{
	struct qed_hash_iscsi_con *hash_con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	hlist_del(&hash_con->node);
	qed_iscsi_release_connection(QED_AFFIN_HWFN(cdev), hash_con->con);
	kfree(hash_con);

	return 0;
}

static int qed_iscsi_offload_conn(struct qed_dev *cdev,
				  u32 handle,
				  struct qed_iscsi_params_offload *conn_info)
{
	struct qed_hash_iscsi_con *hash_con;
	struct qed_iscsi_conn *con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;

	ether_addr_copy(con->local_mac, conn_info->src.mac);
	ether_addr_copy(con->remote_mac, conn_info->dst.mac);
	memcpy(con->local_ip, conn_info->src.ip, sizeof(con->local_ip));
	memcpy(con->remote_ip, conn_info->dst.ip, sizeof(con->remote_ip));
	con->local_port = conn_info->src.port;
	con->remote_port = conn_info->dst.port;

	con->layer_code = conn_info->layer_code;
	con->sq_pbl_addr = conn_info->sq_pbl_addr;
	con->initial_ack = conn_info->initial_ack;
	con->vlan_id = conn_info->vlan_id;
	con->tcp_flags = conn_info->tcp_flags;
	con->ip_version = conn_info->ip_version;
	con->default_cq = conn_info->default_cq;
	con->ka_max_probe_cnt = conn_info->ka_max_probe_cnt;
	con->dup_ack_theshold = conn_info->dup_ack_theshold;
	con->rcv_next = conn_info->rcv_next;
	con->snd_una = conn_info->snd_una;
	con->snd_next = conn_info->snd_next;
	con->snd_max = conn_info->snd_max;
	con->snd_wnd = conn_info->snd_wnd;
	con->rcv_wnd = conn_info->rcv_wnd;
	con->snd_wl1 = conn_info->snd_wl1;
	con->cwnd = conn_info->cwnd;
	con->ss_thresh = conn_info->ss_thresh;
	con->srtt = conn_info->srtt;
	con->rtt_var = conn_info->rtt_var;
	con->ts_recent = conn_info->ts_recent;
	con->ts_recent_age = conn_info->ts_recent_age;
	con->total_rt = conn_info->total_rt;
	con->ka_timeout_delta = conn_info->ka_timeout_delta;
	con->rt_timeout_delta = conn_info->rt_timeout_delta;
	con->dup_ack_cnt = conn_info->dup_ack_cnt;
	con->snd_wnd_probe_cnt = conn_info->snd_wnd_probe_cnt;
	con->ka_probe_cnt = conn_info->ka_probe_cnt;
	con->rt_cnt = conn_info->rt_cnt;
	con->flow_label = conn_info->flow_label;
	con->ka_timeout = conn_info->ka_timeout;
	con->ka_interval = conn_info->ka_interval;
	con->max_rt_time = conn_info->max_rt_time;
	con->initial_rcv_wnd = conn_info->initial_rcv_wnd;
	con->ttl = conn_info->ttl;
	con->tos_or_tc = conn_info->tos_or_tc;
	con->remote_port = conn_info->remote_port;
	con->local_port = conn_info->local_port;
	con->mss = conn_info->mss;
	con->snd_wnd_scale = conn_info->snd_wnd_scale;
	con->rcv_wnd_scale = conn_info->rcv_wnd_scale;
	con->da_timeout_value = conn_info->da_timeout_value;
	con->ack_frequency = conn_info->ack_frequency;

	/* Set default values on other connection fields */
	con->offl_flags = 0x1;

	return qed_sp_iscsi_conn_offload(QED_AFFIN_HWFN(cdev), con,
					 QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_iscsi_update_conn(struct qed_dev *cdev,
				 u32 handle,
				 struct qed_iscsi_params_update *conn_info)
{
	struct qed_hash_iscsi_con *hash_con;
	struct qed_iscsi_conn *con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;
	con->update_flag = conn_info->update_flag;
	con->max_seq_size = conn_info->max_seq_size;
	con->max_recv_pdu_length = conn_info->max_recv_pdu_length;
	con->max_send_pdu_length = conn_info->max_send_pdu_length;
	con->first_seq_length = conn_info->first_seq_length;
	con->exp_stat_sn = conn_info->exp_stat_sn;

	return qed_sp_iscsi_conn_update(QED_AFFIN_HWFN(cdev), con,
					QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_iscsi_clear_conn_sq(struct qed_dev *cdev, u32 handle)
{
	struct qed_hash_iscsi_con *hash_con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	return qed_sp_iscsi_conn_clear_sq(QED_AFFIN_HWFN(cdev), hash_con->con,
					  QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_iscsi_destroy_conn(struct qed_dev *cdev,
				  u32 handle, u8 abrt_conn)
{
	struct qed_hash_iscsi_con *hash_con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	hash_con->con->abortive_dsconnect = abrt_conn;

	return qed_sp_iscsi_conn_terminate(QED_AFFIN_HWFN(cdev), hash_con->con,
					   QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_iscsi_stats(struct qed_dev *cdev, struct qed_iscsi_stats *stats)
{
	return qed_iscsi_get_stats(QED_AFFIN_HWFN(cdev), stats);
}

static int qed_iscsi_change_mac(struct qed_dev *cdev,
				u32 handle, const u8 *mac)
{
	struct qed_hash_iscsi_con *hash_con;

	hash_con = qed_iscsi_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	return qed_sp_iscsi_mac_update(QED_AFFIN_HWFN(cdev), hash_con->con,
				       QED_SPQ_MODE_EBLOCK, NULL);
}

void qed_get_protocol_stats_iscsi(struct qed_dev *cdev,
				  struct qed_mcp_iscsi_stats *stats)
{
	struct qed_iscsi_stats proto_stats;

	/* Retrieve FW statistics */
	memset(&proto_stats, 0, sizeof(proto_stats));
	if (qed_iscsi_stats(cdev, &proto_stats)) {
		DP_VERBOSE(cdev, QED_MSG_STORAGE,
			   "Failed to collect ISCSI statistics\n");
		return;
	}

	/* Translate FW statistics into struct */
	stats->rx_pdus = proto_stats.iscsi_rx_total_pdu_cnt;
	stats->tx_pdus = proto_stats.iscsi_tx_total_pdu_cnt;
	stats->rx_bytes = proto_stats.iscsi_rx_bytes_cnt;
	stats->tx_bytes = proto_stats.iscsi_tx_bytes_cnt;
}

static const struct qed_iscsi_ops qed_iscsi_ops_pass = {
	.common = &qed_common_ops_pass,
	.ll2 = &qed_ll2_ops_pass,
	.fill_dev_info = &qed_fill_iscsi_dev_info,
	.register_ops = &qed_register_iscsi_ops,
	.start = &qed_iscsi_start,
	.stop = &qed_iscsi_stop,
	.acquire_conn = &qed_iscsi_acquire_conn,
	.release_conn = &qed_iscsi_release_conn,
	.offload_conn = &qed_iscsi_offload_conn,
	.update_conn = &qed_iscsi_update_conn,
	.destroy_conn = &qed_iscsi_destroy_conn,
	.clear_sq = &qed_iscsi_clear_conn_sq,
	.get_stats = &qed_iscsi_stats,
	.change_mac = &qed_iscsi_change_mac,
};

const struct qed_iscsi_ops *qed_get_iscsi_ops(void)
{
	return &qed_iscsi_ops_pass;
}
EXPORT_SYMBOL(qed_get_iscsi_ops);

void qed_put_iscsi_ops(void)
{
}
EXPORT_SYMBOL(qed_put_iscsi_ops);
