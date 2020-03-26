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
#include <linux/types.h>
#include <net/addrconf.h>
#include <rdma/ib_addr.h>
#include <rdma/ib_cache.h>
#include <rdma/ib_umem.h>
#include <rdma/uverbs_ioctl.h>

#include "hnae3.h"
#include "hns_roce_common.h"
#include "hns_roce_device.h"
#include "hns_roce_cmd.h"
#include "hns_roce_hem.h"
#include "hns_roce_hw_v2.h"

static void set_data_seg_v2(struct hns_roce_v2_wqe_data_seg *dseg,
			    struct ib_sge *sg)
{
	dseg->lkey = cpu_to_le32(sg->lkey);
	dseg->addr = cpu_to_le64(sg->addr);
	dseg->len  = cpu_to_le32(sg->length);
}

/*
 * mapped-value = 1 + real-value
 * The hns wr opcode real value is start from 0, In order to distinguish between
 * initialized and uninitialized map values, we plus 1 to the actual value when
 * defining the mapping, so that the validity can be identified by checking the
 * mapped value is greater than 0.
 */
#define HR_OPC_MAP(ib_key, hr_key) \
		[IB_WR_ ## ib_key] = 1 + HNS_ROCE_V2_WQE_OP_ ## hr_key

static const u32 hns_roce_op_code[] = {
	HR_OPC_MAP(RDMA_WRITE,			RDMA_WRITE),
	HR_OPC_MAP(RDMA_WRITE_WITH_IMM,		RDMA_WRITE_WITH_IMM),
	HR_OPC_MAP(SEND,			SEND),
	HR_OPC_MAP(SEND_WITH_IMM,		SEND_WITH_IMM),
	HR_OPC_MAP(RDMA_READ,			RDMA_READ),
	HR_OPC_MAP(ATOMIC_CMP_AND_SWP,		ATOM_CMP_AND_SWAP),
	HR_OPC_MAP(ATOMIC_FETCH_AND_ADD,	ATOM_FETCH_AND_ADD),
	HR_OPC_MAP(SEND_WITH_INV,		SEND_WITH_INV),
	HR_OPC_MAP(LOCAL_INV,			LOCAL_INV),
	HR_OPC_MAP(MASKED_ATOMIC_CMP_AND_SWP,	ATOM_MSK_CMP_AND_SWAP),
	HR_OPC_MAP(MASKED_ATOMIC_FETCH_AND_ADD,	ATOM_MSK_FETCH_AND_ADD),
	HR_OPC_MAP(REG_MR,			FAST_REG_PMR),
};

static u32 to_hr_opcode(u32 ib_opcode)
{
	if (ib_opcode >= ARRAY_SIZE(hns_roce_op_code))
		return HNS_ROCE_V2_WQE_OP_MASK;

	return hns_roce_op_code[ib_opcode] ? hns_roce_op_code[ib_opcode] - 1 :
					     HNS_ROCE_V2_WQE_OP_MASK;
}

static void set_frmr_seg(struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			 void *wqe, const struct ib_reg_wr *wr)
{
	struct hns_roce_mr *mr = to_hr_mr(wr->mr);
	struct hns_roce_wqe_frmr_seg *fseg = wqe;

	/* use ib_access_flags */
	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_FRMR_WQE_BYTE_4_BIND_EN_S,
		     wr->access & IB_ACCESS_MW_BIND ? 1 : 0);
	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_FRMR_WQE_BYTE_4_ATOMIC_S,
		     wr->access & IB_ACCESS_REMOTE_ATOMIC ? 1 : 0);
	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_FRMR_WQE_BYTE_4_RR_S,
		     wr->access & IB_ACCESS_REMOTE_READ ? 1 : 0);
	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_FRMR_WQE_BYTE_4_RW_S,
		     wr->access & IB_ACCESS_REMOTE_WRITE ? 1 : 0);
	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_FRMR_WQE_BYTE_4_LW_S,
		     wr->access & IB_ACCESS_LOCAL_WRITE ? 1 : 0);

	/* Data structure reuse may lead to confusion */
	rc_sq_wqe->msg_len = cpu_to_le32(mr->pbl_ba & 0xffffffff);
	rc_sq_wqe->inv_key = cpu_to_le32(mr->pbl_ba >> 32);

	rc_sq_wqe->byte_16 = cpu_to_le32(wr->mr->length & 0xffffffff);
	rc_sq_wqe->byte_20 = cpu_to_le32(wr->mr->length >> 32);
	rc_sq_wqe->rkey = cpu_to_le32(wr->key);
	rc_sq_wqe->va = cpu_to_le64(wr->mr->iova);

	fseg->pbl_size = cpu_to_le32(mr->pbl_size);
	roce_set_field(fseg->mode_buf_pg_sz,
		       V2_RC_FRMR_WQE_BYTE_40_PBL_BUF_PG_SZ_M,
		       V2_RC_FRMR_WQE_BYTE_40_PBL_BUF_PG_SZ_S,
		       mr->pbl_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_bit(fseg->mode_buf_pg_sz,
		     V2_RC_FRMR_WQE_BYTE_40_BLK_MODE_S, 0);
}

static void set_atomic_seg(const struct ib_send_wr *wr, void *wqe,
			   struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			   int valid_num_sge)
{
	struct hns_roce_wqe_atomic_seg *aseg;

	set_data_seg_v2(wqe, wr->sg_list);
	aseg = wqe + sizeof(struct hns_roce_v2_wqe_data_seg);

	if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
		aseg->fetchadd_swap_data = cpu_to_le64(atomic_wr(wr)->swap);
		aseg->cmp_data = cpu_to_le64(atomic_wr(wr)->compare_add);
	} else {
		aseg->fetchadd_swap_data =
			cpu_to_le64(atomic_wr(wr)->compare_add);
		aseg->cmp_data  = 0;
	}

	roce_set_field(rc_sq_wqe->byte_16, V2_RC_SEND_WQE_BYTE_16_SGE_NUM_M,
		       V2_RC_SEND_WQE_BYTE_16_SGE_NUM_S, valid_num_sge);
}

static void set_extend_sge(struct hns_roce_qp *qp, const struct ib_send_wr *wr,
			   unsigned int *sge_ind, int valid_num_sge)
{
	struct hns_roce_v2_wqe_data_seg *dseg;
	struct ib_sge *sg;
	int num_in_wqe = 0;
	int extend_sge_num;
	int fi_sge_num;
	int se_sge_num;
	int shift;
	int i;

	if (qp->ibqp.qp_type == IB_QPT_RC || qp->ibqp.qp_type == IB_QPT_UC)
		num_in_wqe = HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE;
	extend_sge_num = valid_num_sge - num_in_wqe;
	sg = wr->sg_list + num_in_wqe;
	shift = qp->hr_buf.page_shift;

	/*
	 * Check whether wr->num_sge sges are in the same page. If not, we
	 * should calculate how many sges in the first page and the second
	 * page.
	 */
	dseg = hns_roce_get_extend_sge(qp, (*sge_ind) & (qp->sge.sge_cnt - 1));
	fi_sge_num = (round_up((uintptr_t)dseg, 1 << shift) -
		      (uintptr_t)dseg) /
		      sizeof(struct hns_roce_v2_wqe_data_seg);
	if (extend_sge_num > fi_sge_num) {
		se_sge_num = extend_sge_num - fi_sge_num;
		for (i = 0; i < fi_sge_num; i++) {
			set_data_seg_v2(dseg++, sg + i);
			(*sge_ind)++;
		}
		dseg = hns_roce_get_extend_sge(qp,
					   (*sge_ind) & (qp->sge.sge_cnt - 1));
		for (i = 0; i < se_sge_num; i++) {
			set_data_seg_v2(dseg++, sg + fi_sge_num + i);
			(*sge_ind)++;
		}
	} else {
		for (i = 0; i < extend_sge_num; i++) {
			set_data_seg_v2(dseg++, sg + i);
			(*sge_ind)++;
		}
	}
}

static int set_rwqe_data_seg(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			     struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			     void *wqe, unsigned int *sge_ind,
			     int valid_num_sge)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_v2_wqe_data_seg *dseg = wqe;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *qp = to_hr_qp(ibqp);
	int j = 0;
	int i;

	if (wr->send_flags & IB_SEND_INLINE && valid_num_sge) {
		if (le32_to_cpu(rc_sq_wqe->msg_len) >
		    hr_dev->caps.max_sq_inline) {
			ibdev_err(ibdev, "inline len(1-%d)=%d, illegal",
				  rc_sq_wqe->msg_len,
				  hr_dev->caps.max_sq_inline);
			return -EINVAL;
		}

		if (wr->opcode == IB_WR_RDMA_READ) {
			ibdev_err(ibdev, "Not support inline data!\n");
			return -EINVAL;
		}

		for (i = 0; i < wr->num_sge; i++) {
			memcpy(wqe, ((void *)wr->sg_list[i].addr),
			       wr->sg_list[i].length);
			wqe += wr->sg_list[i].length;
		}

		roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_INLINE_S,
			     1);
	} else {
		if (valid_num_sge <= HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE) {
			for (i = 0; i < wr->num_sge; i++) {
				if (likely(wr->sg_list[i].length)) {
					set_data_seg_v2(dseg, wr->sg_list + i);
					dseg++;
				}
			}
		} else {
			roce_set_field(rc_sq_wqe->byte_20,
				     V2_RC_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_M,
				     V2_RC_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_S,
				     (*sge_ind) & (qp->sge.sge_cnt - 1));

			for (i = 0; i < wr->num_sge &&
			     j < HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE; i++) {
				if (likely(wr->sg_list[i].length)) {
					set_data_seg_v2(dseg, wr->sg_list + i);
					dseg++;
					j++;
				}
			}

			set_extend_sge(qp, wr, sge_ind, valid_num_sge);
		}

		roce_set_field(rc_sq_wqe->byte_16,
			       V2_RC_SEND_WQE_BYTE_16_SGE_NUM_M,
			       V2_RC_SEND_WQE_BYTE_16_SGE_NUM_S, valid_num_sge);
	}

	return 0;
}

static int check_send_valid(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct ib_qp *ibqp = &hr_qp->ibqp;

	if (unlikely(ibqp->qp_type != IB_QPT_RC &&
		     ibqp->qp_type != IB_QPT_GSI &&
		     ibqp->qp_type != IB_QPT_UD)) {
		ibdev_err(ibdev, "Not supported QP(0x%x)type!\n",
			  ibqp->qp_type);
		return -EOPNOTSUPP;
	} else if (unlikely(hr_qp->state == IB_QPS_RESET ||
		   hr_qp->state == IB_QPS_INIT ||
		   hr_qp->state == IB_QPS_RTR)) {
		ibdev_err(ibdev, "failed to post WQE, QP state %d!\n",
			  hr_qp->state);
		return -EINVAL;
	} else if (unlikely(hr_dev->state >= HNS_ROCE_DEVICE_STATE_RST_DOWN)) {
		ibdev_err(ibdev, "failed to post WQE, dev state %d!\n",
			  hr_dev->state);
		return -EIO;
	}

	return 0;
}

static inline int calc_wr_sge_num(const struct ib_send_wr *wr, u32 *sge_len)
{
	int valid_num = 0;
	u32 len = 0;
	int i;

	for (i = 0; i < wr->num_sge; i++) {
		if (likely(wr->sg_list[i].length)) {
			len += wr->sg_list[i].length;
			valid_num++;
		}
	}

	*sge_len = len;
	return valid_num;
}

static inline int set_ud_wqe(struct hns_roce_qp *qp,
			     const struct ib_send_wr *wr,
			     void *wqe, unsigned int *sge_idx,
			     unsigned int owner_bit)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(qp->ibqp.device);
	struct hns_roce_ah *ah = to_hr_ah(ud_wr(wr)->ah);
	struct hns_roce_v2_ud_send_wqe *ud_sq_wqe = wqe;
	unsigned int curr_idx = *sge_idx;
	int valid_num_sge;
	u32 msg_len = 0;
	bool loopback;
	u8 *smac;

	valid_num_sge = calc_wr_sge_num(wr, &msg_len);
	memset(ud_sq_wqe, 0, sizeof(*ud_sq_wqe));

	roce_set_field(ud_sq_wqe->dmac, V2_UD_SEND_WQE_DMAC_0_M,
		       V2_UD_SEND_WQE_DMAC_0_S, ah->av.mac[0]);
	roce_set_field(ud_sq_wqe->dmac, V2_UD_SEND_WQE_DMAC_1_M,
		       V2_UD_SEND_WQE_DMAC_1_S, ah->av.mac[1]);
	roce_set_field(ud_sq_wqe->dmac, V2_UD_SEND_WQE_DMAC_2_M,
		       V2_UD_SEND_WQE_DMAC_2_S, ah->av.mac[2]);
	roce_set_field(ud_sq_wqe->dmac, V2_UD_SEND_WQE_DMAC_3_M,
		       V2_UD_SEND_WQE_DMAC_3_S, ah->av.mac[3]);
	roce_set_field(ud_sq_wqe->byte_48, V2_UD_SEND_WQE_BYTE_48_DMAC_4_M,
		       V2_UD_SEND_WQE_BYTE_48_DMAC_4_S, ah->av.mac[4]);
	roce_set_field(ud_sq_wqe->byte_48, V2_UD_SEND_WQE_BYTE_48_DMAC_5_M,
		       V2_UD_SEND_WQE_BYTE_48_DMAC_5_S, ah->av.mac[5]);

	/* MAC loopback */
	smac = (u8 *)hr_dev->dev_addr[qp->port];
	loopback = ether_addr_equal_unaligned(ah->av.mac, smac) ? 1 : 0;

	roce_set_bit(ud_sq_wqe->byte_40,
		     V2_UD_SEND_WQE_BYTE_40_LBI_S, loopback);

	roce_set_field(ud_sq_wqe->byte_4,
		       V2_UD_SEND_WQE_BYTE_4_OPCODE_M,
		       V2_UD_SEND_WQE_BYTE_4_OPCODE_S,
		       HNS_ROCE_V2_WQE_OP_SEND);

	ud_sq_wqe->msg_len = cpu_to_le32(msg_len);

	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		ud_sq_wqe->immtdata = cpu_to_le32(be32_to_cpu(wr->ex.imm_data));
		break;
	default:
		ud_sq_wqe->immtdata = 0;
		break;
	}

	/* Set sig attr */
	roce_set_bit(ud_sq_wqe->byte_4, V2_UD_SEND_WQE_BYTE_4_CQE_S,
		     (wr->send_flags & IB_SEND_SIGNALED) ? 1 : 0);

	/* Set se attr */
	roce_set_bit(ud_sq_wqe->byte_4, V2_UD_SEND_WQE_BYTE_4_SE_S,
		     (wr->send_flags & IB_SEND_SOLICITED) ? 1 : 0);

	roce_set_bit(ud_sq_wqe->byte_4, V2_UD_SEND_WQE_BYTE_4_OWNER_S,
		     owner_bit);

	roce_set_field(ud_sq_wqe->byte_16, V2_UD_SEND_WQE_BYTE_16_PD_M,
		       V2_UD_SEND_WQE_BYTE_16_PD_S, to_hr_pd(qp->ibqp.pd)->pdn);

	roce_set_field(ud_sq_wqe->byte_16, V2_UD_SEND_WQE_BYTE_16_SGE_NUM_M,
		       V2_UD_SEND_WQE_BYTE_16_SGE_NUM_S, valid_num_sge);

	roce_set_field(ud_sq_wqe->byte_20,
		       V2_UD_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_M,
		       V2_UD_SEND_WQE_BYTE_20_MSG_START_SGE_IDX_S,
		       curr_idx & (qp->sge.sge_cnt - 1));

	roce_set_field(ud_sq_wqe->byte_24, V2_UD_SEND_WQE_BYTE_24_UDPSPN_M,
		       V2_UD_SEND_WQE_BYTE_24_UDPSPN_S, 0);
	ud_sq_wqe->qkey = cpu_to_le32(ud_wr(wr)->remote_qkey & 0x80000000 ?
			  qp->qkey : ud_wr(wr)->remote_qkey);
	roce_set_field(ud_sq_wqe->byte_32, V2_UD_SEND_WQE_BYTE_32_DQPN_M,
		       V2_UD_SEND_WQE_BYTE_32_DQPN_S, ud_wr(wr)->remote_qpn);

	roce_set_field(ud_sq_wqe->byte_36, V2_UD_SEND_WQE_BYTE_36_VLAN_M,
		       V2_UD_SEND_WQE_BYTE_36_VLAN_S, ah->av.vlan_id);
	roce_set_field(ud_sq_wqe->byte_36, V2_UD_SEND_WQE_BYTE_36_HOPLIMIT_M,
		       V2_UD_SEND_WQE_BYTE_36_HOPLIMIT_S, ah->av.hop_limit);
	roce_set_field(ud_sq_wqe->byte_36, V2_UD_SEND_WQE_BYTE_36_TCLASS_M,
		       V2_UD_SEND_WQE_BYTE_36_TCLASS_S, ah->av.tclass);
	roce_set_field(ud_sq_wqe->byte_40, V2_UD_SEND_WQE_BYTE_40_FLOW_LABEL_M,
		       V2_UD_SEND_WQE_BYTE_40_FLOW_LABEL_S, ah->av.flowlabel);
	roce_set_field(ud_sq_wqe->byte_40, V2_UD_SEND_WQE_BYTE_40_SL_M,
		       V2_UD_SEND_WQE_BYTE_40_SL_S, ah->av.sl);
	roce_set_field(ud_sq_wqe->byte_40, V2_UD_SEND_WQE_BYTE_40_PORTN_M,
		       V2_UD_SEND_WQE_BYTE_40_PORTN_S, qp->port);

	roce_set_bit(ud_sq_wqe->byte_40, V2_UD_SEND_WQE_BYTE_40_UD_VLAN_EN_S,
		     ah->av.vlan_en ? 1 : 0);
	roce_set_field(ud_sq_wqe->byte_48, V2_UD_SEND_WQE_BYTE_48_SGID_INDX_M,
		       V2_UD_SEND_WQE_BYTE_48_SGID_INDX_S, ah->av.gid_index);

	memcpy(&ud_sq_wqe->dgid[0], &ah->av.dgid[0], GID_LEN_V2);

	set_extend_sge(qp, wr, &curr_idx, valid_num_sge);

	*sge_idx = curr_idx;

	return 0;
}

static inline int set_rc_wqe(struct hns_roce_qp *qp,
			     const struct ib_send_wr *wr,
			     void *wqe, unsigned int *sge_idx,
			     unsigned int owner_bit)
{
	struct hns_roce_v2_rc_send_wqe *rc_sq_wqe = wqe;
	unsigned int curr_idx = *sge_idx;
	int valid_num_sge;
	u32 msg_len = 0;
	int ret = 0;

	valid_num_sge = calc_wr_sge_num(wr, &msg_len);
	memset(rc_sq_wqe, 0, sizeof(*rc_sq_wqe));

	rc_sq_wqe->msg_len = cpu_to_le32(msg_len);

	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		rc_sq_wqe->immtdata = cpu_to_le32(be32_to_cpu(wr->ex.imm_data));
		break;
	case IB_WR_SEND_WITH_INV:
		rc_sq_wqe->inv_key = cpu_to_le32(wr->ex.invalidate_rkey);
		break;
	default:
		rc_sq_wqe->immtdata = 0;
		break;
	}

	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_FENCE_S,
		     (wr->send_flags & IB_SEND_FENCE) ? 1 : 0);

	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_SE_S,
		     (wr->send_flags & IB_SEND_SOLICITED) ? 1 : 0);

	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_CQE_S,
		     (wr->send_flags & IB_SEND_SIGNALED) ? 1 : 0);

	roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_OWNER_S,
		     owner_bit);

	wqe += sizeof(struct hns_roce_v2_rc_send_wqe);
	switch (wr->opcode) {
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		rc_sq_wqe->rkey = cpu_to_le32(rdma_wr(wr)->rkey);
		rc_sq_wqe->va = cpu_to_le64(rdma_wr(wr)->remote_addr);
		break;
	case IB_WR_LOCAL_INV:
		roce_set_bit(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_SO_S, 1);
		rc_sq_wqe->inv_key = cpu_to_le32(wr->ex.invalidate_rkey);
		break;
	case IB_WR_REG_MR:
		set_frmr_seg(rc_sq_wqe, wqe, reg_wr(wr));
		break;
	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		rc_sq_wqe->rkey = cpu_to_le32(atomic_wr(wr)->rkey);
		rc_sq_wqe->va = cpu_to_le64(atomic_wr(wr)->remote_addr);
		break;
	default:
		break;
	}

	roce_set_field(rc_sq_wqe->byte_4, V2_RC_SEND_WQE_BYTE_4_OPCODE_M,
		       V2_RC_SEND_WQE_BYTE_4_OPCODE_S,
		       to_hr_opcode(wr->opcode));

	if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
	    wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
		set_atomic_seg(wr, wqe, rc_sq_wqe, valid_num_sge);
	else if (wr->opcode != IB_WR_REG_MR)
		ret = set_rwqe_data_seg(&qp->ibqp, wr, rc_sq_wqe,
					wqe, &curr_idx, valid_num_sge);

	*sge_idx = curr_idx;

	return ret;
}

static inline void update_sq_db(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *qp)
{
	/*
	 * Hip08 hardware cannot flush the WQEs in SQ if the QP state
	 * gets into errored mode. Hence, as a workaround to this
	 * hardware limitation, driver needs to assist in flushing. But
	 * the flushing operation uses mailbox to convey the QP state to
	 * the hardware and which can sleep due to the mutex protection
	 * around the mailbox calls. Hence, use the deferred flush for
	 * now.
	 */
	if (qp->state == IB_QPS_ERR) {
		if (!test_and_set_bit(HNS_ROCE_FLUSH_FLAG, &qp->flush_flag))
			init_flush_work(hr_dev, qp);
	} else {
		struct hns_roce_v2_db sq_db = {};

		roce_set_field(sq_db.byte_4, V2_DB_BYTE_4_TAG_M,
			       V2_DB_BYTE_4_TAG_S, qp->doorbell_qpn);
		roce_set_field(sq_db.byte_4, V2_DB_BYTE_4_CMD_M,
			       V2_DB_BYTE_4_CMD_S, HNS_ROCE_V2_SQ_DB);
		roce_set_field(sq_db.parameter, V2_DB_PARAMETER_IDX_M,
			       V2_DB_PARAMETER_IDX_S,
			       qp->sq.head & ((qp->sq.wqe_cnt << 1) - 1));
		roce_set_field(sq_db.parameter, V2_DB_PARAMETER_SL_M,
			       V2_DB_PARAMETER_SL_S, qp->sl);

		hns_roce_write64(hr_dev, (__le32 *)&sq_db, qp->sq.db_reg_l);
	}
}

static int hns_roce_v2_post_send(struct ib_qp *ibqp,
				 const struct ib_send_wr *wr,
				 const struct ib_send_wr **bad_wr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *qp = to_hr_qp(ibqp);
	unsigned long flags = 0;
	unsigned int owner_bit;
	unsigned int sge_idx;
	unsigned int wqe_idx;
	void *wqe = NULL;
	int nreq;
	int ret;

	spin_lock_irqsave(&qp->sq.lock, flags);

	ret = check_send_valid(hr_dev, qp);
	if (ret) {
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	sge_idx = qp->next_sge;

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (hns_roce_wq_overflow(&qp->sq, nreq, qp->ibqp.send_cq)) {
			ret = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		wqe_idx = (qp->sq.head + nreq) & (qp->sq.wqe_cnt - 1);

		if (unlikely(wr->num_sge > qp->sq.max_gs)) {
			ibdev_err(ibdev, "num_sge=%d > qp->sq.max_gs=%d\n",
				  wr->num_sge, qp->sq.max_gs);
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		wqe = hns_roce_get_send_wqe(qp, wqe_idx);
		qp->sq.wrid[wqe_idx] = wr->wr_id;
		owner_bit =
		       ~(((qp->sq.head + nreq) >> ilog2(qp->sq.wqe_cnt)) & 0x1);

		/* Corresponding to the QP type, wqe process separately */
		if (ibqp->qp_type == IB_QPT_GSI)
			ret = set_ud_wqe(qp, wr, wqe, &sge_idx, owner_bit);
		else if (ibqp->qp_type == IB_QPT_RC)
			ret = set_rc_wqe(qp, wr, wqe, &sge_idx, owner_bit);

		if (ret) {
			*bad_wr = wr;
			goto out;
		}
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;
		qp->next_sge = sge_idx;
		/* Memory barrier */
		wmb();
		update_sq_db(hr_dev, qp);
	}

	spin_unlock_irqrestore(&qp->sq.lock, flags);

	return ret;
}

static int check_recv_valid(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp)
{
	if (unlikely(hr_dev->state >= HNS_ROCE_DEVICE_STATE_RST_DOWN))
		return -EIO;
	else if (hr_qp->state == IB_QPS_RESET)
		return -EINVAL;

	return 0;
}

static int hns_roce_v2_post_recv(struct ib_qp *ibqp,
				 const struct ib_recv_wr *wr,
				 const struct ib_recv_wr **bad_wr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_v2_wqe_data_seg *dseg;
	struct hns_roce_rinl_sge *sge_list;
	unsigned long flags;
	void *wqe = NULL;
	u32 wqe_idx;
	int nreq;
	int ret;
	int i;

	spin_lock_irqsave(&hr_qp->rq.lock, flags);

	ret = check_recv_valid(hr_dev, hr_qp);
	if (ret) {
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (hns_roce_wq_overflow(&hr_qp->rq, nreq,
			hr_qp->ibqp.recv_cq)) {
			ret = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		wqe_idx = (hr_qp->rq.head + nreq) & (hr_qp->rq.wqe_cnt - 1);

		if (unlikely(wr->num_sge > hr_qp->rq.max_gs)) {
			ibdev_err(ibdev, "rq:num_sge=%d >= qp->sq.max_gs=%d\n",
				  wr->num_sge, hr_qp->rq.max_gs);
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		wqe = hns_roce_get_recv_wqe(hr_qp, wqe_idx);
		dseg = (struct hns_roce_v2_wqe_data_seg *)wqe;
		for (i = 0; i < wr->num_sge; i++) {
			if (!wr->sg_list[i].length)
				continue;
			set_data_seg_v2(dseg, wr->sg_list + i);
			dseg++;
		}

		if (i < hr_qp->rq.max_gs) {
			dseg->lkey = cpu_to_le32(HNS_ROCE_INVALID_LKEY);
			dseg->addr = 0;
		}

		/* rq support inline data */
		if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE) {
			sge_list = hr_qp->rq_inl_buf.wqe_list[wqe_idx].sg_list;
			hr_qp->rq_inl_buf.wqe_list[wqe_idx].sge_cnt =
							       (u32)wr->num_sge;
			for (i = 0; i < wr->num_sge; i++) {
				sge_list[i].addr =
					       (void *)(u64)wr->sg_list[i].addr;
				sge_list[i].len = wr->sg_list[i].length;
			}
		}

		hr_qp->rq.wrid[wqe_idx] = wr->wr_id;
	}

out:
	if (likely(nreq)) {
		hr_qp->rq.head += nreq;
		/* Memory barrier */
		wmb();

		/*
		 * Hip08 hardware cannot flush the WQEs in RQ if the QP state
		 * gets into errored mode. Hence, as a workaround to this
		 * hardware limitation, driver needs to assist in flushing. But
		 * the flushing operation uses mailbox to convey the QP state to
		 * the hardware and which can sleep due to the mutex protection
		 * around the mailbox calls. Hence, use the deferred flush for
		 * now.
		 */
		if (hr_qp->state == IB_QPS_ERR) {
			if (!test_and_set_bit(HNS_ROCE_FLUSH_FLAG,
					      &hr_qp->flush_flag))
				init_flush_work(hr_dev, hr_qp);
		} else {
			*hr_qp->rdb.db_record = hr_qp->rq.head & 0xffff;
		}
	}
	spin_unlock_irqrestore(&hr_qp->rq.lock, flags);

	return ret;
}

static int hns_roce_v2_cmd_hw_reseted(struct hns_roce_dev *hr_dev,
				      unsigned long instance_stage,
				      unsigned long reset_stage)
{
	/* When hardware reset has been completed once or more, we should stop
	 * sending mailbox&cmq&doorbell to hardware. If now in .init_instance()
	 * function, we should exit with error. If now at HNAE3_INIT_CLIENT
	 * stage of soft reset process, we should exit with error, and then
	 * HNAE3_INIT_CLIENT related process can rollback the operation like
	 * notifing hardware to free resources, HNAE3_INIT_CLIENT related
	 * process will exit with error to notify NIC driver to reschedule soft
	 * reset process once again.
	 */
	hr_dev->is_reset = true;
	hr_dev->dis_db = true;

	if (reset_stage == HNS_ROCE_STATE_RST_INIT ||
	    instance_stage == HNS_ROCE_STATE_INIT)
		return CMD_RST_PRC_EBUSY;

	return CMD_RST_PRC_SUCCESS;
}

static int hns_roce_v2_cmd_hw_resetting(struct hns_roce_dev *hr_dev,
					unsigned long instance_stage,
					unsigned long reset_stage)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	/* When hardware reset is detected, we should stop sending mailbox&cmq&
	 * doorbell to hardware. If now in .init_instance() function, we should
	 * exit with error. If now at HNAE3_INIT_CLIENT stage of soft reset
	 * process, we should exit with error, and then HNAE3_INIT_CLIENT
	 * related process can rollback the operation like notifing hardware to
	 * free resources, HNAE3_INIT_CLIENT related process will exit with
	 * error to notify NIC driver to reschedule soft reset process once
	 * again.
	 */
	hr_dev->dis_db = true;
	if (!ops->get_hw_reset_stat(handle))
		hr_dev->is_reset = true;

	if (!hr_dev->is_reset || reset_stage == HNS_ROCE_STATE_RST_INIT ||
	    instance_stage == HNS_ROCE_STATE_INIT)
		return CMD_RST_PRC_EBUSY;

	return CMD_RST_PRC_SUCCESS;
}

static int hns_roce_v2_cmd_sw_resetting(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	/* When software reset is detected at .init_instance() function, we
	 * should stop sending mailbox&cmq&doorbell to hardware, and exit
	 * with error.
	 */
	hr_dev->dis_db = true;
	if (ops->ae_dev_reset_cnt(handle) != hr_dev->reset_cnt)
		hr_dev->is_reset = true;

	return CMD_RST_PRC_EBUSY;
}

static int hns_roce_v2_rst_process_cmd(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long instance_stage;	/* the current instance stage */
	unsigned long reset_stage;	/* the current reset stage */
	unsigned long reset_cnt;
	bool sw_resetting;
	bool hw_resetting;

	if (hr_dev->is_reset)
		return CMD_RST_PRC_SUCCESS;

	/* Get information about reset from NIC driver or RoCE driver itself,
	 * the meaning of the following variables from NIC driver are described
	 * as below:
	 * reset_cnt -- The count value of completed hardware reset.
	 * hw_resetting -- Whether hardware device is resetting now.
	 * sw_resetting -- Whether NIC's software reset process is running now.
	 */
	instance_stage = handle->rinfo.instance_state;
	reset_stage = handle->rinfo.reset_state;
	reset_cnt = ops->ae_dev_reset_cnt(handle);
	hw_resetting = ops->get_hw_reset_stat(handle);
	sw_resetting = ops->ae_dev_resetting(handle);

	if (reset_cnt != hr_dev->reset_cnt)
		return hns_roce_v2_cmd_hw_reseted(hr_dev, instance_stage,
						  reset_stage);
	else if (hw_resetting)
		return hns_roce_v2_cmd_hw_resetting(hr_dev, instance_stage,
						    reset_stage);
	else if (sw_resetting && instance_stage == HNS_ROCE_STATE_INIT)
		return hns_roce_v2_cmd_sw_resetting(hr_dev);

	return 0;
}

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

	ring->desc_dma_addr = 0;
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
			   ring->desc_num >> HNS_ROCE_CMQ_DESC_NUM_S);
		roce_write(hr_dev, ROCEE_TX_CMQ_HEAD_REG, 0);
		roce_write(hr_dev, ROCEE_TX_CMQ_TAIL_REG, 0);
	} else {
		roce_write(hr_dev, ROCEE_RX_CMQ_BASEADDR_L_REG, (u32)dma);
		roce_write(hr_dev, ROCEE_RX_CMQ_BASEADDR_H_REG,
			   upper_32_bits(dma));
		roce_write(hr_dev, ROCEE_RX_CMQ_DEPTH_REG,
			   ring->desc_num >> HNS_ROCE_CMQ_DESC_NUM_S);
		roce_write(hr_dev, ROCEE_RX_CMQ_HEAD_REG, 0);
		roce_write(hr_dev, ROCEE_RX_CMQ_TAIL_REG, 0);
	}
}

