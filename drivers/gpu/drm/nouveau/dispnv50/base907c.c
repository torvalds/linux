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

#include <nvif/push507c.h>

#include <nvhw/class/cl907c.h>

static int
base907c_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 10)))
		return ret;

	PUSH_MTHD(push, NV907C, SET_PRESENT_CONTROL,
		  NVVAL(NV907C, SET_PRESENT_CONTROL, BEGIN_MODE, asyw->image.mode) |
		  NVDEF(NV907C, SET_PRESENT_CONTROL, TIMESTAMP_MODE, DISABLE) |
		  NVVAL(NV907C, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval));

	PUSH_MTHD(push, NV907C, SET_CONTEXT_DMAS_ISO(0), asyw->image.handle, 1);

	PUSH_MTHD(push, NV907C, SURFACE_SET_OFFSET(0, 0), asyw->image.offset[0] >> 8,
				SURFACE_SET_OFFSET(0, 1), 0x00000000,

				SURFACE_SET_SIZE(0),
		  NVVAL(NV907C, SURFACE_SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NV907C, SURFACE_SET_SIZE, HEIGHT, asyw->image.h),

				SURFACE_SET_STORAGE(0),
		  NVVAL(NV907C, SURFACE_SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh) |
		  NVVAL(NV907C, SURFACE_SET_STORAGE, PITCH, asyw->image.pitch[0] >> 8) |
		  NVVAL(NV907C, SURFACE_SET_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NV907C, SURFACE_SET_STORAGE, MEMORY_LAYOUT, asyw->image.layout),

				SURFACE_SET_PARAMS(0),
		  NVVAL(NV907C, SURFACE_SET_PARAMS, FORMAT, asyw->image.format) |
		  NVDEF(NV907C, SURFACE_SET_PARAMS, SUPER_SAMPLE, X1_AA) |
		  NVDEF(NV907C, SURFACE_SET_PARAMS, GAMMA, LINEAR) |
		  NVDEF(NV907C, SURFACE_SET_PARAMS, LAYOUT, FRM));
	return 0;
}

static int
base907c_xlut_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 6)))
		return ret;

	PUSH_MTHD(push, NV907C, SET_BASE_LUT_LO,
		  NVDEF(NV907C, SET_BASE_LUT_LO, ENABLE, DISABLE));

	PUSH_MTHD(push, NV907C, SET_OUTPUT_LUT_LO,
		  NVDEF(NV907C, SET_OUTPUT_LUT_LO, ENABLE, DISABLE));

	PUSH_MTHD(push, NV907C, SET_CONTEXT_DMA_LUT, 0x00000000);
	return 0;
}

static int
base907c_xlut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 6)))
		return ret;

	PUSH_MTHD(push, NV907C, SET_BASE_LUT_LO,
		  NVVAL(NV907C, SET_BASE_LUT_LO, ENABLE, asyw->xlut.i.enable) |
		  NVVAL(NV907C, SET_BASE_LUT_LO, MODE, asyw->xlut.i.mode),

				SET_BASE_LUT_HI, asyw->xlut.i.offset >> 8,

				SET_OUTPUT_LUT_LO,
		  NVDEF(NV907C, SET_OUTPUT_LUT_LO, ENABLE, USE_CORE_LUT));

	PUSH_MTHD(push, NV907C, SET_CONTEXT_DMA_LUT, asyw->xlut.handle);
	return 0;
}

static void
base907c_ilut(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw, int size)
{
	if (size == 1024)
		asyw->xlut.i.mode = NV907C_SET_BASE_LUT_LO_MODE_INTERPOLATE_1025_UNITY_RANGE;
	else
		asyw->xlut.i.mode = NV907C_SET_BASE_LUT_LO_MODE_INTERPOLATE_257_UNITY_RANGE;

	asyw->xlut.i.enable = NV907C_SET_BASE_LUT_LO_ENABLE_ENABLE;
	asyw->xlut.i.load = head907d_olut_load;
}

static inline u32
csc_drm_to_base(u64 in)
{
	/* base takes a 19-bit 2's complement value in S3.16 format */
	bool sign = in & BIT_ULL(63);
	u32 integer = (in >> 32) & 0x7fffffff;
	u32 fraction = in & 0xffffffff;

	if (integer >= 4) {
		return (1 << 18) - (sign ? 0 : 1);
	} else {
		u32 ret = (integer << 16) | (fraction >> 16);
		if (sign)
			ret = -ret;
		return ret & GENMASK(18, 0);
	}
}

void
base907c_csc(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw,
	     const struct drm_color_ctm *ctm)
{
	int i, j;

	for (j = 0; j < 3; j++) {
		for (i = 0; i < 4; i++) {
			u32 *val = &asyw->csc.matrix[j * 4 + i];
			/* DRM does not support constant offset, while
			 * HW CSC does. Skip it. */
			if (i == 3) {
				*val = 0;
			} else {
				*val = csc_drm_to_base(ctm->matrix[j * 3 + i]);
			}
		}
	}
}

static int
base907c_csc_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 2)))
		return ret;

	PUSH_MTHD(push, NV907C, SET_CSC_RED2RED,
		  NVDEF(NV907C, SET_CSC_RED2RED, OWNER, CORE));
	return 0;
}

static int
base907c_csc_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_MTHD(push, NV907C, SET_CSC_RED2RED,
		  NVDEF(NV907C, SET_CSC_RED2RED, OWNER, BASE) |
		  NVVAL(NV907C, SET_CSC_RED2RED, COEFF, asyw->csc.matrix[0]),

				SET_CSC_GRN2RED, &asyw->csc.matrix[1], 11);
	return 0;
}

const struct nv50_wndw_func
base907c = {
	.acquire = base507c_acquire,
	.release = base507c_release,
	.sema_set = base507c_sema_set,
	.sema_clr = base507c_sema_clr,
	.ntfy_reset = base507c_ntfy_reset,
	.ntfy_set = base507c_ntfy_set,
	.ntfy_clr = base507c_ntfy_clr,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.ilut = base907c_ilut,
	.csc = base907c_csc,
	.csc_set = base907c_csc_set,
	.csc_clr = base907c_csc_clr,
	.olut_core = true,
	.ilut_size = 1024,
	.xlut_set = base907c_xlut_set,
	.xlut_clr = base907c_xlut_clr,
	.image_set = base907c_image_set,
	.image_clr = base507c_image_clr,
	.update = base507c_update,
};

int
base907c_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return base507c_new_(&base907c, base507c_format, drm, head, oclass,
			     0x00000002 << (head * 4), pwndw);
}
