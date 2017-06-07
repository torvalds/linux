/*
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

#include "mdp4_kms.h"

#include <drm/drm_mode.h>
#include "drm_crtc.h"
#include "drm_crtc_helper.h"
#include "drm_flip_work.h"

struct mdp4_crtc {
	struct drm_crtc base;
	char name[8];
	int id;
	int ovlp;
	enum mdp4_dma dma;
	bool enabled;

	/* which mixer/encoder we route output to: */
	int mixer;

	struct {
		spinlock_t lock;
		bool stale;
		uint32_t width, height;
		uint32_t x, y;

		/* next cursor to scan-out: */
		uint32_t next_iova;
		struct drm_gem_object *next_bo;

		/* current cursor being scanned out: */
		struct drm_gem_object *scanout_bo;
	} cursor;


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
};
#define to_mdp4_crtc(x) container_of(x, struct mdp4_crtc, base)

static struct mdp4_kms *get_kms(struct drm_crtc *crtc)
{
	struct msm_drm_private *priv = crtc->dev->dev_private;
	return to_mdp4_kms(to_mdp_kms(priv->kms));
}

static void request_pending(struct drm_crtc *crtc, uint32_t pending)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);

	atomic_or(pending, &mdp4_crtc->pending);
	mdp_irq_register(&get_kms(crtc)->base, &mdp4_crtc->vblank);
}

static void crtc_flush(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	struct drm_plane *plane;
	uint32_t flush = 0;

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		enum mdp4_pipe pipe_id = mdp4_plane_pipe(plane);
		flush |= pipe2flush(pipe_id);
	}

	flush |= ovlp2flush(mdp4_crtc->ovlp);

	DBG("%s: flush=%08x", mdp4_crtc->name, flush);

	mdp4_crtc->flushed_mask = flush;

	mdp4_write(mdp4_kms, REG_MDP4_OVERLAY_FLUSH, flush);
}

/* if file!=NULL, this is preclose potential cancel-flip path */
static void complete_flip(struct drm_crtc *crtc, struct drm_file *file)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_pending_vblank_event *event;
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	event = mdp4_crtc->event;
	if (event) {
		mdp4_crtc->event = NULL;
		DBG("%s: send event: %p", mdp4_crtc->name, event);
		drm_crtc_send_vblank_event(crtc, event);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static void unref_cursor_worker(struct drm_flip_work *work, void *val)
{
	struct mdp4_crtc *mdp4_crtc =
		container_of(work, struct mdp4_crtc, unref_cursor_work);
	struct mdp4_kms *mdp4_kms = get_kms(&mdp4_crtc->base);

	msm_gem_put_iova(val, mdp4_kms->id);
	drm_gem_object_unreference_unlocked(val);
}

static void mdp4_crtc_destroy(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);

	drm_crtc_cleanup(crtc);
	drm_flip_work_cleanup(&mdp4_crtc->unref_cursor_work);

	kfree(mdp4_crtc);
}

/* statically (for now) map planes to mixer stage (z-order): */
static const int idxs[] = {
		[VG1]  = 1,
		[VG2]  = 2,
		[RGB1] = 0,
		[RGB2] = 0,
		[RGB3] = 0,
		[VG3]  = 3,
		[VG4]  = 4,

};

/* setup mixer config, for which we need to consider all crtc's and
 * the planes attached to them
 *
 * TODO may possibly need some extra locking here
 */
static void setup_mixer(struct mdp4_kms *mdp4_kms)
{
	struct drm_mode_config *config = &mdp4_kms->dev->mode_config;
	struct drm_crtc *crtc;
	uint32_t mixer_cfg = 0;
	static const enum mdp_mixer_stage_id stages[] = {
			STAGE_BASE, STAGE0, STAGE1, STAGE2, STAGE3,
	};

	list_for_each_entry(crtc, &config->crtc_list, head) {
		struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
		struct drm_plane *plane;

		drm_atomic_crtc_for_each_plane(plane, crtc) {
			enum mdp4_pipe pipe_id = mdp4_plane_pipe(plane);
			int idx = idxs[pipe_id];
			mixer_cfg = mixercfg(mixer_cfg, mdp4_crtc->mixer,
					pipe_id, stages[idx]);
		}
	}

	mdp4_write(mdp4_kms, REG_MDP4_LAYERMIXER_IN_CFG, mixer_cfg);
}

