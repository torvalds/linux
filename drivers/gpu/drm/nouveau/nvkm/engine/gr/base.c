/*
 * Copyright 2015 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "priv.h"

#include <engine/fifo.h>

static void
nvkm_gr_tile(struct nvkm_engine *engine, int region, struct nvkm_fb_tile *tile)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	if (gr->func->tile)
		gr->func->tile(gr, region, tile);
}

u64
nvkm_gr_units(struct nvkm_gr *gr)
{
	if (gr->func->units)
		return gr->func->units(gr);
	return 0;
}

int
nvkm_gr_tlb_flush(struct nvkm_gr *gr)
{
	if (gr->func->tlb_flush)
		return gr->func->tlb_flush(gr);
	return -ENODEV;
}

static int
nvkm_gr_oclass_get(struct nvkm_oclass *oclass, int index)
{
	struct nvkm_gr *gr = nvkm_gr(oclass->engine);
	int c = 0;

	if (gr->func->object_get) {
		int ret = gr->func->object_get(gr, index, &oclass->base);
		if (oclass->base.oclass)
			return index;
		return ret;
	}

	while (gr->func->sclass[c].oclass) {
		if (c++ == index) {
			oclass->base = gr->func->sclass[index];
			return index;
		}
	}

	return c;
}

static int
nvkm_gr_cclass_new(struct nvkm_fifo_chan *chan,
		   const struct nvkm_oclass *oclass,
		   struct nvkm_object **pobject)
{
	struct nvkm_gr *gr = nvkm_gr(oclass->engine);
	if (gr->func->chan_new)
		return gr->func->chan_new(gr, chan, oclass, pobject);
	return 0;
}

static void
nvkm_gr_intr(struct nvkm_engine *engine)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	gr->func->intr(gr);
}

static int
nvkm_gr_oneinit(struct nvkm_engine *engine)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	if (gr->func->oneinit)
		return gr->func->oneinit(gr);
	return 0;
}

static int
nvkm_gr_init(struct nvkm_engine *engine)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	return gr->func->init(gr);
}

static int
nvkm_gr_fini(struct nvkm_engine *engine, bool suspend)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	if (gr->func->fini)
		return gr->func->fini(gr, suspend);
	return 0;
}

static void *
nvkm_gr_dtor(struct nvkm_engine *engine)
{
	struct nvkm_gr *gr = nvkm_gr(engine);
	if (gr->func->dtor)
		return gr->func->dtor(gr);
	return gr;
}

static const struct nvkm_engine_func
nvkm_gr = {
	.dtor = nvkm_gr_dtor,
	.oneinit = nvkm_gr_oneinit,
	.init = nvkm_gr_init,
	.fini = nvkm_gr_fini,
	.intr = nvkm_gr_intr,
	.tile = nvkm_gr_tile,
	.fifo.cclass = nvkm_gr_cclass_new,
	.fifo.sclass = nvkm_gr_oclass_get,
};

int
nvkm_gr_ctor(const struct nvkm_gr_func *func, struct nvkm_device *device,
	     int index, bool enable, struct nvkm_gr *gr)
{
	gr->func = func;
	return nvkm_engine_ctor(&nvkm_gr, device, index, enable, &gr->engine);
}
