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

static void
g84_fifo_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x40000000, 0x00000000);
}

static void
g84_fifo_uevent_init(struct nvkm_event *event, int type, int index)
{
	struct nvkm_fifo *fifo = container_of(event, typeof(*fifo), uevent);
	struct nvkm_device *device = fifo->engine.subdev.device;
	nvkm_mask(device, 0x002140, 0x40000000, 0x40000000);
}

static const struct nvkm_event_func
g84_fifo_uevent_func = {
	.ctor = nvkm_fifo_uevent_ctor,
	.init = g84_fifo_uevent_init,
	.fini = g84_fifo_uevent_fini,
};

static const struct nvkm_fifo_func
g84_fifo_func = {
	.chan = {
		&g84_fifo_dma_oclass,
		&g84_fifo_gpfifo_oclass,
		NULL
	},
};

static int
g84_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nv50_fifo *fifo;
	int ret;

	ret = nvkm_fifo_create(parent, engine, oclass, 1, 127, &fifo);
	*pobject = nv_object(fifo);
	if (ret)
		return ret;

	fifo->base.func = &g84_fifo_func;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			      false, &fifo->runlist[0]);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			      false, &fifo->runlist[1]);
	if (ret)
		return ret;

	ret = nvkm_event_init(&g84_fifo_uevent_func, 1, 1, &fifo->base.uevent);
	if (ret)
		return ret;

	nv_subdev(fifo)->unit = 0x00000100;
	nv_subdev(fifo)->intr = nv04_fifo_intr;
	fifo->base.pause = nv04_fifo_pause;
	fifo->base.start = nv04_fifo_start;
	return 0;
}

struct nvkm_oclass *
g84_fifo_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(FIFO, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_fifo_ctor,
		.dtor = nv50_fifo_dtor,
		.init = nv50_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
};
