/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_OVL_H__
#define __MDP_REG_OVL_H__

#define MDP_OVL_EN			(0x00c)
#define MDP_OVL_ROI_SIZE		(0x020)
#define MDP_OVL_DP_CON			(0x024)
#define MDP_OVL_SRC_CON			(0x02c)
#define MDP_OVL_L0_CON			(0x030)
#define MDP_OVL_L0_SRC_SIZE		(0x038)

/* MASK */
#define MDP_OVL_DP_CON_MASK		(0x0FFFFFFF)
#define MDP_OVL_EN_MASK			(0xB07D07B1)
#define MDP_OVL_L0_CON_MASK		(0xFFFFFFFF)
#define MDP_OVL_L0_SRC_SIZE_MASK	(0x1FFF1FFF)
#define MDP_OVL_ROI_SIZE_MASK		(0x1FFF1FFF)
#define MDP_OVL_SRC_CON_MASK		(0x0000031F)

#endif  //__MDP_REG_OVL_H__
