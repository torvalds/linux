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

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/debugfs.h>
#include <linux/slab.h>

#include <plat/common.h>
#include <plat/voltage.h>

#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"
#include "prcm44xx.h"
#include "prminst44xx.h"
#include "control.h"

#define VP_IDLE_TIMEOUT		200
#define VP_TRANXDONE_TIMEOUT	300
#define VOLTAGE_DIR_SIZE	16

/* Voltage processor register offsets */
struct vp_reg_offs {
	u8 vpconfig;
	u8 vstepmin;
	u8 vstepmax;
	u8 vlimitto;
	u8 vstatus;
	u8 voltage;
};

/* Voltage Processor bit field values, shifts and masks */
struct vp_reg_val {
	/* PRM module */
	u16 prm_mod;
	/* VPx_VPCONFIG */
	u32 vpconfig_erroroffset;
	u16 vpconfig_errorgain;
	u32 vpconfig_errorgain_mask;
	u8 vpconfig_errorgain_shift;
	u32 vpconfig_initvoltage_mask;
	u8 vpconfig_initvoltage_shift;
	u32 vpconfig_timeouten;
	u32 vpconfig_initvdd;
	u32 vpconfig_forceupdate;
	u32 vpconfig_vpenable;
	/* VPx_VSTEPMIN */
	u8 vstepmin_stepmin;
	u16 vstepmin_smpswaittimemin;
	u8 vstepmin_stepmin_shift;
	u8 vstepmin_smpswaittimemin_shift;
	/* VPx_VSTEPMAX */
	u8 vstepmax_stepmax;
	u16 vstepmax_smpswaittimemax;
	u8 vstepmax_stepmax_shift;
	u8 vstepmax_smpswaittimemax_shift;
	/* VPx_VLIMITTO */
	u8 vlimitto_vddmin;
	u8 vlimitto_vddmax;
	u16 vlimitto_timeout;
	u8 vlimitto_vddmin_shift;
	u8 vlimitto_vddmax_shift;
	u8 vlimitto_timeout_shift;
	/* PRM_IRQSTATUS*/
	u32 tranxdone_status;
};

/* Voltage controller registers and offsets */
struct vc_reg_info {
	/* PRM module */
	u16 prm_mod;
	/* VC register offsets */
	u8 smps_sa_reg;
	u8 smps_volra_reg;
	u8 bypass_val_reg;
	u8 cmdval_reg;
	u8 voltsetup_reg;
	/*VC_SMPS_SA*/
	u8 smps_sa_shift;
	u32 smps_sa_mask;
	/* VC_SMPS_VOL_RA */
	u8 smps_volra_shift;
	u32 smps_volra_mask;
	/* VC_BYPASS_VAL */
	u8 data_shift;
	u8 slaveaddr_shift;
	u8 regaddr_shift;
	u32 valid;
	/* VC_CMD_VAL */
	u8 cmd_on_shift;
	u8 cmd_onlp_shift;
	u8 cmd_ret_shift;
	u8 cmd_off_shift;
	u32 cmd_on_mask;
	/* PRM_VOLTSETUP */
	u8 voltsetup_shift;
	u32 voltsetup_mask;
};

/**
 * omap_vdd_info - Per Voltage Domain info
 *
 * @volt_data		: voltage table having the distinct voltages supported
 *			  by the domain and other associated per voltage data.
 * @pmic_info		: pmic specific parameters which should be populted by
 *			  the pmic drivers.
 * @vp_offs		: structure containing the offsets for various
 *			  vp registers
 * @vp_reg		: the register values, shifts, masks for various
 *			  vp registers
 * @vc_reg		: structure containing various various vc registers,
 *			  shifts, masks etc.
 * @voltdm		: pointer to the voltage domain structure
 * @debug_dir		: debug directory for this voltage domain.
 * @curr_volt		: current voltage for this vdd.
 * @ocp_mod		: The prm module for accessing the prm irqstatus reg.
 * @prm_irqst_reg	: prm irqstatus register.
 * @vp_enabled		: flag to keep track of whether vp is enabled or not
 * @volt_scale		: API to scale the voltage of the vdd.
 */
struct omap_vdd_info {
	struct omap_volt_data *volt_data;
	struct omap_volt_pmic_info *pmic_info;
	struct vp_reg_offs vp_offs;
	struct vp_reg_val vp_reg;
	struct vc_reg_info vc_reg;
	struct voltagedomain voltdm;
	struct dentry *debug_dir;
	u32 curr_volt;
	u16 ocp_mod;
	u8 prm_irqst_reg;
	bool vp_enabled;
	u32 (*read_reg) (u16 mod, u8 offset);
	void (*write_reg) (u32 val, u16 mod, u8 offset);
	int (*volt_scale) (struct omap_vdd_info *vdd,
		unsigned long target_volt);
};

static struct omap_vdd_info *vdd_info;
/*
 * Number of scalable voltage domains.
 */
static int nr_scalable_vdd;

/* OMAP3 VDD sturctures */
static struct omap_vdd_info omap3_vdd_info[] = {
	{
		.vp_offs = {
			.vpconfig = OMAP3_PRM_VP1_CONFIG_OFFSET,
			.vstepmin = OMAP3_PRM_VP1_VSTEPMIN_OFFSET,
			.vstepmax = OMAP3_PRM_VP1_VSTEPMAX_OFFSET,
			.vlimitto = OMAP3_PRM_VP1_VLIMITTO_OFFSET,
			.vstatus = OMAP3_PRM_VP1_STATUS_OFFSET,
			.voltage = OMAP3_PRM_VP1_VOLTAGE_OFFSET,
		},
		.voltdm = {
			.name = "mpu",
		},
	},
	{
		.vp_offs = {
			.vpconfig = OMAP3_PRM_VP2_CONFIG_OFFSET,
			.vstepmin = OMAP3_PRM_VP2_VSTEPMIN_OFFSET,
			.vstepmax = OMAP3_PRM_VP2_VSTEPMAX_OFFSET,
			.vlimitto = OMAP3_PRM_VP2_VLIMITTO_OFFSET,
			.vstatus = OMAP3_PRM_VP2_STATUS_OFFSET,
			.voltage = OMAP3_PRM_VP2_VOLTAGE_OFFSET,
		},
		.voltdm = {
			.name = "core",
		},
	},
};

#define OMAP3_NR_SCALABLE_VDD ARRAY_SIZE(omap3_vdd_info)

/* OMAP4 VDD sturctures */
static struct omap_vdd_info omap4_vdd_info[] = {
	{
		.vp_offs = {
			.vpconfig = OMAP4_PRM_VP_MPU_CONFIG_OFFSET,
			.vstepmin = OMAP4_PRM_VP_MPU_VSTEPMIN_OFFSET,
			.vstepmax = OMAP4_PRM_VP_MPU_VSTEPMAX_OFFSET,
			.vlimitto = OMAP4_PRM_VP_MPU_VLIMITTO_OFFSET,
			.vstatus = OMAP4_PRM_VP_MPU_STATUS_OFFSET,
			.voltage = OMAP4_PRM_VP_MPU_VOLTAGE_OFFSET,
		},
		.voltdm = {
			.name = "mpu",
		},
	},
	{
		.vp_offs = {
			.vpconfig = OMAP4_PRM_VP_IVA_CONFIG_OFFSET,
			.vstepmin = OMAP4_PRM_VP_IVA_VSTEPMIN_OFFSET,
			.vstepmax = OMAP4_PRM_VP_IVA_VSTEPMAX_OFFSET,
			.vlimitto = OMAP4_PRM_VP_IVA_VLIMITTO_OFFSET,
			.vstatus = OMAP4_PRM_VP_IVA_STATUS_OFFSET,
			.voltage = OMAP4_PRM_VP_IVA_VOLTAGE_OFFSET,
		},
		.voltdm = {
			.name = "iva",
		},
	},
	{
		.vp_offs = {
			.vpconfig = OMAP4_PRM_VP_CORE_CONFIG_OFFSET,
			.vstepmin = OMAP4_PRM_VP_CORE_VSTEPMIN_OFFSET,
			.vstepmax = OMAP4_PRM_VP_CORE_VSTEPMAX_OFFSET,
			.vlimitto = OMAP4_PRM_VP_CORE_VLIMITTO_OFFSET,
			.vstatus = OMAP4_PRM_VP_CORE_STATUS_OFFSET,
			.voltage = OMAP4_PRM_VP_CORE_VOLTAGE_OFFSET,
		},
		.voltdm = {
			.name = "core",
		},
	},
};

