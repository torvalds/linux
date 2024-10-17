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
#define anx9805_pad(p) container_of((p), struct anx9805_pad, base)
#define anx9805_bus(p) container_of((p), struct anx9805_bus, base)
#define anx9805_aux(p) container_of((p), struct anx9805_aux, base)
#include "auxch.h"
#include "bus.h"

struct anx9805_pad {
	struct nvkm_i2c_pad base;
	struct nvkm_i2c_bus *bus;
	u8 addr;
};

struct anx9805_bus {
	struct nvkm_i2c_bus base;
	struct anx9805_pad *pad;
	u8 addr;
};

static int
anx9805_bus_xfer(struct nvkm_i2c_bus *base, struct i2c_msg *msgs, int num)
{
	struct anx9805_bus *bus = anx9805_bus(base);
	struct anx9805_pad *pad = bus->pad;
	struct i2c_adapter *adap = &pad->bus->i2c;
	struct i2c_msg *msg = msgs;
	int ret = -ETIMEDOUT;
	int i, j, cnt = num;
	u8 seg = 0x00, off = 0x00, tmp;

	tmp = nvkm_rdi2cr(adap, pad->addr, 0x07) & ~0x10;
	nvkm_wri2cr(adap, pad->addr, 0x07, tmp | 0x10);
	nvkm_wri2cr(adap, pad->addr, 0x07, tmp);
	nvkm_wri2cr(adap, bus->addr, 0x43, 0x05);
	mdelay(5);

	while (cnt--) {
		if ( (msg->flags & I2C_M_RD) && msg->addr == 0x50) {
			nvkm_wri2cr(adap, bus->addr, 0x40, msg->addr << 1);
			nvkm_wri2cr(adap, bus->addr, 0x41, seg);
			nvkm_wri2cr(adap, bus->addr, 0x42, off);
			nvkm_wri2cr(adap, bus->addr, 0x44, msg->len);
			nvkm_wri2cr(adap, bus->addr, 0x45, 0x00);
			nvkm_wri2cr(adap, bus->addr, 0x43, 0x01);
			for (i = 0; i < msg->len; i++) {
				j = 0;
				while (nvkm_rdi2cr(adap, bus->addr, 0x46) & 0x10) {
					mdelay(5);
					if (j++ == 32)
						goto done;
				}
				msg->buf[i] = nvkm_rdi2cr(adap, bus->addr, 0x47);
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
	nvkm_wri2cr(adap, bus->addr, 0x43, 0x00);
	return ret;
}

static const struct nvkm_i2c_bus_func
anx9805_bus_func = {
	.xfer = anx9805_bus_xfer,
};

static int
anx9805_bus_new(struct nvkm_i2c_pad *base, int id, u8 drive,
		struct nvkm_i2c_bus **pbus)
{
	struct anx9805_pad *pad = anx9805_pad(base);
	struct anx9805_bus *bus;
	int ret;

	if (!(bus = kzalloc(sizeof(*bus), GFP_KERNEL)))
		return -ENOMEM;
	*pbus = &bus->base;
	bus->pad = pad;

	ret = nvkm_i2c_bus_ctor(&anx9805_bus_func, &pad->base, id, &bus->base);
	if (ret)
		return ret;

	switch (pad->addr) {
	case 0x39: bus->addr = 0x3d; break;
	case 0x3b: bus->addr = 0x3f; break;
	default:
		return -ENOSYS;
	}

	return 0;
}

struct anx9805_aux {
	struct nvkm_i2c_aux base;
	struct anx9805_pad *pad;
	u8 addr;
};

static int
anx9805_aux_xfer(struct nvkm_i2c_aux *base, bool retry,
		 u8 type, u32 addr, u8 *data, u8 *size)
{
	struct anx9805_aux *aux = anx9805_aux(base);
	struct anx9805_pad *pad = aux->pad;
	struct i2c_adapter *adap = &pad->bus->i2c;
	int i, ret = -ETIMEDOUT;
	u8 buf[16] = {};
	u8 tmp;

	AUX_DBG(&aux->base, "%02x %05x %d", type, addr, *size);

	tmp = nvkm_rdi2cr(adap, pad->addr, 0x07) & ~0x04;
	nvkm_wri2cr(adap, pad->addr, 0x07, tmp | 0x04);
	nvkm_wri2cr(adap, pad->addr, 0x07, tmp);
	nvkm_wri2cr(adap, pad->addr, 0xf7, 0x01);

	nvkm_wri2cr(adap, aux->addr, 0xe4, 0x80);
	if (!(type & 1)) {
		memcpy(buf, data, *size);
		AUX_DBG(&aux->base, "%16ph", buf);
		for (i = 0; i < *size; i++)
			nvkm_wri2cr(adap, aux->addr, 0xf0 + i, buf[i]);
	}
	nvkm_wri2cr(adap, aux->addr, 0xe5, ((*size - 1) << 4) | type);
	nvkm_wri2cr(adap, aux->addr, 0xe6, (addr & 0x000ff) >>  0);
	nvkm_wri2cr(adap, aux->addr, 0xe7, (addr & 0x0ff00) >>  8);
	nvkm_wri2cr(adap, aux->addr, 0xe8, (addr & 0xf0000) >> 16);
	nvkm_wri2cr(adap, aux->addr, 0xe9, 0x01);

	i = 0;
	while ((tmp = nvkm_rdi2cr(adap, aux->addr, 0xe9)) & 0x01) {
		mdelay(5);
		if (i++ == 32)
			goto done;
	}

	if ((tmp = nvkm_rdi2cr(adap, pad->addr, 0xf7)) & 0x01) {
		ret = -EIO;
		goto done;
	}

	if (type & 1) {
		for (i = 0; i < *size; i++)
			buf[i] = nvkm_rdi2cr(adap, aux->addr, 0xf0 + i);
		AUX_DBG(&aux->base, "%16ph", buf);
		memcpy(data, buf, *size);
	}

	ret = 0;
done:
	nvkm_wri2cr(adap, pad->addr, 0xf7, 0x01);
	return ret;
}

static int
anx9805_aux_lnk_ctl(struct nvkm_i2c_aux *base,
		    int link_nr, int link_bw, bool enh)
{
	struct anx9805_aux *aux = anx9805_aux(base);
	struct anx9805_pad *pad = aux->pad;
	struct i2c_adapter *adap = &pad->bus->i2c;
	u8 tmp, i;

	AUX_DBG(&aux->base, "ANX9805 train %d %02x %d",
		link_nr, link_bw, enh);

	nvkm_wri2cr(adap, aux->addr, 0xa0, link_bw);
	nvkm_wri2cr(adap, aux->addr, 0xa1, link_nr | (enh ? 0x80 : 0x00));
	nvkm_wri2cr(adap, aux->addr, 0xa2, 0x01);
	nvkm_wri2cr(adap, aux->addr, 0xa8, 0x01);

	i = 0;
	while ((tmp = nvkm_rdi2cr(adap, aux->addr, 0xa8)) & 0x01) {
		mdelay(5);
		if (i++ == 100) {
			AUX_ERR(&aux->base, "link training timeout");
			return -ETIMEDOUT;
		}
	}

	if (tmp & 0x70) {
		AUX_ERR(&aux->base, "link training failed");
		return -EIO;
	}

	return 0;
}

static const struct nvkm_i2c_aux_func
anx9805_aux_func = {
	.xfer = anx9805_aux_xfer,
	.lnk_ctl = anx9805_aux_lnk_ctl,
};

static int
anx9805_aux_new(struct nvkm_i2c_pad *base, int id, u8 drive,
		struct nvkm_i2c_aux **pbus)
{
	struct anx9805_pad *pad = anx9805_pad(base);
	struct anx9805_aux *aux;
	int ret;

	if (!(aux = kzalloc(sizeof(*aux), GFP_KERNEL)))
		return -ENOMEM;
	*pbus = &aux->base;
	aux->pad = pad;

	ret = nvkm_i2c_aux_ctor(&anx9805_aux_func, &pad->base, id, &aux->base);
	if (ret)
		return ret;

	switch (pad->addr) {
	case 0x39: aux->addr = 0x38; break;
	case 0x3b: aux->addr = 0x3c; break;
	default:
		return -ENOSYS;
	}

	return 0;
}

static const struct nvkm_i2c_pad_func
anx9805_pad_func = {
	.bus_new_4 = anx9805_bus_new,
	.aux_new_6 = anx9805_aux_new,
};

int
anx9805_pad_new(struct nvkm_i2c_bus *bus, int id, u8 addr,
		struct nvkm_i2c_pad **ppad)
{
	struct anx9805_pad *pad;

	if (!(pad = kzalloc(sizeof(*pad), GFP_KERNEL)))
		return -ENOMEM;
	*ppad = &pad->base;

	nvkm_i2c_pad_ctor(&anx9805_pad_func, bus->pad->i2c, id, &pad->base);
	pad->bus = bus;
	pad->addr = addr;
	return 0;
}
