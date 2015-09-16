/*
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include <linux/firmware.h>
#include "drmP.h"
#include "amdgpu.h"
#include "fiji_smumgr.h"

MODULE_FIRMWARE("amdgpu/fiji_smc.bin");

static void fiji_dpm_set_funcs(struct amdgpu_device *adev);

static int fiji_dpm_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	fiji_dpm_set_funcs(adev);

	return 0;
}

static int fiji_dpm_init_microcode(struct amdgpu_device *adev)
{
	char fw_name[30] = "amdgpu/fiji_smc.bin";
	int err;

	err = request_firmware(&adev->pm.fw, fw_name, adev->dev);
	if (err)
		goto out;
	err = amdgpu_ucode_validate(adev->pm.fw);

out:
	if (err) {
		DRM_ERROR("Failed to load firmware \"%s\"", fw_name);
		release_firmware(adev->pm.fw);
		adev->pm.fw = NULL;
	}
	return err;
}

static int fiji_dpm_sw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	ret = fiji_dpm_init_microcode(adev);
	if (ret)
		return ret;

	return 0;
}

static int fiji_dpm_sw_fini(void *handle)
{
	return 0;
}

static int fiji_dpm_hw_init(void *handle)
{
	int ret;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	mutex_lock(&adev->pm.mutex);

	ret = fiji_smu_init(adev);
	if (ret) {
		DRM_ERROR("SMU initialization failed\n");
		goto fail;
	}

	ret = fiji_smu_start(adev);
	if (ret) {
		DRM_ERROR("SMU start failed\n");
		goto fail;
	}

	mutex_unlock(&adev->pm.mutex);
	return 0;

fail:
	adev->firmware.smu_load = false;
	mutex_unlock(&adev->pm.mutex);
	return -EINVAL;
}

static int fiji_dpm_hw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	mutex_lock(&adev->pm.mutex);
	fiji_smu_fini(adev);
	mutex_unlock(&adev->pm.mutex);
	return 0;
}

static int fiji_dpm_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	fiji_dpm_hw_fini(adev);

	return 0;
}

static int fiji_dpm_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	fiji_dpm_hw_init(adev);

	return 0;
}

static int fiji_dpm_set_clockgating_state(void *handle,
			enum amd_clockgating_state state)
{
	return 0;
}

static int fiji_dpm_set_powergating_state(void *handle,
			enum amd_powergating_state state)
{
	return 0;
}

const struct amd_ip_funcs fiji_dpm_ip_funcs = {
	.early_init = fiji_dpm_early_init,
	.late_init = NULL,
	.sw_init = fiji_dpm_sw_init,
	.sw_fini = fiji_dpm_sw_fini,
	.hw_init = fiji_dpm_hw_init,
	.hw_fini = fiji_dpm_hw_fini,
	.suspend = fiji_dpm_suspend,
	.resume = fiji_dpm_resume,
	.is_idle = NULL,
	.wait_for_idle = NULL,
	.soft_reset = NULL,
	.print_status = NULL,
	.set_clockgating_state = fiji_dpm_set_clockgating_state,
	.set_powergating_state = fiji_dpm_set_powergating_state,
};

static const struct amdgpu_dpm_funcs fiji_dpm_funcs = {
	.get_temperature = NULL,
	.pre_set_power_state = NULL,
	.set_power_state = NULL,
	.post_set_power_state = NULL,
	.display_configuration_changed = NULL,
	.get_sclk = NULL,
	.get_mclk = NULL,
	.print_power_state = NULL,
	.debugfs_print_current_performance_level = NULL,
	.force_performance_level = NULL,
	.vblank_too_short = NULL,
	.powergate_uvd = NULL,
};

static void fiji_dpm_set_funcs(struct amdgpu_device *adev)
{
	if (NULL == adev->pm.funcs)
		adev->pm.funcs = &fiji_dpm_funcs;
}
