/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_RDMA_H__
#define __MDP_REG_RDMA_H__

#define MDP_RDMA_EN                     0x000
#define MDP_RDMA_RESET                  0x008
#define MDP_RDMA_CON                    0x020
#define MDP_RDMA_GMCIF_CON              0x028
#define MDP_RDMA_SRC_CON                0x030
#define MDP_RDMA_MF_BKGD_SIZE_IN_BYTE   0x060
#define MDP_RDMA_MF_BKGD_SIZE_IN_PXL    0x068
#define MDP_RDMA_MF_SRC_SIZE            0x070
#define MDP_RDMA_MF_CLIP_SIZE           0x078
#define MDP_RDMA_MF_OFFSET_1            0x080
#define MDP_RDMA_SF_BKGD_SIZE_IN_BYTE   0x090
#define MDP_RDMA_SRC_END_0              0x100
#define MDP_RDMA_SRC_END_1              0x108
#define MDP_RDMA_SRC_END_2              0x110
#define MDP_RDMA_SRC_OFFSET_0           0x118
#define MDP_RDMA_SRC_OFFSET_1           0x120
#define MDP_RDMA_SRC_OFFSET_2           0x128
#define MDP_RDMA_SRC_OFFSET_0_P         0x148
#define MDP_RDMA_TRANSFORM_0            0x200
#define MDP_RDMA_DMABUF_CON_0           0x240
#define MDP_RDMA_ULTRA_TH_HIGH_CON_0    0x248
#define MDP_RDMA_ULTRA_TH_LOW_CON_0     0x250
#define MDP_RDMA_DMABUF_CON_1           0x258
#define MDP_RDMA_ULTRA_TH_HIGH_CON_1    0x260
#define MDP_RDMA_ULTRA_TH_LOW_CON_1     0x268
#define MDP_RDMA_DMABUF_CON_2           0x270
#define MDP_RDMA_ULTRA_TH_HIGH_CON_2    0x278
#define MDP_RDMA_ULTRA_TH_LOW_CON_2     0x280
#define MDP_RDMA_DMABUF_CON_3           0x288
#define MDP_RDMA_ULTRA_TH_HIGH_CON_3    0x290
#define MDP_RDMA_ULTRA_TH_LOW_CON_3     0x298
#define MDP_RDMA_RESV_DUMMY_0           0x2a0
#define MDP_RDMA_MON_STA_1              0x408
#define MDP_RDMA_SRC_BASE_0             0xf00
#define MDP_RDMA_SRC_BASE_1             0xf08
#define MDP_RDMA_SRC_BASE_2             0xf10
#define MDP_RDMA_UFO_DEC_LENGTH_BASE_Y  0xf20
#define MDP_RDMA_UFO_DEC_LENGTH_BASE_C  0xf28

/* MASK */
#define MDP_RDMA_EN_MASK                    0x00000001
#define MDP_RDMA_RESET_MASK                 0x00000001
#define MDP_RDMA_CON_MASK                   0x00001110
#define MDP_RDMA_GMCIF_CON_MASK             0xfffb3771
#define MDP_RDMA_SRC_CON_MASK               0xf3ffffff
#define MDP_RDMA_MF_BKGD_SIZE_IN_BYTE_MASK  0x001fffff
#define MDP_RDMA_MF_BKGD_SIZE_IN_PXL_MASK   0x001fffff
#define MDP_RDMA_MF_SRC_SIZE_MASK           0x1fff1fff
#define MDP_RDMA_MF_CLIP_SIZE_MASK          0x1fff1fff
#define MDP_RDMA_MF_OFFSET_1_MASK           0x003f001f
#define MDP_RDMA_SF_BKGD_SIZE_IN_BYTE_MASK  0x001fffff
#define MDP_RDMA_SRC_END_0_MASK             0xffffffff
#define MDP_RDMA_SRC_END_1_MASK             0xffffffff
#define MDP_RDMA_SRC_END_2_MASK             0xffffffff
#define MDP_RDMA_SRC_OFFSET_0_MASK          0xffffffff
#define MDP_RDMA_SRC_OFFSET_1_MASK          0xffffffff
#define MDP_RDMA_SRC_OFFSET_2_MASK          0xffffffff
#define MDP_RDMA_SRC_OFFSET_0_P_MASK        0xffffffff
#define MDP_RDMA_TRANSFORM_0_MASK           0xff110777
#define MDP_RDMA_DMABUF_CON_0_MASK          0x0fff00ff
#define MDP_RDMA_ULTRA_TH_HIGH_CON_0_MASK   0x3fffffff
#define MDP_RDMA_ULTRA_TH_LOW_CON_0_MASK    0x3fffffff
#define MDP_RDMA_DMABUF_CON_1_MASK          0x0f7f007f
#define MDP_RDMA_ULTRA_TH_HIGH_CON_1_MASK   0x3fffffff
#define MDP_RDMA_ULTRA_TH_LOW_CON_1_MASK    0x3fffffff
#define MDP_RDMA_DMABUF_CON_2_MASK          0x0f3f003f
#define MDP_RDMA_ULTRA_TH_HIGH_CON_2_MASK   0x3fffffff
#define MDP_RDMA_ULTRA_TH_LOW_CON_2_MASK    0x3fffffff
#define MDP_RDMA_DMABUF_CON_3_MASK          0x0f3f003f
#define MDP_RDMA_ULTRA_TH_HIGH_CON_3_MASK   0x3fffffff
#define MDP_RDMA_ULTRA_TH_LOW_CON_3_MASK    0x3fffffff
#define MDP_RDMA_RESV_DUMMY_0_MASK          0xffffffff
#define MDP_RDMA_MON_STA_1_MASK             0xffffffff
#define MDP_RDMA_SRC_BASE_0_MASK            0xffffffff
#define MDP_RDMA_SRC_BASE_1_MASK            0xffffffff
#define MDP_RDMA_SRC_BASE_2_MASK            0xffffffff
#define MDP_RDMA_UFO_DEC_LENGTH_BASE_Y_MASK 0xffffffff
#define MDP_RDMA_UFO_DEC_LENGTH_BASE_C_MASK 0xffffffff

#endif  // __MDP_REG_RDMA_H__
