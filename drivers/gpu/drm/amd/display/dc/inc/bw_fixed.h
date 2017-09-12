/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef BW_FIXED_H_
#define BW_FIXED_H_

struct bw_fixed {
	int64_t value;
};

struct bw_fixed bw_min3(struct bw_fixed v1, struct bw_fixed v2, struct bw_fixed v3);

struct bw_fixed bw_max3(struct bw_fixed v1, struct bw_fixed v2, struct bw_fixed v3);

struct bw_fixed bw_int_to_fixed(int64_t value);

int32_t bw_fixed_to_int(struct bw_fixed value);

struct bw_fixed bw_frc_to_fixed(int64_t num, int64_t denum);

struct bw_fixed fixed31_32_to_bw_fixed(int64_t raw);

struct bw_fixed bw_add(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_sub(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_mul(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_div(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_mod(const struct bw_fixed arg1, const struct bw_fixed arg2);

struct bw_fixed bw_min2(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_max2(const struct bw_fixed arg1, const struct bw_fixed arg2);
struct bw_fixed bw_floor2(const struct bw_fixed arg, const struct bw_fixed significance);
struct bw_fixed bw_ceil2(const struct bw_fixed arg, const struct bw_fixed significance);

bool bw_equ(const struct bw_fixed arg1, const struct bw_fixed arg2);
bool bw_neq(const struct bw_fixed arg1, const struct bw_fixed arg2);
bool bw_leq(const struct bw_fixed arg1, const struct bw_fixed arg2);
bool bw_meq(const struct bw_fixed arg1, const struct bw_fixed arg2);
bool bw_ltn(const struct bw_fixed arg1, const struct bw_fixed arg2);
bool bw_mtn(const struct bw_fixed arg1, const struct bw_fixed arg2);

#endif //BW_FIXED_H_
