/*
 * Copyright (c) 2014-2015 The Linux Foundation. All rights reserved.
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/sort.h>
#include <drm/drm_mode.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_flip_work.h>

#include "mdp5_kms.h"

#define CURSOR_WIDTH	64
#define CURSOR_HEIGHT	64

struct mdp5_crtc {
	struct drm_crtc base;
	int id;
	bool enabled;

	spinlock_t lm_lock;     /* protect REG_MDP5_LM_* registers */

	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;

	/* Bits have been flushed at the last commit,
	 * used to decide if a vsync has happened since last commit.
	 */
	u32 flushed_mask;

#define PENDING_CURSOR 0x1
#define PENDING_FLIP   0x2
	atomic_t pending;

	/* for unref'ing cursor bo's after scanout completes: */
	struct drm_flip_work unref_cursor_work;

	struct mdp_irq vblank;
	struct mdp_irq err;
	struct mdp_irq pp_done;

	struct completion pp_completion;

	bool lm_cursor_enabled;

	struct {
		/* protect REG_MDP5_LM_CURSOR* registers and cursor scanout_bo*/
		spinlock_t lock;

		/* current cursor being scanned out: */
		struct drm_gem_object *scanout_bo;
		uint64_t iova;
		uint32_t width, height;
		uint32_t x, y;
	} cursor;
};
#define to_mdp5_crtc(x) container_of(x, struct mdp5_crtc, base)

static void mdp5_crtc_restore_cursor(struct drm_crtc *crtc);

static struct mdp5_kms *get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	return to_mdp5_kms(to_mdp_kms(priv->kms));
}

static void request_pending(struct drm_crtc *crtc, uint32_t pending)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	atomic_or(pending, &mdp5_crtc->pending);
	mdp_irq_register(&get_kms(crtc)->base, &mdp5_crtc->vblank);
}

static void request_pp_done_pending(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	reinit_completion(&mdp5_crtc->pp_completion);
}

static u32 crtc_flush(struct drm_crtc *crtc, u32 flush_mask)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_ctl *ctl = mdp5_cstate->ctl;
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	bool start = !mdp5_cstate->defer_start;

	mdp5_cstate->defer_start = false;

	DBG("%s: flush=%08x", crtc->name, flush_mask);

	return mdp5_ctl_commit(ctl, pipeline, flush_mask, start);
}

/*
 * flush updates, to make sure hw is updated to new scanout fb,
 * so that we can safely queue unref to current fb (ie. next
 * vblank we know hw is done w/ previous scanout_fb).
 */
static u32 crtc_flush_all(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_hw_mixer *mixer, *r_mixer;
	struct drm_plane *plane;
	uint32_t flush_mask = 0;

	/* this should not happen: */
	if (WARN_ON(!mdp5_cstate->ctl))
		return 0;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		if (!plane->state->visible)
			continue;
		flush_mask |= mdp5_plane_get_flush(plane);
	}

	mixer = mdp5_cstate->pipeline.mixer;
	flush_mask |= mdp_ctl_flush_mask_lm(mixer->lm);

	r_mixer = mdp5_cstate->pipeline.r_mixer;
	if (r_mixer)
		flush_mask |= mdp_ctl_flush_mask_lm(r_mixer->lm);

	return crtc_flush(crtc, flush_mask);
}

/* if file!=NULL, this is preclose potential cancel-flip path */
static void complete_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_ctl *ctl = mdp5_cstate->ctl;
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = mdp5_crtc->event;
	if (event) {
		mdp5_crtc->event = NULL;
		DBG("%s: send event: %p", crtc->name, event);
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (ctl && !crtc->state->enable) {
		/* set STAGE_UNUSED for all layers */
		mdp5_ctl_blend(ctl, pipeline, NULL, NULL, 0, 0);
		/* XXX: What to do here? */
		/* mdp5_crtc->ctl = NULL; */
	}
}

static void unref_cursor_worker(struct drm_flip_work *work, void *val)
{
	struct mdp5_crtc *mdp5_crtc =
		container_of(work, struct mdp5_crtc, unref_cursor_work);
	struct mdp5_kms *mdp5_kms = get_kms(&mdp5_crtc->base);
	struct msm_kms *kms = &mdp5_kms->base.base;

	msm_gem_put_iova(val, kms->aspace);
	drm_gem_object_put_unlocked(val);
}

static void mdp5_crtc_destroy(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&mdp5_crtc->unref_cursor_work);

	kfree(mdp5_crtc);
}

