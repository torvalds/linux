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

#include <drm/drm_vblank.h>

#include "amdgpu.h"
#include "amdgpu_pm.h"
#include "amdgpu_i2c.h"
#include "atom.h"
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"
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

#define DCE_VIRTUAL_VBLANK_PERIOD 16666666


static void dce_virtual_set_display_funcs(struct amdgpu_device *adev);
static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev);
static int dce_virtual_connector_encoder_init(struct amdgpu_device *adev,
					      int index);
static void dce_virtual_set_crtc_vblank_interrupt_state(struct amdgpu_device *adev,
							int crtc,
							enum amdgpu_interrupt_state state);

static u32 dce_virtual_vblank_get_counter(struct amdgpu_device *adev, int crtc)
{
	return 0;
}

static void dce_virtual_page_flip(struct amdgpu_device *adev,
			      int crtc_id, u64 crtc_base, bool async)
{
	return;
}

static int dce_virtual_crtc_get_scanoutpos(struct amdgpu_device *adev, int crtc,
					u32 *vbl, u32 *position)
{
	*vbl = 0;
	*position = 0;

	return -EINVAL;
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

static int dce_virtual_crtc_gamma_set(struct drm_crtc *crtc, u16 *red,
				      u16 *green, u16 *blue, uint32_t size,
				      struct drm_modeset_acquire_ctx *ctx)
{
	return 0;
}

static void dce_virtual_crtc_destroy(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(amdgpu_crtc);
}

static const struct drm_crtc_funcs dce_virtual_crtc_funcs = {
	.cursor_set2 = NULL,
	.cursor_move = NULL,
	.gamma_set = dce_virtual_crtc_gamma_set,
	.set_config = amdgpu_display_crtc_set_config,
	.destroy = dce_virtual_crtc_destroy,
	.page_flip_target = amdgpu_display_crtc_page_flip_target,
	.get_vblank_counter = amdgpu_get_vblank_counter_kms,
	.enable_vblank = amdgpu_enable_vblank_kms,
	.disable_vblank = amdgpu_disable_vblank_kms,
	.get_vblank_timestamp = drm_crtc_vblank_helper_get_vblank_timestamp,
};

static void dce_virtual_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	unsigned type;

	if (amdgpu_sriov_vf(adev))
		return;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		amdgpu_crtc->enabled = true;
		/* Make sure VBLANK interrupts are still enabled */
		type = amdgpu_display_crtc_idx_to_irq_type(adev,
						amdgpu_crtc->crtc_id);
		amdgpu_irq_update(adev, &adev->crtc_irq, type);
		drm_crtc_vblank_on(crtc);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		drm_crtc_vblank_off(crtc);
		amdgpu_crtc->enabled = false;
		break;
	}
}


static void dce_virtual_crtc_prepare(struct drm_crtc *crtc)
{
	dce_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void dce_virtual_crtc_commit(struct drm_crtc *crtc)
{
	dce_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
}

static void dce_virtual_crtc_disable(struct drm_crtc *crtc)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	dce_virtual_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);

	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
}

static int dce_virtual_crtc_mode_set(struct drm_crtc *crtc,
				  struct drm_display_mode *mode,
				  struct drm_display_mode *adjusted_mode,
				  int x, int y, struct drm_framebuffer *old_fb)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);

	/* update the hw version fpr dpm */
	amdgpu_crtc->hw_mode = *adjusted_mode;

	return 0;
}

static bool dce_virtual_crtc_mode_fixup(struct drm_crtc *crtc,
				     const struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	return true;
}


static int dce_virtual_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return 0;
}

static int dce_virtual_crtc_set_base_atomic(struct drm_crtc *crtc,
					 struct drm_framebuffer *fb,
					 int x, int y, enum mode_set_atomic state)
{
	return 0;
}

static const struct drm_crtc_helper_funcs dce_virtual_crtc_helper_funcs = {
	.dpms = dce_virtual_crtc_dpms,
	.mode_fixup = dce_virtual_crtc_mode_fixup,
	.mode_set = dce_virtual_crtc_mode_set,
	.mode_set_base = dce_virtual_crtc_set_base,
	.mode_set_base_atomic = dce_virtual_crtc_set_base_atomic,
	.prepare = dce_virtual_crtc_prepare,
	.commit = dce_virtual_crtc_commit,
	.disable = dce_virtual_crtc_disable,
	.get_scanout_position = amdgpu_crtc_get_scanout_position,
};

