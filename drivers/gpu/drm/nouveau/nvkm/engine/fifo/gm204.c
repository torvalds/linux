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
 * Authors: Ben Skeggs
 */
#include "gk104.h"

#include <nvif/class.h>

static struct nvkm_oclass
gm204_fifo_sclass[] = {
	{ MAXWELL_CHANNEL_GPFIFO_A, &gk104_fifo_chan_ofuncs },
	{}
};

static int
gm204_fifo_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	int ret = gk104_fifo_ctor(parent, engine, oclass, data, size, pobject);
	if (ret == 0) {
		struct gk104_fifo_priv *priv = (void *)*pobject;
		nv_engine(priv)->sclass = gm204_fifo_sclass;
	}
	return ret;
}

struct nvkm_oclass *
gm204_fifo_oclass = &(struct gk104_fifo_impl) {
	.base.handle = NV_ENGINE(FIFO, 0x24),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_fifo_ctor,
		.dtor = gk104_fifo_dtor,
		.init = gk104_fifo_init,
		.fini = _nvkm_fifo_fini,
	},
	.channels = 4096,
}.base;
