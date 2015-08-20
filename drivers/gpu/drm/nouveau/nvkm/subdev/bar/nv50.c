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

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

struct nv50_bar {
	struct nvkm_bar base;
	spinlock_t lock;
	struct nvkm_gpuobj *mem;
	struct nvkm_gpuobj *pad;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *bar1_vm;
	struct nvkm_gpuobj *bar1;
	struct nvkm_vm *bar3_vm;
	struct nvkm_gpuobj *bar3;
};

static int
nv50_bar_kmap(struct nvkm_bar *obj, struct nvkm_mem *mem, u32 flags,
	      struct nvkm_vma *vma)
{
	struct nv50_bar *bar = container_of(obj, typeof(*bar), base);
	int ret;

	ret = nvkm_vm_get(bar->bar3_vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nvkm_vm_map(vma, mem);
	return 0;
}

static int
nv50_bar_umap(struct nvkm_bar *obj, struct nvkm_mem *mem, u32 flags,
	      struct nvkm_vma *vma)
{
	struct nv50_bar *bar = container_of(obj, typeof(*bar), base);
	int ret;

	ret = nvkm_vm_get(bar->bar1_vm, mem->size << 12, 12, flags, vma);
	if (ret)
		return ret;

	nvkm_vm_map(vma, mem);
	return 0;
}

static void
nv50_bar_unmap(struct nvkm_bar *bar, struct nvkm_vma *vma)
{
	nvkm_vm_unmap(vma);
	nvkm_vm_put(vma);
}

static void
nv50_bar_flush(struct nvkm_bar *obj)
{
	struct nv50_bar *bar = container_of(obj, typeof(*bar), base);
	struct nvkm_device *device = bar->base.subdev.device;
	unsigned long flags;
	spin_lock_irqsave(&bar->lock, flags);
	nvkm_wr32(device, 0x00330c, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x00330c) & 0x00000002))
			break;
	);
	spin_unlock_irqrestore(&bar->lock, flags);
}

void
g84_bar_flush(struct nvkm_bar *obj)
{
	struct nv50_bar *bar = container_of(obj, typeof(*bar), base);
	struct nvkm_device *device = bar->base.subdev.device;
	unsigned long flags;
	spin_lock_irqsave(&bar->lock, flags);
	nvkm_wr32(device, 0x070000, 0x00000001);
	nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x070000) & 0x00000002))
			break;
	);
	spin_unlock_irqrestore(&bar->lock, flags);
}

