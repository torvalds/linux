/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
#ifndef __NAVI10_PPT_H__
#define __NAVI10_PPT_H__

#define NAVI10_PEAK_SCLK_XTX		(1830)
#define NAVI10_PEAK_SCLK_XT  		(1755)
#define NAVI10_PEAK_SCLK_XL  		(1625)

#define NAVI10_UMD_PSTATE_PROFILING_GFXCLK    (1300)
#define NAVI10_UMD_PSTATE_PROFILING_SOCCLK    (980)
#define NAVI10_UMD_PSTATE_PROFILING_MEMCLK    (625)
#define NAVI10_UMD_PSTATE_PROFILING_VCLK      (980)
#define NAVI10_UMD_PSTATE_PROFILING_DCLK      (850)

#define NAVI14_UMD_PSTATE_PEAK_XT_GFXCLK      (1670)
#define NAVI14_UMD_PSTATE_PEAK_XTM_GFXCLK     (1448)
#define NAVI14_UMD_PSTATE_PEAK_XLM_GFXCLK     (1181)
#define NAVI14_UMD_PSTATE_PEAK_XTX_GFXCLK     (1717)
#define NAVI14_UMD_PSTATE_PEAK_XL_GFXCLK      (1448)

#define NAVI14_UMD_PSTATE_PROFILING_GFXCLK    (1200)
#define NAVI14_UMD_PSTATE_PROFILING_SOCCLK    (900)
#define NAVI14_UMD_PSTATE_PROFILING_MEMCLK    (600)
#define NAVI14_UMD_PSTATE_PROFILING_VCLK      (900)
#define NAVI14_UMD_PSTATE_PROFILING_DCLK      (800)

#define NAVI12_UMD_PSTATE_PEAK_GFXCLK     (1100)

#define NAVI10_VOLTAGE_SCALE (4)

#define smnPCIE_LC_SPEED_CNTL			0x11140290
#define smnPCIE_LC_LINK_WIDTH_CNTL		0x11140288

extern void navi10_set_ppt_funcs(struct smu_context *smu);

#endif
