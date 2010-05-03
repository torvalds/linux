/*
 * Copyright (C) 2008-2009 ST-Ericsson
 *
 * Author: Srinidhi KASAGAR <srinidhi.kasagar@stericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 */
#include <linux/types.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/localtimer.h>
#include <asm/mach/map.h>
#include <plat/mtu.h>
#include <mach/hardware.h>
#include <mach/setup.h>

#define GPIO_RESOURCE(block)						\
	{								\
		.start	= U8500_GPIOBANK##block##_BASE,			\
		.end	= U8500_GPIOBANK##block##_BASE + 127,		\
		.flags	= IORESOURCE_MEM,				\
	},								\
	{								\
		.start	= IRQ_GPIO##block,				\
		.end	= IRQ_GPIO##block,				\
		.flags	= IORESOURCE_IRQ,				\
	}

#define GPIO_DEVICE(block)						\
	{								\
		.name 		= "gpio",				\
		.id		= block,				\
		.num_resources 	= 2,					\
		.resource	= &u8500_gpio_resources[block * 2],	\
		.dev = {						\
			.platform_data = &u8500_gpio_data[block],	\
		},							\
	}

#define GPIO_DATA(_name, first)						\
	{								\
		.name 		= _name,				\
		.first_gpio 	= first,				\
		.first_irq 	= NOMADIK_GPIO_TO_IRQ(first),		\
	}

static struct nmk_gpio_platform_data u8500_gpio_data[] = {
	GPIO_DATA("GPIO-0-31", 0),
	GPIO_DATA("GPIO-32-63", 32), /* 37..63 not routed to pin */
	GPIO_DATA("GPIO-64-95", 64),
	GPIO_DATA("GPIO-96-127", 96), /* 97..127 not routed to pin */
	GPIO_DATA("GPIO-128-159", 128),
	GPIO_DATA("GPIO-160-191", 160), /* 172..191 not routed to pin */
	GPIO_DATA("GPIO-192-223", 192),
	GPIO_DATA("GPIO-224-255", 224), /* 231..255 not routed to pin */
	GPIO_DATA("GPIO-256-288", 256), /* 258..288 not routed to pin */
};

static struct resource u8500_gpio_resources[] = {
	GPIO_RESOURCE(0),
	GPIO_RESOURCE(1),
	GPIO_RESOURCE(2),
	GPIO_RESOURCE(3),
	GPIO_RESOURCE(4),
	GPIO_RESOURCE(5),
	GPIO_RESOURCE(6),
	GPIO_RESOURCE(7),
	GPIO_RESOURCE(8),
};

static struct platform_device u8500_gpio_devs[] = {
	GPIO_DEVICE(0),
	GPIO_DEVICE(1),
	GPIO_DEVICE(2),
	GPIO_DEVICE(3),
	GPIO_DEVICE(4),
	GPIO_DEVICE(5),
	GPIO_DEVICE(6),
	GPIO_DEVICE(7),
	GPIO_DEVICE(8),
};

static struct platform_device *platform_devs[] __initdata = {
	&u8500_gpio_devs[0],
	&u8500_gpio_devs[1],
	&u8500_gpio_devs[2],
	&u8500_gpio_devs[3],
	&u8500_gpio_devs[4],
	&u8500_gpio_devs[5],
	&u8500_gpio_devs[6],
	&u8500_gpio_devs[7],
	&u8500_gpio_devs[8],
};

/* minimum static i/o mapping required to boot U8500 platforms */
static struct map_desc u8500_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_PRCMU_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO0_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO1_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO2_BASE, SZ_4K),
	__IO_DEV_DESC(U8500_GPIO3_BASE, SZ_4K),
};

static struct map_desc u8500ed_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE_ED, SZ_4K),
	__IO_DEV_DESC(U8500_CLKRST7_BASE_ED, SZ_8K),
};

static struct map_desc u8500v1_io_desc[] __initdata = {
	__IO_DEV_DESC(U8500_MTU0_BASE, SZ_4K),
};

void __init u8500_map_io(void)
{
	ux500_map_io();

	iotable_init(u8500_io_desc, ARRAY_SIZE(u8500_io_desc));

	if (cpu_is_u8500ed())
		iotable_init(u8500ed_io_desc, ARRAY_SIZE(u8500ed_io_desc));
	else
		iotable_init(u8500v1_io_desc, ARRAY_SIZE(u8500v1_io_desc));
}

/*
 * This function is called from the board init
 */
void __init u8500_init_devices(void)
{
	/* Register the platform devices */
	platform_add_devices(platform_devs, ARRAY_SIZE(platform_devs));

	return ;
}

static void __init u8500_timer_init(void)
{
#ifdef CONFIG_LOCAL_TIMERS
	/* Setup the local timer base */
	twd_base = __io_address(U8500_TWD_BASE);
#endif
	/* Setup the MTU base */
	if (cpu_is_u8500ed())
		mtu_base = __io_address(U8500_MTU0_BASE_ED);
	else
		mtu_base = __io_address(U8500_MTU0_BASE);

	nmdk_timer_init();
}

struct sys_timer u8500_timer = {
	.init	= u8500_timer_init,
};
