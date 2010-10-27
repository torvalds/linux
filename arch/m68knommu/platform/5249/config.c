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
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfqspi.h>

/***************************************************************************/

static struct mcf_platform_uart m5249_uart_platform[] = {
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

static struct platform_device m5249_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m5249_uart_platform,
};

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

#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
static struct resource m5249_qspi_resources[] = {
	{
		.start		= MCFQSPI_IOBASE,
		.end		= MCFQSPI_IOBASE + MCFQSPI_IOSIZE - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCF_IRQ_QSPI,
		.end		= MCF_IRQ_QSPI,
		.flags		= IORESOURCE_IRQ,
	},
};

#define MCFQSPI_CS0    29
#define MCFQSPI_CS1    24
#define MCFQSPI_CS2    21
#define MCFQSPI_CS3    22

static int m5249_cs_setup(struct mcfqspi_cs_control *cs_control)
{
	int status;

	status = gpio_request(MCFQSPI_CS0, "MCFQSPI_CS0");
	if (status) {
		pr_debug("gpio_request for MCFQSPI_CS0 failed\n");
		goto fail0;
	}
	status = gpio_direction_output(MCFQSPI_CS0, 1);
	if (status) {
		pr_debug("gpio_direction_output for MCFQSPI_CS0 failed\n");
		goto fail1;
	}

	status = gpio_request(MCFQSPI_CS1, "MCFQSPI_CS1");
	if (status) {
		pr_debug("gpio_request for MCFQSPI_CS1 failed\n");
		goto fail1;
	}
	status = gpio_direction_output(MCFQSPI_CS1, 1);
	if (status) {
		pr_debug("gpio_direction_output for MCFQSPI_CS1 failed\n");
		goto fail2;
	}

	status = gpio_request(MCFQSPI_CS2, "MCFQSPI_CS2");
	if (status) {
		pr_debug("gpio_request for MCFQSPI_CS2 failed\n");
		goto fail2;
	}
	status = gpio_direction_output(MCFQSPI_CS2, 1);
	if (status) {
		pr_debug("gpio_direction_output for MCFQSPI_CS2 failed\n");
		goto fail3;
	}

	status = gpio_request(MCFQSPI_CS3, "MCFQSPI_CS3");
	if (status) {
		pr_debug("gpio_request for MCFQSPI_CS3 failed\n");
		goto fail3;
	}
	status = gpio_direction_output(MCFQSPI_CS3, 1);
	if (status) {
		pr_debug("gpio_direction_output for MCFQSPI_CS3 failed\n");
		goto fail4;
	}

	return 0;

fail4:
	gpio_free(MCFQSPI_CS3);
fail3:
	gpio_free(MCFQSPI_CS2);
fail2:
	gpio_free(MCFQSPI_CS1);
fail1:
	gpio_free(MCFQSPI_CS0);
fail0:
	return status;
}

static void m5249_cs_teardown(struct mcfqspi_cs_control *cs_control)
{
	gpio_free(MCFQSPI_CS3);
	gpio_free(MCFQSPI_CS2);
	gpio_free(MCFQSPI_CS1);
	gpio_free(MCFQSPI_CS0);
}

static void m5249_cs_select(struct mcfqspi_cs_control *cs_control,
			    u8 chip_select, bool cs_high)
{
	switch (chip_select) {
	case 0:
		gpio_set_value(MCFQSPI_CS0, cs_high);
		break;
	case 1:
		gpio_set_value(MCFQSPI_CS1, cs_high);
		break;
	case 2:
		gpio_set_value(MCFQSPI_CS2, cs_high);
		break;
	case 3:
		gpio_set_value(MCFQSPI_CS3, cs_high);
		break;
	}
}

static void m5249_cs_deselect(struct mcfqspi_cs_control *cs_control,
			      u8 chip_select, bool cs_high)
{
	switch (chip_select) {
	case 0:
		gpio_set_value(MCFQSPI_CS0, !cs_high);
		break;
	case 1:
		gpio_set_value(MCFQSPI_CS1, !cs_high);
		break;
	case 2:
		gpio_set_value(MCFQSPI_CS2, !cs_high);
		break;
	case 3:
		gpio_set_value(MCFQSPI_CS3, !cs_high);
		break;
	}
}

static struct mcfqspi_cs_control m5249_cs_control = {
	.setup                  = m5249_cs_setup,
	.teardown               = m5249_cs_teardown,
	.select                 = m5249_cs_select,
	.deselect               = m5249_cs_deselect,
};

static struct mcfqspi_platform_data m5249_qspi_data = {
	.bus_num		= 0,
	.num_chipselect		= 4,
	.cs_control		= &m5249_cs_control,
};

static struct platform_device m5249_qspi = {
	.name			= "mcfqspi",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m5249_qspi_resources),
	.resource		= m5249_qspi_resources,
	.dev.platform_data	= &m5249_qspi_data,
};

static void __init m5249_qspi_init(void)
{
	/* QSPI irq setup */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL4 | MCFSIM_ICR_PRI0,
	       MCF_MBAR + MCFSIM_QSPIICR);
	mcf_mapirq2imr(MCF_IRQ_QSPI, MCFINTC_QSPI);
}
#endif /* defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE) */


static struct platform_device *m5249_devices[] __initdata = {
	&m5249_uart,
#ifdef CONFIG_M5249C3
	&m5249_smc91x,
#endif
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	&m5249_qspi,
#endif
};

/***************************************************************************/

static void __init m5249_uart_init_line(int line, int irq)
{
	if (line == 0) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCF_MBAR + MCFSIM_UART1ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE1 + MCFUART_UIVR);
		mcf_mapirq2imr(irq, MCFINTC_UART0);
	} else if (line == 1) {
		writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCF_MBAR + MCFSIM_UART2ICR);
		writeb(irq, MCF_MBAR + MCFUART_BASE2 + MCFUART_UIVR);
		mcf_mapirq2imr(irq, MCFINTC_UART1);
	}
}

static void __init m5249_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m5249_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m5249_uart_init_line(line, m5249_uart_platform[line].irq);
}

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

static void __init m5249_timers_init(void)
{
	/* Timer1 is always used as system timer */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI3,
		MCF_MBAR + MCFSIM_TIMER1ICR);
	mcf_mapirq2imr(MCF_IRQ_TIMER, MCFINTC_TIMER1);

#ifdef CONFIG_HIGHPROFILE
	/* Timer2 is to be used as a high speed profile timer  */
	writeb(MCFSIM_ICR_AUTOVEC | MCFSIM_ICR_LEVEL7 | MCFSIM_ICR_PRI3,
		MCF_MBAR + MCFSIM_TIMER2ICR);
	mcf_mapirq2imr(MCF_IRQ_PROFILER, MCFINTC_TIMER2);
#endif
}

/***************************************************************************/

void m5249_cpu_reset(void)
{
	local_irq_disable();
	/* Set watchdog to soft reset, and enabled */
	__raw_writeb(0xc0, MCF_MBAR + MCFSIM_SYPCR);
	for (;;)
		/* wait for watchdog to timeout */;
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = m5249_cpu_reset;
	m5249_timers_init();
	m5249_uarts_init();
#ifdef CONFIG_M5249C3
	m5249_smc91x_init();
#endif
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
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
