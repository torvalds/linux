/*
 * arch/arm/plat-orion/common.c
 *
 * Marvell Orion SoC common setup code used by multiple mach-/common.c
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>

/* Fill in the resources structure and link it into the platform
   device structure. There is always a memory region, and nearly
   always an interrupt.*/
static void fill_resources(struct platform_device *device,
			   struct resource *resources,
			   resource_size_t mapbase,
			   resource_size_t size,
			   unsigned int irq)
{
	device->resource = resources;
	device->num_resources = 1;
	resources[0].flags = IORESOURCE_MEM;
	resources[0].start = mapbase;
	resources[0].end = mapbase + size;

	if (irq != NO_IRQ) {
		device->num_resources++;
		resources[1].flags = IORESOURCE_IRQ;
		resources[1].start = irq;
		resources[1].end = irq;
	}
}

/*****************************************************************************
 * UART
 ****************************************************************************/
static void __init uart_complete(
	struct platform_device *orion_uart,
	struct plat_serial8250_port *data,
	struct resource *resources,
	unsigned int membase,
	resource_size_t mapbase,
	unsigned int irq,
	unsigned int uartclk)
{
	data->mapbase = mapbase;
	data->membase = (void __iomem *)membase;
	data->irq = irq;
	data->uartclk = uartclk;
	orion_uart->dev.platform_data = data;

	fill_resources(orion_uart, resources, mapbase, 0xff, irq);
	platform_device_register(orion_uart);
}

/*****************************************************************************
 * UART0
 ****************************************************************************/
static struct plat_serial8250_port orion_uart0_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart0_resources[2];

static struct platform_device orion_uart0 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM,
};

void __init orion_uart0_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart0, orion_uart0_data, orion_uart0_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART1
 ****************************************************************************/
static struct plat_serial8250_port orion_uart1_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart1_resources[2];

static struct platform_device orion_uart1 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM1,
};

void __init orion_uart1_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart1, orion_uart1_data, orion_uart1_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART2
 ****************************************************************************/
static struct plat_serial8250_port orion_uart2_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart2_resources[2];

static struct platform_device orion_uart2 = {
	.name			= "serial8250",
	.id			= PLAT8250_DEV_PLATFORM2,
};

void __init orion_uart2_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart2, orion_uart2_data, orion_uart2_resources,
		      membase, mapbase, irq, uartclk);
}

/*****************************************************************************
 * UART3
 ****************************************************************************/
static struct plat_serial8250_port orion_uart3_data[] = {
	{
		.flags		= UPF_SKIP_TEST | UPF_BOOT_AUTOCONF,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
	}, {
	},
};

static struct resource orion_uart3_resources[2];

static struct platform_device orion_uart3 = {
	.name			= "serial8250",
	.id			= 3,
};

void __init orion_uart3_init(unsigned int membase,
			     resource_size_t mapbase,
			     unsigned int irq,
			     unsigned int uartclk)
{
	uart_complete(&orion_uart3, orion_uart3_data, orion_uart3_resources,
		      membase, mapbase, irq, uartclk);
}
