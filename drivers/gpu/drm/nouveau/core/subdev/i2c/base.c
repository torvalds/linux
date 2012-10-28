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

#include "core/option.h"

#include "subdev/i2c.h"
#include "subdev/vga.h"

int
nv_rdi2cr(struct nouveau_i2c_port *port, u8 addr, u8 reg)
{
	u8 val;
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = I2C_M_RD, .len = 1, .buf = &val },
	};

	int ret = i2c_transfer(&port->adapter, msgs, 2);
	if (ret != 2)
		return -EIO;

	return val;
}

int
nv_wri2cr(struct nouveau_i2c_port *port, u8 addr, u8 reg, u8 val)
{
	struct i2c_msg msgs[] = {
		{ .addr = addr, .flags = 0, .len = 1, .buf = &reg },
		{ .addr = addr, .flags = 0, .len = 1, .buf = &val },
	};

	int ret = i2c_transfer(&port->adapter, msgs, 2);
	if (ret != 2)
		return -EIO;

	return 0;
}

bool
nv_probe_i2c(struct nouveau_i2c_port *port, u8 addr)
{
	u8 buf[] = { 0 };
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

	return i2c_transfer(&port->adapter, msgs, 2) == 2;
}

static struct nouveau_i2c_port *
nouveau_i2c_find(struct nouveau_i2c *i2c, u8 index)
{
	struct nouveau_bios *bios = nouveau_bios(i2c);
	struct nouveau_i2c_port *port;

	if (index == NV_I2C_DEFAULT(0) ||
	    index == NV_I2C_DEFAULT(1)) {
		u8  ver, hdr, cnt, len;
		u16 i2c = dcb_i2c_table(bios, &ver, &hdr, &cnt, &len);
		if (i2c && ver >= 0x30) {
			u8 auxidx = nv_ro08(bios, i2c + 4);
			if (index == NV_I2C_DEFAULT(0))
				index = (auxidx & 0x0f) >> 0;
			else
				index = (auxidx & 0xf0) >> 4;
		} else {
			index = 2;
		}
	}

	list_for_each_entry(port, &i2c->ports, head) {
		if (port->index == index)
			break;
	}

	if (&port->head == &i2c->ports)
		return NULL;

	if (nv_device(i2c)->card_type >= NV_50 && (port->dcb & 0x00000100)) {
		u32 reg = 0x00e500, val;
		if (port->type == 6) {
			reg += port->drive * 0x50;
			val  = 0x2002;
		} else {
			reg += ((port->dcb & 0x1e00) >> 9) * 0x50;
			val  = 0xe001;
		}

		/* nfi, but neither auxch or i2c work if it's 1 */
		nv_mask(i2c, reg + 0x0c, 0x00000001, 0x00000000);
		/* nfi, but switches auxch vs normal i2c */
		nv_mask(i2c, reg + 0x00, 0x0000f003, val);
	}

	return port;
}

static int
nouveau_i2c_identify(struct nouveau_i2c *i2c, int index, const char *what,
		     struct i2c_board_info *info,
		     bool (*match)(struct nouveau_i2c_port *,
				   struct i2c_board_info *))
{
	struct nouveau_i2c_port *port = nouveau_i2c_find(i2c, index);
	int i;

	if (!port) {
		nv_debug(i2c, "no bus when probing %s on %d\n", what, index);
		return -ENODEV;
	}

	nv_debug(i2c, "probing %ss on bus: %d\n", what, port->index);
	for (i = 0; info[i].addr; i++) {
		if (nv_probe_i2c(port, info[i].addr) &&
		    (!match || match(port, &info[i]))) {
			nv_info(i2c, "detected %s: %s\n", what, info[i].type);
			return i;
		}
	}

	nv_debug(i2c, "no devices found.\n");
	return -ENODEV;
}

