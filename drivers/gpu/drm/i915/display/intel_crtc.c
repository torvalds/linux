// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/kernel.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_plane_helper.h>

#include "i915_trace.h"
#include "i915_vgpu.h"

#include "intel_atomic.h"
#include "intel_atomic_plane.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_cursor.h"
#include "intel_display_debugfs.h"
#include "intel_display_types.h"
#include "intel_dsi.h"
#include "intel_pipe_crc.h"
#include "intel_psr.h"
#include "intel_sprite.h"
#include "intel_vrr.h"
#include "i9xx_plane.h"
#include "skl_universal_plane.h"

static void assert_vblank_disabled(struct drm_crtc *crtc)
{
	if (I915_STATE_WARN_ON(drm_crtc_vblank_get(crtc) == 0))
		drm_crtc_vblank_put(crtc);
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc)
{
	struct drm_device *dev = crtc->base.dev;
	struct drm_vblank_crtc *vblank = &dev->vblank[drm_crtc_index(&crtc->base)];

	if (!crtc->active)
		return 0;

	if (!vblank->max_vblank_count)
		return (u32)drm_crtc_accurate_vblank_count(&crtc->base);

	return crtc->base.funcs->get_vblank_counter(&crtc->base);
}

u32 intel_crtc_max_vblank_count(const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *dev_priv = to_i915(crtc_state->uapi.crtc->dev);

	/*
	 * From Gen 11, In case of dsi cmd mode, frame counter wouldnt
	 * have updated at the beginning of TE, if we want to use
	 * the hw counter, then we would find it updated in only
	 * the next TE, hence switching to sw counter.
	 */
	if (crtc_state->mode_flags & (I915_MODE_FLAG_DSI_USE_TE0 |
				      I915_MODE_FLAG_DSI_USE_TE1))
		return 0;

	/*
	 * On i965gm the hardware frame counter reads
	 * zero when the TV encoder is enabled :(
	 */
	if (IS_I965GM(dev_priv) &&
	    (crtc_state->output_types & BIT(INTEL_OUTPUT_TVOUT)))
		return 0;

	if (INTEL_GEN(dev_priv) >= 5 || IS_G4X(dev_priv))
		return 0xffffffff; /* full 32 bit counter */
	else if (INTEL_GEN(dev_priv) >= 3)
		return 0xffffff; /* only 24 bits of frame count */
	else
		return 0; /* Gen2 doesn't have a hardware frame counter */
}

void intel_crtc_vblank_on(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	assert_vblank_disabled(&crtc->base);
	drm_crtc_set_max_vblank_count(&crtc->base,
				      intel_crtc_max_vblank_count(crtc_state));
	drm_crtc_vblank_on(&crtc->base);

	/*
	 * Should really happen exactly when we enable the pipe
	 * but we want the frame counters in the trace, and that
	 * requires vblank support on some platforms/outputs.
	 */
	trace_intel_pipe_enable(crtc);
}

void intel_crtc_vblank_off(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	/*
	 * Should really happen exactly when we disable the pipe
	 * but we want the frame counters in the trace, and that
	 * requires vblank support on some platforms/outputs.
	 */
	trace_intel_pipe_disable(crtc);

	drm_crtc_vblank_off(&crtc->base);
	assert_vblank_disabled(&crtc->base);
}

struct intel_crtc_state *intel_crtc_state_alloc(struct intel_crtc *crtc)
{
	struct intel_crtc_state *crtc_state;

	crtc_state = kmalloc(sizeof(*crtc_state), GFP_KERNEL);

	if (crtc_state)
		intel_crtc_state_reset(crtc_state, crtc);

	return crtc_state;
}

void intel_crtc_state_reset(struct intel_crtc_state *crtc_state,
			    struct intel_crtc *crtc)
{
	memset(crtc_state, 0, sizeof(*crtc_state));

	__drm_atomic_helper_crtc_state_reset(&crtc_state->uapi, &crtc->base);

	crtc_state->cpu_transcoder = INVALID_TRANSCODER;
	crtc_state->master_transcoder = INVALID_TRANSCODER;
	crtc_state->hsw_workaround_pipe = INVALID_PIPE;
	crtc_state->scaler_state.scaler_id = -1;
	crtc_state->mst_master_transcoder = INVALID_TRANSCODER;
}

static struct intel_crtc *intel_crtc_alloc(void)
{
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;

