/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2017 Intel Deutschland GmbH
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
 * Copyright(c) 2017 Intel Deutschland GmbH
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

#ifndef __iwl_fw_api_rs_h__
#define __iwl_fw_api_rs_h__

#include "mac.h"

/**
 * enum iwl_tlc_mng_cfg_flags_enum - options for TLC config flags
 * @IWL_TLC_MNG_CFG_FLAGS_CCK_MSK: CCK support
 * @IWL_TLC_MNG_CFG_FLAGS_DD_MSK: enable DD
 * @IWL_TLC_MNG_CFG_FLAGS_STBC_MSK: enable STBC
 * @IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK: enable LDPC
 * @IWL_TLC_MNG_CFG_FLAGS_BF_MSK: enable BFER
 * @IWL_TLC_MNG_CFG_FLAGS_DCM_MSK: enable DCM
 */
enum iwl_tlc_mng_cfg_flags_enum {
	IWL_TLC_MNG_CFG_FLAGS_CCK_MSK	= BIT(0),
	IWL_TLC_MNG_CFG_FLAGS_DD_MSK	= BIT(1),
	IWL_TLC_MNG_CFG_FLAGS_STBC_MSK	= BIT(2),
	IWL_TLC_MNG_CFG_FLAGS_LDPC_MSK	= BIT(3),
	IWL_TLC_MNG_CFG_FLAGS_BF_MSK	= BIT(4),
	IWL_TLC_MNG_CFG_FLAGS_DCM_MSK	= BIT(5),
};

/**
 * enum iwl_tlc_mng_cfg_cw_enum - channel width options
 * @IWL_TLC_MNG_MAX_CH_WIDTH_20MHZ: 20MHZ channel
 * @IWL_TLC_MNG_MAX_CH_WIDTH_40MHZ: 40MHZ channel
 * @IWL_TLC_MNG_MAX_CH_WIDTH_80MHZ: 80MHZ channel
 * @IWL_TLC_MNG_MAX_CH_WIDTH_160MHZ: 160MHZ channel
 * @IWL_TLC_MNG_MAX_CH_WIDTH_LAST: maximum value
 */
enum iwl_tlc_mng_cfg_cw_enum {
	IWL_TLC_MNG_MAX_CH_WIDTH_20MHZ,
	IWL_TLC_MNG_MAX_CH_WIDTH_40MHZ,
	IWL_TLC_MNG_MAX_CH_WIDTH_80MHZ,
	IWL_TLC_MNG_MAX_CH_WIDTH_160MHZ,
	IWL_TLC_MNG_MAX_CH_WIDTH_LAST = IWL_TLC_MNG_MAX_CH_WIDTH_160MHZ,
};

/**
 * enum iwl_tlc_mng_cfg_chains_enum - possible chains
 * @IWL_TLC_MNG_CHAIN_A_MSK: chain A
 * @IWL_TLC_MNG_CHAIN_B_MSK: chain B
 * @IWL_TLC_MNG_CHAIN_C_MSK: chain C
 */
enum iwl_tlc_mng_cfg_chains_enum {
	IWL_TLC_MNG_CHAIN_A_MSK = BIT(0),
	IWL_TLC_MNG_CHAIN_B_MSK = BIT(1),
	IWL_TLC_MNG_CHAIN_C_MSK = BIT(2),
};

/**
 * enum iwl_tlc_mng_cfg_gi_enum - guard interval options
 * @IWL_TLC_MNG_SGI_20MHZ_MSK: enable short GI for 20MHZ
 * @IWL_TLC_MNG_SGI_40MHZ_MSK: enable short GI for 40MHZ
 * @IWL_TLC_MNG_SGI_80MHZ_MSK: enable short GI for 80MHZ
 * @IWL_TLC_MNG_SGI_160MHZ_MSK: enable short GI for 160MHZ
 */
enum iwl_tlc_mng_cfg_gi_enum {
	IWL_TLC_MNG_SGI_20MHZ_MSK  = BIT(0),
	IWL_TLC_MNG_SGI_40MHZ_MSK  = BIT(1),
	IWL_TLC_MNG_SGI_80MHZ_MSK  = BIT(2),
	IWL_TLC_MNG_SGI_160MHZ_MSK = BIT(3),
};

