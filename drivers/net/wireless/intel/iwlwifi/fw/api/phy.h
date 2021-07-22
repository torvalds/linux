/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2019-2020 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_phy_h__
#define __iwl_fw_api_phy_h__

/**
 * enum iwl_phy_ops_subcmd_ids - PHY group commands
 */
enum iwl_phy_ops_subcmd_ids {
	/**
	 * @CMD_DTS_MEASUREMENT_TRIGGER_WIDE:
	 * Uses either &struct iwl_dts_measurement_cmd or
	 * &struct iwl_ext_dts_measurement_cmd
	 */
	CMD_DTS_MEASUREMENT_TRIGGER_WIDE = 0x0,

	/**
	 * @CTDP_CONFIG_CMD: &struct iwl_mvm_ctdp_cmd
	 */
	CTDP_CONFIG_CMD = 0x03,

	/**
	 * @TEMP_REPORTING_THRESHOLDS_CMD: &struct temp_report_ths_cmd
	 */
	TEMP_REPORTING_THRESHOLDS_CMD = 0x04,

	/**
	 * @GEO_TX_POWER_LIMIT: &struct iwl_geo_tx_power_profiles_cmd
	 */
	GEO_TX_POWER_LIMIT = 0x05,

	/**
	 * @PER_PLATFORM_ANT_GAIN_CMD: &struct iwl_ppag_table_cmd
	 */
	PER_PLATFORM_ANT_GAIN_CMD = 0x07,

	/**
	 * @CT_KILL_NOTIFICATION: &struct ct_kill_notif
	 */
	CT_KILL_NOTIFICATION = 0xFE,

	/**
	 * @DTS_MEASUREMENT_NOTIF_WIDE:
	 * &struct iwl_dts_measurement_notif_v1 or
	 * &struct iwl_dts_measurement_notif_v2
	 */
	DTS_MEASUREMENT_NOTIF_WIDE = 0xFF,
};

/* DTS measurements */

enum iwl_dts_measurement_flags {
	DTS_TRIGGER_CMD_FLAGS_TEMP	= BIT(0),
	DTS_TRIGGER_CMD_FLAGS_VOLT	= BIT(1),
};

/**
 * struct iwl_dts_measurement_cmd - request DTS temp and/or voltage measurements
 *
 * @flags: indicates which measurements we want as specified in
 *	&enum iwl_dts_measurement_flags
 */
struct iwl_dts_measurement_cmd {
	__le32 flags;
} __packed; /* TEMPERATURE_MEASUREMENT_TRIGGER_CMD_S */

/**
* enum iwl_dts_control_measurement_mode - DTS measurement type
* @DTS_AUTOMATIC: Automatic mode (full SW control). Provide temperature read
*                 back (latest value. Not waiting for new value). Use automatic
*                 SW DTS configuration.
* @DTS_REQUEST_READ: Request DTS read. Configure DTS with manual settings,
*                    trigger DTS reading and provide read back temperature read
*                    when available.
* @DTS_OVER_WRITE: over-write the DTS temperatures in the SW until next read
* @DTS_DIRECT_WITHOUT_MEASURE: DTS returns its latest temperature result,
*                              without measurement trigger.
*/
enum iwl_dts_control_measurement_mode {
	DTS_AUTOMATIC			= 0,
	DTS_REQUEST_READ		= 1,
	DTS_OVER_WRITE			= 2,
	DTS_DIRECT_WITHOUT_MEASURE	= 3,
};

/**
* enum iwl_dts_used - DTS to use or used for measurement in the DTS request
* @DTS_USE_TOP: Top
* @DTS_USE_CHAIN_A: chain A
* @DTS_USE_CHAIN_B: chain B
* @DTS_USE_CHAIN_C: chain C
* @XTAL_TEMPERATURE: read temperature from xtal
*/
enum iwl_dts_used {
	DTS_USE_TOP		= 0,
	DTS_USE_CHAIN_A		= 1,
	DTS_USE_CHAIN_B		= 2,
	DTS_USE_CHAIN_C		= 3,
	XTAL_TEMPERATURE	= 4,
};