	crtc = kzalloc(sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return ERR_PTR(-ENOMEM);

	crtc_state = intel_crtc_state_alloc(crtc);
	if (!crtc_state) {
		kfree(crtc);
		return ERR_PTR(-ENOMEM);
	}

	crtc->base.state = &crtc_state->uapi;
	crtc->config = crtc_state;

	return crtc;
}

static void intel_crtc_free(struct intel_crtc *crtc)
{
	intel_crtc_destroy_state(&crtc->base, crtc->base.state);
	kfree(crtc);
}

static void intel_crtc_destroy(struct drm_crtc *crtc)
{
	struct intel_crtc *intel_crtc = to_intel_crtc(crtc);

	drm_crtc_cleanup(crtc);
	kfree(intel_crtc);
}

static int intel_crtc_late_register(struct drm_crtc *crtc)
{
	intel_crtc_debugfs_add(crtc);
	return 0;
}

#define INTEL_CRTC_FUNCS \
	.set_config = drm_atomic_helper_set_config, \
	.destroy = intel_crtc_destroy, \
	.page_flip = drm_atomic_helper_page_flip, \
	.atomic_duplicate_state = intel_crtc_duplicate_state, \
	.atomic_destroy_state = intel_crtc_destroy_state, \
	.set_crc_source = intel_crtc_set_crc_source, \
	.verify_crc_source = intel_crtc_verify_crc_source, \
	.get_crc_sources = intel_crtc_get_crc_sources, \
	.late_register = intel_crtc_late_register

static const struct drm_crtc_funcs bdw_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = g4x_get_vblank_counter,
	.enable_vblank = bdw_enable_vblank,
	.disable_vblank = bdw_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs ilk_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = g4x_get_vblank_counter,
	.enable_vblank = ilk_enable_vblank,
	.disable_vblank = ilk_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs g4x_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = g4x_get_vblank_counter,
	.enable_vblank = i965_enable_vblank,
	.disable_vblank = i965_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs i965_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = i915_get_vblank_counter,
	.enable_vblank = i965_enable_vblank,
	.disable_vblank = i965_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs i915gm_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = i915_get_vblank_counter,
	.enable_vblank = i915gm_enable_vblank,
	.disable_vblank = i915gm_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs i915_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	.get_vblank_counter = i915_get_vblank_counter,
	.enable_vblank = i8xx_enable_vblank,
	.disable_vblank = i8xx_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

static const struct drm_crtc_funcs i8xx_crtc_funcs = {
	INTEL_CRTC_FUNCS,

	/* no hw vblank counter */
	.enable_vblank = i8xx_enable_vblank,
	.disable_vblank = i8xx_disable_vblank,
	.get_vblank_timestamp = intel_crtc_get_vblank_timestamp,
};

int intel_crtc_init(struct drm_i915_private *dev_priv, enum pipe pipe)
{
	struct intel_plane *primary, *cursor;
	const struct drm_crtc_funcs *funcs;
	struct intel_crtc *crtc;
	int sprite, ret;

	crtc = intel_crtc_alloc();
	if (IS_ERR(crtc))
		return PTR_ERR(crtc);

	crtc->pipe = pipe;
	crtc->num_scalers = RUNTIME_INFO(dev_priv)->num_scalers[pipe];

	if (INTEL_GEN(dev_priv) >= 9)
		primary = skl_universal_plane_create(dev_priv, pipe,
						     PLANE_PRIMARY);
	else
		primary = intel_primary_plane_create(dev_priv, pipe);
	if (IS_ERR(primary)) {
		ret = PTR_ERR(primary);
		goto fail;
	}
	crtc->plane_ids_mask |= BIT(primary->id);

	for_each_sprite(dev_priv, pipe, sprite) {
		struct intel_plane *plane;

		if (INTEL_GEN(dev_priv) >= 9)
			plane = skl_universal_plane_create(dev_priv, pipe,
							   PLANE_SPRITE0 + sprite);
		else
			plane = intel_sprite_plane_create(dev_priv, pipe, sprite);
		if (IS_ERR(plane)) {
			ret = PTR_ERR(plane);
			goto fail;
		}
		crtc->plane_ids_mask |= BIT(plane->id);
	}

	cursor = intel_cursor_plane_create(dev_priv, pipe);
	if (IS_ERR(cursor)) {
		ret = PTR_ERR(cursor);
		goto fail;
	}
	crtc->plane_ids_mask |= BIT(cursor->id);

	if (HAS_GMCH(dev_priv)) {
		if (IS_CHERRYVIEW(dev_priv) ||
		    IS_VALLEYVIEW(dev_priv) || IS_G4X(dev_priv))
			funcs = &g4x_crtc_funcs;
		else if (IS_GEN(dev_priv, 4))
			funcs = &i965_crtc_funcs;
		else if (IS_I945GM(dev_priv) || IS_I915GM(dev_priv))
			funcs = &i915gm_crtc_funcs;
		else if (IS_GEN(dev_priv, 3))
			funcs = &i915_crtc_funcs;
		else
			funcs = &i8xx_crtc_funcs;
	} else {
		if (INTEL_GEN(dev_priv) >= 8)
			funcs = &bdw_crtc_funcs;
		else
			funcs = &ilk_crtc_funcs;
	}

	ret = drm_crtc_init_with_planes(&dev_priv->drm, &crtc->base,
					&primary->base, &cursor->base,
					funcs, "pipe %c", pipe_name(pipe));
	if (ret)
		goto fail;

	BUG_ON(pipe >= ARRAY_SIZE(dev_priv->pipe_to_crtc_mapping) ||
	       dev_priv->pipe_to_crtc_mapping[pipe] != NULL);
	dev_priv->pipe_to_crtc_mapping[pipe] = crtc;

	if (INTEL_GEN(dev_priv) < 9) {
		enum i9xx_plane_id i9xx_plane = primary->i9xx_plane;

		BUG_ON(i9xx_plane >= ARRAY_SIZE(dev_priv->plane_to_crtc_mapping) ||
		       dev_priv->plane_to_crtc_mapping[i9xx_plane] != NULL);
		dev_priv->plane_to_crtc_mapping[i9xx_plane] = crtc;
	}

	if (INTEL_GEN(dev_priv) >= 10)
		drm_crtc_create_scaling_filter_property(&crtc->base,
						BIT(DRM_SCALING_FILTER_DEFAULT) |
						BIT(DRM_SCALING_FILTER_NEAREST_NEIGHBOR));

	intel_color_init(crtc);

	intel_crtc_crc_init(crtc);

	drm_WARN_ON(&dev_priv->drm, drm_crtc_index(&crtc->base) != crtc->pipe);

	return 0;

fail:
	intel_crtc_free(crtc);

	return ret;
}

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs)
{
	/* paranoia */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP(usecs * adjusted_mode->crtc_clock,
			    1000 * adjusted_mode->crtc_htotal);
}