static int hns_roce_v2_cmq_init(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	int ret;

	/* Setup the queue entries for command queue */
	priv->cmq.csq.desc_num = CMD_CSQ_DESC_NUM;
	priv->cmq.crq.desc_num = CMD_CRQ_DESC_NUM;

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

static void hns_roce_cmq_setup_basic_desc(struct hns_roce_cmq_desc *desc,
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

static int __hns_roce_cmq_send(struct hns_roce_dev *hr_dev,
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
	if (le16_to_cpu(desc->flag) & HNS_ROCE_CMD_FLAG_NO_INTR) {
		do {
			if (hns_roce_cmq_csq_done(hr_dev))
				break;
			udelay(1);
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
			desc_ret = le16_to_cpu(desc[handle].retval);
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

static int hns_roce_cmq_send(struct hns_roce_dev *hr_dev,
			     struct hns_roce_cmq_desc *desc, int num)
{
	int retval;
	int ret;

	ret = hns_roce_v2_rst_process_cmd(hr_dev);
	if (ret == CMD_RST_PRC_SUCCESS)
		return 0;
	if (ret == CMD_RST_PRC_EBUSY)
		return -EBUSY;

	ret = __hns_roce_cmq_send(hr_dev, desc, num);
	if (ret) {
		retval = hns_roce_v2_rst_process_cmd(hr_dev);
		if (retval == CMD_RST_PRC_SUCCESS)
			return 0;
		else if (retval == CMD_RST_PRC_EBUSY)
			return -EBUSY;
	}

	return ret;
}

static int hns_roce_cmq_query_hw_info(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_query_version *resp;
	struct hns_roce_cmq_desc desc;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_HW_VER, true);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	resp = (struct hns_roce_query_version *)desc.data;
	hr_dev->hw_rev = le16_to_cpu(resp->rocee_hw_version);
	hr_dev->vendor_id = hr_dev->pci_dev->vendor;

	return 0;
}

static bool hns_roce_func_clr_chk_rst(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long reset_cnt;
	bool sw_resetting;
	bool hw_resetting;

	reset_cnt = ops->ae_dev_reset_cnt(handle);
	hw_resetting = ops->get_hw_reset_stat(handle);
	sw_resetting = ops->ae_dev_resetting(handle);

	if (reset_cnt != hr_dev->reset_cnt || hw_resetting || sw_resetting)
		return true;

	return false;
}

static void hns_roce_func_clr_rst_prc(struct hns_roce_dev *hr_dev, int retval,
				      int flag)
{
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long instance_stage;
	unsigned long reset_cnt;
	unsigned long end;
	bool sw_resetting;
	bool hw_resetting;

	instance_stage = handle->rinfo.instance_state;
	reset_cnt = ops->ae_dev_reset_cnt(handle);
	hw_resetting = ops->get_hw_reset_stat(handle);
	sw_resetting = ops->ae_dev_resetting(handle);

	if (reset_cnt != hr_dev->reset_cnt) {
		hr_dev->dis_db = true;
		hr_dev->is_reset = true;
		dev_info(hr_dev->dev, "Func clear success after reset.\n");
	} else if (hw_resetting) {
		hr_dev->dis_db = true;

		dev_warn(hr_dev->dev,
			 "Func clear is pending, device in resetting state.\n");
		end = HNS_ROCE_V2_HW_RST_TIMEOUT;
		while (end) {
			if (!ops->get_hw_reset_stat(handle)) {
				hr_dev->is_reset = true;
				dev_info(hr_dev->dev,
					 "Func clear success after reset.\n");
				return;
			}
			msleep(HNS_ROCE_V2_HW_RST_COMPLETION_WAIT);
			end -= HNS_ROCE_V2_HW_RST_COMPLETION_WAIT;
		}

		dev_warn(hr_dev->dev, "Func clear failed.\n");
	} else if (sw_resetting && instance_stage == HNS_ROCE_STATE_INIT) {
		hr_dev->dis_db = true;

		dev_warn(hr_dev->dev,
			 "Func clear is pending, device in resetting state.\n");
		end = HNS_ROCE_V2_HW_RST_TIMEOUT;
		while (end) {
			if (ops->ae_dev_reset_cnt(handle) !=
			    hr_dev->reset_cnt) {
				hr_dev->is_reset = true;
				dev_info(hr_dev->dev,
					 "Func clear success after sw reset\n");
				return;
			}
			msleep(HNS_ROCE_V2_HW_RST_COMPLETION_WAIT);
			end -= HNS_ROCE_V2_HW_RST_COMPLETION_WAIT;
		}

		dev_warn(hr_dev->dev, "Func clear failed because of unfinished sw reset\n");
	} else {
		if (retval && !flag)
			dev_warn(hr_dev->dev,
				 "Func clear read failed, ret = %d.\n", retval);

		dev_warn(hr_dev->dev, "Func clear failed.\n");
	}
}
static void hns_roce_function_clear(struct hns_roce_dev *hr_dev)
{
	bool fclr_write_fail_flag = false;
	struct hns_roce_func_clear *resp;
	struct hns_roce_cmq_desc desc;
	unsigned long end;
	int ret = 0;

	if (hns_roce_func_clr_chk_rst(hr_dev))
		goto out;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_FUNC_CLEAR, false);
	resp = (struct hns_roce_func_clear *)desc.data;

	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		fclr_write_fail_flag = true;
		dev_err(hr_dev->dev, "Func clear write failed, ret = %d.\n",
			 ret);
		goto out;
	}

	msleep(HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_INTERVAL);
	end = HNS_ROCE_V2_FUNC_CLEAR_TIMEOUT_MSECS;
	while (end) {
		if (hns_roce_func_clr_chk_rst(hr_dev))
			goto out;
		msleep(HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_FAIL_WAIT);
		end -= HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_FAIL_WAIT;

		hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_FUNC_CLEAR,
					      true);

		ret = hns_roce_cmq_send(hr_dev, &desc, 1);
		if (ret)
			continue;

		if (roce_get_bit(resp->func_done, FUNC_CLEAR_RST_FUN_DONE_S)) {
			hr_dev->is_reset = true;
			return;
		}
	}

out:
	hns_roce_func_clr_rst_prc(hr_dev, ret, fclr_write_fail_flag);
}

static int hns_roce_query_fw_ver(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_query_fw_info *resp;
	struct hns_roce_cmq_desc desc;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_QUERY_FW_VER, true);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	resp = (struct hns_roce_query_fw_info *)desc.data;
	hr_dev->caps.fw_ver = (u64)(le32_to_cpu(resp->fw_ver));

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
	struct hns_roce_pf_res_a *req_a;
	struct hns_roce_pf_res_b *req_b;
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

	req_a = (struct hns_roce_pf_res_a *)desc[0].data;
	req_b = (struct hns_roce_pf_res_b *)desc[1].data;

	hr_dev->caps.qpc_bt_num = roce_get_field(req_a->qpc_bt_idx_num,
						 PF_RES_DATA_1_PF_QPC_BT_NUM_M,
						 PF_RES_DATA_1_PF_QPC_BT_NUM_S);
	hr_dev->caps.srqc_bt_num = roce_get_field(req_a->srqc_bt_idx_num,
						PF_RES_DATA_2_PF_SRQC_BT_NUM_M,
						PF_RES_DATA_2_PF_SRQC_BT_NUM_S);
	hr_dev->caps.cqc_bt_num = roce_get_field(req_a->cqc_bt_idx_num,
						 PF_RES_DATA_3_PF_CQC_BT_NUM_M,
						 PF_RES_DATA_3_PF_CQC_BT_NUM_S);
	hr_dev->caps.mpt_bt_num = roce_get_field(req_a->mpt_bt_idx_num,
						 PF_RES_DATA_4_PF_MPT_BT_NUM_M,
						 PF_RES_DATA_4_PF_MPT_BT_NUM_S);

	hr_dev->caps.sl_num = roce_get_field(req_b->qid_idx_sl_num,
					     PF_RES_DATA_3_PF_SL_NUM_M,
					     PF_RES_DATA_3_PF_SL_NUM_S);
	hr_dev->caps.sccc_bt_num = roce_get_field(req_b->sccc_bt_idx_num,
					     PF_RES_DATA_4_PF_SCCC_BT_NUM_M,
					     PF_RES_DATA_4_PF_SCCC_BT_NUM_S);

	return 0;
}

static int hns_roce_query_pf_timer_resource(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_pf_timer_res_a *req_a;
	struct hns_roce_cmq_desc desc[2];
	int ret, i;

	for (i = 0; i < 2; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i],
					      HNS_ROCE_OPC_QUERY_PF_TIMER_RES,
					      true);

		if (i == 0)
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	}

	ret = hns_roce_cmq_send(hr_dev, desc, 2);
	if (ret)
		return ret;

	req_a = (struct hns_roce_pf_timer_res_a *)desc[0].data;

	hr_dev->caps.qpc_timer_bt_num =
				roce_get_field(req_a->qpc_timer_bt_idx_num,
					PF_RES_DATA_1_PF_QPC_TIMER_BT_NUM_M,
					PF_RES_DATA_1_PF_QPC_TIMER_BT_NUM_S);
	hr_dev->caps.cqc_timer_bt_num =
				roce_get_field(req_a->cqc_timer_bt_idx_num,
					PF_RES_DATA_2_PF_CQC_TIMER_BT_NUM_M,
					PF_RES_DATA_2_PF_CQC_TIMER_BT_NUM_S);

	return 0;
}

static int hns_roce_set_vf_switch_param(struct hns_roce_dev *hr_dev, int vf_id)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_vf_switch *swt;
	int ret;

	swt = (struct hns_roce_vf_switch *)desc.data;
	hns_roce_cmq_setup_basic_desc(&desc, HNS_SWITCH_PARAMETER_CFG, true);
	swt->rocee_sel |= cpu_to_le32(HNS_ICL_SWITCH_CMD_ROCEE_SEL);
	roce_set_field(swt->fun_id, VF_SWITCH_DATA_FUN_ID_VF_ID_M,
		       VF_SWITCH_DATA_FUN_ID_VF_ID_S, vf_id);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	desc.flag =
		cpu_to_le16(HNS_ROCE_CMD_FLAG_NO_INTR | HNS_ROCE_CMD_FLAG_IN);
	desc.flag &= cpu_to_le16(~HNS_ROCE_CMD_FLAG_WR);
	roce_set_bit(swt->cfg, VF_SWITCH_DATA_CFG_ALW_LPBK_S, 1);
	roce_set_bit(swt->cfg, VF_SWITCH_DATA_CFG_ALW_LCL_LPBK_S, 0);
	roce_set_bit(swt->cfg, VF_SWITCH_DATA_CFG_ALW_DST_OVRD_S, 1);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
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
	}

	roce_set_field(req_a->vf_qpc_bt_idx_num,
		       VF_RES_A_DATA_1_VF_QPC_BT_IDX_M,
		       VF_RES_A_DATA_1_VF_QPC_BT_IDX_S, 0);
	roce_set_field(req_a->vf_qpc_bt_idx_num,
		       VF_RES_A_DATA_1_VF_QPC_BT_NUM_M,
		       VF_RES_A_DATA_1_VF_QPC_BT_NUM_S, HNS_ROCE_VF_QPC_BT_NUM);

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
		       VF_RES_A_DATA_3_VF_CQC_BT_NUM_S, HNS_ROCE_VF_CQC_BT_NUM);

	roce_set_field(req_a->vf_mpt_bt_idx_num,
		       VF_RES_A_DATA_4_VF_MPT_BT_IDX_M,
		       VF_RES_A_DATA_4_VF_MPT_BT_IDX_S, 0);
	roce_set_field(req_a->vf_mpt_bt_idx_num,
		       VF_RES_A_DATA_4_VF_MPT_BT_NUM_M,
		       VF_RES_A_DATA_4_VF_MPT_BT_NUM_S, HNS_ROCE_VF_MPT_BT_NUM);

	roce_set_field(req_a->vf_eqc_bt_idx_num, VF_RES_A_DATA_5_VF_EQC_IDX_M,
		       VF_RES_A_DATA_5_VF_EQC_IDX_S, 0);
	roce_set_field(req_a->vf_eqc_bt_idx_num, VF_RES_A_DATA_5_VF_EQC_NUM_M,
		       VF_RES_A_DATA_5_VF_EQC_NUM_S, HNS_ROCE_VF_EQC_NUM);

	roce_set_field(req_b->vf_smac_idx_num, VF_RES_B_DATA_1_VF_SMAC_IDX_M,
		       VF_RES_B_DATA_1_VF_SMAC_IDX_S, 0);
	roce_set_field(req_b->vf_smac_idx_num, VF_RES_B_DATA_1_VF_SMAC_NUM_M,
		       VF_RES_B_DATA_1_VF_SMAC_NUM_S, HNS_ROCE_VF_SMAC_NUM);

	roce_set_field(req_b->vf_sgid_idx_num, VF_RES_B_DATA_2_VF_SGID_IDX_M,
		       VF_RES_B_DATA_2_VF_SGID_IDX_S, 0);
	roce_set_field(req_b->vf_sgid_idx_num, VF_RES_B_DATA_2_VF_SGID_NUM_M,
		       VF_RES_B_DATA_2_VF_SGID_NUM_S, HNS_ROCE_VF_SGID_NUM);

	roce_set_field(req_b->vf_qid_idx_sl_num, VF_RES_B_DATA_3_VF_QID_IDX_M,
		       VF_RES_B_DATA_3_VF_QID_IDX_S, 0);
	roce_set_field(req_b->vf_qid_idx_sl_num, VF_RES_B_DATA_3_VF_SL_NUM_M,
		       VF_RES_B_DATA_3_VF_SL_NUM_S, HNS_ROCE_VF_SL_NUM);

	roce_set_field(req_b->vf_sccc_idx_num, VF_RES_B_DATA_4_VF_SCCC_BT_IDX_M,
		       VF_RES_B_DATA_4_VF_SCCC_BT_IDX_S, 0);
	roce_set_field(req_b->vf_sccc_idx_num, VF_RES_B_DATA_4_VF_SCCC_BT_NUM_M,
		       VF_RES_B_DATA_4_VF_SCCC_BT_NUM_S,
		       HNS_ROCE_VF_SCCC_BT_NUM);

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static int hns_roce_v2_set_bt(struct hns_roce_dev *hr_dev)
{
	u8 srqc_hop_num = hr_dev->caps.srqc_hop_num;
	u8 qpc_hop_num = hr_dev->caps.qpc_hop_num;
	u8 cqc_hop_num = hr_dev->caps.cqc_hop_num;
	u8 mpt_hop_num = hr_dev->caps.mpt_hop_num;
	u8 sccc_hop_num = hr_dev->caps.sccc_hop_num;
	struct hns_roce_cfg_bt_attr *req;
	struct hns_roce_cmq_desc desc;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_BT_ATTR, false);
	req = (struct hns_roce_cfg_bt_attr *)desc.data;
	memset(req, 0, sizeof(*req));

	roce_set_field(req->vf_qpc_cfg, CFG_BT_ATTR_DATA_0_VF_QPC_BA_PGSZ_M,
		       CFG_BT_ATTR_DATA_0_VF_QPC_BA_PGSZ_S,
		       hr_dev->caps.qpc_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_qpc_cfg, CFG_BT_ATTR_DATA_0_VF_QPC_BUF_PGSZ_M,
		       CFG_BT_ATTR_DATA_0_VF_QPC_BUF_PGSZ_S,
		       hr_dev->caps.qpc_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_qpc_cfg, CFG_BT_ATTR_DATA_0_VF_QPC_HOPNUM_M,
		       CFG_BT_ATTR_DATA_0_VF_QPC_HOPNUM_S,
		       qpc_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 : qpc_hop_num);

	roce_set_field(req->vf_srqc_cfg, CFG_BT_ATTR_DATA_1_VF_SRQC_BA_PGSZ_M,
		       CFG_BT_ATTR_DATA_1_VF_SRQC_BA_PGSZ_S,
		       hr_dev->caps.srqc_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_srqc_cfg, CFG_BT_ATTR_DATA_1_VF_SRQC_BUF_PGSZ_M,
		       CFG_BT_ATTR_DATA_1_VF_SRQC_BUF_PGSZ_S,
		       hr_dev->caps.srqc_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_srqc_cfg, CFG_BT_ATTR_DATA_1_VF_SRQC_HOPNUM_M,
		       CFG_BT_ATTR_DATA_1_VF_SRQC_HOPNUM_S,
		       srqc_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 : srqc_hop_num);

	roce_set_field(req->vf_cqc_cfg, CFG_BT_ATTR_DATA_2_VF_CQC_BA_PGSZ_M,
		       CFG_BT_ATTR_DATA_2_VF_CQC_BA_PGSZ_S,
		       hr_dev->caps.cqc_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_cqc_cfg, CFG_BT_ATTR_DATA_2_VF_CQC_BUF_PGSZ_M,
		       CFG_BT_ATTR_DATA_2_VF_CQC_BUF_PGSZ_S,
		       hr_dev->caps.cqc_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_cqc_cfg, CFG_BT_ATTR_DATA_2_VF_CQC_HOPNUM_M,
		       CFG_BT_ATTR_DATA_2_VF_CQC_HOPNUM_S,
		       cqc_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 : cqc_hop_num);

	roce_set_field(req->vf_mpt_cfg, CFG_BT_ATTR_DATA_3_VF_MPT_BA_PGSZ_M,
		       CFG_BT_ATTR_DATA_3_VF_MPT_BA_PGSZ_S,
		       hr_dev->caps.mpt_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_mpt_cfg, CFG_BT_ATTR_DATA_3_VF_MPT_BUF_PGSZ_M,
		       CFG_BT_ATTR_DATA_3_VF_MPT_BUF_PGSZ_S,
		       hr_dev->caps.mpt_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_mpt_cfg, CFG_BT_ATTR_DATA_3_VF_MPT_HOPNUM_M,
		       CFG_BT_ATTR_DATA_3_VF_MPT_HOPNUM_S,
		       mpt_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 : mpt_hop_num);

	roce_set_field(req->vf_sccc_cfg,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_BA_PGSZ_M,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_BA_PGSZ_S,
		       hr_dev->caps.sccc_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_sccc_cfg,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_BUF_PGSZ_M,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_BUF_PGSZ_S,
		       hr_dev->caps.sccc_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(req->vf_sccc_cfg,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_HOPNUM_M,
		       CFG_BT_ATTR_DATA_4_VF_SCCC_HOPNUM_S,
		       sccc_hop_num ==
			      HNS_ROCE_HOP_NUM_0 ? 0 : sccc_hop_num);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static void set_default_caps(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;

	caps->num_qps		= HNS_ROCE_V2_MAX_QP_NUM;
	caps->max_wqes		= HNS_ROCE_V2_MAX_WQE_NUM;
	caps->num_cqs		= HNS_ROCE_V2_MAX_CQ_NUM;
	caps->num_srqs		= HNS_ROCE_V2_MAX_SRQ_NUM;
	caps->min_cqes		= HNS_ROCE_MIN_CQE_NUM;
	caps->max_cqes		= HNS_ROCE_V2_MAX_CQE_NUM;
	caps->max_sq_sg		= HNS_ROCE_V2_MAX_SQ_SGE_NUM;
	caps->max_extend_sg	= HNS_ROCE_V2_MAX_EXTEND_SGE_NUM;
	caps->max_rq_sg		= HNS_ROCE_V2_MAX_RQ_SGE_NUM;
	caps->max_sq_inline	= HNS_ROCE_V2_MAX_SQ_INLINE;
	caps->num_uars		= HNS_ROCE_V2_UAR_NUM;
	caps->phy_num_uars	= HNS_ROCE_V2_PHY_UAR_NUM;
	caps->num_aeq_vectors	= HNS_ROCE_V2_AEQE_VEC_NUM;
	caps->num_comp_vectors	= HNS_ROCE_V2_COMP_VEC_NUM;
	caps->num_other_vectors = HNS_ROCE_V2_ABNORMAL_VEC_NUM;
	caps->num_mtpts		= HNS_ROCE_V2_MAX_MTPT_NUM;
	caps->num_mtt_segs	= HNS_ROCE_V2_MAX_MTT_SEGS;
	caps->num_cqe_segs	= HNS_ROCE_V2_MAX_CQE_SEGS;
	caps->num_srqwqe_segs	= HNS_ROCE_V2_MAX_SRQWQE_SEGS;
	caps->num_idx_segs	= HNS_ROCE_V2_MAX_IDX_SEGS;
	caps->num_pds		= HNS_ROCE_V2_MAX_PD_NUM;
	caps->max_qp_init_rdma	= HNS_ROCE_V2_MAX_QP_INIT_RDMA;
	caps->max_qp_dest_rdma	= HNS_ROCE_V2_MAX_QP_DEST_RDMA;
	caps->max_sq_desc_sz	= HNS_ROCE_V2_MAX_SQ_DESC_SZ;
	caps->max_rq_desc_sz	= HNS_ROCE_V2_MAX_RQ_DESC_SZ;
	caps->max_srq_desc_sz	= HNS_ROCE_V2_MAX_SRQ_DESC_SZ;
	caps->qpc_entry_sz	= HNS_ROCE_V2_QPC_ENTRY_SZ;
	caps->irrl_entry_sz	= HNS_ROCE_V2_IRRL_ENTRY_SZ;
	caps->trrl_entry_sz	= HNS_ROCE_V2_EXT_ATOMIC_TRRL_ENTRY_SZ;
	caps->cqc_entry_sz	= HNS_ROCE_V2_CQC_ENTRY_SZ;
	caps->srqc_entry_sz	= HNS_ROCE_V2_SRQC_ENTRY_SZ;
	caps->mtpt_entry_sz	= HNS_ROCE_V2_MTPT_ENTRY_SZ;
	caps->mtt_entry_sz	= HNS_ROCE_V2_MTT_ENTRY_SZ;
	caps->idx_entry_sz	= HNS_ROCE_V2_IDX_ENTRY_SZ;
	caps->cq_entry_sz	= HNS_ROCE_V2_CQE_ENTRY_SIZE;
	caps->page_size_cap	= HNS_ROCE_V2_PAGE_SIZE_SUPPORTED;
	caps->reserved_lkey	= 0;
	caps->reserved_pds	= 0;
	caps->reserved_mrws	= 1;
	caps->reserved_uars	= 0;
	caps->reserved_cqs	= 0;
	caps->reserved_srqs	= 0;
	caps->reserved_qps	= HNS_ROCE_V2_RSV_QPS;

	caps->qpc_ba_pg_sz	= 0;
	caps->qpc_buf_pg_sz	= 0;
	caps->qpc_hop_num	= HNS_ROCE_CONTEXT_HOP_NUM;
	caps->srqc_ba_pg_sz	= 0;
	caps->srqc_buf_pg_sz	= 0;
	caps->srqc_hop_num	= HNS_ROCE_CONTEXT_HOP_NUM;
	caps->cqc_ba_pg_sz	= 0;
	caps->cqc_buf_pg_sz	= 0;
	caps->cqc_hop_num	= HNS_ROCE_CONTEXT_HOP_NUM;
	caps->mpt_ba_pg_sz	= 0;
	caps->mpt_buf_pg_sz	= 0;
	caps->mpt_hop_num	= HNS_ROCE_CONTEXT_HOP_NUM;
	caps->mtt_ba_pg_sz	= 0;
	caps->mtt_buf_pg_sz	= 0;
	caps->mtt_hop_num	= HNS_ROCE_MTT_HOP_NUM;
	caps->wqe_sq_hop_num	= HNS_ROCE_SQWQE_HOP_NUM;
	caps->wqe_sge_hop_num	= HNS_ROCE_EXT_SGE_HOP_NUM;
	caps->wqe_rq_hop_num	= HNS_ROCE_RQWQE_HOP_NUM;
	caps->cqe_ba_pg_sz	= HNS_ROCE_BA_PG_SZ_SUPPORTED_256K;
	caps->cqe_buf_pg_sz	= 0;
	caps->cqe_hop_num	= HNS_ROCE_CQE_HOP_NUM;
	caps->srqwqe_ba_pg_sz	= 0;
	caps->srqwqe_buf_pg_sz	= 0;
	caps->srqwqe_hop_num	= HNS_ROCE_SRQWQE_HOP_NUM;
	caps->idx_ba_pg_sz	= 0;
	caps->idx_buf_pg_sz	= 0;
	caps->idx_hop_num	= HNS_ROCE_IDX_HOP_NUM;
	caps->chunk_sz		= HNS_ROCE_V2_TABLE_CHUNK_SIZE;

	caps->flags		= HNS_ROCE_CAP_FLAG_REREG_MR |
				  HNS_ROCE_CAP_FLAG_ROCE_V1_V2 |
				  HNS_ROCE_CAP_FLAG_RQ_INLINE |
				  HNS_ROCE_CAP_FLAG_RECORD_DB |
				  HNS_ROCE_CAP_FLAG_SQ_RECORD_DB;

	caps->pkey_table_len[0] = 1;
	caps->gid_table_len[0]	= HNS_ROCE_V2_GID_INDEX_NUM;
	caps->ceqe_depth	= HNS_ROCE_V2_COMP_EQE_NUM;
	caps->aeqe_depth	= HNS_ROCE_V2_ASYNC_EQE_NUM;
	caps->local_ca_ack_delay = 0;
	caps->max_mtu = IB_MTU_4096;

	caps->max_srq_wrs	= HNS_ROCE_V2_MAX_SRQ_WR;
	caps->max_srq_sges	= HNS_ROCE_V2_MAX_SRQ_SGE;

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP08_B) {
		caps->flags |= HNS_ROCE_CAP_FLAG_ATOMIC | HNS_ROCE_CAP_FLAG_MW |
			       HNS_ROCE_CAP_FLAG_SRQ | HNS_ROCE_CAP_FLAG_FRMR |
			       HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL;

		caps->num_qpc_timer	  = HNS_ROCE_V2_MAX_QPC_TIMER_NUM;
		caps->qpc_timer_entry_sz  = HNS_ROCE_V2_QPC_TIMER_ENTRY_SZ;
		caps->qpc_timer_ba_pg_sz  = 0;
		caps->qpc_timer_buf_pg_sz = 0;
		caps->qpc_timer_hop_num   = HNS_ROCE_HOP_NUM_0;
		caps->num_cqc_timer	  = HNS_ROCE_V2_MAX_CQC_TIMER_NUM;
		caps->cqc_timer_entry_sz  = HNS_ROCE_V2_CQC_TIMER_ENTRY_SZ;
		caps->cqc_timer_ba_pg_sz  = 0;
		caps->cqc_timer_buf_pg_sz = 0;
		caps->cqc_timer_hop_num   = HNS_ROCE_HOP_NUM_0;

		caps->sccc_entry_sz	  = HNS_ROCE_V2_SCCC_ENTRY_SZ;
		caps->sccc_ba_pg_sz	  = 0;
		caps->sccc_buf_pg_sz	  = 0;
		caps->sccc_hop_num	  = HNS_ROCE_SCCC_HOP_NUM;
	}
}

static void calc_pg_sz(int obj_num, int obj_size, int hop_num, int ctx_bt_num,
		       int *buf_page_size, int *bt_page_size, u32 hem_type)
{
	u64 obj_per_chunk;
	int bt_chunk_size = 1 << PAGE_SHIFT;
	int buf_chunk_size = 1 << PAGE_SHIFT;
	int obj_per_chunk_default = buf_chunk_size / obj_size;

	*buf_page_size = 0;
	*bt_page_size = 0;

	switch (hop_num) {
	case 3:
		obj_per_chunk = ctx_bt_num * (bt_chunk_size / BA_BYTE_LEN) *
				(bt_chunk_size / BA_BYTE_LEN) *
				(bt_chunk_size / BA_BYTE_LEN) *
				 obj_per_chunk_default;
		break;
	case 2:
		obj_per_chunk = ctx_bt_num * (bt_chunk_size / BA_BYTE_LEN) *
				(bt_chunk_size / BA_BYTE_LEN) *
				 obj_per_chunk_default;
		break;
	case 1:
		obj_per_chunk = ctx_bt_num * (bt_chunk_size / BA_BYTE_LEN) *
				obj_per_chunk_default;
		break;
	case HNS_ROCE_HOP_NUM_0:
		obj_per_chunk = ctx_bt_num * obj_per_chunk_default;
		break;
	default:
		pr_err("Table %d not support hop_num = %d!\n", hem_type,
			hop_num);
		return;
	}

	if (hem_type >= HEM_TYPE_MTT)
		*bt_page_size = ilog2(DIV_ROUND_UP(obj_num, obj_per_chunk));
	else
		*buf_page_size = ilog2(DIV_ROUND_UP(obj_num, obj_per_chunk));
}

