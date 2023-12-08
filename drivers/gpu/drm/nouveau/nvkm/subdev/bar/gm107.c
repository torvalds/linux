/*
 * Copyright 2017 Red Hat Inc.
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
#include "gf100.h"

#include <subdev/timer.h>

void
gm107_bar_bar1_wait(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x001710) & 0x00000003))
			break;
	);
}

static void
gm107_bar_bar2_wait(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x001710) & 0x0000000c))
			break;
	);
}

static const struct nvkm_bar_func
gm107_bar_func = {
	.dtor = gf100_bar_dtor,
	.oneinit = gf100_bar_oneinit,
	.bar1.init = gf100_bar_bar1_init,
	.bar1.fini = gf100_bar_bar1_fini,
	.bar1.wait = gm107_bar_bar1_wait,
	.bar1.vmm = gf100_bar_bar1_vmm,
	.bar2.init = gf100_bar_bar2_init,
	.bar2.fini = gf100_bar_bar2_fini,
	.bar2.wait = gm107_bar_bar2_wait,
	.bar2.vmm = gf100_bar_bar2_vmm,
	.flush = g84_bar_flush,
};

int
gm107_bar_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_bar **pbar)
{
	return gf100_bar_new_(&gm107_bar_func, device, type, inst, pbar);
}
