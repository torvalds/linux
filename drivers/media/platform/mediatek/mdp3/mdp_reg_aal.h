/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_AAL_H__
#define __MDP_REG_AAL_H__

#define MDP_AAL_EN			(0x000)
#define MDP_AAL_CFG			(0x020)
#define MDP_AAL_SIZE			(0x030)
#define MDP_AAL_OUTPUT_SIZE		(0x034)
#define MDP_AAL_OUTPUT_OFFSET		(0x038)
#define MDP_AAL_CFG_MAIN		(0x200)

/* MASK */
#define MDP_AAL_EN_MASK			(0x01)
#define MDP_AAL_CFG_MASK		(0x70FF00B3)
#define MDP_AAL_SIZE_MASK		(0x1FFF1FFF)
#define MDP_AAL_OUTPUT_SIZE_MASK	(0x1FFF1FFF)
#define MDP_AAL_OUTPUT_OFFSET_MASK	(0x0FF00FF)
#define MDP_AAL_CFG_MAIN_MASK		(0x0FE)

#endif  // __MDP_REG_AAL_H__
