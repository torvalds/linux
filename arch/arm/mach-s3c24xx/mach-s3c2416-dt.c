/*
 * Samsung's S3C2416 flattened device tree enabled machine
 *
 * Copyright (c) 2012 Heiko Stuebner <heiko@sntech.de>
 *
 * based on mach-exynos/mach-exynos4-dt.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 * Copyright (c) 2010-2011 Linaro Ltd.
 *		www.linaro.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/clocksource.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/serial_s3c.h>

#include <asm/mach/arch.h>
#include <mach/map.h>

#include <plat/cpu.h>
#include <plat/pm.h>

#include "common.h"

static void __init s3c2416_dt_map_io(void)
{
	s3c24xx_init_io(NULL, 0);
}

static void __init s3c2416_dt_machine_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
	s3c_pm_init();
}

static char const *s3c2416_dt_compat[] __initdata = {
	"samsung,s3c2416",
	"samsung,s3c2450",
	NULL
};

DT_MACHINE_START(S3C2416_DT, "Samsung S3C2416 (Flattened Device Tree)")
	/* Maintainer: Heiko Stuebner <heiko@sntech.de> */
	.dt_compat	= s3c2416_dt_compat,
	.map_io		= s3c2416_dt_map_io,
	.init_irq	= irqchip_init,
	.init_machine	= s3c2416_dt_machine_init,
	.restart	= s3c2416_restart,
MACHINE_END
