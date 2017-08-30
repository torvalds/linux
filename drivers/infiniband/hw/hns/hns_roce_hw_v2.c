/*
 * Copyright (c) 2016-2017 Hisilicon Limited.
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
 */

#include <linux/acpi.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <rdma/ib_umem.h>

#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"
#include "hns_roce_hw_v2.h"

static int hns_roce_cmq_space(struct hns_roce_v2_cmq_ring *ring)
{
	int ntu = ring->next_to_use;
	int ntc = ring->next_to_clean;
	int used = (ntu - ntc + ring->desc_num) % ring->desc_num;

	return ring->desc_num - used - 1;
}

static int hns_roce_alloc_cmq_desc(struct hns_roce_dev *hr_dev,
				   struct hns_roce_v2_cmq_ring *ring)
{
	int size = ring->desc_num * sizeof(struct hns_roce_cmq_desc);

	ring->desc = kzalloc(size, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	ring->desc_dma_addr = dma_map_single(hr_dev->dev, ring->desc, size,
					     DMA_BIDIRECTIONAL);
	if (dma_mapping_error(hr_dev->dev, ring->desc_dma_addr)) {
		ring->desc_dma_addr = 0;
		kfree(ring->desc);
		ring->desc = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void hns_roce_free_cmq_desc(struct hns_roce_dev *hr_dev,
				   struct hns_roce_v2_cmq_ring *ring)
{
	dma_unmap_single(hr_dev->dev, ring->desc_dma_addr,
			 ring->desc_num * sizeof(struct hns_roce_cmq_desc),
			 DMA_BIDIRECTIONAL);
	kfree(ring->desc);
}

static int hns_roce_init_cmq_ring(struct hns_roce_dev *hr_dev, bool ring_type)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hns_roce_v2_cmq_ring *ring = (ring_type == TYPE_CSQ) ?
					    &priv->cmq.csq : &priv->cmq.crq;

	ring->flag = ring_type;
	ring->next_to_clean = 0;
	ring->next_to_use = 0;

	return hns_roce_alloc_cmq_desc(hr_dev, ring);
}

static void hns_roce_cmq_init_regs(struct hns_roce_dev *hr_dev, bool ring_type)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hns_roce_v2_cmq_ring *ring = (ring_type == TYPE_CSQ) ?
					    &priv->cmq.csq : &priv->cmq.crq;
	dma_addr_t dma = ring->desc_dma_addr;

	if (ring_type == TYPE_CSQ) {
		roce_write(hr_dev, ROCEE_TX_CMQ_BASEADDR_L_REG, (u32)dma);
		roce_write(hr_dev, ROCEE_TX_CMQ_BASEADDR_H_REG,
			   upper_32_bits(dma));
		roce_write(hr_dev, ROCEE_TX_CMQ_DEPTH_REG,
			  (ring->desc_num >> HNS_ROCE_CMQ_DESC_NUM_S) |
			   HNS_ROCE_CMQ_ENABLE);
		roce_write(hr_dev, ROCEE_TX_CMQ_HEAD_REG, 0);
		roce_write(hr_dev, ROCEE_TX_CMQ_TAIL_REG, 0);
	} else {
		roce_write(hr_dev, ROCEE_RX_CMQ_BASEADDR_L_REG, (u32)dma);
		roce_write(hr_dev, ROCEE_RX_CMQ_BASEADDR_H_REG,
			   upper_32_bits(dma));
		roce_write(hr_dev, ROCEE_RX_CMQ_DEPTH_REG,
			  (ring->desc_num >> HNS_ROCE_CMQ_DESC_NUM_S) |
			   HNS_ROCE_CMQ_ENABLE);
		roce_write(hr_dev, ROCEE_RX_CMQ_HEAD_REG, 0);
		roce_write(hr_dev, ROCEE_RX_CMQ_TAIL_REG, 0);
	}
}

static int hns_roce_v2_cmq_init(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	int ret;

	/* Setup the queue entries for command queue */
	priv->cmq.csq.desc_num = 1024;
	priv->cmq.crq.desc_num = 1024;

	/* Setup the lock for command queue */
	spin_lock_init(&priv->cmq.csq.lock);
	spin_lock_init(&priv->cmq.crq.lock);

	/* Setup Tx write back timeout */
	priv->cmq.tx_timeout = HNS_ROCE_CMQ_TX_TIMEOUT;

	/* Init CSQ */
	ret = hns_roce_init_cmq_ring(hr_dev, TYPE_CSQ);
	if (ret) {
		dev_err(hr_dev->dev, "Init CSQ error, ret = %d.\n", ret);
		return ret;
	}

	/* Init CRQ */
	ret = hns_roce_init_cmq_ring(hr_dev, TYPE_CRQ);
	if (ret) {
		dev_err(hr_dev->dev, "Init CRQ error, ret = %d.\n", ret);
		goto err_crq;
	}

	/* Init CSQ REG */
	hns_roce_cmq_init_regs(hr_dev, TYPE_CSQ);

	/* Init CRQ REG */
	hns_roce_cmq_init_regs(hr_dev, TYPE_CRQ);

	return 0;

err_crq:
	hns_roce_free_cmq_desc(hr_dev, &priv->cmq.csq);

	return ret;
}

static void hns_roce_v2_cmq_exit(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;

	hns_roce_free_cmq_desc(hr_dev, &priv->cmq.csq);
	hns_roce_free_cmq_desc(hr_dev, &priv->cmq.crq);
}

void hns_roce_cmq_setup_basic_desc(struct hns_roce_cmq_desc *desc,
				   enum hns_roce_opcode_type opcode,
				   bool is_read)
{
	memset((void *)desc, 0, sizeof(struct hns_roce_cmq_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flag =
		cpu_to_le16(HNS_ROCE_CMD_FLAG_NO_INTR | HNS_ROCE_CMD_FLAG_IN);
	if (is_read)
		desc->flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HNS_ROCE_CMD_FLAG_WR);
}

static int hns_roce_cmq_csq_done(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	u32 head = roce_read(hr_dev, ROCEE_TX_CMQ_HEAD_REG);

	return head == priv->cmq.csq.next_to_use;
}

static int hns_roce_cmq_csq_clean(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hns_roce_v2_cmq_ring *csq = &priv->cmq.csq;
	struct hns_roce_cmq_desc *desc;
	u16 ntc = csq->next_to_clean;
	u32 head;
	int clean = 0;

	desc = &csq->desc[ntc];
	head = roce_read(hr_dev, ROCEE_TX_CMQ_HEAD_REG);
	while (head != ntc) {
		memset(desc, 0, sizeof(*desc));
		ntc++;
		if (ntc == csq->desc_num)
			ntc = 0;
		desc = &csq->desc[ntc];
		clean++;
	}
	csq->next_to_clean = ntc;

	return clean;
}

int hns_roce_cmq_send(struct hns_roce_dev *hr_dev,
		      struct hns_roce_cmq_desc *desc, int num)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hns_roce_v2_cmq_ring *csq = &priv->cmq.csq;
	struct hns_roce_cmq_desc *desc_to_use;
	bool complete = false;
	u32 timeout = 0;
	int handle = 0;
	u16 desc_ret;
	int ret = 0;
	int ntc;

	spin_lock_bh(&csq->lock);

	if (num > hns_roce_cmq_space(csq)) {
		spin_unlock_bh(&csq->lock);
		return -EBUSY;
	}

	/*
	 * Record the location of desc in the cmq for this time
	 * which will be use for hardware to write back
	 */
	ntc = csq->next_to_use;

	while (handle < num) {
		desc_to_use = &csq->desc[csq->next_to_use];
		*desc_to_use = desc[handle];
		dev_dbg(hr_dev->dev, "set cmq desc:\n");
		csq->next_to_use++;
		if (csq->next_to_use == csq->desc_num)
			csq->next_to_use = 0;
		handle++;
	}

	/* Write to hardware */
	roce_write(hr_dev, ROCEE_TX_CMQ_TAIL_REG, csq->next_to_use);

	/*
	 * If the command is sync, wait for the firmware to write back,
	 * if multi descriptors to be sent, use the first one to check
	 */
	if ((desc->flag) & HNS_ROCE_CMD_FLAG_NO_INTR) {
		do {
			if (hns_roce_cmq_csq_done(hr_dev))
				break;
			usleep_range(1000, 2000);
			timeout++;
		} while (timeout < priv->cmq.tx_timeout);
	}

	if (hns_roce_cmq_csq_done(hr_dev)) {
		complete = true;
		handle = 0;
		while (handle < num) {
			/* get the result of hardware write back */
			desc_to_use = &csq->desc[ntc];
			desc[handle] = *desc_to_use;
			dev_dbg(hr_dev->dev, "Get cmq desc:\n");
			desc_ret = desc[handle].retval;
			if (desc_ret == CMD_EXEC_SUCCESS)
				ret = 0;
			else
				ret = -EIO;
			priv->cmq.last_status = desc_ret;
			ntc++;
			handle++;
			if (ntc == csq->desc_num)
				ntc = 0;
		}
	}

	if (!complete)
		ret = -EAGAIN;

	/* clean the command send queue */
	handle = hns_roce_cmq_csq_clean(hr_dev);
	if (handle != num)
		dev_warn(hr_dev->dev, "Cleaned %d, need to clean %d\n",
			 handle, num);

	spin_unlock_bh(&csq->lock);

	return ret;
}

int hns_roce_cmq_query_hw_info(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_query_version *resp;
	struct hns_roce_cmq_desc desc;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_HW_VER, true);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	resp = (struct hns_roce_query_version *)desc.data;
	hr_dev->hw_rev = le32_to_cpu(resp->rocee_hw_version);
	hr_dev->vendor_id = le32_to_cpu(resp->rocee_vendor_id);

