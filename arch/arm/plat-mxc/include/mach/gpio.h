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


/* There's a off-by-one betweem the gpio bank number and the gpiochip */
/* range e.g. GPIO_1_5 is gpio 5 under linux */
#define IMX_GPIO_NR(bank, nr)		(((bank) - 1) * 32 + (nr))

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

#define DEFINE_IMX_GPIO_PORT_IRQ_HIGH(soc, _id, _hwid, _irq, _irq_high)	\
	{								\
		.chip.label = "gpio-" #_id,				\
		.irq = _irq,						\
		.irq_high = _irq_high,					\
		.base = soc ## _IO_ADDRESS(				\
				soc ## _GPIO ## _hwid ## _BASE_ADDR),	\
		.virtual_irq_start = MXC_GPIO_IRQ_START + (_id) * 32,	\
	}

#define DEFINE_IMX_GPIO_PORT_IRQ(soc, _id, _hwid, _irq)			\
	DEFINE_IMX_GPIO_PORT_IRQ_HIGH(soc, _id, _hwid, _irq, 0)
#define DEFINE_IMX_GPIO_PORT(soc, _id, _hwid)				\
	DEFINE_IMX_GPIO_PORT_IRQ(soc, _id, _hwid, 0)

int mxc_gpio_init(struct mxc_gpio_port*, int);

#endif
