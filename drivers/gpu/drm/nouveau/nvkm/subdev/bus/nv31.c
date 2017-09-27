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

#include <subdev/gpio.h>
#include <subdev/therm.h>

static void
nv31_bus_intr(struct nvkm_bus *bus)
{
	struct nvkm_subdev *subdev = &bus->subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x001100) & nvkm_rd32(device, 0x001140);
	u32 gpio = nvkm_rd32(device, 0x001104) & nvkm_rd32(device, 0x001144);

	if (gpio) {
		struct nvkm_gpio *gpio = device->gpio;
		if (gpio)
			nvkm_subdev_intr(&gpio->subdev);
	}

	if (stat & 0x00000008) {  /* NV41- */
		u32 addr = nvkm_rd32(device, 0x009084);
		u32 data = nvkm_rd32(device, 0x009088);

		nvkm_error(subdev, "MMIO %s of %08x FAULT at %06x\n",
			   (addr & 0x00000002) ? "write" : "read", data,
			   (addr & 0x00fffffc));

		stat &= ~0x00000008;
		nvkm_wr32(device, 0x001100, 0x00000008);
	}

	if (stat & 0x00070000) {
		struct nvkm_therm *therm = device->therm;
		if (therm)
			nvkm_subdev_intr(&therm->subdev);
		stat &= ~0x00070000;
		nvkm_wr32(device, 0x001100, 0x00070000);
	}

	if (stat) {
		nvkm_error(subdev, "intr %08x\n", stat);
		nvkm_mask(device, 0x001140, stat, 0x00000000);
	}
}

static void
nv31_bus_init(struct nvkm_bus *bus)
{
	struct nvkm_device *device = bus->subdev.device;
	nvkm_wr32(device, 0x001100, 0xffffffff);
	nvkm_wr32(device, 0x001140, 0x00070008);
}

static const struct nvkm_bus_func
nv31_bus = {
	.init = nv31_bus_init,
	.intr = nv31_bus_intr,
};

int
nv31_bus_new(struct nvkm_device *device, int index, struct nvkm_bus **pbus)
{
	return nvkm_bus_new_(&nv31_bus, device, index, pbus);
}
