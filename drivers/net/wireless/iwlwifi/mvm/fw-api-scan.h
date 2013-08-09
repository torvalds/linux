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
 *
 *****************************************************************************/

#ifndef __fw_api_scan_h__
#define __fw_api_scan_h__

#include "fw-api.h"

/* Scan Commands, Responses, Notifications */

/* Masks for iwl_scan_channel.type flags */
#define SCAN_CHANNEL_TYPE_ACTIVE	BIT(0)
#define SCAN_CHANNEL_NARROW_BAND	BIT(22)

/* Max number of IEs for direct SSID scans in a command */
#define PROBE_OPTION_MAX		20

/**
 * struct iwl_scan_channel - entry in REPLY_SCAN_CMD channel table
 * @channel: band is selected by iwl_scan_cmd "flags" field
 * @tx_gain: gain for analog radio
 * @dsp_atten: gain for DSP
 * @active_dwell: dwell time for active scan in TU, typically 5-50
 * @passive_dwell: dwell time for passive scan in TU, typically 20-500
 * @type: type is broken down to these bits:
 *	bit 0: 0 = passive, 1 = active
 *	bits 1-20: SSID direct bit map. If any of these bits is set then
 *		the corresponding SSID IE is transmitted in probe request
 *		(bit i adds IE in position i to the probe request)
 *	bit 22: channel width, 0 = regular, 1 = TGj narrow channel
 *
 * @iteration_count:
 * @iteration_interval:
 * This struct is used once for each channel in the scan list.
 * Each channel can independently select:
 * 1)  SSID for directed active scans
 * 2)  Txpower setting (for rate specified within Tx command)
 * 3)  How long to stay on-channel (behavior may be modified by quiet_time,
 *     quiet_plcp_th, good_CRC_th)
 *
 * To avoid uCode errors, make sure the following are true (see comments
 * under struct iwl_scan_cmd about max_out_time and quiet_time):
 * 1)  If using passive_dwell (i.e. passive_dwell != 0):
 *     active_dwell <= passive_dwell (< max_out_time if max_out_time != 0)
 * 2)  quiet_time <= active_dwell
 * 3)  If restricting off-channel time (i.e. max_out_time !=0):
 *     passive_dwell < max_out_time
 *     active_dwell < max_out_time
 */
struct iwl_scan_channel {
	__le32 type;
	__le16 channel;
	__le16 iteration_count;
	__le32 iteration_interval;
	__le16 active_dwell;
	__le16 passive_dwell;
} __packed; /* SCAN_CHANNEL_CONTROL_API_S_VER_1 */

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

/**
 * iwl_scan_flags - masks for scan command flags
 *@SCAN_FLAGS_PERIODIC_SCAN:
 *@SCAN_FLAGS_P2P_PUBLIC_ACTION_FRAME_TX:
 *@SCAN_FLAGS_DELAYED_SCAN_LOWBAND:
 *@SCAN_FLAGS_DELAYED_SCAN_HIGHBAND:
 *@SCAN_FLAGS_FRAGMENTED_SCAN:
 *@SCAN_FLAGS_PASSIVE2ACTIVE: use active scan on channels that was active
 *	in the past hour, even if they are marked as passive.
 */
enum iwl_scan_flags {
	SCAN_FLAGS_PERIODIC_SCAN		= BIT(0),
	SCAN_FLAGS_P2P_PUBLIC_ACTION_FRAME_TX	= BIT(1),
	SCAN_FLAGS_DELAYED_SCAN_LOWBAND		= BIT(2),
	SCAN_FLAGS_DELAYED_SCAN_HIGHBAND	= BIT(3),
	SCAN_FLAGS_FRAGMENTED_SCAN		= BIT(4),
	SCAN_FLAGS_PASSIVE2ACTIVE		= BIT(5),
};

/**
 * enum iwl_scan_type - Scan types for scan command
 * @SCAN_TYPE_FORCED:
 * @SCAN_TYPE_BACKGROUND:
 * @SCAN_TYPE_OS:
 * @SCAN_TYPE_ROAMING:
 * @SCAN_TYPE_ACTION:
 * @SCAN_TYPE_DISCOVERY:
 * @SCAN_TYPE_DISCOVERY_FORCED:
 */
