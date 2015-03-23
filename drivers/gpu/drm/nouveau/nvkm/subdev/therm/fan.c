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

#include <subdev/bios/fan.h>
#include <subdev/gpio.h>
#include <subdev/timer.h>

static int
nvkm_fan_update(struct nvkm_fan *fan, bool immediate, int target)
{
	struct nvkm_therm *therm = fan->parent;
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvkm_timer *ptimer = nvkm_timer(priv);
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
	if (duty == target) {
		spin_unlock_irqrestore(&fan->lock, flags);
		return 0;
	}

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
	if (ret) {
		spin_unlock_irqrestore(&fan->lock, flags);
		return ret;
	}

	/* fan speed updated, drop the fan lock before grabbing the
	 * alarm-scheduling lock and risking a deadlock
	 */
	spin_unlock_irqrestore(&fan->lock, flags);

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

	return ret;
}

static void
nvkm_fan_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_fan *fan = container_of(alarm, struct nvkm_fan, alarm);
	nvkm_fan_update(fan, false, -1);
}

int
nvkm_therm_fan_get(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	return priv->fan->get(therm);
}

int
nvkm_therm_fan_set(struct nvkm_therm *therm, bool immediate, int percent)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	return nvkm_fan_update(priv->fan, immediate, percent);
}

int
nvkm_therm_fan_sense(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvkm_timer *ptimer = nvkm_timer(therm);
	struct nvkm_gpio *gpio = nvkm_gpio(therm);
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
nvkm_therm_fan_user_get(struct nvkm_therm *therm)
{
	return nvkm_therm_fan_get(therm);
}

int
nvkm_therm_fan_user_set(struct nvkm_therm *therm, int percent)
{
	struct nvkm_therm_priv *priv = (void *)therm;

	if (priv->mode != NVKM_THERM_CTRL_MANUAL)
		return -EINVAL;

	return nvkm_therm_fan_set(therm, true, percent);
}

static void
nvkm_therm_fan_set_defaults(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;

	priv->fan->bios.pwm_freq = 0;
	priv->fan->bios.min_duty = 0;
	priv->fan->bios.max_duty = 100;
	priv->fan->bios.bump_period = 500;
	priv->fan->bios.slow_down_period = 2000;
	priv->fan->bios.linear_min_temp = 40;
	priv->fan->bios.linear_max_temp = 85;
}

static void
nvkm_therm_fan_safety_checks(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;

	if (priv->fan->bios.min_duty > 100)
		priv->fan->bios.min_duty = 100;
	if (priv->fan->bios.max_duty > 100)
		priv->fan->bios.max_duty = 100;

	if (priv->fan->bios.min_duty > priv->fan->bios.max_duty)
		priv->fan->bios.min_duty = priv->fan->bios.max_duty;
}

int
nvkm_therm_fan_init(struct nvkm_therm *therm)
{
	return 0;
}

int
nvkm_therm_fan_fini(struct nvkm_therm *therm, bool suspend)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvkm_timer *ptimer = nvkm_timer(therm);

	if (suspend)
		ptimer->alarm_cancel(ptimer, &priv->fan->alarm);
	return 0;
}

int
nvkm_therm_fan_ctor(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvkm_gpio *gpio = nvkm_gpio(therm);
	struct nvkm_bios *bios = nvkm_bios(therm);
	struct dcb_gpio_func func;
	int ret;

	/* attempt to locate a drivable fan, and determine control method */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN, 0xff, &func);
	if (ret == 0) {
		/* FIXME: is this really the place to perform such checks ? */
		if (func.line != 16 && func.log[0] & DCB_GPIO_LOG_DIR_IN) {
			nv_debug(therm, "GPIO_FAN is in input mode\n");
			ret = -EINVAL;
		} else {
			ret = nvkm_fanpwm_create(therm, &func);
			if (ret != 0)
				ret = nvkm_fantog_create(therm, &func);
		}
	}

	/* no controllable fan found, create a dummy fan module */
	if (ret != 0) {
		ret = nvkm_fannil_create(therm);
		if (ret)
			return ret;
	}

	nv_info(therm, "FAN control: %s\n", priv->fan->type);

	/* read the current speed, it is useful when resuming */
	priv->fan->percent = nvkm_therm_fan_get(therm);

	/* attempt to detect a tachometer connection */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN_SENSE, 0xff, &priv->fan->tach);
	if (ret)
		priv->fan->tach.func = DCB_GPIO_UNUSED;

	/* initialise fan bump/slow update handling */
	priv->fan->parent = therm;
	nvkm_alarm_init(&priv->fan->alarm, nvkm_fan_alarm);
	spin_lock_init(&priv->fan->lock);

	/* other random init... */
	nvkm_therm_fan_set_defaults(therm);
	nvbios_perf_fan_parse(bios, &priv->fan->perf);
	if (!nvbios_fan_parse(bios, &priv->fan->bios)) {
		nv_debug(therm, "parsing the fan table failed\n");
		if (nvbios_therm_fan_parse(bios, &priv->fan->bios))
			nv_error(therm, "parsing both fan tables failed\n");
	}
	nvkm_therm_fan_safety_checks(therm);
	return 0;
}
