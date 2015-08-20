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
#include "channv50.h"

#include <core/client.h>
#include <core/ramht.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static int
g84_fifo_chan_ctor_dma(struct nvkm_object *parent, struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	union {
		struct nv50_channel_dma_v0 v0;
	} *args = data;
	struct nvkm_device *device = parent->engine->subdev.device;
	struct nv50_fifo_base *base = (void *)parent;
	struct nv50_fifo_chan *chan;
	int ret;

	nvif_ioctl(parent, "create channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create channel dma vers %d vm %llx "
				   "pushbuf %llx offset %016llx\n",
			   args->v0.version, args->v0.vm, args->v0.pushbuf,
			   args->v0.offset);
		if (args->v0.vm)
			return -ENOENT;
	} else
		return ret;

	ret = nvkm_fifo_channel_create(parent, engine, oclass, 0, 0xc00000,
				       0x2000, args->v0.pushbuf,
				       (1ULL << NVDEV_ENGINE_DMAOBJ) |
				       (1ULL << NVDEV_ENGINE_SW) |
				       (1ULL << NVDEV_ENGINE_GR) |
				       (1ULL << NVDEV_ENGINE_MPEG) |
				       (1ULL << NVDEV_ENGINE_ME) |
				       (1ULL << NVDEV_ENGINE_VP) |
				       (1ULL << NVDEV_ENGINE_CIPHER) |
				       (1ULL << NVDEV_ENGINE_SEC) |
				       (1ULL << NVDEV_ENGINE_BSP) |
				       (1ULL << NVDEV_ENGINE_MSVLD) |
				       (1ULL << NVDEV_ENGINE_MSPDEC) |
				       (1ULL << NVDEV_ENGINE_MSPPP) |
				       (1ULL << NVDEV_ENGINE_CE0) |
				       (1ULL << NVDEV_ENGINE_VIC), &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->base.inst = base->base.gpuobj.addr;
	args->v0.chid = chan->base.chid;

	ret = nvkm_ramht_new(device, 0x8000, 16, &base->base.gpuobj,
			     &chan->ramht);
	if (ret)
		return ret;

	nv_parent(chan)->context_attach = g84_fifo_context_attach;
	nv_parent(chan)->context_detach = g84_fifo_context_detach;
	nv_parent(chan)->object_attach = g84_fifo_object_attach;
	nv_parent(chan)->object_detach = nv50_fifo_object_detach;

	nvkm_kmap(base->ramfc);
	nvkm_wo32(base->ramfc, 0x08, lower_32_bits(args->v0.offset));
	nvkm_wo32(base->ramfc, 0x0c, upper_32_bits(args->v0.offset));
	nvkm_wo32(base->ramfc, 0x10, lower_32_bits(args->v0.offset));
	nvkm_wo32(base->ramfc, 0x14, upper_32_bits(args->v0.offset));
	nvkm_wo32(base->ramfc, 0x3c, 0x003f6078);
	nvkm_wo32(base->ramfc, 0x44, 0x01003fff);
	nvkm_wo32(base->ramfc, 0x48, chan->base.pushgpu->node->offset >> 4);
	nvkm_wo32(base->ramfc, 0x4c, 0xffffffff);
	nvkm_wo32(base->ramfc, 0x60, 0x7fffffff);
	nvkm_wo32(base->ramfc, 0x78, 0x00000000);
	nvkm_wo32(base->ramfc, 0x7c, 0x30000001);
	nvkm_wo32(base->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				     (4 << 24) /* SEARCH_FULL */ |
				     (chan->ramht->gpuobj->node->offset >> 4));
	nvkm_wo32(base->ramfc, 0x88, base->cache->addr >> 10);
	nvkm_wo32(base->ramfc, 0x98, nv_gpuobj(base)->addr >> 12);
	nvkm_done(base->ramfc);
	return 0;
}

static struct nvkm_ofuncs
g84_fifo_ofuncs_dma = {
	.ctor = g84_fifo_chan_ctor_dma,
	.dtor = nv50_fifo_chan_dtor,
	.init = g84_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.map  = _nvkm_fifo_channel_map,
	.rd32 = _nvkm_fifo_channel_rd32,
	.wr32 = _nvkm_fifo_channel_wr32,
	.ntfy = _nvkm_fifo_channel_ntfy
};

struct nvkm_oclass
g84_fifo_sclass[] = {
	{ G82_CHANNEL_DMA, &g84_fifo_ofuncs_dma },
	{ G82_CHANNEL_GPFIFO, &g84_fifo_ofuncs_ind },
	{}
};
