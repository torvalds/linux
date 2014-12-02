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
nvd0_i2c_sense_scl(struct nouveau_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000010);
}

static int
nvd0_i2c_sense_sda(struct nouveau_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000020);
}

static const struct nouveau_i2c_func
nvd0_i2c_func = {
	.drive_scl = nv50_i2c_drive_scl,
	.drive_sda = nv50_i2c_drive_sda,
	.sense_scl = nvd0_i2c_sense_scl,
	.sense_sda = nvd0_i2c_sense_sda,
};

int
nvd0_i2c_port_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		   struct nouveau_oclass *oclass, void *data, u32 index,
		   struct nouveau_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv50_i2c_port *port;
	int ret;

	ret = nouveau_i2c_port_create(parent, engine, oclass, index,
				      &nouveau_i2c_bit_algo, &nvd0_i2c_func,
				      &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	port->state = 0x00000007;
	port->addr = 0x00d014 + (info->drive * 0x20);
	return 0;
}

struct nouveau_oclass
nvd0_i2c_sclass[] = {
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_BIT),
	  .ofuncs = &(struct nouveau_ofuncs) {
		  .ctor = nvd0_i2c_port_ctor,
		  .dtor = _nouveau_i2c_port_dtor,
		  .init = nv50_i2c_port_init,
		  .fini = _nouveau_i2c_port_fini,
	  },
	},
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_AUX),
	  .ofuncs = &(struct nouveau_ofuncs) {
		  .ctor = nv94_aux_port_ctor,
		  .dtor = _nouveau_i2c_port_dtor,
		  .init = _nouveau_i2c_port_init,
		  .fini = _nouveau_i2c_port_fini,
	  },
	},
	{}
};

struct nouveau_oclass *
nvd0_i2c_oclass = &(struct nouveau_i2c_impl) {
	.base.handle = NV_SUBDEV(I2C, 0xd0),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_i2c_ctor,
		.dtor = _nouveau_i2c_dtor,
		.init = _nouveau_i2c_init,
		.fini = _nouveau_i2c_fini,
	},
	.sclass = nvd0_i2c_sclass,
	.pad_x = &nv04_i2c_pad_oclass,
	.pad_s = &nv94_i2c_pad_oclass,
	.aux = 4,
	.aux_stat = nv94_aux_stat,
	.aux_mask = nv94_aux_mask,
}.base;
