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

struct nvkm_engine_func
nvkm_gr = {
	.fifo.cclass = nvkm_gr_cclass_new,
	.fifo.sclass = nvkm_gr_oclass_get,
};

int
nvkm_gr_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, bool enable,
		int length, void **pobject)
{
	struct nvkm_gr *gr;
	int ret;

	ret = nvkm_engine_create_(parent, engine, oclass, enable,
				  "gr", "gr", length, pobject);
	gr = *pobject;
	if (ret)
		return ret;

	gr->engine.func = &nvkm_gr;
	return 0;
}
