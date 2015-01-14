#ifndef __NVTHERM_PRIV_H__
#define __NVTHERM_PRIV_H__
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
#include <subdev/therm.h>
#include <subdev/bios.h>
#include <subdev/bios/extdev.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/perf.h>
#include <subdev/bios/therm.h>
#include <subdev/timer.h>

struct nvkm_fan {
	struct nvkm_therm *parent;
	const char *type;

	struct nvbios_therm_fan bios;
	struct nvbios_perf_fan perf;

	struct nvkm_alarm alarm;
	spinlock_t lock;
	int percent;

	int (*get)(struct nvkm_therm *);
	int (*set)(struct nvkm_therm *, int percent);

	struct dcb_gpio_func tach;
};

enum nvkm_therm_thrs_direction {
	NVKM_THERM_THRS_FALLING = 0,
	NVKM_THERM_THRS_RISING = 1
};

enum nvkm_therm_thrs_state {
	NVKM_THERM_THRS_LOWER = 0,
	NVKM_THERM_THRS_HIGHER = 1
};

enum nvkm_therm_thrs {
	NVKM_THERM_THRS_FANBOOST = 0,
	NVKM_THERM_THRS_DOWNCLOCK = 1,
	NVKM_THERM_THRS_CRITICAL = 2,
	NVKM_THERM_THRS_SHUTDOWN = 3,
	NVKM_THERM_THRS_NR
};

struct nvkm_therm_priv {
	struct nvkm_therm base;

	/* automatic thermal management */
	struct nvkm_alarm alarm;
	spinlock_t lock;
	struct nvbios_therm_trip_point *last_trip;
	int mode;
	int cstate;
	int suspend;

	/* bios */
	struct nvbios_therm_sensor bios_sensor;

	/* fan priv */
	struct nvkm_fan *fan;

	/* alarms priv */
	struct {
		spinlock_t alarm_program_lock;
		struct nvkm_alarm therm_poll_alarm;
		enum nvkm_therm_thrs_state alarm_state[NVKM_THERM_THRS_NR];
		void (*program_alarms)(struct nvkm_therm *);
	} sensor;

	/* what should be done if the card overheats */
	struct {
		void (*downclock)(struct nvkm_therm *, bool active);
		void (*pause)(struct nvkm_therm *, bool active);
	} emergency;

	/* ic */
	struct i2c_client *ic;
};

int nvkm_therm_fan_mode(struct nvkm_therm *, int mode);
int nvkm_therm_attr_get(struct nvkm_therm *, enum nvkm_therm_attr_type);
int nvkm_therm_attr_set(struct nvkm_therm *, enum nvkm_therm_attr_type, int);

void nvkm_therm_ic_ctor(struct nvkm_therm *);

int nvkm_therm_sensor_ctor(struct nvkm_therm *);

int nvkm_therm_fan_ctor(struct nvkm_therm *);
int nvkm_therm_fan_init(struct nvkm_therm *);
int nvkm_therm_fan_fini(struct nvkm_therm *, bool suspend);
int nvkm_therm_fan_get(struct nvkm_therm *);
int nvkm_therm_fan_set(struct nvkm_therm *, bool now, int percent);
int nvkm_therm_fan_user_get(struct nvkm_therm *);
int nvkm_therm_fan_user_set(struct nvkm_therm *, int percent);

int nvkm_therm_fan_sense(struct nvkm_therm *);

int nvkm_therm_preinit(struct nvkm_therm *);

int  nvkm_therm_sensor_init(struct nvkm_therm *);
int  nvkm_therm_sensor_fini(struct nvkm_therm *, bool suspend);
void nvkm_therm_sensor_preinit(struct nvkm_therm *);
void nvkm_therm_sensor_set_threshold_state(struct nvkm_therm *,
					   enum nvkm_therm_thrs,
					   enum nvkm_therm_thrs_state);
enum nvkm_therm_thrs_state
nvkm_therm_sensor_get_threshold_state(struct nvkm_therm *,
				      enum nvkm_therm_thrs);
void nvkm_therm_sensor_event(struct nvkm_therm *, enum nvkm_therm_thrs,
			     enum nvkm_therm_thrs_direction);
void nvkm_therm_program_alarms_polling(struct nvkm_therm *);

void nv40_therm_intr(struct nvkm_subdev *);
int  nv50_fan_pwm_ctrl(struct nvkm_therm *, int, bool);
int  nv50_fan_pwm_get(struct nvkm_therm *, int, u32 *, u32 *);
int  nv50_fan_pwm_set(struct nvkm_therm *, int, u32, u32);
int  nv50_fan_pwm_clock(struct nvkm_therm *, int);
int  g84_temp_get(struct nvkm_therm *);
void g84_sensor_setup(struct nvkm_therm *);
int  g84_therm_fini(struct nvkm_object *, bool suspend);

int gt215_therm_fan_sense(struct nvkm_therm *);

int gf110_therm_init(struct nvkm_object *);

int nvkm_fanpwm_create(struct nvkm_therm *, struct dcb_gpio_func *);
int nvkm_fantog_create(struct nvkm_therm *, struct dcb_gpio_func *);
int nvkm_fannil_create(struct nvkm_therm *);
#endif
