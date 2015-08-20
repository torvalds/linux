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

static int
gm107_fan_pwm_ctrl(struct nvkm_therm *therm, int line, bool enable)
{
	/* nothing to do, it seems hardwired */
	return 0;
}

static int
gm107_fan_pwm_get(struct nvkm_therm *therm, int line, u32 *divs, u32 *duty)
{
	struct nvkm_device *device = therm->subdev.device;
	*divs = nvkm_rd32(device, 0x10eb20) & 0x1fff;
	*duty = nvkm_rd32(device, 0x10eb24) & 0x1fff;
	return 0;
}

static int
gm107_fan_pwm_set(struct nvkm_therm *therm, int line, u32 divs, u32 duty)
{
	struct nvkm_device *device = therm->subdev.device;
	nvkm_mask(device, 0x10eb10, 0x1fff, divs); /* keep the high bits */
	nvkm_wr32(device, 0x10eb14, duty | 0x80000000);
	return 0;
}

static int
gm107_fan_pwm_clock(struct nvkm_therm *therm, int line)
{
	return therm->subdev.device->crystal * 1000;
}

static int
gm107_therm_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		 struct nvkm_oclass *oclass, void *data, u32 size,
		 struct nvkm_object **pobject)
{
	struct nvkm_therm_priv *therm;
	int ret;

	ret = nvkm_therm_create(parent, engine, oclass, &therm);
	*pobject = nv_object(therm);
	if (ret)
		return ret;

	therm->base.pwm_ctrl = gm107_fan_pwm_ctrl;
	therm->base.pwm_get = gm107_fan_pwm_get;
	therm->base.pwm_set = gm107_fan_pwm_set;
	therm->base.pwm_clock = gm107_fan_pwm_clock;
	therm->base.temp_get = g84_temp_get;
	therm->base.fan_sense = gt215_therm_fan_sense;
	therm->sensor.program_alarms = nvkm_therm_program_alarms_polling;
	return nvkm_therm_preinit(&therm->base);
}

struct nvkm_oclass
gm107_therm_oclass = {
	.handle = NV_SUBDEV(THERM, 0x117),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm107_therm_ctor,
		.dtor = _nvkm_therm_dtor,
		.init = gf110_therm_init,
		.fini = g84_therm_fini,
	},
};
