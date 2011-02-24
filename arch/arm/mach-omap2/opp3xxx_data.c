/*
 * OMAP3 OPP table definitions.
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated - http://www.ti.com/
 *	Nishanth Menon
 *	Kevin Hilman
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

static struct omap_opp_def __initdata omap34xx_opp_def_list[] = {
	/* MPU OPP1 */
	OPP_INITIALIZER("mpu", true, 125000000, 975000),
	/* MPU OPP2 */
	OPP_INITIALIZER("mpu", true, 250000000, 1075000),
	/* MPU OPP3 */
	OPP_INITIALIZER("mpu", true, 500000000, 1200000),
	/* MPU OPP4 */
	OPP_INITIALIZER("mpu", true, 550000000, 1270000),
	/* MPU OPP5 */
	OPP_INITIALIZER("mpu", true, 600000000, 1350000),

	/*
	 * L3 OPP1 - 41.5 MHz is disabled because: The voltage for that OPP is
	 * almost the same than the one at 83MHz thus providing very little
	 * gain for the power point of view. In term of energy it will even
	 * increase the consumption due to the very negative performance
	 * impact that frequency will do to the MPU and the whole system in
	 * general.
	 */
	OPP_INITIALIZER("l3_main", false, 41500000, 975000),
	/* L3 OPP2 */
	OPP_INITIALIZER("l3_main", true, 83000000, 1050000),
	/* L3 OPP3 */
	OPP_INITIALIZER("l3_main", true, 166000000, 1150000),

	/* DSP OPP1 */
	OPP_INITIALIZER("iva", true, 90000000, 975000),
	/* DSP OPP2 */
	OPP_INITIALIZER("iva", true, 180000000, 1075000),
	/* DSP OPP3 */
	OPP_INITIALIZER("iva", true, 360000000, 1200000),
	/* DSP OPP4 */
	OPP_INITIALIZER("iva", true, 400000000, 1270000),
	/* DSP OPP5 */
	OPP_INITIALIZER("iva", true, 430000000, 1350000),
};

static struct omap_opp_def __initdata omap36xx_opp_def_list[] = {
	/* MPU OPP1 - OPP50 */
	OPP_INITIALIZER("mpu", true,  300000000, 1012500),
	/* MPU OPP2 - OPP100 */
	OPP_INITIALIZER("mpu", true,  600000000, 1200000),
	/* MPU OPP3 - OPP-Turbo */
	OPP_INITIALIZER("mpu", false, 800000000, 1325000),
	/* MPU OPP4 - OPP-SB */
	OPP_INITIALIZER("mpu", false, 1000000000, 1375000),

	/* L3 OPP1 - OPP50 */
	OPP_INITIALIZER("l3_main", true, 100000000, 1000000),
	/* L3 OPP2 - OPP100, OPP-Turbo, OPP-SB */
	OPP_INITIALIZER("l3_main", true, 200000000, 1200000),

	/* DSP OPP1 - OPP50 */
	OPP_INITIALIZER("iva", true,  260000000, 1012500),
	/* DSP OPP2 - OPP100 */
	OPP_INITIALIZER("iva", true,  520000000, 1200000),
	/* DSP OPP3 - OPP-Turbo */
	OPP_INITIALIZER("iva", false, 660000000, 1325000),
	/* DSP OPP4 - OPP-SB */
	OPP_INITIALIZER("iva", false, 800000000, 1375000),
};

/**
 * omap3_opp_init() - initialize omap3 opp table
 */
static int __init omap3_opp_init(void)
{
	int r = -ENODEV;

	if (!cpu_is_omap34xx())
		return r;

	if (cpu_is_omap3630())
		r = omap_init_opp_table(omap36xx_opp_def_list,
			ARRAY_SIZE(omap36xx_opp_def_list));
	else
		r = omap_init_opp_table(omap34xx_opp_def_list,
			ARRAY_SIZE(omap34xx_opp_def_list));

	return r;
}
device_initcall(omap3_opp_init);
