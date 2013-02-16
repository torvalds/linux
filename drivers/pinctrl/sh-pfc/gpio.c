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

	struct sh_pfc_window	*mem;
};

static struct sh_pfc_chip *gpio_to_pfc_chip(struct gpio_chip *gc)
{
	return container_of(gc, struct sh_pfc_chip, gpio_chip);
}

static struct sh_pfc *gpio_to_pfc(struct gpio_chip *gc)
{
	return gpio_to_pfc_chip(gc)->pfc;
}

static void gpio_get_data_reg(struct sh_pfc *pfc, unsigned int gpio,
			      struct pinmux_data_reg **dr, unsigned int *bit)
{
	struct sh_pfc_pin *gpiop = sh_pfc_get_pin(pfc, gpio);

	*dr = pfc->info->data_regs
	    + ((gpiop->flags & PINMUX_FLAG_DREG) >> PINMUX_FLAG_DREG_SHIFT);
	*bit = (gpiop->flags & PINMUX_FLAG_DBIT) >> PINMUX_FLAG_DBIT_SHIFT;
}

static unsigned long gpio_read_data_reg(struct sh_pfc_chip *chip,
					const struct pinmux_data_reg *dreg)
{
	void __iomem *mem = dreg->reg - chip->mem->phys + chip->mem->virt;

	return sh_pfc_read_raw_reg(mem, dreg->reg_width);
}

static void gpio_write_data_reg(struct sh_pfc_chip *chip,
				const struct pinmux_data_reg *dreg,
				unsigned long value)
{
	void __iomem *mem = dreg->reg - chip->mem->phys + chip->mem->virt;

	sh_pfc_write_raw_reg(mem, dreg->reg_width, value);
}

static void gpio_setup_data_reg(struct sh_pfc *pfc, unsigned gpio)
{
	struct sh_pfc_pin *gpiop = &pfc->info->pins[gpio];
	const struct pinmux_data_reg *dreg;
	unsigned int bit;
	unsigned int i;

	for (i = 0, dreg = pfc->info->data_regs; dreg->reg; ++i, ++dreg) {
		for (bit = 0; bit < dreg->reg_width; bit++) {
			if (dreg->enum_ids[bit] == gpiop->enum_id) {
				gpiop->flags &= ~PINMUX_FLAG_DREG;
				gpiop->flags |= i << PINMUX_FLAG_DREG_SHIFT;
				gpiop->flags &= ~PINMUX_FLAG_DBIT;
				gpiop->flags |= bit << PINMUX_FLAG_DBIT_SHIFT;
				return;
			}
		}
	}

	BUG();
}

static int gpio_setup_data_regs(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	unsigned long addr = pfc->info->data_regs[0].reg;
	struct pinmux_data_reg *dreg;
	unsigned int i;

	/* Find the window that contain the GPIO registers. */
	for (i = 0; i < pfc->num_windows; ++i) {
		struct sh_pfc_window *window = &pfc->window[i];

		if (addr >= window->phys && addr < window->phys + window->size)
			break;
	}

	if (i == pfc->num_windows)
		return -EINVAL;

	/* GPIO data registers must be in the first memory resource. */
	chip->mem = &pfc->window[i];

	for (dreg = pfc->info->data_regs; dreg->reg; ++dreg)
		dreg->reg_shadow = gpio_read_data_reg(chip, dreg);

	for (i = 0; i < pfc->info->nr_pins; i++) {
		if (pfc->info->pins[i].enum_id == 0)
			continue;

		gpio_setup_data_reg(pfc, i);
	}

	return 0;
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

static void gpio_pin_set_value(struct sh_pfc_chip *chip, unsigned offset,
			       int value)
{
	struct pinmux_data_reg *dreg;
	unsigned long pos;
	unsigned int bit;

	gpio_get_data_reg(chip->pfc, offset, &dreg, &bit);

	pos = dreg->reg_width - (bit + 1);

	if (value)
		set_bit(pos, &dreg->reg_shadow);
	else
		clear_bit(pos, &dreg->reg_shadow);

	gpio_write_data_reg(chip, dreg, dreg->reg_shadow);
}

static int gpio_pin_direction_input(struct gpio_chip *gc, unsigned offset)
{
	return pinctrl_gpio_direction_input(offset);
}

static int gpio_pin_direction_output(struct gpio_chip *gc, unsigned offset,
				    int value)
{
	gpio_pin_set_value(gpio_to_pfc_chip(gc), offset, value);

	return pinctrl_gpio_direction_output(offset);
}

static int gpio_pin_get(struct gpio_chip *gc, unsigned offset)
{
	struct sh_pfc_chip *chip = gpio_to_pfc_chip(gc);
	struct pinmux_data_reg *dreg;
	unsigned long pos;
	unsigned int bit;

	gpio_get_data_reg(chip->pfc, offset, &dreg, &bit);

	pos = dreg->reg_width - (bit + 1);

	return (gpio_read_data_reg(chip, dreg) >> pos) & 1;
}

static void gpio_pin_set(struct gpio_chip *gc, unsigned offset, int value)
{
	gpio_pin_set_value(gpio_to_pfc_chip(gc), offset, value);
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

static int gpio_pin_setup(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	struct gpio_chip *gc = &chip->gpio_chip;
	int ret;

	ret = gpio_setup_data_regs(chip);
	if (ret < 0)
		return ret;

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

	return 0;
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

static int gpio_function_setup(struct sh_pfc_chip *chip)
{
	struct sh_pfc *pfc = chip->pfc;
	struct gpio_chip *gc = &chip->gpio_chip;

	gc->request = gpio_function_request;
	gc->free = gpio_function_free;

	gc->label = pfc->info->name;
	gc->owner = THIS_MODULE;
	gc->base = pfc->nr_pins;
	gc->ngpio = pfc->info->nr_func_gpios;

	return 0;
}

/* -----------------------------------------------------------------------------
 * Register/unregister
 */

static struct sh_pfc_chip *
sh_pfc_add_gpiochip(struct sh_pfc *pfc, int(*setup)(struct sh_pfc_chip *))
{
	struct sh_pfc_chip *chip;
	int ret;

	chip = devm_kzalloc(pfc->dev, sizeof(*chip), GFP_KERNEL);
	if (unlikely(!chip))
		return ERR_PTR(-ENOMEM);

	chip->pfc = pfc;

	ret = setup(chip);
	if (ret < 0)
		return ERR_PTR(ret);

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
