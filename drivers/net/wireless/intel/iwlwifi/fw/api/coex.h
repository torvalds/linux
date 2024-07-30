/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2023-2024 Intel Corporation
 * Copyright (C) 2013-2014, 2018-2019 Intel Corporation
 * Copyright (C) 2013-2014 Intel Mobile Communications GmbH
 * Copyright (C) 2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_coex_h__
#define __iwl_fw_api_coex_h__

#include <linux/types.h>
#include <linux/bitops.h>

#define BITS(nb) (BIT(nb) - 1)

enum iwl_bt_coex_lut_type {
	BT_COEX_TIGHT_LUT = 0,
	BT_COEX_LOOSE_LUT,
	BT_COEX_TX_DIS_LUT,

	BT_COEX_MAX_LUT,
	BT_COEX_INVALID_LUT = 0xff,
}; /* BT_COEX_DECISION_LUT_INDEX_API_E_VER_1 */

#define BT_REDUCED_TX_POWER_BIT BIT(7)

enum iwl_bt_coex_mode {
	BT_COEX_DISABLE			= 0x0,
	BT_COEX_NW			= 0x1,
	BT_COEX_BT			= 0x2,
	BT_COEX_WIFI			= 0x3,
}; /* BT_COEX_MODES_E */

enum iwl_bt_coex_enabled_modules {
	BT_COEX_MPLUT_ENABLED		= BIT(0),
	BT_COEX_MPLUT_BOOST_ENABLED	= BIT(1),
	BT_COEX_SYNC2SCO_ENABLED	= BIT(2),
	BT_COEX_CORUN_ENABLED		= BIT(3),
	BT_COEX_HIGH_BAND_RET		= BIT(4),
}; /* BT_COEX_MODULES_ENABLE_E_VER_1 */

/**
 * struct iwl_bt_coex_cmd - bt coex configuration command
 * @mode: &enum iwl_bt_coex_mode
 * @enabled_modules: &enum iwl_bt_coex_enabled_modules
 *
 * The structure is used for the BT_COEX command.
 */
struct iwl_bt_coex_cmd {
	__le32 mode;
	__le32 enabled_modules;
} __packed; /* BT_COEX_CMD_API_S_VER_6 */

/**
 * struct iwl_bt_coex_reduced_txp_update_cmd
 * @reduced_txp: bit BT_REDUCED_TX_POWER_BIT to enable / disable, rest of the
 *	bits are the sta_id (value)
 */
struct iwl_bt_coex_reduced_txp_update_cmd {
	__le32 reduced_txp;
} __packed; /* BT_COEX_UPDATE_REDUCED_TX_POWER_API_S_VER_1 */

/**
 * struct iwl_bt_coex_ci_cmd - bt coex channel inhibition command
 * @bt_primary_ci: primary channel inhibition bitmap
 * @primary_ch_phy_id: primary channel PHY ID
 * @bt_secondary_ci: secondary channel inhibition bitmap
 * @secondary_ch_phy_id: secondary channel PHY ID
 *
 * Used for BT_COEX_CI command
 */
struct iwl_bt_coex_ci_cmd {
	__le64 bt_primary_ci;
	__le32 primary_ch_phy_id;

	__le64 bt_secondary_ci;
	__le32 secondary_ch_phy_id;
} __packed; /* BT_CI_MSG_API_S_VER_2 */

enum iwl_bt_activity_grading {
	BT_OFF			= 0,
	BT_ON_NO_CONNECTION	= 1,
	BT_LOW_TRAFFIC		= 2,
	BT_HIGH_TRAFFIC		= 3,
	BT_VERY_HIGH_TRAFFIC	= 4,

	BT_MAX_AG,
}; /* BT_COEX_BT_ACTIVITY_GRADING_API_E_VER_1 */

enum iwl_bt_ci_compliance {
	BT_CI_COMPLIANCE_NONE		= 0,
	BT_CI_COMPLIANCE_PRIMARY	= 1,
	BT_CI_COMPLIANCE_SECONDARY	= 2,
	BT_CI_COMPLIANCE_BOTH		= 3,
}; /* BT_COEX_CI_COMPLIENCE_E_VER_1 */

/**
 * struct iwl_bt_coex_profile_notif - notification about BT coex
 * @mbox_msg: message from BT to WiFi
 * @msg_idx: the index of the message
 * @bt_ci_compliance: enum %iwl_bt_ci_compliance
 * @primary_ch_lut: LUT used for primary channel &enum iwl_bt_coex_lut_type
 * @secondary_ch_lut: LUT used for secondary channel &enum iwl_bt_coex_lut_type
 * @bt_activity_grading: the activity of BT &enum iwl_bt_activity_grading
 * @ttc_status: is TTC enabled - one bit per PHY
 * @rrc_status: is RRC enabled - one bit per PHY
 * The following fields are only for version 5, and are reserved in version 4:
 * @wifi_loss_low_rssi: The predicted lost WiFi rate (% of air time that BT is
 *	utilizing) when the RSSI is low (<= -65 dBm)
 * @wifi_loss_mid_high_rssi: The predicted lost WiFi rate (% of air time that
 *	BT is utilizing) when the RSSI is mid/high (>= -65 dBm)
 */
struct iwl_bt_coex_profile_notif {
	__le32 mbox_msg[4];
	__le32 msg_idx;
	__le32 bt_ci_compliance;

	__le32 primary_ch_lut;
	__le32 secondary_ch_lut;
	__le32 bt_activity_grading;
	u8 ttc_status;
	u8 rrc_status;
	u8 wifi_loss_low_rssi;
	u8 wifi_loss_mid_high_rssi;
} __packed; /* BT_COEX_PROFILE_NTFY_API_S_VER_4
	     * BT_COEX_PROFILE_NTFY_API_S_VER_5
	     */

#endif /* __iwl_fw_api_coex_h__ */
