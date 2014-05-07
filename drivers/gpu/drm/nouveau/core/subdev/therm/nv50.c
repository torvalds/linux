/*
 * Copyright 2012 Red Hat Inc.
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
 * Authors: Ben Skeggs
 * 	    Martin Peres
 */

#include "priv.h"

struct nv50_therm_priv {
	struct nouveau_therm_priv base;
};

static int
pwm_info(struct nouveau_therm *therm, int *line, int *ctrl, int *indx)
{
	if (*line == 0x04) {
		*ctrl = 0x00e100;
		*line = 4;
		*indx = 0;
	} else
	if (*line == 0x09) {
		*ctrl = 0x00e100;
		*line = 9;
		*indx = 1;
	} else
	if (*line == 0x10) {
		*ctrl = 0x00e28c;
		*line = 0;
		*indx = 0;
	} else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", *line);
		return -ENODEV;
	}

	return 0;
}

int
nv50_fan_pwm_ctrl(struct nouveau_therm *therm, int line, bool enable)
{
	u32 data = enable ? 0x00000001 : 0x00000000;
	int ctrl, id, ret = pwm_info(therm, &line, &ctrl, &id);
	if (ret == 0)
		nv_mask(therm, ctrl, 0x00010001 << line, data << line);
	return ret;
}

int
nv50_fan_pwm_get(struct nouveau_therm *therm, int line, u32 *divs, u32 *duty)
{
	int ctrl, id, ret = pwm_info(therm, &line, &ctrl, &id);
	if (ret)
		return ret;

	if (nv_rd32(therm, ctrl) & (1 << line)) {
		*divs = nv_rd32(therm, 0x00e114 + (id * 8));
		*duty = nv_rd32(therm, 0x00e118 + (id * 8));
		return 0;
	}

	return -EINVAL;
}

int
nv50_fan_pwm_set(struct nouveau_therm *therm, int line, u32 divs, u32 duty)
{
	int ctrl, id, ret = pwm_info(therm, &line, &ctrl, &id);
	if (ret)
		return ret;

	nv_wr32(therm, 0x00e114 + (id * 8), divs);
	nv_wr32(therm, 0x00e118 + (id * 8), duty | 0x80000000);
	return 0;
}

int
nv50_fan_pwm_clock(struct nouveau_therm *therm, int line)
{
	int chipset = nv_device(therm)->chipset;
	int crystal = nv_device(therm)->crystal;
	int pwm_clock;

	/* determine the PWM source clock */
	if (chipset > 0x50 && chipset < 0x94) {
		u8 pwm_div = nv_rd32(therm, 0x410c);
		if (nv_rd32(therm, 0xc040) & 0x800000) {
			/* Use the HOST clock (100 MHz)
			* Where does this constant(2.4) comes from? */
			pwm_clock = (100000000 >> pwm_div) * 10 / 24;
		} else {
			/* Where does this constant(20) comes from? */
			pwm_clock = (crystal * 1000) >> pwm_div;
			pwm_clock /= 20;
		}
	} else {
		pwm_clock = (crystal * 1000) / 20;
	}

	return pwm_clock;
}

static void
nv50_sensor_setup(struct nouveau_therm *therm)
{
	nv_mask(therm, 0x20010, 0x40000000, 0x0);
	mdelay(20); /* wait for the temperature to stabilize */
}

static int
nv50_temp_get(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nvbios_therm_sensor *sensor = &priv->bios_sensor;
	int core_temp;

	core_temp = nv_rd32(therm, 0x20014) & 0x3fff;

	/* if the slope or the offset is unset, do no use the sensor */
	if (!sensor->slope_div || !sensor->slope_mult ||
	    !sensor->offset_num || !sensor->offset_den)
	    return -ENODEV;

	core_temp = core_temp * sensor->slope_mult / sensor->slope_div;
	core_temp = core_temp + sensor->offset_num / sensor->offset_den;
	core_temp = core_temp + sensor->offset_constant - 8;

	/* reserve negative temperatures for errors */
	if (core_temp < 0)
		core_temp = 0;

	return core_temp;
}

static int
nv50_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv50_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.pwm_ctrl = nv50_fan_pwm_ctrl;
	priv->base.base.pwm_get = nv50_fan_pwm_get;
	priv->base.base.pwm_set = nv50_fan_pwm_set;
	priv->base.base.pwm_clock = nv50_fan_pwm_clock;
	priv->base.base.temp_get = nv50_temp_get;
	priv->base.sensor.program_alarms = nouveau_therm_program_alarms_polling;
	nv_subdev(priv)->intr = nv40_therm_intr;

	return nouveau_therm_preinit(&priv->base.base);
}

static int
nv50_therm_init(struct nouveau_object *object)
{
	struct nouveau_therm *therm = (void *)object;

	nv50_sensor_setup(therm);

	return _nouveau_therm_init(object);
}

struct nouveau_oclass
nv50_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nv50_therm_init,
		.fini = _nouveau_therm_fini,
	},
};
