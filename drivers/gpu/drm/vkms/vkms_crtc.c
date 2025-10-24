// SPDX-License-Identifier: GPL-2.0+

#include <linux/dma-fence.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_helper.h>

#include "vkms_drv.h"

static bool vkms_crtc_handle_vblank_timeout(struct drm_crtc *crtc)
{
	struct vkms_output *output = drm_crtc_to_vkms_output(crtc);
	struct vkms_crtc_state *state;
	bool ret, fence_cookie;

	fence_cookie = dma_fence_begin_signalling();

	spin_lock(&output->lock);
	ret = drm_crtc_handle_vblank(crtc);
	if (!ret)
		DRM_ERROR("vkms failure on handling vblank");

	state = output->composer_state;
	spin_unlock(&output->lock);

	if (state && output->composer_enabled) {
		u64 frame = drm_crtc_accurate_vblank_count(crtc);

		/* update frame_start only if a queued vkms_composer_worker()
		 * has read the data
		 */
		spin_lock(&output->composer_lock);
		if (!state->crc_pending)
			state->frame_start = frame;
		else
			DRM_DEBUG_DRIVER("crc worker falling behind, frame_start: %llu, frame_end: %llu\n",
					 state->frame_start, frame);
		state->frame_end = frame;
		state->crc_pending = true;
		spin_unlock(&output->composer_lock);

		ret = queue_work(output->composer_workq, &state->composer_work);
		if (!ret)
			DRM_DEBUG_DRIVER("Composer worker already queued\n");
	}

	dma_fence_end_signalling(fence_cookie);

	return true;
}

static struct drm_crtc_state *
vkms_atomic_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct vkms_crtc_state *vkms_state;

	if (WARN_ON(!crtc->state))
		return NULL;

	vkms_state = kzalloc(sizeof(*vkms_state), GFP_KERNEL);
	if (!vkms_state)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &vkms_state->base);

	INIT_WORK(&vkms_state->composer_work, vkms_composer_worker);

	return &vkms_state->base;
}

static void vkms_atomic_crtc_destroy_state(struct drm_crtc *crtc,
					   struct drm_crtc_state *state)
{
	struct vkms_crtc_state *vkms_state = to_vkms_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(state);

	WARN_ON(work_pending(&vkms_state->composer_work));
	kfree(vkms_state->active_planes);
	kfree(vkms_state);
}

static void vkms_atomic_crtc_reset(struct drm_crtc *crtc)
{
	struct vkms_crtc_state *vkms_state =
		kzalloc(sizeof(*vkms_state), GFP_KERNEL);

	if (crtc->state)
		vkms_atomic_crtc_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &vkms_state->base);
	if (vkms_state)
		INIT_WORK(&vkms_state->composer_work, vkms_composer_worker);
}

static const struct drm_crtc_funcs vkms_crtc_funcs = {
	.set_config             = drm_atomic_helper_set_config,
	.page_flip              = drm_atomic_helper_page_flip,
	.reset                  = vkms_atomic_crtc_reset,
	.atomic_duplicate_state = vkms_atomic_crtc_duplicate_state,
	.atomic_destroy_state   = vkms_atomic_crtc_destroy_state,
	DRM_CRTC_VBLANK_TIMER_FUNCS,
	.get_crc_sources	= vkms_get_crc_sources,
	.set_crc_source		= vkms_set_crc_source,
	.verify_crc_source	= vkms_verify_crc_source,
};

static int vkms_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state,
									  crtc);
	struct vkms_crtc_state *vkms_state = to_vkms_crtc_state(crtc_state);
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i = 0, ret;

	if (vkms_state->active_planes)
		return 0;

	ret = drm_atomic_add_affected_planes(crtc_state->state, crtc);
	if (ret < 0)
		return ret;

	drm_for_each_plane_mask(plane, crtc->dev, crtc_state->plane_mask) {
		plane_state = drm_atomic_get_new_plane_state(crtc_state->state, plane);
		WARN_ON(!plane_state);

		if (!plane_state->visible)
			continue;

		i++;
	}

	vkms_state->active_planes = kcalloc(i, sizeof(*vkms_state->active_planes), GFP_KERNEL);
	if (!vkms_state->active_planes)
		return -ENOMEM;
	vkms_state->num_active_planes = i;

	i = 0;
	drm_for_each_plane_mask(plane, crtc->dev, crtc_state->plane_mask) {
		plane_state = drm_atomic_get_new_plane_state(crtc_state->state, plane);

		if (!plane_state->visible)
			continue;

		vkms_state->active_planes[i++] =
			to_vkms_plane_state(plane_state);
	}

	return 0;
}

static void vkms_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
	__acquires(&vkms_output->lock)
{
	struct vkms_output *vkms_output = drm_crtc_to_vkms_output(crtc);

	/* This lock is held across the atomic commit to block vblank timer
	 * from scheduling vkms_composer_worker until the composer is updated
	 */
	spin_lock_irq(&vkms_output->lock);
}

static void vkms_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_atomic_state *state)
	__releases(&vkms_output->lock)
{
	struct vkms_output *vkms_output = drm_crtc_to_vkms_output(crtc);

	if (crtc->state->event) {
		spin_lock(&crtc->dev->event_lock);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock(&crtc->dev->event_lock);

		crtc->state->event = NULL;
	}

	vkms_output->composer_state = to_vkms_crtc_state(crtc->state);

	spin_unlock_irq(&vkms_output->lock);
}

static const struct drm_crtc_helper_funcs vkms_crtc_helper_funcs = {
	.atomic_check	= vkms_crtc_atomic_check,
	.atomic_begin	= vkms_crtc_atomic_begin,
	.atomic_flush	= vkms_crtc_atomic_flush,
	.atomic_enable	= drm_crtc_vblank_atomic_enable,
	.atomic_disable	= drm_crtc_vblank_atomic_disable,
	.handle_vblank_timeout = vkms_crtc_handle_vblank_timeout,
};

struct vkms_output *vkms_crtc_init(struct drm_device *dev, struct drm_plane *primary,
				   struct drm_plane *cursor)
{
	struct vkms_output *vkms_out;
	struct drm_crtc *crtc;
	int ret;

	vkms_out = drmm_crtc_alloc_with_planes(dev, struct vkms_output, crtc,
					       primary, cursor,
					       &vkms_crtc_funcs, NULL);
	if (IS_ERR(vkms_out)) {
		DRM_DEV_ERROR(dev->dev, "Failed to init CRTC\n");
		return vkms_out;
	}

	crtc = &vkms_out->crtc;

	drm_crtc_helper_add(crtc, &vkms_crtc_helper_funcs);

	ret = drm_mode_crtc_set_gamma_size(crtc, VKMS_LUT_SIZE);
	if (ret) {
		DRM_ERROR("Failed to set gamma size\n");
		return ERR_PTR(ret);
	}

	drm_crtc_enable_color_mgmt(crtc, 0, false, VKMS_LUT_SIZE);

	spin_lock_init(&vkms_out->lock);
	spin_lock_init(&vkms_out->composer_lock);

	vkms_out->composer_workq = drmm_alloc_ordered_workqueue(dev, "vkms_composer", 0);
	if (IS_ERR(vkms_out->composer_workq))
		return ERR_CAST(vkms_out->composer_workq);

	return vkms_out;
}
