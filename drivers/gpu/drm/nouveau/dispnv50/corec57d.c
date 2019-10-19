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

static void
corec57d_init(struct nv50_core *core)
{
	const u32 windows = 8; /*XXX*/
	u32 *push, i;
	if ((push = evo_wait(&core->chan, 2 + 6 * windows + 2))) {
		evo_mthd(push, 0x0208, 1);
		evo_data(push, core->chan.sync.handle);
		for (i = 0; i < windows; i++) {
			evo_mthd(push, 0x1000 + (i * 0x080), 3);
			evo_data(push, i >> 1);
			evo_data(push, 0x0000000f);
			evo_data(push, 0x00000000);
			evo_mthd(push, 0x1010 + (i * 0x080), 1);
			evo_data(push, 0x00117fff);
		}
		evo_mthd(push, 0x0200, 1);
		evo_data(push, 0x00000001);
		evo_kick(push, &core->chan);
	}
}

static const struct nv50_core_func
corec57d = {
	.init = corec57d_init,
	.ntfy_init = corec37d_ntfy_init,
	.ntfy_wait_done = corec37d_ntfy_wait_done,
	.update = corec37d_update,
	.head = &headc57d,
	.sor = &sorc37d,
};

int
corec57d_new(struct nouveau_drm *drm, s32 oclass, struct nv50_core **pcore)
{
	return core507d_new_(&corec57d, drm, oclass, pcore);
}
