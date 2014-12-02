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
gm204_i2c_pad_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_i2c *i2c = (void *)object->engine;
	struct gm204_i2c_pad *pad = (void *)object;
	nv_mask(i2c, 0x00d97c + pad->addr, 0x00000001, 0x00000001);
	return nvkm_i2c_pad_fini(&pad->base, suspend);
}

static int
gm204_i2c_pad_init(struct nouveau_object *object)
{
	struct nouveau_i2c *i2c = (void *)object->engine;
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
gm204_i2c_pad_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		  struct nouveau_oclass *oclass, void *data, u32 index,
		  struct nouveau_object **pobject)
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

struct nouveau_oclass
gm204_i2c_pad_oclass = {
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gm204_i2c_pad_ctor,
		.dtor = _nvkm_i2c_pad_dtor,
		.init = gm204_i2c_pad_init,
		.fini = gm204_i2c_pad_fini,
	},
};
