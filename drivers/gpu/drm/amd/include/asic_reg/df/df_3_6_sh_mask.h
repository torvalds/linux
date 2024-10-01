/*
 * Copyright (C) 2018  Advanced Micro Devices, Inc.
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
#ifndef _df_3_6_SH_MASK_HEADER
#define _df_3_6_SH_MASK_HEADER

/* FabricConfigAccessControl */
#define FabricConfigAccessControl__CfgRegInstAccEn__SHIFT						0x0
#define FabricConfigAccessControl__CfgRegInstAccRegLock__SHIFT						0x1
#define FabricConfigAccessControl__CfgRegInstID__SHIFT							0x10
#define FabricConfigAccessControl__CfgRegInstAccEn_MASK							0x00000001L
#define FabricConfigAccessControl__CfgRegInstAccRegLock_MASK						0x00000002L
#define FabricConfigAccessControl__CfgRegInstID_MASK							0x00FF0000L

/* DF_PIE_AON0_DfGlobalClkGater */
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode__SHIFT							0x0
#define DF_PIE_AON0_DfGlobalClkGater__MGCGMode_MASK							0x0000000FL

/* DF_CS_UMC_AON0_DfGlobalCtrl */
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl64K__SHIFT						0x14
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl2M__SHIFT						0x15
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl1G__SHIFT						0x16
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl64K_MASK						0x00100000L
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl2M_MASK						0x00200000L
#define DF_CS_UMC_AON0_DfGlobalCtrl__GlbHashIntlvCtl1G_MASK						0x00400000L

/* DF_CS_AON0_DramBaseAddress0 */
#define DF_CS_UMC_AON0_DramBaseAddress0__AddrRngVal__SHIFT						0x0
#define DF_CS_UMC_AON0_DramBaseAddress0__LgcyMmioHoleEn__SHIFT						0x1
#define DF_CS_UMC_AON0_DramBaseAddress0__IntLvNumChan__SHIFT						0x2
#define DF_CS_UMC_AON0_DramBaseAddress0__IntLvAddrSel__SHIFT						0x9
#define DF_CS_UMC_AON0_DramBaseAddress0__DramBaseAddr__SHIFT						0xc
#define DF_CS_UMC_AON0_DramBaseAddress0__AddrRngVal_MASK						0x00000001L
#define DF_CS_UMC_AON0_DramBaseAddress0__LgcyMmioHoleEn_MASK						0x00000002L
#define DF_CS_UMC_AON0_DramBaseAddress0__IntLvNumChan_MASK						0x0000003CL
#define ALDEBARAN_DF_CS_UMC_AON0_DramBaseAddress0__IntLvNumChan_MASK					0x0000007CL
#define DF_CS_UMC_AON0_DramBaseAddress0__IntLvAddrSel_MASK						0x00000E00L
#define DF_CS_UMC_AON0_DramBaseAddress0__DramBaseAddr_MASK						0xFFFFF000L

//DF_CS_UMC_AON0_DramLimitAddress0
#define DF_CS_UMC_AON0_DramLimitAddress0__DstFabricID__SHIFT                                                  0x0
#define DF_CS_UMC_AON0_DramLimitAddress0__AllowReqIO__SHIFT                                                   0xa
#define DF_CS_UMC_AON0_DramLimitAddress0__DramLimitAddr__SHIFT                                                0xc
#define DF_CS_UMC_AON0_DramLimitAddress0__DstFabricID_MASK                                                    0x000003FFL
#define DF_CS_UMC_AON0_DramLimitAddress0__AllowReqIO_MASK                                                     0x00000400L
#define DF_CS_UMC_AON0_DramLimitAddress0__DramLimitAddr_MASK                                                  0xFFFFF000L

