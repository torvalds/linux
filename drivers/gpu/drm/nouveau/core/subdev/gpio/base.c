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

#include <subdev/gpio.h>
#include <subdev/bios.h>
#include <subdev/bios/gpio.h>

static int
nouveau_gpio_drive(struct nouveau_gpio *gpio,
		   int idx, int line, int dir, int out)
{
	return gpio->drive ? gpio->drive(gpio, line, dir, out) : -ENODEV;
}

static int
nouveau_gpio_sense(struct nouveau_gpio *gpio, int idx, int line)
{
	return gpio->sense ? gpio->sense(gpio, line) : -ENODEV;
}

static int
nouveau_gpio_find(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line,
		  struct dcb_gpio_func *func)
{
	struct nouveau_bios *bios = nouveau_bios(gpio);
	u8  ver, len;
	u16 data;

	if (line == 0xff && tag == 0xff)
		return -EINVAL;

	data = dcb_gpio_match(bios, idx, tag, line, &ver, &len, func);
	if (data)
		return 0;

	/* Apple iMac G4 NV18 */
	if (nv_device_match(nv_object(gpio), 0x0189, 0x10de, 0x0010)) {
		if (tag == DCB_GPIO_TVDAC0) {
			*func = (struct dcb_gpio_func) {
				.func = DCB_GPIO_TVDAC0,
				.line = 4,
				.log[0] = 0,
				.log[1] = 1,
			};
			return 0;
		}
	}

	return -EINVAL;
}

static int
nouveau_gpio_set(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line, int state)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		int dir = !!(func.log[state] & 0x02);
		int out = !!(func.log[state] & 0x01);
		ret = nouveau_gpio_drive(gpio, idx, func.line, dir, out);
	}

	return ret;
}

static int
nouveau_gpio_get(struct nouveau_gpio *gpio, int idx, u8 tag, u8 line)
{
	struct dcb_gpio_func func;
	int ret;

	ret = nouveau_gpio_find(gpio, idx, tag, line, &func);
	if (ret == 0) {
		ret = nouveau_gpio_sense(gpio, idx, func.line);
		if (ret >= 0)
			ret = (ret == (func.log[1] & 1));
	}

	return ret;
}

void
_nouveau_gpio_dtor(struct nouveau_object *object)
{
	struct nouveau_gpio *gpio = (void *)object;
	nouveau_event_destroy(&gpio->events);
	nouveau_subdev_destroy(&gpio->base);
}

int
nouveau_gpio_create_(struct nouveau_object *parent,
		     struct nouveau_object *engine,
		     struct nouveau_oclass *oclass, int lines,
		     int length, void **pobject)
{
	struct nouveau_gpio *gpio;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "GPIO", "gpio",
				     length, pobject);
	gpio = *pobject;
	if (ret)
		return ret;

	ret = nouveau_event_create(lines, &gpio->events);
	if (ret)
		return ret;

	gpio->find = nouveau_gpio_find;
	gpio->set  = nouveau_gpio_set;
	gpio->get  = nouveau_gpio_get;
	return 0;
}

static struct dmi_system_id gpio_reset_ids[] = {
	{
		.ident = "Apple Macbook 10,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro10,1"),
		}
	},
	{ }
};

int
nouveau_gpio_init(struct nouveau_gpio *gpio)
{
	int ret = nouveau_subdev_init(&gpio->base);
	if (ret == 0 && gpio->reset) {
		if (dmi_check_system(gpio_reset_ids))
			gpio->reset(gpio, DCB_GPIO_UNUSED);
	}
	return ret;
}
