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
#include <linux/iopoll.h>
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

enum {
	CMD_RST_PRC_OTHERS,
	CMD_RST_PRC_SUCCESS,
	CMD_RST_PRC_EBUSY,
};

enum ecc_resource_type {
	ECC_RESOURCE_QPC,
	ECC_RESOURCE_CQC,
	ECC_RESOURCE_MPT,
	ECC_RESOURCE_SRQC,
	ECC_RESOURCE_GMV,
	ECC_RESOURCE_QPC_TIMER,
	ECC_RESOURCE_CQC_TIMER,
	ECC_RESOURCE_SCCC,
	ECC_RESOURCE_COUNT,
};

static const struct {
	const char *name;
	u8 read_bt0_op;
	u8 write_bt0_op;
} fmea_ram_res[] = {
	{ "ECC_RESOURCE_QPC",
	  HNS_ROCE_CMD_READ_QPC_BT0, HNS_ROCE_CMD_WRITE_QPC_BT0 },
	{ "ECC_RESOURCE_CQC",
	  HNS_ROCE_CMD_READ_CQC_BT0, HNS_ROCE_CMD_WRITE_CQC_BT0 },
	{ "ECC_RESOURCE_MPT",
	  HNS_ROCE_CMD_READ_MPT_BT0, HNS_ROCE_CMD_WRITE_MPT_BT0 },
	{ "ECC_RESOURCE_SRQC",
	  HNS_ROCE_CMD_READ_SRQC_BT0, HNS_ROCE_CMD_WRITE_SRQC_BT0 },
	/* ECC_RESOURCE_GMV is handled by cmdq, not mailbox */
	{ "ECC_RESOURCE_GMV",
	  0, 0 },
	{ "ECC_RESOURCE_QPC_TIMER",
	  HNS_ROCE_CMD_READ_QPC_TIMER_BT0, HNS_ROCE_CMD_WRITE_QPC_TIMER_BT0 },
	{ "ECC_RESOURCE_CQC_TIMER",
	  HNS_ROCE_CMD_READ_CQC_TIMER_BT0, HNS_ROCE_CMD_WRITE_CQC_TIMER_BT0 },
	{ "ECC_RESOURCE_SCCC",
	  HNS_ROCE_CMD_READ_SCCC_BT0, HNS_ROCE_CMD_WRITE_SCCC_BT0 },
};

static inline void set_data_seg_v2(struct hns_roce_v2_wqe_data_seg *dseg,
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
			 const struct ib_reg_wr *wr)
{
	struct hns_roce_wqe_frmr_seg *fseg =
		(void *)rc_sq_wqe + sizeof(struct hns_roce_v2_rc_send_wqe);
	struct hns_roce_mr *mr = to_hr_mr(wr->mr);
	u64 pbl_ba;

	/* use ib_access_flags */
	hr_reg_write_bool(fseg, FRMR_BIND_EN, wr->access & IB_ACCESS_MW_BIND);
	hr_reg_write_bool(fseg, FRMR_ATOMIC,
			  wr->access & IB_ACCESS_REMOTE_ATOMIC);
	hr_reg_write_bool(fseg, FRMR_RR, wr->access & IB_ACCESS_REMOTE_READ);
	hr_reg_write_bool(fseg, FRMR_RW, wr->access & IB_ACCESS_REMOTE_WRITE);
	hr_reg_write_bool(fseg, FRMR_LW, wr->access & IB_ACCESS_LOCAL_WRITE);

	/* Data structure reuse may lead to confusion */
	pbl_ba = mr->pbl_mtr.hem_cfg.root_ba;
	rc_sq_wqe->msg_len = cpu_to_le32(lower_32_bits(pbl_ba));
	rc_sq_wqe->inv_key = cpu_to_le32(upper_32_bits(pbl_ba));

	rc_sq_wqe->byte_16 = cpu_to_le32(wr->mr->length & 0xffffffff);
	rc_sq_wqe->byte_20 = cpu_to_le32(wr->mr->length >> 32);
	rc_sq_wqe->rkey = cpu_to_le32(wr->key);
	rc_sq_wqe->va = cpu_to_le64(wr->mr->iova);

	hr_reg_write(fseg, FRMR_PBL_SIZE, mr->npages);
	hr_reg_write(fseg, FRMR_PBL_BUF_PG_SZ,
		     to_hr_hw_page_shift(mr->pbl_mtr.hem_cfg.buf_pg_shift));
	hr_reg_clear(fseg, FRMR_BLK_MODE);
}

static void set_atomic_seg(const struct ib_send_wr *wr,
			   struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			   unsigned int valid_num_sge)
{
	struct hns_roce_v2_wqe_data_seg *dseg =
		(void *)rc_sq_wqe + sizeof(struct hns_roce_v2_rc_send_wqe);
	struct hns_roce_wqe_atomic_seg *aseg =
		(void *)dseg + sizeof(struct hns_roce_v2_wqe_data_seg);

	set_data_seg_v2(dseg, wr->sg_list);

	if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP) {
		aseg->fetchadd_swap_data = cpu_to_le64(atomic_wr(wr)->swap);
		aseg->cmp_data = cpu_to_le64(atomic_wr(wr)->compare_add);
	} else {
		aseg->fetchadd_swap_data =
			cpu_to_le64(atomic_wr(wr)->compare_add);
		aseg->cmp_data = 0;
	}

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_SGE_NUM, valid_num_sge);
}

static int fill_ext_sge_inl_data(struct hns_roce_qp *qp,
				 const struct ib_send_wr *wr,
				 unsigned int *sge_idx, u32 msg_len)
{
	struct ib_device *ibdev = &(to_hr_dev(qp->ibqp.device))->ib_dev;
	unsigned int left_len_in_pg;
	unsigned int idx = *sge_idx;
	unsigned int i = 0;
	unsigned int len;
	void *addr;
	void *dseg;

	if (msg_len > qp->sq.ext_sge_cnt * HNS_ROCE_SGE_SIZE) {
		ibdev_err(ibdev,
			  "no enough extended sge space for inline data.\n");
		return -EINVAL;
	}

	dseg = hns_roce_get_extend_sge(qp, idx & (qp->sge.sge_cnt - 1));
	left_len_in_pg = hr_hw_page_align((uintptr_t)dseg) - (uintptr_t)dseg;
	len = wr->sg_list[0].length;
	addr = (void *)(unsigned long)(wr->sg_list[0].addr);

	/* When copying data to extended sge space, the left length in page may
	 * not long enough for current user's sge. So the data should be
	 * splited into several parts, one in the first page, and the others in
	 * the subsequent pages.
	 */
	while (1) {
		if (len <= left_len_in_pg) {
			memcpy(dseg, addr, len);

			idx += len / HNS_ROCE_SGE_SIZE;

			i++;
			if (i >= wr->num_sge)
				break;

			left_len_in_pg -= len;
			len = wr->sg_list[i].length;
			addr = (void *)(unsigned long)(wr->sg_list[i].addr);
			dseg += len;
		} else {
			memcpy(dseg, addr, left_len_in_pg);

			len -= left_len_in_pg;
			addr += left_len_in_pg;
			idx += left_len_in_pg / HNS_ROCE_SGE_SIZE;
			dseg = hns_roce_get_extend_sge(qp,
						idx & (qp->sge.sge_cnt - 1));
			left_len_in_pg = 1 << HNS_HW_PAGE_SHIFT;
		}
	}

	*sge_idx = idx;

	return 0;
}

static void set_extend_sge(struct hns_roce_qp *qp, struct ib_sge *sge,
			   unsigned int *sge_ind, unsigned int cnt)
{
	struct hns_roce_v2_wqe_data_seg *dseg;
	unsigned int idx = *sge_ind;

	while (cnt > 0) {
		dseg = hns_roce_get_extend_sge(qp, idx & (qp->sge.sge_cnt - 1));
		if (likely(sge->length)) {
			set_data_seg_v2(dseg, sge);
			idx++;
			cnt--;
		}
		sge++;
	}

	*sge_ind = idx;
}

static bool check_inl_data_len(struct hns_roce_qp *qp, unsigned int len)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(qp->ibqp.device);
	int mtu = ib_mtu_enum_to_int(qp->path_mtu);

	if (mtu < 0 || len > qp->max_inline_data || len > mtu) {
		ibdev_err(&hr_dev->ib_dev,
			  "invalid length of data, data len = %u, max inline len = %u, path mtu = %d.\n",
			  len, qp->max_inline_data, mtu);
		return false;
	}

	return true;
}

static int set_rc_inl(struct hns_roce_qp *qp, const struct ib_send_wr *wr,
		      struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
		      unsigned int *sge_idx)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(qp->ibqp.device);
	u32 msg_len = le32_to_cpu(rc_sq_wqe->msg_len);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	unsigned int curr_idx = *sge_idx;
	void *dseg = rc_sq_wqe;
	unsigned int i;
	int ret;

	if (unlikely(wr->opcode == IB_WR_RDMA_READ)) {
		ibdev_err(ibdev, "invalid inline parameters!\n");
		return -EINVAL;
	}

	if (!check_inl_data_len(qp, msg_len))
		return -EINVAL;

	dseg += sizeof(struct hns_roce_v2_rc_send_wqe);

	if (msg_len <= HNS_ROCE_V2_MAX_RC_INL_INN_SZ) {
		hr_reg_clear(rc_sq_wqe, RC_SEND_WQE_INL_TYPE);

		for (i = 0; i < wr->num_sge; i++) {
			memcpy(dseg, ((void *)wr->sg_list[i].addr),
			       wr->sg_list[i].length);
			dseg += wr->sg_list[i].length;
		}
	} else {
		hr_reg_enable(rc_sq_wqe, RC_SEND_WQE_INL_TYPE);

		ret = fill_ext_sge_inl_data(qp, wr, &curr_idx, msg_len);
		if (ret)
			return ret;

		hr_reg_write(rc_sq_wqe, RC_SEND_WQE_SGE_NUM, curr_idx - *sge_idx);
	}

	*sge_idx = curr_idx;

	return 0;
}

static int set_rwqe_data_seg(struct ib_qp *ibqp, const struct ib_send_wr *wr,
			     struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			     unsigned int *sge_ind,
			     unsigned int valid_num_sge)
{
	struct hns_roce_v2_wqe_data_seg *dseg =
		(void *)rc_sq_wqe + sizeof(struct hns_roce_v2_rc_send_wqe);
	struct hns_roce_qp *qp = to_hr_qp(ibqp);
	int j = 0;
	int i;

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_MSG_START_SGE_IDX,
		     (*sge_ind) & (qp->sge.sge_cnt - 1));

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_INLINE,
		     !!(wr->send_flags & IB_SEND_INLINE));
	if (wr->send_flags & IB_SEND_INLINE)
		return set_rc_inl(qp, wr, rc_sq_wqe, sge_ind);

	if (valid_num_sge <= HNS_ROCE_SGE_IN_WQE) {
		for (i = 0; i < wr->num_sge; i++) {
			if (likely(wr->sg_list[i].length)) {
				set_data_seg_v2(dseg, wr->sg_list + i);
				dseg++;
			}
		}
	} else {
		for (i = 0; i < wr->num_sge && j < HNS_ROCE_SGE_IN_WQE; i++) {
			if (likely(wr->sg_list[i].length)) {
				set_data_seg_v2(dseg, wr->sg_list + i);
				dseg++;
				j++;
			}
		}

		set_extend_sge(qp, wr->sg_list + i, sge_ind,
			       valid_num_sge - HNS_ROCE_SGE_IN_WQE);
	}

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_SGE_NUM, valid_num_sge);

	return 0;
}

static int check_send_valid(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;

	if (unlikely(hr_qp->state == IB_QPS_RESET ||
		     hr_qp->state == IB_QPS_INIT ||
		     hr_qp->state == IB_QPS_RTR)) {
		ibdev_err(ibdev, "failed to post WQE, QP state %u!\n",
			  hr_qp->state);
		return -EINVAL;
	} else if (unlikely(hr_dev->state >= HNS_ROCE_DEVICE_STATE_RST_DOWN)) {
		ibdev_err(ibdev, "failed to post WQE, dev state %d!\n",
			  hr_dev->state);
		return -EIO;
	}

	return 0;
}

static unsigned int calc_wr_sge_num(const struct ib_send_wr *wr,
				    unsigned int *sge_len)
{
	unsigned int valid_num = 0;
	unsigned int len = 0;
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

static __le32 get_immtdata(const struct ib_send_wr *wr)
{
	switch (wr->opcode) {
	case IB_WR_SEND_WITH_IMM:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		return cpu_to_le32(be32_to_cpu(wr->ex.imm_data));
	default:
		return 0;
	}
}

static int set_ud_opcode(struct hns_roce_v2_ud_send_wqe *ud_sq_wqe,
			 const struct ib_send_wr *wr)
{
	u32 ib_op = wr->opcode;

	if (ib_op != IB_WR_SEND && ib_op != IB_WR_SEND_WITH_IMM)
		return -EINVAL;

	ud_sq_wqe->immtdata = get_immtdata(wr);

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_OPCODE, to_hr_opcode(ib_op));

	return 0;
}

static int fill_ud_av(struct hns_roce_v2_ud_send_wqe *ud_sq_wqe,
		      struct hns_roce_ah *ah)
{
	struct ib_device *ib_dev = ah->ibah.device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ib_dev);

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_UDPSPN, ah->av.udp_sport);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_HOPLIMIT, ah->av.hop_limit);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_TCLASS, ah->av.tclass);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_FLOW_LABEL, ah->av.flowlabel);

	if (WARN_ON(ah->av.sl > MAX_SERVICE_LEVEL))
		return -EINVAL;

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_SL, ah->av.sl);

	ud_sq_wqe->sgid_index = ah->av.gid_index;

	memcpy(ud_sq_wqe->dmac, ah->av.mac, ETH_ALEN);
	memcpy(ud_sq_wqe->dgid, ah->av.dgid, GID_LEN_V2);

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
		return 0;

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_VLAN_EN, ah->av.vlan_en);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_VLAN, ah->av.vlan_id);

	return 0;
}

static inline int set_ud_wqe(struct hns_roce_qp *qp,
			     const struct ib_send_wr *wr,
			     void *wqe, unsigned int *sge_idx,
			     unsigned int owner_bit)
{
	struct hns_roce_ah *ah = to_hr_ah(ud_wr(wr)->ah);
	struct hns_roce_v2_ud_send_wqe *ud_sq_wqe = wqe;
	unsigned int curr_idx = *sge_idx;
	unsigned int valid_num_sge;
	u32 msg_len = 0;
	int ret;

	valid_num_sge = calc_wr_sge_num(wr, &msg_len);

	ret = set_ud_opcode(ud_sq_wqe, wr);
	if (WARN_ON(ret))
		return ret;

	ud_sq_wqe->msg_len = cpu_to_le32(msg_len);

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_CQE,
		     !!(wr->send_flags & IB_SEND_SIGNALED));
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_SE,
		     !!(wr->send_flags & IB_SEND_SOLICITED));

	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_PD, to_hr_pd(qp->ibqp.pd)->pdn);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_SGE_NUM, valid_num_sge);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_MSG_START_SGE_IDX,
		     curr_idx & (qp->sge.sge_cnt - 1));

	ud_sq_wqe->qkey = cpu_to_le32(ud_wr(wr)->remote_qkey & 0x80000000 ?
			  qp->qkey : ud_wr(wr)->remote_qkey);
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_DQPN, ud_wr(wr)->remote_qpn);

	ret = fill_ud_av(ud_sq_wqe, ah);
	if (ret)
		return ret;

	qp->sl = to_hr_ah(ud_wr(wr)->ah)->av.sl;

	set_extend_sge(qp, wr->sg_list, &curr_idx, valid_num_sge);

	/*
	 * The pipeline can sequentially post all valid WQEs into WQ buffer,
	 * including new WQEs waiting for the doorbell to update the PI again.
	 * Therefore, the owner bit of WQE MUST be updated after all fields
	 * and extSGEs have been written into DDR instead of cache.
	 */
	if (qp->en_flags & HNS_ROCE_QP_CAP_OWNER_DB)
		dma_wmb();

	*sge_idx = curr_idx;
	hr_reg_write(ud_sq_wqe, UD_SEND_WQE_OWNER, owner_bit);

	return 0;
}

static int set_rc_opcode(struct hns_roce_dev *hr_dev,
			 struct hns_roce_v2_rc_send_wqe *rc_sq_wqe,
			 const struct ib_send_wr *wr)
{
	u32 ib_op = wr->opcode;
	int ret = 0;

	rc_sq_wqe->immtdata = get_immtdata(wr);

	switch (ib_op) {
	case IB_WR_RDMA_READ:
	case IB_WR_RDMA_WRITE:
	case IB_WR_RDMA_WRITE_WITH_IMM:
		rc_sq_wqe->rkey = cpu_to_le32(rdma_wr(wr)->rkey);
		rc_sq_wqe->va = cpu_to_le64(rdma_wr(wr)->remote_addr);
		break;
	case IB_WR_SEND:
	case IB_WR_SEND_WITH_IMM:
		break;
	case IB_WR_ATOMIC_CMP_AND_SWP:
	case IB_WR_ATOMIC_FETCH_AND_ADD:
		rc_sq_wqe->rkey = cpu_to_le32(atomic_wr(wr)->rkey);
		rc_sq_wqe->va = cpu_to_le64(atomic_wr(wr)->remote_addr);
		break;
	case IB_WR_REG_MR:
		if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
			set_frmr_seg(rc_sq_wqe, reg_wr(wr));
		else
			ret = -EOPNOTSUPP;
		break;
	case IB_WR_SEND_WITH_INV:
		rc_sq_wqe->inv_key = cpu_to_le32(wr->ex.invalidate_rkey);
		break;
	default:
		ret = -EINVAL;
	}

	if (unlikely(ret))
		return ret;

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_OPCODE, to_hr_opcode(ib_op));

	return ret;
}

static inline int set_rc_wqe(struct hns_roce_qp *qp,
			     const struct ib_send_wr *wr,
			     void *wqe, unsigned int *sge_idx,
			     unsigned int owner_bit)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(qp->ibqp.device);
	struct hns_roce_v2_rc_send_wqe *rc_sq_wqe = wqe;
	unsigned int curr_idx = *sge_idx;
	unsigned int valid_num_sge;
	u32 msg_len = 0;
	int ret;

	valid_num_sge = calc_wr_sge_num(wr, &msg_len);

	rc_sq_wqe->msg_len = cpu_to_le32(msg_len);

	ret = set_rc_opcode(hr_dev, rc_sq_wqe, wr);
	if (WARN_ON(ret))
		return ret;

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_FENCE,
		     (wr->send_flags & IB_SEND_FENCE) ? 1 : 0);

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_SE,
		     (wr->send_flags & IB_SEND_SOLICITED) ? 1 : 0);

	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_CQE,
		     (wr->send_flags & IB_SEND_SIGNALED) ? 1 : 0);

	if (wr->opcode == IB_WR_ATOMIC_CMP_AND_SWP ||
	    wr->opcode == IB_WR_ATOMIC_FETCH_AND_ADD)
		set_atomic_seg(wr, rc_sq_wqe, valid_num_sge);
	else if (wr->opcode != IB_WR_REG_MR)
		ret = set_rwqe_data_seg(&qp->ibqp, wr, rc_sq_wqe,
					&curr_idx, valid_num_sge);

	/*
	 * The pipeline can sequentially post all valid WQEs into WQ buffer,
	 * including new WQEs waiting for the doorbell to update the PI again.
	 * Therefore, the owner bit of WQE MUST be updated after all fields
	 * and extSGEs have been written into DDR instead of cache.
	 */
	if (qp->en_flags & HNS_ROCE_QP_CAP_OWNER_DB)
		dma_wmb();

	*sge_idx = curr_idx;
	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_OWNER, owner_bit);

	return ret;
}

static inline void update_sq_db(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *qp)
{
	if (unlikely(qp->state == IB_QPS_ERR)) {
		flush_cqe(hr_dev, qp);
	} else {
		struct hns_roce_v2_db sq_db = {};

		hr_reg_write(&sq_db, DB_TAG, qp->qpn);
		hr_reg_write(&sq_db, DB_CMD, HNS_ROCE_V2_SQ_DB);
		hr_reg_write(&sq_db, DB_PI, qp->sq.head);
		hr_reg_write(&sq_db, DB_SL, qp->sl);

		hns_roce_write64(hr_dev, (__le32 *)&sq_db, qp->sq.db_reg);
	}
}

static inline void update_rq_db(struct hns_roce_dev *hr_dev,
				struct hns_roce_qp *qp)
{
	if (unlikely(qp->state == IB_QPS_ERR)) {
		flush_cqe(hr_dev, qp);
	} else {
		if (likely(qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)) {
			*qp->rdb.db_record =
					qp->rq.head & V2_DB_PRODUCER_IDX_M;
		} else {
			struct hns_roce_v2_db rq_db = {};

			hr_reg_write(&rq_db, DB_TAG, qp->qpn);
			hr_reg_write(&rq_db, DB_CMD, HNS_ROCE_V2_RQ_DB);
			hr_reg_write(&rq_db, DB_PI, qp->rq.head);

			hns_roce_write64(hr_dev, (__le32 *)&rq_db,
					 qp->rq.db_reg);
		}
	}
}

static void hns_roce_write512(struct hns_roce_dev *hr_dev, u64 *val,
			      u64 __iomem *dest)
{
#define HNS_ROCE_WRITE_TIMES 8
	struct hns_roce_v2_priv *priv = (struct hns_roce_v2_priv *)hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	int i;

	if (!hr_dev->dis_db && !ops->get_hw_reset_stat(handle))
		for (i = 0; i < HNS_ROCE_WRITE_TIMES; i++)
			writeq_relaxed(*(val + i), dest + i);
}

static void write_dwqe(struct hns_roce_dev *hr_dev, struct hns_roce_qp *qp,
		       void *wqe)
{
#define HNS_ROCE_SL_SHIFT 2
	struct hns_roce_v2_rc_send_wqe *rc_sq_wqe = wqe;

	/* All kinds of DirectWQE have the same header field layout */
	hr_reg_enable(rc_sq_wqe, RC_SEND_WQE_FLAG);
	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_DB_SL_L, qp->sl);
	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_DB_SL_H,
		     qp->sl >> HNS_ROCE_SL_SHIFT);
	hr_reg_write(rc_sq_wqe, RC_SEND_WQE_WQE_INDEX, qp->sq.head);

	hns_roce_write512(hr_dev, wqe, qp->sq.db_reg);
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
	u32 nreq;
	int ret;

	spin_lock_irqsave(&qp->sq.lock, flags);

	ret = check_send_valid(hr_dev, qp);
	if (unlikely(ret)) {
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
			ibdev_err(ibdev, "num_sge = %d > qp->sq.max_gs = %u.\n",
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
		if (ibqp->qp_type == IB_QPT_RC)
			ret = set_rc_wqe(qp, wr, wqe, &sge_idx, owner_bit);
		else
			ret = set_ud_wqe(qp, wr, wqe, &sge_idx, owner_bit);

		if (unlikely(ret)) {
			*bad_wr = wr;
			goto out;
		}
	}

out:
	if (likely(nreq)) {
		qp->sq.head += nreq;
		qp->next_sge = sge_idx;

		if (nreq == 1 && !ret &&
		    (qp->en_flags & HNS_ROCE_QP_CAP_DIRECT_WQE))
			write_dwqe(hr_dev, qp, wqe);
		else
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

	if (hr_qp->state == IB_QPS_RESET)
		return -EINVAL;

	return 0;
}

static void fill_recv_sge_to_wqe(const struct ib_recv_wr *wr, void *wqe,
				 u32 max_sge, bool rsv)
{
	struct hns_roce_v2_wqe_data_seg *dseg = wqe;
	u32 i, cnt;

	for (i = 0, cnt = 0; i < wr->num_sge; i++) {
		/* Skip zero-length sge */
		if (!wr->sg_list[i].length)
			continue;
		set_data_seg_v2(dseg + cnt, wr->sg_list + i);
		cnt++;
	}

	/* Fill a reserved sge to make hw stop reading remaining segments */
	if (rsv) {
		dseg[cnt].lkey = cpu_to_le32(HNS_ROCE_INVALID_LKEY);
		dseg[cnt].addr = 0;
		dseg[cnt].len = cpu_to_le32(HNS_ROCE_INVALID_SGE_LENGTH);
	} else {
		/* Clear remaining segments to make ROCEE ignore sges */
		if (cnt < max_sge)
			memset(dseg + cnt, 0,
			       (max_sge - cnt) * HNS_ROCE_SGE_SIZE);
	}
}

static void fill_rq_wqe(struct hns_roce_qp *hr_qp, const struct ib_recv_wr *wr,
			u32 wqe_idx, u32 max_sge)
{
	void *wqe = NULL;

	wqe = hns_roce_get_recv_wqe(hr_qp, wqe_idx);
	fill_recv_sge_to_wqe(wr, wqe, max_sge, hr_qp->rq.rsv_sge);
}

static int hns_roce_v2_post_recv(struct ib_qp *ibqp,
				 const struct ib_recv_wr *wr,
				 const struct ib_recv_wr **bad_wr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u32 wqe_idx, nreq, max_sge;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&hr_qp->rq.lock, flags);

	ret = check_recv_valid(hr_dev, hr_qp);
	if (unlikely(ret)) {
		*bad_wr = wr;
		nreq = 0;
		goto out;
	}

	max_sge = hr_qp->rq.max_gs - hr_qp->rq.rsv_sge;
	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		if (unlikely(hns_roce_wq_overflow(&hr_qp->rq, nreq,
						  hr_qp->ibqp.recv_cq))) {
			ret = -ENOMEM;
			*bad_wr = wr;
			goto out;
		}

		if (unlikely(wr->num_sge > max_sge)) {
			ibdev_err(ibdev, "num_sge = %d >= max_sge = %u.\n",
				  wr->num_sge, max_sge);
			ret = -EINVAL;
			*bad_wr = wr;
			goto out;
		}

		wqe_idx = (hr_qp->rq.head + nreq) & (hr_qp->rq.wqe_cnt - 1);
		fill_rq_wqe(hr_qp, wr, wqe_idx, max_sge);
		hr_qp->rq.wrid[wqe_idx] = wr->wr_id;
	}

out:
	if (likely(nreq)) {
		hr_qp->rq.head += nreq;

		update_rq_db(hr_dev, hr_qp);
	}
	spin_unlock_irqrestore(&hr_qp->rq.lock, flags);

	return ret;
}

static void *get_srq_wqe_buf(struct hns_roce_srq *srq, u32 n)
{
	return hns_roce_buf_offset(srq->buf_mtr.kmem, n << srq->wqe_shift);
}

static void *get_idx_buf(struct hns_roce_idx_que *idx_que, u32 n)
{
	return hns_roce_buf_offset(idx_que->mtr.kmem,
				   n << idx_que->entry_shift);
}

static void hns_roce_free_srq_wqe(struct hns_roce_srq *srq, u32 wqe_index)
{
	/* always called with interrupts disabled. */
	spin_lock(&srq->lock);

	bitmap_clear(srq->idx_que.bitmap, wqe_index, 1);
	srq->idx_que.tail++;

	spin_unlock(&srq->lock);
}

static int hns_roce_srqwq_overflow(struct hns_roce_srq *srq)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;

	return idx_que->head - idx_que->tail >= srq->wqe_cnt;
}

static int check_post_srq_valid(struct hns_roce_srq *srq, u32 max_sge,
				const struct ib_recv_wr *wr)
{
	struct ib_device *ib_dev = srq->ibsrq.device;

	if (unlikely(wr->num_sge > max_sge)) {
		ibdev_err(ib_dev,
			  "failed to check sge, wr->num_sge = %d, max_sge = %u.\n",
			  wr->num_sge, max_sge);
		return -EINVAL;
	}

	if (unlikely(hns_roce_srqwq_overflow(srq))) {
		ibdev_err(ib_dev,
			  "failed to check srqwq status, srqwq is full.\n");
		return -ENOMEM;
	}

	return 0;
}

static int get_srq_wqe_idx(struct hns_roce_srq *srq, u32 *wqe_idx)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;
	u32 pos;

	pos = find_first_zero_bit(idx_que->bitmap, srq->wqe_cnt);
	if (unlikely(pos == srq->wqe_cnt))
		return -ENOSPC;

	bitmap_set(idx_que->bitmap, pos, 1);
	*wqe_idx = pos;
	return 0;
}

static void fill_wqe_idx(struct hns_roce_srq *srq, unsigned int wqe_idx)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;
	unsigned int head;
	__le32 *buf;

	head = idx_que->head & (srq->wqe_cnt - 1);

	buf = get_idx_buf(idx_que, head);
	*buf = cpu_to_le32(wqe_idx);

	idx_que->head++;
}

static void update_srq_db(struct hns_roce_srq *srq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(srq->ibsrq.device);
	struct hns_roce_v2_db db;

	hr_reg_write(&db, DB_TAG, srq->srqn);
	hr_reg_write(&db, DB_CMD, HNS_ROCE_V2_SRQ_DB);
	hr_reg_write(&db, DB_PI, srq->idx_que.head);

	hns_roce_write64(hr_dev, (__le32 *)&db, srq->db_reg);
}

