/*
 * Huawei HiNIC PCI Express Linux driver
 * Copyright(c) 2017 Huawei Technologies Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 */

#ifndef HINIC_HW_QP_CTXT_H
#define HINIC_HW_QP_CTXT_H

#include <linux/types.h>

#include "hinic_hw_cmdq.h"

#define HINIC_SQ_CTXT_CEQ_ATTR_GLOBAL_SQ_ID_SHIFT       13
#define HINIC_SQ_CTXT_CEQ_ATTR_EN_SHIFT                 23

#define HINIC_SQ_CTXT_CEQ_ATTR_GLOBAL_SQ_ID_MASK        0x3FF
#define HINIC_SQ_CTXT_CEQ_ATTR_EN_MASK                  0x1

#define HINIC_SQ_CTXT_CEQ_ATTR_SET(val, member)         \
	(((u32)(val) & HINIC_SQ_CTXT_CEQ_ATTR_##member##_MASK) \
	 << HINIC_SQ_CTXT_CEQ_ATTR_##member##_SHIFT)

#define HINIC_SQ_CTXT_CI_IDX_SHIFT                      11
#define HINIC_SQ_CTXT_CI_WRAPPED_SHIFT                  23

#define HINIC_SQ_CTXT_CI_IDX_MASK                       0xFFF
#define HINIC_SQ_CTXT_CI_WRAPPED_MASK                   0x1

#define HINIC_SQ_CTXT_CI_SET(val, member)               \
	(((u32)(val) & HINIC_SQ_CTXT_CI_##member##_MASK) \
	 << HINIC_SQ_CTXT_CI_##member##_SHIFT)

#define HINIC_SQ_CTXT_WQ_PAGE_HI_PFN_SHIFT              0
#define HINIC_SQ_CTXT_WQ_PAGE_PI_SHIFT                  20

#define HINIC_SQ_CTXT_WQ_PAGE_HI_PFN_MASK               0xFFFFF
#define HINIC_SQ_CTXT_WQ_PAGE_PI_MASK                   0xFFF

#define HINIC_SQ_CTXT_WQ_PAGE_SET(val, member)          \
	(((u32)(val) & HINIC_SQ_CTXT_WQ_PAGE_##member##_MASK) \
	 << HINIC_SQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define HINIC_SQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT        0
#define HINIC_SQ_CTXT_PREF_CACHE_MAX_SHIFT              14
#define HINIC_SQ_CTXT_PREF_CACHE_MIN_SHIFT              25

#define HINIC_SQ_CTXT_PREF_CACHE_THRESHOLD_MASK         0x3FFF
#define HINIC_SQ_CTXT_PREF_CACHE_MAX_MASK               0x7FF
#define HINIC_SQ_CTXT_PREF_CACHE_MIN_MASK               0x7F

#define HINIC_SQ_CTXT_PREF_WQ_HI_PFN_SHIFT              0
#define HINIC_SQ_CTXT_PREF_CI_SHIFT                     20

#define HINIC_SQ_CTXT_PREF_WQ_HI_PFN_MASK               0xFFFFF
#define HINIC_SQ_CTXT_PREF_CI_MASK                      0xFFF

#define HINIC_SQ_CTXT_PREF_SET(val, member)             \
	(((u32)(val) & HINIC_SQ_CTXT_PREF_##member##_MASK) \
	 << HINIC_SQ_CTXT_PREF_##member##_SHIFT)

#define HINIC_SQ_CTXT_WQ_BLOCK_HI_PFN_SHIFT             0

#define HINIC_SQ_CTXT_WQ_BLOCK_HI_PFN_MASK              0x7FFFFF

#define HINIC_SQ_CTXT_WQ_BLOCK_SET(val, member)         \
	(((u32)(val) & HINIC_SQ_CTXT_WQ_BLOCK_##member##_MASK) \
	 << HINIC_SQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define HINIC_RQ_CTXT_CEQ_ATTR_EN_SHIFT                 0
#define HINIC_RQ_CTXT_CEQ_ATTR_WRAPPED_SHIFT            1

#define HINIC_RQ_CTXT_CEQ_ATTR_EN_MASK                  0x1
#define HINIC_RQ_CTXT_CEQ_ATTR_WRAPPED_MASK             0x1

#define HINIC_RQ_CTXT_CEQ_ATTR_SET(val, member)         \
	(((u32)(val) & HINIC_RQ_CTXT_CEQ_ATTR_##member##_MASK) \
	 << HINIC_RQ_CTXT_CEQ_ATTR_##member##_SHIFT)

#define HINIC_RQ_CTXT_PI_IDX_SHIFT                      0
#define HINIC_RQ_CTXT_PI_INTR_SHIFT                     22

#define HINIC_RQ_CTXT_PI_IDX_MASK                       0xFFF
#define HINIC_RQ_CTXT_PI_INTR_MASK                      0x3FF

#define HINIC_RQ_CTXT_PI_SET(val, member)               \
	(((u32)(val) & HINIC_RQ_CTXT_PI_##member##_MASK) << \
	 HINIC_RQ_CTXT_PI_##member##_SHIFT)

#define HINIC_RQ_CTXT_WQ_PAGE_HI_PFN_SHIFT              0
#define HINIC_RQ_CTXT_WQ_PAGE_CI_SHIFT                  20

#define HINIC_RQ_CTXT_WQ_PAGE_HI_PFN_MASK               0xFFFFF
#define HINIC_RQ_CTXT_WQ_PAGE_CI_MASK                   0xFFF

#define HINIC_RQ_CTXT_WQ_PAGE_SET(val, member)          \
	(((u32)(val) & HINIC_RQ_CTXT_WQ_PAGE_##member##_MASK) << \
	 HINIC_RQ_CTXT_WQ_PAGE_##member##_SHIFT)

#define HINIC_RQ_CTXT_PREF_CACHE_THRESHOLD_SHIFT        0
#define HINIC_RQ_CTXT_PREF_CACHE_MAX_SHIFT              14
#define HINIC_RQ_CTXT_PREF_CACHE_MIN_SHIFT              25

#define HINIC_RQ_CTXT_PREF_CACHE_THRESHOLD_MASK         0x3FFF
#define HINIC_RQ_CTXT_PREF_CACHE_MAX_MASK               0x7FF
#define HINIC_RQ_CTXT_PREF_CACHE_MIN_MASK               0x7F

#define HINIC_RQ_CTXT_PREF_WQ_HI_PFN_SHIFT              0
#define HINIC_RQ_CTXT_PREF_CI_SHIFT                     20

#define HINIC_RQ_CTXT_PREF_WQ_HI_PFN_MASK               0xFFFFF
#define HINIC_RQ_CTXT_PREF_CI_MASK                      0xFFF

#define HINIC_RQ_CTXT_PREF_SET(val, member)             \
	(((u32)(val) & HINIC_RQ_CTXT_PREF_##member##_MASK) << \
	 HINIC_RQ_CTXT_PREF_##member##_SHIFT)

#define HINIC_RQ_CTXT_WQ_BLOCK_HI_PFN_SHIFT             0

#define HINIC_RQ_CTXT_WQ_BLOCK_HI_PFN_MASK              0x7FFFFF

#define HINIC_RQ_CTXT_WQ_BLOCK_SET(val, member)         \
	(((u32)(val) & HINIC_RQ_CTXT_WQ_BLOCK_##member##_MASK) << \
	 HINIC_RQ_CTXT_WQ_BLOCK_##member##_SHIFT)

#define HINIC_SQ_CTXT_SIZE(num_sqs) (sizeof(struct hinic_qp_ctxt_header) \
				     + (num_sqs) * sizeof(struct hinic_sq_ctxt))

#define HINIC_RQ_CTXT_SIZE(num_rqs) (sizeof(struct hinic_qp_ctxt_header) \
				     + (num_rqs) * sizeof(struct hinic_rq_ctxt))

#define HINIC_WQ_PAGE_PFN_SHIFT         12
#define HINIC_WQ_BLOCK_PFN_SHIFT        9

#define HINIC_WQ_PAGE_PFN(page_addr)    ((page_addr) >> HINIC_WQ_PAGE_PFN_SHIFT)
#define HINIC_WQ_BLOCK_PFN(page_addr)   ((page_addr) >> \
					 HINIC_WQ_BLOCK_PFN_SHIFT)

#define HINIC_Q_CTXT_MAX                \
		((HINIC_CMDQ_BUF_SIZE - sizeof(struct hinic_qp_ctxt_header)) \
		 / sizeof(struct hinic_sq_ctxt))

enum hinic_qp_ctxt_type {
	HINIC_QP_CTXT_TYPE_SQ,
	HINIC_QP_CTXT_TYPE_RQ
};

struct hinic_qp_ctxt_header {
	u16     num_queues;
	u16     queue_type;
	u32     addr_offset;
};

struct hinic_sq_ctxt {
	u32     ceq_attr;

	u32     ci_wrapped;

	u32     wq_hi_pfn_pi;
	u32     wq_lo_pfn;

	u32     pref_cache;
	u32     pref_wrapped;
	u32     pref_wq_hi_pfn_ci;
	u32     pref_wq_lo_pfn;

	u32     rsvd0;
	u32     rsvd1;

	u32     wq_block_hi_pfn;
	u32     wq_block_lo_pfn;
};

struct hinic_rq_ctxt {
	u32     ceq_attr;

	u32     pi_intr_attr;

	u32     wq_hi_pfn_ci;
	u32     wq_lo_pfn;

	u32     pref_cache;
	u32     pref_wrapped;

	u32     pref_wq_hi_pfn_ci;
	u32     pref_wq_lo_pfn;

	u32     pi_paddr_hi;
	u32     pi_paddr_lo;

	u32     wq_block_hi_pfn;
	u32     wq_block_lo_pfn;
};

struct hinic_sq_ctxt_block {
	struct hinic_qp_ctxt_header hdr;
	struct hinic_sq_ctxt sq_ctxt[HINIC_Q_CTXT_MAX];
};

struct hinic_rq_ctxt_block {
	struct hinic_qp_ctxt_header hdr;
	struct hinic_rq_ctxt rq_ctxt[HINIC_Q_CTXT_MAX];
};

#endif
