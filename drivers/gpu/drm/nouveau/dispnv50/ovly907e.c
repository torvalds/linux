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

#include <nvif/push507c.h>

#include <nvhw/class/cl907e.h>

static int
ovly907e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = &wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 12)))
		return ret;

	PUSH_MTHD(push, NV907E, SET_PRESENT_CONTROL,
		  NVDEF(NV907E, SET_PRESENT_CONTROL, BEGIN_MODE, ASAP) |
		  NVVAL(NV907E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval));

	PUSH_MTHD(push, NV907E, SET_CONTEXT_DMA_ISO, asyw->image.handle[0]);

	PUSH_MTHD(push, NV907E, SET_COMPOSITION_CONTROL,
		  NVDEF(NV907E, SET_COMPOSITION_CONTROL, MODE, OPAQUE));

	PUSH_MTHD(push, NV907E, SURFACE_SET_OFFSET, asyw->image.offset[0] >> 8);

	PUSH_MTHD(push, NV907E, SURFACE_SET_SIZE,
		  NVVAL(NV907E, SURFACE_SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NV907E, SURFACE_SET_SIZE, HEIGHT, asyw->image.h),

				SURFACE_SET_STORAGE,
		  NVVAL(NV907E, SURFACE_SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh) |
		  NVVAL(NV907E, SURFACE_SET_STORAGE, PITCH, (asyw->image.pitch[0] >> 8)) |
		  NVVAL(NV907E, SURFACE_SET_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NV907E, SURFACE_SET_STORAGE, MEMORY_LAYOUT, asyw->image.layout),

				SURFACE_SET_PARAMS,
		  NVVAL(NV907E, SURFACE_SET_PARAMS, FORMAT, asyw->image.format) |
		  NVVAL(NV907E, SURFACE_SET_PARAMS, COLOR_SPACE, asyw->image.colorspace));
	return 0;
}

const struct nv50_wndw_func
ovly907e = {
	.acquire = ovly507e_acquire,
	.release = ovly507e_release,
	.ntfy_set = base507c_ntfy_set,
	.ntfy_clr = base507c_ntfy_clr,
	.ntfy_reset = ovly827e_ntfy_reset,
	.ntfy_wait_begun = ovly827e_ntfy_wait_begun,
	.image_set = ovly907e_image_set,
	.image_clr = base507c_image_clr,
	.scale_set = ovly507e_scale_set,
	.update = base507c_update,
};

static const u32
ovly907e_format[] = {
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XBGR2101010,
	DRM_FORMAT_XBGR16161616F,
	0
};

int
ovly907e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly907e, ovly907e_format, drm, head, oclass,
			     0x00000004 << (head * 4), pwndw);
}
