/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2013 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 *****************************************************************************/

#ifndef __fw_api_bt_coex_h__
#define __fw_api_bt_coex_h__

#include <linux/types.h>
#include <linux/bitops.h>

#define BITS(nb) (BIT(nb) - 1)

/**
 * enum iwl_bt_coex_flags - flags for BT_COEX command
 * @BT_COEX_MODE_POS:
 * @BT_COEX_MODE_MSK:
 * @BT_COEX_DISABLE_OLD:
 * @BT_COEX_2W_OLD:
 * @BT_COEX_3W_OLD:
 * @BT_COEX_NW_OLD:
 * @BT_COEX_AUTO_OLD:
 * @BT_COEX_BT_OLD: Antenna is for BT (manufacuring tests)
 * @BT_COEX_WIFI_OLD: Antenna is for BT (manufacuring tests)
 * @BT_COEX_SYNC2SCO:
 * @BT_COEX_CORUNNING:
 * @BT_COEX_MPLUT:
 * @BT_COEX_TTC:
 * @BT_COEX_RRC:
 *
 * The COEX_MODE must be set for each command. Even if it is not changed.
 */
enum iwl_bt_coex_flags {
	BT_COEX_MODE_POS		= 3,
	BT_COEX_MODE_MSK		= BITS(3) << BT_COEX_MODE_POS,
	BT_COEX_DISABLE_OLD		= 0x0 << BT_COEX_MODE_POS,
	BT_COEX_2W_OLD			= 0x1 << BT_COEX_MODE_POS,
	BT_COEX_3W_OLD			= 0x2 << BT_COEX_MODE_POS,
	BT_COEX_NW_OLD			= 0x3 << BT_COEX_MODE_POS,
	BT_COEX_AUTO_OLD		= 0x5 << BT_COEX_MODE_POS,
	BT_COEX_BT_OLD			= 0x6 << BT_COEX_MODE_POS,
	BT_COEX_WIFI_OLD		= 0x7 << BT_COEX_MODE_POS,
	BT_COEX_SYNC2SCO		= BIT(7),
	BT_COEX_CORUNNING		= BIT(8),
	BT_COEX_MPLUT			= BIT(9),
	BT_COEX_TTC			= BIT(20),
	BT_COEX_RRC			= BIT(21),
};

/*
 * indicates what has changed in the BT_COEX command.
 * BT_VALID_ENABLE must be set for each command. Commands without this bit will
 * discarded by the firmware
 */
enum iwl_bt_coex_valid_bit_msk {
	BT_VALID_ENABLE			= BIT(0),
	BT_VALID_BT_PRIO_BOOST		= BIT(1),
	BT_VALID_MAX_KILL		= BIT(2),
	BT_VALID_3W_TMRS		= BIT(3),
	BT_VALID_KILL_ACK		= BIT(4),
	BT_VALID_KILL_CTS		= BIT(5),
	BT_VALID_REDUCED_TX_POWER	= BIT(6),
	BT_VALID_LUT			= BIT(7),
	BT_VALID_WIFI_RX_SW_PRIO_BOOST	= BIT(8),
	BT_VALID_WIFI_TX_SW_PRIO_BOOST	= BIT(9),
	BT_VALID_MULTI_PRIO_LUT		= BIT(10),
	BT_VALID_TRM_KICK_FILTER	= BIT(11),
	BT_VALID_CORUN_LUT_20		= BIT(12),
	BT_VALID_CORUN_LUT_40		= BIT(13),
	BT_VALID_ANT_ISOLATION		= BIT(14),
	BT_VALID_ANT_ISOLATION_THRS	= BIT(15),
	BT_VALID_TXTX_DELTA_FREQ_THRS	= BIT(16),
	BT_VALID_TXRX_MAX_FREQ_0	= BIT(17),
	BT_VALID_SYNC_TO_SCO		= BIT(18),
	BT_VALID_TTC			= BIT(20),
	BT_VALID_RRC			= BIT(21),
};

/**
 * enum iwl_bt_reduced_tx_power - allows to reduce txpower for WiFi frames.
 * @BT_REDUCED_TX_POWER_CTL: reduce Tx power for control frames
 * @BT_REDUCED_TX_POWER_DATA: reduce Tx power for data frames
 *
 * This mechanism allows to have BT and WiFi run concurrently. Since WiFi
 * reduces its Tx power, it can work along with BT, hence reducing the amount
 * of WiFi frames being killed by BT.
 */
