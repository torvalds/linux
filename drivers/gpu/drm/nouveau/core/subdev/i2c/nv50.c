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

void
nv50_i2c_drive_scl(struct nouveau_i2c_port *base, int state)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	if (state) port->state |= 0x01;
	else	   port->state &= 0xfe;
	nv_wr32(priv, port->addr, port->state);
}

void
nv50_i2c_drive_sda(struct nouveau_i2c_port *base, int state)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	if (state) port->state |= 0x02;
	else	   port->state &= 0xfd;
	nv_wr32(priv, port->addr, port->state);
}

int
nv50_i2c_sense_scl(struct nouveau_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000001);
}

int
nv50_i2c_sense_sda(struct nouveau_i2c_port *base)
{
	struct nv50_i2c_priv *priv = (void *)nv_object(base)->engine;
	struct nv50_i2c_port *port = (void *)base;
	return !!(nv_rd32(priv, port->addr) & 0x00000002);
}

static const struct nouveau_i2c_func
nv50_i2c_func = {
	.drive_scl = nv50_i2c_drive_scl,
	.drive_sda = nv50_i2c_drive_sda,
	.sense_scl = nv50_i2c_sense_scl,
	.sense_sda = nv50_i2c_sense_sda,
};

const u32 nv50_i2c_addr[] = {
	0x00e138, 0x00e150, 0x00e168, 0x00e180,
	0x00e254, 0x00e274, 0x00e764, 0x00e780,
	0x00e79c, 0x00e7b8
};
const int nv50_i2c_addr_nr = ARRAY_SIZE(nv50_i2c_addr);

static int
nv50_i2c_port_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		   struct nouveau_oclass *oclass, void *data, u32 index,
		   struct nouveau_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv50_i2c_port *port;
	int ret;

	ret = nouveau_i2c_port_create(parent, engine, oclass, index,
				      &nouveau_i2c_bit_algo, &nv50_i2c_func,
				      &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	if (info->drive >= nv50_i2c_addr_nr)
		return -EINVAL;

	port->state = 0x00000007;
	port->addr = nv50_i2c_addr[info->drive];
	return 0;
}

int
nv50_i2c_port_init(struct nouveau_object *object)
{
	struct nv50_i2c_priv *priv = (void *)object->engine;
	struct nv50_i2c_port *port = (void *)object;
	nv_wr32(priv, port->addr, port->state);
	return nouveau_i2c_port_init(&port->base);
}

static struct nouveau_oclass
nv50_i2c_sclass[] = {
	{ .handle = NV_I2C_TYPE_DCBI2C(DCB_I2C_NVIO_BIT),
	  .ofuncs = &(struct nouveau_ofuncs) {
		  .ctor = nv50_i2c_port_ctor,
		  .dtor = _nouveau_i2c_port_dtor,
		  .init = nv50_i2c_port_init,
		  .fini = _nouveau_i2c_port_fini,
	  },
	},
	{}
};

static int
nv50_i2c_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	      struct nouveau_oclass *oclass, void *data, u32 size,
	      struct nouveau_object **pobject)
{
	struct nv50_i2c_priv *priv;
	int ret;

	ret = nouveau_i2c_create(parent, engine, oclass, nv50_i2c_sclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	return 0;
}

struct nouveau_oclass
nv50_i2c_oclass = {
	.handle = NV_SUBDEV(I2C, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_i2c_ctor,
		.dtor = _nouveau_i2c_dtor,
		.init = _nouveau_i2c_init,
		.fini = _nouveau_i2c_fini,
	},
};
