/*
 *  linux/arch/arm/mach-ep93xx/micro9.c
 *
 * Copyright (C) 2006 Contec Steuerungstechnik & Automation GmbH
 *                   Manfred Gruber <manfred.gruber@contec.at>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/platform_device.h>
#include <linux/sched.h>

#include <linux/mtd/physmap.h>

#include <asm/io.h>
#include <mach/hardware.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

static struct ep93xx_eth_data micro9_eth_data = {
       .phy_id                 = 0x1f,
};

static void __init micro9_init(void)
{
	ep93xx_register_eth(&micro9_eth_data, 1);
}

/*
 * Micro9-H
 */
#ifdef CONFIG_MACH_MICRO9H
static struct physmap_flash_data micro9h_flash_data = {
       .width          = 4,
};

static struct resource micro9h_flash_resource = {
       .start          = 0x10000000,
       .end            = 0x13ffffff,
       .flags          = IORESOURCE_MEM,
};

static struct platform_device micro9h_flash = {
       .name           = "physmap-flash",
       .id             = 0,
       .dev            = {
               .platform_data  = &micro9h_flash_data,
       },
       .num_resources  = 1,
       .resource       = &micro9h_flash_resource,
};

static void __init micro9h_init(void)
{
       platform_device_register(&micro9h_flash);
}

static void __init micro9h_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
       micro9h_init();
}

MACHINE_START(MICRO9, "Contec Hypercontrol Micro9-H")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9h_init_machine,
MACHINE_END
#endif

/*
 * Micro9-M
 */
#ifdef CONFIG_MACH_MICRO9M
static void __init micro9m_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
}

MACHINE_START(MICRO9M, "Contec Hypercontrol Micro9-M")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9m_init_machine,
MACHINE_END
#endif

/*
 * Micro9-L
 */
#ifdef CONFIG_MACH_MICRO9L
static void __init micro9l_init_machine(void)
{
       ep93xx_init_devices();
       micro9_init();
}

MACHINE_START(MICRO9L, "Contec Hypercontrol Micro9-L")
       /* Maintainer: Manfred Gruber <manfred.gruber@contec.at> */
       .phys_io        = EP93XX_APB_PHYS_BASE,
       .io_pg_offst    = ((EP93XX_APB_VIRT_BASE) >> 18) & 0xfffc,
       .boot_params    = 0x00000100,
       .map_io         = ep93xx_map_io,
       .init_irq       = ep93xx_init_irq,
       .timer          = &ep93xx_timer,
       .init_machine   = micro9l_init_machine,
MACHINE_END
#endif

