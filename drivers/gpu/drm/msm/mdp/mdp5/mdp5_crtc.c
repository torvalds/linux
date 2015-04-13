/*
 * Copyright (c) 2014 The Linux Foundation. All rights reserved.
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

#define SSPP_MAX	(SSPP_RGB3 + 1) /* TODO: Add SSPP_MAX in mdp5.xml.h */

struct mdp5_crtc {
	struct drm_crtc base;
	char name[8];
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

#define PENDING_CURSOR 0x1
#define PENDING_FLIP   0x2
	atomic_t pending;

	/* for unref'ing cursor bo's after scanout completes: */
	struct drm_flip_work unref_cursor_work;

	struct mdp_irq vblank;
	struct mdp_irq err;

	struct {
		/* protect REG_MDP5_LM_CURSOR* registers and cursor scanout_bo*/
		spinlock_t lock;

		/* current cursor being scanned out: */
		struct drm_gem_object *scanout_bo;
		uint32_t width;
		uint32_t height;
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

#define mdp5_lm_get_flush(lm)	mdp_ctl_flush_mask_lm(lm)

static void crtc_flush(struct drm_crtc *crtc, u32 flush_mask)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	DBG("%s: flush=%08x", mdp5_crtc->name, flush_mask);
	mdp5_ctl_commit(mdp5_crtc->ctl, flush_mask);
}

/*
 * flush updates, to make sure hw is updated to new scanout fb,
 * so that we can safely queue unref to current fb (ie. next
 * vblank we know hw is done w/ previous scanout_fb).
 */
static void crtc_flush_all(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_plane *plane;
	uint32_t flush_mask = 0;

	/* we could have already released CTL in the disable path: */
	if (!mdp5_crtc->ctl)
		return;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		flush_mask |= mdp5_plane_get_flush(plane);
	}
	flush_mask |= mdp5_ctl_get_flush(mdp5_crtc->ctl);
	flush_mask |= mdp5_lm_get_flush(mdp5_crtc->lm);

	crtc_flush(crtc, flush_mask);
}

/* if file!=NULL, this is preclose potential cancel-flip path */
static void complete_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	struct drm_plane *plane;
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
			DBG("%s: send event: %p", mdp5_crtc->name, event);
			drm_send_vblank_event(dev, mdp5_crtc->id, event);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		mdp5_plane_complete_flip(plane);
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

static bool mdp5_crtc_mode_fixup(struct drm_crtc *crtc,
		const struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode)
{
	return true;
}

/*
 * blend_setup() - blend all the planes of a CRTC
 *
 * When border is enabled, the border color will ALWAYS be the base layer.
 * Therefore, the first plane (private RGB pipe) will start at STAGE0.
 * If disabled, the first plane starts at STAGE_BASE.
 *
 * Note:
 * Border is not enabled here because the private plane is exactly
 * the CRTC resolution.
 */
static void blend_setup(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	const struct mdp5_cfg_hw *hw_cfg;
	uint32_t lm = mdp5_crtc->lm, blend_cfg = 0;
	unsigned long flags;
#define blender(stage)	((stage) - STAGE_BASE)

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg);

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);

	/* ctl could be released already when we are shutting down: */
	if (!mdp5_crtc->ctl)
		goto out;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		enum mdp_mixer_stage_id stage =
			to_mdp5_plane_state(plane->state)->stage;

		/*
		 * Note: This cannot happen with current implementation but
		 * we need to check this condition once z property is added
		 */
		BUG_ON(stage > hw_cfg->lm.nb_stages);

		/* LM */
		mdp5_write(mdp5_kms,
				REG_MDP5_LM_BLEND_OP_MODE(lm, blender(stage)),
				MDP5_LM_BLEND_OP_MODE_FG_ALPHA(FG_CONST) |
				MDP5_LM_BLEND_OP_MODE_BG_ALPHA(BG_CONST));
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_FG_ALPHA(lm,
				blender(stage)), 0xff);
		mdp5_write(mdp5_kms, REG_MDP5_LM_BLEND_BG_ALPHA(lm,
				blender(stage)), 0x00);
		/* CTL */
		blend_cfg |= mdp_ctl_blend_mask(mdp5_plane_pipe(plane), stage);
		DBG("%s: blending pipe %s on stage=%d", mdp5_crtc->name,
				pipe2name(mdp5_plane_pipe(plane)), stage);
	}

	DBG("%s: lm%d: blend config = 0x%08x", mdp5_crtc->name, lm, blend_cfg);
	mdp5_ctl_blend(mdp5_crtc->ctl, lm, blend_cfg);

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
			mdp5_crtc->name, mode->base.id, mode->name,
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

	DBG("%s", mdp5_crtc->name);

	if (WARN_ON(!mdp5_crtc->enabled))
		return;

	/* set STAGE_UNUSED for all layers */
	mdp5_ctl_blend(mdp5_crtc->ctl, mdp5_crtc->lm, 0x00000000);

	mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->err);
	mdp5_disable(mdp5_kms);

	mdp5_crtc->enabled = false;
}

