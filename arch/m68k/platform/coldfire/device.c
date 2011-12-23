/*
 * device.c  -- common ColdFire SoC device support
 *
 * (C) Copyright 2011, Greg Ungerer <gerg@uclinux.org>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/traps.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

static struct mcf_platform_uart mcf_uart_platform_data[] = {
	{
		.mapbase	= MCFUART_BASE0,
		.irq		= MCF_IRQ_UART0,
	},
	{
		.mapbase	= MCFUART_BASE1,
		.irq		= MCF_IRQ_UART1,
	},
#ifdef MCFUART_BASE2
	{
		.mapbase	= MCFUART_BASE2,
		.irq		= MCF_IRQ_UART2,
	},
#endif
#ifdef MCFUART_BASE3
	{
		.mapbase	= MCFUART_BASE3,
		.irq		= MCF_IRQ_UART3,
	},
#endif
	{ },
};

static struct platform_device mcf_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= mcf_uart_platform_data,
};

static struct platform_device *mcf_devices[] __initdata = {
	&mcf_uart,
};

static int __init mcf_init_devices(void)
{
	platform_add_devices(mcf_devices, ARRAY_SIZE(mcf_devices));
	return 0;
}

arch_initcall(mcf_init_devices);

