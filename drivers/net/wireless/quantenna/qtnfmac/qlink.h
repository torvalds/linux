/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _QTN_QLINK_H_
#define _QTN_QLINK_H_

#include <linux/ieee80211.h>

#define QLINK_PROTO_VER		5

#define QLINK_MACID_RSVD		0xFF
#define QLINK_VIFID_RSVD		0xFF

/* Common QLINK protocol messages definitions.
 */

/**
 * enum qlink_msg_type - QLINK message types
 *
 * Used to distinguish between message types of QLINK protocol.
 *
 * @QLINK_MSG_TYPE_CMD: Message is carrying data of a command sent from
 *	driver to wireless hardware.
 * @QLINK_MSG_TYPE_CMDRSP: Message is carrying data of a response to a command.
 *	Sent from wireless HW to driver in reply to previously issued command.
 * @QLINK_MSG_TYPE_EVENT: Data for an event originated in wireless hardware and
 *	sent asynchronously to driver.
 */
enum qlink_msg_type {
	QLINK_MSG_TYPE_CMD	= 1,
	QLINK_MSG_TYPE_CMDRSP	= 2,
	QLINK_MSG_TYPE_EVENT	= 3
};

/**
 * struct qlink_msg_header - common QLINK protocol message header
 *
 * Portion of QLINK protocol header common for all message types.
 *
 * @type: Message type, one of &enum qlink_msg_type.
 * @len: Total length of message including all headers.
 */
struct qlink_msg_header {
	__le16 type;
	__le16 len;
} __packed;

/* Generic definitions of data and information carried in QLINK messages
 */

enum qlink_hw_capab {
	QLINK_HW_SUPPORTS_REG_UPDATE	= BIT(0),
};

enum qlink_phy_mode {
	QLINK_PHYMODE_BGN	= BIT(0),
	QLINK_PHYMODE_AN	= BIT(1),
	QLINK_PHYMODE_AC	= BIT(2),
};

enum qlink_iface_type {
	QLINK_IFTYPE_AP		= 1,
	QLINK_IFTYPE_STATION	= 2,
	QLINK_IFTYPE_ADHOC	= 3,
	QLINK_IFTYPE_MONITOR	= 4,
	QLINK_IFTYPE_WDS	= 5,
	QLINK_IFTYPE_AP_VLAN	= 6,
};

/**
 * struct qlink_intf_info - information on virtual interface.
 *
 * Data describing a single virtual interface.
 *
 * @if_type: Mode of interface operation, one of &enum qlink_iface_type
 * @vlanid: VLAN ID for AP_VLAN interface type
 * @mac_addr: MAC address of virtual interface.
 */
struct qlink_intf_info {
	__le16 if_type;
	__le16 vlanid;
	u8 mac_addr[ETH_ALEN];
	u8 rsvd[2];
} __packed;

enum qlink_sta_flags {
	QLINK_STA_FLAG_INVALID		= 0,
	QLINK_STA_FLAG_AUTHORIZED		= BIT(0),
	QLINK_STA_FLAG_SHORT_PREAMBLE	= BIT(1),
	QLINK_STA_FLAG_WME			= BIT(2),
	QLINK_STA_FLAG_MFP			= BIT(3),
	QLINK_STA_FLAG_AUTHENTICATED		= BIT(4),
	QLINK_STA_FLAG_TDLS_PEER		= BIT(5),
	QLINK_STA_FLAG_ASSOCIATED		= BIT(6),
};

enum qlink_channel_width {
	QLINK_CHAN_WIDTH_5		= BIT(0),
	QLINK_CHAN_WIDTH_10		= BIT(1),
	QLINK_CHAN_WIDTH_20_NOHT	= BIT(2),
	QLINK_CHAN_WIDTH_20		= BIT(3),
	QLINK_CHAN_WIDTH_40		= BIT(4),
	QLINK_CHAN_WIDTH_80		= BIT(5),
	QLINK_CHAN_WIDTH_80P80		= BIT(6),
	QLINK_CHAN_WIDTH_160		= BIT(7),
};

/* QLINK Command messages related definitions
 */

/**
 * enum qlink_cmd_type - list of supported commands
 *
 * Commands are QLINK messages of type @QLINK_MSG_TYPE_CMD, sent by driver to
 * wireless network device for processing. Device is expected to send back a
 * reply message of type &QLINK_MSG_TYPE_CMDRSP, containing at least command
 * execution status (one of &enum qlink_cmd_result) at least. Reply message
 * may also contain data payload specific to the command type.
 *
 * @QLINK_CMD_CHANS_INFO_GET: for the specified MAC and specified band, get
 *	number of operational channels and information on each of the channel.
 *	This command is generic to a specified MAC, interface index must be set
 *	to QLINK_VIFID_RSVD in command header.
 * @QLINK_CMD_REG_NOTIFY: notify device about regulatory domain change. This
 *	command is supported only if device reports QLINK_HW_SUPPORTS_REG_UPDATE
 *	capability.
 */
