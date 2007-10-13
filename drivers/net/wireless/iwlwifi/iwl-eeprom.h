/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU Geeral Public License as
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2007 Intel Corporation. All rights reserved.
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

#ifndef __iwl_eeprom_h__
#define __iwl_eeprom_h__

/*
 * This file defines EEPROM related constants, enums, and inline functions.
 *
 */

#define IWL_EEPROM_ACCESS_TIMEOUT	5000 /* uSec */
#define IWL_EEPROM_ACCESS_DELAY		10   /* uSec */
/* EEPROM field values */
#define ANTENNA_SWITCH_NORMAL     0
#define ANTENNA_SWITCH_INVERSE    1

enum {
	EEPROM_CHANNEL_VALID = (1 << 0),	/* usable for this SKU/geo */
	EEPROM_CHANNEL_IBSS = (1 << 1),	/* usable as an IBSS channel */
	/* Bit 2 Reserved */
	EEPROM_CHANNEL_ACTIVE = (1 << 3),	/* active scanning allowed */
	EEPROM_CHANNEL_RADAR = (1 << 4),	/* radar detection required */
	EEPROM_CHANNEL_WIDE = (1 << 5),
	EEPROM_CHANNEL_NARROW = (1 << 6),
	EEPROM_CHANNEL_DFS = (1 << 7),	/* dynamic freq selection candidate */
};

/* EEPROM field lengths */
#define EEPROM_BOARD_PBA_NUMBER_LENGTH                  11

/* EEPROM field lengths */
#define EEPROM_BOARD_PBA_NUMBER_LENGTH                  11
#define EEPROM_REGULATORY_SKU_ID_LENGTH                 4
#define EEPROM_REGULATORY_BAND1_CHANNELS_LENGTH         14
#define EEPROM_REGULATORY_BAND2_CHANNELS_LENGTH         13
#define EEPROM_REGULATORY_BAND3_CHANNELS_LENGTH         12
#define EEPROM_REGULATORY_BAND4_CHANNELS_LENGTH         11
#define EEPROM_REGULATORY_BAND5_CHANNELS_LENGTH         6

#if IWL == 3945
#define EEPROM_REGULATORY_CHANNELS_LENGTH ( \
	EEPROM_REGULATORY_BAND1_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND2_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND3_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND4_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND5_CHANNELS_LENGTH)
#elif IWL == 4965
#define EEPROM_REGULATORY_BAND_24_FAT_CHANNELS_LENGTH 7
#define EEPROM_REGULATORY_BAND_52_FAT_CHANNELS_LENGTH 11
#define EEPROM_REGULATORY_CHANNELS_LENGTH ( \
	EEPROM_REGULATORY_BAND1_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND2_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND3_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND4_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND5_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND_24_FAT_CHANNELS_LENGTH + \
	EEPROM_REGULATORY_BAND_52_FAT_CHANNELS_LENGTH)
#endif

#define EEPROM_REGULATORY_NUMBER_OF_BANDS               5

/* SKU Capabilities */
#define EEPROM_SKU_CAP_SW_RF_KILL_ENABLE                (1 << 0)
#define EEPROM_SKU_CAP_HW_RF_KILL_ENABLE                (1 << 1)
#define EEPROM_SKU_CAP_OP_MODE_MRC                      (1 << 7)

/* *regulatory* channel data from eeprom, one for each channel */
struct iwl_eeprom_channel {
	u8 flags;		/* flags copied from EEPROM */
	s8 max_power_avg;	/* max power (dBm) on this chnl, limit 31 */
} __attribute__ ((packed));

/*
 * Mapping of a Tx power level, at factory calibration temperature,
 *   to a radio/DSP gain table index.
 * One for each of 5 "sample" power levels in each band.
 * v_det is measured at the factory, using the 3945's built-in power amplifier
 *   (PA) output voltage detector.  This same detector is used during Tx of
 *   long packets in normal operation to provide feedback as to proper output
 *   level.
 * Data copied from EEPROM.
 */
struct iwl_eeprom_txpower_sample {
	u8 gain_index;		/* index into power (gain) setup table ... */
	s8 power;		/* ... for this pwr level for this chnl group */
	u16 v_det;		/* PA output voltage */
} __attribute__ ((packed));

/*
 * Mappings of Tx power levels -> nominal radio/DSP gain table indexes.
 * One for each channel group (a.k.a. "band") (1 for BG, 4 for A).
 * Tx power setup code interpolates between the 5 "sample" power levels
 *    to determine the nominal setup for a requested power level.
 * Data copied from EEPROM.
 * DO NOT ALTER THIS STRUCTURE!!!
 */