static void blend_setup(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	struct drm_plane *plane;
	int i, ovlp = mdp4_crtc->ovlp;
	bool alpha[4]= { false, false, false, false };

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_LOW0(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_LOW1(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_HIGH0(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_TRANSP_HIGH1(ovlp), 0);

	drm_atomic_crtc_for_each_plane(plane, crtc) {
		enum mdp4_pipe pipe_id = mdp4_plane_pipe(plane);
		int idx = idxs[pipe_id];
		if (idx > 0) {
			const struct mdp_format *format =
					to_mdp_format(msm_framebuffer_format(plane->fb));
			alpha[idx-1] = format->alpha_enable;
		}
	}

	for (i = 0; i < 4; i++) {
		uint32_t op;

		if (alpha[i]) {
			op = MDP4_OVLP_STAGE_OP_FG_ALPHA(FG_PIXEL) |
					MDP4_OVLP_STAGE_OP_BG_ALPHA(FG_PIXEL) |
					MDP4_OVLP_STAGE_OP_BG_INV_ALPHA;
		} else {
			op = MDP4_OVLP_STAGE_OP_FG_ALPHA(FG_CONST) |
					MDP4_OVLP_STAGE_OP_BG_ALPHA(BG_CONST);
		}

		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_FG_ALPHA(ovlp, i), 0xff);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_BG_ALPHA(ovlp, i), 0x00);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_OP(ovlp, i), op);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_CO3(ovlp, i), 1);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_LOW0(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_LOW1(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_HIGH0(ovlp, i), 0);
		mdp4_write(mdp4_kms, REG_MDP4_OVLP_STAGE_TRANSP_HIGH1(ovlp, i), 0);
	}

	setup_mixer(mdp4_kms);
}

static void mdp4_crtc_mode_set_nofb(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	enum mdp4_dma dma = mdp4_crtc->dma;
	int ovlp = mdp4_crtc->ovlp;
	struct drm_display_mode *mode;

	if (WARN_ON(!crtc->state))
		return;

	mode = &crtc->state->adjusted_mode;

	DBG("%s: set mode: %d:\"%s\" %d %d %d %d %d %d %d %d %d %d 0x%x 0x%x",
			mdp4_crtc->name, mode->base.id, mode->name,
			mode->vrefresh, mode->clock,
			mode->hdisplay, mode->hsync_start,
			mode->hsync_end, mode->htotal,
			mode->vdisplay, mode->vsync_start,
			mode->vsync_end, mode->vtotal,
			mode->type, mode->flags);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_SIZE(dma),
			MDP4_DMA_SRC_SIZE_WIDTH(mode->hdisplay) |
			MDP4_DMA_SRC_SIZE_HEIGHT(mode->vdisplay));

	/* take data from pipe: */
	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_BASE(dma), 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_SRC_STRIDE(dma), 0);
	mdp4_write(mdp4_kms, REG_MDP4_DMA_DST_SIZE(dma),
			MDP4_DMA_DST_SIZE_WIDTH(0) |
			MDP4_DMA_DST_SIZE_HEIGHT(0));

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_BASE(ovlp), 0);
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_SIZE(ovlp),
			MDP4_OVLP_SIZE_WIDTH(mode->hdisplay) |
			MDP4_OVLP_SIZE_HEIGHT(mode->vdisplay));
	mdp4_write(mdp4_kms, REG_MDP4_OVLP_STRIDE(ovlp), 0);

	mdp4_write(mdp4_kms, REG_MDP4_OVLP_CFG(ovlp), 1);

	if (dma == DMA_E) {
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(0), 0x00ff0000);
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(1), 0x00ff0000);
		mdp4_write(mdp4_kms, REG_MDP4_DMA_E_QUANT(2), 0x00ff0000);
	}
}

static void mdp4_crtc_disable(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);

	DBG("%s", mdp4_crtc->name);

	if (WARN_ON(!mdp4_crtc->enabled))
		return;

	mdp_irq_unregister(&mdp4_kms->base, &mdp4_crtc->err);
	mdp4_disable(mdp4_kms);

	mdp4_crtc->enabled = false;
}

