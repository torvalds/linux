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

struct nvd0_display {
	struct nouveau_gpuobj *mem;
	struct {
		dma_addr_t handle;
		u32 *ptr;
	} evo[1];

	struct tasklet_struct tasklet;
	u32 modeset;
};

static struct nvd0_display *
nvd0_display(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	return dev_priv->engine.display.priv;
}

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

	return disp->evo[id].ptr + put;
}

static void
evo_kick(u32 *push, struct drm_device *dev, int id)
{
	struct nvd0_display *disp = nvd0_display(dev);
	nv_wr32(dev, 0x640000 + (id * 0x1000), (push - disp->evo[id].ptr) << 2);
}

#define evo_mthd(p,m,s) *((p)++) = (((s) << 18) | (m))
#define evo_data(p,d)   *((p)++) = (d)

static struct drm_crtc *
nvd0_display_crtc_get(struct drm_encoder *encoder)
{
	return nouveau_encoder(encoder)->crtc;
}

/******************************************************************************
 * CRTC
 *****************************************************************************/
static int
nvd0_crtc_set_dither(struct nouveau_crtc *nv_crtc, bool on, bool update)
{
	struct drm_device *dev = nv_crtc->base.dev;
	u32 *push, mode;

	mode = 0x00000000;
	if (on) {
		/* 0x11: 6bpc dynamic 2x2
		 * 0x13: 8bpc dynamic 2x2
		 * 0x19: 6bpc static 2x2
		 * 0x1b: 8bpc static 2x2
		 * 0x21: 6bpc temporal
		 * 0x23: 8bpc temporal
		 */
		mode = 0x00000011;
	}

	push = evo_wait(dev, 0, 4);
	if (push) {
		evo_mthd(push, 0x0490 + (nv_crtc->index * 0x300), 1);
		evo_data(push, mode);
		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, dev, 0);
	}

	return 0;
}

static int
nvd0_crtc_set_scale(struct nouveau_crtc *nv_crtc, int type, bool update)
{
	struct drm_display_mode *mode = &nv_crtc->base.mode;
	struct drm_device *dev = nv_crtc->base.dev;
	struct nouveau_connector *nv_connector;
	u32 *push, outX, outY;

	outX = mode->hdisplay;
	outY = mode->vdisplay;

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	if (nv_connector && nv_connector->native_mode) {
		struct drm_display_mode *native = nv_connector->native_mode;
		u32 xratio = (native->hdisplay << 19) / mode->hdisplay;
		u32 yratio = (native->vdisplay << 19) / mode->vdisplay;

		switch (type) {
		case DRM_MODE_SCALE_ASPECT:
			if (xratio > yratio) {
				outX = (mode->hdisplay * yratio) >> 19;
				outY = (mode->vdisplay * yratio) >> 19;
			} else {
				outX = (mode->hdisplay * xratio) >> 19;
				outY = (mode->vdisplay * xratio) >> 19;
			}
			break;
		case DRM_MODE_SCALE_FULLSCREEN:
			outX = native->hdisplay;
			outY = native->vdisplay;
			break;
		default:
			break;
		}
	}

	push = evo_wait(dev, 0, 16);
	if (push) {
		evo_mthd(push, 0x04c0 + (nv_crtc->index * 0x300), 3);
		evo_data(push, (outY << 16) | outX);
		evo_data(push, (outY << 16) | outX);
		evo_data(push, (outY << 16) | outX);
		evo_mthd(push, 0x0494 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x04b8 + (nv_crtc->index * 0x300), 1);
		evo_data(push, (mode->vdisplay << 16) | mode->hdisplay);
		if (update) {
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, dev, 0);
	}

	return 0;
}

static int
nvd0_crtc_set_image(struct nouveau_crtc *nv_crtc, struct drm_framebuffer *fb,
		    int x, int y, bool update)
{
	struct nouveau_framebuffer *nvfb = nouveau_framebuffer(fb);
	u32 *push;

	push = evo_wait(fb->dev, 0, 16);
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
		evo_kick(push, fb->dev, 0);
	}

	nv_crtc->fb.tile_flags = nvfb->r_dma;
	return 0;
}