	return 0;
}

static int hns_roce_config_global_param(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cfg_global_param *req;
	struct hns_roce_cmq_desc desc;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_GLOBAL_PARAM,
				      false);

	req = (struct hns_roce_cfg_global_param *)desc.data;
	memset(req, 0, sizeof(*req));
	roce_set_field(req->time_cfg_udp_port,
		       CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_M,
		       CFG_GLOBAL_PARAM_DATA_0_ROCEE_TIME_1US_CFG_S, 0x3e8);
	roce_set_field(req->time_cfg_udp_port,
		       CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_M,
		       CFG_GLOBAL_PARAM_DATA_0_ROCEE_UDP_PORT_S, 0x12b7);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int hns_roce_query_pf_resource(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_pf_res *res;
	int ret;
	int i;

	for (i = 0; i < 2; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i],
					      HNS_ROCE_OPC_QUERY_PF_RES, true);

		if (i == 0)
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	}

	ret = hns_roce_cmq_send(hr_dev, desc, 2);
	if (ret)
		return ret;

	res = (struct hns_roce_pf_res *)desc[0].data;

	hr_dev->caps.qpc_bt_num = roce_get_field(res->qpc_bt_idx_num,
						 PF_RES_DATA_1_PF_QPC_BT_NUM_M,
						 PF_RES_DATA_1_PF_QPC_BT_NUM_S);
	hr_dev->caps.srqc_bt_num = roce_get_field(res->srqc_bt_idx_num,
						PF_RES_DATA_2_PF_SRQC_BT_NUM_M,
						PF_RES_DATA_2_PF_SRQC_BT_NUM_S);
	hr_dev->caps.cqc_bt_num = roce_get_field(res->cqc_bt_idx_num,
						 PF_RES_DATA_3_PF_CQC_BT_NUM_M,
						 PF_RES_DATA_3_PF_CQC_BT_NUM_S);
	hr_dev->caps.mpt_bt_num = roce_get_field(res->mpt_bt_idx_num,
						 PF_RES_DATA_4_PF_MPT_BT_NUM_M,
						 PF_RES_DATA_4_PF_MPT_BT_NUM_S);

	return 0;
}