static inline u32 mdp5_lm_use_fg_alpha_mask(enum mdp_mixer_stage_id stage)
{
	switch (stage) {
	case STAGE0: return MDP5_LM_BLEND_COLOR_OUT_STAGE0_FG_ALPHA;
	case STAGE1: return MDP5_LM_BLEND_COLOR_OUT_STAGE1_FG_ALPHA;
	case STAGE2: return MDP5_LM_BLEND_COLOR_OUT_STAGE2_FG_ALPHA;
	case STAGE3: return MDP5_LM_BLEND_COLOR_OUT_STAGE3_FG_ALPHA;
	case STAGE4: return MDP5_LM_BLEND_COLOR_OUT_STAGE4_FG_ALPHA;
	case STAGE5: return MDP5_LM_BLEND_COLOR_OUT_STAGE5_FG_ALPHA;
	case STAGE6: return MDP5_LM_BLEND_COLOR_OUT_STAGE6_FG_ALPHA;
	default:
		return 0;
	}
}

/*
 * left/right pipe offsets for the stage array used in blend_setup()
 */
#define PIPE_LEFT	0
#define PIPE_RIGHT	1

/*
 * blend_setup() - blend all the planes of a CRTC
 *
 * If no base layer is available, border will be enabled as the base layer.
 * Otherwise all layers will be blended based on their stage calculated
 * in mdp5_crtc_atomic_check.
 */
static void blend_setup(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	const struct mdp5_cfg_hw *hw_cfg;
	struct mdp5_plane_state *pstate, *pstates[STAGE_MAX + 1] = {NULL};
	const struct mdp_format *format;
	struct mdp5_hw_mixer *mixer = pipeline->mixer;
	uint32_t lm = mixer->lm;
	struct mdp5_hw_mixer *r_mixer = pipeline->r_mixer;
	uint32_t r_lm = r_mixer ? r_mixer->lm : 0;
	struct mdp5_ctl *ctl = mdp5_cstate->ctl;
	uint32_t blend_op, fg_alpha, bg_alpha, ctl_blend_flags = 0;
	unsigned long flags;
	enum mdp5_pipe stage[STAGE_MAX + 1][MAX_PIPE_STAGE] = { { SSPP_NONE } };
	enum mdp5_pipe r_stage[STAGE_MAX + 1][MAX_PIPE_STAGE] = { { SSPP_NONE } };
	int i, plane_cnt = 0;
	bool bg_alpha_enabled = false;
	u32 mixer_op_mode = 0;
	u32 val;
#define blender(stage)	((stage) - STAGE0)

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);

	/* ctl could be released already when we are shutting down: */
	/* XXX: Can this happen now? */
	if (!ctl)
		goto out;

	/* Collect all plane information */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		enum mdp5_pipe right_pipe;

		if (!plane->state->visible)
			continue;

		pstate = to_mdp5_plane_state(plane->state);
		pstates[pstate->stage] = pstate;
		stage[pstate->stage][PIPE_LEFT] = mdp5_plane_pipe(plane);
		/*
		 * if we have a right mixer, stage the same pipe as we
		 * have on the left mixer
		 */
		if (r_mixer)
			r_stage[pstate->stage][PIPE_LEFT] =
						mdp5_plane_pipe(plane);
		/*
		 * if we have a right pipe (i.e, the plane comprises of 2
		 * hwpipes, then stage the right pipe on the right side of both
		 * the layer mixers
		 */
		right_pipe = mdp5_plane_right_pipe(plane);
		if (right_pipe) {
			stage[pstate->stage][PIPE_RIGHT] = right_pipe;
			r_stage[pstate->stage][PIPE_RIGHT] = right_pipe;
		}

		plane_cnt++;
	}

	if (!pstates[STAGE_BASE]) {
		ctl_blend_flags |= MDP5_CTL_BLEND_OP_FLAG_BORDER_OUT;
		DBG("Border Color is enabled");
	} else if (plane_cnt) {
		format = to_mdp_format(msm_framebuffer_format(pstates[STAGE_BASE]->base.fb));

		if (format->alpha_enable)
			bg_alpha_enabled = true;
	}

	/* The reset for blending */
	for (i = STAGE0; i <= STAGE_MAX; i++) {
		if (!pstates[i])
			continue;

		format = to_mdp_format(
			msm_framebuffer_format(pstates[i]->base.fb));
		plane = pstates[i]->base.plane;
		blend_op = MDP5_LM_BLEND_OP_MODE_FG_ALPHA(FG_CONST) |
			MDP5_LM_BLEND_OP_MODE_BG_ALPHA(BG_CONST);
		fg_alpha = pstates[i]->alpha;
		bg_alpha = 0xFF - pstates[i]->alpha;

		if (!format->alpha_enable && bg_alpha_enabled)
			mixer_op_mode = 0;
		else
			mixer_op_mode |= mdp5_lm_use_fg_alpha_mask(i);

		DBG("Stage %d fg_alpha %x bg_alpha %x", i, fg_alpha, bg_alpha);

		if (format->alpha_enable && pstates[i]->premultiplied) {
			blend_op = MDP5_LM_BLEND_OP_MODE_FG_ALPHA(FG_CONST) |
				MDP5_LM_BLEND_OP_MODE_BG_ALPHA(FG_PIXEL);
			if (fg_alpha != 0xff) {
				bg_alpha = fg_alpha;
				blend_op |=
					MDP5_LM_BLEND_OP_MODE_BG_MOD_ALPHA |
					MDP5_LM_BLEND_OP_MODE_BG_INV_MOD_ALPHA;
			} else {
				blend_op |= MDP5_LM_BLEND_OP_MODE_BG_INV_ALPHA;
			}
		} else if (format->alpha_enable) {
			blend_op = MDP5_LM_BLEND_OP_MODE_FG_ALPHA(FG_PIXEL) |
				MDP5_LM_BLEND_OP_MODE_BG_ALPHA(FG_PIXEL);
			if (fg_alpha != 0xff) {
				bg_alpha = fg_alpha;
				blend_op |=
				       MDP5_LM_BLEND_OP_MODE_FG_MOD_ALPHA |
				       MDP5_LM_BLEND_OP_MODE_FG_INV_MOD_ALPHA |
				       MDP5_LM_BLEND_OP_MODE_BG_MOD_ALPHA |
				       MDP5_LM_BLEND_OP_MODE_BG_INV_MOD_ALPHA;
			} else {
				blend_op |= MDP5_LM_BLEND_OP_MODE_BG_INV_ALPHA;
			}
		}

		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_OP_MODE(lm,
				blender(i)), blend_op);
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_FG_ALPHA(lm,
				blender(i)), fg_alpha);
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_BG_ALPHA(lm,
				blender(i)), bg_alpha);
		if (r_mixer) {
			mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_OP_MODE(r_lm,
					blender(i)), blend_op);
			mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_FG_ALPHA(r_lm,
					blender(i)), fg_alpha);
			mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_BG_ALPHA(r_lm,
					blender(i)), bg_alpha);
		}
	}

	val = mdp5_read(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(lm));
	mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(lm),
		   val | mixer_op_mode);
	if (r_mixer) {
		val = mdp5_read(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(r_lm));
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(r_lm),
			   val | mixer_op_mode);
	}

	mdp5_ctl_blend(ctl, pipeline, stage, r_stage, plane_cnt,
		       ctl_blend_flags);
