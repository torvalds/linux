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

struct nv40_therm_priv {
	struct nouveau_therm_priv base;
};

enum nv40_sensor_style { INVALID_STYLE = -1, OLD_STYLE = 0, NEW_STYLE = 1 };

static enum nv40_sensor_style
nv40_sensor_style(struct nouveau_therm *therm)
{
	struct nouveau_device *device = nv_device(therm);

	switch (device->chipset) {
	case 0x43:
	case 0x44:
	case 0x4a:
	case 0x47:
		return OLD_STYLE;

	case 0x46:
	case 0x49:
	case 0x4b:
	case 0x4e:
	case 0x4c:
	case 0x67:
	case 0x68:
	case 0x63:
		return NEW_STYLE;
	default:
		return INVALID_STYLE;
	}
}

static int
nv40_sensor_setup(struct nouveau_therm *therm)
{
	enum nv40_sensor_style style = nv40_sensor_style(therm);

	/* enable ADC readout and disable the ALARM threshold */
	if (style == NEW_STYLE) {
		nv_mask(therm, 0x15b8, 0x80000000, 0);
		nv_wr32(therm, 0x15b0, 0x80003fff);
		mdelay(20); /* wait for the temperature to stabilize */
		return nv_rd32(therm, 0x15b4) & 0x3fff;
	} else if (style == OLD_STYLE) {
		nv_wr32(therm, 0x15b0, 0xff);
		mdelay(20); /* wait for the temperature to stabilize */
		return nv_rd32(therm, 0x15b4) & 0xff;
	} else
		return -ENODEV;
}

static int
nv40_temp_get(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nvbios_therm_sensor *sensor = &priv->bios_sensor;
	enum nv40_sensor_style style = nv40_sensor_style(therm);
	int core_temp;

	if (style == NEW_STYLE) {
		nv_wr32(therm, 0x15b0, 0x80003fff);
		core_temp = nv_rd32(therm, 0x15b4) & 0x3fff;
	} else if (style == OLD_STYLE) {
		nv_wr32(therm, 0x15b0, 0xff);
		core_temp = nv_rd32(therm, 0x15b4) & 0xff;
	} else
		return -ENODEV;

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
nv40_fan_pwm_ctrl(struct nouveau_therm *therm, int line, bool enable)
{
	u32 mask = enable ? 0x80000000 : 0x0000000;
	if      (line == 2) nv_mask(therm, 0x0010f0, 0x80000000, mask);
	else if (line == 9) nv_mask(therm, 0x0015f4, 0x80000000, mask);
	else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}
	return 0;
}

static int
nv40_fan_pwm_get(struct nouveau_therm *therm, int line, u32 *divs, u32 *duty)
{
	if (line == 2) {
		u32 reg = nv_rd32(therm, 0x0010f0);
		if (reg & 0x80000000) {
			*duty = (reg & 0x7fff0000) >> 16;
			*divs = (reg & 0x00007fff);
			return 0;
		}
	} else
	if (line == 9) {
		u32 reg = nv_rd32(therm, 0x0015f4);
		if (reg & 0x80000000) {
			*divs = nv_rd32(therm, 0x0015f8);
			*duty = (reg & 0x7fffffff);
			return 0;
		}
	} else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}

	return -EINVAL;
}

static int
nv40_fan_pwm_set(struct nouveau_therm *therm, int line, u32 divs, u32 duty)
{
	if (line == 2) {
		nv_mask(therm, 0x0010f0, 0x7fff7fff, (duty << 16) | divs);
	} else
	if (line == 9) {
		nv_wr32(therm, 0x0015f8, divs);
		nv_mask(therm, 0x0015f4, 0x7fffffff, duty);
	} else {
		nv_error(therm, "unknown pwm ctrl for gpio %d\n", line);
		return -ENODEV;
	}

	return 0;
}

void
nv40_therm_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_therm *therm = nouveau_therm(subdev);
	uint32_t stat = nv_rd32(therm, 0x1100);

	/* traitement */

	/* ack all IRQs */
	nv_wr32(therm, 0x1100, 0x70000);

	nv_error(therm, "THERM received an IRQ: stat = %x\n", stat);
}

static int
nv40_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nv40_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.pwm_ctrl = nv40_fan_pwm_ctrl;
	priv->base.base.pwm_get = nv40_fan_pwm_get;
	priv->base.base.pwm_set = nv40_fan_pwm_set;
	priv->base.base.temp_get = nv40_temp_get;
	priv->base.sensor.program_alarms = nouveau_therm_program_alarms_polling;
	nv_subdev(priv)->intr = nv40_therm_intr;
	return nouveau_therm_preinit(&priv->base.base);
}

static int
nv40_therm_init(struct nouveau_object *object)
{
	struct nouveau_therm *therm = (void *)object;

	nv40_sensor_setup(therm);

	return _nouveau_therm_init(object);
}

struct nouveau_oclass
nv40_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nv40_therm_init,
		.fini = _nouveau_therm_fini,
	},
};
