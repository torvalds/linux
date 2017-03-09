/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
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

#ifndef __fw_api_power_h__
#define __fw_api_power_h__

/* Power Management Commands, Responses, Notifications */

/**
 * enum iwl_ltr_config_flags - masks for LTR config command flags
 * @LTR_CFG_FLAG_FEATURE_ENABLE: Feature operational status
 * @LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS: allow LTR change on shadow
 *	memory access
 * @LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH: allow LTR msg send on ANY LTR
 *	reg change
 * @LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3: allow LTR msg send on transition from
 *	D0 to D3
 * @LTR_CFG_FLAG_SW_SET_SHORT: fixed static short LTR register
 * @LTR_CFG_FLAG_SW_SET_LONG: fixed static short LONG register
 * @LTR_CFG_FLAG_DENIE_C10_ON_PD: allow going into C10 on PD
 */
enum iwl_ltr_config_flags {
	LTR_CFG_FLAG_FEATURE_ENABLE = BIT(0),
	LTR_CFG_FLAG_HW_DIS_ON_SHADOW_REG_ACCESS = BIT(1),
	LTR_CFG_FLAG_HW_EN_SHRT_WR_THROUGH = BIT(2),
	LTR_CFG_FLAG_HW_DIS_ON_D0_2_D3 = BIT(3),
	LTR_CFG_FLAG_SW_SET_SHORT = BIT(4),
	LTR_CFG_FLAG_SW_SET_LONG = BIT(5),
	LTR_CFG_FLAG_DENIE_C10_ON_PD = BIT(6),
};

/**
 * struct iwl_ltr_config_cmd_v1 - configures the LTR
 * @flags: See %enum iwl_ltr_config_flags
 */
struct iwl_ltr_config_cmd_v1 {
	__le32 flags;
	__le32 static_long;
	__le32 static_short;
} __packed; /* LTR_CAPABLE_API_S_VER_1 */

#define LTR_VALID_STATES_NUM 4

/**
 * struct iwl_ltr_config_cmd - configures the LTR
 * @flags: See %enum iwl_ltr_config_flags
 * @static_long:
 * @static_short:
 * @ltr_cfg_values:
 * @ltr_short_idle_timeout:
 */
struct iwl_ltr_config_cmd {
	__le32 flags;
	__le32 static_long;
	__le32 static_short;
	__le32 ltr_cfg_values[LTR_VALID_STATES_NUM];
	__le32 ltr_short_idle_timeout;
} __packed; /* LTR_CAPABLE_API_S_VER_2 */

/* Radio LP RX Energy Threshold measured in dBm */
#define POWER_LPRX_RSSI_THRESHOLD	75
#define POWER_LPRX_RSSI_THRESHOLD_MAX	94
#define POWER_LPRX_RSSI_THRESHOLD_MIN	30

/**
 * enum iwl_power_flags - masks for power table command flags
 * @POWER_FLAGS_POWER_SAVE_ENA_MSK: '1' Allow to save power by turning off
 *		receiver and transmitter. '0' - does not allow.
 * @POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK: '0' Driver disables power management,
 *		'1' Driver enables PM (use rest of parameters)
 * @POWER_FLAGS_SKIP_OVER_DTIM_MSK: '0' PM have to walk up every DTIM,
 *		'1' PM could sleep over DTIM till listen Interval.
 * @POWER_FLAGS_SNOOZE_ENA_MSK: Enable snoozing only if uAPSD is enabled and all
 *		access categories are both delivery and trigger enabled.
 * @POWER_FLAGS_BT_SCO_ENA: Enable BT SCO coex only if uAPSD and
 *		PBW Snoozing enabled
 * @POWER_FLAGS_ADVANCE_PM_ENA_MSK: Advanced PM (uAPSD) enable mask
 * @POWER_FLAGS_LPRX_ENA_MSK: Low Power RX enable.
 * @POWER_FLAGS_AP_UAPSD_MISBEHAVING_ENA_MSK: AP/GO's uAPSD misbehaving
 *		detection enablement
*/
enum iwl_power_flags {
	POWER_FLAGS_POWER_SAVE_ENA_MSK		= BIT(0),
	POWER_FLAGS_POWER_MANAGEMENT_ENA_MSK	= BIT(1),
	POWER_FLAGS_SKIP_OVER_DTIM_MSK		= BIT(2),
	POWER_FLAGS_SNOOZE_ENA_MSK		= BIT(5),
	POWER_FLAGS_BT_SCO_ENA			= BIT(8),
	POWER_FLAGS_ADVANCE_PM_ENA_MSK		= BIT(9),
	POWER_FLAGS_LPRX_ENA_MSK		= BIT(11),
	POWER_FLAGS_UAPSD_MISBEHAVING_ENA_MSK	= BIT(12),
};

