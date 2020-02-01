// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Whiskey Cove PMIC GPIO Driver
 *
 * This driver is written based on gpio-crystalcove.c
 *
 * Copyright (C) 2016 Intel Corporation. All rights reserved.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/mfd/intel_soc_pmic.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/seq_file.h>

/*
 * Whiskey Cove PMIC has 13 physical GPIO pins divided into 3 banks:
 * Bank 0: Pin  0 - 6
 * Bank 1: Pin  7 - 10
 * Bank 2: Pin 11 - 12
 * Each pin has one output control register and one input control register.
 */
#define BANK0_NR_PINS		7
#define BANK1_NR_PINS		4
#define BANK2_NR_PINS		2
#define WCOVE_GPIO_NUM		(BANK0_NR_PINS + BANK1_NR_PINS + BANK2_NR_PINS)
#define WCOVE_VGPIO_NUM		94
/* GPIO output control registers (one per pin): 0x4e44 - 0x4e50 */
#define GPIO_OUT_CTRL_BASE	0x4e44
/* GPIO input control registers (one per pin): 0x4e51 - 0x4e5d */
#define GPIO_IN_CTRL_BASE	0x4e51

/*
 * GPIO interrupts are organized in two groups:
 * Group 0: Bank 0 pins (Pin 0 - 6)
 * Group 1: Bank 1 and Bank 2 pins (Pin 7 - 12)
 * Each group has two registers (one bit per pin): status and mask.
 */
#define GROUP0_NR_IRQS		7
#define GROUP1_NR_IRQS		6
#define IRQ_MASK_BASE		0x4e19
#define IRQ_STATUS_BASE		0x4e0b
#define GPIO_IRQ0_MASK		GENMASK(6, 0)
#define GPIO_IRQ1_MASK		GENMASK(5, 0)
#define UPDATE_IRQ_TYPE		BIT(0)
#define UPDATE_IRQ_MASK		BIT(1)

#define CTLI_INTCNT_DIS		(0 << 1)
#define CTLI_INTCNT_NE		(1 << 1)
#define CTLI_INTCNT_PE		(2 << 1)
#define CTLI_INTCNT_BE		(3 << 1)

#define CTLO_DIR_IN		(0 << 5)
#define CTLO_DIR_OUT		(1 << 5)

#define CTLO_DRV_MASK		(1 << 4)
#define CTLO_DRV_OD		(0 << 4)
#define CTLO_DRV_CMOS		(1 << 4)

#define CTLO_DRV_REN		(1 << 3)

#define CTLO_RVAL_2KDOWN	(0 << 1)
#define CTLO_RVAL_2KUP		(1 << 1)
#define CTLO_RVAL_50KDOWN	(2 << 1)
#define CTLO_RVAL_50KUP		(3 << 1)

#define CTLO_INPUT_SET		(CTLO_DRV_CMOS | CTLO_DRV_REN | CTLO_RVAL_2KUP)
#define CTLO_OUTPUT_SET		(CTLO_DIR_OUT | CTLO_INPUT_SET)

enum ctrl_register {
	CTRL_IN,
	CTRL_OUT,
};

/*
 * struct wcove_gpio - Whiskey Cove GPIO controller
 * @buslock: for bus lock/sync and unlock.
 * @chip: the abstract gpio_chip structure.
 * @dev: the gpio device
 * @regmap: the regmap from the parent device.
 * @regmap_irq_chip: the regmap of the gpio irq chip.
 * @update: pending IRQ setting update, to be written to the chip upon unlock.
 * @intcnt: the Interrupt Detect value to be written.
 * @set_irq_mask: true if the IRQ mask needs to be set, false to clear.
 */
struct wcove_gpio {
	struct mutex buslock;
	struct gpio_chip chip;
	struct device *dev;
	struct regmap *regmap;
	struct regmap_irq_chip_data *regmap_irq_chip;
	int update;
	int intcnt;
	bool set_irq_mask;
};

static inline int to_reg(int gpio, enum ctrl_register reg_type)
{
	unsigned int reg;

	if (gpio >= WCOVE_GPIO_NUM)
		return -EOPNOTSUPP;

	if (reg_type == CTRL_IN)
		reg = GPIO_IN_CTRL_BASE + gpio;
	else
		reg = GPIO_OUT_CTRL_BASE + gpio;

	return reg;
}