out:
	spin_unlock_irqrestore(&mdp5_crtc->lm_lock, flags);
}

static void mdp5_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct mdp5_hw_mixer *mixer = mdp5_cstate->pipeline.mixer;
	struct mdp5_hw_mixer *r_mixer = mdp5_cstate->pipeline.r_mixer;
	uint32_t lm = mixer->lm;
	u32 mixer_width, val;
	unsigned long flags;
	struct drm_display_mode *mode;

	if (WARN_ON(!crtc->state))
		return;

	mode = &crtc->state->adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			crtc->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	mixer_width = mode->hdisplay;
	if (r_mixer)
		mixer_width /= 2;

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_LM_OUT_SIZE(lm),
			MDP5_LM_OUT_SIZE_WIDTH(mixer_width) |
			MDP5_LM_OUT_SIZE_HEIGHT(mode->vdisplay));

	/* Assign mixer to LEFT side in source split mode */
	val = mdp5_read(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(lm));
	val &= ~MDP5_LM_BLEND_COLOR_OUT_SPLIT_LEFT_RIGHT;
	mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(lm), val);

	if (r_mixer) {
		u32 r_lm = r_mixer->lm;

		mdp5_write(mdp5_kms, REG_MDP5_LM_OUT_SIZE(r_lm),
			   MDP5_LM_OUT_SIZE_WIDTH(mixer_width) |
			   MDP5_LM_OUT_SIZE_HEIGHT(mode->vdisplay));

		/* Assign mixer to RIGHT side in source split mode */
		val = mdp5_read(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(r_lm));
		val |= MDP5_LM_BLEND_COLOR_OUT_SPLIT_LEFT_RIGHT;
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(r_lm), val);
	}

	spin_unlock_irqrestore(&mdp5_crtc->lm_lock, flags);
}

static void mdp5_crtc_atomic_disable(struct drm_crtc *crtc,
				     struct drm_crtc_state *old_state)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct device *dev = &mdp5_kms->pdev->dev;

	DBG("%s", crtc->name);

	if (WARN_ON(!mdp5_crtc->enabled))
		return;

	/* Disable/save vblank irq handling before power is disabled */
	drm_crtc_vblank_off(crtc);

	if (mdp5_cstate->cmd_mode)
		mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->pp_done);

	mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->err);
	pm_runtime_put_sync(dev);

	mdp5_crtc->enabled = false;
}

