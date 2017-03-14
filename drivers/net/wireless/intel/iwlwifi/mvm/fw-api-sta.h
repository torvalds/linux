/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2014 Intel Mobile Communications GmbH
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
 *****************************************************************************/

#ifndef __fw_api_sta_h__
#define __fw_api_sta_h__

/**
 * enum iwl_sta_flags - flags for the ADD_STA host command
 * @STA_FLG_REDUCED_TX_PWR_CTRL:
 * @STA_FLG_REDUCED_TX_PWR_DATA:
 * @STA_FLG_DISABLE_TX: set if TX should be disabled
 * @STA_FLG_PS: set if STA is in Power Save
 * @STA_FLG_INVALID: set if STA is invalid
 * @STA_FLG_DLP_EN: Direct Link Protocol is enabled
 * @STA_FLG_SET_ALL_KEYS: the current key applies to all key IDs
 * @STA_FLG_DRAIN_FLOW: drain flow
 * @STA_FLG_PAN: STA is for PAN interface
 * @STA_FLG_CLASS_AUTH:
 * @STA_FLG_CLASS_ASSOC:
 * @STA_FLG_CLASS_MIMO_PROT:
 * @STA_FLG_MAX_AGG_SIZE_MSK: maximal size for A-MPDU
 * @STA_FLG_AGG_MPDU_DENS_MSK: maximal MPDU density for Tx aggregation
 * @STA_FLG_FAT_EN_MSK: support for channel width (for Tx). This flag is
 *	initialised by driver and can be updated by fw upon reception of
 *	action frames that can change the channel width. When cleared the fw
 *	will send all the frames in 20MHz even when FAT channel is requested.
 * @STA_FLG_MIMO_EN_MSK: support for MIMO. This flag is initialised by the
 *	driver and can be updated by fw upon reception of action frames.
 * @STA_FLG_MFP_EN: Management Frame Protection
 */
enum iwl_sta_flags {
	STA_FLG_REDUCED_TX_PWR_CTRL	= BIT(3),
	STA_FLG_REDUCED_TX_PWR_DATA	= BIT(6),

	STA_FLG_DISABLE_TX		= BIT(4),

	STA_FLG_PS			= BIT(8),
	STA_FLG_DRAIN_FLOW		= BIT(12),
	STA_FLG_PAN			= BIT(13),
	STA_FLG_CLASS_AUTH		= BIT(14),
	STA_FLG_CLASS_ASSOC		= BIT(15),
	STA_FLG_RTS_MIMO_PROT		= BIT(17),

