/*
 * Copyright 2012-15 Advanced Micro Devices, Inc.
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

#ifndef __DAL_TYPES_H__
#define __DAL_TYPES_H__

#include "signal_types.h"
#include "dc_types.h"

struct dal_logger;
struct dc_bios;

enum dce_version {
	DCE_VERSION_UNKNOWN = (-1),
	DCE_VERSION_6_0,
	DCE_VERSION_6_1,
	DCE_VERSION_6_4,
	DCE_VERSION_8_0,
	DCE_VERSION_8_1,
	DCE_VERSION_8_3,
	DCE_VERSION_10_0,
	DCE_VERSION_11_0,
	DCE_VERSION_11_2,
	DCE_VERSION_11_22,
	DCE_VERSION_12_0,
	DCE_VERSION_12_1,
	DCE_VERSION_MAX,
	DCN_VERSION_1_0,
	DCN_VERSION_1_01,
	DCN_VERSION_2_0,
	DCN_VERSION_2_01,
	DCN_VERSION_2_1,
	DCN_VERSION_3_0,
	DCN_VERSION_3_01,
	DCN_VERSION_3_02,
	DCN_VERSION_3_03,
	DCN_VERSION_3_1,
	DCN_VERSION_3_14,
	DCN_VERSION_3_15,
	DCN_VERSION_3_16,
	DCN_VERSION_3_2,
	DCN_VERSION_3_21,
	DCN_VERSION_3_5,
	DCN_VERSION_3_51,
	DCN_VERSION_MAX
};

#endif /* __DAL_TYPES_H__ */
