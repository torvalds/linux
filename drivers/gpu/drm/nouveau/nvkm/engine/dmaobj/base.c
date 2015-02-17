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
#include <core/device.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static int
nvkm_dmaobj_bind(struct nvkm_dmaobj *dmaobj, struct nvkm_object *parent,
		 struct nvkm_gpuobj **pgpuobj)
{
	const struct nvkm_dmaeng_impl *impl = (void *)
		nv_oclass(nv_object(dmaobj)->engine);
	int ret = 0;

	if (nv_object(dmaobj) == parent) { /* ctor bind */
		if (nv_mclass(parent->parent) == NV_DEVICE) {
			/* delayed, or no, binding */
			return 0;
		}
		ret = impl->bind(dmaobj, parent, pgpuobj);
		if (ret == 0)
			nvkm_object_ref(NULL, &parent);
		return ret;
	}

	return impl->bind(dmaobj, parent, pgpuobj);
}

int
nvkm_dmaobj_create_(struct nvkm_object *parent,
		    struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void **pdata, u32 *psize,
		    int length, void **pobject)
{
	union {
		struct nv_dma_v0 v0;
	} *args = *pdata;
	struct nvkm_instmem *instmem = nvkm_instmem(parent);
	struct nvkm_client *client = nvkm_client(parent);
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_fb *pfb = nvkm_fb(parent);
	struct nvkm_dmaobj *dmaobj;
	void *data = *pdata;
	u32 size = *psize;
	int ret;

	ret = nvkm_object_create_(parent, engine, oclass, 0, length, pobject);
	dmaobj = *pobject;
	if (ret)
		return ret;

	nv_ioctl(parent, "create dma size %d\n", *psize);
	if (nvif_unpack(args->v0, 0, 0, true)) {
		nv_ioctl(parent, "create dma vers %d target %d access %d "
				 "start %016llx limit %016llx\n",
			 args->v0.version, args->v0.target, args->v0.access,
			 args->v0.start, args->v0.limit);
		dmaobj->target = args->v0.target;
		dmaobj->access = args->v0.access;
		dmaobj->start  = args->v0.start;
		dmaobj->limit  = args->v0.limit;
	} else
		return ret;

	*pdata = data;
	*psize = size;

	if (dmaobj->start > dmaobj->limit)
		return -EINVAL;

	switch (dmaobj->target) {
	case NV_DMA_V0_TARGET_VM:
		dmaobj->target = NV_MEM_TARGET_VM;
		break;
	case NV_DMA_V0_TARGET_VRAM:
		if (!client->super) {
			if (dmaobj->limit >= pfb->ram->size - instmem->reserved)
				return -EACCES;
			if (device->card_type >= NV_50)
				return -EACCES;
		}
		dmaobj->target = NV_MEM_TARGET_VRAM;
		break;
	case NV_DMA_V0_TARGET_PCI:
		if (!client->super)
			return -EACCES;
		dmaobj->target = NV_MEM_TARGET_PCI;
		break;
	case NV_DMA_V0_TARGET_PCI_US:
	case NV_DMA_V0_TARGET_AGP:
		if (!client->super)
			return -EACCES;
		dmaobj->target = NV_MEM_TARGET_PCI_NOSNOOP;
		break;
	default:
		return -EINVAL;
	}

	switch (dmaobj->access) {
	case NV_DMA_V0_ACCESS_VM:
		dmaobj->access = NV_MEM_ACCESS_VM;
		break;
	case NV_DMA_V0_ACCESS_RD:
		dmaobj->access = NV_MEM_ACCESS_RO;
		break;
	case NV_DMA_V0_ACCESS_WR:
		dmaobj->access = NV_MEM_ACCESS_WO;
		break;
	case NV_DMA_V0_ACCESS_RDWR:
		dmaobj->access = NV_MEM_ACCESS_RW;
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

int
_nvkm_dmaeng_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, void *data, u32 size,
		  struct nvkm_object **pobject)
{
	const struct nvkm_dmaeng_impl *impl = (void *)oclass;
	struct nvkm_dmaeng *dmaeng;
	int ret;

	ret = nvkm_engine_create(parent, engine, oclass, true, "DMAOBJ",
				 "dmaobj", &dmaeng);
	*pobject = nv_object(dmaeng);
	if (ret)
		return ret;

	nv_engine(dmaeng)->sclass = impl->sclass;
	dmaeng->bind = nvkm_dmaobj_bind;
	return 0;
}
