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
#ifndef _vce_4_0_SH_MASK_HEADER
#define _vce_4_0_SH_MASK_HEADER


// addressBlock: vce0_vce_dec
//VCE_STATUS
#define VCE_STATUS__JOB_BUSY__SHIFT                                                                           0x0
#define VCE_STATUS__VCPU_REPORT__SHIFT                                                                        0x1
#define VCE_STATUS__UENC_BUSY__SHIFT                                                                          0x8
#define VCE_STATUS__VCE_CONFIGURATION__SHIFT                                                                  0x16
#define VCE_STATUS__VCE_INSTANCE_ID__SHIFT                                                                    0x18
#define VCE_STATUS__JOB_BUSY_MASK                                                                             0x00000001L
#define VCE_STATUS__VCPU_REPORT_MASK                                                                          0x000000FEL
#define VCE_STATUS__UENC_BUSY_MASK                                                                            0x00000100L
#define VCE_STATUS__VCE_CONFIGURATION_MASK                                                                    0x00C00000L
#define VCE_STATUS__VCE_INSTANCE_ID_MASK                                                                      0x03000000L
//VCE_VCPU_CNTL
#define VCE_VCPU_CNTL__CLK_EN__SHIFT                                                                          0x0
#define VCE_VCPU_CNTL__ED_ENABLE__SHIFT                                                                       0x1
#define VCE_VCPU_CNTL__RBBM_SOFT_RESET__SHIFT                                                                 0x12
#define VCE_VCPU_CNTL__ONE_CACHE_SURFACE_EN__SHIFT                                                            0x15
#define VCE_VCPU_CNTL__CLK_EN_MASK                                                                            0x00000001L
#define VCE_VCPU_CNTL__ED_ENABLE_MASK                                                                         0x00000002L
#define VCE_VCPU_CNTL__RBBM_SOFT_RESET_MASK                                                                   0x00040000L
#define VCE_VCPU_CNTL__ONE_CACHE_SURFACE_EN_MASK                                                              0x00200000L
//VCE_VCPU_CACHE_OFFSET0
#define VCE_VCPU_CACHE_OFFSET0__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET0__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE0
#define VCE_VCPU_CACHE_SIZE0__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE0__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET1
#define VCE_VCPU_CACHE_OFFSET1__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET1__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE1
#define VCE_VCPU_CACHE_SIZE1__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE1__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET2
#define VCE_VCPU_CACHE_OFFSET2__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET2__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE2
#define VCE_VCPU_CACHE_SIZE2__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE2__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET3
#define VCE_VCPU_CACHE_OFFSET3__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET3__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE3
#define VCE_VCPU_CACHE_SIZE3__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE3__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET4
#define VCE_VCPU_CACHE_OFFSET4__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET4__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE4
#define VCE_VCPU_CACHE_SIZE4__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE4__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET5
#define VCE_VCPU_CACHE_OFFSET5__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET5__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE5
#define VCE_VCPU_CACHE_SIZE5__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE5__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET6
#define VCE_VCPU_CACHE_OFFSET6__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET6__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE6
#define VCE_VCPU_CACHE_SIZE6__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE6__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET7
#define VCE_VCPU_CACHE_OFFSET7__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET7__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE7
#define VCE_VCPU_CACHE_SIZE7__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE7__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_VCPU_CACHE_OFFSET8
#define VCE_VCPU_CACHE_OFFSET8__OFFSET__SHIFT                                                                 0x0
#define VCE_VCPU_CACHE_OFFSET8__OFFSET_MASK                                                                   0x0FFFFFFFL
//VCE_VCPU_CACHE_SIZE8
#define VCE_VCPU_CACHE_SIZE8__SIZE__SHIFT                                                                     0x0
#define VCE_VCPU_CACHE_SIZE8__SIZE_MASK                                                                       0x00FFFFFFL
//VCE_SOFT_RESET
#define VCE_SOFT_RESET__ECPU_SOFT_RESET__SHIFT                                                                0x0
#define VCE_SOFT_RESET__UENC_SOFT_RESET__SHIFT                                                                0x1
#define VCE_SOFT_RESET__FME_SOFT_RESET__SHIFT                                                                 0x2
#define VCE_SOFT_RESET__MIF_SOFT_RESET__SHIFT                                                                 0x3
#define VCE_SOFT_RESET__DBF_SOFT_RESET__SHIFT                                                                 0x4
#define VCE_SOFT_RESET__ENT_SOFT_RESET__SHIFT                                                                 0x5
#define VCE_SOFT_RESET__TBE_SOFT_RESET__SHIFT                                                                 0x6
#define VCE_SOFT_RESET__LCM_SOFT_RESET__SHIFT                                                                 0x7
#define VCE_SOFT_RESET__CTL_SOFT_RESET__SHIFT                                                                 0x8
#define VCE_SOFT_RESET__IME_SOFT_RESET__SHIFT                                                                 0x9
#define VCE_SOFT_RESET__IH_SOFT_RESET__SHIFT                                                                  0xa
#define VCE_SOFT_RESET__SEM_SOFT_RESET__SHIFT                                                                 0xb
#define VCE_SOFT_RESET__DCAP_SOFT_RESET__SHIFT                                                                0xc
#define VCE_SOFT_RESET__ACAP_SOFT_RESET__SHIFT                                                                0xd
#define VCE_SOFT_RESET__TAP_SOFT_RESET__SHIFT                                                                 0xe
#define VCE_SOFT_RESET__LMI_SOFT_RESET__SHIFT                                                                 0xf
#define VCE_SOFT_RESET__LMI_UMC_SOFT_RESET__SHIFT                                                             0x10
#define VCE_SOFT_RESET__AVMUX_SOFT_RESET__SHIFT                                                               0x13
#define VCE_SOFT_RESET__VREG_SOFT_RESET__SHIFT                                                                0x14
#define VCE_SOFT_RESET__DCAP_FSM_SOFT_RESET__SHIFT                                                            0x15
#define VCE_SOFT_RESET__VEP_SOFT_RESET__SHIFT                                                                 0x16
#define VCE_SOFT_RESET__ECPU_SOFT_RESET_MASK                                                                  0x00000001L
#define VCE_SOFT_RESET__UENC_SOFT_RESET_MASK                                                                  0x00000002L
#define VCE_SOFT_RESET__FME_SOFT_RESET_MASK                                                                   0x00000004L
#define VCE_SOFT_RESET__MIF_SOFT_RESET_MASK                                                                   0x00000008L
#define VCE_SOFT_RESET__DBF_SOFT_RESET_MASK                                                                   0x00000010L
#define VCE_SOFT_RESET__ENT_SOFT_RESET_MASK                                                                   0x00000020L
#define VCE_SOFT_RESET__TBE_SOFT_RESET_MASK                                                                   0x00000040L
#define VCE_SOFT_RESET__LCM_SOFT_RESET_MASK                                                                   0x00000080L
#define VCE_SOFT_RESET__CTL_SOFT_RESET_MASK                                                                   0x00000100L
#define VCE_SOFT_RESET__IME_SOFT_RESET_MASK                                                                   0x00000200L
#define VCE_SOFT_RESET__IH_SOFT_RESET_MASK                                                                    0x00000400L
#define VCE_SOFT_RESET__SEM_SOFT_RESET_MASK                                                                   0x00000800L
#define VCE_SOFT_RESET__DCAP_SOFT_RESET_MASK                                                                  0x00001000L
#define VCE_SOFT_RESET__ACAP_SOFT_RESET_MASK                                                                  0x00002000L
#define VCE_SOFT_RESET__TAP_SOFT_RESET_MASK                                                                   0x00004000L
#define VCE_SOFT_RESET__LMI_SOFT_RESET_MASK                                                                   0x00008000L
#define VCE_SOFT_RESET__LMI_UMC_SOFT_RESET_MASK                                                               0x00010000L
#define VCE_SOFT_RESET__AVMUX_SOFT_RESET_MASK                                                                 0x00080000L
#define VCE_SOFT_RESET__VREG_SOFT_RESET_MASK                                                                  0x00100000L
#define VCE_SOFT_RESET__DCAP_FSM_SOFT_RESET_MASK                                                              0x00200000L
#define VCE_SOFT_RESET__VEP_SOFT_RESET_MASK                                                                   0x00400000L
//VCE_RB_BASE_LO2
#define VCE_RB_BASE_LO2__RB_BASE_LO__SHIFT                                                                    0x6
#define VCE_RB_BASE_LO2__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//VCE_RB_BASE_HI2
#define VCE_RB_BASE_HI2__RB_BASE_HI__SHIFT                                                                    0x0
#define VCE_RB_BASE_HI2__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//VCE_RB_SIZE2
#define VCE_RB_SIZE2__RB_SIZE__SHIFT                                                                          0x4
#define VCE_RB_SIZE2__RB_SIZE_MASK                                                                            0x007FFFF0L
//VCE_RB_RPTR2
#define VCE_RB_RPTR2__RB_RPTR__SHIFT                                                                          0x4
#define VCE_RB_RPTR2__RB_RPTR_MASK                                                                            0x007FFFF0L
//VCE_RB_WPTR2
#define VCE_RB_WPTR2__RB_WPTR__SHIFT                                                                          0x4
#define VCE_RB_WPTR2__RB_WPTR_MASK                                                                            0x007FFFF0L
//VCE_RB_BASE_LO
#define VCE_RB_BASE_LO__RB_BASE_LO__SHIFT                                                                     0x6
#define VCE_RB_BASE_LO__RB_BASE_LO_MASK                                                                       0xFFFFFFC0L
//VCE_RB_BASE_HI
#define VCE_RB_BASE_HI__RB_BASE_HI__SHIFT                                                                     0x0
#define VCE_RB_BASE_HI__RB_BASE_HI_MASK                                                                       0xFFFFFFFFL
//VCE_RB_SIZE
#define VCE_RB_SIZE__RB_SIZE__SHIFT                                                                           0x4
#define VCE_RB_SIZE__RB_SIZE_MASK                                                                             0x007FFFF0L
//VCE_RB_RPTR
#define VCE_RB_RPTR__RB_RPTR__SHIFT                                                                           0x4
#define VCE_RB_RPTR__RB_RPTR_MASK                                                                             0x007FFFF0L
//VCE_RB_WPTR
#define VCE_RB_WPTR__RB_WPTR__SHIFT                                                                           0x4
#define VCE_RB_WPTR__RB_WPTR_MASK                                                                             0x007FFFF0L
//VCE_RB_ARB_CTRL
#define VCE_RB_ARB_CTRL__RB_ARB_CTRL__SHIFT                                                                   0x0
#define VCE_RB_ARB_CTRL__VCE_CGTT_OVERRIDE__SHIFT                                                             0x10
#define VCE_RB_ARB_CTRL__RB_ARB_CTRL_MASK                                                                     0x000001FFL
#define VCE_RB_ARB_CTRL__VCE_CGTT_OVERRIDE_MASK                                                               0x00010000L
//VCE_CLOCK_GATING_A
#define VCE_CLOCK_GATING_A__CGC_CLK_ON_DELAY__SHIFT                                                           0x0
#define VCE_CLOCK_GATING_A__CGC_CLK_OFF_DELAY__SHIFT                                                          0x4
#define VCE_CLOCK_GATING_A__CGC_REG_AWAKE__SHIFT                                                              0x11
#define VCE_CLOCK_GATING_A__CGC_CLK_ON_DELAY_MASK                                                             0x0000000FL
#define VCE_CLOCK_GATING_A__CGC_CLK_OFF_DELAY_MASK                                                            0x00000FF0L
#define VCE_CLOCK_GATING_A__CGC_REG_AWAKE_MASK                                                                0x00020000L
//VCE_CLOCK_GATING_B
#define VCE_CLOCK_GATING_B__CGC_SYS_CLK_FORCE_ON__SHIFT                                                       0x0
#define VCE_CLOCK_GATING_B__CGC_LMI_MC_CLK_FORCE_ON__SHIFT                                                    0x1
#define VCE_CLOCK_GATING_B__CGC_LMI_UMC_CLK_FORCE_ON__SHIFT                                                   0x2
#define VCE_CLOCK_GATING_B__CGC_UENC_CLK_FORCE_ON__SHIFT                                                      0x3
#define VCE_CLOCK_GATING_B__CGC_VREG_CLK_FORCE_ON__SHIFT                                                      0x4
#define VCE_CLOCK_GATING_B__CGC_ECPU_CLK_FORCE_ON__SHIFT                                                      0x5
#define VCE_CLOCK_GATING_B__CGC_IH_CLK_FORCE_ON__SHIFT                                                        0x6
#define VCE_CLOCK_GATING_B__CGC_SEM_CLK_FORCE_ON__SHIFT                                                       0x7
#define VCE_CLOCK_GATING_B__CGC_CTLREG_CLK_FORCE_ON__SHIFT                                                    0x8
#define VCE_CLOCK_GATING_B__CGC_MMSCH_CLK_FORCE_ON__SHIFT                                                     0x9
#define VCE_CLOCK_GATING_B__CGC_SYS_CLK_FORCE_OFF__SHIFT                                                      0x10
#define VCE_CLOCK_GATING_B__CGC_LMI_MC_CLK_FORCE_OFF__SHIFT                                                   0x11
#define VCE_CLOCK_GATING_B__CGC_LMI_UMC_CLK_FORCE_OFF__SHIFT                                                  0x12
#define VCE_CLOCK_GATING_B__CGC_UENC_CLK_FORCE_OFF__SHIFT                                                     0x13
#define VCE_CLOCK_GATING_B__CGC_ECPU_CLK_FORCE_OFF__SHIFT                                                     0x15
#define VCE_CLOCK_GATING_B__CGC_IH_CLK_FORCE_OFF__SHIFT                                                       0x16
#define VCE_CLOCK_GATING_B__CGC_SEM_CLK_FORCE_OFF__SHIFT                                                      0x17
#define VCE_CLOCK_GATING_B__CGC_MMSCH_CLK_FORCE_OFF__SHIFT                                                    0x18
#define VCE_CLOCK_GATING_B__CGC_SYS_CLK_FORCE_ON_MASK                                                         0x00000001L
#define VCE_CLOCK_GATING_B__CGC_LMI_MC_CLK_FORCE_ON_MASK                                                      0x00000002L
#define VCE_CLOCK_GATING_B__CGC_LMI_UMC_CLK_FORCE_ON_MASK                                                     0x00000004L
#define VCE_CLOCK_GATING_B__CGC_UENC_CLK_FORCE_ON_MASK                                                        0x00000008L
#define VCE_CLOCK_GATING_B__CGC_VREG_CLK_FORCE_ON_MASK                                                        0x00000010L
#define VCE_CLOCK_GATING_B__CGC_ECPU_CLK_FORCE_ON_MASK                                                        0x00000020L
#define VCE_CLOCK_GATING_B__CGC_IH_CLK_FORCE_ON_MASK                                                          0x00000040L
#define VCE_CLOCK_GATING_B__CGC_SEM_CLK_FORCE_ON_MASK                                                         0x00000080L
#define VCE_CLOCK_GATING_B__CGC_CTLREG_CLK_FORCE_ON_MASK                                                      0x00000100L
#define VCE_CLOCK_GATING_B__CGC_MMSCH_CLK_FORCE_ON_MASK                                                       0x00000200L
#define VCE_CLOCK_GATING_B__CGC_SYS_CLK_FORCE_OFF_MASK                                                        0x00010000L
#define VCE_CLOCK_GATING_B__CGC_LMI_MC_CLK_FORCE_OFF_MASK                                                     0x00020000L
#define VCE_CLOCK_GATING_B__CGC_LMI_UMC_CLK_FORCE_OFF_MASK                                                    0x00040000L
#define VCE_CLOCK_GATING_B__CGC_UENC_CLK_FORCE_OFF_MASK                                                       0x00080000L
#define VCE_CLOCK_GATING_B__CGC_ECPU_CLK_FORCE_OFF_MASK                                                       0x00200000L
#define VCE_CLOCK_GATING_B__CGC_IH_CLK_FORCE_OFF_MASK                                                         0x00400000L
#define VCE_CLOCK_GATING_B__CGC_SEM_CLK_FORCE_OFF_MASK                                                        0x00800000L
#define VCE_CLOCK_GATING_B__CGC_MMSCH_CLK_FORCE_OFF_MASK                                                      0x01000000L
//VCE_RB_BASE_LO3
#define VCE_RB_BASE_LO3__RB_BASE_LO__SHIFT                                                                    0x6
#define VCE_RB_BASE_LO3__RB_BASE_LO_MASK                                                                      0xFFFFFFC0L
//VCE_RB_BASE_HI3
#define VCE_RB_BASE_HI3__RB_BASE_HI__SHIFT                                                                    0x0
#define VCE_RB_BASE_HI3__RB_BASE_HI_MASK                                                                      0xFFFFFFFFL
//VCE_RB_SIZE3
#define VCE_RB_SIZE3__RB_SIZE__SHIFT                                                                          0x4
#define VCE_RB_SIZE3__RB_SIZE_MASK                                                                            0x007FFFF0L
//VCE_RB_RPTR3
#define VCE_RB_RPTR3__RB_RPTR__SHIFT                                                                          0x4
#define VCE_RB_RPTR3__RB_RPTR_MASK                                                                            0x007FFFF0L
//VCE_RB_WPTR3
#define VCE_RB_WPTR3__RB_WPTR__SHIFT                                                                          0x4
#define VCE_RB_WPTR3__RB_WPTR_MASK                                                                            0x007FFFF0L
//VCE_SYS_INT_EN
#define VCE_SYS_INT_EN__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_EN__SHIFT                                          0x0
#define VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN__SHIFT                                                  0x3
#define VCE_SYS_INT_EN__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_EN_MASK                                            0x00000001L
#define VCE_SYS_INT_EN__VCE_SYS_INT_TRAP_INTERRUPT_EN_MASK                                                    0x00000008L
//VCE_SYS_INT_ACK
#define VCE_SYS_INT_ACK__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_ACK__SHIFT                                        0x0
#define VCE_SYS_INT_ACK__VCE_SYS_INT_TRAP_INTERRUPT_ACK__SHIFT                                                0x3
#define VCE_SYS_INT_ACK__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_ACK_MASK                                          0x00000001L
#define VCE_SYS_INT_ACK__VCE_SYS_INT_TRAP_INTERRUPT_ACK_MASK                                                  0x00000008L
//VCE_SYS_INT_STATUS
#define VCE_SYS_INT_STATUS__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_INT__SHIFT                                     0x0
#define VCE_SYS_INT_STATUS__VCE_SYS_INT_TRAP_INTERRUPT_INT__SHIFT                                             0x3
#define VCE_SYS_INT_STATUS__VCE_SYS_INT_SEMA_WAIT_FAIL_TIMEOUT_INT_MASK                                       0x00000001L
#define VCE_SYS_INT_STATUS__VCE_SYS_INT_TRAP_INTERRUPT_INT_MASK                                               0x00000008L


