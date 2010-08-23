/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2010 Intel Corporation. All rights reserved.
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
/*
 * Please use this file (iwl-3945-hw.h) only for hardware-related definitions.
 * Please use iwl-commands.h for uCode API definitions.
 * Please use iwl-3945.h for driver implementation definitions.
 */

#ifndef __iwl_3945_hw__
#define __iwl_3945_hw__

#include "iwl-eeprom.h"

/* RSSI to dBm */
#define IWL39_RSSI_OFFSET	95

#define IWL_DEFAULT_TX_POWER	0x0F

/*
 * EEPROM related constants, enums, and structures.
 */
#define EEPROM_SKU_CAP_OP_MODE_MRC                      (1 << 7)

/*
 * Mapping of a Tx power level, at factory calibration temperature,
 *   to a radio/DSP gain table index.
 * One for each of 5 "sample" power levels in each band.
 * v_det is measured at the factory, using the 3945's built-in power amplifier
 *   (PA) output voltage detector.  This same detector is used during Tx of
 *   long packets in normal operation to provide feedback as to proper output
 *   level.
 * Data copied from EEPROM.
 * DO NOT ALTER THIS STRUCTURE!!!
 */
struct iwl3945_eeprom_txpower_sample {
	u8 gain_index;		/* index into power (gain) setup table ... */
	s8 power;		/* ... for this pwr level for this chnl group */
	u16 v_det;		/* PA output voltage */
} __packed;

/*
 * Mappings of Tx power levels -> nominal radio/DSP gain table indexes.
 * One for each channel group (a.k.a. "band") (1 for BG, 4 for A).
 * Tx power setup code interpolates between the 5 "sample" power levels
 *    to determine the nominal setup for a requested power level.
 * Data copied from EEPROM.
 * DO NOT ALTER THIS STRUCTURE!!!
 */
struct iwl3945_eeprom_txpower_group {
	struct iwl3945_eeprom_txpower_sample samples[5];  /* 5 power levels */
	s32 a, b, c, d, e;	/* coefficients for voltage->power
				 * formula (signed) */
	s32 Fa, Fb, Fc, Fd, Fe;	/* these modify coeffs based on
				 * frequency (signed) */
	s8 saturation_power;	/* highest power possible by h/w in this
				 * band */
	u8 group_channel;	/* "representative" channel # in this band */
	s16 temperature;	/* h/w temperature at factory calib this band
				 * (signed) */
} __packed;

/*
 * Temperature-based Tx-power compensation data, not band-specific.
 * These coefficients are use to modify a/b/c/d/e coeffs based on
 *   difference between current temperature and factory calib temperature.
 * Data copied from EEPROM.
 */
struct iwl3945_eeprom_temperature_corr {
	u32 Ta;
	u32 Tb;
	u32 Tc;
	u32 Td;
	u32 Te;
} __packed;

/*
 * EEPROM map
 */
struct iwl3945_eeprom {
	u8 reserved0[16];
	u16 device_id;	/* abs.ofs: 16 */
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
	u8 reserved6[42];
	u8 sku_id[4];		/* abs.ofs: 192 */

/*
 * Per-channel regulatory data.
 *
 * Each channel that *might* be supported by 3945 or 4965 has a fixed location
 * in EEPROM containing EEPROM_CHANNEL_* usage flags (LSB) and max regulatory
 * txpower (MSB).
 *
 * Entries immediately below are for 20 MHz channel width.  HT40 (40 MHz)
 * channels (only for 4965, not supported by 3945) appear later in the EEPROM.
 *
 * 2.4 GHz channels 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
 */
	u16 band_1_count;	/* abs.ofs: 196 */
	struct iwl_eeprom_channel band_1_channels[14];  /* abs.ofs: 198 */

/*
 * 4.9 GHz channels 183, 184, 185, 187, 188, 189, 192, 196,
 * 5.0 GHz channels 7, 8, 11, 12, 16
 * (4915-5080MHz) (none of these is ever supported)
 */
	u16 band_2_count;	/* abs.ofs: 226 */
	struct iwl_eeprom_channel band_2_channels[13];  /* abs.ofs: 228 */

/*
 * 5.2 GHz channels 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
 * (5170-5320MHz)
 */
	u16 band_3_count;	/* abs.ofs: 254 */
	struct iwl_eeprom_channel band_3_channels[12];  /* abs.ofs: 256 */

/*
 * 5.5 GHz channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
 * (5500-5700MHz)
 */
	u16 band_4_count;	/* abs.ofs: 280 */
	struct iwl_eeprom_channel band_4_channels[11];  /* abs.ofs: 282 */

/*
 * 5.7 GHz channels 145, 149, 153, 157, 161, 165
 * (5725-5825MHz)
 */
	u16 band_5_count;	/* abs.ofs: 304 */
	struct iwl_eeprom_channel band_5_channels[6];  /* abs.ofs: 306 */

