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
 * Authors: Ben Skeggs, Ilia Mirkin
 */
#include <engine/vp.h>

#include <nvif/class.h>

static const struct nvkm_xtensa_func
g84_vp_func = {
	.sclass = {
		{ -1, -1, NV74_VP2 },
		{}
	}
};

static int
g84_vp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	    struct nvkm_oclass *oclass, void *data, u32 size,
	    struct nvkm_object **pobject)
{
	struct nvkm_xtensa *vp;
	int ret;

	ret = nvkm_xtensa_create(parent, engine, oclass, 0xf000, true,
				 "PVP", "vp", &vp);
	*pobject = nv_object(vp);
	if (ret)
		return ret;

	vp->func = &g84_vp_func;
	nv_subdev(vp)->unit = 0x01020000;
	vp->fifo_val = 0x111;
	vp->unkd28 = 0x9c544;
	return 0;
}

struct nvkm_oclass
g84_vp_oclass = {
	.handle = NV_ENGINE(VP, 0x84),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g84_vp_ctor,
		.dtor = _nvkm_xtensa_dtor,
		.init = _nvkm_xtensa_init,
		.fini = _nvkm_xtensa_fini,
	},
};
