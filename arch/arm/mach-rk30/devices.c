/* arch/arm/mach-rk30/devices.c
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
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/pmu.h>
#include <mach/irqs.h>
#include <mach/board.h>
#include <plat/dma-pl330.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
static u64 dma_dmamask = DMA_BIT_MASK(32);

static struct resource resource_dmac1[] = {
	[0] = {
		.start  = RK30_DMACS1_PHYS,
		.end    = RK30_DMACS1_PHYS + RK30_DMACS1_SIZE -1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC1_0,
		.end	= IRQ_DMAC1_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata dmac1_pdata = {
	.peri = {
		[0] = DMACH_UART0_TX,
		[1] = DMACH_UART0_RX,
		[2] = DMACH_UART1_TX,
		[3] = DMACH_UART1_RX,
		[4] = DMACH_I2S0_8CH_TX,
		[5] = DMACH_I2S0_8CH_RX,
		[6] = DMACH_I2S1_2CH_TX,
		[7] = DMACH_I2S1_2CH_RX,
		[8] = DMACH_SPDIF_TX,
		[9] = DMACH_I2S2_2CH_TX,
		[10] = DMACH_I2S2_2CH_RX,
		[11] = DMACH_MAX,
		[12] = DMACH_MAX,
		[13] = DMACH_MAX,
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

static struct platform_device device_dmac1 = {
	.name		= "rk29-pl330",
	.id		= 1,
	.num_resources	= ARRAY_SIZE(resource_dmac1),
	.resource	= resource_dmac1,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &dmac1_pdata,
	},
};

static struct resource resource_dmac2[] = {
	[0] = {
		.start  = RK30_DMAC2_PHYS,
		.end    = RK30_DMAC2_PHYS + RK30_DMAC2_SIZE - 1,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start	= IRQ_DMAC2_0,
		.end	= IRQ_DMAC2_1,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct rk29_pl330_platdata dmac2_pdata = {
	.peri = {
		[0] = DMACH_HSADC,
		[1] = DMACH_SDMMC,
		[2] = DMACH_MAX,
		[3] = DMACH_SDIO,
		[4] = DMACH_EMMC,
		[5] = DMACH_PID_FILTER,
		[6] = DMACH_UART2_TX,
		[7] = DMACH_UART2_RX,
		[8] = DMACH_UART3_TX,
		[9] = DMACH_UART3_RX,
		[10] = DMACH_SPI0_TX,
		[11] = DMACH_SPI0_RX,
		[12] = DMACH_SPI1_TX,
		[13] = DMACH_SPI1_RX,
		[14] = DMACH_DMAC2_MEMTOMEM,
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

static struct platform_device device_dmac2 = {
	.name		= "rk29-pl330",
	.id		= 2,
	.num_resources	= ARRAY_SIZE(resource_dmac2),
	.resource	= resource_dmac2,
	.dev		= {
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data = &dmac2_pdata,
	},
};

static struct platform_device *rk30_dmacs[] __initdata = {
	&device_dmac1,
	&device_dmac2,
};

static void __init rk30_init_dma(void)
{
	platform_add_devices(rk30_dmacs, ARRAY_SIZE(rk30_dmacs));
}

#ifdef CONFIG_UART0_RK29
static struct resource resources_uart0[] = {
	{
		.start	= IRQ_UART0,
		.end	= IRQ_UART0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_UART0_PHYS,
		.end	= RK30_UART0_PHYS + RK30_UART0_SIZE - 1,
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
		.start	= RK30_UART1_PHYS,
		.end	= RK30_UART1_PHYS + RK30_UART1_SIZE - 1,
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
		.start	= RK30_UART2_PHYS,
		.end	= RK30_UART2_PHYS + RK30_UART2_SIZE - 1,
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

#ifdef CONFIG_UART3_RK29
static struct resource resources_uart3[] = {
	{
		.start	= IRQ_UART3,
		.end	= IRQ_UART3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_UART3_PHYS,
		.end	= RK30_UART3_PHYS + RK30_UART3_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_uart3 = {
	.name	= "rk_serial",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_uart3),
	.resource	= resources_uart3,
};
#endif

static void __init rk30_init_uart(void)
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
#ifdef CONFIG_UART3_RK29
	platform_device_register(&device_uart3);
#endif
}

// i2c
#ifdef CONFIG_I2C0_CONTROLLER_RK29
#define I2C0_ADAP_TYPE  I2C_RK29_ADAP
#define I2C0_START      RK30_I2C0_PHYS
#define I2C0_END        RK30_I2C0_PHYS + SZ_4K - 1
#endif
#ifdef CONFIG_I2C0_CONTROLLER_RK30
#define I2C0_ADAP_TYPE   I2C_RK30_ADAP
#define I2C0_START      RK30_I2C0_PHYS + SZ_4K
#define I2C0_END        RK30_I2C0_PHYS + SZ_8K - 1
#endif

#ifdef CONFIG_I2C1_CONTROLLER_RK29
#define I2C1_ADAP_TYPE  I2C_RK29_ADAP
#define I2C1_START      RK30_I2C1_PHYS
#define I2C1_END        RK30_I2C1_PHYS + SZ_4K - 1
#endif
#ifdef CONFIG_I2C1_CONTROLLER_RK30
#define I2C1_ADAP_TYPE   I2C_RK30_ADAP
#define I2C1_START      RK30_I2C1_PHYS + SZ_4K
#define I2C1_END        RK30_I2C1_PHYS + SZ_8K - 1
#endif

#ifdef CONFIG_I2C2_CONTROLLER_RK29
#define I2C2_ADAP_TYPE  I2C_RK29_ADAP
#define I2C2_START      RK30_I2C2_PHYS
#define I2C2_END        RK30_I2C2_PHYS + SZ_4K - 1
#endif
#ifdef CONFIG_I2C2_CONTROLLER_RK30
#define I2C2_ADAP_TYPE   I2C_RK30_ADAP
#define I2C2_START      RK30_I2C2_PHYS + SZ_4K
#define I2C2_END        RK30_I2C2_PHYS + SZ_8K - 1
#endif

#ifdef CONFIG_I2C3_CONTROLLER_RK29
#define I2C3_ADAP_TYPE  I2C_RK29_ADAP
#define I2C3_START      RK30_I2C3_PHYS
#define I2C3_END        RK30_I2C3_PHYS + SZ_4K - 1
#endif
#ifdef CONFIG_I2C3_CONTROLLER_RK30
#define I2C3_ADAP_TYPE   I2C_RK30_ADAP
#define I2C3_START      RK30_I2C3_PHYS + SZ_4K
#define I2C3_END        RK30_I2C3_PHYS + SZ_8K - 1
#endif

#ifdef CONFIG_I2C4_CONTROLLER_RK29
#define I2C4_ADAP_TYPE  I2C_RK29_ADAP
#define I2C4_START      RK30_I2C4_PHYS
#define I2C4_END        RK30_I2C4_PHYS + SZ_4K - 1
#endif
#ifdef CONFIG_I2C4_CONTROLLER_RK30
#define I2C4_ADAP_TYPE   I2C_RK30_ADAP
#define I2C4_START      RK30_I2C4_PHYS + SZ_4K
#define I2C4_END        RK30_I2C4_PHYS + SZ_8K - 1
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

#ifdef CONFIG_I2C4_RK30
static struct rk30_i2c_platform_data default_i2c4_data = {
	.bus_num = 4,
	.is_div_from_arm = 0,
	.adap_type = I2C4_ADAP_TYPE,
};

static struct resource resources_i2c4[] = {
	{
		.start	= IRQ_I2C4,
		.end	= IRQ_I2C4,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= I2C4_START,
        .end    = I2C4_END,    
		.flags	= IORESOURCE_MEM,
	},
};

static struct platform_device device_i2c4 = {
	.name	= "rk30_i2c",
	.id	= 4,
	.num_resources	= ARRAY_SIZE(resources_i2c4),
	.resource	= resources_i2c4,
	.dev 		= {
		.platform_data = &default_i2c4_data,
	},
};
#endif

static void __init rk30_init_i2c(void)
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
#ifdef CONFIG_I2C4_RK30
	platform_device_register(&device_i2c4);
#endif
}
//end of i2c

/*****************************************************************************************
 * spi devices
 * author: cmc@rock-chips.com
 *****************************************************************************************/
