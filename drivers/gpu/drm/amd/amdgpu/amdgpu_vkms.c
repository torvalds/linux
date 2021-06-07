// SPDX-License-Identifier: GPL-2.0+

#include <drm/drm_atomic_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_vblank.h>

#include "amdgpu.h"
#include "amdgpu_vkms.h"
#include "amdgpu_display.h"

/**
 * DOC: amdgpu_vkms
 *
 * The amdgpu vkms interface provides a virtual KMS interface for several use
 * cases: devices without display hardware, platforms where the actual display
 * hardware is not useful (e.g., servers), SR-IOV virtual functions, device
 * emulation/simulation, and device bring up prior to display hardware being
 * usable. We previously emulated a legacy KMS interface, but there was a desire
 * to move to the atomic KMS interface. The vkms driver did everything we
 * needed, but we wanted KMS support natively in the driver without buffer
 * sharing and the ability to support an instance of VKMS per device. We first
 * looked at splitting vkms into a stub driver and a helper module that other
 * drivers could use to implement a virtual display, but this strategy ended up
 * being messy due to driver specific callbacks needed for buffer management.
 * Ultimately, it proved easier to import the vkms code as it mostly used core
 * drm helpers anyway.
 */

static const u32 amdgpu_vkms_formats[] = {
	DRM_FORMAT_XRGB8888,
};

static enum hrtimer_restart amdgpu_vkms_vblank_simulate(struct hrtimer *timer)
{
	struct amdgpu_vkms_output *output = container_of(timer,
							 struct amdgpu_vkms_output,
							 vblank_hrtimer);
	struct drm_crtc *crtc = &output->crtc;
	u64 ret_overrun;
	bool ret;

	ret_overrun = hrtimer_forward_now(&output->vblank_hrtimer,
					  output->period_ns);
	WARN_ON(ret_overrun != 1);

	ret = drm_crtc_handle_vblank(crtc);
	if (!ret)
		DRM_ERROR("amdgpu_vkms failure on handling vblank");

	return HRTIMER_RESTART;
}

static int amdgpu_vkms_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct amdgpu_vkms_output *out = drm_crtc_to_amdgpu_vkms_output(crtc);

	drm_calc_timestamping_constants(crtc, &crtc->mode);

	hrtimer_init(&out->vblank_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	out->vblank_hrtimer.function = &amdgpu_vkms_vblank_simulate;
	out->period_ns = ktime_set(0, vblank->framedur_ns);
	hrtimer_start(&out->vblank_hrtimer, out->period_ns, HRTIMER_MODE_REL);

	return 0;
}

static void amdgpu_vkms_disable_vblank(struct drm_crtc *crtc)
{
	struct amdgpu_vkms_output *out = drm_crtc_to_amdgpu_vkms_output(crtc);

	hrtimer_cancel(&out->vblank_hrtimer);
}

static bool amdgpu_vkms_get_vblank_timestamp(struct drm_crtc *crtc,
					     int *max_error,
					     ktime_t *vblank_time,
					     bool in_vblank_irq)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = crtc->index;
	struct amdgpu_vkms_output *output = drm_crtc_to_amdgpu_vkms_output(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

	if (!READ_ONCE(vblank->enabled)) {
		*vblank_time = ktime_get();
		return true;
	}

	*vblank_time = READ_ONCE(output->vblank_hrtimer.node.expires);

	if (WARN_ON(*vblank_time == vblank->time))
		return true;

	/*
	 * To prevent races we roll the hrtimer forward before we do any
	 * interrupt processing - this is how real hw works (the interrupt is
	 * only generated after all the vblank registers are updated) and what
	 * the vblank core expects. Therefore we need to always correct the
	 * timestampe by one frame.
	 */
	*vblank_time -= output->period_ns;

	return true;
}

static const struct drm_crtc_funcs amdgpu_vkms_crtc_funcs = {
	.set_config             = drm_atomic_helper_set_config,
	.destroy                = drm_crtc_cleanup,
	.page_flip              = drm_atomic_helper_page_flip,
	.reset                  = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state   = drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= amdgpu_vkms_enable_vblank,
	.disable_vblank		= amdgpu_vkms_disable_vblank,
	.get_vblank_timestamp	= amdgpu_vkms_get_vblank_timestamp,
};

