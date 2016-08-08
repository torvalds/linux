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
#include "drmP.h"
#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_i2c.h"
#include "atom.h"
#include "amdgpu_atombios.h"
#include "atombios_crtc.h"
#include "atombios_encoders.h"
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"

static void dce_virtual_set_display_funcs(struct amdgpu_device *adev);
static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev);

static const struct drm_crtc_funcs dce_virtual_crtc_funcs = {
	.cursor_set2 = NULL,
	.cursor_move = NULL,
	.gamma_set = NULL,
	.set_config = NULL,
	.destroy = NULL,
	.page_flip = NULL,
};

static const struct drm_crtc_helper_funcs dce_virtual_crtc_helper_funcs = {
	.dpms = NULL,
	.mode_fixup = NULL,
	.mode_set = NULL,
	.mode_set_base = NULL,
	.mode_set_base_atomic = NULL,
	.prepare = NULL,
	.commit = NULL,
	.load_lut = NULL,
	.disable = NULL,
};

static int dce_virtual_crtc_init(struct amdgpu_device *adev, int index)
{
	struct amdgpu_crtc *amdgpu_crtc;
	int i;

	amdgpu_crtc = kzalloc(sizeof(struct amdgpu_crtc) +
			      (AMDGPUFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (amdgpu_crtc == NULL)
		return -ENOMEM;

	drm_crtc_init(adev->ddev, &amdgpu_crtc->base, &dce_virtual_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&amdgpu_crtc->base, 256);
	amdgpu_crtc->crtc_id = index;
	adev->mode_info.crtcs[index] = amdgpu_crtc;

	for (i = 0; i < 256; i++) {
		amdgpu_crtc->lut_r[i] = i << 2;
		amdgpu_crtc->lut_g[i] = i << 2;
		amdgpu_crtc->lut_b[i] = i << 2;
	}

	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
	drm_crtc_helper_add(&amdgpu_crtc->base, &dce_virtual_crtc_helper_funcs);

	return 0;
}

static int dce_virtual_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dce_virtual_set_display_funcs(adev);
	dce_virtual_set_irq_funcs(adev);

	adev->mode_info.num_crtc = 1;
	adev->mode_info.num_hpd = 1;
	adev->mode_info.num_dig = 1;
	return 0;
}

static bool dce_virtual_get_connector_info(struct amdgpu_device *adev)
{
	struct amdgpu_i2c_bus_rec ddc_bus;
	struct amdgpu_router router;
	struct amdgpu_hpd hpd;

	/* look up gpio for ddc, hpd */
	ddc_bus.valid = false;
	hpd.hpd = AMDGPU_HPD_NONE;
	/* needed for aux chan transactions */
	ddc_bus.hpd = hpd.hpd;

	memset(&router, 0, sizeof(router));
	router.ddc_valid = false;
	router.cd_valid = false;
	amdgpu_display_add_connector(adev,
				      0,
				      ATOM_DEVICE_CRT1_SUPPORT,
				      DRM_MODE_CONNECTOR_VIRTUAL, &ddc_bus,
				      CONNECTOR_OBJECT_ID_VIRTUAL,
				      &hpd,
				      &router);

	amdgpu_display_add_encoder(adev, ENCODER_VIRTUAL_ENUM_VIRTUAL,
							ATOM_DEVICE_CRT1_SUPPORT,
							0);

	amdgpu_link_encoder_connector(adev->ddev);

	return true;
}

static int dce_virtual_sw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_irq_add_id(adev, 229, &adev->crtc_irq);
	if (r)
		return r;

	adev->ddev->mode_config.funcs = &amdgpu_mode_funcs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;

	adev->ddev->mode_config.fb_base = adev->mc.aper_base;

	r = amdgpu_modeset_create_props(adev);
	if (r)
		return r;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	/* allocate crtcs */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = dce_virtual_crtc_init(adev, i);
		if (r)
			return r;
	}

	dce_virtual_get_connector_info(adev);
	amdgpu_print_display_setup(adev->ddev);

	drm_kms_helper_poll_init(adev->ddev);

	adev->mode_info.mode_config_initialized = true;
	return 0;
}

static int dce_virtual_sw_fini(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	kfree(adev->mode_info.bios_hardcoded_edid);

	drm_kms_helper_poll_fini(adev->ddev);

	drm_mode_config_cleanup(adev->ddev);
	adev->mode_info.mode_config_initialized = false;
	return 0;
}

static int dce_virtual_hw_init(void *handle)
{
	return 0;
}

static int dce_virtual_hw_fini(void *handle)
{
	return 0;
}

static int dce_virtual_suspend(void *handle)
{
	return dce_virtual_hw_fini(handle);
}

static int dce_virtual_resume(void *handle)
{
	int ret;

	ret = dce_virtual_hw_init(handle);

	return ret;
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

const struct amd_ip_funcs dce_virtual_ip_funcs = {
	.name = "dce_virtual",
	.early_init = dce_virtual_early_init,
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

static const struct amdgpu_display_funcs dce_virtual_display_funcs = {
	.set_vga_render_state = NULL,
	.bandwidth_update = NULL,
	.vblank_get_counter = NULL,
	.vblank_wait = NULL,
	.is_display_hung = NULL,
	.backlight_set_level = NULL,
	.backlight_get_level = NULL,
	.hpd_sense = NULL,
	.hpd_set_polarity = NULL,
	.hpd_get_gpio_reg = NULL,
	.page_flip = NULL,
	.page_flip_get_scanoutpos = NULL,
	.add_encoder = NULL,
	.add_connector = &amdgpu_connector_add,
	.stop_mc_access = NULL,
	.resume_mc_access = NULL,
};

static void dce_virtual_set_display_funcs(struct amdgpu_device *adev)
{
	if (adev->mode_info.funcs == NULL)
		adev->mode_info.funcs = &dce_virtual_display_funcs;
}

static const struct amdgpu_irq_src_funcs dce_virtual_crtc_irq_funcs = {
	.set = NULL,
	.process = NULL,
};

static const struct amdgpu_irq_src_funcs dce_virtual_pageflip_irq_funcs = {
	.set = NULL,
	.process = NULL,
};

static const struct amdgpu_irq_src_funcs dce_virtual_hpd_irq_funcs = {
	.set = NULL,
	.process = NULL,
};

static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->crtc_irq.num_types = AMDGPU_CRTC_IRQ_LAST;
	adev->crtc_irq.funcs = &dce_virtual_crtc_irq_funcs;

	adev->pageflip_irq.num_types = AMDGPU_PAGEFLIP_IRQ_LAST;
	adev->pageflip_irq.funcs = &dce_virtual_pageflip_irq_funcs;

	adev->hpd_irq.num_types = AMDGPU_HPD_LAST;
	adev->hpd_irq.funcs = &dce_virtual_hpd_irq_funcs;
}


