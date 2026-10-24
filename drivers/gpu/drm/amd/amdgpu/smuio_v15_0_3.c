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
#include "smuio_v15_0_3.h"
#include "soc15_common.h"
#include "smuio/smuio_15_0_3_offset.h"
#include "smuio/smuio_15_0_3_sh_mask.h"
#include <linux/preempt.h>

static u64 smuio_v15_0_3_get_gpu_clock_counter(struct amdgpu_device *adev)
{
	u32 clock_counter_lo, clock_counter_hi, clock_counter_hi_check;

	preempt_disable();
	do {
		clock_counter_hi = RREG32_SOC15(SMUIO, 0, regCCD_GOLDEN_TSC_COUNT_UPPER);
		clock_counter_lo = RREG32_SOC15(SMUIO, 0, regCCD_GOLDEN_TSC_COUNT_LOWER);
		/* Retry if the counter rolled over while polling the registers. */
		clock_counter_hi_check = RREG32_SOC15(SMUIO, 0, regCCD_GOLDEN_TSC_COUNT_UPPER);
	} while (clock_counter_hi != clock_counter_hi_check);
	preempt_enable();

	return clock_counter_lo | ((u64)clock_counter_hi << 32ULL);
}

/**
 * smuio_v15_0_3_get_die_id - query die id from FCH.
 *
 * @adev: amdgpu device pointer
 *
 * Returns die id
 */
static u32 smuio_v15_0_3_get_die_id(struct amdgpu_device *adev)
{
	u32 data, die_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	die_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, DIE_ID);

	return die_id;
}

/**
 * smuio_v15_0_3_get_socket_id - query socket id from FCH
 *
 * @adev: amdgpu device pointer
 *
 * Returns socket id
 */
static u32 smuio_v15_0_3_get_socket_id(struct amdgpu_device *adev)
{
	u32 data, socket_id;

	data = RREG32_SOC15(SMUIO, 0, regSMUIO_MCM_CONFIG);
	socket_id = REG_GET_FIELD(data, SMUIO_MCM_CONFIG, SOCKET_ID);

	return socket_id;
}

const struct amdgpu_smuio_funcs smuio_v15_0_3_funcs = {
	.get_gpu_clock_counter = smuio_v15_0_3_get_gpu_clock_counter,
	.get_die_id = smuio_v15_0_3_get_die_id,
	.get_socket_id = smuio_v15_0_3_get_socket_id,
};
