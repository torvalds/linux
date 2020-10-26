// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Broadcom
 */

/**
 * DOC: VC4 KMS
 *
 * This is the general code for implementing KMS mode setting that
 * doesn't clearly associate with any of the other objects (plane,
 * crtc, HDMI encoder).
 */

#include <linux/clk.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

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

struct vc4_load_tracker_state {
	struct drm_private_state base;
	u64 hvs_load;
	u64 membus_load;
};

static struct vc4_load_tracker_state *
to_vc4_load_tracker_state(struct drm_private_state *priv)
{
	return container_of(priv, struct vc4_load_tracker_state, base);
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

static void vc4_hvs_pv_muxing_commit(struct vc4_dev *vc4,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	unsigned int i;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc_state);
		u32 dispctrl;
		u32 dsp3_mux;

		if (!crtc_state->active)
			continue;

		if (vc4_state->assigned_channel != 2)
			continue;

		/*
		 * SCALER_DISPCTRL_DSP3 = X, where X < 2 means 'connect DSP3 to
		 * FIFO X'.
		 * SCALER_DISPCTRL_DSP3 = 3 means 'disable DSP 3'.
		 *
		 * DSP3 is connected to FIFO2 unless the transposer is
		 * enabled. In this case, FIFO 2 is directly accessed by the
		 * TXP IP, and we need to disable the FIFO2 -> pixelvalve1
		 * route.
		 */
		if (vc4_state->feed_txp)
			dsp3_mux = VC4_SET_FIELD(3, SCALER_DISPCTRL_DSP3_MUX);
		else
			dsp3_mux = VC4_SET_FIELD(2, SCALER_DISPCTRL_DSP3_MUX);

		dispctrl = HVS_READ(SCALER_DISPCTRL) &
			   ~SCALER_DISPCTRL_DSP3_MUX_MASK;
		HVS_WRITE(SCALER_DISPCTRL, dispctrl | dsp3_mux);
	}
}

static void vc5_hvs_pv_muxing_commit(struct vc4_dev *vc4,
				     struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	unsigned char dsp2_mux = 0;
	unsigned char dsp3_mux = 3;
	unsigned char dsp4_mux = 3;
	unsigned char dsp5_mux = 3;
	unsigned int i;
	u32 reg;

	for_each_new_crtc_in_state(state, crtc, crtc_state, i) {
		struct vc4_crtc_state *vc4_state = to_vc4_crtc_state(crtc_state);
		struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);

		if (!crtc_state->active)
			continue;

		switch (vc4_crtc->data->hvs_output) {
		case 2:
			dsp2_mux = (vc4_state->assigned_channel == 2) ? 0 : 1;
			break;

		case 3:
			dsp3_mux = vc4_state->assigned_channel;
			break;

		case 4:
			dsp4_mux = vc4_state->assigned_channel;
			break;

		case 5:
			dsp5_mux = vc4_state->assigned_channel;
			break;

		default:
			break;
		}
	}

	reg = HVS_READ(SCALER_DISPECTRL);
	HVS_WRITE(SCALER_DISPECTRL,
		  (reg & ~SCALER_DISPECTRL_DSP2_MUX_MASK) |
		  VC4_SET_FIELD(dsp2_mux, SCALER_DISPECTRL_DSP2_MUX));

	reg = HVS_READ(SCALER_DISPCTRL);
	HVS_WRITE(SCALER_DISPCTRL,
		  (reg & ~SCALER_DISPCTRL_DSP3_MUX_MASK) |
		  VC4_SET_FIELD(dsp3_mux, SCALER_DISPCTRL_DSP3_MUX));

	reg = HVS_READ(SCALER_DISPEOLN);
	HVS_WRITE(SCALER_DISPEOLN,
		  (reg & ~SCALER_DISPEOLN_DSP4_MUX_MASK) |
		  VC4_SET_FIELD(dsp4_mux, SCALER_DISPEOLN_DSP4_MUX));

	reg = HVS_READ(SCALER_DISPDITHER);
	HVS_WRITE(SCALER_DISPDITHER,
		  (reg & ~SCALER_DISPDITHER_DSP5_MUX_MASK) |
		  VC4_SET_FIELD(dsp5_mux, SCALER_DISPDITHER_DSP5_MUX));
}

