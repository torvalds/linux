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

#include <subdev/i2c.h>
#include <subdev/vga.h>

struct nv4e_i2c_priv {
	struct nouveau_i2c base;
};

struct nv4e_i2c_port {
	struct nouveau_i2c_port base;
	u32 addr;
};

static void
nv4e_i2c_drive_scl(struct nouveau_i2c_port *base, int state)
{
	struct nv4e_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv4e_i2c_port *port = (void *)base;
	nv_mask(priv, port->addr, 0x2f, state ? 0x21 : 0x01);
}

static void
nv4e_i2c_drive_sda(struct nouveau_i2c_port *base, int state)
{
	struct nv4e_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv4e_i2c_port *port = (void *)base;
	nv_mask(priv, port->addr, 0x1f, state ? 0x11 : 0x01);
}

static int
nv4e_i2c_sense_scl(struct nouveau_i2c_port *base)
{
	struct nv4e_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv4e_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00040000);
}

static int
nv4e_i2c_sense_sda(struct nouveau_i2c_port *base)
{
	struct nv4e_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv4e_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00080000);
}

static const struct nouveau_i2c_func
nv4e_i2c_func = {
	.drive_scl = nv4e_i2c_drive_scl,
	.drive_sda = nv4e_i2c_drive_sda,
	.sense_scl = nv4e_i2c_sense_scl,
	.sense_sda = nv4e_i2c_sense_sda,
};

static int
nv4e_i2c_port_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		   struct nouveau_oclass *oclass, void *data, u32 index,
		   struct nouveau_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv4e_i2c_port *port;
	int ret;

	ret = nouveau_i2c_port_create(parent, engine, oclass, index,
				     &nouveau_i2c_bit_algo, &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	port->base.func = &nv4e_i2c_func;
	port->addr = 0x600800 + info->drive;
	return 0;
}

static struct nouveau_oclass
nv4e_i2c_sclass[] = {
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NV4E_BIT),
	  .ofuncs = &(struct nouveau_ofuncs) {
		  .ctor = nv4e_i2c_port_ctor,
		  .dtor = _nouveau_i2c_port_dtor,
		  .init = _nouveau_i2c_port_init,
		  .fini = _nouveau_i2c_port_fini,
	  },
	},
	{}
};

static int
nv4e_i2c_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nv4e_i2c_priv *priv;
	int ret;

	ret = nouveau_i2c_create(parent, engine, oclass, nv4e_i2c_sclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass
nv4e_i2c_oclass = {
	.handle = NV_SUBDEV(I2C, 0x4e),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv4e_i2c_ctor,
		.dtor = _nouveau_i2c_dtor,
		.init = _nouveau_i2c_init,
		.fini = _nouveau_i2c_fini,
	},
};
