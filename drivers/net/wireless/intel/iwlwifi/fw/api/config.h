/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

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
 * struct iwl_phy_cfg_cmd - Phy configuration command
 * @phy_cfg: PHY configuration value, uses &enum iwl_fw_phy_cfg
 * @calib_control: calibration control data
 */
struct iwl_phy_cfg_cmd {
	__le32	phy_cfg;
	struct iwl_calib_ctrl calib_control;
} __packed;

#define PHY_CFG_RADIO_TYPE	(BIT(0) | BIT(1))
#define PHY_CFG_RADIO_STEP	(BIT(2) | BIT(3))
#define PHY_CFG_RADIO_DASH	(BIT(4) | BIT(5))
#define PHY_CFG_PRODUCT_NUMBER	(BIT(6) | BIT(7))
#define PHY_CFG_TX_CHAIN_A	BIT(8)
#define PHY_CFG_TX_CHAIN_B	BIT(9)
#define PHY_CFG_TX_CHAIN_C	BIT(10)
#define PHY_CFG_RX_CHAIN_A	BIT(12)
#define PHY_CFG_RX_CHAIN_B	BIT(13)
#define PHY_CFG_RX_CHAIN_C	BIT(14)

/*
 * enum iwl_dc2dc_config_id - flag ids
 *
 * Ids of dc2dc configuration flags
 */
enum iwl_dc2dc_config_id {
	DCDC_LOW_POWER_MODE_MSK_SET  = 0x1, /* not used */
	DCDC_FREQ_TUNE_SET = 0x2,
}; /* MARKER_ID_API_E_VER_1 */

/**
 * struct iwl_dc2dc_config_cmd - configure dc2dc values
 *
 * (DC2DC_CONFIG_CMD = 0x83)
 *
 * Set/Get & configure dc2dc values.
 * The command always returns the current dc2dc values.
 *
 * @flags: set/get dc2dc
 * @enable_low_power_mode: not used.
 * @dc2dc_freq_tune0: frequency divider - digital domain
 * @dc2dc_freq_tune1: frequency divider - analog domain
 */
struct iwl_dc2dc_config_cmd {
	__le32 flags;
	__le32 enable_low_power_mode; /* not used */
	__le32 dc2dc_freq_tune0;
	__le32 dc2dc_freq_tune1;
} __packed; /* DC2DC_CONFIG_CMD_API_S_VER_1 */

/**
 * struct iwl_dc2dc_config_resp - response for iwl_dc2dc_config_cmd
 *
 * Current dc2dc values returned by the FW.
 *
 * @dc2dc_freq_tune0: frequency divider - digital domain
 * @dc2dc_freq_tune1: frequency divider - analog domain
 */
struct iwl_dc2dc_config_resp {
	__le32 dc2dc_freq_tune0;
	__le32 dc2dc_freq_tune1;
} __packed; /* DC2DC_CONFIG_RESP_API_S_VER_1 */

#endif /* __iwl_fw_api_config_h__ */