// addressBlock: vce0_ctl_dec
//VCE_UENC_CLOCK_GATING
#define VCE_UENC_CLOCK_GATING__CLOCK_ON_DELAY__SHIFT                                                          0x0
#define VCE_UENC_CLOCK_GATING__CLOCK_OFF_DELAY__SHIFT                                                         0x4
#define VCE_UENC_CLOCK_GATING__VEPCLK_FORCE_ON__SHIFT                                                         0xc
#define VCE_UENC_CLOCK_GATING__IMECLK_FORCE_ON__SHIFT                                                         0xd
#define VCE_UENC_CLOCK_GATING__FMECLK_FORCE_ON__SHIFT                                                         0xe
#define VCE_UENC_CLOCK_GATING__TBECLK_FORCE_ON__SHIFT                                                         0xf
#define VCE_UENC_CLOCK_GATING__DBFCLK_FORCE_ON__SHIFT                                                         0x10
#define VCE_UENC_CLOCK_GATING__ENTCLK_FORCE_ON__SHIFT                                                         0x11
#define VCE_UENC_CLOCK_GATING__LCMCLK_FORCE_ON__SHIFT                                                         0x12
#define VCE_UENC_CLOCK_GATING__AVMCLK_FORCE_ON__SHIFT                                                         0x13
#define VCE_UENC_CLOCK_GATING__DCAPCLK_FORCE_ON__SHIFT                                                        0x14
#define VCE_UENC_CLOCK_GATING__ACAPCLK_FORCE_ON__SHIFT                                                        0x15
#define VCE_UENC_CLOCK_GATING__ACAPCLK_FORCE_OFF__SHIFT                                                       0x16
#define VCE_UENC_CLOCK_GATING__VEPCLK_FORCE_OFF__SHIFT                                                        0x17
#define VCE_UENC_CLOCK_GATING__IMECLK_FORCE_OFF__SHIFT                                                        0x18
#define VCE_UENC_CLOCK_GATING__FMECLK_FORCE_OFF__SHIFT                                                        0x19
#define VCE_UENC_CLOCK_GATING__TBECLK_FORCE_OFF__SHIFT                                                        0x1a
#define VCE_UENC_CLOCK_GATING__DBFCLK_FORCE_OFF__SHIFT                                                        0x1b
#define VCE_UENC_CLOCK_GATING__ENTCLK_FORCE_OFF__SHIFT                                                        0x1c
#define VCE_UENC_CLOCK_GATING__LCMCLK_FORCE_OFF__SHIFT                                                        0x1d
#define VCE_UENC_CLOCK_GATING__AVMCLK_FORCE_OFF__SHIFT                                                        0x1e
#define VCE_UENC_CLOCK_GATING__DCAPCLK_FORCE_OFF__SHIFT                                                       0x1f
#define VCE_UENC_CLOCK_GATING__CLOCK_ON_DELAY_MASK                                                            0x0000000FL
#define VCE_UENC_CLOCK_GATING__CLOCK_OFF_DELAY_MASK                                                           0x00000FF0L
#define VCE_UENC_CLOCK_GATING__VEPCLK_FORCE_ON_MASK                                                           0x00001000L
#define VCE_UENC_CLOCK_GATING__IMECLK_FORCE_ON_MASK                                                           0x00002000L
#define VCE_UENC_CLOCK_GATING__FMECLK_FORCE_ON_MASK                                                           0x00004000L
#define VCE_UENC_CLOCK_GATING__TBECLK_FORCE_ON_MASK                                                           0x00008000L
#define VCE_UENC_CLOCK_GATING__DBFCLK_FORCE_ON_MASK                                                           0x00010000L
#define VCE_UENC_CLOCK_GATING__ENTCLK_FORCE_ON_MASK                                                           0x00020000L
#define VCE_UENC_CLOCK_GATING__LCMCLK_FORCE_ON_MASK                                                           0x00040000L
#define VCE_UENC_CLOCK_GATING__AVMCLK_FORCE_ON_MASK                                                           0x00080000L
#define VCE_UENC_CLOCK_GATING__DCAPCLK_FORCE_ON_MASK                                                          0x00100000L
#define VCE_UENC_CLOCK_GATING__ACAPCLK_FORCE_ON_MASK                                                          0x00200000L
#define VCE_UENC_CLOCK_GATING__ACAPCLK_FORCE_OFF_MASK                                                         0x00400000L
#define VCE_UENC_CLOCK_GATING__VEPCLK_FORCE_OFF_MASK                                                          0x00800000L
#define VCE_UENC_CLOCK_GATING__IMECLK_FORCE_OFF_MASK                                                          0x01000000L
#define VCE_UENC_CLOCK_GATING__FMECLK_FORCE_OFF_MASK                                                          0x02000000L
#define VCE_UENC_CLOCK_GATING__TBECLK_FORCE_OFF_MASK                                                          0x04000000L
#define VCE_UENC_CLOCK_GATING__DBFCLK_FORCE_OFF_MASK                                                          0x08000000L
#define VCE_UENC_CLOCK_GATING__ENTCLK_FORCE_OFF_MASK                                                          0x10000000L
#define VCE_UENC_CLOCK_GATING__LCMCLK_FORCE_OFF_MASK                                                          0x20000000L
#define VCE_UENC_CLOCK_GATING__AVMCLK_FORCE_OFF_MASK                                                          0x40000000L
#define VCE_UENC_CLOCK_GATING__DCAPCLK_FORCE_OFF_MASK                                                         0x80000000L
//VCE_UENC_REG_CLOCK_GATING
#define VCE_UENC_REG_CLOCK_GATING__MIFREGCLK_FORCE_ON__SHIFT                                                  0x0
#define VCE_UENC_REG_CLOCK_GATING__IMEREGCLK_FORCE_ON__SHIFT                                                  0x1
#define VCE_UENC_REG_CLOCK_GATING__FMEREGCLK_FORCE_ON__SHIFT                                                  0x2
#define VCE_UENC_REG_CLOCK_GATING__TBEREGCLK_FORCE_ON__SHIFT                                                  0x3
#define VCE_UENC_REG_CLOCK_GATING__DBFREGCLK_FORCE_ON__SHIFT                                                  0x4
#define VCE_UENC_REG_CLOCK_GATING__ENTREGCLK_FORCE_ON__SHIFT                                                  0x5
#define VCE_UENC_REG_CLOCK_GATING__LCMREGCLK_FORCE_ON__SHIFT                                                  0x6
#define VCE_UENC_REG_CLOCK_GATING__RESERVED__SHIFT                                                            0x7
#define VCE_UENC_REG_CLOCK_GATING__AVMREGCLK_FORCE_ON__SHIFT                                                  0x8
#define VCE_UENC_REG_CLOCK_GATING__DCAPREGCLK_FORCE_ON__SHIFT                                                 0x9
#define VCE_UENC_REG_CLOCK_GATING__VEPREGCLK_FORCE_ON__SHIFT                                                  0xa
#define VCE_UENC_REG_CLOCK_GATING__MIFREGCLK_FORCE_ON_MASK                                                    0x00000001L
#define VCE_UENC_REG_CLOCK_GATING__IMEREGCLK_FORCE_ON_MASK                                                    0x00000002L
#define VCE_UENC_REG_CLOCK_GATING__FMEREGCLK_FORCE_ON_MASK                                                    0x00000004L
#define VCE_UENC_REG_CLOCK_GATING__TBEREGCLK_FORCE_ON_MASK                                                    0x00000008L
#define VCE_UENC_REG_CLOCK_GATING__DBFREGCLK_FORCE_ON_MASK                                                    0x00000010L
#define VCE_UENC_REG_CLOCK_GATING__ENTREGCLK_FORCE_ON_MASK                                                    0x00000020L
#define VCE_UENC_REG_CLOCK_GATING__LCMREGCLK_FORCE_ON_MASK                                                    0x00000040L
#define VCE_UENC_REG_CLOCK_GATING__RESERVED_MASK                                                              0x00000080L
#define VCE_UENC_REG_CLOCK_GATING__AVMREGCLK_FORCE_ON_MASK                                                    0x00000100L
#define VCE_UENC_REG_CLOCK_GATING__DCAPREGCLK_FORCE_ON_MASK                                                   0x00000200L
#define VCE_UENC_REG_CLOCK_GATING__VEPREGCLK_FORCE_ON_MASK                                                    0x00000400L
//VCE_UENC_CLOCK_GATING_2
#define VCE_UENC_CLOCK_GATING_2__DBF2CLK_FORCE_ON__SHIFT                                                      0x1
#define VCE_UENC_CLOCK_GATING_2__DBF2CLK_FORCE_OFF__SHIFT                                                     0x10
#define VCE_UENC_CLOCK_GATING_2__DBF2CLK_FORCE_ON_MASK                                                        0x00000002L
#define VCE_UENC_CLOCK_GATING_2__DBF2CLK_FORCE_OFF_MASK                                                       0x00010000L


