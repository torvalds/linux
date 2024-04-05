/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_HDR_H__
#define __MDP_REG_HDR_H__

#define MDP_HDR_TOP			(0x000)
#define MDP_HDR_RELAY			(0x004)
#define MDP_HDR_SIZE_0			(0x014)
#define MDP_HDR_SIZE_1			(0x018)
#define MDP_HDR_SIZE_2			(0x01C)
#define MDP_HDR_HIST_CTRL_0		(0x020)
#define MDP_HDR_HIST_CTRL_1		(0x024)
#define MDP_HDR_HIST_ADDR		(0x0DC)
#define MDP_HDR_TILE_POS		(0x118)

/* MASK */
#define MDP_HDR_RELAY_MASK		(0x01)
#define MDP_HDR_TOP_MASK		(0xFF0FEB6D)
#define MDP_HDR_SIZE_0_MASK		(0x1FFF1FFF)
#define MDP_HDR_SIZE_1_MASK		(0x1FFF1FFF)
#define MDP_HDR_SIZE_2_MASK		(0x1FFF1FFF)
#define MDP_HDR_HIST_CTRL_0_MASK	(0x1FFF1FFF)
#define MDP_HDR_HIST_CTRL_1_MASK	(0x1FFF1FFF)
#define MDP_HDR_HIST_ADDR_MASK		(0xBF3F2F3F)
#define MDP_HDR_TILE_POS_MASK		(0x1FFF1FFF)

#endif // __MDP_REG_HDR_H__
