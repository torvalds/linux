/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include <linux/dma-mapping.h>

#include "drmP.h"
#include "drm_crtc_helper.h"

#include "nouveau_drv.h"
#include "nouveau_connector.h"
#include "nouveau_encoder.h"
#include "nouveau_crtc.h"
#include "nouveau_dma.h"
#include "nouveau_fb.h"
#include "nv50_display.h"

#define EVO_DMA_NR 9

#define EVO_MASTER  (0x00)
#define EVO_FLIP(c) (0x01 + (c))
#define EVO_OVLY(c) (0x05 + (c))
#define EVO_OIMM(c) (0x09 + (c))
#define EVO_CURS(c) (0x0d + (c))

/* offsets in shared sync bo of various structures */
#define EVO_SYNC(c, o) ((c) * 0x0100 + (o))
#define EVO_MAST_NTFY     EVO_SYNC(  0, 0x00)
#define EVO_FLIP_SEM0(c)  EVO_SYNC((c), 0x00)
#define EVO_FLIP_SEM1(c)  EVO_SYNC((c), 0x10)

struct evo {
	int idx;
	dma_addr_t handle;
	u32 *ptr;
	struct {
		u32 offset;
		u16 value;
	} sem;
};

struct nvd0_display {
	struct nouveau_gpuobj *mem;
	struct nouveau_bo *sync;
	struct evo evo[9];

	struct tasklet_struct tasklet;
	u32 modeset;
};

static struct nvd0_display *
nvd0_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

static struct drm_crtc *
nvd0_display_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

/******************************************************************************
 * EVO channel helpers
 *****************************************************************************/
static inline int
evo_icmd(struct drm_device *dev, int id, u32 mthd, u32 data)
{
	int ret = 0;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000001);
	nv_wr32(dev, 0x610704 + (id * 0x10), data);
	nv_mask(dev, 0x610704 + (id * 0x10), 0x80000ffc, 0x80000000 | mthd);
	if (!nv_wait(dev, 0x610704 + (id * 0x10), 0x80000000, 0x00000000))
		ret = -EBUSY;
	nv_mask(dev, 0x610700 + (id * 0x10), 0x00000001, 0x00000000);
	return ret;
}

static u32 *
evo_wait(struct drm_device *dev, int id, int nr)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 put = nv_rd32(dev, 0x640000 + (id * 0x1000)) / 4;

	if (put + nr >= (PAGE_SIZE / 4)) {
		disp->evo[id].ptr[put] = 0x20000000;

		nv_wr32(dev, 0x640000 + (id * 0x1000), 0x00000000);
		if (!nv_wait(dev, 0x640004 + (id * 0x1000), ~0, 0x00000000)) {
			NV_ERROR(dev, "evo %d dma stalled\n", id);
			return NULL;
		}

		put = 0;
	}

	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_EVO)
		NV_INFO(dev, "Evo%d: %p START\n", id, disp->evo[id].ptr + put);

	return disp->evo[id].ptr + put;
}

static void
evo_kick(u32 *push, struct drm_device *dev, int id)
{
	struct nvd0_display *disp = nvd0_display(dev);

	if (nouveau_reg_debug & NOUVEAU_REG_DEBUG_EVO) {
		u32 curp = nv_rd32(dev, 0x640000 + (id * 0x1000)) >> 2;
		u32 *cur = disp->evo[id].ptr + curp;

		while (cur < push)
			NV_INFO(dev, "Evo%d: 0x%08x\n", id, *cur++);
		NV_INFO(dev, "Evo%d: %p KICK!\n", id, push);
	}

	nv_wr32(dev, 0x640000 + (id * 0x1000), (push - disp->evo[id].ptr) << 2);
}

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

static int
evo_init_dma(struct drm_device *dev, int ch)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 flags;

	flags = 0x00000000;
	if (ch == EVO_MASTER)
		flags |= 0x01000000;

	nv_wr32(dev, 0x610494 + (ch * 0x0010), (disp->evo[ch].handle >> 8) | 3);
	nv_wr32(dev, 0x610498 + (ch * 0x0010), 0x00010000);
	nv_wr32(dev, 0x61049c + (ch * 0x0010), 0x00000001);
	nv_mask(dev, 0x610490 + (ch * 0x0010), 0x00000010, 0x00000010);
	nv_wr32(dev, 0x640000 + (ch * 0x1000), 0x00000000);
	nv_wr32(dev, 0x610490 + (ch * 0x0010), 0x00000013 | flags);
	if (!nv_wait(dev, 0x610490 + (ch * 0x0010), 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "PDISP: ch%d 0x%08x\n", ch,
			      nv_rd32(dev, 0x610490 + (ch * 0x0010)));
		return -EBUSY;
	}

	nv_mask(dev, 0x610090, (1 << ch), (1 << ch));
	nv_mask(dev, 0x6100a0, (1 << ch), (1 << ch));
	return 0;
}

static void
evo_fini_dma(struct drm_device *dev, int ch)
{
	if (!(nv_rd32(dev, 0x610490 + (ch * 0x0010)) & 0x00000010))
		return;

	nv_mask(dev, 0x610490 + (ch * 0x0010), 0x00000010, 0x00000000);
	nv_mask(dev, 0x610490 + (ch * 0x0010), 0x00000003, 0x00000000);
	nv_wait(dev, 0x610490 + (ch * 0x0010), 0x80000000, 0x00000000);
	nv_mask(dev, 0x610090, (1 << ch), 0x00000000);
	nv_mask(dev, 0x6100a0, (1 << ch), 0x00000000);
}

static inline void
evo_piow(struct drm_device *dev, int ch, u16 mthd, u32 data)
{
	nv_wr32(dev, 0x640000 + (ch * 0x1000) + mthd, data);
}

static int
evo_init_pio(struct drm_device *dev, int ch)
{
	nv_wr32(dev, 0x610490 + (ch * 0x0010), 0x00000001);
	if (!nv_wait(dev, 0x610490 + (ch * 0x0010), 0x00010000, 0x00010000)) {
		NV_ERROR(dev, "PDISP: ch%d 0x%08x\n", ch,
			      nv_rd32(dev, 0x610490 + (ch * 0x0010)));
		return -EBUSY;
	}

	nv_mask(dev, 0x610090, (1 << ch), (1 << ch));
	nv_mask(dev, 0x6100a0, (1 << ch), (1 << ch));
	return 0;
}

static void
evo_fini_pio(struct drm_device *dev, int ch)
{
	if (!(nv_rd32(dev, 0x610490 + (ch * 0x0010)) & 0x00000001))
		return;

	nv_mask(dev, 0x610490 + (ch * 0x0010), 0x00000010, 0x00000010);
	nv_mask(dev, 0x610490 + (ch * 0x0010), 0x00000001, 0x00000000);
	nv_wait(dev, 0x610490 + (ch * 0x0010), 0x00010000, 0x00000000);
	nv_mask(dev, 0x610090, (1 << ch), 0x00000000);
	nv_mask(dev, 0x6100a0, (1 << ch), 0x00000000);
}

static bool
evo_sync_wait(void *data)
{
	return nouveau_bo_rd32(data, EVO_MAST_NTFY) != 0x00000000;
}

static int
evo_sync(struct drm_device *dev, int ch)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 *push = evo_wait(dev, ch, 8);
	if (push) {
		nouveau_bo_wr32(disp->sync, EVO_MAST_NTFY, 0x00000000);
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x80000000 | EVO_MAST_NTFY);
		evo_mthd(push, 0x0080, 2);
		evo_data(push, 0x00000000);
		evo_data(push, 0x00000000);
		evo_kick(push, dev, ch);
		if (nv_wait_cb(dev, evo_sync_wait, disp->sync))
			return 0;
	}

	return -EBUSY;
}

/******************************************************************************
 * Page flipping channel
 *****************************************************************************/
struct nouveau_bo *
nvd0_display_crtc_sema(struct drm_device *dev, int crtc)
{
	return nvd0_display(dev)->sync;
}

void
nvd0_display_flip_stop(struct drm_crtc *crtc)
{
	struct nvd0_display *disp = nvd0_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct evo *evo = &disp->evo[EVO_FLIP(nv_crtc->index)];
	u32 *push;

	push = evo_wait(crtc->dev, evo->idx, 8);
	if (push) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0094, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0080, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, crtc->dev, evo->idx);
	}
}

