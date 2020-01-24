/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
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
#define __PREVENT_DUMP_MEM_ARR__
#define __PREVENT_PXP_GLOBAL_WIN__
#include "qed.h"
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include "qed_fcoe.h"
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_ll2.h"
#include "qed_mcp.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"
#include "qed_sriov.h"
#include <linux/qed/qed_fcoe_if.h>

struct qed_fcoe_conn {
	struct list_head list_entry;
	bool free_on_delete;

	u16 conn_id;
	u32 icid;
	u32 fw_cid;
	u8 layer_code;

	dma_addr_t sq_pbl_addr;
	dma_addr_t sq_curr_page_addr;
	dma_addr_t sq_next_page_addr;
	dma_addr_t xferq_pbl_addr;
	void *xferq_pbl_addr_virt_addr;
	dma_addr_t xferq_addr[4];
	void *xferq_addr_virt_addr[4];
	dma_addr_t confq_pbl_addr;
	void *confq_pbl_addr_virt_addr;
	dma_addr_t confq_addr[2];
	void *confq_addr_virt_addr[2];

	dma_addr_t terminate_params;

	u16 dst_mac_addr_lo;
	u16 dst_mac_addr_mid;
	u16 dst_mac_addr_hi;
	u16 src_mac_addr_lo;
	u16 src_mac_addr_mid;
	u16 src_mac_addr_hi;

	u16 tx_max_fc_pay_len;
	u16 e_d_tov_timer_val;
	u16 rec_tov_timer_val;
	u16 rx_max_fc_pay_len;
	u16 vlan_tag;
	u16 physical_q0;

	struct fc_addr_nw s_id;
	u8 max_conc_seqs_c3;
	struct fc_addr_nw d_id;
	u8 flags;
	u8 def_q_idx;
};

static int
qed_sp_fcoe_func_start(struct qed_hwfn *p_hwfn,
		       enum spq_mode comp_mode,
		       struct qed_spq_comp_cb *p_comp_addr)
{
	struct qed_fcoe_pf_params *fcoe_pf_params = NULL;
	struct fcoe_init_ramrod_params *p_ramrod = NULL;
	struct fcoe_init_func_ramrod_data *p_data;
	struct e4_fcoe_conn_context *p_cxt = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	struct qed_cxt_info cxt_info;
	u32 dummy_cid;
	int rc = 0;
	u16 tmp;
	u8 i;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = qed_spq_get_cid(p_hwfn);
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 FCOE_RAMROD_CMD_ID_INIT_FUNC,
				 PROTOCOLID_FCOE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.fcoe_init;
	p_data = &p_ramrod->init_ramrod_data;
	fcoe_pf_params = &p_hwfn->pf_params.fcoe_pf_params;

	/* Sanity */
	if (fcoe_pf_params->num_cqs > p_hwfn->hw_info.feat_num[QED_FCOE_CQ]) {
		DP_ERR(p_hwfn,
		       "Cannot satisfy CQ amount. CQs requested %d, CQs available %d. Aborting function start\n",
		       fcoe_pf_params->num_cqs,
		       p_hwfn->hw_info.feat_num[QED_FCOE_CQ]);
		rc = -EINVAL;
		goto err;
	}

