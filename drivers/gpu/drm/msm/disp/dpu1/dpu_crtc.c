// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2014-2018 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#define pr_fmt(fmt)	"[drm:%s:%d] " fmt, __func__, __LINE__
#include <linux/sort.h>
#include <linux/debugfs.h>
#include <linux/ktime.h>
#include <drm/drm_crtc.h>
#include <drm/drm_flip_work.h>
#include <drm/drm_mode.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_rect.h>

#include "dpu_kms.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_ctl.h"
#include "dpu_crtc.h"
#include "dpu_plane.h"
#include "dpu_encoder.h"
#include "dpu_vbif.h"
#include "dpu_core_perf.h"
#include "dpu_trace.h"

#define DPU_DRM_BLEND_OP_NOT_DEFINED    0
#define DPU_DRM_BLEND_OP_OPAQUE         1
#define DPU_DRM_BLEND_OP_PREMULTIPLIED  2
#define DPU_DRM_BLEND_OP_COVERAGE       3
#define DPU_DRM_BLEND_OP_MAX            4

/* layer mixer index on dpu_crtc */
#define LEFT_MIXER 0
#define RIGHT_MIXER 1

/* timeout in ms waiting for frame done */
#define DPU_CRTC_FRAME_DONE_TIMEOUT_MS	60

static struct dpu_kms *_dpu_crtc_get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;

	return to_dpu_kms(priv->kms);
}

static void dpu_crtc_destroy(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	DPU_DEBUG("\n");

	if (!crtc)
		return;

	drm_crtc_cleanup(crtc);
	kfree(dpu_crtc);
}

static void _dpu_crtc_setup_blend_cfg(struct dpu_crtc_mixer *mixer,
		struct dpu_plane_state *pstate, struct dpu_format *format)
{
	struct dpu_hw_mixer *lm = mixer->hw_lm;
	uint32_t blend_op;
	struct drm_format_name_buf format_name;

	/* default to opaque blending */
	blend_op = DPU_BLEND_FG_ALPHA_FG_CONST |
		DPU_BLEND_BG_ALPHA_BG_CONST;

	if (format->alpha_enable) {
		/* coverage blending */
		blend_op = DPU_BLEND_FG_ALPHA_FG_PIXEL |
			DPU_BLEND_BG_ALPHA_FG_PIXEL |
			DPU_BLEND_BG_INV_ALPHA;
	}

	lm->ops.setup_blend_config(lm, pstate->stage,
				0xFF, 0, blend_op);

	DPU_DEBUG("format:%s, alpha_en:%u blend_op:0x%x\n",
		drm_get_format_name(format->base.pixel_format, &format_name),
		format->alpha_enable, blend_op);
}

static void _dpu_crtc_program_lm_output_roi(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *crtc_state;
	int lm_idx, lm_horiz_position;

	dpu_crtc = to_dpu_crtc(crtc);
	crtc_state = to_dpu_crtc_state(crtc->state);

	lm_horiz_position = 0;
	for (lm_idx = 0; lm_idx < crtc_state->num_mixers; lm_idx++) {
		const struct drm_rect *lm_roi = &crtc_state->lm_bounds[lm_idx];
		struct dpu_hw_mixer *hw_lm = crtc_state->mixers[lm_idx].hw_lm;
		struct dpu_hw_mixer_cfg cfg;

		if (!lm_roi || !drm_rect_visible(lm_roi))
			continue;

		cfg.out_width = drm_rect_width(lm_roi);
		cfg.out_height = drm_rect_height(lm_roi);
		cfg.right_mixer = lm_horiz_position++;
		cfg.flags = 0;
		hw_lm->ops.setup_mixer_out(hw_lm, &cfg);
	}
}

static void _dpu_crtc_blend_setup_mixer(struct drm_crtc *crtc,
	struct dpu_crtc *dpu_crtc, struct dpu_crtc_mixer *mixer)
{
	struct drm_plane *plane;
	struct drm_framebuffer *fb;
	struct drm_plane_state *state;
	struct dpu_crtc_state *cstate = to_dpu_crtc_state(crtc->state);
	struct dpu_plane_state *pstate = NULL;
	struct dpu_format *format;
	struct dpu_hw_ctl *ctl = mixer->lm_ctl;
	struct dpu_hw_stage_cfg *stage_cfg = &dpu_crtc->stage_cfg;

	u32 flush_mask;
	uint32_t stage_idx, lm_idx;
	int zpos_cnt[DPU_STAGE_MAX + 1] = { 0 };
	bool bg_alpha_enable = false;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		state = plane->state;
		if (!state)
			continue;

		pstate = to_dpu_plane_state(state);
		fb = state->fb;

		dpu_plane_get_ctl_flush(plane, ctl, &flush_mask);

		DPU_DEBUG("crtc %d stage:%d - plane %d sspp %d fb %d\n",
				crtc->base.id,
				pstate->stage,
				plane->base.id,
				dpu_plane_pipe(plane) - SSPP_VIG0,
				state->fb ? state->fb->base.id : -1);

		format = to_dpu_format(msm_framebuffer_format(pstate->base.fb));

		if (pstate->stage == DPU_STAGE_BASE && format->alpha_enable)
			bg_alpha_enable = true;

		stage_idx = zpos_cnt[pstate->stage]++;
		stage_cfg->stage[pstate->stage][stage_idx] =
					dpu_plane_pipe(plane);
		stage_cfg->multirect_index[pstate->stage][stage_idx] =
					pstate->multirect_index;

		trace_dpu_crtc_setup_mixer(DRMID(crtc), DRMID(plane),
					   state, pstate, stage_idx,
					   dpu_plane_pipe(plane) - SSPP_VIG0,
					   format->base.pixel_format,
					   fb ? fb->modifier : 0);

