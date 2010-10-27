/*
 * Copyright 2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ASM_ARCH_MXC_GPIO_H__
#define __ASM_ARCH_MXC_GPIO_H__

#include <linux/spinlock.h>
#include <mach/hardware.h>
#include <asm-generic/gpio.h>

/* use gpiolib dispatchers */
#define gpio_get_value		__gpio_get_value
#define gpio_set_value		__gpio_set_value
#define gpio_cansleep		__gpio_cansleep

#define gpio_to_irq(gpio)	(MXC_GPIO_IRQ_START + (gpio))
#define irq_to_gpio(irq)	((irq) - MXC_GPIO_IRQ_START)

struct mxc_gpio_port {
	void __iomem *base;
	int irq;
	int irq_high;
	int virtual_irq_start;
	struct gpio_chip chip;
	u32 both_edges;
	spinlock_t lock;
};

int mxc_gpio_init(struct mxc_gpio_port*, int);

#endif
