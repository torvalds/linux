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

#include <core/engctx.h>

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nvkm_oclass
gk104_ce_sclass[] = {
	{ 0xa0b5, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCE context
 ******************************************************************************/

static struct nvkm_ofuncs
gk104_ce_context_ofuncs = {
	.ctor = _nvkm_engctx_ctor,
	.dtor = _nvkm_engctx_dtor,
	.init = _nvkm_engctx_init,
	.fini = _nvkm_engctx_fini,
	.rd32 = _nvkm_engctx_rd32,
	.wr32 = _nvkm_engctx_wr32,
};

static struct nvkm_oclass
gk104_ce_cclass = {
	.handle = NV_ENGCTX(CE0, 0xc0),
	.ofuncs = &gk104_ce_context_ofuncs,
};

/*******************************************************************************
 * PCE engine/subdev functions
 ******************************************************************************/

static void
gk104_ce_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_device *device = subdev->device;
	const int idx = nv_subidx(subdev) - NVDEV_ENGINE_CE0;
	struct nvkm_engine *ce = (void *)subdev;
	u32 stat = nvkm_rd32(device, 0x104908 + (idx * 0x1000));

	if (stat) {
		nv_warn(ce, "unhandled intr 0x%08x\n", stat);
		nvkm_wr32(device, 0x104908 + (idx * 0x1000), stat);
	}
}

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

	nv_subdev(ce)->unit = 0x00000040;
	nv_subdev(ce)->intr = gk104_ce_intr;
	nv_engine(ce)->cclass = &gk104_ce_cclass;
	nv_engine(ce)->sclass = gk104_ce_sclass;
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

	nv_subdev(ce)->unit = 0x00000080;
	nv_subdev(ce)->intr = gk104_ce_intr;
	nv_engine(ce)->cclass = &gk104_ce_cclass;
	nv_engine(ce)->sclass = gk104_ce_sclass;
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

	nv_subdev(ce)->unit = 0x00200000;
	nv_subdev(ce)->intr = gk104_ce_intr;
	nv_engine(ce)->cclass = &gk104_ce_cclass;
	nv_engine(ce)->sclass = gk104_ce_sclass;
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
