// SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause)
/* Copyright 2021 Marvell. All rights reserved. */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/kernel.h>
#include <linux/log2.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/qed/qed_nvmetcp_if.h>
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_nvmetcp.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_sp.h"
#include "qed_reg_addr.h"
#include "qed_nvmetcp_fw_funcs.h"

static int qed_nvmetcp_async_event(struct qed_hwfn *p_hwfn, u8 fw_event_code,
				   u16 echo, union event_ring_data *data,
				   u8 fw_return_code)
{
	if (p_hwfn->p_nvmetcp_info->event_cb) {
		struct qed_nvmetcp_info *p_nvmetcp = p_hwfn->p_nvmetcp_info;

		return p_nvmetcp->event_cb(p_nvmetcp->event_context,
					 fw_event_code, data);
	} else {
		DP_NOTICE(p_hwfn, "nvmetcp async completion is not set\n");

		return -EINVAL;
	}
}

static int qed_sp_nvmetcp_func_start(struct qed_hwfn *p_hwfn,
				     enum spq_mode comp_mode,
				     struct qed_spq_comp_cb *p_comp_addr,
				     void *event_context,
				     nvmetcp_event_cb_t async_event_cb)
{
	struct nvmetcp_init_ramrod_params *p_ramrod = NULL;
	struct qed_nvmetcp_pf_params *p_params = NULL;
	struct scsi_init_func_queues *p_queue = NULL;
	struct nvmetcp_spe_func_init *p_init = NULL;
	struct qed_sp_init_data init_data = {};
	struct qed_spq_entry *p_ent = NULL;
	int rc = 0;
	u16 val;
	u8 i;

	/* Get SPQ entry */
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;
	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 NVMETCP_RAMROD_CMD_ID_INIT_FUNC,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.nvmetcp_init;
	p_init = &p_ramrod->nvmetcp_init_spe;
	p_params = &p_hwfn->pf_params.nvmetcp_pf_params;
	p_queue = &p_init->q_params;
	p_init->num_sq_pages_in_ring = p_params->num_sq_pages_in_ring;
	p_init->num_r2tq_pages_in_ring = p_params->num_r2tq_pages_in_ring;
	p_init->num_uhq_pages_in_ring = p_params->num_uhq_pages_in_ring;
	p_init->ll2_rx_queue_id = RESC_START(p_hwfn, QED_LL2_RAM_QUEUE) +
					p_params->ll2_ooo_queue_id;
	SET_FIELD(p_init->flags, NVMETCP_SPE_FUNC_INIT_NVMETCP_MODE, 1);
	p_init->func_params.log_page_size = ilog2(PAGE_SIZE);
	p_init->func_params.num_tasks = cpu_to_le16(p_params->num_tasks);
	p_init->debug_flags = p_params->debug_mode;
	DMA_REGPAIR_LE(p_queue->glbl_q_params_addr,
		       p_params->glbl_q_params_addr);
	p_queue->cq_num_entries = cpu_to_le16(QED_NVMETCP_FW_CQ_SIZE);
	p_queue->num_queues = p_params->num_queues;
	val = RESC_START(p_hwfn, QED_CMDQS_CQS);
	p_queue->queue_relative_offset = cpu_to_le16((u16)val);
	p_queue->cq_sb_pi = p_params->gl_rq_pi;

	for (i = 0; i < p_params->num_queues; i++) {
		val = qed_get_igu_sb_id(p_hwfn, i);
		p_queue->cq_cmdq_sb_num_arr[i] = cpu_to_le16(val);
	}

	SET_FIELD(p_queue->q_validity,
		  SCSI_INIT_FUNC_QUEUES_CMD_VALID, 0);
	p_queue->cmdq_num_entries = 0;
	p_queue->bdq_resource_id = (u8)RESC_START(p_hwfn, QED_BDQ);
	p_ramrod->tcp_init.two_msl_timer = cpu_to_le32(QED_TCP_TWO_MSL_TIMER);
	p_ramrod->tcp_init.tx_sws_timer = cpu_to_le16(QED_TCP_SWS_TIMER);
	p_init->half_way_close_timeout = cpu_to_le16(QED_TCP_HALF_WAY_CLOSE_TIMEOUT);
	p_ramrod->tcp_init.max_fin_rt = QED_TCP_MAX_FIN_RT;
	SET_FIELD(p_ramrod->nvmetcp_init_spe.params,
		  NVMETCP_SPE_FUNC_INIT_MAX_SYN_RT, QED_TCP_MAX_FIN_RT);
	p_hwfn->p_nvmetcp_info->event_context = event_context;
	p_hwfn->p_nvmetcp_info->event_cb = async_event_cb;
	qed_spq_register_async_cb(p_hwfn, PROTOCOLID_TCP_ULP,
				  qed_nvmetcp_async_event);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_nvmetcp_func_stop(struct qed_hwfn *p_hwfn,
				    enum spq_mode comp_mode,
				    struct qed_spq_comp_cb *p_comp_addr)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;
	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 NVMETCP_RAMROD_CMD_ID_DESTROY_FUNC,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	rc = qed_spq_post(p_hwfn, p_ent, NULL);
	qed_spq_unregister_async_cb(p_hwfn, PROTOCOLID_TCP_ULP);

	return rc;
}

