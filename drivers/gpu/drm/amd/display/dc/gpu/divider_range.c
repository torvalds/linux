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
#include "dm_services.h"
#include "divider_range.h"

bool dal_divider_range_construct(
	struct divider_range *div_range,
	uint32_t range_start,
	uint32_t range_step,
	uint32_t did_min,
	uint32_t did_max)
{
	div_range->div_range_start = range_start;
	div_range->div_range_step = range_step;
	div_range->did_min = did_min;
	div_range->did_max = did_max;

	if (div_range->div_range_step == 0) {
		div_range->div_range_step = 1;
		/*div_range_step cannot be zero*/
		BREAK_TO_DEBUGGER();
	}
	/* Calculate this based on the other inputs.*/
	/* See DividerRange.h for explanation of */
	/* the relationship between divider id (DID) and a divider.*/
	/* Number of Divider IDs = (Maximum Divider ID - Minimum Divider ID)*/
	/* Maximum divider identified in this range =
	 * (Number of Divider IDs)*Step size between dividers
	 *  + The start of this range.*/
	div_range->div_range_end = (did_max - did_min) * range_step
		+ range_start;
	return true;
}

static uint32_t dal_divider_range_calc_divider(
	struct divider_range *div_range,
	uint32_t did)
{
	/* Is this DID within our range?*/
	if ((did < div_range->did_min) || (did >= div_range->did_max))
		return INVALID_DIVIDER;

	return ((did - div_range->did_min) * div_range->div_range_step)
			+ div_range->div_range_start;

}

static uint32_t dal_divider_range_calc_did(
	struct divider_range *div_range,
	uint32_t div)
{
	uint32_t did;
	/* Check before dividing.*/
	if (div_range->div_range_step == 0) {
		div_range->div_range_step = 1;
		/*div_range_step cannot be zero*/
		BREAK_TO_DEBUGGER();
	}
	/* Is this divider within our range?*/
	if ((div < div_range->div_range_start)
		|| (div >= div_range->div_range_end))
		return INVALID_DID;
/* did = (divider - range_start + (range_step-1)) / range_step) + did_min*/
	did = div - div_range->div_range_start;
	did += div_range->div_range_step - 1;
	did /= div_range->div_range_step;
	did += div_range->did_min;
	return did;
}

uint32_t dal_divider_range_get_divider(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t did)
{
	uint32_t div = INVALID_DIVIDER;
	uint32_t i;

	for (i = 0; i < ranges_num; i++) {
		/* Calculate divider with given divider ID*/
		div = dal_divider_range_calc_divider(&div_range[i], did);
		/* Found a valid return divider*/
		if (div != INVALID_DIVIDER)
			break;
	}
	return div;
}
uint32_t dal_divider_range_get_did(
	struct divider_range *div_range,
	uint32_t ranges_num,
	uint32_t divider)
{
	uint32_t did = INVALID_DID;
	uint32_t i;

	for (i = 0; i < ranges_num; i++) {
		/*  CalcDid returns InvalidDid if a divider ID isn't found*/
		did = dal_divider_range_calc_did(&div_range[i], divider);
		/* Found a valid return did*/
		if (did != INVALID_DID)
			break;
	}
	return did;
}

