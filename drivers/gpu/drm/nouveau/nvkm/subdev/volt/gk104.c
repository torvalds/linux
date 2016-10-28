/*
 * Copyright 2015 Martin Peres
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

#include <subdev/volt.h>
#include <subdev/gpio.h>
#include <subdev/bios.h>
#include <subdev/bios/volt.h>
#include <subdev/fuse.h>

#define gk104_volt(p) container_of((p), struct gk104_volt, base)
struct gk104_volt {
	struct nvkm_volt base;
	struct nvbios_volt bios;
};

int
gk104_volt_get(struct nvkm_volt *base)
{
	struct nvbios_volt *bios = &gk104_volt(base)->bios;
	struct nvkm_device *device = base->subdev.device;
	u32 div, duty;

	div  = nvkm_rd32(device, 0x20340);
	duty = nvkm_rd32(device, 0x20344);

	return bios->base + bios->pwm_range * duty / div;
}

int
gk104_volt_set(struct nvkm_volt *base, u32 uv)
{
	struct nvbios_volt *bios = &gk104_volt(base)->bios;
	struct nvkm_device *device = base->subdev.device;
	u32 div, duty;

	/* the blob uses this crystal frequency, let's use it too. */
	div = 27648000 / bios->pwm_freq;
	duty = DIV_ROUND_UP((uv - bios->base) * div, bios->pwm_range);

	nvkm_wr32(device, 0x20340, div);
	nvkm_wr32(device, 0x20344, 0x80000000 | duty);

	return 0;
}

static int
gk104_volt_speedo_read(struct nvkm_volt *volt)
{
	struct nvkm_device *device = volt->subdev.device;
	struct nvkm_fuse *fuse = device->fuse;
	int ret;

	if (!fuse)
		return -EINVAL;

	nvkm_wr32(device, 0x122634, 0x0);
	ret = nvkm_fuse_read(fuse, 0x3a8);
	nvkm_wr32(device, 0x122634, 0x41);
	return ret;
}

static const struct nvkm_volt_func
gk104_volt_pwm = {
	.oneinit = gf100_volt_oneinit,
	.volt_get = gk104_volt_get,
	.volt_set = gk104_volt_set,
	.speedo_read = gk104_volt_speedo_read,
}, gk104_volt_gpio = {
	.oneinit = gf100_volt_oneinit,
	.vid_get = nvkm_voltgpio_get,
	.vid_set = nvkm_voltgpio_set,
	.speedo_read = gk104_volt_speedo_read,
};

int
gk104_volt_new(struct nvkm_device *device, int index, struct nvkm_volt **pvolt)
{
	const struct nvkm_volt_func *volt_func = &gk104_volt_gpio;
	struct dcb_gpio_func gpio;
	struct nvbios_volt bios;
	struct gk104_volt *volt;
	u8 ver, hdr, cnt, len;
	const char *mode;

	if (!nvbios_volt_parse(device->bios, &ver, &hdr, &cnt, &len, &bios))
		return 0;

	if (!nvkm_gpio_find(device->gpio, 0, DCB_GPIO_VID_PWM, 0xff, &gpio) &&
	    bios.type == NVBIOS_VOLT_PWM) {
		volt_func = &gk104_volt_pwm;
	}

	if (!(volt = kzalloc(sizeof(*volt), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_volt_ctor(volt_func, device, index, &volt->base);
	*pvolt = &volt->base;
	volt->bios = bios;

	/* now that we have a subdev, we can show an error if we found through
	 * the voltage table that we were supposed to use the PWN mode but we
	 * did not find the right GPIO for it.
	 */
	if (bios.type == NVBIOS_VOLT_PWM && volt_func != &gk104_volt_pwm) {
		nvkm_error(&volt->base.subdev,
			   "Type mismatch between the voltage table type and "
			   "the GPIO table. Fallback to GPIO mode.\n");
	}

	if (volt_func == &gk104_volt_gpio) {
		nvkm_voltgpio_init(&volt->base);
		mode = "GPIO";
	} else
		mode = "PWM";

	nvkm_debug(&volt->base.subdev, "Using %s mode\n", mode);

	return 0;
}
