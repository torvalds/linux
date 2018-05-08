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

#include <nvif/cl507e.h>

#include "nouveau_bo.h"

static const struct nv50_wndw_func
ovly507e = {
};

static const u32
ovly507e_format[] = {
	0
};

static int
ovly507e_new_(const struct nv50_wndw_func *func, const u32 *format,
	      struct nouveau_drm *drm, int head, s32 oclass,
	      struct nv50_wndw **pwndw)
{
	struct nv50_disp_overlay_channel_dma_v0 args = {
		.head = head,
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_wndw *wndw;
	int ret;

	ret = nv50_wndw_new_(func, drm->dev, DRM_PLANE_TYPE_OVERLAY,
			     "ovly", head, format, &wndw);
	if (*pwndw = wndw, ret)
		return ret;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args),
			       disp->sync->bo.offset, &wndw->wndw);
	if (ret) {
		NV_ERROR(drm, "ovly%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	return 0;
}

int
ovly507e_new(struct nouveau_drm *drm, int head, s32 oclass,
	     struct nv50_wndw **pwndw)
{
	return ovly507e_new_(&ovly507e, ovly507e_format, drm, head, oclass, pwndw);
}
