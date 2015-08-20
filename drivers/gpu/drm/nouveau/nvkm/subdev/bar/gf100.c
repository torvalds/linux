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

struct gf100_bar_vm {
	struct nvkm_memory *mem;
	struct nvkm_gpuobj *pgd;
	struct nvkm_vm *vm;
};

struct gf100_bar {
	struct nvkm_bar base;
	spinlock_t lock;
	struct gf100_bar_vm bar[2];
};

static struct nvkm_vm *
gf100_bar_kmap(struct nvkm_bar *obj)
{
	struct gf100_bar *bar = container_of(obj, typeof(*bar), base);
	return bar->bar[0].vm;
}

static int
gf100_bar_umap(struct nvkm_bar *obj, u64 size, int type, struct nvkm_vma *vma)
{
	struct gf100_bar *bar = container_of(obj, typeof(*bar), base);
	return nvkm_vm_get(bar->bar[1].vm, size, type, NV_MEM_ACCESS_RW, vma);
}

static void
gf100_bar_unmap(struct nvkm_bar *bar, struct nvkm_vma *vma)
{
	nvkm_vm_unmap(vma);
	nvkm_vm_put(vma);
}


static int
gf100_bar_ctor_vm(struct gf100_bar *bar, struct gf100_bar_vm *bar_vm,
		  struct lock_class_key *key, int bar_nr)
{
	struct nvkm_device *device = nv_device(&bar->base);
	struct nvkm_vm *vm;
	resource_size_t bar_len;
	int ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x1000, 0, false,
			      &bar_vm->mem);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x8000, 0, false, NULL, &bar_vm->pgd);
	if (ret)
		return ret;

	bar_len = nv_device_resource_len(device, bar_nr);

	ret = nvkm_vm_new(device, 0, bar_len, 0, key, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVDEV_SUBDEV_BAR]);

	/*
	 * Bootstrap page table lookup.
	 */
	if (bar_nr == 3) {
		ret = nvkm_vm_boot(vm, bar_len);
		if (ret)
			return ret;
	}

	ret = nvkm_vm_ref(vm, &bar_vm->vm, bar_vm->pgd);
	nvkm_vm_ref(NULL, &vm, NULL);
	if (ret)
		return ret;

	nvkm_kmap(bar_vm->mem);
	nvkm_wo32(bar_vm->mem, 0x0200, lower_32_bits(bar_vm->pgd->addr));
	nvkm_wo32(bar_vm->mem, 0x0204, upper_32_bits(bar_vm->pgd->addr));
	nvkm_wo32(bar_vm->mem, 0x0208, lower_32_bits(bar_len - 1));
	nvkm_wo32(bar_vm->mem, 0x020c, upper_32_bits(bar_len - 1));
	nvkm_done(bar_vm->mem);
	return 0;
}

int
gf100_bar_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	static struct lock_class_key bar1_lock;
	static struct lock_class_key bar3_lock;
	struct nvkm_device *device = nv_device(parent);
	struct gf100_bar *bar;
	bool has_bar3 = nv_device_resource_len(device, 3) != 0;
	int ret;

	ret = nvkm_bar_create(parent, engine, oclass, &bar);
	*pobject = nv_object(bar);
	if (ret)
		return ret;

	device->bar = &bar->base;
	bar->base.flush = g84_bar_flush;
	spin_lock_init(&bar->lock);

	/* BAR3 */
	if (has_bar3) {
		ret = gf100_bar_ctor_vm(bar, &bar->bar[0], &bar3_lock, 3);
		if (ret)
			return ret;
	}

	/* BAR1 */
	ret = gf100_bar_ctor_vm(bar, &bar->bar[1], &bar1_lock, 1);
	if (ret)
		return ret;

	if (has_bar3)
		bar->base.kmap = gf100_bar_kmap;
	bar->base.umap = gf100_bar_umap;
	bar->base.unmap = gf100_bar_unmap;
	return 0;
}

void
gf100_bar_dtor(struct nvkm_object *object)
{
	struct gf100_bar *bar = (void *)object;

	nvkm_vm_ref(NULL, &bar->bar[1].vm, bar->bar[1].pgd);
	nvkm_gpuobj_del(&bar->bar[1].pgd);
	nvkm_memory_del(&bar->bar[1].mem);

	if (bar->bar[0].vm) {
		nvkm_memory_del(&bar->bar[0].vm->pgt[0].mem[0]);
		nvkm_vm_ref(NULL, &bar->bar[0].vm, bar->bar[0].pgd);
	}
	nvkm_gpuobj_del(&bar->bar[0].pgd);
	nvkm_memory_del(&bar->bar[0].mem);

	nvkm_bar_destroy(&bar->base);
}

int
gf100_bar_init(struct nvkm_object *object)
{
	struct gf100_bar *bar = (void *)object;
	struct nvkm_device *device = bar->base.subdev.device;
	u32 addr;
	int ret;

	ret = nvkm_bar_init(&bar->base);
	if (ret)
		return ret;

	nvkm_mask(device, 0x000200, 0x00000100, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00000100, 0x00000100);

	addr = nvkm_memory_addr(bar->bar[1].mem) >> 12;
	nvkm_wr32(device, 0x001704, 0x80000000 | addr);

	if (bar->bar[0].mem) {
		addr = nvkm_memory_addr(bar->bar[0].mem) >> 12;
		nvkm_wr32(device, 0x001714, 0xc0000000 | addr);
	}

	return 0;
}

struct nvkm_oclass
gf100_bar_oclass = {
	.handle = NV_SUBDEV(BAR, 0xc0),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gf100_bar_ctor,
		.dtor = gf100_bar_dtor,
		.init = gf100_bar_init,
		.fini = _nvkm_bar_fini,
	},
};
