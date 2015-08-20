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

static void
nvkm_gpuobj_release(struct nvkm_gpuobj *gpuobj)
{
	if (gpuobj->node) {
		nvkm_done(gpuobj->parent);
		return;
	}
	nvkm_done(gpuobj->memory);
}

static void
nvkm_gpuobj_acquire(struct nvkm_gpuobj *gpuobj)
{
	if (gpuobj->node) {
		nvkm_kmap(gpuobj->parent);
		return;
	}
	nvkm_kmap(gpuobj->memory);
}

static u32
nvkm_gpuobj_rd32(struct nvkm_gpuobj *gpuobj, u32 offset)
{
	if (gpuobj->node)
		return nvkm_ro32(gpuobj->parent, gpuobj->node->offset + offset);
	return nvkm_ro32(gpuobj->memory, offset);
}

static void
nvkm_gpuobj_wr32(struct nvkm_gpuobj *gpuobj, u32 offset, u32 data)
{
	if (gpuobj->node) {
		nvkm_wo32(gpuobj->parent, gpuobj->node->offset + offset, data);
		return;
	}
	nvkm_wo32(gpuobj->memory, offset, data);
}

void
nvkm_gpuobj_destroy(struct nvkm_gpuobj *gpuobj)
{
	int i;

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE) {
		nvkm_kmap(gpuobj);
		for (i = 0; i < gpuobj->size; i += 4)
			nvkm_wo32(gpuobj, i, 0x00000000);
		nvkm_done(gpuobj);
	}

	if (gpuobj->node)
		nvkm_mm_free(&nv_gpuobj(gpuobj->parent)->heap, &gpuobj->node);

	if (gpuobj->heap.block_size)
		nvkm_mm_fini(&gpuobj->heap);

	nvkm_memory_del(&gpuobj->memory);
	nvkm_object_destroy(&gpuobj->object);
}

static const struct nvkm_gpuobj_func
nvkm_gpuobj_func = {
	.acquire = nvkm_gpuobj_acquire,
	.release = nvkm_gpuobj_release,
	.rd32 = nvkm_gpuobj_rd32,
	.wr32 = nvkm_gpuobj_wr32,
};

int
nvkm_gpuobj_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    struct nvkm_object *objgpu, u32 size, u32 align, u32 flags,
		    int length, void **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_memory *memory = NULL;
	struct nvkm_gpuobj *pargpu = NULL;
	struct nvkm_gpuobj *gpuobj;
	struct nvkm_mm *heap = NULL;
	int ret, i;
	u64 addr;

	*pobject = NULL;

	if (objgpu) {
		while ((objgpu = nv_pclass(objgpu, NV_GPUOBJ_CLASS))) {
			if (nv_gpuobj(objgpu)->heap.block_size)
				break;
			objgpu = objgpu->parent;
		}

		if (WARN_ON(objgpu == NULL))
			return -EINVAL;
		pargpu = nv_gpuobj(objgpu);

		addr =  pargpu->addr;
		heap = &pargpu->heap;
	} else {
		ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST,
				      size, align, false, &memory);
		if (ret)
			return ret;

		addr = nvkm_memory_addr(memory);
		size = nvkm_memory_size(memory);
	}

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_GPUOBJ_CLASS, length, pobject);
	gpuobj = *pobject;
	if (ret) {
		nvkm_memory_del(&memory);
		return ret;
	}

	gpuobj->func = &nvkm_gpuobj_func;
	gpuobj->memory = memory;
	gpuobj->parent = pargpu;
	gpuobj->flags = flags;
	gpuobj->addr = addr;
	gpuobj->size = size;

	if (heap) {
		ret = nvkm_mm_head(heap, 0, 1, size, size, max(align, (u32)1),
				   &gpuobj->node);
		if (ret)
			return ret;

		gpuobj->addr += gpuobj->node->offset;
	}

	if (gpuobj->flags & NVOBJ_FLAG_HEAP) {
		ret = nvkm_mm_init(&gpuobj->heap, 0, gpuobj->size, 1);
		if (ret)
			return ret;
	}

	if (flags & NVOBJ_FLAG_ZERO_ALLOC) {
		nvkm_kmap(gpuobj);
		for (i = 0; i < gpuobj->size; i += 4)
			nvkm_wo32(gpuobj, i, 0x00000000);
		nvkm_done(gpuobj);
	}

	return ret;
}

struct nvkm_gpuobj_class {
	struct nvkm_object *pargpu;
	u64 size;
	u32 align;
	u32 flags;
};