static void mdp4_crtc_enable(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);

	DBG("%s", mdp4_crtc->name);

	if (WARN_ON(mdp4_crtc->enabled))
		return;

	mdp4_enable(mdp4_kms);
	mdp_irq_register(&mdp4_kms->base, &mdp4_crtc->err);

	crtc_flush(crtc);

	mdp4_crtc->enabled = true;
}

static int mdp4_crtc_atomic_check(struct drm_crtc *crtc,
		struct drm_crtc_state *state)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	DBG("%s: check", mdp4_crtc->name);
	// TODO anything else to check?
	return 0;
}

static void mdp4_crtc_atomic_begin(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	DBG("%s: begin", mdp4_crtc->name);
}

static void mdp4_crtc_atomic_flush(struct drm_crtc *crtc,
				   struct drm_crtc_state *old_crtc_state)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	unsigned long flags;

	DBG("%s: event: %p", mdp4_crtc->name, crtc->state->event);

	WARN_ON(mdp4_crtc->event);

	spin_lock_irqsave(&dev->event_lock, flags);
	mdp4_crtc->event = crtc->state->event;
	spin_unlock_irqrestore(&dev->event_lock, flags);

	blend_setup(crtc);
	crtc_flush(crtc);
	request_pending(crtc, PENDING_FLIP);
}

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

/* called from IRQ to update cursor related registers (if needed).  The
 * cursor registers, other than x/y position, appear not to be double
 * buffered, and changing them other than from vblank seems to trigger
 * underflow.
 */
static void update_cursor(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	enum mdp4_dma dma = mdp4_crtc->dma;
	unsigned long flags;

	spin_lock_irqsave(&mdp4_crtc->cursor.lock, flags);
	if (mdp4_crtc->cursor.stale) {
		struct drm_gem_object *next_bo = mdp4_crtc->cursor.next_bo;
		struct drm_gem_object *prev_bo = mdp4_crtc->cursor.scanout_bo;
		uint64_t iova = mdp4_crtc->cursor.next_iova;

		if (next_bo) {
			/* take a obj ref + iova ref when we start scanning out: */
			drm_gem_object_reference(next_bo);
			msm_gem_get_iova_locked(next_bo, mdp4_kms->id, &iova);

			/* enable cursor: */
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_SIZE(dma),
					MDP4_DMA_CURSOR_SIZE_WIDTH(mdp4_crtc->cursor.width) |
					MDP4_DMA_CURSOR_SIZE_HEIGHT(mdp4_crtc->cursor.height));
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BASE(dma), iova);
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BLEND_CONFIG(dma),
					MDP4_DMA_CURSOR_BLEND_CONFIG_FORMAT(CURSOR_ARGB) |
					MDP4_DMA_CURSOR_BLEND_CONFIG_CURSOR_EN);
		} else {
			/* disable cursor: */
			mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_BASE(dma),
					mdp4_kms->blank_cursor_iova);
		}

		/* and drop the iova ref + obj rev when done scanning out: */
		if (prev_bo)
			drm_flip_work_queue(&mdp4_crtc->unref_cursor_work, prev_bo);

		mdp4_crtc->cursor.scanout_bo = next_bo;
		mdp4_crtc->cursor.stale = false;
	}

	mdp4_write(mdp4_kms, REG_MDP4_DMA_CURSOR_POS(dma),
			MDP4_DMA_CURSOR_POS_X(mdp4_crtc->cursor.x) |
			MDP4_DMA_CURSOR_POS_Y(mdp4_crtc->cursor.y));

	spin_unlock_irqrestore(&mdp4_crtc->cursor.lock, flags);
}

