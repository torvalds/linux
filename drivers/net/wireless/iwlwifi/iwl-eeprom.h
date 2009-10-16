/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2009 Intel Corporation. All rights reserved.
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
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2009 Intel Corporation. All rights reserved.
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

#ifndef __iwl_eeprom_h__
#define __iwl_eeprom_h__

struct iwl_priv;

/*
 * EEPROM access time values:
 *
 * Driver initiates EEPROM read by writing byte address << 1 to CSR_EEPROM_REG.
 * Driver then polls CSR_EEPROM_REG for CSR_EEPROM_REG_READ_VALID_MSK (0x1).
 * When polling, wait 10 uSec between polling loops, up to a maximum 5000 uSec.
 * Driver reads 16-bit value from bits 31-16 of CSR_EEPROM_REG.
 */
#define IWL_EEPROM_ACCESS_TIMEOUT	5000 /* uSec */

#define IWL_EEPROM_SEM_TIMEOUT 		10   /* microseconds */
#define IWL_EEPROM_SEM_RETRY_LIMIT	1000 /* number of attempts (not time) */


/*
 * Regulatory channel usage flags in EEPROM struct iwl4965_eeprom_channel.flags.
 *
 * IBSS and/or AP operation is allowed *only* on those channels with
 * (VALID && IBSS && ACTIVE && !RADAR).  This restriction is in place because
 * RADAR detection is not supported by the 4965 driver, but is a
 * requirement for establishing a new network for legal operation on channels
 * requiring RADAR detection or restricting ACTIVE scanning.
 *
 * NOTE:  "WIDE" flag does not indicate anything about "HT40" 40 MHz channels.
 *        It only indicates that 20 MHz channel use is supported; HT40 channel
 *        usage is indicated by a separate set of regulatory flags for each
 *        HT40 channel pair.
 *
 * NOTE:  Using a channel inappropriately will result in a uCode error!
 */
#define IWL_NUM_TX_CALIB_GROUPS 5
enum {
	EEPROM_CHANNEL_VALID = (1 << 0),	/* usable for this SKU/geo */
	EEPROM_CHANNEL_IBSS = (1 << 1),		/* usable as an IBSS channel */
	/* Bit 2 Reserved */
	EEPROM_CHANNEL_ACTIVE = (1 << 3),	/* active scanning allowed */
	EEPROM_CHANNEL_RADAR = (1 << 4),	/* radar detection required */
	EEPROM_CHANNEL_WIDE = (1 << 5),		/* 20 MHz channel okay */
	/* Bit 6 Reserved (was Narrow Channel) */
	EEPROM_CHANNEL_DFS = (1 << 7),	/* dynamic freq selection candidate */
};

/* SKU Capabilities */
#define EEPROM_SKU_CAP_SW_RF_KILL_ENABLE                (1 << 0)
#define EEPROM_SKU_CAP_HW_RF_KILL_ENABLE                (1 << 1)

/* *regulatory* channel data format in eeprom, one for each channel.
 * There are separate entries for HT40 (40 MHz) vs. normal (20 MHz) channels. */
struct iwl_eeprom_channel {
	u8 flags;		/* EEPROM_CHANNEL_* flags copied from EEPROM */
	s8 max_power_avg;	/* max power (dBm) on this chnl, limit 31 */
} __attribute__ ((packed));

/**
 * iwl_eeprom_enhanced_txpwr structure
 *    This structure presents the enhanced regulatory tx power limit layout
 *    in eeprom image
 *    Enhanced regulatory tx power portion of eeprom image can be broken down
 *    into individual structures; each one is 8 bytes in size and contain the
 *    following information
 * @chain_a_max_pwr: chain a max power in 1/2 dBm
 * @chain_b_max_pwr: chain b max power in 1/2 dBm
 * @chain_c_max_pwr: chain c max power in 1/2 dBm
 * @mimo2_max_pwr: mimo2 max power in 1/2 dBm
 * @mimo3_max_pwr: mimo3 max power in 1/2 dBm
 *
 */
struct iwl_eeprom_enhanced_txpwr {
	u16 reserved;
	s8 chain_a_max;
	s8 chain_b_max;
	s8 chain_c_max;
	s8 reserved1;
	s8 mimo2_max;
	s8 mimo3_max;
} __attribute__ ((packed));

/* 3945 Specific */
#define EEPROM_3945_EEPROM_VERSION	(0x2f)

