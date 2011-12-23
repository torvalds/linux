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


/*
 *	Some ColdFire UARTs let you set the IRQ line to use.
 */
static void __init mcf_uart_set_irq(void)
{
#ifdef MCFUART_UIVR
	/* UART0 interrupt setup */
	writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
	writeb(MCF_IRQ_UART0, MCFUART_BASE0 + MCFUART_UIVR);
	mcf_mapirq2imr(MCF_IRQ_UART0, MCFINTC_UART0);

	/* UART1 interrupt setup */
	writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
	writeb(MCF_IRQ_UART1, MCFUART_BASE1 + MCFUART_UIVR);
	mcf_mapirq2imr(MCF_IRQ_UART1, MCFINTC_UART1);
#endif
}

static int __init mcf_init_devices(void)
{
	mcf_uart_set_irq();
	platform_add_devices(mcf_devices, ARRAY_SIZE(mcf_devices));
	return 0;
}

arch_initcall(mcf_init_devices);

