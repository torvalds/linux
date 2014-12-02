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

#define AUX_DBG(fmt, args...) nv_debug(aux, "AUXCH(%d): " fmt, ch, ##args)
#define AUX_ERR(fmt, args...) nv_error(aux, "AUXCH(%d): " fmt, ch, ##args)

static void
auxch_fini(struct nouveau_i2c *aux, int ch)
{
	nv_mask(aux, 0x00d954 + (ch * 0x50), 0x00310000, 0x00000000);
}

static int
auxch_init(struct nouveau_i2c *aux, int ch)
{
	const u32 unksel = 1; /* nfi which to use, or if it matters.. */
	const u32 ureq = unksel ? 0x00100000 : 0x00200000;
	const u32 urep = unksel ? 0x01000000 : 0x02000000;
	u32 ctrl, timeout;

	/* wait up to 1ms for any previous transaction to be done... */
	timeout = 1000;
	do {
		ctrl = nv_rd32(aux, 0x00d954 + (ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR("begin idle timeout 0x%08x\n", ctrl);
			return -EBUSY;
		}
	} while (ctrl & 0x03010000);

	/* set some magic, and wait up to 1ms for it to appear */
	nv_mask(aux, 0x00d954 + (ch * 0x50), 0x00300000, ureq);
	timeout = 1000;
	do {
		ctrl = nv_rd32(aux, 0x00d954 + (ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR("magic wait 0x%08x\n", ctrl);
			auxch_fini(aux, ch);
			return -EBUSY;
		}
	} while ((ctrl & 0x03000000) != urep);

	return 0;
}

int
gm204_aux(struct nouveau_i2c_port *base, bool retry,
	 u8 type, u32 addr, u8 *data, u8 size)
{
	struct nouveau_i2c *aux = nouveau_i2c(base);
	struct nv50_i2c_port *port = (void *)base;
	u32 ctrl, stat, timeout, retries;
	u32 xbuf[4] = {};
	int ch = port->addr;
	int ret, i;

	AUX_DBG("%d: 0x%08x %d\n", type, addr, size);

	ret = auxch_init(aux, ch);
	if (ret)
		goto out;

	stat = nv_rd32(aux, 0x00d958 + (ch * 0x50));
	if (!(stat & 0x10000000)) {
		AUX_DBG("sink not detected\n");
		ret = -ENXIO;
		goto out;
	}

	if (!(type & 1)) {
		memcpy(xbuf, data, size);
		for (i = 0; i < 16; i += 4) {
			AUX_DBG("wr 0x%08x\n", xbuf[i / 4]);
			nv_wr32(aux, 0x00d930 + (ch * 0x50) + i, xbuf[i / 4]);
		}
	}

	ctrl  = nv_rd32(aux, 0x00d954 + (ch * 0x50));
	ctrl &= ~0x0001f0ff;
	ctrl |= type << 12;
	ctrl |= size - 1;
	nv_wr32(aux, 0x00d950 + (ch * 0x50), addr);

	/* (maybe) retry transaction a number of times on failure... */
	for (retries = 0; !ret && retries < 32; retries++) {
		/* reset, and delay a while if this is a retry */
		nv_wr32(aux, 0x00d954 + (ch * 0x50), 0x80000000 | ctrl);
		nv_wr32(aux, 0x00d954 + (ch * 0x50), 0x00000000 | ctrl);
		if (retries)
			udelay(400);

		/* transaction request, wait up to 1ms for it to complete */
		nv_wr32(aux, 0x00d954 + (ch * 0x50), 0x00010000 | ctrl);

		timeout = 1000;
		do {
			ctrl = nv_rd32(aux, 0x00d954 + (ch * 0x50));
			udelay(1);
			if (!timeout--) {
				AUX_ERR("tx req timeout 0x%08x\n", ctrl);
				ret = -EIO;
				goto out;
			}
		} while (ctrl & 0x00010000);
		ret = 1;

		/* read status, and check if transaction completed ok */
		stat = nv_mask(aux, 0x00d958 + (ch * 0x50), 0, 0);
		if ((stat & 0x000f0000) == 0x00080000 ||
		    (stat & 0x000f0000) == 0x00020000)
			ret = retry ? 0 : 1;
		if ((stat & 0x00000100))
			ret = -ETIMEDOUT;
		if ((stat & 0x00000e00))
			ret = -EIO;

		AUX_DBG("%02d 0x%08x 0x%08x\n", retries, ctrl, stat);
	}

	if (type & 1) {
		for (i = 0; i < 16; i += 4) {
			xbuf[i / 4] = nv_rd32(aux, 0x00d940 + (ch * 0x50) + i);
			AUX_DBG("rd 0x%08x\n", xbuf[i / 4]);
		}
		memcpy(data, xbuf, size);
	}

out:
	auxch_fini(aux, ch);
	return ret < 0 ? ret : (stat & 0x000f0000) >> 16;
}

static const struct nouveau_i2c_func
gm204_aux_func = {
	.aux       = gm204_aux,
};

int
gm204_aux_port_ctor(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass, void *data, u32 index,
		    struct nouveau_object **pobject)
{
	struct dcb_i2c_entry *info = data;
	struct nv50_i2c_port *port;
	int ret;

	ret = nouveau_i2c_port_create(parent, engine, oclass, index,
				      &nouveau_i2c_aux_algo, &gm204_aux_func,
				      &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	port->base.aux = info->auxch;
	port->addr = info->auxch;
	return 0;
}

struct nouveau_oclass
gm204_i2c_sclass[] = {
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
		  .ctor = gm204_aux_port_ctor,
		  .dtor = _nouveau_i2c_port_dtor,
		  .init = _nouveau_i2c_port_init,
		  .fini = _nouveau_i2c_port_fini,
	  },
	},
	{}
};

struct nouveau_oclass *
gm204_i2c_oclass = &(struct nouveau_i2c_impl) {
	.base.handle = NV_SUBDEV(I2C, 0x24),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = _nouveau_i2c_ctor,
		.dtor = _nouveau_i2c_dtor,
		.init = _nouveau_i2c_init,
		.fini = _nouveau_i2c_fini,
	},
	.sclass = gm204_i2c_sclass,
	.pad_x = &nv04_i2c_pad_oclass,
	.pad_s = &gm204_i2c_pad_oclass,
	.aux = 8,
	.aux_stat = nve0_aux_stat,
	.aux_mask = nve0_aux_mask,
}.base;