struct iwl_eeprom_txpower_group {
	struct iwl_eeprom_txpower_sample samples[5];	/* 5 power levels */
	s32 a, b, c, d, e;	/* coefficients for voltage->power
				 * formula (signed) */
	s32 Fa, Fb, Fc, Fd, Fe;	/* these modify coeffs based on
					 * frequency (signed) */
	s8 saturation_power;	/* highest power possible by h/w in this
				 * band */
	u8 group_channel;	/* "representative" channel # in this band */
	s16 temperature;	/* h/w temperature at factory calib this band
				 * (signed) */
} __attribute__ ((packed));

/*
 * Temperature-based Tx-power compensation data, not band-specific.
 * These coefficients are use to modify a/b/c/d/e coeffs based on
 *   difference between current temperature and factory calib temperature.
 * Data copied from EEPROM.
 */
struct iwl_eeprom_temperature_corr {
	u32 Ta;
	u32 Tb;
	u32 Tc;
	u32 Td;
	u32 Te;
} __attribute__ ((packed));

#if IWL == 4965
#define EEPROM_TX_POWER_TX_CHAINS      (2)
#define EEPROM_TX_POWER_BANDS          (8)
#define EEPROM_TX_POWER_MEASUREMENTS   (3)
#define EEPROM_TX_POWER_VERSION        (2)
#define EEPROM_TX_POWER_VERSION_NEW    (5)

struct iwl_eeprom_calib_measure {
	u8 temperature;
	u8 gain_idx;
	u8 actual_pow;
	s8 pa_det;
} __attribute__ ((packed));

struct iwl_eeprom_calib_ch_info {
	u8 ch_num;
	struct iwl_eeprom_calib_measure measurements[EEPROM_TX_POWER_TX_CHAINS]
		[EEPROM_TX_POWER_MEASUREMENTS];
} __attribute__ ((packed));

struct iwl_eeprom_calib_subband_info {
	u8 ch_from;
	u8 ch_to;
	struct iwl_eeprom_calib_ch_info ch1;
	struct iwl_eeprom_calib_ch_info ch2;
} __attribute__ ((packed));

struct iwl_eeprom_calib_info {
	u8 saturation_power24;
	u8 saturation_power52;
	s16 voltage;		/* signed */
	struct iwl_eeprom_calib_subband_info band_info[EEPROM_TX_POWER_BANDS];
} __attribute__ ((packed));

#endif

struct iwl_eeprom {
	u8 reserved0[16];
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
	u16 device_id;	/* abs.ofs: 16 */
	u8 reserved1[2];
#define EEPROM_PMC                          (2*0x0A)	/* 2 bytes */
	u16 pmc;		/* abs.ofs: 20 */
	u8 reserved2[20];
#define EEPROM_MAC_ADDRESS                  (2*0x15)	/* 6  bytes */
	u8 mac_address[6];	/* abs.ofs: 42 */
	u8 reserved3[58];
#define EEPROM_BOARD_REVISION               (2*0x35)	/* 2  bytes */
	u16 board_revision;	/* abs.ofs: 106 */
	u8 reserved4[11];
#define EEPROM_BOARD_PBA_NUMBER             (2*0x3B+1)	/* 9  bytes */
	u8 board_pba_number[9];	/* abs.ofs: 119 */
	u8 reserved5[8];
#define EEPROM_VERSION                      (2*0x44)	/* 2  bytes */
	u16 version;		/* abs.ofs: 136 */
#define EEPROM_SKU_CAP                      (2*0x45)	/* 1  bytes */
	u8 sku_cap;		/* abs.ofs: 138 */
#define EEPROM_LEDS_MODE                    (2*0x45+1)	/* 1  bytes */
	u8 leds_mode;		/* abs.ofs: 139 */
#define EEPROM_OEM_MODE                     (2*0x46)	/* 2  bytes */
	u16 oem_mode;
#define EEPROM_WOWLAN_MODE                  (2*0x47)	/* 2  bytes */
	u16 wowlan_mode;	/* abs.ofs: 142 */
#define EEPROM_LEDS_TIME_INTERVAL           (2*0x48)	/* 2  bytes */
	u16 leds_time_interval;	/* abs.ofs: 144 */
#define EEPROM_LEDS_OFF_TIME                (2*0x49)	/* 1  bytes */
	u8 leds_off_time;	/* abs.ofs: 146 */
#define EEPROM_LEDS_ON_TIME                 (2*0x49+1)	/* 1  bytes */
	u8 leds_on_time;	/* abs.ofs: 147 */
#define EEPROM_ALMGOR_M_VERSION             (2*0x4A)	/* 1  bytes */
	u8 almgor_m_version;	/* abs.ofs: 148 */
#define EEPROM_ANTENNA_SWITCH_TYPE          (2*0x4A+1)	/* 1  bytes */
	u8 antenna_switch_type;	/* abs.ofs: 149 */
#if IWL == 3945
	u8 reserved6[42];
#else
	u8 reserved6[8];
#define EEPROM_4965_BOARD_REVISION          (2*0x4F)	/* 2 bytes */
	u16 board_revision_4965;	/* abs.ofs: 158 */
	u8 reserved7[13];
#define EEPROM_4965_BOARD_PBA               (2*0x56+1)	/* 9 bytes */
	u8 board_pba_number_4965[9];	/* abs.ofs: 173 */
	u8 reserved8[10];
#endif
#define EEPROM_REGULATORY_SKU_ID            (2*0x60)	/* 4  bytes */
	u8 sku_id[4];		/* abs.ofs: 192 */
#define EEPROM_REGULATORY_BAND_1            (2*0x62)	/* 2  bytes */
	u16 band_1_count;	/* abs.ofs: 196 */
#define EEPROM_REGULATORY_BAND_1_CHANNELS   (2*0x63)	/* 28 bytes */
	struct iwl_eeprom_channel band_1_channels[14];	/* abs.ofs: 196 */
#define EEPROM_REGULATORY_BAND_2            (2*0x71)	/* 2  bytes */
	u16 band_2_count;	/* abs.ofs: 226 */
#define EEPROM_REGULATORY_BAND_2_CHANNELS   (2*0x72)	/* 26 bytes */
	struct iwl_eeprom_channel band_2_channels[13];	/* abs.ofs: 228 */
#define EEPROM_REGULATORY_BAND_3            (2*0x7F)	/* 2  bytes */
	u16 band_3_count;	/* abs.ofs: 254 */
#define EEPROM_REGULATORY_BAND_3_CHANNELS   (2*0x80)	/* 24 bytes */
	struct iwl_eeprom_channel band_3_channels[12];	/* abs.ofs: 256 */
#define EEPROM_REGULATORY_BAND_4            (2*0x8C)	/* 2  bytes */
	u16 band_4_count;	/* abs.ofs: 280 */
#define EEPROM_REGULATORY_BAND_4_CHANNELS   (2*0x8D)	/* 22 bytes */
	struct iwl_eeprom_channel band_4_channels[11];	/* abs.ofs: 282 */
#define EEPROM_REGULATORY_BAND_5            (2*0x98)	/* 2  bytes */
	u16 band_5_count;	/* abs.ofs: 304 */
#define EEPROM_REGULATORY_BAND_5_CHANNELS   (2*0x99)	/* 12 bytes */
	struct iwl_eeprom_channel band_5_channels[6];	/* abs.ofs: 306 */

/* From here on out the EEPROM diverges between the 4965 and the 3945 */
#if IWL == 3945

