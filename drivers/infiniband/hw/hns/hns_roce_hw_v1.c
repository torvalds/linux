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
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_platform.h>
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

static int hns_roce_v1_post_send(struct ib_qp *ibqp, struct ib_send_wr *wr,
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
	u8 *smac;
	int loopback;

	if (unlikely(ibqp->qp_type != IB_QPT_GSI &&
		ibqp->qp_type != IB_QPT_RC)) {
		dev_err(dev, "un-supported QP type\n");
		*bad_wr = NULL;
		return -EOPNOTSUPP;
	}

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

			smac = (u8 *)hr_dev->dev_addr[qp->port];
			loopback = ether_addr_equal_unaligned(ah->av.mac,
							      smac) ? 1 : 0;
			roce_set_bit(ud_sq_wqe->u32_8,
				     UD_SEND_WQE_U32_8_LOOPBACK_INDICATOR_S,
				     loopback);

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
				set_raddr_seg(wqe,  rdma_wr(wr)->remote_addr,
					       rdma_wr(wr)->rkey);
				break;
			case IB_WR_RDMA_WRITE:
			case IB_WR_RDMA_WRITE_WITH_IMM:
				ps_opcode = HNS_ROCE_WQE_OPCODE_RDMA_WRITE;
				set_raddr_seg(wqe,  rdma_wr(wr)->remote_addr,
					      rdma_wr(wr)->rkey);
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
		roce_set_field(sq_db.u32_4, SQ_DOORBELL_U32_4_SL_M,
			       SQ_DOORBELL_U32_4_SL_S, qp->sl);
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

static int hns_roce_v1_post_recv(struct ib_qp *ibqp, struct ib_recv_wr *wr,
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

static struct hns_roce_qp *hns_roce_v1_create_lp_qp(struct hns_roce_dev *hr_dev,
						    struct ib_pd *pd)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct ib_qp_init_attr init_attr;
	struct ib_qp *qp;

	memset(&init_attr, 0, sizeof(struct ib_qp_init_attr));
	init_attr.qp_type		= IB_QPT_RC;
	init_attr.sq_sig_type		= IB_SIGNAL_ALL_WR;
	init_attr.cap.max_recv_wr	= HNS_ROCE_MIN_WQE_NUM;
	init_attr.cap.max_send_wr	= HNS_ROCE_MIN_WQE_NUM;

	qp = hns_roce_create_qp(pd, &init_attr, NULL);
	if (IS_ERR(qp)) {
		dev_err(dev, "Create loop qp for mr free failed!");
		return NULL;
	}

	return to_hr_qp(qp);
}

static int hns_roce_v1_rsv_lp_qp(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;
	struct device *dev = &hr_dev->pdev->dev;
	struct ib_cq_init_attr cq_init_attr;
	struct hns_roce_free_mr *free_mr;
	struct ib_qp_attr attr = { 0 };
	struct hns_roce_v1_priv *priv;
	struct hns_roce_qp *hr_qp;
	struct ib_cq *cq;
	struct ib_pd *pd;
	union ib_gid dgid;
	u64 subnet_prefix;
	int attr_mask = 0;
	int i, j;
	int ret;
	u8 queue_en[HNS_ROCE_V1_RESV_QP] = { 0 };
	u8 phy_port;
	u8 port = 0;
	u8 sl;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	/* Reserved cq for loop qp */
	cq_init_attr.cqe		= HNS_ROCE_MIN_WQE_NUM * 2;
	cq_init_attr.comp_vector	= 0;
	cq = hns_roce_ib_create_cq(&hr_dev->ib_dev, &cq_init_attr, NULL, NULL);
	if (IS_ERR(cq)) {
		dev_err(dev, "Create cq for reseved loop qp failed!");
		return -ENOMEM;
	}
	free_mr->mr_free_cq = to_hr_cq(cq);
	free_mr->mr_free_cq->ib_cq.device		= &hr_dev->ib_dev;
	free_mr->mr_free_cq->ib_cq.uobject		= NULL;
	free_mr->mr_free_cq->ib_cq.comp_handler		= NULL;
	free_mr->mr_free_cq->ib_cq.event_handler	= NULL;
	free_mr->mr_free_cq->ib_cq.cq_context		= NULL;
	atomic_set(&free_mr->mr_free_cq->ib_cq.usecnt, 0);

	pd = hns_roce_alloc_pd(&hr_dev->ib_dev, NULL, NULL);
	if (IS_ERR(pd)) {
		dev_err(dev, "Create pd for reseved loop qp failed!");
		ret = -ENOMEM;
		goto alloc_pd_failed;
	}
	free_mr->mr_free_pd = to_hr_pd(pd);
	free_mr->mr_free_pd->ibpd.device  = &hr_dev->ib_dev;
	free_mr->mr_free_pd->ibpd.uobject = NULL;
	atomic_set(&free_mr->mr_free_pd->ibpd.usecnt, 0);

	attr.qp_access_flags	= IB_ACCESS_REMOTE_WRITE;
	attr.pkey_index		= 0;
	attr.min_rnr_timer	= 0;
	/* Disable read ability */
	attr.max_dest_rd_atomic = 0;
	attr.max_rd_atomic	= 0;
	/* Use arbitrary values as rq_psn and sq_psn */
	attr.rq_psn		= 0x0808;
	attr.sq_psn		= 0x0808;
	attr.retry_cnt		= 7;
	attr.rnr_retry		= 7;
	attr.timeout		= 0x12;
	attr.path_mtu		= IB_MTU_256;
	attr.ah_attr.type	= RDMA_AH_ATTR_TYPE_ROCE;
	rdma_ah_set_grh(&attr.ah_attr, NULL, 0, 0, 1, 0);
	rdma_ah_set_static_rate(&attr.ah_attr, 3);

	subnet_prefix = cpu_to_be64(0xfe80000000000000LL);
	for (i = 0; i < HNS_ROCE_V1_RESV_QP; i++) {
		phy_port = (i >= HNS_ROCE_MAX_PORTS) ? (i - 2) :
				(i % HNS_ROCE_MAX_PORTS);
		sl = i / HNS_ROCE_MAX_PORTS;

		for (j = 0; j < caps->num_ports; j++) {
			if (hr_dev->iboe.phy_port[j] == phy_port) {
				queue_en[i] = 1;
				port = j;
				break;
			}
		}

		if (!queue_en[i])
			continue;

		free_mr->mr_free_qp[i] = hns_roce_v1_create_lp_qp(hr_dev, pd);
		if (!free_mr->mr_free_qp[i]) {
			dev_err(dev, "Create loop qp failed!\n");
			goto create_lp_qp_failed;
		}
		hr_qp = free_mr->mr_free_qp[i];

		hr_qp->port		= port;
		hr_qp->phy_port		= phy_port;
		hr_qp->ibqp.qp_type	= IB_QPT_RC;
		hr_qp->ibqp.device	= &hr_dev->ib_dev;
		hr_qp->ibqp.uobject	= NULL;
		atomic_set(&hr_qp->ibqp.usecnt, 0);
		hr_qp->ibqp.pd		= pd;
		hr_qp->ibqp.recv_cq	= cq;
		hr_qp->ibqp.send_cq	= cq;

		rdma_ah_set_port_num(&attr.ah_attr, port + 1);
		rdma_ah_set_sl(&attr.ah_attr, sl);
		attr.port_num		= port + 1;

		attr.dest_qp_num	= hr_qp->qpn;
		memcpy(rdma_ah_retrieve_dmac(&attr.ah_attr),
		       hr_dev->dev_addr[port],
		       MAC_ADDR_OCTET_NUM);

		memcpy(&dgid.raw, &subnet_prefix, sizeof(u64));
		memcpy(&dgid.raw[8], hr_dev->dev_addr[port], 3);
		memcpy(&dgid.raw[13], hr_dev->dev_addr[port] + 3, 3);
		dgid.raw[11] = 0xff;
		dgid.raw[12] = 0xfe;
		dgid.raw[8] ^= 2;
		rdma_ah_set_dgid_raw(&attr.ah_attr, dgid.raw);

		ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, &attr, attr_mask,
					    IB_QPS_RESET, IB_QPS_INIT);
		if (ret) {
			dev_err(dev, "modify qp failed(%d)!\n", ret);
			goto create_lp_qp_failed;
		}

		ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, &attr, attr_mask,
					    IB_QPS_INIT, IB_QPS_RTR);
		if (ret) {
			dev_err(dev, "modify qp failed(%d)!\n", ret);
			goto create_lp_qp_failed;
		}

		ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, &attr, attr_mask,
					    IB_QPS_RTR, IB_QPS_RTS);
		if (ret) {
			dev_err(dev, "modify qp failed(%d)!\n", ret);
			goto create_lp_qp_failed;
		}
	}

	return 0;

create_lp_qp_failed:
	for (i -= 1; i >= 0; i--) {
		hr_qp = free_mr->mr_free_qp[i];
		if (hns_roce_v1_destroy_qp(&hr_qp->ibqp))
			dev_err(dev, "Destroy qp %d for mr free failed!\n", i);
	}

	if (hns_roce_dealloc_pd(pd))
		dev_err(dev, "Destroy pd for create_lp_qp failed!\n");

alloc_pd_failed:
	if (hns_roce_ib_destroy_cq(cq))
		dev_err(dev, "Destroy cq for create_lp_qp failed!\n");

	return -EINVAL;
}

static void hns_roce_v1_release_lp_qp(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_qp *hr_qp;
	int ret;
	int i;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	for (i = 0; i < HNS_ROCE_V1_RESV_QP; i++) {
		hr_qp = free_mr->mr_free_qp[i];
		if (!hr_qp)
			continue;

		ret = hns_roce_v1_destroy_qp(&hr_qp->ibqp);
		if (ret)
			dev_err(dev, "Destroy qp %d for mr free failed(%d)!\n",
				i, ret);
	}

	ret = hns_roce_ib_destroy_cq(&free_mr->mr_free_cq->ib_cq);
	if (ret)
		dev_err(dev, "Destroy cq for mr_free failed(%d)!\n", ret);

	ret = hns_roce_dealloc_pd(&free_mr->mr_free_pd->ibpd);
	if (ret)
		dev_err(dev, "Destroy pd for mr_free failed(%d)!\n", ret);
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

static void hns_roce_v1_recreate_lp_qp_work_fn(struct work_struct *work)
{
	struct hns_roce_recreate_lp_qp_work *lp_qp_work;
	struct hns_roce_dev *hr_dev;

	lp_qp_work = container_of(work, struct hns_roce_recreate_lp_qp_work,
				  work);
	hr_dev = to_hr_dev(lp_qp_work->ib_dev);

	hns_roce_v1_release_lp_qp(hr_dev);

	if (hns_roce_v1_rsv_lp_qp(hr_dev))
		dev_err(&hr_dev->pdev->dev, "create reserver qp failed\n");

	if (lp_qp_work->comp_flag)
		complete(lp_qp_work->comp);

	kfree(lp_qp_work);
}

static int hns_roce_v1_recreate_lp_qp(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_recreate_lp_qp_work *lp_qp_work;
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_v1_priv *priv;
	struct completion comp;
	unsigned long end =
	  msecs_to_jiffies(HNS_ROCE_V1_RECREATE_LP_QP_TIMEOUT_MSECS) + jiffies;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	lp_qp_work = kzalloc(sizeof(struct hns_roce_recreate_lp_qp_work),
			     GFP_KERNEL);
	if (!lp_qp_work)
		return -ENOMEM;

	INIT_WORK(&(lp_qp_work->work), hns_roce_v1_recreate_lp_qp_work_fn);

	lp_qp_work->ib_dev = &(hr_dev->ib_dev);
	lp_qp_work->comp = &comp;
	lp_qp_work->comp_flag = 1;

	init_completion(lp_qp_work->comp);

	queue_work(free_mr->free_mr_wq, &(lp_qp_work->work));

	while (time_before_eq(jiffies, end)) {
		if (try_wait_for_completion(&comp))
			return 0;
		msleep(HNS_ROCE_V1_RECREATE_LP_QP_WAIT_VALUE);
	}

	lp_qp_work->comp_flag = 0;
	if (try_wait_for_completion(&comp))
		return 0;

	dev_warn(dev, "recreate lp qp failed 20s timeout and return failed!\n");
	return -ETIMEDOUT;
}

static int hns_roce_v1_send_lp_wqe(struct hns_roce_qp *hr_qp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_qp->ibqp.device);
	struct device *dev = &hr_dev->pdev->dev;
	struct ib_send_wr send_wr, *bad_wr;
	int ret;

	memset(&send_wr, 0, sizeof(send_wr));
	send_wr.next	= NULL;
	send_wr.num_sge	= 0;
	send_wr.send_flags = 0;
	send_wr.sg_list	= NULL;
	send_wr.wr_id	= (unsigned long long)&send_wr;
	send_wr.opcode	= IB_WR_RDMA_WRITE;

	ret = hns_roce_v1_post_send(&hr_qp->ibqp, &send_wr, &bad_wr);
	if (ret) {
		dev_err(dev, "Post write wqe for mr free failed(%d)!", ret);
		return ret;
	}

	return 0;
}

