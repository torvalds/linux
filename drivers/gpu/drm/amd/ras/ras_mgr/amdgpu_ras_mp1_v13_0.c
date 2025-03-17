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
#include "amdgpu_smu.h"
#include "amdgpu_reset.h"
#include "amdgpu_ras_mp1_v13_0.h"

#define RAS_MP1_MSG_QueryValidMcaCeCount  0x3A
#define RAS_MP1_MSG_McaBankCeDumpDW       0x3B

static int mp1_v13_0_get_valid_bank_count(struct ras_core_context *ras_core,
					  u32 msg, u32 *count)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	u32 smu_msg;
	int ret = 0;

	if (!count)
		return -EINVAL;

	smu_msg = (msg == RAS_MP1_MSG_QueryValidMcaCeCount) ?
			SMU_MSG_QueryValidMcaCeCount : SMU_MSG_QueryValidMcaCount;

	if (down_read_trylock(&adev->reset_domain->sem)) {
		ret = amdgpu_smu_ras_send_msg(adev, smu_msg, 0, count);
		up_read(&adev->reset_domain->sem);
	} else {
		ret = -RAS_CORE_GPU_IN_MODE1_RESET;
	}

	if (ret)
		*count = 0;

	return ret;
}

static int mp1_v13_0_dump_valid_bank(struct ras_core_context *ras_core,
				     u32 msg, u32 idx, u32 reg_idx, u64 *val)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)ras_core->dev;
	uint32_t data[2] = {0, 0};
	uint32_t param;
	int ret = 0;
	int i, offset;
	u32 smu_msg = (msg == RAS_MP1_MSG_McaBankCeDumpDW) ?
			     SMU_MSG_McaBankCeDumpDW : SMU_MSG_McaBankDumpDW;

	if (down_read_trylock(&adev->reset_domain->sem)) {
		offset = reg_idx * 8;
		for (i = 0; i < ARRAY_SIZE(data); i++) {
			param = ((idx & 0xffff) << 16) | ((offset + (i << 2)) & 0xfffc);
			ret = amdgpu_smu_ras_send_msg(adev, smu_msg, param, &data[i]);
			if (ret) {
				RAS_DEV_ERR(adev, "ACA failed to read register[%d], offset:0x%x\n",
					reg_idx, offset);
				break;
			}
		}
		up_read(&adev->reset_domain->sem);

		if (!ret)
			*val = (uint64_t)data[1] << 32 | data[0];
	} else {
		ret = -RAS_CORE_GPU_IN_MODE1_RESET;
	}

	return ret;
}

const struct ras_mp1_sys_func amdgpu_ras_mp1_sys_func_v13_0 = {
	.mp1_get_valid_bank_count = mp1_v13_0_get_valid_bank_count,
	.mp1_dump_valid_bank = mp1_v13_0_dump_valid_bank,
};