static int qed_fill_nvmetcp_dev_info(struct qed_dev *cdev,
				     struct qed_dev_nvmetcp_info *info)
{
	struct qed_hwfn *hwfn = QED_AFFIN_HWFN(cdev);
	int rc;

	memset(info, 0, sizeof(*info));
	rc = qed_fill_dev_info(cdev, &info->common);
	info->port_id = MFW_PORT(hwfn);
	info->num_cqs = FEAT_NUM(hwfn, QED_NVMETCP_CQ);

	return rc;
}

static void qed_register_nvmetcp_ops(struct qed_dev *cdev,
				     struct qed_nvmetcp_cb_ops *ops,
				     void *cookie)
{
	cdev->protocol_ops.nvmetcp = ops;
	cdev->ops_cookie = cookie;
}

static int qed_nvmetcp_stop(struct qed_dev *cdev)
{
	int rc;

	if (!(cdev->flags & QED_FLAG_STORAGE_STARTED)) {
		DP_NOTICE(cdev, "nvmetcp already stopped\n");

		return 0;
	}

	if (!hash_empty(cdev->connections)) {
		DP_NOTICE(cdev,
			  "Can't stop nvmetcp - not all connections were returned\n");

		return -EINVAL;
	}

	/* Stop the nvmetcp */
	rc = qed_sp_nvmetcp_func_stop(QED_AFFIN_HWFN(cdev), QED_SPQ_MODE_EBLOCK,
				      NULL);
	cdev->flags &= ~QED_FLAG_STORAGE_STARTED;

	return rc;
}

static int qed_nvmetcp_start(struct qed_dev *cdev,
			     struct qed_nvmetcp_tid *tasks,
			     void *event_context,
			     nvmetcp_event_cb_t async_event_cb)
{
	struct qed_tid_mem *tid_info;
	int rc;

	if (cdev->flags & QED_FLAG_STORAGE_STARTED) {
		DP_NOTICE(cdev, "nvmetcp already started;\n");

		return 0;
	}

	rc = qed_sp_nvmetcp_func_start(QED_AFFIN_HWFN(cdev),
				       QED_SPQ_MODE_EBLOCK, NULL,
				       event_context, async_event_cb);
	if (rc) {
		DP_NOTICE(cdev, "Failed to start nvmetcp\n");

		return rc;
	}

	cdev->flags |= QED_FLAG_STORAGE_STARTED;
	hash_init(cdev->connections);

	if (!tasks)
		return 0;

	tid_info = kzalloc(sizeof(*tid_info), GFP_KERNEL);
	if (!tid_info) {
		qed_nvmetcp_stop(cdev);

		return -ENOMEM;
	}

	rc = qed_cxt_get_tid_mem_info(QED_AFFIN_HWFN(cdev), tid_info);
	if (rc) {
		DP_NOTICE(cdev, "Failed to gather task information\n");
		qed_nvmetcp_stop(cdev);
		kfree(tid_info);

		return rc;
	}

	/* Fill task information */
	tasks->size = tid_info->tid_size;
	tasks->num_tids_per_block = tid_info->num_tids_per_block;
	memcpy(tasks->blocks, tid_info->blocks,
	       MAX_TID_BLOCKS_NVMETCP * sizeof(u8 *));
	kfree(tid_info);

	return 0;
}