/* 4965 has two radio transmitters (and 3 radio receivers) */
#define EEPROM_TX_POWER_TX_CHAINS      (2)

/* 4965 has room for up to 8 sets of txpower calibration data */
#define EEPROM_TX_POWER_BANDS          (8)

/* 4965 factory calibration measures txpower gain settings for
 * each of 3 target output levels */
#define EEPROM_TX_POWER_MEASUREMENTS   (3)

/* 4965 Specific */
/* 4965 driver does not work with txpower calibration version < 5 */
#define EEPROM_4965_TX_POWER_VERSION    (5)
#define EEPROM_4965_EEPROM_VERSION	(0x2f)
#define EEPROM_4965_CALIB_VERSION_OFFSET       (2*0xB6) /* 2 bytes */
#define EEPROM_4965_CALIB_TXPOWER_OFFSET       (2*0xE8) /* 48  bytes */
#define EEPROM_4965_BOARD_REVISION             (2*0x4F) /* 2 bytes */
#define EEPROM_4965_BOARD_PBA                  (2*0x56+1) /* 9 bytes */

/* 5000 Specific */
#define EEPROM_5000_TX_POWER_VERSION    (4)
#define EEPROM_5000_EEPROM_VERSION	(0x11A)

/*5000 calibrations */
#define EEPROM_5000_CALIB_ALL	(INDIRECT_ADDRESS | INDIRECT_CALIBRATION)
#define EEPROM_5000_XTAL	((2*0x128) | EEPROM_5000_CALIB_ALL)
#define EEPROM_5000_TEMPERATURE ((2*0x12A) | EEPROM_5000_CALIB_ALL)

/* 5000 links */
#define EEPROM_5000_LINK_HOST             (2*0x64)
#define EEPROM_5000_LINK_GENERAL          (2*0x65)
#define EEPROM_5000_LINK_REGULATORY       (2*0x66)
#define EEPROM_5000_LINK_CALIBRATION      (2*0x67)
#define EEPROM_5000_LINK_PROCESS_ADJST    (2*0x68)
#define EEPROM_5000_LINK_OTHERS           (2*0x69)

/* 5000 regulatory - indirect access */
#define EEPROM_5000_REG_SKU_ID ((0x02)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 4  bytes */
#define EEPROM_5000_REG_BAND_1_CHANNELS       ((0x08)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 28 bytes */
#define EEPROM_5000_REG_BAND_2_CHANNELS       ((0x26)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 26 bytes */
#define EEPROM_5000_REG_BAND_3_CHANNELS       ((0x42)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 24 bytes */
#define EEPROM_5000_REG_BAND_4_CHANNELS       ((0x5C)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 22 bytes */
#define EEPROM_5000_REG_BAND_5_CHANNELS       ((0x74)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 12 bytes */
#define EEPROM_5000_REG_BAND_24_HT40_CHANNELS  ((0x82)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 14  bytes */
#define EEPROM_5000_REG_BAND_52_HT40_CHANNELS  ((0x92)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 22  bytes */

/* 6000 and up regulatory tx power - indirect access */
/* max. elements per section */
#define EEPROM_MAX_TXPOWER_SECTION_ELEMENTS	(8)
#define EEPROM_TXPOWER_COMMON_HT40_INDEX	(2)

/**
 * Partition the enhanced tx power portion of eeprom image into
 * 10 sections based on band, modulation, frequency and channel
 *
 * Section 1: all CCK channels
 * Section 2: all 2.4 GHz OFDM (Legacy, HT and HT40 ) channels
 * Section 3: all 5.2 GHz OFDM (Legacy, HT and HT40) channels
 * Section 4: 2.4 GHz 20MHz channels: 1, 2, 10, 11. Both Legacy and HT
 * Section 5: 2.4 GHz 40MHz channels: 1, 2, 6, 7, 9, (_above_)
 * Section 6: 5.2 GHz 20MHz channels: 36, 64, 100, both Legacy and HT
 * Section 7: 5.2 GHz 40MHz channels: 36, 60, 100 (_above_)
 * Section 8: 2.4 GHz channel 13, Both Legacy and HT
 * Section 9: 2.4 GHz channel 140, Both Legacy and HT
 * Section 10: 2.4 GHz 40MHz channels: 132, 44 (_above_)
 */
