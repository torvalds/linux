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

#define T_TIMEOUT  2200000
#define T_RISEFALL 1000
#define T_HOLD     5000

static void
i2c_drive_scl(void *data, int state)
{
	struct nouveau_i2c_chan *port = data;
	if (port->type == 0) {
		u8 val = NVReadVgaCrtc(port->dev, 0, port->drive);
		if (state) val |= 0x20;
		else	   val &= 0xdf;
		NVWriteVgaCrtc(port->dev, 0, port->drive, val | 0x01);
	} else
	if (port->type == 4) {
		nv_mask(port->dev, port->drive, 0x2f, state ? 0x21 : 0x01);
	} else
	if (port->type == 5) {
		if (state) port->state |= 0x01;
		else	   port->state &= 0xfe;
		nv_wr32(port->dev, port->drive, 4 | port->state);
	}
}

static void
i2c_drive_sda(void *data, int state)
{
	struct nouveau_i2c_chan *port = data;
	if (port->type == 0) {
		u8 val = NVReadVgaCrtc(port->dev, 0, port->drive);
		if (state) val |= 0x10;
		else	   val &= 0xef;
		NVWriteVgaCrtc(port->dev, 0, port->drive, val | 0x01);
	} else
	if (port->type == 4) {
		nv_mask(port->dev, port->drive, 0x1f, state ? 0x11 : 0x01);
	} else
	if (port->type == 5) {
		if (state) port->state |= 0x02;
		else	   port->state &= 0xfd;
		nv_wr32(port->dev, port->drive, 4 | port->state);
	}
}

static int
i2c_sense_scl(void *data)
{
	struct nouveau_i2c_chan *port = data;
	struct drm_nouveau_private *dev_priv = port->dev->dev_private;
	if (port->type == 0) {
		return !!(NVReadVgaCrtc(port->dev, 0, port->sense) & 0x04);
	} else
	if (port->type == 4) {
		return !!(nv_rd32(port->dev, port->sense) & 0x00040000);
	} else
	if (port->type == 5) {
		if (dev_priv->card_type < NV_D0)
			return !!(nv_rd32(port->dev, port->sense) & 0x01);
		else
			return !!(nv_rd32(port->dev, port->sense) & 0x10);
	}
	return 0;
}

static int
i2c_sense_sda(void *data)
{
	struct nouveau_i2c_chan *port = data;
	struct drm_nouveau_private *dev_priv = port->dev->dev_private;
	if (port->type == 0) {
		return !!(NVReadVgaCrtc(port->dev, 0, port->sense) & 0x08);
	} else
	if (port->type == 4) {
		return !!(nv_rd32(port->dev, port->sense) & 0x00080000);
	} else
	if (port->type == 5) {
		if (dev_priv->card_type < NV_D0)
			return !!(nv_rd32(port->dev, port->sense) & 0x02);
		else
			return !!(nv_rd32(port->dev, port->sense) & 0x20);
	}
	return 0;
}

static void
i2c_delay(struct nouveau_i2c_chan *port, u32 nsec)
{
	udelay((nsec + 500) / 1000);
}

static bool
i2c_raise_scl(struct nouveau_i2c_chan *port)
{
	u32 timeout = T_TIMEOUT / T_RISEFALL;

	i2c_drive_scl(port, 1);
	do {
		i2c_delay(port, T_RISEFALL);
	} while (!i2c_sense_scl(port) && --timeout);

	return timeout != 0;
}

static int
i2c_start(struct nouveau_i2c_chan *port)
{
	int ret = 0;

	port->state  = i2c_sense_scl(port);
	port->state |= i2c_sense_sda(port) << 1;
	if (port->state != 3) {
		i2c_drive_scl(port, 0);
		i2c_drive_sda(port, 1);
		if (!i2c_raise_scl(port))
			ret = -EBUSY;
	}

	i2c_drive_sda(port, 0);
	i2c_delay(port, T_HOLD);
	i2c_drive_scl(port, 0);
	i2c_delay(port, T_HOLD);
	return ret;
}

static void
i2c_stop(struct nouveau_i2c_chan *port)
{
	i2c_drive_scl(port, 0);
	i2c_drive_sda(port, 0);
	i2c_delay(port, T_RISEFALL);

	i2c_drive_scl(port, 1);
	i2c_delay(port, T_HOLD);
	i2c_drive_sda(port, 1);
	i2c_delay(port, T_HOLD);
}

