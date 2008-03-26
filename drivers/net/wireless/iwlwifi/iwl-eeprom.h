/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 Intel Corporation. All rights reserved.
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
 * Tomas Winkler <tomas.winkler@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2008 Intel Corporation. All rights reserved.
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
 * Driver initiates EEPROM read by writing byte address << 1 to CSR_EEPROM_REG,
 *   then clearing (with subsequent read/modify/write) CSR_EEPROM_REG bit
 *   CSR_EEPROM_REG_BIT_CMD (0x2).
 * Driver then polls CSR_EEPROM_REG for CSR_EEPROM_REG_READ_VALID_MSK (0x1).
 * When polling, wait 10 uSec between polling loops, up to a maximum 5000 uSec.
 * Driver reads 16-bit value from bits 31-16 of CSR_EEPROM_REG.
 */
#define IWL_EEPROM_ACCESS_TIMEOUT	5000 /* uSec */
#define IWL_EEPROM_ACCESS_DELAY		10   /* uSec */

#define IWL_EEPROM_SEM_TIMEOUT 		10   /* milliseconds */
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
 * NOTE:  "WIDE" flag does not indicate anything about "FAT" 40 MHz channels.
 *        It only indicates that 20 MHz channel use is supported; FAT channel
 *        usage is indicated by a separate set of regulatory flags for each
 *        FAT channel pair.
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
	EEPROM_CHANNEL_NARROW = (1 << 6),	/* 10 MHz channel (not used) */
	EEPROM_CHANNEL_DFS = (1 << 7),	/* dynamic freq selection candidate */
};

/* SKU Capabilities */
#define EEPROM_SKU_CAP_SW_RF_KILL_ENABLE                (1 << 0)
#define EEPROM_SKU_CAP_HW_RF_KILL_ENABLE                (1 << 1)

/* *regulatory* channel data format in eeprom, one for each channel.
 * There are separate entries for FAT (40 MHz) vs. normal (20 MHz) channels. */
struct iwl4965_eeprom_channel {
	u8 flags;		/* EEPROM_CHANNEL_* flags copied from EEPROM */
	s8 max_power_avg;	/* max power (dBm) on this chnl, limit 31 */
} __attribute__ ((packed));

/* 4965 has two radio transmitters (and 3 radio receivers) */
#define EEPROM_TX_POWER_TX_CHAINS      (2)

/* 4965 has room for up to 8 sets of txpower calibration data */
#define EEPROM_TX_POWER_BANDS          (8)

/* 4965 factory calibration measures txpower gain settings for
 * each of 3 target output levels */
#define EEPROM_TX_POWER_MEASUREMENTS   (3)

#define EEPROM_4965_TX_POWER_VERSION        (2)

/* 4965 driver does not work with txpower calibration version < 5.
 * Look for this in calib_version member of struct iwl4965_eeprom. */
#define EEPROM_TX_POWER_VERSION_NEW    (5)

/* 2.4 GHz */
extern const u8 iwl_eeprom_band_1[14];

/*
 * 4965 factory calibration data for one txpower level, on one channel,
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
struct iwl4965_eeprom_calib_measure {
	u8 temperature;		/* Device temperature (Celsius) */
	u8 gain_idx;		/* Index into gain table */
	u8 actual_pow;		/* Measured RF output power, half-dBm */
	s8 pa_det;		/* Power amp detector level (not used) */
} __attribute__ ((packed));


/*
 * 4965 measurement set for one channel.  EEPROM contains:
 *
 * 1)  Channel number measured
 *
 * 2)  Measurements for each of 3 power levels for each of 2 radio transmitters
 *     (a.k.a. "tx chains") (6 measurements altogether)
 */
struct iwl4965_eeprom_calib_ch_info {
	u8 ch_num;
	struct iwl4965_eeprom_calib_measure
		measurements[EEPROM_TX_POWER_TX_CHAINS]
			[EEPROM_TX_POWER_MEASUREMENTS];
} __attribute__ ((packed));

/*
 * 4965 txpower subband info.
 *
 * For each frequency subband, EEPROM contains the following:
 *
 * 1)  First and last channels within range of the subband.  "0" values
 *     indicate that this sample set is not being used.
 *
 * 2)  Sample measurement sets for 2 channels close to the range endpoints.
 */
struct iwl4965_eeprom_calib_subband_info {
	u8 ch_from;	/* channel number of lowest channel in subband */
	u8 ch_to;	/* channel number of highest channel in subband */
	struct iwl4965_eeprom_calib_ch_info ch1;
	struct iwl4965_eeprom_calib_ch_info ch2;
} __attribute__ ((packed));


/*
 * 4965 txpower calibration info.  EEPROM contains:
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
 *     struct iwl4965_eeprom_calib_subband_info contains range of channels
 *     (0 if unused) for each set of data.
 */
struct iwl4965_eeprom_calib_info {
	u8 saturation_power24;	/* half-dBm (e.g. "34" = 17 dBm) */
	u8 saturation_power52;	/* half-dBm */
	s16 voltage;		/* signed */
	struct iwl4965_eeprom_calib_subband_info
		band_info[EEPROM_TX_POWER_BANDS];
} __attribute__ ((packed));



/*
 * 4965 EEPROM map
 */
