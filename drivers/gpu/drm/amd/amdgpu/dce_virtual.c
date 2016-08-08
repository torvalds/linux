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

/**
 * dce_virtual_vblank_wait - vblank wait asic callback.
 *
 * @adev: amdgpu_device pointer
 * @crtc: crtc to wait for vblank on
 *
 * Wait for vblank on the requested crtc (evergreen+).
 */
static void dce_virtual_vblank_wait(struct amdgpu_device *adev, int crtc)
{
	return;
}

static u32 dce_virtual_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	if (crtc >= adev->mode_info.num_crtc)
		return 0;
	else
		return adev->ddev->vblank[crtc].count;
}

static void dce_virtual_page_flip(struct amdgpu_device *adev,
			      int crtc_id, u64 crtc_base, bool async)
{
	return;
}

static int dce_virtual_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	if ((crtc < 0) || (crtc >= adev->mode_info.num_crtc))
		return -EINVAL;

	*vbl = 0;
	*position = 0;

	return 0;
}

static bool dce_virtual_hpd_sense(struct amdgpu_device *adev,
			       enum amdgpu_hpd_id hpd)
{
	return true;
}

static void dce_virtual_hpd_set_polarity(struct amdgpu_device *adev,
				      enum amdgpu_hpd_id hpd)
{
	return;
}

static u32 dce_virtual_hpd_get_gpio_reg(struct amdgpu_device *adev)
{
	return 0;
}

static bool dce_virtual_is_display_hung(struct amdgpu_device *adev)
{
	return false;
}

void dce_virtual_stop_mc_access(struct amdgpu_device *adev,
			      struct amdgpu_mode_mc_save *save)
{
	return;
}
void dce_virtual_resume_mc_access(struct amdgpu_device *adev,
				struct amdgpu_mode_mc_save *save)
{
	return;
}

void dce_virtual_set_vga_render_state(struct amdgpu_device *adev,
				    bool render)
{
	return;
}

/**
 * dce_virtual_bandwidth_update - program display watermarks
 *
 * @adev: amdgpu_device pointer
 *
 * Calculate and program the display watermarks and line
 * buffer allocation (CIK).
 */
static void dce_virtual_bandwidth_update(struct amdgpu_device *adev)
{
	return;
}

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

/* these are handled by the primary encoders */
static void dce_virtual_encoder_prepare(struct drm_encoder *encoder)
{
	return;
}

static void dce_virtual_encoder_commit(struct drm_encoder *encoder)
{
	return;
}

static void
dce_virtual_encoder_mode_set(struct drm_encoder *encoder,
		      struct drm_display_mode *mode,
		      struct drm_display_mode *adjusted_mode)
{
	return;
}

static void dce_virtual_encoder_disable(struct drm_encoder *encoder)
{
	return;
}

static void
dce_virtual_encoder_dpms(struct drm_encoder *encoder, int mode)
{
	return;
}

static bool dce_virtual_encoder_mode_fixup(struct drm_encoder *encoder,
				    const struct drm_display_mode *mode,
				    struct drm_display_mode *adjusted_mode)
{

	/* set the active encoder to connector routing */
	amdgpu_encoder_set_active_device(encoder);

	return true;
}

static const struct drm_encoder_helper_funcs dce_virtual_encoder_helper_funcs = {
	.dpms = dce_virtual_encoder_dpms,
	.mode_fixup = dce_virtual_encoder_mode_fixup,
	.prepare = dce_virtual_encoder_prepare,
	.mode_set = dce_virtual_encoder_mode_set,
	.commit = dce_virtual_encoder_commit,
	.disable = dce_virtual_encoder_disable,
};

static void dce_virtual_encoder_destroy(struct drm_encoder *encoder)
{
	struct amdgpu_encoder *amdgpu_encoder = to_amdgpu_encoder(encoder);

	kfree(amdgpu_encoder->enc_priv);
	drm_encoder_cleanup(encoder);
	kfree(amdgpu_encoder);
}

static const struct drm_encoder_funcs dce_virtual_encoder_funcs = {
	.destroy = dce_virtual_encoder_destroy,
};

static void dce_virtual_encoder_add(struct amdgpu_device *adev,
				 uint32_t encoder_enum,
				 uint32_t supported_device,
				 u16 caps)
{
	struct drm_device *dev = adev->ddev;
	struct drm_encoder *encoder;
	struct amdgpu_encoder *amdgpu_encoder;

	/* see if we already added it */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		amdgpu_encoder = to_amdgpu_encoder(encoder);
		if (amdgpu_encoder->encoder_enum == encoder_enum) {
			amdgpu_encoder->devices |= supported_device;
			return;
		}

	}

	/* add a new one */
	amdgpu_encoder = kzalloc(sizeof(struct amdgpu_encoder), GFP_KERNEL);
	if (!amdgpu_encoder)
		return;

	encoder = &amdgpu_encoder->base;
	encoder->possible_crtcs = 0x1;
	amdgpu_encoder->enc_priv = NULL;
	amdgpu_encoder->encoder_enum = encoder_enum;
	amdgpu_encoder->encoder_id = (encoder_enum & OBJECT_ID_MASK) >> OBJECT_ID_SHIFT;
	amdgpu_encoder->devices = supported_device;
	amdgpu_encoder->rmx_type = RMX_OFF;
	amdgpu_encoder->underscan_type = UNDERSCAN_OFF;
	amdgpu_encoder->is_ext_encoder = false;
	amdgpu_encoder->caps = caps;

	drm_encoder_init(dev, encoder, &dce_virtual_encoder_funcs,
					 DRM_MODE_ENCODER_VIRTUAL, NULL);
	drm_encoder_helper_add(encoder, &dce_virtual_encoder_helper_funcs);
	DRM_INFO("[FM]encoder: %d is VIRTUAL\n", amdgpu_encoder->encoder_id);
}

static const struct amdgpu_display_funcs dce_virtual_display_funcs = {
	.set_vga_render_state = &dce_virtual_set_vga_render_state,
	.bandwidth_update = &dce_virtual_bandwidth_update,
	.vblank_get_counter = &dce_virtual_vblank_get_counter,
	.vblank_wait = &dce_virtual_vblank_wait,
	.is_display_hung = &dce_virtual_is_display_hung,
	.backlight_set_level = NULL,
	.backlight_get_level = NULL,
	.hpd_sense = &dce_virtual_hpd_sense,
	.hpd_set_polarity = &dce_virtual_hpd_set_polarity,
	.hpd_get_gpio_reg = &dce_virtual_hpd_get_gpio_reg,
	.page_flip = &dce_virtual_page_flip,
	.page_flip_get_scanoutpos = &dce_virtual_crtc_get_scanoutpos,
	.add_encoder = &dce_virtual_encoder_add,
	.add_connector = &amdgpu_connector_add,
	.stop_mc_access = &dce_virtual_stop_mc_access,
	.resume_mc_access = &dce_virtual_resume_mc_access,
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

