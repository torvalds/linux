// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */
#include <linux/kernel.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane.h>
#include <drm/drm_vblank.h>
#include <drm/drm_vblank_work.h>

#include "i915_vgpu.h"
#include "i9xx_plane.h"
#include "icl_dsi.h"
#include "intel_atomic.h"
#include "intel_atomic_plane.h"
#include "intel_color.h"
#include "intel_crtc.h"
#include "intel_cursor.h"
#include "intel_display_debugfs.h"
#include "intel_display_irq.h"
#include "intel_display_trace.h"
#include "intel_display_types.h"
#include "intel_drrs.h"
#include "intel_dsi.h"
#include "intel_fifo_underrun.h"
#include "intel_pipe_crc.h"
#include "intel_psr.h"
#include "intel_sprite.h"
#include "intel_vblank.h"
#include "intel_vrr.h"
#include "skl_universal_plane.h"

static void assert_vblank_disabled(struct drm_crtc *crtc)
{
	struct intel_display *display = to_intel_display(crtc->dev);

	if (INTEL_DISPLAY_STATE_WARN(display, drm_crtc_vblank_get(crtc) == 0,
				     "[CRTC:%d:%s] vblank assertion failure (expected off, current on)\n",
				     crtc->base.id, crtc->name))
		drm_crtc_vblank_put(crtc);
}

struct intel_crtc *intel_first_crtc(struct drm_i915_private *i915)
{
	return to_intel_crtc(drm_crtc_from_index(&i915->drm, 0));
}

struct intel_crtc *intel_crtc_for_pipe(struct intel_display *display,
				       enum pipe pipe)
{
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		if (crtc->pipe == pipe)
			return crtc;
	}

	return NULL;
}

void intel_crtc_wait_for_next_vblank(struct intel_crtc *crtc)
{
	drm_crtc_wait_one_vblank(&crtc->base);
}

void intel_wait_for_vblank_if_active(struct drm_i915_private *i915,
				     enum pipe pipe)
{
	struct intel_display *display = &i915->display;
	struct intel_crtc *crtc = intel_crtc_for_pipe(display, pipe);

	if (crtc->active)
		intel_crtc_wait_for_next_vblank(crtc);
}

u32 intel_crtc_get_vblank_counter(struct intel_crtc *crtc)
{
	struct drm_vblank_crtc *vblank = drm_crtc_vblank_crtc(&crtc->base);

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

	if (DISPLAY_VER(dev_priv) >= 5 || IS_G4X(dev_priv))
		return 0xffffffff; /* full 32 bit counter */
	else if (DISPLAY_VER(dev_priv) >= 3)
		return 0xffffff; /* only 24 bits of frame count */
	else
		return 0; /* Gen2 doesn't have a hardware frame counter */
}

void intel_crtc_vblank_on(const struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	crtc->block_dc_for_vblank = intel_psr_needs_block_dc_vblank(crtc_state);

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
	struct intel_display *display = to_intel_display(crtc);

	/*
	 * Should really happen exactly when we disable the pipe
	 * but we want the frame counters in the trace, and that
	 * requires vblank support on some platforms/outputs.
	 */
	trace_intel_pipe_disable(crtc);

	drm_crtc_vblank_off(&crtc->base);
	assert_vblank_disabled(&crtc->base);

	crtc->block_dc_for_vblank = false;

	flush_work(&display->irq.vblank_dc_work);
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
	crtc_state->max_link_bpp_x16 = INT_MAX;
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

static void intel_crtc_destroy(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);

	cpu_latency_qos_remove_request(&crtc->vblank_pm_qos);

	drm_crtc_cleanup(&crtc->base);
	kfree(crtc);
}

