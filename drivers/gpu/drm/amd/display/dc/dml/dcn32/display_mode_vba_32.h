/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#ifndef __DML32_DISPLAY_MODE_VBA_H__
#define __DML32_DISPLAY_MODE_VBA_H__

#include "../display_mode_enums.h"

// To enable a lot of debug msg
//#define __DML_VBA_DEBUG__
// For DML-C changes that hasn't been propagated to VBA yet
//#define __DML_VBA_ALLOW_DELTA__

// Move these to ip parameters/constant
// At which vstartup the DML start to try if the mode can be supported
#define __DML_VBA_MIN_VSTARTUP__    9

// Delay in DCFCLK from ARB to DET (1st num is ARB to SDPIF, 2nd number is SDPIF to DET)
#define __DML_ARB_TO_RET_DELAY__    7 + 95

// fudge factor for min dcfclk calclation
#define __DML_MIN_DCFCLK_FACTOR__   1.15

// Prefetch schedule max vratio
#define __DML_MAX_VRATIO_PRE__ 7.9
#define __DML_MAX_BW_RATIO_PRE__ 4.0

#define __DML_VBA_MAX_DST_Y_PRE__    63.75

#define BPP_INVALID 0
#define BPP_BLENDED_PIPE 0xffffffff

#define MEM_STROBE_FREQ_MHZ 1600
#define MEM_STROBE_MAX_DELIVERY_TIME_US 60.0

struct display_mode_lib;

void dml32_ModeSupportAndSystemConfigurationFull(struct display_mode_lib *mode_lib);
void dml32_recalculate(struct display_mode_lib *mode_lib);

#endif
