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

#include <core/device.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>

struct nvkm_barobj {
	struct nvkm_object base;
	struct nvkm_vma vma;
	void __iomem *iomem;
};

static int
nvkm_barobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_bar *bar = nvkm_bar(device);
	struct nvkm_mem *mem = data;
	struct nvkm_barobj *barobj;
	int ret;

	ret = nvkm_object_create(parent, engine, oclass, 0, &barobj);
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
nvkm_barobj_dtor(struct nvkm_object *object)
{
	struct nvkm_bar *bar = nvkm_bar(object);
	struct nvkm_barobj *barobj = (void *)object;
	if (barobj->vma.node) {
		if (barobj->iomem)
			iounmap(barobj->iomem);
		bar->unmap(bar, &barobj->vma);
	}
	nvkm_object_destroy(&barobj->base);
}

static u32
nvkm_barobj_rd32(struct nvkm_object *object, u64 addr)
{
	struct nvkm_barobj *barobj = (void *)object;
	return ioread32_native(barobj->iomem + addr);
}

static void
nvkm_barobj_wr32(struct nvkm_object *object, u64 addr, u32 data)
{
	struct nvkm_barobj *barobj = (void *)object;
	iowrite32_native(data, barobj->iomem + addr);
}

static struct nvkm_oclass
nvkm_barobj_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nvkm_barobj_ctor,
		.dtor = nvkm_barobj_dtor,
		.init = nvkm_object_init,
		.fini = nvkm_object_fini,
		.rd32 = nvkm_barobj_rd32,
		.wr32 = nvkm_barobj_wr32,
	},
};

int
nvkm_bar_alloc(struct nvkm_bar *bar, struct nvkm_object *parent,
	       struct nvkm_mem *mem, struct nvkm_object **pobject)
{
	struct nvkm_object *gpuobj;
	int ret = nvkm_object_ctor(parent, &parent->engine->subdev.object,
				   &nvkm_barobj_oclass, mem, 0, &gpuobj);
	if (ret == 0)
		*pobject = gpuobj;
	return ret;
}

int
nvkm_bar_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_bar *bar;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "BARCTL",
				  "bar", length, pobject);
	bar = *pobject;
	if (ret)
		return ret;

	return 0;
}

void
nvkm_bar_destroy(struct nvkm_bar *bar)
{
	nvkm_subdev_destroy(&bar->base);
}

void
_nvkm_bar_dtor(struct nvkm_object *object)
{
	struct nvkm_bar *bar = (void *)object;
	nvkm_bar_destroy(bar);
}
