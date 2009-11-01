/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2008 Maxime Bizon <mbizon@freebox.fr>
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <bcm63xx_cpu.h>
#include <bcm63xx_dev_uart.h>

static struct resource uart_resources[] = {
	{
		.start		= -1, /* filled at runtime */
		.end		= -1, /* filled at runtime */
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= -1, /* filled at runtime */
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device bcm63xx_uart_device = {
	.name		= "bcm63xx_uart",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(uart_resources),
	.resource	= uart_resources,
};

int __init bcm63xx_uart_register(void)
{
	uart_resources[0].start = bcm63xx_regset_address(RSET_UART0);
	uart_resources[0].end = uart_resources[0].start;
	uart_resources[0].end += RSET_UART_SIZE - 1;
	uart_resources[1].start = bcm63xx_get_irq_number(IRQ_UART0);
	return platform_device_register(&bcm63xx_uart_device);
}
