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

#include "common.h"
#include <plat/cpu.h>

#include "prm-regbits-34xx.h"
#include "omap_opp_data.h"
#include "voltage.h"
#include "vc.h"
#include "vp.h"

/*
 * VDD data
 */

/* OMAP3-common voltagedomain data */

static struct voltagedomain omap3_voltdm_wkup = {
	.name = "wakeup",
};

/* 34xx/36xx voltagedomain data */

static const struct omap_vfsm_instance omap3_vdd1_vfsm = {
	.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET,
	.voltsetup_mask = OMAP3430_SETUP_TIME1_MASK,
};

static const struct omap_vfsm_instance omap3_vdd2_vfsm = {
	.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET,
	.voltsetup_mask = OMAP3430_SETUP_TIME2_MASK,
};

static struct voltagedomain omap3_voltdm_mpu = {
	.name = "mpu_iva",
	.scalable = true,
	.read = omap3_prm_vcvp_read,
	.write = omap3_prm_vcvp_write,
	.rmw = omap3_prm_vcvp_rmw,
	.vc = &omap3_vc_mpu,
	.vfsm = &omap3_vdd1_vfsm,
	.vp = &omap3_vp_mpu,
};

static struct voltagedomain omap3_voltdm_core = {
	.name = "core",
	.scalable = true,
	.read = omap3_prm_vcvp_read,
	.write = omap3_prm_vcvp_write,
	.rmw = omap3_prm_vcvp_rmw,
	.vc = &omap3_vc_core,
	.vfsm = &omap3_vdd2_vfsm,
	.vp = &omap3_vp_core,
};

static struct voltagedomain *voltagedomains_omap3[] __initdata = {
	&omap3_voltdm_mpu,
	&omap3_voltdm_core,
	&omap3_voltdm_wkup,
	NULL,
};

/* AM35xx voltagedomain data */

static struct voltagedomain am35xx_voltdm_mpu = {
	.name = "mpu_iva",
};

static struct voltagedomain am35xx_voltdm_core = {
	.name = "core",
};

static struct voltagedomain *voltagedomains_am35xx[] __initdata = {
	&am35xx_voltdm_mpu,
	&am35xx_voltdm_core,
	&omap3_voltdm_wkup,
	NULL,
};


static const char *sys_clk_name __initdata = "sys_ck";

void __init omap3xxx_voltagedomains_init(void)
{
	struct voltagedomain *voltdm;
	struct voltagedomain **voltdms;
	int i;

	/*
	 * XXX Will depend on the process, validation, and binning
	 * for the currently-running IC
	 */
#ifdef CONFIG_PM_OPP
	if (cpu_is_omap3630()) {
		omap3_voltdm_mpu.volt_data = omap36xx_vddmpu_volt_data;
		omap3_voltdm_core.volt_data = omap36xx_vddcore_volt_data;
	} else {
		omap3_voltdm_mpu.volt_data = omap34xx_vddmpu_volt_data;
		omap3_voltdm_core.volt_data = omap34xx_vddcore_volt_data;
	}
#endif

	if (cpu_is_omap3517() || cpu_is_omap3505())
		voltdms = voltagedomains_am35xx;
	else
		voltdms = voltagedomains_omap3;

	for (i = 0; voltdm = voltdms[i], voltdm; i++)
		voltdm->sys_clk.name = sys_clk_name;

	voltdm_init(voltdms);
};