enum qlink_cmd_type {
	QLINK_CMD_FW_INIT		= 0x0001,
	QLINK_CMD_FW_DEINIT		= 0x0002,
	QLINK_CMD_REGISTER_MGMT		= 0x0003,
	QLINK_CMD_SEND_MGMT_FRAME	= 0x0004,
	QLINK_CMD_MGMT_SET_APPIE	= 0x0005,
	QLINK_CMD_PHY_PARAMS_GET	= 0x0011,
	QLINK_CMD_PHY_PARAMS_SET	= 0x0012,
	QLINK_CMD_GET_HW_INFO		= 0x0013,
	QLINK_CMD_MAC_INFO		= 0x0014,
	QLINK_CMD_ADD_INTF		= 0x0015,
	QLINK_CMD_DEL_INTF		= 0x0016,
	QLINK_CMD_CHANGE_INTF		= 0x0017,
	QLINK_CMD_UPDOWN_INTF		= 0x0018,
	QLINK_CMD_REG_NOTIFY		= 0x0019,
	QLINK_CMD_CHANS_INFO_GET	= 0x001A,
	QLINK_CMD_CHAN_SWITCH		= 0x001B,
	QLINK_CMD_CONFIG_AP		= 0x0020,
	QLINK_CMD_START_AP		= 0x0021,
	QLINK_CMD_STOP_AP		= 0x0022,
	QLINK_CMD_GET_STA_INFO		= 0x0030,
	QLINK_CMD_ADD_KEY		= 0x0040,
	QLINK_CMD_DEL_KEY		= 0x0041,
	QLINK_CMD_SET_DEFAULT_KEY	= 0x0042,
	QLINK_CMD_SET_DEFAULT_MGMT_KEY	= 0x0043,
	QLINK_CMD_CHANGE_STA		= 0x0051,
	QLINK_CMD_DEL_STA		= 0x0052,
	QLINK_CMD_SCAN			= 0x0053,
	QLINK_CMD_CHAN_STATS		= 0x0054,
	QLINK_CMD_CONNECT		= 0x0060,
	QLINK_CMD_DISCONNECT		= 0x0061,
};

/**
 * struct qlink_cmd - QLINK command message header
 *
 * Header used for QLINK messages of QLINK_MSG_TYPE_CMD type.
 *
 * @mhdr: Common QLINK message header.
 * @cmd_id: command id, one of &enum qlink_cmd_type.
 * @seq_num: sequence number of command message, used for matching with
 *	response message.
 * @macid: index of physical radio device the command is destined to or
 *	QLINK_MACID_RSVD if not applicable.
 * @vifid: index of virtual wireless interface on specified @macid the command
 *	is destined to or QLINK_VIFID_RSVD if not applicable.
 */
struct qlink_cmd {
	struct qlink_msg_header mhdr;
	__le16 cmd_id;
	__le16 seq_num;
	u8 rsvd[2];
	u8 macid;
	u8 vifid;
} __packed;

/**
 * struct qlink_cmd_manage_intf - interface management command
 *
 * Data for interface management commands QLINK_CMD_ADD_INTF, QLINK_CMD_DEL_INTF
 * and QLINK_CMD_CHANGE_INTF.
 *
 * @intf_info: interface description.
 */
struct qlink_cmd_manage_intf {
	struct qlink_cmd chdr;
	struct qlink_intf_info intf_info;
} __packed;

enum qlink_mgmt_frame_type {
	QLINK_MGMT_FRAME_ASSOC_REQ	= 0x00,
	QLINK_MGMT_FRAME_ASSOC_RESP	= 0x01,
	QLINK_MGMT_FRAME_REASSOC_REQ	= 0x02,
	QLINK_MGMT_FRAME_REASSOC_RESP	= 0x03,
	QLINK_MGMT_FRAME_PROBE_REQ	= 0x04,
	QLINK_MGMT_FRAME_PROBE_RESP	= 0x05,
	QLINK_MGMT_FRAME_BEACON		= 0x06,
	QLINK_MGMT_FRAME_ATIM		= 0x07,
	QLINK_MGMT_FRAME_DISASSOC	= 0x08,
	QLINK_MGMT_FRAME_AUTH		= 0x09,
	QLINK_MGMT_FRAME_DEAUTH		= 0x0A,
	QLINK_MGMT_FRAME_ACTION		= 0x0B,

	QLINK_MGMT_FRAME_TYPE_COUNT
};

/**
 * struct qlink_cmd_mgmt_frame_register - data for QLINK_CMD_REGISTER_MGMT
 *
 * @frame_type: MGMT frame type the registration request describes, one of
 *	&enum qlink_mgmt_frame_type.
 * @do_register: 0 - unregister, otherwise register for reception of specified
 *	MGMT frame type.
 */
struct qlink_cmd_mgmt_frame_register {
	struct qlink_cmd chdr;
	__le16 frame_type;
	u8 do_register;
} __packed;

enum qlink_mgmt_frame_tx_flags {
	QLINK_MGMT_FRAME_TX_FLAG_NONE		= 0,
	QLINK_MGMT_FRAME_TX_FLAG_OFFCHAN	= BIT(0),
	QLINK_MGMT_FRAME_TX_FLAG_NO_CCK		= BIT(1),
	QLINK_MGMT_FRAME_TX_FLAG_ACK_NOWAIT	= BIT(2),
};

