/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2019 Intel Corporation
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
 * Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
 * Copyright (C) 2019 Intel Corporation
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
#ifndef __iwl_fw_api_location_h__
#define __iwl_fw_api_location_h__

/**
 * enum iwl_location_subcmd_ids - location group command IDs
 */
enum iwl_location_subcmd_ids {
	/**
	 * @TOF_RANGE_REQ_CMD: TOF ranging request,
	 *	uses &struct iwl_tof_range_req_cmd
	 */
	TOF_RANGE_REQ_CMD = 0x0,
	/**
	 * @TOF_CONFIG_CMD: TOF configuration, uses &struct iwl_tof_config_cmd
	 */
	TOF_CONFIG_CMD = 0x1,
	/**
	 * @TOF_RANGE_ABORT_CMD: abort ongoing ranging, uses
	 *	&struct iwl_tof_range_abort_cmd
	 */
	TOF_RANGE_ABORT_CMD = 0x2,
	/**
	 * @TOF_RANGE_REQ_EXT_CMD: TOF extended ranging config,
	 *	uses &struct iwl_tof_range_request_ext_cmd
	 */
	TOF_RANGE_REQ_EXT_CMD = 0x3,
	/**
	 * @TOF_RESPONDER_CONFIG_CMD: FTM responder configuration,
	 *	uses &struct iwl_tof_responder_config_cmd
	 */
	TOF_RESPONDER_CONFIG_CMD = 0x4,
	/**
	 * @TOF_RESPONDER_DYN_CONFIG_CMD: FTM dynamic configuration,
	 *	uses &struct iwl_tof_responder_dyn_config_cmd
	 */
	TOF_RESPONDER_DYN_CONFIG_CMD = 0x5,
	/**
	 * @CSI_HEADER_NOTIFICATION: CSI header
	 */
	CSI_HEADER_NOTIFICATION = 0xFA,
	/**
	 * @CSI_CHUNKS_NOTIFICATION: CSI chunk,
	 *	uses &struct iwl_csi_chunk_notification
	 */
	CSI_CHUNKS_NOTIFICATION = 0xFB,
	/**
	 * @TOF_LC_NOTIF: used for LCI/civic location, contains just
	 *	the action frame
	 */
	TOF_LC_NOTIF = 0xFC,
	/**
	 * @TOF_RESPONDER_STATS: FTM responder statistics notification,
	 *	uses &struct iwl_ftm_responder_stats
	 */
	TOF_RESPONDER_STATS = 0xFD,
	/**
	 * @TOF_MCSI_DEBUG_NOTIF: MCSI debug notification, uses
	 *	&struct iwl_tof_mcsi_notif
	 */
	TOF_MCSI_DEBUG_NOTIF = 0xFE,
	/**
	 * @TOF_RANGE_RESPONSE_NOTIF: ranging response, using
	 *	&struct iwl_tof_range_rsp_ntfy
	 */
	TOF_RANGE_RESPONSE_NOTIF = 0xFF,
};

/**
 * struct iwl_tof_config_cmd - ToF configuration
 * @tof_disabled: indicates if ToF is disabled (or not)
 * @one_sided_disabled: indicates if one-sided is disabled (or not)
 * @is_debug_mode: indiciates if debug mode is active
 * @is_buf_required: indicates if channel estimation buffer is required
 */
struct iwl_tof_config_cmd {
	u8 tof_disabled;
	u8 one_sided_disabled;
	u8 is_debug_mode;
	u8 is_buf_required;
} __packed;

/**
 * enum iwl_tof_bandwidth - values for iwl_tof_range_req_ap_entry.bandwidth
 * @IWL_TOF_BW_20_LEGACY: 20 MHz non-HT
 * @IWL_TOF_BW_20_HT: 20 MHz HT
 * @IWL_TOF_BW_40: 40 MHz
 * @IWL_TOF_BW_80: 80 MHz
 * @IWL_TOF_BW_160: 160 MHz
 */
enum iwl_tof_bandwidth {
	IWL_TOF_BW_20_LEGACY,
	IWL_TOF_BW_20_HT,
	IWL_TOF_BW_40,
	IWL_TOF_BW_80,
	IWL_TOF_BW_160,
}; /* LOCAT_BW_TYPE_E */

/*
 * enum iwl_tof_algo_type - Algorithym type for range measurement request
 */
enum iwl_tof_algo_type {
	IWL_TOF_ALGO_TYPE_MAX_LIKE	= 0,
	IWL_TOF_ALGO_TYPE_LINEAR_REG	= 1,
	IWL_TOF_ALGO_TYPE_FFT		= 2,

	/* Keep last */
	IWL_TOF_ALGO_TYPE_INVALID,
}; /* ALGO_TYPE_E */

/*
 * enum iwl_tof_mcsi_ntfy - Enable/Disable MCSI notifications
 */
enum iwl_tof_mcsi_enable {
	IWL_TOF_MCSI_DISABLED = 0,
	IWL_TOF_MCSI_ENABLED = 1,
}; /* MCSI_ENABLE_E */

/**
 * enum iwl_tof_responder_cmd_valid_field - valid fields in the responder cfg
 * @IWL_TOF_RESPONDER_CMD_VALID_CHAN_INFO: channel info is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_TOA_OFFSET: ToA offset is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_COMMON_CALIB: common calibration mode is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_SPECIFIC_CALIB: spefici calibration mode is
 *	valid
 * @IWL_TOF_RESPONDER_CMD_VALID_BSSID: BSSID is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_TX_ANT: TX antenna is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_ALGO_TYPE: algorithm type is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_NON_ASAP_SUPPORT: non-ASAP support is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_STATISTICS_REPORT_SUPPORT: statistics report
 *	support is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_MCSI_NOTIF_SUPPORT: MCSI notification support
 *	is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_FAST_ALGO_SUPPORT: fast algorithm support
 *	is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_RETRY_ON_ALGO_FAIL: retry on algorithm failure
 *	is valid
 * @IWL_TOF_RESPONDER_CMD_VALID_STA_ID: station ID is valid
 */
