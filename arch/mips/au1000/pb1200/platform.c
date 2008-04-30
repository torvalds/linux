/*
 * Pb1200/DBAu1200 board platform device registration
 *
 * Copyright (C) 2008 MontaVista Software Inc. <source@mvista.com>
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>

#include <asm/mach-au1x00/au1xxx.h>

static struct resource ide_resources[] = {
	[0] = {
		.start	= IDE_PHYS_ADDR,
		.end 	= IDE_PHYS_ADDR + IDE_PHYS_LEN - 1,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= IDE_INT,
		.end	= IDE_INT,
		.flags	= IORESOURCE_IRQ
	}
};

static u64 ide_dmamask = ~(u32)0;

static struct platform_device ide_device = {
	.name		= "au1200-ide",
	.id		= 0,
	.dev = {
		.dma_mask 		= &ide_dmamask,
		.coherent_dma_mask	= 0xffffffff,
	},
	.num_resources	= ARRAY_SIZE(ide_resources),
	.resource	= ide_resources
};

static struct resource smc91c111_resources[] = {
	[0] = {
		.name	= "smc91x-regs",
		.start	= SMC91C111_PHYS_ADDR,
		.end	= SMC91C111_PHYS_ADDR + 0xf,
		.flags	= IORESOURCE_MEM
	},
	[1] = {
		.start	= SMC91C111_INT,
		.end	= SMC91C111_INT,
		.flags	= IORESOURCE_IRQ
	},
};

static struct platform_device smc91c111_device = {
	.name		= "smc91x",
	.id		= -1,
	.num_resources	= ARRAY_SIZE(smc91c111_resources),
	.resource	= smc91c111_resources
};

static struct platform_device *board_platform_devices[] __initdata = {
	&ide_device,
	&smc91c111_device
};

static int __init board_register_devices(void)
{
	return platform_add_devices(board_platform_devices,
				    ARRAY_SIZE(board_platform_devices));
}

arch_initcall(board_register_devices);
