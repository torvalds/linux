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

#include <subdev/mc.h>

struct nv44_mc_priv {
	struct nouveau_mc base;
};

static int
nv44_mc_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv44_mc_priv *priv;
	int ret;

	ret = nouveau_mc_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->intr = nouveau_mc_intr;
	priv->base.intr_map = nv04_mc_intr;
	return 0;
}

static int
nv44_mc_init(struct nouveau_object *object)
{
	struct nv44_mc_priv *priv = (void *)object;
	u32 tmp = nv_rd32(priv, 0x10020c);

	nv_wr32(priv, 0x000200, 0xffffffff); /* everything enabled */

	nv_wr32(priv, 0x001700, tmp);
	nv_wr32(priv, 0x001704, 0);
	nv_wr32(priv, 0x001708, 0);
	nv_wr32(priv, 0x00170c, tmp);

	return nouveau_mc_init(&priv->base);
}

struct nouveau_oclass
nv44_mc_oclass = {
	.handle = NV_SUBDEV(MC, 0x44),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv44_mc_ctor,
		.dtor = _nouveau_mc_dtor,
		.init = nv44_mc_init,
		.fini = _nouveau_mc_fini,
	},
};
