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
#define nv04_instmem(p) container_of((p), struct nv04_instmem, base)
#include "priv.h"

#include <core/memory.h>
#include <core/ramht.h>

struct nv04_instmem {
	struct nvkm_instmem base;
	struct nvkm_mm heap;
};

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/
#define nv04_instobj(p) container_of((p), struct nv04_instobj, memory)

struct nv04_instobj {
	struct nvkm_memory memory;
	struct nv04_instmem *imem;
	struct nvkm_mm_node *node;
};

static enum nvkm_memory_target
nv04_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_INST;
}

static u64
nv04_instobj_addr(struct nvkm_memory *memory)
{
	return nv04_instobj(memory)->node->offset;
}

static u64
nv04_instobj_size(struct nvkm_memory *memory)
{
	return nv04_instobj(memory)->node->length;
}

static void __iomem *
nv04_instobj_acquire(struct nvkm_memory *memory)
{
	struct nv04_instobj *iobj = nv04_instobj(memory);
	struct nvkm_device *device = iobj->imem->base.subdev.device;
	return device->pri + 0x700000 + iobj->node->offset;
}

static void
nv04_instobj_release(struct nvkm_memory *memory)
{
}

static u32
nv04_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct nv04_instobj *iobj = nv04_instobj(memory);
	struct nvkm_device *device = iobj->imem->base.subdev.device;
	return nvkm_rd32(device, 0x700000 + iobj->node->offset + offset);
}

static void
nv04_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nv04_instobj *iobj = nv04_instobj(memory);
	struct nvkm_device *device = iobj->imem->base.subdev.device;
	nvkm_wr32(device, 0x700000 + iobj->node->offset + offset, data);
}

static void *
nv04_instobj_dtor(struct nvkm_memory *memory)
{
	struct nv04_instobj *iobj = nv04_instobj(memory);
	mutex_lock(&iobj->imem->base.subdev.mutex);
	nvkm_mm_free(&iobj->imem->heap, &iobj->node);
	mutex_unlock(&iobj->imem->base.subdev.mutex);
	return iobj;
}

static const struct nvkm_memory_func
nv04_instobj_func = {
	.dtor = nv04_instobj_dtor,
	.target = nv04_instobj_target,
	.size = nv04_instobj_size,
	.addr = nv04_instobj_addr,
	.acquire = nv04_instobj_acquire,
	.release = nv04_instobj_release,
	.rd32 = nv04_instobj_rd32,
	.wr32 = nv04_instobj_wr32,
};

static int
nv04_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		 struct nvkm_memory **pmemory)
{
	struct nv04_instmem *imem = nv04_instmem(base);
	struct nv04_instobj *iobj;
	int ret;

	if (!(iobj = kzalloc(sizeof(*iobj), GFP_KERNEL)))
		return -ENOMEM;
	*pmemory = &iobj->memory;

	nvkm_memory_ctor(&nv04_instobj_func, &iobj->memory);
	iobj->imem = imem;

	mutex_lock(&imem->base.subdev.mutex);
	ret = nvkm_mm_head(&imem->heap, 0, 1, size, size,
			   align ? align : 1, &iobj->node);
	mutex_unlock(&imem->base.subdev.mutex);
	return ret;
}

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static u32
nv04_instmem_rd32(struct nvkm_instmem *imem, u32 addr)
{
	return nvkm_rd32(imem->subdev.device, 0x700000 + addr);
}

static void
nv04_instmem_wr32(struct nvkm_instmem *imem, u32 addr, u32 data)
{
	nvkm_wr32(imem->subdev.device, 0x700000 + addr, data);
}

static int
nv04_instmem_oneinit(struct nvkm_instmem *base)
{
	struct nv04_instmem *imem = nv04_instmem(base);
	struct nvkm_device *device = imem->base.subdev.device;
	int ret;

	/* PRAMIN aperture maps over the end of VRAM, reserve it */
	imem->base.reserved = 512 * 1024;

	ret = nvkm_mm_init(&imem->heap, 0, imem->base.reserved, 1);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x10000, 0, false,
			      &imem->base.vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nvkm_ramht_new(device, 0x08000, 0, NULL, &imem->base.ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18800: reserve for RAMFC (enough for 32 nv30 channels) */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x00800, 0, true,
			      &imem->base.ramfc);
	if (ret)
		return ret;

	/* 0x18800-0x18a00: reserve for RAMRO */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x00200, 0, false,
			      &imem->base.ramro);
	if (ret)
		return ret;

	return 0;
}

static void *
nv04_instmem_dtor(struct nvkm_instmem *base)
{
	struct nv04_instmem *imem = nv04_instmem(base);
	nvkm_memory_del(&imem->base.ramfc);
	nvkm_memory_del(&imem->base.ramro);
	nvkm_ramht_del(&imem->base.ramht);
	nvkm_memory_del(&imem->base.vbios);
	nvkm_mm_fini(&imem->heap);
	return imem;
}

static const struct nvkm_instmem_func
nv04_instmem = {
	.dtor = nv04_instmem_dtor,
	.oneinit = nv04_instmem_oneinit,
	.rd32 = nv04_instmem_rd32,
	.wr32 = nv04_instmem_wr32,
	.memory_new = nv04_instobj_new,
	.persistent = false,
	.zero = false,
};

int
nv04_instmem_new(struct nvkm_device *device, int index,
		 struct nvkm_instmem **pimem)
{
	struct nv04_instmem *imem;

	if (!(imem = kzalloc(sizeof(*imem), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_instmem_ctor(&nv04_instmem, device, index, &imem->base);
	*pimem = &imem->base;
	return 0;
}
