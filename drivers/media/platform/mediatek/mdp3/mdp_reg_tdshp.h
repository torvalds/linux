/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_TDSHP_H__
#define __MDP_REG_TDSHP_H__

#define MDP_HIST_CFG_00				(0x064)
#define MDP_HIST_CFG_01				(0x068)
#define MDP_TDSHP_CTRL				(0x100)
#define MDP_TDSHP_CFG				(0x110)
#define MDP_TDSHP_INPUT_SIZE			(0x120)
#define MDP_TDSHP_OUTPUT_OFFSET			(0x124)
#define MDP_TDSHP_OUTPUT_SIZE			(0x128)
#define MDP_LUMA_HIST_INIT			(0x200)
#define MDP_DC_TWO_D_W1_RESULT_INIT		(0x260)
#define MDP_CONTOUR_HIST_INIT			(0x398)

/* MASK */
#define MDP_HIST_CFG_00_MASK			(0xFFFFFFFF)
#define MDP_HIST_CFG_01_MASK			(0xFFFFFFFF)
#define MDP_LUMA_HIST_MASK			(0xFFFFFFFF)
#define MDP_TDSHP_CTRL_MASK			(0x07)
#define MDP_TDSHP_CFG_MASK			(0x03F7)
#define MDP_TDSHP_INPUT_SIZE_MASK		(0x1FFF1FFF)
#define MDP_TDSHP_OUTPUT_OFFSET_MASK		(0x0FF00FF)
#define MDP_TDSHP_OUTPUT_SIZE_MASK		(0x1FFF1FFF)
#define MDP_LUMA_HIST_INIT_MASK			(0xFFFFFFFF)
#define MDP_DC_TWO_D_W1_RESULT_INIT_MASK	(0x007FFFFF)
#define MDP_CONTOUR_HIST_INIT_MASK		(0xFFFFFFFF)

#endif  // __MDP_REG_TDSHP_H__