static void mdp5_crtc_atomic_enable(struct drm_crtc *crtc,
				    struct drm_crtc_state *old_state)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct device *dev = &mdp5_kms->pdev->dev;

	DBG("%s", crtc->name);

	if (WARN_ON(mdp5_crtc->enabled))
		return;

	pm_runtime_get_sync(dev);

	if (mdp5_crtc->lm_cursor_enabled) {
		/*
		 * Restore LM cursor state, as it might have been lost
		 * with suspend:
		 */
		if (mdp5_crtc->cursor.iova) {
			unsigned long flags;

			spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
			mdp5_crtc_restore_cursor(crtc);
			spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

			mdp5_ctl_set_cursor(mdp5_cstate->ctl,
					    &mdp5_cstate->pipeline, 0, true);
		} else {
			mdp5_ctl_set_cursor(mdp5_cstate->ctl,
					    &mdp5_cstate->pipeline, 0, false);
		}
	}

	/* Restore vblank irq handling after power is enabled */
	drm_crtc_vblank_on(crtc);

	mdp5_crtc_mode_set_nofb(crtc);

	mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->err);

	if (mdp5_cstate->cmd_mode)
		mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->pp_done);

	mdp5_crtc->enabled = true;
}

int mdp5_crtc_setup_pipeline(struct drm_crtc *crtc,
			     struct drm_crtc_state *new_crtc_state,
			     bool need_right_mixer)
{
	struct mdp5_crtc_state *mdp5_cstate =
			to_mdp5_crtc_state(new_crtc_state);
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	struct mdp5_interface *intf;
	bool new_mixer = false;

	new_mixer = !pipeline->mixer;

	if ((need_right_mixer && !pipeline->r_mixer) ||
	    (!need_right_mixer && pipeline->r_mixer))
		new_mixer = true;

	if (new_mixer) {
		struct mdp5_hw_mixer *old_mixer = pipeline->mixer;
		struct mdp5_hw_mixer *old_r_mixer = pipeline->r_mixer;
		u32 caps;
		int ret;

		caps = MDP_LM_CAP_DISPLAY;
		if (need_right_mixer)
			caps |= MDP_LM_CAP_PAIR;

		ret = mdp5_mixer_assign(new_crtc_state->state, crtc, caps,
					&pipeline->mixer, need_right_mixer ?
					&pipeline->r_mixer : NULL);
		if (ret)
			return ret;

		mdp5_mixer_release(new_crtc_state->state, old_mixer);
		if (old_r_mixer) {
			mdp5_mixer_release(new_crtc_state->state, old_r_mixer);
			if (!need_right_mixer)
				pipeline->r_mixer = NULL;
		}
	}

	/*
	 * these should have been already set up in the encoder's atomic
	 * check (called by drm_atomic_helper_check_modeset)
	 */
	intf = pipeline->intf;

	mdp5_cstate->err_irqmask = intf2err(intf->num);
	mdp5_cstate->vblank_irqmask = intf2vblank(pipeline->mixer, intf);

	if ((intf->type == INTF_DSI) &&
	    (intf->mode == MDP5_INTF_DSI_MODE_COMMAND)) {
		mdp5_cstate->pp_done_irqmask = lm2ppdone(pipeline->mixer);
		mdp5_cstate->cmd_mode = true;
	} else {
		mdp5_cstate->pp_done_irqmask = 0;
		mdp5_cstate->cmd_mode = false;
	}

	return 0;
}

struct plane_state {
	struct drm_plane *plane;
	struct mdp5_plane_state *state;
};

static int pstate_cmp(const void *a, const void *b)
{
	struct plane_state *pa = (struct plane_state *)a;
	struct plane_state *pb = (struct plane_state *)b;
	return pa->state->zpos - pb->state->zpos;
}

/* is there a helper for this? */
static bool is_fullscreen(struct drm_crtc_state *cstate,
		struct drm_plane_state *pstate)
{
	return (pstate->crtc_x <= 0) && (pstate->crtc_y <= 0) &&
		((pstate->crtc_x + pstate->crtc_w) >= cstate->mode.hdisplay) &&
		((pstate->crtc_y + pstate->crtc_h) >= cstate->mode.vdisplay);
}

static enum mdp_mixer_stage_id get_start_stage(struct drm_crtc *crtc,
					struct drm_crtc_state *new_crtc_state,
					struct drm_plane_state *bpstate)
{
	struct mdp5_crtc_state *mdp5_cstate =
			to_mdp5_crtc_state(new_crtc_state);

	/*
	 * if we're in source split mode, it's mandatory to have
	 * border out on the base stage
	 */
	if (mdp5_cstate->pipeline.r_mixer)
		return STAGE0;

	/* if the bottom-most layer is not fullscreen, we need to use
	 * it for solid-color:
	 */
	if (!is_fullscreen(new_crtc_state, bpstate))
		return STAGE0;