static void hns_roce_v1_mr_free_work_fn(struct work_struct *work)
{
	struct hns_roce_mr_free_work *mr_work;
	struct ib_wc wc[HNS_ROCE_V1_RESV_QP];
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_cq *mr_free_cq;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_dev *hr_dev;
	struct hns_roce_mr *hr_mr;
	struct hns_roce_qp *hr_qp;
	struct device *dev;
	unsigned long end =
		msecs_to_jiffies(HNS_ROCE_V1_FREE_MR_TIMEOUT_MSECS) + jiffies;
	int i;
	int ret;
	int ne = 0;

	mr_work = container_of(work, struct hns_roce_mr_free_work, work);
	hr_mr = (struct hns_roce_mr *)mr_work->mr;
	hr_dev = to_hr_dev(mr_work->ib_dev);
	dev = &hr_dev->pdev->dev;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;
	mr_free_cq = free_mr->mr_free_cq;

	for (i = 0; i < HNS_ROCE_V1_RESV_QP; i++) {
		hr_qp = free_mr->mr_free_qp[i];
		if (!hr_qp)
			continue;
		ne++;

		ret = hns_roce_v1_send_lp_wqe(hr_qp);
		if (ret) {
			dev_err(dev,
			     "Send wqe (qp:0x%lx) for mr free failed(%d)!\n",
			     hr_qp->qpn, ret);
			goto free_work;
		}
	}

	if (!ne) {
		dev_err(dev, "Reserved loop qp is absent!\n");
		goto free_work;
	}

	do {
		ret = hns_roce_v1_poll_cq(&mr_free_cq->ib_cq, ne, wc);
		if (ret < 0) {
			dev_err(dev,
			   "(qp:0x%lx) starts, Poll cqe failed(%d) for mr 0x%x free! Remain %d cqe\n",
			   hr_qp->qpn, ret, hr_mr->key, ne);
			goto free_work;
		}
		ne -= ret;
		usleep_range(HNS_ROCE_V1_FREE_MR_WAIT_VALUE * 1000,
			     (1 + HNS_ROCE_V1_FREE_MR_WAIT_VALUE) * 1000);
	} while (ne && time_before_eq(jiffies, end));

	if (ne != 0)
		dev_err(dev,
			"Poll cqe for mr 0x%x free timeout! Remain %d cqe\n",
			hr_mr->key, ne);

free_work:
	if (mr_work->comp_flag)
		complete(mr_work->comp);
	kfree(mr_work);
}

static int hns_roce_v1_dereg_mr(struct hns_roce_dev *hr_dev,
				struct hns_roce_mr *mr)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_mr_free_work *mr_work;
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_v1_priv *priv;
	struct completion comp;
	unsigned long end =
		msecs_to_jiffies(HNS_ROCE_V1_FREE_MR_TIMEOUT_MSECS) + jiffies;
	unsigned long start = jiffies;
	int npages;
	int ret = 0;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	if (mr->enabled) {
		if (hns_roce_hw2sw_mpt(hr_dev, NULL, key_to_hw_index(mr->key)
				       & (hr_dev->caps.num_mtpts - 1)))
			dev_warn(dev, "HW2SW_MPT failed!\n");
	}

	mr_work = kzalloc(sizeof(*mr_work), GFP_KERNEL);
	if (!mr_work) {
		ret = -ENOMEM;
		goto free_mr;
	}

	INIT_WORK(&(mr_work->work), hns_roce_v1_mr_free_work_fn);

	mr_work->ib_dev = &(hr_dev->ib_dev);
	mr_work->comp = &comp;
	mr_work->comp_flag = 1;
	mr_work->mr = (void *)mr;
	init_completion(mr_work->comp);

	queue_work(free_mr->free_mr_wq, &(mr_work->work));

	while (time_before_eq(jiffies, end)) {
		if (try_wait_for_completion(&comp))
			goto free_mr;
		msleep(HNS_ROCE_V1_FREE_MR_WAIT_VALUE);
	}

	mr_work->comp_flag = 0;
	if (try_wait_for_completion(&comp))
		goto free_mr;

	dev_warn(dev, "Free mr work 0x%x over 50s and failed!\n", mr->key);
	ret = -ETIMEDOUT;

free_mr:
	dev_dbg(dev, "Free mr 0x%x use 0x%x us.\n",
		mr->key, jiffies_to_usecs(jiffies) - jiffies_to_usecs(start));

	if (mr->size != ~0ULL) {
		npages = ib_umem_page_count(mr->umem);
		dma_free_coherent(dev, npages * 8, mr->pbl_buf,
				  mr->pbl_dma_addr);
	}

	hns_roce_bitmap_free(&hr_dev->mr_table.mtpt_bitmap,
			     key_to_hw_index(mr->key), 0);

	if (mr->umem)
		ib_umem_release(mr->umem);

	kfree(mr);

	return ret;
}

static void hns_roce_db_free(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_db_table *db;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
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

static int hns_roce_bt_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	int ret;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;

	priv->bt_table.qpc_buf.buf = dma_alloc_coherent(dev,
		HNS_ROCE_BT_RSV_BUF_SIZE, &priv->bt_table.qpc_buf.map,
		GFP_KERNEL);
	if (!priv->bt_table.qpc_buf.buf)
		return -ENOMEM;

	priv->bt_table.mtpt_buf.buf = dma_alloc_coherent(dev,
		HNS_ROCE_BT_RSV_BUF_SIZE, &priv->bt_table.mtpt_buf.map,
		GFP_KERNEL);
	if (!priv->bt_table.mtpt_buf.buf) {
		ret = -ENOMEM;
		goto err_failed_alloc_mtpt_buf;
	}

	priv->bt_table.cqc_buf.buf = dma_alloc_coherent(dev,
		HNS_ROCE_BT_RSV_BUF_SIZE, &priv->bt_table.cqc_buf.map,
		GFP_KERNEL);
	if (!priv->bt_table.cqc_buf.buf) {
		ret = -ENOMEM;
		goto err_failed_alloc_cqc_buf;
	}

	return 0;

err_failed_alloc_cqc_buf:
	dma_free_coherent(dev, HNS_ROCE_BT_RSV_BUF_SIZE,
		priv->bt_table.mtpt_buf.buf, priv->bt_table.mtpt_buf.map);

err_failed_alloc_mtpt_buf:
	dma_free_coherent(dev, HNS_ROCE_BT_RSV_BUF_SIZE,
		priv->bt_table.qpc_buf.buf, priv->bt_table.qpc_buf.map);

	return ret;
}

static void hns_roce_bt_free(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;

	dma_free_coherent(dev, HNS_ROCE_BT_RSV_BUF_SIZE,
		priv->bt_table.cqc_buf.buf, priv->bt_table.cqc_buf.map);

	dma_free_coherent(dev, HNS_ROCE_BT_RSV_BUF_SIZE,
		priv->bt_table.mtpt_buf.buf, priv->bt_table.mtpt_buf.map);

	dma_free_coherent(dev, HNS_ROCE_BT_RSV_BUF_SIZE,
		priv->bt_table.qpc_buf.buf, priv->bt_table.qpc_buf.map);
}

static int hns_roce_tptr_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_buf_list *tptr_buf;
	struct hns_roce_v1_priv *priv;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	tptr_buf = &priv->tptr_table.tptr_buf;

	/*
	 * This buffer will be used for CQ's tptr(tail pointer), also
	 * named ci(customer index). Every CQ will use 2 bytes to save
	 * cqe ci in hip06. Hardware will read this area to get new ci
	 * when the queue is almost full.
	 */
	tptr_buf->buf = dma_alloc_coherent(dev, HNS_ROCE_V1_TPTR_BUF_SIZE,
					   &tptr_buf->map, GFP_KERNEL);
	if (!tptr_buf->buf)
		return -ENOMEM;

	hr_dev->tptr_dma_addr = tptr_buf->map;
	hr_dev->tptr_size = HNS_ROCE_V1_TPTR_BUF_SIZE;

	return 0;
}

static void hns_roce_tptr_free(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_buf_list *tptr_buf;
	struct hns_roce_v1_priv *priv;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	tptr_buf = &priv->tptr_table.tptr_buf;

	dma_free_coherent(dev, HNS_ROCE_V1_TPTR_BUF_SIZE,
			  tptr_buf->buf, tptr_buf->map);
}

static int hns_roce_free_mr_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_v1_priv *priv;
	int ret = 0;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	free_mr->free_mr_wq = create_singlethread_workqueue("hns_roce_free_mr");
	if (!free_mr->free_mr_wq) {
		dev_err(dev, "Create free mr workqueue failed!\n");
		return -ENOMEM;
	}

	ret = hns_roce_v1_rsv_lp_qp(hr_dev);
	if (ret) {
		dev_err(dev, "Reserved loop qp failed(%d)!\n", ret);
		flush_workqueue(free_mr->free_mr_wq);
		destroy_workqueue(free_mr->free_mr_wq);
	}

	return ret;
}

static void hns_roce_free_mr_free(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_free_mr *free_mr;
	struct hns_roce_v1_priv *priv;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	free_mr = &priv->free_mr;

	flush_workqueue(free_mr->free_mr_wq);
	destroy_workqueue(free_mr->free_mr_wq);

	hns_roce_v1_release_lp_qp(hr_dev);
}

/**
 * hns_roce_v1_reset - reset RoCE
 * @hr_dev: RoCE device struct pointer
 * @enable: true -- drop reset, false -- reset
 * return 0 - success , negative --fail
 */
static int hns_roce_v1_reset(struct hns_roce_dev *hr_dev, bool dereset)
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

static int hns_roce_des_qp_init(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_des_qp *des_qp;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	des_qp = &priv->des_qp;

	des_qp->requeue_flag = 1;
	des_qp->qp_wq = create_singlethread_workqueue("hns_roce_destroy_qp");
	if (!des_qp->qp_wq) {
		dev_err(dev, "Create destroy qp workqueue failed!\n");
		return -ENOMEM;
	}

	return 0;
}

static void hns_roce_des_qp_free(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v1_priv *priv;
	struct hns_roce_des_qp *des_qp;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	des_qp = &priv->des_qp;

	des_qp->requeue_flag = 0;
	flush_workqueue(des_qp->qp_wq);
	destroy_workqueue(des_qp->qp_wq);
}

static int hns_roce_v1_profile(struct hns_roce_dev *hr_dev)
{
	int i = 0;
	struct hns_roce_caps *caps = &hr_dev->caps;

	hr_dev->vendor_id = le32_to_cpu(roce_read(hr_dev, ROCEE_VENDOR_ID_REG));
	hr_dev->vendor_part_id = le32_to_cpu(roce_read(hr_dev,
					     ROCEE_VENDOR_PART_ID_REG));
	hr_dev->sys_image_guid = le32_to_cpu(roce_read(hr_dev,
					     ROCEE_SYS_IMAGE_GUID_L_REG)) |
				((u64)le32_to_cpu(roce_read(hr_dev,
					    ROCEE_SYS_IMAGE_GUID_H_REG)) << 32);
	hr_dev->hw_rev		= HNS_ROCE_HW_VER1;

	caps->num_qps		= HNS_ROCE_V1_MAX_QP_NUM;
	caps->max_wqes		= HNS_ROCE_V1_MAX_WQE_NUM;
	caps->min_wqes		= HNS_ROCE_MIN_WQE_NUM;
	caps->num_cqs		= HNS_ROCE_V1_MAX_CQ_NUM;
	caps->min_cqes		= HNS_ROCE_MIN_CQE_NUM;
	caps->max_cqes		= HNS_ROCE_V1_MAX_CQE_NUM;
	caps->max_sq_sg		= HNS_ROCE_V1_SG_NUM;
	caps->max_rq_sg		= HNS_ROCE_V1_SG_NUM;
	caps->max_sq_inline	= HNS_ROCE_V1_INLINE_SIZE;
	caps->num_uars		= HNS_ROCE_V1_UAR_NUM;
	caps->phy_num_uars	= HNS_ROCE_V1_PHY_UAR_NUM;
	caps->num_aeq_vectors	= HNS_ROCE_V1_AEQE_VEC_NUM;
	caps->num_comp_vectors	= HNS_ROCE_V1_COMP_VEC_NUM;
	caps->num_other_vectors	= HNS_ROCE_V1_ABNORMAL_VEC_NUM;
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
	caps->reserved_lkey	= 0;
	caps->reserved_pds	= 0;
	caps->reserved_mrws	= 1;
	caps->reserved_uars	= 0;
	caps->reserved_cqs	= 0;
	caps->chunk_sz		= HNS_ROCE_V1_TABLE_CHUNK_SIZE;

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

	caps->ceqe_depth = HNS_ROCE_V1_COMP_EQE_NUM;
	caps->aeqe_depth = HNS_ROCE_V1_ASYNC_EQE_NUM;
	caps->local_ca_ack_delay = le32_to_cpu(roce_read(hr_dev,
							 ROCEE_ACK_DELAY_REG));
	caps->max_mtu = IB_MTU_2048;

	return 0;
}

