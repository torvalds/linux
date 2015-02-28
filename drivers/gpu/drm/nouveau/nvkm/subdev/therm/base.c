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

#include <core/device.h>

static int
nvkm_therm_update_trip(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvbios_therm_trip_point *trip = priv->fan->bios.trip,
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
nvkm_therm_update_linear(struct nvkm_therm *therm)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	u8  linear_min_temp = priv->fan->bios.linear_min_temp;
	u8  linear_max_temp = priv->fan->bios.linear_max_temp;
	u8  temp = therm->temp_get(therm);
	u16 duty;

	/* handle the non-linear part first */
	if (temp < linear_min_temp)
		return priv->fan->bios.min_duty;
	else if (temp > linear_max_temp)
		return priv->fan->bios.max_duty;

	/* we are in the linear zone */
	duty  = (temp - linear_min_temp);
	duty *= (priv->fan->bios.max_duty - priv->fan->bios.min_duty);
	duty /= (linear_max_temp - linear_min_temp);
	duty += priv->fan->bios.min_duty;
	return duty;
}

static void
nvkm_therm_update(struct nvkm_therm *therm, int mode)
{
	struct nvkm_timer *ptimer = nvkm_timer(therm);
	struct nvkm_therm_priv *priv = (void *)therm;
	unsigned long flags;
	bool immd = true;
	bool poll = true;
	int duty = -1;

	spin_lock_irqsave(&priv->lock, flags);
	if (mode < 0)
		mode = priv->mode;
	priv->mode = mode;

	switch (mode) {
	case NVKM_THERM_CTRL_MANUAL:
		ptimer->alarm_cancel(ptimer, &priv->alarm);
		duty = nvkm_therm_fan_get(therm);
		if (duty < 0)
			duty = 100;
		poll = false;
		break;
	case NVKM_THERM_CTRL_AUTO:
		switch(priv->fan->bios.fan_mode) {
		case NVBIOS_THERM_FAN_TRIP:
			duty = nvkm_therm_update_trip(therm);
			break;
		case NVBIOS_THERM_FAN_LINEAR:
			duty = nvkm_therm_update_linear(therm);
			break;
		case NVBIOS_THERM_FAN_OTHER:
			if (priv->cstate)
				duty = priv->cstate;
			poll = false;
			break;
		}
		immd = false;
		break;
	case NVKM_THERM_CTRL_NONE:
	default:
		ptimer->alarm_cancel(ptimer, &priv->alarm);
		poll = false;
	}

	if (list_empty(&priv->alarm.head) && poll)
		ptimer->alarm(ptimer, 1000000000ULL, &priv->alarm);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (duty >= 0) {
		nv_debug(therm, "FAN target request: %d%%\n", duty);
		nvkm_therm_fan_set(therm, immd, duty);
	}
}

int
nvkm_therm_cstate(struct nvkm_therm *ptherm, int fan, int dir)
{
	struct nvkm_therm_priv *priv = (void *)ptherm;
	if (!dir || (dir < 0 && fan < priv->cstate) ||
		    (dir > 0 && fan > priv->cstate)) {
		nv_debug(ptherm, "default fan speed -> %d%%\n", fan);
		priv->cstate = fan;
		nvkm_therm_update(ptherm, -1);
	}
	return 0;
}

static void
nvkm_therm_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_therm_priv *priv =
	       container_of(alarm, struct nvkm_therm_priv, alarm);
	nvkm_therm_update(&priv->base, -1);
}

int
nvkm_therm_fan_mode(struct nvkm_therm *therm, int mode)
{
	struct nvkm_therm_priv *priv = (void *)therm;
	struct nvkm_device *device = nv_device(therm);
	static const char *name[] = {
		"disabled",
		"manual",
		"automatic"
	};

	/* The default PPWR ucode on fermi interferes with fan management */
	if ((mode >= ARRAY_SIZE(name)) ||
	    (mode != NVKM_THERM_CTRL_NONE && device->card_type >= NV_C0 &&
	     !nvkm_subdev(device, NVDEV_SUBDEV_PMU)))
		return -EINVAL;

	/* do not allow automatic fan management if the thermal sensor is
	 * not available */
	if (mode == NVKM_THERM_CTRL_AUTO && therm->temp_get(therm) < 0)
		return -EINVAL;

	if (priv->mode == mode)
		return 0;

	nv_info(therm, "fan management: %s\n", name[mode]);
	nvkm_therm_update(therm, mode);
	return 0;
}

int
nvkm_therm_attr_get(struct nvkm_therm *therm,
		       enum nvkm_therm_attr_type type)
{
	struct nvkm_therm_priv *priv = (void *)therm;

