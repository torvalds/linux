/*
 * Copyright 2009 Red Hat Inc.
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
 * Authors: Dave Airlie
 */
#ifndef RADEON_FIXED_H
#define RADEON_FIXED_H

typedef union rfixed {
	u32 full;
} fixed20_12;


#define rfixed_const(A) (u32)(((A) << 12))/*  + ((B + 0.000122)*4096)) */
#define rfixed_const_half(A) (u32)(((A) << 12) + 2048)
#define rfixed_const_666(A) (u32)(((A) << 12) + 2731)
#define rfixed_const_8(A) (u32)(((A) << 12) + 3277)
#define rfixed_mul(A, B) ((u64)((u64)(A).full * (B).full + 2048) >> 12)
#define fixed_init(A) { .full = rfixed_const((A)) }
#define fixed_init_half(A) { .full = rfixed_const_half((A)) }
#define rfixed_trunc(A) ((A).full >> 12)

static inline u32 rfixed_floor(fixed20_12 A)
{
	u32 non_frac = rfixed_trunc(A);

	return rfixed_const(non_frac);
}

static inline u32 rfixed_ceil(fixed20_12 A)
{
	u32 non_frac = rfixed_trunc(A);

	if (A.full > rfixed_const(non_frac))
		return rfixed_const(non_frac + 1);
	else
		return rfixed_const(non_frac);
}

static inline u32 rfixed_div(fixed20_12 A, fixed20_12 B)
{
	u64 tmp = ((u64)A.full << 13);

	do_div(tmp, B.full);
	tmp += 1;
	tmp /= 2;
	return lower_32_bits(tmp);
}
#endif
