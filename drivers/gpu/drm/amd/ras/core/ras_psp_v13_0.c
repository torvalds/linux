// SPDX-License-Identifier: MIT
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

#include "ras.h"
#include "ras_psp_v13_0.h"

#define regMP0_SMN_C2PMSG_67                           0x0083
#define regMP0_SMN_C2PMSG_67_BASE_IDX                  0

static uint32_t ras_psp_v13_0_ring_wptr_get(struct ras_core_context *ras_core)
{
	return RAS_DEV_RREG32_SOC15(ras_core->dev, MP0, 0, regMP0_SMN_C2PMSG_67);
}

static int ras_psp_v13_0_ring_wptr_set(struct ras_core_context *ras_core, uint32_t value)
{
	RAS_DEV_WREG32_SOC15(ras_core->dev, MP0, 0, regMP0_SMN_C2PMSG_67, value);

	return 0;
}

const struct ras_psp_ip_func ras_psp_v13_0 = {
	.psp_ras_ring_wptr_get = ras_psp_v13_0_ring_wptr_get,
	.psp_ras_ring_wptr_set = ras_psp_v13_0_ring_wptr_set,
};
