/*
 * Copyright (c) 2016 Hisilicon Limited.
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

#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <rdma/ib_umem.h>
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"
#include "hns_roce_hw_v1.h"

static void set_data_seg(struct hns_roce_wqe_data_seg *dseg, struct ib_sge *sg)
{
	dseg->lkey = cpu_to_le32(sg->lkey);
	dseg->addr = cpu_to_le64(sg->addr);
	dseg->len  = cpu_to_le32(sg->length);
}

static void set_raddr_seg(struct hns_roce_wqe_raddr_seg *rseg, u64 remote_addr,
			  u32 rkey)
{
	rseg->raddr = cpu_to_le64(remote_addr);
	rseg->rkey  = cpu_to_le32(rkey);
	rseg->len   = 0;
}

int hns_roce_v1_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
			  struct ib_send_wr **bad_wr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_ah *ah = to_hr_ah(ud_wr(wr)->ah);
	struct hns_roce_ud_send_wqe *ud_sq_wqe = NULL;
	struct hns_roce_wqe_ctrl_seg *ctrl = NULL;
	struct hns_roce_wqe_data_seg *dseg = NULL;
	struct hns_roce_qp *qp = to_hr_qp(ibqp);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_sq_db sq_db;
	int ps_opcode = 0, i = 0;
	unsigned long flags = 0;
	void *wqe = NULL;
	u32 doorbell[2];
	int nreq = 0;
	u32 ind = 0;
	int ret = 0;

	spin_lock_irqsave(&qp->sq.lock, flags);

	ind = qp->sq_next_wqe;
	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (hns_roce_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			ret = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			dev_err(dev, "num_sge=%d > qp->sq.max_gs=%d\n",
				wr->num_sge, qp->sq.max_gs);
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		wqe = get_send_wqe(qp, ind & (qp->sq.wqe_cnt - 1));
		qp->sq.wrid[(qp->sq.head + nreq) & (qp->sq.wqe_cnt - 1)] =
								      wr->wr_id;

		/* Corresponding to the RC and RD type wqe process separately */
		if (ibqp->qp_type == IB_QPT_GSI) {
			ud_sq_wqe = wqe;
			roce_set_field(ud_sq_wqe->dmac_h,
				       UD_SEND_WQE_U32_4_DMAC_0_M,
				       UD_SEND_WQE_U32_4_DMAC_0_S,
				       ah->av.mac[0]);
			roce_set_field(ud_sq_wqe->dmac_h,
				       UD_SEND_WQE_U32_4_DMAC_1_M,
				       UD_SEND_WQE_U32_4_DMAC_1_S,
				       ah->av.mac[1]);
			roce_set_field(ud_sq_wqe->dmac_h,
				       UD_SEND_WQE_U32_4_DMAC_2_M,
				       UD_SEND_WQE_U32_4_DMAC_2_S,
				       ah->av.mac[2]);
			roce_set_field(ud_sq_wqe->dmac_h,
				       UD_SEND_WQE_U32_4_DMAC_3_M,
				       UD_SEND_WQE_U32_4_DMAC_3_S,
				       ah->av.mac[3]);

			roce_set_field(ud_sq_wqe->u32_8,
				       UD_SEND_WQE_U32_8_DMAC_4_M,
				       UD_SEND_WQE_U32_8_DMAC_4_S,
				       ah->av.mac[4]);
			roce_set_field(ud_sq_wqe->u32_8,
				       UD_SEND_WQE_U32_8_DMAC_5_M,
				       UD_SEND_WQE_U32_8_DMAC_5_S,
				       ah->av.mac[5]);
			roce_set_field(ud_sq_wqe->u32_8,
				       UD_SEND_WQE_U32_8_OPERATION_TYPE_M,
				       UD_SEND_WQE_U32_8_OPERATION_TYPE_S,
				       HNS_ROCE_WQE_OPCODE_SEND);
			roce_set_field(ud_sq_wqe->u32_8,
				       UD_SEND_WQE_U32_8_NUMBER_OF_DATA_SEG_M,
				       UD_SEND_WQE_U32_8_NUMBER_OF_DATA_SEG_S,
				       2);
			roce_set_bit(ud_sq_wqe->u32_8,
				UD_SEND_WQE_U32_8_SEND_GL_ROUTING_HDR_FLAG_S,
				1);

			ud_sq_wqe->u32_8 |= (wr->send_flags & IB_SEND_SIGNALED ?
				cpu_to_le32(HNS_ROCE_WQE_CQ_NOTIFY) : 0) |
				(wr->send_flags & IB_SEND_SOLICITED ?
				cpu_to_le32(HNS_ROCE_WQE_SE) : 0) |
				((wr->opcode == IB_WR_SEND_WITH_IMM) ?
				cpu_to_le32(HNS_ROCE_WQE_IMM) : 0);

			roce_set_field(ud_sq_wqe->u32_16,
				       UD_SEND_WQE_U32_16_DEST_QP_M,
				       UD_SEND_WQE_U32_16_DEST_QP_S,
				       ud_wr(wr)->remote_qpn);
			roce_set_field(ud_sq_wqe->u32_16,
				       UD_SEND_WQE_U32_16_MAX_STATIC_RATE_M,
				       UD_SEND_WQE_U32_16_MAX_STATIC_RATE_S,
				       ah->av.stat_rate);

			roce_set_field(ud_sq_wqe->u32_36,
				       UD_SEND_WQE_U32_36_FLOW_LABEL_M,
				       UD_SEND_WQE_U32_36_FLOW_LABEL_S, 0);
			roce_set_field(ud_sq_wqe->u32_36,
				       UD_SEND_WQE_U32_36_PRIORITY_M,
				       UD_SEND_WQE_U32_36_PRIORITY_S,
				       ah->av.sl_tclass_flowlabel >>
				       HNS_ROCE_SL_SHIFT);
			roce_set_field(ud_sq_wqe->u32_36,
				       UD_SEND_WQE_U32_36_SGID_INDEX_M,
				       UD_SEND_WQE_U32_36_SGID_INDEX_S,
				       hns_get_gid_index(hr_dev, qp->phy_port,
							 ah->av.gid_index));

			roce_set_field(ud_sq_wqe->u32_40,
				       UD_SEND_WQE_U32_40_HOP_LIMIT_M,
				       UD_SEND_WQE_U32_40_HOP_LIMIT_S,
				       ah->av.hop_limit);
			roce_set_field(ud_sq_wqe->u32_40,
				       UD_SEND_WQE_U32_40_TRAFFIC_CLASS_M,
				       UD_SEND_WQE_U32_40_TRAFFIC_CLASS_S, 0);

			memcpy(&ud_sq_wqe->dgid[0], &ah->av.dgid[0], GID_LEN);

			ud_sq_wqe->va0_l = (u32)wr->sg_list[0].addr;
			ud_sq_wqe->va0_h = (wr->sg_list[0].addr) >> 32;
			ud_sq_wqe->l_key0 = wr->sg_list[0].lkey;

			ud_sq_wqe->va1_l = (u32)wr->sg_list[1].addr;
			ud_sq_wqe->va1_h = (wr->sg_list[1].addr) >> 32;
			ud_sq_wqe->l_key1 = wr->sg_list[1].lkey;
			ind++;
		} else if (ibqp->qp_type == IB_QPT_RC) {
			ctrl = wqe;
			memset(ctrl, 0, sizeof(struct hns_roce_wqe_ctrl_seg));
			for (i = 0; i < wr->num_sge; i++)
				ctrl->msg_length += wr->sg_list[i].length;

			ctrl->sgl_pa_h = 0;
			ctrl->flag = 0;
			ctrl->imm_data = send_ieth(wr);

			/*Ctrl field, ctrl set type: sig, solic, imm, fence */
			/* SO wait for conforming application scenarios */
			ctrl->flag |= (wr->send_flags & IB_SEND_SIGNALED ?
				      cpu_to_le32(HNS_ROCE_WQE_CQ_NOTIFY) : 0) |
				      (wr->send_flags & IB_SEND_SOLICITED ?
				      cpu_to_le32(HNS_ROCE_WQE_SE) : 0) |
				      ((wr->opcode == IB_WR_SEND_WITH_IMM ||
				      wr->opcode == IB_WR_RDMA_WRITE_WITH_IMM) ?
				      cpu_to_le32(HNS_ROCE_WQE_IMM) : 0) |
				      (wr->send_flags & IB_SEND_FENCE ?
				      (cpu_to_le32(HNS_ROCE_WQE_FENCE)) : 0);

			wqe += sizeof(struct hns_roce_wqe_ctrl_seg);

			switch (wr->opcode) {
			case IB_WR_RDMA_READ:
				ps_opcode = HNS_ROCE_WQE_OPCODE_RDMA_READ;
				set_raddr_seg(wqe, atomic_wr(wr)->remote_addr,
					      atomic_wr(wr)->rkey);
				break;
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				ps_opcode = HNS_ROCE_WQE_OPCODE_RDMA_WRITE;
				set_raddr_seg(wqe, atomic_wr(wr)->remote_addr,
					      atomic_wr(wr)->rkey);
				break;
			case IB_WR_SEND:
			case IB_WR_SEND_WITH_INV:
			case IB_WR_SEND_WITH_IMM:
				ps_opcode = HNS_ROCE_WQE_OPCODE_SEND;
				break;
			case IB_WR_LOCAL_INV:
				break;
			case IB_WR_ATOMIC_CMP_AND_SWP:
			case IB_WR_ATOMIC_FETCH_AND_ADD:
			case IB_WR_LSO:
			default:
				ps_opcode = HNS_ROCE_WQE_OPCODE_MASK;
				break;
			}
			ctrl->flag |= cpu_to_le32(ps_opcode);
			wqe += sizeof(struct hns_roce_wqe_raddr_seg);

			dseg = wqe;
			if (wr->send_flags & IB_SEND_INLINE && wr->num_sge) {
				if (ctrl->msg_length >
					hr_dev->caps.max_sq_inline) {
					ret = -EINVAL;
					*bad_wr = wr;
					dev_err(dev, "inline len(1-%d)=%d, illegal",
						ctrl->msg_length,
						hr_dev->caps.max_sq_inline);
					goto out;
				}
				for (i = 0; i < wr->num_sge; i++) {
					memcpy(wqe, ((void *) (uintptr_t)
					       wr->sg_list[i].addr),
					       wr->sg_list[i].length);
					wqe += wr->sg_list[i].length;
				}
				ctrl->flag |= HNS_ROCE_WQE_INLINE;
			} else {
				/*sqe num is two */
				for (i = 0; i < wr->num_sge; i++)
					set_data_seg(dseg + i, wr->sg_list + i);

				ctrl->flag |= cpu_to_le32(wr->num_sge <<
					      HNS_ROCE_WQE_SGE_NUM_BIT);
			}
			ind++;
		} else {
			dev_dbg(dev, "unSupported QP type\n");
			break;
		}
	}

out:
	/* Set DB return */
	if (likely(nreq)) {
		qp->sq.head += nreq;
		/* Memory barrier */
		wmb();

		sq_db.u32_4 = 0;
		sq_db.u32_8 = 0;
		roce_set_field(sq_db.u32_4, SQ_DOORBELL_U32_4_SQ_HEAD_M,
			       SQ_DOORBELL_U32_4_SQ_HEAD_S,
			      (qp->sq.head & ((qp->sq.wqe_cnt << 1) - 1)));
		roce_set_field(sq_db.u32_4, SQ_DOORBELL_U32_4_PORT_M,
			       SQ_DOORBELL_U32_4_PORT_S, qp->phy_port);
		roce_set_field(sq_db.u32_8, SQ_DOORBELL_U32_8_QPN_M,
			       SQ_DOORBELL_U32_8_QPN_S, qp->doorbell_qpn);
		roce_set_bit(sq_db.u32_8, SQ_DOORBELL_HW_SYNC_S, 1);

		doorbell[0] = sq_db.u32_4;
		doorbell[1] = sq_db.u32_8;

		hns_roce_write64_k(doorbell, qp->sq.db_reg_l);
		qp->sq_next_wqe = ind;
	}

	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return ret;
}

int hns_roce_v1_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
			  struct ib_recv_wr **bad_wr)
{
	int ret = 0;
	int nreq = 0;
	int ind = 0;
	int i = 0;
	u32 reg_val = 0;
	unsigned long flags = 0;
	struct hns_roce_rq_wqe_ctrl *ctrl = NULL;
	struct hns_roce_wqe_data_seg *scat = NULL;
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_rq_db rq_db;
	uint32_t doorbell[2] = {0};

	spin_lock_irqsave(&hr_qp->rq.lock, flags);
	ind = hr_qp->rq.head & (hr_qp->rq.wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (hns_roce_wq_overflow(&hr_qp->rq, nreq,
			hr_qp->ibqp.recv_cq)) {
			ret = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > hr_qp->rq.max_gs)) {
			dev_err(dev, "rq:num_sge=%d > qp->sq.max_gs=%d\n",
				wr->num_sge, hr_qp->rq.max_gs);
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		ctrl = get_recv_wqe(hr_qp, ind);

		roce_set_field(ctrl->rwqe_byte_12,
			       RQ_WQE_CTRL_RWQE_BYTE_12_RWQE_SGE_NUM_M,
			       RQ_WQE_CTRL_RWQE_BYTE_12_RWQE_SGE_NUM_S,
			       wr->num_sge);

		scat = (struct hns_roce_wqe_data_seg *)(ctrl + 1);

		for (i = 0; i < wr->num_sge; i++)
			set_data_seg(scat + i, wr->sg_list + i);

		hr_qp->rq.wrid[ind] = wr->wr_id;

		ind = (ind + 1) & (hr_qp->rq.wqe_cnt - 1);
	}

out:
	if (likely(nreq)) {
		hr_qp->rq.head += nreq;
		/* Memory barrier */
		wmb();

		if (ibqp->qp_type == IB_QPT_GSI) {
			/* SW update GSI rq header */
			reg_val = roce_read(to_hr_dev(ibqp->device),
					    ROCEE_QP1C_CFG3_0_REG +
					    QP1C_CFGN_OFFSET * hr_qp->phy_port);
			roce_set_field(reg_val,
				       ROCEE_QP1C_CFG3_0_ROCEE_QP1C_RQ_HEAD_M,
				       ROCEE_QP1C_CFG3_0_ROCEE_QP1C_RQ_HEAD_S,
				       hr_qp->rq.head);
			roce_write(to_hr_dev(ibqp->device),
				   ROCEE_QP1C_CFG3_0_REG +
				   QP1C_CFGN_OFFSET * hr_qp->phy_port, reg_val);
		} else {
			rq_db.u32_4 = 0;
			rq_db.u32_8 = 0;

			roce_set_field(rq_db.u32_4, RQ_DOORBELL_U32_4_RQ_HEAD_M,
				       RQ_DOORBELL_U32_4_RQ_HEAD_S,
				       hr_qp->rq.head);
			roce_set_field(rq_db.u32_8, RQ_DOORBELL_U32_8_QPN_M,
				       RQ_DOORBELL_U32_8_QPN_S, hr_qp->qpn);
			roce_set_field(rq_db.u32_8, RQ_DOORBELL_U32_8_CMD_M,
				       RQ_DOORBELL_U32_8_CMD_S, 1);
			roce_set_bit(rq_db.u32_8, RQ_DOORBELL_U32_8_HW_SYNC_S,
				     1);

			doorbell[0] = rq_db.u32_4;
			doorbell[1] = rq_db.u32_8;

			hns_roce_write64_k(doorbell, hr_qp->rq.db_reg_l);
		}
	}
	spin_unlock_irqrestore(&hr_qp->rq.lock, flags);