static int hns_roce_v2_post_srq_recv(struct ib_srq *ibsrq,
				     const struct ib_recv_wr *wr,
				     const struct ib_recv_wr **bad_wr)
{
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);
	unsigned long flags;
	int ret = 0;
	u32 max_sge;
	u32 wqe_idx;
	void *wqe;
	u32 nreq;

	spin_lock_irqsave(&srq->lock, flags);

	max_sge = srq->max_gs - srq->rsv_sge;
	for (nreq = 0; wr; ++nreq, wr = wr->next) {
		ret = check_post_srq_valid(srq, max_sge, wr);
		if (ret) {
			*bad_wr = wr;
			break;
		}

		ret = get_srq_wqe_idx(srq, &wqe_idx);
		if (unlikely(ret)) {
			*bad_wr = wr;
			break;
		}

		wqe = get_srq_wqe_buf(srq, wqe_idx);
		fill_recv_sge_to_wqe(wr, wqe, max_sge, srq->rsv_sge);
		fill_wqe_idx(srq, wqe_idx);
		srq->wrid[wqe_idx] = wr->wr_id;
	}

	if (likely(nreq)) {
		if (srq->cap_flags & HNS_ROCE_SRQ_CAP_RECORD_DB)
			*srq->rdb.db_record = srq->idx_que.head &
					      V2_DB_PRODUCER_IDX_M;
		else
			update_srq_db(srq);
	}

	spin_unlock_irqrestore(&srq->lock, flags);

	return ret;
}

static u32 hns_roce_v2_cmd_hw_reseted(struct hns_roce_dev *hr_dev,
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

static u32 hns_roce_v2_cmd_hw_resetting(struct hns_roce_dev *hr_dev,
					unsigned long instance_stage,
					unsigned long reset_stage)
{
#define HW_RESET_TIMEOUT_US 1000000
#define HW_RESET_SLEEP_US 1000

	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long val;
	int ret;

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

	ret = read_poll_timeout(ops->ae_dev_reset_cnt, val,
				val > hr_dev->reset_cnt, HW_RESET_SLEEP_US,
				HW_RESET_TIMEOUT_US, false, handle);
	if (!ret)
		hr_dev->is_reset = true;

	if (!hr_dev->is_reset || reset_stage == HNS_ROCE_STATE_RST_INIT ||
	    instance_stage == HNS_ROCE_STATE_INIT)
		return CMD_RST_PRC_EBUSY;

	return CMD_RST_PRC_SUCCESS;
}

static u32 hns_roce_v2_cmd_sw_resetting(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
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

static u32 check_aedev_reset_status(struct hns_roce_dev *hr_dev,
				    struct hnae3_handle *handle)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long instance_stage; /* the current instance stage */
	unsigned long reset_stage; /* the current reset stage */
	unsigned long reset_cnt;
	bool sw_resetting;
	bool hw_resetting;

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
	if (reset_cnt != hr_dev->reset_cnt)
		return hns_roce_v2_cmd_hw_reseted(hr_dev, instance_stage,
						  reset_stage);

	hw_resetting = ops->get_cmdq_stat(handle);
	if (hw_resetting)
		return hns_roce_v2_cmd_hw_resetting(hr_dev, instance_stage,
						    reset_stage);

	sw_resetting = ops->ae_dev_resetting(handle);
	if (sw_resetting && instance_stage == HNS_ROCE_STATE_INIT)
		return hns_roce_v2_cmd_sw_resetting(hr_dev);

	return CMD_RST_PRC_OTHERS;
}

static bool check_device_is_in_reset(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	if (hr_dev->reset_cnt != ops->ae_dev_reset_cnt(handle))
		return true;

	if (ops->get_hw_reset_stat(handle))
		return true;

	if (ops->ae_dev_resetting(handle))
		return true;

	return false;
}

static bool v2_chk_mbox_is_avail(struct hns_roce_dev *hr_dev, bool *busy)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	u32 status;

	if (hr_dev->is_reset)
		status = CMD_RST_PRC_SUCCESS;
	else
		status = check_aedev_reset_status(hr_dev, priv->handle);

	*busy = (status == CMD_RST_PRC_EBUSY);

	return status == CMD_RST_PRC_OTHERS;
}

static int hns_roce_alloc_cmq_desc(struct hns_roce_dev *hr_dev,
				   struct hns_roce_v2_cmq_ring *ring)
{
	int size = ring->desc_num * sizeof(struct hns_roce_cmq_desc);

	ring->desc = dma_alloc_coherent(hr_dev->dev, size,
					&ring->desc_dma_addr, GFP_KERNEL);
	if (!ring->desc)
		return -ENOMEM;

	return 0;
}

static void hns_roce_free_cmq_desc(struct hns_roce_dev *hr_dev,
				   struct hns_roce_v2_cmq_ring *ring)
{
	dma_free_coherent(hr_dev->dev,
			  ring->desc_num * sizeof(struct hns_roce_cmq_desc),
			  ring->desc, ring->desc_dma_addr);

	ring->desc_dma_addr = 0;
}

static int init_csq(struct hns_roce_dev *hr_dev,
		    struct hns_roce_v2_cmq_ring *csq)
{
	dma_addr_t dma;
	int ret;

	csq->desc_num = CMD_CSQ_DESC_NUM;
	spin_lock_init(&csq->lock);
	csq->flag = TYPE_CSQ;
	csq->head = 0;

	ret = hns_roce_alloc_cmq_desc(hr_dev, csq);
	if (ret)
		return ret;

	dma = csq->desc_dma_addr;
	roce_write(hr_dev, ROCEE_TX_CMQ_BASEADDR_L_REG, lower_32_bits(dma));
	roce_write(hr_dev, ROCEE_TX_CMQ_BASEADDR_H_REG, upper_32_bits(dma));
	roce_write(hr_dev, ROCEE_TX_CMQ_DEPTH_REG,
		   (u32)csq->desc_num >> HNS_ROCE_CMQ_DESC_NUM_S);

	/* Make sure to write CI first and then PI */
	roce_write(hr_dev, ROCEE_TX_CMQ_CI_REG, 0);
	roce_write(hr_dev, ROCEE_TX_CMQ_PI_REG, 0);

	return 0;
}

static int hns_roce_v2_cmq_init(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	int ret;

	priv->cmq.tx_timeout = HNS_ROCE_CMQ_TX_TIMEOUT;

	ret = init_csq(hr_dev, &priv->cmq.csq);
	if (ret)
		dev_err(hr_dev->dev, "failed to init CSQ, ret = %d.\n", ret);

	return ret;
}

static void hns_roce_v2_cmq_exit(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;

	hns_roce_free_cmq_desc(hr_dev, &priv->cmq.csq);
}

static void hns_roce_cmq_setup_basic_desc(struct hns_roce_cmq_desc *desc,
					  enum hns_roce_opcode_type opcode,
					  bool is_read)
{
	memset((void *)desc, 0, sizeof(struct hns_roce_cmq_desc));
	desc->opcode = cpu_to_le16(opcode);
	desc->flag = cpu_to_le16(HNS_ROCE_CMD_FLAG_IN);
	if (is_read)
		desc->flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_WR);
	else
		desc->flag &= cpu_to_le16(~HNS_ROCE_CMD_FLAG_WR);
}

static int hns_roce_cmq_csq_done(struct hns_roce_dev *hr_dev)
{
	u32 tail = roce_read(hr_dev, ROCEE_TX_CMQ_CI_REG);
	struct hns_roce_v2_priv *priv = hr_dev->priv;

	return tail == priv->cmq.csq.head;
}

static void update_cmdq_status(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;

	if (handle->rinfo.reset_state == HNS_ROCE_STATE_RST_INIT ||
	    handle->rinfo.instance_state == HNS_ROCE_STATE_INIT)
		hr_dev->cmd.state = HNS_ROCE_CMDQ_STATE_FATAL_ERR;
}

static int hns_roce_cmd_err_convert_errno(u16 desc_ret)
{
	struct hns_roce_cmd_errcode errcode_table[] = {
		{CMD_EXEC_SUCCESS, 0},
		{CMD_NO_AUTH, -EPERM},
		{CMD_NOT_EXIST, -EOPNOTSUPP},
		{CMD_CRQ_FULL, -EXFULL},
		{CMD_NEXT_ERR, -ENOSR},
		{CMD_NOT_EXEC, -ENOTBLK},
		{CMD_PARA_ERR, -EINVAL},
		{CMD_RESULT_ERR, -ERANGE},
		{CMD_TIMEOUT, -ETIME},
		{CMD_HILINK_ERR, -ENOLINK},
		{CMD_INFO_ILLEGAL, -ENXIO},
		{CMD_INVALID, -EBADR},
	};
	u16 i;

	for (i = 0; i < ARRAY_SIZE(errcode_table); i++)
		if (desc_ret == errcode_table[i].return_status)
			return errcode_table[i].errno;
	return -EIO;
}

static int __hns_roce_cmq_send(struct hns_roce_dev *hr_dev,
			       struct hns_roce_cmq_desc *desc, int num)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_cmq_ring *csq = &priv->cmq.csq;
	u32 timeout = 0;
	u16 desc_ret;
	u32 tail;
	int ret;
	int i;

	spin_lock_bh(&csq->lock);

	tail = csq->head;

	for (i = 0; i < num; i++) {
		csq->desc[csq->head++] = desc[i];
		if (csq->head == csq->desc_num)
			csq->head = 0;
	}

	/* Write to hardware */
	roce_write(hr_dev, ROCEE_TX_CMQ_PI_REG, csq->head);

	atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_CMDS_CNT]);

	do {
		if (hns_roce_cmq_csq_done(hr_dev))
			break;
		udelay(1);
	} while (++timeout < priv->cmq.tx_timeout);

	if (hns_roce_cmq_csq_done(hr_dev)) {
		ret = 0;
		for (i = 0; i < num; i++) {
			/* check the result of hardware write back */
			desc[i] = csq->desc[tail++];
			if (tail == csq->desc_num)
				tail = 0;

			desc_ret = le16_to_cpu(desc[i].retval);
			if (likely(desc_ret == CMD_EXEC_SUCCESS))
				continue;

			dev_err_ratelimited(hr_dev->dev,
					    "Cmdq IO error, opcode = 0x%x, return = 0x%x.\n",
					    desc->opcode, desc_ret);
			ret = hns_roce_cmd_err_convert_errno(desc_ret);
		}
	} else {
		/* FW/HW reset or incorrect number of desc */
		tail = roce_read(hr_dev, ROCEE_TX_CMQ_CI_REG);
		dev_warn(hr_dev->dev, "CMDQ move tail from %u to %u.\n",
			 csq->head, tail);
		csq->head = tail;

		update_cmdq_status(hr_dev);

		ret = -EAGAIN;
	}

	spin_unlock_bh(&csq->lock);

	if (ret)
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_CMDS_ERR_CNT]);

	return ret;
}

static int hns_roce_cmq_send(struct hns_roce_dev *hr_dev,
			     struct hns_roce_cmq_desc *desc, int num)
{
	bool busy;
	int ret;

	if (hr_dev->cmd.state == HNS_ROCE_CMDQ_STATE_FATAL_ERR)
		return -EIO;

	if (!v2_chk_mbox_is_avail(hr_dev, &busy))
		return busy ? -EBUSY : 0;

	ret = __hns_roce_cmq_send(hr_dev, desc, num);
	if (ret) {
		if (!v2_chk_mbox_is_avail(hr_dev, &busy))
			return busy ? -EBUSY : 0;
	}

	return ret;
}

static int config_hem_ba_to_hw(struct hns_roce_dev *hr_dev,
			       dma_addr_t base_addr, u8 cmd, unsigned long tag)
{
	struct hns_roce_cmd_mailbox *mbox;
	int ret;

	mbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mbox))
		return PTR_ERR(mbox);

	ret = hns_roce_cmd_mbox(hr_dev, base_addr, mbox->dma, cmd, tag);
	hns_roce_free_cmd_mailbox(hr_dev, mbox);
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

static void func_clr_hw_resetting_state(struct hns_roce_dev *hr_dev,
					struct hnae3_handle *handle)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long end;

	hr_dev->dis_db = true;

	dev_warn(hr_dev->dev,
		 "func clear is pending, device in resetting state.\n");
	end = HNS_ROCE_V2_HW_RST_TIMEOUT;
	while (end) {
		if (!ops->get_hw_reset_stat(handle)) {
			hr_dev->is_reset = true;
			dev_info(hr_dev->dev,
				 "func clear success after reset.\n");
			return;
		}
		msleep(HNS_ROCE_V2_HW_RST_COMPLETION_WAIT);
		end -= HNS_ROCE_V2_HW_RST_COMPLETION_WAIT;
	}

	dev_warn(hr_dev->dev, "func clear failed.\n");
}

static void func_clr_sw_resetting_state(struct hns_roce_dev *hr_dev,
					struct hnae3_handle *handle)
{
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;
	unsigned long end;

	hr_dev->dis_db = true;

	dev_warn(hr_dev->dev,
		 "func clear is pending, device in resetting state.\n");
	end = HNS_ROCE_V2_HW_RST_TIMEOUT;
	while (end) {
		if (ops->ae_dev_reset_cnt(handle) !=
		    hr_dev->reset_cnt) {
			hr_dev->is_reset = true;
			dev_info(hr_dev->dev,
				 "func clear success after sw reset\n");
			return;
		}
		msleep(HNS_ROCE_V2_HW_RST_COMPLETION_WAIT);
		end -= HNS_ROCE_V2_HW_RST_COMPLETION_WAIT;
	}

	dev_warn(hr_dev->dev, "func clear failed because of unfinished sw reset\n");
}

static void hns_roce_func_clr_rst_proc(struct hns_roce_dev *hr_dev, int retval,
				       int flag)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hnae3_handle *handle = priv->handle;
	const struct hnae3_ae_ops *ops = handle->ae_algo->ops;

	if (ops->ae_dev_reset_cnt(handle) != hr_dev->reset_cnt) {
		hr_dev->dis_db = true;
		hr_dev->is_reset = true;
		dev_info(hr_dev->dev, "func clear success after reset.\n");
		return;
	}

	if (ops->get_hw_reset_stat(handle)) {
		func_clr_hw_resetting_state(hr_dev, handle);
		return;
	}

	if (ops->ae_dev_resetting(handle) &&
	    handle->rinfo.instance_state == HNS_ROCE_STATE_INIT) {
		func_clr_sw_resetting_state(hr_dev, handle);
		return;
	}

	if (retval && !flag)
		dev_warn(hr_dev->dev,
			 "func clear read failed, ret = %d.\n", retval);

	dev_warn(hr_dev->dev, "func clear failed.\n");
}

static void __hns_roce_function_clear(struct hns_roce_dev *hr_dev, int vf_id)
{
	bool fclr_write_fail_flag = false;
	struct hns_roce_func_clear *resp;
	struct hns_roce_cmq_desc desc;
	unsigned long end;
	int ret = 0;

	if (check_device_is_in_reset(hr_dev))
		goto out;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_FUNC_CLEAR, false);
	resp = (struct hns_roce_func_clear *)desc.data;
	resp->rst_funcid_en = cpu_to_le32(vf_id);

	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		fclr_write_fail_flag = true;
		dev_err(hr_dev->dev, "func clear write failed, ret = %d.\n",
			 ret);
		goto out;
	}

	msleep(HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_INTERVAL);
	end = HNS_ROCE_V2_FUNC_CLEAR_TIMEOUT_MSECS;
	while (end) {
		if (check_device_is_in_reset(hr_dev))
			goto out;
		msleep(HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_FAIL_WAIT);
		end -= HNS_ROCE_V2_READ_FUNC_CLEAR_FLAG_FAIL_WAIT;

		hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_FUNC_CLEAR,
					      true);

		resp->rst_funcid_en = cpu_to_le32(vf_id);
		ret = hns_roce_cmq_send(hr_dev, &desc, 1);
		if (ret)
			continue;

		if (hr_reg_read(resp, FUNC_CLEAR_RST_FUN_DONE)) {
			if (vf_id == 0)
				hr_dev->is_reset = true;
			return;
		}
	}

out:
	hns_roce_func_clr_rst_proc(hr_dev, ret, fclr_write_fail_flag);
}

static int hns_roce_free_vf_resource(struct hns_roce_dev *hr_dev, int vf_id)
{
	enum hns_roce_opcode_type opcode = HNS_ROCE_OPC_ALLOC_VF_RES;
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cmq_req *req_a;

	req_a = (struct hns_roce_cmq_req *)desc[0].data;
	hns_roce_cmq_setup_basic_desc(&desc[0], opcode, false);
	desc[0].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	hns_roce_cmq_setup_basic_desc(&desc[1], opcode, false);
	hr_reg_write(req_a, FUNC_RES_A_VF_ID, vf_id);

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static void hns_roce_function_clear(struct hns_roce_dev *hr_dev)
{
	int ret;
	int i;

	if (hr_dev->cmd.state == HNS_ROCE_CMDQ_STATE_FATAL_ERR)
		return;

	for (i = hr_dev->func_num - 1; i >= 0; i--) {
		__hns_roce_function_clear(hr_dev, i);

		if (i == 0)
			continue;

		ret = hns_roce_free_vf_resource(hr_dev, i);
		if (ret)
			ibdev_err(&hr_dev->ib_dev,
				  "failed to free vf resource, vf_id = %d, ret = %d.\n",
				  i, ret);
	}
}

static int hns_roce_clear_extdb_list_info(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CLEAR_EXTDB_LIST_INFO,
				      false);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to clear extended doorbell info, ret = %d.\n",
			  ret);

	return ret;
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

static int hns_roce_query_func_info(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	int ret;

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		hr_dev->func_num = 1;
		return 0;
	}

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_FUNC_INFO,
				      true);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		hr_dev->func_num = 1;
		return ret;
	}

	hr_dev->func_num = le32_to_cpu(desc.func_info.own_func_num);
	hr_dev->cong_algo_tmpl_id = le32_to_cpu(desc.func_info.own_mac_id);

	return 0;
}

static int hns_roce_hw_v2_query_counter(struct hns_roce_dev *hr_dev,
					u64 *stats, u32 port, int *num_counters)
{
#define CNT_PER_DESC 3
	struct hns_roce_cmq_desc *desc;
	int bd_idx, cnt_idx;
	__le64 *cnt_data;
	int desc_num;
	int ret;
	int i;

	if (port > hr_dev->caps.num_ports)
		return -EINVAL;

	desc_num = DIV_ROUND_UP(HNS_ROCE_HW_CNT_TOTAL, CNT_PER_DESC);
	desc = kcalloc(desc_num, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	for (i = 0; i < desc_num; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i],
					      HNS_ROCE_OPC_QUERY_COUNTER, true);
		if (i != desc_num - 1)
			desc[i].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	}

	ret = hns_roce_cmq_send(hr_dev, desc, desc_num);
	if (ret) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to get counter, ret = %d.\n", ret);
		goto err_out;
	}

	for (i = 0; i < HNS_ROCE_HW_CNT_TOTAL && i < *num_counters; i++) {
		bd_idx = i / CNT_PER_DESC;
		if (!(desc[bd_idx].flag & HNS_ROCE_CMD_FLAG_NEXT) &&
		    bd_idx != HNS_ROCE_HW_CNT_TOTAL / CNT_PER_DESC)
			break;

		cnt_data = (__le64 *)&desc[bd_idx].data[0];
		cnt_idx = i % CNT_PER_DESC;
		stats[i] = le64_to_cpu(cnt_data[cnt_idx]);
	}
	*num_counters = i;

err_out:
	kfree(desc);
	return ret;
}

static int hns_roce_config_global_param(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	u32 clock_cycles_of_1us;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_GLOBAL_PARAM,
				      false);

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08)
		clock_cycles_of_1us = HNS_ROCE_1NS_CFG;
	else
		clock_cycles_of_1us = HNS_ROCE_1US_CFG;

	hr_reg_write(req, CFG_GLOBAL_PARAM_1US_CYCLES, clock_cycles_of_1us);
	hr_reg_write(req, CFG_GLOBAL_PARAM_UDP_PORT, ROCE_V2_UDP_DPORT);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int load_func_res_caps(struct hns_roce_dev *hr_dev, bool is_vf)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cmq_req *r_a = (struct hns_roce_cmq_req *)desc[0].data;
	struct hns_roce_cmq_req *r_b = (struct hns_roce_cmq_req *)desc[1].data;
	struct hns_roce_caps *caps = &hr_dev->caps;
	enum hns_roce_opcode_type opcode;
	u32 func_num;
	int ret;

	if (is_vf) {
		opcode = HNS_ROCE_OPC_QUERY_VF_RES;
		func_num = 1;
	} else {
		opcode = HNS_ROCE_OPC_QUERY_PF_RES;
		func_num = hr_dev->func_num;
	}

	hns_roce_cmq_setup_basic_desc(&desc[0], opcode, true);
	desc[0].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	hns_roce_cmq_setup_basic_desc(&desc[1], opcode, true);

	ret = hns_roce_cmq_send(hr_dev, desc, 2);
	if (ret)
		return ret;

	caps->qpc_bt_num = hr_reg_read(r_a, FUNC_RES_A_QPC_BT_NUM) / func_num;
	caps->srqc_bt_num = hr_reg_read(r_a, FUNC_RES_A_SRQC_BT_NUM) / func_num;
	caps->cqc_bt_num = hr_reg_read(r_a, FUNC_RES_A_CQC_BT_NUM) / func_num;
	caps->mpt_bt_num = hr_reg_read(r_a, FUNC_RES_A_MPT_BT_NUM) / func_num;
	caps->eqc_bt_num = hr_reg_read(r_a, FUNC_RES_A_EQC_BT_NUM) / func_num;
	caps->smac_bt_num = hr_reg_read(r_b, FUNC_RES_B_SMAC_NUM) / func_num;
	caps->sgid_bt_num = hr_reg_read(r_b, FUNC_RES_B_SGID_NUM) / func_num;
	caps->sccc_bt_num = hr_reg_read(r_b, FUNC_RES_B_SCCC_BT_NUM) / func_num;

	if (is_vf) {
		caps->sl_num = hr_reg_read(r_b, FUNC_RES_V_QID_NUM) / func_num;
		caps->gmv_bt_num = hr_reg_read(r_b, FUNC_RES_V_GMV_BT_NUM) /
					       func_num;
	} else {
		caps->sl_num = hr_reg_read(r_b, FUNC_RES_B_QID_NUM) / func_num;
		caps->gmv_bt_num = hr_reg_read(r_b, FUNC_RES_B_GMV_BT_NUM) /
					       func_num;
	}

	return 0;
}

static int load_pf_timer_res_caps(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	struct hns_roce_caps *caps = &hr_dev->caps;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_PF_TIMER_RES,
				      true);

	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	caps->qpc_timer_bt_num = hr_reg_read(req, PF_TIMER_RES_QPC_ITEM_NUM);
	caps->cqc_timer_bt_num = hr_reg_read(req, PF_TIMER_RES_CQC_ITEM_NUM);

	return 0;
}

static int hns_roce_query_pf_resource(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	ret = load_func_res_caps(hr_dev, false);
	if (ret) {
		dev_err(dev, "failed to load pf res caps, ret = %d.\n", ret);
		return ret;
	}

	ret = load_pf_timer_res_caps(hr_dev);
	if (ret)
		dev_err(dev, "failed to load pf timer resource, ret = %d.\n",
			ret);

	return ret;
}

static int hns_roce_query_vf_resource(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	ret = load_func_res_caps(hr_dev, true);
	if (ret)
		dev_err(dev, "failed to load vf res caps, ret = %d.\n", ret);

	return ret;
}

static int __hns_roce_set_vf_switch_param(struct hns_roce_dev *hr_dev,
					  u32 vf_id)
{
	struct hns_roce_vf_switch *swt;
	struct hns_roce_cmq_desc desc;
	int ret;

	swt = (struct hns_roce_vf_switch *)desc.data;
	hns_roce_cmq_setup_basic_desc(&desc, HNS_SWITCH_PARAMETER_CFG, true);
	swt->rocee_sel |= cpu_to_le32(HNS_ICL_SWITCH_CMD_ROCEE_SEL);
	hr_reg_write(swt, VF_SWITCH_VF_ID, vf_id);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	desc.flag = cpu_to_le16(HNS_ROCE_CMD_FLAG_IN);
	desc.flag &= cpu_to_le16(~HNS_ROCE_CMD_FLAG_WR);
	hr_reg_enable(swt, VF_SWITCH_ALW_LPBK);
	hr_reg_clear(swt, VF_SWITCH_ALW_LCL_LPBK);
	hr_reg_enable(swt, VF_SWITCH_ALW_DST_OVRD);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int hns_roce_set_vf_switch_param(struct hns_roce_dev *hr_dev)
{
	u32 vf_id;
	int ret;

	for (vf_id = 0; vf_id < hr_dev->func_num; vf_id++) {
		ret = __hns_roce_set_vf_switch_param(hr_dev, vf_id);
		if (ret)
			return ret;
	}
	return 0;
}

static int config_vf_hem_resource(struct hns_roce_dev *hr_dev, int vf_id)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cmq_req *r_a = (struct hns_roce_cmq_req *)desc[0].data;
	struct hns_roce_cmq_req *r_b = (struct hns_roce_cmq_req *)desc[1].data;
	enum hns_roce_opcode_type opcode = HNS_ROCE_OPC_ALLOC_VF_RES;
	struct hns_roce_caps *caps = &hr_dev->caps;

	hns_roce_cmq_setup_basic_desc(&desc[0], opcode, false);
	desc[0].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	hns_roce_cmq_setup_basic_desc(&desc[1], opcode, false);

	hr_reg_write(r_a, FUNC_RES_A_VF_ID, vf_id);

	hr_reg_write(r_a, FUNC_RES_A_QPC_BT_NUM, caps->qpc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_QPC_BT_IDX, vf_id * caps->qpc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_SRQC_BT_NUM, caps->srqc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_SRQC_BT_IDX, vf_id * caps->srqc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_CQC_BT_NUM, caps->cqc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_CQC_BT_IDX, vf_id * caps->cqc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_MPT_BT_NUM, caps->mpt_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_MPT_BT_IDX, vf_id * caps->mpt_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_EQC_BT_NUM, caps->eqc_bt_num);
	hr_reg_write(r_a, FUNC_RES_A_EQC_BT_IDX, vf_id * caps->eqc_bt_num);
	hr_reg_write(r_b, FUNC_RES_V_QID_NUM, caps->sl_num);
	hr_reg_write(r_b, FUNC_RES_B_QID_IDX, vf_id * caps->sl_num);
	hr_reg_write(r_b, FUNC_RES_B_SCCC_BT_NUM, caps->sccc_bt_num);
	hr_reg_write(r_b, FUNC_RES_B_SCCC_BT_IDX, vf_id * caps->sccc_bt_num);

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09) {
		hr_reg_write(r_b, FUNC_RES_V_GMV_BT_NUM, caps->gmv_bt_num);
		hr_reg_write(r_b, FUNC_RES_B_GMV_BT_IDX,
			     vf_id * caps->gmv_bt_num);
	} else {
		hr_reg_write(r_b, FUNC_RES_B_SGID_NUM, caps->sgid_bt_num);
		hr_reg_write(r_b, FUNC_RES_B_SGID_IDX,
			     vf_id * caps->sgid_bt_num);
		hr_reg_write(r_b, FUNC_RES_B_SMAC_NUM, caps->smac_bt_num);
		hr_reg_write(r_b, FUNC_RES_B_SMAC_IDX,
			     vf_id * caps->smac_bt_num);
	}

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static int hns_roce_alloc_vf_resource(struct hns_roce_dev *hr_dev)
{
	u32 func_num = max_t(u32, 1, hr_dev->func_num);
	u32 vf_id;
	int ret;

	for (vf_id = 0; vf_id < func_num; vf_id++) {
		ret = config_vf_hem_resource(hr_dev, vf_id);
		if (ret) {
			dev_err(hr_dev->dev,
				"failed to config vf-%u hem res, ret = %d.\n",
				vf_id, ret);
			return ret;
		}
	}

	return 0;
}