/**
 * struct qlink_cmd_mgmt_frame_tx - data for QLINK_CMD_SEND_MGMT_FRAME command
 *
 * @cookie: opaque request identifier.
 * @freq: Frequency to use for frame transmission.
 * @flags: Transmission flags, one of &enum qlink_mgmt_frame_tx_flags.
 * @frame_data: frame to transmit.
 */
struct qlink_cmd_mgmt_frame_tx {
	struct qlink_cmd chdr;
	__le32 cookie;
	__le16 freq;
	__le16 flags;
	u8 frame_data[0];
} __packed;

/**
 * struct qlink_cmd_mgmt_append_ie - data for QLINK_CMD_MGMT_SET_APPIE command
 *
 * @type: type of MGMT frame to appent requested IEs to, one of
 *	&enum qlink_mgmt_frame_type.
 * @flags: for future use.
 * @ie_data: IEs data to append.
 */
struct qlink_cmd_mgmt_append_ie {
	struct qlink_cmd chdr;
	u8 type;
	u8 flags;
	u8 ie_data[0];
} __packed;

/**
 * struct qlink_cmd_get_sta_info - data for QLINK_CMD_GET_STA_INFO command
 *
 * @sta_addr: MAC address of the STA statistics is requested for.
 */
struct qlink_cmd_get_sta_info {
	struct qlink_cmd chdr;
	u8 sta_addr[ETH_ALEN];
} __packed;

/**
 * struct qlink_cmd_add_key - data for QLINK_CMD_ADD_KEY command.
 *
 * @key_index: index of the key being installed.
 * @pairwise: whether to use pairwise key.
 * @addr: MAC address of a STA key is being installed to.
 * @cipher: cipher suite.
 * @vlanid: VLAN ID for AP_VLAN interface type
 * @key_data: key data itself.
 */
struct qlink_cmd_add_key {
	struct qlink_cmd chdr;
	u8 key_index;
	u8 pairwise;
	u8 addr[ETH_ALEN];
	__le32 cipher;
	__le16 vlanid;
	u8 key_data[0];
} __packed;

/**
 * struct qlink_cmd_del_key_req - data for QLINK_CMD_DEL_KEY command
 *
 * @key_index: index of the key being removed.
 * @pairwise: whether to use pairwise key.
 * @addr: MAC address of a STA for which a key is removed.
 */
struct qlink_cmd_del_key {
	struct qlink_cmd chdr;
	u8 key_index;
	u8 pairwise;
	u8 addr[ETH_ALEN];
} __packed;

/**
 * struct qlink_cmd_set_def_key - data for QLINK_CMD_SET_DEFAULT_KEY command
 *
 * @key_index: index of the key to be set as default one.
 * @unicast: key is unicast.
 * @multicast: key is multicast.
 */
struct qlink_cmd_set_def_key {
	struct qlink_cmd chdr;
	u8 key_index;
	u8 unicast;
	u8 multicast;
} __packed;

/**
 * struct qlink_cmd_set_def_mgmt_key - data for QLINK_CMD_SET_DEFAULT_MGMT_KEY
 *
 * @key_index: index of the key to be set as default MGMT key.
 */
struct qlink_cmd_set_def_mgmt_key {
	struct qlink_cmd chdr;
	u8 key_index;
} __packed;

/**
 * struct qlink_cmd_change_sta - data for QLINK_CMD_CHANGE_STA command
 *
 * @sta_flags_mask: STA flags mask, bitmap of &enum qlink_sta_flags
 * @sta_flags_set: STA flags values, bitmap of &enum qlink_sta_flags
 * @if_type: Mode of interface operation, one of &enum qlink_iface_type
 * @vlanid: VLAN ID to assign to specific STA
 * @sta_addr: address of the STA for which parameters are set.
 */
struct qlink_cmd_change_sta {
	struct qlink_cmd chdr;
	__le32 sta_flags_mask;
	__le32 sta_flags_set;
	__le16 if_type;
	__le16 vlanid;
	u8 sta_addr[ETH_ALEN];
} __packed;

/**
 * struct qlink_cmd_del_sta - data for QLINK_CMD_DEL_STA command.
 *
 * See &struct station_del_parameters
 */
struct qlink_cmd_del_sta {
	struct qlink_cmd chdr;
	__le16 reason_code;
	u8 subtype;
	u8 sta_addr[ETH_ALEN];
} __packed;

enum qlink_sta_connect_flags {
	QLINK_STA_CONNECT_DISABLE_HT	= BIT(0),
	QLINK_STA_CONNECT_DISABLE_VHT	= BIT(1),
	QLINK_STA_CONNECT_USE_RRM	= BIT(2),
};

/**
 * struct qlink_cmd_connect - data for QLINK_CMD_CONNECT command
 *
 * @flags: for future use.
 * @freq: center frequence of a channel which should be used to connect.
 * @bg_scan_period: period of background scan.
 * @bssid: BSSID of the BSS to connect to.
 * @payload: variable portion of connection request.
 */
