/* arch/arm/mach-rk29/devices.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
 
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include "devices.h"
#ifdef CONFIG_I2C_RK29
static struct resource resources_i2c0[] = {
	{
		.start	= IRQ_I2C0,
		.end	= IRQ_I2C0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_I2C0_PHYS,
		.end	= RK29_I2C0_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_i2c1[] = {
	{
		.start	= IRQ_I2C1,
		.end	= IRQ_I2C1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_I2C1_PHYS,
		.end	= RK29_I2C1_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_i2c2[] = {
	{
		.start	= IRQ_I2C2,
		.end	= IRQ_I2C2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_I2C2_PHYS,
		.end	= RK29_I2C2_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_i2c3[] = {
	{
		.start	= IRQ_I2C3,
		.end	= IRQ_I2C3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_I2C3_PHYS,
		.end	= RK29_I2C3_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device rk29_device_i2c0 = {
	.name	= "rk29_i2c",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_i2c0),
	.resource	= resources_i2c0,
	.dev 			= {
		.platform_data = &default_i2c0_data,
	},
};
struct platform_device rk29_device_i2c1 = {
	.name	= "rk29_i2c",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_i2c1),
	.resource	= resources_i2c1,
	.dev 			= {
		.platform_data = &default_i2c1_data,
	},
};
struct platform_device rk29_device_i2c2 = {
	.name	= "rk29_i2c",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_i2c2),
	.resource	= resources_i2c2,
	.dev 			= {
		.platform_data = &default_i2c2_data,
	},
};
struct platform_device rk29_device_i2c3 = {
	.name	= "rk29_i2c",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_i2c3),
	.resource	= resources_i2c3,
	.dev 			= {
		.platform_data = &default_i2c3_data,
	},
};
#endif

#ifdef CONFIG_SDMMC0_RK29 
static struct resource resources_sdmmc0[] = {
	{
		.start 	= IRQ_SDMMC,
		.end 	= IRQ_SDMMC,
		.flags 	= IORESOURCE_IRQ,
	},
	{
		.start 	= RK29_SDMMC0_PHYS,
		.end 	= RK29_SDMMC0_PHYS + RK29_SDMMC0_SIZE -1,
		.flags 	= IORESOURCE_MEM,
	}
};
#endif
#ifdef CONFIG_SDMMC1_RK29
static struct resource resources_sdmmc1[] = {
	{
		.start 	= IRQ_SDIO,
		.end 	= IRQ_SDIO,
		.flags 	= IORESOURCE_IRQ,
	},
	{
		.start 	= RK29_SDMMC1_PHYS,
		.end 	= RK29_SDMMC1_PHYS + RK29_SDMMC1_SIZE -1,
		.flags 	= IORESOURCE_MEM,
	}
}; 
#endif
/* sdmmc */
#ifdef CONFIG_SDMMC0_RK29
struct platform_device rk29_device_sdmmc0 = {
	.name			= "rk29_sdmmc",
	.id				= 0,
	.num_resources	= ARRAY_SIZE(resources_sdmmc0),
	.resource		= resources_sdmmc0,
	.dev 			= {
		.platform_data = &default_sdmmc0_data,
	},
};
#endif
#ifdef CONFIG_SDMMC1_RK29
struct platform_device rk29_device_sdmmc1 = {
	.name			= "rk29_sdmmc",
	.id				= 1,
	.num_resources	= ARRAY_SIZE(resources_sdmmc1),
	.resource		= resources_sdmmc1,
	.dev 			= {
		.platform_data = &default_sdmmc1_data,
	},
};
#endif
/*
 * rk29 4 uarts device
 */
#ifdef CONFIG_UART0_RK29
static struct resource resources_uart0[] = {
	{
		.start	= IRQ_UART0,
		.end	= IRQ_UART0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART0_PHYS,
		.end	= RK29_UART0_PHYS + RK29_UART0_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART1_RK29
static struct resource resources_uart1[] = {
	{
		.start	= IRQ_UART1,
		.end	= IRQ_UART1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART1_PHYS,
		.end	= RK29_UART1_PHYS + RK29_UART1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART2_RK29
static struct resource resources_uart2[] = {
	{
		.start	= IRQ_UART2,
		.end	= IRQ_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART2_PHYS,
		.end	= RK29_UART2_PHYS + RK29_UART2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART3_RK29
static struct resource resources_uart3[] = {
	{
		.start	= IRQ_UART3,
		.end	= IRQ_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_UART3_PHYS,
		.end	= RK29_UART3_PHYS + RK29_UART3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};
#endif
#ifdef CONFIG_UART0_RK29
struct platform_device rk29_device_uart0 = {
	.name	= "rk29_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart0),
	.resource	= resources_uart0,
	.dev = {
		.platform_data = &rk2818_serial0_platdata,
	},
};
#endif
#ifdef CONFIG_UART1_RK29
struct platform_device rk29_device_uart1 = {
	.name	= "rk29_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
};
#endif
#ifdef CONFIG_UART2_RK29
struct platform_device rk29_device_uart2 = {
	.name	= "rk29_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};
#endif
#ifdef CONFIG_UART3_RK29
struct platform_device rk29_device_uart3 = {
	.name	= "rk29_serial",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};
#endif

#ifdef CONFIG_FB_RK29
/* rk29 fb resource */
static struct resource rk29_fb_resource[] = {
	[0] = {
		.start = RK29_LCDC_PHYS,
		.end   = RK29_LCDC_PHYS + RK29_LCDC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_LCDC,
		.end   = IRQ_LCDC,
		.flags = IORESOURCE_IRQ,
	},
};

/*platform_device*/
extern struct rk29fb_info rk29_fb_info;
struct platform_device rk29_device_fb = {
	.name		  = "rk29-fb",
	.id		  = 4,
	.num_resources	  = ARRAY_SIZE(rk29_fb_resource),
	.resource	  = rk29_fb_resource,
	.dev            = {
		.platform_data  = &rk29_fb_info,
	}
};
#endif
#if defined(CONFIG_MTD_NAND_RK29)  
static struct resource nand_resources[] = {
	{
		.start	= RK29_NANDC_PHYS,
		.end	= 	RK29_NANDC_PHYS+RK29_NANDC_SIZE -1,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device rk29_device_nand = {
	.name	= "rk29-nand",
	.id		=  -1, 
	.resource	= nand_resources,
	.num_resources= ARRAY_SIZE(nand_resources),
	.dev	= {
		.platform_data= &rk29_nand_data,
	},
	
};
#endif