static int hns_roce_v1_init(struct hns_roce_dev *hr_dev)
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

	ret = hns_roce_bt_init(hr_dev);
	if (ret) {
		dev_err(dev, "bt init failed!\n");
		goto error_failed_bt_init;
	}

	ret = hns_roce_tptr_init(hr_dev);
	if (ret) {
		dev_err(dev, "tptr init failed!\n");
		goto error_failed_tptr_init;
	}

	ret = hns_roce_des_qp_init(hr_dev);
	if (ret) {
		dev_err(dev, "des qp init failed!\n");
		goto error_failed_des_qp_init;
	}

	ret = hns_roce_free_mr_init(hr_dev);
	if (ret) {
		dev_err(dev, "free mr init failed!\n");
		goto error_failed_free_mr_init;
	}

	hns_roce_port_enable(hr_dev, HNS_ROCE_PORT_UP);

	return 0;

error_failed_free_mr_init:
	hns_roce_des_qp_free(hr_dev);

error_failed_des_qp_init:
	hns_roce_tptr_free(hr_dev);

error_failed_tptr_init:
	hns_roce_bt_free(hr_dev);

error_failed_bt_init:
	hns_roce_raq_free(hr_dev);

error_failed_raq_init:
	hns_roce_db_free(hr_dev);
	return ret;
}

static void hns_roce_v1_exit(struct hns_roce_dev *hr_dev)
{
	hns_roce_port_enable(hr_dev, HNS_ROCE_PORT_DOWN);
	hns_roce_free_mr_free(hr_dev);
	hns_roce_des_qp_free(hr_dev);
	hns_roce_tptr_free(hr_dev);
	hns_roce_bt_free(hr_dev);
	hns_roce_raq_free(hr_dev);
	hns_roce_db_free(hr_dev);
}

static int hns_roce_v1_cmd_pending(struct hns_roce_dev *hr_dev)
{
	u32 status = readl(hr_dev->reg_base + ROCEE_MB6_REG);

	return (!!(status & (1 << HCR_GO_BIT)));
}

static int hns_roce_v1_post_mbox(struct hns_roce_dev *hr_dev, u64 in_param,
				 u64 out_param, u32 in_modifier, u8 op_modifier,
				 u16 op, u16 token, int event)
{
	u32 __iomem *hcr = (u32 __iomem *)(hr_dev->reg_base + ROCEE_MB1_REG);
	unsigned long end;
	u32 val = 0;

	end = msecs_to_jiffies(GO_BIT_TIMEOUT_MSECS) + jiffies;
	while (hns_roce_v1_cmd_pending(hr_dev)) {
		if (time_after(jiffies, end)) {
			dev_err(hr_dev->dev, "jiffies=%d end=%d\n",
				(int)jiffies, (int)end);
			return -EAGAIN;
		}
		cond_resched();
	}

	roce_set_field(val, ROCEE_MB6_ROCEE_MB_CMD_M, ROCEE_MB6_ROCEE_MB_CMD_S,
		       op);
	roce_set_field(val, ROCEE_MB6_ROCEE_MB_CMD_MDF_M,
		       ROCEE_MB6_ROCEE_MB_CMD_MDF_S, op_modifier);
	roce_set_bit(val, ROCEE_MB6_ROCEE_MB_EVENT_S, event);
	roce_set_bit(val, ROCEE_MB6_ROCEE_MB_HW_RUN_S, 1);
	roce_set_field(val, ROCEE_MB6_ROCEE_MB_TOKEN_M,
		       ROCEE_MB6_ROCEE_MB_TOKEN_S, token);

	__raw_writeq(cpu_to_le64(in_param), hcr + 0);
	__raw_writeq(cpu_to_le64(out_param), hcr + 2);
	__raw_writel(cpu_to_le32(in_modifier), hcr + 4);
	/* Memory barrier */
	wmb();

	__raw_writel(cpu_to_le32(val), hcr + 5);

	mmiowb();

	return 0;
}

static int hns_roce_v1_chk_mbox(struct hns_roce_dev *hr_dev,
				unsigned long timeout)
{
	u8 __iomem *hcr = hr_dev->reg_base + ROCEE_MB1_REG;
	unsigned long end = 0;
	u32 status = 0;

	end = msecs_to_jiffies(timeout) + jiffies;
	while (hns_roce_v1_cmd_pending(hr_dev) && time_before(jiffies, end))
		cond_resched();

	if (hns_roce_v1_cmd_pending(hr_dev)) {
		dev_err(hr_dev->dev, "[cmd_poll]hw run cmd TIMEDOUT!\n");
		return -ETIMEDOUT;
	}

	status = le32_to_cpu((__force __be32)
			      __raw_readl(hcr + HCR_STATUS_OFFSET));
	if ((status & STATUS_MASK) != 0x1) {
		dev_err(hr_dev->dev, "mailbox status 0x%x!\n", status);
		return -EBUSY;
	}

	return 0;
}

static int hns_roce_v1_set_gid(struct hns_roce_dev *hr_dev, u8 port,
			       int gid_index, union ib_gid *gid,
			       const struct ib_gid_attr *attr)
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

	return 0;
}

static int hns_roce_v1_set_mac(struct hns_roce_dev *hr_dev, u8 phy_port,
			       u8 *addr)
{
	u32 reg_smac_l;
	u16 reg_smac_h;
	u16 *p_h;
	u32 *p;
	u32 val;

	/*
	 * When mac changed, loopback may fail
	 * because of smac not equal to dmac.
	 * We Need to release and create reserved qp again.
	 */
	if (hr_dev->hw->dereg_mr) {
		int ret;

		ret = hns_roce_v1_recreate_lp_qp(hr_dev);
		if (ret && ret != -ETIMEDOUT)
			return ret;
	}

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

	return 0;
}

static void hns_roce_v1_set_mtu(struct hns_roce_dev *hr_dev, u8 phy_port,
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

static int hns_roce_v1_write_mtpt(void *mb_buf, struct hns_roce_mr *mr,
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

	/* DMA memory register */
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

static void hns_roce_v1_cq_set_ci(struct hns_roce_cq *hr_cq, u32 cons_index)
{
	u32 doorbell[2];

	doorbell[0] = cons_index & ((hr_cq->cq_depth << 1) - 1);
	doorbell[1] = 0;
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

		hns_roce_v1_cq_set_ci(hr_cq, hr_cq->cons_index);
	}
}

static void hns_roce_v1_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				 struct hns_roce_srq *srq)
{
	spin_lock_irq(&hr_cq->lock);
	__hns_roce_v1_cq_clean(hr_cq, qpn, srq);
	spin_unlock_irq(&hr_cq->lock);
}

static void hns_roce_v1_write_cqc(struct hns_roce_dev *hr_dev,
				  struct hns_roce_cq *hr_cq, void *mb_buf,
				  u64 *mtts, dma_addr_t dma_handle, int nent,
				  u32 vector)
{
	struct hns_roce_cq_context *cq_context = NULL;
	struct hns_roce_buf_list *tptr_buf;
	struct hns_roce_v1_priv *priv;
	dma_addr_t tptr_dma_addr;
	int offset;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	tptr_buf = &priv->tptr_table.tptr_buf;

	cq_context = mb_buf;
	memset(cq_context, 0, sizeof(*cq_context));

	/* Get the tptr for this CQ. */
	offset = hr_cq->cqn * HNS_ROCE_V1_TPTR_ENTRY_SIZE;
	tptr_dma_addr = tptr_buf->map + offset;
	hr_cq->tptr_addr = (u16 *)(tptr_buf->buf + offset);

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
		       tptr_dma_addr >> 44);
	cq_context->cqc_byte_20 = cpu_to_le32(cq_context->cqc_byte_20);

	cq_context->cqe_tptr_addr_l = (u32)(tptr_dma_addr >> 12);

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
	/* The initial value of cq's ci is 0 */
	roce_set_field(cq_context->cqc_byte_32,
		       CQ_CONTEXT_CQC_BYTE_32_CQ_CONS_IDX_M,
		       CQ_CONTEXT_CQC_BYTE_32_CQ_CONS_IDX_S, 0);
	cq_context->cqc_byte_32 = cpu_to_le32(cq_context->cqc_byte_32);
}

static int hns_roce_v1_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	return -EOPNOTSUPP;
}

static int hns_roce_v1_req_notify_cq(struct ib_cq *ibcq,
				     enum ib_cq_notify_flags flags)
{
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	u32 notification_flag;
	u32 doorbell[2];

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

	return 0;
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
						CQE_BYTE_4_WQE_INDEX_S)&
						((*cur_qp)->sq.wqe_cnt-1));
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
		*hr_cq->tptr_addr = hr_cq->cons_index &
			((hr_cq->cq_depth << 1) - 1);

		/* Memroy barrier */
		wmb();
		hns_roce_v1_cq_set_ci(hr_cq, hr_cq->cons_index);
	}

	spin_unlock_irqrestore(&hr_cq->lock, flags);

	if (ret == 0 || ret == -EAGAIN)
		return npolled;
	else
		return ret;
}

