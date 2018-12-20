/*
 * Broadcom NetXtreme-E RoCE driver.
 *
 * Copyright (c) 2016 - 2017, Broadcom. All rights reserved.  The term
 * Broadcom refers to Broadcom Limited and/or its subsidiaries.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Description: Slow Path Operators (header)
 *
 */

#ifndef __BNXT_RE_H__
#define __BNXT_RE_H__
#define ROCE_DRV_MODULE_NAME		"bnxt_re"

#define BNXT_RE_DESC	"Broadcom NetXtreme-C/E RoCE Driver"
#define BNXT_RE_PAGE_SHIFT_4K		(12)
#define BNXT_RE_PAGE_SHIFT_8K		(13)
#define BNXT_RE_PAGE_SHIFT_64K		(16)
#define BNXT_RE_PAGE_SHIFT_2M		(21)
#define BNXT_RE_PAGE_SHIFT_8M		(23)
#define BNXT_RE_PAGE_SHIFT_1G		(30)

#define BNXT_RE_PAGE_SIZE_4K		BIT(BNXT_RE_PAGE_SHIFT_4K)
#define BNXT_RE_PAGE_SIZE_8K		BIT(BNXT_RE_PAGE_SHIFT_8K)
#define BNXT_RE_PAGE_SIZE_64K		BIT(BNXT_RE_PAGE_SHIFT_64K)
#define BNXT_RE_PAGE_SIZE_2M		BIT(BNXT_RE_PAGE_SHIFT_2M)
#define BNXT_RE_PAGE_SIZE_8M		BIT(BNXT_RE_PAGE_SHIFT_8M)
#define BNXT_RE_PAGE_SIZE_1G		BIT(BNXT_RE_PAGE_SHIFT_1G)

#define BNXT_RE_MAX_MR_SIZE_LOW		BIT_ULL(BNXT_RE_PAGE_SHIFT_1G)
#define BNXT_RE_MAX_MR_SIZE_HIGH	BIT_ULL(39)
#define BNXT_RE_MAX_MR_SIZE		BNXT_RE_MAX_MR_SIZE_HIGH

#define BNXT_RE_MAX_QPC_COUNT		(64 * 1024)
#define BNXT_RE_MAX_MRW_COUNT		(64 * 1024)
#define BNXT_RE_MAX_SRQC_COUNT		(64 * 1024)
#define BNXT_RE_MAX_CQ_COUNT		(64 * 1024)
#define BNXT_RE_MAX_MRW_COUNT_64K	(64 * 1024)
#define BNXT_RE_MAX_MRW_COUNT_256K	(256 * 1024)

/* Number of MRs to reserve for PF, leaving remainder for VFs */
#define BNXT_RE_RESVD_MR_FOR_PF         (32 * 1024)
#define BNXT_RE_MAX_GID_PER_VF          128

/*
 * Percentage of resources of each type reserved for PF.
 * Remaining resources are divided equally among VFs.
 * [0, 100]
 */
#define BNXT_RE_PCT_RSVD_FOR_PF         50

#define BNXT_RE_UD_QP_HW_STALL		0x400000

#define BNXT_RE_RQ_WQE_THRESHOLD	32

/*
 * Setting the default ack delay value to 16, which means
 * the default timeout is approx. 260ms(4 usec * 2 ^(timeout))
 */

#define BNXT_RE_DEFAULT_ACK_DELAY	16

struct bnxt_re_work {
	struct work_struct	work;
	unsigned long		event;
	struct bnxt_re_dev      *rdev;
	struct net_device	*vlan_dev;
};

struct bnxt_re_sqp_entries {
	struct bnxt_qplib_sge sge;
	u64 wrid;
	/* For storing the actual qp1 cqe */
	struct bnxt_qplib_cqe cqe;
	struct bnxt_re_qp *qp1_qp;
};

#define BNXT_RE_MIN_MSIX		2
#define BNXT_RE_MAX_MSIX		9
#define BNXT_RE_AEQ_IDX			0
#define BNXT_RE_NQ_IDX			1

struct bnxt_re_dev {
	struct ib_device		ibdev;
	struct list_head		list;
	unsigned long			flags;
#define BNXT_RE_FLAG_NETDEV_REGISTERED		0
#define BNXT_RE_FLAG_IBDEV_REGISTERED		1
#define BNXT_RE_FLAG_GOT_MSIX			2
#define BNXT_RE_FLAG_HAVE_L2_REF		3
#define BNXT_RE_FLAG_RCFW_CHANNEL_EN		4
#define BNXT_RE_FLAG_QOS_WORK_REG		5
#define BNXT_RE_FLAG_RESOURCES_ALLOCATED	7
#define BNXT_RE_FLAG_RESOURCES_INITIALIZED	8
#define BNXT_RE_FLAG_ISSUE_ROCE_STATS          29
	struct net_device		*netdev;
	unsigned int			version, major, minor;
	struct bnxt_en_dev		*en_dev;
	struct bnxt_msix_entry		msix_entries[BNXT_RE_MAX_MSIX];
	int				num_msix;

	int				id;

	struct delayed_work		worker;
	u8				cur_prio_map;
	u8				active_speed;
	u8				active_width;

	/* FP Notification Queue (CQ & SRQ) */
	struct tasklet_struct		nq_task;

	/* RCFW Channel */
	struct bnxt_qplib_rcfw		rcfw;

	/* NQ */
	struct bnxt_qplib_nq		nq[BNXT_RE_MAX_MSIX];

	/* Device Resources */
	struct bnxt_qplib_dev_attr	dev_attr;
	struct bnxt_qplib_ctx		qplib_ctx;
	struct bnxt_qplib_res		qplib_res;
	struct bnxt_qplib_dpi		dpi_privileged;

	atomic_t			qp_count;
	struct mutex			qp_lock;	/* protect qp list */
	struct list_head		qp_list;

	atomic_t			cq_count;
	atomic_t			srq_count;
	atomic_t			mr_count;
	atomic_t			mw_count;
	atomic_t			sched_count;
	/* Max of 2 lossless traffic class supported per port */
	u16				cosq[2];

	/* QP for for handling QP1 packets */
	u32				sqp_id;
	struct bnxt_re_qp		*qp1_sqp;
	struct bnxt_re_ah		*sqp_ah;
	struct bnxt_re_sqp_entries sqp_tbl[1024];
	atomic_t nq_alloc_cnt;
	u32 is_virtfn;
	u32 num_vfs;
	struct bnxt_qplib_roce_stats	stats;
};

#define to_bnxt_re_dev(ptr, member)	\
	container_of((ptr), struct bnxt_re_dev, member)

#define BNXT_RE_ROCE_V1_PACKET		0
#define BNXT_RE_ROCEV2_IPV4_PACKET	2
#define BNXT_RE_ROCEV2_IPV6_PACKET	3

static inline struct device *rdev_to_dev(struct bnxt_re_dev *rdev)
{
	if (rdev)
		return  &rdev->ibdev.dev;
	return NULL;
}

#endif
