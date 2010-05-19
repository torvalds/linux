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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/android_pmem.h>

#include <mach/irqs.h>
#include <mach/rk2818_iomap.h>
#include "devices.h"

#include <asm/mach/flash.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <linux/dm9000.h>
#include <mach/gpio.h>

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

struct platform_device rk2818_device_uart0 = {
	.name	= "rk2818_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart0),
	.resource	= resources_uart0,
};
struct platform_device rk2818_device_uart1 = {
	.name	= "rk2818_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
};
struct platform_device rk2818_device_uart2 = {
	.name	= "rk2818_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};
struct platform_device rk2818_device_uart3 = {
	.name	= "rk2818_serial",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};

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
extern struct rk2818_fb_mach_info rk2818_fb_mach_info;

struct platform_device rk2818_device_fb = {
	.name		  = "rk2818-fb",
	.id		  = 4,
	.num_resources	  = ARRAY_SIZE(rk2818_fb_resource),
	.resource	  = rk2818_fb_resource,
	.dev            = {
		.platform_data  = &rk2818_fb_mach_info,
	}
};

/***********************************************************
*	  backlight
*	author :nzy zhongyw
*	data:2010-05-18
***************************************************************/
extern struct rk2818bl_info rk2818_bl_info;

struct platform_device rk2818_device_backlight = {
		.name	= "rk2818_backlight",
		.id 	= -1,
        .dev    = {
           .platform_data  = &rk2818_bl_info,
        }
};

//net device
/* DM9000 */
static struct resource dm9k_resource[] = {
	[0] = {
		.start = RK2818_NANDC_PHYS + 0x800 + (1*0x100 + 0x8),    //nand_cs1+nand_cmd
		.end   = RK2818_NANDC_PHYS + 0x800 + (1*0x100 + 0x8) + 3,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = RK2818_NANDC_PHYS + 0x800 + (1*0x100 + 0x4),	//nand_cs1+nand_data
		.end   = RK2818_NANDC_PHYS + 0x800 + (1*0x100 + 0x4) + 3,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = RK2818_PIN_PE2,	//use pe2 as interrupt
		.end   = RK2818_PIN_PE2,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	}

};

/* for the moment we limit ourselves to 8bit IO until some
 * better IO routines can be written and tested
*/

static struct dm9000_plat_data dm9k_platdata = {
	.flags		= DM9000_PLATF_8BITONLY,
};

struct platform_device rk2818_device_dm9k = {
	.name		= "dm9000",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(dm9k_resource),
	.resource	= dm9k_resource,
	.dev		= {
		.platform_data = &dm9k_platdata,
	}
};

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
};

struct platform_device rk2818_device_battery = {
		.name	= "rk2818-battery",
		.id 	= -1,
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
	.cached = 1,
	.start = 0x6f000000,
	.size =  0x1000000,
	//.start = 0x67000000,
	//.size =  0x1000000,
};

static struct android_pmem_platform_data pmem_pdata_dsp = {
	.name = "pmem-dsp",
	.no_allocator = 1,
	.cached = 0,
    .start = 0x66B00000,
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

