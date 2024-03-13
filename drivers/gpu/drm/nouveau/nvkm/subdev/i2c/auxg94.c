/*
 * Copyright 2015 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial busions of the Software.
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
#define g94_i2c_aux(p) container_of((p), struct g94_i2c_aux, base)
#include "aux.h"

struct g94_i2c_aux {
	struct nvkm_i2c_aux base;
	int ch;
};

static void
g94_i2c_aux_fini(struct g94_i2c_aux *aux)
{
	struct nvkm_device *device = aux->base.pad->i2c->subdev.device;
	nvkm_mask(device, 0x00e4e4 + (aux->ch * 0x50), 0x00310000, 0x00000000);
}

static int
g94_i2c_aux_init(struct g94_i2c_aux *aux)
{
	struct nvkm_device *device = aux->base.pad->i2c->subdev.device;
	const u32 unksel = 1; /* nfi which to use, or if it matters.. */
	const u32 ureq = unksel ? 0x00100000 : 0x00200000;
	const u32 urep = unksel ? 0x01000000 : 0x02000000;
	u32 ctrl, timeout;

	/* wait up to 1ms for any previous transaction to be done... */
	timeout = 1000;
	do {
		ctrl = nvkm_rd32(device, 0x00e4e4 + (aux->ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR(&aux->base, "begin idle timeout %08x", ctrl);
			return -EBUSY;
		}
	} while (ctrl & 0x03010000);

	/* set some magic, and wait up to 1ms for it to appear */
	nvkm_mask(device, 0x00e4e4 + (aux->ch * 0x50), 0x00300000, ureq);
	timeout = 1000;
	do {
		ctrl = nvkm_rd32(device, 0x00e4e4 + (aux->ch * 0x50));
		udelay(1);
		if (!timeout--) {
			AUX_ERR(&aux->base, "magic wait %08x", ctrl);
			g94_i2c_aux_fini(aux);
			return -EBUSY;
		}
	} while ((ctrl & 0x03000000) != urep);

	return 0;
}

int
g94_i2c_aux_xfer(struct nvkm_i2c_aux *obj, bool retry,
		 u8 type, u32 addr, u8 *data, u8 *size)
{
	struct g94_i2c_aux *aux = g94_i2c_aux(obj);
	struct nvkm_i2c *i2c = aux->base.pad->i2c;
	struct nvkm_device *device = i2c->subdev.device;
	const u32 base = aux->ch * 0x50;
	u32 ctrl, stat, timeout, retries = 0;
	u32 xbuf[4] = {};
	int ret, i;

	AUX_TRACE(&aux->base, "%d: %08x %d", type, addr, *size);

	ret = g94_i2c_aux_init(aux);
	if (ret < 0)
		goto out;

	stat = nvkm_rd32(device, 0x00e4e8 + base);
	if (!(stat & 0x10000000)) {
		AUX_TRACE(&aux->base, "sink not detected");
		ret = -ENXIO;
		goto out;
	}

	nvkm_i2c_aux_autodpcd(i2c, aux->ch, false);

	if (!(type & 1)) {
		memcpy(xbuf, data, *size);
		for (i = 0; i < 16; i += 4) {
			AUX_TRACE(&aux->base, "wr %08x", xbuf[i / 4]);
			nvkm_wr32(device, 0x00e4c0 + base + i, xbuf[i / 4]);
		}
	}

	ctrl  = nvkm_rd32(device, 0x00e4e4 + base);
	ctrl &= ~0x0001f1ff;
	ctrl |= type << 12;
	ctrl |= (*size ? (*size - 1) : 0x00000100);
	nvkm_wr32(device, 0x00e4e0 + base, addr);

	/* (maybe) retry transaction a number of times on failure... */
	do {
		/* reset, and delay a while if this is a retry */
		nvkm_wr32(device, 0x00e4e4 + base, 0x80000000 | ctrl);
		nvkm_wr32(device, 0x00e4e4 + base, 0x00000000 | ctrl);
		if (retries)
			udelay(400);

		/* transaction request, wait up to 2ms for it to complete */
		nvkm_wr32(device, 0x00e4e4 + base, 0x00010000 | ctrl);

		timeout = 2000;
		do {
			ctrl = nvkm_rd32(device, 0x00e4e4 + base);
			udelay(1);
			if (!timeout--) {
				AUX_ERR(&aux->base, "timeout %08x", ctrl);
				ret = -EIO;
				goto out_err;
			}
		} while (ctrl & 0x00010000);
		ret = 0;

		/* read status, and check if transaction completed ok */
		stat = nvkm_mask(device, 0x00e4e8 + base, 0, 0);
		if ((stat & 0x000f0000) == 0x00080000 ||
		    (stat & 0x000f0000) == 0x00020000)
			ret = 1;
		if ((stat & 0x00000100))
			ret = -ETIMEDOUT;
		if ((stat & 0x00000e00))
			ret = -EIO;

		AUX_TRACE(&aux->base, "%02d %08x %08x", retries, ctrl, stat);
	} while (ret && retry && retries++ < 32);

	if (type & 1) {
		for (i = 0; i < 16; i += 4) {
			xbuf[i / 4] = nvkm_rd32(device, 0x00e4d0 + base + i);
			AUX_TRACE(&aux->base, "rd %08x", xbuf[i / 4]);
		}
		memcpy(data, xbuf, *size);
		*size = stat & 0x0000001f;
	}
out_err:
	nvkm_i2c_aux_autodpcd(i2c, aux->ch, true);
out:
	g94_i2c_aux_fini(aux);
	return ret < 0 ? ret : (stat & 0x000f0000) >> 16;
}

int
g94_i2c_aux_new_(const struct nvkm_i2c_aux_func *func,
		 struct nvkm_i2c_pad *pad, int index, u8 drive,
		 struct nvkm_i2c_aux **paux)
{
	struct g94_i2c_aux *aux;

	if (!(aux = kzalloc(sizeof(*aux), GFP_KERNEL)))
		return -ENOMEM;
	*paux = &aux->base;

	nvkm_i2c_aux_ctor(func, pad, index, &aux->base);
	aux->ch = drive;
	aux->base.intr = 1 << aux->ch;
	return 0;
}

static const struct nvkm_i2c_aux_func
g94_i2c_aux = {
	.xfer = g94_i2c_aux_xfer,
};

int
g94_i2c_aux_new(struct nvkm_i2c_pad *pad, int index, u8 drive,
		struct nvkm_i2c_aux **paux)
{
	return g94_i2c_aux_new_(&g94_i2c_aux, pad, index, drive, paux);
}
