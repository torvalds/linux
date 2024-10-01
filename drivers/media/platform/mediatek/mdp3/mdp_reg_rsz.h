/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Ping-Hsun Wu <ping-hsun.wu@mediatek.com>
 */

#ifndef __MDP_REG_RSZ_H__
#define __MDP_REG_RSZ_H__

#define PRZ_ENABLE                                        0x000
#define PRZ_CONTROL_1                                     0x004
#define PRZ_CONTROL_2                                     0x008
#define PRZ_INPUT_IMAGE                                   0x010
#define PRZ_OUTPUT_IMAGE                                  0x014
#define PRZ_HORIZONTAL_COEFF_STEP                         0x018
#define PRZ_VERTICAL_COEFF_STEP                           0x01c
#define PRZ_LUMA_HORIZONTAL_INTEGER_OFFSET                0x020
#define PRZ_LUMA_HORIZONTAL_SUBPIXEL_OFFSET               0x024
#define PRZ_LUMA_VERTICAL_INTEGER_OFFSET                  0x028
#define PRZ_LUMA_VERTICAL_SUBPIXEL_OFFSET                 0x02c
#define PRZ_CHROMA_HORIZONTAL_INTEGER_OFFSET              0x030
#define PRZ_CHROMA_HORIZONTAL_SUBPIXEL_OFFSET             0x034

/* MASK */
#define PRZ_ENABLE_MASK                                   0x00010001
#define PRZ_CONTROL_1_MASK                                0xfffffff3
#define PRZ_CONTROL_2_MASK                                0x0ffffaff
#define PRZ_INPUT_IMAGE_MASK                              0xffffffff
#define PRZ_OUTPUT_IMAGE_MASK                             0xffffffff
#define PRZ_HORIZONTAL_COEFF_STEP_MASK                    0x007fffff
#define PRZ_VERTICAL_COEFF_STEP_MASK                      0x007fffff
#define PRZ_LUMA_HORIZONTAL_INTEGER_OFFSET_MASK           0x0000ffff
#define PRZ_LUMA_HORIZONTAL_SUBPIXEL_OFFSET_MASK          0x001fffff
#define PRZ_LUMA_VERTICAL_INTEGER_OFFSET_MASK             0x0000ffff
#define PRZ_LUMA_VERTICAL_SUBPIXEL_OFFSET_MASK            0x001fffff
#define PRZ_CHROMA_HORIZONTAL_INTEGER_OFFSET_MASK         0x0000ffff
#define PRZ_CHROMA_HORIZONTAL_SUBPIXEL_OFFSET_MASK        0x001fffff

#endif // __MDP_REG_RSZ_H__
