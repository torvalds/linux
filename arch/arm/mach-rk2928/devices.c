/* arch/arm/mach-rk2928/devices.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/pmu.h>
#include <mach/irqs.h>
#include <mach/board.h>
#include <mach/dma-pl330.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <plat/rk_fiq_debugger.h>

#ifdef CONFIG_ADC_RK30
static struct resource rk30_adc_resource[] = {
	{
		.start	= IRQ_SARADC,
		.end	= IRQ_SARADC,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2928_SARADC_PHYS,
		.end	= RK2928_SARADC_PHYS + RK2928_SARADC_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

struct platform_device device_adc = {
	.name		= "rk30-adc",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(rk30_adc_resource),
	.resource	= rk30_adc_resource,
};
#endif

static u64 dma_dmamask = DMA_BIT_MASK(32);

static struct resource resource_dmac[] = {
	[0] = {
		.start  = RK2928_DMAC_PHYS,
		.end    = RK2928_DMAC_PHYS + RK2928_DMAC_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC_0,
		.end	= IRQ_DMAC_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata dmac_pdata = {
	.peri = {
		[0] = DMACH_I2S0_8CH_TX,
		[1] = DMACH_I2S0_8CH_RX,
		[2] = DMACH_UART0_TX,
		[3] = DMACH_UART0_RX,
		[4] = DMACH_UART1_TX,
		[5] = DMACH_UART1_RX,
		[6] = DMACH_UART2_TX,
		[7] = DMACH_UART2_RX,
		[8] = DMACH_SPI0_TX,
		[9] = DMACH_SPI0_RX,
		[10] = DMACH_SDMMC,
		[11] = DMACH_SDIO,
		[12] = DMACH_EMMC,
		[13] = DMACH_DMAC1_MEMTOMEM,
		[14] = DMACH_MAX,
		[15] = DMACH_MAX,
		[16] = DMACH_MAX,
		[17] = DMACH_MAX,
		[18] = DMACH_MAX,
		[19] = DMACH_MAX,
		[20] = DMACH_MAX,
		[21] = DMACH_MAX,
		[22] = DMACH_MAX,
		[23] = DMACH_MAX,
		[24] = DMACH_MAX,
		[25] = DMACH_MAX,
		[26] = DMACH_MAX,
		[27] = DMACH_MAX,
		[28] = DMACH_MAX,
		[29] = DMACH_MAX,
		[30] = DMACH_MAX,
		[31] = DMACH_MAX,
	},
};

static struct platform_device device_dmac = {
	.name		= "rk29-pl330",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_dmac),
	.resource	= resource_dmac,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &dmac_pdata,
	},
};

static struct platform_device *rk2928_dmacs[] __initdata = {
	&device_dmac,
};

static void __init rk2928_init_dma(void)
{
	platform_add_devices(rk2928_dmacs, ARRAY_SIZE(rk2928_dmacs));
}

#ifdef CONFIG_UART0_RK29
static struct resource resources_uart0[] = {
	{
		.start	= IRQ_UART0,
		.end	= IRQ_UART0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2928_UART0_PHYS,
		.end	= RK2928_UART0_PHYS + RK2928_UART0_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_uart0 = {
	.name	= "rk_serial",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_uart0),
	.resource	= resources_uart0,
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
		.start	= RK2928_UART1_PHYS,
		.end	= RK2928_UART1_PHYS + RK2928_UART1_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_uart1 = {
	.name	= "rk_serial",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_uart1),
	.resource	= resources_uart1,
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
		.start	= RK2928_UART2_PHYS,
		.end	= RK2928_UART2_PHYS + RK2928_UART2_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_uart2 = {
	.name	= "rk_serial",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_uart2),
	.resource	= resources_uart2,
};
#endif

static void __init rk2928_init_uart(void)
{
#ifdef CONFIG_UART0_RK29
	platform_device_register(&device_uart0);
#endif
#ifdef CONFIG_UART1_RK29
	platform_device_register(&device_uart1);
#endif
#ifdef CONFIG_UART2_RK29
	platform_device_register(&device_uart2);
#endif
}


//LCDC
#ifdef CONFIG_LCDC_RK2928
extern struct rk29fb_info lcdc_screen_info;
static struct resource resource_lcdc[] = {
	[0] = {
		.name  = "lcdc reg",
		.start = RK2928_LCDC_PHYS,
		.end   = RK2928_LCDC_PHYS + RK2928_LCDC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	
	[1] = {
		.name  = "lcdc irq",
		.start = IRQ_LCDC,
		.end   = IRQ_LCDC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_lcdc = {
	.name		  = "rk2928-lcdc",
	.id		  = 0,
	.num_resources	  = ARRAY_SIZE(resource_lcdc),
	.resource	  = resource_lcdc,
	.dev 		= {
		.platform_data = &lcdc_screen_info,
	},
};
#endif

// i2c
#ifdef CONFIG_I2C0_CONTROLLER_RK29
#define I2C0_ADAP_TYPE  I2C_RK29_ADAP
#define I2C0_START      RK2928_I2C0_PHYS
#define I2C0_END        RK2928_I2C0_PHYS + RK2928_I2C0_SIZE - 1
#endif
#ifdef CONFIG_I2C0_CONTROLLER_RK30
#define I2C0_ADAP_TYPE   I2C_RK30_ADAP
#define I2C0_START      RK2928_RKI2C0_PHYS
#define I2C0_END        RK2928_RKI2C0_PHYS + RK2928_RKI2C0_SIZE - 1
#endif

#ifdef CONFIG_I2C1_CONTROLLER_RK29
#define I2C1_ADAP_TYPE  I2C_RK29_ADAP
#define I2C1_START      RK2928_I2C1_PHYS
#define I2C1_END        RK2928_I2C1_PHYS + RK2928_I2C1_SIZE - 1
#endif
#ifdef CONFIG_I2C1_CONTROLLER_RK30
#define I2C1_ADAP_TYPE   I2C_RK30_ADAP
#define I2C1_START      RK2928_RKI2C1_PHYS 
#define I2C1_END        RK2928_RKI2C1_PHYS + RK2928_RKI2C1_SIZE - 1
#endif

#ifdef CONFIG_I2C2_CONTROLLER_RK29
#define I2C2_ADAP_TYPE  I2C_RK29_ADAP
#define I2C2_START      RK2928_I2C2_PHYS
#define I2C2_END        RK2928_I2C2_PHYS + RK2928_I2C2_SIZE - 1
#endif
#ifdef CONFIG_I2C2_CONTROLLER_RK30
#define I2C2_ADAP_TYPE   I2C_RK30_ADAP
#define I2C2_START      RK2928_RKI2C2_PHYS
#define I2C2_END        RK2928_RKI2C2_PHYS + RK2928_RKI2C2_SIZE - 1
#endif

#ifdef CONFIG_I2C3_CONTROLLER_RK29
#define I2C3_ADAP_TYPE  I2C_RK29_ADAP
#define I2C3_START      RK2928_I2C3_PHYS
#define I2C3_END        RK2928_I2C3_PHYS + RK2928_I2C3_SIZE - 1
#endif
#ifdef CONFIG_I2C3_CONTROLLER_RK30
#define I2C3_ADAP_TYPE   I2C_RK30_ADAP
#define I2C3_START      RK2928_RKI2C3_PHYS
#define I2C3_END        RK2928_RKI2C3_PHYS + RK2928_RKI2C3_SIZE - 1
#endif

#ifdef CONFIG_I2C0_RK30
static struct rk30_i2c_platform_data default_i2c0_data = {
	.bus_num = 0,
	.is_div_from_arm = 1,
	.adap_type = I2C0_ADAP_TYPE,
};

static struct resource resources_i2c0[] = {
	{
		.start	= IRQ_I2C0,
		.end	= IRQ_I2C0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= I2C0_START,
        .end    = I2C0_END,    
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_i2c0 = {
	.name	= "rk30_i2c",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_i2c0),
	.resource	= resources_i2c0,
	.dev 		= {
		.platform_data = &default_i2c0_data,
	},
};
#endif

#ifdef CONFIG_I2C1_RK30
static struct rk30_i2c_platform_data default_i2c1_data = {
	.bus_num = 1,
	.is_div_from_arm = 1,
	.adap_type = I2C1_ADAP_TYPE,
};

static struct resource resources_i2c1[] = {
	{
		.start	= IRQ_I2C1,
		.end	= IRQ_I2C1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= I2C1_START,
        .end    = I2C1_END,    
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_i2c1 = {
	.name	= "rk30_i2c",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_i2c1),
	.resource	= resources_i2c1,
	.dev 		= {
		.platform_data = &default_i2c1_data,
	},
};
#endif

#ifdef CONFIG_I2C2_RK30
static struct rk30_i2c_platform_data default_i2c2_data = {
	.bus_num = 2,
	.is_div_from_arm = 0,
	.adap_type = I2C2_ADAP_TYPE,
};

static struct resource resources_i2c2[] = {
	{
		.start	= IRQ_I2C2,
		.end	= IRQ_I2C2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= I2C2_START,
        .end    = I2C2_END,    
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_i2c2 = {
	.name	= "rk30_i2c",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_i2c2),
	.resource	= resources_i2c2,
	.dev 		= {
		.platform_data = &default_i2c2_data,
	},
};
#endif

#ifdef CONFIG_I2C3_RK30
static struct rk30_i2c_platform_data default_i2c3_data = {
	.bus_num = 3,
	.is_div_from_arm = 0,
	.adap_type = I2C3_ADAP_TYPE,
};

static struct resource resources_i2c3[] = {
	{
		.start	= IRQ_I2C3,
		.end	= IRQ_I2C3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= I2C3_START,
        .end    = I2C3_END,    
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_i2c3 = {
	.name	= "rk30_i2c",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_i2c3),
	.resource	= resources_i2c3,
	.dev 		= {
		.platform_data = &default_i2c3_data,
	},
};
#endif

#ifdef CONFIG_I2C_GPIO_RK30
static struct platform_device device_i2c_gpio = {
        .name   = "i2c-gpio",
        .id = 4,
        .dev            = {
                .platform_data = &default_i2c_gpio_data,
        },
};
#endif

static void __init rk2928_init_i2c(void)
{
#ifdef CONFIG_I2C0_RK30
	platform_device_register(&device_i2c0);
#endif
#ifdef CONFIG_I2C1_RK30
	platform_device_register(&device_i2c1);
#endif
#ifdef CONFIG_I2C2_RK30
	platform_device_register(&device_i2c2);
#endif
#ifdef CONFIG_I2C3_RK30
	platform_device_register(&device_i2c3);
#endif
#ifdef CONFIG_I2C_GPIO_RK30
	platform_device_register(&device_i2c_gpio);
#endif
}
//end of i2c

#if defined(CONFIG_SPIM0_RK29) || defined(CONFIG_SPIM1_RK29)
/*****************************************************************************************
 * spi devices
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define SPI_CHIPSELECT_NUM 2

static int spi_io_init(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	int i;
	if (cs_gpios) {
		for (i = 0; i < cs_num; i++) {
			rk30_mux_api_set(cs_gpios[i].cs_iomux_name, cs_gpios[i].cs_iomux_mode);
		}
	}
	return 0;
}

static int spi_io_deinit(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	return 0;
}

static int spi_io_fix_leakage_bug(void)
{
#if 0
	gpio_direction_output(RK29_PIN2_PC1, GPIO_LOW);
#endif
	return 0;
}

static int spi_io_resume_leakage_bug(void)
{
#if 0
	gpio_direction_output(RK29_PIN2_PC1, GPIO_HIGH);
#endif
	return 0;
}
#endif

/*
 * rk29xx spi master device
 */
