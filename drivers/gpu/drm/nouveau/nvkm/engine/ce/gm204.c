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
 * Authors: Ben Skeggs
 */
#include <engine/ce.h>

#include <core/engctx.h>

struct gm204_ce_priv {
	struct nvkm_engine base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nvkm_oclass
gm204_ce_sclass[] = {
	{ 0xb0b5, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCE context
 ******************************************************************************/

static struct nvkm_ofuncs
gm204_ce_context_ofuncs = {
	.ctor = _nvkm_engctx_ctor,
	.dtor = _nvkm_engctx_dtor,
	.init = _nvkm_engctx_init,
	.fini = _nvkm_engctx_fini,
	.rd32 = _nvkm_engctx_rd32,
	.wr32 = _nvkm_engctx_wr32,
};

static struct nvkm_oclass
gm204_ce_cclass = {
	.handle = NV_ENGCTX(CE0, 0x24),
	.ofuncs = &gm204_ce_context_ofuncs,
};

/*******************************************************************************
 * PCE engine/subdev functions
 ******************************************************************************/

static void
gm204_ce_intr(struct nvkm_subdev *subdev)
{
	const int ce = nv_subidx(subdev) - NVDEV_ENGINE_CE0;
	struct gm204_ce_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, 0x104908 + (ce * 0x1000));

	if (stat) {
		nv_warn(priv, "unhandled intr 0x%08x\n", stat);
		nv_wr32(priv, 0x104908 + (ce * 0x1000), stat);
	}
}

static int
gm204_ce0_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct gm204_ce_priv *priv;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE0", "ce0", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000040;
	nv_subdev(priv)->intr = gm204_ce_intr;
	nv_engine(priv)->cclass = &gm204_ce_cclass;
	nv_engine(priv)->sclass = gm204_ce_sclass;
	return 0;
}

static int
gm204_ce1_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct gm204_ce_priv *priv;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE1", "ce1", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000080;
	nv_subdev(priv)->intr = gm204_ce_intr;
	nv_engine(priv)->cclass = &gm204_ce_cclass;
	nv_engine(priv)->sclass = gm204_ce_sclass;
	return 0;
}

static int
gm204_ce2_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct gm204_ce_priv *priv;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true,
				 "PCE2", "ce2", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00200000;
	nv_subdev(priv)->intr = gm204_ce_intr;
	nv_engine(priv)->cclass = &gm204_ce_cclass;
	nv_engine(priv)->sclass = gm204_ce_sclass;
	return 0;
}

struct nvkm_oclass
gm204_ce0_oclass = {
	.handle = NV_ENGINE(CE0, 0x24),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_ce0_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};

struct nvkm_oclass
gm204_ce1_oclass = {
	.handle = NV_ENGINE(CE1, 0x24),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_ce1_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};

struct nvkm_oclass
gm204_ce2_oclass = {
	.handle = NV_ENGINE(CE2, 0x24),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_ce2_ctor,
		.dtor = _nvkm_engine_dtor,
		.init = _nvkm_engine_init,
		.fini = _nvkm_engine_fini,
	},
};