static int hns_roce_v2_set_bt(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	struct hns_roce_caps *caps = &hr_dev->caps;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_BT_ATTR, false);

	hr_reg_write(req, CFG_BT_ATTR_QPC_BA_PGSZ,
		     caps->qpc_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_QPC_BUF_PGSZ,
		     caps->qpc_buf_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_QPC_HOPNUM,
		     to_hr_hem_hopnum(caps->qpc_hop_num, caps->num_qps));

	hr_reg_write(req, CFG_BT_ATTR_SRQC_BA_PGSZ,
		     caps->srqc_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_SRQC_BUF_PGSZ,
		     caps->srqc_buf_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_SRQC_HOPNUM,
		     to_hr_hem_hopnum(caps->srqc_hop_num, caps->num_srqs));

	hr_reg_write(req, CFG_BT_ATTR_CQC_BA_PGSZ,
		     caps->cqc_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_CQC_BUF_PGSZ,
		     caps->cqc_buf_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_CQC_HOPNUM,
		     to_hr_hem_hopnum(caps->cqc_hop_num, caps->num_cqs));

	hr_reg_write(req, CFG_BT_ATTR_MPT_BA_PGSZ,
		     caps->mpt_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_MPT_BUF_PGSZ,
		     caps->mpt_buf_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_MPT_HOPNUM,
		     to_hr_hem_hopnum(caps->mpt_hop_num, caps->num_mtpts));

	hr_reg_write(req, CFG_BT_ATTR_SCCC_BA_PGSZ,
		     caps->sccc_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_SCCC_BUF_PGSZ,
		     caps->sccc_buf_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(req, CFG_BT_ATTR_SCCC_HOPNUM,
		     to_hr_hem_hopnum(caps->sccc_hop_num, caps->num_qps));

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static void calc_pg_sz(u32 obj_num, u32 obj_size, u32 hop_num, u32 ctx_bt_num,
		       u32 *buf_page_size, u32 *bt_page_size, u32 hem_type)
{
	u64 obj_per_chunk;
	u64 bt_chunk_size = PAGE_SIZE;
	u64 buf_chunk_size = PAGE_SIZE;
	u64 obj_per_chunk_default = buf_chunk_size / obj_size;

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
		pr_err("table %u not support hop_num = %u!\n", hem_type,
		       hop_num);
		return;
	}

	if (hem_type >= HEM_TYPE_MTT)
		*bt_page_size = ilog2(DIV_ROUND_UP(obj_num, obj_per_chunk));
	else
		*buf_page_size = ilog2(DIV_ROUND_UP(obj_num, obj_per_chunk));
}

static void set_hem_page_size(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;

	/* EQ */
	caps->eqe_ba_pg_sz = 0;
	caps->eqe_buf_pg_sz = 0;

	/* Link Table */
	caps->llm_buf_pg_sz = 0;

	/* MR */
	caps->mpt_ba_pg_sz = 0;
	caps->mpt_buf_pg_sz = 0;
	caps->pbl_ba_pg_sz = HNS_ROCE_BA_PG_SZ_SUPPORTED_16K;
	caps->pbl_buf_pg_sz = 0;
	calc_pg_sz(caps->num_mtpts, caps->mtpt_entry_sz, caps->mpt_hop_num,
		   caps->mpt_bt_num, &caps->mpt_buf_pg_sz, &caps->mpt_ba_pg_sz,
		   HEM_TYPE_MTPT);

	/* QP */
	caps->qpc_ba_pg_sz = 0;
	caps->qpc_buf_pg_sz = 0;
	caps->qpc_timer_ba_pg_sz = 0;
	caps->qpc_timer_buf_pg_sz = 0;
	caps->sccc_ba_pg_sz = 0;
	caps->sccc_buf_pg_sz = 0;
	caps->mtt_ba_pg_sz = 0;
	caps->mtt_buf_pg_sz = 0;
	calc_pg_sz(caps->num_qps, caps->qpc_sz, caps->qpc_hop_num,
		   caps->qpc_bt_num, &caps->qpc_buf_pg_sz, &caps->qpc_ba_pg_sz,
		   HEM_TYPE_QPC);

	if (caps->flags & HNS_ROCE_CAP_FLAG_QP_FLOW_CTRL)
		calc_pg_sz(caps->num_qps, caps->sccc_sz, caps->sccc_hop_num,
			   caps->sccc_bt_num, &caps->sccc_buf_pg_sz,
			   &caps->sccc_ba_pg_sz, HEM_TYPE_SCCC);

	/* CQ */
	caps->cqc_ba_pg_sz = 0;
	caps->cqc_buf_pg_sz = 0;
	caps->cqc_timer_ba_pg_sz = 0;
	caps->cqc_timer_buf_pg_sz = 0;
	caps->cqe_ba_pg_sz = HNS_ROCE_BA_PG_SZ_SUPPORTED_256K;
	caps->cqe_buf_pg_sz = 0;
	calc_pg_sz(caps->num_cqs, caps->cqc_entry_sz, caps->cqc_hop_num,
		   caps->cqc_bt_num, &caps->cqc_buf_pg_sz, &caps->cqc_ba_pg_sz,
		   HEM_TYPE_CQC);
	calc_pg_sz(caps->max_cqes, caps->cqe_sz, caps->cqe_hop_num,
		   1, &caps->cqe_buf_pg_sz, &caps->cqe_ba_pg_sz, HEM_TYPE_CQE);

	/* SRQ */
	if (caps->flags & HNS_ROCE_CAP_FLAG_SRQ) {
		caps->srqc_ba_pg_sz = 0;
		caps->srqc_buf_pg_sz = 0;
		caps->srqwqe_ba_pg_sz = 0;
		caps->srqwqe_buf_pg_sz = 0;
		caps->idx_ba_pg_sz = 0;
		caps->idx_buf_pg_sz = 0;
		calc_pg_sz(caps->num_srqs, caps->srqc_entry_sz,
			   caps->srqc_hop_num, caps->srqc_bt_num,
			   &caps->srqc_buf_pg_sz, &caps->srqc_ba_pg_sz,
			   HEM_TYPE_SRQC);
		calc_pg_sz(caps->num_srqwqe_segs, caps->mtt_entry_sz,
			   caps->srqwqe_hop_num, 1, &caps->srqwqe_buf_pg_sz,
			   &caps->srqwqe_ba_pg_sz, HEM_TYPE_SRQWQE);
		calc_pg_sz(caps->num_idx_segs, caps->idx_entry_sz,
			   caps->idx_hop_num, 1, &caps->idx_buf_pg_sz,
			   &caps->idx_ba_pg_sz, HEM_TYPE_IDX);
	}

	/* GMV */
	caps->gmv_ba_pg_sz = 0;
	caps->gmv_buf_pg_sz = 0;
}

/* Apply all loaded caps before setting to hardware */
static void apply_func_caps(struct hns_roce_dev *hr_dev)
{
#define MAX_GID_TBL_LEN 256
	struct hns_roce_caps *caps = &hr_dev->caps;
	struct hns_roce_v2_priv *priv = hr_dev->priv;

	/* The following configurations don't need to be got from firmware. */
	caps->qpc_timer_entry_sz = HNS_ROCE_V2_QPC_TIMER_ENTRY_SZ;
	caps->cqc_timer_entry_sz = HNS_ROCE_V2_CQC_TIMER_ENTRY_SZ;
	caps->mtt_entry_sz = HNS_ROCE_V2_MTT_ENTRY_SZ;

	caps->pbl_hop_num = HNS_ROCE_PBL_HOP_NUM;
	caps->qpc_timer_hop_num = HNS_ROCE_HOP_NUM_0;
	caps->cqc_timer_hop_num = HNS_ROCE_HOP_NUM_0;

	caps->num_srqwqe_segs = HNS_ROCE_V2_MAX_SRQWQE_SEGS;
	caps->num_idx_segs = HNS_ROCE_V2_MAX_IDX_SEGS;

	if (!caps->num_comp_vectors)
		caps->num_comp_vectors =
			min_t(u32, caps->eqc_bt_num - HNS_ROCE_V2_AEQE_VEC_NUM,
				(u32)priv->handle->rinfo.num_vectors -
		(HNS_ROCE_V2_AEQE_VEC_NUM + HNS_ROCE_V2_ABNORMAL_VEC_NUM));

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09) {
		caps->eqe_hop_num = HNS_ROCE_V3_EQE_HOP_NUM;
		caps->ceqe_size = HNS_ROCE_V3_EQE_SIZE;
		caps->aeqe_size = HNS_ROCE_V3_EQE_SIZE;

		/* The following configurations will be overwritten */
		caps->qpc_sz = HNS_ROCE_V3_QPC_SZ;
		caps->cqe_sz = HNS_ROCE_V3_CQE_SIZE;
		caps->sccc_sz = HNS_ROCE_V3_SCCC_SZ;

		/* The following configurations are not got from firmware */
		caps->gmv_entry_sz = HNS_ROCE_V3_GMV_ENTRY_SZ;

		caps->gmv_hop_num = HNS_ROCE_HOP_NUM_0;

		/* It's meaningless to support excessively large gid_table_len,
		 * as the type of sgid_index in kernel struct ib_global_route
		 * and userspace struct ibv_global_route are u8/uint8_t (0-255).
		 */
		caps->gid_table_len[0] = min_t(u32, MAX_GID_TBL_LEN,
					 caps->gmv_bt_num *
					 (HNS_HW_PAGE_SIZE / caps->gmv_entry_sz));

		caps->gmv_entry_num = caps->gmv_bt_num * (PAGE_SIZE /
							  caps->gmv_entry_sz);
	} else {
		u32 func_num = max_t(u32, 1, hr_dev->func_num);

		caps->eqe_hop_num = HNS_ROCE_V2_EQE_HOP_NUM;
		caps->ceqe_size = HNS_ROCE_CEQE_SIZE;
		caps->aeqe_size = HNS_ROCE_AEQE_SIZE;
		caps->gid_table_len[0] /= func_num;
	}

	if (hr_dev->is_vf) {
		caps->default_aeq_arm_st = 0x3;
		caps->default_ceq_arm_st = 0x3;
		caps->default_ceq_max_cnt = 0x1;
		caps->default_ceq_period = 0x10;
		caps->default_aeq_max_cnt = 0x1;
		caps->default_aeq_period = 0x10;
	}

	set_hem_page_size(hr_dev);
}

static int hns_roce_query_caps(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_cmq_desc desc[HNS_ROCE_QUERY_PF_CAPS_CMD_NUM];
	struct hns_roce_caps *caps = &hr_dev->caps;
	struct hns_roce_query_pf_caps_a *resp_a;
	struct hns_roce_query_pf_caps_b *resp_b;
	struct hns_roce_query_pf_caps_c *resp_c;
	struct hns_roce_query_pf_caps_d *resp_d;
	struct hns_roce_query_pf_caps_e *resp_e;
	enum hns_roce_opcode_type cmd;
	int ctx_hop_num;
	int pbl_hop_num;
	int ret;
	int i;

	cmd = hr_dev->is_vf ? HNS_ROCE_OPC_QUERY_VF_CAPS_NUM :
	      HNS_ROCE_OPC_QUERY_PF_CAPS_NUM;

	for (i = 0; i < HNS_ROCE_QUERY_PF_CAPS_CMD_NUM; i++) {
		hns_roce_cmq_setup_basic_desc(&desc[i], cmd, true);
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

	caps->local_ca_ack_delay = resp_a->local_ca_ack_delay;
	caps->max_sq_sg = le16_to_cpu(resp_a->max_sq_sg);
	caps->max_sq_inline = le16_to_cpu(resp_a->max_sq_inline);
	caps->max_rq_sg = le16_to_cpu(resp_a->max_rq_sg);
	caps->max_rq_sg = roundup_pow_of_two(caps->max_rq_sg);
	caps->max_srq_sges = le16_to_cpu(resp_a->max_srq_sges);
	caps->max_srq_sges = roundup_pow_of_two(caps->max_srq_sges);
	caps->num_aeq_vectors = resp_a->num_aeq_vectors;
	caps->num_other_vectors = resp_a->num_other_vectors;
	caps->max_sq_desc_sz = resp_a->max_sq_desc_sz;
	caps->max_rq_desc_sz = resp_a->max_rq_desc_sz;

	caps->mtpt_entry_sz = resp_b->mtpt_entry_sz;
	caps->irrl_entry_sz = resp_b->irrl_entry_sz;
	caps->trrl_entry_sz = resp_b->trrl_entry_sz;
	caps->cqc_entry_sz = resp_b->cqc_entry_sz;
	caps->srqc_entry_sz = resp_b->srqc_entry_sz;
	caps->idx_entry_sz = resp_b->idx_entry_sz;
	caps->sccc_sz = resp_b->sccc_sz;
	caps->max_mtu = resp_b->max_mtu;
	caps->min_cqes = resp_b->min_cqes;
	caps->min_wqes = resp_b->min_wqes;
	caps->page_size_cap = le32_to_cpu(resp_b->page_size_cap);
	caps->pkey_table_len[0] = resp_b->pkey_table_len;
	caps->phy_num_uars = resp_b->phy_num_uars;
	ctx_hop_num = resp_b->ctx_hop_num;
	pbl_hop_num = resp_b->pbl_hop_num;

	caps->num_pds = 1 << hr_reg_read(resp_c, PF_CAPS_C_NUM_PDS);

	caps->flags = hr_reg_read(resp_c, PF_CAPS_C_CAP_FLAGS);
	caps->flags |= le16_to_cpu(resp_d->cap_flags_ex) <<
		       HNS_ROCE_CAP_FLAGS_EX_SHIFT;

	caps->num_cqs = 1 << hr_reg_read(resp_c, PF_CAPS_C_NUM_CQS);
	caps->gid_table_len[0] = hr_reg_read(resp_c, PF_CAPS_C_MAX_GID);
	caps->max_cqes = 1 << hr_reg_read(resp_c, PF_CAPS_C_CQ_DEPTH);
	caps->num_xrcds = 1 << hr_reg_read(resp_c, PF_CAPS_C_NUM_XRCDS);
	caps->num_mtpts = 1 << hr_reg_read(resp_c, PF_CAPS_C_NUM_MRWS);
	caps->num_qps = 1 << hr_reg_read(resp_c, PF_CAPS_C_NUM_QPS);
	caps->max_qp_init_rdma = hr_reg_read(resp_c, PF_CAPS_C_MAX_ORD);
	caps->max_qp_dest_rdma = caps->max_qp_init_rdma;
	caps->max_wqes = 1 << le16_to_cpu(resp_c->sq_depth);

	caps->num_srqs = 1 << hr_reg_read(resp_d, PF_CAPS_D_NUM_SRQS);
	caps->cong_type = hr_reg_read(resp_d, PF_CAPS_D_CONG_TYPE);
	caps->max_srq_wrs = 1 << le16_to_cpu(resp_d->srq_depth);
	caps->ceqe_depth = 1 << hr_reg_read(resp_d, PF_CAPS_D_CEQ_DEPTH);
	caps->num_comp_vectors = hr_reg_read(resp_d, PF_CAPS_D_NUM_CEQS);
	caps->aeqe_depth = 1 << hr_reg_read(resp_d, PF_CAPS_D_AEQ_DEPTH);
	caps->reserved_pds = hr_reg_read(resp_d, PF_CAPS_D_RSV_PDS);
	caps->num_uars = 1 << hr_reg_read(resp_d, PF_CAPS_D_NUM_UARS);
	caps->reserved_qps = hr_reg_read(resp_d, PF_CAPS_D_RSV_QPS);
	caps->reserved_uars = hr_reg_read(resp_d, PF_CAPS_D_RSV_UARS);

	caps->reserved_mrws = hr_reg_read(resp_e, PF_CAPS_E_RSV_MRWS);
	caps->chunk_sz = 1 << hr_reg_read(resp_e, PF_CAPS_E_CHUNK_SIZE_SHIFT);
	caps->reserved_cqs = hr_reg_read(resp_e, PF_CAPS_E_RSV_CQS);
	caps->reserved_xrcds = hr_reg_read(resp_e, PF_CAPS_E_RSV_XRCDS);
	caps->reserved_srqs = hr_reg_read(resp_e, PF_CAPS_E_RSV_SRQS);
	caps->reserved_lkey = hr_reg_read(resp_e, PF_CAPS_E_RSV_LKEYS);

	caps->qpc_hop_num = ctx_hop_num;
	caps->sccc_hop_num = ctx_hop_num;
	caps->srqc_hop_num = ctx_hop_num;
	caps->cqc_hop_num = ctx_hop_num;
	caps->mpt_hop_num = ctx_hop_num;
	caps->mtt_hop_num = pbl_hop_num;
	caps->cqe_hop_num = pbl_hop_num;
	caps->srqwqe_hop_num = pbl_hop_num;
	caps->idx_hop_num = pbl_hop_num;
	caps->wqe_sq_hop_num = hr_reg_read(resp_d, PF_CAPS_D_SQWQE_HOP_NUM);
	caps->wqe_sge_hop_num = hr_reg_read(resp_d, PF_CAPS_D_EX_SGE_HOP_NUM);
	caps->wqe_rq_hop_num = hr_reg_read(resp_d, PF_CAPS_D_RQWQE_HOP_NUM);

	if (!(caps->page_size_cap & PAGE_SIZE))
		caps->page_size_cap = HNS_ROCE_V2_PAGE_SIZE_SUPPORTED;

	if (!hr_dev->is_vf) {
		caps->cqe_sz = resp_a->cqe_sz;
		caps->qpc_sz = le16_to_cpu(resp_b->qpc_sz);
		caps->default_aeq_arm_st =
				hr_reg_read(resp_d, PF_CAPS_D_AEQ_ARM_ST);
		caps->default_ceq_arm_st =
				hr_reg_read(resp_d, PF_CAPS_D_CEQ_ARM_ST);
		caps->default_ceq_max_cnt = le16_to_cpu(resp_e->ceq_max_cnt);
		caps->default_ceq_period = le16_to_cpu(resp_e->ceq_period);
		caps->default_aeq_max_cnt = le16_to_cpu(resp_e->aeq_max_cnt);
		caps->default_aeq_period = le16_to_cpu(resp_e->aeq_period);
	}

	return 0;
}

static int config_hem_entry_size(struct hns_roce_dev *hr_dev, u32 type, u32 val)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_ENTRY_SIZE,
				      false);

	hr_reg_write(req, CFG_HEM_ENTRY_SIZE_TYPE, type);
	hr_reg_write(req, CFG_HEM_ENTRY_SIZE_VALUE, val);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int hns_roce_config_entry_size(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_caps *caps = &hr_dev->caps;
	int ret;

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08)
		return 0;

	ret = config_hem_entry_size(hr_dev, HNS_ROCE_CFG_QPC_SIZE,
				    caps->qpc_sz);
	if (ret) {
		dev_err(hr_dev->dev, "failed to cfg qpc sz, ret = %d.\n", ret);
		return ret;
	}

	ret = config_hem_entry_size(hr_dev, HNS_ROCE_CFG_SCCC_SIZE,
				    caps->sccc_sz);
	if (ret)
		dev_err(hr_dev->dev, "failed to cfg sccc sz, ret = %d.\n", ret);

	return ret;
}

static int hns_roce_v2_vf_profile(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	hr_dev->func_num = 1;

	ret = hns_roce_query_caps(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query VF caps, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_query_vf_resource(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query VF resource, ret = %d.\n", ret);
		return ret;
	}

	apply_func_caps(hr_dev);

	ret = hns_roce_v2_set_bt(hr_dev);
	if (ret)
		dev_err(dev, "failed to config VF BA table, ret = %d.\n", ret);

	return ret;
}

static int hns_roce_v2_pf_profile(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	ret = hns_roce_query_func_info(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query func info, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_config_global_param(hr_dev);
	if (ret) {
		dev_err(dev, "failed to config global param, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_set_vf_switch_param(hr_dev);
	if (ret) {
		dev_err(dev, "failed to set switch param, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_query_caps(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query PF caps, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_query_pf_resource(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query pf resource, ret = %d.\n", ret);
		return ret;
	}

	apply_func_caps(hr_dev);

	ret = hns_roce_alloc_vf_resource(hr_dev);
	if (ret) {
		dev_err(dev, "failed to alloc vf resource, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_v2_set_bt(hr_dev);
	if (ret) {
		dev_err(dev, "failed to config BA table, ret = %d.\n", ret);
		return ret;
	}

	/* Configure the size of QPC, SCCC, etc. */
	return hns_roce_config_entry_size(hr_dev);
}

static int hns_roce_v2_profile(struct hns_roce_dev *hr_dev)
{
	struct device *dev = hr_dev->dev;
	int ret;

	ret = hns_roce_cmq_query_hw_info(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query hardware info, ret = %d.\n", ret);
		return ret;
	}

	ret = hns_roce_query_fw_ver(hr_dev);
	if (ret) {
		dev_err(dev, "failed to query firmware info, ret = %d.\n", ret);
		return ret;
	}

	hr_dev->vendor_part_id = hr_dev->pci_dev->device;
	hr_dev->sys_image_guid = be64_to_cpu(hr_dev->ib_dev.node_guid);

	if (hr_dev->is_vf)
		return hns_roce_v2_vf_profile(hr_dev);
	else
		return hns_roce_v2_pf_profile(hr_dev);
}

static void config_llm_table(struct hns_roce_buf *data_buf, void *cfg_buf)
{
	u32 i, next_ptr, page_num;
	__le64 *entry = cfg_buf;
	dma_addr_t addr;
	u64 val;

	page_num = data_buf->npages;
	for (i = 0; i < page_num; i++) {
		addr = hns_roce_buf_page(data_buf, i);
		if (i == (page_num - 1))
			next_ptr = 0;
		else
			next_ptr = i + 1;

		val = HNS_ROCE_EXT_LLM_ENTRY(addr, (u64)next_ptr);
		entry[i] = cpu_to_le64(val);
	}
}

static int set_llm_cfg_to_hw(struct hns_roce_dev *hr_dev,
			     struct hns_roce_link_table *table)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cmq_req *r_a = (struct hns_roce_cmq_req *)desc[0].data;
	struct hns_roce_cmq_req *r_b = (struct hns_roce_cmq_req *)desc[1].data;
	struct hns_roce_buf *buf = table->buf;
	enum hns_roce_opcode_type opcode;
	dma_addr_t addr;

	opcode = HNS_ROCE_OPC_CFG_EXT_LLM;
	hns_roce_cmq_setup_basic_desc(&desc[0], opcode, false);
	desc[0].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);
	hns_roce_cmq_setup_basic_desc(&desc[1], opcode, false);

	hr_reg_write(r_a, CFG_LLM_A_BA_L, lower_32_bits(table->table.map));
	hr_reg_write(r_a, CFG_LLM_A_BA_H, upper_32_bits(table->table.map));
	hr_reg_write(r_a, CFG_LLM_A_DEPTH, buf->npages);
	hr_reg_write(r_a, CFG_LLM_A_PGSZ, to_hr_hw_page_shift(buf->page_shift));
	hr_reg_enable(r_a, CFG_LLM_A_INIT_EN);

	addr = to_hr_hw_page_addr(hns_roce_buf_page(buf, 0));
	hr_reg_write(r_a, CFG_LLM_A_HEAD_BA_L, lower_32_bits(addr));
	hr_reg_write(r_a, CFG_LLM_A_HEAD_BA_H, upper_32_bits(addr));
	hr_reg_write(r_a, CFG_LLM_A_HEAD_NXTPTR, 1);
	hr_reg_write(r_a, CFG_LLM_A_HEAD_PTR, 0);

	addr = to_hr_hw_page_addr(hns_roce_buf_page(buf, buf->npages - 1));
	hr_reg_write(r_b, CFG_LLM_B_TAIL_BA_L, lower_32_bits(addr));
	hr_reg_write(r_b, CFG_LLM_B_TAIL_BA_H, upper_32_bits(addr));
	hr_reg_write(r_b, CFG_LLM_B_TAIL_PTR, buf->npages - 1);

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static struct hns_roce_link_table *
alloc_link_table_buf(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_link_table *link_tbl;
	u32 pg_shift, size, min_size;

	link_tbl = &priv->ext_llm;
	pg_shift = hr_dev->caps.llm_buf_pg_sz + PAGE_SHIFT;
	size = hr_dev->caps.num_qps * HNS_ROCE_V2_EXT_LLM_ENTRY_SZ;
	min_size = HNS_ROCE_EXT_LLM_MIN_PAGES(hr_dev->caps.sl_num) << pg_shift;

	/* Alloc data table */
	size = max(size, min_size);
	link_tbl->buf = hns_roce_buf_alloc(hr_dev, size, pg_shift, 0);
	if (IS_ERR(link_tbl->buf))
		return ERR_PTR(-ENOMEM);

	/* Alloc config table */
	size = link_tbl->buf->npages * sizeof(u64);
	link_tbl->table.buf = dma_alloc_coherent(hr_dev->dev, size,
						 &link_tbl->table.map,
						 GFP_KERNEL);
	if (!link_tbl->table.buf) {
		hns_roce_buf_free(hr_dev, link_tbl->buf);
		return ERR_PTR(-ENOMEM);
	}

	return link_tbl;
}

static void free_link_table_buf(struct hns_roce_dev *hr_dev,
				struct hns_roce_link_table *tbl)
{
	if (tbl->buf) {
		u32 size = tbl->buf->npages * sizeof(u64);

		dma_free_coherent(hr_dev->dev, size, tbl->table.buf,
				  tbl->table.map);
	}

	hns_roce_buf_free(hr_dev, tbl->buf);
}

static int hns_roce_init_link_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_link_table *link_tbl;
	int ret;

	link_tbl = alloc_link_table_buf(hr_dev);
	if (IS_ERR(link_tbl))
		return -ENOMEM;

	if (WARN_ON(link_tbl->buf->npages > HNS_ROCE_V2_EXT_LLM_MAX_DEPTH)) {
		ret = -EINVAL;
		goto err_alloc;
	}

	config_llm_table(link_tbl->buf, link_tbl->table.buf);
	ret = set_llm_cfg_to_hw(hr_dev, link_tbl);
	if (ret)
		goto err_alloc;

	return 0;

err_alloc:
	free_link_table_buf(hr_dev, link_tbl);
	return ret;
}

static void hns_roce_free_link_table(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;

	free_link_table_buf(hr_dev, &priv->ext_llm);
}

static void free_dip_list(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_dip *hr_dip;
	struct hns_roce_dip *tmp;
	unsigned long flags;

	spin_lock_irqsave(&hr_dev->dip_list_lock, flags);

	list_for_each_entry_safe(hr_dip, tmp, &hr_dev->dip_list, node) {
		list_del(&hr_dip->node);
		kfree(hr_dip);
	}

	spin_unlock_irqrestore(&hr_dev->dip_list_lock, flags);
}

static struct ib_pd *free_mr_init_pd(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_pd *hr_pd;
	struct ib_pd *pd;

	hr_pd = kzalloc(sizeof(*hr_pd), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hr_pd))
		return NULL;
	pd = &hr_pd->ibpd;
	pd->device = ibdev;

	if (hns_roce_alloc_pd(pd, NULL)) {
		ibdev_err(ibdev, "failed to create pd for free mr.\n");
		kfree(hr_pd);
		return NULL;
	}
	free_mr->rsv_pd = to_hr_pd(pd);
	free_mr->rsv_pd->ibpd.device = &hr_dev->ib_dev;
	free_mr->rsv_pd->ibpd.uobject = NULL;
	free_mr->rsv_pd->ibpd.__internal_mr = NULL;
	atomic_set(&free_mr->rsv_pd->ibpd.usecnt, 0);

	return pd;
}

static struct ib_cq *free_mr_init_cq(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct ib_cq_init_attr cq_init_attr = {};
	struct hns_roce_cq *hr_cq;
	struct ib_cq *cq;

	cq_init_attr.cqe = HNS_ROCE_FREE_MR_USED_CQE_NUM;

	hr_cq = kzalloc(sizeof(*hr_cq), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hr_cq))
		return NULL;

	cq = &hr_cq->ib_cq;
	cq->device = ibdev;

	if (hns_roce_create_cq(cq, &cq_init_attr, NULL)) {
		ibdev_err(ibdev, "failed to create cq for free mr.\n");
		kfree(hr_cq);
		return NULL;
	}
	free_mr->rsv_cq = to_hr_cq(cq);
	free_mr->rsv_cq->ib_cq.device = &hr_dev->ib_dev;
	free_mr->rsv_cq->ib_cq.uobject = NULL;
	free_mr->rsv_cq->ib_cq.comp_handler = NULL;
	free_mr->rsv_cq->ib_cq.event_handler = NULL;
	free_mr->rsv_cq->ib_cq.cq_context = NULL;
	atomic_set(&free_mr->rsv_cq->ib_cq.usecnt, 0);

	return cq;
}

static int free_mr_init_qp(struct hns_roce_dev *hr_dev, struct ib_cq *cq,
			   struct ib_qp_init_attr *init_attr, int i)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *hr_qp;
	struct ib_qp *qp;
	int ret;

	hr_qp = kzalloc(sizeof(*hr_qp), GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(hr_qp))
		return -ENOMEM;

	qp = &hr_qp->ibqp;
	qp->device = ibdev;

	ret = hns_roce_create_qp(qp, init_attr, NULL);
	if (ret) {
		ibdev_err(ibdev, "failed to create qp for free mr.\n");
		kfree(hr_qp);
		return ret;
	}

	free_mr->rsv_qp[i] = hr_qp;
	free_mr->rsv_qp[i]->ibqp.recv_cq = cq;
	free_mr->rsv_qp[i]->ibqp.send_cq = cq;

	return 0;
}

static void free_mr_exit(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_qp *qp;
	int i;

	for (i = 0; i < ARRAY_SIZE(free_mr->rsv_qp); i++) {
		if (free_mr->rsv_qp[i]) {
			qp = &free_mr->rsv_qp[i]->ibqp;
			hns_roce_v2_destroy_qp(qp, NULL);
			kfree(free_mr->rsv_qp[i]);
			free_mr->rsv_qp[i] = NULL;
		}
	}

	if (free_mr->rsv_cq) {
		hns_roce_destroy_cq(&free_mr->rsv_cq->ib_cq, NULL);
		kfree(free_mr->rsv_cq);
		free_mr->rsv_cq = NULL;
	}

	if (free_mr->rsv_pd) {
		hns_roce_dealloc_pd(&free_mr->rsv_pd->ibpd, NULL);
		kfree(free_mr->rsv_pd);
		free_mr->rsv_pd = NULL;
	}
}

static int free_mr_alloc_res(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_qp_init_attr qp_init_attr = {};
	struct ib_pd *pd;
	struct ib_cq *cq;
	int ret;
	int i;

	pd = free_mr_init_pd(hr_dev);
	if (!pd)
		return -ENOMEM;

	cq = free_mr_init_cq(hr_dev);
	if (!cq) {
		ret = -ENOMEM;
		goto create_failed_cq;
	}

	qp_init_attr.qp_type = IB_QPT_RC;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	for (i = 0; i < ARRAY_SIZE(free_mr->rsv_qp); i++) {
		qp_init_attr.cap.max_send_wr = HNS_ROCE_FREE_MR_USED_SQWQE_NUM;
		qp_init_attr.cap.max_send_sge = HNS_ROCE_FREE_MR_USED_SQSGE_NUM;
		qp_init_attr.cap.max_recv_wr = HNS_ROCE_FREE_MR_USED_RQWQE_NUM;
		qp_init_attr.cap.max_recv_sge = HNS_ROCE_FREE_MR_USED_RQSGE_NUM;

		ret = free_mr_init_qp(hr_dev, cq, &qp_init_attr, i);
		if (ret)
			goto create_failed_qp;
	}

	return 0;

create_failed_qp:
	for (i--; i >= 0; i--) {
		hns_roce_v2_destroy_qp(&free_mr->rsv_qp[i]->ibqp, NULL);
		kfree(free_mr->rsv_qp[i]);
	}
	hns_roce_destroy_cq(cq, NULL);
	kfree(cq);

create_failed_cq:
	hns_roce_dealloc_pd(pd, NULL);
	kfree(pd);

	return ret;
}

static int free_mr_modify_rsv_qp(struct hns_roce_dev *hr_dev,
				 struct ib_qp_attr *attr, int sl_num)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *hr_qp;
	int loopback;
	int mask;
	int ret;

	hr_qp = to_hr_qp(&free_mr->rsv_qp[sl_num]->ibqp);
	hr_qp->free_mr_en = 1;
	hr_qp->ibqp.device = ibdev;
	hr_qp->ibqp.qp_type = IB_QPT_RC;

	mask = IB_QP_STATE | IB_QP_PKEY_INDEX | IB_QP_PORT | IB_QP_ACCESS_FLAGS;
	attr->qp_state = IB_QPS_INIT;
	attr->port_num = 1;
	attr->qp_access_flags = IB_ACCESS_REMOTE_WRITE;
	ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, attr, mask, IB_QPS_INIT,
				    IB_QPS_INIT, NULL);
	if (ret) {
		ibdev_err(ibdev, "failed to modify qp to init, ret = %d.\n",
			  ret);
		return ret;
	}

	loopback = hr_dev->loop_idc;
	/* Set qpc lbi = 1 incidate loopback IO */
	hr_dev->loop_idc = 1;

	mask = IB_QP_STATE | IB_QP_AV | IB_QP_PATH_MTU | IB_QP_DEST_QPN |
	       IB_QP_RQ_PSN | IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER;
	attr->qp_state = IB_QPS_RTR;
	attr->ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
	attr->path_mtu = IB_MTU_256;
	attr->dest_qp_num = hr_qp->qpn;
	attr->rq_psn = HNS_ROCE_FREE_MR_USED_PSN;

	rdma_ah_set_sl(&attr->ah_attr, (u8)sl_num);

	ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, attr, mask, IB_QPS_INIT,
				    IB_QPS_RTR, NULL);
	hr_dev->loop_idc = loopback;
	if (ret) {
		ibdev_err(ibdev, "failed to modify qp to rtr, ret = %d.\n",
			  ret);
		return ret;
	}

	mask = IB_QP_STATE | IB_QP_SQ_PSN | IB_QP_RETRY_CNT | IB_QP_TIMEOUT |
	       IB_QP_RNR_RETRY | IB_QP_MAX_QP_RD_ATOMIC;
	attr->qp_state = IB_QPS_RTS;
	attr->sq_psn = HNS_ROCE_FREE_MR_USED_PSN;
	attr->retry_cnt = HNS_ROCE_FREE_MR_USED_QP_RETRY_CNT;
	attr->timeout = HNS_ROCE_FREE_MR_USED_QP_TIMEOUT;
	ret = hr_dev->hw->modify_qp(&hr_qp->ibqp, attr, mask, IB_QPS_RTR,
				    IB_QPS_RTS, NULL);
	if (ret)
		ibdev_err(ibdev, "failed to modify qp to rts, ret = %d.\n",
			  ret);

	return ret;
}