	return ret;
}

static void hns_roce_set_db_event_mode(struct hns_roce_dev *hr_dev,
				       int sdb_mode, int odb_mode)
{
	u32 val;

	val = roce_read(hr_dev, ROCEE_GLB_CFG_REG);
	roce_set_bit(val, ROCEE_GLB_CFG_ROCEE_DB_SQ_MODE_S, sdb_mode);
	roce_set_bit(val, ROCEE_GLB_CFG_ROCEE_DB_OTH_MODE_S, odb_mode);
	roce_write(hr_dev, ROCEE_GLB_CFG_REG, val);
}

static void hns_roce_set_db_ext_mode(struct hns_roce_dev *hr_dev, u32 sdb_mode,
				     u32 odb_mode)
{
	u32 val;

	/* Configure SDB/ODB extend mode */
	val = roce_read(hr_dev, ROCEE_GLB_CFG_REG);
	roce_set_bit(val, ROCEE_GLB_CFG_SQ_EXT_DB_MODE_S, sdb_mode);
	roce_set_bit(val, ROCEE_GLB_CFG_OTH_EXT_DB_MODE_S, odb_mode);
	roce_write(hr_dev, ROCEE_GLB_CFG_REG, val);
}

static void hns_roce_set_sdb(struct hns_roce_dev *hr_dev, u32 sdb_alept,
			     u32 sdb_alful)
{
	u32 val;

	/* Configure SDB */
	val = roce_read(hr_dev, ROCEE_DB_SQ_WL_REG);
	roce_set_field(val, ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_M,
		       ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_S, sdb_alful);
	roce_set_field(val, ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_EMPTY_M,
		       ROCEE_DB_SQ_WL_ROCEE_DB_SQ_WL_EMPTY_S, sdb_alept);
	roce_write(hr_dev, ROCEE_DB_SQ_WL_REG, val);
}

static void hns_roce_set_odb(struct hns_roce_dev *hr_dev, u32 odb_alept,
			     u32 odb_alful)
{
	u32 val;

	/* Configure ODB */
	val = roce_read(hr_dev, ROCEE_DB_OTHERS_WL_REG);
	roce_set_field(val, ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_M,
		       ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_S, odb_alful);
	roce_set_field(val, ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_EMPTY_M,
		       ROCEE_DB_OTHERS_WL_ROCEE_DB_OTH_WL_EMPTY_S, odb_alept);
	roce_write(hr_dev, ROCEE_DB_OTHERS_WL_REG, val);
}

static void hns_roce_set_sdb_ext(struct hns_roce_dev *hr_dev, u32 ext_sdb_alept,
				 u32 ext_sdb_alful)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;
	dma_addr_t sdb_dma_addr;
	u32 val;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	db = &priv->db_table;

	/* Configure extend SDB threshold */
	roce_write(hr_dev, ROCEE_EXT_DB_SQ_WL_EMPTY_REG, ext_sdb_alept);
	roce_write(hr_dev, ROCEE_EXT_DB_SQ_WL_REG, ext_sdb_alful);

	/* Configure extend SDB base addr */
	sdb_dma_addr = db->ext_db->sdb_buf_list->map;
	roce_write(hr_dev, ROCEE_EXT_DB_SQ_REG, (u32)(sdb_dma_addr >> 12));

	/* Configure extend SDB depth */
	val = roce_read(hr_dev, ROCEE_EXT_DB_SQ_H_REG);
	roce_set_field(val, ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_SHIFT_M,
		       ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_SHIFT_S,
		       db->ext_db->esdb_dep);
	/*
	 * 44 = 32 + 12, When evaluating addr to hardware, shift 12 because of
	 * using 4K page, and shift more 32 because of
	 * caculating the high 32 bit value evaluated to hardware.
	 */
	roce_set_field(val, ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_BA_H_M,
		       ROCEE_EXT_DB_SQ_H_EXT_DB_SQ_BA_H_S, sdb_dma_addr >> 44);
	roce_write(hr_dev, ROCEE_EXT_DB_SQ_H_REG, val);

	dev_dbg(dev, "ext SDB depth: 0x%x\n", db->ext_db->esdb_dep);
	dev_dbg(dev, "ext SDB threshold: epmty: 0x%x, ful: 0x%x\n",
		ext_sdb_alept, ext_sdb_alful);
}

static void hns_roce_set_odb_ext(struct hns_roce_dev *hr_dev, u32 ext_odb_alept,
				 u32 ext_odb_alful)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;
	dma_addr_t odb_dma_addr;
	u32 val;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	db = &priv->db_table;

	/* Configure extend ODB threshold */
	roce_write(hr_dev, ROCEE_EXT_DB_OTHERS_WL_EMPTY_REG, ext_odb_alept);
	roce_write(hr_dev, ROCEE_EXT_DB_OTHERS_WL_REG, ext_odb_alful);

	/* Configure extend ODB base addr */
	odb_dma_addr = db->ext_db->odb_buf_list->map;
	roce_write(hr_dev, ROCEE_EXT_DB_OTH_REG, (u32)(odb_dma_addr >> 12));

	/* Configure extend ODB depth */
	val = roce_read(hr_dev, ROCEE_EXT_DB_OTH_H_REG);
	roce_set_field(val, ROCEE_EXT_DB_OTH_H_EXT_DB_OTH_SHIFT_M,
		       ROCEE_EXT_DB_OTH_H_EXT_DB_OTH_SHIFT_S,
		       db->ext_db->eodb_dep);
	roce_set_field(val, ROCEE_EXT_DB_SQ_H_EXT_DB_OTH_BA_H_M,
		       ROCEE_EXT_DB_SQ_H_EXT_DB_OTH_BA_H_S,
		       db->ext_db->eodb_dep);
	roce_write(hr_dev, ROCEE_EXT_DB_OTH_H_REG, val);

	dev_dbg(dev, "ext ODB depth: 0x%x\n", db->ext_db->eodb_dep);
	dev_dbg(dev, "ext ODB threshold: empty: 0x%x, ful: 0x%x\n",
		ext_odb_alept, ext_odb_alful);
}

static int hns_roce_db_ext_init(struct hns_roce_dev *hr_dev, u32 sdb_ext_mod,
				u32 odb_ext_mod)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;
	dma_addr_t sdb_dma_addr;
	dma_addr_t odb_dma_addr;
	int ret = 0;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	db = &priv->db_table;

	db->ext_db = kmalloc(sizeof(*db->ext_db), GFP_KERNEL);
	if (!db->ext_db)
		return -ENOMEM;

	if (sdb_ext_mod) {
		db->ext_db->sdb_buf_list = kmalloc(
				sizeof(*db->ext_db->sdb_buf_list), GFP_KERNEL);
		if (!db->ext_db->sdb_buf_list) {
			ret = -ENOMEM;
			goto ext_sdb_buf_fail_out;
		}

		db->ext_db->sdb_buf_list->buf = dma_alloc_coherent(dev,
						     HNS_ROCE_V1_EXT_SDB_SIZE,
						     &sdb_dma_addr, GFP_KERNEL);
		if (!db->ext_db->sdb_buf_list->buf) {
			ret = -ENOMEM;
			goto alloc_sq_db_buf_fail;
		}
		db->ext_db->sdb_buf_list->map = sdb_dma_addr;

		db->ext_db->esdb_dep = ilog2(HNS_ROCE_V1_EXT_SDB_DEPTH);
		hns_roce_set_sdb_ext(hr_dev, HNS_ROCE_V1_EXT_SDB_ALEPT,
				     HNS_ROCE_V1_EXT_SDB_ALFUL);
	} else
		hns_roce_set_sdb(hr_dev, HNS_ROCE_V1_SDB_ALEPT,
				 HNS_ROCE_V1_SDB_ALFUL);

	if (odb_ext_mod) {
		db->ext_db->odb_buf_list = kmalloc(
				sizeof(*db->ext_db->odb_buf_list), GFP_KERNEL);
		if (!db->ext_db->odb_buf_list) {
			ret = -ENOMEM;
			goto ext_odb_buf_fail_out;
		}

		db->ext_db->odb_buf_list->buf = dma_alloc_coherent(dev,
						     HNS_ROCE_V1_EXT_ODB_SIZE,
						     &odb_dma_addr, GFP_KERNEL);
		if (!db->ext_db->odb_buf_list->buf) {
			ret = -ENOMEM;
			goto alloc_otr_db_buf_fail;
		}
		db->ext_db->odb_buf_list->map = odb_dma_addr;

		db->ext_db->eodb_dep = ilog2(HNS_ROCE_V1_EXT_ODB_DEPTH);
		hns_roce_set_odb_ext(hr_dev, HNS_ROCE_V1_EXT_ODB_ALEPT,
				     HNS_ROCE_V1_EXT_ODB_ALFUL);
	} else
		hns_roce_set_odb(hr_dev, HNS_ROCE_V1_ODB_ALEPT,
				 HNS_ROCE_V1_ODB_ALFUL);

	hns_roce_set_db_ext_mode(hr_dev, sdb_ext_mod, odb_ext_mod);

	return 0;

alloc_otr_db_buf_fail:
	kfree(db->ext_db->odb_buf_list);

ext_odb_buf_fail_out:
	if (sdb_ext_mod) {
		dma_free_coherent(dev, HNS_ROCE_V1_EXT_SDB_SIZE,
				  db->ext_db->sdb_buf_list->buf,
				  db->ext_db->sdb_buf_list->map);
	}

alloc_sq_db_buf_fail:
	if (sdb_ext_mod)
		kfree(db->ext_db->sdb_buf_list);

ext_sdb_buf_fail_out:
	kfree(db->ext_db);
	return ret;
}

static int hns_roce_db_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;
	u32 sdb_ext_mod;
	u32 odb_ext_mod;
	u32 sdb_evt_mod;
	u32 odb_evt_mod;
	int ret = 0;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	db = &priv->db_table;

	memset(db, 0, sizeof(*db));

	/* Default DB mode */
	sdb_ext_mod = HNS_ROCE_SDB_EXTEND_MODE;
	odb_ext_mod = HNS_ROCE_ODB_EXTEND_MODE;
	sdb_evt_mod = HNS_ROCE_SDB_NORMAL_MODE;
	odb_evt_mod = HNS_ROCE_ODB_POLL_MODE;

	db->sdb_ext_mod = sdb_ext_mod;
	db->odb_ext_mod = odb_ext_mod;

	/* Init extend DB */
	ret = hns_roce_db_ext_init(hr_dev, sdb_ext_mod, odb_ext_mod);
	if (ret) {
		dev_err(dev, "Failed in extend DB configuration.\n");
		return ret;
	}

	hns_roce_set_db_event_mode(hr_dev, sdb_evt_mod, odb_evt_mod);

	return 0;
}

static void hns_roce_db_free(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	db = &priv->db_table;

	if (db->sdb_ext_mod) {
		dma_free_coherent(dev, HNS_ROCE_V1_EXT_SDB_SIZE,
				  db->ext_db->sdb_buf_list->buf,
				  db->ext_db->sdb_buf_list->map);
		kfree(db->ext_db->sdb_buf_list);
	}

	if (db->odb_ext_mod) {
		dma_free_coherent(dev, HNS_ROCE_V1_EXT_ODB_SIZE,
				  db->ext_db->odb_buf_list->buf,
				  db->ext_db->odb_buf_list->map);
		kfree(db->ext_db->odb_buf_list);
	}

	kfree(db->ext_db);
}

static int hns_roce_raq_init(struct hns_roce_dev *hr_dev)
{
	int ret;
	int raq_shift = 0;
	dma_addr_t addr;
	u32 val;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_raq_table *raq;
	struct device *dev = &hr_dev->pdev->dev;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	raq = &priv->raq_table;

	raq->e_raq_buf = kzalloc(sizeof(*(raq->e_raq_buf)), GFP_KERNEL);
	if (!raq->e_raq_buf)
		return -ENOMEM;

	raq->e_raq_buf->buf = dma_alloc_coherent(dev, HNS_ROCE_V1_RAQ_SIZE,
						 &addr, GFP_KERNEL);
	if (!raq->e_raq_buf->buf) {
		ret = -ENOMEM;
		goto err_dma_alloc_raq;
	}
	raq->e_raq_buf->map = addr;

	/* Configure raq extended address. 48bit 4K align*/
	roce_write(hr_dev, ROCEE_EXT_RAQ_REG, raq->e_raq_buf->map >> 12);

	/* Configure raq_shift */
	raq_shift = ilog2(HNS_ROCE_V1_RAQ_SIZE / HNS_ROCE_V1_RAQ_ENTRY);
	val = roce_read(hr_dev, ROCEE_EXT_RAQ_H_REG);
	roce_set_field(val, ROCEE_EXT_RAQ_H_EXT_RAQ_SHIFT_M,
		       ROCEE_EXT_RAQ_H_EXT_RAQ_SHIFT_S, raq_shift);
	/*
	 * 44 = 32 + 12, When evaluating addr to hardware, shift 12 because of
	 * using 4K page, and shift more 32 because of
	 * caculating the high 32 bit value evaluated to hardware.
	 */
	roce_set_field(val, ROCEE_EXT_RAQ_H_EXT_RAQ_BA_H_M,
		       ROCEE_EXT_RAQ_H_EXT_RAQ_BA_H_S,
		       raq->e_raq_buf->map >> 44);
	roce_write(hr_dev, ROCEE_EXT_RAQ_H_REG, val);
	dev_dbg(dev, "Configure raq_shift 0x%x.\n", val);

	/* Configure raq threshold */
	val = roce_read(hr_dev, ROCEE_RAQ_WL_REG);
	roce_set_field(val, ROCEE_RAQ_WL_ROCEE_RAQ_WL_M,
		       ROCEE_RAQ_WL_ROCEE_RAQ_WL_S,
		       HNS_ROCE_V1_EXT_RAQ_WF);
	roce_write(hr_dev, ROCEE_RAQ_WL_REG, val);
	dev_dbg(dev, "Configure raq_wl 0x%x.\n", val);

	/* Enable extend raq */
	val = roce_read(hr_dev, ROCEE_WRMS_POL_TIME_INTERVAL_REG);
	roce_set_field(val,
		       ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_POL_TIME_INTERVAL_M,
		       ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_POL_TIME_INTERVAL_S,
		       POL_TIME_INTERVAL_VAL);
	roce_set_bit(val, ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_EXT_RAQ_MODE, 1);
	roce_set_field(val,
		       ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_CFG_M,
		       ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_CFG_S,
		       2);
	roce_set_bit(val,
		     ROCEE_WRMS_POL_TIME_INTERVAL_WRMS_RAQ_TIMEOUT_CHK_EN_S, 1);
	roce_write(hr_dev, ROCEE_WRMS_POL_TIME_INTERVAL_REG, val);
	dev_dbg(dev, "Configure WrmsPolTimeInterval 0x%x.\n", val);

	/* Enable raq drop */
	val = roce_read(hr_dev, ROCEE_GLB_CFG_REG);
	roce_set_bit(val, ROCEE_GLB_CFG_TRP_RAQ_DROP_EN_S, 1);
	roce_write(hr_dev, ROCEE_GLB_CFG_REG, val);
	dev_dbg(dev, "Configure GlbCfg = 0x%x.\n", val);

	return 0;

err_dma_alloc_raq:
	kfree(raq->e_raq_buf);
	return ret;
}

