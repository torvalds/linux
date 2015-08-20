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
#include <engine/falcon.h>
#include "fuc/gf100.fuc3.h"

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nvkm_oclass
gf100_ce0_sclass[] = {
	{ 0x90b5, &nvkm_object_ofuncs },
	{},
};

static struct nvkm_oclass
gf100_ce1_sclass[] = {
	{ 0x90b8, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCE context
 ******************************************************************************/

static struct nvkm_ofuncs
gf100_ce_context_ofuncs = {
	.ctor = _nvkm_falcon_context_ctor,
	.dtor = _nvkm_falcon_context_dtor,
	.init = _nvkm_falcon_context_init,
	.fini = _nvkm_falcon_context_fini,
	.rd32 = _nvkm_falcon_context_rd32,
	.wr32 = _nvkm_falcon_context_wr32,
};

static struct nvkm_oclass
gf100_ce0_cclass = {
	.handle = NV_ENGCTX(CE0, 0xc0),
	.ofuncs = &gf100_ce_context_ofuncs,
};

static struct nvkm_oclass
gf100_ce1_cclass = {
	.handle = NV_ENGCTX(CE1, 0xc0),
	.ofuncs = &gf100_ce_context_ofuncs,
};

/*******************************************************************************
 * PCE engine/subdev functions
 ******************************************************************************/

static int
gf100_ce_init(struct nvkm_object *object)
{
	struct nvkm_falcon *ce = (void *)object;
	struct nvkm_device *device = ce->engine.subdev.device;
	const int idx = nv_engidx(&ce->engine) - NVDEV_ENGINE_CE0;
	u32 base = idx * 0x1000;
	int ret;

	ret = nvkm_falcon_init(ce);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x104084 + base, idx);
	return 0;
}

static int
gf100_ce0_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_falcon *ce;
	int ret;

	ret = nvkm_falcon_create(parent, engine, oclass, 0x104000, true,
				 "PCE0", "ce0", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	nv_subdev(ce)->unit = 0x00000040;
	nv_subdev(ce)->intr = gt215_ce_intr;
	nv_engine(ce)->cclass = &gf100_ce0_cclass;
	nv_engine(ce)->sclass = gf100_ce0_sclass;
	nv_falcon(ce)->code.data = gf100_ce_code;
	nv_falcon(ce)->code.size = sizeof(gf100_ce_code);
	nv_falcon(ce)->data.data = gf100_ce_data;
	nv_falcon(ce)->data.size = sizeof(gf100_ce_data);
	return 0;
}

static int
gf100_ce1_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_falcon *ce;
	int ret;

	ret = nvkm_falcon_create(parent, engine, oclass, 0x105000, true,
				 "PCE1", "ce1", &ce);
	*pobject = nv_object(ce);
	if (ret)
		return ret;

	nv_subdev(ce)->unit = 0x00000080;
	nv_subdev(ce)->intr = gt215_ce_intr;
	nv_engine(ce)->cclass = &gf100_ce1_cclass;
	nv_engine(ce)->sclass = gf100_ce1_sclass;
	nv_falcon(ce)->code.data = gf100_ce_code;
	nv_falcon(ce)->code.size = sizeof(gf100_ce_code);
	nv_falcon(ce)->data.data = gf100_ce_data;
	nv_falcon(ce)->data.size = sizeof(gf100_ce_data);
	return 0;
}

struct nvkm_oclass
gf100_ce0_oclass = {
	.handle = NV_ENGINE(CE0, 0xc0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_ce0_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = gf100_ce_init,
		.fini = _nvkm_falcon_fini,
	},
};

struct nvkm_oclass
gf100_ce1_oclass = {
	.handle = NV_ENGINE(CE1, 0xc0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_ce1_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = gf100_ce_init,
		.fini = _nvkm_falcon_fini,
	},
};
