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
 * Authors: Ben Skeggs
 */

#include <core/option.h>

#include <subdev/bios.h>
#include <subdev/bios/dcb.h>
#include <subdev/bios/i2c.h>
#include <subdev/i2c.h>
#include <subdev/vga.h>

/******************************************************************************
 * interface to linux i2c bit-banging algorithm
 *****************************************************************************/

#ifdef CONFIG_NOUVEAU_I2C_INTERNAL_DEFAULT
#define CSTMSEL true
#else
#define CSTMSEL false
#endif

static int
nouveau_i2c_pre_xfer(struct i2c_adapter *adap)
{
	struct i2c_algo_bit_data *bit = adap->algo_data;
	struct nouveau_i2c_port *port = bit->data;
	if (port->func->acquire)
		port->func->acquire(port);
	return 0;
}

static void
nouveau_i2c_setscl(void *data, int state)
{
	struct nouveau_i2c_port *port = data;
	port->func->drive_scl(port, state);
}

static void
nouveau_i2c_setsda(void *data, int state)
{
	struct nouveau_i2c_port *port = data;
	port->func->drive_sda(port, state);
}

static int
nouveau_i2c_getscl(void *data)
{
	struct nouveau_i2c_port *port = data;
	return port->func->sense_scl(port);
}

static int
nouveau_i2c_getsda(void *data)
{
	struct nouveau_i2c_port *port = data;
	return port->func->sense_sda(port);
}

/******************************************************************************
 * base i2c "port" class implementation
 *****************************************************************************/

void
_nouveau_i2c_port_dtor(struct nouveau_object *object)
{
	struct nouveau_i2c_port *port = (void *)object;
	i2c_del_adapter(&port->adapter);
	nouveau_object_destroy(&port->base);
}

int
nouveau_i2c_port_create_(struct nouveau_object *parent,
			 struct nouveau_object *engine,
			 struct nouveau_oclass *oclass, u8 index,
			 const struct i2c_algorithm *algo,
			 int size, void **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_i2c *i2c = (void *)engine;
	struct nouveau_i2c_port *port;
	int ret;

	ret = nouveau_object_create_(parent, engine, oclass, 0, size, pobject);
	port = *pobject;
	if (ret)
		return ret;

	snprintf(port->adapter.name, sizeof(port->adapter.name),
		 "nouveau-%s-%d", device->name, index);
	port->adapter.owner = THIS_MODULE;
	port->adapter.dev.parent = &device->pdev->dev;
	port->index = index;
	i2c_set_adapdata(&port->adapter, i2c);

	if ( algo == &nouveau_i2c_bit_algo &&
	    !nouveau_boolopt(device->cfgopt, "NvI2C", CSTMSEL)) {
		struct i2c_algo_bit_data *bit;

		bit = kzalloc(sizeof(*bit), GFP_KERNEL);
		if (!bit)
			return -ENOMEM;

		bit->udelay = 10;
		bit->timeout = usecs_to_jiffies(2200);
		bit->data = port;
		bit->pre_xfer = nouveau_i2c_pre_xfer;
		bit->setsda = nouveau_i2c_setsda;
		bit->setscl = nouveau_i2c_setscl;
		bit->getsda = nouveau_i2c_getsda;
		bit->getscl = nouveau_i2c_getscl;

		port->adapter.algo_data = bit;
		ret = i2c_bit_add_bus(&port->adapter);
	} else {
		port->adapter.algo_data = port;
		port->adapter.algo = algo;
		ret = i2c_add_adapter(&port->adapter);
	}

	/* drop port's i2c subdev refcount, i2c handles this itself */
	if (ret == 0)
		list_add_tail(&port->head, &i2c->ports);
	return ret;
}

/******************************************************************************
 * base i2c subdev class implementation
 *****************************************************************************/

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
			return port;
	}

	return NULL;
}

