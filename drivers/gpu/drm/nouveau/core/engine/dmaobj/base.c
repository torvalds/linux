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
#include <core/class.h>

#include <subdev/fb.h>
#include <engine/dmaobj.h>

static int
nouveau_dmaobj_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nouveau_dmaeng *dmaeng = (void *)engine;
	struct nouveau_dmaobj *dmaobj;
	struct nouveau_gpuobj *gpuobj;
	struct nv_dma_class *args = data;
	int ret;

	if (size < sizeof(*args))
		return -EINVAL;

	ret = nouveau_object_create(parent, engine, oclass, 0, &dmaobj);
	*pobject = nv_object(dmaobj);
	if (ret)
		return ret;

	switch (args->flags & NV_DMA_TARGET_MASK) {
	case NV_DMA_TARGET_VM:
		dmaobj->target = NV_MEM_TARGET_VM;
		break;
	case NV_DMA_TARGET_VRAM:
		dmaobj->target = NV_MEM_TARGET_VRAM;
		break;
	case NV_DMA_TARGET_PCI:
		dmaobj->target = NV_MEM_TARGET_PCI;
		break;
	case NV_DMA_TARGET_PCI_US:
	case NV_DMA_TARGET_AGP:
		dmaobj->target = NV_MEM_TARGET_PCI_NOSNOOP;
		break;
	default:
		return -EINVAL;
	}

	switch (args->flags & NV_DMA_ACCESS_MASK) {
	case NV_DMA_ACCESS_VM:
		dmaobj->access = NV_MEM_ACCESS_VM;
		break;
	case NV_DMA_ACCESS_RD:
		dmaobj->access = NV_MEM_ACCESS_RO;
		break;
	case NV_DMA_ACCESS_WR:
		dmaobj->access = NV_MEM_ACCESS_WO;
		break;
	case NV_DMA_ACCESS_RDWR:
		dmaobj->access = NV_MEM_ACCESS_RW;
		break;
	default:
		return -EINVAL;
	}

	dmaobj->start = args->start;
	dmaobj->limit = args->limit;
	dmaobj->conf0 = args->conf0;

	switch (nv_mclass(parent)) {
	case NV_DEVICE_CLASS:
		/* delayed, or no, binding */
		break;
	default:
		ret = dmaeng->bind(dmaeng, *pobject, dmaobj, &gpuobj);
		if (ret == 0) {
			nouveau_object_ref(NULL, pobject);
			*pobject = nv_object(gpuobj);
		}
		break;
	}

	return ret;
}

static struct nouveau_ofuncs
nouveau_dmaobj_ofuncs = {
	.ctor = nouveau_dmaobj_ctor,
	.dtor = nouveau_object_destroy,
	.init = nouveau_object_init,
	.fini = nouveau_object_fini,
};

struct nouveau_oclass
nouveau_dmaobj_sclass[] = {
	{ NV_DMA_FROM_MEMORY_CLASS, &nouveau_dmaobj_ofuncs },
	{ NV_DMA_TO_MEMORY_CLASS, &nouveau_dmaobj_ofuncs },
	{ NV_DMA_IN_MEMORY_CLASS, &nouveau_dmaobj_ofuncs },
	{}
};
