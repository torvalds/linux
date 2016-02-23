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

static int
nv50_fifo_chan_engine_addr(struct nvkm_engine *engine)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : return -1;
	case NVKM_ENGINE_GR    : return 0x0000;
	case NVKM_ENGINE_MPEG  : return 0x0060;
	default:
		WARN_ON(1);
		return -1;
	}
}

static int
nv50_fifo_chan_engine_fini(struct nvkm_fifo_chan *base,
			   struct nvkm_engine *engine, bool suspend)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nv50_fifo *fifo = chan->fifo;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int offset, ret = 0;
	u32 me;

	offset = nv50_fifo_chan_engine_addr(engine);
	if (offset < 0)
		return 0;

	/* HW bug workaround:
	 *
	 * PFIFO will hang forever if the connected engines don't report
	 * that they've processed the context switch request.
	 *
	 * In order for the kickoff to work, we need to ensure all the
	 * connected engines are in a state where they can answer.
	 *
	 * Newer chipsets don't seem to suffer from this issue, and well,
	 * there's also a "ignore these engines" bitmask reg we can use
	 * if we hit the issue there..
	 */
	me = nvkm_mask(device, 0x00b860, 0x00000001, 0x00000001);

	/* do the kickoff... */
	nvkm_wr32(device, 0x0032fc, chan->base.inst->addr >> 12);
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x0032fc) != 0xffffffff)
			break;
	) < 0) {
		nvkm_error(subdev, "channel %d [%s] unload timeout\n",
			   chan->base.chid, chan->base.object.client->name);
		if (suspend)
			ret = -EBUSY;
	}
	nvkm_wr32(device, 0x00b860, me);

	if (ret == 0) {
		nvkm_kmap(chan->eng);
		nvkm_wo32(chan->eng, offset + 0x00, 0x00000000);
		nvkm_wo32(chan->eng, offset + 0x04, 0x00000000);
		nvkm_wo32(chan->eng, offset + 0x08, 0x00000000);
		nvkm_wo32(chan->eng, offset + 0x0c, 0x00000000);
		nvkm_wo32(chan->eng, offset + 0x10, 0x00000000);
		nvkm_wo32(chan->eng, offset + 0x14, 0x00000000);
		nvkm_done(chan->eng);
	}

	return ret;
}

static int
nv50_fifo_chan_engine_init(struct nvkm_fifo_chan *base,
			   struct nvkm_engine *engine)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nvkm_gpuobj *engn = chan->engn[engine->subdev.index];
	u64 limit, start;
	int offset;

	offset = nv50_fifo_chan_engine_addr(engine);
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

void
nv50_fifo_chan_engine_dtor(struct nvkm_fifo_chan *base,
			   struct nvkm_engine *engine)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	nvkm_gpuobj_del(&chan->engn[engine->subdev.index]);
}

static int
nv50_fifo_chan_engine_ctor(struct nvkm_fifo_chan *base,
			   struct nvkm_engine *engine,
			   struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	int engn = engine->subdev.index;

	if (nv50_fifo_chan_engine_addr(engine) < 0)
		return 0;

	return nvkm_object_bind(object, NULL, 0, &chan->engn[engn]);
}

void
nv50_fifo_chan_object_dtor(struct nvkm_fifo_chan *base, int cookie)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	nvkm_ramht_remove(chan->ramht, cookie);
}

static int
nv50_fifo_chan_object_ctor(struct nvkm_fifo_chan *base,
			   struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	u32 handle = object->handle;
	u32 context;

	switch (object->engine->subdev.index) {
	case NVKM_ENGINE_DMAOBJ:
	case NVKM_ENGINE_SW    : context = 0x00000000; break;
	case NVKM_ENGINE_GR    : context = 0x00100000; break;
	case NVKM_ENGINE_MPEG  : context = 0x00200000; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return nvkm_ramht_insert(chan->ramht, object, 0, 4, handle, context);
}

void
nv50_fifo_chan_fini(struct nvkm_fifo_chan *base)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nv50_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 chid = chan->base.chid;

	/* remove channel from runlist, fifo will unload context */
	nvkm_mask(device, 0x002600 + (chid * 4), 0x80000000, 0x00000000);
	nv50_fifo_runlist_update(fifo);
	nvkm_wr32(device, 0x002600 + (chid * 4), 0x00000000);
}

static void
nv50_fifo_chan_init(struct nvkm_fifo_chan *base)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	struct nv50_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u64 addr = chan->ramfc->addr >> 12;
	u32 chid = chan->base.chid;

	nvkm_wr32(device, 0x002600 + (chid * 4), 0x80000000 | addr);
	nv50_fifo_runlist_update(fifo);
}

void *
nv50_fifo_chan_dtor(struct nvkm_fifo_chan *base)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	nvkm_vm_ref(NULL, &chan->vm, chan->pgd);
	nvkm_ramht_del(&chan->ramht);
	nvkm_gpuobj_del(&chan->pgd);
	nvkm_gpuobj_del(&chan->eng);
	nvkm_gpuobj_del(&chan->cache);
	nvkm_gpuobj_del(&chan->ramfc);
	return chan;
}

static const struct nvkm_fifo_chan_func
nv50_fifo_chan_func = {
	.dtor = nv50_fifo_chan_dtor,
	.init = nv50_fifo_chan_init,
	.fini = nv50_fifo_chan_fini,
	.engine_ctor = nv50_fifo_chan_engine_ctor,
	.engine_dtor = nv50_fifo_chan_engine_dtor,
	.engine_init = nv50_fifo_chan_engine_init,
	.engine_fini = nv50_fifo_chan_engine_fini,
	.object_ctor = nv50_fifo_chan_object_ctor,
	.object_dtor = nv50_fifo_chan_object_dtor,
};

int
nv50_fifo_chan_ctor(struct nv50_fifo *fifo, u64 vm, u64 push,
		    const struct nvkm_oclass *oclass,
		    struct nv50_fifo_chan *chan)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret;

	ret = nvkm_fifo_chan_ctor(&nv50_fifo_chan_func, &fifo->base,
				  0x10000, 0x1000, false, vm, push,
				  (1ULL << NVKM_ENGINE_DMAOBJ) |
				  (1ULL << NVKM_ENGINE_SW) |
				  (1ULL << NVKM_ENGINE_GR) |
				  (1ULL << NVKM_ENGINE_MPEG),
				  0, 0xc00000, 0x2000, oclass, &chan->base);
	chan->fifo = fifo;
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x0200, 0x1000, true, chan->base.inst,
			      &chan->ramfc);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x1200, 0, true, chan->base.inst,
			      &chan->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, chan->base.inst,
			      &chan->pgd);
	if (ret)
		return ret;

	ret = nvkm_ramht_new(device, 0x8000, 16, chan->base.inst, &chan->ramht);
	if (ret)
		return ret;

	return nvkm_vm_ref(chan->base.vm, &chan->vm, chan->pgd);
}