		/* blend config update */
		for (lm_idx = 0; lm_idx < cstate->num_mixers; lm_idx++) {
			_dpu_crtc_setup_blend_cfg(mixer + lm_idx,
						pstate, format);

			mixer[lm_idx].flush_mask |= flush_mask;

			if (bg_alpha_enable && !format->alpha_enable)
				mixer[lm_idx].mixer_op_mode = 0;
			else
				mixer[lm_idx].mixer_op_mode |=
						1 << pstate->stage;
		}
	}

	 _dpu_crtc_program_lm_output_roi(crtc);
}

/**
 * _dpu_crtc_blend_setup - configure crtc mixers
 * @crtc: Pointer to drm crtc structure
 */
static void _dpu_crtc_blend_setup(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct dpu_crtc_state *cstate = to_dpu_crtc_state(crtc->state);
	struct dpu_crtc_mixer *mixer = cstate->mixers;
	struct dpu_hw_ctl *ctl;
	struct dpu_hw_mixer *lm;
	int i;

	DPU_DEBUG("%s\n", dpu_crtc->name);

	for (i = 0; i < cstate->num_mixers; i++) {
		if (!mixer[i].hw_lm || !mixer[i].lm_ctl) {
			DPU_ERROR("invalid lm or ctl assigned to mixer\n");
			return;
		}
		mixer[i].mixer_op_mode = 0;
		mixer[i].flush_mask = 0;
		if (mixer[i].lm_ctl->ops.clear_all_blendstages)
			mixer[i].lm_ctl->ops.clear_all_blendstages(
					mixer[i].lm_ctl);
	}

	/* initialize stage cfg */
	memset(&dpu_crtc->stage_cfg, 0, sizeof(struct dpu_hw_stage_cfg));

	_dpu_crtc_blend_setup_mixer(crtc, dpu_crtc, mixer);

	for (i = 0; i < cstate->num_mixers; i++) {
		ctl = mixer[i].lm_ctl;
		lm = mixer[i].hw_lm;

		lm->ops.setup_alpha_out(lm, mixer[i].mixer_op_mode);

		mixer[i].flush_mask |= ctl->ops.get_bitmask_mixer(ctl,
			mixer[i].hw_lm->idx);

		/* stage config flush mask */
		ctl->ops.update_pending_flush(ctl, mixer[i].flush_mask);

		DPU_DEBUG("lm %d, op_mode 0x%X, ctl %d, flush mask 0x%x\n",
			mixer[i].hw_lm->idx - LM_0,
			mixer[i].mixer_op_mode,
			ctl->idx - CTL_0,
			mixer[i].flush_mask);

		ctl->ops.setup_blendstage(ctl, mixer[i].hw_lm->idx,
			&dpu_crtc->stage_cfg);
	}
}

/**
 *  _dpu_crtc_complete_flip - signal pending page_flip events
 * Any pending vblank events are added to the vblank_event_list
 * so that the next vblank interrupt shall signal them.
 * However PAGE_FLIP events are not handled through the vblank_event_list.
 * This API signals any pending PAGE_FLIP events requested through
 * DRM_IOCTL_MODE_PAGE_FLIP and are cached in the dpu_crtc->event.
 * @crtc: Pointer to drm crtc structure
 */
