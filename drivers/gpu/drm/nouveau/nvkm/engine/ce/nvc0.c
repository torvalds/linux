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

#include <engine/falcon.h>
#include <engine/fifo.h>
#include <engine/ce.h>

#include <core/enum.h>
#include <core/enum.h>

#include "fuc/nvc0.fuc3.h"

struct nvc0_ce_priv {
	struct nouveau_falcon base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nouveau_oclass
nvc0_ce0_sclass[] = {
	{ 0x90b5, &nouveau_object_ofuncs },
	{},
};

static struct nouveau_oclass
nvc0_ce1_sclass[] = {
	{ 0x90b8, &nouveau_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCE context
 ******************************************************************************/

static struct nouveau_ofuncs
nvc0_ce_context_ofuncs = {
	.ctor = _nouveau_falcon_context_ctor,
	.dtor = _nouveau_falcon_context_dtor,
	.init = _nouveau_falcon_context_init,
	.fini = _nouveau_falcon_context_fini,
	.rd32 = _nouveau_falcon_context_rd32,
	.wr32 = _nouveau_falcon_context_wr32,
};

static struct nouveau_oclass
nvc0_ce0_cclass = {
	.handle = NV_ENGCTX(CE0, 0xc0),
	.ofuncs = &nvc0_ce_context_ofuncs,
};

static struct nouveau_oclass
nvc0_ce1_cclass = {
	.handle = NV_ENGCTX(CE1, 0xc0),
	.ofuncs = &nvc0_ce_context_ofuncs,
};

/*******************************************************************************
 * PCE engine/subdev functions
 ******************************************************************************/

static int
nvc0_ce_init(struct nouveau_object *object)
{
	struct nvc0_ce_priv *priv = (void *)object;
	int ret;

	ret = nouveau_falcon_init(&priv->base);
	if (ret)
		return ret;

	nv_wo32(priv, 0x084, nv_engidx(&priv->base.base) - NVDEV_ENGINE_CE0);
	return 0;
}

static int
nvc0_ce0_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_ce_priv *priv;
	int ret;

	ret = nouveau_falcon_create(parent, engine, oclass, 0x104000, true,
				    "PCE0", "ce0", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000040;
	nv_subdev(priv)->intr = nva3_ce_intr;
	nv_engine(priv)->cclass = &nvc0_ce0_cclass;
	nv_engine(priv)->sclass = nvc0_ce0_sclass;
	nv_falcon(priv)->code.data = nvc0_pce_code;
	nv_falcon(priv)->code.size = sizeof(nvc0_pce_code);
	nv_falcon(priv)->data.data = nvc0_pce_data;
	nv_falcon(priv)->data.size = sizeof(nvc0_pce_data);
	return 0;
}

static int
nvc0_ce1_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_ce_priv *priv;
	int ret;

	ret = nouveau_falcon_create(parent, engine, oclass, 0x105000, true,
				    "PCE1", "ce1", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000080;
	nv_subdev(priv)->intr = nva3_ce_intr;
	nv_engine(priv)->cclass = &nvc0_ce1_cclass;
	nv_engine(priv)->sclass = nvc0_ce1_sclass;
	nv_falcon(priv)->code.data = nvc0_pce_code;
	nv_falcon(priv)->code.size = sizeof(nvc0_pce_code);
	nv_falcon(priv)->data.data = nvc0_pce_data;
	nv_falcon(priv)->data.size = sizeof(nvc0_pce_data);
	return 0;
}

struct nouveau_oclass
nvc0_ce0_oclass = {
	.handle = NV_ENGINE(CE0, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_ce0_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = nvc0_ce_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};

struct nouveau_oclass
nvc0_ce1_oclass = {
	.handle = NV_ENGINE(CE1, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_ce1_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = nvc0_ce_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};
