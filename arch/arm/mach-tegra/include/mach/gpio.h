/*
 * arch/arm/mach-tegra/include/mach/gpio.h
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Erik Gilling <konkers@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __MACH_TEGRA_GPIO_H
#define __MACH_TEGRA_GPIO_H

#include <mach/irqs.h>

#define TEGRA_NR_GPIOS		INT_GPIO_NR

#include <asm-generic/gpio.h>

#define gpio_get_value		__gpio_get_value
#define gpio_set_value		__gpio_set_value
#define gpio_cansleep		__gpio_cansleep

#define TEGRA_GPIO_TO_IRQ(gpio) (INT_GPIO_BASE + (gpio))
#define TEGRA_IRQ_TO_GPIO(irq) ((irq) - INT_GPIO_BASE)

static inline int gpio_to_irq(unsigned int gpio)
{
	if (gpio < TEGRA_NR_GPIOS)
		return INT_GPIO_BASE + gpio;
	return -EINVAL;
}

static inline int irq_to_gpio(unsigned int irq)
{
	if ((irq >= INT_GPIO_BASE) && (irq < INT_GPIO_BASE + INT_GPIO_NR))
		return irq - INT_GPIO_BASE;
	return -EINVAL;
}

void tegra_gpio_enable(int gpio);
void tegra_gpio_disable(int gpio);

#endif