	p_data->mtu = cpu_to_le16(fcoe_pf_params->mtu);
	tmp = cpu_to_le16(fcoe_pf_params->sq_num_pbl_pages);
	p_data->sq_num_pages_in_pbl = tmp;

	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_FCOE, &dummy_cid);
	if (rc)
		goto err;

	cxt_info.iid = dummy_cid;
	rc = qed_cxt_get_cid_info(p_hwfn, &cxt_info);
	if (rc) {
		DP_NOTICE(p_hwfn, "Cannot find context info for dummy cid=%d\n",
			  dummy_cid);
		goto err;
	}
	p_cxt = cxt_info.p_cxt;
	SET_FIELD(p_cxt->tstorm_ag_context.flags3,
		  E4_TSTORM_FCOE_CONN_AG_CTX_DUMMY_TIMER_CF_EN, 1);

	fcoe_pf_params->dummy_icid = (u16)dummy_cid;

	tmp = cpu_to_le16(fcoe_pf_params->num_tasks);
	p_data->func_params.num_tasks = tmp;
	p_data->func_params.log_page_size = fcoe_pf_params->log_page_size;
	p_data->func_params.debug_mode = fcoe_pf_params->debug_mode;

	DMA_REGPAIR_LE(p_data->q_params.glbl_q_params_addr,
		       fcoe_pf_params->glbl_q_params_addr);

	tmp = cpu_to_le16(fcoe_pf_params->cq_num_entries);
	p_data->q_params.cq_num_entries = tmp;

	tmp = cpu_to_le16(fcoe_pf_params->cmdq_num_entries);
	p_data->q_params.cmdq_num_entries = tmp;

	tmp = fcoe_pf_params->num_cqs;
	p_data->q_params.num_queues = (u8)tmp;

	tmp = (u16)p_hwfn->hw_info.resc_start[QED_CMDQS_CQS];
	p_data->q_params.queue_relative_offset = (u8)tmp;

	for (i = 0; i < fcoe_pf_params->num_cqs; i++) {
		u16 igu_sb_id;

		igu_sb_id = qed_get_igu_sb_id(p_hwfn, i);
		tmp = cpu_to_le16(igu_sb_id);
		p_data->q_params.cq_cmdq_sb_num_arr[i] = tmp;
	}

	p_data->q_params.cq_sb_pi = fcoe_pf_params->gl_rq_pi;
	p_data->q_params.cmdq_sb_pi = fcoe_pf_params->gl_cmd_pi;

	p_data->q_params.bdq_resource_id = (u8)RESC_START(p_hwfn, QED_BDQ);

	DMA_REGPAIR_LE(p_data->q_params.bdq_pbl_base_address[BDQ_ID_RQ],
		       fcoe_pf_params->bdq_pbl_base_addr[BDQ_ID_RQ]);
	p_data->q_params.bdq_pbl_num_entries[BDQ_ID_RQ] =
	    fcoe_pf_params->bdq_pbl_num_entries[BDQ_ID_RQ];
	tmp = fcoe_pf_params->bdq_xoff_threshold[BDQ_ID_RQ];
	p_data->q_params.bdq_xoff_threshold[BDQ_ID_RQ] = cpu_to_le16(tmp);
	tmp = fcoe_pf_params->bdq_xon_threshold[BDQ_ID_RQ];
	p_data->q_params.bdq_xon_threshold[BDQ_ID_RQ] = cpu_to_le16(tmp);

	DMA_REGPAIR_LE(p_data->q_params.bdq_pbl_base_address[BDQ_ID_IMM_DATA],
		       fcoe_pf_params->bdq_pbl_base_addr[BDQ_ID_IMM_DATA]);
	p_data->q_params.bdq_pbl_num_entries[BDQ_ID_IMM_DATA] =
	    fcoe_pf_params->bdq_pbl_num_entries[BDQ_ID_IMM_DATA];
	tmp = fcoe_pf_params->bdq_xoff_threshold[BDQ_ID_IMM_DATA];
	p_data->q_params.bdq_xoff_threshold[BDQ_ID_IMM_DATA] = cpu_to_le16(tmp);
	tmp = fcoe_pf_params->bdq_xon_threshold[BDQ_ID_IMM_DATA];
	p_data->q_params.bdq_xon_threshold[BDQ_ID_IMM_DATA] = cpu_to_le16(tmp);
	tmp = fcoe_pf_params->rq_buffer_size;
	p_data->q_params.rq_buffer_size = cpu_to_le16(tmp);

	if (fcoe_pf_params->is_target) {
		SET_FIELD(p_data->q_params.q_validity,
			  SCSI_INIT_FUNC_QUEUES_RQ_VALID, 1);
		if (p_data->q_params.bdq_pbl_num_entries[BDQ_ID_IMM_DATA])
			SET_FIELD(p_data->q_params.q_validity,
				  SCSI_INIT_FUNC_QUEUES_IMM_DATA_VALID, 1);
		SET_FIELD(p_data->q_params.q_validity,
			  SCSI_INIT_FUNC_QUEUES_CMD_VALID, 1);
	} else {
		SET_FIELD(p_data->q_params.q_validity,
			  SCSI_INIT_FUNC_QUEUES_RQ_VALID, 1);
	}

	rc = qed_spq_post(p_hwfn, p_ent, NULL);

	return rc;

err:
	qed_sp_destroy_request(p_hwfn, p_ent);
	return rc;
}

static int
qed_sp_fcoe_conn_offload(struct qed_hwfn *p_hwfn,
			 struct qed_fcoe_conn *p_conn,
			 enum spq_mode comp_mode,
			 struct qed_spq_comp_cb *p_comp_addr)
{
	struct fcoe_conn_offload_ramrod_params *p_ramrod = NULL;
	struct fcoe_conn_offload_ramrod_data *p_data;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	u16 physical_q0, tmp;
	int rc;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 FCOE_RAMROD_CMD_ID_OFFLOAD_CONN,
				 PROTOCOLID_FCOE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.fcoe_conn_ofld;
	p_data = &p_ramrod->offload_ramrod_data;

	/* Transmission PQ is the first of the PF */
	physical_q0 = qed_get_cm_pq_idx(p_hwfn, PQ_FLAGS_OFLD);
	p_conn->physical_q0 = cpu_to_le16(physical_q0);
	p_data->physical_q0 = cpu_to_le16(physical_q0);

