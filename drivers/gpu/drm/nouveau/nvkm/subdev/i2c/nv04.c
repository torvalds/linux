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

#include <subdev/vga.h>

struct nv04_i2c_priv {
	struct nvkm_i2c base;
};

struct nv04_i2c_port {
	struct nvkm_i2c_port base;
	u8 drive;
	u8 sense;
};

static void
nv04_i2c_drive_scl(struct nvkm_i2c_port *base, int state)
{
	struct nv04_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv04_i2c_port *port = (void *)base;
	u8 val = nv_rdvgac(priv, 0, port->drive);
	if (state) val |= 0x20;
	else	   val &= 0xdf;
	nv_wrvgac(priv, 0, port->drive, val | 0x01);
}

static void
nv04_i2c_drive_sda(struct nvkm_i2c_port *base, int state)
{
	struct nv04_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv04_i2c_port *port = (void *)base;
	u8 val = nv_rdvgac(priv, 0, port->drive);
	if (state) val |= 0x10;
	else	   val &= 0xef;
	nv_wrvgac(priv, 0, port->drive, val | 0x01);
}

static int
nv04_i2c_sense_scl(struct nvkm_i2c_port *base)
{
	struct nv04_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv04_i2c_port *port = (void *)base;
	return !!(nv_rdvgac(priv, 0, port->sense) & 0x04);
}

static int
nv04_i2c_sense_sda(struct nvkm_i2c_port *base)
{
	struct nv04_i2c_priv *priv = (void *)nvkm_i2c(base);
	struct nv04_i2c_port *port = (void *)base;
	return !!(nv_rdvgac(priv, 0, port->sense) & 0x08);
}

static const struct nvkm_i2c_func
nv04_i2c_func = {
	.drive_scl = nv04_i2c_drive_scl,
	.drive_sda = nv04_i2c_drive_sda,
	.sense_scl = nv04_i2c_sense_scl,
	.sense_sda = nv04_i2c_sense_sda,
};

static int
nv04_i2c_port_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, void *data, u32 index,
		   struct nvkm_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv04_i2c_port *port;
	int ret;

	ret = nvkm_i2c_port_create(parent, engine, oclass, index,
				   &nvkm_i2c_bit_algo, &nv04_i2c_func, &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	port->drive = info->drive;
	port->sense = info->sense;
	return 0;
}

static struct nvkm_oclass
nv04_i2c_sclass[] = {
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NV04_BIT),
	  .ofuncs = &(struct nvkm_ofuncs) {
		  .ctor = nv04_i2c_port_ctor,
		  .dtor = _nvkm_i2c_port_dtor,
		  .init = _nvkm_i2c_port_init,
		  .fini = _nvkm_i2c_port_fini,
	  },
	},
	{}
};

struct nvkm_oclass *
nv04_i2c_oclass = &(struct nvkm_i2c_impl) {
	.base.handle = NV_SUBDEV(I2C, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_i2c_ctor,
		.dtor = _nvkm_i2c_dtor,
		.init = _nvkm_i2c_init,
		.fini = _nvkm_i2c_fini,
	},
	.sclass = nv04_i2c_sclass,
	.pad_x = &nv04_i2c_pad_oclass,
}.base;
