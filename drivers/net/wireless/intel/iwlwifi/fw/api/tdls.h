/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2012-2014, 2018 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#ifndef __iwl_fw_api_tdls_h__
#define __iwl_fw_api_tdls_h__

#include "fw/api/tx.h"
#include "fw/api/phy-ctxt.h"

#define IWL_MVM_TDLS_STA_COUNT	4

/* Type of TDLS request */
enum iwl_tdls_channel_switch_type {
	TDLS_SEND_CHAN_SW_REQ = 0,
	TDLS_SEND_CHAN_SW_RESP_AND_MOVE_CH,
	TDLS_MOVE_CH,
}; /* TDLS_STA_CHANNEL_SWITCH_CMD_TYPE_API_E_VER_1 */

/**
 * struct iwl_tdls_channel_switch_timing - Switch timing in TDLS channel-switch
 * @frame_timestamp: GP2 timestamp of channel-switch request/response packet
 *	received from peer
 * @max_offchan_duration: What amount of microseconds out of a DTIM is given
 *	to the TDLS off-channel communication. For instance if the DTIM is
 *	200TU and the TDLS peer is to be given 25% of the time, the value
 *	given will be 50TU, or 50 * 1024 if translated into microseconds.
 * @switch_time: switch time the peer sent in its channel switch timing IE
 * @switch_timeout: switch timeout the peer sent in its channel switch timing IE
 */
struct iwl_tdls_channel_switch_timing {
	__le32 frame_timestamp; /* GP2 time of peer packet Rx */
	__le32 max_offchan_duration; /* given in micro-seconds */
	__le32 switch_time; /* given in micro-seconds */
	__le32 switch_timeout; /* given in micro-seconds */
} __packed; /* TDLS_STA_CHANNEL_SWITCH_TIMING_DATA_API_S_VER_1 */

#define IWL_TDLS_CH_SW_FRAME_MAX_SIZE 200

/**
 * struct iwl_tdls_channel_switch_frame - TDLS channel switch frame template
 *
 * A template representing a TDLS channel-switch request or response frame
 *
 * @switch_time_offset: offset to the channel switch timing IE in the template
 * @tx_cmd: Tx parameters for the frame
 * @data: frame data
 */
struct iwl_tdls_channel_switch_frame {
	__le32 switch_time_offset;
	struct iwl_tx_cmd tx_cmd;
	u8 data[IWL_TDLS_CH_SW_FRAME_MAX_SIZE];
} __packed; /* TDLS_STA_CHANNEL_SWITCH_FRAME_API_S_VER_1 */

/**
 * struct iwl_tdls_channel_switch_cmd_tail - tail of iwl_tdls_channel_switch_cmd
 *
 * @timing: timing related data for command
 * @frame: channel-switch request/response template, depending to switch_type
 */
struct iwl_tdls_channel_switch_cmd_tail {
	struct iwl_tdls_channel_switch_timing timing;
	struct iwl_tdls_channel_switch_frame frame;
} __packed;

/**
 * struct iwl_tdls_channel_switch_cmd - TDLS channel switch command
 *
 * The command is sent to initiate a channel switch and also in response to
 * incoming TDLS channel-switch request/response packets from remote peers.
 *
 * @switch_type: see &enum iwl_tdls_channel_switch_type
 * @peer_sta_id: station id of TDLS peer
 * @ci: channel we switch to
 * @tail: command tail
 */
struct iwl_tdls_channel_switch_cmd {
	u8 switch_type;
	__le32 peer_sta_id;
	struct iwl_fw_channel_info ci;
	struct iwl_tdls_channel_switch_cmd_tail tail;
} __packed; /* TDLS_STA_CHANNEL_SWITCH_CMD_API_S_VER_1 */

/**
 * struct iwl_tdls_channel_switch_notif - TDLS channel switch start notification
 *
 * @status: non-zero on success
 * @offchannel_duration: duration given in microseconds
 * @sta_id: peer currently performing the channel-switch with
 */
struct iwl_tdls_channel_switch_notif {
	__le32 status;
	__le32 offchannel_duration;
	__le32 sta_id;
} __packed; /* TDLS_STA_CHANNEL_SWITCH_NTFY_API_S_VER_1 */

/**
 * struct iwl_tdls_sta_info - TDLS station info
 *
 * @sta_id: station id of the TDLS peer
 * @tx_to_peer_tid: TID reserved vs. the peer for FW based Tx
 * @tx_to_peer_ssn: initial SSN the FW should use for Tx on its TID vs the peer
 * @is_initiator: 1 if the peer is the TDLS link initiator, 0 otherwise
 */
struct iwl_tdls_sta_info {
	u8 sta_id;
	u8 tx_to_peer_tid;
	__le16 tx_to_peer_ssn;
	__le32 is_initiator;
} __packed; /* TDLS_STA_INFO_VER_1 */

/**
 * struct iwl_tdls_config_cmd - TDLS basic config command
 *
 * @id_and_color: MAC id and color being configured
 * @tdls_peer_count: amount of currently connected TDLS peers
 * @tx_to_ap_tid: TID reverved vs. the AP for FW based Tx
 * @tx_to_ap_ssn: initial SSN the FW should use for Tx on its TID vs. the AP
 * @sta_info: per-station info. Only the first tdls_peer_count entries are set
 * @pti_req_data_offset: offset of network-level data for the PTI template
 * @pti_req_tx_cmd: Tx parameters for PTI request template
 * @pti_req_template: PTI request template data
 */
struct iwl_tdls_config_cmd {
	__le32 id_and_color; /* mac id and color */
	u8 tdls_peer_count;
	u8 tx_to_ap_tid;
	__le16 tx_to_ap_ssn;
	struct iwl_tdls_sta_info sta_info[IWL_MVM_TDLS_STA_COUNT];

	__le32 pti_req_data_offset;
	struct iwl_tx_cmd pti_req_tx_cmd;
	u8 pti_req_template[];
} __packed; /* TDLS_CONFIG_CMD_API_S_VER_1 */

/**
 * struct iwl_tdls_config_sta_info_res - TDLS per-station config information
 *
 * @sta_id: station id of the TDLS peer
 * @tx_to_peer_last_seq: last sequence number used by FW during FW-based Tx to
 *	the peer
 */
struct iwl_tdls_config_sta_info_res {
	__le16 sta_id;
	__le16 tx_to_peer_last_seq;
} __packed; /* TDLS_STA_INFO_RSP_VER_1 */

/**
 * struct iwl_tdls_config_res - TDLS config information from FW
 *
 * @tx_to_ap_last_seq: last sequence number used by FW during FW-based Tx to AP
 * @sta_info: per-station TDLS config information
 */
struct iwl_tdls_config_res {
	__le32 tx_to_ap_last_seq;
	struct iwl_tdls_config_sta_info_res sta_info[IWL_MVM_TDLS_STA_COUNT];
} __packed; /* TDLS_CONFIG_RSP_API_S_VER_1 */

#endif /* __iwl_fw_api_tdls_h__ */