static void hns_roce_raq_free(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_raq_table *raq;

	priv = (struct hns_roce_v1_priv *)hr_dev->hw->priv;
	raq = &priv->raq_table;

	dma_free_coherent(dev, HNS_ROCE_V1_RAQ_SIZE, raq->e_raq_buf->buf,
			  raq->e_raq_buf->map);
	kfree(raq->e_raq_buf);
}

static void hns_roce_port_enable(struct hns_roce_dev *hr_dev, int enable_flag)
{
	u32 val;

	if (enable_flag) {
		val = roce_read(hr_dev, ROCEE_GLB_CFG_REG);
		 /* Open all ports */
		roce_set_field(val, ROCEE_GLB_CFG_ROCEE_PORT_ST_M,
			       ROCEE_GLB_CFG_ROCEE_PORT_ST_S,
			       ALL_PORT_VAL_OPEN);
		roce_write(hr_dev, ROCEE_GLB_CFG_REG, val);
	} else {
		val = roce_read(hr_dev, ROCEE_GLB_CFG_REG);
		/* Close all ports */
		roce_set_field(val, ROCEE_GLB_CFG_ROCEE_PORT_ST_M,
			       ROCEE_GLB_CFG_ROCEE_PORT_ST_S, 0x0);
		roce_write(hr_dev, ROCEE_GLB_CFG_REG, val);
	}
}

/**
 * hns_roce_v1_reset - reset RoCE
 * @hr_dev: RoCE device struct pointer
 * @enable: true -- drop reset, false -- reset
 * return 0 - success , negative --fail
 */
int hns_roce_v1_reset(struct hns_roce_dev *hr_dev, bool dereset)
{
	struct device_node *dsaf_node;
	struct device *dev = &hr_dev->pdev->dev;
	struct device_node *np = dev->of_node;
	struct fwnode_handle *fwnode;
	int ret;

	/* check if this is DT/ACPI case */
	if (dev_of_node(dev)) {
		dsaf_node = of_parse_phandle(np, "dsaf-handle", 0);
		if (!dsaf_node) {
			dev_err(dev, "could not find dsaf-handle\n");
			return -EINVAL;
		}
		fwnode = &dsaf_node->fwnode;
	} else if (is_acpi_device_node(dev->fwnode)) {
		struct acpi_reference_args args;

		ret = acpi_node_get_property_reference(dev->fwnode,
						       "dsaf-handle", 0, &args);
		if (ret) {
			dev_err(dev, "could not find dsaf-handle\n");
			return ret;
		}
		fwnode = acpi_fwnode_handle(args.adev);
	} else {
		dev_err(dev, "cannot read data from DT or ACPI\n");
		return -ENXIO;
	}

	ret = hns_dsaf_roce_reset(fwnode, false);
	if (ret)
		return ret;

	if (dereset) {
		msleep(SLEEP_TIME_INTERVAL);
		ret = hns_dsaf_roce_reset(fwnode, true);
	}

	return ret;
}

void hns_roce_v1_profile(struct hns_roce_dev *hr_dev)
{
	int i = 0;
	struct hns_roce_caps *caps = &hr_dev->caps;

	hr_dev->vendor_id = le32_to_cpu(roce_read(hr_dev, ROCEE_VENDOR_ID_REG));
	hr_dev->vendor_part_id = le32_to_cpu(roce_read(hr_dev,
					     ROCEE_VENDOR_PART_ID_REG));
	hr_dev->hw_rev = le32_to_cpu(roce_read(hr_dev, ROCEE_HW_VERSION_REG));

	hr_dev->sys_image_guid = le32_to_cpu(roce_read(hr_dev,
					     ROCEE_SYS_IMAGE_GUID_L_REG)) |
				((u64)le32_to_cpu(roce_read(hr_dev,
					    ROCEE_SYS_IMAGE_GUID_H_REG)) << 32);

	caps->num_qps		= HNS_ROCE_V1_MAX_QP_NUM;
	caps->max_wqes		= HNS_ROCE_V1_MAX_WQE_NUM;
	caps->num_cqs		= HNS_ROCE_V1_MAX_CQ_NUM;
	caps->max_cqes		= HNS_ROCE_V1_MAX_CQE_NUM;
	caps->max_sq_sg		= HNS_ROCE_V1_SG_NUM;
	caps->max_rq_sg		= HNS_ROCE_V1_SG_NUM;
	caps->max_sq_inline	= HNS_ROCE_V1_INLINE_SIZE;
	caps->num_uars		= HNS_ROCE_V1_UAR_NUM;
	caps->phy_num_uars	= HNS_ROCE_V1_PHY_UAR_NUM;
	caps->num_aeq_vectors	= HNS_ROCE_AEQE_VEC_NUM;
	caps->num_comp_vectors	= HNS_ROCE_COMP_VEC_NUM;
	caps->num_other_vectors	= HNS_ROCE_AEQE_OF_VEC_NUM;
	caps->num_mtpts		= HNS_ROCE_V1_MAX_MTPT_NUM;
	caps->num_mtt_segs	= HNS_ROCE_V1_MAX_MTT_SEGS;
	caps->num_pds		= HNS_ROCE_V1_MAX_PD_NUM;
	caps->max_qp_init_rdma	= HNS_ROCE_V1_MAX_QP_INIT_RDMA;
	caps->max_qp_dest_rdma	= HNS_ROCE_V1_MAX_QP_DEST_RDMA;
	caps->max_sq_desc_sz	= HNS_ROCE_V1_MAX_SQ_DESC_SZ;
	caps->max_rq_desc_sz	= HNS_ROCE_V1_MAX_RQ_DESC_SZ;
	caps->qpc_entry_sz	= HNS_ROCE_V1_QPC_ENTRY_SIZE;
	caps->irrl_entry_sz	= HNS_ROCE_V1_IRRL_ENTRY_SIZE;
	caps->cqc_entry_sz	= HNS_ROCE_V1_CQC_ENTRY_SIZE;
	caps->mtpt_entry_sz	= HNS_ROCE_V1_MTPT_ENTRY_SIZE;
	caps->mtt_entry_sz	= HNS_ROCE_V1_MTT_ENTRY_SIZE;
	caps->cq_entry_sz	= HNS_ROCE_V1_CQE_ENTRY_SIZE;
	caps->page_size_cap	= HNS_ROCE_V1_PAGE_SIZE_SUPPORT;
	caps->sqp_start		= 0;
	caps->reserved_lkey	= 0;
	caps->reserved_pds	= 0;
	caps->reserved_mrws	= 1;
	caps->reserved_uars	= 0;
	caps->reserved_cqs	= 0;

	for (i = 0; i < caps->num_ports; i++)
		caps->pkey_table_len[i] = 1;

	for (i = 0; i < caps->num_ports; i++) {
		/* Six ports shared 16 GID in v1 engine */
		if (i >= (HNS_ROCE_V1_GID_NUM % caps->num_ports))
			caps->gid_table_len[i] = HNS_ROCE_V1_GID_NUM /
						 caps->num_ports;
		else
			caps->gid_table_len[i] = HNS_ROCE_V1_GID_NUM /
						 caps->num_ports + 1;
	}

	for (i = 0; i < caps->num_comp_vectors; i++)
		caps->ceqe_depth[i] = HNS_ROCE_V1_NUM_COMP_EQE;

	caps->aeqe_depth = HNS_ROCE_V1_NUM_ASYNC_EQE;
	caps->local_ca_ack_delay = le32_to_cpu(roce_read(hr_dev,
							 ROCEE_ACK_DELAY_REG));
	caps->max_mtu = IB_MTU_2048;
}

int hns_roce_v1_init(struct hns_roce_dev *hr_dev)
{
	int ret;
	u32 val;
	struct device *dev = &hr_dev->pdev->dev;

	/* DMAE user config */
	val = roce_read(hr_dev, ROCEE_DMAE_USER_CFG1_REG);
	roce_set_field(val, ROCEE_DMAE_USER_CFG1_ROCEE_CACHE_TB_CFG_M,
		       ROCEE_DMAE_USER_CFG1_ROCEE_CACHE_TB_CFG_S, 0xf);
	roce_set_field(val, ROCEE_DMAE_USER_CFG1_ROCEE_STREAM_ID_TB_CFG_M,
		       ROCEE_DMAE_USER_CFG1_ROCEE_STREAM_ID_TB_CFG_S,
		       1 << PAGES_SHIFT_16);
	roce_write(hr_dev, ROCEE_DMAE_USER_CFG1_REG, val);

	val = roce_read(hr_dev, ROCEE_DMAE_USER_CFG2_REG);
	roce_set_field(val, ROCEE_DMAE_USER_CFG2_ROCEE_CACHE_PKT_CFG_M,
		       ROCEE_DMAE_USER_CFG2_ROCEE_CACHE_PKT_CFG_S, 0xf);
	roce_set_field(val, ROCEE_DMAE_USER_CFG2_ROCEE_STREAM_ID_PKT_CFG_M,
		       ROCEE_DMAE_USER_CFG2_ROCEE_STREAM_ID_PKT_CFG_S,
		       1 << PAGES_SHIFT_16);

	ret = hns_roce_db_init(hr_dev);
	if (ret) {
		dev_err(dev, "doorbell init failed!\n");
		return ret;
	}

	ret = hns_roce_raq_init(hr_dev);
	if (ret) {
		dev_err(dev, "raq init failed!\n");
		goto error_failed_raq_init;
	}

	hns_roce_port_enable(hr_dev, HNS_ROCE_PORT_UP);

	return 0;

error_failed_raq_init:
	hns_roce_db_free(hr_dev);
	return ret;
}

void hns_roce_v1_exit(struct hns_roce_dev *hr_dev)
{
	hns_roce_port_enable(hr_dev, HNS_ROCE_PORT_DOWN);
	hns_roce_raq_free(hr_dev);
	hns_roce_db_free(hr_dev);
}

void hns_roce_v1_set_gid(struct hns_roce_dev *hr_dev, u8 port, int gid_index,
			 union ib_gid *gid)
{
	u32 *p = NULL;
	u8 gid_idx = 0;

	gid_idx = hns_get_gid_index(hr_dev, port, gid_index);

	p = (u32 *)&gid->raw[0];
	roce_raw_write(*p, hr_dev->reg_base + ROCEE_PORT_GID_L_0_REG +
		       (HNS_ROCE_V1_GID_NUM * gid_idx));

	p = (u32 *)&gid->raw[4];
	roce_raw_write(*p, hr_dev->reg_base + ROCEE_PORT_GID_ML_0_REG +
		       (HNS_ROCE_V1_GID_NUM * gid_idx));

	p = (u32 *)&gid->raw[8];
	roce_raw_write(*p, hr_dev->reg_base + ROCEE_PORT_GID_MH_0_REG +
		       (HNS_ROCE_V1_GID_NUM * gid_idx));

	p = (u32 *)&gid->raw[0xc];
	roce_raw_write(*p, hr_dev->reg_base + ROCEE_PORT_GID_H_0_REG +
		       (HNS_ROCE_V1_GID_NUM * gid_idx));
}

void hns_roce_v1_set_mac(struct hns_roce_dev *hr_dev, u8 phy_port, u8 *addr)
{
	u32 reg_smac_l;
	u16 reg_smac_h;
	u16 *p_h;
	u32 *p;
	u32 val;

	p = (u32 *)(&addr[0]);
	reg_smac_l = *p;
	roce_raw_write(reg_smac_l, hr_dev->reg_base + ROCEE_SMAC_L_0_REG +
		       PHY_PORT_OFFSET * phy_port);

	val = roce_read(hr_dev,
			ROCEE_SMAC_H_0_REG + phy_port * PHY_PORT_OFFSET);
	p_h = (u16 *)(&addr[4]);
	reg_smac_h  = *p_h;
	roce_set_field(val, ROCEE_SMAC_H_ROCEE_SMAC_H_M,
		       ROCEE_SMAC_H_ROCEE_SMAC_H_S, reg_smac_h);
	roce_write(hr_dev, ROCEE_SMAC_H_0_REG + phy_port * PHY_PORT_OFFSET,
		   val);
}

void hns_roce_v1_set_mtu(struct hns_roce_dev *hr_dev, u8 phy_port,
			 enum ib_mtu mtu)
{
	u32 val;

	val = roce_read(hr_dev,
			ROCEE_SMAC_H_0_REG + phy_port * PHY_PORT_OFFSET);
	roce_set_field(val, ROCEE_SMAC_H_ROCEE_PORT_MTU_M,
		       ROCEE_SMAC_H_ROCEE_PORT_MTU_S, mtu);
	roce_write(hr_dev, ROCEE_SMAC_H_0_REG + phy_port * PHY_PORT_OFFSET,
		   val);
}