struct qlink_cmd_connect {
	struct qlink_cmd chdr;
	__le32 flags;
	__le16 channel;
	__le16 bg_scan_period;
	u8 bssid[ETH_ALEN];
	u8 payload[0];
} __packed;

/**
 * struct qlink_cmd_disconnect - data for QLINK_CMD_DISCONNECT command
 *
 * @reason: code of the reason of disconnect, see &enum ieee80211_reasoncode.
 */
struct qlink_cmd_disconnect {
	struct qlink_cmd chdr;
	__le16 reason;
} __packed;

/**
 * struct qlink_cmd_updown - data for QLINK_CMD_UPDOWN_INTF command
 *
 * @if_up: bring specified interface DOWN (if_up==0) or UP (otherwise).
 *	Interface is specified in common command header @chdr.
 */
struct qlink_cmd_updown {
	struct qlink_cmd chdr;
	u8 if_up;
} __packed;

/**
 * enum qlink_band - a list of frequency bands
 *
 * @QLINK_BAND_2GHZ: 2.4GHz band
 * @QLINK_BAND_5GHZ: 5GHz band
 * @QLINK_BAND_60GHZ: 60GHz band
 */
enum qlink_band {
	QLINK_BAND_2GHZ = BIT(0),
	QLINK_BAND_5GHZ = BIT(1),
	QLINK_BAND_60GHZ = BIT(2),
};

/**
 * struct qlink_cmd_chans_info_get - data for QLINK_CMD_CHANS_INFO_GET command
 *
 * @band: a PHY band for which channels info is needed, one of @enum qlink_band
 */
struct qlink_cmd_chans_info_get {
	struct qlink_cmd chdr;
	u8 band;
} __packed;

/**
 * struct qlink_cmd_get_chan_stats - data for QLINK_CMD_CHAN_STATS command
 *
 * @channel: channel number according to 802.11 17.3.8.3.2 and Annex J
 */
struct qlink_cmd_get_chan_stats {
	struct qlink_cmd chdr;
	__le16 channel;
} __packed;

/**
 * enum qlink_reg_initiator - Indicates the initiator of a reg domain request
 *
 * See &enum nl80211_reg_initiator for more info.
 */
enum qlink_reg_initiator {
	QLINK_REGDOM_SET_BY_CORE,
	QLINK_REGDOM_SET_BY_USER,
	QLINK_REGDOM_SET_BY_DRIVER,
	QLINK_REGDOM_SET_BY_COUNTRY_IE,
};

/**
 * enum qlink_user_reg_hint_type - type of user regulatory hint
 *
 * See &enum nl80211_user_reg_hint_type for more info.
 */
enum qlink_user_reg_hint_type {
	QLINK_USER_REG_HINT_USER	= 0,
	QLINK_USER_REG_HINT_CELL_BASE	= 1,
	QLINK_USER_REG_HINT_INDOOR	= 2,
};

/**
 * struct qlink_cmd_reg_notify - data for QLINK_CMD_REG_NOTIFY command
 *
 * @alpha2: the ISO / IEC 3166 alpha2 country code.
 * @initiator: which entity sent the request, one of &enum qlink_reg_initiator.
 * @user_reg_hint_type: type of hint for QLINK_REGDOM_SET_BY_USER request, one
 *	of &enum qlink_user_reg_hint_type.
 */
struct qlink_cmd_reg_notify {
	struct qlink_cmd chdr;
	u8 alpha2[2];
	u8 initiator;
	u8 user_reg_hint_type;
} __packed;

/**
 * struct qlink_cmd_chan_switch - data for QLINK_CMD_CHAN_SWITCH command
 *
 * @channel: channel number according to 802.11 17.3.8.3.2 and Annex J
 * @radar_required: whether radar detection is required on the new channel
 * @block_tx: whether transmissions should be blocked while changing
 * @beacon_count: number of beacons until switch
 */
struct qlink_cmd_chan_switch {
	struct qlink_cmd chdr;
	__le16 channel;
	u8 radar_required;
	u8 block_tx;
	u8 beacon_count;
} __packed;

/* QLINK Command Responses messages related definitions
 */

enum qlink_cmd_result {
	QLINK_CMD_RESULT_OK = 0,
	QLINK_CMD_RESULT_INVALID,
	QLINK_CMD_RESULT_ENOTSUPP,
	QLINK_CMD_RESULT_ENOTFOUND,
	QLINK_CMD_RESULT_EALREADY,
};

/**
 * struct qlink_resp - QLINK command response message header
 *
 * Header used for QLINK messages of QLINK_MSG_TYPE_CMDRSP type.
 *
 * @mhdr: see &struct qlink_msg_header.
 * @cmd_id: command ID the response corresponds to, one of &enum qlink_cmd_type.
 * @seq_num: sequence number of command message, used for matching with
 *	response message.
 * @result: result of the command execution, one of &enum qlink_cmd_result.
 * @macid: index of physical radio device the response is sent from or
 *	QLINK_MACID_RSVD if not applicable.
 * @vifid: index of virtual wireless interface on specified @macid the response
 *	is sent from or QLINK_VIFID_RSVD if not applicable.
 */
