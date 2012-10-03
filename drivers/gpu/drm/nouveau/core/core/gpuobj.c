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

#include <core/object.h>
#include <core/gpuobj.h>

#include <subdev/instmem.h>
#include <subdev/bar.h>
#include <subdev/vm.h>

void
nouveau_gpuobj_destroy(struct nouveau_gpuobj *gpuobj)
{
	int i;

	if (gpuobj->flags & NVOBJ_FLAG_ZERO_FREE) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0x00000000);
	}

	if (gpuobj->heap.block_size)
		nouveau_mm_fini(&gpuobj->heap);

	nouveau_object_destroy(&gpuobj->base);
}

int
nouveau_gpuobj_create_(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, u32 pclass,
		       struct nouveau_object *pargpu,
		       u32 size, u32 align, u32 flags,
		       int length, void **pobject)
{
	struct nouveau_instmem *imem = nouveau_instmem(parent);
	struct nouveau_bar *bar = nouveau_bar(parent);
	struct nouveau_gpuobj *gpuobj;
	struct nouveau_mm *heap = NULL;
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
			struct nouveau_instobj *iobj = (void *)parent;
			struct nouveau_mem **mem = (void *)(iobj + 1);
			struct nouveau_mem *node = *mem;
			if (!bar->alloc(bar, parent, node, &pargpu)) {
				nouveau_object_ref(NULL, &parent);
				parent = pargpu;
			}
		}
	}

	ret = nouveau_object_create_(parent, engine, oclass, pclass |
				     NV_GPUOBJ_CLASS, length, pobject);
	nouveau_object_ref(NULL, &parent);
	gpuobj = *pobject;
	if (ret)
		return ret;

	gpuobj->parent = pargpu;
	gpuobj->flags = flags;
	gpuobj->addr = addr;
	gpuobj->size = size;

	if (heap) {
		ret = nouveau_mm_head(heap, 1, size, size,
				      max(align, (u32)1), &gpuobj->node);
		if (ret)
			return ret;

		gpuobj->addr += gpuobj->node->offset;
	}

	if (gpuobj->flags & NVOBJ_FLAG_HEAP) {
		ret = nouveau_mm_init(&gpuobj->heap, 0, gpuobj->size, 1);
		if (ret)
			return ret;
	}

	if (flags & NVOBJ_FLAG_ZERO_ALLOC) {
		for (i = 0; i < gpuobj->size; i += 4)
			nv_wo32(gpuobj, i, 0x00000000);
	}

	return ret;
}

struct nouveau_gpuobj_class {
	struct nouveau_object *pargpu;
	u64 size;
	u32 align;
	u32 flags;
};

static int
_nouveau_gpuobj_ctor(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, void *data, u32 size,
		     struct nouveau_object **pobject)
{
	struct nouveau_gpuobj_class *args = data;
	struct nouveau_gpuobj *object;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, args->pargpu,
				    args->size, args->align, args->flags,
				    &object);
	*pobject = nv_object(object);
	if (ret)
		return ret;

	return 0;
}

void
_nouveau_gpuobj_dtor(struct nouveau_object *object)
{
	nouveau_gpuobj_destroy(nv_gpuobj(object));
}

int
_nouveau_gpuobj_init(struct nouveau_object *object)
{
	return nouveau_gpuobj_init(nv_gpuobj(object));
}

int
_nouveau_gpuobj_fini(struct nouveau_object *object, bool suspend)
{
	return nouveau_gpuobj_fini(nv_gpuobj(object), suspend);
}

u32
_nouveau_gpuobj_rd32(struct nouveau_object *object, u32 addr)
{
	struct nouveau_gpuobj *gpuobj = nv_gpuobj(object);
	struct nouveau_ofuncs *pfuncs = nv_ofuncs(gpuobj->parent);
	if (gpuobj->node)
		addr += gpuobj->node->offset;
	return pfuncs->rd32(gpuobj->parent, addr);
}

void
_nouveau_gpuobj_wr32(struct nouveau_object *object, u32 addr, u32 data)
{
	struct nouveau_gpuobj *gpuobj = nv_gpuobj(object);
	struct nouveau_ofuncs *pfuncs = nv_ofuncs(gpuobj->parent);
	if (gpuobj->node)
		addr += gpuobj->node->offset;
	pfuncs->wr32(gpuobj->parent, addr, data);
}