int hns_roce_v1_write_mtpt(void *mb_buf, struct hns_roce_mr *mr,
			   unsigned long mtpt_idx)
{
	struct hns_roce_v1_mpt_entry *mpt_entry;
	struct scatterlist *sg;
	u64 *pages;
	int entry;
	int i;

	/* MPT filled into mailbox buf */
	mpt_entry = (struct hns_roce_v1_mpt_entry *)mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	roce_set_field(mpt_entry->mpt_byte_4, MPT_BYTE_4_KEY_STATE_M,
		       MPT_BYTE_4_KEY_STATE_S, KEY_VALID);
	roce_set_field(mpt_entry->mpt_byte_4, MPT_BYTE_4_KEY_M,
		       MPT_BYTE_4_KEY_S, mr->key);
	roce_set_field(mpt_entry->mpt_byte_4, MPT_BYTE_4_PAGE_SIZE_M,
		       MPT_BYTE_4_PAGE_SIZE_S, MR_SIZE_4K);
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_MW_TYPE_S, 0);
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_MW_BIND_ENABLE_S,
		     (mr->access & IB_ACCESS_MW_BIND ? 1 : 0));
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_OWN_S, 0);
	roce_set_field(mpt_entry->mpt_byte_4, MPT_BYTE_4_MEMORY_LOCATION_TYPE_M,
		       MPT_BYTE_4_MEMORY_LOCATION_TYPE_S, mr->type);
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_REMOTE_ATOMIC_S, 0);
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_LOCAL_WRITE_S,
		     (mr->access & IB_ACCESS_LOCAL_WRITE ? 1 : 0));
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_REMOTE_WRITE_S,
		     (mr->access & IB_ACCESS_REMOTE_WRITE ? 1 : 0));
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_REMOTE_READ_S,
		     (mr->access & IB_ACCESS_REMOTE_READ ? 1 : 0));
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_REMOTE_INVAL_ENABLE_S,
		     0);
	roce_set_bit(mpt_entry->mpt_byte_4, MPT_BYTE_4_ADDRESS_TYPE_S, 0);

	roce_set_field(mpt_entry->mpt_byte_12, MPT_BYTE_12_PBL_ADDR_H_M,
		       MPT_BYTE_12_PBL_ADDR_H_S, 0);
	roce_set_field(mpt_entry->mpt_byte_12, MPT_BYTE_12_MW_BIND_COUNTER_M,
		       MPT_BYTE_12_MW_BIND_COUNTER_S, 0);

	mpt_entry->virt_addr_l = (u32)mr->iova;
	mpt_entry->virt_addr_h = (u32)(mr->iova >> 32);
	mpt_entry->length = (u32)mr->size;

	roce_set_field(mpt_entry->mpt_byte_28, MPT_BYTE_28_PD_M,
		       MPT_BYTE_28_PD_S, mr->pd);
	roce_set_field(mpt_entry->mpt_byte_28, MPT_BYTE_28_L_KEY_IDX_L_M,
		       MPT_BYTE_28_L_KEY_IDX_L_S, mtpt_idx);
	roce_set_field(mpt_entry->mpt_byte_64, MPT_BYTE_64_L_KEY_IDX_H_M,
		       MPT_BYTE_64_L_KEY_IDX_H_S, mtpt_idx >> MTPT_IDX_SHIFT);

	/* DMA momery regsiter */
	if (mr->type == MR_TYPE_DMA)
		return 0;

	pages = (u64 *) __get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = 0;
	for_each_sg(mr->umem->sg_head.sgl, sg, mr->umem->nmap, entry) {
		pages[i] = ((u64)sg_dma_address(sg)) >> 12;

		/* Directly record to MTPT table firstly 7 entry */
		if (i >= HNS_ROCE_MAX_INNER_MTPT_NUM)
			break;
		i++;
	}

	/* Register user mr */
	for (i = 0; i < HNS_ROCE_MAX_INNER_MTPT_NUM; i++) {
		switch (i) {
		case 0:
			mpt_entry->pa0_l = cpu_to_le32((u32)(pages[i]));
			roce_set_field(mpt_entry->mpt_byte_36,
				MPT_BYTE_36_PA0_H_M,
				MPT_BYTE_36_PA0_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_32)));
			break;
		case 1:
			roce_set_field(mpt_entry->mpt_byte_36,
				       MPT_BYTE_36_PA1_L_M,
				       MPT_BYTE_36_PA1_L_S,
				       cpu_to_le32((u32)(pages[i])));
			roce_set_field(mpt_entry->mpt_byte_40,
				MPT_BYTE_40_PA1_H_M,
				MPT_BYTE_40_PA1_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_24)));
			break;
		case 2:
			roce_set_field(mpt_entry->mpt_byte_40,
				       MPT_BYTE_40_PA2_L_M,
				       MPT_BYTE_40_PA2_L_S,
				       cpu_to_le32((u32)(pages[i])));
			roce_set_field(mpt_entry->mpt_byte_44,
				MPT_BYTE_44_PA2_H_M,
				MPT_BYTE_44_PA2_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_16)));
			break;
		case 3:
			roce_set_field(mpt_entry->mpt_byte_44,
				       MPT_BYTE_44_PA3_L_M,
				       MPT_BYTE_44_PA3_L_S,
				       cpu_to_le32((u32)(pages[i])));
			roce_set_field(mpt_entry->mpt_byte_48,
				MPT_BYTE_48_PA3_H_M,
				MPT_BYTE_48_PA3_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_8)));
			break;
		case 4:
			mpt_entry->pa4_l = cpu_to_le32((u32)(pages[i]));
			roce_set_field(mpt_entry->mpt_byte_56,
				MPT_BYTE_56_PA4_H_M,
				MPT_BYTE_56_PA4_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_32)));
			break;
		case 5:
			roce_set_field(mpt_entry->mpt_byte_56,
				       MPT_BYTE_56_PA5_L_M,
				       MPT_BYTE_56_PA5_L_S,
				       cpu_to_le32((u32)(pages[i])));
			roce_set_field(mpt_entry->mpt_byte_60,
				MPT_BYTE_60_PA5_H_M,
				MPT_BYTE_60_PA5_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_24)));
			break;
		case 6:
			roce_set_field(mpt_entry->mpt_byte_60,
				       MPT_BYTE_60_PA6_L_M,
				       MPT_BYTE_60_PA6_L_S,
				       cpu_to_le32((u32)(pages[i])));
			roce_set_field(mpt_entry->mpt_byte_64,
				MPT_BYTE_64_PA6_H_M,
				MPT_BYTE_64_PA6_H_S,
				cpu_to_le32((u32)(pages[i] >> PAGES_SHIFT_16)));
			break;
		default:
			break;
		}
	}

	free_page((unsigned long) pages);

	mpt_entry->pbl_addr_l = (u32)(mr->pbl_dma_addr);

	roce_set_field(mpt_entry->mpt_byte_12, MPT_BYTE_12_PBL_ADDR_H_M,
		       MPT_BYTE_12_PBL_ADDR_H_S,
		       ((u32)(mr->pbl_dma_addr >> 32)));

	return 0;
}

static void *get_cqe(struct hns_roce_cq *hr_cq, int n)
{
	return hns_roce_buf_offset(&hr_cq->hr_buf.hr_buf,
				   n * HNS_ROCE_V1_CQE_ENTRY_SIZE);
}

static void *get_sw_cqe(struct hns_roce_cq *hr_cq, int n)
{
	struct hns_roce_cqe *hr_cqe = get_cqe(hr_cq, n & hr_cq->ib_cq.cqe);

	/* Get cqe when Owner bit is Conversely with the MSB of cons_idx */
	return (roce_get_bit(hr_cqe->cqe_byte_4, CQE_BYTE_4_OWNER_S) ^
		!!(n & (hr_cq->ib_cq.cqe + 1))) ? hr_cqe : NULL;
}

static struct hns_roce_cqe *next_cqe_sw(struct hns_roce_cq *hr_cq)
{
	return get_sw_cqe(hr_cq, hr_cq->cons_index);
}

void hns_roce_v1_cq_set_ci(struct hns_roce_cq *hr_cq, u32 cons_index,
			   spinlock_t *doorbell_lock)

{
	u32 doorbell[2];

	doorbell[0] = cons_index & ((hr_cq->cq_depth << 1) - 1);
	roce_set_bit(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_HW_SYNS_S, 1);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_S, 3);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_S, 0);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_S, hr_cq->cqn);

	hns_roce_write64_k(doorbell, hr_cq->cq_db_l);
}

static void __hns_roce_v1_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				   struct hns_roce_srq *srq)
{
	struct hns_roce_cqe *cqe, *dest;
	u32 prod_index;
	int nfreed = 0;
	u8 owner_bit;

	for (prod_index = hr_cq->cons_index; get_sw_cqe(hr_cq, prod_index);
	     ++prod_index) {
		if (prod_index == hr_cq->cons_index + hr_cq->ib_cq.cqe)
			break;
	}

	/*
	* Now backwards through the CQ, removing CQ entries
	* that match our QP by overwriting them with next entries.
	*/
	while ((int) --prod_index - (int) hr_cq->cons_index >= 0) {
		cqe = get_cqe(hr_cq, prod_index & hr_cq->ib_cq.cqe);
		if ((roce_get_field(cqe->cqe_byte_16, CQE_BYTE_16_LOCAL_QPN_M,
				     CQE_BYTE_16_LOCAL_QPN_S) &
				     HNS_ROCE_CQE_QPN_MASK) == qpn) {
			/* In v1 engine, not support SRQ */
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe(hr_cq, (prod_index + nfreed) &
				       hr_cq->ib_cq.cqe);
			owner_bit = roce_get_bit(dest->cqe_byte_4,
						 CQE_BYTE_4_OWNER_S);
			memcpy(dest, cqe, sizeof(*cqe));
			roce_set_bit(dest->cqe_byte_4, CQE_BYTE_4_OWNER_S,
				     owner_bit);
		}
	}

	if (nfreed) {
		hr_cq->cons_index += nfreed;
		/*
		* Make sure update of buffer contents is done before
		* updating consumer index.
		*/
		wmb();

		hns_roce_v1_cq_set_ci(hr_cq, hr_cq->cons_index,
				   &to_hr_dev(hr_cq->ib_cq.device)->cq_db_lock);
	}
}

static void hns_roce_v1_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				 struct hns_roce_srq *srq)
{
	spin_lock_irq(&hr_cq->lock);
	__hns_roce_v1_cq_clean(hr_cq, qpn, srq);
	spin_unlock_irq(&hr_cq->lock);
}

void hns_roce_v1_write_cqc(struct hns_roce_dev *hr_dev,
			   struct hns_roce_cq *hr_cq, void *mb_buf, u64 *mtts,
			   dma_addr_t dma_handle, int nent, u32 vector)
{
	struct hns_roce_cq_context *cq_context = NULL;
	void __iomem *tptr_addr;

	cq_context = mb_buf;
	memset(cq_context, 0, sizeof(*cq_context));

	tptr_addr = 0;
	hr_dev->priv_addr = tptr_addr;
	hr_cq->tptr_addr = tptr_addr;

	/* Register cq_context members */
	roce_set_field(cq_context->cqc_byte_4,
		       CQ_CONTEXT_CQC_BYTE_4_CQC_STATE_M,
		       CQ_CONTEXT_CQC_BYTE_4_CQC_STATE_S, CQ_STATE_VALID);
	roce_set_field(cq_context->cqc_byte_4, CQ_CONTEXT_CQC_BYTE_4_CQN_M,
		       CQ_CONTEXT_CQC_BYTE_4_CQN_S, hr_cq->cqn);
	cq_context->cqc_byte_4 = cpu_to_le32(cq_context->cqc_byte_4);

	cq_context->cq_bt_l = (u32)dma_handle;
	cq_context->cq_bt_l = cpu_to_le32(cq_context->cq_bt_l);

	roce_set_field(cq_context->cqc_byte_12,
		       CQ_CONTEXT_CQC_BYTE_12_CQ_BT_H_M,
		       CQ_CONTEXT_CQC_BYTE_12_CQ_BT_H_S,
		       ((u64)dma_handle >> 32));
	roce_set_field(cq_context->cqc_byte_12,
		       CQ_CONTEXT_CQC_BYTE_12_CQ_CQE_SHIFT_M,
		       CQ_CONTEXT_CQC_BYTE_12_CQ_CQE_SHIFT_S,
		       ilog2((unsigned int)nent));
	roce_set_field(cq_context->cqc_byte_12, CQ_CONTEXT_CQC_BYTE_12_CEQN_M,
		       CQ_CONTEXT_CQC_BYTE_12_CEQN_S, vector);
	cq_context->cqc_byte_12 = cpu_to_le32(cq_context->cqc_byte_12);

	cq_context->cur_cqe_ba0_l = (u32)(mtts[0]);
	cq_context->cur_cqe_ba0_l = cpu_to_le32(cq_context->cur_cqe_ba0_l);

	roce_set_field(cq_context->cqc_byte_20,
		       CQ_CONTEXT_CQC_BYTE_20_CUR_CQE_BA0_H_M,
		       CQ_CONTEXT_CQC_BYTE_20_CUR_CQE_BA0_H_S,
		       cpu_to_le32((mtts[0]) >> 32));
	/* Dedicated hardware, directly set 0 */
	roce_set_field(cq_context->cqc_byte_20,
		       CQ_CONTEXT_CQC_BYTE_20_CQ_CUR_INDEX_M,
		       CQ_CONTEXT_CQC_BYTE_20_CQ_CUR_INDEX_S, 0);
	/**
	 * 44 = 32 + 12, When evaluating addr to hardware, shift 12 because of
	 * using 4K page, and shift more 32 because of
	 * caculating the high 32 bit value evaluated to hardware.
	 */
	roce_set_field(cq_context->cqc_byte_20,
		       CQ_CONTEXT_CQC_BYTE_20_CQE_TPTR_ADDR_H_M,
		       CQ_CONTEXT_CQC_BYTE_20_CQE_TPTR_ADDR_H_S,
		       (u64)tptr_addr >> 44);
	cq_context->cqc_byte_20 = cpu_to_le32(cq_context->cqc_byte_20);

	cq_context->cqe_tptr_addr_l = (u32)((u64)tptr_addr >> 12);

	roce_set_field(cq_context->cqc_byte_32,
		       CQ_CONTEXT_CQC_BYTE_32_CUR_CQE_BA1_H_M,
		       CQ_CONTEXT_CQC_BYTE_32_CUR_CQE_BA1_H_S, 0);
	roce_set_bit(cq_context->cqc_byte_32,
		     CQ_CONTEXT_CQC_BYTE_32_SE_FLAG_S, 0);
	roce_set_bit(cq_context->cqc_byte_32,
		     CQ_CONTEXT_CQC_BYTE_32_CE_FLAG_S, 0);
	roce_set_bit(cq_context->cqc_byte_32,
		     CQ_CONTEXT_CQC_BYTE_32_NOTIFICATION_FLAG_S, 0);
	roce_set_bit(cq_context->cqc_byte_32,
		     CQ_CQNTEXT_CQC_BYTE_32_TYPE_OF_COMPLETION_NOTIFICATION_S,
		     0);
	/*The initial value of cq's ci is 0 */
	roce_set_field(cq_context->cqc_byte_32,
		       CQ_CONTEXT_CQC_BYTE_32_CQ_CONS_IDX_M,
		       CQ_CONTEXT_CQC_BYTE_32_CQ_CONS_IDX_S, 0);
	cq_context->cqc_byte_32 = cpu_to_le32(cq_context->cqc_byte_32);
}

int hns_roce_v1_req_notify_cq(struct ib_cq *ibcq, enum ib_cq_notify_flags flags)
{
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	u32 notification_flag;
	u32 doorbell[2];
	int ret = 0;

	notification_flag = (flags & IB_CQ_SOLICITED_MASK) ==
			    IB_CQ_SOLICITED ? CQ_DB_REQ_NOT : CQ_DB_REQ_NOT_SOL;
	/*
	* flags = 0; Notification Flag = 1, next
	* flags = 1; Notification Flag = 0, solocited
	*/
	doorbell[0] = hr_cq->cons_index & ((hr_cq->cq_depth << 1) - 1);
	roce_set_bit(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_HW_SYNS_S, 1);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_S, 3);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_CMD_MDF_S, 1);
	roce_set_field(doorbell[1], ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_M,
		       ROCEE_DB_OTHERS_H_ROCEE_DB_OTH_INP_H_S,
		       hr_cq->cqn | notification_flag);

	hns_roce_write64_k(doorbell, hr_cq->cq_db_l);

	return ret;
}