static int hns_roce_query_pf_caps(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc[HNS_ROCE_QUERY_PF_CAPS_CMD_NUM];
	struct hns_roce_caps *caps = &hr_dev->caps;
	struct hns_roce_query_pf_caps_a *resp_a;
	struct hns_roce_query_pf_caps_b *resp_b;
	struct hns_roce_query_pf_caps_c *resp_c;
	struct hns_roce_query_pf_caps_d *resp_d;
	struct hns_roce_query_pf_caps_e *resp_e;
	int ctx_hop_num;
	int pbl_hop_num;
	int ret;
	int i;

	for (i = 0; i < HNS_ROCE_QUERY_PF_CAPS_CMD_NUM; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i],
					      HNS_ROCE_OPC_QUERY_PF_CAPS_NUM,
					      true);
		if (i < (HNS_ROCE_QUERY_PF_CAPS_CMD_NUM - 1))
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	}

	ret = hns_roce_cmq_send(hr_dev, desc, HNS_ROCE_QUERY_PF_CAPS_CMD_NUM);
	if (ret)
		return ret;

	resp_a = (struct hns_roce_query_pf_caps_a *)desc[0].data;
	resp_b = (struct hns_roce_query_pf_caps_b *)desc[1].data;
	resp_c = (struct hns_roce_query_pf_caps_c *)desc[2].data;
	resp_d = (struct hns_roce_query_pf_caps_d *)desc[3].data;
	resp_e = (struct hns_roce_query_pf_caps_e *)desc[4].data;

	caps->local_ca_ack_delay     = resp_a->local_ca_ack_delay;
	caps->max_sq_sg		     = le16_to_cpu(resp_a->max_sq_sg);
	caps->max_sq_inline	     = le16_to_cpu(resp_a->max_sq_inline);
	caps->max_rq_sg		     = le16_to_cpu(resp_a->max_rq_sg);
	caps->max_extend_sg	     = le32_to_cpu(resp_a->max_extend_sg);
	caps->num_qpc_timer	     = le16_to_cpu(resp_a->num_qpc_timer);
	caps->num_cqc_timer	     = le16_to_cpu(resp_a->num_cqc_timer);
	caps->max_srq_sges	     = le16_to_cpu(resp_a->max_srq_sges);
	caps->num_aeq_vectors	     = resp_a->num_aeq_vectors;
	caps->num_other_vectors	     = resp_a->num_other_vectors;
	caps->max_sq_desc_sz	     = resp_a->max_sq_desc_sz;
	caps->max_rq_desc_sz	     = resp_a->max_rq_desc_sz;
	caps->max_srq_desc_sz	     = resp_a->max_srq_desc_sz;
	caps->cq_entry_sz	     = resp_a->cq_entry_sz;

	caps->mtpt_entry_sz	     = resp_b->mtpt_entry_sz;
	caps->irrl_entry_sz	     = resp_b->irrl_entry_sz;
	caps->trrl_entry_sz	     = resp_b->trrl_entry_sz;
	caps->cqc_entry_sz	     = resp_b->cqc_entry_sz;
	caps->srqc_entry_sz	     = resp_b->srqc_entry_sz;
	caps->idx_entry_sz	     = resp_b->idx_entry_sz;
	caps->sccc_entry_sz	     = resp_b->scc_ctx_entry_sz;
	caps->max_mtu		     = resp_b->max_mtu;
	caps->qpc_entry_sz	     = le16_to_cpu(resp_b->qpc_entry_sz);
	caps->min_cqes		     = resp_b->min_cqes;
	caps->min_wqes		     = resp_b->min_wqes;
	caps->page_size_cap	     = le32_to_cpu(resp_b->page_size_cap);
	caps->pkey_table_len[0]	     = resp_b->pkey_table_len;
	caps->phy_num_uars	     = resp_b->phy_num_uars;
	ctx_hop_num		     = resp_b->ctx_hop_num;
	pbl_hop_num		     = resp_b->pbl_hop_num;

	caps->num_pds = 1 << roce_get_field(resp_c->cap_flags_num_pds,
					    V2_QUERY_PF_CAPS_C_NUM_PDS_M,
					    V2_QUERY_PF_CAPS_C_NUM_PDS_S);
	caps->flags = roce_get_field(resp_c->cap_flags_num_pds,
				     V2_QUERY_PF_CAPS_C_CAP_FLAGS_M,
				     V2_QUERY_PF_CAPS_C_CAP_FLAGS_S);
	caps->num_cqs = 1 << roce_get_field(resp_c->max_gid_num_cqs,
					    V2_QUERY_PF_CAPS_C_NUM_CQS_M,
					    V2_QUERY_PF_CAPS_C_NUM_CQS_S);
	caps->gid_table_len[0] = roce_get_field(resp_c->max_gid_num_cqs,
						V2_QUERY_PF_CAPS_C_MAX_GID_M,
						V2_QUERY_PF_CAPS_C_MAX_GID_S);
	caps->max_cqes = 1 << roce_get_field(resp_c->cq_depth,
					     V2_QUERY_PF_CAPS_C_CQ_DEPTH_M,
					     V2_QUERY_PF_CAPS_C_CQ_DEPTH_S);
	caps->num_mtpts = 1 << roce_get_field(resp_c->num_mrws,
					      V2_QUERY_PF_CAPS_C_NUM_MRWS_M,
					      V2_QUERY_PF_CAPS_C_NUM_MRWS_S);
	caps->num_qps = 1 << roce_get_field(resp_c->ord_num_qps,
					    V2_QUERY_PF_CAPS_C_NUM_QPS_M,
					    V2_QUERY_PF_CAPS_C_NUM_QPS_S);
	caps->max_qp_init_rdma = roce_get_field(resp_c->ord_num_qps,
						V2_QUERY_PF_CAPS_C_MAX_ORD_M,
						V2_QUERY_PF_CAPS_C_MAX_ORD_S);
	caps->max_qp_dest_rdma = caps->max_qp_init_rdma;
	caps->max_wqes = 1 << le16_to_cpu(resp_c->sq_depth);
	caps->num_srqs = 1 << roce_get_field(resp_d->wq_hop_num_max_srqs,
					     V2_QUERY_PF_CAPS_D_NUM_SRQS_M,
					     V2_QUERY_PF_CAPS_D_NUM_SRQS_S);
	caps->max_srq_wrs = 1 << le16_to_cpu(resp_d->srq_depth);
	caps->ceqe_depth = 1 << roce_get_field(resp_d->num_ceqs_ceq_depth,
					       V2_QUERY_PF_CAPS_D_CEQ_DEPTH_M,
					       V2_QUERY_PF_CAPS_D_CEQ_DEPTH_S);
	caps->num_comp_vectors = roce_get_field(resp_d->num_ceqs_ceq_depth,
						V2_QUERY_PF_CAPS_D_NUM_CEQS_M,
						V2_QUERY_PF_CAPS_D_NUM_CEQS_S);
	caps->aeqe_depth = 1 << roce_get_field(resp_d->arm_st_aeq_depth,
					       V2_QUERY_PF_CAPS_D_AEQ_DEPTH_M,
					       V2_QUERY_PF_CAPS_D_AEQ_DEPTH_S);
	caps->default_aeq_arm_st = roce_get_field(resp_d->arm_st_aeq_depth,
					    V2_QUERY_PF_CAPS_D_AEQ_ARM_ST_M,
					    V2_QUERY_PF_CAPS_D_AEQ_ARM_ST_S);
	caps->default_ceq_arm_st = roce_get_field(resp_d->arm_st_aeq_depth,
					    V2_QUERY_PF_CAPS_D_CEQ_ARM_ST_M,
					    V2_QUERY_PF_CAPS_D_CEQ_ARM_ST_S);
	caps->reserved_pds = roce_get_field(resp_d->num_uars_rsv_pds,
					    V2_QUERY_PF_CAPS_D_RSV_PDS_M,
					    V2_QUERY_PF_CAPS_D_RSV_PDS_S);
	caps->num_uars = 1 << roce_get_field(resp_d->num_uars_rsv_pds,
					     V2_QUERY_PF_CAPS_D_NUM_UARS_M,
					     V2_QUERY_PF_CAPS_D_NUM_UARS_S);
	caps->reserved_qps = roce_get_field(resp_d->rsv_uars_rsv_qps,
					    V2_QUERY_PF_CAPS_D_RSV_QPS_M,
					    V2_QUERY_PF_CAPS_D_RSV_QPS_S);
	caps->reserved_uars = roce_get_field(resp_d->rsv_uars_rsv_qps,
					     V2_QUERY_PF_CAPS_D_RSV_UARS_M,
					     V2_QUERY_PF_CAPS_D_RSV_UARS_S);
	caps->reserved_mrws = roce_get_field(resp_e->chunk_size_shift_rsv_mrws,
					     V2_QUERY_PF_CAPS_E_RSV_MRWS_M,
					     V2_QUERY_PF_CAPS_E_RSV_MRWS_S);
	caps->chunk_sz = 1 << roce_get_field(resp_e->chunk_size_shift_rsv_mrws,
					 V2_QUERY_PF_CAPS_E_CHUNK_SIZE_SHIFT_M,
					 V2_QUERY_PF_CAPS_E_CHUNK_SIZE_SHIFT_S);
	caps->reserved_cqs = roce_get_field(resp_e->rsv_cqs,
					    V2_QUERY_PF_CAPS_E_RSV_CQS_M,
					    V2_QUERY_PF_CAPS_E_RSV_CQS_S);
	caps->reserved_srqs = roce_get_field(resp_e->rsv_srqs,
					     V2_QUERY_PF_CAPS_E_RSV_SRQS_M,
					     V2_QUERY_PF_CAPS_E_RSV_SRQS_S);
	caps->reserved_lkey = roce_get_field(resp_e->rsv_lkey,
					     V2_QUERY_PF_CAPS_E_RSV_LKEYS_M,
					     V2_QUERY_PF_CAPS_E_RSV_LKEYS_S);
	caps->default_ceq_max_cnt = le16_to_cpu(resp_e->ceq_max_cnt);
	caps->default_ceq_period = le16_to_cpu(resp_e->ceq_period);
	caps->default_aeq_max_cnt = le16_to_cpu(resp_e->aeq_max_cnt);
	caps->default_aeq_period = le16_to_cpu(resp_e->aeq_period);

	caps->qpc_timer_entry_sz = HNS_ROCE_V2_QPC_TIMER_ENTRY_SZ;
	caps->cqc_timer_entry_sz = HNS_ROCE_V2_CQC_TIMER_ENTRY_SZ;
	caps->mtt_entry_sz = HNS_ROCE_V2_MTT_ENTRY_SZ;
	caps->num_mtt_segs = HNS_ROCE_V2_MAX_MTT_SEGS;
	caps->mtt_ba_pg_sz = 0;
	caps->num_cqe_segs = HNS_ROCE_V2_MAX_CQE_SEGS;
	caps->num_srqwqe_segs = HNS_ROCE_V2_MAX_SRQWQE_SEGS;
	caps->num_idx_segs = HNS_ROCE_V2_MAX_IDX_SEGS;

	caps->qpc_hop_num = ctx_hop_num;
	caps->srqc_hop_num = ctx_hop_num;
	caps->cqc_hop_num = ctx_hop_num;
	caps->mpt_hop_num = ctx_hop_num;
	caps->mtt_hop_num = pbl_hop_num;
	caps->cqe_hop_num = pbl_hop_num;
	caps->srqwqe_hop_num = pbl_hop_num;
	caps->idx_hop_num = pbl_hop_num;
	caps->wqe_sq_hop_num = roce_get_field(resp_d->wq_hop_num_max_srqs,
					  V2_QUERY_PF_CAPS_D_SQWQE_HOP_NUM_M,
					  V2_QUERY_PF_CAPS_D_SQWQE_HOP_NUM_S);
	caps->wqe_sge_hop_num = roce_get_field(resp_d->wq_hop_num_max_srqs,
					  V2_QUERY_PF_CAPS_D_EX_SGE_HOP_NUM_M,
					  V2_QUERY_PF_CAPS_D_EX_SGE_HOP_NUM_S);
	caps->wqe_rq_hop_num = roce_get_field(resp_d->wq_hop_num_max_srqs,
					  V2_QUERY_PF_CAPS_D_RQWQE_HOP_NUM_M,
					  V2_QUERY_PF_CAPS_D_RQWQE_HOP_NUM_S);

	calc_pg_sz(caps->num_qps, caps->qpc_entry_sz, caps->qpc_hop_num,
		   caps->qpc_bt_num, &caps->qpc_buf_pg_sz, &caps->qpc_ba_pg_sz,
		   HEM_TYPE_QPC);
	calc_pg_sz(caps->num_mtpts, caps->mtpt_entry_sz, caps->mpt_hop_num,
		   caps->mpt_bt_num, &caps->mpt_buf_pg_sz, &caps->mpt_ba_pg_sz,
		   HEM_TYPE_MTPT);
	calc_pg_sz(caps->num_cqs, caps->cqc_entry_sz, caps->cqc_hop_num,
		   caps->cqc_bt_num, &caps->cqc_buf_pg_sz, &caps->cqc_ba_pg_sz,
		   HEM_TYPE_CQC);
	calc_pg_sz(caps->num_srqs, caps->srqc_entry_sz, caps->srqc_hop_num,
		   caps->srqc_bt_num, &caps->srqc_buf_pg_sz,
		   &caps->srqc_ba_pg_sz, HEM_TYPE_SRQC);

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP08_B) {
		caps->sccc_hop_num = ctx_hop_num;
		caps->qpc_timer_hop_num = HNS_ROCE_HOP_NUM_0;
		caps->cqc_timer_hop_num = HNS_ROCE_HOP_NUM_0;

		calc_pg_sz(caps->num_qps, caps->sccc_entry_sz,
			   caps->sccc_hop_num, caps->sccc_bt_num,
			   &caps->sccc_buf_pg_sz, &caps->sccc_ba_pg_sz,
			   HEM_TYPE_SCCC);
		calc_pg_sz(caps->num_cqc_timer, caps->cqc_timer_entry_sz,
			   caps->cqc_timer_hop_num, caps->cqc_timer_bt_num,
			   &caps->cqc_timer_buf_pg_sz,
			   &caps->cqc_timer_ba_pg_sz, HEM_TYPE_CQC_TIMER);
	}

	calc_pg_sz(caps->num_cqe_segs, caps->mtt_entry_sz, caps->cqe_hop_num,
		   1, &caps->cqe_buf_pg_sz, &caps->cqe_ba_pg_sz, HEM_TYPE_CQE);
	calc_pg_sz(caps->num_srqwqe_segs, caps->mtt_entry_sz,
		   caps->srqwqe_hop_num, 1, &caps->srqwqe_buf_pg_sz,
		   &caps->srqwqe_ba_pg_sz, HEM_TYPE_SRQWQE);
	calc_pg_sz(caps->num_idx_segs, caps->idx_entry_sz, caps->idx_hop_num,
		   1, &caps->idx_buf_pg_sz, &caps->idx_ba_pg_sz, HEM_TYPE_IDX);

	return 0;
}

static int hns_roce_v2_profile(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;
	int ret;

	ret = hns_roce_cmq_query_hw_info(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Query hardware version fail, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hns_roce_query_fw_ver(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Query firmware version fail, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hns_roce_config_global_param(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Configure global param fail, ret = %d.\n",
			ret);
		return ret;
	}

	/* Get pf resource owned by every pf */
	ret = hns_roce_query_pf_resource(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Query pf resource fail, ret = %d.\n",
			ret);
		return ret;
	}

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP08_B) {
		ret = hns_roce_query_pf_timer_resource(hr_dev);
		if (ret) {
			dev_err(hr_dev->dev,
				"Query pf timer resource fail, ret = %d.\n",
				ret);
			return ret;
		}

		ret = hns_roce_set_vf_switch_param(hr_dev, 0);
		if (ret) {
			dev_err(hr_dev->dev,
				"Set function switch param fail, ret = %d.\n",
				ret);
			return ret;
		}
	}

	hr_dev->vendor_part_id = hr_dev->pci_dev->device;
	hr_dev->sys_image_guid = be64_to_cpu(hr_dev->ib_dev.node_guid);

	caps->num_mtt_segs	= HNS_ROCE_V2_MAX_MTT_SEGS;
	caps->num_cqe_segs	= HNS_ROCE_V2_MAX_CQE_SEGS;
	caps->num_srqwqe_segs	= HNS_ROCE_V2_MAX_SRQWQE_SEGS;
	caps->num_idx_segs	= HNS_ROCE_V2_MAX_IDX_SEGS;

	caps->pbl_ba_pg_sz	= HNS_ROCE_BA_PG_SZ_SUPPORTED_16K;
	caps->pbl_buf_pg_sz	= 0;
	caps->pbl_hop_num	= HNS_ROCE_PBL_HOP_NUM;
	caps->eqe_ba_pg_sz	= 0;
	caps->eqe_buf_pg_sz	= 0;
	caps->eqe_hop_num	= HNS_ROCE_EQE_HOP_NUM;
	caps->tsq_buf_pg_sz	= 0;

	ret = hns_roce_query_pf_caps(hr_dev);
	if (ret)
		set_default_caps(hr_dev);

	ret = hns_roce_alloc_vf_resource(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "Allocate vf resource fail, ret = %d.\n",
			ret);
		return ret;
	}

	ret = hns_roce_v2_set_bt(hr_dev);
	if (ret)
		dev_err(hr_dev->dev, "Configure bt attribute fail, ret = %d.\n",
			ret);

	return ret;
}

static int hns_roce_config_link_table(struct hns_roce_dev *hr_dev,
				      enum hns_roce_link_table_type type)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cfg_llm_a *req_a =
				(struct hns_roce_cfg_llm_a *)desc[0].data;
	struct hns_roce_cfg_llm_b *req_b =
				(struct hns_roce_cfg_llm_b *)desc[1].data;
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_link_table *link_tbl;
	struct hns_roce_link_table_entry *entry;
	enum hns_roce_opcode_type opcode;
	u32 page_num;
	int i;

	switch (type) {
	case TSQ_LINK_TABLE:
		link_tbl = &priv->tsq;
		opcode = HNS_ROCE_OPC_CFG_EXT_LLM;
		break;
	case TPQ_LINK_TABLE:
		link_tbl = &priv->tpq;
		opcode = HNS_ROCE_OPC_CFG_TMOUT_LLM;
		break;
	default:
		return -EINVAL;
	}

	page_num = link_tbl->npages;
	entry = link_tbl->table.buf;
	memset(req_a, 0, sizeof(*req_a));
	memset(req_b, 0, sizeof(*req_b));

	for (i = 0; i < 2; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i], opcode, false);

		if (i == 0)
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
		else
			desc[i].flag &= ~cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);

		if (i == 0) {
			req_a->base_addr_l =
				cpu_to_le32(link_tbl->table.map & 0xffffffff);
			req_a->base_addr_h =
				cpu_to_le32(link_tbl->table.map >> 32);
			roce_set_field(req_a->depth_pgsz_init_en,
				       CFG_LLM_QUE_DEPTH_M, CFG_LLM_QUE_DEPTH_S,
				       link_tbl->npages);
			roce_set_field(req_a->depth_pgsz_init_en,
				       CFG_LLM_QUE_PGSZ_M, CFG_LLM_QUE_PGSZ_S,
				       link_tbl->pg_sz);
			req_a->head_ba_l = cpu_to_le32(entry[0].blk_ba0);
			req_a->head_ba_h_nxtptr =
				cpu_to_le32(entry[0].blk_ba1_nxt_ptr);
			roce_set_field(req_a->head_ptr, CFG_LLM_HEAD_PTR_M,
				       CFG_LLM_HEAD_PTR_S, 0);
		} else {
			req_b->tail_ba_l =
				cpu_to_le32(entry[page_num - 1].blk_ba0);
			roce_set_field(req_b->tail_ba_h, CFG_LLM_TAIL_BA_H_M,
				       CFG_LLM_TAIL_BA_H_S,
				       entry[page_num - 1].blk_ba1_nxt_ptr &
					       HNS_ROCE_LINK_TABLE_BA1_M);
			roce_set_field(req_b->tail_ptr, CFG_LLM_TAIL_PTR_M,
				       CFG_LLM_TAIL_PTR_S,
				       (entry[page_num - 2].blk_ba1_nxt_ptr &
					HNS_ROCE_LINK_TABLE_NXT_PTR_M) >>
					       HNS_ROCE_LINK_TABLE_NXT_PTR_S);
		}
	}
	roce_set_field(req_a->depth_pgsz_init_en, CFG_LLM_INIT_EN_M,
		       CFG_LLM_INIT_EN_S, 1);

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static int hns_roce_init_link_table(struct hns_roce_dev *hr_dev,
				    enum hns_roce_link_table_type type)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_link_table *link_tbl;
	struct hns_roce_link_table_entry *entry;
	struct device *dev = hr_dev->dev;
	u32 buf_chk_sz;
	dma_addr_t t;
	int func_num = 1;
	int pg_num_a;
	int pg_num_b;
	int pg_num;
	int size;
	int i;

	switch (type) {
	case TSQ_LINK_TABLE:
		link_tbl = &priv->tsq;
		buf_chk_sz = 1 << (hr_dev->caps.tsq_buf_pg_sz + PAGE_SHIFT);
		pg_num_a = hr_dev->caps.num_qps * 8 / buf_chk_sz;
		pg_num_b = hr_dev->caps.sl_num * 4 + 2;
		break;
	case TPQ_LINK_TABLE:
		link_tbl = &priv->tpq;
		buf_chk_sz = 1 << (hr_dev->caps.tpq_buf_pg_sz +	PAGE_SHIFT);
		pg_num_a = hr_dev->caps.num_cqs * 4 / buf_chk_sz;
		pg_num_b = 2 * 4 * func_num + 2;
		break;
	default:
		return -EINVAL;
	}

	pg_num = max(pg_num_a, pg_num_b);
	size = pg_num * sizeof(struct hns_roce_link_table_entry);

	link_tbl->table.buf = dma_alloc_coherent(dev, size,
						 &link_tbl->table.map,
						 GFP_KERNEL);
	if (!link_tbl->table.buf)
		goto out;

	link_tbl->pg_list = kcalloc(pg_num, sizeof(*link_tbl->pg_list),
				    GFP_KERNEL);
	if (!link_tbl->pg_list)
		goto err_kcalloc_failed;

	entry = link_tbl->table.buf;
	for (i = 0; i < pg_num; ++i) {
		link_tbl->pg_list[i].buf = dma_alloc_coherent(dev, buf_chk_sz,
							      &t, GFP_KERNEL);
		if (!link_tbl->pg_list[i].buf)
			goto err_alloc_buf_failed;

		link_tbl->pg_list[i].map = t;

		entry[i].blk_ba0 = (u32)(t >> 12);
		entry[i].blk_ba1_nxt_ptr = (u32)(t >> 44);

		if (i < (pg_num - 1))
			entry[i].blk_ba1_nxt_ptr |=
				(i + 1) << HNS_ROCE_LINK_TABLE_NXT_PTR_S;

	}
	link_tbl->npages = pg_num;
	link_tbl->pg_sz = buf_chk_sz;

	return hns_roce_config_link_table(hr_dev, type);

err_alloc_buf_failed:
	for (i -= 1; i >= 0; i--)
		dma_free_coherent(dev, buf_chk_sz,
				  link_tbl->pg_list[i].buf,
				  link_tbl->pg_list[i].map);
	kfree(link_tbl->pg_list);

err_kcalloc_failed:
	dma_free_coherent(dev, size, link_tbl->table.buf,
			  link_tbl->table.map);

out:
	return -ENOMEM;
}

static void hns_roce_free_link_table(struct hns_roce_dev *hr_dev,
				     struct hns_roce_link_table *link_tbl)
{
	struct device *dev = hr_dev->dev;
	int size;
	int i;

	size = link_tbl->npages * sizeof(struct hns_roce_link_table_entry);

	for (i = 0; i < link_tbl->npages; ++i)
		if (link_tbl->pg_list[i].buf)
			dma_free_coherent(dev, link_tbl->pg_sz,
					  link_tbl->pg_list[i].buf,
					  link_tbl->pg_list[i].map);
	kfree(link_tbl->pg_list);

	dma_free_coherent(dev, size, link_tbl->table.buf,
			  link_tbl->table.map);
}

static int hns_roce_v2_init(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	int qpc_count, cqc_count;
	int ret, i;

	/* TSQ includes SQ doorbell and ack doorbell */
	ret = hns_roce_init_link_table(hr_dev, TSQ_LINK_TABLE);
	if (ret) {
		dev_err(hr_dev->dev, "TSQ init failed, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_init_link_table(hr_dev, TPQ_LINK_TABLE);
	if (ret) {
		dev_err(hr_dev->dev, "TPQ init failed, ret = %d.\n", ret);
		goto err_tpq_init_failed;
	}

	/* Alloc memory for QPC Timer buffer space chunk */
	for (qpc_count = 0; qpc_count < hr_dev->caps.qpc_timer_bt_num;
	     qpc_count++) {
		ret = hns_roce_table_get(hr_dev, &hr_dev->qpc_timer_table,
					 qpc_count);
		if (ret) {
			dev_err(hr_dev->dev, "QPC Timer get failed\n");
			goto err_qpc_timer_failed;
		}
	}

	/* Alloc memory for CQC Timer buffer space chunk */
	for (cqc_count = 0; cqc_count < hr_dev->caps.cqc_timer_bt_num;
	     cqc_count++) {
		ret = hns_roce_table_get(hr_dev, &hr_dev->cqc_timer_table,
					 cqc_count);
		if (ret) {
			dev_err(hr_dev->dev, "CQC Timer get failed\n");
			goto err_cqc_timer_failed;
		}
	}

	return 0;

err_cqc_timer_failed:
	for (i = 0; i < cqc_count; i++)
		hns_roce_table_put(hr_dev, &hr_dev->cqc_timer_table, i);

err_qpc_timer_failed:
	for (i = 0; i < qpc_count; i++)
		hns_roce_table_put(hr_dev, &hr_dev->qpc_timer_table, i);

	hns_roce_free_link_table(hr_dev, &priv->tpq);

err_tpq_init_failed:
	hns_roce_free_link_table(hr_dev, &priv->tsq);

	return ret;
}

static void hns_roce_v2_exit(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP08_B)
		hns_roce_function_clear(hr_dev);

	hns_roce_free_link_table(hr_dev, &priv->tpq);
	hns_roce_free_link_table(hr_dev, &priv->tsq);
}

static int hns_roce_query_mbox_status(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_mbox_status *mb_st =
				       (struct hns_roce_mbox_status *)desc.data;
	enum hns_roce_cmd_return_status status;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_MB_ST, true);

	status = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (status)
		return status;

	return le32_to_cpu(mb_st->mb_status_hw_run);
}

static int hns_roce_v2_cmd_pending(struct hns_roce_dev *hr_dev)
{
	u32 status = hns_roce_query_mbox_status(hr_dev);

	return status >> HNS_ROCE_HW_RUN_BIT_SHIFT;
}

static int hns_roce_v2_cmd_complete(struct hns_roce_dev *hr_dev)
{
	u32 status = hns_roce_query_mbox_status(hr_dev);

	return status & HNS_ROCE_HW_MB_STATUS_MASK;
}

static int hns_roce_mbox_post(struct hns_roce_dev *hr_dev, u64 in_param,
			      u64 out_param, u32 in_modifier, u8 op_modifier,
			      u16 op, u16 token, int event)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_post_mbox *mb = (struct hns_roce_post_mbox *)desc.data;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_POST_MB, false);

	mb->in_param_l = cpu_to_le32(in_param);
	mb->in_param_h = cpu_to_le32(in_param >> 32);
	mb->out_param_l = cpu_to_le32(out_param);
	mb->out_param_h = cpu_to_le32(out_param >> 32);
	mb->cmd_tag = cpu_to_le32(in_modifier << 8 | op);
	mb->token_event_en = cpu_to_le32(event << 16 | token);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int hns_roce_v2_post_mbox(struct hns_roce_dev *hr_dev, u64 in_param,
				 u64 out_param, u32 in_modifier, u8 op_modifier,
				 u16 op, u16 token, int event)
{
	struct device *dev = hr_dev->dev;
	unsigned long end;
	int ret;

	end = msecs_to_jiffies(HNS_ROCE_V2_GO_BIT_TIMEOUT_MSECS) + jiffies;
	while (hns_roce_v2_cmd_pending(hr_dev)) {
		if (time_after(jiffies, end)) {
			dev_dbg(dev, "jiffies=%d end=%d\n", (int)jiffies,
				(int)end);
			return -EAGAIN;
		}
		cond_resched();
	}

	ret = hns_roce_mbox_post(hr_dev, in_param, out_param, in_modifier,
				 op_modifier, op, token, event);
	if (ret)
		dev_err(dev, "Post mailbox fail(%d)\n", ret);

	return ret;
}

static int hns_roce_v2_chk_mbox(struct hns_roce_dev *hr_dev,
				unsigned long timeout)
{
	struct device *dev = hr_dev->dev;
	unsigned long end;
	u32 status;

	end = msecs_to_jiffies(timeout) + jiffies;
	while (hns_roce_v2_cmd_pending(hr_dev) && time_before(jiffies, end))
		cond_resched();

	if (hns_roce_v2_cmd_pending(hr_dev)) {
		dev_err(dev, "[cmd_poll]hw run cmd TIMEDOUT!\n");
		return -ETIMEDOUT;
	}

	status = hns_roce_v2_cmd_complete(hr_dev);
	if (status != 0x1) {
		if (status == CMD_RST_PRC_EBUSY)
			return status;

		dev_err(dev, "mailbox status 0x%x!\n", status);
		return -EBUSY;
	}

	return 0;
}

static int hns_roce_config_sgid_table(struct hns_roce_dev *hr_dev,
				      int gid_index, const union ib_gid *gid,
				      enum hns_roce_sgid_type sgid_type)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cfg_sgid_tb *sgid_tb =
				    (struct hns_roce_cfg_sgid_tb *)desc.data;
	u32 *p;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_SGID_TB, false);

	roce_set_field(sgid_tb->table_idx_rsv, CFG_SGID_TB_TABLE_IDX_M,
		       CFG_SGID_TB_TABLE_IDX_S, gid_index);
	roce_set_field(sgid_tb->vf_sgid_type_rsv, CFG_SGID_TB_VF_SGID_TYPE_M,
		       CFG_SGID_TB_VF_SGID_TYPE_S, sgid_type);

	p = (u32 *)&gid->raw[0];
	sgid_tb->vf_sgid_l = cpu_to_le32(*p);

	p = (u32 *)&gid->raw[4];
	sgid_tb->vf_sgid_ml = cpu_to_le32(*p);

	p = (u32 *)&gid->raw[8];
	sgid_tb->vf_sgid_mh = cpu_to_le32(*p);

	p = (u32 *)&gid->raw[0xc];
	sgid_tb->vf_sgid_h = cpu_to_le32(*p);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int hns_roce_v2_set_gid(struct hns_roce_dev *hr_dev, u8 port,
			       int gid_index, const union ib_gid *gid,
			       const struct ib_gid_attr *attr)
{
	enum hns_roce_sgid_type sgid_type = GID_TYPE_FLAG_ROCE_V1;
	int ret;

	if (!gid || !attr)
		return -EINVAL;

	if (attr->gid_type == IB_GID_TYPE_ROCE)
		sgid_type = GID_TYPE_FLAG_ROCE_V1;

	if (attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) {
		if (ipv6_addr_v4mapped((void *)gid))
			sgid_type = GID_TYPE_FLAG_ROCE_V2_IPV4;
		else
			sgid_type = GID_TYPE_FLAG_ROCE_V2_IPV6;
	}

	ret = hns_roce_config_sgid_table(hr_dev, gid_index, gid, sgid_type);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to configure sgid table, ret = %d!\n",
			  ret);

	return ret;
}

static int hns_roce_v2_set_mac(struct hns_roce_dev *hr_dev, u8 phy_port,
			       u8 *addr)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cfg_smac_tb *smac_tb =
				    (struct hns_roce_cfg_smac_tb *)desc.data;
	u16 reg_smac_h;
	u32 reg_smac_l;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_SMAC_TB, false);

	reg_smac_l = *(u32 *)(&addr[0]);
	reg_smac_h = *(u16 *)(&addr[4]);

	memset(smac_tb, 0, sizeof(*smac_tb));
	roce_set_field(smac_tb->tb_idx_rsv,
		       CFG_SMAC_TB_IDX_M,
		       CFG_SMAC_TB_IDX_S, phy_port);
	roce_set_field(smac_tb->vf_smac_h_rsv,
		       CFG_SMAC_TB_VF_SMAC_H_M,
		       CFG_SMAC_TB_VF_SMAC_H_S, reg_smac_h);
	smac_tb->vf_smac_l = cpu_to_le32(reg_smac_l);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int set_mtpt_pbl(struct hns_roce_v2_mpt_entry *mpt_entry,
			struct hns_roce_mr *mr)
{
	struct sg_dma_page_iter sg_iter;
	u64 page_addr;
	u64 *pages;
	int i;

