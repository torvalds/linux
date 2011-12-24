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

#define MCFQSPI_CS0    46
#define MCFQSPI_CS1    47
#define MCFQSPI_CS2    27

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
	writeb(0x3f, MCF_GPIO_PAR_QSPI);
	/* make U1CTS and U2RTS gpio for cs_control */
	par = readw(MCF_GPIO_PAR_UART);
	par &= 0x00ff;
	writew(par, MCF_GPIO_PAR_UART);
}
#endif /* defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE) */


static struct platform_device *m520x_devices[] __initdata = {
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	&m520x_qspi,
#endif
};

/***************************************************************************/

static void __init m520x_uarts_init(void)
{
	u16 par;
	u8 par2;

	/* UART0 and UART1 GPIO pin setup */
	par = readw(MCF_GPIO_PAR_UART);
	par |= MCF_GPIO_PAR_UART_PAR_UTXD0 | MCF_GPIO_PAR_UART_PAR_URXD0;
	par |= MCF_GPIO_PAR_UART_PAR_UTXD1 | MCF_GPIO_PAR_UART_PAR_URXD1;
	writew(par, MCF_GPIO_PAR_UART);

	/* UART1 GPIO pin setup */
	par2 = readb(MCF_GPIO_PAR_FECI2C);
	par2 &= ~0x0F;
	par2 |= MCF_GPIO_PAR_FECI2C_PAR_SCL_UTXD2 |
		MCF_GPIO_PAR_FECI2C_PAR_SDA_URXD2;
	writeb(par2, MCF_GPIO_PAR_FECI2C);
}

/***************************************************************************/

static void __init m520x_fec_init(void)
{
	u8 v;

	/* Set multi-function pins to ethernet mode */
	v = readb(MCF_GPIO_PAR_FEC);
	writeb(v | 0xf0, MCF_GPIO_PAR_FEC);

	v = readb(MCF_GPIO_PAR_FECI2C);
	writeb(v | 0x0f, MCF_GPIO_PAR_FECI2C);
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
	mach_sched_init = hw_timer_init;
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
