/*
 * OMAP Voltage Controller (VC) interface
 *
 * Copyright (C) 2011 Texas Instruments, Inc.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <plat/cpu.h>

#include "voltage.h"
#include "vc.h"
#include "prm-regbits-34xx.h"
#include "prm-regbits-44xx.h"
#include "prm44xx.h"

/*
 * Channel configuration bits, common for OMAP3 & 4
 * OMAP3 register: PRM_VC_CH_CONF
 * OMAP4 register: PRM_VC_CFG_CHANNEL
 */
#define CFG_CHANNEL_SA    BIT(0)
#define CFG_CHANNEL_RAV   BIT(1)
#define CFG_CHANNEL_RAC   BIT(2)
#define CFG_CHANNEL_RACEN BIT(3)
#define CFG_CHANNEL_CMD   BIT(4)
#define CFG_CHANNEL_MASK 0x3f

/**
 * omap_vc_config_channel - configure VC channel to PMIC mappings
 * @voltdm: pointer to voltagdomain defining the desired VC channel
 *
 * Configures the VC channel to PMIC mappings for the following
 * PMIC settings
 * - i2c slave address (SA)
 * - voltage configuration address (RAV)
 * - command configuration address (RAC) and enable bit (RACEN)
 * - command values for ON, ONLP, RET and OFF (CMD)
 *
 * This function currently only allows flexible configuration of the
 * non-default channel.  Starting with OMAP4, there are more than 2
 * channels, with one defined as the default (on OMAP4, it's MPU.)
 * Only the non-default channel can be configured.
 */
static int omap_vc_config_channel(struct voltagedomain *voltdm)
{
	struct omap_vc_channel *vc = voltdm->vc;

	/*
	 * For default channel, the only configurable bit is RACEN.
	 * All others must stay at zero (see function comment above.)
	 */
	if (vc->flags & OMAP_VC_CHANNEL_DEFAULT)
		vc->cfg_channel &= CFG_CHANNEL_RACEN;

	voltdm->rmw(CFG_CHANNEL_MASK << vc->cfg_channel_sa_shift,
		    vc->cfg_channel << vc->cfg_channel_sa_shift,
		    vc->common->cfg_channel_reg);

	return 0;
}

/* Voltage scale and accessory APIs */
int omap_vc_pre_scale(struct voltagedomain *voltdm,
		      unsigned long target_volt,
		      u8 *target_vsel, u8 *current_vsel)
{
	struct omap_vc_channel *vc = voltdm->vc;
	struct omap_vdd_info *vdd = voltdm->vdd;
	struct omap_volt_data *volt_data;
	const struct omap_vp_common_data *vp_common;
	u32 vc_cmdval, vp_errgain_val;

	vp_common = vdd->vp_data->vp_common;

	/* Check if sufficient pmic info is available for this vdd */
	if (!vdd->pmic_info) {
		pr_err("%s: Insufficient pmic info to scale the vdd_%s\n",
			__func__, voltdm->name);
		return -EINVAL;
	}

	if (!vdd->pmic_info->uv_to_vsel) {
		pr_err("%s: PMIC function to convert voltage in uV to"
			"vsel not registered. Hence unable to scale voltage"
			"for vdd_%s\n", __func__, voltdm->name);
		return -ENODATA;
	}

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return -EINVAL;
	}

	/* Get volt_data corresponding to target_volt */
	volt_data = omap_voltage_get_voltdata(voltdm, target_volt);
	if (IS_ERR(volt_data))
		volt_data = NULL;

	*target_vsel = vdd->pmic_info->uv_to_vsel(target_volt);
	*current_vsel = voltdm->read(vdd->vp_data->voltage);

	/* Setting the ON voltage to the new target voltage */
	vc_cmdval = voltdm->read(vc->cmdval_reg);
	vc_cmdval &= ~vc->common->cmd_on_mask;
	vc_cmdval |= (*target_vsel << vc->common->cmd_on_shift);
	voltdm->write(vc_cmdval, vc->cmdval_reg);

	/* Setting vp errorgain based on the voltage */
	if (volt_data) {
		vp_errgain_val = voltdm->read(vdd->vp_data->vpconfig);
		vdd->vp_rt_data.vpconfig_errorgain = volt_data->vp_errgain;
		vp_errgain_val &= ~vp_common->vpconfig_errorgain_mask;
		vp_errgain_val |= vdd->vp_rt_data.vpconfig_errorgain <<
			vp_common->vpconfig_errorgain_shift;
		voltdm->write(vp_errgain_val, vdd->vp_data->vpconfig);
	}

	return 0;
}

void omap_vc_post_scale(struct voltagedomain *voltdm,
			unsigned long target_volt,
			u8 target_vsel, u8 current_vsel)
{
	struct omap_vdd_info *vdd = voltdm->vdd;
	u32 smps_steps = 0, smps_delay = 0;

	smps_steps = abs(target_vsel - current_vsel);
	/* SMPS slew rate / step size. 2us added as buffer. */
	smps_delay = ((smps_steps * vdd->pmic_info->step_size) /
			vdd->pmic_info->slew_rate) + 2;
	udelay(smps_delay);

	vdd->curr_volt = target_volt;
}

