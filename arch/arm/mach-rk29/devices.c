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
#include <linux/usb/android_composite.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <mach/rk29_iomap.h>
#include <mach/rk29-dma-pl330.h> 
#include <mach/rk29_camera.h>                          /* ddl@rock-chips.com : camera support */
#include "devices.h"
#ifdef CONFIG_ADC_RK29
static struct resource rk29_adc_resource[] = {
	{
		.start = IRQ_SARADC,
		.end   = IRQ_SARADC,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK29_ADC_PHYS,
		.end   = RK29_ADC_PHYS + RK29_ADC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device rk29_device_adc = {
	.name		  = "rk29-adc",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(rk29_adc_resource),
	.resource	  = rk29_adc_resource,
};

#endif
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

/***********************************************************
*	  backlight
***************************************************************/
#ifdef CONFIG_BACKLIGHT_RK29_BL
struct platform_device rk29_device_backlight = {
		.name	= "rk29_backlight",
		.id 	= -1,
        .dev    = {
           .platform_data  = &rk29_bl_info,
        }
};
#endif
#ifdef CONFIG_SDMMC0_RK29 
#ifndef CONFIG_EMMC_RK29 
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
#else
static struct resource resources_sdmmc0[] = {
	{
		.start 	= IRQ_EMMC,
		.end 	= IRQ_EMMC,
		.flags 	= IORESOURCE_IRQ,
	},
	{
		.start 	= RK29_EMMC_PHYS,   
		.end 	= RK29_EMMC_PHYS + RK29_EMMC_SIZE -1,
		.flags 	= IORESOURCE_MEM,
	}
};
#endif
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

/*
 * rk29xx spi master device
 */
static struct resource rk29_spi0_resources[] = {
	{
		.start	= IRQ_SPI0,
		.end	= IRQ_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_SPI0_PHYS,
		.end	= RK29_SPI0_PHYS + RK29_SPI0_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start  = DMACH_SPI0_TX,
		.end    = DMACH_SPI0_TX,
		.flags  = IORESOURCE_DMA,
	},
	{
		.start  = DMACH_SPI0_RX,
		.end    = DMACH_SPI0_RX,
		.flags  = IORESOURCE_DMA,
	},
};

struct platform_device rk29xx_device_spi0m = {
	.name	= "rk29xx_spim",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(rk29_spi0_resources),
	.resource	= rk29_spi0_resources,
	.dev			= {
		.platform_data	= &rk29xx_spi0_platdata,
	},
};

static struct resource rk29_spi1_resources[] = {
	{
		.start	= IRQ_SPI1,
		.end	= IRQ_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK29_SPI1_PHYS,
		.end	= RK29_SPI1_PHYS + RK29_SPI1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start  = DMACH_SPI1_TX,
		.end    = DMACH_SPI1_TX,
		.flags  = IORESOURCE_DMA,
	},
	{
		.start  = DMACH_SPI1_RX,
		.end    = DMACH_SPI1_RX,
		.flags  = IORESOURCE_DMA,
	},
};

struct platform_device rk29xx_device_spi1m = {
	.name	= "rk29xx_spim",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(rk29_spi1_resources),
	.resource	= rk29_spi1_resources,
	.dev			= {
		.platform_data	= &rk29xx_spi1_platdata,
	},
};

/* RK29 Camera :  ddl@rock-chips.com  */
#ifdef CONFIG_VIDEO_RK29
extern struct rk29camera_platform_data rk29_camera_platform_data;

static struct resource rk29_camera_resource[] = {
	[0] = {
		.start = RK29_VIP_PHYS,
		.end   = RK29_VIP_PHYS + RK29_VIP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_VIP,
		.end   = IRQ_VIP,
		.flags = IORESOURCE_IRQ,
	}
};

static u64 rockchip_device_camera_dmamask = 0xffffffffUL;

/*platform_device : */
struct platform_device rk29_device_camera = {
	.name		  = RK29_CAM_DRV_NAME,
	.id		  = RK29_CAM_PLATFORM_DEV_ID,               /* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk29_camera_resource),
	.resource	  = rk29_camera_resource,
	.dev            = {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data  = &rk29_camera_platform_data,
	}
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

#if defined(CONFIG_MTD_NAND_RK29XX)  
static struct resource rk29xxnand_resources[] = {
	{
		.start	= RK29_NANDC_PHYS,
		.end	= 	RK29_NANDC_PHYS+RK29_NANDC_SIZE -1,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device rk29xx_device_nand = {
	.name	= "rk29xxnand", 
	.id		=  -1, 
	.resource	= rk29xxnand_resources,
	.num_resources= ARRAY_SIZE(rk29xxnand_resources),
	.dev	= {
		.platform_data= &rk29_nand_data,
	},
	
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

#if defined(CONFIG_SND_RK29_SOC_I2S)
static struct resource rk29_iis_2ch_resource[] = {
        [0] = {
                .start = RK29_I2S_2CH_PHYS,
                .end   = RK29_I2S_2CH_PHYS + RK29_I2S_2CH_SIZE,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = DMACH_I2S_2CH_TX,
                .end   = DMACH_I2S_2CH_TX,
                .flags = IORESOURCE_DMA,
        },
        [2] = {
                .start = DMACH_I2S_2CH_RX,
                .end   = DMACH_I2S_2CH_RX,
                .flags = IORESOURCE_DMA,
        },
        [3] = {
                .start = IRQ_I2S_2CH,
                .end   = IRQ_I2S_2CH,
                .flags = IORESOURCE_IRQ,        
        },
};

struct platform_device rk29_device_iis_2ch = {
        .name           = "rk29_i2s",
        .id             = 1,
        .num_resources  = ARRAY_SIZE(rk29_iis_2ch_resource),
        .resource       = rk29_iis_2ch_resource,
};

static struct resource rk29_iis_8ch_resource[] = {
        [0] = {
                .start = RK29_I2S_8CH_PHYS,
                .end   = RK29_I2S_8CH_PHYS + RK29_I2S_8CH_SIZE,
                .flags = IORESOURCE_MEM,
        },
        [1] = {
                .start = DMACH_I2S_8CH_TX,
                .end   = DMACH_I2S_8CH_TX,
                .flags = IORESOURCE_DMA,
        },
        [2] = {
                .start = DMACH_I2S_8CH_RX,
                .end   = DMACH_I2S_8CH_RX,
                .flags = IORESOURCE_DMA,
        },
        [3] = {
                .start = IRQ_I2S_8CH,
                .end   = IRQ_I2S_8CH,
                .flags = IORESOURCE_IRQ,        
        },
};

struct platform_device rk29_device_iis_8ch = {
        .name           = "rk29_i2s",
        .id             = 0,
        .num_resources  = ARRAY_SIZE(rk29_iis_8ch_resource),
        .resource       = rk29_iis_8ch_resource,
};
#endif
#ifdef CONFIG_DWC_OTG
/*DWC_OTG*/
static struct resource dwc_otg_resource[] = {
	{
		.start = IRQ_USB_OTG0,
		.end   = IRQ_USB_OTG0,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK29_USBOTG0_PHYS,
		.end   = RK29_USBOTG0_PHYS + RK29_USBOTG0_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device rk29_device_dwc_otg = {
	.name		  = "dwc_otg",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(dwc_otg_resource),
	.resource	  = dwc_otg_resource,
};

static char *usb_functions_rockchip[] = {
	"usb_mass_storage",
};

static char *usb_functions_rockchip_adb[] = {
	"usb_mass_storage",
	"adb",
};

static char *usb_functions_rndis_rockchip[] = {
	"rndis",
	"usb_mass_storage",
};

static char *usb_functions_rndis_rockchip_adb[] = {
	"rndis",
	"usb_mass_storage",
	"adb",
};

#ifdef CONFIG_USB_ANDROID_DIAG
static char *usb_functions_adb_diag[] = {
	"usb_mass_storage",
	"adb",
	"diag",
};
#endif

static char *usb_functions_all[] = {
#ifdef CONFIG_USB_ANDROID_RNDIS
	"rndis",
#endif
	"usb_mass_storage",
#ifdef CONFIG_USB_ANDROID_ADB
	"adb",
#endif
#ifdef CONFIG_USB_ANDROID_ACM
	"acm",
#endif
#ifdef CONFIG_USB_ANDROID_DIAG
	"diag",
#endif
};

static struct android_usb_product usb_products[] = {
	{
		.product_id	= 0x2810,//0x0c02,//0x4e11,
		.num_functions	= ARRAY_SIZE(usb_functions_rockchip),
		.functions	= usb_functions_rockchip,
	},
	{
		.product_id	= 0x4e12,
		.num_functions	= ARRAY_SIZE(usb_functions_rockchip_adb),
		.functions	= usb_functions_rockchip_adb,
	},
	{
		.product_id	= 0x4e13,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis_rockchip),
		.functions	= usb_functions_rndis_rockchip,
	},
	{
		.product_id	= 0x4e14,
		.num_functions	= ARRAY_SIZE(usb_functions_rndis_rockchip_adb),
		.functions	= usb_functions_rndis_rockchip_adb,
	},
#ifdef CONFIG_USB_ANDROID_DIAG
	{
		.product_id	= 0x4e17,
		.num_functions	= ARRAY_SIZE(usb_functions_adb_diag),
		.functions	= usb_functions_adb_diag,
	},
#endif
};

static struct android_usb_platform_data android_usb_pdata = {
	.vendor_id	= 0x2207,//0x0bb4,//0x18d1,
	.product_id	= 0x2810,//0x4e11,
	.version	= 0x0100,
	.product_name		= "rk2818 sdk",
	.manufacturer_name	= "RockChip",
	.num_products = ARRAY_SIZE(usb_products),
	.products = usb_products,
	.num_functions = ARRAY_SIZE(usb_functions_all),
	.functions = usb_functions_all,
};

//static 
struct platform_device android_usb_device = {
	.name	= "android_usb",
	.id		= -1,
	.dev		= {
		.platform_data = &android_usb_pdata,
	},
};

//static 
struct platform_device usb_mass_storage_device = {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &mass_storage_pdata,
	},
};
#endif