#define IWL_POWER_VEC_SIZE 5

/**
 * struct iwl_powertable_cmd - legacy power command. Beside old API support this
 *	is used also with a new	power API for device wide power settings.
 * POWER_TABLE_CMD = 0x77 (command, has simple generic response)
 *
 * @flags:		Power table command flags from POWER_FLAGS_*
 * @keep_alive_seconds: Keep alive period in seconds. Default - 25 sec.
 *			Minimum allowed:- 3 * DTIM. Keep alive period must be
 *			set regardless of power scheme or current power state.
 *			FW use this value also when PM is disabled.
 * @rx_data_timeout:    Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - legacy PM
 * @tx_data_timeout:    Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - legacy PM
 * @sleep_interval:	not in use
 * @skip_dtim_periods:	Number of DTIM periods to skip if Skip over DTIM flag
 *			is set. For example, if it is required to skip over
 *			one DTIM, this value need to be set to 2 (DTIM periods).
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 */
struct iwl_powertable_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_6 */
	__le16 flags;
	u8 keep_alive_seconds;
	u8 debug_flags;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 sleep_interval[IWL_POWER_VEC_SIZE];
	__le32 skip_dtim_periods;
	__le32 lprx_rssi_threshold;
} __packed;

/**
 * enum iwl_device_power_flags - masks for device power command flags
 * @DEVIC_POWER_FLAGS_POWER_SAVE_ENA_MSK: '1' Allow to save power by turning off
 *	receiver and transmitter. '0' - does not allow.
*/
enum iwl_device_power_flags {
	DEVICE_POWER_FLAGS_POWER_SAVE_ENA_MSK	= BIT(0),
};

/**
 * struct iwl_device_power_cmd - device wide power command.
 * DEVICE_POWER_CMD = 0x77 (command, has simple generic response)
 *
 * @flags:	Power table command flags from DEVICE_POWER_FLAGS_*
 */
struct iwl_device_power_cmd {
	/* PM_POWER_TABLE_CMD_API_S_VER_6 */
	__le16 flags;
	__le16 reserved;
} __packed;

/**
 * struct iwl_mac_power_cmd - New power command containing uAPSD support
 * MAC_PM_POWER_TABLE = 0xA9 (command, has simple generic response)
 * @id_and_color:	MAC contex identifier
 * @flags:		Power table command flags from POWER_FLAGS_*
 * @keep_alive_seconds:	Keep alive period in seconds. Default - 25 sec.
 *			Minimum allowed:- 3 * DTIM. Keep alive period must be
 *			set regardless of power scheme or current power state.
 *			FW use this value also when PM is disabled.
 * @rx_data_timeout:    Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - legacy PM
 * @tx_data_timeout:    Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - legacy PM
 * @sleep_interval:	not in use
 * @skip_dtim_periods:	Number of DTIM periods to skip if Skip over DTIM flag
 *			is set. For example, if it is required to skip over
 *			one DTIM, this value need to be set to 2 (DTIM periods).
 * @rx_data_timeout_uapsd: Minimum time (usec) from last Rx packet for AM to
 *			PSM transition - uAPSD
 * @tx_data_timeout_uapsd: Minimum time (usec) from last Tx packet for AM to
 *			PSM transition - uAPSD
 * @lprx_rssi_threshold: Signal strength up to which LP RX can be enabled.
 *			Default: 80dbm
 * @num_skip_dtim:	Number of DTIMs to skip if Skip over DTIM flag is set
 * @snooze_interval:	Maximum time between attempts to retrieve buffered data
 *			from the AP [msec]
 * @snooze_window:	A window of time in which PBW snoozing insures that all
 *			packets received. It is also the minimum time from last
 *			received unicast RX packet, before client stops snoozing
 *			for data. [msec]
 * @snooze_step:	TBD
 * @qndp_tid:		TID client shall use for uAPSD QNDP triggers
 * @uapsd_ac_flags:	Set trigger-enabled and delivery-enabled indication for
 *			each corresponding AC.
 *			Use IEEE80211_WMM_IE_STA_QOSINFO_AC* for correct values.
 * @uapsd_max_sp:	Use IEEE80211_WMM_IE_STA_QOSINFO_SP_* for correct
 *			values.
 * @heavy_tx_thld_packets:	TX threshold measured in number of packets
 * @heavy_rx_thld_packets:	RX threshold measured in number of packets
 * @heavy_tx_thld_percentage:	TX threshold measured in load's percentage
 * @heavy_rx_thld_percentage:	RX threshold measured in load's percentage
 * @limited_ps_threshold:
*/
struct iwl_mac_power_cmd {
	/* CONTEXT_DESC_API_T_VER_1 */
	__le32 id_and_color;

