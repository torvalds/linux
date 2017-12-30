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

#include <drm/drm_crtc.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include "vc4_drv.h"

static void vc4_output_poll_changed(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_fbdev_cma_hotplug_event(vc4->fbdev);
}

static void
vc4_atomic_complete_commit(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_atomic_helper_wait_for_fences(dev, state, false);

	drm_atomic_helper_wait_for_dependencies(state);

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

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_vblanks(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_helper_commit_cleanup_done(state);

	drm_atomic_state_put(state);

	up(&vc4->async_modeset);
}

static void commit_work(struct work_struct *work)
{
	struct drm_atomic_state *state = container_of(work,
						      struct drm_atomic_state,
						      commit_work);
	vc4_atomic_complete_commit(state);
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

	ret = drm_atomic_helper_setup_commit(state, nonblock);
	if (ret)
		return ret;

	INIT_WORK(&state->commit_work, commit_work);

	ret = down_interruptible(&vc4->async_modeset);
	if (ret)
		return ret;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret) {
		up(&vc4->async_modeset);
		return ret;
	}

	if (!nonblock) {
		ret = drm_atomic_helper_wait_for_fences(dev, state, true);
		if (ret) {
			drm_atomic_helper_cleanup_planes(dev, state);
			up(&vc4->async_modeset);
			return ret;
		}
	}

	/*
	 * This is the point of no return - everything below never fails except
	 * when the hw goes bonghits. Which means we can commit the new state on
	 * the software side now.
	 */

	BUG_ON(drm_atomic_helper_swap_state(state, false) < 0);

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

	drm_atomic_state_get(state);
	if (nonblock)
		queue_work(system_unbound_wq, &state->commit_work);
	else
		vc4_atomic_complete_commit(state);

	return 0;
}

static struct drm_framebuffer *vc4_fb_create(struct drm_device *dev,
					     struct drm_file *file_priv,
					     const struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct drm_mode_fb_cmd2 mode_cmd_local;

	/* If the user didn't specify a modifier, use the
	 * vc4_set_tiling_ioctl() state for the BO.
	 */
	if (!(mode_cmd->flags & DRM_MODE_FB_MODIFIERS)) {
		struct drm_gem_object *gem_obj;
		struct vc4_bo *bo;

		gem_obj = drm_gem_object_lookup(file_priv,
						mode_cmd->handles[0]);
		if (!gem_obj) {
			DRM_DEBUG("Failed to look up GEM BO %d\n",
				  mode_cmd->handles[0]);
			return ERR_PTR(-ENOENT);
		}
		bo = to_vc4_bo(gem_obj);

		mode_cmd_local = *mode_cmd;

		if (bo->t_format) {
			mode_cmd_local.modifier[0] =
				DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED;
		} else {
			mode_cmd_local.modifier[0] = DRM_FORMAT_MOD_NONE;
		}

		drm_gem_object_put_unlocked(gem_obj);

		mode_cmd = &mode_cmd_local;
	}

	return drm_gem_fb_create(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs vc4_mode_funcs = {
	.output_poll_changed = vc4_output_poll_changed,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = vc4_atomic_commit,
	.fb_create = vc4_fb_create,
};

int vc4_kms_load(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	int ret;

	sema_init(&vc4->async_modeset, 1);

	/* Set support for vblank irq fast disable, before drm_vblank_init() */
	dev->vblank_disable_immediate = true;

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

	if (dev->mode_config.num_connector) {
		vc4->fbdev = drm_fbdev_cma_init(dev, 32,
						dev->mode_config.num_connector);
		if (IS_ERR(vc4->fbdev))
			vc4->fbdev = NULL;
	}

	drm_kms_helper_poll_init(dev);

	return 0;
}
