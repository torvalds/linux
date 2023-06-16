/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __SMB5_IIO_H
#define __SMB5_IIO_H

#include <linux/iio/iio.h>
#include <dt-bindings/iio/qti_power_supply_iio.h>

enum iio_type {
	MAIN,
	QG,
	CP,
	SMB_PARALLEL,
};

/* For qpnp-smb5.c and smb5-lib.c */
enum qg_chg_iio_channels {
	SMB5_QG_DEBUG_BATTERY,
	SMB5_QG_CAPACITY,
	SMB5_QG_REAL_CAPACITY,
	SMB5_QG_CC_SOC,
	SMB5_QG_CURRENT_NOW,
	SMB5_QG_VOLTAGE_NOW,
	SMB5_QG_VOLTAGE_MAX,
	SMB5_QG_CHARGE_FULL,
	SMB5_QG_RESISTANCE_ID,
	SMB5_QG_TEMP,
	SMB5_QG_CHARGE_COUNTER,
	SMB5_QG_CYCLE_COUNT,
	SMB5_QG_CHARGE_FULL_DESIGN,
	SMB5_QG_TIME_TO_FULL_NOW,
	SMB5_QG_TIME_TO_EMPTY_NOW,
	SMB5_QG_NOMINAL_CAPACITY,
	SMB5_QG_LEARNED_CAPACITY,
	SMB5_QG_SOH,
	SMB5_QG_MAX,
};

enum cp_iio_channels {
	CP_PARALLEL_OUTPUT_MODE,
	CP_MASTER_ENABLE,
	CP_ILIM,
	CP_DIE_TEMP,
};

/* For smb5-lib.c and smb5-iio.c */
enum smb_parallel_iio_channels {
	SMB_CHARGER_TEMP,
	SMB_CHARGER_TEMP_MAX,
	SMB_SET_SHIP_MODE,
};

/* For step-chg-jeita.c */
enum step_chg_iio_channels {
	STEP_QG_RESISTANCE_ID = 0,
	STEP_QG_VOLTAGE_NOW,
	STEP_QG_TEMP,
	STEP_QG_CAPACITY,
	STEP_QG_VOLTAGE_OCV,
	STEP_QG_VOLTAGE_AVG,
};

/* For battery.c */
enum bat_cp_iio_channels {
	BAT_CP_PARALLEL_MODE,
	BAT_CP_PARALLEL_OUTPUT_MODE,
	BAT_CP_MIN_ICL,
	BAT_CP_SWITCHER_EN,
};

enum bat_smb_parallel_iio_channels {
	BAT_SMB_PARALLEL_INPUT_SUSPEND,
	BAT_SMB_PARALLEL_MODE,
	BAT_SMB_PARALLEL_BATFET_MODE,
	BAT_SMB_PARALLEL_MIN_ICL,
	BAT_SMB_PARALLEL_FCC_MAX,
	BAT_SMB_PARALLEL_CURRENT_MAX,
	BAT_SMB_PARALLEL_CONSTANT_CHARGE_CURRENT_MAX,
	BAT_SMB_PARALLEL_VOLTAGE_MAX,
	BAT_SMB_PARALLEL_CHARGE_TYPE,
};

struct smb5_iio_prop_channels {
	const char *datasheet_name;
	int channel_num;
	enum iio_chan_type type;
	long info_mask;
};

#define PARAM(chan) PSY_IIO_##chan

#define SMB5_CHAN(_dname, _chan, _type, _mask)		\
	{								\
		.datasheet_name = _dname,				\
		.channel_num = _chan,				\
		.type = _type,						\
		.info_mask = _mask,					\
	},								\

#define SMB5_CHAN_VOLT(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_VOLTAGE, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_CUR(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_CURRENT, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_RES(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_RESISTANCE, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_TEMP(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_TEMP, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_POWER(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_POWER, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_CAP(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_CAPACITANCE, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_COUNT(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_COUNT, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_INDEX(_dname, chan)					\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_INDEX, BIT(IIO_CHAN_INFO_PROCESSED))	\

