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

#ifndef __fw_api_rs_h__
#define __fw_api_rs_h__

#include "fw-api-mac.h"

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
#define RATE_MCS_ANT_NUM 3

/* Bit 17-18: (0) SS, (1) SS*2 */
#define RATE_MCS_STBC_POS		17
#define RATE_MCS_STBC_MSK		(1 << RATE_MCS_STBC_POS)

/* Bit 19: (0) Beamforming is off, (1) Beamforming is on */
#define RATE_MCS_BF_POS			19
#define RATE_MCS_BF_MSK			(1 << RATE_MCS_BF_POS)

/* Bit 20: (0) ZLF is off, (1) ZLF is on */
#define RATE_MCS_ZLF_POS		20
#define RATE_MCS_ZLF_MSK		(1 << RATE_MCS_ZLF_POS)

/* Bit 24-25: (0) 20MHz (no dup), (1) 2x20MHz, (2) 4x20MHz, 3 8x20MHz */
#define RATE_MCS_DUP_POS		24
#define RATE_MCS_DUP_MSK		(3 << RATE_MCS_DUP_POS)

/* Bit 27: (1) LDPC enabled, (0) LDPC disabled */
#define RATE_MCS_LDPC_POS		27
#define RATE_MCS_LDPC_MSK		(1 << RATE_MCS_LDPC_POS)


/* Link Quality definitions */

/* # entries in rate scale table to support Tx retries */
#define  LQ_MAX_RETRY_NUM 16

/* Link quality command flags, only this one is available */
#define  LQ_FLAG_SET_STA_TLC_RTS_MSK	BIT(0)

/**
 * struct iwl_lq_cmd - link quality command
 * @sta_id: station to update
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
 * @rs_table: array of rates for each TX try, each is rate_n_flags,
 *	meaning it is a combination of RATE_MCS_* and IWL_RATE_*_PLCP
 * @bf_params: beam forming params, currently not used
 */
struct iwl_lq_cmd {
	u8 sta_id;
	u8 reserved1;
	u16 control;
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
	__le32 bf_params;
}; /* LINK_QUALITY_CMD_API_S_VER_1 */
#endif /* __fw_api_rs_h__ */
