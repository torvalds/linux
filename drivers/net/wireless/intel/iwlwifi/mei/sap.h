// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Intel Corporation
 */

#ifndef __sap_h__
#define __sap_h__

#include "mei/iwl-mei.h"

/**
 * DOC: Introduction
 *
 * SAP is the protocol used by the Intel Wireless driver (iwlwifi)
 * and the wireless driver implemented in the CSME firmware.
 * It allows to do several things:
 * 1) Decide who is the owner of the device: CSME or the host
 * 2) When the host is the owner of the device, CSME can still
 * send and receive packets through iwlwifi.
 *
 * The protocol uses the ME interface (mei driver) to send
 * messages to the CSME firmware. Those messages have a header
 * &struct iwl_sap_me_msg_hdr and this header is followed
 * by a payload.
 *
 * Since this messaging system cannot support high amounts of
 * traffic, iwlwifi and the CSME firmware's WLAN driver have an
 * addtional communication pipe to exchange information. The body
 * of the message is copied to a shared area and the message that
 * goes over the ME interface just signals the other side
 * that a new message is waiting in the shared area. The ME
 * interface is used only for signaling and not to transfer
 * the payload.
 *
 * This shared area of memory is DMA'able mapped to be
 * writable by both the CSME firmware and iwlwifi. It is
 * mapped to address space of the device that controls the ME
 * interface's DMA engine. Any data that iwlwifi needs to
 * send to the CSME firmware needs to be copied to there.
 */

/**
 * DOC: Initial Handshake
 *
 * Once we get a link to the CMSE's WLAN driver we start the handshake
 * to establish the shared memory that will allow the communication between
 * the CSME's WLAN driver and the host.
 *
 * 1) Host sends %SAP_ME_MSG_START message with the physical address
 * of the shared area.
 * 2) CSME replies with %SAP_ME_MSG_START_OK which includes the versions
 * protocol versions supported by CSME.
 */

/**
 * DOC: Host and driver state messages
 *
 * In order to let CSME konw about the host state and the host driver state,
 * the host sends messages that let CSME know about the host's state.
 * When the host driver is loaded, the host sends %SAP_MSG_NOTIF_WIFIDR_UP.
 * When the host driver is unloaded, the host sends %SAP_MSG_NOTIF_WIFIDR_DOWN.
 * When the iwlmei is unloaded, %SAP_MSG_NOTIF_HOST_GOES_DOWN is sent to let
 * CSME know not to access the shared memory anymore since it'll be freed.
 *
 * CSME will reply to SAP_MSG_NOTIF_WIFIDR_UP by
 * %SAP_MSG_NOTIF_AMT_STATE to let the host driver whether CSME can use the
 * WiFi device or not followed by %SAP_MSG_NOTIF_CSME_CONN_STATUS to inform
 * the host driver on the connection state of CSME.
 *
 * When host is associated to an AP, it must send %SAP_MSG_NOTIF_HOST_LINK_UP
 * and when it disconnect from the AP, it must send
 * %SAP_MSG_NOTIF_HOST_LINK_DOWN.
 */

/**
 * DOC: Ownership
 *
 * The device can be controlled either by the CSME firmware or
 * by the host driver: iwlwifi. There is a negotiaion between
 * those two entities to determine who controls (or owns) the
 * device. Since the CSME can control the device even when the
 * OS is not working or even missing, the CSME can request the
 * device if it comes to the conclusion that the OS's host driver
 * is not operational. This is why the host driver needs to
 * signal CSME that it is up and running. If the driver is
 * unloaded, it'll signal CSME that it is going down so that
 * CSME can take ownership.
 */

/**
 * DOC: Ownership transfer
 *
 * When the host driver needs the device, it'll send the
 * %SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP that will be replied by
 * %SAP_MSG_NOTIF_CSME_REPLY_TO_HOST_OWNERSHIP_REQ which will let the
 * host know whether the ownership is granted or no. If the ownership is
 * granted, the hosts sends %SAP_MSG_NOTIF_HOST_OWNERSHIP_CONFIRMED.
 *
 * When CSME requests ownership, it'll send the
 * %SAP_MSG_NOTIF_CSME_TAKING_OWNERSHIP and give some time to host to stop
 * accessing the device. The host needs to send
 * %SAP_MSG_NOTIF_CSME_OWNERSHIP_CONFIRMED to confirm that it won't access
 * the device anymore. If the host failed to send this message fast enough,
 * CSME will take ownership on the device anyway.
 * When CSME is willing to release the ownership, it'll send
 * %SAP_MSG_NOTIF_CSME_CAN_RELEASE_OWNERSHIP.
 */

