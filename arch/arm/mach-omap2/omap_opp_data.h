/*
 * OMAP SoC specific OPP Data helpers
 *
 * Copyright (C) 2009-2010 Texas Instruments Incorporated - https://www.ti.com/
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
#ifndef __ARCH_ARM_MACH_OMAP2_OMAP_OPP_DATA_H
#define __ARCH_ARM_MACH_OMAP2_OMAP_OPP_DATA_H

#include "omap_hwmod.h"

#include "voltage.h"

/*
 * *BIG FAT WARNING*:
 * USE the following ONLY in opp data initialization common to an SoC.
 * DO NOT USE these in board files/pm core etc.
 */

/**
 * struct omap_opp_def - OMAP OPP Definition
 * @hwmod_name:	Name of the hwmod for this domain
 * @freq:	Frequency in hertz corresponding to this OPP
 * @u_volt:	Nominal voltage in microvolts corresponding to this OPP
 * @default_available:	True/false - is this OPP available by default
 *
 * OMAP SOCs have a standard set of tuples consisting of frequency and voltage
 * pairs that the device will support per voltage domain. This is called
 * Operating Points or OPP. The actual definitions of OMAP Operating Points
 * varies over silicon within the same family of devices. For a specific
 * domain, you can have a set of {frequency, voltage} pairs and this is denoted
 * by an array of omap_opp_def. As the kernel boots and more information is
 * available, a set of these are activated based on the precise nature of
 * device the kernel boots up on. It is interesting to remember that each IP
 * which belongs to a voltage domain may define their own set of OPPs on top
 * of this - but this is handled by the appropriate driver.
 */
struct omap_opp_def {
	char *hwmod_name;

	unsigned long freq;
	unsigned long u_volt;

	bool default_available;
};

/*
 * Initialization wrapper used to define an OPP for OMAP variants.
 */
#define OPP_INITIALIZER(_hwmod_name, _enabled, _freq, _uv)	\
{								\
	.hwmod_name	= _hwmod_name,				\
	.default_available	= _enabled,			\
	.freq		= _freq,				\
	.u_volt		= _uv,					\
}

/*
 * Initialization wrapper used to define SmartReflex process data
 * XXX Is this needed?  Just use C99 initializers in data files?
 */
#define VOLT_DATA_DEFINE(_v_nom, _efuse_offs, _errminlimit, _errgain)  \
{								       \
	.volt_nominal	= _v_nom,				       \
	.sr_efuse_offs	= _efuse_offs,				       \
	.sr_errminlimit = _errminlimit,				       \
	.vp_errgain	= _errgain				       \
}

/* Use this to initialize the default table */
extern int __init omap_init_opp_table(struct omap_opp_def *opp_def,
		u32 opp_def_size);


extern struct omap_volt_data omap34xx_vddmpu_volt_data[];
extern struct omap_volt_data omap34xx_vddcore_volt_data[];
extern struct omap_volt_data omap36xx_vddmpu_volt_data[];
extern struct omap_volt_data omap36xx_vddcore_volt_data[];

extern struct omap_volt_data omap443x_vdd_mpu_volt_data[];
extern struct omap_volt_data omap443x_vdd_iva_volt_data[];
extern struct omap_volt_data omap443x_vdd_core_volt_data[];
extern struct omap_volt_data omap446x_vdd_mpu_volt_data[];
extern struct omap_volt_data omap446x_vdd_iva_volt_data[];
extern struct omap_volt_data omap446x_vdd_core_volt_data[];

#endif		/* __ARCH_ARM_MACH_OMAP2_OMAP_OPP_DATA_H */
