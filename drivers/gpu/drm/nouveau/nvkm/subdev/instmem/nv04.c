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

#include <core/ramht.h>

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/

static u32
nv04_instobj_rd32(struct nvkm_object *object, u64 addr)
{
	struct nv04_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct nv04_instobj_priv *node = (void *)object;
	return nv_ro32(priv, node->mem->offset + addr);
}

static void
nv04_instobj_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nv04_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct nv04_instobj_priv *node = (void *)object;
	nv_wo32(priv, node->mem->offset + addr, data);
}

static void
nv04_instobj_dtor(struct nvkm_object *object)
{
	struct nv04_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct nv04_instobj_priv *node = (void *)object;
	struct nvkm_subdev *subdev = (void *)priv;

	mutex_lock(&subdev->mutex);
	nvkm_mm_free(&priv->heap, &node->mem);
	mutex_unlock(&subdev->mutex);

	nvkm_instobj_destroy(&node->base);
}

static int
nv04_instobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nv04_instmem_priv *priv = (void *)nvkm_instmem(parent);
	struct nv04_instobj_priv *node;
	struct nvkm_instobj_args *args = data;
	struct nvkm_subdev *subdev = (void *)priv;
	int ret;

	if (!args->align)
		args->align = 1;

	ret = nvkm_instobj_create(parent, engine, oclass, &node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	mutex_lock(&subdev->mutex);
	ret = nvkm_mm_head(&priv->heap, 0, 1, args->size, args->size,
			   args->align, &node->mem);
	mutex_unlock(&subdev->mutex);
	if (ret)
		return ret;

	node->base.addr = node->mem->offset;
	node->base.size = node->mem->length;
	return 0;
}

struct nvkm_instobj_impl
nv04_instobj_oclass = {
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_instobj_ctor,
		.dtor = nv04_instobj_dtor,
		.init = _nvkm_instobj_init,
		.fini = _nvkm_instobj_fini,
		.rd32 = nv04_instobj_rd32,
		.wr32 = nv04_instobj_wr32,
	},
};

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static u32
nv04_instmem_rd32(struct nvkm_object *object, u64 addr)
{
	return nv_rd32(object, 0x700000 + addr);
}

static void
nv04_instmem_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	return nv_wr32(object, 0x700000 + addr, data);
}

void
nv04_instmem_dtor(struct nvkm_object *object)
{
	struct nv04_instmem_priv *priv = (void *)object;
	nvkm_gpuobj_ref(NULL, &priv->ramfc);
	nvkm_gpuobj_ref(NULL, &priv->ramro);
	nvkm_ramht_ref(NULL, &priv->ramht);
	nvkm_gpuobj_ref(NULL, &priv->vbios);
	nvkm_mm_fini(&priv->heap);
	if (priv->iomem)
		iounmap(priv->iomem);
	nvkm_instmem_destroy(&priv->base);
}

static int
nv04_instmem_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nv04_instmem_priv *priv;
	int ret;

	ret = nvkm_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	/* PRAMIN aperture maps over the end of VRAM, reserve it */
	priv->base.reserved = 512 * 1024;

	ret = nvkm_mm_init(&priv->heap, 0, priv->base.reserved, 1);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 0x10000, 0, 0,
			      &priv->vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nvkm_ramht_new(nv_object(priv), NULL, 0x08000, 0, &priv->ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18800: reserve for RAMFC (enough for 32 nv30 channels) */
	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 0x00800, 0,
			      NVOBJ_FLAG_ZERO_ALLOC, &priv->ramfc);
	if (ret)
		return ret;

	/* 0x18800-0x18a00: reserve for RAMRO */
	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 0x00200, 0, 0,
			      &priv->ramro);
	if (ret)
		return ret;

	return 0;
}

struct nvkm_oclass *
nv04_instmem_oclass = &(struct nvkm_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_instmem_ctor,
		.dtor = nv04_instmem_dtor,
		.init = _nvkm_instmem_init,
		.fini = _nvkm_instmem_fini,
		.rd32 = nv04_instmem_rd32,
		.wr32 = nv04_instmem_wr32,
	},
	.instobj = &nv04_instobj_oclass.base,
}.base;