#define OMAP4_NR_SCALABLE_VDD ARRAY_SIZE(omap4_vdd_info)

/*
 * Structures containing OMAP3430/OMAP3630 voltage supported and various
 * voltage dependent data for each VDD.
 */
#define VOLT_DATA_DEFINE(_v_nom, _efuse_offs, _errminlimit, _errgain)	\
{									\
	.volt_nominal	= _v_nom,					\
	.sr_efuse_offs	= _efuse_offs,					\
	.sr_errminlimit	= _errminlimit,					\
	.vp_errgain	= _errgain					\
}

/* VDD1 */
static struct omap_volt_data omap34xx_vddmpu_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP1_UV, OMAP343X_CONTROL_FUSE_OPP1_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP2_UV, OMAP343X_CONTROL_FUSE_OPP2_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP3_UV, OMAP343X_CONTROL_FUSE_OPP3_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP4_UV, OMAP343X_CONTROL_FUSE_OPP4_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(OMAP3430_VDD_MPU_OPP5_UV, OMAP343X_CONTROL_FUSE_OPP5_VDD1, 0xf9, 0x18),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

static struct omap_volt_data omap36xx_vddmpu_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP50_UV, OMAP3630_CONTROL_FUSE_OPP50_VDD1, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP100_UV, OMAP3630_CONTROL_FUSE_OPP100_VDD1, 0xf9, 0x16),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP120_UV, OMAP3630_CONTROL_FUSE_OPP120_VDD1, 0xfa, 0x23),
	VOLT_DATA_DEFINE(OMAP3630_VDD_MPU_OPP1G_UV, OMAP3630_CONTROL_FUSE_OPP1G_VDD1, 0xfa, 0x27),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

/* VDD2 */
static struct omap_volt_data omap34xx_vddcore_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP1_UV, OMAP343X_CONTROL_FUSE_OPP1_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP2_UV, OMAP343X_CONTROL_FUSE_OPP2_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3430_VDD_CORE_OPP3_UV, OMAP343X_CONTROL_FUSE_OPP3_VDD2, 0xf9, 0x18),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

static struct omap_volt_data omap36xx_vddcore_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP3630_VDD_CORE_OPP50_UV, OMAP3630_CONTROL_FUSE_OPP50_VDD2, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP3630_VDD_CORE_OPP100_UV, OMAP3630_CONTROL_FUSE_OPP100_VDD2, 0xf9, 0x16),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

/*
 * Structures containing OMAP4430 voltage supported and various
 * voltage dependent data for each VDD.
 */
static struct omap_volt_data omap44xx_vdd_mpu_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP4430_VDD_MPU_OPP50_UV, OMAP44XX_CONTROL_FUSE_MPU_OPP50, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP4430_VDD_MPU_OPP100_UV, OMAP44XX_CONTROL_FUSE_MPU_OPP100, 0xf9, 0x16),
	VOLT_DATA_DEFINE(OMAP4430_VDD_MPU_OPPTURBO_UV, OMAP44XX_CONTROL_FUSE_MPU_OPPTURBO, 0xfa, 0x23),
	VOLT_DATA_DEFINE(OMAP4430_VDD_MPU_OPPNITRO_UV, OMAP44XX_CONTROL_FUSE_MPU_OPPNITRO, 0xfa, 0x27),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

static struct omap_volt_data omap44xx_vdd_iva_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP4430_VDD_IVA_OPP50_UV, OMAP44XX_CONTROL_FUSE_IVA_OPP50, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP4430_VDD_IVA_OPP100_UV, OMAP44XX_CONTROL_FUSE_IVA_OPP100, 0xf9, 0x16),
	VOLT_DATA_DEFINE(OMAP4430_VDD_IVA_OPPTURBO_UV, OMAP44XX_CONTROL_FUSE_IVA_OPPTURBO, 0xfa, 0x23),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

static struct omap_volt_data omap44xx_vdd_core_volt_data[] = {
	VOLT_DATA_DEFINE(OMAP4430_VDD_CORE_OPP50_UV, OMAP44XX_CONTROL_FUSE_CORE_OPP50, 0xf4, 0x0c),
	VOLT_DATA_DEFINE(OMAP4430_VDD_CORE_OPP100_UV, OMAP44XX_CONTROL_FUSE_CORE_OPP100, 0xf9, 0x16),
	VOLT_DATA_DEFINE(0, 0, 0, 0),
};

static struct dentry *voltage_dir;

/* Init function pointers */
static void (*vc_init) (struct omap_vdd_info *vdd);
static int (*vdd_data_configure) (struct omap_vdd_info *vdd);

static u32 omap3_voltage_read_reg(u16 mod, u8 offset)
{
	return omap2_prm_read_mod_reg(mod, offset);
}

static void omap3_voltage_write_reg(u32 val, u16 mod, u8 offset)
{
	omap2_prm_write_mod_reg(val, mod, offset);
}

static u32 omap4_voltage_read_reg(u16 mod, u8 offset)
{
	return omap4_prminst_read_inst_reg(OMAP4430_PRM_PARTITION,
					mod, offset);
}

static void omap4_voltage_write_reg(u32 val, u16 mod, u8 offset)
{
	omap4_prminst_write_inst_reg(val, OMAP4430_PRM_PARTITION, mod, offset);
}

/* Voltage debugfs support */
static int vp_volt_debug_get(void *data, u64 *val)
{
	struct omap_vdd_info *vdd = (struct omap_vdd_info *) data;
	u8 vsel;

	if (!vdd) {
		pr_warning("Wrong paramater passed\n");
		return -EINVAL;
	}

	vsel = vdd->read_reg(vdd->vp_reg.prm_mod, vdd->vp_offs.voltage);
	pr_notice("curr_vsel = %x\n", vsel);

	if (!vdd->pmic_info->vsel_to_uv) {
		pr_warning("PMIC function to convert vsel to voltage"
			"in uV not registerd\n");
		return -EINVAL;
	}

	*val = vdd->pmic_info->vsel_to_uv(vsel);
	return 0;
}

static int nom_volt_debug_get(void *data, u64 *val)
{
	struct omap_vdd_info *vdd = (struct omap_vdd_info *) data;

	if (!vdd) {
		pr_warning("Wrong paramater passed\n");
		return -EINVAL;
	}

	*val = omap_voltage_get_nom_volt(&vdd->voltdm);

	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(vp_volt_debug_fops, vp_volt_debug_get, NULL, "%llu\n");
DEFINE_SIMPLE_ATTRIBUTE(nom_volt_debug_fops, nom_volt_debug_get, NULL,
								"%llu\n");
static void vp_latch_vsel(struct omap_vdd_info *vdd)
{
	u32 vpconfig;
	u16 mod;
	unsigned long uvdc;
	char vsel;

	uvdc = omap_voltage_get_nom_volt(&vdd->voltdm);
	if (!uvdc) {
		pr_warning("%s: unable to find current voltage for vdd_%s\n",
			__func__, vdd->voltdm.name);
		return;
	}

	if (!vdd->pmic_info || !vdd->pmic_info->uv_to_vsel) {
		pr_warning("%s: PMIC function to convert voltage in uV to"
			" vsel not registered\n", __func__);
		return;
	}

	mod = vdd->vp_reg.prm_mod;

	vsel = vdd->pmic_info->uv_to_vsel(uvdc);

	vpconfig = vdd->read_reg(mod, vdd->vp_offs.vpconfig);
	vpconfig &= ~(vdd->vp_reg.vpconfig_initvoltage_mask |
			vdd->vp_reg.vpconfig_initvdd);
	vpconfig |= vsel << vdd->vp_reg.vpconfig_initvoltage_shift;

	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	vdd->write_reg((vpconfig | vdd->vp_reg.vpconfig_initvdd), mod,
			vdd->vp_offs.vpconfig);

	/* Clear initVDD copy trigger bit */
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);
}

