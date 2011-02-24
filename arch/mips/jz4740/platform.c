/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 platform devices
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/resource.h>

#include <linux/dma-mapping.h>

#include <asm/mach-jz4740/platform.h>
#include <asm/mach-jz4740/base.h>
#include <asm/mach-jz4740/irq.h>

#include <linux/serial_core.h>
#include <linux/serial_8250.h>

#include "serial.h"
#include "clock.h"

/* OHCI controller */
static struct resource jz4740_usb_ohci_resources[] = {
	{
		.start	= JZ4740_UHC_BASE_ADDR,
		.end	= JZ4740_UHC_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= JZ4740_IRQ_UHC,
		.end	= JZ4740_IRQ_UHC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device jz4740_usb_ohci_device = {
	.name		= "jz4740-ohci",
	.id		= -1,
	.dev = {
		.dma_mask = &jz4740_usb_ohci_device.dev.coherent_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(jz4740_usb_ohci_resources),
	.resource	= jz4740_usb_ohci_resources,
};

/* UDC (USB gadget controller) */
static struct resource jz4740_usb_gdt_resources[] = {
	{
		.start	= JZ4740_UDC_BASE_ADDR,
		.end	= JZ4740_UDC_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= JZ4740_IRQ_UDC,
		.end	= JZ4740_IRQ_UDC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device jz4740_udc_device = {
	.name		= "jz-udc",
	.id		= -1,
	.dev = {
		.dma_mask = &jz4740_udc_device.dev.coherent_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources	= ARRAY_SIZE(jz4740_usb_gdt_resources),
	.resource	= jz4740_usb_gdt_resources,
};

/* MMC/SD controller */
static struct resource jz4740_mmc_resources[] = {
	{
		.start	= JZ4740_MSC_BASE_ADDR,
		.end	= JZ4740_MSC_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= JZ4740_IRQ_MSC,
		.end	= JZ4740_IRQ_MSC,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device jz4740_mmc_device = {
	.name		= "jz4740-mmc",
	.id		= 0,
	.dev = {
		.dma_mask = &jz4740_mmc_device.dev.coherent_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
	.num_resources  = ARRAY_SIZE(jz4740_mmc_resources),
	.resource	= jz4740_mmc_resources,
};

/* RTC controller */
static struct resource jz4740_rtc_resources[] = {
	{
		.start	= JZ4740_RTC_BASE_ADDR,
		.end	= JZ4740_RTC_BASE_ADDR + 0x38 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start  = JZ4740_IRQ_RTC,
		.end	= JZ4740_IRQ_RTC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device jz4740_rtc_device = {
	.name		= "jz4740-rtc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(jz4740_rtc_resources),
	.resource	= jz4740_rtc_resources,
};

/* I2C controller */
static struct resource jz4740_i2c_resources[] = {
	{
		.start	= JZ4740_I2C_BASE_ADDR,
		.end	= JZ4740_I2C_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= JZ4740_IRQ_I2C,
		.end	= JZ4740_IRQ_I2C,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device jz4740_i2c_device = {
	.name		= "jz4740-i2c",
	.id		= 0,
	.num_resources  = ARRAY_SIZE(jz4740_i2c_resources),
	.resource	= jz4740_i2c_resources,
};

/* NAND controller */
static struct resource jz4740_nand_resources[] = {
	{
		.name	= "mmio",
		.start	= JZ4740_EMC_BASE_ADDR,
		.end	= JZ4740_EMC_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "bank",
		.start	= 0x18000000,
		.end	= 0x180C0000 - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device jz4740_nand_device = {
	.name = "jz4740-nand",
	.num_resources = ARRAY_SIZE(jz4740_nand_resources),
	.resource = jz4740_nand_resources,
};

/* LCD controller */
static struct resource jz4740_framebuffer_resources[] = {
	{
		.start	= JZ4740_LCD_BASE_ADDR,
		.end	= JZ4740_LCD_BASE_ADDR + 0x1000 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device jz4740_framebuffer_device = {
	.name		= "jz4740-fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(jz4740_framebuffer_resources),
	.resource	= jz4740_framebuffer_resources,
	.dev = {
		.dma_mask = &jz4740_framebuffer_device.dev.coherent_dma_mask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

/* I2S controller */
static struct resource jz4740_i2s_resources[] = {
	{
		.start	= JZ4740_AIC_BASE_ADDR,
		.end	= JZ4740_AIC_BASE_ADDR + 0x38 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device jz4740_i2s_device = {
	.name		= "jz4740-i2s",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(jz4740_i2s_resources),
	.resource	= jz4740_i2s_resources,
};

/* PCM */
struct platform_device jz4740_pcm_device = {
	.name		= "jz4740-pcm-audio",
	.id		= -1,
};

/* Codec */
static struct resource jz4740_codec_resources[] = {
	{
		.start	= JZ4740_AIC_BASE_ADDR + 0x80,
		.end	= JZ4740_AIC_BASE_ADDR + 0x88 - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device jz4740_codec_device = {
	.name		= "jz4740-codec",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(jz4740_codec_resources),
	.resource	= jz4740_codec_resources,
};

/* ADC controller */
static struct resource jz4740_adc_resources[] = {
	{
		.start	= JZ4740_SADC_BASE_ADDR,
		.end	= JZ4740_SADC_BASE_ADDR + 0x30,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= JZ4740_IRQ_SADC,
		.end	= JZ4740_IRQ_SADC,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= JZ4740_IRQ_ADC_BASE,
		.end	= JZ4740_IRQ_ADC_BASE,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device jz4740_adc_device = {
	.name		= "jz4740-adc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(jz4740_adc_resources),
	.resource	= jz4740_adc_resources,
};

/* Serial */
#define JZ4740_UART_DATA(_id) \
	{ \
		.flags = UPF_SKIP_TEST | UPF_IOREMAP | UPF_FIXED_TYPE, \
		.iotype = UPIO_MEM, \
		.regshift = 2, \
		.serial_out = jz4740_serial_out, \
		.type = PORT_16550, \
		.mapbase = JZ4740_UART ## _id ## _BASE_ADDR, \
		.irq = JZ4740_IRQ_UART ## _id, \
	}

static struct plat_serial8250_port jz4740_uart_data[] = {
	JZ4740_UART_DATA(0),
	JZ4740_UART_DATA(1),
	{},
};

static struct platform_device jz4740_uart_device = {
	.name = "serial8250",
	.id = 0,
	.dev = {
		.platform_data = jz4740_uart_data,
	},
};

void jz4740_serial_device_register(void)
{
	struct plat_serial8250_port *p;

	for (p = jz4740_uart_data; p->flags != 0; ++p)
		p->uartclk = jz4740_clock_bdata.ext_rate;

	platform_device_register(&jz4740_uart_device);
}