static int mdp4_crtc_cursor_set(struct drm_crtc *crtc,
		struct drm_file *file_priv, uint32_t handle,
		uint32_t width, uint32_t height)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *cursor_bo, *old_bo;
	unsigned long flags;
	uint64_t iova;
	int ret;

	if ((width > CURSOR_WIDTH) || (height > CURSOR_HEIGHT)) {
		dev_err(dev->dev, "bad cursor size: %dx%d\n", width, height);
		return -EINVAL;
	}

	if (handle) {
		cursor_bo = drm_gem_object_lookup(file_priv, handle);
		if (!cursor_bo)
			return -ENOENT;
	} else {
		cursor_bo = NULL;
	}

	if (cursor_bo) {
		ret = msm_gem_get_iova(cursor_bo, mdp4_kms->id, &iova);
		if (ret)
			goto fail;
	} else {
		iova = 0;
	}

	spin_lock_irqsave(&mdp4_crtc->cursor.lock, flags);
	old_bo = mdp4_crtc->cursor.next_bo;
	mdp4_crtc->cursor.next_bo   = cursor_bo;
	mdp4_crtc->cursor.next_iova = iova;
	mdp4_crtc->cursor.width     = width;
	mdp4_crtc->cursor.height    = height;
	mdp4_crtc->cursor.stale     = true;
	spin_unlock_irqrestore(&mdp4_crtc->cursor.lock, flags);

	if (old_bo) {
		/* drop our previous reference: */
		drm_flip_work_queue(&mdp4_crtc->unref_cursor_work, old_bo);
	}

	request_pending(crtc, PENDING_CURSOR);

	return 0;

fail:
	drm_gem_object_unreference_unlocked(cursor_bo);
	return ret;
}

static int mdp4_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	unsigned long flags;

	spin_lock_irqsave(&mdp4_crtc->cursor.lock, flags);
	mdp4_crtc->cursor.x = x;
	mdp4_crtc->cursor.y = y;
	spin_unlock_irqrestore(&mdp4_crtc->cursor.lock, flags);

	crtc_flush(crtc);
	request_pending(crtc, PENDING_CURSOR);

	return 0;
}

static const struct drm_crtc_funcs mdp4_crtc_funcs = {
	.set_config = drm_atomic_helper_set_config,
	.destroy = mdp4_crtc_destroy,
	.page_flip = drm_atomic_helper_page_flip,
	.set_property = drm_atomic_helper_crtc_set_property,
	.cursor_set = mdp4_crtc_cursor_set,
	.cursor_move = mdp4_crtc_cursor_move,
	.reset = drm_atomic_helper_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
};

static const struct drm_crtc_helper_funcs mdp4_crtc_helper_funcs = {
	.mode_set_nofb = mdp4_crtc_mode_set_nofb,
	.disable = mdp4_crtc_disable,
	.enable = mdp4_crtc_enable,
	.atomic_check = mdp4_crtc_atomic_check,
	.atomic_begin = mdp4_crtc_atomic_begin,
	.atomic_flush = mdp4_crtc_atomic_flush,
};

static void mdp4_crtc_vblank_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp4_crtc *mdp4_crtc = container_of(irq, struct mdp4_crtc, vblank);
	struct drm_crtc *crtc = &mdp4_crtc->base;
	struct msm_drm_private *priv = crtc->dev->dev_private;
	unsigned pending;

	mdp_irq_unregister(&get_kms(crtc)->base, &mdp4_crtc->vblank);

	pending = atomic_xchg(&mdp4_crtc->pending, 0);

	if (pending & PENDING_FLIP) {
		complete_flip(crtc, NULL);
	}

	if (pending & PENDING_CURSOR) {
		update_cursor(crtc);
		drm_flip_work_commit(&mdp4_crtc->unref_cursor_work, priv->wq);
	}
}

static void mdp4_crtc_err_irq(struct mdp_irq *irq, uint32_t irqstatus)
{
	struct mdp4_crtc *mdp4_crtc = container_of(irq, struct mdp4_crtc, err);
	struct drm_crtc *crtc = &mdp4_crtc->base;
	DBG("%s: error: %08x", mdp4_crtc->name, irqstatus);
	crtc_flush(crtc);
}

static void mdp4_crtc_wait_for_flush_done(struct drm_crtc *crtc)
{
	struct drm_device *dev = crtc->dev;
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	int ret;

	ret = drm_crtc_vblank_get(crtc);
	if (ret)
		return;

	ret = wait_event_timeout(dev->vblank[drm_crtc_index(crtc)].queue,
		!(mdp4_read(mdp4_kms, REG_MDP4_OVERLAY_FLUSH) &
			mdp4_crtc->flushed_mask),
		msecs_to_jiffies(50));
	if (ret <= 0)
		dev_warn(dev->dev, "vblank time out, crtc=%d\n", mdp4_crtc->id);

	mdp4_crtc->flushed_mask = 0;

	drm_crtc_vblank_put(crtc);
}