static int intel_mode_vblank_start(const struct drm_display_mode *mode)
{
	int vblank_start = mode->crtc_vblank_start;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		vblank_start = DIV_ROUND_UP(vblank_start, 2);

	return vblank_start;
}

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @new_crtc_state: the new crtc state
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays.
 */
void intel_pipe_update_start(const struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct drm_display_mode *adjusted_mode = &new_crtc_state->hw.adjusted_mode;
	long timeout = msecs_to_jiffies_timeout(1);
	int scanline, min, max, vblank_start;
	wait_queue_head_t *wq = drm_crtc_vblank_waitqueue(&crtc->base);
	bool need_vlv_dsi_wa = (IS_VALLEYVIEW(dev_priv) || IS_CHERRYVIEW(dev_priv)) &&
		intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI);
	DEFINE_WAIT(wait);

	if (new_crtc_state->uapi.async_flip)
		return;

	if (new_crtc_state->vrr.enable)
		vblank_start = intel_vrr_vmax_vblank_start(new_crtc_state);
	else
		vblank_start = intel_mode_vblank_start(adjusted_mode);

	/* FIXME needs to be calibrated sensibly */
	min = vblank_start - intel_usecs_to_scanlines(adjusted_mode,
						      VBLANK_EVASION_TIME_US);
	max = vblank_start - 1;

	if (min <= 0 || max <= 0)
		goto irq_disable;

	if (drm_WARN_ON(&dev_priv->drm, drm_crtc_vblank_get(&crtc->base)))
		goto irq_disable;

	/*
	 * Wait for psr to idle out after enabling the VBL interrupts
	 * VBL interrupts will start the PSR exit and prevent a PSR
	 * re-entry as well.
	 */
	intel_psr_wait_for_idle(new_crtc_state);

	local_irq_disable();

	crtc->debug.min_vbl = min;
	crtc->debug.max_vbl = max;
	trace_intel_pipe_update_start(crtc);

	for (;;) {
		/*
		 * prepare_to_wait() has a memory barrier, which guarantees
		 * other CPUs can see the task state update by the time we
		 * read the scanline.
		 */
		prepare_to_wait(wq, &wait, TASK_UNINTERRUPTIBLE);

		scanline = intel_get_crtc_scanline(crtc);
		if (scanline < min || scanline > max)
			break;

		if (!timeout) {
			drm_err(&dev_priv->drm,
				"Potential atomic update failure on pipe %c\n",
				pipe_name(crtc->pipe));
			break;
		}

		local_irq_enable();

		timeout = schedule_timeout(timeout);

		local_irq_disable();
	}

	finish_wait(wq, &wait);

	drm_crtc_vblank_put(&crtc->base);

	/*
	 * On VLV/CHV DSI the scanline counter would appear to
	 * increment approx. 1/3 of a scanline before start of vblank.
	 * The registers still get latched at start of vblank however.
	 * This means we must not write any registers on the first
	 * line of vblank (since not the whole line is actually in
	 * vblank). And unfortunately we can't use the interrupt to
	 * wait here since it will fire too soon. We could use the
	 * frame start interrupt instead since it will fire after the
	 * critical scanline, but that would require more changes
	 * in the interrupt code. So for now we'll just do the nasty
	 * thing and poll for the bad scanline to pass us by.
	 *
	 * FIXME figure out if BXT+ DSI suffers from this as well
	 */
	while (need_vlv_dsi_wa && scanline == vblank_start)
		scanline = intel_get_crtc_scanline(crtc);

	crtc->debug.scanline_start = scanline;
	crtc->debug.start_vbl_time = ktime_get();
	crtc->debug.start_vbl_count = intel_crtc_get_vblank_counter(crtc);

	trace_intel_pipe_update_vblank_evaded(crtc);
	return;

