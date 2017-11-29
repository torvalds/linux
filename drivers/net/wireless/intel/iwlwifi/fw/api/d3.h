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
 *****************************************************************************/

#ifndef __iwl_fw_api_d3_h__
#define __iwl_fw_api_d3_h__

/**
 * enum iwl_d3_wakeup_flags - D3 manager wakeup flags
 * @IWL_WAKEUP_D3_CONFIG_FW_ERROR: wake up on firmware sysassert
 */
enum iwl_d3_wakeup_flags {
	IWL_WAKEUP_D3_CONFIG_FW_ERROR = BIT(0),
}; /* D3_MANAGER_WAKEUP_CONFIG_API_E_VER_3 */

/**
 * struct iwl_d3_manager_config - D3 manager configuration command
 * @min_sleep_time: minimum sleep time (in usec)
 * @wakeup_flags: wakeup flags, see &enum iwl_d3_wakeup_flags
 * @wakeup_host_timer: force wakeup after this many seconds
 *
 * The structure is used for the D3_CONFIG_CMD command.
 */
struct iwl_d3_manager_config {
	__le32 min_sleep_time;
	__le32 wakeup_flags;
	__le32 wakeup_host_timer;
} __packed; /* D3_MANAGER_CONFIG_CMD_S_VER_4 */


/* TODO: OFFLOADS_QUERY_API_S_VER_1 */

/**
 * enum iwl_d3_proto_offloads - enabled protocol offloads
 * @IWL_D3_PROTO_OFFLOAD_ARP: ARP data is enabled
 * @IWL_D3_PROTO_OFFLOAD_NS: NS (Neighbor Solicitation) is enabled
 * @IWL_D3_PROTO_IPV4_VALID: IPv4 data is valid
 * @IWL_D3_PROTO_IPV6_VALID: IPv6 data is valid
 */
enum iwl_proto_offloads {
	IWL_D3_PROTO_OFFLOAD_ARP = BIT(0),
	IWL_D3_PROTO_OFFLOAD_NS = BIT(1),
	IWL_D3_PROTO_IPV4_VALID = BIT(2),
	IWL_D3_PROTO_IPV6_VALID = BIT(3),
};

#define IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V1	2
#define IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V2	6
#define IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3L	12
#define IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3S	4
#define IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_MAX	12

#define IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3L	4
#define IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3S	2

/**
 * struct iwl_proto_offload_cmd_common - ARP/NS offload common part
 * @enabled: enable flags
 * @remote_ipv4_addr: remote address to answer to (or zero if all)
 * @host_ipv4_addr: our IPv4 address to respond to queries for
 * @arp_mac_addr: our MAC address for ARP responses
 * @reserved: unused
 */
struct iwl_proto_offload_cmd_common {
	__le32 enabled;
	__be32 remote_ipv4_addr;
	__be32 host_ipv4_addr;
	u8 arp_mac_addr[ETH_ALEN];
	__le16 reserved;
} __packed;

/**
 * struct iwl_proto_offload_cmd_v1 - ARP/NS offload configuration
 * @common: common/IPv4 configuration
 * @remote_ipv6_addr: remote address to answer to (or zero if all)
 * @solicited_node_ipv6_addr: broken -- solicited node address exists
 *	for each target address
 * @target_ipv6_addr: our target addresses
 * @ndp_mac_addr: neighbor solicitation response MAC address
 * @reserved2: reserved
 */
struct iwl_proto_offload_cmd_v1 {
	struct iwl_proto_offload_cmd_common common;
	u8 remote_ipv6_addr[16];
	u8 solicited_node_ipv6_addr[16];
	u8 target_ipv6_addr[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V1][16];
	u8 ndp_mac_addr[ETH_ALEN];
	__le16 reserved2;
} __packed; /* PROT_OFFLOAD_CONFIG_CMD_DB_S_VER_1 */