static int hns_roce_v1_poll_one(struct hns_roce_cq *hr_cq,
				struct hns_roce_qp **cur_qp, struct ib_wc *wc)
{
	int qpn;
	int is_send;
	u16 wqe_ctr;
	u32 status;
	u32 opcode;
	struct hns_roce_cqe *cqe;
	struct hns_roce_qp *hr_qp;
	struct hns_roce_wq *wq;
	struct hns_roce_wqe_ctrl_seg *sq_wqe;
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_cq->ib_cq.device);
	struct device *dev = &hr_dev->pdev->dev;

	/* Find cqe according consumer index */
	cqe = next_cqe_sw(hr_cq);
	if (!cqe)
		return -EAGAIN;

	++hr_cq->cons_index;
	/* Memory barrier */
	rmb();
	/* 0->SQ, 1->RQ */
	is_send  = !(roce_get_bit(cqe->cqe_byte_4, CQE_BYTE_4_SQ_RQ_FLAG_S));

	/* Local_qpn in UD cqe is always 1, so it needs to compute new qpn */
	if (roce_get_field(cqe->cqe_byte_16, CQE_BYTE_16_LOCAL_QPN_M,
			   CQE_BYTE_16_LOCAL_QPN_S) <= 1) {
		qpn = roce_get_field(cqe->cqe_byte_20, CQE_BYTE_20_PORT_NUM_M,
				     CQE_BYTE_20_PORT_NUM_S) +
		      roce_get_field(cqe->cqe_byte_16, CQE_BYTE_16_LOCAL_QPN_M,
				     CQE_BYTE_16_LOCAL_QPN_S) *
				     HNS_ROCE_MAX_PORTS;
	} else {
		qpn = roce_get_field(cqe->cqe_byte_16, CQE_BYTE_16_LOCAL_QPN_M,
				     CQE_BYTE_16_LOCAL_QPN_S);
	}

	if (!*cur_qp || (qpn & HNS_ROCE_CQE_QPN_MASK) != (*cur_qp)->qpn) {
		hr_qp = __hns_roce_qp_lookup(hr_dev, qpn);
		if (unlikely(!hr_qp)) {
			dev_err(dev, "CQ %06lx with entry for unknown QPN %06x\n",
				hr_cq->cqn, (qpn & HNS_ROCE_CQE_QPN_MASK));
			return -EINVAL;
		}

		*cur_qp = hr_qp;
	}

	wc->qp = &(*cur_qp)->ibqp;
	wc->vendor_err = 0;

	status = roce_get_field(cqe->cqe_byte_4,
				CQE_BYTE_4_STATUS_OF_THE_OPERATION_M,
				CQE_BYTE_4_STATUS_OF_THE_OPERATION_S) &
				HNS_ROCE_CQE_STATUS_MASK;
	switch (status) {
	case HNS_ROCE_CQE_SUCCESS:
		wc->status = IB_WC_SUCCESS;
		break;
	case HNS_ROCE_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		wc->status = IB_WC_LOC_LEN_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		wc->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_LOCAL_PROT_ERR:
		wc->status = IB_WC_LOC_PROT_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_WR_FLUSH_ERR:
		wc->status = IB_WC_WR_FLUSH_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_MEM_MANAGE_OPERATE_ERR:
		wc->status = IB_WC_MW_BIND_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_BAD_RESP_ERR:
		wc->status = IB_WC_BAD_RESP_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		wc->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		wc->status = IB_WC_REM_ACCESS_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_REMOTE_OP_ERR:
		wc->status = IB_WC_REM_OP_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WC_RETRY_EXC_ERR;
		break;
	case HNS_ROCE_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		wc->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		break;
	}

	/* CQE status error, directly return */
	if (wc->status != IB_WC_SUCCESS)
		return 0;

	if (is_send) {
		/* SQ conrespond to CQE */
		sq_wqe = get_send_wqe(*cur_qp, roce_get_field(cqe->cqe_byte_4,
						CQE_BYTE_4_WQE_INDEX_M,
						CQE_BYTE_4_WQE_INDEX_S));
		switch (sq_wqe->flag & HNS_ROCE_WQE_OPCODE_MASK) {
		case HNS_ROCE_WQE_OPCODE_SEND:
			wc->opcode = IB_WC_SEND;
			break;
		case HNS_ROCE_WQE_OPCODE_RDMA_READ:
			wc->opcode = IB_WC_RDMA_READ;
			wc->byte_len = le32_to_cpu(cqe->byte_cnt);
			break;
		case HNS_ROCE_WQE_OPCODE_RDMA_WRITE:
			wc->opcode = IB_WC_RDMA_WRITE;
			break;
		case HNS_ROCE_WQE_OPCODE_LOCAL_INV:
			wc->opcode = IB_WC_LOCAL_INV;
			break;
		case HNS_ROCE_WQE_OPCODE_UD_SEND:
			wc->opcode = IB_WC_SEND;
			break;
		default:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		}
		wc->wc_flags = (sq_wqe->flag & HNS_ROCE_WQE_IMM ?
				IB_WC_WITH_IMM : 0);

		wq = &(*cur_qp)->sq;
		if ((*cur_qp)->sq_signal_bits) {
			/*
			* If sg_signal_bit is 1,
			* firstly tail pointer updated to wqe
			* which current cqe correspond to
			*/
			wqe_ctr = (u16)roce_get_field(cqe->cqe_byte_4,
						      CQE_BYTE_4_WQE_INDEX_M,
						      CQE_BYTE_4_WQE_INDEX_S);
			wq->tail += (wqe_ctr - (u16)wq->tail) &
				    (wq->wqe_cnt - 1);
		}
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
		} else {
		/* RQ conrespond to CQE */
		wc->byte_len = le32_to_cpu(cqe->byte_cnt);
		opcode = roce_get_field(cqe->cqe_byte_4,
					CQE_BYTE_4_OPERATION_TYPE_M,
					CQE_BYTE_4_OPERATION_TYPE_S) &
					HNS_ROCE_CQE_OPCODE_MASK;
		switch (opcode) {
		case HNS_ROCE_OPCODE_RDMA_WITH_IMM_RECEIVE:
			wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
			wc->wc_flags = IB_WC_WITH_IMM;
			wc->ex.imm_data = le32_to_cpu(cqe->immediate_data);
			break;
		case HNS_ROCE_OPCODE_SEND_DATA_RECEIVE:
			if (roce_get_bit(cqe->cqe_byte_4,
					 CQE_BYTE_4_IMM_INDICATOR_S)) {
				wc->opcode = IB_WC_RECV;
				wc->wc_flags = IB_WC_WITH_IMM;
				wc->ex.imm_data = le32_to_cpu(
						  cqe->immediate_data);
			} else {
				wc->opcode = IB_WC_RECV;
				wc->wc_flags = 0;
			}
			break;
		default:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		}

		/* Update tail pointer, record wr_id */
		wq = &(*cur_qp)->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
		wc->sl = (u8)roce_get_field(cqe->cqe_byte_20, CQE_BYTE_20_SL_M,
					    CQE_BYTE_20_SL_S);
		wc->src_qp = (u8)roce_get_field(cqe->cqe_byte_20,
						CQE_BYTE_20_REMOTE_QPN_M,
						CQE_BYTE_20_REMOTE_QPN_S);
		wc->wc_flags |= (roce_get_bit(cqe->cqe_byte_20,
					      CQE_BYTE_20_GRH_PRESENT_S) ?
					      IB_WC_GRH : 0);
		wc->pkey_index = (u16)roce_get_field(cqe->cqe_byte_28,
						     CQE_BYTE_28_P_KEY_IDX_M,
						     CQE_BYTE_28_P_KEY_IDX_S);
	}

	return 0;
}

int hns_roce_v1_poll_cq(struct ib_cq *ibcq, int num_entries, struct ib_wc *wc)
{
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	struct hns_roce_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;
	int ret = 0;

	spin_lock_irqsave(&hr_cq->lock, flags);

	for (npolled = 0; npolled < num_entries; ++npolled) {
		ret = hns_roce_v1_poll_one(hr_cq, &cur_qp, wc + npolled);
		if (ret)
			break;
	}

	if (npolled) {
		hns_roce_v1_cq_set_ci(hr_cq, hr_cq->cons_index,
				      &to_hr_dev(ibcq->device)->cq_db_lock);
	}

	spin_unlock_irqrestore(&hr_cq->lock, flags);

	if (ret == 0 || ret == -EAGAIN)
		return npolled;
	else
		return ret;
}

static int hns_roce_v1_qp_modify(struct hns_roce_dev *hr_dev,
				 struct hns_roce_mtt *mtt,
				 enum hns_roce_qp_state cur_state,
				 enum hns_roce_qp_state new_state,
				 struct hns_roce_qp_context *context,
				 struct hns_roce_qp *hr_qp)
{
	static const u16
	op[HNS_ROCE_QP_NUM_STATE][HNS_ROCE_QP_NUM_STATE] = {
		[HNS_ROCE_QP_STATE_RST] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		[HNS_ROCE_QP_STATE_INIT] = HNS_ROCE_CMD_RST2INIT_QP,
		},
		[HNS_ROCE_QP_STATE_INIT] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		/* Note: In v1 engine, HW doesn't support RST2INIT.
		 * We use RST2INIT cmd instead of INIT2INIT.
		 */
		[HNS_ROCE_QP_STATE_INIT] = HNS_ROCE_CMD_RST2INIT_QP,
		[HNS_ROCE_QP_STATE_RTR] = HNS_ROCE_CMD_INIT2RTR_QP,
		},
		[HNS_ROCE_QP_STATE_RTR] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		[HNS_ROCE_QP_STATE_RTS] = HNS_ROCE_CMD_RTR2RTS_QP,
		},
		[HNS_ROCE_QP_STATE_RTS] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		[HNS_ROCE_QP_STATE_RTS] = HNS_ROCE_CMD_RTS2RTS_QP,
		[HNS_ROCE_QP_STATE_SQD] = HNS_ROCE_CMD_RTS2SQD_QP,
		},
		[HNS_ROCE_QP_STATE_SQD] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		[HNS_ROCE_QP_STATE_RTS] = HNS_ROCE_CMD_SQD2RTS_QP,
		[HNS_ROCE_QP_STATE_SQD] = HNS_ROCE_CMD_SQD2SQD_QP,
		},
		[HNS_ROCE_QP_STATE_ERR] = {
		[HNS_ROCE_QP_STATE_RST] = HNS_ROCE_CMD_2RST_QP,
		[HNS_ROCE_QP_STATE_ERR] = HNS_ROCE_CMD_2ERR_QP,
		}
	};

	struct hns_roce_cmd_mailbox *mailbox;
	struct device *dev = &hr_dev->pdev->dev;
	int ret = 0;

	if (cur_state >= HNS_ROCE_QP_NUM_STATE ||
	    new_state >= HNS_ROCE_QP_NUM_STATE ||
	    !op[cur_state][new_state]) {
		dev_err(dev, "[modify_qp]not support state %d to %d\n",
			cur_state, new_state);
		return -EINVAL;
	}

	if (op[cur_state][new_state] == HNS_ROCE_CMD_2RST_QP)
		return hns_roce_cmd_mbox(hr_dev, 0, 0, hr_qp->qpn, 2,
					 HNS_ROCE_CMD_2RST_QP,
					 HNS_ROCE_CMD_TIME_CLASS_A);

	if (op[cur_state][new_state] == HNS_ROCE_CMD_2ERR_QP)
		return hns_roce_cmd_mbox(hr_dev, 0, 0, hr_qp->qpn, 2,
					 HNS_ROCE_CMD_2ERR_QP,
					 HNS_ROCE_CMD_TIME_CLASS_A);

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, context, sizeof(*context));

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, hr_qp->qpn, 0,
				op[cur_state][new_state],
				HNS_ROCE_CMD_TIME_CLASS_C);

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v1_m_sqp(struct ib_qp *ibqp, const struct ib_qp_attr *attr,
			     int attr_mask, enum ib_qp_state cur_state,
			     enum ib_qp_state new_state)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_sqp_context *context;
	struct device *dev = &hr_dev->pdev->dev;
	dma_addr_t dma_handle = 0;
	int rq_pa_start;
	u32 reg_val;
	u64 *mtts;
	u32 *addr;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	/* Search QP buf's MTTs */
	mtts = hns_roce_table_find(&hr_dev->mr_table.mtt_table,
				   hr_qp->mtt.first_seg, &dma_handle);
	if (!mtts) {
		dev_err(dev, "qp buf pa find failed\n");
		goto out;
	}

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		roce_set_field(context->qp1c_bytes_4,
			       QP1C_BYTES_4_SQ_WQE_SHIFT_M,
			       QP1C_BYTES_4_SQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->sq.wqe_cnt));
		roce_set_field(context->qp1c_bytes_4,
			       QP1C_BYTES_4_RQ_WQE_SHIFT_M,
			       QP1C_BYTES_4_RQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->rq.wqe_cnt));
		roce_set_field(context->qp1c_bytes_4, QP1C_BYTES_4_PD_M,
			       QP1C_BYTES_4_PD_S, to_hr_pd(ibqp->pd)->pdn);

		context->sq_rq_bt_l = (u32)(dma_handle);
		roce_set_field(context->qp1c_bytes_12,
			       QP1C_BYTES_12_SQ_RQ_BT_H_M,
			       QP1C_BYTES_12_SQ_RQ_BT_H_S,
			       ((u32)(dma_handle >> 32)));

		roce_set_field(context->qp1c_bytes_16, QP1C_BYTES_16_RQ_HEAD_M,
			       QP1C_BYTES_16_RQ_HEAD_S, hr_qp->rq.head);
		roce_set_field(context->qp1c_bytes_16, QP1C_BYTES_16_PORT_NUM_M,
			       QP1C_BYTES_16_PORT_NUM_S, hr_qp->phy_port);
		roce_set_bit(context->qp1c_bytes_16,
			     QP1C_BYTES_16_SIGNALING_TYPE_S,
			     hr_qp->sq_signal_bits);
		roce_set_bit(context->qp1c_bytes_16,
			     QP1C_BYTES_16_LOCAL_ENABLE_E2E_CREDIT_S,
			     hr_qp->sq_signal_bits);
		roce_set_bit(context->qp1c_bytes_16, QP1C_BYTES_16_RQ_BA_FLG_S,
			     1);
		roce_set_bit(context->qp1c_bytes_16, QP1C_BYTES_16_SQ_BA_FLG_S,
			     1);
		roce_set_bit(context->qp1c_bytes_16, QP1C_BYTES_16_QP1_ERR_S,
			     0);

		roce_set_field(context->qp1c_bytes_20, QP1C_BYTES_20_SQ_HEAD_M,
			       QP1C_BYTES_20_SQ_HEAD_S, hr_qp->sq.head);
		roce_set_field(context->qp1c_bytes_20, QP1C_BYTES_20_PKEY_IDX_M,
			       QP1C_BYTES_20_PKEY_IDX_S, attr->pkey_index);

		rq_pa_start = (u32)hr_qp->rq.offset / PAGE_SIZE;
		context->cur_rq_wqe_ba_l = (u32)(mtts[rq_pa_start]);

		roce_set_field(context->qp1c_bytes_28,
			       QP1C_BYTES_28_CUR_RQ_WQE_BA_H_M,
			       QP1C_BYTES_28_CUR_RQ_WQE_BA_H_S,
			       (mtts[rq_pa_start]) >> 32);
		roce_set_field(context->qp1c_bytes_28,
			       QP1C_BYTES_28_RQ_CUR_IDX_M,
			       QP1C_BYTES_28_RQ_CUR_IDX_S, 0);

		roce_set_field(context->qp1c_bytes_32,
			       QP1C_BYTES_32_RX_CQ_NUM_M,
			       QP1C_BYTES_32_RX_CQ_NUM_S,
			       to_hr_cq(ibqp->recv_cq)->cqn);
		roce_set_field(context->qp1c_bytes_32,
			       QP1C_BYTES_32_TX_CQ_NUM_M,
			       QP1C_BYTES_32_TX_CQ_NUM_S,
			       to_hr_cq(ibqp->send_cq)->cqn);

		context->cur_sq_wqe_ba_l  = (u32)mtts[0];

		roce_set_field(context->qp1c_bytes_40,
			       QP1C_BYTES_40_CUR_SQ_WQE_BA_H_M,
			       QP1C_BYTES_40_CUR_SQ_WQE_BA_H_S,
			       (mtts[0]) >> 32);
		roce_set_field(context->qp1c_bytes_40,
			       QP1C_BYTES_40_SQ_CUR_IDX_M,
			       QP1C_BYTES_40_SQ_CUR_IDX_S, 0);

		/* Copy context to QP1C register */
		addr = (u32 *)(hr_dev->reg_base + ROCEE_QP1C_CFG0_0_REG +
			hr_qp->phy_port * sizeof(*context));

		writel(context->qp1c_bytes_4, addr);
		writel(context->sq_rq_bt_l, addr + 1);
		writel(context->qp1c_bytes_12, addr + 2);
		writel(context->qp1c_bytes_16, addr + 3);
		writel(context->qp1c_bytes_20, addr + 4);
		writel(context->cur_rq_wqe_ba_l, addr + 5);
		writel(context->qp1c_bytes_28, addr + 6);
		writel(context->qp1c_bytes_32, addr + 7);
		writel(context->cur_sq_wqe_ba_l, addr + 8);
		writel(context->qp1c_bytes_40, addr + 9);
	}

	/* Modify QP1C status */
	reg_val = roce_read(hr_dev, ROCEE_QP1C_CFG0_0_REG +
			    hr_qp->phy_port * sizeof(*context));
	roce_set_field(reg_val, ROCEE_QP1C_CFG0_0_ROCEE_QP1C_QP_ST_M,
		       ROCEE_QP1C_CFG0_0_ROCEE_QP1C_QP_ST_S, new_state);
	roce_write(hr_dev, ROCEE_QP1C_CFG0_0_REG +
		    hr_qp->phy_port * sizeof(*context), reg_val);

	hr_qp->state = new_state;
	if (new_state == IB_QPS_RESET) {
		hns_roce_v1_cq_clean(to_hr_cq(ibqp->recv_cq), hr_qp->qpn,
				     ibqp->srq ? to_hr_srq(ibqp->srq) : NULL);
		if (ibqp->send_cq != ibqp->recv_cq)
			hns_roce_v1_cq_clean(to_hr_cq(ibqp->send_cq),
					     hr_qp->qpn, NULL);

		hr_qp->rq.head = 0;
		hr_qp->rq.tail = 0;
		hr_qp->sq.head = 0;
		hr_qp->sq.tail = 0;
		hr_qp->sq_next_wqe = 0;
	}

	kfree(context);
	return 0;

