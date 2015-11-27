/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
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

#ifndef __fw_api_scan_h__
#define __fw_api_scan_h__

#include "fw-api.h"

/* Scan Commands, Responses, Notifications */

/* Max number of IEs for direct SSID scans in a command */
#define PROBE_OPTION_MAX		20

/**
 * struct iwl_ssid_ie - directed scan network information element
 *
 * Up to 20 of these may appear in REPLY_SCAN_CMD,
 * selected by "type" bit field in struct iwl_scan_channel;
 * each channel may select different ssids from among the 20 entries.
 * SSID IEs get transmitted in reverse order of entry.
 */
struct iwl_ssid_ie {
	u8 id;
	u8 len;
	u8 ssid[IEEE80211_MAX_SSID_LEN];
} __packed; /* SCAN_DIRECT_SSID_IE_API_S_VER_1 */

/* scan offload */
#define IWL_SCAN_MAX_BLACKLIST_LEN	64
#define IWL_SCAN_SHORT_BLACKLIST_LEN	16
#define IWL_SCAN_MAX_PROFILES		11
#define SCAN_OFFLOAD_PROBE_REQ_SIZE	512

/* Default watchdog (in MS) for scheduled scan iteration */
#define IWL_SCHED_SCAN_WATCHDOG cpu_to_le16(15000)

#define IWL_GOOD_CRC_TH_DEFAULT cpu_to_le16(1)
#define CAN_ABORT_STATUS 1

#define IWL_FULL_SCAN_MULTIPLIER 5
#define IWL_FAST_SCHED_SCAN_ITERATIONS 3
#define IWL_MAX_SCHED_SCAN_PLANS 2

enum scan_framework_client {
	SCAN_CLIENT_SCHED_SCAN		= BIT(0),
	SCAN_CLIENT_NETDETECT		= BIT(1),
	SCAN_CLIENT_ASSET_TRACKING	= BIT(2),
};

/**
 * iwl_scan_offload_blacklist - SCAN_OFFLOAD_BLACKLIST_S
 * @ssid:		MAC address to filter out
 * @reported_rssi:	AP rssi reported to the host
 * @client_bitmap: clients ignore this entry  - enum scan_framework_client
 */
struct iwl_scan_offload_blacklist {
	u8 ssid[ETH_ALEN];
	u8 reported_rssi;
	u8 client_bitmap;
} __packed;

enum iwl_scan_offload_network_type {
	IWL_NETWORK_TYPE_BSS	= 1,
	IWL_NETWORK_TYPE_IBSS	= 2,
	IWL_NETWORK_TYPE_ANY	= 3,
};

enum iwl_scan_offload_band_selection {
	IWL_SCAN_OFFLOAD_SELECT_2_4	= 0x4,
	IWL_SCAN_OFFLOAD_SELECT_5_2	= 0x8,
	IWL_SCAN_OFFLOAD_SELECT_ANY	= 0xc,
};

/**
 * iwl_scan_offload_profile - SCAN_OFFLOAD_PROFILE_S
 * @ssid_index:		index to ssid list in fixed part
 * @unicast_cipher:	encryption algorithm to match - bitmap
 * @aut_alg:		authentication algorithm to match - bitmap
 * @network_type:	enum iwl_scan_offload_network_type
 * @band_selection:	enum iwl_scan_offload_band_selection
 * @client_bitmap:	clients waiting for match - enum scan_framework_client
 */
struct iwl_scan_offload_profile {
	u8 ssid_index;
	u8 unicast_cipher;
	u8 auth_alg;
	u8 network_type;
	u8 band_selection;
	u8 client_bitmap;
	u8 reserved[2];
} __packed;

/**
 * iwl_scan_offload_profile_cfg - SCAN_OFFLOAD_PROFILES_CFG_API_S_VER_1
 * @blaclist:		AP list to filter off from scan results
 * @profiles:		profiles to search for match
 * @blacklist_len:	length of blacklist
 * @num_profiles:	num of profiles in the list
 * @match_notify:	clients waiting for match found notification
 * @pass_match:		clients waiting for the results
 * @active_clients:	active clients bitmap - enum scan_framework_client
 * @any_beacon_notify:	clients waiting for match notification without match
 */
