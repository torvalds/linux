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
 */

#ifndef _PP_OVERDRIVER_H_
#define _PP_OVERDRIVER_H_

#include <linux/types.h>
#include <linux/kernel.h>

struct phm_fuses_default {
	uint64_t key;
	uint32_t VFT2_m1;
	uint32_t VFT2_m2;
	uint32_t VFT2_b;
	uint32_t VFT1_m1;
	uint32_t VFT1_m2;
	uint32_t VFT1_b;
	uint32_t VFT0_m1;
	uint32_t VFT0_m2;
	uint32_t VFT0_b;
};

extern int pp_override_get_default_fuse_value(uint64_t key,
			struct phm_fuses_default *result);

#endif