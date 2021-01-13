/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 */
#include "priv.h"
#include <subdev/timer.h>

static void
gk104_ibus_intr_hub(struct nvkm_subdev *ibus, int i)
{
	struct nvkm_device *device = ibus->device;
	u32 addr = nvkm_rd32(device, 0x122120 + (i * 0x0800));
	u32 data = nvkm_rd32(device, 0x122124 + (i * 0x0800));
	u32 stat = nvkm_rd32(device, 0x122128 + (i * 0x0800));
	nvkm_debug(ibus, "HUB%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

static void
gk104_ibus_intr_rop(struct nvkm_subdev *ibus, int i)
{
	struct nvkm_device *device = ibus->device;
	u32 addr = nvkm_rd32(device, 0x124120 + (i * 0x0800));
	u32 data = nvkm_rd32(device, 0x124124 + (i * 0x0800));
	u32 stat = nvkm_rd32(device, 0x124128 + (i * 0x0800));
	nvkm_debug(ibus, "ROP%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

static void
gk104_ibus_intr_gpc(struct nvkm_subdev *ibus, int i)
{
	struct nvkm_device *device = ibus->device;
	u32 addr = nvkm_rd32(device, 0x128120 + (i * 0x0800));
	u32 data = nvkm_rd32(device, 0x128124 + (i * 0x0800));
	u32 stat = nvkm_rd32(device, 0x128128 + (i * 0x0800));
	nvkm_debug(ibus, "GPC%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

void
gk104_ibus_intr(struct nvkm_subdev *ibus)
{
	struct nvkm_device *device = ibus->device;
	u32 intr0 = nvkm_rd32(device, 0x120058);
	u32 intr1 = nvkm_rd32(device, 0x12005c);
	u32 hubnr = nvkm_rd32(device, 0x120070);
	u32 ropnr = nvkm_rd32(device, 0x120074);
	u32 gpcnr = nvkm_rd32(device, 0x120078);
	u32 i;

	for (i = 0; (intr0 & 0x0000ff00) && i < hubnr; i++) {
		u32 stat = 0x00000100 << i;
		if (intr0 & stat) {
			gk104_ibus_intr_hub(ibus, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; (intr0 & 0xffff0000) && i < ropnr; i++) {
		u32 stat = 0x00010000 << i;
		if (intr0 & stat) {
			gk104_ibus_intr_rop(ibus, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; intr1 && i < gpcnr; i++) {
		u32 stat = 0x00000001 << i;
		if (intr1 & stat) {
			gk104_ibus_intr_gpc(ibus, i);
			intr1 &= ~stat;
		}
	}

	nvkm_mask(device, 0x12004c, 0x0000003f, 0x00000002);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x12004c) & 0x0000003f))
			break;
	);
}

static int
gk104_ibus_init(struct nvkm_subdev *ibus)
{
	struct nvkm_device *device = ibus->device;
	nvkm_mask(device, 0x122318, 0x0003ffff, 0x00001000);
	nvkm_mask(device, 0x12231c, 0x0003ffff, 0x00000200);
	nvkm_mask(device, 0x122310, 0x0003ffff, 0x00000800);
	nvkm_mask(device, 0x122348, 0x0003ffff, 0x00000100);
	nvkm_mask(device, 0x1223b0, 0x0003ffff, 0x00000fff);
	nvkm_mask(device, 0x122348, 0x0003ffff, 0x00000200);
	nvkm_mask(device, 0x122358, 0x0003ffff, 0x00002880);
	return 0;
}

static const struct nvkm_subdev_func
gk104_ibus = {
	.preinit = gk104_ibus_init,
	.init = gk104_ibus_init,
	.intr = gk104_ibus_intr,
};

int
gk104_ibus_new(struct nvkm_device *device, int index,
	       struct nvkm_subdev **pibus)
{
	struct nvkm_subdev *ibus;
	if (!(ibus = *pibus = kzalloc(sizeof(*ibus), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&gk104_ibus, device, index, ibus);
	return 0;
}
