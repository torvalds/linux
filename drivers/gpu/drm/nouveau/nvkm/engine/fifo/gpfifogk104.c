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
#include "changk104.h"
#include "cgrp.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>

#include <nvif/cla06f.h>
#include <nvif/unpack.h>

static u32
gk104_fifo_gpfifo_engine_addr(struct nvkm_engine *engine)
{
	switch (engine->subdev.type) {
	case NVKM_ENGINE_SW    :
	case NVKM_ENGINE_CE    : return 0;
	case NVKM_ENGINE_GR    : return 0x0210;
	case NVKM_ENGINE_SEC   : return 0x0220;
	case NVKM_ENGINE_MSPDEC: return 0x0250;
	case NVKM_ENGINE_MSPPP : return 0x0260;
	case NVKM_ENGINE_MSVLD : return 0x0270;
	case NVKM_ENGINE_VIC   : return 0x0280;
	case NVKM_ENGINE_MSENC : return 0x0290;
	case NVKM_ENGINE_NVDEC : return 0x02100270;
	case NVKM_ENGINE_NVENC :
		if (engine->subdev.inst)
			return 0x0210;
		return 0x02100290;
	default:
		WARN_ON(1);
		return 0;
	}
}

struct gk104_fifo_engn *
gk104_fifo_gpfifo_engine(struct gk104_fifo_chan *chan, struct nvkm_engine *engine)
{
	int engi = chan->base.fifo->func->engine_id(chan->base.fifo, engine);
	if (engi >= 0)
		return &chan->engn[engi];
	return NULL;
}

static int
gk104_fifo_gpfifo_engine_fini(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine, bool suspend)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct nvkm_gpuobj *inst = chan->base.inst;
	u32 offset = gk104_fifo_gpfifo_engine_addr(engine);

	if (offset) {
		nvkm_kmap(inst);
		nvkm_wo32(inst, (offset & 0xffff) + 0x00, 0x00000000);
		nvkm_wo32(inst, (offset & 0xffff) + 0x04, 0x00000000);
		if ((offset >>= 16)) {
			nvkm_wo32(inst, offset + 0x00, 0x00000000);
			nvkm_wo32(inst, offset + 0x04, 0x00000000);
		}
		nvkm_done(inst);
	}

	return 0;
}

static int
gk104_fifo_gpfifo_engine_init(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo_engn *engn = gk104_fifo_gpfifo_engine(chan, engine);
	struct nvkm_gpuobj *inst = chan->base.inst;
	u32 offset = gk104_fifo_gpfifo_engine_addr(engine);

	if (offset) {
		u32 datalo = lower_32_bits(engn->vma->addr) | 0x00000004;
		u32 datahi = upper_32_bits(engn->vma->addr);
		nvkm_kmap(inst);
		nvkm_wo32(inst, (offset & 0xffff) + 0x00, datalo);
		nvkm_wo32(inst, (offset & 0xffff) + 0x04, datahi);
		if ((offset >>= 16)) {
			nvkm_wo32(inst, offset + 0x00, datalo);
			nvkm_wo32(inst, offset + 0x04, datahi);
		}
		nvkm_done(inst);
	}

	return 0;
}

void
gk104_fifo_gpfifo_engine_dtor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo_engn *engn = gk104_fifo_gpfifo_engine(chan, engine);
	nvkm_vmm_put(chan->base.vmm, &engn->vma);
	nvkm_gpuobj_del(&engn->inst);
}

int
gk104_fifo_gpfifo_engine_ctor(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine,
			      struct nvkm_object *object)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo_engn *engn = gk104_fifo_gpfifo_engine(chan, engine);
	int ret;

	if (!gk104_fifo_gpfifo_engine_addr(engine)) {
		if (engine->subdev.type != NVKM_ENGINE_CE ||
		    engine->subdev.device->card_type < GV100)
			return 0;
	}

	ret = nvkm_object_bind(object, NULL, 0, &engn->inst);
	if (ret)
		return ret;

	if (!gk104_fifo_gpfifo_engine_addr(engine))
		return 0;

	ret = nvkm_vmm_get(chan->base.vmm, 12, engn->inst->size, &engn->vma);
	if (ret)
		return ret;

	return nvkm_memory_map(engn->inst, 0, chan->base.vmm, engn->vma, NULL, 0);
}

