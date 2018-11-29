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
#include <drm/drmP.h>
#include "amdgpu.h"
#include "amdgpu_smu.h"
#include "soc15_common.h"
#include "smu_v11_0.h"

static int smu_set_funcs(struct amdgpu_device *adev)
{
	struct smu_context *smu = &adev->smu;

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		smu_v11_0_set_smu_funcs(smu);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int smu_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	ret = smu_set_funcs(adev);
	if (ret)
		return ret;

	smu->adev = adev;
	mutex_init(&smu->mutex);

	return 0;
}

static int smu_sw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;
	int ret;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	ret = smu_init_microcode(smu);
	if (ret) {
		pr_err("Failed to load smu firmware!\n");
		return ret;
	}

	return 0;
}

static int smu_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	return 0;
}

static int smu_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	/* TODO */

	mutex_unlock(&smu->mutex);

	pr_info("SMU is initialized successfully!\n");

	return 0;
}

static int smu_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	return 0;
}

static int smu_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	return 0;
}

static int smu_resume(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	struct smu_context *smu = &adev->smu;

	if (adev->asic_type < CHIP_VEGA20)
		return -EINVAL;

	mutex_lock(&smu->mutex);

	/* TODO */

	mutex_unlock(&smu->mutex);

	return 0;
}

static int smu_set_clockgating_state(void *handle,
				     enum amd_clockgating_state state)
{
	return 0;
}

static int smu_set_powergating_state(void *handle,
				     enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs smu_ip_funcs = {
	.name = "smu",
	.early_init = smu_early_init,
	.late_init = NULL,
	.sw_init = smu_sw_init,
	.sw_fini = smu_sw_fini,
	.hw_init = smu_hw_init,
	.hw_fini = smu_hw_fini,
	.suspend = smu_suspend,
	.resume = smu_resume,
	.is_idle = NULL,
	.check_soft_reset = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.set_clockgating_state = smu_set_clockgating_state,
	.set_powergating_state = smu_set_powergating_state,
};

const struct amdgpu_ip_block_version smu_v11_0_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 11,
	.minor = 0,
	.rev = 0,
	.funcs = &smu_ip_funcs,
};