/**
 * DOC: Data messages
 *
 * Data messages must be sent and receives on a separate queue in the shared
 * memory. Almost all the data messages use the %SAP_MSG_DATA_PACKET for both
 * packets sent by CSME to the host to be sent to the AP or for packets
 * received from the AP and sent by the host to CSME.
 * CSME sends filters to the host to let the host what inbound packets it must
 * send to CSME. Those filters are received by the host as a
 * %SAP_MSG_NOTIF_CSME_FILTERS command.
 * The only outbound packets that must be sent to CSME are the DHCP packets.
 * Those packets must use the %SAP_MSG_CB_DATA_PACKET message.
 */

/**
 * enum iwl_sap_me_msg_id - the ID of the ME message
 * @SAP_ME_MSG_START: See &struct iwl_sap_me_msg_start.
 * @SAP_ME_MSG_START_OK: See &struct iwl_sap_me_msg_start_ok.
 * @SAP_ME_MSG_CHECK_SHARED_AREA: This message has no payload.
 */
enum iwl_sap_me_msg_id {
	SAP_ME_MSG_START	= 1,
	SAP_ME_MSG_START_OK,
	SAP_ME_MSG_CHECK_SHARED_AREA,
};

/**
 * struct iwl_sap_me_msg_hdr - the header of the ME message
 * @type: the type of the message, see &enum iwl_sap_me_msg_id.
 * @seq_num: a sequence number used for debug only.
 * @len: the length of the mssage.
 */
struct iwl_sap_me_msg_hdr {
	__le32 type;
	__le32 seq_num;
	__le32 len;
} __packed;

/**
 * struct iwl_sap_me_msg_start - used for the %SAP_ME_MSG_START message
 * @hdr: See &struct iwl_sap_me_msg_hdr.
 * @shared_mem: physical address of SAP shared memory area.
 * @init_data_seq_num: seq_num of the first data packet HOST -> CSME.
 * @init_notif_seq_num: seq_num of the first notification HOST -> CSME.
 * @supported_versions: The host sends to the CSME a zero-terminated array
 * of versions its supports.
 *
 * This message is sent by the host to CSME and will responded by the
 * %SAP_ME_MSG_START_OK message.
 */
struct iwl_sap_me_msg_start {
	struct iwl_sap_me_msg_hdr hdr;
	__le64 shared_mem;
	__le16 init_data_seq_num;
	__le16 init_notif_seq_num;
	u8 supported_versions[64];
} __packed;

/**
 * struct iwl_sap_me_msg_start_ok - used for the %SAP_ME_MSG_START_OK
 * @hdr: See &struct iwl_sap_me_msg_hdr
 * @init_data_seq_num: Not used.
 * @init_notif_seq_num: Not used
 * @supported_version: The version that will be used.
 * @reserved: For alignment.
 *
 * This message is sent by CSME to the host in response to the
 * %SAP_ME_MSG_START message.
 */
struct iwl_sap_me_msg_start_ok {
	struct iwl_sap_me_msg_hdr hdr;
	__le16 init_data_seq_num;
	__le16 init_notif_seq_num;
	u8 supported_version;
	u8 reserved[3];
} __packed;