int
nvd0_display_flip_next(struct drm_crtc *crtc, struct drm_framebuffer *fb,
		       struct nouveau_channel *chan, u32 swap_interval)
{
	struct nouveau_framebuffer *nv_fb = nouveau_framebuffer(fb);
	struct nvd0_display *disp = nvd0_display(crtc->dev);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct evo *evo = &disp->evo[EVO_FLIP(nv_crtc->index)];
	u64 offset;
	u32 *push;
	int ret;

	evo_sync(crtc->dev, EVO_MASTER);

	swap_interval <<= 4;
	if (swap_interval == 0)
		swap_interval |= 0x100;

	push = evo_wait(crtc->dev, evo->idx, 128);
	if (unlikely(push == NULL))
		return -EBUSY;

	/* synchronise with the rendering channel, if necessary */
	if (likely(chan)) {
		ret = RING_SPACE(chan, 10);
		if (ret)
			return ret;

		offset  = chan->dispc_vma[nv_crtc->index].offset;
		offset += evo->sem.offset;

		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 0xf00d0000 | evo->sem.value);
		OUT_RING  (chan, 0x1002);
		BEGIN_NVC0(chan, 0, NV84_SUBCHAN_SEMAPHORE_ADDRESS_HIGH, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset ^ 0x10));
		OUT_RING  (chan, 0x74b1e000);
		OUT_RING  (chan, 0x1001);
		FIRE_RING (chan);
	} else {
		nouveau_bo_wr32(disp->sync, evo->sem.offset / 4,
				0xf00d0000 | evo->sem.value);
		evo_sync(crtc->dev, EVO_MASTER);
	}

	/* queue the flip */
	evo_mthd(push, 0x0100, 1);
	evo_data(push, 0xfffe0000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, swap_interval);
	if (!(swap_interval & 0x00000100)) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, 0x40000000);
	}
	evo_mthd(push, 0x0088, 4);
	evo_data(push, evo->sem.offset);
	evo_data(push, 0xf00d0000 | evo->sem.value);
	evo_data(push, 0x74b1e000);
	evo_data(push, NvEvoSync);
	evo_mthd(push, 0x00a0, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x00c0, 1);
	evo_data(push, nv_fb->r_dma);
	evo_mthd(push, 0x0110, 2);
	evo_data(push, 0x00000000);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0400, 5);
	evo_data(push, nv_fb->nvbo->bo.offset >> 8);
	evo_data(push, 0);
	evo_data(push, (fb->height << 16) | fb->width);
	evo_data(push, nv_fb->r_pitch);
	evo_data(push, nv_fb->r_format);
	evo_mthd(push, 0x0080, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, crtc->dev, evo->idx);

	evo->sem.offset ^= 0x10;
	evo->sem.value++;
	return 0;
}

/******************************************************************************
 * CRTC
 *****************************************************************************/
static int
nvd0_crtc_set_dither(struct nouveau_crtc *nv_crtc, bool update)
{
	struct drm_nouveau_private *dev_priv = nv_crtc->base.dev->dev_private;
	struct drm_device *dev = nv_crtc->base.dev;
	struct nouveau_connector *nv_connector;
	struct drm_connector *connector;
	u32 *push, mode = 0x00;
	u32 mthd;

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	connector = &nv_connector->base;
	if (nv_connector->dithering_mode == DITHERING_MODE_AUTO) {
		if (nv_crtc->base.fb->depth > connector->display_info.bpc * 3)
			mode = DITHERING_MODE_DYNAMIC2X2;
	} else {
		mode = nv_connector->dithering_mode;
	}

	if (nv_connector->dithering_depth == DITHERING_DEPTH_AUTO) {
		if (connector->display_info.bpc >= 8)
			mode |= DITHERING_DEPTH_8BPC;
	} else {
		mode |= nv_connector->dithering_depth;
	}

	if (dev_priv->card_type < NV_E0)
		mthd = 0x0490 + (nv_crtc->index * 0x0300);
	else
		mthd = 0x04a0 + (nv_crtc->index * 0x0300);

	push = evo_wait(dev, EVO_MASTER, 4);
	if (push) {
		evo_mthd(push, mthd, 1);
		evo_data(push, mode);
		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, dev, EVO_MASTER);
	}

	return 0;
}

static int
nvd0_crtc_set_scale(struct nouveau_crtc *nv_crtc, bool update)
{
	struct drm_display_mode *omode, *umode = &nv_crtc->base.mode;
	struct drm_device *dev = nv_crtc->base.dev;
	struct drm_crtc *crtc = &nv_crtc->base;
	struct nouveau_connector *nv_connector;
	int mode = DRM_MODE_SCALE_NONE;
	u32 oX, oY, *push;

	/* start off at the resolution we programmed the crtc for, this
	 * effectively handles NONE/FULL scaling
	 */
	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	if (nv_connector && nv_connector->native_mode)
		mode = nv_connector->scaling_mode;

	if (mode != DRM_MODE_SCALE_NONE)
		omode = nv_connector->native_mode;
	else
		omode = umode;

	oX = omode->hdisplay;
	oY = omode->vdisplay;
	if (omode->flags & DRM_MODE_FLAG_DBLSCAN)
		oY *= 2;

	/* add overscan compensation if necessary, will keep the aspect
	 * ratio the same as the backend mode unless overridden by the
	 * user setting both hborder and vborder properties.
	 */
	if (nv_connector && ( nv_connector->underscan == UNDERSCAN_ON ||
			     (nv_connector->underscan == UNDERSCAN_AUTO &&
			      nv_connector->edid &&
			      drm_detect_hdmi_monitor(nv_connector->edid)))) {
		u32 bX = nv_connector->underscan_hborder;
		u32 bY = nv_connector->underscan_vborder;
		u32 aspect = (oY << 19) / oX;

		if (bX) {
			oX -= (bX * 2);
			if (bY) oY -= (bY * 2);
			else    oY  = ((oX * aspect) + (aspect / 2)) >> 19;
		} else {
			oX -= (oX >> 4) + 32;
			if (bY) oY -= (bY * 2);
			else    oY  = ((oX * aspect) + (aspect / 2)) >> 19;
		}
	}

	/* handle CENTER/ASPECT scaling, taking into account the areas
	 * removed already for overscan compensation
	 */
	switch (mode) {
	case DRM_MODE_SCALE_CENTER:
		oX = min((u32)umode->hdisplay, oX);
		oY = min((u32)umode->vdisplay, oY);
		/* fall-through */
	case DRM_MODE_SCALE_ASPECT:
		if (oY < oX) {
			u32 aspect = (umode->hdisplay << 19) / umode->vdisplay;
			oX = ((oY * aspect) + (aspect / 2)) >> 19;
		} else {
			u32 aspect = (umode->vdisplay << 19) / umode->hdisplay;
			oY = ((oX * aspect) + (aspect / 2)) >> 19;
		}
		break;
	default:
		break;
	}

	push = evo_wait(dev, EVO_MASTER, 8);
	if (push) {
		evo_mthd(push, 0x04c0 + (nv_crtc->index * 0x300), 3);
		evo_data(push, (oY << 16) | oX);
		evo_data(push, (oY << 16) | oX);
		evo_data(push, (oY << 16) | oX);
		evo_mthd(push, 0x0494 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x04b8 + (nv_crtc->index * 0x300), 1);
		evo_data(push, (umode->vdisplay << 16) | umode->hdisplay);
		evo_kick(push, dev, EVO_MASTER);
		if (update) {
			nvd0_display_flip_stop(crtc);
			nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
		}
	}

	return 0;
}

static int
nvd0_crtc_set_image(struct nouveau_crtc *nv_crtc, struct drm_framebuffer *fb,
		    int x, int y, bool update)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(fb);
	u32 *push;

	push = evo_wait(fb->dev, EVO_MASTER, 16);
	if (push) {
		evo_mthd(push, 0x0460 + (nv_crtc->index * 0x300), 1);
		evo_data(push, nvfb->nvbo->bo.offset >> 8);
		evo_mthd(push, 0x0468 + (nv_crtc->index * 0x300), 4);
		evo_data(push, (fb->height << 16) | fb->width);
		evo_data(push, nvfb->r_pitch);
		evo_data(push, nvfb->r_format);
		evo_data(push, nvfb->r_dma);
		evo_mthd(push, 0x04b0 + (nv_crtc->index * 0x300), 1);
		evo_data(push, (y << 16) | x);
		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, fb->dev, EVO_MASTER);
	}

	nv_crtc->fb.tile_flags = nvfb->r_dma;
	return 0;
}

