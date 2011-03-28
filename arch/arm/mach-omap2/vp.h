/*
 * OMAP3/4 Voltage Processor (VP) structure and macro definitions
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
#ifndef __ARCH_ARM_MACH_OMAP2_VP_H
#define __ARCH_ARM_MACH_OMAP2_VP_H

#include <linux/kernel.h>

/* XXX document */
#define VP_IDLE_TIMEOUT		200
#define VP_TRANXDONE_TIMEOUT	300


/**
 * struct omap_vp_common_data - register data common to all VDDs
 * @vpconfig_errorgain_mask: ERRORGAIN bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_initvoltage_mask: INITVOLTAGE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_timeouten_mask: TIMEOUT bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_initvdd: INITVDD bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_forceupdate: FORCEUPDATE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_vpenable: VPENABLE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_erroroffset_shift: ERROROFFSET field shift in PRM_VP*_CONFIG reg
 * @vpconfig_errorgain_shift: ERRORGAIN field shift in PRM_VP*_CONFIG reg
 * @vpconfig_initvoltage_shift: INITVOLTAGE field shift in PRM_VP*_CONFIG reg
 * @vpconfig_stepmin_shift: VSTEPMIN field shift in the PRM_VP*_VSTEPMIN reg
 * @vpconfig_smpswaittimemin_shift: SMPSWAITTIMEMIN field shift in PRM_VP*_VSTEPMIN reg
 * @vpconfig_stepmax_shift: VSTEPMAX field shift in the PRM_VP*_VSTEPMAX reg
 * @vpconfig_smpswaittimemax_shift: SMPSWAITTIMEMAX field shift in PRM_VP*_VSTEPMAX reg
 * @vpconfig_vlimitto_vddmin_shift: VDDMIN field shift in PRM_VP*_VLIMITTO reg
 * @vpconfig_vlimitto_vddmax_shift: VDDMAX field shift in PRM_VP*_VLIMITTO reg
 * @vpconfig_vlimitto_timeout_shift: TIMEOUT field shift in PRM_VP*_VLIMITTO reg
 *
 * XXX It it not necessary to have both a mask and a shift for the same
 *     bitfield - remove one
 * XXX Many of these fields are wrongly named -- e.g., vpconfig_smps* -- fix!
 */
struct omap_vp_common_data {
	u32 vpconfig_errorgain_mask;
	u32 vpconfig_initvoltage_mask;
	u32 vpconfig_timeouten;
	u32 vpconfig_initvdd;
	u32 vpconfig_forceupdate;
	u32 vpconfig_vpenable;
	u8 vpconfig_erroroffset_shift;
	u8 vpconfig_errorgain_shift;
	u8 vpconfig_initvoltage_shift;
	u8 vstepmin_stepmin_shift;
	u8 vstepmin_smpswaittimemin_shift;
	u8 vstepmax_stepmax_shift;
	u8 vstepmax_smpswaittimemax_shift;
	u8 vlimitto_vddmin_shift;
	u8 vlimitto_vddmax_shift;
	u8 vlimitto_timeout_shift;
};

/**
 * struct omap_vp_prm_irqst_data - PRM_IRQSTATUS_MPU.VP_TRANXDONE_ST data
 * @prm_irqst_reg: reg offset for PRM_IRQSTATUS_MPU from top of PRM
 * @tranxdone_status: VP_TRANXDONE_ST bitmask in PRM_IRQSTATUS_MPU reg
 *
 * XXX prm_irqst_reg does not belong here
 * XXX Note that on OMAP3, VP_TRANXDONE interrupt may not work due to a
 *     hardware bug
 * XXX This structure is probably not needed
 */
struct omap_vp_prm_irqst_data {
	u8 prm_irqst_reg;
	u32 tranxdone_status;
};

/**
 * struct omap_vp_instance_data - VP register offsets (per-VDD)
 * @vp_common: pointer to struct omap_vp_common_data * for this SoC
 * @prm_irqst_data: pointer to struct omap_vp_prm_irqst_data for this VDD
 * @vpconfig: PRM_VP*_CONFIG reg offset from PRM start
 * @vstepmin: PRM_VP*_VSTEPMIN reg offset from PRM start
 * @vlimitto: PRM_VP*_VLIMITTO reg offset from PRM start
 * @vstatus: PRM_VP*_VSTATUS reg offset from PRM start
 * @voltage: PRM_VP*_VOLTAGE reg offset from PRM start
 *
 * XXX vp_common is probably not needed since it is per-SoC
 */
struct omap_vp_instance_data {
	const struct omap_vp_common_data *vp_common;
	const struct omap_vp_prm_irqst_data *prm_irqst_data;
	u8 vpconfig;
	u8 vstepmin;
	u8 vstepmax;
	u8 vlimitto;
	u8 vstatus;
	u8 voltage;
};

/**
 * struct omap_vp_runtime_data - VP data populated at runtime by code
 * @vpconfig_erroroffset: value of ERROROFFSET bitfield in PRM_VP*_CONFIG
 * @vpconfig_errorgain: value of ERRORGAIN bitfield in PRM_VP*_CONFIG
 * @vstepmin_smpswaittimemin: value of SMPSWAITTIMEMIN bitfield in PRM_VP*_VSTEPMIN
 * @vstepmax_smpswaittimemax: value of SMPSWAITTIMEMAX bitfield in PRM_VP*_VSTEPMAX
 * @vlimitto_timeout: value of TIMEOUT bitfield in PRM_VP*_VLIMITTO
 * @vstepmin_stepmin: value of VSTEPMIN bitfield in PRM_VP*_VSTEPMIN
 * @vstepmax_stepmax: value of VSTEPMAX bitfield in PRM_VP*_VSTEPMAX
 * @vlimitto_vddmin: value of VDDMIN bitfield in PRM_VP*_VLIMITTO
 * @vlimitto_vddmax: value of VDDMAX bitfield in PRM_VP*_VLIMITTO
 *
 * XXX Is this structure really needed?  Why not just program the
 * device directly?  They are in PRM space, therefore in the WKUP
 * powerdomain, so register contents should not be lost in off-mode.
 * XXX Some of these fields are incorrectly named, e.g., vstep*
 */
struct omap_vp_runtime_data {
	u32 vpconfig_erroroffset;
	u16 vpconfig_errorgain;
	u16 vstepmin_smpswaittimemin;
	u16 vstepmax_smpswaittimemax;
	u16 vlimitto_timeout;
	u8 vstepmin_stepmin;
	u8 vstepmax_stepmax;
	u8 vlimitto_vddmin;
	u8 vlimitto_vddmax;
};

extern struct omap_vp_instance_data omap3_vp1_data;
extern struct omap_vp_instance_data omap3_vp2_data;

extern struct omap_vp_instance_data omap4_vp_mpu_data;
extern struct omap_vp_instance_data omap4_vp_iva_data;
extern struct omap_vp_instance_data omap4_vp_core_data;

#endif
