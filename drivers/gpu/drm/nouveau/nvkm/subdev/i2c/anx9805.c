/*
 * Copyright 2013 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "port.h"

struct anx9805_i2c_port {
	struct nvkm_i2c_port base;
	u32 addr;
	u32 ctrl;
};

static int
anx9805_train(struct nvkm_i2c_port *port, int link_nr, int link_bw, bool enh)
{
	struct anx9805_i2c_port *chan = (void *)port;
	struct nvkm_i2c_port *mast = (void *)nv_object(chan)->parent;
	u8 tmp, i;

	DBG("ANX9805 train %d 0x%02x %d\n", link_nr, link_bw, enh);

	nv_wri2cr(mast, chan->addr, 0xa0, link_bw);
	nv_wri2cr(mast, chan->addr, 0xa1, link_nr | (enh ? 0x80 : 0x00));
	nv_wri2cr(mast, chan->addr, 0xa2, 0x01);
	nv_wri2cr(mast, chan->addr, 0xa8, 0x01);

	i = 0;
	while ((tmp = nv_rdi2cr(mast, chan->addr, 0xa8)) & 0x01) {
		mdelay(5);
		if (i++ == 100) {
			nv_error(port, "link training timed out\n");
			return -ETIMEDOUT;
		}
	}

	if (tmp & 0x70) {
		nv_error(port, "link training failed: 0x%02x\n", tmp);
		return -EIO;
	}

	return 1;
}

static int
anx9805_aux(struct nvkm_i2c_port *port, bool retry,
	    u8 type, u32 addr, u8 *data, u8 size)
{
	struct anx9805_i2c_port *chan = (void *)port;
	struct nvkm_i2c_port *mast = (void *)nv_object(chan)->parent;
	int i, ret = -ETIMEDOUT;
	u8 buf[16] = {};
	u8 tmp;

	DBG("%02x %05x %d\n", type, addr, size);

	tmp = nv_rdi2cr(mast, chan->ctrl, 0x07) & ~0x04;
	nv_wri2cr(mast, chan->ctrl, 0x07, tmp | 0x04);
	nv_wri2cr(mast, chan->ctrl, 0x07, tmp);
	nv_wri2cr(mast, chan->ctrl, 0xf7, 0x01);

	nv_wri2cr(mast, chan->addr, 0xe4, 0x80);
	if (!(type & 1)) {
		memcpy(buf, data, size);
		DBG("%16ph", buf);
		for (i = 0; i < size; i++)
			nv_wri2cr(mast, chan->addr, 0xf0 + i, buf[i]);
	}
	nv_wri2cr(mast, chan->addr, 0xe5, ((size - 1) << 4) | type);
	nv_wri2cr(mast, chan->addr, 0xe6, (addr & 0x000ff) >>  0);
	nv_wri2cr(mast, chan->addr, 0xe7, (addr & 0x0ff00) >>  8);
	nv_wri2cr(mast, chan->addr, 0xe8, (addr & 0xf0000) >> 16);
	nv_wri2cr(mast, chan->addr, 0xe9, 0x01);

	i = 0;
	while ((tmp = nv_rdi2cr(mast, chan->addr, 0xe9)) & 0x01) {
		mdelay(5);
		if (i++ == 32)
			goto done;
	}

	if ((tmp = nv_rdi2cr(mast, chan->ctrl, 0xf7)) & 0x01) {
		ret = -EIO;
		goto done;
	}

	if (type & 1) {
		for (i = 0; i < size; i++)
			buf[i] = nv_rdi2cr(mast, chan->addr, 0xf0 + i);
		DBG("%16ph", buf);
		memcpy(data, buf, size);
	}

	ret = 0;
done:
	nv_wri2cr(mast, chan->ctrl, 0xf7, 0x01);
	return ret;
}

static const struct nvkm_i2c_func
anx9805_aux_func = {
	.aux = anx9805_aux,
	.lnk_ctl = anx9805_train,
};

static int
anx9805_aux_chan_ctor(struct nvkm_object *parent,
		      struct nvkm_object *engine,
		      struct nvkm_oclass *oclass, void *data, u32 index,
		      struct nvkm_object **pobject)
{
	struct nvkm_i2c_port *mast = (void *)parent;
	struct anx9805_i2c_port *chan;
	int ret;

	ret = nvkm_i2c_port_create(parent, engine, oclass, index,
				   &nvkm_i2c_aux_algo, &anx9805_aux_func,
				   &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	switch ((oclass->handle & 0xff00) >> 8) {
	case 0x0d:
		chan->addr = 0x38;
		chan->ctrl = 0x39;
		break;
	case 0x0e:
		chan->addr = 0x3c;
		chan->ctrl = 0x3b;
		break;
	default:
		BUG_ON(1);
	}

	if (mast->adapter.algo == &i2c_bit_algo) {
		struct i2c_algo_bit_data *algo = mast->adapter.algo_data;
		algo->udelay = max(algo->udelay, 40);
	}

	return 0;
}

static struct nvkm_ofuncs
anx9805_aux_ofuncs = {
	.ctor =  anx9805_aux_chan_ctor,
	.dtor = _nvkm_i2c_port_dtor,
	.init = _nvkm_i2c_port_init,
	.fini = _nvkm_i2c_port_fini,
};

static int
anx9805_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct anx9805_i2c_port *port = adap->algo_data;
	struct nvkm_i2c_port *mast = (void *)nv_object(port)->parent;
	struct i2c_msg *msg = msgs;
	int ret = -ETIMEDOUT;
	int i, j, cnt = num;
	u8 seg = 0x00, off = 0x00, tmp;

	tmp = nv_rdi2cr(mast, port->ctrl, 0x07) & ~0x10;
	nv_wri2cr(mast, port->ctrl, 0x07, tmp | 0x10);
	nv_wri2cr(mast, port->ctrl, 0x07, tmp);
	nv_wri2cr(mast, port->addr, 0x43, 0x05);
	mdelay(5);

	while (cnt--) {
		if ( (msg->flags & I2C_M_RD) && msg->addr == 0x50) {
			nv_wri2cr(mast, port->addr, 0x40, msg->addr << 1);
			nv_wri2cr(mast, port->addr, 0x41, seg);
			nv_wri2cr(mast, port->addr, 0x42, off);
			nv_wri2cr(mast, port->addr, 0x44, msg->len);
			nv_wri2cr(mast, port->addr, 0x45, 0x00);
			nv_wri2cr(mast, port->addr, 0x43, 0x01);
			for (i = 0; i < msg->len; i++) {
				j = 0;
				while (nv_rdi2cr(mast, port->addr, 0x46) & 0x10) {
					mdelay(5);
					if (j++ == 32)
						goto done;
				}
				msg->buf[i] = nv_rdi2cr(mast, port->addr, 0x47);
			}
		} else
		if (!(msg->flags & I2C_M_RD)) {
			if (msg->addr == 0x50 && msg->len == 0x01) {
				off = msg->buf[0];
			} else
			if (msg->addr == 0x30 && msg->len == 0x01) {
				seg = msg->buf[0];
			} else
				goto done;
		} else {
			goto done;
		}
		msg++;
	}

	ret = num;
done:
	nv_wri2cr(mast, port->addr, 0x43, 0x00);
	return ret;
}

static u32
anx9805_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm
anx9805_i2c_algo = {
	.master_xfer = anx9805_xfer,
	.functionality = anx9805_func
};

static const struct nvkm_i2c_func
anx9805_i2c_func = {
};

static int
anx9805_ddc_port_ctor(struct nvkm_object *parent,
		      struct nvkm_object *engine,
		      struct nvkm_oclass *oclass, void *data, u32 index,
		      struct nvkm_object **pobject)
{
	struct nvkm_i2c_port *mast = (void *)parent;
	struct anx9805_i2c_port *port;
	int ret;

	ret = nvkm_i2c_port_create(parent, engine, oclass, index,
				   &anx9805_i2c_algo, &anx9805_i2c_func, &port);
	*pobject = nv_object(port);
	if (ret)
		return ret;

	switch ((oclass->handle & 0xff00) >> 8) {
	case 0x0d:
		port->addr = 0x3d;
		port->ctrl = 0x39;
		break;
	case 0x0e:
		port->addr = 0x3f;
		port->ctrl = 0x3b;
		break;
	default:
		BUG_ON(1);
	}

	if (mast->adapter.algo == &i2c_bit_algo) {
		struct i2c_algo_bit_data *algo = mast->adapter.algo_data;
		algo->udelay = max(algo->udelay, 40);
	}

	return 0;
}

static struct nvkm_ofuncs
anx9805_ddc_ofuncs = {
	.ctor =  anx9805_ddc_port_ctor,
	.dtor = _nvkm_i2c_port_dtor,
	.init = _nvkm_i2c_port_init,
	.fini = _nvkm_i2c_port_fini,
};

struct nvkm_oclass
nvkm_anx9805_sclass[] = {
	{ .handle = NV_I2C_TYPE_EXTDDC(0x0d), .ofuncs = &anx9805_ddc_ofuncs },
	{ .handle = NV_I2C_TYPE_EXTAUX(0x0d), .ofuncs = &anx9805_aux_ofuncs },
	{ .handle = NV_I2C_TYPE_EXTDDC(0x0e), .ofuncs = &anx9805_ddc_ofuncs },
	{ .handle = NV_I2C_TYPE_EXTAUX(0x0e), .ofuncs = &anx9805_aux_ofuncs },
	{}
};
