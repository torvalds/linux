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

#include <subdev/timer.h>

static int
g94_bus_hwsq_exec(struct nvkm_bus *bus, u32 *data, u32 size)
{
	struct nvkm_device *device = bus->subdev.device;
	int i;

	nvkm_mask(device, 0x001098, 0x00000008, 0x00000000);
	nvkm_wr32(device, 0x001304, 0x00000000);
	nvkm_wr32(device, 0x001318, 0x00000000);
	for (i = 0; i < size; i++)
		nvkm_wr32(device, 0x080000 + (i * 4), data[i]);
	nvkm_mask(device, 0x001098, 0x00000018, 0x00000018);
	nvkm_wr32(device, 0x00130c, 0x00000001);

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x001308) & 0x00000100))
			break;
	) < 0)
		return -ETIMEDOUT;

	return 0;
}

static const struct nvkm_bus_func
g94_bus = {
	.init = nv50_bus_init,
	.intr = nv50_bus_intr,
	.hwsq_exec = g94_bus_hwsq_exec,
	.hwsq_size = 128,
};

int
g94_bus_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	    struct nvkm_bus **pbus)
{
	return nvkm_bus_new_(&g94_bus, device, type, inst, pbus);
}
