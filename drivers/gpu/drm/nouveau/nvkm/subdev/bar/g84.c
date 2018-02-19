/*
 * Copyright 2015 Red Hat Inc.
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
 *
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "nv50.h"

#include <subdev/timer.h>

void
g84_bar_flush(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	unsigned long flags;
	spin_lock_irqsave(&bar->lock, flags);
	nvkm_wr32(device, 0x070000, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x070000) & 0x00000002))
			break;
	);
	spin_unlock_irqrestore(&bar->lock, flags);
}

static const struct nvkm_bar_func
g84_bar_func = {
	.dtor = nv50_bar_dtor,
	.oneinit = nv50_bar_oneinit,
	.init = nv50_bar_init,
	.bar1.init = nv50_bar_bar1_init,
	.bar1.fini = nv50_bar_bar1_fini,
	.bar1.wait = nv50_bar_bar1_wait,
	.bar1.vmm = nv50_bar_bar1_vmm,
	.bar2.init = nv50_bar_bar2_init,
	.bar2.fini = nv50_bar_bar2_fini,
	.bar2.wait = nv50_bar_bar1_wait,
	.bar2.vmm = nv50_bar_bar2_vmm,
	.flush = g84_bar_flush,
};

int
g84_bar_new(struct nvkm_device *device, int index, struct nvkm_bar **pbar)
{
	return nv50_bar_new_(&g84_bar_func, device, index, 0x200, pbar);
}
