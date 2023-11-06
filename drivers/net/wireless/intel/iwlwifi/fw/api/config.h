/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018-2019, 2023 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_config_h__
#define __iwl_fw_api_config_h__

/*
 * struct iwl_dqa_enable_cmd
 * @cmd_queue: the TXQ number of the command queue
 */
struct iwl_dqa_enable_cmd {
	__le32 cmd_queue;
} __packed; /* DQA_CONTROL_CMD_API_S_VER_1 */

/*
 * struct iwl_tx_ant_cfg_cmd
 * @valid: valid antenna configuration
 */
struct iwl_tx_ant_cfg_cmd {
	__le32 valid;
} __packed;

/**
 * struct iwl_calib_ctrl - Calibration control struct.
 * Sent as part of the phy configuration command.
 * @flow_trigger: bitmap for which calibrations to perform according to
 *		flow triggers, using &enum iwl_calib_cfg
 * @event_trigger: bitmap for which calibrations to perform according to
 *		event triggers, using &enum iwl_calib_cfg
 */
struct iwl_calib_ctrl {
	__le32 flow_trigger;
	__le32 event_trigger;
} __packed;

/* This enum defines the bitmap of various calibrations to enable in both
 * init ucode and runtime ucode through CALIBRATION_CFG_CMD.
 */
enum iwl_calib_cfg {
	IWL_CALIB_CFG_XTAL_IDX			= BIT(0),
	IWL_CALIB_CFG_TEMPERATURE_IDX		= BIT(1),
	IWL_CALIB_CFG_VOLTAGE_READ_IDX		= BIT(2),
	IWL_CALIB_CFG_PAPD_IDX			= BIT(3),
	IWL_CALIB_CFG_TX_PWR_IDX		= BIT(4),
	IWL_CALIB_CFG_DC_IDX			= BIT(5),
	IWL_CALIB_CFG_BB_FILTER_IDX		= BIT(6),
	IWL_CALIB_CFG_LO_LEAKAGE_IDX		= BIT(7),
	IWL_CALIB_CFG_TX_IQ_IDX			= BIT(8),
	IWL_CALIB_CFG_TX_IQ_SKEW_IDX		= BIT(9),
	IWL_CALIB_CFG_RX_IQ_IDX			= BIT(10),
	IWL_CALIB_CFG_RX_IQ_SKEW_IDX		= BIT(11),
	IWL_CALIB_CFG_SENSITIVITY_IDX		= BIT(12),
	IWL_CALIB_CFG_CHAIN_NOISE_IDX		= BIT(13),
	IWL_CALIB_CFG_DISCONNECTED_ANT_IDX	= BIT(14),
	IWL_CALIB_CFG_ANT_COUPLING_IDX		= BIT(15),
	IWL_CALIB_CFG_DAC_IDX			= BIT(16),
	IWL_CALIB_CFG_ABS_IDX			= BIT(17),
	IWL_CALIB_CFG_AGC_IDX			= BIT(18),
};

/**
 * struct iwl_phy_specific_cfg - specific PHY filter configuration
 *
 * Sent as part of the phy configuration command (v3) to configure specific FW
 * defined PHY filters that can be applied to each antenna.
 *
 * @filter_cfg_chains: filter config id for LMAC1 chain A, LMAC1 chain B,
 *	LMAC2 chain A, LMAC2 chain B (in that order)
 *	values: 0: no filter; 0xffffffff: reserved; otherwise: filter id
 */
struct iwl_phy_specific_cfg {
	__le32 filter_cfg_chains[4];
} __packed; /* PHY_SPECIFIC_CONFIGURATION_API_VER_1*/

/**
 * struct iwl_phy_cfg_cmd - Phy configuration command
 *
 * @phy_cfg: PHY configuration value, uses &enum iwl_fw_phy_cfg
 * @calib_control: calibration control data
 */
struct iwl_phy_cfg_cmd_v1 {
	__le32	phy_cfg;
	struct iwl_calib_ctrl calib_control;
} __packed;

/**
 * struct iwl_phy_cfg_cmd_v3 - Phy configuration command (v3)
 *
 * @phy_cfg: PHY configuration value, uses &enum iwl_fw_phy_cfg
 * @calib_control: calibration control data
 * @phy_specific_cfg: configure predefined PHY filters
 */
struct iwl_phy_cfg_cmd_v3 {
	__le32	phy_cfg;
	struct iwl_calib_ctrl calib_control;
	struct iwl_phy_specific_cfg phy_specific_cfg;
} __packed; /* PHY_CONFIGURATION_CMD_API_S_VER_3 */

/*
 * enum iwl_dc2dc_config_id - flag ids
 *
 * Ids of dc2dc configuration flags
 */
enum iwl_dc2dc_config_id {
	DCDC_LOW_POWER_MODE_MSK_SET  = 0x1, /* not used */
	DCDC_FREQ_TUNE_SET = 0x2,
}; /* MARKER_ID_API_E_VER_1 */

#endif /* __iwl_fw_api_config_h__ */