	return STAGE_BASE;
}

static int mdp5_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	struct drm_device *dev = crtc->dev;
	struct plane_state pstates[STAGE_MAX + 1];
	const struct mdp5_cfg_hw *hw_cfg;
	const struct drm_plane_state *pstate;
	const struct drm_display_mode *mode = &state->adjusted_mode;
	bool cursor_plane = false;
	bool need_right_mixer = false;
	int cnt = 0, i;
	int ret;
	enum mdp_mixer_stage_id start;

	DBG("%s: check", crtc->name);

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		if (!pstate->visible)
			continue;

		pstates[cnt].plane = plane;
		pstates[cnt].state = to_mdp5_plane_state(pstate);

		/*
		 * if any plane on this crtc uses 2 hwpipes, then we need
		 * the crtc to have a right hwmixer.
		 */
		if (pstates[cnt].state->r_hwpipe)
			need_right_mixer = true;
		cnt++;

		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor_plane = true;
	}

	/* bail out early if there aren't any planes */
	if (!cnt)
		return 0;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	/*
	 * we need a right hwmixer if the mode's width is greater than a single
	 * LM's max width
	 */
	if (mode->hdisplay > hw_cfg->lm.max_width)
		need_right_mixer = true;

	ret = mdp5_crtc_setup_pipeline(crtc, state, need_right_mixer);
	if (ret) {
		dev_err(dev->dev, "couldn't assign mixers %d\n", ret);
		return ret;
	}

	/* assign a stage based on sorted zpos property */
	sort(pstates, cnt, sizeof(pstates[0]), pstate_cmp, NULL);

	/* trigger a warning if cursor isn't the highest zorder */
	WARN_ON(cursor_plane &&
		(pstates[cnt - 1].plane->type != DRM_PLANE_TYPE_CURSOR));

	start = get_start_stage(crtc, state, &pstates[0].state->base);

	/* verify that there are not too many planes attached to crtc
	 * and that we don't have conflicting mixer stages:
	 */
	if ((cnt + start - 1) >= hw_cfg->lm.nb_stages) {
		dev_err(dev->dev, "too many planes! cnt=%d, start stage=%d\n",
			cnt, start);
		return -EINVAL;
	}

	for (i = 0; i < cnt; i++) {
		if (cursor_plane && (i == (cnt - 1)))
			pstates[i].state->stage = hw_cfg->lm.nb_stages;
		else
			pstates[i].state->stage = start + i;
		DBG("%s: assign pipe %s on stage=%d", crtc->name,
				pstates[i].plane->name,
				pstates[i].state->stage);
	}

	return 0;
}

static void mdp5_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	DBG("%s: begin", crtc->name);
}

static void mdp5_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	DBG("%s: event: %p", crtc->name, crtc->state->event);

	WARN_ON(mdp5_crtc->event);

	spin_lock_irqsave(&dev->event_lock, flags);
	mdp5_crtc->event = crtc->state->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	/*
	 * If no CTL has been allocated in mdp5_crtc_atomic_check(),
	 * it means we are trying to flush a CRTC whose state is disabled:
	 * nothing else needs to be done.
	 */
	/* XXX: Can this happen now ? */
	if (unlikely(!mdp5_cstate->ctl))
		return;

	blend_setup(crtc);

	/* PP_DONE irq is only used by command mode for now.
	 * It is better to request pending before FLUSH and START trigger
	 * to make sure no pp_done irq missed.
	 * This is safe because no pp_done will happen before SW trigger
	 * in command mode.
	 */
	if (mdp5_cstate->cmd_mode)
		request_pp_done_pending(crtc);

	mdp5_crtc->flushed_mask = crtc_flush_all(crtc);

	/* XXX are we leaking out state here? */
	mdp5_crtc->vblank.irqmask = mdp5_cstate->vblank_irqmask;
	mdp5_crtc->err.irqmask = mdp5_cstate->err_irqmask;
	mdp5_crtc->pp_done.irqmask = mdp5_cstate->pp_done_irqmask;

	request_pending(crtc, PENDING_FLIP);
}

static void get_roi(struct drm_crtc *crtc, uint32_t *roi_w, uint32_t *roi_h)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	uint32_t xres = crtc->mode.hdisplay;
	uint32_t yres = crtc->mode.vdisplay;

	/*
	 * Cursor Region Of Interest (ROI) is a plane read from cursor
	 * buffer to render. The ROI region is determined by the visibility of
	 * the cursor point. In the default Cursor image the cursor point will
	 * be at the top left of the cursor image, unless it is specified
	 * otherwise using hotspot feature.
	 *
	 * If the cursor point reaches the right (xres - x < cursor.width) or
	 * bottom (yres - y < cursor.height) boundary of the screen, then ROI
	 * width and ROI height need to be evaluated to crop the cursor image
	 * accordingly.
	 * (xres-x) will be new cursor width when x > (xres - cursor.width)
	 * (yres-y) will be new cursor height when y > (yres - cursor.height)
	 */
	*roi_w = min(mdp5_crtc->cursor.width, xres -
			mdp5_crtc->cursor.x);
	*roi_h = min(mdp5_crtc->cursor.height, yres -
			mdp5_crtc->cursor.y);
}