/* vc_bypass_scale - VC bypass method of voltage scaling */
int omap_vc_bypass_scale(struct voltagedomain *voltdm,
			 unsigned long target_volt)
{
	struct omap_vc_channel *vc = voltdm->vc;
	u32 loop_cnt = 0, retries_cnt = 0;
	u32 vc_valid, vc_bypass_val_reg, vc_bypass_value;
	u8 target_vsel, current_vsel;
	int ret;

	ret = omap_vc_pre_scale(voltdm, target_volt, &target_vsel, &current_vsel);
	if (ret)
		return ret;

	vc_valid = vc->common->valid;
	vc_bypass_val_reg = vc->common->bypass_val_reg;
	vc_bypass_value = (target_vsel << vc->common->data_shift) |
		(vc->volt_reg_addr << vc->common->regaddr_shift) |
		(vc->i2c_slave_addr << vc->common->slaveaddr_shift);

	voltdm->write(vc_bypass_value, vc_bypass_val_reg);
	voltdm->write(vc_bypass_value | vc_valid, vc_bypass_val_reg);

	vc_bypass_value = voltdm->read(vc_bypass_val_reg);
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
		vc_bypass_value = voltdm->read(vc_bypass_val_reg);
	}

	omap_vc_post_scale(voltdm, target_volt, target_vsel, current_vsel);
	return 0;
}

static void __init omap3_vfsm_init(struct voltagedomain *voltdm)
{
	/*
	 * Voltage Manager FSM parameters init
	 * XXX This data should be passed in from the board file
	 */
	voltdm->write(OMAP3_CLKSETUP, OMAP3_PRM_CLKSETUP_OFFSET);
	voltdm->write(OMAP3_VOLTOFFSET, OMAP3_PRM_VOLTOFFSET_OFFSET);
	voltdm->write(OMAP3_VOLTSETUP2, OMAP3_PRM_VOLTSETUP2_OFFSET);
}

static void __init omap3_vc_init_channel(struct voltagedomain *voltdm)
{
	static bool is_initialized;

	if (is_initialized)
		return;

	/*
	 * Generic VC parameters init
	 * XXX This data should be abstracted out
	 */
	voltdm->write(OMAP3430_MCODE_SHIFT | OMAP3430_HSEN_MASK,
		       OMAP3_PRM_VC_I2C_CFG_OFFSET);

	omap3_vfsm_init(voltdm);

	is_initialized = true;
}


/* OMAP4 specific voltage init functions */
static void __init omap4_vc_init_channel(struct voltagedomain *voltdm)
{
	static bool is_initialized;
	u32 vc_val;

	if (is_initialized)
		return;

	/* XXX These are magic numbers and do not belong! */
	vc_val = (0x60 << OMAP4430_SCLL_SHIFT | 0x26 << OMAP4430_SCLH_SHIFT);
	voltdm->write(vc_val, OMAP4_PRM_VC_CFG_I2C_CLK_OFFSET);

	is_initialized = true;
}

void __init omap_vc_init_channel(struct voltagedomain *voltdm)
{
	struct omap_vc_channel *vc = voltdm->vc;
	struct omap_vdd_info *vdd = voltdm->vdd;
	u8 on_vsel, onlp_vsel, ret_vsel, off_vsel;
	u32 val;

	if (!vdd->pmic_info || !vdd->pmic_info->uv_to_vsel) {
		pr_err("%s: PMIC info requried to configure vc for"
			"vdd_%s not populated.Hence cannot initialize vc\n",
			__func__, voltdm->name);
		return;
	}

	if (!voltdm->read || !voltdm->write) {
		pr_err("%s: No read/write API for accessing vdd_%s regs\n",
			__func__, voltdm->name);
		return;
	}

	vc->cfg_channel = 0;

	/* get PMIC/board specific settings */
	vc->i2c_slave_addr = vdd->pmic_info->i2c_slave_addr;
	vc->volt_reg_addr = vdd->pmic_info->volt_reg_addr;
	vc->cmd_reg_addr = vdd->pmic_info->cmd_reg_addr;
	vc->setup_time = vdd->pmic_info->volt_setup_time;

	/* Configure the i2c slave address for this VC */
	voltdm->rmw(vc->smps_sa_mask,
		    vc->i2c_slave_addr << __ffs(vc->smps_sa_mask),
		    vc->common->smps_sa_reg);
	vc->cfg_channel |= CFG_CHANNEL_SA;

	/*
	 * Configure the PMIC register addresses.
	 */
	voltdm->rmw(vc->smps_volra_mask,
		    vc->volt_reg_addr << __ffs(vc->smps_volra_mask),
		    vc->common->smps_volra_reg);
	vc->cfg_channel |= CFG_CHANNEL_RAV;

	if (vc->cmd_reg_addr) {
		voltdm->rmw(vc->smps_cmdra_mask,
			    vc->cmd_reg_addr << __ffs(vc->smps_cmdra_mask),
			    vc->common->smps_cmdra_reg);
		vc->cfg_channel |= CFG_CHANNEL_RAC | CFG_CHANNEL_RACEN;
	}

	/* Set up the on, inactive, retention and off voltage */
	on_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->on_volt);
	onlp_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->onlp_volt);
	ret_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->ret_volt);
	off_vsel = vdd->pmic_info->uv_to_vsel(vdd->pmic_info->off_volt);
	val = ((on_vsel << vc->common->cmd_on_shift) |
	       (onlp_vsel << vc->common->cmd_onlp_shift) |
	       (ret_vsel << vc->common->cmd_ret_shift) |
	       (off_vsel << vc->common->cmd_off_shift));
	voltdm->write(val, vc->cmdval_reg);
	vc->cfg_channel |= CFG_CHANNEL_CMD;

	/* Channel configuration */
	omap_vc_config_channel(voltdm);

	/* Configure the setup times */
	voltdm->rmw(voltdm->vfsm->voltsetup_mask,
		    vc->setup_time << __ffs(voltdm->vfsm->voltsetup_mask),
		    voltdm->vfsm->voltsetup_reg);

	if (cpu_is_omap34xx())
		omap3_vc_init_channel(voltdm);
	else if (cpu_is_omap44xx())
		omap4_vc_init_channel(voltdm);
}