	mpt_entry->pbl_size = cpu_to_le32(mr->pbl_size);
	mpt_entry->pbl_ba_l = cpu_to_le32(lower_32_bits(mr->pbl_ba >> 3));
	roce_set_field(mpt_entry->byte_48_mode_ba,
		       V2_MPT_BYTE_48_PBL_BA_H_M, V2_MPT_BYTE_48_PBL_BA_H_S,
		       upper_32_bits(mr->pbl_ba >> 3));

	pages = (u64 *)__get_free_page(GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	i = 0;
	for_each_sg_dma_page(mr->umem->sg_head.sgl, &sg_iter, mr->umem->nmap, 0) {
		page_addr = sg_page_iter_dma_address(&sg_iter);
		pages[i] = page_addr >> 6;

		/* Record the first 2 entry directly to MTPT table */
		if (i >= HNS_ROCE_V2_MAX_INNER_MTPT_NUM - 1)
			goto found;
		i++;
	}
found:
	mpt_entry->pa0_l = cpu_to_le32(lower_32_bits(pages[0]));
	roce_set_field(mpt_entry->byte_56_pa0_h, V2_MPT_BYTE_56_PA0_H_M,
		       V2_MPT_BYTE_56_PA0_H_S, upper_32_bits(pages[0]));

	mpt_entry->pa1_l = cpu_to_le32(lower_32_bits(pages[1]));
	roce_set_field(mpt_entry->byte_64_buf_pa1, V2_MPT_BYTE_64_PA1_H_M,
		       V2_MPT_BYTE_64_PA1_H_S, upper_32_bits(pages[1]));
	roce_set_field(mpt_entry->byte_64_buf_pa1,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_M,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_S,
		       mr->pbl_buf_pg_sz + PG_SHIFT_OFFSET);

	free_page((unsigned long)pages);

	return 0;
}

static int hns_roce_v2_write_mtpt(void *mb_buf, struct hns_roce_mr *mr,
				  unsigned long mtpt_idx)
{
	struct hns_roce_v2_mpt_entry *mpt_entry;
	int ret;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_MPT_ST_M,
		       V2_MPT_BYTE_4_MPT_ST_S, V2_MPT_ST_VALID);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PBL_HOP_NUM_M,
		       V2_MPT_BYTE_4_PBL_HOP_NUM_S, mr->pbl_hop_num ==
		       HNS_ROCE_HOP_NUM_0 ? 0 : mr->pbl_hop_num);
	roce_set_field(mpt_entry->byte_4_pd_hop_st,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_M,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_S,
		       mr->pbl_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PD_M,
		       V2_MPT_BYTE_4_PD_S, mr->pd);

	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RA_EN_S, 0);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_R_INV_EN_S, 0);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_L_INV_EN_S, 1);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_BIND_EN_S,
		     (mr->access & IB_ACCESS_MW_BIND ? 1 : 0));
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_ATOMIC_EN_S,
		     mr->access & IB_ACCESS_REMOTE_ATOMIC ? 1 : 0);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RR_EN_S,
		     (mr->access & IB_ACCESS_REMOTE_READ ? 1 : 0));
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RW_EN_S,
		     (mr->access & IB_ACCESS_REMOTE_WRITE ? 1 : 0));
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_LW_EN_S,
		     (mr->access & IB_ACCESS_LOCAL_WRITE ? 1 : 0));

	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_PA_S,
		     mr->type == MR_TYPE_MR ? 0 : 1);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_INNER_PA_VLD_S,
		     1);

	mpt_entry->len_l = cpu_to_le32(lower_32_bits(mr->size));
	mpt_entry->len_h = cpu_to_le32(upper_32_bits(mr->size));
	mpt_entry->lkey = cpu_to_le32(mr->key);
	mpt_entry->va_l = cpu_to_le32(lower_32_bits(mr->iova));
	mpt_entry->va_h = cpu_to_le32(upper_32_bits(mr->iova));

	if (mr->type == MR_TYPE_DMA)
		return 0;

	ret = set_mtpt_pbl(mpt_entry, mr);

	return ret;
}

static int hns_roce_v2_rereg_write_mtpt(struct hns_roce_dev *hr_dev,
					struct hns_roce_mr *mr, int flags,
					u32 pdn, int mr_access_flags, u64 iova,
					u64 size, void *mb_buf)
{
	struct hns_roce_v2_mpt_entry *mpt_entry = mb_buf;
	int ret = 0;

	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_MPT_ST_M,
		       V2_MPT_BYTE_4_MPT_ST_S, V2_MPT_ST_VALID);

	if (flags & IB_MR_REREG_PD) {
		roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PD_M,
			       V2_MPT_BYTE_4_PD_S, pdn);
		mr->pd = pdn;
	}

	if (flags & IB_MR_REREG_ACCESS) {
		roce_set_bit(mpt_entry->byte_8_mw_cnt_en,
			     V2_MPT_BYTE_8_BIND_EN_S,
			     (mr_access_flags & IB_ACCESS_MW_BIND ? 1 : 0));
		roce_set_bit(mpt_entry->byte_8_mw_cnt_en,
			     V2_MPT_BYTE_8_ATOMIC_EN_S,
			     mr_access_flags & IB_ACCESS_REMOTE_ATOMIC ? 1 : 0);
		roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RR_EN_S,
			     mr_access_flags & IB_ACCESS_REMOTE_READ ? 1 : 0);
		roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RW_EN_S,
			     mr_access_flags & IB_ACCESS_REMOTE_WRITE ? 1 : 0);
		roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_LW_EN_S,
			     mr_access_flags & IB_ACCESS_LOCAL_WRITE ? 1 : 0);
	}

	if (flags & IB_MR_REREG_TRANS) {
		mpt_entry->va_l = cpu_to_le32(lower_32_bits(iova));
		mpt_entry->va_h = cpu_to_le32(upper_32_bits(iova));
		mpt_entry->len_l = cpu_to_le32(lower_32_bits(size));
		mpt_entry->len_h = cpu_to_le32(upper_32_bits(size));

		mr->iova = iova;
		mr->size = size;

		ret = set_mtpt_pbl(mpt_entry, mr);
	}

	return ret;
}

static int hns_roce_v2_frmr_write_mtpt(void *mb_buf, struct hns_roce_mr *mr)
{
	struct hns_roce_v2_mpt_entry *mpt_entry;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_MPT_ST_M,
		       V2_MPT_BYTE_4_MPT_ST_S, V2_MPT_ST_FREE);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PBL_HOP_NUM_M,
		       V2_MPT_BYTE_4_PBL_HOP_NUM_S, 1);
	roce_set_field(mpt_entry->byte_4_pd_hop_st,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_M,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_S,
		       mr->pbl_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PD_M,
		       V2_MPT_BYTE_4_PD_S, mr->pd);

	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_RA_EN_S, 1);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_R_INV_EN_S, 1);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_L_INV_EN_S, 1);

	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_FRE_S, 1);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_PA_S, 0);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_MR_MW_S, 0);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_BPD_S, 1);

	mpt_entry->pbl_size = cpu_to_le32(mr->pbl_size);

	mpt_entry->pbl_ba_l = cpu_to_le32(lower_32_bits(mr->pbl_ba >> 3));
	roce_set_field(mpt_entry->byte_48_mode_ba, V2_MPT_BYTE_48_PBL_BA_H_M,
		       V2_MPT_BYTE_48_PBL_BA_H_S,
		       upper_32_bits(mr->pbl_ba >> 3));

	roce_set_field(mpt_entry->byte_64_buf_pa1,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_M,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_S,
		       mr->pbl_buf_pg_sz + PG_SHIFT_OFFSET);

	return 0;
}

static int hns_roce_v2_mw_write_mtpt(void *mb_buf, struct hns_roce_mw *mw)
{
	struct hns_roce_v2_mpt_entry *mpt_entry;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_MPT_ST_M,
		       V2_MPT_BYTE_4_MPT_ST_S, V2_MPT_ST_FREE);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PD_M,
		       V2_MPT_BYTE_4_PD_S, mw->pdn);
	roce_set_field(mpt_entry->byte_4_pd_hop_st, V2_MPT_BYTE_4_PBL_HOP_NUM_M,
		       V2_MPT_BYTE_4_PBL_HOP_NUM_S,
		       mw->pbl_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 :
							       mw->pbl_hop_num);
	roce_set_field(mpt_entry->byte_4_pd_hop_st,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_M,
		       V2_MPT_BYTE_4_PBL_BA_PG_SZ_S,
		       mw->pbl_ba_pg_sz + PG_SHIFT_OFFSET);

	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_R_INV_EN_S, 1);
	roce_set_bit(mpt_entry->byte_8_mw_cnt_en, V2_MPT_BYTE_8_L_INV_EN_S, 1);

	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_PA_S, 0);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_MR_MW_S, 1);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_BPD_S, 1);
	roce_set_bit(mpt_entry->byte_12_mw_pa, V2_MPT_BYTE_12_BQP_S,
		     mw->ibmw.type == IB_MW_TYPE_1 ? 0 : 1);

	roce_set_field(mpt_entry->byte_64_buf_pa1,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_M,
		       V2_MPT_BYTE_64_PBL_BUF_PG_SZ_S,
		       mw->pbl_buf_pg_sz + PG_SHIFT_OFFSET);

	mpt_entry->lkey = cpu_to_le32(mw->rkey);

	return 0;
}

static void *get_cqe_v2(struct hns_roce_cq *hr_cq, int n)
{
	return hns_roce_buf_offset(&hr_cq->buf, n * HNS_ROCE_V2_CQE_ENTRY_SIZE);
}

static void *get_sw_cqe_v2(struct hns_roce_cq *hr_cq, int n)
{
	struct hns_roce_v2_cqe *cqe = get_cqe_v2(hr_cq, n & hr_cq->ib_cq.cqe);

	/* Get cqe when Owner bit is Conversely with the MSB of cons_idx */
	return (roce_get_bit(cqe->byte_4, V2_CQE_BYTE_4_OWNER_S) ^
		!!(n & hr_cq->cq_depth)) ? cqe : NULL;
}

static struct hns_roce_v2_cqe *next_cqe_sw_v2(struct hns_roce_cq *hr_cq)
{
	return get_sw_cqe_v2(hr_cq, hr_cq->cons_index);
}

static void *get_srq_wqe(struct hns_roce_srq *srq, int n)
{
	return hns_roce_buf_offset(&srq->buf, n << srq->wqe_shift);
}

static void hns_roce_free_srq_wqe(struct hns_roce_srq *srq, int wqe_index)
{
	/* always called with interrupts disabled. */
	spin_lock(&srq->lock);

	bitmap_clear(srq->idx_que.bitmap, wqe_index, 1);
	srq->tail++;

	spin_unlock(&srq->lock);
}

static void hns_roce_v2_cq_set_ci(struct hns_roce_cq *hr_cq, u32 cons_index)
{
	*hr_cq->set_ci_db = cons_index & V2_CQ_DB_PARAMETER_CONS_IDX_M;
}

static void __hns_roce_v2_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				   struct hns_roce_srq *srq)
{
	struct hns_roce_v2_cqe *cqe, *dest;
	u32 prod_index;
	int nfreed = 0;
	int wqe_index;
	u8 owner_bit;

	for (prod_index = hr_cq->cons_index; get_sw_cqe_v2(hr_cq, prod_index);
	     ++prod_index) {
		if (prod_index > hr_cq->cons_index + hr_cq->ib_cq.cqe)
			break;
	}

	/*
	 * Now backwards through the CQ, removing CQ entries
	 * that match our QP by overwriting them with next entries.
	 */
	while ((int) --prod_index - (int) hr_cq->cons_index >= 0) {
		cqe = get_cqe_v2(hr_cq, prod_index & hr_cq->ib_cq.cqe);
		if ((roce_get_field(cqe->byte_16, V2_CQE_BYTE_16_LCL_QPN_M,
				    V2_CQE_BYTE_16_LCL_QPN_S) &
				    HNS_ROCE_V2_CQE_QPN_MASK) == qpn) {
			if (srq &&
			    roce_get_bit(cqe->byte_4, V2_CQE_BYTE_4_S_R_S)) {
				wqe_index = roce_get_field(cqe->byte_4,
						     V2_CQE_BYTE_4_WQE_INDX_M,
						     V2_CQE_BYTE_4_WQE_INDX_S);
				hns_roce_free_srq_wqe(srq, wqe_index);
			}
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe_v2(hr_cq, (prod_index + nfreed) &
					  hr_cq->ib_cq.cqe);
			owner_bit = roce_get_bit(dest->byte_4,
						 V2_CQE_BYTE_4_OWNER_S);
			memcpy(dest, cqe, sizeof(*cqe));
			roce_set_bit(dest->byte_4, V2_CQE_BYTE_4_OWNER_S,
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
		hns_roce_v2_cq_set_ci(hr_cq, hr_cq->cons_index);
	}
}

static void hns_roce_v2_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				 struct hns_roce_srq *srq)
{
	spin_lock_irq(&hr_cq->lock);
	__hns_roce_v2_cq_clean(hr_cq, qpn, srq);
	spin_unlock_irq(&hr_cq->lock);
}

static void hns_roce_v2_write_cqc(struct hns_roce_dev *hr_dev,
				  struct hns_roce_cq *hr_cq, void *mb_buf,
				  u64 *mtts, dma_addr_t dma_handle)
{
	struct hns_roce_v2_cq_context *cq_context;

	cq_context = mb_buf;
	memset(cq_context, 0, sizeof(*cq_context));

	roce_set_field(cq_context->byte_4_pg_ceqn, V2_CQC_BYTE_4_CQ_ST_M,
		       V2_CQC_BYTE_4_CQ_ST_S, V2_CQ_STATE_VALID);
	roce_set_field(cq_context->byte_4_pg_ceqn, V2_CQC_BYTE_4_ARM_ST_M,
		       V2_CQC_BYTE_4_ARM_ST_S, REG_NXT_CEQE);
	roce_set_field(cq_context->byte_4_pg_ceqn, V2_CQC_BYTE_4_SHIFT_M,
		       V2_CQC_BYTE_4_SHIFT_S, ilog2(hr_cq->cq_depth));
	roce_set_field(cq_context->byte_4_pg_ceqn, V2_CQC_BYTE_4_CEQN_M,
		       V2_CQC_BYTE_4_CEQN_S, hr_cq->vector);

	roce_set_field(cq_context->byte_8_cqn, V2_CQC_BYTE_8_CQN_M,
		       V2_CQC_BYTE_8_CQN_S, hr_cq->cqn);

	cq_context->cqe_cur_blk_addr = cpu_to_le32(mtts[0] >> PAGE_ADDR_SHIFT);

	roce_set_field(cq_context->byte_16_hop_addr,
		       V2_CQC_BYTE_16_CQE_CUR_BLK_ADDR_M,
		       V2_CQC_BYTE_16_CQE_CUR_BLK_ADDR_S,
		       mtts[0] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(cq_context->byte_16_hop_addr,
		       V2_CQC_BYTE_16_CQE_HOP_NUM_M,
		       V2_CQC_BYTE_16_CQE_HOP_NUM_S, hr_dev->caps.cqe_hop_num ==
		       HNS_ROCE_HOP_NUM_0 ? 0 : hr_dev->caps.cqe_hop_num);

	cq_context->cqe_nxt_blk_addr = cpu_to_le32(mtts[1] >> PAGE_ADDR_SHIFT);
	roce_set_field(cq_context->byte_24_pgsz_addr,
		       V2_CQC_BYTE_24_CQE_NXT_BLK_ADDR_M,
		       V2_CQC_BYTE_24_CQE_NXT_BLK_ADDR_S,
		       mtts[1] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(cq_context->byte_24_pgsz_addr,
		       V2_CQC_BYTE_24_CQE_BA_PG_SZ_M,
		       V2_CQC_BYTE_24_CQE_BA_PG_SZ_S,
		       hr_dev->caps.cqe_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(cq_context->byte_24_pgsz_addr,
		       V2_CQC_BYTE_24_CQE_BUF_PG_SZ_M,
		       V2_CQC_BYTE_24_CQE_BUF_PG_SZ_S,
		       hr_dev->caps.cqe_buf_pg_sz + PG_SHIFT_OFFSET);

	cq_context->cqe_ba = cpu_to_le32(dma_handle >> 3);

	roce_set_field(cq_context->byte_40_cqe_ba, V2_CQC_BYTE_40_CQE_BA_M,
		       V2_CQC_BYTE_40_CQE_BA_S, (dma_handle >> (32 + 3)));

	if (hr_cq->db_en)
		roce_set_bit(cq_context->byte_44_db_record,
			     V2_CQC_BYTE_44_DB_RECORD_EN_S, 1);

	roce_set_field(cq_context->byte_44_db_record,
		       V2_CQC_BYTE_44_DB_RECORD_ADDR_M,
		       V2_CQC_BYTE_44_DB_RECORD_ADDR_S,
		       ((u32)hr_cq->db.dma) >> 1);
	cq_context->db_record_addr = cpu_to_le32(hr_cq->db.dma >> 32);

	roce_set_field(cq_context->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_MAX_CNT_M,
		       V2_CQC_BYTE_56_CQ_MAX_CNT_S,
		       HNS_ROCE_V2_CQ_DEFAULT_BURST_NUM);
	roce_set_field(cq_context->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_PERIOD_M,
		       V2_CQC_BYTE_56_CQ_PERIOD_S,
		       HNS_ROCE_V2_CQ_DEFAULT_INTERVAL);
}

static int hns_roce_v2_req_notify_cq(struct ib_cq *ibcq,
				     enum ib_cq_notify_flags flags)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibcq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	u32 notification_flag;
	__le32 doorbell[2];

	doorbell[0] = 0;
	doorbell[1] = 0;

	notification_flag = (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
			     V2_CQ_DB_REQ_NOT : V2_CQ_DB_REQ_NOT_SOL;
	/*
	 * flags = 0; Notification Flag = 1, next
	 * flags = 1; Notification Flag = 0, solocited
	 */
	roce_set_field(doorbell[0], V2_CQ_DB_BYTE_4_TAG_M, V2_DB_BYTE_4_TAG_S,
		       hr_cq->cqn);
	roce_set_field(doorbell[0], V2_CQ_DB_BYTE_4_CMD_M, V2_DB_BYTE_4_CMD_S,
		       HNS_ROCE_V2_CQ_DB_NTR);
	roce_set_field(doorbell[1], V2_CQ_DB_PARAMETER_CONS_IDX_M,
		       V2_CQ_DB_PARAMETER_CONS_IDX_S,
		       hr_cq->cons_index & ((hr_cq->cq_depth << 1) - 1));
	roce_set_field(doorbell[1], V2_CQ_DB_PARAMETER_CMD_SN_M,
		       V2_CQ_DB_PARAMETER_CMD_SN_S, hr_cq->arm_sn & 0x3);
	roce_set_bit(doorbell[1], V2_CQ_DB_PARAMETER_NOTIFY_S,
		     notification_flag);

	hns_roce_write64(hr_dev, doorbell, hr_cq->cq_db_l);

	return 0;
}

static int hns_roce_handle_recv_inl_wqe(struct hns_roce_v2_cqe *cqe,
						    struct hns_roce_qp **cur_qp,
						    struct ib_wc *wc)
{
	struct hns_roce_rinl_sge *sge_list;
	u32 wr_num, wr_cnt, sge_num;
	u32 sge_cnt, data_len, size;
	void *wqe_buf;

	wr_num = roce_get_field(cqe->byte_4, V2_CQE_BYTE_4_WQE_INDX_M,
				V2_CQE_BYTE_4_WQE_INDX_S) & 0xffff;
	wr_cnt = wr_num & ((*cur_qp)->rq.wqe_cnt - 1);

	sge_list = (*cur_qp)->rq_inl_buf.wqe_list[wr_cnt].sg_list;
	sge_num = (*cur_qp)->rq_inl_buf.wqe_list[wr_cnt].sge_cnt;
	wqe_buf = hns_roce_get_recv_wqe(*cur_qp, wr_cnt);
	data_len = wc->byte_len;

	for (sge_cnt = 0; (sge_cnt < sge_num) && (data_len); sge_cnt++) {
		size = min(sge_list[sge_cnt].len, data_len);
		memcpy((void *)sge_list[sge_cnt].addr, wqe_buf, size);

		data_len -= size;
		wqe_buf += size;
	}

	if (data_len) {
		wc->status = IB_WC_LOC_LEN_ERR;
		return -EAGAIN;
	}

	return 0;
}

static int sw_comp(struct hns_roce_qp *hr_qp, struct hns_roce_wq *wq,
		   int num_entries, struct ib_wc *wc)
{
	unsigned int left;
	int npolled = 0;

	left = wq->head - wq->tail;
	if (left == 0)
		return 0;

	left = min_t(unsigned int, (unsigned int)num_entries, left);
	while (npolled < left) {
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		wc->status = IB_WC_WR_FLUSH_ERR;
		wc->vendor_err = 0;
		wc->qp = &hr_qp->ibqp;

		wq->tail++;
		wc++;
		npolled++;
	}

	return npolled;
}

static int hns_roce_v2_sw_poll_cq(struct hns_roce_cq *hr_cq, int num_entries,
				  struct ib_wc *wc)
{
	struct hns_roce_qp *hr_qp;
	int npolled = 0;

	list_for_each_entry(hr_qp, &hr_cq->sq_list, sq_node) {
		npolled += sw_comp(hr_qp, &hr_qp->sq,
				   num_entries - npolled, wc + npolled);
		if (npolled >= num_entries)
			goto out;
	}

	list_for_each_entry(hr_qp, &hr_cq->rq_list, rq_node) {
		npolled += sw_comp(hr_qp, &hr_qp->rq,
				   num_entries - npolled, wc + npolled);
		if (npolled >= num_entries)
			goto out;
	}

out:
	return npolled;
}

static int hns_roce_v2_poll_one(struct hns_roce_cq *hr_cq,
				struct hns_roce_qp **cur_qp, struct ib_wc *wc)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_cq->ib_cq.device);
	struct hns_roce_srq *srq = NULL;
	struct hns_roce_v2_cqe *cqe;
	struct hns_roce_qp *hr_qp;
	struct hns_roce_wq *wq;
	int is_send;
	u16 wqe_ctr;
	u32 opcode;
	u32 status;
	int qpn;
	int ret;

	/* Find cqe according to consumer index */
	cqe = next_cqe_sw_v2(hr_cq);
	if (!cqe)
		return -EAGAIN;

	++hr_cq->cons_index;
	/* Memory barrier */
	rmb();

	/* 0->SQ, 1->RQ */
	is_send = !roce_get_bit(cqe->byte_4, V2_CQE_BYTE_4_S_R_S);

	qpn = roce_get_field(cqe->byte_16, V2_CQE_BYTE_16_LCL_QPN_M,
				V2_CQE_BYTE_16_LCL_QPN_S);

	if (!*cur_qp || (qpn & HNS_ROCE_V2_CQE_QPN_MASK) != (*cur_qp)->qpn) {
		hr_qp = __hns_roce_qp_lookup(hr_dev, qpn);
		if (unlikely(!hr_qp)) {
			ibdev_err(&hr_dev->ib_dev,
				  "CQ %06lx with entry for unknown QPN %06x\n",
				  hr_cq->cqn, qpn & HNS_ROCE_V2_CQE_QPN_MASK);
			return -EINVAL;
		}
		*cur_qp = hr_qp;
	}

	hr_qp = *cur_qp;
	wc->qp = &(*cur_qp)->ibqp;
	wc->vendor_err = 0;

	if (is_send) {
		wq = &(*cur_qp)->sq;
		if ((*cur_qp)->sq_signal_bits) {
			/*
			 * If sg_signal_bit is 1,
			 * firstly tail pointer updated to wqe
			 * which current cqe correspond to
			 */
			wqe_ctr = (u16)roce_get_field(cqe->byte_4,
						      V2_CQE_BYTE_4_WQE_INDX_M,
						      V2_CQE_BYTE_4_WQE_INDX_S);
			wq->tail += (wqe_ctr - (u16)wq->tail) &
				    (wq->wqe_cnt - 1);
		}

		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	} else if ((*cur_qp)->ibqp.srq) {
		srq = to_hr_srq((*cur_qp)->ibqp.srq);
		wqe_ctr = (u16)roce_get_field(cqe->byte_4,
					      V2_CQE_BYTE_4_WQE_INDX_M,
					      V2_CQE_BYTE_4_WQE_INDX_S);
		wc->wr_id = srq->wrid[wqe_ctr];
		hns_roce_free_srq_wqe(srq, wqe_ctr);
	} else {
		/* Update tail pointer, record wr_id */
		wq = &(*cur_qp)->rq;
		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;
	}

	status = roce_get_field(cqe->byte_4, V2_CQE_BYTE_4_STATUS_M,
				V2_CQE_BYTE_4_STATUS_S);
	switch (status & HNS_ROCE_V2_CQE_STATUS_MASK) {
	case HNS_ROCE_CQE_V2_SUCCESS:
		wc->status = IB_WC_SUCCESS;
		break;
	case HNS_ROCE_CQE_V2_LOCAL_LENGTH_ERR:
		wc->status = IB_WC_LOC_LEN_ERR;
		break;
	case HNS_ROCE_CQE_V2_LOCAL_QP_OP_ERR:
		wc->status = IB_WC_LOC_QP_OP_ERR;
		break;
	case HNS_ROCE_CQE_V2_LOCAL_PROT_ERR:
		wc->status = IB_WC_LOC_PROT_ERR;
		break;
	case HNS_ROCE_CQE_V2_WR_FLUSH_ERR:
		wc->status = IB_WC_WR_FLUSH_ERR;
		break;
	case HNS_ROCE_CQE_V2_MW_BIND_ERR:
		wc->status = IB_WC_MW_BIND_ERR;
		break;
	case HNS_ROCE_CQE_V2_BAD_RESP_ERR:
		wc->status = IB_WC_BAD_RESP_ERR;
		break;
	case HNS_ROCE_CQE_V2_LOCAL_ACCESS_ERR:
		wc->status = IB_WC_LOC_ACCESS_ERR;
		break;
	case HNS_ROCE_CQE_V2_REMOTE_INVAL_REQ_ERR:
		wc->status = IB_WC_REM_INV_REQ_ERR;
		break;
	case HNS_ROCE_CQE_V2_REMOTE_ACCESS_ERR:
		wc->status = IB_WC_REM_ACCESS_ERR;
		break;
	case HNS_ROCE_CQE_V2_REMOTE_OP_ERR:
		wc->status = IB_WC_REM_OP_ERR;
		break;
	case HNS_ROCE_CQE_V2_TRANSPORT_RETRY_EXC_ERR:
		wc->status = IB_WC_RETRY_EXC_ERR;
		break;
	case HNS_ROCE_CQE_V2_RNR_RETRY_EXC_ERR:
		wc->status = IB_WC_RNR_RETRY_EXC_ERR;
		break;
	case HNS_ROCE_CQE_V2_REMOTE_ABORT_ERR:
		wc->status = IB_WC_REM_ABORT_ERR;
		break;
	default:
		wc->status = IB_WC_GENERAL_ERR;
		break;
	}

	/*
	 * Hip08 hardware cannot flush the WQEs in SQ/RQ if the QP state gets
	 * into errored mode. Hence, as a workaround to this hardware
	 * limitation, driver needs to assist in flushing. But the flushing
	 * operation uses mailbox to convey the QP state to the hardware and
	 * which can sleep due to the mutex protection around the mailbox calls.
	 * Hence, use the deferred flush for now. Once wc error detected, the
	 * flushing operation is needed.
	 */
	if (wc->status != IB_WC_SUCCESS &&
	    wc->status != IB_WC_WR_FLUSH_ERR) {
		ibdev_err(&hr_dev->ib_dev, "error cqe status is: 0x%x\n",
			  status & HNS_ROCE_V2_CQE_STATUS_MASK);

		if (!test_and_set_bit(HNS_ROCE_FLUSH_FLAG, &hr_qp->flush_flag))
			init_flush_work(hr_dev, hr_qp);

		return 0;
	}

	if (wc->status == IB_WC_WR_FLUSH_ERR)
		return 0;

	if (is_send) {
		wc->wc_flags = 0;
		/* SQ corresponding to CQE */
		switch (roce_get_field(cqe->byte_4, V2_CQE_BYTE_4_OPCODE_M,
				       V2_CQE_BYTE_4_OPCODE_S) & 0x1f) {
		case HNS_ROCE_SQ_OPCODE_SEND:
			wc->opcode = IB_WC_SEND;
			break;
		case HNS_ROCE_SQ_OPCODE_SEND_WITH_INV:
			wc->opcode = IB_WC_SEND;
			break;
		case HNS_ROCE_SQ_OPCODE_SEND_WITH_IMM:
			wc->opcode = IB_WC_SEND;
			wc->wc_flags |= IB_WC_WITH_IMM;
			break;
		case HNS_ROCE_SQ_OPCODE_RDMA_READ:
			wc->opcode = IB_WC_RDMA_READ;
			wc->byte_len = le32_to_cpu(cqe->byte_cnt);
			break;
		case HNS_ROCE_SQ_OPCODE_RDMA_WRITE:
			wc->opcode = IB_WC_RDMA_WRITE;
			break;
		case HNS_ROCE_SQ_OPCODE_RDMA_WRITE_WITH_IMM:
			wc->opcode = IB_WC_RDMA_WRITE;
			wc->wc_flags |= IB_WC_WITH_IMM;
			break;
		case HNS_ROCE_SQ_OPCODE_LOCAL_INV:
			wc->opcode = IB_WC_LOCAL_INV;
			wc->wc_flags |= IB_WC_WITH_INVALIDATE;
			break;
		case HNS_ROCE_SQ_OPCODE_ATOMIC_COMP_AND_SWAP:
			wc->opcode = IB_WC_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case HNS_ROCE_SQ_OPCODE_ATOMIC_FETCH_AND_ADD:
			wc->opcode = IB_WC_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case HNS_ROCE_SQ_OPCODE_ATOMIC_MASK_COMP_AND_SWAP:
			wc->opcode = IB_WC_MASKED_COMP_SWAP;
			wc->byte_len  = 8;
			break;
		case HNS_ROCE_SQ_OPCODE_ATOMIC_MASK_FETCH_AND_ADD:
			wc->opcode = IB_WC_MASKED_FETCH_ADD;
			wc->byte_len  = 8;
			break;
		case HNS_ROCE_SQ_OPCODE_FAST_REG_WR:
			wc->opcode = IB_WC_REG_MR;
			break;
		case HNS_ROCE_SQ_OPCODE_BIND_MW:
			wc->opcode = IB_WC_REG_MR;
			break;
		default:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		}
	} else {
		/* RQ correspond to CQE */
		wc->byte_len = le32_to_cpu(cqe->byte_cnt);

		opcode = roce_get_field(cqe->byte_4, V2_CQE_BYTE_4_OPCODE_M,
					V2_CQE_BYTE_4_OPCODE_S);
		switch (opcode & 0x1f) {
		case HNS_ROCE_V2_OPCODE_RDMA_WRITE_IMM:
			wc->opcode = IB_WC_RECV_RDMA_WITH_IMM;
			wc->wc_flags = IB_WC_WITH_IMM;
			wc->ex.imm_data =
				cpu_to_be32(le32_to_cpu(cqe->immtdata));
			break;
		case HNS_ROCE_V2_OPCODE_SEND:
			wc->opcode = IB_WC_RECV;
			wc->wc_flags = 0;
			break;
		case HNS_ROCE_V2_OPCODE_SEND_WITH_IMM:
			wc->opcode = IB_WC_RECV;
			wc->wc_flags = IB_WC_WITH_IMM;
			wc->ex.imm_data =
				cpu_to_be32(le32_to_cpu(cqe->immtdata));
			break;
		case HNS_ROCE_V2_OPCODE_SEND_WITH_INV:
			wc->opcode = IB_WC_RECV;
			wc->wc_flags = IB_WC_WITH_INVALIDATE;
			wc->ex.invalidate_rkey = le32_to_cpu(cqe->rkey);
			break;
		default:
			wc->status = IB_WC_GENERAL_ERR;
			break;
		}

		if ((wc->qp->qp_type == IB_QPT_RC ||
		     wc->qp->qp_type == IB_QPT_UC) &&
		    (opcode == HNS_ROCE_V2_OPCODE_SEND ||
		    opcode == HNS_ROCE_V2_OPCODE_SEND_WITH_IMM ||
		    opcode == HNS_ROCE_V2_OPCODE_SEND_WITH_INV) &&
		    (roce_get_bit(cqe->byte_4, V2_CQE_BYTE_4_RQ_INLINE_S))) {
			ret = hns_roce_handle_recv_inl_wqe(cqe, cur_qp, wc);
			if (ret)
				return -EAGAIN;
		}

		wc->sl = (u8)roce_get_field(cqe->byte_32, V2_CQE_BYTE_32_SL_M,
					    V2_CQE_BYTE_32_SL_S);
		wc->src_qp = (u8)roce_get_field(cqe->byte_32,
						V2_CQE_BYTE_32_RMT_QPN_M,
						V2_CQE_BYTE_32_RMT_QPN_S);
		wc->slid = 0;
		wc->wc_flags |= (roce_get_bit(cqe->byte_32,
					      V2_CQE_BYTE_32_GRH_S) ?
					      IB_WC_GRH : 0);
		wc->port_num = roce_get_field(cqe->byte_32,
				V2_CQE_BYTE_32_PORTN_M, V2_CQE_BYTE_32_PORTN_S);
		wc->pkey_index = 0;

		if (roce_get_bit(cqe->byte_28, V2_CQE_BYTE_28_VID_VLD_S)) {
			wc->vlan_id = (u16)roce_get_field(cqe->byte_28,
							  V2_CQE_BYTE_28_VID_M,
							  V2_CQE_BYTE_28_VID_S);
			wc->wc_flags |= IB_WC_WITH_VLAN;
		} else {
			wc->vlan_id = 0xffff;
		}

		wc->network_hdr_type = roce_get_field(cqe->byte_28,
						    V2_CQE_BYTE_28_PORT_TYPE_M,
						    V2_CQE_BYTE_28_PORT_TYPE_S);
	}

	return 0;
}

static int hns_roce_v2_poll_cq(struct ib_cq *ibcq, int num_entries,
			       struct ib_wc *wc)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibcq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	struct hns_roce_qp *cur_qp = NULL;
	unsigned long flags;
	int npolled;

	spin_lock_irqsave(&hr_cq->lock, flags);

	/*
	 * When the device starts to reset, the state is RST_DOWN. At this time,
	 * there may still be some valid CQEs in the hardware that are not
	 * polled. Therefore, it is not allowed to switch to the software mode
	 * immediately. When the state changes to UNINIT, CQE no longer exists
	 * in the hardware, and then switch to software mode.
	 */
	if (hr_dev->state == HNS_ROCE_DEVICE_STATE_UNINIT) {
		npolled = hns_roce_v2_sw_poll_cq(hr_cq, num_entries, wc);
		goto out;
	}

	for (npolled = 0; npolled < num_entries; ++npolled) {
		if (hns_roce_v2_poll_one(hr_cq, &cur_qp, wc + npolled))
			break;
	}

	if (npolled) {
		/* Memory barrier */
		wmb();
		hns_roce_v2_cq_set_ci(hr_cq, hr_cq->cons_index);
	}

out:
	spin_unlock_irqrestore(&hr_cq->lock, flags);

	return npolled;
}

static int get_op_for_set_hem(struct hns_roce_dev *hr_dev, u32 type,
			      int step_idx)
{
	int op;

	if (type == HEM_TYPE_SCCC && step_idx)
		return -EINVAL;

	switch (type) {
	case HEM_TYPE_QPC:
		op = HNS_ROCE_CMD_WRITE_QPC_BT0;
		break;
	case HEM_TYPE_MTPT:
		op = HNS_ROCE_CMD_WRITE_MPT_BT0;
		break;
	case HEM_TYPE_CQC:
		op = HNS_ROCE_CMD_WRITE_CQC_BT0;
		break;
	case HEM_TYPE_SRQC:
		op = HNS_ROCE_CMD_WRITE_SRQC_BT0;
		break;
	case HEM_TYPE_SCCC:
		op = HNS_ROCE_CMD_WRITE_SCCC_BT0;
		break;
	case HEM_TYPE_QPC_TIMER:
		op = HNS_ROCE_CMD_WRITE_QPC_TIMER_BT0;
		break;
	case HEM_TYPE_CQC_TIMER:
		op = HNS_ROCE_CMD_WRITE_CQC_TIMER_BT0;
		break;
	default:
		dev_warn(hr_dev->dev,
			 "Table %d not to be written by mailbox!\n", type);
		return -EINVAL;
	}

	return op + step_idx;
}

static int hns_roce_v2_set_hem(struct hns_roce_dev *hr_dev,
			       struct hns_roce_hem_table *table, int obj,
			       int step_idx)
{
	struct hns_roce_cmd_mailbox *mailbox;
	struct hns_roce_hem_iter iter;
	struct hns_roce_hem_mhop mhop;
	struct hns_roce_hem *hem;
	unsigned long mhop_obj = obj;
	int i, j, k;
	int ret = 0;
	u64 hem_idx = 0;
	u64 l1_idx = 0;
	u64 bt_ba = 0;
	u32 chunk_ba_num;
	u32 hop_num;
	int op;

	if (!hns_roce_check_whether_mhop(hr_dev, table->type))
		return 0;

	hns_roce_calc_hem_mhop(hr_dev, table, &mhop_obj, &mhop);
	i = mhop.l0_idx;
	j = mhop.l1_idx;
	k = mhop.l2_idx;
	hop_num = mhop.hop_num;
	chunk_ba_num = mhop.bt_chunk_size / 8;

	if (hop_num == 2) {
		hem_idx = i * chunk_ba_num * chunk_ba_num + j * chunk_ba_num +
			  k;
		l1_idx = i * chunk_ba_num + j;
	} else if (hop_num == 1) {
		hem_idx = i * chunk_ba_num + j;
	} else if (hop_num == HNS_ROCE_HOP_NUM_0) {
		hem_idx = i;
	}

	op = get_op_for_set_hem(hr_dev, table->type, step_idx);
	if (op == -EINVAL)
		return 0;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	if (table->type == HEM_TYPE_SCCC)
		obj = mhop.l0_idx;

	if (check_whether_last_step(hop_num, step_idx)) {
		hem = table->hem[hem_idx];
		for (hns_roce_hem_first(hem, &iter);
		     !hns_roce_hem_last(&iter); hns_roce_hem_next(&iter)) {
			bt_ba = hns_roce_hem_addr(&iter);

			/* configure the ba, tag, and op */
			ret = hns_roce_cmd_mbox(hr_dev, bt_ba, mailbox->dma,
						obj, 0, op,
						HNS_ROCE_CMD_TIMEOUT_MSECS);
		}
	} else {
		if (step_idx == 0)
			bt_ba = table->bt_l0_dma_addr[i];
		else if (step_idx == 1 && hop_num == 2)
			bt_ba = table->bt_l1_dma_addr[l1_idx];

		/* configure the ba, tag, and op */
		ret = hns_roce_cmd_mbox(hr_dev, bt_ba, mailbox->dma, obj,
					0, op, HNS_ROCE_CMD_TIMEOUT_MSECS);
	}

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v2_clear_hem(struct hns_roce_dev *hr_dev,
				 struct hns_roce_hem_table *table, int obj,
				 int step_idx)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;
	u16 op = 0xff;

	if (!hns_roce_check_whether_mhop(hr_dev, table->type))
		return 0;

	switch (table->type) {
	case HEM_TYPE_QPC:
		op = HNS_ROCE_CMD_DESTROY_QPC_BT0;
		break;
	case HEM_TYPE_MTPT:
		op = HNS_ROCE_CMD_DESTROY_MPT_BT0;
		break;
	case HEM_TYPE_CQC:
		op = HNS_ROCE_CMD_DESTROY_CQC_BT0;
		break;
	case HEM_TYPE_SCCC:
	case HEM_TYPE_QPC_TIMER:
	case HEM_TYPE_CQC_TIMER:
		break;
	case HEM_TYPE_SRQC:
		op = HNS_ROCE_CMD_DESTROY_SRQC_BT0;
		break;
	default:
		dev_warn(dev, "Table %d not to be destroyed by mailbox!\n",
			 table->type);
		return 0;
	}

	if (table->type == HEM_TYPE_SCCC ||
	    table->type == HEM_TYPE_QPC_TIMER ||
	    table->type == HEM_TYPE_CQC_TIMER)
		return 0;

	op += step_idx;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	/* configure the tag and op */
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, obj, 0, op,
				HNS_ROCE_CMD_TIMEOUT_MSECS);

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v2_qp_modify(struct hns_roce_dev *hr_dev,
				 struct hns_roce_v2_qp_context *context,
				 struct hns_roce_qp *hr_qp)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	memcpy(mailbox->buf, context, sizeof(*context) * 2);

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, hr_qp->qpn, 0,
				HNS_ROCE_CMD_MODIFY_QPC,
				HNS_ROCE_CMD_TIMEOUT_MSECS);

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

