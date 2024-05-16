/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#ifndef __ISP_V4_1_1_H__
#define __ISP_V4_1_1_H__

#include "amdgpu_isp.h"

#include "ivsrcid/isp/irqsrcs_isp_4_1.h"

#define mmDAGB1_WRCLI5_V4_1_1   0x68420
#define mmDAGB1_WRCLI9_V4_1_1   0x68430
#define mmDAGB1_WRCLI10_V4_1_1  0x68434
#define mmDAGB1_WRCLI14_V4_1_1  0x68444
#define mmDAGB1_WRCLI19_V4_1_1  0x68458
#define mmDAGB1_WRCLI20_V4_1_1  0x6845C

#define MAX_ISP411_INT_SRC 8

void isp_v4_1_1_set_isp_funcs(struct amdgpu_isp *isp);

#endif