static int
i2c_bitw(struct nouveau_i2c_chan *port, int sda)
{
	i2c_drive_sda(port, sda);
	i2c_delay(port, T_RISEFALL);

	if (!i2c_raise_scl(port))
		return -ETIMEDOUT;
	i2c_delay(port, T_HOLD);

	i2c_drive_scl(port, 0);
	i2c_delay(port, T_HOLD);
	return 0;
}

static int
i2c_bitr(struct nouveau_i2c_chan *port)
{
	int sda;

	i2c_drive_sda(port, 1);
	i2c_delay(port, T_RISEFALL);

	if (!i2c_raise_scl(port))
		return -ETIMEDOUT;
	i2c_delay(port, T_HOLD);

	sda = i2c_sense_sda(port);

	i2c_drive_scl(port, 0);
	i2c_delay(port, T_HOLD);
	return sda;
}

static int
i2c_get_byte(struct nouveau_i2c_chan *port, u8 *byte, bool last)
{
	int i, bit;

	*byte = 0;
	for (i = 7; i >= 0; i--) {
		bit = i2c_bitr(port);
		if (bit < 0)
			return bit;
		*byte |= bit << i;
	}

	return i2c_bitw(port, last ? 1 : 0);
}

static int
i2c_put_byte(struct nouveau_i2c_chan *port, u8 byte)
{
	int i, ret;
	for (i = 7; i >= 0; i--) {
		ret = i2c_bitw(port, !!(byte & (1 << i)));
		if (ret < 0)
			return ret;
	}

	ret = i2c_bitr(port);
	if (ret == 1) /* nack */
		ret = -EIO;
	return ret;
}

static int
i2c_addr(struct nouveau_i2c_chan *port, struct i2c_msg *msg)
{
	u32 addr = msg->addr << 1;
	if (msg->flags & I2C_M_RD)
		addr |= 1;
	return i2c_put_byte(port, addr);
}

static int
i2c_bit_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nouveau_i2c_chan *port = (struct nouveau_i2c_chan *)adap;
	struct i2c_msg *msg = msgs;
	int ret = 0, mcnt = num;

	while (!ret && mcnt--) {
		u8 remaining = msg->len;
		u8 *ptr = msg->buf;

		ret = i2c_start(port);
		if (ret == 0)
			ret = i2c_addr(port, msg);

		if (msg->flags & I2C_M_RD) {
			while (!ret && remaining--)
				ret = i2c_get_byte(port, ptr++, !remaining);
		} else {
			while (!ret && remaining--)
				ret = i2c_put_byte(port, *ptr++);
		}

		msg++;
	}

	i2c_stop(port);
	return (ret < 0) ? ret : num;
}

static u32
i2c_bit_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

const struct i2c_algorithm nouveau_i2c_bit_algo = {
	.master_xfer = i2c_bit_xfer,
	.functionality = i2c_bit_func
};

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
			port->drive = entry[0];
			port->sense = entry[1];
			port->adapter.algo = &nouveau_i2c_bit_algo;
			break;
		case 4: /* NV4E */
			port->drive = 0x600800 + entry[1];
			port->sense = port->drive;
			port->adapter.algo = &nouveau_i2c_bit_algo;
			break;
		case 5: /* NV50- */
			port->drive = entry[0] & 0x0f;
			if (dev_priv->card_type < NV_D0) {
				if (port->drive >= ARRAY_SIZE(nv50_i2c_port))
					break;
				port->drive = nv50_i2c_port[port->drive];
				port->sense = port->drive;
			} else {
				port->drive = 0x00d014 + (port->drive * 0x20);
				port->sense = port->drive;
			}
			port->adapter.algo = &nouveau_i2c_bit_algo;
			break;
		case 6: /* NV50- DP AUX */
			port->drive = entry[0];
			port->sense = port->drive;
			port->adapter.algo = &nouveau_dp_i2c_algo;
			break;
		default:
			break;
		}

		if (!port->adapter.algo) {
			NV_ERROR(dev, "I2C%d: type %d index %x/%x unknown\n",
				 i, port->type, port->drive, port->sense);
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

		ret = i2c_add_adapter(&port->adapter);
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
			reg += port->drive * 0x50;
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

	if (!i2c) {
		NV_DEBUG(dev, "No bus when probing %s on %d\n", what, index);
		return -ENODEV;
	}

	NV_DEBUG(dev, "Probing %ss on I2C bus: %d\n", what, i2c->index);
	for (i = 0; info[i].addr; i++) {
		if (nouveau_probe_i2c_addr(i2c, info[i].addr) &&
		    (!match || match(i2c, &info[i]))) {
			NV_INFO(dev, "Detected %s: %s\n", what, info[i].type);
			return i;
		}
	}

	NV_DEBUG(dev, "No devices found.\n");
	return -ENODEV;
}
