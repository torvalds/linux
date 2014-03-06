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

#include "nv04.h"

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/

static u32
nv04_instobj_rd32(struct nouveau_object *object, u64 addr)
{
	struct nv04_instobj_priv *node = (void *)object;
	return nv_ro32(object->engine, node->mem->offset + addr);
}

static void
nv04_instobj_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	struct nv04_instobj_priv *node = (void *)object;
	nv_wo32(object->engine, node->mem->offset + addr, data);
}

static void
nv04_instobj_dtor(struct nouveau_object *object)
{
	struct nv04_instmem_priv *priv = (void *)object->engine;
	struct nv04_instobj_priv *node = (void *)object;
	nouveau_mm_free(&priv->heap, &node->mem);
	nouveau_instobj_destroy(&node->base);
}

static int
nv04_instobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv04_instmem_priv *priv = (void *)engine;
	struct nv04_instobj_priv *node;
	struct nouveau_instobj_args *args = data;
	int ret;

	if (!args->align)
		args->align = 1;

	ret = nouveau_instobj_create(parent, engine, oclass, &node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	ret = nouveau_mm_head(&priv->heap, 1, args->size, args->size,
			      args->align, &node->mem);
	if (ret)
		return ret;

	node->base.addr = node->mem->offset;
	node->base.size = node->mem->length;
	return 0;
}

struct nouveau_instobj_impl
nv04_instobj_oclass = {
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_instobj_ctor,
		.dtor = nv04_instobj_dtor,
		.init = _nouveau_instobj_init,
		.fini = _nouveau_instobj_fini,
		.rd32 = nv04_instobj_rd32,
		.wr32 = nv04_instobj_wr32,
	},
};

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static u32
nv04_instmem_rd32(struct nouveau_object *object, u64 addr)
{
	return nv_rd32(object, 0x700000 + addr);
}

static void
nv04_instmem_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	return nv_wr32(object, 0x700000 + addr, data);
}

void
nv04_instmem_dtor(struct nouveau_object *object)
{
	struct nv04_instmem_priv *priv = (void *)object;
	nouveau_gpuobj_ref(NULL, &priv->ramfc);
	nouveau_gpuobj_ref(NULL, &priv->ramro);
	nouveau_ramht_ref(NULL, &priv->ramht);
	nouveau_gpuobj_ref(NULL, &priv->vbios);
	nouveau_mm_fini(&priv->heap);
	if (priv->iomem)
		iounmap(priv->iomem);
	nouveau_instmem_destroy(&priv->base);
}

static int
nv04_instmem_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv04_instmem_priv *priv;
	int ret;

	ret = nouveau_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* PRAMIN aperture maps over the end of VRAM, reserve it */
	priv->base.reserved = 512 * 1024;

	ret = nouveau_mm_init(&priv->heap, 0, priv->base.reserved, 1);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x10000, 0, 0,
				&priv->vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nouveau_ramht_new(nv_object(priv), NULL, 0x08000, 0, &priv->ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18800: reserve for RAMFC (enough for 32 nv30 channels) */
	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x00800, 0,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->ramfc);
	if (ret)
		return ret;

	/* 0x18800-0x18a00: reserve for RAMRO */
	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x00200, 0, 0,
				&priv->ramro);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass *
nv04_instmem_oclass = &(struct nouveau_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0x04),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_instmem_ctor,
		.dtor = nv04_instmem_dtor,
		.init = _nouveau_instmem_init,
		.fini = _nouveau_instmem_fini,
		.rd32 = nv04_instmem_rd32,
		.wr32 = nv04_instmem_wr32,
	},
	.instobj = &nv04_instobj_oclass.base,
}.base;
