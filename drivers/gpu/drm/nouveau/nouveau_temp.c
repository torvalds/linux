/*
 * Copyright 2010 PathScale inc.
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

#include <linux/module.h>

#include "drmP.h"

#include "nouveau_drm.h"
#include "nouveau_pm.h"

#include <subdev/i2c.h>
#include <subdev/bios/therm.h>

static int
nv40_sensor_setup(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);

	/* enable ADC readout and disable the ALARM threshold */
	if (nv_device(drm->device)->chipset >= 0x46) {
		nv_mask(device, 0x15b8, 0x80000000, 0);
		nv_wr32(device, 0x15b0, 0x80003fff);
		return nv_rd32(device, 0x15b4) & 0x3fff;
	} else {
		nv_wr32(device, 0x15b0, 0xff);
		return nv_rd32(device, 0x15b4) & 0xff;
	}
}

int
nv40_temp_get(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_temp_sensor_constants *sensor = &pm->sensor_constants;
	int core_temp;

	if (nv_device(drm->device)->chipset >= 0x46) {
		nv_wr32(device, 0x15b0, 0x80003fff);
		core_temp = nv_rd32(device, 0x15b4) & 0x3fff;
	} else {
		nv_wr32(device, 0x15b0, 0xff);
		core_temp = nv_rd32(device, 0x15b4) & 0xff;
	}

	/* Setup the sensor if the temperature is 0 */
	if (core_temp == 0)
		core_temp = nv40_sensor_setup(dev);

	if (sensor->slope_div == 0)
		sensor->slope_div = 1;
	if (sensor->offset_div == 0)
		sensor->offset_div = 1;
	if (sensor->slope_mult < 1)
		sensor->slope_mult = 1;

	core_temp = core_temp * sensor->slope_mult / sensor->slope_div;
	core_temp = core_temp + sensor->offset_mult / sensor->offset_div;
	core_temp = core_temp + sensor->offset_constant - 8;

	return core_temp;
}

int
nv84_temp_get(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	return nv_rd32(device, 0x20400);
}

void
nouveau_temp_safety_checks(struct drm_device *dev)
{
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_threshold_temp *temps = &pm->threshold_temp;

	if (temps->critical > 120)
		temps->critical = 120;
	else if (temps->critical < 80)
		temps->critical = 80;

	if (temps->down_clock > 110)
		temps->down_clock = 110;
	else if (temps->down_clock < 60)
		temps->down_clock = 60;
}

static bool
probe_monitoring_device(struct nouveau_i2c_port *i2c,
			struct i2c_board_info *info)
{
	struct i2c_client *client;

	request_module("%s%s", I2C_MODULE_PREFIX, info->type);

	client = i2c_new_device(&i2c->adapter, info);
	if (!client)
		return false;

	if (!client->driver || client->driver->detect(client, info)) {
		i2c_unregister_device(client);
		return false;
	}

	return true;
}

static void
nouveau_temp_probe_i2c(struct drm_device *dev)
{
	struct nouveau_device *device = nouveau_dev(dev);
	struct nouveau_i2c *i2c = nouveau_i2c(device);
	struct i2c_board_info info[] = {
		{ I2C_BOARD_INFO("w83l785ts", 0x2d) },
		{ I2C_BOARD_INFO("w83781d", 0x2d) },
		{ I2C_BOARD_INFO("adt7473", 0x2e) },
		{ I2C_BOARD_INFO("f75375", 0x2e) },
		{ I2C_BOARD_INFO("lm99", 0x4c) },
		{ }
	};

	i2c->identify(i2c, NV_I2C_DEFAULT(0), "monitoring device", info,
		      probe_monitoring_device);
}

void
nouveau_temp_init(struct drm_device *dev)
{
	struct nouveau_drm *drm = nouveau_drm(dev);
	struct nouveau_device *device = nv_device(drm->device);
	struct nouveau_bios *bios = nouveau_bios(device);
	struct nouveau_pm *pm = nouveau_pm(dev);
	struct nouveau_pm_temp_sensor_constants *sensor = &pm->sensor_constants;
	struct nouveau_pm_threshold_temp *temps = &pm->threshold_temp;
	struct nvbios_therm_sensor bios_sensor;
	struct nvbios_therm_fan bios_fan;

	/* store some safe defaults */
	sensor->offset_constant = 0;
	sensor->offset_mult = 0;
	sensor->offset_div = 1;
	sensor->slope_mult = 1;
	sensor->slope_div = 1;

	if (!nvbios_therm_sensor_parse(bios, NVBIOS_THERM_DOMAIN_CORE,
				       &bios_sensor)) {
		sensor->slope_mult = bios_sensor.slope_mult;
		sensor->slope_div = bios_sensor.slope_div;
		sensor->offset_mult = bios_sensor.offset_num;
		sensor->offset_div = bios_sensor.offset_den;
		sensor->offset_constant = bios_sensor.offset_constant;

		temps->down_clock = bios_sensor.thrs_down_clock.temp;
		temps->critical = bios_sensor.thrs_critical.temp;
	}

	if (nvbios_therm_fan_parse(bios, &bios_fan)) {
		pm->fan.min_duty = bios_fan.min_duty;
		pm->fan.max_duty = bios_fan.max_duty;
		pm->fan.pwm_freq = bios_fan.pwm_freq;
	}

	nouveau_temp_safety_checks(dev);
	nouveau_temp_probe_i2c(dev);
}

void
nouveau_temp_fini(struct drm_device *dev)
{

}