#define SMB5_CHAN_ACTIVITY(_dname, chan)				\
	[PARAM(chan)] = SMB5_CHAN(_dname, PARAM(chan),		\
			IIO_ACTIVITY, BIT(IIO_CHAN_INFO_PROCESSED))	\

static const struct smb5_iio_prop_channels smb5_chans_pmic[] = {
	SMB5_CHAN_CUR("usb_pd_current_max", PD_CURRENT_MAX)
	SMB5_CHAN_INDEX("usb_typec_mode", TYPEC_MODE)
	SMB5_CHAN_INDEX("usb_typec_power_role", TYPEC_POWER_ROLE)
	SMB5_CHAN_INDEX("usb_typec_cc_orientation", TYPEC_CC_ORIENTATION)
	SMB5_CHAN_INDEX("usb_pd_active", PD_ACTIVE)
	SMB5_CHAN_CUR("usb_input_current_settled", USB_INPUT_CURRENT_SETTLED)
	SMB5_CHAN_ACTIVITY("usb_pe_start", PE_START)
	SMB5_CHAN_CUR("usb_ctm_current_max", CTM_CURRENT_MAX)
	SMB5_CHAN_CUR("usb_hw_current_max", HW_CURRENT_MAX)
	SMB5_CHAN_INDEX("usb_real_type", USB_REAL_TYPE)
	SMB5_CHAN_VOLT("usb_pd_voltage_max", PD_VOLTAGE_MAX)
	SMB5_CHAN_VOLT("usb_pd_voltage_min", PD_VOLTAGE_MIN)
	SMB5_CHAN_VOLT("voltage_qnovo", VOLTAGE_QNOVO)
	SMB5_CHAN_CUR("current_qnovo", CURRENT_QNOVO)
	SMB5_CHAN_INDEX("usb_connector_type", CONNECTOR_TYPE)
	SMB5_CHAN_INDEX("usb_connector_health", CONNECTOR_HEALTH)
	SMB5_CHAN_VOLT("usb_voltage_max_limit", VOLTAGE_MAX_LIMIT)
	SMB5_CHAN_INDEX("usb_smb_en_mode", SMB_EN_MODE)
	SMB5_CHAN_INDEX("usb_smb_en_reason", SMB_EN_REASON)
	SMB5_CHAN_INDEX("usb_adapter_cc_mode", ADAPTER_CC_MODE)
	SMB5_CHAN_INDEX("usb_moisture_detected", MOISTURE_DETECTED)
	SMB5_CHAN_INDEX("usb_moisture_detection_en", MOISTURE_DETECTION_EN)
	SMB5_CHAN_INDEX("usb_hvdcp_opti_allowed", HVDCP_OPTI_ALLOWED)
	SMB5_CHAN_ACTIVITY("usb_qc_opti_disable", QC_OPTI_DISABLE)
	SMB5_CHAN_VOLT("usb_voltage_vph", VOLTAGE_VPH)
	SMB5_CHAN_CUR("usb_therm_icl_limit", THERM_ICL_LIMIT)
	SMB5_CHAN_INDEX("usb_skin_health", SKIN_HEALTH)
	SMB5_CHAN_ACTIVITY("usb_apsd_rerun", APSD_RERUN)
	SMB5_CHAN_COUNT("usb_apsd_timeout", APSD_TIMEOUT)
	SMB5_CHAN_INDEX("usb_charger_status", CHARGER_STATUS)
	SMB5_CHAN_VOLT("usb_input_voltage_settled", USB_INPUT_VOLTAGE_SETTLED)
	SMB5_CHAN_ACTIVITY("usb_typec_src_rp", TYPEC_SRC_RP)
	SMB5_CHAN_ACTIVITY("usb_pd_in_hard_reset", PD_IN_HARD_RESET)
	SMB5_CHAN_INDEX("usb_pd_usb_suspend_supported",
			PD_USB_SUSPEND_SUPPORTED)
	SMB5_CHAN_ACTIVITY("usb_pr_swap", PR_SWAP)
	SMB5_CHAN_CUR("main_input_current_settled", MAIN_INPUT_CURRENT_SETTLED)
	SMB5_CHAN_VOLT("main_input_voltage_settled", MAIN_INPUT_VOLTAGE_SETTLED)
	SMB5_CHAN_CUR("main_fcc_delta", FCC_DELTA)
	SMB5_CHAN_ACTIVITY("main_flash_active", FLASH_ACTIVE)
	SMB5_CHAN_ACTIVITY("main_flash_trigger", FLASH_TRIGGER)
	SMB5_CHAN_ACTIVITY("main_toggle_stat", TOGGLE_STAT)
	SMB5_CHAN_CUR("main_fcc_max", MAIN_FCC_MAX)
	SMB5_CHAN_INDEX("main_irq_status", IRQ_STATUS)
	SMB5_CHAN_ACTIVITY("main_force_main_fcc", FORCE_MAIN_FCC)
	SMB5_CHAN_ACTIVITY("main_force_main_icl", FORCE_MAIN_ICL)
	SMB5_CHAN_INDEX("main_comp_clamp_level", COMP_CLAMP_LEVEL)
	SMB5_CHAN_TEMP("main_temp_hot", HOT_TEMP)
	SMB5_CHAN_VOLT("main_voltage_max", VOLTAGE_MAX)
	SMB5_CHAN_CUR("main_constant_charge_current_max",
			CONSTANT_CHARGE_CURRENT_MAX)
	SMB5_CHAN_CUR("main_current_max", CURRENT_MAX)
	SMB5_CHAN_INDEX("main_health", HEALTH)
	SMB5_CHAN_VOLT("dc_input_voltage_regulation", INPUT_VOLTAGE_REGULATION)
	SMB5_CHAN_INDEX("dc_real_type", DC_REAL_TYPE)
	SMB5_CHAN_ACTIVITY("dc_reset", DC_RESET)
	SMB5_CHAN_ACTIVITY("dc_aicl_done", AICL_DONE)
	SMB5_CHAN_TEMP("battery_charger_temp", CHARGER_TEMP)
	SMB5_CHAN_TEMP("battery_charger_temp_max", CHARGER_TEMP_MAX)
	SMB5_CHAN_CUR("battery_input_current_limited", INPUT_CURRENT_LIMITED)
	SMB5_CHAN_ACTIVITY("battery_sw_jeita_enabled", SW_JEITA_ENABLED)
	SMB5_CHAN_ACTIVITY("battery_charge_done", CHARGE_DONE)
	SMB5_CHAN_ACTIVITY("battery_parallel_disable", PARALLEL_DISABLE)
	SMB5_CHAN_ACTIVITY("battery_set_ship_mode", SET_SHIP_MODE)
	SMB5_CHAN_INDEX("battery_die_health", DIE_HEALTH)
	SMB5_CHAN_ACTIVITY("battery_rerun_aicl", RERUN_AICL)
	SMB5_CHAN_COUNT("battery_dp_dm", DP_DM)
	SMB5_CHAN_ACTIVITY("battery_recharge_soc", RECHARGE_SOC)
	SMB5_CHAN_ACTIVITY("battery_force_recharge", FORCE_RECHARGE)
	SMB5_CHAN_ACTIVITY("battery_fcc_stepper_enable", FCC_STEPPER_ENABLE)
	SMB5_CHAN_INDEX("usb_typec_accessory_mode", TYPEC_ACCESSORY_MODE)
	SMB5_CHAN_ACTIVITY("battery_sys_soc", SYS_SOC)
};

struct iio_channel **get_ext_channels(struct device *dev,
	const char *const *channel_map, int size);
#endif
