/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "changk104.h"
#include "cgrp.h"

#include <core/client.h>
#include <core/gpuobj.h>

#include <nvif/clc36f.h>
#include <nvif/unpack.h>

static u32
gv100_fifo_gpfifo_submit_token(struct nvkm_fifo_chan *chan)
{
	return chan->chid;
}

static int
gv100_fifo_gpfifo_engine_valid(struct gk104_fifo_chan *chan, bool ce, bool valid)
{
	struct nvkm_subdev *subdev = &chan->base.fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 mask = ce ? 0x00020000 : 0x00010000;
	const u32 data = valid ? mask : 0x00000000;
	int ret;

	/* Block runlist to prevent the channel from being rescheduled. */
	mutex_lock(&chan->fifo->base.mutex);
	nvkm_mask(device, 0x002630, BIT(chan->runl), BIT(chan->runl));

	/* Preempt the channel. */
	ret = gk104_fifo_gpfifo_kick_locked(chan);
	if (ret == 0) {
		/* Update engine context validity. */
		nvkm_kmap(chan->base.inst);
		nvkm_mo32(chan->base.inst, 0x0ac, mask, data);
		nvkm_done(chan->base.inst);
	}

	/* Resume runlist. */
	nvkm_mask(device, 0x002630, BIT(chan->runl), 0);
	mutex_unlock(&chan->fifo->base.mutex);
	return ret;
}

int
gv100_fifo_gpfifo_engine_fini(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine, bool suspend)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct nvkm_gpuobj *inst = chan->base.inst;
	int ret;

	if (engine->subdev.type == NVKM_ENGINE_CE)
		return gk104_fifo_gpfifo_kick(chan);

	ret = gv100_fifo_gpfifo_engine_valid(chan, false, false);
	if (ret && suspend)
		return ret;

	nvkm_kmap(inst);
	nvkm_wo32(inst, 0x0210, 0x00000000);
	nvkm_wo32(inst, 0x0214, 0x00000000);
	nvkm_done(inst);
	return ret;
}

int
gv100_fifo_gpfifo_engine_init(struct nvkm_fifo_chan *base,
			      struct nvkm_engine *engine)
{
	struct gk104_fifo_chan *chan = gk104_fifo_chan(base);
	struct gk104_fifo_engn *engn = gk104_fifo_gpfifo_engine(chan, engine);
	struct nvkm_gpuobj *inst = chan->base.inst;

	if (engine->subdev.type == NVKM_ENGINE_CE)
		return 0;

	nvkm_kmap(inst);
	nvkm_wo32(inst, 0x210, lower_32_bits(engn->vma->addr) | 0x00000004);
	nvkm_wo32(inst, 0x214, upper_32_bits(engn->vma->addr));
	nvkm_done(inst);

	return gv100_fifo_gpfifo_engine_valid(chan, false, true);
}

static const struct nvkm_fifo_chan_func
gv100_fifo_gpfifo = {
	.dtor = gk104_fifo_gpfifo_dtor,
	.init = gk104_fifo_gpfifo_init,
	.fini = gk104_fifo_gpfifo_fini,
	.ntfy = gf100_fifo_chan_ntfy,
	.engine_ctor = gk104_fifo_gpfifo_engine_ctor,
	.engine_dtor = gk104_fifo_gpfifo_engine_dtor,
	.engine_init = gv100_fifo_gpfifo_engine_init,
	.engine_fini = gv100_fifo_gpfifo_engine_fini,
	.submit_token = gv100_fifo_gpfifo_submit_token,
};

