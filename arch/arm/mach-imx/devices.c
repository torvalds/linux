/*
 * Author: MontaVista Software, Inc.
 *       <source@mvista.com>
 *
 * Based on the OMAP devices.c
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under the
 * terms of the GNU General Public License version 2. This program is
 * licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 * Copyright (c) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 * Copyright (c) 2008 Darius Augulis <darius.augulis@teltonika.lt>
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/dma-mapping.h>
#include <linux/serial.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/common.h>
#include <mach/mmc.h>

#if defined(CONFIG_ARCH_MX1)
/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR),
		.irq = MX1_GPIO_INT_PORTA,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x100),
		.irq = MX1_GPIO_INT_PORTB,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x200),
		.irq = MX1_GPIO_INT_PORTC,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR + 0x300),
		.irq = MX1_GPIO_INT_PORTD,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}
};

int __init imx1_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}
#endif

#if defined(CONFIG_MACH_MX21) || defined(CONFIG_MACH_MX27)
/* GPIO port description */
#define DEFINE_MXC_GPIO_PORT_IRQ(SOC, n, _irq)				\
	{								\
		.chip.label = "gpio-" #n,				\
		.irq = _irq,						\
		.base = SOC ## _IO_ADDRESS(MX2x_GPIO_BASE_ADDR +	\
				n * 0x100),				\
		.virtual_irq_start = MXC_GPIO_IRQ_START + n * 32,	\
	}

#define DEFINE_MXC_GPIO_PORT(SOC, n)					\
	{								\
		.chip.label = "gpio-" #n,				\
		.base = SOC ## _IO_ADDRESS(MX2x_GPIO_BASE_ADDR +	\
				n * 0x100),				\
		.virtual_irq_start = MXC_GPIO_IRQ_START + n * 32,	\
	}

#define DEFINE_MXC_GPIO_PORTS(SOC, pfx)					\
	static struct mxc_gpio_port pfx ## _gpio_ports[] = {		\
		DEFINE_MXC_GPIO_PORT_IRQ(SOC, 0, SOC ## _INT_GPIO),	\
		DEFINE_MXC_GPIO_PORT(SOC, 1),				\
		DEFINE_MXC_GPIO_PORT(SOC, 2),				\
		DEFINE_MXC_GPIO_PORT(SOC, 3),				\
		DEFINE_MXC_GPIO_PORT(SOC, 4),				\
		DEFINE_MXC_GPIO_PORT(SOC, 5),				\
	}

#ifdef CONFIG_MACH_MX21
DEFINE_MXC_GPIO_PORTS(MX21, imx21);

int __init imx21_register_gpios(void)
{
	return mxc_gpio_init(imx21_gpio_ports, ARRAY_SIZE(imx21_gpio_ports));
}
#endif

#ifdef CONFIG_MACH_MX27
DEFINE_MXC_GPIO_PORTS(MX27, imx27);

int __init imx27_register_gpios(void)
{
	return mxc_gpio_init(imx27_gpio_ports, ARRAY_SIZE(imx27_gpio_ports));
}
#endif

#endif
