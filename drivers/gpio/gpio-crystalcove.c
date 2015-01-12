/*
 * gpio-crystalcove.c - Intel Crystal Cove GPIO Driver
 *
 * Copyright (C) 2012, 2014 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Author: Yang, Bin <bin.yang@intel.com>
 */

#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/seq_file.h>
#include <linux/bitops.h>
#include <linux/regmap.h>
#include <linux/mfd/intel_soc_pmic.h>

#define CRYSTALCOVE_GPIO_NUM	16
#define CRYSTALCOVE_VGPIO_NUM	94

#define UPDATE_IRQ_TYPE		BIT(0)
#define UPDATE_IRQ_MASK		BIT(1)

#define GPIO0IRQ		0x0b
#define GPIO1IRQ		0x0c
#define MGPIO0IRQS0		0x19
#define MGPIO1IRQS0		0x1a
#define MGPIO0IRQSX		0x1b
#define MGPIO1IRQSX		0x1c
#define GPIO0P0CTLO		0x2b
#define GPIO0P0CTLI		0x33
#define GPIO1P0CTLO		0x3b
#define GPIO1P0CTLI		0x43

#define CTLI_INTCNT_DIS		(0)
#define CTLI_INTCNT_NE		(1 << 1)
#define CTLI_INTCNT_PE		(2 << 1)
#define CTLI_INTCNT_BE		(3 << 1)

#define CTLO_DIR_IN		(0)
#define CTLO_DIR_OUT		(1 << 5)

#define CTLO_DRV_CMOS		(0)
#define CTLO_DRV_OD		(1 << 4)

#define CTLO_DRV_REN		(1 << 3)

#define CTLO_RVAL_2KDW		(0)
#define CTLO_RVAL_2KUP		(1 << 1)
#define CTLO_RVAL_50KDW		(2 << 1)
#define CTLO_RVAL_50KUP		(3 << 1)

#define CTLO_INPUT_SET	(CTLO_DRV_CMOS | CTLO_DRV_REN | CTLO_RVAL_2KUP)
#define CTLO_OUTPUT_SET	(CTLO_DIR_OUT | CTLO_INPUT_SET)

enum ctrl_register {
	CTRL_IN,
	CTRL_OUT,
};

/**
 * struct crystalcove_gpio - Crystal Cove GPIO controller
 * @buslock: for bus lock/sync and unlock.
 * @chip: the abstract gpio_chip structure.
 * @regmap: the regmap from the parent device.
 * @update: pending IRQ setting update, to be written to the chip upon unlock.
 * @intcnt_value: the Interrupt Detect value to be written.
 * @set_irq_mask: true if the IRQ mask needs to be set, false to clear.
 */
struct crystalcove_gpio {
	struct mutex buslock; /* irq_bus_lock */
	struct gpio_chip chip;
	struct regmap *regmap;
	int update;
	int intcnt_value;
	bool set_irq_mask;
};

static inline struct crystalcove_gpio *to_cg(struct gpio_chip *gc)
{
	return container_of(gc, struct crystalcove_gpio, chip);
}

static inline int to_reg(int gpio, enum ctrl_register reg_type)
{
	int reg;

	if (reg_type == CTRL_IN) {
		if (gpio < 8)
			reg = GPIO0P0CTLI;
		else
			reg = GPIO1P0CTLI;
	} else {
		if (gpio < 8)
			reg = GPIO0P0CTLO;
		else
			reg = GPIO1P0CTLO;
	}

	return reg + gpio % 8;
}

static void crystalcove_update_irq_mask(struct crystalcove_gpio *cg,
					int gpio)
{
	u8 mirqs0 = gpio < 8 ? MGPIO0IRQS0 : MGPIO1IRQS0;
	int mask = BIT(gpio % 8);

	if (cg->set_irq_mask)
		regmap_update_bits(cg->regmap, mirqs0, mask, mask);
	else
		regmap_update_bits(cg->regmap, mirqs0, mask, 0);
}

static void crystalcove_update_irq_ctrl(struct crystalcove_gpio *cg, int gpio)
{
	int reg = to_reg(gpio, CTRL_IN);

	regmap_update_bits(cg->regmap, reg, CTLI_INTCNT_BE, cg->intcnt_value);
}

static int crystalcove_gpio_dir_in(struct gpio_chip *chip, unsigned gpio)
{
	struct crystalcove_gpio *cg = to_cg(chip);

	if (gpio > CRYSTALCOVE_VGPIO_NUM)
		return 0;

	return regmap_write(cg->regmap, to_reg(gpio, CTRL_OUT),
			    CTLO_INPUT_SET);
}