	u8 reserved9[194];

#define EEPROM_TXPOWER_CALIB_GROUP0 0x200
#define EEPROM_TXPOWER_CALIB_GROUP1 0x240
#define EEPROM_TXPOWER_CALIB_GROUP2 0x280
#define EEPROM_TXPOWER_CALIB_GROUP3 0x2c0
#define EEPROM_TXPOWER_CALIB_GROUP4 0x300
#define IWL_NUM_TX_CALIB_GROUPS 5
	struct iwl_eeprom_txpower_group groups[IWL_NUM_TX_CALIB_GROUPS];
/* abs.ofs: 512 */
#define EEPROM_CALIB_TEMPERATURE_CORRECT 0x340
	struct iwl_eeprom_temperature_corr corrections;	/* abs.ofs: 832 */
	u8 reserved16[172];	/* fill out to full 1024 byte block */

/* 4965AGN adds fat channel support */
#elif IWL == 4965

	u8 reserved10[2];
#define EEPROM_REGULATORY_BAND_24_FAT_CHANNELS (2*0xA0)	/* 14 bytes */
	struct iwl_eeprom_channel band_24_channels[7];	/* abs.ofs: 320 */
	u8 reserved11[2];
#define EEPROM_REGULATORY_BAND_52_FAT_CHANNELS (2*0xA8)	/* 22 bytes */
	struct iwl_eeprom_channel band_52_channels[11];	/* abs.ofs: 336 */
	u8 reserved12[6];
#define EEPROM_CALIB_VERSION_OFFSET            (2*0xB6)	/* 2 bytes */
	u16 calib_version;	/* abs.ofs: 364 */
	u8 reserved13[2];
#define EEPROM_SATURATION_POWER_OFFSET         (2*0xB8)	/* 2 bytes */
	u16 satruation_power;	/* abs.ofs: 368 */
	u8 reserved14[94];
#define EEPROM_IWL_CALIB_TXPOWER_OFFSET        (2*0xE8)	/* 48  bytes */
	struct iwl_eeprom_calib_info calib_info;	/* abs.ofs: 464 */

	u8 reserved16[140];	/* fill out to full 1024 byte block */

#endif

} __attribute__ ((packed));

#define IWL_EEPROM_IMAGE_SIZE 1024

#endif
