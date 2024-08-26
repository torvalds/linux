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
#include <rdma/uverbs_ioctl.h>
#include "hw_counters.h"
#include <linux/hashtable.h>
#define ROCE_DRV_MODULE_NAME		"bnxt_re"

#define BNXT_RE_DESC	"Broadcom NetXtreme-C/E RoCE Driver"

#define BNXT_RE_PAGE_SHIFT_1G		(30)
#define BNXT_RE_PAGE_SIZE_SUPPORTED	0x7FFFF000 /* 4kb - 1G */

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

struct bnxt_re_ring_attr {
	dma_addr_t	*dma_arr;
	int		pages;
	int		type;
	u32		depth;
	u32		lrid; /* Logical ring id */
	u8		mode;
};

struct bnxt_re_sqp_entries {
	struct bnxt_qplib_sge sge;
	u64 wrid;
	/* For storing the actual qp1 cqe */
	struct bnxt_qplib_cqe cqe;
	struct bnxt_re_qp *qp1_qp;
};

#define BNXT_RE_MAX_GSI_SQP_ENTRIES	1024
struct bnxt_re_gsi_context {
	struct	bnxt_re_qp *gsi_qp;
	struct	bnxt_re_qp *gsi_sqp;
	struct	bnxt_re_ah *gsi_sah;
	struct	bnxt_re_sqp_entries *sqp_tbl;
};

#define BNXT_RE_AEQ_IDX			0
#define BNXT_RE_NQ_IDX			1
#define BNXT_RE_GEN_P5_MAX_VF		64

struct bnxt_re_pacing {
	u64 dbr_db_fifo_reg_off;
	void *dbr_page;
	u64 dbr_bar_addr;
	u32 pacing_algo_th;
	u32 do_pacing_save;
	u32 dbq_pacing_time; /* ms */
	u32 dbr_def_do_pacing;
	bool dbr_pacing;
	struct mutex dbq_lock; /* synchronize db pacing algo */
};

#define BNXT_RE_MAX_DBR_DO_PACING 0xFFFF
#define BNXT_RE_DBR_PACING_TIME 5 /* ms */
#define BNXT_RE_PACING_ALGO_THRESHOLD 250 /* Entries in DB FIFO */
#define BNXT_RE_PACING_ALARM_TH_MULTIPLE 2 /* Multiple of pacing algo threshold */
/* Default do_pacing value when there is no congestion */
#define BNXT_RE_DBR_DO_PACING_NO_CONGESTION 0x7F /* 1 in 512 probability */

#define BNXT_RE_MAX_FIFO_DEPTH_P5       0x2c00
#define BNXT_RE_MAX_FIFO_DEPTH_P7       0x8000

#define BNXT_RE_MAX_FIFO_DEPTH(ctx)	\
	(bnxt_qplib_is_chip_gen_p7((ctx)) ? \
	 BNXT_RE_MAX_FIFO_DEPTH_P7 :\
	 BNXT_RE_MAX_FIFO_DEPTH_P5)

#define BNXT_RE_GRC_FIFO_REG_BASE 0x2000

#define MAX_CQ_HASH_BITS		(16)
struct bnxt_re_dev {
	struct ib_device		ibdev;
	struct list_head		list;
	unsigned long			flags;
#define BNXT_RE_FLAG_NETDEV_REGISTERED		0
#define BNXT_RE_FLAG_HAVE_L2_REF		3
#define BNXT_RE_FLAG_RCFW_CHANNEL_EN		4
#define BNXT_RE_FLAG_QOS_WORK_REG		5
#define BNXT_RE_FLAG_RESOURCES_ALLOCATED	7
#define BNXT_RE_FLAG_RESOURCES_INITIALIZED	8
#define BNXT_RE_FLAG_ERR_DEVICE_DETACHED       17
#define BNXT_RE_FLAG_ISSUE_ROCE_STATS          29
	struct net_device		*netdev;
	struct notifier_block		nb;
	unsigned int			version, major, minor;
	struct bnxt_qplib_chip_ctx	*chip_ctx;
	struct bnxt_en_dev		*en_dev;
	int				num_msix;

	int				id;

	struct delayed_work		worker;
	u8				cur_prio_map;

	/* FP Notification Queue (CQ & SRQ) */
	struct tasklet_struct		nq_task;

	/* RCFW Channel */
	struct bnxt_qplib_rcfw		rcfw;

	/* NQ */
	struct bnxt_qplib_nq		nq[BNXT_MAX_ROCE_MSIX];

	/* Device Resources */
	struct bnxt_qplib_dev_attr	dev_attr;
	struct bnxt_qplib_ctx		qplib_ctx;
	struct bnxt_qplib_res		qplib_res;
	struct bnxt_qplib_dpi		dpi_privileged;

	struct mutex			qp_lock;	/* protect qp list */
	struct list_head		qp_list;

	/* Max of 2 lossless traffic class supported per port */
	u16				cosq[2];

	/* QP for handling QP1 packets */
	struct bnxt_re_gsi_context	gsi_ctx;
	struct bnxt_re_stats		stats;
	atomic_t nq_alloc_cnt;
	u32 is_virtfn;
	u32 num_vfs;
	struct bnxt_re_pacing pacing;
	struct work_struct dbq_fifo_check_work;
	struct delayed_work dbq_pacing_work;
	DECLARE_HASHTABLE(cq_hash, MAX_CQ_HASH_BITS);
};

#define to_bnxt_re_dev(ptr, member)	\
	container_of((ptr), struct bnxt_re_dev, member)

#define BNXT_RE_ROCE_V1_PACKET		0
#define BNXT_RE_ROCEV2_IPV4_PACKET	2
#define BNXT_RE_ROCEV2_IPV6_PACKET	3

#define BNXT_RE_CHECK_RC(x) ((x) && ((x) != -ETIMEDOUT))
void bnxt_re_pacing_alert(struct bnxt_re_dev *rdev);

static inline struct device *rdev_to_dev(struct bnxt_re_dev *rdev)
{
	if (rdev)
		return  &rdev->ibdev.dev;
	return NULL;
}

extern const struct uapi_definition bnxt_re_uapi_defs[];
#endif