static int crystalcove_gpio_dir_out(struct gpio_chip *chip, unsigned gpio,
				    int value)
{
	struct crystalcove_gpio *cg = to_cg(chip);

	if (gpio > CRYSTALCOVE_VGPIO_NUM)
		return 0;

	return regmap_write(cg->regmap, to_reg(gpio, CTRL_OUT),
			    CTLO_OUTPUT_SET | value);
}

static int crystalcove_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	struct crystalcove_gpio *cg = to_cg(chip);
	int ret;
	unsigned int val;

	if (gpio > CRYSTALCOVE_VGPIO_NUM)
		return 0;

	ret = regmap_read(cg->regmap, to_reg(gpio, CTRL_IN), &val);
	if (ret)
		return ret;

	return val & 0x1;
}

static void crystalcove_gpio_set(struct gpio_chip *chip,
				 unsigned gpio, int value)
{
	struct crystalcove_gpio *cg = to_cg(chip);

	if (gpio > CRYSTALCOVE_VGPIO_NUM)
		return;

	if (value)
		regmap_update_bits(cg->regmap, to_reg(gpio, CTRL_OUT), 1, 1);
	else
		regmap_update_bits(cg->regmap, to_reg(gpio, CTRL_OUT), 1, 0);
}

static int crystalcove_irq_type(struct irq_data *data, unsigned type)
{
	struct crystalcove_gpio *cg = to_cg(irq_data_get_irq_chip_data(data));

	switch (type) {
	case IRQ_TYPE_NONE:
		cg->intcnt_value = CTLI_INTCNT_DIS;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		cg->intcnt_value = CTLI_INTCNT_BE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		cg->intcnt_value = CTLI_INTCNT_PE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		cg->intcnt_value = CTLI_INTCNT_NE;
		break;
	default:
		return -EINVAL;
	}

	cg->update |= UPDATE_IRQ_TYPE;

	return 0;
}

static void crystalcove_bus_lock(struct irq_data *data)
{
	struct crystalcove_gpio *cg = to_cg(irq_data_get_irq_chip_data(data));

	mutex_lock(&cg->buslock);
}

static void crystalcove_bus_sync_unlock(struct irq_data *data)
{
	struct crystalcove_gpio *cg = to_cg(irq_data_get_irq_chip_data(data));
	int gpio = data->hwirq;

	if (cg->update & UPDATE_IRQ_TYPE)
		crystalcove_update_irq_ctrl(cg, gpio);
	if (cg->update & UPDATE_IRQ_MASK)
		crystalcove_update_irq_mask(cg, gpio);
	cg->update = 0;

	mutex_unlock(&cg->buslock);
}

static void crystalcove_irq_unmask(struct irq_data *data)
{
	struct crystalcove_gpio *cg = to_cg(irq_data_get_irq_chip_data(data));

	cg->set_irq_mask = false;
	cg->update |= UPDATE_IRQ_MASK;
}

static void crystalcove_irq_mask(struct irq_data *data)
{
	struct crystalcove_gpio *cg = to_cg(irq_data_get_irq_chip_data(data));

	cg->set_irq_mask = true;
	cg->update |= UPDATE_IRQ_MASK;
}

static struct irq_chip crystalcove_irqchip = {
	.name			= "Crystal Cove",
	.irq_mask		= crystalcove_irq_mask,
	.irq_unmask		= crystalcove_irq_unmask,
	.irq_set_type		= crystalcove_irq_type,
	.irq_bus_lock		= crystalcove_bus_lock,
	.irq_bus_sync_unlock	= crystalcove_bus_sync_unlock,
};

static irqreturn_t crystalcove_gpio_irq_handler(int irq, void *data)
{
	struct crystalcove_gpio *cg = data;
	unsigned int p0, p1;
	int pending;
	int gpio;
	unsigned int virq;

	if (regmap_read(cg->regmap, GPIO0IRQ, &p0) ||
	    regmap_read(cg->regmap, GPIO1IRQ, &p1))
		return IRQ_NONE;

	regmap_write(cg->regmap, GPIO0IRQ, p0);
	regmap_write(cg->regmap, GPIO1IRQ, p1);

	pending = p0 | p1 << 8;

	for (gpio = 0; gpio < CRYSTALCOVE_GPIO_NUM; gpio++) {
		if (pending & BIT(gpio)) {
			virq = irq_find_mapping(cg->chip.irqdomain, gpio);
			handle_nested_irq(virq);
		}
	}

	return IRQ_HANDLED;
}