enum iwl_tof_responder_cmd_valid_field {
	IWL_TOF_RESPONDER_CMD_VALID_CHAN_INFO = BIT(0),
	IWL_TOF_RESPONDER_CMD_VALID_TOA_OFFSET = BIT(1),
	IWL_TOF_RESPONDER_CMD_VALID_COMMON_CALIB = BIT(2),
	IWL_TOF_RESPONDER_CMD_VALID_SPECIFIC_CALIB = BIT(3),
	IWL_TOF_RESPONDER_CMD_VALID_BSSID = BIT(4),
	IWL_TOF_RESPONDER_CMD_VALID_TX_ANT = BIT(5),
	IWL_TOF_RESPONDER_CMD_VALID_ALGO_TYPE = BIT(6),
	IWL_TOF_RESPONDER_CMD_VALID_NON_ASAP_SUPPORT = BIT(7),
	IWL_TOF_RESPONDER_CMD_VALID_STATISTICS_REPORT_SUPPORT = BIT(8),
	IWL_TOF_RESPONDER_CMD_VALID_MCSI_NOTIF_SUPPORT = BIT(9),
	IWL_TOF_RESPONDER_CMD_VALID_FAST_ALGO_SUPPORT = BIT(10),
	IWL_TOF_RESPONDER_CMD_VALID_RETRY_ON_ALGO_FAIL = BIT(11),
	IWL_TOF_RESPONDER_CMD_VALID_STA_ID = BIT(12),
};

/**
 * enum iwl_tof_responder_cfg_flags - responder configuration flags
 * @IWL_TOF_RESPONDER_FLAGS_NON_ASAP_SUPPORT: non-ASAP support
 * @IWL_TOF_RESPONDER_FLAGS_REPORT_STATISTICS: report statistics
 * @IWL_TOF_RESPONDER_FLAGS_REPORT_MCSI: report MCSI
 * @IWL_TOF_RESPONDER_FLAGS_ALGO_TYPE: algorithm type
 * @IWL_TOF_RESPONDER_FLAGS_TOA_OFFSET_MODE: ToA offset mode
 * @IWL_TOF_RESPONDER_FLAGS_COMMON_CALIB_MODE: common calibration mode
 * @IWL_TOF_RESPONDER_FLAGS_SPECIFIC_CALIB_MODE: specific calibration mode
 * @IWL_TOF_RESPONDER_FLAGS_FAST_ALGO_SUPPORT: fast algorithm support
 * @IWL_TOF_RESPONDER_FLAGS_RETRY_ON_ALGO_FAIL: retry on algorithm fail
 * @IWL_TOF_RESPONDER_FLAGS_FTM_TX_ANT: TX antenna mask
 */
enum iwl_tof_responder_cfg_flags {
	IWL_TOF_RESPONDER_FLAGS_NON_ASAP_SUPPORT = BIT(0),
	IWL_TOF_RESPONDER_FLAGS_REPORT_STATISTICS = BIT(1),
	IWL_TOF_RESPONDER_FLAGS_REPORT_MCSI = BIT(2),
	IWL_TOF_RESPONDER_FLAGS_ALGO_TYPE = BIT(3) | BIT(4) | BIT(5),
	IWL_TOF_RESPONDER_FLAGS_TOA_OFFSET_MODE = BIT(6),
	IWL_TOF_RESPONDER_FLAGS_COMMON_CALIB_MODE = BIT(7),
	IWL_TOF_RESPONDER_FLAGS_SPECIFIC_CALIB_MODE = BIT(8),
	IWL_TOF_RESPONDER_FLAGS_FAST_ALGO_SUPPORT = BIT(9),
	IWL_TOF_RESPONDER_FLAGS_RETRY_ON_ALGO_FAIL = BIT(10),
	IWL_TOF_RESPONDER_FLAGS_FTM_TX_ANT = RATE_MCS_ANT_ABC_MSK,
};

/**
 * struct iwl_tof_responder_config_cmd - ToF AP mode (for debug)
 * @cmd_valid_fields: &iwl_tof_responder_cmd_valid_field
 * @responder_cfg_flags: &iwl_tof_responder_cfg_flags
 * @bandwidth: current AP Bandwidth: &enum iwl_tof_bandwidth
 * @rate: current AP rate
 * @channel_num: current AP Channel
 * @ctrl_ch_position: coding of the control channel position relative to
 *	the center frequency, see iwl_mvm_get_ctrl_pos()
 * @sta_id: index of the AP STA when in AP mode
 * @reserved1: reserved
 * @toa_offset: Artificial addition [pSec] for the ToA - to be used for debug
 *	purposes, simulating station movement by adding various values
 *	to this field
 * @common_calib: XVT: common calibration value
 * @specific_calib: XVT: specific calibration value
 * @bssid: Current AP BSSID
 * @reserved2: reserved
 */
struct iwl_tof_responder_config_cmd {
	__le32 cmd_valid_fields;
	__le32 responder_cfg_flags;
	u8 bandwidth;
	u8 rate;
	u8 channel_num;
	u8 ctrl_ch_position;
	u8 sta_id;
	u8 reserved1;
	__le16 toa_offset;
	__le16 common_calib;
	__le16 specific_calib;
	u8 bssid[ETH_ALEN];
	__le16 reserved2;
} __packed; /* TOF_RESPONDER_CONFIG_CMD_API_S_VER_6 */

#define IWL_LCI_CIVIC_IE_MAX_SIZE	400

