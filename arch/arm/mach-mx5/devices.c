/*
 * Copyright 2009 Amit Kucheria <amit.kucheria@canonical.com>
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/imx-uart.h>
#include <mach/irqs.h>

static struct resource uart0[] = {
	{
		.start = MX51_UART1_BASE_ADDR,
		.end = MX51_UART1_BASE_ADDR + 0xfff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX51_MXC_INT_UART1,
		.end = MX51_MXC_INT_UART1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device0 = {
	.name = "imx-uart",
	.id = 0,
	.resource = uart0,
	.num_resources = ARRAY_SIZE(uart0),
};

static struct resource uart1[] = {
	{
		.start = MX51_UART2_BASE_ADDR,
		.end = MX51_UART2_BASE_ADDR + 0xfff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX51_MXC_INT_UART2,
		.end = MX51_MXC_INT_UART2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device1 = {
	.name = "imx-uart",
	.id = 1,
	.resource = uart1,
	.num_resources = ARRAY_SIZE(uart1),
};

static struct resource uart2[] = {
	{
		.start = MX51_UART3_BASE_ADDR,
		.end = MX51_UART3_BASE_ADDR + 0xfff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MX51_MXC_INT_UART3,
		.end = MX51_MXC_INT_UART3,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device2 = {
	.name = "imx-uart",
	.id = 2,
	.resource = uart2,
	.num_resources = ARRAY_SIZE(uart2),
};

static struct resource mxc_fec_resources[] = {
	{
		.start	= MX51_MXC_FEC_BASE_ADDR,
		.end	= MX51_MXC_FEC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MX51_MXC_INT_FEC,
		.end	= MX51_MXC_INT_FEC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mxc_fec_device = {
	.name = "fec",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_fec_resources),
	.resource = mxc_fec_resources,
};

static u64 usb_dma_mask = DMA_BIT_MASK(32);

static struct resource usbotg_resources[] = {
	{
		.start = MX51_OTG_BASE_ADDR,
		.end = MX51_OTG_BASE_ADDR + 0x1ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MX51_MXC_INT_USB_OTG,
		.flags = IORESOURCE_IRQ,
	},
};

/* OTG gadget device */
struct platform_device mxc_usbdr_udc_device = {
	.name		= "fsl-usb2-udc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(usbotg_resources),
	.resource	= usbotg_resources,
	.dev		= {
		.dma_mask		= &usb_dma_mask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
};

struct platform_device mxc_usbdr_host_device = {
	.name = "mxc-ehci",
	.id = 0,
	.num_resources = ARRAY_SIZE(usbotg_resources),
	.resource = usbotg_resources,
	.dev = {
		.dma_mask = &usb_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource usbh1_resources[] = {
	{
		.start = MX51_OTG_BASE_ADDR + 0x200,
		.end = MX51_OTG_BASE_ADDR + 0x200 + 0x1ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MX51_MXC_INT_USB_H1,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh1_device = {
	.name = "mxc-ehci",
	.id = 1,
	.num_resources = ARRAY_SIZE(usbh1_resources),
	.resource = usbh1_resources,
	.dev = {
		.dma_mask = &usb_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource mxc_wdt_resources[] = {
	{
		.start = MX51_WDOG_BASE_ADDR,
		.end = MX51_WDOG_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_wdt = {
	.name = "imx2-wdt",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_wdt_resources),
	.resource = mxc_wdt_resources,
};

static struct mxc_gpio_port mxc_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = MX51_IO_ADDRESS(MX51_GPIO1_BASE_ADDR),
		.irq = MX51_MXC_INT_GPIO1_LOW,
		.virtual_irq_start = MXC_GPIO_IRQ_START
	},
	{
		.chip.label = "gpio-1",
		.base = MX51_IO_ADDRESS(MX51_GPIO2_BASE_ADDR),
		.irq = MX51_MXC_INT_GPIO2_LOW,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32 * 1
	},
	{
		.chip.label = "gpio-2",
		.base = MX51_IO_ADDRESS(MX51_GPIO3_BASE_ADDR),
		.irq = MX51_MXC_INT_GPIO3_LOW,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32 * 2
	},
	{
		.chip.label = "gpio-3",
		.base = MX51_IO_ADDRESS(MX51_GPIO4_BASE_ADDR),
		.irq = MX51_MXC_INT_GPIO4_LOW,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32 * 3
	},
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(mxc_gpio_ports, ARRAY_SIZE(mxc_gpio_ports));
}
