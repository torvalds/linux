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

#include <core/object.h>
#include <core/device.h>

#include <subdev/bios.h>

static void
nouveau_therm_temp_set_defaults(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->bios_sensor.offset_constant = 0;

	priv->bios_sensor.thrs_fan_boost.temp = 90;
	priv->bios_sensor.thrs_fan_boost.hysteresis = 3;

	priv->bios_sensor.thrs_down_clock.temp = 95;
	priv->bios_sensor.thrs_down_clock.hysteresis = 3;

	priv->bios_sensor.thrs_critical.temp = 105;
	priv->bios_sensor.thrs_critical.hysteresis = 5;

	priv->bios_sensor.thrs_shutdown.temp = 135;
	priv->bios_sensor.thrs_shutdown.hysteresis = 5; /*not that it matters */
}


static void
nouveau_therm_temp_safety_checks(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nvbios_therm_sensor *s = &priv->bios_sensor;

	/* enforce a minimum hysteresis on thresholds */
	s->thrs_fan_boost.hysteresis = max_t(u8, s->thrs_fan_boost.hysteresis, 2);
	s->thrs_down_clock.hysteresis = max_t(u8, s->thrs_down_clock.hysteresis, 2);
	s->thrs_critical.hysteresis = max_t(u8, s->thrs_critical.hysteresis, 2);
	s->thrs_shutdown.hysteresis = max_t(u8, s->thrs_shutdown.hysteresis, 2);
}

/* must be called with alarm_program_lock taken ! */
void nouveau_therm_sensor_set_threshold_state(struct nouveau_therm *therm,
					     enum nouveau_therm_thrs thrs,
					     enum nouveau_therm_thrs_state st)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	priv->sensor.alarm_state[thrs] = st;
}

/* must be called with alarm_program_lock taken ! */
enum nouveau_therm_thrs_state
nouveau_therm_sensor_get_threshold_state(struct nouveau_therm *therm,
					 enum nouveau_therm_thrs thrs)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	return priv->sensor.alarm_state[thrs];
}

static void
nv_poweroff_work(struct work_struct *work)
{
	orderly_poweroff(true);
	kfree(work);
}

void nouveau_therm_sensor_event(struct nouveau_therm *therm,
			        enum nouveau_therm_thrs thrs,
			        enum nouveau_therm_thrs_direction dir)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	bool active;
	const char *thresolds[] = {
		"fanboost", "downclock", "critical", "shutdown"
	};
	int temperature = therm->temp_get(therm);

	if (thrs < 0 || thrs > 3)
		return;

	if (dir == NOUVEAU_THERM_THRS_FALLING)
		nv_info(therm, "temperature (%i C) went below the '%s' threshold\n",
			temperature, thresolds[thrs]);
	else
		nv_info(therm, "temperature (%i C) hit the '%s' threshold\n",
			temperature, thresolds[thrs]);

	active = (dir == NOUVEAU_THERM_THRS_RISING);
	switch (thrs) {
	case NOUVEAU_THERM_THRS_FANBOOST:
		if (active) {
			nouveau_therm_fan_set(therm, true, 100);
			nouveau_therm_fan_mode(therm, NOUVEAU_THERM_CTRL_AUTO);
		}
		break;
	case NOUVEAU_THERM_THRS_DOWNCLOCK:
		if (priv->emergency.downclock)
			priv->emergency.downclock(therm, active);
		break;
	case NOUVEAU_THERM_THRS_CRITICAL:
		if (priv->emergency.pause)
			priv->emergency.pause(therm, active);
		break;
	case NOUVEAU_THERM_THRS_SHUTDOWN:
		if (active) {
			struct work_struct *work;

			work = kmalloc(sizeof(*work), GFP_ATOMIC);
			if (work) {
				INIT_WORK(work, nv_poweroff_work);
				schedule_work(work);
			}
		}
		break;
	case NOUVEAU_THERM_THRS_NR:
		break;
	}

}

/* must be called with alarm_program_lock taken ! */
static void
nouveau_therm_threshold_hyst_polling(struct nouveau_therm *therm,
				   const struct nvbios_therm_threshold *thrs,
				   enum nouveau_therm_thrs thrs_name)
{
	enum nouveau_therm_thrs_direction direction;
	enum nouveau_therm_thrs_state prev_state, new_state;
	int temp = therm->temp_get(therm);

