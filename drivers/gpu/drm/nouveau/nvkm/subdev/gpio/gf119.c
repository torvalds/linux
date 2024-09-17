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

void
gf119_gpio_reset(struct nvkm_gpio *gpio, u8 match)
{
	struct nvkm_device *device = gpio->subdev.device;
	struct nvkm_bios *bios = device->bios;
	u8 ver, len;
	u16 entry;
	int ent = -1;

	while ((entry = dcb_gpio_entry(bios, 0, ++ent, &ver, &len))) {
		u32 data = nvbios_rd32(bios, entry);
		u8  line =   (data & 0x0000003f);
		u8  defs = !!(data & 0x00000080);
		u8  func =   (data & 0x0000ff00) >> 8;
		u8  unk0 =   (data & 0x00ff0000) >> 16;
		u8  unk1 =   (data & 0x1f000000) >> 24;

		if ( func  == DCB_GPIO_UNUSED ||
		    (match != DCB_GPIO_UNUSED && match != func))
			continue;

		nvkm_gpio_set(gpio, 0, func, line, defs);

		nvkm_mask(device, 0x00d610 + (line * 4), 0xff, unk0);
		if (unk1--)
			nvkm_mask(device, 0x00d740 + (unk1 * 4), 0xff, line);
	}
}

int
gf119_gpio_drive(struct nvkm_gpio *gpio, int line, int dir, int out)
{
	struct nvkm_device *device = gpio->subdev.device;
	u32 data = ((dir ^ 1) << 13) | (out << 12);
	nvkm_mask(device, 0x00d610 + (line * 4), 0x00003000, data);
	nvkm_mask(device, 0x00d604, 0x00000001, 0x00000001); /* update? */
	return 0;
}

int
gf119_gpio_sense(struct nvkm_gpio *gpio, int line)
{
	struct nvkm_device *device = gpio->subdev.device;
	return !!(nvkm_rd32(device, 0x00d610 + (line * 4)) & 0x00004000);
}

static const struct nvkm_gpio_func
gf119_gpio = {
	.lines = 32,
	.intr_stat = g94_gpio_intr_stat,
	.intr_mask = g94_gpio_intr_mask,
	.drive = gf119_gpio_drive,
	.sense = gf119_gpio_sense,
	.reset = gf119_gpio_reset,
};

int
gf119_gpio_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_gpio **pgpio)
{
	return nvkm_gpio_new_(&gf119_gpio, device, type, inst, pgpio);
}
