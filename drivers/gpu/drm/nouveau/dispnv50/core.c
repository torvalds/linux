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
#include "core.h"

#include <nvif/class.h>

void
nv50_core_del(struct nv50_core **pcore)
{
	struct nv50_core *core = *pcore;
	if (core) {
		nv50_dmac_destroy(&core->chan);
		kfree(*pcore);
		*pcore = NULL;
	}
}

int
nv50_core_new(struct nouveau_drm *drm, struct nv50_core **pcore)
{
	struct {
		s32 oclass;
		int version;
		int (*new)(struct nouveau_drm *, s32, struct nv50_core **);
	} cores[] = {
		{ TU104_DISP_CORE_CHANNEL_DMA, 0, corec57d_new },
		{ GV100_DISP_CORE_CHANNEL_DMA, 0, corec37d_new },
		{ GP102_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GP100_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GM200_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GM107_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GK110_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GK104_DISP_CORE_CHANNEL_DMA, 0, core917d_new },
		{ GF110_DISP_CORE_CHANNEL_DMA, 0, core907d_new },
		{ GT214_DISP_CORE_CHANNEL_DMA, 0, core827d_new },
		{ GT206_DISP_CORE_CHANNEL_DMA, 0, core827d_new },
		{ GT200_DISP_CORE_CHANNEL_DMA, 0, core827d_new },
		{   G82_DISP_CORE_CHANNEL_DMA, 0, core827d_new },
		{  NV50_DISP_CORE_CHANNEL_DMA, 0, core507d_new },
		{}
	};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	int cid;

	cid = nvif_mclass(&disp->disp->object, cores);
	if (cid < 0) {
		NV_ERROR(drm, "No supported core channel class\n");
		return cid;
	}

	return cores[cid].new(drm, cores[cid].oclass, pcore);
}
