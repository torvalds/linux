/*
 * OMAP4 OPP table definitions.
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *	Kevin Hilman
 *	Thara Gopinath
 * Copyright (C) 2010 Nokia Corporation.
 *      Eduardo Valentin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>

#include <plat/cpu.h>

#include "omap_opp_data.h"

static struct omap_opp_def __initdata omap44xx_opp_def_list[] = {
	/* MPU OPP1 - OPP50 */
	OPP_INITIALIZER("mpu", true, 300000000, 1100000),
	/* MPU OPP2 - OPP100 */
	OPP_INITIALIZER("mpu", true, 600000000, 1200000),
	/* MPU OPP3 - OPP-Turbo */
	OPP_INITIALIZER("mpu", false, 800000000, 1260000),
	/* MPU OPP4 - OPP-SB */
	OPP_INITIALIZER("mpu", false, 1008000000, 1350000),
	/* L3 OPP1 - OPP50 */
	OPP_INITIALIZER("l3_main_1", true, 100000000, 930000),
	/* L3 OPP2 - OPP100, OPP-Turbo, OPP-SB */
	OPP_INITIALIZER("l3_main_1", true, 200000000, 1100000),
	/* TODO: add IVA, DSP, aess, fdif, gpu */
};

/**
 * omap4_opp_init() - initialize omap4 opp table
 */
static int __init omap4_opp_init(void)
{
	int r = -ENODEV;

	if (!cpu_is_omap44xx())
		return r;

	r = omap_init_opp_table(omap44xx_opp_def_list,
			ARRAY_SIZE(omap44xx_opp_def_list));

	return r;
}
device_initcall(omap4_opp_init);
