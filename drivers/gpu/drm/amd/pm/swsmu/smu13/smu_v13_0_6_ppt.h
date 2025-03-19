/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 */
#ifndef __SMU_13_0_6_PPT_H__
#define __SMU_13_0_6_PPT_H__

#define SMU_13_0_6_UMD_PSTATE_GFXCLK_LEVEL 0x2
#define SMU_13_0_6_UMD_PSTATE_SOCCLK_LEVEL 0x4
#define SMU_13_0_6_UMD_PSTATE_MCLK_LEVEL 0x2

typedef enum {
/*0*/   METRICS_VERSION_V0                  = 0,
/*1*/   METRICS_VERSION_V1                  = 1,
/*2*/   METRICS_VERSION_V2                  = 2,

/*3*/   NUM_METRICS                         = 3
} METRICS_LIST_e;

struct PPTable_t {
	uint32_t MaxSocketPowerLimit;
	uint32_t MaxGfxclkFrequency;
	uint32_t MinGfxclkFrequency;
	uint32_t FclkFrequencyTable[4];
	uint32_t UclkFrequencyTable[4];
	uint32_t SocclkFrequencyTable[4];
	uint32_t VclkFrequencyTable[4];
	uint32_t DclkFrequencyTable[4];
	uint32_t LclkFrequencyTable[4];
	uint32_t MaxLclkDpmRange;
	uint32_t MinLclkDpmRange;
	uint64_t PublicSerialNumber_AID;
	bool Init;
};

extern void smu_v13_0_6_set_ppt_funcs(struct smu_context *smu);

#endif