/**
 * enum iwl_tlc_mng_cfg_mode_enum - supported modes
 * @IWL_TLC_MNG_MODE_CCK: enable CCK
 * @IWL_TLC_MNG_MODE_OFDM_NON_HT: enable OFDM (non HT)
 * @IWL_TLC_MNG_MODE_NON_HT: enable non HT
 * @IWL_TLC_MNG_MODE_HT: enable HT
 * @IWL_TLC_MNG_MODE_VHT: enable VHT
 * @IWL_TLC_MNG_MODE_HE: enable HE
 * @IWL_TLC_MNG_MODE_INVALID: invalid value
 * @IWL_TLC_MNG_MODE_NUM: a count of possible modes
 */
enum iwl_tlc_mng_cfg_mode_enum {
	IWL_TLC_MNG_MODE_CCK = 0,
	IWL_TLC_MNG_MODE_OFDM_NON_HT = IWL_TLC_MNG_MODE_CCK,
	IWL_TLC_MNG_MODE_NON_HT = IWL_TLC_MNG_MODE_CCK,
	IWL_TLC_MNG_MODE_HT,
	IWL_TLC_MNG_MODE_VHT,
	IWL_TLC_MNG_MODE_HE,
	IWL_TLC_MNG_MODE_INVALID,
	IWL_TLC_MNG_MODE_NUM = IWL_TLC_MNG_MODE_INVALID,
};

/**
 * enum iwl_tlc_mng_vht_he_types_enum - VHT HE types
 * @IWL_TLC_MNG_VALID_VHT_HE_TYPES_SU: VHT HT single user
 * @IWL_TLC_MNG_VALID_VHT_HE_TYPES_SU_EXT: VHT HT single user extended
 * @IWL_TLC_MNG_VALID_VHT_HE_TYPES_MU: VHT HT multiple users
 * @IWL_TLC_MNG_VALID_VHT_HE_TYPES_TRIG_BASED: trigger based
 * @IWL_TLC_MNG_VALID_VHT_HE_TYPES_NUM: a count of possible types
 */
enum iwl_tlc_mng_vht_he_types_enum {
	IWL_TLC_MNG_VALID_VHT_HE_TYPES_SU = 0,
	IWL_TLC_MNG_VALID_VHT_HE_TYPES_SU_EXT,
	IWL_TLC_MNG_VALID_VHT_HE_TYPES_MU,
	IWL_TLC_MNG_VALID_VHT_HE_TYPES_TRIG_BASED,
	IWL_TLC_MNG_VALID_VHT_HE_TYPES_NUM =
		IWL_TLC_MNG_VALID_VHT_HE_TYPES_TRIG_BASED,

};

/**
 * enum iwl_tlc_mng_ht_rates_enum - HT/VHT rates
 * @IWL_TLC_MNG_HT_RATE_MCS0: index of MCS0
 * @IWL_TLC_MNG_HT_RATE_MCS1: index of MCS1
 * @IWL_TLC_MNG_HT_RATE_MCS2: index of MCS2
 * @IWL_TLC_MNG_HT_RATE_MCS3: index of MCS3
 * @IWL_TLC_MNG_HT_RATE_MCS4: index of MCS4
 * @IWL_TLC_MNG_HT_RATE_MCS5: index of MCS5
 * @IWL_TLC_MNG_HT_RATE_MCS6: index of MCS6
 * @IWL_TLC_MNG_HT_RATE_MCS7: index of MCS7
 * @IWL_TLC_MNG_HT_RATE_MCS8: index of MCS8
 * @IWL_TLC_MNG_HT_RATE_MCS9: index of MCS9
 * @IWL_TLC_MNG_HT_RATE_MAX: maximal rate for HT/VHT
 */