//DF_CS_UMC_AON0_HardwareAssertMaskLow
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk0__SHIFT                                             0x0
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk1__SHIFT                                             0x1
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk2__SHIFT                                             0x2
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk3__SHIFT                                             0x3
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk4__SHIFT                                             0x4
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk5__SHIFT                                             0x5
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk6__SHIFT                                             0x6
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk7__SHIFT                                             0x7
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk8__SHIFT                                             0x8
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk9__SHIFT                                             0x9
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk10__SHIFT                                            0xa
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk11__SHIFT                                            0xb
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk12__SHIFT                                            0xc
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk13__SHIFT                                            0xd
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk14__SHIFT                                            0xe
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk15__SHIFT                                            0xf
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk16__SHIFT                                            0x10
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk17__SHIFT                                            0x11
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk18__SHIFT                                            0x12
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk19__SHIFT                                            0x13
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk20__SHIFT                                            0x14
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk21__SHIFT                                            0x15
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk22__SHIFT                                            0x16
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk23__SHIFT                                            0x17
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk24__SHIFT                                            0x18
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk25__SHIFT                                            0x19
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk26__SHIFT                                            0x1a
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk27__SHIFT                                            0x1b
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk28__SHIFT                                            0x1c
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk29__SHIFT                                            0x1d
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk30__SHIFT                                            0x1e
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk31__SHIFT                                            0x1f
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk0_MASK                                               0x00000001L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk1_MASK                                               0x00000002L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk2_MASK                                               0x00000004L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk3_MASK                                               0x00000008L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk4_MASK                                               0x00000010L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk5_MASK                                               0x00000020L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk6_MASK                                               0x00000040L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk7_MASK                                               0x00000080L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk8_MASK                                               0x00000100L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk9_MASK                                               0x00000200L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk10_MASK                                              0x00000400L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk11_MASK                                              0x00000800L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk12_MASK                                              0x00001000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk13_MASK                                              0x00002000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk14_MASK                                              0x00004000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk15_MASK                                              0x00008000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk16_MASK                                              0x00010000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk17_MASK                                              0x00020000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk18_MASK                                              0x00040000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk19_MASK                                              0x00080000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk20_MASK                                              0x00100000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk21_MASK                                              0x00200000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk22_MASK                                              0x00400000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk23_MASK                                              0x00800000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk24_MASK                                              0x01000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk25_MASK                                              0x02000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk26_MASK                                              0x04000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk27_MASK                                              0x08000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk28_MASK                                              0x10000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk29_MASK                                              0x20000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk30_MASK                                              0x40000000L
#define DF_CS_UMC_AON0_HardwareAssertMaskLow__HWAssertMsk31_MASK                                              0x80000000L

//DF_NCS_PG0_HardwareAssertMaskHigh
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk0__SHIFT                                                0x0
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk1__SHIFT                                                0x1
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk2__SHIFT                                                0x2
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk3__SHIFT                                                0x3
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk4__SHIFT                                                0x4
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk5__SHIFT                                                0x5
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk6__SHIFT                                                0x6
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk7__SHIFT                                                0x7
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk8__SHIFT                                                0x8
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk9__SHIFT                                                0x9
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk10__SHIFT                                               0xa
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk11__SHIFT                                               0xb
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk12__SHIFT                                               0xc
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk13__SHIFT                                               0xd
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk14__SHIFT                                               0xe
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk15__SHIFT                                               0xf
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk16__SHIFT                                               0x10
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk17__SHIFT                                               0x11
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk18__SHIFT                                               0x12
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk19__SHIFT                                               0x13
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk20__SHIFT                                               0x14
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk21__SHIFT                                               0x15
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk22__SHIFT                                               0x16
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk23__SHIFT                                               0x17
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk24__SHIFT                                               0x18
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk25__SHIFT                                               0x19
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk26__SHIFT                                               0x1a
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk27__SHIFT                                               0x1b
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk28__SHIFT                                               0x1c
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk29__SHIFT                                               0x1d
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk30__SHIFT                                               0x1e
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk31__SHIFT                                               0x1f
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk0_MASK                                                  0x00000001L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk1_MASK                                                  0x00000002L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk2_MASK                                                  0x00000004L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk3_MASK                                                  0x00000008L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk4_MASK                                                  0x00000010L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk5_MASK                                                  0x00000020L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk6_MASK                                                  0x00000040L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk7_MASK                                                  0x00000080L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk8_MASK                                                  0x00000100L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk9_MASK                                                  0x00000200L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk10_MASK                                                 0x00000400L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk11_MASK                                                 0x00000800L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk12_MASK                                                 0x00001000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk13_MASK                                                 0x00002000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk14_MASK                                                 0x00004000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk15_MASK                                                 0x00008000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk16_MASK                                                 0x00010000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk17_MASK                                                 0x00020000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk18_MASK                                                 0x00040000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk19_MASK                                                 0x00080000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk20_MASK                                                 0x00100000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk21_MASK                                                 0x00200000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk22_MASK                                                 0x00400000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk23_MASK                                                 0x00800000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk24_MASK                                                 0x01000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk25_MASK                                                 0x02000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk26_MASK                                                 0x04000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk27_MASK                                                 0x08000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk28_MASK                                                 0x10000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk29_MASK                                                 0x20000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk30_MASK                                                 0x40000000L
#define DF_NCS_PG0_HardwareAssertMaskHigh__HWAssertMsk31_MASK                                                 0x80000000L

#endif