static void
nvd0_crtc_cursor_show(struct nouveau_crtc *nv_crtc, bool show, bool update)
{
	struct drm_device *dev = nv_crtc->base.dev;
	u32 *push = evo_wait(dev, 0, 16);
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

		evo_kick(push, dev, 0);
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

	push = evo_wait(crtc->dev, 0, 2);
	if (push) {
		evo_mthd(push, 0x0474 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0440 + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x03000000);
		evo_mthd(push, 0x045c + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000);
		evo_kick(push, crtc->dev, 0);
	}

	nvd0_crtc_cursor_show(nv_crtc, false, false);
}

static void
nvd0_crtc_commit(struct drm_crtc *crtc)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
	u32 *push;

	push = evo_wait(crtc->dev, 0, 32);
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
		evo_kick(push, crtc->dev, 0);
	}

	nvd0_crtc_cursor_show(nv_crtc, nv_crtc->cursor.visible, true);
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
	u32 htotal = mode->htotal;
	u32 vtotal = mode->vtotal;
	u32 hsyncw = mode->hsync_end - mode->hsync_start - 1;
	u32 vsyncw = mode->vsync_end - mode->vsync_start - 1;
	u32 hfrntp = mode->hsync_start - mode->hdisplay;
	u32 vfrntp = mode->vsync_start - mode->vdisplay;
	u32 hbackp = mode->htotal - mode->hsync_end;
	u32 vbackp = mode->vtotal - mode->vsync_end;
	u32 hss2be = hsyncw + hbackp;
	u32 vss2be = vsyncw + vbackp;
	u32 hss2de = htotal - hfrntp;
	u32 vss2de = vtotal - vfrntp;
	u32 syncs, *push;
	int ret;

	syncs = 0x00000001;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		syncs |= 0x00000008;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		syncs |= 0x00000010;

	ret = nvd0_crtc_swap_fbs(crtc, old_fb);
	if (ret)
		return ret;

	push = evo_wait(crtc->dev, 0, 64);
	if (push) {
		evo_mthd(push, 0x0410 + (nv_crtc->index * 0x300), 5);
		evo_data(push, 0x00000000);
		evo_data(push, (vtotal << 16) | htotal);
		evo_data(push, (vsyncw << 16) | hsyncw);
		evo_data(push, (vss2be << 16) | hss2be);
		evo_data(push, (vss2de << 16) | hss2de);
		evo_mthd(push, 0x042c + (nv_crtc->index * 0x300), 1);
		evo_data(push, 0x00000000); /* ??? */
		evo_mthd(push, 0x0450 + (nv_crtc->index * 0x300), 3);
		evo_data(push, mode->clock * 1000);
		evo_data(push, 0x00200000); /* ??? */
		evo_data(push, mode->clock * 1000);
		evo_mthd(push, 0x0404 + (nv_crtc->index * 0x300), 1);
		evo_data(push, syncs);
		evo_kick(push, crtc->dev, 0);
	}

	nv_connector = nouveau_crtc_connector_get(nv_crtc);
	nvd0_crtc_set_dither(nv_crtc, nv_connector->use_dithering, false);
	nvd0_crtc_set_scale(nv_crtc, nv_connector->scaling_mode, false);
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

	nvd0_crtc_set_image(nv_crtc, crtc->fb, x, y, true);
	return 0;
}

static int
nvd0_crtc_mode_set_base_atomic(struct drm_crtc *crtc,
			       struct drm_framebuffer *fb, int x, int y,
			       enum mode_set_atomic state)
{
	struct nouveau_crtc *nv_crtc = nouveau_crtc(crtc);
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
	const u32 data = (y << 16) | x;