static void wcove_update_irq_mask(struct wcove_gpio *wg, int gpio)
{
	unsigned int reg, mask;

	if (gpio < GROUP0_NR_IRQS) {
		reg = IRQ_MASK_BASE;
		mask = BIT(gpio % GROUP0_NR_IRQS);
	} else {
		reg = IRQ_MASK_BASE + 1;
		mask = BIT((gpio - GROUP0_NR_IRQS) % GROUP1_NR_IRQS);
	}

	if (wg->set_irq_mask)
		regmap_update_bits(wg->regmap, reg, mask, mask);
	else
		regmap_update_bits(wg->regmap, reg, mask, 0);
}

static void wcove_update_irq_ctrl(struct wcove_gpio *wg, int gpio)
{
	int reg = to_reg(gpio, CTRL_IN);

	if (reg < 0)
		return;

	regmap_update_bits(wg->regmap, reg, CTLI_INTCNT_BE, wg->intcnt);
}

static int wcove_gpio_dir_in(struct gpio_chip *chip, unsigned int gpio)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int reg = to_reg(gpio, CTRL_OUT);

	if (reg < 0)
		return 0;

	return regmap_write(wg->regmap, reg, CTLO_INPUT_SET);
}

static int wcove_gpio_dir_out(struct gpio_chip *chip, unsigned int gpio,
				    int value)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int reg = to_reg(gpio, CTRL_OUT);

	if (reg < 0)
		return 0;

	return regmap_write(wg->regmap, reg, CTLO_OUTPUT_SET | value);
}

static int wcove_gpio_get_direction(struct gpio_chip *chip, unsigned int gpio)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	unsigned int val;
	int ret, reg = to_reg(gpio, CTRL_OUT);

	if (reg < 0)
		return GPIO_LINE_DIRECTION_OUT;

	ret = regmap_read(wg->regmap, reg, &val);
	if (ret)
		return ret;

	if (val & CTLO_DIR_OUT)
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
}

static int wcove_gpio_get(struct gpio_chip *chip, unsigned int gpio)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	unsigned int val;
	int ret, reg = to_reg(gpio, CTRL_IN);

	if (reg < 0)
		return 0;

	ret = regmap_read(wg->regmap, reg, &val);
	if (ret)
		return ret;

	return val & 0x1;
}

static void wcove_gpio_set(struct gpio_chip *chip, unsigned int gpio, int value)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int reg = to_reg(gpio, CTRL_OUT);

	if (reg < 0)
		return;

	if (value)
		regmap_update_bits(wg->regmap, reg, 1, 1);
	else
		regmap_update_bits(wg->regmap, reg, 1, 0);
}

static int wcove_gpio_set_config(struct gpio_chip *chip, unsigned int gpio,
				 unsigned long config)
{
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int reg = to_reg(gpio, CTRL_OUT);

	if (reg < 0)
		return 0;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		return regmap_update_bits(wg->regmap, reg, CTLO_DRV_MASK,
					  CTLO_DRV_OD);
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		return regmap_update_bits(wg->regmap, reg, CTLO_DRV_MASK,
					  CTLO_DRV_CMOS);
	default:
		break;
	}

	return -ENOTSUPP;
}

static int wcove_irq_type(struct irq_data *data, unsigned int type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct wcove_gpio *wg = gpiochip_get_data(chip);

	if (data->hwirq >= WCOVE_GPIO_NUM)
		return 0;

	switch (type) {
	case IRQ_TYPE_NONE:
		wg->intcnt = CTLI_INTCNT_DIS;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		wg->intcnt = CTLI_INTCNT_BE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		wg->intcnt = CTLI_INTCNT_PE;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		wg->intcnt = CTLI_INTCNT_NE;
		break;
	default:
		return -EINVAL;
	}

	wg->update |= UPDATE_IRQ_TYPE;

	return 0;
}

static void wcove_bus_lock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct wcove_gpio *wg = gpiochip_get_data(chip);

	mutex_lock(&wg->buslock);
}

