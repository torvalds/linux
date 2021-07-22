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
#include "oimm.h"

#include <nvif/cl507b.h>

static int
oimm507b_init_(const struct nv50_wimm_func *func, struct nouveau_drm *drm,
	       s32 oclass, struct nv50_wndw *wndw)
{
	struct nv50_disp_overlay_v0 args = {
		.head = wndw->id,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int ret;

	ret = nvif_object_ctor(&disp->disp->object, "kmsOvim", 0, oclass,
			       &args, sizeof(args), &wndw->wimm.base.user);
	if (ret) {
		NV_ERROR(drm, "oimm%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	nvif_object_map(&wndw->wimm.base.user, NULL, 0);
	wndw->immd = func;
	return 0;
}

int
oimm507b_init(struct nouveau_drm *drm, s32 oclass, struct nv50_wndw *wndw)
{
	return oimm507b_init_(&curs507a, drm, oclass, wndw);
}
