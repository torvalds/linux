/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/527x/config.c
 *
 *	Sub-architcture dependant initialization code for the Freescale
 *	5270/5271 CPUs.
 *
 *	Copyright (C) 1999-2004, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>

/***************************************************************************/

void coldfire_reset(void);

/***************************************************************************/

static struct mcf_platform_uart m527x_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART1,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART2,
	},
	{ },
};

static struct platform_device m527x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m527x_uart_platform,
};

static struct resource m527x_fec0_resources[] = {
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

static struct resource m527x_fec1_resources[] = {
	{
		.start		= MCF_MBAR + 0x1800,
		.end		= MCF_MBAR + 0x1800 + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 128 + 23,
		.end		= 128 + 23,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 128 + 27,
		.end		= 128 + 27,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 128 + 29,
		.end		= 128 + 29,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m527x_fec[] = {
	{
		.name		= "fec",
		.id		= 0,
		.num_resources	= ARRAY_SIZE(m527x_fec0_resources),
		.resource	= m527x_fec0_resources,
	},
	{
		.name		= "fec",
		.id		= 1,
		.num_resources	= ARRAY_SIZE(m527x_fec1_resources),
		.resource	= m527x_fec1_resources,
	},
};

static struct platform_device *m527x_devices[] __initdata = {
	&m527x_uart,
	&m527x_fec[0],
#ifdef CONFIG_FEC2
	&m527x_fec[1],
#endif
};

/***************************************************************************/

#define	INTC0	(MCF_MBAR + MCFICM_INTC0)

static void __init m527x_uart_init_line(int line, int irq)
{
	u16 sepmask;
	u32 imr;

	if ((line < 0) || (line > 2))
		return;

	/* level 6, line based priority */
	writeb(0x30+line, INTC0 + MCFINTC_ICR0 + MCFINT_UART0 + line);

	imr = readl(INTC0 + MCFINTC_IMRL);
	imr &= ~((1 << (irq - MCFINT_VECBASE)) | 1);
	writel(imr, INTC0 + MCFINTC_IMRL);

	/*
	 * External Pin Mask Setting & Enable External Pin for Interface
	 */
	sepmask = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
	if (line == 0)
		sepmask |= UART0_ENABLE_MASK;
	else if (line == 1)
		sepmask |= UART1_ENABLE_MASK;
	else if (line == 2)
		sepmask |= UART2_ENABLE_MASK;
	writew(sepmask, MCF_IPSBAR + MCF_GPIO_PAR_UART);
}

static void __init m527x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m527x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m527x_uart_init_line(line, m527x_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m527x_fec_irq_init(int nr)
{
	unsigned long base;
	u32 imr;

	base = MCF_IPSBAR + (nr ? MCFICM_INTC1 : MCFICM_INTC0);

	writeb(0x28, base + MCFINTC_ICR0 + 23);
	writeb(0x27, base + MCFINTC_ICR0 + 27);
	writeb(0x26, base + MCFINTC_ICR0 + 29);

	imr = readl(base + MCFINTC_IMRH);
	imr &= ~0xf;
	writel(imr, base + MCFINTC_IMRH);
	imr = readl(base + MCFINTC_IMRL);
	imr &= ~0xff800001;
	writel(imr, base + MCFINTC_IMRL);
}

static void __init m527x_fec_init(void)
{
	u16 par;
	u8 v;

	m527x_fec_irq_init(0);

	/* Set multi-function pins to ethernet mode for fec0 */
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xf00, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100078);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100078);

#ifdef CONFIG_FEC2
	m527x_fec_irq_init(1);

	/* Set multi-function pins to ethernet mode for fec1 */
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xa0, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100079);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100079);
#endif
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
	m527x_uarts_init();
	m527x_fec_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m527x_devices, ARRAY_SIZE(m527x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