enum iwl_bt_reduced_tx_power {
	BT_REDUCED_TX_POWER_CTL		= BIT(0),
	BT_REDUCED_TX_POWER_DATA	= BIT(1),
};

enum iwl_bt_coex_lut_type {
	BT_COEX_TIGHT_LUT = 0,
	BT_COEX_LOOSE_LUT,
	BT_COEX_TX_DIS_LUT,

	BT_COEX_MAX_LUT,
	BT_COEX_INVALID_LUT = 0xff,
}; /* BT_COEX_DECISION_LUT_INDEX_API_E_VER_1 */

#define BT_COEX_LUT_SIZE (12)
#define BT_COEX_CORUN_LUT_SIZE (32)
#define BT_COEX_MULTI_PRIO_LUT_SIZE (2)
#define BT_COEX_BOOST_SIZE (4)
#define BT_REDUCED_TX_POWER_BIT BIT(7)

/**
 * struct iwl_bt_coex_cmd_old - bt coex configuration command
 * @flags:&enum iwl_bt_coex_flags
 * @max_kill:
 * @bt_reduced_tx_power: enum %iwl_bt_reduced_tx_power
 * @override_primary_lut: enum %iwl_bt_coex_lut_type: BT_COEX_INVALID_LUT
 *	should be set by default
 * @override_secondary_lut: enum %iwl_bt_coex_lut_type: BT_COEX_INVALID_LUT
 *	should be set by default
 * @bt4_antenna_isolation: antenna isolation
 * @bt4_antenna_isolation_thr: antenna threshold value
 * @bt4_tx_tx_delta_freq_thr: TxTx delta frequency
 * @bt4_tx_rx_max_freq0: TxRx max frequency
 * @bt_prio_boost: BT priority boost registers
 * @wifi_tx_prio_boost: SW boost of wifi tx priority
 * @wifi_rx_prio_boost: SW boost of wifi rx priority
 * @kill_ack_msk: kill ACK mask. 1 - Tx ACK, 0 - kill Tx of ACK.
 * @kill_cts_msk: kill CTS mask. 1 - Tx CTS, 0 - kill Tx of CTS.
 * @decision_lut: PTA decision LUT, per Prio-Ch
 * @bt4_multiprio_lut: multi priority LUT configuration
 * @bt4_corun_lut20: co-running 20 MHz LUT configuration
 * @bt4_corun_lut40: co-running 40 MHz LUT configuration
 * @valid_bit_msk: enum %iwl_bt_coex_valid_bit_msk
 *
 * The structure is used for the BT_COEX command.
 */
struct iwl_bt_coex_cmd_old {
	__le32 flags;
	u8 max_kill;
	u8 bt_reduced_tx_power;
	u8 override_primary_lut;
	u8 override_secondary_lut;

	u8 bt4_antenna_isolation;
	u8 bt4_antenna_isolation_thr;
	u8 bt4_tx_tx_delta_freq_thr;
	u8 bt4_tx_rx_max_freq0;

	__le32 bt_prio_boost[BT_COEX_BOOST_SIZE];
	__le32 wifi_tx_prio_boost;
	__le32 wifi_rx_prio_boost;
	__le32 kill_ack_msk;
	__le32 kill_cts_msk;

	__le32 decision_lut[BT_COEX_MAX_LUT][BT_COEX_LUT_SIZE];
	__le32 bt4_multiprio_lut[BT_COEX_MULTI_PRIO_LUT_SIZE];
	__le32 bt4_corun_lut20[BT_COEX_CORUN_LUT_SIZE];
	__le32 bt4_corun_lut40[BT_COEX_CORUN_LUT_SIZE];

	__le32 valid_bit_msk;
} __packed; /* BT_COEX_CMD_API_S_VER_5 */

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
 * @mode: enum %iwl_bt_coex_mode
 * @enabled_modules: enum %iwl_bt_coex_enabled_modules
 * @max_kill: max count of Tx retries due to kill from PTA
 * @override_primary_lut: enum %iwl_bt_coex_lut_type: BT_COEX_INVALID_LUT
 *	should be set by default
 * @override_secondary_lut: enum %iwl_bt_coex_lut_type: BT_COEX_INVALID_LUT
 *	should be set by default
 * @bt4_antenna_isolation_thr: antenna threshold value
 * @bt4_tx_tx_delta_freq_thr: TxTx delta frequency
 * @bt4_tx_rx_max_freq0: TxRx max frequency
 * @multiprio_lut: multi priority LUT configuration
 * @mplut_prio_boost: BT priority boost registers
 * @decision_lut: PTA decision LUT, per Prio-Ch
 *
 * The structure is used for the BT_COEX command.
 */
