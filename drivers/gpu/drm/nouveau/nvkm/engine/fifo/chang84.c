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
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/cl826e.h>

int
g84_fifo_chan_ntfy(struct nvkm_fifo_chan *chan, u32 type,
		   struct nvkm_event **pevent)
{
	switch (type) {
	case G82_CHANNEL_DMA_V0_NTFY_UEVENT:
		*pevent = &chan->fifo->uevent;
		return 0;
	default:
		break;
	}
	return -EINVAL;
}

static int
g84_fifo_chan_engine(struct nvkm_engine *engine)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_GR    : return 0;
	case NVKM_ENGINE_MPEG  :
	case NVKM_ENGINE_MSPPP : return 1;
	case NVKM_ENGINE_CE0   : return 2;
	case NVKM_ENGINE_VP    :
	case NVKM_ENGINE_MSPDEC: return 3;
	case NVKM_ENGINE_CIPHER:
	case NVKM_ENGINE_SEC   : return 4;
	case NVKM_ENGINE_BSP   :
	case NVKM_ENGINE_MSVLD : return 5;
	default:
		WARN_ON(1);
		return 0;
	}
}

static int
g84_fifo_chan_engine_addr(struct nvkm_engine *engine)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : return -1;
	case NVKM_ENGINE_GR    : return 0x0020;
	case NVKM_ENGINE_VP    :
	case NVKM_ENGINE_MSPDEC: return 0x0040;
	case NVKM_ENGINE_MPEG  :
	case NVKM_ENGINE_MSPPP : return 0x0060;
	case NVKM_ENGINE_BSP   :
	case NVKM_ENGINE_MSVLD : return 0x0080;
	case NVKM_ENGINE_CIPHER:
	case NVKM_ENGINE_SEC   : return 0x00a0;
	case NVKM_ENGINE_CE0   : return 0x00c0;
	default:
		WARN_ON(1);
		return -1;
	}
}

static int
g84_fifo_chan_engine_fini(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine, bool suspend)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nv50_fifo *fifo = chan->fifo;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 engn, save;
	int offset;
	bool done;

	offset = g84_fifo_chan_engine_addr(engine);
	if (offset < 0)
		return 0;

	engn = g84_fifo_chan_engine(engine);
	save = nvkm_mask(device, 0x002520, 0x0000003f, 1 << engn);
	nvkm_wr32(device, 0x0032fc, chan->base.inst->addr >> 12);
	done = nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x0032fc) != 0xffffffff)
			break;
	) >= 0;
	nvkm_wr32(device, 0x002520, save);
	if (!done) {
		nvkm_error(subdev, "channel %d [%s] unload timeout\n",
			   chan->base.chid, chan->base.object.client->name);
		if (suspend)
			return -EBUSY;
	}

	nvkm_kmap(chan->eng);
	nvkm_wo32(chan->eng, offset + 0x00, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x04, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x08, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x0c, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x10, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x14, 0x00000000);
	nvkm_done(chan->eng);
	return 0;
}


int
g84_fifo_chan_engine_init(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nvkm_gpuobj *engn = chan->engn[engine->subdev.index];
	u64 limit, start;
	int offset;

	offset = g84_fifo_chan_engine_addr(engine);
	if (offset < 0)
		return 0;
	limit = engn->addr + engn->size - 1;
	start = engn->addr;

	nvkm_kmap(chan->eng);
	nvkm_wo32(chan->eng, offset + 0x00, 0x00190000);
	nvkm_wo32(chan->eng, offset + 0x04, lower_32_bits(limit));
	nvkm_wo32(chan->eng, offset + 0x08, lower_32_bits(start));
	nvkm_wo32(chan->eng, offset + 0x0c, upper_32_bits(limit) << 24 |
					    upper_32_bits(start));
	nvkm_wo32(chan->eng, offset + 0x10, 0x00000000);
	nvkm_wo32(chan->eng, offset + 0x14, 0x00000000);
	nvkm_done(chan->eng);
	return 0;
}

