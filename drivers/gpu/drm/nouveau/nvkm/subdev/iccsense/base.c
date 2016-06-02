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
			      enum nvbios_extdev_type type)
{
	switch (type) {
	case NVBIOS_EXTDEV_INA209:
	case NVBIOS_EXTDEV_INA219:
		return nv_rd16i2cr(i2c, addr, 0x0) >= 0;
	case NVBIOS_EXTDEV_INA3221:
		return nv_rd16i2cr(i2c, addr, 0xff) == 0x3220 &&
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
	return nvkm_iccsense_poll_lane(rail->sensor->i2c, rail->sensor->addr,
				       shunt_reg, 0, bus_reg, 3, rail->mohm,
				       10 * 4);
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
	return nvkm_iccsense_poll_lane(rail->sensor->i2c, rail->sensor->addr,
				       1 + (rail->idx * 2), 3,
				       2 + (rail->idx * 2), 3, rail->mohm,
				       40 * 8);
}

static void
nvkm_iccsense_ina209_config(struct nvkm_iccsense *iccsense,
			    struct nvkm_iccsense_sensor *sensor)
{
	struct nvkm_subdev *subdev = &iccsense->subdev;
	/* configuration:
	 * 0x0007: 0x0007 shunt and bus continous
	 * 0x0078: 0x0078 128 samples shunt
	 * 0x0780: 0x0780 128 samples bus
	 * 0x1800: 0x0000 +-40 mV shunt range
	 * 0x2000: 0x0000 16V FSR
         */
	u16 value = 0x07ff;
	nvkm_debug(subdev, "config for sensor id %i: 0x%x\n", sensor->id, value);
	nv_wr16i2cr(sensor->i2c, sensor->addr, 0x00, value);
}

static void
nvkm_iccsense_ina3221_config(struct nvkm_iccsense *iccsense,
			     struct nvkm_iccsense_sensor *sensor)
{
	struct nvkm_subdev *subdev = &iccsense->subdev;
	/* configuration:
	 * 0x0007: 0x0007 shunt and bus continous
	 * 0x0031: 0x0000 140 us conversion time shunt
	 * 0x01c0: 0x0000 140 us conversion time bus
	 * 0x0f00: 0x0f00 1024 samples
	 * 0x7000: 0x?000 channels
         */
	u16 value = 0x0e07;
	if (sensor->rail_mask & 0x1)
		value |= 0x1 << 14;
	if (sensor->rail_mask & 0x2)
		value |= 0x1 << 13;
	if (sensor->rail_mask & 0x4)
		value |= 0x1 << 12;
	nvkm_debug(subdev, "config for sensor id %i: 0x%x\n", sensor->id, value);
	nv_wr16i2cr(sensor->i2c, sensor->addr, 0x00, value);
}

static void
nvkm_iccsense_sensor_config(struct nvkm_iccsense *iccsense,
		            struct nvkm_iccsense_sensor *sensor)
{
	switch (sensor->type) {
	case NVBIOS_EXTDEV_INA209:
	case NVBIOS_EXTDEV_INA219:
		nvkm_iccsense_ina209_config(iccsense, sensor);
		break;
	case NVBIOS_EXTDEV_INA3221:
		nvkm_iccsense_ina3221_config(iccsense, sensor);
		break;
	default:
		break;
	}
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
	struct nvkm_iccsense_sensor *sensor, *tmps;
	struct nvkm_iccsense_rail *rail, *tmpr;

	list_for_each_entry_safe(sensor, tmps, &iccsense->sensors, head) {
		list_del(&sensor->head);
		kfree(sensor);
	}
	list_for_each_entry_safe(rail, tmpr, &iccsense->rails, head) {
		list_del(&rail->head);
		kfree(rail);
	}