struct qlink_resp {
	struct qlink_msg_header mhdr;
	__le16 cmd_id;
	__le16 seq_num;
	__le16 result;
	u8 macid;
	u8 vifid;
} __packed;

/**
 * struct qlink_resp_get_mac_info - response for QLINK_CMD_MAC_INFO command
 *
 * Data describing specific physical device providing wireless MAC
 * functionality.
 *
 * @dev_mac: MAC address of physical WMAC device (used for first BSS on
 *	specified WMAC).
 * @num_tx_chain: Number of transmit chains used by WMAC.
 * @num_rx_chain: Number of receive chains used by WMAC.
 * @vht_cap: VHT capabilities.
 * @ht_cap: HT capabilities.
 * @bands_cap: wireless bands WMAC can operate in, bitmap of &enum qlink_band.
 * @phymode_cap: PHY modes WMAC can operate in, bitmap of &enum qlink_phy_mode.
 * @max_ap_assoc_sta: Maximum number of associations supported by WMAC.
 * @radar_detect_widths: bitmask of channels BW for which WMAC can detect radar.
 * @var_info: variable-length WMAC info data.
 */
struct qlink_resp_get_mac_info {
	struct qlink_resp rhdr;
	u8 dev_mac[ETH_ALEN];
	u8 num_tx_chain;
	u8 num_rx_chain;
	struct ieee80211_vht_cap vht_cap;
	struct ieee80211_ht_cap ht_cap;
	u8 bands_cap;
	u8 phymode_cap;
	__le16 max_ap_assoc_sta;
	__le16 radar_detect_widths;
	u8 var_info[0];
} __packed;

/**
 * enum qlink_dfs_regions - regulatory DFS regions
 *
 * Corresponds to &enum nl80211_dfs_regions.
 */
enum qlink_dfs_regions {
	QLINK_DFS_UNSET	= 0,
	QLINK_DFS_FCC	= 1,
	QLINK_DFS_ETSI	= 2,
	QLINK_DFS_JP	= 3,
};

/**
 * struct qlink_resp_get_hw_info - response for QLINK_CMD_GET_HW_INFO command
 *
 * Description of wireless hardware capabilities and features.
 *
 * @fw_ver: wireless hardware firmware version.
 * @hw_capab: Bitmap of capabilities supported by firmware.
 * @ql_proto_ver: Version of QLINK protocol used by firmware.
 * @num_mac: Number of separate physical radio devices provided by hardware.
 * @mac_bitmap: Bitmap of MAC IDs that are active and can be used in firmware.
 * @total_tx_chains: total number of transmit chains used by device.
 * @total_rx_chains: total number of receive chains.
 * @alpha2: country code ID firmware is configured to.
 * @n_reg_rules: number of regulatory rules TLVs in variable portion of the
 *	message.
 * @dfs_region: regulatory DFS region, one of @enum qlink_dfs_region.
 * @info: variable-length HW info, can contain QTN_TLV_ID_REG_RULE.
 */
struct qlink_resp_get_hw_info {
	struct qlink_resp rhdr;
	__le32 fw_ver;
	__le32 hw_capab;
	__le16 ql_proto_ver;
	u8 num_mac;
	u8 mac_bitmap;
	u8 total_tx_chain;
	u8 total_rx_chain;
	u8 alpha2[2];
	u8 n_reg_rules;
	u8 dfs_region;
	u8 info[0];
} __packed;

/**
 * struct qlink_resp_manage_intf - response for interface management commands
 *
 * Response data for QLINK_CMD_ADD_INTF and QLINK_CMD_CHANGE_INTF commands.
 *
 * @rhdr: Common Command Response message header.
 * @intf_info: interface description.
 */
struct qlink_resp_manage_intf {
	struct qlink_resp rhdr;
	struct qlink_intf_info intf_info;
} __packed;

/**
 * struct qlink_resp_get_sta_info - response for QLINK_CMD_GET_STA_INFO command
 *
 * Response data containing statistics for specified STA.
 *
 * @sta_addr: MAC address of STA the response carries statistic for.
 * @info: statistics for specified STA.
 */
struct qlink_resp_get_sta_info {
	struct qlink_resp rhdr;
	u8 sta_addr[ETH_ALEN];
	u8 info[0];
} __packed;

/**
 * struct qlink_resp_get_chan_info - response for QLINK_CMD_CHANS_INFO_GET cmd
 *
 * @band: frequency band to which channels belong to, one of @enum qlink_band.
 * @num_chans: total number of channels info data contained in reply data.
 * @info: variable-length channels info.
 */
struct qlink_resp_get_chan_info {
	struct qlink_resp rhdr;
	u8 band;
	u8 num_chans;
	u8 rsvd[2];
	u8 info[0];
} __packed;

/**
 * struct qlink_resp_phy_params - response for QLINK_CMD_PHY_PARAMS_GET command
 *
 * @info: variable-length array of PHY params.
 */
