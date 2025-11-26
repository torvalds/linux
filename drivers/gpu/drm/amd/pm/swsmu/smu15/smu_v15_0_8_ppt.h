/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#ifndef __SMU_15_0_8_PPT_H__
#define __SMU_15_0_8_PPT_H__

extern void smu_v15_0_8_set_ppt_funcs(struct smu_context *smu);

typedef struct {
	uint32_t MaxSocketPowerLimit;
	uint32_t MaxGfxclkFrequency;
	uint32_t MinGfxclkFrequency;
	uint32_t MaxFclkFrequency;
	uint32_t MinFclkFrequency;
	uint32_t MaxGl2clkFrequency;
	uint32_t MinGl2clkFrequency;
	uint32_t UclkFrequencyTable[4];
	uint32_t SocclkFrequency;
	uint32_t LclkFrequency;
	uint32_t VclkFrequency;
	uint32_t DclkFrequency;
	uint32_t CTFLimitMID;
	uint32_t CTFLimitAID;
	uint32_t CTFLimitXCD;
	uint32_t CTFLimitHBM;
	uint32_t ThermalLimitMID;
	uint32_t ThermalLimitAID;
	uint32_t ThermalLimitXCD;
	uint32_t ThermalLimitHBM;
	uint64_t PublicSerialNumberMID;
	uint64_t PublicSerialNumberAID;
	uint64_t PublicSerialNumberXCD;
	uint32_t PPT1Max;
	uint32_t PPT1Min;
	uint32_t PPT1Default;
	bool init;
} PPTable_t;

#endif