enum iwl_scan_type {
	SCAN_TYPE_FORCED		= 0,
	SCAN_TYPE_BACKGROUND		= 1,
	SCAN_TYPE_OS			= 2,
	SCAN_TYPE_ROAMING		= 3,
	SCAN_TYPE_ACTION		= 4,
	SCAN_TYPE_DISCOVERY		= 5,
	SCAN_TYPE_DISCOVERY_FORCED	= 6,
}; /* SCAN_ACTIVITY_TYPE_E_VER_1 */

/* Maximal number of channels to scan */
#define MAX_NUM_SCAN_CHANNELS 0x24

/**
 * struct iwl_scan_cmd - scan request command
 * ( SCAN_REQUEST_CMD = 0x80 )
 * @len: command length in bytes
 * @scan_flags: scan flags from SCAN_FLAGS_*
 * @channel_count: num of channels in channel list (1 - MAX_NUM_SCAN_CHANNELS)
 * @quiet_time: in msecs, dwell this time for active scan on quiet channels
 * @quiet_plcp_th: quiet PLCP threshold (channel is quiet if less than
 *	this number of packets were received (typically 1)
 * @passive2active: is auto switching from passive to active during scan allowed
 * @rxchain_sel_flags: RXON_RX_CHAIN_*
 * @max_out_time: in usecs, max out of serving channel time
 * @suspend_time: how long to pause scan when returning to service channel:
 *	bits 0-19: beacon interal in usecs (suspend before executing)
 *	bits 20-23: reserved
 *	bits 24-31: number of beacons (suspend between channels)
 * @rxon_flags: RXON_FLG_*
 * @filter_flags: RXON_FILTER_*
 * @tx_cmd: for active scans (zero for passive), w/o payload,
 *	no RS so specify TX rate
 * @direct_scan: direct scan SSIDs
 * @type: one of SCAN_TYPE_*
 * @repeats: how many time to repeat the scan
 */
struct iwl_scan_cmd {
	__le16 len;
	u8 scan_flags;
	u8 channel_count;
	__le16 quiet_time;
	__le16 quiet_plcp_th;
	__le16 passive2active;
	__le16 rxchain_sel_flags;
	__le32 max_out_time;
	__le32 suspend_time;
	/* RX_ON_FLAGS_API_S_VER_1 */
	__le32 rxon_flags;
	__le32 filter_flags;
	struct iwl_tx_cmd tx_cmd;
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];
	__le32 type;
	__le32 repeats;

	/*
	 * Probe request frame, followed by channel list.
	 *
	 * Size of probe request frame is specified by byte count in tx_cmd.
	 * Channel list follows immediately after probe request frame.
	 * Number of channels in list is specified by channel_count.
	 * Each channel in list is of type:
	 *
	 * struct iwl_scan_channel channels[0];
	 *
	 * NOTE:  Only one band of channels can be scanned per pass.  You
	 * must not mix 2.4GHz channels and 5.2GHz channels, and you must wait
	 * for one scan to complete (i.e. receive SCAN_COMPLETE_NOTIFICATION)
	 * before requesting another scan.
	 */
	u8 data[0];
} __packed; /* SCAN_REQUEST_FIXED_PART_API_S_VER_5 */

/* Response to scan request contains only status with one of these values */
#define SCAN_RESPONSE_OK	0x1
#define SCAN_RESPONSE_ERROR	0x2

/*
 * SCAN_ABORT_CMD = 0x81
 * When scan abort is requested, the command has no fields except the common
 * header. The response contains only a status with one of these values.
 */
#define SCAN_ABORT_POSSIBLE	0x1
#define SCAN_ABORT_IGNORED	0x2 /* no pending scans */

/* TODO: complete documentation */
#define  SCAN_OWNER_STATUS 0x1
#define  MEASURE_OWNER_STATUS 0x2

/**
 * struct iwl_scan_start_notif - notifies start of scan in the device
 * ( SCAN_START_NOTIFICATION = 0x82 )
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @beacon_timer: structured as follows:
 *	bits 0:19 - beacon interval in usecs
 *	bits 20:23 - reserved (0)
 *	bits 24:31 - number of beacons
 * @channel: which channel is scanned
 * @band: 0 for 5.2 GHz, 1 for 2.4 GHz
 * @status: one of *_OWNER_STATUS
 */