static int intel_crtc_late_register(struct drm_crtc *crtc)
{
	intel_crtc_debugfs_add(to_intel_crtc(crtc));
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
	crtc->num_scalers = DISPLAY_RUNTIME_INFO(dev_priv)->num_scalers[pipe];

	if (DISPLAY_VER(dev_priv) >= 9)
		primary = skl_universal_plane_create(dev_priv, pipe, PLANE_1);
	else
		primary = intel_primary_plane_create(dev_priv, pipe);
	if (IS_ERR(primary)) {
		ret = PTR_ERR(primary);
		goto fail;
	}
	crtc->plane_ids_mask |= BIT(primary->id);

	intel_init_fifo_underrun_reporting(dev_priv, crtc, false);

	for_each_sprite(dev_priv, pipe, sprite) {
		struct intel_plane *plane;

		if (DISPLAY_VER(dev_priv) >= 9)
			plane = skl_universal_plane_create(dev_priv, pipe, PLANE_2 + sprite);
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
		else if (DISPLAY_VER(dev_priv) == 4)
			funcs = &i965_crtc_funcs;
		else if (IS_I945GM(dev_priv) || IS_I915GM(dev_priv))
			funcs = &i915gm_crtc_funcs;
		else if (DISPLAY_VER(dev_priv) == 3)
			funcs = &i915_crtc_funcs;
		else
			funcs = &i8xx_crtc_funcs;
	} else {
		if (DISPLAY_VER(dev_priv) >= 8)
			funcs = &bdw_crtc_funcs;
		else
			funcs = &ilk_crtc_funcs;
	}

	ret = drm_crtc_init_with_planes(&dev_priv->drm, &crtc->base,
					&primary->base, &cursor->base,
					funcs, "pipe %c", pipe_name(pipe));
	if (ret)
		goto fail;

	if (DISPLAY_VER(dev_priv) >= 11)
		drm_crtc_create_scaling_filter_property(&crtc->base,
						BIT(DRM_SCALING_FILTER_DEFAULT) |
						BIT(DRM_SCALING_FILTER_NEAREST_NEIGHBOR));

	intel_color_crtc_init(crtc);
	intel_drrs_crtc_init(crtc);
	intel_crtc_crc_init(crtc);

	cpu_latency_qos_add_request(&crtc->vblank_pm_qos, PM_QOS_DEFAULT_VALUE);

	drm_WARN_ON(&dev_priv->drm, drm_crtc_index(&crtc->base) != crtc->pipe);

	return 0;

fail:
	intel_crtc_free(crtc);

	return ret;
}

int intel_crtc_get_pipe_from_crtc_id_ioctl(struct drm_device *dev, void *data,
					   struct drm_file *file)
{
	struct drm_i915_get_pipe_from_crtc_id *pipe_from_crtc_id = data;
	struct drm_crtc *drm_crtc;
	struct intel_crtc *crtc;

	drm_crtc = drm_crtc_find(dev, file, pipe_from_crtc_id->crtc_id);
	if (!drm_crtc)
		return -ENOENT;

	crtc = to_intel_crtc(drm_crtc);
	pipe_from_crtc_id->pipe = crtc->pipe;

	return 0;
}

static bool intel_crtc_needs_vblank_work(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->hw.active &&
		!crtc_state->preload_luts &&
		!intel_crtc_needs_modeset(crtc_state) &&
		intel_crtc_needs_color_update(crtc_state) &&
		!intel_color_uses_dsb(crtc_state) &&
		!crtc_state->use_dsb;
}

static void intel_crtc_vblank_work(struct kthread_work *base)
{
	struct drm_vblank_work *work = to_drm_vblank_work(base);
	struct intel_crtc_state *crtc_state =
		container_of(work, typeof(*crtc_state), vblank_work);
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	trace_intel_crtc_vblank_work_start(crtc);

	intel_color_load_luts(crtc_state);

	if (crtc_state->uapi.event) {
		spin_lock_irq(&crtc->base.dev->event_lock);
		drm_crtc_send_vblank_event(&crtc->base, crtc_state->uapi.event);
		spin_unlock_irq(&crtc->base.dev->event_lock);
		crtc_state->uapi.event = NULL;
	}

	trace_intel_crtc_vblank_work_end(crtc);
}

static void intel_crtc_vblank_work_init(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);

	drm_vblank_work_init(&crtc_state->vblank_work, &crtc->base,
			     intel_crtc_vblank_work);
	/*
	 * Interrupt latency is critical for getting the vblank
	 * work executed as early as possible during the vblank.
	 */
	cpu_latency_qos_update_request(&crtc->vblank_pm_qos, 0);
}