/**
 * struct iwl_tof_responder_dyn_config_cmd - Dynamic responder settings
 * @lci_len: The length of the 1st (LCI) part in the @lci_civic buffer
 * @civic_len: The length of the 2nd (CIVIC) part in the @lci_civic buffer
 * @lci_civic: The LCI/CIVIC buffer. LCI data (if exists) comes first, then, if
 *	needed, 0-padding such that the next part is dword-aligned, then CIVIC
 *	data (if exists) follows, and then 0-padding again to complete a
 *	4-multiple long buffer.
 */
struct iwl_tof_responder_dyn_config_cmd {
	__le32 lci_len;
	__le32 civic_len;
	u8 lci_civic[];
} __packed; /* TOF_RESPONDER_DYN_CONFIG_CMD_API_S_VER_2 */

/**
 * struct iwl_tof_range_request_ext_cmd - extended range req for WLS
 * @tsf_timer_offset_msec: the recommended time offset (mSec) from the AP's TSF
 * @reserved: reserved
 * @min_delta_ftm: Minimal time between two consecutive measurements,
 *		   in units of 100us. 0 means no preference by station
 * @ftm_format_and_bw20M: FTM Channel Spacing/Format for 20MHz: recommended
 *			value be sent to the AP
 * @ftm_format_and_bw40M: FTM Channel Spacing/Format for 40MHz: recommended
 *			value to be sent to the AP
 * @ftm_format_and_bw80M: FTM Channel Spacing/Format for 80MHz: recommended
 *			value to be sent to the AP
 */
struct iwl_tof_range_req_ext_cmd {
	__le16 tsf_timer_offset_msec;
	__le16 reserved;
	u8 min_delta_ftm;
	u8 ftm_format_and_bw20M;
	u8 ftm_format_and_bw40M;
	u8 ftm_format_and_bw80M;
} __packed;

/**
 * enum iwl_tof_location_query - values for query bitmap
 * @IWL_TOF_LOC_LCI: query LCI
 * @IWL_TOF_LOC_CIVIC: query civic
 */
enum iwl_tof_location_query {
	IWL_TOF_LOC_LCI = 0x01,
	IWL_TOF_LOC_CIVIC = 0x02,
};

 /**
 * struct iwl_tof_range_req_ap_entry_v2 - AP configuration parameters
 * @channel_num: Current AP Channel
 * @bandwidth: Current AP Bandwidth. One of iwl_tof_bandwidth.
 * @tsf_delta_direction: TSF relatively to the subject AP
 * @ctrl_ch_position: Coding of the control channel position relative to the
 *	center frequency, see iwl_mvm_get_ctrl_pos().
 * @bssid: AP's BSSID
 * @measure_type: Measurement type: 0 - two sided, 1 - One sided
 * @num_of_bursts: Recommended value to be sent to the AP.  2s Exponent of the
 *	number of measurement iterations (min 2^0 = 1, max 2^14)
 * @burst_period: Recommended value to be sent to the AP. Measurement
 *	periodicity In units of 100ms. ignored if num_of_bursts = 0
 * @samples_per_burst: 2-sided: the number of FTMs pairs in single Burst (1-31);
 *	1-sided: how many rts/cts pairs should be used per burst.
 * @retries_per_sample: Max number of retries that the LMAC should send
 *	in case of no replies by the AP.
 * @tsf_delta: TSF Delta in units of microseconds.
 *	The difference between the AP TSF and the device local clock.
 * @location_req: Location Request Bit[0] LCI should be sent in the FTMR;
 *	Bit[1] Civic should be sent in the FTMR
 * @asap_mode: 0 - non asap mode, 1 - asap mode (not relevant for one sided)
 * @enable_dyn_ack: Enable Dynamic ACK BW.
 *	0: Initiator interact with regular AP;
 *	1: Initiator interact with Responder machine: need to send the
 *	Initiator Acks with HT 40MHz / 80MHz, since the Responder should
 *	use it for its ch est measurement (this flag will be set when we
 *	configure the opposite machine to be Responder).
 * @rssi: Last received value
 *	legal values: -128-0 (0x7f). above 0x0 indicating an invalid value.
 * @algo_type: &enum iwl_tof_algo_type
 * @notify_mcsi: &enum iwl_tof_mcsi_ntfy.
 * @reserved: For alignment and future use
 */
struct iwl_tof_range_req_ap_entry_v2 {
	u8 channel_num;
	u8 bandwidth;
	u8 tsf_delta_direction;
	u8 ctrl_ch_position;
	u8 bssid[ETH_ALEN];
	u8 measure_type;
	u8 num_of_bursts;
	__le16 burst_period;
	u8 samples_per_burst;
	u8 retries_per_sample;
	__le32 tsf_delta;
	u8 location_req;
	u8 asap_mode;
	u8 enable_dyn_ack;
	s8 rssi;
	u8 algo_type;
	u8 notify_mcsi;
	__le16 reserved;
} __packed; /* LOCATION_RANGE_REQ_AP_ENTRY_CMD_API_S_VER_2 */

/**
 * enum iwl_initiator_ap_flags - per responder FTM configuration flags
 * @IWL_INITIATOR_AP_FLAGS_ASAP: Request for ASAP measurement.
 * @IWL_INITIATOR_AP_FLAGS_LCI_REQUEST: Request for LCI information
 * @IWL_INITIATOR_AP_FLAGS_CIVIC_REQUEST: Request for CIVIC information
 * @IWL_INITIATOR_AP_FLAGS_DYN_ACK: Send HT/VHT ack for FTM frames. If not set,
 *	20Mhz dup acks will be sent.
 * @IWL_INITIATOR_AP_FLAGS_ALGO_LR: Use LR algo type for rtt calculation.
 *	Default algo type is ML.
 * @IWL_INITIATOR_AP_FLAGS_ALGO_FFT: Use FFT algo type for rtt calculation.
 *	Default algo type is ML.
 * @IWL_INITIATOR_AP_FLAGS_MCSI_REPORT: Send the MCSI for each FTM frame to the
 *	driver.
 */
