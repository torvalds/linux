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

#include <subdev/instmem.h>
#include <subdev/fb.h>

#include <core/mm.h>

struct nv50_instmem_priv {
	struct nouveau_instmem base;
	spinlock_t lock;
	u64 addr;
};

struct nv50_instobj_priv {
	struct nouveau_instobj base;
	struct nouveau_mem *mem;
};

static int
nv50_instobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nouveau_fb *pfb = nouveau_fb(parent);
	struct nv50_instobj_priv *node;
	u32 align = (unsigned long)data;
	int ret;

	size  = max((size  + 4095) & ~4095, (u32)4096);
	align = max((align + 4095) & ~4095, (u32)4096);

	ret = nouveau_instobj_create(parent, engine, oclass, &node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	ret = pfb->ram.get(pfb, size, align, 0, 0x800, &node->mem);
	if (ret)
		return ret;

	node->base.addr = node->mem->offset;
	node->base.size = node->mem->size << 12;
	node->mem->page_shift = 12;
	return 0;
}

static void
nv50_instobj_dtor(struct nouveau_object *object)
{
	struct nv50_instobj_priv *node = (void *)object;
	struct nouveau_fb *pfb = nouveau_fb(object);
	pfb->ram.put(pfb, &node->mem);
	nouveau_instobj_destroy(&node->base);
}

static u32
nv50_instobj_rd32(struct nouveau_object *object, u32 offset)
{
	struct nv50_instmem_priv *priv = (void *)object->engine;
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
nv50_instobj_wr32(struct nouveau_object *object, u32 offset, u32 data)
{
	struct nv50_instmem_priv *priv = (void *)object->engine;
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

static struct nouveau_oclass
nv50_instobj_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_instobj_ctor,
		.dtor = nv50_instobj_dtor,
		.init = _nouveau_instobj_init,
		.fini = _nouveau_instobj_fini,
		.rd32 = nv50_instobj_rd32,
		.wr32 = nv50_instobj_wr32,
	},
};

static int
nv50_instmem_alloc(struct nouveau_instmem *imem, struct nouveau_object *parent,
		   u32 size, u32 align, struct nouveau_object **pobject)
{
	struct nouveau_object *engine = nv_object(imem);
	return nouveau_object_ctor(parent, engine, &nv50_instobj_oclass,
				   (void *)(unsigned long)align, size, pobject);
}

static int
nv50_instmem_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 size,
		  struct nouveau_object **pobject)
{
	struct nv50_instmem_priv *priv;
	int ret;

	ret = nouveau_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	spin_lock_init(&priv->lock);
	priv->base.alloc = nv50_instmem_alloc;
	return 0;
}

static int
nv50_instmem_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_instmem_priv *priv = (void *)object;
	priv->addr = ~0ULL;
	return nouveau_instmem_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv50_instmem_oclass = {
	.handle = NV_SUBDEV(INSTMEM, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_instmem_ctor,
		.dtor = _nouveau_instmem_dtor,
		.init = _nouveau_instmem_init,
		.fini = nv50_instmem_fini,
	},
};