out:
	kfree(context);
	return -EINVAL;
}

static int hns_roce_v1_m_qp(struct ib_qp *ibqp, const struct ib_qp_attr *attr,
			    int attr_mask, enum ib_qp_state cur_state,
			    enum ib_qp_state new_state)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_qp_context *context;
	dma_addr_t dma_handle_2 = 0;
	dma_addr_t dma_handle = 0;
	uint32_t doorbell[2] = {0};
	int rq_pa_start = 0;
	u64 *mtts_2 = NULL;
	int ret = -EINVAL;
	u64 *mtts = NULL;
	int port;
	u8 *dmac;
	u8 *smac;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	/* Search qp buf's mtts */
	mtts = hns_roce_table_find(&hr_dev->mr_table.mtt_table,
				   hr_qp->mtt.first_seg, &dma_handle);
	if (mtts == NULL) {
		dev_err(dev, "qp buf pa find failed\n");
		goto out;
	}

	/* Search IRRL's mtts */
	mtts_2 = hns_roce_table_find(&hr_dev->qp_table.irrl_table, hr_qp->qpn,
				     &dma_handle_2);
	if (mtts_2 == NULL) {
		dev_err(dev, "qp irrl_table find failed\n");
		goto out;
	}

	/*
	*Reset to init
	*	Mandatory param:
	*	IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS
	*	Optional param: NA
	*/
	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_TRANSPORT_SERVICE_TYPE_M,
			       QP_CONTEXT_QPC_BYTES_4_TRANSPORT_SERVICE_TYPE_S,
			       to_hr_qp_type(hr_qp->ibqp.qp_type));

		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_ENABLE_FPMR_S, 0);
		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_RDMA_READ_ENABLE_S,
			     !!(attr->qp_access_flags & IB_ACCESS_REMOTE_READ));
		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_RDMA_WRITE_ENABLE_S,
			     !!(attr->qp_access_flags & IB_ACCESS_REMOTE_WRITE)
			     );
		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_ATOMIC_OPERATION_ENABLE_S,
			     !!(attr->qp_access_flags & IB_ACCESS_REMOTE_ATOMIC)
			     );
		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_RDMAR_USE_S, 1);
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_SQ_WQE_SHIFT_M,
			       QP_CONTEXT_QPC_BYTES_4_SQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->sq.wqe_cnt));
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_RQ_WQE_SHIFT_M,
			       QP_CONTEXT_QPC_BYTES_4_RQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->rq.wqe_cnt));
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_PD_M,
			       QP_CONTEXT_QPC_BYTES_4_PD_S,
			       to_hr_pd(ibqp->pd)->pdn);
		hr_qp->access_flags = attr->qp_access_flags;
		roce_set_field(context->qpc_bytes_8,
			       QP_CONTEXT_QPC_BYTES_8_TX_COMPLETION_M,
			       QP_CONTEXT_QPC_BYTES_8_TX_COMPLETION_S,
			       to_hr_cq(ibqp->send_cq)->cqn);
		roce_set_field(context->qpc_bytes_8,
			       QP_CONTEXT_QPC_BYTES_8_RX_COMPLETION_M,
			       QP_CONTEXT_QPC_BYTES_8_RX_COMPLETION_S,
			       to_hr_cq(ibqp->recv_cq)->cqn);

		if (ibqp->srq)
			roce_set_field(context->qpc_bytes_12,
				       QP_CONTEXT_QPC_BYTES_12_SRQ_NUMBER_M,
				       QP_CONTEXT_QPC_BYTES_12_SRQ_NUMBER_S,
				       to_hr_srq(ibqp->srq)->srqn);

		roce_set_field(context->qpc_bytes_12,
			       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_S,
			       attr->pkey_index);
		hr_qp->pkey_index = attr->pkey_index;
		roce_set_field(context->qpc_bytes_16,
			       QP_CONTEXT_QPC_BYTES_16_QP_NUM_M,
			       QP_CONTEXT_QPC_BYTES_16_QP_NUM_S, hr_qp->qpn);

	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_INIT) {
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_TRANSPORT_SERVICE_TYPE_M,
			       QP_CONTEXT_QPC_BYTES_4_TRANSPORT_SERVICE_TYPE_S,
			       to_hr_qp_type(hr_qp->ibqp.qp_type));
		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_ENABLE_FPMR_S, 0);
		if (attr_mask & IB_QP_ACCESS_FLAGS) {
			roce_set_bit(context->qpc_bytes_4,
				     QP_CONTEXT_QPC_BYTE_4_RDMA_READ_ENABLE_S,
				     !!(attr->qp_access_flags &
				     IB_ACCESS_REMOTE_READ));
			roce_set_bit(context->qpc_bytes_4,
				     QP_CONTEXT_QPC_BYTE_4_RDMA_WRITE_ENABLE_S,
				     !!(attr->qp_access_flags &
				     IB_ACCESS_REMOTE_WRITE));
		} else {
			roce_set_bit(context->qpc_bytes_4,
				     QP_CONTEXT_QPC_BYTE_4_RDMA_READ_ENABLE_S,
				     !!(hr_qp->access_flags &
				     IB_ACCESS_REMOTE_READ));
			roce_set_bit(context->qpc_bytes_4,
				     QP_CONTEXT_QPC_BYTE_4_RDMA_WRITE_ENABLE_S,
				     !!(hr_qp->access_flags &
				     IB_ACCESS_REMOTE_WRITE));
		}

		roce_set_bit(context->qpc_bytes_4,
			     QP_CONTEXT_QPC_BYTE_4_RDMAR_USE_S, 1);
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_SQ_WQE_SHIFT_M,
			       QP_CONTEXT_QPC_BYTES_4_SQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->sq.wqe_cnt));
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_RQ_WQE_SHIFT_M,
			       QP_CONTEXT_QPC_BYTES_4_RQ_WQE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->rq.wqe_cnt));
		roce_set_field(context->qpc_bytes_4,
			       QP_CONTEXT_QPC_BYTES_4_PD_M,
			       QP_CONTEXT_QPC_BYTES_4_PD_S,
			       to_hr_pd(ibqp->pd)->pdn);

		roce_set_field(context->qpc_bytes_8,
			       QP_CONTEXT_QPC_BYTES_8_TX_COMPLETION_M,
			       QP_CONTEXT_QPC_BYTES_8_TX_COMPLETION_S,
			       to_hr_cq(ibqp->send_cq)->cqn);
		roce_set_field(context->qpc_bytes_8,
			       QP_CONTEXT_QPC_BYTES_8_RX_COMPLETION_M,
			       QP_CONTEXT_QPC_BYTES_8_RX_COMPLETION_S,
			       to_hr_cq(ibqp->recv_cq)->cqn);

		if (ibqp->srq)
			roce_set_field(context->qpc_bytes_12,
				       QP_CONTEXT_QPC_BYTES_12_SRQ_NUMBER_M,
				       QP_CONTEXT_QPC_BYTES_12_SRQ_NUMBER_S,
				       to_hr_srq(ibqp->srq)->srqn);
		if (attr_mask & IB_QP_PKEY_INDEX)
			roce_set_field(context->qpc_bytes_12,
				       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_M,
				       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_S,
				       attr->pkey_index);
		else
			roce_set_field(context->qpc_bytes_12,
				       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_M,
				       QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_S,
				       hr_qp->pkey_index);

		roce_set_field(context->qpc_bytes_16,
			       QP_CONTEXT_QPC_BYTES_16_QP_NUM_M,
			       QP_CONTEXT_QPC_BYTES_16_QP_NUM_S, hr_qp->qpn);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		if ((attr_mask & IB_QP_ALT_PATH) ||
		    (attr_mask & IB_QP_ACCESS_FLAGS) ||
		    (attr_mask & IB_QP_PKEY_INDEX) ||
		    (attr_mask & IB_QP_QKEY)) {
			dev_err(dev, "INIT2RTR attr_mask error\n");
			goto out;
		}

		dmac = (u8 *)attr->ah_attr.dmac;

		context->sq_rq_bt_l = (u32)(dma_handle);
		roce_set_field(context->qpc_bytes_24,
			       QP_CONTEXT_QPC_BYTES_24_SQ_RQ_BT_H_M,
			       QP_CONTEXT_QPC_BYTES_24_SQ_RQ_BT_H_S,
			       ((u32)(dma_handle >> 32)));
		roce_set_bit(context->qpc_bytes_24,
			     QP_CONTEXT_QPC_BYTE_24_REMOTE_ENABLE_E2E_CREDITS_S,
			     1);
		roce_set_field(context->qpc_bytes_24,
			       QP_CONTEXT_QPC_BYTES_24_MINIMUM_RNR_NAK_TIMER_M,
			       QP_CONTEXT_QPC_BYTES_24_MINIMUM_RNR_NAK_TIMER_S,
			       attr->min_rnr_timer);
		context->irrl_ba_l = (u32)(dma_handle_2);
		roce_set_field(context->qpc_bytes_32,
			       QP_CONTEXT_QPC_BYTES_32_IRRL_BA_H_M,
			       QP_CONTEXT_QPC_BYTES_32_IRRL_BA_H_S,
			       ((u32)(dma_handle_2 >> 32)) &
				QP_CONTEXT_QPC_BYTES_32_IRRL_BA_H_M);
		roce_set_field(context->qpc_bytes_32,
			       QP_CONTEXT_QPC_BYTES_32_MIG_STATE_M,
			       QP_CONTEXT_QPC_BYTES_32_MIG_STATE_S, 0);
		roce_set_bit(context->qpc_bytes_32,
			     QP_CONTEXT_QPC_BYTE_32_LOCAL_ENABLE_E2E_CREDITS_S,
			     1);
		roce_set_bit(context->qpc_bytes_32,
			     QP_CONTEXT_QPC_BYTE_32_SIGNALING_TYPE_S,
			     hr_qp->sq_signal_bits);

		for (port = 0; port < hr_dev->caps.num_ports; port++) {
			smac = (u8 *)hr_dev->dev_addr[port];
			dev_dbg(dev, "smac: %2x: %2x: %2x: %2x: %2x: %2x\n",
				smac[0], smac[1], smac[2], smac[3], smac[4],
				smac[5]);
			if ((dmac[0] == smac[0]) && (dmac[1] == smac[1]) &&
			    (dmac[2] == smac[2]) && (dmac[3] == smac[3]) &&
			    (dmac[4] == smac[4]) && (dmac[5] == smac[5])) {
				roce_set_bit(context->qpc_bytes_32,
				    QP_CONTEXT_QPC_BYTE_32_LOOPBACK_INDICATOR_S,
				    1);
				break;
			}
		}

		if (hr_dev->loop_idc == 0x1)
			roce_set_bit(context->qpc_bytes_32,
				QP_CONTEXT_QPC_BYTE_32_LOOPBACK_INDICATOR_S, 1);

		roce_set_bit(context->qpc_bytes_32,
			     QP_CONTEXT_QPC_BYTE_32_GLOBAL_HEADER_S,
			     attr->ah_attr.ah_flags);
		roce_set_field(context->qpc_bytes_32,
			       QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_M,
			       QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_S,
			       ilog2((unsigned int)attr->max_dest_rd_atomic));

		roce_set_field(context->qpc_bytes_36,
			       QP_CONTEXT_QPC_BYTES_36_DEST_QP_M,
			       QP_CONTEXT_QPC_BYTES_36_DEST_QP_S,
			       attr->dest_qp_num);

		/* Configure GID index */
		roce_set_field(context->qpc_bytes_36,
			       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_S,
			       hns_get_gid_index(hr_dev,
						 attr->ah_attr.port_num - 1,
						 attr->ah_attr.grh.sgid_index));

		memcpy(&(context->dmac_l), dmac, 4);

		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_DMAC_H_M,
			       QP_CONTEXT_QPC_BYTES_44_DMAC_H_S,
			       *((u16 *)(&dmac[4])));
		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_MAXIMUM_STATIC_RATE_M,
			       QP_CONTEXT_QPC_BYTES_44_MAXIMUM_STATIC_RATE_S,
			       attr->ah_attr.static_rate);
		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_HOPLMT_M,
			       QP_CONTEXT_QPC_BYTES_44_HOPLMT_S,
			       attr->ah_attr.grh.hop_limit);

		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_M,
			       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_S,
			       attr->ah_attr.grh.flow_label);
		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_TCLASS_M,
			       QP_CONTEXT_QPC_BYTES_48_TCLASS_S,
			       attr->ah_attr.grh.traffic_class);
		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_MTU_M,
			       QP_CONTEXT_QPC_BYTES_48_MTU_S, attr->path_mtu);

		memcpy(context->dgid, attr->ah_attr.grh.dgid.raw,
		       sizeof(attr->ah_attr.grh.dgid.raw));

		dev_dbg(dev, "dmac:%x :%lx\n", context->dmac_l,
			roce_get_field(context->qpc_bytes_44,
				       QP_CONTEXT_QPC_BYTES_44_DMAC_H_M,
				       QP_CONTEXT_QPC_BYTES_44_DMAC_H_S));

		roce_set_field(context->qpc_bytes_68,
			       QP_CONTEXT_QPC_BYTES_68_RQ_HEAD_M,
			       QP_CONTEXT_QPC_BYTES_68_RQ_HEAD_S, 0);
		roce_set_field(context->qpc_bytes_68,
			       QP_CONTEXT_QPC_BYTES_68_RQ_CUR_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_68_RQ_CUR_INDEX_S, 0);

		rq_pa_start = (u32)hr_qp->rq.offset / PAGE_SIZE;
		context->cur_rq_wqe_ba_l = (u32)(mtts[rq_pa_start]);

		roce_set_field(context->qpc_bytes_76,
			QP_CONTEXT_QPC_BYTES_76_CUR_RQ_WQE_BA_H_M,
			QP_CONTEXT_QPC_BYTES_76_CUR_RQ_WQE_BA_H_S,
			mtts[rq_pa_start] >> 32);
		roce_set_field(context->qpc_bytes_76,
			       QP_CONTEXT_QPC_BYTES_76_RX_REQ_MSN_M,
			       QP_CONTEXT_QPC_BYTES_76_RX_REQ_MSN_S, 0);

		context->rx_rnr_time = 0;

		roce_set_field(context->qpc_bytes_84,
			       QP_CONTEXT_QPC_BYTES_84_LAST_ACK_PSN_M,
			       QP_CONTEXT_QPC_BYTES_84_LAST_ACK_PSN_S,
			       attr->rq_psn - 1);
		roce_set_field(context->qpc_bytes_84,
			       QP_CONTEXT_QPC_BYTES_84_TRRL_HEAD_M,
			       QP_CONTEXT_QPC_BYTES_84_TRRL_HEAD_S, 0);

		roce_set_field(context->qpc_bytes_88,
			       QP_CONTEXT_QPC_BYTES_88_RX_REQ_EPSN_M,
			       QP_CONTEXT_QPC_BYTES_88_RX_REQ_EPSN_S,
			       attr->rq_psn);
		roce_set_bit(context->qpc_bytes_88,
			     QP_CONTEXT_QPC_BYTES_88_RX_REQ_PSN_ERR_FLAG_S, 0);
		roce_set_bit(context->qpc_bytes_88,
			     QP_CONTEXT_QPC_BYTES_88_RX_LAST_OPCODE_FLG_S, 0);
		roce_set_field(context->qpc_bytes_88,
			QP_CONTEXT_QPC_BYTES_88_RQ_REQ_LAST_OPERATION_TYPE_M,
			QP_CONTEXT_QPC_BYTES_88_RQ_REQ_LAST_OPERATION_TYPE_S,
			0);
		roce_set_field(context->qpc_bytes_88,
			       QP_CONTEXT_QPC_BYTES_88_RQ_REQ_RDMA_WR_FLAG_M,
			       QP_CONTEXT_QPC_BYTES_88_RQ_REQ_RDMA_WR_FLAG_S,
			       0);

		context->dma_length = 0;
		context->r_key = 0;
		context->va_l = 0;
		context->va_h = 0;

		roce_set_field(context->qpc_bytes_108,
			       QP_CONTEXT_QPC_BYTES_108_TRRL_SDB_PSN_M,
			       QP_CONTEXT_QPC_BYTES_108_TRRL_SDB_PSN_S, 0);
		roce_set_bit(context->qpc_bytes_108,
			     QP_CONTEXT_QPC_BYTES_108_TRRL_SDB_PSN_FLG_S, 0);
		roce_set_bit(context->qpc_bytes_108,
			     QP_CONTEXT_QPC_BYTES_108_TRRL_TDB_PSN_FLG_S, 0);

		roce_set_field(context->qpc_bytes_112,
			       QP_CONTEXT_QPC_BYTES_112_TRRL_TDB_PSN_M,
			       QP_CONTEXT_QPC_BYTES_112_TRRL_TDB_PSN_S, 0);
		roce_set_field(context->qpc_bytes_112,
			       QP_CONTEXT_QPC_BYTES_112_TRRL_TAIL_M,
			       QP_CONTEXT_QPC_BYTES_112_TRRL_TAIL_S, 0);

		/* For chip resp ack */
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_PORT_NUM_M,
			       QP_CONTEXT_QPC_BYTES_156_PORT_NUM_S,
			       hr_qp->phy_port);
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_SL_M,
			       QP_CONTEXT_QPC_BYTES_156_SL_S, attr->ah_attr.sl);
		hr_qp->sl = attr->ah_attr.sl;
	} else if (cur_state == IB_QPS_RTR &&
		new_state == IB_QPS_RTS) {
		/* If exist optional param, return error */
		if ((attr_mask & IB_QP_ALT_PATH) ||
		    (attr_mask & IB_QP_ACCESS_FLAGS) ||
		    (attr_mask & IB_QP_QKEY) ||
		    (attr_mask & IB_QP_PATH_MIG_STATE) ||
		    (attr_mask & IB_QP_CUR_STATE) ||
		    (attr_mask & IB_QP_MIN_RNR_TIMER)) {
			dev_err(dev, "RTR2RTS attr_mask error\n");
			goto out;
		}

		context->rx_cur_sq_wqe_ba_l = (u32)(mtts[0]);

		roce_set_field(context->qpc_bytes_120,
			       QP_CONTEXT_QPC_BYTES_120_RX_CUR_SQ_WQE_BA_H_M,
			       QP_CONTEXT_QPC_BYTES_120_RX_CUR_SQ_WQE_BA_H_S,
			       (mtts[0]) >> 32);

		roce_set_field(context->qpc_bytes_124,
			       QP_CONTEXT_QPC_BYTES_124_RX_ACK_MSN_M,
			       QP_CONTEXT_QPC_BYTES_124_RX_ACK_MSN_S, 0);
		roce_set_field(context->qpc_bytes_124,
			       QP_CONTEXT_QPC_BYTES_124_IRRL_MSG_IDX_M,
			       QP_CONTEXT_QPC_BYTES_124_IRRL_MSG_IDX_S, 0);

		roce_set_field(context->qpc_bytes_128,
			       QP_CONTEXT_QPC_BYTES_128_RX_ACK_EPSN_M,
			       QP_CONTEXT_QPC_BYTES_128_RX_ACK_EPSN_S,
			       attr->sq_psn);
		roce_set_bit(context->qpc_bytes_128,
			     QP_CONTEXT_QPC_BYTES_128_RX_ACK_PSN_ERR_FLG_S, 0);
		roce_set_field(context->qpc_bytes_128,
			     QP_CONTEXT_QPC_BYTES_128_ACK_LAST_OPERATION_TYPE_M,
			     QP_CONTEXT_QPC_BYTES_128_ACK_LAST_OPERATION_TYPE_S,
			     0);
		roce_set_bit(context->qpc_bytes_128,
			     QP_CONTEXT_QPC_BYTES_128_IRRL_PSN_VLD_FLG_S, 0);

		roce_set_field(context->qpc_bytes_132,
			       QP_CONTEXT_QPC_BYTES_132_IRRL_PSN_M,
			       QP_CONTEXT_QPC_BYTES_132_IRRL_PSN_S, 0);
		roce_set_field(context->qpc_bytes_132,
			       QP_CONTEXT_QPC_BYTES_132_IRRL_TAIL_M,
			       QP_CONTEXT_QPC_BYTES_132_IRRL_TAIL_S, 0);

		roce_set_field(context->qpc_bytes_136,
			       QP_CONTEXT_QPC_BYTES_136_RETRY_MSG_PSN_M,
			       QP_CONTEXT_QPC_BYTES_136_RETRY_MSG_PSN_S,
			       attr->sq_psn);
		roce_set_field(context->qpc_bytes_136,
			       QP_CONTEXT_QPC_BYTES_136_RETRY_MSG_FPKT_PSN_L_M,
			       QP_CONTEXT_QPC_BYTES_136_RETRY_MSG_FPKT_PSN_L_S,
			       attr->sq_psn);

		roce_set_field(context->qpc_bytes_140,
			       QP_CONTEXT_QPC_BYTES_140_RETRY_MSG_FPKT_PSN_H_M,
			       QP_CONTEXT_QPC_BYTES_140_RETRY_MSG_FPKT_PSN_H_S,
			       (attr->sq_psn >> SQ_PSN_SHIFT));
		roce_set_field(context->qpc_bytes_140,
			       QP_CONTEXT_QPC_BYTES_140_RETRY_MSG_MSN_M,
			       QP_CONTEXT_QPC_BYTES_140_RETRY_MSG_MSN_S, 0);
		roce_set_bit(context->qpc_bytes_140,
			     QP_CONTEXT_QPC_BYTES_140_RNR_RETRY_FLG_S, 0);

		roce_set_field(context->qpc_bytes_144,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_M,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_S,
			       attr->qp_state);

		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_CHECK_FLAG_M,
			       QP_CONTEXT_QPC_BYTES_148_CHECK_FLAG_S, 0);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_M,
			       QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_S, 0);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_RNR_RETRY_COUNT_M,
			       QP_CONTEXT_QPC_BYTES_148_RNR_RETRY_COUNT_S, 0);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_LSN_M,
			       QP_CONTEXT_QPC_BYTES_148_LSN_S, 0x100);

		context->rnr_retry = 0;

		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_RETRY_COUNT_INIT_M,
			       QP_CONTEXT_QPC_BYTES_156_RETRY_COUNT_INIT_S,
			       attr->retry_cnt);
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_M,
			       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_S,
			       attr->timeout);
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_RNR_RETRY_COUNT_INIT_M,
			       QP_CONTEXT_QPC_BYTES_156_RNR_RETRY_COUNT_INIT_S,
			       attr->rnr_retry);
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_PORT_NUM_M,
			       QP_CONTEXT_QPC_BYTES_156_PORT_NUM_S,
			       hr_qp->phy_port);
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_SL_M,
			       QP_CONTEXT_QPC_BYTES_156_SL_S, attr->ah_attr.sl);
		hr_qp->sl = attr->ah_attr.sl;
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_M,
			       QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_S,
			       ilog2((unsigned int)attr->max_rd_atomic));
		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_ACK_REQ_IND_M,
			       QP_CONTEXT_QPC_BYTES_156_ACK_REQ_IND_S, 0);
		context->pkt_use_len = 0;

		roce_set_field(context->qpc_bytes_164,
			       QP_CONTEXT_QPC_BYTES_164_SQ_PSN_M,
			       QP_CONTEXT_QPC_BYTES_164_SQ_PSN_S, attr->sq_psn);
		roce_set_field(context->qpc_bytes_164,
			       QP_CONTEXT_QPC_BYTES_164_IRRL_HEAD_M,
			       QP_CONTEXT_QPC_BYTES_164_IRRL_HEAD_S, 0);

		roce_set_field(context->qpc_bytes_168,
			       QP_CONTEXT_QPC_BYTES_168_RETRY_SQ_PSN_M,
			       QP_CONTEXT_QPC_BYTES_168_RETRY_SQ_PSN_S,
			       attr->sq_psn);
		roce_set_field(context->qpc_bytes_168,
			       QP_CONTEXT_QPC_BYTES_168_SGE_USE_FLA_M,
			       QP_CONTEXT_QPC_BYTES_168_SGE_USE_FLA_S, 0);
		roce_set_field(context->qpc_bytes_168,
			       QP_CONTEXT_QPC_BYTES_168_DB_TYPE_M,
			       QP_CONTEXT_QPC_BYTES_168_DB_TYPE_S, 0);
		roce_set_bit(context->qpc_bytes_168,
			     QP_CONTEXT_QPC_BYTES_168_MSG_LP_IND_S, 0);
		roce_set_bit(context->qpc_bytes_168,
			     QP_CONTEXT_QPC_BYTES_168_CSDB_LP_IND_S, 0);
		roce_set_bit(context->qpc_bytes_168,
			     QP_CONTEXT_QPC_BYTES_168_QP_ERR_FLG_S, 0);
		context->sge_use_len = 0;

		roce_set_field(context->qpc_bytes_176,
			       QP_CONTEXT_QPC_BYTES_176_DB_CUR_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_176_DB_CUR_INDEX_S, 0);
		roce_set_field(context->qpc_bytes_176,
			       QP_CONTEXT_QPC_BYTES_176_RETRY_DB_CUR_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_176_RETRY_DB_CUR_INDEX_S,
			       0);
		roce_set_field(context->qpc_bytes_180,
			       QP_CONTEXT_QPC_BYTES_180_SQ_CUR_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_180_SQ_CUR_INDEX_S, 0);
		roce_set_field(context->qpc_bytes_180,
			       QP_CONTEXT_QPC_BYTES_180_SQ_HEAD_M,
			       QP_CONTEXT_QPC_BYTES_180_SQ_HEAD_S, 0);

		context->tx_cur_sq_wqe_ba_l = (u32)(mtts[0]);

		roce_set_field(context->qpc_bytes_188,
			       QP_CONTEXT_QPC_BYTES_188_TX_CUR_SQ_WQE_BA_H_M,
			       QP_CONTEXT_QPC_BYTES_188_TX_CUR_SQ_WQE_BA_H_S,
			       (mtts[0]) >> 32);
		roce_set_bit(context->qpc_bytes_188,
			     QP_CONTEXT_QPC_BYTES_188_PKT_RETRY_FLG_S, 0);
		roce_set_field(context->qpc_bytes_188,
			       QP_CONTEXT_QPC_BYTES_188_TX_RETRY_CUR_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_188_TX_RETRY_CUR_INDEX_S,
			       0);
	} else if ((cur_state == IB_QPS_INIT && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_INIT && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_RTR && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_RTR && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_RTS && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_RTS && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_ERR && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_ERR && new_state == IB_QPS_ERR)) {
		roce_set_field(context->qpc_bytes_144,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_M,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_S,
			       attr->qp_state);

	} else {
		dev_err(dev, "not support this modify\n");
		goto out;
	}

	/* Every status migrate must change state */
	roce_set_field(context->qpc_bytes_144,
		       QP_CONTEXT_QPC_BYTES_144_QP_STATE_M,
		       QP_CONTEXT_QPC_BYTES_144_QP_STATE_S, attr->qp_state);

	/* SW pass context to HW */
	ret = hns_roce_v1_qp_modify(hr_dev, &hr_qp->mtt,
				    to_hns_roce_state(cur_state),
				    to_hns_roce_state(new_state), context,
				    hr_qp);
	if (ret) {
		dev_err(dev, "hns_roce_qp_modify failed\n");
		goto out;
	}

	/*
	* Use rst2init to instead of init2init with drv,
	* need to hw to flash RQ HEAD by DB again
	*/
	if (cur_state == IB_QPS_INIT && new_state == IB_QPS_INIT) {
		/* Memory barrier */
		wmb();

		roce_set_field(doorbell[0], RQ_DOORBELL_U32_4_RQ_HEAD_M,
			       RQ_DOORBELL_U32_4_RQ_HEAD_S, hr_qp->rq.head);
		roce_set_field(doorbell[1], RQ_DOORBELL_U32_8_QPN_M,
			       RQ_DOORBELL_U32_8_QPN_S, hr_qp->qpn);
		roce_set_field(doorbell[1], RQ_DOORBELL_U32_8_CMD_M,
			       RQ_DOORBELL_U32_8_CMD_S, 1);
		roce_set_bit(doorbell[1], RQ_DOORBELL_U32_8_HW_SYNC_S, 1);

		if (ibqp->uobject) {
			hr_qp->rq.db_reg_l = hr_dev->reg_base +
				     ROCEE_DB_OTHERS_L_0_REG +
				     DB_REG_OFFSET * hr_dev->priv_uar.index;
		}

		hns_roce_write64_k(doorbell, hr_qp->rq.db_reg_l);
	}

	hr_qp->state = new_state;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		hr_qp->resp_depth = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_PORT) {
		hr_qp->port = attr->port_num - 1;
		hr_qp->phy_port = hr_dev->iboe.phy_port[hr_qp->port];
	}

	if (new_state == IB_QPS_RESET && !ibqp->uobject) {
		hns_roce_v1_cq_clean(to_hr_cq(ibqp->recv_cq), hr_qp->qpn,
				     ibqp->srq ? to_hr_srq(ibqp->srq) : NULL);
		if (ibqp->send_cq != ibqp->recv_cq)
			hns_roce_v1_cq_clean(to_hr_cq(ibqp->send_cq),
					     hr_qp->qpn, NULL);

		hr_qp->rq.head = 0;
		hr_qp->rq.tail = 0;
		hr_qp->sq.head = 0;
		hr_qp->sq.tail = 0;
		hr_qp->sq_next_wqe = 0;
	}
