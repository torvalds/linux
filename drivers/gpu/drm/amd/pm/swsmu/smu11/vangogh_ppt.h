/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#ifndef __VANGOGH_PPT_H__
#define __VANGOGH_PPT_H__


extern void vangogh_set_ppt_funcs(struct smu_context *smu);

/* UMD PState Vangogh Msg Parameters in MHz */
#define VANGOGH_UMD_PSTATE_STANDARD_GFXCLK       1100
#define VANGOGH_UMD_PSTATE_STANDARD_SOCCLK       600
#define VANGOGH_UMD_PSTATE_STANDARD_FCLK         800
#define VANGOGH_UMD_PSTATE_STANDARD_VCLK         705
#define VANGOGH_UMD_PSTATE_STANDARD_DCLK         600

#define VANGOGH_UMD_PSTATE_PEAK_GFXCLK       1300
#define VANGOGH_UMD_PSTATE_PEAK_SOCCLK       600
#define VANGOGH_UMD_PSTATE_PEAK_FCLK         800
#define VANGOGH_UMD_PSTATE_PEAK_VCLK         705
#define VANGOGH_UMD_PSTATE_PEAK_DCLK         600

#define VANGOGH_UMD_PSTATE_MIN_SCLK_GFXCLK       400
#define VANGOGH_UMD_PSTATE_MIN_SCLK_SOCCLK       1000
#define VANGOGH_UMD_PSTATE_MIN_SCLK_FCLK         800
#define VANGOGH_UMD_PSTATE_MIN_SCLK_VCLK         1000
#define VANGOGH_UMD_PSTATE_MIN_SCLK_DCLK         800

#define VANGOGH_UMD_PSTATE_MIN_MCLK_GFXCLK       1100
#define VANGOGH_UMD_PSTATE_MIN_MCLK_SOCCLK       1000
#define VANGOGH_UMD_PSTATE_MIN_MCLK_FCLK         400
#define VANGOGH_UMD_PSTATE_MIN_MCLK_VCLK         1000
#define VANGOGH_UMD_PSTATE_MIN_MCLK_DCLK         800

/* RLC Power Status */
#define RLC_STATUS_OFF          0
#define RLC_STATUS_NORMAL       1

#endif
