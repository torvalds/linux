/*
 * Copyright 2011 Red Hat Inc.
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

#include <core/option.h>
#include <core/notify.h>

static int
nvkm_gpio_drive(struct nvkm_gpio *gpio, int idx, int line, int dir, int out)
{
	return gpio->func->drive(gpio, line, dir, out);
}

static int
nvkm_gpio_sense(struct nvkm_gpio *gpio, int idx, int line)
{
	return gpio->func->sense(gpio, line);
}

void
nvkm_gpio_reset(struct nvkm_gpio *gpio, u8 func)
{
	if (gpio->func->reset)
		gpio->func->reset(gpio, func);
}

int
nvkm_gpio_find(struct nvkm_gpio *gpio, int idx, u8 tag, u8 line,
	       struct dcb_gpio_func *func)
{
	struct nvkm_device *device = gpio->subdev.device;
	struct nvkm_bios *bios = device->bios;
	u8  ver, len;
	u16 data;

	if (line == 0xff && tag == 0xff)
		return -EINVAL;

	data = dcb_gpio_match(bios, idx, tag, line, &ver, &len, func);
	if (data)
		return 0;

	/* Apple iMac G4 NV18 */
	if (device->quirk && device->quirk->tv_gpio) {
		if (tag == DCB_GPIO_TVDAC0) {
			*func = (struct dcb_gpio_func) {
				.func = DCB_GPIO_TVDAC0,
				.line = device->quirk->tv_gpio,
				.log[0] = 0,
				.log[1] = 1,
			};
			return 0;
		}
	}

	return -ENOENT;
}

int
nvkm_gpio_set(struct nvkm_gpio *gpio, int idx, u8 tag, u8 line, int state)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nvkm_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		int dir = !!(func.log[state] & 0x02);
		int out = !!(func.log[state] & 0x01);
		ret = nvkm_gpio_drive(gpio, idx, func.line, dir, out);
	}

	return ret;
}

int
nvkm_gpio_get(struct nvkm_gpio *gpio, int idx, u8 tag, u8 line)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nvkm_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		ret = nvkm_gpio_sense(gpio, idx, func.line);
		if (ret >= 0)
			ret = (ret == (func.log[1] & 1));
	}

	return ret;
}

static void
nvkm_gpio_intr_fini(struct nvkm_event *event, int type, int index)
{
	struct nvkm_gpio *gpio = container_of(event, typeof(*gpio), event);
	gpio->func->intr_mask(gpio, type, 1 << index, 0);
}

static void
nvkm_gpio_intr_init(struct nvkm_event *event, int type, int index)
{
	struct nvkm_gpio *gpio = container_of(event, typeof(*gpio), event);
	gpio->func->intr_mask(gpio, type, 1 << index, 1 << index);
}

static int
nvkm_gpio_intr_ctor(struct nvkm_object *object, void *data, u32 size,
		    struct nvkm_notify *notify)
{
	struct nvkm_gpio_ntfy_req *req = data;
	if (!WARN_ON(size != sizeof(*req))) {
		notify->size  = sizeof(struct nvkm_gpio_ntfy_rep);
		notify->types = req->mask;
		notify->index = req->line;
		return 0;
	}
	return -EINVAL;
}

static const struct nvkm_event_func
nvkm_gpio_intr_func = {
	.ctor = nvkm_gpio_intr_ctor,
	.init = nvkm_gpio_intr_init,
	.fini = nvkm_gpio_intr_fini,
};

static void
nvkm_gpio_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_gpio *gpio = nvkm_gpio(subdev);
	u32 hi, lo, i;

	gpio->func->intr_stat(gpio, &hi, &lo);

	for (i = 0; (hi | lo) && i < gpio->func->lines; i++) {
		struct nvkm_gpio_ntfy_rep rep = {
			.mask = (NVKM_GPIO_HI * !!(hi & (1 << i))) |
				(NVKM_GPIO_LO * !!(lo & (1 << i))),
		};
		nvkm_event_send(&gpio->event, rep.mask, i, &rep, sizeof(rep));
	}
}

static int
nvkm_gpio_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_gpio *gpio = nvkm_gpio(subdev);
	u32 mask = (1ULL << gpio->func->lines) - 1;

	gpio->func->intr_mask(gpio, NVKM_GPIO_TOGGLED, mask, 0);
	gpio->func->intr_stat(gpio, &mask, &mask);
	return 0;
}

static const struct dmi_system_id gpio_reset_ids[] = {
	{
		.ident = "Apple Macbook 10,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro10,1"),
		}
	},
	{ }
};

static enum dcb_gpio_func_name power_checks[] = {
	DCB_GPIO_THERM_EXT_POWER_EVENT,
	DCB_GPIO_POWER_ALERT,
	DCB_GPIO_EXT_POWER_LOW,
};

static int
nvkm_gpio_init(struct nvkm_subdev *subdev)
{
	struct nvkm_gpio *gpio = nvkm_gpio(subdev);
	struct dcb_gpio_func func;
	int ret;
	int i;

	if (dmi_check_system(gpio_reset_ids))
		nvkm_gpio_reset(gpio, DCB_GPIO_UNUSED);

	if (nvkm_boolopt(subdev->device->cfgopt, "NvPowerChecks", true)) {
		for (i = 0; i < ARRAY_SIZE(power_checks); ++i) {
			ret = nvkm_gpio_find(gpio, 0, power_checks[i],
					     DCB_GPIO_UNUSED, &func);
			if (ret)
				continue;

			ret = nvkm_gpio_get(gpio, 0, func.func, func.line);
			if (!ret)
				continue;

			nvkm_error(&gpio->subdev,
				   "GPU is missing power, check its power "
				   "cables.  Boot with "
				   "nouveau.config=NvPowerChecks=0 to "
				   "disable.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static void *
nvkm_gpio_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_gpio *gpio = nvkm_gpio(subdev);
	nvkm_event_fini(&gpio->event);
	return gpio;
}

static const struct nvkm_subdev_func
nvkm_gpio = {
	.dtor = nvkm_gpio_dtor,
	.init = nvkm_gpio_init,
	.fini = nvkm_gpio_fini,
	.intr = nvkm_gpio_intr,
};

int
nvkm_gpio_new_(const struct nvkm_gpio_func *func, struct nvkm_device *device,
	       enum nvkm_subdev_type type, int inst, struct nvkm_gpio **pgpio)
{
	struct nvkm_gpio *gpio;

	if (!(gpio = *pgpio = kzalloc(sizeof(*gpio), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_gpio, device, type, inst, &gpio->subdev);
	gpio->func = func;

	return nvkm_event_init(&nvkm_gpio_intr_func, 2, func->lines,
			       &gpio->event);
}