/**
 * enum iwl_sap_msg - SAP messages
 * @SAP_MSG_NOTIF_BOTH_WAYS_MIN: Not used.
 * @SAP_MSG_NOTIF_PING: No payload. Solicitate a response message (check-alive).
 * @SAP_MSG_NOTIF_PONG: No payload. The response message.
 * @SAP_MSG_NOTIF_BOTH_WAYS_MAX: Not used.
 *
 * @SAP_MSG_NOTIF_FROM_CSME_MIN: Not used.
 * @SAP_MSG_NOTIF_CSME_FILTERS: TODO
 * @SAP_MSG_NOTIF_AMT_STATE: Payload is a DW. Any non-zero value means
 *	that CSME is enabled.
 * @SAP_MSG_NOTIF_CSME_REPLY_TO_HOST_OWNERSHIP_REQ: Payload is a DW. 0 means
 *	the host will not get ownership. Any other value means the host is
 *	the owner.
 * @SAP_MSG_NOTIF_CSME_TAKING_OWNERSHIP: No payload.
 * @SAP_MSG_NOTIF_TRIGGER_IP_REFRESH: No payload.
 * @SAP_MSG_NOTIF_CSME_CAN_RELEASE_OWNERSHIP: No payload.
 * @SAP_MSG_NOTIF_NIC_OWNER: Payload is a DW. See &enum iwl_sap_nic_owner.
 * @SAP_MSG_NOTIF_CSME_CONN_STATUS: See &struct iwl_sap_notif_conn_status.
 * @SAP_MSG_NOTIF_NVM: See &struct iwl_sap_nvm.
 * @SAP_MSG_NOTIF_FROM_CSME_MAX: Not used.
 *
 * @SAP_MSG_NOTIF_FROM_HOST_MIN: Not used.
 * @SAP_MSG_NOTIF_BAND_SELECTION: TODO
 * @SAP_MSG_NOTIF_RADIO_STATE: Payload is a DW.
 *	See &enum iwl_sap_radio_state_bitmap.
 * @SAP_MSG_NOTIF_NIC_INFO: See &struct iwl_sap_notif_host_nic_info.
 * @SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP: No payload.
 * @SAP_MSG_NOTIF_HOST_SUSPENDS: Payload is a DW. Bitmap described in
 *	&enum iwl_sap_notif_host_suspends_bitmap.
 * @SAP_MSG_NOTIF_HOST_RESUMES: Payload is a DW. 0 or 1. 1 says that
 *	the CSME should re-initialize the init control block.
 * @SAP_MSG_NOTIF_HOST_GOES_DOWN: No payload.
 * @SAP_MSG_NOTIF_CSME_OWNERSHIP_CONFIRMED: No payload.
 * @SAP_MSG_NOTIF_COUNTRY_CODE: See &struct iwl_sap_notif_country_code.
 * @SAP_MSG_NOTIF_HOST_LINK_UP: See &struct iwl_sap_notif_host_link_up.
 * @SAP_MSG_NOTIF_HOST_LINK_DOWN: See &struct iwl_sap_notif_host_link_down.
 * @SAP_MSG_NOTIF_WHO_OWNS_NIC: No payload.
 * @SAP_MSG_NOTIF_WIFIDR_DOWN: No payload.
 * @SAP_MSG_NOTIF_WIFIDR_UP: No payload.
 * @SAP_MSG_NOTIF_HOST_OWNERSHIP_CONFIRMED: No payload.
 * @SAP_MSG_NOTIF_SAR_LIMITS: See &struct iwl_sap_notif_sar_limits.
 * @SAP_MSG_NOTIF_GET_NVM: No payload. Triggers %SAP_MSG_NOTIF_NVM.
 * @SAP_MSG_NOTIF_FROM_HOST_MAX: Not used.
 *
 * @SAP_MSG_DATA_MIN: Not used.
 * @SAP_MSG_DATA_PACKET: Packets that passed the filters defined by
 *	%SAP_MSG_NOTIF_CSME_FILTERS. The payload is &struct iwl_sap_hdr with
 *	the payload of the packet immediately afterwards.
 * @SAP_MSG_CB_DATA_PACKET: Indicates to CSME that we transmitted a specific
 *	packet. Used only for DHCP transmitted packets. See
 *	&struct iwl_sap_cb_data.
 * @SAP_MSG_DATA_MAX: Not used.
 */
enum iwl_sap_msg {
	SAP_MSG_NOTIF_BOTH_WAYS_MIN			= 0,
	SAP_MSG_NOTIF_PING				= 1,
	SAP_MSG_NOTIF_PONG				= 2,
	SAP_MSG_NOTIF_BOTH_WAYS_MAX,