	p_data->conn_id = cpu_to_le16(p_conn->conn_id);
	DMA_REGPAIR_LE(p_data->sq_pbl_addr, p_conn->sq_pbl_addr);
	DMA_REGPAIR_LE(p_data->sq_curr_page_addr, p_conn->sq_curr_page_addr);
	DMA_REGPAIR_LE(p_data->sq_next_page_addr, p_conn->sq_next_page_addr);
	DMA_REGPAIR_LE(p_data->xferq_pbl_addr, p_conn->xferq_pbl_addr);
	DMA_REGPAIR_LE(p_data->xferq_curr_page_addr, p_conn->xferq_addr[0]);
	DMA_REGPAIR_LE(p_data->xferq_next_page_addr, p_conn->xferq_addr[1]);

	DMA_REGPAIR_LE(p_data->respq_pbl_addr, p_conn->confq_pbl_addr);
	DMA_REGPAIR_LE(p_data->respq_curr_page_addr, p_conn->confq_addr[0]);
	DMA_REGPAIR_LE(p_data->respq_next_page_addr, p_conn->confq_addr[1]);

	p_data->dst_mac_addr_lo = cpu_to_le16(p_conn->dst_mac_addr_lo);
	p_data->dst_mac_addr_mid = cpu_to_le16(p_conn->dst_mac_addr_mid);
	p_data->dst_mac_addr_hi = cpu_to_le16(p_conn->dst_mac_addr_hi);
	p_data->src_mac_addr_lo = cpu_to_le16(p_conn->src_mac_addr_lo);
	p_data->src_mac_addr_mid = cpu_to_le16(p_conn->src_mac_addr_mid);
	p_data->src_mac_addr_hi = cpu_to_le16(p_conn->src_mac_addr_hi);

	tmp = cpu_to_le16(p_conn->tx_max_fc_pay_len);
	p_data->tx_max_fc_pay_len = tmp;
	tmp = cpu_to_le16(p_conn->e_d_tov_timer_val);
	p_data->e_d_tov_timer_val = tmp;
	tmp = cpu_to_le16(p_conn->rec_tov_timer_val);
	p_data->rec_rr_tov_timer_val = tmp;
	tmp = cpu_to_le16(p_conn->rx_max_fc_pay_len);
	p_data->rx_max_fc_pay_len = tmp;

	p_data->vlan_tag = cpu_to_le16(p_conn->vlan_tag);
	p_data->s_id.addr_hi = p_conn->s_id.addr_hi;
	p_data->s_id.addr_mid = p_conn->s_id.addr_mid;
	p_data->s_id.addr_lo = p_conn->s_id.addr_lo;
	p_data->max_conc_seqs_c3 = p_conn->max_conc_seqs_c3;
	p_data->d_id.addr_hi = p_conn->d_id.addr_hi;
	p_data->d_id.addr_mid = p_conn->d_id.addr_mid;
	p_data->d_id.addr_lo = p_conn->d_id.addr_lo;
	p_data->flags = p_conn->flags;
	if (test_bit(QED_MF_UFP_SPECIFIC, &p_hwfn->cdev->mf_bits))
		SET_FIELD(p_data->flags,
			  FCOE_CONN_OFFLOAD_RAMROD_DATA_B_SINGLE_VLAN, 1);
	p_data->def_q_idx = p_conn->def_q_idx;

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_sp_fcoe_conn_destroy(struct qed_hwfn *p_hwfn,
			 struct qed_fcoe_conn *p_conn,
			 enum spq_mode comp_mode,
			 struct qed_spq_comp_cb *p_comp_addr)
{
	struct fcoe_conn_terminate_ramrod_params *p_ramrod = NULL;
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	int rc = 0;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_conn->icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 FCOE_RAMROD_CMD_ID_TERMINATE_CONN,
				 PROTOCOLID_FCOE, &init_data);
	if (rc)
		return rc;

	p_ramrod = &p_ent->ramrod.fcoe_conn_terminate;
	DMA_REGPAIR_LE(p_ramrod->terminate_ramrod_data.terminate_params_addr,
		       p_conn->terminate_params);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_sp_fcoe_func_stop(struct qed_hwfn *p_hwfn,
		      struct qed_ptt *p_ptt,
		      enum spq_mode comp_mode,
		      struct qed_spq_comp_cb *p_comp_addr)
{
	struct qed_spq_entry *p_ent = NULL;
	struct qed_sp_init_data init_data;
	u32 active_segs = 0;
	int rc = 0;

	/* Get SPQ entry */
	memset(&init_data, 0, sizeof(init_data));
	init_data.cid = p_hwfn->pf_params.fcoe_pf_params.dummy_icid;
	init_data.opaque_fid = p_hwfn->hw_info.opaque_fid;
	init_data.comp_mode = comp_mode;
	init_data.p_comp_data = p_comp_addr;

	rc = qed_sp_init_request(p_hwfn, &p_ent,
				 FCOE_RAMROD_CMD_ID_DESTROY_FUNC,
				 PROTOCOLID_FCOE, &init_data);
	if (rc)
		return rc;

	active_segs = qed_rd(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK);
	active_segs &= ~BIT(QED_CXT_FCOE_TID_SEG);
	qed_wr(p_hwfn, p_ptt, TM_REG_PF_ENABLE_TASK, active_segs);