static int dce_virtual_crtc_init(struct amdgpu_device *adev, int index)
{
	struct amdgpu_crtc *amdgpu_crtc;

	amdgpu_crtc = kzalloc(sizeof(struct amdgpu_crtc) +
			      (AMDGPUFB_CONN_LIMIT * sizeof(struct drm_connector *)), GFP_KERNEL);
	if (amdgpu_crtc == NULL)
		return -ENOMEM;

	drm_crtc_init(adev->ddev, &amdgpu_crtc->base, &dce_virtual_crtc_funcs);

	drm_mode_crtc_set_gamma_size(&amdgpu_crtc->base, 256);
	amdgpu_crtc->crtc_id = index;
	adev->mode_info.crtcs[index] = amdgpu_crtc;

	amdgpu_crtc->pll_id = ATOM_PPLL_INVALID;
	amdgpu_crtc->encoder = NULL;
	amdgpu_crtc->connector = NULL;
	amdgpu_crtc->vsync_timer_enabled = AMDGPU_IRQ_STATE_DISABLE;
	drm_crtc_helper_add(&amdgpu_crtc->base, &dce_virtual_crtc_helper_funcs);

	return 0;
}

static int dce_virtual_early_init(void *handle)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	dce_virtual_set_display_funcs(adev);
	dce_virtual_set_irq_funcs(adev);

	adev->mode_info.num_hpd = 1;
	adev->mode_info.num_dig = 1;
	return 0;
}

static struct drm_encoder *
dce_virtual_encoder(struct drm_connector *connector)
{
	struct drm_encoder *encoder;

	drm_connector_for_each_possible_encoder(connector, encoder) {
		if (encoder->encoder_type == DRM_MODE_ENCODER_VIRTUAL)
			return encoder;
	}

	/* pick the first one */
	drm_connector_for_each_possible_encoder(connector, encoder)
		return encoder;

	return NULL;
}

static int dce_virtual_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	unsigned i;
	static const struct mode_size {
		int w;
		int h;
	} common_modes[21] = {
		{ 640,  480},
		{ 720,  480},
		{ 800,  600},
		{ 848,  480},
		{1024,  768},
		{1152,  768},
		{1280,  720},
		{1280,  800},
		{1280,  854},
		{1280,  960},
		{1280, 1024},
		{1440,  900},
		{1400, 1050},
		{1680, 1050},
		{1600, 1200},
		{1920, 1080},
		{1920, 1200},
		{4096, 3112},
		{3656, 2664},
		{3840, 2160},
		{4096, 2160},
	};

	for (i = 0; i < 21; i++) {
		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h, 60, false, false, false);
		drm_mode_probed_add(connector, mode);
	}

	return 0;
}

static enum drm_mode_status dce_virtual_mode_valid(struct drm_connector *connector,
				  struct drm_display_mode *mode)
{
	return MODE_OK;
}

static int
dce_virtual_dpms(struct drm_connector *connector, int mode)
{
	return 0;
}

static int
dce_virtual_set_property(struct drm_connector *connector,
			 struct drm_property *property,
			 uint64_t val)
{
	return 0;
}

static void dce_virtual_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
	kfree(connector);
}

static void dce_virtual_force(struct drm_connector *connector)
{
	return;
}

static const struct drm_connector_helper_funcs dce_virtual_connector_helper_funcs = {
	.get_modes = dce_virtual_get_modes,
	.mode_valid = dce_virtual_mode_valid,
	.best_encoder = dce_virtual_encoder,
};

static const struct drm_connector_funcs dce_virtual_connector_funcs = {
	.dpms = dce_virtual_dpms,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.set_property = dce_virtual_set_property,
	.destroy = dce_virtual_destroy,
	.force = dce_virtual_force,
};

