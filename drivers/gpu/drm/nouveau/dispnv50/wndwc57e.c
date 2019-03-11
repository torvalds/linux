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
#include "wndw.h"
#include "atom.h"

#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>
#include <nouveau_bo.h>

#include <nvif/clc37e.h>

static void
wndwc57e_ilut_clr(struct nv50_wndw *wndw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 2))) {
		evo_mthd(push, 0x0444, 1);
		evo_data(push, 0x00000000);
		evo_kick(push, &wndw->wndw);
	}
}

static void
wndwc57e_ilut_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 4))) {
		evo_mthd(push, 0x0440, 3);
		evo_data(push, asyw->xlut.i.size << 8 |
			       asyw->xlut.i.mode << 2 |
			       asyw->xlut.i.output_mode);
		evo_data(push, asyw->xlut.handle);
		evo_data(push, asyw->xlut.i.offset >> 8);
		evo_kick(push, &wndw->wndw);
	}
}

static u16
fixedU0_16_FP16(u16 fixed)
{
        int sign = 0, exp = 0, man = 0;
        if (fixed) {
                while (--exp && !(fixed & 0x8000))
                        fixed <<= 1;
                man = ((fixed << 1) & 0xffc0) >> 6;
                exp += 15;
        }
        return (sign << 15) | (exp << 10) | man;
}

static void
wndwc57e_ilut_load(struct drm_color_lut *in, int size, void __iomem *mem)
{
	memset_io(mem, 0x00, 0x20); /* VSS header. */
	mem += 0x20;

	for (; size--; in++, mem += 0x08) {
		u16 r = fixedU0_16_FP16(drm_color_lut_extract(in->  red, 16));
		u16 g = fixedU0_16_FP16(drm_color_lut_extract(in->green, 16));
		u16 b = fixedU0_16_FP16(drm_color_lut_extract(in-> blue, 16));
		writew(r, mem + 0);
		writew(g, mem + 2);
		writew(b, mem + 4);
	}

	/* INTERPOLATE modes require a "next" entry to interpolate with,
	 * so we replicate the last entry to deal with this for now.
	 */
	writew(readw(mem - 8), mem + 0);
	writew(readw(mem - 6), mem + 2);
	writew(readw(mem - 4), mem + 4);
}

static void
wndwc57e_ilut(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u16 size = asyw->ilut->length / sizeof(struct drm_color_lut);
	if (size == 256) {
		asyw->xlut.i.mode = 1; /* DIRECT8. */
	} else {
		asyw->xlut.i.mode = 2; /* DIRECT10. */
		size = 1024;
	}
	asyw->xlut.i.size = 4 /* VSS header. */ + size + 1 /* Entries. */;
	asyw->xlut.i.output_mode = 0; /* INTERPOLATE_DISABLE. */
	asyw->xlut.i.load = wndwc57e_ilut_load;
}

static const struct nv50_wndw_func
wndwc57e = {
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
	.xlut_set = wndwc57e_ilut_set,
	.xlut_clr = wndwc57e_ilut_clr,
	.image_set = wndwc37e_image_set,
	.image_clr = wndwc37e_image_clr,
	.update = wndwc37e_update,
};

int
wndwc57e_new(struct nouveau_drm *drm, enum drm_plane_type type, int index,
	     s32 oclass, struct nv50_wndw **pwndw)
{
	return wndwc37e_new_(&wndwc57e, drm, type, index, oclass,
			     BIT(index >> 1), pwndw);
}