	nv_wr32(crtc->dev, 0x64d084 + (nv_crtc->index * 0x1000), data);
	nv_wr32(crtc->dev, 0x64d080 + (nv_crtc->index * 0x1000), 0x00000000);
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
			     0, 0x0000, &nv_crtc->cursor.nvbo);
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
			     0, 0x0000, &nv_crtc->lut.nvbo);
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
nvd0_dac_prepare(struct drm_encoder *encoder)
{
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
	u32 *push;

	nvd0_dac_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(encoder->dev, 0, 4);
	if (push) {
		evo_mthd(push, 0x0180 + (nv_encoder->or * 0x20), 2);
		evo_data(push, 1 << nv_crtc->index);
		evo_data(push, 0x00ff);
		evo_kick(push, encoder->dev, 0);
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

		push = evo_wait(dev, 0, 4);
		if (push) {
			evo_mthd(push, 0x0180 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, dev, 0);
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
	.prepare = nvd0_dac_prepare,
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
 * SOR
 *****************************************************************************/
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
nvd0_sor_prepare(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_commit(struct drm_encoder *encoder)
{
}

static void
nvd0_sor_mode_set(struct drm_encoder *encoder, struct drm_display_mode *umode,
		  struct drm_display_mode *mode)
{
	struct drm_nouveau_private *dev_priv = encoder->dev->dev_private;
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct nouveau_crtc *nv_crtc = nouveau_crtc(encoder->crtc);
	struct nouveau_connector *nv_connector;
	struct nvbios *bios = &dev_priv->vbios;
	u32 mode_ctrl = (1 << nv_crtc->index);
	u32 *push, or_config;

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
		break;
	case OUTPUT_LVDS:
		or_config = (mode_ctrl & 0x00000f00) >> 8;
		if (bios->fp_no_ddc) {
			if (bios->fp.dual_link)
				or_config |= 0x0100;
			if (bios->fp.if_is_24bit)
				or_config |= 0x0200;
		} else {
			if (nv_connector->dcb->type == DCB_CONNECTOR_LVDS_SPWG) {
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
	default:
		BUG_ON(1);
		break;
	}

	nvd0_sor_dpms(encoder, DRM_MODE_DPMS_ON);

	push = evo_wait(encoder->dev, 0, 4);
	if (push) {
		evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 2);
		evo_data(push, mode_ctrl);
		evo_data(push, or_config);
		evo_kick(push, encoder->dev, 0);
	}

	nv_encoder->crtc = encoder->crtc;
}

static void
nvd0_sor_disconnect(struct drm_encoder *encoder)
{
	struct nouveau_encoder *nv_encoder = nouveau_encoder(encoder);
	struct drm_device *dev = encoder->dev;
	u32 *push;

	if (nv_encoder->crtc) {
		nvd0_crtc_prepare(nv_encoder->crtc);

		push = evo_wait(dev, 0, 4);
		if (push) {
			evo_mthd(push, 0x0200 + (nv_encoder->or * 0x20), 1);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x0080, 1);
			evo_data(push, 0x00000000);
			evo_kick(push, dev, 0);
		}

		nv_encoder->crtc = NULL;
		nv_encoder->last_dpms = DRM_MODE_DPMS_OFF;
	}
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
	int type, or, i;

	if (id < 4) {
		type = OUTPUT_ANALOG;
		or   = id;
	} else {
		switch (mc & 0x00000f00) {
		case 0x00000000: type = OUTPUT_LVDS; break;
		case 0x00000100: type = OUTPUT_TMDS; break;
		case 0x00000200: type = OUTPUT_TMDS; break;
		case 0x00000500: type = OUTPUT_TMDS; break;
		default:
			NV_ERROR(dev, "PDISP: unknown SOR mc 0x%08x\n", mc);
			return NULL;
		}

		or = id - 4;
	}

	for (i = 0; i < dev_priv->vbios.dcb.entries; i++) {
		struct dcb_entry *dcb = &dev_priv->vbios.dcb.entry[i];
		if (dcb->type == type && (dcb->or & (1 << or)))
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
	if (mask & 0x00010000) {
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
	u32 mask, crtc;
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

	mask = nv_rd32(dev, 0x6101d4);
	crtc = 0;
	if (!mask) {
		mask = nv_rd32(dev, 0x6109d4);
		crtc = 1;
	}

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

	if (intr & 0x01000000) {
		u32 stat = nv_rd32(dev, 0x6100bc);
		nv_wr32(dev, 0x6100bc, stat);
		intr &= ~0x01000000;
	}

	if (intr & 0x02000000) {
		u32 stat = nv_rd32(dev, 0x6108bc);
		nv_wr32(dev, 0x6108bc, stat);
		intr &= ~0x02000000;
	}

	if (intr)
		NV_INFO(dev, "PDISP: unknown intr 0x%08x\n", intr);
}

/******************************************************************************
 * Init
 *****************************************************************************/
static void
nvd0_display_fini(struct drm_device *dev)
{
	int i;

	/* fini cursors */
	for (i = 14; i >= 13; i--) {
		if (!(nv_rd32(dev, 0x610490 + (i * 0x10)) & 0x00000001))
			continue;

		nv_mask(dev, 0x610490 + (i * 0x10), 0x00000001, 0x00000000);
		nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00000000);
		nv_mask(dev, 0x610090, 1 << i, 0x00000000);
		nv_mask(dev, 0x6100a0, 1 << i, 0x00000000);
	}

	/* fini master */
	if (nv_rd32(dev, 0x610490) & 0x00000010) {
		nv_mask(dev, 0x610490, 0x00000010, 0x00000000);
		nv_mask(dev, 0x610490, 0x00000003, 0x00000000);
		nv_wait(dev, 0x610490, 0x80000000, 0x00000000);
		nv_mask(dev, 0x610090, 0x00000001, 0x00000000);
		nv_mask(dev, 0x6100a0, 0x00000001, 0x00000000);
	}
}

int
nvd0_display_init(struct drm_device *dev)
{
	struct nvd0_display *disp = nvd0_display(dev);
	u32 *push;
	int i;

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

	for (i = 0; i < 2; i++) {
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
	nv_wr32(dev, 0x610494, (disp->evo[0].handle >> 8) | 3);
	nv_wr32(dev, 0x610498, 0x00010000);
	nv_wr32(dev, 0x61049c, 0x00000001);
	nv_mask(dev, 0x610490, 0x00000010, 0x00000010);
	nv_wr32(dev, 0x640000, 0x00000000);
	nv_wr32(dev, 0x610490, 0x01000013);
	if (!nv_wait(dev, 0x610490, 0x80000000, 0x00000000)) {
		NV_ERROR(dev, "PDISP: master 0x%08x\n",
			 nv_rd32(dev, 0x610490));
		return -EBUSY;
	}
	nv_mask(dev, 0x610090, 0x00000001, 0x00000001);
	nv_mask(dev, 0x6100a0, 0x00000001, 0x00000001);

	/* init cursors */
	for (i = 13; i <= 14; i++) {
		nv_wr32(dev, 0x610490 + (i * 0x10), 0x00000001);
		if (!nv_wait(dev, 0x610490 + (i * 0x10), 0x00010000, 0x00010000)) {
			NV_ERROR(dev, "PDISP: curs%d 0x%08x\n", i,
				 nv_rd32(dev, 0x610490 + (i * 0x10)));
			return -EBUSY;
		}

		nv_mask(dev, 0x610090, 1 << i, 1 << i);
		nv_mask(dev, 0x6100a0, 1 << i, 1 << i);
	}

	push = evo_wait(dev, 0, 32);
	if (!push)
		return -EBUSY;
	evo_mthd(push, 0x0088, 1);
	evo_data(push, NvEvoSync);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x00000000);
	evo_mthd(push, 0x0084, 1);
	evo_data(push, 0x80000000);
	evo_mthd(push, 0x008c, 1);
	evo_data(push, 0x00000000);
	evo_kick(push, dev, 0);

	return 0;
}

void
nvd0_display_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvd0_display *disp = nvd0_display(dev);
	struct pci_dev *pdev = dev->pdev;

	nvd0_display_fini(dev);

	pci_free_consistent(pdev, PAGE_SIZE, disp->evo[0].ptr, disp->evo[0].handle);
	nouveau_gpuobj_ref(NULL, &disp->mem);
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
	int ret, i;

	disp = kzalloc(sizeof(*disp), GFP_KERNEL);
	if (!disp)
		return -ENOMEM;
	dev_priv->engine.display.priv = disp;

	/* create crtc objects to represent the hw heads */
	for (i = 0; i < 2; i++) {
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

	/* hash table and dma objects for the memory areas we care about */
	ret = nouveau_gpuobj_new(dev, NULL, 0x4000, 0x10000,
				 NVOBJ_FLAG_ZERO_ALLOC, &disp->mem);
	if (ret)
		goto out;

	nv_wo32(disp->mem, 0x1000, 0x00000049);
	nv_wo32(disp->mem, 0x1004, (disp->mem->vinst + 0x2000) >> 8);
	nv_wo32(disp->mem, 0x1008, (disp->mem->vinst + 0x2fff) >> 8);
	nv_wo32(disp->mem, 0x100c, 0x00000000);
	nv_wo32(disp->mem, 0x1010, 0x00000000);
	nv_wo32(disp->mem, 0x1014, 0x00000000);
	nv_wo32(disp->mem, 0x0000, NvEvoSync);
	nv_wo32(disp->mem, 0x0004, (0x1000 << 9) | 0x00000001);

	nv_wo32(disp->mem, 0x1020, 0x00000049);
	nv_wo32(disp->mem, 0x1024, 0x00000000);
	nv_wo32(disp->mem, 0x1028, (dev_priv->vram_size - 1) >> 8);
	nv_wo32(disp->mem, 0x102c, 0x00000000);
	nv_wo32(disp->mem, 0x1030, 0x00000000);
	nv_wo32(disp->mem, 0x1034, 0x00000000);
	nv_wo32(disp->mem, 0x0008, NvEvoVRAM);
	nv_wo32(disp->mem, 0x000c, (0x1020 << 9) | 0x00000001);

	nv_wo32(disp->mem, 0x1040, 0x00000009);
	nv_wo32(disp->mem, 0x1044, 0x00000000);
	nv_wo32(disp->mem, 0x1048, (dev_priv->vram_size - 1) >> 8);
	nv_wo32(disp->mem, 0x104c, 0x00000000);
	nv_wo32(disp->mem, 0x1050, 0x00000000);
	nv_wo32(disp->mem, 0x1054, 0x00000000);
	nv_wo32(disp->mem, 0x0010, NvEvoVRAM_LP);
	nv_wo32(disp->mem, 0x0014, (0x1040 << 9) | 0x00000001);

	nv_wo32(disp->mem, 0x1060, 0x0fe00009);
	nv_wo32(disp->mem, 0x1064, 0x00000000);
	nv_wo32(disp->mem, 0x1068, (dev_priv->vram_size - 1) >> 8);
	nv_wo32(disp->mem, 0x106c, 0x00000000);
	nv_wo32(disp->mem, 0x1070, 0x00000000);
	nv_wo32(disp->mem, 0x1074, 0x00000000);
	nv_wo32(disp->mem, 0x0018, NvEvoFB32);
	nv_wo32(disp->mem, 0x001c, (0x1060 << 9) | 0x00000001);

	pinstmem->flush(dev);

	/* push buffers for evo channels */
	disp->evo[0].ptr =
		pci_alloc_consistent(pdev, PAGE_SIZE, &disp->evo[0].handle);
	if (!disp->evo[0].ptr) {
		ret = -ENOMEM;
		goto out;
	}

	ret = nvd0_display_init(dev);
	if (ret)
		goto out;

out:
	if (ret)
		nvd0_display_destroy(dev);
	return ret;
}
