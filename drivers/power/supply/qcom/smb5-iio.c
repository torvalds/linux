// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/delay.h>
#include <linux/pmic-voter.h>
#include <linux/power_supply.h>
#include <linux/iio/consumer.h>
#include "smb5-lib.h"
#include "smb5-iio.h"
#include "smb5-reg.h"
#include "schgm-flash.h"

#define MIN_THERMAL_VOTE_UA	500000

int smb5_iio_get_prop(struct smb_charger *chg, int channel, int *val)
{
	union power_supply_propval pval = {0, };
	int rc = 0;
	u8 reg = 0, buff[2] = {0};

	pval.intval = 0;
	*val = 0;

	switch (channel) {
	/* USB */
	case PSY_IIO_VOLTAGE_MAX_LIMIT:
		if (chg->usbin_forced_max_uv) {
			*val = chg->usbin_forced_max_uv;
		} else {
			rc = smblib_get_prop_usb_voltage_max_design(chg, &pval);
			*val = pval.intval;
		}
		break;
	case PSY_IIO_PD_CURRENT_MAX:
		*val = get_client_vote(chg->usb_icl_votable, PD_VOTER);
		break;
	case PSY_IIO_USB_REAL_TYPE:
		*val = chg->real_charger_type;
		break;
	case PSY_IIO_TYPEC_MODE:
		rc = smblib_get_usb_prop_typec_mode(chg, val);
		break;
	case PSY_IIO_TYPEC_POWER_ROLE:
		rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case PSY_IIO_TYPEC_CC_ORIENTATION:
		rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case PSY_IIO_TYPEC_SRC_RP:
		rc = smblib_get_prop_typec_select_rp(chg, val);
		break;
	case PSY_IIO_PD_ACTIVE:
		*val = chg->pd_active;
		break;
	case PSY_IIO_USB_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, &pval);
		if (!rc)
			*val = pval.intval;
		break;
	case PSY_IIO_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case PSY_IIO_PD_USB_SUSPEND_SUPPORTED:
		*val = chg->system_suspend_supported;
		break;
	case PSY_IIO_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
	case PSY_IIO_CTM_CURRENT_MAX:
		*val = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case PSY_IIO_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, val);
		break;
	case PSY_IIO_PR_SWAP:
		rc = smblib_get_prop_pr_swap_in_progress(chg, val);
		break;
	case PSY_IIO_PD_VOLTAGE_MAX:
		*val = chg->voltage_max_uv;
		break;
	case PSY_IIO_PD_VOLTAGE_MIN:
		*val = chg->voltage_min_uv;
		break;
	case PSY_IIO_VOLTAGE_QNOVO:
		*val = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
	case PSY_IIO_CURRENT_QNOVO:
		*val = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case PSY_IIO_CONNECTOR_TYPE:
		*val = chg->connector_type;
		break;
	case PSY_IIO_CONNECTOR_HEALTH:
		*val = smblib_get_prop_connector_health(chg);
		break;
	case PSY_IIO_SMB_EN_MODE:
		mutex_lock(&chg->smb_lock);
		*val = chg->sec_chg_selected;
		mutex_unlock(&chg->smb_lock);
		break;
	case PSY_IIO_SMB_EN_REASON:
		*val = chg->cp_reason;
		break;
	case PSY_IIO_MOISTURE_DETECTED:
		*val = chg->moisture_present;
		break;
	case PSY_IIO_MOISTURE_DETECTION_EN:
		*val = !chg->lpd_disabled;
		break;
	case PSY_IIO_HVDCP_OPTI_ALLOWED:
		*val = !chg->flash_active;
		break;
	case PSY_IIO_QC_OPTI_DISABLE:
		if (chg->hw_die_temp_mitigation)
			*val = QC_THERMAL_BALANCE_DISABLE
					| QC_INOV_THERMAL_DISABLE;
		if (chg->hw_connector_mitigation)
			*val |= QC_CTM_DISABLE;
		break;
	case PSY_IIO_VOLTAGE_VPH:
		rc = smblib_get_prop_vph_voltage_now(chg, val);
		break;
	case PSY_IIO_THERM_ICL_LIMIT:
		*val = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER);
		break;
	case PSY_IIO_ADAPTER_CC_MODE:
		*val = chg->adapter_cc_mode;
		break;
	case PSY_IIO_SKIN_HEALTH:
		*val = smblib_get_skin_temp_status(chg);
		break;
	case PSY_IIO_APSD_RERUN:
		*val = 0;
		break;
	case PSY_IIO_APSD_TIMEOUT:
		*val = chg->apsd_ext_timeout;
		break;
	case PSY_IIO_CHARGER_STATUS:
		*val = 0;
		if (chg->sdam_base) {
			rc = smblib_read(chg,
				chg->sdam_base + SDAM_QC_DET_STATUS_REG, &reg);
			if (!rc)
				*val = reg;
		}
		break;
	case PSY_IIO_USB_INPUT_VOLTAGE_SETTLED:
		*val = 0;
		if (chg->sdam_base) {
			rc = smblib_batch_read(chg,
				chg->sdam_base + SDAM_QC_ADC_LSB_REG, buff, 2);
			if (!rc)
				*val = (buff[1] << 8 | buff[0]) * 1038;
		}
		break;
	/* MAIN */
	case PSY_IIO_MAIN_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, &pval);
		if (!rc)
			*val = pval.intval;
		break;
	case PSY_IIO_MAIN_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case PSY_IIO_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case PSY_IIO_FLASH_ACTIVE:
		*val = chg->flash_active;
		break;
	case PSY_IIO_FLASH_TRIGGER:
		*val = 0;
		if (chg->chg_param.smb_version == PMI632)
			rc = schgm_flash_get_vreg_ok(chg, val);
		break;
	case PSY_IIO_TOGGLE_STAT:
		*val = 0;
		break;
	case PSY_IIO_MAIN_FCC_MAX:
		*val = chg->main_fcc_max;
		break;
	case PSY_IIO_IRQ_STATUS:
		rc = smblib_get_irq_status(chg, val);
		break;
	case PSY_IIO_FORCE_MAIN_FCC:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							val);
		break;
	case PSY_IIO_FORCE_MAIN_ICL:
		rc = smblib_get_charge_param(chg, &chg->param.usb_icl,
							val);
		break;
	case PSY_IIO_COMP_CLAMP_LEVEL:
		*val = chg->comp_clamp_level;
		break;
	/* Use this property to report overheat status */
	case PSY_IIO_HOT_TEMP:
		*val = chg->thermal_overheat;
		break;
	case PSY_IIO_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, val);
		break;
	case PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							val);
		break;
	case PSY_IIO_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, val);
		break;
	case PSY_IIO_HEALTH:
		rc = *val = smblib_get_prop_smb_health(chg);
		break;
	/* DC */
	case PSY_IIO_DC_REAL_TYPE:
		*val = POWER_SUPPLY_TYPE_MAINS;
		break;
	case PSY_IIO_INPUT_VOLTAGE_REGULATION:
		rc = smblib_get_prop_voltage_wls_output(chg, &pval);
		if (!rc)
			*val = pval.intval;
		break;
	case PSY_IIO_DC_RESET:
		*val = 0;
		break;
	case PSY_IIO_AICL_DONE:
		*val = chg->dcin_aicl_done;
		break;
	/* BATTERY */
	case PSY_IIO_CHARGER_TEMP:
		rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case PSY_IIO_CHARGER_TEMP_MAX:
		*val = chg->charger_temp_max;
		break;
	case PSY_IIO_SW_JEITA_ENABLED:
		*val = chg->sw_jeita_enabled;
		break;
	case PSY_IIO_PARALLEL_DISABLE:
		*val = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case PSY_IIO_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case PSY_IIO_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		*val = 0;
		break;
	case PSY_IIO_RERUN_AICL:
		*val = 0;
		break;
	case PSY_IIO_DP_DM:
		*val = chg->pulse_cnt;
		break;
	case PSY_IIO_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case PSY_IIO_DIE_HEALTH:
		rc = smblib_get_die_health(chg, val);
		break;
	case PSY_IIO_RECHARGE_SOC:
		*val = chg->auto_recharge_soc;
		break;
	case PSY_IIO_FORCE_RECHARGE:
		*val = 0;
		break;
	case PSY_IIO_FCC_STEPPER_ENABLE:
		*val = chg->fcc_stepper_enable;
		break;
	case PSY_IIO_TYPEC_ACCESSORY_MODE:
		rc = smblib_get_usb_prop_typec_accessory_mode(chg, val);
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_err("Couldn't get prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return IIO_VAL_INT;
}

