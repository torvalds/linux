/*
 * Device Tree support for Rockchip FPGA
 *
 * Copyright (C) 2014 ROCKCHIP, Inc.
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
 */

#include <linux/clocksource.h>
#include <linux/init.h>
#include <linux/irqchip.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/cpu.h>
#include <linux/rockchip/iomap.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

static struct map_desc rk3288_io_desc[] __initdata = {
	{
		.virtual	= (unsigned long) RK_DEBUG_UART_VIRT,
		.pfn		= __phys_to_pfn(RK3288_UART_DBG_PHYS),
		.length		= RK3288_UART_SIZE,
		.type		= MT_DEVICE,
	},
};

static void __init rk3288_fpga_map_io(void)
{
	iotable_init(rk3288_io_desc, ARRAY_SIZE(rk3288_io_desc));
	debug_ll_io_init();

	rockchip_soc_id = ROCKCHIP_SOC_RK3288;
}

static void __init rk3288_fpga_init_timer(void)
{
	clocksource_of_init();
}

static const char * const rk3288_fpga_compat[] __initconst = {
	"rockchip,rk3288-fpga",
	NULL,
};

DT_MACHINE_START(RK3288_FPGA_DT, "Rockchip RK3288 FPGA (Flattened Device Tree)")
	.map_io		= rk3288_fpga_map_io,
	.init_time	= rk3288_fpga_init_timer,
	.dt_compat	= rk3288_fpga_compat,
MACHINE_END
