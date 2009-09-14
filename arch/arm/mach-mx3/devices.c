/*
 * Copyright 2006-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
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

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/gpio.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <mach/common.h>
#include <mach/imx-uart.h>
#include <mach/mx3_camera.h>

#include "devices.h"

static struct resource uart0[] = {
	{
		.start = UART1_BASE_ADDR,
		.end = UART1_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART1,
		.end = MXC_INT_UART1,
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
		.start = UART2_BASE_ADDR,
		.end = UART2_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART2,
		.end = MXC_INT_UART2,
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
		.start = UART3_BASE_ADDR,
		.end = UART3_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART3,
		.end = MXC_INT_UART3,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device2 = {
	.name = "imx-uart",
	.id = 2,
	.resource = uart2,
	.num_resources = ARRAY_SIZE(uart2),
};

#ifdef CONFIG_ARCH_MX31
static struct resource uart3[] = {
	{
		.start = UART4_BASE_ADDR,
		.end = UART4_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART4,
		.end = MXC_INT_UART4,
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
		.start = UART5_BASE_ADDR,
		.end = UART5_BASE_ADDR + 0x0B5,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_UART5,
		.end = MXC_INT_UART5,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_uart_device4 = {
	.name = "imx-uart",
	.id = 4,
	.resource = uart4,
	.num_resources = ARRAY_SIZE(uart4),
};
#endif /* CONFIG_ARCH_MX31 */

/* GPIO port description */
static struct mxc_gpio_port imx_gpio_ports[] = {
	[0] = {
		.chip.label = "gpio-0",
		.base = IO_ADDRESS(GPIO1_BASE_ADDR),
		.irq = MXC_INT_GPIO1,
		.virtual_irq_start = MXC_GPIO_IRQ_START,
	},
	[1] = {
		.chip.label = "gpio-1",
		.base = IO_ADDRESS(GPIO2_BASE_ADDR),
		.irq = MXC_INT_GPIO2,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 32,
	},
	[2] = {
		.chip.label = "gpio-2",
		.base = IO_ADDRESS(GPIO3_BASE_ADDR),
		.irq = MXC_INT_GPIO3,
		.virtual_irq_start = MXC_GPIO_IRQ_START + 64,
	}
};

int __init mxc_register_gpios(void)
{
	return mxc_gpio_init(imx_gpio_ports, ARRAY_SIZE(imx_gpio_ports));
}