struct qlink_resp_phy_params {
	struct qlink_resp rhdr;
	u8 info[0];
} __packed;

/**
 * struct qlink_resp_get_chan_stats - response for QLINK_CMD_CHAN_STATS cmd
 *
 * @info: variable-length channel info.
 */
struct qlink_resp_get_chan_stats {
	struct qlink_cmd rhdr;
	u8 info[0];
} __packed;

/* QLINK Events messages related definitions
 */

enum qlink_event_type {
	QLINK_EVENT_STA_ASSOCIATED	= 0x0021,
	QLINK_EVENT_STA_DEAUTH		= 0x0022,
	QLINK_EVENT_MGMT_RECEIVED	= 0x0023,
	QLINK_EVENT_SCAN_RESULTS	= 0x0024,
	QLINK_EVENT_SCAN_COMPLETE	= 0x0025,
	QLINK_EVENT_BSS_JOIN		= 0x0026,
	QLINK_EVENT_BSS_LEAVE		= 0x0027,
	QLINK_EVENT_FREQ_CHANGE		= 0x0028,
};

/**
 * struct qlink_event - QLINK event message header
 *
 * Header used for QLINK messages of QLINK_MSG_TYPE_EVENT type.
 *
 * @mhdr: Common QLINK message header.
 * @event_id: Specifies specific event ID, one of &enum qlink_event_type.
 * @macid: index of physical radio device the event was generated on or
 *	QLINK_MACID_RSVD if not applicable.
 * @vifid: index of virtual wireless interface on specified @macid the event
 *	was generated on or QLINK_VIFID_RSVD if not applicable.
 */
struct qlink_event {
	struct qlink_msg_header mhdr;
	__le16 event_id;
	u8 macid;
	u8 vifid;
} __packed;

/**
 * struct qlink_event_sta_assoc - data for QLINK_EVENT_STA_ASSOCIATED event
 *
 * @sta_addr: Address of a STA for which new association event was generated
 * @frame_control: control bits from 802.11 ASSOC_REQUEST header.
 * @payload: IEs from association request.
 */
struct qlink_event_sta_assoc {
	struct qlink_event ehdr;
	u8 sta_addr[ETH_ALEN];
	__le16 frame_control;
	u8 ies[0];
} __packed;

/**
 * struct qlink_event_sta_deauth - data for QLINK_EVENT_STA_DEAUTH event
 *
 * @sta_addr: Address of a deauthenticated STA.
 * @reason: reason for deauthentication.
 */
struct qlink_event_sta_deauth {
	struct qlink_event ehdr;
	u8 sta_addr[ETH_ALEN];
	__le16 reason;
} __packed;

/**
 * struct qlink_event_bss_join - data for QLINK_EVENT_BSS_JOIN event
 *
 * @bssid: BSSID of a BSS which interface tried to joined.
 * @status: status of joining attempt, see &enum ieee80211_statuscode.
 */
struct qlink_event_bss_join {
	struct qlink_event ehdr;
	u8 bssid[ETH_ALEN];
	__le16 status;
} __packed;

/**
 * struct qlink_event_bss_leave - data for QLINK_EVENT_BSS_LEAVE event
 *
 * @reason: reason of disconnecting from BSS.
 */
struct qlink_event_bss_leave {
	struct qlink_event ehdr;
	__le16 reason;
} __packed;

/**
 * struct qlink_event_freq_change - data for QLINK_EVENT_FREQ_CHANGE event
 *
 * @freq: new operating frequency in MHz
 */
struct qlink_event_freq_change {
	struct qlink_event ehdr;
	__le32 freq;
} __packed;

enum qlink_rxmgmt_flags {
	QLINK_RXMGMT_FLAG_ANSWERED = 1 << 0,
};

/**
 * struct qlink_event_rxmgmt - data for QLINK_EVENT_MGMT_RECEIVED event
 *
 * @freq: Frequency on which the frame was received in MHz.
 * @sig_dbm: signal strength in dBm.
 * @flags: bitmap of &enum qlink_rxmgmt_flags.
 * @frame_data: data of Rx'd frame itself.
 */
struct qlink_event_rxmgmt {
	struct qlink_event ehdr;
	__le32 freq;
	__le32 sig_dbm;
	__le32 flags;
	u8 frame_data[0];
} __packed;

enum qlink_frame_type {
	QLINK_BSS_FTYPE_UNKNOWN,
	QLINK_BSS_FTYPE_BEACON,
	QLINK_BSS_FTYPE_PRESP,
};

/**
 * struct qlink_event_scan_result - data for QLINK_EVENT_SCAN_RESULTS event
 *
 * @tsf: TSF timestamp indicating when scan results were generated.
 * @freq: Center frequency of the channel where BSS for which the scan result
 *	event was generated was discovered.
 * @capab: capabilities field.
 * @bintval: beacon interval announced by discovered BSS.
 * @signal: signal strength.
 * @frame_type: frame type used to get scan result, see &enum qlink_frame_type.
 * @bssid: BSSID announced by discovered BSS.
 * @ssid_len: length of SSID announced by BSS.
 * @ssid: SSID announced by discovered BSS.
 * @payload: IEs that are announced by discovered BSS in its MGMt frames.
 */
