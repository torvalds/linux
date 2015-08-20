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
#include "priv.h"

static int
pwm_info(struct nvkm_therm *therm, int line)
{
	struct nvkm_subdev *subdev = &therm->subdev;
	struct nvkm_device *device = subdev->device;
	u32 gpio = nvkm_rd32(device, 0x00d610 + (line * 0x04));

	switch (gpio & 0x000000c0) {
	case 0x00000000: /* normal mode, possibly pwm forced off by us */
	case 0x00000040: /* nvio special */
		switch (gpio & 0x0000001f) {
		case 0x00: return 2;
		case 0x19: return 1;
		case 0x1c: return 0;
		case 0x1e: return 2;
		default:
			break;
		}
	default:
		break;
	}

	nvkm_error(subdev, "GPIO %d unknown PWM: %08x\n", line, gpio);
	return -ENODEV;
}

static int
gf119_fan_pwm_ctrl(struct nvkm_therm *therm, int line, bool enable)
{
	struct nvkm_device *device = therm->subdev.device;
	u32 data = enable ? 0x00000040 : 0x00000000;
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2)
		nvkm_mask(device, 0x00d610 + (line * 0x04), 0x000000c0, data);
	/* nothing to do for indx == 2, it seems hardwired to PTHERM */
	return 0;
}

static int
gf119_fan_pwm_get(struct nvkm_therm *therm, int line, u32 *divs, u32 *duty)
{
	struct nvkm_device *device = therm->subdev.device;
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2) {
		if (nvkm_rd32(device, 0x00d610 + (line * 0x04)) & 0x00000040) {
			*divs = nvkm_rd32(device, 0x00e114 + (indx * 8));
			*duty = nvkm_rd32(device, 0x00e118 + (indx * 8));
			return 0;
		}
	} else if (indx == 2) {
		*divs = nvkm_rd32(device, 0x0200d8) & 0x1fff;
		*duty = nvkm_rd32(device, 0x0200dc) & 0x1fff;
		return 0;
	}

	return -EINVAL;
}

static int
gf119_fan_pwm_set(struct nvkm_therm *therm, int line, u32 divs, u32 duty)
{
	struct nvkm_device *device = therm->subdev.device;
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2) {
		nvkm_wr32(device, 0x00e114 + (indx * 8), divs);
		nvkm_wr32(device, 0x00e118 + (indx * 8), duty | 0x80000000);
	} else if (indx == 2) {
		nvkm_mask(device, 0x0200d8, 0x1fff, divs); /* keep the high bits */
		nvkm_wr32(device, 0x0200dc, duty | 0x40000000);
	}
	return 0;
}

static int
gf119_fan_pwm_clock(struct nvkm_therm *therm, int line)
{
	struct nvkm_device *device = therm->subdev.device;
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return 0;
	else if (indx < 2)
		return (device->crystal * 1000) / 20;
	else
		return device->crystal * 1000 / 10;
}

void
gf119_therm_init(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;

	g84_sensor_setup(therm);

	/* enable fan tach, count revolutions per-second */
	nvkm_mask(device, 0x00e720, 0x00000003, 0x00000002);
	if (therm->fan->tach.func != DCB_GPIO_UNUSED) {
		nvkm_mask(device, 0x00d79c, 0x000000ff, therm->fan->tach.line);
		nvkm_wr32(device, 0x00e724, device->crystal * 1000);
		nvkm_mask(device, 0x00e720, 0x00000001, 0x00000001);
	}
	nvkm_mask(device, 0x00e720, 0x00000002, 0x00000000);
}

static const struct nvkm_therm_func
gf119_therm = {
	.init = gf119_therm_init,
	.fini = g84_therm_fini,
	.pwm_ctrl = gf119_fan_pwm_ctrl,
	.pwm_get = gf119_fan_pwm_get,
	.pwm_set = gf119_fan_pwm_set,
	.pwm_clock = gf119_fan_pwm_clock,
	.temp_get = g84_temp_get,
	.fan_sense = gt215_therm_fan_sense,
	.program_alarms = nvkm_therm_program_alarms_polling,
};

int
gf119_therm_new(struct nvkm_device *device, int index,
	       struct nvkm_therm **ptherm)
{
	return nvkm_therm_new_(&gf119_therm, device, index, ptherm);
}