static int hns_roce_alloc_vf_resource(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_vf_res_a *req_a;
	struct hns_roce_vf_res_b *req_b;
	int i;

	req_a = (struct hns_roce_vf_res_a *)desc[0].data;
	req_b = (struct hns_roce_vf_res_b *)desc[1].data;
	memset(req_a, 0, sizeof(*req_a));
	memset(req_b, 0, sizeof(*req_b));
	for (i = 0; i < 2; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i],
					      HNS_ROCE_OPC_ALLOC_VF_RES, false);

		if (i == 0)
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);

		if (i == 0) {
			roce_set_field(req_a->vf_qpc_bt_idx_num,
				       VF_RES_A_DATA_1_VF_QPC_BT_IDX_M,
				       VF_RES_A_DATA_1_VF_QPC_BT_IDX_S, 0);
			roce_set_field(req_a->vf_qpc_bt_idx_num,
				       VF_RES_A_DATA_1_VF_QPC_BT_NUM_M,
				       VF_RES_A_DATA_1_VF_QPC_BT_NUM_S,
				       HNS_ROCE_VF_QPC_BT_NUM);

			roce_set_field(req_a->vf_srqc_bt_idx_num,
				       VF_RES_A_DATA_2_VF_SRQC_BT_IDX_M,
				       VF_RES_A_DATA_2_VF_SRQC_BT_IDX_S, 0);
			roce_set_field(req_a->vf_srqc_bt_idx_num,
				       VF_RES_A_DATA_2_VF_SRQC_BT_NUM_M,
				       VF_RES_A_DATA_2_VF_SRQC_BT_NUM_S,
				       HNS_ROCE_VF_SRQC_BT_NUM);

			roce_set_field(req_a->vf_cqc_bt_idx_num,
				       VF_RES_A_DATA_3_VF_CQC_BT_IDX_M,
				       VF_RES_A_DATA_3_VF_CQC_BT_IDX_S, 0);
			roce_set_field(req_a->vf_cqc_bt_idx_num,
				       VF_RES_A_DATA_3_VF_CQC_BT_NUM_M,
				       VF_RES_A_DATA_3_VF_CQC_BT_NUM_S,
				       HNS_ROCE_VF_CQC_BT_NUM);

			roce_set_field(req_a->vf_mpt_bt_idx_num,
				       VF_RES_A_DATA_4_VF_MPT_BT_IDX_M,
				       VF_RES_A_DATA_4_VF_MPT_BT_IDX_S, 0);
			roce_set_field(req_a->vf_mpt_bt_idx_num,
				       VF_RES_A_DATA_4_VF_MPT_BT_NUM_M,
				       VF_RES_A_DATA_4_VF_MPT_BT_NUM_S,
				       HNS_ROCE_VF_MPT_BT_NUM);

			roce_set_field(req_a->vf_eqc_bt_idx_num,
				       VF_RES_A_DATA_5_VF_EQC_IDX_M,
				       VF_RES_A_DATA_5_VF_EQC_IDX_S, 0);
			roce_set_field(req_a->vf_eqc_bt_idx_num,
				       VF_RES_A_DATA_5_VF_EQC_NUM_M,
				       VF_RES_A_DATA_5_VF_EQC_NUM_S,
				       HNS_ROCE_VF_EQC_NUM);
		} else {
			roce_set_field(req_b->vf_smac_idx_num,
				       VF_RES_B_DATA_1_VF_SMAC_IDX_M,
				       VF_RES_B_DATA_1_VF_SMAC_IDX_S, 0);
			roce_set_field(req_b->vf_smac_idx_num,
				       VF_RES_B_DATA_1_VF_SMAC_NUM_M,
				       VF_RES_B_DATA_1_VF_SMAC_NUM_S,
				       HNS_ROCE_VF_SMAC_NUM);

			roce_set_field(req_b->vf_sgid_idx_num,
				       VF_RES_B_DATA_2_VF_SGID_IDX_M,
				       VF_RES_B_DATA_2_VF_SGID_IDX_S, 0);
			roce_set_field(req_b->vf_sgid_idx_num,
				       VF_RES_B_DATA_2_VF_SGID_NUM_M,
				       VF_RES_B_DATA_2_VF_SGID_NUM_S,
				       HNS_ROCE_VF_SGID_NUM);

			roce_set_field(req_b->vf_qid_idx_sl_num,
				       VF_RES_B_DATA_3_VF_QID_IDX_M,
				       VF_RES_B_DATA_3_VF_QID_IDX_S, 0);
			roce_set_field(req_b->vf_qid_idx_sl_num,
				       VF_RES_B_DATA_3_VF_SL_NUM_M,
				       VF_RES_B_DATA_3_VF_SL_NUM_S,
				       HNS_ROCE_VF_SL_NUM);
		}
	}

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static int hns_roce_v2_profile(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;
	int ret;

	ret = hns_roce_cmq_query_hw_info(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Query firmware version fail, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hns_roce_config_global_param(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Configure global param fail, ret = %d.\n",
			ret);
	}

	/* Get pf resource owned by every pf */
	ret = hns_roce_query_pf_resource(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Query pf resource fail, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hns_roce_alloc_vf_resource(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Allocate vf resource fail, ret = %d.\n",
			ret);
		return ret;
	}

	hr_dev->vendor_part_id = 0;
	hr_dev->sys_image_guid = 0;

	caps->num_qps		= HNS_ROCE_V2_MAX_QP_NUM;
	caps->max_wqes		= HNS_ROCE_V2_MAX_WQE_NUM;
	caps->num_cqs		= HNS_ROCE_V2_MAX_CQ_NUM;
	caps->max_cqes		= HNS_ROCE_V2_MAX_CQE_NUM;
	caps->max_sq_sg		= HNS_ROCE_V2_MAX_SQ_SGE_NUM;
	caps->max_rq_sg		= HNS_ROCE_V2_MAX_RQ_SGE_NUM;
	caps->max_sq_inline	= HNS_ROCE_V2_MAX_SQ_INLINE;
	caps->num_uars		= HNS_ROCE_V2_UAR_NUM;
	caps->phy_num_uars	= HNS_ROCE_V2_PHY_UAR_NUM;
	caps->num_aeq_vectors	= 1;
	caps->num_comp_vectors	= 63;
	caps->num_other_vectors	= 0;
	caps->num_mtpts		= HNS_ROCE_V2_MAX_MTPT_NUM;
	caps->num_mtt_segs	= HNS_ROCE_V2_MAX_MTT_SEGS;
	caps->num_cqe_segs	= HNS_ROCE_V2_MAX_CQE_SEGS;
	caps->num_pds		= HNS_ROCE_V2_MAX_PD_NUM;
	caps->max_qp_init_rdma	= HNS_ROCE_V2_MAX_QP_INIT_RDMA;
	caps->max_qp_dest_rdma	= HNS_ROCE_V2_MAX_QP_DEST_RDMA;
	caps->max_sq_desc_sz	= HNS_ROCE_V2_MAX_SQ_DESC_SZ;
	caps->max_rq_desc_sz	= HNS_ROCE_V2_MAX_RQ_DESC_SZ;
	caps->max_srq_desc_sz	= HNS_ROCE_V2_MAX_SRQ_DESC_SZ;
	caps->qpc_entry_sz	= HNS_ROCE_V2_QPC_ENTRY_SZ;
	caps->irrl_entry_sz	= HNS_ROCE_V2_IRRL_ENTRY_SZ;
	caps->cqc_entry_sz	= HNS_ROCE_V2_CQC_ENTRY_SZ;
	caps->mtpt_entry_sz	= HNS_ROCE_V2_MTPT_ENTRY_SZ;
	caps->mtt_entry_sz	= HNS_ROCE_V2_MTT_ENTRY_SZ;
	caps->cq_entry_sz	= HNS_ROCE_V2_CQE_ENTRY_SIZE;
	caps->page_size_cap	= HNS_ROCE_V2_PAGE_SIZE_SUPPORTED;
	caps->reserved_lkey	= 0;
	caps->reserved_pds	= 0;
	caps->reserved_mrws	= 1;
	caps->reserved_uars	= 0;
	caps->reserved_cqs	= 0;

	caps->pkey_table_len[0] = 1;
	caps->gid_table_len[0] = 2;
	caps->local_ca_ack_delay = 0;
	caps->max_mtu = IB_MTU_4096;

	return 0;
}

