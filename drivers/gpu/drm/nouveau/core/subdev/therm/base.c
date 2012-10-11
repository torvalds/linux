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

int
nouveau_therm_attr_get(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	switch (type) {
	case NOUVEAU_THERM_ATTR_FAN_MIN_DUTY:
		return priv->bios_fan.min_duty;
	case NOUVEAU_THERM_ATTR_FAN_MAX_DUTY:
		return priv->bios_fan.max_duty;
	case NOUVEAU_THERM_ATTR_FAN_MODE:
		return priv->fan.mode;
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
		if (value > priv->bios_fan.max_duty)
			value = priv->bios_fan.max_duty;
		priv->bios_fan.min_duty = value;
		return 0;
	case NOUVEAU_THERM_ATTR_FAN_MAX_DUTY:
		if (value < 0)
			value = 0;
		if (value < priv->bios_fan.min_duty)
			value = priv->bios_fan.min_duty;
		priv->bios_fan.max_duty = value;
		return 0;
	case NOUVEAU_THERM_ATTR_FAN_MODE:
		return nouveau_therm_fan_set_mode(therm, value);
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
nouveau_therm_init(struct nouveau_object *object)
{
	struct nouveau_therm *therm = (void *)object;
	struct nouveau_therm_priv *priv = (void *)therm;
	int ret;

	ret = nouveau_subdev_init(&therm->base);
	if (ret)
		return ret;

	if (priv->fan.percent >= 0)
		therm->fan_set(therm, priv->fan.percent);

	return 0;
}

int
nouveau_therm_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_therm *therm = (void *)object;
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->fan.percent = therm->fan_get(therm);

	return nouveau_subdev_fini(&therm->base, suspend);
}
