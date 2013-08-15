/*
 * arch/arm/mach-spear13xx/spear1310.c
 *
 * SPEAr1310 machine source file
 *
 * Copyright (C) 2012 ST Microelectronics
 * Viresh Kumar <viresh.linux@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#define pr_fmt(fmt) "SPEAr1310: " fmt

#include <linux/amba/pl022.h>
#include <linux/of_platform.h>
#include <linux/pata_arasan_cf_data.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "generic.h"
#include <mach/spear.h>

/* Base addresses */
#define SPEAR1310_RAS_GRP1_BASE			UL(0xD8000000)
#define VA_SPEAR1310_RAS_GRP1_BASE		UL(0xFA000000)

static void __init spear1310_dt_init(void)
{
	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

static const char * const spear1310_dt_board_compat[] = {
	"st,spear1310",
	"st,spear1310-evb",
	NULL,
};

/*
 * Following will create 16MB static virtual/physical mappings
 * PHYSICAL		VIRTUAL
 * 0xD8000000		0xFA000000
 */
struct map_desc spear1310_io_desc[] __initdata = {
	{
		.virtual	= VA_SPEAR1310_RAS_GRP1_BASE,
		.pfn		= __phys_to_pfn(SPEAR1310_RAS_GRP1_BASE),
		.length		= SZ_16M,
		.type		= MT_DEVICE
	},
};

static void __init spear1310_map_io(void)
{
	iotable_init(spear1310_io_desc, ARRAY_SIZE(spear1310_io_desc));
	spear13xx_map_io();
}

DT_MACHINE_START(SPEAR1310_DT, "ST SPEAr1310 SoC with Flattened Device Tree")
	.smp		=	smp_ops(spear13xx_smp_ops),
	.map_io		=	spear1310_map_io,
	.init_time	=	spear13xx_timer_init,
	.init_machine	=	spear1310_dt_init,
	.restart	=	spear_restart,
	.dt_compat	=	spear1310_dt_board_compat,
MACHINE_END
