/*
 * Copyright 2013 Red Hat Inc.
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
#include "nv40.h"

static int
nv44_ram_create(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_fb *fb = nvkm_fb(parent);
	struct nv40_ram *ram;
	struct nvkm_device *device = fb->subdev.device;
	u32 fb474 = nvkm_rd32(device, 0x100474);
	int ret;

	ret = nvkm_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	if (fb474 & 0x00000004)
		ram->base.type = NV_MEM_TYPE_GDDR3;
	if (fb474 & 0x00000002)
		ram->base.type = NV_MEM_TYPE_DDR2;
	if (fb474 & 0x00000001)
		ram->base.type = NV_MEM_TYPE_DDR1;

	ram->base.size = nvkm_rd32(device, 0x10020c) & 0xff000000;
	ram->base.calc = nv40_ram_calc;
	ram->base.prog = nv40_ram_prog;
	ram->base.tidy = nv40_ram_tidy;
	return 0;
}

struct nvkm_oclass
nv44_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_ram_create,
		.dtor = _nvkm_ram_dtor,
		.init = _nvkm_ram_init,
		.fini = _nvkm_ram_fini,
	}
};
