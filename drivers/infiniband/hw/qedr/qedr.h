/* QLogic qedr NIC Driver
 * Copyright (c) 2015-2016  QLogic Corporation
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
#ifndef __QEDR_H__
#define __QEDR_H__

#include <linux/pci.h>
#include <rdma/ib_addr.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_roce_if.h>
#include <linux/qed/qede_roce.h>
#include <linux/qed/roce_common.h>
#include "qedr_hsi_rdma.h"

#define QEDR_MODULE_VERSION	"8.10.10.0"
#define QEDR_NODE_DESC "QLogic 579xx RoCE HCA"
#define DP_NAME(dev) ((dev)->ibdev.name)

#define DP_DEBUG(dev, module, fmt, ...)					\
	pr_debug("(%s) " module ": " fmt,				\
		 DP_NAME(dev) ? DP_NAME(dev) : "", ## __VA_ARGS__)

#define QEDR_MSG_INIT "INIT"
#define QEDR_MSG_MISC "MISC"
#define QEDR_MSG_CQ   "  CQ"
#define QEDR_MSG_MR   "  MR"
#define QEDR_MSG_RQ   "  RQ"
#define QEDR_MSG_SQ   "  SQ"
#define QEDR_MSG_QP   "  QP"
#define QEDR_MSG_GSI  " GSI"

#define QEDR_CQ_MAGIC_NUMBER	(0x11223344)

#define FW_PAGE_SIZE		(RDMA_RING_PAGE_SIZE)
#define FW_PAGE_SHIFT		(12)

struct qedr_dev;

struct qedr_cnq {
	struct qedr_dev		*dev;
	struct qed_chain	pbl;
	struct qed_sb_info	*sb;
	char			name[32];
	u64			n_comp;
	__le16			*hw_cons_ptr;
	u8			index;
};

#define QEDR_MAX_SGID 128

struct qedr_device_attr {
	u32	vendor_id;
	u32	vendor_part_id;
	u32	hw_ver;
	u64	fw_ver;
	u64	node_guid;
	u64	sys_image_guid;
	u8	max_cnq;
	u8	max_sge;
	u16	max_inline;
	u32	max_sqe;
	u32	max_rqe;
	u8	max_qp_resp_rd_atomic_resc;
	u8	max_qp_req_rd_atomic_resc;
	u64	max_dev_resp_rd_atomic_resc;
	u32	max_cq;
	u32	max_qp;
	u32	max_mr;
	u64	max_mr_size;
	u32	max_cqe;
	u32	max_mw;
	u32	max_fmr;
	u32	max_mr_mw_fmr_pbl;
	u64	max_mr_mw_fmr_size;
	u32	max_pd;
	u32	max_ah;
	u8	max_pkey;
	u32	max_srq;
	u32	max_srq_wr;
	u8	max_srq_sge;
	u8	max_stats_queues;
	u32	dev_caps;

	u64	page_size_caps;
	u8	dev_ack_delay;
	u32	reserved_lkey;
	u32	bad_pkey_counter;
	struct qed_rdma_events events;
};

#define QEDR_ENET_STATE_BIT	(0)

struct qedr_dev {
	struct ib_device	ibdev;
	struct qed_dev		*cdev;
	struct pci_dev		*pdev;
	struct net_device	*ndev;

	enum ib_atomic_cap	atomic_cap;

	void *rdma_ctx;
	struct qedr_device_attr attr;

	const struct qed_rdma_ops *ops;
	struct qed_int_info	int_info;

	struct qed_sb_info	*sb_array;
	struct qedr_cnq		*cnq_array;
	int			num_cnq;
	int			sb_start;

	void __iomem		*db_addr;
	u64			db_phys_addr;
	u32			db_size;
	u16			dpi;

	union ib_gid *sgid_tbl;

	/* Lock for sgid table */
	spinlock_t sgid_lock;

	u64			guid;

	u32			dp_module;
	u8			dp_level;
	u8			num_hwfns;
	u8			gsi_ll2_handle;

	uint			wq_multiplier;
	u8			gsi_ll2_mac_address[ETH_ALEN];
	int			gsi_qp_created;
	struct qedr_cq		*gsi_sqcq;
	struct qedr_cq		*gsi_rqcq;
	struct qedr_qp		*gsi_qp;

	unsigned long enet_state;
};