struct iwl_scan_offload_profile_cfg {
	struct iwl_scan_offload_profile profiles[IWL_SCAN_MAX_PROFILES];
	u8 blacklist_len;
	u8 num_profiles;
	u8 match_notify;
	u8 pass_match;
	u8 active_clients;
	u8 any_beacon_notify;
	u8 reserved[2];
} __packed;

/**
 * iwl_scan_schedule_lmac - schedule of scan offload
 * @delay:		delay between iterations, in seconds.
 * @iterations:		num of scan iterations
 * @full_scan_mul:	number of partial scans before each full scan
 */
struct iwl_scan_schedule_lmac {
	__le16 delay;
	u8 iterations;
	u8 full_scan_mul;
} __packed; /* SCAN_SCHEDULE_API_S */

enum iwl_scan_offload_complete_status {
	IWL_SCAN_OFFLOAD_COMPLETED	= 1,
	IWL_SCAN_OFFLOAD_ABORTED	= 2,
};

enum iwl_scan_ebs_status {
	IWL_SCAN_EBS_SUCCESS,
	IWL_SCAN_EBS_FAILED,
	IWL_SCAN_EBS_CHAN_NOT_FOUND,
	IWL_SCAN_EBS_INACTIVE,
};

/**
 * iwl_scan_req_tx_cmd - SCAN_REQ_TX_CMD_API_S
 * @tx_flags: combination of TX_CMD_FLG_*
 * @rate_n_flags: rate for *all* Tx attempts, if TX_CMD_FLG_STA_RATE_MSK is
 *	cleared. Combination of RATE_MCS_*
 * @sta_id: index of destination station in FW station table
 * @reserved: for alignment and future use
 */
struct iwl_scan_req_tx_cmd {
	__le32 tx_flags;
	__le32 rate_n_flags;
	u8 sta_id;
	u8 reserved[3];
} __packed;

enum iwl_scan_channel_flags_lmac {
	IWL_UNIFIED_SCAN_CHANNEL_FULL		= BIT(27),
	IWL_UNIFIED_SCAN_CHANNEL_PARTIAL	= BIT(28),
};

/**
 * iwl_scan_channel_cfg_lmac - SCAN_CHANNEL_CFG_S_VER2
 * @flags:		bits 1-20: directed scan to i'th ssid
 *			other bits &enum iwl_scan_channel_flags_lmac
 * @channel_number:	channel number 1-13 etc
 * @iter_count:		scan iteration on this channel
 * @iter_interval:	interval in seconds between iterations on one channel
 */
struct iwl_scan_channel_cfg_lmac {
	__le32 flags;
	__le16 channel_num;
	__le16 iter_count;
	__le32 iter_interval;
} __packed;

/*
 * iwl_scan_probe_segment - PROBE_SEGMENT_API_S_VER_1
 * @offset: offset in the data block
 * @len: length of the segment
 */
struct iwl_scan_probe_segment {
	__le16 offset;
	__le16 len;
} __packed;

/* iwl_scan_probe_req - PROBE_REQUEST_FRAME_API_S_VER_2
 * @mac_header: first (and common) part of the probe
 * @band_data: band specific data
 * @common_data: last (and common) part of the probe
 * @buf: raw data block
 */
struct iwl_scan_probe_req {
	struct iwl_scan_probe_segment mac_header;
	struct iwl_scan_probe_segment band_data[2];
	struct iwl_scan_probe_segment common_data;
	u8 buf[SCAN_OFFLOAD_PROBE_REQ_SIZE];
} __packed;

enum iwl_scan_channel_flags {
	IWL_SCAN_CHANNEL_FLAG_EBS		= BIT(0),
	IWL_SCAN_CHANNEL_FLAG_EBS_ACCURATE	= BIT(1),
	IWL_SCAN_CHANNEL_FLAG_CACHE_ADD		= BIT(2),
};

/* iwl_scan_channel_opt - CHANNEL_OPTIMIZATION_API_S
 * @flags: enum iwl_scan_channel_flags
 * @non_ebs_ratio: defines the ratio of number of scan iterations where EBS is
 *	involved.
 *	1 - EBS is disabled.
 *	2 - every second scan will be full scan(and so on).
 */
