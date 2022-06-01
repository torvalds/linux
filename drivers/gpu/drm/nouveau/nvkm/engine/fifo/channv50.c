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

	switch (object->engine->subdev.type) {
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

void *
nv50_fifo_chan_dtor(struct nvkm_fifo_chan *base)
{
	struct nv50_fifo_chan *chan = nv50_fifo_chan(base);
	return chan;
}

static const struct nvkm_fifo_chan_func
nv50_fifo_chan_func = {
	.dtor = nv50_fifo_chan_dtor,
	.object_ctor = nv50_fifo_chan_object_ctor,
	.object_dtor = nv50_fifo_chan_object_dtor,
};

int
nv50_fifo_chan_ctor(struct nv50_fifo *fifo, u64 vmm, u64 push,
		    const struct nvkm_oclass *oclass,
		    struct nv50_fifo_chan *chan)
{
	int ret;

	if (!vmm)
		return -EINVAL;

	ret = nvkm_fifo_chan_ctor(&nv50_fifo_chan_func, &fifo->base,
				  0x10000, 0x1000, false, vmm, push,
				  BIT(NV50_FIFO_ENGN_SW) |
				  BIT(NV50_FIFO_ENGN_GR) |
				  BIT(NV50_FIFO_ENGN_MPEG) |
				  BIT(NV50_FIFO_ENGN_DMA),
				  0, 0xc00000, 0x2000, oclass, &chan->base);
	chan->fifo = fifo;
	return ret;
}