static int
nv50_bar_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_object *heap;
	struct nvkm_vm *vm;
	struct nv50_bar *bar;
	u64 start, limit;
	int ret;

	ret = nvkm_bar_create(parent, engine, oclass, &bar);
	*pobject = nv_object(bar);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(bar), NULL, 0x20000, 0,
			      NVOBJ_FLAG_HEAP, &bar->mem);
	heap = nv_object(bar->mem);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(bar), heap,
			      (device->chipset == 0x50) ? 0x1400 : 0x0200,
			      0, 0, &bar->pad);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(bar), heap, 0x4000, 0, 0, &bar->pgd);
	if (ret)
		return ret;

	/* BAR3 */
	start = 0x0100000000ULL;
	limit = start + nv_device_resource_len(device, 3);

	ret = nvkm_vm_new(device, start, limit, start, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nvkm_gpuobj_new(nv_object(bar), heap,
			      ((limit-- - start) >> 12) * 8, 0x1000,
			      NVOBJ_FLAG_ZERO_ALLOC, &vm->pgt[0].obj[0]);
	vm->pgt[0].refcount[0] = 1;
	if (ret)
		return ret;

	ret = nvkm_vm_ref(vm, &bar->bar3_vm, bar->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(bar), heap, 24, 16, 0, &bar->bar3);
	if (ret)
		return ret;

	nvkm_kmap(bar->bar3);
	nvkm_wo32(bar->bar3, 0x00, 0x7fc00000);
	nvkm_wo32(bar->bar3, 0x04, lower_32_bits(limit));
	nvkm_wo32(bar->bar3, 0x08, lower_32_bits(start));
	nvkm_wo32(bar->bar3, 0x0c, upper_32_bits(limit) << 24 |
				   upper_32_bits(start));
	nvkm_wo32(bar->bar3, 0x10, 0x00000000);
	nvkm_wo32(bar->bar3, 0x14, 0x00000000);
	nvkm_done(bar->bar3);

	/* BAR1 */
	start = 0x0000000000ULL;
	limit = start + nv_device_resource_len(device, 1);

	ret = nvkm_vm_new(device, start, limit--, start, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	ret = nvkm_vm_ref(vm, &bar->bar1_vm, bar->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(bar), heap, 24, 16, 0, &bar->bar1);
	if (ret)
		return ret;

	nvkm_kmap(bar->bar1);
	nvkm_wo32(bar->bar1, 0x00, 0x7fc00000);
	nvkm_wo32(bar->bar1, 0x04, lower_32_bits(limit));
	nvkm_wo32(bar->bar1, 0x08, lower_32_bits(start));
	nvkm_wo32(bar->bar1, 0x0c, upper_32_bits(limit) << 24 |
				   upper_32_bits(start));
	nvkm_wo32(bar->bar1, 0x10, 0x00000000);
	nvkm_wo32(bar->bar1, 0x14, 0x00000000);
	nvkm_done(bar->bar1);

	bar->base.alloc = nvkm_bar_alloc;
	bar->base.kmap = nv50_bar_kmap;
	bar->base.umap = nv50_bar_umap;
	bar->base.unmap = nv50_bar_unmap;
	if (device->chipset == 0x50)
		bar->base.flush = nv50_bar_flush;
	else
		bar->base.flush = g84_bar_flush;
	spin_lock_init(&bar->lock);
	return 0;
}

static void
nv50_bar_dtor(struct nvkm_object *object)
{
	struct nv50_bar *bar = (void *)object;
	nvkm_gpuobj_ref(NULL, &bar->bar1);
	nvkm_vm_ref(NULL, &bar->bar1_vm, bar->pgd);
	nvkm_gpuobj_ref(NULL, &bar->bar3);
	if (bar->bar3_vm) {
		nvkm_gpuobj_ref(NULL, &bar->bar3_vm->pgt[0].obj[0]);
		nvkm_vm_ref(NULL, &bar->bar3_vm, bar->pgd);
	}
	nvkm_gpuobj_ref(NULL, &bar->pgd);
	nvkm_gpuobj_ref(NULL, &bar->pad);
	nvkm_gpuobj_ref(NULL, &bar->mem);
	nvkm_bar_destroy(&bar->base);
}

static int
nv50_bar_init(struct nvkm_object *object)
{
	struct nv50_bar *bar = (void *)object;
	struct nvkm_device *device = bar->base.subdev.device;
	int ret, i;

	ret = nvkm_bar_init(&bar->base);
	if (ret)
		return ret;

	nvkm_mask(device, 0x000200, 0x00000100, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00000100, 0x00000100);
	nvkm_wr32(device, 0x100c80, 0x00060001);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x100c80) & 0x00000001))
			break;
	) < 0)
		return -EBUSY;

	nvkm_wr32(device, 0x001704, 0x00000000 | bar->mem->addr >> 12);
	nvkm_wr32(device, 0x001704, 0x40000000 | bar->mem->addr >> 12);
	nvkm_wr32(device, 0x001708, 0x80000000 | bar->bar1->node->offset >> 4);
	nvkm_wr32(device, 0x00170c, 0x80000000 | bar->bar3->node->offset >> 4);
	for (i = 0; i < 8; i++)
		nvkm_wr32(device, 0x001900 + (i * 4), 0x00000000);
	return 0;
}

static int
nv50_bar_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_bar *bar = (void *)object;
	return nvkm_bar_fini(&bar->base, suspend);
}

struct nvkm_oclass
nv50_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_bar_ctor,
		.dtor = nv50_bar_dtor,
		.init = nv50_bar_init,
		.fini = nv50_bar_fini,
	},
};
