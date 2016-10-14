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

#include <core/memory.h>
#include <subdev/bar.h>

/******************************************************************************
 * instmem object base implementation
 *****************************************************************************/
#define nvkm_instobj(p) container_of((p), struct nvkm_instobj, memory)

struct nvkm_instobj {
	struct nvkm_memory memory;
	struct nvkm_memory *parent;
	struct nvkm_instmem *imem;
	struct list_head head;
	u32 *suspend;
	void __iomem *map;
};

static enum nvkm_memory_target
nvkm_instobj_target(struct nvkm_memory *memory)
{
	memory = nvkm_instobj(memory)->parent;
	return nvkm_memory_target(memory);
}

static u64
nvkm_instobj_addr(struct nvkm_memory *memory)
{
	memory = nvkm_instobj(memory)->parent;
	return nvkm_memory_addr(memory);
}

static u64
nvkm_instobj_size(struct nvkm_memory *memory)
{
	memory = nvkm_instobj(memory)->parent;
	return nvkm_memory_size(memory);
}

static void
nvkm_instobj_release(struct nvkm_memory *memory)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	nvkm_bar_flush(iobj->imem->subdev.device->bar);
}

static void __iomem *
nvkm_instobj_acquire(struct nvkm_memory *memory)
{
	return nvkm_instobj(memory)->map;
}

static u32
nvkm_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	return ioread32_native(nvkm_instobj(memory)->map + offset);
}

static void
nvkm_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	iowrite32_native(data, nvkm_instobj(memory)->map + offset);
}

static void
nvkm_instobj_map(struct nvkm_memory *memory, struct nvkm_vma *vma, u64 offset)
{
	memory = nvkm_instobj(memory)->parent;
	nvkm_memory_map(memory, vma, offset);
}

static void *
nvkm_instobj_dtor(struct nvkm_memory *memory)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	spin_lock(&iobj->imem->lock);
	list_del(&iobj->head);
	spin_unlock(&iobj->imem->lock);
	nvkm_memory_del(&iobj->parent);
	return iobj;
}

const struct nvkm_memory_func
nvkm_instobj_func = {
	.dtor = nvkm_instobj_dtor,
	.target = nvkm_instobj_target,
	.addr = nvkm_instobj_addr,
	.size = nvkm_instobj_size,
	.acquire = nvkm_instobj_acquire,
	.release = nvkm_instobj_release,
	.rd32 = nvkm_instobj_rd32,
	.wr32 = nvkm_instobj_wr32,
	.map = nvkm_instobj_map,
};

static void
nvkm_instobj_boot(struct nvkm_memory *memory, struct nvkm_vm *vm)
{
	memory = nvkm_instobj(memory)->parent;
	nvkm_memory_boot(memory, vm);
}

static void
nvkm_instobj_release_slow(struct nvkm_memory *memory)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	nvkm_instobj_release(memory);
	nvkm_done(iobj->parent);
}

static void __iomem *
nvkm_instobj_acquire_slow(struct nvkm_memory *memory)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	iobj->map = nvkm_kmap(iobj->parent);
	if (iobj->map)
		memory->func = &nvkm_instobj_func;
	return iobj->map;
}

static u32
nvkm_instobj_rd32_slow(struct nvkm_memory *memory, u64 offset)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	return nvkm_ro32(iobj->parent, offset);
}

static void
nvkm_instobj_wr32_slow(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nvkm_instobj *iobj = nvkm_instobj(memory);
	return nvkm_wo32(iobj->parent, offset, data);
}

const struct nvkm_memory_func
nvkm_instobj_func_slow = {
	.dtor = nvkm_instobj_dtor,
	.target = nvkm_instobj_target,
	.addr = nvkm_instobj_addr,
	.size = nvkm_instobj_size,
	.boot = nvkm_instobj_boot,
	.acquire = nvkm_instobj_acquire_slow,
	.release = nvkm_instobj_release_slow,
	.rd32 = nvkm_instobj_rd32_slow,
	.wr32 = nvkm_instobj_wr32_slow,
	.map = nvkm_instobj_map,
};

