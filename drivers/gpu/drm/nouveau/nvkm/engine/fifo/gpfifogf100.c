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
#include "changf100.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/timer.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

static u32
gf100_fifo_gpfifo_engine_addr(struct nvkm_engine *engine)
{
	switch (engine->subdev.index) {
	case NVKM_ENGINE_SW    : return 0;
	case NVKM_ENGINE_GR    : return 0x0210;
	case NVKM_ENGINE_CE0   : return 0x0230;
	case NVKM_ENGINE_CE1   : return 0x0240;
	case NVKM_ENGINE_MSPDEC: return 0x0250;
	case NVKM_ENGINE_MSPPP : return 0x0260;
	case NVKM_ENGINE_MSVLD : return 0x0270;
	default:
		WARN_ON(1);
		return 0;
	}
}

static int
gf100_fifo_gpfifo_engine_fini(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine, bool suspend)
{
	const u32 offset = gf100_fifo_gpfifo_engine_addr(engine);
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	struct nvkm_subdev *subdev = &chan->fifo->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_gpuobj *inst = chan->base.inst;
	int ret = 0;

	mutex_lock(&subdev->mutex);
	nvkm_wr32(device, 0x002634, chan->base.chid);
	if (nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x002634) == chan->base.chid)
			break;
	) < 0) {
		nvkm_error(subdev, "channel %d [%s] kick timeout\n",
			   chan->base.chid, chan->base.object.client->name);
		ret = -ETIMEDOUT;
	}
	mutex_unlock(&subdev->mutex);

	if (ret && suspend)
		return ret;

	if (offset) {
		nvkm_kmap(inst);
		nvkm_wo32(inst, offset + 0x00, 0x00000000);
		nvkm_wo32(inst, offset + 0x04, 0x00000000);
		nvkm_done(inst);
	}

	return ret;
}

static int
gf100_fifo_gpfifo_engine_init(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	const u32 offset = gf100_fifo_gpfifo_engine_addr(engine);
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	struct nvkm_gpuobj *inst = chan->base.inst;

	if (offset) {
		u64 addr = chan->engn[engine->subdev.index].vma.offset;
		nvkm_kmap(inst);
		nvkm_wo32(inst, offset + 0x00, lower_32_bits(addr) | 4);
		nvkm_wo32(inst, offset + 0x04, upper_32_bits(addr));
		nvkm_done(inst);
	}

	return 0;
}

static void
gf100_fifo_gpfifo_engine_dtor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	nvkm_gpuobj_unmap(&chan->engn[engine->subdev.index].vma);
	nvkm_gpuobj_del(&chan->engn[engine->subdev.index].inst);
}

static int
gf100_fifo_gpfifo_engine_ctor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine,
			      struct nvkm_object *object)
{
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	int engn = engine->subdev.index;
	int ret;

	if (!gf100_fifo_gpfifo_engine_addr(engine))
		return 0;

	ret = nvkm_object_bind(object, NULL, 0, &chan->engn[engn].inst);
	if (ret)
		return ret;

	return nvkm_gpuobj_map(chan->engn[engn].inst, chan->vm,
			       NV_MEM_ACCESS_RW, &chan->engn[engn].vma);
}

static void
gf100_fifo_gpfifo_fini(struct nvkm_fifo_chan *base)
{
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	struct gf100_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 coff = chan->base.chid * 8;

	if (!list_empty(&chan->head) && !chan->killed) {
		list_del_init(&chan->head);
		nvkm_mask(device, 0x003004 + coff, 0x00000001, 0x00000000);
		gf100_fifo_runlist_update(fifo);
	}

	gf100_fifo_intr_engine(fifo);

	nvkm_wr32(device, 0x003000 + coff, 0x00000000);
}

static void
gf100_fifo_gpfifo_init(struct nvkm_fifo_chan *base)
{
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	struct gf100_fifo *fifo = chan->fifo;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	u32 addr = chan->base.inst->addr >> 12;
	u32 coff = chan->base.chid * 8;

	nvkm_wr32(device, 0x003000 + coff, 0xc0000000 | addr);

	if (list_empty(&chan->head) && !chan->killed) {
		list_add_tail(&chan->head, &fifo->chan);
		nvkm_wr32(device, 0x003004 + coff, 0x001f0001);
		gf100_fifo_runlist_update(fifo);
	}
}

static void *
gf100_fifo_gpfifo_dtor(struct nvkm_fifo_chan *base)
{
	struct gf100_fifo_chan *chan = gf100_fifo_chan(base);
	nvkm_vm_ref(NULL, &chan->vm, chan->pgd);
	nvkm_gpuobj_del(&chan->pgd);
	return chan;
}

