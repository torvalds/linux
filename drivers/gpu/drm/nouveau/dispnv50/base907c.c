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

static void
base907c_xlut_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 6))) {
		evo_mthd(push, 0x00e0, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00e8, 1);
		evo_data(push, 0x00000000);
		evo_mthd(push, 0x00fc, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
base907c_xlut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 6))) {
		evo_mthd(push, 0x00e0, 3);
		evo_data(push, asyw->xlut.i.enable << 30 |
			       asyw->xlut.i.mode << 24);
		evo_data(push, asyw->xlut.i.offset >> 8);
		evo_data(push, 0x40000000);
		evo_mthd(push, 0x00fc, 1);
		evo_data(push, asyw->xlut.handle);
		evo_kick(push, &wndw->wndw);
	}
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

static void
base907c_csc_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0140, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
base907c_csc_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push, i;
	if ((push = evo_wait(&wndw->wndw, 13))) {
		evo_mthd(push, 0x0140, 12);
		evo_data(push, asyw->csc.matrix[0] | 0x80000000);
		for (i = 1; i < 12; i++)
			evo_data(push, asyw->csc.matrix[i]);
		evo_kick(push, &wndw->wndw);
	}
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
