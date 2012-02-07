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
	.name	= "i2c-rk30",
	.id	= 0,
	.num_resources	= ARRAY_SIZE(resources_i2c0),
	.resource	= resources_i2c0,
    .dev 			= {
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
	.name	= "i2c-rk30",
	.id	= 1,
	.num_resources	= ARRAY_SIZE(resources_i2c1),
	.resource	= resources_i2c1,
    .dev 			= {
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
	.name	= "i2c-rk30",
	.id	= 2,
	.num_resources	= ARRAY_SIZE(resources_i2c2),
	.resource	= resources_i2c2,
    .dev 			= {
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
	.name	= "i2c-rk30",
	.id	= 3,
	.num_resources	= ARRAY_SIZE(resources_i2c3),
	.resource	= resources_i2c3,
    .dev 			= {
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
	.name	= "i2c-rk30",
	.id	= 4,
	.num_resources	= ARRAY_SIZE(resources_i2c4),
	.resource	= resources_i2c4,
    .dev 			= {
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

static int __init rk30_init_devices(void)
{
	rk30_init_uart();
	rk30_init_i2c();
#ifdef CONFIG_MTD_NAND_RK29XX
	platform_device_register(&device_nand);
#endif
        return 0;
}
arch_initcall(rk30_init_devices);
