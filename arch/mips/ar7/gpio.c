/*
 * Copyright (C) 2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2007 Eugene Konev <ejka@openwrt.org>
 * Copyright (C) 2009 Florian Fainelli <florian@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <linux/gpio.h>

#include <asm/mach-ar7/gpio.h>

struct ar7_gpio_chip {
	void __iomem	*regs;
	struct gpio_chip chip;
};

static int ar7_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_in = gpch->regs + AR7_GPIO_INPUT;

	return readl(gpio_in) & (1 << gpio);
}

static void ar7_gpio_set_value(struct gpio_chip *chip,
				unsigned gpio, int value)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_out = gpch->regs + AR7_GPIO_OUTPUT;
	unsigned tmp;

	tmp = readl(gpio_out) & ~(1 << gpio);
	if (value)
		tmp |= 1 << gpio;
	writel(tmp, gpio_out);
}

static int ar7_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_dir = gpch->regs + AR7_GPIO_DIR;

	writel(readl(gpio_dir) | (1 << gpio), gpio_dir);

	return 0;
}

static int ar7_gpio_direction_output(struct gpio_chip *chip,
					unsigned gpio, int value)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_dir = gpch->regs + AR7_GPIO_DIR;

	ar7_gpio_set_value(chip, gpio, value);
	writel(readl(gpio_dir) & ~(1 << gpio), gpio_dir);

	return 0;
}

static struct ar7_gpio_chip ar7_gpio_chip = {
	.chip = {
		.label		= "ar7-gpio",
		.direction_input	= ar7_gpio_direction_input,
		.direction_output	= ar7_gpio_direction_output,
		.set			= ar7_gpio_set_value,
		.get			= ar7_gpio_get_value,
		.base			= 0,
		.ngpio			= AR7_GPIO_MAX,
	}
};

int ar7_gpio_enable(unsigned gpio)
{
	void __iomem *gpio_en = ar7_gpio_chip.regs + AR7_GPIO_ENABLE;

	writel(readl(gpio_en) | (1 << gpio), gpio_en);

	return 0;
}
EXPORT_SYMBOL(ar7_gpio_enable);

int ar7_gpio_disable(unsigned gpio)
{
	void __iomem *gpio_en = ar7_gpio_chip.regs + AR7_GPIO_ENABLE;

	writel(readl(gpio_en) & ~(1 << gpio), gpio_en);

	return 0;
}
EXPORT_SYMBOL(ar7_gpio_disable);

static int __init ar7_gpio_init(void)
{
	int ret;

	ar7_gpio_chip.regs = ioremap_nocache(AR7_REGS_GPIO,
					AR7_REGS_GPIO + 0x10);

	if (!ar7_gpio_chip.regs) {
		printk(KERN_ERR "ar7-gpio: failed to ioremap regs\n");
		return -ENOMEM;
	}

	ret = gpiochip_add(&ar7_gpio_chip.chip);
	if (ret) {
		printk(KERN_ERR "ar7-gpio: failed to add gpiochip\n");
		return ret;
	}
	printk(KERN_INFO "ar7-gpio: registered %d GPIOs\n",
				ar7_gpio_chip.chip.ngpio);
	return ret;
}
arch_initcall(ar7_gpio_init);