static void wcove_bus_sync_unlock(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int gpio = data->hwirq;

	if (wg->update & UPDATE_IRQ_TYPE)
		wcove_update_irq_ctrl(wg, gpio);
	if (wg->update & UPDATE_IRQ_MASK)
		wcove_update_irq_mask(wg, gpio);
	wg->update = 0;

	mutex_unlock(&wg->buslock);
}

static void wcove_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct wcove_gpio *wg = gpiochip_get_data(chip);

	if (data->hwirq >= WCOVE_GPIO_NUM)
		return;

	wg->set_irq_mask = false;
	wg->update |= UPDATE_IRQ_MASK;
}

static void wcove_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct wcove_gpio *wg = gpiochip_get_data(chip);

	if (data->hwirq >= WCOVE_GPIO_NUM)
		return;

	wg->set_irq_mask = true;
	wg->update |= UPDATE_IRQ_MASK;
}

static struct irq_chip wcove_irqchip = {
	.name			= "Whiskey Cove",
	.irq_mask		= wcove_irq_mask,
	.irq_unmask		= wcove_irq_unmask,
	.irq_set_type		= wcove_irq_type,
	.irq_bus_lock		= wcove_bus_lock,
	.irq_bus_sync_unlock	= wcove_bus_sync_unlock,
};

static irqreturn_t wcove_gpio_irq_handler(int irq, void *data)
{
	struct wcove_gpio *wg = (struct wcove_gpio *)data;
	unsigned int pending, virq, gpio, mask, offset;
	u8 p[2];

	if (regmap_bulk_read(wg->regmap, IRQ_STATUS_BASE, p, 2)) {
		dev_err(wg->dev, "Failed to read irq status register\n");
		return IRQ_NONE;
	}

	pending = (p[0] & GPIO_IRQ0_MASK) | ((p[1] & GPIO_IRQ1_MASK) << 7);
	if (!pending)
		return IRQ_NONE;

	/* Iterate until no interrupt is pending */
	while (pending) {
		/* One iteration is for all pending bits */
		for_each_set_bit(gpio, (const unsigned long *)&pending,
						 WCOVE_GPIO_NUM) {
			offset = (gpio > GROUP0_NR_IRQS) ? 1 : 0;
			mask = (offset == 1) ? BIT(gpio - GROUP0_NR_IRQS) :
								BIT(gpio);
			virq = irq_find_mapping(wg->chip.irq.domain, gpio);
			handle_nested_irq(virq);
			regmap_update_bits(wg->regmap, IRQ_STATUS_BASE + offset,
								mask, mask);
		}

		/* Next iteration */
		if (regmap_bulk_read(wg->regmap, IRQ_STATUS_BASE, p, 2)) {
			dev_err(wg->dev, "Failed to read irq status\n");
			break;
		}

		pending = (p[0] & GPIO_IRQ0_MASK) | ((p[1] & GPIO_IRQ1_MASK) << 7);
	}

	return IRQ_HANDLED;
}

static void wcove_gpio_dbg_show(struct seq_file *s,
				      struct gpio_chip *chip)
{
	unsigned int ctlo, ctli, irq_mask, irq_status;
	struct wcove_gpio *wg = gpiochip_get_data(chip);
	int gpio, offset, group, ret = 0;

	for (gpio = 0; gpio < WCOVE_GPIO_NUM; gpio++) {
		group = gpio < GROUP0_NR_IRQS ? 0 : 1;
		ret += regmap_read(wg->regmap, to_reg(gpio, CTRL_OUT), &ctlo);
		ret += regmap_read(wg->regmap, to_reg(gpio, CTRL_IN), &ctli);
		ret += regmap_read(wg->regmap, IRQ_MASK_BASE + group,
							&irq_mask);
		ret += regmap_read(wg->regmap, IRQ_STATUS_BASE + group,
							&irq_status);
		if (ret) {
			pr_err("Failed to read registers: ctrl out/in or irq status/mask\n");
			break;
		}

		offset = gpio % 8;
		seq_printf(s, " gpio-%-2d %s %s %s %s ctlo=%2x,%s %s\n",
			   gpio, ctlo & CTLO_DIR_OUT ? "out" : "in ",
			   ctli & 0x1 ? "hi" : "lo",
			   ctli & CTLI_INTCNT_NE ? "fall" : "    ",
			   ctli & CTLI_INTCNT_PE ? "rise" : "    ",
			   ctlo,
			   irq_mask & BIT(offset) ? "mask  " : "unmask",
			   irq_status & BIT(offset) ? "pending" : "       ");
	}
}

