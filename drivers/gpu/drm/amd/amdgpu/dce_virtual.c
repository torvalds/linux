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

#include <drm/drm_atomic_helper.h>

#include "amdgpu.h"
#ifdef CONFIG_DRM_AMDGPU_SI
#include "dce_v6_0.h"
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
#include "dce_v8_0.h"
#endif
#include "dce_v10_0.h"
#include "dce_v11_0.h"
#include "dce_virtual.h"
#include "ivsrcid/ivsrcid_vislands30.h"
#include "amdgpu_display.h"
#include "amdgpu_vkms.h"

const struct drm_mode_config_funcs dce_virtual_mode_funcs = {
	.fb_create = amdgpu_display_user_framebuffer_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int dce_virtual_sw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	adev_to_drm(adev)->max_vblank_count = 0;

	adev_to_drm(adev)->mode_config.funcs = &dce_virtual_mode_funcs;

	adev_to_drm(adev)->mode_config.max_width = XRES_MAX;
	adev_to_drm(adev)->mode_config.max_height = YRES_MAX;

	adev_to_drm(adev)->mode_config.preferred_depth = 24;
	adev_to_drm(adev)->mode_config.prefer_shadow = 1;

	adev_to_drm(adev)->mode_config.fb_base = adev->gmc.aper_base;

	r = amdgpu_display_modeset_create_props(adev);
	if (r)
		return r;

	adev->amdgpu_vkms_output = kcalloc(adev->mode_info.num_crtc, sizeof(struct amdgpu_vkms_output), GFP_KERNEL);

	/* allocate crtcs, encoders, connectors */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = amdgpu_vkms_output_init(adev_to_drm(adev), &adev->amdgpu_vkms_output[i], i);
		if (r)
			return r;
	}

	drm_kms_helper_poll_init(adev_to_drm(adev));

	adev->mode_info.mode_config_initialized = true;
	return 0;
}

static int dce_virtual_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i = 0;

	for (i = 0; i < adev->mode_info.num_crtc; i++)
		if (adev->mode_info.crtcs[i])
			hrtimer_cancel(&adev->mode_info.crtcs[i]->vblank_timer);

	kfree(adev->mode_info.bios_hardcoded_edid);
	kfree(adev->amdgpu_vkms_output);

	drm_kms_helper_poll_fini(adev_to_drm(adev));

	adev->mode_info.mode_config_initialized = false;
	return 0;
}

static int dce_virtual_hw_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	switch (adev->asic_type) {
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
		dce_v6_0_disable_dce(adev);
		break;
#endif
#ifdef CONFIG_DRM_AMDGPU_CIK
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
		dce_v8_0_disable_dce(adev);
		break;
#endif
	case CHIP_FIJI:
	case CHIP_TONGA:
		dce_v10_0_disable_dce(adev);
		break;
	case CHIP_CARRIZO:
	case CHIP_STONEY:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_VEGAM:
		dce_v11_0_disable_dce(adev);
		break;
	case CHIP_TOPAZ:
#ifdef CONFIG_DRM_AMDGPU_SI
	case CHIP_HAINAN:
#endif
		/* no DCE */
		break;
	default:
		break;
	}
	return 0;
}

static int dce_virtual_hw_fini(void *handle)
{
	return 0;
}

static int dce_virtual_suspend(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = drm_mode_config_helper_suspend(adev_to_drm(adev));
	if (r)
		return r;
	return dce_virtual_hw_fini(handle);
}

static int dce_virtual_resume(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int r;

	r = dce_virtual_hw_init(handle);
	if (r)
		return r;
	return drm_mode_config_helper_resume(adev_to_drm(adev));
}

static bool dce_virtual_is_idle(void *handle)
{
	return true;
}

static int dce_virtual_wait_for_idle(void *handle)
{
	return 0;
}

static int dce_virtual_soft_reset(void *handle)
{
	return 0;
}

static int dce_virtual_set_clockgating_state(void *handle,
					  enum amd_clockgating_state state)
{
	return 0;
}

static int dce_virtual_set_powergating_state(void *handle,
					  enum amd_powergating_state state)
{
	return 0;
}

static const struct amd_ip_funcs dce_virtual_ip_funcs = {
	.name = "dce_virtual",
	.early_init = NULL,
	.late_init = NULL,
	.sw_init = dce_virtual_sw_init,
	.sw_fini = dce_virtual_sw_fini,
	.hw_init = dce_virtual_hw_init,
	.hw_fini = dce_virtual_hw_fini,
	.suspend = dce_virtual_suspend,
	.resume = dce_virtual_resume,
	.is_idle = dce_virtual_is_idle,
	.wait_for_idle = dce_virtual_wait_for_idle,
	.soft_reset = dce_virtual_soft_reset,
	.set_clockgating_state = dce_virtual_set_clockgating_state,
	.set_powergating_state = dce_virtual_set_powergating_state,
};

const struct amdgpu_ip_block_version dce_virtual_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &dce_virtual_ip_funcs,
};
