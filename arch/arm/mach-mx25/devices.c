/*
 * Copyright 2009 Sascha Hauer, <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <mach/mx25.h>
#include <mach/irqs.h>

static struct resource uart0[] = {
	{
		.start = 0x43f90000,
		.end = 0x43f93fff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 45,
		.end = 45,
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
		.start = 0x43f94000,
		.end = 0x43f97fff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 32,
		.end = 32,
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
		.start = 0x5000c000,
		.end = 0x5000ffff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 18,
		.end = 18,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device2 = {
	.name = "imx-uart",
	.id = 2,
	.resource = uart2,
	.num_resources = ARRAY_SIZE(uart2),
};

static struct resource uart3[] = {
	{
		.start = 0x50008000,
		.end = 0x5000bfff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 5,
		.end = 5,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device3 = {
	.name = "imx-uart",
	.id = 3,
	.resource = uart3,
	.num_resources = ARRAY_SIZE(uart3),
};

static struct resource uart4[] = {
	{
		.start = 0x5002c000,
		.end = 0x5002ffff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 40,
		.end = 40,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device4 = {
	.name = "imx-uart",
	.id = 4,
	.resource = uart4,
	.num_resources = ARRAY_SIZE(uart4),
};

#define MX25_OTG_BASE_ADDR 0x53FF4000

static u64 otg_dmamask = DMA_BIT_MASK(32);

static struct resource mxc_otg_resources[] = {
	{
		.start = MX25_OTG_BASE_ADDR,
		.end = MX25_OTG_BASE_ADDR + 0x1ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 37,
		.end = 37,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_otg = {
	.name = "mxc-ehci",
	.id = 0,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
		.dma_mask = &otg_dmamask,
	},
	.resource = mxc_otg_resources,
	.num_resources = ARRAY_SIZE(mxc_otg_resources),
};

/* OTG gadget device */
struct platform_device otg_udc_device = {
	.name = "fsl-usb2-udc",
	.id   = -1,
	.dev  = {
		.dma_mask          = &otg_dmamask,
		.coherent_dma_mask = 0xffffffff,
	},
	.resource = mxc_otg_resources,
	.num_resources = ARRAY_SIZE(mxc_otg_resources),
};

static u64 usbh2_dmamask = DMA_BIT_MASK(32);

