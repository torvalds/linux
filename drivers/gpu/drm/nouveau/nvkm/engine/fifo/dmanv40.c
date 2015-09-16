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
#include "channv04.h"
#include "regsnv04.h"

#include <core/client.h>
#include <core/ramht.h>
#include <subdev/instmem.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static bool
nv40_fifo_dma_engine(struct nvkm_engine *engine, u32 *reg, u32 *ctx)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW:
		return false;
	case NVKM_ENGINE_GR:
		*reg = 0x0032e0;
		*ctx = 0x38;
		return true;
	case NVKM_ENGINE_MPEG:
		*reg = 0x00330c;
		*ctx = 0x54;
		return true;
	default:
		WARN_ON(1);
		return false;
	}
}

static int
nv40_fifo_dma_engine_fini(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine, bool suspend)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nv04_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	unsigned long flags;
	u32 reg, ctx;
	int chid;

	if (!nv40_fifo_dma_engine(engine, &reg, &ctx))
		return 0;

	spin_lock_irqsave(&fifo->base.lock, flags);
	nvkm_mask(device, 0x002500, 0x00000001, 0x00000000);

	chid = nvkm_rd32(device, 0x003204) & (fifo->base.nr - 1);
	if (chid == chan->base.chid)
		nvkm_wr32(device, reg, 0x00000000);
	nvkm_kmap(imem->ramfc);
	nvkm_wo32(imem->ramfc, chan->ramfc + ctx, 0x00000000);
	nvkm_done(imem->ramfc);

	nvkm_mask(device, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&fifo->base.lock, flags);
	return 0;
}

static int
nv40_fifo_dma_engine_init(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nv04_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	unsigned long flags;
	u32 inst, reg, ctx;
	int chid;

	if (!nv40_fifo_dma_engine(engine, &reg, &ctx))
		return 0;
	inst = chan->engn[engine->subdev.index]->addr >> 4;

	spin_lock_irqsave(&fifo->base.lock, flags);
	nvkm_mask(device, 0x002500, 0x00000001, 0x00000000);

	chid = nvkm_rd32(device, 0x003204) & (fifo->base.nr - 1);
	if (chid == chan->base.chid)
		nvkm_wr32(device, reg, inst);
	nvkm_kmap(imem->ramfc);
	nvkm_wo32(imem->ramfc, chan->ramfc + ctx, inst);
	nvkm_done(imem->ramfc);

	nvkm_mask(device, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&fifo->base.lock, flags);
	return 0;
}

static void
nv40_fifo_dma_engine_dtor(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	nvkm_gpuobj_del(&chan->engn[engine->subdev.index]);
}

static int
nv40_fifo_dma_engine_ctor(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine,
			  struct nvkm_object *object)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	const int engn = engine->subdev.index;
	u32 reg, ctx;

	if (!nv40_fifo_dma_engine(engine, &reg, &ctx))
		return 0;

	return nvkm_object_bind(object, NULL, 0, &chan->engn[engn]);
}

static int
nv40_fifo_dma_object_ctor(struct nvkm_fifo_chan *base,
			  struct nvkm_object *object)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nvkm_instmem *imem = chan->fifo->base.engine.subdev.device->imem;
	u32 context = chan->base.chid << 23;
	u32 handle  = object->handle;
	int hash;

	switch (object->engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : context |= 0x00000000; break;
	case NVKM_ENGINE_GR    : context |= 0x00100000; break;
	case NVKM_ENGINE_MPEG  : context |= 0x00200000; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	mutex_lock(&chan->fifo->base.engine.subdev.mutex);
	hash = nvkm_ramht_insert(imem->ramht, object, chan->base.chid, 4,
				 handle, context);
	mutex_unlock(&chan->fifo->base.engine.subdev.mutex);
	return hash;
}

static const struct nvkm_fifo_chan_func
nv40_fifo_dma_func = {
	.dtor = nv04_fifo_dma_dtor,
	.init = nv04_fifo_dma_init,
	.fini = nv04_fifo_dma_fini,
	.engine_ctor = nv40_fifo_dma_engine_ctor,
	.engine_dtor = nv40_fifo_dma_engine_dtor,
	.engine_init = nv40_fifo_dma_engine_init,
	.engine_fini = nv40_fifo_dma_engine_fini,
	.object_ctor = nv40_fifo_dma_object_ctor,
	.object_dtor = nv04_fifo_dma_object_dtor,
};

static int
nv40_fifo_dma_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
		  void *data, u32 size, struct nvkm_object **pobject)
{
	struct nvkm_object *parent = oclass->parent;
	union {
		struct nv03_channel_dma_v0 v0;
	} *args = data;
	struct nv04_fifo *fifo = nv04_fifo(base);
	struct nv04_fifo_chan *chan = NULL;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	int ret;

	nvif_ioctl(parent, "create channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create channel dma vers %d pushbuf %llx "
				   "offset %08x\n", args->v0.version,
			   args->v0.pushbuf, args->v0.offset);
		if (!args->v0.pushbuf)
			return -EINVAL;
	} else
		return ret;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;

	ret = nvkm_fifo_chan_ctor(&nv40_fifo_dma_func, &fifo->base,
				  0x1000, 0x1000, false, 0, args->v0.pushbuf,
				  (1ULL << NVKM_ENGINE_DMAOBJ) |
				  (1ULL << NVKM_ENGINE_GR) |
				  (1ULL << NVKM_ENGINE_MPEG) |
				  (1ULL << NVKM_ENGINE_SW),
				  0, 0xc00000, 0x1000, oclass, &chan->base);
	chan->fifo = fifo;
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;
	chan->ramfc = chan->base.chid * 128;

	nvkm_kmap(imem->ramfc);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x00, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x04, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x0c, chan->base.push->addr >> 4);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x18, 0x30000000 |
			       NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			       NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
			       NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			       NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x3c, 0x0001ffff);
	nvkm_done(imem->ramfc);
	return 0;
}

const struct nvkm_fifo_chan_oclass
nv40_fifo_dma_oclass = {
	.base.oclass = NV40_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv40_fifo_dma_new,
};
