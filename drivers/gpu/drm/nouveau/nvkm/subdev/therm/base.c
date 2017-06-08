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

int
nvkm_therm_temp_get(struct nvkm_therm *therm)
{
	if (therm->func->temp_get)
		return therm->func->temp_get(therm);
	return -ENODEV;
}

static int
nvkm_therm_update_trip(struct nvkm_therm *therm)
{
	struct nvbios_therm_trip_point *trip = therm->fan->bios.trip,
				       *cur_trip = NULL,
				       *last_trip = therm->last_trip;
	u8  temp = therm->func->temp_get(therm);
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
nvkm_therm_compute_linear_duty(struct nvkm_therm *therm, u8 linear_min_temp,
                               u8 linear_max_temp)
{
	u8  temp = therm->func->temp_get(therm);
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

static int
nvkm_therm_update_linear(struct nvkm_therm *therm)
{
	u8  min = therm->fan->bios.linear_min_temp;
	u8  max = therm->fan->bios.linear_max_temp;
	return nvkm_therm_compute_linear_duty(therm, min, max);
}

static int
nvkm_therm_update_linear_fallback(struct nvkm_therm *therm)
{
	u8 max = therm->bios_sensor.thrs_fan_boost.temp;
	return nvkm_therm_compute_linear_duty(therm, 30, max);
}

static void
nvkm_therm_update(struct nvkm_therm *therm, int mode)
{
	struct nvkm_subdev *subdev = &therm->subdev;
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
		nvkm_timer_alarm_cancel(tmr, &therm->alarm);
		duty = nvkm_therm_fan_get(therm);
		if (duty < 0)
			duty = 100;
		poll = false;
		break;
	case NVKM_THERM_CTRL_AUTO:
		switch(therm->fan->bios.fan_mode) {
		case NVBIOS_THERM_FAN_TRIP:
			duty = nvkm_therm_update_trip(therm);
			break;
		case NVBIOS_THERM_FAN_LINEAR:
			duty = nvkm_therm_update_linear(therm);
			break;
		case NVBIOS_THERM_FAN_OTHER:
			if (therm->cstate)
				duty = therm->cstate;
			else
				duty = nvkm_therm_update_linear_fallback(therm);
			poll = false;
			break;
		}
		immd = false;
		break;
	case NVKM_THERM_CTRL_NONE:
	default:
		nvkm_timer_alarm_cancel(tmr, &therm->alarm);
		poll = false;
	}

	if (poll)
		nvkm_timer_alarm(tmr, 1000000000ULL, &therm->alarm);
	spin_unlock_irqrestore(&therm->lock, flags);

	if (duty >= 0) {
		nvkm_debug(subdev, "FAN target request: %d%%\n", duty);
		nvkm_therm_fan_set(therm, immd, duty);
	}
}

int
nvkm_therm_cstate(struct nvkm_therm *therm, int fan, int dir)
{
	struct nvkm_subdev *subdev = &therm->subdev;
	if (!dir || (dir < 0 && fan < therm->cstate) ||
		    (dir > 0 && fan > therm->cstate)) {
		nvkm_debug(subdev, "default fan speed -> %d%%\n", fan);
		therm->cstate = fan;
		nvkm_therm_update(therm, -1);
	}
	return 0;
}

static void
nvkm_therm_alarm(struct nvkm_alarm *alarm)
{
	struct nvkm_therm *therm =
	       container_of(alarm, struct nvkm_therm, alarm);
	nvkm_therm_update(therm, -1);
}

int
nvkm_therm_fan_mode(struct nvkm_therm *therm, int mode)
{
	struct nvkm_subdev *subdev = &therm->subdev;
	struct nvkm_device *device = subdev->device;
	static const char *name[] = {
		"disabled",
		"manual",
		"automatic"
	};

	/* The default PPWR ucode on fermi interferes with fan management */
	if ((mode >= ARRAY_SIZE(name)) ||
	    (mode != NVKM_THERM_CTRL_NONE && device->card_type >= NV_C0 &&
	     !device->pmu))
		return -EINVAL;

	/* do not allow automatic fan management if the thermal sensor is
	 * not available */
	if (mode == NVKM_THERM_CTRL_AUTO &&
	    therm->func->temp_get(therm) < 0)
		return -EINVAL;

	if (therm->mode == mode)
		return 0;

	nvkm_debug(subdev, "fan management: %s\n", name[mode]);
	nvkm_therm_update(therm, mode);
	return 0;
}

int
nvkm_therm_attr_get(struct nvkm_therm *therm, enum nvkm_therm_attr_type type)
{
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
nvkm_therm_attr_set(struct nvkm_therm *therm,
		    enum nvkm_therm_attr_type type, int value)
{
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
		return nvkm_therm_fan_mode(therm, value);
	case NVKM_THERM_ATTR_THRS_FAN_BOOST:
		therm->bios_sensor.thrs_fan_boost.temp = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_FAN_BOOST_HYST:
		therm->bios_sensor.thrs_fan_boost.hysteresis = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK:
		therm->bios_sensor.thrs_down_clock.temp = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_DOWN_CLK_HYST:
		therm->bios_sensor.thrs_down_clock.hysteresis = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL:
		therm->bios_sensor.thrs_critical.temp = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_CRITICAL_HYST:
		therm->bios_sensor.thrs_critical.hysteresis = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN:
		therm->bios_sensor.thrs_shutdown.temp = value;
		therm->func->program_alarms(therm);
		return 0;
	case NVKM_THERM_ATTR_THRS_SHUTDOWN_HYST:
		therm->bios_sensor.thrs_shutdown.hysteresis = value;
		therm->func->program_alarms(therm);
		return 0;
	}

	return -EINVAL;
}

static void
nvkm_therm_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);
	if (therm->func->intr)
		therm->func->intr(therm);
}

