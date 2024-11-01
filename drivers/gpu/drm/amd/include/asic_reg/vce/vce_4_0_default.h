/*
 * Copyright (C) 2017  Advanced Micro Devices, Inc.
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
#ifndef _vce_4_0_DEFAULT_HEADER
#define _vce_4_0_DEFAULT_HEADER


// addressBlock: vce0_vce_dec
#define mmVCE_STATUS_DEFAULT                                                     0x00000000
#define mmVCE_VCPU_CNTL_DEFAULT                                                  0x00200000
#define mmVCE_VCPU_CACHE_OFFSET0_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE0_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET1_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE1_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET2_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE2_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET3_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE3_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET4_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE4_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET5_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE5_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET6_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE6_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET7_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE7_DEFAULT                                           0x00000000
#define mmVCE_VCPU_CACHE_OFFSET8_DEFAULT                                         0x00000000
#define mmVCE_VCPU_CACHE_SIZE8_DEFAULT                                           0x00000000
#define mmVCE_SOFT_RESET_DEFAULT                                                 0x00000001
#define mmVCE_RB_BASE_LO2_DEFAULT                                                0x00000000
#define mmVCE_RB_BASE_HI2_DEFAULT                                                0x00000000
#define mmVCE_RB_SIZE2_DEFAULT                                                   0x00000000
#define mmVCE_RB_RPTR2_DEFAULT                                                   0x00000000
#define mmVCE_RB_WPTR2_DEFAULT                                                   0x00000000
#define mmVCE_RB_BASE_LO_DEFAULT                                                 0x00000000
#define mmVCE_RB_BASE_HI_DEFAULT                                                 0x00000000
#define mmVCE_RB_SIZE_DEFAULT                                                    0x00000000
#define mmVCE_RB_RPTR_DEFAULT                                                    0x00000000
#define mmVCE_RB_WPTR_DEFAULT                                                    0x00000000
#define mmVCE_RB_ARB_CTRL_DEFAULT                                                0x00010000
#define mmVCE_CLOCK_GATING_A_DEFAULT                                             0x00000040
#define mmVCE_CLOCK_GATING_B_DEFAULT                                             0x01ef0100
#define mmVCE_RB_BASE_LO3_DEFAULT                                                0x00000000
#define mmVCE_RB_BASE_HI3_DEFAULT                                                0x00000000
#define mmVCE_RB_SIZE3_DEFAULT                                                   0x00000000
#define mmVCE_RB_RPTR3_DEFAULT                                                   0x00000000
#define mmVCE_RB_WPTR3_DEFAULT                                                   0x00000000
#define mmVCE_SYS_INT_EN_DEFAULT                                                 0x00000000
#define mmVCE_SYS_INT_ACK_DEFAULT                                                0x00000000
#define mmVCE_SYS_INT_STATUS_DEFAULT                                             0x00000000


// addressBlock: vce0_ctl_dec
#define mmVCE_UENC_CLOCK_GATING_DEFAULT                                          0xffc00040
#define mmVCE_UENC_REG_CLOCK_GATING_DEFAULT                                      0x000007ff
#define mmVCE_UENC_CLOCK_GATING_2_DEFAULT                                        0x00010000


// addressBlock: vce0_vce_sclk_dec
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR_DEFAULT                                   0x00000000
#define mmVCE_LMI_CTRL2_DEFAULT                                                  0x00000000
#define mmVCE_LMI_SWAP_CNTL3_DEFAULT                                             0x00000000
#define mmVCE_LMI_CTRL_DEFAULT                                                   0x00104000
#define mmVCE_LMI_STATUS_DEFAULT                                                 0x00003f7f
#define mmVCE_LMI_VM_CTRL_DEFAULT                                                0x00000000
#define mmVCE_LMI_SWAP_CNTL_DEFAULT                                              0x00000000
#define mmVCE_LMI_SWAP_CNTL1_DEFAULT                                             0x00000000
#define mmVCE_LMI_SWAP_CNTL2_DEFAULT                                             0x00000000
#define mmVCE_LMI_CACHE_CTRL_DEFAULT                                             0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR0_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR1_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR2_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR3_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR4_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR5_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR6_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_64BIT_BAR7_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR0_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR1_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR2_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR3_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR4_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR5_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR6_DEFAULT                                  0x00000000
#define mmVCE_LMI_VCPU_CACHE_40BIT_BAR7_DEFAULT                                  0x00000000


// addressBlock: vce0_mmsch_dec
#define mmVCE_MMSCH_VF_VMID_DEFAULT                                              0x00000000
#define mmVCE_MMSCH_VF_CTX_ADDR_LO_DEFAULT                                       0x00000000
#define mmVCE_MMSCH_VF_CTX_ADDR_HI_DEFAULT                                       0x00000000
#define mmVCE_MMSCH_VF_CTX_SIZE_DEFAULT                                          0x00000000
#define mmVCE_MMSCH_VF_GPCOM_ADDR_LO_DEFAULT                                     0x00000000
#define mmVCE_MMSCH_VF_GPCOM_ADDR_HI_DEFAULT                                     0x00000000
#define mmVCE_MMSCH_VF_GPCOM_SIZE_DEFAULT                                        0x00000000
#define mmVCE_MMSCH_VF_MAILBOX_HOST_DEFAULT                                      0x00000000
#define mmVCE_MMSCH_VF_MAILBOX_RESP_DEFAULT                                      0x00000000


// addressBlock: vce0_vce_rb_pg_dec
#define mmVCE_HW_VERSION_DEFAULT                                                 0x00000000



#endif
