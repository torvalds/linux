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
#include "amdgpu_pll.h"
#include "amdgpu_connectors.h"
#ifdef CONFIG_DRM_AMDGPU_CIK
#include "dce_v8_0.h"
#endif
#include "dce_v10_0.h"
#include "dce_v11_0.h"
#include "dce_virtual.h"

static void dce_virtual_set_display_funcs(struct amdgpu_device *adev);
static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev);
static int dce_virtual_pageflip_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry);

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

static bool dce_virtual_is_display_hung(struct amdgpu_device *adev)
{
	return false;
}

static void dce_virtual_stop_mc_access(struct amdgpu_device *adev,
			      struct amdgpu_mode_mc_save *save)
{
	switch (adev->asic_type) {
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
	case CHIP_POLARIS11:
	case CHIP_POLARIS10:
		dce_v11_0_disable_dce(adev);
		break;
	case CHIP_TOPAZ:
		/* no DCE */
		return;
	default:
		DRM_ERROR("Virtual display unsupported ASIC type: 0x%X\n", adev->asic_type);
	}

	return;
}
static void dce_virtual_resume_mc_access(struct amdgpu_device *adev,
				struct amdgpu_mode_mc_save *save)
{
	return;
}

static void dce_virtual_set_vga_render_state(struct amdgpu_device *adev,
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

static int dce_virtual_crtc_gamma_set(struct drm_crtc *crtc, u16 *red,
				      u16 *green, u16 *blue, uint32_t size)
{
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	int i;

	/* userspace palettes are always correct as is */
	for (i = 0; i < size; i++) {
		amdgpu_crtc->lut_r[i] = red[i] >> 6;
		amdgpu_crtc->lut_g[i] = green[i] >> 6;
		amdgpu_crtc->lut_b[i] = blue[i] >> 6;
	}

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
	.set_config = amdgpu_crtc_set_config,
	.destroy = dce_virtual_crtc_destroy,
	.page_flip_target = amdgpu_crtc_page_flip_target,
};

static void dce_virtual_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct drm_device *dev = crtc->dev;
	struct amdgpu_device *adev = dev->dev_private;
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	unsigned type;

	switch (mode) {
	case DRM_MODE_DPMS_ON:
		amdgpu_crtc->enabled = true;
		/* Make sure VBLANK and PFLIP interrupts are still enabled */
		type = amdgpu_crtc_idx_to_irq_type(adev, amdgpu_crtc->crtc_id);
		amdgpu_irq_update(adev, &adev->crtc_irq, type);
		amdgpu_irq_update(adev, &adev->pageflip_irq, type);
		drm_vblank_on(dev, amdgpu_crtc->crtc_id);
		break;
	case DRM_MODE_DPMS_STANDBY:
	case DRM_MODE_DPMS_SUSPEND:
	case DRM_MODE_DPMS_OFF:
		drm_vblank_off(dev, amdgpu_crtc->crtc_id);
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
	if (crtc->primary->fb) {
		int r;
		struct amdgpu_framebuffer *amdgpu_fb;
		struct amdgpu_bo *abo;

		amdgpu_fb = to_amdgpu_framebuffer(crtc->primary->fb);
		abo = gem_to_amdgpu_bo(amdgpu_fb->obj);
		r = amdgpu_bo_reserve(abo, false);
		if (unlikely(r))
			DRM_ERROR("failed to reserve abo before unpin\n");
		else {
			amdgpu_bo_unpin(abo);
			amdgpu_bo_unreserve(abo);
		}
	}

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
	struct amdgpu_crtc *amdgpu_crtc = to_amdgpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_encoder *encoder;

	/* assign the encoder to the amdgpu crtc to avoid repeated lookups later */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		if (encoder->crtc == crtc) {
			amdgpu_crtc->encoder = encoder;
			amdgpu_crtc->connector = amdgpu_get_connector_for_encoder(encoder);
			break;
		}
	}
	if ((amdgpu_crtc->encoder == NULL) || (amdgpu_crtc->connector == NULL)) {
		amdgpu_crtc->encoder = NULL;
		amdgpu_crtc->connector = NULL;
		return false;
	}

	return true;
}


static int dce_virtual_crtc_set_base(struct drm_crtc *crtc, int x, int y,
				  struct drm_framebuffer *old_fb)
{
	return 0;
}