static struct qed_hash_nvmetcp_con *qed_nvmetcp_get_hash(struct qed_dev *cdev,
							 u32 handle)
{
	struct qed_hash_nvmetcp_con *hash_con = NULL;

	if (!(cdev->flags & QED_FLAG_STORAGE_STARTED))
		return NULL;

	hash_for_each_possible(cdev->connections, hash_con, node, handle) {
		if (hash_con->con->icid == handle)
			break;
	}

	if (!hash_con || hash_con->con->icid != handle)
		return NULL;

	return hash_con;
}

static int qed_sp_nvmetcp_conn_offload(struct qed_hwfn *p_hwfn,
				       struct qed_nvmetcp_conn *p_conn,
				       enum spq_mode comp_mode,
				       struct qed_spq_comp_cb *p_comp_addr)
{
	struct nvmetcp_spe_conn_offload *p_ramrod = NULL;
	struct tcp_offload_params_opt2 *p_tcp = NULL;
	struct qed_sp_init_data init_data = { 0 };
	struct qed_spq_entry *p_ent = NULL;
	dma_addr_t r2tq_pbl_addr;
	dma_addr_t xhq_pbl_addr;
	dma_addr_t uhq_pbl_addr;
	u16 physical_q;
	int rc = 0;
	u8 i;

	/* Get SPQ entry */
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;
	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 NVMETCP_RAMROD_CMD_ID_OFFLOAD_CONN,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.nvmetcp_conn_offload;

	/* Transmission PQ is the first of the PF */
	physical_q = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_conn->physical_q0 = cpu_to_le16(physical_q);
	p_ramrod->nvmetcp.physical_q0 = cpu_to_le16(physical_q);