static int dce_virtual_sw_init(void *handle)
{
	int r, i;
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;

	r = amdgpu_irq_add_id(adev, AMDGPU_IRQ_CLIENTID_LEGACY, VISLANDS30_IV_SRCID_SMU_DISP_TIMER2_TRIGGER, &adev->crtc_irq);
	if (r)
		return r;

	adev->ddev->max_vblank_count = 0;

	adev->ddev->mode_config.funcs = &amdgpu_mode_funcs;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	adev->ddev->mode_config.preferred_depth = 24;
	adev->ddev->mode_config.prefer_shadow = 1;

	adev->ddev->mode_config.fb_base = adev->gmc.aper_base;

	r = amdgpu_display_modeset_create_props(adev);
	if (r)
		return r;

	adev->ddev->mode_config.max_width = 16384;
	adev->ddev->mode_config.max_height = 16384;

	/* allocate crtcs, encoders, connectors */
	for (i = 0; i < adev->mode_info.num_crtc; i++) {
		r = dce_virtual_crtc_init(adev, i);
		if (r)
			return r;
		r = dce_virtual_connector_encoder_init(adev, i);
		if (r)
			return r;
	}

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
	/* clear crtcs pointer to avoid dce irq finish routine access freed data */
	memset(adev->mode_info.crtcs, 0, sizeof(adev->mode_info.crtcs[0]) * AMDGPU_MAX_CRTCS);
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
	struct amdgpu_device *adev = (struct amdgpu_device *)handle;
	int i = 0;

	for (i = 0; i<adev->mode_info.num_crtc; i++)
		if (adev->mode_info.crtcs[i])
			dce_virtual_set_crtc_vblank_interrupt_state(adev, i, AMDGPU_IRQ_STATE_DISABLE);

	return 0;
}

static int dce_virtual_suspend(void *handle)
{
	return dce_virtual_hw_fini(handle);
}

