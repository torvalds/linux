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
	.prm_irqst_mod = OCP_MOD,
	.prm_irqst_reg = OMAP3_PRM_IRQSTATUS_MPU_OFFSET,
	.vp_data = &omap3_vp1_data,
	.vc_data = &omap3_vc1_data,
	.vfsm = &omap3_vdd1_vfsm_data,
};

static const struct omap_vfsm_instance_data omap3_vdd2_vfsm_data = {
	.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET,
	.voltsetup_shift = OMAP3430_SETUP_TIME2_SHIFT,
	.voltsetup_mask = OMAP3430_SETUP_TIME2_MASK,
};

static struct omap_vdd_info omap3_vdd2_info = {
	.prm_irqst_mod = OCP_MOD,
	.prm_irqst_reg = OMAP3_PRM_IRQSTATUS_MPU_OFFSET,
	.vp_data = &omap3_vp2_data,
	.vc_data = &omap3_vc2_data,
	.vfsm = &omap3_vdd2_vfsm_data,
};

static struct voltagedomain omap3_voltdm_mpu = {
	.name = "mpu",
	.vdd = &omap3_vdd1_info,
};

static struct voltagedomain omap3_voltdm_core = {
	.name = "core",
	.vdd = &omap3_vdd2_info,
};

static struct voltagedomain *voltagedomains_omap3[] __initdata = {
	&omap3_voltdm_mpu,
	&omap3_voltdm_core,
	NULL,
};

void __init omap3xxx_voltagedomains_init(void)
{
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

	voltdm_init(voltagedomains_omap3);
};
