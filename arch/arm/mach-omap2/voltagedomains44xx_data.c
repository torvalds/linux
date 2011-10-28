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
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/init.h>

#include <plat/common.h>

#include "prm-regbits-44xx.h"
#include "prm44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"
#include "voltage.h"
#include "omap_opp_data.h"
#include "vc.h"
#include "vp.h"

static const struct omap_vfsm_instance_data omap4_vdd_mpu_vfsm_data = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_MPU_RET_SLEEP_OFFSET,
};

static struct omap_vdd_info omap4_vdd_mpu_info = {
	.vp_data = &omap4_vp_mpu_data,
	.vc_data = &omap4_vc_mpu_data,
	.vfsm = &omap4_vdd_mpu_vfsm_data,
	.voltdm = {
		.name = "mpu",
	},
};

static const struct omap_vfsm_instance_data omap4_vdd_iva_vfsm_data = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_IVA_RET_SLEEP_OFFSET,
};

static struct omap_vdd_info omap4_vdd_iva_info = {
	.vp_data = &omap4_vp_iva_data,
	.vc_data = &omap4_vc_iva_data,
	.vfsm = &omap4_vdd_iva_vfsm_data,
	.voltdm = {
		.name = "iva",
	},
};

static const struct omap_vfsm_instance_data omap4_vdd_core_vfsm_data = {
	.voltsetup_reg = OMAP4_PRM_VOLTSETUP_CORE_RET_SLEEP_OFFSET,
};

static struct omap_vdd_info omap4_vdd_core_info = {
	.vp_data = &omap4_vp_core_data,
	.vc_data = &omap4_vc_core_data,
	.vfsm = &omap4_vdd_core_vfsm_data,
	.voltdm = {
		.name = "core",
	},
};

/* OMAP4 VDD structures */
static struct omap_vdd_info *omap4_vdd_info[] = {
	&omap4_vdd_mpu_info,
	&omap4_vdd_iva_info,
	&omap4_vdd_core_info,
};

/* OMAP4 specific voltage init functions */
static int __init omap44xx_voltage_early_init(void)
{
	s16 prm_mod = OMAP4430_PRM_DEVICE_INST;
	s16 prm_irqst_ocp_mod = OMAP4430_PRM_OCP_SOCKET_INST;

	if (!cpu_is_omap44xx())
		return 0;

	/*
	 * XXX Will depend on the process, validation, and binning
	 * for the currently-running IC
	 */
	omap4_vdd_mpu_info.volt_data = omap44xx_vdd_mpu_volt_data;
	omap4_vdd_iva_info.volt_data = omap44xx_vdd_iva_volt_data;
	omap4_vdd_core_info.volt_data = omap44xx_vdd_core_volt_data;

	return omap_voltage_early_init(prm_mod, prm_irqst_ocp_mod,
				       omap4_vdd_info,
				       ARRAY_SIZE(omap4_vdd_info));
};
core_initcall(omap44xx_voltage_early_init);