	/* CLIENT_PM_POWER_TABLE_S_VER_1 */
	__le16 flags;
	__le16 keep_alive_seconds;
	__le32 rx_data_timeout;
	__le32 tx_data_timeout;
	__le32 rx_data_timeout_uapsd;
	__le32 tx_data_timeout_uapsd;
	u8 lprx_rssi_threshold;
	u8 skip_dtim_periods;
	__le16 snooze_interval;
	__le16 snooze_window;
	u8 snooze_step;
	u8 qndp_tid;
	u8 uapsd_ac_flags;
	u8 uapsd_max_sp;
	u8 heavy_tx_thld_packets;
	u8 heavy_rx_thld_packets;
	u8 heavy_tx_thld_percentage;
	u8 heavy_rx_thld_percentage;
	u8 limited_ps_threshold;
	u8 reserved;
} __packed;

/*
 * struct iwl_uapsd_misbehaving_ap_notif - FW sends this notification when
 * associated AP is identified as improperly implementing uAPSD protocol.
 * PSM_UAPSD_AP_MISBEHAVING_NOTIFICATION = 0x78
 * @sta_id: index of station in uCode's station table - associated AP ID in
 *	    this context.
 */
struct iwl_uapsd_misbehaving_ap_notif {
	__le32 sta_id;
	u8 mac_id;
	u8 reserved[3];
} __packed;

/**
 * struct iwl_reduce_tx_power_cmd - TX power reduction command
 * REDUCE_TX_POWER_CMD = 0x9f
 * @flags: (reserved for future implementation)
 * @mac_context_id: id of the mac ctx for which we are reducing TX power.
 * @pwr_restriction: TX power restriction in dBms.
 */
struct iwl_reduce_tx_power_cmd {
	u8 flags;
	u8 mac_context_id;
	__le16 pwr_restriction;
} __packed; /* TX_REDUCED_POWER_API_S_VER_1 */

enum iwl_dev_tx_power_cmd_mode {
	IWL_TX_POWER_MODE_SET_MAC = 0,
	IWL_TX_POWER_MODE_SET_DEVICE = 1,
	IWL_TX_POWER_MODE_SET_CHAINS = 2,
	IWL_TX_POWER_MODE_SET_ACK = 3,
}; /* TX_POWER_REDUCED_FLAGS_TYPE_API_E_VER_4 */;

#define IWL_NUM_CHAIN_LIMITS	2
#define IWL_NUM_SUB_BANDS	5

/**
 * struct iwl_dev_tx_power_cmd - TX power reduction command
 * @set_mode: see &enum iwl_dev_tx_power_cmd_mode
 * @mac_context_id: id of the mac ctx for which we are reducing TX power.
 * @pwr_restriction: TX power restriction in 1/8 dBms.
 * @dev_24: device TX power restriction in 1/8 dBms
 * @dev_52_low: device TX power restriction upper band - low
 * @dev_52_high: device TX power restriction upper band - high
 * @per_chain_restriction: per chain restrictions
 */
struct iwl_dev_tx_power_cmd_v3 {
	__le32 set_mode;
	__le32 mac_context_id;
	__le16 pwr_restriction;
	__le16 dev_24;
	__le16 dev_52_low;
	__le16 dev_52_high;
	__le16 per_chain_restriction[IWL_NUM_CHAIN_LIMITS][IWL_NUM_SUB_BANDS];
} __packed; /* TX_REDUCED_POWER_API_S_VER_3 */

#define IWL_DEV_MAX_TX_POWER 0x7FFF

/**
 * struct iwl_dev_tx_power_cmd - TX power reduction command
 * @v3: version 3 of the command, embedded here for easier software handling
 * @enable_ack_reduction: enable or disable close range ack TX power
 *	reduction.
 */
struct iwl_dev_tx_power_cmd {
	/* v4 is just an extension of v3 - keep this here */
	struct iwl_dev_tx_power_cmd_v3 v3;
	u8 enable_ack_reduction;
	u8 reserved[3];
} __packed; /* TX_REDUCED_POWER_API_S_VER_4 */