static void
nvd0_crtc_cursor_show(struct nouveau_crtc *nv_crtc, bool show, bool update)
{
	struct drm_device *dev = nv_crtc->base.dev;
	u32 *push = evo_wait(dev, EVO_MASTER, 16);
	if (push) {
		if (show) {
			evo_mthd(push, 0x0480 + (nv_crtc->index * 0x300), 2);
			evo_data(push, 0x85000000);
			evo_data(push, nv_crtc->cursor.nvbo->bo.offset >> 8);
			evo_mthd(push, 0x048c + (nv_crtc->index * 0x300), 1);
			evo_data(push, NvEvoVRAM);
		} else {
			evo_mthd(push, 0x0480 + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x05000000);
			evo_mthd(push, 0x048c + (nv_crtc->index * 0x300), 1);
			evo_data(push, 0x00000000);
		}

		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}

		evo_kick(push, dev, EVO_MASTER);
	}
}

static void
nvd0_crtc_dpms(struct drm_crtc *crtc, int mode)
{
}

static void
nvd0_crtc_prepare(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 *push;

	nvd0_display_flip_stop(crtc);

	push = evo_wait(crtc->dev, EVO_MASTER, 2);
	if (push) {
		evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x03000000);
		evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_kick(push, crtc->dev, EVO_MASTER);
	}

	nvd0_crtc_cursor_show(nv_crtc, false, false);
}

static void
nvd0_crtc_commit(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 *push;

	push = evo_wait(crtc->dev, EVO_MASTER, 32);
	if (push) {
		evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
		evo_data(push, nv_crtc->fb.tile_flags);
		evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 4);
		evo_data(push, 0x83000000);
		evo_data(push, nv_crtc->lut.nvbo->bo.offset >> 8);
		evo_data(push, 0x00000000);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
		evo_data(push, NvEvoVRAM);
		evo_mthd(push, 0x0430 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0xffffff00);
		evo_kick(push, crtc->dev, EVO_MASTER);
	}

	nvd0_crtc_cursor_show(nv_crtc, nv_crtc->cursor.visible, true);
	nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
}

static bool
nvd0_crtc_mode_fixup(struct drm_crtc *crtc, struct drm_display_mode *mode,
		     struct drm_display_mode *adjusted_mode)
{
	return true;
}

static int
nvd0_crtc_swap_fbs(struct drm_crtc *crtc, struct drm_framebuffer *old_fb)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(crtc->fb);
	int ret;

	ret = nouveau_bo_pin(nvfb->nvbo, TTM_PL_FLAG_VRAM);
	if (ret)
		return ret;

	if (old_fb) {
		nvfb = nouveau_framebuffer(old_fb);
		nouveau_bo_unpin(nvfb->nvbo);
	}

	return 0;
}

static int
nvd0_crtc_mode_set(struct drm_crtc *crtc, struct drm_display_mode *umode,
		   struct drm_display_mode *mode, int x, int y,
		   struct drm_framebuffer *old_fb)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct nouveau_connector *nv_connector;
	u32 ilace = (mode->flags & DRM_MODE_FLAG_INTERLACE) ? 2 : 1;
	u32 vscan = (mode->flags & DRM_MODE_FLAG_DBLSCAN) ? 2 : 1;
	u32 hactive, hsynce, hbackp, hfrontp, hblanke, hblanks;
	u32 vactive, vsynce, vbackp, vfrontp, vblanke, vblanks;
	u32 vblan2e = 0, vblan2s = 1;
	u32 *push;
	int ret;

	hactive = mode->htotal;
	hsynce  = mode->hsync_end - mode->hsync_start - 1;
	hbackp  = mode->htotal - mode->hsync_end;
	hblanke = hsynce + hbackp;
	hfrontp = mode->hsync_start - mode->hdisplay;
	hblanks = mode->htotal - hfrontp - 1;

	vactive = mode->vtotal * vscan / ilace;
	vsynce  = ((mode->vsync_end - mode->vsync_start) * vscan / ilace) - 1;
	vbackp  = (mode->vtotal - mode->vsync_end) * vscan / ilace;
	vblanke = vsynce + vbackp;
	vfrontp = (mode->vsync_start - mode->vdisplay) * vscan / ilace;
	vblanks = vactive - vfrontp - 1;
	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		vblan2e = vactive + vsynce + vbackp;
		vblan2s = vblan2e + (mode->vdisplay * vscan / ilace);
		vactive = (vactive * 2) + 1;
	}

	ret = nvd0_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	push = evo_wait(crtc->dev, EVO_MASTER, 64);
	if (push) {
		evo_mthd(push, 0x0410 + (nv_crtc->index * 0x300), 6);
		evo_data(push, 0x00000000);
		evo_data(push, (vactive << 16) | hactive);
		evo_data(push, ( vsynce << 16) | hsynce);
		evo_data(push, (vblanke << 16) | hblanke);
		evo_data(push, (vblanks << 16) | hblanks);
		evo_data(push, (vblan2e << 16) | vblan2s);
		evo_mthd(push, 0x042c + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000); /* ??? */
		evo_mthd(push, 0x0450 + (nv_crtc->index * 0x300), 3);
		evo_data(push, mode->clock * 1000);
		evo_data(push, 0x00200000); /* ??? */
		evo_data(push, mode->clock * 1000);
		evo_mthd(push, 0x04d0 + (nv_crtc->index * 0x300), 2);
		evo_data(push, 0x00000311);
		evo_data(push, 0x00000100);
		evo_kick(push, crtc->dev, EVO_MASTER);
	}

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	nvd0_crtc_set_dither(nv_crtc, false);
	nvd0_crtc_set_scale(nv_crtc, false);
	nvd0_crtc_set_image(nv_crtc, crtc->fb, x, y, false);
	return 0;
}

static int
nvd0_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
			struct drm_framebuffer *old_fb)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int ret;

	if (!crtc->fb) {
		NV_DEBUG_KMS(crtc->dev, "No FB bound\n");
		return 0;
	}

	ret = nvd0_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	nvd0_display_flip_stop(crtc);
	nvd0_crtc_set_image(nv_crtc, crtc->fb, x, y, true);
	nvd0_display_flip_next(crtc, crtc->fb, NULL, 1);
	return 0;
}

static int
nvd0_crtc_mode_set_base_atomic(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb, int x, int y,
			       enum mode_set_atomic state)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	nvd0_display_flip_stop(crtc);
	nvd0_crtc_set_image(nv_crtc, fb, x, y, true);
	return 0;
}

static void
nvd0_crtc_lut_load(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	void __iomem *lut = nvbo_kmap_obj_iovirtual(nv_crtc->lut.nvbo);
	int i;

	for (i = 0; i < 256; i++) {
		writew(0x6000 + (nv_crtc->lut.r[i] >> 2), lut + (i * 0x20) + 0);
		writew(0x6000 + (nv_crtc->lut.g[i] >> 2), lut + (i * 0x20) + 2);
		writew(0x6000 + (nv_crtc->lut.b[i] >> 2), lut + (i * 0x20) + 4);
	}
}

static int
nvd0_crtc_cursor_set(struct drm_crtc *crtc, struct drm_file *file_priv,
		     uint32_t handle, uint32_t width, uint32_t height)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	struct drm_device *dev = crtc->dev;
	struct drm_gem_object *gem;
	struct nouveau_bo *nvbo;
	bool visible = (handle != 0);
	int i, ret = 0;

	if (visible) {
		if (width != 64 || height != 64)
			return -EINVAL;

		gem = drm_gem_object_lookup(dev, file_priv, handle);
		if (unlikely(!gem))
			return -ENOENT;
		nvbo = nouveau_gem_object(gem);

		ret = nouveau_bo_map(nvbo);
		if (ret == 0) {
			for (i = 0; i < 64 * 64; i++) {
				u32 v = nouveau_bo_rd32(nvbo, i);
				nouveau_bo_wr32(nv_crtc->cursor.nvbo, i, v);
			}
			nouveau_bo_unmap(nvbo);
		}

		drm_gem_object_unreference_unlocked(gem);
	}

	if (visible != nv_crtc->cursor.visible) {
		nvd0_crtc_cursor_show(nv_crtc, visible, true);
		nv_crtc->cursor.visible = visible;
	}

	return ret;
}

static int
nvd0_crtc_cursor_move(struct drm_crtc *crtc, int x, int y)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	int ch = EVO_CURS(nv_crtc->index);

	evo_piow(crtc->dev, ch, 0x0084, (y << 16) | x);
	evo_piow(crtc->dev, ch, 0x0080, 0x00000000);
	return 0;
}

