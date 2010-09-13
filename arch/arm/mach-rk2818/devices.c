/* arch/arm/mach-rk2818/devices.c
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
#include <linux/android_pmem.h>
#include <linux/usb/android_composite.h>
#include <linux/delay.h>
#include <mach/irqs.h>
#include <mach/rk2818_iomap.h>
#include "devices.h"

#include <asm/mach/flash.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <mach/gpio.h>
#include <mach/rk2818_nand.h>
#include <mach/iomux.h>
#include <mach/rk2818_camera.h>                          /* ddl@rock-chips.com : camera support */
#include <linux/i2c.h>  
#include <linux/miscdevice.h>
#include <linux/circ_buf.h>
#include <mach/spi_fpga.h>                                    
#include <media/soc_camera.h>
#include "../../../drivers/staging/android/timed_gpio.h"
static struct resource resources_sdmmc0[] = {
	{
		.start 	= IRQ_NR_SDMMC0,
		.end 	= IRQ_NR_SDMMC0,
		.flags 	= IORESOURCE_IRQ,
	},
	{
		.start 	= RK2818_SDMMC0_PHYS,
		.end 	= RK2818_SDMMC0_PHYS + SZ_8K -1,
		.flags 	= IORESOURCE_MEM,
	}
};
static struct resource resources_sdmmc1[] = {
	{
		.start 	= IRQ_NR_SDMMC1,
		.end 	= IRQ_NR_SDMMC1,
		.flags 	= IORESOURCE_IRQ,
	},
	{
		.start 	= RK2818_SDMMC1_PHYS,
		.end 	= RK2818_SDMMC1_PHYS + SZ_8K -1,
		.flags 	= IORESOURCE_MEM,
	}
};


