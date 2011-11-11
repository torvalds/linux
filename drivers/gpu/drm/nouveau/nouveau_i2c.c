/*
 * Copyright 2009 Red Hat Inc.
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

#include <linux/module.h>

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_i2c.h"
#include "nouveau_hw.h"

static void
nv04_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (NVReadVgaCrtc(dev, 0, i2c->wr) & 0xd0) | (state ? 0x20 : 0);
	NVWriteVgaCrtc(dev, 0, i2c->wr, val | 0x01);
}

static void
nv04_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (NVReadVgaCrtc(dev, 0, i2c->wr) & 0xe0) | (state ? 0x10 : 0);
	NVWriteVgaCrtc(dev, 0, i2c->wr, val | 0x01);
}

static int
nv04_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(NVReadVgaCrtc(dev, 0, i2c->rd) & 4);
}

static int
nv04_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(NVReadVgaCrtc(dev, 0, i2c->rd) & 8);
}

static void
nv4e_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (nv_rd32(dev, i2c->wr) & 0xd0) | (state ? 0x20 : 0);
	nv_wr32(dev, i2c->wr, val | 0x01);
}

static void
nv4e_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;
	uint8_t val;

	val = (nv_rd32(dev, i2c->wr) & 0xe0) | (state ? 0x10 : 0);
	nv_wr32(dev, i2c->wr, val | 0x01);
}

static int
nv4e_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!((nv_rd32(dev, i2c->rd) >> 16) & 4);
}

static int
nv4e_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!((nv_rd32(dev, i2c->rd) >> 16) & 8);
}

static int
nv50_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(nv_rd32(dev, i2c->rd) & 1);
}

static int
nv50_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	return !!(nv_rd32(dev, i2c->rd) & 2);
}

static void
nv50_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;

	nv_wr32(i2c->dev, i2c->wr, 4 | (i2c->data ? 2 : 0) | (state ? 1 : 0));
}

static void
nv50_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;

	nv_mask(i2c->dev, i2c->wr, 0x00000006, 4 | (state ? 2 : 0));
	i2c->data = state;
}

static int
nvd0_i2c_getscl(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	return !!(nv_rd32(i2c->dev, i2c->rd) & 0x10);
}

static int
nvd0_i2c_getsda(void *data)
{
	struct nouveau_i2c_chan *i2c = data;
	return !!(nv_rd32(i2c->dev, i2c->rd) & 0x20);
}

static const uint32_t nv50_i2c_port[] = {
	0x00e138, 0x00e150, 0x00e168, 0x00e180,
	0x00e254, 0x00e274, 0x00e764, 0x00e780,
	0x00e79c, 0x00e7b8
};

static u8 *
i2c_table(struct drm_device *dev, u8 *version)
{
	u8 *dcb = dcb_table(dev), *i2c = NULL;
	if (dcb) {
		if (dcb[0] >= 0x15)
			i2c = ROMPTR(dev, dcb[2]);
		if (dcb[0] >= 0x30)
			i2c = ROMPTR(dev, dcb[4]);
	}

	/* early revisions had no version number, use dcb version */
	if (i2c) {
		*version = dcb[0];
		if (*version >= 0x30)
			*version = i2c[0];
	}

	return i2c;
}