static void
nvd0_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b,
		    uint32_t start, uint32_t size)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 end = max(start + size, (u32)256);
	u32 i;

	for (i = start; i < end; i++) {
		nv_crtc->lut.r[i] = r[i];
		nv_crtc->lut.g[i] = g[i];
		nv_crtc->lut.b[i] = b[i];
	}

	nvd0_crtc_lut_load(crtc);
}

static void
nvd0_crtc_destroy(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	nouveau_bo_unmap(nv_crtc->cursor.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);
	nouveau_bo_unmap(nv_crtc->lut.nvbo);
	nouveau_bo_ref(NULL, &nv_crtc->lut.nvbo);
	drm_crtc_cleanup(crtc);
	kfree(crtc);
}

static const struct drm_crtc_helper_funcs nvd0_crtc_hfunc = {
	.dpms = nvd0_crtc_dpms,
	.prepare = nvd0_crtc_prepare,
	.commit = nvd0_crtc_commit,
	.mode_fixup = nvd0_crtc_mode_fixup,
	.mode_set = nvd0_crtc_mode_set,
	.mode_set_base = nvd0_crtc_mode_set_base,
	.mode_set_base_atomic = nvd0_crtc_mode_set_base_atomic,
	.load_lut = nvd0_crtc_lut_load,
};

static const struct drm_crtc_funcs nvd0_crtc_func = {
	.cursor_set = nvd0_crtc_cursor_set,
	.cursor_move = nvd0_crtc_cursor_move,
	.gamma_set = nvd0_crtc_gamma_set,
	.set_config = drm_crtc_helper_set_config,
	.destroy = nvd0_crtc_destroy,
	.page_flip = nouveau_crtc_page_flip,
};

static void
nvd0_cursor_set_pos(struct nouveau_crtc *nv_crtc, int x, int y)
{
}

static void
nvd0_cursor_set_offset(struct nouveau_crtc *nv_crtc, uint32_t offset)
{
}

static int
nvd0_crtc_create(struct drm_device *dev, int index)
{
	struct nouveau_crtc *nv_crtc;
	struct drm_crtc *crtc;
	int ret, i;

	nv_crtc = kzalloc(sizeof(*nv_crtc), GFP_KERNEL);
	if (!nv_crtc)
		return -ENOMEM;

	nv_crtc->index = index;
	nv_crtc->set_dither = nvd0_crtc_set_dither;
	nv_crtc->set_scale = nvd0_crtc_set_scale;
	nv_crtc->cursor.set_offset = nvd0_cursor_set_offset;
	nv_crtc->cursor.set_pos = nvd0_cursor_set_pos;
	for (i = 0; i < 256; i++) {
		nv_crtc->lut.r[i] = i << 8;
		nv_crtc->lut.g[i] = i << 8;
		nv_crtc->lut.b[i] = i << 8;
	}

	crtc = &nv_crtc->base;
	drm_crtc_init(dev, crtc, &nvd0_crtc_func);
	drm_crtc_helper_add(crtc, &nvd0_crtc_hfunc);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	ret = nouveau_bo_new(dev, 64 * 64 * 4, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &nv_crtc->cursor.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(nv_crtc->cursor.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(nv_crtc->cursor.nvbo);
		if (ret)
			nouveau_bo_ref(NULL, &nv_crtc->cursor.nvbo);
	}

	if (ret)
		goto out;

	ret = nouveau_bo_new(dev, 8192, 0x100, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &nv_crtc->lut.nvbo);
	if (!ret) {
		ret = nouveau_bo_pin(nv_crtc->lut.nvbo, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(nv_crtc->lut.nvbo);
		if (ret)
			nouveau_bo_ref(NULL, &nv_crtc->lut.nvbo);
	}

	if (ret)
		goto out;

	nvd0_crtc_lut_load(crtc);

out:
	if (ret)
		nvd0_crtc_destroy(crtc);
	return ret;
}

/******************************************************************************
 * DAC
 *****************************************************************************/
static void
nvd0_dac_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	int or = nv_encoder->or;
	u32 dpms_ctrl;

	dpms_ctrl = 0x80000000;
	if (mode == DRM_MODE_DPMS_STANDBY || mode == DRM_MODE_DPMS_OFF)
		dpms_ctrl |= 0x00000001;
	if (mode == DRM_MODE_DPMS_SUSPEND || mode == DRM_MODE_DPMS_OFF)
		dpms_ctrl |= 0x00000004;

	nv_wait(dev, 0x61a004 + (or * 0x0800), 0x80000000, 0x00000000);
	nv_mask(dev, 0x61a004 + (or * 0x0800), 0xc000007f, dpms_ctrl);
	nv_wait(dev, 0x61a004 + (or * 0x0800), 0x80000000, 0x00000000);
}

static bool
nvd0_dac_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		if (nv_connector->scaling_mode != DRM_MODE_SCALE_NONE) {
			int id = adjusted_mode->base.id;
			*adjusted_mode = *nv_connector->native_mode;
			adjusted_mode->base.id = id;
		}
	}

	return true;
}

static void
nvd0_dac_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_dac_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode,
		  struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	u32 syncs, magic, *push;

	syncs = 0x00000001;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		syncs |= 0x00000008;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		syncs |= 0x00000010;

	magic = 0x31ec6000 | (nv_crtc->index << 25);
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		magic |= 0x00000001;

	nvd0_dac_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(encoder->dev, EVO_MASTER, 8);
	if (push) {
		evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 2);
		evo_data(push, syncs);
		evo_data(push, magic);
		evo_mthd(push, 0x0180 + (nv_encoder->or * 0x020), 2);
		evo_data(push, 1 << nv_crtc->index);
		evo_data(push, 0x00ff);
		evo_kick(push, encoder->dev, EVO_MASTER);
	}

	nv_encoder->crtc = encoder->crtc;
}

static void
nvd0_dac_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	u32 *push;

	if (nv_encoder->crtc) {
		nvd0_crtc_prepare(nv_encoder->crtc);

		push = evo_wait(dev, EVO_MASTER, 4);
		if (push) {
			evo_mthd(push, 0x0180 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, dev, EVO_MASTER);
		}

		nv_encoder->crtc = NULL;
	}
}

static enum drm_connector_status
nvd0_dac_detect(struct drm_encoder *encoder, struct drm_connector *connector)
{
	enum drm_connector_status status = connector_status_disconnected;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	int or = nv_encoder->or;
	u32 load;

	nv_wr32(dev, 0x61a00c + (or * 0x800), 0x00100000);
	udelay(9500);
	nv_wr32(dev, 0x61a00c + (or * 0x800), 0x80000000);

	load = nv_rd32(dev, 0x61a00c + (or * 0x800));
	if ((load & 0x38000000) == 0x38000000)
		status = connector_status_connected;

	nv_wr32(dev, 0x61a00c + (or * 0x800), 0x00000000);
	return status;
}

static void
nvd0_dac_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nvd0_dac_hfunc = {
	.dpms = nvd0_dac_dpms,
	.mode_fixup = nvd0_dac_mode_fixup,
	.prepare = nvd0_dac_disconnect,
	.commit = nvd0_dac_commit,
	.mode_set = nvd0_dac_mode_set,
	.disable = nvd0_dac_disconnect,
	.get_crtc = nvd0_display_crtc_get,
	.detect = nvd0_dac_detect
};

static const struct drm_encoder_funcs nvd0_dac_func = {
	.destroy = nvd0_dac_destroy,
};

static int
nvd0_dac_create(struct drm_connector *connector, struct dcb_entry *dcbe)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(dev, encoder, &nvd0_dac_func, DRM_MODE_ENCODER_DAC);
	drm_encoder_helper_add(encoder, &nvd0_dac_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * Audio
 *****************************************************************************/
static void
nvd0_audio_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;
	struct drm_device *dev = encoder->dev;
	int i, or = nv_encoder->or * 0x30;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_monitor_audio(nv_connector->edid))
		return;

	nv_mask(dev, 0x10ec10 + or, 0x80000003, 0x80000001);

	drm_edid_to_eld(&nv_connector->base, nv_connector->edid);
	if (nv_connector->base.eld[0]) {
		u8 *eld = nv_connector->base.eld;

		for (i = 0; i < eld[2] * 4; i++)
			nv_wr32(dev, 0x10ec00 + or, (i << 8) | eld[i]);
		for (i = eld[2] * 4; i < 0x60; i++)
			nv_wr32(dev, 0x10ec00 + or, (i << 8) | 0x00);

		nv_mask(dev, 0x10ec10 + or, 0x80000002, 0x80000002);
	}
}

