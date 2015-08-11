/*
 * Samsung's S3C64XX flattened device tree enabled machine
 *
 * Copyright (c) 2013 Tomasz Figa <tomasz.figa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/of_platform.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include <plat/cpu.h>
#include <mach/map.h>

#include "common.h"
#include "watchdog-reset.h"

/*
 * IO mapping for shared system controller IP.
 *
 * FIXME: Make remaining drivers use dynamic mapping.
 */
static struct map_desc s3c64xx_dt_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S3C_VA_SYS,
		.pfn		= __phys_to_pfn(S3C64XX_PA_SYSCON),
		.length		= SZ_4K,
		.type		= MT_DEVICE,
	},
};

static void __init s3c64xx_dt_map_io(void)
{
	debug_ll_io_init();
	iotable_init(s3c64xx_dt_iodesc, ARRAY_SIZE(s3c64xx_dt_iodesc));

	s3c64xx_init_cpu();

	if (!soc_is_s3c64xx())
		panic("SoC is not S3C64xx!");
}

static void __init s3c64xx_dt_init_machine(void)
{
	samsung_wdt_reset_of_init();
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static void s3c64xx_dt_restart(enum reboot_mode mode, const char *cmd)
{
	if (mode != REBOOT_SOFT)
		samsung_wdt_reset();

	/* if all else fails, or mode was for soft, jump to 0 */
	soft_restart(0);
}

static const char *const s3c64xx_dt_compat[] __initconst = {
	"samsung,s3c6400",
	"samsung,s3c6410",
	NULL
};

DT_MACHINE_START(S3C6400_DT, "Samsung S3C64xx (Flattened Device Tree)")
	/* Maintainer: Tomasz Figa <tomasz.figa@gmail.com> */
	.dt_compat	= s3c64xx_dt_compat,
	.map_io		= s3c64xx_dt_map_io,
	.init_machine	= s3c64xx_dt_init_machine,
	.restart        = s3c64xx_dt_restart,
MACHINE_END
