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
 * Authors: Ben Skeggs, Ilia Mirkin
 */
#include <engine/bsp.h>

#include <nvif/class.h>

static const struct nvkm_xtensa_func
g84_bsp_func = {
	.sclass = {
		{ -1, -1, NV74_BSP },
		{}
	}
};

static int
g84_bsp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nvkm_xtensa *bsp;
	int ret;

	ret = nvkm_xtensa_create(parent, engine, oclass, 0x103000, true,
				 "PBSP", "bsp", &bsp);
	*pobject = nv_object(bsp);
	if (ret)
		return ret;

	bsp->func = &g84_bsp_func;
	nv_subdev(bsp)->unit = 0x04008000;
	bsp->fifo_val = 0x1111;
	bsp->unkd28 = 0x90044;
	return 0;
}

struct nvkm_oclass
g84_bsp_oclass = {
	.handle = NV_ENGINE(BSP, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_bsp_ctor,
		.dtor = _nvkm_xtensa_dtor,
		.init = _nvkm_xtensa_init,
		.fini = _nvkm_xtensa_fini,
	},
};
