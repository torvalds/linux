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
#include <core/class.h>

#include <subdev/fb.h>
#include <subdev/vm/nv04.h>

#include <engine/dmaobj.h>

struct nv04_dmaeng_priv {
	struct nouveau_dmaeng base;
};

struct nv04_dmaobj_priv {
	struct nouveau_dmaobj base;
};

static int
nv04_dmaobj_bind(struct nouveau_dmaeng *dmaeng,
		 struct nouveau_object *parent,
		 struct nouveau_dmaobj *dmaobj,
		 struct nouveau_gpuobj **pgpuobj)
{
	struct nv04_vmmgr_priv *vmm = nv04_vmmgr(dmaeng);
	struct nouveau_gpuobj *gpuobj;
	u32 flags0 = nv_mclass(dmaobj);
	u32 flags2 = 0x00000000;
	u64 offset = dmaobj->start & 0xfffff000;
	u64 adjust = dmaobj->start & 0x00000fff;
	u32 length = dmaobj->limit - dmaobj->start;
	int ret;

	if (dmaobj->target == NV_MEM_TARGET_VM) {
		if (nv_object(vmm)->oclass == &nv04_vmmgr_oclass) {
			struct nouveau_gpuobj *pgt = vmm->vm->pgt[0].obj[0];
			if (!dmaobj->start)
				return nouveau_gpuobj_dup(parent, pgt, pgpuobj);
			offset  = nv_ro32(pgt, 8 + (offset >> 10));
			offset &= 0xfffff000;
		}

		dmaobj->target = NV_MEM_TARGET_PCI;
		dmaobj->access = NV_MEM_ACCESS_RW;
	}

	switch (dmaobj->target) {
	case NV_MEM_TARGET_VRAM:
		flags0 |= 0x00003000;
		break;
	case NV_MEM_TARGET_PCI:
		flags0 |= 0x00023000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		flags0 |= 0x00033000;
		break;
	default:
		return -EINVAL;
	}

	switch (dmaobj->access) {
	case NV_MEM_ACCESS_RO:
		flags0 |= 0x00004000;
		break;
	case NV_MEM_ACCESS_WO:
		flags0 |= 0x00008000;
	case NV_MEM_ACCESS_RW:
		flags2 |= 0x00000002;
		break;
	default:
		return -EINVAL;
	}

	ret = nouveau_gpuobj_new(parent, parent, 16, 16, 0, &gpuobj);
	*pgpuobj = gpuobj;
	if (ret == 0) {
		nv_wo32(*pgpuobj, 0x00, flags0 | (adjust << 20));
		nv_wo32(*pgpuobj, 0x04, length);
		nv_wo32(*pgpuobj, 0x08, flags2 | offset);
		nv_wo32(*pgpuobj, 0x0c, flags2 | offset);
	}

	return ret;
}

static int
nv04_dmaobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nouveau_dmaeng *dmaeng = (void *)engine;
	struct nv04_dmaobj_priv *dmaobj;
	struct nouveau_gpuobj *gpuobj;
	int ret;

	ret = nouveau_dmaobj_create(parent, engine, oclass,
				    data, size, &dmaobj);
	*pobject = nv_object(dmaobj);
	if (ret)
		return ret;

	switch (nv_mclass(parent)) {
	case NV_DEVICE_CLASS:
		break;
	case NV03_CHANNEL_DMA_CLASS:
	case NV10_CHANNEL_DMA_CLASS:
	case NV17_CHANNEL_DMA_CLASS:
	case NV40_CHANNEL_DMA_CLASS:
		ret = dmaeng->bind(dmaeng, *pobject, &dmaobj->base, &gpuobj);
		nouveau_object_ref(NULL, pobject);
		*pobject = nv_object(gpuobj);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static struct nouveau_ofuncs
nv04_dmaobj_ofuncs = {
	.ctor = nv04_dmaobj_ctor,
	.dtor = _nouveau_dmaobj_dtor,
	.init = _nouveau_dmaobj_init,
	.fini = _nouveau_dmaobj_fini,
};

static struct nouveau_oclass
nv04_dmaobj_sclass[] = {
	{ 0x0002, &nv04_dmaobj_ofuncs },
	{ 0x0003, &nv04_dmaobj_ofuncs },
	{ 0x003d, &nv04_dmaobj_ofuncs },
	{}
};

static int
nv04_dmaeng_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nv04_dmaeng_priv *priv;
	int ret;

	ret = nouveau_dmaeng_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.sclass = nv04_dmaobj_sclass;
	priv->base.bind = nv04_dmaobj_bind;
	return 0;
}

struct nouveau_oclass
nv04_dmaeng_oclass = {
	.handle = NV_ENGINE(DMAOBJ, 0x04),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_dmaeng_ctor,
		.dtor = _nouveau_dmaeng_dtor,
		.init = _nouveau_dmaeng_init,
		.fini = _nouveau_dmaeng_fini,
	},
};