int
gv100_fifo_gpfifo_new_(const struct nvkm_fifo_chan_func *func,
		       struct gk104_fifo *fifo, u64 *runlists, u16 *chid,
		       u64 vmm, u64 ioffset, u64 ilength, u64 *inst, bool priv,
		       u32 *token, const struct nvkm_oclass *oclass,
		       struct nvkm_object **pobject)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct gk104_fifo_chan *chan;
	int runlist = ffs(*runlists) -1, ret, i;
	u64 usermem, mthd;
	u32 size;

	if (!vmm || runlist < 0 || runlist >= fifo->runlist_nr)
		return -EINVAL;
	*runlists = BIT_ULL(runlist);

	/* Allocate the channel. */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;
	chan->fifo = fifo;
	chan->runl = runlist;
	INIT_LIST_HEAD(&chan->head);

	ret = nvkm_fifo_chan_ctor(func, &fifo->base, 0x1000, 0x1000, true, vmm,
				  0, fifo->runlist[runlist].engm, 1, fifo->user.bar->addr, 0x200,
				  oclass, &chan->base);
	if (ret)
		return ret;

	*chid = chan->base.chid;
	*inst = chan->base.inst->addr;
	*token = chan->base.func->submit_token(&chan->base);

	/* Hack to support GPUs where even individual channels should be
	 * part of a channel group.
	 */
	if (fifo->func->cgrp_force) {
		if (!(chan->cgrp = kmalloc(sizeof(*chan->cgrp), GFP_KERNEL)))
			return -ENOMEM;
		chan->cgrp->id = chan->base.chid;
		INIT_LIST_HEAD(&chan->cgrp->head);
		INIT_LIST_HEAD(&chan->cgrp->chan);
		chan->cgrp->chan_nr = 0;
	}

	/* Clear channel control registers. */
	usermem = chan->base.chid * 0x200;
	ilength = order_base_2(ilength / 8);

	nvkm_kmap(fifo->user.mem);
	for (i = 0; i < 0x200; i += 4)
		nvkm_wo32(fifo->user.mem, usermem + i, 0x00000000);
	nvkm_done(fifo->user.mem);
	usermem = nvkm_memory_addr(fifo->user.mem) + usermem;

	/* Allocate fault method buffer (magics come from nvgpu). */
	size = nvkm_rd32(device, 0x104028); /* NV_PCE_PCE_MAP */
	size = 27 * 5 * (((9 + 1 + 3) * hweight32(size)) + 2);
	size = roundup(size, PAGE_SIZE);

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, size, 0x1000, true,
			      &chan->mthd);
	if (ret)
		return ret;

	mthd = nvkm_memory_bar2(chan->mthd);
	if (mthd == ~0ULL)
		return -EFAULT;

	/* RAMFC */
	nvkm_kmap(chan->base.inst);
	nvkm_wo32(chan->base.inst, 0x008, lower_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x00c, upper_32_bits(usermem));
	nvkm_wo32(chan->base.inst, 0x010, 0x0000face);
	nvkm_wo32(chan->base.inst, 0x030, 0x7ffff902);
	nvkm_wo32(chan->base.inst, 0x048, lower_32_bits(ioffset));
	nvkm_wo32(chan->base.inst, 0x04c, upper_32_bits(ioffset) |
					  (ilength << 16));
	nvkm_wo32(chan->base.inst, 0x084, 0x20400000);
	nvkm_wo32(chan->base.inst, 0x094, 0x30000001);
	nvkm_wo32(chan->base.inst, 0x0e4, priv ? 0x00000020 : 0x00000000);
	nvkm_wo32(chan->base.inst, 0x0e8, chan->base.chid);
	nvkm_wo32(chan->base.inst, 0x0f4, 0x00001000);
	nvkm_wo32(chan->base.inst, 0x0f8, 0x10003080);
	nvkm_mo32(chan->base.inst, 0x218, 0x00000000, 0x00000000);
	nvkm_wo32(chan->base.inst, 0x220, lower_32_bits(mthd));
	nvkm_wo32(chan->base.inst, 0x224, upper_32_bits(mthd));
	nvkm_done(chan->base.inst);
	return gv100_fifo_gpfifo_engine_valid(chan, true, true);
}

int
gv100_fifo_gpfifo_new(struct gk104_fifo *fifo, const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	struct nvkm_object *parent = oclass->parent;
	union {
		struct volta_channel_gpfifo_a_v0 v0;
	} *args = data;
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vmm %llx "
				   "ioffset %016llx ilength %08x "
				   "runlist %016llx priv %d\n",
			   args->v0.version, args->v0.vmm, args->v0.ioffset,
			   args->v0.ilength, args->v0.runlist, args->v0.priv);
		return gv100_fifo_gpfifo_new_(&gv100_fifo_gpfifo, fifo,
					      &args->v0.runlist,
					      &args->v0.chid,
					       args->v0.vmm,
					       args->v0.ioffset,
					       args->v0.ilength,
					      &args->v0.inst,
					       args->v0.priv,
					      &args->v0.token,
					      oclass, pobject);
	}

	return ret;
}
