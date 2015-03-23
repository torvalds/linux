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
#include "priv.h"

#include <subdev/fb.h>

struct nv50_instmem_priv {
	struct nvkm_instmem base;
	spinlock_t lock;
	u64 addr;
};

struct nv50_instobj_priv {
	struct nvkm_instobj base;
	struct nvkm_mem *mem;
};

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/

static u32
nv50_instobj_rd32(struct nvkm_object *object, u64 offset)
{
	struct nv50_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct nv50_instobj_priv *node = (void *)object;
	unsigned long flags;
	u64 base = (node->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (node->mem->offset + offset) & 0x000000fffffULL;
	u32 data;

	spin_lock_irqsave(&priv->lock, flags);
	if (unlikely(priv->addr != base)) {
		nv_wr32(priv, 0x001700, base >> 16);
		priv->addr = base;
	}
	data = nv_rd32(priv, 0x700000 + addr);
	spin_unlock_irqrestore(&priv->lock, flags);
	return data;
}

static void
nv50_instobj_wr32(struct nvkm_object *object, u64 offset, u32 data)
{
	struct nv50_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct nv50_instobj_priv *node = (void *)object;
	unsigned long flags;
	u64 base = (node->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (node->mem->offset + offset) & 0x000000fffffULL;

	spin_lock_irqsave(&priv->lock, flags);
	if (unlikely(priv->addr != base)) {
		nv_wr32(priv, 0x001700, base >> 16);
		priv->addr = base;
	}
	nv_wr32(priv, 0x700000 + addr, data);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void
nv50_instobj_dtor(struct nvkm_object *object)
{
	struct nv50_instobj_priv *node = (void *)object;
	struct nvkm_fb *pfb = nvkm_fb(object);
	pfb->ram->put(pfb, &node->mem);
	nvkm_instobj_destroy(&node->base);
}

static int
nv50_instobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct nvkm_instobj_args *args = data;
	struct nv50_instobj_priv *node;
	int ret;

	args->size  = max((args->size  + 4095) & ~4095, (u32)4096);
	args->align = max((args->align + 4095) & ~4095, (u32)4096);

	ret = nvkm_instobj_create(parent, engine, oclass, &node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	ret = pfb->ram->get(pfb, args->size, args->align, 0, 0x800, &node->mem);
	if (ret)
		return ret;

	node->base.addr = node->mem->offset;
	node->base.size = node->mem->size << 12;
	node->mem->page_shift = 12;
	return 0;
}

static struct nvkm_instobj_impl
nv50_instobj_oclass = {
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_instobj_ctor,
		.dtor = nv50_instobj_dtor,
		.init = _nvkm_instobj_init,
		.fini = _nvkm_instobj_fini,
		.rd32 = nv50_instobj_rd32,
		.wr32 = nv50_instobj_wr32,
	},
};

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static int
nv50_instmem_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_instmem_priv *priv = (void *)object;
	priv->addr = ~0ULL;
	return nvkm_instmem_fini(&priv->base, suspend);
}

static int
nv50_instmem_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nv50_instmem_priv *priv;
	int ret;

	ret = nvkm_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	spin_lock_init(&priv->lock);
	return 0;
}

struct nvkm_oclass *
nv50_instmem_oclass = &(struct nvkm_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0x50),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_instmem_ctor,
		.dtor = _nvkm_instmem_dtor,
		.init = _nvkm_instmem_init,
		.fini = nv50_instmem_fini,
	},
	.instobj = &nv50_instobj_oclass.base,
}.base;
