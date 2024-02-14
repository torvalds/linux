// SPDX-License-Identifier: GPL-2.0-only
/*
 * OMAP3/OMAP4 Voltage Management Routines
 *
 * Author: Thara Gopinath	<thara@ti.com>
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 * Rajendra Nayak <rnayak@ti.com>
 * Lesly A M <x0080970@ti.com>
 *
 * Copyright (C) 2008 Nokia Corporation
 * Kalle Jokiniemi
 *
 * Copyright (C) 2010 Texas Instruments, Inc.
 * Thara Gopinath <thara@ti.com>
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>

#include "common.h"
#include "soc.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"
#include "voltage.h"
#include "omap_opp_data.h"
#include "vc.h"
#include "vp.h"

static const struct omap_vfsm_instance omap4_vdd_mpu_vfsm = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_MPU_RET_SLEEP_OFFSET,
	.voltsetup_off_reg = OMAP4_PRM_VOLTSETUP_MPU_OFF_OFFSET,
};

static const struct omap_vfsm_instance omap4_vdd_iva_vfsm = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_IVA_RET_SLEEP_OFFSET,
	.voltsetup_off_reg = OMAP4_PRM_VOLTSETUP_IVA_OFF_OFFSET,
};

static const struct omap_vfsm_instance omap4_vdd_core_vfsm = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_CORE_RET_SLEEP_OFFSET,
	.voltsetup_off_reg = OMAP4_PRM_VOLTSETUP_CORE_OFF_OFFSET,
};

static struct voltagedomain omap4_voltdm_mpu = {
	.name = "mpu",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_mpu,
	.vfsm = &omap4_vdd_mpu_vfsm,
	.vp = &omap4_vp_mpu,
};

static struct voltagedomain omap4_voltdm_iva = {
	.name = "iva",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_iva,
	.vfsm = &omap4_vdd_iva_vfsm,
	.vp = &omap4_vp_iva,
};

static struct voltagedomain omap4_voltdm_core = {
	.name = "core",
	.scalable = true,
	.read = omap4_prm_vcvp_read,
	.write = omap4_prm_vcvp_write,
	.rmw = omap4_prm_vcvp_rmw,
	.vc = &omap4_vc_core,
	.vfsm = &omap4_vdd_core_vfsm,
	.vp = &omap4_vp_core,
};

static struct voltagedomain omap4_voltdm_wkup = {
	.name = "wakeup",
};

static struct voltagedomain *voltagedomains_omap4[] __initdata = {
	&omap4_voltdm_mpu,
	&omap4_voltdm_iva,
	&omap4_voltdm_core,
	&omap4_voltdm_wkup,
	NULL,
};

static const char *const sys_clk_name __initconst = "sys_clkin_ck";

void __init omap44xx_voltagedomains_init(void)
{
	struct voltagedomain *voltdm;
	int i;

	/*
	 * XXX Will depend on the process, validation, and binning
	 * for the currently-running IC
	 */
#ifdef CONFIG_PM_OPP
	if (cpu_is_omap443x()) {
		omap4_voltdm_mpu.volt_data = omap443x_vdd_mpu_volt_data;
		omap4_voltdm_iva.volt_data = omap443x_vdd_iva_volt_data;
		omap4_voltdm_core.volt_data = omap443x_vdd_core_volt_data;
	} else if (cpu_is_omap446x()) {
		omap4_voltdm_mpu.volt_data = omap446x_vdd_mpu_volt_data;
		omap4_voltdm_iva.volt_data = omap446x_vdd_iva_volt_data;
		omap4_voltdm_core.volt_data = omap446x_vdd_core_volt_data;
	}
#endif

	omap4_voltdm_mpu.vp_param = &omap4_mpu_vp_data;
	omap4_voltdm_iva.vp_param = &omap4_iva_vp_data;
	omap4_voltdm_core.vp_param = &omap4_core_vp_data;

	omap4_voltdm_mpu.vc_param = &omap4_mpu_vc_data;
	omap4_voltdm_iva.vc_param = &omap4_iva_vc_data;
	omap4_voltdm_core.vc_param = &omap4_core_vc_data;

	for (i = 0; voltdm = voltagedomains_omap4[i], voltdm; i++)
		voltdm->sys_clk.name = sys_clk_name;

	voltdm_init(voltagedomains_omap4);
};
