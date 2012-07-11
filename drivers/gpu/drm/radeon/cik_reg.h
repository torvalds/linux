/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Alex Deucher
 */
#ifndef __CIK_REG_H__
#define __CIK_REG_H__

#define CIK_DC_GPIO_HPD_MASK                      0x65b0
#define CIK_DC_GPIO_HPD_A                         0x65b4
#define CIK_DC_GPIO_HPD_EN                        0x65b8
#define CIK_DC_GPIO_HPD_Y                         0x65bc

/* CUR blocks at 0x6998, 0x7598, 0x10198, 0x10d98, 0x11998, 0x12598 */
#define CIK_CUR_CONTROL                           0x6998
#       define CIK_CURSOR_EN                      (1 << 0)
#       define CIK_CURSOR_MODE(x)                 (((x) & 0x3) << 8)
#       define CIK_CURSOR_MONO                    0
#       define CIK_CURSOR_24_1                    1
#       define CIK_CURSOR_24_8_PRE_MULT           2
#       define CIK_CURSOR_24_8_UNPRE_MULT         3
#       define CIK_CURSOR_2X_MAGNIFY              (1 << 16)
#       define CIK_CURSOR_FORCE_MC_ON             (1 << 20)
#       define CIK_CURSOR_URGENT_CONTROL(x)       (((x) & 0x7) << 24)
#       define CIK_CURSOR_URGENT_ALWAYS           0
#       define CIK_CURSOR_URGENT_1_8              1
#       define CIK_CURSOR_URGENT_1_4              2
#       define CIK_CURSOR_URGENT_3_8              3
#       define CIK_CURSOR_URGENT_1_2              4
#define CIK_CUR_SURFACE_ADDRESS                   0x699c
#       define CIK_CUR_SURFACE_ADDRESS_MASK       0xfffff000
#define CIK_CUR_SIZE                              0x69a0
#define CIK_CUR_SURFACE_ADDRESS_HIGH              0x69a4
#define CIK_CUR_POSITION                          0x69a8
#define CIK_CUR_HOT_SPOT                          0x69ac
#define CIK_CUR_COLOR1                            0x69b0
#define CIK_CUR_COLOR2                            0x69b4
#define CIK_CUR_UPDATE                            0x69b8
#       define CIK_CURSOR_UPDATE_PENDING          (1 << 0)
#       define CIK_CURSOR_UPDATE_TAKEN            (1 << 1)
#       define CIK_CURSOR_UPDATE_LOCK             (1 << 16)
#       define CIK_CURSOR_DISABLE_MULTIPLE_UPDATE (1 << 24)

#define CIK_ALPHA_CONTROL                         0x6af0
#       define CIK_CURSOR_ALPHA_BLND_ENA          (1 << 1)

#define CIK_LB_DATA_FORMAT                        0x6b00
#       define CIK_INTERLEAVE_EN                  (1 << 3)

#endif