#define QEDR_MAX_SQ_PBL			(0x8000)
#define QEDR_MAX_SQ_PBL_ENTRIES		(0x10000 / sizeof(void *))
#define QEDR_SQE_ELEMENT_SIZE		(sizeof(struct rdma_sq_sge))
#define QEDR_MAX_SQE_ELEMENTS_PER_SQE	(ROCE_REQ_MAX_SINGLE_SQ_WQE_SIZE / \
					 QEDR_SQE_ELEMENT_SIZE)
#define QEDR_MAX_SQE_ELEMENTS_PER_PAGE	((RDMA_RING_PAGE_SIZE) / \
					 QEDR_SQE_ELEMENT_SIZE)
#define QEDR_MAX_SQE			((QEDR_MAX_SQ_PBL_ENTRIES) *\
					 (RDMA_RING_PAGE_SIZE) / \
					 (QEDR_SQE_ELEMENT_SIZE) /\
					 (QEDR_MAX_SQE_ELEMENTS_PER_SQE))
/* RQ */
#define QEDR_MAX_RQ_PBL			(0x2000)
#define QEDR_MAX_RQ_PBL_ENTRIES		(0x10000 / sizeof(void *))
#define QEDR_RQE_ELEMENT_SIZE		(sizeof(struct rdma_rq_sge))
#define QEDR_MAX_RQE_ELEMENTS_PER_RQE	(RDMA_MAX_SGE_PER_RQ_WQE)
#define QEDR_MAX_RQE_ELEMENTS_PER_PAGE	((RDMA_RING_PAGE_SIZE) / \
					 QEDR_RQE_ELEMENT_SIZE)
#define QEDR_MAX_RQE			((QEDR_MAX_RQ_PBL_ENTRIES) *\
					 (RDMA_RING_PAGE_SIZE) / \
					 (QEDR_RQE_ELEMENT_SIZE) /\
					 (QEDR_MAX_RQE_ELEMENTS_PER_RQE))

#define QEDR_CQE_SIZE	(sizeof(union rdma_cqe))
#define QEDR_MAX_CQE_PBL_SIZE (512 * 1024)
#define QEDR_MAX_CQE_PBL_ENTRIES (((QEDR_MAX_CQE_PBL_SIZE) / \
				  sizeof(u64)) - 1)
#define QEDR_MAX_CQES ((u32)((QEDR_MAX_CQE_PBL_ENTRIES) * \
			     (QED_CHAIN_PAGE_SIZE) / QEDR_CQE_SIZE))

#define QEDR_ROCE_MAX_CNQ_SIZE		(0x4000)

#define QEDR_MAX_PORT			(1)
#define QEDR_PORT			(1)

#define QEDR_UVERBS(CMD_NAME) (1ull << IB_USER_VERBS_CMD_##CMD_NAME)

#define QEDR_ROCE_PKEY_MAX 1
#define QEDR_ROCE_PKEY_TABLE_LEN 1
#define QEDR_ROCE_PKEY_DEFAULT 0xffff

struct qedr_pbl {
	struct list_head list_entry;
	void *va;
	dma_addr_t pa;
};

struct qedr_ucontext {
	struct ib_ucontext ibucontext;
	struct qedr_dev *dev;
	struct qedr_pd *pd;
	u64 dpi_addr;
	u64 dpi_phys_addr;
	u32 dpi_size;
	u16 dpi;

	struct list_head mm_head;

	/* Lock to protect mm list */
	struct mutex mm_list_lock;
};

union db_prod64 {
	struct rdma_pwm_val32_data data;
	u64 raw;
};

enum qedr_cq_type {
	QEDR_CQ_TYPE_GSI,
	QEDR_CQ_TYPE_KERNEL,
	QEDR_CQ_TYPE_USER,
};