#ifdef CONFIG_SPIM0_RK29
static struct spi_cs_gpio rk29xx_spi0_cs_gpios[SPI_CHIPSELECT_NUM] = {
	{
		.name = "spi0 cs0",
		.cs_gpio = RK2928_PIN1_PB3,
		.cs_iomux_name = GPIO1B3_SPI_CSN0_UART1_RTSN_NAME,
		.cs_iomux_mode = GPIO1B_SPI_CSN0,
	},
	{
		.name = "spi0 cs1",
		.cs_gpio = RK2928_PIN1_PB4,
		.cs_iomux_name = GPIO1B4_SPI_CSN1_UART1_CTSN_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO1B_SPI_CSN1,
	},
};

static struct rk29xx_spi_platform_data rk29xx_spi0_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi0_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

static struct resource rk29_spi0_resources[] = {
	{
		.start	= IRQ_SPI,
		.end	= IRQ_SPI,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK2928_SPI_PHYS,
		.end	= RK2928_SPI_PHYS + RK2928_SPI_SIZE - 1,
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
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data	= &rk29xx_spi0_platdata,
	},
};
#endif

static void __init rk2928_init_spim(void)
{
#ifdef CONFIG_SPIM0_RK29
	platform_device_register(&rk29xx_device_spi0m);
#endif
}

