/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_FG_H__
#define __MDP_REG_FG_H__

#define MDP_FG_TRIGGER			(0x0)
#define MDP_FG_FG_CTRL_0		(0x20)
#define MDP_FG_FG_CK_EN			(0x24)
#define MDP_FG_TILE_INFO_0		(0x418)
#define MDP_FG_TILE_INFO_1		(0x41c)

/* MASK */
#define MDP_FG_TRIGGER_MASK		(0x00000007)
#define MDP_FG_FG_CTRL_0_MASK		(0x00000033)
#define MDP_FG_FG_CK_EN_MASK		(0x0000000F)
#define MDP_FG_TILE_INFO_0_MASK		(0xFFFFFFFF)
#define MDP_FG_TILE_INFO_1_MASK		(0xFFFFFFFF)

#endif  //__MDP_REG_FG_H__