static void set_access_flags(struct hns_roce_qp *hr_qp,
			     struct hns_roce_v2_qp_context *context,
			     struct hns_roce_v2_qp_context *qpc_mask,
			     const struct ib_qp_attr *attr, int attr_mask)
{
	u8 dest_rd_atomic;
	u32 access_flags;

	dest_rd_atomic = (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) ?
			 attr->max_dest_rd_atomic : hr_qp->resp_depth;

	access_flags = (attr_mask & IB_QP_ACCESS_FLAGS) ?
		       attr->qp_access_flags : hr_qp->atomic_rd_en;

	if (!dest_rd_atomic)
		access_flags &= IB_ACCESS_REMOTE_WRITE;

	roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S,
		     !!(access_flags & IB_ACCESS_REMOTE_READ));
	roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S, 0);

	roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S,
		     !!(access_flags & IB_ACCESS_REMOTE_WRITE));
	roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S, 0);

	roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S,
		     !!(access_flags & IB_ACCESS_REMOTE_ATOMIC));
	roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S, 0);
	roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_EXT_ATE_S,
		     !!(access_flags & IB_ACCESS_REMOTE_ATOMIC));
	roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_EXT_ATE_S, 0);
}

static void set_qpc_wqe_cnt(struct hns_roce_qp *hr_qp,
			    struct hns_roce_v2_qp_context *context,
			    struct hns_roce_v2_qp_context *qpc_mask)
{
	if (hr_qp->ibqp.qp_type == IB_QPT_GSI)
		roce_set_field(context->byte_4_sqpn_tst,
			       V2_QPC_BYTE_4_SGE_SHIFT_M,
			       V2_QPC_BYTE_4_SGE_SHIFT_S,
			       ilog2((unsigned int)hr_qp->sge.sge_cnt));
	else
		roce_set_field(context->byte_4_sqpn_tst,
			       V2_QPC_BYTE_4_SGE_SHIFT_M,
			       V2_QPC_BYTE_4_SGE_SHIFT_S,
			       hr_qp->sq.max_gs >
			       HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE ?
			       ilog2((unsigned int)hr_qp->sge.sge_cnt) : 0);

	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SQ_SHIFT_M, V2_QPC_BYTE_20_SQ_SHIFT_S,
		       ilog2((unsigned int)hr_qp->sq.wqe_cnt));

	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_RQ_SHIFT_M, V2_QPC_BYTE_20_RQ_SHIFT_S,
		       (hr_qp->ibqp.qp_type == IB_QPT_XRC_INI ||
		       hr_qp->ibqp.qp_type == IB_QPT_XRC_TGT ||
		       hr_qp->ibqp.srq) ? 0 :
		       ilog2((unsigned int)hr_qp->rq.wqe_cnt));
}

static void modify_qp_reset_to_init(struct ib_qp *ibqp,
				    const struct ib_qp_attr *attr,
				    int attr_mask,
				    struct hns_roce_v2_qp_context *context,
				    struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	roce_set_field(context->byte_4_sqpn_tst, V2_QPC_BYTE_4_TST_M,
		       V2_QPC_BYTE_4_TST_S, to_hr_qp_type(hr_qp->ibqp.qp_type));

	roce_set_field(context->byte_4_sqpn_tst, V2_QPC_BYTE_4_SQPN_M,
		       V2_QPC_BYTE_4_SQPN_S, hr_qp->qpn);

	roce_set_field(context->byte_16_buf_ba_pg_sz, V2_QPC_BYTE_16_PD_M,
		       V2_QPC_BYTE_16_PD_S, to_hr_pd(ibqp->pd)->pdn);

	roce_set_field(context->byte_20_smac_sgid_idx, V2_QPC_BYTE_20_RQWS_M,
		       V2_QPC_BYTE_20_RQWS_S, ilog2(hr_qp->rq.max_gs));

	set_qpc_wqe_cnt(hr_qp, context, qpc_mask);

	/* No VLAN need to set 0xFFF */
	roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_VLAN_ID_M,
		       V2_QPC_BYTE_24_VLAN_ID_S, 0xfff);

	if (hr_qp->rdb_en)
		roce_set_bit(context->byte_68_rq_db,
			     V2_QPC_BYTE_68_RQ_RECORD_EN_S, 1);

	roce_set_field(context->byte_68_rq_db,
		       V2_QPC_BYTE_68_RQ_DB_RECORD_ADDR_M,
		       V2_QPC_BYTE_68_RQ_DB_RECORD_ADDR_S,
		       ((u32)hr_qp->rdb.dma) >> 1);
	context->rq_db_record_addr = cpu_to_le32(hr_qp->rdb.dma >> 32);

	roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RQIE_S,
		    (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_RQ_INLINE) ? 1 : 0);

	roce_set_field(context->byte_80_rnr_rx_cqn, V2_QPC_BYTE_80_RX_CQN_M,
		       V2_QPC_BYTE_80_RX_CQN_S, to_hr_cq(ibqp->recv_cq)->cqn);
	if (ibqp->srq) {
		roce_set_field(context->byte_76_srqn_op_en,
			       V2_QPC_BYTE_76_SRQN_M, V2_QPC_BYTE_76_SRQN_S,
			       to_hr_srq(ibqp->srq)->srqn);
		roce_set_bit(context->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_SRQ_EN_S, 1);
	}

	roce_set_field(context->byte_172_sq_psn, V2_QPC_BYTE_172_ACK_REQ_FREQ_M,
		       V2_QPC_BYTE_172_ACK_REQ_FREQ_S, 4);

	roce_set_bit(context->byte_172_sq_psn, V2_QPC_BYTE_172_FRE_S, 1);

	hr_qp->access_flags = attr->qp_access_flags;
	roce_set_field(context->byte_252_err_txcqn, V2_QPC_BYTE_252_TX_CQN_M,
		       V2_QPC_BYTE_252_TX_CQN_S, to_hr_cq(ibqp->send_cq)->cqn);
}

static void modify_qp_init_to_init(struct ib_qp *ibqp,
				   const struct ib_qp_attr *attr, int attr_mask,
				   struct hns_roce_v2_qp_context *context,
				   struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	roce_set_field(context->byte_4_sqpn_tst, V2_QPC_BYTE_4_TST_M,
		       V2_QPC_BYTE_4_TST_S, to_hr_qp_type(hr_qp->ibqp.qp_type));
	roce_set_field(qpc_mask->byte_4_sqpn_tst, V2_QPC_BYTE_4_TST_M,
		       V2_QPC_BYTE_4_TST_S, 0);

	if (attr_mask & IB_QP_ACCESS_FLAGS) {
		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S,
			     !!(attr->qp_access_flags & IB_ACCESS_REMOTE_READ));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S,
			     0);

		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S,
			     !!(attr->qp_access_flags &
			     IB_ACCESS_REMOTE_WRITE));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S,
			     0);

		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S,
			     !!(attr->qp_access_flags &
			     IB_ACCESS_REMOTE_ATOMIC));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S,
			     0);
		roce_set_bit(context->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_EXT_ATE_S,
			     !!(attr->qp_access_flags &
				IB_ACCESS_REMOTE_ATOMIC));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_EXT_ATE_S, 0);
	} else {
		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S,
			     !!(hr_qp->access_flags & IB_ACCESS_REMOTE_READ));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RRE_S,
			     0);

		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S,
			     !!(hr_qp->access_flags & IB_ACCESS_REMOTE_WRITE));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_RWE_S,
			     0);

		roce_set_bit(context->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S,
			     !!(hr_qp->access_flags & IB_ACCESS_REMOTE_ATOMIC));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en, V2_QPC_BYTE_76_ATE_S,
			     0);
		roce_set_bit(context->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_EXT_ATE_S,
			     !!(hr_qp->access_flags & IB_ACCESS_REMOTE_ATOMIC));
		roce_set_bit(qpc_mask->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_EXT_ATE_S, 0);
	}

	roce_set_field(context->byte_16_buf_ba_pg_sz, V2_QPC_BYTE_16_PD_M,
		       V2_QPC_BYTE_16_PD_S, to_hr_pd(ibqp->pd)->pdn);
	roce_set_field(qpc_mask->byte_16_buf_ba_pg_sz, V2_QPC_BYTE_16_PD_M,
		       V2_QPC_BYTE_16_PD_S, 0);

	roce_set_field(context->byte_80_rnr_rx_cqn, V2_QPC_BYTE_80_RX_CQN_M,
		       V2_QPC_BYTE_80_RX_CQN_S, to_hr_cq(ibqp->recv_cq)->cqn);
	roce_set_field(qpc_mask->byte_80_rnr_rx_cqn, V2_QPC_BYTE_80_RX_CQN_M,
		       V2_QPC_BYTE_80_RX_CQN_S, 0);

	roce_set_field(context->byte_252_err_txcqn, V2_QPC_BYTE_252_TX_CQN_M,
		       V2_QPC_BYTE_252_TX_CQN_S, to_hr_cq(ibqp->send_cq)->cqn);
	roce_set_field(qpc_mask->byte_252_err_txcqn, V2_QPC_BYTE_252_TX_CQN_M,
		       V2_QPC_BYTE_252_TX_CQN_S, 0);

	if (ibqp->srq) {
		roce_set_bit(context->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_SRQ_EN_S, 1);
		roce_set_bit(qpc_mask->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_SRQ_EN_S, 0);
		roce_set_field(context->byte_76_srqn_op_en,
			       V2_QPC_BYTE_76_SRQN_M, V2_QPC_BYTE_76_SRQN_S,
			       to_hr_srq(ibqp->srq)->srqn);
		roce_set_field(qpc_mask->byte_76_srqn_op_en,
			       V2_QPC_BYTE_76_SRQN_M, V2_QPC_BYTE_76_SRQN_S, 0);
	}

	roce_set_field(context->byte_4_sqpn_tst, V2_QPC_BYTE_4_SQPN_M,
		       V2_QPC_BYTE_4_SQPN_S, hr_qp->qpn);
	roce_set_field(qpc_mask->byte_4_sqpn_tst, V2_QPC_BYTE_4_SQPN_M,
		       V2_QPC_BYTE_4_SQPN_S, 0);

	if (attr_mask & IB_QP_DEST_QPN) {
		roce_set_field(context->byte_56_dqpn_err, V2_QPC_BYTE_56_DQPN_M,
			       V2_QPC_BYTE_56_DQPN_S, hr_qp->qpn);
		roce_set_field(qpc_mask->byte_56_dqpn_err,
			       V2_QPC_BYTE_56_DQPN_M, V2_QPC_BYTE_56_DQPN_S, 0);
	}
}

static bool check_wqe_rq_mtt_count(struct hns_roce_dev *hr_dev,
				   struct hns_roce_qp *hr_qp, int mtt_cnt,
				   u32 page_size)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;

	if (hr_qp->rq.wqe_cnt < 1)
		return true;

	if (mtt_cnt < 1) {
		ibdev_err(ibdev, "failed to find RQWQE buf ba of QP(0x%lx)\n",
			  hr_qp->qpn);
		return false;
	}

	if (mtt_cnt < MTT_MIN_COUNT &&
		(hr_qp->rq.offset + page_size) < hr_qp->buff_size) {
		ibdev_err(ibdev,
			  "failed to find next RQWQE buf ba of QP(0x%lx)\n",
			  hr_qp->qpn);
		return false;
	}

	return true;
}

static int modify_qp_init_to_rtr(struct ib_qp *ibqp,
				 const struct ib_qp_attr *attr, int attr_mask,
				 struct hns_roce_v2_qp_context *context,
				 struct hns_roce_v2_qp_context *qpc_mask)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u64 mtts[MTT_MIN_COUNT] = { 0 };
	dma_addr_t dma_handle_3;
	dma_addr_t dma_handle_2;
	u64 wqe_sge_ba;
	u32 page_size;
	u8 port_num;
	u64 *mtts_3;
	u64 *mtts_2;
	int count;
	u8 *dmac;
	u8 *smac;
	int port;

	/* Search qp buf's mtts */
	page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
	count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr,
				  hr_qp->rq.offset / page_size, mtts,
				  MTT_MIN_COUNT, &wqe_sge_ba);
	if (!ibqp->srq)
		if (!check_wqe_rq_mtt_count(hr_dev, hr_qp, count, page_size))
			return -EINVAL;

	/* Search IRRL's mtts */
	mtts_2 = hns_roce_table_find(hr_dev, &hr_dev->qp_table.irrl_table,
				     hr_qp->qpn, &dma_handle_2);
	if (!mtts_2) {
		ibdev_err(ibdev, "failed to find QP irrl_table\n");
		return -EINVAL;
	}

	/* Search TRRL's mtts */
	mtts_3 = hns_roce_table_find(hr_dev, &hr_dev->qp_table.trrl_table,
				     hr_qp->qpn, &dma_handle_3);
	if (!mtts_3) {
		ibdev_err(ibdev, "failed to find QP trrl_table\n");
		return -EINVAL;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		ibdev_err(ibdev, "INIT2RTR attr_mask (0x%x) error\n",
			  attr_mask);
		return -EINVAL;
	}

	dmac = (u8 *)attr->ah_attr.roce.dmac;
	context->wqe_sge_ba = cpu_to_le32(wqe_sge_ba >> 3);
	qpc_mask->wqe_sge_ba = 0;

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	roce_set_field(context->byte_12_sq_hop, V2_QPC_BYTE_12_WQE_SGE_BA_M,
		       V2_QPC_BYTE_12_WQE_SGE_BA_S, wqe_sge_ba >> (32 + 3));
	roce_set_field(qpc_mask->byte_12_sq_hop, V2_QPC_BYTE_12_WQE_SGE_BA_M,
		       V2_QPC_BYTE_12_WQE_SGE_BA_S, 0);

	roce_set_field(context->byte_12_sq_hop, V2_QPC_BYTE_12_SQ_HOP_NUM_M,
		       V2_QPC_BYTE_12_SQ_HOP_NUM_S,
		       hr_dev->caps.wqe_sq_hop_num == HNS_ROCE_HOP_NUM_0 ?
		       0 : hr_dev->caps.wqe_sq_hop_num);
	roce_set_field(qpc_mask->byte_12_sq_hop, V2_QPC_BYTE_12_SQ_HOP_NUM_M,
		       V2_QPC_BYTE_12_SQ_HOP_NUM_S, 0);

	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGE_HOP_NUM_M,
		       V2_QPC_BYTE_20_SGE_HOP_NUM_S,
		       ((ibqp->qp_type == IB_QPT_GSI) ||
		       hr_qp->sq.max_gs > HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE) ?
		       hr_dev->caps.wqe_sge_hop_num : 0);
	roce_set_field(qpc_mask->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGE_HOP_NUM_M,
		       V2_QPC_BYTE_20_SGE_HOP_NUM_S, 0);

	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_RQ_HOP_NUM_M,
		       V2_QPC_BYTE_20_RQ_HOP_NUM_S,
		       hr_dev->caps.wqe_rq_hop_num == HNS_ROCE_HOP_NUM_0 ?
		       0 : hr_dev->caps.wqe_rq_hop_num);
	roce_set_field(qpc_mask->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_RQ_HOP_NUM_M,
		       V2_QPC_BYTE_20_RQ_HOP_NUM_S, 0);

	roce_set_field(context->byte_16_buf_ba_pg_sz,
		       V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_M,
		       V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_S,
		       hr_qp->wqe_bt_pg_shift + PG_SHIFT_OFFSET);
	roce_set_field(qpc_mask->byte_16_buf_ba_pg_sz,
		       V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_M,
		       V2_QPC_BYTE_16_WQE_SGE_BA_PG_SZ_S, 0);

	roce_set_field(context->byte_16_buf_ba_pg_sz,
		       V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_M,
		       V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_S,
		       hr_dev->caps.mtt_buf_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(qpc_mask->byte_16_buf_ba_pg_sz,
		       V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_M,
		       V2_QPC_BYTE_16_WQE_SGE_BUF_PG_SZ_S, 0);

	context->rq_cur_blk_addr = cpu_to_le32(mtts[0] >> PAGE_ADDR_SHIFT);
	qpc_mask->rq_cur_blk_addr = 0;

	roce_set_field(context->byte_92_srq_info,
		       V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_S,
		       mtts[0] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(qpc_mask->byte_92_srq_info,
		       V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_92_RQ_CUR_BLK_ADDR_S, 0);

	context->rq_nxt_blk_addr = cpu_to_le32(mtts[1] >> PAGE_ADDR_SHIFT);
	qpc_mask->rq_nxt_blk_addr = 0;

	roce_set_field(context->byte_104_rq_sge,
		       V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_M,
		       V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_S,
		       mtts[1] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(qpc_mask->byte_104_rq_sge,
		       V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_M,
		       V2_QPC_BYTE_104_RQ_NXT_BLK_ADDR_S, 0);

	roce_set_field(context->byte_132_trrl, V2_QPC_BYTE_132_TRRL_BA_M,
		       V2_QPC_BYTE_132_TRRL_BA_S, dma_handle_3 >> 4);
	roce_set_field(qpc_mask->byte_132_trrl, V2_QPC_BYTE_132_TRRL_BA_M,
		       V2_QPC_BYTE_132_TRRL_BA_S, 0);
	context->trrl_ba = cpu_to_le32(dma_handle_3 >> (16 + 4));
	qpc_mask->trrl_ba = 0;
	roce_set_field(context->byte_140_raq, V2_QPC_BYTE_140_TRRL_BA_M,
		       V2_QPC_BYTE_140_TRRL_BA_S,
		       (u32)(dma_handle_3 >> (32 + 16 + 4)));
	roce_set_field(qpc_mask->byte_140_raq, V2_QPC_BYTE_140_TRRL_BA_M,
		       V2_QPC_BYTE_140_TRRL_BA_S, 0);

	context->irrl_ba = cpu_to_le32(dma_handle_2 >> 6);
	qpc_mask->irrl_ba = 0;
	roce_set_field(context->byte_208_irrl, V2_QPC_BYTE_208_IRRL_BA_M,
		       V2_QPC_BYTE_208_IRRL_BA_S,
		       dma_handle_2 >> (32 + 6));
	roce_set_field(qpc_mask->byte_208_irrl, V2_QPC_BYTE_208_IRRL_BA_M,
		       V2_QPC_BYTE_208_IRRL_BA_S, 0);

	roce_set_bit(context->byte_208_irrl, V2_QPC_BYTE_208_RMT_E2E_S, 1);
	roce_set_bit(qpc_mask->byte_208_irrl, V2_QPC_BYTE_208_RMT_E2E_S, 0);

	roce_set_bit(context->byte_252_err_txcqn, V2_QPC_BYTE_252_SIG_TYPE_S,
		     hr_qp->sq_signal_bits);
	roce_set_bit(qpc_mask->byte_252_err_txcqn, V2_QPC_BYTE_252_SIG_TYPE_S,
		     0);

	port = (attr_mask & IB_QP_PORT) ? (attr->port_num - 1) : hr_qp->port;

	smac = (u8 *)hr_dev->dev_addr[port];
	/* when dmac equals smac or loop_idc is 1, it should loopback */
	if (ether_addr_equal_unaligned(dmac, smac) ||
	    hr_dev->loop_idc == 0x1) {
		roce_set_bit(context->byte_28_at_fl, V2_QPC_BYTE_28_LBI_S, 1);
		roce_set_bit(qpc_mask->byte_28_at_fl, V2_QPC_BYTE_28_LBI_S, 0);
	}

	if (attr_mask & IB_QP_DEST_QPN) {
		roce_set_field(context->byte_56_dqpn_err, V2_QPC_BYTE_56_DQPN_M,
			       V2_QPC_BYTE_56_DQPN_S, attr->dest_qp_num);
		roce_set_field(qpc_mask->byte_56_dqpn_err,
			       V2_QPC_BYTE_56_DQPN_M, V2_QPC_BYTE_56_DQPN_S, 0);
	}

	/* Configure GID index */
	port_num = rdma_ah_get_port_num(&attr->ah_attr);
	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGID_IDX_M, V2_QPC_BYTE_20_SGID_IDX_S,
		       hns_get_gid_index(hr_dev, port_num - 1,
					 grh->sgid_index));
	roce_set_field(qpc_mask->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGID_IDX_M, V2_QPC_BYTE_20_SGID_IDX_S, 0);
	memcpy(&(context->dmac), dmac, sizeof(u32));
	roce_set_field(context->byte_52_udpspn_dmac, V2_QPC_BYTE_52_DMAC_M,
		       V2_QPC_BYTE_52_DMAC_S, *((u16 *)(&dmac[4])));
	qpc_mask->dmac = 0;
	roce_set_field(qpc_mask->byte_52_udpspn_dmac, V2_QPC_BYTE_52_DMAC_M,
		       V2_QPC_BYTE_52_DMAC_S, 0);

	/* mtu*(2^LP_PKTN_INI) should not bigger than 1 message length 64kb */
	roce_set_field(context->byte_56_dqpn_err, V2_QPC_BYTE_56_LP_PKTN_INI_M,
		       V2_QPC_BYTE_56_LP_PKTN_INI_S, 0);
	roce_set_field(qpc_mask->byte_56_dqpn_err, V2_QPC_BYTE_56_LP_PKTN_INI_M,
		       V2_QPC_BYTE_56_LP_PKTN_INI_S, 0);

	if (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_UD)
		roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_MTU_M,
			       V2_QPC_BYTE_24_MTU_S, IB_MTU_4096);
	else if (attr_mask & IB_QP_PATH_MTU)
		roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_MTU_M,
			       V2_QPC_BYTE_24_MTU_S, attr->path_mtu);

	roce_set_field(qpc_mask->byte_24_mtu_tc, V2_QPC_BYTE_24_MTU_M,
		       V2_QPC_BYTE_24_MTU_S, 0);

	roce_set_field(context->byte_84_rq_ci_pi,
		       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_M,
		       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_S, hr_qp->rq.head);
	roce_set_field(qpc_mask->byte_84_rq_ci_pi,
		       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_M,
		       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_S, 0);

	roce_set_field(qpc_mask->byte_84_rq_ci_pi,
		       V2_QPC_BYTE_84_RQ_CONSUMER_IDX_M,
		       V2_QPC_BYTE_84_RQ_CONSUMER_IDX_S, 0);
	roce_set_bit(qpc_mask->byte_108_rx_reqepsn,
		     V2_QPC_BYTE_108_RX_REQ_PSN_ERR_S, 0);
	roce_set_field(qpc_mask->byte_96_rx_reqmsn, V2_QPC_BYTE_96_RX_REQ_MSN_M,
		       V2_QPC_BYTE_96_RX_REQ_MSN_S, 0);
	roce_set_field(qpc_mask->byte_108_rx_reqepsn,
		       V2_QPC_BYTE_108_RX_REQ_LAST_OPTYPE_M,
		       V2_QPC_BYTE_108_RX_REQ_LAST_OPTYPE_S, 0);

	context->rq_rnr_timer = 0;
	qpc_mask->rq_rnr_timer = 0;

	roce_set_field(qpc_mask->byte_132_trrl, V2_QPC_BYTE_132_TRRL_HEAD_MAX_M,
		       V2_QPC_BYTE_132_TRRL_HEAD_MAX_S, 0);
	roce_set_field(qpc_mask->byte_132_trrl, V2_QPC_BYTE_132_TRRL_TAIL_MAX_M,
		       V2_QPC_BYTE_132_TRRL_TAIL_MAX_S, 0);

	/* rocee send 2^lp_sgen_ini segs every time */
	roce_set_field(context->byte_168_irrl_idx,
		       V2_QPC_BYTE_168_LP_SGEN_INI_M,
		       V2_QPC_BYTE_168_LP_SGEN_INI_S, 3);
	roce_set_field(qpc_mask->byte_168_irrl_idx,
		       V2_QPC_BYTE_168_LP_SGEN_INI_M,
		       V2_QPC_BYTE_168_LP_SGEN_INI_S, 0);

	return 0;
}