#ifdef CONFIG_HDMI_RK2928
static struct resource resource_hdmi[] = {
	[0] = {
		.start = RK2928_HDMI_PHYS,
		.end   = RK2928_HDMI_PHYS + RK2928_HDMI_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_HDMI,
		.end   = IRQ_HDMI,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_hdmi = {
	.name				= "rk2928-hdmi",
	.id					= -1,
	.num_resources		= ARRAY_SIZE(resource_hdmi),
	.resource			= resource_hdmi,
};
#endif
#ifdef CONFIG_RGA_RK30
static struct resource resource_rga[] = {
	[0] = {
		.start = RK2928_RGA_PHYS,
		.end   = RK2928_RGA_PHYS + RK2928_RGA_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_RGA,
		.end   = IRQ_RGA,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device device_rga = {
	.name		= "rga",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resource_rga),
	.resource	= resource_rga,
};
#endif
#ifdef CONFIG_SND_RK29_SOC_I2S
#ifdef CONFIG_SND_RK29_SOC_I2S_8CH
static struct resource resource_iis0_8ch[] = {
	[0] = {
		.start	= RK2928_I2S_PHYS,
		.end	= RK2928_I2S_PHYS + RK2928_I2S_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= DMACH_I2S0_8CH_TX,
		.end	= DMACH_I2S0_8CH_TX,
		.flags	= IORESOURCE_DMA,
	},
	[2] = {
		.start	= DMACH_I2S0_8CH_RX,
		.end	= DMACH_I2S0_8CH_RX,
		.flags	= IORESOURCE_DMA,
	},
	[3] = {
		.start	= IRQ_I2S,
		.end	= IRQ_I2S,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device device_iis0_8ch = {
	.name		= "rk29_i2s",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(resource_iis0_8ch),
	.resource	= resource_iis0_8ch,
};
#endif
#endif
static struct platform_device device_pcm = {
	.name = "rockchip-audio",
	.id = -1,
};

static void __init rk2928_init_i2s(void)
{
#ifdef CONFIG_SND_RK29_SOC_I2S_8CH
	platform_device_register(&device_iis0_8ch);
#endif
	platform_device_register(&device_pcm);
}
#ifdef CONFIG_KEYS_RK29
extern struct rk29_keys_platform_data rk29_keys_pdata;
static struct platform_device device_keys = {
	.name		= "rk29-keypad",
	.id		= -1,
	.dev		= {
		.platform_data	= &rk29_keys_pdata,
	},
};
#endif

#ifdef CONFIG_USB20_OTG
/*DWC_OTG*/
static struct resource usb20_otg_resource[] = {
	{
		.start = IRQ_USB_OTG,
		.end   = IRQ_USB_OTG,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start = RK2928_USBOTG20_PHYS,
		.end   = RK2928_USBOTG20_PHYS + RK2928_USBOTG20_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},

};

struct platform_device device_usb20_otg = {
	.name		  = "usb20_otg",
	.id		  = -1,
	.num_resources	  = ARRAY_SIZE(usb20_otg_resource),
	.resource	  = usb20_otg_resource,
};
#endif
#ifdef CONFIG_USB20_HOST
static struct resource usb20_host_resource[] = {
    {
        .start = IRQ_USB_HOST,
        .end   = IRQ_USB_HOST,
        .flags = IORESOURCE_IRQ,
    },
    {
        .start = RK2928_USBHOST20_PHYS,
        .end   = RK2928_USBHOST20_PHYS + RK2928_USBHOST20_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },

};

struct platform_device device_usb20_host = {
    .name             = "usb20_host",
    .id               = -1,
    .num_resources    = ARRAY_SIZE(usb20_host_resource),
    .resource         = usb20_host_resource,
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
		.start 	= RK2928_SDMMC_PHYS,
		.end 	= RK2928_SDMMC_PHYS + RK2928_SDMMC_SIZE -1,
		.flags 	= IORESOURCE_MEM,
	}
};

static struct platform_device device_sdmmc0 = {
	.name		= "rk29_sdmmc",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(resources_sdmmc0),
	.resource	= resources_sdmmc0,
	.dev 		= {
		.platform_data = &default_sdmmc0_data,
	},
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
		.start 	= RK2928_SDIO_PHYS,
		.end 	= RK2928_SDIO_PHYS + RK2928_SDIO_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	}
};

static struct platform_device device_sdmmc1 = {
	.name		= "rk29_sdmmc",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(resources_sdmmc1),
	.resource	= resources_sdmmc1,
	.dev 		= {
		.platform_data = &default_sdmmc1_data,
	},
};
#endif
static void __init rk2928_init_sdmmc(void)
{
#ifdef CONFIG_SDMMC0_RK29
	platform_device_register(&device_sdmmc0);
#endif
#ifdef CONFIG_SDMMC1_RK29
	platform_device_register(&device_sdmmc1);
#endif
}

#ifdef CONFIG_SND_SOC_RK2928
static struct resource resources_acodec[] = {
	{
		.start 	= RK2928_ACODEC_PHYS,
		.end 	= RK2928_ACODEC_PHYS + RK2928_ACODEC_SIZE - 1,
		.flags 	= IORESOURCE_MEM,
	},
};

static struct platform_device device_acodec = {
	.name	= "rk2928-codec",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(resources_acodec),
	.resource	= resources_acodec,
};
#endif

static int __init rk2928_init_devices(void)
{
	rk2928_init_dma();
	rk2928_init_uart();
	rk2928_init_i2c();
	rk2928_init_spim();
#ifdef CONFIG_ADC_RK30
	platform_device_register(&device_adc);
#endif
#ifdef CONFIG_KEYS_RK29
	platform_device_register(&device_keys);
#endif
#ifdef CONFIG_RGA_RK30
	platform_device_register(&device_rga);
#endif
#ifdef CONFIG_LCDC_RK2928
	platform_device_register(&device_lcdc);
#endif
#ifdef CONFIG_USB20_OTG
	platform_device_register(&device_usb20_otg);
#endif
#ifdef CONFIG_USB20_HOST
	platform_device_register(&device_usb20_host);
#endif
	rk2928_init_sdmmc();
#if defined(CONFIG_FIQ_DEBUGGER) && defined(DEBUG_UART_PHYS)
	rk_serial_debug_init(DEBUG_UART_BASE, IRQ_DEBUG_UART, IRQ_UART_SIGNAL, -1);
#endif
	rk2928_init_i2s();
#ifdef CONFIG_SND_SOC_RK2928
	platform_device_register(&device_acodec);
#endif
#ifdef CONFIG_HDMI_RK2928
	platform_device_register(&device_hdmi);
#endif
	return 0;
}
arch_initcall(rk2928_init_devices);
