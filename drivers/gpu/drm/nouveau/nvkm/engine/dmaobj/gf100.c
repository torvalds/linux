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

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

struct gf100_dmaobj_priv {
	struct nvkm_dmaobj base;
	u32 flags0;
	u32 flags5;
};

static int
gf100_dmaobj_bind(struct nvkm_dmaobj *dmaobj, struct nvkm_object *parent,
		  struct nvkm_gpuobj **pgpuobj)
{
	struct gf100_dmaobj_priv *priv = (void *)dmaobj;
	int ret;

	if (!nv_iclass(parent, NV_ENGCTX_CLASS)) {
		switch (nv_mclass(parent->parent)) {
		case GT214_DISP_CORE_CHANNEL_DMA:
		case GT214_DISP_BASE_CHANNEL_DMA:
		case GT214_DISP_OVERLAY_CHANNEL_DMA:
			break;
		default:
			return -EINVAL;
		}
	} else
		return 0;

	ret = nvkm_gpuobj_new(parent, parent, 24, 32, 0, pgpuobj);
	if (ret == 0) {
		nv_wo32(*pgpuobj, 0x00, priv->flags0 | nv_mclass(dmaobj));
		nv_wo32(*pgpuobj, 0x04, lower_32_bits(priv->base.limit));
		nv_wo32(*pgpuobj, 0x08, lower_32_bits(priv->base.start));
		nv_wo32(*pgpuobj, 0x0c, upper_32_bits(priv->base.limit) << 24 |
					upper_32_bits(priv->base.start));
		nv_wo32(*pgpuobj, 0x10, 0x00000000);
		nv_wo32(*pgpuobj, 0x14, priv->flags5);
	}

	return ret;
}

static int
gf100_dmaobj_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	struct nvkm_dmaeng *dmaeng = (void *)engine;
	union {
		struct gf100_dma_v0 v0;
	} *args;
	struct gf100_dmaobj_priv *priv;
	u32 kind, user, unkn;
	int ret;

	ret = nvkm_dmaobj_create(parent, engine, oclass, &data, &size, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;
	args = data;

	nv_ioctl(parent, "create gf100 dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nv_ioctl(parent, "create gf100 dma vers %d priv %d kind %02x\n",
			 args->v0.version, args->v0.priv, args->v0.kind);
		kind = args->v0.kind;
		user = args->v0.priv;
		unkn = 0;
	} else
	if (size == 0) {
		if (priv->base.target != NV_MEM_TARGET_VM) {
			kind = GF100_DMA_V0_KIND_PITCH;
			user = GF100_DMA_V0_PRIV_US;
			unkn = 2;
		} else {
			kind = GF100_DMA_V0_KIND_VM;
			user = GF100_DMA_V0_PRIV_VM;
			unkn = 0;
		}
	} else
		return ret;

	if (user > 2)
		return -EINVAL;
	priv->flags0 |= (kind << 22) | (user << 20);
	priv->flags5 |= (unkn << 16);

	switch (priv->base.target) {
	case NV_MEM_TARGET_VM:
		priv->flags0 |= 0x00000000;
		break;
	case NV_MEM_TARGET_VRAM:
		priv->flags0 |= 0x00010000;
		break;
	case NV_MEM_TARGET_PCI:
		priv->flags0 |= 0x00020000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		priv->flags0 |= 0x00030000;
		break;
	default:
		return -EINVAL;
	}

	switch (priv->base.access) {
	case NV_MEM_ACCESS_VM:
		break;
	case NV_MEM_ACCESS_RO:
		priv->flags0 |= 0x00040000;
		break;
	case NV_MEM_ACCESS_WO:
	case NV_MEM_ACCESS_RW:
		priv->flags0 |= 0x00080000;
		break;
	}

	return dmaeng->bind(&priv->base, nv_object(priv), (void *)pobject);
}

static struct nvkm_ofuncs
gf100_dmaobj_ofuncs = {
	.ctor =  gf100_dmaobj_ctor,
	.dtor = _nvkm_dmaobj_dtor,
	.init = _nvkm_dmaobj_init,
	.fini = _nvkm_dmaobj_fini,
};

static struct nvkm_oclass
gf100_dmaeng_sclass[] = {
	{ NV_DMA_FROM_MEMORY, &gf100_dmaobj_ofuncs },
	{ NV_DMA_TO_MEMORY, &gf100_dmaobj_ofuncs },
	{ NV_DMA_IN_MEMORY, &gf100_dmaobj_ofuncs },
	{}
};

struct nvkm_oclass *
gf100_dmaeng_oclass = &(struct nvkm_dmaeng_impl) {
	.base.handle = NV_ENGINE(DMAOBJ, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_dmaeng_ctor,
		.dtor = _nvkm_dmaeng_dtor,
		.init = _nvkm_dmaeng_init,
		.fini = _nvkm_dmaeng_fini,
	},
	.sclass = gf100_dmaeng_sclass,
	.bind = gf100_dmaobj_bind,
}.base;