static int free_mr_modify_qp(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_qp_attr attr = {};
	int ret;
	int i;

	rdma_ah_set_grh(&attr.ah_attr, NULL, 0, 0, 1, 0);
	rdma_ah_set_static_rate(&attr.ah_attr, 3);
	rdma_ah_set_port_num(&attr.ah_attr, 1);

	for (i = 0; i < ARRAY_SIZE(free_mr->rsv_qp); i++) {
		ret = free_mr_modify_rsv_qp(hr_dev, &attr, i);
		if (ret)
			return ret;
	}

	return 0;
}

static int free_mr_init(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	int ret;

	mutex_init(&free_mr->mutex);

	ret = free_mr_alloc_res(hr_dev);
	if (ret)
		return ret;

	ret = free_mr_modify_qp(hr_dev);
	if (ret)
		goto err_modify_qp;

	return 0;

err_modify_qp:
	free_mr_exit(hr_dev);

	return ret;
}

static int get_hem_table(struct hns_roce_dev *hr_dev)
{
	unsigned int qpc_count;
	unsigned int cqc_count;
	unsigned int gmv_count;
	int ret;
	int i;

	/* Alloc memory for source address table buffer space chunk */
	for (gmv_count = 0; gmv_count < hr_dev->caps.gmv_entry_num;
	     gmv_count++) {
		ret = hns_roce_table_get(hr_dev, &hr_dev->gmv_table, gmv_count);
		if (ret)
			goto err_gmv_failed;
	}

	if (hr_dev->is_vf)
		return 0;

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

err_gmv_failed:
	for (i = 0; i < gmv_count; i++)
		hns_roce_table_put(hr_dev, &hr_dev->gmv_table, i);

	return ret;
}

static void put_hem_table(struct hns_roce_dev *hr_dev)
{
	int i;

	for (i = 0; i < hr_dev->caps.gmv_entry_num; i++)
		hns_roce_table_put(hr_dev, &hr_dev->gmv_table, i);

	if (hr_dev->is_vf)
		return;

	for (i = 0; i < hr_dev->caps.qpc_timer_bt_num; i++)
		hns_roce_table_put(hr_dev, &hr_dev->qpc_timer_table, i);

	for (i = 0; i < hr_dev->caps.cqc_timer_bt_num; i++)
		hns_roce_table_put(hr_dev, &hr_dev->cqc_timer_table, i);
}

static int hns_roce_v2_init(struct hns_roce_dev *hr_dev)
{
	int ret;

	/* The hns ROCEE requires the extdb info to be cleared before using */
	ret = hns_roce_clear_extdb_list_info(hr_dev);
	if (ret)
		return ret;

	ret = get_hem_table(hr_dev);
	if (ret)
		return ret;

	if (hr_dev->is_vf)
		return 0;

	ret = hns_roce_init_link_table(hr_dev);
	if (ret) {
		dev_err(hr_dev->dev, "failed to init llm, ret = %d.\n", ret);
		goto err_llm_init_failed;
	}

	return 0;

err_llm_init_failed:
	put_hem_table(hr_dev);

	return ret;
}

static void hns_roce_v2_exit(struct hns_roce_dev *hr_dev)
{
	hns_roce_function_clear(hr_dev);

	if (!hr_dev->is_vf)
		hns_roce_free_link_table(hr_dev);

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP09)
		free_dip_list(hr_dev);
}

static int hns_roce_mbox_post(struct hns_roce_dev *hr_dev,
			      struct hns_roce_mbox_msg *mbox_msg)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_post_mbox *mb = (struct hns_roce_post_mbox *)desc.data;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_POST_MB, false);

	mb->in_param_l = cpu_to_le32(mbox_msg->in_param);
	mb->in_param_h = cpu_to_le32(mbox_msg->in_param >> 32);
	mb->out_param_l = cpu_to_le32(mbox_msg->out_param);
	mb->out_param_h = cpu_to_le32(mbox_msg->out_param >> 32);
	mb->cmd_tag = cpu_to_le32(mbox_msg->tag << 8 | mbox_msg->cmd);
	mb->token_event_en = cpu_to_le32(mbox_msg->event_en << 16 |
					 mbox_msg->token);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int v2_wait_mbox_complete(struct hns_roce_dev *hr_dev, u32 timeout,
				 u8 *complete_status)
{
	struct hns_roce_mbox_status *mb_st;
	struct hns_roce_cmq_desc desc;
	unsigned long end;
	int ret = -EBUSY;
	u32 status;
	bool busy;

	mb_st = (struct hns_roce_mbox_status *)desc.data;
	end = msecs_to_jiffies(timeout) + jiffies;
	while (v2_chk_mbox_is_avail(hr_dev, &busy)) {
		if (hr_dev->cmd.state == HNS_ROCE_CMDQ_STATE_FATAL_ERR)
			return -EIO;

		status = 0;
		hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_QUERY_MB_ST,
					      true);
		ret = __hns_roce_cmq_send(hr_dev, &desc, 1);
		if (!ret) {
			status = le32_to_cpu(mb_st->mb_status_hw_run);
			/* No pending message exists in ROCEE mbox. */
			if (!(status & MB_ST_HW_RUN_M))
				break;
		} else if (!v2_chk_mbox_is_avail(hr_dev, &busy)) {
			break;
		}

		if (time_after(jiffies, end)) {
			dev_err_ratelimited(hr_dev->dev,
					    "failed to wait mbox status 0x%x\n",
					    status);
			return -ETIMEDOUT;
		}

		cond_resched();
		ret = -EBUSY;
	}

	if (!ret) {
		*complete_status = (u8)(status & MB_ST_COMPLETE_M);
	} else if (!v2_chk_mbox_is_avail(hr_dev, &busy)) {
		/* Ignore all errors if the mbox is unavailable. */
		ret = 0;
		*complete_status = MB_ST_COMPLETE_M;
	}

	return ret;
}

static int v2_post_mbox(struct hns_roce_dev *hr_dev,
			struct hns_roce_mbox_msg *mbox_msg)
{
	u8 status = 0;
	int ret;

	/* Waiting for the mbox to be idle */
	ret = v2_wait_mbox_complete(hr_dev, HNS_ROCE_V2_GO_BIT_TIMEOUT_MSECS,
				    &status);
	if (unlikely(ret)) {
		dev_err_ratelimited(hr_dev->dev,
				    "failed to check post mbox status = 0x%x, ret = %d.\n",
				    status, ret);
		return ret;
	}

	/* Post new message to mbox */
	ret = hns_roce_mbox_post(hr_dev, mbox_msg);
	if (ret)
		dev_err_ratelimited(hr_dev->dev,
				    "failed to post mailbox, ret = %d.\n", ret);

	return ret;
}

static int v2_poll_mbox_done(struct hns_roce_dev *hr_dev)
{
	u8 status = 0;
	int ret;

	ret = v2_wait_mbox_complete(hr_dev, HNS_ROCE_CMD_TIMEOUT_MSECS,
				    &status);
	if (!ret) {
		if (status != MB_ST_COMPLETE_SUCC)
			return -EBUSY;
	} else {
		dev_err_ratelimited(hr_dev->dev,
				    "failed to check mbox status = 0x%x, ret = %d.\n",
				    status, ret);
	}

	return ret;
}

static void copy_gid(void *dest, const union ib_gid *gid)
{
#define GID_SIZE 4
	const union ib_gid *src = gid;
	__le32 (*p)[GID_SIZE] = dest;
	int i;

	if (!gid)
		src = &zgid;

	for (i = 0; i < GID_SIZE; i++)
		(*p)[i] = cpu_to_le32(*(u32 *)&src->raw[i * sizeof(u32)]);
}

static int config_sgid_table(struct hns_roce_dev *hr_dev,
			     int gid_index, const union ib_gid *gid,
			     enum hns_roce_sgid_type sgid_type)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cfg_sgid_tb *sgid_tb =
				    (struct hns_roce_cfg_sgid_tb *)desc.data;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_SGID_TB, false);

	hr_reg_write(sgid_tb, CFG_SGID_TB_TABLE_IDX, gid_index);
	hr_reg_write(sgid_tb, CFG_SGID_TB_VF_SGID_TYPE, sgid_type);

	copy_gid(&sgid_tb->vf_sgid_l, gid);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int config_gmv_table(struct hns_roce_dev *hr_dev,
			    int gid_index, const union ib_gid *gid,
			    enum hns_roce_sgid_type sgid_type,
			    const struct ib_gid_attr *attr)
{
	struct hns_roce_cmq_desc desc[2];
	struct hns_roce_cfg_gmv_tb_a *tb_a =
				(struct hns_roce_cfg_gmv_tb_a *)desc[0].data;
	struct hns_roce_cfg_gmv_tb_b *tb_b =
				(struct hns_roce_cfg_gmv_tb_b *)desc[1].data;

	u16 vlan_id = VLAN_CFI_MASK;
	u8 mac[ETH_ALEN] = {};
	int ret;

	if (gid) {
		ret = rdma_read_gid_l2_fields(attr, &vlan_id, mac);
		if (ret)
			return ret;
	}

	hns_roce_cmq_setup_basic_desc(&desc[0], HNS_ROCE_OPC_CFG_GMV_TBL, false);
	desc[0].flag |= cpu_to_le16(HNS_ROCE_CMD_FLAG_NEXT);

	hns_roce_cmq_setup_basic_desc(&desc[1], HNS_ROCE_OPC_CFG_GMV_TBL, false);

	copy_gid(&tb_a->vf_sgid_l, gid);

	hr_reg_write(tb_a, GMV_TB_A_VF_SGID_TYPE, sgid_type);
	hr_reg_write(tb_a, GMV_TB_A_VF_VLAN_EN, vlan_id < VLAN_CFI_MASK);
	hr_reg_write(tb_a, GMV_TB_A_VF_VLAN_ID, vlan_id);

	tb_b->vf_smac_l = cpu_to_le32(*(u32 *)mac);

	hr_reg_write(tb_b, GMV_TB_B_SMAC_H, *(u16 *)&mac[4]);
	hr_reg_write(tb_b, GMV_TB_B_SGID_IDX, gid_index);

	return hns_roce_cmq_send(hr_dev, desc, 2);
}

static int hns_roce_v2_set_gid(struct hns_roce_dev *hr_dev, int gid_index,
			       const union ib_gid *gid,
			       const struct ib_gid_attr *attr)
{
	enum hns_roce_sgid_type sgid_type = GID_TYPE_FLAG_ROCE_V1;
	int ret;

	if (gid) {
		if (attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP) {
			if (ipv6_addr_v4mapped((void *)gid))
				sgid_type = GID_TYPE_FLAG_ROCE_V2_IPV4;
			else
				sgid_type = GID_TYPE_FLAG_ROCE_V2_IPV6;
		} else if (attr->gid_type == IB_GID_TYPE_ROCE) {
			sgid_type = GID_TYPE_FLAG_ROCE_V1;
		}
	}

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
		ret = config_gmv_table(hr_dev, gid_index, gid, sgid_type, attr);
	else
		ret = config_sgid_table(hr_dev, gid_index, gid, sgid_type);

	if (ret)
		ibdev_err(&hr_dev->ib_dev, "failed to set gid, ret = %d!\n",
			  ret);

	return ret;
}

static int hns_roce_v2_set_mac(struct hns_roce_dev *hr_dev, u8 phy_port,
			       const u8 *addr)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cfg_smac_tb *smac_tb =
				    (struct hns_roce_cfg_smac_tb *)desc.data;
	u16 reg_smac_h;
	u32 reg_smac_l;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_SMAC_TB, false);

	reg_smac_l = *(u32 *)(&addr[0]);
	reg_smac_h = *(u16 *)(&addr[4]);

	hr_reg_write(smac_tb, CFG_SMAC_TB_IDX, phy_port);
	hr_reg_write(smac_tb, CFG_SMAC_TB_VF_SMAC_H, reg_smac_h);
	smac_tb->vf_smac_l = cpu_to_le32(reg_smac_l);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int set_mtpt_pbl(struct hns_roce_dev *hr_dev,
			struct hns_roce_v2_mpt_entry *mpt_entry,
			struct hns_roce_mr *mr)
{
	u64 pages[HNS_ROCE_V2_MAX_INNER_MTPT_NUM] = { 0 };
	struct ib_device *ibdev = &hr_dev->ib_dev;
	dma_addr_t pbl_ba;
	int i, count;

	count = hns_roce_mtr_find(hr_dev, &mr->pbl_mtr, 0, pages,
				  min_t(int, ARRAY_SIZE(pages), mr->npages),
				  &pbl_ba);
	if (count < 1) {
		ibdev_err(ibdev, "failed to find PBL mtr, count = %d.\n",
			  count);
		return -ENOBUFS;
	}

	/* Aligned to the hardware address access unit */
	for (i = 0; i < count; i++)
		pages[i] >>= 6;

	mpt_entry->pbl_size = cpu_to_le32(mr->npages);
	mpt_entry->pbl_ba_l = cpu_to_le32(pbl_ba >> 3);
	hr_reg_write(mpt_entry, MPT_PBL_BA_H, upper_32_bits(pbl_ba >> 3));

	mpt_entry->pa0_l = cpu_to_le32(lower_32_bits(pages[0]));
	hr_reg_write(mpt_entry, MPT_PA0_H, upper_32_bits(pages[0]));

	mpt_entry->pa1_l = cpu_to_le32(lower_32_bits(pages[1]));
	hr_reg_write(mpt_entry, MPT_PA1_H, upper_32_bits(pages[1]));
	hr_reg_write(mpt_entry, MPT_PBL_BUF_PG_SZ,
		     to_hr_hw_page_shift(mr->pbl_mtr.hem_cfg.buf_pg_shift));

	return 0;
}

static int hns_roce_v2_write_mtpt(struct hns_roce_dev *hr_dev,
				  void *mb_buf, struct hns_roce_mr *mr)
{
	struct hns_roce_v2_mpt_entry *mpt_entry;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	hr_reg_write(mpt_entry, MPT_ST, V2_MPT_ST_VALID);
	hr_reg_write(mpt_entry, MPT_PD, mr->pd);

	hr_reg_write_bool(mpt_entry, MPT_BIND_EN,
			  mr->access & IB_ACCESS_MW_BIND);
	hr_reg_write_bool(mpt_entry, MPT_ATOMIC_EN,
			  mr->access & IB_ACCESS_REMOTE_ATOMIC);
	hr_reg_write_bool(mpt_entry, MPT_RR_EN,
			  mr->access & IB_ACCESS_REMOTE_READ);
	hr_reg_write_bool(mpt_entry, MPT_RW_EN,
			  mr->access & IB_ACCESS_REMOTE_WRITE);
	hr_reg_write_bool(mpt_entry, MPT_LW_EN,
			  mr->access & IB_ACCESS_LOCAL_WRITE);

	mpt_entry->len_l = cpu_to_le32(lower_32_bits(mr->size));
	mpt_entry->len_h = cpu_to_le32(upper_32_bits(mr->size));
	mpt_entry->lkey = cpu_to_le32(mr->key);
	mpt_entry->va_l = cpu_to_le32(lower_32_bits(mr->iova));
	mpt_entry->va_h = cpu_to_le32(upper_32_bits(mr->iova));

	if (mr->type != MR_TYPE_MR)
		hr_reg_enable(mpt_entry, MPT_PA);

	if (mr->type == MR_TYPE_DMA)
		return 0;

	if (mr->pbl_hop_num != HNS_ROCE_HOP_NUM_0)
		hr_reg_write(mpt_entry, MPT_PBL_HOP_NUM, mr->pbl_hop_num);

	hr_reg_write(mpt_entry, MPT_PBL_BA_PG_SZ,
		     to_hr_hw_page_shift(mr->pbl_mtr.hem_cfg.ba_pg_shift));
	hr_reg_enable(mpt_entry, MPT_INNER_PA_VLD);

	return set_mtpt_pbl(hr_dev, mpt_entry, mr);
}

static int hns_roce_v2_rereg_write_mtpt(struct hns_roce_dev *hr_dev,
					struct hns_roce_mr *mr, int flags,
					void *mb_buf)
{
	struct hns_roce_v2_mpt_entry *mpt_entry = mb_buf;
	u32 mr_access_flags = mr->access;
	int ret = 0;

	hr_reg_write(mpt_entry, MPT_ST, V2_MPT_ST_VALID);
	hr_reg_write(mpt_entry, MPT_PD, mr->pd);

	if (flags & IB_MR_REREG_ACCESS) {
		hr_reg_write(mpt_entry, MPT_BIND_EN,
			     (mr_access_flags & IB_ACCESS_MW_BIND ? 1 : 0));
		hr_reg_write(mpt_entry, MPT_ATOMIC_EN,
			     mr_access_flags & IB_ACCESS_REMOTE_ATOMIC ? 1 : 0);
		hr_reg_write(mpt_entry, MPT_RR_EN,
			     mr_access_flags & IB_ACCESS_REMOTE_READ ? 1 : 0);
		hr_reg_write(mpt_entry, MPT_RW_EN,
			     mr_access_flags & IB_ACCESS_REMOTE_WRITE ? 1 : 0);
		hr_reg_write(mpt_entry, MPT_LW_EN,
			     mr_access_flags & IB_ACCESS_LOCAL_WRITE ? 1 : 0);
	}

	if (flags & IB_MR_REREG_TRANS) {
		mpt_entry->va_l = cpu_to_le32(lower_32_bits(mr->iova));
		mpt_entry->va_h = cpu_to_le32(upper_32_bits(mr->iova));
		mpt_entry->len_l = cpu_to_le32(lower_32_bits(mr->size));
		mpt_entry->len_h = cpu_to_le32(upper_32_bits(mr->size));

		ret = set_mtpt_pbl(hr_dev, mpt_entry, mr);
	}

	return ret;
}

static int hns_roce_v2_frmr_write_mtpt(struct hns_roce_dev *hr_dev,
				       void *mb_buf, struct hns_roce_mr *mr)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_v2_mpt_entry *mpt_entry;
	dma_addr_t pbl_ba = 0;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	if (hns_roce_mtr_find(hr_dev, &mr->pbl_mtr, 0, NULL, 0, &pbl_ba) < 0) {
		ibdev_err(ibdev, "failed to find frmr mtr.\n");
		return -ENOBUFS;
	}

	hr_reg_write(mpt_entry, MPT_ST, V2_MPT_ST_FREE);
	hr_reg_write(mpt_entry, MPT_PD, mr->pd);

	hr_reg_enable(mpt_entry, MPT_RA_EN);
	hr_reg_enable(mpt_entry, MPT_R_INV_EN);

	hr_reg_enable(mpt_entry, MPT_FRE);
	hr_reg_clear(mpt_entry, MPT_MR_MW);
	hr_reg_enable(mpt_entry, MPT_BPD);
	hr_reg_clear(mpt_entry, MPT_PA);

	hr_reg_write(mpt_entry, MPT_PBL_HOP_NUM, 1);
	hr_reg_write(mpt_entry, MPT_PBL_BA_PG_SZ,
		     to_hr_hw_page_shift(mr->pbl_mtr.hem_cfg.ba_pg_shift));
	hr_reg_write(mpt_entry, MPT_PBL_BUF_PG_SZ,
		     to_hr_hw_page_shift(mr->pbl_mtr.hem_cfg.buf_pg_shift));

	mpt_entry->pbl_size = cpu_to_le32(mr->npages);

	mpt_entry->pbl_ba_l = cpu_to_le32(lower_32_bits(pbl_ba >> 3));
	hr_reg_write(mpt_entry, MPT_PBL_BA_H, upper_32_bits(pbl_ba >> 3));

	return 0;
}

static int hns_roce_v2_mw_write_mtpt(void *mb_buf, struct hns_roce_mw *mw)
{
	struct hns_roce_v2_mpt_entry *mpt_entry;

	mpt_entry = mb_buf;
	memset(mpt_entry, 0, sizeof(*mpt_entry));

	hr_reg_write(mpt_entry, MPT_ST, V2_MPT_ST_FREE);
	hr_reg_write(mpt_entry, MPT_PD, mw->pdn);

	hr_reg_enable(mpt_entry, MPT_R_INV_EN);
	hr_reg_enable(mpt_entry, MPT_LW_EN);

	hr_reg_enable(mpt_entry, MPT_MR_MW);
	hr_reg_enable(mpt_entry, MPT_BPD);
	hr_reg_clear(mpt_entry, MPT_PA);
	hr_reg_write(mpt_entry, MPT_BQP,
		     mw->ibmw.type == IB_MW_TYPE_1 ? 0 : 1);

	mpt_entry->lkey = cpu_to_le32(mw->rkey);

	hr_reg_write(mpt_entry, MPT_PBL_HOP_NUM,
		     mw->pbl_hop_num == HNS_ROCE_HOP_NUM_0 ? 0 :
							     mw->pbl_hop_num);
	hr_reg_write(mpt_entry, MPT_PBL_BA_PG_SZ,
		     mw->pbl_ba_pg_sz + PG_SHIFT_OFFSET);
	hr_reg_write(mpt_entry, MPT_PBL_BUF_PG_SZ,
		     mw->pbl_buf_pg_sz + PG_SHIFT_OFFSET);

	return 0;
}

static int free_mr_post_send_lp_wqe(struct hns_roce_qp *hr_qp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_qp->ibqp.device);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	const struct ib_send_wr *bad_wr;
	struct ib_rdma_wr rdma_wr = {};
	struct ib_send_wr *send_wr;
	int ret;

	send_wr = &rdma_wr.wr;
	send_wr->opcode = IB_WR_RDMA_WRITE;

	ret = hns_roce_v2_post_send(&hr_qp->ibqp, send_wr, &bad_wr);
	if (ret) {
		ibdev_err(ibdev, "failed to post wqe for free mr, ret = %d.\n",
			  ret);
		return ret;
	}

	return 0;
}

static int hns_roce_v2_poll_cq(struct ib_cq *ibcq, int num_entries,
			       struct ib_wc *wc);

static void free_mr_send_cmd_to_hw(struct hns_roce_dev *hr_dev)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	struct hns_roce_v2_free_mr *free_mr = &priv->free_mr;
	struct ib_wc wc[ARRAY_SIZE(free_mr->rsv_qp)];
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_qp *hr_qp;
	unsigned long end;
	int cqe_cnt = 0;
	int npolled;
	int ret;
	int i;

	/*
	 * If the device initialization is not complete or in the uninstall
	 * process, then there is no need to execute free mr.
	 */
	if (priv->handle->rinfo.reset_state == HNS_ROCE_STATE_RST_INIT ||
	    priv->handle->rinfo.instance_state == HNS_ROCE_STATE_INIT ||
	    hr_dev->state == HNS_ROCE_DEVICE_STATE_UNINIT)
		return;

	mutex_lock(&free_mr->mutex);

	for (i = 0; i < ARRAY_SIZE(free_mr->rsv_qp); i++) {
		hr_qp = free_mr->rsv_qp[i];

		ret = free_mr_post_send_lp_wqe(hr_qp);
		if (ret) {
			ibdev_err(ibdev,
				  "failed to send wqe (qp:0x%lx) for free mr, ret = %d.\n",
				  hr_qp->qpn, ret);
			break;
		}

		cqe_cnt++;
	}

	end = msecs_to_jiffies(HNS_ROCE_V2_FREE_MR_TIMEOUT) + jiffies;
	while (cqe_cnt) {
		npolled = hns_roce_v2_poll_cq(&free_mr->rsv_cq->ib_cq, cqe_cnt, wc);
		if (npolled < 0) {
			ibdev_err(ibdev,
				  "failed to poll cqe for free mr, remain %d cqe.\n",
				  cqe_cnt);
			goto out;
		}

		if (time_after(jiffies, end)) {
			ibdev_err(ibdev,
				  "failed to poll cqe for free mr and timeout, remain %d cqe.\n",
				  cqe_cnt);
			goto out;
		}
		cqe_cnt -= npolled;
	}

out:
	mutex_unlock(&free_mr->mutex);
}