int
nouveau_i2c_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvbios *bios = &dev_priv->vbios;
	struct nouveau_i2c_chan *port;
	u8 *i2c, *entry, legacy[2][4] = {};
	u8 version, entries, recordlen;
	int ret, i;

	INIT_LIST_HEAD(&dev_priv->i2c_ports);

	i2c = i2c_table(dev, &version);
	if (!i2c) {
		u8 *bmp = &bios->data[bios->offset];
		if (bios->type != NVBIOS_BMP)
			return -ENODEV;

		legacy[0][0] = NV_CIO_CRE_DDC_WR__INDEX;
		legacy[0][1] = NV_CIO_CRE_DDC_STATUS__INDEX;
		legacy[1][0] = NV_CIO_CRE_DDC0_WR__INDEX;
		legacy[1][1] = NV_CIO_CRE_DDC0_STATUS__INDEX;

		/* BMP (from v4.0) has i2c info in the structure, it's in a
		 * fixed location on earlier VBIOS
		 */
		if (bmp[5] < 4)
			i2c = &bios->data[0x48];
		else
			i2c = &bmp[0x36];

		if (i2c[4]) legacy[0][0] = i2c[4];
		if (i2c[5]) legacy[0][1] = i2c[5];
		if (i2c[6]) legacy[1][0] = i2c[6];
		if (i2c[7]) legacy[1][1] = i2c[7];
	}

	if (i2c && version >= 0x30) {
		entry     = i2c[1] + i2c;
		entries   = i2c[2];
		recordlen = i2c[3];
	} else
	if (i2c) {
		entry     = i2c;
		entries   = 16;
		recordlen = 4;
	} else {
		entry     = legacy[0];
		entries   = 2;
		recordlen = 4;
	}

	for (i = 0; i < entries; i++, entry += recordlen) {
		port = kzalloc(sizeof(*port), GFP_KERNEL);
		if (port == NULL) {
			nouveau_i2c_fini(dev);
			return -ENOMEM;
		}

		port->type = entry[3];
		if (version < 0x30) {
			port->type &= 0x07;
			if (port->type == 0x07)
				port->type = 0xff;
		}

		if (port->type == 0xff) {
			kfree(port);
			continue;
		}

		switch (port->type) {
		case 0: /* NV04:NV50 */
			port->wr = entry[0];
			port->rd = entry[1];
			port->bit.setsda = nv04_i2c_setsda;
			port->bit.setscl = nv04_i2c_setscl;
			port->bit.getsda = nv04_i2c_getsda;
			port->bit.getscl = nv04_i2c_getscl;
			break;
		case 4: /* NV4E */
			port->wr = 0x600800 + entry[1];
			port->rd = port->wr;
			port->bit.setsda = nv4e_i2c_setsda;
			port->bit.setscl = nv4e_i2c_setscl;
			port->bit.getsda = nv4e_i2c_getsda;
			port->bit.getscl = nv4e_i2c_getscl;
			break;
		case 5: /* NV50- */
			port->wr = entry[0] & 0x0f;
			if (dev_priv->card_type < NV_D0) {
				if (port->wr >= ARRAY_SIZE(nv50_i2c_port))
					break;
				port->wr = nv50_i2c_port[port->wr];
				port->rd = port->wr;
				port->bit.getsda = nv50_i2c_getsda;
				port->bit.getscl = nv50_i2c_getscl;
			} else {
				port->wr = 0x00d014 + (port->wr * 0x20);
				port->rd = port->wr;
				port->bit.getsda = nvd0_i2c_getsda;
				port->bit.getscl = nvd0_i2c_getscl;
			}
			port->bit.setsda = nv50_i2c_setsda;
			port->bit.setscl = nv50_i2c_setscl;
			break;
		case 6: /* NV50- DP AUX */
			port->wr = entry[0];
			port->rd = port->wr;
			port->adapter.algo = &nouveau_dp_i2c_algo;
			break;
		default:
			break;
		}

		if (!port->adapter.algo && !port->wr) {
			NV_ERROR(dev, "I2C%d: type %d index %x/%x unknown\n",
				 i, port->type, port->wr, port->rd);
			kfree(port);
			continue;
		}

		snprintf(port->adapter.name, sizeof(port->adapter.name),
			 "nouveau-%s-%d", pci_name(dev->pdev), i);
		port->adapter.owner = THIS_MODULE;
		port->adapter.dev.parent = &dev->pdev->dev;
		port->dev = dev;
		port->index = i;
		port->dcb = ROM32(entry[0]);
		i2c_set_adapdata(&port->adapter, i2c);

		if (port->adapter.algo != &nouveau_dp_i2c_algo) {
			port->adapter.algo_data = &port->bit;
			port->bit.udelay = 40;
			port->bit.timeout = usecs_to_jiffies(5000);
			port->bit.data = port;
			ret = i2c_bit_add_bus(&port->adapter);
		} else {
			port->adapter.algo = &nouveau_dp_i2c_algo;
			ret = i2c_add_adapter(&port->adapter);
		}

		if (ret) {
			NV_ERROR(dev, "I2C%d: failed register: %d\n", i, ret);
			kfree(port);
			continue;
		}

		list_add_tail(&port->head, &dev_priv->i2c_ports);
	}

	return 0;
}

void
nouveau_i2c_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *port, *tmp;

	list_for_each_entry_safe(port, tmp, &dev_priv->i2c_ports, head) {
		i2c_del_adapter(&port->adapter);
		kfree(port);
	}
}

struct nouveau_i2c_chan *
nouveau_i2c_find(struct drm_device *dev, u8 index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *port;

	if (index == NV_I2C_DEFAULT(0) ||
	    index == NV_I2C_DEFAULT(1)) {
		u8 version, *i2c = i2c_table(dev, &version);
		if (i2c && version >= 0x30) {
			if (index == NV_I2C_DEFAULT(0))
				index = (i2c[4] & 0x0f);
			else
				index = (i2c[4] & 0xf0) >> 4;
		} else {
			index = 2;
		}
	}

	list_for_each_entry(port, &dev_priv->i2c_ports, head) {
		if (port->index == index)
			break;
	}

	if (&port->head == &dev_priv->i2c_ports)
		return NULL;

	if (dev_priv->card_type >= NV_50 && (port->dcb & 0x00000100)) {
		u32 reg = 0x00e500, val;
		if (port->type == 6) {
			reg += port->rd * 0x50;
			val  = 0x2002;
		} else {
			reg += ((port->dcb & 0x1e00) >> 9) * 0x50;
			val  = 0xe001;
		}

		/* nfi, but neither auxch or i2c work if it's 1 */
		nv_mask(dev, reg + 0x0c, 0x00000001, 0x00000000);
		/* nfi, but switches auxch vs normal i2c */
		nv_mask(dev, reg + 0x00, 0x0000f003, val);
	}

	return port;
}

bool
nouveau_probe_i2c_addr(struct nouveau_i2c_chan *i2c, int addr)
{
	uint8_t buf[] = { 0 };
	struct i2c_msg msgs[] = {
		{
			.addr = addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	return i2c_transfer(&i2c->adapter, msgs, 2) == 2;
}

int
nouveau_i2c_identify(struct drm_device *dev, const char *what,
		     struct i2c_board_info *info,
		     bool (*match)(struct nouveau_i2c_chan *,
				   struct i2c_board_info *),
		     int index)
{
	struct nouveau_i2c_chan *i2c = nouveau_i2c_find(dev, index);
	int i;

	NV_DEBUG(dev, "Probing %ss on I2C bus: %d\n", what, index);

	for (i = 0; i2c && info[i].addr; i++) {
		if (nouveau_probe_i2c_addr(i2c, info[i].addr) &&
		    (!match || match(i2c, &info[i]))) {
			NV_INFO(dev, "Detected %s: %s\n", what, info[i].type);
			return i;
		}
	}

	NV_DEBUG(dev, "No devices found.\n");

	return -ENODEV;
}
