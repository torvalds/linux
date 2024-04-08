/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_WROT_H__
#define __MDP_REG_WROT_H__

#define VIDO_CTRL                   0x000
#define VIDO_MAIN_BUF_SIZE          0x008
#define VIDO_SOFT_RST               0x010
#define VIDO_SOFT_RST_STAT          0x014
#define VIDO_CROP_OFST              0x020
#define VIDO_TAR_SIZE               0x024
#define VIDO_OFST_ADDR              0x02c
#define VIDO_STRIDE                 0x030
#define VIDO_OFST_ADDR_C            0x038
#define VIDO_STRIDE_C               0x03c
#define VIDO_CTRL_2                 0x048
#define VIDO_DITHER                 0x054
#define VIDO_STRIDE_V               0x06c
#define VIDO_OFST_ADDR_V            0x068
#define VIDO_RSV_1                  0x070
#define VIDO_DMA_PREULTRA           0x074
#define VIDO_IN_SIZE                0x078
#define VIDO_ROT_EN                 0x07c
#define VIDO_FIFO_TEST              0x080
#define VIDO_MAT_CTRL               0x084
#define VIDO_SCAN_10BIT             0x0dc
#define VIDO_PENDING_ZERO           0x0e0
#define VIDO_BASE_ADDR              0xf00
#define VIDO_BASE_ADDR_C            0xf04
#define VIDO_BASE_ADDR_V            0xf08

/* MASK */
#define VIDO_CTRL_MASK                  0xf530711f
#define VIDO_MAIN_BUF_SIZE_MASK         0x1fff7f77
#define VIDO_SOFT_RST_MASK              0x00000001
#define VIDO_SOFT_RST_STAT_MASK         0x00000001
#define VIDO_TAR_SIZE_MASK              0x1fff1fff
#define VIDO_CROP_OFST_MASK             0x1fff1fff
#define VIDO_OFST_ADDR_MASK             0x0fffffff
#define VIDO_STRIDE_MASK                0x0000ffff
#define VIDO_OFST_ADDR_C_MASK           0x0fffffff
#define VIDO_STRIDE_C_MASK              0x0000ffff
#define VIDO_CTRL_2_MASK                0x0000000f
#define VIDO_DITHER_MASK                0xff000001
#define VIDO_STRIDE_V_MASK              0x0000ffff
#define VIDO_OFST_ADDR_V_MASK           0x0fffffff
#define VIDO_RSV_1_MASK                 0xffffffff
#define VIDO_DMA_PREULTRA_MASK		0x00ffffff
#define VIDO_IN_SIZE_MASK               0x1fff1fff
#define VIDO_ROT_EN_MASK                0x00000001
#define VIDO_FIFO_TEST_MASK             0x00000fff
#define VIDO_MAT_CTRL_MASK              0x000000f3
#define VIDO_SCAN_10BIT_MASK            0x0000000f
#define VIDO_PENDING_ZERO_MASK          0x07ffffff
#define VIDO_BASE_ADDR_MASK             0xffffffff
#define VIDO_BASE_ADDR_C_MASK           0xffffffff
#define VIDO_BASE_ADDR_V_MASK           0xffffffff

#endif  // __MDP_REG_WROT_H__