	/* nvmetcp Pure-ACK PQ */
	physical_q = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_ACK);
	p_conn->physical_q1 = cpu_to_le16(physical_q);
	p_ramrod->nvmetcp.physical_q1 = cpu_to_le16(physical_q);
	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	DMA_REGPAIR_LE(p_ramrod->nvmetcp.sq_pbl_addr, p_conn->sq_pbl_addr);
	r2tq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->r2tq);
	DMA_REGPAIR_LE(p_ramrod->nvmetcp.r2tq_pbl_addr, r2tq_pbl_addr);
	xhq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->xhq);
	DMA_REGPAIR_LE(p_ramrod->nvmetcp.xhq_pbl_addr, xhq_pbl_addr);
	uhq_pbl_addr = qed_chain_get_pbl_phys(&p_conn->uhq);
	DMA_REGPAIR_LE(p_ramrod->nvmetcp.uhq_pbl_addr, uhq_pbl_addr);
	p_ramrod->nvmetcp.flags = p_conn->offl_flags;
	p_ramrod->nvmetcp.default_cq = p_conn->default_cq;
	p_ramrod->nvmetcp.initial_ack = 0;
	DMA_REGPAIR_LE(p_ramrod->nvmetcp.nvmetcp.cccid_itid_table_addr,
		       p_conn->nvmetcp_cccid_itid_table_addr);
	p_ramrod->nvmetcp.nvmetcp.cccid_max_range =
		 cpu_to_le16(p_conn->nvmetcp_cccid_max_range);
	p_tcp = &p_ramrod->tcp;
	qed_set_fw_mac_addr(&p_tcp->remote_mac_addr_hi,
			    &p_tcp->remote_mac_addr_mid,
			    &p_tcp->remote_mac_addr_lo, p_conn->remote_mac);
	qed_set_fw_mac_addr(&p_tcp->local_mac_addr_hi,
			    &p_tcp->local_mac_addr_mid,
			    &p_tcp->local_mac_addr_lo, p_conn->local_mac);
	p_tcp->vlan_id = cpu_to_le16(p_conn->vlan_id);
	p_tcp->flags = cpu_to_le16(p_conn->tcp_flags);
	p_tcp->ip_version = p_conn->ip_version;
	if (p_tcp->ip_version == TCP_IPV6) {
		for (i = 0; i < 4; i++) {
			p_tcp->remote_ip[i] = cpu_to_le32(p_conn->remote_ip[i]);
			p_tcp->local_ip[i] = cpu_to_le32(p_conn->local_ip[i]);
		}
	} else {
		p_tcp->remote_ip[0] = cpu_to_le32(p_conn->remote_ip[0]);
		p_tcp->local_ip[0] = cpu_to_le32(p_conn->local_ip[0]);
	}

	p_tcp->flow_label = cpu_to_le32(p_conn->flow_label);
	p_tcp->ttl = p_conn->ttl;
	p_tcp->tos_or_tc = p_conn->tos_or_tc;
	p_tcp->remote_port = cpu_to_le16(p_conn->remote_port);
	p_tcp->local_port = cpu_to_le16(p_conn->local_port);
	p_tcp->mss = cpu_to_le16(p_conn->mss);
	p_tcp->rcv_wnd_scale = p_conn->rcv_wnd_scale;
	p_tcp->connect_mode = p_conn->connect_mode;
	p_tcp->cwnd = cpu_to_le32(p_conn->cwnd);
	p_tcp->ka_max_probe_cnt = p_conn->ka_max_probe_cnt;
	p_tcp->ka_timeout = cpu_to_le32(p_conn->ka_timeout);
	p_tcp->max_rt_time = cpu_to_le32(p_conn->max_rt_time);
	p_tcp->ka_interval = cpu_to_le32(p_conn->ka_interval);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_nvmetcp_conn_update(struct qed_hwfn *p_hwfn,
				      struct qed_nvmetcp_conn *p_conn,
				      enum spq_mode comp_mode,
				      struct qed_spq_comp_cb *p_comp_addr)
{
	struct nvmetcp_conn_update_ramrod_params *p_ramrod = NULL;
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
				 NVMETCP_RAMROD_CMD_ID_UPDATE_CONN,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.nvmetcp_conn_update;
	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	p_ramrod->flags = p_conn->update_flag;
	p_ramrod->max_seq_size = cpu_to_le32(p_conn->max_seq_size);
	dval = p_conn->max_recv_pdu_length;
	p_ramrod->max_recv_pdu_length = cpu_to_le32(dval);
	dval = p_conn->max_send_pdu_length;
	p_ramrod->max_send_pdu_length = cpu_to_le32(dval);
	p_ramrod->first_seq_length = cpu_to_le32(p_conn->first_seq_length);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_nvmetcp_conn_terminate(struct qed_hwfn *p_hwfn,
					 struct qed_nvmetcp_conn *p_conn,
					 enum spq_mode comp_mode,
					 struct qed_spq_comp_cb *p_comp_addr)
{
	struct nvmetcp_spe_conn_termination *p_ramrod = NULL;
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
				 NVMETCP_RAMROD_CMD_ID_TERMINATION_CONN,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.nvmetcp_conn_terminate;
	p_ramrod->conn_id = cpu_to_le16(p_conn->conn_id);
	p_ramrod->abortive = p_conn->abortive_dsconnect;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int qed_sp_nvmetcp_conn_clear_sq(struct qed_hwfn *p_hwfn,
					struct qed_nvmetcp_conn *p_conn,
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
				 NVMETCP_RAMROD_CMD_ID_CLEAR_SQ,
				 PROTOCOLID_TCP_ULP, &init_data);
	if (rc)
		return rc;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static void __iomem *qed_nvmetcp_get_db_addr(struct qed_hwfn *p_hwfn, u32 cid)
{
	return (u8 __iomem *)p_hwfn->doorbells +
			     qed_db_addr(cid, DQ_DEMS_LEGACY);
}

static int qed_nvmetcp_allocate_connection(struct qed_hwfn *p_hwfn,
					   struct qed_nvmetcp_conn **p_out_conn)
{
	struct qed_chain_init_params params = {
		.mode		= QED_CHAIN_MODE_PBL,
		.intended_use	= QED_CHAIN_USE_TO_CONSUME_PRODUCE,
		.cnt_type	= QED_CHAIN_CNT_TYPE_U16,
	};
	struct qed_nvmetcp_pf_params *p_params = NULL;
	struct qed_nvmetcp_conn *p_conn = NULL;
	int rc = 0;

