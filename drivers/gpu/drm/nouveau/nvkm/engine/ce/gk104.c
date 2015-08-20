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
#include <engine/ce.h>
#include <engine/fifo.h>

#include <nvif/class.h>

static void
gk104_ce_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_device *device = subdev->device;
	const int idx = nv_subidx(subdev) - NVDEV_ENGINE_CE0;
	u32 stat = nvkm_rd32(device, 0x104908 + (idx * 0x1000));

	if (stat) {
		nvkm_warn(subdev, "intr %08x\n", stat);
		nvkm_wr32(device, 0x104908 + (idx * 0x1000), stat);
	}
}

static const struct nvkm_engine_func
gk104_ce = {
	.sclass = {
		{ -1, -1, KEPLER_DMA_COPY_A },
		{}
	}
};

static int
gk104_ce0_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_engine *ce;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE0", "ce0", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	ce->func = &gk104_ce;
	nv_subdev(ce)->unit = 0x00000040;
	nv_subdev(ce)->intr = gk104_ce_intr;
	return 0;
}

static int
gk104_ce1_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_engine *ce;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE1", "ce1", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	ce->func = &gk104_ce;
	nv_subdev(ce)->unit = 0x00000080;
	nv_subdev(ce)->intr = gk104_ce_intr;
	return 0;
}

static int
gk104_ce2_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_engine *ce;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE2", "ce2", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	ce->func = &gk104_ce;
	nv_subdev(ce)->unit = 0x00200000;
	nv_subdev(ce)->intr = gk104_ce_intr;
	return 0;
}

struct nvkm_oclass
gk104_ce0_oclass = {
	.handle = NV_ENGINE(CE0, 0xe0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk104_ce0_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};

struct nvkm_oclass
gk104_ce1_oclass = {
	.handle = NV_ENGINE(CE1, 0xe0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk104_ce1_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};

struct nvkm_oclass
gk104_ce2_oclass = {
	.handle = NV_ENGINE(CE2, 0xe0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk104_ce2_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};
