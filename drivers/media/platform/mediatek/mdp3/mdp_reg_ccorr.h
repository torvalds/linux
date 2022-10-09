/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_CCORR_H__
#define __MDP_REG_CCORR_H__

#define MDP_CCORR_EN                0x000
#define MDP_CCORR_CFG               0x020
#define MDP_CCORR_SIZE              0x030

/* MASK */
#define MDP_CCORR_EN_MASK           0x00000001
#define MDP_CCORR_CFG_MASK          0x70001317
#define MDP_CCORR_SIZE_MASK         0x1fff1fff

#endif  // __MDP_REG_CCORR_H__
