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

#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu/nv04.h>

#include <nvif/class.h>

struct nv04_dmaobj {
	struct nvkm_dmaobj base;
	bool clone;
	u32 flags0;
	u32 flags2;
};

static int
nv04_dmaobj_bind(struct nvkm_dmaobj *obj, struct nvkm_gpuobj *parent,
		 struct nvkm_gpuobj **pgpuobj)
{
	struct nv04_dmaobj *dmaobj = container_of(obj, typeof(*dmaobj), base);
	struct nvkm_device *device = dmaobj->base.base.engine->subdev.device;
	u64 offset = dmaobj->base.start & 0xfffff000;
	u64 adjust = dmaobj->base.start & 0x00000fff;
	u32 length = dmaobj->base.limit - dmaobj->base.start;
	int ret;

	if (dmaobj->clone) {
		struct nv04_mmu *mmu = nv04_mmu(dmaobj);
		struct nvkm_memory *pgt = mmu->vm->pgt[0].mem[0];
		if (!dmaobj->base.start)
			return nvkm_gpuobj_wrap(pgt, pgpuobj);
		nvkm_kmap(pgt);
		offset  = nvkm_ro32(pgt, 8 + (offset >> 10));
		offset &= 0xfffff000;
		nvkm_done(pgt);
	}

	ret = nvkm_gpuobj_new(device, 16, 16, false, parent, pgpuobj);
	if (ret == 0) {
		nvkm_kmap(*pgpuobj);
		nvkm_wo32(*pgpuobj, 0x00, dmaobj->flags0 | (adjust << 20));
		nvkm_wo32(*pgpuobj, 0x04, length);
		nvkm_wo32(*pgpuobj, 0x08, dmaobj->flags2 | offset);
		nvkm_wo32(*pgpuobj, 0x0c, dmaobj->flags2 | offset);
		nvkm_done(*pgpuobj);
	}

	return ret;
}

static int
nv04_dmaobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_dmaeng *dmaeng = (void *)engine;
	struct nv04_mmu *mmu = nv04_mmu(engine);
	struct nv04_dmaobj *dmaobj;
	int ret;

	ret = nvkm_dmaobj_create(parent, engine, oclass, &data, &size, &dmaobj);
	*pobject = nv_object(dmaobj);
	if (ret || (ret = -ENOSYS, size))
		return ret;

	if (dmaobj->base.target == NV_MEM_TARGET_VM) {
		if (nv_object(mmu)->oclass == &nv04_mmu_oclass)
			dmaobj->clone = true;
		dmaobj->base.target = NV_MEM_TARGET_PCI;
		dmaobj->base.access = NV_MEM_ACCESS_RW;
	}

	dmaobj->flags0 = nv_mclass(dmaobj);
	switch (dmaobj->base.target) {
	case NV_MEM_TARGET_VRAM:
		dmaobj->flags0 |= 0x00003000;
		break;
	case NV_MEM_TARGET_PCI:
		dmaobj->flags0 |= 0x00023000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		dmaobj->flags0 |= 0x00033000;
		break;
	default:
		return -EINVAL;
	}

	switch (dmaobj->base.access) {
	case NV_MEM_ACCESS_RO:
		dmaobj->flags0 |= 0x00004000;
		break;
	case NV_MEM_ACCESS_WO:
		dmaobj->flags0 |= 0x00008000;
	case NV_MEM_ACCESS_RW:
		dmaobj->flags2 |= 0x00000002;
		break;
	default:
		return -EINVAL;
	}

	return dmaeng->bind(&dmaobj->base, (void *)dmaobj, (void *)pobject);
}

static struct nvkm_ofuncs
nv04_dmaobj_ofuncs = {
	.ctor =  nv04_dmaobj_ctor,
	.dtor = _nvkm_dmaobj_dtor,
	.init = _nvkm_dmaobj_init,
	.fini = _nvkm_dmaobj_fini,
};

static struct nvkm_oclass
nv04_dmaeng_sclass[] = {
	{ NV_DMA_FROM_MEMORY, &nv04_dmaobj_ofuncs },
	{ NV_DMA_TO_MEMORY, &nv04_dmaobj_ofuncs },
	{ NV_DMA_IN_MEMORY, &nv04_dmaobj_ofuncs },
	{}
};

struct nvkm_oclass *
nv04_dmaeng_oclass = &(struct nvkm_dmaeng_impl) {
	.base.handle = NV_ENGINE(DMAOBJ, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_dmaeng_ctor,
		.dtor = _nvkm_dmaeng_dtor,
		.init = _nvkm_dmaeng_init,
		.fini = _nvkm_dmaeng_fini,
	},
	.sclass = nv04_dmaeng_sclass,
	.bind = nv04_dmaobj_bind,
}.base;
