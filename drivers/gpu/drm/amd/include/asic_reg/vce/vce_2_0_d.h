/*
 * VCE_2_0 Register documentation
 *
 * Copyright (C) 2014  Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VCE_2_0_D_H
#define VCE_2_0_D_H

#define mmVCE_STATUS                                                            0x8001
#define mmVCE_VCPU_CNTL                                                         0x8005
#define mmVCE_VCPU_CACHE_OFFSET0                                                0x8009
#define mmVCE_VCPU_CACHE_SIZE0                                                  0x800a
#define mmVCE_VCPU_CACHE_OFFSET1                                                0x800b
#define mmVCE_VCPU_CACHE_SIZE1                                                  0x800c
#define mmVCE_VCPU_CACHE_OFFSET2                                                0x800d
#define mmVCE_VCPU_CACHE_SIZE2                                                  0x800e
#define mmVCE_SOFT_RESET                                                        0x8048
#define mmVCE_RB_BASE_LO2                                                       0x805b
#define mmVCE_RB_BASE_HI2                                                       0x805c
#define mmVCE_RB_SIZE2                                                          0x805d
#define mmVCE_RB_RPTR2                                                          0x805e
#define mmVCE_RB_WPTR2                                                          0x805f
#define mmVCE_RB_BASE_LO                                                        0x8060
#define mmVCE_RB_BASE_HI                                                        0x8061
#define mmVCE_RB_SIZE                                                           0x8062
#define mmVCE_RB_RPTR                                                           0x8063
#define mmVCE_RB_WPTR                                                           0x8064
#define mmVCE_RB_ARB_CTRL                                                       0x809f
#define mmVCE_CLOCK_GATING_A                                                    0x80be
#define mmVCE_CLOCK_GATING_B                                                    0x80bf
#define mmVCE_UENC_DMA_DCLK_CTRL                                                0x8390
#define mmVCE_CGTT_CLK_OVERRIDE                                                 0x81e8
#define mmVCE_UENC_CLOCK_GATING                                                 0x81ef
#define mmVCE_UENC_REG_CLOCK_GATING                                             0x81f0
#define mmVCE_SYS_INT_EN                                                        0x84c0
#define mmVCE_SYS_INT_STATUS                                                    0x84c1
#define mmVCE_SYS_INT_ACK                                                       0x84c1
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR                                          0x8517
#define mmVCE_LMI_CTRL2                                                         0x851d
#define mmVCE_LMI_SWAP_CNTL3                                                    0x851e
#define mmVCE_LMI_CTRL                                                          0x8526
#define mmVCE_LMI_STATUS                                                        0x8527
#define mmVCE_LMI_VM_CTRL                                                       0x8528
#define mmVCE_LMI_SWAP_CNTL                                                     0x852d
#define mmVCE_LMI_SWAP_CNTL1                                                    0x852e
#define mmVCE_LMI_SWAP_CNTL2                                                    0x8533
#define mmVCE_LMI_MISC_CTRL                                                     0x8535
#define mmVCE_LMI_CACHE_CTRL                                                    0x853d

#endif /* VCE_2_0_D_H */
