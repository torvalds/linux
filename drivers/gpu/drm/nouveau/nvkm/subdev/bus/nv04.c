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

static void
nv04_bus_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_bus *bus = nvkm_bus(subdev);
	u32 stat = nv_rd32(bus, 0x001100) & nv_rd32(bus, 0x001140);

	if (stat & 0x00000001) {
		nv_error(bus, "BUS ERROR\n");
		stat &= ~0x00000001;
		nv_wr32(bus, 0x001100, 0x00000001);
	}

	if (stat & 0x00000110) {
		subdev = nvkm_subdev(subdev, NVDEV_SUBDEV_GPIO);
		if (subdev && subdev->intr)
			subdev->intr(subdev);
		stat &= ~0x00000110;
		nv_wr32(bus, 0x001100, 0x00000110);
	}

	if (stat) {
		nv_error(bus, "unknown intr 0x%08x\n", stat);
		nv_mask(bus, 0x001140, stat, 0x00000000);
	}
}

static int
nv04_bus_init(struct nvkm_object *object)
{
	struct nvkm_bus *bus = (void *)object;

	nv_wr32(bus, 0x001100, 0xffffffff);
	nv_wr32(bus, 0x001140, 0x00000111);

	return nvkm_bus_init(bus);
}

int
nv04_bus_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nv04_bus_impl *impl = (void *)oclass;
	struct nvkm_bus *bus;
	int ret;

	ret = nvkm_bus_create(parent, engine, oclass, &bus);
	*pobject = nv_object(bus);
	if (ret)
		return ret;

	nv_subdev(bus)->intr = impl->intr;
	bus->hwsq_exec = impl->hwsq_exec;
	bus->hwsq_size = impl->hwsq_size;
	return 0;
}

struct nvkm_oclass *
nv04_bus_oclass = &(struct nv04_bus_impl) {
	.base.handle = NV_SUBDEV(BUS, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_bus_ctor,
		.dtor = _nvkm_bus_dtor,
		.init = nv04_bus_init,
		.fini = _nvkm_bus_fini,
	},
	.intr = nv04_bus_intr,
}.base;
