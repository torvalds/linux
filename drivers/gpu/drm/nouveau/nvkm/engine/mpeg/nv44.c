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
#define nv44_mpeg(p) container_of((p), struct nv44_mpeg, engine)
#include "priv.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <engine/fifo.h>

#include <nvif/class.h>

struct nv44_mpeg {
	struct nvkm_engine engine;
	struct list_head chan;
};

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/
#define nv44_mpeg_chan(p) container_of((p), struct nv44_mpeg_chan, object)

struct nv44_mpeg_chan {
	struct nvkm_object object;
	struct nv44_mpeg *mpeg;
	struct nvkm_fifo_chan *fifo;
	struct list_head head;
	u32 inst;
};

static int
nv44_mpeg_chan_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		    int align, struct nvkm_gpuobj **pgpuobj)
{
	struct nv44_mpeg_chan *chan = nv44_mpeg_chan(object);
	int ret = nvkm_gpuobj_new(chan->object.engine->subdev.device, 264 * 4,
				  align, true, parent, pgpuobj);
	if (ret == 0) {
		chan->inst = (*pgpuobj)->addr;
		nvkm_kmap(*pgpuobj);
		nvkm_wo32(*pgpuobj, 0x78, 0x02001ec1);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

static int
nv44_mpeg_chan_fini(struct nvkm_object *object, bool suspend)
{

	struct nv44_mpeg_chan *chan = nv44_mpeg_chan(object);
	struct nv44_mpeg *mpeg = chan->mpeg;
	struct nvkm_device *device = mpeg->engine.subdev.device;
	u32 inst = 0x80000000 | (chan->inst >> 4);

	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000000);
	if (nvkm_rd32(device, 0x00b318) == inst)
		nvkm_mask(device, 0x00b318, 0x80000000, 0x00000000);
	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000001);
	return 0;
}

static void *
nv44_mpeg_chan_dtor(struct nvkm_object *object)
{
	struct nv44_mpeg_chan *chan = nv44_mpeg_chan(object);
	struct nv44_mpeg *mpeg = chan->mpeg;
	unsigned long flags;
	spin_lock_irqsave(&mpeg->engine.lock, flags);
	list_del(&chan->head);
	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
	return chan;
}

static const struct nvkm_object_func
nv44_mpeg_chan = {
	.dtor = nv44_mpeg_chan_dtor,
	.fini = nv44_mpeg_chan_fini,
	.bind = nv44_mpeg_chan_bind,
};

static int
nv44_mpeg_chan_new(struct nvkm_fifo_chan *fifoch,
		   const struct nvkm_oclass *oclass,
		   struct nvkm_object **pobject)
{
	struct nv44_mpeg *mpeg = nv44_mpeg(oclass->engine);
	struct nv44_mpeg_chan *chan;
	unsigned long flags;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv44_mpeg_chan, oclass, &chan->object);
	chan->mpeg = mpeg;
	chan->fifo = fifoch;
	*pobject = &chan->object;

	spin_lock_irqsave(&mpeg->engine.lock, flags);
	list_add(&chan->head, &mpeg->chan);
	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
	return 0;
}

/*******************************************************************************
 * PMPEG engine/subdev functions
 ******************************************************************************/

static bool
nv44_mpeg_mthd(struct nvkm_device *device, u32 mthd, u32 data)
{
	switch (mthd) {
	case 0x190:
	case 0x1a0:
	case 0x1b0:
		return nv40_mpeg_mthd_dma(device, mthd, data);
	default:
		break;
	}
	return false;
}

static void
nv44_mpeg_intr(struct nvkm_engine *engine)
{
	struct nv44_mpeg *mpeg = nv44_mpeg(engine);
	struct nvkm_subdev *subdev = &mpeg->engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nv44_mpeg_chan *temp, *chan = NULL;
	unsigned long flags;
	u32 inst = nvkm_rd32(device, 0x00b318) & 0x000fffff;
	u32 stat = nvkm_rd32(device, 0x00b100);
	u32 type = nvkm_rd32(device, 0x00b230);
	u32 mthd = nvkm_rd32(device, 0x00b234);
	u32 data = nvkm_rd32(device, 0x00b238);
	u32 show = stat;

	spin_lock_irqsave(&mpeg->engine.lock, flags);
	list_for_each_entry(temp, &mpeg->chan, head) {
		if (temp->inst >> 4 == inst) {
			chan = temp;
			list_del(&chan->head);
			list_add(&chan->head, &mpeg->chan);
			break;
		}
	}

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nvkm_mask(device, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			if (nv44_mpeg_mthd(subdev->device, mthd, data))
				show &= ~0x01000000;
		}
	}

	nvkm_wr32(device, 0x00b100, stat);
	nvkm_wr32(device, 0x00b230, 0x00000001);

	if (show) {
		nvkm_error(subdev, "ch %d [%08x %s] %08x %08x %08x %08x\n",
			   chan ? chan->fifo->chid : -1, inst << 4,
			   chan ? chan->object.client->name : "unknown",
			   stat, type, mthd, data);
	}

	spin_unlock_irqrestore(&mpeg->engine.lock, flags);
}

static const struct nvkm_engine_func
nv44_mpeg = {
	.init = nv31_mpeg_init,
	.intr = nv44_mpeg_intr,
	.tile = nv31_mpeg_tile,
	.fifo.cclass = nv44_mpeg_chan_new,
	.sclass = {
		{ -1, -1, NV31_MPEG, &nv31_mpeg_object },
		{}
	}
};

int
nv44_mpeg_new(struct nvkm_device *device, int index, struct nvkm_engine **pmpeg)
{
	struct nv44_mpeg *mpeg;

	if (!(mpeg = kzalloc(sizeof(*mpeg), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&mpeg->chan);
	*pmpeg = &mpeg->engine;

	return nvkm_engine_ctor(&nv44_mpeg, device, index, true, &mpeg->engine);
}
