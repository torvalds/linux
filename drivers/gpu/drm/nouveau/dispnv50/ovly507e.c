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
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include <nvif/if0014.h>
#include <nvif/push507c.h>

#include <nvhw/class/cl507e.h>

int
ovly507e_scale_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV507E, SET_POINT_IN,
		  NVVAL(NV507E, SET_POINT_IN, X, asyw->scale.sx) |
		  NVVAL(NV507E, SET_POINT_IN, Y, asyw->scale.sy),

				SET_SIZE_IN,
		  NVVAL(NV507E, SET_SIZE_IN, WIDTH, asyw->scale.sw) |
		  NVVAL(NV507E, SET_SIZE_IN, HEIGHT, asyw->scale.sh),

				SET_SIZE_OUT,
		  NVVAL(NV507E, SET_SIZE_OUT, WIDTH, asyw->scale.dw));
	return 0;
}

static int
ovly507e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 12)))
		return ret;

	PUSH_MTHD(push, NV507E, SET_PRESENT_CONTROL,
		  NVDEF(NV507E, SET_PRESENT_CONTROL, BEGIN_MODE, ASAP) |
		  NVVAL(NV507E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval));

	PUSH_MTHD(push, NV507E, SET_CONTEXT_DMA_ISO, asyw->image.handle[0]);

	PUSH_MTHD(push, NV507E, SET_COMPOSITION_CONTROL,
		  NVDEF(NV507E, SET_COMPOSITION_CONTROL, MODE, OPAQUE_SUSPEND_BASE));

	PUSH_MTHD(push, NV507E, SURFACE_SET_OFFSET, asyw->image.offset[0] >> 8);

	PUSH_MTHD(push, NV507E, SURFACE_SET_SIZE,
		  NVVAL(NV507E, SURFACE_SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NV507E, SURFACE_SET_SIZE, HEIGHT, asyw->image.h),

				SURFACE_SET_STORAGE,
		  NVVAL(NV507E, SURFACE_SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh) |
		  NVVAL(NV507E, SURFACE_SET_STORAGE, PITCH, (asyw->image.pitch[0] >> 8)) |
		  NVVAL(NV507E, SURFACE_SET_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NV507E, SURFACE_SET_STORAGE, MEMORY_LAYOUT, asyw->image.layout),

				SURFACE_SET_PARAMS,
		  NVVAL(NV507E, SURFACE_SET_PARAMS, FORMAT, asyw->image.format) |
		  NVVAL(NV507E, SURFACE_SET_PARAMS, COLOR_SPACE, asyw->image.colorspace) |
		  NVVAL(NV507E, SURFACE_SET_PARAMS, KIND, asyw->image.kind) |
		  NVDEF(NV507E, SURFACE_SET_PARAMS, PART_STRIDE, PARTSTRIDE_256));
	return 0;
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
	.ntfy_set = base507c_ntfy_set,
	.ntfy_clr = base507c_ntfy_clr,
	.ntfy_reset = base507c_ntfy_reset,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.image_set = ovly507e_image_set,
	.image_clr = base507c_image_clr,
	.scale_set = ovly507e_scale_set,
	.update = base507c_update,
};

static const u32
ovly507e_format[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB1555,
	0
};

int
ovly507e_new_(const struct nv50_wndw_func *func, const u32 *format,
	      struct nouveau_drm *drm, int head, s32 oclass, u32 interlock_data,
	      struct nv50_wndw **pwndw)
{
	struct nvif_disp_chan_v0 args = {
		.id = head,
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
			       disp->sync->offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "ovly%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

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
