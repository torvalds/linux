/*
 *  linux/arch/arm/mach-clps711x/autcpu12.c
 *
 * (c) 2001 Thomas Gleixner, autronix automation <gleixner@autronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>

#include <mach/hardware.h>
#include <asm/sizes.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/pgtable.h>
#include <asm/page.h>

#include <asm/mach/map.h>
#include <mach/autcpu12.h>

#include "common.h"

#define AUTCPU12_CS8900_BASE	(CS2_PHYS_BASE + 0x300)
#define AUTCPU12_CS8900_IRQ	(IRQ_EINT3)

static struct resource autcpu12_cs8900_resource[] __initdata = {
	DEFINE_RES_MEM(AUTCPU12_CS8900_BASE, SZ_1K),
	DEFINE_RES_IRQ(AUTCPU12_CS8900_IRQ),
};

static struct resource autcpu12_nvram_resource[] __initdata = {
	DEFINE_RES_MEM_NAMED(AUTCPU12_PHYS_NVRAM, SZ_128K, "SRAM"),
};

static struct platform_device autcpu12_nvram_pdev __initdata = {
	.name		= "autcpu12_nvram",
	.id		= -1,
	.resource	= autcpu12_nvram_resource,
	.num_resources	= ARRAY_SIZE(autcpu12_nvram_resource),
};

static void __init autcpu12_init(void)
{
	platform_device_register_simple("video-clps711x", 0, NULL, 0);
	platform_device_register_simple("cs89x0", 0, autcpu12_cs8900_resource,
					ARRAY_SIZE(autcpu12_cs8900_resource));
	platform_device_register(&autcpu12_nvram_pdev);
}

MACHINE_START(AUTCPU12, "autronix autcpu12")
	/* Maintainer: Thomas Gleixner */
	.atag_offset	= 0x20000,
	.nr_irqs	= CLPS711X_NR_IRQS,
	.map_io		= clps711x_map_io,
	.init_irq	= clps711x_init_irq,
	.timer		= &clps711x_timer,
	.init_machine	= autcpu12_init,
	.handle_irq	= clps711x_handle_irq,
	.restart	= clps711x_restart,
MACHINE_END