struct iwl_scan_channel_opt {
	__le16 flags;
	__le16 non_ebs_ratio;
} __packed;

/**
 * iwl_mvm_lmac_scan_flags
 * @IWL_MVM_LMAC_SCAN_FLAG_PASS_ALL: pass all beacons and probe responses
 *	without filtering.
 * @IWL_MVM_LMAC_SCAN_FLAG_PASSIVE: force passive scan on all channels
 * @IWL_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION: single channel scan
 * @IWL_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE: send iteration complete notification
 * @IWL_MVM_LMAC_SCAN_FLAG_MULTIPLE_SSIDS multiple SSID matching
 * @IWL_MVM_LMAC_SCAN_FLAG_FRAGMENTED: all passive scans will be fragmented
 * @IWL_MVM_LMAC_SCAN_FLAGS_RRM_ENABLED: insert WFA vendor-specific TPC report
 *	and DS parameter set IEs into probe requests.
 * @IWL_MVM_LMAC_SCAN_FLAG_MATCH: Send match found notification on matches
 */
enum iwl_mvm_lmac_scan_flags {
	IWL_MVM_LMAC_SCAN_FLAG_PASS_ALL		= BIT(0),
	IWL_MVM_LMAC_SCAN_FLAG_PASSIVE		= BIT(1),
	IWL_MVM_LMAC_SCAN_FLAG_PRE_CONNECTION	= BIT(2),
	IWL_MVM_LMAC_SCAN_FLAG_ITER_COMPLETE	= BIT(3),
	IWL_MVM_LMAC_SCAN_FLAG_MULTIPLE_SSIDS	= BIT(4),
	IWL_MVM_LMAC_SCAN_FLAG_FRAGMENTED	= BIT(5),
	IWL_MVM_LMAC_SCAN_FLAGS_RRM_ENABLED	= BIT(6),
	IWL_MVM_LMAC_SCAN_FLAG_MATCH		= BIT(9),
};

enum iwl_scan_priority {
	IWL_SCAN_PRIORITY_LOW,
	IWL_SCAN_PRIORITY_MEDIUM,
	IWL_SCAN_PRIORITY_HIGH,
};

enum iwl_scan_priority_ext {
	IWL_SCAN_PRIORITY_EXT_0_LOWEST,
	IWL_SCAN_PRIORITY_EXT_1,
	IWL_SCAN_PRIORITY_EXT_2,
	IWL_SCAN_PRIORITY_EXT_3,
	IWL_SCAN_PRIORITY_EXT_4,
	IWL_SCAN_PRIORITY_EXT_5,
	IWL_SCAN_PRIORITY_EXT_6,
	IWL_SCAN_PRIORITY_EXT_7_HIGHEST,
};

/**
 * iwl_scan_req_lmac - SCAN_REQUEST_CMD_API_S_VER_1
 * @reserved1: for alignment and future use
 * @channel_num: num of channels to scan
 * @active-dwell: dwell time for active channels
 * @passive-dwell: dwell time for passive channels
 * @fragmented-dwell: dwell time for fragmented passive scan
 * @reserved2: for alignment and future use
 * @rx_chain_selct: PHY_RX_CHAIN_* flags
 * @scan_flags: &enum iwl_mvm_lmac_scan_flags
 * @max_out_time: max time (in TU) to be out of associated channel
 * @suspend_time: pause scan this long (TUs) when returning to service channel
 * @flags: RXON flags
 * @filter_flags: RXON filter
 * @tx_cmd: tx command for active scan; for 2GHz and for 5GHz
 * @direct_scan: list of SSIDs for directed active scan
 * @scan_prio: enum iwl_scan_priority
 * @iter_num: number of scan iterations
 * @delay: delay in seconds before first iteration
 * @schedule: two scheduling plans. The first one is finite, the second one can
 *	be infinite.
 * @channel_opt: channel optimization options, for full and partial scan
 * @data: channel configuration and probe request packet.
 */
