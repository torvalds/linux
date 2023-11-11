/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_WDMA_H__
#define __MDP_REG_WDMA_H__

#define WDMA_EN                 0x008
#define WDMA_RST                0x00c
#define WDMA_CFG                0x014
#define WDMA_SRC_SIZE           0x018
#define WDMA_CLIP_SIZE          0x01c
#define WDMA_CLIP_COORD         0x020
#define WDMA_DST_W_IN_BYTE      0x028
#define WDMA_ALPHA              0x02c
#define WDMA_BUF_CON2           0x03c
#define WDMA_DST_UV_PITCH       0x078
#define WDMA_DST_ADDR_OFFSET    0x080
#define WDMA_DST_U_ADDR_OFFSET  0x084
#define WDMA_DST_V_ADDR_OFFSET  0x088
#define WDMA_FLOW_CTRL_DBG      0x0a0
#define WDMA_DST_ADDR           0xf00
#define WDMA_DST_U_ADDR         0xf04
#define WDMA_DST_V_ADDR         0xf08

/* MASK */
#define WDMA_EN_MASK                0x00000001
#define WDMA_RST_MASK               0x00000001
#define WDMA_CFG_MASK               0xff03bff0
#define WDMA_SRC_SIZE_MASK          0x3fff3fff
#define WDMA_CLIP_SIZE_MASK         0x3fff3fff
#define WDMA_CLIP_COORD_MASK        0x3fff3fff
#define WDMA_DST_W_IN_BYTE_MASK     0x0000ffff
#define WDMA_ALPHA_MASK             0x800000ff
#define WDMA_BUF_CON2_MASK          0xffffffff
#define WDMA_DST_UV_PITCH_MASK      0x0000ffff
#define WDMA_DST_ADDR_OFFSET_MASK   0x0fffffff
#define WDMA_DST_U_ADDR_OFFSET_MASK 0x0fffffff
#define WDMA_DST_V_ADDR_OFFSET_MASK 0x0fffffff
#define WDMA_FLOW_CTRL_DBG_MASK     0x0000f3ff
#define WDMA_DST_ADDR_MASK          0xffffffff
#define WDMA_DST_U_ADDR_MASK        0xffffffff
#define WDMA_DST_V_ADDR_MASK        0xffffffff

#endif  // __MDP_REG_WDMA_H__
