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

static void
ovly907e_image_set(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wndw, 12))) {
		evo_mthd(push, 0x0084, 1);
		evo_data(push, asyw->image.interval << 4);
		evo_mthd(push, 0x00c0, 1);
		evo_data(push, asyw->image.handle[0]);
		evo_mthd(push, 0x0100, 1);
		evo_data(push, 0x00000002);
		evo_mthd(push, 0x0400, 1);
		evo_data(push, asyw->image.offset[0] >> 8);
		evo_mthd(push, 0x0408, 3);
		evo_data(push, asyw->image.h << 16 | asyw->image.w);
		evo_data(push, asyw->image.layout << 24 |
			       (asyw->image.pitch[0] >> 8) << 8 |
			       asyw->image.blocks[0] << 8 |
			       asyw->image.blockh);
		evo_data(push, asyw->image.format << 8 |
			       asyw->image.colorspace);
		evo_kick(push, &wndw->wndw);
	}
}

const struct nv50_wndw_func
ovly907e = {
	.acquire = ovly507e_acquire,
	.release = ovly507e_release,
	.ntfy_set = ovly507e_ntfy_set,
	.ntfy_clr = ovly507e_ntfy_clr,
	.ntfy_reset = ovly827e_ntfy_reset,
	.ntfy_wait_begun = ovly827e_ntfy_wait_begun,
	.image_set = ovly907e_image_set,
	.image_clr = ovly507e_image_clr,
	.scale_set = ovly507e_scale_set,
	.update = ovly507e_update,
};

int
ovly907e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly907e, ovly827e_format, drm, head, oclass,
			     0x00000004 << (head * 4), pwndw);
}
