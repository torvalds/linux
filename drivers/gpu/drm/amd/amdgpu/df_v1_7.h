/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#ifndef __DF_V1_7_H__
#define __DF_V1_7_H__

#include "soc15_common.h"
enum DF_V1_7_MGCG
{
	DF_V1_7_MGCG_DISABLE = 0,
	DF_V1_7_MGCG_ENABLE_00_CYCLE_DELAY =1,
	DF_V1_7_MGCG_ENABLE_01_CYCLE_DELAY =2,
	DF_V1_7_MGCG_ENABLE_15_CYCLE_DELAY =13,
	DF_V1_7_MGCG_ENABLE_31_CYCLE_DELAY =14,
	DF_V1_7_MGCG_ENABLE_63_CYCLE_DELAY =15
};

extern const struct amdgpu_df_funcs df_v1_7_funcs;

#endif
