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

#include <subdev/fb.h>
#include <subdev/vm.h>

#include "priv.h"

struct nouveau_barobj {
	struct nouveau_object base;
	struct nouveau_vma vma;
	void __iomem *iomem;
};

static int
nouveau_barobj_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 size,
		    struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_bar *bar = (void *)engine;
	struct nouveau_mem *mem = data;
	struct nouveau_barobj *barobj;
	int ret;

	ret = nouveau_object_create(parent, engine, oclass, 0, &barobj);
	*pobject = nv_object(barobj);
	if (ret)
		return ret;

	ret = bar->kmap(bar, mem, NV_MEM_ACCESS_RW, &barobj->vma);
	if (ret)
		return ret;

	barobj->iomem = ioremap(nv_device_resource_start(device, 3) +
				(u32)barobj->vma.offset, mem->size << 12);
	if (!barobj->iomem) {
		nv_warn(bar, "PRAMIN ioremap failed\n");
		return -ENOMEM;
	}

	return 0;
}

static void
nouveau_barobj_dtor(struct nouveau_object *object)
{
	struct nouveau_bar *bar = (void *)object->engine;
	struct nouveau_barobj *barobj = (void *)object;
	if (barobj->vma.node) {
		if (barobj->iomem)
			iounmap(barobj->iomem);
		bar->unmap(bar, &barobj->vma);
	}
	nouveau_object_destroy(&barobj->base);
}

static u32
nouveau_barobj_rd32(struct nouveau_object *object, u64 addr)
{
	struct nouveau_barobj *barobj = (void *)object;
	return ioread32_native(barobj->iomem + addr);
}

static void
nouveau_barobj_wr32(struct nouveau_object *object, u64 addr, u32 data)
{
	struct nouveau_barobj *barobj = (void *)object;
	iowrite32_native(data, barobj->iomem + addr);
}

static struct nouveau_oclass
nouveau_barobj_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nouveau_barobj_ctor,
		.dtor = nouveau_barobj_dtor,
		.init = nouveau_object_init,
		.fini = nouveau_object_fini,
		.rd32 = nouveau_barobj_rd32,
		.wr32 = nouveau_barobj_wr32,
	},
};

int
nouveau_bar_alloc(struct nouveau_bar *bar, struct nouveau_object *parent,
		  struct nouveau_mem *mem, struct nouveau_object **pobject)
{
	struct nouveau_object *engine = nv_object(bar);
	struct nouveau_object *gpuobj;
	int ret = nouveau_object_ctor(parent, engine, &nouveau_barobj_oclass,
				      mem, 0, &gpuobj);
	if (ret == 0)
		*pobject = gpuobj;
	return ret;
}

int
nouveau_bar_create_(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, int length, void **pobject)
{
	struct nouveau_bar *bar;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "BARCTL",
				     "bar", length, pobject);
	bar = *pobject;
	if (ret)
		return ret;

	return 0;
}

void
nouveau_bar_destroy(struct nouveau_bar *bar)
{
	nouveau_subdev_destroy(&bar->base);
}

void
_nouveau_bar_dtor(struct nouveau_object *object)
{
	struct nouveau_bar *bar = (void *)object;
	nouveau_bar_destroy(bar);
}
