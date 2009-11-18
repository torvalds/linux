/*
 * arch/arm/mach-ep93xx/edb93xx.c
 * Cirrus Logic EDB93xx Development Board support.
 *
 * EDB93XX, EDB9301, EDB9307A
 * Copyright (C) 2008-2009 H Hartley Sweeten <hsweeten@visionengravers.com>
 *
 * EDB9302
 * Copyright (C) 2006 George Kashperko <george@chas.com.ua>
 *
 * EDB9302A, EDB9315, EDB9315A
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * EDB9307
 * Copyright (C) 2007 Herbert Valerio Riedel <hvr@gnu.org>
 *
 * EDB9312
 * Copyright (C) 2006 Infosys Technologies Limited
 *                    Toufeeq Hussain <toufeeq_hussain@infosys.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c-gpio.h>

#include <mach/hardware.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>


static struct physmap_flash_data edb93xx_flash_data;

static struct resource edb93xx_flash_resource = {
	.flags		= IORESOURCE_MEM,
};

static struct platform_device edb93xx_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &edb93xx_flash_data,
	},
	.num_resources	= 1,
	.resource	= &edb93xx_flash_resource,
};

static void __init __edb93xx_register_flash(unsigned int width,
			resource_size_t start, resource_size_t size)
{
	edb93xx_flash_data.width	= width;
	edb93xx_flash_resource.start	= start;
	edb93xx_flash_resource.end	= start + size - 1;

	platform_device_register(&edb93xx_flash);
}

static void __init edb93xx_register_flash(void)
{
	if (machine_is_edb9307() || machine_is_edb9312() ||
	    machine_is_edb9315()) {
		__edb93xx_register_flash(4, EP93XX_CS6_PHYS_BASE, SZ_32M);
	} else {
		__edb93xx_register_flash(2, EP93XX_CS6_PHYS_BASE, SZ_16M);
	}
}

static struct ep93xx_eth_data edb93xx_eth_data = {
	.phy_id		= 1,
};


/*************************************************************************
 * EDB93xx i2c peripheral handling
 *************************************************************************/
static struct i2c_gpio_platform_data edb93xx_i2c_gpio_data = {
	.sda_pin		= EP93XX_GPIO_LINE_EEDAT,
	.sda_is_open_drain	= 0,
	.scl_pin		= EP93XX_GPIO_LINE_EECLK,
	.scl_is_open_drain	= 0,
	.udelay			= 0,	/* default to 100 kHz */
	.timeout		= 0,	/* default to 100 ms */
};

static struct i2c_board_info __initdata edb93xxa_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("isl1208", 0x6f),
	},
};

static struct i2c_board_info __initdata edb93xx_i2c_board_info[] = {
	{
		I2C_BOARD_INFO("ds1337", 0x68),
	},
};

static void __init edb93xx_register_i2c(void)
{
	if (machine_is_edb9302a() || machine_is_edb9307a() ||
	    machine_is_edb9315a()) {
		ep93xx_register_i2c(&edb93xx_i2c_gpio_data,
				    edb93xxa_i2c_board_info,
				    ARRAY_SIZE(edb93xxa_i2c_board_info));
	} else if (machine_is_edb9307() || machine_is_edb9312() ||
		   machine_is_edb9315()) {
		ep93xx_register_i2c(&edb93xx_i2c_gpio_data,
				    edb93xx_i2c_board_info,
				    ARRAY_SIZE(edb93xx_i2c_board_info));
	}
}

static void __init edb93xx_init_machine(void)
{
	ep93xx_init_devices();
	edb93xx_register_flash();
	ep93xx_register_eth(&edb93xx_eth_data, 1);
	edb93xx_register_i2c();
}


#ifdef CONFIG_MACH_EDB9301
MACHINE_START(EDB9301, "Cirrus Logic EDB9301 Evaluation Board")
	/* Maintainer: H Hartley Sweeten <hsweeten@visionengravers.com> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9302
MACHINE_START(EDB9302, "Cirrus Logic EDB9302 Evaluation Board")
	/* Maintainer: George Kashperko <george@chas.com.ua> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9302A
MACHINE_START(EDB9302A, "Cirrus Logic EDB9302A Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE0_PHYS_BASE + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9307
MACHINE_START(EDB9307, "Cirrus Logic EDB9307 Evaluation Board")
	/* Maintainer: Herbert Valerio Riedel <hvr@gnu.org> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9307A
MACHINE_START(EDB9307A, "Cirrus Logic EDB9307A Evaluation Board")
	/* Maintainer: H Hartley Sweeten <hsweeten@visionengravers.com> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE0_PHYS_BASE + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9312
MACHINE_START(EDB9312, "Cirrus Logic EDB9312 Evaluation Board")
	/* Maintainer: Toufeeq Hussain <toufeeq_hussain@infosys.com> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9315
MACHINE_START(EDB9315, "Cirrus Logic EDB9315 Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_EDB9315A
MACHINE_START(EDB9315A, "Cirrus Logic EDB9315A Evaluation Board")
	/* Maintainer: Lennert Buytenhek <buytenh@wantstofly.org> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE0_PHYS_BASE + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= edb93xx_init_machine,
MACHINE_END
#endif
