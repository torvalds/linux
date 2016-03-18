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
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/fec.h>
#include <asm/traps.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfqspi.h>

/*
 *	All current ColdFire parts contain from 2, 3, 4 or 10 UARTS.
 */
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
#ifdef MCFUART_BASE4
	{
		.mapbase	= MCFUART_BASE4,
		.irq		= MCF_IRQ_UART4,
	},
#endif
#ifdef MCFUART_BASE5
	{
		.mapbase	= MCFUART_BASE5,
		.irq		= MCF_IRQ_UART5,
	},
#endif
#ifdef MCFUART_BASE6
	{
		.mapbase	= MCFUART_BASE6,
		.irq		= MCF_IRQ_UART6,
	},
#endif
#ifdef MCFUART_BASE7
	{
		.mapbase	= MCFUART_BASE7,
		.irq		= MCF_IRQ_UART7,
	},
#endif
#ifdef MCFUART_BASE8
	{
		.mapbase	= MCFUART_BASE8,
		.irq		= MCF_IRQ_UART8,
	},
#endif
#ifdef MCFUART_BASE9
	{
		.mapbase	= MCFUART_BASE9,
		.irq		= MCF_IRQ_UART9,
	},
#endif
	{ },
};

static struct platform_device mcf_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= mcf_uart_platform_data,
};

#if IS_ENABLED(CONFIG_FEC)

#ifdef CONFIG_M5441x
#define FEC_NAME	"enet-fec"
static struct fec_platform_data fec_pdata = {
	.phy		= PHY_INTERFACE_MODE_RMII,
};
#define FEC_PDATA	(&fec_pdata)
#else
#define FEC_NAME	"fec"
#define FEC_PDATA	NULL
#endif

/*
 *	Some ColdFire cores contain the Fast Ethernet Controller (FEC)
 *	block. It is Freescale's own hardware block. Some ColdFires
 *	have 2 of these.
 */
static struct resource mcf_fec0_resources[] = {
	{
		.start		= MCFFEC_BASE0,
		.end		= MCFFEC_BASE0 + MCFFEC_SIZE0 - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCF_IRQ_FECRX0,
		.end		= MCF_IRQ_FECRX0,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= MCF_IRQ_FECTX0,
		.end		= MCF_IRQ_FECTX0,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= MCF_IRQ_FECENTC0,
		.end		= MCF_IRQ_FECENTC0,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mcf_fec0 = {
	.name			= FEC_NAME,
	.id			= 0,
	.num_resources		= ARRAY_SIZE(mcf_fec0_resources),
	.resource		= mcf_fec0_resources,
	.dev.platform_data	= FEC_PDATA,
};

#ifdef MCFFEC_BASE1
static struct resource mcf_fec1_resources[] = {
	{
		.start		= MCFFEC_BASE1,
		.end		= MCFFEC_BASE1 + MCFFEC_SIZE1 - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCF_IRQ_FECRX1,
		.end		= MCF_IRQ_FECRX1,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= MCF_IRQ_FECTX1,
		.end		= MCF_IRQ_FECTX1,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= MCF_IRQ_FECENTC1,
		.end		= MCF_IRQ_FECENTC1,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device mcf_fec1 = {
	.name			= FEC_NAME,
	.id			= 1,
	.num_resources		= ARRAY_SIZE(mcf_fec1_resources),
	.resource		= mcf_fec1_resources,
	.dev.platform_data	= FEC_PDATA,
};
#endif /* MCFFEC_BASE1 */
#endif /* CONFIG_FEC */

#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
/*
 *	The ColdFire QSPI module is an SPI protocol hardware block used
 *	on a number of different ColdFire CPUs.
 */
static struct resource mcf_qspi_resources[] = {
	{
		.start		= MCFQSPI_BASE,
		.end		= MCFQSPI_BASE + MCFQSPI_SIZE - 1,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= MCF_IRQ_QSPI,
		.end		= MCF_IRQ_QSPI,
		.flags		= IORESOURCE_IRQ,
	},
};

static int mcf_cs_setup(struct mcfqspi_cs_control *cs_control)
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

#ifdef MCFQSPI_CS3
	status = gpio_request(MCFQSPI_CS3, "MCFQSPI_CS3");
	if (status) {
		pr_debug("gpio_request for MCFQSPI_CS3 failed\n");
		goto fail3;
	}
	status = gpio_direction_output(MCFQSPI_CS3, 1);
	if (status) {
		pr_debug("gpio_direction_output for MCFQSPI_CS3 failed\n");
		gpio_free(MCFQSPI_CS3);
		goto fail3;
	}
#endif

	return 0;

fail3:
	gpio_free(MCFQSPI_CS2);
fail2:
	gpio_free(MCFQSPI_CS1);
fail1:
	gpio_free(MCFQSPI_CS0);
fail0:
	return status;
}

static void mcf_cs_teardown(struct mcfqspi_cs_control *cs_control)
{
#ifdef MCFQSPI_CS3
	gpio_free(MCFQSPI_CS3);
#endif
	gpio_free(MCFQSPI_CS2);
	gpio_free(MCFQSPI_CS1);
	gpio_free(MCFQSPI_CS0);
}

static void mcf_cs_select(struct mcfqspi_cs_control *cs_control,
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
#ifdef MCFQSPI_CS3
	case 3:
		gpio_set_value(MCFQSPI_CS3, cs_high);
		break;
#endif
	}
}

static void mcf_cs_deselect(struct mcfqspi_cs_control *cs_control,
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
#ifdef MCFQSPI_CS3
	case 3:
		gpio_set_value(MCFQSPI_CS3, !cs_high);
		break;
#endif
	}
}

static struct mcfqspi_cs_control mcf_cs_control = {
	.setup			= mcf_cs_setup,
	.teardown		= mcf_cs_teardown,
	.select			= mcf_cs_select,
	.deselect		= mcf_cs_deselect,
};

static struct mcfqspi_platform_data mcf_qspi_data = {
	.bus_num		= 0,
	.num_chipselect		= 4,
	.cs_control		= &mcf_cs_control,
};

static struct platform_device mcf_qspi = {
	.name			= "mcfqspi",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(mcf_qspi_resources),
	.resource		= mcf_qspi_resources,
	.dev.platform_data	= &mcf_qspi_data,
};
#endif /* IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI) */

static struct platform_device *mcf_devices[] __initdata = {
	&mcf_uart,
#if IS_ENABLED(CONFIG_FEC)
	&mcf_fec0,
#ifdef MCFFEC_BASE1
	&mcf_fec1,
#endif
#endif
#if IS_ENABLED(CONFIG_SPI_COLDFIRE_QSPI)
	&mcf_qspi,
#endif
};

/*
 *	Some ColdFire UARTs let you set the IRQ line to use.
 */
static void __init mcf_uart_set_irq(void)
{
#ifdef MCFUART_UIVR
	/* UART0 interrupt setup */
	writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI1, MCFSIM_UART1ICR);
	writeb(MCF_IRQ_UART0, MCFUART_BASE0 + MCFUART_UIVR);
	mcf_mapirq2imr(MCF_IRQ_UART0, MCFINTC_UART0);

	/* UART1 interrupt setup */
	writeb(MCFSIM_ICR_LEVEL6 | MCFSIM_ICR_PRI2, MCFSIM_UART2ICR);
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