void intel_wait_for_vblank_workers(struct intel_atomic_state *state)
{
	struct intel_crtc_state *crtc_state;
	struct intel_crtc *crtc;
	int i;

	for_each_new_intel_crtc_in_state(state, crtc, crtc_state, i) {
		if (!intel_crtc_needs_vblank_work(crtc_state))
			continue;

		drm_vblank_work_flush(&crtc_state->vblank_work);
		cpu_latency_qos_update_request(&crtc->vblank_pm_qos,
					       PM_QOS_DEFAULT_VALUE);
	}
}

int intel_usecs_to_scanlines(const struct drm_display_mode *adjusted_mode,
			     int usecs)
{
	/* paranoia */
	if (!adjusted_mode->crtc_htotal)
		return 1;

	return DIV_ROUND_UP_ULL(mul_u32_u32(usecs, adjusted_mode->crtc_clock),
				1000 * adjusted_mode->crtc_htotal);
}

int intel_scanlines_to_usecs(const struct drm_display_mode *adjusted_mode,
			     int scanlines)
{
	/* paranoia */
	if (!adjusted_mode->crtc_clock)
		return 1;

	return DIV_ROUND_UP_ULL(mul_u32_u32(scanlines, adjusted_mode->crtc_htotal * 1000),
				adjusted_mode->crtc_clock);
}

/**
 * intel_pipe_update_start() - start update of a set of display registers
 * @state: the atomic state
 * @crtc: the crtc
 *
 * Mark the start of an update to pipe registers that should be updated
 * atomically regarding vblank. If the next vblank will happens within
 * the next 100 us, this function waits until the vblank passes.
 *
 * After a successful call to this function, interrupts will be disabled
 * until a subsequent call to intel_pipe_update_end(). That is done to
 * avoid random delays.
 */
void intel_pipe_update_start(struct intel_atomic_state *state,
			     struct intel_crtc *crtc)
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	const struct intel_crtc_state *old_crtc_state =
		intel_atomic_get_old_crtc_state(state, crtc);
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	struct intel_vblank_evade_ctx evade;
	int scanline;

	intel_psr_lock(new_crtc_state);

	if (new_crtc_state->do_async_flip) {
		intel_crtc_prepare_vblank_event(new_crtc_state,
						&crtc->flip_done_event);
		return;
	}

	if (intel_crtc_needs_vblank_work(new_crtc_state))
		intel_crtc_vblank_work_init(new_crtc_state);

	if (state->base.legacy_cursor_update) {
		struct intel_plane *plane;
		struct intel_plane_state *old_plane_state, *new_plane_state;
		int i;

		for_each_oldnew_intel_plane_in_state(state, plane, old_plane_state,
						     new_plane_state, i) {
			if (old_plane_state->uapi.crtc == &crtc->base)
				intel_plane_init_cursor_vblank_work(old_plane_state,
								    new_plane_state);
		}
	}

	intel_vblank_evade_init(old_crtc_state, new_crtc_state, &evade);

	if (drm_WARN_ON(&dev_priv->drm, drm_crtc_vblank_get(&crtc->base)))
		goto irq_disable;

	/*
	 * Wait for psr to idle out after enabling the VBL interrupts
	 * VBL interrupts will start the PSR exit and prevent a PSR
	 * re-entry as well.
	 */
	intel_psr_wait_for_idle_locked(new_crtc_state);

	local_irq_disable();

	crtc->debug.min_vbl = evade.min;
	crtc->debug.max_vbl = evade.max;
	trace_intel_pipe_update_start(crtc);

	scanline = intel_vblank_evade(&evade);

	drm_crtc_vblank_put(&crtc->base);

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

void intel_crtc_arm_vblank_event(struct intel_crtc_state *crtc_state)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	unsigned long irqflags;

	if (!crtc_state->uapi.event)
		return;

	drm_WARN_ON(crtc->base.dev, drm_crtc_vblank_get(&crtc->base) != 0);

	spin_lock_irqsave(&crtc->base.dev->event_lock, irqflags);
	drm_crtc_arm_vblank_event(&crtc->base, crtc_state->uapi.event);
	spin_unlock_irqrestore(&crtc->base.dev->event_lock, irqflags);

	crtc_state->uapi.event = NULL;
}