	SAP_MSG_NOTIF_FROM_CSME_MIN			= 500,
	SAP_MSG_NOTIF_CSME_FILTERS			= SAP_MSG_NOTIF_FROM_CSME_MIN,
	/* 501 is deprecated */
	SAP_MSG_NOTIF_AMT_STATE				= 502,
	SAP_MSG_NOTIF_CSME_REPLY_TO_HOST_OWNERSHIP_REQ	= 503,
	SAP_MSG_NOTIF_CSME_TAKING_OWNERSHIP		= 504,
	SAP_MSG_NOTIF_TRIGGER_IP_REFRESH		= 505,
	SAP_MSG_NOTIF_CSME_CAN_RELEASE_OWNERSHIP	= 506,
	/* 507 is deprecated */
	/* 508 is deprecated */
	/* 509 is deprecated */
	/* 510 is deprecated */
	SAP_MSG_NOTIF_NIC_OWNER				= 511,
	SAP_MSG_NOTIF_CSME_CONN_STATUS			= 512,
	SAP_MSG_NOTIF_NVM				= 513,
	SAP_MSG_NOTIF_FROM_CSME_MAX,

	SAP_MSG_NOTIF_FROM_HOST_MIN			= 1000,
	SAP_MSG_NOTIF_BAND_SELECTION			= SAP_MSG_NOTIF_FROM_HOST_MIN,
	SAP_MSG_NOTIF_RADIO_STATE			= 1001,
	SAP_MSG_NOTIF_NIC_INFO				= 1002,
	SAP_MSG_NOTIF_HOST_ASKS_FOR_NIC_OWNERSHIP	= 1003,
	SAP_MSG_NOTIF_HOST_SUSPENDS			= 1004,
	SAP_MSG_NOTIF_HOST_RESUMES			= 1005,
	SAP_MSG_NOTIF_HOST_GOES_DOWN			= 1006,
	SAP_MSG_NOTIF_CSME_OWNERSHIP_CONFIRMED		= 1007,
	SAP_MSG_NOTIF_COUNTRY_CODE			= 1008,
	SAP_MSG_NOTIF_HOST_LINK_UP			= 1009,
	SAP_MSG_NOTIF_HOST_LINK_DOWN			= 1010,
	SAP_MSG_NOTIF_WHO_OWNS_NIC			= 1011,
	SAP_MSG_NOTIF_WIFIDR_DOWN			= 1012,
	SAP_MSG_NOTIF_WIFIDR_UP				= 1013,
	/* 1014 is deprecated */
	SAP_MSG_NOTIF_HOST_OWNERSHIP_CONFIRMED		= 1015,
	SAP_MSG_NOTIF_SAR_LIMITS			= 1016,
	SAP_MSG_NOTIF_GET_NVM				= 1017,
	SAP_MSG_NOTIF_FROM_HOST_MAX,

	SAP_MSG_DATA_MIN				= 2000,
	SAP_MSG_DATA_PACKET				= SAP_MSG_DATA_MIN,
	SAP_MSG_CB_DATA_PACKET				= 2001,
	SAP_MSG_DATA_MAX,
};

/**
 * struct iwl_sap_hdr - prefixes any SAP message
 * @type: See &enum iwl_sap_msg.
 * @len: The length of the message (header not included).
 * @seq_num: For debug.
 * @payload: The payload of the message.
 */
struct iwl_sap_hdr {
	__le16 type;
	__le16 len;
	__le32 seq_num;
	u8 payload[];
};

/**
 * struct iwl_sap_msg_dw - suits any DW long SAP message
 * @hdr: The SAP header
 * @val: The value of the DW.
 */
struct iwl_sap_msg_dw {
	struct iwl_sap_hdr hdr;
	__le32 val;
};

/**
 * enum iwl_sap_nic_owner - used by %SAP_MSG_NOTIF_NIC_OWNER
 * @SAP_NIC_OWNER_UNKNOWN: Not used.
 * @SAP_NIC_OWNER_HOST: The host owns the NIC.
 * @SAP_NIC_OWNER_ME: CSME owns the NIC.
 */
enum iwl_sap_nic_owner {
	SAP_NIC_OWNER_UNKNOWN,
	SAP_NIC_OWNER_HOST,
	SAP_NIC_OWNER_ME,
};

enum iwl_sap_wifi_auth_type {
	SAP_WIFI_AUTH_TYPE_OPEN		= IWL_MEI_AKM_AUTH_OPEN,
	SAP_WIFI_AUTH_TYPE_RSNA		= IWL_MEI_AKM_AUTH_RSNA,
	SAP_WIFI_AUTH_TYPE_RSNA_PSK	= IWL_MEI_AKM_AUTH_RSNA_PSK,
	SAP_WIFI_AUTH_TYPE_SAE		= IWL_MEI_AKM_AUTH_SAE,
	SAP_WIFI_AUTH_TYPE_MAX,
};