/* 2.4 GHz band: CCK */
#define EEPROM_LB_CCK_20_COMMON       ((0xA8)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 8 bytes */
/* 2.4 GHz band: 20MHz-Legacy, 20MHz-HT, 40MHz-HT */
#define EEPROM_LB_OFDM_COMMON       ((0xB0)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 24 bytes */
/* 5.2 GHz band: 20MHz-Legacy, 20MHz-HT, 40MHz-HT */
#define EEPROM_HB_OFDM_COMMON       ((0xC8)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 24 bytes */
/* 2.4GHz band channels:
 *	1Legacy, 1HT, 2Legacy, 2HT, 10Legacy, 10HT, 11Legacy, 11HT */
#define EEPROM_LB_OFDM_20_BAND       ((0xE0)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 64 bytes */
/* 2.4 GHz band HT40 channels: (1,+1) (2,+1) (6,+1) (7,+1) (9,+1) */
#define EEPROM_LB_OFDM_HT40_BAND       ((0x120)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 40 bytes */
/* 5.2GHz band channels: 36Legacy, 36HT, 64Legacy, 64HT, 100Legacy, 100HT */
#define EEPROM_HB_OFDM_20_BAND       ((0x148)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 48 bytes */
/* 5.2 GHz band HT40 channels: (36,+1) (60,+1) (100,+1) */
#define EEPROM_HB_OFDM_HT40_BAND       ((0x178)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 24 bytes */
/* 2.4 GHz band, channnel 13: Legacy, HT */
#define EEPROM_LB_OFDM_20_CHANNEL_13       ((0x190)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 16 bytes */
/* 5.2 GHz band, channnel 140: Legacy, HT */
#define EEPROM_HB_OFDM_20_CHANNEL_140       ((0x1A0)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 16 bytes */
/* 5.2 GHz band, HT40 channnels (132,+1) (44,+1) */
#define EEPROM_HB_OFDM_HT40_BAND_1       ((0x1B0)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 16 bytes */


/* 5050 Specific */
#define EEPROM_5050_TX_POWER_VERSION    (4)
#define EEPROM_5050_EEPROM_VERSION	(0x21E)

/* OTP */
/* lower blocks contain EEPROM image and calibration data */
#define OTP_LOW_IMAGE_SIZE		(2 * 512 * sizeof(u16)) /* 2 KB */
/* high blocks contain PAPD data */
#define OTP_HIGH_IMAGE_SIZE_6x00        (6 * 512 * sizeof(u16)) /* 6 KB */
#define OTP_HIGH_IMAGE_SIZE_1000        (0x200 * sizeof(u16)) /* 1024 bytes */
#define OTP_MAX_LL_ITEMS_1000		(3)	/* OTP blocks for 1000 */
#define OTP_MAX_LL_ITEMS_6x00		(4)	/* OTP blocks for 6x00 */
#define OTP_MAX_LL_ITEMS_6x50		(7)	/* OTP blocks for 6x50 */

/* 2.4 GHz */
extern const u8 iwl_eeprom_band_1[14];

/*
 * factory calibration data for one txpower level, on one channel,
 * measured on one of the 2 tx chains (radio transmitter and associated
 * antenna).  EEPROM contains:
 *
 * 1)  Temperature (degrees Celsius) of device when measurement was made.
 *
 * 2)  Gain table index used to achieve the target measurement power.
 *     This refers to the "well-known" gain tables (see iwl-4965-hw.h).
 *
 * 3)  Actual measured output power, in half-dBm ("34" = 17 dBm).
 *
 * 4)  RF power amplifier detector level measurement (not used).
 */
struct iwl_eeprom_calib_measure {
	u8 temperature;		/* Device temperature (Celsius) */
	u8 gain_idx;		/* Index into gain table */
	u8 actual_pow;		/* Measured RF output power, half-dBm */
	s8 pa_det;		/* Power amp detector level (not used) */
} __attribute__ ((packed));


/*
 * measurement set for one channel.  EEPROM contains:
 *
 * 1)  Channel number measured
 *
 * 2)  Measurements for each of 3 power levels for each of 2 radio transmitters
 *     (a.k.a. "tx chains") (6 measurements altogether)
 */
struct iwl_eeprom_calib_ch_info {
	u8 ch_num;
	struct iwl_eeprom_calib_measure
		measurements[EEPROM_TX_POWER_TX_CHAINS]
			[EEPROM_TX_POWER_MEASUREMENTS];
} __attribute__ ((packed));

