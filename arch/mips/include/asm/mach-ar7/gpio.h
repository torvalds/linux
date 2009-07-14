/*
 * Copyright (C) 2007 Florian Fainelli <florian@openwrt.org>
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

#ifndef __AR7_GPIO_H__
#define __AR7_GPIO_H__

#include <asm/mach-ar7/ar7.h>

#define AR7_GPIO_MAX 32

extern int gpio_request(unsigned gpio, const char *label);
extern void gpio_free(unsigned gpio);

/* Common GPIO layer */
static inline int gpio_get_value(unsigned gpio)
{
	void __iomem *gpio_in =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_INPUT);

	return readl(gpio_in) & (1 << gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	void __iomem *gpio_out =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_OUTPUT);
	unsigned tmp;

	tmp = readl(gpio_out) & ~(1 << gpio);
	if (value)
		tmp |= 1 << gpio;
	writel(tmp, gpio_out);
}

static inline int gpio_direction_input(unsigned gpio)
{
	void __iomem *gpio_dir =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_DIR);

	if (gpio >= AR7_GPIO_MAX)
		return -EINVAL;

	writel(readl(gpio_dir) | (1 << gpio), gpio_dir);

	return 0;
}

static inline int gpio_direction_output(unsigned gpio, int value)
{
	void __iomem *gpio_dir =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_DIR);

	if (gpio >= AR7_GPIO_MAX)
		return -EINVAL;

	gpio_set_value(gpio, value);
	writel(readl(gpio_dir) & ~(1 << gpio), gpio_dir);

	return 0;
}

static inline int gpio_to_irq(unsigned gpio)
{
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned irq)
{
	return -EINVAL;
}

/* Board specific GPIO functions */
static inline int ar7_gpio_enable(unsigned gpio)
{
	void __iomem *gpio_en =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_ENABLE);

	writel(readl(gpio_en) | (1 << gpio), gpio_en);

	return 0;
}

static inline int ar7_gpio_disable(unsigned gpio)
{
	void __iomem *gpio_en =
		(void __iomem *)KSEG1ADDR(AR7_REGS_GPIO + AR7_GPIO_ENABLE);

	writel(readl(gpio_en) & ~(1 << gpio), gpio_en);

	return 0;
}

#include <asm-generic/gpio.h>

#endif
