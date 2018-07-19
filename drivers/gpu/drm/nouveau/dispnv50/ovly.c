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
#include "oimm.h"

#include <nvif/class.h>

int
nv50_ovly_new(struct nouveau_drm *drm, int head, struct nv50_wndw **pwndw)
{
	static const struct {
		s32 oclass;
		int version;
		int (*new)(struct nouveau_drm *, int, s32, struct nv50_wndw **);
	} ovlys[] = {
		{ GK104_DISP_OVERLAY_CONTROL_DMA, 0, ovly917e_new },
		{ GF110_DISP_OVERLAY_CONTROL_DMA, 0, ovly907e_new },
		{ GT214_DISP_OVERLAY_CHANNEL_DMA, 0, ovly827e_new },
		{ GT200_DISP_OVERLAY_CHANNEL_DMA, 0, ovly827e_new },
		{   G82_DISP_OVERLAY_CHANNEL_DMA, 0, ovly827e_new },
		{  NV50_DISP_OVERLAY_CHANNEL_DMA, 0, ovly507e_new },
		{}
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int cid, ret;

	cid = nvif_mclass(&disp->disp->object, ovlys);
	if (cid < 0) {
		NV_ERROR(drm, "No supported overlay class\n");
		return cid;
	}

	ret = ovlys[cid].new(drm, head, ovlys[cid].oclass, pwndw);
	if (ret)
		return ret;

	return nv50_oimm_init(drm, *pwndw);
}
