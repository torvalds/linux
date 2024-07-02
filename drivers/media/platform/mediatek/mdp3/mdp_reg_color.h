/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_COLOR_H__
#define __MDP_REG_COLOR_H__

#define MDP_COLOR_WIN_X_MAIN			(0x40C)
#define MDP_COLOR_WIN_Y_MAIN			(0x410)
#define MDP_COLOR_START				(0xC00)
#define MDP_COLOR_INTEN				(0xC04)
#define MDP_COLOR_OUT_SEL			(0xC0C)
#define MDP_COLOR_INTERNAL_IP_WIDTH		(0xC50)
#define MDP_COLOR_INTERNAL_IP_HEIGHT		(0xC54)
#define MDP_COLOR_CM1_EN			(0xC60)
#define MDP_COLOR_CM2_EN			(0xCA0)

/* MASK */
#define MDP_COLOR_WIN_X_MAIN_MASK		(0xFFFFFFFF)
#define MDP_COLOR_WIN_Y_MAIN_MASK		(0xFFFFFFFF)
#define MDP_COLOR_START_MASK			(0x0FF013F)
#define MDP_COLOR_INTEN_MASK			(0x07)
#define MDP_COLOR_OUT_SEL_MASK			(0x0777)
#define MDP_COLOR_INTERNAL_IP_WIDTH_MASK	(0x03FFF)
#define MDP_COLOR_INTERNAL_IP_HEIGHT_MASK	(0x03FFF)
#define MDP_COLOR_CM1_EN_MASK			(0x03)
#define MDP_COLOR_CM2_EN_MASK			(0x017)

#endif  // __MDP_REG_COLOR_H__