#define IWL_NUM_GEO_PROFILES	3

/**
 * enum iwl_geo_per_chain_offset_operation - type of operation
 * @IWL_PER_CHAIN_OFFSET_SET_TABLES: send the tables from the host to the FW.
 * @IWL_PER_CHAIN_OFFSET_GET_CURRENT_TABLE: retrieve the last configured table.
 */
enum iwl_geo_per_chain_offset_operation {
	IWL_PER_CHAIN_OFFSET_SET_TABLES,
	IWL_PER_CHAIN_OFFSET_GET_CURRENT_TABLE,
};  /* GEO_TX_POWER_LIMIT FLAGS TYPE */

/**
 * struct iwl_per_chain_offset - embedded struct for GEO_TX_POWER_LIMIT.
 * @max_tx_power: maximum allowed tx power.
 * @chain_a: tx power offset for chain a.
 * @chain_b: tx power offset for chain b.
 */
struct iwl_per_chain_offset {
	__le16 max_tx_power;
	u8 chain_a;
	u8 chain_b;
} __packed; /* PER_CHAIN_LIMIT_OFFSET_PER_CHAIN_S_VER_1 */

struct iwl_per_chain_offset_group {
	struct iwl_per_chain_offset lb;
	struct iwl_per_chain_offset hb;
} __packed; /* PER_CHAIN_LIMIT_OFFSET_GROUP_S_VER_1 */

/**
 * struct iwl_geo_tx_power_profile_cmd - struct for GEO_TX_POWER_LIMIT cmd.
 * @ops: operations, value from &enum iwl_geo_per_chain_offset_operation
 * @table: offset profile per band.
 */
struct iwl_geo_tx_power_profiles_cmd {
	__le32 ops;
	struct iwl_per_chain_offset_group table[IWL_NUM_GEO_PROFILES];
} __packed; /* GEO_TX_POWER_LIMIT */

/**
 * struct iwl_beacon_filter_cmd
 * REPLY_BEACON_FILTERING_CMD = 0xd2 (command)
 * @id_and_color: MAC contex identifier
 * @bf_energy_delta: Used for RSSI filtering, if in 'normal' state. Send beacon
 *      to driver if delta in Energy values calculated for this and last
 *      passed beacon is greater than this threshold. Zero value means that
 *      the Energy change is ignored for beacon filtering, and beacon will
 *      not be forced to be sent to driver regardless of this delta. Typical
 *      energy delta 5dB.
 * @bf_roaming_energy_delta: Used for RSSI filtering, if in 'roaming' state.
 *      Send beacon to driver if delta in Energy values calculated for this
 *      and last passed beacon is greater than this threshold. Zero value
 *      means that the Energy change is ignored for beacon filtering while in
 *      Roaming state, typical energy delta 1dB.
 * @bf_roaming_state: Used for RSSI filtering. If absolute Energy values
 *      calculated for current beacon is less than the threshold, use
 *      Roaming Energy Delta Threshold, otherwise use normal Energy Delta
 *      Threshold. Typical energy threshold is -72dBm.
 * @bf_temp_threshold: This threshold determines the type of temperature
 *	filtering (Slow or Fast) that is selected (Units are in Celsuis):
 *	If the current temperature is above this threshold - Fast filter
 *	will be used, If the current temperature is below this threshold -
 *	Slow filter will be used.
 * @bf_temp_fast_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be  forced to be sent to driver
 *      regardless of whether its temerature has been changed.
 * @bf_temp_slow_filter: Send Beacon to driver if delta in temperature values
 *      calculated for this and the last passed beacon is greater than this
 *      threshold. Zero value means that the temperature change is ignored for
 *      beacon filtering; beacons will not be forced to be sent to driver
 *      regardless of whether its temerature has been changed.
 * @bf_enable_beacon_filter: 1, beacon filtering is enabled; 0, disabled.
 * @bf_filter_escape_timer: Send beacons to to driver if no beacons were passed
 *      for a specific period of time. Units: Beacons.
 * @ba_escape_timer: Fully receive and parse beacon if no beacons were passed
 *      for a longer period of time then this escape-timeout. Units: Beacons.
 * @ba_enable_beacon_abort: 1, beacon abort is enabled; 0, disabled.
 */
