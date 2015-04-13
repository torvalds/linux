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
 * Authors: Ben Skeggs, Maarten Lankhorst, Ilia Mirkin
 */
#include <engine/msvld.h>
#include <engine/falcon.h>

struct g98_msvld_priv {
	struct nvkm_falcon base;
};

/*******************************************************************************
 * MSVLD object classes
 ******************************************************************************/

static struct nvkm_oclass
g98_msvld_sclass[] = {
	{ 0x88b1, &nvkm_object_ofuncs },
	{ 0x85b1, &nvkm_object_ofuncs },
	{ 0x86b1, &nvkm_object_ofuncs },
	{},
};

/*******************************************************************************
 * PMSVLD context
 ******************************************************************************/

static struct nvkm_oclass
g98_msvld_cclass = {
	.handle = NV_ENGCTX(MSVLD, 0x98),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_falcon_context_ctor,
		.dtor = _nvkm_falcon_context_dtor,
		.init = _nvkm_falcon_context_init,
		.fini = _nvkm_falcon_context_fini,
		.rd32 = _nvkm_falcon_context_rd32,
		.wr32 = _nvkm_falcon_context_wr32,
	},
};

/*******************************************************************************
 * PMSVLD engine/subdev functions
 ******************************************************************************/

static int
g98_msvld_init(struct nvkm_object *object)
{
	struct g98_msvld_priv *priv = (void *)object;
	int ret;

	ret = nvkm_falcon_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x084010, 0x0000ffd2);
	nv_wr32(priv, 0x08401c, 0x0000fff2);
	return 0;
}

static int
g98_msvld_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct g98_msvld_priv *priv;
	int ret;

	ret = nvkm_falcon_create(parent, engine, oclass, 0x084000, true,
				 "PMSVLD", "msvld", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x04008000;
	nv_engine(priv)->cclass = &g98_msvld_cclass;
	nv_engine(priv)->sclass = g98_msvld_sclass;
	return 0;
}

struct nvkm_oclass
g98_msvld_oclass = {
	.handle = NV_ENGINE(MSVLD, 0x98),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g98_msvld_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = g98_msvld_init,
		.fini = _nvkm_falcon_fini,
		.rd32 = _nvkm_falcon_rd32,
		.wr32 = _nvkm_falcon_wr32,
	},
};