enum iwl_tlc_mng_ht_rates_enum {
	IWL_TLC_MNG_HT_RATE_MCS0 = 0,
	IWL_TLC_MNG_HT_RATE_MCS1,
	IWL_TLC_MNG_HT_RATE_MCS2,
	IWL_TLC_MNG_HT_RATE_MCS3,
	IWL_TLC_MNG_HT_RATE_MCS4,
	IWL_TLC_MNG_HT_RATE_MCS5,
	IWL_TLC_MNG_HT_RATE_MCS6,
	IWL_TLC_MNG_HT_RATE_MCS7,
	IWL_TLC_MNG_HT_RATE_MCS8,
	IWL_TLC_MNG_HT_RATE_MCS9,
	IWL_TLC_MNG_HT_RATE_MAX = IWL_TLC_MNG_HT_RATE_MCS9,
};

/* Maximum supported tx antennas number */
#define MAX_RS_ANT_NUM 3

/**
 * struct tlc_config_cmd - TLC configuration
 * @sta_id: station id
 * @reserved1: reserved
 * @max_supp_ch_width: channel width
 * @flags: bitmask of %IWL_TLC_MNG_CONFIG_FLAGS_ENABLE_\*
 * @chains: bitmask of %IWL_TLC_MNG_CHAIN_\*
 * @max_supp_ss: valid values are 0-3, 0 - spatial streams are not supported
 * @valid_vht_he_types: bitmap of %IWL_TLC_MNG_VALID_VHT_HE_TYPES_\*
 * @non_ht_supp_rates: bitmap of supported legacy rates
 * @ht_supp_rates: bitmap of supported HT/VHT rates, valid bits are 0-9
 * @mode: modulation type %IWL_TLC_MNG_MODE_\*
 * @reserved2: reserved
 * @he_supp_rates: bitmap of supported HE rates
 * @sgi_ch_width_supp: bitmap of SGI support per channel width
 * @he_gi_support: 11ax HE guard interval
 * @max_ampdu_cnt: max AMPDU size (frames count)
 */
struct iwl_tlc_config_cmd {
	u8 sta_id;
	u8 reserved1[3];
	u8 max_supp_ch_width;
	u8 chains;
	u8 max_supp_ss;
	u8 valid_vht_he_types;
	__le16 flags;
	__le16 non_ht_supp_rates;
	__le16 ht_supp_rates[MAX_RS_ANT_NUM];
	u8 mode;
	u8 reserved2;
	__le16 he_supp_rates;
	u8 sgi_ch_width_supp;
	u8 he_gi_support;
	__le32 max_ampdu_cnt;
} __packed; /* TLC_MNG_CONFIG_CMD_API_S_VER_1 */

#define IWL_TLC_NOTIF_INIT_RATE_POS 0
#define IWL_TLC_NOTIF_INIT_RATE_MSK BIT(IWL_TLC_NOTIF_INIT_RATE_POS)
#define IWL_TLC_NOTIF_REQ_INTERVAL (500)

/**
 * struct iwl_tlc_notif_req_config_cmd - request notif on specific changes
 * @sta_id: relevant station
 * @reserved1: reserved
 * @flags: bitmap of requested notifications %IWL_TLC_NOTIF_INIT_\*
 * @interval: minimum time between notifications from TLC to the driver (msec)
 * @reserved2: reserved
 */
struct iwl_tlc_notif_req_config_cmd {
	u8 sta_id;
	u8 reserved1;
	__le16 flags;
	__le16 interval;
	__le16 reserved2;
} __packed; /* TLC_MNG_NOTIF_REQ_CMD_API_S_VER_1 */

/**
 * struct iwl_tlc_update_notif - TLC notification from FW
 * @sta_id: station id
 * @reserved: reserved
 * @flags: bitmap of notifications reported
 * @values: field per flag in struct iwl_tlc_notif_req_config_cmd
 */
struct iwl_tlc_update_notif {
	u8 sta_id;
	u8 reserved;
	__le16 flags;
	__le32 values[16];
} __packed; /* TLC_MNG_UPDATE_NTFY_API_S_VER_1 */

