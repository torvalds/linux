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

#include "mdp5_kms.h"

#include <linux/sort.h>
#include <drm/drm_mode.h>
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_flip_work.h"

#define CURSOR_WIDTH	64
#define CURSOR_HEIGHT	64

struct mdp5_crtc {
	struct drm_crtc base;
	int id;
	bool enabled;

	/* layer mixer used for this CRTC (+ its lock): */
#define GET_LM_ID(crtc_id)	((crtc_id == 3) ? 5 : crtc_id)
	int lm;
	spinlock_t lm_lock;	/* protect REG_MDP5_LM_* registers */

	/* CTL used for this CRTC: */
	struct mdp5_ctl *ctl;

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

	bool cmd_mode;

	struct {
		/* protect REG_MDP5_LM_CURSOR* registers and cursor scanout_bo*/
		spinlock_t lock;

		/* current cursor being scanned out: */
		struct drm_gem_object *scanout_bo;
		uint32_t width, height;
		uint32_t x, y;
	} cursor;
};
#define to_mdp5_crtc(x) container_of(x, struct mdp5_crtc, base)

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
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	DBG("%s: flush=%08x", crtc->name, flush_mask);
	return mdp5_ctl_commit(mdp5_crtc->ctl, flush_mask);
}

/*
 * flush updates, to make sure hw is updated to new scanout fb,
 * so that we can safely queue unref to current fb (ie. next
 * vblank we know hw is done w/ previous scanout_fb).
 */
static u32 crtc_flush_all(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_plane *plane;
	uint32_t flush_mask = 0;

	/* this should not happen: */
	if (WARN_ON(!mdp5_crtc->ctl))
		return 0;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		flush_mask |= mdp5_plane_get_flush(plane);
	}

	flush_mask |= mdp_ctl_flush_mask_lm(mdp5_crtc->lm);

	return crtc_flush(crtc, flush_mask);
}

/* if file!=NULL, this is preclose potential cancel-flip path */
static void complete_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = mdp5_crtc->event;
	if (event) {
		/* if regular vblank case (!file) or if cancel-flip from
		 * preclose on file that requested flip, then send the
		 * event:
		 */
		if (!file || (event->base.file_priv == file)) {
			mdp5_crtc->event = NULL;
			DBG("%s: send event: %p", crtc->name, event);
			drm_crtc_send_vblank_event(crtc, event);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	if (mdp5_crtc->ctl && !crtc->state->enable) {
		/* set STAGE_UNUSED for all layers */
		mdp5_ctl_blend(mdp5_crtc->ctl, NULL, 0, 0);
		mdp5_crtc->ctl = NULL;
	}
}

static void unref_cursor_worker(struct drm_flip_work *work, void *val)
{
	struct mdp5_crtc *mdp5_crtc =
		container_of(work, struct mdp5_crtc, unref_cursor_work);
	struct mdp5_kms *mdp5_kms = get_kms(&mdp5_crtc->base);

	msm_gem_put_iova(val, mdp5_kms->id);
	drm_gem_object_unreference_unlocked(val);
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
 * blend_setup() - blend all the planes of a CRTC
 *
 * If no base layer is available, border will be enabled as the base layer.
 * Otherwise all layers will be blended based on their stage calculated
 * in mdp5_crtc_atomic_check.
 */
static void blend_setup(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	const struct mdp5_cfg_hw *hw_cfg;
	struct mdp5_plane_state *pstate, *pstates[STAGE_MAX + 1] = {NULL};
	const struct mdp_format *format;
	uint32_t lm = mdp5_crtc->lm;
	uint32_t blend_op, fg_alpha, bg_alpha, ctl_blend_flags = 0;
	unsigned long flags;
	enum mdp5_pipe stage[STAGE_MAX + 1] = { SSPP_NONE };
	int i, plane_cnt = 0;
	bool bg_alpha_enabled = false;
	u32 mixer_op_mode = 0;
#define blender(stage)	((stage) - STAGE0)

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);

	/* ctl could be released already when we are shutting down: */
	if (!mdp5_crtc->ctl)
		goto out;

	/* Collect all plane information */
	drm_atomic_crtc_for_each_plane(plane, crtc) {
		pstate = to_mdp5_plane_state(plane->state);
		pstates[pstate->stage] = pstate;
		stage[pstate->stage] = mdp5_plane_pipe(plane);
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
	}

	mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_COLOR_OUT(lm), mixer_op_mode);

	mdp5_ctl_blend(mdp5_crtc->ctl, stage, plane_cnt, ctl_blend_flags);

out:
	spin_unlock_irqrestore(&mdp5_crtc->lm_lock, flags);
}

static void mdp5_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
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

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_LM_OUT_SIZE(mdp5_crtc->lm),
			MDP5_LM_OUT_SIZE_WIDTH(mode->hdisplay) |
			MDP5_LM_OUT_SIZE_HEIGHT(mode->vdisplay));
	spin_unlock_irqrestore(&mdp5_crtc->lm_lock, flags);
}

