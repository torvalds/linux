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

#include <core/gpuobj.h>

#include <subdev/fb.h>
#include <engine/dmaobj.h>

struct nvc0_dmaeng_priv {
	struct nouveau_dmaeng base;
};

struct nvc0_dmaobj_priv {
	struct nouveau_dmaobj base;
};

static int
nvc0_dmaobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nvc0_dmaobj_priv *dmaobj;
	int ret;

	ret = nouveau_dmaobj_create(parent, engine, oclass, data, size, &dmaobj);
	*pobject = nv_object(dmaobj);
	if (ret)
		return ret;

	if (dmaobj->base.target != NV_MEM_TARGET_VM || dmaobj->base.start)
		return -EINVAL;

	return 0;
}

static struct nouveau_ofuncs
nvc0_dmaobj_ofuncs = {
	.ctor = nvc0_dmaobj_ctor,
	.dtor = _nouveau_dmaobj_dtor,
	.init = _nouveau_dmaobj_init,
	.fini = _nouveau_dmaobj_fini,
};

static struct nouveau_oclass
nvc0_dmaobj_sclass[] = {
	{ 0x0002, &nvc0_dmaobj_ofuncs },
	{ 0x0003, &nvc0_dmaobj_ofuncs },
	{ 0x003d, &nvc0_dmaobj_ofuncs },
	{}
};

static int
nvc0_dmaeng_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nvc0_dmaeng_priv *priv;
	int ret;

	ret = nouveau_dmaeng_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.sclass = nvc0_dmaobj_sclass;
	return 0;
}

struct nouveau_oclass
nvc0_dmaeng_oclass = {
	.handle = NV_ENGINE(DMAOBJ, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_dmaeng_ctor,
		.dtor = _nouveau_dmaeng_dtor,
		.init = _nouveau_dmaeng_init,
		.fini = _nouveau_dmaeng_fini,
	},
};
