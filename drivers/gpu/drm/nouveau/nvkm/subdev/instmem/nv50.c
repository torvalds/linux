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
#define nv50_instmem(p) container_of((p), struct nv50_instmem, base)
#include "priv.h"

#include <core/memory.h>
#include <subdev/bar.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>

struct nv50_instmem {
	struct nvkm_instmem base;
	u64 addr;
};

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/
#define nv50_instobj(p) container_of((p), struct nv50_instobj, base.memory)

struct nv50_instobj {
	struct nvkm_instobj base;
	struct nv50_instmem *imem;
	struct nvkm_mem *mem;
	struct nvkm_vma bar;
	refcount_t maps;
	void *map;
};

static void
nv50_instobj_wr32_slow(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (iobj->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (iobj->mem->offset + offset) & 0x000000fffffULL;
	unsigned long flags;

	spin_lock_irqsave(&imem->base.lock, flags);
	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	nvkm_wr32(device, 0x700000 + addr, data);
	spin_unlock_irqrestore(&imem->base.lock, flags);
}

static u32
nv50_instobj_rd32_slow(struct nvkm_memory *memory, u64 offset)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (iobj->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (iobj->mem->offset + offset) & 0x000000fffffULL;
	u32 data;
	unsigned long flags;

	spin_lock_irqsave(&imem->base.lock, flags);
	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	data = nvkm_rd32(device, 0x700000 + addr);
	spin_unlock_irqrestore(&imem->base.lock, flags);
	return data;
}

static const struct nvkm_memory_ptrs
nv50_instobj_slow = {
	.rd32 = nv50_instobj_rd32_slow,
	.wr32 = nv50_instobj_wr32_slow,
};

static void
nv50_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	iowrite32_native(data, nv50_instobj(memory)->map + offset);
}

static u32
nv50_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	return ioread32_native(nv50_instobj(memory)->map + offset);
}

static const struct nvkm_memory_ptrs
nv50_instobj_fast = {
	.rd32 = nv50_instobj_rd32,
	.wr32 = nv50_instobj_wr32,
};

static void
nv50_instobj_kmap(struct nv50_instobj *iobj, struct nvkm_vmm *vmm)
{
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_memory *memory = &iobj->base.memory;
	struct nvkm_subdev *subdev = &imem->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_vma bar = {};
	u64 size = nvkm_memory_size(memory);
	int ret;

	/* Attempt to allocate BAR2 address-space and map the object
	 * into it.  The lock has to be dropped while doing this due
	 * to the possibility of recursion for page table allocation.
	 */
	mutex_unlock(&subdev->mutex);
	ret = nvkm_vm_get(vmm, size, 12, NV_MEM_ACCESS_RW, &bar);
	if (ret == 0)
		nvkm_memory_map(memory, &bar, 0);
	mutex_lock(&subdev->mutex);
	if (ret || iobj->bar.node) {
		/* We either failed, or another thread beat us. */
		mutex_unlock(&subdev->mutex);
		nvkm_vm_put(&bar);
		mutex_lock(&subdev->mutex);
		return;
	}

	/* Make the mapping visible to the host. */
	iobj->bar = bar;
	iobj->map = ioremap_wc(device->func->resource_addr(device, 3) +
			       (u32)iobj->bar.offset, size);
	if (!iobj->map) {
		nvkm_warn(subdev, "PRAMIN ioremap failed\n");
		nvkm_vm_put(&iobj->bar);
	}
}

static void
nv50_instobj_map(struct nvkm_memory *memory, struct nvkm_vma *vma, u64 offset)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	nvkm_vm_map_at(vma, offset, iobj->mem);
}

static void
nv50_instobj_release(struct nvkm_memory *memory)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_subdev *subdev = &imem->base.subdev;

	wmb();
	nvkm_bar_flush(subdev->device->bar);

	if (refcount_dec_and_mutex_lock(&iobj->maps, &subdev->mutex)) {
		/* Switch back to NULL accessors when last map is gone. */
		iobj->base.memory.ptrs = &nv50_instobj_slow;
		mutex_unlock(&subdev->mutex);
	}
}

