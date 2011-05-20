/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/527x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5270/5271 CPUs.
 *
 *	Copyright (C) 1999-2004, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2004, SnapGear Inc. (www.snapgear.com)
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

static struct mcf_platform_uart m527x_uart_platform[] = {
	{
		.mapbase	= MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART1,
	},
	{
		.mapbase 	= MCFUART_BASE3,
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
		.start		= MCFFEC_BASE0,
		.end		= MCFFEC_BASE0 + MCFFEC_SIZE0 - 1,
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
		.start		= MCFFEC_BASE1,
		.end		= MCFFEC_BASE1 + MCFFEC_SIZE1 - 1,
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

#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
static struct resource m527x_qspi_resources[] = {
	{
		.start		= MCFQSPI_IOBASE,
		.end		= MCFQSPI_IOBASE + MCFQSPI_IOSIZE - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCFINT_VECBASE + MCFINT_QSPI,
		.end		= MCFINT_VECBASE + MCFINT_QSPI,
		.flags		= IORESOURCE_IRQ,
	},
};

#if defined(CONFIG_M5271)
#define MCFQSPI_CS0    91
#define MCFQSPI_CS1    92
#define MCFQSPI_CS2    99
#define MCFQSPI_CS3    103
#elif defined(CONFIG_M5275)
#define MCFQSPI_CS0    59
#define MCFQSPI_CS1    60
#define MCFQSPI_CS2    61
#define MCFQSPI_CS3    62
#endif

static int m527x_cs_setup(struct mcfqspi_cs_control *cs_control)
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

static void m527x_cs_teardown(struct mcfqspi_cs_control *cs_control)
{
	gpio_free(MCFQSPI_CS3);
	gpio_free(MCFQSPI_CS2);
	gpio_free(MCFQSPI_CS1);
	gpio_free(MCFQSPI_CS0);
}

static void m527x_cs_select(struct mcfqspi_cs_control *cs_control,
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

static void m527x_cs_deselect(struct mcfqspi_cs_control *cs_control,
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

static struct mcfqspi_cs_control m527x_cs_control = {
	.setup                  = m527x_cs_setup,
	.teardown               = m527x_cs_teardown,
	.select                 = m527x_cs_select,
	.deselect               = m527x_cs_deselect,
};

static struct mcfqspi_platform_data m527x_qspi_data = {
	.bus_num		= 0,
	.num_chipselect		= 4,
	.cs_control		= &m527x_cs_control,
};

static struct platform_device m527x_qspi = {
	.name			= "mcfqspi",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m527x_qspi_resources),
	.resource		= m527x_qspi_resources,
	.dev.platform_data	= &m527x_qspi_data,
};

static void __init m527x_qspi_init(void)
{
#if defined(CONFIG_M5271)
	u16 par;

	/* setup QSPS pins for QSPI with gpio CS control */
	writeb(0x1f, MCFGPIO_PAR_QSPI);
	/* and CS2 & CS3 as gpio */
	par = readw(MCFGPIO_PAR_TIMER);
	par &= 0x3f3f;
	writew(par, MCFGPIO_PAR_TIMER);
#elif defined(CONFIG_M5275)
	/* setup QSPS pins for QSPI with gpio CS control */
	writew(0x003e, MCFGPIO_PAR_QSPI);
#endif
}
#endif /* defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE) */

static struct platform_device *m527x_devices[] __initdata = {
	&m527x_uart,
	&m527x_fec[0],
#ifdef CONFIG_FEC2
	&m527x_fec[1],
#endif
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	&m527x_qspi,
#endif
};

/***************************************************************************/

static void __init m527x_uart_init_line(int line, int irq)
{
	u16 sepmask;

	if ((line < 0) || (line > 2))
		return;

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

static void __init m527x_fec_init(void)
{
	u16 par;
	u8 v;

	/* Set multi-function pins to ethernet mode for fec0 */
#if defined(CONFIG_M5271)
	v = readb(MCF_IPSBAR + 0x100047);
	writeb(v | 0xf0, MCF_IPSBAR + 0x100047);
#else
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xf00, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100078);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100078);
#endif

#ifdef CONFIG_FEC2
	/* Set multi-function pins to ethernet mode for fec1 */
	par = readw(MCF_IPSBAR + 0x100082);
	writew(par | 0xa0, MCF_IPSBAR + 0x100082);
	v = readb(MCF_IPSBAR + 0x100079);
	writeb(v | 0xc0, MCF_IPSBAR + 0x100079);
#endif
}

/***************************************************************************/

static void m527x_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_IPSBAR + MCF_RCR);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = m527x_cpu_reset;
	m527x_uarts_init();
	m527x_fec_init();
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	m527x_qspi_init();
#endif
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m527x_devices, ARRAY_SIZE(m527x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
