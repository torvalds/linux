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
#include "include/fixed32_32.h"

static uint64_t u64_div(uint64_t n, uint64_t d)
{
	uint32_t i = 0;
	uint64_t r;
	uint64_t q = div64_u64_rem(n, d, &r);

	for (i = 0; i < 32; ++i) {
		uint64_t sbit = q & (1ULL<<63);

		r <<= 1;
		r |= sbit ? 1 : 0;
		q <<= 1;
		if (r >= d) {
			r -= d;
			q |= 1;
		}
	}

	if (2*r >= d)
		q += 1;
	return q;
}

struct fixed32_32 dal_fixed32_32_from_fraction(uint32_t n, uint32_t d)
{
	struct fixed32_32 fx;

	fx.value = u64_div((uint64_t)n << 32, (uint64_t)d << 32);
	return fx;
}

struct fixed32_32 dal_fixed32_32_from_int(uint32_t value)
{
	struct fixed32_32 fx;

	fx.value = (uint64_t)value<<32;
	return fx;
}

struct fixed32_32 dal_fixed32_32_add(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	struct fixed32_32 fx = {lhs.value + rhs.value};

	ASSERT(fx.value >= rhs.value);
	return fx;
}

struct fixed32_32 dal_fixed32_32_add_int(struct fixed32_32 lhs, uint32_t rhs)
{
	struct fixed32_32 fx = {lhs.value + ((uint64_t)rhs << 32)};

	ASSERT(fx.value >= (uint64_t)rhs << 32);
	return fx;

}
struct fixed32_32 dal_fixed32_32_sub(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	struct fixed32_32 fx;

	ASSERT(lhs.value >= rhs.value);
	fx.value = lhs.value - rhs.value;
	return fx;
}

struct fixed32_32 dal_fixed32_32_sub_int(struct fixed32_32 lhs, uint32_t rhs)
{
	struct fixed32_32 fx;

	ASSERT(lhs.value >= ((uint64_t)rhs<<32));
	fx.value = lhs.value - ((uint64_t)rhs<<32);
	return fx;
}

struct fixed32_32 dal_fixed32_32_mul(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	struct fixed32_32 fx;
	uint64_t lhs_int = lhs.value>>32;
	uint64_t lhs_frac = (uint32_t)lhs.value;
	uint64_t rhs_int = rhs.value>>32;
	uint64_t rhs_frac = (uint32_t)rhs.value;
	uint64_t ahbh = lhs_int * rhs_int;
	uint64_t ahbl = lhs_int * rhs_frac;
	uint64_t albh = lhs_frac * rhs_int;
	uint64_t albl = lhs_frac * rhs_frac;

	ASSERT((ahbh>>32) == 0);

	fx.value = (ahbh<<32) + ahbl + albh + (albl>>32);
	return fx;

}

struct fixed32_32 dal_fixed32_32_mul_int(struct fixed32_32 lhs, uint32_t rhs)
{
	struct fixed32_32 fx;
	uint64_t lhsi = (lhs.value>>32) * (uint64_t)rhs;
	uint64_t lhsf;

	ASSERT((lhsi>>32) == 0);
	lhsf = ((uint32_t)lhs.value) * (uint64_t)rhs;
	ASSERT((lhsi<<32) + lhsf >= lhsf);
	fx.value = (lhsi<<32) + lhsf;
	return fx;
}

struct fixed32_32 dal_fixed32_32_div(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	struct fixed32_32 fx;

	fx.value = u64_div(lhs.value, rhs.value);
	return fx;
}

struct fixed32_32 dal_fixed32_32_div_int(struct fixed32_32 lhs, uint32_t rhs)
{
	struct fixed32_32 fx;

	fx.value = u64_div(lhs.value, (uint64_t)rhs << 32);
	return fx;
}

struct fixed32_32 dal_fixed32_32_min(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	return (lhs.value < rhs.value) ? lhs : rhs;
}

struct fixed32_32 dal_fixed32_32_max(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs)
{
	return (lhs.value > rhs.value) ? lhs : rhs;
}

bool dal_fixed32_32_gt(struct fixed32_32 lhs, struct fixed32_32 rhs)
{
	return lhs.value > rhs.value;
}
bool dal_fixed32_32_gt_int(struct fixed32_32 lhs, uint32_t rhs)
{
	return lhs.value > ((uint64_t)rhs<<32);
}

bool dal_fixed32_32_lt(struct fixed32_32 lhs, struct fixed32_32 rhs)
{
	return lhs.value < rhs.value;
}

bool dal_fixed32_32_le(struct fixed32_32 lhs, struct fixed32_32 rhs)
{
	return lhs.value <= rhs.value;
}

bool dal_fixed32_32_lt_int(struct fixed32_32 lhs, uint32_t rhs)
{
	return lhs.value < ((uint64_t)rhs<<32);
}

bool dal_fixed32_32_le_int(struct fixed32_32 lhs, uint32_t rhs)
{
	return lhs.value <= ((uint64_t)rhs<<32);
}

uint32_t dal_fixed32_32_ceil(struct fixed32_32 v)
{
	ASSERT((uint32_t)v.value ? (v.value >> 32) + 1 >= 1 : true);
	return (v.value>>32) + ((uint32_t)v.value ? 1 : 0);
}

uint32_t dal_fixed32_32_floor(struct fixed32_32 v)
{
	return v.value>>32;
}

uint32_t dal_fixed32_32_round(struct fixed32_32 v)
{
	ASSERT(v.value + (1ULL<<31) >= (1ULL<<31));
	return (v.value + (1ULL<<31))>>32;
}

bool dal_fixed32_32_eq(struct fixed32_32 lhs, struct fixed32_32 rhs)
{
	return lhs.value == rhs.value;
}