static void mdp5_crtc_restore_cursor(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	const enum mdp5_cursor_alpha cur_alpha = CURSOR_ALPHA_PER_PIXEL;
	uint32_t blendcfg, stride;
	uint32_t x, y, width, height;
	uint32_t roi_w, roi_h;
	int lm;

	assert_spin_locked(&mdp5_crtc->cursor.lock);

	lm = mdp5_cstate->pipeline.mixer->lm;

	x = mdp5_crtc->cursor.x;
	y = mdp5_crtc->cursor.y;
	width = mdp5_crtc->cursor.width;
	height = mdp5_crtc->cursor.height;

	stride = width * drm_format_plane_cpp(DRM_FORMAT_ARGB8888, 0);

	get_roi(crtc, &roi_w, &roi_h);

	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_STRIDE(lm), stride);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_FORMAT(lm),
			MDP5_LM_CURSOR_FORMAT_FORMAT(CURSOR_FMT_ARGB8888));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_IMG_SIZE(lm),
			MDP5_LM_CURSOR_IMG_SIZE_SRC_H(height) |
			MDP5_LM_CURSOR_IMG_SIZE_SRC_W(width));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_SIZE(lm),
			MDP5_LM_CURSOR_SIZE_ROI_H(roi_h) |
			MDP5_LM_CURSOR_SIZE_ROI_W(roi_w));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_START_XY(lm),
			MDP5_LM_CURSOR_START_XY_Y_START(y) |
			MDP5_LM_CURSOR_START_XY_X_START(x));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BASE_ADDR(lm),
			mdp5_crtc->cursor.iova);

	blendcfg = MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_EN;
	blendcfg |= MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_ALPHA_SEL(cur_alpha);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BLEND_CONFIG(lm), blendcfg);
}

static int mdp5_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file, uint32_t handle,
		uint32_t width, uint32_t height)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	struct drm_device *dev = crtc->dev;
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct platform_device *pdev = mdp5_kms->pdev;
	struct msm_kms *kms = &mdp5_kms->base.base;
	struct drm_gem_object *cursor_bo, *old_bo = NULL;
	struct mdp5_ctl *ctl;
	int ret;
	uint32_t flush_mask = mdp_ctl_flush_mask_cursor(0);
	bool cursor_enable = true;
	unsigned long flags;

	if (!mdp5_crtc->lm_cursor_enabled) {
		dev_warn(dev->dev,
			 "cursor_set is deprecated with cursor planes\n");
		return -EINVAL;
	}

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		dev_err(dev->dev, "bad cursor size: %dx%d\n", width, height);
		return -EINVAL;
	}

	ctl = mdp5_cstate->ctl;
	if (!ctl)
		return -EINVAL;

	/* don't support LM cursors when we we have source split enabled */
	if (mdp5_cstate->pipeline.r_mixer)
		return -EINVAL;

	if (!handle) {
		DBG("Cursor off");
		cursor_enable = false;
		mdp5_crtc->cursor.iova = 0;
		pm_runtime_get_sync(&pdev->dev);
		goto set_cursor;
	}

	cursor_bo = drm_gem_object_lookup(file, handle);
	if (!cursor_bo)
		return -ENOENT;

	ret = msm_gem_get_iova(cursor_bo, kms->aspace,
			&mdp5_crtc->cursor.iova);
	if (ret)
		return -EINVAL;

	pm_runtime_get_sync(&pdev->dev);

	spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
	old_bo = mdp5_crtc->cursor.scanout_bo;

	mdp5_crtc->cursor.scanout_bo = cursor_bo;
	mdp5_crtc->cursor.width = width;
	mdp5_crtc->cursor.height = height;

	mdp5_crtc_restore_cursor(crtc);

	spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

set_cursor:
	ret = mdp5_ctl_set_cursor(ctl, pipeline, 0, cursor_enable);
	if (ret) {
		dev_err(dev->dev, "failed to %sable cursor: %d\n",
				cursor_enable ? "en" : "dis", ret);
		goto end;
	}

	crtc_flush(crtc, flush_mask);

end:
	pm_runtime_put_sync(&pdev->dev);
	if (old_bo) {
		drm_flip_work_queue(&mdp5_crtc->unref_cursor_work, old_bo);
		/* enable vblank to complete cursor work: */
		request_pending(crtc, PENDING_CURSOR);
	}
	return ret;
}

