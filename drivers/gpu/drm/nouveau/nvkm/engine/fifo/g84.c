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
#include "priv.h"
#include "cgrp.h"
#include "chan.h"
#include "runl.h"

#include <core/ramht.h>
#include <subdev/timer.h>

#include <nvif/class.h>

static void
g84_chan_bind(struct nvkm_chan *chan)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;

	nvkm_wr32(device, 0x002600 + (chan->id * 4), chan->ramfc->addr >> 8);
}

static int
g84_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_device *device = chan->cgrp->runl->fifo->engine.subdev.device;
	const u32 limit2 = ilog2(length / 8);
	int ret;

	ret = nvkm_gpuobj_new(device, 0x0200, 0, true, chan->inst, &chan->eng);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x4000, 0, false, chan->inst, &chan->pgd);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x1000, 0x400, true, chan->inst, &chan->cache);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(device, 0x100, 0x100, true, chan->inst, &chan->ramfc);
	if (ret)
		return ret;

	ret = nvkm_ramht_new(device, 0x8000, 16, chan->inst, &chan->ramht);
	if (ret)
		return ret;

	nvkm_kmap(chan->ramfc);
	nvkm_wo32(chan->ramfc, 0x3c, 0x403f6078);
	nvkm_wo32(chan->ramfc, 0x44, 0x01003fff);
	nvkm_wo32(chan->ramfc, 0x48, chan->push->node->offset >> 4);
	nvkm_wo32(chan->ramfc, 0x50, lower_32_bits(offset));
	nvkm_wo32(chan->ramfc, 0x54, upper_32_bits(offset) | (limit2 << 16));
	nvkm_wo32(chan->ramfc, 0x60, 0x7fffffff);
	nvkm_wo32(chan->ramfc, 0x78, 0x00000000);
	nvkm_wo32(chan->ramfc, 0x7c, 0x30000000 | devm);
	nvkm_wo32(chan->ramfc, 0x80, ((chan->ramht->bits - 9) << 27) |
				     (4 << 24) /* SEARCH_FULL */ |
				     (chan->ramht->gpuobj->node->offset >> 4));
	nvkm_wo32(chan->ramfc, 0x88, chan->cache->addr >> 10);
	nvkm_wo32(chan->ramfc, 0x98, chan->inst->addr >> 12);
	nvkm_done(chan->ramfc);
	return 0;
}

static const struct nvkm_chan_func_ramfc
g84_chan_ramfc = {
	.write = g84_chan_ramfc_write,
	.ctxdma = true,
	.devm = 0xfff,
};

const struct nvkm_chan_func
g84_chan = {
	.inst = &nv50_chan_inst,
	.userd = &nv50_chan_userd,
	.ramfc = &g84_chan_ramfc,
	.bind = g84_chan_bind,
	.unbind = nv50_chan_unbind,
	.start = nv50_chan_start,
	.stop = nv50_chan_stop,
};

