#ifndef __NVTHERM_PRIV_H__
#define __NVTHERM_PRIV_H__
#define nvkm_therm(p) container_of((p), struct nvkm_therm, subdev)
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

int nvkm_therm_new_(const struct nvkm_therm_func *, struct nvkm_device *,
		    int index, struct nvkm_therm **);

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

struct nvkm_therm_func {
	void (*init)(struct nvkm_therm *);
	void (*fini)(struct nvkm_therm *);
	void (*intr)(struct nvkm_therm *);

	int (*pwm_ctrl)(struct nvkm_therm *, int line, bool);
	int (*pwm_get)(struct nvkm_therm *, int line, u32 *, u32 *);
	int (*pwm_set)(struct nvkm_therm *, int line, u32, u32);
	int (*pwm_clock)(struct nvkm_therm *, int line);

	int (*temp_get)(struct nvkm_therm *);

	int (*fan_sense)(struct nvkm_therm *);

	void (*program_alarms)(struct nvkm_therm *);
};

void nv40_therm_intr(struct nvkm_therm *);

int  nv50_fan_pwm_ctrl(struct nvkm_therm *, int, bool);
int  nv50_fan_pwm_get(struct nvkm_therm *, int, u32 *, u32 *);
int  nv50_fan_pwm_set(struct nvkm_therm *, int, u32, u32);
int  nv50_fan_pwm_clock(struct nvkm_therm *, int);

int  g84_temp_get(struct nvkm_therm *);
void g84_sensor_setup(struct nvkm_therm *);
void g84_therm_fini(struct nvkm_therm *);

int gt215_therm_fan_sense(struct nvkm_therm *);

void g84_therm_init(struct nvkm_therm *);
void gf119_therm_init(struct nvkm_therm *);

int nvkm_fanpwm_create(struct nvkm_therm *, struct dcb_gpio_func *);
int nvkm_fantog_create(struct nvkm_therm *, struct dcb_gpio_func *);
int nvkm_fannil_create(struct nvkm_therm *);
#endif