/**
 * enum iwl_sap_wifi_cipher_alg
 * @SAP_WIFI_CIPHER_ALG_NONE: TBD
 * @SAP_WIFI_CIPHER_ALG_CCMP: TBD
 * @SAP_WIFI_CIPHER_ALG_GCMP: TBD
 * @SAP_WIFI_CIPHER_ALG_GCMP_256: TBD
 */
enum iwl_sap_wifi_cipher_alg {
	SAP_WIFI_CIPHER_ALG_NONE	= IWL_MEI_CIPHER_NONE,
	SAP_WIFI_CIPHER_ALG_CCMP	= IWL_MEI_CIPHER_CCMP,
	SAP_WIFI_CIPHER_ALG_GCMP	= IWL_MEI_CIPHER_GCMP,
	SAP_WIFI_CIPHER_ALG_GCMP_256	= IWL_MEI_CIPHER_GCMP_256,
};

/**
 * struct iwl_sap_notif_connection_info - nested in other structures
 * @ssid_len: The length of the SSID.
 * @ssid: The SSID.
 * @auth_mode: The authentication mode. See &enum iwl_sap_wifi_auth_type.
 * @pairwise_cipher: The cipher used for unicast packets.
 *	See &enum iwl_sap_wifi_cipher_alg.
 * @channel: The channel on which we are associated.
 * @band: The band on which we are associated.
 * @reserved: For alignment.
 * @bssid: The BSSID.
 * @reserved1: For alignment.
 */
struct iwl_sap_notif_connection_info {
	__le32 ssid_len;
	u8 ssid[32];
	__le32 auth_mode;
	__le32 pairwise_cipher;
	u8 channel;
	u8 band;
	__le16 reserved;
	u8 bssid[6];
	__le16 reserved1;
} __packed;

/**
 * enum iwl_sap_scan_request - for the scan_request field
 * @SCAN_REQUEST_FILTERING: Filtering is requested.
 * @SCAN_REQUEST_FAST: Fast scan is requested.
 */
enum iwl_sap_scan_request {
	SCAN_REQUEST_FILTERING	= 1 << 0,
	SCAN_REQUEST_FAST	= 1 << 1,
};

/**
 * struct iwl_sap_notif_conn_status - payload of %SAP_MSG_NOTIF_CSME_CONN_STATUS
 * @hdr: The SAP header
 * @link_prot_state: Non-zero if link protection is active.
 * @scan_request: See &enum iwl_sap_scan_request.
 * @conn_info: Information about the connection.
 */
struct iwl_sap_notif_conn_status {
	struct iwl_sap_hdr hdr;
	__le32 link_prot_state;
	__le32 scan_request;
	struct iwl_sap_notif_connection_info conn_info;
} __packed;

/**
 * enum iwl_sap_radio_state_bitmap - used for %SAP_MSG_NOTIF_RADIO_STATE
 * @SAP_SW_RFKILL_DEASSERTED: If set, SW RfKill is de-asserted
 * @SAP_HW_RFKILL_DEASSERTED: If set, HW RfKill is de-asserted
 *
 * If both bits are set, then the radio is on.
 */
enum iwl_sap_radio_state_bitmap {
	SAP_SW_RFKILL_DEASSERTED	= 1 << 0,
	SAP_HW_RFKILL_DEASSERTED	= 1 << 1,
};

/**
 * enum iwl_sap_notif_host_suspends_bitmap - used for %SAP_MSG_NOTIF_HOST_SUSPENDS
 * @SAP_OFFER_NIC: TBD
 * @SAP_FILTER_CONFIGURED: TBD
 * @SAP_NLO_CONFIGURED: TBD
 * @SAP_HOST_OWNS_NIC: TBD
 * @SAP_LINK_PROTECTED: TBD
 */
enum iwl_sap_notif_host_suspends_bitmap {
	SAP_OFFER_NIC		= 1 << 0,
	SAP_FILTER_CONFIGURED	= 1 << 1,
	SAP_NLO_CONFIGURED	= 1 << 2,
	SAP_HOST_OWNS_NIC	= 1 << 3,
	SAP_LINK_PROTECTED	= 1 << 4,
};

