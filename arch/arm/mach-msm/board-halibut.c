/* linux/arch/arm/mach-msm/board-halibut.c
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
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

#include <mach/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>

#include <mach/board.h>
#include <mach/msm_iomap.h>

#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

static struct resource smc91x_resources[] = {
	[0] = {
		.start	= 0x9C004300,
		.end	= 0x9C004400,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MSM_GPIO_TO_INT(49),
		.end	= MSM_GPIO_TO_INT(49),
		.flags	= IORESOURCE_IRQ,
	},
};

static struct platform_device smc91x_device = {
	.name		= "smc91x",
	.id		= 0,
	.num_resources	= ARRAY_SIZE(smc91x_resources),
	.resource	= smc91x_resources,
};

static void mddi0_panel_power(int on)
{
}

static struct msm_mddi_platform_data msm_mddi0_pdata = {
	.panel_power	= mddi0_panel_power,
	.has_vsync_irq	= 0,
};

static struct platform_device msm_mddi0_device = {
	.name	= "msm_mddi",
	.id	= 0,
	.dev	= {
		.platform_data = &msm_mddi0_pdata
	},
};

static struct platform_device msm_serial0_device = {
	.name	= "msm_serial",
	.id	= 0,
};

static struct platform_device *devices[] __initdata = {
	&msm_serial0_device,
	&msm_mddi0_device,
	&smc91x_device,
};

extern struct sys_timer msm_timer;

static void __init halibut_init_irq(void)
{
	msm_init_irq();
}

static void __init halibut_init(void)
{
	platform_add_devices(devices, ARRAY_SIZE(devices));
	msm_add_devices();
}

static void __init halibut_map_io(void)
{
	msm_map_common_io();
}

MACHINE_START(HALIBUT, "Halibut Board (QCT SURF7200A)")

/* UART for LL DEBUG */
	.phys_io	= MSM_UART1_PHYS,
	.io_pg_offst	= ((MSM_UART1_BASE) >> 18) & 0xfffc,

	.boot_params	= 0x10000100,
	.map_io		= halibut_map_io,
	.init_irq	= halibut_init_irq,
	.init_machine	= halibut_init,
	.timer		= &msm_timer,
MACHINE_END