struct iwl_beacon_filter_cmd {
	__le32 bf_energy_delta;
	__le32 bf_roaming_energy_delta;
	__le32 bf_roaming_state;
	__le32 bf_temp_threshold;
	__le32 bf_temp_fast_filter;
	__le32 bf_temp_slow_filter;
	__le32 bf_enable_beacon_filter;
	__le32 bf_debug_flag;
	__le32 bf_escape_timer;
	__le32 ba_escape_timer;
	__le32 ba_enable_beacon_abort;
} __packed;

/* Beacon filtering and beacon abort */
#define IWL_BF_ENERGY_DELTA_DEFAULT 5
#define IWL_BF_ENERGY_DELTA_D0I3 20
#define IWL_BF_ENERGY_DELTA_MAX 255
#define IWL_BF_ENERGY_DELTA_MIN 0

#define IWL_BF_ROAMING_ENERGY_DELTA_DEFAULT 1
#define IWL_BF_ROAMING_ENERGY_DELTA_D0I3 20
#define IWL_BF_ROAMING_ENERGY_DELTA_MAX 255
#define IWL_BF_ROAMING_ENERGY_DELTA_MIN 0

#define IWL_BF_ROAMING_STATE_DEFAULT 72
#define IWL_BF_ROAMING_STATE_D0I3 72
#define IWL_BF_ROAMING_STATE_MAX 255
#define IWL_BF_ROAMING_STATE_MIN 0

#define IWL_BF_TEMP_THRESHOLD_DEFAULT 112
#define IWL_BF_TEMP_THRESHOLD_D0I3 112
#define IWL_BF_TEMP_THRESHOLD_MAX 255
#define IWL_BF_TEMP_THRESHOLD_MIN 0

#define IWL_BF_TEMP_FAST_FILTER_DEFAULT 1
#define IWL_BF_TEMP_FAST_FILTER_D0I3 1
#define IWL_BF_TEMP_FAST_FILTER_MAX 255
#define IWL_BF_TEMP_FAST_FILTER_MIN 0

#define IWL_BF_TEMP_SLOW_FILTER_DEFAULT 5
#define IWL_BF_TEMP_SLOW_FILTER_D0I3 20
#define IWL_BF_TEMP_SLOW_FILTER_MAX 255
#define IWL_BF_TEMP_SLOW_FILTER_MIN 0

#define IWL_BF_ENABLE_BEACON_FILTER_DEFAULT 1

#define IWL_BF_DEBUG_FLAG_DEFAULT 0
#define IWL_BF_DEBUG_FLAG_D0I3 0

#define IWL_BF_ESCAPE_TIMER_DEFAULT 0
#define IWL_BF_ESCAPE_TIMER_D0I3 0
#define IWL_BF_ESCAPE_TIMER_MAX 1024
#define IWL_BF_ESCAPE_TIMER_MIN 0

#define IWL_BA_ESCAPE_TIMER_DEFAULT 6
#define IWL_BA_ESCAPE_TIMER_D0I3 6
#define IWL_BA_ESCAPE_TIMER_D3 9
#define IWL_BA_ESCAPE_TIMER_MAX 1024
#define IWL_BA_ESCAPE_TIMER_MIN 0

#define IWL_BA_ENABLE_BEACON_ABORT_DEFAULT 1

#define IWL_BF_CMD_CONFIG(mode)					     \
	.bf_energy_delta = cpu_to_le32(IWL_BF_ENERGY_DELTA ## mode),	      \
	.bf_roaming_energy_delta =					      \
		cpu_to_le32(IWL_BF_ROAMING_ENERGY_DELTA ## mode),	      \
	.bf_roaming_state = cpu_to_le32(IWL_BF_ROAMING_STATE ## mode),	      \
	.bf_temp_threshold = cpu_to_le32(IWL_BF_TEMP_THRESHOLD ## mode),      \
	.bf_temp_fast_filter = cpu_to_le32(IWL_BF_TEMP_FAST_FILTER ## mode),  \
	.bf_temp_slow_filter = cpu_to_le32(IWL_BF_TEMP_SLOW_FILTER ## mode),  \
	.bf_debug_flag = cpu_to_le32(IWL_BF_DEBUG_FLAG ## mode),	      \
	.bf_escape_timer = cpu_to_le32(IWL_BF_ESCAPE_TIMER ## mode),	      \
	.ba_escape_timer = cpu_to_le32(IWL_BA_ESCAPE_TIMER ## mode)

#define IWL_BF_CMD_CONFIG_DEFAULTS IWL_BF_CMD_CONFIG(_DEFAULT)
#define IWL_BF_CMD_CONFIG_D0I3 IWL_BF_CMD_CONFIG(_D0I3)
#endif