void
nouveau_i2c_drive_scl(void *data, int state)
{
	struct nouveau_i2c_port *port = data;

	if (port->type == DCB_I2C_NV04_BIT) {
		u8 val = nv_rdvgac(port->i2c, 0, port->drive);
		if (state) val |= 0x20;
		else	   val &= 0xdf;
		nv_wrvgac(port->i2c, 0, port->drive, val | 0x01);
	} else
	if (port->type == DCB_I2C_NV4E_BIT) {
		nv_mask(port->i2c, port->drive, 0x2f, state ? 0x21 : 0x01);
	} else
	if (port->type == DCB_I2C_NVIO_BIT) {
		if (state) port->state |= 0x01;
		else	   port->state &= 0xfe;
		nv_wr32(port->i2c, port->drive, 4 | port->state);
	}
}

void
nouveau_i2c_drive_sda(void *data, int state)
{
	struct nouveau_i2c_port *port = data;

	if (port->type == DCB_I2C_NV04_BIT) {
		u8 val = nv_rdvgac(port->i2c, 0, port->drive);
		if (state) val |= 0x10;
		else	   val &= 0xef;
		nv_wrvgac(port->i2c, 0, port->drive, val | 0x01);
	} else
	if (port->type == DCB_I2C_NV4E_BIT) {
		nv_mask(port->i2c, port->drive, 0x1f, state ? 0x11 : 0x01);
	} else
	if (port->type == DCB_I2C_NVIO_BIT) {
		if (state) port->state |= 0x02;
		else	   port->state &= 0xfd;
		nv_wr32(port->i2c, port->drive, 4 | port->state);
	}
}

int
nouveau_i2c_sense_scl(void *data)
{
	struct nouveau_i2c_port *port = data;
	struct nouveau_device *device = nv_device(port->i2c);

	if (port->type == DCB_I2C_NV04_BIT) {
		return !!(nv_rdvgac(port->i2c, 0, port->sense) & 0x04);
	} else
	if (port->type == DCB_I2C_NV4E_BIT) {
		return !!(nv_rd32(port->i2c, port->sense) & 0x00040000);
	} else
	if (port->type == DCB_I2C_NVIO_BIT) {
		if (device->card_type < NV_D0)
			return !!(nv_rd32(port->i2c, port->sense) & 0x01);
		else
			return !!(nv_rd32(port->i2c, port->sense) & 0x10);
	}

	return 0;
}

int
nouveau_i2c_sense_sda(void *data)
{
	struct nouveau_i2c_port *port = data;
	struct nouveau_device *device = nv_device(port->i2c);

	if (port->type == DCB_I2C_NV04_BIT) {
		return !!(nv_rdvgac(port->i2c, 0, port->sense) & 0x08);
	} else
	if (port->type == DCB_I2C_NV4E_BIT) {
		return !!(nv_rd32(port->i2c, port->sense) & 0x00080000);
	} else
	if (port->type == DCB_I2C_NVIO_BIT) {
		if (device->card_type < NV_D0)
			return !!(nv_rd32(port->i2c, port->sense) & 0x02);
		else
			return !!(nv_rd32(port->i2c, port->sense) & 0x20);
	}

	return 0;
}

static const u32 nv50_i2c_port[] = {
	0x00e138, 0x00e150, 0x00e168, 0x00e180,
	0x00e254, 0x00e274, 0x00e764, 0x00e780,
	0x00e79c, 0x00e7b8
};

