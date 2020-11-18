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
#include <nvif/push507c.h>
#include <nvif/timer.h>

#include <nvhw/class/cl507c.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_plane_helper.h>

#include "nouveau_bo.h"

int
base507c_update(struct nv50_wndw *wndw, u32 *interlock)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507C, UPDATE, interlock[NV50_DISP_INTERLOCK_CORE]);
	return PUSH_KICK(push);
}

int
base507c_image_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 4)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_PRESENT_CONTROL,
		  NVDEF(NV507C, SET_PRESENT_CONTROL, BEGIN_MODE, NON_TEARING) |
		  NVVAL(NV507C, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, 0));

	PUSH_MTHD(push, NV507C, SET_CONTEXT_DMA_ISO, 0x00000000);
	return 0;
}

static int
base507c_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_PRESENT_CONTROL,
		  NVVAL(NV507C, SET_PRESENT_CONTROL, BEGIN_MODE, asyw->image.mode) |
		  NVVAL(NV507C, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval));

	PUSH_MTHD(push, NV507C, SET_CONTEXT_DMA_ISO, asyw->image.handle[0]);

	if (asyw->image.format == NV507C_SURFACE_SET_PARAMS_FORMAT_RF16_GF16_BF16_AF16) {
		PUSH_MTHD(push, NV507C, SET_PROCESSING,
			  NVDEF(NV507C, SET_PROCESSING, USE_GAIN_OFS, ENABLE),

					SET_CONVERSION,
			  NVVAL(NV507C, SET_CONVERSION, GAIN, 0) |
			  NVVAL(NV507C, SET_CONVERSION, OFS, 0x64));
	} else {
		PUSH_MTHD(push, NV507C, SET_PROCESSING,
			  NVDEF(NV507C, SET_PROCESSING, USE_GAIN_OFS, DISABLE));
	}

	PUSH_MTHD(push, NV507C, SURFACE_SET_OFFSET(0, 0), asyw->image.offset[0] >> 8);

	PUSH_MTHD(push, NV507C, SURFACE_SET_SIZE(0),
		  NVVAL(NV507C, SURFACE_SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NV507C, SURFACE_SET_SIZE, HEIGHT, asyw->image.h),

				SURFACE_SET_STORAGE(0),
		  NVVAL(NV507C, SURFACE_SET_STORAGE, MEMORY_LAYOUT, asyw->image.layout) |
		  NVVAL(NV507C, SURFACE_SET_STORAGE, PITCH, asyw->image.pitch[0] >> 8) |
		  NVVAL(NV507C, SURFACE_SET_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NV507C, SURFACE_SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh),

				SURFACE_SET_PARAMS(0),
		  NVVAL(NV507C, SURFACE_SET_PARAMS, FORMAT, asyw->image.format) |
		  NVDEF(NV507C, SURFACE_SET_PARAMS, SUPER_SAMPLE, X1_AA) |
		  NVDEF(NV507C, SURFACE_SET_PARAMS, GAMMA, LINEAR) |
		  NVDEF(NV507C, SURFACE_SET_PARAMS, LAYOUT, FRM) |
		  NVVAL(NV507C, SURFACE_SET_PARAMS, KIND, asyw->image.kind) |
		  NVDEF(NV507C, SURFACE_SET_PARAMS, PART_STRIDE, PARTSTRIDE_256));
	return 0;
}

int
base507c_xlut_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_BASE_LUT_LO,
		  NVDEF(NV507C, SET_BASE_LUT_LO, ENABLE, DISABLE));
	return 0;
}

int
base507c_xlut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_BASE_LUT_LO,
		  NVDEF(NV507C, SET_BASE_LUT_LO, ENABLE, USE_CORE_LUT));
	return 0;
}

int
base507c_ntfy_wait_begun(struct nouveau_bo *bo, u32 offset,
			 struct nvif_device *device)
{
	s64 time = nvif_msec(device, 2000ULL,
		if (NVBO_TD32(bo, offset, NV_DISP_BASE_NOTIFIER_1, _0, STATUS, ==, BEGUN))
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

int
base507c_ntfy_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_CONTEXT_DMA_NOTIFIER, 0x00000000);
	return 0;
}

int
base507c_ntfy_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 3)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_NOTIFIER_CONTROL,
		  NVVAL(NV507C, SET_NOTIFIER_CONTROL, MODE, asyw->ntfy.awaken) |
		  NVVAL(NV507C, SET_NOTIFIER_CONTROL, OFFSET, asyw->ntfy.offset >> 2),

				SET_CONTEXT_DMA_NOTIFIER, asyw->ntfy.handle);
	return 0;
}

void
base507c_ntfy_reset(struct nouveau_bo *bo, u32 offset)
{
	NVBO_WR32(bo, offset, NV_DISP_BASE_NOTIFIER_1, _0,
			NVDEF(NV_DISP_BASE_NOTIFIER_1, _0, STATUS, NOT_BEGUN));
}

int
base507c_sema_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_CONTEXT_DMA_SEMAPHORE, 0x00000000);
	return 0;
}

int
base507c_sema_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 5)))
		return ret;

	PUSH_MTHD(push, NV507C, SET_SEMAPHORE_CONTROL, asyw->sema.offset,
				SET_SEMAPHORE_ACQUIRE, asyw->sema.acquire,
				SET_SEMAPHORE_RELEASE, asyw->sema.release,
				SET_CONTEXT_DMA_SEMAPHORE, asyw->sema.handle);
	return 0;
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
	struct nouveau_display *disp = nouveau_display(drm->dev);
	struct nv50_disp *disp50 = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(func, drm->dev, DRM_PLANE_TYPE_PRIMARY,
			     "base", head, format, BIT(head),
			     NV50_DISP_INTERLOCK_BASE, interlock_data, &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp.object,
			       &oclass, head, &args, sizeof(args),
			       disp50->sync->offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "base%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	ret = nvif_notify_ctor(&wndw->wndw.base.user, "kmsBaseNtfy",
			       wndw->notify.func, false,
			       NV50_DISP_BASE_CHANNEL_DMA_V0_NTFY_UEVENT,
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