/* Generic voltage init functions */
static void __init vp_init(struct omap_vdd_info *vdd)
{
	u32 vp_val;
	u16 mod;

	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, vdd->voltdm.name);
		return;
	}

	mod = vdd->vp_reg.prm_mod;

	vp_val = vdd->vp_reg.vpconfig_erroroffset |
		(vdd->vp_reg.vpconfig_errorgain <<
		vdd->vp_reg.vpconfig_errorgain_shift) |
		vdd->vp_reg.vpconfig_timeouten;
	vdd->write_reg(vp_val, mod, vdd->vp_offs.vpconfig);

	vp_val = ((vdd->vp_reg.vstepmin_smpswaittimemin <<
		vdd->vp_reg.vstepmin_smpswaittimemin_shift) |
		(vdd->vp_reg.vstepmin_stepmin <<
		vdd->vp_reg.vstepmin_stepmin_shift));
	vdd->write_reg(vp_val, mod, vdd->vp_offs.vstepmin);

	vp_val = ((vdd->vp_reg.vstepmax_smpswaittimemax <<
		vdd->vp_reg.vstepmax_smpswaittimemax_shift) |
		(vdd->vp_reg.vstepmax_stepmax <<
		vdd->vp_reg.vstepmax_stepmax_shift));
	vdd->write_reg(vp_val, mod, vdd->vp_offs.vstepmax);

	vp_val = ((vdd->vp_reg.vlimitto_vddmax <<
		vdd->vp_reg.vlimitto_vddmax_shift) |
		(vdd->vp_reg.vlimitto_vddmin <<
		vdd->vp_reg.vlimitto_vddmin_shift) |
		(vdd->vp_reg.vlimitto_timeout <<
		vdd->vp_reg.vlimitto_timeout_shift));
	vdd->write_reg(vp_val, mod, vdd->vp_offs.vlimitto);
}

static void __init vdd_debugfs_init(struct omap_vdd_info *vdd)
{
	char *name;

	name = kzalloc(VOLTAGE_DIR_SIZE, GFP_KERNEL);
	if (!name) {
		pr_warning("%s: Unable to allocate memory for debugfs"
			" directory name for vdd_%s",
			__func__, vdd->voltdm.name);
		return;
	}
	strcpy(name, "vdd_");
	strcat(name, vdd->voltdm.name);

	vdd->debug_dir = debugfs_create_dir(name, voltage_dir);
	kfree(name);
	if (IS_ERR(vdd->debug_dir)) {
		pr_warning("%s: Unable to create debugfs directory for"
			" vdd_%s\n", __func__, vdd->voltdm.name);
		vdd->debug_dir = NULL;
		return;
	}

	(void) debugfs_create_x16("vp_errorgain", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vpconfig_errorgain));
	(void) debugfs_create_x16("vp_smpswaittimemin", S_IRUGO,
				vdd->debug_dir,
				&(vdd->vp_reg.vstepmin_smpswaittimemin));
	(void) debugfs_create_x8("vp_stepmin", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vstepmin_stepmin));
	(void) debugfs_create_x16("vp_smpswaittimemax", S_IRUGO,
				vdd->debug_dir,
				&(vdd->vp_reg.vstepmax_smpswaittimemax));
	(void) debugfs_create_x8("vp_stepmax", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vstepmax_stepmax));
	(void) debugfs_create_x8("vp_vddmax", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vlimitto_vddmax));
	(void) debugfs_create_x8("vp_vddmin", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vlimitto_vddmin));
	(void) debugfs_create_x16("vp_timeout", S_IRUGO, vdd->debug_dir,
				&(vdd->vp_reg.vlimitto_timeout));
	(void) debugfs_create_file("curr_vp_volt", S_IRUGO, vdd->debug_dir,
				(void *) vdd, &vp_volt_debug_fops);
	(void) debugfs_create_file("curr_nominal_volt", S_IRUGO,
				vdd->debug_dir, (void *) vdd,
				&nom_volt_debug_fops);
}

/* Voltage scale and accessory APIs */
static int _pre_volt_scale(struct omap_vdd_info *vdd,
		unsigned long target_volt, u8 *target_vsel, u8 *current_vsel)
{
	struct omap_volt_data *volt_data;
	u32 vc_cmdval, vp_errgain_val;
	u16 vp_mod, vc_mod;

	/* Check if suffiecient pmic info is available for this vdd */
	if (!vdd->pmic_info) {
		pr_err("%s: Insufficient pmic info to scale the vdd_%s\n",
			__func__, vdd->voltdm.name);
		return -EINVAL;
	}

	if (!vdd->pmic_info->uv_to_vsel) {
		pr_err("%s: PMIC function to convert voltage in uV to"
			"vsel not registered. Hence unable to scale voltage"
			"for vdd_%s\n", __func__, vdd->voltdm.name);
		return -ENODATA;
	}

	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, vdd->voltdm.name);
		return -EINVAL;
	}

	vp_mod = vdd->vp_reg.prm_mod;
	vc_mod = vdd->vc_reg.prm_mod;

	/* Get volt_data corresponding to target_volt */
	volt_data = omap_voltage_get_voltdata(&vdd->voltdm, target_volt);
	if (IS_ERR(volt_data))
		volt_data = NULL;

	*target_vsel = vdd->pmic_info->uv_to_vsel(target_volt);
	*current_vsel = vdd->read_reg(vp_mod, vdd->vp_offs.voltage);

	/* Setting the ON voltage to the new target voltage */
	vc_cmdval = vdd->read_reg(vc_mod, vdd->vc_reg.cmdval_reg);
	vc_cmdval &= ~vdd->vc_reg.cmd_on_mask;
	vc_cmdval |= (*target_vsel << vdd->vc_reg.cmd_on_shift);
	vdd->write_reg(vc_cmdval, vc_mod, vdd->vc_reg.cmdval_reg);

	/* Setting vp errorgain based on the voltage */
	if (volt_data) {
		vp_errgain_val = vdd->read_reg(vp_mod,
				vdd->vp_offs.vpconfig);
		vdd->vp_reg.vpconfig_errorgain = volt_data->vp_errgain;
		vp_errgain_val &= ~vdd->vp_reg.vpconfig_errorgain_mask;
		vp_errgain_val |= vdd->vp_reg.vpconfig_errorgain <<
				vdd->vp_reg.vpconfig_errorgain_shift;
		vdd->write_reg(vp_errgain_val, vp_mod,
				vdd->vp_offs.vpconfig);
	}

	return 0;
}

static void _post_volt_scale(struct omap_vdd_info *vdd,
		unsigned long target_volt, u8 target_vsel, u8 current_vsel)
{
	u32 smps_steps = 0, smps_delay = 0;

	smps_steps = abs(target_vsel - current_vsel);
	/* SMPS slew rate / step size. 2us added as buffer. */
	smps_delay = ((smps_steps * vdd->pmic_info->step_size) /
			vdd->pmic_info->slew_rate) + 2;
	udelay(smps_delay);

	vdd->curr_volt = target_volt;
}

/* vc_bypass_scale_voltage - VC bypass method of voltage scaling */
static int vc_bypass_scale_voltage(struct omap_vdd_info *vdd,
		unsigned long target_volt)
{
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 vc_valid, vc_bypass_val_reg, vc_bypass_value;
	u16 mod;
	u8 target_vsel, current_vsel;
	int ret;

	ret = _pre_volt_scale(vdd, target_volt, &target_vsel, &current_vsel);
	if (ret)
		return ret;

	mod = vdd->vc_reg.prm_mod;

	vc_valid = vdd->vc_reg.valid;
	vc_bypass_val_reg = vdd->vc_reg.bypass_val_reg;
	vc_bypass_value = (target_vsel << vdd->vc_reg.data_shift) |
			(vdd->pmic_info->pmic_reg <<
			vdd->vc_reg.regaddr_shift) |
			(vdd->pmic_info->i2c_slave_addr <<
			vdd->vc_reg.slaveaddr_shift);

	vdd->write_reg(vc_bypass_value, mod, vc_bypass_val_reg);
	vdd->write_reg(vc_bypass_value | vc_valid, mod, vc_bypass_val_reg);

	vc_bypass_value = vdd->read_reg(mod, vc_bypass_val_reg);
	/*
	 * Loop till the bypass command is acknowledged from the SMPS.
	 * NOTE: This is legacy code. The loop count and retry count needs
	 * to be revisited.
	 */
	while (!(vc_bypass_value & vc_valid)) {
		loop_cnt++;

		if (retries_cnt > 10) {
			pr_warning("%s: Retry count exceeded\n", __func__);
			return -ETIMEDOUT;
		}

		if (loop_cnt > 50) {
			retries_cnt++;
			loop_cnt = 0;
			udelay(10);
		}
		vc_bypass_value = vdd->read_reg(mod, vc_bypass_val_reg);
	}

	_post_volt_scale(vdd, target_volt, target_vsel, current_vsel);
	return 0;
}