static void
nvd0_audio_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	int or = nv_encoder->or * 0x30;

	nv_mask(dev, 0x10ec10 + or, 0x80000003, 0x80000000);
}

/******************************************************************************
 * HDMI
 *****************************************************************************/
static void
nvd0_hdmi_mode_set(struct drm_encoder *encoder, struct drm_display_mode *mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct drm_device *dev = encoder->dev;
	int head = nv_crtc->index * 0x800;
	u32 rekey = 56; /* binary driver, and tegra constant */
	u32 max_ac_packet;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (!drm_detect_hdmi_monitor(nv_connector->edid))
		return;

	max_ac_packet  = mode->htotal - mode->hdisplay;
	max_ac_packet -= rekey;
	max_ac_packet -= 18; /* constant from tegra */
	max_ac_packet /= 32;

	/* AVI InfoFrame */
	nv_mask(dev, 0x616714 + head, 0x00000001, 0x00000000);
	nv_wr32(dev, 0x61671c + head, 0x000d0282);
	nv_wr32(dev, 0x616720 + head, 0x0000006f);
	nv_wr32(dev, 0x616724 + head, 0x00000000);
	nv_wr32(dev, 0x616728 + head, 0x00000000);
	nv_wr32(dev, 0x61672c + head, 0x00000000);
	nv_mask(dev, 0x616714 + head, 0x00000001, 0x00000001);

	/* ??? InfoFrame? */
	nv_mask(dev, 0x6167a4 + head, 0x00000001, 0x00000000);
	nv_wr32(dev, 0x6167ac + head, 0x00000010);
	nv_mask(dev, 0x6167a4 + head, 0x00000001, 0x00000001);

	/* HDMI_CTRL */
	nv_mask(dev, 0x616798 + head, 0x401f007f, 0x40000000 | rekey |
						  max_ac_packet << 16);

	/* NFI, audio doesn't work without it though.. */
	nv_mask(dev, 0x616548 + head, 0x00000070, 0x00000000);

	nvd0_audio_mode_set(encoder, mode);
}

static void
nvd0_hdmi_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(nv_encoder->crtc);
	struct drm_device *dev = encoder->dev;
	int head = nv_crtc->index * 0x800;

	nvd0_audio_disconnect(encoder);

	nv_mask(dev, 0x616798 + head, 0x40000000, 0x00000000);
	nv_mask(dev, 0x6167a4 + head, 0x00000001, 0x00000000);
	nv_mask(dev, 0x616714 + head, 0x00000001, 0x00000000);
}

/******************************************************************************
 * SOR
 *****************************************************************************/
static inline u32
nvd0_sor_dp_lane_map(struct drm_device *dev, struct dcb_entry *dcb, u8 lane)
{
	static const u8 nvd0[] = { 16, 8, 0, 24 };
	return nvd0[lane];
}

static void
nvd0_sor_dp_train_set(struct drm_device *dev, struct dcb_entry *dcb, u8 pattern)
{
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 loff = (or * 0x800) + (link * 0x80);
	nv_mask(dev, 0x61c110 + loff, 0x0f0f0f0f, 0x01010101 * pattern);
}

static void
nvd0_sor_dp_train_adj(struct drm_device *dev, struct dcb_entry *dcb,
		      u8 lane, u8 swing, u8 preem)
{
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 loff = (or * 0x800) + (link * 0x80);
	u32 shift = nvd0_sor_dp_lane_map(dev, dcb, lane);
	u32 mask = 0x000000ff << shift;
	u8 *table, *entry, *config = NULL;

	switch (swing) {
	case 0: preem += 0; break;
	case 1: preem += 4; break;
	case 2: preem += 7; break;
	case 3: preem += 9; break;
	}

	table = nouveau_dp_bios_data(dev, dcb, &entry);
	if (table) {
		if (table[0] == 0x30) {
			config  = entry + table[4];
			config += table[5] * preem;
		} else
		if (table[0] == 0x40) {
			config  = table + table[1];
			config += table[2] * table[3];
			config += table[6] * preem;
		}
	}

	if (!config) {
		NV_ERROR(dev, "PDISP: unsupported DP table for chipset\n");
		return;
	}

	nv_mask(dev, 0x61c118 + loff, mask, config[1] << shift);
	nv_mask(dev, 0x61c120 + loff, mask, config[2] << shift);
	nv_mask(dev, 0x61c130 + loff, 0x0000ff00, config[3] << 8);
	nv_mask(dev, 0x61c13c + loff, 0x00000000, 0x00000000);
}

static void
nvd0_sor_dp_link_set(struct drm_device *dev, struct dcb_entry *dcb, int crtc,
		     int link_nr, u32 link_bw, bool enhframe)
{
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 soff = (or * 0x800);
	u32 dpctrl = nv_rd32(dev, 0x61c10c + loff) & ~0x001f4000;
	u32 clksor = nv_rd32(dev, 0x612300 + soff) & ~0x007c0000;
	u32 script = 0x0000, lane_mask = 0;
	u8 *table, *entry;
	int i;

	link_bw /= 27000;

	table = nouveau_dp_bios_data(dev, dcb, &entry);
	if (table) {
		if      (table[0] == 0x30) entry = ROMPTR(dev, entry[10]);
		else if (table[0] == 0x40) entry = ROMPTR(dev, entry[9]);
		else                       entry = NULL;

		while (entry) {
			if (entry[0] >= link_bw)
				break;
			entry += 3;
		}

		nouveau_bios_run_init_table(dev, script, dcb, crtc);
	}

	clksor |= link_bw << 18;
	dpctrl |= ((1 << link_nr) - 1) << 16;
	if (enhframe)
		dpctrl |= 0x00004000;

	for (i = 0; i < link_nr; i++)
		lane_mask |= 1 << (nvd0_sor_dp_lane_map(dev, dcb, i) >> 3);

	nv_wr32(dev, 0x612300 + soff, clksor);
	nv_wr32(dev, 0x61c10c + loff, dpctrl);
	nv_mask(dev, 0x61c130 + loff, 0x0000000f, lane_mask);
}

static void
nvd0_sor_dp_link_get(struct drm_device *dev, struct dcb_entry *dcb,
		     u32 *link_nr, u32 *link_bw)
{
	const u32 or = ffs(dcb->or) - 1, link = !(dcb->sorconf.link & 1);
	const u32 loff = (or * 0x800) + (link * 0x80);
	const u32 soff = (or * 0x800);
	u32 dpctrl = nv_rd32(dev, 0x61c10c + loff) & 0x000f0000;
	u32 clksor = nv_rd32(dev, 0x612300 + soff);

	if      (dpctrl > 0x00030000) *link_nr = 4;
	else if (dpctrl > 0x00010000) *link_nr = 2;
	else			      *link_nr = 1;

	*link_bw  = (clksor & 0x007c0000) >> 18;
	*link_bw *= 27000;
}

static void
nvd0_sor_dp_calc_tu(struct drm_device *dev, struct dcb_entry *dcb,
		    u32 crtc, u32 datarate)
{
	const u32 symbol = 100000;
	const u32 TU = 64;
	u32 link_nr, link_bw;
	u64 ratio, value;

	nvd0_sor_dp_link_get(dev, dcb, &link_nr, &link_bw);

	ratio  = datarate;
	ratio *= symbol;
	do_div(ratio, link_nr * link_bw);

	value  = (symbol - ratio) * TU;
	value *= ratio;
	do_div(value, symbol);
	do_div(value, symbol);

	value += 5;
	value |= 0x08000000;

	nv_wr32(dev, 0x616610 + (crtc * 0x800), value);
}

static void
nvd0_sor_dpms(struct drm_encoder *encoder, int mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	struct drm_encoder *partner;
	int or = nv_encoder->or;
	u32 dpms_ctrl;

	nv_encoder->last_dpms = mode;

	list_for_each_entry(partner, &dev->mode_config.encoder_list, head) {
		struct nouveau_encoder *nv_partner = nouveau_encoder(partner);

		if (partner->encoder_type != DRM_MODE_ENCODER_TMDS)
			continue;

		if (nv_partner != nv_encoder &&
		    nv_partner->dcb->or == nv_encoder->dcb->or) {
			if (nv_partner->last_dpms == DRM_MODE_DPMS_ON)
				return;
			break;
		}
	}

	dpms_ctrl  = (mode == DRM_MODE_DPMS_ON);
	dpms_ctrl |= 0x80000000;

	nv_wait(dev, 0x61c004 + (or * 0x0800), 0x80000000, 0x00000000);
	nv_mask(dev, 0x61c004 + (or * 0x0800), 0x80000001, dpms_ctrl);
	nv_wait(dev, 0x61c004 + (or * 0x0800), 0x80000000, 0x00000000);
	nv_wait(dev, 0x61c030 + (or * 0x0800), 0x10000000, 0x00000000);

	if (nv_encoder->dcb->type == OUTPUT_DP) {
		struct dp_train_func func = {
			.link_set = nvd0_sor_dp_link_set,
			.train_set = nvd0_sor_dp_train_set,
			.train_adj = nvd0_sor_dp_train_adj
		};

		nouveau_dp_dpms(encoder, mode, nv_encoder->dp.datarate, &func);
	}
}