/**
 * struct iwl_proto_offload_cmd_v2 - ARP/NS offload configuration
 * @common: common/IPv4 configuration
 * @remote_ipv6_addr: remote address to answer to (or zero if all)
 * @solicited_node_ipv6_addr: broken -- solicited node address exists
 *	for each target address
 * @target_ipv6_addr: our target addresses
 * @ndp_mac_addr: neighbor solicitation response MAC address
 * @num_valid_ipv6_addrs: number of valid IPv6 addresses
 * @reserved2: reserved
 */
struct iwl_proto_offload_cmd_v2 {
	struct iwl_proto_offload_cmd_common common;
	u8 remote_ipv6_addr[16];
	u8 solicited_node_ipv6_addr[16];
	u8 target_ipv6_addr[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V2][16];
	u8 ndp_mac_addr[ETH_ALEN];
	u8 num_valid_ipv6_addrs;
	u8 reserved2[3];
} __packed; /* PROT_OFFLOAD_CONFIG_CMD_DB_S_VER_2 */

struct iwl_ns_config {
	struct in6_addr source_ipv6_addr;
	struct in6_addr dest_ipv6_addr;
	u8 target_mac_addr[ETH_ALEN];
	__le16 reserved;
} __packed; /* NS_OFFLOAD_CONFIG */

struct iwl_targ_addr {
	struct in6_addr addr;
	__le32 config_num;
} __packed; /* TARGET_IPV6_ADDRESS */

/**
 * struct iwl_proto_offload_cmd_v3_small - ARP/NS offload configuration
 * @common: common/IPv4 configuration
 * @num_valid_ipv6_addrs: number of valid IPv6 addresses
 * @targ_addrs: target IPv6 addresses
 * @ns_config: NS offload configurations
 */
struct iwl_proto_offload_cmd_v3_small {
	struct iwl_proto_offload_cmd_common common;
	__le32 num_valid_ipv6_addrs;
	struct iwl_targ_addr targ_addrs[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3S];
	struct iwl_ns_config ns_config[IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3S];
} __packed; /* PROT_OFFLOAD_CONFIG_CMD_DB_S_VER_3 */

/**
 * struct iwl_proto_offload_cmd_v3_large - ARP/NS offload configuration
 * @common: common/IPv4 configuration
 * @num_valid_ipv6_addrs: number of valid IPv6 addresses
 * @targ_addrs: target IPv6 addresses
 * @ns_config: NS offload configurations
 */
struct iwl_proto_offload_cmd_v3_large {
	struct iwl_proto_offload_cmd_common common;
	__le32 num_valid_ipv6_addrs;
	struct iwl_targ_addr targ_addrs[IWL_PROTO_OFFLOAD_NUM_IPV6_ADDRS_V3L];
	struct iwl_ns_config ns_config[IWL_PROTO_OFFLOAD_NUM_NS_CONFIG_V3L];
} __packed; /* PROT_OFFLOAD_CONFIG_CMD_DB_S_VER_3 */

/*
 * WOWLAN_PATTERNS
 */
#define IWL_WOWLAN_MIN_PATTERN_LEN	16
#define IWL_WOWLAN_MAX_PATTERN_LEN	128

struct iwl_wowlan_pattern {
	u8 mask[IWL_WOWLAN_MAX_PATTERN_LEN / 8];
	u8 pattern[IWL_WOWLAN_MAX_PATTERN_LEN];
	u8 mask_size;
	u8 pattern_size;
	__le16 reserved;
} __packed; /* WOWLAN_PATTERN_API_S_VER_1 */

#define IWL_WOWLAN_MAX_PATTERNS	20

struct iwl_wowlan_patterns_cmd {
	__le32 n_patterns;
	struct iwl_wowlan_pattern patterns[];
} __packed; /* WOWLAN_PATTERN_ARRAY_API_S_VER_1 */

