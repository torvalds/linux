/*
 *  author: Sascha Hauer
 *  Created: april 20th, 2004
 *  Copyright: Synertronixx GmbH
 *
 *  Common code for i.MX1 machines
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
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include <asm/mach/map.h>

#include <mach/common.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/irqs.h>
#include <mach/iomux-v1.h>

static struct map_desc imx_io_desc[] __initdata = {
	imx_map_entry(MX1, IO, MT_DEVICE),
};

void __init mx1_map_io(void)
{
	iotable_init(imx_io_desc, ARRAY_SIZE(imx_io_desc));
}

void __init imx1_init_early(void)
{
	mxc_set_cpu_type(MXC_CPU_MX1);
	mxc_arch_reset_init(MX1_IO_ADDRESS(MX1_WDT_BASE_ADDR));
	imx_iomuxv1_init(MX1_IO_ADDRESS(MX1_GPIO_BASE_ADDR),
			MX1_NUM_GPIO_PORT);
}

static struct mxc_gpio_port imx1_gpio_ports[] = {
	DEFINE_IMX_GPIO_PORT_IRQ(MX1, 0, 1, MX1_GPIO_INT_PORTA),
	DEFINE_IMX_GPIO_PORT_IRQ(MX1, 1, 2, MX1_GPIO_INT_PORTB),
	DEFINE_IMX_GPIO_PORT_IRQ(MX1, 2, 3, MX1_GPIO_INT_PORTC),
	DEFINE_IMX_GPIO_PORT_IRQ(MX1, 3, 4, MX1_GPIO_INT_PORTD),
};

void __init mx1_init_irq(void)
{
	mxc_init_irq(MX1_IO_ADDRESS(MX1_AVIC_BASE_ADDR));
	mxc_gpio_init(imx1_gpio_ports,	ARRAY_SIZE(imx1_gpio_ports));
}