enum iwl_initiator_ap_flags {
	IWL_INITIATOR_AP_FLAGS_ASAP = BIT(1),
	IWL_INITIATOR_AP_FLAGS_LCI_REQUEST = BIT(2),
	IWL_INITIATOR_AP_FLAGS_CIVIC_REQUEST = BIT(3),
	IWL_INITIATOR_AP_FLAGS_DYN_ACK = BIT(4),
	IWL_INITIATOR_AP_FLAGS_ALGO_LR = BIT(5),
	IWL_INITIATOR_AP_FLAGS_ALGO_FFT = BIT(6),
	IWL_INITIATOR_AP_FLAGS_MCSI_REPORT = BIT(8),
};

/**
 * struct iwl_tof_range_req_ap_entry - AP configuration parameters
 * @initiator_ap_flags: see &enum iwl_initiator_ap_flags.
 * @channel_num: AP Channel number
 * @bandwidth: AP bandwidth. One of iwl_tof_bandwidth.
 * @ctrl_ch_position: Coding of the control channel position relative to the
 *	center frequency, see iwl_mvm_get_ctrl_pos().
 * @ftmr_max_retries: Max number of retries to send the FTMR in case of no
 *	reply from the AP.
 * @bssid: AP's BSSID
 * @burst_period: Recommended value to be sent to the AP. Measurement
 *	periodicity In units of 100ms. ignored if num_of_bursts_exp = 0
 * @samples_per_burst: the number of FTMs pairs in single Burst (1-31);
 * @num_of_bursts: Recommended value to be sent to the AP. 2s Exponent of
 *	the number of measurement iterations (min 2^0 = 1, max 2^14)
 * @reserved: For alignment and future use
 * @tsf_delta: not in use
 */
struct iwl_tof_range_req_ap_entry {
	__le32 initiator_ap_flags;
	u8 channel_num;
	u8 bandwidth;
	u8 ctrl_ch_position;
	u8 ftmr_max_retries;
	u8 bssid[ETH_ALEN];
	__le16 burst_period;
	u8 samples_per_burst;
	u8 num_of_bursts;
	__le16 reserved;
	__le32 tsf_delta;
} __packed; /* LOCATION_RANGE_REQ_AP_ENTRY_CMD_API_S_VER_3 */

/**
 * enum iwl_tof_response_mode
 * @IWL_MVM_TOF_RESPONSE_ASAP: report each AP measurement separately as soon as
 *			       possible (not supported for this release)
 * @IWL_MVM_TOF_RESPONSE_TIMEOUT: report all AP measurements as a batch upon
 *				  timeout expiration
 * @IWL_MVM_TOF_RESPONSE_COMPLETE: report all AP measurements as a batch at the
 *				   earlier of: measurements completion / timeout
 *				   expiration.
 */
enum iwl_tof_response_mode {
	IWL_MVM_TOF_RESPONSE_ASAP,
	IWL_MVM_TOF_RESPONSE_TIMEOUT,
	IWL_MVM_TOF_RESPONSE_COMPLETE,
};

/**
 * enum iwl_tof_initiator_flags
 *
 * @IWL_TOF_INITIATOR_FLAGS_FAST_ALGO_DISABLED: disable fast algo, meaning run
 *	the algo on ant A+B, instead of only one of them.
 * @IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_A: open RX antenna A for FTMs RX
 * @IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_B: open RX antenna B for FTMs RX
 * @IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_C: open RX antenna C for FTMs RX
 * @IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_A: use antenna A fo TX ACKs during FTM
 * @IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_B: use antenna B fo TX ACKs during FTM
 * @IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_C: use antenna C fo TX ACKs during FTM
 * @IWL_TOF_INITIATOR_FLAGS_MACADDR_RANDOM: use random mac address for FTM
 * @IWL_TOF_INITIATOR_FLAGS_SPECIFIC_CALIB: use the specific calib value from
 *	the range request command
 * @IWL_TOF_INITIATOR_FLAGS_COMMON_CALIB: use the common calib value from the
 *	ragne request command
 * @IWL_TOF_INITIATOR_FLAGS_NON_ASAP_SUPPORT: support non-asap measurements
 */
enum iwl_tof_initiator_flags {
	IWL_TOF_INITIATOR_FLAGS_FAST_ALGO_DISABLED = BIT(0),
	IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_A = BIT(1),
	IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_B = BIT(2),
	IWL_TOF_INITIATOR_FLAGS_RX_CHAIN_SEL_C = BIT(3),
	IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_A = BIT(4),
	IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_B = BIT(5),
	IWL_TOF_INITIATOR_FLAGS_TX_CHAIN_SEL_C = BIT(6),
	IWL_TOF_INITIATOR_FLAGS_MACADDR_RANDOM = BIT(7),
	IWL_TOF_INITIATOR_FLAGS_SPECIFIC_CALIB = BIT(15),
	IWL_TOF_INITIATOR_FLAGS_COMMON_CALIB   = BIT(16),
	IWL_TOF_INITIATOR_FLAGS_NON_ASAP_SUPPORT = BIT(20),
}; /* LOCATION_RANGE_REQ_CMD_API_S_VER_5 */

#define IWL_MVM_TOF_MAX_APS 5
#define IWL_MVM_TOF_MAX_TWO_SIDED_APS 5