static void __iomem *
nv50_instobj_acquire(struct nvkm_memory *memory)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nvkm_instmem *imem = &iobj->imem->base;
	struct nvkm_vmm *vmm;
	void __iomem *map = NULL;

	/* Already mapped? */
	if (refcount_inc_not_zero(&iobj->maps))
		return iobj->map;

	/* Take the lock, and re-check that another thread hasn't
	 * already mapped the object in the meantime.
	 */
	mutex_lock(&imem->subdev.mutex);
	if (refcount_inc_not_zero(&iobj->maps)) {
		mutex_unlock(&imem->subdev.mutex);
		return iobj->map;
	}

	/* Attempt to get a direct CPU mapping of the object. */
	if (!iobj->map && (vmm = nvkm_bar_bar2_vmm(imem->subdev.device)))
		nv50_instobj_kmap(iobj, vmm);
	map = iobj->map;

	if (!refcount_inc_not_zero(&iobj->maps)) {
		if (map)
			iobj->base.memory.ptrs = &nv50_instobj_fast;
		else
			iobj->base.memory.ptrs = &nv50_instobj_slow;
		refcount_inc(&iobj->maps);
	}

	mutex_unlock(&imem->subdev.mutex);
	return map;
}

static void
nv50_instobj_boot(struct nvkm_memory *memory, struct nvkm_vmm *vmm)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nvkm_instmem *imem = &iobj->imem->base;

	mutex_lock(&imem->subdev.mutex);
	nv50_instobj_kmap(iobj, vmm);
	mutex_unlock(&imem->subdev.mutex);
}

static u64
nv50_instobj_size(struct nvkm_memory *memory)
{
	return (u64)nv50_instobj(memory)->mem->size << NVKM_RAM_MM_SHIFT;
}

static u64
nv50_instobj_addr(struct nvkm_memory *memory)
{
	return nv50_instobj(memory)->mem->offset;
}

static enum nvkm_memory_target
nv50_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_VRAM;
}

static void *
nv50_instobj_dtor(struct nvkm_memory *memory)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nvkm_instmem *imem = &iobj->imem->base;
	struct nvkm_ram *ram = imem->subdev.device->fb->ram;
	if (iobj->map) {
		iounmap(iobj->map);
		nvkm_vm_put(&iobj->bar);
	}
	ram->func->put(ram, &iobj->mem);
	nvkm_instobj_dtor(imem, &iobj->base);
	return iobj;
}

static const struct nvkm_memory_func
nv50_instobj_func = {
	.dtor = nv50_instobj_dtor,
	.target = nv50_instobj_target,
	.size = nv50_instobj_size,
	.addr = nv50_instobj_addr,
	.boot = nv50_instobj_boot,
	.acquire = nv50_instobj_acquire,
	.release = nv50_instobj_release,
	.map = nv50_instobj_map,
};

static int
nv50_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		 struct nvkm_memory **pmemory)
{
	struct nv50_instmem *imem = nv50_instmem(base);
	struct nv50_instobj *iobj;
	struct nvkm_ram *ram = imem->base.subdev.device->fb->ram;
	int ret;

	if (!(iobj = kzalloc(sizeof(*iobj), GFP_KERNEL)))
		return -ENOMEM;
	*pmemory = &iobj->base.memory;

	nvkm_instobj_ctor(&nv50_instobj_func, &imem->base, &iobj->base);
	iobj->base.memory.ptrs = &nv50_instobj_slow;
	iobj->imem = imem;
	refcount_set(&iobj->maps, 0);

	size  = max((size  + 4095) & ~4095, (u32)4096);
	align = max((align + 4095) & ~4095, (u32)4096);

	ret = ram->func->get(ram, size, align, 0, 0x800, &iobj->mem);
	if (ret)
		return ret;

	iobj->mem->page_shift = 12;
	return 0;
}

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static void
nv50_instmem_fini(struct nvkm_instmem *base)
{
	nv50_instmem(base)->addr = ~0ULL;
}

static const struct nvkm_instmem_func
nv50_instmem = {
	.fini = nv50_instmem_fini,
	.memory_new = nv50_instobj_new,
	.persistent = true,
	.zero = false,
};

int
nv50_instmem_new(struct nvkm_device *device, int index,
		 struct nvkm_instmem **pimem)
{
	struct nv50_instmem *imem;

	if (!(imem = kzalloc(sizeof(*imem), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_instmem_ctor(&nv50_instmem, device, index, &imem->base);
	*pimem = &imem->base;
	return 0;
}
