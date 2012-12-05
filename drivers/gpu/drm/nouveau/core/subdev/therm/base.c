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

#include <core/object.h>
#include <core/device.h>

#include <subdev/bios.h>

#include "priv.h"

static int
nouveau_therm_update_trip(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_therm_trip_point *trip = priv->fan->bios.trip,
					*cur_trip = NULL,
					*last_trip = priv->last_trip;
	u8  temp = therm->temp_get(therm);
	u16 duty, i;

	/* look for the trip point corresponding to the current temperature */
	cur_trip = NULL;
	for (i = 0; i < priv->fan->bios.nr_fan_trip; i++) {
		if (temp >= trip[i].temp)
			cur_trip = &trip[i];
	}

	/* account for the hysteresis cycle */
	if (last_trip && temp <= (last_trip->temp) &&
	    temp > (last_trip->temp - last_trip->hysteresis))
		cur_trip = last_trip;

	if (cur_trip) {
		duty = cur_trip->fan_duty;
		priv->last_trip = cur_trip;
	} else {
		duty = 0;
		priv->last_trip = NULL;
	}

	return duty;
}

static int
nouveau_therm_update_linear(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	u8  linear_min_temp = priv->fan->bios.linear_min_temp;
	u8  linear_max_temp = priv->fan->bios.linear_max_temp;
	u8  temp = therm->temp_get(therm);
	u16 duty;

	duty  = (temp - linear_min_temp);
	duty *= (priv->fan->bios.max_duty - priv->fan->bios.min_duty);
	duty /= (linear_max_temp - linear_min_temp);
	duty += priv->fan->bios.min_duty;

	return duty;
}

static void
nouveau_therm_update(struct nouveau_therm *therm, int mode)
{
	struct nouveau_timer *ptimer = nouveau_timer(therm);
	struct nouveau_therm_priv *priv = (void *)therm;
	unsigned long flags;
	int duty;

	spin_lock_irqsave(&priv->lock, flags);
	if (mode < 0)
		mode = priv->mode;
	priv->mode = mode;

	switch (mode) {
	case FAN_CONTROL_MANUAL:
		duty = priv->fan->percent;
		break;
	case FAN_CONTROL_AUTO:
		if (priv->fan->bios.nr_fan_trip)
			duty = nouveau_therm_update_trip(therm);
		else
			duty = nouveau_therm_update_linear(therm);
		break;
	case FAN_CONTROL_NONE:
	default:
		goto done;
	}

	nouveau_therm_fan_set(therm, (mode != FAN_CONTROL_AUTO), duty);

done:
	if (list_empty(&priv->alarm.head) && (mode == FAN_CONTROL_AUTO))
		ptimer->alarm(ptimer, 1000000000ULL, &priv->alarm);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void
nouveau_therm_alarm(struct nouveau_alarm *alarm)
{
	struct nouveau_therm_priv *priv =
	       container_of(alarm, struct nouveau_therm_priv, alarm);
	nouveau_therm_update(&priv->base, -1);
}

static int
nouveau_therm_mode(struct nouveau_therm *therm, int mode)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (priv->mode == mode)
		return 0;

	/* The default PDAEMON ucode interferes with fan management */
	if (nv_device(therm)->card_type >= NV_C0)
		return -EINVAL;

	switch (mode) {
	case FAN_CONTROL_NONE:
		nv_info(therm, "switch to no-control mode\n");
		break;
	case FAN_CONTROL_MANUAL:
		nv_info(therm, "switch to manual mode\n");
		break;
	case FAN_CONTROL_AUTO:
		nv_info(therm, "switch to automatic mode\n");
		break;
	default:
		return -EINVAL;
	}

	nouveau_therm_update(therm, mode);
	return 0;
}