/**
* enum iwl_dts_bit_mode - bit-mode to use in DTS request read mode
* @DTS_BIT6_MODE: bit 6 mode
* @DTS_BIT8_MODE: bit 8 mode
*/
enum iwl_dts_bit_mode {
	DTS_BIT6_MODE	= 0,
	DTS_BIT8_MODE	= 1,
};

/**
 * struct iwl_ext_dts_measurement_cmd - request extended DTS temp measurements
 * @control_mode: see &enum iwl_dts_control_measurement_mode
 * @temperature: used when over write DTS mode is selected
 * @sensor: set temperature sensor to use. See &enum iwl_dts_used
 * @avg_factor: average factor to DTS in request DTS read mode
 * @bit_mode: value defines the DTS bit mode to use. See &enum iwl_dts_bit_mode
 * @step_duration: step duration for the DTS
 */
struct iwl_ext_dts_measurement_cmd {
	__le32 control_mode;
	__le32 temperature;
	__le32 sensor;
	__le32 avg_factor;
	__le32 bit_mode;
	__le32 step_duration;
} __packed; /* XVT_FW_DTS_CONTROL_MEASUREMENT_REQUEST_API_S */

/**
 * struct iwl_dts_measurement_notif_v1 - measurements notification
 *
 * @temp: the measured temperature
 * @voltage: the measured voltage
 */
struct iwl_dts_measurement_notif_v1 {
	__le32 temp;
	__le32 voltage;
} __packed; /* TEMPERATURE_MEASUREMENT_TRIGGER_NTFY_S_VER_1*/

/**
 * struct iwl_dts_measurement_notif_v2 - measurements notification
 *
 * @temp: the measured temperature
 * @voltage: the measured voltage
 * @threshold_idx: the trip index that was crossed
 */
struct iwl_dts_measurement_notif_v2 {
	__le32 temp;
	__le32 voltage;
	__le32 threshold_idx;
} __packed; /* TEMPERATURE_MEASUREMENT_TRIGGER_NTFY_S_VER_2 */

/**
 * struct iwl_dts_measurement_resp - measurements response
 *
 * @temp: the measured temperature
 */
struct iwl_dts_measurement_resp {
	__le32 temp;
} __packed; /* CMD_DTS_MEASUREMENT_RSP_API_S_VER_1 */

/**
 * struct ct_kill_notif - CT-kill entry notification
 *
 * @temperature: the current temperature in celsius
 * @reserved: reserved
 */
struct ct_kill_notif {
	__le16 temperature;
	__le16 reserved;
} __packed; /* GRP_PHY_CT_KILL_NTF */

/**
* enum ctdp_cmd_operation - CTDP command operations
* @CTDP_CMD_OPERATION_START: update the current budget
* @CTDP_CMD_OPERATION_STOP: stop ctdp
* @CTDP_CMD_OPERATION_REPORT: get the average budget
*/
enum iwl_mvm_ctdp_cmd_operation {
	CTDP_CMD_OPERATION_START	= 0x1,
	CTDP_CMD_OPERATION_STOP		= 0x2,
	CTDP_CMD_OPERATION_REPORT	= 0x4,
};/* CTDP_CMD_OPERATION_TYPE_E */

/**
 * struct iwl_mvm_ctdp_cmd - track and manage the FW power consumption budget
 *
 * @operation: see &enum iwl_mvm_ctdp_cmd_operation
 * @budget: the budget in milliwatt
 * @window_size: defined in API but not used
 */
struct iwl_mvm_ctdp_cmd {
	__le32 operation;
	__le32 budget;
	__le32 window_size;
} __packed;

#define IWL_MAX_DTS_TRIPS	8

/**
 * struct temp_report_ths_cmd - set temperature thresholds
 *
 * @num_temps: number of temperature thresholds passed
 * @thresholds: array with the thresholds to be configured
 */
struct temp_report_ths_cmd {
	__le32 num_temps;
	__le16 thresholds[IWL_MAX_DTS_TRIPS];
} __packed; /* GRP_PHY_TEMP_REPORTING_THRESHOLDS_CMD */

#endif /* __iwl_fw_api_phy_h__ */
