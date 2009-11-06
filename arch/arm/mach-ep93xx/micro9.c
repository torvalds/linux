/*
 *  linux/arch/arm/mach-ep93xx/micro9.c
 *
 * Copyright (C) 2006 Contec Steuerungstechnik & Automation GmbH
 *                    Manfred Gruber <m.gruber@tirol.com>
 * Copyright (C) 2009 Contec Steuerungstechnik & Automation GmbH
 *                    Hubert Feurstein <hubert.feurstein@contec.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mtd/physmap.h>

#include <mach/hardware.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>


/*************************************************************************
 * Micro9 NOR Flash
 *
 * Micro9-High has up to 64MB of 32-bit flash on CS1
 * Micro9-Mid has up to 64MB of either 32-bit or 16-bit flash on CS1
 * Micro9-Lite uses a seperate MTD map driver for flash support
 * Micro9-Slim has up to 64MB of either 32-bit or 16-bit flash on CS1
 *************************************************************************/
static struct physmap_flash_data micro9_flash_data;

static struct resource micro9_flash_resource = {
	.start		= EP93XX_CS1_PHYS_BASE,
	.end		= EP93XX_CS1_PHYS_BASE + SZ_64M - 1,
	.flags		= IORESOURCE_MEM,
};

static struct platform_device micro9_flash = {
	.name		= "physmap-flash",
	.id		= 0,
	.dev		= {
		.platform_data	= &micro9_flash_data,
	},
	.num_resources	= 1,
	.resource	= &micro9_flash_resource,
};

static void __init __micro9_register_flash(unsigned int width)
{
	micro9_flash_data.width = width;

	platform_device_register(&micro9_flash);
}

static unsigned int __init micro9_detect_bootwidth(void)
{
	u32 v;

	/* Detect the bus width of the external flash memory */
	v = __raw_readl(EP93XX_SYSCON_SYSCFG);
	if (v & EP93XX_SYSCON_SYSCFG_LCSN7)
		return 4; /* 32-bit */
	else
		return 2; /* 16-bit */
}

static void __init micro9_register_flash(void)
{
	if (machine_is_micro9())
		__micro9_register_flash(4);
	else if (machine_is_micro9m() || machine_is_micro9s())
		__micro9_register_flash(micro9_detect_bootwidth());
}


/*************************************************************************
 * Micro9 Ethernet
 *************************************************************************/
static struct ep93xx_eth_data micro9_eth_data = {
	.phy_id		= 0x1f,
};


static void __init micro9_init_machine(void)
{
	ep93xx_init_devices();
	ep93xx_register_eth(&micro9_eth_data, 1);
	micro9_register_flash();
}


#ifdef CONFIG_MACH_MICRO9H
MACHINE_START(MICRO9, "Contec Micro9-High")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= micro9_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9M
MACHINE_START(MICRO9M, "Contec Micro9-Mid")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_ASYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= micro9_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9L
MACHINE_START(MICRO9L, "Contec Micro9-Lite")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_SYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= micro9_init_machine,
MACHINE_END
#endif

#ifdef CONFIG_MACH_MICRO9S
MACHINE_START(MICRO9S, "Contec Micro9-Slim")
	/* Maintainer: Hubert Feurstein <hubert.feurstein@contec.at> */
	.phys_io	= EP93XX_APB_PHYS_BASE,
	.io_pg_offst	= ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
	.boot_params	= EP93XX_SDCE3_PHYS_BASE_ASYNC + 0x100,
	.map_io		= ep93xx_map_io,
	.init_irq	= ep93xx_init_irq,
	.timer		= &ep93xx_timer,
	.init_machine	= micro9_init_machine,
MACHINE_END
#endif