/*
 * txpower subband info.
 *
 * For each frequency subband, EEPROM contains the following:
 *
 * 1)  First and last channels within range of the subband.  "0" values
 *     indicate that this sample set is not being used.
 *
 * 2)  Sample measurement sets for 2 channels close to the range endpoints.
 */
struct iwl_eeprom_calib_subband_info {
	u8 ch_from;	/* channel number of lowest channel in subband */
	u8 ch_to;	/* channel number of highest channel in subband */
	struct iwl_eeprom_calib_ch_info ch1;
	struct iwl_eeprom_calib_ch_info ch2;
} __attribute__ ((packed));


/*
 * txpower calibration info.  EEPROM contains:
 *
 * 1)  Factory-measured saturation power levels (maximum levels at which
 *     tx power amplifier can output a signal without too much distortion).
 *     There is one level for 2.4 GHz band and one for 5 GHz band.  These
 *     values apply to all channels within each of the bands.
 *
 * 2)  Factory-measured power supply voltage level.  This is assumed to be
 *     constant (i.e. same value applies to all channels/bands) while the
 *     factory measurements are being made.
 *
 * 3)  Up to 8 sets of factory-measured txpower calibration values.
 *     These are for different frequency ranges, since txpower gain
 *     characteristics of the analog radio circuitry vary with frequency.
 *
 *     Not all sets need to be filled with data;
 *     struct iwl_eeprom_calib_subband_info contains range of channels
 *     (0 if unused) for each set of data.
 */
struct iwl_eeprom_calib_info {
	u8 saturation_power24;	/* half-dBm (e.g. "34" = 17 dBm) */
	u8 saturation_power52;	/* half-dBm */
	s16 voltage;		/* signed */
	struct iwl_eeprom_calib_subband_info
		band_info[EEPROM_TX_POWER_BANDS];
} __attribute__ ((packed));


#define ADDRESS_MSK                 0x0000FFFF
#define INDIRECT_TYPE_MSK           0x000F0000
#define INDIRECT_HOST               0x00010000
#define INDIRECT_GENERAL            0x00020000
#define INDIRECT_REGULATORY         0x00030000
#define INDIRECT_CALIBRATION        0x00040000
#define INDIRECT_PROCESS_ADJST      0x00050000
#define INDIRECT_OTHERS             0x00060000
#define INDIRECT_ADDRESS            0x00100000

/* General */
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
#define EEPROM_MAC_ADDRESS                  (2*0x15)	/* 6  bytes */
#define EEPROM_BOARD_REVISION               (2*0x35)	/* 2  bytes */
#define EEPROM_BOARD_PBA_NUMBER             (2*0x3B+1)	/* 9  bytes */
#define EEPROM_VERSION                      (2*0x44)	/* 2  bytes */
#define EEPROM_SKU_CAP                      (2*0x45)	/* 1  bytes */
#define EEPROM_LEDS_MODE                    (2*0x45+1)	/* 1  bytes */
#define EEPROM_OEM_MODE                     (2*0x46)	/* 2  bytes */
#define EEPROM_WOWLAN_MODE                  (2*0x47)	/* 2  bytes */
#define EEPROM_RADIO_CONFIG                 (2*0x48)	/* 2  bytes */
#define EEPROM_3945_M_VERSION               (2*0x4A)	/* 1  bytes */
#define EEPROM_ANTENNA_SWITCH_TYPE          (2*0x4A+1)	/* 1  bytes */

/* The following masks are to be applied on EEPROM_RADIO_CONFIG */
#define EEPROM_RF_CFG_TYPE_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define EEPROM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define EEPROM_RF_CFG_DASH_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define EEPROM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define EEPROM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define EEPROM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

#define EEPROM_3945_RF_CFG_TYPE_MAX  0x0
#define EEPROM_4965_RF_CFG_TYPE_MAX  0x1
#define EEPROM_5000_RF_CFG_TYPE_MAX  0x3

/*
 * Per-channel regulatory data.
 *
 * Each channel that *might* be supported by iwl has a fixed location
 * in EEPROM containing EEPROM_CHANNEL_* usage flags (LSB) and max regulatory
 * txpower (MSB).
 *
 * Entries immediately below are for 20 MHz channel width.  HT40 (40 MHz)
 * channels (only for 4965, not supported by 3945) appear later in the EEPROM.
 *
 * 2.4 GHz channels 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
 */
#define EEPROM_REGULATORY_SKU_ID            (2*0x60)    /* 4  bytes */
#define EEPROM_REGULATORY_BAND_1            (2*0x62)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_1_CHANNELS   (2*0x63)	/* 28 bytes */

