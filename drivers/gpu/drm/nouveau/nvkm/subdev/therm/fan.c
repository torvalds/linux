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
	struct nvkm_therm_priv *therm = (void *)fan->parent;
	struct nvkm_subdev *subdev = &therm->base.subdev;
	struct nvkm_timer *tmr = subdev->device->timer;
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
		nvkm_debug(subdev, "FAN target: %d\n", target);
		fan->percent = target;
	}

	/* check that we're not already at the target duty cycle */
	duty = fan->get(&therm->base);
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

	nvkm_debug(subdev, "FAN update: %d\n", duty);
	ret = fan->set(&therm->base, duty);
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

		tmr->alarm(tmr, delay * 1000 * 1000, &fan->alarm);
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
nvkm_therm_fan_get(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	return therm->fan->get(&therm->base);
}

int
nvkm_therm_fan_set(struct nvkm_therm *obj, bool immediate, int percent)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	return nvkm_fan_update(therm->fan, immediate, percent);
}

int
nvkm_therm_fan_sense(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_timer *tmr = nvkm_timer(therm);
	struct nvkm_gpio *gpio = nvkm_gpio(therm);
	u32 cycles, cur, prev;
	u64 start, end, tach;

	if (therm->fan->tach.func == DCB_GPIO_UNUSED)
		return -ENODEV;

	/* Time a complete rotation and extrapolate to RPM:
	 * When the fan spins, it changes the value of GPIO FAN_SENSE.
	 * We get 4 changes (0 -> 1 -> 0 -> 1) per complete rotation.
	 */
	start = tmr->read(tmr);
	prev = gpio->get(gpio, 0, therm->fan->tach.func, therm->fan->tach.line);
	cycles = 0;
	do {
		usleep_range(500, 1000); /* supports 0 < rpm < 7500 */

		cur = gpio->get(gpio, 0, therm->fan->tach.func, therm->fan->tach.line);
		if (prev != cur) {
			if (!start)
				start = tmr->read(tmr);
			cycles++;
			prev = cur;
		}
	} while (cycles < 5 && tmr->read(tmr) - start < 250000000);
	end = tmr->read(tmr);

	if (cycles == 5) {
		tach = (u64)60000000000ULL;
		do_div(tach, (end - start));
		return tach;
	} else
		return 0;
}

int
nvkm_therm_fan_user_get(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	return nvkm_therm_fan_get(&therm->base);
}

int
nvkm_therm_fan_user_set(struct nvkm_therm *obj, int percent)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);

	if (therm->mode != NVKM_THERM_CTRL_MANUAL)
		return -EINVAL;

	return nvkm_therm_fan_set(&therm->base, true, percent);
}

static void
nvkm_therm_fan_set_defaults(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);

	therm->fan->bios.pwm_freq = 0;
	therm->fan->bios.min_duty = 0;
	therm->fan->bios.max_duty = 100;
	therm->fan->bios.bump_period = 500;
	therm->fan->bios.slow_down_period = 2000;
	therm->fan->bios.linear_min_temp = 40;
	therm->fan->bios.linear_max_temp = 85;
}

static void
nvkm_therm_fan_safety_checks(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);

	if (therm->fan->bios.min_duty > 100)
		therm->fan->bios.min_duty = 100;
	if (therm->fan->bios.max_duty > 100)
		therm->fan->bios.max_duty = 100;

	if (therm->fan->bios.min_duty > therm->fan->bios.max_duty)
		therm->fan->bios.min_duty = therm->fan->bios.max_duty;
}

int
nvkm_therm_fan_init(struct nvkm_therm *obj)
{
	return 0;
}

int
nvkm_therm_fan_fini(struct nvkm_therm *obj, bool suspend)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_timer *tmr = nvkm_timer(therm);

	if (suspend)
		tmr->alarm_cancel(tmr, &therm->fan->alarm);
	return 0;
}

int
nvkm_therm_fan_ctor(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_subdev *subdev = &therm->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_gpio *gpio = device->gpio;
	struct nvkm_bios *bios = device->bios;
	struct dcb_gpio_func func;
	int ret;

	/* attempt to locate a drivable fan, and determine control method */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN, 0xff, &func);
	if (ret == 0) {
		/* FIXME: is this really the place to perform such checks ? */
		if (func.line != 16 && func.log[0] & DCB_GPIO_LOG_DIR_IN) {
			nvkm_debug(subdev, "GPIO_FAN is in input mode\n");
			ret = -EINVAL;
		} else {
			ret = nvkm_fanpwm_create(&therm->base, &func);
			if (ret != 0)
				ret = nvkm_fantog_create(&therm->base, &func);
		}
	}

	/* no controllable fan found, create a dummy fan module */
	if (ret != 0) {
		ret = nvkm_fannil_create(&therm->base);
		if (ret)
			return ret;
	}

	nvkm_debug(subdev, "FAN control: %s\n", therm->fan->type);

	/* read the current speed, it is useful when resuming */
	therm->fan->percent = nvkm_therm_fan_get(&therm->base);

	/* attempt to detect a tachometer connection */
	ret = gpio->find(gpio, 0, DCB_GPIO_FAN_SENSE, 0xff, &therm->fan->tach);
	if (ret)
		therm->fan->tach.func = DCB_GPIO_UNUSED;

	/* initialise fan bump/slow update handling */
	therm->fan->parent = &therm->base;
	nvkm_alarm_init(&therm->fan->alarm, nvkm_fan_alarm);
	spin_lock_init(&therm->fan->lock);

	/* other random init... */
	nvkm_therm_fan_set_defaults(&therm->base);
	nvbios_perf_fan_parse(bios, &therm->fan->perf);
	if (!nvbios_fan_parse(bios, &therm->fan->bios)) {
		nvkm_debug(subdev, "parsing the fan table failed\n");
		if (nvbios_therm_fan_parse(bios, &therm->fan->bios))
			nvkm_error(subdev, "parsing both fan tables failed\n");
	}
	nvkm_therm_fan_safety_checks(&therm->base);
	return 0;
}