struct iwl_bt_coex_cmd {
	__le32 mode;
	__le32 enabled_modules;

	__le32 max_kill;
	__le32 override_primary_lut;
	__le32 override_secondary_lut;
	__le32 bt4_antenna_isolation_thr;

	__le32 bt4_tx_tx_delta_freq_thr;
	__le32 bt4_tx_rx_max_freq0;

	__le32 multiprio_lut[BT_COEX_MULTI_PRIO_LUT_SIZE];
	__le32 mplut_prio_boost[BT_COEX_BOOST_SIZE];

	__le32 decision_lut[BT_COEX_MAX_LUT][BT_COEX_LUT_SIZE];
} __packed; /* BT_COEX_CMD_API_S_VER_6 */

/**
 * struct iwl_bt_coex_corun_lut_update - bt coex update the corun lut
 * @corun_lut20: co-running 20 MHz LUT configuration
 * @corun_lut40: co-running 40 MHz LUT configuration
 *
 * The structure is used for the BT_COEX_UPDATE_CORUN_LUT command.
 */
struct iwl_bt_coex_corun_lut_update_cmd {
	__le32 corun_lut20[BT_COEX_CORUN_LUT_SIZE];
	__le32 corun_lut40[BT_COEX_CORUN_LUT_SIZE];
} __packed; /* BT_COEX_UPDATE_CORUN_LUT_API_S_VER_1 */

/**
 * struct iwl_bt_coex_sw_boost - SW boost values
 * @wifi_tx_prio_boost: SW boost of wifi tx priority
 * @wifi_rx_prio_boost: SW boost of wifi rx priority
 * @kill_ack_msk: kill ACK mask. 1 - Tx ACK, 0 - kill Tx of ACK.
 * @kill_cts_msk: kill CTS mask. 1 - Tx CTS, 0 - kill Tx of CTS.
 */
struct iwl_bt_coex_sw_boost {
	__le32 wifi_tx_prio_boost;
	__le32 wifi_rx_prio_boost;
	__le32 kill_ack_msk;
	__le32 kill_cts_msk;
};

/**
 * struct iwl_bt_coex_sw_boost_update_cmd - command to update the SW boost
 * @boost_values: check struct  %iwl_bt_coex_sw_boost - one for each channel
 *	primary / secondary / low priority
 */
struct iwl_bt_coex_sw_boost_update_cmd {
	struct iwl_bt_coex_sw_boost boost_values[3];
} __packed; /* BT_COEX_UPDATE_SW_BOOST_S_VER_1 */

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
 * @bt_primary_ci:
 * @primary_ch_phy_id:
 * @bt_secondary_ci:
 * @secondary_ch_phy_id:
 *
 * Used for BT_COEX_CI command
 */
struct iwl_bt_coex_ci_cmd {
	__le64 bt_primary_ci;
	__le32 primary_ch_phy_id;

	__le64 bt_secondary_ci;
	__le32 secondary_ch_phy_id;
} __packed; /* BT_CI_MSG_API_S_VER_2 */

#define BT_MBOX(n_dw, _msg, _pos, _nbits)	\
	BT_MBOX##n_dw##_##_msg##_POS = (_pos),	\
	BT_MBOX##n_dw##_##_msg = BITS(_nbits) << BT_MBOX##n_dw##_##_msg##_POS

enum iwl_bt_mxbox_dw0 {
	BT_MBOX(0, LE_SLAVE_LAT, 0, 3),
	BT_MBOX(0, LE_PROF1, 3, 1),
	BT_MBOX(0, LE_PROF2, 4, 1),
	BT_MBOX(0, LE_PROF_OTHER, 5, 1),
	BT_MBOX(0, CHL_SEQ_N, 8, 4),
	BT_MBOX(0, INBAND_S, 13, 1),
	BT_MBOX(0, LE_MIN_RSSI, 16, 4),
	BT_MBOX(0, LE_SCAN, 20, 1),
	BT_MBOX(0, LE_ADV, 21, 1),
	BT_MBOX(0, LE_MAX_TX_POWER, 24, 4),
	BT_MBOX(0, OPEN_CON_1, 28, 2),
};