enum iwl_wowlan_wakeup_filters {
	IWL_WOWLAN_WAKEUP_MAGIC_PACKET			= BIT(0),
	IWL_WOWLAN_WAKEUP_PATTERN_MATCH			= BIT(1),
	IWL_WOWLAN_WAKEUP_BEACON_MISS			= BIT(2),
	IWL_WOWLAN_WAKEUP_LINK_CHANGE			= BIT(3),
	IWL_WOWLAN_WAKEUP_GTK_REKEY_FAIL		= BIT(4),
	IWL_WOWLAN_WAKEUP_EAP_IDENT_REQ			= BIT(5),
	IWL_WOWLAN_WAKEUP_4WAY_HANDSHAKE		= BIT(6),
	IWL_WOWLAN_WAKEUP_ENABLE_NET_DETECT		= BIT(7),
	IWL_WOWLAN_WAKEUP_RF_KILL_DEASSERT		= BIT(8),
	IWL_WOWLAN_WAKEUP_REMOTE_LINK_LOSS		= BIT(9),
	IWL_WOWLAN_WAKEUP_REMOTE_SIGNATURE_TABLE	= BIT(10),
	IWL_WOWLAN_WAKEUP_REMOTE_TCP_EXTERNAL		= BIT(11),
	IWL_WOWLAN_WAKEUP_REMOTE_WAKEUP_PACKET		= BIT(12),
	IWL_WOWLAN_WAKEUP_IOAC_MAGIC_PACKET		= BIT(13),
	IWL_WOWLAN_WAKEUP_HOST_TIMER			= BIT(14),
	IWL_WOWLAN_WAKEUP_RX_FRAME			= BIT(15),
	IWL_WOWLAN_WAKEUP_BCN_FILTERING			= BIT(16),
}; /* WOWLAN_WAKEUP_FILTER_API_E_VER_4 */

enum iwl_wowlan_flags {
	IS_11W_ASSOC		= BIT(0),
	ENABLE_L3_FILTERING	= BIT(1),
	ENABLE_NBNS_FILTERING	= BIT(2),
	ENABLE_DHCP_FILTERING	= BIT(3),
	ENABLE_STORE_BEACON	= BIT(4),
};

/**
 * struct iwl_wowlan_config_cmd - WoWLAN configuration
 * @wakeup_filter: filter from &enum iwl_wowlan_wakeup_filters
 * @non_qos_seq: non-QoS sequence counter to use next
 * @qos_seq: QoS sequence counters to use next
 * @wowlan_ba_teardown_tids: bitmap of BA sessions to tear down
 * @is_11n_connection: indicates HT connection
 * @offloading_tid: TID reserved for firmware use
 * @flags: extra flags, see &enum iwl_wowlan_flags
 * @reserved: reserved
 */
struct iwl_wowlan_config_cmd {
	__le32 wakeup_filter;
	__le16 non_qos_seq;
	__le16 qos_seq[8];
	u8 wowlan_ba_teardown_tids;
	u8 is_11n_connection;
	u8 offloading_tid;
	u8 flags;
	u8 reserved[2];
} __packed; /* WOWLAN_CONFIG_API_S_VER_4 */

/*
 * WOWLAN_TSC_RSC_PARAMS
 */
#define IWL_NUM_RSC	16

struct tkip_sc {
	__le16 iv16;
	__le16 pad;
	__le32 iv32;
} __packed; /* TKIP_SC_API_U_VER_1 */

struct iwl_tkip_rsc_tsc {
	struct tkip_sc unicast_rsc[IWL_NUM_RSC];
	struct tkip_sc multicast_rsc[IWL_NUM_RSC];
	struct tkip_sc tsc;
} __packed; /* TKIP_TSC_RSC_API_S_VER_1 */

struct aes_sc {
	__le64 pn;
} __packed; /* TKIP_AES_SC_API_U_VER_1 */

struct iwl_aes_rsc_tsc {
	struct aes_sc unicast_rsc[IWL_NUM_RSC];
	struct aes_sc multicast_rsc[IWL_NUM_RSC];
	struct aes_sc tsc;
} __packed; /* AES_TSC_RSC_API_S_VER_1 */

union iwl_all_tsc_rsc {
	struct iwl_tkip_rsc_tsc tkip;
	struct iwl_aes_rsc_tsc aes;
}; /* ALL_TSC_RSC_API_S_VER_2 */

struct iwl_wowlan_rsc_tsc_params_cmd {
	union iwl_all_tsc_rsc all_tsc_rsc;
} __packed; /* ALL_TSC_RSC_API_S_VER_2 */