/**
 * enum iwl_tlc_debug_flags - debug options
 * @IWL_TLC_DEBUG_FIXED_RATE: set fixed rate for rate scaling
 * @IWL_TLC_DEBUG_STATS_TH: threshold for sending statistics to the driver, in
 *	frames
 * @IWL_TLC_DEBUG_STATS_TIME_TH: threshold for sending statistics to the
 *	driver, in msec
 * @IWL_TLC_DEBUG_AGG_TIME_LIM: time limit for a BA session
 * @IWL_TLC_DEBUG_AGG_DIS_START_TH: frame with try-count greater than this
 *	threshold should not start an aggregation session
 * @IWL_TLC_DEBUG_AGG_FRAME_CNT_LIM: set max number of frames in an aggregation
 * @IWL_TLC_DEBUG_RENEW_ADDBA_DELAY: delay between retries of ADD BA
 * @IWL_TLC_DEBUG_START_AC_RATE_IDX: frames per second to start a BA session
 * @IWL_TLC_DEBUG_NO_FAR_RANGE_TWEAK: disable BW scaling
 */
enum iwl_tlc_debug_flags {
	IWL_TLC_DEBUG_FIXED_RATE,
	IWL_TLC_DEBUG_STATS_TH,
	IWL_TLC_DEBUG_STATS_TIME_TH,
	IWL_TLC_DEBUG_AGG_TIME_LIM,
	IWL_TLC_DEBUG_AGG_DIS_START_TH,
	IWL_TLC_DEBUG_AGG_FRAME_CNT_LIM,
	IWL_TLC_DEBUG_RENEW_ADDBA_DELAY,
	IWL_TLC_DEBUG_START_AC_RATE_IDX,
	IWL_TLC_DEBUG_NO_FAR_RANGE_TWEAK,
}; /* TLC_MNG_DEBUG_FLAGS_API_E_VER_1 */

/**
 * struct iwl_dhc_tlc_dbg - fixed debug config
 * @sta_id: bit 0 - enable/disable, bits 1 - 7 hold station id
 * @reserved1: reserved
 * @flags: bitmap of %IWL_TLC_DEBUG_\*
 * @fixed_rate: rate value
 * @stats_threshold: if number of tx-ed frames is greater, send statistics
 * @time_threshold: statistics threshold in usec
 * @agg_time_lim: max agg time
 * @agg_dis_start_threshold: frames with try-cont greater than this count will
 *			     not be aggregated
 * @agg_frame_count_lim: agg size
 * @addba_retry_delay: delay between retries of ADD BA
 * @start_ac_rate_idx: frames per second to start a BA session
 * @no_far_range_tweak: disable BW scaling
 * @reserved2: reserved
 */
struct iwl_dhc_tlc_cmd {
	u8 sta_id;
	u8 reserved1[3];
	__le32 flags;
	__le32 fixed_rate;
	__le16 stats_threshold;
	__le16 time_threshold;
	__le16 agg_time_lim;
	__le16 agg_dis_start_threshold;
	__le16 agg_frame_count_lim;
	__le16 addba_retry_delay;
	u8 start_ac_rate_idx[IEEE80211_NUM_ACS];
	u8 no_far_range_tweak;
	u8 reserved2[3];
} __packed;

/*
 * These serve as indexes into
 * struct iwl_rate_info fw_rate_idx_to_plcp[IWL_RATE_COUNT];
 * TODO: avoid overlap between legacy and HT rates
 */
enum {
	IWL_RATE_1M_INDEX = 0,
	IWL_FIRST_CCK_RATE = IWL_RATE_1M_INDEX,
	IWL_RATE_2M_INDEX,
	IWL_RATE_5M_INDEX,
	IWL_RATE_11M_INDEX,
	IWL_LAST_CCK_RATE = IWL_RATE_11M_INDEX,
	IWL_RATE_6M_INDEX,
	IWL_FIRST_OFDM_RATE = IWL_RATE_6M_INDEX,
	IWL_RATE_MCS_0_INDEX = IWL_RATE_6M_INDEX,
	IWL_FIRST_HT_RATE = IWL_RATE_MCS_0_INDEX,
	IWL_FIRST_VHT_RATE = IWL_RATE_MCS_0_INDEX,
	IWL_RATE_9M_INDEX,
	IWL_RATE_12M_INDEX,
	IWL_RATE_MCS_1_INDEX = IWL_RATE_12M_INDEX,
	IWL_RATE_18M_INDEX,
	IWL_RATE_MCS_2_INDEX = IWL_RATE_18M_INDEX,
	IWL_RATE_24M_INDEX,
	IWL_RATE_MCS_3_INDEX = IWL_RATE_24M_INDEX,
	IWL_RATE_36M_INDEX,
	IWL_RATE_MCS_4_INDEX = IWL_RATE_36M_INDEX,
	IWL_RATE_48M_INDEX,
	IWL_RATE_MCS_5_INDEX = IWL_RATE_48M_INDEX,
	IWL_RATE_54M_INDEX,
	IWL_RATE_MCS_6_INDEX = IWL_RATE_54M_INDEX,
	IWL_LAST_NON_HT_RATE = IWL_RATE_54M_INDEX,
	IWL_RATE_60M_INDEX,
	IWL_RATE_MCS_7_INDEX = IWL_RATE_60M_INDEX,
	IWL_LAST_HT_RATE = IWL_RATE_MCS_7_INDEX,
	IWL_RATE_MCS_8_INDEX,
	IWL_RATE_MCS_9_INDEX,
	IWL_LAST_VHT_RATE = IWL_RATE_MCS_9_INDEX,
	IWL_RATE_COUNT_LEGACY = IWL_LAST_NON_HT_RATE + 1,
	IWL_RATE_COUNT = IWL_LAST_VHT_RATE + 1,
};