#define SPI_CHIPSELECT_NUM 2
static struct spi_cs_gpio rk29xx_spi0_cs_gpios[SPI_CHIPSELECT_NUM] = {
	{
		.name = "spi0 cs0",
		.cs_gpio = RK30_PIN1_PA4,
		.cs_iomux_name = GPIO1A4_UART1SIN_SPI0CSN0_NAME,
		.cs_iomux_mode = GPIO1A_SPI0_CSN0,
	},
	{
		.name = "spi0 cs1",
		.cs_gpio = RK30_PIN4_PB7,
		.cs_iomux_name = GPIO4B7_SPI0CSN1_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO4B_SPI0_CSN1,
	}
};

static struct spi_cs_gpio rk29xx_spi1_cs_gpios[SPI_CHIPSELECT_NUM] = {
	{
		.name = "spi1 cs0",
		.cs_gpio = RK30_PIN2_PC4,
		.cs_iomux_name = GPIO2C4_LCDC1DATA20_SPI1CSN0_HSADCDATA1_NAME,
		.cs_iomux_mode = GPIO2C_SPI1_CSN0,
	},
	{
		.name = "spi1 cs1",
		.cs_gpio = RK30_PIN2_PC7,
		.cs_iomux_name = GPIO2C7_LCDC1DATA23_SPI1CSN1_HSADCDATA4_NAME,//if no iomux,set it NULL
		.cs_iomux_mode = GPIO2C_SPI1_CSN1,
	}
};

