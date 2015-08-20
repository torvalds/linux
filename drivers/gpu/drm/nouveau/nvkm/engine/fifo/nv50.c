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
nv50_fifo_runlist_update_locked(struct nv50_fifo *fifo)
{
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	struct nvkm_memory *cur;
	int i, p;

	cur = fifo->runlist[fifo->cur_runlist];
	fifo->cur_runlist = !fifo->cur_runlist;

	nvkm_kmap(cur);
	for (i = fifo->base.min, p = 0; i < fifo->base.max; i++) {
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
	mutex_lock(&nv_subdev(fifo)->mutex);
	nv50_fifo_runlist_update_locked(fifo);
	mutex_unlock(&nv_subdev(fifo)->mutex);
}

int
nv50_fifo_init(struct nvkm_object *object)
{
	struct nv50_fifo *fifo = (void *)object;
	struct nvkm_device *device = fifo->base.engine.subdev.device;
	int ret, i;

	ret = nvkm_fifo_init(&fifo->base);
	if (ret)
		return ret;

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
	return 0;
}

void
nv50_fifo_dtor(struct nvkm_object *object)
{
	struct nv50_fifo *fifo = (void *)object;

	nvkm_memory_del(&fifo->runlist[1]);
	nvkm_memory_del(&fifo->runlist[0]);

	nvkm_fifo_destroy(&fifo->base);
}

static int
nv50_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
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

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			      false, &fifo->runlist[0]);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 128 * 4, 0x1000,
			      false, &fifo->runlist[1]);
	if (ret)
		return ret;

	nv_subdev(fifo)->unit = 0x00000100;
	nv_subdev(fifo)->intr = nv04_fifo_intr;
	nv_engine(fifo)->cclass = &nv50_fifo_cclass;
	nv_engine(fifo)->sclass = nv50_fifo_sclass;
	fifo->base.pause = nv04_fifo_pause;
	fifo->base.start = nv04_fifo_start;
	return 0;
}

struct nvkm_oclass *
nv50_fifo_oclass = &(struct nvkm_oclass) {
	.handle = NV_ENGINE(FIFO, 0x50),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv50_fifo_ctor,
		.dtor = nv50_fifo_dtor,
		.init = nv50_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
};
