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
#ifndef _df_3_6_OFFSET_HEADER
#define _df_3_6_OFFSET_HEADER

#define mmFabricConfigAccessControl									0x0410
#define mmFabricConfigAccessControl_BASE_IDX								0

#define mmDF_PIE_AON0_DfGlobalClkGater									0x00fc
#define mmDF_PIE_AON0_DfGlobalClkGater_BASE_IDX								0

#define mmDF_CS_UMC_AON0_DramBaseAddress0								0x0044
#define mmDF_CS_UMC_AON0_DramBaseAddress0_BASE_IDX							0

#define smnPerfMonCtlLo0					0x01d440UL
#define smnPerfMonCtlHi0					0x01d444UL
#define smnPerfMonCtlLo1					0x01d450UL
#define smnPerfMonCtlHi1					0x01d454UL
#define smnPerfMonCtlLo2					0x01d460UL
#define smnPerfMonCtlHi2					0x01d464UL
#define smnPerfMonCtlLo3					0x01d470UL
#define smnPerfMonCtlHi3					0x01d474UL

#define smnPerfMonCtrLo0					0x01d448UL
#define smnPerfMonCtrHi0					0x01d44cUL
#define smnPerfMonCtrLo1					0x01d458UL
#define smnPerfMonCtrHi1					0x01d45cUL
#define smnPerfMonCtrLo2					0x01d468UL
#define smnPerfMonCtrHi2					0x01d46cUL
#define smnPerfMonCtrLo3					0x01d478UL
#define smnPerfMonCtrHi3					0x01d47cUL

#define smnDF_PIE_AON_FabricIndirectConfigAccessAddress3	0x1d05cUL
#define smnDF_PIE_AON_FabricIndirectConfigAccessDataLo3		0x1d098UL
#define smnDF_PIE_AON_FabricIndirectConfigAccessDataHi3		0x1d09cUL

#endif
