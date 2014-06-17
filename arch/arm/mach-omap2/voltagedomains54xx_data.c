/*
 * OMAP5 Voltage Management Routines
 *
 * Based on voltagedomains44xx_data.c
 *
 * Copyright (C) 2013 Texas Instruments Incorporated - http://www.ti.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>

#include "common.h"

#include "prm54xx.h"
#include "voltage.h"
#include "omap_opp_data.h"
#include "vc.h"
#include "vp.h"

static const struct omap_vfsm_instance omap5_vdd_mpu_vfsm = {
	.voltsetup_reg = OMAP54XX_PRM_VOLTSETUP_MPU_RET_SLEEP_OFFSET,
};

static const struct omap_vfsm_instance omap5_vdd_mm_vfsm = {
	.voltsetup_reg = OMAP54XX_PRM_VOLTSETUP_MM_RET_SLEEP_OFFSET,
};

static const struct omap_vfsm_instance omap5_vdd_core_vfsm = {
	.voltsetup_reg = OMAP54XX_PRM_VOLTSETUP_CORE_RET_SLEEP_OFFSET,
};

static struct voltagedomain omap5_voltdm_mpu = {
	.name = "mpu",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_mpu,
	.vfsm = &omap5_vdd_mpu_vfsm,
	.vp = &omap4_vp_mpu,
};

static struct voltagedomain omap5_voltdm_mm = {
	.name = "mm",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_iva,
	.vfsm = &omap5_vdd_mm_vfsm,
	.vp = &omap4_vp_iva,
};

static struct voltagedomain omap5_voltdm_core = {
	.name = "core",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_core,
	.vfsm = &omap5_vdd_core_vfsm,
	.vp = &omap4_vp_core,
};

static struct voltagedomain omap5_voltdm_wkup = {
	.name = "wkup",
};

static struct voltagedomain *voltagedomains_omap5[] __initdata = {
	&omap5_voltdm_mpu,
	&omap5_voltdm_mm,
	&omap5_voltdm_core,
	&omap5_voltdm_wkup,
	NULL,
};

static const char *sys_clk_name __initdata = "sys_clkin";

void __init omap54xx_voltagedomains_init(void)
{
	struct voltagedomain *voltdm;
	int i;

	for (i = 0; voltdm = voltagedomains_omap5[i], voltdm; i++)
		voltdm->sys_clk.name = sys_clk_name;

	voltdm_init(voltagedomains_omap5);
};
