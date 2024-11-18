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

#include <nouveau_bo.h>

#include <nvif/push507c.h>
#include <nvif/timer.h>

#include <nvhw/class/cl827e.h>

static int
ovly827e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = &wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 12)))
		return ret;

	PUSH_MTHD(push, NV827E, SET_PRESENT_CONTROL,
		  NVDEF(NV827E, SET_PRESENT_CONTROL, BEGIN_MODE, ASAP) |
		  NVVAL(NV827E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval));

	PUSH_MTHD(push, NV827E, SET_CONTEXT_DMA_ISO, asyw->image.handle[0]);

	PUSH_MTHD(push, NV827E, SET_COMPOSITION_CONTROL,
		  NVDEF(NV827E, SET_COMPOSITION_CONTROL, MODE, OPAQUE_SUSPEND_BASE));

	PUSH_MTHD(push, NV827E, SURFACE_SET_OFFSET, asyw->image.offset[0] >> 8);

	PUSH_MTHD(push, NV827E, SURFACE_SET_SIZE,
		  NVVAL(NV827E, SURFACE_SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NV827E, SURFACE_SET_SIZE, HEIGHT, asyw->image.h),

				SURFACE_SET_STORAGE,
		  NVVAL(NV827E, SURFACE_SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh) |
		  NVVAL(NV827E, SURFACE_SET_STORAGE, PITCH, (asyw->image.pitch[0] >> 8)) |
		  NVVAL(NV827E, SURFACE_SET_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NV827E, SURFACE_SET_STORAGE, MEMORY_LAYOUT, asyw->image.layout),

				SURFACE_SET_PARAMS,
		  NVVAL(NV827E, SURFACE_SET_PARAMS, FORMAT, asyw->image.format) |
		  NVVAL(NV827E, SURFACE_SET_PARAMS, COLOR_SPACE, asyw->image.colorspace));
	return 0;
}

int
ovly827e_ntfy_wait_begun(struct nouveau_bo *bo, u32 offset,
			 struct nvif_device *device)
{
	s64 time = nvif_msec(device, 2000ULL,
		if (NVBO_TD32(bo, offset, NV_DISP_NOTIFICATION_1, _3, STATUS, ==, BEGUN))
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

void
ovly827e_ntfy_reset(struct nouveau_bo *bo, u32 offset)
{
	NVBO_WR32(bo, offset, NV_DISP_NOTIFICATION_1, TIME_STAMP_0, 0);
	NVBO_WR32(bo, offset, NV_DISP_NOTIFICATION_1, TIME_STAMP_1, 0);
	NVBO_WR32(bo, offset, NV_DISP_NOTIFICATION_1, _2, 0);
	NVBO_WR32(bo, offset, NV_DISP_NOTIFICATION_1, _3,
			NVDEF(NV_DISP_NOTIFICATION_1, _3, STATUS, NOT_BEGUN));
}

static const struct nv50_wndw_func
ovly827e = {
	.acquire = ovly507e_acquire,
	.release = ovly507e_release,
	.ntfy_set = base507c_ntfy_set,
	.ntfy_clr = base507c_ntfy_clr,
	.ntfy_reset = ovly827e_ntfy_reset,
	.ntfy_wait_begun = ovly827e_ntfy_wait_begun,
	.image_set = ovly827e_image_set,
	.image_clr = base507c_image_clr,
	.scale_set = ovly507e_scale_set,
	.update = base507c_update,
};

const u32
ovly827e_format[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR2101010,
	0
};

int
ovly827e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly827e, ovly827e_format, drm, head, oclass,
			     0x00000004 << (head * 8), pwndw);
}