struct qedr_pbl_info {
	u32 num_pbls;
	u32 num_pbes;
	u32 pbl_size;
	u32 pbe_size;
	bool two_layered;
};

struct qedr_userq {
	struct ib_umem *umem;
	struct qedr_pbl_info pbl_info;
	struct qedr_pbl *pbl_tbl;
	u64 buf_addr;
	size_t buf_len;
};

struct qedr_cq {
	struct ib_cq ibcq;

	enum qedr_cq_type cq_type;
	u32 sig;

	u16 icid;

	/* Lock to protect multiplem CQ's */
	spinlock_t cq_lock;
	u8 arm_flags;
	struct qed_chain pbl;

	void __iomem *db_addr;
	union db_prod64 db;

	u8 pbl_toggle;
	union rdma_cqe *latest_cqe;
	union rdma_cqe *toggle_cqe;

	u32 cq_cons;

	struct qedr_userq q;
	u8 destroyed;
	u16 cnq_notif;
};

struct qedr_pd {
	struct ib_pd ibpd;
	u32 pd_id;
	struct qedr_ucontext *uctx;
};

struct qedr_mm {
	struct {
		u64 phy_addr;
		unsigned long len;
	} key;
	struct list_head entry;
};

union db_prod32 {
	struct rdma_pwm_val16_data data;
	u32 raw;
};

struct qedr_qp_hwq_info {
	/* WQE Elements */
	struct qed_chain pbl;
	u64 p_phys_addr_tbl;
	u32 max_sges;

	/* WQE */
	u16 prod;
	u16 cons;
	u16 wqe_cons;
	u16 gsi_cons;
	u16 max_wr;

	/* DB */
	void __iomem *db;
	union db_prod32 db_data;
};

#define QEDR_INC_SW_IDX(p_info, index)					\
	do {								\
		p_info->index = (p_info->index + 1) &			\
				qed_chain_get_capacity(p_info->pbl)	\
	} while (0)

enum qedr_qp_err_bitmap {
	QEDR_QP_ERR_SQ_FULL = 1,
	QEDR_QP_ERR_RQ_FULL = 2,
	QEDR_QP_ERR_BAD_SR = 4,
	QEDR_QP_ERR_BAD_RR = 8,
	QEDR_QP_ERR_SQ_PBL_FULL = 16,
	QEDR_QP_ERR_RQ_PBL_FULL = 32,
};

struct qedr_qp {
	struct ib_qp ibqp;	/* must be first */
	struct qedr_dev *dev;

	struct qedr_qp_hwq_info sq;
	struct qedr_qp_hwq_info rq;

	u32 max_inline_data;

	/* Lock for QP's */
	spinlock_t q_lock;
	struct qedr_cq *sq_cq;
	struct qedr_cq *rq_cq;
	struct qedr_srq *srq;
	enum qed_roce_qp_state state;
	u32 id;
	struct qedr_pd *pd;
	enum ib_qp_type qp_type;
	struct qed_rdma_qp *qed_qp;
	u32 qp_id;
	u16 icid;
	u16 mtu;
	int sgid_idx;
	u32 rq_psn;
	u32 sq_psn;
	u32 qkey;
	u32 dest_qp_num;

	/* Relevant to qps created from kernel space only (ULPs) */
	u8 prev_wqe_size;
	u16 wqe_cons;
	u32 err_bitmap;
	bool signaled;

	/* SQ shadow */
	struct {
		u64 wr_id;
		enum ib_wc_opcode opcode;
		u32 bytes_len;
		u8 wqe_size;
		bool signaled;
		dma_addr_t icrc_mapping;
		u32 *icrc;
		struct qedr_mr *mr;
	} *wqe_wr_id;

	/* RQ shadow */
	struct {
		u64 wr_id;
		struct ib_sge sg_list[RDMA_MAX_SGE_PER_RQ_WQE];
		u8 wqe_size;

		u8 smac[ETH_ALEN];
		u16 vlan_id;
		int rc;
	} *rqe_wr_id;

