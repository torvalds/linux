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
#include <drm/drm_gem_framebuffer_helper.h>
#include "vc4_drv.h"
#include "vc4_regs.h"

struct vc4_ctm_state {
	struct drm_private_state base;
	struct drm_color_ctm *ctm;
	int fifo;
};

static struct vc4_ctm_state *to_vc4_ctm_state(struct drm_private_state *priv)
{
	return container_of(priv, struct vc4_ctm_state, base);
}

static struct vc4_ctm_state *vc4_get_ctm_state(struct drm_atomic_state *state,
					       struct drm_private_obj *manager)
{
	struct drm_device *dev = state->dev;
	struct vc4_dev *vc4 = dev->dev_private;
	struct drm_private_state *priv_state;
	int ret;

	ret = drm_modeset_lock(&vc4->ctm_state_lock, state->acquire_ctx);
	if (ret)
		return ERR_PTR(ret);

	priv_state = drm_atomic_get_private_obj_state(state, manager);
	if (IS_ERR(priv_state))
		return ERR_CAST(priv_state);

	return to_vc4_ctm_state(priv_state);
}

static struct drm_private_state *
vc4_ctm_duplicate_state(struct drm_private_obj *obj)
{
	struct vc4_ctm_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void vc4_ctm_destroy_state(struct drm_private_obj *obj,
				  struct drm_private_state *state)
{
	struct vc4_ctm_state *ctm_state = to_vc4_ctm_state(state);

	kfree(ctm_state);
}

static const struct drm_private_state_funcs vc4_ctm_state_funcs = {
	.atomic_duplicate_state = vc4_ctm_duplicate_state,
	.atomic_destroy_state = vc4_ctm_destroy_state,
};

/* Converts a DRM S31.32 value to the HW S0.9 format. */
static u16 vc4_ctm_s31_32_to_s0_9(u64 in)
{
	u16 r;

	/* Sign bit. */
	r = in & BIT_ULL(63) ? BIT(9) : 0;

	if ((in & GENMASK_ULL(62, 32)) > 0) {
		/* We have zero integer bits so we can only saturate here. */
		r |= GENMASK(8, 0);
	} else {
		/* Otherwise take the 9 most important fractional bits. */
		r |= (in >> 23) & GENMASK(8, 0);
	}

	return r;
}

static void
vc4_ctm_commit(struct vc4_dev *vc4, struct drm_atomic_state *state)
{
	struct vc4_ctm_state *ctm_state = to_vc4_ctm_state(vc4->ctm_manager.state);
	struct drm_color_ctm *ctm = ctm_state->ctm;

	if (ctm_state->fifo) {
		HVS_WRITE(SCALER_OLEDCOEF2,
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[0]),
					SCALER_OLEDCOEF2_R_TO_R) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[3]),
					SCALER_OLEDCOEF2_R_TO_G) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[6]),
					SCALER_OLEDCOEF2_R_TO_B));
		HVS_WRITE(SCALER_OLEDCOEF1,
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[1]),
					SCALER_OLEDCOEF1_G_TO_R) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[4]),
					SCALER_OLEDCOEF1_G_TO_G) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[7]),
					SCALER_OLEDCOEF1_G_TO_B));
		HVS_WRITE(SCALER_OLEDCOEF0,
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[2]),
					SCALER_OLEDCOEF0_B_TO_R) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[5]),
					SCALER_OLEDCOEF0_B_TO_G) |
			  VC4_SET_FIELD(vc4_ctm_s31_32_to_s0_9(ctm->matrix[8]),
					SCALER_OLEDCOEF0_B_TO_B));
	}

	HVS_WRITE(SCALER_OLEDOFFS,
		  VC4_SET_FIELD(ctm_state->fifo, SCALER_OLEDOFFS_DISPFIFO));
}