/**
 * struct iwl_sap_notif_country_code - payload of %SAP_MSG_NOTIF_COUNTRY_CODE
 * @hdr: The SAP header
 * @mcc: The country code.
 * @source_id: TBD
 * @reserved: For alignment.
 * @diff_time: TBD
 */
struct iwl_sap_notif_country_code {
	struct iwl_sap_hdr hdr;
	__le16 mcc;
	u8 source_id;
	u8 reserved;
	__le32 diff_time;
} __packed;

/**
 * struct iwl_sap_notif_host_link_up - payload of %SAP_MSG_NOTIF_HOST_LINK_UP
 * @hdr: The SAP header
 * @conn_info: Information about the connection.
 * @colloc_channel: The collocated channel
 * @colloc_band: The band of the collocated channel.
 * @reserved: For alignment.
 * @colloc_bssid: The collocated BSSID.
 * @reserved1: For alignment.
 */
struct iwl_sap_notif_host_link_up {
	struct iwl_sap_hdr hdr;
	struct iwl_sap_notif_connection_info conn_info;
	u8 colloc_channel;
	u8 colloc_band;
	__le16 reserved;
	u8 colloc_bssid[6];
	__le16 reserved1;
} __packed;

/**
 * enum iwl_sap_notif_link_down_type - used in &struct iwl_sap_notif_host_link_down
 * @HOST_LINK_DOWN_TYPE_NONE: TBD
 * @HOST_LINK_DOWN_TYPE_TEMPORARY: TBD
 * @HOST_LINK_DOWN_TYPE_LONG: TBD
 */
enum iwl_sap_notif_link_down_type {
	HOST_LINK_DOWN_TYPE_NONE,
	HOST_LINK_DOWN_TYPE_TEMPORARY,
	HOST_LINK_DOWN_TYPE_LONG,
};

/**
 * struct iwl_sap_notif_host_link_down - payload for %SAP_MSG_NOTIF_HOST_LINK_DOWN
 * @hdr: The SAP header
 * @type: See &enum iwl_sap_notif_link_down_type.
 * @reserved: For alignment.
 * @reason_valid: If 0, ignore the next field.
 * @reason: The reason of the disconnection.
 */
struct iwl_sap_notif_host_link_down {
	struct iwl_sap_hdr hdr;
	u8 type;
	u8 reserved[2];
	u8 reason_valid;
	__le32 reason;
} __packed;

/**
 * struct iwl_sap_notif_host_nic_info - payload for %SAP_MSG_NOTIF_NIC_INFO
 * @hdr: The SAP header
 * @mac_address: The MAC address as configured to the interface.
 * @nvm_address: The MAC address as configured in the NVM.
 */
struct iwl_sap_notif_host_nic_info {
	struct iwl_sap_hdr hdr;
	u8 mac_address[6];
	u8 nvm_address[6];
} __packed;

/**
 * struct iwl_sap_notif_dw - payload is a dw
 * @hdr: The SAP header.
 * @dw: The payload.
 */
struct iwl_sap_notif_dw {
	struct iwl_sap_hdr hdr;
	__le32 dw;
} __packed;

/**
 * struct iwl_sap_notif_sar_limits - payload for %SAP_MSG_NOTIF_SAR_LIMITS
 * @hdr: The SAP header
 * @sar_chain_info_table: Tx power limits.
 */
struct iwl_sap_notif_sar_limits {
	struct iwl_sap_hdr hdr;
	__le16 sar_chain_info_table[2][5];
} __packed;

/**
 * enum iwl_sap_nvm_caps - capabilities for NVM SAP
 * @SAP_NVM_CAPS_LARI_SUPPORT: Lari is supported
 * @SAP_NVM_CAPS_11AX_SUPPORT: 11AX is supported
 */
enum iwl_sap_nvm_caps {
	SAP_NVM_CAPS_LARI_SUPPORT	= BIT(0),
	SAP_NVM_CAPS_11AX_SUPPORT	= BIT(1),
};

/**
 * struct iwl_sap_nvm - payload for %SAP_MSG_NOTIF_NVM
 * @hdr: The SAP header.
 * @hw_addr: The MAC address
 * @n_hw_addrs: The number of MAC addresses
 * @reserved: For alignment.
 * @radio_cfg: The radio configuration.
 * @caps: See &enum iwl_sap_nvm_caps.
 * @nvm_version: The version of the NVM.
 * @channels: The data for each channel.
 */
