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

#include <nvif/class.h>

static const struct nvkm_engine_func
g84_mpeg = {
	.cclass = &nv50_mpeg_cclass,
	.sclass = {
		{ -1, -1, G82_MPEG, &nv31_mpeg_object },
		{}
	}
};

static int
g84_mpeg_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nvkm_mpeg *mpeg;
	int ret;

	ret = nvkm_mpeg_create(parent, engine, oclass, &mpeg);
	*pobject = nv_object(mpeg);
	if (ret)
		return ret;

	mpeg->engine.func = &g84_mpeg;

	nv_subdev(mpeg)->unit = 0x00000002;
	nv_subdev(mpeg)->intr = nv50_mpeg_intr;
	return 0;
}

struct nvkm_oclass
g84_mpeg_oclass = {
	.handle = NV_ENGINE(MPEG, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_mpeg_ctor,
		.dtor = _nvkm_mpeg_dtor,
		.init = nv50_mpeg_init,
		.fini = _nvkm_mpeg_fini,
	},
};
