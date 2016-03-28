/*
 * Copyright 2015 Martin Peres
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
 * Authors: Martin Peres
 */
#include "priv.h"

#include <subdev/bios.h>
#include <subdev/bios/extdev.h>
#include <subdev/bios/iccsense.h>
#include <subdev/i2c.h>

static bool
nvkm_iccsense_validate_device(struct i2c_adapter *i2c, u8 addr,
			      enum nvbios_extdev_type type, u8 rail)
{
	switch (type) {
	case NVBIOS_EXTDEV_INA209:
	case NVBIOS_EXTDEV_INA219:
		return rail == 0 && nv_rd16i2cr(i2c, addr, 0x0) >= 0;
	case NVBIOS_EXTDEV_INA3221:
		return rail <= 3 &&
		       nv_rd16i2cr(i2c, addr, 0xff) == 0x3220 &&
		       nv_rd16i2cr(i2c, addr, 0xfe) == 0x5449;
	default:
		return false;
	}
}

static int
nvkm_iccsense_poll_lane(struct i2c_adapter *i2c, u8 addr, u8 shunt_reg,
			u8 shunt_shift, u8 bus_reg, u8 bus_shift, u8 shunt,
			u16 lsb)
{
	int vshunt = nv_rd16i2cr(i2c, addr, shunt_reg);
	int vbus = nv_rd16i2cr(i2c, addr, bus_reg);

	if (vshunt < 0 || vbus < 0)
		return -EINVAL;

	vshunt >>= shunt_shift;
	vbus >>= bus_shift;

	return vbus * vshunt * lsb / shunt;
}

static int
nvkm_iccsense_ina2x9_read(struct nvkm_iccsense *iccsense,
                          struct nvkm_iccsense_rail *rail,
			  u8 shunt_reg, u8 bus_reg)
{
	return nvkm_iccsense_poll_lane(rail->i2c, rail->addr, shunt_reg, 0,
				       bus_reg, 3, rail->mohm, 10 * 4);
}

static int
nvkm_iccsense_ina209_read(struct nvkm_iccsense *iccsense,
			  struct nvkm_iccsense_rail *rail)
{
	return nvkm_iccsense_ina2x9_read(iccsense, rail, 3, 4);
}

static int
nvkm_iccsense_ina219_read(struct nvkm_iccsense *iccsense,
			  struct nvkm_iccsense_rail *rail)
{
	return nvkm_iccsense_ina2x9_read(iccsense, rail, 1, 2);
}

static int
nvkm_iccsense_ina3221_read(struct nvkm_iccsense *iccsense,
			   struct nvkm_iccsense_rail *rail)
{
	return nvkm_iccsense_poll_lane(rail->i2c, rail->addr,
				       1 + (rail->rail * 2), 3,
				       2 + (rail->rail * 2), 3, rail->mohm,
				       40 * 8);
}

int
nvkm_iccsense_read_all(struct nvkm_iccsense *iccsense)
{
	int result = 0;
	struct nvkm_iccsense_rail *rail;

	if (!iccsense)
		return -EINVAL;

	list_for_each_entry(rail, &iccsense->rails, head) {
		int res;
		if (!rail->read)
			return -ENODEV;

		res = rail->read(iccsense, rail);
		if (res < 0)
			return res;
		result += res;
	}
	return result;
}

static void *
nvkm_iccsense_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_iccsense *iccsense = nvkm_iccsense(subdev);
	struct nvkm_iccsense_rail *rail, *tmp;

	list_for_each_entry_safe(rail, tmp, &iccsense->rails, head) {
		list_del(&rail->head);
		kfree(rail);
	}

	return iccsense;
}

static int
nvkm_iccsense_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_iccsense *iccsense = nvkm_iccsense(subdev);
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvkm_i2c *i2c = subdev->device->i2c;
	struct nvbios_iccsense stbl;
	int i;

	if (!i2c || !bios || nvbios_iccsense_parse(bios, &stbl)
	    || !stbl.nr_entry)
		return 0;

	iccsense->data_valid = true;
	for (i = 0; i < stbl.nr_entry; ++i) {
		struct pwr_rail_t *r = &stbl.rail[i];
		struct nvbios_extdev_func extdev;
		struct nvkm_iccsense_rail *rail;
		struct nvkm_i2c_bus *i2c_bus;
		u8 addr;

		if (!r->mode || r->resistor_mohm == 0)
			continue;

		if (nvbios_extdev_parse(bios, r->extdev_id, &extdev))
			continue;

		if (extdev.type == 0xff)
			continue;

		if (extdev.bus)
			i2c_bus = nvkm_i2c_bus_find(i2c, NVKM_I2C_BUS_SEC);
		else
			i2c_bus = nvkm_i2c_bus_find(i2c, NVKM_I2C_BUS_PRI);
		if (!i2c_bus)
			continue;

		addr = extdev.addr >> 1;
		if (!nvkm_iccsense_validate_device(&i2c_bus->i2c, addr,
						   extdev.type, r->rail)) {
			iccsense->data_valid = false;
			nvkm_warn(subdev, "found unknown or invalid rail entry"
				  " type 0x%x rail %i, power reading might be"
				  " invalid\n", extdev.type, r->rail);
			continue;
		}

		rail = kmalloc(sizeof(*rail), GFP_KERNEL);
		if (!rail)
			return -ENOMEM;

		switch (extdev.type) {
		case NVBIOS_EXTDEV_INA209:
			rail->read = nvkm_iccsense_ina209_read;
			break;
		case NVBIOS_EXTDEV_INA219:
			rail->read = nvkm_iccsense_ina219_read;
			break;
		case NVBIOS_EXTDEV_INA3221:
			rail->read = nvkm_iccsense_ina3221_read;
			break;
		}

		rail->addr = addr;
		rail->rail = r->rail;
		rail->mohm = r->resistor_mohm;
		rail->i2c = &i2c_bus->i2c;
		list_add_tail(&rail->head, &iccsense->rails);
	}
	return 0;
}

struct nvkm_subdev_func iccsense_func = {
	.oneinit = nvkm_iccsense_oneinit,
	.dtor = nvkm_iccsense_dtor,
};

void
nvkm_iccsense_ctor(struct nvkm_device *device, int index,
		   struct nvkm_iccsense *iccsense)
{
	nvkm_subdev_ctor(&iccsense_func, device, index, 0, &iccsense->subdev);
}

int
nvkm_iccsense_new_(struct nvkm_device *device, int index,
		   struct nvkm_iccsense **iccsense)
{
	if (!(*iccsense = kzalloc(sizeof(**iccsense), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&(*iccsense)->rails);
	nvkm_iccsense_ctor(device, index, *iccsense);
	return 0;
}