int smb5_iio_set_prop(struct smb_charger *chg, int channel, int val)
{
	union power_supply_propval pval = {0, };
	int real_chg_type = chg->real_charger_type;
	int icl, rc = 0, offset_ua = 0;

	switch (channel) {
	/* USB */
	case PSY_IIO_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case PSY_IIO_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
	case PSY_IIO_TYPEC_SRC_RP:
		rc = smblib_set_prop_typec_select_rp(chg, val);
		break;
	case PSY_IIO_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case PSY_IIO_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	case PSY_IIO_PD_USB_SUSPEND_SUPPORTED:
		chg->system_suspend_supported = val;
		break;
	case PSY_IIO_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val >= 0, val);
		break;
	case PSY_IIO_PR_SWAP:
		rc = smblib_set_prop_pr_swap_in_progress(chg, val);
		break;
	case PSY_IIO_PD_VOLTAGE_MAX:
		rc = smblib_set_prop_pd_voltage_max(chg, val);
		break;
	case PSY_IIO_PD_VOLTAGE_MIN:
		rc = smblib_set_prop_pd_voltage_min(chg, val);
		break;
	case PSY_IIO_VOLTAGE_QNOVO:
		if (val == -EINVAL) {
			vote(chg->fv_votable, BATT_PROFILE_VOTER, true,
					chg->batt_profile_fv_uv);
			vote(chg->fv_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fv_votable, QNOVO_VOTER, true, val);
			vote(chg->fv_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case PSY_IIO_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val != -EINVAL && val < 2000000, 0);
		if (val == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case PSY_IIO_CONNECTOR_HEALTH:
		chg->connector_health = val;
		if (chg->usb_psy)
			power_supply_changed(chg->usb_psy);
		break;
	case PSY_IIO_THERM_ICL_LIMIT:
		if (!is_client_vote_enabled(chg->usb_icl_votable,
						THERMAL_THROTTLE_VOTER)) {
			chg->init_thermal_ua = get_effective_result(
							chg->usb_icl_votable);
			icl = chg->init_thermal_ua + val;
		} else {
			icl = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER) + val;
		}

		if (icl >= MIN_THERMAL_VOTE_UA)
			rc = vote(chg->usb_icl_votable, THERMAL_THROTTLE_VOTER,
				(icl != chg->init_thermal_ua), icl);
		else
			rc = -EINVAL;
		break;
	case PSY_IIO_VOLTAGE_MAX_LIMIT:
		smblib_set_prop_usb_voltage_max_limit(chg, val);
		break;
	case PSY_IIO_ADAPTER_CC_MODE:
		chg->adapter_cc_mode = val;
		break;
	case PSY_IIO_APSD_RERUN:
		del_timer_sync(&chg->apsd_timer);
		chg->apsd_ext_timeout = false;
		smblib_rerun_apsd(chg);
		break;
	case PSY_IIO_MOISTURE_DETECTION_EN:
		smblib_moisture_detection_enable(chg, val);
		break;
	/* MAIN */
	case PSY_IIO_FLASH_ACTIVE:
		if ((chg->chg_param.smb_version == PMI632)
				&& (chg->flash_active != val)) {
			chg->flash_active = val;

			rc = smblib_get_prop_usb_present(chg, &pval);
			if (rc < 0)
				pr_err("Failed to get USB preset status rc=%d\n",
						rc);
			if (pval.intval) {
				rc = smblib_force_vbus_voltage(chg,
					chg->flash_active ? FORCE_5V_BIT
								: IDLE_BIT);
				if (rc < 0)
					pr_err("Failed to force 5V\n");
				else
					chg->pulse_cnt = 0;
			} else {
				/* USB absent & flash not-active - vote 100mA */
				vote(chg->usb_icl_votable, SW_ICL_MAX_VOTER,
							true, SDP_100_MA);
			}

			pr_debug("flash active VBUS 5V restriction %s\n",
				chg->flash_active ? "applied" : "removed");

			/* Update userspace */
			if (chg->batt_psy)
				power_supply_changed(chg->batt_psy);
		}
		break;
	case PSY_IIO_TOGGLE_STAT:
		rc = smblib_toggle_smb_en(chg, val);
		break;
	case PSY_IIO_MAIN_FCC_MAX:
		chg->main_fcc_max = val;
		rerun_election(chg->fcc_votable);
		break;
	case PSY_IIO_FORCE_MAIN_FCC:
		vote_override(chg->fcc_main_votable, CC_MODE_VOTER,
				(val < 0) ? false : true, val);
		if (val >= 0)
			chg->chg_param.forced_main_fcc = val;
		/*
		 * Remove low vote on FCC_MAIN, for WLS, to allow FCC_MAIN to
		 * rise to its full value.
		 */
		if (val < 0)
			vote(chg->fcc_main_votable, WLS_PL_CHARGING_VOTER,
								false, 0);
		/* Main FCC updated re-calculate FCC */
		rerun_election(chg->fcc_votable);
		break;
	case PSY_IIO_FORCE_MAIN_ICL:
		vote_override(chg->usb_icl_votable, CC_MODE_VOTER,
				(val < 0) ? false : true, val);
		/* Main ICL updated re-calculate ILIM */
		if (real_chg_type == QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
			real_chg_type == QTI_POWER_SUPPLY_TYPE_USB_HVDCP_3P5)
			rerun_election(chg->fcc_votable);
		break;
#ifdef CONFIG_QPNP_SMB5
	case PSY_IIO_COMP_CLAMP_LEVEL:
		rc = smb5_set_prop_comp_clamp_level(chg, val);
		break;
#endif
	case PSY_IIO_HOT_TEMP:
		rc = smblib_set_prop_thermal_overheat(chg, val);
		break;
	case PSY_IIO_VOLTAGE_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fv, val);
		break;
	case PSY_IIO_CONSTANT_CHARGE_CURRENT_MAX:
		/* Adjust Main FCC for QC3.0 + SMB1390 */
		rc = smblib_get_qc3_main_icl_offset(chg, &offset_ua);
		if (rc < 0)
			offset_ua = 0;

		rc = smblib_set_charge_param(chg, &chg->param.fcc,
						val + offset_ua);
		break;
	case PSY_IIO_CURRENT_MAX:
		rc = smblib_set_icl_current(chg, val);
		break;
	/* DC */
	case PSY_IIO_INPUT_VOLTAGE_REGULATION:
		pval.intval = val;
		rc = smblib_set_prop_voltage_wls_output(chg, &pval);
		break;
	case PSY_IIO_DC_RESET:
		rc = smblib_set_prop_dc_reset(chg);
		break;
	/* BATTERY */
	case PSY_IIO_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val, 0);
		break;
	case PSY_IIO_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val)
			break;
		if (chg->iio_chan_list_smb_parallel)
			rc = iio_write_channel_raw(
				chg->iio_chan_list_smb_parallel[SMB_SET_SHIP_MODE],
				val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
	case PSY_IIO_RERUN_AICL:
		rc = smblib_run_aicl(chg, RERUN_AICL);
		break;
	case PSY_IIO_DP_DM:
		if (!chg->flash_active)
			rc = smblib_dp_dm(chg, val);
		break;
	case PSY_IIO_INPUT_CURRENT_LIMITED:
		rc = smblib_set_prop_input_current_limited(chg, val);
		break;
	case PSY_IIO_DIE_HEALTH:
		chg->die_health = val;
		if (chg->batt_psy)
			power_supply_changed(chg->batt_psy);
		break;
	case PSY_IIO_RECHARGE_SOC:
		rc = smblib_set_prop_rechg_soc_thresh(chg, val);
		break;
	case PSY_IIO_FORCE_RECHARGE:
		/* toggle charging to force recharge */
		vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
				true, 0);
		/* charge disable delay */
		msleep(50);
		vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
				false, 0);
		break;
	case PSY_IIO_FCC_STEPPER_ENABLE:
		chg->fcc_stepper_enable = val;
		break;
	default:
		pr_err("get prop %d is not supported\n", channel);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_err("Couldn't set prop %d rc = %d\n", channel, rc);
		return rc;
	}

	return 0;
}

#ifndef CONFIG_QPNP_SMBLITE
struct iio_channel **get_ext_channels(struct device *dev,
		 const char *const *channel_map, int size)
{
	int i, rc = 0;
	struct iio_channel **iio_ch_ext;

	iio_ch_ext = devm_kcalloc(dev, size, sizeof(*iio_ch_ext), GFP_KERNEL);
	if (!iio_ch_ext)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < size; i++) {
		iio_ch_ext[i] = devm_iio_channel_get(dev, channel_map[i]);

		if (IS_ERR(iio_ch_ext[i])) {
			rc = PTR_ERR(iio_ch_ext[i]);
			if (rc != -EPROBE_DEFER)
				dev_err(dev, "%s channel unavailable, %d\n",
						channel_map[i], rc);
			return ERR_PTR(rc);
		}
	}

	return iio_ch_ext;
}
#endif
