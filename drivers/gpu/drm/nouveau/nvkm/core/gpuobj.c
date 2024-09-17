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
#include <core/engine.h>

#include <subdev/instmem.h>
#include <subdev/bar.h>
#include <subdev/mmu.h>

/* fast-path, where backend is able to provide direct pointer to memory */
static u32
nvkm_gpuobj_rd32_fast(struct nvkm_gpuobj *gpuobj, u32 offset)
{
	return ioread32_native(gpuobj->map + offset);
}

static void
nvkm_gpuobj_wr32_fast(struct nvkm_gpuobj *gpuobj, u32 offset, u32 data)
{
	iowrite32_native(data, gpuobj->map + offset);
}

/* accessor functions for gpuobjs allocated directly from instmem */
static int
nvkm_gpuobj_heap_map(struct nvkm_gpuobj *gpuobj, u64 offset,
		     struct nvkm_vmm *vmm, struct nvkm_vma *vma,
		     void *argv, u32 argc)
{
	return nvkm_memory_map(gpuobj->memory, offset, vmm, vma, argv, argc);
}

static u32
nvkm_gpuobj_heap_rd32(struct nvkm_gpuobj *gpuobj, u32 offset)
{
	return nvkm_ro32(gpuobj->memory, offset);
}

static void
nvkm_gpuobj_heap_wr32(struct nvkm_gpuobj *gpuobj, u32 offset, u32 data)
{
	nvkm_wo32(gpuobj->memory, offset, data);
}

static const struct nvkm_gpuobj_func nvkm_gpuobj_heap;
static void
nvkm_gpuobj_heap_release(struct nvkm_gpuobj *gpuobj)
{
	gpuobj->func = &nvkm_gpuobj_heap;
	nvkm_done(gpuobj->memory);
}

static const struct nvkm_gpuobj_func
nvkm_gpuobj_heap_fast = {
	.release = nvkm_gpuobj_heap_release,
	.rd32 = nvkm_gpuobj_rd32_fast,
	.wr32 = nvkm_gpuobj_wr32_fast,
	.map = nvkm_gpuobj_heap_map,
};

static const struct nvkm_gpuobj_func
nvkm_gpuobj_heap_slow = {
	.release = nvkm_gpuobj_heap_release,
	.rd32 = nvkm_gpuobj_heap_rd32,
	.wr32 = nvkm_gpuobj_heap_wr32,
	.map = nvkm_gpuobj_heap_map,
};

static void *
nvkm_gpuobj_heap_acquire(struct nvkm_gpuobj *gpuobj)
{
	gpuobj->map = nvkm_kmap(gpuobj->memory);
	if (likely(gpuobj->map))
		gpuobj->func = &nvkm_gpuobj_heap_fast;
	else
		gpuobj->func = &nvkm_gpuobj_heap_slow;
	return gpuobj->map;
}

static const struct nvkm_gpuobj_func
nvkm_gpuobj_heap = {
	.acquire = nvkm_gpuobj_heap_acquire,
	.map = nvkm_gpuobj_heap_map,
};

/* accessor functions for gpuobjs sub-allocated from a parent gpuobj */
static int
nvkm_gpuobj_map(struct nvkm_gpuobj *gpuobj, u64 offset,
		struct nvkm_vmm *vmm, struct nvkm_vma *vma,
		void *argv, u32 argc)
{
	return nvkm_memory_map(gpuobj->parent, gpuobj->node->offset + offset,
			       vmm, vma, argv, argc);
}

static u32
nvkm_gpuobj_rd32(struct nvkm_gpuobj *gpuobj, u32 offset)
{
	return nvkm_ro32(gpuobj->parent, gpuobj->node->offset + offset);
}

static void
nvkm_gpuobj_wr32(struct nvkm_gpuobj *gpuobj, u32 offset, u32 data)
{
	nvkm_wo32(gpuobj->parent, gpuobj->node->offset + offset, data);
}

static const struct nvkm_gpuobj_func nvkm_gpuobj_func;
static void
nvkm_gpuobj_release(struct nvkm_gpuobj *gpuobj)
{
	gpuobj->func = &nvkm_gpuobj_func;
	nvkm_done(gpuobj->parent);
}

static const struct nvkm_gpuobj_func
nvkm_gpuobj_fast = {
	.release = nvkm_gpuobj_release,
	.rd32 = nvkm_gpuobj_rd32_fast,
	.wr32 = nvkm_gpuobj_wr32_fast,
	.map = nvkm_gpuobj_map,
};