out:
	kfree(context);
	return ret;
}

int hns_roce_v1_modify_qp(struct ib_qp *ibqp, const struct ib_qp_attr *attr,
			  int attr_mask, enum ib_qp_state cur_state,
			  enum ib_qp_state new_state)
{

	if (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_SMI)
		return hns_roce_v1_m_sqp(ibqp, attr, attr_mask, cur_state,
					 new_state);
	else
		return hns_roce_v1_m_qp(ibqp, attr, attr_mask, cur_state,
					new_state);
}

static enum ib_qp_state to_ib_qp_state(enum hns_roce_qp_state state)
{
	switch (state) {
	case HNS_ROCE_QP_STATE_RST:
		return IB_QPS_RESET;
	case HNS_ROCE_QP_STATE_INIT:
		return IB_QPS_INIT;
	case HNS_ROCE_QP_STATE_RTR:
		return IB_QPS_RTR;
	case HNS_ROCE_QP_STATE_RTS:
		return IB_QPS_RTS;
	case HNS_ROCE_QP_STATE_SQD:
		return IB_QPS_SQD;
	case HNS_ROCE_QP_STATE_ERR:
		return IB_QPS_ERR;
	default:
		return IB_QPS_ERR;
	}
}

static int hns_roce_v1_query_qpc(struct hns_roce_dev *hr_dev,
				 struct hns_roce_qp *hr_qp,
				 struct hns_roce_qp_context *hr_context)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, hr_qp->qpn, 0,
				HNS_ROCE_CMD_QUERY_QP,
				HNS_ROCE_CMD_TIME_CLASS_A);
	if (!ret)
		memcpy(hr_context, mailbox->buf, sizeof(*hr_context));
	else
		dev_err(&hr_dev->pdev->dev, "QUERY QP cmd process error\n");

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