static int mdp5_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	uint32_t flush_mask = mdp_ctl_flush_mask_cursor(0);
	struct drm_device *dev = crtc->dev;
	uint32_t roi_w;
	uint32_t roi_h;
	unsigned long flags;

	if (!mdp5_crtc->lm_cursor_enabled) {
		dev_warn(dev->dev,
			 "cursor_move is deprecated with cursor planes\n");
		return -EINVAL;
	}

	/* don't support LM cursors when we we have source split enabled */
	if (mdp5_cstate->pipeline.r_mixer)
		return -EINVAL;

	/* In case the CRTC is disabled, just drop the cursor update */
	if (unlikely(!crtc->state->enable))
		return 0;

	mdp5_crtc->cursor.x = x = max(x, 0);
	mdp5_crtc->cursor.y = y = max(y, 0);

	get_roi(crtc, &roi_w, &roi_h);

	pm_runtime_get_sync(&mdp5_kms->pdev->dev);

	spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
	mdp5_crtc_restore_cursor(crtc);
	spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

	crtc_flush(crtc, flush_mask);

	pm_runtime_put_sync(&mdp5_kms->pdev->dev);

	return 0;
}

static void
mdp5_crtc_atomic_print_state(struct drm_printer *p,
			     const struct drm_crtc_state *state)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(state);
	struct mdp5_pipeline *pipeline = &mdp5_cstate->pipeline;
	struct mdp5_kms *mdp5_kms = get_kms(state->crtc);

	if (WARN_ON(!pipeline))
		return;

	if (mdp5_cstate->ctl)
		drm_printf(p, "\tctl=%d\n", mdp5_ctl_get_ctl_id(mdp5_cstate->ctl));

	drm_printf(p, "\thwmixer=%s\n", pipeline->mixer ?
			pipeline->mixer->name : "(null)");

	if (mdp5_kms->caps & MDP_CAP_SRC_SPLIT)
		drm_printf(p, "\tright hwmixer=%s\n", pipeline->r_mixer ?
			   pipeline->r_mixer->name : "(null)");

	drm_printf(p, "\tcmd_mode=%d\n", mdp5_cstate->cmd_mode);
}

static void mdp5_crtc_reset(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate;

	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state(crtc->state);
		kfree(to_mdp5_crtc_state(crtc->state));
	}

	mdp5_cstate = kzalloc(sizeof(*mdp5_cstate), GFP_KERNEL);

	if (mdp5_cstate) {
		mdp5_cstate->base.crtc = crtc;
		crtc->state = &mdp5_cstate->base;
	}
}

static struct drm_crtc_state *
mdp5_crtc_duplicate_state(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate;

	if (WARN_ON(!crtc->state))
		return NULL;

	mdp5_cstate = kmemdup(to_mdp5_crtc_state(crtc->state),
			      sizeof(*mdp5_cstate), GFP_KERNEL);
	if (!mdp5_cstate)
		return NULL;

	__drm_atomic_helper_crtc_duplicate_state(crtc, &mdp5_cstate->base);

	return &mdp5_cstate->base;
}

static void mdp5_crtc_destroy_state(struct drm_crtc *crtc, struct drm_crtc_state *state)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(state);

	__drm_atomic_helper_crtc_destroy_state(state);

	kfree(mdp5_cstate);
}

static const struct drm_crtc_funcs mdp5_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = mdp5_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.reset = mdp5_crtc_reset,
	.atomic_duplicate_state = mdp5_crtc_duplicate_state,
	.atomic_destroy_state = mdp5_crtc_destroy_state,
	.cursor_set = mdp5_crtc_cursor_set,
	.cursor_move = mdp5_crtc_cursor_move,
	.atomic_print_state = mdp5_crtc_atomic_print_state,
};

static const struct drm_crtc_helper_funcs mdp5_crtc_helper_funcs = {
	.mode_set_nofb = mdp5_crtc_mode_set_nofb,
	.atomic_check = mdp5_crtc_atomic_check,
	.atomic_begin = mdp5_crtc_atomic_begin,
	.atomic_flush = mdp5_crtc_atomic_flush,
	.atomic_enable = mdp5_crtc_atomic_enable,
	.atomic_disable = mdp5_crtc_atomic_disable,
};

static void mdp5_crtc_vblank_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp5_crtc *mdp5_crtc = container_of(irq, struct mdp5_crtc, vblank);
	struct drm_crtc *crtc = &mdp5_crtc->base;
	struct msm_drm_private *priv = crtc->dev->dev_private;
	unsigned pending;

	mdp_irq_unregister(&get_kms(crtc)->base, &mdp5_crtc->vblank);

	pending = atomic_xchg(&mdp5_crtc->pending, 0);

	if (pending & PENDING_FLIP) {
		complete_flip(crtc, NULL);
	}

	if (pending & PENDING_CURSOR)
		drm_flip_work_commit(&mdp5_crtc->unref_cursor_work, priv->wq);
}

