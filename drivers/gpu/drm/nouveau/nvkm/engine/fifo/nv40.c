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
#include "nv04.h"
#include "channv04.h"
#include "regsnv04.h"

#include <core/ramht.h>
#include <subdev/fb.h>
#include <subdev/instmem.h>

static const struct nv04_fifo_ramfc
nv40_fifo_ramfc[] = {
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
};

static void
nv40_fifo_init(struct nvkm_fifo *base)
{
	struct nv04_fifo *fifo = nv04_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
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

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->base.nr - 1);

	nvkm_wr32(device, NV03_PFIFO_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
}

static const struct nvkm_fifo_func
nv40_fifo = {
	.init = nv40_fifo_init,
	.intr = nv04_fifo_intr,
	.engine_id = nv04_fifo_engine_id,
	.id_engine = nv04_fifo_id_engine,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.chan = {
		&nv40_fifo_dma_oclass,
		NULL
	},
};

int
nv40_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nv04_fifo_new_(&nv40_fifo, device, type, inst, 32, nv40_fifo_ramfc, pfifo);
}
