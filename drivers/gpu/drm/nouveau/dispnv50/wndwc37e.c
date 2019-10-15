/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "wndw.h"
#include "atom.h"

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <nouveau_bo.h>

#include <nvif/clc37e.h>

static void
wndwc37e_csc_clr(struct nv50_wndw *wndw)
{
}

static void
wndwc37e_csc_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push, i;
	if ((push = evo_wait(&wndw->wndw, 13))) {
		 evo_mthd(push, 0x02bc, 12);
		 for (i = 0; i < 12; i++)
			  evo_data(push, asyw->csc.matrix[i]);
		 evo_kick(push, &wndw->wndw);
	}
}

static void
wndwc37e_ilut_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x02b8, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
wndwc37e_ilut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 4))) {
		evo_mthd(push, 0x02b0, 3);
		evo_data(push, asyw->xlut.i.output_mode << 8 |
			       asyw->xlut.i.range << 4 |
			       asyw->xlut.i.size);
		evo_data(push, asyw->xlut.i.offset >> 8);
		evo_data(push, asyw->xlut.handle);
		evo_kick(push, &wndw->wndw);
	}
}

static void
wndwc37e_ilut(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	asyw->xlut.i.mode = 2;
	asyw->xlut.i.size = 0;
	asyw->xlut.i.range = 0;
	asyw->xlut.i.output_mode = 1;
	asyw->xlut.i.load = head907d_olut_load;
}

void
wndwc37e_blend_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 8))) {
		evo_mthd(push, 0x02ec, 7);
		evo_data(push, asyw->blend.depth << 4);
		evo_data(push, asyw->blend.k1);
		evo_data(push, asyw->blend.dst_color << 12 |
			       asyw->blend.dst_color << 8 |
			       asyw->blend.src_color << 4 |
			       asyw->blend.src_color);
		evo_data(push, 0xffff0000);
		evo_data(push, 0xffff0000);
		evo_data(push, 0xffff0000);
		evo_data(push, 0xffff0000);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_image_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 4))) {
		evo_mthd(push, 0x0308, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x0240, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
wndwc37e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;

	if (!(push = evo_wait(&wndw->wndw, 17)))
		return;

	evo_mthd(push, 0x0308, 1);
	evo_data(push, asyw->image.mode << 4 | asyw->image.interval);
	evo_mthd(push, 0x0224, 4);
	evo_data(push, asyw->image.h << 16 | asyw->image.w);
	evo_data(push, asyw->image.layout << 4 | asyw->image.blockh);
	evo_data(push, asyw->csc.valid << 17 |
		       asyw->image.colorspace << 8 |
		       asyw->image.format);
	evo_data(push, asyw->image.blocks[0] | (asyw->image.pitch[0] >> 6));
	evo_mthd(push, 0x0240, 1);
	evo_data(push, asyw->image.handle[0]);
	evo_mthd(push, 0x0260, 1);
	evo_data(push, asyw->image.offset[0] >> 8);
	evo_mthd(push, 0x0290, 1);
	evo_data(push, (asyw->state.src_y >> 16) << 16 |
		       (asyw->state.src_x >> 16));
	evo_mthd(push, 0x0298, 1);
	evo_data(push, (asyw->state.src_h >> 16) << 16 |
		       (asyw->state.src_w >> 16));
	evo_mthd(push, 0x02a4, 1);
	evo_data(push, asyw->state.crtc_h << 16 |
		       asyw->state.crtc_w);
	evo_kick(push, &wndw->wndw);
}

void
wndwc37e_ntfy_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x021c, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_ntfy_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 3))) {
		evo_mthd(push, 0x021c, 2);
		evo_data(push, asyw->ntfy.handle);
		evo_data(push, asyw->ntfy.offset | asyw->ntfy.awaken);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_sema_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0218, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_sema_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 5))) {
		evo_mthd(push, 0x020c, 4);
		evo_data(push, asyw->sema.offset);
		evo_data(push, asyw->sema.acquire);
		evo_data(push, asyw->sema.release);
		evo_data(push, asyw->sema.handle);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_update(struct nv50_wndw *wndw, u32 *interlock)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 5))) {
		evo_mthd(push, 0x0370, 2);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_CURS] << 1 |
			       interlock[NV50_DISP_INTERLOCK_CORE]);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_WNDW]);
		evo_mthd(push, 0x0200, 1);
		if (interlock[NV50_DISP_INTERLOCK_WIMM] & wndw->interlock.data)
			evo_data(push, 0x00001001);
		else
			evo_data(push, 0x00000001);
		evo_kick(push, &wndw->wndw);
	}
}

void
wndwc37e_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
}

int
wndwc37e_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	return drm_atomic_helper_check_plane_state(&asyw->state, &asyh->state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   true, true);
}

static const u32
wndwc37e_format[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XRGB2101010,
	DRM_FORMAT_ARGB2101010,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_ABGR16161616F,
	0
};

static const struct nv50_wndw_func
wndwc37e = {
	.acquire = wndwc37e_acquire,
	.release = wndwc37e_release,
	.sema_set = wndwc37e_sema_set,
	.sema_clr = wndwc37e_sema_clr,
	.ntfy_set = wndwc37e_ntfy_set,
	.ntfy_clr = wndwc37e_ntfy_clr,
	.ntfy_reset = corec37d_ntfy_init,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.ilut = wndwc37e_ilut,
	.xlut_set = wndwc37e_ilut_set,
	.xlut_clr = wndwc37e_ilut_clr,
	.csc = base907c_csc,
	.csc_set = wndwc37e_csc_set,
	.csc_clr = wndwc37e_csc_clr,
	.image_set = wndwc37e_image_set,
	.image_clr = wndwc37e_image_clr,
	.blend_set = wndwc37e_blend_set,
	.update = wndwc37e_update,
};

int
wndwc37e_new_(const struct nv50_wndw_func *func, struct nouveau_drm *drm,
	      enum drm_plane_type type, int index, s32 oclass, u32 heads,
	      struct nv50_wndw **pwndw)
{
	struct nvc37e_window_channel_dma_v0 args = {
		.pushbuf = 0xb0007e00 | index,
		.index = index,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(func, drm->dev, type, "wndw", index,
			     wndwc37e_format, heads, NV50_DISP_INTERLOCK_WNDW,
			     BIT(index), &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args),
			       disp->sync->bo.offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "qndw%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	wndw->ntfy = NV50_DISP_WNDW_NTFY(wndw->id);
	wndw->sema = NV50_DISP_WNDW_SEM0(wndw->id);
	wndw->data = 0x00000000;
	return 0;
}

int
wndwc37e_new(struct nouveau_drm *drm, enum drm_plane_type type, int index,
	     s32 oclass, struct nv50_wndw **pwndw)
{
	return wndwc37e_new_(&wndwc37e, drm, type, index, oclass,
			     BIT(index >> 1), pwndw);
}
