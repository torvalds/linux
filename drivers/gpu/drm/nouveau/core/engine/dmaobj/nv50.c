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
#include <engine/dmaobj.h>

struct nv50_dmaeng_priv {
	struct nouveau_dmaeng base;
};

static int
nv50_dmaobj_bind(struct nouveau_dmaeng *dmaeng,
		 struct nouveau_object *parent,
		 struct nouveau_dmaobj *dmaobj,
		 struct nouveau_gpuobj **pgpuobj)
{
	u32 flags0 = nv_mclass(dmaobj);
	u32 flags5 = 0x00000000;
	int ret;

	if (!nv_iclass(parent, NV_ENGCTX_CLASS)) {
		switch (nv_mclass(parent->parent)) {
		case NV50_CHANNEL_DMA_CLASS:
		case NV84_CHANNEL_DMA_CLASS:
		case NV50_CHANNEL_IND_CLASS:
		case NV84_CHANNEL_IND_CLASS:
		case NV50_DISP_MAST_CLASS:
		case NV84_DISP_MAST_CLASS:
		case NV94_DISP_MAST_CLASS:
		case NVA0_DISP_MAST_CLASS:
		case NVA3_DISP_MAST_CLASS:
		case NV50_DISP_SYNC_CLASS:
		case NV84_DISP_SYNC_CLASS:
		case NV94_DISP_SYNC_CLASS:
		case NVA0_DISP_SYNC_CLASS:
		case NVA3_DISP_SYNC_CLASS:
		case NV50_DISP_OVLY_CLASS:
		case NV84_DISP_OVLY_CLASS:
		case NV94_DISP_OVLY_CLASS:
		case NVA0_DISP_OVLY_CLASS:
		case NVA3_DISP_OVLY_CLASS:
			break;
		default:
			return -EINVAL;
		}
	}

	if (!(dmaobj->conf0 & NV50_DMA_CONF0_ENABLE)) {
		if (dmaobj->target == NV_MEM_TARGET_VM) {
			dmaobj->conf0  = NV50_DMA_CONF0_PRIV_VM;
			dmaobj->conf0 |= NV50_DMA_CONF0_PART_VM;
			dmaobj->conf0 |= NV50_DMA_CONF0_COMP_VM;
			dmaobj->conf0 |= NV50_DMA_CONF0_TYPE_VM;
		} else {
			dmaobj->conf0  = NV50_DMA_CONF0_PRIV_US;
			dmaobj->conf0 |= NV50_DMA_CONF0_PART_256;
			dmaobj->conf0 |= NV50_DMA_CONF0_COMP_NONE;
			dmaobj->conf0 |= NV50_DMA_CONF0_TYPE_LINEAR;
		}
	}

	flags0 |= (dmaobj->conf0 & NV50_DMA_CONF0_COMP) << 22;
	flags0 |= (dmaobj->conf0 & NV50_DMA_CONF0_TYPE) << 22;
	flags0 |= (dmaobj->conf0 & NV50_DMA_CONF0_PRIV);
	flags5 |= (dmaobj->conf0 & NV50_DMA_CONF0_PART);

	switch (dmaobj->target) {
	case NV_MEM_TARGET_VM:
		flags0 |= 0x00000000;
		break;
	case NV_MEM_TARGET_VRAM:
		flags0 |= 0x00010000;
		break;
	case NV_MEM_TARGET_PCI:
		flags0 |= 0x00020000;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		flags0 |= 0x00030000;
		break;
	default:
		return -EINVAL;
	}

	switch (dmaobj->access) {
	case NV_MEM_ACCESS_VM:
		break;
	case NV_MEM_ACCESS_RO:
		flags0 |= 0x00040000;
		break;
	case NV_MEM_ACCESS_WO:
	case NV_MEM_ACCESS_RW:
		flags0 |= 0x00080000;
		break;
	}

	ret = nouveau_gpuobj_new(parent, parent, 24, 32, 0, pgpuobj);
	if (ret == 0) {
		nv_wo32(*pgpuobj, 0x00, flags0);
		nv_wo32(*pgpuobj, 0x04, lower_32_bits(dmaobj->limit));
		nv_wo32(*pgpuobj, 0x08, lower_32_bits(dmaobj->start));
		nv_wo32(*pgpuobj, 0x0c, upper_32_bits(dmaobj->limit) << 24 |
					upper_32_bits(dmaobj->start));
		nv_wo32(*pgpuobj, 0x10, 0x00000000);
		nv_wo32(*pgpuobj, 0x14, flags5);
	}

	return ret;
}

static int
nv50_dmaeng_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nv50_dmaeng_priv *priv;
	int ret;

	ret = nouveau_dmaeng_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nouveau_dmaobj_sclass;
	priv->base.bind = nv50_dmaobj_bind;
	return 0;
}

struct nouveau_oclass
nv50_dmaeng_oclass = {
	.handle = NV_ENGINE(DMAOBJ, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_dmaeng_ctor,
		.dtor = _nouveau_dmaeng_dtor,
		.init = _nouveau_dmaeng_init,
		.fini = _nouveau_dmaeng_fini,
	},
};