static int hns_roce_v1_clear_hem(struct hns_roce_dev *hr_dev,
				 struct hns_roce_hem_table *table, int obj,
				 int step_idx)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_v1_priv *priv;
	unsigned long end = 0, flags = 0;
	uint32_t bt_cmd_val[2] = {0};
	void __iomem *bt_cmd;
	u64 bt_ba = 0;

	priv = (struct hns_roce_v1_priv *)hr_dev->priv;

	switch (table->type) {
	case HEM_TYPE_QPC:
		roce_set_field(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, HEM_TYPE_QPC);
		bt_ba = priv->bt_table.qpc_buf.map >> 12;
		break;
	case HEM_TYPE_MTPT:
		roce_set_field(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, HEM_TYPE_MTPT);
		bt_ba = priv->bt_table.mtpt_buf.map >> 12;
		break;
	case HEM_TYPE_CQC:
		roce_set_field(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_M,
			ROCEE_BT_CMD_H_ROCEE_BT_CMD_MDF_S, HEM_TYPE_CQC);
		bt_ba = priv->bt_table.cqc_buf.map >> 12;
		break;
	case HEM_TYPE_SRQC:
		dev_dbg(dev, "HEM_TYPE_SRQC not support.\n");
		return -EINVAL;
	default:
		return 0;
	}
	roce_set_field(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_M,
		ROCEE_BT_CMD_H_ROCEE_BT_CMD_IN_MDF_S, obj);
	roce_set_bit(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_S, 0);
	roce_set_bit(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_HW_SYNS_S, 1);

	spin_lock_irqsave(&hr_dev->bt_cmd_lock, flags);

	bt_cmd = hr_dev->reg_base + ROCEE_BT_CMD_H_REG;

	end = msecs_to_jiffies(HW_SYNC_TIMEOUT_MSECS) + jiffies;
	while (1) {
		if (readl(bt_cmd) >> BT_CMD_SYNC_SHIFT) {
			if (!(time_before(jiffies, end))) {
				dev_err(dev, "Write bt_cmd err,hw_sync is not zero.\n");
				spin_unlock_irqrestore(&hr_dev->bt_cmd_lock,
					flags);
				return -EBUSY;
			}
		} else {
			break;
		}
		msleep(HW_SYNC_SLEEP_TIME_INTERVAL);
	}

	bt_cmd_val[0] = (uint32_t)bt_ba;
	roce_set_field(bt_cmd_val[1], ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_M,
		ROCEE_BT_CMD_H_ROCEE_BT_CMD_BA_H_S, bt_ba >> 32);
	hns_roce_write64_k(bt_cmd_val, hr_dev->reg_base + ROCEE_BT_CMD_L_REG);

	spin_unlock_irqrestore(&hr_dev->bt_cmd_lock, flags);

	return 0;
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
					 HNS_ROCE_CMD_TIMEOUT_MSECS);

	if (op[cur_state][new_state] == HNS_ROCE_CMD_2ERR_QP)
		return hns_roce_cmd_mbox(hr_dev, 0, 0, hr_qp->qpn, 2,
					 HNS_ROCE_CMD_2ERR_QP,
					 HNS_ROCE_CMD_TIMEOUT_MSECS);

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, context, sizeof(*context));

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, hr_qp->qpn, 0,
				op[cur_state][new_state],
				HNS_ROCE_CMD_TIMEOUT_MSECS);

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
	u32 __iomem *addr;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	/* Search QP buf's MTTs */
	mtts = hns_roce_table_find(hr_dev, &hr_dev->mr_table.mtt_table,
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
		addr = (u32 __iomem *)(hr_dev->reg_base +
				       ROCEE_QP1C_CFG0_0_REG +
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
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	dma_addr_t dma_handle_2 = 0;
	dma_addr_t dma_handle = 0;
	uint32_t doorbell[2] = {0};
	int rq_pa_start = 0;
	u64 *mtts_2 = NULL;
	int ret = -EINVAL;
	u64 *mtts = NULL;
	int port;
	u8 port_num;
	u8 *dmac;
	u8 *smac;

	context = kzalloc(sizeof(*context), GFP_KERNEL);
	if (!context)
		return -ENOMEM;

	/* Search qp buf's mtts */
	mtts = hns_roce_table_find(hr_dev, &hr_dev->mr_table.mtt_table,
				   hr_qp->mtt.first_seg, &dma_handle);
	if (mtts == NULL) {
		dev_err(dev, "qp buf pa find failed\n");
		goto out;
	}

	/* Search IRRL's mtts */
	mtts_2 = hns_roce_table_find(hr_dev, &hr_dev->qp_table.irrl_table,
				     hr_qp->qpn, &dma_handle_2);
	if (mtts_2 == NULL) {
		dev_err(dev, "qp irrl_table find failed\n");
		goto out;
	}

	/*
	 * Reset to init
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

		dmac = (u8 *)attr->ah_attr.roce.dmac;

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

		port = (attr_mask & IB_QP_PORT) ? (attr->port_num - 1) :
			hr_qp->port;
		smac = (u8 *)hr_dev->dev_addr[port];
		/* when dmac equals smac or loop_idc is 1, it should loopback */
		if (ether_addr_equal_unaligned(dmac, smac) ||
		    hr_dev->loop_idc == 0x1)
			roce_set_bit(context->qpc_bytes_32,
			      QP_CONTEXT_QPC_BYTE_32_LOOPBACK_INDICATOR_S, 1);

		roce_set_bit(context->qpc_bytes_32,
			     QP_CONTEXT_QPC_BYTE_32_GLOBAL_HEADER_S,
			     rdma_ah_get_ah_flags(&attr->ah_attr));
		roce_set_field(context->qpc_bytes_32,
			       QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_M,
			       QP_CONTEXT_QPC_BYTES_32_RESPONDER_RESOURCES_S,
			       ilog2((unsigned int)attr->max_dest_rd_atomic));

		if (attr_mask & IB_QP_DEST_QPN)
			roce_set_field(context->qpc_bytes_36,
				       QP_CONTEXT_QPC_BYTES_36_DEST_QP_M,
				       QP_CONTEXT_QPC_BYTES_36_DEST_QP_S,
				       attr->dest_qp_num);

		/* Configure GID index */
		port_num = rdma_ah_get_port_num(&attr->ah_attr);
		roce_set_field(context->qpc_bytes_36,
			       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_M,
			       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_S,
				hns_get_gid_index(hr_dev,
						  port_num - 1,
						  grh->sgid_index));

		memcpy(&(context->dmac_l), dmac, 4);

		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_DMAC_H_M,
			       QP_CONTEXT_QPC_BYTES_44_DMAC_H_S,
			       *((u16 *)(&dmac[4])));
		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_MAXIMUM_STATIC_RATE_M,
			       QP_CONTEXT_QPC_BYTES_44_MAXIMUM_STATIC_RATE_S,
			       rdma_ah_get_static_rate(&attr->ah_attr));
		roce_set_field(context->qpc_bytes_44,
			       QP_CONTEXT_QPC_BYTES_44_HOPLMT_M,
			       QP_CONTEXT_QPC_BYTES_44_HOPLMT_S,
			       grh->hop_limit);

		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_M,
			       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_S,
			       grh->flow_label);
		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_TCLASS_M,
			       QP_CONTEXT_QPC_BYTES_48_TCLASS_S,
			       grh->traffic_class);
		roce_set_field(context->qpc_bytes_48,
			       QP_CONTEXT_QPC_BYTES_48_MTU_M,
			       QP_CONTEXT_QPC_BYTES_48_MTU_S, attr->path_mtu);

		memcpy(context->dgid, grh->dgid.raw,
		       sizeof(grh->dgid.raw));

		dev_dbg(dev, "dmac:%x :%lx\n", context->dmac_l,
			roce_get_field(context->qpc_bytes_44,
				       QP_CONTEXT_QPC_BYTES_44_DMAC_H_M,
				       QP_CONTEXT_QPC_BYTES_44_DMAC_H_S));

		roce_set_field(context->qpc_bytes_68,
			       QP_CONTEXT_QPC_BYTES_68_RQ_HEAD_M,
			       QP_CONTEXT_QPC_BYTES_68_RQ_HEAD_S,
			       hr_qp->rq.head);
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
			       QP_CONTEXT_QPC_BYTES_156_SL_S,
			       rdma_ah_get_sl(&attr->ah_attr));
		hr_qp->sl = rdma_ah_get_sl(&attr->ah_attr);
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

		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_CHECK_FLAG_M,
			       QP_CONTEXT_QPC_BYTES_148_CHECK_FLAG_S, 0);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_M,
			       QP_CONTEXT_QPC_BYTES_148_RETRY_COUNT_S,
			       attr->retry_cnt);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_RNR_RETRY_COUNT_M,
			       QP_CONTEXT_QPC_BYTES_148_RNR_RETRY_COUNT_S,
			       attr->rnr_retry);
		roce_set_field(context->qpc_bytes_148,
			       QP_CONTEXT_QPC_BYTES_148_LSN_M,
			       QP_CONTEXT_QPC_BYTES_148_LSN_S, 0x100);

		context->rnr_retry = 0;

		roce_set_field(context->qpc_bytes_156,
			       QP_CONTEXT_QPC_BYTES_156_RETRY_COUNT_INIT_M,
			       QP_CONTEXT_QPC_BYTES_156_RETRY_COUNT_INIT_S,
			       attr->retry_cnt);
		if (attr->timeout < 0x12) {
			dev_info(dev, "ack timeout value(0x%x) must bigger than 0x12.\n",
				 attr->timeout);
			roce_set_field(context->qpc_bytes_156,
				       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_M,
				       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_S,
				       0x12);
		} else {
			roce_set_field(context->qpc_bytes_156,
				       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_M,
				       QP_CONTEXT_QPC_BYTES_156_ACK_TIMEOUT_S,
				       attr->timeout);
		}
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
			       QP_CONTEXT_QPC_BYTES_156_SL_S,
			       rdma_ah_get_sl(&attr->ah_attr));
		hr_qp->sl = rdma_ah_get_sl(&attr->ah_attr);
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
	} else if (!((cur_state == IB_QPS_INIT && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_INIT && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_RTR && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_RTR && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_RTS && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_RTS && new_state == IB_QPS_ERR) ||
		   (cur_state == IB_QPS_ERR && new_state == IB_QPS_RESET) ||
		   (cur_state == IB_QPS_ERR && new_state == IB_QPS_ERR))) {
		dev_err(dev, "not support this status migration\n");
		goto out;
	}

	/* Every status migrate must change state */
	roce_set_field(context->qpc_bytes_144,
		       QP_CONTEXT_QPC_BYTES_144_QP_STATE_M,
		       QP_CONTEXT_QPC_BYTES_144_QP_STATE_S, new_state);

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
				     hr_dev->odb_offset +
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

static int hns_roce_v1_modify_qp(struct ib_qp *ibqp,
				 const struct ib_qp_attr *attr, int attr_mask,
				 enum ib_qp_state cur_state,
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
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (!ret)
		memcpy(hr_context, mailbox->buf, sizeof(*hr_context));
	else
		dev_err(&hr_dev->pdev->dev, "QUERY QP cmd process error\n");

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

static int hns_roce_v1_q_sqp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
			     int qp_attr_mask,
			     struct ib_qp_init_attr *qp_init_attr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_sqp_context context;
	u32 addr;

	mutex_lock(&hr_qp->mutex);

	if (hr_qp->state == IB_QPS_RESET) {
		qp_attr->qp_state = IB_QPS_RESET;
		goto done;
	}

	addr = ROCEE_QP1C_CFG0_0_REG +
		hr_qp->port * sizeof(struct hns_roce_sqp_context);
	context.qp1c_bytes_4 = roce_read(hr_dev, addr);
	context.sq_rq_bt_l = roce_read(hr_dev, addr + 1);
	context.qp1c_bytes_12 = roce_read(hr_dev, addr + 2);
	context.qp1c_bytes_16 = roce_read(hr_dev, addr + 3);
	context.qp1c_bytes_20 = roce_read(hr_dev, addr + 4);
	context.cur_rq_wqe_ba_l = roce_read(hr_dev, addr + 5);
	context.qp1c_bytes_28 = roce_read(hr_dev, addr + 6);
	context.qp1c_bytes_32 = roce_read(hr_dev, addr + 7);
	context.cur_sq_wqe_ba_l = roce_read(hr_dev, addr + 8);
	context.qp1c_bytes_40 = roce_read(hr_dev, addr + 9);

	hr_qp->state = roce_get_field(context.qp1c_bytes_4,
				      QP1C_BYTES_4_QP_STATE_M,
				      QP1C_BYTES_4_QP_STATE_S);
	qp_attr->qp_state	= hr_qp->state;
	qp_attr->path_mtu	= IB_MTU_256;
	qp_attr->path_mig_state	= IB_MIG_ARMED;
	qp_attr->qkey		= QKEY_VAL;
	qp_attr->ah_attr.type   = RDMA_AH_ATTR_TYPE_ROCE;
	qp_attr->rq_psn		= 0;
	qp_attr->sq_psn		= 0;
	qp_attr->dest_qp_num	= 1;
	qp_attr->qp_access_flags = 6;

	qp_attr->pkey_index = roce_get_field(context.qp1c_bytes_20,
					     QP1C_BYTES_20_PKEY_IDX_M,
					     QP1C_BYTES_20_PKEY_IDX_S);
	qp_attr->port_num = hr_qp->port + 1;
	qp_attr->sq_draining = 0;
	qp_attr->max_rd_atomic = 0;
	qp_attr->max_dest_rd_atomic = 0;
	qp_attr->min_rnr_timer = 0;
	qp_attr->timeout = 0;
	qp_attr->retry_cnt = 0;
	qp_attr->rnr_retry = 0;
	qp_attr->alt_timeout = 0;

done:
	qp_attr->cur_qp_state = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr = hr_qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge = hr_qp->rq.max_gs;
	qp_attr->cap.max_send_wr = hr_qp->sq.wqe_cnt;
	qp_attr->cap.max_send_sge = hr_qp->sq.max_gs;
	qp_attr->cap.max_inline_data = 0;
	qp_init_attr->cap = qp_attr->cap;
	qp_init_attr->create_flags = 0;

	mutex_unlock(&hr_qp->mutex);

	return 0;
}

static int hns_roce_v1_q_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
			    int qp_attr_mask,
			    struct ib_qp_init_attr *qp_init_attr)
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
	qp_attr->ah_attr.type   = RDMA_AH_ATTR_TYPE_ROCE;
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
		struct ib_global_route *grh =
			rdma_ah_retrieve_grh(&qp_attr->ah_attr);

		rdma_ah_set_sl(&qp_attr->ah_attr,
			       roce_get_field(context->qpc_bytes_156,
					      QP_CONTEXT_QPC_BYTES_156_SL_M,
					      QP_CONTEXT_QPC_BYTES_156_SL_S));
		rdma_ah_set_ah_flags(&qp_attr->ah_attr, IB_AH_GRH);
		grh->flow_label =
			roce_get_field(context->qpc_bytes_48,
				       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_M,
				       QP_CONTEXT_QPC_BYTES_48_FLOWLABEL_S);
		grh->sgid_index =
			roce_get_field(context->qpc_bytes_36,
				       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_M,
				       QP_CONTEXT_QPC_BYTES_36_SGID_INDEX_S);
		grh->hop_limit =
			roce_get_field(context->qpc_bytes_44,
				       QP_CONTEXT_QPC_BYTES_44_HOPLMT_M,
				       QP_CONTEXT_QPC_BYTES_44_HOPLMT_S);
		grh->traffic_class =
			roce_get_field(context->qpc_bytes_48,
				       QP_CONTEXT_QPC_BYTES_48_TCLASS_M,
				       QP_CONTEXT_QPC_BYTES_48_TCLASS_S);

		memcpy(grh->dgid.raw, context->dgid,
		       sizeof(grh->dgid.raw));
	}

	qp_attr->pkey_index = roce_get_field(context->qpc_bytes_12,
			      QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_M,
			      QP_CONTEXT_QPC_BYTES_12_P_KEY_INDEX_S);
	qp_attr->port_num = hr_qp->port + 1;
	qp_attr->sq_draining = 0;
	qp_attr->max_rd_atomic = 1 << roce_get_field(context->qpc_bytes_156,
				 QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_M,
				 QP_CONTEXT_QPC_BYTES_156_INITIATOR_DEPTH_S);
	qp_attr->max_dest_rd_atomic = 1 << roce_get_field(context->qpc_bytes_32,
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

static int hns_roce_v1_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
				int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr)
{
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);

	return hr_qp->doorbell_qpn <= 1 ?
		hns_roce_v1_q_sqp(ibqp, qp_attr, qp_attr_mask, qp_init_attr) :
		hns_roce_v1_q_qp(ibqp, qp_attr, qp_attr_mask, qp_init_attr);
}