static void mdp5_crtc_enable(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);

	DBG("%s", mdp5_crtc->name);

	if (WARN_ON(mdp5_crtc->enabled))
		return;

	mdp5_enable(mdp5_kms);
	mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->err);

	crtc_flush_all(crtc);

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

static int mdp5_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_plane *plane;
	struct drm_device *dev = crtc->dev;
	struct plane_state pstates[STAGE3 + 1];
	int cnt = 0, i;

	DBG("%s: check", mdp5_crtc->name);

	/* request a free CTL, if none is already allocated for this CRTC */
	if (state->enable && !mdp5_crtc->ctl) {
		mdp5_crtc->ctl = mdp5_ctlm_request(mdp5_kms->ctlm, crtc);
		if (WARN_ON(!mdp5_crtc->ctl))
			return -EINVAL;
	}

	/* verify that there are not too many planes attached to crtc
	 * and that we don't have conflicting mixer stages:
	 */
	drm_atomic_crtc_state_for_each_plane(plane, state) {
		struct drm_plane_state *pstate;

		if (cnt >= ARRAY_SIZE(pstates)) {
			dev_err(dev->dev, "too many planes!\n");
			return -EINVAL;
		}

		pstate = state->state->plane_states[drm_plane_index(plane)];

		/* plane might not have changed, in which case take
		 * current state:
		 */
		if (!pstate)
			pstate = plane->state;

		pstates[cnt].plane = plane;
		pstates[cnt].state = to_mdp5_plane_state(pstate);

		cnt++;
	}

	sort(pstates, cnt, sizeof(pstates[0]), pstate_cmp, NULL);

	for (i = 0; i < cnt; i++) {
		pstates[i].state->stage = STAGE_BASE + i;
		DBG("%s: assign pipe %s on stage=%d", mdp5_crtc->name,
				pipe2name(mdp5_plane_pipe(pstates[i].plane)),
				pstates[i].state->stage);
	}

	return 0;
}

static void mdp5_crtc_atomic_begin(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	DBG("%s: begin", mdp5_crtc->name);
}

static void mdp5_crtc_atomic_flush(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	DBG("%s: event: %p", mdp5_crtc->name, crtc->state->event);

	WARN_ON(mdp5_crtc->event);

	spin_lock_irqsave(&dev->event_lock, flags);
	mdp5_crtc->event = crtc->state->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	blend_setup(crtc);
	crtc_flush_all(crtc);
	request_pending(crtc, PENDING_FLIP);

	if (mdp5_crtc->ctl && !crtc->state->enable) {
		mdp5_ctl_release(mdp5_crtc->ctl);
		mdp5_crtc->ctl = NULL;
	}
}

static int mdp5_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property, uint64_t val)
{
	// XXX
	return -EINVAL;
}

