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
 */

#include "priv.h"

struct nvd0_therm_priv {
	struct nouveau_therm_priv base;
};

static int
pwm_info(struct nouveau_therm *therm, int line)
{
	u32 gpio = nv_rd32(therm, 0x00d610 + (line * 0x04));

	switch (gpio & 0x000000c0) {
	case 0x00000000: /* normal mode, possibly pwm forced off by us */
	case 0x00000040: /* nvio special */
		switch (gpio & 0x0000001f) {
		case 0x00: return 2;
		case 0x19: return 1;
		case 0x1c: return 0;
		case 0x1e: return 2;
		default:
			break;
		}
	default:
		break;
	}

	nv_error(therm, "GPIO %d unknown PWM: 0x%08x\n", line, gpio);
	return -ENODEV;
}

static int
nvd0_fan_pwm_ctrl(struct nouveau_therm *therm, int line, bool enable)
{
	u32 data = enable ? 0x00000040 : 0x00000000;
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2)
		nv_mask(therm, 0x00d610 + (line * 0x04), 0x000000c0, data);
	/* nothing to do for indx == 2, it seems hardwired to PTHERM */
	return 0;
}

static int
nvd0_fan_pwm_get(struct nouveau_therm *therm, int line, u32 *divs, u32 *duty)
{
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2) {
		if (nv_rd32(therm, 0x00d610 + (line * 0x04)) & 0x00000040) {
			*divs = nv_rd32(therm, 0x00e114 + (indx * 8));
			*duty = nv_rd32(therm, 0x00e118 + (indx * 8));
			return 0;
		}
	} else if (indx == 2) {
		*divs = nv_rd32(therm, 0x0200d8) & 0x1fff;
		*duty = nv_rd32(therm, 0x0200dc) & 0x1fff;
		return 0;
	}

	return -EINVAL;
}

static int
nvd0_fan_pwm_set(struct nouveau_therm *therm, int line, u32 divs, u32 duty)
{
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return indx;
	else if (indx < 2) {
		nv_wr32(therm, 0x00e114 + (indx * 8), divs);
		nv_wr32(therm, 0x00e118 + (indx * 8), duty | 0x80000000);
	} else if (indx == 2) {
		nv_mask(therm, 0x0200d8, 0x1fff, divs); /* keep the high bits */
		nv_wr32(therm, 0x0200dc, duty | 0x40000000);
	}
	return 0;
}

static int
nvd0_fan_pwm_clock(struct nouveau_therm *therm, int line)
{
	int indx = pwm_info(therm, line);
	if (indx < 0)
		return 0;
	else if (indx < 2)
		return (nv_device(therm)->crystal * 1000) / 20;
	else
		return nv_device(therm)->crystal * 1000 / 10;
}

int
nvd0_therm_init(struct nouveau_object *object)
{
	struct nvd0_therm_priv *priv = (void *)object;
	int ret;

	ret = nouveau_therm_init(&priv->base.base);
	if (ret)
		return ret;

	/* enable fan tach, count revolutions per-second */
	nv_mask(priv, 0x00e720, 0x00000003, 0x00000002);
	if (priv->base.fan->tach.func != DCB_GPIO_UNUSED) {
		nv_mask(priv, 0x00d79c, 0x000000ff, priv->base.fan->tach.line);
		nv_wr32(priv, 0x00e724, nv_device(priv)->crystal * 1000);
		nv_mask(priv, 0x00e720, 0x00000001, 0x00000001);
	}
	nv_mask(priv, 0x00e720, 0x00000002, 0x00000000);

	return 0;
}

static int
nvd0_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nvd0_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv84_sensor_setup(&priv->base.base);

	priv->base.base.pwm_ctrl = nvd0_fan_pwm_ctrl;
	priv->base.base.pwm_get = nvd0_fan_pwm_get;
	priv->base.base.pwm_set = nvd0_fan_pwm_set;
	priv->base.base.pwm_clock = nvd0_fan_pwm_clock;
	priv->base.base.temp_get = nv84_temp_get;
	priv->base.base.fan_sense = nva3_therm_fan_sense;
	priv->base.sensor.program_alarms = nouveau_therm_program_alarms_polling;
	return nouveau_therm_preinit(&priv->base.base);
}

struct nouveau_oclass
nvd0_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0xd0),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvd0_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nvd0_therm_init,
		.fini = nv84_therm_fini,
	},
};
