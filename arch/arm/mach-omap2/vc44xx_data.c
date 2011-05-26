/*
 * OMAP4 Voltage Controller (VC) data
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
#include <linux/io.h>
#include <linux/err.h>
#include <linux/init.h>

#include <plat/common.h>

#include "prm44xx.h"
#include "prm-regbits-44xx.h"
#include "voltage.h"

#include "vc.h"

/*
 * VC data common to 44xx chips
 * XXX This stuff presumably belongs in the vc3xxx.c or vc.c file.
 */
static const struct omap_vc_common_data omap4_vc_common = {
	.smps_sa_reg = OMAP4_PRM_VC_SMPS_SA_OFFSET,
	.smps_volra_reg = OMAP4_PRM_VC_VAL_SMPS_RA_VOL_OFFSET,
	.bypass_val_reg = OMAP4_PRM_VC_VAL_BYPASS_OFFSET,
	.data_shift = OMAP4430_DATA_SHIFT,
	.slaveaddr_shift = OMAP4430_SLAVEADDR_SHIFT,
	.regaddr_shift = OMAP4430_REGADDR_SHIFT,
	.valid = OMAP4430_VALID_MASK,
	.cmd_on_shift = OMAP4430_ON_SHIFT,
	.cmd_on_mask = OMAP4430_ON_MASK,
	.cmd_onlp_shift = OMAP4430_ONLP_SHIFT,
	.cmd_ret_shift = OMAP4430_RET_SHIFT,
	.cmd_off_shift = OMAP4430_OFF_SHIFT,
};

/* VC instance data for each controllable voltage line */
struct omap_vc_instance_data omap4_vc_mpu_data = {
	.vc_common = &omap4_vc_common,
	.cmdval_reg = OMAP4_PRM_VC_VAL_CMD_VDD_MPU_L_OFFSET,
	.smps_sa_shift = OMAP4430_SA_VDD_MPU_L_PRM_VC_SMPS_SA_SHIFT,
	.smps_sa_mask = OMAP4430_SA_VDD_MPU_L_PRM_VC_SMPS_SA_MASK,
	.smps_volra_shift = OMAP4430_VOLRA_VDD_MPU_L_SHIFT,
	.smps_volra_mask = OMAP4430_VOLRA_VDD_MPU_L_MASK,
};

struct omap_vc_instance_data omap4_vc_iva_data = {
	.vc_common = &omap4_vc_common,
	.cmdval_reg = OMAP4_PRM_VC_VAL_CMD_VDD_IVA_L_OFFSET,
	.smps_sa_shift = OMAP4430_SA_VDD_IVA_L_PRM_VC_SMPS_SA_SHIFT,
	.smps_sa_mask = OMAP4430_SA_VDD_IVA_L_PRM_VC_SMPS_SA_MASK,
	.smps_volra_shift = OMAP4430_VOLRA_VDD_IVA_L_SHIFT,
	.smps_volra_mask = OMAP4430_VOLRA_VDD_IVA_L_MASK,
};

struct omap_vc_instance_data omap4_vc_core_data = {
	.vc_common = &omap4_vc_common,
	.cmdval_reg = OMAP4_PRM_VC_VAL_CMD_VDD_CORE_L_OFFSET,
	.smps_sa_shift = OMAP4430_SA_VDD_CORE_L_0_6_SHIFT,
	.smps_sa_mask = OMAP4430_SA_VDD_CORE_L_0_6_MASK,
	.smps_volra_shift = OMAP4430_VOLRA_VDD_CORE_L_SHIFT,
	.smps_volra_mask = OMAP4430_VOLRA_VDD_CORE_L_MASK,
};

