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
#include "dmacnv50.h"
#include "rootnv50.h"

#include <core/client.h>
#include <core/handle.h>
#include <core/ramht.h>
#include <subdev/fb.h>
#include <subdev/timer.h>
#include <engine/dma.h>

void
nv50_disp_dmac_object_detach(struct nvkm_object *parent, int cookie)
{
	struct nv50_disp_root *root = (void *)parent->parent;
	nvkm_ramht_remove(root->ramht, cookie);
}

int
nv50_disp_dmac_object_attach(struct nvkm_object *parent,
			     struct nvkm_object *object, u32 name)
{
	struct nv50_disp_root *root = (void *)parent->parent;
	struct nv50_disp_chan *chan = (void *)parent;
	u32 addr = nv_gpuobj(object)->node->offset;
	u32 chid = chan->chid;
	u32 data = (chid << 28) | (addr << 10) | chid;
	return nvkm_ramht_insert(root->ramht, NULL, chid, 0, name, data);
}

int
nv50_disp_dmac_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int chid = dmac->base.chid;

	/* deactivate channel */
	nvkm_mask(device, 0x610200 + (chid * 0x0010), 0x00001010, 0x00001000);
	nvkm_mask(device, 0x610200 + (chid * 0x0010), 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (chid * 0x10)) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini timeout, %08x\n", chid,
			   nvkm_rd32(device, 0x610200 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting and completion notifications */
	nvkm_mask(device, 0x610028, 0x00010001 << chid, 0x00000000 << chid);

	return nv50_disp_chan_fini(&dmac->base, suspend);
}

int
nv50_disp_dmac_init(struct nvkm_object *object)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_dmac *dmac = (void *)object;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int chid = dmac->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&dmac->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nvkm_mask(device, 0x610028, 0x00010000 << chid, 0x00010000 << chid);

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610204 + (chid * 0x0010), dmac->push);
	nvkm_wr32(device, 0x610208 + (chid * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61020c + (chid * 0x0010), chid);
	nvkm_mask(device, 0x610200 + (chid * 0x0010), 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000 + (chid * 0x1000), 0x00000000);
	nvkm_wr32(device, 0x610200 + (chid * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (chid * 0x10)) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init timeout, %08x\n", chid,
			   nvkm_rd32(device, 0x610200 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}

void
nv50_disp_dmac_dtor(struct nvkm_object *object)
{
	struct nv50_disp_dmac *dmac = (void *)object;
	nv50_disp_chan_destroy(&dmac->base);
}

int
nv50_disp_dmac_create_(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, u64 pushbuf, int head,
		       int length, void **pobject)
{
	struct nvkm_device *device = parent->engine->subdev.device;
	struct nvkm_client *client = nvkm_client(parent);
	struct nvkm_dmaobj *dmaobj;
	struct nv50_disp_dmac *dmac;
	int ret;

	ret = nv50_disp_chan_create_(parent, engine, oclass, head,
				     length, pobject);
	dmac = *pobject;
	if (ret)
		return ret;

	dmaobj = nvkm_dma_search(device->dma, client, pushbuf);
	if (!dmaobj)
		return -ENOENT;

	if (dmaobj->limit - dmaobj->start != 0xfff)
		return -EINVAL;

	switch (dmaobj->target) {
	case NV_MEM_TARGET_VRAM:
		dmac->push = 0x00000001 | dmaobj->start >> 8;
		break;
	case NV_MEM_TARGET_PCI_NOSNOOP:
		dmac->push = 0x00000003 | dmaobj->start >> 8;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
