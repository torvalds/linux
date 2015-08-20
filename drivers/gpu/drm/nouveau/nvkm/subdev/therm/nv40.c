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
 * 	    Martin Peres
 */
#include "priv.h"

enum nv40_sensor_style { INVALID_STYLE = -1, OLD_STYLE = 0, NEW_STYLE = 1 };

static enum nv40_sensor_style
nv40_sensor_style(struct nvkm_therm *therm)
{
	struct nvkm_device *device = nv_device(therm);

	switch (device->chipset) {
	case 0x43:
	case 0x44:
	case 0x4a:
	case 0x47:
		return OLD_STYLE;

	case 0x46:
	case 0x49:
	case 0x4b:
	case 0x4e:
	case 0x4c:
	case 0x67:
	case 0x68:
	case 0x63:
		return NEW_STYLE;
	default:
		return INVALID_STYLE;
	}
}

static int
nv40_sensor_setup(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;
	enum nv40_sensor_style style = nv40_sensor_style(therm);

	/* enable ADC readout and disable the ALARM threshold */
	if (style == NEW_STYLE) {
		nvkm_mask(device, 0x15b8, 0x80000000, 0);
		nvkm_wr32(device, 0x15b0, 0x80003fff);
		mdelay(20); /* wait for the temperature to stabilize */
		return nvkm_rd32(device, 0x15b4) & 0x3fff;
	} else if (style == OLD_STYLE) {
		nvkm_wr32(device, 0x15b0, 0xff);
		mdelay(20); /* wait for the temperature to stabilize */
		return nvkm_rd32(device, 0x15b4) & 0xff;
	} else
		return -ENODEV;
}

static int
nv40_temp_get(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_device *device = therm->base.subdev.device;
	struct nvbios_therm_sensor *sensor = &therm->bios_sensor;
	enum nv40_sensor_style style = nv40_sensor_style(&therm->base);
	int core_temp;

	if (style == NEW_STYLE) {
		nvkm_wr32(device, 0x15b0, 0x80003fff);
		core_temp = nvkm_rd32(device, 0x15b4) & 0x3fff;
	} else if (style == OLD_STYLE) {
		nvkm_wr32(device, 0x15b0, 0xff);
		core_temp = nvkm_rd32(device, 0x15b4) & 0xff;
	} else
		return -ENODEV;

	/* if the slope or the offset is unset, do no use the sensor */
	if (!sensor->slope_div || !sensor->slope_mult ||
	    !sensor->offset_num || !sensor->offset_den)
	    return -ENODEV;

	core_temp = core_temp * sensor->slope_mult / sensor->slope_div;
	core_temp = core_temp + sensor->offset_num / sensor->offset_den;
	core_temp = core_temp + sensor->offset_constant - 8;

	/* reserve negative temperatures for errors */
	if (core_temp < 0)
		core_temp = 0;

	return core_temp;
}

static int
nv40_fan_pwm_ctrl(struct nvkm_therm *therm, int line, bool enable)
{
	struct nvkm_device *device = therm->subdev.device;
	u32 mask = enable ? 0x80000000 : 0x0000000;
	if      (line == 2) nvkm_mask(device, 0x0010f0, 0x80000000, mask);
	else if (line == 9) nvkm_mask(device, 0x0015f4, 0x80000000, mask);
	else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}
	return 0;
}

static int
nv40_fan_pwm_get(struct nvkm_therm *therm, int line, u32 *divs, u32 *duty)
{
	struct nvkm_device *device = therm->subdev.device;
	if (line == 2) {
		u32 reg = nvkm_rd32(device, 0x0010f0);
		if (reg & 0x80000000) {
			*duty = (reg & 0x7fff0000) >> 16;
			*divs = (reg & 0x00007fff);
			return 0;
		}
	} else
	if (line == 9) {
		u32 reg = nvkm_rd32(device, 0x0015f4);
		if (reg & 0x80000000) {
			*divs = nvkm_rd32(device, 0x0015f8);
			*duty = (reg & 0x7fffffff);
			return 0;
		}
	} else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}

	return -EINVAL;
}

static int
nv40_fan_pwm_set(struct nvkm_therm *therm, int line, u32 divs, u32 duty)
{
	struct nvkm_device *device = therm->subdev.device;
	if (line == 2) {
		nvkm_mask(device, 0x0010f0, 0x7fff7fff, (duty << 16) | divs);
	} else
	if (line == 9) {
		nvkm_wr32(device, 0x0015f8, divs);
		nvkm_mask(device, 0x0015f4, 0x7fffffff, duty);
	} else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}

	return 0;
}

void
nv40_therm_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);
	struct nvkm_device *device = therm->subdev.device;
	uint32_t stat = nvkm_rd32(device, 0x1100);

	/* traitement */

	/* ack all IRQs */
	nvkm_wr32(device, 0x1100, 0x70000);

	nv_error(therm, "THERM received an IRQ: stat = %x\n", stat);
}

static int
nv40_therm_ctor(struct nvkm_object *parent,
		struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_therm_priv *therm;
	int ret;

	ret = nvkm_therm_create(parent, engine, oclass, &therm);
	*pobject = nv_object(therm);
	if (ret)
		return ret;

	therm->base.pwm_ctrl = nv40_fan_pwm_ctrl;
	therm->base.pwm_get = nv40_fan_pwm_get;
	therm->base.pwm_set = nv40_fan_pwm_set;
	therm->base.temp_get = nv40_temp_get;
	therm->sensor.program_alarms = nvkm_therm_program_alarms_polling;
	nv_subdev(therm)->intr = nv40_therm_intr;
	return nvkm_therm_preinit(&therm->base);
}

static int
nv40_therm_init(struct nvkm_object *object)
{
	struct nvkm_therm *therm = (void *)object;

	nv40_sensor_setup(therm);

	return _nvkm_therm_init(object);
}

struct nvkm_oclass
nv40_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x40),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_therm_ctor,
		.dtor = _nvkm_therm_dtor,
		.init = nv40_therm_init,
		.fini = _nvkm_therm_fini,
	},
};