	/* Relevant to qps created from user space only (applications) */
	struct qedr_userq usq;
	struct qedr_userq urq;
};

struct qedr_ah {
	struct ib_ah ibah;
	struct rdma_ah_attr attr;
};

enum qedr_mr_type {
	QEDR_MR_USER,
	QEDR_MR_KERNEL,
	QEDR_MR_DMA,
	QEDR_MR_FRMR,
};

struct mr_info {
	struct qedr_pbl *pbl_table;
	struct qedr_pbl_info pbl_info;
	struct list_head free_pbl_list;
	struct list_head inuse_pbl_list;
	u32 completed;
	u32 completed_handled;
};

struct qedr_mr {
	struct ib_mr ibmr;
	struct ib_umem *umem;

	struct qed_rdma_register_tid_in_params hw_mr;
	enum qedr_mr_type type;

	struct qedr_dev *dev;
	struct mr_info info;

	u64 *pages;
	u32 npages;
};

#define SET_FIELD2(value, name, flag) ((value) |= ((flag) << (name ## _SHIFT)))

#define QEDR_RESP_IMM	(RDMA_CQE_RESPONDER_IMM_FLG_MASK << \
			 RDMA_CQE_RESPONDER_IMM_FLG_SHIFT)
#define QEDR_RESP_RDMA	(RDMA_CQE_RESPONDER_RDMA_FLG_MASK << \
			 RDMA_CQE_RESPONDER_RDMA_FLG_SHIFT)
#define QEDR_RESP_INV	(RDMA_CQE_RESPONDER_INV_FLG_MASK << \
			 RDMA_CQE_RESPONDER_INV_FLG_SHIFT)

static inline void qedr_inc_sw_cons(struct qedr_qp_hwq_info *info)
{
	info->cons = (info->cons + 1) % info->max_wr;
	info->wqe_cons++;
}

static inline void qedr_inc_sw_prod(struct qedr_qp_hwq_info *info)
{
	info->prod = (info->prod + 1) % info->max_wr;
}

static inline int qedr_get_dmac(struct qedr_dev *dev,
				struct rdma_ah_attr *ah_attr, u8 *mac_addr)
{
	union ib_gid zero_sgid = { { 0 } };
	struct in6_addr in6;
	const struct ib_global_route *grh = rdma_ah_read_grh(ah_attr);
	u8 *dmac;

	if (!memcmp(&grh->dgid, &zero_sgid, sizeof(union ib_gid))) {
		DP_ERR(dev, "Local port GID not supported\n");
		eth_zero_addr(mac_addr);
		return -EINVAL;
	}

	memcpy(&in6, grh->dgid.raw, sizeof(in6));
	dmac = rdma_ah_retrieve_dmac(ah_attr);
	if (!dmac)
		return -EINVAL;
	ether_addr_copy(mac_addr, dmac);

	return 0;
}

static inline
struct qedr_ucontext *get_qedr_ucontext(struct ib_ucontext *ibucontext)
{
	return container_of(ibucontext, struct qedr_ucontext, ibucontext);
}

static inline struct qedr_dev *get_qedr_dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct qedr_dev, ibdev);
}

static inline struct qedr_pd *get_qedr_pd(struct ib_pd *ibpd)
{
	return container_of(ibpd, struct qedr_pd, ibpd);
}

static inline struct qedr_cq *get_qedr_cq(struct ib_cq *ibcq)
{
	return container_of(ibcq, struct qedr_cq, ibcq);
}

static inline struct qedr_qp *get_qedr_qp(struct ib_qp *ibqp)
{
	return container_of(ibqp, struct qedr_qp, ibqp);
}

static inline struct qedr_ah *get_qedr_ah(struct ib_ah *ibah)
{
	return container_of(ibah, struct qedr_ah, ibah);
}

static inline struct qedr_mr *get_qedr_mr(struct ib_mr *ibmr)
{
	return container_of(ibmr, struct qedr_mr, ibmr);
}
#endif
