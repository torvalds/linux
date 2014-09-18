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

#include <subdev/gpio.h>

#include "priv.h"

struct nva3_therm_priv {
	struct nouveau_therm_priv base;
};

int
nva3_therm_fan_sense(struct nouveau_therm *therm)
{
	u32 tach = nv_rd32(therm, 0x00e728) & 0x0000ffff;
	u32 ctrl = nv_rd32(therm, 0x00e720);
	if (ctrl & 0x00000001)
		return tach * 60 / 2;
	return -ENODEV;
}

static int
nva3_therm_init(struct nouveau_object *object)
{
	struct nva3_therm_priv *priv = (void *)object;
	struct dcb_gpio_func *tach = &priv->base.fan->tach;
	int ret;

	ret = nouveau_therm_init(&priv->base.base);
	if (ret)
		return ret;

	/* enable fan tach, count revolutions per-second */
	nv_mask(priv, 0x00e720, 0x00000003, 0x00000002);
	if (tach->func != DCB_GPIO_UNUSED) {
		nv_wr32(priv, 0x00e724, nv_device(priv)->crystal * 1000);
		nv_mask(priv, 0x00e720, 0x001f0000, tach->line << 16);
		nv_mask(priv, 0x00e720, 0x00000001, 0x00000001);
	}
	nv_mask(priv, 0x00e720, 0x00000002, 0x00000000);

	return 0;
}

static int
nva3_therm_ctor(struct nouveau_object *parent,
		struct nouveau_object *engine,
		struct nouveau_oclass *oclass, void *data, u32 size,
		struct nouveau_object **pobject)
{
	struct nva3_therm_priv *priv;
	int ret;

	ret = nouveau_therm_create(parent, engine, oclass, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	priv->base.base.pwm_ctrl = nv50_fan_pwm_ctrl;
	priv->base.base.pwm_get = nv50_fan_pwm_get;
	priv->base.base.pwm_set = nv50_fan_pwm_set;
	priv->base.base.pwm_clock = nv50_fan_pwm_clock;
	priv->base.base.temp_get = nv84_temp_get;
	priv->base.base.fan_sense = nva3_therm_fan_sense;
	priv->base.sensor.program_alarms = nouveau_therm_program_alarms_polling;
	return nouveau_therm_preinit(&priv->base.base);
}

struct nouveau_oclass
nva3_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0xa3),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_therm_ctor,
		.dtor = _nouveau_therm_dtor,
		.init = nva3_therm_init,
		.fini = nv84_therm_fini,
	},
};