	prev_state = nouveau_therm_sensor_get_threshold_state(therm, thrs_name);

	if (temp >= thrs->temp && prev_state == NOUVEAU_THERM_THRS_LOWER) {
		direction = NOUVEAU_THERM_THRS_RISING;
		new_state = NOUVEAU_THERM_THRS_HIGHER;
	} else if (temp <= thrs->temp - thrs->hysteresis &&
			prev_state == NOUVEAU_THERM_THRS_HIGHER) {
		direction = NOUVEAU_THERM_THRS_FALLING;
		new_state = NOUVEAU_THERM_THRS_LOWER;
	} else
		return; /* nothing to do */

	nouveau_therm_sensor_set_threshold_state(therm, thrs_name, new_state);
	nouveau_therm_sensor_event(therm, thrs_name, direction);
}

static void
alarm_timer_callback(struct nouveau_alarm *alarm)
{
	struct nouveau_therm_priv *priv =
	container_of(alarm, struct nouveau_therm_priv, sensor.therm_poll_alarm);
	struct nvbios_therm_sensor *sensor = &priv->bios_sensor;
	struct nouveau_timer *ptimer = nouveau_timer(priv);
	struct nouveau_therm *therm = &priv->base;
	unsigned long flags;

	spin_lock_irqsave(&priv->sensor.alarm_program_lock, flags);

	nv_debug(therm, "polling the internal temperature\n");

	nouveau_therm_threshold_hyst_polling(therm, &sensor->thrs_fan_boost,
					     NOUVEAU_THERM_THRS_FANBOOST);

	nouveau_therm_threshold_hyst_polling(therm, &sensor->thrs_down_clock,
					     NOUVEAU_THERM_THRS_DOWNCLOCK);

	nouveau_therm_threshold_hyst_polling(therm, &sensor->thrs_critical,
					     NOUVEAU_THERM_THRS_CRITICAL);

	nouveau_therm_threshold_hyst_polling(therm, &sensor->thrs_shutdown,
					     NOUVEAU_THERM_THRS_SHUTDOWN);

	/* schedule the next poll in one second */
	if (therm->temp_get(therm) >= 0 && list_empty(&alarm->head))
		ptimer->alarm(ptimer, 1000 * 1000 * 1000, alarm);

	spin_unlock_irqrestore(&priv->sensor.alarm_program_lock, flags);
}

void
nouveau_therm_program_alarms_polling(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nvbios_therm_sensor *sensor = &priv->bios_sensor;

	nv_debug(therm,
		 "programmed thresholds [ %d(%d), %d(%d), %d(%d), %d(%d) ]\n",
		 sensor->thrs_fan_boost.temp, sensor->thrs_fan_boost.hysteresis,
		 sensor->thrs_down_clock.temp,
		 sensor->thrs_down_clock.hysteresis,
		 sensor->thrs_critical.temp, sensor->thrs_critical.hysteresis,
		 sensor->thrs_shutdown.temp, sensor->thrs_shutdown.hysteresis);

	alarm_timer_callback(&priv->sensor.therm_poll_alarm);
}

int
nouveau_therm_sensor_init(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	priv->sensor.program_alarms(therm);
	return 0;
}

int
nouveau_therm_sensor_fini(struct nouveau_therm *therm, bool suspend)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_timer *ptimer = nouveau_timer(therm);

	if (suspend)
		ptimer->alarm_cancel(ptimer, &priv->sensor.therm_poll_alarm);
	return 0;
}

void
nouveau_therm_sensor_preinit(struct nouveau_therm *therm)
{
	const char *sensor_avail = "yes";

	if (therm->temp_get(therm) < 0)
		sensor_avail = "no";

	nv_info(therm, "internal sensor: %s\n", sensor_avail);
}

int
nouveau_therm_sensor_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_bios *bios = nouveau_bios(therm);

	nouveau_alarm_init(&priv->sensor.therm_poll_alarm, alarm_timer_callback);

	nouveau_therm_temp_set_defaults(therm);
	if (nvbios_therm_sensor_parse(bios, NVBIOS_THERM_DOMAIN_CORE,
				      &priv->bios_sensor))
		nv_error(therm, "nvbios_therm_sensor_parse failed\n");
	nouveau_therm_temp_safety_checks(therm);

	return 0;
}