	/* Try finding a free connection that can be used */
	spin_lock_bh(&p_hwfn->p_nvmetcp_info->lock);
	if (!list_empty(&p_hwfn->p_nvmetcp_info->free_list))
		p_conn = list_first_entry(&p_hwfn->p_nvmetcp_info->free_list,
					  struct qed_nvmetcp_conn, list_entry);
	if (p_conn) {
		list_del(&p_conn->list_entry);
		spin_unlock_bh(&p_hwfn->p_nvmetcp_info->lock);
		*p_out_conn = p_conn;

		return 0;
	}
	spin_unlock_bh(&p_hwfn->p_nvmetcp_info->lock);

	/* Need to allocate a new connection */
	p_params = &p_hwfn->pf_params.nvmetcp_pf_params;
	p_conn = kzalloc(sizeof(*p_conn), GFP_KERNEL);
	if (!p_conn)
		return -ENOMEM;

	params.num_elems = p_params->num_r2tq_pages_in_ring *
			   QED_CHAIN_PAGE_SIZE / sizeof(struct nvmetcp_wqe);
	params.elem_size = sizeof(struct nvmetcp_wqe);
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
	kfree(p_conn);

	return -ENOMEM;
}

static int qed_nvmetcp_acquire_connection(struct qed_hwfn *p_hwfn,
					  struct qed_nvmetcp_conn **p_out_conn)
{
	struct qed_nvmetcp_conn *p_conn = NULL;
	int rc = 0;
	u32 icid;

	spin_lock_bh(&p_hwfn->p_nvmetcp_info->lock);
	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_TCP_ULP, &icid);
	spin_unlock_bh(&p_hwfn->p_nvmetcp_info->lock);

	if (rc)
		return rc;

	rc = qed_nvmetcp_allocate_connection(p_hwfn, &p_conn);
	if (rc) {
		spin_lock_bh(&p_hwfn->p_nvmetcp_info->lock);
		qed_cxt_release_cid(p_hwfn, icid);
		spin_unlock_bh(&p_hwfn->p_nvmetcp_info->lock);

		return rc;
	}

	p_conn->icid = icid;
	p_conn->conn_id = (u16)icid;
	p_conn->fw_cid = (p_hwfn->hw_info.opaque_fid << 16) | icid;
	*p_out_conn = p_conn;

	return rc;
}

static void qed_nvmetcp_release_connection(struct qed_hwfn *p_hwfn,
					   struct qed_nvmetcp_conn *p_conn)
{
	spin_lock_bh(&p_hwfn->p_nvmetcp_info->lock);
	list_add_tail(&p_conn->list_entry, &p_hwfn->p_nvmetcp_info->free_list);
	qed_cxt_release_cid(p_hwfn, p_conn->icid);
	spin_unlock_bh(&p_hwfn->p_nvmetcp_info->lock);
}

static void qed_nvmetcp_free_connection(struct qed_hwfn *p_hwfn,
					struct qed_nvmetcp_conn *p_conn)
{
	qed_chain_free(p_hwfn->cdev, &p_conn->xhq);
	qed_chain_free(p_hwfn->cdev, &p_conn->uhq);
	qed_chain_free(p_hwfn->cdev, &p_conn->r2tq);
	kfree(p_conn);
}

int qed_nvmetcp_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_nvmetcp_info *p_nvmetcp_info;

	p_nvmetcp_info = kzalloc(sizeof(*p_nvmetcp_info), GFP_KERNEL);
	if (!p_nvmetcp_info)
		return -ENOMEM;

	INIT_LIST_HEAD(&p_nvmetcp_info->free_list);
	p_hwfn->p_nvmetcp_info = p_nvmetcp_info;

	return 0;
}

void qed_nvmetcp_setup(struct qed_hwfn *p_hwfn)
{
	spin_lock_init(&p_hwfn->p_nvmetcp_info->lock);
}

void qed_nvmetcp_free(struct qed_hwfn *p_hwfn)
{
	struct qed_nvmetcp_conn *p_conn = NULL;

	if (!p_hwfn->p_nvmetcp_info)
		return;

	while (!list_empty(&p_hwfn->p_nvmetcp_info->free_list)) {
		p_conn = list_first_entry(&p_hwfn->p_nvmetcp_info->free_list,
					  struct qed_nvmetcp_conn, list_entry);
		if (p_conn) {
			list_del(&p_conn->list_entry);
			qed_nvmetcp_free_connection(p_hwfn, p_conn);
		}
	}

	kfree(p_hwfn->p_nvmetcp_info);
	p_hwfn->p_nvmetcp_info = NULL;
}