static const struct hns_roce_hw hns_roce_hw_v2 = {
	.cmq_init = hns_roce_v2_cmq_init,
	.cmq_exit = hns_roce_v2_cmq_exit,
	.hw_profile = hns_roce_v2_profile,
};

static const struct pci_device_id hns_roce_hw_v2_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC), 0},
	/* required last entry */
	{0, }
};

static int hns_roce_hw_v2_get_cfg(struct hns_roce_dev *hr_dev,
				  struct hnae3_handle *handle)
{
	const struct pci_device_id *id;

	id = pci_match_id(hns_roce_hw_v2_pci_tbl, hr_dev->pci_dev);
	if (!id) {
		dev_err(hr_dev->dev, "device is not compatible!\n");
		return -ENXIO;
	}

	hr_dev->hw = &hns_roce_hw_v2;

	/* Get info from NIC driver. */
	hr_dev->reg_base = handle->rinfo.roce_io_base;
	hr_dev->caps.num_ports = 1;
	hr_dev->iboe.netdevs[0] = handle->rinfo.netdev;
	hr_dev->iboe.phy_port[0] = 0;

	/* cmd issue mode: 0 is poll, 1 is event */
	hr_dev->cmd_mod = 0;
	hr_dev->loop_idc = 0;

	return 0;
}

