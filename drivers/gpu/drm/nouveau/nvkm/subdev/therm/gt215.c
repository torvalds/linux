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

#include <subdev/gpio.h>

int
gt215_therm_fan_sense(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;
	u32 tach = nvkm_rd32(device, 0x00e728) & 0x0000ffff;
	u32 ctrl = nvkm_rd32(device, 0x00e720);
	if (ctrl & 0x00000001)
		return tach * 60 / 2;
	return -ENODEV;
}

static void
gt215_therm_init(struct nvkm_therm *therm)
{
	struct nvkm_device *device = therm->subdev.device;
	struct dcb_gpio_func *tach = &therm->fan->tach;

	g84_sensor_setup(therm);

	/* enable fan tach, count revolutions per-second */
	nvkm_mask(device, 0x00e720, 0x00000003, 0x00000002);
	if (tach->func != DCB_GPIO_UNUSED) {
		nvkm_wr32(device, 0x00e724, device->crystal * 1000);
		nvkm_mask(device, 0x00e720, 0x001f0000, tach->line << 16);
		nvkm_mask(device, 0x00e720, 0x00000001, 0x00000001);
	}
	nvkm_mask(device, 0x00e720, 0x00000002, 0x00000000);
}

static const struct nvkm_therm_func
gt215_therm = {
	.init = gt215_therm_init,
	.fini = g84_therm_fini,
	.pwm_ctrl = nv50_fan_pwm_ctrl,
	.pwm_get = nv50_fan_pwm_get,
	.pwm_set = nv50_fan_pwm_set,
	.pwm_clock = nv50_fan_pwm_clock,
	.temp_get = g84_temp_get,
	.fan_sense = gt215_therm_fan_sense,
	.program_alarms = nvkm_therm_program_alarms_polling,
};

int
gt215_therm_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		struct nvkm_therm **ptherm)
{
	return nvkm_therm_new_(&gt215_therm, device, type, inst, ptherm);
}
