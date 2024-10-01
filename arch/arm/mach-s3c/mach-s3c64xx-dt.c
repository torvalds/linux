// SPDX-License-Identifier: GPL-2.0
//
// Samsung's S3C64XX flattened device tree enabled machine
//
// Copyright (c) 2013 Tomasz Figa <tomasz.figa@gmail.com>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include "cpu.h"
#include "map.h"

#include "s3c64xx.h"

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

static const char *const s3c64xx_dt_compat[] __initconst = {
	"samsung,s3c6400",
	"samsung,s3c6410",
	NULL
};

DT_MACHINE_START(S3C6400_DT, "Samsung S3C64xx (Flattened Device Tree)")
	/* Maintainer: Tomasz Figa <tomasz.figa@gmail.com> */
	.dt_compat	= s3c64xx_dt_compat,
	.map_io		= s3c64xx_dt_map_io,
MACHINE_END
