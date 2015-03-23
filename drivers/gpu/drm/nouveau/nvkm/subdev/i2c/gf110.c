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

static int
gf110_i2c_sense_scl(struct nvkm_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000010);
}

static int
gf110_i2c_sense_sda(struct nvkm_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000020);
}

static const struct nvkm_i2c_func
gf110_i2c_func = {
	.drive_scl = nv50_i2c_drive_scl,
	.drive_sda = nv50_i2c_drive_sda,
	.sense_scl = gf110_i2c_sense_scl,
	.sense_sda = gf110_i2c_sense_sda,
};

int
gf110_i2c_port_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 index,
		    struct nvkm_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv50_i2c_port *port;
	int ret;

	ret = nvkm_i2c_port_create(parent, engine, oclass, index,
				   &nvkm_i2c_bit_algo, &gf110_i2c_func, &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	port->state = 0x00000007;
	port->addr = 0x00d014 + (info->drive * 0x20);
	return 0;
}

struct nvkm_oclass
gf110_i2c_sclass[] = {
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_BIT),
	  .ofuncs = &(struct nvkm_ofuncs) {
		  .ctor = gf110_i2c_port_ctor,
		  .dtor = _nvkm_i2c_port_dtor,
		  .init = nv50_i2c_port_init,
		  .fini = _nvkm_i2c_port_fini,
	  },
	},
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_AUX),
	  .ofuncs = &(struct nvkm_ofuncs) {
		  .ctor = g94_aux_port_ctor,
		  .dtor = _nvkm_i2c_port_dtor,
		  .init = _nvkm_i2c_port_init,
		  .fini = _nvkm_i2c_port_fini,
	  },
	},
	{}
};

struct nvkm_oclass *
gf110_i2c_oclass = &(struct nvkm_i2c_impl) {
	.base.handle = NV_SUBDEV(I2C, 0xd0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_i2c_ctor,
		.dtor = _nvkm_i2c_dtor,
		.init = _nvkm_i2c_init,
		.fini = _nvkm_i2c_fini,
	},
	.sclass = gf110_i2c_sclass,
	.pad_x = &nv04_i2c_pad_oclass,
	.pad_s = &g94_i2c_pad_oclass,
	.aux = 4,
	.aux_stat = g94_aux_stat,
	.aux_mask = g94_aux_mask,
}.base;
