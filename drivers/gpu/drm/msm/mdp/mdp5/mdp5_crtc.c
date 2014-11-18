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

#include <drm/drm_mode.h>
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_flip_work.h"

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
	void *ctl;

	/* if there is a pending flip, these will be non-null: */
	struct drm_pending_vblank_event *event;
	struct msm_fence_cb pageflip_cb;

#define PENDING_CURSOR 0x1
#define PENDING_FLIP   0x2
	atomic_t pending;

	/* the fb that we logically (from PoV of KMS API) hold a ref
	 * to.  Which we may not yet be scanning out (we may still
	 * be scanning out previous in case of page_flip while waiting
	 * for gpu rendering to complete:
	 */
	struct drm_framebuffer *fb;

	/* the fb that we currently hold a scanout ref to: */
	struct drm_framebuffer *scanout_fb;

	/* for unref'ing framebuffers after scanout completes: */
	struct drm_flip_work unref_fb_work;

	struct mdp_irq vblank;
	struct mdp_irq err;
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

	for_each_plane_on_crtc(crtc, plane) {
		flush_mask |= mdp5_plane_get_flush(plane);
	}
	flush_mask |= mdp5_ctl_get_flush(mdp5_crtc->ctl);
	flush_mask |= mdp5_lm_get_flush(mdp5_crtc->lm);

	crtc_flush(crtc, flush_mask);
}

static void update_fb(struct drm_crtc *crtc, struct drm_framebuffer *new_fb)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_framebuffer *old_fb = mdp5_crtc->fb;

	/* grab reference to incoming scanout fb: */
	drm_framebuffer_reference(new_fb);
	mdp5_crtc->base.primary->fb = new_fb;
	mdp5_crtc->fb = new_fb;

	if (old_fb)
		drm_flip_work_queue(&mdp5_crtc->unref_fb_work, old_fb);
}

/* unlike update_fb(), take a ref to the new scanout fb *before* updating
 * plane, then call this.  Needed to ensure we don't unref the buffer that
 * is actually still being scanned out.
 *
 * Note that this whole thing goes away with atomic.. since we can defer
 * calling into driver until rendering is done.
 */
static void update_scanout(struct drm_crtc *crtc, struct drm_framebuffer *fb)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	if (mdp5_crtc->scanout_fb)
		drm_flip_work_queue(&mdp5_crtc->unref_fb_work,
				mdp5_crtc->scanout_fb);

	mdp5_crtc->scanout_fb = fb;

	/* enable vblank to complete flip: */
	request_pending(crtc, PENDING_FLIP);
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
			drm_send_vblank_event(dev, mdp5_crtc->id, event);
		}
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	for_each_plane_on_crtc(crtc, plane)
		mdp5_plane_complete_flip(plane);
}

static void pageflip_cb(struct msm_fence_cb *cb)
{
	struct mdp5_crtc *mdp5_crtc =
		container_of(cb, struct mdp5_crtc, pageflip_cb);
	struct drm_crtc *crtc = &mdp5_crtc->base;
	struct drm_framebuffer *fb = mdp5_crtc->fb;

	if (!fb)
		return;

	drm_framebuffer_reference(fb);
	mdp5_plane_set_scanout(crtc->primary, fb);
	update_scanout(crtc, fb);
	crtc_flush_all(crtc);
}

static void unref_fb_worker(struct drm_flip_work *work, void *val)
{
	struct mdp5_crtc *mdp5_crtc =
		container_of(work, struct mdp5_crtc, unref_fb_work);
	struct drm_device *dev = mdp5_crtc->base.dev;

	mutex_lock(&dev->mode_config.mutex);
	drm_framebuffer_unreference(val);
	mutex_unlock(&dev->mode_config.mutex);
}

static void mdp5_crtc_destroy(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&mdp5_crtc->unref_fb_work);

	kfree(mdp5_crtc);
}

static void mdp5_crtc_dpms(struct drm_crtc *crtc, int mode)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	bool enabled = (mode == DRM_MODE_DPMS_ON);

	DBG("%s: mode=%d", mdp5_crtc->name, mode);

	if (enabled != mdp5_crtc->enabled) {
		if (enabled) {
			mdp5_enable(mdp5_kms);
			mdp_irq_register(&mdp5_kms->base, &mdp5_crtc->err);
		} else {
			mdp_irq_unregister(&mdp5_kms->base, &mdp5_crtc->err);
			mdp5_disable(mdp5_kms);
		}
		mdp5_crtc->enabled = enabled;
	}
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
	enum mdp_mixer_stage_id stage;
	unsigned long flags;
