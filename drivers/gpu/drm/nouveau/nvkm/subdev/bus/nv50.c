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
#include "nv04.h"

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

	return nv_wait(bus, 0x001308, 0x00000100, 0x00000000) ? 0 : -ETIMEDOUT;
}

void
nv50_bus_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_bus *bus = nvkm_bus(subdev);
	struct nvkm_device *device = bus->subdev.device;
	u32 stat = nvkm_rd32(device, 0x001100) & nvkm_rd32(device, 0x001140);

	if (stat & 0x00000008) {
		u32 addr = nvkm_rd32(device, 0x009084);
		u32 data = nvkm_rd32(device, 0x009088);

		nv_error(bus, "MMIO %s of 0x%08x FAULT at 0x%06x\n",
			 (addr & 0x00000002) ? "write" : "read", data,
			 (addr & 0x00fffffc));

		stat &= ~0x00000008;
		nvkm_wr32(device, 0x001100, 0x00000008);
	}

	if (stat & 0x00010000) {
		subdev = nvkm_subdev(bus, NVDEV_SUBDEV_THERM);
		if (subdev && subdev->intr)
			subdev->intr(subdev);
		stat &= ~0x00010000;
		nvkm_wr32(device, 0x001100, 0x00010000);
	}

	if (stat) {
		nv_error(bus, "unknown intr 0x%08x\n", stat);
		nvkm_mask(device, 0x001140, stat, 0);
	}
}

int
nv50_bus_init(struct nvkm_object *object)
{
	struct nvkm_bus *bus = (void *)object;
	struct nvkm_device *device = bus->subdev.device;
	int ret;

	ret = nvkm_bus_init(bus);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x001100, 0xffffffff);
	nvkm_wr32(device, 0x001140, 0x00010008);
	return 0;
}

struct nvkm_oclass *
nv50_bus_oclass = &(struct nv04_bus_impl) {
	.base.handle = NV_SUBDEV(BUS, 0x50),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_bus_ctor,
		.dtor = _nvkm_bus_dtor,
		.init = nv50_bus_init,
		.fini = _nvkm_bus_fini,
	},
	.intr = nv50_bus_intr,
	.hwsq_exec = nv50_bus_hwsq_exec,
	.hwsq_size = 64,
}.base;