	return iccsense;
}

static struct nvkm_iccsense_sensor*
nvkm_iccsense_create_sensor(struct nvkm_iccsense *iccsense, u8 id)
{

	struct nvkm_subdev *subdev = &iccsense->subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvkm_i2c *i2c = subdev->device->i2c;
	struct nvbios_extdev_func extdev;
	struct nvkm_i2c_bus *i2c_bus;
	struct nvkm_iccsense_sensor *sensor;
	u8 addr;

	if (!i2c || !bios || nvbios_extdev_parse(bios, id, &extdev))
		return NULL;

	if (extdev.type == 0xff)
		return NULL;

	if (extdev.type != NVBIOS_EXTDEV_INA209 &&
	    extdev.type != NVBIOS_EXTDEV_INA219 &&
	    extdev.type != NVBIOS_EXTDEV_INA3221) {
		iccsense->data_valid = false;
		nvkm_error(subdev, "Unknown sensor type %x, power reading "
			   "disabled\n", extdev.type);
		return NULL;
	}

	if (extdev.bus)
		i2c_bus = nvkm_i2c_bus_find(i2c, NVKM_I2C_BUS_SEC);
	else
		i2c_bus = nvkm_i2c_bus_find(i2c, NVKM_I2C_BUS_PRI);
	if (!i2c_bus)
		return NULL;

	addr = extdev.addr >> 1;
	if (!nvkm_iccsense_validate_device(&i2c_bus->i2c, addr,
					   extdev.type)) {
		iccsense->data_valid = false;
		nvkm_warn(subdev, "found invalid sensor id: %i, power reading"
			  "might be invalid\n", id);
		return NULL;
	}

	sensor = kmalloc(sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return NULL;

	list_add_tail(&sensor->head, &iccsense->sensors);
	sensor->id = id;
	sensor->type = extdev.type;
	sensor->i2c = &i2c_bus->i2c;
	sensor->addr = addr;
	sensor->rail_mask = 0x0;
	return sensor;
}

static struct nvkm_iccsense_sensor*
nvkm_iccsense_get_sensor(struct nvkm_iccsense *iccsense, u8 id)
{
	struct nvkm_iccsense_sensor *sensor;
	list_for_each_entry(sensor, &iccsense->sensors, head) {
		if (sensor->id == id)
			return sensor;
	}
	return nvkm_iccsense_create_sensor(iccsense, id);
}

static int
nvkm_iccsense_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_iccsense *iccsense = nvkm_iccsense(subdev);
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvbios_iccsense stbl;
	int i;

	if (!bios || nvbios_iccsense_parse(bios, &stbl) || !stbl.nr_entry)
		return 0;

	iccsense->data_valid = true;
	for (i = 0; i < stbl.nr_entry; ++i) {
		struct pwr_rail_t *r = &stbl.rail[i];
		struct nvkm_iccsense_rail *rail;
		struct nvkm_iccsense_sensor *sensor;

		if (!r->mode || r->resistor_mohm == 0)
			continue;

		sensor = nvkm_iccsense_get_sensor(iccsense, r->extdev_id);
		if (!sensor)
			continue;

		rail = kmalloc(sizeof(*rail), GFP_KERNEL);
		if (!rail)
			return -ENOMEM;

		switch (sensor->type) {
		case NVBIOS_EXTDEV_INA209:
			if (r->rail != 0)
				continue;
			rail->read = nvkm_iccsense_ina209_read;
			break;
		case NVBIOS_EXTDEV_INA219:
			if (r->rail != 0)
				continue;
			rail->read = nvkm_iccsense_ina219_read;
			break;
		case NVBIOS_EXTDEV_INA3221:
			if (r->rail >= 3)
				continue;
			rail->read = nvkm_iccsense_ina3221_read;
			break;
		default:
			continue;
		}

		sensor->rail_mask |= 1 << r->rail;
		rail->sensor = sensor;
		rail->idx = r->rail;
		rail->mohm = r->resistor_mohm;
		list_add_tail(&rail->head, &iccsense->rails);
	}
	return 0;
}

static int
nvkm_iccsense_init(struct nvkm_subdev *subdev)
{
	struct nvkm_iccsense *iccsense = nvkm_iccsense(subdev);
	struct nvkm_iccsense_sensor *sensor;
	list_for_each_entry(sensor, &iccsense->sensors, head)
		nvkm_iccsense_sensor_config(iccsense, sensor);
	return 0;
}

struct nvkm_subdev_func iccsense_func = {
	.oneinit = nvkm_iccsense_oneinit,
	.init = nvkm_iccsense_init,
	.dtor = nvkm_iccsense_dtor,
};

void
nvkm_iccsense_ctor(struct nvkm_device *device, int index,
		   struct nvkm_iccsense *iccsense)
{
	nvkm_subdev_ctor(&iccsense_func, device, index, &iccsense->subdev);
}

int
nvkm_iccsense_new_(struct nvkm_device *device, int index,
		   struct nvkm_iccsense **iccsense)
{
	if (!(*iccsense = kzalloc(sizeof(**iccsense), GFP_KERNEL)))
		return -ENOMEM;
	INIT_LIST_HEAD(&(*iccsense)->sensors);
	INIT_LIST_HEAD(&(*iccsense)->rails);
	nvkm_iccsense_ctor(device, index, *iccsense);
	return 0;
}