#define blender(stage)	((stage) - STAGE_BASE)

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg_priv);

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);

	/* ctl could be released already when we are shutting down: */
	if (!mdp5_crtc->ctl)
		goto out;

	for_each_plane_on_crtc(crtc, plane) {
		struct mdp5_overlay_info *overlay;

		overlay = mdp5_plane_get_overlay_info(plane);
		stage = overlay->zorder;

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

static int mdp5_crtc_mode_set(struct drm_crtc *crtc,
		struct drm_display_mode *mode,
		struct drm_display_mode *adjusted_mode,
		int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	unsigned long flags;
	int ret;

	mode = adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mdp5_crtc->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	/* request a free CTL, if none is already allocated for this CRTC */
	if (!mdp5_crtc->ctl) {
		mdp5_crtc->ctl = mdp5_ctl_request(mdp5_kms->ctl_priv, crtc);
		if (!mdp5_crtc->ctl)
			return -EBUSY;
	}

	/* grab extra ref for update_scanout() */
	drm_framebuffer_reference(crtc->primary->fb);

	ret = mdp5_plane_mode_set(crtc->primary, crtc, crtc->primary->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
	if (ret) {
		drm_framebuffer_unreference(crtc->primary->fb);
		dev_err(crtc->dev->dev, "%s: failed to set mode on plane: %d\n",
				mdp5_crtc->name, ret);
		return ret;
	}

	spin_lock_irqsave(&mdp5_crtc->lm_lock, flags);
	mdp5_write(mdp5_kms, REG_MDP5_LM_OUT_SIZE(mdp5_crtc->lm),
			MDP5_LM_OUT_SIZE_WIDTH(mode->hdisplay) |
			MDP5_LM_OUT_SIZE_HEIGHT(mode->vdisplay));
	spin_unlock_irqrestore(&mdp5_crtc->lm_lock, flags);

	update_fb(crtc, crtc->primary->fb);
	update_scanout(crtc, crtc->primary->fb);
	/* crtc_flush_all(crtc) will be called in _commit callback */

	return 0;
}

static void mdp5_crtc_prepare(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	DBG("%s", mdp5_crtc->name);
	/* make sure we hold a ref to mdp clks while setting up mode: */
	mdp5_enable(get_kms(crtc));
	mdp5_crtc_dpms(crtc, DRM_MODE_DPMS_OFF);
}

static void mdp5_crtc_commit(struct drm_crtc *crtc)
{
	mdp5_crtc_dpms(crtc, DRM_MODE_DPMS_ON);
	crtc_flush_all(crtc);
	/* drop the ref to mdp clk's that we got in prepare: */
	mdp5_disable(get_kms(crtc));
}

static int mdp5_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
		struct drm_framebuffer *old_fb)
{
	struct drm_plane *plane = crtc->primary;
	struct drm_display_mode *mode = &crtc->mode;
	int ret;

	/* grab extra ref for update_scanout() */
	drm_framebuffer_reference(crtc->primary->fb);

	ret = mdp5_plane_mode_set(plane, crtc, crtc->primary->fb,
			0, 0, mode->hdisplay, mode->vdisplay,
			x << 16, y << 16,
			mode->hdisplay << 16, mode->vdisplay << 16);
	if (ret) {
		drm_framebuffer_unreference(crtc->primary->fb);
		return ret;
	}

	update_fb(crtc, crtc->primary->fb);
	update_scanout(crtc, crtc->primary->fb);
	crtc_flush_all(crtc);

	return 0;
}

static void mdp5_crtc_load_lut(struct drm_crtc *crtc)
{
}

static void mdp5_crtc_disable(struct drm_crtc *crtc)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	DBG("%s", mdp5_crtc->name);

	if (mdp5_crtc->ctl) {
		mdp5_ctl_release(mdp5_crtc->ctl);
		mdp5_crtc->ctl = NULL;
	}
}


static int mdp5_crtc_page_flip(struct drm_crtc *crtc,
		struct drm_framebuffer *new_fb,
		struct drm_pending_vblank_event *event,
		uint32_t page_flip_flags)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *obj;
	unsigned long flags;

	if (mdp5_crtc->event) {
		dev_err(dev->dev, "already pending flip!\n");
		return -EBUSY;
	}

	obj = msm_framebuffer_bo(new_fb, 0);

	spin_lock_irqsave(&dev->event_lock, flags);
	mdp5_crtc->event = event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	update_fb(crtc, new_fb);

	return msm_gem_queue_inactive_cb(obj, &mdp5_crtc->pageflip_cb);
}

static int mdp5_crtc_set_property(struct drm_crtc *crtc,
		struct drm_property *property, uint64_t val)
{
	// XXX
	return -EINVAL;
}

