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
#include <engine/mpeg.h>

#include <core/client.h>
#include <engine/fifo.h>

struct nv44_mpeg {
	struct nvkm_mpeg base;
	struct list_head chan;
};

struct nv44_mpeg_chan {
	struct nvkm_mpeg_chan base;
	struct nvkm_fifo_chan *fifo;
	u32 inst;
	struct list_head head;
};

bool nv40_mpeg_mthd_dma(struct nvkm_device *, u32, u32);

/*******************************************************************************
 * PMPEG context
 ******************************************************************************/

static void
nv44_mpeg_context_dtor(struct nvkm_object *object)
{
	struct nv44_mpeg_chan *chan = (void *)object;
	struct nv44_mpeg *mpeg = (void *)object->engine;
	unsigned long flags;
	spin_lock_irqsave(&mpeg->base.engine.lock, flags);
	list_del(&chan->head);
	spin_unlock_irqrestore(&mpeg->base.engine.lock, flags);
	nvkm_mpeg_context_destroy(&chan->base);
}

static int
nv44_mpeg_context_ctor(struct nvkm_object *parent,
		       struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nv44_mpeg *mpeg = (void *)engine;
	struct nv44_mpeg_chan *chan;
	unsigned long flags;
	int ret;

	ret = nvkm_mpeg_context_create(parent, engine, oclass, NULL, 264 * 4,
				       16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	spin_lock_irqsave(&mpeg->base.engine.lock, flags);
	chan->fifo = nvkm_fifo_chan(parent);
	chan->inst = chan->base.base.gpuobj.addr;
	list_add(&chan->head, &mpeg->chan);
	spin_unlock_irqrestore(&mpeg->base.engine.lock, flags);

	nvkm_kmap(&chan->base.base.gpuobj);
	nvkm_wo32(&chan->base.base.gpuobj, 0x78, 0x02001ec1);
	nvkm_done(&chan->base.base.gpuobj);
	return 0;
}

static int
nv44_mpeg_context_fini(struct nvkm_object *object, bool suspend)
{

	struct nvkm_mpeg *mpeg = (void *)object->engine;
	struct nv44_mpeg_chan *chan = (void *)object;
	struct nvkm_device *device = mpeg->engine.subdev.device;
	u32 inst = 0x80000000 | nv_gpuobj(chan)->addr >> 4;

	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000000);
	if (nvkm_rd32(device, 0x00b318) == inst)
		nvkm_mask(device, 0x00b318, 0x80000000, 0x00000000);
	nvkm_mask(device, 0x00b32c, 0x00000001, 0x00000001);
	return 0;
}

static struct nvkm_oclass
nv44_mpeg_cclass = {
	.handle = NV_ENGCTX(MPEG, 0x44),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_mpeg_context_ctor,
		.dtor = nv44_mpeg_context_dtor,
		.init = _nvkm_mpeg_context_init,
		.fini = nv44_mpeg_context_fini,
		.rd32 = _nvkm_mpeg_context_rd32,
		.wr32 = _nvkm_mpeg_context_wr32,
	},
};

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
nv44_mpeg_intr(struct nvkm_subdev *subdev)
{
	struct nv44_mpeg *mpeg = (void *)subdev;
	struct nv44_mpeg_chan *temp, *chan = NULL;
	struct nvkm_device *device = mpeg->base.engine.subdev.device;
	unsigned long flags;
	u32 inst = nvkm_rd32(device, 0x00b318) & 0x000fffff;
	u32 stat = nvkm_rd32(device, 0x00b100);
	u32 type = nvkm_rd32(device, 0x00b230);
	u32 mthd = nvkm_rd32(device, 0x00b234);
	u32 data = nvkm_rd32(device, 0x00b238);
	u32 show = stat;
	int chid = -1;

	spin_lock_irqsave(&mpeg->base.engine.lock, flags);
	list_for_each_entry(temp, &mpeg->chan, head) {
		if (temp->inst >> 4 == inst) {
			chan = temp;
			chid = chan->fifo->chid;
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
			if (!nv44_mpeg_mthd(subdev->device, mthd, data))
				show &= ~0x01000000;
		}
	}

	nvkm_wr32(device, 0x00b100, stat);
	nvkm_wr32(device, 0x00b230, 0x00000001);

	if (show) {
		nvkm_error(subdev, "ch %d [%08x %s] %08x %08x %08x %08x\n",
			   chid, inst << 4, nvkm_client_name(chan),
			   stat, type, mthd, data);
	}

	spin_unlock_irqrestore(&mpeg->base.engine.lock, flags);
}

static void
nv44_mpeg_me_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_mpeg *mpeg = (void *)subdev;
	struct nvkm_device *device = mpeg->engine.subdev.device;
	u32 stat;

	if ((stat = nvkm_rd32(device, 0x00b100)))
		nv44_mpeg_intr(subdev);

	if ((stat = nvkm_rd32(device, 0x00b800))) {
		nvkm_error(subdev, "PMSRCH %08x\n", stat);
		nvkm_wr32(device, 0x00b800, stat);
	}
}

static int
nv44_mpeg_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv44_mpeg *mpeg;
	int ret;

	ret = nvkm_mpeg_create(parent, engine, oclass, &mpeg);
	*pobject = nv_object(mpeg);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&mpeg->chan);

	nv_subdev(mpeg)->unit = 0x00000002;
	nv_subdev(mpeg)->intr = nv44_mpeg_me_intr;
	nv_engine(mpeg)->cclass = &nv44_mpeg_cclass;
	nv_engine(mpeg)->sclass = nv40_mpeg_sclass;
	nv_engine(mpeg)->tile_prog = nv31_mpeg_tile_prog;
	return 0;
}

struct nvkm_oclass
nv44_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x44),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv44_mpeg_ctor,
		.dtor = _nvkm_mpeg_dtor,
		.init = nv31_mpeg_init,
		.fini = _nvkm_mpeg_fini,
	},
};