static struct resource mxc_w1_master_resources[] = {
	{
		.start = OWIRE_BASE_ADDR,
		.end   = OWIRE_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_w1_master_device = {
	.name = "mxc_w1",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_w1_master_resources),
	.resource = mxc_w1_master_resources,
};

static struct resource mxc_nand_resources[] = {
	{
		.start	= 0, /* runtime dependent */
		.end	= 0,
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MXC_INT_NANDFC,
		.end	= MXC_INT_NANDFC,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mxc_nand_device = {
	.name = "mxc_nand",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_nand_resources),
	.resource = mxc_nand_resources,
};

static struct resource mxc_i2c0_resources[] = {
	{
		.start = I2C_BASE_ADDR,
		.end = I2C_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MXC_INT_I2C,
		.end = MXC_INT_I2C,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_i2c_device0 = {
	.name = "imx-i2c",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_i2c0_resources),
	.resource = mxc_i2c0_resources,
};

static struct resource mxc_i2c1_resources[] = {
	{
		.start = I2C2_BASE_ADDR,
		.end = I2C2_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MXC_INT_I2C2,
		.end = MXC_INT_I2C2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_i2c_device1 = {
	.name = "imx-i2c",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxc_i2c1_resources),
	.resource = mxc_i2c1_resources,
};

static struct resource mxc_i2c2_resources[] = {
	{
		.start = I2C3_BASE_ADDR,
		.end = I2C3_BASE_ADDR + SZ_4K - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = MXC_INT_I2C3,
		.end = MXC_INT_I2C3,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxc_i2c_device2 = {
	.name = "imx-i2c",
	.id = 2,
	.num_resources = ARRAY_SIZE(mxc_i2c2_resources),
	.resource = mxc_i2c2_resources,
};

#ifdef CONFIG_ARCH_MX31
static struct resource mxcsdhc0_resources[] = {
	{
		.start = MMC_SDHC1_BASE_ADDR,
		.end = MMC_SDHC1_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_MMC_SDHC1,
		.end = MXC_INT_MMC_SDHC1,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource mxcsdhc1_resources[] = {
	{
		.start = MMC_SDHC2_BASE_ADDR,
		.end = MMC_SDHC2_BASE_ADDR + SZ_16K - 1,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_MMC_SDHC2,
		.end = MXC_INT_MMC_SDHC2,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mxcsdhc_device0 = {
	.name = "mxc-mmc",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxcsdhc0_resources),
	.resource = mxcsdhc0_resources,
};

struct platform_device mxcsdhc_device1 = {
	.name = "mxc-mmc",
	.id = 1,
	.num_resources = ARRAY_SIZE(mxcsdhc1_resources),
	.resource = mxcsdhc1_resources,
};

static struct resource rnga_resources[] = {
	{
		.start = RNGA_BASE_ADDR,
		.end = RNGA_BASE_ADDR + 0x28,
		.flags = IORESOURCE_MEM,
	},
};

struct platform_device mxc_rnga_device = {
	.name = "mxc_rnga",
	.id = -1,
	.num_resources = 1,
	.resource = rnga_resources,
};
#endif /* CONFIG_ARCH_MX31 */

/* i.MX31 Image Processing Unit */

/* The resource order is important! */
static struct resource mx3_ipu_rsrc[] = {
	{
		.start = IPU_CTRL_BASE_ADDR,
		.end = IPU_CTRL_BASE_ADDR + 0x5F,
		.flags = IORESOURCE_MEM,
	}, {
		.start = IPU_CTRL_BASE_ADDR + 0x88,
		.end = IPU_CTRL_BASE_ADDR + 0xB3,
		.flags = IORESOURCE_MEM,
	}, {
		.start = MXC_INT_IPU_SYN,
		.end = MXC_INT_IPU_SYN,
		.flags = IORESOURCE_IRQ,
	}, {
		.start = MXC_INT_IPU_ERR,
		.end = MXC_INT_IPU_ERR,
		.flags = IORESOURCE_IRQ,
	},
};

struct platform_device mx3_ipu = {
	.name = "ipu-core",
	.id = -1,
	.num_resources = ARRAY_SIZE(mx3_ipu_rsrc),
	.resource = mx3_ipu_rsrc,
};

static struct resource fb_resources[] = {
	{
		.start	= IPU_CTRL_BASE_ADDR + 0xB4,
		.end	= IPU_CTRL_BASE_ADDR + 0x1BF,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mx3_fb = {
	.name		= "mx3_sdc_fb",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(fb_resources),
	.resource	= fb_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
       },
};

static struct resource camera_resources[] = {
	{
		.start	= IPU_CTRL_BASE_ADDR + 0x60,
		.end	= IPU_CTRL_BASE_ADDR + 0x87,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device mx3_camera = {
	.name		= "mx3-camera",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(camera_resources),
	.resource	= camera_resources,
	.dev		= {
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct resource otg_resources[] = {
	{
		.start	= OTG_BASE_ADDR,
		.end	= OTG_BASE_ADDR + 0x1ff,
		.flags	= IORESOURCE_MEM,
	}, {
		.start	= MXC_INT_USB3,
		.end	= MXC_INT_USB3,
		.flags	= IORESOURCE_IRQ,
	},
};

static u64 otg_dmamask = DMA_BIT_MASK(32);

/* OTG gadget device */
struct platform_device mxc_otg_udc_device = {
	.name		= "fsl-usb2-udc",
	.id		= -1,
	.dev		= {
		.dma_mask		= &otg_dmamask,
		.coherent_dma_mask	= DMA_BIT_MASK(32),
	},
	.resource	= otg_resources,
	.num_resources	= ARRAY_SIZE(otg_resources),
};

#ifdef CONFIG_ARCH_MX35
static struct resource mxc_fec_resources[] = {
	{
		.start	= MXC_FEC_BASE_ADDR,
		.end	= MXC_FEC_BASE_ADDR + 0xfff,
		.flags	= IORESOURCE_MEM
	}, {
		.start	= MXC_INT_FEC,
		.end	= MXC_INT_FEC,
		.flags	= IORESOURCE_IRQ
	},
};

struct platform_device mxc_fec_device = {
	.name = "fec",
	.id = 0,
	.num_resources = ARRAY_SIZE(mxc_fec_resources),
	.resource = mxc_fec_resources,
};
#endif

static int mx3_devices_init(void)
{
	if (cpu_is_mx31()) {
		mxc_nand_resources[0].start = MX31_NFC_BASE_ADDR;
		mxc_nand_resources[0].end = MX31_NFC_BASE_ADDR + 0xfff;
		mxc_register_device(&mxc_rnga_device, NULL);
	}
	if (cpu_is_mx35()) {
		mxc_nand_resources[0].start = MX35_NFC_BASE_ADDR;
		mxc_nand_resources[0].end = MX35_NFC_BASE_ADDR + 0xfff;
	}

	return 0;
}

subsys_initcall(mx3_devices_init);
