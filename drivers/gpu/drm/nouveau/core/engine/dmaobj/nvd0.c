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

#include <core/client.h>
#include <core/device.h>
#include <core/gpuobj.h>
#include <nvif/unpack.h>
#include <nvif/class.h>

#include <subdev/fb.h>

#include "priv.h"

struct nvd0_dmaobj_priv {
	struct nouveau_dmaobj base;
	u32 flags0;
};

static int
nvd0_dmaobj_bind(struct nouveau_dmaobj *dmaobj,
		 struct nouveau_object *parent,
		 struct nouveau_gpuobj **pgpuobj)
{
	struct nvd0_dmaobj_priv *priv = (void *)dmaobj;
	int ret;

	if (!nv_iclass(parent, NV_ENGCTX_CLASS)) {
		switch (nv_mclass(parent->parent)) {
		case GF110_DISP_CORE_CHANNEL_DMA:
		case GK104_DISP_CORE_CHANNEL_DMA:
		case GK110_DISP_CORE_CHANNEL_DMA:
		case GM107_DISP_CORE_CHANNEL_DMA:
		case GM204_DISP_CORE_CHANNEL_DMA:
		case GF110_DISP_BASE_CHANNEL_DMA:
		case GK104_DISP_BASE_CHANNEL_DMA:
		case GK110_DISP_BASE_CHANNEL_DMA:
		case GF110_DISP_OVERLAY_CONTROL_DMA:
		case GK104_DISP_OVERLAY_CONTROL_DMA:
			break;
		default:
			return -EINVAL;
		}
	} else
		return 0;

	ret = nouveau_gpuobj_new(parent, parent, 24, 32, 0, pgpuobj);
	if (ret == 0) {
		nv_wo32(*pgpuobj, 0x00, priv->flags0);
		nv_wo32(*pgpuobj, 0x04, priv->base.start >> 8);
		nv_wo32(*pgpuobj, 0x08, priv->base.limit >> 8);
		nv_wo32(*pgpuobj, 0x0c, 0x00000000);
		nv_wo32(*pgpuobj, 0x10, 0x00000000);
		nv_wo32(*pgpuobj, 0x14, 0x00000000);
	}

	return ret;
}

static int
nvd0_dmaobj_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nouveau_dmaeng *dmaeng = (void *)engine;
	union {
		struct gf110_dma_v0 v0;
	} *args;
	struct nvd0_dmaobj_priv *priv;
	u32 kind, page;
	int ret;

	ret = nvkm_dmaobj_create(parent, engine, oclass, &data, &size, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;
	args = data;

	nv_ioctl(parent, "create gf110 dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create gf100 dma vers %d page %d kind %02x\n",
			 args->v0.version, args->v0.page, args->v0.kind);
		kind = args->v0.kind;
		page = args->v0.page;
	} else
	if (size == 0) {
		if (priv->base.target != NV_MEM_TARGET_VM) {
			kind = GF110_DMA_V0_KIND_PITCH;
			page = GF110_DMA_V0_PAGE_SP;
		} else {
			kind = GF110_DMA_V0_KIND_VM;
			page = GF110_DMA_V0_PAGE_LP;
		}
	} else
		return ret;

	if (page > 1)
		return -EINVAL;
	priv->flags0 = (kind << 20) | (page << 6);

	switch (priv->base.target) {
	case NV_MEM_TARGET_VRAM:
		priv->flags0 |= 0x00000009;
		break;
	case NV_MEM_TARGET_VM:
	case NV_MEM_TARGET_PCI:
	case NV_MEM_TARGET_PCI_NOSNOOP:
		/* XXX: don't currently know how to construct a real one
		 *      of these.  we only use them to represent pushbufs
		 *      on these chipsets, and the classes that use them
		 *      deal with the target themselves.
		 */
		break;
	default:
		return -EINVAL;
	}

	return dmaeng->bind(&priv->base, nv_object(priv), (void *)pobject);
}

static struct nouveau_ofuncs
nvd0_dmaobj_ofuncs = {
	.ctor =  nvd0_dmaobj_ctor,
	.dtor = _nvkm_dmaobj_dtor,
	.init = _nvkm_dmaobj_init,
	.fini = _nvkm_dmaobj_fini,
};

static struct nouveau_oclass
nvd0_dmaeng_sclass[] = {
	{ NV_DMA_FROM_MEMORY, &nvd0_dmaobj_ofuncs },
	{ NV_DMA_TO_MEMORY, &nvd0_dmaobj_ofuncs },
	{ NV_DMA_IN_MEMORY, &nvd0_dmaobj_ofuncs },
	{}
};

struct nouveau_oclass *
nvd0_dmaeng_oclass = &(struct nvkm_dmaeng_impl) {
	.base.handle = NV_ENGINE(DMAOBJ, 0xd0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nvkm_dmaeng_ctor,
		.dtor = _nvkm_dmaeng_dtor,
		.init = _nvkm_dmaeng_init,
		.fini = _nvkm_dmaeng_fini,
	},
	.sclass = nvd0_dmaeng_sclass,
	.bind = nvd0_dmaobj_bind,
}.base;