struct iwl_scan_start_notif {
	__le32 tsf_low;
	__le32 tsf_high;
	__le32 beacon_timer;
	u8 channel;
	u8 band;
	u8 reserved[2];
	__le32 status;
} __packed; /* SCAN_START_NTF_API_S_VER_1 */

/* scan results probe_status first bit indicates success */
#define SCAN_PROBE_STATUS_OK		0
#define SCAN_PROBE_STATUS_TX_FAILED	BIT(0)
/* error statuses combined with TX_FAILED */
#define SCAN_PROBE_STATUS_FAIL_TTL	BIT(1)
#define SCAN_PROBE_STATUS_FAIL_BT	BIT(2)

/* How many statistics are gathered for each channel */
#define SCAN_RESULTS_STATISTICS 1

/**
 * enum iwl_scan_complete_status - status codes for scan complete notifications
 * @SCAN_COMP_STATUS_OK:  scan completed successfully
 * @SCAN_COMP_STATUS_ABORT: scan was aborted by user
 * @SCAN_COMP_STATUS_ERR_SLEEP: sending null sleep packet failed
 * @SCAN_COMP_STATUS_ERR_CHAN_TIMEOUT: timeout before channel is ready
 * @SCAN_COMP_STATUS_ERR_PROBE: sending probe request failed
 * @SCAN_COMP_STATUS_ERR_WAKEUP: sending null wakeup packet failed
 * @SCAN_COMP_STATUS_ERR_ANTENNAS: invalid antennas chosen at scan command
 * @SCAN_COMP_STATUS_ERR_INTERNAL: internal error caused scan abort
 * @SCAN_COMP_STATUS_ERR_COEX: medium was lost ot WiMax
 * @SCAN_COMP_STATUS_P2P_ACTION_OK: P2P public action frame TX was successful
 *	(not an error!)
 * @SCAN_COMP_STATUS_ITERATION_END: indicates end of one repeatition the driver
 *	asked for
 * @SCAN_COMP_STATUS_ERR_ALLOC_TE: scan could not allocate time events
*/
enum iwl_scan_complete_status {
	SCAN_COMP_STATUS_OK = 0x1,
	SCAN_COMP_STATUS_ABORT = 0x2,
	SCAN_COMP_STATUS_ERR_SLEEP = 0x3,
	SCAN_COMP_STATUS_ERR_CHAN_TIMEOUT = 0x4,
	SCAN_COMP_STATUS_ERR_PROBE = 0x5,
	SCAN_COMP_STATUS_ERR_WAKEUP = 0x6,
	SCAN_COMP_STATUS_ERR_ANTENNAS = 0x7,
	SCAN_COMP_STATUS_ERR_INTERNAL = 0x8,
	SCAN_COMP_STATUS_ERR_COEX = 0x9,
	SCAN_COMP_STATUS_P2P_ACTION_OK = 0xA,
	SCAN_COMP_STATUS_ITERATION_END = 0x0B,
	SCAN_COMP_STATUS_ERR_ALLOC_TE = 0x0C,
};

/**
 * struct iwl_scan_results_notif - scan results for one channel
 * ( SCAN_RESULTS_NOTIFICATION = 0x83 )
 * @channel: which channel the results are from
 * @band: 0 for 5.2 GHz, 1 for 2.4 GHz
 * @probe_status: SCAN_PROBE_STATUS_*, indicates success of probe request
 * @num_probe_not_sent: # of request that weren't sent due to not enough time
 * @duration: duration spent in channel, in usecs
 * @statistics: statistics gathered for this channel
 */
struct iwl_scan_results_notif {
	u8 channel;
	u8 band;
	u8 probe_status;
	u8 num_probe_not_sent;
	__le32 duration;
	__le32 statistics[SCAN_RESULTS_STATISTICS];
} __packed; /* SCAN_RESULT_NTF_API_S_VER_2 */

/**
 * struct iwl_scan_complete_notif - notifies end of scanning (all channels)
 * ( SCAN_COMPLETE_NOTIFICATION = 0x84 )
 * @scanned_channels: number of channels scanned (and number of valid results)
 * @status: one of SCAN_COMP_STATUS_*
 * @bt_status: BT on/off status
 * @last_channel: last channel that was scanned
 * @tsf_low: TSF timer (lower half) in usecs
 * @tsf_high: TSF timer (higher half) in usecs
 * @results: all scan results, only "scanned_channels" of them are valid
 */
