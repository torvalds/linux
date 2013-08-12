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

static int
nouveau_fan_update(struct nouveau_fan *fan, bool immediate, int target)
{
	struct nouveau_therm *therm = fan->parent;
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_timer *ptimer = nouveau_timer(priv);
	unsigned long flags;
	int ret = 0;
	int duty;

	/* update target fan speed, restricting to allowed range */
	spin_lock_irqsave(&fan->lock, flags);
	if (target < 0)
		target = fan->percent;
	target = max_t(u8, target, fan->bios.min_duty);
	target = min_t(u8, target, fan->bios.max_duty);
	if (fan->percent != target) {
		nv_debug(therm, "FAN target: %d\n", target);
		fan->percent = target;
	}

	/* check that we're not already at the target duty cycle */
	duty = fan->get(therm);
	if (duty == target)
		goto done;

	/* smooth out the fanspeed increase/decrease */
	if (!immediate && duty >= 0) {
		/* the constant "3" is a rough approximation taken from
		 * nvidia's behaviour.
		 * it is meant to bump the fan speed more incrementally
		 */
		if (duty < target)
			duty = min(duty + 3, target);
		else if (duty > target)
			duty = max(duty - 3, target);
	} else {
		duty = target;
	}

	nv_debug(therm, "FAN update: %d\n", duty);
	ret = fan->set(therm, duty);
	if (ret)
		goto done;

	/* schedule next fan update, if not at target speed already */
	if (list_empty(&fan->alarm.head) && target != duty) {
		u16 bump_period = fan->bios.bump_period;
		u16 slow_down_period = fan->bios.slow_down_period;
		u64 delay;

		if (duty > target)
			delay = slow_down_period;
		else if (duty == target)
			delay = min(bump_period, slow_down_period) ;
		else
			delay = bump_period;

		ptimer->alarm(ptimer, delay * 1000 * 1000, &fan->alarm);
	}

done:
	spin_unlock_irqrestore(&fan->lock, flags);
	return ret;
}

static void
nouveau_fan_alarm(struct nouveau_alarm *alarm)
{
	struct nouveau_fan *fan = container_of(alarm, struct nouveau_fan, alarm);
	nouveau_fan_update(fan, false, -1);
}

int
nouveau_therm_fan_get(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	return priv->fan->get(therm);
}

int
nouveau_therm_fan_set(struct nouveau_therm *therm, bool immediate, int percent)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	return nouveau_fan_update(priv->fan, immediate, percent);
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
nouveau_therm_fan_user_get(struct nouveau_therm *therm)
{
	return nouveau_therm_fan_get(therm);
}

int
nouveau_therm_fan_user_set(struct nouveau_therm *therm, int percent)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->mode != NOUVEAU_THERM_CTRL_MANUAL)
		return -EINVAL;

	return nouveau_therm_fan_set(therm, true, percent);
}

static void
nouveau_therm_fan_set_defaults(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->fan->bios.pwm_freq = 0;
	priv->fan->bios.min_duty = 0;
	priv->fan->bios.max_duty = 100;
	priv->fan->bios.bump_period = 500;
	priv->fan->bios.slow_down_period = 2000;
	priv->fan->bios.linear_min_temp = 40;
	priv->fan->bios.linear_max_temp = 85;
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

int
nouveau_therm_fan_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_gpio *gpio = nouveau_gpio(therm);
	struct nouveau_bios *bios = nouveau_bios(therm);
	struct dcb_gpio_func func;
	int ret;

	/* attempt to locate a drivable fan, and determine control method */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN, 0xff, &func);
	if (ret == 0) {
		if (func.log[0] & DCB_GPIO_LOG_DIR_IN) {
			nv_debug(therm, "GPIO_FAN is in input mode\n");
			ret = -EINVAL;
		} else {
			ret = nouveau_fanpwm_create(therm, &func);
			if (ret != 0)
				ret = nouveau_fantog_create(therm, &func);
		}
	}

	/* no controllable fan found, create a dummy fan module */
	if (ret != 0) {
		ret = nouveau_fannil_create(therm);
		if (ret)
			return ret;
	}

	nv_info(therm, "FAN control: %s\n", priv->fan->type);

	/* read the current speed, it is useful when resuming */
	priv->fan->percent = nouveau_therm_fan_get(therm);

	/* attempt to detect a tachometer connection */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN_SENSE, 0xff, &priv->fan->tach);
	if (ret)
		priv->fan->tach.func = DCB_GPIO_UNUSED;

	/* initialise fan bump/slow update handling */
	priv->fan->parent = therm;
	nouveau_alarm_init(&priv->fan->alarm, nouveau_fan_alarm);
	spin_lock_init(&priv->fan->lock);

	/* other random init... */
	nouveau_therm_fan_set_defaults(therm);
	nvbios_perf_fan_parse(bios, &priv->fan->perf);
	if (nvbios_therm_fan_parse(bios, &priv->fan->bios))
		nv_error(therm, "parsing the thermal table failed\n");
	nouveau_therm_fan_safety_checks(therm);
	return 0;
}