/**
 * struct iwl_tof_range_req_cmd_v5 - start measurement cmd
 * @initiator_flags: see flags @ iwl_tof_initiator_flags
 * @request_id: A Token incremented per request. The same Token will be
 *		sent back in the range response
 * @initiator: 0- NW initiated,  1 - Client Initiated
 * @one_sided_los_disable: '0'- run ML-Algo for both ToF/OneSided,
 *			   '1' - run ML-Algo for ToF only
 * @req_timeout: Requested timeout of the response in units of 100ms.
 *	     This is equivalent to the session time configured to the
 *	     LMAC in Initiator Request
 * @report_policy: Supported partially for this release: For current release -
 *		   the range report will be uploaded as a batch when ready or
 *		   when the session is done (successfully / partially).
 *		   one of iwl_tof_response_mode.
 * @reserved0: reserved
 * @num_of_ap: Number of APs to measure (error if > IWL_MVM_TOF_MAX_APS)
 * @macaddr_random: '0' Use default source MAC address (i.e. p2_p),
 *	            '1' Use MAC Address randomization according to the below
 * @range_req_bssid: ranging request BSSID
 * @macaddr_template: MAC address template to use for non-randomized bits
 * @macaddr_mask: Bits set to 0 shall be copied from the MAC address template.
 *		  Bits set to 1 shall be randomized by the UMAC
 * @ftm_rx_chains: Rx chain to open to receive Responder's FTMs (XVT)
 * @ftm_tx_chains: Tx chain to send the ack to the Responder FTM (XVT)
 * @common_calib: The common calib value to inject to this measurement calc
 * @specific_calib: The specific calib value to inject to this measurement calc
 * @ap: per-AP request data
 */
struct iwl_tof_range_req_cmd_v5 {
	__le32 initiator_flags;
	u8 request_id;
	u8 initiator;
	u8 one_sided_los_disable;
	u8 req_timeout;
	u8 report_policy;
	u8 reserved0;
	u8 num_of_ap;
	u8 macaddr_random;
	u8 range_req_bssid[ETH_ALEN];
	u8 macaddr_template[ETH_ALEN];
	u8 macaddr_mask[ETH_ALEN];
	u8 ftm_rx_chains;
	u8 ftm_tx_chains;
	__le16 common_calib;
	__le16 specific_calib;
	struct iwl_tof_range_req_ap_entry_v2 ap[IWL_MVM_TOF_MAX_APS];
} __packed;
/* LOCATION_RANGE_REQ_CMD_API_S_VER_5 */

/**
 * struct iwl_tof_range_req_cmd - start measurement cmd
 * @initiator_flags: see flags @ iwl_tof_initiator_flags
 * @request_id: A Token incremented per request. The same Token will be
 *		sent back in the range response
 * @num_of_ap: Number of APs to measure (error if > IWL_MVM_TOF_MAX_APS)
 * @range_req_bssid: ranging request BSSID
 * @macaddr_mask: Bits set to 0 shall be copied from the MAC address template.
 *		  Bits set to 1 shall be randomized by the UMAC
 * @macaddr_template: MAC address template to use for non-randomized bits
 * @req_timeout_ms: Requested timeout of the response in units of milliseconds.
 *	This is the session time for completing the measurement.
 * @tsf_mac_id: report the measurement start time for each ap in terms of the
 *	TSF of this mac id. 0xff to disable TSF reporting.
 * @common_calib: The common calib value to inject to this measurement calc
 * @specific_calib: The specific calib value to inject to this measurement calc
 * @ap: per-AP request data, see &struct iwl_tof_range_req_ap_entry_v2.
 */
struct iwl_tof_range_req_cmd {
	__le32 initiator_flags;
	u8 request_id;
	u8 num_of_ap;
	u8 range_req_bssid[ETH_ALEN];
	u8 macaddr_mask[ETH_ALEN];
	u8 macaddr_template[ETH_ALEN];
	__le32 req_timeout_ms;
	__le32 tsf_mac_id;
	__le16 common_calib;
	__le16 specific_calib;
	struct iwl_tof_range_req_ap_entry ap[IWL_MVM_TOF_MAX_APS];
} __packed; /* LOCATION_RANGE_REQ_CMD_API_S_VER_7 */

/*
 * enum iwl_tof_range_request_status - status of the sent request
 * @IWL_TOF_RANGE_REQUEST_STATUS_SUCCESSFUL - FW successfully received the
 *	request
 * @IWL_TOF_RANGE_REQUEST_STATUS_BUSY - FW is busy with a previous request, the
 *	sent request will not be handled
 */
enum iwl_tof_range_request_status {
	IWL_TOF_RANGE_REQUEST_STATUS_SUCCESS,
	IWL_TOF_RANGE_REQUEST_STATUS_BUSY,
};

/**
 * enum iwl_tof_entry_status
 *
 * @IWL_TOF_ENTRY_SUCCESS: successful measurement.
 * @IWL_TOF_ENTRY_GENERAL_FAILURE: General failure.
 * @IWL_TOF_ENTRY_NO_RESPONSE: Responder didn't reply to the request.
 * @IWL_TOF_ENTRY_REQUEST_REJECTED: Responder rejected the request.
 * @IWL_TOF_ENTRY_NOT_SCHEDULED: Time event was scheduled but not called yet.
 * @IWL_TOF_ENTRY_TIMING_MEASURE_TIMEOUT: Time event triggered but no
 *	measurement was completed.
 * @IWL_TOF_ENTRY_TARGET_DIFF_CH_CANNOT_CHANGE: No range due inability to switch
 *	from the primary channel.
 * @IWL_TOF_ENTRY_RANGE_NOT_SUPPORTED: Device doesn't support FTM.
 * @IWL_TOF_ENTRY_REQUEST_ABORT_UNKNOWN_REASON: Request aborted due to unknown
 *	reason.
 * @IWL_TOF_ENTRY_LOCATION_INVALID_T1_T4_TIME_STAMP: Failure due to invalid
 *	T1/T4.
 * @IWL_TOF_ENTRY_11MC_PROTOCOL_FAILURE: Failure due to invalid FTM frame
 *	structure.
 * @IWL_TOF_ENTRY_REQUEST_CANNOT_SCHED: Request cannot be scheduled.
 * @IWL_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE: Responder cannot serve the
 *	initiator for some period, period supplied in @refusal_period.
 * @IWL_TOF_ENTRY_BAD_REQUEST_ARGS: Bad request arguments.
 * @IWL_TOF_ENTRY_WIFI_NOT_ENABLED: Wifi not enabled.
 * @IWL_TOF_ENTRY_RESPONDER_OVERRIDE_PARAMS: Responder override the original
 *	parameters within the current session.
 */