struct iwl_scan_req_lmac {
	/* SCAN_REQUEST_FIXED_PART_API_S_VER_7 */
	__le32 reserved1;
	u8 n_channels;
	u8 active_dwell;
	u8 passive_dwell;
	u8 fragmented_dwell;
	__le16 reserved2;
	__le16 rx_chain_select;
	__le32 scan_flags;
	__le32 max_out_time;
	__le32 suspend_time;
	/* RX_ON_FLAGS_API_S_VER_1 */
	__le32 flags;
	__le32 filter_flags;
	struct iwl_scan_req_tx_cmd tx_cmd[2];
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];
	__le32 scan_prio;
	/* SCAN_REQ_PERIODIC_PARAMS_API_S */
	__le32 iter_num;
	__le32 delay;
	struct iwl_scan_schedule_lmac schedule[IWL_MAX_SCHED_SCAN_PLANS];
	struct iwl_scan_channel_opt channel_opt[2];
	u8 data[];
} __packed;

/**
 * struct iwl_scan_results_notif - scan results for one channel -
 *	SCAN_RESULT_NTF_API_S_VER_3
 * @channel: which channel the results are from
 * @band: 0 for 5.2 GHz, 1 for 2.4 GHz
 * @probe_status: SCAN_PROBE_STATUS_*, indicates success of probe request
 * @num_probe_not_sent: # of request that weren't sent due to not enough time
 * @duration: duration spent in channel, in usecs
 */
struct iwl_scan_results_notif {
	u8 channel;
	u8 band;
	u8 probe_status;
	u8 num_probe_not_sent;
	__le32 duration;
} __packed;

/**
 * struct iwl_lmac_scan_complete_notif - notifies end of scanning (all channels)
 *	SCAN_COMPLETE_NTF_API_S_VER_3
 * @scanned_channels: number of channels scanned (and number of valid results)
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: an array of scan results, only "scanned_channels" of them are valid
 */
struct iwl_lmac_scan_complete_notif {
	u8 scanned_channels;
	u8 status;
	u8 bt_status;
	u8 last_channel;
	__le32 tsf_low;
	__le32 tsf_high;
	struct iwl_scan_results_notif results[];
} __packed;

/**
 * iwl_scan_offload_complete - PERIODIC_SCAN_COMPLETE_NTF_API_S_VER_2
 * @last_schedule_line: last schedule line executed (fast or regular)
 * @last_schedule_iteration: last scan iteration executed before scan abort
 * @status: enum iwl_scan_offload_complete_status
 * @ebs_status: EBS success status &enum iwl_scan_ebs_status
 * @time_after_last_iter; time in seconds elapsed after last iteration
 */
struct iwl_periodic_scan_complete {
	u8 last_schedule_line;
	u8 last_schedule_iteration;
	u8 status;
	u8 ebs_status;
	__le32 time_after_last_iter;
	__le32 reserved;
} __packed;

/* UMAC Scan API */

/* The maximum of either of these cannot exceed 8, because we use an
 * 8-bit mask (see IWL_MVM_SCAN_MASK in mvm.h).
 */
#define IWL_MVM_MAX_UMAC_SCANS 8
#define IWL_MVM_MAX_LMAC_SCANS 1

enum scan_config_flags {
	SCAN_CONFIG_FLAG_ACTIVATE			= BIT(0),
	SCAN_CONFIG_FLAG_DEACTIVATE			= BIT(1),
	SCAN_CONFIG_FLAG_FORBID_CHUB_REQS		= BIT(2),
	SCAN_CONFIG_FLAG_ALLOW_CHUB_REQS		= BIT(3),
	SCAN_CONFIG_FLAG_SET_TX_CHAINS			= BIT(8),
	SCAN_CONFIG_FLAG_SET_RX_CHAINS			= BIT(9),
	SCAN_CONFIG_FLAG_SET_AUX_STA_ID			= BIT(10),
	SCAN_CONFIG_FLAG_SET_ALL_TIMES			= BIT(11),
	SCAN_CONFIG_FLAG_SET_EFFECTIVE_TIMES		= BIT(12),
	SCAN_CONFIG_FLAG_SET_CHANNEL_FLAGS		= BIT(13),
	SCAN_CONFIG_FLAG_SET_LEGACY_RATES		= BIT(14),
	SCAN_CONFIG_FLAG_SET_MAC_ADDR			= BIT(15),
	SCAN_CONFIG_FLAG_SET_FRAGMENTED			= BIT(16),
	SCAN_CONFIG_FLAG_CLEAR_FRAGMENTED		= BIT(17),
	SCAN_CONFIG_FLAG_SET_CAM_MODE			= BIT(18),
	SCAN_CONFIG_FLAG_CLEAR_CAM_MODE			= BIT(19),
	SCAN_CONFIG_FLAG_SET_PROMISC_MODE		= BIT(20),
	SCAN_CONFIG_FLAG_CLEAR_PROMISC_MODE		= BIT(21),