static int dce_virtual_resume(void *handle)
{
	return dce_virtual_hw_init(handle);
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
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_funcs dce_virtual_encoder_funcs = {
	.destroy = dce_virtual_encoder_destroy,
};

static int dce_virtual_connector_encoder_init(struct amdgpu_device *adev,
					      int index)
{
	struct drm_encoder *encoder;
	struct drm_connector *connector;

	/* add a new encoder */
	encoder = kzalloc(sizeof(struct drm_encoder), GFP_KERNEL);
	if (!encoder)
		return -ENOMEM;
	encoder->possible_crtcs = 1 << index;
	drm_encoder_init(adev->ddev, encoder, &dce_virtual_encoder_funcs,
			 DRM_MODE_ENCODER_VIRTUAL, NULL);
	drm_encoder_helper_add(encoder, &dce_virtual_encoder_helper_funcs);

	connector = kzalloc(sizeof(struct drm_connector), GFP_KERNEL);
	if (!connector) {
		kfree(encoder);
		return -ENOMEM;
	}

	/* add a new connector */
	drm_connector_init(adev->ddev, connector, &dce_virtual_connector_funcs,
			   DRM_MODE_CONNECTOR_VIRTUAL);
	drm_connector_helper_add(connector, &dce_virtual_connector_helper_funcs);
	connector->display_info.subpixel_order = SubPixelHorizontalRGB;
	connector->interlace_allowed = false;
	connector->doublescan_allowed = false;

	/* link them */
	drm_connector_attach_encoder(connector, encoder);

	return 0;
}

static const struct amdgpu_display_funcs dce_virtual_display_funcs = {
	.bandwidth_update = &dce_virtual_bandwidth_update,
	.vblank_get_counter = &dce_virtual_vblank_get_counter,
	.backlight_set_level = NULL,
	.backlight_get_level = NULL,
	.hpd_sense = &dce_virtual_hpd_sense,
	.hpd_set_polarity = &dce_virtual_hpd_set_polarity,
	.hpd_get_gpio_reg = &dce_virtual_hpd_get_gpio_reg,
	.page_flip = &dce_virtual_page_flip,
	.page_flip_get_scanoutpos = &dce_virtual_crtc_get_scanoutpos,
	.add_encoder = NULL,
	.add_connector = NULL,
};

static void dce_virtual_set_display_funcs(struct amdgpu_device *adev)
{
	adev->mode_info.funcs = &dce_virtual_display_funcs;
}

static int dce_virtual_pageflip(struct amdgpu_device *adev,
				unsigned crtc_id)
{
	unsigned long flags;
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_flip_work *works;

	amdgpu_crtc = adev->mode_info.crtcs[crtc_id];

	if (crtc_id >= adev->mode_info.num_crtc) {
		DRM_ERROR("invalid pageflip crtc %d\n", crtc_id);
		return -EINVAL;
	}

	/* IRQ could occur when in initial stage */
	if (amdgpu_crtc == NULL)
		return 0;

	spin_lock_irqsave(&adev->ddev->event_lock, flags);
	works = amdgpu_crtc->pflip_works;
	if (amdgpu_crtc->pflip_status != AMDGPU_FLIP_SUBMITTED) {
		DRM_DEBUG_DRIVER("amdgpu_crtc->pflip_status = %d != "
			"AMDGPU_FLIP_SUBMITTED(%d)\n",
			amdgpu_crtc->pflip_status,
			AMDGPU_FLIP_SUBMITTED);
		spin_unlock_irqrestore(&adev->ddev->event_lock, flags);
		return 0;
	}

	/* page flip completed. clean up */
	amdgpu_crtc->pflip_status = AMDGPU_FLIP_NONE;
	amdgpu_crtc->pflip_works = NULL;

	/* wakeup usersapce */
	if (works->event)
		drm_crtc_send_vblank_event(&amdgpu_crtc->base, works->event);

	spin_unlock_irqrestore(&adev->ddev->event_lock, flags);

	drm_crtc_vblank_put(&amdgpu_crtc->base);
	amdgpu_bo_unref(&works->old_abo);
	kfree(works->shared);
	kfree(works);

	return 0;
}

static enum hrtimer_restart dce_virtual_vblank_timer_handle(struct hrtimer *vblank_timer)
{
	struct amdgpu_crtc *amdgpu_crtc = container_of(vblank_timer,
						       struct amdgpu_crtc, vblank_timer);
	struct drm_device *ddev = amdgpu_crtc->base.dev;
	struct amdgpu_device *adev = ddev->dev_private;

	drm_handle_vblank(ddev, amdgpu_crtc->crtc_id);
	dce_virtual_pageflip(adev, amdgpu_crtc->crtc_id);
	hrtimer_start(vblank_timer, DCE_VIRTUAL_VBLANK_PERIOD,
		      HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

static void dce_virtual_set_crtc_vblank_interrupt_state(struct amdgpu_device *adev,
							int crtc,
							enum amdgpu_interrupt_state state)
{
	if (crtc >= adev->mode_info.num_crtc || !adev->mode_info.crtcs[crtc]) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	if (state && !adev->mode_info.crtcs[crtc]->vsync_timer_enabled) {
		DRM_DEBUG("Enable software vsync timer\n");
		hrtimer_init(&adev->mode_info.crtcs[crtc]->vblank_timer,
			     CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hrtimer_set_expires(&adev->mode_info.crtcs[crtc]->vblank_timer,
				    DCE_VIRTUAL_VBLANK_PERIOD);
		adev->mode_info.crtcs[crtc]->vblank_timer.function =
			dce_virtual_vblank_timer_handle;
		hrtimer_start(&adev->mode_info.crtcs[crtc]->vblank_timer,
			      DCE_VIRTUAL_VBLANK_PERIOD, HRTIMER_MODE_REL);
	} else if (!state && adev->mode_info.crtcs[crtc]->vsync_timer_enabled) {
		DRM_DEBUG("Disable software vsync timer\n");
		hrtimer_cancel(&adev->mode_info.crtcs[crtc]->vblank_timer);
	}

	adev->mode_info.crtcs[crtc]->vsync_timer_enabled = state;
	DRM_DEBUG("[FM]set crtc %d vblank interrupt state %d\n", crtc, state);
}


static int dce_virtual_set_crtc_irq_state(struct amdgpu_device *adev,
					  struct amdgpu_irq_src *source,
					  unsigned type,
					  enum amdgpu_interrupt_state state)
{
	if (type > AMDGPU_CRTC_IRQ_VBLANK6)
		return -EINVAL;

	dce_virtual_set_crtc_vblank_interrupt_state(adev, type, state);

	return 0;
}

static const struct amdgpu_irq_src_funcs dce_virtual_crtc_irq_funcs = {
	.set = dce_virtual_set_crtc_irq_state,
	.process = NULL,
};

static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->crtc_irq.num_types = AMDGPU_CRTC_IRQ_VBLANK6 + 1;
	adev->crtc_irq.funcs = &dce_virtual_crtc_irq_funcs;
}

const struct amdgpu_ip_block_version dce_virtual_ip_block =
{
	.type = AMD_IP_BLOCK_TYPE_DCE,
	.major = 1,
	.minor = 0,
	.rev = 0,
	.funcs = &dce_virtual_ip_funcs,
};
