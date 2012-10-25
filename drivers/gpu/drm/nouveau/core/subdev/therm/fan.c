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

#include <core/object.h>
#include <core/device.h>
#include <subdev/gpio.h>
#include <subdev/timer.h>

int
nouveau_therm_fan_get(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	struct dcb_gpio_func func;
	int card_type = nv_device(therm)->card_type;
	u32 divs, duty;
	int ret;

	if (!priv->fan.pwm_get)
		return -ENODEV;

	ret = gpio->find(gpio, 0, DCB_GPIO_PWM_FAN, 0xff, &func);
	if (ret == 0) {
		ret = priv->fan.pwm_get(therm, func.line, &divs, &duty);
		if (ret == 0 && divs) {
			divs = max(divs, duty);
			if (card_type <= NV_40 || (func.log[0] & 1))
				duty = divs - duty;
			return (duty * 100) / divs;
		}

		return gpio->get(gpio, 0, func.func, func.line) * 100;
	}

	return -ENODEV;
}

int
nouveau_therm_fan_set(struct nouveau_therm *therm, int percent)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	struct dcb_gpio_func func;
	int card_type = nv_device(therm)->card_type;
	u32 divs, duty;
	int ret;

	if (priv->fan.mode == FAN_CONTROL_NONE)
		return -EINVAL;

	if (!priv->fan.pwm_set)
		return -ENODEV;

	if (percent < priv->bios_fan.min_duty)
		percent = priv->bios_fan.min_duty;
	if (percent > priv->bios_fan.max_duty)
		percent = priv->bios_fan.max_duty;

	ret = gpio->find(gpio, 0, DCB_GPIO_PWM_FAN, 0xff, &func);
	if (ret == 0) {
		divs = priv->bios_perf_fan.pwm_divisor;
		if (priv->bios_fan.pwm_freq) {
			divs = 1;
			if (priv->fan.pwm_clock)
				divs = priv->fan.pwm_clock(therm);
			divs /= priv->bios_fan.pwm_freq;
		}

		duty = ((divs * percent) + 99) / 100;
		if (card_type <= NV_40 || (func.log[0] & 1))
			duty = divs - duty;

		ret = priv->fan.pwm_set(therm, func.line, divs, duty);
		return ret;
	}

	return -ENODEV;
}

int
nouveau_therm_fan_sense(struct nouveau_therm *therm)
{
	struct nouveau_timer *ptimer = nouveau_timer(therm);
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	struct dcb_gpio_func func;
	u32 cycles, cur, prev;
	u64 start, end, tach;

	if (gpio->find(gpio, 0, DCB_GPIO_FAN_SENSE, 0xff, &func))
		return -ENODEV;

	/* Time a complete rotation and extrapolate to RPM:
	 * When the fan spins, it changes the value of GPIO FAN_SENSE.
	 * We get 4 changes (0 -> 1 -> 0 -> 1) per complete rotation.
	 */
	start = ptimer->read(ptimer);
	prev = gpio->get(gpio, 0, func.func, func.line);
	cycles = 0;
	do {
		usleep_range(500, 1000); /* supports 0 < rpm < 7500 */

		cur = gpio->get(gpio, 0, func.func, func.line);
		if (prev != cur) {
			if (!start)
				start = ptimer->read(ptimer);
			cycles++;
			prev = cur;
		}
	} while (cycles < 5 && ptimer->read(ptimer) - start < 250000000);
	end = ptimer->read(ptimer);

	if (cycles == 5) {
		tach = (u64)60000000000ULL;
		do_div(tach, (end - start));
		return tach;
	} else
		return 0;
}

int
nouveau_therm_fan_set_mode(struct nouveau_therm *therm,
			   enum nouveau_therm_fan_mode mode)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->fan.mode == mode)
		return 0;

	if (mode < FAN_CONTROL_NONE || mode >= FAN_CONTROL_NR)
		return -EINVAL;

	switch (mode)
	{
	case FAN_CONTROL_NONE:
		nv_info(therm, "switch fan to no-control mode\n");
		break;
	case FAN_CONTROL_MANUAL:
		nv_info(therm, "switch fan to manual mode\n");
		break;
	case FAN_CONTROL_NR:
		break;
	}

	priv->fan.mode = mode;
	return 0;
}

int
nouveau_therm_fan_user_get(struct nouveau_therm *therm)
{
	return nouveau_therm_fan_get(therm);
}

int
nouveau_therm_fan_user_set(struct nouveau_therm *therm, int percent)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->fan.mode != FAN_CONTROL_MANUAL)
		return -EINVAL;

	return nouveau_therm_fan_set(therm, percent);
}

void
nouveau_therm_fan_set_defaults(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->bios_fan.pwm_freq = 0;
	priv->bios_fan.min_duty = 0;
	priv->bios_fan.max_duty = 100;
}


static void
nouveau_therm_fan_safety_checks(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->bios_fan.min_duty > 100)
		priv->bios_fan.min_duty = 100;
	if (priv->bios_fan.max_duty > 100)
		priv->bios_fan.max_duty = 100;

	if (priv->bios_fan.min_duty > priv->bios_fan.max_duty)
		priv->bios_fan.min_duty = priv->bios_fan.max_duty;
}

int nouveau_fan_pwm_clock_dummy(struct nouveau_therm *therm)
{
	return 1;
}

int
nouveau_therm_fan_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_bios *bios = nouveau_bios(therm);

	nouveau_therm_fan_set_defaults(therm);
	nvbios_perf_fan_parse(bios, &priv->bios_perf_fan);
	if (nvbios_therm_fan_parse(bios, &priv->bios_fan))
		nv_error(therm, "parsing the thermal table failed\n");
	nouveau_therm_fan_safety_checks(therm);

	nouveau_therm_fan_set_mode(therm, FAN_CONTROL_NONE);

	return 0;
}
