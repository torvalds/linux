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
	return priv->fan->get(therm);
}

int
nouveau_therm_fan_set(struct nouveau_therm *therm, int percent)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (percent < priv->fan->bios.min_duty)
		percent = priv->fan->bios.min_duty;
	if (percent > priv->fan->bios.max_duty)
		percent = priv->fan->bios.max_duty;

	if (priv->fan->mode == FAN_CONTROL_NONE)
		return -EINVAL;

	return priv->fan->set(therm, percent);
}

int
nouveau_therm_fan_sense(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_timer *ptimer = nouveau_timer(therm);
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	u32 cycles, cur, prev;
	u64 start, end, tach;

	if (priv->fan->tach.func == DCB_GPIO_UNUSED)
		return -ENODEV;

	/* Time a complete rotation and extrapolate to RPM:
	 * When the fan spins, it changes the value of GPIO FAN_SENSE.
	 * We get 4 changes (0 -> 1 -> 0 -> 1) per complete rotation.
	 */
	start = ptimer->read(ptimer);
	prev = gpio->get(gpio, 0, priv->fan->tach.func, priv->fan->tach.line);
	cycles = 0;
	do {
		usleep_range(500, 1000); /* supports 0 < rpm < 7500 */

		cur = gpio->get(gpio, 0, priv->fan->tach.func, priv->fan->tach.line);
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

	if (priv->fan->mode == mode)
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

	priv->fan->mode = mode;
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

	if (priv->fan->mode != FAN_CONTROL_MANUAL)
		return -EINVAL;

	return nouveau_therm_fan_set(therm, percent);
}

void
nouveau_therm_fan_set_defaults(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->fan->bios.pwm_freq = 0;
	priv->fan->bios.min_duty = 0;
	priv->fan->bios.max_duty = 100;
}


static void
nouveau_therm_fan_safety_checks(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->fan->bios.min_duty > 100)
		priv->fan->bios.min_duty = 100;
	if (priv->fan->bios.max_duty > 100)
		priv->fan->bios.max_duty = 100;

	if (priv->fan->bios.min_duty > priv->fan->bios.max_duty)
		priv->fan->bios.min_duty = priv->fan->bios.max_duty;
}

int nouveau_fan_pwm_clock_dummy(struct nouveau_therm *therm)
{
	return 1;
}

int
nouveau_therm_fan_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	struct nouveau_bios *bios = nouveau_bios(therm);
	struct dcb_gpio_func func;
	int ret;

	/* attempt to locate a drivable fan, and determine control method */
	ret = gpio->find(gpio, 0, DCB_GPIO_PWM_FAN, 0xff, &func);
	if (ret == 0)
		ret = nouveau_fanpwm_create(therm, &func);
	if (ret)
		ret = nouveau_fannil_create(therm);
	if (ret)
		return ret;

	nv_info(therm, "FAN control: %s\n", priv->fan->type);

	/* attempt to detect a tachometer connection */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN_SENSE, 0xff, &priv->fan->tach);
	if (ret)
		priv->fan->tach.func = DCB_GPIO_UNUSED;

	/* other random init... */
	nouveau_therm_fan_set_defaults(therm);
	nvbios_perf_fan_parse(bios, &priv->fan->perf);
	if (nvbios_therm_fan_parse(bios, &priv->fan->bios))
		nv_error(therm, "parsing the thermal table failed\n");
	nouveau_therm_fan_safety_checks(therm);

	nouveau_therm_fan_set_mode(therm, FAN_CONTROL_NONE);

	return 0;
}
