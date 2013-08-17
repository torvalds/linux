/*
 * Copyright (C) 2007 Felix Fietkau <nbd@openwrt.org>
 * Copyright (C) 2007 Eugene Konev <ejka@openwrt.org>
 * Copyright (C) 2009-2010 Florian Fainelli <florian@openwrt.org>
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
	void __iomem		*regs;
	struct gpio_chip	chip;
};

static int ar7_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_in = gpch->regs + AR7_GPIO_INPUT;

	return readl(gpio_in) & (1 << gpio);
}

static int titan_gpio_get_value(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_in0 = gpch->regs + TITAN_GPIO_INPUT_0;
	void __iomem *gpio_in1 = gpch->regs + TITAN_GPIO_INPUT_1;

	return readl(gpio >> 5 ? gpio_in1 : gpio_in0) & (1 << (gpio & 0x1f));
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

static void titan_gpio_set_value(struct gpio_chip *chip,
				unsigned gpio, int value)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_out0 = gpch->regs + TITAN_GPIO_OUTPUT_0;
	void __iomem *gpio_out1 = gpch->regs + TITAN_GPIO_OUTPUT_1;
	unsigned tmp;

	tmp = readl(gpio >> 5 ? gpio_out1 : gpio_out0) & ~(1 << (gpio & 0x1f));
	if (value)
		tmp |= 1 << (gpio & 0x1f);
	writel(tmp, gpio >> 5 ? gpio_out1 : gpio_out0);
}

static int ar7_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_dir = gpch->regs + AR7_GPIO_DIR;

	writel(readl(gpio_dir) | (1 << gpio), gpio_dir);

	return 0;
}

static int titan_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_dir0 = gpch->regs + TITAN_GPIO_DIR_0;
	void __iomem *gpio_dir1 = gpch->regs + TITAN_GPIO_DIR_1;

	if (gpio >= TITAN_GPIO_MAX)
		return -EINVAL;

	writel(readl(gpio >> 5 ? gpio_dir1 : gpio_dir0) | (1 << (gpio & 0x1f)),
			gpio >> 5 ? gpio_dir1 : gpio_dir0);
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

static int titan_gpio_direction_output(struct gpio_chip *chip,
					unsigned gpio, int value)
{
	struct ar7_gpio_chip *gpch =
				container_of(chip, struct ar7_gpio_chip, chip);
	void __iomem *gpio_dir0 = gpch->regs + TITAN_GPIO_DIR_0;
	void __iomem *gpio_dir1 = gpch->regs + TITAN_GPIO_DIR_1;

	if (gpio >= TITAN_GPIO_MAX)
		return -EINVAL;

	titan_gpio_set_value(chip, gpio, value);
	writel(readl(gpio >> 5 ? gpio_dir1 : gpio_dir0) & ~(1 <<
		(gpio & 0x1f)), gpio >> 5 ? gpio_dir1 : gpio_dir0);

	return 0;
}

static struct ar7_gpio_chip ar7_gpio_chip = {
	.chip = {
		.label			= "ar7-gpio",
		.direction_input	= ar7_gpio_direction_input,
		.direction_output	= ar7_gpio_direction_output,
		.set			= ar7_gpio_set_value,
		.get			= ar7_gpio_get_value,
		.base			= 0,
		.ngpio			= AR7_GPIO_MAX,
	}
};

static struct ar7_gpio_chip titan_gpio_chip = {
	.chip = {
		.label			= "titan-gpio",
		.direction_input	= titan_gpio_direction_input,
		.direction_output	= titan_gpio_direction_output,
		.set			= titan_gpio_set_value,
		.get			= titan_gpio_get_value,
		.base			= 0,
		.ngpio			= TITAN_GPIO_MAX,
	}
};

static inline int ar7_gpio_enable_ar7(unsigned gpio)
{
	void __iomem *gpio_en = ar7_gpio_chip.regs + AR7_GPIO_ENABLE;

	writel(readl(gpio_en) | (1 << gpio), gpio_en);

	return 0;
}

static inline int ar7_gpio_enable_titan(unsigned gpio)
{
	void __iomem *gpio_en0 = titan_gpio_chip.regs  + TITAN_GPIO_ENBL_0;
	void __iomem *gpio_en1 = titan_gpio_chip.regs  + TITAN_GPIO_ENBL_1;

	writel(readl(gpio >> 5 ? gpio_en1 : gpio_en0) | (1 << (gpio & 0x1f)),
		gpio >> 5 ? gpio_en1 : gpio_en0);

	return 0;
}

int ar7_gpio_enable(unsigned gpio)
{
	return ar7_is_titan() ? ar7_gpio_enable_titan(gpio) :
				ar7_gpio_enable_ar7(gpio);
}
EXPORT_SYMBOL(ar7_gpio_enable);

static inline int ar7_gpio_disable_ar7(unsigned gpio)
{
	void __iomem *gpio_en = ar7_gpio_chip.regs + AR7_GPIO_ENABLE;

	writel(readl(gpio_en) & ~(1 << gpio), gpio_en);

	return 0;
}

static inline int ar7_gpio_disable_titan(unsigned gpio)
{
	void __iomem *gpio_en0 = titan_gpio_chip.regs + TITAN_GPIO_ENBL_0;
	void __iomem *gpio_en1 = titan_gpio_chip.regs + TITAN_GPIO_ENBL_1;

	writel(readl(gpio >> 5 ? gpio_en1 : gpio_en0) & ~(1 << (gpio & 0x1f)),
			gpio >> 5 ? gpio_en1 : gpio_en0);

	return 0;
}

int ar7_gpio_disable(unsigned gpio)
{
	return ar7_is_titan() ? ar7_gpio_disable_titan(gpio) :
				ar7_gpio_disable_ar7(gpio);
}
EXPORT_SYMBOL(ar7_gpio_disable);

struct titan_gpio_cfg {
	u32 reg;
	u32 shift;
	u32 func;
};

static const struct titan_gpio_cfg titan_gpio_table[] = {
	/* reg, start bit, mux value */
	{4, 24, 1},
	{4, 26, 1},
	{4, 28, 1},
	{4, 30, 1},
	{5, 6, 1},
	{5, 8, 1},
	{5, 10, 1},
	{5, 12, 1},
	{7, 14, 3},
	{7, 16, 3},
	{7, 18, 3},
	{7, 20, 3},
	{7, 22, 3},
	{7, 26, 3},
	{7, 28, 3},
	{7, 30, 3},
	{8, 0, 3},
	{8, 2, 3},
	{8, 4, 3},
	{8, 10, 3},
	{8, 14, 3},
	{8, 16, 3},
	{8, 18, 3},
	{8, 20, 3},
	{9, 8, 3},
	{9, 10, 3},
	{9, 12, 3},
	{9, 14, 3},
	{9, 18, 3},
	{9, 20, 3},
	{9, 24, 3},
	{9, 26, 3},
	{9, 28, 3},
	{9, 30, 3},
	{10, 0, 3},
	{10, 2, 3},
	{10, 8, 3},
	{10, 10, 3},
	{10, 12, 3},
	{10, 14, 3},
	{13, 12, 3},
	{13, 14, 3},
	{13, 16, 3},
	{13, 18, 3},
	{13, 24, 3},
	{13, 26, 3},
	{13, 28, 3},
	{13, 30, 3},
	{14, 2, 3},
	{14, 6, 3},
	{14, 8, 3},
	{14, 12, 3}
};

