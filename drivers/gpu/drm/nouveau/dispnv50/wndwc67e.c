/*
 * Copyright 2021 Red Hat Inc.
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

#include <nvif/pushc37b.h>

#include <nvhw/class/clc57e.h>

static int
wndwc67e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = &wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 17)))
		return ret;

	PUSH_MTHD(push, NVC57E, SET_PRESENT_CONTROL,
		  NVVAL(NVC57E, SET_PRESENT_CONTROL, MIN_PRESENT_INTERVAL, asyw->image.interval) |
		  NVVAL(NVC57E, SET_PRESENT_CONTROL, BEGIN_MODE, asyw->image.mode) |
		  NVDEF(NVC57E, SET_PRESENT_CONTROL, TIMESTAMP_MODE, DISABLE));

	PUSH_MTHD(push, NVC57E, SET_SIZE,
		  NVVAL(NVC57E, SET_SIZE, WIDTH, asyw->image.w) |
		  NVVAL(NVC57E, SET_SIZE, HEIGHT, asyw->image.h),

				SET_STORAGE,
		  NVVAL(NVC57E, SET_STORAGE, BLOCK_HEIGHT, asyw->image.blockh),

				SET_PARAMS,
		  NVVAL(NVC57E, SET_PARAMS, FORMAT, asyw->image.format) |
		  NVDEF(NVC57E, SET_PARAMS, CLAMP_BEFORE_BLEND, DISABLE) |
		  NVDEF(NVC57E, SET_PARAMS, SWAP_UV, DISABLE) |
		  NVDEF(NVC57E, SET_PARAMS, FMT_ROUNDING_MODE, ROUND_TO_NEAREST),

				SET_PLANAR_STORAGE(0),
		  NVVAL(NVC57E, SET_PLANAR_STORAGE, PITCH, asyw->image.blocks[0]) |
		  NVVAL(NVC57E, SET_PLANAR_STORAGE, PITCH, asyw->image.pitch[0] >> 6));

	PUSH_MTHD(push, NVC57E, SET_CONTEXT_DMA_ISO(0), asyw->image.handle, 1);
	PUSH_MTHD(push, NVC57E, SET_OFFSET(0), asyw->image.offset[0] >> 8);

	PUSH_MTHD(push, NVC57E, SET_POINT_IN(0),
		  NVVAL(NVC57E, SET_POINT_IN, X, asyw->state.src_x >> 16) |
		  NVVAL(NVC57E, SET_POINT_IN, Y, asyw->state.src_y >> 16));

	PUSH_MTHD(push, NVC57E, SET_SIZE_IN,
		  NVVAL(NVC57E, SET_SIZE_IN, WIDTH, asyw->state.src_w >> 16) |
		  NVVAL(NVC57E, SET_SIZE_IN, HEIGHT, asyw->state.src_h >> 16));

	PUSH_MTHD(push, NVC57E, SET_SIZE_OUT,
		  NVVAL(NVC57E, SET_SIZE_OUT, WIDTH, asyw->state.crtc_w) |
		  NVVAL(NVC57E, SET_SIZE_OUT, HEIGHT, asyw->state.crtc_h));
	return 0;
}

static const struct nv50_wndw_func
wndwc67e = {
	.acquire = wndwc37e_acquire,
	.release = wndwc37e_release,
	.sema_set = wndwc37e_sema_set,
	.sema_clr = wndwc37e_sema_clr,
	.ntfy_set = wndwc37e_ntfy_set,
	.ntfy_clr = wndwc37e_ntfy_clr,
	.ntfy_reset = corec37d_ntfy_init,
	.ntfy_wait_begun = base507c_ntfy_wait_begun,
	.ilut = wndwc57e_ilut,
	.ilut_identity = true,
	.ilut_size = 1024,
	.xlut_set = wndwc57e_ilut_set,
	.xlut_clr = wndwc57e_ilut_clr,
	.csc = base907c_csc,
	.csc_set = wndwc57e_csc_set,
	.csc_clr = wndwc57e_csc_clr,
	.image_set = wndwc67e_image_set,
	.image_clr = wndwc37e_image_clr,
	.blend_set = wndwc37e_blend_set,
	.update = wndwc37e_update,
};

int
wndwc67e_new(struct nouveau_drm *drm, enum drm_plane_type type, int index,
	     s32 oclass, struct nv50_wndw **pwndw)
{
	return wndwc37e_new_(&wndwc67e, drm, type, index, oclass, BIT(index >> 1), pwndw);
}
