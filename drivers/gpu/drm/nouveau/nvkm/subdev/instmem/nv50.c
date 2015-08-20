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
	unsigned long lock_flags;
	spinlock_t lock;
	u64 addr;
};

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/
#define nv50_instobj(p) container_of((p), struct nv50_instobj, memory)

struct nv50_instobj {
	struct nvkm_memory memory;
	struct nv50_instmem *imem;
	struct nvkm_mem *mem;
	struct nvkm_vma bar;
	void *map;
};

static enum nvkm_memory_target
nv50_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_VRAM;
}

static u64
nv50_instobj_addr(struct nvkm_memory *memory)
{
	return nv50_instobj(memory)->mem->offset;
}

static u64
nv50_instobj_size(struct nvkm_memory *memory)
{
	return (u64)nv50_instobj(memory)->mem->size << NVKM_RAM_MM_SHIFT;
}

static void
nv50_instobj_boot(struct nvkm_memory *memory, struct nvkm_vm *vm)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nvkm_subdev *subdev = &iobj->imem->base.subdev;
	struct nvkm_device *device = subdev->device;
	u64 size = nvkm_memory_size(memory);
	void __iomem *map;
	int ret;

	iobj->map = ERR_PTR(-ENOMEM);

	ret = nvkm_vm_get(vm, size, 12, NV_MEM_ACCESS_RW, &iobj->bar);
	if (ret == 0) {
		map = ioremap(device->func->resource_addr(device, 3) +
			      (u32)iobj->bar.offset, size);
		if (map) {
			nvkm_memory_map(memory, &iobj->bar, 0);
			iobj->map = map;
		} else {
			nvkm_warn(subdev, "PRAMIN ioremap failed\n");
			nvkm_vm_put(&iobj->bar);
		}
	} else {
		nvkm_warn(subdev, "PRAMIN exhausted\n");
	}
}

static void
nv50_instobj_release(struct nvkm_memory *memory)
{
	struct nv50_instmem *imem = nv50_instobj(memory)->imem;
	spin_unlock_irqrestore(&imem->lock, imem->lock_flags);
}

static void __iomem *
nv50_instobj_acquire(struct nvkm_memory *memory)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_bar *bar = imem->base.subdev.device->bar;
	struct nvkm_vm *vm;
	unsigned long flags;

	if (!iobj->map && (vm = nvkm_bar_kmap(bar)))
		nvkm_memory_boot(memory, vm);
	if (!IS_ERR_OR_NULL(iobj->map))
		return iobj->map;

	spin_lock_irqsave(&imem->lock, flags);
	imem->lock_flags = flags;
	return NULL;
}

static u32
nv50_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (iobj->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (iobj->mem->offset + offset) & 0x000000fffffULL;
	u32 data;

	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	data = nvkm_rd32(device, 0x700000 + addr);
	return data;
}

static void
nv50_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nv50_instmem *imem = iobj->imem;
	struct nvkm_device *device = imem->base.subdev.device;
	u64 base = (iobj->mem->offset + offset) & 0xffffff00000ULL;
	u64 addr = (iobj->mem->offset + offset) & 0x000000fffffULL;

	if (unlikely(imem->addr != base)) {
		nvkm_wr32(device, 0x001700, base >> 16);
		imem->addr = base;
	}
	nvkm_wr32(device, 0x700000 + addr, data);
}

static void
nv50_instobj_map(struct nvkm_memory *memory, struct nvkm_vma *vma, u64 offset)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	nvkm_vm_map_at(vma, offset, iobj->mem);
}

static void *
nv50_instobj_dtor(struct nvkm_memory *memory)
{
	struct nv50_instobj *iobj = nv50_instobj(memory);
	struct nvkm_ram *ram = iobj->imem->base.subdev.device->fb->ram;
	if (!IS_ERR_OR_NULL(iobj->map)) {
		nvkm_vm_put(&iobj->bar);
		iounmap(iobj->map);
	}
	ram->func->put(ram, &iobj->mem);
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
	.rd32 = nv50_instobj_rd32,
	.wr32 = nv50_instobj_wr32,
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
	*pmemory = &iobj->memory;

	nvkm_memory_ctor(&nv50_instobj_func, &iobj->memory);
	iobj->imem = imem;

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
	.persistent = false,
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
	spin_lock_init(&imem->lock);
	*pimem = &imem->base;
	return 0;
}
