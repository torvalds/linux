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
#include <subdev/instmem.h>

#include <nvif/class.h>

static int
nv17_chan_ramfc_write(struct nvkm_chan *chan, u64 offset, u64 length, u32 devm, bool priv)
{
	struct nvkm_memory *ramfc = chan->cgrp->runl->fifo->engine.subdev.device->imem->ramfc;
	const u32 base = chan->id * 64;

	chan->ramfc_offset = base;

	nvkm_kmap(ramfc);
	nvkm_wo32(ramfc, base + 0x00, offset);
	nvkm_wo32(ramfc, base + 0x04, offset);
	nvkm_wo32(ramfc, base + 0x0c, chan->push->addr >> 4);
	nvkm_wo32(ramfc, base + 0x14, NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES |
				      NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES |
#ifdef __BIG_ENDIAN
				      NV_PFIFO_CACHE1_BIG_ENDIAN |
#endif
				      NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8);
	nvkm_done(ramfc);
	return 0;
}

static const struct nvkm_chan_func_ramfc
nv17_chan_ramfc = {
	.layout = (const struct nvkm_ramfc_layout[]) {
		{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
		{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
		{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
		{ 16,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
		{ 16, 16, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
		{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_STATE },
		{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
		{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_ENGINE },
		{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_PULL1 },
		{ 32,  0, 0x20,  0, NV10_PFIFO_CACHE1_ACQUIRE_VALUE },
		{ 32,  0, 0x24,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP },
		{ 32,  0, 0x28,  0, NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT },
		{ 32,  0, 0x2c,  0, NV10_PFIFO_CACHE1_SEMAPHORE },
		{ 32,  0, 0x30,  0, NV10_PFIFO_CACHE1_DMA_SUBROUTINE },
		{}
	},
	.write = nv17_chan_ramfc_write,
	.clear = nv04_chan_ramfc_clear,
	.ctxdma = true,
};

static const struct nvkm_chan_func
nv17_chan = {
	.inst = &nv04_chan_inst,
	.userd = &nv04_chan_userd,
	.ramfc = &nv17_chan_ramfc,
	.start = nv04_chan_start,
	.stop = nv04_chan_stop,
};

static void
nv17_fifo_init(struct nvkm_fifo *fifo)
{
	struct nvkm_device *device = fifo->engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_ramht *ramht = imem->ramht;
	struct nvkm_memory *ramro = imem->ramro;
	struct nvkm_memory *ramfc = imem->ramfc;

	nvkm_wr32(device, NV04_PFIFO_DELAY_0, 0x000000ff);
	nvkm_wr32(device, NV04_PFIFO_DMA_TIMESLICE, 0x0101ffff);

	nvkm_wr32(device, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
					    ((ramht->bits - 9) << 16) |
					    (ramht->gpuobj->addr >> 8));
	nvkm_wr32(device, NV03_PFIFO_RAMRO, nvkm_memory_addr(ramro) >> 8);
	nvkm_wr32(device, NV03_PFIFO_RAMFC, nvkm_memory_addr(ramfc) >> 8 |
					    0x00010000);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->chid->mask);

	nvkm_wr32(device, NV03_PFIFO_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
}

static const struct nvkm_fifo_func
nv17_fifo = {
	.chid_nr = nv10_fifo_chid_nr,
	.chid_ctor = nv04_fifo_chid_ctor,
	.runl_ctor = nv04_fifo_runl_ctor,
	.init = nv17_fifo_init,
	.intr = nv04_fifo_intr,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.runl = &nv04_runl,
	.engn = &nv04_engn,
	.engn_sw = &nv04_engn,
	.cgrp = {{                        }, &nv04_cgrp },
	.chan = {{ 0, 0, NV17_CHANNEL_DMA }, &nv17_chan },
};

int
nv17_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nvkm_fifo_new_(&nv17_fifo, device, type, inst, pfifo);
}