static struct resource mxc_usbh2_resources[] = {
	{
		.start = MX25_OTG_BASE_ADDR + 0x400,
		.end = MX25_OTG_BASE_ADDR + 0x5ff,
		.flags = IORESOURCE_MEM,
	}, {
		.start = 35,
		.end = 35,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_usbh2 = {
	.name = "mxc-ehci",
	.id = 1,
	.dev = {
		.coherent_dma_mask = 0xffffffff,
		.dma_mask = &usbh2_dmamask,
	},
	.resource = mxc_usbh2_resources,
	.num_resources = ARRAY_SIZE(mxc_usbh2_resources),
};

static struct resource mxc_spi_resources0[] = {
	{
	       .start = 0x43fa4000,
	       .end = 0x43fa7fff,
	       .flags = IORESOURCE_MEM,
	}, {
	       .start = 14,
	       .end = 14,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_spi_device0 = {
	.name = "spi_imx",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_spi_resources0),
	.resource = mxc_spi_resources0,
};

static struct resource mxc_spi_resources1[] = {
	{
	       .start = 0x50010000,
	       .end = 0x50013fff,
	       .flags = IORESOURCE_MEM,
	}, {
	       .start = 13,
	       .end = 13,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_spi_device1 = {
	.name = "spi_imx",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_spi_resources1),
	.resource = mxc_spi_resources1,
};

static struct resource mxc_spi_resources2[] = {
	{
	       .start = 0x50004000,
	       .end = 0x50007fff,
	       .flags = IORESOURCE_MEM,
	}, {
	       .start = 0,
	       .end = 0,
	       .flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_spi_device2 = {
	.name = "spi_imx",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_spi_resources2),
	.resource = mxc_spi_resources2,
};

static struct resource mxc_pwm_resources0[] = {
	{
		.start	= 0x53fe0000,
		.end	= 0x53fe3fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 26,
		.end     = 26,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device0 = {
	.name = "mxc_pwm",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources0),
	.resource = mxc_pwm_resources0,
};

static struct resource mxc_pwm_resources1[] = {
	{
		.start	= 0x53fa0000,
		.end	= 0x53fa3fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 36,
		.end     = 36,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device1 = {
	.name = "mxc_pwm",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources1),
	.resource = mxc_pwm_resources1,
};

static struct resource mxc_pwm_resources2[] = {
	{
		.start	= 0x53fa8000,
		.end	= 0x53fabfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 41,
		.end     = 41,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device2 = {
	.name = "mxc_pwm",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources2),
	.resource = mxc_pwm_resources2,
};

static struct resource mxc_keypad_resources[] = {
	{
		.start	= 0x43fa8000,
		.end	= 0x43fabfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 24,
		.end     = 24,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_keypad_device = {
	.name = "mxc-keypad",
	.id = -1,
	.num_resources = ARRAY_SIZE(mxc_keypad_resources),
	.resource = mxc_keypad_resources,
};

static struct resource mxc_pwm_resources3[] = {
	{
		.start	= 0x53fc8000,
		.end	= 0x53fcbfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start   = 42,
		.end     = 42,
		.flags   = IORESOURCE_IRQ,
	}
};

struct platform_device mxc_pwm_device3 = {
	.name = "mxc_pwm",
	.id = 3,
	.num_resources = ARRAY_SIZE(mxc_pwm_resources3),
	.resource = mxc_pwm_resources3,
};

static struct resource mxc_i2c_1_resources[] = {
	{
		.start	= 0x43f80000,
		.end	= 0x43f83fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 3,
		.end	= 3,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device mxc_i2c_device0 = {
	.name = "imx-i2c",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_i2c_1_resources),
	.resource = mxc_i2c_1_resources,
};

static struct resource mxc_i2c_2_resources[] = {
	{
		.start	= 0x43f98000,
		.end	= 0x43f9bfff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 4,
		.end	= 4,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device mxc_i2c_device1 = {
	.name = "imx-i2c",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_i2c_2_resources),
	.resource = mxc_i2c_2_resources,
};

static struct resource mxc_i2c_3_resources[] = {
	{
		.start	= 0x43f84000,
		.end	= 0x43f87fff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= 10,
		.end	= 10,
		.flags	= IORESOURCE_IRQ,
	}
};

struct platform_device mxc_i2c_device2 = {
	.name = "imx-i2c",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_i2c_3_resources),
	.resource = mxc_i2c_3_resources,
};

static struct mxc_gpio_port imx_gpio_ports[] = {
	{
		.chip.label = "gpio-0",
		.base = (void __iomem *)MX25_GPIO1_BASE_ADDR_VIRT,
		.irq = 52,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	}, {
		.chip.label = "gpio-1",
		.base = (void __iomem *)MX25_GPIO2_BASE_ADDR_VIRT,
		.irq = 51,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	}, {
		.chip.label = "gpio-2",
		.base = (void __iomem *)MX25_GPIO3_BASE_ADDR_VIRT,
		.irq = 16,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}, {
		.chip.label = "gpio-3",
		.base = (void __iomem *)MX25_GPIO4_BASE_ADDR_VIRT,
		.irq = 23,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 96,
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}

static struct resource mx25_fec_resources[] = {
	{
		.start	= MX25_FEC_BASE_ADDR,
		.end	= MX25_FEC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_FEC,
		.end	= MX25_INT_FEC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mx25_fec_device = {
	.name	= "fec",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(mx25_fec_resources),
	.resource	= mx25_fec_resources,
};

static struct resource mxc_nand_resources[] = {
	{
		.start	= MX25_NFC_BASE_ADDR,
		.end	= MX25_NFC_BASE_ADDR + 0x1fff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_NANDFC,
		.end	= MX25_INT_NANDFC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mxc_nand_device = {
	.name		= "mxc_nand",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(mxc_nand_resources),
	.resource	= mxc_nand_resources,
};

static struct resource mx25_rtc_resources[] = {
	{
		.start	= MX25_DRYICE_BASE_ADDR,
		.end	= MX25_DRYICE_BASE_ADDR + 0x40,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_DRYICE,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mx25_rtc_device = {
	.name	= "imxdi_rtc",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(mx25_rtc_resources),
	.resource	= mx25_rtc_resources,
};

static struct resource mx25_fb_resources[] = {
	{
		.start	= MX25_LCDC_BASE_ADDR,
		.end	= MX25_LCDC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= MX25_INT_LCDC,
		.end	= MX25_INT_LCDC,
		.flags	= IORESOURCE_IRQ,
	},
};

struct platform_device mx25_fb_device = {
	.name		= "imx-fb",
	.id		= 0,
	.resource	= mx25_fb_resources,
	.num_resources	= ARRAY_SIZE(mx25_fb_resources),
	.dev		= {
		.coherent_dma_mask = 0xFFFFFFFF,
	},
};
