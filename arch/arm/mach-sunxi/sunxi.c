/*
 * Device Tree support for Allwinner A1X SoCs
 *
 * Copyright (C) 2012 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/sunxi_timer.h>

#include <linux/irqchip/sunxi.h>

#include <asm/hardware/vic.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "sunxi.h"

static struct map_desc sunxi_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) SUNXI_REGS_VIRT_BASE,
		.pfn		= __phys_to_pfn(SUNXI_REGS_PHYS_BASE),
		.length		= SUNXI_REGS_SIZE,
		.type		= MT_DEVICE,
	},
};

void __init sunxi_map_io(void)
{
	iotable_init(sunxi_io_desc, ARRAY_SIZE(sunxi_io_desc));
}

static void __init sunxi_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const sunxi_board_dt_compat[] = {
	"allwinner,sun4i",
	"allwinner,sun5i",
	NULL,
};

DT_MACHINE_START(SUNXI_DT, "Allwinner A1X (Device Tree)")
	.init_machine	= sunxi_dt_init,
	.map_io		= sunxi_map_io,
	.init_irq	= sunxi_init_irq,
	.handle_irq	= sunxi_handle_irq,
	.timer		= &sunxi_timer,
	.dt_compat	= sunxi_board_dt_compat,
MACHINE_END
