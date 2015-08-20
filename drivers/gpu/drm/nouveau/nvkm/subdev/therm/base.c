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

static int
nvkm_therm_update_trip(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvbios_therm_trip_point *trip = therm->fan->bios.trip,
				       *cur_trip = NULL,
				       *last_trip = therm->last_trip;
	u8  temp = therm->base.temp_get(&therm->base);
	u16 duty, i;

	/* look for the trip point corresponding to the current temperature */
	cur_trip = NULL;
	for (i = 0; i < therm->fan->bios.nr_fan_trip; i++) {
		if (temp >= trip[i].temp)
			cur_trip = &trip[i];
	}

	/* account for the hysteresis cycle */
	if (last_trip && temp <= (last_trip->temp) &&
	    temp > (last_trip->temp - last_trip->hysteresis))
		cur_trip = last_trip;

	if (cur_trip) {
		duty = cur_trip->fan_duty;
		therm->last_trip = cur_trip;
	} else {
		duty = 0;
		therm->last_trip = NULL;
	}

	return duty;
}

static int
nvkm_therm_update_linear(struct nvkm_therm *obj)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	u8  linear_min_temp = therm->fan->bios.linear_min_temp;
	u8  linear_max_temp = therm->fan->bios.linear_max_temp;
	u8  temp = therm->base.temp_get(&therm->base);
	u16 duty;

	/* handle the non-linear part first */
	if (temp < linear_min_temp)
		return therm->fan->bios.min_duty;
	else if (temp > linear_max_temp)
		return therm->fan->bios.max_duty;

	/* we are in the linear zone */
	duty  = (temp - linear_min_temp);
	duty *= (therm->fan->bios.max_duty - therm->fan->bios.min_duty);
	duty /= (linear_max_temp - linear_min_temp);
	duty += therm->fan->bios.min_duty;
	return duty;
}

static void
nvkm_therm_update(struct nvkm_therm *obj, int mode)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_subdev *subdev = &therm->base.subdev;
	struct nvkm_timer *tmr = subdev->device->timer;
	unsigned long flags;
	bool immd = true;
	bool poll = true;
	int duty = -1;

	spin_lock_irqsave(&therm->lock, flags);
	if (mode < 0)
		mode = therm->mode;
	therm->mode = mode;

	switch (mode) {
	case NVKM_THERM_CTRL_MANUAL:
		tmr->alarm_cancel(tmr, &therm->alarm);
		duty = nvkm_therm_fan_get(&therm->base);
		if (duty < 0)
			duty = 100;
		poll = false;
		break;
	case NVKM_THERM_CTRL_AUTO:
		switch(therm->fan->bios.fan_mode) {
		case NVBIOS_THERM_FAN_TRIP:
			duty = nvkm_therm_update_trip(&therm->base);
			break;
		case NVBIOS_THERM_FAN_LINEAR:
			duty = nvkm_therm_update_linear(&therm->base);
			break;
		case NVBIOS_THERM_FAN_OTHER:
			if (therm->cstate)
				duty = therm->cstate;
			poll = false;
			break;
		}
		immd = false;
		break;
	case NVKM_THERM_CTRL_NONE:
	default:
		tmr->alarm_cancel(tmr, &therm->alarm);
		poll = false;
	}

	if (list_empty(&therm->alarm.head) && poll)
		tmr->alarm(tmr, 1000000000ULL, &therm->alarm);
	spin_unlock_irqrestore(&therm->lock, flags);

	if (duty >= 0) {
		nvkm_debug(subdev, "FAN target request: %d%%\n", duty);
		nvkm_therm_fan_set(&therm->base, immd, duty);
	}
}

int
nvkm_therm_cstate(struct nvkm_therm *obj, int fan, int dir)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_subdev *subdev = &therm->base.subdev;
	if (!dir || (dir < 0 && fan < therm->cstate) ||
		    (dir > 0 && fan > therm->cstate)) {
		nvkm_debug(subdev, "default fan speed -> %d%%\n", fan);
		therm->cstate = fan;
		nvkm_therm_update(&therm->base, -1);
	}
	return 0;
}

static void
nvkm_therm_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_therm_priv *therm =
	       container_of(alarm, struct nvkm_therm_priv, alarm);
	nvkm_therm_update(&therm->base, -1);
}

int
nvkm_therm_fan_mode(struct nvkm_therm *obj, int mode)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);
	struct nvkm_subdev *subdev = &therm->base.subdev;
	struct nvkm_device *device = subdev->device;
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
	if (mode == NVKM_THERM_CTRL_AUTO &&
	    therm->base.temp_get(&therm->base) < 0)
		return -EINVAL;

	if (therm->mode == mode)
		return 0;

	nvkm_debug(subdev, "fan management: %s\n", name[mode]);
	nvkm_therm_update(&therm->base, mode);
	return 0;
}

