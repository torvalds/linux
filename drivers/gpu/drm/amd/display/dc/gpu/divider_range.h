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

#ifndef __DAL_DIVIDER_RANGE_H__
#define __DAL_DIVIDER_RANGE_H__

enum divider_error_types {
	INVALID_DID = 0,
	INVALID_DIVIDER = 1
};

struct divider_range {
	uint32_t div_range_start;
	/* The end of this range of dividers.*/
	uint32_t div_range_end;
	/* The distance between each divider in this range.*/
	uint32_t div_range_step;
	/* The divider id for the lowest divider.*/
	uint32_t did_min;
	/* The divider id for the highest divider.*/
	uint32_t did_max;
};

bool dal_divider_range_construct(
	struct divider_range *div_range,
	uint32_t range_start,
	uint32_t range_step,
	uint32_t did_min,
	uint32_t did_max);

uint32_t dal_divider_range_get_divider(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t did);
uint32_t dal_divider_range_get_did(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t divider);

#endif /* __DAL_DIVIDER_RANGE_H__ */