static void hns_roce_v2_dereg_mr(struct hns_roce_dev *hr_dev)
{
	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08)
		free_mr_send_cmd_to_hw(hr_dev);
}

static void *get_cqe_v2(struct hns_roce_cq *hr_cq, int n)
{
	return hns_roce_buf_offset(hr_cq->mtr.kmem, n * hr_cq->cqe_size);
}

static void *get_sw_cqe_v2(struct hns_roce_cq *hr_cq, unsigned int n)
{
	struct hns_roce_v2_cqe *cqe = get_cqe_v2(hr_cq, n & hr_cq->ib_cq.cqe);

	/* Get cqe when Owner bit is Conversely with the MSB of cons_idx */
	return (hr_reg_read(cqe, CQE_OWNER) ^ !!(n & hr_cq->cq_depth)) ? cqe :
									 NULL;
}

static inline void update_cq_db(struct hns_roce_dev *hr_dev,
				struct hns_roce_cq *hr_cq)
{
	if (likely(hr_cq->flags & HNS_ROCE_CQ_FLAG_RECORD_DB)) {
		*hr_cq->set_ci_db = hr_cq->cons_index & V2_CQ_DB_CONS_IDX_M;
	} else {
		struct hns_roce_v2_db cq_db = {};

		hr_reg_write(&cq_db, DB_TAG, hr_cq->cqn);
		hr_reg_write(&cq_db, DB_CMD, HNS_ROCE_V2_CQ_DB);
		hr_reg_write(&cq_db, DB_CQ_CI, hr_cq->cons_index);
		hr_reg_write(&cq_db, DB_CQ_CMD_SN, 1);

		hns_roce_write64(hr_dev, (__le32 *)&cq_db, hr_cq->db_reg);
	}
}

static void __hns_roce_v2_cq_clean(struct hns_roce_cq *hr_cq, u32 qpn,
				   struct hns_roce_srq *srq)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_cq->ib_cq.device);
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
		if (hr_reg_read(cqe, CQE_LCL_QPN) == qpn) {
			if (srq && hr_reg_read(cqe, CQE_S_R)) {
				wqe_index = hr_reg_read(cqe, CQE_WQE_IDX);
				hns_roce_free_srq_wqe(srq, wqe_index);
			}
			++nfreed;
		} else if (nfreed) {
			dest = get_cqe_v2(hr_cq, (prod_index + nfreed) &
					  hr_cq->ib_cq.cqe);
			owner_bit = hr_reg_read(dest, CQE_OWNER);
			memcpy(dest, cqe, hr_cq->cqe_size);
			hr_reg_write(dest, CQE_OWNER, owner_bit);
		}
	}

	if (nfreed) {
		hr_cq->cons_index += nfreed;
		update_cq_db(hr_dev, hr_cq);
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

	hr_reg_write(cq_context, CQC_CQ_ST, V2_CQ_STATE_VALID);
	hr_reg_write(cq_context, CQC_ARM_ST, NO_ARMED);
	hr_reg_write(cq_context, CQC_SHIFT, ilog2(hr_cq->cq_depth));
	hr_reg_write(cq_context, CQC_CEQN, hr_cq->vector);
	hr_reg_write(cq_context, CQC_CQN, hr_cq->cqn);

	if (hr_cq->cqe_size == HNS_ROCE_V3_CQE_SIZE)
		hr_reg_write(cq_context, CQC_CQE_SIZE, CQE_SIZE_64B);

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_STASH)
		hr_reg_enable(cq_context, CQC_STASH);

	hr_reg_write(cq_context, CQC_CQE_CUR_BLK_ADDR_L,
		     to_hr_hw_page_addr(mtts[0]));
	hr_reg_write(cq_context, CQC_CQE_CUR_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts[0])));
	hr_reg_write(cq_context, CQC_CQE_HOP_NUM, hr_dev->caps.cqe_hop_num ==
		     HNS_ROCE_HOP_NUM_0 ? 0 : hr_dev->caps.cqe_hop_num);
	hr_reg_write(cq_context, CQC_CQE_NEX_BLK_ADDR_L,
		     to_hr_hw_page_addr(mtts[1]));
	hr_reg_write(cq_context, CQC_CQE_NEX_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts[1])));
	hr_reg_write(cq_context, CQC_CQE_BAR_PG_SZ,
		     to_hr_hw_page_shift(hr_cq->mtr.hem_cfg.ba_pg_shift));
	hr_reg_write(cq_context, CQC_CQE_BUF_PG_SZ,
		     to_hr_hw_page_shift(hr_cq->mtr.hem_cfg.buf_pg_shift));
	hr_reg_write(cq_context, CQC_CQE_BA_L, dma_handle >> 3);
	hr_reg_write(cq_context, CQC_CQE_BA_H, (dma_handle >> (32 + 3)));
	hr_reg_write_bool(cq_context, CQC_DB_RECORD_EN,
			  hr_cq->flags & HNS_ROCE_CQ_FLAG_RECORD_DB);
	hr_reg_write(cq_context, CQC_CQE_DB_RECORD_ADDR_L,
		     ((u32)hr_cq->db.dma) >> 1);
	hr_reg_write(cq_context, CQC_CQE_DB_RECORD_ADDR_H,
		     hr_cq->db.dma >> 32);
	hr_reg_write(cq_context, CQC_CQ_MAX_CNT,
		     HNS_ROCE_V2_CQ_DEFAULT_BURST_NUM);
	hr_reg_write(cq_context, CQC_CQ_PERIOD,
		     HNS_ROCE_V2_CQ_DEFAULT_INTERVAL);
}

static int hns_roce_v2_req_notify_cq(struct ib_cq *ibcq,
				     enum ib_cq_notify_flags flags)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibcq->device);
	struct hns_roce_cq *hr_cq = to_hr_cq(ibcq);
	struct hns_roce_v2_db cq_db = {};
	u32 notify_flag;

	/*
	 * flags = 0, then notify_flag : next
	 * flags = 1, then notify flag : solocited
	 */
	notify_flag = (flags & IB_CQ_SOLICITED_MASK) == IB_CQ_SOLICITED ?
		      V2_CQ_DB_REQ_NOT : V2_CQ_DB_REQ_NOT_SOL;

	hr_reg_write(&cq_db, DB_TAG, hr_cq->cqn);
	hr_reg_write(&cq_db, DB_CMD, HNS_ROCE_V2_CQ_DB_NOTIFY);
	hr_reg_write(&cq_db, DB_CQ_CI, hr_cq->cons_index);
	hr_reg_write(&cq_db, DB_CQ_CMD_SN, hr_cq->arm_sn);
	hr_reg_write(&cq_db, DB_CQ_NOTIFY, notify_flag);

	hns_roce_write64(hr_dev, (__le32 *)&cq_db, hr_cq->db_reg);

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

static void get_cqe_status(struct hns_roce_dev *hr_dev, struct hns_roce_qp *qp,
			   struct hns_roce_cq *cq, struct hns_roce_v2_cqe *cqe,
			   struct ib_wc *wc)
{
	static const struct {
		u32 cqe_status;
		enum ib_wc_status wc_status;
	} map[] = {
		{ HNS_ROCE_CQE_V2_SUCCESS, IB_WC_SUCCESS },
		{ HNS_ROCE_CQE_V2_LOCAL_LENGTH_ERR, IB_WC_LOC_LEN_ERR },
		{ HNS_ROCE_CQE_V2_LOCAL_QP_OP_ERR, IB_WC_LOC_QP_OP_ERR },
		{ HNS_ROCE_CQE_V2_LOCAL_PROT_ERR, IB_WC_LOC_PROT_ERR },
		{ HNS_ROCE_CQE_V2_WR_FLUSH_ERR, IB_WC_WR_FLUSH_ERR },
		{ HNS_ROCE_CQE_V2_MW_BIND_ERR, IB_WC_MW_BIND_ERR },
		{ HNS_ROCE_CQE_V2_BAD_RESP_ERR, IB_WC_BAD_RESP_ERR },
		{ HNS_ROCE_CQE_V2_LOCAL_ACCESS_ERR, IB_WC_LOC_ACCESS_ERR },
		{ HNS_ROCE_CQE_V2_REMOTE_INVAL_REQ_ERR, IB_WC_REM_INV_REQ_ERR },
		{ HNS_ROCE_CQE_V2_REMOTE_ACCESS_ERR, IB_WC_REM_ACCESS_ERR },
		{ HNS_ROCE_CQE_V2_REMOTE_OP_ERR, IB_WC_REM_OP_ERR },
		{ HNS_ROCE_CQE_V2_TRANSPORT_RETRY_EXC_ERR,
		  IB_WC_RETRY_EXC_ERR },
		{ HNS_ROCE_CQE_V2_RNR_RETRY_EXC_ERR, IB_WC_RNR_RETRY_EXC_ERR },
		{ HNS_ROCE_CQE_V2_REMOTE_ABORT_ERR, IB_WC_REM_ABORT_ERR },
		{ HNS_ROCE_CQE_V2_GENERAL_ERR, IB_WC_GENERAL_ERR}
	};

	u32 cqe_status = hr_reg_read(cqe, CQE_STATUS);
	int i;

	wc->status = IB_WC_GENERAL_ERR;
	for (i = 0; i < ARRAY_SIZE(map); i++)
		if (cqe_status == map[i].cqe_status) {
			wc->status = map[i].wc_status;
			break;
		}

	if (likely(wc->status == IB_WC_SUCCESS ||
		   wc->status == IB_WC_WR_FLUSH_ERR))
		return;

	ibdev_err(&hr_dev->ib_dev, "error cqe status 0x%x:\n", cqe_status);
	print_hex_dump(KERN_ERR, "", DUMP_PREFIX_NONE, 16, 4, cqe,
		       cq->cqe_size, false);
	wc->vendor_err = hr_reg_read(cqe, CQE_SUB_STATUS);

	/*
	 * For hns ROCEE, GENERAL_ERR is an error type that is not defined in
	 * the standard protocol, the driver must ignore it and needn't to set
	 * the QP to an error state.
	 */
	if (cqe_status == HNS_ROCE_CQE_V2_GENERAL_ERR)
		return;

	flush_cqe(hr_dev, qp);
}

static int get_cur_qp(struct hns_roce_cq *hr_cq, struct hns_roce_v2_cqe *cqe,
		      struct hns_roce_qp **cur_qp)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_cq->ib_cq.device);
	struct hns_roce_qp *hr_qp = *cur_qp;
	u32 qpn;

	qpn = hr_reg_read(cqe, CQE_LCL_QPN);

	if (!hr_qp || qpn != hr_qp->qpn) {
		hr_qp = __hns_roce_qp_lookup(hr_dev, qpn);
		if (unlikely(!hr_qp)) {
			ibdev_err(&hr_dev->ib_dev,
				  "CQ %06lx with entry for unknown QPN %06x\n",
				  hr_cq->cqn, qpn);
			return -EINVAL;
		}
		*cur_qp = hr_qp;
	}

	return 0;
}

/*
 * mapped-value = 1 + real-value
 * The ib wc opcode's real value is start from 0, In order to distinguish
 * between initialized and uninitialized map values, we plus 1 to the actual
 * value when defining the mapping, so that the validity can be identified by
 * checking whether the mapped value is greater than 0.
 */
#define HR_WC_OP_MAP(hr_key, ib_key) \
		[HNS_ROCE_V2_WQE_OP_ ## hr_key] = 1 + IB_WC_ ## ib_key

static const u32 wc_send_op_map[] = {
	HR_WC_OP_MAP(SEND,			SEND),
	HR_WC_OP_MAP(SEND_WITH_INV,		SEND),
	HR_WC_OP_MAP(SEND_WITH_IMM,		SEND),
	HR_WC_OP_MAP(RDMA_READ,			RDMA_READ),
	HR_WC_OP_MAP(RDMA_WRITE,		RDMA_WRITE),
	HR_WC_OP_MAP(RDMA_WRITE_WITH_IMM,	RDMA_WRITE),
	HR_WC_OP_MAP(ATOM_CMP_AND_SWAP,		COMP_SWAP),
	HR_WC_OP_MAP(ATOM_FETCH_AND_ADD,	FETCH_ADD),
	HR_WC_OP_MAP(ATOM_MSK_CMP_AND_SWAP,	MASKED_COMP_SWAP),
	HR_WC_OP_MAP(ATOM_MSK_FETCH_AND_ADD,	MASKED_FETCH_ADD),
	HR_WC_OP_MAP(FAST_REG_PMR,		REG_MR),
	HR_WC_OP_MAP(BIND_MW,			REG_MR),
};

static int to_ib_wc_send_op(u32 hr_opcode)
{
	if (hr_opcode >= ARRAY_SIZE(wc_send_op_map))
		return -EINVAL;

	return wc_send_op_map[hr_opcode] ? wc_send_op_map[hr_opcode] - 1 :
					   -EINVAL;
}

static const u32 wc_recv_op_map[] = {
	HR_WC_OP_MAP(RDMA_WRITE_WITH_IMM,		WITH_IMM),
	HR_WC_OP_MAP(SEND,				RECV),
	HR_WC_OP_MAP(SEND_WITH_IMM,			WITH_IMM),
	HR_WC_OP_MAP(SEND_WITH_INV,			RECV),
};

static int to_ib_wc_recv_op(u32 hr_opcode)
{
	if (hr_opcode >= ARRAY_SIZE(wc_recv_op_map))
		return -EINVAL;

	return wc_recv_op_map[hr_opcode] ? wc_recv_op_map[hr_opcode] - 1 :
					   -EINVAL;
}

static void fill_send_wc(struct ib_wc *wc, struct hns_roce_v2_cqe *cqe)
{
	u32 hr_opcode;
	int ib_opcode;

	wc->wc_flags = 0;

	hr_opcode = hr_reg_read(cqe, CQE_OPCODE);
	switch (hr_opcode) {
	case HNS_ROCE_V2_WQE_OP_RDMA_READ:
		wc->byte_len = le32_to_cpu(cqe->byte_cnt);
		break;
	case HNS_ROCE_V2_WQE_OP_SEND_WITH_IMM:
	case HNS_ROCE_V2_WQE_OP_RDMA_WRITE_WITH_IMM:
		wc->wc_flags |= IB_WC_WITH_IMM;
		break;
	case HNS_ROCE_V2_WQE_OP_ATOM_CMP_AND_SWAP:
	case HNS_ROCE_V2_WQE_OP_ATOM_FETCH_AND_ADD:
	case HNS_ROCE_V2_WQE_OP_ATOM_MSK_CMP_AND_SWAP:
	case HNS_ROCE_V2_WQE_OP_ATOM_MSK_FETCH_AND_ADD:
		wc->byte_len  = 8;
		break;
	default:
		break;
	}

	ib_opcode = to_ib_wc_send_op(hr_opcode);
	if (ib_opcode < 0)
		wc->status = IB_WC_GENERAL_ERR;
	else
		wc->opcode = ib_opcode;
}

static int fill_recv_wc(struct ib_wc *wc, struct hns_roce_v2_cqe *cqe)
{
	u32 hr_opcode;
	int ib_opcode;

	wc->byte_len = le32_to_cpu(cqe->byte_cnt);

	hr_opcode = hr_reg_read(cqe, CQE_OPCODE);
	switch (hr_opcode) {
	case HNS_ROCE_V2_OPCODE_RDMA_WRITE_IMM:
	case HNS_ROCE_V2_OPCODE_SEND_WITH_IMM:
		wc->wc_flags = IB_WC_WITH_IMM;
		wc->ex.imm_data = cpu_to_be32(le32_to_cpu(cqe->immtdata));
		break;
	case HNS_ROCE_V2_OPCODE_SEND_WITH_INV:
		wc->wc_flags = IB_WC_WITH_INVALIDATE;
		wc->ex.invalidate_rkey = le32_to_cpu(cqe->rkey);
		break;
	default:
		wc->wc_flags = 0;
	}

	ib_opcode = to_ib_wc_recv_op(hr_opcode);
	if (ib_opcode < 0)
		wc->status = IB_WC_GENERAL_ERR;
	else
		wc->opcode = ib_opcode;

	wc->sl = hr_reg_read(cqe, CQE_SL);
	wc->src_qp = hr_reg_read(cqe, CQE_RMT_QPN);
	wc->slid = 0;
	wc->wc_flags |= hr_reg_read(cqe, CQE_GRH) ? IB_WC_GRH : 0;
	wc->port_num = hr_reg_read(cqe, CQE_PORTN);
	wc->pkey_index = 0;

	if (hr_reg_read(cqe, CQE_VID_VLD)) {
		wc->vlan_id = hr_reg_read(cqe, CQE_VID);
		wc->wc_flags |= IB_WC_WITH_VLAN;
	} else {
		wc->vlan_id = 0xffff;
	}

	wc->network_hdr_type = hr_reg_read(cqe, CQE_PORT_TYPE);

	return 0;
}

static int hns_roce_v2_poll_one(struct hns_roce_cq *hr_cq,
				struct hns_roce_qp **cur_qp, struct ib_wc *wc)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(hr_cq->ib_cq.device);
	struct hns_roce_qp *qp = *cur_qp;
	struct hns_roce_srq *srq = NULL;
	struct hns_roce_v2_cqe *cqe;
	struct hns_roce_wq *wq;
	int is_send;
	u16 wqe_idx;
	int ret;

	cqe = get_sw_cqe_v2(hr_cq, hr_cq->cons_index);
	if (!cqe)
		return -EAGAIN;

	++hr_cq->cons_index;
	/* Memory barrier */
	rmb();

	ret = get_cur_qp(hr_cq, cqe, &qp);
	if (ret)
		return ret;

	wc->qp = &qp->ibqp;
	wc->vendor_err = 0;

	wqe_idx = hr_reg_read(cqe, CQE_WQE_IDX);

	is_send = !hr_reg_read(cqe, CQE_S_R);
	if (is_send) {
		wq = &qp->sq;

		/* If sg_signal_bit is set, tail pointer will be updated to
		 * the WQE corresponding to the current CQE.
		 */
		if (qp->sq_signal_bits)
			wq->tail += (wqe_idx - (u16)wq->tail) &
				    (wq->wqe_cnt - 1);

		wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
		++wq->tail;

		fill_send_wc(wc, cqe);
	} else {
		if (qp->ibqp.srq) {
			srq = to_hr_srq(qp->ibqp.srq);
			wc->wr_id = srq->wrid[wqe_idx];
			hns_roce_free_srq_wqe(srq, wqe_idx);
		} else {
			wq = &qp->rq;
			wc->wr_id = wq->wrid[wq->tail & (wq->wqe_cnt - 1)];
			++wq->tail;
		}

		ret = fill_recv_wc(wc, cqe);
	}

	get_cqe_status(hr_dev, qp, hr_cq, cqe, wc);
	if (unlikely(wc->status != IB_WC_SUCCESS))
		return 0;

	return ret;
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

	if (npolled)
		update_cq_db(hr_dev, hr_cq);

out:
	spin_unlock_irqrestore(&hr_cq->lock, flags);

	return npolled;
}

static int get_op_for_set_hem(struct hns_roce_dev *hr_dev, u32 type,
			      u32 step_idx, u8 *mbox_cmd)
{
	u8 cmd;

	switch (type) {
	case HEM_TYPE_QPC:
		cmd = HNS_ROCE_CMD_WRITE_QPC_BT0;
		break;
	case HEM_TYPE_MTPT:
		cmd = HNS_ROCE_CMD_WRITE_MPT_BT0;
		break;
	case HEM_TYPE_CQC:
		cmd = HNS_ROCE_CMD_WRITE_CQC_BT0;
		break;
	case HEM_TYPE_SRQC:
		cmd = HNS_ROCE_CMD_WRITE_SRQC_BT0;
		break;
	case HEM_TYPE_SCCC:
		cmd = HNS_ROCE_CMD_WRITE_SCCC_BT0;
		break;
	case HEM_TYPE_QPC_TIMER:
		cmd = HNS_ROCE_CMD_WRITE_QPC_TIMER_BT0;
		break;
	case HEM_TYPE_CQC_TIMER:
		cmd = HNS_ROCE_CMD_WRITE_CQC_TIMER_BT0;
		break;
	default:
		dev_warn(hr_dev->dev, "failed to check hem type %u.\n", type);
		return -EINVAL;
	}

	*mbox_cmd = cmd + step_idx;

	return 0;
}

static int config_gmv_ba_to_hw(struct hns_roce_dev *hr_dev, unsigned long obj,
			       dma_addr_t base_addr)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	u32 idx = obj / (HNS_HW_PAGE_SIZE / hr_dev->caps.gmv_entry_sz);
	u64 addr = to_hr_hw_page_addr(base_addr);

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_GMV_BT, false);

	hr_reg_write(req, CFG_GMV_BT_BA_L, lower_32_bits(addr));
	hr_reg_write(req, CFG_GMV_BT_BA_H, upper_32_bits(addr));
	hr_reg_write(req, CFG_GMV_BT_IDX, idx);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static int set_hem_to_hw(struct hns_roce_dev *hr_dev, int obj,
			 dma_addr_t base_addr, u32 hem_type, u32 step_idx)
{
	int ret;
	u8 cmd;

	if (unlikely(hem_type == HEM_TYPE_GMV))
		return config_gmv_ba_to_hw(hr_dev, obj, base_addr);

	if (unlikely(hem_type == HEM_TYPE_SCCC && step_idx))
		return 0;

	ret = get_op_for_set_hem(hr_dev, hem_type, step_idx, &cmd);
	if (ret < 0)
		return ret;

	return config_hem_ba_to_hw(hr_dev, base_addr, cmd, obj);
}

static int hns_roce_v2_set_hem(struct hns_roce_dev *hr_dev,
			       struct hns_roce_hem_table *table, int obj,
			       u32 step_idx)
{
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

	if (table->type == HEM_TYPE_SCCC)
		obj = mhop.l0_idx;

	if (check_whether_last_step(hop_num, step_idx)) {
		hem = table->hem[hem_idx];
		for (hns_roce_hem_first(hem, &iter);
		     !hns_roce_hem_last(&iter); hns_roce_hem_next(&iter)) {
			bt_ba = hns_roce_hem_addr(&iter);
			ret = set_hem_to_hw(hr_dev, obj, bt_ba, table->type,
					    step_idx);
		}
	} else {
		if (step_idx == 0)
			bt_ba = table->bt_l0_dma_addr[i];
		else if (step_idx == 1 && hop_num == 2)
			bt_ba = table->bt_l1_dma_addr[l1_idx];

		ret = set_hem_to_hw(hr_dev, obj, bt_ba, table->type, step_idx);
	}

	return ret;
}

static int hns_roce_v2_clear_hem(struct hns_roce_dev *hr_dev,
				 struct hns_roce_hem_table *table,
				 int tag, u32 step_idx)
{
	struct hns_roce_cmd_mailbox *mailbox;
	struct device *dev = hr_dev->dev;
	u8 cmd = 0xff;
	int ret;

	if (!hns_roce_check_whether_mhop(hr_dev, table->type))
		return 0;

	switch (table->type) {
	case HEM_TYPE_QPC:
		cmd = HNS_ROCE_CMD_DESTROY_QPC_BT0;
		break;
	case HEM_TYPE_MTPT:
		cmd = HNS_ROCE_CMD_DESTROY_MPT_BT0;
		break;
	case HEM_TYPE_CQC:
		cmd = HNS_ROCE_CMD_DESTROY_CQC_BT0;
		break;
	case HEM_TYPE_SRQC:
		cmd = HNS_ROCE_CMD_DESTROY_SRQC_BT0;
		break;
	case HEM_TYPE_SCCC:
	case HEM_TYPE_QPC_TIMER:
	case HEM_TYPE_CQC_TIMER:
	case HEM_TYPE_GMV:
		return 0;
	default:
		dev_warn(dev, "table %u not to be destroyed by mailbox!\n",
			 table->type);
		return 0;
	}

	cmd += step_idx;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, cmd, tag);

	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v2_qp_modify(struct hns_roce_dev *hr_dev,
				 struct hns_roce_v2_qp_context *context,
				 struct hns_roce_v2_qp_context *qpc_mask,
				 struct hns_roce_qp *hr_qp)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int qpc_size;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	/* The qpc size of HIP08 is only 256B, which is half of HIP09 */
	qpc_size = hr_dev->caps.qpc_sz;
	memcpy(mailbox->buf, context, qpc_size);
	memcpy(mailbox->buf + qpc_size, qpc_mask, qpc_size);

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0,
				HNS_ROCE_CMD_MODIFY_QPC, hr_qp->qpn);

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

	hr_reg_write_bool(context, QPC_RRE,
			  access_flags & IB_ACCESS_REMOTE_READ);
	hr_reg_clear(qpc_mask, QPC_RRE);

	hr_reg_write_bool(context, QPC_RWE,
			  access_flags & IB_ACCESS_REMOTE_WRITE);
	hr_reg_clear(qpc_mask, QPC_RWE);

	hr_reg_write_bool(context, QPC_ATE,
			  access_flags & IB_ACCESS_REMOTE_ATOMIC);
	hr_reg_clear(qpc_mask, QPC_ATE);
	hr_reg_write_bool(context, QPC_EXT_ATE,
			  access_flags & IB_ACCESS_REMOTE_ATOMIC);
	hr_reg_clear(qpc_mask, QPC_EXT_ATE);
}

static void set_qpc_wqe_cnt(struct hns_roce_qp *hr_qp,
			    struct hns_roce_v2_qp_context *context,
			    struct hns_roce_v2_qp_context *qpc_mask)
{
	hr_reg_write(context, QPC_SGE_SHIFT,
		     to_hr_hem_entries_shift(hr_qp->sge.sge_cnt,
					     hr_qp->sge.sge_shift));

	hr_reg_write(context, QPC_SQ_SHIFT, ilog2(hr_qp->sq.wqe_cnt));

	hr_reg_write(context, QPC_RQ_SHIFT, ilog2(hr_qp->rq.wqe_cnt));
}

static inline int get_cqn(struct ib_cq *ib_cq)
{
	return ib_cq ? to_hr_cq(ib_cq)->cqn : 0;
}

static inline int get_pdn(struct ib_pd *ib_pd)
{
	return ib_pd ? to_hr_pd(ib_pd)->pdn : 0;
}

static void modify_qp_reset_to_init(struct ib_qp *ibqp,
				    const struct ib_qp_attr *attr,
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
	hr_reg_write(context, QPC_TST, to_hr_qp_type(ibqp->qp_type));

	hr_reg_write(context, QPC_PD, get_pdn(ibqp->pd));

	hr_reg_write(context, QPC_RQWS, ilog2(hr_qp->rq.max_gs));

	set_qpc_wqe_cnt(hr_qp, context, qpc_mask);

	/* No VLAN need to set 0xFFF */
	hr_reg_write(context, QPC_VLAN_ID, 0xfff);

	if (ibqp->qp_type == IB_QPT_XRC_TGT) {
		context->qkey_xrcd = cpu_to_le32(hr_qp->xrcdn);

		hr_reg_enable(context, QPC_XRC_QP_TYPE);
	}

	if (hr_qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)
		hr_reg_enable(context, QPC_RQ_RECORD_EN);

	if (hr_qp->en_flags & HNS_ROCE_QP_CAP_OWNER_DB)
		hr_reg_enable(context, QPC_OWNER_MODE);

	hr_reg_write(context, QPC_RQ_DB_RECORD_ADDR_L,
		     lower_32_bits(hr_qp->rdb.dma) >> 1);
	hr_reg_write(context, QPC_RQ_DB_RECORD_ADDR_H,
		     upper_32_bits(hr_qp->rdb.dma));

	hr_reg_write(context, QPC_RX_CQN, get_cqn(ibqp->recv_cq));

	if (ibqp->srq) {
		hr_reg_enable(context, QPC_SRQ_EN);
		hr_reg_write(context, QPC_SRQN, to_hr_srq(ibqp->srq)->srqn);
	}

	hr_reg_enable(context, QPC_FRE);

	hr_reg_write(context, QPC_TX_CQN, get_cqn(ibqp->send_cq));

	if (hr_dev->caps.qpc_sz < HNS_ROCE_V3_QPC_SZ)
		return;

	if (hr_dev->caps.flags & HNS_ROCE_CAP_FLAG_STASH)
		hr_reg_enable(&context->ext, QPCEX_STASH);
}

static void modify_qp_init_to_init(struct ib_qp *ibqp,
				   const struct ib_qp_attr *attr,
				   struct hns_roce_v2_qp_context *context,
				   struct hns_roce_v2_qp_context *qpc_mask)
{
	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	hr_reg_write(context, QPC_TST, to_hr_qp_type(ibqp->qp_type));
	hr_reg_clear(qpc_mask, QPC_TST);

	hr_reg_write(context, QPC_PD, get_pdn(ibqp->pd));
	hr_reg_clear(qpc_mask, QPC_PD);

	hr_reg_write(context, QPC_RX_CQN, get_cqn(ibqp->recv_cq));
	hr_reg_clear(qpc_mask, QPC_RX_CQN);

	hr_reg_write(context, QPC_TX_CQN, get_cqn(ibqp->send_cq));
	hr_reg_clear(qpc_mask, QPC_TX_CQN);