static int modify_qp_rtr_to_rts(struct ib_qp *ibqp,
				const struct ib_qp_attr *attr, int attr_mask,
				struct hns_roce_v2_qp_context *context,
				struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u64 sge_cur_blk = 0;
	u64 sq_cur_blk = 0;
	u32 page_size;
	int count;

	/* Search qp buf's mtts */
	count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr, 0, &sq_cur_blk, 1, NULL);
	if (count < 1) {
		ibdev_err(ibdev, "failed to find buf pa of QP(0x%lx)\n",
			  hr_qp->qpn);
		return -EINVAL;
	}

	if (hr_qp->sge.offset) {
		page_size = 1 << (hr_dev->caps.mtt_buf_pg_sz + PAGE_SHIFT);
		count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr,
					  hr_qp->sge.offset / page_size,
					  &sge_cur_blk, 1, NULL);
		if (count < 1) {
			ibdev_err(ibdev, "failed to find sge pa of QP(0x%lx)\n",
				  hr_qp->qpn);
			return -EINVAL;
		}
	}

	/* Not support alternate path and path migration */
	if (attr_mask & (IB_QP_ALT_PATH | IB_QP_PATH_MIG_STATE)) {
		ibdev_err(ibdev, "RTR2RTS attr_mask (0x%x)error\n", attr_mask);
		return -EINVAL;
	}

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	context->sq_cur_blk_addr = cpu_to_le32(sq_cur_blk >> PAGE_ADDR_SHIFT);
	roce_set_field(context->byte_168_irrl_idx,
		       V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_S,
		       sq_cur_blk >> (32 + PAGE_ADDR_SHIFT));
	qpc_mask->sq_cur_blk_addr = 0;
	roce_set_field(qpc_mask->byte_168_irrl_idx,
		       V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_168_SQ_CUR_BLK_ADDR_S, 0);

	context->sq_cur_sge_blk_addr = ((ibqp->qp_type == IB_QPT_GSI) ||
		       hr_qp->sq.max_gs > HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE) ?
		       cpu_to_le32(sge_cur_blk >>
		       PAGE_ADDR_SHIFT) : 0;
	roce_set_field(context->byte_184_irrl_idx,
		       V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_M,
		       V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_S,
		       ((ibqp->qp_type == IB_QPT_GSI) || hr_qp->sq.max_gs >
		       HNS_ROCE_V2_UC_RC_SGE_NUM_IN_WQE) ?
		       (sge_cur_blk >>
		       (32 + PAGE_ADDR_SHIFT)) : 0);
	qpc_mask->sq_cur_sge_blk_addr = 0;
	roce_set_field(qpc_mask->byte_184_irrl_idx,
		       V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_M,
		       V2_QPC_BYTE_184_SQ_CUR_SGE_BLK_ADDR_S, 0);

	context->rx_sq_cur_blk_addr =
		cpu_to_le32(sq_cur_blk >> PAGE_ADDR_SHIFT);
	roce_set_field(context->byte_232_irrl_sge,
		       V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_S,
		       sq_cur_blk >> (32 + PAGE_ADDR_SHIFT));
	qpc_mask->rx_sq_cur_blk_addr = 0;
	roce_set_field(qpc_mask->byte_232_irrl_sge,
		       V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_M,
		       V2_QPC_BYTE_232_RX_SQ_CUR_BLK_ADDR_S, 0);

	/*
	 * Set some fields in context to zero, Because the default values
	 * of all fields in context are zero, we need not set them to 0 again.
	 * but we should set the relevant fields of context mask to 0.
	 */
	roce_set_field(qpc_mask->byte_232_irrl_sge,
		       V2_QPC_BYTE_232_IRRL_SGE_IDX_M,
		       V2_QPC_BYTE_232_IRRL_SGE_IDX_S, 0);

	roce_set_field(qpc_mask->byte_240_irrl_tail,
		       V2_QPC_BYTE_240_RX_ACK_MSN_M,
		       V2_QPC_BYTE_240_RX_ACK_MSN_S, 0);

	roce_set_field(qpc_mask->byte_248_ack_psn,
		       V2_QPC_BYTE_248_ACK_LAST_OPTYPE_M,
		       V2_QPC_BYTE_248_ACK_LAST_OPTYPE_S, 0);
	roce_set_bit(qpc_mask->byte_248_ack_psn,
		     V2_QPC_BYTE_248_IRRL_PSN_VLD_S, 0);
	roce_set_field(qpc_mask->byte_248_ack_psn,
		       V2_QPC_BYTE_248_IRRL_PSN_M,
		       V2_QPC_BYTE_248_IRRL_PSN_S, 0);

	roce_set_field(qpc_mask->byte_240_irrl_tail,
		       V2_QPC_BYTE_240_IRRL_TAIL_REAL_M,
		       V2_QPC_BYTE_240_IRRL_TAIL_REAL_S, 0);

	roce_set_field(qpc_mask->byte_220_retry_psn_msn,
		       V2_QPC_BYTE_220_RETRY_MSG_MSN_M,
		       V2_QPC_BYTE_220_RETRY_MSG_MSN_S, 0);

	roce_set_bit(qpc_mask->byte_248_ack_psn,
		     V2_QPC_BYTE_248_RNR_RETRY_FLAG_S, 0);

	roce_set_field(qpc_mask->byte_212_lsn, V2_QPC_BYTE_212_CHECK_FLG_M,
		       V2_QPC_BYTE_212_CHECK_FLG_S, 0);

	roce_set_field(context->byte_212_lsn, V2_QPC_BYTE_212_LSN_M,
		       V2_QPC_BYTE_212_LSN_S, 0x100);
	roce_set_field(qpc_mask->byte_212_lsn, V2_QPC_BYTE_212_LSN_M,
		       V2_QPC_BYTE_212_LSN_S, 0);

	roce_set_field(qpc_mask->byte_196_sq_psn, V2_QPC_BYTE_196_IRRL_HEAD_M,
		       V2_QPC_BYTE_196_IRRL_HEAD_S, 0);

	return 0;
}

static inline bool hns_roce_v2_check_qp_stat(enum ib_qp_state cur_state,
					     enum ib_qp_state new_state)
{

	if ((cur_state != IB_QPS_RESET &&
	    (new_state == IB_QPS_ERR || new_state == IB_QPS_RESET)) ||
	    ((cur_state == IB_QPS_RTS || cur_state == IB_QPS_SQD) &&
	    (new_state == IB_QPS_RTS || new_state == IB_QPS_SQD)) ||
	    (cur_state == IB_QPS_SQE && new_state == IB_QPS_RTS))
		return true;

	return false;

}

static int hns_roce_v2_set_path(struct ib_qp *ibqp,
				const struct ib_qp_attr *attr,
				int attr_mask,
				struct hns_roce_v2_qp_context *context,
				struct hns_roce_v2_qp_context *qpc_mask)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	const struct ib_gid_attr *gid_attr = NULL;
	int is_roce_protocol;
	u16 vlan_id = 0xffff;
	bool is_udp = false;
	u8 ib_port;
	u8 hr_port;
	int ret;

	ib_port = (attr_mask & IB_QP_PORT) ? attr->port_num : hr_qp->port + 1;
	hr_port = ib_port - 1;
	is_roce_protocol = rdma_cap_eth_ah(&hr_dev->ib_dev, ib_port) &&
			   rdma_ah_get_ah_flags(&attr->ah_attr) & IB_AH_GRH;

	if (is_roce_protocol) {
		gid_attr = attr->ah_attr.grh.sgid_attr;
		ret = rdma_read_gid_l2_fields(gid_attr, &vlan_id, NULL);
		if (ret)
			return ret;

		if (gid_attr)
			is_udp = (gid_attr->gid_type ==
				 IB_GID_TYPE_ROCE_UDP_ENCAP);
	}

	if (vlan_id < VLAN_N_VID) {
		roce_set_bit(context->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_RQ_VLAN_EN_S, 1);
		roce_set_bit(qpc_mask->byte_76_srqn_op_en,
			     V2_QPC_BYTE_76_RQ_VLAN_EN_S, 0);
		roce_set_bit(context->byte_168_irrl_idx,
			     V2_QPC_BYTE_168_SQ_VLAN_EN_S, 1);
		roce_set_bit(qpc_mask->byte_168_irrl_idx,
			     V2_QPC_BYTE_168_SQ_VLAN_EN_S, 0);
	}

	roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_VLAN_ID_M,
		       V2_QPC_BYTE_24_VLAN_ID_S, vlan_id);
	roce_set_field(qpc_mask->byte_24_mtu_tc, V2_QPC_BYTE_24_VLAN_ID_M,
		       V2_QPC_BYTE_24_VLAN_ID_S, 0);

	if (grh->sgid_index >= hr_dev->caps.gid_table_len[hr_port]) {
		ibdev_err(ibdev, "sgid_index(%u) too large. max is %d\n",
			  grh->sgid_index, hr_dev->caps.gid_table_len[hr_port]);
		return -EINVAL;
	}

	if (attr->ah_attr.type != RDMA_AH_ATTR_TYPE_ROCE) {
		ibdev_err(ibdev, "ah attr is not RDMA roce type\n");
		return -EINVAL;
	}

	roce_set_field(context->byte_52_udpspn_dmac, V2_QPC_BYTE_52_UDPSPN_M,
		       V2_QPC_BYTE_52_UDPSPN_S,
		       is_udp ? 0x12b7 : 0);

	roce_set_field(qpc_mask->byte_52_udpspn_dmac, V2_QPC_BYTE_52_UDPSPN_M,
		       V2_QPC_BYTE_52_UDPSPN_S, 0);

	roce_set_field(context->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGID_IDX_M, V2_QPC_BYTE_20_SGID_IDX_S,
		       grh->sgid_index);

	roce_set_field(qpc_mask->byte_20_smac_sgid_idx,
		       V2_QPC_BYTE_20_SGID_IDX_M, V2_QPC_BYTE_20_SGID_IDX_S, 0);

	roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_HOP_LIMIT_M,
		       V2_QPC_BYTE_24_HOP_LIMIT_S, grh->hop_limit);
	roce_set_field(qpc_mask->byte_24_mtu_tc, V2_QPC_BYTE_24_HOP_LIMIT_M,
		       V2_QPC_BYTE_24_HOP_LIMIT_S, 0);

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP08_B && is_udp)
		roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_TC_M,
			       V2_QPC_BYTE_24_TC_S, grh->traffic_class >> 2);
	else
		roce_set_field(context->byte_24_mtu_tc, V2_QPC_BYTE_24_TC_M,
			       V2_QPC_BYTE_24_TC_S, grh->traffic_class);
	roce_set_field(qpc_mask->byte_24_mtu_tc, V2_QPC_BYTE_24_TC_M,
		       V2_QPC_BYTE_24_TC_S, 0);
	roce_set_field(context->byte_28_at_fl, V2_QPC_BYTE_28_FL_M,
		       V2_QPC_BYTE_28_FL_S, grh->flow_label);
	roce_set_field(qpc_mask->byte_28_at_fl, V2_QPC_BYTE_28_FL_M,
		       V2_QPC_BYTE_28_FL_S, 0);
	memcpy(context->dgid, grh->dgid.raw, sizeof(grh->dgid.raw));
	memset(qpc_mask->dgid, 0, sizeof(grh->dgid.raw));
	roce_set_field(context->byte_28_at_fl, V2_QPC_BYTE_28_SL_M,
		       V2_QPC_BYTE_28_SL_S, rdma_ah_get_sl(&attr->ah_attr));
	roce_set_field(qpc_mask->byte_28_at_fl, V2_QPC_BYTE_28_SL_M,
		       V2_QPC_BYTE_28_SL_S, 0);
	hr_qp->sl = rdma_ah_get_sl(&attr->ah_attr);

	return 0;
}

static int hns_roce_v2_set_abs_fields(struct ib_qp *ibqp,
				      const struct ib_qp_attr *attr,
				      int attr_mask,
				      enum ib_qp_state cur_state,
				      enum ib_qp_state new_state,
				      struct hns_roce_v2_qp_context *context,
				      struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	int ret = 0;

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		memset(qpc_mask, 0, sizeof(*qpc_mask));
		modify_qp_reset_to_init(ibqp, attr, attr_mask, context,
					qpc_mask);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_INIT) {
		modify_qp_init_to_init(ibqp, attr, attr_mask, context,
				       qpc_mask);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		ret = modify_qp_init_to_rtr(ibqp, attr, attr_mask, context,
					    qpc_mask);
		if (ret)
			goto out;
	} else if (cur_state == IB_QPS_RTR && new_state == IB_QPS_RTS) {
		ret = modify_qp_rtr_to_rts(ibqp, attr, attr_mask, context,
					   qpc_mask);
		if (ret)
			goto out;
	} else if (hns_roce_v2_check_qp_stat(cur_state, new_state)) {
		/* Nothing */
		;
	} else {
		ibdev_err(&hr_dev->ib_dev, "Illegal state for QP!\n");
		ret = -EINVAL;
		goto out;
	}

out:
	return ret;
}

static int hns_roce_v2_set_opt_fields(struct ib_qp *ibqp,
				      const struct ib_qp_attr *attr,
				      int attr_mask,
				      struct hns_roce_v2_qp_context *context,
				      struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	int ret = 0;

	if (attr_mask & IB_QP_AV) {
		ret = hns_roce_v2_set_path(ibqp, attr, attr_mask, context,
					   qpc_mask);
		if (ret)
			return ret;
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		if (attr->timeout < 31) {
			roce_set_field(context->byte_28_at_fl,
				       V2_QPC_BYTE_28_AT_M, V2_QPC_BYTE_28_AT_S,
				       attr->timeout);
			roce_set_field(qpc_mask->byte_28_at_fl,
				       V2_QPC_BYTE_28_AT_M, V2_QPC_BYTE_28_AT_S,
				       0);
		} else {
			ibdev_warn(&hr_dev->ib_dev,
				   "Local ACK timeout shall be 0 to 30.\n");
		}
	}

	if (attr_mask & IB_QP_RETRY_CNT) {
		roce_set_field(context->byte_212_lsn,
			       V2_QPC_BYTE_212_RETRY_NUM_INIT_M,
			       V2_QPC_BYTE_212_RETRY_NUM_INIT_S,
			       attr->retry_cnt);
		roce_set_field(qpc_mask->byte_212_lsn,
			       V2_QPC_BYTE_212_RETRY_NUM_INIT_M,
			       V2_QPC_BYTE_212_RETRY_NUM_INIT_S, 0);

		roce_set_field(context->byte_212_lsn,
			       V2_QPC_BYTE_212_RETRY_CNT_M,
			       V2_QPC_BYTE_212_RETRY_CNT_S, attr->retry_cnt);
		roce_set_field(qpc_mask->byte_212_lsn,
			       V2_QPC_BYTE_212_RETRY_CNT_M,
			       V2_QPC_BYTE_212_RETRY_CNT_S, 0);
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		roce_set_field(context->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RNR_NUM_INIT_M,
			       V2_QPC_BYTE_244_RNR_NUM_INIT_S, attr->rnr_retry);
		roce_set_field(qpc_mask->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RNR_NUM_INIT_M,
			       V2_QPC_BYTE_244_RNR_NUM_INIT_S, 0);

		roce_set_field(context->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RNR_CNT_M,
			       V2_QPC_BYTE_244_RNR_CNT_S, attr->rnr_retry);
		roce_set_field(qpc_mask->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RNR_CNT_M,
			       V2_QPC_BYTE_244_RNR_CNT_S, 0);
	}

	/* RC&UC&UD required attr */
	if (attr_mask & IB_QP_SQ_PSN) {
		roce_set_field(context->byte_172_sq_psn,
			       V2_QPC_BYTE_172_SQ_CUR_PSN_M,
			       V2_QPC_BYTE_172_SQ_CUR_PSN_S, attr->sq_psn);
		roce_set_field(qpc_mask->byte_172_sq_psn,
			       V2_QPC_BYTE_172_SQ_CUR_PSN_M,
			       V2_QPC_BYTE_172_SQ_CUR_PSN_S, 0);

		roce_set_field(context->byte_196_sq_psn,
			       V2_QPC_BYTE_196_SQ_MAX_PSN_M,
			       V2_QPC_BYTE_196_SQ_MAX_PSN_S, attr->sq_psn);
		roce_set_field(qpc_mask->byte_196_sq_psn,
			       V2_QPC_BYTE_196_SQ_MAX_PSN_M,
			       V2_QPC_BYTE_196_SQ_MAX_PSN_S, 0);

		roce_set_field(context->byte_220_retry_psn_msn,
			       V2_QPC_BYTE_220_RETRY_MSG_PSN_M,
			       V2_QPC_BYTE_220_RETRY_MSG_PSN_S, attr->sq_psn);
		roce_set_field(qpc_mask->byte_220_retry_psn_msn,
			       V2_QPC_BYTE_220_RETRY_MSG_PSN_M,
			       V2_QPC_BYTE_220_RETRY_MSG_PSN_S, 0);

		roce_set_field(context->byte_224_retry_msg,
			       V2_QPC_BYTE_224_RETRY_MSG_PSN_M,
			       V2_QPC_BYTE_224_RETRY_MSG_PSN_S,
			       attr->sq_psn >> V2_QPC_BYTE_220_RETRY_MSG_PSN_S);
		roce_set_field(qpc_mask->byte_224_retry_msg,
			       V2_QPC_BYTE_224_RETRY_MSG_PSN_M,
			       V2_QPC_BYTE_224_RETRY_MSG_PSN_S, 0);

		roce_set_field(context->byte_224_retry_msg,
			       V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_M,
			       V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_S,
			       attr->sq_psn);
		roce_set_field(qpc_mask->byte_224_retry_msg,
			       V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_M,
			       V2_QPC_BYTE_224_RETRY_MSG_FPKT_PSN_S, 0);

		roce_set_field(context->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RX_ACK_EPSN_M,
			       V2_QPC_BYTE_244_RX_ACK_EPSN_S, attr->sq_psn);
		roce_set_field(qpc_mask->byte_244_rnr_rxack,
			       V2_QPC_BYTE_244_RX_ACK_EPSN_M,
			       V2_QPC_BYTE_244_RX_ACK_EPSN_S, 0);
	}

	if ((attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) &&
	     attr->max_dest_rd_atomic) {
		roce_set_field(context->byte_140_raq, V2_QPC_BYTE_140_RR_MAX_M,
			       V2_QPC_BYTE_140_RR_MAX_S,
			       fls(attr->max_dest_rd_atomic - 1));
		roce_set_field(qpc_mask->byte_140_raq, V2_QPC_BYTE_140_RR_MAX_M,
			       V2_QPC_BYTE_140_RR_MAX_S, 0);
	}

	if ((attr_mask & IB_QP_MAX_QP_RD_ATOMIC) && attr->max_rd_atomic) {
		roce_set_field(context->byte_208_irrl, V2_QPC_BYTE_208_SR_MAX_M,
			       V2_QPC_BYTE_208_SR_MAX_S,
			       fls(attr->max_rd_atomic - 1));
		roce_set_field(qpc_mask->byte_208_irrl,
			       V2_QPC_BYTE_208_SR_MAX_M,
			       V2_QPC_BYTE_208_SR_MAX_S, 0);
	}

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC))
		set_access_flags(hr_qp, context, qpc_mask, attr, attr_mask);

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		roce_set_field(context->byte_80_rnr_rx_cqn,
			       V2_QPC_BYTE_80_MIN_RNR_TIME_M,
			       V2_QPC_BYTE_80_MIN_RNR_TIME_S,
			       attr->min_rnr_timer);
		roce_set_field(qpc_mask->byte_80_rnr_rx_cqn,
			       V2_QPC_BYTE_80_MIN_RNR_TIME_M,
			       V2_QPC_BYTE_80_MIN_RNR_TIME_S, 0);
	}

	/* RC&UC required attr */
	if (attr_mask & IB_QP_RQ_PSN) {
		roce_set_field(context->byte_108_rx_reqepsn,
			       V2_QPC_BYTE_108_RX_REQ_EPSN_M,
			       V2_QPC_BYTE_108_RX_REQ_EPSN_S, attr->rq_psn);
		roce_set_field(qpc_mask->byte_108_rx_reqepsn,
			       V2_QPC_BYTE_108_RX_REQ_EPSN_M,
			       V2_QPC_BYTE_108_RX_REQ_EPSN_S, 0);

		roce_set_field(context->byte_152_raq, V2_QPC_BYTE_152_RAQ_PSN_M,
			       V2_QPC_BYTE_152_RAQ_PSN_S, attr->rq_psn - 1);
		roce_set_field(qpc_mask->byte_152_raq,
			       V2_QPC_BYTE_152_RAQ_PSN_M,
			       V2_QPC_BYTE_152_RAQ_PSN_S, 0);
	}

	if (attr_mask & IB_QP_QKEY) {
		context->qkey_xrcd = cpu_to_le32(attr->qkey);
		qpc_mask->qkey_xrcd = 0;
		hr_qp->qkey = attr->qkey;
	}

	return ret;
}

static void hns_roce_v2_record_opt_fields(struct ib_qp *ibqp,
					  const struct ib_qp_attr *attr,
					  int attr_mask)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);

	if (attr_mask & IB_QP_ACCESS_FLAGS)
		hr_qp->atomic_rd_en = attr->qp_access_flags;

	if (attr_mask & IB_QP_MAX_DEST_RD_ATOMIC)
		hr_qp->resp_depth = attr->max_dest_rd_atomic;
	if (attr_mask & IB_QP_PORT) {
		hr_qp->port = attr->port_num - 1;
		hr_qp->phy_port = hr_dev->iboe.phy_port[hr_qp->port];
	}
}

static int hns_roce_v2_modify_qp(struct ib_qp *ibqp,
				 const struct ib_qp_attr *attr,
				 int attr_mask, enum ib_qp_state cur_state,
				 enum ib_qp_state new_state)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_v2_qp_context ctx[2];
	struct hns_roce_v2_qp_context *context = ctx;
	struct hns_roce_v2_qp_context *qpc_mask = ctx + 1;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	unsigned long sq_flag = 0;
	unsigned long rq_flag = 0;
	int ret;

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	memset(context, 0, sizeof(*context));
	memset(qpc_mask, 0xff, sizeof(*qpc_mask));
	ret = hns_roce_v2_set_abs_fields(ibqp, attr, attr_mask, cur_state,
					 new_state, context, qpc_mask);
	if (ret)
		goto out;

	/* When QP state is err, SQ and RQ WQE should be flushed */
	if (new_state == IB_QPS_ERR) {
		spin_lock_irqsave(&hr_qp->sq.lock, sq_flag);
		hr_qp->state = IB_QPS_ERR;
		roce_set_field(context->byte_160_sq_ci_pi,
			       V2_QPC_BYTE_160_SQ_PRODUCER_IDX_M,
			       V2_QPC_BYTE_160_SQ_PRODUCER_IDX_S,
			       hr_qp->sq.head);
		roce_set_field(qpc_mask->byte_160_sq_ci_pi,
			       V2_QPC_BYTE_160_SQ_PRODUCER_IDX_M,
			       V2_QPC_BYTE_160_SQ_PRODUCER_IDX_S, 0);
		spin_unlock_irqrestore(&hr_qp->sq.lock, sq_flag);

		if (!ibqp->srq) {
			spin_lock_irqsave(&hr_qp->rq.lock, rq_flag);
			roce_set_field(context->byte_84_rq_ci_pi,
			       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_M,
			       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_S,
			       hr_qp->rq.head);
			roce_set_field(qpc_mask->byte_84_rq_ci_pi,
			       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_M,
			       V2_QPC_BYTE_84_RQ_PRODUCER_IDX_S, 0);
			spin_unlock_irqrestore(&hr_qp->rq.lock, rq_flag);
		}
	}

	/* Configure the optional fields */
	ret = hns_roce_v2_set_opt_fields(ibqp, attr, attr_mask, context,
					 qpc_mask);
	if (ret)
		goto out;

	roce_set_bit(context->byte_108_rx_reqepsn, V2_QPC_BYTE_108_INV_CREDIT_S,
		     ibqp->srq ? 1 : 0);
	roce_set_bit(qpc_mask->byte_108_rx_reqepsn,
		     V2_QPC_BYTE_108_INV_CREDIT_S, 0);

	/* Every status migrate must change state */
	roce_set_field(context->byte_60_qpst_tempid, V2_QPC_BYTE_60_QP_ST_M,
		       V2_QPC_BYTE_60_QP_ST_S, new_state);
	roce_set_field(qpc_mask->byte_60_qpst_tempid, V2_QPC_BYTE_60_QP_ST_M,
		       V2_QPC_BYTE_60_QP_ST_S, 0);

	/* SW pass context to HW */
	ret = hns_roce_v2_qp_modify(hr_dev, ctx, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "failed to modify QP, ret = %d\n", ret);
		goto out;
	}

	hr_qp->state = new_state;

	hns_roce_v2_record_opt_fields(ibqp, attr, attr_mask);

	if (new_state == IB_QPS_RESET && !ibqp->uobject) {
		hns_roce_v2_cq_clean(to_hr_cq(ibqp->recv_cq), hr_qp->qpn,
				     ibqp->srq ? to_hr_srq(ibqp->srq) : NULL);
		if (ibqp->send_cq != ibqp->recv_cq)
			hns_roce_v2_cq_clean(to_hr_cq(ibqp->send_cq),
					     hr_qp->qpn, NULL);

		hr_qp->rq.head = 0;
		hr_qp->rq.tail = 0;
		hr_qp->sq.head = 0;
		hr_qp->sq.tail = 0;
		hr_qp->next_sge = 0;
		if (hr_qp->rq.wqe_cnt)
			*hr_qp->rdb.db_record = 0;
	}

out:
	return ret;
}

static inline enum ib_qp_state to_ib_qp_st(enum hns_roce_v2_qp_state state)
{
	switch (state) {
	case HNS_ROCE_QP_ST_RST:	return IB_QPS_RESET;
	case HNS_ROCE_QP_ST_INIT:	return IB_QPS_INIT;
	case HNS_ROCE_QP_ST_RTR:	return IB_QPS_RTR;
	case HNS_ROCE_QP_ST_RTS:	return IB_QPS_RTS;
	case HNS_ROCE_QP_ST_SQ_DRAINING:
	case HNS_ROCE_QP_ST_SQD:	return IB_QPS_SQD;
	case HNS_ROCE_QP_ST_SQER:	return IB_QPS_SQE;
	case HNS_ROCE_QP_ST_ERR:	return IB_QPS_ERR;
	default:			return -1;
	}
}