/* VP force update method of voltage scaling */
static int vp_forceupdate_scale_voltage(struct omap_vdd_info *vdd,
		unsigned long target_volt)
{
	u32 vpconfig;
	u16 mod, ocp_mod;
	u8 target_vsel, current_vsel, prm_irqst_reg;
	int ret, timeout = 0;

	ret = _pre_volt_scale(vdd, target_volt, &target_vsel, &current_vsel);
	if (ret)
		return ret;

	mod = vdd->vp_reg.prm_mod;
	ocp_mod = vdd->ocp_mod;
	prm_irqst_reg = vdd->prm_irqst_reg;

	/*
	 * Clear all pending TransactionDone interrupt/status. Typical latency
	 * is <3us
	 */
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vdd->write_reg(vdd->vp_reg.tranxdone_status,
				ocp_mod, prm_irqst_reg);
		if (!(vdd->read_reg(ocp_mod, prm_irqst_reg) &
				vdd->vp_reg.tranxdone_status))
				break;
		udelay(1);
	}
	if (timeout >= VP_TRANXDONE_TIMEOUT) {
		pr_warning("%s: vdd_%s TRANXDONE timeout exceeded."
			"Voltage change aborted", __func__, vdd->voltdm.name);
		return -ETIMEDOUT;
	}

	/* Configure for VP-Force Update */
	vpconfig = vdd->read_reg(mod, vdd->vp_offs.vpconfig);
	vpconfig &= ~(vdd->vp_reg.vpconfig_initvdd |
			vdd->vp_reg.vpconfig_forceupdate |
			vdd->vp_reg.vpconfig_initvoltage_mask);
	vpconfig |= ((target_vsel <<
			vdd->vp_reg.vpconfig_initvoltage_shift));
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	/* Trigger initVDD value copy to voltage processor */
	vpconfig |= vdd->vp_reg.vpconfig_initvdd;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	/* Force update of voltage */
	vpconfig |= vdd->vp_reg.vpconfig_forceupdate;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	/*
	 * Wait for TransactionDone. Typical latency is <200us.
	 * Depends on SMPSWAITTIMEMIN/MAX and voltage change
	 */
	timeout = 0;
	omap_test_timeout((vdd->read_reg(ocp_mod, prm_irqst_reg) &
			vdd->vp_reg.tranxdone_status),
			VP_TRANXDONE_TIMEOUT, timeout);
	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_err("%s: vdd_%s TRANXDONE timeout exceeded."
			"TRANXDONE never got set after the voltage update\n",
			__func__, vdd->voltdm.name);

	_post_volt_scale(vdd, target_volt, target_vsel, current_vsel);

	/*
	 * Disable TransactionDone interrupt , clear all status, clear
	 * control registers
	 */
	timeout = 0;
	while (timeout++ < VP_TRANXDONE_TIMEOUT) {
		vdd->write_reg(vdd->vp_reg.tranxdone_status,
				ocp_mod, prm_irqst_reg);
		if (!(vdd->read_reg(ocp_mod, prm_irqst_reg) &
				vdd->vp_reg.tranxdone_status))
				break;
		udelay(1);
	}

	if (timeout >= VP_TRANXDONE_TIMEOUT)
		pr_warning("%s: vdd_%s TRANXDONE timeout exceeded while trying"
			"to clear the TRANXDONE status\n",
			__func__, vdd->voltdm.name);

	vpconfig = vdd->read_reg(mod, vdd->vp_offs.vpconfig);
	/* Clear initVDD copy trigger bit */
	vpconfig &= ~vdd->vp_reg.vpconfig_initvdd;;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);
	/* Clear force bit */
	vpconfig &= ~vdd->vp_reg.vpconfig_forceupdate;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	return 0;
}

/* OMAP3 specific voltage init functions */

/*
 * Intializes the voltage controller registers with the PMIC and board
 * specific parameters and voltage setup times for OMAP3.
 */
static void __init omap3_vc_init(struct omap_vdd_info *vdd)
{
	u32 vc_val;
	u16 mod;
	u8 on_vsel, onlp_vsel, ret_vsel, off_vsel;
	static bool is_initialized;

	if (!vdd->pmic_info || !vdd->pmic_info->uv_to_vsel) {
		pr_err("%s: PMIC info requried to configure vc for"
			"vdd_%s not populated.Hence cannot initialize vc\n",
			__func__, vdd->voltdm.name);
		return;
	}

	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, vdd->voltdm.name);
		return;
	}

	mod = vdd->vc_reg.prm_mod;

	/* Set up the SMPS_SA(i2c slave address in VC */
	vc_val = vdd->read_reg(mod, vdd->vc_reg.smps_sa_reg);
	vc_val &= ~vdd->vc_reg.smps_sa_mask;
	vc_val |= vdd->pmic_info->i2c_slave_addr << vdd->vc_reg.smps_sa_shift;
	vdd->write_reg(vc_val, mod, vdd->vc_reg.smps_sa_reg);

	/* Setup the VOLRA(pmic reg addr) in VC */
	vc_val = vdd->read_reg(mod, vdd->vc_reg.smps_volra_reg);
	vc_val &= ~vdd->vc_reg.smps_volra_mask;
	vc_val |= vdd->pmic_info->pmic_reg << vdd->vc_reg.smps_volra_shift;
	vdd->write_reg(vc_val, mod, vdd->vc_reg.smps_volra_reg);

	/*Configure the setup times */
	vc_val = vdd->read_reg(mod, vdd->vc_reg.voltsetup_reg);
	vc_val &= ~vdd->vc_reg.voltsetup_mask;
	vc_val |= vdd->pmic_info->volt_setup_time <<
			vdd->vc_reg.voltsetup_shift;
	vdd->write_reg(vc_val, mod, vdd->vc_reg.voltsetup_reg);

	/* Set up the on, inactive, retention and off voltage */
	on_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->on_volt);
	onlp_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->onlp_volt);
	ret_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->ret_volt);
	off_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->off_volt);
	vc_val	= ((on_vsel << vdd->vc_reg.cmd_on_shift) |
		(onlp_vsel << vdd->vc_reg.cmd_onlp_shift) |
		(ret_vsel << vdd->vc_reg.cmd_ret_shift) |
		(off_vsel << vdd->vc_reg.cmd_off_shift));
	vdd->write_reg(vc_val, mod, vdd->vc_reg.cmdval_reg);

	if (is_initialized)
		return;

	/* Generic VC parameters init */
	vdd->write_reg(OMAP3430_CMD1_MASK | OMAP3430_RAV1_MASK, mod,
			OMAP3_PRM_VC_CH_CONF_OFFSET);
	vdd->write_reg(OMAP3430_MCODE_SHIFT | OMAP3430_HSEN_MASK, mod,
			OMAP3_PRM_VC_I2C_CFG_OFFSET);
	vdd->write_reg(OMAP3_CLKSETUP, mod, OMAP3_PRM_CLKSETUP_OFFSET);
	vdd->write_reg(OMAP3_VOLTOFFSET, mod, OMAP3_PRM_VOLTOFFSET_OFFSET);
	vdd->write_reg(OMAP3_VOLTSETUP2, mod, OMAP3_PRM_VOLTSETUP2_OFFSET);
	is_initialized = true;
}

