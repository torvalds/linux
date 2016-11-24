/*
 * Copyright (C) 2015 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/**
 * DOC: VC4 KMS
 *
 * This is the general code for implementing KMS mode setting that
 * doesn't clearly associate with any of the other objects (plane,
 * crtc, HDMI encoder).
 */

#include "drm_crtc.h"
#include "drm_atomic.h"
#include "drm_atomic_helper.h"
#include "drm_crtc_helper.h"
#include "drm_plane_helper.h"
#include "drm_fb_cma_helper.h"
#include "vc4_drv.h"

static void vc4_output_poll_changed(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_fbdev_cma_hotplug_event(vc4->fbdev);
}

struct vc4_commit {
	struct drm_device *dev;
	struct drm_atomic_state *state;
	struct vc4_seqno_cb cb;
};

static void
vc4_atomic_complete_commit(struct vc4_commit *c)
{
	struct drm_atomic_state *state = c->state;
	struct drm_device *dev = state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_planes(dev, state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	/* Make sure that drm_atomic_helper_wait_for_vblanks()
	 * actually waits for vblank.  If we're doing a full atomic
	 * modeset (as opposed to a vc4_update_plane() short circuit),
	 * then we need to wait for scanout to be done with our
	 * display lists before we free it and potentially reallocate
	 * and overwrite the dlist memory with a new modeset.
	 */
	state->legacy_cursor_update = false;

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	up(&vc4->async_modeset);

	kfree(c);
}

static void
vc4_atomic_complete_commit_seqno_cb(struct vc4_seqno_cb *cb)
{
	struct vc4_commit *c = container_of(cb, struct vc4_commit, cb);

	vc4_atomic_complete_commit(c);
}

static struct vc4_commit *commit_init(struct drm_atomic_state *state)
{
	struct vc4_commit *c = kzalloc(sizeof(*c), GFP_KERNEL);

	if (!c)
		return NULL;
	c->dev = state->dev;
	c->state = state;

	return c;
}

/**
 * vc4_atomic_commit - commit validated state object
 * @dev: DRM device
 * @state: the driver state object
 * @nonblock: nonblocking commit
 *
 * This function commits a with drm_atomic_helper_check() pre-validated state
 * object. This can still fail when e.g. the framebuffer reservation fails. For
 * now this doesn't implement asynchronous commits.
 *
 * RETURNS
 * Zero for success or -errno.
 */
static int vc4_atomic_commit(struct drm_device *dev,
			     struct drm_atomic_state *state,
			     bool nonblock)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret;
	int i;
	uint64_t wait_seqno = 0;
	struct vc4_commit *c;
	struct drm_plane *plane;
	struct drm_plane_state *new_state;

	c = commit_init(state);
	if (!c)
		return -ENOMEM;

	/* Make sure that any outstanding modesets have finished. */
	if (nonblock) {
		struct drm_crtc *crtc;
		struct drm_crtc_state *crtc_state;
		unsigned long flags;
		bool busy = false;

		/*
		 * If there's an undispatched event to send then we're
		 * obviously still busy.  If there isn't, then we can
		 * unconditionally wait for the semaphore because it
		 * shouldn't be contended (for long).
		 *
		 * This is to prevent a race where queuing a new flip
		 * from userspace immediately on receipt of an event
		 * beats our clean-up and returns EBUSY.
		 */
		spin_lock_irqsave(&dev->event_lock, flags);
		for_each_crtc_in_state(state, crtc, crtc_state, i)
			busy |= vc4_event_pending(crtc);
		spin_unlock_irqrestore(&dev->event_lock, flags);
		if (busy) {
			kfree(c);
			return -EBUSY;
		}
	}
	ret = down_interruptible(&vc4->async_modeset);
	if (ret) {
		kfree(c);
		return ret;
	}

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret) {
		kfree(c);
		up(&vc4->async_modeset);
		return ret;
	}

	for_each_plane_in_state(state, plane, new_state, i) {
		if ((plane->state->fb != new_state->fb) && new_state->fb) {
			struct drm_gem_cma_object *cma_bo =
				drm_fb_cma_get_gem_obj(new_state->fb, 0);
			struct vc4_bo *bo = to_vc4_bo(&cma_bo->base);

			wait_seqno = max(bo->seqno, wait_seqno);
		}
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	drm_atomic_helper_swap_state(state, true);

	/*
	 * Everything below can be run asynchronously without the need to grab
	 * any modeset locks at all under one condition: It must be guaranteed
	 * that the asynchronous work has either been cancelled (if the driver
	 * supports it, which at least requires that the framebuffers get
	 * cleaned up with drm_atomic_helper_cleanup_planes()) or completed
	 * before the new state gets committed on the software side with
	 * drm_atomic_helper_swap_state().
	 *
	 * This scheme allows new atomic state updates to be prepared and
	 * checked in parallel to the asynchronous completion of the previous
	 * update. Which is important since compositors need to figure out the
	 * composition of the next frame right after having submitted the
	 * current layout.
	 */

	if (nonblock) {
		vc4_queue_seqno_cb(dev, &c->cb, wait_seqno,
				   vc4_atomic_complete_commit_seqno_cb);
	} else {
		vc4_wait_for_seqno(dev, wait_seqno, ~0ull, false);
		vc4_atomic_complete_commit(c);
	}

	return 0;
}

static const struct drm_mode_config_funcs vc4_mode_funcs = {
	.output_poll_changed = vc4_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = vc4_atomic_commit,
	.fb_create = drm_fb_cma_create,
};

int vc4_kms_load(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret;

	sema_init(&vc4->async_modeset, 1);

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		return ret;
	}

	dev->mode_config.max_width = 2048;
	dev->mode_config.max_height = 2048;
	dev->mode_config.funcs = &vc4_mode_funcs;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.async_page_flip = true;

	drm_mode_config_reset(dev);

	vc4->fbdev = drm_fbdev_cma_init(dev, 32,
					dev->mode_config.num_crtc,
					dev->mode_config.num_connector);
	if (IS_ERR(vc4->fbdev))
		vc4->fbdev = NULL;

	drm_kms_helper_poll_init(dev);

	return 0;
}