struct iwl_sap_nvm {
	struct iwl_sap_hdr hdr;
	u8 hw_addr[6];
	u8 n_hw_addrs;
	u8 reserved;
	__le32 radio_cfg;
	__le32 caps;
	__le32 nvm_version;
	__le32 channels[110];
} __packed;

/**
 * enum iwl_sap_eth_filter_flags - used in &struct iwl_sap_eth_filter
 * @SAP_ETH_FILTER_STOP: Do not process further filters.
 * @SAP_ETH_FILTER_COPY: Copy the packet to the CSME.
 * @SAP_ETH_FILTER_ENABLED: If false, the filter should be ignored.
 */
enum iwl_sap_eth_filter_flags {
	SAP_ETH_FILTER_STOP    = BIT(0),
	SAP_ETH_FILTER_COPY    = BIT(1),
	SAP_ETH_FILTER_ENABLED = BIT(2),
};

/**
 * struct iwl_sap_eth_filter - a L2 filter
 * @mac_address: Address to filter.
 * @flags: See &enum iwl_sap_eth_filter_flags.
 */
struct iwl_sap_eth_filter {
	u8 mac_address[6];
	u8 flags;
} __packed;

/**
 * enum iwl_sap_flex_filter_flags - used in &struct iwl_sap_flex_filter
 * @SAP_FLEX_FILTER_COPY: Pass UDP / TCP packets to CSME.
 * @SAP_FLEX_FILTER_ENABLED: If false, the filter should be ignored.
 * @SAP_FLEX_FILTER_IPV4: Filter requires match on the IP address as well.
 * @SAP_FLEX_FILTER_IPV6: Filter requires match on the IP address as well.
 * @SAP_FLEX_FILTER_TCP: Filter should be applied on TCP packets.
 * @SAP_FLEX_FILTER_UDP: Filter should be applied on UDP packets.
 */
enum iwl_sap_flex_filter_flags {
	SAP_FLEX_FILTER_COPY		= BIT(0),
	SAP_FLEX_FILTER_ENABLED		= BIT(1),
	SAP_FLEX_FILTER_IPV6		= BIT(2),
	SAP_FLEX_FILTER_IPV4		= BIT(3),
	SAP_FLEX_FILTER_TCP		= BIT(4),
	SAP_FLEX_FILTER_UDP		= BIT(5),
};

/**
 * struct iwl_sap_flex_filter -
 * @src_port: Source port in network format.
 * @dst_port: Destination port in network format.
 * @flags: Flags and protocol, see &enum iwl_sap_flex_filter_flags.
 * @reserved: For alignment.
 */
struct iwl_sap_flex_filter {
	__be16 src_port;
	__be16 dst_port;
	u8 flags;
	u8 reserved;
} __packed;

/**
 * enum iwl_sap_ipv4_filter_flags - used in &struct iwl_sap_ipv4_filter
 * @SAP_IPV4_FILTER_ICMP_PASS: Pass ICMP packets to CSME.
 * @SAP_IPV4_FILTER_ICMP_COPY: Pass ICMP packets to host.
 * @SAP_IPV4_FILTER_ARP_REQ_PASS: Pass ARP requests to CSME.
 * @SAP_IPV4_FILTER_ARP_REQ_COPY: Pass ARP requests to host.
 * @SAP_IPV4_FILTER_ARP_RESP_PASS: Pass ARP responses to CSME.
 * @SAP_IPV4_FILTER_ARP_RESP_COPY: Pass ARP responses to host.
 */
enum iwl_sap_ipv4_filter_flags {
	SAP_IPV4_FILTER_ICMP_PASS	= BIT(0),
	SAP_IPV4_FILTER_ICMP_COPY	= BIT(1),
	SAP_IPV4_FILTER_ARP_REQ_PASS	= BIT(2),
	SAP_IPV4_FILTER_ARP_REQ_COPY	= BIT(3),
	SAP_IPV4_FILTER_ARP_RESP_PASS	= BIT(4),
	SAP_IPV4_FILTER_ARP_RESP_COPY	= BIT(5),
};

/**
 * struct iwl_sap_ipv4_filter-
 * @ipv4_addr: The IP address to filer.
 * @flags: See &enum iwl_sap_ipv4_filter_flags.
 */