static void mdp5_crtc_err_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp5_crtc *mdp5_crtc = container_of(irq, struct mdp5_crtc, err);

	DBG("%s: error: %08x", mdp5_crtc->base.name, irqstatus);
}

static void mdp5_crtc_pp_done_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp5_crtc *mdp5_crtc = container_of(irq, struct mdp5_crtc,
								pp_done);

	complete(&mdp5_crtc->pp_completion);
}

static void mdp5_crtc_wait_for_pp_done(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	int ret;

	ret = wait_for_completion_timeout(&mdp5_crtc->pp_completion,
						msecs_to_jiffies(50));
	if (ret == 0)
		dev_warn(dev->dev, "pp done time out, lm=%d\n",
			 mdp5_cstate->pipeline.mixer->lm);
}

static void mdp5_crtc_wait_for_flush_done(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_ctl *ctl = mdp5_cstate->ctl;
	int ret;

	/* Should not call this function if crtc is disabled. */
	if (!ctl)
		return;

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		return;

	ret = wait_event_timeout(dev->vblank[drm_crtc_index(crtc)].queue,
		((mdp5_ctl_get_commit_status(ctl) &
		mdp5_crtc->flushed_mask) == 0),
		msecs_to_jiffies(50));
	if (ret <= 0)
		dev_warn(dev->dev, "vblank time out, crtc=%d\n", mdp5_crtc->id);

	mdp5_crtc->flushed_mask = 0;

	drm_crtc_vblank_put(crtc);
}

uint32_t mdp5_crtc_vblank(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	return mdp5_crtc->vblank.irqmask;
}

void mdp5_crtc_set_pipeline(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);

	/* should this be done elsewhere ? */
	mdp_irq_update(&mdp5_kms->base);

	mdp5_ctl_set_pipeline(mdp5_cstate->ctl, &mdp5_cstate->pipeline);
}

struct mdp5_ctl *mdp5_crtc_get_ctl(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);

	return mdp5_cstate->ctl;
}

struct mdp5_hw_mixer *mdp5_crtc_get_mixer(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate;

	if (WARN_ON(!crtc))
		return ERR_PTR(-EINVAL);

	mdp5_cstate = to_mdp5_crtc_state(crtc->state);

	return WARN_ON(!mdp5_cstate->pipeline.mixer) ?
		ERR_PTR(-EINVAL) : mdp5_cstate->pipeline.mixer;
}

struct mdp5_pipeline *mdp5_crtc_get_pipeline(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate;

	if (WARN_ON(!crtc))
		return ERR_PTR(-EINVAL);

	mdp5_cstate = to_mdp5_crtc_state(crtc->state);

	return &mdp5_cstate->pipeline;
}

void mdp5_crtc_wait_for_commit_done(struct drm_crtc *crtc)
{
	struct mdp5_crtc_state *mdp5_cstate = to_mdp5_crtc_state(crtc->state);

	if (mdp5_cstate->cmd_mode)
		mdp5_crtc_wait_for_pp_done(crtc);
	else
		mdp5_crtc_wait_for_flush_done(crtc);
}

/* initialize crtc */
struct drm_crtc *mdp5_crtc_init(struct drm_device *dev,
				struct drm_plane *plane,
				struct drm_plane *cursor_plane, int id)
{
	struct drm_crtc *crtc = NULL;
	struct mdp5_crtc *mdp5_crtc;

	mdp5_crtc = kzalloc(sizeof(*mdp5_crtc), GFP_KERNEL);
	if (!mdp5_crtc)
		return ERR_PTR(-ENOMEM);

	crtc = &mdp5_crtc->base;

	mdp5_crtc->id = id;

	spin_lock_init(&mdp5_crtc->lm_lock);
	spin_lock_init(&mdp5_crtc->cursor.lock);
	init_completion(&mdp5_crtc->pp_completion);

	mdp5_crtc->vblank.irq = mdp5_crtc_vblank_irq;
	mdp5_crtc->err.irq = mdp5_crtc_err_irq;
	mdp5_crtc->pp_done.irq = mdp5_crtc_pp_done_irq;

	mdp5_crtc->lm_cursor_enabled = cursor_plane ? false : true;

	drm_crtc_init_with_planes(dev, crtc, plane, cursor_plane,
				  &mdp5_crtc_funcs, NULL);

	drm_flip_work_init(&mdp5_crtc->unref_cursor_work,
			"unref cursor", unref_cursor_worker);

	drm_crtc_helper_add(crtc, &mdp5_crtc_helper_funcs);
	plane->crtc = crtc;

	return crtc;
}