static int wcove_gpio_probe(struct platform_device *pdev)
{
	struct intel_soc_pmic *pmic;
	struct wcove_gpio *wg;
	int virq, ret, irq;
	struct device *dev;

	/*
	 * This gpio platform device is created by a mfd device (see
	 * drivers/mfd/intel_soc_pmic_bxtwc.c for details). Information
	 * shared by all sub-devices created by the mfd device, the regmap
	 * pointer for instance, is stored as driver data of the mfd device
	 * driver.
	 */
	pmic = dev_get_drvdata(pdev->dev.parent);
	if (!pmic)
		return -ENODEV;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	dev = &pdev->dev;

	wg = devm_kzalloc(dev, sizeof(*wg), GFP_KERNEL);
	if (!wg)
		return -ENOMEM;

	wg->regmap_irq_chip = pmic->irq_chip_data;

	platform_set_drvdata(pdev, wg);

	mutex_init(&wg->buslock);
	wg->chip.label = KBUILD_MODNAME;
	wg->chip.direction_input = wcove_gpio_dir_in;
	wg->chip.direction_output = wcove_gpio_dir_out;
	wg->chip.get_direction = wcove_gpio_get_direction;
	wg->chip.get = wcove_gpio_get;
	wg->chip.set = wcove_gpio_set;
	wg->chip.set_config = wcove_gpio_set_config,
	wg->chip.base = -1;
	wg->chip.ngpio = WCOVE_VGPIO_NUM;
	wg->chip.can_sleep = true;
	wg->chip.parent = pdev->dev.parent;
	wg->chip.dbg_show = wcove_gpio_dbg_show;
	wg->dev = dev;
	wg->regmap = pmic->regmap;

	ret = devm_gpiochip_add_data(dev, &wg->chip, wg);
	if (ret) {
		dev_err(dev, "Failed to add gpiochip: %d\n", ret);
		return ret;
	}

	ret = gpiochip_irqchip_add_nested(&wg->chip, &wcove_irqchip, 0,
					  handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		dev_err(dev, "Failed to add irqchip: %d\n", ret);
		return ret;
	}

	virq = regmap_irq_get_virq(wg->regmap_irq_chip, irq);
	if (virq < 0) {
		dev_err(dev, "Failed to get virq by irq %d\n", irq);
		return virq;
	}

	ret = devm_request_threaded_irq(dev, virq, NULL,
		wcove_gpio_irq_handler, IRQF_ONESHOT, pdev->name, wg);
	if (ret) {
		dev_err(dev, "Failed to request irq %d\n", virq);
		return ret;
	}

	gpiochip_set_nested_irqchip(&wg->chip, &wcove_irqchip, virq);

	/* Enable GPIO0 interrupts */
	ret = regmap_update_bits(wg->regmap, IRQ_MASK_BASE, GPIO_IRQ0_MASK,
				 0x00);
	if (ret)
		return ret;

	/* Enable GPIO1 interrupts */
	ret = regmap_update_bits(wg->regmap, IRQ_MASK_BASE + 1, GPIO_IRQ1_MASK,
				 0x00);
	if (ret)
		return ret;

	return 0;
}

/*
 * Whiskey Cove PMIC itself is a analog device(but with digital control
 * interface) providing power management support for other devices in
 * the accompanied SoC, so we have no .pm for Whiskey Cove GPIO driver.
 */
static struct platform_driver wcove_gpio_driver = {
	.driver = {
		.name = "bxt_wcove_gpio",
	},
	.probe = wcove_gpio_probe,
};

module_platform_driver(wcove_gpio_driver);

MODULE_AUTHOR("Ajay Thomas <ajay.thomas.david.rajamanickam@intel.com>");
MODULE_AUTHOR("Bin Gao <bin.gao@intel.com>");
MODULE_DESCRIPTION("Intel Whiskey Cove GPIO Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt_wcove_gpio");