	if (ibqp->srq) {
		hr_reg_enable(context, QPC_SRQ_EN);
		hr_reg_clear(qpc_mask, QPC_SRQ_EN);
		hr_reg_write(context, QPC_SRQN, to_hr_srq(ibqp->srq)->srqn);
		hr_reg_clear(qpc_mask, QPC_SRQN);
	}
}

static int config_qp_rq_buf(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp,
			    struct hns_roce_v2_qp_context *context,
			    struct hns_roce_v2_qp_context *qpc_mask)
{
	u64 mtts[MTT_MIN_COUNT] = { 0 };
	u64 wqe_sge_ba;
	int count;

	/* Search qp buf's mtts */
	count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr, hr_qp->rq.offset, mtts,
				  MTT_MIN_COUNT, &wqe_sge_ba);
	if (hr_qp->rq.wqe_cnt && count < 1) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to find RQ WQE, QPN = 0x%lx.\n", hr_qp->qpn);
		return -EINVAL;
	}

	context->wqe_sge_ba = cpu_to_le32(wqe_sge_ba >> 3);
	qpc_mask->wqe_sge_ba = 0;

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	hr_reg_write(context, QPC_WQE_SGE_BA_H, wqe_sge_ba >> (32 + 3));
	hr_reg_clear(qpc_mask, QPC_WQE_SGE_BA_H);

	hr_reg_write(context, QPC_SQ_HOP_NUM,
		     to_hr_hem_hopnum(hr_dev->caps.wqe_sq_hop_num,
				      hr_qp->sq.wqe_cnt));
	hr_reg_clear(qpc_mask, QPC_SQ_HOP_NUM);

	hr_reg_write(context, QPC_SGE_HOP_NUM,
		     to_hr_hem_hopnum(hr_dev->caps.wqe_sge_hop_num,
				      hr_qp->sge.sge_cnt));
	hr_reg_clear(qpc_mask, QPC_SGE_HOP_NUM);

	hr_reg_write(context, QPC_RQ_HOP_NUM,
		     to_hr_hem_hopnum(hr_dev->caps.wqe_rq_hop_num,
				      hr_qp->rq.wqe_cnt));

	hr_reg_clear(qpc_mask, QPC_RQ_HOP_NUM);

	hr_reg_write(context, QPC_WQE_SGE_BA_PG_SZ,
		     to_hr_hw_page_shift(hr_qp->mtr.hem_cfg.ba_pg_shift));
	hr_reg_clear(qpc_mask, QPC_WQE_SGE_BA_PG_SZ);

	hr_reg_write(context, QPC_WQE_SGE_BUF_PG_SZ,
		     to_hr_hw_page_shift(hr_qp->mtr.hem_cfg.buf_pg_shift));
	hr_reg_clear(qpc_mask, QPC_WQE_SGE_BUF_PG_SZ);

	context->rq_cur_blk_addr = cpu_to_le32(to_hr_hw_page_addr(mtts[0]));
	qpc_mask->rq_cur_blk_addr = 0;

	hr_reg_write(context, QPC_RQ_CUR_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts[0])));
	hr_reg_clear(qpc_mask, QPC_RQ_CUR_BLK_ADDR_H);

	context->rq_nxt_blk_addr = cpu_to_le32(to_hr_hw_page_addr(mtts[1]));
	qpc_mask->rq_nxt_blk_addr = 0;

	hr_reg_write(context, QPC_RQ_NXT_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts[1])));
	hr_reg_clear(qpc_mask, QPC_RQ_NXT_BLK_ADDR_H);

	return 0;
}

static int config_qp_sq_buf(struct hns_roce_dev *hr_dev,
			    struct hns_roce_qp *hr_qp,
			    struct hns_roce_v2_qp_context *context,
			    struct hns_roce_v2_qp_context *qpc_mask)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	u64 sge_cur_blk = 0;
	u64 sq_cur_blk = 0;
	int count;

	/* search qp buf's mtts */
	count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr, 0, &sq_cur_blk, 1, NULL);
	if (count < 1) {
		ibdev_err(ibdev, "failed to find QP(0x%lx) SQ buf.\n",
			  hr_qp->qpn);
		return -EINVAL;
	}
	if (hr_qp->sge.sge_cnt > 0) {
		count = hns_roce_mtr_find(hr_dev, &hr_qp->mtr,
					  hr_qp->sge.offset,
					  &sge_cur_blk, 1, NULL);
		if (count < 1) {
			ibdev_err(ibdev, "failed to find QP(0x%lx) SGE buf.\n",
				  hr_qp->qpn);
			return -EINVAL;
		}
	}

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	hr_reg_write(context, QPC_SQ_CUR_BLK_ADDR_L,
		     lower_32_bits(to_hr_hw_page_addr(sq_cur_blk)));
	hr_reg_write(context, QPC_SQ_CUR_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(sq_cur_blk)));
	hr_reg_clear(qpc_mask, QPC_SQ_CUR_BLK_ADDR_L);
	hr_reg_clear(qpc_mask, QPC_SQ_CUR_BLK_ADDR_H);

	hr_reg_write(context, QPC_SQ_CUR_SGE_BLK_ADDR_L,
		     lower_32_bits(to_hr_hw_page_addr(sge_cur_blk)));
	hr_reg_write(context, QPC_SQ_CUR_SGE_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(sge_cur_blk)));
	hr_reg_clear(qpc_mask, QPC_SQ_CUR_SGE_BLK_ADDR_L);
	hr_reg_clear(qpc_mask, QPC_SQ_CUR_SGE_BLK_ADDR_H);

	hr_reg_write(context, QPC_RX_SQ_CUR_BLK_ADDR_L,
		     lower_32_bits(to_hr_hw_page_addr(sq_cur_blk)));
	hr_reg_write(context, QPC_RX_SQ_CUR_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(sq_cur_blk)));
	hr_reg_clear(qpc_mask, QPC_RX_SQ_CUR_BLK_ADDR_L);
	hr_reg_clear(qpc_mask, QPC_RX_SQ_CUR_BLK_ADDR_H);

	return 0;
}

static inline enum ib_mtu get_mtu(struct ib_qp *ibqp,
				  const struct ib_qp_attr *attr)
{
	if (ibqp->qp_type == IB_QPT_GSI || ibqp->qp_type == IB_QPT_UD)
		return IB_MTU_4096;

	return attr->path_mtu;
}

static int modify_qp_init_to_rtr(struct ib_qp *ibqp,
				 const struct ib_qp_attr *attr, int attr_mask,
				 struct hns_roce_v2_qp_context *context,
				 struct hns_roce_v2_qp_context *qpc_mask,
				 struct ib_udata *udata)
{
	struct hns_roce_ucontext *uctx = rdma_udata_to_drv_context(udata,
					  struct hns_roce_ucontext, ibucontext);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct ib_device *ibdev = &hr_dev->ib_dev;
	dma_addr_t trrl_ba;
	dma_addr_t irrl_ba;
	enum ib_mtu ib_mtu;
	const u8 *smac;
	u8 lp_pktn_ini;
	u64 *mtts;
	u8 *dmac;
	u32 port;
	int mtu;
	int ret;

	ret = config_qp_rq_buf(hr_dev, hr_qp, context, qpc_mask);
	if (ret) {
		ibdev_err(ibdev, "failed to config rq buf, ret = %d.\n", ret);
		return ret;
	}

	/* Search IRRL's mtts */
	mtts = hns_roce_table_find(hr_dev, &hr_dev->qp_table.irrl_table,
				   hr_qp->qpn, &irrl_ba);
	if (!mtts) {
		ibdev_err(ibdev, "failed to find qp irrl_table.\n");
		return -EINVAL;
	}

	/* Search TRRL's mtts */
	mtts = hns_roce_table_find(hr_dev, &hr_dev->qp_table.trrl_table,
				   hr_qp->qpn, &trrl_ba);
	if (!mtts) {
		ibdev_err(ibdev, "failed to find qp trrl_table.\n");
		return -EINVAL;
	}

	if (attr_mask & IB_QP_ALT_PATH) {
		ibdev_err(ibdev, "INIT2RTR attr_mask (0x%x) error.\n",
			  attr_mask);
		return -EINVAL;
	}

	hr_reg_write(context, QPC_TRRL_BA_L, trrl_ba >> 4);
	hr_reg_clear(qpc_mask, QPC_TRRL_BA_L);
	context->trrl_ba = cpu_to_le32(trrl_ba >> (16 + 4));
	qpc_mask->trrl_ba = 0;
	hr_reg_write(context, QPC_TRRL_BA_H, trrl_ba >> (32 + 16 + 4));
	hr_reg_clear(qpc_mask, QPC_TRRL_BA_H);

	context->irrl_ba = cpu_to_le32(irrl_ba >> 6);
	qpc_mask->irrl_ba = 0;
	hr_reg_write(context, QPC_IRRL_BA_H, irrl_ba >> (32 + 6));
	hr_reg_clear(qpc_mask, QPC_IRRL_BA_H);

	hr_reg_enable(context, QPC_RMT_E2E);
	hr_reg_clear(qpc_mask, QPC_RMT_E2E);

	hr_reg_write(context, QPC_SIG_TYPE, hr_qp->sq_signal_bits);
	hr_reg_clear(qpc_mask, QPC_SIG_TYPE);

	port = (attr_mask & IB_QP_PORT) ? (attr->port_num - 1) : hr_qp->port;

	smac = (const u8 *)hr_dev->dev_addr[port];
	dmac = (u8 *)attr->ah_attr.roce.dmac;
	/* when dmac equals smac or loop_idc is 1, it should loopback */
	if (ether_addr_equal_unaligned(dmac, smac) ||
	    hr_dev->loop_idc == 0x1) {
		hr_reg_write(context, QPC_LBI, hr_dev->loop_idc);
		hr_reg_clear(qpc_mask, QPC_LBI);
	}

	if (attr_mask & IB_QP_DEST_QPN) {
		hr_reg_write(context, QPC_DQPN, attr->dest_qp_num);
		hr_reg_clear(qpc_mask, QPC_DQPN);
	}

	memcpy(&context->dmac, dmac, sizeof(u32));
	hr_reg_write(context, QPC_DMAC_H, *((u16 *)(&dmac[4])));
	qpc_mask->dmac = 0;
	hr_reg_clear(qpc_mask, QPC_DMAC_H);

	ib_mtu = get_mtu(ibqp, attr);
	hr_qp->path_mtu = ib_mtu;

	mtu = ib_mtu_enum_to_int(ib_mtu);
	if (WARN_ON(mtu <= 0))
		return -EINVAL;
#define MIN_LP_MSG_LEN 1024
	/* mtu * (2 ^ lp_pktn_ini) should be in the range of 1024 to mtu */
	lp_pktn_ini = ilog2(max(mtu, MIN_LP_MSG_LEN) / mtu);

	if (attr_mask & IB_QP_PATH_MTU) {
		hr_reg_write(context, QPC_MTU, ib_mtu);
		hr_reg_clear(qpc_mask, QPC_MTU);
	}

	hr_reg_write(context, QPC_LP_PKTN_INI, lp_pktn_ini);
	hr_reg_clear(qpc_mask, QPC_LP_PKTN_INI);

	/* ACK_REQ_FREQ should be larger than or equal to LP_PKTN_INI */
	hr_reg_write(context, QPC_ACK_REQ_FREQ, lp_pktn_ini);
	hr_reg_clear(qpc_mask, QPC_ACK_REQ_FREQ);

	hr_reg_clear(qpc_mask, QPC_RX_REQ_PSN_ERR);
	hr_reg_clear(qpc_mask, QPC_RX_REQ_MSN);
	hr_reg_clear(qpc_mask, QPC_RX_REQ_LAST_OPTYPE);

	context->rq_rnr_timer = 0;
	qpc_mask->rq_rnr_timer = 0;

	hr_reg_clear(qpc_mask, QPC_TRRL_HEAD_MAX);
	hr_reg_clear(qpc_mask, QPC_TRRL_TAIL_MAX);

	/* rocee send 2^lp_sgen_ini segs every time */
	hr_reg_write(context, QPC_LP_SGEN_INI, 3);
	hr_reg_clear(qpc_mask, QPC_LP_SGEN_INI);

	if (udata && ibqp->qp_type == IB_QPT_RC &&
	    (uctx->config & HNS_ROCE_RQ_INLINE_FLAGS)) {
		hr_reg_write_bool(context, QPC_RQIE,
				  hr_dev->caps.flags &
				  HNS_ROCE_CAP_FLAG_RQ_INLINE);
		hr_reg_clear(qpc_mask, QPC_RQIE);
	}

	if (udata &&
	    (ibqp->qp_type == IB_QPT_RC || ibqp->qp_type == IB_QPT_XRC_TGT) &&
	    (uctx->config & HNS_ROCE_CQE_INLINE_FLAGS)) {
		hr_reg_write_bool(context, QPC_CQEIE,
				  hr_dev->caps.flags &
				  HNS_ROCE_CAP_FLAG_CQE_INLINE);
		hr_reg_clear(qpc_mask, QPC_CQEIE);

		hr_reg_write(context, QPC_CQEIS, 0);
		hr_reg_clear(qpc_mask, QPC_CQEIS);
	}

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
	int ret;

	/* Not support alternate path and path migration */
	if (attr_mask & (IB_QP_ALT_PATH | IB_QP_PATH_MIG_STATE)) {
		ibdev_err(ibdev, "RTR2RTS attr_mask (0x%x)error\n", attr_mask);
		return -EINVAL;
	}

	ret = config_qp_sq_buf(hr_dev, hr_qp, context, qpc_mask);
	if (ret) {
		ibdev_err(ibdev, "failed to config sq buf, ret = %d.\n", ret);
		return ret;
	}

	/*
	 * Set some fields in context to zero, Because the default values
	 * of all fields in context are zero, we need not set them to 0 again.
	 * but we should set the relevant fields of context mask to 0.
	 */
	hr_reg_clear(qpc_mask, QPC_IRRL_SGE_IDX);

	hr_reg_clear(qpc_mask, QPC_RX_ACK_MSN);

	hr_reg_clear(qpc_mask, QPC_ACK_LAST_OPTYPE);
	hr_reg_clear(qpc_mask, QPC_IRRL_PSN_VLD);
	hr_reg_clear(qpc_mask, QPC_IRRL_PSN);

	hr_reg_clear(qpc_mask, QPC_IRRL_TAIL_REAL);

	hr_reg_clear(qpc_mask, QPC_RETRY_MSG_MSN);

	hr_reg_clear(qpc_mask, QPC_RNR_RETRY_FLAG);

	hr_reg_clear(qpc_mask, QPC_CHECK_FLG);

	hr_reg_clear(qpc_mask, QPC_V2_IRRL_HEAD);

	return 0;
}

static int get_dip_ctx_idx(struct ib_qp *ibqp, const struct ib_qp_attr *attr,
			   u32 *dip_idx)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	u32 *spare_idx = hr_dev->qp_table.idx_table.spare_idx;
	u32 *head =  &hr_dev->qp_table.idx_table.head;
	u32 *tail =  &hr_dev->qp_table.idx_table.tail;
	struct hns_roce_dip *hr_dip;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&hr_dev->dip_list_lock, flags);

	spare_idx[*tail] = ibqp->qp_num;
	*tail = (*tail == hr_dev->caps.num_qps - 1) ? 0 : (*tail + 1);

	list_for_each_entry(hr_dip, &hr_dev->dip_list, node) {
		if (!memcmp(grh->dgid.raw, hr_dip->dgid, 16)) {
			*dip_idx = hr_dip->dip_idx;
			goto out;
		}
	}

	/* If no dgid is found, a new dip and a mapping between dgid and
	 * dip_idx will be created.
	 */
	hr_dip = kzalloc(sizeof(*hr_dip), GFP_ATOMIC);
	if (!hr_dip) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(hr_dip->dgid, grh->dgid.raw, sizeof(grh->dgid.raw));
	hr_dip->dip_idx = *dip_idx = spare_idx[*head];
	*head = (*head == hr_dev->caps.num_qps - 1) ? 0 : (*head + 1);
	list_add_tail(&hr_dip->node, &hr_dev->dip_list);

out:
	spin_unlock_irqrestore(&hr_dev->dip_list_lock, flags);
	return ret;
}

enum {
	CONG_DCQCN,
	CONG_WINDOW,
};

enum {
	UNSUPPORT_CONG_LEVEL,
	SUPPORT_CONG_LEVEL,
};

enum {
	CONG_LDCP,
	CONG_HC3,
};

enum {
	DIP_INVALID,
	DIP_VALID,
};

enum {
	WND_LIMIT,
	WND_UNLIMIT,
};

static int check_cong_type(struct ib_qp *ibqp,
			   struct hns_roce_congestion_algorithm *cong_alg)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);

	if (ibqp->qp_type == IB_QPT_UD)
		hr_dev->caps.cong_type = CONG_TYPE_DCQCN;

	/* different congestion types match different configurations */
	switch (hr_dev->caps.cong_type) {
	case CONG_TYPE_DCQCN:
		cong_alg->alg_sel = CONG_DCQCN;
		cong_alg->alg_sub_sel = UNSUPPORT_CONG_LEVEL;
		cong_alg->dip_vld = DIP_INVALID;
		cong_alg->wnd_mode_sel = WND_LIMIT;
		break;
	case CONG_TYPE_LDCP:
		cong_alg->alg_sel = CONG_WINDOW;
		cong_alg->alg_sub_sel = CONG_LDCP;
		cong_alg->dip_vld = DIP_INVALID;
		cong_alg->wnd_mode_sel = WND_UNLIMIT;
		break;
	case CONG_TYPE_HC3:
		cong_alg->alg_sel = CONG_WINDOW;
		cong_alg->alg_sub_sel = CONG_HC3;
		cong_alg->dip_vld = DIP_INVALID;
		cong_alg->wnd_mode_sel = WND_LIMIT;
		break;
	case CONG_TYPE_DIP:
		cong_alg->alg_sel = CONG_DCQCN;
		cong_alg->alg_sub_sel = UNSUPPORT_CONG_LEVEL;
		cong_alg->dip_vld = DIP_VALID;
		cong_alg->wnd_mode_sel = WND_LIMIT;
		break;
	default:
		ibdev_warn(&hr_dev->ib_dev,
			   "invalid type(%u) for congestion selection.\n",
			   hr_dev->caps.cong_type);
		hr_dev->caps.cong_type = CONG_TYPE_DCQCN;
		cong_alg->alg_sel = CONG_DCQCN;
		cong_alg->alg_sub_sel = UNSUPPORT_CONG_LEVEL;
		cong_alg->dip_vld = DIP_INVALID;
		cong_alg->wnd_mode_sel = WND_LIMIT;
		break;
	}

	return 0;
}

static int fill_cong_field(struct ib_qp *ibqp, const struct ib_qp_attr *attr,
			   struct hns_roce_v2_qp_context *context,
			   struct hns_roce_v2_qp_context *qpc_mask)
{
	const struct ib_global_route *grh = rdma_ah_read_grh(&attr->ah_attr);
	struct hns_roce_congestion_algorithm cong_field;
	struct ib_device *ibdev = ibqp->device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibdev);
	u32 dip_idx = 0;
	int ret;

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08 ||
	    grh->sgid_attr->gid_type == IB_GID_TYPE_ROCE)
		return 0;

	ret = check_cong_type(ibqp, &cong_field);
	if (ret)
		return ret;

	hr_reg_write(context, QPC_CONG_ALGO_TMPL_ID, hr_dev->cong_algo_tmpl_id +
		     hr_dev->caps.cong_type * HNS_ROCE_CONG_SIZE);
	hr_reg_clear(qpc_mask, QPC_CONG_ALGO_TMPL_ID);
	hr_reg_write(&context->ext, QPCEX_CONG_ALG_SEL, cong_field.alg_sel);
	hr_reg_clear(&qpc_mask->ext, QPCEX_CONG_ALG_SEL);
	hr_reg_write(&context->ext, QPCEX_CONG_ALG_SUB_SEL,
		     cong_field.alg_sub_sel);
	hr_reg_clear(&qpc_mask->ext, QPCEX_CONG_ALG_SUB_SEL);
	hr_reg_write(&context->ext, QPCEX_DIP_CTX_IDX_VLD, cong_field.dip_vld);
	hr_reg_clear(&qpc_mask->ext, QPCEX_DIP_CTX_IDX_VLD);
	hr_reg_write(&context->ext, QPCEX_SQ_RQ_NOT_FORBID_EN,
		     cong_field.wnd_mode_sel);
	hr_reg_clear(&qpc_mask->ext, QPCEX_SQ_RQ_NOT_FORBID_EN);

	/* if dip is disabled, there is no need to set dip idx */
	if (cong_field.dip_vld == 0)
		return 0;

	ret = get_dip_ctx_idx(ibqp, attr, &dip_idx);
	if (ret) {
		ibdev_err(ibdev, "failed to fill cong field, ret = %d.\n", ret);
		return ret;
	}

	hr_reg_write(&context->ext, QPCEX_DIP_CTX_IDX, dip_idx);
	hr_reg_write(&qpc_mask->ext, QPCEX_DIP_CTX_IDX, 0);

	return 0;
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
	u8 sl = rdma_ah_get_sl(&attr->ah_attr);
	int is_roce_protocol;
	u16 vlan_id = 0xffff;
	bool is_udp = false;
	u32 max_sl;
	u8 ib_port;
	u8 hr_port;
	int ret;

	max_sl = min_t(u32, MAX_SERVICE_LEVEL, hr_dev->caps.sl_num - 1);
	if (unlikely(sl > max_sl)) {
		ibdev_err_ratelimited(ibdev,
				      "failed to fill QPC, sl (%u) shouldn't be larger than %u.\n",
				      sl, max_sl);
		return -EINVAL;
	}

	/*
	 * If free_mr_en of qp is set, it means that this qp comes from
	 * free mr. This qp will perform the loopback operation.
	 * In the loopback scenario, only sl needs to be set.
	 */
	if (hr_qp->free_mr_en) {
		hr_reg_write(context, QPC_SL, sl);
		hr_reg_clear(qpc_mask, QPC_SL);
		hr_qp->sl = sl;
		return 0;
	}

	ib_port = (attr_mask & IB_QP_PORT) ? attr->port_num : hr_qp->port + 1;
	hr_port = ib_port - 1;
	is_roce_protocol = rdma_cap_eth_ah(&hr_dev->ib_dev, ib_port) &&
			   rdma_ah_get_ah_flags(&attr->ah_attr) & IB_AH_GRH;

	if (is_roce_protocol) {
		gid_attr = attr->ah_attr.grh.sgid_attr;
		ret = rdma_read_gid_l2_fields(gid_attr, &vlan_id, NULL);
		if (ret)
			return ret;

		is_udp = (gid_attr->gid_type == IB_GID_TYPE_ROCE_UDP_ENCAP);
	}

	/* Only HIP08 needs to set the vlan_en bits in QPC */
	if (vlan_id < VLAN_N_VID &&
	    hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		hr_reg_enable(context, QPC_RQ_VLAN_EN);
		hr_reg_clear(qpc_mask, QPC_RQ_VLAN_EN);
		hr_reg_enable(context, QPC_SQ_VLAN_EN);
		hr_reg_clear(qpc_mask, QPC_SQ_VLAN_EN);
	}

	hr_reg_write(context, QPC_VLAN_ID, vlan_id);
	hr_reg_clear(qpc_mask, QPC_VLAN_ID);

	if (grh->sgid_index >= hr_dev->caps.gid_table_len[hr_port]) {
		ibdev_err(ibdev, "sgid_index(%u) too large. max is %d\n",
			  grh->sgid_index, hr_dev->caps.gid_table_len[hr_port]);
		return -EINVAL;
	}

	if (attr->ah_attr.type != RDMA_AH_ATTR_TYPE_ROCE) {
		ibdev_err(ibdev, "ah attr is not RDMA roce type\n");
		return -EINVAL;
	}

	hr_reg_write(context, QPC_UDPSPN,
		     is_udp ? rdma_get_udp_sport(grh->flow_label, ibqp->qp_num,
						 attr->dest_qp_num) :
				    0);

	hr_reg_clear(qpc_mask, QPC_UDPSPN);

	hr_reg_write(context, QPC_GMV_IDX, grh->sgid_index);

	hr_reg_clear(qpc_mask, QPC_GMV_IDX);

	hr_reg_write(context, QPC_HOPLIMIT, grh->hop_limit);
	hr_reg_clear(qpc_mask, QPC_HOPLIMIT);

	ret = fill_cong_field(ibqp, attr, context, qpc_mask);
	if (ret)
		return ret;

	hr_reg_write(context, QPC_TC, get_tclass(&attr->ah_attr.grh));
	hr_reg_clear(qpc_mask, QPC_TC);

	hr_reg_write(context, QPC_FL, grh->flow_label);
	hr_reg_clear(qpc_mask, QPC_FL);
	memcpy(context->dgid, grh->dgid.raw, sizeof(grh->dgid.raw));
	memset(qpc_mask->dgid, 0, sizeof(grh->dgid.raw));

	hr_qp->sl = sl;
	hr_reg_write(context, QPC_SL, hr_qp->sl);
	hr_reg_clear(qpc_mask, QPC_SL);

	return 0;
}

static bool check_qp_state(enum ib_qp_state cur_state,
			   enum ib_qp_state new_state)
{
	static const bool sm[][IB_QPS_ERR + 1] = {
		[IB_QPS_RESET] = { [IB_QPS_RESET] = true,
				   [IB_QPS_INIT] = true },
		[IB_QPS_INIT] = { [IB_QPS_RESET] = true,
				  [IB_QPS_INIT] = true,
				  [IB_QPS_RTR] = true,
				  [IB_QPS_ERR] = true },
		[IB_QPS_RTR] = { [IB_QPS_RESET] = true,
				 [IB_QPS_RTS] = true,
				 [IB_QPS_ERR] = true },
		[IB_QPS_RTS] = { [IB_QPS_RESET] = true,
				 [IB_QPS_RTS] = true,
				 [IB_QPS_ERR] = true },
		[IB_QPS_SQD] = {},
		[IB_QPS_SQE] = {},
		[IB_QPS_ERR] = { [IB_QPS_RESET] = true,
				 [IB_QPS_ERR] = true }
	};

	return sm[cur_state][new_state];
}

static int hns_roce_v2_set_abs_fields(struct ib_qp *ibqp,
				      const struct ib_qp_attr *attr,
				      int attr_mask,
				      enum ib_qp_state cur_state,
				      enum ib_qp_state new_state,
				      struct hns_roce_v2_qp_context *context,
				      struct hns_roce_v2_qp_context *qpc_mask,
				      struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	int ret = 0;

	if (!check_qp_state(cur_state, new_state)) {
		ibdev_err(&hr_dev->ib_dev, "Illegal state for QP!\n");
		return -EINVAL;
	}

	if (cur_state == IB_QPS_RESET && new_state == IB_QPS_INIT) {
		memset(qpc_mask, 0, hr_dev->caps.qpc_sz);
		modify_qp_reset_to_init(ibqp, attr, context, qpc_mask);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_INIT) {
		modify_qp_init_to_init(ibqp, attr, context, qpc_mask);
	} else if (cur_state == IB_QPS_INIT && new_state == IB_QPS_RTR) {
		ret = modify_qp_init_to_rtr(ibqp, attr, attr_mask, context,
					    qpc_mask, udata);
	} else if (cur_state == IB_QPS_RTR && new_state == IB_QPS_RTS) {
		ret = modify_qp_rtr_to_rts(ibqp, attr, attr_mask, context,
					   qpc_mask);
	}

	return ret;
}

