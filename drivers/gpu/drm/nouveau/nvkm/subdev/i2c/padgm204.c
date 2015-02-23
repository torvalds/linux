/*
 * Copyright 2014 Red Hat Inc.
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
#include "pad.h"

struct gm204_i2c_pad {
	struct nvkm_i2c_pad base;
	int addr;
};

static int
gm204_i2c_pad_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_i2c *i2c = (void *)nvkm_i2c(object);
	struct gm204_i2c_pad *pad = (void *)object;
	nv_mask(i2c, 0x00d97c + pad->addr, 0x00000001, 0x00000001);
	return nvkm_i2c_pad_fini(&pad->base, suspend);
}

static int
gm204_i2c_pad_init(struct nvkm_object *object)
{
	struct nvkm_i2c *i2c = (void *)nvkm_i2c(object);
	struct gm204_i2c_pad *pad = (void *)object;

	switch (nv_oclass(pad->base.next)->handle) {
	case NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_AUX):
		nv_mask(i2c, 0x00d970 + pad->addr, 0x0000c003, 0x00000002);
		break;
	case NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_BIT):
	default:
		nv_mask(i2c, 0x00d970 + pad->addr, 0x0000c003, 0x0000c001);
		break;
	}

	nv_mask(i2c, 0x00d97c + pad->addr, 0x00000001, 0x00000000);
	return nvkm_i2c_pad_init(&pad->base);
}

static int
gm204_i2c_pad_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, void *data, u32 index,
		   struct nvkm_object **pobject)
{
	struct gm204_i2c_pad *pad;
	int ret;

	ret = nvkm_i2c_pad_create(parent, engine, oclass, index, &pad);
	*pobject = nv_object(pad);
	if (ret)
		return ret;

	pad->addr = index * 0x50;;
	return 0;
}

struct nvkm_oclass
gm204_i2c_pad_oclass = {
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_i2c_pad_ctor,
		.dtor = _nvkm_i2c_pad_dtor,
		.init = gm204_i2c_pad_init,
		.fini = gm204_i2c_pad_fini,
	},
};