	return qed_spq_post(p_hwfn, p_ent, NULL);
}

static int
qed_fcoe_allocate_connection(struct qed_hwfn *p_hwfn,
			     struct qed_fcoe_conn **p_out_conn)
{
	struct qed_fcoe_conn *p_conn = NULL;
	void *p_addr;
	u32 i;

	spin_lock_bh(&p_hwfn->p_fcoe_info->lock);
	if (!list_empty(&p_hwfn->p_fcoe_info->free_list))
		p_conn =
		    list_first_entry(&p_hwfn->p_fcoe_info->free_list,
				     struct qed_fcoe_conn, list_entry);
	if (p_conn) {
		list_del(&p_conn->list_entry);
		spin_unlock_bh(&p_hwfn->p_fcoe_info->lock);
		*p_out_conn = p_conn;
		return 0;
	}
	spin_unlock_bh(&p_hwfn->p_fcoe_info->lock);

	p_conn = kzalloc(sizeof(*p_conn), GFP_KERNEL);
	if (!p_conn)
		return -ENOMEM;

	p_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    QED_CHAIN_PAGE_SIZE,
				    &p_conn->xferq_pbl_addr, GFP_KERNEL);
	if (!p_addr)
		goto nomem_pbl_xferq;
	p_conn->xferq_pbl_addr_virt_addr = p_addr;

	for (i = 0; i < ARRAY_SIZE(p_conn->xferq_addr); i++) {
		p_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_conn->xferq_addr[i], GFP_KERNEL);
		if (!p_addr)
			goto nomem_xferq;
		p_conn->xferq_addr_virt_addr[i] = p_addr;

		p_addr = p_conn->xferq_pbl_addr_virt_addr;
		((dma_addr_t *)p_addr)[i] = p_conn->xferq_addr[i];
	}

	p_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
				    QED_CHAIN_PAGE_SIZE,
				    &p_conn->confq_pbl_addr, GFP_KERNEL);
	if (!p_addr)
		goto nomem_xferq;
	p_conn->confq_pbl_addr_virt_addr = p_addr;

	for (i = 0; i < ARRAY_SIZE(p_conn->confq_addr); i++) {
		p_addr = dma_alloc_coherent(&p_hwfn->cdev->pdev->dev,
					    QED_CHAIN_PAGE_SIZE,
					    &p_conn->confq_addr[i], GFP_KERNEL);
		if (!p_addr)
			goto nomem_confq;
		p_conn->confq_addr_virt_addr[i] = p_addr;

		p_addr = p_conn->confq_pbl_addr_virt_addr;
		((dma_addr_t *)p_addr)[i] = p_conn->confq_addr[i];
	}

	p_conn->free_on_delete = true;
	*p_out_conn = p_conn;
	return 0;

nomem_confq:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  QED_CHAIN_PAGE_SIZE,
			  p_conn->confq_pbl_addr_virt_addr,
			  p_conn->confq_pbl_addr);
	for (i = 0; i < ARRAY_SIZE(p_conn->confq_addr); i++)
		if (p_conn->confq_addr_virt_addr[i])
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  QED_CHAIN_PAGE_SIZE,
					  p_conn->confq_addr_virt_addr[i],
					  p_conn->confq_addr[i]);
nomem_xferq:
	dma_free_coherent(&p_hwfn->cdev->pdev->dev,
			  QED_CHAIN_PAGE_SIZE,
			  p_conn->xferq_pbl_addr_virt_addr,
			  p_conn->xferq_pbl_addr);
	for (i = 0; i < ARRAY_SIZE(p_conn->xferq_addr); i++)
		if (p_conn->xferq_addr_virt_addr[i])
			dma_free_coherent(&p_hwfn->cdev->pdev->dev,
					  QED_CHAIN_PAGE_SIZE,
					  p_conn->xferq_addr_virt_addr[i],
					  p_conn->xferq_addr[i]);
nomem_pbl_xferq:
	kfree(p_conn);
	return -ENOMEM;
}

static void qed_fcoe_free_connection(struct qed_hwfn *p_hwfn,
				     struct qed_fcoe_conn *p_conn)
{
	u32 i;

	if (!p_conn)
		return;

	if (p_conn->confq_pbl_addr_virt_addr)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  p_conn->confq_pbl_addr_virt_addr,
				  p_conn->confq_pbl_addr);

	for (i = 0; i < ARRAY_SIZE(p_conn->confq_addr); i++) {
		if (!p_conn->confq_addr_virt_addr[i])
			continue;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  p_conn->confq_addr_virt_addr[i],
				  p_conn->confq_addr[i]);
	}

	if (p_conn->xferq_pbl_addr_virt_addr)
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  p_conn->xferq_pbl_addr_virt_addr,
				  p_conn->xferq_pbl_addr);

	for (i = 0; i < ARRAY_SIZE(p_conn->xferq_addr); i++) {
		if (!p_conn->xferq_addr_virt_addr[i])
			continue;
		dma_free_coherent(&p_hwfn->cdev->pdev->dev,
				  QED_CHAIN_PAGE_SIZE,
				  p_conn->xferq_addr_virt_addr[i],
				  p_conn->xferq_addr[i]);
	}
	kfree(p_conn);
}