static struct resource resources_i2c0[] = {
	{
		.start	= IRQ_NR_I2C0,
		.end	= IRQ_NR_I2C0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_I2C0_PHYS,
		.end	= RK2818_I2C0_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_i2c1[] = {
	{
		.start	= IRQ_NR_I2C1,
		.end	= IRQ_NR_I2C1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_I2C1_PHYS,
		.end	= RK2818_I2C1_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
/*
 * rk2818 4 uarts device
 */
static struct resource resources_uart0[] = {
	{
		.start	= IRQ_NR_UART0,
		.end	= IRQ_NR_UART0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_UART0_PHYS,
		.end	= RK2818_UART0_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_uart1[] = {
	{
		.start	= IRQ_NR_UART1,
		.end	= IRQ_NR_UART1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_UART1_PHYS,
		.end	= RK2818_UART1_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_uart2[] = {
	{
		.start	= IRQ_NR_UART2,
		.end	= IRQ_NR_UART2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_UART2_PHYS,
		.end	= RK2818_UART2_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
static struct resource resources_uart3[] = {
	{
		.start	= IRQ_NR_UART3,
		.end	= IRQ_NR_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_UART3_PHYS,
		.end	= RK2818_UART3_PHYS + SZ_1K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
/* sdmmc */
struct platform_device rk2818_device_sdmmc0 = {
	.name			= "rk2818_sdmmc",
	.id				= 0,
	.num_resources	= ARRAY_SIZE(resources_sdmmc0),
	.resource		= resources_sdmmc0,
	.dev 			= {
		.platform_data = &default_sdmmc0_data,
	},
};
struct platform_device rk2818_device_sdmmc1 = {
	.name			= "rk2818_sdmmc",
	.id				= 1,
	.num_resources	= ARRAY_SIZE(resources_sdmmc1),
	.resource		= resources_sdmmc1,
	.dev 			= {
		.platform_data = &default_sdmmc1_data,
	},
};

struct platform_device rk2818_device_i2c0 = {
	.name	= "rk2818_i2c",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_i2c0),
	.resource	= resources_i2c0,
	.dev 			= {
		.platform_data = &default_i2c0_data,
	},
};
struct platform_device rk2818_device_i2c1 = {
	.name	= "rk2818_i2c",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_i2c1),
	.resource	= resources_i2c1,
	.dev 			= {
		.platform_data = &default_i2c1_data,
	},
};
#ifdef CONFIG_SPI_FPGA_I2C
struct platform_device rk2818_device_i2c2 = {
	.name	= "fpga_i2c",
	.id	= 2,	
	.dev 			= {
		.platform_data = &default_i2c2_data,
	},
};
struct platform_device rk2818_device_i2c3 = {
	.name	= "fpga_i2c",
	.id	= 3,	
	.dev 			= {
		.platform_data = &default_i2c3_data,
	},
};
#endif
#ifdef CONFIG_UART0_RK2818
struct platform_device rk2818_device_uart0 = {
	.name	= "rk2818_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart0),
	.resource	= resources_uart0,
	.dev = {
		.platform_data = &rk2818_serial0_platdata,
	},
};
#endif
#ifdef CONFIG_UART1_RK2818
struct platform_device rk2818_device_uart1 = {
	.name	= "rk2818_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
};
#endif
#ifdef CONFIG_UART2_RK2818
struct platform_device rk2818_device_uart2 = {
	.name	= "rk2818_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
	.dev = {
		.platform_data = &rk2818_serial2_platdata,
	},
};
#endif
#ifdef CONFIG_UART3_RK2818
struct platform_device rk2818_device_uart3 = {
	.name	= "rk2818_serial",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};
#endif
/*
 * rk2818 spi master device
 */
static struct resource resources_spim[] = {
	{
		.start	= IRQ_NR_SPIM,
		.end	= IRQ_NR_SPIM,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_SPIMASTER_PHYS,
		.end	= RK2818_SPIMASTER_PHYS + SZ_4K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
struct platform_device rk2818_device_spim = {
	.name	= "rk2818_spim",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_spim),
	.resource	= resources_spim,
	.dev			= {
		.platform_data	= &rk2818_spi_platdata,
	},
};

/* rk2818 fb resource */
static struct resource rk2818_fb_resource[] = {
	[0] = {
		.start = RK2818_LCDC_PHYS,
		.end   = RK2818_LCDC_PHYS + RK2818_LCDC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_NR_LCDC,
		.end   = IRQ_NR_LCDC,
		.flags = IORESOURCE_IRQ,
	},	
};

/*platform_device*/
extern struct rk2818fb_info rk2818_fb_info;

struct platform_device rk2818_device_fb = {
	.name		  = "rk2818-fb",
	.id		  = 4,
	.num_resources	  = ARRAY_SIZE(rk2818_fb_resource),
	.resource	  = rk2818_fb_resource,
	.dev            = {
		.platform_data  = &rk2818_fb_info,
	}
};

/***********************************************************
*	  backlight
*	author :nzy zhongyw
*	data:2010-05-18
***************************************************************/
struct platform_device rk2818_device_backlight = {
		.name	= "rk2818_backlight",
		.id 	= -1,
        .dev    = {
           .platform_data  = &rk2818_bl_info,
        }
};

/* RK2818 Camera :  ddl@rock-chips.com  */
#ifdef CONFIG_VIDEO_RK2818

static struct resource rk2818_camera_resource[] = {
	[0] = {
		.start = RK2818_VIP_PHYS,
		.end   = RK2818_VIP_PHYS + RK2818_VIP_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_NR_VIP,
		.end   = IRQ_NR_VIP,
		.flags = IORESOURCE_IRQ,
	},
};

static u64 rockchip_device_camera_dmamask = 0xffffffffUL;

/*platform_device : */
struct platform_device rk2818_device_camera = {
	.name		  = RK28_CAM_DRV_NAME,
	.id		  = RK28_CAM_PLATFORM_DEV_ID,               /* This is used to put cameras on this interface */
	.num_resources	  = ARRAY_SIZE(rk2818_camera_resource),
	.resource	  = rk2818_camera_resource,
	.dev            = {
		.dma_mask = &rockchip_device_camera_dmamask,
		.coherent_dma_mask = 0xffffffffUL,
		.platform_data  = &rk28_camera_platform_data,
	}
};
extern struct platform_device rk2818_soc_camera_pdrv;
#endif

/*ADC*/
static struct resource rk2818_adc_resource[] = {
	{
		.start = IRQ_NR_ADC,
		.end   = IRQ_NR_ADC,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK2818_ADC_PHYS,
		.end   = RK2818_ADC_PHYS + RK2818_ADC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device rk2818_device_adc = {
	.name		  = "rk2818-adc",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(rk2818_adc_resource),
	.resource	  = rk2818_adc_resource,
};


struct platform_device rk2818_device_adckey = {
	.name		= "rk2818-adckey",
	.id		= -1,
	.dev.parent	= &rk2818_device_adc.dev,
	.dev.platform_data = &rk2818_adckey_platdata,
};

/*
 *rk2818 i2s
 */
static struct resource resources_i2s[] = {
	{
		.start	= IRQ_NR_I2S,
		.end	= IRQ_NR_I2S,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2818_I2S_PHYS,
		.end	= RK2818_I2S_PHYS + SZ_8K - 1,
		.flags	= IORESOURCE_MEM,
	},
};
struct platform_device rk2818_device_i2s = {
	.name	= "rk2818_i2s",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_i2s),
	.resource	= resources_i2s,
	.dev = {
		.platform_data = &rk2818_i2s_platdata,
	},
};

struct platform_device rk2818_device_battery = {
		.name	= "rk2818-battery",
		.id 	= -1,
		.dev = {
			.platform_data = &rk2818_battery_platdata,
		},
};

/*
 * rk2818 dsp device
 */
 static struct resource resources_dsp[] = {
        [0] = {
                .start = RK2818_DSP_PHYS,
                .end   = RK2818_DSP_PHYS + 0x5fffff,
                .flags = IORESOURCE_DMA,
        },
        [1] = {
                .start  = IRQ_NR_PIUCMD,
                .end    = IRQ_NR_PIUCMD,
                .flags  = IORESOURCE_IRQ,
        },
        [2] = {
                .start  = IRQ_NR_DSPSWI,
                .end    = IRQ_NR_DSPSWI,
                .flags  = IORESOURCE_IRQ,
        },
};
static u64 rk2818_device_dsp_dmamask = 0xffffffffUL;
struct platform_device rk2818_device_dsp = {
        .name             = "rk28-dsp",
        .id               = 0,
        .num_resources    = ARRAY_SIZE(resources_dsp),
        .resource         = resources_dsp,
        .dev              = {
                .dma_mask = &rk2818_device_dsp_dmamask,
                .coherent_dma_mask = 0xffffffffUL
        }
};

#if defined(CONFIG_ANDROID_PMEM)

static struct android_pmem_platform_data pmem_pdata = {
	.name = "pmem",
	.no_allocator = 1,
	.cached = 0,
	.start = 0x6f000000,
	.size =  0x1000000,
};

static struct android_pmem_platform_data pmem_pdata_dsp = {
	.name = "pmem-dsp",
	.no_allocator = 0,                  
	.cached = 0,
    .start = 0x6db00000,
	.size =  0x1500000,
};

struct platform_device rk2818_device_pmem = {
	.name = "android_pmem",
	.id = 0,
	.dev = { .platform_data = &pmem_pdata },
};

struct platform_device rk2818_device_pmem_dsp = {
	.name = "android_pmem",
	.id = 1,
	.dev = { .platform_data = &pmem_pdata_dsp },
};

#endif
#if defined(CONFIG_MTD_NAND_RK2818)  
static struct resource nand_resources[] = {
	{
		.start	= RK2818_NANDC_PHYS,
		.end	= 	RK2818_NANDC_PHYS+RK2818_NANDC_SIZE -1,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device rk2818_nand_device = {
	.name	= "rk2818-nand",
	.id		=  -1, 
	.resource	= nand_resources,
	.num_resources= ARRAY_SIZE(nand_resources),
	.dev	= {
		.platform_data= &rk2818_nand_data,
	},
	
};

#endif

#if defined(CONFIG_INPUT_JOGBALL)
struct platform_device rk2818_jogball_device = {
		.name	= "rk2818_jogball",
		.id 	= -1,
};
#endif

/*DWC_OTG*/
static struct resource dwc_otg_resource[] = {
	{
		.start = IRQ_NR_OTG,
		.end   = IRQ_NR_OTG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK2818_USBOTG_PHYS,
		.end   = RK2818_USBOTG_PHYS + RK2818_USBOTG_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device rk2818_device_dwc_otg = {
	.name		  = "dwc_otg",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(dwc_otg_resource),
	.resource	  = dwc_otg_resource,
};
#ifdef CONFIG_RK2818_HOST11
static struct resource rk2818_host11_resource[] = {
	{
		.start = IRQ_NR_USB_HOST,
		.end   = IRQ_NR_USB_HOST,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK2818_USBHOST_PHYS,
		.end   = RK2818_USBHOST_PHYS + RK2818_USBHOST_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device rk2818_device_host11 = {
	.name		  = "rk2818_host11",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(rk2818_host11_resource),
	.resource	  = rk2818_host11_resource,
};
#endif
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

static struct usb_mass_storage_platform_data mass_storage_pdata = {
	.nluns		= 2,
	.vendor		= "RockChip",
	.product	= "rk2818 sdk",
	.release	= 0x0100,
};

//static 
struct platform_device usb_mass_storage_device = {
	.name	= "usb_mass_storage",
	.id	= -1,
	.dev	= {
		.platform_data = &mass_storage_pdata,
	},
};

#if CONFIG_ANDROID_TIMED_GPIO
struct platform_device rk28_device_vibrator ={
	.name = "timed-gpio",
	.id = -1,
	.dev = {
		.platform_data = &rk28_vibrator_info,
		},

};
#endif 