static struct nouveau_i2c_port *
nouveau_i2c_find_type(struct nouveau_i2c *i2c, u16 type)
{
	struct nouveau_i2c_port *port;

	list_for_each_entry(port, &i2c->ports, head) {
		if (nv_hclass(port) == type)
			return port;
	}

	return NULL;
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

int
_nouveau_i2c_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_i2c *i2c = (void *)object;
	struct nouveau_i2c_port *port;
	int ret;

	list_for_each_entry(port, &i2c->ports, head) {
		ret = nv_ofuncs(port)->fini(nv_object(port), suspend);
		if (ret && suspend)
			goto fail;
	}

	return nouveau_subdev_fini(&i2c->base, suspend);
fail:
	list_for_each_entry_continue_reverse(port, &i2c->ports, head) {
		nv_ofuncs(port)->init(nv_object(port));
	}

	return ret;
}

int
_nouveau_i2c_init(struct nouveau_object *object)
{
	struct nouveau_i2c *i2c = (void *)object;
	struct nouveau_i2c_port *port;
	int ret;

	ret = nouveau_subdev_init(&i2c->base);
	if (ret == 0) {
		list_for_each_entry(port, &i2c->ports, head) {
			ret = nv_ofuncs(port)->init(nv_object(port));
			if (ret)
				goto fail;
		}
	}

	return ret;
fail:
	list_for_each_entry_continue_reverse(port, &i2c->ports, head) {
		nv_ofuncs(port)->fini(nv_object(port), false);
	}

	return ret;
}

void
_nouveau_i2c_dtor(struct nouveau_object *object)
{
	struct nouveau_i2c *i2c = (void *)object;
	struct nouveau_i2c_port *port, *temp;

	list_for_each_entry_safe(port, temp, &i2c->ports, head) {
		nouveau_object_ref(NULL, (struct nouveau_object **)&port);
	}

	nouveau_subdev_destroy(&i2c->base);
}

static struct nouveau_oclass *
nouveau_i2c_extdev_sclass[] = {
	nouveau_anx9805_sclass,
};

int
nouveau_i2c_create_(struct nouveau_object *parent,
		    struct nouveau_object *engine,
		    struct nouveau_oclass *oclass,
		    struct nouveau_oclass *sclass,
		    int length, void **pobject)
{
	struct nouveau_bios *bios = nouveau_bios(parent);
	struct nouveau_i2c *i2c;
	struct nouveau_object *object;
	struct dcb_i2c_entry info;
	int ret, i, j, index = -1;
	struct dcb_output outp;
	u8  ver, hdr;
	u32 data;

	ret = nouveau_subdev_create(parent, engine, oclass, 0,
				    "I2C", "i2c", &i2c);
	*pobject = nv_object(i2c);
	if (ret)
		return ret;

	i2c->find = nouveau_i2c_find;
	i2c->find_type = nouveau_i2c_find_type;
	i2c->identify = nouveau_i2c_identify;
	INIT_LIST_HEAD(&i2c->ports);

	while (!dcb_i2c_parse(bios, ++index, &info)) {
		if (info.type == DCB_I2C_UNUSED)
			continue;

		oclass = sclass;
		do {
			ret = -EINVAL;
			if (oclass->handle == info.type) {
				ret = nouveau_object_ctor(*pobject, *pobject,
							  oclass, &info,
							  index, &object);
			}
		} while (ret && (++oclass)->handle);
	}

	/* in addition to the busses specified in the i2c table, there
	 * may be ddc/aux channels hiding behind external tmds/dp/etc
	 * transmitters.
	 */
	index = ((index + 0x0f) / 0x10) * 0x10;
	i = -1;
	while ((data = dcb_outp_parse(bios, ++i, &ver, &hdr, &outp))) {
		if (!outp.location || !outp.extdev)
			continue;

		switch (outp.type) {
		case DCB_OUTPUT_TMDS:
			info.type = NV_I2C_TYPE_EXTDDC(outp.extdev);
			break;
		case DCB_OUTPUT_DP:
			info.type = NV_I2C_TYPE_EXTAUX(outp.extdev);
			break;
		default:
			continue;
		}

		ret = -ENODEV;
		j = -1;
		while (ret && ++j < ARRAY_SIZE(nouveau_i2c_extdev_sclass)) {
			parent = nv_object(i2c->find(i2c, outp.i2c_index));
			oclass = nouveau_i2c_extdev_sclass[j];
			do {
				if (oclass->handle != info.type)
					continue;
				ret = nouveau_object_ctor(parent, *pobject,
							  oclass, NULL,
							  index++, &object);
			} while (ret && (++oclass)->handle);
		}
	}

	return 0;
}