static struct nouveau_oclass
_nouveau_gpuobj_oclass = {
	.handle = 0x00000000,
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_gpuobj_ctor,
		.dtor = _nouveau_gpuobj_dtor,
		.init = _nouveau_gpuobj_init,
		.fini = _nouveau_gpuobj_fini,
		.rd32 = _nouveau_gpuobj_rd32,
		.wr32 = _nouveau_gpuobj_wr32,
	},
};

int
nouveau_gpuobj_new(struct nouveau_object *parent, struct nouveau_object *pargpu,
		   u32 size, u32 align, u32 flags,
		   struct nouveau_gpuobj **pgpuobj)
{
	struct nouveau_object *engine = parent;
	struct nouveau_gpuobj_class args = {
		.pargpu = pargpu,
		.size = size,
		.align = align,
		.flags = flags,
	};

	if (!nv_iclass(engine, NV_SUBDEV_CLASS))
		engine = engine->engine;
	BUG_ON(engine == NULL);

	return nouveau_object_ctor(parent, engine, &_nouveau_gpuobj_oclass,
				   &args, sizeof(args),
				   (struct nouveau_object **)pgpuobj);
}

int
nouveau_gpuobj_map(struct nouveau_gpuobj *gpuobj, u32 access,
		   struct nouveau_vma *vma)
{
	struct nouveau_bar *bar = nouveau_bar(gpuobj);
	int ret = -EINVAL;

	if (bar && bar->umap) {
		struct nouveau_instobj *iobj = (void *)
			nv_pclass(nv_object(gpuobj), NV_MEMOBJ_CLASS);
		struct nouveau_mem **mem = (void *)(iobj + 1);
		ret = bar->umap(bar, *mem, access, vma);
	}

	return ret;
}

int
nouveau_gpuobj_map_vm(struct nouveau_gpuobj *gpuobj, struct nouveau_vm *vm,
		      u32 access, struct nouveau_vma *vma)
{
	struct nouveau_instobj *iobj = (void *)
		nv_pclass(nv_object(gpuobj), NV_MEMOBJ_CLASS);
	struct nouveau_mem **mem = (void *)(iobj + 1);
	int ret;

	ret = nouveau_vm_get(vm, gpuobj->size, 12, access, vma);
	if (ret)
		return ret;

	nouveau_vm_map(vma, *mem);
	return 0;
}

void
nouveau_gpuobj_unmap(struct nouveau_vma *vma)
{
	if (vma->node) {
		nouveau_vm_unmap(vma);
		nouveau_vm_put(vma);
	}
}

/* the below is basically only here to support sharing the paged dma object
 * for PCI(E)GART on <=nv4x chipsets, and should *not* be expected to work
 * anywhere else.
 */

static void
nouveau_gpudup_dtor(struct nouveau_object *object)
{
	struct nouveau_gpuobj *gpuobj = (void *)object;
	nouveau_object_ref(NULL, &gpuobj->parent);
	nouveau_object_destroy(&gpuobj->base);
}

static struct nouveau_oclass
nouveau_gpudup_oclass = {
	.handle = NV_GPUOBJ_CLASS,
	.ofuncs = &(struct nouveau_ofuncs) {
		.dtor = nouveau_gpudup_dtor,
		.init = nouveau_object_init,
		.fini = nouveau_object_fini,
	},
};

int
nouveau_gpuobj_dup(struct nouveau_object *parent, struct nouveau_gpuobj *base,
		   struct nouveau_gpuobj **pgpuobj)
{
	struct nouveau_gpuobj *gpuobj;
	int ret;

	ret = nouveau_object_create(parent, parent->engine,
				   &nouveau_gpudup_oclass, 0, &gpuobj);
	*pgpuobj = gpuobj;
	if (ret)
		return ret;

	nouveau_object_ref(nv_object(base), &gpuobj->parent);
	gpuobj->addr = base->addr;
	gpuobj->size = base->size;
	return 0;
}