int
nouveau_therm_attr_get(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	switch (type) {
	case NOUVEAU_THERM_ATTR_FAN_MIN_DUTY:
		return priv->fan->bios.min_duty;
	case NOUVEAU_THERM_ATTR_FAN_MAX_DUTY:
		return priv->fan->bios.max_duty;
	case NOUVEAU_THERM_ATTR_FAN_MODE:
		return priv->mode;
	case NOUVEAU_THERM_ATTR_THRS_FAN_BOOST:
		return priv->bios_sensor.thrs_fan_boost.temp;
	case NOUVEAU_THERM_ATTR_THRS_FAN_BOOST_HYST:
		return priv->bios_sensor.thrs_fan_boost.hysteresis;
	case NOUVEAU_THERM_ATTR_THRS_DOWN_CLK:
		return priv->bios_sensor.thrs_down_clock.temp;
	case NOUVEAU_THERM_ATTR_THRS_DOWN_CLK_HYST:
		return priv->bios_sensor.thrs_down_clock.hysteresis;
	case NOUVEAU_THERM_ATTR_THRS_CRITICAL:
		return priv->bios_sensor.thrs_critical.temp;
	case NOUVEAU_THERM_ATTR_THRS_CRITICAL_HYST:
		return priv->bios_sensor.thrs_critical.hysteresis;
	case NOUVEAU_THERM_ATTR_THRS_SHUTDOWN:
		return priv->bios_sensor.thrs_shutdown.temp;
	case NOUVEAU_THERM_ATTR_THRS_SHUTDOWN_HYST:
		return priv->bios_sensor.thrs_shutdown.hysteresis;
	}

	return -EINVAL;
}

int
nouveau_therm_attr_set(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type, int value)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	switch (type) {
	case NOUVEAU_THERM_ATTR_FAN_MIN_DUTY:
		if (value < 0)
			value = 0;
		if (value > priv->fan->bios.max_duty)
			value = priv->fan->bios.max_duty;
		priv->fan->bios.min_duty = value;
		return 0;
	case NOUVEAU_THERM_ATTR_FAN_MAX_DUTY:
		if (value < 0)
			value = 0;
		if (value < priv->fan->bios.min_duty)
			value = priv->fan->bios.min_duty;
		priv->fan->bios.max_duty = value;
		return 0;
	case NOUVEAU_THERM_ATTR_FAN_MODE:
		return nouveau_therm_mode(therm, value);
	case NOUVEAU_THERM_ATTR_THRS_FAN_BOOST:
		priv->bios_sensor.thrs_fan_boost.temp = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_FAN_BOOST_HYST:
		priv->bios_sensor.thrs_fan_boost.hysteresis = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_DOWN_CLK:
		priv->bios_sensor.thrs_down_clock.temp = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_DOWN_CLK_HYST:
		priv->bios_sensor.thrs_down_clock.hysteresis = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_CRITICAL:
		priv->bios_sensor.thrs_critical.temp = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_CRITICAL_HYST:
		priv->bios_sensor.thrs_critical.hysteresis = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_SHUTDOWN:
		priv->bios_sensor.thrs_shutdown.temp = value;
		return 0;
	case NOUVEAU_THERM_ATTR_THRS_SHUTDOWN_HYST:
		priv->bios_sensor.thrs_shutdown.hysteresis = value;
		return 0;
	}

	return -EINVAL;
}

int
_nouveau_therm_init(struct nouveau_object *object)
{
	struct nouveau_therm *therm = (void *)object;
	struct nouveau_therm_priv *priv = (void *)therm;
	int ret;

	ret = nouveau_subdev_init(&therm->base);
	if (ret)
		return ret;

	if (priv->fan->percent >= 0)
		therm->fan_set(therm, priv->fan->percent);

	return 0;
}

int
_nouveau_therm_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_therm *therm = (void *)object;
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->fan->percent = therm->fan_get(therm);

	return nouveau_subdev_fini(&therm->base, suspend);
}

int
nouveau_therm_create_(struct nouveau_object *parent,
		      struct nouveau_object *engine,
		      struct nouveau_oclass *oclass,
		      int length, void **pobject)
{
	struct nouveau_therm_priv *priv;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "PTHERM",
				     "therm", length, pobject);
	priv = *pobject;
	if (ret)
		return ret;

	nouveau_alarm_init(&priv->alarm, nouveau_therm_alarm);
	spin_lock_init(&priv->lock);

	priv->base.fan_get = nouveau_therm_fan_user_get;
	priv->base.fan_set = nouveau_therm_fan_user_set;
	priv->base.fan_sense = nouveau_therm_fan_sense;
	priv->base.attr_get = nouveau_therm_attr_get;
	priv->base.attr_set = nouveau_therm_attr_set;
	return 0;
}

int
nouveau_therm_preinit(struct nouveau_therm *therm)
{
	nouveau_therm_ic_ctor(therm);
	nouveau_therm_sensor_ctor(therm);
	nouveau_therm_fan_ctor(therm);
	return 0;
}

void
_nouveau_therm_dtor(struct nouveau_object *object)
{
	struct nouveau_therm_priv *priv = (void *)object;
	kfree(priv->fan);
	nouveau_subdev_destroy(&priv->base.base);
}
