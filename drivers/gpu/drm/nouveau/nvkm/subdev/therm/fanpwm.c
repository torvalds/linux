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

#include <core/option.h>
#include <subdev/bios.h>
#include <subdev/bios/fan.h>
#include <subdev/gpio.h>

struct nvkm_fanpwm {
	struct nvkm_fan base;
	struct dcb_gpio_func func;
};

static int
nvkm_fanpwm_get(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_fanpwm *fan = (void *)therm->fan;
	struct nvkm_device *device = therm->base.subdev.device;
	struct nvkm_gpio *gpio = device->gpio;
	int card_type = device->card_type;
	u32 divs, duty;
	int ret;

	ret = therm->base.pwm_get(&therm->base, fan->func.line, &divs, &duty);
	if (ret == 0 && divs) {
		divs = max(divs, duty);
		if (card_type <= NV_40 || (fan->func.log[0] & 1))
			duty = divs - duty;
		return (duty * 100) / divs;
	}

	return nvkm_gpio_get(gpio, 0, fan->func.func, fan->func.line) * 100;
}

static int
nvkm_fanpwm_set(struct nvkm_therm *obj, int percent)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_fanpwm *fan = (void *)therm->fan;
	int card_type = nv_device(therm)->card_type;
	u32 divs, duty;
	int ret;

	divs = fan->base.perf.pwm_divisor;
	if (fan->base.bios.pwm_freq) {
		divs = 1;
		if (therm->base.pwm_clock)
			divs = therm->base.pwm_clock(&therm->base,
						     fan->func.line);
		divs /= fan->base.bios.pwm_freq;
	}

	duty = ((divs * percent) + 99) / 100;
	if (card_type <= NV_40 || (fan->func.log[0] & 1))
		duty = divs - duty;

	ret = therm->base.pwm_set(&therm->base, fan->func.line, divs, duty);
	if (ret == 0)
		ret = therm->base.pwm_ctrl(&therm->base, fan->func.line, true);
	return ret;
}

int
nvkm_fanpwm_create(struct nvkm_therm *obj, struct dcb_gpio_func *func)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_device *device = therm->base.subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nvkm_fanpwm *fan;
	struct nvbios_therm_fan info;
	u32 divs, duty;

	nvbios_fan_parse(bios, &info);

	if (!nvkm_boolopt(device->cfgopt, "NvFanPWM", func->param) ||
	    !therm->base.pwm_ctrl || info.type == NVBIOS_THERM_FAN_TOGGLE ||
	     therm->base.pwm_get(&therm->base, func->line, &divs, &duty) == -ENODEV)
		return -ENODEV;

	fan = kzalloc(sizeof(*fan), GFP_KERNEL);
	therm->fan = &fan->base;
	if (!fan)
		return -ENOMEM;

	fan->base.type = "PWM";
	fan->base.get = nvkm_fanpwm_get;
	fan->base.set = nvkm_fanpwm_set;
	fan->func = *func;
	return 0;
}