static bool
nvd0_sor_mode_fixup(struct drm_encoder *encoder, struct drm_display_mode *mode,
		    struct drm_display_mode *adjusted_mode)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_connector *nv_connector;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	if (nv_connector && nv_connector->native_mode) {
		if (nv_connector->scaling_mode != DRM_MODE_SCALE_NONE) {
			int id = adjusted_mode->base.id;
			*adjusted_mode = *nv_connector->native_mode;
			adjusted_mode->base.id = id;
		}
	}

	return true;
}

static void
nvd0_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	u32 *push;

	if (nv_encoder->crtc) {
		nvd0_crtc_prepare(nv_encoder->crtc);

		push = evo_wait(dev, EVO_MASTER, 4);
		if (push) {
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, dev, EVO_MASTER);
		}

		nvd0_hdmi_disconnect(encoder);

		nv_encoder->crtc = NULL;
		nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
	}
}

static void
nvd0_sor_prepare(struct drm_encoder *encoder)
{
	nvd0_sor_disconnect(encoder);
	if (nouveau_encoder(encoder)->dcb->type == OUTPUT_DP)
		evo_sync(encoder->dev, EVO_MASTER);
}

static void
nvd0_sor_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *umode,
		  struct drm_display_mode *mode)
{
	struct drm_device *dev = encoder->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nvbios *bios = &dev_priv->vbios;
	u32 mode_ctrl = (1 << nv_crtc->index);
	u32 syncs, magic, *push;
	u32 or_config;

	syncs = 0x00000001;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		syncs |= 0x00000008;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		syncs |= 0x00000010;

	magic = 0x31ec6000 | (nv_crtc->index << 25);
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		magic |= 0x00000001;

	nv_connector = nouveau_encoder_connector_get(nv_encoder);
	switch (nv_encoder->dcb->type) {
	case OUTPUT_TMDS:
		if (nv_encoder->dcb->sorconf.link & 1) {
			if (mode->clock < 165000)
				mode_ctrl |= 0x00000100;
			else
				mode_ctrl |= 0x00000500;
		} else {
			mode_ctrl |= 0x00000200;
		}

		or_config = (mode_ctrl & 0x00000f00) >> 8;
		if (mode->clock >= 165000)
			or_config |= 0x0100;

		nvd0_hdmi_mode_set(encoder, mode);
		break;
	case OUTPUT_LVDS:
		or_config = (mode_ctrl & 0x00000f00) >> 8;
		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				or_config |= 0x0100;
			if (bios->fp.if_is_24bit)
				or_config |= 0x0200;
		} else {
			if (nv_connector->type == DCB_CONNECTOR_LVDS_SPWG) {
				if (((u8 *)nv_connector->edid)[121] == 2)
					or_config |= 0x0100;
			} else
			if (mode->clock >= bios->fp.duallink_transition_clk) {
				or_config |= 0x0100;
			}

			if (or_config & 0x0100) {
				if (bios->fp.strapless_is_24bit & 2)
					or_config |= 0x0200;
			} else {
				if (bios->fp.strapless_is_24bit & 1)
					or_config |= 0x0200;
			}

			if (nv_connector->base.display_info.bpc == 8)
				or_config |= 0x0200;

		}
		break;
	case OUTPUT_DP:
		if (nv_connector->base.display_info.bpc == 6) {
			nv_encoder->dp.datarate = mode->clock * 18 / 8;
			syncs |= 0x00000140;
		} else {
			nv_encoder->dp.datarate = mode->clock * 24 / 8;
			syncs |= 0x00000180;
		}

		if (nv_encoder->dcb->sorconf.link & 1)
			mode_ctrl |= 0x00000800;
		else
			mode_ctrl |= 0x00000900;

		or_config = (mode_ctrl & 0x00000f00) >> 8;
		break;
	default:
		BUG_ON(1);
		break;
	}

	nvd0_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	if (nv_encoder->dcb->type == OUTPUT_DP) {
		nvd0_sor_dp_calc_tu(dev, nv_encoder->dcb, nv_crtc->index,
					 nv_encoder->dp.datarate);
	}

	push = evo_wait(dev, EVO_MASTER, 8);
	if (push) {
		evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 2);
		evo_data(push, syncs);
		evo_data(push, magic);
		evo_mthd(push, 0x0200 + (nv_encoder->or * 0x020), 2);
		evo_data(push, mode_ctrl);
		evo_data(push, or_config);
		evo_kick(push, dev, EVO_MASTER);
	}

	nv_encoder->crtc = encoder->crtc;
}

static void
nvd0_sor_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
	kfree(encoder);
}

static const struct drm_encoder_helper_funcs nvd0_sor_hfunc = {
	.dpms = nvd0_sor_dpms,
	.mode_fixup = nvd0_sor_mode_fixup,
	.prepare = nvd0_sor_prepare,
	.commit = nvd0_sor_commit,
	.mode_set = nvd0_sor_mode_set,
	.disable = nvd0_sor_disconnect,
	.get_crtc = nvd0_display_crtc_get,
};

static const struct drm_encoder_funcs nvd0_sor_func = {
	.destroy = nvd0_sor_destroy,
};

static int
nvd0_sor_create(struct drm_connector *connector, struct dcb_entry *dcbe)
{
	struct drm_device *dev = connector->dev;
	struct nouveau_encoder *nv_encoder;
	struct drm_encoder *encoder;

	nv_encoder = kzalloc(sizeof(*nv_encoder), GFP_KERNEL);
	if (!nv_encoder)
		return -ENOMEM;
	nv_encoder->dcb = dcbe;
	nv_encoder->or = ffs(dcbe->or) - 1;
	nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;

	encoder = to_drm_encoder(nv_encoder);
	encoder->possible_crtcs = dcbe->heads;
	encoder->possible_clones = 0;
	drm_encoder_init(dev, encoder, &nvd0_sor_func, DRM_MODE_ENCODER_TMDS);
	drm_encoder_helper_add(encoder, &nvd0_sor_hfunc);

	drm_mode_connector_attach_encoder(connector, encoder);
	return 0;
}

/******************************************************************************
 * IRQ
 *****************************************************************************/
static struct dcb_entry *
lookup_dcb(struct drm_device *dev, int id, u32 mc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int type, or, i, link = -1;

	if (id < 4) {
		type = OUTPUT_ANALOG;
		or   = id;
	} else {
		switch (mc & 0x00000f00) {
		case 0x00000000: link = 0; type = OUTPUT_LVDS; break;
		case 0x00000100: link = 0; type = OUTPUT_TMDS; break;
		case 0x00000200: link = 1; type = OUTPUT_TMDS; break;
		case 0x00000500: link = 0; type = OUTPUT_TMDS; break;
		case 0x00000800: link = 0; type = OUTPUT_DP; break;
		case 0x00000900: link = 1; type = OUTPUT_DP; break;
		default:
			NV_ERROR(dev, "PDISP: unknown SOR mc 0x%08x\n", mc);
			return NULL;
		}

		or = id - 4;
	}

	for (i = 0; i < dev_priv->vbios.dcb.entries; i++) {
		struct dcb_entry *dcb = &dev_priv->vbios.dcb.entry[i];
		if (dcb->type == type && (dcb->or & (1 << or)) &&
		    (link < 0 || link == !(dcb->sorconf.link & 1)))
			return dcb;
	}

	NV_ERROR(dev, "PDISP: DCB for %d/0x%08x not found\n", id, mc);
	return NULL;
}

