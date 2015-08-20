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
#include <subdev/timer.h>

int
nv50_fifo_context_detach(struct nvkm_object *parent, bool suspend,
			 struct nvkm_object *object)
{
	struct nv50_fifo *fifo = (void *)parent->engine;
	struct nv50_fifo_base *base = (void *)parent->parent;
	struct nv50_fifo_chan *chan = (void *)parent;
	struct nvkm_subdev *subdev = &fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 addr, me;
	int ret = 0;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0000; break;
	case NVDEV_ENGINE_MPEG : addr = 0x0060; break;
	default:
		return -EINVAL;
	}

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
	nvkm_wr32(device, 0x0032fc, nv_gpuobj(base)->addr >> 12);
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x0032fc) != 0xffffffff)
			break;
	) < 0) {
		nvkm_error(subdev, "channel %d [%s] unload timeout\n",
			   chan->base.chid, nvkm_client_name(chan));
		if (suspend)
			ret = -EBUSY;
	}
	nvkm_wr32(device, 0x00b860, me);

	if (ret == 0) {
		nvkm_kmap(base->eng);
		nvkm_wo32(base->eng, addr + 0x00, 0x00000000);
		nvkm_wo32(base->eng, addr + 0x04, 0x00000000);
		nvkm_wo32(base->eng, addr + 0x08, 0x00000000);
		nvkm_wo32(base->eng, addr + 0x0c, 0x00000000);
		nvkm_wo32(base->eng, addr + 0x10, 0x00000000);
		nvkm_wo32(base->eng, addr + 0x14, 0x00000000);
		nvkm_done(base->eng);
	}

	return ret;
}

int
nv50_fifo_context_attach(struct nvkm_object *parent, struct nvkm_object *object)
{
	struct nv50_fifo_base *base = (void *)parent->parent;
	struct nvkm_gpuobj *ectx = (void *)object;
	u64 limit = ectx->addr + ectx->size - 1;
	u64 start = ectx->addr;
	u32 addr;

	switch (nv_engidx(object->engine)) {
	case NVDEV_ENGINE_SW   : return 0;
	case NVDEV_ENGINE_GR   : addr = 0x0000; break;
	case NVDEV_ENGINE_MPEG : addr = 0x0060; break;
	default:
		return -EINVAL;
	}

	nv_engctx(ectx)->addr = nv_gpuobj(base)->addr >> 12;

	nvkm_kmap(base->eng);
	nvkm_wo32(base->eng, addr + 0x00, 0x00190000);
	nvkm_wo32(base->eng, addr + 0x04, lower_32_bits(limit));
	nvkm_wo32(base->eng, addr + 0x08, lower_32_bits(start));
	nvkm_wo32(base->eng, addr + 0x0c, upper_32_bits(limit) << 24 |
					  upper_32_bits(start));
	nvkm_wo32(base->eng, addr + 0x10, 0x00000000);
	nvkm_wo32(base->eng, addr + 0x14, 0x00000000);
	nvkm_done(base->eng);
	return 0;
}

void
nv50_fifo_object_detach(struct nvkm_object *parent, int cookie)
{
	struct nv50_fifo_chan *chan = (void *)parent;
	nvkm_ramht_remove(chan->ramht, cookie);
}

int
nv50_fifo_object_attach(struct nvkm_object *parent,
			struct nvkm_object *object, u32 handle)
{
	struct nv50_fifo_chan *chan = (void *)parent;
	u32 context;

	if (nv_iclass(object, NV_GPUOBJ_CLASS))
		context = nv_gpuobj(object)->node->offset >> 4;
	else
		context = 0x00000004; /* just non-zero */

	if (object->engine) {
		switch (nv_engidx(object->engine)) {
		case NVDEV_ENGINE_DMAOBJ:
		case NVDEV_ENGINE_SW    : context |= 0x00000000; break;
		case NVDEV_ENGINE_GR    : context |= 0x00100000; break;
		case NVDEV_ENGINE_MPEG  : context |= 0x00200000; break;
		default:
			return -EINVAL;
		}
	}

	return nvkm_ramht_insert(chan->ramht, NULL, 0, 0, handle, context);
}

int
nv50_fifo_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_fifo *fifo = (void *)object->engine;
	struct nv50_fifo_chan *chan = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 chid = chan->base.chid;

	/* remove channel from runlist, fifo will unload context */
	nvkm_mask(device, 0x002600 + (chid * 4), 0x80000000, 0x00000000);
	nv50_fifo_runlist_update(fifo);
	nvkm_wr32(device, 0x002600 + (chid * 4), 0x00000000);

	return nvkm_fifo_channel_fini(&chan->base, suspend);
}

int
nv50_fifo_chan_init(struct nvkm_object *object)
{
	struct nv50_fifo *fifo = (void *)object->engine;
	struct nv50_fifo_base *base = (void *)object->parent;
	struct nv50_fifo_chan *chan = (void *)object;
	struct nvkm_gpuobj *ramfc = base->ramfc;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 chid = chan->base.chid;
	int ret;

	ret = nvkm_fifo_channel_init(&chan->base);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x002600 + (chid * 4), 0x80000000 | ramfc->addr >> 12);
	nv50_fifo_runlist_update(fifo);
	return 0;
}

void
nv50_fifo_chan_dtor(struct nvkm_object *object)
{
	struct nv50_fifo_chan *chan = (void *)object;
	nvkm_ramht_del(&chan->ramht);
	nvkm_fifo_channel_destroy(&chan->base);
}

void
nv50_fifo_context_dtor(struct nvkm_object *object)
{
	struct nv50_fifo_base *base = (void *)object;
	nvkm_vm_ref(NULL, &base->vm, base->pgd);
	nvkm_gpuobj_del(&base->pgd);
	nvkm_gpuobj_del(&base->eng);
	nvkm_gpuobj_del(&base->ramfc);
	nvkm_gpuobj_del(&base->cache);
	nvkm_fifo_context_destroy(&base->base);
}

static int
nv50_fifo_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		       struct nvkm_oclass *oclass, void *data, u32 size,
		       struct nvkm_object **pobject)
{
	struct nvkm_device *device = nv_engine(engine)->subdev.device;
	struct nv50_fifo_base *base;
	int ret;

	ret = nvkm_fifo_context_create(parent, engine, oclass, NULL, 0x10000,
				       0x1000, NVOBJ_FLAG_HEAP, &base);
	*pobject = nv_object(base);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x0200, 0x1000, true, &base->base.gpuobj,
			      &base->ramfc);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x1200, 0, true, &base->base.gpuobj,
			      &base->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, &base->base.gpuobj,
			      &base->pgd);
	if (ret)
		return ret;

	ret = nvkm_vm_ref(nvkm_client(parent)->vm, &base->vm, base->pgd);
	if (ret)
		return ret;

	return 0;
}

struct nvkm_oclass
nv50_fifo_cclass = {
	.handle = NV_ENGCTX(FIFO, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_fifo_context_ctor,
		.dtor = nv50_fifo_context_dtor,
		.init = _nvkm_fifo_context_init,
		.fini = _nvkm_fifo_context_fini,
		.rd32 = _nvkm_fifo_context_rd32,
		.wr32 = _nvkm_fifo_context_wr32,
	},
};
