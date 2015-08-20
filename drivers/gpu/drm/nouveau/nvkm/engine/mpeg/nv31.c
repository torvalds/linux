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
#include "nv31.h"

#include <core/client.h>
#include <subdev/fb.h>
#include <subdev/timer.h>

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static int
nv31_mpeg_object_ctor(struct nvkm_object *parent,
		      struct nvkm_object *engine,
		      struct nvkm_oclass *oclass, void *data, u32 size,
		      struct nvkm_object **pobject)
{
	struct nvkm_gpuobj *obj;
	int ret;

	ret = nvkm_gpuobj_create(parent, engine, oclass, 0, parent,
				 20, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nvkm_kmap(obj);
	nvkm_wo32(obj, 0x00, nv_mclass(obj));
	nvkm_wo32(obj, 0x04, 0x00000000);
	nvkm_wo32(obj, 0x08, 0x00000000);
	nvkm_wo32(obj, 0x0c, 0x00000000);
	nvkm_done(obj);
	return 0;
}

static bool
nv31_mpeg_mthd_dma(struct nvkm_device *device, u32 mthd, u32 data)
{
	u32 inst = data << 4;
	u32 dma0 = nvkm_rd32(device, 0x700000 + inst);
	u32 dma1 = nvkm_rd32(device, 0x700004 + inst);
	u32 dma2 = nvkm_rd32(device, 0x700008 + inst);
	u32 base = (dma2 & 0xfffff000) | (dma0 >> 20);
	u32 size = dma1 + 1;

	/* only allow linear DMA objects */
	if (!(dma0 & 0x00002000))
		return false;

	if (mthd == 0x0190) {
		/* DMA_CMD */
		nvkm_mask(device, 0x00b300, 0x00010000,
				  (dma0 & 0x00030000) ? 0x00010000 : 0);
		nvkm_wr32(device, 0x00b334, base);
		nvkm_wr32(device, 0x00b324, size);
	} else
	if (mthd == 0x01a0) {
		/* DMA_DATA */
		nvkm_mask(device, 0x00b300, 0x00020000,
				  (dma0 & 0x00030000) ? 0x00020000 : 0);
		nvkm_wr32(device, 0x00b360, base);
		nvkm_wr32(device, 0x00b364, size);
	} else {
		/* DMA_IMAGE, VRAM only */
		if (dma0 & 0x00030000)
			return false;

		nvkm_wr32(device, 0x00b370, base);
		nvkm_wr32(device, 0x00b374, size);
	}

	return true;
}

static bool
nv31_mpeg_mthd(struct nv31_mpeg *mpeg, u32 mthd, u32 data)
{
	struct nvkm_device *device = mpeg->base.engine.subdev.device;
	switch (mthd) {
	case 0x190:
	case 0x1a0:
	case 0x1b0:
		return mpeg->mthd_dma(device, mthd, data);
	default:
		break;
	}
	return false;
}

struct nvkm_ofuncs
nv31_mpeg_ofuncs = {
	.ctor = nv31_mpeg_object_ctor,
	.dtor = _nvkm_gpuobj_dtor,
	.init = _nvkm_gpuobj_init,
	.fini = _nvkm_gpuobj_fini,
	.rd32 = _nvkm_gpuobj_rd32,
	.wr32 = _nvkm_gpuobj_wr32,
};

struct nvkm_oclass
nv31_mpeg_sclass[] = {
	{ 0x3174, &nv31_mpeg_ofuncs },
	{}
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static int
nv31_mpeg_context_ctor(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nv31_mpeg *mpeg = (void *)engine;
	struct nv31_mpeg_chan *chan;
	unsigned long flags;
	int ret;

	ret = nvkm_object_create(parent, engine, oclass, 0, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&nv_engine(mpeg)->lock, flags);
	if (mpeg->chan) {
		spin_unlock_irqrestore(&nv_engine(mpeg)->lock, flags);
		nvkm_object_destroy(&chan->base);
		*pobject = NULL;
		return -EBUSY;
	}
	chan->fifo = nvkm_fifo_chan(parent);
	mpeg->chan = chan;
	spin_unlock_irqrestore(&nv_engine(mpeg)->lock, flags);
	return 0;
}

static void
nv31_mpeg_context_dtor(struct nvkm_object *object)
{
	struct nv31_mpeg *mpeg = (void *)object->engine;
	struct nv31_mpeg_chan *chan = (void *)object;
	unsigned long flags;

	spin_lock_irqsave(&nv_engine(mpeg)->lock, flags);
	mpeg->chan = NULL;
	spin_unlock_irqrestore(&nv_engine(mpeg)->lock, flags);
	nvkm_object_destroy(&chan->base);
}

struct nvkm_oclass
nv31_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x31),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv31_mpeg_context_ctor,
		.dtor = nv31_mpeg_context_dtor,
		.init = _nvkm_object_init,
		.fini = _nvkm_object_fini,
	},
};

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