enum iwl_tof_entry_status {
	IWL_TOF_ENTRY_SUCCESS = 0,
	IWL_TOF_ENTRY_GENERAL_FAILURE = 1,
	IWL_TOF_ENTRY_NO_RESPONSE = 2,
	IWL_TOF_ENTRY_REQUEST_REJECTED = 3,
	IWL_TOF_ENTRY_NOT_SCHEDULED = 4,
	IWL_TOF_ENTRY_TIMING_MEASURE_TIMEOUT = 5,
	IWL_TOF_ENTRY_TARGET_DIFF_CH_CANNOT_CHANGE = 6,
	IWL_TOF_ENTRY_RANGE_NOT_SUPPORTED = 7,
	IWL_TOF_ENTRY_REQUEST_ABORT_UNKNOWN_REASON = 8,
	IWL_TOF_ENTRY_LOCATION_INVALID_T1_T4_TIME_STAMP = 9,
	IWL_TOF_ENTRY_11MC_PROTOCOL_FAILURE = 10,
	IWL_TOF_ENTRY_REQUEST_CANNOT_SCHED = 11,
	IWL_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE = 12,
	IWL_TOF_ENTRY_BAD_REQUEST_ARGS = 13,
	IWL_TOF_ENTRY_WIFI_NOT_ENABLED = 14,
	IWL_TOF_ENTRY_RESPONDER_OVERRIDE_PARAMS = 15,
}; /* LOCATION_RANGE_RSP_AP_ENTRY_NTFY_API_S_VER_2 */

/**
 * struct iwl_tof_range_rsp_ap_entry_ntfy_v3 - AP parameters (response)
 * @bssid: BSSID of the AP
 * @measure_status: current APs measurement status, one of
 *	&enum iwl_tof_entry_status.
 * @measure_bw: Current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @rtt: The Round Trip Time that took for the last measurement for
 *	current AP [pSec]
 * @rtt_variance: The Variance of the RTT values measured for current AP
 * @rtt_spread: The Difference between the maximum and the minimum RTT
 *	values measured for current AP in the current session [pSec]
 * @rssi: RSSI as uploaded in the Channel Estimation notification
 * @rssi_spread: The Difference between the maximum and the minimum RSSI values
 *	measured for current AP in the current session
 * @reserved: reserved
 * @refusal_period: refusal period in case of
 *	@IWL_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE [sec]
 * @range: Measured range [cm]
 * @range_variance: Measured range variance [cm]
 * @timestamp: The GP2 Clock [usec] where Channel Estimation notification was
 *	uploaded by the LMAC
 * @t2t3_initiator: as calculated from the algo in the initiator
 * @t1t4_responder: as calculated from the algo in the responder
 * @common_calib: Calib val that was used in for this AP measurement
 * @specific_calib: val that was used in for this AP measurement
 * @papd_calib_output: The result of the tof papd calibration that was injected
 *	into the algorithm.
 */
struct iwl_tof_range_rsp_ap_entry_ntfy_v3 {
	u8 bssid[ETH_ALEN];
	u8 measure_status;
	u8 measure_bw;
	__le32 rtt;
	__le32 rtt_variance;
	__le32 rtt_spread;
	s8 rssi;
	u8 rssi_spread;
	u8 reserved;
	u8 refusal_period;
	__le32 range;
	__le32 range_variance;
	__le32 timestamp;
	__le32 t2t3_initiator;
	__le32 t1t4_responder;
	__le16 common_calib;
	__le16 specific_calib;
	__le32 papd_calib_output;
} __packed; /* LOCATION_RANGE_RSP_AP_ETRY_NTFY_API_S_VER_3 */

/**
 * struct iwl_tof_range_rsp_ap_entry_ntfy_v4 - AP parameters (response)
 * @bssid: BSSID of the AP
 * @measure_status: current APs measurement status, one of
 *	&enum iwl_tof_entry_status.
 * @measure_bw: Current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @rtt: The Round Trip Time that took for the last measurement for
 *	current AP [pSec]
 * @rtt_variance: The Variance of the RTT values measured for current AP
 * @rtt_spread: The Difference between the maximum and the minimum RTT
 *	values measured for current AP in the current session [pSec]
 * @rssi: RSSI as uploaded in the Channel Estimation notification
 * @rssi_spread: The Difference between the maximum and the minimum RSSI values
 *	measured for current AP in the current session
 * @last_burst: 1 if no more FTM sessions are scheduled for this responder
 * @refusal_period: refusal period in case of
 *	@IWL_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE [sec]
 * @timestamp: The GP2 Clock [usec] where Channel Estimation notification was
 *	uploaded by the LMAC
 * @start_tsf: measurement start time in TSF of the mac specified in the range
 *	request
 * @rx_rate_n_flags: rate and flags of the last FTM frame received from this
 *	responder
 * @tx_rate_n_flags: rate and flags of the last ack sent to this responder
 * @t2t3_initiator: as calculated from the algo in the initiator
 * @t1t4_responder: as calculated from the algo in the responder
 * @common_calib: Calib val that was used in for this AP measurement
 * @specific_calib: val that was used in for this AP measurement
 * @papd_calib_output: The result of the tof papd calibration that was injected
 *	into the algorithm.
 */