static bool check_qp_timeout_cfg_range(struct hns_roce_dev *hr_dev, u8 *timeout)
{
#define QP_ACK_TIMEOUT_MAX_HIP08 20
#define QP_ACK_TIMEOUT_MAX 31

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		if (*timeout > QP_ACK_TIMEOUT_MAX_HIP08) {
			ibdev_warn(&hr_dev->ib_dev,
				   "local ACK timeout shall be 0 to 20.\n");
			return false;
		}
		*timeout += HNS_ROCE_V2_QP_ACK_TIMEOUT_OFS_HIP08;
	} else if (hr_dev->pci_dev->revision > PCI_REVISION_ID_HIP08) {
		if (*timeout > QP_ACK_TIMEOUT_MAX) {
			ibdev_warn(&hr_dev->ib_dev,
				   "local ACK timeout shall be 0 to 31.\n");
			return false;
		}
	}

	return true;
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
	u8 timeout;

	if (attr_mask & IB_QP_AV) {
		ret = hns_roce_v2_set_path(ibqp, attr, attr_mask, context,
					   qpc_mask);
		if (ret)
			return ret;
	}

	if (attr_mask & IB_QP_TIMEOUT) {
		timeout = attr->timeout;
		if (check_qp_timeout_cfg_range(hr_dev, &timeout)) {
			hr_reg_write(context, QPC_AT, timeout);
			hr_reg_clear(qpc_mask, QPC_AT);
		}
	}

	if (attr_mask & IB_QP_RETRY_CNT) {
		hr_reg_write(context, QPC_RETRY_NUM_INIT, attr->retry_cnt);
		hr_reg_clear(qpc_mask, QPC_RETRY_NUM_INIT);

		hr_reg_write(context, QPC_RETRY_CNT, attr->retry_cnt);
		hr_reg_clear(qpc_mask, QPC_RETRY_CNT);
	}

	if (attr_mask & IB_QP_RNR_RETRY) {
		hr_reg_write(context, QPC_RNR_NUM_INIT, attr->rnr_retry);
		hr_reg_clear(qpc_mask, QPC_RNR_NUM_INIT);

		hr_reg_write(context, QPC_RNR_CNT, attr->rnr_retry);
		hr_reg_clear(qpc_mask, QPC_RNR_CNT);
	}

	if (attr_mask & IB_QP_SQ_PSN) {
		hr_reg_write(context, QPC_SQ_CUR_PSN, attr->sq_psn);
		hr_reg_clear(qpc_mask, QPC_SQ_CUR_PSN);

		hr_reg_write(context, QPC_SQ_MAX_PSN, attr->sq_psn);
		hr_reg_clear(qpc_mask, QPC_SQ_MAX_PSN);

		hr_reg_write(context, QPC_RETRY_MSG_PSN_L, attr->sq_psn);
		hr_reg_clear(qpc_mask, QPC_RETRY_MSG_PSN_L);

		hr_reg_write(context, QPC_RETRY_MSG_PSN_H,
			     attr->sq_psn >> RETRY_MSG_PSN_SHIFT);
		hr_reg_clear(qpc_mask, QPC_RETRY_MSG_PSN_H);

		hr_reg_write(context, QPC_RETRY_MSG_FPKT_PSN, attr->sq_psn);
		hr_reg_clear(qpc_mask, QPC_RETRY_MSG_FPKT_PSN);

		hr_reg_write(context, QPC_RX_ACK_EPSN, attr->sq_psn);
		hr_reg_clear(qpc_mask, QPC_RX_ACK_EPSN);
	}

	if ((attr_mask & IB_QP_MAX_DEST_RD_ATOMIC) &&
	     attr->max_dest_rd_atomic) {
		hr_reg_write(context, QPC_RR_MAX,
			     fls(attr->max_dest_rd_atomic - 1));
		hr_reg_clear(qpc_mask, QPC_RR_MAX);
	}

	if ((attr_mask & IB_QP_MAX_QP_RD_ATOMIC) && attr->max_rd_atomic) {
		hr_reg_write(context, QPC_SR_MAX, fls(attr->max_rd_atomic - 1));
		hr_reg_clear(qpc_mask, QPC_SR_MAX);
	}

	if (attr_mask & (IB_QP_ACCESS_FLAGS | IB_QP_MAX_DEST_RD_ATOMIC))
		set_access_flags(hr_qp, context, qpc_mask, attr, attr_mask);

	if (attr_mask & IB_QP_MIN_RNR_TIMER) {
		hr_reg_write(context, QPC_MIN_RNR_TIME,
			    hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08 ?
			    HNS_ROCE_RNR_TIMER_10NS : attr->min_rnr_timer);
		hr_reg_clear(qpc_mask, QPC_MIN_RNR_TIME);
	}

	if (attr_mask & IB_QP_RQ_PSN) {
		hr_reg_write(context, QPC_RX_REQ_EPSN, attr->rq_psn);
		hr_reg_clear(qpc_mask, QPC_RX_REQ_EPSN);

		hr_reg_write(context, QPC_RAQ_PSN, attr->rq_psn - 1);
		hr_reg_clear(qpc_mask, QPC_RAQ_PSN);
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

static void clear_qp(struct hns_roce_qp *hr_qp)
{
	struct ib_qp *ibqp = &hr_qp->ibqp;

	if (ibqp->send_cq)
		hns_roce_v2_cq_clean(to_hr_cq(ibqp->send_cq),
				     hr_qp->qpn, NULL);

	if (ibqp->recv_cq  && ibqp->recv_cq != ibqp->send_cq)
		hns_roce_v2_cq_clean(to_hr_cq(ibqp->recv_cq),
				     hr_qp->qpn, ibqp->srq ?
				     to_hr_srq(ibqp->srq) : NULL);

	if (hr_qp->en_flags & HNS_ROCE_QP_CAP_RQ_RECORD_DB)
		*hr_qp->rdb.db_record = 0;

	hr_qp->rq.head = 0;
	hr_qp->rq.tail = 0;
	hr_qp->sq.head = 0;
	hr_qp->sq.tail = 0;
	hr_qp->next_sge = 0;
}

static void v2_set_flushed_fields(struct ib_qp *ibqp,
				  struct hns_roce_v2_qp_context *context,
				  struct hns_roce_v2_qp_context *qpc_mask)
{
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	unsigned long sq_flag = 0;
	unsigned long rq_flag = 0;

	if (ibqp->qp_type == IB_QPT_XRC_TGT)
		return;

	spin_lock_irqsave(&hr_qp->sq.lock, sq_flag);
	hr_reg_write(context, QPC_SQ_PRODUCER_IDX, hr_qp->sq.head);
	hr_reg_clear(qpc_mask, QPC_SQ_PRODUCER_IDX);
	hr_qp->state = IB_QPS_ERR;
	spin_unlock_irqrestore(&hr_qp->sq.lock, sq_flag);

	if (ibqp->srq || ibqp->qp_type == IB_QPT_XRC_INI) /* no RQ */
		return;

	spin_lock_irqsave(&hr_qp->rq.lock, rq_flag);
	hr_reg_write(context, QPC_RQ_PRODUCER_IDX, hr_qp->rq.head);
	hr_reg_clear(qpc_mask, QPC_RQ_PRODUCER_IDX);
	spin_unlock_irqrestore(&hr_qp->rq.lock, rq_flag);
}

static int hns_roce_v2_modify_qp(struct ib_qp *ibqp,
				 const struct ib_qp_attr *attr,
				 int attr_mask, enum ib_qp_state cur_state,
				 enum ib_qp_state new_state, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	struct hns_roce_v2_qp_context ctx[2];
	struct hns_roce_v2_qp_context *context = ctx;
	struct hns_roce_v2_qp_context *qpc_mask = ctx + 1;
	struct ib_device *ibdev = &hr_dev->ib_dev;
	int ret;

	if (attr_mask & ~IB_QP_ATTR_STANDARD_BITS)
		return -EOPNOTSUPP;

	/*
	 * In v2 engine, software pass context and context mask to hardware
	 * when modifying qp. If software need modify some fields in context,
	 * we should set all bits of the relevant fields in context mask to
	 * 0 at the same time, else set them to 0x1.
	 */
	memset(context, 0, hr_dev->caps.qpc_sz);
	memset(qpc_mask, 0xff, hr_dev->caps.qpc_sz);

	ret = hns_roce_v2_set_abs_fields(ibqp, attr, attr_mask, cur_state,
					 new_state, context, qpc_mask, udata);
	if (ret)
		goto out;

	/* When QP state is err, SQ and RQ WQE should be flushed */
	if (new_state == IB_QPS_ERR)
		v2_set_flushed_fields(ibqp, context, qpc_mask);

	/* Configure the optional fields */
	ret = hns_roce_v2_set_opt_fields(ibqp, attr, attr_mask, context,
					 qpc_mask);
	if (ret)
		goto out;

	hr_reg_write_bool(context, QPC_INV_CREDIT,
			  to_hr_qp_type(hr_qp->ibqp.qp_type) == SERV_TYPE_XRC ||
			  ibqp->srq);
	hr_reg_clear(qpc_mask, QPC_INV_CREDIT);

	/* Every status migrate must change state */
	hr_reg_write(context, QPC_QP_ST, new_state);
	hr_reg_clear(qpc_mask, QPC_QP_ST);

	/* SW pass context to HW */
	ret = hns_roce_v2_qp_modify(hr_dev, context, qpc_mask, hr_qp);
	if (ret) {
		ibdev_err(ibdev, "failed to modify QP, ret = %d.\n", ret);
		goto out;
	}

	hr_qp->state = new_state;

	hns_roce_v2_record_opt_fields(ibqp, attr, attr_mask);

	if (new_state == IB_QPS_RESET && !ibqp->uobject)
		clear_qp(hr_qp);

out:
	return ret;
}

static int to_ib_qp_st(enum hns_roce_v2_qp_state state)
{
	static const enum ib_qp_state map[] = {
		[HNS_ROCE_QP_ST_RST] = IB_QPS_RESET,
		[HNS_ROCE_QP_ST_INIT] = IB_QPS_INIT,
		[HNS_ROCE_QP_ST_RTR] = IB_QPS_RTR,
		[HNS_ROCE_QP_ST_RTS] = IB_QPS_RTS,
		[HNS_ROCE_QP_ST_SQD] = IB_QPS_SQD,
		[HNS_ROCE_QP_ST_SQER] = IB_QPS_SQE,
		[HNS_ROCE_QP_ST_ERR] = IB_QPS_ERR,
		[HNS_ROCE_QP_ST_SQ_DRAINING] = IB_QPS_SQD
	};

	return (state < ARRAY_SIZE(map)) ? map[state] : -1;
}

static int hns_roce_v2_query_qpc(struct hns_roce_dev *hr_dev, u32 qpn,
				 void *buffer)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, HNS_ROCE_CMD_QUERY_QPC,
				qpn);
	if (ret)
		goto out;

	memcpy(buffer, mailbox->buf, hr_dev->caps.qpc_sz);

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static int hns_roce_v2_query_srqc(struct hns_roce_dev *hr_dev, u32 srqn,
				 void *buffer)
{
	struct hns_roce_srq_context *context;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, HNS_ROCE_CMD_QUERY_SRQC,
				srqn);
	if (ret)
		goto out;

	memcpy(buffer, context, sizeof(*context));

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static u8 get_qp_timeout_attr(struct hns_roce_dev *hr_dev,
			      struct hns_roce_v2_qp_context *context)
{
	u8 timeout;

	timeout = (u8)hr_reg_read(context, QPC_AT);
	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08)
		timeout -= HNS_ROCE_V2_QP_ACK_TIMEOUT_OFS_HIP08;

	return timeout;
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

	ret = hns_roce_v2_query_qpc(hr_dev, hr_qp->qpn, &context);
	if (ret) {
		ibdev_err(ibdev, "failed to query QPC, ret = %d.\n", ret);
		ret = -EINVAL;
		goto out;
	}

	state = hr_reg_read(&context, QPC_QP_ST);
	tmp_qp_state = to_ib_qp_st((enum hns_roce_v2_qp_state)state);
	if (tmp_qp_state == -1) {
		ibdev_err(ibdev, "Illegal ib_qp_state\n");
		ret = -EINVAL;
		goto out;
	}
	hr_qp->state = (u8)tmp_qp_state;
	qp_attr->qp_state = (enum ib_qp_state)hr_qp->state;
	qp_attr->path_mtu = (enum ib_mtu)hr_reg_read(&context, QPC_MTU);
	qp_attr->path_mig_state = IB_MIG_ARMED;
	qp_attr->ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;
	if (hr_qp->ibqp.qp_type == IB_QPT_UD)
		qp_attr->qkey = le32_to_cpu(context.qkey_xrcd);

	qp_attr->rq_psn = hr_reg_read(&context, QPC_RX_REQ_EPSN);
	qp_attr->sq_psn = (u32)hr_reg_read(&context, QPC_SQ_CUR_PSN);
	qp_attr->dest_qp_num = hr_reg_read(&context, QPC_DQPN);
	qp_attr->qp_access_flags =
		((hr_reg_read(&context, QPC_RRE)) << V2_QP_RRE_S) |
		((hr_reg_read(&context, QPC_RWE)) << V2_QP_RWE_S) |
		((hr_reg_read(&context, QPC_ATE)) << V2_QP_ATE_S);

	if (hr_qp->ibqp.qp_type == IB_QPT_RC ||
	    hr_qp->ibqp.qp_type == IB_QPT_XRC_INI ||
	    hr_qp->ibqp.qp_type == IB_QPT_XRC_TGT) {
		struct ib_global_route *grh =
			rdma_ah_retrieve_grh(&qp_attr->ah_attr);

		rdma_ah_set_sl(&qp_attr->ah_attr,
			       hr_reg_read(&context, QPC_SL));
		rdma_ah_set_port_num(&qp_attr->ah_attr, hr_qp->port + 1);
		rdma_ah_set_ah_flags(&qp_attr->ah_attr, IB_AH_GRH);
		grh->flow_label = hr_reg_read(&context, QPC_FL);
		grh->sgid_index = hr_reg_read(&context, QPC_GMV_IDX);
		grh->hop_limit = hr_reg_read(&context, QPC_HOPLIMIT);
		grh->traffic_class = hr_reg_read(&context, QPC_TC);

		memcpy(grh->dgid.raw, context.dgid, sizeof(grh->dgid.raw));
	}

	qp_attr->port_num = hr_qp->port + 1;
	qp_attr->sq_draining = 0;
	qp_attr->max_rd_atomic = 1 << hr_reg_read(&context, QPC_SR_MAX);
	qp_attr->max_dest_rd_atomic = 1 << hr_reg_read(&context, QPC_RR_MAX);

	qp_attr->min_rnr_timer = (u8)hr_reg_read(&context, QPC_MIN_RNR_TIME);
	qp_attr->timeout = get_qp_timeout_attr(hr_dev, &context);
	qp_attr->retry_cnt = hr_reg_read(&context, QPC_RETRY_NUM_INIT);
	qp_attr->rnr_retry = hr_reg_read(&context, QPC_RNR_NUM_INIT);

done:
	qp_attr->cur_qp_state = qp_attr->qp_state;
	qp_attr->cap.max_recv_wr = hr_qp->rq.wqe_cnt;
	qp_attr->cap.max_recv_sge = hr_qp->rq.max_gs - hr_qp->rq.rsv_sge;
	qp_attr->cap.max_inline_data = hr_qp->max_inline_data;

	qp_attr->cap.max_send_wr = hr_qp->sq.wqe_cnt;
	qp_attr->cap.max_send_sge = hr_qp->sq.max_gs;

	qp_init_attr->qp_context = ibqp->qp_context;
	qp_init_attr->qp_type = ibqp->qp_type;
	qp_init_attr->recv_cq = ibqp->recv_cq;
	qp_init_attr->send_cq = ibqp->send_cq;
	qp_init_attr->srq = ibqp->srq;
	qp_init_attr->cap = qp_attr->cap;
	qp_init_attr->sq_sig_type = hr_qp->sq_signal_bits;

out:
	mutex_unlock(&hr_qp->mutex);
	return ret;
}

static inline int modify_qp_is_ok(struct hns_roce_qp *hr_qp)
{
	return ((hr_qp->ibqp.qp_type == IB_QPT_RC ||
		 hr_qp->ibqp.qp_type == IB_QPT_UD ||
		 hr_qp->ibqp.qp_type == IB_QPT_XRC_INI ||
		 hr_qp->ibqp.qp_type == IB_QPT_XRC_TGT) &&
		hr_qp->state != IB_QPS_RESET);
}

static int hns_roce_v2_destroy_qp_common(struct hns_roce_dev *hr_dev,
					 struct hns_roce_qp *hr_qp,
					 struct ib_udata *udata)
{
	struct ib_device *ibdev = &hr_dev->ib_dev;
	struct hns_roce_cq *send_cq, *recv_cq;
	unsigned long flags;
	int ret = 0;

	if (modify_qp_is_ok(hr_qp)) {
		/* Modify qp to reset before destroying qp */
		ret = hns_roce_v2_modify_qp(&hr_qp->ibqp, NULL, 0,
					    hr_qp->state, IB_QPS_RESET, udata);
		if (ret)
			ibdev_err(ibdev,
				  "failed to modify QP to RST, ret = %d.\n",
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

int hns_roce_v2_destroy_qp(struct ib_qp *ibqp, struct ib_udata *udata)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibqp->device);
	struct hns_roce_qp *hr_qp = to_hr_qp(ibqp);
	int ret;

	ret = hns_roce_v2_destroy_qp_common(hr_dev, hr_qp, udata);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to destroy QP, QPN = 0x%06lx, ret = %d.\n",
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

	if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09)
		return 0;

	mutex_lock(&hr_dev->qp_table.scc_mutex);

	/* set scc ctx clear done flag */
	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_RESET_SCCC, false);
	ret =  hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		ibdev_err(ibdev, "failed to reset SCC ctx, ret = %d.\n", ret);
		goto out;
	}

	/* clear scc context */
	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CLR_SCCC, false);
	clr = (struct hns_roce_sccc_clr *)desc.data;
	clr->qpn = cpu_to_le32(hr_qp->qpn);
	ret =  hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		ibdev_err(ibdev, "failed to clear SCC ctx, ret = %d.\n", ret);
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

	ibdev_err(ibdev, "query SCC clr done flag overtime.\n");
	ret = -ETIMEDOUT;

out:
	mutex_unlock(&hr_dev->qp_table.scc_mutex);
	return ret;
}

#define DMA_IDX_SHIFT 3
#define DMA_WQE_SHIFT 3

static int hns_roce_v2_write_srqc_index_queue(struct hns_roce_srq *srq,
					      struct hns_roce_srq_context *ctx)
{
	struct hns_roce_idx_que *idx_que = &srq->idx_que;
	struct ib_device *ibdev = srq->ibsrq.device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibdev);
	u64 mtts_idx[MTT_MIN_COUNT] = {};
	dma_addr_t dma_handle_idx = 0;
	int ret;

	/* Get physical address of idx que buf */
	ret = hns_roce_mtr_find(hr_dev, &idx_que->mtr, 0, mtts_idx,
				ARRAY_SIZE(mtts_idx), &dma_handle_idx);
	if (ret < 1) {
		ibdev_err(ibdev, "failed to find mtr for SRQ idx, ret = %d.\n",
			  ret);
		return -ENOBUFS;
	}

	hr_reg_write(ctx, SRQC_IDX_HOP_NUM,
		     to_hr_hem_hopnum(hr_dev->caps.idx_hop_num, srq->wqe_cnt));

	hr_reg_write(ctx, SRQC_IDX_BT_BA_L, dma_handle_idx >> DMA_IDX_SHIFT);
	hr_reg_write(ctx, SRQC_IDX_BT_BA_H,
		     upper_32_bits(dma_handle_idx >> DMA_IDX_SHIFT));

	hr_reg_write(ctx, SRQC_IDX_BA_PG_SZ,
		     to_hr_hw_page_shift(idx_que->mtr.hem_cfg.ba_pg_shift));
	hr_reg_write(ctx, SRQC_IDX_BUF_PG_SZ,
		     to_hr_hw_page_shift(idx_que->mtr.hem_cfg.buf_pg_shift));

	hr_reg_write(ctx, SRQC_IDX_CUR_BLK_ADDR_L,
		     to_hr_hw_page_addr(mtts_idx[0]));
	hr_reg_write(ctx, SRQC_IDX_CUR_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts_idx[0])));

	hr_reg_write(ctx, SRQC_IDX_NXT_BLK_ADDR_L,
		     to_hr_hw_page_addr(mtts_idx[1]));
	hr_reg_write(ctx, SRQC_IDX_NXT_BLK_ADDR_H,
		     upper_32_bits(to_hr_hw_page_addr(mtts_idx[1])));

	return 0;
}

static int hns_roce_v2_write_srqc(struct hns_roce_srq *srq, void *mb_buf)
{
	struct ib_device *ibdev = srq->ibsrq.device;
	struct hns_roce_dev *hr_dev = to_hr_dev(ibdev);
	struct hns_roce_srq_context *ctx = mb_buf;
	u64 mtts_wqe[MTT_MIN_COUNT] = {};
	dma_addr_t dma_handle_wqe = 0;
	int ret;

	memset(ctx, 0, sizeof(*ctx));

	/* Get the physical address of srq buf */
	ret = hns_roce_mtr_find(hr_dev, &srq->buf_mtr, 0, mtts_wqe,
				ARRAY_SIZE(mtts_wqe), &dma_handle_wqe);
	if (ret < 1) {
		ibdev_err(ibdev, "failed to find mtr for SRQ WQE, ret = %d.\n",
			  ret);
		return -ENOBUFS;
	}

	hr_reg_write(ctx, SRQC_SRQ_ST, 1);
	hr_reg_write_bool(ctx, SRQC_SRQ_TYPE,
			  srq->ibsrq.srq_type == IB_SRQT_XRC);
	hr_reg_write(ctx, SRQC_PD, to_hr_pd(srq->ibsrq.pd)->pdn);
	hr_reg_write(ctx, SRQC_SRQN, srq->srqn);
	hr_reg_write(ctx, SRQC_XRCD, srq->xrcdn);
	hr_reg_write(ctx, SRQC_XRC_CQN, srq->cqn);
	hr_reg_write(ctx, SRQC_SHIFT, ilog2(srq->wqe_cnt));
	hr_reg_write(ctx, SRQC_RQWS,
		     srq->max_gs <= 0 ? 0 : fls(srq->max_gs - 1));

	hr_reg_write(ctx, SRQC_WQE_HOP_NUM,
		     to_hr_hem_hopnum(hr_dev->caps.srqwqe_hop_num,
				      srq->wqe_cnt));

	hr_reg_write(ctx, SRQC_WQE_BT_BA_L, dma_handle_wqe >> DMA_WQE_SHIFT);
	hr_reg_write(ctx, SRQC_WQE_BT_BA_H,
		     upper_32_bits(dma_handle_wqe >> DMA_WQE_SHIFT));

	hr_reg_write(ctx, SRQC_WQE_BA_PG_SZ,
		     to_hr_hw_page_shift(srq->buf_mtr.hem_cfg.ba_pg_shift));
	hr_reg_write(ctx, SRQC_WQE_BUF_PG_SZ,
		     to_hr_hw_page_shift(srq->buf_mtr.hem_cfg.buf_pg_shift));

	if (srq->cap_flags & HNS_ROCE_SRQ_CAP_RECORD_DB) {
		hr_reg_enable(ctx, SRQC_DB_RECORD_EN);
		hr_reg_write(ctx, SRQC_DB_RECORD_ADDR_L,
			     lower_32_bits(srq->rdb.dma) >> 1);
		hr_reg_write(ctx, SRQC_DB_RECORD_ADDR_H,
			     upper_32_bits(srq->rdb.dma));
	}

	return hns_roce_v2_write_srqc_index_queue(srq, ctx);
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
	int ret = 0;

	/* Resizing SRQs is not supported yet */
	if (srq_attr_mask & IB_SRQ_MAX_WR) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (srq_attr_mask & IB_SRQ_LIMIT) {
		if (srq_attr->srq_limit > srq->wqe_cnt) {
			ret = -EINVAL;
			goto out;
		}

		mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
		if (IS_ERR(mailbox)) {
			ret = PTR_ERR(mailbox);
			goto out;
		}

		srq_context = mailbox->buf;
		srqc_mask = (struct hns_roce_srq_context *)mailbox->buf + 1;

		memset(srqc_mask, 0xff, sizeof(*srqc_mask));

		hr_reg_write(srq_context, SRQC_LIMIT_WL, srq_attr->srq_limit);
		hr_reg_clear(srqc_mask, SRQC_LIMIT_WL);

		ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0,
					HNS_ROCE_CMD_MODIFY_SRQC, srq->srqn);
		hns_roce_free_cmd_mailbox(hr_dev, mailbox);
		if (ret)
			ibdev_err(&hr_dev->ib_dev,
				  "failed to handle cmd of modifying SRQ, ret = %d.\n",
				  ret);
	}

out:
	if (ret)
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_SRQ_MODIFY_ERR_CNT]);

	return ret;
}

static int hns_roce_v2_query_srq(struct ib_srq *ibsrq, struct ib_srq_attr *attr)
{
	struct hns_roce_dev *hr_dev = to_hr_dev(ibsrq->device);
	struct hns_roce_srq *srq = to_hr_srq(ibsrq);
	struct hns_roce_srq_context *srq_context;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	srq_context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma,
				HNS_ROCE_CMD_QUERY_SRQC, srq->srqn);
	if (ret) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd of querying SRQ, ret = %d.\n",
			  ret);
		goto out;
	}

	attr->srq_limit = hr_reg_read(srq_context, SRQC_LIMIT_WL);
	attr->max_wr = srq->wqe_cnt;
	attr->max_sge = srq->max_gs - srq->rsv_sge;

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
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
	ret = PTR_ERR_OR_ZERO(mailbox);
	if (ret)
		goto err_out;

	cq_context = mailbox->buf;
	cqc_mask = (struct hns_roce_v2_cq_context *)mailbox->buf + 1;

	memset(cqc_mask, 0xff, sizeof(*cqc_mask));

	hr_reg_write(cq_context, CQC_CQ_MAX_CNT, cq_count);
	hr_reg_clear(cqc_mask, CQC_CQ_MAX_CNT);

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		if (cq_period * HNS_ROCE_CLOCK_ADJUST > USHRT_MAX) {
			dev_info(hr_dev->dev,
				 "cq_period(%u) reached the upper limit, adjusted to 65.\n",
				 cq_period);
			cq_period = HNS_ROCE_MAX_CQ_PERIOD;
		}
		cq_period *= HNS_ROCE_CLOCK_ADJUST;
	}
	hr_reg_write(cq_context, CQC_CQ_PERIOD, cq_period);
	hr_reg_clear(cqc_mask, CQC_CQ_PERIOD);

	ret = hns_roce_cmd_mbox(hr_dev, mailbox->dma, 0,
				HNS_ROCE_CMD_MODIFY_CQC, hr_cq->cqn);
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	if (ret)
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd when modifying CQ, ret = %d.\n",
			  ret);

err_out:
	if (ret)
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_CQ_MODIFY_ERR_CNT]);

	return ret;
}

static int hns_roce_v2_query_cqc(struct hns_roce_dev *hr_dev, u32 cqn,
				 void *buffer)
{
	struct hns_roce_v2_cq_context *context;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma,
				HNS_ROCE_CMD_QUERY_CQC, cqn);
	if (ret) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd when querying CQ, ret = %d.\n",
			  ret);
		goto err_mailbox;
	}

	memcpy(buffer, context, sizeof(*context));

err_mailbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

static int hns_roce_v2_query_mpt(struct hns_roce_dev *hr_dev, u32 key,
				 void *buffer)
{
	struct hns_roce_v2_mpt_entry *context;
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	context = mailbox->buf;
	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, HNS_ROCE_CMD_QUERY_MPT,
				key_to_hw_index(key));
	if (ret) {
		ibdev_err(&hr_dev->ib_dev,
			  "failed to process cmd when querying MPT, ret = %d.\n",
			  ret);
		goto err_mailbox;
	}

	memcpy(buffer, context, sizeof(*context));

err_mailbox:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);

	return ret;
}