static int mdp5_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file, uint32_t handle,
		uint32_t width, uint32_t height)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct drm_gem_object *cursor_bo, *old_bo;
	uint32_t blendcfg, cursor_addr, stride;
	int ret, bpp, lm;
	unsigned int depth;
	enum mdp5_cursor_alpha cur_alpha = CURSOR_ALPHA_PER_PIXEL;
	uint32_t flush_mask = mdp_ctl_flush_mask_cursor(0);
	unsigned long flags;

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		dev_err(dev->dev, "bad cursor size: %dx%d\n", width, height);
		return -EINVAL;
	}

	if (NULL == mdp5_crtc->ctl)
		return -EINVAL;

	if (!handle) {
		DBG("Cursor off");
		return mdp5_ctl_set_cursor(mdp5_crtc->ctl, false);
	}

	cursor_bo = drm_gem_object_lookup(dev, file, handle);
	if (!cursor_bo)
		return -ENOENT;

	ret = msm_gem_get_iova(cursor_bo, mdp5_kms->id, &cursor_addr);
	if (ret)
		return -EINVAL;

	lm = mdp5_crtc->lm;
	drm_fb_get_bpp_depth(DRM_FORMAT_ARGB8888, &depth, &bpp);
	stride = width * (bpp >> 3);

	spin_lock_irqsave(&mdp5_crtc->cursor.lock, flags);
	old_bo = mdp5_crtc->cursor.scanout_bo;

	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_STRIDE(lm), stride);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_FORMAT(lm),
			MDP5_LM_CURSOR_FORMAT_FORMAT(CURSOR_FMT_ARGB8888));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_IMG_SIZE(lm),
			MDP5_LM_CURSOR_IMG_SIZE_SRC_H(height) |
			MDP5_LM_CURSOR_IMG_SIZE_SRC_W(width));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_SIZE(lm),
			MDP5_LM_CURSOR_SIZE_ROI_H(height) |
			MDP5_LM_CURSOR_SIZE_ROI_W(width));
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BASE_ADDR(lm), cursor_addr);


	blendcfg = MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_EN;
	blendcfg |= MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_TRANSP_EN;
	blendcfg |= MDP5_LM_CURSOR_BLEND_CONFIG_BLEND_ALPHA_SEL(cur_alpha);
	mdp5_write(mdp5_kms, REG_MDP5_LM_CURSOR_BLEND_CONFIG(lm), blendcfg);

	mdp5_crtc->cursor.scanout_bo = cursor_bo;
	mdp5_crtc->cursor.width = width;
	mdp5_crtc->cursor.height = height;
	spin_unlock_irqrestore(&mdp5_crtc->cursor.lock, flags);

	ret = mdp5_ctl_set_cursor(mdp5_crtc->ctl, true);
	if (ret)
		goto end;

	flush_mask |= mdp5_ctl_get_flush(mdp5_crtc->ctl);
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
	uint32_t xres = crtc->mode.hdisplay;
	uint32_t yres = crtc->mode.vdisplay;
	uint32_t roi_w;
	uint32_t roi_h;
	unsigned long flags;

	x = (x > 0) ? x : 0;
	y = (y > 0) ? y : 0;

	/*
	 * Cursor Region Of Interest (ROI) is a plane read from cursor
	 * buffer to render. The ROI region is determined by the visiblity of
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
	roi_w = min(mdp5_crtc->cursor.width, xres - x);
	roi_h = min(mdp5_crtc->cursor.height, yres - y);

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
	.set_property = mdp5_crtc_set_property,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.cursor_set = mdp5_crtc_cursor_set,
	.cursor_move = mdp5_crtc_cursor_move,
};

static const struct drm_crtc_helper_funcs mdp5_crtc_helper_funcs = {
	.mode_fixup = mdp5_crtc_mode_fixup,
	.mode_set_nofb = mdp5_crtc_mode_set_nofb,
	.prepare = mdp5_crtc_disable,
	.commit = mdp5_crtc_enable,
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

	DBG("%s: error: %08x", mdp5_crtc->name, irqstatus);
}

uint32_t mdp5_crtc_vblank(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	return mdp5_crtc->vblank.irqmask;
}

void mdp5_crtc_cancel_pending_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	DBG("cancel: %p", file);
	complete_flip(crtc, file);
}

/* set interface for routing crtc->encoder: */
void mdp5_crtc_set_intf(struct drm_crtc *crtc, int intf,
		enum mdp5_intf intf_id)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	uint32_t flush_mask = 0;
	uint32_t intf_sel;
	unsigned long flags;

	/* now that we know what irq's we want: */
	mdp5_crtc->err.irqmask = intf2err(intf);
	mdp5_crtc->vblank.irqmask = intf2vblank(intf);
	mdp_irq_update(&mdp5_kms->base);

	spin_lock_irqsave(&mdp5_kms->resource_lock, flags);
	intf_sel = mdp5_read(mdp5_kms, REG_MDP5_DISP_INTF_SEL);

	switch (intf) {
	case 0:
		intf_sel &= ~MDP5_DISP_INTF_SEL_INTF0__MASK;
		intf_sel |= MDP5_DISP_INTF_SEL_INTF0(intf_id);
		break;
	case 1:
		intf_sel &= ~MDP5_DISP_INTF_SEL_INTF1__MASK;
		intf_sel |= MDP5_DISP_INTF_SEL_INTF1(intf_id);
		break;
	case 2:
		intf_sel &= ~MDP5_DISP_INTF_SEL_INTF2__MASK;
		intf_sel |= MDP5_DISP_INTF_SEL_INTF2(intf_id);
		break;
	case 3:
		intf_sel &= ~MDP5_DISP_INTF_SEL_INTF3__MASK;
		intf_sel |= MDP5_DISP_INTF_SEL_INTF3(intf_id);
		break;
	default:
		BUG();
		break;
	}

	mdp5_write(mdp5_kms, REG_MDP5_DISP_INTF_SEL, intf_sel);
	spin_unlock_irqrestore(&mdp5_kms->resource_lock, flags);

	DBG("%s: intf_sel=%08x", mdp5_crtc->name, intf_sel);
	mdp5_ctl_set_intf(mdp5_crtc->ctl, intf);
	flush_mask |= mdp5_ctl_get_flush(mdp5_crtc->ctl);
	flush_mask |= mdp5_lm_get_flush(mdp5_crtc->lm);

	crtc_flush(crtc, flush_mask);
}

int mdp5_crtc_get_lm(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	if (WARN_ON(!crtc))
		return -EINVAL;

	return mdp5_crtc->lm;
}

/* initialize crtc */
struct drm_crtc *mdp5_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id)
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

	mdp5_crtc->vblank.irq = mdp5_crtc_vblank_irq;
	mdp5_crtc->err.irq = mdp5_crtc_err_irq;

	snprintf(mdp5_crtc->name, sizeof(mdp5_crtc->name), "%s:%d",
			pipe2name(mdp5_plane_pipe(plane)), id);

	drm_crtc_init_with_planes(dev, crtc, plane, NULL, &mdp5_crtc_funcs);

	drm_flip_work_init(&mdp5_crtc->unref_cursor_work,
			"unref cursor", unref_cursor_worker);

	drm_crtc_helper_add(crtc, &mdp5_crtc_helper_funcs);
	plane->crtc = crtc;

	mdp5_plane_install_properties(plane, &crtc->base);

	return crtc;
}