static void __iomem *qed_fcoe_get_db_addr(struct qed_hwfn *p_hwfn, u32 cid)
{
	return (u8 __iomem *)p_hwfn->doorbells +
	       qed_db_addr(cid, DQ_DEMS_LEGACY);
}

static void __iomem *qed_fcoe_get_primary_bdq_prod(struct qed_hwfn *p_hwfn,
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

static void __iomem *qed_fcoe_get_secondary_bdq_prod(struct qed_hwfn *p_hwfn,
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

int qed_fcoe_alloc(struct qed_hwfn *p_hwfn)
{
	struct qed_fcoe_info *p_fcoe_info;

	/* Allocate LL2's set struct */
	p_fcoe_info = kzalloc(sizeof(*p_fcoe_info), GFP_KERNEL);
	if (!p_fcoe_info) {
		DP_NOTICE(p_hwfn, "Failed to allocate qed_fcoe_info'\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&p_fcoe_info->free_list);

	p_hwfn->p_fcoe_info = p_fcoe_info;
	return 0;
}

void qed_fcoe_setup(struct qed_hwfn *p_hwfn)
{
	struct e4_fcoe_task_context *p_task_ctx = NULL;
	int rc;
	u32 i;

	spin_lock_init(&p_hwfn->p_fcoe_info->lock);
	for (i = 0; i < p_hwfn->pf_params.fcoe_pf_params.num_tasks; i++) {
		rc = qed_cxt_get_task_ctx(p_hwfn, i,
					  QED_CTX_WORKING_MEM,
					  (void **)&p_task_ctx);
		if (rc)
			continue;

		memset(p_task_ctx, 0, sizeof(struct e4_fcoe_task_context));
		SET_FIELD(p_task_ctx->timer_context.logical_client_0,
			  TIMERS_CONTEXT_VALIDLC0, 1);
		SET_FIELD(p_task_ctx->timer_context.logical_client_1,
			  TIMERS_CONTEXT_VALIDLC1, 1);
		SET_FIELD(p_task_ctx->tstorm_ag_context.flags0,
			  E4_TSTORM_FCOE_TASK_AG_CTX_CONNECTION_TYPE, 1);
	}
}

void qed_fcoe_free(struct qed_hwfn *p_hwfn)
{
	struct qed_fcoe_conn *p_conn = NULL;

	if (!p_hwfn->p_fcoe_info)
		return;

	while (!list_empty(&p_hwfn->p_fcoe_info->free_list)) {
		p_conn = list_first_entry(&p_hwfn->p_fcoe_info->free_list,
					  struct qed_fcoe_conn, list_entry);
		if (!p_conn)
			break;
		list_del(&p_conn->list_entry);
		qed_fcoe_free_connection(p_hwfn, p_conn);
	}

	kfree(p_hwfn->p_fcoe_info);
	p_hwfn->p_fcoe_info = NULL;
}

static int
qed_fcoe_acquire_connection(struct qed_hwfn *p_hwfn,
			    struct qed_fcoe_conn *p_in_conn,
			    struct qed_fcoe_conn **p_out_conn)
{
	struct qed_fcoe_conn *p_conn = NULL;
	int rc = 0;
	u32 icid;

	spin_lock_bh(&p_hwfn->p_fcoe_info->lock);
	rc = qed_cxt_acquire_cid(p_hwfn, PROTOCOLID_FCOE, &icid);
	spin_unlock_bh(&p_hwfn->p_fcoe_info->lock);
	if (rc)
		return rc;

	/* Use input connection [if provided] or allocate a new one */
	if (p_in_conn) {
		p_conn = p_in_conn;
	} else {
		rc = qed_fcoe_allocate_connection(p_hwfn, &p_conn);
		if (rc) {
			spin_lock_bh(&p_hwfn->p_fcoe_info->lock);
			qed_cxt_release_cid(p_hwfn, icid);
			spin_unlock_bh(&p_hwfn->p_fcoe_info->lock);
			return rc;
		}
	}

	p_conn->icid = icid;
	p_conn->fw_cid = (p_hwfn->hw_info.opaque_fid << 16) | icid;
	*p_out_conn = p_conn;

	return rc;
}

static void qed_fcoe_release_connection(struct qed_hwfn *p_hwfn,
					struct qed_fcoe_conn *p_conn)
{
	spin_lock_bh(&p_hwfn->p_fcoe_info->lock);
	list_add_tail(&p_conn->list_entry, &p_hwfn->p_fcoe_info->free_list);
	qed_cxt_release_cid(p_hwfn, p_conn->icid);
	spin_unlock_bh(&p_hwfn->p_fcoe_info->lock);
}

static void _qed_fcoe_get_tstats(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_fcoe_stats *p_stats)
{
	struct fcoe_rx_stat tstats;
	u32 tstats_addr;

	memset(&tstats, 0, sizeof(tstats));
	tstats_addr = BAR0_MAP_REG_TSDM_RAM +
	    TSTORM_FCOE_RX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &tstats, tstats_addr, sizeof(tstats));

	p_stats->fcoe_rx_byte_cnt = HILO_64_REGPAIR(tstats.fcoe_rx_byte_cnt);
	p_stats->fcoe_rx_data_pkt_cnt =
	    HILO_64_REGPAIR(tstats.fcoe_rx_data_pkt_cnt);
	p_stats->fcoe_rx_xfer_pkt_cnt =
	    HILO_64_REGPAIR(tstats.fcoe_rx_xfer_pkt_cnt);
	p_stats->fcoe_rx_other_pkt_cnt =
	    HILO_64_REGPAIR(tstats.fcoe_rx_other_pkt_cnt);

	p_stats->fcoe_silent_drop_pkt_cmdq_full_cnt =
	    le32_to_cpu(tstats.fcoe_silent_drop_pkt_cmdq_full_cnt);
	p_stats->fcoe_silent_drop_pkt_rq_full_cnt =
	    le32_to_cpu(tstats.fcoe_silent_drop_pkt_rq_full_cnt);
	p_stats->fcoe_silent_drop_pkt_crc_error_cnt =
	    le32_to_cpu(tstats.fcoe_silent_drop_pkt_crc_error_cnt);
	p_stats->fcoe_silent_drop_pkt_task_invalid_cnt =
	    le32_to_cpu(tstats.fcoe_silent_drop_pkt_task_invalid_cnt);
	p_stats->fcoe_silent_drop_total_pkt_cnt =
	    le32_to_cpu(tstats.fcoe_silent_drop_total_pkt_cnt);
}

static void _qed_fcoe_get_pstats(struct qed_hwfn *p_hwfn,
				 struct qed_ptt *p_ptt,
				 struct qed_fcoe_stats *p_stats)
{
	struct fcoe_tx_stat pstats;
	u32 pstats_addr;

	memset(&pstats, 0, sizeof(pstats));
	pstats_addr = BAR0_MAP_REG_PSDM_RAM +
	    PSTORM_FCOE_TX_STATS_OFFSET(p_hwfn->rel_pf_id);
	qed_memcpy_from(p_hwfn, p_ptt, &pstats, pstats_addr, sizeof(pstats));

	p_stats->fcoe_tx_byte_cnt = HILO_64_REGPAIR(pstats.fcoe_tx_byte_cnt);
	p_stats->fcoe_tx_data_pkt_cnt =
	    HILO_64_REGPAIR(pstats.fcoe_tx_data_pkt_cnt);
	p_stats->fcoe_tx_xfer_pkt_cnt =
	    HILO_64_REGPAIR(pstats.fcoe_tx_xfer_pkt_cnt);
	p_stats->fcoe_tx_other_pkt_cnt =
	    HILO_64_REGPAIR(pstats.fcoe_tx_other_pkt_cnt);
}

static int qed_fcoe_get_stats(struct qed_hwfn *p_hwfn,
			      struct qed_fcoe_stats *p_stats)
{
	struct qed_ptt *p_ptt;

	memset(p_stats, 0, sizeof(*p_stats));

	p_ptt = qed_ptt_acquire(p_hwfn);

	if (!p_ptt) {
		DP_ERR(p_hwfn, "Failed to acquire ptt\n");
		return -EINVAL;
	}

	_qed_fcoe_get_tstats(p_hwfn, p_ptt, p_stats);
	_qed_fcoe_get_pstats(p_hwfn, p_ptt, p_stats);

	qed_ptt_release(p_hwfn, p_ptt);

	return 0;
}

struct qed_hash_fcoe_con {
	struct hlist_node node;
	struct qed_fcoe_conn *con;
};

static int qed_fill_fcoe_dev_info(struct qed_dev *cdev,
				  struct qed_dev_fcoe_info *info)
{
	struct qed_hwfn *hwfn = QED_AFFIN_HWFN(cdev);
	int rc;

	memset(info, 0, sizeof(*info));
	rc = qed_fill_dev_info(cdev, &info->common);

	info->primary_dbq_rq_addr =
	    qed_fcoe_get_primary_bdq_prod(hwfn, BDQ_ID_RQ);
	info->secondary_bdq_rq_addr =
	    qed_fcoe_get_secondary_bdq_prod(hwfn, BDQ_ID_RQ);

	info->wwpn = hwfn->mcp_info->func_info.wwn_port;
	info->wwnn = hwfn->mcp_info->func_info.wwn_node;

	info->num_cqs = FEAT_NUM(hwfn, QED_FCOE_CQ);

	return rc;
}

static void qed_register_fcoe_ops(struct qed_dev *cdev,
				  struct qed_fcoe_cb_ops *ops, void *cookie)
{
	cdev->protocol_ops.fcoe = ops;
	cdev->ops_cookie = cookie;
}

static struct qed_hash_fcoe_con *qed_fcoe_get_hash(struct qed_dev *cdev,
						   u32 handle)
{
	struct qed_hash_fcoe_con *hash_con = NULL;

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

static int qed_fcoe_stop(struct qed_dev *cdev)
{
	struct qed_ptt *p_ptt;
	int rc;

	if (!(cdev->flags & QED_FLAG_STORAGE_STARTED)) {
		DP_NOTICE(cdev, "fcoe already stopped\n");
		return 0;
	}

	if (!hash_empty(cdev->connections)) {
		DP_NOTICE(cdev,
			  "Can't stop fcoe - not all connections were returned\n");
		return -EINVAL;
	}

	p_ptt = qed_ptt_acquire(QED_AFFIN_HWFN(cdev));
	if (!p_ptt)
		return -EAGAIN;

	/* Stop the fcoe */
	rc = qed_sp_fcoe_func_stop(QED_AFFIN_HWFN(cdev), p_ptt,
				   QED_SPQ_MODE_EBLOCK, NULL);
	cdev->flags &= ~QED_FLAG_STORAGE_STARTED;
	qed_ptt_release(QED_AFFIN_HWFN(cdev), p_ptt);

	return rc;
}

static int qed_fcoe_start(struct qed_dev *cdev, struct qed_fcoe_tid *tasks)
{
	int rc;

	if (cdev->flags & QED_FLAG_STORAGE_STARTED) {
		DP_NOTICE(cdev, "fcoe already started;\n");
		return 0;
	}

	rc = qed_sp_fcoe_func_start(QED_AFFIN_HWFN(cdev), QED_SPQ_MODE_EBLOCK,
				    NULL);
	if (rc) {
		DP_NOTICE(cdev, "Failed to start fcoe\n");
		return rc;
	}

	cdev->flags |= QED_FLAG_STORAGE_STARTED;
	hash_init(cdev->connections);

	if (tasks) {
		struct qed_tid_mem *tid_info = kzalloc(sizeof(*tid_info),
						       GFP_ATOMIC);

		if (!tid_info) {
			DP_NOTICE(cdev,
				  "Failed to allocate tasks information\n");
			qed_fcoe_stop(cdev);
			return -ENOMEM;
		}

		rc = qed_cxt_get_tid_mem_info(QED_AFFIN_HWFN(cdev), tid_info);
		if (rc) {
			DP_NOTICE(cdev, "Failed to gather task information\n");
			qed_fcoe_stop(cdev);
			kfree(tid_info);
			return rc;
		}

		/* Fill task information */
		tasks->size = tid_info->tid_size;
		tasks->num_tids_per_block = tid_info->num_tids_per_block;
		memcpy(tasks->blocks, tid_info->blocks,
		       MAX_TID_BLOCKS_FCOE * sizeof(u8 *));

		kfree(tid_info);
	}

	return 0;
}

static int qed_fcoe_acquire_conn(struct qed_dev *cdev,
				 u32 *handle,
				 u32 *fw_cid, void __iomem **p_doorbell)
{
	struct qed_hash_fcoe_con *hash_con;
	int rc;

	/* Allocate a hashed connection */
	hash_con = kzalloc(sizeof(*hash_con), GFP_KERNEL);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to allocate hashed connection\n");
		return -ENOMEM;
	}

	/* Acquire the connection */
	rc = qed_fcoe_acquire_connection(QED_AFFIN_HWFN(cdev), NULL,
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
		*p_doorbell = qed_fcoe_get_db_addr(QED_AFFIN_HWFN(cdev),
						   *handle);

	return 0;
}

static int qed_fcoe_release_conn(struct qed_dev *cdev, u32 handle)
{
	struct qed_hash_fcoe_con *hash_con;

	hash_con = qed_fcoe_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	hlist_del(&hash_con->node);
	qed_fcoe_release_connection(QED_AFFIN_HWFN(cdev), hash_con->con);
	kfree(hash_con);

	return 0;
}

static int qed_fcoe_offload_conn(struct qed_dev *cdev,
				 u32 handle,
				 struct qed_fcoe_params_offload *conn_info)
{
	struct qed_hash_fcoe_con *hash_con;
	struct qed_fcoe_conn *con;

	hash_con = qed_fcoe_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;

	con->sq_pbl_addr = conn_info->sq_pbl_addr;
	con->sq_curr_page_addr = conn_info->sq_curr_page_addr;
	con->sq_next_page_addr = conn_info->sq_next_page_addr;
	con->tx_max_fc_pay_len = conn_info->tx_max_fc_pay_len;
	con->e_d_tov_timer_val = conn_info->e_d_tov_timer_val;
	con->rec_tov_timer_val = conn_info->rec_tov_timer_val;
	con->rx_max_fc_pay_len = conn_info->rx_max_fc_pay_len;
	con->vlan_tag = conn_info->vlan_tag;
	con->max_conc_seqs_c3 = conn_info->max_conc_seqs_c3;
	con->flags = conn_info->flags;
	con->def_q_idx = conn_info->def_q_idx;

	con->src_mac_addr_hi = (conn_info->src_mac[5] << 8) |
	    conn_info->src_mac[4];
	con->src_mac_addr_mid = (conn_info->src_mac[3] << 8) |
	    conn_info->src_mac[2];
	con->src_mac_addr_lo = (conn_info->src_mac[1] << 8) |
	    conn_info->src_mac[0];
	con->dst_mac_addr_hi = (conn_info->dst_mac[5] << 8) |
	    conn_info->dst_mac[4];
	con->dst_mac_addr_mid = (conn_info->dst_mac[3] << 8) |
	    conn_info->dst_mac[2];
	con->dst_mac_addr_lo = (conn_info->dst_mac[1] << 8) |
	    conn_info->dst_mac[0];

	con->s_id.addr_hi = conn_info->s_id.addr_hi;
	con->s_id.addr_mid = conn_info->s_id.addr_mid;
	con->s_id.addr_lo = conn_info->s_id.addr_lo;
	con->d_id.addr_hi = conn_info->d_id.addr_hi;
	con->d_id.addr_mid = conn_info->d_id.addr_mid;
	con->d_id.addr_lo = conn_info->d_id.addr_lo;

	return qed_sp_fcoe_conn_offload(QED_AFFIN_HWFN(cdev), con,
					QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_fcoe_destroy_conn(struct qed_dev *cdev,
				 u32 handle, dma_addr_t terminate_params)
{
	struct qed_hash_fcoe_con *hash_con;
	struct qed_fcoe_conn *con;

	hash_con = qed_fcoe_get_hash(cdev, handle);
	if (!hash_con) {
		DP_NOTICE(cdev, "Failed to find connection for handle %d\n",
			  handle);
		return -EINVAL;
	}

	/* Update the connection with information from the params */
	con = hash_con->con;
	con->terminate_params = terminate_params;

	return qed_sp_fcoe_conn_destroy(QED_AFFIN_HWFN(cdev), con,
					QED_SPQ_MODE_EBLOCK, NULL);
}

static int qed_fcoe_stats(struct qed_dev *cdev, struct qed_fcoe_stats *stats)
{
	return qed_fcoe_get_stats(QED_AFFIN_HWFN(cdev), stats);
}

void qed_get_protocol_stats_fcoe(struct qed_dev *cdev,
				 struct qed_mcp_fcoe_stats *stats)
{
	struct qed_fcoe_stats proto_stats;

	/* Retrieve FW statistics */
	memset(&proto_stats, 0, sizeof(proto_stats));
	if (qed_fcoe_stats(cdev, &proto_stats)) {
		DP_VERBOSE(cdev, QED_MSG_STORAGE,
			   "Failed to collect FCoE statistics\n");
		return;
	}

	/* Translate FW statistics into struct */
	stats->rx_pkts = proto_stats.fcoe_rx_data_pkt_cnt +
			 proto_stats.fcoe_rx_xfer_pkt_cnt +
			 proto_stats.fcoe_rx_other_pkt_cnt;
	stats->tx_pkts = proto_stats.fcoe_tx_data_pkt_cnt +
			 proto_stats.fcoe_tx_xfer_pkt_cnt +
			 proto_stats.fcoe_tx_other_pkt_cnt;
	stats->fcs_err = proto_stats.fcoe_silent_drop_pkt_crc_error_cnt;

	/* Request protocol driver to fill-in the rest */
	if (cdev->protocol_ops.fcoe && cdev->ops_cookie) {
		struct qed_fcoe_cb_ops *ops = cdev->protocol_ops.fcoe;
		void *cookie = cdev->ops_cookie;

		if (ops->get_login_failures)
			stats->login_failure = ops->get_login_failures(cookie);
	}
}

static const struct qed_fcoe_ops qed_fcoe_ops_pass = {
	.common = &qed_common_ops_pass,
	.ll2 = &qed_ll2_ops_pass,
	.fill_dev_info = &qed_fill_fcoe_dev_info,
	.start = &qed_fcoe_start,
	.stop = &qed_fcoe_stop,
	.register_ops = &qed_register_fcoe_ops,
	.acquire_conn = &qed_fcoe_acquire_conn,
	.release_conn = &qed_fcoe_release_conn,
	.offload_conn = &qed_fcoe_offload_conn,
	.destroy_conn = &qed_fcoe_destroy_conn,
	.get_stats = &qed_fcoe_stats,
};

const struct qed_fcoe_ops *qed_get_fcoe_ops(void)
{
	return &qed_fcoe_ops_pass;
}
EXPORT_SYMBOL(qed_get_fcoe_ops);

void qed_put_fcoe_ops(void)
{
}
EXPORT_SYMBOL(qed_put_fcoe_ops);
