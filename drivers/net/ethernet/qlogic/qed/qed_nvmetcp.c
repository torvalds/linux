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

static const struct qed_nvmetcp_ops qed_nvmetcp_ops_pass = {
	.common = &qed_common_ops_pass,
	.ll2 = &qed_ll2_ops_pass,
	.fill_dev_info = &qed_fill_nvmetcp_dev_info,
	.register_ops = &qed_register_nvmetcp_ops,
	.start = &qed_nvmetcp_start,
	.stop = &qed_nvmetcp_stop,

	/* Placeholder - Connection level ops */
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
