/*
 * Copyright 2014 Martin Peres
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

struct gm107_therm_priv {
	struct nouveau_therm_priv base;
};

static int
gm107_fan_pwm_ctrl(struct nouveau_therm *therm, int line, bool enable)
{
	/* nothing to do, it seems hardwired */
	return 0;
}

static int
gm107_fan_pwm_get(struct nouveau_therm *therm, int line, u32 *divs, u32 *duty)
{
	*divs = nv_rd32(therm, 0x10eb20) & 0x1fff;
	*duty = nv_rd32(therm, 0x10eb24) & 0x1fff;
	return 0;
}

static int
gm107_fan_pwm_set(struct nouveau_therm *therm, int line, u32 divs, u32 duty)
{
	nv_mask(therm, 0x10eb10, 0x1fff, divs); /* keep the high bits */
	nv_wr32(therm, 0x10eb14, duty | 0x80000000);
	return 0;
}

static int
gm107_fan_pwm_clock(struct nouveau_therm *therm, int line)
{
	return nv_device(therm)->crystal * 1000;
}

static int
gm107_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct gm107_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.pwm_ctrl = gm107_fan_pwm_ctrl;
	priv->base.base.pwm_get = gm107_fan_pwm_get;
	priv->base.base.pwm_set = gm107_fan_pwm_set;
	priv->base.base.pwm_clock = gm107_fan_pwm_clock;
	priv->base.base.temp_get = nv84_temp_get;
	priv->base.base.fan_sense = nva3_therm_fan_sense;
	priv->base.sensor.program_alarms = nouveau_therm_program_alarms_polling;
	return nouveau_therm_preinit(&priv->base.base);
}

struct nouveau_oclass
gm107_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x117),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = gm107_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nvd0_therm_init,
		.fini = nv84_therm_fini,
	},
};
