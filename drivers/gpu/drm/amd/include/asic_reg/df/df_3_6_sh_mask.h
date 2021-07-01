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

#endif
