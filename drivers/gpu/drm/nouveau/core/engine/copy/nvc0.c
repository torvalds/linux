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
#include <engine/copy.h>

#include <core/class.h>
#include <core/enum.h>
#include <core/class.h>
#include <core/enum.h>

#include "fuc/nvc0.fuc.h"

struct nvc0_copy_priv {
	struct nouveau_falcon base;
};

/*******************************************************************************
 * Copy object classes
 ******************************************************************************/

static struct nouveau_oclass
nvc0_copy0_sclass[] = {
	{ 0x90b5, &nouveau_object_ofuncs },
	{},
};

static struct nouveau_oclass
nvc0_copy1_sclass[] = {
	{ 0x90b8, &nouveau_object_ofuncs },
	{},
};

/*******************************************************************************
 * PCOPY context
 ******************************************************************************/

static struct nouveau_ofuncs
nvc0_copy_context_ofuncs = {
	.ctor = _nouveau_falcon_context_ctor,
	.dtor = _nouveau_falcon_context_dtor,
	.init = _nouveau_falcon_context_init,
	.fini = _nouveau_falcon_context_fini,
	.rd32 = _nouveau_falcon_context_rd32,
	.wr32 = _nouveau_falcon_context_wr32,
};

static struct nouveau_oclass
nvc0_copy0_cclass = {
	.handle = NV_ENGCTX(COPY0, 0xc0),
	.ofuncs = &nvc0_copy_context_ofuncs,
};

static struct nouveau_oclass
nvc0_copy1_cclass = {
	.handle = NV_ENGCTX(COPY1, 0xc0),
	.ofuncs = &nvc0_copy_context_ofuncs,
};

/*******************************************************************************
 * PCOPY engine/subdev functions
 ******************************************************************************/

static int
nvc0_copy_init(struct nouveau_object *object)
{
	struct nvc0_copy_priv *priv = (void *)object;
	int ret;

	ret = nouveau_falcon_init(&priv->base);
	if (ret)
		return ret;

	nv_wo32(priv, 0x084, nv_engidx(object) - NVDEV_ENGINE_COPY0);
	return 0;
}

static int
nvc0_copy0_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_copy_priv *priv;
	int ret;

	ret = nouveau_falcon_create(parent, engine, oclass, 0x104000, true,
				    "PCE0", "copy0", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000040;
	nv_subdev(priv)->intr = nva3_copy_intr;
	nv_engine(priv)->cclass = &nvc0_copy0_cclass;
	nv_engine(priv)->sclass = nvc0_copy0_sclass;
	nv_falcon(priv)->code.data = nvc0_pcopy_code;
	nv_falcon(priv)->code.size = sizeof(nvc0_pcopy_code);
	nv_falcon(priv)->data.data = nvc0_pcopy_data;
	nv_falcon(priv)->data.size = sizeof(nvc0_pcopy_data);
	return 0;
}

static int
nvc0_copy1_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvc0_copy_priv *priv;
	int ret;

	ret = nouveau_falcon_create(parent, engine, oclass, 0x105000, true,
				    "PCE1", "copy1", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00000080;
	nv_subdev(priv)->intr = nva3_copy_intr;
	nv_engine(priv)->cclass = &nvc0_copy1_cclass;
	nv_engine(priv)->sclass = nvc0_copy1_sclass;
	nv_falcon(priv)->code.data = nvc0_pcopy_code;
	nv_falcon(priv)->code.size = sizeof(nvc0_pcopy_code);
	nv_falcon(priv)->data.data = nvc0_pcopy_data;
	nv_falcon(priv)->data.size = sizeof(nvc0_pcopy_data);
	return 0;
}

struct nouveau_oclass
nvc0_copy0_oclass = {
	.handle = NV_ENGINE(COPY0, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_copy0_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = nvc0_copy_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};

struct nouveau_oclass
nvc0_copy1_oclass = {
	.handle = NV_ENGINE(COPY1, 0xc0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvc0_copy1_ctor,
		.dtor = _nouveau_falcon_dtor,
		.init = nvc0_copy_init,
		.fini = _nouveau_falcon_fini,
		.rd32 = _nouveau_falcon_rd32,
		.wr32 = _nouveau_falcon_wr32,
	},
};
