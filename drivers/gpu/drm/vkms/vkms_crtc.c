// SPDX-License-Identifier: GPL-2.0+

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include "vkms_drv.h"

static enum hrtimer_restart vkms_vblank_simulate(struct hrtimer *timer)
{
	struct vkms_output *output = container_of(timer, struct vkms_output,
						  vblank_hrtimer);
	struct drm_crtc *crtc = &output->crtc;
	struct vkms_crtc_state *state;
	u64 ret_overrun;
	bool ret;

	ret_overrun = hrtimer_forward_now(&output->vblank_hrtimer,
					  output->period_ns);
	WARN_ON(ret_overrun != 1);

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

	return HRTIMER_RESTART;
}

static int vkms_enable_vblank(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	unsigned int pipe = drm_crtc_index(crtc);
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);

	drm_calc_timestamping_constants(crtc, &crtc->mode);

	hrtimer_init(&out->vblank_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	out->vblank_hrtimer.function = &vkms_vblank_simulate;
	out->period_ns = ktime_set(0, vblank->framedur_ns);
	hrtimer_start(&out->vblank_hrtimer, out->period_ns, HRTIMER_MODE_REL);

	return 0;
}

static void vkms_disable_vblank(struct drm_crtc *crtc)
{
	struct vkms_output *out = drm_crtc_to_vkms_output(crtc);

	hrtimer_cancel(&out->vblank_hrtimer);
}

bool vkms_get_vblank_timestamp(struct drm_device *dev, unsigned int pipe,
			       int *max_error, ktime_t *vblank_time,
			       bool in_vblank_irq)
{
	struct vkms_device *vkmsdev = drm_device_to_vkms_device(dev);
	struct vkms_output *output = &vkmsdev->output;
	struct drm_vblank_crtc *vblank = &dev->vblank[pipe];

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
	.destroy                = drm_crtc_cleanup,
	.page_flip              = drm_atomic_helper_page_flip,
	.reset                  = vkms_atomic_crtc_reset,
	.atomic_duplicate_state = vkms_atomic_crtc_duplicate_state,
	.atomic_destroy_state   = vkms_atomic_crtc_destroy_state,
	.enable_vblank		= vkms_enable_vblank,
	.disable_vblank		= vkms_disable_vblank,
	.get_crc_sources	= vkms_get_crc_sources,
	.set_crc_source		= vkms_set_crc_source,
	.verify_crc_source	= vkms_verify_crc_source,
};

static int vkms_crtc_atomic_check(struct drm_crtc *crtc,
				  struct drm_crtc_state *state)
{
	struct vkms_crtc_state *vkms_state = to_vkms_crtc_state(state);
	struct drm_plane *plane;
	struct drm_plane_state *plane_state;
	int i = 0, ret;

	if (vkms_state->active_planes)
		return 0;

	ret = drm_atomic_add_affected_planes(state->state, crtc);
	if (ret < 0)
		return ret;

	drm_for_each_plane_mask(plane, crtc->dev, state->plane_mask) {
		plane_state = drm_atomic_get_existing_plane_state(state->state,
								  plane);
		WARN_ON(!plane_state);

		if (!plane_state->visible)
			continue;

		i++;
	}

	vkms_state->active_planes = kcalloc(i, sizeof(plane), GFP_KERNEL);
	if (!vkms_state->active_planes)
		return -ENOMEM;
	vkms_state->num_active_planes = i;

	i = 0;
	drm_for_each_plane_mask(plane, crtc->dev, state->plane_mask) {
		plane_state = drm_atomic_get_existing_plane_state(state->state,
								  plane);

		if (!plane_state->visible)
			continue;

		vkms_state->active_planes[i++] =
			to_vkms_plane_state(plane_state);
	}

	return 0;
}

static void vkms_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	drm_crtc_vblank_on(crtc);
}

static void vkms_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	drm_crtc_vblank_off(crtc);
}

static void vkms_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct vkms_output *vkms_output = drm_crtc_to_vkms_output(crtc);

	/* This lock is held across the atomic commit to block vblank timer
	 * from scheduling vkms_composer_worker until the composer is updated
	 */
	spin_lock_irq(&vkms_output->lock);
}

static void vkms_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
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
	.atomic_enable	= vkms_crtc_atomic_enable,
	.atomic_disable	= vkms_crtc_atomic_disable,
};

int vkms_crtc_init(struct drm_device *dev, struct drm_crtc *crtc,
		   struct drm_plane *primary, struct drm_plane *cursor)
{
	struct vkms_output *vkms_out = drm_crtc_to_vkms_output(crtc);
	int ret;

	ret = drm_crtc_init_with_planes(dev, crtc, primary, cursor,
					&vkms_crtc_funcs, NULL);
	if (ret) {
		DRM_ERROR("Failed to init CRTC\n");
		return ret;
	}

	drm_crtc_helper_add(crtc, &vkms_crtc_helper_funcs);

	spin_lock_init(&vkms_out->lock);
	spin_lock_init(&vkms_out->composer_lock);

	vkms_out->composer_workq = alloc_ordered_workqueue("vkms_composer", 0);
	if (!vkms_out->composer_workq)
		return -ENOMEM;

	return ret;
}
