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

#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>

#include <engine/vp.h>

struct nv84_vp_priv {
	struct nouveau_vp base;
};

struct nv84_vp_chan {
	struct nouveau_vp_chan base;
};

/*******************************************************************************
 * VP object classes
 ******************************************************************************/

static struct nouveau_oclass
nv84_vp_sclass[] = {
	{},
};

/*******************************************************************************
 * PVP context
 ******************************************************************************/

static int
nv84_vp_context_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *data, u32 size,
		     struct nouveau_object **pobject)
{
	struct nv84_vp_chan *priv;
	int ret;

	ret = nouveau_vp_context_create(parent, engine, oclass, NULL,
					0, 0, 0, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static void
nv84_vp_context_dtor(struct nouveau_object *object)
{
	struct nv84_vp_chan *priv = (void *)object;
	nouveau_vp_context_destroy(&priv->base);
}

static int
nv84_vp_context_init(struct nouveau_object *object)
{
	struct nv84_vp_chan *priv = (void *)object;
	int ret;

	ret = nouveau_vp_context_init(&priv->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv84_vp_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv84_vp_chan *priv = (void *)object;
	return nouveau_vp_context_fini(&priv->base, suspend);
}

static struct nouveau_oclass
nv84_vp_cclass = {
	.handle = NV_ENGCTX(VP, 0x84),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_vp_context_ctor,
		.dtor = nv84_vp_context_dtor,
		.init = nv84_vp_context_init,
		.fini = nv84_vp_context_fini,
		.rd32 = _nouveau_vp_context_rd32,
		.wr32 = _nouveau_vp_context_wr32,
	},
};

/*******************************************************************************
 * PVP engine/subdev functions
 ******************************************************************************/

static void
nv84_vp_intr(struct nouveau_subdev *subdev)
{
}

static int
nv84_vp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	     struct nouveau_oclass *oclass, void *data, u32 size,
	     struct nouveau_object **pobject)
{
	struct nv84_vp_priv *priv;
	int ret;

	ret = nouveau_vp_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x01020000;
	nv_subdev(priv)->intr = nv84_vp_intr;
	nv_engine(priv)->cclass = &nv84_vp_cclass;
	nv_engine(priv)->sclass = nv84_vp_sclass;
	return 0;
}

static void
nv84_vp_dtor(struct nouveau_object *object)
{
	struct nv84_vp_priv *priv = (void *)object;
	nouveau_vp_destroy(&priv->base);
}

static int
nv84_vp_init(struct nouveau_object *object)
{
	struct nv84_vp_priv *priv = (void *)object;
	int ret;

	ret = nouveau_vp_init(&priv->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv84_vp_fini(struct nouveau_object *object, bool suspend)
{
	struct nv84_vp_priv *priv = (void *)object;
	return nouveau_vp_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv84_vp_oclass = {
	.handle = NV_ENGINE(VP, 0x84),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_vp_ctor,
		.dtor = nv84_vp_dtor,
		.init = nv84_vp_init,
		.fini = nv84_vp_fini,
	},
};