#define IWL_MIC_KEY_SIZE	8
struct iwl_mic_keys {
	u8 tx[IWL_MIC_KEY_SIZE];
	u8 rx_unicast[IWL_MIC_KEY_SIZE];
	u8 rx_mcast[IWL_MIC_KEY_SIZE];
} __packed; /* MIC_KEYS_API_S_VER_1 */

#define IWL_P1K_SIZE		5
struct iwl_p1k_cache {
	__le16 p1k[IWL_P1K_SIZE];
} __packed;

#define IWL_NUM_RX_P1K_CACHE	2

struct iwl_wowlan_tkip_params_cmd {
	struct iwl_mic_keys mic_keys;
	struct iwl_p1k_cache tx;
	struct iwl_p1k_cache rx_uni[IWL_NUM_RX_P1K_CACHE];
	struct iwl_p1k_cache rx_multi[IWL_NUM_RX_P1K_CACHE];
} __packed; /* WOWLAN_TKIP_SETTING_API_S_VER_1 */

#define IWL_KCK_MAX_SIZE	32
#define IWL_KEK_MAX_SIZE	32

struct iwl_wowlan_kek_kck_material_cmd {
	u8	kck[IWL_KCK_MAX_SIZE];
	u8	kek[IWL_KEK_MAX_SIZE];
	__le16	kck_len;
	__le16	kek_len;
	__le64	replay_ctr;
} __packed; /* KEK_KCK_MATERIAL_API_S_VER_2 */

#define RF_KILL_INDICATOR_FOR_WOWLAN	0x87

enum iwl_wowlan_rekey_status {
	IWL_WOWLAN_REKEY_POST_REKEY = 0,
	IWL_WOWLAN_REKEY_WHILE_REKEY = 1,
}; /* WOWLAN_REKEY_STATUS_API_E_VER_1 */

enum iwl_wowlan_wakeup_reason {
	IWL_WOWLAN_WAKEUP_BY_NON_WIRELESS			= 0,
	IWL_WOWLAN_WAKEUP_BY_MAGIC_PACKET			= BIT(0),
	IWL_WOWLAN_WAKEUP_BY_PATTERN				= BIT(1),
	IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_MISSED_BEACON	= BIT(2),
	IWL_WOWLAN_WAKEUP_BY_DISCONNECTION_ON_DEAUTH		= BIT(3),
	IWL_WOWLAN_WAKEUP_BY_GTK_REKEY_FAILURE			= BIT(4),
	IWL_WOWLAN_WAKEUP_BY_RFKILL_DEASSERTED			= BIT(5),
	IWL_WOWLAN_WAKEUP_BY_UCODE_ERROR			= BIT(6),
	IWL_WOWLAN_WAKEUP_BY_EAPOL_REQUEST			= BIT(7),
	IWL_WOWLAN_WAKEUP_BY_FOUR_WAY_HANDSHAKE			= BIT(8),
	IWL_WOWLAN_WAKEUP_BY_REM_WAKE_LINK_LOSS			= BIT(9),
	IWL_WOWLAN_WAKEUP_BY_REM_WAKE_SIGNATURE_TABLE		= BIT(10),
	IWL_WOWLAN_WAKEUP_BY_REM_WAKE_TCP_EXTERNAL		= BIT(11),
	IWL_WOWLAN_WAKEUP_BY_REM_WAKE_WAKEUP_PACKET		= BIT(12),
	IWL_WOWLAN_WAKEUP_BY_IOAC_MAGIC_PACKET			= BIT(13),
	IWL_WOWLAN_WAKEUP_BY_D3_WAKEUP_HOST_TIMER		= BIT(14),
	IWL_WOWLAN_WAKEUP_BY_RXFRAME_FILTERED_IN		= BIT(15),
	IWL_WOWLAN_WAKEUP_BY_BEACON_FILTERED_IN			= BIT(16),

}; /* WOWLAN_WAKE_UP_REASON_API_E_VER_2 */

struct iwl_wowlan_gtk_status {
	u8 key_index;
	u8 reserved[3];
	u8 decrypt_key[16];
	u8 tkip_mic_key[8];
	struct iwl_wowlan_rsc_tsc_params_cmd rsc;
} __packed; /* WOWLAN_GTK_MATERIAL_VER_1 */

