/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef QMAN_IF_H
#define QMAN_IF_H

#include <linux/types.h>

/*
 * PRIMARY QUEUE
 */

struct hl_bd {
	__le64	ptr;
	__le32	len;
	__le32	ctl;
};

#define HL_BD_SIZE			sizeof(struct hl_bd)

/*
 * S/W CTL FIELDS.
 *
 * BD_CTL_REPEAT_VALID tells the CP whether the repeat field in the BD CTL is
 * valid. 1 means the repeat field is valid, 0 means not-valid,
 * i.e. repeat == 1
 */
#define BD_CTL_REPEAT_VALID_SHIFT	24
#define BD_CTL_REPEAT_VALID_MASK	0x01000000

#define BD_CTL_SHADOW_INDEX_SHIFT	0
#define BD_CTL_SHADOW_INDEX_MASK	0x00000FFF

/*
 * H/W CTL FIELDS
 */

#define BD_CTL_COMP_OFFSET_SHIFT	16
#define BD_CTL_COMP_OFFSET_MASK		0x00FF0000

#define BD_CTL_COMP_DATA_SHIFT		0
#define BD_CTL_COMP_DATA_MASK		0x0000FFFF

/*
 * COMPLETION QUEUE
 */

struct hl_cq_entry {
	__le32	data;
};

#define HL_CQ_ENTRY_SIZE		sizeof(struct hl_cq_entry)

#define CQ_ENTRY_READY_SHIFT			31
#define CQ_ENTRY_READY_MASK			0x80000000

#define CQ_ENTRY_SHADOW_INDEX_VALID_SHIFT	30
#define CQ_ENTRY_SHADOW_INDEX_VALID_MASK	0x40000000

#define CQ_ENTRY_SHADOW_INDEX_SHIFT		BD_CTL_SHADOW_INDEX_SHIFT
#define CQ_ENTRY_SHADOW_INDEX_MASK		BD_CTL_SHADOW_INDEX_MASK


#endif /* QMAN_IF_H */
