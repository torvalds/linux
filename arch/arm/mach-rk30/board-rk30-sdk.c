/* arch/arm/mach-rk30/board-rk30-sdk.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/spi/spi.h>
#include <linux/mmc/host.h>
#include <linux/ion.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <asm/hardware/gic.h>

#include <mach/board.h>
#include <mach/hardware.h>
//#include "devices.h"
#include <mach/io.h>

#if defined(CONFIG_MTD_NAND_RK29XX)  

static struct resource rk30xxnand_resources[] = {
	{
		.start	= RK30_NANDC_PHYS,
		.end	= RK30_NANDC_PHYS+RK30_NANDC_SIZE -1,
		.flags	= IORESOURCE_MEM,
	}
};

struct platform_device rk30xx_device_nand = {
	.name	= "rk30xxnand", 
	.id		=  -1, 
	.resource	= rk30xxnand_resources,
	.num_resources= ARRAY_SIZE(rk30xxnand_resources),
};
#endif

static struct platform_device *devices[] __initdata = {
#ifdef CONFIG_MTD_NAND_RK29XX
	&rk30xx_device_nand,
#endif
};
static void __init machine_rk30_board_init(void)
{
    platform_add_devices(devices, ARRAY_SIZE(devices));
}


static void __init rk30_reserve(void)
{
    board_mem_reserved();
}
MACHINE_START(RK30, "RK30board")
	.boot_params	= PLAT_PHYS_OFFSET + 0x800,
	.fixup		= rk30_fixup,
	.map_io		= rk30_map_io,
	.init_irq	= rk30_init_irq,
	.timer		= &rk30_timer,
    .reserve    = &rk30_reserve,
	.init_machine	= machine_rk30_board_init,
MACHINE_END
