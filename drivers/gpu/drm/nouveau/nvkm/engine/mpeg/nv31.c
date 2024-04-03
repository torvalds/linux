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
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

#include <nvif/class.h>

/*******************************************************************************
 * MPEG object classes
 ******************************************************************************/

static int
nv31_mpeg_object_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		      int align, struct nvkm_gpuobj **pgpuobj)
{
	int ret = nvkm_gpuobj_new(object->engine->subdev.device, 16, align,
				  false, parent, pgpuobj);
	if (ret == 0) {
		nvkm_kmap(*pgpuobj);
		nvkm_wo32(*pgpuobj, 0x00, object->oclass);
		nvkm_wo32(*pgpuobj, 0x04, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x08, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x0c, 0x00000000);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

const struct nvkm_object_func
nv31_mpeg_object = {
	.bind = nv31_mpeg_object_bind,
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static void *
nv31_mpeg_chan_dtor(struct nvkm_object *object)
{
	struct nv31_mpeg_chan *chan = nv31_mpeg_chan(object);
	struct nv31_mpeg *mpeg = chan->mpeg;
	unsigned long flags;

	spin_lock_irqsave(&mpeg->engine.lock, flags);
	if (mpeg->chan == chan)
		mpeg->chan = NULL;
	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
	return chan;
}

static const struct nvkm_object_func
nv31_mpeg_chan = {
	.dtor = nv31_mpeg_chan_dtor,
};

int
nv31_mpeg_chan_new(struct nvkm_chan *fifoch, const struct nvkm_oclass *oclass,
		   struct nvkm_object **pobject)
{
	struct nv31_mpeg *mpeg = nv31_mpeg(oclass->engine);
	struct nv31_mpeg_chan *chan;
	unsigned long flags;
	int ret = -EBUSY;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv31_mpeg_chan, oclass, &chan->object);
	chan->mpeg = mpeg;
	chan->fifo = fifoch;
	*pobject = &chan->object;

	spin_lock_irqsave(&mpeg->engine.lock, flags);
	if (!mpeg->chan) {
		mpeg->chan = chan;
		ret = 0;
	}
	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
	return ret;
}

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

void
nv31_mpeg_tile(struct nvkm_engine *engine, int i, struct nvkm_fb_tile *tile)
{
	struct nv31_mpeg *mpeg = nv31_mpeg(engine);
	struct nvkm_device *device = mpeg->engine.subdev.device;

	nvkm_wr32(device, 0x00b008 + (i * 0x10), tile->pitch);
	nvkm_wr32(device, 0x00b004 + (i * 0x10), tile->limit);
	nvkm_wr32(device, 0x00b000 + (i * 0x10), tile->addr);
}

static bool
nv31_mpeg_mthd_dma(struct nvkm_device *device, u32 mthd, u32 data)
{
	struct nv31_mpeg *mpeg = nv31_mpeg(device->mpeg);
	struct nvkm_subdev *subdev = &mpeg->engine.subdev;
	u32 inst = data << 4;
	u32 dma0 = nvkm_rd32(device, 0x700000 + inst);
	u32 dma1 = nvkm_rd32(device, 0x700004 + inst);
	u32 dma2 = nvkm_rd32(device, 0x700008 + inst);
	u32 base = (dma2 & 0xfffff000) | (dma0 >> 20);
	u32 size = dma1 + 1;

	/* only allow linear DMA objects */
	if (!(dma0 & 0x00002000)) {
		nvkm_error(subdev, "inst %08x dma0 %08x dma1 %08x dma2 %08x\n",
			   inst, dma0, dma1, dma2);
		return false;
	}

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
	struct nvkm_device *device = mpeg->engine.subdev.device;
	switch (mthd) {
	case 0x190:
	case 0x1a0:
	case 0x1b0:
		return mpeg->func->mthd_dma(device, mthd, data);
	default:
		break;
	}
	return false;
}

static void
nv31_mpeg_intr(struct nvkm_engine *engine)
{
	struct nv31_mpeg *mpeg = nv31_mpeg(engine);
	struct nvkm_subdev *subdev = &mpeg->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, 0x00b100);
	u32 type = nvkm_rd32(device, 0x00b230);
	u32 mthd = nvkm_rd32(device, 0x00b234);
	u32 data = nvkm_rd32(device, 0x00b238);
	u32 show = stat;
	unsigned long flags;

	spin_lock_irqsave(&mpeg->engine.lock, flags);

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nvkm_mask(device, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			if (nv31_mpeg_mthd(mpeg, mthd, data))
				show &= ~0x01000000;
		}
	}

	nvkm_wr32(device, 0x00b100, stat);
	nvkm_wr32(device, 0x00b230, 0x00000001);

	if (show) {
		nvkm_error(subdev, "ch %d [%s] %08x %08x %08x %08x\n",
			   mpeg->chan ? mpeg->chan->fifo->id : -1,
			   mpeg->chan ? mpeg->chan->fifo->name :
			   "unknown", stat, type, mthd, data);
	}

	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
}

int
nv31_mpeg_init(struct nvkm_engine *mpeg)
{
	struct nvkm_subdev *subdev = &mpeg->subdev;
	struct nvkm_device *device = subdev->device;

	/* VPE init */
	nvkm_wr32(device, 0x00b0e0, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */
	nvkm_wr32(device, 0x00b0e8, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */

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

static void *
nv31_mpeg_dtor(struct nvkm_engine *engine)
{
	return nv31_mpeg(engine);
}

static const struct nvkm_engine_func
nv31_mpeg_ = {
	.dtor = nv31_mpeg_dtor,
	.init = nv31_mpeg_init,
	.intr = nv31_mpeg_intr,
	.tile = nv31_mpeg_tile,
	.fifo.cclass = nv31_mpeg_chan_new,
	.sclass = {
		{ -1, -1, NV31_MPEG, &nv31_mpeg_object },
		{}
	}
};

int
nv31_mpeg_new_(const struct nv31_mpeg_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, struct nvkm_engine **pmpeg)
{
	struct nv31_mpeg *mpeg;

	if (!(mpeg = kzalloc(sizeof(*mpeg), GFP_KERNEL)))
		return -ENOMEM;
	mpeg->func = func;
	*pmpeg = &mpeg->engine;

	return nvkm_engine_ctor(&nv31_mpeg_, device, type, inst, true, &mpeg->engine);
}

static const struct nv31_mpeg_func
nv31_mpeg = {
	.mthd_dma = nv31_mpeg_mthd_dma,
};

int
nv31_mpeg_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_engine **pmpeg)
{
	return nv31_mpeg_new_(&nv31_mpeg, device, type, inst, pmpeg);
}
