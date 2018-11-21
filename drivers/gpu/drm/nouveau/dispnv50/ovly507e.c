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
#include "ovly.h"
#include "atom.h"

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include <nvif/cl507e.h>
#include <nvif/event.h>

void
ovly507e_update(struct nv50_wndw *wndw, u32 *interlock)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0080, 1);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_CORE]);
		evo_kick(push, &wndw->wndw);
	}
}

void
ovly507e_scale_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 4))) {
		evo_mthd(push, 0x00e0, 3);
		evo_data(push, asyw->scale.sy << 16 | asyw->scale.sx);
		evo_data(push, asyw->scale.sh << 16 | asyw->scale.sw);
		evo_data(push, asyw->scale.dw);
		evo_kick(push, &wndw->wndw);
	}
}

void
ovly507e_image_clr(struct nv50_wndw *wndw)
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
ovly507e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 12))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, asyw->image.interval << 4);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, asyw->image.handle[0]);
		evo_mthd(push, 0x0100, 1);
		evo_data(push, 0x00000002);
		evo_mthd(push, 0x0800, 1);
		evo_data(push, asyw->image.offset[0] >> 8);
		evo_mthd(push, 0x0808, 3);
		evo_data(push, asyw->image.h << 16 | asyw->image.w);
		evo_data(push, asyw->image.layout << 20 |
			       (asyw->image.pitch[0] >> 8) << 8 |
			       asyw->image.blocks[0] << 8 |
			       asyw->image.blockh);
		evo_data(push, asyw->image.kind << 16 |
			       asyw->image.format << 8 |
			       asyw->image.colorspace);
		evo_kick(push, &wndw->wndw);
	}
}

void
ovly507e_ntfy_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x00a4, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

void
ovly507e_ntfy_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
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
ovly507e_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	asyh->ovly.cpp = 0;
}

int
ovly507e_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	const struct drm_framebuffer *fb = asyw->state.fb;
	int ret;

	ret = drm_atomic_helper_check_plane_state(&asyw->state, &asyh->state,
						  DRM_PLANE_HELPER_NO_SCALING,
						  DRM_PLANE_HELPER_NO_SCALING,
						  true, true);
	if (ret)
		return ret;

	asyh->ovly.cpp = fb->format->cpp[0];
	return 0;
}

#include "nouveau_bo.h"

static const struct nv50_wndw_func
ovly507e = {
	.acquire = ovly507e_acquire,
	.release = ovly507e_release,
	.ntfy_set = ovly507e_ntfy_set,
	.ntfy_clr = ovly507e_ntfy_clr,
	.ntfy_reset = base507c_ntfy_reset,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.image_set = ovly507e_image_set,
	.image_clr = ovly507e_image_clr,
	.scale_set = ovly507e_scale_set,
	.update = ovly507e_update,
};

static const u32
ovly507e_format[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	0
};

int
ovly507e_new_(const struct nv50_wndw_func *func, const u32 *format,
	      struct nouveau_drm *drm, int head, s32 oclass, u32 interlock_data,
	      struct nv50_wndw **pwndw)
{
	struct nv50_disp_overlay_channel_dma_v0 args = {
		.head = head,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(func, drm->dev, DRM_PLANE_TYPE_OVERLAY,
			     "ovly", head, format, BIT(head),
			     NV50_DISP_INTERLOCK_OVLY, interlock_data,
			     &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args),
			       disp->sync->bo.offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "ovly%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	ret = nvif_notify_init(&wndw->wndw.base.user, wndw->notify.func, false,
			       NV50_DISP_OVERLAY_CHANNEL_DMA_V0_NTFY_UEVENT,
			       &(struct nvif_notify_uevent_req) {},
			       sizeof(struct nvif_notify_uevent_req),
			       sizeof(struct nvif_notify_uevent_rep),
			       &wndw->notify);
	if (ret)
		return ret;

	wndw->ntfy = NV50_DISP_OVLY_NTFY(wndw->id);
	wndw->sema = NV50_DISP_OVLY_SEM0(wndw->id);
	wndw->data = 0x00000000;
	return 0;
}

int
ovly507e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly507e, ovly507e_format, drm, head, oclass,
			     0x00000004 << (head * 8), pwndw);
}
