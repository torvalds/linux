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

static void
base907c_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 10))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, asyw->image.mode << 8 |
			       asyw->image.interval << 4);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, asyw->image.handle[0]);
		evo_mthd(push, 0x0400, 5);
		evo_data(push, asyw->image.offset[0] >> 8);
		evo_data(push, 0x00000000);
		evo_data(push, asyw->image.h << 16 | asyw->image.w);
		evo_data(push, asyw->image.layout << 24 |
			       (asyw->image.pitch[0] >> 8) << 8 |
			       asyw->image.blocks[0] << 8 |
			       asyw->image.blockh);
		evo_data(push, asyw->image.format << 8);
		evo_kick(push, &wndw->wndw);
	}
}

static int
base907c_xlut_clr(struct nv50_wndw *wndw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 6)))
		return ret;

	PUSH_NVSQ(push, NV907C, 0x00e0, 0x00000000);
	PUSH_NVSQ(push, NV907C, 0x00e8, 0x00000000);
	PUSH_NVSQ(push, NV907C, 0x00fc, 0x00000000);
	return 0;
}

static int
base907c_xlut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 6)))
		return ret;

	PUSH_NVSQ(push, NV907C, 0x00e0, asyw->xlut.i.enable << 30 |
					asyw->xlut.i.mode << 24,
				0x00e4, asyw->xlut.i.offset >> 8,
				0x00e8, 0x40000000);
	PUSH_NVSQ(push, NV907C, 0x00fc, asyw->xlut.handle);
	return 0;
}

static bool
base907c_ilut(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw, int size)
{
	if (size != 256 && size != 1024)
		return false;

	asyw->xlut.i.mode = size == 1024 ? 4 : 7;
	asyw->xlut.i.enable = 2;
	asyw->xlut.i.load = head907d_olut_load;
	return true;
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

	PUSH_NVSQ(push, NV907C, 0x0140, 0x00000000);
	return 0;
}

static int
base907c_csc_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	struct nvif_push *push = wndw->wndw.push;
	int ret;

	if ((ret = PUSH_WAIT(push, 13)))
		return ret;

	PUSH_NVSQ(push, NV907C, 0x0140,  asyw->csc.matrix[0] | 0x80000000,
				0x0144, &asyw->csc.matrix[1], 11);
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
