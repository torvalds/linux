/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright 2014 Cisco Systems, Inc.  All rights reserved. */

#ifndef _CQ_ENET_DESC_H_
#define _CQ_ENET_DESC_H_

#include "cq_desc.h"

/* Ethernet completion queue descriptor: 16B */
struct cq_enet_wq_desc {
	__le16 completed_index;
	__le16 q_number;
	u8 reserved[11];
	u8 type_color;
};

static inline void cq_enet_wq_desc_dec(struct cq_enet_wq_desc *desc,
	u8 *type, u8 *color, u16 *q_number, u16 *completed_index)
{
	cq_desc_dec((struct cq_desc *)desc, type,
		color, q_number, completed_index);
}

#endif /* _CQ_ENET_DESC_H_ */
