/*
 * OMAP3 voltage domain data
 *
 * Copyright (C) 2007, 2010 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 * Lesly A M <x0080970@ti.com>
 * Thara Gopinath <thara@ti.com>
 *
 * Copyright (C) 2008, 2011 Nokia Corporation
 * Kalle Jokiniemi
 * Paul Walmsley
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>

#include <plat/common.h>
#include <plat/cpu.h>

#include "prm-regbits-34xx.h"
#include "omap_opp_data.h"
#include "voltage.h"
#include "vc.h"
#include "vp.h"

/*
 * VDD data
 */

static const struct omap_vfsm_instance_data omap3_vdd1_vfsm_data = {
	.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET,
	.voltsetup_shift = OMAP3430_SETUP_TIME1_SHIFT,
	.voltsetup_mask = OMAP3430_SETUP_TIME1_MASK,
};

static struct omap_vdd_info omap3_vdd1_info = {
	.vp_data = &omap3_vp1_data,
	.vc_data = &omap3_vc1_data,
	.vfsm = &omap3_vdd1_vfsm_data,
	.voltdm = {
		.name = "mpu",
	},
};

static const struct omap_vfsm_instance_data omap3_vdd2_vfsm_data = {
	.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET,
	.voltsetup_shift = OMAP3430_SETUP_TIME2_SHIFT,
	.voltsetup_mask = OMAP3430_SETUP_TIME2_MASK,
};

static struct omap_vdd_info omap3_vdd2_info = {
	.vp_data = &omap3_vp2_data,
	.vc_data = &omap3_vc2_data,
	.vfsm = &omap3_vdd2_vfsm_data,
	.voltdm = {
		.name = "core",
	},
};

/* OMAP3 VDD structures */
static struct omap_vdd_info *omap3_vdd_info[] = {
	&omap3_vdd1_info,
	&omap3_vdd2_info,
};

/* OMAP3 specific voltage init functions */
static int __init omap3xxx_voltage_early_init(void)
{
	s16 prm_mod = OMAP3430_GR_MOD;
	s16 prm_irqst_ocp_mod = OCP_MOD;

	if (!cpu_is_omap34xx())
		return 0;

	/*
	 * XXX Will depend on the process, validation, and binning
	 * for the currently-running IC
	 */
	if (cpu_is_omap3630()) {
		omap3_vdd1_info.volt_data = omap36xx_vddmpu_volt_data;
		omap3_vdd2_info.volt_data = omap36xx_vddcore_volt_data;
	} else {
		omap3_vdd1_info.volt_data = omap34xx_vddmpu_volt_data;
		omap3_vdd2_info.volt_data = omap34xx_vddcore_volt_data;
	}

	return omap_voltage_early_init(prm_mod, prm_irqst_ocp_mod,
				       omap3_vdd_info,
				       ARRAY_SIZE(omap3_vdd_info));
};
core_initcall(omap3xxx_voltage_early_init);