static int
nouveau_i2c_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
		 struct nouveau_oclass *oclass, void *data, u32 size,
		 struct nouveau_object **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nouveau_i2c_port *port;
	struct nouveau_i2c *i2c;
	struct dcb_i2c_entry info;
	int ret, i = -1;

	ret = nouveau_subdev_create(parent, engine, oclass, 0,
				    "I2C", "i2c", &i2c);
	*pobject = nv_object(i2c);
	if (ret)
		return ret;

	i2c->find = nouveau_i2c_find;
	i2c->identify = nouveau_i2c_identify;
	INIT_LIST_HEAD(&i2c->ports);

	while (!dcb_i2c_parse(bios, ++i, &info)) {
		if (info.type == DCB_I2C_UNUSED)
			continue;

		port = kzalloc(sizeof(*port), GFP_KERNEL);
		if (!port) {
			nv_error(i2c, "failed port memory alloc at %d\n", i);
			break;
		}

		port->type = info.type;
		switch (port->type) {
		case DCB_I2C_NV04_BIT:
			port->drive = info.drive;
			port->sense = info.sense;
			break;
		case DCB_I2C_NV4E_BIT:
			port->drive = 0x600800 + info.drive;
			port->sense = port->drive;
			break;
		case DCB_I2C_NVIO_BIT:
			port->drive = info.drive & 0x0f;
			if (device->card_type < NV_D0) {
				if (info.drive >= ARRAY_SIZE(nv50_i2c_port))
					break;
				port->drive = nv50_i2c_port[port->drive];
				port->sense = port->drive;
			} else {
				port->drive = 0x00d014 + (port->drive * 0x20);
				port->sense = port->drive;
			}
			break;
		case DCB_I2C_NVIO_AUX:
			port->drive = info.drive & 0x0f;
			port->sense = port->drive;
			port->adapter.algo = &nouveau_i2c_aux_algo;
			break;
		default:
			break;
		}

		if (!port->adapter.algo && !port->drive) {
			nv_error(i2c, "I2C%d: type %d index %x/%x unknown\n",
				 i, port->type, port->drive, port->sense);
			kfree(port);
			continue;
		}

		snprintf(port->adapter.name, sizeof(port->adapter.name),
			 "nouveau-%s-%d", device->name, i);
		port->adapter.owner = THIS_MODULE;
		port->adapter.dev.parent = &device->pdev->dev;
		port->i2c = i2c;
		port->index = i;
		port->dcb = info.data;
		i2c_set_adapdata(&port->adapter, i2c);

		if (port->adapter.algo != &nouveau_i2c_aux_algo) {
			nouveau_i2c_drive_scl(port, 0);
			nouveau_i2c_drive_sda(port, 1);
			nouveau_i2c_drive_scl(port, 1);

#ifdef CONFIG_NOUVEAU_I2C_INTERNAL_DEFAULT
			if (nouveau_boolopt(device->cfgopt, "NvI2C", true)) {
#else
			if (nouveau_boolopt(device->cfgopt, "NvI2C", false)) {
#endif
				port->adapter.algo = &nouveau_i2c_bit_algo;
				ret = i2c_add_adapter(&port->adapter);
			} else {
				port->adapter.algo_data = &port->bit;
				port->bit.udelay = 10;
				port->bit.timeout = usecs_to_jiffies(2200);
				port->bit.data = port;
				port->bit.setsda = nouveau_i2c_drive_sda;
				port->bit.setscl = nouveau_i2c_drive_scl;
				port->bit.getsda = nouveau_i2c_sense_sda;
				port->bit.getscl = nouveau_i2c_sense_scl;
				ret = i2c_bit_add_bus(&port->adapter);
			}
		} else {
			port->adapter.algo = &nouveau_i2c_aux_algo;
			ret = i2c_add_adapter(&port->adapter);
		}

		if (ret) {
			nv_error(i2c, "I2C%d: failed register: %d\n", i, ret);
			kfree(port);
			continue;
		}

		list_add_tail(&port->head, &i2c->ports);
	}

	return 0;
}

static void
nouveau_i2c_dtor(struct nouveau_object *object)
{
	struct nouveau_i2c *i2c = (void *)object;
	struct nouveau_i2c_port *port, *temp;

	list_for_each_entry_safe(port, temp, &i2c->ports, head) {
		i2c_del_adapter(&port->adapter);
		list_del(&port->head);
		kfree(port);
	}

	nouveau_subdev_destroy(&i2c->base);
}

static int
nouveau_i2c_init(struct nouveau_object *object)
{
	struct nouveau_i2c *i2c = (void *)object;
	return nouveau_subdev_init(&i2c->base);
}

static int
nouveau_i2c_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_i2c *i2c = (void *)object;
	return nouveau_subdev_fini(&i2c->base, suspend);
}

struct nouveau_oclass
nouveau_i2c_oclass = {
	.handle = NV_SUBDEV(I2C, 0x00),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nouveau_i2c_ctor,
		.dtor = nouveau_i2c_dtor,
		.init = nouveau_i2c_init,
		.fini = nouveau_i2c_fini,
	},
};
