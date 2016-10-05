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
#include "priv.h"

static void
nvkm_bus_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_bus *bus = nvkm_bus(subdev);
	bus->func->intr(bus);
}

static int
nvkm_bus_init(struct nvkm_subdev *subdev)
{
	struct nvkm_bus *bus = nvkm_bus(subdev);
	bus->func->init(bus);
	return 0;
}

static void *
nvkm_bus_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_bus(subdev);
}

static const struct nvkm_subdev_func
nvkm_bus = {
	.dtor = nvkm_bus_dtor,
	.init = nvkm_bus_init,
	.intr = nvkm_bus_intr,
};

int
nvkm_bus_new_(const struct nvkm_bus_func *func, struct nvkm_device *device,
	      int index, struct nvkm_bus **pbus)
{
	struct nvkm_bus *bus;
	if (!(bus = *pbus = kzalloc(sizeof(*bus), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_bus, device, index, &bus->subdev);
	bus->func = func;
	return 0;
}