/*
 * 4.9 GHz channels 183, 184, 185, 187, 188, 189, 192, 196,
 * 5.0 GHz channels 7, 8, 11, 12, 16
 * (4915-5080MHz) (none of these is ever supported)
 */
#define EEPROM_REGULATORY_BAND_2            (2*0x71)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_2_CHANNELS   (2*0x72)	/* 26 bytes */

/*
 * 5.2 GHz channels 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
 * (5170-5320MHz)
 */
#define EEPROM_REGULATORY_BAND_3            (2*0x7F)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_3_CHANNELS   (2*0x80)	/* 24 bytes */

/*
 * 5.5 GHz channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
 * (5500-5700MHz)
 */
#define EEPROM_REGULATORY_BAND_4            (2*0x8C)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_4_CHANNELS   (2*0x8D)	/* 22 bytes */

/*
 * 5.7 GHz channels 145, 149, 153, 157, 161, 165
 * (5725-5825MHz)
 */
#define EEPROM_REGULATORY_BAND_5            (2*0x98)	/* 2  bytes */
#define EEPROM_REGULATORY_BAND_5_CHANNELS   (2*0x99)	/* 12 bytes */

/*
 * 2.4 GHz HT40 channels 1 (5), 2 (6), 3 (7), 4 (8), 5 (9), 6 (10), 7 (11)
 *
 * The channel listed is the center of the lower 20 MHz half of the channel.
 * The overall center frequency is actually 2 channels (10 MHz) above that,
 * and the upper half of each HT40 channel is centered 4 channels (20 MHz) away
 * from the lower half; e.g. the upper half of HT40 channel 1 is channel 5,
 * and the overall HT40 channel width centers on channel 3.
 *
 * NOTE:  The RXON command uses 20 MHz channel numbers to specify the
 *        control channel to which to tune.  RXON also specifies whether the
 *        control channel is the upper or lower half of a HT40 channel.
 *
 * NOTE:  4965 does not support HT40 channels on 2.4 GHz.
 */
#define EEPROM_4965_REGULATORY_BAND_24_HT40_CHANNELS (2*0xA0)	/* 14 bytes */

/*
 * 5.2 GHz HT40 channels 36 (40), 44 (48), 52 (56), 60 (64),
 * 100 (104), 108 (112), 116 (120), 124 (128), 132 (136), 149 (153), 157 (161)
 */
#define EEPROM_4965_REGULATORY_BAND_52_HT40_CHANNELS (2*0xA8)	/* 22 bytes */

#define EEPROM_REGULATORY_BAND_NO_HT40			(0)

struct iwl_eeprom_ops {
	const u32 regulatory_bands[7];
	int (*verify_signature) (struct iwl_priv *priv);
	int (*acquire_semaphore) (struct iwl_priv *priv);
	void (*release_semaphore) (struct iwl_priv *priv);
	u16 (*calib_version) (struct iwl_priv *priv);
	const u8* (*query_addr) (const struct iwl_priv *priv, size_t offset);
	void (*update_enhanced_txpower) (struct iwl_priv *priv);
};


void iwl_eeprom_get_mac(const struct iwl_priv *priv, u8 *mac);
int iwl_eeprom_init(struct iwl_priv *priv);
void iwl_eeprom_free(struct iwl_priv *priv);
int  iwl_eeprom_check_version(struct iwl_priv *priv);
const u8 *iwl_eeprom_query_addr(const struct iwl_priv *priv, size_t offset);
u16 iwl_eeprom_query16(const struct iwl_priv *priv, size_t offset);

int iwlcore_eeprom_verify_signature(struct iwl_priv *priv);
int iwlcore_eeprom_acquire_semaphore(struct iwl_priv *priv);
void iwlcore_eeprom_release_semaphore(struct iwl_priv *priv);
const u8 *iwlcore_eeprom_query_addr(const struct iwl_priv *priv, size_t offset);
void iwlcore_eeprom_enhanced_txpower(struct iwl_priv *priv);
int iwl_init_channel_map(struct iwl_priv *priv);
void iwl_free_channel_map(struct iwl_priv *priv);
const struct iwl_channel_info *iwl_get_channel_info(
		const struct iwl_priv *priv,
		enum ieee80211_band band, u16 channel);

#endif  /* __iwl_eeprom_h__ */