static void
vc4_atomic_complete_commit(struct drm_atomic_state *state)
{
	struct drm_device *dev = state->dev;
	struct vc4_dev *vc4 = to_vc4_dev(dev);
	struct vc4_hvs *hvs = vc4->hvs;
	struct drm_crtc_state *new_crtc_state;
	struct drm_crtc *crtc;
	int i;

	for_each_new_crtc_in_state(state, crtc, new_crtc_state, i) {
		struct vc4_crtc_state *vc4_crtc_state;

		if (!new_crtc_state->commit)
			continue;

		vc4_crtc_state = to_vc4_crtc_state(new_crtc_state);
		vc4_hvs_mask_underrun(dev, vc4_crtc_state->assigned_channel);
	}

	if (vc4->hvs->hvs5)
		clk_set_min_rate(hvs->core_clk, 500000000);

	drm_atomic_helper_wait_for_fences(dev, state, false);

	drm_atomic_helper_wait_for_dependencies(state);

	drm_atomic_helper_commit_modeset_disables(dev, state);

	vc4_ctm_commit(vc4, state);

	if (vc4->hvs->hvs5)
		vc5_hvs_pv_muxing_commit(vc4, state);
	else
		vc4_hvs_pv_muxing_commit(vc4, state);

	drm_atomic_helper_commit_planes(dev, state, 0);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	drm_atomic_helper_fake_vblank(state);

	drm_atomic_helper_commit_hw_done(state);

	drm_atomic_helper_wait_for_flip_done(dev, state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_helper_commit_cleanup_done(state);

	if (vc4->hvs->hvs5)
		clk_set_min_rate(hvs->core_clk, 0);

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

		drm_gem_object_put(gem_obj);

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
			struct vc4_crtc_state *vc4_crtc_state =
				to_vc4_crtc_state(new_crtc_state);

			/* fifo is 1-based since 0 disables CTM. */
			int fifo = vc4_crtc_state->assigned_channel + 1;

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

static int vc4_load_tracker_atomic_check(struct drm_atomic_state *state)
{
	struct drm_plane_state *old_plane_state, *new_plane_state;
	struct vc4_dev *vc4 = to_vc4_dev(state->dev);
	struct vc4_load_tracker_state *load_state;
	struct drm_private_state *priv_state;
	struct drm_plane *plane;
	int i;

	if (!vc4->load_tracker_available)
		return 0;

	priv_state = drm_atomic_get_private_obj_state(state,
						      &vc4->load_tracker);
	if (IS_ERR(priv_state))
		return PTR_ERR(priv_state);

	load_state = to_vc4_load_tracker_state(priv_state);
	for_each_oldnew_plane_in_state(state, plane, old_plane_state,
				       new_plane_state, i) {
		struct vc4_plane_state *vc4_plane_state;

		if (old_plane_state->fb && old_plane_state->crtc) {
			vc4_plane_state = to_vc4_plane_state(old_plane_state);
			load_state->membus_load -= vc4_plane_state->membus_load;
			load_state->hvs_load -= vc4_plane_state->hvs_load;
		}

		if (new_plane_state->fb && new_plane_state->crtc) {
			vc4_plane_state = to_vc4_plane_state(new_plane_state);
			load_state->membus_load += vc4_plane_state->membus_load;
			load_state->hvs_load += vc4_plane_state->hvs_load;
		}
	}

	/* Don't check the load when the tracker is disabled. */
	if (!vc4->load_tracker_enabled)
		return 0;

	/* The absolute limit is 2Gbyte/sec, but let's take a margin to let
	 * the system work when other blocks are accessing the memory.
	 */
	if (load_state->membus_load > SZ_1G + SZ_512M)
		return -ENOSPC;

	/* HVS clock is supposed to run @ 250Mhz, let's take a margin and
	 * consider the maximum number of cycles is 240M.
	 */
	if (load_state->hvs_load > 240000000ULL)
		return -ENOSPC;

	return 0;
}

static struct drm_private_state *
vc4_load_tracker_duplicate_state(struct drm_private_obj *obj)
{
	struct vc4_load_tracker_state *state;

	state = kmemdup(obj->state, sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_private_obj_duplicate_state(obj, &state->base);

	return &state->base;
}

static void vc4_load_tracker_destroy_state(struct drm_private_obj *obj,
					   struct drm_private_state *state)
{
	struct vc4_load_tracker_state *load_state;

	load_state = to_vc4_load_tracker_state(state);
	kfree(load_state);
}

static const struct drm_private_state_funcs vc4_load_tracker_state_funcs = {
	.atomic_duplicate_state = vc4_load_tracker_duplicate_state,
	.atomic_destroy_state = vc4_load_tracker_destroy_state,
};

#define NUM_OUTPUTS  6
#define NUM_CHANNELS 3

static int
vc4_atomic_check(struct drm_device *dev, struct drm_atomic_state *state)
{
	unsigned long unassigned_channels = GENMASK(NUM_CHANNELS - 1, 0);
	struct drm_crtc_state *old_crtc_state, *new_crtc_state;
	struct drm_crtc *crtc;
	int i, ret;

	/*
	 * Since the HVS FIFOs are shared across all the pixelvalves and
	 * the TXP (and thus all the CRTCs), we need to pull the current
	 * state of all the enabled CRTCs so that an update to a single
	 * CRTC still keeps the previous FIFOs enabled and assigned to
	 * the same CRTCs, instead of evaluating only the CRTC being
	 * modified.
	 */
	list_for_each_entry(crtc, &dev->mode_config.crtc_list, head) {
		struct drm_crtc_state *crtc_state;

		if (!crtc->state->enable)
			continue;

		crtc_state = drm_atomic_get_crtc_state(state, crtc);
		if (IS_ERR(crtc_state))
			return PTR_ERR(crtc_state);
	}

	for_each_oldnew_crtc_in_state(state, crtc, old_crtc_state, new_crtc_state, i) {
		struct vc4_crtc_state *new_vc4_crtc_state =
			to_vc4_crtc_state(new_crtc_state);
		struct vc4_crtc *vc4_crtc = to_vc4_crtc(crtc);
		unsigned int matching_channels;

		if (old_crtc_state->enable && !new_crtc_state->enable)
			new_vc4_crtc_state->assigned_channel = VC4_HVS_CHANNEL_DISABLED;

		if (!new_crtc_state->enable)
			continue;

		if (new_vc4_crtc_state->assigned_channel != VC4_HVS_CHANNEL_DISABLED) {
			unassigned_channels &= ~BIT(new_vc4_crtc_state->assigned_channel);
			continue;
		}

		/*
		 * The problem we have to solve here is that we have
		 * up to 7 encoders, connected to up to 6 CRTCs.
		 *
		 * Those CRTCs, depending on the instance, can be
		 * routed to 1, 2 or 3 HVS FIFOs, and we need to set
		 * the change the muxing between FIFOs and outputs in
		 * the HVS accordingly.
		 *
		 * It would be pretty hard to come up with an
		 * algorithm that would generically solve
		 * this. However, the current routing trees we support
		 * allow us to simplify a bit the problem.
		 *
		 * Indeed, with the current supported layouts, if we
		 * try to assign in the ascending crtc index order the
		 * FIFOs, we can't fall into the situation where an
		 * earlier CRTC that had multiple routes is assigned
		 * one that was the only option for a later CRTC.
		 *
		 * If the layout changes and doesn't give us that in
		 * the future, we will need to have something smarter,
		 * but it works so far.
		 */
		matching_channels = unassigned_channels & vc4_crtc->data->hvs_available_channels;
		if (matching_channels) {
			unsigned int channel = ffs(matching_channels) - 1;

			new_vc4_crtc_state->assigned_channel = channel;
			unassigned_channels &= ~BIT(channel);
		} else {
			return -EINVAL;
		}
	}

	ret = vc4_ctm_atomic_check(dev, state);
	if (ret < 0)
		return ret;

	ret = drm_atomic_helper_check(dev, state);
	if (ret)
		return ret;

	return vc4_load_tracker_atomic_check(state);
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
	struct vc4_load_tracker_state *load_state;
	bool is_vc5 = of_device_is_compatible(dev->dev->of_node,
					      "brcm,bcm2711-vc5");
	int ret;

	if (!is_vc5) {
		vc4->load_tracker_available = true;

		/* Start with the load tracker enabled. Can be
		 * disabled through the debugfs load_tracker file.
		 */
		vc4->load_tracker_enabled = true;
	}

	sema_init(&vc4->async_modeset, 1);

	/* Set support for vblank irq fast disable, before drm_vblank_init() */
	dev->vblank_disable_immediate = true;

	dev->irq_enabled = true;
	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret < 0) {
		dev_err(dev->dev, "failed to initialize vblank\n");
		return ret;
	}

	if (is_vc5) {
		dev->mode_config.max_width = 7680;
		dev->mode_config.max_height = 7680;
	} else {
		dev->mode_config.max_width = 2048;
		dev->mode_config.max_height = 2048;
	}

	dev->mode_config.funcs = &vc4_mode_funcs;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.async_page_flip = true;
	dev->mode_config.allow_fb_modifiers = true;

	drm_modeset_lock_init(&vc4->ctm_state_lock);

	ctm_state = kzalloc(sizeof(*ctm_state), GFP_KERNEL);
	if (!ctm_state)
		return -ENOMEM;

	drm_atomic_private_obj_init(dev, &vc4->ctm_manager, &ctm_state->base,
				    &vc4_ctm_state_funcs);

	if (vc4->load_tracker_available) {
		load_state = kzalloc(sizeof(*load_state), GFP_KERNEL);
		if (!load_state) {
			drm_atomic_private_obj_fini(&vc4->ctm_manager);
			return -ENOMEM;
		}

		drm_atomic_private_obj_init(dev, &vc4->load_tracker,
					    &load_state->base,
					    &vc4_load_tracker_state_funcs);
	}

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}