static void
g84_ectx_bind(struct nvkm_engn *engn, struct nvkm_cctx *cctx, struct nvkm_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->cgrp->runl->fifo->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u64 start = 0, limit = 0;
	u32 flags = 0, ptr0, save;

	switch (engn->engine->subdev.type) {
	case NVKM_ENGINE_GR    : ptr0 = 0x0020; break;
	case NVKM_ENGINE_VP    :
	case NVKM_ENGINE_MSPDEC: ptr0 = 0x0040; break;
	case NVKM_ENGINE_MPEG  :
	case NVKM_ENGINE_MSPPP : ptr0 = 0x0060; break;
	case NVKM_ENGINE_BSP   :
	case NVKM_ENGINE_MSVLD : ptr0 = 0x0080; break;
	case NVKM_ENGINE_CIPHER:
	case NVKM_ENGINE_SEC   : ptr0 = 0x00a0; break;
	case NVKM_ENGINE_CE    : ptr0 = 0x00c0; break;
	default:
		WARN_ON(1);
		return;
	}

	if (!cctx) {
		save = nvkm_mask(device, 0x002520, 0x0000003f, BIT(engn->id - 1));
		nvkm_wr32(device, 0x0032fc, chan->inst->addr >> 12);
		nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x0032fc) != 0xffffffff)
				break;
		);
		nvkm_wr32(device, 0x002520, save);
	} else {
		flags = 0x00190000;
		start = cctx->vctx->inst->addr;
		limit = start + cctx->vctx->inst->size - 1;
	}

	nvkm_kmap(chan->eng);
	nvkm_wo32(chan->eng, ptr0 + 0x00, flags);
	nvkm_wo32(chan->eng, ptr0 + 0x04, lower_32_bits(limit));
	nvkm_wo32(chan->eng, ptr0 + 0x08, lower_32_bits(start));
	nvkm_wo32(chan->eng, ptr0 + 0x0c, upper_32_bits(limit) << 24 |
					  lower_32_bits(start));
	nvkm_wo32(chan->eng, ptr0 + 0x10, 0x00000000);
	nvkm_wo32(chan->eng, ptr0 + 0x14, 0x00000000);
	nvkm_done(chan->eng);
}

const struct nvkm_engn_func
g84_engn = {
	.bind = g84_ectx_bind,
	.ramht_add = nv50_eobj_ramht_add,
	.ramht_del = nv50_eobj_ramht_del,
};

static void
g84_fifo_nonstall_block(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_mask(fifo->engine.subdev.device, 0x002140, 0x40000000, 0x00000000);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

static void
g84_fifo_nonstall_allow(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), nonstall.event);
	unsigned long flags;

	spin_lock_irqsave(&fifo->lock, flags);
	nvkm_mask(fifo->engine.subdev.device, 0x002140, 0x40000000, 0x40000000);
	spin_unlock_irqrestore(&fifo->lock, flags);
}

const struct nvkm_event_func
g84_fifo_nonstall = {
	.init = g84_fifo_nonstall_allow,
	.fini = g84_fifo_nonstall_block,
};

static int
g84_fifo_runl_ctor(struct nvkm_fifo *fifo)
{
	struct nvkm_runl *runl;

	runl = nvkm_runl_new(fifo, 0, 0, 0);
	if (IS_ERR(runl))
		return PTR_ERR(runl);

	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_SW, 0);
	nvkm_runl_add(runl, 0, fifo->func->engn_sw, NVKM_ENGINE_DMAOBJ, 0);
	nvkm_runl_add(runl, 1, fifo->func->engn, NVKM_ENGINE_GR, 0);
	nvkm_runl_add(runl, 2, fifo->func->engn, NVKM_ENGINE_MPEG, 0);
	nvkm_runl_add(runl, 3, fifo->func->engn, NVKM_ENGINE_ME, 0);
	nvkm_runl_add(runl, 4, fifo->func->engn, NVKM_ENGINE_VP, 0);
	nvkm_runl_add(runl, 5, fifo->func->engn, NVKM_ENGINE_CIPHER, 0);
	nvkm_runl_add(runl, 6, fifo->func->engn, NVKM_ENGINE_BSP, 0);
	return 0;
}

static const struct nvkm_fifo_func
g84_fifo = {
	.chid_nr = nv50_fifo_chid_nr,
	.chid_ctor = nv50_fifo_chid_ctor,
	.runl_ctor = g84_fifo_runl_ctor,
	.init = nv50_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.nonstall = &g84_fifo_nonstall,
	.runl = &nv50_runl,
	.engn = &g84_engn,
	.engn_sw = &nv50_engn_sw,
	.cgrp = {{                          }, &nv04_cgrp },
	.chan = {{ 0, 0, G82_CHANNEL_GPFIFO }, &g84_chan },
};

int
g84_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&g84_fifo, device, type, inst, pfifo);
}
