/*
 * Copyright (c) 2015, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <subdev/fb.h>
#include <core/mm.h>
#include <core/device.h>

#include "priv.h"

struct gk20a_instobj_priv {
	struct nvkm_instobj base;
	/* Must be second member here - see nouveau_gpuobj_map_vm() */
	struct nvkm_mem *mem;
	/* Pointed by mem */
	struct nvkm_mem _mem;
	void *cpuaddr;
	dma_addr_t handle;
	struct nvkm_mm_node r;
};

struct gk20a_instmem_priv {
	struct nvkm_instmem base;
	spinlock_t lock;
	u64 addr;
};

static u32
gk20a_instobj_rd32(struct nvkm_object *object, u64 offset)
{
	struct gk20a_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct gk20a_instobj_priv *node = (void *)object;
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
gk20a_instobj_wr32(struct nvkm_object *object, u64 offset, u32 data)
{
	struct gk20a_instmem_priv *priv = (void *)nvkm_instmem(object);
	struct gk20a_instobj_priv *node = (void *)object;
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
gk20a_instobj_dtor(struct nvkm_object *object)
{
	struct gk20a_instobj_priv *node = (void *)object;
	struct gk20a_instmem_priv *priv = (void *)nvkm_instmem(node);
	struct device *dev = nv_device_base(nv_device(priv));

	if (unlikely(!node->handle))
		return;

	dma_free_coherent(dev, node->mem->size << PAGE_SHIFT, node->cpuaddr,
			  node->handle);

	nvkm_instobj_destroy(&node->base);
}

static int
gk20a_instobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, void *data, u32 _size,
		   struct nvkm_object **pobject)
{
	struct nvkm_instobj_args *args = data;
	struct gk20a_instmem_priv *priv = (void *)nvkm_instmem(parent);
	struct device *dev = nv_device_base(nv_device(priv));
	struct gk20a_instobj_priv *node;
	u32 size, align;
	u32 npages;
	int ret;

	nv_debug(parent, "%s: size: %x align: %x\n", __func__,
		 args->size, args->align);

	size  = max((args->size  + 4095) & ~4095, (u32)4096);
	align = max((args->align + 4095) & ~4095, (u32)4096);

	npages = size >> PAGE_SHIFT;

	ret = nvkm_instobj_create_(parent, engine, oclass, sizeof(*node),
				      (void **)&node);
	*pobject = nv_object(node);
	if (ret)
		return ret;

	node->mem = &node->_mem;

	node->cpuaddr = dma_alloc_coherent(dev, npages << PAGE_SHIFT,
					   &node->handle, GFP_KERNEL);
	if (!node->cpuaddr) {
		nv_error(priv, "cannot allocate DMA memory\n");
		return -ENOMEM;
	}

	/* alignment check */
	if (unlikely(node->handle & (align - 1)))
		nv_warn(priv, "memory not aligned as requested: %pad (0x%x)\n",
			&node->handle, align);

	node->mem->offset = node->handle;
	node->mem->size = size >> 12;
	node->mem->memtype = 0;
	node->mem->page_shift = 12;
	INIT_LIST_HEAD(&node->mem->regions);

	node->r.type = 12;
	node->r.offset = node->handle >> 12;
	node->r.length = npages;
	list_add_tail(&node->r.rl_entry, &node->mem->regions);

	node->base.addr = node->mem->offset;
	node->base.size = size;

	nv_debug(parent, "alloc size: 0x%x, align: 0x%x, gaddr: 0x%llx\n",
		 size, align, node->mem->offset);

	return 0;
}

static struct nvkm_instobj_impl
gk20a_instobj_oclass = {
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk20a_instobj_ctor,
		.dtor = gk20a_instobj_dtor,
		.init = _nvkm_instobj_init,
		.fini = _nvkm_instobj_fini,
		.rd32 = gk20a_instobj_rd32,
		.wr32 = gk20a_instobj_wr32,
	},
};



static int
gk20a_instmem_fini(struct nvkm_object *object, bool suspend)
{
	struct gk20a_instmem_priv *priv = (void *)object;
	priv->addr = ~0ULL;
	return nvkm_instmem_fini(&priv->base, suspend);
}

static int
gk20a_instmem_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, void *data, u32 size,
		   struct nvkm_object **pobject)
{
	struct gk20a_instmem_priv *priv;
	int ret;

	ret = nvkm_instmem_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	spin_lock_init(&priv->lock);

	return 0;
}

struct nvkm_oclass *
gk20a_instmem_oclass = &(struct nvkm_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0xea),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gk20a_instmem_ctor,
		.dtor = _nvkm_instmem_dtor,
		.init = _nvkm_instmem_init,
		.fini = gk20a_instmem_fini,
	},
	.instobj = &gk20a_instobj_oclass.base,
}.base;