static void hns_roce_check_sdb_status(struct hns_roce_dev *hr_dev,
				      u32 *old_send, u32 *old_retry,
				      u32 *tsp_st, u32 *success_flags)
{
	u32 sdb_retry_cnt;
	u32 sdb_send_ptr;
	u32 cur_cnt, old_cnt;
	u32 send_ptr;

	sdb_send_ptr = roce_read(hr_dev, ROCEE_SDB_SEND_PTR_REG);
	sdb_retry_cnt =	roce_read(hr_dev, ROCEE_SDB_RETRY_CNT_REG);
	cur_cnt = roce_get_field(sdb_send_ptr,
				 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
				 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S) +
		  roce_get_field(sdb_retry_cnt,
				 ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_M,
				 ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_S);
	if (!roce_get_bit(*tsp_st, ROCEE_CNT_CLR_CE_CNT_CLR_CE_S)) {
		old_cnt = roce_get_field(*old_send,
					 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
					 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S) +
			  roce_get_field(*old_retry,
					 ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_M,
					 ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_S);
		if (cur_cnt - old_cnt > SDB_ST_CMP_VAL)
			*success_flags = 1;
	} else {
		old_cnt = roce_get_field(*old_send,
					 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
					 ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S);
		if (cur_cnt - old_cnt > SDB_ST_CMP_VAL) {
			*success_flags = 1;
		} else {
			send_ptr = roce_get_field(*old_send,
					    ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
					    ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S) +
				   roce_get_field(sdb_retry_cnt,
					    ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_M,
					    ROCEE_SDB_RETRY_CNT_SDB_RETRY_CT_S);
			roce_set_field(*old_send,
				       ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
				       ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S,
				       send_ptr);
		}
	}
}

static int check_qp_db_process_status(struct hns_roce_dev *hr_dev,
				      struct hns_roce_qp *hr_qp,
				      u32 sdb_issue_ptr,
				      u32 *sdb_inv_cnt,
				      u32 *wait_stage)
{
	struct device *dev = &hr_dev->pdev->dev;
	u32 sdb_send_ptr, old_send;
	u32 success_flags = 0;
	unsigned long end;
	u32 old_retry;
	u32 inv_cnt;
	u32 tsp_st;

	if (*wait_stage > HNS_ROCE_V1_DB_STAGE2 ||
	    *wait_stage < HNS_ROCE_V1_DB_STAGE1) {
		dev_err(dev, "QP(0x%lx) db status wait stage(%d) error!\n",
			hr_qp->qpn, *wait_stage);
		return -EINVAL;
	}

	/* Calculate the total timeout for the entire verification process */
	end = msecs_to_jiffies(HNS_ROCE_V1_CHECK_DB_TIMEOUT_MSECS) + jiffies;

	if (*wait_stage == HNS_ROCE_V1_DB_STAGE1) {
		/* Query db process status, until hw process completely */
		sdb_send_ptr = roce_read(hr_dev, ROCEE_SDB_SEND_PTR_REG);
		while (roce_hw_index_cmp_lt(sdb_send_ptr, sdb_issue_ptr,
					    ROCEE_SDB_PTR_CMP_BITS)) {
			if (!time_before(jiffies, end)) {
				dev_dbg(dev, "QP(0x%lx) db process stage1 timeout. issue 0x%x send 0x%x.\n",
					hr_qp->qpn, sdb_issue_ptr,
					sdb_send_ptr);
				return 0;
			}

			msleep(HNS_ROCE_V1_CHECK_DB_SLEEP_MSECS);
			sdb_send_ptr = roce_read(hr_dev,
						 ROCEE_SDB_SEND_PTR_REG);
		}

		if (roce_get_field(sdb_issue_ptr,
				   ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_M,
				   ROCEE_SDB_ISSUE_PTR_SDB_ISSUE_PTR_S) ==
		    roce_get_field(sdb_send_ptr,
				   ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_M,
				   ROCEE_SDB_SEND_PTR_SDB_SEND_PTR_S)) {
			old_send = roce_read(hr_dev, ROCEE_SDB_SEND_PTR_REG);
			old_retry = roce_read(hr_dev, ROCEE_SDB_RETRY_CNT_REG);

			do {
				tsp_st = roce_read(hr_dev, ROCEE_TSP_BP_ST_REG);
				if (roce_get_bit(tsp_st,
					ROCEE_TSP_BP_ST_QH_FIFO_ENTRY_S) == 1) {
					*wait_stage = HNS_ROCE_V1_DB_WAIT_OK;
					return 0;
				}

				if (!time_before(jiffies, end)) {
					dev_dbg(dev, "QP(0x%lx) db process stage1 timeout when send ptr equals issue ptr.\n"
						     "issue 0x%x send 0x%x.\n",
						hr_qp->qpn, sdb_issue_ptr,
						sdb_send_ptr);
					return 0;
				}

				msleep(HNS_ROCE_V1_CHECK_DB_SLEEP_MSECS);

				hns_roce_check_sdb_status(hr_dev, &old_send,
							  &old_retry, &tsp_st,
							  &success_flags);
			} while (!success_flags);
		}

		*wait_stage = HNS_ROCE_V1_DB_STAGE2;

		/* Get list pointer */
		*sdb_inv_cnt = roce_read(hr_dev, ROCEE_SDB_INV_CNT_REG);
		dev_dbg(dev, "QP(0x%lx) db process stage2. inv cnt = 0x%x.\n",
			hr_qp->qpn, *sdb_inv_cnt);
	}

	if (*wait_stage == HNS_ROCE_V1_DB_STAGE2) {
		/* Query db's list status, until hw reversal */
		inv_cnt = roce_read(hr_dev, ROCEE_SDB_INV_CNT_REG);
		while (roce_hw_index_cmp_lt(inv_cnt,
					    *sdb_inv_cnt + SDB_INV_CNT_OFFSET,
					    ROCEE_SDB_CNT_CMP_BITS)) {
			if (!time_before(jiffies, end)) {
				dev_dbg(dev, "QP(0x%lx) db process stage2 timeout. inv cnt 0x%x.\n",
					hr_qp->qpn, inv_cnt);
				return 0;
			}

			msleep(HNS_ROCE_V1_CHECK_DB_SLEEP_MSECS);
			inv_cnt = roce_read(hr_dev, ROCEE_SDB_INV_CNT_REG);
		}

		*wait_stage = HNS_ROCE_V1_DB_WAIT_OK;
	}

	return 0;
}

static int check_qp_reset_state(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *hr_qp,
				struct hns_roce_qp_work *qp_work_entry,
				int *is_timeout)
{
	struct device *dev = &hr_dev->pdev->dev;
	u32 sdb_issue_ptr;
	int ret;

	if (hr_qp->state != IB_QPS_RESET) {
		/* Set qp to ERR, waiting for hw complete processing all dbs */
		ret = hns_roce_v1_modify_qp(&hr_qp->ibqp, NULL, 0, hr_qp->state,
					    IB_QPS_ERR);
		if (ret) {
			dev_err(dev, "Modify QP(0x%lx) to ERR failed!\n",
				hr_qp->qpn);
			return ret;
		}

		/* Record issued doorbell */
		sdb_issue_ptr = roce_read(hr_dev, ROCEE_SDB_ISSUE_PTR_REG);
		qp_work_entry->sdb_issue_ptr = sdb_issue_ptr;
		qp_work_entry->db_wait_stage = HNS_ROCE_V1_DB_STAGE1;

		/* Query db process status, until hw process completely */
		ret = check_qp_db_process_status(hr_dev, hr_qp, sdb_issue_ptr,
						 &qp_work_entry->sdb_inv_cnt,
						 &qp_work_entry->db_wait_stage);
		if (ret) {
			dev_err(dev, "Check QP(0x%lx) db process status failed!\n",
				hr_qp->qpn);
			return ret;
		}

		if (qp_work_entry->db_wait_stage != HNS_ROCE_V1_DB_WAIT_OK) {
			qp_work_entry->sche_cnt = 0;
			*is_timeout = 1;
			return 0;
		}

		/* Modify qp to reset before destroying qp */
		ret = hns_roce_v1_modify_qp(&hr_qp->ibqp, NULL, 0, hr_qp->state,
					    IB_QPS_RESET);
		if (ret) {
			dev_err(dev, "Modify QP(0x%lx) to RST failed!\n",
				hr_qp->qpn);
			return ret;
		}
	}

	return 0;
}

static void hns_roce_v1_destroy_qp_work_fn(struct work_struct *work)
{
	struct hns_roce_qp_work *qp_work_entry;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_dev *hr_dev;
	struct hns_roce_qp *hr_qp;
	struct device *dev;
	unsigned long qpn;
	int ret;

	qp_work_entry = container_of(work, struct hns_roce_qp_work, work);
	hr_dev = to_hr_dev(qp_work_entry->ib_dev);
	dev = &hr_dev->pdev->dev;
	priv = (struct hns_roce_v1_priv *)hr_dev->priv;
	hr_qp = qp_work_entry->qp;
	qpn = hr_qp->qpn;

	dev_dbg(dev, "Schedule destroy QP(0x%lx) work.\n", qpn);

	qp_work_entry->sche_cnt++;

	/* Query db process status, until hw process completely */
	ret = check_qp_db_process_status(hr_dev, hr_qp,
					 qp_work_entry->sdb_issue_ptr,
					 &qp_work_entry->sdb_inv_cnt,
					 &qp_work_entry->db_wait_stage);
	if (ret) {
		dev_err(dev, "Check QP(0x%lx) db process status failed!\n",
			qpn);
		return;
	}

	if (qp_work_entry->db_wait_stage != HNS_ROCE_V1_DB_WAIT_OK &&
	    priv->des_qp.requeue_flag) {
		queue_work(priv->des_qp.qp_wq, work);
		return;
	}

	/* Modify qp to reset before destroying qp */
	ret = hns_roce_v1_modify_qp(&hr_qp->ibqp, NULL, 0, hr_qp->state,
				    IB_QPS_RESET);
	if (ret) {
		dev_err(dev, "Modify QP(0x%lx) to RST failed!\n", qpn);
		return;
	}

	hns_roce_qp_remove(hr_dev, hr_qp);
	hns_roce_qp_free(hr_dev, hr_qp);

	if (hr_qp->ibqp.qp_type == IB_QPT_RC) {
		/* RC QP, release QPN */
		hns_roce_release_range_qp(hr_dev, qpn, 1);
		kfree(hr_qp);
	} else
		kfree(hr_to_hr_sqp(hr_qp));

	kfree(qp_work_entry);

	dev_dbg(dev, "Accomplished destroy QP(0x%lx) work.\n", qpn);
}

