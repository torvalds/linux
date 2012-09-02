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

#include "priv.h"

#include <core/object.h>
#include <core/device.h>

#include <subdev/bios.h>

static void
nouveau_therm_temp_set_defaults(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	priv->bios_sensor.slope_mult = 1;
	priv->bios_sensor.slope_div = 1;
	priv->bios_sensor.offset_num = 0;
	priv->bios_sensor.offset_den = 1;
	priv->bios_sensor.offset_constant = 0;

	priv->bios_sensor.thrs_fan_boost.temp = 90;
	priv->bios_sensor.thrs_fan_boost.hysteresis = 3;

	priv->bios_sensor.thrs_down_clock.temp = 95;
	priv->bios_sensor.thrs_down_clock.hysteresis = 3;

	priv->bios_sensor.thrs_critical.temp = 105;
	priv->bios_sensor.thrs_critical.hysteresis = 5;

	priv->bios_sensor.thrs_shutdown.temp = 135;
	priv->bios_sensor.thrs_shutdown.hysteresis = 5; /*not that it matters */
}


static void
nouveau_therm_temp_safety_checks(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;

	if (!priv->bios_sensor.slope_div)
		priv->bios_sensor.slope_div = 1;
	if (!priv->bios_sensor.offset_den)
		priv->bios_sensor.offset_den = 1;
}

int
nouveau_therm_sensor_ctor(struct nouveau_therm *therm)
{
	struct nouveau_therm_priv *priv = (void *)therm;
	struct nouveau_bios *bios = nouveau_bios(therm);

	nouveau_therm_temp_set_defaults(therm);
	if (nvbios_therm_sensor_parse(bios, NVBIOS_THERM_DOMAIN_CORE,
				      &priv->bios_sensor))
		nv_error(therm, "nvbios_therm_sensor_parse failed\n");
	nouveau_therm_temp_safety_checks(therm);

	return 0;
}