static const struct nvkm_fifo_chan_func
gf100_fifo_gpfifo_func = {
	.dtor = gf100_fifo_gpfifo_dtor,
	.init = gf100_fifo_gpfifo_init,
	.fini = gf100_fifo_gpfifo_fini,
	.ntfy = g84_fifo_chan_ntfy,
	.engine_ctor = gf100_fifo_gpfifo_engine_ctor,
	.engine_dtor = gf100_fifo_gpfifo_engine_dtor,
	.engine_init = gf100_fifo_gpfifo_engine_init,
	.engine_fini = gf100_fifo_gpfifo_engine_fini,
};

static int
gf100_fifo_gpfifo_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	union {
		struct fermi_channel_gpfifo_v0 v0;
	} *args = data;
	struct gf100_fifo *fifo = gf100_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_object *parent = oclass->parent;
	struct gf100_fifo_chan *chan;
	u64 usermem, ioffset, ilength;
	int ret, i;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vm %llx "
				   "ioffset %016llx ilength %08x\n",
			   args->v0.version, args->v0.vm, args->v0.ioffset,
			   args->v0.ilength);
	} else
		return ret;

	/* allocate channel */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;
	chan->fifo = fifo;
	INIT_LIST_HEAD(&chan->head);

	ret = nvkm_fifo_chan_ctor(&gf100_fifo_gpfifo_func, &fifo->base,
				  0x1000, 0x1000, true, args->v0.vm, 0,
				  (1ULL << NVKM_ENGINE_CE0) |
				  (1ULL << NVKM_ENGINE_CE1) |
				  (1ULL << NVKM_ENGINE_GR) |
				  (1ULL << NVKM_ENGINE_MSPDEC) |
				  (1ULL << NVKM_ENGINE_MSPPP) |
				  (1ULL << NVKM_ENGINE_MSVLD) |
				  (1ULL << NVKM_ENGINE_SW),
				  1, fifo->user.bar.offset, 0x1000,
				  oclass, &chan->base);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	/* page directory */
	ret = nvkm_gpuobj_new(device, 0x10000, 0x1000, false, NULL, &chan->pgd);
	if (ret)
		return ret;

	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x0200, lower_32_bits(chan->pgd->addr));
	nvkm_wo32(chan->base.inst, 0x0204, upper_32_bits(chan->pgd->addr));
	nvkm_wo32(chan->base.inst, 0x0208, 0xffffffff);
	nvkm_wo32(chan->base.inst, 0x020c, 0x000000ff);
	nvkm_done(chan->base.inst);

	ret = nvkm_vm_ref(chan->base.vm, &chan->vm, chan->pgd);
	if (ret)
		return ret;

	/* clear channel control registers */

	usermem = chan->base.chid * 0x1000;
	ioffset = args->v0.ioffset;
	ilength = order_base_2(args->v0.ilength / 8);

	nvkm_kmap(fifo->user.mem);
	for (i = 0; i < 0x1000; i += 4)
		nvkm_wo32(fifo->user.mem, usermem + i, 0x00000000);
	nvkm_done(fifo->user.mem);
	usermem = nvkm_memory_addr(fifo->user.mem) + usermem;

	/* RAMFC */
	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x08, lower_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x0c, upper_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x10, 0x0000face);
	nvkm_wo32(chan->base.inst, 0x30, 0xfffff902);
	nvkm_wo32(chan->base.inst, 0x48, lower_32_bits(ioffset));
	nvkm_wo32(chan->base.inst, 0x4c, upper_32_bits(ioffset) |
					 (ilength << 16));
	nvkm_wo32(chan->base.inst, 0x54, 0x00000002);
	nvkm_wo32(chan->base.inst, 0x84, 0x20400000);
	nvkm_wo32(chan->base.inst, 0x94, 0x30000001);
	nvkm_wo32(chan->base.inst, 0x9c, 0x00000100);
	nvkm_wo32(chan->base.inst, 0xa4, 0x1f1f1f1f);
	nvkm_wo32(chan->base.inst, 0xa8, 0x1f1f1f1f);
	nvkm_wo32(chan->base.inst, 0xac, 0x0000001f);
	nvkm_wo32(chan->base.inst, 0xb8, 0xf8000000);
	nvkm_wo32(chan->base.inst, 0xf8, 0x10003080); /* 0x002310 */
	nvkm_wo32(chan->base.inst, 0xfc, 0x10000010); /* 0x002350 */
	nvkm_done(chan->base.inst);
	return 0;
}

const struct nvkm_fifo_chan_oclass
gf100_fifo_gpfifo_oclass = {
	.base.oclass = FERMI_CHANNEL_GPFIFO,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = gf100_fifo_gpfifo_new,
};
