/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

/* PPE hardware register and table declarations. */
#ifndef __PPE_REGS_H__
#define __PPE_REGS_H__

#include <linux/bitfield.h>

/* There are 15 BM ports and 4 BM groups supported by PPE.
 * BM port (0-7) is for EDMA port 0, BM port (8-13) is for
 * PPE physical port 1-6 and BM port 14 is for EIP port.
 */
#define PPE_BM_PORT_FC_MODE_ADDR		0x600100
#define PPE_BM_PORT_FC_MODE_ENTRIES		15
#define PPE_BM_PORT_FC_MODE_INC			0x4
#define PPE_BM_PORT_FC_MODE_EN			BIT(0)

#define PPE_BM_PORT_GROUP_ID_ADDR		0x600180
#define PPE_BM_PORT_GROUP_ID_ENTRIES		15
#define PPE_BM_PORT_GROUP_ID_INC		0x4
#define PPE_BM_PORT_GROUP_ID_SHARED_GROUP_ID	GENMASK(1, 0)

#define PPE_BM_SHARED_GROUP_CFG_ADDR		0x600290
#define PPE_BM_SHARED_GROUP_CFG_ENTRIES		4
#define PPE_BM_SHARED_GROUP_CFG_INC		0x4
#define PPE_BM_SHARED_GROUP_CFG_SHARED_LIMIT	GENMASK(10, 0)

#define PPE_BM_PORT_FC_CFG_TBL_ADDR		0x601000
#define PPE_BM_PORT_FC_CFG_TBL_ENTRIES		15
#define PPE_BM_PORT_FC_CFG_TBL_INC		0x10
#define PPE_BM_PORT_FC_W0_REACT_LIMIT		GENMASK(8, 0)
#define PPE_BM_PORT_FC_W0_RESUME_THRESHOLD	GENMASK(17, 9)
#define PPE_BM_PORT_FC_W0_RESUME_OFFSET		GENMASK(28, 18)
#define PPE_BM_PORT_FC_W0_CEILING_LOW		GENMASK(31, 29)
#define PPE_BM_PORT_FC_W1_CEILING_HIGH		GENMASK(7, 0)
#define PPE_BM_PORT_FC_W1_WEIGHT		GENMASK(10, 8)
#define PPE_BM_PORT_FC_W1_DYNAMIC		BIT(11)
#define PPE_BM_PORT_FC_W1_PRE_ALLOC		GENMASK(22, 12)

#define PPE_BM_PORT_FC_SET_REACT_LIMIT(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W0_REACT_LIMIT, tbl_cfg, value)
#define PPE_BM_PORT_FC_SET_RESUME_THRESHOLD(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W0_RESUME_THRESHOLD, tbl_cfg, value)
#define PPE_BM_PORT_FC_SET_RESUME_OFFSET(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W0_RESUME_OFFSET, tbl_cfg, value)
#define PPE_BM_PORT_FC_SET_CEILING_LOW(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W0_CEILING_LOW, tbl_cfg, value)
#define PPE_BM_PORT_FC_SET_CEILING_HIGH(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W1_CEILING_HIGH, (tbl_cfg) + 0x1, value)
#define PPE_BM_PORT_FC_SET_WEIGHT(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W1_WEIGHT, (tbl_cfg) + 0x1, value)
#define PPE_BM_PORT_FC_SET_DYNAMIC(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W1_DYNAMIC, (tbl_cfg) + 0x1, value)
#define PPE_BM_PORT_FC_SET_PRE_ALLOC(tbl_cfg, value)	\
	FIELD_MODIFY(PPE_BM_PORT_FC_W1_PRE_ALLOC, (tbl_cfg) + 0x1, value)
#endif
