/***************************************************************************/

/*
 *  linux/arch/m68knommu/platform/520x/config.c
 *
 *  Copyright (C) 2005,      Freescale (www.freescale.com)
 *  Copyright (C) 2005,      Intec Automation (mike@steroidmicros.com)
 *  Copyright (C) 1999-2007, Greg Ungerer (gerg@snapgear.com)
 *  Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
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

static struct mcf_platform_uart m520x_uart_platform[] = {
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

static struct platform_device m520x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m520x_uart_platform,
};

static struct resource m520x_fec_resources[] = {
	{
		.start		= MCF_MBAR + 0x30000,
		.end		= MCF_MBAR + 0x30000 + 0x7ff,
		.flags		= IORESOURCE_MEM,
	},
	{
		.start		= 64 + 36,
		.end		= 64 + 36,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 40,
		.end		= 64 + 40,
		.flags		= IORESOURCE_IRQ,
	},
	{
		.start		= 64 + 42,
		.end		= 64 + 42,
		.flags		= IORESOURCE_IRQ,
	},
};

static struct platform_device m520x_fec = {
	.name			= "fec",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m520x_fec_resources),
	.resource		= m520x_fec_resources,
};

#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
static struct resource m520x_qspi_resources[] = {
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

#define MCFQSPI_CS0    62
#define MCFQSPI_CS1    63
#define MCFQSPI_CS2    44

static int m520x_cs_setup(struct mcfqspi_cs_control *cs_control)
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

static void m520x_cs_teardown(struct mcfqspi_cs_control *cs_control)
{
	gpio_free(MCFQSPI_CS2);
	gpio_free(MCFQSPI_CS1);
	gpio_free(MCFQSPI_CS0);
}

static void m520x_cs_select(struct mcfqspi_cs_control *cs_control,
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
	}
}

static void m520x_cs_deselect(struct mcfqspi_cs_control *cs_control,
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
	}
}

static struct mcfqspi_cs_control m520x_cs_control = {
	.setup                  = m520x_cs_setup,
	.teardown               = m520x_cs_teardown,
	.select                 = m520x_cs_select,
	.deselect               = m520x_cs_deselect,
};

static struct mcfqspi_platform_data m520x_qspi_data = {
	.bus_num		= 0,
	.num_chipselect		= 3,
	.cs_control		= &m520x_cs_control,
};

static struct platform_device m520x_qspi = {
	.name			= "mcfqspi",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m520x_qspi_resources),
	.resource		= m520x_qspi_resources,
	.dev.platform_data	= &m520x_qspi_data,
};

static void __init m520x_qspi_init(void)
{
	u16 par;
	/* setup Port QS for QSPI with gpio CS control */
	writeb(0x3f, MCF_IPSBAR + MCF_GPIO_PAR_QSPI);
	/* make U1CTS and U2RTS gpio for cs_control */
	par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
	par &= 0x00ff;
	writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
}
#endif /* defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE) */


static struct platform_device *m520x_devices[] __initdata = {
	&m520x_uart,
	&m520x_fec,
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	&m520x_qspi,
#endif
};

/***************************************************************************/

static void __init m520x_uart_init_line(int line, int irq)
{
	u16 par;
	u8 par2;

	switch (line) {
	case 0:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD0 |
		       MCF_GPIO_PAR_UART_PAR_URXD0;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 1:
		par = readw(MCF_IPSBAR + MCF_GPIO_PAR_UART);
		par |= MCF_GPIO_PAR_UART_PAR_UTXD1 |
		       MCF_GPIO_PAR_UART_PAR_URXD1;
		writew(par, MCF_IPSBAR + MCF_GPIO_PAR_UART);
		break;
	case 2:
		par2 = readb(MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		par2 &= ~0x0F;
		par2 |= MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2 |
			MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2;
		writeb(par2, MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
		break;
	}
}

static void __init m520x_uarts_init(void)
{
	const int nrlines = ARRAY_SIZE(m520x_uart_platform);
	int line;

	for (line = 0; (line < nrlines); line++)
		m520x_uart_init_line(line, m520x_uart_platform[line].irq);
}

/***************************************************************************/

static void __init m520x_fec_init(void)
{
	u8 v;

	/* Set multi-function pins to ethernet mode */
	v = readb(MCF_IPSBAR + MCF_GPIO_PAR_FEC);
	writeb(v | 0xf0, MCF_IPSBAR + MCF_GPIO_PAR_FEC);

	v = readb(MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
	writeb(v | 0x0f, MCF_IPSBAR + MCF_GPIO_PAR_FECI2C);
}

/***************************************************************************/

static void m520x_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_RCR);
}

/***************************************************************************/

void __init config_BSP(char *commandp, int size)
{
	mach_reset = m520x_cpu_reset;
	m520x_uarts_init();
	m520x_fec_init();
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	m520x_qspi_init();
#endif
}

/***************************************************************************/

static int __init init_BSP(void)
{
	platform_add_devices(m520x_devices, ARRAY_SIZE(m520x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
