/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
#include "amdgpu_display.h"
#include "hwmgr.h"
#include "amdgpu_smu.h"
#include "amdgpu_dpm_internal.h"

void amdgpu_dpm_get_active_displays(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;

	adev->pm.dpm.new_active_crtcs = 0;
	adev->pm.dpm.new_active_crtc_count = 0;
	if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc,
				    &ddev->mode_config.crtc_list, head) {
			amdgpu_crtc = to_amdgpu_crtc(crtc);
			if (amdgpu_crtc->enabled) {
				adev->pm.dpm.new_active_crtcs |= (1 << amdgpu_crtc->crtc_id);
				adev->pm.dpm.new_active_crtc_count++;
			}
		}
	}
}

u32 amdgpu_dpm_get_vblank_time(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;
	u32 vblank_in_pixels;
	u32 vblank_time_us = 0xffffffff; /* if the displays are off, vblank time is max */

	if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			amdgpu_crtc = to_amdgpu_crtc(crtc);
			if (crtc->enabled && amdgpu_crtc->enabled && amdgpu_crtc->hw_mode.clock) {
				vblank_in_pixels =
					amdgpu_crtc->hw_mode.crtc_htotal *
					(amdgpu_crtc->hw_mode.crtc_vblank_end -
					amdgpu_crtc->hw_mode.crtc_vdisplay +
					(amdgpu_crtc->v_border * 2));

				vblank_time_us = vblank_in_pixels * 1000 / amdgpu_crtc->hw_mode.clock;

				/* we have issues with mclk switching with
				 * refresh rates over 120 hz on the non-DC code.
				 */
				if (drm_mode_vrefresh(&amdgpu_crtc->hw_mode) > 120)
					vblank_time_us = 0;

				break;
			}
		}
	}

	return vblank_time_us;
}

u32 amdgpu_dpm_get_vrefresh(struct amdgpu_device *adev)
{
	struct drm_device *dev = adev_to_drm(adev);
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;
	u32 vrefresh = 0;

	if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
			amdgpu_crtc = to_amdgpu_crtc(crtc);
			if (crtc->enabled && amdgpu_crtc->enabled && amdgpu_crtc->hw_mode.clock) {
				vrefresh = drm_mode_vrefresh(&amdgpu_crtc->hw_mode);
				break;
			}
		}
	}

	return vrefresh;
}
