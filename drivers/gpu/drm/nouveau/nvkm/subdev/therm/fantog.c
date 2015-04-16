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

struct nvkm_fantog_priv {
	struct nvkm_fan base;
	struct nvkm_alarm alarm;
	spinlock_t lock;
	u32 period_us;
	u32 percent;
	struct dcb_gpio_func func;
};

static void
nvkm_fantog_update(struct nvkm_fantog_priv *priv, int percent)
{
	struct nvkm_therm_priv *tpriv = (void *)priv->base.parent;
	struct nvkm_timer *ptimer = nvkm_timer(tpriv);
	struct nvkm_gpio *gpio = nvkm_gpio(tpriv);
	unsigned long flags;
	int duty;

	spin_lock_irqsave(&priv->lock, flags);
	if (percent < 0)
		percent = priv->percent;
	priv->percent = percent;

	duty = !gpio->get(gpio, 0, DCB_GPIO_FAN, 0xff);
	gpio->set(gpio, 0, DCB_GPIO_FAN, 0xff, duty);

	if (list_empty(&priv->alarm.head) && percent != (duty * 100)) {
		u64 next_change = (percent * priv->period_us) / 100;
		if (!duty)
			next_change = priv->period_us - next_change;
		ptimer->alarm(ptimer, next_change * 1000, &priv->alarm);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void
nvkm_fantog_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_fantog_priv *priv =
	       container_of(alarm, struct nvkm_fantog_priv, alarm);
	nvkm_fantog_update(priv, -1);
}

static int
nvkm_fantog_get(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *tpriv = (void *)therm;
	struct nvkm_fantog_priv *priv = (void *)tpriv->fan;
	return priv->percent;
}

static int
nvkm_fantog_set(struct nvkm_therm *therm, int percent)
{
	struct nvkm_therm_priv *tpriv = (void *)therm;
	struct nvkm_fantog_priv *priv = (void *)tpriv->fan;
	if (therm->pwm_ctrl)
		therm->pwm_ctrl(therm, priv->func.line, false);
	nvkm_fantog_update(priv, percent);
	return 0;
}

int
nvkm_fantog_create(struct nvkm_therm *therm, struct dcb_gpio_func *func)
{
	struct nvkm_therm_priv *tpriv = (void *)therm;
	struct nvkm_fantog_priv *priv;
	int ret;

	if (therm->pwm_ctrl) {
		ret = therm->pwm_ctrl(therm, func->line, false);
		if (ret)
			return ret;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	tpriv->fan = &priv->base;
	if (!priv)
		return -ENOMEM;

	priv->base.type = "toggle";
	priv->base.get = nvkm_fantog_get;
	priv->base.set = nvkm_fantog_set;
	nvkm_alarm_init(&priv->alarm, nvkm_fantog_alarm);
	priv->period_us = 100000; /* 10Hz */
	priv->percent = 100;
	priv->func = *func;
	spin_lock_init(&priv->lock);
	return 0;
}