uint32_t mdp4_crtc_vblank(struct drm_crtc *crtc)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	return mdp4_crtc->vblank.irqmask;
}

/* set dma config, ie. the format the encoder wants. */
void mdp4_crtc_set_config(struct drm_crtc *crtc, uint32_t config)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);

	mdp4_write(mdp4_kms, REG_MDP4_DMA_CONFIG(mdp4_crtc->dma), config);
}

/* set interface for routing crtc->encoder: */
void mdp4_crtc_set_intf(struct drm_crtc *crtc, enum mdp4_intf intf, int mixer)
{
	struct mdp4_crtc *mdp4_crtc = to_mdp4_crtc(crtc);
	struct mdp4_kms *mdp4_kms = get_kms(crtc);
	uint32_t intf_sel;

	intf_sel = mdp4_read(mdp4_kms, REG_MDP4_DISP_INTF_SEL);

	switch (mdp4_crtc->dma) {
	case DMA_P:
		intf_sel &= ~MDP4_DISP_INTF_SEL_PRIM__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_PRIM(intf);
		break;
	case DMA_S:
		intf_sel &= ~MDP4_DISP_INTF_SEL_SEC__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_SEC(intf);
		break;
	case DMA_E:
		intf_sel &= ~MDP4_DISP_INTF_SEL_EXT__MASK;
		intf_sel |= MDP4_DISP_INTF_SEL_EXT(intf);
		break;
	}

	if (intf == INTF_DSI_VIDEO) {
		intf_sel &= ~MDP4_DISP_INTF_SEL_DSI_CMD;
		intf_sel |= MDP4_DISP_INTF_SEL_DSI_VIDEO;
	} else if (intf == INTF_DSI_CMD) {
		intf_sel &= ~MDP4_DISP_INTF_SEL_DSI_VIDEO;
		intf_sel |= MDP4_DISP_INTF_SEL_DSI_CMD;
	}

	mdp4_crtc->mixer = mixer;

	blend_setup(crtc);

	DBG("%s: intf_sel=%08x", mdp4_crtc->name, intf_sel);

	mdp4_write(mdp4_kms, REG_MDP4_DISP_INTF_SEL, intf_sel);
}

void mdp4_crtc_wait_for_commit_done(struct drm_crtc *crtc)
{
	/* wait_for_flush_done is the only case for now.
	 * Later we will have command mode CRTC to wait for
	 * other event.
	 */
	mdp4_crtc_wait_for_flush_done(crtc);
}

static const char *dma_names[] = {
		"DMA_P", "DMA_S", "DMA_E",
};

/* initialize crtc */
struct drm_crtc *mdp4_crtc_init(struct drm_device *dev,
		struct drm_plane *plane, int id, int ovlp_id,
		enum mdp4_dma dma_id)
{
	struct drm_crtc *crtc = NULL;
	struct mdp4_crtc *mdp4_crtc;

	mdp4_crtc = kzalloc(sizeof(*mdp4_crtc), GFP_KERNEL);
	if (!mdp4_crtc)
		return ERR_PTR(-ENOMEM);

	crtc = &mdp4_crtc->base;

	mdp4_crtc->id = id;

	mdp4_crtc->ovlp = ovlp_id;
	mdp4_crtc->dma = dma_id;

	mdp4_crtc->vblank.irqmask = dma2irq(mdp4_crtc->dma);
	mdp4_crtc->vblank.irq = mdp4_crtc_vblank_irq;

	mdp4_crtc->err.irqmask = dma2err(mdp4_crtc->dma);
	mdp4_crtc->err.irq = mdp4_crtc_err_irq;

	snprintf(mdp4_crtc->name, sizeof(mdp4_crtc->name), "%s:%d",
			dma_names[dma_id], ovlp_id);

	spin_lock_init(&mdp4_crtc->cursor.lock);

	drm_flip_work_init(&mdp4_crtc->unref_cursor_work,
			"unref cursor", unref_cursor_worker);

	drm_crtc_init_with_planes(dev, crtc, plane, NULL, &mdp4_crtc_funcs,
				  NULL);
	drm_crtc_helper_add(crtc, &mdp4_crtc_helper_funcs);
	plane->crtc = crtc;

	return crtc;
}