struct iwl_scan_complete_notif {
	u8 scanned_channels;
	u8 status;
	u8 bt_status;
	u8 last_channel;
	__le32 tsf_low;
	__le32 tsf_high;
	struct iwl_scan_results_notif results[MAX_NUM_SCAN_CHANNELS];
} __packed; /* SCAN_COMPLETE_NTF_API_S_VER_2 */

/* scan offload */
#define IWL_MAX_SCAN_CHANNELS		40
#define IWL_SCAN_MAX_BLACKLIST_LEN	64
#define IWL_SCAN_MAX_PROFILES		11
#define SCAN_OFFLOAD_PROBE_REQ_SIZE	512

/* Default watchdog (in MS) for scheduled scan iteration */
#define IWL_SCHED_SCAN_WATCHDOG cpu_to_le16(15000)

#define IWL_GOOD_CRC_TH_DEFAULT cpu_to_le16(1)
#define CAN_ABORT_STATUS 1

#define IWL_FULL_SCAN_MULTIPLIER 5
#define IWL_FAST_SCHED_SCAN_ITERATIONS 3

/**
 * struct iwl_scan_offload_cmd - SCAN_REQUEST_FIXED_PART_API_S_VER_6
 * @scan_flags:		see enum iwl_scan_flags
 * @channel_count:	channels in channel list
 * @quiet_time:		dwell time, in milisiconds, on quiet channel
 * @quiet_plcp_th:	quiet channel num of packets threshold
 * @good_CRC_th:	passive to active promotion threshold
 * @rx_chain:		RXON rx chain.
 * @max_out_time:	max uSec to be out of assoceated channel
 * @suspend_time:	pause scan this long when returning to service channel
 * @flags:		RXON flags
 * @filter_flags:	RXONfilter
 * @tx_cmd:		tx command for active scan; for 2GHz and for 5GHz.
 * @direct_scan:	list of SSIDs for directed active scan
 * @scan_type:		see enum iwl_scan_type.
 * @rep_count:		repetition count for each scheduled scan iteration.
 */
struct iwl_scan_offload_cmd {
	__le16 len;
	u8 scan_flags;
	u8 channel_count;
	__le16 quiet_time;
	__le16 quiet_plcp_th;
	__le16 good_CRC_th;
	__le16 rx_chain;
	__le32 max_out_time;
	__le32 suspend_time;
	/* RX_ON_FLAGS_API_S_VER_1 */
	__le32 flags;
	__le32 filter_flags;
	struct iwl_tx_cmd tx_cmd[2];
	/* SCAN_DIRECT_SSID_IE_API_S_VER_1 */
	struct iwl_ssid_ie direct_scan[PROBE_OPTION_MAX];
	__le32 scan_type;
	__le32 rep_count;
} __packed;

enum iwl_scan_offload_channel_flags {
	IWL_SCAN_OFFLOAD_CHANNEL_ACTIVE		= BIT(0),
	IWL_SCAN_OFFLOAD_CHANNEL_NARROW		= BIT(22),
	IWL_SCAN_OFFLOAD_CHANNEL_FULL		= BIT(24),
	IWL_SCAN_OFFLOAD_CHANNEL_PARTIAL	= BIT(25),
};

/**
 * iwl_scan_channel_cfg - SCAN_CHANNEL_CFG_S
 * @type:		bitmap - see enum iwl_scan_offload_channel_flags.
 *			0:	passive (0) or active (1) scan.
 *			1-20:	directed scan to i'th ssid.
 *			22:	channel width configuation - 1 for narrow.
 *			24:	full scan.
 *			25:	partial scan.
 * @channel_number:	channel number 1-13 etc.
 * @iter_count:		repetition count for the channel.
 * @iter_interval:	interval between two innteration on one channel.
 * @dwell_time:	entry 0 - active scan, entry 1 - passive scan.
 */
struct iwl_scan_channel_cfg {
	__le32 type[IWL_MAX_SCAN_CHANNELS];
	__le16 channel_number[IWL_MAX_SCAN_CHANNELS];
	__le16 iter_count[IWL_MAX_SCAN_CHANNELS];
	__le32 iter_interval[IWL_MAX_SCAN_CHANNELS];
	u8 dwell_time[IWL_MAX_SCAN_CHANNELS][2];
} __packed;

