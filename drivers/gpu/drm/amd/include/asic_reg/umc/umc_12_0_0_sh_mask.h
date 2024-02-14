/*
 * Copyright (C) 2023  Advanced Micro Devices, Inc.
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
#ifndef _umc_12_0_0_SH_MASK_HEADER
#define _umc_12_0_0_SH_MASK_HEADER

//UMCCH0_OdEccCntSel
#define UMCCH0_OdEccCntSel__OdEccCntSel__SHIFT                                 0x0
#define UMCCH0_OdEccCntSel__OdEccErrInt__SHIFT                                 0x4
#define UMCCH0_OdEccCntSel__OdEccCntSel_MASK                                   0x00000007L
#define UMCCH0_OdEccCntSel__OdEccErrInt_MASK                                   0x00000030L
//UMCCH0_OdEccErrCnt
#define UMCCH0_OdEccErrCnt__Cnt__SHIFT                                         0x0
#define UMCCH0_OdEccErrCnt__CntOvr__SHIFT                                      0x10
#define UMCCH0_OdEccErrCnt__OvrClr__SHIFT                                      0x11
#define UMCCH0_OdEccErrCnt__Cnt_MASK                                           0x0000FFFFL
#define UMCCH0_OdEccErrCnt__CntOvr_MASK                                        0x00010000L
#define UMCCH0_OdEccErrCnt__OvrClr_MASK                                        0x00020000L
//MCA_UMC_UMC0_MCUMC_STATUST0
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrorCode__SHIFT                          0x0
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrorCodeExt__SHIFT                       0x10
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV22__SHIFT                           0x16
#define MCA_UMC_UMC0_MCUMC_STATUST0__AddrLsb__SHIFT                            0x18
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV30__SHIFT                           0x1e
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrCoreId__SHIFT                          0x20
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV38__SHIFT                           0x26
#define MCA_UMC_UMC0_MCUMC_STATUST0__Scrub__SHIFT                              0x28
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV41__SHIFT                           0x29
#define MCA_UMC_UMC0_MCUMC_STATUST0__Poison__SHIFT                             0x2b
#define MCA_UMC_UMC0_MCUMC_STATUST0__Deferred__SHIFT                           0x2c
#define MCA_UMC_UMC0_MCUMC_STATUST0__UECC__SHIFT                               0x2d
#define MCA_UMC_UMC0_MCUMC_STATUST0__CECC__SHIFT                               0x2e
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV47__SHIFT                           0x2f
#define MCA_UMC_UMC0_MCUMC_STATUST0__Transparent__SHIFT                        0x34
#define MCA_UMC_UMC0_MCUMC_STATUST0__SyndV__SHIFT                              0x35
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV54__SHIFT                           0x36
#define MCA_UMC_UMC0_MCUMC_STATUST0__TCC__SHIFT                                0x37
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrCoreIdVal__SHIFT                       0x38
#define MCA_UMC_UMC0_MCUMC_STATUST0__PCC__SHIFT                                0x39
#define MCA_UMC_UMC0_MCUMC_STATUST0__AddrV__SHIFT                              0x3a
#define MCA_UMC_UMC0_MCUMC_STATUST0__MiscV__SHIFT                              0x3b
#define MCA_UMC_UMC0_MCUMC_STATUST0__En__SHIFT                                 0x3c
#define MCA_UMC_UMC0_MCUMC_STATUST0__UC__SHIFT                                 0x3d
#define MCA_UMC_UMC0_MCUMC_STATUST0__Overflow__SHIFT                           0x3e
#define MCA_UMC_UMC0_MCUMC_STATUST0__Val__SHIFT                                0x3f
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrorCode_MASK                            0x000000000000FFFFL
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrorCodeExt_MASK                         0x00000000003F0000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV22_MASK                             0x0000000000C00000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__AddrLsb_MASK                              0x000000003F000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV30_MASK                             0x00000000C0000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrCoreId_MASK                            0x0000003F00000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV38_MASK                             0x000000C000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Scrub_MASK                                0x0000010000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV41_MASK                             0x0000060000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Poison_MASK                               0x0000080000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Deferred_MASK                             0x0000100000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__UECC_MASK                                 0x0000200000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__CECC_MASK                                 0x0000400000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV47_MASK                             0x000F800000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Transparent_MASK                          0x0010000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__SyndV_MASK                                0x0020000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__RESERV54_MASK                             0x0040000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__TCC_MASK                                  0x0080000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__ErrCoreIdVal_MASK                         0x0100000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__PCC_MASK                                  0x0200000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__AddrV_MASK                                0x0400000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__MiscV_MASK                                0x0800000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__En_MASK                                   0x1000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__UC_MASK                                   0x2000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Overflow_MASK                             0x4000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0__Val_MASK                                  0x8000000000000000L
//MCA_UMC_UMC0_MCUMC_ADDRT0
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr__SHIFT                            0x0
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved__SHIFT                             0x38
#define MCA_UMC_UMC0_MCUMC_ADDRT0__ErrorAddr_MASK                              0x00FFFFFFFFFFFFFFL
#define MCA_UMC_UMC0_MCUMC_ADDRT0__Reserved_MASK                               0xFF00000000000000L

#endif
