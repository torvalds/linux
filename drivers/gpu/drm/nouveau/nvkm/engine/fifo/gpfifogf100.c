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

#include <nvif/cl906f.h>
#include <nvif/unpack.h>

static void *
gf100_fifo_gpfifo_dtor(struct nvkm_fifo_chan *base)
{
	return gf100_fifo_chan(base);
}

static const struct nvkm_fifo_chan_func
gf100_fifo_gpfifo_func = {
	.dtor = gf100_fifo_gpfifo_dtor,
};

static int
gf100_fifo_gpfifo_new(struct nvkm_fifo *base, const struct nvkm_oclass *oclass,
		      void *data, u32 size, struct nvkm_object **pobject)
{
	union {
		struct fermi_channel_gpfifo_v0 v0;
	} *args = data;
	struct gf100_fifo *fifo = gf100_fifo(base);
	struct nvkm_object *parent = oclass->parent;
	struct gf100_fifo_chan *chan;
	u64 usermem, ioffset, ilength;
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create channel gpfifo size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create channel gpfifo vers %d vmm %llx "
				   "ioffset %016llx ilength %08x\n",
			   args->v0.version, args->v0.vmm, args->v0.ioffset,
			   args->v0.ilength);
		if (!args->v0.vmm)
			return -EINVAL;
	} else
		return ret;

	/* allocate channel */
	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	*pobject = &chan->base.object;

	ret = nvkm_fifo_chan_ctor(&gf100_fifo_gpfifo_func, &fifo->base,
				  0x1000, 0x1000, true, args->v0.vmm, 0,
				  BIT(GF100_FIFO_ENGN_GR) |
				  BIT(GF100_FIFO_ENGN_MSPDEC) |
				  BIT(GF100_FIFO_ENGN_MSPPP) |
				  BIT(GF100_FIFO_ENGN_MSVLD) |
				  BIT(GF100_FIFO_ENGN_CE0) |
				  BIT(GF100_FIFO_ENGN_CE1) |
				  BIT(GF100_FIFO_ENGN_SW),
				  0, 0, 0,
				  oclass, &chan->base);
	if (ret)
		return ret;

	args->v0.chid = chan->base.chid;

	usermem = nvkm_memory_addr(chan->base.userd.mem) + chan->base.userd.base;
	ioffset = args->v0.ioffset;
	ilength = args->v0.ilength;

	chan->base.func->ramfc->write(&chan->base, ioffset, ilength, BIT(0), false);
	return 0;
}

const struct nvkm_fifo_chan_oclass
gf100_fifo_gpfifo_oclass = {
	.ctor = gf100_fifo_gpfifo_new,
};
