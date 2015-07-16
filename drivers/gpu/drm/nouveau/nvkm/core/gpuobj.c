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

void
nvkm_gpuobj_destroy(struct nvkm_gpuobj *gpuobj)
{
	int i;

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0x00000000);
	}

	if (gpuobj->node)
		nvkm_mm_free(&nv_gpuobj(gpuobj->parent)->heap, &gpuobj->node);

	if (gpuobj->heap.block_size)
		nvkm_mm_fini(&gpuobj->heap);

	nvkm_object_destroy(&gpuobj->object);
}

int
nvkm_gpuobj_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, u32 pclass,
		    struct nvkm_object *pargpu, u32 size, u32 align, u32 flags,
		    int length, void **pobject)
{
	struct nvkm_instmem *imem = nvkm_instmem(parent);
	struct nvkm_bar *bar = nvkm_bar(parent);
	struct nvkm_gpuobj *gpuobj;
	struct nvkm_mm *heap = NULL;
	int ret, i;
	u64 addr;

	*pobject = NULL;

	if (pargpu) {
		while ((pargpu = nv_pclass(pargpu, NV_GPUOBJ_CLASS))) {
			if (nv_gpuobj(pargpu)->heap.block_size)
				break;
			pargpu = pargpu->parent;
		}

		if (unlikely(pargpu == NULL)) {
			nv_error(parent, "no gpuobj heap\n");
			return -EINVAL;
		}

		addr =  nv_gpuobj(pargpu)->addr;
		heap = &nv_gpuobj(pargpu)->heap;
		atomic_inc(&parent->refcount);
	} else {
		ret = imem->alloc(imem, parent, size, align, &parent);
		pargpu = parent;
		if (ret)
			return ret;

		addr = nv_memobj(pargpu)->addr;
		size = nv_memobj(pargpu)->size;

		if (bar && bar->alloc) {
			struct nvkm_instobj *iobj = (void *)parent;
			struct nvkm_mem **mem = (void *)(iobj + 1);
			struct nvkm_mem *node = *mem;
			if (!bar->alloc(bar, parent, node, &pargpu)) {
				nvkm_object_ref(NULL, &parent);
				parent = pargpu;
			}
		}
	}

	ret = nvkm_object_create_(parent, engine, oclass, pclass |
				  NV_GPUOBJ_CLASS, length, pobject);
	nvkm_object_ref(NULL, &parent);
	gpuobj = *pobject;
	if (ret)
		return ret;

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
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0x00000000);
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
	struct nvkm_ofuncs *pfuncs = nv_ofuncs(gpuobj->parent);
	if (gpuobj->node)
		addr += gpuobj->node->offset;
	return pfuncs->rd32(gpuobj->parent, addr);
}

void
_nvkm_gpuobj_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nvkm_gpuobj *gpuobj = nv_gpuobj(object);
	struct nvkm_ofuncs *pfuncs = nv_ofuncs(gpuobj->parent);
	if (gpuobj->node)
		addr += gpuobj->node->offset;
	pfuncs->wr32(gpuobj->parent, addr, data);
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
	struct nvkm_object *engine = parent;
	struct nvkm_gpuobj_class args = {
		.pargpu = pargpu,
		.size = size,
		.align = align,
		.flags = flags,
	};

	if (!nv_iclass(engine, NV_SUBDEV_CLASS))
		engine = &engine->engine->subdev.object;
	BUG_ON(engine == NULL);

	return nvkm_object_ctor(parent, engine, &_nvkm_gpuobj_oclass,
				&args, sizeof(args),
				(struct nvkm_object **)pgpuobj);
}

int
nvkm_gpuobj_map(struct nvkm_gpuobj *gpuobj, u32 access, struct nvkm_vma *vma)
{
	struct nvkm_bar *bar = nvkm_bar(gpuobj);
	int ret = -EINVAL;

	if (bar && bar->umap) {
		struct nvkm_instobj *iobj = (void *)
			nv_pclass(nv_object(gpuobj), NV_MEMOBJ_CLASS);
		struct nvkm_mem **mem = (void *)(iobj + 1);
		ret = bar->umap(bar, *mem, access, vma);
	}

	return ret;
}

int
nvkm_gpuobj_map_vm(struct nvkm_gpuobj *gpuobj, struct nvkm_vm *vm,
		   u32 access, struct nvkm_vma *vma)
{
	struct nvkm_instobj *iobj = (void *)
		nv_pclass(nv_object(gpuobj), NV_MEMOBJ_CLASS);
	struct nvkm_mem **mem = (void *)(iobj + 1);
	int ret;

	ret = nvkm_vm_get(vm, gpuobj->size, 12, access, vma);
	if (ret)
		return ret;

	nvkm_vm_map(vma, *mem);
	return 0;
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
	nvkm_object_ref(NULL, &gpuobj->parent);
	nvkm_object_destroy(&gpuobj->object);
}

static struct nvkm_oclass
nvkm_gpudup_oclass = {
	.handle = NV_GPUOBJ_CLASS,
	.ofuncs = &(struct nvkm_ofuncs) {
		.dtor = nvkm_gpudup_dtor,
		.init = nvkm_object_init,
		.fini = nvkm_object_fini,
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

	nvkm_object_ref(nv_object(base), &gpuobj->parent);
	gpuobj->addr = base->addr;
	gpuobj->size = base->size;
	return 0;
}
