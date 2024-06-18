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
#include "chid.h"
#include "runl.h"

#include "regsnv04.h"

#include <core/ramht.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>

#include <nvif/class.h>

static int
nv40_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_memory *ramfc = chan->cgrp->runl->fifo->engine.subdev.device->imem->ramfc;
	const u32 base = chan->id * 128;

	chan->ramfc_offset = base;

	nvkm_kmap(ramfc);
	nvkm_wo32(ramfc, base + 0x00, offset);
	nvkm_wo32(ramfc, base + 0x04, offset);
	nvkm_wo32(ramfc, base + 0x0c, chan->push->addr >> 4);
	nvkm_wo32(ramfc, base + 0x18, 0x30000000 |
				      NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				      NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
				      NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				      NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nvkm_wo32(ramfc, base + 0x3c, 0x0001ffff);
	nvkm_done(ramfc);
	return 0;
}

static const struct nvkm_chan_func_ramfc
nv40_chan_ramfc = {
	.layout = (const struct nvkm_ramfc_layout[]) {
		{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
		{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
		{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
		{ 32,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
		{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
		{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_STATE },
		{ 28,  0, 0x18,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
		{  2, 28, 0x18, 28, 0x002058 },
		{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_ENGINE },
		{ 32,  0, 0x20,  0, NV04_PFIFO_CACHE1_PULL1 },
		{ 32,  0, 0x24,  0, NV10_PFIFO_CACHE1_ACQUIRE_VALUE },
		{ 32,  0, 0x28,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP },
		{ 32,  0, 0x2c,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT },
		{ 32,  0, 0x30,  0, NV10_PFIFO_CACHE1_SEMAPHORE },
		{ 32,  0, 0x34,  0, NV10_PFIFO_CACHE1_DMA_SUBROUTINE },
		{ 32,  0, 0x38,  0, NV40_PFIFO_GRCTX_INSTANCE },
		{ 17,  0, 0x3c,  0, NV04_PFIFO_DMA_TIMESLICE },
		{ 32,  0, 0x40,  0, 0x0032e4 },
		{ 32,  0, 0x44,  0, 0x0032e8 },
		{ 32,  0, 0x4c,  0, 0x002088 },
		{ 32,  0, 0x50,  0, 0x003300 },
		{ 32,  0, 0x54,  0, 0x00330c },
		{}
	},
	.write = nv40_chan_ramfc_write,
	.clear = nv04_chan_ramfc_clear,
	.ctxdma = true,
};

static const struct nvkm_chan_func_userd
nv40_chan_userd = {
	.bar = 0,
	.base = 0xc00000,
	.size = 0x001000,
};

static const struct nvkm_chan_func
nv40_chan = {
	.inst = &nv04_chan_inst,
	.userd = &nv40_chan_userd,
	.ramfc = &nv40_chan_ramfc,
	.start = nv04_chan_start,
	.stop = nv04_chan_stop,
};

static int
nv40_eobj_ramht_add(struct nvkm_engn *engn, struct nvkm_object *eobj, struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_instmem *imem = fifo->engine.subdev.device->imem;
	u32 context = chan->id << 23 | engn->id << 20;
	int hash;

	mutex_lock(&fifo->mutex);
	hash = nvkm_ramht_insert(imem->ramht, eobj, chan->id, 4, eobj->handle, context);
	mutex_unlock(&fifo->mutex);
	return hash;
}

static void
nv40_ectx_bind(struct nvkm_engn *engn, struct nvkm_cctx *cctx, struct nvkm_chan *chan)
{
	struct nvkm_fifo *fifo = chan->cgrp->runl->fifo;
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_memory *ramfc = device->imem->ramfc;
	u32 inst = 0x00000000, reg, ctx;
	int chid;

	switch (engn->engine->subdev.type) {
	case NVKM_ENGINE_GR:
		reg = 0x0032e0;
		ctx = 0x38;
		break;
	case NVKM_ENGINE_MPEG:
		if (WARN_ON(device->chipset < 0x44))
			return;
		reg = 0x00330c;
		ctx = 0x54;
		break;
	default:
		WARN_ON(1);
		return;
	}

	if (cctx)
		inst = cctx->vctx->inst->addr >> 4;

	spin_lock_irq(&fifo->lock);
	nvkm_mask(device, 0x002500, 0x00000001, 0x00000000);

	chid = nvkm_rd32(device, 0x003204) & (fifo->chid->nr - 1);
	if (chid == chan->id)
		nvkm_wr32(device, reg, inst);

	nvkm_kmap(ramfc);
	nvkm_wo32(ramfc, chan->ramfc_offset + ctx, inst);
	nvkm_done(ramfc);

	nvkm_mask(device, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irq(&fifo->lock);
}

static const struct nvkm_engn_func
nv40_engn = {
	.bind = nv40_ectx_bind,
	.ramht_add = nv40_eobj_ramht_add,
	.ramht_del = nv04_eobj_ramht_del,
};

static const struct nvkm_engn_func
nv40_engn_sw = {
	.ramht_add = nv40_eobj_ramht_add,
	.ramht_del = nv04_eobj_ramht_del,
};

static void
nv40_fifo_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_fb *fb = device->fb;
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_ramht *ramht = imem->ramht;
	struct nvkm_memory *ramro = imem->ramro;
	struct nvkm_memory *ramfc = imem->ramfc;

	nvkm_wr32(device, 0x002040, 0x000000ff);
	nvkm_wr32(device, 0x002044, 0x2101ffff);
	nvkm_wr32(device, 0x002058, 0x00000001);

	nvkm_wr32(device, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
					    ((ramht->bits - 9) << 16) |
					    (ramht->gpuobj->addr >> 8));
	nvkm_wr32(device, NV03_PFIFO_RAMRO, nvkm_memory_addr(ramro) >> 8);

	switch (device->chipset) {
	case 0x47:
	case 0x49:
	case 0x4b:
		nvkm_wr32(device, 0x002230, 0x00000001);
		fallthrough;
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x45:
	case 0x48:
		nvkm_wr32(device, 0x002220, 0x00030002);
		break;
	default:
		nvkm_wr32(device, 0x002230, 0x00000000);
		nvkm_wr32(device, 0x002220, ((fb->ram->size - 512 * 1024 +
					      nvkm_memory_addr(ramfc)) >> 16) |
					    0x00030000);
		break;
	}

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->chid->mask);

	nvkm_wr32(device, NV03_PFIFO_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
}

static const struct nvkm_fifo_func
nv40_fifo = {
	.chid_nr = nv10_fifo_chid_nr,
	.chid_ctor = nv04_fifo_chid_ctor,
	.runl_ctor = nv04_fifo_runl_ctor,
	.init = nv40_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.runl = &nv04_runl,
	.engn = &nv40_engn,
	.engn_sw = &nv40_engn_sw,
	.cgrp = {{                        }, &nv04_cgrp },
	.chan = {{ 0, 0, NV40_CHANNEL_DMA }, &nv40_chan },
};

int
nv40_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&nv40_fifo, device, type, inst, pfifo);
}