static const struct drm_crtc_funcs mdp5_crtc_funcs = {
	.set_config = drm_crtc_helper_set_config,
	.destroy = mdp5_crtc_destroy,
	.page_flip = mdp5_crtc_page_flip,
	.set_property = mdp5_crtc_set_property,
};

static const struct drm_crtc_helper_funcs mdp5_crtc_helper_funcs = {
	.dpms = mdp5_crtc_dpms,
	.mode_fixup = mdp5_crtc_mode_fixup,
	.mode_set = mdp5_crtc_mode_set,
	.prepare = mdp5_crtc_prepare,
	.commit = mdp5_crtc_commit,
	.mode_set_base = mdp5_crtc_mode_set_base,
	.load_lut = mdp5_crtc_load_lut,
	.disable = mdp5_crtc_disable,
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
		drm_flip_work_commit(&mdp5_crtc->unref_fb_work, priv->wq);
	}
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

	/* when called from modeset_init(), skip the rest until later: */
	if (!mdp5_kms)
		return;

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

static int count_planes(struct drm_crtc *crtc)
{
	struct drm_plane *plane;
	int cnt = 0;
	for_each_plane_on_crtc(crtc, plane)
		cnt++;
	return cnt;
}

static void set_attach(struct drm_crtc *crtc, enum mdp5_pipe pipe_id,
		struct drm_plane *plane)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);

	if (plane)
		plane->crtc = crtc;

	DBG("%s: %d planes attached", mdp5_crtc->name, count_planes(crtc));

	blend_setup(crtc);
	if (mdp5_crtc->enabled)
		crtc_flush_all(crtc);
}

int mdp5_crtc_attach(struct drm_crtc *crtc, struct drm_plane *plane)
{
	struct mdp5_crtc *mdp5_crtc = to_mdp5_crtc(crtc);
	struct mdp5_kms *mdp5_kms = get_kms(crtc);
	struct device *dev = crtc->dev->dev;
	const struct mdp5_cfg_hw *hw_cfg;
	bool private_plane = (plane == crtc->primary);
	struct mdp5_overlay_info overlay_info;
	enum mdp_mixer_stage_id stage = STAGE_BASE;
	int max_nb_planes;

	hw_cfg = mdp5_cfg_get_hw_config(mdp5_kms->cfg_priv);
	max_nb_planes = hw_cfg->lm.nb_stages;

	if (count_planes(crtc) >= max_nb_planes) {
		dev_err(dev, "%s: max # of planes (%d) reached\n",
				mdp5_crtc->name, max_nb_planes);
		return -EBUSY;
	}

	/*
	 * Set default z-ordering depending on the type of plane
	 * private -> lower stage
	 * public  -> topmost stage
	 *
	 * TODO: add a property to give userspace an API to change this...
	 * (will come in a subsequent patch)
	 */
	if (private_plane) {
		stage = STAGE_BASE;
	} else {
		struct drm_plane *attached_plane;
		for_each_plane_on_crtc(crtc, attached_plane) {
			struct mdp5_overlay_info *overlay;

			if (!attached_plane)
				continue;
			overlay = mdp5_plane_get_overlay_info(attached_plane);
			stage = max(stage, overlay->zorder);
		}
		stage++;
	}
	overlay_info.zorder = stage;
	mdp5_plane_set_overlay_info(plane, &overlay_info);

	DBG("%s: %s plane %s set to stage %d by default", mdp5_crtc->name,
			private_plane ? "private" : "public",
			pipe2name(mdp5_plane_pipe(plane)), overlay_info.zorder);

	set_attach(crtc, mdp5_plane_pipe(plane), plane);

	return 0;
}

void mdp5_crtc_detach(struct drm_crtc *crtc, struct drm_plane *plane)
{
	/* don't actually detatch our primary plane: */
	if (crtc->primary == plane)
		return;
	set_attach(crtc, mdp5_plane_pipe(plane), NULL);
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

	mdp5_crtc->vblank.irq = mdp5_crtc_vblank_irq;
	mdp5_crtc->err.irq = mdp5_crtc_err_irq;

	snprintf(mdp5_crtc->name, sizeof(mdp5_crtc->name), "%s:%d",
			pipe2name(mdp5_plane_pipe(plane)), id);

	drm_flip_work_init(&mdp5_crtc->unref_fb_work,
			"unref fb", unref_fb_worker);

	INIT_FENCE_CB(&mdp5_crtc->pageflip_cb, pageflip_cb);

	drm_crtc_init_with_planes(dev, crtc, plane, NULL, &mdp5_crtc_funcs);
	drm_crtc_helper_add(crtc, &mdp5_crtc_helper_funcs);
	plane->crtc = crtc;

	mdp5_plane_install_properties(plane, &crtc->base);

	return crtc;
}
