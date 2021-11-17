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
#include "user.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>

#include <nvif/cl0002.h>
#include <nvif/unpack.h>

static const struct nvkm_object_func nvkm_dmaobj_func;
struct nvkm_dmaobj *
nvkm_dmaobj_search(struct nvkm_client *client, u64 handle)
{
	struct nvkm_object *object;

	object = nvkm_object_search(client, handle, &nvkm_dmaobj_func);
	if (IS_ERR(object))
		return (void *)object;

	return nvkm_dmaobj(object);
}

static int
nvkm_dmaobj_bind(struct nvkm_object *base, struct nvkm_gpuobj *gpuobj,
		 int align, struct nvkm_gpuobj **pgpuobj)
{
	struct nvkm_dmaobj *dmaobj = nvkm_dmaobj(base);
	return dmaobj->func->bind(dmaobj, gpuobj, align, pgpuobj);
}

static void *
nvkm_dmaobj_dtor(struct nvkm_object *base)
{
	return nvkm_dmaobj(base);
}

static const struct nvkm_object_func
nvkm_dmaobj_func = {
	.dtor = nvkm_dmaobj_dtor,
	.bind = nvkm_dmaobj_bind,
};

int
nvkm_dmaobj_ctor(const struct nvkm_dmaobj_func *func, struct nvkm_dma *dma,
		 const struct nvkm_oclass *oclass, void **pdata, u32 *psize,
		 struct nvkm_dmaobj *dmaobj)
{
	union {
		struct nv_dma_v0 v0;
	} *args = *pdata;
	struct nvkm_object *parent = oclass->parent;
	void *data = *pdata;
	u32 size = *psize;
	int ret = -ENOSYS;

	nvkm_object_ctor(&nvkm_dmaobj_func, oclass, &dmaobj->object);
	dmaobj->func = func;
	dmaobj->dma = dma;

	nvif_ioctl(parent, "create dma size %d\n", *psize);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(parent, "create dma vers %d target %d access %d "
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
		dmaobj->target = NV_MEM_TARGET_VRAM;
		break;
	case NV_DMA_V0_TARGET_PCI:
		dmaobj->target = NV_MEM_TARGET_PCI;
		break;
	case NV_DMA_V0_TARGET_PCI_US:
	case NV_DMA_V0_TARGET_AGP:
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