void intel_crtc_prepare_vblank_event(struct intel_crtc_state *crtc_state,
				     struct drm_pending_vblank_event **event)
{
	struct intel_crtc *crtc = to_intel_crtc(crtc_state->uapi.crtc);
	unsigned long irqflags;

	spin_lock_irqsave(&crtc->base.dev->event_lock, irqflags);
	*event = crtc_state->uapi.event;
	spin_unlock_irqrestore(&crtc->base.dev->event_lock, irqflags);

	crtc_state->uapi.event = NULL;
}

/**
 * intel_pipe_update_end() - end update of a set of display registers
 * @state: the atomic state
 * @crtc: the crtc
 *
 * Mark the end of an update started with intel_pipe_update_start(). This
 * re-enables interrupts and verifies the update was actually completed
 * before a vblank.
 */
void intel_pipe_update_end(struct intel_atomic_state *state,
			   struct intel_crtc *crtc)
{
	struct intel_crtc_state *new_crtc_state =
		intel_atomic_get_new_crtc_state(state, crtc);
	enum pipe pipe = crtc->pipe;
	int scanline_end = intel_get_crtc_scanline(crtc);
	u32 end_vbl_count = intel_crtc_get_vblank_counter(crtc);
	ktime_t end_vbl_time = ktime_get();
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);

	if (new_crtc_state->do_async_flip)
		goto out;

	trace_intel_pipe_update_end(crtc, end_vbl_count, scanline_end);

	/*
	 * Incase of mipi dsi command mode, we need to set frame update
	 * request for every commit.
	 */
	if (DISPLAY_VER(dev_priv) >= 11 &&
	    intel_crtc_has_type(new_crtc_state, INTEL_OUTPUT_DSI))
		icl_dsi_frame_update(new_crtc_state);

	/* We're still in the vblank-evade critical section, this can't race.
	 * Would be slightly nice to just grab the vblank count and arm the
	 * event outside of the critical section - the spinlock might spin for a
	 * while ... */
	if (intel_crtc_needs_vblank_work(new_crtc_state)) {
		drm_vblank_work_schedule(&new_crtc_state->vblank_work,
					 drm_crtc_accurate_vblank_count(&crtc->base) + 1,
					 false);
	} else {
		intel_crtc_arm_vblank_event(new_crtc_state);
	}

	if (state->base.legacy_cursor_update) {
		struct intel_plane *plane;
		struct intel_plane_state *old_plane_state;
		int i;

		for_each_old_intel_plane_in_state(state, plane, old_plane_state, i) {
			if (old_plane_state->uapi.crtc == &crtc->base &&
			    old_plane_state->unpin_work.vblank) {
				drm_vblank_work_schedule(&old_plane_state->unpin_work,
							 drm_crtc_accurate_vblank_count(&crtc->base) + 1,
							 false);

				/* Remove plane from atomic state, cleanup/free is done from vblank worker. */
				memset(&state->base.planes[i], 0, sizeof(state->base.planes[i]));
			}
		}
	}

	/*
	 * Send VRR Push to terminate Vblank. If we are already in vblank
	 * this has to be done _after_ sampling the frame counter, as
	 * otherwise the push would immediately terminate the vblank and
	 * the sampled frame counter would correspond to the next frame
	 * instead of the current frame.
	 *
	 * There is a tiny race here (iff vblank evasion failed us) where
	 * we might sample the frame counter just before vmax vblank start
	 * but the push would be sent just after it. That would cause the
	 * push to affect the next frame instead of the current frame,
	 * which would cause the next frame to terminate already at vmin
	 * vblank start instead of vmax vblank start.
	 */
	intel_vrr_send_push(new_crtc_state);

	local_irq_enable();

	if (intel_vgpu_active(dev_priv))
		goto out;

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

out:
	intel_psr_unlock(new_crtc_state);
}
