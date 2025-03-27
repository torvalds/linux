/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _CQ_DESC_H_
#define _CQ_DESC_H_

/*
 * Completion queue descriptor types
 */
enum cq_desc_types {
	CQ_DESC_TYPE_WQ_ENET = 0,
	CQ_DESC_TYPE_DESC_COPY = 1,
	CQ_DESC_TYPE_WQ_EXCH = 2,
	CQ_DESC_TYPE_RQ_ENET = 3,
	CQ_DESC_TYPE_RQ_FCP = 4,
};

/* Completion queue descriptor: 16B
 *
 * All completion queues have this basic layout.  The
 * type_specfic area is unique for each completion
 * queue type.
 */
struct cq_desc {
	__le16 completed_index;
	__le16 q_number;
	u8 type_specfic[11];
	u8 type_color;
};

#define CQ_DESC_TYPE_BITS        4
#define CQ_DESC_TYPE_MASK        ((1 << CQ_DESC_TYPE_BITS) - 1)
#define CQ_DESC_COLOR_MASK       1
#define CQ_DESC_COLOR_SHIFT      7
#define CQ_DESC_Q_NUM_BITS       10
#define CQ_DESC_Q_NUM_MASK       ((1 << CQ_DESC_Q_NUM_BITS) - 1)
#define CQ_DESC_COMP_NDX_BITS    12
#define CQ_DESC_COMP_NDX_MASK    ((1 << CQ_DESC_COMP_NDX_BITS) - 1)

#define CQ_DESC_32_FI_MASK (BIT(0) | BIT(1))
#define CQ_DESC_64_FI_MASK (BIT(0) | BIT(1))

#endif /* _CQ_DESC_H_ */