static void
nvd0_display_unk1_handler(struct drm_device *dev, u32 crtc, u32 mask)
{
	struct dcb_entry *dcb;
	int i;

	for (i = 0; mask && i < 8; i++) {
		u32 mcc = nv_rd32(dev, 0x640180 + (i * 0x20));
		if (!(mcc & (1 << crtc)))
			continue;

		dcb = lookup_dcb(dev, i, mcc);
		if (!dcb)
			continue;

		nouveau_bios_run_display_table(dev, 0x0000, -1, dcb, crtc);
	}

	nv_wr32(dev, 0x6101d4, 0x00000000);
	nv_wr32(dev, 0x6109d4, 0x00000000);
	nv_wr32(dev, 0x6101d0, 0x80000000);
}

static void
nvd0_display_unk2_handler(struct drm_device *dev, u32 crtc, u32 mask)
{
	struct dcb_entry *dcb;
	u32 or, tmp, pclk;
	int i;

	for (i = 0; mask && i < 8; i++) {
		u32 mcc = nv_rd32(dev, 0x640180 + (i * 0x20));
		if (!(mcc & (1 << crtc)))
			continue;

		dcb = lookup_dcb(dev, i, mcc);
		if (!dcb)
			continue;

		nouveau_bios_run_display_table(dev, 0x0000, -2, dcb, crtc);
	}

	pclk = nv_rd32(dev, 0x660450 + (crtc * 0x300)) / 1000;
	NV_DEBUG_KMS(dev, "PDISP: crtc %d pclk %d mask 0x%08x\n",
			  crtc, pclk, mask);
	if (pclk && (mask & 0x00010000)) {
		nv50_crtc_set_clock(dev, crtc, pclk);
	}

	for (i = 0; mask && i < 8; i++) {
		u32 mcp = nv_rd32(dev, 0x660180 + (i * 0x20));
		u32 cfg = nv_rd32(dev, 0x660184 + (i * 0x20));
		if (!(mcp & (1 << crtc)))
			continue;

		dcb = lookup_dcb(dev, i, mcp);
		if (!dcb)
			continue;
		or = ffs(dcb->or) - 1;

		nouveau_bios_run_display_table(dev, cfg, pclk, dcb, crtc);

		nv_wr32(dev, 0x612200 + (crtc * 0x800), 0x00000000);
		switch (dcb->type) {
		case OUTPUT_ANALOG:
			nv_wr32(dev, 0x612280 + (or * 0x800), 0x00000000);
			break;
		case OUTPUT_TMDS:
		case OUTPUT_LVDS:
		case OUTPUT_DP:
			if (cfg & 0x00000100)
				tmp = 0x00000101;
			else
				tmp = 0x00000000;

			nv_mask(dev, 0x612300 + (or * 0x800), 0x00000707, tmp);
			break;
		default:
			break;
		}

		break;
	}

	nv_wr32(dev, 0x6101d4, 0x00000000);
	nv_wr32(dev, 0x6109d4, 0x00000000);
	nv_wr32(dev, 0x6101d0, 0x80000000);
}

static void
nvd0_display_unk4_handler(struct drm_device *dev, u32 crtc, u32 mask)
{
	struct dcb_entry *dcb;
	int pclk, i;

	pclk = nv_rd32(dev, 0x660450 + (crtc * 0x300)) / 1000;

	for (i = 0; mask && i < 8; i++) {
		u32 mcp = nv_rd32(dev, 0x660180 + (i * 0x20));
		u32 cfg = nv_rd32(dev, 0x660184 + (i * 0x20));
		if (!(mcp & (1 << crtc)))
			continue;

		dcb = lookup_dcb(dev, i, mcp);
		if (!dcb)
			continue;

		nouveau_bios_run_display_table(dev, cfg, -pclk, dcb, crtc);
	}

	nv_wr32(dev, 0x6101d4, 0x00000000);
	nv_wr32(dev, 0x6109d4, 0x00000000);
	nv_wr32(dev, 0x6101d0, 0x80000000);
}

static void
nvd0_display_bh(unsigned long data)
{
	struct drm_device *dev = (struct drm_device *)data;
	struct nvd0_display *disp = nvd0_display(dev);
	u32 mask = 0, crtc = ~0;
	int i;

	if (drm_debug & (DRM_UT_DRIVER | DRM_UT_KMS)) {
		NV_INFO(dev, "PDISP: modeset req %d\n", disp->modeset);
		NV_INFO(dev, " STAT: 0x%08x 0x%08x 0x%08x\n",
			 nv_rd32(dev, 0x6101d0),
			 nv_rd32(dev, 0x6101d4), nv_rd32(dev, 0x6109d4));
		for (i = 0; i < 8; i++) {
			NV_INFO(dev, " %s%d: 0x%08x 0x%08x\n",
				i < 4 ? "DAC" : "SOR", i,
				nv_rd32(dev, 0x640180 + (i * 0x20)),
				nv_rd32(dev, 0x660180 + (i * 0x20)));
		}
	}

	while (!mask && ++crtc < dev->mode_config.num_crtc)
		mask = nv_rd32(dev, 0x6101d4 + (crtc * 0x800));

	if (disp->modeset & 0x00000001)
		nvd0_display_unk1_handler(dev, crtc, mask);
	if (disp->modeset & 0x00000002)
		nvd0_display_unk2_handler(dev, crtc, mask);
	if (disp->modeset & 0x00000004)
		nvd0_display_unk4_handler(dev, crtc, mask);
}

static void
nvd0_display_intr(struct drm_device *dev)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 intr = nv_rd32(dev, 0x610088);
	int i;

	if (intr & 0x00000001) {
		u32 stat = nv_rd32(dev, 0x61008c);
		nv_wr32(dev, 0x61008c, stat);
		intr &= ~0x00000001;
	}

	if (intr & 0x00000002) {
		u32 stat = nv_rd32(dev, 0x61009c);
		int chid = ffs(stat) - 1;
		if (chid >= 0) {
			u32 mthd = nv_rd32(dev, 0x6101f0 + (chid * 12));
			u32 data = nv_rd32(dev, 0x6101f4 + (chid * 12));
			u32 unkn = nv_rd32(dev, 0x6101f8 + (chid * 12));

			NV_INFO(dev, "EvoCh: chid %d mthd 0x%04x data 0x%08x "
				     "0x%08x 0x%08x\n",
				chid, (mthd & 0x0000ffc), data, mthd, unkn);
			nv_wr32(dev, 0x61009c, (1 << chid));
			nv_wr32(dev, 0x6101f0 + (chid * 12), 0x90000000);
		}

		intr &= ~0x00000002;
	}

	if (intr & 0x00100000) {
		u32 stat = nv_rd32(dev, 0x6100ac);

		if (stat & 0x00000007) {
			disp->modeset = stat;
			tasklet_schedule(&disp->tasklet);

			nv_wr32(dev, 0x6100ac, (stat & 0x00000007));
			stat &= ~0x00000007;
		}

		if (stat) {
			NV_INFO(dev, "PDISP: unknown intr24 0x%08x\n", stat);
			nv_wr32(dev, 0x6100ac, stat);
		}

		intr &= ~0x00100000;
	}

	for (i = 0; i < dev->mode_config.num_crtc; i++) {
		u32 mask = 0x01000000 << i;
		if (intr & mask) {
			u32 stat = nv_rd32(dev, 0x6100bc + (i * 0x800));
			nv_wr32(dev, 0x6100bc + (i * 0x800), stat);
			intr &= ~mask;
		}
	}

	if (intr)
		NV_INFO(dev, "PDISP: unknown intr 0x%08x\n", intr);
}

/******************************************************************************
 * Init
 *****************************************************************************/
void
nvd0_display_fini(struct drm_device *dev)
{
	int i;

	/* fini cursors + overlays + flips */
	for (i = 1; i >= 0; i--) {
		evo_fini_pio(dev, EVO_CURS(i));
		evo_fini_pio(dev, EVO_OIMM(i));
		evo_fini_dma(dev, EVO_OVLY(i));
		evo_fini_dma(dev, EVO_FLIP(i));
	}

	/* fini master */
	evo_fini_dma(dev, EVO_MASTER);
}

