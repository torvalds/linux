/*
 * Copyright 2012 Nouveau Community
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
 * Authors: Martin Peres <martin.peres@labri.fr>
 *          Ben Skeggs
 */
#include "priv.h"

static void
gf100_bus_intr(struct nvkm_bus *bus)
{
	struct nvkm_subdev *subdev = &bus->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x001100) & nvkm_rd32(device, 0x001140);

	if (stat & 0x0000000e) {
		u32 addr = nvkm_rd32(device, 0x009084);
		u32 data = nvkm_rd32(device, 0x009088);

		nvkm_error_ratelimited(subdev,
				       "MMIO %s of %08x FAULT at %06x [ %s%s%s]\n",
				       (addr & 0x00000002) ? "write" : "read", data,
				       (addr & 0x00fffffc),
				       (stat & 0x00000002) ? "!ENGINE " : "",
				       (stat & 0x00000004) ? "PRIVRING " : "",
				       (stat & 0x00000008) ? "TIMEOUT " : "");

		nvkm_wr32(device, 0x009084, 0x00000000);
		nvkm_wr32(device, 0x001100, (stat & 0x0000000e));
		stat &= ~0x0000000e;
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_mask(device, 0x001140, stat, 0x00000000);
	}
}

static void
gf100_bus_init(struct nvkm_bus *bus)
{
	struct nvkm_device *device = bus->subdev.device;
	nvkm_wr32(device, 0x001100, 0xffffffff);
	nvkm_wr32(device, 0x001140, 0x0000000e);
}

static const struct nvkm_bus_func
gf100_bus = {
	.init = gf100_bus_init,
	.intr = gf100_bus_intr,
};

int
gf100_bus_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_bus **pbus)
{
	return nvkm_bus_new_(&gf100_bus, device, type, inst, pbus);
}
