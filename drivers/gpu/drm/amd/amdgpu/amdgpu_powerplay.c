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
#include "atom.h"
#include "amdgpu.h"
#include "amd_shared.h"
#include <linux/module.h>
#include <linux/moduleparam.h>
#include "amdgpu_pm.h"
#include <drm/amdgpu_drm.h>
#include "amdgpu_powerplay.h"
#include "si_dpm.h"
#include "cik_dpm.h"
#include "vi_dpm.h"

static int amdgpu_powerplay_init(struct amdgpu_device *adev)
{
	int ret = 0;
	struct amd_powerplay *amd_pp;

	amd_pp = &(adev->powerplay);

	if (adev->pp_enabled) {
		struct amd_pp_init *pp_init;

		pp_init = kzalloc(sizeof(struct amd_pp_init), GFP_KERNEL);

		if (pp_init == NULL)
			return -ENOMEM;

		pp_init->chip_family = adev->family;
		pp_init->chip_id = adev->asic_type;
		pp_init->device = amdgpu_cgs_create_device(adev);
		ret = amd_powerplay_init(pp_init, amd_pp);
		kfree(pp_init);
	} else {
		amd_pp->pp_handle = (void *)adev;

		switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_SI
		case CHIP_TAHITI:
		case CHIP_PITCAIRN:
		case CHIP_VERDE:
		case CHIP_OLAND:
		case CHIP_HAINAN:
			amd_pp->ip_funcs = &si_dpm_ip_funcs;
		break;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
		case CHIP_BONAIRE:
		case CHIP_HAWAII:
			amd_pp->ip_funcs = &ci_dpm_ip_funcs;
			break;
		case CHIP_KABINI:
		case CHIP_MULLINS:
		case CHIP_KAVERI:
			amd_pp->ip_funcs = &kv_dpm_ip_funcs;
			break;
#endif
		case CHIP_CARRIZO:
		case CHIP_STONEY:
			amd_pp->ip_funcs = &cz_dpm_ip_funcs;
			break;
		default:
			ret = -EINVAL;
			break;
		}
	}
	return ret;
}

static int amdgpu_pp_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int ret = 0;

	switch (adev->asic_type) {
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
	case CHIP_POLARIS12:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_TOPAZ:
		adev->pp_enabled = true;
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		adev->pp_enabled = (amdgpu_powerplay == 0) ? false : true;
		break;
	/* These chips don't have powerplay implemenations */
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_KAVERI:
	default:
		adev->pp_enabled = false;
		break;
	}

	ret = amdgpu_powerplay_init(adev);
	if (ret)
		return ret;

	if (adev->powerplay.ip_funcs->early_init)
		ret = adev->powerplay.ip_funcs->early_init(
					adev->powerplay.pp_handle);
	return ret;
}


static int amdgpu_pp_late_init(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->late_init)
		ret = adev->powerplay.ip_funcs->late_init(
					adev->powerplay.pp_handle);

	if (adev->pp_enabled && adev->pm.dpm_enabled) {
		amdgpu_pm_sysfs_init(adev);
		amdgpu_dpm_dispatch_task(adev, AMD_PP_EVENT_COMPLETE_INIT, NULL, NULL);
	}

	return ret;
}

static int amdgpu_pp_sw_init(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->sw_init)
		ret = adev->powerplay.ip_funcs->sw_init(
					adev->powerplay.pp_handle);

	return ret;
}

static int amdgpu_pp_sw_fini(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->sw_fini)
		ret = adev->powerplay.ip_funcs->sw_fini(
					adev->powerplay.pp_handle);
	if (ret)
		return ret;

	return ret;
}

static int amdgpu_pp_hw_init(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pp_enabled && adev->firmware.smu_load)
		amdgpu_ucode_init_bo(adev);

	if (adev->powerplay.ip_funcs->hw_init)
		ret = adev->powerplay.ip_funcs->hw_init(
					adev->powerplay.pp_handle);

	if ((amdgpu_dpm != 0) && !amdgpu_sriov_vf(adev))
		adev->pm.dpm_enabled = true;

	return ret;
}

static int amdgpu_pp_hw_fini(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->hw_fini)
		ret = adev->powerplay.ip_funcs->hw_fini(
					adev->powerplay.pp_handle);

	if (adev->pp_enabled && adev->firmware.smu_load)
		amdgpu_ucode_fini_bo(adev);

	return ret;
}

static void amdgpu_pp_late_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->pp_enabled) {
		amdgpu_pm_sysfs_fini(adev);
		amd_powerplay_fini(adev->powerplay.pp_handle);
	}

	if (adev->powerplay.ip_funcs->late_fini)
		adev->powerplay.ip_funcs->late_fini(
			  adev->powerplay.pp_handle);
}

static int amdgpu_pp_suspend(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->suspend)
		ret = adev->powerplay.ip_funcs->suspend(
					 adev->powerplay.pp_handle);
	return ret;
}

static int amdgpu_pp_resume(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->resume)
		ret = adev->powerplay.ip_funcs->resume(
					adev->powerplay.pp_handle);
	return ret;
}

static int amdgpu_pp_set_clockgating_state(void *handle,
					enum amd_clockgating_state state)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->set_clockgating_state)
		ret = adev->powerplay.ip_funcs->set_clockgating_state(
				adev->powerplay.pp_handle, state);
	return ret;
}

static int amdgpu_pp_set_powergating_state(void *handle,
					enum amd_powergating_state state)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->set_powergating_state)
		ret = adev->powerplay.ip_funcs->set_powergating_state(
				 adev->powerplay.pp_handle, state);
	return ret;
}


static bool amdgpu_pp_is_idle(void *handle)
{
	bool ret = true;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->is_idle)
		ret = adev->powerplay.ip_funcs->is_idle(
					adev->powerplay.pp_handle);
	return ret;
}

static int amdgpu_pp_wait_for_idle(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->wait_for_idle)
		ret = adev->powerplay.ip_funcs->wait_for_idle(
					adev->powerplay.pp_handle);
	return ret;
}

static int amdgpu_pp_soft_reset(void *handle)
{
	int ret = 0;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	if (adev->powerplay.ip_funcs->soft_reset)
		ret = adev->powerplay.ip_funcs->soft_reset(
					adev->powerplay.pp_handle);
	return ret;
}

static const struct amd_ip_funcs amdgpu_pp_ip_funcs = {
	.name = "amdgpu_powerplay",
	.early_init = amdgpu_pp_early_init,
	.late_init = amdgpu_pp_late_init,
	.sw_init = amdgpu_pp_sw_init,
	.sw_fini = amdgpu_pp_sw_fini,
	.hw_init = amdgpu_pp_hw_init,
	.hw_fini = amdgpu_pp_hw_fini,
	.late_fini = amdgpu_pp_late_fini,
	.suspend = amdgpu_pp_suspend,
	.resume = amdgpu_pp_resume,
	.is_idle = amdgpu_pp_is_idle,
	.wait_for_idle = amdgpu_pp_wait_for_idle,
	.soft_reset = amdgpu_pp_soft_reset,
	.set_clockgating_state = amdgpu_pp_set_clockgating_state,
	.set_powergating_state = amdgpu_pp_set_powergating_state,
};

const struct amdgpu_ip_block_version amdgpu_pp_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_SMC,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &amdgpu_pp_ip_funcs,
};