static void
vc4_atomic_complete_commit(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);

	drm_atomic_helper_wait_for_fences(dev, state, false);

	drm_atomic_helper_wait_for_dependencies(state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	vc4_ctm_commit(vc4, state);

	drm_atomic_helper_commit_planes(dev, state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_fake_vblank(state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_flip_done(dev, state);

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

	if (state->async_update) {
		ret = down_interruptible(&vc4->async_modeset);
		if (ret)
			return ret;

		ret = drm_atomic_helper_prepare_planes(dev, state);
		if (ret) {
			up(&vc4->async_modeset);
			return ret;
		}

		drm_atomic_helper_async_commit(dev, state);

		drm_atomic_helper_cleanup_planes(dev, state);

		up(&vc4->async_modeset);

		return 0;
	}

	/* We know for sure we don't want an async update here. Set
	 * state->legacy_cursor_update to false to prevent
	 * drm_atomic_helper_setup_commit() from auto-completing
	 * commit->flip_done.
	 */
	state->legacy_cursor_update = false;
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

/* Our CTM has some peculiar limitations: we can only enable it for one CRTC
 * at a time and the HW only supports S0.9 scalars. To account for the latter,
 * we don't allow userland to set a CTM that we have no hope of approximating.
 */
static int
vc4_ctm_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_ctm_state *ctm_state = NULL;
	struct drm_crtc *crtc;
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_color_ctm *ctm;
	int i;

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		/* CTM is being disabled. */
		if (!new_crtc_state->ctm && old_crtc_state->ctm) {
			ctm_state = vc4_get_ctm_state(state, &vc4->ctm_manager);
			if (IS_ERR(ctm_state))
				return PTR_ERR(ctm_state);
			ctm_state->fifo = 0;
		}
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		if (new_crtc_state->ctm == old_crtc_state->ctm)
			continue;

		if (!ctm_state) {
			ctm_state = vc4_get_ctm_state(state, &vc4->ctm_manager);
			if (IS_ERR(ctm_state))
				return PTR_ERR(ctm_state);
		}

		/* CTM is being enabled or the matrix changed. */
		if (new_crtc_state->ctm) {
			/* fifo is 1-based since 0 disables CTM. */
			int fifo = to_vc4_crtc(crtc)->channel + 1;

			/* Check userland isn't trying to turn on CTM for more
			 * than one CRTC at a time.
			 */
			if (ctm_state->fifo && ctm_state->fifo != fifo) {
				DRM_DEBUG_DRIVER("Too many CTM configured\n");
				return -EINVAL;
			}

			/* Check we can approximate the specified CTM.
			 * We disallow scalars |c| > 1.0 since the HW has
			 * no integer bits.
			 */
			ctm = new_crtc_state->ctm->data;
			for (i = 0; i < ARRAY_SIZE(ctm->matrix); i++) {
				u64 val = ctm->matrix[i];

				val &= ~BIT_ULL(63);
				if (val > BIT_ULL(32))
					return -EINVAL;
			}

			ctm_state->fifo = fifo;
			ctm_state->ctm = ctm;
		}
	}

	return 0;
}

static int
vc4_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	int ret;

	ret = vc4_ctm_atomic_check(dev, state);
	if (ret < 0)
		return ret;

	return drm_atomic_helper_check(dev, state);
}

static const struct drm_mode_config_funcs vc4_mode_funcs = {
	.atomic_check = vc4_atomic_check,
	.atomic_commit = vc4_atomic_commit,
	.fb_create = vc4_fb_create,
};

int vc4_kms_load(struct drm_device *dev)
{
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_ctm_state *ctm_state;
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
	dev->mode_config.allow_fb_modifiers = true;

	drm_modeset_lock_init(&vc4->ctm_state_lock);

	ctm_state = kzalloc(sizeof(*ctm_state), GFP_KERNEL);
	if (!ctm_state)
		return -ENOMEM;
	drm_atomic_private_obj_init(&vc4->ctm_manager, &ctm_state->base,
				    &vc4_ctm_state_funcs);

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}