/**
 * iwl_scan_offload_cfg - SCAN_OFFLOAD_CONFIG_API_S
 * @scan_cmd:		scan command fixed part
 * @channel_cfg:	scan channel configuration
 * @data:		probe request frames (one per band)
 */
struct iwl_scan_offload_cfg {
	struct iwl_scan_offload_cmd scan_cmd;
	struct iwl_scan_channel_cfg channel_cfg;
	u8 data[0];
} __packed;

/**
 * iwl_scan_offload_blacklist - SCAN_OFFLOAD_BLACKLIST_S
 * @ssid:		MAC address to filter out
 * @reported_rssi:	AP rssi reported to the host
 */
struct iwl_scan_offload_blacklist {
	u8 ssid[ETH_ALEN];
	u8 reported_rssi;
	u8 reserved;
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
 * @unicast_cipher:	encryption olgorithm to match - bitmap
 * @aut_alg:		authentication olgorithm to match - bitmap
 * @network_type:	enum iwl_scan_offload_network_type
 * @band_selection:	enum iwl_scan_offload_band_selection
 */
struct iwl_scan_offload_profile {
	u8 ssid_index;
	u8 unicast_cipher;
	u8 auth_alg;
	u8 network_type;
	u8 band_selection;
	u8 reserved[3];
} __packed;

/**
 * iwl_scan_offload_profile_cfg - SCAN_OFFLOAD_PROFILES_CFG_API_S_VER_1
 * @blaclist:		AP list to filter off from scan results
 * @profiles:		profiles to search for match
 * @blacklist_len:	length of blacklist
 * @num_profiles:	num of profiles in the list
 */
struct iwl_scan_offload_profile_cfg {
	struct iwl_scan_offload_blacklist blacklist[IWL_SCAN_MAX_BLACKLIST_LEN];
	struct iwl_scan_offload_profile profiles[IWL_SCAN_MAX_PROFILES];
	u8 blacklist_len;
	u8 num_profiles;
	u8 reserved[2];
} __packed;

/**
 * iwl_scan_offload_schedule - schedule of scan offload
 * @delay:		delay between iterations, in seconds.
 * @iterations:		num of scan iterations
 * @full_scan_mul:	number of partial scans before each full scan
 */
struct iwl_scan_offload_schedule {
	u16 delay;
	u8 iterations;
	u8 full_scan_mul;
} __packed;

/*
 * iwl_scan_offload_flags
 *
 * IWL_SCAN_OFFLOAD_FLAG_FILTER_SSID: filter mode - upload every beacon or match
 *	ssid list.
 * IWL_SCAN_OFFLOAD_FLAG_CACHED_CHANNEL: add cached channels to partial scan.
 * IWL_SCAN_OFFLOAD_FLAG_ENERGY_SCAN: use energy based scan before partial scan
 *	on A band.
 */
enum iwl_scan_offload_flags {
	IWL_SCAN_OFFLOAD_FLAG_FILTER_SSID	= BIT(0),
	IWL_SCAN_OFFLOAD_FLAG_CACHED_CHANNEL	= BIT(2),
	IWL_SCAN_OFFLOAD_FLAG_ENERGY_SCAN	= BIT(3),
};

/**
 * iwl_scan_offload_req - scan offload request command
 * @flags:		bitmap - enum iwl_scan_offload_flags.
 * @watchdog:		maximum scan duration in TU.
 * @delay:		delay in seconds before first iteration.
 * @schedule_line:	scan offload schedule, for fast and regular scan.
 */
struct iwl_scan_offload_req {
	__le16 flags;
	__le16 watchdog;
	__le16 delay;
	__le16 reserved;
	struct iwl_scan_offload_schedule schedule_line[2];
} __packed;

enum iwl_scan_offload_compleate_status {
	IWL_SCAN_OFFLOAD_COMPLETED	= 1,
	IWL_SCAN_OFFLOAD_ABORTED	= 2,
};

/**
 * iwl_scan_offload_complete - SCAN_OFFLOAD_COMPLETE_NTF_API_S_VER_1
 * @last_schedule_line:		last schedule line executed (fast or regular)
 * @last_schedule_iteration:	last scan iteration executed before scan abort
 * @status:			enum iwl_scan_offload_compleate_status
 */
struct iwl_scan_offload_complete {
	u8 last_schedule_line;
	u8 last_schedule_iteration;
	u8 status;
	u8 reserved;
} __packed;

#endif
