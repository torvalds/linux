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
#include <subdev/instmem.h>

static struct ramfc_desc
nv17_ramfc[] = {
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
};

static int
nv17_fifo_init(struct nvkm_object *object)
{
	struct nv04_fifo *fifo = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_instmem *imem = device->imem;
	struct nvkm_ramht *ramht = imem->ramht;
	struct nvkm_memory *ramro = imem->ramro;
	struct nvkm_memory *ramfc = imem->ramfc;
	int ret;

	ret = nvkm_fifo_init(&fifo->base);
	if (ret)
		return ret;

	nvkm_wr32(device, NV04_PFIFO_DELAY_0, 0x000000ff);
	nvkm_wr32(device, NV04_PFIFO_DMA_TIMESLICE, 0x0101ffff);

	nvkm_wr32(device, NV03_PFIFO_RAMHT, (0x03 << 24) /* search 128 */ |
					    ((ramht->bits - 9) << 16) |
					    (ramht->gpuobj->addr >> 8));
	nvkm_wr32(device, NV03_PFIFO_RAMRO, nvkm_memory_addr(ramro) >> 8);
	nvkm_wr32(device, NV03_PFIFO_RAMFC, nvkm_memory_addr(ramfc) >> 8 |
					    0x00010000);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH1, fifo->base.nr - 1);

	nvkm_wr32(device, NV03_PFIFO_INTR_0, 0xffffffff);
	nvkm_wr32(device, NV03_PFIFO_INTR_EN_0, 0xffffffff);

	nvkm_wr32(device, NV03_PFIFO_CACHE1_PUSH0, 1);
	nvkm_wr32(device, NV04_PFIFO_CACHE1_PULL0, 1);
	nvkm_wr32(device, NV03_PFIFO_CACHES, 1);
	return 0;
}

static const struct nvkm_fifo_func
nv17_fifo_func = {
	.chan = {
		&nv17_fifo_dma_oclass,
		NULL
	},
};

static int
nv17_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nv04_fifo *fifo;
	int ret;

	ret = nvkm_fifo_create(parent, engine, oclass, 0, 31, &fifo);
	*pobject = nv_object(fifo);
	if (ret)
		return ret;

	fifo->base.func = &nv17_fifo_func;

	nv_subdev(fifo)->unit = 0x00000100;
	nv_subdev(fifo)->intr = nv04_fifo_intr;
	fifo->base.pause = nv04_fifo_pause;
	fifo->base.start = nv04_fifo_start;
	fifo->ramfc_desc = nv17_ramfc;
	return 0;
}

struct nvkm_oclass *
nv17_fifo_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(FIFO, 0x17),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv17_fifo_ctor,
		.dtor = nv04_fifo_dtor,
		.init = nv17_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
};