struct iwl4965_eeprom {
	u8 reserved0[16];
	u16 device_id;		/* abs.ofs: 16 */
	u8 reserved1[2];
	u16 pmc;		/* abs.ofs: 20 */
	u8 reserved2[20];
	u8 mac_address[6];	/* abs.ofs: 42 */
	u8 reserved3[58];
	u16 board_revision;	/* abs.ofs: 106 */
	u8 reserved4[11];
	u8 board_pba_number[9];	/* abs.ofs: 119 */
	u8 reserved5[8];
	u16 version;		/* abs.ofs: 136 */
	u8 sku_cap;		/* abs.ofs: 138 */
	u8 leds_mode;		/* abs.ofs: 139 */
	u16 oem_mode;
	u16 wowlan_mode;	/* abs.ofs: 142 */
	u16 leds_time_interval;	/* abs.ofs: 144 */
	u8 leds_off_time;	/* abs.ofs: 146 */
	u8 leds_on_time;	/* abs.ofs: 147 */
	u8 almgor_m_version;	/* abs.ofs: 148 */
	u8 antenna_switch_type;	/* abs.ofs: 149 */
	u8 reserved6[8];
	u16 board_revision_4965;	/* abs.ofs: 158 */
	u8 reserved7[13];
	u8 board_pba_number_4965[9];	/* abs.ofs: 173 */
	u8 reserved8[10];
	u8 sku_id[4];		/* abs.ofs: 192 */

/*
 * Per-channel regulatory data.
 *
 * Each channel that *might* be supported by 3945 or 4965 has a fixed location
 * in EEPROM containing EEPROM_CHANNEL_* usage flags (LSB) and max regulatory
 * txpower (MSB).
 *
 * Entries immediately below are for 20 MHz channel width.  FAT (40 MHz)
 * channels (only for 4965, not supported by 3945) appear later in the EEPROM.
 *
 * 2.4 GHz channels 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
 */
	u16 band_1_count;	/* abs.ofs: 196 */
	struct iwl4965_eeprom_channel band_1_channels[14]; /* abs.ofs: 196 */

/*
 * 4.9 GHz channels 183, 184, 185, 187, 188, 189, 192, 196,
 * 5.0 GHz channels 7, 8, 11, 12, 16
 * (4915-5080MHz) (none of these is ever supported)
 */
	u16 band_2_count;	/* abs.ofs: 226 */
	struct iwl4965_eeprom_channel band_2_channels[13]; /* abs.ofs: 228 */

/*
 * 5.2 GHz channels 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
 * (5170-5320MHz)
 */
	u16 band_3_count;	/* abs.ofs: 254 */
	struct iwl4965_eeprom_channel band_3_channels[12]; /* abs.ofs: 256 */

/*
 * 5.5 GHz channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
 * (5500-5700MHz)
 */
	u16 band_4_count;	/* abs.ofs: 280 */
	struct iwl4965_eeprom_channel band_4_channels[11]; /* abs.ofs: 282 */

/*
 * 5.7 GHz channels 145, 149, 153, 157, 161, 165
 * (5725-5825MHz)
 */
	u16 band_5_count;	/* abs.ofs: 304 */
	struct iwl4965_eeprom_channel band_5_channels[6]; /* abs.ofs: 306 */

	u8 reserved10[2];


/*
 * 2.4 GHz FAT channels 1 (5), 2 (6), 3 (7), 4 (8), 5 (9), 6 (10), 7 (11)
 *
 * The channel listed is the center of the lower 20 MHz half of the channel.
 * The overall center frequency is actually 2 channels (10 MHz) above that,
 * and the upper half of each FAT channel is centered 4 channels (20 MHz) away
 * from the lower half; e.g. the upper half of FAT channel 1 is channel 5,
 * and the overall FAT channel width centers on channel 3.
 *
 * NOTE:  The RXON command uses 20 MHz channel numbers to specify the
 *        control channel to which to tune.  RXON also specifies whether the
 *        control channel is the upper or lower half of a FAT channel.
 *
 * NOTE:  4965 does not support FAT channels on 2.4 GHz.
 */
	struct iwl4965_eeprom_channel band_24_channels[7]; /* abs.ofs: 320 */
	u8 reserved11[2];

/*
 * 5.2 GHz FAT channels 36 (40), 44 (48), 52 (56), 60 (64),
 * 100 (104), 108 (112), 116 (120), 124 (128), 132 (136), 149 (153), 157 (161)
 */
	struct iwl4965_eeprom_channel band_52_channels[11]; /* abs.ofs: 336 */
	u8 reserved12[6];

/*
 * 4965 driver requires txpower calibration format version 5 or greater.
 * Driver does not work with txpower calibration version < 5.
 * This value is simply a 16-bit number, no major/minor versions here.
 */
	u16 calib_version;	/* abs.ofs: 364 */
	u8 reserved13[2];
	u8 reserved14[96];	/* abs.ofs: 368 */

/*
 * 4965 Txpower calibration data.
 */
	struct iwl4965_eeprom_calib_info calib_info;	/* abs.ofs: 464 */

	u8 reserved16[140];	/* fill out to full 1024 byte block */


} __attribute__ ((packed));

#define IWL_EEPROM_IMAGE_SIZE 1024

/* End of EEPROM */

struct iwl_eeprom_ops {
	int (*verify_signature) (struct iwl_priv *priv);
	int (*acquire_semaphore) (struct iwl_priv *priv);
	void (*release_semaphore) (struct iwl_priv *priv);
};


void iwl_eeprom_get_mac(const struct iwl_priv *priv, u8 *mac);
int iwl_eeprom_init(struct iwl_priv *priv);

int iwlcore_eeprom_verify_signature(struct iwl_priv *priv);
int iwlcore_eeprom_acquire_semaphore(struct iwl_priv *priv);
void iwlcore_eeprom_release_semaphore(struct iwl_priv *priv);

int iwl_init_channel_map(struct iwl_priv *priv);
void iwl_free_channel_map(struct iwl_priv *priv);
const struct iwl_channel_info *iwl_get_channel_info(
		const struct iwl_priv *priv,
		enum ieee80211_band band, u16 channel);

#endif  /* __iwl_eeprom_h__ */