/* Sets up all the VDD related info for OMAP3 */
static int __init omap3_vdd_data_configure(struct omap_vdd_info *vdd)
{
	struct clk *sys_ck;
	u32 sys_clk_speed, timeout_val, waittime;

	if (!vdd->pmic_info) {
		pr_err("%s: PMIC info requried to configure vdd_%s not"
			"populated.Hence cannot initialize vdd_%s\n",
			__func__, vdd->voltdm.name, vdd->voltdm.name);
		return -EINVAL;
	}

	if (!strcmp(vdd->voltdm.name, "mpu")) {
		if (cpu_is_omap3630())
			vdd->volt_data = omap36xx_vddmpu_volt_data;
		else
			vdd->volt_data = omap34xx_vddmpu_volt_data;

		vdd->vp_reg.tranxdone_status = OMAP3430_VP1_TRANXDONE_ST_MASK;
		vdd->vc_reg.cmdval_reg = OMAP3_PRM_VC_CMD_VAL_0_OFFSET;
		vdd->vc_reg.smps_sa_shift = OMAP3430_PRM_VC_SMPS_SA_SA0_SHIFT;
		vdd->vc_reg.smps_sa_mask = OMAP3430_PRM_VC_SMPS_SA_SA0_MASK;
		vdd->vc_reg.smps_volra_shift = OMAP3430_VOLRA0_SHIFT;
		vdd->vc_reg.smps_volra_mask = OMAP3430_VOLRA0_MASK;
		vdd->vc_reg.voltsetup_shift = OMAP3430_SETUP_TIME1_SHIFT;
		vdd->vc_reg.voltsetup_mask = OMAP3430_SETUP_TIME1_MASK;
	} else if (!strcmp(vdd->voltdm.name, "core")) {
		if (cpu_is_omap3630())
			vdd->volt_data = omap36xx_vddcore_volt_data;
		else
			vdd->volt_data = omap34xx_vddcore_volt_data;

		vdd->vp_reg.tranxdone_status = OMAP3430_VP2_TRANXDONE_ST_MASK;
		vdd->vc_reg.cmdval_reg = OMAP3_PRM_VC_CMD_VAL_1_OFFSET;
		vdd->vc_reg.smps_sa_shift = OMAP3430_PRM_VC_SMPS_SA_SA1_SHIFT;
		vdd->vc_reg.smps_sa_mask = OMAP3430_PRM_VC_SMPS_SA_SA1_MASK;
		vdd->vc_reg.smps_volra_shift = OMAP3430_VOLRA1_SHIFT;
		vdd->vc_reg.smps_volra_mask = OMAP3430_VOLRA1_MASK;
		vdd->vc_reg.voltsetup_shift = OMAP3430_SETUP_TIME2_SHIFT;
		vdd->vc_reg.voltsetup_mask = OMAP3430_SETUP_TIME2_MASK;
	} else {
		pr_warning("%s: vdd_%s does not exisit in OMAP3\n",
			__func__, vdd->voltdm.name);
		return -EINVAL;
	}

	/*
	 * Sys clk rate is require to calculate vp timeout value and
	 * smpswaittimemin and smpswaittimemax.
	 */
	sys_ck = clk_get(NULL, "sys_ck");
	if (IS_ERR(sys_ck)) {
		pr_warning("%s: Could not get the sys clk to calculate"
			"various vdd_%s params\n", __func__, vdd->voltdm.name);
		return -EINVAL;
	}
	sys_clk_speed = clk_get_rate(sys_ck);
	clk_put(sys_ck);
	/* Divide to avoid overflow */
	sys_clk_speed /= 1000;

	/* Generic voltage parameters */
	vdd->curr_volt = 1200000;
	vdd->ocp_mod = OCP_MOD;
	vdd->prm_irqst_reg = OMAP3_PRM_IRQSTATUS_MPU_OFFSET;
	vdd->read_reg = omap3_voltage_read_reg;
	vdd->write_reg = omap3_voltage_write_reg;
	vdd->volt_scale = vp_forceupdate_scale_voltage;
	vdd->vp_enabled = false;

	/* VC parameters */
	vdd->vc_reg.prm_mod = OMAP3430_GR_MOD;
	vdd->vc_reg.smps_sa_reg = OMAP3_PRM_VC_SMPS_SA_OFFSET;
	vdd->vc_reg.smps_volra_reg = OMAP3_PRM_VC_SMPS_VOL_RA_OFFSET;
	vdd->vc_reg.bypass_val_reg = OMAP3_PRM_VC_BYPASS_VAL_OFFSET;
	vdd->vc_reg.voltsetup_reg = OMAP3_PRM_VOLTSETUP1_OFFSET;
	vdd->vc_reg.data_shift = OMAP3430_DATA_SHIFT;
	vdd->vc_reg.slaveaddr_shift = OMAP3430_SLAVEADDR_SHIFT;
	vdd->vc_reg.regaddr_shift = OMAP3430_REGADDR_SHIFT;
	vdd->vc_reg.valid = OMAP3430_VALID_MASK;
	vdd->vc_reg.cmd_on_shift = OMAP3430_VC_CMD_ON_SHIFT;
	vdd->vc_reg.cmd_on_mask = OMAP3430_VC_CMD_ON_MASK;
	vdd->vc_reg.cmd_onlp_shift = OMAP3430_VC_CMD_ONLP_SHIFT;
	vdd->vc_reg.cmd_ret_shift = OMAP3430_VC_CMD_RET_SHIFT;
	vdd->vc_reg.cmd_off_shift = OMAP3430_VC_CMD_OFF_SHIFT;

	vdd->vp_reg.prm_mod = OMAP3430_GR_MOD;

	/* VPCONFIG bit fields */
	vdd->vp_reg.vpconfig_erroroffset = (vdd->pmic_info->vp_erroroffset <<
				 OMAP3430_ERROROFFSET_SHIFT);
	vdd->vp_reg.vpconfig_errorgain_mask = OMAP3430_ERRORGAIN_MASK;
	vdd->vp_reg.vpconfig_errorgain_shift = OMAP3430_ERRORGAIN_SHIFT;
	vdd->vp_reg.vpconfig_initvoltage_shift = OMAP3430_INITVOLTAGE_SHIFT;
	vdd->vp_reg.vpconfig_initvoltage_mask = OMAP3430_INITVOLTAGE_MASK;
	vdd->vp_reg.vpconfig_timeouten = OMAP3430_TIMEOUTEN_MASK;
	vdd->vp_reg.vpconfig_initvdd = OMAP3430_INITVDD_MASK;
	vdd->vp_reg.vpconfig_forceupdate = OMAP3430_FORCEUPDATE_MASK;
	vdd->vp_reg.vpconfig_vpenable = OMAP3430_VPENABLE_MASK;

	/* VSTEPMIN VSTEPMAX bit fields */
	waittime = ((vdd->pmic_info->step_size / vdd->pmic_info->slew_rate) *
				sys_clk_speed) / 1000;
	vdd->vp_reg.vstepmin_smpswaittimemin = waittime;
	vdd->vp_reg.vstepmax_smpswaittimemax = waittime;
	vdd->vp_reg.vstepmin_stepmin = vdd->pmic_info->vp_vstepmin;
	vdd->vp_reg.vstepmax_stepmax = vdd->pmic_info->vp_vstepmax;
	vdd->vp_reg.vstepmin_smpswaittimemin_shift =
				OMAP3430_SMPSWAITTIMEMIN_SHIFT;
	vdd->vp_reg.vstepmax_smpswaittimemax_shift =
				OMAP3430_SMPSWAITTIMEMAX_SHIFT;
	vdd->vp_reg.vstepmin_stepmin_shift = OMAP3430_VSTEPMIN_SHIFT;
	vdd->vp_reg.vstepmax_stepmax_shift = OMAP3430_VSTEPMAX_SHIFT;

	/* VLIMITTO bit fields */
	timeout_val = (sys_clk_speed * vdd->pmic_info->vp_timeout_us) / 1000;
	vdd->vp_reg.vlimitto_timeout = timeout_val;
	vdd->vp_reg.vlimitto_vddmin = vdd->pmic_info->vp_vddmin;
	vdd->vp_reg.vlimitto_vddmax = vdd->pmic_info->vp_vddmax;
	vdd->vp_reg.vlimitto_vddmin_shift = OMAP3430_VDDMIN_SHIFT;
	vdd->vp_reg.vlimitto_vddmax_shift = OMAP3430_VDDMAX_SHIFT;
	vdd->vp_reg.vlimitto_timeout_shift = OMAP3430_TIMEOUT_SHIFT;

	return 0;
}

