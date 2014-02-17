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

#include <subdev/timer.h>
#include <subdev/fb.h>
#include <subdev/vm.h>

#include "priv.h"

struct nv50_bar_priv {
	struct nouveau_bar base;
	spinlock_t lock;
	struct nouveau_gpuobj *mem;
	struct nouveau_gpuobj *pad;
	struct nouveau_gpuobj *pgd;
	struct nouveau_vm *bar1_vm;
	struct nouveau_gpuobj *bar1;
	struct nouveau_vm *bar3_vm;
	struct nouveau_gpuobj *bar3;
};

static int
nv50_bar_kmap(struct nouveau_bar *bar, struct nouveau_mem *mem,
	      u32 flags, struct nouveau_vma *vma)
{
	struct nv50_bar_priv *priv = (void *)bar;
	int ret;

	ret = nouveau_vm_get(priv->bar3_vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nouveau_vm_map(vma, mem);
	return 0;
}

static int
nv50_bar_umap(struct nouveau_bar *bar, struct nouveau_mem *mem,
	      u32 flags, struct nouveau_vma *vma)
{
	struct nv50_bar_priv *priv = (void *)bar;
	int ret;

	ret = nouveau_vm_get(priv->bar1_vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nouveau_vm_map(vma, mem);
	return 0;
}

static void
nv50_bar_unmap(struct nouveau_bar *bar, struct nouveau_vma *vma)
{
	nouveau_vm_unmap(vma);
	nouveau_vm_put(vma);
}

static void
nv50_bar_flush(struct nouveau_bar *bar)
{
	struct nv50_bar_priv *priv = (void *)bar;
	unsigned long flags;
	spin_lock_irqsave(&priv->lock, flags);
	nv_wr32(priv, 0x00330c, 0x00000001);
	if (!nv_wait(priv, 0x00330c, 0x00000002, 0x00000000))
		nv_warn(priv, "flush timeout\n");
	spin_unlock_irqrestore(&priv->lock, flags);
}

void
nv84_bar_flush(struct nouveau_bar *bar)
{
	struct nv50_bar_priv *priv = (void *)bar;
	unsigned long flags;
	spin_lock_irqsave(&priv->lock, flags);
	nv_wr32(bar, 0x070000, 0x00000001);
	if (!nv_wait(priv, 0x070000, 0x00000002, 0x00000000))
		nv_warn(priv, "flush timeout\n");
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int
nv50_bar_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_object *heap;
	struct nouveau_vm *vm;
	struct nv50_bar_priv *priv;
	u64 start, limit;
	int ret;

	ret = nouveau_bar_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 0x20000, 0,
				 NVOBJ_FLAG_HEAP, &priv->mem);
	heap = nv_object(priv->mem);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), heap,
				(device->chipset == 0x50) ? 0x1400 : 0x0200,
				 0, 0, &priv->pad);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), heap, 0x4000, 0,
				 0, &priv->pgd);
	if (ret)
		return ret;

	/* BAR3 */
	start = 0x0100000000ULL;
	limit = start + nv_device_resource_len(device, 3);

	ret = nouveau_vm_new(device, start, limit, start, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nouveau_gpuobj_new(nv_object(priv), heap,
				 ((limit-- - start) >> 12) * 8, 0x1000,
				 NVOBJ_FLAG_ZERO_ALLOC, &vm->pgt[0].obj[0]);
	vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	ret = nouveau_vm_ref(vm, &priv->bar3_vm, priv->pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), heap, 24, 16, 0, &priv->bar3);
	if (ret)
		return ret;

	nv_wo32(priv->bar3, 0x00, 0x7fc00000);
	nv_wo32(priv->bar3, 0x04, lower_32_bits(limit));
	nv_wo32(priv->bar3, 0x08, lower_32_bits(start));
	nv_wo32(priv->bar3, 0x0c, upper_32_bits(limit) << 24 |
				  upper_32_bits(start));
	nv_wo32(priv->bar3, 0x10, 0x00000000);
	nv_wo32(priv->bar3, 0x14, 0x00000000);

	/* BAR1 */
	start = 0x0000000000ULL;
	limit = start + nv_device_resource_len(device, 1);

	ret = nouveau_vm_new(device, start, limit--, start, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nouveau_vm_ref(vm, &priv->bar1_vm, priv->pgd);
	nouveau_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), heap, 24, 16, 0, &priv->bar1);
	if (ret)
		return ret;

	nv_wo32(priv->bar1, 0x00, 0x7fc00000);
	nv_wo32(priv->bar1, 0x04, lower_32_bits(limit));
	nv_wo32(priv->bar1, 0x08, lower_32_bits(start));
	nv_wo32(priv->bar1, 0x0c, upper_32_bits(limit) << 24 |
				  upper_32_bits(start));
	nv_wo32(priv->bar1, 0x10, 0x00000000);
	nv_wo32(priv->bar1, 0x14, 0x00000000);

	priv->base.alloc = nouveau_bar_alloc;
	priv->base.kmap = nv50_bar_kmap;
	priv->base.umap = nv50_bar_umap;
	priv->base.unmap = nv50_bar_unmap;
	if (device->chipset == 0x50)
		priv->base.flush = nv50_bar_flush;
	else
		priv->base.flush = nv84_bar_flush;
	spin_lock_init(&priv->lock);
	return 0;
}

static void
nv50_bar_dtor(struct nouveau_object *object)
{
	struct nv50_bar_priv *priv = (void *)object;
	nouveau_gpuobj_ref(NULL, &priv->bar1);
	nouveau_vm_ref(NULL, &priv->bar1_vm, priv->pgd);
	nouveau_gpuobj_ref(NULL, &priv->bar3);
	if (priv->bar3_vm) {
		nouveau_gpuobj_ref(NULL, &priv->bar3_vm->pgt[0].obj[0]);
		nouveau_vm_ref(NULL, &priv->bar3_vm, priv->pgd);
	}
	nouveau_gpuobj_ref(NULL, &priv->pgd);
	nouveau_gpuobj_ref(NULL, &priv->pad);
	nouveau_gpuobj_ref(NULL, &priv->mem);
	nouveau_bar_destroy(&priv->base);
}

static int
nv50_bar_init(struct nouveau_object *object)
{
	struct nv50_bar_priv *priv = (void *)object;
	int ret;

	ret = nouveau_bar_init(&priv->base);
	if (ret)
		return ret;

	nv_mask(priv, 0x000200, 0x00000100, 0x00000000);
	nv_mask(priv, 0x000200, 0x00000100, 0x00000100);
	nv_wr32(priv, 0x100c80, 0x00060001);
	if (!nv_wait(priv, 0x100c80, 0x00000001, 0x00000000)) {
		nv_error(priv, "vm flush timeout\n");
		return -EBUSY;
	}

	nv_wr32(priv, 0x001704, 0x00000000 | priv->mem->addr >> 12);
	nv_wr32(priv, 0x001704, 0x40000000 | priv->mem->addr >> 12);
	nv_wr32(priv, 0x001708, 0x80000000 | priv->bar1->node->offset >> 4);
	nv_wr32(priv, 0x00170c, 0x80000000 | priv->bar3->node->offset >> 4);
	return 0;
}

static int
nv50_bar_fini(struct nouveau_object *object, bool suspend)
{
	struct nv50_bar_priv *priv = (void *)object;
	return nouveau_bar_fini(&priv->base, suspend);
}

struct nouveau_oclass
nv50_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_bar_ctor,
		.dtor = nv50_bar_dtor,
		.init = nv50_bar_init,
		.fini = nv50_bar_fini,
	},
};