irq_disable:
	local_irq_disable();
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_VBLANK_EVADE)
static void dbg_vblank_evade(struct intel_crtc *crtc, ktime_t end)
{
	u64 delta = ktime_to_ns(ktime_sub(end, crtc->debug.start_vbl_time));
	unsigned int h;

	h = ilog2(delta >> 9);
	if (h >= ARRAY_SIZE(crtc->debug.vbl.times))
		h = ARRAY_SIZE(crtc->debug.vbl.times) - 1;
	crtc->debug.vbl.times[h]++;

	crtc->debug.vbl.sum += delta;
	if (!crtc->debug.vbl.min || delta < crtc->debug.vbl.min)
		crtc->debug.vbl.min = delta;
	if (delta > crtc->debug.vbl.max)
		crtc->debug.vbl.max = delta;

	if (delta > 1000 * VBLANK_EVASION_TIME_US) {
		drm_dbg_kms(crtc->base.dev,
			    "Atomic update on pipe (%c) took %lld us, max time under evasion is %u us\n",
			    pipe_name(crtc->pipe),
			    div_u64(delta, 1000),
			    VBLANK_EVASION_TIME_US);
		crtc->debug.vbl.over++;
	}
}
#else
static void dbg_vblank_evade(struct intel_crtc *crtc, ktime_t end) {}
#endif

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @new_crtc_state: the new crtc state
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank.
 */
void intel_pipe_update_end(struct intel_crtc_state *new_crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(new_crtc_state->uapi.crtc);
	enum pipe pipe = crtc->pipe;
	int scanline_end = intel_get_crtc_scanline(crtc);
	u32 end_vbl_count = intel_crtc_get_vblank_counter(crtc);
	ktime_t end_vbl_time = ktime_get();
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (new_crtc_state->uapi.async_flip)
		return;

	trace_intel_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/*
	 * Incase of mipi dsi command mode, we need to set frame update
	 * request for every commit.
	 */
	if (INTEL_GEN(dev_priv) >= 11 &&
	    intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI))
		icl_dsi_frame_update(new_crtc_state);

	/* We're still in the vblank-evade critical section, this can't race.
	 * Would be slightly nice to just grab the vblank count and arm the
	 * event outside of the critical section - the spinlock might spin for a
	 * while ... */
	if (new_crtc_state->uapi.event) {
		drm_WARN_ON(&dev_priv->drm,
			    drm_crtc_vblank_get(&crtc->base) != 0);

		spin_lock(&crtc->base.dev->event_lock);
		drm_crtc_arm_vblank_event(&crtc->base,
					  new_crtc_state->uapi.event);
		spin_unlock(&crtc->base.dev->event_lock);

		new_crtc_state->uapi.event = NULL;
	}

	local_irq_enable();

	/* Send VRR Push to terminate Vblank */
	intel_vrr_send_push(new_crtc_state);

	if (intel_vgpu_active(dev_priv))
		return;

	if (crtc->debug.start_vbl_count &&
	    crtc->debug.start_vbl_count != end_vbl_count) {
		drm_err(&dev_priv->drm,
			"Atomic update failure on pipe %c (start=%u end=%u) time %lld us, min %d, max %d, scanline start %d, end %d\n",
			pipe_name(pipe), crtc->debug.start_vbl_count,
			end_vbl_count,
			ktime_us_delta(end_vbl_time,
				       crtc->debug.start_vbl_time),
			crtc->debug.min_vbl, crtc->debug.max_vbl,
			crtc->debug.scanline_start, scanline_end);
	}

	dbg_vblank_evade(crtc, end_vbl_time);
}
