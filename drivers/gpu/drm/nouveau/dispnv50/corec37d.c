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

#include <nouveau_bo.h>

void
corec37d_wndw_owner(struct nv50_core *core)
{
	const u32 windows = 8; /*XXX*/
	u32 *push, i;
	if ((push = evo_wait(&core->chan, 2 * windows))) {
		for (i = 0; i < windows; i++) {
			evo_mthd(push, 0x1000 + (i * 0x080), 1);
			evo_data(push, i >> 1);
		}
		evo_kick(push, &core->chan);
	}
}

void
corec37d_update(struct nv50_core *core, u32 *interlock, bool ntfy)
{
	u32 *push;
	if ((push = evo_wait(&core->chan, 9))) {
		if (ntfy) {
			evo_mthd(push, 0x020c, 1);
			evo_data(push, 0x00001000 | NV50_DISP_CORE_NTFY);
		}

		evo_mthd(push, 0x0218, 2);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_CURS]);
		evo_data(push, interlock[NV50_DISP_INTERLOCK_WNDW]);
		evo_mthd(push, 0x0200, 1);
		evo_data(push, 0x00000001);

		if (ntfy) {
			evo_mthd(push, 0x020c, 1);
			evo_data(push, 0x00000000);
		}
		evo_kick(push, &core->chan);
	}
}

int
corec37d_ntfy_wait_done(struct nouveau_bo *bo, u32 offset,
			struct nvif_device *device)
{
	u32 data;
	s64 time = nvif_msec(device, 2000ULL,
		data = nouveau_bo_rd32(bo, offset / 4 + 0);
		if ((data & 0xc0000000) == 0x80000000)
			break;
		usleep_range(1, 2);
	);
	return time < 0 ? time : 0;
}

void
corec37d_ntfy_init(struct nouveau_bo *bo, u32 offset)
{
	nouveau_bo_wr32(bo, offset / 4 + 0, 0x00000000);
	nouveau_bo_wr32(bo, offset / 4 + 1, 0x00000000);
	nouveau_bo_wr32(bo, offset / 4 + 2, 0x00000000);
	nouveau_bo_wr32(bo, offset / 4 + 3, 0x00000000);
}

static void
corec37d_init(struct nv50_core *core)
{
	const u32 windows = 8; /*XXX*/
	u32 *push, i;
	if ((push = evo_wait(&core->chan, 2 + 5 * windows))) {
		evo_mthd(push, 0x0208, 1);
		evo_data(push, core->chan.sync.handle);
		for (i = 0; i < windows; i++) {
			evo_mthd(push, 0x1004 + (i * 0x080), 2);
			evo_data(push, 0x0000001f);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x1010 + (i * 0x080), 1);
			evo_data(push, 0x00127fff);
		}
		evo_kick(push, &core->chan);
		core->assign_windows = true;
	}
}

static const struct nv50_core_func
corec37d = {
	.init = corec37d_init,
	.ntfy_init = corec37d_ntfy_init,
	.ntfy_wait_done = corec37d_ntfy_wait_done,
	.update = corec37d_update,
	.wndw.owner = corec37d_wndw_owner,
	.head = &headc37d,
	.sor = &sorc37d,
};

int
corec37d_new(struct nouveau_drm *drm, s32 oclass, struct nv50_core **pcore)
{
	return core507d_new_(&corec37d, drm, oclass, pcore);
}
