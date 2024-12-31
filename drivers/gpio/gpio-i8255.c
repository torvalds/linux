// SPDX-License-Identifier: GPL-2.0
/*
 * Intel 8255 Programmable Peripheral Interface
 * Copyright (C) 2022 William Breathitt Gray
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gpio/regmap.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include "gpio-i8255.h"

#define I8255_NGPIO 24
#define I8255_NGPIO_PER_REG 8
#define I8255_CONTROL_PORTC_LOWER_DIRECTION BIT(0)
#define I8255_CONTROL_PORTB_DIRECTION BIT(1)
#define I8255_CONTROL_PORTC_UPPER_DIRECTION BIT(3)
#define I8255_CONTROL_PORTA_DIRECTION BIT(4)
#define I8255_CONTROL_MODE_SET BIT(7)
#define I8255_PORTA 0x0
#define I8255_PORTB 0x1
#define I8255_PORTC 0x2
#define I8255_CONTROL 0x3
#define I8255_REG_DAT_BASE I8255_PORTA
#define I8255_REG_DIR_IN_BASE I8255_CONTROL

static int i8255_direction_mask(const unsigned int offset)
{
	const unsigned int stride = offset / I8255_NGPIO_PER_REG;
	const unsigned int line = offset % I8255_NGPIO_PER_REG;

	switch (stride) {
	case I8255_PORTA:
		return I8255_CONTROL_PORTA_DIRECTION;
	case I8255_PORTB:
		return I8255_CONTROL_PORTB_DIRECTION;
	case I8255_PORTC:
		/* Port C can be configured by nibble */
		if (line >= 4)
			return I8255_CONTROL_PORTC_UPPER_DIRECTION;
		return I8255_CONTROL_PORTC_LOWER_DIRECTION;
	default:
		/* Should never reach this path */
		return 0;
	}
}

static int i8255_ppi_init(struct regmap *const map, const unsigned int base)
{
	int err;

	/* Configure all ports to MODE 0 output mode */
	err = regmap_write(map, base + I8255_CONTROL, I8255_CONTROL_MODE_SET);
	if (err)
		return err;

	/* Initialize all GPIO to output 0 */
	err = regmap_write(map, base + I8255_PORTA, 0x00);
	if (err)
		return err;
	err = regmap_write(map, base + I8255_PORTB, 0x00);
	if (err)
		return err;
	return regmap_write(map, base + I8255_PORTC, 0x00);
}

static int i8255_reg_mask_xlate(struct gpio_regmap *gpio, unsigned int base,
				unsigned int offset, unsigned int *reg,
				unsigned int *mask)
{
	const unsigned int ppi = offset / I8255_NGPIO;
	const unsigned int ppi_offset = offset % I8255_NGPIO;
	const unsigned int stride = ppi_offset / I8255_NGPIO_PER_REG;
	const unsigned int line = ppi_offset % I8255_NGPIO_PER_REG;

	switch (base) {
	case I8255_REG_DAT_BASE:
		*reg = base + stride + ppi * 4;
		*mask = BIT(line);
		return 0;
	case I8255_REG_DIR_IN_BASE:
		*reg = base + ppi * 4;
		*mask = i8255_direction_mask(ppi_offset);
		return 0;
	default:
		/* Should never reach this path */
		return -EINVAL;
	}
}

/**
 * devm_i8255_regmap_register - Register an i8255 GPIO controller
 * @dev:	device that is registering this i8255 GPIO device
 * @config:	configuration for i8255_regmap_config
 *
 * Registers an Intel 8255 Programmable Peripheral Interface GPIO controller.
 * Returns 0 on success and negative error number on failure.
 */
int devm_i8255_regmap_register(struct device *const dev,
			       const struct i8255_regmap_config *const config)
{
	struct gpio_regmap_config gpio_config = {0};
	unsigned long i;
	int err;

	if (!config->parent)
		return -EINVAL;

	if (!config->map)
		return -EINVAL;

	if (!config->num_ppi)
		return -EINVAL;

	for (i = 0; i < config->num_ppi; i++) {
		err = i8255_ppi_init(config->map, i * 4);
		if (err)
			return err;
	}

	gpio_config.parent = config->parent;
	gpio_config.regmap = config->map;
	gpio_config.ngpio = I8255_NGPIO * config->num_ppi;
	gpio_config.names = config->names;
	gpio_config.reg_dat_base = GPIO_REGMAP_ADDR(I8255_REG_DAT_BASE);
	gpio_config.reg_set_base = GPIO_REGMAP_ADDR(I8255_REG_DAT_BASE);
	gpio_config.reg_dir_in_base = GPIO_REGMAP_ADDR(I8255_REG_DIR_IN_BASE);
	gpio_config.ngpio_per_reg = I8255_NGPIO_PER_REG;
	gpio_config.irq_domain = config->domain;
	gpio_config.reg_mask_xlate = i8255_reg_mask_xlate;

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &gpio_config));
}
EXPORT_SYMBOL_NS_GPL(devm_i8255_regmap_register, "I8255");

MODULE_AUTHOR("William Breathitt Gray");
MODULE_DESCRIPTION("Intel 8255 Programmable Peripheral Interface");
MODULE_LICENSE("GPL");
