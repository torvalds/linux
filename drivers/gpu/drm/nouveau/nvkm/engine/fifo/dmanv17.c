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
#include <core/gpuobj.h>
#include <subdev/instmem.h>

#include <nvif/class.h>
#include <nvif/cl006b.h>
#include <nvif/unpack.h>

static int
nv17_fifo_dma_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
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
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create channel dma size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
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
				  BIT(NV04_FIFO_ENGN_SW) |
				  BIT(NV04_FIFO_ENGN_GR) |
				  BIT(NV04_FIFO_ENGN_MPEG) | /* NV31- */
				  BIT(NV04_FIFO_ENGN_DMA),
				  0, 0x800000, 0x10000, oclass, &chan->base);
	chan->fifo = fifo;
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;
	chan->ramfc = chan->base.chid * 64;

	nvkm_kmap(imem->ramfc);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x00, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x04, args->v0.offset);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x0c, chan->base.push->addr >> 4);
	nvkm_wo32(imem->ramfc, chan->ramfc + 0x14,
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
nv17_fifo_dma_oclass = {
	.base.oclass = NV17_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv17_fifo_dma_new,
};