static const struct nvkm_gpuobj_func
nvkm_gpuobj_slow = {
	.release = nvkm_gpuobj_release,
	.rd32 = nvkm_gpuobj_rd32,
	.wr32 = nvkm_gpuobj_wr32,
	.map = nvkm_gpuobj_map,
};

static void *
nvkm_gpuobj_acquire(struct nvkm_gpuobj *gpuobj)
{
	gpuobj->map = nvkm_kmap(gpuobj->parent);
	if (likely(gpuobj->map)) {
		gpuobj->map  = (u8 *)gpuobj->map + gpuobj->node->offset;
		gpuobj->func = &nvkm_gpuobj_fast;
	} else {
		gpuobj->func = &nvkm_gpuobj_slow;
	}
	return gpuobj->map;
}

static const struct nvkm_gpuobj_func
nvkm_gpuobj_func = {
	.acquire = nvkm_gpuobj_acquire,
	.map = nvkm_gpuobj_map,
};

static int
nvkm_gpuobj_ctor(struct nvkm_device *device, u32 size, int align, bool zero,
		 struct nvkm_gpuobj *parent, struct nvkm_gpuobj *gpuobj)
{
	u32 offset;
	int ret;

	if (parent) {
		if (align >= 0) {
			ret = nvkm_mm_head(&parent->heap, 0, 1, size, size,
					   max(align, 1), &gpuobj->node);
		} else {
			ret = nvkm_mm_tail(&parent->heap, 0, 1, size, size,
					   -align, &gpuobj->node);
		}
		if (ret)
			return ret;

		gpuobj->parent = parent;
		gpuobj->func = &nvkm_gpuobj_func;
		gpuobj->addr = parent->addr + gpuobj->node->offset;
		gpuobj->size = gpuobj->node->length;

		if (zero) {
			nvkm_kmap(gpuobj);
			for (offset = 0; offset < gpuobj->size; offset += 4)
				nvkm_wo32(gpuobj, offset, 0x00000000);
			nvkm_done(gpuobj);
		}
	} else {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, size,
				      abs(align), zero, &gpuobj->memory);
		if (ret)
			return ret;

		gpuobj->func = &nvkm_gpuobj_heap;
		gpuobj->addr = nvkm_memory_addr(gpuobj->memory);
		gpuobj->size = nvkm_memory_size(gpuobj->memory);
	}

	return nvkm_mm_init(&gpuobj->heap, 0, 0, gpuobj->size, 1);
}

void
nvkm_gpuobj_del(struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_gpuobj *gpuobj = *pgpuobj;
	if (gpuobj) {
		if (gpuobj->parent)
			nvkm_mm_free(&gpuobj->parent->heap, &gpuobj->node);
		nvkm_mm_fini(&gpuobj->heap);
		nvkm_memory_unref(&gpuobj->memory);
		kfree(*pgpuobj);
		*pgpuobj = NULL;
	}
}

int
nvkm_gpuobj_new(struct nvkm_device *device, u32 size, int align, bool zero,
		struct nvkm_gpuobj *parent, struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_gpuobj *gpuobj;
	int ret;

	if (!(gpuobj = *pgpuobj = kzalloc(sizeof(*gpuobj), GFP_KERNEL)))
		return -ENOMEM;

	ret = nvkm_gpuobj_ctor(device, size, align, zero, parent, gpuobj);
	if (ret)
		nvkm_gpuobj_del(pgpuobj);
	return ret;
}

/* the below is basically only here to support sharing the paged dma object
 * for PCI(E)GART on <=nv4x chipsets, and should *not* be expected to work
 * anywhere else.
 */

int
nvkm_gpuobj_wrap(struct nvkm_memory *memory, struct nvkm_gpuobj **pgpuobj)
{
	if (!(*pgpuobj = kzalloc(sizeof(**pgpuobj), GFP_KERNEL)))
		return -ENOMEM;

	(*pgpuobj)->addr = nvkm_memory_addr(memory);
	(*pgpuobj)->size = nvkm_memory_size(memory);
	return 0;
}

void
nvkm_gpuobj_memcpy_to(struct nvkm_gpuobj *dst, u32 dstoffset, void *src,
		      u32 length)
{
	int i;

	for (i = 0; i < length; i += 4)
		nvkm_wo32(dst, dstoffset + i, *(u32 *)(src + i));
}

void
nvkm_gpuobj_memcpy_from(void *dst, struct nvkm_gpuobj *src, u32 srcoffset,
			u32 length)
{
	int i;

	for (i = 0; i < length; i += 4)
		((u32 *)src)[i / 4] = nvkm_ro32(src, srcoffset + i);
}
