/*
 * Copyright 2013 Red Hat Inc.
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
#include <subdev/volt.h>
#include <subdev/bios.h>
#include <subdev/bios/gpio.h>
#include <subdev/gpio.h>
#include "priv.h"

static const u8 tags[] = {
	DCB_GPIO_VID0, DCB_GPIO_VID1, DCB_GPIO_VID2, DCB_GPIO_VID3,
	DCB_GPIO_VID4, DCB_GPIO_VID5, DCB_GPIO_VID6, DCB_GPIO_VID7,
};

int
nvkm_voltgpio_get(struct nvkm_volt *volt)
{
	struct nvkm_gpio *gpio = volt->subdev.device->gpio;
	u8 vid = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(tags); i++) {
		if (volt->vid_mask & (1 << i)) {
			int ret = nvkm_gpio_get(gpio, 0, tags[i], 0xff);
			if (ret < 0)
				return ret;
			vid |= ret << i;
		}
	}

	return vid;
}

int
nvkm_voltgpio_set(struct nvkm_volt *volt, u8 vid)
{
	struct nvkm_gpio *gpio = volt->subdev.device->gpio;
	int i;

	for (i = 0; i < ARRAY_SIZE(tags); i++, vid >>= 1) {
		if (volt->vid_mask & (1 << i)) {
			int ret = nvkm_gpio_set(gpio, 0, tags[i], 0xff, vid & 1);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

int
nvkm_voltgpio_init(struct nvkm_volt *volt)
{
	struct nvkm_subdev *subdev = &volt->subdev;
	struct nvkm_gpio *gpio = subdev->device->gpio;
	struct dcb_gpio_func func;
	int i;

	/* check we have gpio function info for each vid bit.  on some
	 * boards (ie. nvs295) the vid mask has more bits than there
	 * are valid gpio functions... from traces, nvidia appear to
	 * just touch the existing ones, so let's mask off the invalid
	 * bits and continue with life
	 */
	for (i = 0; i < ARRAY_SIZE(tags); i++) {
		if (volt->vid_mask & (1 << i)) {
			int ret = nvkm_gpio_find(gpio, 0, tags[i], 0xff, &func);
			if (ret) {
				if (ret != -ENOENT)
					return ret;
				nvkm_debug(subdev, "VID bit %d has no GPIO\n", i);
				volt->vid_mask &= ~(1 << i);
			}
		}
	}

	return 0;
}
