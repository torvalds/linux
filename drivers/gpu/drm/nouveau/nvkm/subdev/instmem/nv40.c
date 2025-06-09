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
#define nv40_instmem(p) container_of((p), struct nv40_instmem, base)
#include "priv.h"

#include <core/ramht.h>
#include <engine/gr/nv40.h>

struct nv40_instmem {
	struct nvkm_instmem base;
	struct nvkm_mm heap;
	void __iomem *iomem;
};

/******************************************************************************
 * instmem object implementation
 *****************************************************************************/
#define nv40_instobj(p) container_of((p), struct nv40_instobj, base.memory)

struct nv40_instobj {
	struct nvkm_instobj base;
	struct nv40_instmem *imem;
	struct nvkm_mm_node *node;
};

static void
nv40_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	iowrite32_native(data, iobj->imem->iomem + iobj->node->offset + offset);
}

static u32
nv40_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	return ioread32_native(iobj->imem->iomem + iobj->node->offset + offset);
}

static const struct nvkm_memory_ptrs
nv40_instobj_ptrs = {
	.rd32 = nv40_instobj_rd32,
	.wr32 = nv40_instobj_wr32,
};

static void
nv40_instobj_release(struct nvkm_memory *memory)
{
	wmb();
}

static void __iomem *
nv40_instobj_acquire(struct nvkm_memory *memory)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	return iobj->imem->iomem + iobj->node->offset;
}

static u64
nv40_instobj_size(struct nvkm_memory *memory)
{
	return nv40_instobj(memory)->node->length;
}

static u64
nv40_instobj_addr(struct nvkm_memory *memory)
{
	return nv40_instobj(memory)->node->offset;
}

static enum nvkm_memory_target
nv40_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_INST;
}

static void *
nv40_instobj_dtor(struct nvkm_memory *memory)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	mutex_lock(&iobj->imem->base.mutex);
	nvkm_mm_free(&iobj->imem->heap, &iobj->node);
	mutex_unlock(&iobj->imem->base.mutex);
	nvkm_instobj_dtor(&iobj->imem->base, &iobj->base);
	return iobj;
}

static const struct nvkm_memory_func
nv40_instobj_func = {
	.dtor = nv40_instobj_dtor,
	.target = nv40_instobj_target,
	.size = nv40_instobj_size,
	.addr = nv40_instobj_addr,
	.acquire = nv40_instobj_acquire,
	.release = nv40_instobj_release,
};

static int
nv40_instobj_new(struct nvkm_instmem *base, u32 size, u32 align, bool zero,
		 struct nvkm_memory **pmemory)
{
	struct nv40_instmem *imem = nv40_instmem(base);
	struct nv40_instobj *iobj;
	int ret;

	if (!(iobj = kzalloc(sizeof(*iobj), GFP_KERNEL)))
		return -ENOMEM;
	*pmemory = &iobj->base.memory;

	nvkm_instobj_ctor(&nv40_instobj_func, &imem->base, &iobj->base);
	iobj->base.memory.ptrs = &nv40_instobj_ptrs;
	iobj->imem = imem;

	mutex_lock(&imem->base.mutex);
	ret = nvkm_mm_head(&imem->heap, 0, 1, size, size, align ? align : 1, &iobj->node);
	mutex_unlock(&imem->base.mutex);
	return ret;
}

/******************************************************************************
 * instmem subdev implementation
 *****************************************************************************/

static u32
nv40_instmem_rd32(struct nvkm_instmem *base, u32 addr)
{
	return ioread32_native(nv40_instmem(base)->iomem + addr);
}

static void
nv40_instmem_wr32(struct nvkm_instmem *base, u32 addr, u32 data)
{
	iowrite32_native(data, nv40_instmem(base)->iomem + addr);
}

static int
nv40_instmem_oneinit(struct nvkm_instmem *base)
{
	struct nv40_instmem *imem = nv40_instmem(base);
	struct nvkm_device *device = imem->base.subdev.device;
	int ret, vs;

	/* PRAMIN aperture maps over the end of vram, reserve enough space
	 * to fit graphics contexts for every channel, the magics come
	 * from engine/gr/nv40.c
	 */
	vs = hweight8((nvkm_rd32(device, 0x001540) & 0x0000ff00) >> 8);
	if      (device->chipset == 0x40) imem->base.reserved = 0x6aa0 * vs;
	else if (device->chipset  < 0x43) imem->base.reserved = 0x4f00 * vs;
	else if (nv44_gr_class(device))   imem->base.reserved = 0x4980 * vs;
	else				  imem->base.reserved = 0x4a40 * vs;
	imem->base.reserved += 16 * 1024;
	imem->base.reserved *= 32;		/* per-channel */
	imem->base.reserved += 512 * 1024;	/* pci(e)gart table */
	imem->base.reserved += 512 * 1024;	/* object storage */
	imem->base.reserved = round_up(imem->base.reserved, 4096);

	ret = nvkm_mm_init(&imem->heap, 0, 0, imem->base.reserved, 1);
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

	/* 0x18000-0x18200: reserve for RAMRO
	 * 0x18200-0x20000: padding
	 */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x08000, 0, false,
			      &imem->base.ramro);
	if (ret)
		return ret;

	/* 0x20000-0x21000: reserve for RAMFC
	 * 0x21000-0x40000: padding and some unknown crap
	 */
	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 0x20000, 0, true,
			      &imem->base.ramfc);
	if (ret)
		return ret;

	return 0;
}

static void *
nv40_instmem_dtor(struct nvkm_instmem *base)
{
	struct nv40_instmem *imem = nv40_instmem(base);
	nvkm_memory_unref(&imem->base.ramfc);
	nvkm_memory_unref(&imem->base.ramro);
	nvkm_ramht_del(&imem->base.ramht);
	nvkm_memory_unref(&imem->base.vbios);
	nvkm_mm_fini(&imem->heap);
	if (imem->iomem)
		iounmap(imem->iomem);
	return imem;
}

static const struct nvkm_instmem_func
nv40_instmem = {
	.dtor = nv40_instmem_dtor,
	.oneinit = nv40_instmem_oneinit,
	.rd32 = nv40_instmem_rd32,
	.wr32 = nv40_instmem_wr32,
	.memory_new = nv40_instobj_new,
	.zero = false,
};

int
nv40_instmem_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		 struct nvkm_instmem **pimem)
{
	struct nv40_instmem *imem;

	if (!(imem = kzalloc(sizeof(*imem), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_instmem_ctor(&nv40_instmem, device, type, inst, &imem->base);
	*pimem = &imem->base;

	/* map bar */
	imem->iomem = ioremap_wc(device->func->resource_addr(device, NVKM_BAR2_INST),
				 device->func->resource_size(device, NVKM_BAR2_INST));
	if (!imem->iomem) {
		nvkm_error(&imem->base.subdev, "unable to map PRAMIN BAR\n");
		return -EFAULT;
	}

	return 0;
}