static int
_nvkm_gpuobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_gpuobj_class *args = data;
	struct nvkm_gpuobj *object;
	int ret;

	ret = nvkm_gpuobj_create(parent, engine, oclass, 0, args->pargpu,
				 args->size, args->align, args->flags,
				 &object);
	*pobject = nv_object(object);
	if (ret)
		return ret;

	return 0;
}

void
_nvkm_gpuobj_dtor(struct nvkm_object *object)
{
	nvkm_gpuobj_destroy(nv_gpuobj(object));
}

int
_nvkm_gpuobj_init(struct nvkm_object *object)
{
	return nvkm_gpuobj_init(nv_gpuobj(object));
}

int
_nvkm_gpuobj_fini(struct nvkm_object *object, bool suspend)
{
	return nvkm_gpuobj_fini(nv_gpuobj(object), suspend);
}

u32
_nvkm_gpuobj_rd32(struct nvkm_object *object, u64 addr)
{
	struct nvkm_gpuobj *gpuobj = nv_gpuobj(object);
	return nvkm_ro32(gpuobj, addr);
}

void
_nvkm_gpuobj_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nvkm_gpuobj *gpuobj = nv_gpuobj(object);
	nvkm_wo32(gpuobj, addr, data);
}

static struct nvkm_oclass
_nvkm_gpuobj_oclass = {
	.handle = 0x00000000,
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_gpuobj_ctor,
		.dtor = _nvkm_gpuobj_dtor,
		.init = _nvkm_gpuobj_init,
		.fini = _nvkm_gpuobj_fini,
		.rd32 = _nvkm_gpuobj_rd32,
		.wr32 = _nvkm_gpuobj_wr32,
	},
};

int
nvkm_gpuobj_new(struct nvkm_object *parent, struct nvkm_object *pargpu,
		u32 size, u32 align, u32 flags,
		struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_gpuobj_class args = {
		.pargpu = pargpu,
		.size = size,
		.align = align,
		.flags = flags,
	};

	return nvkm_object_old(parent, &parent->engine->subdev.object,
			       &_nvkm_gpuobj_oclass, &args, sizeof(args),
			       (struct nvkm_object **)pgpuobj);
}

int
nvkm_gpuobj_map(struct nvkm_gpuobj *gpuobj, u32 access, struct nvkm_vma *vma)
{
	struct nvkm_memory *memory = gpuobj->memory;
	struct nvkm_bar *bar = nvkm_bar(gpuobj);
	int ret = -EINVAL;

	if (bar && bar->umap) {
		ret = bar->umap(bar, gpuobj->size, 12, vma);
		if (ret == 0)
			nvkm_memory_map(memory, vma, 0);
	}

	return ret;
}

int
nvkm_gpuobj_map_vm(struct nvkm_gpuobj *gpuobj, struct nvkm_vm *vm,
		   u32 access, struct nvkm_vma *vma)
{
	struct nvkm_memory *memory = gpuobj->memory;
	int ret = nvkm_vm_get(vm, gpuobj->size, 12, access, vma);
	if (ret == 0)
		nvkm_memory_map(memory, vma, 0);
	return ret;
}

void
nvkm_gpuobj_unmap(struct nvkm_vma *vma)
{
	if (vma->node) {
		nvkm_vm_unmap(vma);
		nvkm_vm_put(vma);
	}
}

/* the below is basically only here to support sharing the paged dma object
 * for PCI(E)GART on <=nv4x chipsets, and should *not* be expected to work
 * anywhere else.
 */

static void
nvkm_gpudup_dtor(struct nvkm_object *object)
{
	struct nvkm_gpuobj *gpuobj = (void *)object;
	nvkm_object_ref(NULL, (struct nvkm_object **)&gpuobj->parent);
	nvkm_object_destroy(&gpuobj->object);
}

static struct nvkm_oclass
nvkm_gpudup_oclass = {
	.handle = NV_GPUOBJ_CLASS,
	.ofuncs = &(struct nvkm_ofuncs) {
		.dtor = nvkm_gpudup_dtor,
		.init = _nvkm_object_init,
		.fini = _nvkm_object_fini,
	},
};

int
nvkm_gpuobj_dup(struct nvkm_object *parent, struct nvkm_gpuobj *base,
		struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_gpuobj *gpuobj;
	int ret;

	ret = nvkm_object_create(parent, &parent->engine->subdev.object,
				 &nvkm_gpudup_oclass, 0, &gpuobj);
	*pgpuobj = gpuobj;
	if (ret)
		return ret;

	nvkm_object_ref(nv_object(base), (struct nvkm_object **)&gpuobj->parent);
	gpuobj->addr = base->addr;
	gpuobj->size = base->size;
	return 0;
}