static void crystalcove_gpio_dbg_show(struct seq_file *s,
				      struct gpio_chip *chip)
{
	struct crystalcove_gpio *cg = to_cg(chip);
	int gpio, offset;
	unsigned int ctlo, ctli, mirqs0, mirqsx, irq;

	for (gpio = 0; gpio < CRYSTALCOVE_GPIO_NUM; gpio++) {
		regmap_read(cg->regmap, to_reg(gpio, CTRL_OUT), &ctlo);
		regmap_read(cg->regmap, to_reg(gpio, CTRL_IN), &ctli);
		regmap_read(cg->regmap, gpio < 8 ? MGPIO0IRQS0 : MGPIO1IRQS0,
			    &mirqs0);
		regmap_read(cg->regmap, gpio < 8 ? MGPIO0IRQSX : MGPIO1IRQSX,
			    &mirqsx);
		regmap_read(cg->regmap, gpio < 8 ? GPIO0IRQ : GPIO1IRQ,
			    &irq);

		offset = gpio % 8;
		seq_printf(s, " gpio-%-2d %s %s %s %s ctlo=%2x,%s %s %s\n",
			   gpio, ctlo & CTLO_DIR_OUT ? "out" : "in ",
			   ctli & 0x1 ? "hi" : "lo",
			   ctli & CTLI_INTCNT_NE ? "fall" : "    ",
			   ctli & CTLI_INTCNT_PE ? "rise" : "    ",
			   ctlo,
			   mirqs0 & BIT(offset) ? "s0 mask  " : "s0 unmask",
			   mirqsx & BIT(offset) ? "sx mask  " : "sx unmask",
			   irq & BIT(offset) ? "pending" : "       ");
	}
}

static int crystalcove_gpio_probe(struct platform_device *pdev)
{
	int irq = platform_get_irq(pdev, 0);
	struct crystalcove_gpio *cg;
	int retval;
	struct device *dev = pdev->dev.parent;
	struct intel_soc_pmic *pmic = dev_get_drvdata(dev);

	if (irq < 0)
		return irq;

	cg = devm_kzalloc(&pdev->dev, sizeof(*cg), GFP_KERNEL);
	if (!cg)
		return -ENOMEM;

	platform_set_drvdata(pdev, cg);

	mutex_init(&cg->buslock);
	cg->chip.label = KBUILD_MODNAME;
	cg->chip.direction_input = crystalcove_gpio_dir_in;
	cg->chip.direction_output = crystalcove_gpio_dir_out;
	cg->chip.get = crystalcove_gpio_get;
	cg->chip.set = crystalcove_gpio_set;
	cg->chip.base = -1;
	cg->chip.ngpio = CRYSTALCOVE_VGPIO_NUM;
	cg->chip.can_sleep = true;
	cg->chip.dev = dev;
	cg->chip.dbg_show = crystalcove_gpio_dbg_show;
	cg->regmap = pmic->regmap;

	retval = gpiochip_add(&cg->chip);
	if (retval) {
		dev_warn(&pdev->dev, "add gpio chip error: %d\n", retval);
		return retval;
	}

	gpiochip_irqchip_add(&cg->chip, &crystalcove_irqchip, 0,
			     handle_simple_irq, IRQ_TYPE_NONE);

	retval = request_threaded_irq(irq, NULL, crystalcove_gpio_irq_handler,
				      IRQF_ONESHOT, KBUILD_MODNAME, cg);

	if (retval) {
		dev_warn(&pdev->dev, "request irq failed: %d\n", retval);
		goto out_remove_gpio;
	}

	return 0;

out_remove_gpio:
	gpiochip_remove(&cg->chip);
	return retval;
}

static int crystalcove_gpio_remove(struct platform_device *pdev)
{
	struct crystalcove_gpio *cg = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	gpiochip_remove(&cg->chip);
	if (irq >= 0)
		free_irq(irq, cg);
	return 0;
}

static struct platform_driver crystalcove_gpio_driver = {
	.probe = crystalcove_gpio_probe,
	.remove = crystalcove_gpio_remove,
	.driver = {
		.name = "crystal_cove_gpio",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(crystalcove_gpio_driver);

MODULE_AUTHOR("Yang, Bin <bin.yang@intel.com>");
MODULE_DESCRIPTION("Intel Crystal Cove GPIO Driver");
MODULE_LICENSE("GPL v2");