static int hns_roce_hw_v2_init_instance(struct hnae3_handle *handle)
{
	struct hns_roce_dev *hr_dev;
	int ret;

	hr_dev = (struct hns_roce_dev *)ib_alloc_device(sizeof(*hr_dev));
	if (!hr_dev)
		return -ENOMEM;

	hr_dev->priv = kzalloc(sizeof(struct hns_roce_v2_priv), GFP_KERNEL);
	if (!hr_dev->priv) {
		ret = -ENOMEM;
		goto error_failed_kzalloc;
	}

	hr_dev->pci_dev = handle->pdev;
	hr_dev->dev = &handle->pdev->dev;
	handle->priv = hr_dev;

	ret = hns_roce_hw_v2_get_cfg(hr_dev, handle);
	if (ret) {
		dev_err(hr_dev->dev, "Get Configuration failed!\n");
		goto error_failed_get_cfg;
	}

	ret = hns_roce_init(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "RoCE Engine init failed!\n");
		goto error_failed_get_cfg;
	}

	return 0;

error_failed_get_cfg:
	kfree(hr_dev->priv);

error_failed_kzalloc:
	ib_dealloc_device(&hr_dev->ib_dev);

	return ret;
}

static void hns_roce_hw_v2_uninit_instance(struct hnae3_handle *handle,
					   bool reset)
{
	struct hns_roce_dev *hr_dev = (struct hns_roce_dev *)handle->priv;

	hns_roce_exit(hr_dev);
	kfree(hr_dev->priv);
	ib_dealloc_device(&hr_dev->ib_dev);
}

static const struct hnae3_client_ops hns_roce_hw_v2_ops = {
	.init_instance = hns_roce_hw_v2_init_instance,
	.uninit_instance = hns_roce_hw_v2_uninit_instance,
};

static struct hnae3_client hns_roce_hw_v2_client = {
	.name = "hns_roce_hw_v2",
	.type = HNAE3_CLIENT_ROCE,
	.ops = &hns_roce_hw_v2_ops,
};

static int __init hns_roce_hw_v2_init(void)
{
	return hnae3_register_client(&hns_roce_hw_v2_client);
}

static void __exit hns_roce_hw_v2_exit(void)
{
	hnae3_unregister_client(&hns_roce_hw_v2_client);
}

module_init(hns_roce_hw_v2_init);
module_exit(hns_roce_hw_v2_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_AUTHOR("Shaobo Xu <xushaobo2@huawei.com>");
MODULE_DESCRIPTION("Hisilicon Hip08 Family RoCE Driver");
