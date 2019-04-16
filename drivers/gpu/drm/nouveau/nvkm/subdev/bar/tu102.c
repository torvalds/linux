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
#include "gf100.h"

#include <core/memory.h>
#include <subdev/timer.h>

static void
tu102_bar_bar2_wait(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0xb80f50) & 0x0000000c))
			break;
	);
}

static void
tu102_bar_bar2_fini(struct nvkm_bar *bar)
{
	nvkm_mask(bar->subdev.device, 0xb80f48, 0x80000000, 0x00000000);
}

static void
tu102_bar_bar2_init(struct nvkm_bar *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct gf100_bar *bar = gf100_bar(base);
	u32 addr = nvkm_memory_addr(bar->bar[0].inst) >> 12;
	if (bar->bar2_halve)
		addr |= 0x40000000;
	nvkm_wr32(device, 0xb80f48, 0x80000000 | addr);
}

static void
tu102_bar_bar1_wait(struct nvkm_bar *bar)
{
	struct nvkm_device *device = bar->subdev.device;
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0xb80f50) & 0x00000003))
			break;
	);
}

static void
tu102_bar_bar1_fini(struct nvkm_bar *bar)
{
	nvkm_mask(bar->subdev.device, 0xb80f40, 0x80000000, 0x00000000);
}

static void
tu102_bar_bar1_init(struct nvkm_bar *base)
{
	struct nvkm_device *device = base->subdev.device;
	struct gf100_bar *bar = gf100_bar(base);
	const u32 addr = nvkm_memory_addr(bar->bar[1].inst) >> 12;
	nvkm_wr32(device, 0xb80f40, 0x80000000 | addr);
}

static const struct nvkm_bar_func
tu102_bar = {
	.dtor = gf100_bar_dtor,
	.oneinit = gf100_bar_oneinit,
	.bar1.init = tu102_bar_bar1_init,
	.bar1.fini = tu102_bar_bar1_fini,
	.bar1.wait = tu102_bar_bar1_wait,
	.bar1.vmm = gf100_bar_bar1_vmm,
	.bar2.init = tu102_bar_bar2_init,
	.bar2.fini = tu102_bar_bar2_fini,
	.bar2.wait = tu102_bar_bar2_wait,
	.bar2.vmm = gf100_bar_bar2_vmm,
	.flush = g84_bar_flush,
};

int
tu102_bar_new(struct nvkm_device *device, int index, struct nvkm_bar **pbar)
{
	return gf100_bar_new_(&tu102_bar, device, index, pbar);
}
