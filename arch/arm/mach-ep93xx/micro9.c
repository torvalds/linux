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
#include <linux/io.h>

#include <mach/hardware.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>


/*************************************************************************
 * Micro9 NOR Flash
 *
 * Micro9-High has up to 64MB of 32-bit flash on CS1
 * Micro9-Mid has up to 64MB of either 32-bit or 16-bit flash on CS1
 * Micro9-Lite uses a separate MTD map driver for flash support
 * Micro9-Slim has up to 64MB of either 32-bit or 16-bit flash on CS1
 *************************************************************************/
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
	unsigned int width;

	if (machine_is_micro9())
		width = 4;
	else if (machine_is_micro9m() || machine_is_micro9s())
		width = micro9_detect_bootwidth();
	else
		width = 0;

	if (width)
		ep93xx_register_flash(width, EP93XX_CS1_PHYS_BASE, SZ_64M);
}


/*************************************************************************
 * Micro9 Ethernet
 *************************************************************************/
static struct ep93xx_eth_data __initdata micro9_eth_data = {
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