#define IWL_RATE_BIT_MSK(r) BIT(IWL_RATE_##r##M_INDEX)

/* fw API values for legacy bit rates, both OFDM and CCK */
enum {
	IWL_RATE_6M_PLCP  = 13,
	IWL_RATE_9M_PLCP  = 15,
	IWL_RATE_12M_PLCP = 5,
	IWL_RATE_18M_PLCP = 7,
	IWL_RATE_24M_PLCP = 9,
	IWL_RATE_36M_PLCP = 11,
	IWL_RATE_48M_PLCP = 1,
	IWL_RATE_54M_PLCP = 3,
	IWL_RATE_1M_PLCP  = 10,
	IWL_RATE_2M_PLCP  = 20,
	IWL_RATE_5M_PLCP  = 55,
	IWL_RATE_11M_PLCP = 110,
	IWL_RATE_INVM_PLCP = -1,
};

/*
 * rate_n_flags bit fields
 *
 * The 32-bit value has different layouts in the low 8 bites depending on the
 * format. There are three formats, HT, VHT and legacy (11abg, with subformats
 * for CCK and OFDM).
 *
 * High-throughput (HT) rate format
 *	bit 8 is 1, bit 26 is 0, bit 9 is 0 (OFDM)
 * Very High-throughput (VHT) rate format
 *	bit 8 is 0, bit 26 is 1, bit 9 is 0 (OFDM)
 * Legacy OFDM rate format for bits 7:0
 *	bit 8 is 0, bit 26 is 0, bit 9 is 0 (OFDM)
 * Legacy CCK rate format for bits 7:0:
 *	bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK)
 */

/* Bit 8: (1) HT format, (0) legacy or VHT format */
#define RATE_MCS_HT_POS 8
#define RATE_MCS_HT_MSK (1 << RATE_MCS_HT_POS)

/* Bit 9: (1) CCK, (0) OFDM.  HT (bit 8) must be "0" for this bit to be valid */
#define RATE_MCS_CCK_POS 9
#define RATE_MCS_CCK_MSK (1 << RATE_MCS_CCK_POS)

/* Bit 26: (1) VHT format, (0) legacy format in bits 8:0 */
#define RATE_MCS_VHT_POS 26
#define RATE_MCS_VHT_MSK (1 << RATE_MCS_VHT_POS)


/*
 * High-throughput (HT) rate format for bits 7:0
 *
 *  2-0:  MCS rate base
 *        0)   6 Mbps
 *        1)  12 Mbps
 *        2)  18 Mbps
 *        3)  24 Mbps
 *        4)  36 Mbps
 *        5)  48 Mbps
 *        6)  54 Mbps
 *        7)  60 Mbps
 *  4-3:  0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 *    5:  Value of 0x20 in bits 7:0 indicates 6 Mbps HT40 duplicate data
 *  (bits 7-6 are zero)
 *
 * Together the low 5 bits work out to the MCS index because we don't
 * support MCSes above 15/23, and 0-7 have one stream, 8-15 have two
 * streams and 16-23 have three streams. We could also support MCS 32
 * which is the duplicate 20 MHz MCS (bit 5 set, all others zero.)
 */
