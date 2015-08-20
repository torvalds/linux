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

#include <core/memory.h>
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
#define nv40_instobj(p) container_of((p), struct nv40_instobj, memory)

struct nv40_instobj {
	struct nvkm_memory memory;
	struct nv40_instmem *imem;
	struct nvkm_mm_node *node;
};

static enum nvkm_memory_target
nv40_instobj_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_INST;
}

static u64
nv40_instobj_addr(struct nvkm_memory *memory)
{
	return nv40_instobj(memory)->node->offset;
}

static u64
nv40_instobj_size(struct nvkm_memory *memory)
{
	return nv40_instobj(memory)->node->length;
}

static void __iomem *
nv40_instobj_acquire(struct nvkm_memory *memory)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	return iobj->imem->iomem + iobj->node->offset;
}

static void
nv40_instobj_release(struct nvkm_memory *memory)
{
}

static u32
nv40_instobj_rd32(struct nvkm_memory *memory, u64 offset)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	return ioread32_native(iobj->imem->iomem + iobj->node->offset + offset);
}

static void
nv40_instobj_wr32(struct nvkm_memory *memory, u64 offset, u32 data)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	iowrite32_native(data, iobj->imem->iomem + iobj->node->offset + offset);
}

static void *
nv40_instobj_dtor(struct nvkm_memory *memory)
{
	struct nv40_instobj *iobj = nv40_instobj(memory);
	mutex_lock(&iobj->imem->base.subdev.mutex);
	nvkm_mm_free(&iobj->imem->heap, &iobj->node);
	mutex_unlock(&iobj->imem->base.subdev.mutex);
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
	.rd32 = nv40_instobj_rd32,
	.wr32 = nv40_instobj_wr32,
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
	*pmemory = &iobj->memory;

	nvkm_memory_ctor(&nv40_instobj_func, &iobj->memory);
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
nv40_instmem_rd32(struct nvkm_instmem *obj, u32 addr)
{
	struct nv40_instmem *imem = container_of(obj, typeof(*imem), base);
	return ioread32_native(imem->iomem + addr);
}

static void
nv40_instmem_wr32(struct nvkm_instmem *obj, u32 addr, u32 data)
{
	struct nv40_instmem *imem = container_of(obj, typeof(*imem), base);
	iowrite32_native(data, imem->iomem + addr);
}

static void
nv40_instmem_dtor(struct nvkm_object *object)
{
	struct nv40_instmem *imem = (void *)object;
	nvkm_gpuobj_ref(NULL, &imem->base.ramfc);
	nvkm_gpuobj_ref(NULL, &imem->base.ramro);
	nvkm_ramht_ref(NULL, &imem->base.ramht);
	nvkm_gpuobj_ref(NULL, &imem->base.vbios);
	nvkm_mm_fini(&imem->heap);
	if (imem->iomem)
		iounmap(imem->iomem);
	nvkm_instmem_destroy(&imem->base);
}

static const struct nvkm_instmem_func
nv40_instmem_func = {
	.rd32 = nv40_instmem_rd32,
	.wr32 = nv40_instmem_wr32,
};

static int
nv40_instmem_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nv40_instmem *imem;
	int ret, bar, vs;

	ret = nvkm_instmem_create(parent, engine, oclass, &imem);
	*pobject = nv_object(imem);
	if (ret)
		return ret;

	imem->base.func = &nv40_instmem_func;

	/* map bar */
	if (nv_device_resource_len(device, 2))
		bar = 2;
	else
		bar = 3;

	imem->iomem = ioremap(nv_device_resource_start(device, bar),
			      nv_device_resource_len(device, bar));
	if (!imem->iomem) {
		nvkm_error(&imem->base.subdev, "unable to map PRAMIN BAR\n");
		return -EFAULT;
	}

	/* PRAMIN aperture maps over the end of vram, reserve enough space
	 * to fit graphics contexts for every channel, the magics come
	 * from engine/gr/nv40.c
	 */
	vs = hweight8((nvkm_rd32(device, 0x001540) & 0x0000ff00) >> 8);
	if      (device->chipset == 0x40) imem->base.reserved = 0x6aa0 * vs;
	else if (device->chipset  < 0x43) imem->base.reserved = 0x4f00 * vs;
	else if (nv44_gr_class(imem))     imem->base.reserved = 0x4980 * vs;
	else				  imem->base.reserved = 0x4a40 * vs;
	imem->base.reserved += 16 * 1024;
	imem->base.reserved *= 32;		/* per-channel */
	imem->base.reserved += 512 * 1024;	/* pci(e)gart table */
	imem->base.reserved += 512 * 1024;	/* object storage */

	imem->base.reserved = round_up(imem->base.reserved, 4096);

	ret = nvkm_mm_init(&imem->heap, 0, imem->base.reserved, 1);
	if (ret)
		return ret;

	/* 0x00000-0x10000: reserve for probable vbios image */
	ret = nvkm_gpuobj_new(nv_object(imem), NULL, 0x10000, 0, 0,
			      &imem->base.vbios);
	if (ret)
		return ret;

	/* 0x10000-0x18000: reserve for RAMHT */
	ret = nvkm_ramht_new(nv_object(imem), NULL, 0x08000, 0,
			     &imem->base.ramht);
	if (ret)
		return ret;

	/* 0x18000-0x18200: reserve for RAMRO
	 * 0x18200-0x20000: padding
	 */
	ret = nvkm_gpuobj_new(nv_object(imem), NULL, 0x08000, 0, 0,
			      &imem->base.ramro);
	if (ret)
		return ret;

	/* 0x20000-0x21000: reserve for RAMFC
	 * 0x21000-0x40000: padding and some unknown crap
	 */
	ret = nvkm_gpuobj_new(nv_object(imem), NULL, 0x20000, 0,
			      NVOBJ_FLAG_ZERO_ALLOC, &imem->base.ramfc);
	if (ret)
		return ret;

	return 0;
}

struct nvkm_oclass *
nv40_instmem_oclass = &(struct nvkm_instmem_impl) {
	.base.handle = NV_SUBDEV(INSTMEM, 0x40),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_instmem_ctor,
		.dtor = nv40_instmem_dtor,
		.init = _nvkm_instmem_init,
		.fini = _nvkm_instmem_fini,
	},
	.memory_new = nv40_instobj_new,
	.persistent = false,
	.zero = false,
}.base;
