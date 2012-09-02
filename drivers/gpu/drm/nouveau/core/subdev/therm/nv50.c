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

	nv_mask(therm, ctrl, 0x00010001 << line, 0x00000001 << line);
	nv_wr32(therm, 0x00e114 + (id * 8), divs);
	nv_wr32(therm, 0x00e118 + (id * 8), duty | 0x80000000);
	return 0;
}

int
nv50_temp_get(struct nouveau_therm *therm)
{
	return nv_rd32(therm, 0x20400);
}

static int
nv50_therm_ctor(struct nouveau_object *parent,
		   struct nouveau_object *engine,
		   struct nouveau_oclass *oclass, void *data, u32 size,
		   struct nouveau_object **pobject)
{
	struct nouveau_therm_priv *priv;
	struct nouveau_therm *therm;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	therm = (void *) priv;
	if (ret)
		return ret;

	nouveau_therm_ic_ctor(therm);
	nouveau_therm_sensor_ctor(therm);
	nouveau_therm_fan_ctor(therm);

	priv->fan.pwm_get = nv50_fan_pwm_get;
	priv->fan.pwm_set = nv50_fan_pwm_set;

	therm->temp_get = nv50_temp_get;
	therm->fan_get = nouveau_therm_fan_get;
	therm->fan_set = nouveau_therm_fan_set;
	therm->fan_sense = nouveau_therm_fan_sense;
	therm->attr_get = nouveau_therm_attr_get;
	therm->attr_set = nouveau_therm_attr_set;

	return 0;
}

struct nouveau_oclass
nv50_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x50),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv50_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nouveau_therm_init,
		.fini = nouveau_therm_fini,
	},
};