/* OMAP4 specific voltage init functions */
static void __init omap4_vc_init(struct omap_vdd_info *vdd)
{
	u32 vc_val;
	u16 mod;
	static bool is_initialized;

	if (!vdd->pmic_info || !vdd->pmic_info->uv_to_vsel) {
		pr_err("%s: PMIC info requried to configure vc for"
			"vdd_%s not populated.Hence cannot initialize vc\n",
			__func__, vdd->voltdm.name);
		return;
	}

	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, vdd->voltdm.name);
		return;
	}

	mod = vdd->vc_reg.prm_mod;

	/* Set up the SMPS_SA(i2c slave address in VC */
	vc_val = vdd->read_reg(mod, vdd->vc_reg.smps_sa_reg);
	vc_val &= ~vdd->vc_reg.smps_sa_mask;
	vc_val |= vdd->pmic_info->i2c_slave_addr << vdd->vc_reg.smps_sa_shift;
	vdd->write_reg(vc_val, mod, vdd->vc_reg.smps_sa_reg);

	/* Setup the VOLRA(pmic reg addr) in VC */
	vc_val = vdd->read_reg(mod, vdd->vc_reg.smps_volra_reg);
	vc_val &= ~vdd->vc_reg.smps_volra_mask;
	vc_val |= vdd->pmic_info->pmic_reg << vdd->vc_reg.smps_volra_shift;
	vdd->write_reg(vc_val, mod, vdd->vc_reg.smps_volra_reg);

	/* TODO: Configure setup times and CMD_VAL values*/

	if (is_initialized)
		return;

	/* Generic VC parameters init */
	vc_val = (OMAP4430_RAV_VDD_MPU_L_MASK | OMAP4430_CMD_VDD_MPU_L_MASK |
		OMAP4430_RAV_VDD_IVA_L_MASK | OMAP4430_CMD_VDD_IVA_L_MASK |
		OMAP4430_RAV_VDD_CORE_L_MASK | OMAP4430_CMD_VDD_CORE_L_MASK);
	vdd->write_reg(vc_val, mod, OMAP4_PRM_VC_CFG_CHANNEL_OFFSET);

	vc_val = (0x60 << OMAP4430_SCLL_SHIFT | 0x26 << OMAP4430_SCLH_SHIFT);
	vdd->write_reg(vc_val, mod, OMAP4_PRM_VC_CFG_I2C_CLK_OFFSET);

	is_initialized = true;
}

/* Sets up all the VDD related info for OMAP4 */
static int __init omap4_vdd_data_configure(struct omap_vdd_info *vdd)
{
	struct clk *sys_ck;
	u32 sys_clk_speed, timeout_val, waittime;

	if (!vdd->pmic_info) {
		pr_err("%s: PMIC info requried to configure vdd_%s not"
			"populated.Hence cannot initialize vdd_%s\n",
			__func__, vdd->voltdm.name, vdd->voltdm.name);
		return -EINVAL;
	}

	if (!strcmp(vdd->voltdm.name, "mpu")) {
		vdd->volt_data = omap44xx_vdd_mpu_volt_data;
		vdd->vp_reg.tranxdone_status =
				OMAP4430_VP_MPU_TRANXDONE_ST_MASK;
		vdd->vc_reg.cmdval_reg = OMAP4_PRM_VC_VAL_CMD_VDD_MPU_L_OFFSET;
		vdd->vc_reg.smps_sa_shift =
				OMAP4430_SA_VDD_MPU_L_PRM_VC_SMPS_SA_SHIFT;
		vdd->vc_reg.smps_sa_mask =
				OMAP4430_SA_VDD_MPU_L_PRM_VC_SMPS_SA_MASK;
		vdd->vc_reg.smps_volra_shift = OMAP4430_VOLRA_VDD_MPU_L_SHIFT;
		vdd->vc_reg.smps_volra_mask = OMAP4430_VOLRA_VDD_MPU_L_MASK;
		vdd->vc_reg.voltsetup_reg =
				OMAP4_PRM_VOLTSETUP_MPU_RET_SLEEP_OFFSET;
		vdd->prm_irqst_reg = OMAP4_PRM_IRQSTATUS_MPU_2_OFFSET;
	} else if (!strcmp(vdd->voltdm.name, "core")) {
		vdd->volt_data = omap44xx_vdd_core_volt_data;
		vdd->vp_reg.tranxdone_status =
				OMAP4430_VP_CORE_TRANXDONE_ST_MASK;
		vdd->vc_reg.cmdval_reg =
				OMAP4_PRM_VC_VAL_CMD_VDD_CORE_L_OFFSET;
		vdd->vc_reg.smps_sa_shift = OMAP4430_SA_VDD_CORE_L_0_6_SHIFT;
		vdd->vc_reg.smps_sa_mask = OMAP4430_SA_VDD_CORE_L_0_6_MASK;
		vdd->vc_reg.smps_volra_shift = OMAP4430_VOLRA_VDD_CORE_L_SHIFT;
		vdd->vc_reg.smps_volra_mask = OMAP4430_VOLRA_VDD_CORE_L_MASK;
		vdd->vc_reg.voltsetup_reg =
				OMAP4_PRM_VOLTSETUP_CORE_RET_SLEEP_OFFSET;
		vdd->prm_irqst_reg = OMAP4_PRM_IRQSTATUS_MPU_OFFSET;
	} else if (!strcmp(vdd->voltdm.name, "iva")) {
		vdd->volt_data = omap44xx_vdd_iva_volt_data;
		vdd->vp_reg.tranxdone_status =
				OMAP4430_VP_IVA_TRANXDONE_ST_MASK;
		vdd->vc_reg.cmdval_reg = OMAP4_PRM_VC_VAL_CMD_VDD_IVA_L_OFFSET;
		vdd->vc_reg.smps_sa_shift =
				OMAP4430_SA_VDD_IVA_L_PRM_VC_SMPS_SA_SHIFT;
		vdd->vc_reg.smps_sa_mask =
				OMAP4430_SA_VDD_IVA_L_PRM_VC_SMPS_SA_MASK;
		vdd->vc_reg.smps_volra_shift = OMAP4430_VOLRA_VDD_IVA_L_SHIFT;
		vdd->vc_reg.smps_volra_mask = OMAP4430_VOLRA_VDD_IVA_L_MASK;
		vdd->vc_reg.voltsetup_reg =
				OMAP4_PRM_VOLTSETUP_IVA_RET_SLEEP_OFFSET;
		vdd->prm_irqst_reg = OMAP4_PRM_IRQSTATUS_MPU_OFFSET;
	} else {
		pr_warning("%s: vdd_%s does not exisit in OMAP4\n",
			__func__, vdd->voltdm.name);
		return -EINVAL;
	}

	/*
	 * Sys clk rate is require to calculate vp timeout value and
	 * smpswaittimemin and smpswaittimemax.
	 */
	sys_ck = clk_get(NULL, "sys_clkin_ck");
	if (IS_ERR(sys_ck)) {
		pr_warning("%s: Could not get the sys clk to calculate"
			"various vdd_%s params\n", __func__, vdd->voltdm.name);
		return -EINVAL;
	}
	sys_clk_speed = clk_get_rate(sys_ck);
	clk_put(sys_ck);
	/* Divide to avoid overflow */
	sys_clk_speed /= 1000;

	/* Generic voltage parameters */
	vdd->curr_volt = 1200000;
	vdd->ocp_mod = OMAP4430_PRM_OCP_SOCKET_INST;
	vdd->read_reg = omap4_voltage_read_reg;
	vdd->write_reg = omap4_voltage_write_reg;
	vdd->volt_scale = vp_forceupdate_scale_voltage;
	vdd->vp_enabled = false;

	/* VC parameters */
	vdd->vc_reg.prm_mod = OMAP4430_PRM_DEVICE_INST;
	vdd->vc_reg.smps_sa_reg = OMAP4_PRM_VC_SMPS_SA_OFFSET;
	vdd->vc_reg.smps_volra_reg = OMAP4_PRM_VC_VAL_SMPS_RA_VOL_OFFSET;
	vdd->vc_reg.bypass_val_reg = OMAP4_PRM_VC_VAL_BYPASS_OFFSET;
	vdd->vc_reg.data_shift = OMAP4430_DATA_SHIFT;
	vdd->vc_reg.slaveaddr_shift = OMAP4430_SLAVEADDR_SHIFT;
	vdd->vc_reg.regaddr_shift = OMAP4430_REGADDR_SHIFT;
	vdd->vc_reg.valid = OMAP4430_VALID_MASK;
	vdd->vc_reg.cmd_on_shift = OMAP4430_ON_SHIFT;
	vdd->vc_reg.cmd_on_mask = OMAP4430_ON_MASK;
	vdd->vc_reg.cmd_onlp_shift = OMAP4430_ONLP_SHIFT;
	vdd->vc_reg.cmd_ret_shift = OMAP4430_RET_SHIFT;
	vdd->vc_reg.cmd_off_shift = OMAP4430_OFF_SHIFT;

	vdd->vp_reg.prm_mod = OMAP4430_PRM_DEVICE_INST;

	/* VPCONFIG bit fields */
	vdd->vp_reg.vpconfig_erroroffset = (vdd->pmic_info->vp_erroroffset <<
				 OMAP4430_ERROROFFSET_SHIFT);
	vdd->vp_reg.vpconfig_errorgain_mask = OMAP4430_ERRORGAIN_MASK;
	vdd->vp_reg.vpconfig_errorgain_shift = OMAP4430_ERRORGAIN_SHIFT;
	vdd->vp_reg.vpconfig_initvoltage_shift = OMAP4430_INITVOLTAGE_SHIFT;
	vdd->vp_reg.vpconfig_initvoltage_mask = OMAP4430_INITVOLTAGE_MASK;
	vdd->vp_reg.vpconfig_timeouten = OMAP4430_TIMEOUTEN_MASK;
	vdd->vp_reg.vpconfig_initvdd = OMAP4430_INITVDD_MASK;
	vdd->vp_reg.vpconfig_forceupdate = OMAP4430_FORCEUPDATE_MASK;
	vdd->vp_reg.vpconfig_vpenable = OMAP4430_VPENABLE_MASK;

	/* VSTEPMIN VSTEPMAX bit fields */
	waittime = ((vdd->pmic_info->step_size / vdd->pmic_info->slew_rate) *
				sys_clk_speed) / 1000;
	vdd->vp_reg.vstepmin_smpswaittimemin = waittime;
	vdd->vp_reg.vstepmax_smpswaittimemax = waittime;
	vdd->vp_reg.vstepmin_stepmin = vdd->pmic_info->vp_vstepmin;
	vdd->vp_reg.vstepmax_stepmax = vdd->pmic_info->vp_vstepmax;
	vdd->vp_reg.vstepmin_smpswaittimemin_shift =
			OMAP4430_SMPSWAITTIMEMIN_SHIFT;
	vdd->vp_reg.vstepmax_smpswaittimemax_shift =
			OMAP4430_SMPSWAITTIMEMAX_SHIFT;
	vdd->vp_reg.vstepmin_stepmin_shift = OMAP4430_VSTEPMIN_SHIFT;
	vdd->vp_reg.vstepmax_stepmax_shift = OMAP4430_VSTEPMAX_SHIFT;

	/* VLIMITTO bit fields */
	timeout_val = (sys_clk_speed * vdd->pmic_info->vp_timeout_us) / 1000;
	vdd->vp_reg.vlimitto_timeout = timeout_val;
	vdd->vp_reg.vlimitto_vddmin = vdd->pmic_info->vp_vddmin;
	vdd->vp_reg.vlimitto_vddmax = vdd->pmic_info->vp_vddmax;
	vdd->vp_reg.vlimitto_vddmin_shift = OMAP4430_VDDMIN_SHIFT;
	vdd->vp_reg.vlimitto_vddmax_shift = OMAP4430_VDDMAX_SHIFT;
	vdd->vp_reg.vlimitto_timeout_shift = OMAP4430_TIMEOUT_SHIFT;

	return 0;
}