#define RATE_HT_MCS_RATE_CODE_MSK	0x7
#define RATE_HT_MCS_NSS_POS             3
#define RATE_HT_MCS_NSS_MSK             (3 << RATE_HT_MCS_NSS_POS)

/* Bit 10: (1) Use Green Field preamble */
#define RATE_HT_MCS_GF_POS		10
#define RATE_HT_MCS_GF_MSK		(1 << RATE_HT_MCS_GF_POS)

#define RATE_HT_MCS_INDEX_MSK		0x3f

/*
 * Very High-throughput (VHT) rate format for bits 7:0
 *
 *  3-0:  VHT MCS (0-9)
 *  5-4:  number of streams - 1:
 *        0)  Single stream (SISO)
 *        1)  Dual stream (MIMO)
 *        2)  Triple stream (MIMO)
 */

/* Bit 4-5: (0) SISO, (1) MIMO2 (2) MIMO3 */
#define RATE_VHT_MCS_RATE_CODE_MSK	0xf
#define RATE_VHT_MCS_NSS_POS		4
#define RATE_VHT_MCS_NSS_MSK		(3 << RATE_VHT_MCS_NSS_POS)

/*
 * Legacy OFDM rate format for bits 7:0
 *
 *  3-0:  0xD)   6 Mbps
 *        0xF)   9 Mbps
 *        0x5)  12 Mbps
 *        0x7)  18 Mbps
 *        0x9)  24 Mbps
 *        0xB)  36 Mbps
 *        0x1)  48 Mbps
 *        0x3)  54 Mbps
 * (bits 7-4 are 0)
 *
 * Legacy CCK rate format for bits 7:0:
 * bit 8 is 0, bit 26 is 0, bit 9 is 1 (CCK):
 *
 *  6-0:   10)  1 Mbps
 *         20)  2 Mbps
 *         55)  5.5 Mbps
 *        110)  11 Mbps
 * (bit 7 is 0)
 */
#define RATE_LEGACY_RATE_MSK 0xff

/* Bit 10 - OFDM HE */
#define RATE_MCS_OFDM_HE_POS		10
#define RATE_MCS_OFDM_HE_MSK		BIT(RATE_MCS_OFDM_HE_POS)

/*
 * Bit 11-12: (0) 20MHz, (1) 40MHz, (2) 80MHz, (3) 160MHz
 * 0 and 1 are valid for HT and VHT, 2 and 3 only for VHT
 */
#define RATE_MCS_CHAN_WIDTH_POS		11
#define RATE_MCS_CHAN_WIDTH_MSK		(3 << RATE_MCS_CHAN_WIDTH_POS)
#define RATE_MCS_CHAN_WIDTH_20		(0 << RATE_MCS_CHAN_WIDTH_POS)
#define RATE_MCS_CHAN_WIDTH_40		(1 << RATE_MCS_CHAN_WIDTH_POS)
#define RATE_MCS_CHAN_WIDTH_80		(2 << RATE_MCS_CHAN_WIDTH_POS)
#define RATE_MCS_CHAN_WIDTH_160		(3 << RATE_MCS_CHAN_WIDTH_POS)

/* Bit 13: (1) Short guard interval (0.4 usec), (0) normal GI (0.8 usec) */
#define RATE_MCS_SGI_POS		13
#define RATE_MCS_SGI_MSK		(1 << RATE_MCS_SGI_POS)

/* Bit 14-16: Antenna selection (1) Ant A, (2) Ant B, (4) Ant C */
#define RATE_MCS_ANT_POS		14
#define RATE_MCS_ANT_A_MSK		(1 << RATE_MCS_ANT_POS)
#define RATE_MCS_ANT_B_MSK		(2 << RATE_MCS_ANT_POS)
#define RATE_MCS_ANT_C_MSK		(4 << RATE_MCS_ANT_POS)
#define RATE_MCS_ANT_AB_MSK		(RATE_MCS_ANT_A_MSK | \
					 RATE_MCS_ANT_B_MSK)
