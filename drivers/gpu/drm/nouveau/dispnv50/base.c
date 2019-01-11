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

#include <nvif/class.h>

int
nv50_base_new(struct nouveau_drm *drm, int head, struct nv50_wndw **pwndw)
{
	struct {
		s32 oclass;
		int version;
		int (*new)(struct nouveau_drm *, int, s32, struct nv50_wndw **);
	} bases[] = {
		{ GK110_DISP_BASE_CHANNEL_DMA, 0, base917c_new },
		{ GK104_DISP_BASE_CHANNEL_DMA, 0, base917c_new },
		{ GF110_DISP_BASE_CHANNEL_DMA, 0, base907c_new },
		{ GT214_DISP_BASE_CHANNEL_DMA, 0, base827c_new },
		{ GT200_DISP_BASE_CHANNEL_DMA, 0, base827c_new },
		{   G82_DISP_BASE_CHANNEL_DMA, 0, base827c_new },
		{  NV50_DISP_BASE_CHANNEL_DMA, 0, base507c_new },
		{}
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int cid;

	cid = nvif_mclass(&disp->disp->object, bases);
	if (cid < 0) {
		NV_ERROR(drm, "No supported base class\n");
		return cid;
	}

	return bases[cid].new(drm, head, bases[cid].oclass, pwndw);
}