static void _dpu_crtc_complete_flip(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (dpu_crtc->event) {
		DRM_DEBUG_VBL("%s: send event: %pK\n", dpu_crtc->name,
			      dpu_crtc->event);
		trace_dpu_crtc_complete_flip(DRMID(crtc));
		drm_crtc_send_vblank_event(crtc, dpu_crtc->event);
		dpu_crtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

enum dpu_intf_mode dpu_crtc_get_intf_mode(struct drm_crtc *crtc)
{
	struct drm_encoder *encoder;

	if (!crtc || !crtc->dev) {
		DPU_ERROR("invalid crtc\n");
		return INTF_MODE_NONE;
	}

	WARN_ON(!drm_modeset_is_locked(&crtc->mutex));

	/* TODO: Returns the first INTF_MODE, could there be multiple values? */
	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		return dpu_encoder_get_intf_mode(encoder);

	return INTF_MODE_NONE;
}

void dpu_crtc_vblank_callback(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	/* keep statistics on vblank callback - with auto reset via debugfs */
	if (ktime_compare(dpu_crtc->vblank_cb_time, ktime_set(0, 0)) == 0)
		dpu_crtc->vblank_cb_time = ktime_get();
	else
		dpu_crtc->vblank_cb_count++;
	_dpu_crtc_complete_flip(crtc);
	drm_crtc_handle_vblank(crtc);
	trace_dpu_crtc_vblank_cb(DRMID(crtc));
}

static void dpu_crtc_release_bw_unlocked(struct drm_crtc *crtc)
{
	int ret = 0;
	struct drm_modeset_acquire_ctx ctx;

	DRM_MODESET_LOCK_ALL_BEGIN(crtc->dev, ctx, 0, ret);
	dpu_core_perf_crtc_release_bw(crtc);
	DRM_MODESET_LOCK_ALL_END(ctx, ret);
	if (ret)
		DRM_ERROR("Failed to acquire modeset locks to release bw, %d\n",
			  ret);
}

static void dpu_crtc_frame_event_work(struct kthread_work *work)
{
	struct dpu_crtc_frame_event *fevent = container_of(work,
			struct dpu_crtc_frame_event, work);
	struct drm_crtc *crtc = fevent->crtc;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	unsigned long flags;
	bool frame_done = false;

	DPU_ATRACE_BEGIN("crtc_frame_event");

	DRM_DEBUG_KMS("crtc%d event:%u ts:%lld\n", crtc->base.id, fevent->event,
			ktime_to_ns(fevent->ts));

	if (fevent->event & (DPU_ENCODER_FRAME_EVENT_DONE
				| DPU_ENCODER_FRAME_EVENT_ERROR
				| DPU_ENCODER_FRAME_EVENT_PANEL_DEAD)) {

		if (atomic_read(&dpu_crtc->frame_pending) < 1) {
			/* this should not happen */
			DRM_ERROR("crtc%d ev:%u ts:%lld frame_pending:%d\n",
					crtc->base.id,
					fevent->event,
					ktime_to_ns(fevent->ts),
					atomic_read(&dpu_crtc->frame_pending));
		} else if (atomic_dec_return(&dpu_crtc->frame_pending) == 0) {
			/* release bandwidth and other resources */
			trace_dpu_crtc_frame_event_done(DRMID(crtc),
							fevent->event);
			dpu_crtc_release_bw_unlocked(crtc);
		} else {
			trace_dpu_crtc_frame_event_more_pending(DRMID(crtc),
								fevent->event);
		}

		if (fevent->event & DPU_ENCODER_FRAME_EVENT_DONE)
			dpu_core_perf_crtc_update(crtc, 0, false);

		if (fevent->event & (DPU_ENCODER_FRAME_EVENT_DONE
					| DPU_ENCODER_FRAME_EVENT_ERROR))
			frame_done = true;
	}

	if (fevent->event & DPU_ENCODER_FRAME_EVENT_PANEL_DEAD)
		DPU_ERROR("crtc%d ts:%lld received panel dead event\n",
				crtc->base.id, ktime_to_ns(fevent->ts));

	if (frame_done)
		complete_all(&dpu_crtc->frame_done_comp);

	spin_lock_irqsave(&dpu_crtc->spin_lock, flags);
	list_add_tail(&fevent->list, &dpu_crtc->frame_event_list);
	spin_unlock_irqrestore(&dpu_crtc->spin_lock, flags);
	DPU_ATRACE_END("crtc_frame_event");
}

/*
 * dpu_crtc_frame_event_cb - crtc frame event callback API. CRTC module
 * registers this API to encoder for all frame event callbacks like
 * frame_error, frame_done, idle_timeout, etc. Encoder may call different events
 * from different context - IRQ, user thread, commit_thread, etc. Each event
 * should be carefully reviewed and should be processed in proper task context
 * to avoid schedulin delay or properly manage the irq context's bottom half
 * processing.
 */
static void dpu_crtc_frame_event_cb(void *data, u32 event)
{
	struct drm_crtc *crtc = (struct drm_crtc *)data;
	struct dpu_crtc *dpu_crtc;
	struct msm_drm_private *priv;
	struct dpu_crtc_frame_event *fevent;
	unsigned long flags;
	u32 crtc_id;

	/* Nothing to do on idle event */
	if (event & DPU_ENCODER_FRAME_EVENT_IDLE)
		return;

	dpu_crtc = to_dpu_crtc(crtc);
	priv = crtc->dev->dev_private;
	crtc_id = drm_crtc_index(crtc);

	trace_dpu_crtc_frame_event_cb(DRMID(crtc), event);

	spin_lock_irqsave(&dpu_crtc->spin_lock, flags);
	fevent = list_first_entry_or_null(&dpu_crtc->frame_event_list,
			struct dpu_crtc_frame_event, list);
	if (fevent)
		list_del_init(&fevent->list);
	spin_unlock_irqrestore(&dpu_crtc->spin_lock, flags);

	if (!fevent) {
		DRM_ERROR("crtc%d event %d overflow\n", crtc->base.id, event);
		return;
	}

	fevent->event = event;
	fevent->crtc = crtc;
	fevent->ts = ktime_get();
	kthread_queue_work(&priv->event_thread[crtc_id].worker, &fevent->work);
}

void dpu_crtc_complete_commit(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state)
{
	if (!crtc || !crtc->state) {
		DPU_ERROR("invalid crtc\n");
		return;
	}
	trace_dpu_crtc_complete_commit(DRMID(crtc));
}

static void _dpu_crtc_setup_lm_bounds(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct dpu_crtc_state *cstate = to_dpu_crtc_state(state);
	struct drm_display_mode *adj_mode = &state->adjusted_mode;
	u32 crtc_split_width = adj_mode->hdisplay / cstate->num_mixers;
	int i;

	for (i = 0; i < cstate->num_mixers; i++) {
		struct drm_rect *r = &cstate->lm_bounds[i];
		r->x1 = crtc_split_width * i;
		r->y1 = 0;
		r->x2 = r->x1 + crtc_split_width;
		r->y2 = adj_mode->vdisplay;

		trace_dpu_crtc_setup_lm_bounds(DRMID(crtc), i, r);
	}

	drm_mode_debug_printmodeline(adj_mode);
}

static void dpu_crtc_atomic_begin(struct drm_crtc *crtc,
		struct drm_crtc_state *old_state)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *cstate;
	struct drm_encoder *encoder;
	struct drm_device *dev;
	unsigned long flags;
	struct dpu_crtc_smmu_state_data *smmu_state;

	if (!crtc) {
		DPU_ERROR("invalid crtc\n");
		return;
	}

	if (!crtc->state->enable) {
		DPU_DEBUG("crtc%d -> enable %d, skip atomic_begin\n",
				crtc->base.id, crtc->state->enable);
		return;
	}

	DPU_DEBUG("crtc%d\n", crtc->base.id);

	dpu_crtc = to_dpu_crtc(crtc);
	cstate = to_dpu_crtc_state(crtc->state);
	dev = crtc->dev;
	smmu_state = &dpu_crtc->smmu_state;

	_dpu_crtc_setup_lm_bounds(crtc, crtc->state);

	if (dpu_crtc->event) {
		WARN_ON(dpu_crtc->event);
	} else {
		spin_lock_irqsave(&dev->event_lock, flags);
		dpu_crtc->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	/* encoder will trigger pending mask now */
	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		dpu_encoder_trigger_kickoff_pending(encoder);

	/*
	 * If no mixers have been allocated in dpu_crtc_atomic_check(),
	 * it means we are trying to flush a CRTC whose state is disabled:
	 * nothing else needs to be done.
	 */
	if (unlikely(!cstate->num_mixers))
		return;

	_dpu_crtc_blend_setup(crtc);

	/*
	 * PP_DONE irq is only used by command mode for now.
	 * It is better to request pending before FLUSH and START trigger
	 * to make sure no pp_done irq missed.
	 * This is safe because no pp_done will happen before SW trigger
	 * in command mode.
	 */
}

static void dpu_crtc_atomic_flush(struct drm_crtc *crtc,
		struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc;
	struct drm_device *dev;
	struct drm_plane *plane;
	struct msm_drm_private *priv;
	struct msm_drm_thread *event_thread;
	unsigned long flags;
	struct dpu_crtc_state *cstate;

	if (!crtc->state->enable) {
		DPU_DEBUG("crtc%d -> enable %d, skip atomic_flush\n",
				crtc->base.id, crtc->state->enable);
		return;
	}

	DPU_DEBUG("crtc%d\n", crtc->base.id);

	dpu_crtc = to_dpu_crtc(crtc);
	cstate = to_dpu_crtc_state(crtc->state);
	dev = crtc->dev;
	priv = dev->dev_private;

	if (crtc->index >= ARRAY_SIZE(priv->event_thread)) {
		DPU_ERROR("invalid crtc index[%d]\n", crtc->index);
		return;
	}

	event_thread = &priv->event_thread[crtc->index];

	if (dpu_crtc->event) {
		DPU_DEBUG("already received dpu_crtc->event\n");
	} else {
		spin_lock_irqsave(&dev->event_lock, flags);
		dpu_crtc->event = crtc->state->event;
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&dev->event_lock, flags);
	}

	/*
	 * If no mixers has been allocated in dpu_crtc_atomic_check(),
	 * it means we are trying to flush a CRTC whose state is disabled:
	 * nothing else needs to be done.
	 */
	if (unlikely(!cstate->num_mixers))
		return;

	/*
	 * For planes without commit update, drm framework will not add
	 * those planes to current state since hardware update is not
	 * required. However, if those planes were power collapsed since
	 * last commit cycle, driver has to restore the hardware state
	 * of those planes explicitly here prior to plane flush.
	 */
	drm_atomic_crtc_for_each_plane(plane, crtc)
		dpu_plane_restore(plane);

	/* update performance setting before crtc kickoff */
	dpu_core_perf_crtc_update(crtc, 1, false);

	/*
	 * Final plane updates: Give each plane a chance to complete all
	 *                      required writes/flushing before crtc's "flush
	 *                      everything" call below.
	 */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		if (dpu_crtc->smmu_state.transition_error)
			dpu_plane_set_error(plane, true);
		dpu_plane_flush(plane);
	}

	/* Kickoff will be scheduled by outer layer */
}

