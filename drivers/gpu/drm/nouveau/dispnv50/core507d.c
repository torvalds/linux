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
#include "head.h"

#include <nvif/cl507d.h>

#include "nouveau_bo.h"

static const struct nv50_core_func
core507d = {
	.head = &head507d,
	.dac = &dac507d,
	.sor = &sor507d,
	.pior = &pior507d,
};

static int
core507d_new_(const struct nv50_core_func *func, struct nouveau_drm *drm,
	      s32 oclass, struct nv50_core **pcore)
{
	struct nv50_disp_core_channel_dma_v0 args = {};
	struct nv50_disp *disp = nv50_disp(drm->dev);
	struct nv50_core *core;
	int ret;

	if (!(core = *pcore = kzalloc(sizeof(*core), GFP_KERNEL)))
		return -ENOMEM;
	core->func = func;

	ret = nv50_dmac_create(&drm->client.device, &disp->disp->object,
			       &oclass, 0, &args, sizeof(args),
			       disp->sync->bo.offset, &core->chan);
	if (ret) {
		NV_ERROR(drm, "core%04x allocation failed: %d\n", oclass, ret);
		return ret;
	}

	return 0;
}

int
core507d_new(struct nouveau_drm *drm, s32 oclass, struct nv50_core **pcore)
{
	return core507d_new_(&core507d, drm, oclass, pcore);
}