static void hns_roce_irq_work_handle(struct work_struct *work)
{
	struct hns_roce_work *irq_work =
				container_of(work, struct hns_roce_work, work);
	struct ib_device *ibdev = &irq_work->hr_dev->ib_dev;

	switch (irq_work->event_type) {
	case HNS_ROCE_EVENT_TYPE_PATH_MIG:
		ibdev_info(ibdev, "path migrated succeeded.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
		ibdev_warn(ibdev, "path migration failed.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_COMM_EST:
		break;
	case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
		ibdev_dbg(ibdev, "send queue drained.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		ibdev_err(ibdev, "local work queue 0x%x catast error, sub_event type is: %d\n",
			  irq_work->queue_num, irq_work->sub_type);
		break;
	case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		ibdev_err(ibdev, "invalid request local work queue 0x%x error.\n",
			  irq_work->queue_num);
		break;
	case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
		ibdev_err(ibdev, "local access violation work queue 0x%x error, sub_event type is: %d\n",
			  irq_work->queue_num, irq_work->sub_type);
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		ibdev_dbg(ibdev, "SRQ limit reach.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
		ibdev_dbg(ibdev, "SRQ last wqe reach.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
		ibdev_err(ibdev, "SRQ catas error.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		ibdev_err(ibdev, "CQ 0x%x access err.\n", irq_work->queue_num);
		break;
	case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
		ibdev_warn(ibdev, "CQ 0x%x overflow\n", irq_work->queue_num);
		break;
	case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
		ibdev_warn(ibdev, "DB overflow.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_FLR:
		ibdev_warn(ibdev, "function level reset.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_XRCD_VIOLATION:
		ibdev_err(ibdev, "xrc domain violation error.\n");
		break;
	case HNS_ROCE_EVENT_TYPE_INVALID_XRCETH:
		ibdev_err(ibdev, "invalid xrceth error.\n");
		break;
	default:
		break;
	}

	kfree(irq_work);
}

static void hns_roce_v2_init_irq_work(struct hns_roce_dev *hr_dev,
				      struct hns_roce_eq *eq, u32 queue_num)
{
	struct hns_roce_work *irq_work;

	irq_work = kzalloc(sizeof(struct hns_roce_work), GFP_ATOMIC);
	if (!irq_work)
		return;

	INIT_WORK(&irq_work->work, hns_roce_irq_work_handle);
	irq_work->hr_dev = hr_dev;
	irq_work->event_type = eq->event_type;
	irq_work->sub_type = eq->sub_type;
	irq_work->queue_num = queue_num;
	queue_work(hr_dev->irq_workq, &irq_work->work);
}

static void update_eq_db(struct hns_roce_eq *eq)
{
	struct hns_roce_dev *hr_dev = eq->hr_dev;
	struct hns_roce_v2_db eq_db = {};

	if (eq->type_flag == HNS_ROCE_AEQ) {
		hr_reg_write(&eq_db, EQ_DB_CMD,
			     eq->arm_st == HNS_ROCE_V2_EQ_ALWAYS_ARMED ?
			     HNS_ROCE_EQ_DB_CMD_AEQ :
			     HNS_ROCE_EQ_DB_CMD_AEQ_ARMED);
	} else {
		hr_reg_write(&eq_db, EQ_DB_TAG, eq->eqn);

		hr_reg_write(&eq_db, EQ_DB_CMD,
			     eq->arm_st == HNS_ROCE_V2_EQ_ALWAYS_ARMED ?
			     HNS_ROCE_EQ_DB_CMD_CEQ :
			     HNS_ROCE_EQ_DB_CMD_CEQ_ARMED);
	}

	hr_reg_write(&eq_db, EQ_DB_CI, eq->cons_index);

	hns_roce_write64(hr_dev, (__le32 *)&eq_db, eq->db_reg);
}

static struct hns_roce_aeqe *next_aeqe_sw_v2(struct hns_roce_eq *eq)
{
	struct hns_roce_aeqe *aeqe;

	aeqe = hns_roce_buf_offset(eq->mtr.kmem,
				   (eq->cons_index & (eq->entries - 1)) *
				   eq->eqe_size);

	return (hr_reg_read(aeqe, AEQE_OWNER) ^
		!!(eq->cons_index & eq->entries)) ? aeqe : NULL;
}

static irqreturn_t hns_roce_v2_aeq_int(struct hns_roce_dev *hr_dev,
				       struct hns_roce_eq *eq)
{
	struct device *dev = hr_dev->dev;
	struct hns_roce_aeqe *aeqe = next_aeqe_sw_v2(eq);
	irqreturn_t aeqe_found = IRQ_NONE;
	int event_type;
	u32 queue_num;
	int sub_type;

	while (aeqe) {
		/* Make sure we read AEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		event_type = hr_reg_read(aeqe, AEQE_EVENT_TYPE);
		sub_type = hr_reg_read(aeqe, AEQE_SUB_TYPE);
		queue_num = hr_reg_read(aeqe, AEQE_EVENT_QUEUE_NUM);

		switch (event_type) {
		case HNS_ROCE_EVENT_TYPE_PATH_MIG:
		case HNS_ROCE_EVENT_TYPE_PATH_MIG_FAILED:
		case HNS_ROCE_EVENT_TYPE_COMM_EST:
		case HNS_ROCE_EVENT_TYPE_SQ_DRAINED:
		case HNS_ROCE_EVENT_TYPE_WQ_CATAS_ERROR:
		case HNS_ROCE_EVENT_TYPE_SRQ_LAST_WQE_REACH:
		case HNS_ROCE_EVENT_TYPE_INV_REQ_LOCAL_WQ_ERROR:
		case HNS_ROCE_EVENT_TYPE_LOCAL_WQ_ACCESS_ERROR:
		case HNS_ROCE_EVENT_TYPE_XRCD_VIOLATION:
		case HNS_ROCE_EVENT_TYPE_INVALID_XRCETH:
			hns_roce_qp_event(hr_dev, queue_num, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_SRQ_LIMIT_REACH:
		case HNS_ROCE_EVENT_TYPE_SRQ_CATAS_ERROR:
			hns_roce_srq_event(hr_dev, queue_num, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_CQ_ACCESS_ERROR:
		case HNS_ROCE_EVENT_TYPE_CQ_OVERFLOW:
			hns_roce_cq_event(hr_dev, queue_num, event_type);
			break;
		case HNS_ROCE_EVENT_TYPE_MB:
			hns_roce_cmd_event(hr_dev,
					le16_to_cpu(aeqe->event.cmd.token),
					aeqe->event.cmd.status,
					le64_to_cpu(aeqe->event.cmd.out_param));
			break;
		case HNS_ROCE_EVENT_TYPE_DB_OVERFLOW:
		case HNS_ROCE_EVENT_TYPE_FLR:
			break;
		default:
			dev_err(dev, "unhandled event %d on EQ %d at idx %u.\n",
				event_type, eq->eqn, eq->cons_index);
			break;
		}

		eq->event_type = event_type;
		eq->sub_type = sub_type;
		++eq->cons_index;
		aeqe_found = IRQ_HANDLED;

		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_AEQE_CNT]);

		hns_roce_v2_init_irq_work(hr_dev, eq, queue_num);

		aeqe = next_aeqe_sw_v2(eq);
	}

	update_eq_db(eq);

	return IRQ_RETVAL(aeqe_found);
}

static struct hns_roce_ceqe *next_ceqe_sw_v2(struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe;

	ceqe = hns_roce_buf_offset(eq->mtr.kmem,
				   (eq->cons_index & (eq->entries - 1)) *
				   eq->eqe_size);

	return (hr_reg_read(ceqe, CEQE_OWNER) ^
		!!(eq->cons_index & eq->entries)) ? ceqe : NULL;
}

static irqreturn_t hns_roce_v2_ceq_int(struct hns_roce_dev *hr_dev,
				       struct hns_roce_eq *eq)
{
	struct hns_roce_ceqe *ceqe = next_ceqe_sw_v2(eq);
	irqreturn_t ceqe_found = IRQ_NONE;
	u32 cqn;

	while (ceqe) {
		/* Make sure we read CEQ entry after we have checked the
		 * ownership bit
		 */
		dma_rmb();

		cqn = hr_reg_read(ceqe, CEQE_CQN);

		hns_roce_cq_completion(hr_dev, cqn);

		++eq->cons_index;
		ceqe_found = IRQ_HANDLED;
		atomic64_inc(&hr_dev->dfx_cnt[HNS_ROCE_DFX_CEQE_CNT]);

		ceqe = next_ceqe_sw_v2(eq);
	}

	update_eq_db(eq);

	return IRQ_RETVAL(ceqe_found);
}

static irqreturn_t hns_roce_v2_msix_interrupt_eq(int irq, void *eq_ptr)
{
	struct hns_roce_eq *eq = eq_ptr;
	struct hns_roce_dev *hr_dev = eq->hr_dev;
	irqreturn_t int_work;

	if (eq->type_flag == HNS_ROCE_CEQ)
		/* Completion event interrupt */
		int_work = hns_roce_v2_ceq_int(hr_dev, eq);
	else
		/* Asynchronous event interrupt */
		int_work = hns_roce_v2_aeq_int(hr_dev, eq);

	return IRQ_RETVAL(int_work);
}

static irqreturn_t abnormal_interrupt_basic(struct hns_roce_dev *hr_dev,
					    u32 int_st)
{
	struct pci_dev *pdev = hr_dev->pci_dev;
	struct hnae3_ae_dev *ae_dev = pci_get_drvdata(pdev);
	const struct hnae3_ae_ops *ops = ae_dev->ops;
	irqreturn_t int_work = IRQ_NONE;
	u32 int_en;

	int_en = roce_read(hr_dev, ROCEE_VF_ABN_INT_EN_REG);

	if (int_st & BIT(HNS_ROCE_V2_VF_INT_ST_AEQ_OVERFLOW_S)) {
		dev_err(hr_dev->dev, "AEQ overflow!\n");

		roce_write(hr_dev, ROCEE_VF_ABN_INT_ST_REG,
			   1 << HNS_ROCE_V2_VF_INT_ST_AEQ_OVERFLOW_S);

		/* Set reset level for reset_event() */
		if (ops->set_default_reset_request)
			ops->set_default_reset_request(ae_dev,
						       HNAE3_FUNC_RESET);
		if (ops->reset_event)
			ops->reset_event(pdev, NULL);

		int_en |= 1 << HNS_ROCE_V2_VF_ABN_INT_EN_S;
		roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG, int_en);

		int_work = IRQ_HANDLED;
	} else {
		dev_err(hr_dev->dev, "there is no basic abn irq found.\n");
	}

	return IRQ_RETVAL(int_work);
}

static int fmea_ram_ecc_query(struct hns_roce_dev *hr_dev,
			       struct fmea_ram_ecc *ecc_info)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_QUERY_RAM_ECC, true);
	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret)
		return ret;

	ecc_info->is_ecc_err = hr_reg_read(req, QUERY_RAM_ECC_1BIT_ERR);
	ecc_info->res_type = hr_reg_read(req, QUERY_RAM_ECC_RES_TYPE);
	ecc_info->index = hr_reg_read(req, QUERY_RAM_ECC_TAG);

	return 0;
}

static int fmea_recover_gmv(struct hns_roce_dev *hr_dev, u32 idx)
{
	struct hns_roce_cmq_desc desc;
	struct hns_roce_cmq_req *req = (struct hns_roce_cmq_req *)desc.data;
	u32 addr_upper;
	u32 addr_low;
	int ret;

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_GMV_BT, true);
	hr_reg_write(req, CFG_GMV_BT_IDX, idx);

	ret = hns_roce_cmq_send(hr_dev, &desc, 1);
	if (ret) {
		dev_err(hr_dev->dev,
			"failed to execute cmd to read gmv, ret = %d.\n", ret);
		return ret;
	}

	addr_low =  hr_reg_read(req, CFG_GMV_BT_BA_L);
	addr_upper = hr_reg_read(req, CFG_GMV_BT_BA_H);

	hns_roce_cmq_setup_basic_desc(&desc, HNS_ROCE_OPC_CFG_GMV_BT, false);
	hr_reg_write(req, CFG_GMV_BT_BA_L, addr_low);
	hr_reg_write(req, CFG_GMV_BT_BA_H, addr_upper);
	hr_reg_write(req, CFG_GMV_BT_IDX, idx);

	return hns_roce_cmq_send(hr_dev, &desc, 1);
}

static u64 fmea_get_ram_res_addr(u32 res_type, __le64 *data)
{
	if (res_type == ECC_RESOURCE_QPC_TIMER ||
	    res_type == ECC_RESOURCE_CQC_TIMER ||
	    res_type == ECC_RESOURCE_SCCC)
		return le64_to_cpu(*data);

	return le64_to_cpu(*data) << PAGE_SHIFT;
}

static int fmea_recover_others(struct hns_roce_dev *hr_dev, u32 res_type,
			       u32 index)
{
	u8 write_bt0_op = fmea_ram_res[res_type].write_bt0_op;
	u8 read_bt0_op = fmea_ram_res[res_type].read_bt0_op;
	struct hns_roce_cmd_mailbox *mailbox;
	u64 addr;
	int ret;

	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = hns_roce_cmd_mbox(hr_dev, 0, mailbox->dma, read_bt0_op, index);
	if (ret) {
		dev_err(hr_dev->dev,
			"failed to execute cmd to read fmea ram, ret = %d.\n",
			ret);
		goto out;
	}

	addr = fmea_get_ram_res_addr(res_type, mailbox->buf);

	ret = hns_roce_cmd_mbox(hr_dev, addr, 0, write_bt0_op, index);
	if (ret)
		dev_err(hr_dev->dev,
			"failed to execute cmd to write fmea ram, ret = %d.\n",
			ret);

out:
	hns_roce_free_cmd_mailbox(hr_dev, mailbox);
	return ret;
}

static void fmea_ram_ecc_recover(struct hns_roce_dev *hr_dev,
				 struct fmea_ram_ecc *ecc_info)
{
	u32 res_type = ecc_info->res_type;
	u32 index = ecc_info->index;
	int ret;

	BUILD_BUG_ON(ARRAY_SIZE(fmea_ram_res) != ECC_RESOURCE_COUNT);

	if (res_type >= ECC_RESOURCE_COUNT) {
		dev_err(hr_dev->dev, "unsupported fmea ram ecc type %u.\n",
			res_type);
		return;
	}

	if (res_type == ECC_RESOURCE_GMV)
		ret = fmea_recover_gmv(hr_dev, index);
	else
		ret = fmea_recover_others(hr_dev, res_type, index);
	if (ret)
		dev_err(hr_dev->dev,
			"failed to recover %s, index = %u, ret = %d.\n",
			fmea_ram_res[res_type].name, index, ret);
}

static void fmea_ram_ecc_work(struct work_struct *ecc_work)
{
	struct hns_roce_dev *hr_dev =
		container_of(ecc_work, struct hns_roce_dev, ecc_work);
	struct fmea_ram_ecc ecc_info = {};

	if (fmea_ram_ecc_query(hr_dev, &ecc_info)) {
		dev_err(hr_dev->dev, "failed to query fmea ram ecc.\n");
		return;
	}

	if (!ecc_info.is_ecc_err) {
		dev_err(hr_dev->dev, "there is no fmea ram ecc err found.\n");
		return;
	}

	fmea_ram_ecc_recover(hr_dev, &ecc_info);
}

static irqreturn_t hns_roce_v2_msix_interrupt_abn(int irq, void *dev_id)
{
	struct hns_roce_dev *hr_dev = dev_id;
	irqreturn_t int_work = IRQ_NONE;
	u32 int_st;

	int_st = roce_read(hr_dev, ROCEE_VF_ABN_INT_ST_REG);

	if (int_st) {
		int_work = abnormal_interrupt_basic(hr_dev, int_st);
	} else if (hr_dev->pci_dev->revision >= PCI_REVISION_ID_HIP09) {
		queue_work(hr_dev->irq_workq, &hr_dev->ecc_work);
		int_work = IRQ_HANDLED;
	} else {
		dev_err(hr_dev->dev, "there is no abnormal irq found.\n");
	}

	return IRQ_RETVAL(int_work);
}

static void hns_roce_v2_int_mask_enable(struct hns_roce_dev *hr_dev,
					int eq_num, u32 enable_flag)
{
	int i;

	for (i = 0; i < eq_num; i++)
		roce_write(hr_dev, ROCEE_VF_EVENT_INT_EN_REG +
			   i * EQ_REG_OFFSET, enable_flag);

	roce_write(hr_dev, ROCEE_VF_ABN_INT_EN_REG, enable_flag);
	roce_write(hr_dev, ROCEE_VF_ABN_INT_CFG_REG, enable_flag);
}

static void hns_roce_v2_destroy_eqc(struct hns_roce_dev *hr_dev, u32 eqn)
{
	struct device *dev = hr_dev->dev;
	int ret;
	u8 cmd;

	if (eqn < hr_dev->caps.num_comp_vectors)
		cmd = HNS_ROCE_CMD_DESTROY_CEQC;
	else
		cmd = HNS_ROCE_CMD_DESTROY_AEQC;

	ret = hns_roce_destroy_hw_ctx(hr_dev, cmd, eqn & HNS_ROCE_V2_EQN_M);
	if (ret)
		dev_err(dev, "[mailbox cmd] destroy eqc(%u) failed.\n", eqn);
}

static void free_eq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	hns_roce_mtr_destroy(hr_dev, &eq->mtr);
}

static void init_eq_config(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	eq->db_reg = hr_dev->reg_base + ROCEE_VF_EQ_DB_CFG0_REG;
	eq->cons_index = 0;
	eq->over_ignore = HNS_ROCE_V2_EQ_OVER_IGNORE_0;
	eq->coalesce = HNS_ROCE_V2_EQ_COALESCE_0;
	eq->arm_st = HNS_ROCE_V2_EQ_ALWAYS_ARMED;
	eq->shift = ilog2((unsigned int)eq->entries);
}

static int config_eqc(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq,
		      void *mb_buf)
{
	u64 eqe_ba[MTT_MIN_COUNT] = { 0 };
	struct hns_roce_eq_context *eqc;
	u64 bt_ba = 0;
	int count;

	eqc = mb_buf;
	memset(eqc, 0, sizeof(struct hns_roce_eq_context));

	init_eq_config(hr_dev, eq);

	/* if not multi-hop, eqe buffer only use one trunk */
	count = hns_roce_mtr_find(hr_dev, &eq->mtr, 0, eqe_ba, MTT_MIN_COUNT,
				  &bt_ba);
	if (count < 1) {
		dev_err(hr_dev->dev, "failed to find EQE mtr\n");
		return -ENOBUFS;
	}

	hr_reg_write(eqc, EQC_EQ_ST, HNS_ROCE_V2_EQ_STATE_VALID);
	hr_reg_write(eqc, EQC_EQE_HOP_NUM, eq->hop_num);
	hr_reg_write(eqc, EQC_OVER_IGNORE, eq->over_ignore);
	hr_reg_write(eqc, EQC_COALESCE, eq->coalesce);
	hr_reg_write(eqc, EQC_ARM_ST, eq->arm_st);
	hr_reg_write(eqc, EQC_EQN, eq->eqn);
	hr_reg_write(eqc, EQC_EQE_CNT, HNS_ROCE_EQ_INIT_EQE_CNT);
	hr_reg_write(eqc, EQC_EQE_BA_PG_SZ,
		     to_hr_hw_page_shift(eq->mtr.hem_cfg.ba_pg_shift));
	hr_reg_write(eqc, EQC_EQE_BUF_PG_SZ,
		     to_hr_hw_page_shift(eq->mtr.hem_cfg.buf_pg_shift));
	hr_reg_write(eqc, EQC_EQ_PROD_INDX, HNS_ROCE_EQ_INIT_PROD_IDX);
	hr_reg_write(eqc, EQC_EQ_MAX_CNT, eq->eq_max_cnt);

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		if (eq->eq_period * HNS_ROCE_CLOCK_ADJUST > USHRT_MAX) {
			dev_info(hr_dev->dev, "eq_period(%u) reached the upper limit, adjusted to 65.\n",
				 eq->eq_period);
			eq->eq_period = HNS_ROCE_MAX_EQ_PERIOD;
		}
		eq->eq_period *= HNS_ROCE_CLOCK_ADJUST;
	}

	hr_reg_write(eqc, EQC_EQ_PERIOD, eq->eq_period);
	hr_reg_write(eqc, EQC_EQE_REPORT_TIMER, HNS_ROCE_EQ_INIT_REPORT_TIMER);
	hr_reg_write(eqc, EQC_EQE_BA_L, bt_ba >> 3);
	hr_reg_write(eqc, EQC_EQE_BA_H, bt_ba >> 35);
	hr_reg_write(eqc, EQC_SHIFT, eq->shift);
	hr_reg_write(eqc, EQC_MSI_INDX, HNS_ROCE_EQ_INIT_MSI_IDX);
	hr_reg_write(eqc, EQC_CUR_EQE_BA_L, eqe_ba[0] >> 12);
	hr_reg_write(eqc, EQC_CUR_EQE_BA_M, eqe_ba[0] >> 28);
	hr_reg_write(eqc, EQC_CUR_EQE_BA_H, eqe_ba[0] >> 60);
	hr_reg_write(eqc, EQC_EQ_CONS_INDX, HNS_ROCE_EQ_INIT_CONS_IDX);
	hr_reg_write(eqc, EQC_NEX_EQE_BA_L, eqe_ba[1] >> 12);
	hr_reg_write(eqc, EQC_NEX_EQE_BA_H, eqe_ba[1] >> 44);
	hr_reg_write(eqc, EQC_EQE_SIZE, eq->eqe_size == HNS_ROCE_V3_EQE_SIZE);

	return 0;
}

static int alloc_eq_buf(struct hns_roce_dev *hr_dev, struct hns_roce_eq *eq)
{
	struct hns_roce_buf_attr buf_attr = {};
	int err;

	if (hr_dev->caps.eqe_hop_num == HNS_ROCE_HOP_NUM_0)
		eq->hop_num = 0;
	else
		eq->hop_num = hr_dev->caps.eqe_hop_num;

	buf_attr.page_shift = hr_dev->caps.eqe_buf_pg_sz + PAGE_SHIFT;
	buf_attr.region[0].size = eq->entries * eq->eqe_size;
	buf_attr.region[0].hopnum = eq->hop_num;
	buf_attr.region_count = 1;

	err = hns_roce_mtr_create(hr_dev, &eq->mtr, &buf_attr,
				  hr_dev->caps.eqe_ba_pg_sz + PAGE_SHIFT, NULL,
				  0);
	if (err)
		dev_err(hr_dev->dev, "failed to alloc EQE mtr, err %d\n", err);

	return err;
}

static int hns_roce_v2_create_eq(struct hns_roce_dev *hr_dev,
				 struct hns_roce_eq *eq, u8 eq_cmd)
{
	struct hns_roce_cmd_mailbox *mailbox;
	int ret;

	/* Allocate mailbox memory */
	mailbox = hns_roce_alloc_cmd_mailbox(hr_dev);
	if (IS_ERR(mailbox))
		return PTR_ERR(mailbox);

	ret = alloc_eq_buf(hr_dev, eq);
	if (ret)
		goto free_cmd_mbox;

	ret = config_eqc(hr_dev, eq, mailbox->buf);
	if (ret)
		goto err_cmd_mbox;

	ret = hns_roce_create_hw_ctx(hr_dev, mailbox, eq_cmd, eq->eqn);
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
			 "hns-%s-abn-%d", pci_name(hr_dev->pci_dev), j);

	for (j = other_num; j < (other_num + aeq_num); j++)
		snprintf((char *)hr_dev->irq_names[j], HNS_ROCE_INT_NAME_LEN,
			 "hns-%s-aeq-%d", pci_name(hr_dev->pci_dev), j - other_num);

	for (j = (other_num + aeq_num); j < irq_num; j++)
		snprintf((char *)hr_dev->irq_names[j], HNS_ROCE_INT_NAME_LEN,
			 "hns-%s-ceq-%d", pci_name(hr_dev->pci_dev),
			 j - other_num - aeq_num);

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
			dev_err(hr_dev->dev, "request irq error!\n");
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
	int other_num;
	int comp_num;
	int aeq_num;
	int irq_num;
	int eq_num;
	u8 eq_cmd;
	int ret;
	int i;

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
			eq->eqe_size = hr_dev->caps.ceqe_size;
			eq->irq = hr_dev->irq[i + other_num + aeq_num];
			eq->eq_max_cnt = HNS_ROCE_CEQ_DEFAULT_BURST_NUM;
			eq->eq_period = HNS_ROCE_CEQ_DEFAULT_INTERVAL;
		} else {
			/* AEQ */
			eq_cmd = HNS_ROCE_CMD_CREATE_AEQC;
			eq->type_flag = HNS_ROCE_AEQ;
			eq->entries = hr_dev->caps.aeqe_depth;
			eq->eqe_size = hr_dev->caps.aeqe_size;
			eq->irq = hr_dev->irq[i - comp_num + other_num];
			eq->eq_max_cnt = HNS_ROCE_AEQ_DEFAULT_BURST_NUM;
			eq->eq_period = HNS_ROCE_AEQ_DEFAULT_INTERVAL;
		}

		ret = hns_roce_v2_create_eq(hr_dev, eq, eq_cmd);
		if (ret) {
			dev_err(dev, "failed to create eq.\n");
			goto err_create_eq_fail;
		}
	}

	INIT_WORK(&hr_dev->ecc_work, fmea_ram_ecc_work);

	hr_dev->irq_workq = alloc_ordered_workqueue("hns_roce_irq_workq", 0);
	if (!hr_dev->irq_workq) {
		dev_err(dev, "failed to create irq workqueue.\n");
		ret = -ENOMEM;
		goto err_create_eq_fail;
	}

	ret = __hns_roce_request_irq(hr_dev, irq_num, comp_num, aeq_num,
				     other_num);
	if (ret) {
		dev_err(dev, "failed to request irq.\n");
		goto err_request_irq_fail;
	}

	/* enable irq */
	hns_roce_v2_int_mask_enable(hr_dev, eq_num, EQ_ENABLE);

	return 0;

err_request_irq_fail:
	destroy_workqueue(hr_dev->irq_workq);

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
	destroy_workqueue(hr_dev->irq_workq);

	for (i = 0; i < eq_num; i++) {
		hns_roce_v2_destroy_eqc(hr_dev, i);

		free_eq_buf(hr_dev, &eq_table->eq[i]);
	}

	kfree(eq_table->eq);
}

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
	.post_mbox = v2_post_mbox,
	.poll_mbox_done = v2_poll_mbox_done,
	.chk_mbox_avail = v2_chk_mbox_is_avail,
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
	.dereg_mr = hns_roce_v2_dereg_mr,
	.qp_flow_control_init = hns_roce_v2_qp_flow_control_init,
	.init_eq = hns_roce_v2_init_eq_table,
	.cleanup_eq = hns_roce_v2_cleanup_eq_table,
	.write_srqc = hns_roce_v2_write_srqc,
	.query_cqc = hns_roce_v2_query_cqc,
	.query_qpc = hns_roce_v2_query_qpc,
	.query_mpt = hns_roce_v2_query_mpt,
	.query_srqc = hns_roce_v2_query_srqc,
	.query_hw_counter = hns_roce_hw_v2_query_counter,
	.hns_roce_dev_ops = &hns_roce_v2_dev_ops,
	.hns_roce_dev_srq_ops = &hns_roce_v2_dev_srq_ops,
};

static const struct pci_device_id hns_roce_hw_v2_pci_tbl[] = {
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_25GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_50GE_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_100G_RDMA_MACSEC), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_200G_RDMA), 0},
	{PCI_VDEVICE(HUAWEI, HNAE3_DEV_ID_RDMA_DCB_PFC_VF),
	 HNAE3_DEV_SUPPORT_ROCE_DCB_BITS},
	/* required last entry */
	{0, }
};

MODULE_DEVICE_TABLE(pci, hns_roce_hw_v2_pci_tbl);

static void hns_roce_hw_v2_get_cfg(struct hns_roce_dev *hr_dev,
				  struct hnae3_handle *handle)
{
	struct hns_roce_v2_priv *priv = hr_dev->priv;
	const struct pci_device_id *id;
	int i;

	hr_dev->pci_dev = handle->pdev;
	id = pci_match_id(hns_roce_hw_v2_pci_tbl, hr_dev->pci_dev);
	hr_dev->is_vf = id->driver_data;
	hr_dev->dev = &handle->pdev->dev;
	hr_dev->hw = &hns_roce_hw_v2;
	hr_dev->sdb_offset = ROCEE_DB_SQ_L_0_REG;
	hr_dev->odb_offset = hr_dev->sdb_offset;

	/* Get info from NIC driver. */
	hr_dev->reg_base = handle->rinfo.roce_io_base;
	hr_dev->mem_base = handle->rinfo.roce_mem_base;
	hr_dev->caps.num_ports = 1;
	hr_dev->iboe.netdevs[0] = handle->rinfo.netdev;
	hr_dev->iboe.phy_port[0] = 0;

	addrconf_addr_eui48((u8 *)&hr_dev->ib_dev.node_guid,
			    hr_dev->iboe.netdevs[0]->dev_addr);

	for (i = 0; i < handle->rinfo.num_vectors; i++)
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
		goto error_failed_roce_init;
	}

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08) {
		ret = free_mr_init(hr_dev);
		if (ret) {
			dev_err(hr_dev->dev, "failed to init free mr!\n");
			goto error_failed_free_mr_init;
		}
	}

	handle->priv = hr_dev;

	return 0;

error_failed_free_mr_init:
	hns_roce_exit(hr_dev);

error_failed_roce_init:
	kfree(hr_dev->priv);

error_failed_kzalloc:
	ib_dealloc_device(&hr_dev->ib_dev);

	return ret;
}

static void __hns_roce_hw_v2_uninit_instance(struct hnae3_handle *handle,
					   bool reset)
{
	struct hns_roce_dev *hr_dev = handle->priv;

	if (!hr_dev)
		return;

	handle->priv = NULL;

	hr_dev->state = HNS_ROCE_DEVICE_STATE_UNINIT;
	hns_roce_handle_device_err(hr_dev);

	if (hr_dev->pci_dev->revision == PCI_REVISION_ID_HIP08)
		free_mr_exit(hr_dev);

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

	if (id->driver_data && handle->pdev->revision == PCI_REVISION_ID_HIP08)
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

	hr_dev = handle->priv;
	if (!hr_dev)
		return 0;

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
		dev_info(dev, "reset done, RoCE client reinit finished.\n");
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
	hns_roce_init_debugfs();
	return hnae3_register_client(&hns_roce_hw_v2_client);
}

static void __exit hns_roce_hw_v2_exit(void)
{
	hnae3_unregister_client(&hns_roce_hw_v2_client);
	hns_roce_cleanup_debugfs();
}

module_init(hns_roce_hw_v2_init);
module_exit(hns_roce_hw_v2_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Wei Hu <xavier.huwei@huawei.com>");
MODULE_AUTHOR("Lijun Ou <oulijun@huawei.com>");
MODULE_AUTHOR("Shaobo Xu <xushaobo2@huawei.com>");
MODULE_DESCRIPTION("Hisilicon Hip08 Family RoCE Driver");
