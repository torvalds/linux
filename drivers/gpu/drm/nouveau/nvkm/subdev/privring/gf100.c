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
gf100_privring_intr_hub(struct nvkm_subdev *privring, int i)
{
	struct nvkm_device *device = privring->device;
	u32 addr = nvkm_rd32(device, 0x122120 + (i * 0x0400));
	u32 data = nvkm_rd32(device, 0x122124 + (i * 0x0400));
	u32 stat = nvkm_rd32(device, 0x122128 + (i * 0x0400));
	nvkm_debug(privring, "HUB%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

static void
gf100_privring_intr_rop(struct nvkm_subdev *privring, int i)
{
	struct nvkm_device *device = privring->device;
	u32 addr = nvkm_rd32(device, 0x124120 + (i * 0x0400));
	u32 data = nvkm_rd32(device, 0x124124 + (i * 0x0400));
	u32 stat = nvkm_rd32(device, 0x124128 + (i * 0x0400));
	nvkm_debug(privring, "ROP%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

static void
gf100_privring_intr_gpc(struct nvkm_subdev *privring, int i)
{
	struct nvkm_device *device = privring->device;
	u32 addr = nvkm_rd32(device, 0x128120 + (i * 0x0400));
	u32 data = nvkm_rd32(device, 0x128124 + (i * 0x0400));
	u32 stat = nvkm_rd32(device, 0x128128 + (i * 0x0400));
	nvkm_debug(privring, "GPC%d: %06x %08x (%08x)\n", i, addr, data, stat);
}

void
gf100_privring_intr(struct nvkm_subdev *privring)
{
	struct nvkm_device *device = privring->device;
	u32 intr0 = nvkm_rd32(device, 0x121c58);
	u32 intr1 = nvkm_rd32(device, 0x121c5c);
	u32 hubnr = nvkm_rd32(device, 0x121c70);
	u32 ropnr = nvkm_rd32(device, 0x121c74);
	u32 gpcnr = nvkm_rd32(device, 0x121c78);
	u32 i;

	for (i = 0; (intr0 & 0x0000ff00) && i < hubnr; i++) {
		u32 stat = 0x00000100 << i;
		if (intr0 & stat) {
			gf100_privring_intr_hub(privring, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; (intr0 & 0xffff0000) && i < ropnr; i++) {
		u32 stat = 0x00010000 << i;
		if (intr0 & stat) {
			gf100_privring_intr_rop(privring, i);
			intr0 &= ~stat;
		}
	}

	for (i = 0; intr1 && i < gpcnr; i++) {
		u32 stat = 0x00000001 << i;
		if (intr1 & stat) {
			gf100_privring_intr_gpc(privring, i);
			intr1 &= ~stat;
		}
	}

	nvkm_mask(device, 0x121c4c, 0x0000003f, 0x00000002);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x121c4c) & 0x0000003f))
			break;
	);
}

static int
gf100_privring_init(struct nvkm_subdev *privring)
{
	struct nvkm_device *device = privring->device;
	nvkm_mask(device, 0x122310, 0x0003ffff, 0x00000800);
	nvkm_wr32(device, 0x12232c, 0x00100064);
	nvkm_wr32(device, 0x122330, 0x00100064);
	nvkm_wr32(device, 0x122334, 0x00100064);
	nvkm_mask(device, 0x122348, 0x0003ffff, 0x00000100);
	return 0;
}

static const struct nvkm_subdev_func
gf100_privring = {
	.init = gf100_privring_init,
	.intr = gf100_privring_intr,
};

int
gf100_privring_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		   struct nvkm_subdev **pprivring)
{
	return nvkm_subdev_new_(&gf100_privring, device, type, inst, pprivring);
}
