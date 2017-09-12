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


#ifndef __DAL_FIXED32_32_H__
#define __DAL_FIXED32_32_H__

#include "os_types.h"

struct fixed32_32 {
	uint64_t value;
};

static const struct fixed32_32 dal_fixed32_32_zero = { 0 };
static const struct fixed32_32 dal_fixed32_32_one = { 0x100000000LL };
static const struct fixed32_32 dal_fixed32_32_half = { 0x80000000LL };

struct fixed32_32 dal_fixed32_32_from_fraction(uint32_t n, uint32_t d);
struct fixed32_32 dal_fixed32_32_from_int(uint32_t value);
struct fixed32_32 dal_fixed32_32_add(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
struct fixed32_32 dal_fixed32_32_add_int(
	struct fixed32_32 lhs,
	uint32_t rhs);
struct fixed32_32 dal_fixed32_32_sub(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
struct fixed32_32 dal_fixed32_32_sub_int(
	struct fixed32_32 lhs,
	uint32_t rhs);
struct fixed32_32 dal_fixed32_32_mul(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
struct fixed32_32 dal_fixed32_32_mul_int(
	struct fixed32_32 lhs,
	uint32_t rhs);
struct fixed32_32 dal_fixed32_32_div(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
struct fixed32_32 dal_fixed32_32_div_int(
	struct fixed32_32 lhs,
	uint32_t rhs);
struct fixed32_32 dal_fixed32_32_min(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
struct fixed32_32 dal_fixed32_32_max(
	struct fixed32_32 lhs,
	struct fixed32_32 rhs);
bool dal_fixed32_32_gt(struct fixed32_32 lhs, struct fixed32_32 rhs);
bool dal_fixed32_32_gt_int(struct fixed32_32 lhs, uint32_t rhs);
bool dal_fixed32_32_lt(struct fixed32_32 lhs, struct fixed32_32 rhs);
bool dal_fixed32_32_lt_int(struct fixed32_32 lhs, uint32_t rhs);
bool dal_fixed32_32_le(struct fixed32_32 lhs, struct fixed32_32 rhs);
bool dal_fixed32_32_le_int(struct fixed32_32 lhs, uint32_t rhs);
bool dal_fixed32_32_eq(struct fixed32_32 lhs, struct fixed32_32 rhs);
uint32_t dal_fixed32_32_ceil(struct fixed32_32 value);
uint32_t dal_fixed32_32_floor(struct fixed32_32 value);
uint32_t dal_fixed32_32_round(struct fixed32_32 value);

#endif