/**
 * dpu_crtc_destroy_state - state destroy hook
 * @crtc: drm CRTC
 * @state: CRTC state object to release
 */
static void dpu_crtc_destroy_state(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *cstate;

	if (!crtc || !state) {
		DPU_ERROR("invalid argument(s)\n");
		return;
	}

	dpu_crtc = to_dpu_crtc(crtc);
	cstate = to_dpu_crtc_state(state);

	DPU_DEBUG("crtc%d\n", crtc->base.id);

	__drm_atomic_helper_crtc_destroy_state(state);

	kfree(cstate);
}

static int _dpu_crtc_wait_for_frame_done(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	int ret, rc = 0;

	if (!atomic_read(&dpu_crtc->frame_pending)) {
		DPU_DEBUG("no frames pending\n");
		return 0;
	}

	DPU_ATRACE_BEGIN("frame done completion wait");
	ret = wait_for_completion_timeout(&dpu_crtc->frame_done_comp,
			msecs_to_jiffies(DPU_CRTC_FRAME_DONE_TIMEOUT_MS));
	if (!ret) {
		DRM_ERROR("frame done wait timed out, ret:%d\n", ret);
		rc = -ETIMEDOUT;
	}
	DPU_ATRACE_END("frame done completion wait");

	return rc;
}

void dpu_crtc_commit_kickoff(struct drm_crtc *crtc, bool async)
{
	struct drm_encoder *encoder;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct dpu_kms *dpu_kms = _dpu_crtc_get_kms(crtc);
	struct dpu_crtc_state *cstate = to_dpu_crtc_state(crtc->state);
	int ret;

	/*
	 * If no mixers has been allocated in dpu_crtc_atomic_check(),
	 * it means we are trying to start a CRTC whose state is disabled:
	 * nothing else needs to be done.
	 */
	if (unlikely(!cstate->num_mixers))
		return;

	DPU_ATRACE_BEGIN("crtc_commit");

	/*
	 * Encoder will flush/start now, unless it has a tx pending. If so, it
	 * may delay and flush at an irq event (e.g. ppdone)
	 */
	drm_for_each_encoder_mask(encoder, crtc->dev,
				  crtc->state->encoder_mask)
		dpu_encoder_prepare_for_kickoff(encoder, async);

	if (!async) {
		/* wait for frame_event_done completion */
		DPU_ATRACE_BEGIN("wait_for_frame_done_event");
		ret = _dpu_crtc_wait_for_frame_done(crtc);
		DPU_ATRACE_END("wait_for_frame_done_event");
		if (ret) {
			DPU_ERROR("crtc%d wait for frame done failed;frame_pending%d\n",
					crtc->base.id,
					atomic_read(&dpu_crtc->frame_pending));
			goto end;
		}

		if (atomic_inc_return(&dpu_crtc->frame_pending) == 1) {
			/* acquire bandwidth and other resources */
			DPU_DEBUG("crtc%d first commit\n", crtc->base.id);
		} else
			DPU_DEBUG("crtc%d commit\n", crtc->base.id);

		dpu_crtc->play_count++;
	}

	dpu_vbif_clear_errors(dpu_kms);

	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		dpu_encoder_kickoff(encoder, async);

end:
	if (!async)
		reinit_completion(&dpu_crtc->frame_done_comp);
	DPU_ATRACE_END("crtc_commit");
}

static void dpu_crtc_reset(struct drm_crtc *crtc)
{
	struct dpu_crtc_state *cstate;

	if (crtc->state)
		dpu_crtc_destroy_state(crtc, crtc->state);

	crtc->state = kzalloc(sizeof(*cstate), GFP_KERNEL);
	if (crtc->state)
		crtc->state->crtc = crtc;
}