/* Public functions */
/**
 * omap_voltage_get_nom_volt() - Gets the current non-auto-compensated voltage
 * @voltdm:	pointer to the VDD for which current voltage info is needed
 *
 * API to get the current non-auto-compensated voltage for a VDD.
 * Returns 0 in case of error else returns the current voltage for the VDD.
 */
unsigned long omap_voltage_get_nom_volt(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return 0;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	return vdd->curr_volt;
}

/**
 * omap_vp_get_curr_volt() - API to get the current vp voltage.
 * @voltdm:	pointer to the VDD.
 *
 * This API returns the current voltage for the specified voltage processor
 */
unsigned long omap_vp_get_curr_volt(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;
	u8 curr_vsel;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return 0;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	if (!vdd->read_reg) {
		pr_err("%s: No read API for reading vdd_%s regs\n",
			__func__, voltdm->name);
		return 0;
	}

	curr_vsel = vdd->read_reg(vdd->vp_reg.prm_mod,
			vdd->vp_offs.voltage);

	if (!vdd->pmic_info || !vdd->pmic_info->vsel_to_uv) {
		pr_warning("%s: PMIC function to convert vsel to voltage"
			"in uV not registerd\n", __func__);
		return 0;
	}

	return vdd->pmic_info->vsel_to_uv(curr_vsel);
}

/**
 * omap_vp_enable() - API to enable a particular VP
 * @voltdm:	pointer to the VDD whose VP is to be enabled.
 *
 * This API enables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_vp_enable(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;
	u32 vpconfig;
	u16 mod;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	mod = vdd->vp_reg.prm_mod;

	/* If VP is already enabled, do nothing. Return */
	if (vdd->vp_enabled)
		return;

	vp_latch_vsel(vdd);

	/* Enable VP */
	vpconfig = vdd->read_reg(mod, vdd->vp_offs.vpconfig);
	vpconfig |= vdd->vp_reg.vpconfig_vpenable;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);
	vdd->vp_enabled = true;
}

/**
 * omap_vp_disable() - API to disable a particular VP
 * @voltdm:	pointer to the VDD whose VP is to be disabled.
 *
 * This API disables a particular voltage processor. Needed by the smartreflex
 * class drivers.
 */
void omap_vp_disable(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;
	u32 vpconfig;
	u16 mod;
	int timeout;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);
	if (!vdd->read_reg || !vdd->write_reg) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	mod = vdd->vp_reg.prm_mod;

	/* If VP is already disabled, do nothing. Return */
	if (!vdd->vp_enabled) {
		pr_warning("%s: Trying to disable VP for vdd_%s when"
			"it is already disabled\n", __func__, voltdm->name);
		return;
	}

	/* Disable VP */
	vpconfig = vdd->read_reg(mod, vdd->vp_offs.vpconfig);
	vpconfig &= ~vdd->vp_reg.vpconfig_vpenable;
	vdd->write_reg(vpconfig, mod, vdd->vp_offs.vpconfig);

	/*
	 * Wait for VP idle Typical latency is <2us. Maximum latency is ~100us
	 */
	omap_test_timeout((vdd->read_reg(mod, vdd->vp_offs.vstatus)),
				VP_IDLE_TIMEOUT, timeout);

	if (timeout >= VP_IDLE_TIMEOUT)
		pr_warning("%s: vdd_%s idle timedout\n",
			__func__, voltdm->name);

	vdd->vp_enabled = false;

	return;
}

/**
 * omap_voltage_scale_vdd() - API to scale voltage of a particular
 *				voltage domain.
 * @voltdm:	pointer to the VDD which is to be scaled.
 * @target_volt:	The target voltage of the voltage domain
 *
 * This API should be called by the kernel to do the voltage scaling
 * for a particular voltage domain during dvfs or any other situation.
 */
