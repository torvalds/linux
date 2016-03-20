/*
 * Copyright 2012 The Nouveau community
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
#include "priv.h"

#include <subdev/gpio.h>
#include <subdev/timer.h>

struct nvkm_fantog {
	struct nvkm_fan base;
	struct nvkm_alarm alarm;
	spinlock_t lock;
	u32 period_us;
	u32 percent;
	struct dcb_gpio_func func;
};

static void
nvkm_fantog_update(struct nvkm_fantog *fan, int percent)
{
	struct nvkm_therm *therm = fan->base.parent;
	struct nvkm_device *device = therm->subdev.device;
	struct nvkm_timer *tmr = device->timer;
	struct nvkm_gpio *gpio = device->gpio;
	unsigned long flags;
	int duty;

	spin_lock_irqsave(&fan->lock, flags);
	if (percent < 0)
		percent = fan->percent;
	fan->percent = percent;

	duty = !nvkm_gpio_get(gpio, 0, DCB_GPIO_FAN, 0xff);
	nvkm_gpio_set(gpio, 0, DCB_GPIO_FAN, 0xff, duty);

	if (list_empty(&fan->alarm.head) && percent != (duty * 100)) {
		u64 next_change = (percent * fan->period_us) / 100;
		if (!duty)
			next_change = fan->period_us - next_change;
		nvkm_timer_alarm(tmr, next_change * 1000, &fan->alarm);
	}
	spin_unlock_irqrestore(&fan->lock, flags);
}

static void
nvkm_fantog_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_fantog *fan =
	       container_of(alarm, struct nvkm_fantog, alarm);
	nvkm_fantog_update(fan, -1);
}

static int
nvkm_fantog_get(struct nvkm_therm *therm)
{
	struct nvkm_fantog *fan = (void *)therm->fan;
	return fan->percent;
}

static int
nvkm_fantog_set(struct nvkm_therm *therm, int percent)
{
	struct nvkm_fantog *fan = (void *)therm->fan;
	if (therm->func->pwm_ctrl)
		therm->func->pwm_ctrl(therm, fan->func.line, false);
	nvkm_fantog_update(fan, percent);
	return 0;
}

int
nvkm_fantog_create(struct nvkm_therm *therm, struct dcb_gpio_func *func)
{
	struct nvkm_fantog *fan;
	int ret;

	if (therm->func->pwm_ctrl) {
		ret = therm->func->pwm_ctrl(therm, func->line, false);
		if (ret)
			return ret;
	}

	fan = kzalloc(sizeof(*fan), GFP_KERNEL);
	therm->fan = &fan->base;
	if (!fan)
		return -ENOMEM;

	fan->base.type = "toggle";
	fan->base.get = nvkm_fantog_get;
	fan->base.set = nvkm_fantog_set;
	nvkm_alarm_init(&fan->alarm, nvkm_fantog_alarm);
	fan->period_us = 100000; /* 10Hz */
	fan->percent = 100;
	fan->func = *func;
	spin_lock_init(&fan->lock);
	return 0;
}