/**
 * dpu_crtc_duplicate_state - state duplicate hook
 * @crtc: Pointer to drm crtc structure
 * @Returns: Pointer to new drm_crtc_state structure
 */
static struct drm_crtc_state *dpu_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *cstate, *old_cstate;

	if (!crtc || !crtc->state) {
		DPU_ERROR("invalid argument(s)\n");
		return NULL;
	}

	dpu_crtc = to_dpu_crtc(crtc);
	old_cstate = to_dpu_crtc_state(crtc->state);
	cstate = kmemdup(old_cstate, sizeof(*old_cstate), GFP_KERNEL);
	if (!cstate) {
		DPU_ERROR("failed to allocate state\n");
		return NULL;
	}

	/* duplicate base helper */
	__drm_atomic_helper_crtc_duplicate_state(crtc, &cstate->base);

	return &cstate->base;
}

static void dpu_crtc_disable(struct drm_crtc *crtc,
			     struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_crtc_state *cstate;
	struct drm_display_mode *mode;
	struct drm_encoder *encoder;
	struct msm_drm_private *priv;
	unsigned long flags;

	if (!crtc || !crtc->dev || !crtc->dev->dev_private || !crtc->state) {
		DPU_ERROR("invalid crtc\n");
		return;
	}
	dpu_crtc = to_dpu_crtc(crtc);
	cstate = to_dpu_crtc_state(crtc->state);
	mode = &cstate->base.adjusted_mode;
	priv = crtc->dev->dev_private;

	DRM_DEBUG_KMS("crtc%d\n", crtc->base.id);

	/* Disable/save vblank irq handling */
	drm_crtc_vblank_off(crtc);

	drm_for_each_encoder_mask(encoder, crtc->dev,
				  old_crtc_state->encoder_mask)
		dpu_encoder_assign_crtc(encoder, NULL);

	/* wait for frame_event_done completion */
	if (_dpu_crtc_wait_for_frame_done(crtc))
		DPU_ERROR("crtc%d wait for frame done failed;frame_pending%d\n",
				crtc->base.id,
				atomic_read(&dpu_crtc->frame_pending));

	trace_dpu_crtc_disable(DRMID(crtc), false, dpu_crtc);
	dpu_crtc->enabled = false;

	if (atomic_read(&dpu_crtc->frame_pending)) {
		trace_dpu_crtc_disable_frame_pending(DRMID(crtc),
				     atomic_read(&dpu_crtc->frame_pending));
		dpu_core_perf_crtc_release_bw(crtc);
		atomic_set(&dpu_crtc->frame_pending, 0);
	}

	dpu_core_perf_crtc_update(crtc, 0, true);

	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		dpu_encoder_register_frame_event_callback(encoder, NULL, NULL);

	memset(cstate->mixers, 0, sizeof(cstate->mixers));
	cstate->num_mixers = 0;

	/* disable clk & bw control until clk & bw properties are set */
	cstate->bw_control = false;
	cstate->bw_split_vote = false;

	if (crtc->state->event && !crtc->state->active) {
		spin_lock_irqsave(&crtc->dev->event_lock, flags);
		drm_crtc_send_vblank_event(crtc, crtc->state->event);
		crtc->state->event = NULL;
		spin_unlock_irqrestore(&crtc->dev->event_lock, flags);
	}

	pm_runtime_put_sync(crtc->dev->dev);
}

static void dpu_crtc_enable(struct drm_crtc *crtc,
		struct drm_crtc_state *old_crtc_state)
{
	struct dpu_crtc *dpu_crtc;
	struct drm_encoder *encoder;
	struct msm_drm_private *priv;

	if (!crtc || !crtc->dev || !crtc->dev->dev_private) {
		DPU_ERROR("invalid crtc\n");
		return;
	}
	priv = crtc->dev->dev_private;

	pm_runtime_get_sync(crtc->dev->dev);

	DRM_DEBUG_KMS("crtc%d\n", crtc->base.id);
	dpu_crtc = to_dpu_crtc(crtc);

	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		dpu_encoder_register_frame_event_callback(encoder,
				dpu_crtc_frame_event_cb, (void *)crtc);

	trace_dpu_crtc_enable(DRMID(crtc), true, dpu_crtc);
	dpu_crtc->enabled = true;

	drm_for_each_encoder_mask(encoder, crtc->dev, crtc->state->encoder_mask)
		dpu_encoder_assign_crtc(encoder, crtc);

	/* Enable/restore vblank irq handling */
	drm_crtc_vblank_on(crtc);
}

struct plane_state {
	struct dpu_plane_state *dpu_pstate;
	const struct drm_plane_state *drm_pstate;
	int stage;
	u32 pipe_id;
};

