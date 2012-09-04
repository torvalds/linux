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
#include <subdev/bios/perf.h>
#include <subdev/bios/therm.h>

struct nouveau_therm_priv {
	struct nouveau_therm base;

	/* bios */
	struct nvbios_therm_sensor bios_sensor;
	struct nvbios_therm_fan bios_fan;
	struct nvbios_perf_fan bios_perf_fan;

	/* fan priv */
	struct {
		enum nouveau_therm_fan_mode mode;
		int percent;

		int (*pwm_get)(struct nouveau_therm *, int line, u32*, u32*);
		int (*pwm_set)(struct nouveau_therm *, int line, u32, u32);
		int (*pwm_clock)(struct nouveau_therm *);
	} fan;

	/* ic */
	struct i2c_client *ic;
};

int nouveau_therm_init(struct nouveau_object *object);
int nouveau_therm_fini(struct nouveau_object *object, bool suspend);
int nouveau_therm_attr_get(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type);
int nouveau_therm_attr_set(struct nouveau_therm *therm,
		       enum nouveau_therm_attr_type type, int value);

void nouveau_therm_ic_ctor(struct nouveau_therm *therm);

int nouveau_therm_sensor_ctor(struct nouveau_therm *therm);

int nouveau_therm_fan_ctor(struct nouveau_therm *therm);
int nouveau_therm_fan_get(struct nouveau_therm *therm);
int nouveau_therm_fan_set(struct nouveau_therm *therm, int percent);
int nouveau_therm_fan_user_get(struct nouveau_therm *therm);
int nouveau_therm_fan_user_set(struct nouveau_therm *therm, int percent);
int nouveau_therm_fan_set_mode(struct nouveau_therm *therm,
			   enum nouveau_therm_fan_mode mode);


int nouveau_therm_fan_sense(struct nouveau_therm *therm);
