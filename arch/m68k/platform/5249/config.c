/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/5249/config.c
 *
 *	Copyright (C) 2002, Greg Ungerer (gerg@snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>

/***************************************************************************/

#ifdef CONFIG_M5249C3

static struct resource m5249_smc91x_resources[] = {
	{
		.start		= 0xe0000300,
		.end		= 0xe0000300 + 0x100,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCFINTC2_GPIOIRQ6,
		.end		= MCFINTC2_GPIOIRQ6,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m5249_smc91x = {
	.name			= "smc91x",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m5249_smc91x_resources),
	.resource		= m5249_smc91x_resources,
};

#endif /* CONFIG_M5249C3 */

static struct platform_device *m5249_devices[] __initdata = {
#ifdef CONFIG_M5249C3
	&m5249_smc91x,
#endif
};

/***************************************************************************/

#ifdef CONFIG_SPI_COLDFIRE_QSPI

static void __init m5249_qspi_init(void)
{
	/* QSPI irq setup */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL4 | MCFSIM_ICR_PRI0,
	       MCF_MBAR + MCFSIM_QSPIICR);
	mcf_mapirq2imr(MCF_IRQ_QSPI, MCFINTC_QSPI);
}

#endif /* CONFIG_SPI_COLDFIRE_QSPI */

/***************************************************************************/

#ifdef CONFIG_M5249C3

static void __init m5249_smc91x_init(void)
{
	u32  gpio;

	/* Set the GPIO line as interrupt source for smc91x device */
	gpio = readl(MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);
	writel(gpio | 0x40, MCF_MBAR2 + MCFSIM2_GPIOINTENABLE);

	gpio = readl(MCF_MBAR2 + MCFSIM2_INTLEVEL5);
	writel(gpio | 0x04000000, MCF_MBAR2 + MCFSIM2_INTLEVEL5);
}

#endif /* CONFIG_M5249C3 */

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_sched_init = hw_timer_init;

#ifdef CONFIG_M5249C3
	m5249_smc91x_init();
#endif
#ifdef CONFIG_SPI_COLDFIRE_QSPI
	m5249_qspi_init();
#endif
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m5249_devices, ARRAY_SIZE(m5249_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
