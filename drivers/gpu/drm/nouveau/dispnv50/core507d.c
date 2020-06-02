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
#include <nvif/timer.h>

#include "nouveau_bo.h"

void
core507d_update(struct nv50_core *core, u32 *interlock, bool ntfy)
{
	u32 *push;
	if ((push = evo_wait(&core->chan, 5))) {
		if (ntfy) {
			evo_mthd(push, 0x0084, 1);
			evo_data(push, 0x80000000 | NV50_DISP_CORE_NTFY);
		}
		evo_mthd(push, 0x0080, 2);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_BASE] |
			       interlock[NV50_DISP_INTERLOCK_OVLY]);
		evo_data(push, 0x00000000);
		evo_kick(push, &core->chan);
	}
}

int
core507d_ntfy_wait_done(struct nouveau_bo *bo, u32 offset,
			struct nvif_device *device)
{
	s64 time = nvif_msec(device, 2000ULL,
		if (nouveau_bo_rd32(bo, offset / 4))
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

void
core507d_ntfy_init(struct nouveau_bo *bo, u32 offset)
{
	nouveau_bo_wr32(bo, offset / 4, 0x00000000);
}

int
core507d_caps_init(struct nouveau_drm *drm, struct nv50_disp *disp)
{
	u32 *push = evo_wait(&disp->core->chan, 2);

	if (push) {
		evo_mthd(push, 0x008c, 1);
		evo_data(push, 0x0);
		evo_kick(push, &disp->core->chan);
	}

	return 0;
}

void
core507d_init(struct nv50_core *core)
{
	u32 *push;
	if ((push = evo_wait(&core->chan, 2))) {
		evo_mthd(push, 0x0088, 1);
		evo_data(push, core->chan.sync.handle);
		evo_kick(push, &core->chan);
	}
}

static const struct nv50_core_func
core507d = {
	.init = core507d_init,
	.ntfy_init = core507d_ntfy_init,
	.caps_init = core507d_caps_init,
	.ntfy_wait_done = core507d_ntfy_wait_done,
	.update = core507d_update,
	.head = &head507d,
	.dac = &dac507d,
	.sor = &sor507d,
	.pior = &pior507d,
};

int
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
