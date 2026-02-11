/*
 * Copyright 2025 Advanced Micro Devices, Inc.
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
#include "amdgpu.h"
#include "smuio_v15_0_0.h"
#include "smuio/smuio_15_0_0_offset.h"
#include "smuio/smuio_15_0_0_sh_mask.h"
#include <linux/preempt.h>

static u64 smuio_v15_0_0_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	u64 clock;
	u64 clock_counter_lo, clock_counter_hi_pre, clock_counter_hi_after;

	preempt_disable();
	clock_counter_hi_pre = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_UPPER);
	clock_counter_lo = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_LOWER);
	/* the clock counter may be udpated during polling the counters */
	clock_counter_hi_after = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_UPPER);
	if (clock_counter_hi_pre != clock_counter_hi_after)
		clock_counter_lo = (u64)RREG32_SOC15(SMUIO, 0, regGOLDEN_TSC_COUNT_LOWER);
	preempt_enable();

	clock = clock_counter_lo | (clock_counter_hi_after << 32ULL);

	return clock;
}

const struct amdgpu_smuio_funcs smuio_v15_0_0_funcs = {
	.get_gpu_clock_counter = smuio_v15_0_0_get_gpu_clock_counter,
};
