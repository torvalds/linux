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
 * Authors: Ben Skeggs, Maarten Lankhorst, Ilia Mirkin
 */
#include <engine/mspdec.h>
#include <engine/falcon.h>

#include <nvif/class.h>

static int
g98_mspdec_init(struct nvkm_object *object)
{
	struct nvkm_falcon *mspdec = (void *)object;
	struct nvkm_device *device = mspdec->engine.subdev.device;
	int ret;

	ret = nvkm_falcon_init(mspdec);
	if (ret)
		return ret;

	nvkm_wr32(device, 0x085010, 0x0000ffd2);
	nvkm_wr32(device, 0x08501c, 0x0000fff2);
	return 0;
}

static const struct nvkm_falcon_func
g98_mspdec_func = {
	.sclass = {
		{ -1, -1, G98_MSPDEC },
		{ -1, -1, GT212_MSPDEC },
		{}
	},
};

static int
g98_mspdec_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_falcon *mspdec;
	int ret;

	ret = nvkm_falcon_create(&g98_mspdec_func, parent, engine, oclass,
				  0x085000, true, "PMSPDEC", "mspdec", &mspdec);
	*pobject = nv_object(mspdec);
	if (ret)
		return ret;

	nv_subdev(mspdec)->unit = 0x01020000;
	return 0;
}

struct nvkm_oclass
g98_mspdec_oclass = {
	.handle = NV_ENGINE(MSPDEC, 0x98),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g98_mspdec_ctor,
		.dtor = _nvkm_falcon_dtor,
		.init = g98_mspdec_init,
		.fini = _nvkm_falcon_fini,
	},
};
