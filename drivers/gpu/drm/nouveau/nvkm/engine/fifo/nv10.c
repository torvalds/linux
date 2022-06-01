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
#include "chan.h"

#include "nv04.h"
#include "channv04.h"
#include "regsnv04.h"

#include <nvif/class.h>

static const struct nv04_fifo_ramfc
nv10_fifo_ramfc[] = {
	{ 32,  0, 0x00,  0, NV04_PFIFO_CACHE1_DMA_PUT },
	{ 32,  0, 0x04,  0, NV04_PFIFO_CACHE1_DMA_GET },
	{ 32,  0, 0x08,  0, NV10_PFIFO_CACHE1_REF_CNT },
	{ 16,  0, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_INSTANCE },
	{ 16, 16, 0x0c,  0, NV04_PFIFO_CACHE1_DMA_DCOUNT },
	{ 32,  0, 0x10,  0, NV04_PFIFO_CACHE1_DMA_STATE },
	{ 32,  0, 0x14,  0, NV04_PFIFO_CACHE1_DMA_FETCH },
	{ 32,  0, 0x18,  0, NV04_PFIFO_CACHE1_ENGINE },
	{ 32,  0, 0x1c,  0, NV04_PFIFO_CACHE1_PULL1 },
	{}
};

static const struct nvkm_chan_func
nv10_chan = {
	.start = nv04_chan_start,
	.stop = nv04_chan_stop,
};

int
nv10_fifo_chid_nr(struct nvkm_fifo *fifo)
{
	return 32;
}

static const struct nvkm_fifo_func
nv10_fifo = {
	.chid_nr = nv10_fifo_chid_nr,
	.chid_ctor = nv04_fifo_chid_ctor,
	.runl_ctor = nv04_fifo_runl_ctor,
	.init = nv04_fifo_init,
	.intr = nv04_fifo_intr,
	.engine_id = nv04_fifo_engine_id,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.runl = &nv04_runl,
	.engn = &nv04_engn,
	.engn_sw = &nv04_engn,
	.cgrp = {{                        }, &nv04_cgrp },
	.chan = {{ 0, 0, NV10_CHANNEL_DMA }, &nv10_chan, .oclass = &nv10_fifo_dma_oclass },
};

int
nv10_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nv04_fifo_new_(&nv10_fifo, device, type, inst, 32, nv10_fifo_ramfc, pfifo);
}