static void amdgpu_vkms_crtc_atomic_enable(struct drm_crtc *crtc,
					   struct drm_atomic_state *state)
{
	drm_crtc_vblank_on(crtc);
}

static void amdgpu_vkms_crtc_atomic_disable(struct drm_crtc *crtc,
					    struct drm_atomic_state *state)
{
	drm_crtc_vblank_off(crtc);
}

static void amdgpu_vkms_crtc_atomic_flush(struct drm_crtc *crtc,
					  struct drm_atomic_state *state)
{
	if (crtc->state->event) {
		spin_lock(&crtc->dev->event_lock);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}
}

static const struct drm_crtc_helper_funcs amdgpu_vkms_crtc_helper_funcs = {
	.atomic_flush	= amdgpu_vkms_crtc_atomic_flush,
	.atomic_enable	= amdgpu_vkms_crtc_atomic_enable,
	.atomic_disable	= amdgpu_vkms_crtc_atomic_disable,
};

static int amdgpu_vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
			  struct drm_plane *primary, struct drm_plane *cursor)
{
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, primary, cursor,
					&amdgpu_vkms_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to init CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &amdgpu_vkms_crtc_helper_funcs);

	return ret;
}

static const struct drm_connector_funcs amdgpu_vkms_connector_funcs = {
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int amdgpu_vkms_conn_get_modes(struct drm_connector *connector)
{
	struct drm_device *dev = connector->dev;
	struct drm_display_mode *mode = NULL;
	unsigned i;
	static const struct mode_size {
		int w;
		int h;
	} common_modes[] = {
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
		{2560, 1440},
		{4096, 3112},
		{3656, 2664},
		{3840, 2160},
		{4096, 2160},
	};

	for (i = 0; i < ARRAY_SIZE(common_modes); i++) {
		mode = drm_cvt_mode(dev, common_modes[i].w, common_modes[i].h, 60, false, false, false);
		drm_mode_probed_add(connector, mode);
	}

	drm_set_preferred_mode(connector, XRES_DEF, YRES_DEF);

	return ARRAY_SIZE(common_modes);
}

static const struct drm_connector_helper_funcs amdgpu_vkms_conn_helper_funcs = {
	.get_modes    = amdgpu_vkms_conn_get_modes,
};

static const struct drm_plane_funcs amdgpu_vkms_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
};

static void amdgpu_vkms_plane_atomic_update(struct drm_plane *plane,
					    struct drm_atomic_state *old_state)
{
	return;
}

static int amdgpu_vkms_plane_atomic_check(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!new_plane_state->fb || WARN_ON(!new_plane_state->crtc))
		return 0;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	ret = drm_atomic_helper_check_plane_state(new_plane_state, crtc_state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, true);
	if (ret != 0)
		return ret;

	/* for now primary plane must be visible and full screen */
	if (!new_plane_state->visible)
		return -EINVAL;

	return 0;
}

static int amdgpu_vkms_prepare_fb(struct drm_plane *plane,
				  struct drm_plane_state *new_state)
{
	struct amdgpu_framebuffer *afb;
	struct drm_gem_object *obj;
	struct amdgpu_device *adev;
	struct amdgpu_bo *rbo;
	struct list_head list;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	uint32_t domain;
	int r;

	if (!new_state->fb) {
		DRM_DEBUG_KMS("No FB bound\n");
		return 0;
	}
	afb = to_amdgpu_framebuffer(new_state->fb);
	obj = new_state->fb->obj[0];
	rbo = gem_to_amdgpu_bo(obj);
	adev = amdgpu_ttm_adev(rbo->tbo.bdev);
	INIT_LIST_HEAD(&list);

	tv.bo = &rbo->tbo;
	tv.num_shared = 1;
	list_add(&tv.head, &list);

	r = ttm_eu_reserve_buffers(&ticket, &list, false, NULL);
	if (r) {
		dev_err(adev->dev, "fail to reserve bo (%d)\n", r);
		return r;
	}

	if (plane->type != DRM_PLANE_TYPE_CURSOR)
		domain = amdgpu_display_supported_domains(adev, rbo->flags);
	else
		domain = AMDGPU_GEM_DOMAIN_VRAM;