int
nvkm_therm_attr_get(struct nvkm_therm *obj, enum nvkm_therm_attr_type type)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);

	switch (type) {
	case NVKM_THERM_ATTR_FAN_MIN_DUTY:
		return therm->fan->bios.min_duty;
	case NVKM_THERM_ATTR_FAN_MAX_DUTY:
		return therm->fan->bios.max_duty;
	case NVKM_THERM_ATTR_FAN_MODE:
		return therm->mode;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST:
		return therm->bios_sensor.thrs_fan_boost.temp;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST:
		return therm->bios_sensor.thrs_fan_boost.hysteresis;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK:
		return therm->bios_sensor.thrs_down_clock.temp;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST:
		return therm->bios_sensor.thrs_down_clock.hysteresis;
	case NVKM_THERM_ATTR_THRS_CRITICAL:
		return therm->bios_sensor.thrs_critical.temp;
	case NVKM_THERM_ATTR_THRS_CRITICAL_HYST:
		return therm->bios_sensor.thrs_critical.hysteresis;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN:
		return therm->bios_sensor.thrs_shutdown.temp;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST:
		return therm->bios_sensor.thrs_shutdown.hysteresis;
	}

	return -EINVAL;
}

int
nvkm_therm_attr_set(struct nvkm_therm *obj,
		    enum nvkm_therm_attr_type type, int value)
{
	struct nvkm_therm_priv *therm = container_of(obj, typeof(*therm), base);

	switch (type) {
	case NVKM_THERM_ATTR_FAN_MIN_DUTY:
		if (value < 0)
			value = 0;
		if (value > therm->fan->bios.max_duty)
			value = therm->fan->bios.max_duty;
		therm->fan->bios.min_duty = value;
		return 0;
	case NVKM_THERM_ATTR_FAN_MAX_DUTY:
		if (value < 0)
			value = 0;
		if (value < therm->fan->bios.min_duty)
			value = therm->fan->bios.min_duty;
		therm->fan->bios.max_duty = value;
		return 0;
	case NVKM_THERM_ATTR_FAN_MODE:
		return nvkm_therm_fan_mode(&therm->base, value);
	case NVKM_THERM_ATTR_THRS_FAN_BOOST:
		therm->bios_sensor.thrs_fan_boost.temp = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST:
		therm->bios_sensor.thrs_fan_boost.hysteresis = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK:
		therm->bios_sensor.thrs_down_clock.temp = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST:
		therm->bios_sensor.thrs_down_clock.hysteresis = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL:
		therm->bios_sensor.thrs_critical.temp = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL_HYST:
		therm->bios_sensor.thrs_critical.hysteresis = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN:
		therm->bios_sensor.thrs_shutdown.temp = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST:
		therm->bios_sensor.thrs_shutdown.hysteresis = value;
		therm->sensor.program_alarms(&therm->base);
		return 0;
	}

	return -EINVAL;
}

int
_nvkm_therm_init(struct nvkm_object *object)
{
	struct nvkm_therm_priv *therm = (void *)object;
	int ret;

	ret = nvkm_subdev_init(&therm->base.subdev);
	if (ret)
		return ret;

	if (therm->suspend >= 0) {
		/* restore the pwm value only when on manual or auto mode */
		if (therm->suspend > 0)
			nvkm_therm_fan_set(&therm->base, true, therm->fan->percent);

		nvkm_therm_fan_mode(&therm->base, therm->suspend);
	}
	nvkm_therm_sensor_init(&therm->base);
	nvkm_therm_fan_init(&therm->base);
	return 0;
}

int
_nvkm_therm_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_therm_priv *therm = (void *)object;

	nvkm_therm_fan_fini(&therm->base, suspend);
	nvkm_therm_sensor_fini(&therm->base, suspend);
	if (suspend) {
		therm->suspend = therm->mode;
		therm->mode = NVKM_THERM_CTRL_NONE;
	}

	return nvkm_subdev_fini(&therm->base.subdev, suspend);
}

int
nvkm_therm_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		   struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_therm_priv *therm;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "PTHERM",
				  "therm", length, pobject);
	therm = *pobject;
	if (ret)
		return ret;

	nvkm_alarm_init(&therm->alarm, nvkm_therm_alarm);
	spin_lock_init(&therm->lock);
	spin_lock_init(&therm->sensor.alarm_program_lock);

	therm->base.fan_get = nvkm_therm_fan_user_get;
	therm->base.fan_set = nvkm_therm_fan_user_set;
	therm->base.fan_sense = nvkm_therm_fan_sense;
	therm->base.attr_get = nvkm_therm_attr_get;
	therm->base.attr_set = nvkm_therm_attr_set;
	therm->mode = therm->suspend = -1; /* undefined */
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
	struct nvkm_therm_priv *therm = (void *)object;
	kfree(therm->fan);
	nvkm_subdev_destroy(&therm->base.subdev);
}