static int dpu_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct dpu_crtc *dpu_crtc;
	struct plane_state *pstates;
	struct dpu_crtc_state *cstate;

	const struct drm_plane_state *pstate;
	struct drm_plane *plane;
	struct drm_display_mode *mode;

	int cnt = 0, rc = 0, mixer_width, i, z_pos;

	struct dpu_multirect_plane_states multirect_plane[DPU_STAGE_MAX * 2];
	int multirect_count = 0;
	const struct drm_plane_state *pipe_staged[SSPP_MAX];
	int left_zpos_cnt = 0, right_zpos_cnt = 0;
	struct drm_rect crtc_rect = { 0 };

	if (!crtc) {
		DPU_ERROR("invalid crtc\n");
		return -EINVAL;
	}

	pstates = kzalloc(sizeof(*pstates) * DPU_STAGE_MAX * 4, GFP_KERNEL);

	dpu_crtc = to_dpu_crtc(crtc);
	cstate = to_dpu_crtc_state(state);

	if (!state->enable || !state->active) {
		DPU_DEBUG("crtc%d -> enable %d, active %d, skip atomic_check\n",
				crtc->base.id, state->enable, state->active);
		goto end;
	}

	mode = &state->adjusted_mode;
	DPU_DEBUG("%s: check", dpu_crtc->name);

	/* force a full mode set if active state changed */
	if (state->active_changed)
		state->mode_changed = true;

	memset(pipe_staged, 0, sizeof(pipe_staged));

	mixer_width = mode->hdisplay / cstate->num_mixers;

	_dpu_crtc_setup_lm_bounds(crtc, state);

	crtc_rect.x2 = mode->hdisplay;
	crtc_rect.y2 = mode->vdisplay;

	 /* get plane state for all drm planes associated with crtc state */
	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		struct drm_rect dst, clip = crtc_rect;

		if (IS_ERR_OR_NULL(pstate)) {
			rc = PTR_ERR(pstate);
			DPU_ERROR("%s: failed to get plane%d state, %d\n",
					dpu_crtc->name, plane->base.id, rc);
			goto end;
		}
		if (cnt >= DPU_STAGE_MAX * 4)
			continue;

		pstates[cnt].dpu_pstate = to_dpu_plane_state(pstate);
		pstates[cnt].drm_pstate = pstate;
		pstates[cnt].stage = pstate->normalized_zpos;
		pstates[cnt].pipe_id = dpu_plane_pipe(plane);

		if (pipe_staged[pstates[cnt].pipe_id]) {
			multirect_plane[multirect_count].r0 =
				pipe_staged[pstates[cnt].pipe_id];
			multirect_plane[multirect_count].r1 = pstate;
			multirect_count++;

			pipe_staged[pstates[cnt].pipe_id] = NULL;
		} else {
			pipe_staged[pstates[cnt].pipe_id] = pstate;
		}

		cnt++;

		dst = drm_plane_state_dest(pstate);
		if (!drm_rect_intersect(&clip, &dst)) {
			DPU_ERROR("invalid vertical/horizontal destination\n");
			DPU_ERROR("display: " DRM_RECT_FMT " plane: "
				  DRM_RECT_FMT "\n", DRM_RECT_ARG(&crtc_rect),
				  DRM_RECT_ARG(&dst));
			rc = -E2BIG;
			goto end;
		}
	}

	for (i = 1; i < SSPP_MAX; i++) {
		if (pipe_staged[i]) {
			dpu_plane_clear_multirect(pipe_staged[i]);

			if (is_dpu_plane_virtual(pipe_staged[i]->plane)) {
				DPU_ERROR(
					"r1 only virt plane:%d not supported\n",
					pipe_staged[i]->plane->base.id);
				rc  = -EINVAL;
				goto end;
			}
		}
	}

	z_pos = -1;
	for (i = 0; i < cnt; i++) {
		/* reset counts at every new blend stage */
		if (pstates[i].stage != z_pos) {
			left_zpos_cnt = 0;
			right_zpos_cnt = 0;
			z_pos = pstates[i].stage;
		}

		/* verify z_pos setting before using it */
		if (z_pos >= DPU_STAGE_MAX - DPU_STAGE_0) {
			DPU_ERROR("> %d plane stages assigned\n",
					DPU_STAGE_MAX - DPU_STAGE_0);
			rc = -EINVAL;
			goto end;
		} else if (pstates[i].drm_pstate->crtc_x < mixer_width) {
			if (left_zpos_cnt == 2) {
				DPU_ERROR("> 2 planes @ stage %d on left\n",
					z_pos);
				rc = -EINVAL;
				goto end;
			}
			left_zpos_cnt++;

		} else {
			if (right_zpos_cnt == 2) {
				DPU_ERROR("> 2 planes @ stage %d on right\n",
					z_pos);
				rc = -EINVAL;
				goto end;
			}
			right_zpos_cnt++;
		}

		pstates[i].dpu_pstate->stage = z_pos + DPU_STAGE_0;
		DPU_DEBUG("%s: zpos %d", dpu_crtc->name, z_pos);
	}

	for (i = 0; i < multirect_count; i++) {
		if (dpu_plane_validate_multirect_v2(&multirect_plane[i])) {
			DPU_ERROR(
			"multirect validation failed for planes (%d - %d)\n",
					multirect_plane[i].r0->plane->base.id,
					multirect_plane[i].r1->plane->base.id);
			rc = -EINVAL;
			goto end;
		}
	}

	rc = dpu_core_perf_crtc_check(crtc, state);
	if (rc) {
		DPU_ERROR("crtc%d failed performance check %d\n",
				crtc->base.id, rc);
		goto end;
	}

	/* validate source split:
	 * use pstates sorted by stage to check planes on same stage
	 * we assume that all pipes are in source split so its valid to compare
	 * without taking into account left/right mixer placement
	 */
	for (i = 1; i < cnt; i++) {
		struct plane_state *prv_pstate, *cur_pstate;
		struct drm_rect left_rect, right_rect;
		int32_t left_pid, right_pid;
		int32_t stage;

		prv_pstate = &pstates[i - 1];
		cur_pstate = &pstates[i];
		if (prv_pstate->stage != cur_pstate->stage)
			continue;

		stage = cur_pstate->stage;

		left_pid = prv_pstate->dpu_pstate->base.plane->base.id;
		left_rect = drm_plane_state_dest(prv_pstate->drm_pstate);

		right_pid = cur_pstate->dpu_pstate->base.plane->base.id;
		right_rect = drm_plane_state_dest(cur_pstate->drm_pstate);

		if (right_rect.x1 < left_rect.x1) {
			swap(left_pid, right_pid);
			swap(left_rect, right_rect);
		}

		/**
		 * - planes are enumerated in pipe-priority order such that
		 *   planes with lower drm_id must be left-most in a shared
		 *   blend-stage when using source split.
		 * - planes in source split must be contiguous in width
		 * - planes in source split must have same dest yoff and height
		 */
		if (right_pid < left_pid) {
			DPU_ERROR(
				"invalid src split cfg. priority mismatch. stage: %d left: %d right: %d\n",
				stage, left_pid, right_pid);
			rc = -EINVAL;
			goto end;
		} else if (right_rect.x1 != drm_rect_width(&left_rect)) {
			DPU_ERROR("non-contiguous coordinates for src split. "
				  "stage: %d left: " DRM_RECT_FMT " right: "
				  DRM_RECT_FMT "\n", stage,
				  DRM_RECT_ARG(&left_rect),
				  DRM_RECT_ARG(&right_rect));
			rc = -EINVAL;
			goto end;
		} else if (left_rect.y1 != right_rect.y1 ||
			   drm_rect_height(&left_rect) != drm_rect_height(&right_rect)) {
			DPU_ERROR("source split at stage: %d. invalid "
				  "yoff/height: left: " DRM_RECT_FMT " right: "
				  DRM_RECT_FMT "\n", stage,
				  DRM_RECT_ARG(&left_rect),
				  DRM_RECT_ARG(&right_rect));
			rc = -EINVAL;
			goto end;
		}
	}

