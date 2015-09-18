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

struct voltagedomain;

/* XXX document */
#define VP_IDLE_TIMEOUT		200
#define VP_TRANXDONE_TIMEOUT	300

/**
 * struct omap_vp_ops - per-VP operations
 * @check_txdone: check for VP transaction done
 * @clear_txdone: clear VP transaction done status
 */
struct omap_vp_ops {
	u32 (*check_txdone)(u8 vp_id);
	void (*clear_txdone)(u8 vp_id);
};

/**
 * struct omap_vp_common - register data common to all VDDs
 * @vpconfig_erroroffset_mask: ERROROFFSET bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_errorgain_mask: ERRORGAIN bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_initvoltage_mask: INITVOLTAGE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_timeouten: TIMEOUT bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_initvdd: INITVDD bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_forceupdate: FORCEUPDATE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_vpenable: VPENABLE bitmask in the PRM_VP*_CONFIG reg
 * @vpconfig_erroroffset_shift: ERROROFFSET field shift in PRM_VP*_CONFIG reg
 * @vpconfig_errorgain_shift: ERRORGAIN field shift in PRM_VP*_CONFIG reg
 * @vpconfig_initvoltage_shift: INITVOLTAGE field shift in PRM_VP*_CONFIG reg
 * @vstepmin_stepmin_shift: VSTEPMIN field shift in the PRM_VP*_VSTEPMIN reg
 * @vstepmin_smpswaittimemin_shift: SMPSWAITTIMEMIN field shift in PRM_VP*_VSTEPMIN reg
 * @vstepmax_stepmax_shift: VSTEPMAX field shift in the PRM_VP*_VSTEPMAX reg
 * @vstepmax_smpswaittimemax_shift: SMPSWAITTIMEMAX field shift in PRM_VP*_VSTEPMAX reg
 * @vlimitto_vddmin_shift: VDDMIN field shift in PRM_VP*_VLIMITTO reg
 * @vlimitto_vddmax_shift: VDDMAX field shift in PRM_VP*_VLIMITTO reg
 * @vlimitto_timeout_shift: TIMEOUT field shift in PRM_VP*_VLIMITTO reg
 * @vpvoltage_mask: VPVOLTAGE field mask in PRM_VP*_VOLTAGE reg
 */
struct omap_vp_common {
	u32 vpconfig_erroroffset_mask;
	u32 vpconfig_errorgain_mask;
	u32 vpconfig_initvoltage_mask;
	u8 vpconfig_timeouten;
	u8 vpconfig_initvdd;
	u8 vpconfig_forceupdate;
	u8 vpconfig_vpenable;
	u8 vstepmin_stepmin_shift;
	u8 vstepmin_smpswaittimemin_shift;
	u8 vstepmax_stepmax_shift;
	u8 vstepmax_smpswaittimemax_shift;
	u8 vlimitto_vddmin_shift;
	u8 vlimitto_vddmax_shift;
	u8 vlimitto_timeout_shift;
	u8 vpvoltage_mask;

	const struct omap_vp_ops *ops;
};

/**
 * struct omap_vp_instance - VP register offsets (per-VDD)
 * @common: pointer to struct omap_vp_common * for this SoC
 * @vpconfig: PRM_VP*_CONFIG reg offset from PRM start
 * @vstepmin: PRM_VP*_VSTEPMIN reg offset from PRM start
 * @vlimitto: PRM_VP*_VLIMITTO reg offset from PRM start
 * @vstatus: PRM_VP*_VSTATUS reg offset from PRM start
 * @voltage: PRM_VP*_VOLTAGE reg offset from PRM start
 * @id: Unique identifier for VP instance.
 * @enabled: flag to keep track of whether vp is enabled or not
 *
 * XXX vp_common is probably not needed since it is per-SoC
 */
struct omap_vp_instance {
	const struct omap_vp_common *common;
	u8 vpconfig;
	u8 vstepmin;
	u8 vstepmax;
	u8 vlimitto;
	u8 vstatus;
	u8 voltage;
	u8 id;
	bool enabled;
};

extern struct omap_vp_instance omap3_vp_mpu;
extern struct omap_vp_instance omap3_vp_core;

extern struct omap_vp_instance omap4_vp_mpu;
extern struct omap_vp_instance omap4_vp_iva;
extern struct omap_vp_instance omap4_vp_core;

extern struct omap_vp_param omap3_mpu_vp_data;
extern struct omap_vp_param omap3_core_vp_data;

extern struct omap_vp_param omap4_mpu_vp_data;
extern struct omap_vp_param omap4_iva_vp_data;
extern struct omap_vp_param omap4_core_vp_data;

void omap_vp_init(struct voltagedomain *voltdm);
void omap_vp_enable(struct voltagedomain *voltdm);
void omap_vp_disable(struct voltagedomain *voltdm);
int omap_vp_forceupdate_scale(struct voltagedomain *voltdm,
			      unsigned long target_volt);
int omap_vp_update_errorgain(struct voltagedomain *voltdm,
			     unsigned long target_volt);

#endif