struct iwl_tof_range_rsp_ap_entry_ntfy_v4 {
	u8 bssid[ETH_ALEN];
	u8 measure_status;
	u8 measure_bw;
	__le32 rtt;
	__le32 rtt_variance;
	__le32 rtt_spread;
	s8 rssi;
	u8 rssi_spread;
	u8 last_burst;
	u8 refusal_period;
	__le32 timestamp;
	__le32 start_tsf;
	__le32 rx_rate_n_flags;
	__le32 tx_rate_n_flags;
	__le32 t2t3_initiator;
	__le32 t1t4_responder;
	__le16 common_calib;
	__le16 specific_calib;
	__le32 papd_calib_output;
} __packed; /* LOCATION_RANGE_RSP_AP_ETRY_NTFY_API_S_VER_4 */

/**
 * struct iwl_tof_range_rsp_ap_entry_ntfy - AP parameters (response)
 * @bssid: BSSID of the AP
 * @measure_status: current APs measurement status, one of
 *	&enum iwl_tof_entry_status.
 * @measure_bw: Current AP Bandwidth: 0  20MHz, 1  40MHz, 2  80MHz
 * @rtt: The Round Trip Time that took for the last measurement for
 *	current AP [pSec]
 * @rtt_variance: The Variance of the RTT values measured for current AP
 * @rtt_spread: The Difference between the maximum and the minimum RTT
 *	values measured for current AP in the current session [pSec]
 * @rssi: RSSI as uploaded in the Channel Estimation notification
 * @rssi_spread: The Difference between the maximum and the minimum RSSI values
 *	measured for current AP in the current session
 * @last_burst: 1 if no more FTM sessions are scheduled for this responder
 * @refusal_period: refusal period in case of
 *	@IWL_TOF_ENTRY_RESPONDER_CANNOT_COLABORATE [sec]
 * @timestamp: The GP2 Clock [usec] where Channel Estimation notification was
 *	uploaded by the LMAC
 * @start_tsf: measurement start time in TSF of the mac specified in the range
 *	request
 * @rx_rate_n_flags: rate and flags of the last FTM frame received from this
 *	responder
 * @tx_rate_n_flags: rate and flags of the last ack sent to this responder
 * @t2t3_initiator: as calculated from the algo in the initiator
 * @t1t4_responder: as calculated from the algo in the responder
 * @common_calib: Calib val that was used in for this AP measurement
 * @specific_calib: val that was used in for this AP measurement
 * @papd_calib_output: The result of the tof papd calibration that was injected
 *	into the algorithm.
 * @rttConfidence: a value between 0 - 31 that represents the rtt accuracy.
 * @reserved: for alignment
 */
struct iwl_tof_range_rsp_ap_entry_ntfy {
	u8 bssid[ETH_ALEN];
	u8 measure_status;
	u8 measure_bw;
	__le32 rtt;
	__le32 rtt_variance;
	__le32 rtt_spread;
	s8 rssi;
	u8 rssi_spread;
	u8 last_burst;
	u8 refusal_period;
	__le32 timestamp;
	__le32 start_tsf;
	__le32 rx_rate_n_flags;
	__le32 tx_rate_n_flags;
	__le32 t2t3_initiator;
	__le32 t1t4_responder;
	__le16 common_calib;
	__le16 specific_calib;
	__le32 papd_calib_output;
	u8 rttConfidence;
	u8 reserved[3];
} __packed; /* LOCATION_RANGE_RSP_AP_ETRY_NTFY_API_S_VER_5 */

/**
 * enum iwl_tof_response_status - tof response status
 *
 * @IWL_TOF_RESPONSE_SUCCESS: successful range.
 * @IWL_TOF_RESPONSE_TIMEOUT: request aborted due to timeout expiration.
 *	partial result of ranges done so far is included in the response.
 * @IWL_TOF_RESPONSE_ABORTED: Measurement aborted by command.
 * @IWL_TOF_RESPONSE_FAILED: Measurement request command failed.
 */
enum iwl_tof_response_status {
	IWL_TOF_RESPONSE_SUCCESS = 0,
	IWL_TOF_RESPONSE_TIMEOUT = 1,
	IWL_TOF_RESPONSE_ABORTED = 4,
	IWL_TOF_RESPONSE_FAILED  = 5,
}; /* LOCATION_RNG_RSP_STATUS */

/**
 * struct iwl_tof_range_rsp_ntfy_v5 - ranging response notification
 * @request_id: A Token ID of the corresponding Range request
 * @request_status: status of current measurement session, one of
 *	&enum iwl_tof_response_status.
 * @last_in_batch: reprot policy (when not all responses are uploaded at once)
 * @num_of_aps: Number of APs to measure (error if > IWL_MVM_TOF_MAX_APS)
 * @ap: per-AP data
 */
struct iwl_tof_range_rsp_ntfy_v5 {
	u8 request_id;
	u8 request_status;
	u8 last_in_batch;
	u8 num_of_aps;
	struct iwl_tof_range_rsp_ap_entry_ntfy_v3 ap[IWL_MVM_TOF_MAX_APS];
} __packed; /* LOCATION_RANGE_RSP_NTFY_API_S_VER_5 */

/**
 * struct iwl_tof_range_rsp_ntfy_v6 - ranging response notification
 * @request_id: A Token ID of the corresponding Range request
 * @num_of_aps: Number of APs results
 * @last_report: 1 if no more FTM sessions are scheduled, 0 otherwise.
 * @reserved: reserved
 * @ap: per-AP data
 */
struct iwl_tof_range_rsp_ntfy_v6 {
	u8 request_id;
	u8 num_of_aps;
	u8 last_report;
	u8 reserved;
	struct iwl_tof_range_rsp_ap_entry_ntfy_v4 ap[IWL_MVM_TOF_MAX_APS];
} __packed; /* LOCATION_RANGE_RSP_NTFY_API_S_VER_6 */