enum iwl_bt_mxbox_dw1 {
	BT_MBOX(1, BR_MAX_TX_POWER, 0, 4),
	BT_MBOX(1, IP_SR, 4, 1),
	BT_MBOX(1, LE_MSTR, 5, 1),
	BT_MBOX(1, AGGR_TRFC_LD, 8, 6),
	BT_MBOX(1, MSG_TYPE, 16, 3),
	BT_MBOX(1, SSN, 19, 2),
};

enum iwl_bt_mxbox_dw2 {
	BT_MBOX(2, SNIFF_ACT, 0, 3),
	BT_MBOX(2, PAG, 3, 1),
	BT_MBOX(2, INQUIRY, 4, 1),
	BT_MBOX(2, CONN, 5, 1),
	BT_MBOX(2, SNIFF_INTERVAL, 8, 5),
	BT_MBOX(2, DISC, 13, 1),
	BT_MBOX(2, SCO_TX_ACT, 16, 2),
	BT_MBOX(2, SCO_RX_ACT, 18, 2),
	BT_MBOX(2, ESCO_RE_TX, 20, 2),
	BT_MBOX(2, SCO_DURATION, 24, 6),
};

enum iwl_bt_mxbox_dw3 {
	BT_MBOX(3, SCO_STATE, 0, 1),
	BT_MBOX(3, SNIFF_STATE, 1, 1),
	BT_MBOX(3, A2DP_STATE, 2, 1),
	BT_MBOX(3, ACL_STATE, 3, 1),
	BT_MBOX(3, MSTR_STATE, 4, 1),
	BT_MBOX(3, OBX_STATE, 5, 1),
	BT_MBOX(3, OPEN_CON_2, 8, 2),
	BT_MBOX(3, TRAFFIC_LOAD, 10, 2),
	BT_MBOX(3, CHL_SEQN_LSB, 12, 1),
	BT_MBOX(3, INBAND_P, 13, 1),
	BT_MBOX(3, MSG_TYPE_2, 16, 3),
	BT_MBOX(3, SSN_2, 19, 2),
	BT_MBOX(3, UPDATE_REQUEST, 21, 1),
};

