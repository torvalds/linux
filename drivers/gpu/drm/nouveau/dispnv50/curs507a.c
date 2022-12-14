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
#include "curs.h"
#include "core.h"
#include "head.h"

#include <nvif/if0014.h>
#include <nvif/timer.h>

#include <nvhw/class/cl507a.h>

#include <drm/drm_atomic_helper.h>

bool
curs507a_space(struct nv50_wndw *wndw)
{
	nvif_msec(&nouveau_drm(wndw->plane.dev)->client.device, 100,
		if (NVIF_TV32(&wndw->wimm.base.user, NV507A, FREE, COUNT, >=, 4))
			return true;
	);

	WARN_ON(1);
	return false;
}

static int
curs507a_update(struct nv50_wndw *wndw, u32 *interlock)
{
	struct nvif_object *user = &wndw->wimm.base.user;
	int ret = nvif_chan_wait(&wndw->wimm, 1);
	if (ret == 0) {
		NVIF_WR32(user, NV507A, UPDATE,
			  NVDEF(NV507A, UPDATE, INTERLOCK_WITH_CORE, DISABLE));
	}
	return ret;
}

static int
curs507a_point(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_object *user = &wndw->wimm.base.user;
	int ret = nvif_chan_wait(&wndw->wimm, 1);
	if (ret == 0) {
		NVIF_WR32(user, NV507A, SET_CURSOR_HOT_SPOT_POINT_OUT,
			  NVVAL(NV507A, SET_CURSOR_HOT_SPOT_POINT_OUT, X, asyw->point.x) |
			  NVVAL(NV507A, SET_CURSOR_HOT_SPOT_POINT_OUT, Y, asyw->point.y));
	}
	return ret;
}

const struct nv50_wimm_func
curs507a = {
	.point = curs507a_point,
	.update = curs507a_update,
};

static void
curs507a_prepare(struct nv50_wndw *wndw, struct nv50_head_atom *asyh,
		 struct nv50_wndw_atom *asyw)
{
	u32 handle = nv50_disp(wndw->plane.dev)->core->chan.vram.handle;
	u32 offset = asyw->image.offset[0];
	if (asyh->curs.handle != handle || asyh->curs.offset != offset) {
		asyh->curs.handle = handle;
		asyh->curs.offset = offset;
		asyh->set.curs = asyh->curs.visible;
	}
}

static void
curs507a_release(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	asyh->curs.visible = false;
}

static int
curs507a_acquire(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
		 struct nv50_head_atom *asyh)
{
	struct nouveau_drm *drm = nouveau_drm(wndw->plane.dev);
	struct nv50_head *head = nv50_head(asyw->state.crtc);
	int ret;

	ret = drm_atomic_helper_check_plane_state(&asyw->state, &asyh->state,
						  DRM_PLANE_NO_SCALING,
						  DRM_PLANE_NO_SCALING,
						  true, true);
	asyh->curs.visible = asyw->state.visible;
	if (ret || !asyh->curs.visible)
		return ret;

	if (asyw->state.crtc_w != asyw->state.crtc_h) {
		NV_ATOMIC(drm, "Plane width/height must be equal for cursors\n");
		return -EINVAL;
	}

	if (asyw->image.w != asyw->state.crtc_w) {
		NV_ATOMIC(drm, "Plane width must be equal to fb width for cursors (height can be larger though)\n");
		return -EINVAL;
	}

	if (asyw->state.src_x || asyw->state.src_y) {
		NV_ATOMIC(drm, "Cursor planes do not support framebuffer offsets\n");
		return -EINVAL;
	}

	ret = head->func->curs_layout(head, asyw, asyh);
	if (ret)
		return ret;

	return head->func->curs_format(head, asyw, asyh);
}

static const u32
curs507a_format[] = {
	DRM_FORMAT_ARGB8888,
	0
};

static const struct nv50_wndw_func
curs507a_wndw = {
	.acquire = curs507a_acquire,
	.release = curs507a_release,
	.prepare = curs507a_prepare,
};

int
curs507a_new_(const struct nv50_wimm_func *func, struct nouveau_drm *drm,
	      int head, s32 oclass, u32 interlock_data,
	      struct nv50_wndw **pwndw)
{
	struct nvif_disp_chan_v0 args = {
		.id = head,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(&curs507a_wndw, drm->dev, DRM_PLANE_TYPE_CURSOR,
			     "curs", head, curs507a_format, BIT(head),
			     NV50_DISP_INTERLOCK_CURS, interlock_data, &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nvif_object_ctor(&disp->disp->object, "kmsCurs", 0, oclass,
			       &args, sizeof(args), &wndw->wimm.base.user);
	if (ret) {
		NV_ERROR(drm, "curs%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	nvif_object_map(&wndw->wimm.base.user, NULL, 0);
	wndw->immd = func;
	wndw->ctxdma.parent = NULL;
	return 0;
}

int
curs507a_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return curs507a_new_(&curs507a, drm, head, oclass,
			     0x00000001 << (head * 8), pwndw);
}