int
nvkm_instobj_new(struct nvkm_instmem *imem, u32 size, u32 align, bool zero,
		 struct nvkm_memory **pmemory)
{
	struct nvkm_memory *memory = NULL;
	struct nvkm_instobj *iobj;
	u32 offset;
	int ret;

	ret = imem->func->memory_new(imem, size, align, zero, &memory);
	if (ret)
		goto done;

	if (!imem->func->persistent) {
		if (!(iobj = kzalloc(sizeof(*iobj), GFP_KERNEL))) {
			ret = -ENOMEM;
			goto done;
		}

		nvkm_memory_ctor(&nvkm_instobj_func_slow, &iobj->memory);
		iobj->parent = memory;
		iobj->imem = imem;
		spin_lock(&iobj->imem->lock);
		list_add_tail(&iobj->head, &imem->list);
		spin_unlock(&iobj->imem->lock);
		memory = &iobj->memory;
	}

	if (!imem->func->zero && zero) {
		void __iomem *map = nvkm_kmap(memory);
		if (unlikely(!map)) {
			for (offset = 0; offset < size; offset += 4)
				nvkm_wo32(memory, offset, 0x00000000);
		} else {
			memset_io(map, 0x00, size);
		}
		nvkm_done(memory);
	}

done:
	if (ret)
		nvkm_memory_del(&memory);
	*pmemory = memory;
	return ret;
}

/******************************************************************************
 * instmem subdev base implementation
 *****************************************************************************/

u32
nvkm_instmem_rd32(struct nvkm_instmem *imem, u32 addr)
{
	return imem->func->rd32(imem, addr);
}

void
nvkm_instmem_wr32(struct nvkm_instmem *imem, u32 addr, u32 data)
{
	return imem->func->wr32(imem, addr, data);
}

static int
nvkm_instmem_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_instmem *imem = nvkm_instmem(subdev);
	struct nvkm_instobj *iobj;
	int i;

	if (imem->func->fini)
		imem->func->fini(imem);

	if (suspend) {
		list_for_each_entry(iobj, &imem->list, head) {
			struct nvkm_memory *memory = iobj->parent;
			u64 size = nvkm_memory_size(memory);

			iobj->suspend = vmalloc(size);
			if (!iobj->suspend)
				return -ENOMEM;

			for (i = 0; i < size; i += 4)
				iobj->suspend[i / 4] = nvkm_ro32(memory, i);
		}
	}

	return 0;
}

static int
nvkm_instmem_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_instmem *imem = nvkm_instmem(subdev);
	if (imem->func->oneinit)
		return imem->func->oneinit(imem);
	return 0;
}

static int
nvkm_instmem_init(struct nvkm_subdev *subdev)
{
	struct nvkm_instmem *imem = nvkm_instmem(subdev);
	struct nvkm_instobj *iobj;
	int i;

	list_for_each_entry(iobj, &imem->list, head) {
		if (iobj->suspend) {
			struct nvkm_memory *memory = iobj->parent;
			u64 size = nvkm_memory_size(memory);
			for (i = 0; i < size; i += 4)
				nvkm_wo32(memory, i, iobj->suspend[i / 4]);
			vfree(iobj->suspend);
			iobj->suspend = NULL;
		}
	}

	return 0;
}

static void *
nvkm_instmem_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_instmem *imem = nvkm_instmem(subdev);
	if (imem->func->dtor)
		return imem->func->dtor(imem);
	return imem;
}

static const struct nvkm_subdev_func
nvkm_instmem = {
	.dtor = nvkm_instmem_dtor,
	.oneinit = nvkm_instmem_oneinit,
	.init = nvkm_instmem_init,
	.fini = nvkm_instmem_fini,
};

void
nvkm_instmem_ctor(const struct nvkm_instmem_func *func,
		  struct nvkm_device *device, int index,
		  struct nvkm_instmem *imem)
{
	nvkm_subdev_ctor(&nvkm_instmem, device, index, &imem->subdev);
	imem->func = func;
	spin_lock_init(&imem->lock);
	INIT_LIST_HEAD(&imem->list);
}