/**
 * struct iwl_tof_range_rsp_ntfy - ranging response notification
 * @request_id: A Token ID of the corresponding Range request
 * @num_of_aps: Number of APs results
 * @last_report: 1 if no more FTM sessions are scheduled, 0 otherwise.
 * @reserved: reserved
 * @ap: per-AP data
 */
struct iwl_tof_range_rsp_ntfy {
	u8 request_id;
	u8 num_of_aps;
	u8 last_report;
	u8 reserved;
	struct iwl_tof_range_rsp_ap_entry_ntfy ap[IWL_MVM_TOF_MAX_APS];
} __packed; /* LOCATION_RANGE_RSP_NTFY_API_S_VER_7 */

#define IWL_MVM_TOF_MCSI_BUF_SIZE  (245)
/**
 * struct iwl_tof_mcsi_notif - used for debug
 * @token: token ID for the current session
 * @role: '0' - initiator, '1' - responder
 * @reserved: reserved
 * @initiator_bssid: initiator machine
 * @responder_bssid: responder machine
 * @mcsi_buffer: debug data
 */
struct iwl_tof_mcsi_notif {
	u8 token;
	u8 role;
	__le16 reserved;
	u8 initiator_bssid[ETH_ALEN];
	u8 responder_bssid[ETH_ALEN];
	u8 mcsi_buffer[IWL_MVM_TOF_MCSI_BUF_SIZE * 4];
} __packed;

/**
 * struct iwl_tof_range_abort_cmd
 * @request_id: corresponds to a range request
 * @reserved: reserved
 */
struct iwl_tof_range_abort_cmd {
	u8 request_id;
	u8 reserved[3];
} __packed;

enum ftm_responder_stats_flags {
	FTM_RESP_STAT_NON_ASAP_STARTED = BIT(0),
	FTM_RESP_STAT_NON_ASAP_IN_WIN = BIT(1),
	FTM_RESP_STAT_NON_ASAP_OUT_WIN = BIT(2),
	FTM_RESP_STAT_TRIGGER_DUP = BIT(3),
	FTM_RESP_STAT_DUP = BIT(4),
	FTM_RESP_STAT_DUP_IN_WIN = BIT(5),
	FTM_RESP_STAT_DUP_OUT_WIN = BIT(6),
	FTM_RESP_STAT_SCHED_SUCCESS = BIT(7),
	FTM_RESP_STAT_ASAP_REQ = BIT(8),
	FTM_RESP_STAT_NON_ASAP_REQ = BIT(9),
	FTM_RESP_STAT_ASAP_RESP = BIT(10),
	FTM_RESP_STAT_NON_ASAP_RESP = BIT(11),
	FTM_RESP_STAT_FAIL_INITIATOR_INACTIVE = BIT(12),
	FTM_RESP_STAT_FAIL_INITIATOR_OUT_WIN = BIT(13),
	FTM_RESP_STAT_FAIL_INITIATOR_RETRY_LIM = BIT(14),
	FTM_RESP_STAT_FAIL_NEXT_SERVED = BIT(15),
	FTM_RESP_STAT_FAIL_TRIGGER_ERR = BIT(16),
	FTM_RESP_STAT_FAIL_GC = BIT(17),
	FTM_RESP_STAT_SUCCESS = BIT(18),
	FTM_RESP_STAT_INTEL_IE = BIT(19),
	FTM_RESP_STAT_INITIATOR_ACTIVE = BIT(20),
	FTM_RESP_STAT_MEASUREMENTS_AVAILABLE = BIT(21),
	FTM_RESP_STAT_TRIGGER_UNKNOWN = BIT(22),
	FTM_RESP_STAT_PROCESS_FAIL = BIT(23),
	FTM_RESP_STAT_ACK = BIT(24),
	FTM_RESP_STAT_NACK = BIT(25),
	FTM_RESP_STAT_INVALID_INITIATOR_ID = BIT(26),
	FTM_RESP_STAT_TIMER_MIN_DELTA = BIT(27),
	FTM_RESP_STAT_INITIATOR_REMOVED = BIT(28),
	FTM_RESP_STAT_INITIATOR_ADDED = BIT(29),
	FTM_RESP_STAT_ERR_LIST_FULL = BIT(30),
	FTM_RESP_STAT_INITIATOR_SCHED_NOW = BIT(31),
}; /* RESP_IND_E */

/**
 * struct iwl_ftm_responder_stats - FTM responder statistics
 * @addr: initiator address
 * @success_ftm: number of successful ftm frames
 * @ftm_per_burst: num of FTM frames that were received
 * @flags: &enum ftm_responder_stats_flags
 * @duration: actual duration of FTM
 * @allocated_duration: time that was allocated for this FTM session
 * @bw: FTM request bandwidth
 * @rate: FTM request rate
 * @reserved: for alingment and future use
 */
struct iwl_ftm_responder_stats {
	u8 addr[ETH_ALEN];
	u8 success_ftm;
	u8 ftm_per_burst;
	__le32 flags;
	__le32 duration;
	__le32 allocated_duration;
	u8 bw;
	u8 rate;
	__le16 reserved;
} __packed; /* TOF_RESPONDER_STATISTICS_NTFY_S_VER_2 */

#define IWL_CSI_CHUNK_CTL_NUM_MASK	0x3
#define IWL_CSI_CHUNK_CTL_IDX_MASK	0xc

struct iwl_csi_chunk_notification {
	__le32 token;
	__le16 seq;
	__le16 ctl;
	__le32 size;
	u8 data[];
} __packed; /* CSI_CHUNKS_HDR_NTFY_API_S_VER_1 */

#endif /* __iwl_fw_api_location_h__ */