int hns_roce_v1_destroy_qp(struct ib_qp *ibqp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_qp_work qp_work_entry;
	struct hns_roce_qp_work *qp_work;
	struct hns_roce_v1_priv *priv;
	struct hns_roce_cq *send_cq, *recv_cq;
	int is_user = !!ibqp->pd->uobject;
	int is_timeout = 0;
	int ret;

	ret = check_qp_reset_state(hr_dev, hr_qp, &qp_work_entry, &is_timeout);
	if (ret) {
		dev_err(dev, "QP reset state check failed(%d)!\n", ret);
		return ret;
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
	hns_roce_unlock_cqs(send_cq, recv_cq);

	if (!is_timeout) {
		hns_roce_qp_remove(hr_dev, hr_qp);
		hns_roce_qp_free(hr_dev, hr_qp);

		/* RC QP, release QPN */
		if (hr_qp->ibqp.qp_type == IB_QPT_RC)
			hns_roce_release_range_qp(hr_dev, hr_qp->qpn, 1);
	}

	hns_roce_mtt_cleanup(hr_dev, &hr_qp->mtt);

	if (is_user)
		ib_umem_release(hr_qp->umem);
	else {
		kfree(hr_qp->sq.wrid);
		kfree(hr_qp->rq.wrid);

		hns_roce_buf_free(hr_dev, hr_qp->buff_size, &hr_qp->hr_buf);
	}

	if (!is_timeout) {
		if (hr_qp->ibqp.qp_type == IB_QPT_RC)
			kfree(hr_qp);
		else
			kfree(hr_to_hr_sqp(hr_qp));
	} else {
		qp_work = kzalloc(sizeof(*qp_work), GFP_KERNEL);
		if (!qp_work)
			return -ENOMEM;

		INIT_WORK(&qp_work->work, hns_roce_v1_destroy_qp_work_fn);
		qp_work->ib_dev	= &hr_dev->ib_dev;
		qp_work->qp		= hr_qp;
		qp_work->db_wait_stage	= qp_work_entry.db_wait_stage;
		qp_work->sdb_issue_ptr	= qp_work_entry.sdb_issue_ptr;
		qp_work->sdb_inv_cnt	= qp_work_entry.sdb_inv_cnt;
		qp_work->sche_cnt	= qp_work_entry.sche_cnt;

		priv = (struct hns_roce_v1_priv *)hr_dev->priv;
		queue_work(priv->des_qp.qp_wq, &qp_work->work);
		dev_dbg(dev, "Begin destroy QP(0x%lx) work.\n", hr_qp->qpn);
	}

	return 0;
}

static int hns_roce_v1_destroy_cq(struct ib_cq *ibcq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibcq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	struct device *dev = &hr_dev->pdev->dev;
	u32 cqe_cnt_ori;
	u32 cqe_cnt_cur;
	u32 cq_buf_size;
	int wait_time = 0;
	int ret = 0;

	hns_roce_free_cq(hr_dev, hr_cq);

	/*
	 * Before freeing cq buffer, we need to ensure that the outstanding CQE
	 * have been written by checking the CQE counter.
	 */
	cqe_cnt_ori = roce_read(hr_dev, ROCEE_SCAEP_WR_CQE_CNT);
	while (1) {
		if (roce_read(hr_dev, ROCEE_CAEP_CQE_WCMD_EMPTY) &
		    HNS_ROCE_CQE_WCMD_EMPTY_BIT)
			break;

		cqe_cnt_cur = roce_read(hr_dev, ROCEE_SCAEP_WR_CQE_CNT);
		if ((cqe_cnt_cur - cqe_cnt_ori) >= HNS_ROCE_MIN_CQE_CNT)
			break;

		msleep(HNS_ROCE_EACH_FREE_CQ_WAIT_MSECS);
		if (wait_time > HNS_ROCE_MAX_FREE_CQ_WAIT_CNT) {
			dev_warn(dev, "Destroy cq 0x%lx timeout!\n",
				hr_cq->cqn);
			ret = -ETIMEDOUT;
			break;
		}
		wait_time++;
	}

	hns_roce_mtt_cleanup(hr_dev, &hr_cq->hr_buf.hr_mtt);

	if (ibcq->uobject)
		ib_umem_release(hr_cq->umem);
	else {
		/* Free the buff of stored cq */
		cq_buf_size = (ibcq->cqe + 1) * hr_dev->caps.cq_entry_sz;
		hns_roce_buf_free(hr_dev, cq_buf_size, &hr_cq->hr_buf.hr_buf);
	}

	kfree(hr_cq);

	return ret;
}

static void set_eq_cons_index_v1(struct hns_roce_eq *eq, int req_not)
{
	roce_raw_write((eq->cons_index & HNS_ROCE_V1_CONS_IDX_M) |
		      (req_not << eq->log_entries), eq->doorbell);
}

static void hns_roce_v1_wq_catas_err_handle(struct hns_roce_dev *hr_dev,
					    struct hns_roce_aeqe *aeqe, int qpn)
{
	struct device *dev = &hr_dev->pdev->dev;

	dev_warn(dev, "Local Work Queue Catastrophic Error.\n");
	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_LWQCE_QPC_ERROR:
		dev_warn(dev, "QP %d, QPC error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_MTU_ERROR:
		dev_warn(dev, "QP %d, MTU error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_WQE_BA_ADDR_ERROR:
		dev_warn(dev, "QP %d, WQE BA addr error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_WQE_ADDR_ERROR:
		dev_warn(dev, "QP %d, WQE addr error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_SQ_WQE_SHIFT_ERROR:
		dev_warn(dev, "QP %d, WQE shift error\n", qpn);
		break;
	case HNS_ROCE_LWQCE_SL_ERROR:
		dev_warn(dev, "QP %d, SL error.\n", qpn);
		break;
	case HNS_ROCE_LWQCE_PORT_ERROR:
		dev_warn(dev, "QP %d, port error.\n", qpn);
		break;
	default:
		break;
	}
}

static void hns_roce_v1_local_wq_access_err_handle(struct hns_roce_dev *hr_dev,
						   struct hns_roce_aeqe *aeqe,
						   int qpn)
{
	struct device *dev = &hr_dev->pdev->dev;

	dev_warn(dev, "Local Access Violation Work Queue Error.\n");
	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_LAVWQE_R_KEY_VIOLATION:
		dev_warn(dev, "QP %d, R_key violation.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_LENGTH_ERROR:
		dev_warn(dev, "QP %d, length error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_VA_ERROR:
		dev_warn(dev, "QP %d, VA error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_PD_ERROR:
		dev_err(dev, "QP %d, PD error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_RW_ACC_ERROR:
		dev_warn(dev, "QP %d, rw acc error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_KEY_STATE_ERROR:
		dev_warn(dev, "QP %d, key state error.\n", qpn);
		break;
	case HNS_ROCE_LAVWQE_MR_OPERATION_ERROR:
		dev_warn(dev, "QP %d, MR operation error.\n", qpn);
		break;
	default:
		break;
	}
}

static void hns_roce_v1_qp_err_handle(struct hns_roce_dev *hr_dev,
				      struct hns_roce_aeqe *aeqe,
				      int event_type)
{
	struct device *dev = &hr_dev->pdev->dev;
	int phy_port;
	int qpn;

	qpn = roce_get_field(aeqe->event.qp_event.qp,
			     HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_M,
			     HNS_ROCE_AEQE_EVENT_QP_EVENT_QP_QPN_S);
	phy_port = roce_get_field(aeqe->event.qp_event.qp,
				  HNS_ROCE_AEQE_EVENT_QP_EVENT_PORT_NUM_M,
				  HNS_ROCE_AEQE_EVENT_QP_EVENT_PORT_NUM_S);
	if (qpn <= 1)
		qpn = HNS_ROCE_MAX_PORTS * qpn + phy_port;

	switch (event_type) {
	case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		dev_warn(dev, "Invalid Req Local Work Queue Error.\n"
			 "QP %d, phy_port %d.\n", qpn, phy_port);
		break;
	case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		hns_roce_v1_wq_catas_err_handle(hr_dev, aeqe, qpn);
		break;
	case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
		hns_roce_v1_local_wq_access_err_handle(hr_dev, aeqe, qpn);
		break;
	default:
		break;
	}

	hns_roce_qp_event(hr_dev, qpn, event_type);
}

static void hns_roce_v1_cq_err_handle(struct hns_roce_dev *hr_dev,
				      struct hns_roce_aeqe *aeqe,
				      int event_type)
{
	struct device *dev = &hr_dev->pdev->dev;
	u32 cqn;

	cqn = le32_to_cpu(roce_get_field(aeqe->event.cq_event.cq,
			  HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_M,
			  HNS_ROCE_AEQE_EVENT_CQ_EVENT_CQ_CQN_S));

	switch (event_type) {
	case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		dev_warn(dev, "CQ 0x%x access err.\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		dev_warn(dev, "CQ 0x%x overflow\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID:
		dev_warn(dev, "CQ 0x%x ID invalid.\n", cqn);
		break;
	default:
		break;
	}

	hns_roce_cq_event(hr_dev, cqn, event_type);
}

static void hns_roce_v1_db_overflow_handle(struct hns_roce_dev *hr_dev,
					   struct hns_roce_aeqe *aeqe)
{
	struct device *dev = &hr_dev->pdev->dev;

	switch (roce_get_field(aeqe->asyn, HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_M,
			       HNS_ROCE_AEQE_U32_4_EVENT_SUB_TYPE_S)) {
	case HNS_ROCE_DB_SUBTYPE_SDB_OVF:
		dev_warn(dev, "SDB overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_SDB_ALM_OVF:
		dev_warn(dev, "SDB almost overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_SDB_ALM_EMP:
		dev_warn(dev, "SDB almost empty.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_OVF:
		dev_warn(dev, "ODB overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_ALM_OVF:
		dev_warn(dev, "ODB almost overflow.\n");
		break;
	case HNS_ROCE_DB_SUBTYPE_ODB_ALM_EMP:
		dev_warn(dev, "SDB almost empty.\n");
		break;
	default:
		break;
	}
}

static struct hns_roce_aeqe *get_aeqe_v1(struct hns_roce_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->entries - 1)) *
			     HNS_ROCE_AEQ_ENTRY_SIZE;

	return (struct hns_roce_aeqe *)((u8 *)
		(eq->buf_list[off / HNS_ROCE_BA_SIZE].buf) +
		off % HNS_ROCE_BA_SIZE);
}

static struct hns_roce_aeqe *next_aeqe_sw_v1(struct hns_roce_eq *eq)
{
	struct hns_roce_aeqe *aeqe = get_aeqe_v1(eq, eq->cons_index);

	return (roce_get_bit(aeqe->asyn, HNS_ROCE_AEQE_U32_4_OWNER_S) ^
		!!(eq->cons_index & eq->entries)) ? aeqe : NULL;
}

static int hns_roce_v1_aeq_int(struct hns_roce_dev *hr_dev,
			       struct hns_roce_eq *eq)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_aeqe *aeqe;
	int aeqes_found = 0;
	int event_type;

	while ((aeqe = next_aeqe_sw_v1(eq))) {

		/* Make sure we read the AEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		dev_dbg(dev, "aeqe = %p, aeqe->asyn.event_type = 0x%lx\n", aeqe,
			roce_get_field(aeqe->asyn,
				       HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M,
				       HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S));
		event_type = roce_get_field(aeqe->asyn,
					    HNS_ROCE_AEQE_U32_4_EVENT_TYPE_M,
					    HNS_ROCE_AEQE_U32_4_EVENT_TYPE_S);
		switch (event_type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
			dev_warn(dev, "PATH MIG not supported\n");
			break;
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
			dev_warn(dev, "COMMUNICATION established\n");
			break;
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
			dev_warn(dev, "SQ DRAINED not supported\n");
			break;
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
			dev_warn(dev, "PATH MIG failed\n");
			break;
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
			hns_roce_v1_qp_err_handle(hr_dev, aeqe, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
			dev_warn(dev, "SRQ not support!\n");
			break;
		case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		case HNS_ROCE_EVENT_TYPE_CQ_ID_INVALID:
			hns_roce_v1_cq_err_handle(hr_dev, aeqe, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_PORT_CHANGE:
			dev_warn(dev, "port change.\n");
			break;
		case HNS_ROCE_EVENT_TYPE_MB:
			hns_roce_cmd_event(hr_dev,
					   le16_to_cpu(aeqe->event.cmd.token),
					   aeqe->event.cmd.status,
					   le64_to_cpu(aeqe->event.cmd.out_param
					   ));
			break;
		case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
			hns_roce_v1_db_overflow_handle(hr_dev, aeqe);
			break;
		case HNS_ROCE_EVENT_TYPE_CEQ_OVERFLOW:
			dev_warn(dev, "CEQ 0x%lx overflow.\n",
			roce_get_field(aeqe->event.ce_event.ceqe,
				     HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_M,
				     HNS_ROCE_AEQE_EVENT_CE_EVENT_CEQE_CEQN_S));
			break;
		default:
			dev_warn(dev, "Unhandled event %d on EQ %d at idx %u.\n",
				 event_type, eq->eqn, eq->cons_index);
			break;
		}

		eq->cons_index++;
		aeqes_found = 1;

		if (eq->cons_index > 2 * hr_dev->caps.aeqe_depth - 1) {
			dev_warn(dev, "cons_index overflow, set back to 0.\n");
			eq->cons_index = 0;
		}
	}

	set_eq_cons_index_v1(eq, 0);

	return aeqes_found;
}

static struct hns_roce_ceqe *get_ceqe_v1(struct hns_roce_eq *eq, u32 entry)
{
	unsigned long off = (entry & (eq->entries - 1)) *
			     HNS_ROCE_CEQ_ENTRY_SIZE;

	return (struct hns_roce_ceqe *)((u8 *)
			(eq->buf_list[off / HNS_ROCE_BA_SIZE].buf) +
			off % HNS_ROCE_BA_SIZE);
}

static struct hns_roce_ceqe *next_ceqe_sw_v1(struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe = get_ceqe_v1(eq, eq->cons_index);

	return (!!(roce_get_bit(ceqe->comp,
		HNS_ROCE_CEQE_CEQE_COMP_OWNER_S))) ^
		(!!(eq->cons_index & eq->entries)) ? ceqe : NULL;
}

static int hns_roce_v1_ceq_int(struct hns_roce_dev *hr_dev,
			       struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe;
	int ceqes_found = 0;
	u32 cqn;

	while ((ceqe = next_ceqe_sw_v1(eq))) {

		/* Make sure we read CEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		cqn = roce_get_field(ceqe->comp,
				     HNS_ROCE_CEQE_CEQE_COMP_CQN_M,
				     HNS_ROCE_CEQE_CEQE_COMP_CQN_S);
		hns_roce_cq_completion(hr_dev, cqn);

		++eq->cons_index;
		ceqes_found = 1;

		if (eq->cons_index > 2 * hr_dev->caps.ceqe_depth - 1) {
			dev_warn(&eq->hr_dev->pdev->dev,
				"cons_index overflow, set back to 0.\n");
			eq->cons_index = 0;
		}
	}

	set_eq_cons_index_v1(eq, 0);

	return ceqes_found;
}

static irqreturn_t hns_roce_v1_msix_interrupt_eq(int irq, void *eq_ptr)
{
	struct hns_roce_eq  *eq  = eq_ptr;
	struct hns_roce_dev *hr_dev = eq->hr_dev;
	int int_work = 0;

	if (eq->type_flag == HNS_ROCE_CEQ)
		/* CEQ irq routine, CEQ is pulse irq, not clear */
		int_work = hns_roce_v1_ceq_int(hr_dev, eq);
	else
		/* AEQ irq routine, AEQ is pulse irq, not clear */
		int_work = hns_roce_v1_aeq_int(hr_dev, eq);

	return IRQ_RETVAL(int_work);
}

static irqreturn_t hns_roce_v1_msix_interrupt_abn(int irq, void *dev_id)
{
	struct hns_roce_dev *hr_dev = dev_id;
	struct device *dev = &hr_dev->pdev->dev;
	int int_work = 0;
	u32 caepaemask_val;
	u32 cealmovf_val;
	u32 caepaest_val;
	u32 aeshift_val;
	u32 ceshift_val;
	u32 cemask_val;
	int i;

	/*
	 * Abnormal interrupt:
	 * AEQ overflow, ECC multi-bit err, CEQ overflow must clear
	 * interrupt, mask irq, clear irq, cancel mask operation
	 */
	aeshift_val = roce_read(hr_dev, ROCEE_CAEP_AEQC_AEQE_SHIFT_REG);

	/* AEQE overflow */
	if (roce_get_bit(aeshift_val,
		ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQ_ALM_OVF_INT_ST_S) == 1) {
		dev_warn(dev, "AEQ overflow!\n");

		/* Set mask */
		caepaemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
		roce_set_bit(caepaemask_val,
			     ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
			     HNS_ROCE_INT_MASK_ENABLE);
		roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, caepaemask_val);

		/* Clear int state(INT_WC : write 1 clear) */
		caepaest_val = roce_read(hr_dev, ROCEE_CAEP_AE_ST_REG);
		roce_set_bit(caepaest_val,
			     ROCEE_CAEP_AE_ST_CAEP_AEQ_ALM_OVF_S, 1);
		roce_write(hr_dev, ROCEE_CAEP_AE_ST_REG, caepaest_val);

		/* Clear mask */
		caepaemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
		roce_set_bit(caepaemask_val,
			     ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
			     HNS_ROCE_INT_MASK_DISABLE);
		roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, caepaemask_val);
	}

	/* CEQ almost overflow */
	for (i = 0; i < hr_dev->caps.num_comp_vectors; i++) {
		ceshift_val = roce_read(hr_dev, ROCEE_CAEP_CEQC_SHIFT_0_REG +
					i * CEQ_REG_OFFSET);

		if (roce_get_bit(ceshift_val,
			ROCEE_CAEP_CEQC_SHIFT_CAEP_CEQ_ALM_OVF_INT_ST_S) == 1) {
			dev_warn(dev, "CEQ[%d] almost overflow!\n", i);
			int_work++;

			/* Set mask */
			cemask_val = roce_read(hr_dev,
					       ROCEE_CAEP_CE_IRQ_MASK_0_REG +
					       i * CEQ_REG_OFFSET);
			roce_set_bit(cemask_val,
				ROCEE_CAEP_CE_IRQ_MASK_CAEP_CEQ_ALM_OVF_MASK_S,
				HNS_ROCE_INT_MASK_ENABLE);
			roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				   i * CEQ_REG_OFFSET, cemask_val);

			/* Clear int state(INT_WC : write 1 clear) */
			cealmovf_val = roce_read(hr_dev,
				       ROCEE_CAEP_CEQ_ALM_OVF_0_REG +
				       i * CEQ_REG_OFFSET);
			roce_set_bit(cealmovf_val,
				     ROCEE_CAEP_CEQ_ALM_OVF_CAEP_CEQ_ALM_OVF_S,
				     1);
			roce_write(hr_dev, ROCEE_CAEP_CEQ_ALM_OVF_0_REG +
				   i * CEQ_REG_OFFSET, cealmovf_val);

			/* Clear mask */
			cemask_val = roce_read(hr_dev,
				     ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				     i * CEQ_REG_OFFSET);
			roce_set_bit(cemask_val,
			       ROCEE_CAEP_CE_IRQ_MASK_CAEP_CEQ_ALM_OVF_MASK_S,
			       HNS_ROCE_INT_MASK_DISABLE);
			roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
				   i * CEQ_REG_OFFSET, cemask_val);
		}
	}

	/* ECC multi-bit error alarm */
	dev_warn(dev, "ECC UCERR ALARM: 0x%x, 0x%x, 0x%x\n",
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM0_REG),
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM1_REG),
		 roce_read(hr_dev, ROCEE_ECC_UCERR_ALM2_REG));

	dev_warn(dev, "ECC CERR ALARM: 0x%x, 0x%x, 0x%x\n",
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM0_REG),
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM1_REG),
		 roce_read(hr_dev, ROCEE_ECC_CERR_ALM2_REG));

	return IRQ_RETVAL(int_work);
}

static void hns_roce_v1_int_mask_enable(struct hns_roce_dev *hr_dev)
{
	u32 aemask_val;
	int masken = 0;
	int i;

	/* AEQ INT */
	aemask_val = roce_read(hr_dev, ROCEE_CAEP_AE_MASK_REG);
	roce_set_bit(aemask_val, ROCEE_CAEP_AE_MASK_CAEP_AEQ_ALM_OVF_MASK_S,
		     masken);
	roce_set_bit(aemask_val, ROCEE_CAEP_AE_MASK_CAEP_AE_IRQ_MASK_S, masken);
	roce_write(hr_dev, ROCEE_CAEP_AE_MASK_REG, aemask_val);

	/* CEQ INT */
	for (i = 0; i < hr_dev->caps.num_comp_vectors; i++) {
		/* IRQ mask */
		roce_write(hr_dev, ROCEE_CAEP_CE_IRQ_MASK_0_REG +
			   i * CEQ_REG_OFFSET, masken);
	}
}

static void hns_roce_v1_free_eq(struct hns_roce_dev *hr_dev,
				struct hns_roce_eq *eq)
{
	int npages = (PAGE_ALIGN(eq->eqe_size * eq->entries) +
		      HNS_ROCE_BA_SIZE - 1) / HNS_ROCE_BA_SIZE;
	int i;

	if (!eq->buf_list)
		return;

	for (i = 0; i < npages; ++i)
		dma_free_coherent(&hr_dev->pdev->dev, HNS_ROCE_BA_SIZE,
				  eq->buf_list[i].buf, eq->buf_list[i].map);

	kfree(eq->buf_list);
}

static void hns_roce_v1_enable_eq(struct hns_roce_dev *hr_dev, int eq_num,
				  int enable_flag)
{
	void __iomem *eqc = hr_dev->eq_table.eqc_base[eq_num];
	u32 val;

	val = readl(eqc);

	if (enable_flag)
		roce_set_field(val,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
			       HNS_ROCE_EQ_STAT_VALID);
	else
		roce_set_field(val,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
			       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
			       HNS_ROCE_EQ_STAT_INVALID);
	writel(val, eqc);
}

static int hns_roce_v1_create_eq(struct hns_roce_dev *hr_dev,
				 struct hns_roce_eq *eq)
{
	void __iomem *eqc = hr_dev->eq_table.eqc_base[eq->eqn];
	struct device *dev = &hr_dev->pdev->dev;
	dma_addr_t tmp_dma_addr;
	u32 eqconsindx_val = 0;
	u32 eqcuridx_val = 0;
	u32 eqshift_val = 0;
	int num_bas;
	int ret;
	int i;

	num_bas = (PAGE_ALIGN(eq->entries * eq->eqe_size) +
		   HNS_ROCE_BA_SIZE - 1) / HNS_ROCE_BA_SIZE;

	if ((eq->entries * eq->eqe_size) > HNS_ROCE_BA_SIZE) {
		dev_err(dev, "[error]eq buf %d gt ba size(%d) need bas=%d\n",
			(eq->entries * eq->eqe_size), HNS_ROCE_BA_SIZE,
			num_bas);
		return -EINVAL;
	}

	eq->buf_list = kcalloc(num_bas, sizeof(*eq->buf_list), GFP_KERNEL);
	if (!eq->buf_list)
		return -ENOMEM;

	for (i = 0; i < num_bas; ++i) {
		eq->buf_list[i].buf = dma_alloc_coherent(dev, HNS_ROCE_BA_SIZE,
							 &tmp_dma_addr,
							 GFP_KERNEL);
		if (!eq->buf_list[i].buf) {
			ret = -ENOMEM;
			goto err_out_free_pages;
		}

		eq->buf_list[i].map = tmp_dma_addr;
		memset(eq->buf_list[i].buf, 0, HNS_ROCE_BA_SIZE);
	}
	eq->cons_index = 0;
	roce_set_field(eqshift_val,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_M,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_STATE_S,
		       HNS_ROCE_EQ_STAT_INVALID);
	roce_set_field(eqshift_val,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_M,
		       ROCEE_CAEP_AEQC_AEQE_SHIFT_CAEP_AEQC_AEQE_SHIFT_S,
		       eq->log_entries);
	writel(eqshift_val, eqc);

	/* Configure eq extended address 12~44bit */
	writel((u32)(eq->buf_list[0].map >> 12), eqc + 4);

	/*
	 * Configure eq extended address 45~49 bit.
	 * 44 = 32 + 12, When evaluating addr to hardware, shift 12 because of
	 * using 4K page, and shift more 32 because of
	 * caculating the high 32 bit value evaluated to hardware.
	 */
	roce_set_field(eqcuridx_val, ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_M,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQ_BT_H_S,
		       eq->buf_list[0].map >> 44);
	roce_set_field(eqcuridx_val,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_M,
		       ROCEE_CAEP_AEQE_CUR_IDX_CAEP_AEQE_CUR_IDX_S, 0);
	writel(eqcuridx_val, eqc + 8);

	/* Configure eq consumer index */
	roce_set_field(eqconsindx_val,
		       ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_M,
		       ROCEE_CAEP_AEQE_CONS_IDX_CAEP_AEQE_CONS_IDX_S, 0);
	writel(eqconsindx_val, eqc + 0xc);

	return 0;

err_out_free_pages:
	for (i -= 1; i >= 0; i--)
		dma_free_coherent(dev, HNS_ROCE_BA_SIZE, eq->buf_list[i].buf,
				  eq->buf_list[i].map);

	kfree(eq->buf_list);
	return ret;
}

static int hns_roce_v1_init_eq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	struct device *dev = &hr_dev->pdev->dev;
	struct hns_roce_eq *eq;
	int irq_num;
	int eq_num;
	int ret;
	int i, j;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;
	irq_num = eq_num + hr_dev->caps.num_other_vectors;

	eq_table->eq = kcalloc(eq_num, sizeof(*eq_table->eq), GFP_KERNEL);
	if (!eq_table->eq)
		return -ENOMEM;

	eq_table->eqc_base = kcalloc(eq_num, sizeof(*eq_table->eqc_base),
				     GFP_KERNEL);
	if (!eq_table->eqc_base) {
		ret = -ENOMEM;
		goto err_eqc_base_alloc_fail;
	}

	for (i = 0; i < eq_num; i++) {
		eq = &eq_table->eq[i];
		eq->hr_dev = hr_dev;
		eq->eqn = i;
		eq->irq = hr_dev->irq[i];
		eq->log_page_size = PAGE_SHIFT;

		if (i < hr_dev->caps.num_comp_vectors) {
			/* CEQ */
			eq_table->eqc_base[i] = hr_dev->reg_base +
						ROCEE_CAEP_CEQC_SHIFT_0_REG +
						CEQ_REG_OFFSET * i;
			eq->type_flag = HNS_ROCE_CEQ;
			eq->doorbell = hr_dev->reg_base +
				       ROCEE_CAEP_CEQC_CONS_IDX_0_REG +
				       CEQ_REG_OFFSET * i;
			eq->entries = hr_dev->caps.ceqe_depth;
			eq->log_entries = ilog2(eq->entries);
			eq->eqe_size = HNS_ROCE_CEQ_ENTRY_SIZE;
		} else {
			/* AEQ */
			eq_table->eqc_base[i] = hr_dev->reg_base +
						ROCEE_CAEP_AEQC_AEQE_SHIFT_REG;
			eq->type_flag = HNS_ROCE_AEQ;
			eq->doorbell = hr_dev->reg_base +
				       ROCEE_CAEP_AEQE_CONS_IDX_REG;
			eq->entries = hr_dev->caps.aeqe_depth;
			eq->log_entries = ilog2(eq->entries);
			eq->eqe_size = HNS_ROCE_AEQ_ENTRY_SIZE;
		}
	}

	/* Disable irq */
	hns_roce_v1_int_mask_enable(hr_dev);

	/* Configure ce int interval */
	roce_write(hr_dev, ROCEE_CAEP_CE_INTERVAL_CFG_REG,
		   HNS_ROCE_CEQ_DEFAULT_INTERVAL);

	/* Configure ce int burst num */
	roce_write(hr_dev, ROCEE_CAEP_CE_BURST_NUM_CFG_REG,
		   HNS_ROCE_CEQ_DEFAULT_BURST_NUM);

	for (i = 0; i < eq_num; i++) {
		ret = hns_roce_v1_create_eq(hr_dev, &eq_table->eq[i]);
		if (ret) {
			dev_err(dev, "eq create failed\n");
			goto err_create_eq_fail;
		}
	}

	for (j = 0; j < irq_num; j++) {
		if (j < eq_num)
			ret = request_irq(hr_dev->irq[j],
					  hns_roce_v1_msix_interrupt_eq, 0,
					  hr_dev->irq_names[j],
					  &eq_table->eq[j]);
		else
			ret = request_irq(hr_dev->irq[j],
					  hns_roce_v1_msix_interrupt_abn, 0,
					  hr_dev->irq_names[j], hr_dev);

		if (ret) {
			dev_err(dev, "request irq error!\n");
			goto err_request_irq_fail;
		}
	}

	for (i = 0; i < eq_num; i++)
		hns_roce_v1_enable_eq(hr_dev, i, EQ_ENABLE);

	return 0;

err_request_irq_fail:
	for (j -= 1; j >= 0; j--)
		free_irq(hr_dev->irq[j], &eq_table->eq[j]);

err_create_eq_fail:
	for (i -= 1; i >= 0; i--)
		hns_roce_v1_free_eq(hr_dev, &eq_table->eq[i]);

	kfree(eq_table->eqc_base);

err_eqc_base_alloc_fail:
	kfree(eq_table->eq);

	return ret;
}

static void hns_roce_v1_cleanup_eq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	int irq_num;
	int eq_num;
	int i;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;
	irq_num = eq_num + hr_dev->caps.num_other_vectors;
	for (i = 0; i < eq_num; i++) {
		/* Disable EQ */
		hns_roce_v1_enable_eq(hr_dev, i, EQ_DISABLE);

		free_irq(hr_dev->irq[i], &eq_table->eq[i]);

		hns_roce_v1_free_eq(hr_dev, &eq_table->eq[i]);
	}
	for (i = eq_num; i < irq_num; i++)
		free_irq(hr_dev->irq[i], hr_dev);

	kfree(eq_table->eqc_base);
	kfree(eq_table->eq);
}

static const struct hns_roce_hw hns_roce_hw_v1 = {
	.reset = hns_roce_v1_reset,
	.hw_profile = hns_roce_v1_profile,
	.hw_init = hns_roce_v1_init,
	.hw_exit = hns_roce_v1_exit,
	.post_mbox = hns_roce_v1_post_mbox,
	.chk_mbox = hns_roce_v1_chk_mbox,
	.set_gid = hns_roce_v1_set_gid,
	.set_mac = hns_roce_v1_set_mac,
	.set_mtu = hns_roce_v1_set_mtu,
	.write_mtpt = hns_roce_v1_write_mtpt,
	.write_cqc = hns_roce_v1_write_cqc,
	.modify_cq = hns_roce_v1_modify_cq,
	.clear_hem = hns_roce_v1_clear_hem,
	.modify_qp = hns_roce_v1_modify_qp,
	.query_qp = hns_roce_v1_query_qp,
	.destroy_qp = hns_roce_v1_destroy_qp,
	.post_send = hns_roce_v1_post_send,
	.post_recv = hns_roce_v1_post_recv,
	.req_notify_cq = hns_roce_v1_req_notify_cq,
	.poll_cq = hns_roce_v1_poll_cq,
	.dereg_mr = hns_roce_v1_dereg_mr,
	.destroy_cq = hns_roce_v1_destroy_cq,
	.init_eq = hns_roce_v1_init_eq_table,
	.cleanup_eq = hns_roce_v1_cleanup_eq_table,
};

static const struct of_device_id hns_roce_of_match[] = {
	{ .compatible = "hisilicon,hns-roce-v1", .data = &hns_roce_hw_v1, },
	{},
};
MODULE_DEVICE_TABLE(of, hns_roce_of_match);

static const struct acpi_device_id hns_roce_acpi_match[] = {
	{ "HISI00D1", (kernel_ulong_t)&hns_roce_hw_v1 },
	{},
};
MODULE_DEVICE_TABLE(acpi, hns_roce_acpi_match);

static int hns_roce_node_match(struct device *dev, void *fwnode)
{
	return dev->fwnode == fwnode;
}

static struct
platform_device *hns_roce_find_pdev(struct fwnode_handle *fwnode)
{
	struct device *dev;

	/* get the 'device' corresponding to the matching 'fwnode' */
	dev = bus_find_device(&platform_bus_type, NULL,
			      fwnode, hns_roce_node_match);
	/* get the platform device */
	return dev ? to_platform_device(dev) : NULL;
}

static int hns_roce_get_cfg(struct hns_roce_dev *hr_dev)
{
	struct device *dev = &hr_dev->pdev->dev;
	struct platform_device *pdev = NULL;
	struct net_device *netdev = NULL;
	struct device_node *net_node;
	struct resource *res;
	int port_cnt = 0;
	u8 phy_port;
	int ret;
	int i;

	/* check if we are compatible with the underlying SoC */
	if (dev_of_node(dev)) {
		const struct of_device_id *of_id;

		of_id = of_match_node(hns_roce_of_match, dev->of_node);
		if (!of_id) {
			dev_err(dev, "device is not compatible!\n");
			return -ENXIO;
		}
		hr_dev->hw = (const struct hns_roce_hw *)of_id->data;
		if (!hr_dev->hw) {
			dev_err(dev, "couldn't get H/W specific DT data!\n");
			return -ENXIO;
		}
	} else if (is_acpi_device_node(dev->fwnode)) {
		const struct acpi_device_id *acpi_id;

		acpi_id = acpi_match_device(hns_roce_acpi_match, dev);
		if (!acpi_id) {
			dev_err(dev, "device is not compatible!\n");
			return -ENXIO;
		}
		hr_dev->hw = (const struct hns_roce_hw *) acpi_id->driver_data;
		if (!hr_dev->hw) {
			dev_err(dev, "couldn't get H/W specific ACPI data!\n");
			return -ENXIO;
		}
	} else {
		dev_err(dev, "can't read compatibility data from DT or ACPI\n");
		return -ENXIO;
	}

	/* get the mapped register base address */
	res = platform_get_resource(hr_dev->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "memory resource not found!\n");
		return -EINVAL;
	}
	hr_dev->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(hr_dev->reg_base))
		return PTR_ERR(hr_dev->reg_base);

	/* read the node_guid of IB device from the DT or ACPI */
	ret = device_property_read_u8_array(dev, "node-guid",
					    (u8 *)&hr_dev->ib_dev.node_guid,
					    GUID_LEN);
	if (ret) {
		dev_err(dev, "couldn't get node_guid from DT or ACPI!\n");
		return ret;
	}

	/* get the RoCE associated ethernet ports or netdevices */
	for (i = 0; i < HNS_ROCE_MAX_PORTS; i++) {
		if (dev_of_node(dev)) {
			net_node = of_parse_phandle(dev->of_node, "eth-handle",
						    i);
			if (!net_node)
				continue;
			pdev = of_find_device_by_node(net_node);
		} else if (is_acpi_device_node(dev->fwnode)) {
			struct acpi_reference_args args;
			struct fwnode_handle *fwnode;

			ret = acpi_node_get_property_reference(dev->fwnode,
							       "eth-handle",
							       i, &args);
			if (ret)
				continue;
			fwnode = acpi_fwnode_handle(args.adev);
			pdev = hns_roce_find_pdev(fwnode);
		} else {
			dev_err(dev, "cannot read data from DT or ACPI\n");
			return -ENXIO;
		}

		if (pdev) {
			netdev = platform_get_drvdata(pdev);
			phy_port = (u8)i;
			if (netdev) {
				hr_dev->iboe.netdevs[port_cnt] = netdev;
				hr_dev->iboe.phy_port[port_cnt] = phy_port;
			} else {
				dev_err(dev, "no netdev found with pdev %s\n",
					pdev->name);
				return -ENODEV;
			}
			port_cnt++;
		}
	}

	if (port_cnt == 0) {
		dev_err(dev, "unable to get eth-handle for available ports!\n");
		return -EINVAL;
	}

	hr_dev->caps.num_ports = port_cnt;

	/* cmd issue mode: 0 is poll, 1 is event */
	hr_dev->cmd_mod = 1;
	hr_dev->loop_idc = 0;
	hr_dev->sdb_offset = ROCEE_DB_SQ_L_0_REG;
	hr_dev->odb_offset = ROCEE_DB_OTHERS_L_0_REG;

	/* read the interrupt names from the DT or ACPI */
	ret = device_property_read_string_array(dev, "interrupt-names",
						hr_dev->irq_names,
						HNS_ROCE_V1_MAX_IRQ_NUM);
	if (ret < 0) {
		dev_err(dev, "couldn't get interrupt names from DT or ACPI!\n");
		return ret;
	}

	/* fetch the interrupt numbers */
	for (i = 0; i < HNS_ROCE_V1_MAX_IRQ_NUM; i++) {
		hr_dev->irq[i] = platform_get_irq(hr_dev->pdev, i);
		if (hr_dev->irq[i] <= 0) {
			dev_err(dev, "platform get of irq[=%d] failed!\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

/**
 * hns_roce_probe - RoCE driver entrance
 * @pdev: pointer to platform device
 * Return : int
 *
 */
static int hns_roce_probe(struct platform_device *pdev)
{
	int ret;
	struct hns_roce_dev *hr_dev;
	struct device *dev = &pdev->dev;

	hr_dev = (struct hns_roce_dev *)ib_alloc_device(sizeof(*hr_dev));
	if (!hr_dev)
		return -ENOMEM;

	hr_dev->priv = kzalloc(sizeof(struct hns_roce_v1_priv), GFP_KERNEL);
	if (!hr_dev->priv) {
		ret = -ENOMEM;
		goto error_failed_kzalloc;
	}

	hr_dev->pdev = pdev;
	hr_dev->dev = dev;
	platform_set_drvdata(pdev, hr_dev);

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64ULL)) &&
	    dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32ULL))) {
		dev_err(dev, "Not usable DMA addressing mode\n");
		ret = -EIO;
		goto error_failed_get_cfg;
	}

	ret = hns_roce_get_cfg(hr_dev);
	if (ret) {
		dev_err(dev, "Get Configuration failed!\n");
		goto error_failed_get_cfg;
	}

	ret = hns_roce_init(hr_dev);
	if (ret) {
		dev_err(dev, "RoCE engine init failed!\n");
		goto error_failed_get_cfg;
	}

	return 0;

error_failed_get_cfg:
	kfree(hr_dev->priv);

error_failed_kzalloc:
	ib_dealloc_device(&hr_dev->ib_dev);

	return ret;
}

/**
 * hns_roce_remove - remove RoCE device
 * @pdev: pointer to platform device
 */
static int hns_roce_remove(struct platform_device *pdev)
{
	struct hns_roce_dev *hr_dev = platform_get_drvdata(pdev);

	hns_roce_exit(hr_dev);
	kfree(hr_dev->priv);
	ib_dealloc_device(&hr_dev->ib_dev);

	return 0;
}

static struct platform_driver hns_roce_driver = {
	.probe = hns_roce_probe,
	.remove = hns_roce_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = hns_roce_of_match,
		.acpi_match_table = ACPI_PTR(hns_roce_acpi_match),
	},
};

module_platform_driver(hns_roce_driver);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Nenglong Zhao <zhaonenglong@hisilicon.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_DESCRIPTION("Hisilicon Hip06 Family RoCE Driver");