end:
	kfree(pstates);
	return rc;
}

int dpu_crtc_vblank(struct drm_crtc *crtc, bool en)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	struct drm_encoder *enc;

	trace_dpu_crtc_vblank(DRMID(&dpu_crtc->base), en, dpu_crtc);

	/*
	 * Normally we would iterate through encoder_mask in crtc state to find
	 * attached encoders. In this case, we might be disabling vblank _after_
	 * encoder_mask has been cleared.
	 *
	 * Instead, we "assign" a crtc to the encoder in enable and clear it in
	 * disable (which is also after encoder_mask is cleared). So instead of
	 * using encoder mask, we'll ask the encoder to toggle itself iff it's
	 * currently assigned to our crtc.
	 *
	 * Note also that this function cannot be called while crtc is disabled
	 * since we use drm_crtc_vblank_on/off. So we don't need to worry
	 * about the assigned crtcs being inconsistent with the current state
	 * (which means no need to worry about modeset locks).
	 */
	list_for_each_entry(enc, &crtc->dev->mode_config.encoder_list, head) {
		trace_dpu_crtc_vblank_enable(DRMID(crtc), DRMID(enc), en,
					     dpu_crtc);

		dpu_encoder_toggle_vblank_for_crtc(enc, crtc, en);
	}

	return 0;
}

#ifdef CONFIG_DEBUG_FS
static int _dpu_debugfs_status_show(struct seq_file *s, void *data)
{
	struct dpu_crtc *dpu_crtc;
	struct dpu_plane_state *pstate = NULL;
	struct dpu_crtc_mixer *m;

	struct drm_crtc *crtc;
	struct drm_plane *plane;
	struct drm_display_mode *mode;
	struct drm_framebuffer *fb;
	struct drm_plane_state *state;
	struct dpu_crtc_state *cstate;

	int i, out_width;

	dpu_crtc = s->private;
	crtc = &dpu_crtc->base;

	drm_modeset_lock_all(crtc->dev);
	cstate = to_dpu_crtc_state(crtc->state);

	mode = &crtc->state->adjusted_mode;
	out_width = mode->hdisplay / cstate->num_mixers;

	seq_printf(s, "crtc:%d width:%d height:%d\n", crtc->base.id,
				mode->hdisplay, mode->vdisplay);

	seq_puts(s, "\n");

	for (i = 0; i < cstate->num_mixers; ++i) {
		m = &cstate->mixers[i];
		if (!m->hw_lm)
			seq_printf(s, "\tmixer[%d] has no lm\n", i);
		else if (!m->lm_ctl)
			seq_printf(s, "\tmixer[%d] has no ctl\n", i);
		else
			seq_printf(s, "\tmixer:%d ctl:%d width:%d height:%d\n",
				m->hw_lm->idx - LM_0, m->lm_ctl->idx - CTL_0,
				out_width, mode->vdisplay);
	}

	seq_puts(s, "\n");

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		pstate = to_dpu_plane_state(plane->state);
		state = plane->state;

		if (!pstate || !state)
			continue;

		seq_printf(s, "\tplane:%u stage:%d\n", plane->base.id,
			pstate->stage);

		if (plane->state->fb) {
			fb = plane->state->fb;

			seq_printf(s, "\tfb:%d image format:%4.4s wxh:%ux%u ",
				fb->base.id, (char *) &fb->format->format,
				fb->width, fb->height);
			for (i = 0; i < ARRAY_SIZE(fb->format->cpp); ++i)
				seq_printf(s, "cpp[%d]:%u ",
						i, fb->format->cpp[i]);
			seq_puts(s, "\n\t");

			seq_printf(s, "modifier:%8llu ", fb->modifier);
			seq_puts(s, "\n");

			seq_puts(s, "\t");
			for (i = 0; i < ARRAY_SIZE(fb->pitches); i++)
				seq_printf(s, "pitches[%d]:%8u ", i,
							fb->pitches[i]);
			seq_puts(s, "\n");

			seq_puts(s, "\t");
			for (i = 0; i < ARRAY_SIZE(fb->offsets); i++)
				seq_printf(s, "offsets[%d]:%8u ", i,
							fb->offsets[i]);
			seq_puts(s, "\n");
		}

		seq_printf(s, "\tsrc_x:%4d src_y:%4d src_w:%4d src_h:%4d\n",
			state->src_x, state->src_y, state->src_w, state->src_h);

		seq_printf(s, "\tdst x:%4d dst_y:%4d dst_w:%4d dst_h:%4d\n",
			state->crtc_x, state->crtc_y, state->crtc_w,
			state->crtc_h);
		seq_printf(s, "\tmultirect: mode: %d index: %d\n",
			pstate->multirect_mode, pstate->multirect_index);

		seq_puts(s, "\n");
	}
	if (dpu_crtc->vblank_cb_count) {
		ktime_t diff = ktime_sub(ktime_get(), dpu_crtc->vblank_cb_time);
		s64 diff_ms = ktime_to_ms(diff);
		s64 fps = diff_ms ? div_s64(
				dpu_crtc->vblank_cb_count * 1000, diff_ms) : 0;

		seq_printf(s,
			"vblank fps:%lld count:%u total:%llums total_framecount:%llu\n",
				fps, dpu_crtc->vblank_cb_count,
				ktime_to_ms(diff), dpu_crtc->play_count);

		/* reset time & count for next measurement */
		dpu_crtc->vblank_cb_count = 0;
		dpu_crtc->vblank_cb_time = ktime_set(0, 0);
	}

	drm_modeset_unlock_all(crtc->dev);

	return 0;
}

