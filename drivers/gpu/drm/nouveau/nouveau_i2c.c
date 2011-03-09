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
	struct drm_device *dev = i2c->dev;

	nv_wr32(dev, i2c->wr, 4 | (i2c->data ? 2 : 0) | (state ? 1 : 0));
}

static void
nv50_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_chan *i2c = data;
	struct drm_device *dev = i2c->dev;

	nv_wr32(dev, i2c->wr,
			(nv_rd32(dev, i2c->rd) & 1) | 4 | (state ? 2 : 0));
	i2c->data = state;
}

static const uint32_t nv50_i2c_port[] = {
	0x00e138, 0x00e150, 0x00e168, 0x00e180,
	0x00e254, 0x00e274, 0x00e764, 0x00e780,
	0x00e79c, 0x00e7b8
};
#define NV50_I2C_PORTS ARRAY_SIZE(nv50_i2c_port)

int
nouveau_i2c_init(struct drm_device *dev, struct dcb_i2c_entry *entry, int index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_i2c_chan *i2c;
	int ret;

	if (entry->chan)
		return -EEXIST;

	if (dev_priv->card_type >= NV_50 && entry->read >= NV50_I2C_PORTS) {
		NV_ERROR(dev, "unknown i2c port %d\n", entry->read);
		return -EINVAL;
	}

	i2c = kzalloc(sizeof(*i2c), GFP_KERNEL);
	if (i2c == NULL)
		return -ENOMEM;

	switch (entry->port_type) {
	case 0:
		i2c->bit.setsda = nv04_i2c_setsda;
		i2c->bit.setscl = nv04_i2c_setscl;
		i2c->bit.getsda = nv04_i2c_getsda;
		i2c->bit.getscl = nv04_i2c_getscl;
		i2c->rd = entry->read;
		i2c->wr = entry->write;
		break;
	case 4:
		i2c->bit.setsda = nv4e_i2c_setsda;
		i2c->bit.setscl = nv4e_i2c_setscl;
		i2c->bit.getsda = nv4e_i2c_getsda;
		i2c->bit.getscl = nv4e_i2c_getscl;
		i2c->rd = 0x600800 + entry->read;
		i2c->wr = 0x600800 + entry->write;
		break;
	case 5:
		i2c->bit.setsda = nv50_i2c_setsda;
		i2c->bit.setscl = nv50_i2c_setscl;
		i2c->bit.getsda = nv50_i2c_getsda;
		i2c->bit.getscl = nv50_i2c_getscl;
		i2c->rd = nv50_i2c_port[entry->read];
		i2c->wr = i2c->rd;
		break;
	case 6:
		i2c->rd = entry->read;
		i2c->wr = entry->write;
		break;
	default:
		NV_ERROR(dev, "DCB I2C port type %d unknown\n",
			 entry->port_type);
		kfree(i2c);
		return -EINVAL;
	}

	snprintf(i2c->adapter.name, sizeof(i2c->adapter.name),
		 "nouveau-%s-%d", pci_name(dev->pdev), index);
	i2c->adapter.owner = THIS_MODULE;
	i2c->adapter.dev.parent = &dev->pdev->dev;
	i2c->dev = dev;
	i2c_set_adapdata(&i2c->adapter, i2c);

	if (entry->port_type < 6) {
		i2c->adapter.algo_data = &i2c->bit;
		i2c->bit.udelay = 40;
		i2c->bit.timeout = usecs_to_jiffies(5000);
		i2c->bit.data = i2c;
		ret = i2c_bit_add_bus(&i2c->adapter);
	} else {
		i2c->adapter.algo = &nouveau_dp_i2c_algo;
		ret = i2c_add_adapter(&i2c->adapter);
	}

	if (ret) {
		NV_ERROR(dev, "Failed to register i2c %d\n", index);
		kfree(i2c);
		return ret;
	}

	entry->chan = i2c;
	return 0;
}

void
nouveau_i2c_fini(struct drm_device *dev, struct dcb_i2c_entry *entry)
{
	if (!entry->chan)
		return;

	i2c_del_adapter(&entry->chan->adapter);
	kfree(entry->chan);
	entry->chan = NULL;
}

struct nouveau_i2c_chan *
nouveau_i2c_find(struct drm_device *dev, int index)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct dcb_i2c_entry *i2c = &dev_priv->vbios.dcb.i2c[index];

	if (index >= DCB_MAX_NUM_I2C_ENTRIES)
		return NULL;

	if (dev_priv->card_type >= NV_50 && (i2c->entry & 0x00000100)) {
		uint32_t reg = 0xe500, val;

		if (i2c->port_type == 6) {
			reg += i2c->read * 0x50;
			val  = 0x2002;
		} else {
			reg += ((i2c->entry & 0x1e00) >> 9) * 0x50;
			val  = 0xe001;
		}

		nv_wr32(dev, reg, (nv_rd32(dev, reg) & ~0xf003) | val);
	}

	if (!i2c->chan && nouveau_i2c_init(dev, i2c, index))
		return NULL;
	return i2c->chan;
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
