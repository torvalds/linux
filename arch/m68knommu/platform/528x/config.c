/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/528x/config.c
 *
 *	Sub-architcture dependant initialization code for the Motorola
 *	5280 and 5282 CPUs.
 *
 *	Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfqspi.h>

/***************************************************************************/

void coldfire_reset(void);

/***************************************************************************/

static struct mcf_platform_uart m528x_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 1,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 2,
	},
	{ },
};

static struct platform_device m528x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m528x_uart_platform,
};

static struct platform_device *m528x_devices[] __initdata = {
	&m528x_uart,
};

/***************************************************************************/

#define	INTC0	(MCF_MBAR + MCFICM_INTC0)

static void __init m528x_uart_init_line(int line, int irq)
{
	u8 port;
	u32 imr;

	if ((line < 0) || (line > 2))
		return;

	/* level 6, line based priority */
	writeb(0x30+line, INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + line);

	imr = readl(INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (irq - MCFINT_VECBASE)) | 1);
	writel(imr, INTC0 + MCFINTC_IMRL);

	/* make sure PUAPAR is set for UART0 and UART1 */
	if (line < 2) {
		port = readb(MCF_MBAR + MCF5282_GPIO_PUAPAR);
		port |= (0x03 << (line * 2));
		writeb(port, MCF_MBAR + MCF5282_GPIO_PUAPAR);
	}
}

static void __init m528x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m528x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m528x_uart_init_line(line, m528x_uart_platform[line].irq);
}

/***************************************************************************/

void mcf_disableall(void)
{
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRH)) = 0xffffffff;
	*((volatile unsigned long *) (MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL)) = 0xffffffff;
}

/***************************************************************************/

void mcf_autovector(unsigned int vec)
{
	/* Everything is auto-vectored on the 5272 */
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mcf_disableall();
	mach_reset = coldfire_reset;
}

/***************************************************************************/

static int __init init_BSP(void)
{
	m528x_uarts_init();
	platform_add_devices(m528x_devices, ARRAY_SIZE(m528x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