	switch (type) {
	case NVKM_THERM_ATTR_FAN_MIN_DUTY:
		return priv->fan->bios.min_duty;
	case NVKM_THERM_ATTR_FAN_MAX_DUTY:
		return priv->fan->bios.max_duty;
	case NVKM_THERM_ATTR_FAN_MODE:
		return priv->mode;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST:
		return priv->bios_sensor.thrs_fan_boost.temp;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST:
		return priv->bios_sensor.thrs_fan_boost.hysteresis;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK:
		return priv->bios_sensor.thrs_down_clock.temp;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST:
		return priv->bios_sensor.thrs_down_clock.hysteresis;
	case NVKM_THERM_ATTR_THRS_CRITICAL:
		return priv->bios_sensor.thrs_critical.temp;
	case NVKM_THERM_ATTR_THRS_CRITICAL_HYST:
		return priv->bios_sensor.thrs_critical.hysteresis;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN:
		return priv->bios_sensor.thrs_shutdown.temp;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST:
		return priv->bios_sensor.thrs_shutdown.hysteresis;
	}

	return -EINVAL;
}

int
nvkm_therm_attr_set(struct nvkm_therm *therm,
		    enum nvkm_therm_attr_type type, int value)
{
	struct nvkm_therm_priv *priv = (void *)therm;

	switch (type) {
	case NVKM_THERM_ATTR_FAN_MIN_DUTY:
		if (value < 0)
			value = 0;
		if (value > priv->fan->bios.max_duty)
			value = priv->fan->bios.max_duty;
		priv->fan->bios.min_duty = value;
		return 0;
	case NVKM_THERM_ATTR_FAN_MAX_DUTY:
		if (value < 0)
			value = 0;
		if (value < priv->fan->bios.min_duty)
			value = priv->fan->bios.min_duty;
		priv->fan->bios.max_duty = value;
		return 0;
	case NVKM_THERM_ATTR_FAN_MODE:
		return nvkm_therm_fan_mode(therm, value);
	case NVKM_THERM_ATTR_THRS_FAN_BOOST:
		priv->bios_sensor.thrs_fan_boost.temp = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST:
		priv->bios_sensor.thrs_fan_boost.hysteresis = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK:
		priv->bios_sensor.thrs_down_clock.temp = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST:
		priv->bios_sensor.thrs_down_clock.hysteresis = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL:
		priv->bios_sensor.thrs_critical.temp = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL_HYST:
		priv->bios_sensor.thrs_critical.hysteresis = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN:
		priv->bios_sensor.thrs_shutdown.temp = value;
		priv->sensor.program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST:
		priv->bios_sensor.thrs_shutdown.hysteresis = value;
		priv->sensor.program_alarms(therm);
		return 0;
	}

	return -EINVAL;
}

int
_nvkm_therm_init(struct nvkm_object *object)
{
	struct nvkm_therm *therm = (void *)object;
	struct nvkm_therm_priv *priv = (void *)therm;
	int ret;

	ret = nvkm_subdev_init(&therm->base);
	if (ret)
		return ret;

	if (priv->suspend >= 0) {
		/* restore the pwm value only when on manual or auto mode */
		if (priv->suspend > 0)
			nvkm_therm_fan_set(therm, true, priv->fan->percent);

		nvkm_therm_fan_mode(therm, priv->suspend);
	}
	nvkm_therm_sensor_init(therm);
	nvkm_therm_fan_init(therm);
	return 0;
}

int
_nvkm_therm_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_therm *therm = (void *)object;
	struct nvkm_therm_priv *priv = (void *)therm;

	nvkm_therm_fan_fini(therm, suspend);
	nvkm_therm_sensor_fini(therm, suspend);
	if (suspend) {
		priv->suspend = priv->mode;
		priv->mode = NVKM_THERM_CTRL_NONE;
	}

	return nvkm_subdev_fini(&therm->base, suspend);
}

int
nvkm_therm_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_therm_priv *priv;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "PTHERM",
				  "therm", length, pobject);
	priv = *pobject;
	if (ret)
		return ret;

	nvkm_alarm_init(&priv->alarm, nvkm_therm_alarm);
	spin_lock_init(&priv->lock);
	spin_lock_init(&priv->sensor.alarm_program_lock);

	priv->base.fan_get = nvkm_therm_fan_user_get;
	priv->base.fan_set = nvkm_therm_fan_user_set;
	priv->base.fan_sense = nvkm_therm_fan_sense;
	priv->base.attr_get = nvkm_therm_attr_get;
	priv->base.attr_set = nvkm_therm_attr_set;
	priv->mode = priv->suspend = -1; /* undefined */
	return 0;
}

int
nvkm_therm_preinit(struct nvkm_therm *therm)
{
	nvkm_therm_sensor_ctor(therm);
	nvkm_therm_ic_ctor(therm);
	nvkm_therm_fan_ctor(therm);

	nvkm_therm_fan_mode(therm, NVKM_THERM_CTRL_AUTO);
	nvkm_therm_sensor_preinit(therm);
	return 0;
}

void
_nvkm_therm_dtor(struct nvkm_object *object)
{
	struct nvkm_therm_priv *priv = (void *)object;
	kfree(priv->fan);
	nvkm_subdev_destroy(&priv->base.base);
}
