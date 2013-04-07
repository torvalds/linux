/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2012 - 2013 Intel Corporation. All rights reserved.
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
 * @STA_FLG_FLG_ANT_MSK: Antenna selection
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

	STA_FLG_FLG_ANT_A		= (1 << 4),
	STA_FLG_FLG_ANT_B		= (2 << 4),
	STA_FLG_FLG_ANT_MSK		= (STA_FLG_FLG_ANT_A |
					   STA_FLG_FLG_ANT_B),

	STA_FLG_PS			= BIT(8),
	STA_FLG_INVALID			= BIT(9),
	STA_FLG_DLP_EN			= BIT(10),
	STA_FLG_SET_ALL_KEYS		= BIT(11),
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
 * @STA_KEY_FLG_EN_MSK: mask for encryption algorithm
 * @STA_KEY_FLG_WEP_KEY_MAP: wep is either a group key (0 - legacy WEP) or from
 *	station info array (1 - n 1X mode)
 * @STA_KEY_FLG_KEYID_MSK: the index of the key
 * @STA_KEY_NOT_VALID: key is invalid
 * @STA_KEY_FLG_WEP_13BYTES: set for 13 bytes WEP key
 * @STA_KEY_MULTICAST: set for multical key
 * @STA_KEY_MFP: key is used for Management Frame Protection
 */
enum iwl_sta_key_flag {
	STA_KEY_FLG_NO_ENC		= (0 << 0),
	STA_KEY_FLG_WEP			= (1 << 0),
	STA_KEY_FLG_CCM			= (2 << 0),
	STA_KEY_FLG_TKIP		= (3 << 0),
	STA_KEY_FLG_CMAC		= (6 << 0),
	STA_KEY_FLG_ENC_UNKNOWN		= (7 << 0),
	STA_KEY_FLG_EN_MSK		= (7 << 0),

	STA_KEY_FLG_WEP_KEY_MAP		= BIT(3),
	STA_KEY_FLG_KEYID_POS		 = 8,
	STA_KEY_FLG_KEYID_MSK		= (3 << STA_KEY_FLG_KEYID_POS),
	STA_KEY_NOT_VALID		= BIT(11),
	STA_KEY_FLG_WEP_13BYTES		= BIT(12),
	STA_KEY_MULTICAST		= BIT(14),
	STA_KEY_MFP			= BIT(15),
};

/**
 * enum iwl_sta_modify_flag - indicate to the fw what flag are being changed
 * @STA_MODIFY_KEY: this command modifies %key
 * @STA_MODIFY_TID_DISABLE_TX: this command modifies %tid_disable_tx
 * @STA_MODIFY_TX_RATE: unused
 * @STA_MODIFY_ADD_BA_TID: this command modifies %add_immediate_ba_tid
 * @STA_MODIFY_REMOVE_BA_TID: this command modifies %remove_immediate_ba_tid
 * @STA_MODIFY_SLEEPING_STA_TX_COUNT: this command modifies %sleep_tx_count
 * @STA_MODIFY_PROT_TH:
 * @STA_MODIFY_QUEUES: modify the queues used by this station
 */
enum iwl_sta_modify_flag {
	STA_MODIFY_KEY				= BIT(0),
	STA_MODIFY_TID_DISABLE_TX		= BIT(1),
	STA_MODIFY_TX_RATE			= BIT(2),
	STA_MODIFY_ADD_BA_TID			= BIT(3),
	STA_MODIFY_REMOVE_BA_TID		= BIT(4),
	STA_MODIFY_SLEEPING_STA_TX_COUNT	= BIT(5),
	STA_MODIFY_PROT_TH			= BIT(6),
	STA_MODIFY_QUEUES			= BIT(7),
};

#define STA_MODE_MODIFY	1

/**
 * enum iwl_sta_sleep_flag - type of sleep of the station
 * @STA_SLEEP_STATE_AWAKE:
 * @STA_SLEEP_STATE_PS_POLL:
 * @STA_SLEEP_STATE_UAPSD:
 */
enum iwl_sta_sleep_flag {
	STA_SLEEP_STATE_AWAKE	= 0,
	STA_SLEEP_STATE_PS_POLL	= BIT(0),
	STA_SLEEP_STATE_UAPSD	= BIT(1),
};

/* STA ID and color bits definitions */
#define STA_ID_SEED		(0x0f)
#define STA_ID_POS		(0)
#define STA_ID_MSK		(STA_ID_SEED << STA_ID_POS)

#define STA_COLOR_SEED		(0x7)
#define STA_COLOR_POS		(4)
#define STA_COLOR_MSK		(STA_COLOR_SEED << STA_COLOR_POS)

#define STA_ID_N_COLOR_GET_COLOR(id_n_color) \
	(((id_n_color) & STA_COLOR_MSK) >> STA_COLOR_POS)
#define STA_ID_N_COLOR_GET_ID(id_n_color)    \
	(((id_n_color) & STA_ID_MSK) >> STA_ID_POS)