void
nv31_mpeg_tile_prog(struct nvkm_engine *engine, int i)
{
	struct nv31_mpeg *mpeg = (void *)engine;
	struct nvkm_device *device = mpeg->base.engine.subdev.device;
	struct nvkm_fb_tile *tile = &device->fb->tile.region[i];

	nvkm_wr32(device, 0x00b008 + (i * 0x10), tile->pitch);
	nvkm_wr32(device, 0x00b004 + (i * 0x10), tile->limit);
	nvkm_wr32(device, 0x00b000 + (i * 0x10), tile->addr);
}

void
nv31_mpeg_intr(struct nvkm_subdev *subdev)
{
	struct nv31_mpeg *mpeg = (void *)subdev;
	struct nvkm_device *device = mpeg->base.engine.subdev.device;
	u32 stat = nvkm_rd32(device, 0x00b100);
	u32 type = nvkm_rd32(device, 0x00b230);
	u32 mthd = nvkm_rd32(device, 0x00b234);
	u32 data = nvkm_rd32(device, 0x00b238);
	u32 show = stat;
	unsigned long flags;

	spin_lock_irqsave(&mpeg->base.engine.lock, flags);

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nvkm_mask(device, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			if (!nv31_mpeg_mthd(mpeg, mthd, data))
				show &= ~0x01000000;
		}
	}

	nvkm_wr32(device, 0x00b100, stat);
	nvkm_wr32(device, 0x00b230, 0x00000001);

	if (show) {
		nvkm_error(subdev, "ch %d [%s] %08x %08x %08x %08x\n",
			   mpeg->chan ? mpeg->chan->fifo->chid : -1,
			   mpeg->chan ? mpeg->chan->fifo->object.client->name :
			   "unknown", stat, type, mthd, data);
	}

	spin_unlock_irqrestore(&mpeg->base.engine.lock, flags);
}

static int
nv31_mpeg_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv31_mpeg *mpeg;
	int ret;

	ret = nvkm_mpeg_create(parent, engine, oclass, &mpeg);
	*pobject = nv_object(mpeg);
	if (ret)
		return ret;

	mpeg->mthd_dma = nv31_mpeg_mthd_dma;
	nv_subdev(mpeg)->unit = 0x00000002;
	nv_subdev(mpeg)->intr = nv31_mpeg_intr;
	nv_engine(mpeg)->cclass = &nv31_mpeg_cclass;
	nv_engine(mpeg)->sclass = nv31_mpeg_sclass;
	nv_engine(mpeg)->tile_prog = nv31_mpeg_tile_prog;
	return 0;
}

int
nv31_mpeg_init(struct nvkm_object *object)
{
	struct nvkm_engine *engine = nv_engine(object);
	struct nv31_mpeg *mpeg = (void *)object;
	struct nvkm_subdev *subdev = &mpeg->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_fb *fb = device->fb;
	int ret, i;

	ret = nvkm_mpeg_init(&mpeg->base);
	if (ret)
		return ret;

	/* VPE init */
	nvkm_wr32(device, 0x00b0e0, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */
	nvkm_wr32(device, 0x00b0e8, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */

	for (i = 0; i < fb->tile.regions; i++)
		engine->tile_prog(engine, i);

	/* PMPEG init */
	nvkm_wr32(device, 0x00b32c, 0x00000000);
	nvkm_wr32(device, 0x00b314, 0x00000100);
	nvkm_wr32(device, 0x00b220, 0x00000031);
	nvkm_wr32(device, 0x00b300, 0x02001ec1);
	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000001);

	nvkm_wr32(device, 0x00b100, 0xffffffff);
	nvkm_wr32(device, 0x00b140, 0xffffffff);

	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x00b200) & 0x00000001))
			break;
	) < 0) {
		nvkm_error(subdev, "timeout %08x\n",
			   nvkm_rd32(device, 0x00b200));
		return -EBUSY;
	}

	return 0;
}

struct nvkm_oclass
nv31_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x31),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv31_mpeg_ctor,
		.dtor = _nvkm_mpeg_dtor,
		.init = nv31_mpeg_init,
		.fini = _nvkm_mpeg_fini,
	},
};