	r = amdgpu_bo_pin(rbo, domain);
	if (unlikely(r != 0)) {
		if (r != -ERESTARTSYS)
			DRM_ERROR("Failed to pin framebuffer with error %d\n", r);
		ttm_eu_backoff_reservation(&ticket, &list);
		return r;
	}

	r = amdgpu_ttm_alloc_gart(&rbo->tbo);
	if (unlikely(r != 0)) {
		amdgpu_bo_unpin(rbo);
		ttm_eu_backoff_reservation(&ticket, &list);
		DRM_ERROR("%p bind failed\n", rbo);
		return r;
	}

	ttm_eu_backoff_reservation(&ticket, &list);

	afb->address = amdgpu_bo_gpu_offset(rbo);

	amdgpu_bo_ref(rbo);

	return 0;
}

static void amdgpu_vkms_cleanup_fb(struct drm_plane *plane,
				   struct drm_plane_state *old_state)
{
	struct amdgpu_bo *rbo;
	int r;

	if (!old_state->fb)
		return;

	rbo = gem_to_amdgpu_bo(old_state->fb->obj[0]);
	r = amdgpu_bo_reserve(rbo, false);
	if (unlikely(r)) {
		DRM_ERROR("failed to reserve rbo before unpin\n");
		return;
	}

	amdgpu_bo_unpin(rbo);
	amdgpu_bo_unreserve(rbo);
	amdgpu_bo_unref(&rbo);
}

static const struct drm_plane_helper_funcs amdgpu_vkms_primary_helper_funcs = {
	.atomic_update		= amdgpu_vkms_plane_atomic_update,
	.atomic_check		= amdgpu_vkms_plane_atomic_check,
	.prepare_fb		= amdgpu_vkms_prepare_fb,
	.cleanup_fb		= amdgpu_vkms_cleanup_fb,
};

static struct drm_plane *amdgpu_vkms_plane_init(struct drm_device *dev,
						enum drm_plane_type type,
						int index)
{
	struct drm_plane *plane;
	int ret;

	plane = kzalloc(sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	ret = drm_universal_plane_init(dev, plane, 1 << index,
				       &amdgpu_vkms_plane_funcs,
				       amdgpu_vkms_formats,
				       ARRAY_SIZE(amdgpu_vkms_formats),
				       NULL, type, NULL);
	if (ret) {
		kfree(plane);
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(plane, &amdgpu_vkms_primary_helper_funcs);

	return plane;
}

int amdgpu_vkms_output_init(struct drm_device *dev,
			    struct amdgpu_vkms_output *output, int index)
{
	struct drm_connector *connector = &output->connector;
	struct drm_encoder *encoder = &output->encoder;
	struct drm_crtc *crtc = &output->crtc;
	struct drm_plane *primary, *cursor = NULL;
	int ret;

	primary = amdgpu_vkms_plane_init(dev, DRM_PLANE_TYPE_PRIMARY, index);
	if (IS_ERR(primary))
		return PTR_ERR(primary);

	ret = amdgpu_vkms_crtc_init(dev, crtc, primary, cursor);
	if (ret)
		goto err_crtc;

	ret = drm_connector_init(dev, connector, &amdgpu_vkms_connector_funcs,
				 DRM_MODE_CONNECTOR_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init connector\n");
		goto err_connector;
	}

	drm_connector_helper_add(connector, &amdgpu_vkms_conn_helper_funcs);

	ret = drm_simple_encoder_init(dev, encoder, DRM_MODE_ENCODER_VIRTUAL);
	if (ret) {
		DRM_ERROR("Failed to init encoder\n");
		goto err_encoder;
	}
	encoder->possible_crtcs = 1 << index;

	ret = drm_connector_attach_encoder(connector, encoder);
	if (ret) {
		DRM_ERROR("Failed to attach connector to encoder\n");
		goto err_attach;
	}

	drm_mode_config_reset(dev);

	return 0;

err_attach:
	drm_encoder_cleanup(encoder);

err_encoder:
	drm_connector_cleanup(connector);

err_connector:
	drm_crtc_cleanup(crtc);

err_crtc:
	drm_plane_cleanup(primary);

	return ret;
}