static int hns_roce_v2_query_qpc(struct hns_roce_dev *hr_dev,
				 struct hns_roce_qp *hr_qp,
				 struct hns_roce_v2_qp_context *hr_context)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, hr_qp->qpn, 0,
				HNS_ROCE_CMD_QUERY_QPC,
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret)
		goto out;

	memcpy(hr_context, mailbox->buf, sizeof(*hr_context));

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v2_query_qp(struct ib_qp *ibqp, struct ib_qp_attr *qp_attr,
				int qp_attr_mask,
				struct ib_qp_init_attr *qp_init_attr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_v2_qp_context context = {};
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int tmp_qp_state;
	int state;
	int ret;

	memset(qp_attr, 0, sizeof(*qp_attr));
	memset(qp_init_attr, 0, sizeof(*qp_init_attr));

	mutex_lock(&hr_qp->mutex);

	if (hr_qp->state == IB_QPS_RESET) {
		qp_attr->qp_state = IB_QPS_RESET;
		ret = 0;
		goto done;
	}

	ret = hns_roce_v2_query_qpc(hr_dev, hr_qp, &context);
	if (ret) {
		ibdev_err(ibdev, "failed to query QPC, ret = %d\n", ret);
		ret = -EINVAL;
		goto out;
	}

	state = roce_get_field(context.byte_60_qpst_tempid,
			       V2_QPC_BYTE_60_QP_ST_M, V2_QPC_BYTE_60_QP_ST_S);
	tmp_qp_state = to_ib_qp_st((enum hns_roce_v2_qp_state)state);
	if (tmp_qp_state == -1) {
		ibdev_err(ibdev, "Illegal ib_qp_state\n");
		ret = -EINVAL;
		goto out;
	}
	hr_qp->state = (u8)tmp_qp_state;
	qp_attr->qp_state = (enum ib_qp_state)hr_qp->state;
	qp_attr->path_mtu = (enum ib_mtu)roce_get_field(context.byte_24_mtu_tc,
							V2_QPC_BYTE_24_MTU_M,
							V2_QPC_BYTE_24_MTU_S);
	qp_attr->path_mig_state = IB_MIG_ARMED;
	qp_attr->ah_attr.type   = RDMA_AH_ATTR_TYPE_ROCE;
	if (hr_qp->ibqp.qp_type == IB_QPT_UD)
		qp_attr->qkey = V2_QKEY_VAL;

	qp_attr->rq_psn = roce_get_field(context.byte_108_rx_reqepsn,
					 V2_QPC_BYTE_108_RX_REQ_EPSN_M,
					 V2_QPC_BYTE_108_RX_REQ_EPSN_S);
	qp_attr->sq_psn = (u32)roce_get_field(context.byte_172_sq_psn,
					      V2_QPC_BYTE_172_SQ_CUR_PSN_M,
					      V2_QPC_BYTE_172_SQ_CUR_PSN_S);
	qp_attr->dest_qp_num = (u8)roce_get_field(context.byte_56_dqpn_err,
						  V2_QPC_BYTE_56_DQPN_M,
						  V2_QPC_BYTE_56_DQPN_S);
	qp_attr->qp_access_flags = ((roce_get_bit(context.byte_76_srqn_op_en,
				    V2_QPC_BYTE_76_RRE_S)) << V2_QP_RRE_S) |
				    ((roce_get_bit(context.byte_76_srqn_op_en,
				    V2_QPC_BYTE_76_RWE_S)) << V2_QP_RWE_S) |
				    ((roce_get_bit(context.byte_76_srqn_op_en,
				    V2_QPC_BYTE_76_ATE_S)) << V2_QP_ATE_S);

	if (hr_qp->ibqp.qp_type == IB_QPT_RC ||
	    hr_qp->ibqp.qp_type == IB_QPT_UC) {
		struct ib_global_route *grh =
				rdma_ah_retrieve_grh(&qp_attr->ah_attr);

		rdma_ah_set_sl(&qp_attr->ah_attr,
			       roce_get_field(context.byte_28_at_fl,
					      V2_QPC_BYTE_28_SL_M,
					      V2_QPC_BYTE_28_SL_S));
		grh->flow_label = roce_get_field(context.byte_28_at_fl,
						 V2_QPC_BYTE_28_FL_M,
						 V2_QPC_BYTE_28_FL_S);
		grh->sgid_index = roce_get_field(context.byte_20_smac_sgid_idx,
						 V2_QPC_BYTE_20_SGID_IDX_M,
						 V2_QPC_BYTE_20_SGID_IDX_S);
		grh->hop_limit = roce_get_field(context.byte_24_mtu_tc,
						V2_QPC_BYTE_24_HOP_LIMIT_M,
						V2_QPC_BYTE_24_HOP_LIMIT_S);
		grh->traffic_class = roce_get_field(context.byte_24_mtu_tc,
						    V2_QPC_BYTE_24_TC_M,
						    V2_QPC_BYTE_24_TC_S);

		memcpy(grh->dgid.raw, context.dgid, sizeof(grh->dgid.raw));
	}

	qp_attr->port_num = hr_qp->port + 1;
	qp_attr->sq_draining = 0;
	qp_attr->max_rd_atomic = 1 << roce_get_field(context.byte_208_irrl,
						     V2_QPC_BYTE_208_SR_MAX_M,
						     V2_QPC_BYTE_208_SR_MAX_S);
	qp_attr->max_dest_rd_atomic = 1 << roce_get_field(context.byte_140_raq,
						     V2_QPC_BYTE_140_RR_MAX_M,
						     V2_QPC_BYTE_140_RR_MAX_S);
	qp_attr->min_rnr_timer = (u8)roce_get_field(context.byte_80_rnr_rx_cqn,
						 V2_QPC_BYTE_80_MIN_RNR_TIME_M,
						 V2_QPC_BYTE_80_MIN_RNR_TIME_S);
	qp_attr->timeout = (u8)roce_get_field(context.byte_28_at_fl,
					      V2_QPC_BYTE_28_AT_M,
					      V2_QPC_BYTE_28_AT_S);
	qp_attr->retry_cnt = roce_get_field(context.byte_212_lsn,
					    V2_QPC_BYTE_212_RETRY_CNT_M,
					    V2_QPC_BYTE_212_RETRY_CNT_S);
	qp_attr->rnr_retry = le32_to_cpu(context.rq_rnr_timer);

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
	return ret;
}

static int hns_roce_v2_destroy_qp_common(struct hns_roce_dev *hr_dev,
					 struct hns_roce_qp *hr_qp,
					 struct ib_udata *udata)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_cq *send_cq, *recv_cq;
	unsigned long flags;
	int ret = 0;

	if (hr_qp->ibqp.qp_type == IB_QPT_RC && hr_qp->state != IB_QPS_RESET) {
		/* Modify qp to reset before destroying qp */
		ret = hns_roce_v2_modify_qp(&hr_qp->ibqp, NULL, 0,
					    hr_qp->state, IB_QPS_RESET);
		if (ret)
			ibdev_err(ibdev,
				  "failed to modify QP to RST, ret = %d\n",
				  ret);
	}

	send_cq = hr_qp->ibqp.send_cq ? to_hr_cq(hr_qp->ibqp.send_cq) : NULL;
	recv_cq = hr_qp->ibqp.recv_cq ? to_hr_cq(hr_qp->ibqp.recv_cq) : NULL;

	spin_lock_irqsave(&hr_dev->qp_list_lock, flags);
	hns_roce_lock_cqs(send_cq, recv_cq);

	if (!udata) {
		if (recv_cq)
			__hns_roce_v2_cq_clean(recv_cq, hr_qp->qpn,
					       (hr_qp->ibqp.srq ?
						to_hr_srq(hr_qp->ibqp.srq) :
						NULL));

		if (send_cq && send_cq != recv_cq)
			__hns_roce_v2_cq_clean(send_cq, hr_qp->qpn, NULL);

	}

	hns_roce_qp_remove(hr_dev, hr_qp);

	hns_roce_unlock_cqs(send_cq, recv_cq);
	spin_unlock_irqrestore(&hr_dev->qp_list_lock, flags);

	return ret;
}

static int hns_roce_v2_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	int ret;

	ret = hns_roce_v2_destroy_qp_common(hr_dev, hr_qp, udata);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to destroy QP 0x%06lx, ret = %d\n",
			  hr_qp->qpn, ret);

	hns_roce_qp_destroy(hr_dev, hr_qp, udata);

	return 0;
}

static int hns_roce_v2_qp_flow_control_init(struct hns_roce_dev *hr_dev,
					    struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_sccc_clr_done *resp;
	struct hns_roce_sccc_clr *clr;
	struct hns_roce_cmq_desc desc;
	int ret, i;

	mutex_lock(&hr_dev->qp_table.scc_mutex);

	/* set scc ctx clear done flag */
	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_RESET_SCCC, false);
	ret =  hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		ibdev_err(ibdev, "failed to reset SCC ctx, ret = %d\n", ret);
		goto out;
	}

	/* clear scc context */
	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CLR_SCCC, false);
	clr = (struct hns_roce_sccc_clr *)desc.data;
	clr->qpn = cpu_to_le32(hr_qp->qpn);
	ret =  hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		ibdev_err(ibdev, "failed to clear SCC ctx, ret = %d\n", ret);
		goto out;
	}

	/* query scc context clear is done or not */
	resp = (struct hns_roce_sccc_clr_done *)desc.data;
	for (i = 0; i <= HNS_ROCE_CMQ_SCC_CLR_DONE_CNT; i++) {
		hns_roce_cmq_setup_basic_desc(&desc,
					      HNS_ROCE_OPC_QUERY_SCCC, true);
		ret = hns_roce_cmq_send(hr_dev, &desc, 1);
		if (ret) {
			ibdev_err(ibdev, "failed to query clr cmq, ret = %d\n",
				  ret);
			goto out;
		}

		if (resp->clr_done)
			goto out;

		msleep(20);
	}

	ibdev_err(ibdev, "Query SCC clr done flag overtime.\n");
	ret = -ETIMEDOUT;

out:
	mutex_unlock(&hr_dev->qp_table.scc_mutex);
	return ret;
}

static int hns_roce_v2_modify_cq(struct ib_cq *cq, u16 cq_count, u16 cq_period)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(cq->device);
	struct hns_roce_v2_cq_context *cq_context;
	struct hns_roce_cq *hr_cq = to_hr_cq(cq);
	struct hns_roce_v2_cq_context *cqc_mask;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	cq_context = mailbox->buf;
	cqc_mask = (struct hns_roce_v2_cq_context *)mailbox->buf + 1;

	memset(cqc_mask, 0xff, sizeof(*cqc_mask));

	roce_set_field(cq_context->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_MAX_CNT_M, V2_CQC_BYTE_56_CQ_MAX_CNT_S,
		       cq_count);
	roce_set_field(cqc_mask->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_MAX_CNT_M, V2_CQC_BYTE_56_CQ_MAX_CNT_S,
		       0);
	roce_set_field(cq_context->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_PERIOD_M, V2_CQC_BYTE_56_CQ_PERIOD_S,
		       cq_period);
	roce_set_field(cqc_mask->byte_56_cqe_period_maxcnt,
		       V2_CQC_BYTE_56_CQ_PERIOD_M, V2_CQC_BYTE_56_CQ_PERIOD_S,
		       0);

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, hr_cq->cqn, 1,
				HNS_ROCE_CMD_MODIFY_CQC,
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd when modifying CQ, ret = %d\n",
			  ret);

	return ret;
}

static void hns_roce_irq_work_handle(struct work_struct *work)
{
	struct hns_roce_work *irq_work =
				container_of(work, struct hns_roce_work, work);
	struct ib_device *ibdev = &irq_work->hr_dev->ib_dev;
	u32 qpn = irq_work->qpn;
	u32 cqn = irq_work->cqn;

	switch (irq_work->event_type) {
	case HNS_ROCE_EVENT_TYPE_PATH_MIG:
		ibdev_info(ibdev, "Path migrated succeeded.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
		ibdev_warn(ibdev, "Path migration failed.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_COMM_EST:
		break;
	case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
		ibdev_warn(ibdev, "Send queue drained.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		ibdev_err(ibdev, "Local work queue 0x%x catast error, sub_event type is: %d\n",
			  qpn, irq_work->sub_type);
		break;
	case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		ibdev_err(ibdev, "Invalid request local work queue 0x%x error.\n",
			  qpn);
		break;
	case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
		ibdev_err(ibdev, "Local access violation work queue 0x%x error, sub_event type is: %d\n",
			  qpn, irq_work->sub_type);
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		ibdev_warn(ibdev, "SRQ limit reach.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
		ibdev_warn(ibdev, "SRQ last wqe reach.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
		ibdev_err(ibdev, "SRQ catas error.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		ibdev_err(ibdev, "CQ 0x%x access err.\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		ibdev_warn(ibdev, "CQ 0x%x overflow\n", cqn);
		break;
	case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
		ibdev_warn(ibdev, "DB overflow.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_FLR:
		ibdev_warn(ibdev, "Function level reset.\n");
		break;
	default:
		break;
	}

	kfree(irq_work);
}

static void hns_roce_v2_init_irq_work(struct hns_roce_dev *hr_dev,
				      struct hns_roce_eq *eq,
				      u32 qpn, u32 cqn)
{
	struct hns_roce_work *irq_work;

	irq_work = kzalloc(sizeof(struct hns_roce_work), GFP_ATOMIC);
	if (!irq_work)
		return;

	INIT_WORK(&(irq_work->work), hns_roce_irq_work_handle);
	irq_work->hr_dev = hr_dev;
	irq_work->qpn = qpn;
	irq_work->cqn = cqn;
	irq_work->event_type = eq->event_type;
	irq_work->sub_type = eq->sub_type;
	queue_work(hr_dev->irq_workq, &(irq_work->work));
}

static void set_eq_cons_index_v2(struct hns_roce_eq *eq)
{
	struct hns_roce_dev *hr_dev = eq->hr_dev;
	__le32 doorbell[2] = {};

	if (eq->type_flag == HNS_ROCE_AEQ) {
		roce_set_field(doorbell[0], HNS_ROCE_V2_EQ_DB_CMD_M,
			       HNS_ROCE_V2_EQ_DB_CMD_S,
			       eq->arm_st == HNS_ROCE_V2_EQ_ALWAYS_ARMED ?
			       HNS_ROCE_EQ_DB_CMD_AEQ :
			       HNS_ROCE_EQ_DB_CMD_AEQ_ARMED);
	} else {
		roce_set_field(doorbell[0], HNS_ROCE_V2_EQ_DB_TAG_M,
			       HNS_ROCE_V2_EQ_DB_TAG_S, eq->eqn);

		roce_set_field(doorbell[0], HNS_ROCE_V2_EQ_DB_CMD_M,
			       HNS_ROCE_V2_EQ_DB_CMD_S,
			       eq->arm_st == HNS_ROCE_V2_EQ_ALWAYS_ARMED ?
			       HNS_ROCE_EQ_DB_CMD_CEQ :
			       HNS_ROCE_EQ_DB_CMD_CEQ_ARMED);
	}

	roce_set_field(doorbell[1], HNS_ROCE_V2_EQ_DB_PARA_M,
		       HNS_ROCE_V2_EQ_DB_PARA_S,
		       (eq->cons_index & HNS_ROCE_V2_CONS_IDX_M));

	hns_roce_write64(hr_dev, doorbell, eq->doorbell);
}

static inline void *get_eqe_buf(struct hns_roce_eq *eq, unsigned long offset)
{
	u32 buf_chk_sz;

	buf_chk_sz = 1 << (eq->eqe_buf_pg_sz + PAGE_SHIFT);
	if (eq->buf.nbufs == 1)
		return eq->buf.direct.buf + offset % buf_chk_sz;
	else
		return eq->buf.page_list[offset / buf_chk_sz].buf +
		       offset % buf_chk_sz;
}

static struct hns_roce_aeqe *next_aeqe_sw_v2(struct hns_roce_eq *eq)
{
	struct hns_roce_aeqe *aeqe;

	aeqe = get_eqe_buf(eq, (eq->cons_index & (eq->entries - 1)) *
			   HNS_ROCE_AEQ_ENTRY_SIZE);
	return (roce_get_bit(aeqe->asyn, HNS_ROCE_V2_AEQ_AEQE_OWNER_S) ^
		!!(eq->cons_index & eq->entries)) ? aeqe : NULL;
}

static int hns_roce_v2_aeq_int(struct hns_roce_dev *hr_dev,
			       struct hns_roce_eq *eq)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_aeqe *aeqe = next_aeqe_sw_v2(eq);
	int aeqe_found = 0;
	int event_type;
	int sub_type;
	u32 srqn;
	u32 qpn;
	u32 cqn;

	while (aeqe) {
		/* Make sure we read AEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		event_type = roce_get_field(aeqe->asyn,
					    HNS_ROCE_V2_AEQE_EVENT_TYPE_M,
					    HNS_ROCE_V2_AEQE_EVENT_TYPE_S);
		sub_type = roce_get_field(aeqe->asyn,
					  HNS_ROCE_V2_AEQE_SUB_TYPE_M,
					  HNS_ROCE_V2_AEQE_SUB_TYPE_S);
		qpn = roce_get_field(aeqe->event.qp_event.qp,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_M,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_S);
		cqn = roce_get_field(aeqe->event.cq_event.cq,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_M,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_S);
		srqn = roce_get_field(aeqe->event.srq_event.srq,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_M,
				     HNS_ROCE_V2_AEQE_EVENT_QUEUE_NUM_S);

		switch (event_type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
			hns_roce_qp_event(hr_dev, qpn, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
			hns_roce_srq_event(hr_dev, srqn, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
			hns_roce_cq_event(hr_dev, cqn, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
			break;
		case HNS_ROCE_EVENT_TYPE_MB:
			hns_roce_cmd_event(hr_dev,
					le16_to_cpu(aeqe->event.cmd.token),
					aeqe->event.cmd.status,
					le64_to_cpu(aeqe->event.cmd.out_param));
			break;
		case HNS_ROCE_EVENT_TYPE_CEQ_OVERFLOW:
			break;
		case HNS_ROCE_EVENT_TYPE_FLR:
			break;
		default:
			dev_err(dev, "Unhandled event %d on EQ %d at idx %u.\n",
				event_type, eq->eqn, eq->cons_index);
			break;
		}

		eq->event_type = event_type;
		eq->sub_type = sub_type;
		++eq->cons_index;
		aeqe_found = 1;

		if (eq->cons_index > (2 * eq->entries - 1))
			eq->cons_index = 0;

		hns_roce_v2_init_irq_work(hr_dev, eq, qpn, cqn);

		aeqe = next_aeqe_sw_v2(eq);
	}

	set_eq_cons_index_v2(eq);
	return aeqe_found;
}

static struct hns_roce_ceqe *next_ceqe_sw_v2(struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe;

	ceqe = get_eqe_buf(eq, (eq->cons_index & (eq->entries - 1)) *
			   HNS_ROCE_CEQ_ENTRY_SIZE);
	return (!!(roce_get_bit(ceqe->comp, HNS_ROCE_V2_CEQ_CEQE_OWNER_S))) ^
		(!!(eq->cons_index & eq->entries)) ? ceqe : NULL;
}

static int hns_roce_v2_ceq_int(struct hns_roce_dev *hr_dev,
			       struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe = next_ceqe_sw_v2(eq);
	int ceqe_found = 0;
	u32 cqn;

	while (ceqe) {
		/* Make sure we read CEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		cqn = roce_get_field(ceqe->comp, HNS_ROCE_V2_CEQE_COMP_CQN_M,
				     HNS_ROCE_V2_CEQE_COMP_CQN_S);

		hns_roce_cq_completion(hr_dev, cqn);

		++eq->cons_index;
		ceqe_found = 1;

		if (eq->cons_index > (EQ_DEPTH_COEFF * eq->entries - 1))
			eq->cons_index = 0;

		ceqe = next_ceqe_sw_v2(eq);
	}

	set_eq_cons_index_v2(eq);

	return ceqe_found;
}

static irqreturn_t hns_roce_v2_msix_interrupt_eq(int irq, void *eq_ptr)
{
	struct hns_roce_eq *eq = eq_ptr;
	struct hns_roce_dev *hr_dev = eq->hr_dev;
	int int_work = 0;

	if (eq->type_flag == HNS_ROCE_CEQ)
		/* Completion event interrupt */
		int_work = hns_roce_v2_ceq_int(hr_dev, eq);
	else
		/* Asychronous event interrupt */
		int_work = hns_roce_v2_aeq_int(hr_dev, eq);

	return IRQ_RETVAL(int_work);
}

static irqreturn_t hns_roce_v2_msix_interrupt_abn(int irq, void *dev_id)
{
	struct hns_roce_dev *hr_dev = dev_id;
	struct device *dev = hr_dev->dev;
	int int_work = 0;
	u32 int_st;
	u32 int_en;

	/* Abnormal interrupt */
	int_st = roce_read(hr_dev, ROCEE_VF_ABN_INT_ST_REG);
	int_en = roce_read(hr_dev, ROCEE_VF_ABN_INT_EN_REG);

	if (int_st & BIT(HNS_ROCE_V2_VF_INT_ST_AEQ_OVERFLOW_S)) {
		struct pci_dev *pdev = hr_dev->pci_dev;
		struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
		const struct hnae3_ae_ops *ops = ae_dev->ops;

		dev_err(dev, "AEQ overflow!\n");

		int_st |= 1 << HNS_ROCE_V2_VF_INT_ST_AEQ_OVERFLOW_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_ST_REG, int_st);

		/* Set reset level for reset_event() */
		if (ops->set_default_reset_request)
			ops->set_default_reset_request(ae_dev,
						       HNAE3_FUNC_RESET);
		if (ops->reset_event)
			ops->reset_event(pdev, NULL);

		int_en |= 1 << HNS_ROCE_V2_VF_ABN_INT_EN_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG, int_en);

		int_work = 1;
	} else if (int_st & BIT(HNS_ROCE_V2_VF_INT_ST_BUS_ERR_S)) {
		dev_err(dev, "BUS ERR!\n");

		int_st |= 1 << HNS_ROCE_V2_VF_INT_ST_BUS_ERR_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_ST_REG, int_st);

		int_en |= 1 << HNS_ROCE_V2_VF_ABN_INT_EN_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG, int_en);

		int_work = 1;
	} else if (int_st & BIT(HNS_ROCE_V2_VF_INT_ST_OTHER_ERR_S)) {
		dev_err(dev, "OTHER ERR!\n");

		int_st |= 1 << HNS_ROCE_V2_VF_INT_ST_OTHER_ERR_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_ST_REG, int_st);

		int_en |= 1 << HNS_ROCE_V2_VF_ABN_INT_EN_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG, int_en);

		int_work = 1;
	} else
		dev_err(dev, "There is no abnormal irq found!\n");

	return IRQ_RETVAL(int_work);
}

static void hns_roce_v2_int_mask_enable(struct hns_roce_dev *hr_dev,
					int eq_num, int enable_flag)
{
	int i;

	if (enable_flag == EQ_ENABLE) {
		for (i = 0; i < eq_num; i++)
			roce_write(hr_dev, ROCEE_VF_EVENT_INT_EN_REG +
				   i * EQ_REG_OFFSET,
				   HNS_ROCE_V2_VF_EVENT_INT_EN_M);

		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG,
			   HNS_ROCE_V2_VF_ABN_INT_EN_M);
		roce_write(hr_dev, ROCEE_VF_ABN_INT_CFG_REG,
			   HNS_ROCE_V2_VF_ABN_INT_CFG_M);
	} else {
		for (i = 0; i < eq_num; i++)
			roce_write(hr_dev, ROCEE_VF_EVENT_INT_EN_REG +
				   i * EQ_REG_OFFSET,
				   HNS_ROCE_V2_VF_EVENT_INT_EN_M & 0x0);

		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG,
			   HNS_ROCE_V2_VF_ABN_INT_EN_M & 0x0);
		roce_write(hr_dev, ROCEE_VF_ABN_INT_CFG_REG,
			   HNS_ROCE_V2_VF_ABN_INT_CFG_M & 0x0);
	}
}

static void hns_roce_v2_destroy_eqc(struct hns_roce_dev *hr_dev, int eqn)
{
	struct device *dev = hr_dev->dev;
	int ret;

	if (eqn < hr_dev->caps.num_comp_vectors)
		ret = hns_roce_cmd_mbox(hr_dev, 0, 0, eqn & HNS_ROCE_V2_EQN_M,
					0, HNS_ROCE_CMD_DESTROY_CEQC,
					HNS_ROCE_CMD_TIMEOUT_MSECS);
	else
		ret = hns_roce_cmd_mbox(hr_dev, 0, 0, eqn & HNS_ROCE_V2_EQN_M,
					0, HNS_ROCE_CMD_DESTROY_AEQC,
					HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret)
		dev_err(dev, "[mailbox cmd] destroy eqc(%d) failed.\n", eqn);
}

static void free_eq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	if (!eq->hop_num || eq->hop_num == HNS_ROCE_HOP_NUM_0)
		hns_roce_mtr_cleanup(hr_dev, &eq->mtr);
	hns_roce_buf_free(hr_dev, eq->buf.size, &eq->buf);
}

static void hns_roce_config_eqc(struct hns_roce_dev *hr_dev,
				struct hns_roce_eq *eq,
				void *mb_buf)
{
	struct hns_roce_eq_context *eqc;
	u64 ba[MTT_MIN_COUNT] = { 0 };
	int count;

	eqc = mb_buf;
	memset(eqc, 0, sizeof(struct hns_roce_eq_context));

	/* init eqc */
	eq->doorbell = hr_dev->reg_base + ROCEE_VF_EQ_DB_CFG0_REG;
	eq->hop_num = hr_dev->caps.eqe_hop_num;
	eq->cons_index = 0;
	eq->over_ignore = HNS_ROCE_V2_EQ_OVER_IGNORE_0;
	eq->coalesce = HNS_ROCE_V2_EQ_COALESCE_0;
	eq->arm_st = HNS_ROCE_V2_EQ_ALWAYS_ARMED;
	eq->eqe_ba_pg_sz = hr_dev->caps.eqe_ba_pg_sz;
	eq->eqe_buf_pg_sz = hr_dev->caps.eqe_buf_pg_sz;
	eq->shift = ilog2((unsigned int)eq->entries);

	/* if not muti-hop, eqe buffer only use one trunk */
	if (!eq->hop_num || eq->hop_num == HNS_ROCE_HOP_NUM_0) {
		eq->eqe_ba = eq->buf.direct.map;
		eq->cur_eqe_ba = eq->eqe_ba;
		if (eq->buf.npages > 1)
			eq->nxt_eqe_ba = eq->eqe_ba + (1 << eq->eqe_buf_pg_sz);
		else
			eq->nxt_eqe_ba = eq->eqe_ba;
	} else {
		count = hns_roce_mtr_find(hr_dev, &eq->mtr, 0, ba,
					  MTT_MIN_COUNT, &eq->eqe_ba);
		eq->cur_eqe_ba = ba[0];
		if (count > 1)
			eq->nxt_eqe_ba = ba[1];
		else
			eq->nxt_eqe_ba = ba[0];
	}

	/* set eqc state */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_EQ_ST_M, HNS_ROCE_EQC_EQ_ST_S,
		       HNS_ROCE_V2_EQ_STATE_VALID);

	/* set eqe hop num */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_HOP_NUM_M,
		       HNS_ROCE_EQC_HOP_NUM_S, eq->hop_num);

	/* set eqc over_ignore */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_OVER_IGNORE_M,
		       HNS_ROCE_EQC_OVER_IGNORE_S, eq->over_ignore);

	/* set eqc coalesce */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_COALESCE_M,
		       HNS_ROCE_EQC_COALESCE_S, eq->coalesce);

	/* set eqc arm_state */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_ARM_ST_M,
		       HNS_ROCE_EQC_ARM_ST_S, eq->arm_st);

	/* set eqn */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_EQN_M, HNS_ROCE_EQC_EQN_S,
		       eq->eqn);

	/* set eqe_cnt */
	roce_set_field(eqc->byte_4, HNS_ROCE_EQC_EQE_CNT_M,
		       HNS_ROCE_EQC_EQE_CNT_S, HNS_ROCE_EQ_INIT_EQE_CNT);

	/* set eqe_ba_pg_sz */
	roce_set_field(eqc->byte_8, HNS_ROCE_EQC_BA_PG_SZ_M,
		       HNS_ROCE_EQC_BA_PG_SZ_S,
		       eq->eqe_ba_pg_sz + PG_SHIFT_OFFSET);

	/* set eqe_buf_pg_sz */
	roce_set_field(eqc->byte_8, HNS_ROCE_EQC_BUF_PG_SZ_M,
		       HNS_ROCE_EQC_BUF_PG_SZ_S,
		       eq->eqe_buf_pg_sz + PG_SHIFT_OFFSET);

	/* set eq_producer_idx */
	roce_set_field(eqc->byte_8, HNS_ROCE_EQC_PROD_INDX_M,
		       HNS_ROCE_EQC_PROD_INDX_S, HNS_ROCE_EQ_INIT_PROD_IDX);

	/* set eq_max_cnt */
	roce_set_field(eqc->byte_12, HNS_ROCE_EQC_MAX_CNT_M,
		       HNS_ROCE_EQC_MAX_CNT_S, eq->eq_max_cnt);

	/* set eq_period */
	roce_set_field(eqc->byte_12, HNS_ROCE_EQC_PERIOD_M,
		       HNS_ROCE_EQC_PERIOD_S, eq->eq_period);

	/* set eqe_report_timer */
	roce_set_field(eqc->eqe_report_timer, HNS_ROCE_EQC_REPORT_TIMER_M,
		       HNS_ROCE_EQC_REPORT_TIMER_S,
		       HNS_ROCE_EQ_INIT_REPORT_TIMER);

	/* set eqe_ba [34:3] */
	roce_set_field(eqc->eqe_ba0, HNS_ROCE_EQC_EQE_BA_L_M,
		       HNS_ROCE_EQC_EQE_BA_L_S, eq->eqe_ba >> 3);

	/* set eqe_ba [64:35] */
	roce_set_field(eqc->eqe_ba1, HNS_ROCE_EQC_EQE_BA_H_M,
		       HNS_ROCE_EQC_EQE_BA_H_S, eq->eqe_ba >> 35);

	/* set eq shift */
	roce_set_field(eqc->byte_28, HNS_ROCE_EQC_SHIFT_M, HNS_ROCE_EQC_SHIFT_S,
		       eq->shift);

	/* set eq MSI_IDX */
	roce_set_field(eqc->byte_28, HNS_ROCE_EQC_MSI_INDX_M,
		       HNS_ROCE_EQC_MSI_INDX_S, HNS_ROCE_EQ_INIT_MSI_IDX);

	/* set cur_eqe_ba [27:12] */
	roce_set_field(eqc->byte_28, HNS_ROCE_EQC_CUR_EQE_BA_L_M,
		       HNS_ROCE_EQC_CUR_EQE_BA_L_S, eq->cur_eqe_ba >> 12);

	/* set cur_eqe_ba [59:28] */
	roce_set_field(eqc->byte_32, HNS_ROCE_EQC_CUR_EQE_BA_M_M,
		       HNS_ROCE_EQC_CUR_EQE_BA_M_S, eq->cur_eqe_ba >> 28);

	/* set cur_eqe_ba [63:60] */
	roce_set_field(eqc->byte_36, HNS_ROCE_EQC_CUR_EQE_BA_H_M,
		       HNS_ROCE_EQC_CUR_EQE_BA_H_S, eq->cur_eqe_ba >> 60);

	/* set eq consumer idx */
	roce_set_field(eqc->byte_36, HNS_ROCE_EQC_CONS_INDX_M,
		       HNS_ROCE_EQC_CONS_INDX_S, HNS_ROCE_EQ_INIT_CONS_IDX);

	/* set nex_eqe_ba[43:12] */
	roce_set_field(eqc->nxt_eqe_ba0, HNS_ROCE_EQC_NXT_EQE_BA_L_M,
		       HNS_ROCE_EQC_NXT_EQE_BA_L_S, eq->nxt_eqe_ba >> 12);

	/* set nex_eqe_ba[63:44] */
	roce_set_field(eqc->nxt_eqe_ba1, HNS_ROCE_EQC_NXT_EQE_BA_H_M,
		       HNS_ROCE_EQC_NXT_EQE_BA_H_S, eq->nxt_eqe_ba >> 44);
}

static int map_eq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq,
		      u32 page_shift)
{
	struct hns_roce_buf_region region = {};
	dma_addr_t *buf_list = NULL;
	int ba_num;
	int ret;

	ba_num = DIV_ROUND_UP(PAGE_ALIGN(eq->entries * eq->eqe_size),
			      1 << page_shift);
	hns_roce_init_buf_region(&region, hr_dev->caps.eqe_hop_num, 0, ba_num);

	/* alloc a tmp list for storing eq buf address */
	ret = hns_roce_alloc_buf_list(&region, &buf_list, 1);
	if (ret) {
		dev_err(hr_dev->dev, "alloc eq buf_list error\n");
		return ret;
	}

	ba_num = hns_roce_get_kmem_bufs(hr_dev, buf_list, region.count,
					region.offset, &eq->buf);
	if (ba_num != region.count) {
		dev_err(hr_dev->dev, "get eqe buf err,expect %d,ret %d.\n",
			region.count, ba_num);
		ret = -ENOBUFS;
		goto done;
	}

	hns_roce_mtr_init(&eq->mtr, PAGE_SHIFT + hr_dev->caps.eqe_ba_pg_sz,
			  page_shift);
	ret = hns_roce_mtr_attach(hr_dev, &eq->mtr, &buf_list, &region, 1);
	if (ret)
		dev_err(hr_dev->dev, "mtr attach error for eqe\n");

	goto done;

	hns_roce_mtr_cleanup(hr_dev, &eq->mtr);
done:
	hns_roce_free_buf_list(&buf_list, 1);

	return ret;
}

