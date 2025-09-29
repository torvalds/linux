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

void amdgpu_dpm_get_display_cfg(struct amdgpu_device *adev)
{
	struct drm_device *ddev = adev_to_drm(adev);
	struct amd_pp_display_configuration *cfg = &adev->pm.pm_display_cfg;
	struct single_display_configuration *display_cfg;
	struct drm_crtc *crtc;
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_connector *conn;
	int num_crtcs = 0;
	int vrefresh;
	u32 vblank_in_pixels, vblank_time_us;

	cfg->min_vblank_time = 0xffffffff; /* if the displays are off, vblank time is max */

	if (adev->mode_info.num_crtc && adev->mode_info.mode_config_initialized) {
		list_for_each_entry(crtc, &ddev->mode_config.crtc_list, head) {
			amdgpu_crtc = to_amdgpu_crtc(crtc);

			/* The array should only contain active displays. */
			if (!amdgpu_crtc->enabled)
				continue;

			conn = to_amdgpu_connector(amdgpu_crtc->connector);
			display_cfg = &adev->pm.pm_display_cfg.displays[num_crtcs++];

			if (amdgpu_crtc->hw_mode.clock) {
				vrefresh = drm_mode_vrefresh(&amdgpu_crtc->hw_mode);

				vblank_in_pixels =
					amdgpu_crtc->hw_mode.crtc_htotal *
					(amdgpu_crtc->hw_mode.crtc_vblank_end -
					amdgpu_crtc->hw_mode.crtc_vdisplay +
					(amdgpu_crtc->v_border * 2));

				vblank_time_us =
					vblank_in_pixels * 1000 / amdgpu_crtc->hw_mode.clock;

				/* The legacy (non-DC) code has issues with mclk switching
				 * with refresh rates over 120 Hz. Disable mclk switching.
				 */
				if (vrefresh > 120)
					vblank_time_us = 0;

				/* Find minimum vblank time. */
				if (vblank_time_us < cfg->min_vblank_time)
					cfg->min_vblank_time = vblank_time_us;

				/* Find vertical refresh rate of first active display. */
				if (!cfg->vrefresh)
					cfg->vrefresh = vrefresh;
			}

			if (amdgpu_crtc->crtc_id < cfg->crtc_index) {
				/* Find first active CRTC and its line time. */
				cfg->crtc_index = amdgpu_crtc->crtc_id;
				cfg->line_time_in_us = amdgpu_crtc->line_time;
			}

			display_cfg->controller_id = amdgpu_crtc->crtc_id;
			display_cfg->pixel_clock = conn->pixelclock_for_modeset;
		}
	}

	cfg->display_clk = adev->clock.default_dispclk;
	cfg->num_display = num_crtcs;
}