/**
 * struct iwl_wowlan_status - WoWLAN status
 * @gtk: GTK data
 * @replay_ctr: GTK rekey replay counter
 * @pattern_number: number of the matched pattern
 * @non_qos_seq_ctr: non-QoS sequence counter to use next
 * @qos_seq_ctr: QoS sequence counters to use next
 * @wakeup_reasons: wakeup reasons, see &enum iwl_wowlan_wakeup_reason
 * @num_of_gtk_rekeys: number of GTK rekeys
 * @transmitted_ndps: number of transmitted neighbor discovery packets
 * @received_beacons: number of received beacons
 * @wake_packet_length: wakeup packet length
 * @wake_packet_bufsize: wakeup packet buffer size
 * @wake_packet: wakeup packet
 */
struct iwl_wowlan_status {
	struct iwl_wowlan_gtk_status gtk;
	__le64 replay_ctr;
	__le16 pattern_number;
	__le16 non_qos_seq_ctr;
	__le16 qos_seq_ctr[8];
	__le32 wakeup_reasons;
	__le32 num_of_gtk_rekeys;
	__le32 transmitted_ndps;
	__le32 received_beacons;
	__le32 wake_packet_length;
	__le32 wake_packet_bufsize;
	u8 wake_packet[]; /* can be truncated from _length to _bufsize */
} __packed; /* WOWLAN_STATUSES_API_S_VER_6 */

#define IWL_WOWLAN_TCP_MAX_PACKET_LEN		64
#define IWL_WOWLAN_REMOTE_WAKE_MAX_PACKET_LEN	128
#define IWL_WOWLAN_REMOTE_WAKE_MAX_TOKENS	2048

struct iwl_tcp_packet_info {
	__le16 tcp_pseudo_header_checksum;
	__le16 tcp_payload_length;
} __packed; /* TCP_PACKET_INFO_API_S_VER_2 */

struct iwl_tcp_packet {
	struct iwl_tcp_packet_info info;
	u8 rx_mask[IWL_WOWLAN_MAX_PATTERN_LEN / 8];
	u8 data[IWL_WOWLAN_TCP_MAX_PACKET_LEN];
} __packed; /* TCP_PROTOCOL_PACKET_API_S_VER_1 */

struct iwl_remote_wake_packet {
	struct iwl_tcp_packet_info info;
	u8 rx_mask[IWL_WOWLAN_MAX_PATTERN_LEN / 8];
	u8 data[IWL_WOWLAN_REMOTE_WAKE_MAX_PACKET_LEN];
} __packed; /* TCP_PROTOCOL_PACKET_API_S_VER_1 */

struct iwl_wowlan_remote_wake_config {
	__le32 connection_max_time; /* unused */
	/* TCP_PROTOCOL_CONFIG_API_S_VER_1 */
	u8 max_syn_retries;
	u8 max_data_retries;
	u8 tcp_syn_ack_timeout;
	u8 tcp_ack_timeout;

	struct iwl_tcp_packet syn_tx;
	struct iwl_tcp_packet synack_rx;
	struct iwl_tcp_packet keepalive_ack_rx;
	struct iwl_tcp_packet fin_tx;

	struct iwl_remote_wake_packet keepalive_tx;
	struct iwl_remote_wake_packet wake_rx;

	/* REMOTE_WAKE_OFFSET_INFO_API_S_VER_1 */
	u8 sequence_number_offset;
	u8 sequence_number_length;
	u8 token_offset;
	u8 token_length;
	/* REMOTE_WAKE_PROTOCOL_PARAMS_API_S_VER_1 */
	__le32 initial_sequence_number;
	__le16 keepalive_interval;
	__le16 num_tokens;
	u8 tokens[IWL_WOWLAN_REMOTE_WAKE_MAX_TOKENS];
} __packed; /* REMOTE_WAKE_CONFIG_API_S_VER_2 */

/* TODO: NetDetect API */

#endif /* __iwl_fw_api_d3_h__ */