int
nvd0_display_init(struct drm_device *dev)
{
	struct nvd0_display *disp = nvd0_display(dev);
	int ret, i;
	u32 *push;

	if (nv_rd32(dev, 0x6100ac) & 0x00000100) {
		nv_wr32(dev, 0x6100ac, 0x00000100);
		nv_mask(dev, 0x6194e8, 0x00000001, 0x00000000);
		if (!nv_wait(dev, 0x6194e8, 0x00000002, 0x00000000)) {
			NV_ERROR(dev, "PDISP: 0x6194e8 0x%08x\n",
				 nv_rd32(dev, 0x6194e8));
			return -EBUSY;
		}
	}

	/* nfi what these are exactly, i do know that SOR_MODE_CTRL won't
	 * work at all unless you do the SOR part below.
	 */
	for (i = 0; i < 3; i++) {
		u32 dac = nv_rd32(dev, 0x61a000 + (i * 0x800));
		nv_wr32(dev, 0x6101c0 + (i * 0x800), dac);
	}

	for (i = 0; i < 4; i++) {
		u32 sor = nv_rd32(dev, 0x61c000 + (i * 0x800));
		nv_wr32(dev, 0x6301c4 + (i * 0x800), sor);
	}

	for (i = 0; i < dev->mode_config.num_crtc; i++) {
		u32 crtc0 = nv_rd32(dev, 0x616104 + (i * 0x800));
		u32 crtc1 = nv_rd32(dev, 0x616108 + (i * 0x800));
		u32 crtc2 = nv_rd32(dev, 0x61610c + (i * 0x800));
		nv_wr32(dev, 0x6101b4 + (i * 0x800), crtc0);
		nv_wr32(dev, 0x6101b8 + (i * 0x800), crtc1);
		nv_wr32(dev, 0x6101bc + (i * 0x800), crtc2);
	}

	/* point at our hash table / objects, enable interrupts */
	nv_wr32(dev, 0x610010, (disp->mem->vinst >> 8) | 9);
	nv_mask(dev, 0x6100b0, 0x00000307, 0x00000307);

	/* init master */
	ret = evo_init_dma(dev, EVO_MASTER);
	if (ret)
		goto error;

	/* init flips + overlays + cursors */
	for (i = 0; i < dev->mode_config.num_crtc; i++) {
		if ((ret = evo_init_dma(dev, EVO_FLIP(i))) ||
		    (ret = evo_init_dma(dev, EVO_OVLY(i))) ||
		    (ret = evo_init_pio(dev, EVO_OIMM(i))) ||
		    (ret = evo_init_pio(dev, EVO_CURS(i))))
			goto error;
	}

	push = evo_wait(dev, EVO_MASTER, 32);
	if (!push) {
		ret = -EBUSY;
		goto error;
	}
	evo_mthd(push, 0x0088, 1);
	evo_data(push, NvEvoSync);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x80000000);
	evo_mthd(push, 0x008c, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, dev, EVO_MASTER);

error:
	if (ret)
		nvd0_display_fini(dev);
	return ret;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp = nvd0_display(dev);
	struct pci_dev *pdev = dev->pdev;
	int i;

	for (i = 0; i < EVO_DMA_NR; i++) {
		struct evo *evo = &disp->evo[i];
		pci_free_consistent(pdev, PAGE_SIZE, evo->ptr, evo->handle);
	}

	nouveau_gpuobj_ref(NULL, &disp->mem);
	nouveau_bo_unmap(disp->sync);
	nouveau_bo_ref(NULL, &disp->sync);
	nouveau_irq_unregister(dev, 26);

	dev_priv->engine.display.priv = NULL;
	kfree(disp);
}

int
nvd0_display_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct dcb_table *dcb = &dev_priv->vbios.dcb;
	struct drm_connector *connector, *tmp;
	struct pci_dev *pdev = dev->pdev;
	struct nvd0_display *disp;
	struct dcb_entry *dcbe;
	int crtcs, ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	dev_priv->engine.display.priv = disp;

	/* create crtc objects to represent the hw heads */
	crtcs = nv_rd32(dev, 0x022448);
	for (i = 0; i < crtcs; i++) {
		ret = nvd0_crtc_create(dev, i);
		if (ret)
			goto out;
	}

	/* create encoder/connector objects based on VBIOS DCB table */
	for (i = 0, dcbe = &dcb->entry[0]; i < dcb->entries; i++, dcbe++) {
		connector = nouveau_connector_create(dev, dcbe->connector);
		if (IS_ERR(connector))
			continue;

		if (dcbe->location != DCB_LOC_ON_CHIP) {
			NV_WARN(dev, "skipping off-chip encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}

		switch (dcbe->type) {
		case OUTPUT_TMDS:
		case OUTPUT_LVDS:
		case OUTPUT_DP:
			nvd0_sor_create(connector, dcbe);
			break;
		case OUTPUT_ANALOG:
			nvd0_dac_create(connector, dcbe);
			break;
		default:
			NV_WARN(dev, "skipping unsupported encoder %d/%d\n",
				dcbe->type, ffs(dcbe->or) - 1);
			continue;
		}
	}

	/* cull any connectors we created that don't have an encoder */
	list_for_each_entry_safe(connector, tmp, &dev->mode_config.connector_list, head) {
		if (connector->encoder_ids[0])
			continue;

		NV_WARN(dev, "%s has no encoders, removing\n",
			drm_get_connector_name(connector));
		connector->funcs->destroy(connector);
	}

	/* setup interrupt handling */
	tasklet_init(&disp->tasklet, nvd0_display_bh, (unsigned long)dev);
	nouveau_irq_register(dev, 26, nvd0_display_intr);

	/* small shared memory area we use for notifiers and semaphores */
	ret = nouveau_bo_new(dev, 4096, 0x1000, TTM_PL_FLAG_VRAM,
			     0, 0x0000, NULL, &disp->sync);
	if (!ret) {
		ret = nouveau_bo_pin(disp->sync, TTM_PL_FLAG_VRAM);
		if (!ret)
			ret = nouveau_bo_map(disp->sync);
		if (ret)
			nouveau_bo_ref(NULL, &disp->sync);
	}

	if (ret)
		goto out;

	/* hash table and dma objects for the memory areas we care about */
	ret = nouveau_gpuobj_new(dev, NULL, 0x4000, 0x10000,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->mem);
	if (ret)
		goto out;

	/* create evo dma channels */
	for (i = 0; i < EVO_DMA_NR; i++) {
		struct evo *evo = &disp->evo[i];
		u64 offset = disp->sync->bo.offset;
		u32 dmao = 0x1000 + (i * 0x100);
		u32 hash = 0x0000 + (i * 0x040);

		evo->idx = i;
		evo->sem.offset = EVO_SYNC(evo->idx, 0x00);
		evo->ptr = pci_alloc_consistent(pdev, PAGE_SIZE, &evo->handle);
		if (!evo->ptr) {
			ret = -ENOMEM;
			goto out;
		}

		nv_wo32(disp->mem, dmao + 0x00, 0x00000049);
		nv_wo32(disp->mem, dmao + 0x04, (offset + 0x0000) >> 8);
		nv_wo32(disp->mem, dmao + 0x08, (offset + 0x0fff) >> 8);
		nv_wo32(disp->mem, dmao + 0x0c, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x10, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x14, 0x00000000);
		nv_wo32(disp->mem, hash + 0x00, NvEvoSync);
		nv_wo32(disp->mem, hash + 0x04, 0x00000001 | (i << 27) |
						((dmao + 0x00) << 9));

		nv_wo32(disp->mem, dmao + 0x20, 0x00000049);
		nv_wo32(disp->mem, dmao + 0x24, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x28, (dev_priv->vram_size - 1) >> 8);
		nv_wo32(disp->mem, dmao + 0x2c, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x30, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x34, 0x00000000);
		nv_wo32(disp->mem, hash + 0x08, NvEvoVRAM);
		nv_wo32(disp->mem, hash + 0x0c, 0x00000001 | (i << 27) |
						((dmao + 0x20) << 9));

		nv_wo32(disp->mem, dmao + 0x40, 0x00000009);
		nv_wo32(disp->mem, dmao + 0x44, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x48, (dev_priv->vram_size - 1) >> 8);
		nv_wo32(disp->mem, dmao + 0x4c, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x50, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x54, 0x00000000);
		nv_wo32(disp->mem, hash + 0x10, NvEvoVRAM_LP);
		nv_wo32(disp->mem, hash + 0x14, 0x00000001 | (i << 27) |
						((dmao + 0x40) << 9));

		nv_wo32(disp->mem, dmao + 0x60, 0x0fe00009);
		nv_wo32(disp->mem, dmao + 0x64, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x68, (dev_priv->vram_size - 1) >> 8);
		nv_wo32(disp->mem, dmao + 0x6c, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x70, 0x00000000);
		nv_wo32(disp->mem, dmao + 0x74, 0x00000000);
		nv_wo32(disp->mem, hash + 0x18, NvEvoFB32);
		nv_wo32(disp->mem, hash + 0x1c, 0x00000001 | (i << 27) |
						((dmao + 0x60) << 9));
	}

	pinstmem->flush(dev);

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
