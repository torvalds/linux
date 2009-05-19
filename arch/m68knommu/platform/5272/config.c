/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5272/config.c
 *
 *	Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2002, SnapGear Inc. (www.snapgear.com)
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

/*
 *	Some platforms need software versions of the GPIO data registers.
 */
unsigned short ppdata;
unsigned char ledbank = 0xff;

/***************************************************************************/

static struct mcf_platform_uart m5272_uart_platform[] = {
	{
		.mapbase	= MCF_MBAR + MCFUART_BASE1,
		.irq		= 73,
	},
	{
		.mapbase 	= MCF_MBAR + MCFUART_BASE2,
		.irq		= 74,
	},
	{ },
};

static struct platform_device m5272_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m5272_uart_platform,
};

static struct resource m5272_fec_resources[] = {
	{
		.start		= MCF_MBAR + 0x840,
		.end		= MCF_MBAR + 0x840 + 0x1cf,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 86,
		.end		= 86,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 87,
		.end		= 87,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 88,
		.end		= 88,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m5272_fec = {
	.name			= "fec",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m5272_fec_resources),
	.resource		= m5272_fec_resources,
};

static struct platform_device *m5272_devices[] __initdata = {
	&m5272_uart,
	&m5272_fec,
};

/***************************************************************************/

static void __init m5272_uart_init_line(int line, int irq)
{
	u32 v;

	if ((line >= 0) && (line < 2)) {
		v = (line) ? 0x0e000000 : 0xe0000000;
		writel(v, MCF_MBAR + MCFSIM_ICR2);

		/* Enable the output lines for the serial ports */
		v = readl(MCF_MBAR + MCFSIM_PBCNT);
		v = (v & ~0x000000ff) | 0x00000055;
		writel(v, MCF_MBAR + MCFSIM_PBCNT);

		v = readl(MCF_MBAR + MCFSIM_PDCNT);
		v = (v & ~0x000003fc) | 0x000002a8;
		writel(v, MCF_MBAR + MCFSIM_PDCNT);
	}
}

static void __init m5272_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m5272_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m5272_uart_init_line(line, m5272_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m5272_fec_init(void)
{
	u32 imr;

	/* Unmask FEC interrupts at ColdFire interrupt controller */
	imr = readl(MCF_MBAR + MCFSIM_ICR3);
	imr = (imr & ~0x00000fff) | 0x00000ddd;
	writel(imr, MCF_MBAR + MCFSIM_ICR3);

	imr = readl(MCF_MBAR + MCFSIM_ICR1);
	imr = (imr & ~0x0f000000) | 0x0d000000;
	writel(imr, MCF_MBAR + MCFSIM_ICR1);
}

/***************************************************************************/

void mcf_disableall(void)
{
	volatile unsigned long	*icrp;

	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR1);
	icrp[0] = 0x88888888;
	icrp[1] = 0x88888888;
	icrp[2] = 0x88888888;
	icrp[3] = 0x88888888;
}

/***************************************************************************/

static void __init m5272_timers_init(void)
{
	/* Timer1 @ level6 is always used as system timer */
	writel((0x8 | 0x6) << ((4 - 1) * 4), MCF_MBAR + MCFSIM_ICR1);

#ifdef CONFIG_HIGHPROFILE
	/* Timer2 @ level7 is to be used as a high speed profile timer  */
	writel((0x8 | 0x7) << ((4 - 2) * 4), MCF_MBAR + MCFSIM_ICR1);
#endif
}

/***************************************************************************/

static void m5272_cpu_reset(void)
{
	local_irq_disable();
	/* Set watchdog to reset, and enabled */
	__raw_writew(0, MCF_MBAR + MCFSIM_WIRR);
	__raw_writew(1, MCF_MBAR + MCFSIM_WRRR);
	__raw_writew(0, MCF_MBAR + MCFSIM_WCR);
	for (;;)
		/* wait for watchdog to timeout */;
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
#if defined (CONFIG_MOD5272)
	volatile unsigned char	*pivrp;

	/* Set base of device vectors to be 64 */
	pivrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_PIVR);
	*pivrp = 0x40;
#endif

	mcf_disableall();

#if defined(CONFIG_NETtel) || defined(CONFIG_SCALES)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0004000, size);
	commandp[size-1] = 0;
#elif defined(CONFIG_CANCam)
	/* Copy command line from FLASH to local buffer... */
	memcpy(commandp, (char *) 0xf0010000, size);
	commandp[size-1] = 0;
#endif

	mach_reset = m5272_cpu_reset;
	m5272_timers_init();
}

/***************************************************************************/

static int __init init_BSP(void)
{
	m5272_uarts_init();
	m5272_fec_init();
	platform_add_devices(m5272_devices, ARRAY_SIZE(m5272_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