#define BT_MBOX_MSG(_notif, _num, _field)				     \
	((le32_to_cpu((_notif)->mbox_msg[(_num)]) & BT_MBOX##_num##_##_field)\
	>> BT_MBOX##_num##_##_field##_POS)

enum iwl_bt_activity_grading {
	BT_OFF			= 0,
	BT_ON_NO_CONNECTION	= 1,
	BT_LOW_TRAFFIC		= 2,
	BT_HIGH_TRAFFIC		= 3,

	BT_MAX_AG,
}; /* BT_COEX_BT_ACTIVITY_GRADING_API_E_VER_1 */

enum iwl_bt_ci_compliance {
	BT_CI_COMPLIANCE_NONE		= 0,
	BT_CI_COMPLIANCE_PRIMARY	= 1,
	BT_CI_COMPLIANCE_SECONDARY	= 2,
	BT_CI_COMPLIANCE_BOTH		= 3,
}; /* BT_COEX_CI_COMPLIENCE_E_VER_1 */

#define IWL_COEX_IS_TTC_ON(_ttc_rrc_status, _phy_id)	\
		(_ttc_rrc_status & BIT(_phy_id))

#define IWL_COEX_IS_RRC_ON(_ttc_rrc_status, _phy_id)	\
		((_ttc_rrc_status >> 4) & BIT(_phy_id))

/**
 * struct iwl_bt_coex_profile_notif - notification about BT coex
 * @mbox_msg: message from BT to WiFi
 * @msg_idx: the index of the message
 * @bt_ci_compliance: enum %iwl_bt_ci_compliance
 * @primary_ch_lut: LUT used for primary channel enum %iwl_bt_coex_lut_type
 * @secondary_ch_lut: LUT used for secondary channel enume %iwl_bt_coex_lut_type
 * @bt_activity_grading: the activity of BT enum %iwl_bt_activity_grading
 * @ttc_rrc_status: is TTC or RRC enabled - one bit per PHY
 */
struct iwl_bt_coex_profile_notif {
	__le32 mbox_msg[4];
	__le32 msg_idx;
	__le32 bt_ci_compliance;

	__le32 primary_ch_lut;
	__le32 secondary_ch_lut;
	__le32 bt_activity_grading;
	u8 ttc_rrc_status;
	u8 reserved[3];
} __packed; /* BT_COEX_PROFILE_NTFY_API_S_VER_4 */

enum iwl_bt_coex_prio_table_event {
	BT_COEX_PRIO_TBL_EVT_INIT_CALIB1		= 0,
	BT_COEX_PRIO_TBL_EVT_INIT_CALIB2		= 1,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW1	= 2,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_LOW2	= 3,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH1	= 4,
	BT_COEX_PRIO_TBL_EVT_PERIODIC_CALIB_HIGH2	= 5,
	BT_COEX_PRIO_TBL_EVT_DTIM			= 6,
	BT_COEX_PRIO_TBL_EVT_SCAN52			= 7,
	BT_COEX_PRIO_TBL_EVT_SCAN24			= 8,
	BT_COEX_PRIO_TBL_EVT_IDLE			= 9,
	BT_COEX_PRIO_TBL_EVT_MAX			= 16,
}; /* BT_COEX_PRIO_TABLE_EVENTS_API_E_VER_1 */

enum iwl_bt_coex_prio_table_prio {
	BT_COEX_PRIO_TBL_DISABLED	= 0,
	BT_COEX_PRIO_TBL_PRIO_LOW	= 1,
	BT_COEX_PRIO_TBL_PRIO_HIGH	= 2,
	BT_COEX_PRIO_TBL_PRIO_BYPASS	= 3,
	BT_COEX_PRIO_TBL_PRIO_COEX_OFF	= 4,
	BT_COEX_PRIO_TBL_PRIO_COEX_ON	= 5,
	BT_COEX_PRIO_TBL_PRIO_COEX_IDLE = 6,
	BT_COEX_PRIO_TBL_MAX		= 8,
}; /* BT_COEX_PRIO_TABLE_PRIORITIES_API_E_VER_1 */

#define BT_COEX_PRIO_TBL_SHRD_ANT_POS     (0)
#define BT_COEX_PRIO_TBL_PRIO_POS         (1)
#define BT_COEX_PRIO_TBL_RESERVED_POS     (4)

/**
 * struct iwl_bt_coex_prio_tbl_cmd - priority table for BT coex
 * @prio_tbl:
 */
struct iwl_bt_coex_prio_tbl_cmd {
	u8 prio_tbl[BT_COEX_PRIO_TBL_EVT_MAX];
} __packed;

/**
 * struct iwl_bt_coex_ci_cmd_old - bt coex channel inhibition command
 * @bt_primary_ci:
 * @bt_secondary_ci:
 * @co_run_bw_primary:
 * @co_run_bw_secondary:
 * @primary_ch_phy_id:
 * @secondary_ch_phy_id:
 *
 * Used for BT_COEX_CI command
 */
struct iwl_bt_coex_ci_cmd_old {
	__le64 bt_primary_ci;
	__le64 bt_secondary_ci;

	u8 co_run_bw_primary;
	u8 co_run_bw_secondary;
	u8 primary_ch_phy_id;
	u8 secondary_ch_phy_id;
} __packed; /* BT_CI_MSG_API_S_VER_1 */

/**
 * struct iwl_bt_coex_profile_notif_old - notification about BT coex
 * @mbox_msg: message from BT to WiFi
 * @msg_idx: the index of the message
 * @bt_status: 0 - off, 1 - on
 * @bt_open_conn: number of BT connections open
 * @bt_traffic_load: load of BT traffic
 * @bt_agg_traffic_load: aggregated load of BT traffic
 * @bt_ci_compliance: 0 - no CI compliance, 1 - CI compliant
 * @primary_ch_lut: LUT used for primary channel
 * @secondary_ch_lut: LUT used for secondary channel
 * @bt_activity_grading: the activity of BT enum %iwl_bt_activity_grading
 */
struct iwl_bt_coex_profile_notif_old {
	__le32 mbox_msg[4];
	__le32 msg_idx;
	u8 bt_status;
	u8 bt_open_conn;
	u8 bt_traffic_load;
	u8 bt_agg_traffic_load;
	u8 bt_ci_compliance;
	u8 ttc_enabled;
	u8 rrc_enabled;
	u8 reserved;

	__le32 primary_ch_lut;
	__le32 secondary_ch_lut;
	__le32 bt_activity_grading;
} __packed; /* BT_COEX_PROFILE_NTFY_API_S_VER_3 */

#endif /* __fw_api_bt_coex_h__ */