static int _dpu_debugfs_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, _dpu_debugfs_status_show, inode->i_private);
}

#define DEFINE_DPU_DEBUGFS_SEQ_FOPS(__prefix)                          \
static int __prefix ## _open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, __prefix ## _show, inode->i_private);	\
}									\
static const struct file_operations __prefix ## _fops = {		\
	.owner = THIS_MODULE,						\
	.open = __prefix ## _open,					\
	.release = single_release,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
}

static int dpu_crtc_debugfs_state_show(struct seq_file *s, void *v)
{
	struct drm_crtc *crtc = (struct drm_crtc *) s->private;
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);
	int i;

	seq_printf(s, "client type: %d\n", dpu_crtc_get_client_type(crtc));
	seq_printf(s, "intf_mode: %d\n", dpu_crtc_get_intf_mode(crtc));
	seq_printf(s, "core_clk_rate: %llu\n",
			dpu_crtc->cur_perf.core_clk_rate);
	for (i = DPU_CORE_PERF_DATA_BUS_ID_MNOC;
			i < DPU_CORE_PERF_DATA_BUS_ID_MAX; i++) {
		seq_printf(s, "bw_ctl[%d]: %llu\n", i,
				dpu_crtc->cur_perf.bw_ctl[i]);
		seq_printf(s, "max_per_pipe_ib[%d]: %llu\n", i,
				dpu_crtc->cur_perf.max_per_pipe_ib[i]);
	}

	return 0;
}
DEFINE_DPU_DEBUGFS_SEQ_FOPS(dpu_crtc_debugfs_state);

static int _dpu_crtc_init_debugfs(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	static const struct file_operations debugfs_status_fops = {
		.open =		_dpu_debugfs_status_open,
		.read =		seq_read,
		.llseek =	seq_lseek,
		.release =	single_release,
	};

	dpu_crtc->debugfs_root = debugfs_create_dir(dpu_crtc->name,
			crtc->dev->primary->debugfs_root);
	if (!dpu_crtc->debugfs_root)
		return -ENOMEM;

	/* don't error check these */
	debugfs_create_file("status", 0400,
			dpu_crtc->debugfs_root,
			dpu_crtc, &debugfs_status_fops);
	debugfs_create_file("state", 0600,
			dpu_crtc->debugfs_root,
			&dpu_crtc->base,
			&dpu_crtc_debugfs_state_fops);

	return 0;
}
#else
static int _dpu_crtc_init_debugfs(struct drm_crtc *crtc)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static int dpu_crtc_late_register(struct drm_crtc *crtc)
{
	return _dpu_crtc_init_debugfs(crtc);
}

static void dpu_crtc_early_unregister(struct drm_crtc *crtc)
{
	struct dpu_crtc *dpu_crtc = to_dpu_crtc(crtc);

	debugfs_remove_recursive(dpu_crtc->debugfs_root);
}

static const struct drm_crtc_funcs dpu_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = dpu_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = dpu_crtc_reset,
	.atomic_duplicate_state = dpu_crtc_duplicate_state,
	.atomic_destroy_state = dpu_crtc_destroy_state,
	.late_register = dpu_crtc_late_register,
	.early_unregister = dpu_crtc_early_unregister,
};

static const struct drm_crtc_helper_funcs dpu_crtc_helper_funcs = {
	.atomic_disable = dpu_crtc_disable,
	.atomic_enable = dpu_crtc_enable,
	.atomic_check = dpu_crtc_atomic_check,
	.atomic_begin = dpu_crtc_atomic_begin,
	.atomic_flush = dpu_crtc_atomic_flush,
};

/* initialize crtc */
struct drm_crtc *dpu_crtc_init(struct drm_device *dev, struct drm_plane *plane,
				struct drm_plane *cursor)
{
	struct drm_crtc *crtc = NULL;
	struct dpu_crtc *dpu_crtc = NULL;
	struct msm_drm_private *priv = NULL;
	struct dpu_kms *kms = NULL;
	int i;

	priv = dev->dev_private;
	kms = to_dpu_kms(priv->kms);

	dpu_crtc = kzalloc(sizeof(*dpu_crtc), GFP_KERNEL);
	if (!dpu_crtc)
		return ERR_PTR(-ENOMEM);

	crtc = &dpu_crtc->base;
	crtc->dev = dev;

	spin_lock_init(&dpu_crtc->spin_lock);
	atomic_set(&dpu_crtc->frame_pending, 0);

	init_completion(&dpu_crtc->frame_done_comp);

	INIT_LIST_HEAD(&dpu_crtc->frame_event_list);

	for (i = 0; i < ARRAY_SIZE(dpu_crtc->frame_events); i++) {
		INIT_LIST_HEAD(&dpu_crtc->frame_events[i].list);
		list_add(&dpu_crtc->frame_events[i].list,
				&dpu_crtc->frame_event_list);
		kthread_init_work(&dpu_crtc->frame_events[i].work,
				dpu_crtc_frame_event_work);
	}

	drm_crtc_init_with_planes(dev, crtc, plane, cursor, &dpu_crtc_funcs,
				NULL);

	drm_crtc_helper_add(crtc, &dpu_crtc_helper_funcs);

	/* save user friendly CRTC name for later */
	snprintf(dpu_crtc->name, DPU_CRTC_NAME_SIZE, "crtc%u", crtc->base.id);

	/* initialize event handling */
	spin_lock_init(&dpu_crtc->event_lock);

	DPU_DEBUG("%s: successfully initialized crtc\n", dpu_crtc->name);
	return crtc;
}
