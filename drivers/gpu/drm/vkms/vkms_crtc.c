// SPDX-License-Identifier: GPL-2.0+

#include "vkms_drv.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_probe_helper.h>

static enum hrtimer_restart vkms_vblank_simulate(struct hrtimer *timer)
{
	struct vkms_output *output = container_of(timer, struct vkms_output,
						  vblank_hrtimer);
	struct drm_crtc *crtc = &output->crtc;
	struct vkms_crtc_state *state = to_vkms_crtc_state(crtc->state);
	u64 ret_overrun;
	bool ret;

	spin_lock(&output->lock);

	ret_overrun = hrtimer_forward_now(&output->vblank_hrtimer,
					  output->period_ns);
	WARN_ON(ret_overrun != 1);

	ret = drm_crtc_handle_vblank(crtc);
	if (!ret)
		DRM_ERROR("vkms failure on handling vblank");

	if (state && output->crc_enabled) {
		u64 frame = drm_crtc_accurate_vblank_count(crtc);

		/* update frame_start only if a queued vkms_crc_work_handle()
		 * has read the data
		 */
		spin_lock(&output->state_lock);
		if (!state->frame_start)
			state->frame_start = frame;
		spin_unlock(&output->state_lock);

		ret = queue_work(output->crc_workq, &state->crc_work);
		if (!ret)
			DRM_WARN("failed to queue vkms_crc_work_handle");
	}

	spin_unlock(&output->lock);

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

	*vblank_time = output->vblank_hrtimer.node.expires;

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

	INIT_WORK(&vkms_state->crc_work, vkms_crc_work_handle);

	return &vkms_state->base;
}

static void vkms_atomic_crtc_destroy_state(struct drm_crtc *crtc,
					   struct drm_crtc_state *state)
{
	struct vkms_crtc_state *vkms_state = to_vkms_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(state);

	if (vkms_state) {
		flush_work(&vkms_state->crc_work);
		kfree(vkms_state);
	}
}

static void vkms_atomic_crtc_reset(struct drm_crtc *crtc)
{
	struct vkms_crtc_state *vkms_state =
		kzalloc(sizeof(*vkms_state), GFP_KERNEL);

	if (crtc->state)
		vkms_atomic_crtc_destroy_state(crtc, crtc->state);

	__drm_atomic_helper_crtc_reset(crtc, &vkms_state->base);
	if (vkms_state)
		INIT_WORK(&vkms_state->crc_work, vkms_crc_work_handle);
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
	.set_crc_source		= vkms_set_crc_source,
	.verify_crc_source	= vkms_verify_crc_source,
};

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
	 * from scheduling vkms_crc_work_handle until the crc_data is updated
	 */
	spin_lock_irq(&vkms_output->lock);
}

static void vkms_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct vkms_output *vkms_output = drm_crtc_to_vkms_output(crtc);
	unsigned long flags;

	if (crtc->state->event) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);

		if (drm_crtc_vblank_get(crtc) != 0)
			drm_crtc_send_vblank_event(crtc, crtc->state->event);
		else
			drm_crtc_arm_vblank_event(crtc, crtc->state->event);

		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);

		crtc->state->event = NULL;
	}

	spin_unlock_irq(&vkms_output->lock);
}

static const struct drm_crtc_helper_funcs vkms_crtc_helper_funcs = {
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
	spin_lock_init(&vkms_out->state_lock);

	vkms_out->crc_workq = alloc_ordered_workqueue("vkms_crc_workq", 0);
	if (!vkms_out->crc_workq)
		return -ENOMEM;

	return ret;
}