static int
g84_fifo_chan_engine_ctor(struct nvkm_fifo_chan *base,
			  struct nvkm_engine *engine,
			  struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	int engn = engine->subdev.index;

	if (g84_fifo_chan_engine_addr(engine) < 0)
		return 0;

	return nvkm_object_bind(object, NULL, 0, &chan->engn[engn]);
}

int
g84_fifo_chan_object_ctor(struct nvkm_fifo_chan *base,
			  struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	u32 handle = object->handle;
	u32 context;

	switch (object->engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : context = 0x00000000; break;
	case NVKM_ENGINE_GR    : context = 0x00100000; break;
	case NVKM_ENGINE_MPEG  :
	case NVKM_ENGINE_MSPPP : context = 0x00200000; break;
	case NVKM_ENGINE_ME    :
	case NVKM_ENGINE_CE0   : context = 0x00300000; break;
	case NVKM_ENGINE_VP    :
	case NVKM_ENGINE_MSPDEC: context = 0x00400000; break;
	case NVKM_ENGINE_CIPHER:
	case NVKM_ENGINE_SEC   :
	case NVKM_ENGINE_VIC   : context = 0x00500000; break;
	case NVKM_ENGINE_BSP   :
	case NVKM_ENGINE_MSVLD : context = 0x00600000; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return nvkm_ramht_insert(chan->ramht, object, 0, 4, handle, context);
}

static void
g84_fifo_chan_init(struct nvkm_fifo_chan *base)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nv50_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u64 addr = chan->ramfc->addr >> 8;
	u32 chid = chan->base.chid;

	nvkm_wr32(device, 0x002600 + (chid * 4), 0x80000000 | addr);
	nv50_fifo_runlist_update(fifo);
}

static const struct nvkm_fifo_chan_func
g84_fifo_chan_func = {
	.dtor = nv50_fifo_chan_dtor,
	.init = g84_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.ntfy = g84_fifo_chan_ntfy,
	.engine_ctor = g84_fifo_chan_engine_ctor,
	.engine_dtor = nv50_fifo_chan_engine_dtor,
	.engine_init = g84_fifo_chan_engine_init,
	.engine_fini = g84_fifo_chan_engine_fini,
	.object_ctor = g84_fifo_chan_object_ctor,
	.object_dtor = nv50_fifo_chan_object_dtor,
};

int
g84_fifo_chan_ctor(struct nv50_fifo *fifo, u64 vm, u64 push,
		   const struct nvkm_oclass *oclass,
		   struct nv50_fifo_chan *chan)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret;

	ret = nvkm_fifo_chan_ctor(&g84_fifo_chan_func, &fifo->base,
				  0x10000, 0x1000, false, vm, push,
				  (1ULL << NVKM_ENGINE_BSP) |
				  (1ULL << NVKM_ENGINE_CE0) |
				  (1ULL << NVKM_ENGINE_CIPHER) |
				  (1ULL << NVKM_ENGINE_DMAOBJ) |
				  (1ULL << NVKM_ENGINE_GR) |
				  (1ULL << NVKM_ENGINE_ME) |
				  (1ULL << NVKM_ENGINE_MPEG) |
				  (1ULL << NVKM_ENGINE_MSPDEC) |
				  (1ULL << NVKM_ENGINE_MSPPP) |
				  (1ULL << NVKM_ENGINE_MSVLD) |
				  (1ULL << NVKM_ENGINE_SEC) |
				  (1ULL << NVKM_ENGINE_SW) |
				  (1ULL << NVKM_ENGINE_VIC) |
				  (1ULL << NVKM_ENGINE_VP),
				  0, 0xc00000, 0x2000, oclass, &chan->base);
	chan->fifo = fifo;
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x0200, 0, true, chan->base.inst,
			      &chan->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, chan->base.inst,
			      &chan->pgd);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x1000, 0x400, true, chan->base.inst,
			      &chan->cache);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x100, 0x100, true, chan->base.inst,
			      &chan->ramfc);
	if (ret)
		return ret;

	ret = nvkm_ramht_new(device, 0x8000, 16, chan->base.inst, &chan->ramht);
	if (ret)
		return ret;

	return nvkm_vm_ref(chan->base.vm, &chan->vm, chan->pgd);
}
