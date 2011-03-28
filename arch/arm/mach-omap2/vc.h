/*
 * OMAP3/4 Voltage Controller (VC) structure and macro definitions
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
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 */
#ifndef __ARCH_ARM_MACH_OMAP2_VC_H
#define __ARCH_ARM_MACH_OMAP2_VC_H

#include <linux/kernel.h>

struct voltagedomain;

/**
 * struct omap_vc_common - per-VC register/bitfield data
 * @cmd_on_mask: ON bitmask in PRM_VC_CMD_VAL* register
 * @valid: VALID bitmask in PRM_VC_BYPASS_VAL register
 * @smps_sa_reg: Offset of PRM_VC_SMPS_SA reg from PRM start
 * @smps_volra_reg: Offset of PRM_VC_SMPS_VOL_RA reg from PRM start
 * @bypass_val_reg: Offset of PRM_VC_BYPASS_VAL reg from PRM start
 * @data_shift: DATA field shift in PRM_VC_BYPASS_VAL register
 * @slaveaddr_shift: SLAVEADDR field shift in PRM_VC_BYPASS_VAL register
 * @regaddr_shift: REGADDR field shift in PRM_VC_BYPASS_VAL register
 * @cmd_on_shift: ON field shift in PRM_VC_CMD_VAL_* register
 * @cmd_onlp_shift: ONLP field shift in PRM_VC_CMD_VAL_* register
 * @cmd_ret_shift: RET field shift in PRM_VC_CMD_VAL_* register
 * @cmd_off_shift: OFF field shift in PRM_VC_CMD_VAL_* register
 *
 * XXX One of cmd_on_mask and cmd_on_shift are not needed
 * XXX VALID should probably be a shift, not a mask
 */
struct omap_vc_common {
	u32 cmd_on_mask;
	u32 valid;
	u8 smps_sa_reg;
	u8 smps_volra_reg;
	u8 bypass_val_reg;
	u8 data_shift;
	u8 slaveaddr_shift;
	u8 regaddr_shift;
	u8 cmd_on_shift;
	u8 cmd_onlp_shift;
	u8 cmd_ret_shift;
	u8 cmd_off_shift;
};

/**
 * struct omap_vc_channel - VC per-instance data
 * @common: pointer to VC common data for this platform
 * @smps_sa_mask: SA* bitmask in the PRM_VC_SMPS_SA register
 * @smps_volra_mask: VOLRA* bitmask in the PRM_VC_VOL_RA register
 * @smps_sa_shift: SA* field shift in the PRM_VC_SMPS_SA register
 * @smps_volra_shift: VOLRA* field shift in the PRM_VC_VOL_RA register
 *
 * XXX It is not necessary to have both a *_mask and a *_shift -
 *     remove one
 */
struct omap_vc_channel {
	const struct omap_vc_common *common;
	u32 smps_sa_mask;
	u32 smps_volra_mask;
	u8 cmdval_reg;
	u8 smps_sa_shift;
	u8 smps_volra_shift;
};

extern struct omap_vc_channel omap3_vc_mpu;
extern struct omap_vc_channel omap3_vc_core;

extern struct omap_vc_channel omap4_vc_mpu;
extern struct omap_vc_channel omap4_vc_iva;
extern struct omap_vc_channel omap4_vc_core;

void omap_vc_init_channel(struct voltagedomain *voltdm);
int omap_vc_pre_scale(struct voltagedomain *voltdm,
		      unsigned long target_volt,
		      u8 *target_vsel, u8 *current_vsel);
void omap_vc_post_scale(struct voltagedomain *voltdm,
			unsigned long target_volt,
			u8 target_vsel, u8 current_vsel);
int omap_vc_bypass_scale(struct voltagedomain *voltdm,
			 unsigned long target_volt);

#endif