static int
nvkm_therm_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);

	if (therm->func->fini)
		therm->func->fini(therm);

	nvkm_therm_fan_fini(therm, suspend);
	nvkm_therm_sensor_fini(therm, suspend);

	if (suspend) {
		therm->suspend = therm->mode;
		therm->mode = NVKM_THERM_CTRL_NONE;
	}

	return 0;
}

static int
nvkm_therm_oneinit(struct nvkm_subdev *subdev)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);
	nvkm_therm_sensor_ctor(therm);
	nvkm_therm_ic_ctor(therm);
	nvkm_therm_fan_ctor(therm);
	nvkm_therm_fan_mode(therm, NVKM_THERM_CTRL_AUTO);
	nvkm_therm_sensor_preinit(therm);
	return 0;
}

static int
nvkm_therm_init(struct nvkm_subdev *subdev)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);

	therm->func->init(therm);

	if (therm->suspend >= 0) {
		/* restore the pwm value only when on manual or auto mode */
		if (therm->suspend > 0)
			nvkm_therm_fan_set(therm, true, therm->fan->percent);

		nvkm_therm_fan_mode(therm, therm->suspend);
	}

	nvkm_therm_sensor_init(therm);
	nvkm_therm_fan_init(therm);
	return 0;
}

static void *
nvkm_therm_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_therm *therm = nvkm_therm(subdev);
	kfree(therm->fan);
	return therm;
}

static const struct nvkm_subdev_func
nvkm_therm = {
	.dtor = nvkm_therm_dtor,
	.oneinit = nvkm_therm_oneinit,
	.init = nvkm_therm_init,
	.fini = nvkm_therm_fini,
	.intr = nvkm_therm_intr,
};

int
nvkm_therm_new_(const struct nvkm_therm_func *func, struct nvkm_device *device,
		int index, struct nvkm_therm **ptherm)
{
	struct nvkm_therm *therm;

	if (!(therm = *ptherm = kzalloc(sizeof(*therm), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_therm, device, index, &therm->subdev);
	therm->func = func;

	nvkm_alarm_init(&therm->alarm, nvkm_therm_alarm);
	spin_lock_init(&therm->lock);
	spin_lock_init(&therm->sensor.alarm_program_lock);

	therm->fan_get = nvkm_therm_fan_user_get;
	therm->fan_set = nvkm_therm_fan_user_set;
	therm->attr_get = nvkm_therm_attr_get;
	therm->attr_set = nvkm_therm_attr_set;
	therm->mode = therm->suspend = -1; /* undefined */
	return 0;
}
