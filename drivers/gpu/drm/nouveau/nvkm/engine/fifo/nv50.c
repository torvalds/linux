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
#include "nv50.h"
#include "channv50.h"

#include <core/gpuobj.h>

static void
nv50_fifo_runlist_update_locked(struct nv50_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_memory *cur;
	int i, p;

	cur = fifo->runlist[fifo->cur_runlist];
	fifo->cur_runlist = !fifo->cur_runlist;

	nvkm_kmap(cur);
	for (i = 0, p = 0; i < fifo->base.nr; i++) {
		if (nvkm_rd32(device, 0x002600 + (i * 4)) & 0x80000000)
			nvkm_wo32(cur, p++ * 4, i);
	}
	nvkm_done(cur);

	nvkm_wr32(device, 0x0032f4, nvkm_memory_addr(cur) >> 12);
	nvkm_wr32(device, 0x0032ec, p);
	nvkm_wr32(device, 0x002500, 0x00000101);
}

void
nv50_fifo_runlist_update(struct nv50_fifo *fifo)
{
	mutex_lock(&fifo->base.mutex);
	nv50_fifo_runlist_update_locked(fifo);
	mutex_unlock(&fifo->base.mutex);
}

int
nv50_fifo_oneinit(struct nvkm_fifo *base)
{
	struct nv50_fifo *fifo = nv50_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			      false, &fifo->runlist[0]);
	if (ret)
		return ret;

	return nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			       false, &fifo->runlist[1]);
}

void
nv50_fifo_init(struct nvkm_fifo *base)
{
	struct nv50_fifo *fifo = nv50_fifo(base);
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int i;

	nvkm_mask(device, 0x000200, 0x00000100, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00000100, 0x00000100);
	nvkm_wr32(device, 0x00250c, 0x6f3cfc34);
	nvkm_wr32(device, 0x002044, 0x01003fff);

	nvkm_wr32(device, 0x002100, 0xffffffff);
	nvkm_wr32(device, 0x002140, 0xbfffffff);

	for (i = 0; i < 128; i++)
		nvkm_wr32(device, 0x002600 + (i * 4), 0x00000000);
	nv50_fifo_runlist_update_locked(fifo);

	nvkm_wr32(device, 0x003200, 0x00000001);
	nvkm_wr32(device, 0x003250, 0x00000001);
	nvkm_wr32(device, 0x002500, 0x00000001);
}

void *
nv50_fifo_dtor(struct nvkm_fifo *base)
{
	struct nv50_fifo *fifo = nv50_fifo(base);
	nvkm_memory_unref(&fifo->runlist[1]);
	nvkm_memory_unref(&fifo->runlist[0]);
	return fifo;
}

int
nv50_fifo_new_(const struct nvkm_fifo_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, struct nvkm_fifo **pfifo)
{
	struct nv50_fifo *fifo;
	int ret;

	if (!(fifo = kzalloc(sizeof(*fifo), GFP_KERNEL)))
		return -ENOMEM;
	*pfifo = &fifo->base;

	ret = nvkm_fifo_ctor(func, device, type, inst, 128, &fifo->base);
	if (ret)
		return ret;

	set_bit(0, fifo->base.mask); /* PIO channel */
	set_bit(127, fifo->base.mask); /* inactive channel */
	return 0;
}

static const struct nvkm_fifo_func
nv50_fifo = {
	.dtor = nv50_fifo_dtor,
	.oneinit = nv50_fifo_oneinit,
	.init = nv50_fifo_init,
	.intr = nv04_fifo_intr,
	.engine_id = nv04_fifo_engine_id,
	.id_engine = nv04_fifo_id_engine,
	.pause = nv04_fifo_pause,
	.start = nv04_fifo_start,
	.chan = {
		&nv50_fifo_dma_oclass,
		&nv50_fifo_gpfifo_oclass,
		NULL
	},
};

int
nv50_fifo_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_fifo **pfifo)
{
	return nv50_fifo_new_(&nv50_fifo, device, type, inst, pfifo);
}