void *
gk104_fifo_gpfifo_dtor(struct nvkm_fifo_chan *base)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	return chan;
}

const struct nvkm_fifo_chan_func
gk104_fifo_gpfifo_func = {
	.dtor = gk104_fifo_gpfifo_dtor,
	.engine_ctor = gk104_fifo_gpfifo_engine_ctor,
	.engine_dtor = gk104_fifo_gpfifo_engine_dtor,
	.engine_init = gk104_fifo_gpfifo_engine_init,
	.engine_fini = gk104_fifo_gpfifo_engine_fini,
};

static int
gk104_fifo_gpfifo_new_(struct gk104_fifo *fifo, u64 *runlists, u16 *chid,
		       u64 vmm, u64 ioffset, u64 ilength, u64 *inst, bool priv,
		       const struct nvkm_oclass *oclass,
		       struct nvkm_object **pobject)
{
	struct gk104_fifo_chan *chan;
	int runlist = ffs(*runlists) -1, ret;
	u64 usermem;

	if (!vmm || runlist < 0 || runlist >= fifo->runlist_nr)
		return -EINVAL;
	*runlists = BIT_ULL(runlist);

	/* Allocate the channel. */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;
	chan->fifo = fifo;
	chan->runl = runlist;

	ret = nvkm_fifo_chan_ctor(&gk104_fifo_gpfifo_func, &fifo->base,
				  0x1000, 0x1000, true, vmm, 0, fifo->runlist[runlist].engm_sw,
				  0, 0, 0,
				  oclass, &chan->base);
	if (ret)
		return ret;

	*chid = chan->base.chid;
	*inst = chan->base.inst->addr;

	usermem = nvkm_memory_addr(chan->base.userd.mem) + chan->base.userd.base;
	ilength = order_base_2(ilength / 8);

	/* RAMFC */
	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x08, lower_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x0c, upper_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x10, 0x0000face);
	nvkm_wo32(chan->base.inst, 0x30, 0xfffff902);
	nvkm_wo32(chan->base.inst, 0x48, lower_32_bits(ioffset));
	nvkm_wo32(chan->base.inst, 0x4c, upper_32_bits(ioffset) |
					 (ilength << 16));
	nvkm_wo32(chan->base.inst, 0x84, 0x20400000);
	nvkm_wo32(chan->base.inst, 0x94, 0x30000001);
	nvkm_wo32(chan->base.inst, 0x9c, 0x00000100);
	nvkm_wo32(chan->base.inst, 0xac, 0x0000001f);
	nvkm_wo32(chan->base.inst, 0xe4, priv ? 0x00000020 : 0x00000000);
	nvkm_wo32(chan->base.inst, 0xe8, chan->base.chid);
	nvkm_wo32(chan->base.inst, 0xb8, 0xf8000000);
	nvkm_wo32(chan->base.inst, 0xf8, 0x10003080); /* 0x002310 */
	nvkm_wo32(chan->base.inst, 0xfc, 0x10000010); /* 0x002350 */
	nvkm_done(chan->base.inst);
	return 0;
}

int
gk104_fifo_gpfifo_new(struct gk104_fifo *fifo, const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	struct nvkm_object *parent = oclass->parent;
	union {
		struct kepler_channel_gpfifo_a_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vmm %llx "
				   "ioffset %016llx ilength %08x "
				   "runlist %016llx priv %d\n",
			   args->v0.version, args->v0.vmm, args->v0.ioffset,
			   args->v0.ilength, args->v0.runlist, args->v0.priv);
		return gk104_fifo_gpfifo_new_(fifo,
					      &args->v0.runlist,
					      &args->v0.chid,
					       args->v0.vmm,
					       args->v0.ioffset,
					       args->v0.ilength,
					      &args->v0.inst,
					       args->v0.priv,
					      oclass, pobject);
	}

	return ret;
}
