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

static const struct nvkm_fifo_chan_func
gv100_fifo_gpfifo = {
	.dtor = gk104_fifo_gpfifo_dtor,
};

int
gv100_fifo_gpfifo_new_(const struct nvkm_fifo_chan_func *func,
		       struct gk104_fifo *fifo, u64 *runlists, u16 *chid,
		       u64 vmm, u64 ioffset, u64 ilength, u64 *inst, bool priv,
		       u32 *token, const struct nvkm_oclass *oclass,
		       struct nvkm_object **pobject)
{
	struct gk104_fifo_chan *chan;
	int runlist = ffs(*runlists) -1, ret;

	if (!vmm || runlist < 0 || runlist >= fifo->runlist_nr)
		return -EINVAL;
	*runlists = BIT_ULL(runlist);

	/* Allocate the channel. */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;
	chan->fifo = fifo;
	chan->runl = runlist;

	ret = nvkm_fifo_chan_ctor(func, &fifo->base, 0x1000, 0x1000, true, vmm,
				  0, fifo->runlist[runlist].engm, 0, 0, 0,
				  oclass, &chan->base);
	if (ret)
		return ret;

	*chid = chan->base.chid;
	*inst = chan->base.inst->addr;
	*token = chan->base.func->doorbell_handle(&chan->base);

	chan->base.func->ramfc->write(&chan->base, ioffset, ilength, BIT(0), priv);
	return 0;
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
