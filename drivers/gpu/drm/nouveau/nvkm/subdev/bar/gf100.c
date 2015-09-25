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
#include "gf100.h"

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>

static struct nvkm_vm *
gf100_bar_kmap(struct nvkm_bar *base)
{
	return gf100_bar(base)->bar[0].vm;
}

int
gf100_bar_umap(struct nvkm_bar *base, u64 size, int type, struct nvkm_vma *vma)
{
	struct gf100_bar *bar = gf100_bar(base);
	return nvkm_vm_get(bar->bar[1].vm, size, type, NV_MEM_ACCESS_RW, vma);
}

static int
gf100_bar_ctor_vm(struct gf100_bar *bar, struct gf100_bar_vm *bar_vm,
		  struct lock_class_key *key, int bar_nr)
{
	struct nvkm_device *device = bar->base.subdev.device;
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

	bar_len = device->func->resource_size(device, bar_nr);

	ret = nvkm_vm_new(device, 0, bar_len, 0, key, &vm);
	if (ret)
		return ret;

	atomic_inc(&vm->engref[NVKM_SUBDEV_BAR]);

	/*
	 * Bootstrap page table lookup.
	 */
	if (bar_nr == 3) {
		ret = nvkm_vm_boot(vm, bar_len);
		if (ret) {
			nvkm_vm_ref(NULL, &vm, NULL);
			return ret;
		}
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
gf100_bar_oneinit(struct nvkm_bar *base)
{
	static struct lock_class_key bar1_lock;
	static struct lock_class_key bar3_lock;
	struct gf100_bar *bar = gf100_bar(base);
	int ret;

	/* BAR3 */
	if (bar->base.func->kmap) {
		ret = gf100_bar_ctor_vm(bar, &bar->bar[0], &bar3_lock, 3);
		if (ret)
			return ret;
	}

	/* BAR1 */
	ret = gf100_bar_ctor_vm(bar, &bar->bar[1], &bar1_lock, 1);
	if (ret)
		return ret;

	return 0;
}

int
gf100_bar_init(struct nvkm_bar *base)
{
	struct gf100_bar *bar = gf100_bar(base);
	struct nvkm_device *device = bar->base.subdev.device;
	u32 addr;

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

void *
gf100_bar_dtor(struct nvkm_bar *base)
{
	struct gf100_bar *bar = gf100_bar(base);

	nvkm_vm_ref(NULL, &bar->bar[1].vm, bar->bar[1].pgd);
	nvkm_gpuobj_del(&bar->bar[1].pgd);
	nvkm_memory_del(&bar->bar[1].mem);

	if (bar->bar[0].vm) {
		nvkm_memory_del(&bar->bar[0].vm->pgt[0].mem[0]);
		nvkm_vm_ref(NULL, &bar->bar[0].vm, bar->bar[0].pgd);
	}
	nvkm_gpuobj_del(&bar->bar[0].pgd);
	nvkm_memory_del(&bar->bar[0].mem);
	return bar;
}

int
gf100_bar_new_(const struct nvkm_bar_func *func, struct nvkm_device *device,
	       int index, struct nvkm_bar **pbar)
{
	struct gf100_bar *bar;
	if (!(bar = kzalloc(sizeof(*bar), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_bar_ctor(func, device, index, &bar->base);
	*pbar = &bar->base;
	return 0;
}

static const struct nvkm_bar_func
gf100_bar_func = {
	.dtor = gf100_bar_dtor,
	.oneinit = gf100_bar_oneinit,
	.init = gf100_bar_init,
	.kmap = gf100_bar_kmap,
	.umap = gf100_bar_umap,
	.flush = g84_bar_flush,
};

int
gf100_bar_new(struct nvkm_device *device, int index, struct nvkm_bar **pbar)
{
	return gf100_bar_new_(&gf100_bar_func, device, index, pbar);
}