static int qed_nvmetcp_acquire_conn(struct qed_dev *cdev,
				    u32 *handle,
				    u32 *fw_cid, void __iomem **p_doorbell)
{
	struct qed_hash_nvmetcp_con *hash_con;
	int rc;

	/* Allocate a hashed connection */
	hash_con = kzalloc(sizeof(*hash_con), GFP_ATOMIC);
	if (!hash_con)
		return -ENOMEM;

	/* Acquire the connection */
	rc = qed_nvmetcp_acquire_connection(QED_AFFIN_HWFN(cdev),
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
		*p_doorbell = qed_nvmetcp_get_db_addr(QED_AFFIN_HWFN(cdev),
						      *handle);

	return 0;
}

static int qed_nvmetcp_release_conn(struct qed_dev *cdev, u32 handle)
{
	struct qed_hash_nvmetcp_con *hash_con;

	hash_con = qed_nvmetcp_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);

		return -EINVAL;
	}

	hlist_del(&hash_con->node);
	qed_nvmetcp_release_connection(QED_AFFIN_HWFN(cdev), hash_con->con);
	kfree(hash_con);

	return 0;
}

static int qed_nvmetcp_offload_conn(struct qed_dev *cdev, u32 handle,
				    struct qed_nvmetcp_params_offload *conn_info)
{
	struct qed_hash_nvmetcp_con *hash_con;
	struct qed_nvmetcp_conn *con;

	hash_con = qed_nvmetcp_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);

		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;

	/* FW initializations */
	con->layer_code = NVMETCP_SLOW_PATH_LAYER_CODE;
	con->sq_pbl_addr = conn_info->sq_pbl_addr;
	con->nvmetcp_cccid_max_range = conn_info->nvmetcp_cccid_max_range;
	con->nvmetcp_cccid_itid_table_addr = conn_info->nvmetcp_cccid_itid_table_addr;
	con->default_cq = conn_info->default_cq;
	SET_FIELD(con->offl_flags, NVMETCP_CONN_OFFLOAD_PARAMS_TARGET_MODE, 0);
	SET_FIELD(con->offl_flags, NVMETCP_CONN_OFFLOAD_PARAMS_NVMETCP_MODE, 1);
	SET_FIELD(con->offl_flags, NVMETCP_CONN_OFFLOAD_PARAMS_TCP_ON_CHIP_1B, 1);

	/* Networking and TCP stack initializations */
	ether_addr_copy(con->local_mac, conn_info->src.mac);
	ether_addr_copy(con->remote_mac, conn_info->dst.mac);
	memcpy(con->local_ip, conn_info->src.ip, sizeof(con->local_ip));
	memcpy(con->remote_ip, conn_info->dst.ip, sizeof(con->remote_ip));
	con->local_port = conn_info->src.port;
	con->remote_port = conn_info->dst.port;
	con->vlan_id = conn_info->vlan_id;

	if (conn_info->timestamp_en)
		SET_FIELD(con->tcp_flags, TCP_OFFLOAD_PARAMS_OPT2_TS_EN, 1);

	if (conn_info->delayed_ack_en)
		SET_FIELD(con->tcp_flags, TCP_OFFLOAD_PARAMS_OPT2_DA_EN, 1);

	if (conn_info->tcp_keep_alive_en)
		SET_FIELD(con->tcp_flags, TCP_OFFLOAD_PARAMS_OPT2_KA_EN, 1);

	if (conn_info->ecn_en)
		SET_FIELD(con->tcp_flags, TCP_OFFLOAD_PARAMS_OPT2_ECN_EN, 1);

	con->ip_version = conn_info->ip_version;
	con->flow_label = QED_TCP_FLOW_LABEL;
	con->ka_max_probe_cnt = conn_info->ka_max_probe_cnt;
	con->ka_timeout = conn_info->ka_timeout;
	con->ka_interval = conn_info->ka_interval;
	con->max_rt_time = conn_info->max_rt_time;
	con->ttl = conn_info->ttl;
	con->tos_or_tc = conn_info->tos_or_tc;
	con->mss = conn_info->mss;
	con->cwnd = conn_info->cwnd;
	con->rcv_wnd_scale = conn_info->rcv_wnd_scale;
	con->connect_mode = 0;

	return qed_sp_nvmetcp_conn_offload(QED_AFFIN_HWFN(cdev), con,
					 QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_nvmetcp_update_conn(struct qed_dev *cdev,
				   u32 handle,
				   struct qed_nvmetcp_params_update *conn_info)
{
	struct qed_hash_nvmetcp_con *hash_con;
	struct qed_nvmetcp_conn *con;

	hash_con = qed_nvmetcp_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);

		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;
	SET_FIELD(con->update_flag,
		  ISCSI_CONN_UPDATE_RAMROD_PARAMS_INITIAL_R2T, 0);
	SET_FIELD(con->update_flag,
		  ISCSI_CONN_UPDATE_RAMROD_PARAMS_IMMEDIATE_DATA, 1);
	if (conn_info->hdr_digest_en)
		SET_FIELD(con->update_flag, ISCSI_CONN_UPDATE_RAMROD_PARAMS_HD_EN, 1);

	if (conn_info->data_digest_en)
		SET_FIELD(con->update_flag, ISCSI_CONN_UPDATE_RAMROD_PARAMS_DD_EN, 1);

	/* Placeholder - initialize pfv, cpda, hpda */

	con->max_seq_size = conn_info->max_io_size;
	con->max_recv_pdu_length = conn_info->max_recv_pdu_length;
	con->max_send_pdu_length = conn_info->max_send_pdu_length;
	con->first_seq_length = conn_info->max_io_size;

	return qed_sp_nvmetcp_conn_update(QED_AFFIN_HWFN(cdev), con,
					QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_nvmetcp_clear_conn_sq(struct qed_dev *cdev, u32 handle)
{
	struct qed_hash_nvmetcp_con *hash_con;

	hash_con = qed_nvmetcp_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);

		return -EINVAL;
	}

	return qed_sp_nvmetcp_conn_clear_sq(QED_AFFIN_HWFN(cdev), hash_con->con,
					    QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_nvmetcp_destroy_conn(struct qed_dev *cdev,
				    u32 handle, u8 abrt_conn)
{
	struct qed_hash_nvmetcp_con *hash_con;

	hash_con = qed_nvmetcp_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);

		return -EINVAL;
	}

	hash_con->con->abortive_dsconnect = abrt_conn;

	return qed_sp_nvmetcp_conn_terminate(QED_AFFIN_HWFN(cdev), hash_con->con,
					   QED_SPQ_MODE_EBLOCK, NULL);
}