// addressBlock: vce0_vce_sclk_dec
//VCE_LMI_VCPU_CACHE_40BIT_BAR
#define VCE_LMI_VCPU_CACHE_40BIT_BAR__BAR__SHIFT                                                              0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR__BAR_MASK                                                                0xFFFFFFFFL
//VCE_LMI_CTRL2
#define VCE_LMI_CTRL2__STALL_ARB__SHIFT                                                                       0x1
#define VCE_LMI_CTRL2__ASSERT_UMC_URGENT__SHIFT                                                               0x2
#define VCE_LMI_CTRL2__MASK_UMC_URGENT__SHIFT                                                                 0x3
#define VCE_LMI_CTRL2__STALL_ARB_UMC__SHIFT                                                                   0x8
#define VCE_LMI_CTRL2__STALL_ARB_MASK                                                                         0x00000002L
#define VCE_LMI_CTRL2__ASSERT_UMC_URGENT_MASK                                                                 0x00000004L
#define VCE_LMI_CTRL2__MASK_UMC_URGENT_MASK                                                                   0x00000008L
#define VCE_LMI_CTRL2__STALL_ARB_UMC_MASK                                                                     0x00000100L
//VCE_LMI_SWAP_CNTL3
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_SWAP__SHIFT                                                             0x0
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_TRAN__SHIFT                                                             0x14
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_URG__SHIFT                                                              0x1a
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_SWAP_MASK                                                               0x00000003L
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_TRAN_MASK                                                               0x00100000L
#define VCE_LMI_SWAP_CNTL3__RD_MC_CID_URG_MASK                                                                0x04000000L
//VCE_LMI_CTRL
#define VCE_LMI_CTRL__ASSERT_MC_URGENT__SHIFT                                                                 0xb
#define VCE_LMI_CTRL__MASK_MC_URGENT__SHIFT                                                                   0xc
#define VCE_LMI_CTRL__DATA_COHERENCY_EN__SHIFT                                                                0xd
#define VCE_LMI_CTRL__VCPU_DATA_COHERENCY_EN__SHIFT                                                           0x15
#define VCE_LMI_CTRL__MIF_DATA_COHERENCY_EN__SHIFT                                                            0x16
#define VCE_LMI_CTRL__VCPU_RD_CACHE_MISS_COUNT_EN__SHIFT                                                      0x17
#define VCE_LMI_CTRL__VCPU_RD_CACHE_MISS_COUNT_RESET__SHIFT                                                   0x18
#define VCE_LMI_CTRL__ASSERT_MC_URGENT_MASK                                                                   0x00000800L
#define VCE_LMI_CTRL__MASK_MC_URGENT_MASK                                                                     0x00001000L
#define VCE_LMI_CTRL__DATA_COHERENCY_EN_MASK                                                                  0x00002000L
#define VCE_LMI_CTRL__VCPU_DATA_COHERENCY_EN_MASK                                                             0x00200000L
#define VCE_LMI_CTRL__MIF_DATA_COHERENCY_EN_MASK                                                              0x00400000L
#define VCE_LMI_CTRL__VCPU_RD_CACHE_MISS_COUNT_EN_MASK                                                        0x00800000L
#define VCE_LMI_CTRL__VCPU_RD_CACHE_MISS_COUNT_RESET_MASK                                                     0x01000000L
//VCE_LMI_SWAP_CNTL
#define VCE_LMI_SWAP_CNTL__VCPU_W_MC_SWAP__SHIFT                                                              0x0
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_SWAP__SHIFT                                                              0x2
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_TRAN__SHIFT                                                              0x14
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_URG__SHIFT                                                               0x1a
#define VCE_LMI_SWAP_CNTL__VCPU_W_MC_SWAP_MASK                                                                0x00000003L
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_SWAP_MASK                                                                0x00003FFCL
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_TRAN_MASK                                                                0x03F00000L
#define VCE_LMI_SWAP_CNTL__WR_MC_CID_URG_MASK                                                                 0xFC000000L
//VCE_LMI_SWAP_CNTL1
#define VCE_LMI_SWAP_CNTL1__VCPU_R_MC_SWAP__SHIFT                                                             0x0
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_SWAP__SHIFT                                                             0x2
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_TRAN__SHIFT                                                             0x14
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_URG__SHIFT                                                              0x1a
#define VCE_LMI_SWAP_CNTL1__VCPU_R_MC_SWAP_MASK                                                               0x00000003L
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_SWAP_MASK                                                               0x00003FFCL
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_TRAN_MASK                                                               0x03F00000L
#define VCE_LMI_SWAP_CNTL1__RD_MC_CID_URG_MASK                                                                0xFC000000L
//VCE_LMI_SWAP_CNTL2
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_SWAP__SHIFT                                                             0x0
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_TRAN__SHIFT                                                             0x14
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_URG__SHIFT                                                              0x1a
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_SWAP_MASK                                                               0x000000FFL
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_TRAN_MASK                                                               0x00F00000L
#define VCE_LMI_SWAP_CNTL2__WR_MC_CID_URG_MASK                                                                0x3C000000L
//VCE_LMI_CACHE_CTRL
#define VCE_LMI_CACHE_CTRL__VCPU_EN__SHIFT                                                                    0x0
#define VCE_LMI_CACHE_CTRL__VCPU_FLUSH__SHIFT                                                                 0x1
#define VCE_LMI_CACHE_CTRL__VCPU_EN_MASK                                                                      0x00000001L
#define VCE_LMI_CACHE_CTRL__VCPU_FLUSH_MASK                                                                   0x00000002L
//VCE_LMI_VCPU_CACHE_64BIT_BAR0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR0__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR0__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR1
#define VCE_LMI_VCPU_CACHE_64BIT_BAR1__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR1__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR2
#define VCE_LMI_VCPU_CACHE_64BIT_BAR2__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR2__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR3
#define VCE_LMI_VCPU_CACHE_64BIT_BAR3__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR3__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR4
#define VCE_LMI_VCPU_CACHE_64BIT_BAR4__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR4__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR5
#define VCE_LMI_VCPU_CACHE_64BIT_BAR5__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR5__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR6
#define VCE_LMI_VCPU_CACHE_64BIT_BAR6__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR6__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_64BIT_BAR7
#define VCE_LMI_VCPU_CACHE_64BIT_BAR7__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_64BIT_BAR7__BAR_MASK                                                               0x000000FFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR0__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR0__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR1
#define VCE_LMI_VCPU_CACHE_40BIT_BAR1__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR1__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR2
#define VCE_LMI_VCPU_CACHE_40BIT_BAR2__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR2__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR3
#define VCE_LMI_VCPU_CACHE_40BIT_BAR3__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR3__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR4
#define VCE_LMI_VCPU_CACHE_40BIT_BAR4__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR4__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR5
#define VCE_LMI_VCPU_CACHE_40BIT_BAR5__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR5__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR6
#define VCE_LMI_VCPU_CACHE_40BIT_BAR6__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR6__BAR_MASK                                                               0xFFFFFFFFL
//VCE_LMI_VCPU_CACHE_40BIT_BAR7
#define VCE_LMI_VCPU_CACHE_40BIT_BAR7__BAR__SHIFT                                                             0x0
#define VCE_LMI_VCPU_CACHE_40BIT_BAR7__BAR_MASK                                                               0xFFFFFFFFL


