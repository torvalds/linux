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

#include <engine/falcon.h>
#include <engine/vp.h>

struct nv98_vp_priv {
	struct nouveau_falcon base;
};

/*******************************************************************************
 * VP object classes
 ******************************************************************************/

static struct nouveau_oclass
nv98_vp_sclass[] = {
	{ 0x88b2, &nouveau_object_ofuncs },
	{ 0x85b2, &nouveau_object_ofuncs },
	{},
};

/*******************************************************************************
 * PVP context
 ******************************************************************************/

static struct nouveau_oclass
nv98_vp_cclass = {
	.handle = NV_ENGCTX(VP, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_falcon_context_ctor,
		.dtor = _nouveau_falcon_context_dtor,
		.init = _nouveau_falcon_context_init,
		.fini = _nouveau_falcon_context_fini,
		.rd32 = _nouveau_falcon_context_rd32,
		.wr32 = _nouveau_falcon_context_wr32,
	},
};

/*******************************************************************************
 * PVP engine/subdev functions
 ******************************************************************************/

static int
nv98_vp_init(struct nouveau_object *object)
{
	struct nv98_vp_priv *priv = (void *)object;
	int ret;

	ret = nouveau_falcon_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, 0x085010, 0x0000ffd2);
	nv_wr32(priv, 0x08501c, 0x0000fff2);
	return 0;
}

static int
nv98_vp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv98_vp_priv *priv;
	int ret;

	ret = nouveau_falcon_create(parent, engine, oclass, 0x085000, true,
				    "PVP", "vp", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x01020000;
	nv_engine(priv)->cclass = &nv98_vp_cclass;
	nv_engine(priv)->sclass = nv98_vp_sclass;
	return 0;
}

struct nouveau_oclass
nv98_vp_oclass = {
	.handle = NV_ENGINE(VP, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_vp_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = nv98_vp_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};