	/* Bits 26-31 are for num of channels in channel_array */
#define SCAN_CONFIG_N_CHANNELS(n) ((n) << 26)
};

enum scan_config_rates {
	/* OFDM basic rates */
	SCAN_CONFIG_RATE_6M	= BIT(0),
	SCAN_CONFIG_RATE_9M	= BIT(1),
	SCAN_CONFIG_RATE_12M	= BIT(2),
	SCAN_CONFIG_RATE_18M	= BIT(3),
	SCAN_CONFIG_RATE_24M	= BIT(4),
	SCAN_CONFIG_RATE_36M	= BIT(5),
	SCAN_CONFIG_RATE_48M	= BIT(6),
	SCAN_CONFIG_RATE_54M	= BIT(7),
	/* CCK basic rates */
	SCAN_CONFIG_RATE_1M	= BIT(8),
	SCAN_CONFIG_RATE_2M	= BIT(9),
	SCAN_CONFIG_RATE_5M	= BIT(10),
	SCAN_CONFIG_RATE_11M	= BIT(11),

	/* Bits 16-27 are for supported rates */
#define SCAN_CONFIG_SUPPORTED_RATE(rate)	((rate) << 16)
};

enum iwl_channel_flags {
	IWL_CHANNEL_FLAG_EBS				= BIT(0),
	IWL_CHANNEL_FLAG_ACCURATE_EBS			= BIT(1),
	IWL_CHANNEL_FLAG_EBS_ADD			= BIT(2),
	IWL_CHANNEL_FLAG_PRE_SCAN_PASSIVE2ACTIVE	= BIT(3),
};

/**
 * struct iwl_scan_config
 * @flags:			enum scan_config_flags
 * @tx_chains:			valid_tx antenna - ANT_* definitions
 * @rx_chains:			valid_rx antenna - ANT_* definitions
 * @legacy_rates:		default legacy rates - enum scan_config_rates
 * @out_of_channel_time:	default max out of serving channel time
 * @suspend_time:		default max suspend time
 * @dwell_active:		default dwell time for active scan
 * @dwell_passive:		default dwell time for passive scan
 * @dwell_fragmented:		default dwell time for fragmented scan
 * @reserved:			for future use and alignment
 * @mac_addr:			default mac address to be used in probes
 * @bcast_sta_id:		the index of the station in the fw
 * @channel_flags:		default channel flags - enum iwl_channel_flags
 *				scan_config_channel_flag
 * @channel_array:		default supported channels
 */
struct iwl_scan_config {
	__le32 flags;
	__le32 tx_chains;
	__le32 rx_chains;
	__le32 legacy_rates;
	__le32 out_of_channel_time;
	__le32 suspend_time;
	u8 dwell_active;
	u8 dwell_passive;
	u8 dwell_fragmented;
	u8 reserved;
	u8 mac_addr[ETH_ALEN];
	u8 bcast_sta_id;
	u8 channel_flags;
	u8 channel_array[];
} __packed; /* SCAN_CONFIG_DB_CMD_API_S */

/**
 * iwl_umac_scan_flags
 *@IWL_UMAC_SCAN_FLAG_PREEMPTIVE: scan process triggered by this scan request
 *	can be preempted by other scan requests with higher priority.
 *	The low priority scan will be resumed when the higher proirity scan is
 *	completed.
 *@IWL_UMAC_SCAN_FLAG_START_NOTIF: notification will be sent to the driver
 *	when scan starts.
 */