static int spi_io_init(struct spi_cs_gpio *cs_gpios, int cs_num)
{
	int i;
	if (cs_gpios) {
		for (i=0; i<cs_num; i++) {
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

struct rk29xx_spi_platform_data rk29xx_spi0_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi0_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};

struct rk29xx_spi_platform_data rk29xx_spi1_platdata = {
	.num_chipselect = SPI_CHIPSELECT_NUM,
	.chipselect_gpios = rk29xx_spi1_cs_gpios,
	.io_init = spi_io_init,
	.io_deinit = spi_io_deinit,
	.io_fix_leakage_bug = spi_io_fix_leakage_bug,
	.io_resume_leakage_bug = spi_io_resume_leakage_bug,
};



/*
 * rk29xx spi master device
 */
#ifdef CONFIG_SPIM0_RK29
static struct resource rk29_spi0_resources[] = {
	{
		.start	= IRQ_SPI0,
		.end	= IRQ_SPI0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_SPI0_PHYS,
		.end	= RK30_SPI0_PHYS + RK30_SPI0_SIZE - 1,
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

#ifdef CONFIG_SPIM1_RK29
static struct resource rk29_spi1_resources[] = {
	{
		.start	= IRQ_SPI1,
		.end	= IRQ_SPI1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_SPI1_PHYS,
		.end	= RK30_SPI1_PHYS + RK30_SPI1_SIZE - 1,
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
		.dma_mask = &dma_dmamask,
		.coherent_dma_mask = DMA_BIT_MASK(32),
		.platform_data	= &rk29xx_spi1_platdata,
	},
};
#endif

static void __init rk30_init_spim(void)
{
#ifdef CONFIG_SPIM0_RK29
	platform_device_register(&rk29xx_device_spi0m);
#endif
#ifdef CONFIG_SPIM1_RK29
	platform_device_register(&rk29xx_device_spi1m);
#endif
}


#ifdef CONFIG_MTD_NAND_RK29XX
static struct resource resources_nand[] = {
	{
		.start	= RK30_NANDC_PHYS,
		.end	= RK30_NANDC_PHYS + RK30_NANDC_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	}
};

static struct platform_device device_nand = {
	.name		= "rk30xxnand",
	.id		= -1,
	.resource	= resources_nand,
	.num_resources	= ARRAY_SIZE(resources_nand),
};
#endif

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

static int __init rk30_init_devices(void)
{
	rk30_init_dma();
	rk30_init_uart();
	rk30_init_i2c();
	rk30_init_spim();	
#ifdef CONFIG_MTD_NAND_RK29XX
	platform_device_register(&device_nand);
#endif
#ifdef CONFIG_KEYS_RK29
	platform_device_register(&device_keys);
#endif
        return 0;
}
arch_initcall(rk30_init_devices);
