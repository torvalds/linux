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

struct nv04_dmaobj_priv {
	struct nvkm_dmaobj base;
	bool clone;
	u32 flags0;
	u32 flags2;
};

static int
nv04_dmaobj_bind(struct nvkm_dmaobj *dmaobj, struct nvkm_object *parent,
		 struct nvkm_gpuobj **pgpuobj)
{
	struct nv04_dmaobj_priv *priv = (void *)dmaobj;
	struct nvkm_gpuobj *gpuobj;
	u64 offset = priv->base.start & 0xfffff000;
	u64 adjust = priv->base.start & 0x00000fff;
	u32 length = priv->base.limit - priv->base.start;
	int ret;

	if (!nv_iclass(parent, NV_ENGCTX_CLASS)) {
		switch (nv_mclass(parent->parent)) {
		case NV03_CHANNEL_DMA:
		case NV10_CHANNEL_DMA:
		case NV17_CHANNEL_DMA:
		case NV40_CHANNEL_DMA:
			break;
		default:
			return -EINVAL;
		}
	}

	if (priv->clone) {
		struct nv04_mmu_priv *mmu = nv04_mmu(dmaobj);
		struct nvkm_gpuobj *pgt = mmu->vm->pgt[0].obj[0];
		if (!dmaobj->start)
			return nvkm_gpuobj_dup(parent, pgt, pgpuobj);
		offset  = nv_ro32(pgt, 8 + (offset >> 10));
		offset &= 0xfffff000;
	}

	ret = nvkm_gpuobj_new(parent, parent, 16, 16, 0, &gpuobj);
	*pgpuobj = gpuobj;
	if (ret == 0) {
		nv_wo32(*pgpuobj, 0x00, priv->flags0 | (adjust << 20));
		nv_wo32(*pgpuobj, 0x04, length);
		nv_wo32(*pgpuobj, 0x08, priv->flags2 | offset);
		nv_wo32(*pgpuobj, 0x0c, priv->flags2 | offset);
	}

	return ret;
}

static int
nv04_dmaobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_dmaeng *dmaeng = (void *)engine;
	struct nv04_mmu_priv *mmu = nv04_mmu(engine);
	struct nv04_dmaobj_priv *priv;
	int ret;

	ret = nvkm_dmaobj_create(parent, engine, oclass, &data, &size, &priv);
	*pobject = nv_object(priv);
	if (ret || (ret = -ENOSYS, size))
		return ret;

	if (priv->base.target == NV_MEM_TARGET_VM) {
		if (nv_object(mmu)->oclass == &nv04_mmu_oclass)
			priv->clone = true;
		priv->base.target = NV_MEM_TARGET_PCI;
		priv->base.access = NV_MEM_ACCESS_RW;
	}

	priv->flags0 = nv_mclass(priv);
	switch (priv->base.target) {
	case NV_MEM_TARGET_VRAM:
		priv->flags0 |= 0x00003000;
		break;
	case NV_MEM_TARGET_PCI:
		priv->flags0 |= 0x00023000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		priv->flags0 |= 0x00033000;
		break;
	default:
		return -EINVAL;
	}

	switch (priv->base.access) {
	case NV_MEM_ACCESS_RO:
		priv->flags0 |= 0x00004000;
		break;
	case NV_MEM_ACCESS_WO:
		priv->flags0 |= 0x00008000;
	case NV_MEM_ACCESS_RW:
		priv->flags2 |= 0x00000002;
		break;
	default:
		return -EINVAL;
	}

	return dmaeng->bind(&priv->base, nv_object(priv), (void *)pobject);
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
