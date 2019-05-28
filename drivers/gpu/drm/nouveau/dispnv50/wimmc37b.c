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
#include "wimm.h"
#include "atom.h"
#include "wndw.h"

#include <nvif/clc37b.h>

static void
wimmc37b_update(struct nv50_wndw *wndw, u32 *interlock)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wimm, 2))) {
		evo_mthd(push, 0x0200, 1);
		if (interlock[NV50_DISP_INTERLOCK_WNDW] & wndw->interlock.data)
			evo_data(push, 0x00000003);
		else
			evo_data(push, 0x00000001);
		evo_kick(push, &wndw->wimm);
	}
}

static void
wimmc37b_point(struct nv50_wndw *wndw, struct nv50_wndw_atom *asyw)
{
	u32 *push;
	if ((push = evo_wait(&wndw->wimm, 2))) {
		evo_mthd(push, 0x0208, 1);
		evo_data(push, asyw->point.y << 16 | asyw->point.x);
		evo_kick(push, &wndw->wimm);
	}
}

static const struct nv50_wimm_func
wimmc37b = {
	.point = wimmc37b_point,
	.update = wimmc37b_update,
};

static int
wimmc37b_init_(const struct nv50_wimm_func *func, struct nouveau_drm *drm,
	       s32 oclass, struct nv50_wndw *wndw)
{
	struct nvc37b_window_imm_channel_dma_v0 args = {
		.pushbuf = 0xb0007b00 | wndw->id,
		.index = wndw->id,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args), 0,
			       &wndw->wimm);
	if (ret) {
		NV_ERROR(drm, "wimm%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	wndw->interlock.wimm = wndw->interlock.data;
	wndw->immd = func;
	return 0;
}

int
wimmc37b_init(struct nouveau_drm *drm, s32 oclass, struct nv50_wndw *wndw)
{
	return wimmc37b_init_(&wimmc37b, drm, oclass, wndw);
}
