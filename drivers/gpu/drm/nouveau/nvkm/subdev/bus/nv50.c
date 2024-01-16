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

#include <subdev/therm.h>
#include <subdev/timer.h>

static int
nv50_bus_hwsq_exec(struct nvkm_bus *bus, u32 *data, u32 size)
{
	struct nvkm_device *device = bus->subdev.device;
	int i;

	nvkm_mask(device, 0x001098, 0x00000008, 0x00000000);
	nvkm_wr32(device, 0x001304, 0x00000000);
	for (i = 0; i < size; i++)
		nvkm_wr32(device, 0x001400 + (i * 4), data[i]);
	nvkm_mask(device, 0x001098, 0x00000018, 0x00000018);
	nvkm_wr32(device, 0x00130c, 0x00000003);

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x001308) & 0x00000100))
			break;
	) < 0)
		return -ETIMEDOUT;

	return 0;
}

void
nv50_bus_intr(struct nvkm_bus *bus)
{
	struct nvkm_subdev *subdev = &bus->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x001100) & nvkm_rd32(device, 0x001140);

	if (stat & 0x00000008) {
		u32 addr = nvkm_rd32(device, 0x009084);
		u32 data = nvkm_rd32(device, 0x009088);

		nvkm_error_ratelimited(subdev, "MMIO %s of %08x FAULT at %06x\n",
				       (addr & 0x00000002) ? "write" : "read", data,
				       (addr & 0x00fffffc));

		stat &= ~0x00000008;
		nvkm_wr32(device, 0x001100, 0x00000008);
	}

	if (stat & 0x00010000) {
		struct nvkm_therm *therm = device->therm;
		if (therm)
			nvkm_subdev_intr(&therm->subdev);
		stat &= ~0x00010000;
		nvkm_wr32(device, 0x001100, 0x00010000);
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_mask(device, 0x001140, stat, 0);
	}
}

void
nv50_bus_init(struct nvkm_bus *bus)
{
	struct nvkm_device *device = bus->subdev.device;
	nvkm_wr32(device, 0x001100, 0xffffffff);
	nvkm_wr32(device, 0x001140, 0x00010008);
}

static const struct nvkm_bus_func
nv50_bus = {
	.init = nv50_bus_init,
	.intr = nv50_bus_intr,
	.hwsq_exec = nv50_bus_hwsq_exec,
	.hwsq_size = 64,
};

int
nv50_bus_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_bus **pbus)
{
	return nvkm_bus_new_(&nv50_bus, device, type, inst, pbus);
}
