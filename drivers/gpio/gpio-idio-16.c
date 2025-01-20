// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO library for the ACCES IDIO-16 family
 * Copyright (C) 2022 William Breathitt Gray
 */
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/gpio/regmap.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/types.h>

#include "gpio-idio-16.h"

#define DEFAULT_SYMBOL_NAMESPACE "GPIO_IDIO_16"

#define IDIO_16_DAT_BASE 0x0
#define IDIO_16_OUT_BASE IDIO_16_DAT_BASE
#define IDIO_16_IN_BASE (IDIO_16_DAT_BASE + 1)
#define IDIO_16_CLEAR_INTERRUPT 0x1
#define IDIO_16_ENABLE_IRQ 0x2
#define IDIO_16_DEACTIVATE_INPUT_FILTERS 0x3
#define IDIO_16_DISABLE_IRQ IDIO_16_ENABLE_IRQ
#define IDIO_16_INTERRUPT_STATUS 0x6

#define IDIO_16_NGPIO 32
#define IDIO_16_NGPIO_PER_REG 8
#define IDIO_16_REG_STRIDE 4

struct idio_16_data {
	struct regmap *map;
	unsigned int irq_mask;
};

static int idio_16_handle_mask_sync(const int index, const unsigned int mask_buf_def,
				    const unsigned int mask_buf, void *const irq_drv_data)
{
	struct idio_16_data *const data = irq_drv_data;
	const unsigned int prev_mask = data->irq_mask;
	int err;
	unsigned int val;

	/* exit early if no change since the previous mask */
	if (mask_buf == prev_mask)
		return 0;

	/* remember the current mask for the next mask sync */
	data->irq_mask = mask_buf;

	/* if all previously masked, enable interrupts when unmasking */
	if (prev_mask == mask_buf_def) {
		err = regmap_write(data->map, IDIO_16_CLEAR_INTERRUPT, 0x00);
		if (err)
			return err;
		return regmap_read(data->map, IDIO_16_ENABLE_IRQ, &val);
	}

	/* if all are currently masked, disable interrupts */
	if (mask_buf == mask_buf_def)
		return regmap_write(data->map, IDIO_16_DISABLE_IRQ, 0x00);

	return 0;
}

static int idio_16_reg_mask_xlate(struct gpio_regmap *const gpio, const unsigned int base,
				  const unsigned int offset, unsigned int *const reg,
				  unsigned int *const mask)
{
	unsigned int stride;

	/* Input lines start at GPIO 16 */
	if (offset < 16) {
		stride = offset / IDIO_16_NGPIO_PER_REG;
		*reg = IDIO_16_OUT_BASE + stride * IDIO_16_REG_STRIDE;
	} else {
		stride = (offset - 16) / IDIO_16_NGPIO_PER_REG;
		*reg = IDIO_16_IN_BASE + stride * IDIO_16_REG_STRIDE;
	}

	*mask = BIT(offset % IDIO_16_NGPIO_PER_REG);

	return 0;
}

static const char *idio_16_names[IDIO_16_NGPIO] = {
	"OUT0", "OUT1", "OUT2", "OUT3", "OUT4", "OUT5", "OUT6", "OUT7",
	"OUT8", "OUT9", "OUT10", "OUT11", "OUT12", "OUT13", "OUT14", "OUT15",
	"IIN0", "IIN1", "IIN2", "IIN3", "IIN4", "IIN5", "IIN6", "IIN7",
	"IIN8", "IIN9", "IIN10", "IIN11", "IIN12", "IIN13", "IIN14", "IIN15",
};

/**
 * devm_idio_16_regmap_register - Register an IDIO-16 GPIO device
 * @dev:	device that is registering this IDIO-16 GPIO device
 * @config:	configuration for idio_16_regmap_config
 *
 * Registers an IDIO-16 GPIO device. Returns 0 on success and negative error number on failure.
 */
int devm_idio_16_regmap_register(struct device *const dev,
				 const struct idio_16_regmap_config *const config)
{
	struct gpio_regmap_config gpio_config = {};
	int err;
	struct idio_16_data *data;
	struct regmap_irq_chip *chip;
	struct regmap_irq_chip_data *chip_data;

	if (!config->parent)
		return -EINVAL;

	if (!config->map)
		return -EINVAL;

	if (!config->regmap_irqs)
		return -EINVAL;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->map = config->map;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->name = dev_name(dev);
	chip->status_base = IDIO_16_INTERRUPT_STATUS;
	chip->mask_base = IDIO_16_ENABLE_IRQ;
	chip->ack_base = IDIO_16_CLEAR_INTERRUPT;
	chip->no_status = config->no_status;
	chip->num_regs = 1;
	chip->irqs = config->regmap_irqs;
	chip->num_irqs = config->num_regmap_irqs;
	chip->handle_mask_sync = idio_16_handle_mask_sync;
	chip->irq_drv_data = data;

	/* Disable IRQ to prevent spurious interrupts before we're ready */
	err = regmap_write(data->map, IDIO_16_DISABLE_IRQ, 0x00);
	if (err)
		return err;

	err = devm_regmap_add_irq_chip(dev, data->map, config->irq, 0, 0, chip, &chip_data);
	if (err)
		return dev_err_probe(dev, err, "IRQ registration failed\n");

	if (config->filters) {
		/* Deactivate input filters */
		err = regmap_write(data->map, IDIO_16_DEACTIVATE_INPUT_FILTERS, 0x00);
		if (err)
			return err;
	}

	gpio_config.parent = config->parent;
	gpio_config.regmap = data->map;
	gpio_config.ngpio = IDIO_16_NGPIO;
	gpio_config.names = idio_16_names;
	gpio_config.reg_dat_base = GPIO_REGMAP_ADDR(IDIO_16_DAT_BASE);
	gpio_config.reg_set_base = GPIO_REGMAP_ADDR(IDIO_16_DAT_BASE);
	gpio_config.ngpio_per_reg = IDIO_16_NGPIO_PER_REG;
	gpio_config.reg_stride = IDIO_16_REG_STRIDE;
	gpio_config.irq_domain = regmap_irq_get_domain(chip_data);
	gpio_config.reg_mask_xlate = idio_16_reg_mask_xlate;

	return PTR_ERR_OR_ZERO(devm_gpio_regmap_register(dev, &gpio_config));
}
EXPORT_SYMBOL_GPL(devm_idio_16_regmap_register);

MODULE_AUTHOR("William Breathitt Gray");
MODULE_DESCRIPTION("ACCES IDIO-16 GPIO Library");
MODULE_LICENSE("GPL");