static void dce_virtual_crtc_load_lut(struct drm_crtc *crtc)
{
	return;
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
	.load_lut = dce_virtual_crtc_load_lut,
	.disable = dce_virtual_crtc_disable,
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

	adev->mode_info.vsync_timer_enabled = AMDGPU_IRQ_STATE_DISABLE;
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

	adev->ddev->max_vblank_count = 0;

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

static enum hrtimer_restart dce_virtual_vblank_timer_handle(struct hrtimer *vblank_timer)
{
	struct amdgpu_mode_info *mode_info = container_of(vblank_timer, struct amdgpu_mode_info ,vblank_timer);
	struct amdgpu_device *adev = container_of(mode_info, struct amdgpu_device ,mode_info);
	unsigned crtc = 0;
	drm_handle_vblank(adev->ddev, crtc);
	dce_virtual_pageflip_irq(adev, NULL, NULL);
	hrtimer_start(vblank_timer, ktime_set(0, DCE_VIRTUAL_VBLANK_PERIOD), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static void dce_virtual_set_crtc_vblank_interrupt_state(struct amdgpu_device *adev,
						     int crtc,
						     enum amdgpu_interrupt_state state)
{
	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}

	if (state && !adev->mode_info.vsync_timer_enabled) {
		DRM_DEBUG("Enable software vsync timer\n");
		hrtimer_init(&adev->mode_info.vblank_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		hrtimer_set_expires(&adev->mode_info.vblank_timer, ktime_set(0, DCE_VIRTUAL_VBLANK_PERIOD));
		adev->mode_info.vblank_timer.function = dce_virtual_vblank_timer_handle;
		hrtimer_start(&adev->mode_info.vblank_timer, ktime_set(0, DCE_VIRTUAL_VBLANK_PERIOD), HRTIMER_MODE_REL);
	} else if (!state && adev->mode_info.vsync_timer_enabled) {
		DRM_DEBUG("Disable software vsync timer\n");
		hrtimer_cancel(&adev->mode_info.vblank_timer);
	}

	adev->mode_info.vsync_timer_enabled = state;
	DRM_DEBUG("[FM]set crtc %d vblank interrupt state %d\n", crtc, state);
}


static int dce_virtual_set_crtc_irq_state(struct amdgpu_device *adev,
                                       struct amdgpu_irq_src *source,
                                       unsigned type,
                                       enum amdgpu_interrupt_state state)
{
	switch (type) {
	case AMDGPU_CRTC_IRQ_VBLANK1:
		dce_virtual_set_crtc_vblank_interrupt_state(adev, 0, state);
		break;
	default:
		break;
	}
	return 0;
}

static void dce_virtual_crtc_vblank_int_ack(struct amdgpu_device *adev,
					  int crtc)
{
	if (crtc >= adev->mode_info.num_crtc) {
		DRM_DEBUG("invalid crtc %d\n", crtc);
		return;
	}
}

static int dce_virtual_crtc_irq(struct amdgpu_device *adev,
			      struct amdgpu_irq_src *source,
			      struct amdgpu_iv_entry *entry)
{
	unsigned crtc = 0;
	unsigned irq_type = AMDGPU_CRTC_IRQ_VBLANK1;

	dce_virtual_crtc_vblank_int_ack(adev, crtc);

	if (amdgpu_irq_enabled(adev, source, irq_type)) {
		drm_handle_vblank(adev->ddev, crtc);
	}
	dce_virtual_pageflip_irq(adev, NULL, NULL);
	DRM_DEBUG("IH: D%d vblank\n", crtc + 1);
	return 0;
}

static int dce_virtual_set_pageflip_irq_state(struct amdgpu_device *adev,
					    struct amdgpu_irq_src *src,
					    unsigned type,
					    enum amdgpu_interrupt_state state)
{
	if (type >= adev->mode_info.num_crtc) {
		DRM_ERROR("invalid pageflip crtc %d\n", type);
		return -EINVAL;
	}
	DRM_DEBUG("[FM]set pageflip irq type %d state %d\n", type, state);

	return 0;
}

static int dce_virtual_pageflip_irq(struct amdgpu_device *adev,
				  struct amdgpu_irq_src *source,
				  struct amdgpu_iv_entry *entry)
{
	unsigned long flags;
	unsigned crtc_id = 0;
	struct amdgpu_crtc *amdgpu_crtc;
	struct amdgpu_flip_work *works;

	crtc_id = 0;
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
	schedule_work(&works->unpin_work);

	return 0;
}

static const struct amdgpu_irq_src_funcs dce_virtual_crtc_irq_funcs = {
	.set = dce_virtual_set_crtc_irq_state,
	.process = dce_virtual_crtc_irq,
};

static const struct amdgpu_irq_src_funcs dce_virtual_pageflip_irq_funcs = {
	.set = dce_virtual_set_pageflip_irq_state,
	.process = dce_virtual_pageflip_irq,
};

static void dce_virtual_set_irq_funcs(struct amdgpu_device *adev)
{
	adev->crtc_irq.num_types = AMDGPU_CRTC_IRQ_LAST;
	adev->crtc_irq.funcs = &dce_virtual_crtc_irq_funcs;

	adev->pageflip_irq.num_types = AMDGPU_PAGEFLIP_IRQ_LAST;
	adev->pageflip_irq.funcs = &dce_virtual_pageflip_irq_funcs;
}