static const struct qed_nvmetcp_ops qed_nvmetcp_ops_pass = {
	.common = &qed_common_ops_pass,
	.ll2 = &qed_ll2_ops_pass,
	.fill_dev_info = &qed_fill_nvmetcp_dev_info,
	.register_ops = &qed_register_nvmetcp_ops,
	.start = &qed_nvmetcp_start,
	.stop = &qed_nvmetcp_stop,
	.acquire_conn = &qed_nvmetcp_acquire_conn,
	.release_conn = &qed_nvmetcp_release_conn,
	.offload_conn = &qed_nvmetcp_offload_conn,
	.update_conn = &qed_nvmetcp_update_conn,
	.destroy_conn = &qed_nvmetcp_destroy_conn,
	.clear_sq = &qed_nvmetcp_clear_conn_sq,
	.add_src_tcp_port_filter = &qed_llh_add_src_tcp_port_filter,
	.remove_src_tcp_port_filter = &qed_llh_remove_src_tcp_port_filter,
	.add_dst_tcp_port_filter = &qed_llh_add_dst_tcp_port_filter,
	.remove_dst_tcp_port_filter = &qed_llh_remove_dst_tcp_port_filter,
	.clear_all_filters = &qed_llh_clear_all_filters,
	.init_read_io = &init_nvmetcp_host_read_task,
	.init_write_io = &init_nvmetcp_host_write_task,
	.init_icreq_exchange = &init_nvmetcp_init_conn_req_task,
	.init_task_cleanup = &init_cleanup_task_nvmetcp
};

const struct qed_nvmetcp_ops *qed_get_nvmetcp_ops(void)
{
	return &qed_nvmetcp_ops_pass;
}
EXPORT_SYMBOL(qed_get_nvmetcp_ops);

void qed_put_nvmetcp_ops(void)
{
}
EXPORT_SYMBOL(qed_put_nvmetcp_ops);
