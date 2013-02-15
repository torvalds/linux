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

/* -----------------------------------------------------------------------------
 * Pin GPIOs
 */

static int gpio_pin_request(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	struct sh_pfc_pin *pin = sh_pfc_get_pin(pfc, offset);

	if (pin == NULL || pin->enum_id == 0)
		return -EINVAL;

	return pinctrl_request_gpio(offset);
}

static void gpio_pin_free(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_free_gpio(offset);
}

static void gpio_pin_set_value(struct sh_pfc *pfc, unsigned offset, int value)
{
	struct pinmux_data_reg *dr;
	int bit;

	sh_pfc_get_data_reg(pfc, offset, &dr, &bit);
	sh_pfc_write_bit(dr, bit, value);
}

static int gpio_pin_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_gpio_direction_input(offset);
}

static int gpio_pin_direction_output(struct gpio_chip *gc, unsigned offset,
				    int value)
{
	gpio_pin_set_value(gpio_to_pfc(gc), offset, value);

	return pinctrl_gpio_direction_output(offset);
}

static int gpio_pin_get(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	struct pinmux_data_reg *dr;
	int bit;

	sh_pfc_get_data_reg(pfc, offset, &dr, &bit);
	return sh_pfc_read_bit(dr, bit);
}

static void gpio_pin_set(struct gpio_chip *gc, unsigned offset, int value)
{
	gpio_pin_set_value(gpio_to_pfc(gc), offset, value);
}

static int gpio_pin_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	int i, k;

	for (i = 0; i < pfc->info->gpio_irq_size; i++) {
		unsigned short *gpios = pfc->info->gpio_irq[i].gpios;

		for (k = 0; gpios[k]; k++) {
			if (gpios[k] == offset)
				return pfc->info->gpio_irq[i].irq;
		}
	}

	return -ENOSYS;
}

static void gpio_pin_setup(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	struct gpio_chip *gc = &chip->gpio_chip;

	gc->request = gpio_pin_request;
	gc->free = gpio_pin_free;
	gc->direction_input = gpio_pin_direction_input;
	gc->get = gpio_pin_get;
	gc->direction_output = gpio_pin_direction_output;
	gc->set = gpio_pin_set;
	gc->to_irq = gpio_pin_to_irq;

	gc->label = pfc->info->name;
	gc->dev = pfc->dev;
	gc->owner = THIS_MODULE;
	gc->base = 0;
	gc->ngpio = pfc->nr_pins;
}

/* -----------------------------------------------------------------------------
 * Function GPIOs
 */

static int gpio_function_request(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	unsigned int mark = pfc->info->func_gpios[offset].enum_id;
	unsigned long flags;
	int ret = -EINVAL;

	pr_notice_once("Use of GPIO API for function requests is deprecated, convert to pinctrl\n");

	if (mark == 0)
		return ret;

	spin_lock_irqsave(&pfc->lock, flags);

	if (sh_pfc_config_mux(pfc, mark, PINMUX_TYPE_FUNCTION, GPIO_CFG_DRYRUN))
		goto done;

	if (sh_pfc_config_mux(pfc, mark, PINMUX_TYPE_FUNCTION, GPIO_CFG_REQ))
		goto done;

	ret = 0;

done:
	spin_unlock_irqrestore(&pfc->lock, flags);
	return ret;
}

static void gpio_function_free(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc *pfc = gpio_to_pfc(gc);
	unsigned int mark = pfc->info->func_gpios[offset].enum_id;
	unsigned long flags;

	spin_lock_irqsave(&pfc->lock, flags);

	sh_pfc_config_mux(pfc, mark, PINMUX_TYPE_FUNCTION, GPIO_CFG_FREE);

	spin_unlock_irqrestore(&pfc->lock, flags);
}

static void gpio_function_setup(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	struct gpio_chip *gc = &chip->gpio_chip;

	gc->request = gpio_function_request;
	gc->free = gpio_function_free;

	gc->label = pfc->info->name;
	gc->owner = THIS_MODULE;
	gc->base = pfc->nr_pins;
	gc->ngpio = pfc->info->nr_func_gpios;
}

/* -----------------------------------------------------------------------------
 * Register/unregister
 */

static struct sh_pfc_chip *
sh_pfc_add_gpiochip(struct sh_pfc *pfc, void(*setup)(struct sh_pfc_chip *))
{
	struct sh_pfc_chip *chip;
	int ret;

	chip = devm_kzalloc(pfc->dev, sizeof(*chip), GFP_KERNEL);
	if (unlikely(!chip))
		return ERR_PTR(-ENOMEM);

	chip->pfc = pfc;

	setup(chip);

	ret = gpiochip_add(&chip->gpio_chip);
	if (unlikely(ret < 0))
		return ERR_PTR(ret);

	pr_info("%s handling gpio %u -> %u\n",
		chip->gpio_chip.label, chip->gpio_chip.base,
		chip->gpio_chip.base + chip->gpio_chip.ngpio - 1);

	return chip;
}

int sh_pfc_register_gpiochip(struct sh_pfc *pfc)
{
	const struct pinmux_range *ranges;
	struct pinmux_range def_range;
	struct sh_pfc_chip *chip;
	unsigned int nr_ranges;
	unsigned int i;
	int ret;

	/* Register the real GPIOs chip. */
	chip = sh_pfc_add_gpiochip(pfc, gpio_pin_setup);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	pfc->gpio = chip;

	/* Register the GPIO to pin mappings. */
	if (pfc->info->ranges == NULL) {
		def_range.begin = 0;
		def_range.end = pfc->info->nr_pins - 1;
		ranges = &def_range;
		nr_ranges = 1;
	} else {
		ranges = pfc->info->ranges;
		nr_ranges = pfc->info->nr_ranges;
	}

	for (i = 0; i < nr_ranges; ++i) {
		const struct pinmux_range *range = &ranges[i];

		ret = gpiochip_add_pin_range(&chip->gpio_chip,
					     dev_name(pfc->dev),
					     range->begin, range->begin,
					     range->end - range->begin + 1);
		if (ret < 0)
			return ret;
	}

	/* Register the function GPIOs chip. */
	chip = sh_pfc_add_gpiochip(pfc, gpio_function_setup);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	pfc->func = chip;

	return 0;
}

int sh_pfc_unregister_gpiochip(struct sh_pfc *pfc)
{
	int err;
	int ret;

	ret = gpiochip_remove(&pfc->gpio->gpio_chip);
	err = gpiochip_remove(&pfc->func->gpio_chip);

	return ret < 0 ? ret : err;
}