#define RATE_MCS_ANT_ABC_MSK		(RATE_MCS_ANT_AB_MSK | \
					 RATE_MCS_ANT_C_MSK)
#define RATE_MCS_ANT_MSK		RATE_MCS_ANT_ABC_MSK

/* Bit 17: (0) SS, (1) SS*2 */
#define RATE_MCS_STBC_POS		17
#define RATE_MCS_STBC_MSK		BIT(RATE_MCS_STBC_POS)

/* Bit 18: OFDM-HE dual carrier mode */
#define RATE_HE_DUAL_CARRIER_MODE	18
#define RATE_HE_DUAL_CARRIER_MODE_MSK	BIT(RATE_HE_DUAL_CARRIER_MODE)

/* Bit 19: (0) Beamforming is off, (1) Beamforming is on */
#define RATE_MCS_BF_POS			19
#define RATE_MCS_BF_MSK			(1 << RATE_MCS_BF_POS)

/*
 * Bit 20-21: HE guard interval and LTF type.
 * (0) 1xLTF+1.6us, (1) 2xLTF+0.8us,
 * (2) 2xLTF+1.6us, (3) 4xLTF+3.2us
 */
#define RATE_MCS_HE_GI_LTF_POS		20
#define RATE_MCS_HE_GI_LTF_MSK		(3 << RATE_MCS_HE_GI_LTF_POS)

/* Bit 22-23: HE type. (0) SU, (1) SU_EXT, (2) MU, (3) trigger based */
#define RATE_MCS_HE_TYPE_POS		22
#define RATE_MCS_HE_TYPE_MSK		(3 << RATE_MCS_HE_TYPE_POS)

/* Bit 24-25: (0) 20MHz (no dup), (1) 2x20MHz, (2) 4x20MHz, 3 8x20MHz */
#define RATE_MCS_DUP_POS		24
#define RATE_MCS_DUP_MSK		(3 << RATE_MCS_DUP_POS)

/* Bit 27: (1) LDPC enabled, (0) LDPC disabled */
#define RATE_MCS_LDPC_POS		27
#define RATE_MCS_LDPC_MSK		(1 << RATE_MCS_LDPC_POS)


/* Link Quality definitions */

/* # entries in rate scale table to support Tx retries */
#define  LQ_MAX_RETRY_NUM 16

/* Link quality command flags bit fields */

/* Bit 0: (0) Don't use RTS (1) Use RTS */
#define LQ_FLAG_USE_RTS_POS             0
#define LQ_FLAG_USE_RTS_MSK	        (1 << LQ_FLAG_USE_RTS_POS)

/* Bit 1-3: LQ command color. Used to match responses to LQ commands */
#define LQ_FLAG_COLOR_POS               1
#define LQ_FLAG_COLOR_MSK               (7 << LQ_FLAG_COLOR_POS)
#define LQ_FLAG_COLOR_GET(_f)		(((_f) & LQ_FLAG_COLOR_MSK) >>\
					 LQ_FLAG_COLOR_POS)
#define LQ_FLAGS_COLOR_INC(_c)		((((_c) + 1) << LQ_FLAG_COLOR_POS) &\
					 LQ_FLAG_COLOR_MSK)
#define LQ_FLAG_COLOR_SET(_f, _c)	((_c) | ((_f) & ~LQ_FLAG_COLOR_MSK))

/* Bit 4-5: Tx RTS BW Signalling
 * (0) No RTS BW signalling
 * (1) Static BW signalling
 * (2) Dynamic BW signalling
 */
#define LQ_FLAG_RTS_BW_SIG_POS          4
#define LQ_FLAG_RTS_BW_SIG_NONE         (0 << LQ_FLAG_RTS_BW_SIG_POS)
#define LQ_FLAG_RTS_BW_SIG_STATIC       (1 << LQ_FLAG_RTS_BW_SIG_POS)
#define LQ_FLAG_RTS_BW_SIG_DYNAMIC      (2 << LQ_FLAG_RTS_BW_SIG_POS)

/* Bit 6: (0) No dynamic BW selection (1) Allow dynamic BW selection
 * Dyanmic BW selection allows Tx with narrower BW then requested in rates
 */
