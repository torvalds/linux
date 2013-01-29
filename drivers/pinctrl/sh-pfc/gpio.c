/*
 * SuperH Pin Function Controller GPIO driver.
 *
 * Copyright (C) 2008 Magnus Damm
 * Copyright (C) 2009 - 2012 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME " gpio: " fmt

#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pinctrl/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include "core.h"

struct sh_pfc_chip {
	struct sh_pfc		*pfc;
	struct gpio_chip	gpio_chip;
};

static struct sh_pfc_chip *gpio_to_pfc_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct sh_pfc_chip, gpio_chip);
}

static struct sh_pfc *gpio_to_pfc(struct gpio_chip *gc)
{
	return gpio_to_pfc_chip(gc)->pfc;
}

static int sh_gpio_request(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_request_gpio(offset);
}

static void sh_gpio_free(struct gpio_chip *gc, unsigned offset)
{
	pinctrl_free_gpio(offset);
}

static void sh_gpio_set_value(struct sh_pfc *pfc, unsigned gpio, int value)
{
	struct pinmux_data_reg *dr = NULL;
	int bit = 0;

	if (sh_pfc_get_data_reg(pfc, gpio, &dr, &bit) != 0)
		BUG();
	else
		sh_pfc_write_bit(dr, bit, value);
}

static int sh_gpio_get_value(struct sh_pfc *pfc, unsigned gpio)
{
	struct pinmux_data_reg *dr = NULL;
	int bit = 0;

	if (sh_pfc_get_data_reg(pfc, gpio, &dr, &bit) != 0)
		return -EINVAL;

	return sh_pfc_read_bit(dr, bit);
}

static int sh_gpio_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_gpio_direction_input(offset);
}

static int sh_gpio_direction_output(struct gpio_chip *gc, unsigned offset,
				    int value)
{
	sh_gpio_set_value(gpio_to_pfc(gc), offset, value);

	return pinctrl_gpio_direction_output(offset);
}

static int sh_gpio_get(struct gpio_chip *gc, unsigned offset)
{
	return sh_gpio_get_value(gpio_to_pfc(gc), offset);
}

static void sh_gpio_set(struct gpio_chip *gc, unsigned offset, int value)
{
	sh_gpio_set_value(gpio_to_pfc(gc), offset, value);
}

static int sh_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	pinmux_enum_t enum_id;
	pinmux_enum_t *enum_ids;
	int i, k, pos;

	pos = 0;
	enum_id = 0;
	while (1) {
		pos = sh_pfc_gpio_to_enum(pfc, offset, pos, &enum_id);
		if (pos <= 0 || !enum_id)
			break;

		for (i = 0; i < pfc->info->gpio_irq_size; i++) {
			enum_ids = pfc->info->gpio_irq[i].enum_ids;
			for (k = 0; enum_ids[k]; k++) {
				if (enum_ids[k] == enum_id)
					return pfc->info->gpio_irq[i].irq;
			}
		}
	}

	return -ENOSYS;
}

static void sh_pfc_gpio_setup(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	struct gpio_chip *gc = &chip->gpio_chip;

	gc->request = sh_gpio_request;
	gc->free = sh_gpio_free;
	gc->direction_input = sh_gpio_direction_input;
	gc->get = sh_gpio_get;
	gc->direction_output = sh_gpio_direction_output;
	gc->set = sh_gpio_set;
	gc->to_irq = sh_gpio_to_irq;

	WARN_ON(pfc->info->first_gpio != 0); /* needs testing */

	gc->label = pfc->info->name;
	gc->owner = THIS_MODULE;
	gc->base = pfc->info->first_gpio;
	gc->ngpio = (pfc->info->last_gpio - pfc->info->first_gpio) + 1;
}

int sh_pfc_register_gpiochip(struct sh_pfc *pfc)
{
	struct sh_pfc_chip *chip;
	int ret;

	chip = devm_kzalloc(pfc->dev, sizeof(*chip), GFP_KERNEL);
	if (unlikely(!chip))
		return -ENOMEM;

	chip->pfc = pfc;

	sh_pfc_gpio_setup(chip);

	ret = gpiochip_add(&chip->gpio_chip);
	if (unlikely(ret < 0))
		return ret;

	pfc->gpio = chip;

	pr_info("%s handling gpio %d -> %d\n",
		pfc->info->name, pfc->info->first_gpio,
		pfc->info->last_gpio);

	return 0;
}

int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc)
{
	struct sh_pfc_chip *chip = pfc->gpio;
	int ret;

	ret = gpiochip_remove(&chip->gpio_chip);
	if (unlikely(ret < 0))
		return ret;

	pfc->gpio = NULL;
	return 0;
}
