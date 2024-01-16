/*
 * Copyright (C) 2020  Advanced Micro Devices, Inc.
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
#ifndef _umc_6_1_2_SH_MASK_HEADER
#define _umc_6_1_2_SH_MASK_HEADER

//UMCCH0_0_EccErrCntSel_ARCT
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrCntCsSel__SHIFT                                                          0x0
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrInt__SHIFT                                                               0xc
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrCntEn__SHIFT                                                             0xf
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrCntCsSel_MASK                                                            0x0000000FL
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrInt_MASK                                                                 0x00003000L
#define UMCCH0_0_EccErrCntSel_ARCT__EccErrCntEn_MASK                                                               0x00008000L
//UMCCH0_0_EccErrCnt_ARCT
#define UMCCH0_0_EccErrCnt_ARCT__EccErrCnt__SHIFT                                                                  0x0
#define UMCCH0_0_EccErrCnt_ARCT__EccErrCnt_MASK                                                                    0x0000FFFFL
//MCA_UMC_UMC0_MCUMC_STATUST0_ARCT
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrorCode__SHIFT                                                         0x0
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrorCodeExt__SHIFT                                                      0x10
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV0__SHIFT                                                           0x16
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrCoreId__SHIFT                                                         0x20
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV1__SHIFT                                                           0x26
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Scrub__SHIFT                                                             0x28
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV2__SHIFT                                                           0x29
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Poison__SHIFT                                                            0x2b
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Deferred__SHIFT                                                          0x2c
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__UECC__SHIFT                                                              0x2d
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__CECC__SHIFT                                                              0x2e
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV3__SHIFT                                                           0x2f
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Transparent__SHIFT                                                       0x34
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__SyndV__SHIFT                                                             0x35
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV4__SHIFT                                                           0x36
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__TCC__SHIFT                                                               0x37
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrCoreIdVal__SHIFT                                                      0x38
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__PCC__SHIFT                                                               0x39
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__AddrV__SHIFT                                                             0x3a
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__MiscV__SHIFT                                                             0x3b
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__En__SHIFT                                                                0x3c
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__UC__SHIFT                                                                0x3d
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Overflow__SHIFT                                                          0x3e
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Val__SHIFT                                                               0x3f
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrorCode_MASK                                                           0x000000000000FFFFL
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrorCodeExt_MASK                                                        0x00000000003F0000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV0_MASK                                                             0x00000000FFC00000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrCoreId_MASK                                                           0x0000003F00000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV1_MASK                                                             0x000000C000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Scrub_MASK                                                               0x0000010000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV2_MASK                                                             0x0000060000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Poison_MASK                                                              0x0000080000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Deferred_MASK                                                            0x0000100000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__UECC_MASK                                                                0x0000200000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__CECC_MASK                                                                0x0000400000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV3_MASK                                                             0x000F800000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Transparent_MASK                                                         0x0010000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__SyndV_MASK                                                               0x0020000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__RESERV4_MASK                                                             0x0040000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__TCC_MASK                                                                 0x0080000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__ErrCoreIdVal_MASK                                                        0x0100000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__PCC_MASK                                                                 0x0200000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__AddrV_MASK                                                               0x0400000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__MiscV_MASK                                                               0x0800000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__En_MASK                                                                  0x1000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__UC_MASK                                                                  0x2000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Overflow_MASK                                                            0x4000000000000000L
#define MCA_UMC_UMC0_MCUMC_STATUST0_ARCT__Val_MASK                                                                 0x8000000000000000L
//MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__ErrorAddr__SHIFT                                                           0x0
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__LSB__SHIFT                                                                 0x38
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__Reserved__SHIFT                                                            0x3e
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__ErrorAddr_MASK                                                             0x00FFFFFFFFFFFFFFL
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__LSB_MASK                                                                   0x3F00000000000000L
#define MCA_UMC_UMC0_MCUMC_ADDRT0_ARCT__Reserved_MASK                                                              0xC000000000000000L

#endif
