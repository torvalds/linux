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
#include "base.h"

#include <nvif/cl507c.h>
#include <nvif/event.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include "nouveau_bo.h"

void
base507c_update(struct nv50_wndw *wndw, u32 *interlock)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0080, 1);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_CORE]);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_image_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 4))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
base507c_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 13))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, asyw->image.mode << 8 |
			       asyw->image.interval << 4);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, asyw->image.handle[0]);
		if (asyw->image.format == 0xca) {
			evo_mthd(push, 0x0110, 2);
			evo_data(push, 1);
			evo_data(push, 0x6400);
		} else {
			evo_mthd(push, 0x0110, 2);
			evo_data(push, 0);
			evo_data(push, 0);
		}
		evo_mthd(push, 0x0800, 5);
		evo_data(push, asyw->image.offset[0] >> 8);
		evo_data(push, 0x00000000);
		evo_data(push, asyw->image.h << 16 | asyw->image.w);
		evo_data(push, asyw->image.layout << 20 |
			       (asyw->image.pitch[0] >> 8) << 8 |
			       asyw->image.blocks[0] << 8 |
			       asyw->image.blockh);
		evo_data(push, asyw->image.kind << 16 |
			       asyw->image.format << 8);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_xlut_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_xlut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, 0x40000000);
		evo_kick(push, &wndw->wndw);
	}
}

int
base507c_ntfy_wait_begun(struct nouveau_bo *bo, u32 offset,
			 struct nvif_device *device)
{
	s64 time = nvif_msec(device, 2000ULL,
		u32 data = nouveau_bo_rd32(bo, offset / 4);
		if ((data & 0xc0000000) == 0x40000000)
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

void
base507c_ntfy_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x00a4, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_ntfy_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 3))) {
		evo_mthd(push, 0x00a0, 2);
		evo_data(push, asyw->ntfy.awaken << 30 | asyw->ntfy.offset);
		evo_data(push, asyw->ntfy.handle);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_ntfy_reset(struct nouveau_bo *bo, u32 offset)
{
	nouveau_bo_wr32(bo, offset / 4, 0x00000000);
}

void
base507c_sema_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0094, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_sema_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 5))) {
		evo_mthd(push, 0x0088, 4);
		evo_data(push, asyw->sema.offset);
		evo_data(push, asyw->sema.acquire);
		evo_data(push, asyw->sema.release);
		evo_data(push, asyw->sema.handle);
		evo_kick(push, &wndw->wndw);
	}
}

void
base507c_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	asyh->base.cpp = 0;
}

int
base507c_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	const struct drm_framebuffer *fb = asyw->state.fb;
	int ret;

	ret = drm_atomic_helper_check_plane_state(&asyw->state, &asyh->state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  false, true);
	if (ret)
		return ret;

	if (!wndw->func->ilut) {
		if ((asyh->base.cpp != 1) ^ (fb->format->cpp[0] != 1))
			asyh->state.color_mgmt_changed = true;
	}

	asyh->base.depth = fb->format->depth;
	asyh->base.cpp = fb->format->cpp[0];
	asyh->base.x = asyw->state.src.x1 >> 16;
	asyh->base.y = asyw->state.src.y1 >> 16;
	asyh->base.w = asyw->state.fb->width;
	asyh->base.h = asyw->state.fb->height;

	/* Some newer formats, esp FP16 ones, don't have a
	 * "depth". There's nothing that really makes sense there
	 * either, so just set it to the implicit bit count.
	 */
	if (!asyh->base.depth)
		asyh->base.depth = asyh->base.cpp * 8;

	return 0;
}

const u32
base507c_format[] = {
	DRM_FORMAT_C8,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_ABGR2101010,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR16161616F,
	DRM_FORMAT_ABGR16161616F,
	0
};

static const struct nv50_wndw_func
base507c = {
	.acquire = base507c_acquire,
	.release = base507c_release,
	.sema_set = base507c_sema_set,
	.sema_clr = base507c_sema_clr,
	.ntfy_reset = base507c_ntfy_reset,
	.ntfy_set = base507c_ntfy_set,
	.ntfy_clr = base507c_ntfy_clr,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.olut_core = 1,
	.xlut_set = base507c_xlut_set,
	.xlut_clr = base507c_xlut_clr,
	.image_set = base507c_image_set,
	.image_clr = base507c_image_clr,
	.update = base507c_update,
};

int
base507c_new_(const struct nv50_wndw_func *func, const u32 *format,
	      struct nouveau_drm *drm, int head, s32 oclass, u32 interlock_data,
	      struct nv50_wndw **pwndw)
{
	struct nv50_disp_base_channel_dma_v0 args = {
		.head = head,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(func, drm->dev, DRM_PLANE_TYPE_PRIMARY,
			     "base", head, format, BIT(head),
			     NV50_DISP_INTERLOCK_BASE, interlock_data, &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, head, &args, sizeof(args),
			       disp->sync->bo.offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "base%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	ret = nvif_notify_init(&wndw->wndw.base.user, wndw->notify.func,
			       false, NV50_DISP_BASE_CHANNEL_DMA_V0_NTFY_UEVENT,
			       &(struct nvif_notify_uevent_req) {},
			       sizeof(struct nvif_notify_uevent_req),
			       sizeof(struct nvif_notify_uevent_rep),
			       &wndw->notify);
	if (ret)
		return ret;

	wndw->ntfy = NV50_DISP_BASE_NTFY(wndw->id);
	wndw->sema = NV50_DISP_BASE_SEM0(wndw->id);
	wndw->data = 0x00000000;
	return 0;
}

int
base507c_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return base507c_new_(&base507c, base507c_format, drm, head, oclass,
			     0x00000002 << (head * 8), pwndw);
}