// addressBlock: vce0_mmsch_dec
//VCE_MMSCH_VF_VMID
#define VCE_MMSCH_VF_VMID__VF_CTX_VMID__SHIFT                                                                 0x0
#define VCE_MMSCH_VF_VMID__VF_GPCOM_VMID__SHIFT                                                               0x4
#define VCE_MMSCH_VF_VMID__VF_CTX_VMID_MASK                                                                   0x0000000FL
#define VCE_MMSCH_VF_VMID__VF_GPCOM_VMID_MASK                                                                 0x000000F0L
//VCE_MMSCH_VF_CTX_ADDR_LO
#define VCE_MMSCH_VF_CTX_ADDR_LO__VF_CTX_ADDR_LO__SHIFT                                                       0x6
#define VCE_MMSCH_VF_CTX_ADDR_LO__VF_CTX_ADDR_LO_MASK                                                         0xFFFFFFC0L
//VCE_MMSCH_VF_CTX_ADDR_HI
#define VCE_MMSCH_VF_CTX_ADDR_HI__VF_CTX_ADDR_HI__SHIFT                                                       0x0
#define VCE_MMSCH_VF_CTX_ADDR_HI__VF_CTX_ADDR_HI_MASK                                                         0xFFFFFFFFL
//VCE_MMSCH_VF_CTX_SIZE
#define VCE_MMSCH_VF_CTX_SIZE__VF_CTX_SIZE__SHIFT                                                             0x0
#define VCE_MMSCH_VF_CTX_SIZE__VF_CTX_SIZE_MASK                                                               0xFFFFFFFFL
//VCE_MMSCH_VF_GPCOM_ADDR_LO
#define VCE_MMSCH_VF_GPCOM_ADDR_LO__VF_GPCOM_ADDR_LO__SHIFT                                                   0x6
#define VCE_MMSCH_VF_GPCOM_ADDR_LO__VF_GPCOM_ADDR_LO_MASK                                                     0xFFFFFFC0L
//VCE_MMSCH_VF_GPCOM_ADDR_HI
#define VCE_MMSCH_VF_GPCOM_ADDR_HI__VF_GPCOM_ADDR_HI__SHIFT                                                   0x0
#define VCE_MMSCH_VF_GPCOM_ADDR_HI__VF_GPCOM_ADDR_HI_MASK                                                     0xFFFFFFFFL
//VCE_MMSCH_VF_GPCOM_SIZE
#define VCE_MMSCH_VF_GPCOM_SIZE__VF_GPCOM_SIZE__SHIFT                                                         0x0
#define VCE_MMSCH_VF_GPCOM_SIZE__VF_GPCOM_SIZE_MASK                                                           0xFFFFFFFFL
//VCE_MMSCH_VF_MAILBOX_HOST
#define VCE_MMSCH_VF_MAILBOX_HOST__DATA__SHIFT                                                                0x0
#define VCE_MMSCH_VF_MAILBOX_HOST__DATA_MASK                                                                  0xFFFFFFFFL
//VCE_MMSCH_VF_MAILBOX_RESP
#define VCE_MMSCH_VF_MAILBOX_RESP__RESP__SHIFT                                                                0x0
#define VCE_MMSCH_VF_MAILBOX_RESP__RESP_MASK                                                                  0xFFFFFFFFL


// addressBlock: vce0_vce_rb_pg_dec
//VCE_HW_VERSION
#define VCE_HW_VERSION__VCE_VERSION__SHIFT                                                                    0x0
#define VCE_HW_VERSION__VCE_CONFIGURATION__SHIFT                                                              0x8
#define VCE_HW_VERSION__VCE_INSTANCE_ID__SHIFT                                                                0xa
#define VCE_HW_VERSION__VCE_VERSION_MASK                                                                      0x000000FFL
#define VCE_HW_VERSION__VCE_CONFIGURATION_MASK                                                                0x00000300L
#define VCE_HW_VERSION__VCE_INSTANCE_ID_MASK                                                                  0x00000C00L



#endif