#define STA_KEY_MAX_NUM (16)
#define STA_KEY_IDX_INVALID (0xff)
#define STA_KEY_MAX_DATA_KEY_NUM (4)
#define IWL_MAX_GLOBAL_KEYS (4)
#define STA_KEY_LEN_WEP40 (5)
#define STA_KEY_LEN_WEP104 (13)

/**
 * struct iwl_mvm_keyinfo - key information
 * @key_flags: type %iwl_sta_key_flag
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

/**
 * struct iwl_mvm_add_sta_cmd_v5 - Add/modify a station in the fw's sta table.
 * ( REPLY_ADD_STA = 0x18 )
 * @add_modify: 1: modify existing, 0: add new station
 * @unicast_tx_key_id: unicast tx key id. Relevant only when unicast key sent
 * @multicast_tx_key_id: multicast tx key id. Relevant only when multicast key
 *	sent
 * @mac_id_n_color: the Mac context this station belongs to
 * @addr[ETH_ALEN]: station's MAC address
 * @sta_id: index of station in uCode's station table
 * @modify_mask: STA_MODIFY_*, selects which parameters to modify vs. leave
 *	alone. 1 - modify, 0 - don't change.
 * @key: look at %iwl_mvm_keyinfo
 * @station_flags: look at %iwl_sta_flags
 * @station_flags_msk: what of %station_flags have changed
 * @tid_disable_tx: is tid BIT(tid) enabled for Tx. Clear BIT(x) to enable
 *	AMPDU for tid x. Set %STA_MODIFY_TID_DISABLE_TX to change this field.
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
 * @sleep_state_flags: Look at %iwl_sta_sleep_flag.
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
struct iwl_mvm_add_sta_cmd_v5 {
	u8 add_modify;
	u8 unicast_tx_key_id;
	u8 multicast_tx_key_id;
	u8 reserved1;
	__le32 mac_id_n_color;
	u8 addr[ETH_ALEN];
	__le16 reserved2;
	u8 sta_id;
	u8 modify_mask;
	__le16 reserved3;
	struct iwl_mvm_keyinfo key;
	__le32 station_flags;
	__le32 station_flags_msk;
	__le16 tid_disable_tx;
	__le16 reserved4;
	u8 add_immediate_ba_tid;
	u8 remove_immediate_ba_tid;
	__le16 add_immediate_ba_ssn;
	__le16 sleep_tx_count;
	__le16 sleep_state_flags;
	__le16 assoc_id;
	__le16 beamform_flags;
	__le32 tfd_queue_msk;
} __packed; /* ADD_STA_CMD_API_S_VER_5 */

/**
 * struct iwl_mvm_add_sta_cmd_v6 - Add / modify a station
 * VER_6 of this command is quite similar to VER_5 except
 * exclusion of all fields related to the security key installation.
 */
struct iwl_mvm_add_sta_cmd_v6 {
	u8 add_modify;
	u8 reserved1;
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
} __packed; /* ADD_STA_CMD_API_S_VER_6 */

/**
 * struct iwl_mvm_add_sta_key_cmd - add/modify sta key
 * ( REPLY_ADD_STA_KEY = 0x17 )
 * @sta_id: index of station in uCode's station table
 * @key_offset: key offset in key storage
 * @key_flags: type %iwl_sta_key_flag
 * @key: key material data
 * @key2: key material data
 * @rx_secur_seq_cnt: RX security sequence counter for the key
 * @tkip_rx_tsc_byte2: TSC[2] for key mix ph1 detection
 * @tkip_rx_ttak: 10-byte unicast TKIP TTAK for Rx
 */
struct iwl_mvm_add_sta_key_cmd {
	u8 sta_id;
	u8 key_offset;
	__le16 key_flags;
	u8 key[16];
	u8 key2[16];
	u8 rx_secur_seq_cnt[16];
	u8 tkip_rx_tsc_byte2;
	u8 reserved;
	__le16 tkip_rx_ttak[5];
} __packed; /* ADD_MODIFY_STA_KEY_API_S_VER_1 */

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
 * struct iwl_mvm_mgmt_mcast_key_cmd
 * ( MGMT_MCAST_KEY = 0x1f )
 * @ctrl_flags: %iwl_sta_key_flag
 * @IGTK:
 * @K1: IGTK master key
 * @K2: IGTK sub key
 * @sta_id: station ID that support IGTK
 * @key_id:
 * @receive_seq_cnt: initial RSC/PN needed for replay check
 */
struct iwl_mvm_mgmt_mcast_key_cmd {
	__le32 ctrl_flags;
	u8 IGTK[16];
	u8 K1[16];
	u8 K2[16];
	__le32 key_id;
	__le32 sta_id;
	__le64 receive_seq_cnt;
} __packed; /* SEC_MGMT_MULTICAST_KEY_CMD_API_S_VER_1 */

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


#endif /* __fw_api_sta_h__ */