struct iwl_sap_ipv4_filter {
	__be32 ipv4_addr;
	__le32 flags;
} __packed;

/**
 * enum iwl_sap_ipv6_filter_flags -
 * @SAP_IPV6_ADDR_FILTER_COPY: Pass packets to the host.
 * @SAP_IPV6_ADDR_FILTER_ENABLED: If false, the filter should be ignored.
 */
enum iwl_sap_ipv6_filter_flags {
	SAP_IPV6_ADDR_FILTER_COPY	= BIT(0),
	SAP_IPV6_ADDR_FILTER_ENABLED	= BIT(1),
};

/**
 * struct iwl_sap_ipv6_filter -
 * @addr_lo24: Lowest 24 bits of the IPv6 address.
 * @flags: See &enum iwl_sap_ipv6_filter_flags.
 */
struct iwl_sap_ipv6_filter {
	u8 addr_lo24[3];
	u8 flags;
} __packed;

/**
 * enum iwl_sap_icmpv6_filter_flags -
 * @SAP_ICMPV6_FILTER_ENABLED: If false, the filter should be ignored.
 * @SAP_ICMPV6_FILTER_COPY: Pass packets to the host.
 */
enum iwl_sap_icmpv6_filter_flags {
	SAP_ICMPV6_FILTER_ENABLED	= BIT(0),
	SAP_ICMPV6_FILTER_COPY		= BIT(1),
};

/**
 * enum iwl_sap_vlan_filter_flags -
 * @SAP_VLAN_FILTER_VLAN_ID_MSK: TBD
 * @SAP_VLAN_FILTER_ENABLED: If false, the filter should be ignored.
 */
enum iwl_sap_vlan_filter_flags {
	SAP_VLAN_FILTER_VLAN_ID_MSK	= 0x0FFF,
	SAP_VLAN_FILTER_ENABLED		= BIT(15),
};

/**
 * struct iwl_sap_oob_filters - Out of band filters (for RX only)
 * @flex_filters: Array of &struct iwl_sap_flex_filter.
 * @icmpv6_flags: See &enum iwl_sap_icmpv6_filter_flags.
 * @ipv6_filters: Array of &struct iwl_sap_ipv6_filter.
 * @eth_filters: Array of &struct iwl_sap_eth_filter.
 * @reserved: For alignment.
 * @ipv4_filter: &struct iwl_sap_ipv4_filter.
 * @vlan: See &enum iwl_sap_vlan_filter_flags.
 */
struct iwl_sap_oob_filters {
	struct iwl_sap_flex_filter flex_filters[14];
	__le32 icmpv6_flags;
	struct iwl_sap_ipv6_filter ipv6_filters[4];
	struct iwl_sap_eth_filter eth_filters[5];
	u8 reserved;
	struct iwl_sap_ipv4_filter ipv4_filter;
	__le16 vlan[4];
} __packed;

/**
 * struct iwl_sap_csme_filters - payload of %SAP_MSG_NOTIF_CSME_FILTERS
 * @hdr: The SAP header.
 * @mode: Not used.
 * @mac_address: Not used.
 * @reserved: For alignment.
 * @cbfilters: Not used.
 * @filters: Out of band filters.
 */
struct iwl_sap_csme_filters {
	struct iwl_sap_hdr hdr;
	__le32 mode;
	u8 mac_address[6];
	__le16 reserved;
	u8 cbfilters[1728];
	struct iwl_sap_oob_filters filters;
} __packed;

#define CB_TX_DHCP_FILT_IDX 30
/**
 * struct iwl_sap_cb_data - header to be added for transmitted packets.
 * @hdr: The SAP header.
 * @reserved: Not used.
 * @to_me_filt_status: The filter that matches. Bit %CB_TX_DHCP_FILT_IDX should
 *	be set for DHCP (the only packet that uses this header).
 * @reserved2: Not used.
 * @data_len: The length of the payload.
 * @payload: The payload of the transmitted packet.
 */
struct iwl_sap_cb_data {
	struct iwl_sap_hdr hdr;
	__le32 reserved[7];
	__le32 to_me_filt_status;
	__le32 reserved2;
	__le32 data_len;
	u8 payload[];
};

#endif /* __sap_h__ */
