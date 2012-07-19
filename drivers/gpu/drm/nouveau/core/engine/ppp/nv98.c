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

#include <engine/ppp.h>

struct nv98_ppp_priv {
	struct nouveau_ppp base;
};

struct nv98_ppp_chan {
	struct nouveau_ppp_chan base;
};

/*******************************************************************************
 * PPP object classes
 ******************************************************************************/

static struct nouveau_oclass
nv98_ppp_sclass[] = {
	{},
};

/*******************************************************************************
 * PPPP context
 ******************************************************************************/

static int
nv98_ppp_context_ctor(struct nouveau_object *parent,
		      struct nouveau_object *engine,
		      struct nouveau_oclass *oclass, void *data, u32 size,
		      struct nouveau_object **pobject)
{
	struct nv98_ppp_chan *priv;
	int ret;

	ret = nouveau_ppp_context_create(parent, engine, oclass, NULL,
					 0, 0, 0, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

static void
nv98_ppp_context_dtor(struct nouveau_object *object)
{
	struct nv98_ppp_chan *priv = (void *)object;
	nouveau_ppp_context_destroy(&priv->base);
}

static int
nv98_ppp_context_init(struct nouveau_object *object)
{
	struct nv98_ppp_chan *priv = (void *)object;
	int ret;

	ret = nouveau_ppp_context_init(&priv->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv98_ppp_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv98_ppp_chan *priv = (void *)object;
	return nouveau_ppp_context_fini(&priv->base, suspend);
}

static struct nouveau_oclass
nv98_ppp_cclass = {
	.handle = NV_ENGCTX(PPP, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_ppp_context_ctor,
		.dtor = nv98_ppp_context_dtor,
		.init = nv98_ppp_context_init,
		.fini = nv98_ppp_context_fini,
		.rd32 = _nouveau_ppp_context_rd32,
		.wr32 = _nouveau_ppp_context_wr32,
	},
};

/*******************************************************************************
 * PPPP engine/subdev functions
 ******************************************************************************/

static void
nv98_ppp_intr(struct nouveau_subdev *subdev)
{
}

static int
nv98_ppp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nv98_ppp_priv *priv;
	int ret;

	ret = nouveau_ppp_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00400002;
	nv_subdev(priv)->intr = nv98_ppp_intr;
	nv_engine(priv)->cclass = &nv98_ppp_cclass;
	nv_engine(priv)->sclass = nv98_ppp_sclass;
	return 0;
}

static void
nv98_ppp_dtor(struct nouveau_object *object)
{
	struct nv98_ppp_priv *priv = (void *)object;
	nouveau_ppp_destroy(&priv->base);
}

static int
nv98_ppp_init(struct nouveau_object *object)
{
	struct nv98_ppp_priv *priv = (void *)object;
	int ret;

	ret = nouveau_ppp_init(&priv->base);
	if (ret)
		return ret;

	return 0;
}

static int
nv98_ppp_fini(struct nouveau_object *object, bool suspend)
{
	struct nv98_ppp_priv *priv = (void *)object;
	return nouveau_ppp_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv98_ppp_oclass = {
	.handle = NV_ENGINE(PPP, 0x98),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv98_ppp_ctor,
		.dtor = nv98_ppp_dtor,
		.init = nv98_ppp_init,
		.fini = nv98_ppp_fini,
	},
};