struct qlink_event_scan_result {
	struct qlink_event ehdr;
	__le64 tsf;
	__le16 freq;
	__le16 capab;
	__le16 bintval;
	s8 signal;
	u8 frame_type;
	u8 bssid[ETH_ALEN];
	u8 ssid_len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
	u8 payload[0];
} __packed;

/**
 * enum qlink_scan_complete_flags - indicates result of scan request.
 *
 * @QLINK_SCAN_NONE: Scan request was processed.
 * @QLINK_SCAN_ABORTED: Scan was aborted.
 */
enum qlink_scan_complete_flags {
	QLINK_SCAN_NONE		= 0,
	QLINK_SCAN_ABORTED	= BIT(0),
};

/**
 * struct qlink_event_scan_complete - data for QLINK_EVENT_SCAN_COMPLETE event
 *
 * @flags: flags indicating the status of pending scan request,
 *	see &enum qlink_scan_complete_flags.
 */
struct qlink_event_scan_complete {
	struct qlink_event ehdr;
	__le32 flags;
} __packed;

/* QLINK TLVs (Type-Length Values) definitions
 */

enum qlink_tlv_id {
	QTN_TLV_ID_FRAG_THRESH		= 0x0201,
	QTN_TLV_ID_RTS_THRESH		= 0x0202,
	QTN_TLV_ID_SRETRY_LIMIT		= 0x0203,
	QTN_TLV_ID_LRETRY_LIMIT		= 0x0204,
	QTN_TLV_ID_BCN_PERIOD		= 0x0205,
	QTN_TLV_ID_DTIM			= 0x0206,
	QTN_TLV_ID_REG_RULE		= 0x0207,
	QTN_TLV_ID_CHANNEL		= 0x020F,
	QTN_TLV_ID_COVERAGE_CLASS	= 0x0213,
	QTN_TLV_ID_IFACE_LIMIT		= 0x0214,
	QTN_TLV_ID_NUM_IFACE_COMB	= 0x0215,
	QTN_TLV_ID_CHANNEL_STATS	= 0x0216,
	QTN_TLV_ID_STA_BASIC_COUNTERS	= 0x0300,
	QTN_TLV_ID_STA_GENERIC_INFO	= 0x0301,
	QTN_TLV_ID_KEY			= 0x0302,
	QTN_TLV_ID_SEQ			= 0x0303,
	QTN_TLV_ID_CRYPTO		= 0x0304,
	QTN_TLV_ID_IE_SET		= 0x0305,
};

struct qlink_tlv_hdr {
	__le16 type;
	__le16 len;
	u8 val[0];
} __packed;

struct qlink_iface_limit {
	__le16 max_num;
	__le16 type;
} __packed;

struct qlink_iface_comb_num {
	__le16 iface_comb_num;
} __packed;

struct qlink_sta_stat_basic_counters {
	__le64 rx_bytes;
	__le64 tx_bytes;
	__le64 rx_beacons;
	__le32 rx_packets;
	__le32 tx_packets;
	__le32 rx_dropped;
	__le32 tx_failed;
} __packed;

enum qlink_sta_info_rate_flags {
	QLINK_STA_INFO_RATE_FLAG_INVALID	= 0,
	QLINK_STA_INFO_RATE_FLAG_HT_MCS		= BIT(0),
	QLINK_STA_INFO_RATE_FLAG_VHT_MCS	= BIT(1),
	QLINK_STA_INFO_RATE_FLAG_SHORT_GI	= BIT(2),
	QLINK_STA_INFO_RATE_FLAG_60G		= BIT(3),
};

enum qlink_sta_info_rate_bw {
	QLINK_STA_INFO_RATE_BW_5		= 0,
	QLINK_STA_INFO_RATE_BW_10		= 1,
	QLINK_STA_INFO_RATE_BW_20		= 2,
	QLINK_STA_INFO_RATE_BW_40		= 3,
	QLINK_STA_INFO_RATE_BW_80		= 4,
	QLINK_STA_INFO_RATE_BW_160		= 5,
};

/**
 * struct qlink_sta_info_rate - STA rate statistics
 *
 * @rate: data rate in Mbps.
 * @flags: bitmap of &enum qlink_sta_flags.
 * @mcs: 802.11-defined MCS index.
 * nss: Number of Spatial Streams.
 * @bw: bandwidth, one of &enum qlink_sta_info_rate_bw.
 */
struct qlink_sta_info_rate {
	__le16 rate;
	u8 flags;
	u8 mcs;
	u8 nss;
	u8 bw;
} __packed;

struct qlink_sta_info_state {
	__le32 mask;
	__le32 value;
} __packed;

#define QLINK_RSSI_OFFSET	120

struct qlink_sta_info_generic {
	struct qlink_sta_info_state state;
	__le32 connected_time;
	__le32 inactive_time;
	struct qlink_sta_info_rate rx_rate;
	struct qlink_sta_info_rate tx_rate;
	u8 rssi;
	u8 rssi_avg;
} __packed;