static int alloc_eq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	struct hns_roce_buf *buf = &eq->buf;
	bool is_mhop = false;
	u32 page_shift;
	u32 mhop_num;
	u32 max_size;
	int ret;

	page_shift = PAGE_SHIFT + hr_dev->caps.eqe_buf_pg_sz;
	mhop_num = hr_dev->caps.eqe_hop_num;
	if (!mhop_num) {
		max_size = 1 << page_shift;
		buf->size = max_size;
	} else if (mhop_num == HNS_ROCE_HOP_NUM_0) {
		max_size = eq->entries * eq->eqe_size;
		buf->size = max_size;
	} else {
		max_size = 1 << page_shift;
		buf->size = PAGE_ALIGN(eq->entries * eq->eqe_size);
		is_mhop = true;
	}

	ret = hns_roce_buf_alloc(hr_dev, buf->size, max_size, buf, page_shift);
	if (ret) {
		dev_err(hr_dev->dev, "alloc eq buf error\n");
		return ret;
	}

	if (is_mhop) {
		ret = map_eq_buf(hr_dev, eq, page_shift);
		if (ret) {
			dev_err(hr_dev->dev, "map roce buf error\n");
			goto err_alloc;
		}
	}

	return 0;
err_alloc:
	hns_roce_buf_free(hr_dev, buf->size, buf);
	return ret;
}

static int hns_roce_v2_create_eq(struct hns_roce_dev *hr_dev,
				 struct hns_roce_eq *eq,
				 unsigned int eq_cmd)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = alloc_eq_buf(hr_dev, eq);
	if (ret) {
		ret = -ENOMEM;
		goto free_cmd_mbox;
	}
	hns_roce_config_eqc(hr_dev, eq, mailbox->buf);

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, eq->eqn, 0,
				eq_cmd, HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret) {
		dev_err(hr_dev->dev, "[mailbox cmd] create eqc failed.\n");
		goto err_cmd_mbox;
	}

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return 0;

err_cmd_mbox:
	free_eq_buf(hr_dev, eq);

free_cmd_mbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

static int __hns_roce_request_irq(struct hns_roce_dev *hr_dev, int irq_num,
				  int comp_num, int aeq_num, int other_num)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	int i, j;
	int ret;

	for (i = 0; i < irq_num; i++) {
		hr_dev->irq_names[i] = kzalloc(HNS_ROCE_INT_NAME_LEN,
					       GFP_KERNEL);
		if (!hr_dev->irq_names[i]) {
			ret = -ENOMEM;
			goto err_kzalloc_failed;
		}
	}

	/* irq contains: abnormal + AEQ + CEQ */
	for (j = 0; j < other_num; j++)
		snprintf((char *)hr_dev->irq_names[j], HNS_ROCE_INT_NAME_LEN,
			 "hns-abn-%d", j);

	for (j = other_num; j < (other_num + aeq_num); j++)
		snprintf((char *)hr_dev->irq_names[j], HNS_ROCE_INT_NAME_LEN,
			 "hns-aeq-%d", j - other_num);

	for (j = (other_num + aeq_num); j < irq_num; j++)
		snprintf((char *)hr_dev->irq_names[j], HNS_ROCE_INT_NAME_LEN,
			 "hns-ceq-%d", j - other_num - aeq_num);

	for (j = 0; j < irq_num; j++) {
		if (j < other_num)
			ret = request_irq(hr_dev->irq[j],
					  hns_roce_v2_msix_interrupt_abn,
					  0, hr_dev->irq_names[j], hr_dev);

		else if (j < (other_num + comp_num))
			ret = request_irq(eq_table->eq[j - other_num].irq,
					  hns_roce_v2_msix_interrupt_eq,
					  0, hr_dev->irq_names[j + aeq_num],
					  &eq_table->eq[j - other_num]);
		else
			ret = request_irq(eq_table->eq[j - other_num].irq,
					  hns_roce_v2_msix_interrupt_eq,
					  0, hr_dev->irq_names[j - comp_num],
					  &eq_table->eq[j - other_num]);
		if (ret) {
			dev_err(hr_dev->dev, "Request irq error!\n");
			goto err_request_failed;
		}
	}

	return 0;

err_request_failed:
	for (j -= 1; j >= 0; j--)
		if (j < other_num)
			free_irq(hr_dev->irq[j], hr_dev);
		else
			free_irq(eq_table->eq[j - other_num].irq,
				 &eq_table->eq[j - other_num]);

err_kzalloc_failed:
	for (i -= 1; i >= 0; i--)
		kfree(hr_dev->irq_names[i]);

	return ret;
}

static void __hns_roce_free_irq(struct hns_roce_dev *hr_dev)
{
	int irq_num;
	int eq_num;
	int i;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;
	irq_num = eq_num + hr_dev->caps.num_other_vectors;

	for (i = 0; i < hr_dev->caps.num_other_vectors; i++)
		free_irq(hr_dev->irq[i], hr_dev);

	for (i = 0; i < eq_num; i++)
		free_irq(hr_dev->eq_table.eq[i].irq, &hr_dev->eq_table.eq[i]);

	for (i = 0; i < irq_num; i++)
		kfree(hr_dev->irq_names[i]);
}

static int hns_roce_v2_init_eq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	struct device *dev = hr_dev->dev;
	struct hns_roce_eq *eq;
	unsigned int eq_cmd;
	int irq_num;
	int eq_num;
	int other_num;
	int comp_num;
	int aeq_num;
	int i;
	int ret;

	other_num = hr_dev->caps.num_other_vectors;
	comp_num = hr_dev->caps.num_comp_vectors;
	aeq_num = hr_dev->caps.num_aeq_vectors;

	eq_num = comp_num + aeq_num;
	irq_num = eq_num + other_num;

	eq_table->eq = kcalloc(eq_num, sizeof(*eq_table->eq), GFP_KERNEL);
	if (!eq_table->eq)
		return -ENOMEM;

	/* create eq */
	for (i = 0; i < eq_num; i++) {
		eq = &eq_table->eq[i];
		eq->hr_dev = hr_dev;
		eq->eqn = i;
		if (i < comp_num) {
			/* CEQ */
			eq_cmd = HNS_ROCE_CMD_CREATE_CEQC;
			eq->type_flag = HNS_ROCE_CEQ;
			eq->entries = hr_dev->caps.ceqe_depth;
			eq->eqe_size = HNS_ROCE_CEQ_ENTRY_SIZE;
			eq->irq = hr_dev->irq[i + other_num + aeq_num];
			eq->eq_max_cnt = HNS_ROCE_CEQ_DEFAULT_BURST_NUM;
			eq->eq_period = HNS_ROCE_CEQ_DEFAULT_INTERVAL;
		} else {
			/* AEQ */
			eq_cmd = HNS_ROCE_CMD_CREATE_AEQC;
			eq->type_flag = HNS_ROCE_AEQ;
			eq->entries = hr_dev->caps.aeqe_depth;
			eq->eqe_size = HNS_ROCE_AEQ_ENTRY_SIZE;
			eq->irq = hr_dev->irq[i - comp_num + other_num];
			eq->eq_max_cnt = HNS_ROCE_AEQ_DEFAULT_BURST_NUM;
			eq->eq_period = HNS_ROCE_AEQ_DEFAULT_INTERVAL;
		}

		ret = hns_roce_v2_create_eq(hr_dev, eq, eq_cmd);
		if (ret) {
			dev_err(dev, "eq create failed.\n");
			goto err_create_eq_fail;
		}
	}

	/* enable irq */
	hns_roce_v2_int_mask_enable(hr_dev, eq_num, EQ_ENABLE);

	ret = __hns_roce_request_irq(hr_dev, irq_num, comp_num,
				     aeq_num, other_num);
	if (ret) {
		dev_err(dev, "Request irq failed.\n");
		goto err_request_irq_fail;
	}

	hr_dev->irq_workq = alloc_ordered_workqueue("hns_roce_irq_workq", 0);
	if (!hr_dev->irq_workq) {
		dev_err(dev, "Create irq workqueue failed!\n");
		ret = -ENOMEM;
		goto err_create_wq_fail;
	}

	return 0;

err_create_wq_fail:
	__hns_roce_free_irq(hr_dev);

err_request_irq_fail:
	hns_roce_v2_int_mask_enable(hr_dev, eq_num, EQ_DISABLE);

err_create_eq_fail:
	for (i -= 1; i >= 0; i--)
		free_eq_buf(hr_dev, &eq_table->eq[i]);
	kfree(eq_table->eq);

	return ret;
}

static void hns_roce_v2_cleanup_eq_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_eq_table *eq_table = &hr_dev->eq_table;
	int eq_num;
	int i;

	eq_num = hr_dev->caps.num_comp_vectors + hr_dev->caps.num_aeq_vectors;

	/* Disable irq */
	hns_roce_v2_int_mask_enable(hr_dev, eq_num, EQ_DISABLE);

	__hns_roce_free_irq(hr_dev);

	for (i = 0; i < eq_num; i++) {
		hns_roce_v2_destroy_eqc(hr_dev, i);

		free_eq_buf(hr_dev, &eq_table->eq[i]);
	}

	kfree(eq_table->eq);

	flush_workqueue(hr_dev->irq_workq);
	destroy_workqueue(hr_dev->irq_workq);
}

static void hns_roce_v2_write_srqc(struct hns_roce_dev *hr_dev,
				   struct hns_roce_srq *srq, u32 pdn, u16 xrcd,
				   u32 cqn, void *mb_buf, u64 *mtts_wqe,
				   u64 *mtts_idx, dma_addr_t dma_handle_wqe,
				   dma_addr_t dma_handle_idx)
{
	struct hns_roce_srq_context *srq_context;

	srq_context = mb_buf;
	memset(srq_context, 0, sizeof(*srq_context));

	roce_set_field(srq_context->byte_4_srqn_srqst, SRQC_BYTE_4_SRQ_ST_M,
		       SRQC_BYTE_4_SRQ_ST_S, 1);

	roce_set_field(srq_context->byte_4_srqn_srqst,
		       SRQC_BYTE_4_SRQ_WQE_HOP_NUM_M,
		       SRQC_BYTE_4_SRQ_WQE_HOP_NUM_S,
		       (hr_dev->caps.srqwqe_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 :
		       hr_dev->caps.srqwqe_hop_num));
	roce_set_field(srq_context->byte_4_srqn_srqst,
		       SRQC_BYTE_4_SRQ_SHIFT_M, SRQC_BYTE_4_SRQ_SHIFT_S,
		       ilog2(srq->wqe_cnt));

	roce_set_field(srq_context->byte_4_srqn_srqst, SRQC_BYTE_4_SRQN_M,
		       SRQC_BYTE_4_SRQN_S, srq->srqn);

	roce_set_field(srq_context->byte_8_limit_wl, SRQC_BYTE_8_SRQ_LIMIT_WL_M,
		       SRQC_BYTE_8_SRQ_LIMIT_WL_S, 0);

	roce_set_field(srq_context->byte_12_xrcd, SRQC_BYTE_12_SRQ_XRCD_M,
		       SRQC_BYTE_12_SRQ_XRCD_S, xrcd);

	srq_context->wqe_bt_ba = cpu_to_le32((u32)(dma_handle_wqe >> 3));

	roce_set_field(srq_context->byte_24_wqe_bt_ba,
		       SRQC_BYTE_24_SRQ_WQE_BT_BA_M,
		       SRQC_BYTE_24_SRQ_WQE_BT_BA_S,
		       dma_handle_wqe >> 35);

	roce_set_field(srq_context->byte_28_rqws_pd, SRQC_BYTE_28_PD_M,
		       SRQC_BYTE_28_PD_S, pdn);
	roce_set_field(srq_context->byte_28_rqws_pd, SRQC_BYTE_28_RQWS_M,
		       SRQC_BYTE_28_RQWS_S, srq->max_gs <= 0 ? 0 :
		       fls(srq->max_gs - 1));

	srq_context->idx_bt_ba = cpu_to_le32(dma_handle_idx >> 3);
	roce_set_field(srq_context->rsv_idx_bt_ba,
		       SRQC_BYTE_36_SRQ_IDX_BT_BA_M,
		       SRQC_BYTE_36_SRQ_IDX_BT_BA_S,
		       dma_handle_idx >> 35);

	srq_context->idx_cur_blk_addr =
		cpu_to_le32(mtts_idx[0] >> PAGE_ADDR_SHIFT);
	roce_set_field(srq_context->byte_44_idxbufpgsz_addr,
		       SRQC_BYTE_44_SRQ_IDX_CUR_BLK_ADDR_M,
		       SRQC_BYTE_44_SRQ_IDX_CUR_BLK_ADDR_S,
		       mtts_idx[0] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(srq_context->byte_44_idxbufpgsz_addr,
		       SRQC_BYTE_44_SRQ_IDX_HOP_NUM_M,
		       SRQC_BYTE_44_SRQ_IDX_HOP_NUM_S,
		       hr_dev->caps.idx_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 :
		       hr_dev->caps.idx_hop_num);

	roce_set_field(srq_context->byte_44_idxbufpgsz_addr,
		       SRQC_BYTE_44_SRQ_IDX_BA_PG_SZ_M,
		       SRQC_BYTE_44_SRQ_IDX_BA_PG_SZ_S,
		       hr_dev->caps.idx_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(srq_context->byte_44_idxbufpgsz_addr,
		       SRQC_BYTE_44_SRQ_IDX_BUF_PG_SZ_M,
		       SRQC_BYTE_44_SRQ_IDX_BUF_PG_SZ_S,
		       hr_dev->caps.idx_buf_pg_sz + PG_SHIFT_OFFSET);

	srq_context->idx_nxt_blk_addr =
		cpu_to_le32(mtts_idx[1] >> PAGE_ADDR_SHIFT);
	roce_set_field(srq_context->rsv_idxnxtblkaddr,
		       SRQC_BYTE_52_SRQ_IDX_NXT_BLK_ADDR_M,
		       SRQC_BYTE_52_SRQ_IDX_NXT_BLK_ADDR_S,
		       mtts_idx[1] >> (32 + PAGE_ADDR_SHIFT));
	roce_set_field(srq_context->byte_56_xrc_cqn,
		       SRQC_BYTE_56_SRQ_XRC_CQN_M, SRQC_BYTE_56_SRQ_XRC_CQN_S,
		       cqn);
	roce_set_field(srq_context->byte_56_xrc_cqn,
		       SRQC_BYTE_56_SRQ_WQE_BA_PG_SZ_M,
		       SRQC_BYTE_56_SRQ_WQE_BA_PG_SZ_S,
		       hr_dev->caps.srqwqe_ba_pg_sz + PG_SHIFT_OFFSET);
	roce_set_field(srq_context->byte_56_xrc_cqn,
		       SRQC_BYTE_56_SRQ_WQE_BUF_PG_SZ_M,
		       SRQC_BYTE_56_SRQ_WQE_BUF_PG_SZ_S,
		       hr_dev->caps.srqwqe_buf_pg_sz + PG_SHIFT_OFFSET);

	roce_set_bit(srq_context->db_record_addr_record_en,
		     SRQC_BYTE_60_SRQ_RECORD_EN_S, 0);
}

static int hns_roce_v2_modify_srq(struct ib_srq *ibsrq,
				  struct ib_srq_attr *srq_attr,
				  enum ib_srq_attr_mask srq_attr_mask,
				  struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);
	struct hns_roce_srq_context *srq_context;
	struct hns_roce_srq_context *srqc_mask;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	if (srq_attr_mask & IB_SRQ_LIMIT) {
		if (srq_attr->srq_limit >= srq->wqe_cnt)
			return -EINVAL;

		mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
		if (IS_ERR(mailbox))
			return PTR_ERR(mailbox);

		srq_context = mailbox->buf;
		srqc_mask = (struct hns_roce_srq_context *)mailbox->buf + 1;

		memset(srqc_mask, 0xff, sizeof(*srqc_mask));

		roce_set_field(srq_context->byte_8_limit_wl,
			       SRQC_BYTE_8_SRQ_LIMIT_WL_M,
			       SRQC_BYTE_8_SRQ_LIMIT_WL_S, srq_attr->srq_limit);
		roce_set_field(srqc_mask->byte_8_limit_wl,
			       SRQC_BYTE_8_SRQ_LIMIT_WL_M,
			       SRQC_BYTE_8_SRQ_LIMIT_WL_S, 0);

		ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0, srq->srqn, 0,
					HNS_ROCE_CMD_MODIFY_SRQC,
					HNS_ROCE_CMD_TIMEOUT_MSECS);
		hns_roce_free_cmd_mailbox(hr_dev, mailbox);
		if (ret) {
			ibdev_err(&hr_dev->ib_dev,
				  "failed to process cmd when modifying SRQ, ret = %d\n",
				  ret);
			return ret;
		}
	}

	return 0;
}

static int hns_roce_v2_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);
	struct hns_roce_srq_context *srq_context;
	struct hns_roce_cmd_mailbox *mailbox;
	int limit_wl;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	srq_context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, srq->srqn, 0,
				HNS_ROCE_CMD_QUERY_SRQC,
				HNS_ROCE_CMD_TIMEOUT_MSECS);
	if (ret) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd when querying SRQ, ret = %d\n",
			  ret);
		goto out;
	}

	limit_wl = roce_get_field(srq_context->byte_8_limit_wl,
				  SRQC_BYTE_8_SRQ_LIMIT_WL_M,
				  SRQC_BYTE_8_SRQ_LIMIT_WL_S);

	attr->srq_limit = limit_wl;
	attr->max_wr    = srq->wqe_cnt - 1;
	attr->max_sge   = srq->max_gs;

	memcpy(srq_context, mailbox->buf, sizeof(*srq_context));

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int find_empty_entry(struct hns_roce_idx_que *idx_que,
			    unsigned long size)
{
	int wqe_idx;

	if (unlikely(bitmap_full(idx_que->bitmap, size)))
		return -ENOSPC;

	wqe_idx = find_first_zero_bit(idx_que->bitmap, size);

	bitmap_set(idx_que->bitmap, wqe_idx, 1);

	return wqe_idx;
}

static void fill_idx_queue(struct hns_roce_idx_que *idx_que,
			   int cur_idx, int wqe_idx)
{
	unsigned int *addr;

	addr = (unsigned int *)hns_roce_buf_offset(&idx_que->idx_buf,
						   cur_idx * idx_que->entry_sz);
	*addr = wqe_idx;
}

static int hns_roce_v2_post_srq_recv(struct ib_srq *ibsrq,
				     const struct ib_recv_wr *wr,
				     const struct ib_recv_wr **bad_wr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);
	struct hns_roce_v2_wqe_data_seg *dseg;
	struct hns_roce_v2_db srq_db;
	unsigned long flags;
	int ret = 0;
	int wqe_idx;
	void *wqe;
	int nreq;
	int ind;
	int i;

	spin_lock_irqsave(&srq->lock, flags);

	ind = srq->head & (srq->wqe_cnt - 1);

	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(wr->num_sge > srq->max_gs)) {
			ret = -EINVAL;
			*bad_wr = wr;
			break;
		}

		if (unlikely(srq->head == srq->tail)) {
			ret = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		wqe_idx = find_empty_entry(&srq->idx_que, srq->wqe_cnt);
		if (wqe_idx < 0) {
			ret = -ENOMEM;
			*bad_wr = wr;
			break;
		}

		fill_idx_queue(&srq->idx_que, ind, wqe_idx);
		wqe = get_srq_wqe(srq, wqe_idx);
		dseg = (struct hns_roce_v2_wqe_data_seg *)wqe;

		for (i = 0; i < wr->num_sge; ++i) {
			dseg[i].len = cpu_to_le32(wr->sg_list[i].length);
			dseg[i].lkey = cpu_to_le32(wr->sg_list[i].lkey);
			dseg[i].addr = cpu_to_le64(wr->sg_list[i].addr);
		}

		if (i < srq->max_gs) {
			dseg[i].len = 0;
			dseg[i].lkey = cpu_to_le32(0x100);
			dseg[i].addr = 0;
		}

		srq->wrid[wqe_idx] = wr->wr_id;
		ind = (ind + 1) & (srq->wqe_cnt - 1);
	}

	if (likely(nreq)) {
		srq->head += nreq;

		/*
		 * Make sure that descriptors are written before
		 * doorbell record.
		 */
		wmb();

		srq_db.byte_4 =
			cpu_to_le32(HNS_ROCE_V2_SRQ_DB << V2_DB_BYTE_4_CMD_S |
				    (srq->srqn & V2_DB_BYTE_4_TAG_M));
		srq_db.parameter = cpu_to_le32(srq->head);

		hns_roce_write64(hr_dev, (__le32 *)&srq_db, srq->db_reg_l);

	}

	spin_unlock_irqrestore(&srq->lock, flags);

	return ret;
}

static const struct hns_roce_dfx_hw hns_roce_dfx_hw_v2 = {
	.query_cqc_info = hns_roce_v2_query_cqc_info,
};

static const struct ib_device_ops hns_roce_v2_dev_ops = {
	.destroy_qp = hns_roce_v2_destroy_qp,
	.modify_cq = hns_roce_v2_modify_cq,
	.poll_cq = hns_roce_v2_poll_cq,
	.post_recv = hns_roce_v2_post_recv,
	.post_send = hns_roce_v2_post_send,
	.query_qp = hns_roce_v2_query_qp,
	.req_notify_cq = hns_roce_v2_req_notify_cq,
};

static const struct ib_device_ops hns_roce_v2_dev_srq_ops = {
	.modify_srq = hns_roce_v2_modify_srq,
	.post_srq_recv = hns_roce_v2_post_srq_recv,
	.query_srq = hns_roce_v2_query_srq,
};

static const struct hns_roce_hw hns_roce_hw_v2 = {
	.cmq_init = hns_roce_v2_cmq_init,
	.cmq_exit = hns_roce_v2_cmq_exit,
	.hw_profile = hns_roce_v2_profile,
	.hw_init = hns_roce_v2_init,
	.hw_exit = hns_roce_v2_exit,
	.post_mbox = hns_roce_v2_post_mbox,
	.chk_mbox = hns_roce_v2_chk_mbox,
	.rst_prc_mbox = hns_roce_v2_rst_process_cmd,
	.set_gid = hns_roce_v2_set_gid,
	.set_mac = hns_roce_v2_set_mac,
	.write_mtpt = hns_roce_v2_write_mtpt,
	.rereg_write_mtpt = hns_roce_v2_rereg_write_mtpt,
	.frmr_write_mtpt = hns_roce_v2_frmr_write_mtpt,
	.mw_write_mtpt = hns_roce_v2_mw_write_mtpt,
	.write_cqc = hns_roce_v2_write_cqc,
	.set_hem = hns_roce_v2_set_hem,
	.clear_hem = hns_roce_v2_clear_hem,
	.modify_qp = hns_roce_v2_modify_qp,
	.query_qp = hns_roce_v2_query_qp,
	.destroy_qp = hns_roce_v2_destroy_qp,
	.qp_flow_control_init = hns_roce_v2_qp_flow_control_init,
	.modify_cq = hns_roce_v2_modify_cq,
	.post_send = hns_roce_v2_post_send,
	.post_recv = hns_roce_v2_post_recv,
	.req_notify_cq = hns_roce_v2_req_notify_cq,
	.poll_cq = hns_roce_v2_poll_cq,
	.init_eq = hns_roce_v2_init_eq_table,
	.cleanup_eq = hns_roce_v2_cleanup_eq_table,
	.write_srqc = hns_roce_v2_write_srqc,
	.modify_srq = hns_roce_v2_modify_srq,
	.query_srq = hns_roce_v2_query_srq,
	.post_srq_recv = hns_roce_v2_post_srq_recv,
	.hns_roce_dev_ops = &hns_roce_v2_dev_ops,
	.hns_roce_dev_srq_ops = &hns_roce_v2_dev_srq_ops,
};

static const struct pci_device_id hns_roce_hw_v2_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC), 0},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, hns_roce_hw_v2_pci_tbl);

static void hns_roce_hw_v2_get_cfg(struct hns_roce_dev *hr_dev,
				  struct hnae3_handle *handle)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	int i;

	hr_dev->pci_dev = handle->pdev;
	hr_dev->dev = &handle->pdev->dev;
	hr_dev->hw = &hns_roce_hw_v2;
	hr_dev->dfx = &hns_roce_dfx_hw_v2;
	hr_dev->sdb_offset = ROCEE_DB_SQ_L_0_REG;
	hr_dev->odb_offset = hr_dev->sdb_offset;

	/* Get info from NIC driver. */
	hr_dev->reg_base = handle->rinfo.roce_io_base;
	hr_dev->caps.num_ports = 1;
	hr_dev->iboe.netdevs[0] = handle->rinfo.netdev;
	hr_dev->iboe.phy_port[0] = 0;

	addrconf_addr_eui48((u8 *)&hr_dev->ib_dev.node_guid,
			    hr_dev->iboe.netdevs[0]->dev_addr);

	for (i = 0; i < HNS_ROCE_V2_MAX_IRQ_NUM; i++)
		hr_dev->irq[i] = pci_irq_vector(handle->pdev,
						i + handle->rinfo.base_vector);

	/* cmd issue mode: 0 is poll, 1 is event */
	hr_dev->cmd_mod = 1;
	hr_dev->loop_idc = 0;

	hr_dev->reset_cnt = handle->ae_algo->ops->ae_dev_reset_cnt(handle);
	priv->handle = handle;
}

static int __hns_roce_hw_v2_init_instance(struct hnae3_handle *handle)
{
	struct hns_roce_dev *hr_dev;
	int ret;

	hr_dev = ib_alloc_device(hns_roce_dev, ib_dev);
	if (!hr_dev)
		return -ENOMEM;

	hr_dev->priv = kzalloc(sizeof(struct hns_roce_v2_priv), GFP_KERNEL);
	if (!hr_dev->priv) {
		ret = -ENOMEM;
		goto error_failed_kzalloc;
	}

	hns_roce_hw_v2_get_cfg(hr_dev, handle);

	ret = hns_roce_init(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "RoCE Engine init failed!\n");
		goto error_failed_get_cfg;
	}

	handle->priv = hr_dev;

	return 0;

error_failed_get_cfg:
	kfree(hr_dev->priv);

error_failed_kzalloc:
	ib_dealloc_device(&hr_dev->ib_dev);

	return ret;
}

static void __hns_roce_hw_v2_uninit_instance(struct hnae3_handle *handle,
					   bool reset)
{
	struct hns_roce_dev *hr_dev = (struct hns_roce_dev *)handle->priv;

	if (!hr_dev)
		return;

	handle->priv = NULL;

	hr_dev->state = HNS_ROCE_DEVICE_STATE_UNINIT;
	hns_roce_handle_device_err(hr_dev);

	hns_roce_exit(hr_dev);
	kfree(hr_dev->priv);
	ib_dealloc_device(&hr_dev->ib_dev);
}

static int hns_roce_hw_v2_init_instance(struct hnae3_handle *handle)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	const struct pci_device_id *id;
	struct device *dev = &handle->pdev->dev;
	int ret;

	handle->rinfo.instance_state = HNS_ROCE_STATE_INIT;

	if (ops->ae_dev_resetting(handle) || ops->get_hw_reset_stat(handle)) {
		handle->rinfo.instance_state = HNS_ROCE_STATE_NON_INIT;
		goto reset_chk_err;
	}

	id = pci_match_id(hns_roce_hw_v2_pci_tbl, handle->pdev);
	if (!id)
		return 0;

	ret = __hns_roce_hw_v2_init_instance(handle);
	if (ret) {
		handle->rinfo.instance_state = HNS_ROCE_STATE_NON_INIT;
		dev_err(dev, "RoCE instance init failed! ret = %d\n", ret);
		if (ops->ae_dev_resetting(handle) ||
		    ops->get_hw_reset_stat(handle))
			goto reset_chk_err;
		else
			return ret;
	}

	handle->rinfo.instance_state = HNS_ROCE_STATE_INITED;


	return 0;

reset_chk_err:
	dev_err(dev, "Device is busy in resetting state.\n"
		     "please retry later.\n");

	return -EBUSY;
}

static void hns_roce_hw_v2_uninit_instance(struct hnae3_handle *handle,
					   bool reset)
{
	if (handle->rinfo.instance_state != HNS_ROCE_STATE_INITED)
		return;

	handle->rinfo.instance_state = HNS_ROCE_STATE_UNINIT;

	__hns_roce_hw_v2_uninit_instance(handle, reset);

	handle->rinfo.instance_state = HNS_ROCE_STATE_NON_INIT;
}
static int hns_roce_hw_v2_reset_notify_down(struct hnae3_handle *handle)
{
	struct hns_roce_dev *hr_dev;

	if (handle->rinfo.instance_state != HNS_ROCE_STATE_INITED) {
		set_bit(HNS_ROCE_RST_DIRECT_RETURN, &handle->rinfo.state);
		return 0;
	}

	handle->rinfo.reset_state = HNS_ROCE_STATE_RST_DOWN;
	clear_bit(HNS_ROCE_RST_DIRECT_RETURN, &handle->rinfo.state);

	hr_dev = (struct hns_roce_dev *)handle->priv;
	if (!hr_dev)
		return 0;

	hr_dev->is_reset = true;
	hr_dev->active = false;
	hr_dev->dis_db = true;

	hr_dev->state = HNS_ROCE_DEVICE_STATE_RST_DOWN;

	return 0;
}

static int hns_roce_hw_v2_reset_notify_init(struct hnae3_handle *handle)
{
	struct device *dev = &handle->pdev->dev;
	int ret;

	if (test_and_clear_bit(HNS_ROCE_RST_DIRECT_RETURN,
			       &handle->rinfo.state)) {
		handle->rinfo.reset_state = HNS_ROCE_STATE_RST_INITED;
		return 0;
	}

	handle->rinfo.reset_state = HNS_ROCE_STATE_RST_INIT;

	dev_info(&handle->pdev->dev, "In reset process RoCE client reinit.\n");
	ret = __hns_roce_hw_v2_init_instance(handle);
	if (ret) {
		/* when reset notify type is HNAE3_INIT_CLIENT In reset notify
		 * callback function, RoCE Engine reinitialize. If RoCE reinit
		 * failed, we should inform NIC driver.
		 */
		handle->priv = NULL;
		dev_err(dev, "In reset process RoCE reinit failed %d.\n", ret);
	} else {
		handle->rinfo.reset_state = HNS_ROCE_STATE_RST_INITED;
		dev_info(dev, "Reset done, RoCE client reinit finished.\n");
	}

	return ret;
}

static int hns_roce_hw_v2_reset_notify_uninit(struct hnae3_handle *handle)
{
	if (test_bit(HNS_ROCE_RST_DIRECT_RETURN, &handle->rinfo.state))
		return 0;

	handle->rinfo.reset_state = HNS_ROCE_STATE_RST_UNINIT;
	dev_info(&handle->pdev->dev, "In reset process RoCE client uninit.\n");
	msleep(HNS_ROCE_V2_HW_RST_UNINT_DELAY);
	__hns_roce_hw_v2_uninit_instance(handle, false);

	return 0;
}

static int hns_roce_hw_v2_reset_notify(struct hnae3_handle *handle,
				       enum hnae3_reset_notify_type type)
{
	int ret = 0;

	switch (type) {
	case HNAE3_DOWN_CLIENT:
		ret = hns_roce_hw_v2_reset_notify_down(handle);
		break;
	case HNAE3_INIT_CLIENT:
		ret = hns_roce_hw_v2_reset_notify_init(handle);
		break;
	case HNAE3_UNINIT_CLIENT:
		ret = hns_roce_hw_v2_reset_notify_uninit(handle);
		break;
	default:
		break;
	}

	return ret;
}

static const struct hnae3_client_ops hns_roce_hw_v2_ops = {
	.init_instance = hns_roce_hw_v2_init_instance,
	.uninit_instance = hns_roce_hw_v2_uninit_instance,
	.reset_notify = hns_roce_hw_v2_reset_notify,
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