#define LQ_FLAG_DYNAMIC_BW_POS          6
#define LQ_FLAG_DYNAMIC_BW_MSK          (1 << LQ_FLAG_DYNAMIC_BW_POS)

/* Single Stream Tx Parameters (lq_cmd->ss_params)
 * Flags to control a smart FW decision about whether BFER/STBC/SISO will be
 * used for single stream Tx.
 */

/* Bit 0-1: Max STBC streams allowed. Can be 0-3.
 * (0) - No STBC allowed
 * (1) - 2x1 STBC allowed (HT/VHT)
 * (2) - 4x2 STBC allowed (HT/VHT)
 * (3) - 3x2 STBC allowed (HT only)
 * All our chips are at most 2 antennas so only (1) is valid for now.
 */
#define LQ_SS_STBC_ALLOWED_POS          0
#define LQ_SS_STBC_ALLOWED_MSK		(3 << LQ_SS_STBC_ALLOWED_MSK)

/* 2x1 STBC is allowed */
#define LQ_SS_STBC_1SS_ALLOWED		(1 << LQ_SS_STBC_ALLOWED_POS)

/* Bit 2: Beamformer (VHT only) is allowed */
#define LQ_SS_BFER_ALLOWED_POS		2
#define LQ_SS_BFER_ALLOWED		(1 << LQ_SS_BFER_ALLOWED_POS)

/* Bit 3: Force BFER or STBC for testing
 * If this is set:
 * If BFER is allowed then force the ucode to choose BFER else
 * If STBC is allowed then force the ucode to choose STBC over SISO
 */
#define LQ_SS_FORCE_POS			3
#define LQ_SS_FORCE			(1 << LQ_SS_FORCE_POS)

/* Bit 31: ss_params field is valid. Used for FW backward compatibility
 * with other drivers which don't support the ss_params API yet
 */
#define LQ_SS_PARAMS_VALID_POS		31
#define LQ_SS_PARAMS_VALID		(1 << LQ_SS_PARAMS_VALID_POS)

/**
 * struct iwl_lq_cmd - link quality command
 * @sta_id: station to update
 * @reduced_tpc: reduced transmit power control value
 * @control: not used
 * @flags: combination of LQ_FLAG_*
 * @mimo_delim: the first SISO index in rs_table, which separates MIMO
 *	and SISO rates
 * @single_stream_ant_msk: best antenna for SISO (can be dual in CDD).
 *	Should be ANT_[ABC]
 * @dual_stream_ant_msk: best antennas for MIMO, combination of ANT_[ABC]
 * @initial_rate_index: first index from rs_table per AC category
 * @agg_time_limit: aggregation max time threshold in usec/100, meaning
 *	value of 100 is one usec. Range is 100 to 8000
 * @agg_disable_start_th: try-count threshold for starting aggregation.
 *	If a frame has higher try-count, it should not be selected for
 *	starting an aggregation sequence.
 * @agg_frame_cnt_limit: max frame count in an aggregation.
 *	0: no limit
 *	1: no aggregation (one frame per aggregation)
 *	2 - 0x3f: maximal number of frames (up to 3f == 63)
 * @reserved2: reserved
 * @rs_table: array of rates for each TX try, each is rate_n_flags,
 *	meaning it is a combination of RATE_MCS_* and IWL_RATE_*_PLCP
 * @ss_params: single stream features. declare whether STBC or BFER are allowed.
 */
struct iwl_lq_cmd {
	u8 sta_id;
	u8 reduced_tpc;
	__le16 control;
	/* LINK_QUAL_GENERAL_PARAMS_API_S_VER_1 */
	u8 flags;
	u8 mimo_delim;
	u8 single_stream_ant_msk;
	u8 dual_stream_ant_msk;
	u8 initial_rate_index[AC_NUM];
	/* LINK_QUAL_AGG_PARAMS_API_S_VER_1 */
	__le16 agg_time_limit;
	u8 agg_disable_start_th;
	u8 agg_frame_cnt_limit;
	__le32 reserved2;
	__le32 rs_table[LQ_MAX_RETRY_NUM];
	__le32 ss_params;
}; /* LINK_QUALITY_CMD_API_S_VER_1 */

#endif /* __iwl_fw_api_rs_h__ */
