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

void
nv04_fifo_dma_object_dtor(struct nvkm_fifo_chan *base, int cookie)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nvkm_instmem *imem = chan->fifo->base.engine.subdev.device->imem;

	mutex_lock(&chan->fifo->base.engine.subdev.mutex);
	nvkm_ramht_remove(imem->ramht, cookie);
	mutex_unlock(&chan->fifo->base.engine.subdev.mutex);
}

static int
nv04_fifo_dma_object_ctor(struct nvkm_fifo_chan *base,
			  struct nvkm_object *object)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nvkm_instmem *imem = chan->fifo->base.engine.subdev.device->imem;
	u32 context = 0x80000000 | chan->base.chid << 24;
	u32 handle  = object->handle;
	int hash;

	switch (object->engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : context |= 0x00000000; break;
	case NVKM_ENGINE_GR    : context |= 0x00010000; break;
	case NVKM_ENGINE_MPEG  : context |= 0x00020000; break;
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

void
nv04_fifo_dma_fini(struct nvkm_fifo_chan *base)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nv04_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_memory *fctx = device->imem->ramfc;
	const struct nv04_fifo_ramfc *c;
	unsigned long flags;
	u32 mask = fifo->base.nr - 1;
	u32 data = chan->ramfc;
	u32 chid;

	/* prevent fifo context switches */
	spin_lock_irqsave(&fifo->base.lock, flags);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 0);

	/* if this channel is active, replace it with a null context */
	chid = nvkm_rd32(device, NV03_PFIFO_CACHE1_PUSH1) & mask;
	if (chid == chan->base.chid) {
		nvkm_mask(device, NV04_PFIFO_CACHE1_DMA_PUSH, 0x00000001, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 0);
		nvkm_mask(device, NV04_PFIFO_CACHE1_PULL0, 0x00000001, 0);

		c = fifo->ramfc;
		do {
			u32 rm = ((1ULL << c->bits) - 1) << c->regs;
			u32 cm = ((1ULL << c->bits) - 1) << c->ctxs;
			u32 rv = (nvkm_rd32(device, c->regp) &  rm) >> c->regs;
			u32 cv = (nvkm_ro32(fctx, c->ctxp + data) & ~cm);
			nvkm_wo32(fctx, c->ctxp + data, cv | (rv << c->ctxs));
		} while ((++c)->bits);

		c = fifo->ramfc;
		do {
			nvkm_wr32(device, c->regp, 0x00000000);
		} while ((++c)->bits);

		nvkm_wr32(device, NV03_PFIFO_CACHE1_GET, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUT, 0);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, mask);
		nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
		nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	}

	/* restore normal operation, after disabling dma mode */
	nvkm_mask(device, NV04_PFIFO_MODE, 1 << chan->base.chid, 0);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

void
nv04_fifo_dma_init(struct nvkm_fifo_chan *base)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nv04_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 mask = 1 << chan->base.chid;
	unsigned long flags;
	spin_lock_irqsave(&fifo->base.lock, flags);
	nvkm_mask(device, NV04_PFIFO_MODE, mask, mask);
	spin_unlock_irqrestore(&fifo->base.lock, flags);
}

void *
nv04_fifo_dma_dtor(struct nvkm_fifo_chan *base)
{
	struct nv04_fifo_chan *chan = nv04_fifo_chan(base);
	struct nv04_fifo *fifo = chan->fifo;
	struct nvkm_instmem *imem = fifo->base.engine.subdev.device->imem;
	const struct nv04_fifo_ramfc *c = fifo->ramfc;

	nvkm_kmap(imem->ramfc);
	do {
		nvkm_wo32(imem->ramfc, chan->ramfc + c->ctxp, 0x00000000);
	} while ((++c)->bits);
	nvkm_done(imem->ramfc);
	return chan;
}

const struct nvkm_fifo_chan_func
nv04_fifo_dma_func = {
	.dtor = nv04_fifo_dma_dtor,
	.init = nv04_fifo_dma_init,
	.fini = nv04_fifo_dma_fini,
	.object_ctor = nv04_fifo_dma_object_ctor,
	.object_dtor = nv04_fifo_dma_object_dtor,
};

static int
nv04_fifo_dma_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
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

	ret = nvkm_fifo_chan_ctor(&nv04_fifo_dma_func, &fifo->base,
				  0x1000, 0x1000, false, 0, args->v0.pushbuf,
				  (1ULL << NVKM_ENGINE_DMAOBJ) |
				  (1ULL << NVKM_ENGINE_GR) |
				  (1ULL << NVKM_ENGINE_SW),
				  0, 0x800000, 0x10000, oclass, &chan->base);
	chan->fifo = fifo;
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;
	chan->ramfc = chan->base.chid * 32;

	nvkm_kmap(imem->ramfc);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x00, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x04, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x08, chan->base.push->addr >> 4);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x10,
			       NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
			       NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
			       NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
			       NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nvkm_done(imem->ramfc);
	return 0;
}

const struct nvkm_fifo_chan_oclass
nv04_fifo_dma_oclass = {
	.base.oclass = NV03_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv04_fifo_dma_new,
};
