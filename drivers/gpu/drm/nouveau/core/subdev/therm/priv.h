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

#include <subdev/bios/extdev.h>
#include <subdev/bios/gpio.h>
#include <subdev/bios/perf.h>
#include <subdev/bios/therm.h>
#include <subdev/timer.h>

struct nouveau_fan {
	struct nouveau_therm *parent;
	const char *type;
	enum nouveau_therm_fan_mode mode;

	struct nvbios_therm_fan bios;
	struct nvbios_perf_fan perf;

	struct nouveau_alarm alarm;
	spinlock_t lock;
	int percent;

	int (*get)(struct nouveau_therm *therm);
	int (*set)(struct nouveau_therm *therm, int percent);

	struct dcb_gpio_func tach;
};

struct nouveau_therm_priv {
	struct nouveau_therm base;

	/* bios */
	struct nvbios_therm_sensor bios_sensor;

	/* fan priv */
	struct nouveau_fan *fan;

	/* ic */
	struct i2c_client *ic;
};

int nouveau_therm_attr_get(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type);
int nouveau_therm_attr_set(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type, int value);

void nouveau_therm_ic_ctor(struct nouveau_therm *therm);

int nouveau_therm_sensor_ctor(struct nouveau_therm *therm);

int nouveau_therm_fan_ctor(struct nouveau_therm *therm);
int nouveau_therm_fan_get(struct nouveau_therm *therm);
int nouveau_therm_fan_set(struct nouveau_therm *therm, bool now, int percent);
int nouveau_therm_fan_user_get(struct nouveau_therm *therm);
int nouveau_therm_fan_user_set(struct nouveau_therm *therm, int percent);
int nouveau_therm_fan_set_mode(struct nouveau_therm *therm,
			   enum nouveau_therm_fan_mode mode);

int nouveau_therm_fan_sense(struct nouveau_therm *therm);

int nouveau_therm_preinit(struct nouveau_therm *);

int nv50_fan_pwm_ctrl(struct nouveau_therm *, int, bool);
int nv50_fan_pwm_get(struct nouveau_therm *, int, u32 *, u32 *);
int nv50_fan_pwm_set(struct nouveau_therm *, int, u32, u32);
int nv50_fan_pwm_clock(struct nouveau_therm *);
int nv50_temp_get(struct nouveau_therm *therm);

int nva3_therm_fan_sense(struct nouveau_therm *);

int nouveau_fanpwm_create(struct nouveau_therm *, struct dcb_gpio_func *);
int nouveau_fantog_create(struct nouveau_therm *, struct dcb_gpio_func *);
int nouveau_fannil_create(struct nouveau_therm *);

#endif