enum iwl_umac_scan_flags {
	IWL_UMAC_SCAN_FLAG_PREEMPTIVE		= BIT(0),
	IWL_UMAC_SCAN_FLAG_START_NOTIF		= BIT(1),
};

enum iwl_umac_scan_uid_offsets {
	IWL_UMAC_SCAN_UID_TYPE_OFFSET		= 0,
	IWL_UMAC_SCAN_UID_SEQ_OFFSET		= 8,
};

enum iwl_umac_scan_general_flags {
	IWL_UMAC_SCAN_GEN_FLAGS_PERIODIC	= BIT(0),
	IWL_UMAC_SCAN_GEN_FLAGS_OVER_BT		= BIT(1),
	IWL_UMAC_SCAN_GEN_FLAGS_PASS_ALL	= BIT(2),
	IWL_UMAC_SCAN_GEN_FLAGS_PASSIVE		= BIT(3),
	IWL_UMAC_SCAN_GEN_FLAGS_PRE_CONNECT	= BIT(4),
	IWL_UMAC_SCAN_GEN_FLAGS_ITER_COMPLETE	= BIT(5),
	IWL_UMAC_SCAN_GEN_FLAGS_MULTIPLE_SSID	= BIT(6),
	IWL_UMAC_SCAN_GEN_FLAGS_FRAGMENTED	= BIT(7),
	IWL_UMAC_SCAN_GEN_FLAGS_RRM_ENABLED	= BIT(8),
	IWL_UMAC_SCAN_GEN_FLAGS_MATCH		= BIT(9)
};

/**
 * struct iwl_scan_channel_cfg_umac
 * @flags:		bitmap - 0-19:	directed scan to i'th ssid.
 * @channel_num:	channel number 1-13 etc.
 * @iter_count:		repetition count for the channel.
 * @iter_interval:	interval between two scan iterations on one channel.
 */
struct iwl_scan_channel_cfg_umac {
	__le32 flags;
	u8 channel_num;
	u8 iter_count;
	__le16 iter_interval;
} __packed; /* SCAN_CHANNEL_CFG_S_VER2 */

/**
 * struct iwl_scan_umac_schedule
 * @interval: interval in seconds between scan iterations
 * @iter_count: num of scan iterations for schedule plan, 0xff for infinite loop
 * @reserved: for alignment and future use
 */
struct iwl_scan_umac_schedule {
	__le16 interval;
	u8 iter_count;
	u8 reserved;
} __packed; /* SCAN_SCHED_PARAM_API_S_VER_1 */

/**
 * struct iwl_scan_req_umac_tail - the rest of the UMAC scan request command
 *      parameters following channels configuration array.
 * @schedule: two scheduling plans.
 * @delay: delay in TUs before starting the first scan iteration
 * @reserved: for future use and alignment
 * @preq: probe request with IEs blocks
 * @direct_scan: list of SSIDs for directed active scan
 */
struct iwl_scan_req_umac_tail {
	/* SCAN_PERIODIC_PARAMS_API_S_VER_1 */
	struct iwl_scan_umac_schedule schedule[IWL_MAX_SCHED_SCAN_PLANS];
	__le16 delay;
	__le16 reserved;
	/* SCAN_PROBE_PARAMS_API_S_VER_1 */
	struct iwl_scan_probe_req preq;
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];
} __packed;

/**
 * struct iwl_scan_req_umac
 * @flags: &enum iwl_umac_scan_flags
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @ooc_priority: out of channel priority - &enum iwl_scan_priority
 * @general_flags: &enum iwl_umac_scan_general_flags
 * @reserved1: for future use and alignment
 * @active_dwell: dwell time for active scan
 * @passive_dwell: dwell time for passive scan
 * @fragmented_dwell: dwell time for fragmented passive scan
 * @max_out_time: max out of serving channel time
 * @suspend_time: max suspend time
 * @scan_priority: scan internal prioritization &enum iwl_scan_priority
 * @channel_flags: &enum iwl_scan_channel_flags
 * @n_channels: num of channels in scan request
 * @reserved2: for future use and alignment
 * @data: &struct iwl_scan_channel_cfg_umac and
 *	&struct iwl_scan_req_umac_tail
 */