struct qlink_tlv_frag_rts_thr {
	struct qlink_tlv_hdr hdr;
	__le16 thr;
} __packed;

struct qlink_tlv_rlimit {
	struct qlink_tlv_hdr hdr;
	u8 rlimit;
} __packed;

struct qlink_tlv_cclass {
	struct qlink_tlv_hdr hdr;
	u8 cclass;
} __packed;

/**
 * enum qlink_reg_rule_flags - regulatory rule flags
 *
 * See description of &enum nl80211_reg_rule_flags
 */
enum qlink_reg_rule_flags {
	QLINK_RRF_NO_OFDM	= BIT(0),
	QLINK_RRF_NO_CCK	= BIT(1),
	QLINK_RRF_NO_INDOOR	= BIT(2),
	QLINK_RRF_NO_OUTDOOR	= BIT(3),
	QLINK_RRF_DFS		= BIT(4),
	QLINK_RRF_PTP_ONLY	= BIT(5),
	QLINK_RRF_PTMP_ONLY	= BIT(6),
	QLINK_RRF_NO_IR		= BIT(7),
	QLINK_RRF_AUTO_BW	= BIT(8),
	QLINK_RRF_IR_CONCURRENT	= BIT(9),
	QLINK_RRF_NO_HT40MINUS	= BIT(10),
	QLINK_RRF_NO_HT40PLUS	= BIT(11),
	QLINK_RRF_NO_80MHZ	= BIT(12),
	QLINK_RRF_NO_160MHZ	= BIT(13),
};

/**
 * struct qlink_tlv_reg_rule - data for QTN_TLV_ID_REG_RULE TLV
 *
 * Regulatory rule description.
 *
 * @start_freq_khz: start frequency of the range the rule is attributed to.
 * @end_freq_khz: end frequency of the range the rule is attributed to.
 * @max_bandwidth_khz: max bandwidth that channels in specified range can be
 *	configured to.
 * @max_antenna_gain: max antenna gain that can be used in the specified
 *	frequency range, dBi.
 * @max_eirp: maximum EIRP.
 * @flags: regulatory rule flags in &enum qlink_reg_rule_flags.
 * @dfs_cac_ms: DFS CAC period.
 */
struct qlink_tlv_reg_rule {
	struct qlink_tlv_hdr hdr;
	__le32 start_freq_khz;
	__le32 end_freq_khz;
	__le32 max_bandwidth_khz;
	__le32 max_antenna_gain;
	__le32 max_eirp;
	__le32 flags;
	__le32 dfs_cac_ms;
} __packed;

enum qlink_channel_flags {
	QLINK_CHAN_DISABLED		= BIT(0),
	QLINK_CHAN_NO_IR		= BIT(1),
	QLINK_CHAN_RADAR		= BIT(3),
	QLINK_CHAN_NO_HT40PLUS		= BIT(4),
	QLINK_CHAN_NO_HT40MINUS		= BIT(5),
	QLINK_CHAN_NO_OFDM		= BIT(6),
	QLINK_CHAN_NO_80MHZ		= BIT(7),
	QLINK_CHAN_NO_160MHZ		= BIT(8),
	QLINK_CHAN_INDOOR_ONLY		= BIT(9),
	QLINK_CHAN_IR_CONCURRENT	= BIT(10),
	QLINK_CHAN_NO_20MHZ		= BIT(11),
	QLINK_CHAN_NO_10MHZ		= BIT(12),
};

enum qlink_dfs_state {
	QLINK_DFS_USABLE,
	QLINK_DFS_UNAVAILABLE,
	QLINK_DFS_AVAILABLE,
};

struct qlink_tlv_channel {
	struct qlink_tlv_hdr hdr;
	__le16 hw_value;
	__le16 center_freq;
	__le32 flags;
	u8 band;
	u8 max_antenna_gain;
	u8 max_power;
	u8 max_reg_power;
	__le32 dfs_cac_ms;
	u8 dfs_state;
	u8 beacon_found;
	u8 rsvd[2];
} __packed;

#define QLINK_MAX_NR_CIPHER_SUITES            5
#define QLINK_MAX_NR_AKM_SUITES               2

struct qlink_auth_encr {
	__le32 wpa_versions;
	__le32 cipher_group;
	__le32 n_ciphers_pairwise;
	__le32 ciphers_pairwise[QLINK_MAX_NR_CIPHER_SUITES];
	__le32 n_akm_suites;
	__le32 akm_suites[QLINK_MAX_NR_AKM_SUITES];
	__le16 control_port_ethertype;
	u8 auth_type;
	u8 privacy;
	u8 mfp;
	u8 control_port;
	u8 control_port_no_encrypt;
} __packed;

struct qlink_chan_stats {
	__le32 chan_num;
	__le32 cca_tx;
	__le32 cca_rx;
	__le32 cca_busy;
	__le32 cca_try;
	s8 chan_noise;
} __packed;

#endif /* _QTN_QLINK_H_ */
