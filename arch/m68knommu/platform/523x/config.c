/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/523x/config.c
 *
 *	Sub-architcture dependant initialization code for the Freescale
 *	523x CPUs.
 *
 *	Copyright (C) 1999-2005, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

/***************************************************************************/

static struct mcf_platform_uart m523x_uart_platform[] = {
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

static struct platform_device m523x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m523x_uart_platform,
};

static struct resource m523x_fec_resources[] = {
	{
		.start		= MCF_MBAR + 0x1000,
		.end		= MCF_MBAR + 0x1000 + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 64 + 23,
		.end		= 64 + 23,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 27,
		.end		= 64 + 27,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 29,
		.end		= 64 + 29,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m523x_fec = {
	.name			= "fec",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m523x_fec_resources),
	.resource		= m523x_fec_resources,
};

static struct platform_device *m523x_devices[] __initdata = {
	&m523x_uart,
	&m523x_fec,
};

/***************************************************************************/

#define	INTC0	(MCF_MBAR + MCFICM_INTC0)

static void __init m523x_uart_init_line(int line, int irq)
{
	u32 imr;

	if ((line < 0) || (line > 2))
		return;

	writeb(0x30+line, (INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + line));

	imr = readl(INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (irq - MCFINT_VECBASE)) | 1);
	writel(imr, INTC0 + MCFINTC_IMRL);
}

static void __init m523x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m523x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m523x_uart_init_line(line, m523x_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m523x_fec_init(void)
{
	u32 imr;

	/* Unmask FEC interrupts at ColdFire interrupt controller */
	writeb(0x28, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 23);
	writeb(0x27, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 27);
	writeb(0x26, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_ICR0 + 29);

	imr = readl(MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRH);
	imr &= ~0xf;
	writel(imr, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRH);
	imr = readl(MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL);
	imr &= ~0xff800001;
	writel(imr, MCF_IPSBAR + MCFICM_INTC0 + MCFINTC_IMRL);
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
	/* Everything is auto-vectored on the 523x */
}
/***************************************************************************/

static void m523x_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_IPSBAR + MCF_RCR);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mcf_disableall();
	mach_reset = m523x_cpu_reset;
	m523x_uarts_init();
	m523x_fec_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m523x_devices, ARRAY_SIZE(m523x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
