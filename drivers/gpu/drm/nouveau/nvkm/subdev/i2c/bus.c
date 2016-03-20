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
#include "bus.h"
#include "pad.h"

#include <core/option.h>

/*******************************************************************************
 * i2c-algo-bit
 ******************************************************************************/
static int
nvkm_i2c_bus_pre_xfer(struct i2c_adapter *adap)
{
	struct nvkm_i2c_bus *bus = container_of(adap, typeof(*bus), i2c);
	return nvkm_i2c_bus_acquire(bus);
}

static void
nvkm_i2c_bus_post_xfer(struct i2c_adapter *adap)
{
	struct nvkm_i2c_bus *bus = container_of(adap, typeof(*bus), i2c);
	return nvkm_i2c_bus_release(bus);
}

static void
nvkm_i2c_bus_setscl(void *data, int state)
{
	struct nvkm_i2c_bus *bus = data;
	bus->func->drive_scl(bus, state);
}

static void
nvkm_i2c_bus_setsda(void *data, int state)
{
	struct nvkm_i2c_bus *bus = data;
	bus->func->drive_sda(bus, state);
}

static int
nvkm_i2c_bus_getscl(void *data)
{
	struct nvkm_i2c_bus *bus = data;
	return bus->func->sense_scl(bus);
}

static int
nvkm_i2c_bus_getsda(void *data)
{
	struct nvkm_i2c_bus *bus = data;
	return bus->func->sense_sda(bus);
}

/*******************************************************************************
 * !i2c-algo-bit (off-chip i2c bus / hw i2c / internal bit-banging algo)
 ******************************************************************************/
static int
nvkm_i2c_bus_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct nvkm_i2c_bus *bus = container_of(adap, typeof(*bus), i2c);
	int ret;

	ret = nvkm_i2c_bus_acquire(bus);
	if (ret)
		return ret;

	ret = bus->func->xfer(bus, msgs, num);
	nvkm_i2c_bus_release(bus);
	return ret;
}

static u32
nvkm_i2c_bus_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm
nvkm_i2c_bus_algo = {
	.master_xfer = nvkm_i2c_bus_xfer,
	.functionality = nvkm_i2c_bus_func,
};

/*******************************************************************************
 * nvkm_i2c_bus base
 ******************************************************************************/
void
nvkm_i2c_bus_init(struct nvkm_i2c_bus *bus)
{
	BUS_TRACE(bus, "init");
	if (bus->func->init)
		bus->func->init(bus);
}

void
nvkm_i2c_bus_release(struct nvkm_i2c_bus *bus)
{
	struct nvkm_i2c_pad *pad = bus->pad;
	BUS_TRACE(bus, "release");
	nvkm_i2c_pad_release(pad);
	mutex_unlock(&bus->mutex);
}

int
nvkm_i2c_bus_acquire(struct nvkm_i2c_bus *bus)
{
	struct nvkm_i2c_pad *pad = bus->pad;
	int ret;
	BUS_TRACE(bus, "acquire");
	mutex_lock(&bus->mutex);
	ret = nvkm_i2c_pad_acquire(pad, NVKM_I2C_PAD_I2C);
	if (ret)
		mutex_unlock(&bus->mutex);
	return ret;
}

int
nvkm_i2c_bus_probe(struct nvkm_i2c_bus *bus, const char *what,
		   struct nvkm_i2c_bus_probe *info,
		   bool (*match)(struct nvkm_i2c_bus *,
				 struct i2c_board_info *, void *), void *data)
{
	int i;

	BUS_DBG(bus, "probing %ss", what);
	for (i = 0; info[i].dev.addr; i++) {
		u8 orig_udelay = 0;

		if ((bus->i2c.algo == &i2c_bit_algo) && (info[i].udelay != 0)) {
			struct i2c_algo_bit_data *algo = bus->i2c.algo_data;
			BUS_DBG(bus, "%dms delay instead of %dms",
				     info[i].udelay, algo->udelay);
			orig_udelay = algo->udelay;
			algo->udelay = info[i].udelay;
		}

		if (nvkm_probe_i2c(&bus->i2c, info[i].dev.addr) &&
		    (!match || match(bus, &info[i].dev, data))) {
			BUS_DBG(bus, "detected %s: %s",
				what, info[i].dev.type);
			return i;
		}

		if (orig_udelay) {
			struct i2c_algo_bit_data *algo = bus->i2c.algo_data;
			algo->udelay = orig_udelay;
		}
	}

	BUS_DBG(bus, "no devices found.");
	return -ENODEV;
}

void
nvkm_i2c_bus_del(struct nvkm_i2c_bus **pbus)
{
	struct nvkm_i2c_bus *bus = *pbus;
	if (bus && !WARN_ON(!bus->func)) {
		BUS_TRACE(bus, "dtor");
		list_del(&bus->head);
		i2c_del_adapter(&bus->i2c);
		kfree(bus->i2c.algo_data);
		kfree(*pbus);
		*pbus = NULL;
	}
}

int
nvkm_i2c_bus_ctor(const struct nvkm_i2c_bus_func *func,
		  struct nvkm_i2c_pad *pad, int id,
		  struct nvkm_i2c_bus *bus)
{
	struct nvkm_device *device = pad->i2c->subdev.device;
	struct i2c_algo_bit_data *bit;
#ifndef CONFIG_NOUVEAU_I2C_INTERNAL_DEFAULT
	const bool internal = false;
#else
	const bool internal = true;
#endif
	int ret;

	bus->func = func;
	bus->pad = pad;
	bus->id = id;
	mutex_init(&bus->mutex);
	list_add_tail(&bus->head, &pad->i2c->bus);
	BUS_TRACE(bus, "ctor");

	snprintf(bus->i2c.name, sizeof(bus->i2c.name), "nvkm-%s-bus-%04x",
		 dev_name(device->dev), id);
	bus->i2c.owner = THIS_MODULE;
	bus->i2c.dev.parent = device->dev;

	if ( bus->func->drive_scl &&
	    !nvkm_boolopt(device->cfgopt, "NvI2C", internal)) {
		if (!(bit = kzalloc(sizeof(*bit), GFP_KERNEL)))
			return -ENOMEM;
		bit->udelay = 10;
		bit->timeout = usecs_to_jiffies(2200);
		bit->data = bus;
		bit->pre_xfer = nvkm_i2c_bus_pre_xfer;
		bit->post_xfer = nvkm_i2c_bus_post_xfer;
		bit->setscl = nvkm_i2c_bus_setscl;
		bit->setsda = nvkm_i2c_bus_setsda;
		bit->getscl = nvkm_i2c_bus_getscl;
		bit->getsda = nvkm_i2c_bus_getsda;
		bus->i2c.algo_data = bit;
		ret = i2c_bit_add_bus(&bus->i2c);
	} else {
		bus->i2c.algo = &nvkm_i2c_bus_algo;
		ret = i2c_add_adapter(&bus->i2c);
	}

	return ret;
}

int
nvkm_i2c_bus_new_(const struct nvkm_i2c_bus_func *func,
		  struct nvkm_i2c_pad *pad, int id,
		  struct nvkm_i2c_bus **pbus)
{
	if (!(*pbus = kzalloc(sizeof(**pbus), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_i2c_bus_ctor(func, pad, id, *pbus);
}
