/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_PAD_H__
#define __MDP_REG_PAD_H__

#define MDP_PAD_CON			(0x000)
#define MDP_PAD_PIC_SIZE		(0x004)
#define MDP_PAD_W_SIZE			(0x008)
#define MDP_PAD_H_SIZE			(0x00c)

/* MASK */
#define MDP_PAD_CON_MASK		(0x00000007)
#define MDP_PAD_PIC_SIZE_MASK		(0xFFFFFFFF)
#define MDP_PAD_W_SIZE_MASK		(0x1FFF1FFF)
#define MDP_PAD_H_SIZE_MASK		(0x1FFF1FFF)

#endif  // __MDP_REG_PAD_H__