	u8 reserved9[194];

/*
 * 3945 Txpower calibration data.
 */
#define IWL_NUM_TX_CALIB_GROUPS 5
	struct iwl3945_eeprom_txpower_group groups[IWL_NUM_TX_CALIB_GROUPS];
/* abs.ofs: 512 */
	struct iwl3945_eeprom_temperature_corr corrections;  /* abs.ofs: 832 */
	u8 reserved16[172];	/* fill out to full 1024 byte block */
} __packed;

#define IWL3945_EEPROM_IMG_SIZE 1024

/* End of EEPROM */

#define PCI_CFG_REV_ID_BIT_BASIC_SKU                (0x40)	/* bit 6    */
#define PCI_CFG_REV_ID_BIT_RTP                      (0x80)	/* bit 7    */

/* 4 DATA + 1 CMD. There are 2 HCCA queues that are not used. */
#define IWL39_NUM_QUEUES        5
#define IWL39_CMD_QUEUE_NUM	4

#define IWL_DEFAULT_TX_RETRY  15

/*********************************************/

#define RFD_SIZE                              4
#define NUM_TFD_CHUNKS                        4

#define RX_QUEUE_SIZE                         256
#define RX_QUEUE_MASK                         255
#define RX_QUEUE_SIZE_LOG                     8

#define U32_PAD(n)		((4-(n))&0x3)

#define TFD_CTL_COUNT_SET(n)       (n << 24)
#define TFD_CTL_COUNT_GET(ctl)     ((ctl >> 24) & 7)
#define TFD_CTL_PAD_SET(n)         (n << 28)
#define TFD_CTL_PAD_GET(ctl)       (ctl >> 28)

/* Sizes and addresses for instruction and data memory (SRAM) in
 * 3945's embedded processor.  Driver access is via HBUS_TARG_MEM_* regs. */
#define IWL39_RTC_INST_LOWER_BOUND		(0x000000)
#define IWL39_RTC_INST_UPPER_BOUND		(0x014000)

#define IWL39_RTC_DATA_LOWER_BOUND		(0x800000)
#define IWL39_RTC_DATA_UPPER_BOUND		(0x808000)

#define IWL39_RTC_INST_SIZE (IWL39_RTC_INST_UPPER_BOUND - \
				IWL39_RTC_INST_LOWER_BOUND)
#define IWL39_RTC_DATA_SIZE (IWL39_RTC_DATA_UPPER_BOUND - \
				IWL39_RTC_DATA_LOWER_BOUND)

#define IWL39_MAX_INST_SIZE IWL39_RTC_INST_SIZE
#define IWL39_MAX_DATA_SIZE IWL39_RTC_DATA_SIZE

/* Size of uCode instruction memory in bootstrap state machine */
#define IWL39_MAX_BSM_SIZE IWL39_RTC_INST_SIZE

static inline int iwl3945_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= IWL39_RTC_DATA_LOWER_BOUND) &&
	       (addr < IWL39_RTC_DATA_UPPER_BOUND);
}

/* Base physical address of iwl3945_shared is provided to FH_TSSR_CBB_BASE
 * and &iwl3945_shared.rx_read_ptr[0] is provided to FH_RCSR_RPTR_ADDR(0) */
struct iwl3945_shared {
	__le32 tx_base_ptr[8];
} __packed;

static inline u8 iwl3945_hw_get_rate(__le16 rate_n_flags)
{
	return le16_to_cpu(rate_n_flags) & 0xFF;
}

static inline u16 iwl3945_hw_get_rate_n_flags(__le16 rate_n_flags)
{
	return le16_to_cpu(rate_n_flags);
}

static inline __le16 iwl3945_hw_set_rate_n_flags(u8 rate, u16 flags)
{
	return cpu_to_le16((u16)rate|flags);
}
#endif