int hns_roce_v1_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
			 int qp_attr_mask, struct ib_qp_init_attr *qp_init_attr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_qp_context *context;
	int tmp_qp_state = 0;
	int ret = 0;
	int state;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	mutex_lock(&hr_qp->mutex);

	if (hr_qp->state == IB_QPS_RESET) {
		qp_attr->qp_state = IB_QPS_RESET;
		goto done;
	}

	ret = hns_roce_v1_query_qpc(hr_dev, hr_qp, context);
	if (ret) {
		dev_err(dev, "query qpc error\n");
		ret = -EINVAL;
		goto out;
	}

	state = roce_get_field(context->qpc_bytes_144,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_M,
			       QP_CONTEXT_QPC_BYTES_144_QP_STATE_S);
	tmp_qp_state = (int)to_ib_qp_state((enum hns_roce_qp_state)state);
	if (tmp_qp_state == -1) {
		dev_err(dev, "to_ib_qp_state error\n");
		ret = -EINVAL;
		goto out;
	}
	hr_qp->state = (u8)tmp_qp_state;
	qp_attr->qp_state = (enum ib_qp_state)hr_qp->state;
	qp_attr->path_mtu = (enum ib_mtu)roce_get_field(context->qpc_bytes_48,
					       QP_CONTEXT_QPC_BYTES_48_MTU_M,
					       QP_CONTEXT_QPC_BYTES_48_MTU_S);
	qp_attr->path_mig_state = IB_MIG_ARMED;
	if (hr_qp->ibqp.qp_type == IB_QPT_UD)
		qp_attr->qkey = QKEY_VAL;

	qp_attr->rq_psn = roce_get_field(context->qpc_bytes_88,
					 QP_CONTEXT_QPC_BYTES_88_RX_REQ_EPSN_M,
					 QP_CONTEXT_QPC_BYTES_88_RX_REQ_EPSN_S);
	qp_attr->sq_psn = (u32)roce_get_field(context->qpc_bytes_164,
					     QP_CONTEXT_QPC_BYTES_164_SQ_PSN_M,
					     QP_CONTEXT_QPC_BYTES_164_SQ_PSN_S);
	qp_attr->dest_qp_num = (u8)roce_get_field(context->qpc_bytes_36,
					QP_CONTEXT_QPC_BYTES_36_DEST_QP_M,
					QP_CONTEXT_QPC_BYTES_36_DEST_QP_S);
	qp_attr->qp_access_flags = ((roce_get_bit(context->qpc_bytes_4,
			QP_CONTEXT_QPC_BYTE_4_RDMA_READ_ENABLE_S)) << 2) |
				   ((roce_get_bit(context->qpc_bytes_4,
			QP_CONTEXT_QPC_BYTE_4_RDMA_WRITE_ENABLE_S)) << 1) |
				   ((roce_get_bit(context->qpc_bytes_4,
			QP_CONTEXT_QPC_BYTE_4_ATOMIC_OPERATION_ENABLE_S)) << 3);

	if (hr_qp->ibqp.qp_type == IB_QPT_RC ||
	    hr_qp->ibqp.qp_type == IB_QPT_UC) {
		qp_attr->ah_attr.sl = roce_get_field(context->qpc_bytes_156,
						QP_CONTEXT_QPC_BYTES_156_SL_M,
						QP_CONTEXT_QPC_BYTES_156_SL_S);
		qp_attr->ah_attr.grh.flow_label = roce_get_field(
					context->qpc_bytes_48,
					QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_M,
					QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_S);
		qp_attr->ah_attr.grh.sgid_index = roce_get_field(
					context->qpc_bytes_36,
					QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_M,
					QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_S);
		qp_attr->ah_attr.grh.hop_limit = roce_get_field(
					context->qpc_bytes_44,
					QP_CONTEXT_QPC_BYTES_44_HOPLMT_M,
					QP_CONTEXT_QPC_BYTES_44_HOPLMT_S);
		qp_attr->ah_attr.grh.traffic_class = roce_get_field(
					context->qpc_bytes_48,
					QP_CONTEXT_QPC_BYTES_48_TCLASS_M,
					QP_CONTEXT_QPC_BYTES_48_TCLASS_S);

		memcpy(qp_attr->ah_attr.grh.dgid.raw, context->dgid,
		       sizeof(qp_attr->ah_attr.grh.dgid.raw));
	}

	qp_attr->pkey_index = roce_get_field(context->qpc_bytes_12,
			      QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_M,
			      QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_S);
	qp_attr->port_num = (u8)roce_get_field(context->qpc_bytes_156,
			     QP_CONTEXT_QPC_BYTES_156_PORT_NUM_M,
			     QP_CONTEXT_QPC_BYTES_156_PORT_NUM_S) + 1;
	qp_attr->sq_draining = 0;
	qp_attr->max_rd_atomic = roce_get_field(context->qpc_bytes_156,
				 QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_M,
				 QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_S);
	qp_attr->max_dest_rd_atomic = roce_get_field(context->qpc_bytes_32,
				 QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_M,
				 QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_S);
	qp_attr->min_rnr_timer = (u8)(roce_get_field(context->qpc_bytes_24,
			QP_CONTEXT_QPC_BYTES_24_MINIMUM_RNR_NAK_TIMER_M,
			QP_CONTEXT_QPC_BYTES_24_MINIMUM_RNR_NAK_TIMER_S));
	qp_attr->timeout = (u8)(roce_get_field(context->qpc_bytes_156,
			    QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_M,
			    QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_S));
	qp_attr->retry_cnt = roce_get_field(context->qpc_bytes_148,
			     QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_M,
			     QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_S);
	qp_attr->rnr_retry = context->rnr_retry;

done:
	qp_attr->cur_qp_state = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr = hr_qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge = hr_qp->rq.max_gs;

	if (!ibqp->uobject) {
		qp_attr->cap.max_send_wr = hr_qp->sq.wqe_cnt;
		qp_attr->cap.max_send_sge = hr_qp->sq.max_gs;
	} else {
		qp_attr->cap.max_send_wr = 0;
		qp_attr->cap.max_send_sge = 0;
	}

	qp_init_attr->cap = qp_attr->cap;

out:
	mutex_unlock(&hr_qp->mutex);
	kfree(context);
	return ret;
}

static void hns_roce_v1_destroy_qp_common(struct hns_roce_dev *hr_dev,
					  struct hns_roce_qp *hr_qp,
					  int is_user)
{
	u32 sdbinvcnt;
	unsigned long end = 0;
	u32 sdbinvcnt_val;
	u32 sdbsendptr_val;
	u32 sdbisusepr_val;
	struct hns_roce_cq *send_cq, *recv_cq;
	struct device *dev = &hr_dev->pdev->dev;

	if (hr_qp->ibqp.qp_type == IB_QPT_RC) {
		if (hr_qp->state != IB_QPS_RESET) {
			/*
			* Set qp to ERR,
			* waiting for hw complete processing all dbs
			*/
			if (hns_roce_v1_qp_modify(hr_dev, NULL,
					to_hns_roce_state(
						(enum ib_qp_state)hr_qp->state),
						HNS_ROCE_QP_STATE_ERR, NULL,
						hr_qp))
				dev_err(dev, "modify QP %06lx to ERR failed.\n",
					hr_qp->qpn);

			/* Record issued doorbell */
			sdbisusepr_val = roce_read(hr_dev,
					 ROCEE_SDB_ISSUE_PTR_REG);
			/*
			* Query db process status,
			* until hw process completely
			*/
			end = msecs_to_jiffies(
			      HNS_ROCE_QP_DESTROY_TIMEOUT_MSECS) + jiffies;
			do {
				sdbsendptr_val = roce_read(hr_dev,
						 ROCEE_SDB_SEND_PTR_REG);
				if (!time_before(jiffies, end)) {
					dev_err(dev, "destroy qp(0x%lx) timeout!!!",
						hr_qp->qpn);
					break;
				}
			} while ((short)(roce_get_field(sdbsendptr_val,
					ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
					ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S) -
				roce_get_field(sdbisusepr_val,
					ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_M,
					ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_S)
				) < 0);

			/* Get list pointer */
			sdbinvcnt = roce_read(hr_dev, ROCEE_SDB_INV_CNT_REG);

			/* Query db's list status, until hw reversal */
			do {
				sdbinvcnt_val = roce_read(hr_dev,
						ROCEE_SDB_INV_CNT_REG);
				if (!time_before(jiffies, end)) {
					dev_err(dev, "destroy qp(0x%lx) timeout!!!",
						hr_qp->qpn);
					dev_err(dev, "SdbInvCnt = 0x%x\n",
						sdbinvcnt_val);
					break;
				}
			} while ((short)(roce_get_field(sdbinvcnt_val,
				  ROCEE_SDB_INV_CNT_SDB_INV_CNT_M,
				  ROCEE_SDB_INV_CNT_SDB_INV_CNT_S) -
				  (sdbinvcnt + SDB_INV_CNT_OFFSET)) < 0);

			/* Modify qp to reset before destroying qp */
			if (hns_roce_v1_qp_modify(hr_dev, NULL,
					to_hns_roce_state(
					(enum ib_qp_state)hr_qp->state),
					HNS_ROCE_QP_STATE_RST, NULL, hr_qp))
				dev_err(dev, "modify QP %06lx to RESET failed.\n",
					hr_qp->qpn);
		}
	}

	send_cq = to_hr_cq(hr_qp->ibqp.send_cq);
	recv_cq = to_hr_cq(hr_qp->ibqp.recv_cq);

	hns_roce_lock_cqs(send_cq, recv_cq);

	if (!is_user) {
		__hns_roce_v1_cq_clean(recv_cq, hr_qp->qpn, hr_qp->ibqp.srq ?
				       to_hr_srq(hr_qp->ibqp.srq) : NULL);
		if (send_cq != recv_cq)
			__hns_roce_v1_cq_clean(send_cq, hr_qp->qpn, NULL);
	}

	hns_roce_qp_remove(hr_dev, hr_qp);

	hns_roce_unlock_cqs(send_cq, recv_cq);

	hns_roce_qp_free(hr_dev, hr_qp);

	/* Not special_QP, free their QPN */
	if ((hr_qp->ibqp.qp_type == IB_QPT_RC) ||
	    (hr_qp->ibqp.qp_type == IB_QPT_UC) ||
	    (hr_qp->ibqp.qp_type == IB_QPT_UD))
		hns_roce_release_range_qp(hr_dev, hr_qp->qpn, 1);

	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);

	if (is_user) {
		ib_umem_release(hr_qp->umem);
	} else {
		kfree(hr_qp->sq.wrid);
		kfree(hr_qp->rq.wrid);
		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);
	}
}

int hns_roce_v1_destroy_qp(struct ib_qp *ibqp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);

	hns_roce_v1_destroy_qp_common(hr_dev, hr_qp, !!ibqp->pd->uobject);

	if (hr_qp->ibqp.qp_type == IB_QPT_GSI)
		kfree(hr_to_hr_sqp(hr_qp));
	else
		kfree(hr_qp);

	return 0;
}

struct hns_roce_v1_priv hr_v1_priv;

struct hns_roce_hw hns_roce_hw_v1 = {
	.reset = hns_roce_v1_reset,
	.hw_profile = hns_roce_v1_profile,
	.hw_init = hns_roce_v1_init,
	.hw_exit = hns_roce_v1_exit,
	.set_gid = hns_roce_v1_set_gid,
	.set_mac = hns_roce_v1_set_mac,
	.set_mtu = hns_roce_v1_set_mtu,
	.write_mtpt = hns_roce_v1_write_mtpt,
	.write_cqc = hns_roce_v1_write_cqc,
	.modify_qp = hns_roce_v1_modify_qp,
	.query_qp = hns_roce_v1_query_qp,
	.destroy_qp = hns_roce_v1_destroy_qp,
	.post_send = hns_roce_v1_post_send,
	.post_recv = hns_roce_v1_post_recv,
	.req_notify_cq = hns_roce_v1_req_notify_cq,
	.poll_cq = hns_roce_v1_poll_cq,
	.priv = &hr_v1_priv,
};