static void mdp5_crtc_disable(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);

	DBG("%s", crtc->name);

	if (WARN_ON(!mdp5_crtc->enabled))
		return;

	if (mdp5_crtc->cmd_mode)
		mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->pp_done);

	mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->err);
	mdp5_disable(mdp5_kms);

	mdp5_crtc->enabled = false;
}

static void mdp5_crtc_enable(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);

	DBG("%s", crtc->name);

	if (WARN_ON(mdp5_crtc->enabled))
		return;

	mdp5_enable(mdp5_kms);
	mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->err);

	if (mdp5_crtc->cmd_mode)
		mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->pp_done);

	mdp5_crtc->enabled = true;
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

static int mdp5_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	struct drm_device *dev = crtc->dev;
	struct plane_state pstates[STAGE_MAX + 1];
	const struct mdp5_cfg_hw *hw_cfg;
	const struct drm_plane_state *pstate;
	bool cursor_plane = false;
	int cnt = 0, base = 0, i;

	DBG("%s: check", crtc->name);

	drm_atomic_crtc_state_for_each_plane_state(plane, pstate, state) {
		pstates[cnt].plane = plane;
		pstates[cnt].state = to_mdp5_plane_state(pstate);

		cnt++;

		if (plane->type == DRM_PLANE_TYPE_CURSOR)
			cursor_plane = true;
	}

	/* assign a stage based on sorted zpos property */
	sort(pstates, cnt, sizeof(pstates[0]), pstate_cmp, NULL);

	/* if the bottom-most layer is not fullscreen, we need to use
	 * it for solid-color:
	 */
	if ((cnt > 0) && !is_fullscreen(state, &pstates[0].state->base))
		base++;

	/* trigger a warning if cursor isn't the highest zorder */
	WARN_ON(cursor_plane &&
		(pstates[cnt - 1].plane->type != DRM_PLANE_TYPE_CURSOR));

	/* verify that there are not too many planes attached to crtc
	 * and that we don't have conflicting mixer stages:
	 */
	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	if ((cnt + base) >= hw_cfg->lm.nb_stages) {
		dev_err(dev->dev, "too many planes! cnt=%d, base=%d\n", cnt, base);
		return -EINVAL;
	}

	for (i = 0; i < cnt; i++) {
		if (cursor_plane && (i == (cnt - 1)))
			pstates[i].state->stage = hw_cfg->lm.nb_stages;
		else
			pstates[i].state->stage = STAGE_BASE + i + base;
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
	if (unlikely(!mdp5_crtc->ctl))
		return;

	blend_setup(crtc);

	/* PP_DONE irq is only used by command mode for now.
	 * It is better to request pending before FLUSH and START trigger
	 * to make sure no pp_done irq missed.
	 * This is safe because no pp_done will happen before SW trigger
	 * in command mode.
	 */
	if (mdp5_crtc->cmd_mode)
		request_pp_done_pending(crtc);

	mdp5_crtc->flushed_mask = crtc_flush_all(crtc);

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

static int mdp5_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file, uint32_t handle,
		uint32_t width, uint32_t height)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_gem_object *cursor_bo, *old_bo = NULL;
	uint32_t blendcfg, stride;
	uint64_t cursor_addr;
	int ret, lm;
	enum mdp5_cursor_alpha cur_alpha = CURSOR_ALPHA_PER_PIXEL;
	uint32_t flush_mask = mdp_ctl_flush_mask_cursor(0);
	uint32_t roi_w, roi_h;
	bool cursor_enable = true;
	unsigned long flags;

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		dev_err(dev->dev, "bad cursor size: %dx%d\n", width, height);
		return -EINVAL;
	}

	if (NULL == mdp5_crtc->ctl)
		return -EINVAL;

	if (!handle) {
		DBG("Cursor off");
		cursor_enable = false;
		goto set_cursor;
	}

	cursor_bo = drm_gem_object_lookup(file, handle);
	if (!cursor_bo)
		return -ENOENT;

	ret = msm_gem_get_iova(cursor_bo, mdp5_kms->id, &cursor_addr);
	if (ret)
		return -EINVAL;

	lm = mdp5_crtc->lm;
	stride = width * drm_format_plane_cpp(DRM_FORMAT_ARGB8888, 0);

	spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
	old_bo = mdp5_crtc->cursor.scanout_bo;

	mdp5_crtc->cursor.scanout_bo = cursor_bo;
	mdp5_crtc->cursor.width = width;
	mdp5_crtc->cursor.height = height;

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
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BASE_ADDR(lm), cursor_addr);

	blendcfg = MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_EN;
	blendcfg |= MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_ALPHA_SEL(cur_alpha);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BLEND_CONFIG(lm), blendcfg);

	spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

set_cursor:
	ret = mdp5_ctl_set_cursor(mdp5_crtc->ctl, 0, cursor_enable);
	if (ret) {
		dev_err(dev->dev, "failed to %sable cursor: %d\n",
				cursor_enable ? "en" : "dis", ret);
		goto end;
	}

	crtc_flush(crtc, flush_mask);

