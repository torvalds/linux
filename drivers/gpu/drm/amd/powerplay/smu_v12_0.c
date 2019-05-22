/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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
 */

#include "pp_debug.h"
#include <linux/firmware.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "atomfirmware.h"
#include "amdgpu_atomfirmware.h"
#include "smu_v12_0.h"
#include "soc15_common.h"
#include "atom.h"
#include "renoir_ppt.h"

#include "asic_reg/mp/mp_12_0_0_offset.h"
#include "asic_reg/mp/mp_12_0_0_sh_mask.h"

#define smnMP1_FIRMWARE_FLAGS                                0x3010024

static int smu_v12_0_send_msg_without_waiting(struct smu_context *smu,
					      uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_66, msg);
	return 0;
}

static int smu_v12_0_read_arg(struct smu_context *smu, uint32_t *arg)
{
	struct amdgpu_device *adev = smu->adev;

	*arg = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82);
	return 0;
}

static int smu_v12_0_wait_for_response(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;
	uint32_t cur_value, i;

	for (i = 0; i < adev->usec_timeout; i++) {
		cur_value = RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90);
		if ((cur_value & MP1_C2PMSG_90__CONTENT_MASK) != 0)
			break;
		udelay(1);
	}

	/* timeout means wrong logic */
	if (i == adev->usec_timeout)
		return -ETIME;

	return RREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90) == 0x1 ? 0 : -EIO;
}

static int smu_v12_0_send_msg(struct smu_context *smu, uint16_t msg)
{
	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	smu_v12_0_wait_for_response(smu);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	smu_v12_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v12_0_wait_for_response(smu);

	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x\n", index,
		       ret);

	return ret;

}

static int
smu_v12_0_send_msg_with_param(struct smu_context *smu, uint16_t msg,
			      uint32_t param)
{

	struct amdgpu_device *adev = smu->adev;
	int ret = 0, index = 0;

	index = smu_msg_get_index(smu, msg);
	if (index < 0)
		return index;

	ret = smu_v12_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x, param 0x%x\n",
		       index, ret, param);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_90, 0);

	WREG32_SOC15(MP1, 0, mmMP1_SMN_C2PMSG_82, param);

	smu_v12_0_send_msg_without_waiting(smu, (uint16_t)index);

	ret = smu_v12_0_wait_for_response(smu);
	if (ret)
		pr_err("Failed to send message 0x%x, response 0x%x param 0x%x\n",
		       index, ret, param);

	return ret;
}

static int smu_v12_0_check_fw_version(struct smu_context *smu)
{
	uint32_t smu_version = 0xff;
	int ret = 0;

	ret = smu_send_smc_msg(smu, SMU_MSG_GetDriverIfVersion);
	if (ret)
		goto err;

	ret = smu_read_smc_arg(smu, &smu_version);
	if (ret)
		goto err;

	if (smu_version != smu->smc_if_version)
		ret = -EINVAL;
err:
	return ret;
}

static const struct smu_funcs smu_v12_0_funcs = {
	.check_fw_version = smu_v12_0_check_fw_version,
	.send_smc_msg = smu_v12_0_send_msg,
	.send_smc_msg_with_param = smu_v12_0_send_msg_with_param,
	.read_smc_arg = smu_v12_0_read_arg,
};

void smu_v12_0_set_smu_funcs(struct smu_context *smu)
{
	struct amdgpu_device *adev = smu->adev;

	smu->funcs = &smu_v12_0_funcs;

	switch (adev->asic_type) {
	case CHIP_RENOIR:
		renoir_set_ppt_funcs(smu);
		break;
	default:
		pr_warn("Unknown asic for smu12\n");
	}
}