struct iwl_scan_req_umac {
	__le32 flags;
	__le32 uid;
	__le32 ooc_priority;
	/* SCAN_GENERAL_PARAMS_API_S_VER_1 */
	__le32 general_flags;
	u8 reserved1;
	u8 active_dwell;
	u8 passive_dwell;
	u8 fragmented_dwell;
	__le32 max_out_time;
	__le32 suspend_time;
	__le32 scan_priority;
	/* SCAN_CHANNEL_PARAMS_API_S_VER_1 */
	u8 channel_flags;
	u8 n_channels;
	__le16 reserved2;
	u8 data[];
} __packed; /* SCAN_REQUEST_CMD_UMAC_API_S_VER_1 */

/**
 * struct iwl_umac_scan_abort
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @flags: reserved
 */
struct iwl_umac_scan_abort {
	__le32 uid;
	__le32 flags;
} __packed; /* SCAN_ABORT_CMD_UMAC_API_S_VER_1 */

/**
 * struct iwl_umac_scan_complete
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @last_schedule: last scheduling line
 * @last_iter:	last scan iteration number
 * @scan status: &enum iwl_scan_offload_complete_status
 * @ebs_status: &enum iwl_scan_ebs_status
 * @time_from_last_iter: time elapsed from last iteration
 * @reserved: for future use
 */
struct iwl_umac_scan_complete {
	__le32 uid;
	u8 last_schedule;
	u8 last_iter;
	u8 status;
	u8 ebs_status;
	__le32 time_from_last_iter;
	__le32 reserved;
} __packed; /* SCAN_COMPLETE_NTF_UMAC_API_S_VER_1 */

#define SCAN_OFFLOAD_MATCHING_CHANNELS_LEN 5
/**
 * struct iwl_scan_offload_profile_match - match information
 * @bssid: matched bssid
 * @channel: channel where the match occurred
 * @energy:
 * @matching_feature:
 * @matching_channels: bitmap of channels that matched, referencing
 *	the channels passed in tue scan offload request
 */
struct iwl_scan_offload_profile_match {
	u8 bssid[ETH_ALEN];
	__le16 reserved;
	u8 channel;
	u8 energy;
	u8 matching_feature;
	u8 matching_channels[SCAN_OFFLOAD_MATCHING_CHANNELS_LEN];
} __packed; /* SCAN_OFFLOAD_PROFILE_MATCH_RESULTS_S_VER_1 */

/**
 * struct iwl_scan_offload_profiles_query - match results query response
 * @matched_profiles: bitmap of matched profiles, referencing the
 *	matches passed in the scan offload request
 * @last_scan_age: age of the last offloaded scan
 * @n_scans_done: number of offloaded scans done
 * @gp2_d0u: GP2 when D0U occurred
 * @gp2_invoked: GP2 when scan offload was invoked
 * @resume_while_scanning: not used
 * @self_recovery: obsolete
 * @reserved: reserved
 * @matches: array of match information, one for each match
 */
struct iwl_scan_offload_profiles_query {
	__le32 matched_profiles;
	__le32 last_scan_age;
	__le32 n_scans_done;
	__le32 gp2_d0u;
	__le32 gp2_invoked;
	u8 resume_while_scanning;
	u8 self_recovery;
	__le16 reserved;
	struct iwl_scan_offload_profile_match matches[IWL_SCAN_MAX_PROFILES];
} __packed; /* SCAN_OFFLOAD_PROFILES_QUERY_RSP_S_VER_2 */

/**
 * struct iwl_umac_scan_iter_complete_notif - notifies end of scanning iteration
 * @uid: scan id, &enum iwl_umac_scan_uid_offsets
 * @scanned_channels: number of channels scanned and number of valid elements in
 *	results array
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: array of scan results, only "scanned_channels" of them are valid
 */
struct iwl_umac_scan_iter_complete_notif {
	__le32 uid;
	u8 scanned_channels;
	u8 status;
	u8 bt_status;
	u8 last_channel;
	__le32 tsf_low;
	__le32 tsf_high;
	struct iwl_scan_results_notif results[];
} __packed; /* SCAN_ITER_COMPLETE_NTF_UMAC_API_S_VER_1 */

#endif