end:
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
	uint32_t flush_mask = mdp_ctl_flush_mask_cursor(0);
	uint32_t roi_w;
	uint32_t roi_h;
	unsigned long flags;

	/* In case the CRTC is disabled, just drop the cursor update */
	if (unlikely(!crtc->state->enable))
		return 0;

	mdp5_crtc->cursor.x = x = max(x, 0);
	mdp5_crtc->cursor.y = y = max(y, 0);

	get_roi(crtc, &roi_w, &roi_h);

	spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_SIZE(mdp5_crtc->lm),
			MDP5_LM_CURSOR_SIZE_ROI_H(roi_h) |
			MDP5_LM_CURSOR_SIZE_ROI_W(roi_w));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_START_XY(mdp5_crtc->lm),
			MDP5_LM_CURSOR_START_XY_Y_START(y) |
			MDP5_LM_CURSOR_START_XY_X_START(x));
	spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

	crtc_flush(crtc, flush_mask);

	return 0;
}

static const struct drm_crtc_funcs mdp5_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = mdp5_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.set_property = drm_atomic_helper_crtc_set_property,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.cursor_set = mdp5_crtc_cursor_set,
	.cursor_move = mdp5_crtc_cursor_move,
};

static const struct drm_crtc_funcs mdp5_crtc_no_lm_cursor_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = mdp5_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.set_property = drm_atomic_helper_crtc_set_property,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs mdp5_crtc_helper_funcs = {
	.mode_set_nofb = mdp5_crtc_mode_set_nofb,
	.disable = mdp5_crtc_disable,
	.enable = mdp5_crtc_enable,
	.atomic_check = mdp5_crtc_atomic_check,
	.atomic_begin = mdp5_crtc_atomic_begin,
	.atomic_flush = mdp5_crtc_atomic_flush,
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
	int ret;

	ret = wait_for_completion_timeout(&mdp5_crtc->pp_completion,
						msecs_to_jiffies(50));
	if (ret == 0)
		dev_warn(dev->dev, "pp done time out, lm=%d\n", mdp5_crtc->lm);
}

static void mdp5_crtc_wait_for_flush_done(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	int ret;

	/* Should not call this function if crtc is disabled. */
	if (!mdp5_crtc->ctl)
		return;

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		return;

	ret = wait_event_timeout(dev->vblank[drm_crtc_index(crtc)].queue,
		((mdp5_ctl_get_commit_status(mdp5_crtc->ctl) &
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

void mdp5_crtc_set_pipeline(struct drm_crtc *crtc,
		struct mdp5_interface *intf, struct mdp5_ctl *ctl)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	int lm = mdp5_crtc_get_lm(crtc);

	/* now that we know what irq's we want: */
	mdp5_crtc->err.irqmask = intf2err(intf->num);
	mdp5_crtc->vblank.irqmask = intf2vblank(lm, intf);

	if ((intf->type == INTF_DSI) &&
		(intf->mode == MDP5_INTF_DSI_MODE_COMMAND)) {
		mdp5_crtc->pp_done.irqmask = lm2ppdone(lm);
		mdp5_crtc->pp_done.irq = mdp5_crtc_pp_done_irq;
		mdp5_crtc->cmd_mode = true;
	} else {
		mdp5_crtc->pp_done.irqmask = 0;
		mdp5_crtc->pp_done.irq = NULL;
		mdp5_crtc->cmd_mode = false;
	}

	mdp_irq_update(&mdp5_kms->base);

	mdp5_crtc->ctl = ctl;
	mdp5_ctl_set_pipeline(ctl, intf, lm);
}

int mdp5_crtc_get_lm(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	return WARN_ON(!crtc) ? -EINVAL : mdp5_crtc->lm;
}

void mdp5_crtc_wait_for_commit_done(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	if (mdp5_crtc->cmd_mode)
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
	mdp5_crtc->lm = GET_LM_ID(id);

	spin_lock_init(&mdp5_crtc->lm_lock);
	spin_lock_init(&mdp5_crtc->cursor.lock);
	init_completion(&mdp5_crtc->pp_completion);

	mdp5_crtc->vblank.irq = mdp5_crtc_vblank_irq;
	mdp5_crtc->err.irq = mdp5_crtc_err_irq;

	if (cursor_plane)
		drm_crtc_init_with_planes(dev, crtc, plane, cursor_plane,
					  &mdp5_crtc_no_lm_cursor_funcs, NULL);
	else
		drm_crtc_init_with_planes(dev, crtc, plane, NULL,
					  &mdp5_crtc_funcs, NULL);

	drm_flip_work_init(&mdp5_crtc->unref_cursor_work,
			"unref cursor", unref_cursor_worker);

	drm_crtc_helper_add(crtc, &mdp5_crtc_helper_funcs);
	plane->crtc = crtc;

	return crtc;
}
