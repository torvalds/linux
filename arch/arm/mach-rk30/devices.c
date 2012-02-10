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
#ifdef CONFIG_I2C0_RK30
static struct rk30_i2c_platform_data default_i2c0_data = {
	.bus_num = 0,
	.is_div_from_arm = 1,
#ifdef CONFIG_I2C0_CONTROLLER_RK29
	.adap_type = I2C_RK29_ADAP,
#endif
#ifdef CONFIG_I2C0_CONTROLLER_RK30
	.adap_type = I2C_RK30_ADAP,
#endif
};

static struct resource resources_i2c0[] = {
	{
		.start	= IRQ_I2C0,
		.end	= IRQ_I2C0,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_I2C0_PHYS,
		.end	= RK30_I2C0_PHYS + RK30_I2C0_SIZE - 1,
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
#ifdef CONFIG_I2C1_CONTROLLER_RK29
	.adap_type = I2C_RK29_ADAP,
#endif
#ifdef CONFIG_I2C1_CONTROLLER_RK30
	.adap_type = I2C_RK30_ADAP,
#endif
};

static struct resource resources_i2c1[] = {
	{
		.start	= IRQ_I2C1,
		.end	= IRQ_I2C1,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_I2C1_PHYS,
		.end	= RK30_I2C1_PHYS + RK30_I2C1_SIZE - 1,
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
#ifdef CONFIG_I2C2_CONTROLLER_RK29
	.adap_type = I2C_RK29_ADAP,
#endif
#ifdef CONFIG_I2C2_CONTROLLER_RK30
	.adap_type = I2C_RK30_ADAP,
#endif
};

static struct resource resources_i2c2[] = {
	{
		.start	= IRQ_I2C2,
		.end	= IRQ_I2C2,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_I2C2_PHYS,
		.end	= RK30_I2C2_PHYS + RK30_I2C2_SIZE - 1,
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
#ifdef CONFIG_I2C3_CONTROLLER_RK29
	.adap_type = I2C_RK29_ADAP,
#endif
#ifdef CONFIG_I2C3_CONTROLLER_RK30
	.adap_type = I2C_RK30_ADAP,
#endif
};

static struct resource resources_i2c3[] = {
	{
		.start	= IRQ_I2C3,
		.end	= IRQ_I2C3,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_I2C3_PHYS,
		.end	= RK30_I2C3_PHYS + RK30_I2C3_SIZE - 1,
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
#ifdef CONFIG_I2C4_CONTROLLER_RK29
	.adap_type = I2C_RK29_ADAP,
#endif
#ifdef CONFIG_I2C4_CONTROLLER_RK30
	.adap_type = I2C_RK30_ADAP,
#endif
};

static struct resource resources_i2c4[] = {
	{
		.start	= IRQ_I2C4,
		.end	= IRQ_I2C4,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.start	= RK30_I2C4_PHYS,
		.end	= RK30_I2C4_PHYS + RK30_I2C4_SIZE - 1,
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
#ifdef CONFIG_MTD_NAND_RK29XX
	platform_device_register(&device_nand);
#endif
#ifdef CONFIG_KEYS_RK29
	platform_device_register(&device_keys);
#endif
        return 0;
}
arch_initcall(rk30_init_devices);