static int titan_gpio_pinsel(unsigned gpio)
{
	struct titan_gpio_cfg gpio_cfg;
	u32 mux_status, pin_sel_reg, tmp;
	void __iomem *pin_sel = (void __iomem *)KSEG1ADDR(AR7_REGS_PINSEL);

	if (gpio >= ARRAY_SIZE(titan_gpio_table))
		return -EINVAL;

	gpio_cfg = titan_gpio_table[gpio];
	pin_sel_reg = gpio_cfg.reg - 1;

	mux_status = (readl(pin_sel + pin_sel_reg) >> gpio_cfg.shift) & 0x3;

	/* Check the mux status */
	if (!((mux_status == 0) || (mux_status == gpio_cfg.func)))
		return 0;

	/* Set the pin sel value */
	tmp = readl(pin_sel + pin_sel_reg);
	tmp |= ((gpio_cfg.func & 0x3) << gpio_cfg.shift);
	writel(tmp, pin_sel + pin_sel_reg);

	return 0;
}

/* Perform minimal Titan GPIO configuration */
static void titan_gpio_init(void)
{
	unsigned i;

	for (i = 44; i < 48; i++) {
		titan_gpio_pinsel(i);
		ar7_gpio_enable_titan(i);
		titan_gpio_direction_input(&titan_gpio_chip.chip, i);
	}
}

int __init ar7_gpio_init(void)
{
	int ret;
	struct ar7_gpio_chip *gpch;
	unsigned size;

	if (!ar7_is_titan()) {
		gpch = &ar7_gpio_chip;
		size = 0x10;
	} else {
		gpch = &titan_gpio_chip;
		size = 0x1f;
	}

	gpch->regs = ioremap_nocache(AR7_REGS_GPIO, size);
	if (!gpch->regs) {
		printk(KERN_ERR "%s: failed to ioremap regs\n",
					gpch->chip.label);
		return -ENOMEM;
	}

	ret = gpiochip_add(&gpch->chip);
	if (ret) {
		printk(KERN_ERR "%s: failed to add gpiochip\n",
					gpch->chip.label);
		return ret;
	}
	printk(KERN_INFO "%s: registered %d GPIOs\n",
				gpch->chip.label, gpch->chip.ngpio);

	if (ar7_is_titan())
		titan_gpio_init();

	return ret;
}