int omap_voltage_scale_vdd(struct voltagedomain *voltdm,
		unsigned long target_volt)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return -EINVAL;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	if (!vdd->volt_scale) {
		pr_err("%s: No voltage scale API registered for vdd_%s\n",
			__func__, voltdm->name);
		return -ENODATA;
	}

	return vdd->volt_scale(vdd, target_volt);
}

/**
 * omap_voltage_reset() - Resets the voltage of a particular voltage domain
 *			to that of the current OPP.
 * @voltdm:	pointer to the VDD whose voltage is to be reset.
 *
 * This API finds out the correct voltage the voltage domain is supposed
 * to be at and resets the voltage to that level. Should be used expecially
 * while disabling any voltage compensation modules.
 */
void omap_voltage_reset(struct voltagedomain *voltdm)
{
	unsigned long target_uvdc;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	target_uvdc = omap_voltage_get_nom_volt(voltdm);
	if (!target_uvdc) {
		pr_err("%s: unable to find current voltage for vdd_%s\n",
			__func__, voltdm->name);
		return;
	}

	omap_voltage_scale_vdd(voltdm, target_uvdc);
}

/**
 * omap_voltage_get_volttable() - API to get the voltage table associated with a
 *				particular voltage domain.
 * @voltdm:	pointer to the VDD for which the voltage table is required
 * @volt_data:	the voltage table for the particular vdd which is to be
 *		populated by this API
 *
 * This API populates the voltage table associated with a VDD into the
 * passed parameter pointer. Returns the count of distinct voltages
 * supported by this vdd.
 *
 */
void omap_voltage_get_volttable(struct voltagedomain *voltdm,
		struct omap_volt_data **volt_data)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	*volt_data = vdd->volt_data;
}

/**
 * omap_voltage_get_voltdata() - API to get the voltage table entry for a
 *				particular voltage
 * @voltdm:	pointer to the VDD whose voltage table has to be searched
 * @volt:	the voltage to be searched in the voltage table
 *
 * This API searches through the voltage table for the required voltage
 * domain and tries to find a matching entry for the passed voltage volt.
 * If a matching entry is found volt_data is populated with that entry.
 * This API searches only through the non-compensated voltages int the
 * voltage table.
 * Returns pointer to the voltage table entry corresponding to volt on
 * sucess. Returns -ENODATA if no voltage table exisits for the passed voltage
 * domain or if there is no matching entry.
 */
struct omap_volt_data *omap_voltage_get_voltdata(struct voltagedomain *voltdm,
		unsigned long volt)
{
	struct omap_vdd_info *vdd;
	int i;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	if (!vdd->volt_data) {
		pr_warning("%s: voltage table does not exist for vdd_%s\n",
			__func__, voltdm->name);
		return ERR_PTR(-ENODATA);
	}

	for (i = 0; vdd->volt_data[i].volt_nominal != 0; i++) {
		if (vdd->volt_data[i].volt_nominal == volt)
			return &vdd->volt_data[i];
	}

	pr_notice("%s: Unable to match the current voltage with the voltage"
		"table for vdd_%s\n", __func__, voltdm->name);

	return ERR_PTR(-ENODATA);
}

/**
 * omap_voltage_register_pmic() - API to register PMIC specific data
 * @voltdm:	pointer to the VDD for which the PMIC specific data is
 *		to be registered
 * @pmic_info:	the structure containing pmic info
 *
 * This API is to be called by the SOC/PMIC file to specify the
 * pmic specific info as present in omap_volt_pmic_info structure.
 */
int omap_voltage_register_pmic(struct voltagedomain *voltdm,
		struct omap_volt_pmic_info *pmic_info)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return -EINVAL;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	vdd->pmic_info = pmic_info;

	return 0;
}

/**
 * omap_voltage_get_dbgdir() - API to get pointer to the debugfs directory
 *				corresponding to a voltage domain.
 *
 * @voltdm:	pointer to the VDD whose debug directory is required.
 *
 * This API returns pointer to the debugfs directory corresponding
 * to the voltage domain. Should be used by drivers requiring to
 * add any debug entry for a particular voltage domain. Returns NULL
 * in case of error.
 */
struct dentry *omap_voltage_get_dbgdir(struct voltagedomain *voltdm)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return NULL;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	return vdd->debug_dir;
}

/**
 * omap_change_voltscale_method() - API to change the voltage scaling method.
 * @voltdm:	pointer to the VDD whose voltage scaling method
 *		has to be changed.
 * @voltscale_method:	the method to be used for voltage scaling.
 *
 * This API can be used by the board files to change the method of voltage
 * scaling between vpforceupdate and vcbypass. The parameter values are
 * defined in voltage.h
 */
void omap_change_voltscale_method(struct voltagedomain *voltdm,
		int voltscale_method)
{
	struct omap_vdd_info *vdd;

	if (!voltdm || IS_ERR(voltdm)) {
		pr_warning("%s: VDD specified does not exist!\n", __func__);
		return;
	}

	vdd = container_of(voltdm, struct omap_vdd_info, voltdm);

	switch (voltscale_method) {
	case VOLTSCALE_VPFORCEUPDATE:
		vdd->volt_scale = vp_forceupdate_scale_voltage;
		return;
	case VOLTSCALE_VCBYPASS:
		vdd->volt_scale = vc_bypass_scale_voltage;
		return;
	default:
		pr_warning("%s: Trying to change the method of voltage scaling"
			"to an unsupported one!\n", __func__);
	}
}

/**
 * omap_voltage_domain_lookup() - API to get the voltage domain pointer
 * @name:	Name of the voltage domain
 *
 * This API looks up in the global vdd_info struct for the
 * existence of voltage domain <name>. If it exists, the API returns
 * a pointer to the voltage domain structure corresponding to the
 * VDD<name>. Else retuns error pointer.
 */
struct voltagedomain *omap_voltage_domain_lookup(char *name)
{
	int i;

	if (!vdd_info) {
		pr_err("%s: Voltage driver init not yet happened.Faulting!\n",
			__func__);
		return ERR_PTR(-EINVAL);
	}

	if (!name) {
		pr_err("%s: No name to get the votage domain!\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	for (i = 0; i < nr_scalable_vdd; i++) {
		if (!(strcmp(name, vdd_info[i].voltdm.name)))
			return &vdd_info[i].voltdm;
	}

	return ERR_PTR(-EINVAL);
}

/**
 * omap_voltage_late_init() - Init the various voltage parameters
 *
 * This API is to be called in the later stages of the
 * system boot to init the voltage controller and
 * voltage processors.
 */
int __init omap_voltage_late_init(void)
{
	int i;

	if (!vdd_info) {
		pr_err("%s: Voltage driver support not added\n",
			__func__);
		return -EINVAL;
	}

	voltage_dir = debugfs_create_dir("voltage", NULL);
	if (IS_ERR(voltage_dir))
		pr_err("%s: Unable to create voltage debugfs main dir\n",
			__func__);
	for (i = 0; i < nr_scalable_vdd; i++) {
		if (vdd_data_configure(&vdd_info[i]))
			continue;
		vc_init(&vdd_info[i]);
		vp_init(&vdd_info[i]);
		vdd_debugfs_init(&vdd_info[i]);
	}

	return 0;
}

/**
 * omap_voltage_early_init()- Volatage driver early init
 */
static int __init omap_voltage_early_init(void)
{
	if (cpu_is_omap34xx()) {
		vdd_info = omap3_vdd_info;
		nr_scalable_vdd = OMAP3_NR_SCALABLE_VDD;
		vc_init = omap3_vc_init;
		vdd_data_configure = omap3_vdd_data_configure;
	} else if (cpu_is_omap44xx()) {
		vdd_info = omap4_vdd_info;
		nr_scalable_vdd = OMAP4_NR_SCALABLE_VDD;
		vc_init = omap4_vc_init;
		vdd_data_configure = omap4_vdd_data_configure;
	} else {
		pr_warning("%s: voltage driver support not added\n", __func__);
	}

	return 0;
}
core_initcall(omap_voltage_early_init);
