/***************************************************************************/

/*
 *	linux/arch/m68knommu/platform/528x/config.c
 *
 *	Sub-architcture dependent initialization code for the Freescale
 *	5280, 5281 and 5282 CPUs.
 *
 *	Copyright (C) 1999-2003, Greg Ungerer (gerg@snapgear.com)
 *	Copyright (C) 2001-2003, SnapGear Inc. (www.snapgear.com)
 */

/***************************************************************************/

#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <asm/machdep.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/mcfqspi.h>

/***************************************************************************/

static struct mcf_platform_uart m528x_uart_platform[] = {
	{
		.mapbase	= MCFUART_BASE1,
		.irq		= MCFINT_VECBASE + MCFINT_UART0,
	},
	{
		.mapbase 	= MCFUART_BASE2,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 1,
	},
	{
		.mapbase 	= MCFUART_BASE3,
		.irq		= MCFINT_VECBASE + MCFINT_UART0 + 2,
	},
	{ },
};

static struct platform_device m528x_uart = {
	.name			= "mcfuart",
	.id			= 0,
	.dev.platform_data	= m528x_uart_platform,
};

static struct resource m528x_fec_resources[] = {
	{
		.start		= MCFFEC_BASE,
		.end		= MCFFEC_BASE + MCFFEC_SIZE - 1,
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

static struct platform_device m528x_fec = {
	.name			= "fec",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m528x_fec_resources),
	.resource		= m528x_fec_resources,
};

#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
static struct resource m528x_qspi_resources[] = {
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

#define MCFQSPI_CS0    147
#define MCFQSPI_CS1    148
#define MCFQSPI_CS2    149
#define MCFQSPI_CS3    150

static int m528x_cs_setup(struct mcfqspi_cs_control *cs_control)
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

static void m528x_cs_teardown(struct mcfqspi_cs_control *cs_control)
{
	gpio_free(MCFQSPI_CS3);
	gpio_free(MCFQSPI_CS2);
	gpio_free(MCFQSPI_CS1);
	gpio_free(MCFQSPI_CS0);
}

static void m528x_cs_select(struct mcfqspi_cs_control *cs_control,
			    u8 chip_select, bool cs_high)
{
	gpio_set_value(MCFQSPI_CS0 + chip_select, cs_high);
}

static void m528x_cs_deselect(struct mcfqspi_cs_control *cs_control,
			      u8 chip_select, bool cs_high)
{
	gpio_set_value(MCFQSPI_CS0 + chip_select, !cs_high);
}

static struct mcfqspi_cs_control m528x_cs_control = {
	.setup                  = m528x_cs_setup,
	.teardown               = m528x_cs_teardown,
	.select                 = m528x_cs_select,
	.deselect               = m528x_cs_deselect,
};

static struct mcfqspi_platform_data m528x_qspi_data = {
	.bus_num		= 0,
	.num_chipselect		= 4,
	.cs_control		= &m528x_cs_control,
};

static struct platform_device m528x_qspi = {
	.name			= "mcfqspi",
	.id			= 0,
	.num_resources		= ARRAY_SIZE(m528x_qspi_resources),
	.resource		= m528x_qspi_resources,
	.dev.platform_data	= &m528x_qspi_data,
};

static void __init m528x_qspi_init(void)
{
	/* setup Port QS for QSPI with gpio CS control */
	__raw_writeb(0x07, MCFGPIO_PQSPAR);
}
#endif /* defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE) */

static struct platform_device *m528x_devices[] __initdata = {
	&m528x_uart,
	&m528x_fec,
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	&m528x_qspi,
#endif
};

/***************************************************************************/

static void __init m528x_uart_init_line(int line, int irq)
{
	u8 port;

	if ((line < 0) || (line > 2))
		return;

	/* make sure PUAPAR is set for UART0 and UART1 */
	if (line < 2) {
		port = readb(MCF5282_GPIO_PUAPAR);
		port |= (0x03 << (line * 2));
		writeb(port, MCF5282_GPIO_PUAPAR);
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

static void __init m528x_fec_init(void)
{
	u16 v16;

	/* Set multi-function pins to ethernet mode for fec0 */
	v16 = readw(MCF_IPSBAR + 0x100056);
	writew(v16 | 0xf00, MCF_IPSBAR + 0x100056);
	writeb(0xc0, MCF_IPSBAR + 0x100058);
}

/***************************************************************************/

static void m528x_cpu_reset(void)
{
	local_irq_disable();
	__raw_writeb(MCF_RCR_SWRESET, MCF_IPSBAR + MCF_RCR);
}

/***************************************************************************/

#ifdef CONFIG_WILDFIRE
void wildfire_halt(void)
{
	writeb(0, 0x30000007);
	writeb(0x2, 0x30000007);
}
#endif

#ifdef CONFIG_WILDFIREMOD
void wildfiremod_halt(void)
{
	printk(KERN_INFO "WildFireMod hibernating...\n");

	/* Set portE.5 to Digital IO */
	MCF5282_GPIO_PEPAR &= ~(1 << (5 * 2));

	/* Make portE.5 an output */
	MCF5282_GPIO_DDRE |= (1 << 5);

	/* Now toggle portE.5 from low to high */
	MCF5282_GPIO_PORTE &= ~(1 << 5);
	MCF5282_GPIO_PORTE |= (1 << 5);

	printk(KERN_EMERG "Failed to hibernate. Halting!\n");
}
#endif

void __init config_BSP(char *commandp, int size)
{
#ifdef CONFIG_WILDFIRE
	mach_halt = wildfire_halt;
#endif
#ifdef CONFIG_WILDFIREMOD
	mach_halt = wildfiremod_halt;
#endif
}

/***************************************************************************/

static int __init init_BSP(void)
{
	mach_reset = m528x_cpu_reset;
	m528x_uarts_init();
	m528x_fec_init();
#if defined(CONFIG_SPI_COLDFIRE_QSPI) || defined(CONFIG_SPI_COLDFIRE_QSPI_MODULE)
	m528x_qspi_init();
#endif
	platform_add_devices(m528x_devices, ARRAY_SIZE(m528x_devices));
	return 0;
}

arch_initcall(init_BSP);

/***************************************************************************/
