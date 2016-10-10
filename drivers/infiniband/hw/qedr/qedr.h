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
#include "qedr_hsi.h"

#define QEDR_MODULE_VERSION	"8.10.10.0"
#define QEDR_NODE_DESC "QLogic 579xx RoCE HCA"
#define DP_NAME(dev) ((dev)->ibdev.name)

#define DP_DEBUG(dev, module, fmt, ...)					\
	pr_debug("(%s) " module ": " fmt,				\
		 DP_NAME(dev) ? DP_NAME(dev) : "", ## __VA_ARGS__)

#define QEDR_MSG_INIT "INIT"

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

#define QEDR_UVERBS(CMD_NAME) (1ull << IB_USER_VERBS_CMD_##CMD_NAME)

static inline struct qedr_dev *get_qedr_dev(struct ib_device *ibdev)
{
	return container_of(ibdev, struct qedr_dev, ibdev);
}

#endif