	STA_FLG_MAX_AGG_SIZE_SHIFT	= 19,
	STA_FLG_MAX_AGG_SIZE_8K		= (0 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_16K	= (1 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_32K	= (2 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_64K	= (3 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_128K	= (4 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_256K	= (5 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_512K	= (6 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_1024K	= (7 << STA_FLG_MAX_AGG_SIZE_SHIFT),
	STA_FLG_MAX_AGG_SIZE_MSK	= (7 << STA_FLG_MAX_AGG_SIZE_SHIFT),

	STA_FLG_AGG_MPDU_DENS_SHIFT	= 23,
	STA_FLG_AGG_MPDU_DENS_2US	= (4 << STA_FLG_AGG_MPDU_DENS_SHIFT),
	STA_FLG_AGG_MPDU_DENS_4US	= (5 << STA_FLG_AGG_MPDU_DENS_SHIFT),
	STA_FLG_AGG_MPDU_DENS_8US	= (6 << STA_FLG_AGG_MPDU_DENS_SHIFT),
	STA_FLG_AGG_MPDU_DENS_16US	= (7 << STA_FLG_AGG_MPDU_DENS_SHIFT),
	STA_FLG_AGG_MPDU_DENS_MSK	= (7 << STA_FLG_AGG_MPDU_DENS_SHIFT),

	STA_FLG_FAT_EN_20MHZ		= (0 << 26),
	STA_FLG_FAT_EN_40MHZ		= (1 << 26),
	STA_FLG_FAT_EN_80MHZ		= (2 << 26),
	STA_FLG_FAT_EN_160MHZ		= (3 << 26),
	STA_FLG_FAT_EN_MSK		= (3 << 26),

	STA_FLG_MIMO_EN_SISO		= (0 << 28),
	STA_FLG_MIMO_EN_MIMO2		= (1 << 28),
	STA_FLG_MIMO_EN_MIMO3		= (2 << 28),
	STA_FLG_MIMO_EN_MSK		= (3 << 28),
};

/**
 * enum iwl_sta_key_flag - key flags for the ADD_STA host command
 * @STA_KEY_FLG_NO_ENC: no encryption
 * @STA_KEY_FLG_WEP: WEP encryption algorithm
 * @STA_KEY_FLG_CCM: CCMP encryption algorithm
 * @STA_KEY_FLG_TKIP: TKIP encryption algorithm
 * @STA_KEY_FLG_EXT: extended cipher algorithm (depends on the FW support)
 * @STA_KEY_FLG_GCMP: GCMP encryption algorithm
 * @STA_KEY_FLG_CMAC: CMAC encryption algorithm
 * @STA_KEY_FLG_ENC_UNKNOWN: unknown encryption algorithm
 * @STA_KEY_FLG_EN_MSK: mask for encryption algorithmi value
 * @STA_KEY_FLG_WEP_KEY_MAP: wep is either a group key (0 - legacy WEP) or from
 *	station info array (1 - n 1X mode)
 * @STA_KEY_FLG_KEYID_MSK: the index of the key
 * @STA_KEY_NOT_VALID: key is invalid
 * @STA_KEY_FLG_WEP_13BYTES: set for 13 bytes WEP key
 * @STA_KEY_FLG_KEY_32BYTES for non-wep key set for 32 bytes key
 * @STA_KEY_MULTICAST: set for multical key
 * @STA_KEY_MFP: key is used for Management Frame Protection
 */
enum iwl_sta_key_flag {
	STA_KEY_FLG_NO_ENC		= (0 << 0),
	STA_KEY_FLG_WEP			= (1 << 0),
	STA_KEY_FLG_CCM			= (2 << 0),
	STA_KEY_FLG_TKIP		= (3 << 0),
	STA_KEY_FLG_EXT			= (4 << 0),
	STA_KEY_FLG_GCMP		= (5 << 0),
	STA_KEY_FLG_CMAC		= (6 << 0),
	STA_KEY_FLG_ENC_UNKNOWN		= (7 << 0),
	STA_KEY_FLG_EN_MSK		= (7 << 0),

	STA_KEY_FLG_WEP_KEY_MAP		= BIT(3),
	STA_KEY_FLG_KEYID_POS		 = 8,
	STA_KEY_FLG_KEYID_MSK		= (3 << STA_KEY_FLG_KEYID_POS),
	STA_KEY_NOT_VALID		= BIT(11),
	STA_KEY_FLG_WEP_13BYTES		= BIT(12),
	STA_KEY_FLG_KEY_32BYTES		= BIT(12),
	STA_KEY_MULTICAST		= BIT(14),
	STA_KEY_MFP			= BIT(15),
};

/**
 * enum iwl_sta_modify_flag - indicate to the fw what flag are being changed
 * @STA_MODIFY_QUEUE_REMOVAL: this command removes a queue
 * @STA_MODIFY_TID_DISABLE_TX: this command modifies %tid_disable_tx
 * @STA_MODIFY_UAPSD_ACS: this command modifies %uapsd_acs
 * @STA_MODIFY_ADD_BA_TID: this command modifies %add_immediate_ba_tid
 * @STA_MODIFY_REMOVE_BA_TID: this command modifies %remove_immediate_ba_tid
 * @STA_MODIFY_SLEEPING_STA_TX_COUNT: this command modifies %sleep_tx_count
 * @STA_MODIFY_PROT_TH:
 * @STA_MODIFY_QUEUES: modify the queues used by this station
 */
enum iwl_sta_modify_flag {
	STA_MODIFY_QUEUE_REMOVAL		= BIT(0),
	STA_MODIFY_TID_DISABLE_TX		= BIT(1),
	STA_MODIFY_UAPSD_ACS			= BIT(2),
	STA_MODIFY_ADD_BA_TID			= BIT(3),
	STA_MODIFY_REMOVE_BA_TID		= BIT(4),
	STA_MODIFY_SLEEPING_STA_TX_COUNT	= BIT(5),
	STA_MODIFY_PROT_TH			= BIT(6),
	STA_MODIFY_QUEUES			= BIT(7),
};

/**
 * enum iwl_sta_mode - station command mode
 * @STA_MODE_ADD: add new station
 * @STA_MODE_MODIFY: modify the station
 */
enum iwl_sta_mode {
	STA_MODE_ADD	= 0,
	STA_MODE_MODIFY	= 1,
};

/**
 * enum iwl_sta_sleep_flag - type of sleep of the station
 * @STA_SLEEP_STATE_AWAKE:
 * @STA_SLEEP_STATE_PS_POLL:
 * @STA_SLEEP_STATE_UAPSD:
 * @STA_SLEEP_STATE_MOREDATA: set more-data bit on
 *	(last) released frame
 */
enum iwl_sta_sleep_flag {
	STA_SLEEP_STATE_AWAKE		= 0,
	STA_SLEEP_STATE_PS_POLL		= BIT(0),
	STA_SLEEP_STATE_UAPSD		= BIT(1),
	STA_SLEEP_STATE_MOREDATA	= BIT(2),
};

#define STA_KEY_MAX_NUM (16)
#define STA_KEY_IDX_INVALID (0xff)
#define STA_KEY_MAX_DATA_KEY_NUM (4)
#define IWL_MAX_GLOBAL_KEYS (4)
#define STA_KEY_LEN_WEP40 (5)
#define STA_KEY_LEN_WEP104 (13)

/**
 * struct iwl_mvm_keyinfo - key information
 * @key_flags: type &enum iwl_sta_key_flag
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 * @key_offset: key offset in the fw's key table
 * @key: 16-byte unicast decryption key
 * @tx_secur_seq_cnt: initial RSC / PN needed for replay check
 * @hw_tkip_mic_rx_key: byte: MIC Rx Key - used for TKIP only
 * @hw_tkip_mic_tx_key: byte: MIC Tx Key - used for TKIP only
 */
struct iwl_mvm_keyinfo {
	__le16 key_flags;
	u8 tkip_rx_tsc_byte2;
	u8 reserved1;
	__le16 tkip_rx_ttak[5];
	u8 key_offset;
	u8 reserved2;
	u8 key[16];
	__le64 tx_secur_seq_cnt;
	__le64 hw_tkip_mic_rx_key;
	__le64 hw_tkip_mic_tx_key;
} __packed;

#define IWL_ADD_STA_STATUS_MASK		0xFF
#define IWL_ADD_STA_BAID_VALID_MASK	0x8000
#define IWL_ADD_STA_BAID_MASK		0x7F00
#define IWL_ADD_STA_BAID_SHIFT		8

/**
 * struct iwl_mvm_add_sta_cmd_v7 - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: see &enum iwl_sta_mode
 * @awake_acs:
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %STA_MODIFY_TID_DISABLE_TX to change this field.
 * @mac_id_n_color: the Mac context this station belongs to,
 *	see &enum iwl_mvm_id_and_color
 * @addr: station's MAC address
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @station_flags: look at &enum iwl_sta_flags
 * @station_flags_msk: what of %station_flags have changed,
 *	also &enum iwl_sta_flags
 * @add_immediate_ba_tid: tid for which to add block-ack support (Rx)
 *	Set %STA_MODIFY_ADD_BA_TID to use this field, and also set
 *	add_immediate_ba_ssn.
 * @remove_immediate_ba_tid: tid for which to remove block-ack support (Rx)
 *	Set %STA_MODIFY_REMOVE_BA_TID to use this field
 * @add_immediate_ba_ssn: ssn for the Rx block-ack session. Used together with
 *	add_immediate_ba_tid.
 * @sleep_tx_count: number of packets to transmit to station even though it is
 *	asleep. Used to synchronise PS-poll and u-APSD responses while ucode
 *	keeps track of STA sleep state.
 * @sleep_state_flags: Look at &enum iwl_sta_sleep_flag.
 * @assoc_id: assoc_id to be sent in VHT PLCP (9-bit), for grp use 0, for AP
 *	mac-addr.
 * @beamform_flags: beam forming controls
 * @tfd_queue_msk: tfd queues used by this station
 *
 * The device contains an internal table of per-station information, with info
 * on security keys, aggregation parameters, and Tx rates for initial Tx
 * attempt and any retries (set by REPLY_TX_LINK_QUALITY_CMD).
 *
 * ADD_STA sets up the table entry for one station, either creating a new
 * entry, or modifying a pre-existing one.
 */
struct iwl_mvm_add_sta_cmd_v7 {
	u8 add_modify;
	u8 awake_acs;
	__le16 tid_disable_tx;
	__le32 mac_id_n_color;
	u8 addr[ETH_ALEN];	/* _STA_ID_MODIFY_INFO_API_S_VER_1 */
	__le16 reserved2;
	u8 sta_id;
	u8 modify_mask;
	__le16 reserved3;
	__le32 station_flags;
	__le32 station_flags_msk;
	u8 add_immediate_ba_tid;
	u8 remove_immediate_ba_tid;
	__le16 add_immediate_ba_ssn;
	__le16 sleep_tx_count;
	__le16 sleep_state_flags;
	__le16 assoc_id;
	__le16 beamform_flags;
	__le32 tfd_queue_msk;
} __packed; /* ADD_STA_CMD_API_S_VER_7 */

/**
 * enum iwl_sta_type - FW station types
 * ( REPLY_ADD_STA = 0x18 )
 * @IWL_STA_LINK: Link station - normal RX and TX traffic.
 * @IWL_STA_GENERAL_PURPOSE: General purpose. In AP mode used for beacons
 *	and probe responses.
 * @IWL_STA_MULTICAST: multicast traffic,
 * @IWL_STA_TDLS_LINK: TDLS link station
 * @IWL_STA_AUX_ACTIVITY: auxilary station (scan, ROC and so on).
 */
enum iwl_sta_type {
	IWL_STA_LINK,
	IWL_STA_GENERAL_PURPOSE,
	IWL_STA_MULTICAST,
	IWL_STA_TDLS_LINK,
	IWL_STA_AUX_ACTIVITY,
};

/**
 * struct iwl_mvm_add_sta_cmd - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: see &enum iwl_sta_mode
 * @awake_acs:
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %STA_MODIFY_TID_DISABLE_TX to change this field.
 * @mac_id_n_color: the Mac context this station belongs to,
 *	see &enum iwl_mvm_id_and_color
 * @addr: station's MAC address
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @station_flags: look at &enum iwl_sta_flags
 * @station_flags_msk: what of %station_flags have changed,
 *	also &enum iwl_sta_flags
 * @add_immediate_ba_tid: tid for which to add block-ack support (Rx)
 *	Set %STA_MODIFY_ADD_BA_TID to use this field, and also set
 *	add_immediate_ba_ssn.
 * @remove_immediate_ba_tid: tid for which to remove block-ack support (Rx)
 *	Set %STA_MODIFY_REMOVE_BA_TID to use this field
 * @add_immediate_ba_ssn: ssn for the Rx block-ack session. Used together with
 *	add_immediate_ba_tid.
 * @sleep_tx_count: number of packets to transmit to station even though it is
 *	asleep. Used to synchronise PS-poll and u-APSD responses while ucode
 *	keeps track of STA sleep state.
 * @station_type: type of this station. See &enum iwl_sta_type.
 * @sleep_state_flags: Look at &enum iwl_sta_sleep_flag.
 * @assoc_id: assoc_id to be sent in VHT PLCP (9-bit), for grp use 0, for AP
 *	mac-addr.
 * @beamform_flags: beam forming controls
 * @tfd_queue_msk: tfd queues used by this station.
 *	Obselete for new TX API (9 and above).
 * @rx_ba_window: aggregation window size
 * @sp_length: the size of the SP as it appears in the WME IE
 * @uapsd_acs:  4 LS bits are trigger enabled ACs, 4 MS bits are the deliver
 *	enabled ACs.
 *
 * The device contains an internal table of per-station information, with info
 * on security keys, aggregation parameters, and Tx rates for initial Tx
 * attempt and any retries (set by REPLY_TX_LINK_QUALITY_CMD).
 *
 * ADD_STA sets up the table entry for one station, either creating a new
 * entry, or modifying a pre-existing one.
 */
struct iwl_mvm_add_sta_cmd {
	u8 add_modify;
	u8 awake_acs;
	__le16 tid_disable_tx;
	__le32 mac_id_n_color;
	u8 addr[ETH_ALEN];	/* _STA_ID_MODIFY_INFO_API_S_VER_1 */
	__le16 reserved2;
	u8 sta_id;
	u8 modify_mask;
	__le16 reserved3;
	__le32 station_flags;
	__le32 station_flags_msk;
	u8 add_immediate_ba_tid;
	u8 remove_immediate_ba_tid;
	__le16 add_immediate_ba_ssn;
	__le16 sleep_tx_count;
	u8 sleep_state_flags;
	u8 station_type;
	__le16 assoc_id;
	__le16 beamform_flags;
	__le32 tfd_queue_msk;
	__le16 rx_ba_window;
	u8 sp_length;
	u8 uapsd_acs;
} __packed; /* ADD_STA_CMD_API_S_VER_10 */

/**
 * struct iwl_mvm_add_sta_key_common - add/modify sta key common part
 * ( REPLY_ADD_STA_KEY = 0x17 )
 * @sta_id: index of station in uCode's station table
 * @key_offset: key offset in key storage
 * @key_flags: type &enum iwl_sta_key_flag
 * @key: key material data
 * @rx_secur_seq_cnt: RX security sequence counter for the key
 */
struct iwl_mvm_add_sta_key_common {
	u8 sta_id;
	u8 key_offset;
	__le16 key_flags;
	u8 key[32];
	u8 rx_secur_seq_cnt[16];
} __packed;

/**
 * struct iwl_mvm_add_sta_key_cmd_v1 - add/modify sta key
 * @common: see &struct iwl_mvm_add_sta_key_common
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 */
struct iwl_mvm_add_sta_key_cmd_v1 {
	struct iwl_mvm_add_sta_key_common common;
	u8 tkip_rx_tsc_byte2;
	u8 reserved;
	__le16 tkip_rx_ttak[5];
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_1 */

/**
 * struct iwl_mvm_add_sta_key_cmd - add/modify sta key
 * @common: see &struct iwl_mvm_add_sta_key_common
 * @rx_mic_key: TKIP RX unicast or multicast key
 * @tx_mic_key: TKIP TX key
 * @transmit_seq_cnt: TSC, transmit packet number
 */
struct iwl_mvm_add_sta_key_cmd {
	struct iwl_mvm_add_sta_key_common common;
	__le64 rx_mic_key;
	__le64 tx_mic_key;
	__le64 transmit_seq_cnt;
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_2 */

/**
 * enum iwl_mvm_add_sta_rsp_status - status in the response to ADD_STA command
 * @ADD_STA_SUCCESS: operation was executed successfully
 * @ADD_STA_STATIONS_OVERLOAD: no room left in the fw's station table
 * @ADD_STA_IMMEDIATE_BA_FAILURE: can't add Rx block ack session
 * @ADD_STA_MODIFY_NON_EXISTING_STA: driver requested to modify a station that
 *	doesn't exist.
 */
enum iwl_mvm_add_sta_rsp_status {
	ADD_STA_SUCCESS			= 0x1,
	ADD_STA_STATIONS_OVERLOAD	= 0x2,
	ADD_STA_IMMEDIATE_BA_FAILURE	= 0x4,
	ADD_STA_MODIFY_NON_EXISTING_STA	= 0x8,
};

/**
 * struct iwl_mvm_rm_sta_cmd - Add / modify a station in the fw's station table
 * ( REMOVE_STA = 0x19 )
 * @sta_id: the station id of the station to be removed
 */
struct iwl_mvm_rm_sta_cmd {
	u8 sta_id;
	u8 reserved[3];
} __packed; /* REMOVE_STA_CMD_API_S_VER_2 */

/**
 * struct iwl_mvm_mgmt_mcast_key_cmd_v1
 * ( MGMT_MCAST_KEY = 0x1f )
 * @ctrl_flags: &enum iwl_sta_key_flag
 * @igtk:
 * @k1: unused
 * @k2: unused
 * @sta_id: station ID that support IGTK
 * @key_id:
 * @receive_seq_cnt: initial RSC/PN needed for replay check
 */
struct iwl_mvm_mgmt_mcast_key_cmd_v1 {
	__le32 ctrl_flags;
	u8 igtk[16];
	u8 k1[16];
	u8 k2[16];
	__le32 key_id;
	__le32 sta_id;
	__le64 receive_seq_cnt;
} __packed; /* SEC_MGMT_MULTICAST_KEY_CMD_API_S_VER_1 */

/**
 * struct iwl_mvm_mgmt_mcast_key_cmd
 * ( MGMT_MCAST_KEY = 0x1f )
 * @ctrl_flags: &enum iwl_sta_key_flag
 * @igtk: IGTK master key
 * @sta_id: station ID that support IGTK
 * @key_id:
 * @receive_seq_cnt: initial RSC/PN needed for replay check
 */
struct iwl_mvm_mgmt_mcast_key_cmd {
	__le32 ctrl_flags;
	u8 igtk[32];
	__le32 key_id;
	__le32 sta_id;
	__le64 receive_seq_cnt;
} __packed; /* SEC_MGMT_MULTICAST_KEY_CMD_API_S_VER_2 */

struct iwl_mvm_wep_key {
	u8 key_index;
	u8 key_offset;
	__le16 reserved1;
	u8 key_size;
	u8 reserved2[3];
	u8 key[16];
} __packed;

struct iwl_mvm_wep_key_cmd {
	__le32 mac_id_n_color;
	u8 num_keys;
	u8 decryption_type;
	u8 flags;
	u8 reserved;
	struct iwl_mvm_wep_key wep_key[0];
} __packed; /* SEC_CURR_WEP_KEY_CMD_API_S_VER_2 */

/**
 * struct iwl_mvm_eosp_notification - EOSP notification from firmware
 * @remain_frame_count: # of frames remaining, non-zero if SP was cut
 *	short by GO absence
 * @sta_id: station ID
 */
struct iwl_mvm_eosp_notification {
	__le32 remain_frame_count;
	__le32 sta_id;
} __packed; /* UAPSD_EOSP_NTFY_API_S_VER_1 */

#endif /* __fw_api_sta_h__ */
