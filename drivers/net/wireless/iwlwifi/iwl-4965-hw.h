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
/*
 * Please use this file (iwl-4965-hw.h) only for hardware-related definitions.
 * Use iwl-4965-commands.h for uCode API definitions.
 * Use iwl-4965.h for driver implementation definitions.
 */

#ifndef __iwl_4965_hw_h__
#define __iwl_4965_hw_h__

/*
 * uCode queue management definitions ...
 * Queue #4 is the command queue for 3945 and 4965; map it to Tx FIFO chnl 4.
 * The first queue used for block-ack aggregation is #7 (4965 only).
 * All block-ack aggregation queues should map to Tx DMA/FIFO channel 7.
 */
#define IWL_CMD_QUEUE_NUM       4
#define IWL_CMD_FIFO_NUM        4
#define IWL_BACK_QUEUE_FIRST_ID 7

/* Tx rates */
#define IWL_CCK_RATES 4
#define IWL_OFDM_RATES 8
#define IWL_HT_RATES 16
#define IWL_MAX_RATES  (IWL_CCK_RATES+IWL_OFDM_RATES+IWL_HT_RATES)

/* Time constants */
#define SHORT_SLOT_TIME 9
#define LONG_SLOT_TIME 20

/* RSSI to dBm */
#define IWL_RSSI_OFFSET	44

/*
 * EEPROM related constants, enums, and structures.
 */

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

/* 4965 driver does not work with txpower calibration version < 5.
 * Look for this in calib_version member of struct iwl4965_eeprom. */
#define EEPROM_TX_POWER_VERSION_NEW    (5)


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
	struct iwl4965_eeprom_calib_measure measurements[EEPROM_TX_POWER_TX_CHAINS]
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
	struct iwl4965_eeprom_calib_subband_info band_info[EEPROM_TX_POWER_BANDS];
} __attribute__ ((packed));


/*
 * 4965 EEPROM map
 */
struct iwl4965_eeprom {
	u8 reserved0[16];
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
	u16 device_id;		/* abs.ofs: 16 */
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
	u8 reserved6[8];
#define EEPROM_4965_BOARD_REVISION          (2*0x4F)	/* 2 bytes */
	u16 board_revision_4965;	/* abs.ofs: 158 */
	u8 reserved7[13];
#define EEPROM_4965_BOARD_PBA               (2*0x56+1)	/* 9 bytes */
	u8 board_pba_number_4965[9];	/* abs.ofs: 173 */
	u8 reserved8[10];
#define EEPROM_REGULATORY_SKU_ID            (2*0x60)	/* 4  bytes */
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
#define EEPROM_REGULATORY_BAND_1            (2*0x62)	/* 2  bytes */
	u16 band_1_count;	/* abs.ofs: 196 */
#define EEPROM_REGULATORY_BAND_1_CHANNELS   (2*0x63)	/* 28 bytes */
	struct iwl4965_eeprom_channel band_1_channels[14]; /* abs.ofs: 196 */

/*
 * 4.9 GHz channels 183, 184, 185, 187, 188, 189, 192, 196,
 * 5.0 GHz channels 7, 8, 11, 12, 16
 * (4915-5080MHz) (none of these is ever supported)
 */
#define EEPROM_REGULATORY_BAND_2            (2*0x71)	/* 2  bytes */
	u16 band_2_count;	/* abs.ofs: 226 */
#define EEPROM_REGULATORY_BAND_2_CHANNELS   (2*0x72)	/* 26 bytes */
	struct iwl4965_eeprom_channel band_2_channels[13]; /* abs.ofs: 228 */

/*
 * 5.2 GHz channels 34, 36, 38, 40, 42, 44, 46, 48, 52, 56, 60, 64
 * (5170-5320MHz)
 */
#define EEPROM_REGULATORY_BAND_3            (2*0x7F)	/* 2  bytes */
	u16 band_3_count;	/* abs.ofs: 254 */
#define EEPROM_REGULATORY_BAND_3_CHANNELS   (2*0x80)	/* 24 bytes */
	struct iwl4965_eeprom_channel band_3_channels[12]; /* abs.ofs: 256 */

/*
 * 5.5 GHz channels 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140
 * (5500-5700MHz)
 */
#define EEPROM_REGULATORY_BAND_4            (2*0x8C)	/* 2  bytes */
	u16 band_4_count;	/* abs.ofs: 280 */
#define EEPROM_REGULATORY_BAND_4_CHANNELS   (2*0x8D)	/* 22 bytes */
	struct iwl4965_eeprom_channel band_4_channels[11]; /* abs.ofs: 282 */

/*
 * 5.7 GHz channels 145, 149, 153, 157, 161, 165
 * (5725-5825MHz)
 */
#define EEPROM_REGULATORY_BAND_5            (2*0x98)	/* 2  bytes */
	u16 band_5_count;	/* abs.ofs: 304 */
#define EEPROM_REGULATORY_BAND_5_CHANNELS   (2*0x99)	/* 12 bytes */
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
#define EEPROM_REGULATORY_BAND_24_FAT_CHANNELS (2*0xA0)	/* 14 bytes */
	struct iwl4965_eeprom_channel band_24_channels[7]; /* abs.ofs: 320 */
	u8 reserved11[2];

/*
 * 5.2 GHz FAT channels 36 (40), 44 (48), 52 (56), 60 (64),
 * 100 (104), 108 (112), 116 (120), 124 (128), 132 (136), 149 (153), 157 (161)
 */
#define EEPROM_REGULATORY_BAND_52_FAT_CHANNELS (2*0xA8)	/* 22 bytes */
	struct iwl4965_eeprom_channel band_52_channels[11]; /* abs.ofs: 336 */
	u8 reserved12[6];

/*
 * 4965 driver requires txpower calibration format version 5 or greater.
 * Driver does not work with txpower calibration version < 5.
 * This value is simply a 16-bit number, no major/minor versions here.
 */
#define EEPROM_CALIB_VERSION_OFFSET            (2*0xB6)	/* 2 bytes */
	u16 calib_version;	/* abs.ofs: 364 */
	u8 reserved13[2];
	u8 reserved14[96];	/* abs.ofs: 368 */

/*
 * 4965 Txpower calibration data.
 */
#define EEPROM_IWL_CALIB_TXPOWER_OFFSET        (2*0xE8)	/* 48  bytes */
	struct iwl4965_eeprom_calib_info calib_info;	/* abs.ofs: 464 */

	u8 reserved16[140];	/* fill out to full 1024 byte block */


} __attribute__ ((packed));

#define IWL_EEPROM_IMAGE_SIZE 1024

/* End of EEPROM */

#include "iwl-4965-commands.h"

#define PCI_LINK_CTRL      0x0F0
#define PCI_POWER_SOURCE   0x0C8
#define PCI_REG_WUM8       0x0E8
#define PCI_CFG_PMC_PME_FROM_D3COLD_SUPPORT         (0x80000000)

/*=== CSR (control and status registers) ===*/
#define CSR_BASE    (0x000)

#define CSR_SW_VER              (CSR_BASE+0x000)
#define CSR_HW_IF_CONFIG_REG    (CSR_BASE+0x000) /* hardware interface config */
#define CSR_INT_COALESCING      (CSR_BASE+0x004) /* accum ints, 32-usec units */
#define CSR_INT                 (CSR_BASE+0x008) /* host interrupt status/ack */
#define CSR_INT_MASK            (CSR_BASE+0x00c) /* host interrupt enable */
#define CSR_FH_INT_STATUS       (CSR_BASE+0x010) /* busmaster int status/ack*/
#define CSR_GPIO_IN             (CSR_BASE+0x018) /* read external chip pins */
#define CSR_RESET               (CSR_BASE+0x020) /* busmaster enable, NMI, etc*/
#define CSR_GP_CNTRL            (CSR_BASE+0x024)

/*
 * Hardware revision info
 * Bit fields:
 * 31-8:  Reserved
 *  7-4:  Type of device:  0x0 = 4965, 0xd = 3945
 *  3-2:  Revision step:  0 = A, 1 = B, 2 = C, 3 = D
 *  1-0:  "Dash" value, as in A-1, etc.
 *
 * NOTE:  Revision step affects calculation of CCK txpower for 4965.
 */
#define CSR_HW_REV              (CSR_BASE+0x028)

/* EEPROM reads */
#define CSR_EEPROM_REG          (CSR_BASE+0x02c)
#define CSR_EEPROM_GP           (CSR_BASE+0x030)
#define CSR_GP_UCODE		(CSR_BASE+0x044)
#define CSR_UCODE_DRV_GP1       (CSR_BASE+0x054)
#define CSR_UCODE_DRV_GP1_SET   (CSR_BASE+0x058)
#define CSR_UCODE_DRV_GP1_CLR   (CSR_BASE+0x05c)
#define CSR_UCODE_DRV_GP2       (CSR_BASE+0x060)
#define CSR_GIO_CHICKEN_BITS    (CSR_BASE+0x100)

/*
 * Indicates hardware rev, to determine CCK backoff for txpower calculation.
 * Bit fields:
 *  3-2:  0 = A, 1 = B, 2 = C, 3 = D step
 */
#define CSR_HW_REV_WA_REG	(CSR_BASE+0x22C)

/* Hardware interface configuration bits */
#define CSR_HW_IF_CONFIG_REG_BIT_KEDRON_R	(0x00000010)
#define CSR_HW_IF_CONFIG_REG_MSK_BOARD_VER	(0x00000C00)
#define CSR_HW_IF_CONFIG_REG_BIT_MAC_SI		(0x00000100)
#define CSR_HW_IF_CONFIG_REG_BIT_RADIO_SI	(0x00000200)
#define CSR_HW_IF_CONFIG_REG_BIT_EEPROM_OWN_SEM (0x00200000)

/* interrupt flags in INTA, set by uCode or hardware (e.g. dma),
 * acknowledged (reset) by host writing "1" to flagged bits. */
#define CSR_INT_BIT_FH_RX        (1 << 31) /* Rx DMA, cmd responses, FH_INT[17:16] */
#define CSR_INT_BIT_HW_ERR       (1 << 29) /* DMA hardware error FH_INT[31] */
#define CSR_INT_BIT_DNLD         (1 << 28) /* uCode Download */
#define CSR_INT_BIT_FH_TX        (1 << 27) /* Tx DMA FH_INT[1:0] */
#define CSR_INT_BIT_SCD          (1 << 26) /* TXQ pointer advanced */
#define CSR_INT_BIT_SW_ERR       (1 << 25) /* uCode error */
#define CSR_INT_BIT_RF_KILL      (1 << 7)  /* HW RFKILL switch GP_CNTRL[27] toggled */
#define CSR_INT_BIT_CT_KILL      (1 << 6)  /* Critical temp (chip too hot) rfkill */
#define CSR_INT_BIT_SW_RX        (1 << 3)  /* Rx, command responses, 3945 */
#define CSR_INT_BIT_WAKEUP       (1 << 1)  /* NIC controller waking up (pwr mgmt) */
#define CSR_INT_BIT_ALIVE        (1 << 0)  /* uCode interrupts once it initializes */

#define CSR_INI_SET_MASK	(CSR_INT_BIT_FH_RX   | \
				 CSR_INT_BIT_HW_ERR  | \
				 CSR_INT_BIT_FH_TX   | \
				 CSR_INT_BIT_SW_ERR  | \
				 CSR_INT_BIT_RF_KILL | \
				 CSR_INT_BIT_SW_RX   | \
				 CSR_INT_BIT_WAKEUP  | \
				 CSR_INT_BIT_ALIVE)

/* interrupt flags in FH (flow handler) (PCI busmaster DMA) */
#define CSR_FH_INT_BIT_ERR       (1 << 31) /* Error */
#define CSR_FH_INT_BIT_HI_PRIOR  (1 << 30) /* High priority Rx, bypass coalescing */
#define CSR_FH_INT_BIT_RX_CHNL1  (1 << 17) /* Rx channel 1 */
#define CSR_FH_INT_BIT_RX_CHNL0  (1 << 16) /* Rx channel 0 */
#define CSR_FH_INT_BIT_TX_CHNL1  (1 << 1)  /* Tx channel 1 */
#define CSR_FH_INT_BIT_TX_CHNL0  (1 << 0)  /* Tx channel 0 */

#define CSR_FH_INT_RX_MASK	(CSR_FH_INT_BIT_HI_PRIOR | \
				 CSR_FH_INT_BIT_RX_CHNL1 | \
				 CSR_FH_INT_BIT_RX_CHNL0)

#define CSR_FH_INT_TX_MASK	(CSR_FH_INT_BIT_TX_CHNL1 | \
				 CSR_FH_INT_BIT_TX_CHNL0)


/* RESET */
#define CSR_RESET_REG_FLAG_NEVO_RESET                (0x00000001)
#define CSR_RESET_REG_FLAG_FORCE_NMI                 (0x00000002)
#define CSR_RESET_REG_FLAG_SW_RESET                  (0x00000080)
#define CSR_RESET_REG_FLAG_MASTER_DISABLED           (0x00000100)
#define CSR_RESET_REG_FLAG_STOP_MASTER               (0x00000200)

/* GP (general purpose) CONTROL */
#define CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY        (0x00000001)
#define CSR_GP_CNTRL_REG_FLAG_INIT_DONE              (0x00000004)
#define CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ         (0x00000008)
#define CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP         (0x00000010)

#define CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN           (0x00000001)

#define CSR_GP_CNTRL_REG_MSK_POWER_SAVE_TYPE         (0x07000000)
#define CSR_GP_CNTRL_REG_FLAG_MAC_POWER_SAVE         (0x04000000)
#define CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW          (0x08000000)


/* EEPROM REG */
#define CSR_EEPROM_REG_READ_VALID_MSK	(0x00000001)
#define CSR_EEPROM_REG_BIT_CMD		(0x00000002)

/* EEPROM GP */
#define CSR_EEPROM_GP_VALID_MSK		(0x00000006)
#define CSR_EEPROM_GP_BAD_SIGNATURE	(0x00000000)
#define CSR_EEPROM_GP_IF_OWNER_MSK	(0x00000180)

/* UCODE DRV GP */
#define CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP             (0x00000001)
#define CSR_UCODE_SW_BIT_RFKILL                     (0x00000002)
#define CSR_UCODE_DRV_GP1_BIT_CMD_BLOCKED           (0x00000004)
#define CSR_UCODE_DRV_GP1_REG_BIT_CT_KILL_EXIT      (0x00000008)

/* GPIO */
#define CSR_GPIO_IN_BIT_AUX_POWER                   (0x00000200)
#define CSR_GPIO_IN_VAL_VAUX_PWR_SRC                (0x00000000)
#define CSR_GPIO_IN_VAL_VMAIN_PWR_SRC		CSR_GPIO_IN_BIT_AUX_POWER

/* GI Chicken Bits */
#define CSR_GIO_CHICKEN_BITS_REG_BIT_L1A_NO_L0S_RX  (0x00800000)
#define CSR_GIO_CHICKEN_BITS_REG_BIT_DIS_L0S_EXIT_TIMER  (0x20000000)

/*=== HBUS (Host-side Bus) ===*/
#define HBUS_BASE	(0x400)

/*
 * Registers for accessing device's internal SRAM memory (e.g. SCD SRAM
 * structures, error log, event log, verifying uCode load).
 * First write to address register, then read from or write to data register
 * to complete the job.  Once the address register is set up, accesses to
 * data registers auto-increment the address by one dword.
 * Bit usage for address registers (read or write):
 *  0-31:  memory address within device
 */
#define HBUS_TARG_MEM_RADDR     (HBUS_BASE+0x00c)
#define HBUS_TARG_MEM_WADDR     (HBUS_BASE+0x010)
#define HBUS_TARG_MEM_WDAT      (HBUS_BASE+0x018)
#define HBUS_TARG_MEM_RDAT      (HBUS_BASE+0x01c)

/*
 * Registers for accessing device's internal peripheral registers
 * (e.g. SCD, BSM, etc.).  First write to address register,
 * then read from or write to data register to complete the job.
 * Bit usage for address registers (read or write):
 *  0-15:  register address (offset) within device
 * 24-25:  (# bytes - 1) to read or write (e.g. 3 for dword)
 */
#define HBUS_TARG_PRPH_WADDR    (HBUS_BASE+0x044)
#define HBUS_TARG_PRPH_RADDR    (HBUS_BASE+0x048)
#define HBUS_TARG_PRPH_WDAT     (HBUS_BASE+0x04c)
#define HBUS_TARG_PRPH_RDAT     (HBUS_BASE+0x050)

/*
 * Per-Tx-queue write pointer (index, really!) (3945 and 4965).
 * Driver sets this to indicate index to next TFD that driver will fill
 * (1 past latest filled).
 * Bit usage:
 *  0-7:  queue write index (0-255)
 * 11-8:  queue selector (0-15)
 */
#define HBUS_TARG_WRPTR         (HBUS_BASE+0x060)

#define HBUS_TARG_MBX_C         (HBUS_BASE+0x030)

#define HBUS_TARG_MBX_C_REG_BIT_CMD_BLOCKED         (0x00000004)

#define TFD_QUEUE_SIZE_MAX      (256)

#define IWL_NUM_SCAN_RATES         (2)

#define IWL_DEFAULT_TX_RETRY  15

#define RX_QUEUE_SIZE                         256
#define RX_QUEUE_MASK                         255
#define RX_QUEUE_SIZE_LOG                     8

#define TFD_TX_CMD_SLOTS 256
#define TFD_CMD_SLOTS 32

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl4965_cmd) - \
			      sizeof(struct iwl4965_cmd_meta))

/*
 * RX related structures and functions
 */
#define RX_FREE_BUFFERS 64
#define RX_LOW_WATERMARK 8

/* Size of one Rx buffer in host DRAM */
#define IWL_RX_BUF_SIZE_4K (4 * 1024)
#define IWL_RX_BUF_SIZE_8K (8 * 1024)

/* Sizes and addresses for instruction and data memory (SRAM) in
 * 4965's embedded processor.  Driver access is via HBUS_TARG_MEM_* regs. */
#define RTC_INST_LOWER_BOUND			(0x000000)
#define KDR_RTC_INST_UPPER_BOUND		(0x018000)

#define RTC_DATA_LOWER_BOUND			(0x800000)
#define KDR_RTC_DATA_UPPER_BOUND		(0x80A000)

#define KDR_RTC_INST_SIZE    (KDR_RTC_INST_UPPER_BOUND - RTC_INST_LOWER_BOUND)
#define KDR_RTC_DATA_SIZE    (KDR_RTC_DATA_UPPER_BOUND - RTC_DATA_LOWER_BOUND)

#define IWL_MAX_INST_SIZE KDR_RTC_INST_SIZE
#define IWL_MAX_DATA_SIZE KDR_RTC_DATA_SIZE

/* Size of uCode instruction memory in bootstrap state machine */
#define IWL_MAX_BSM_SIZE BSM_SRAM_SIZE

static inline int iwl4965_hw_valid_rtc_data_addr(u32 addr)
{
	return (addr >= RTC_DATA_LOWER_BOUND) &&
	       (addr < KDR_RTC_DATA_UPPER_BOUND);
}

/********************* START TEMPERATURE *************************************/

/**
 * 4965 temperature calculation.
 *
 * The driver must calculate the device temperature before calculating
 * a txpower setting (amplifier gain is temperature dependent).  The
 * calculation uses 4 measurements, 3 of which (R1, R2, R3) are calibration
 * values used for the life of the driver, and one of which (R4) is the
 * real-time temperature indicator.
 *
 * uCode provides all 4 values to the driver via the "initialize alive"
 * notification (see struct iwl4965_init_alive_resp).  After the runtime uCode
 * image loads, uCode updates the R4 value via statistics notifications
 * (see STATISTICS_NOTIFICATION), which occur after each received beacon
 * when associated, or can be requested via REPLY_STATISTICS_CMD.
 *
 * NOTE:  uCode provides the R4 value as a 23-bit signed value.  Driver
 *        must sign-extend to 32 bits before applying formula below.
 *
 * Formula:
 *
 * degrees Kelvin = ((97 * 259 * (R4 - R2) / (R3 - R1)) / 100) + 8
 *
 * NOTE:  The basic formula is 259 * (R4-R2) / (R3-R1).  The 97/100 is
 * an additional correction, which should be centered around 0 degrees
 * Celsius (273 degrees Kelvin).  The 8 (3 percent of 273) compensates for
 * centering the 97/100 correction around 0 degrees K.
 *
 * Add 273 to Kelvin value to find degrees Celsius, for comparing current
 * temperature with factory-measured temperatures when calculating txpower
 * settings.
 */
#define TEMPERATURE_CALIB_KELVIN_OFFSET 8
#define TEMPERATURE_CALIB_A_VAL 259

/* Limit range of calculated temperature to be between these Kelvin values */
#define IWL_TX_POWER_TEMPERATURE_MIN  (263)
#define IWL_TX_POWER_TEMPERATURE_MAX  (410)

#define IWL_TX_POWER_TEMPERATURE_OUT_OF_RANGE(t) \
	(((t) < IWL_TX_POWER_TEMPERATURE_MIN) || \
	 ((t) > IWL_TX_POWER_TEMPERATURE_MAX))

/********************* END TEMPERATURE ***************************************/

/********************* START TXPOWER *****************************************/

/**
 * 4965 txpower calculations rely on information from three sources:
 *
 *     1) EEPROM
 *     2) "initialize" alive notification
 *     3) statistics notifications
 *
 * EEPROM data consists of:
 *
 * 1)  Regulatory information (max txpower and channel usage flags) is provided
 *     separately for each channel that can possibly supported by 4965.
 *     40 MHz wide (.11n fat) channels are listed separately from 20 MHz
 *     (legacy) channels.
 *
 *     See struct iwl4965_eeprom_channel for format, and struct iwl4965_eeprom
 *     for locations in EEPROM.
 *
 * 2)  Factory txpower calibration information is provided separately for
 *     sub-bands of contiguous channels.  2.4GHz has just one sub-band,
 *     but 5 GHz has several sub-bands.
 *
 *     In addition, per-band (2.4 and 5 Ghz) saturation txpowers are provided.
 *
 *     See struct iwl4965_eeprom_calib_info (and the tree of structures
 *     contained within it) for format, and struct iwl4965_eeprom for
 *     locations in EEPROM.
 *
 * "Initialization alive" notification (see struct iwl4965_init_alive_resp)
 * consists of:
 *
 * 1)  Temperature calculation parameters.
 *
 * 2)  Power supply voltage measurement.
 *
 * 3)  Tx gain compensation to balance 2 transmitters for MIMO use.
 *
 * Statistics notifications deliver:
 *
 * 1)  Current values for temperature param R4.
 */

/**
 * To calculate a txpower setting for a given desired target txpower, channel,
 * modulation bit rate, and transmitter chain (4965 has 2 transmitters to
 * support MIMO and transmit diversity), driver must do the following:
 *
 * 1)  Compare desired txpower vs. (EEPROM) regulatory limit for this channel.
 *     Do not exceed regulatory limit; reduce target txpower if necessary.
 *
 *     If setting up txpowers for MIMO rates (rate indexes 8-15, 24-31),
 *     2 transmitters will be used simultaneously; driver must reduce the
 *     regulatory limit by 3 dB (half-power) for each transmitter, so the
 *     combined total output of the 2 transmitters is within regulatory limits.
 *
 *
 * 2)  Compare target txpower vs. (EEPROM) saturation txpower *reduced by
 *     backoff for this bit rate*.  Do not exceed (saturation - backoff[rate]);
 *     reduce target txpower if necessary.
 *
 *     Backoff values below are in 1/2 dB units (equivalent to steps in
 *     txpower gain tables):
 *
 *     OFDM 6 - 36 MBit:  10 steps (5 dB)
 *     OFDM 48 MBit:      15 steps (7.5 dB)
 *     OFDM 54 MBit:      17 steps (8.5 dB)
 *     OFDM 60 MBit:      20 steps (10 dB)
 *     CCK all rates:     10 steps (5 dB)
 *
 *     Backoff values apply to saturation txpower on a per-transmitter basis;
 *     when using MIMO (2 transmitters), each transmitter uses the same
 *     saturation level provided in EEPROM, and the same backoff values;
 *     no reduction (such as with regulatory txpower limits) is required.
 *
 *     Saturation and Backoff values apply equally to 20 Mhz (legacy) channel
 *     widths and 40 Mhz (.11n fat) channel widths; there is no separate
 *     factory measurement for fat channels.
 *
 *     The result of this step is the final target txpower.  The rest of
 *     the steps figure out the proper settings for the device to achieve
 *     that target txpower.
 *
 *
 * 3)  Determine (EEPROM) calibration subband for the target channel, by
 *     comparing against first and last channels in each subband
 *     (see struct iwl4965_eeprom_calib_subband_info).
 *
 *
 * 4)  Linearly interpolate (EEPROM) factory calibration measurement sets,
 *     referencing the 2 factory-measured (sample) channels within the subband.
 *
 *     Interpolation is based on difference between target channel's frequency
 *     and the sample channels' frequencies.  Since channel numbers are based
 *     on frequency (5 MHz between each channel number), this is equivalent
 *     to interpolating based on channel number differences.
 *
 *     Note that the sample channels may or may not be the channels at the
 *     edges of the subband.  The target channel may be "outside" of the
 *     span of the sampled channels.
 *
 *     Driver may choose the pair (for 2 Tx chains) of measurements (see
 *     struct iwl4965_eeprom_calib_ch_info) for which the actual measured
 *     txpower comes closest to the desired txpower.  Usually, though,
 *     the middle set of measurements is closest to the regulatory limits,
 *     and is therefore a good choice for all txpower calculations (this
 *     assumes that high accuracy is needed for maximizing legal txpower,
 *     while lower txpower configurations do not need as much accuracy).
 *
 *     Driver should interpolate both members of the chosen measurement pair,
 *     i.e. for both Tx chains (radio transmitters), unless the driver knows
 *     that only one of the chains will be used (e.g. only one tx antenna
 *     connected, but this should be unusual).  The rate scaling algorithm
 *     switches antennas to find best performance, so both Tx chains will
 *     be used (although only one at a time) even for non-MIMO transmissions.
 *
 *     Driver should interpolate factory values for temperature, gain table
 *     index, and actual power.  The power amplifier detector values are
 *     not used by the driver.
 *
 *     Sanity check:  If the target channel happens to be one of the sample
 *     channels, the results should agree with the sample channel's
 *     measurements!
 *
 *
 * 5)  Find difference between desired txpower and (interpolated)
 *     factory-measured txpower.  Using (interpolated) factory gain table index
 *     (shown elsewhere) as a starting point, adjust this index lower to
 *     increase txpower, or higher to decrease txpower, until the target
 *     txpower is reached.  Each step in the gain table is 1/2 dB.
 *
 *     For example, if factory measured txpower is 16 dBm, and target txpower
 *     is 13 dBm, add 6 steps to the factory gain index to reduce txpower
 *     by 3 dB.
 *
 *
 * 6)  Find difference between current device temperature and (interpolated)
 *     factory-measured temperature for sub-band.  Factory values are in
 *     degrees Celsius.  To calculate current temperature, see comments for
 *     "4965 temperature calculation".
 *
 *     If current temperature is higher than factory temperature, driver must
 *     increase gain (lower gain table index), and vice versa.
 *
 *     Temperature affects gain differently for different channels:
 *
 *     2.4 GHz all channels:  3.5 degrees per half-dB step
 *     5 GHz channels 34-43:  4.5 degrees per half-dB step
 *     5 GHz channels >= 44:  4.0 degrees per half-dB step
 *
 *     NOTE:  Temperature can increase rapidly when transmitting, especially
 *            with heavy traffic at high txpowers.  Driver should update
 *            temperature calculations often under these conditions to
 *            maintain strong txpower in the face of rising temperature.
 *
 *
 * 7)  Find difference between current power supply voltage indicator
 *     (from "initialize alive") and factory-measured power supply voltage
 *     indicator (EEPROM).
 *
 *     If the current voltage is higher (indicator is lower) than factory
 *     voltage, gain should be reduced (gain table index increased) by:
 *
 *     (eeprom - current) / 7
 *
 *     If the current voltage is lower (indicator is higher) than factory
 *     voltage, gain should be increased (gain table index decreased) by:
 *
 *     2 * (current - eeprom) / 7
 *
 *     If number of index steps in either direction turns out to be > 2,
 *     something is wrong ... just use 0.
 *
 *     NOTE:  Voltage compensation is independent of band/channel.
 *
 *     NOTE:  "Initialize" uCode measures current voltage, which is assumed
 *            to be constant after this initial measurement.  Voltage
 *            compensation for txpower (number of steps in gain table)
 *            may be calculated once and used until the next uCode bootload.
 *
 *
 * 8)  If setting up txpowers for MIMO rates (rate indexes 8-15, 24-31),
 *     adjust txpower for each transmitter chain, so txpower is balanced
 *     between the two chains.  There are 5 pairs of tx_atten[group][chain]
 *     values in "initialize alive", one pair for each of 5 channel ranges:
 *
 *     Group 0:  5 GHz channel 34-43
 *     Group 1:  5 GHz channel 44-70
 *     Group 2:  5 GHz channel 71-124
 *     Group 3:  5 GHz channel 125-200
 *     Group 4:  2.4 GHz all channels
 *
 *     Add the tx_atten[group][chain] value to the index for the target chain.
 *     The values are signed, but are in pairs of 0 and a non-negative number,
 *     so as to reduce gain (if necessary) of the "hotter" channel.  This
 *     avoids any need to double-check for regulatory compliance after
 *     this step.
 *
 *
 * 9)  If setting up for a CCK rate, lower the gain by adding a CCK compensation
 *     value to the index:
 *
 *     Hardware rev B:  9 steps (4.5 dB)
 *     Hardware rev C:  5 steps (2.5 dB)
 *
 *     Hardware rev for 4965 can be determined by reading CSR_HW_REV_WA_REG,
 *     bits [3:2], 1 = B, 2 = C.
 *
 *     NOTE:  This compensation is in addition to any saturation backoff that
 *            might have been applied in an earlier step.
 *
 *
 * 10) Select the gain table, based on band (2.4 vs 5 GHz).
 *
 *     Limit the adjusted index to stay within the table!
 *
 *
 * 11) Read gain table entries for DSP and radio gain, place into appropriate
 *     location(s) in command (struct iwl4965_txpowertable_cmd).
 */

/* Limit range of txpower output target to be between these values */
#define IWL_TX_POWER_TARGET_POWER_MIN       (0)	/* 0 dBm = 1 milliwatt */
#define IWL_TX_POWER_TARGET_POWER_MAX      (16)	/* 16 dBm */

/**
 * When MIMO is used (2 transmitters operating simultaneously), driver should
 * limit each transmitter to deliver a max of 3 dB below the regulatory limit
 * for the device.  That is, use half power for each transmitter, so total
 * txpower is within regulatory limits.
 *
 * The value "6" represents number of steps in gain table to reduce power 3 dB.
 * Each step is 1/2 dB.
 */
#define IWL_TX_POWER_MIMO_REGULATORY_COMPENSATION (6)

/**
 * CCK gain compensation.
 *
 * When calculating txpowers for CCK, after making sure that the target power
 * is within regulatory and saturation limits, driver must additionally
 * back off gain by adding these values to the gain table index.
 *
 * Hardware rev for 4965 can be determined by reading CSR_HW_REV_WA_REG,
 * bits [3:2], 1 = B, 2 = C.
 */
#define IWL_TX_POWER_CCK_COMPENSATION_B_STEP (9)
#define IWL_TX_POWER_CCK_COMPENSATION_C_STEP (5)

/*
 * 4965 power supply voltage compensation for txpower
 */
#define TX_POWER_IWL_VOLTAGE_CODES_PER_03V   (7)

/**
 * Gain tables.
 *
 * The following tables contain pair of values for setting txpower, i.e.
 * gain settings for the output of the device's digital signal processor (DSP),
 * and for the analog gain structure of the transmitter.
 *
 * Each entry in the gain tables represents a step of 1/2 dB.  Note that these
 * are *relative* steps, not indications of absolute output power.  Output
 * power varies with temperature, voltage, and channel frequency, and also
 * requires consideration of average power (to satisfy regulatory constraints),
 * and peak power (to avoid distortion of the output signal).
 *
 * Each entry contains two values:
 * 1)  DSP gain (or sometimes called DSP attenuation).  This is a fine-grained
 *     linear value that multiplies the output of the digital signal processor,
 *     before being sent to the analog radio.
 * 2)  Radio gain.  This sets the analog gain of the radio Tx path.
 *     It is a coarser setting, and behaves in a logarithmic (dB) fashion.
 *
 * EEPROM contains factory calibration data for txpower.  This maps actual
 * measured txpower levels to gain settings in the "well known" tables
 * below ("well-known" means here that both factory calibration *and* the
 * driver work with the same table).
 *
 * There are separate tables for 2.4 GHz and 5 GHz bands.  The 5 GHz table
 * has an extension (into negative indexes), in case the driver needs to
 * boost power setting for high device temperatures (higher than would be
 * present during factory calibration).  A 5 Ghz EEPROM index of "40"
 * corresponds to the 49th entry in the table used by the driver.
 */
#define MIN_TX_GAIN_INDEX		(0)  /* highest gain, lowest idx, 2.4 */
#define MIN_TX_GAIN_INDEX_52GHZ_EXT	(-9) /* highest gain, lowest idx, 5 */

/**
 * 2.4 GHz gain table
 *
 * Index    Dsp gain   Radio gain
 *   0        110         0x3f      (highest gain)
 *   1        104         0x3f
 *   2         98         0x3f
 *   3        110         0x3e
 *   4        104         0x3e
 *   5         98         0x3e
 *   6        110         0x3d
 *   7        104         0x3d
 *   8         98         0x3d
 *   9        110         0x3c
 *  10        104         0x3c
 *  11         98         0x3c
 *  12        110         0x3b
 *  13        104         0x3b
 *  14         98         0x3b
 *  15        110         0x3a
 *  16        104         0x3a
 *  17         98         0x3a
 *  18        110         0x39
 *  19        104         0x39
 *  20         98         0x39
 *  21        110         0x38
 *  22        104         0x38
 *  23         98         0x38
 *  24        110         0x37
 *  25        104         0x37
 *  26         98         0x37
 *  27        110         0x36
 *  28        104         0x36
 *  29         98         0x36
 *  30        110         0x35
 *  31        104         0x35
 *  32         98         0x35
 *  33        110         0x34
 *  34        104         0x34
 *  35         98         0x34
 *  36        110         0x33
 *  37        104         0x33
 *  38         98         0x33
 *  39        110         0x32
 *  40        104         0x32
 *  41         98         0x32
 *  42        110         0x31
 *  43        104         0x31
 *  44         98         0x31
 *  45        110         0x30
 *  46        104         0x30
 *  47         98         0x30
 *  48        110          0x6
 *  49        104          0x6
 *  50         98          0x6
 *  51        110          0x5
 *  52        104          0x5
 *  53         98          0x5
 *  54        110          0x4
 *  55        104          0x4
 *  56         98          0x4
 *  57        110          0x3
 *  58        104          0x3
 *  59         98          0x3
 *  60        110          0x2
 *  61        104          0x2
 *  62         98          0x2
 *  63        110          0x1
 *  64        104          0x1
 *  65         98          0x1
 *  66        110          0x0
 *  67        104          0x0
 *  68         98          0x0
 *  69         97            0
 *  70         96            0
 *  71         95            0
 *  72         94            0
 *  73         93            0
 *  74         92            0
 *  75         91            0
 *  76         90            0
 *  77         89            0
 *  78         88            0
 *  79         87            0
 *  80         86            0
 *  81         85            0
 *  82         84            0
 *  83         83            0
 *  84         82            0
 *  85         81            0
 *  86         80            0
 *  87         79            0
 *  88         78            0
 *  89         77            0
 *  90         76            0
 *  91         75            0
 *  92         74            0
 *  93         73            0
 *  94         72            0
 *  95         71            0
 *  96         70            0
 *  97         69            0
 *  98         68            0
 */

/**
 * 5 GHz gain table
 *
 * Index    Dsp gain   Radio gain
 *  -9 	      123         0x3F      (highest gain)
 *  -8 	      117         0x3F
 *  -7        110         0x3F
 *  -6        104         0x3F
 *  -5         98         0x3F
 *  -4        110         0x3E
 *  -3        104         0x3E
 *  -2         98         0x3E
 *  -1        110         0x3D
 *   0        104         0x3D
 *   1         98         0x3D
 *   2        110         0x3C
 *   3        104         0x3C
 *   4         98         0x3C
 *   5        110         0x3B
 *   6        104         0x3B
 *   7         98         0x3B
 *   8        110         0x3A
 *   9        104         0x3A
 *  10         98         0x3A
 *  11        110         0x39
 *  12        104         0x39
 *  13         98         0x39
 *  14        110         0x38
 *  15        104         0x38
 *  16         98         0x38
 *  17        110         0x37
 *  18        104         0x37
 *  19         98         0x37
 *  20        110         0x36
 *  21        104         0x36
 *  22         98         0x36
 *  23        110         0x35
 *  24        104         0x35
 *  25         98         0x35
 *  26        110         0x34
 *  27        104         0x34
 *  28         98         0x34
 *  29        110         0x33
 *  30        104         0x33
 *  31         98         0x33
 *  32        110         0x32
 *  33        104         0x32
 *  34         98         0x32
 *  35        110         0x31
 *  36        104         0x31
 *  37         98         0x31
 *  38        110         0x30
 *  39        104         0x30
 *  40         98         0x30
 *  41        110         0x25
 *  42        104         0x25
 *  43         98         0x25
 *  44        110         0x24
 *  45        104         0x24
 *  46         98         0x24
 *  47        110         0x23
 *  48        104         0x23
 *  49         98         0x23
 *  50        110         0x22
 *  51        104         0x18
 *  52         98         0x18
 *  53        110         0x17
 *  54        104         0x17
 *  55         98         0x17
 *  56        110         0x16
 *  57        104         0x16
 *  58         98         0x16
 *  59        110         0x15
 *  60        104         0x15
 *  61         98         0x15
 *  62        110         0x14
 *  63        104         0x14
 *  64         98         0x14
 *  65        110         0x13
 *  66        104         0x13
 *  67         98         0x13
 *  68        110         0x12
 *  69        104         0x08
 *  70         98         0x08
 *  71        110         0x07
 *  72        104         0x07
 *  73         98         0x07
 *  74        110         0x06
 *  75        104         0x06
 *  76         98         0x06
 *  77        110         0x05
 *  78        104         0x05
 *  79         98         0x05
 *  80        110         0x04
 *  81        104         0x04
 *  82         98         0x04
 *  83        110         0x03
 *  84        104         0x03
 *  85         98         0x03
 *  86        110         0x02
 *  87        104         0x02
 *  88         98         0x02
 *  89        110         0x01
 *  90        104         0x01
 *  91         98         0x01
 *  92        110         0x00
 *  93        104         0x00
 *  94         98         0x00
 *  95         93         0x00
 *  96         88         0x00
 *  97         83         0x00
 *  98         78         0x00
 */


/**
 * Sanity checks and default values for EEPROM regulatory levels.
 * If EEPROM values fall outside MIN/MAX range, use default values.
 *
 * Regulatory limits refer to the maximum average txpower allowed by
 * regulatory agencies in the geographies in which the device is meant
 * to be operated.  These limits are SKU-specific (i.e. geography-specific),
 * and channel-specific; each channel has an individual regulatory limit
 * listed in the EEPROM.
 *
 * Units are in half-dBm (i.e. "34" means 17 dBm).
 */
#define IWL_TX_POWER_DEFAULT_REGULATORY_24   (34)
#define IWL_TX_POWER_DEFAULT_REGULATORY_52   (34)
#define IWL_TX_POWER_REGULATORY_MIN          (0)
#define IWL_TX_POWER_REGULATORY_MAX          (34)

/**
 * Sanity checks and default values for EEPROM saturation levels.
 * If EEPROM values fall outside MIN/MAX range, use default values.
 *
 * Saturation is the highest level that the output power amplifier can produce
 * without significant clipping distortion.  This is a "peak" power level.
 * Different types of modulation (i.e. various "rates", and OFDM vs. CCK)
 * require differing amounts of backoff, relative to their average power output,
 * in order to avoid clipping distortion.
 *
 * Driver must make sure that it is violating neither the saturation limit,
 * nor the regulatory limit, when calculating Tx power settings for various
 * rates.
 *
 * Units are in half-dBm (i.e. "38" means 19 dBm).
 */
#define IWL_TX_POWER_DEFAULT_SATURATION_24   (38)
#define IWL_TX_POWER_DEFAULT_SATURATION_52   (38)
#define IWL_TX_POWER_SATURATION_MIN          (20)
#define IWL_TX_POWER_SATURATION_MAX          (50)

/**
 * Channel groups used for Tx Attenuation calibration (MIMO tx channel balance)
 * and thermal Txpower calibration.
 *
 * When calculating txpower, driver must compensate for current device
 * temperature; higher temperature requires higher gain.  Driver must calculate
 * current temperature (see "4965 temperature calculation"), then compare vs.
 * factory calibration temperature in EEPROM; if current temperature is higher
 * than factory temperature, driver must *increase* gain by proportions shown
 * in table below.  If current temperature is lower than factory, driver must
 * *decrease* gain.
 *
 * Different frequency ranges require different compensation, as shown below.
 */
/* Group 0, 5.2 GHz ch 34-43:  4.5 degrees per 1/2 dB. */
#define CALIB_IWL_TX_ATTEN_GR1_FCH 34
#define CALIB_IWL_TX_ATTEN_GR1_LCH 43

/* Group 1, 5.3 GHz ch 44-70:  4.0 degrees per 1/2 dB. */
#define CALIB_IWL_TX_ATTEN_GR2_FCH 44
#define CALIB_IWL_TX_ATTEN_GR2_LCH 70

/* Group 2, 5.5 GHz ch 71-124:  4.0 degrees per 1/2 dB. */
#define CALIB_IWL_TX_ATTEN_GR3_FCH 71
#define CALIB_IWL_TX_ATTEN_GR3_LCH 124

/* Group 3, 5.7 GHz ch 125-200:  4.0 degrees per 1/2 dB. */
#define CALIB_IWL_TX_ATTEN_GR4_FCH 125
#define CALIB_IWL_TX_ATTEN_GR4_LCH 200

/* Group 4, 2.4 GHz all channels:  3.5 degrees per 1/2 dB. */
#define CALIB_IWL_TX_ATTEN_GR5_FCH 1
#define CALIB_IWL_TX_ATTEN_GR5_LCH 20

enum {
	CALIB_CH_GROUP_1 = 0,
	CALIB_CH_GROUP_2 = 1,
	CALIB_CH_GROUP_3 = 2,
	CALIB_CH_GROUP_4 = 3,
	CALIB_CH_GROUP_5 = 4,
	CALIB_CH_GROUP_MAX
};

/********************* END TXPOWER *****************************************/

/****************************/
/* Flow Handler Definitions */
/****************************/

/**
 * This I/O area is directly read/writable by driver (e.g. Linux uses writel())
 * Addresses are offsets from device's PCI hardware base address.
 */
#define FH_MEM_LOWER_BOUND                   (0x1000)
#define FH_MEM_UPPER_BOUND                   (0x1EF0)

/**
 * Keep-Warm (KW) buffer base address.
 *
 * Driver must allocate a 4KByte buffer that is used by 4965 for keeping the
 * host DRAM powered on (via dummy accesses to DRAM) to maintain low-latency
 * DRAM access when 4965 is Txing or Rxing.  The dummy accesses prevent host
 * from going into a power-savings mode that would cause higher DRAM latency,
 * and possible data over/under-runs, before all Tx/Rx is complete.
 *
 * Driver loads IWL_FH_KW_MEM_ADDR_REG with the physical address (bits 35:4)
 * of the buffer, which must be 4K aligned.  Once this is set up, the 4965
 * automatically invokes keep-warm accesses when normal accesses might not
 * be sufficient to maintain fast DRAM response.
 *
 * Bit fields:
 *  31-0:  Keep-warm buffer physical base address [35:4], must be 4K aligned
 */
#define IWL_FH_KW_MEM_ADDR_REG		     (FH_MEM_LOWER_BOUND + 0x97C)


/**
 * TFD Circular Buffers Base (CBBC) addresses
 *
 * 4965 has 16 base pointer registers, one for each of 16 host-DRAM-resident
 * circular buffers (CBs/queues) containing Transmit Frame Descriptors (TFDs)
 * (see struct iwl_tfd_frame).  These 16 pointer registers are offset by 0x04
 * bytes from one another.  Each TFD circular buffer in DRAM must be 256-byte
 * aligned (address bits 0-7 must be 0).
 *
 * Bit fields in each pointer register:
 *  27-0: TFD CB physical base address [35:8], must be 256-byte aligned
 */
#define FH_MEM_CBBC_LOWER_BOUND              (FH_MEM_LOWER_BOUND + 0x9D0)
#define FH_MEM_CBBC_UPPER_BOUND              (FH_MEM_LOWER_BOUND + 0xA10)

/* Find TFD CB base pointer for given queue (range 0-15). */
#define FH_MEM_CBBC_QUEUE(x)  (FH_MEM_CBBC_LOWER_BOUND + (x) * 0x4)


/**
 * Rx SRAM Control and Status Registers (RSCSR)
 *
 * These registers provide handshake between driver and 4965 for the Rx queue
 * (this queue handles *all* command responses, notifications, Rx data, etc.
 * sent from 4965 uCode to host driver).  Unlike Tx, there is only one Rx
 * queue, and only one Rx DMA/FIFO channel.  Also unlike Tx, which can
 * concatenate up to 20 DRAM buffers to form a Tx frame, each Receive Buffer
 * Descriptor (RBD) points to only one Rx Buffer (RB); there is a 1:1
 * mapping between RBDs and RBs.
 *
 * Driver must allocate host DRAM memory for the following, and set the
 * physical address of each into 4965 registers:
 *
 * 1)  Receive Buffer Descriptor (RBD) circular buffer (CB), typically with 256
 *     entries (although any power of 2, up to 4096, is selectable by driver).
 *     Each entry (1 dword) points to a receive buffer (RB) of consistent size
 *     (typically 4K, although 8K or 16K are also selectable by driver).
 *     Driver sets up RB size and number of RBDs in the CB via Rx config
 *     register FH_MEM_RCSR_CHNL0_CONFIG_REG.
 *
 *     Bit fields within one RBD:
 *     27-0:  Receive Buffer physical address bits [35:8], 256-byte aligned
 *
 *     Driver sets physical address [35:8] of base of RBD circular buffer
 *     into FH_RSCSR_CHNL0_RBDCB_BASE_REG [27:0].
 *
 * 2)  Rx status buffer, 8 bytes, in which 4965 indicates which Rx Buffers
 *     (RBs) have been filled, via a "write pointer", actually the index of
 *     the RB's corresponding RBD within the circular buffer.  Driver sets
 *     physical address [35:4] into FH_RSCSR_CHNL0_STTS_WPTR_REG [31:0].
 *
 *     Bit fields in lower dword of Rx status buffer (upper dword not used
 *     by driver; see struct iwl4965_shared, val0):
 *     31-12:  Not used by driver
 *     11- 0:  Index of last filled Rx buffer descriptor
 *             (4965 writes, driver reads this value)
 *
 * As the driver prepares Receive Buffers (RBs) for 4965 to fill, driver must
 * enter pointers to these RBs into contiguous RBD circular buffer entries,
 * and update the 4965's "write" index register, FH_RSCSR_CHNL0_RBDCB_WPTR_REG.
 *
 * This "write" index corresponds to the *next* RBD that the driver will make
 * available, i.e. one RBD past the tail of the ready-to-fill RBDs within
 * the circular buffer.  This value should initially be 0 (before preparing any
 * RBs), should be 8 after preparing the first 8 RBs (for example), and must
 * wrap back to 0 at the end of the circular buffer (but don't wrap before
 * "read" index has advanced past 1!  See below).
 * NOTE:  4965 EXPECTS THE WRITE INDEX TO BE INCREMENTED IN MULTIPLES OF 8.
 *
 * As the 4965 fills RBs (referenced from contiguous RBDs within the circular
 * buffer), it updates the Rx status buffer in host DRAM, 2) described above,
 * to tell the driver the index of the latest filled RBD.  The driver must
 * read this "read" index from DRAM after receiving an Rx interrupt from 4965.
 *
 * The driver must also internally keep track of a third index, which is the
 * next RBD to process.  When receiving an Rx interrupt, driver should process
 * all filled but unprocessed RBs up to, but not including, the RB
 * corresponding to the "read" index.  For example, if "read" index becomes "1",
 * driver may process the RB pointed to by RBD 0.  Depending on volume of
 * traffic, there may be many RBs to process.
 *
 * If read index == write index, 4965 thinks there is no room to put new data.
 * Due to this, the maximum number of filled RBs is 255, instead of 256.  To
 * be safe, make sure that there is a gap of at least 2 RBDs between "write"
 * and "read" indexes; that is, make sure that there are no more than 254
 * buffers waiting to be filled.
 */
#define FH_MEM_RSCSR_LOWER_BOUND	(FH_MEM_LOWER_BOUND + 0xBC0)
#define FH_MEM_RSCSR_UPPER_BOUND	(FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RSCSR_CHNL0		(FH_MEM_RSCSR_LOWER_BOUND)

/**
 * Physical base address of 8-byte Rx Status buffer.
 * Bit fields:
 *  31-0: Rx status buffer physical base address [35:4], must 16-byte aligned.
 */
#define FH_RSCSR_CHNL0_STTS_WPTR_REG		(FH_MEM_RSCSR_CHNL0)

/**
 * Physical base address of Rx Buffer Descriptor Circular Buffer.
 * Bit fields:
 *  27-0:  RBD CD physical base address [35:8], must be 256-byte aligned.
 */
#define FH_RSCSR_CHNL0_RBDCB_BASE_REG		(FH_MEM_RSCSR_CHNL0 + 0x004)

/**
 * Rx write pointer (index, really!).
 * Bit fields:
 *  11-0:  Index of driver's most recent prepared-to-be-filled RBD, + 1.
 *         NOTE:  For 256-entry circular buffer, use only bits [7:0].
 */
#define FH_RSCSR_CHNL0_RBDCB_WPTR_REG		(FH_MEM_RSCSR_CHNL0 + 0x008)
#define FH_RSCSR_CHNL0_WPTR        (FH_RSCSR_CHNL0_RBDCB_WPTR_REG)


/**
 * Rx Config/Status Registers (RCSR)
 * Rx Config Reg for channel 0 (only channel used)
 *
 * Driver must initialize FH_MEM_RCSR_CHNL0_CONFIG_REG as follows for
 * normal operation (see bit fields).
 *
 * Clearing FH_MEM_RCSR_CHNL0_CONFIG_REG to 0 turns off Rx DMA.
 * Driver should poll FH_MEM_RSSR_RX_STATUS_REG	for
 * FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE (bit 24) before continuing.
 *
 * Bit fields:
 * 31-30: Rx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29-24: reserved
 * 23-20: # RBDs in circular buffer = 2^value; use "8" for 256 RBDs (normal),
 *        min "5" for 32 RBDs, max "12" for 4096 RBDs.
 * 19-18: reserved
 * 17-16: size of each receive buffer; '00' 4K (normal), '01' 8K,
 *        '10' 12K, '11' 16K.
 * 15-14: reserved
 * 13-12: IRQ destination; '00' none, '01' host driver (normal operation)
 * 11- 4: timeout for closing Rx buffer and interrupting host (units 32 usec)
 *        typical value 0x10 (about 1/2 msec)
 *  3- 0: reserved
 */
#define FH_MEM_RCSR_LOWER_BOUND      (FH_MEM_LOWER_BOUND + 0xC00)
#define FH_MEM_RCSR_UPPER_BOUND      (FH_MEM_LOWER_BOUND + 0xCC0)
#define FH_MEM_RCSR_CHNL0            (FH_MEM_RCSR_LOWER_BOUND)

#define FH_MEM_RCSR_CHNL0_CONFIG_REG	(FH_MEM_RCSR_CHNL0)

#define FH_RCSR_CHNL0_RX_CONFIG_RB_TIMEOUT_MASK   (0x00000FF0) /* bit 4-11 */
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_MASK     (0x00001000) /* bit 12 */
#define FH_RCSR_CHNL0_RX_CONFIG_SINGLE_FRAME_MASK (0x00008000) /* bit 15 */
#define FH_RCSR_CHNL0_RX_CONFIG_RB_SIZE_MASK	  (0x00030000) /* bits 16-17 */
#define FH_RCSR_CHNL0_RX_CONFIG_RBDBC_SIZE_MASK   (0x00F00000) /* bits 20-23 */
#define FH_RCSR_CHNL0_RX_CONFIG_DMA_CHNL_EN_MASK  (0xC0000000) /* bits 30-31 */

#define FH_RCSR_RX_CONFIG_RBDCB_SIZE_BITSHIFT	(20)
#define FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_BITSHIFT	(4)
#define RX_RB_TIMEOUT	(0x10)

#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_VAL         (0x00000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_PAUSE_EOF_VAL     (0x40000000)
#define FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL        (0x80000000)

#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K    (0x00000000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K    (0x00010000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_12K   (0x00020000)
#define FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_16K   (0x00030000)

#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_NO_INT_VAL       (0x00000000)
#define FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL     (0x00001000)


/**
 * Rx Shared Status Registers (RSSR)
 *
 * After stopping Rx DMA channel (writing 0 to FH_MEM_RCSR_CHNL0_CONFIG_REG),
 * driver must poll FH_MEM_RSSR_RX_STATUS_REG until Rx channel is idle.
 *
 * Bit fields:
 *  24:  1 = Channel 0 is idle
 *
 * FH_MEM_RSSR_SHARED_CTRL_REG and FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV contain
 * default values that should not be altered by the driver.
 */
#define FH_MEM_RSSR_LOWER_BOUND                	(FH_MEM_LOWER_BOUND + 0xC40)
#define FH_MEM_RSSR_UPPER_BOUND               	(FH_MEM_LOWER_BOUND + 0xD00)

#define FH_MEM_RSSR_SHARED_CTRL_REG           	(FH_MEM_RSSR_LOWER_BOUND)
#define FH_MEM_RSSR_RX_STATUS_REG	(FH_MEM_RSSR_LOWER_BOUND + 0x004)
#define FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV  (FH_MEM_RSSR_LOWER_BOUND + 0x008)

#define FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE	(0x01000000)


/**
 * Transmit DMA Channel Control/Status Registers (TCSR)
 *
 * 4965 has one configuration register for each of 8 Tx DMA/FIFO channels
 * supported in hardware (don't confuse these with the 16 Tx queues in DRAM,
 * which feed the DMA/FIFO channels); config regs are separated by 0x20 bytes.
 *
 * To use a Tx DMA channel, driver must initialize its
 * IWL_FH_TCSR_CHNL_TX_CONFIG_REG(chnl) with:
 *
 * IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE |
 * IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL
 *
 * All other bits should be 0.
 *
 * Bit fields:
 * 31-30: Tx DMA channel enable: '00' off/pause, '01' pause at end of frame,
 *        '10' operate normally
 * 29- 4: Reserved, set to "0"
 *     3: Enable internal DMA requests (1, normal operation), disable (0)
 *  2- 0: Reserved, set to "0"
 */
#define IWL_FH_TCSR_LOWER_BOUND  (FH_MEM_LOWER_BOUND + 0xD00)
#define IWL_FH_TCSR_UPPER_BOUND  (FH_MEM_LOWER_BOUND + 0xE60)

/* Find Control/Status reg for given Tx DMA/FIFO channel */
#define IWL_FH_TCSR_CHNL_TX_CONFIG_REG(_chnl) \
	(IWL_FH_TCSR_LOWER_BOUND + 0x20 * _chnl)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_DISABLE_VAL    (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CREDIT_ENABLE_VAL     (0x00000008)

#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE            (0x00000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_PAUSE_EOF        (0x40000000)
#define IWL_FH_TCSR_TX_CONFIG_REG_VAL_DMA_CHNL_ENABLE           (0x80000000)

/**
 * Tx Shared Status Registers (TSSR)
 *
 * After stopping Tx DMA channel (writing 0 to
 * IWL_FH_TCSR_CHNL_TX_CONFIG_REG(chnl)), driver must poll
 * IWL_FH_TSSR_TX_STATUS_REG until selected Tx channel is idle
 * (channel's buffers empty | no pending requests).
 *
 * Bit fields:
 * 31-24:  1 = Channel buffers empty (channel 7:0)
 * 23-16:  1 = No pending requests (channel 7:0)
 */
#define IWL_FH_TSSR_LOWER_BOUND		(FH_MEM_LOWER_BOUND + 0xEA0)
#define IWL_FH_TSSR_UPPER_BOUND		(FH_MEM_LOWER_BOUND + 0xEC0)

#define IWL_FH_TSSR_TX_STATUS_REG	(IWL_FH_TSSR_LOWER_BOUND + 0x010)

#define IWL_FH_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_chnl)	\
	((1 << (_chnl)) << 24)
#define IWL_FH_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_chnl) \
	((1 << (_chnl)) << 16)

#define IWL_FH_TSSR_TX_STATUS_REG_MSK_CHNL_IDLE(_chnl) \
	(IWL_FH_TSSR_TX_STATUS_REG_BIT_BUFS_EMPTY(_chnl) | \
	IWL_FH_TSSR_TX_STATUS_REG_BIT_NO_PEND_REQ(_chnl))


/********************* START TX SCHEDULER *************************************/

/**
 * 4965 Tx Scheduler
 *
 * The Tx Scheduler selects the next frame to be transmitted, chosing TFDs
 * (Transmit Frame Descriptors) from up to 16 circular Tx queues resident in
 * host DRAM.  It steers each frame's Tx command (which contains the frame
 * data) into one of up to 7 prioritized Tx DMA FIFO channels within the
 * device.  A queue maps to only one (selectable by driver) Tx DMA channel,
 * but one DMA channel may take input from several queues.
 *
 * Tx DMA channels have dedicated purposes.  For 4965, they are used as follows:
 *
 * 0 -- EDCA BK (background) frames, lowest priority
 * 1 -- EDCA BE (best effort) frames, normal priority
 * 2 -- EDCA VI (video) frames, higher priority
 * 3 -- EDCA VO (voice) and management frames, highest priority
 * 4 -- Commands (e.g. RXON, etc.)
 * 5 -- HCCA short frames
 * 6 -- HCCA long frames
 * 7 -- not used by driver (device-internal only)
 *
 * Driver should normally map queues 0-6 to Tx DMA/FIFO channels 0-6.
 * In addition, driver can map queues 7-15 to Tx DMA/FIFO channels 0-3 to
 * support 11n aggregation via EDCA DMA channels.
 *
 * The driver sets up each queue to work in one of two modes:
 *
 * 1)  Scheduler-Ack, in which the scheduler automatically supports a
 *     block-ack (BA) window of up to 64 TFDs.  In this mode, each queue
 *     contains TFDs for a unique combination of Recipient Address (RA)
 *     and Traffic Identifier (TID), that is, traffic of a given
 *     Quality-Of-Service (QOS) priority, destined for a single station.
 *
 *     In scheduler-ack mode, the scheduler keeps track of the Tx status of
 *     each frame within the BA window, including whether it's been transmitted,
 *     and whether it's been acknowledged by the receiving station.  The device
 *     automatically processes block-acks received from the receiving STA,
 *     and reschedules un-acked frames to be retransmitted (successful
 *     Tx completion may end up being out-of-order).
 *
 *     The driver must maintain the queue's Byte Count table in host DRAM
 *     (struct iwl4965_sched_queue_byte_cnt_tbl) for this mode.
 *     This mode does not support fragmentation.
 *
 * 2)  FIFO (a.k.a. non-Scheduler-ACK), in which each TFD is processed in order.
 *     The device may automatically retry Tx, but will retry only one frame
 *     at a time, until receiving ACK from receiving station, or reaching
 *     retry limit and giving up.
 *
 *     The command queue (#4) must use this mode!
 *     This mode does not require use of the Byte Count table in host DRAM.
 *
 * Driver controls scheduler operation via 3 means:
 * 1)  Scheduler registers
 * 2)  Shared scheduler data base in internal 4956 SRAM
 * 3)  Shared data in host DRAM
 *
 * Initialization:
 *
 * When loading, driver should allocate memory for:
 * 1)  16 TFD circular buffers, each with space for (typically) 256 TFDs.
 * 2)  16 Byte Count circular buffers in 16 KBytes contiguous memory
 *     (1024 bytes for each queue).
 *
 * After receiving "Alive" response from uCode, driver must initialize
 * the scheduler (especially for queue #4, the command queue, otherwise
 * the driver can't issue commands!):
 */

/**
 * Max Tx window size is the max number of contiguous TFDs that the scheduler
 * can keep track of at one time when creating block-ack chains of frames.
 * Note that "64" matches the number of ack bits in a block-ack packet.
 * Driver should use SCD_WIN_SIZE and SCD_FRAME_LIMIT values to initialize
 * SCD_CONTEXT_QUEUE_OFFSET(x) values.
 */
#define SCD_WIN_SIZE				64
#define SCD_FRAME_LIMIT				64

/* SCD registers are internal, must be accessed via HBUS_TARG_PRPH regs */
#define SCD_START_OFFSET		0xa02c00

/*
 * 4965 tells driver SRAM address for internal scheduler structs via this reg.
 * Value is valid only after "Alive" response from uCode.
 */
#define SCD_SRAM_BASE_ADDR           (SCD_START_OFFSET + 0x0)

/*
 * Driver may need to update queue-empty bits after changing queue's
 * write and read pointers (indexes) during (re-)initialization (i.e. when
 * scheduler is not tracking what's happening).
 * Bit fields:
 * 31-16:  Write mask -- 1: update empty bit, 0: don't change empty bit
 * 15-00:  Empty state, one for each queue -- 1: empty, 0: non-empty
 * NOTE:  This register is not used by Linux driver.
 */
#define SCD_EMPTY_BITS               (SCD_START_OFFSET + 0x4)

/*
 * Physical base address of array of byte count (BC) circular buffers (CBs).
 * Each Tx queue has a BC CB in host DRAM to support Scheduler-ACK mode.
 * This register points to BC CB for queue 0, must be on 1024-byte boundary.
 * Others are spaced by 1024 bytes.
 * Each BC CB is 2 bytes * (256 + 64) = 740 bytes, followed by 384 bytes pad.
 * (Index into a queue's BC CB) = (index into queue's TFD CB) = (SSN & 0xff).
 * Bit fields:
 * 25-00:  Byte Count CB physical address [35:10], must be 1024-byte aligned.
 */
#define SCD_DRAM_BASE_ADDR           (SCD_START_OFFSET + 0x10)

/*
 * Enables any/all Tx DMA/FIFO channels.
 * Scheduler generates requests for only the active channels.
 * Set this to 0xff to enable all 8 channels (normal usage).
 * Bit fields:
 *  7- 0:  Enable (1), disable (0), one bit for each channel 0-7
 */
#define SCD_TXFACT                   (SCD_START_OFFSET + 0x1c)

/* Mask to enable contiguous Tx DMA/FIFO channels between "lo" and "hi". */
#define SCD_TXFACT_REG_TXFIFO_MASK(lo, hi) \
       ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

/*
 * Queue (x) Write Pointers (indexes, really!), one for each Tx queue.
 * Initialized and updated by driver as new TFDs are added to queue.
 * NOTE:  If using Block Ack, index must correspond to frame's
 *        Start Sequence Number; index = (SSN & 0xff)
 * NOTE:  Alternative to HBUS_TARG_WRPTR, which is what Linux driver uses?
 */
#define SCD_QUEUE_WRPTR(x)           (SCD_START_OFFSET + 0x24 + (x) * 4)

/*
 * Queue (x) Read Pointers (indexes, really!), one for each Tx queue.
 * For FIFO mode, index indicates next frame to transmit.
 * For Scheduler-ACK mode, index indicates first frame in Tx window.
 * Initialized by driver, updated by scheduler.
 */
#define SCD_QUEUE_RDPTR(x)           (SCD_START_OFFSET + 0x64 + (x) * 4)

/*
 * Select which queues work in chain mode (1) vs. not (0).
 * Use chain mode to build chains of aggregated frames.
 * Bit fields:
 * 31-16:  Reserved
 * 15-00:  Mode, one bit for each queue -- 1: Chain mode, 0: one-at-a-time
 * NOTE:  If driver sets up queue for chain mode, it should be also set up
 *        Scheduler-ACK mode as well, via SCD_QUEUE_STATUS_BITS(x).
 */
#define SCD_QUEUECHAIN_SEL           (SCD_START_OFFSET + 0xd0)

/*
 * Select which queues interrupt driver when scheduler increments
 * a queue's read pointer (index).
 * Bit fields:
 * 31-16:  Reserved
 * 15-00:  Interrupt enable, one bit for each queue -- 1: enabled, 0: disabled
 * NOTE:  This functionality is apparently a no-op; driver relies on interrupts
 *        from Rx queue to read Tx command responses and update Tx queues.
 */
#define SCD_INTERRUPT_MASK           (SCD_START_OFFSET + 0xe4)

/*
 * Queue search status registers.  One for each queue.
 * Sets up queue mode and assigns queue to Tx DMA channel.
 * Bit fields:
 * 19-10: Write mask/enable bits for bits 0-9
 *     9: Driver should init to "0"
 *     8: Scheduler-ACK mode (1), non-Scheduler-ACK (i.e. FIFO) mode (0).
 *        Driver should init to "1" for aggregation mode, or "0" otherwise.
 *   7-6: Driver should init to "0"
 *     5: Window Size Left; indicates whether scheduler can request
 *        another TFD, based on window size, etc.  Driver should init
 *        this bit to "1" for aggregation mode, or "0" for non-agg.
 *   4-1: Tx FIFO to use (range 0-7).
 *     0: Queue is active (1), not active (0).
 * Other bits should be written as "0"
 *
 * NOTE:  If enabling Scheduler-ACK mode, chain mode should also be enabled
 *        via SCD_QUEUECHAIN_SEL.
 */
#define SCD_QUEUE_STATUS_BITS(x)     (SCD_START_OFFSET + 0x104 + (x) * 4)

/* Bit field positions */
#define SCD_QUEUE_STTS_REG_POS_ACTIVE		(0)
#define SCD_QUEUE_STTS_REG_POS_TXF		(1)
#define SCD_QUEUE_STTS_REG_POS_WSL		(5)
#define SCD_QUEUE_STTS_REG_POS_SCD_ACK		(8)

/* Write masks */
#define SCD_QUEUE_STTS_REG_POS_SCD_ACT_EN	(10)
#define SCD_QUEUE_STTS_REG_MSK			(0x0007FC00)

/**
 * 4965 internal SRAM structures for scheduler, shared with driver ...
 *
 * Driver should clear and initialize the following areas after receiving
 * "Alive" response from 4965 uCode, i.e. after initial
 * uCode load, or after a uCode load done for error recovery:
 *
 * SCD_CONTEXT_DATA_OFFSET (size 128 bytes)
 * SCD_TX_STTS_BITMAP_OFFSET (size 256 bytes)
 * SCD_TRANSLATE_TBL_OFFSET (size 32 bytes)
 *
 * Driver accesses SRAM via HBUS_TARG_MEM_* registers.
 * Driver reads base address of this scheduler area from SCD_SRAM_BASE_ADDR.
 * All OFFSET values must be added to this base address.
 */

/*
 * Queue context.  One 8-byte entry for each of 16 queues.
 *
 * Driver should clear this entire area (size 0x80) to 0 after receiving
 * "Alive" notification from uCode.  Additionally, driver should init
 * each queue's entry as follows:
 *
 * LS Dword bit fields:
 *  0-06:  Max Tx window size for Scheduler-ACK.  Driver should init to 64.
 *
 * MS Dword bit fields:
 * 16-22:  Frame limit.  Driver should init to 10 (0xa).
 *
 * Driver should init all other bits to 0.
 *
 * Init must be done after driver receives "Alive" response from 4965 uCode,
 * and when setting up queue for aggregation.
 */
#define SCD_CONTEXT_DATA_OFFSET			0x380
#define SCD_CONTEXT_QUEUE_OFFSET(x)	(SCD_CONTEXT_DATA_OFFSET + ((x) * 8))

#define SCD_QUEUE_CTX_REG1_WIN_SIZE_POS		(0)
#define SCD_QUEUE_CTX_REG1_WIN_SIZE_MSK		(0x0000007F)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_POS	(16)
#define SCD_QUEUE_CTX_REG2_FRAME_LIMIT_MSK	(0x007F0000)

/*
 * Tx Status Bitmap
 *
 * Driver should clear this entire area (size 0x100) to 0 after receiving
 * "Alive" notification from uCode.  Area is used only by device itself;
 * no other support (besides clearing) is required from driver.
 */
#define SCD_TX_STTS_BITMAP_OFFSET		0x400

/*
 * RAxTID to queue translation mapping.
 *
 * When queue is in Scheduler-ACK mode, frames placed in a that queue must be
 * for only one combination of receiver address (RA) and traffic ID (TID), i.e.
 * one QOS priority level destined for one station (for this wireless link,
 * not final destination).  The SCD_TRANSLATE_TABLE area provides 16 16-bit
 * mappings, one for each of the 16 queues.  If queue is not in Scheduler-ACK
 * mode, the device ignores the mapping value.
 *
 * Bit fields, for each 16-bit map:
 * 15-9:  Reserved, set to 0
 *  8-4:  Index into device's station table for recipient station
 *  3-0:  Traffic ID (tid), range 0-15
 *
 * Driver should clear this entire area (size 32 bytes) to 0 after receiving
 * "Alive" notification from uCode.  To update a 16-bit map value, driver
 * must read a dword-aligned value from device SRAM, replace the 16-bit map
 * value of interest, and write the dword value back into device SRAM.
 */
#define SCD_TRANSLATE_TBL_OFFSET		0x500

/* Find translation table dword to read/write for given queue */
#define SCD_TRANSLATE_TBL_OFFSET_QUEUE(x) \
	((SCD_TRANSLATE_TBL_OFFSET + ((x) * 2)) & 0xfffffffc)

#define SCD_TXFIFO_POS_TID			(0)
#define SCD_TXFIFO_POS_RA			(4)
#define SCD_QUEUE_RA_TID_MAP_RATID_MSK		(0x01FF)

/*********************** END TX SCHEDULER *************************************/

static inline u8 iwl4965_hw_get_rate(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFF;
}
static inline u16 iwl4965_hw_get_rate_n_flags(__le32 rate_n_flags)
{
	return le32_to_cpu(rate_n_flags) & 0xFFFF;
}
static inline __le32 iwl4965_hw_set_rate_n_flags(u8 rate, u16 flags)
{
	return cpu_to_le32(flags|(u16)rate);
}


/**
 * Tx/Rx Queues
 *
 * Most communication between driver and 4965 is via queues of data buffers.
 * For example, all commands that the driver issues to device's embedded
 * controller (uCode) are via the command queue (one of the Tx queues).  All
 * uCode command responses/replies/notifications, including Rx frames, are
 * conveyed from uCode to driver via the Rx queue.
 *
 * Most support for these queues, including handshake support, resides in
 * structures in host DRAM, shared between the driver and the device.  When
 * allocating this memory, the driver must make sure that data written by
 * the host CPU updates DRAM immediately (and does not get "stuck" in CPU's
 * cache memory), so DRAM and cache are consistent, and the device can
 * immediately see changes made by the driver.
 *
 * 4965 supports up to 16 DRAM-based Tx queues, and services these queues via
 * up to 7 DMA channels (FIFOs).  Each Tx queue is supported by a circular array
 * in DRAM containing 256 Transmit Frame Descriptors (TFDs).
 */
#define IWL4965_MAX_WIN_SIZE              64
#define IWL4965_QUEUE_SIZE               256
#define IWL4965_NUM_FIFOS                  7
#define IWL_MAX_NUM_QUEUES                16


/**
 * struct iwl4965_tfd_frame_data
 *
 * Describes up to 2 buffers containing (contiguous) portions of a Tx frame.
 * Each buffer must be on dword boundary.
 * Up to 10 iwl_tfd_frame_data structures, describing up to 20 buffers,
 * may be filled within a TFD (iwl_tfd_frame).
 *
 * Bit fields in tb1_addr:
 * 31- 0: Tx buffer 1 address bits [31:0]
 *
 * Bit fields in val1:
 * 31-16: Tx buffer 2 address bits [15:0]
 * 15- 4: Tx buffer 1 length (bytes)
 *  3- 0: Tx buffer 1 address bits [32:32]
 *
 * Bit fields in val2:
 * 31-20: Tx buffer 2 length (bytes)
 * 19- 0: Tx buffer 2 address bits [35:16]
 */
struct iwl4965_tfd_frame_data {
	__le32 tb1_addr;

	__le32 val1;
	/* __le32 ptb1_32_35:4; */
#define IWL_tb1_addr_hi_POS 0
#define IWL_tb1_addr_hi_LEN 4
#define IWL_tb1_addr_hi_SYM val1
	/* __le32 tb_len1:12; */
#define IWL_tb1_len_POS 4
#define IWL_tb1_len_LEN 12
#define IWL_tb1_len_SYM val1
	/* __le32 ptb2_0_15:16; */
#define IWL_tb2_addr_lo16_POS 16
#define IWL_tb2_addr_lo16_LEN 16
#define IWL_tb2_addr_lo16_SYM val1

	__le32 val2;
	/* __le32 ptb2_16_35:20; */
#define IWL_tb2_addr_hi20_POS 0
#define IWL_tb2_addr_hi20_LEN 20
#define IWL_tb2_addr_hi20_SYM val2
	/* __le32 tb_len2:12; */
#define IWL_tb2_len_POS 20
#define IWL_tb2_len_LEN 12
#define IWL_tb2_len_SYM val2
} __attribute__ ((packed));


/**
 * struct iwl4965_tfd_frame
 *
 * Transmit Frame Descriptor (TFD)
 *
 * 4965 supports up to 16 Tx queues resident in host DRAM.
 * Each Tx queue uses a circular buffer of 256 TFDs stored in host DRAM.
 * Both driver and device share these circular buffers, each of which must be
 * contiguous 256 TFDs x 128 bytes-per-TFD = 32 KBytes for 4965.
 *
 * Driver must indicate the physical address of the base of each
 * circular buffer via the 4965's FH_MEM_CBBC_QUEUE registers.
 *
 * Each TFD contains pointer/size information for up to 20 data buffers
 * in host DRAM.  These buffers collectively contain the (one) frame described
 * by the TFD.  Each buffer must be a single contiguous block of memory within
 * itself, but buffers may be scattered in host DRAM.  Each buffer has max size
 * of (4K - 4).  The 4965 concatenates all of a TFD's buffers into a single
 * Tx frame, up to 8 KBytes in size.
 *
 * Bit fields in the control dword (val0):
 * 31-30: # dwords (0-3) of padding required at end of frame for 16-byte bound
 *    29: reserved
 * 28-24: # Transmit Buffer Descriptors in TFD
 * 23- 0: reserved
 *
 * A maximum of 255 (not 256!) TFDs may be on a queue waiting for Tx.
 */
struct iwl4965_tfd_frame {
	__le32 val0;
	/* __le32 rsvd1:24; */
	/* __le32 num_tbs:5; */
#define IWL_num_tbs_POS 24
#define IWL_num_tbs_LEN 5
#define IWL_num_tbs_SYM val0
	/* __le32 rsvd2:1; */
	/* __le32 padding:2; */
	struct iwl4965_tfd_frame_data pa[10];
	__le32 reserved;
} __attribute__ ((packed));


/**
 * struct iwl4965_queue_byte_cnt_entry
 *
 * Byte Count Table Entry
 *
 * Bit fields:
 * 15-12: reserved
 * 11- 0: total to-be-transmitted byte count of frame (does not include command)
 */
struct iwl4965_queue_byte_cnt_entry {
	__le16 val;
	/* __le16 byte_cnt:12; */
#define IWL_byte_cnt_POS 0
#define IWL_byte_cnt_LEN 12
#define IWL_byte_cnt_SYM val
	/* __le16 rsvd:4; */
} __attribute__ ((packed));


/**
 * struct iwl4965_sched_queue_byte_cnt_tbl
 *
 * Byte Count table
 *
 * Each Tx queue uses a byte-count table containing 320 entries:
 * one 16-bit entry for each of 256 TFDs, plus an additional 64 entries that
 * duplicate the first 64 entries (to avoid wrap-around within a Tx window;
 * max Tx window is 64 TFDs).
 *
 * When driver sets up a new TFD, it must also enter the total byte count
 * of the frame to be transmitted into the corresponding entry in the byte
 * count table for the chosen Tx queue.  If the TFD index is 0-63, the driver
 * must duplicate the byte count entry in corresponding index 256-319.
 *
 * "dont_care" padding puts each byte count table on a 1024-byte boundary;
 * 4965 assumes tables are separated by 1024 bytes.
 */
struct iwl4965_sched_queue_byte_cnt_tbl {
	struct iwl4965_queue_byte_cnt_entry tfd_offset[IWL4965_QUEUE_SIZE +
						       IWL4965_MAX_WIN_SIZE];
	u8 dont_care[1024 -
		     (IWL4965_QUEUE_SIZE + IWL4965_MAX_WIN_SIZE) *
		     sizeof(__le16)];
} __attribute__ ((packed));


/**
 * struct iwl4965_shared - handshake area for Tx and Rx
 *
 * For convenience in allocating memory, this structure combines 2 areas of
 * DRAM which must be shared between driver and 4965.  These do not need to
 * be combined, if better allocation would result from keeping them separate:
 *
 * 1)  The Tx byte count tables occupy 1024 bytes each (16 KBytes total for
 *     16 queues).  Driver uses SCD_DRAM_BASE_ADDR to tell 4965 where to find
 *     the first of these tables.  4965 assumes tables are 1024 bytes apart.
 *
 * 2)  The Rx status (val0 and val1) occupies only 8 bytes.  Driver uses
 *     FH_RSCSR_CHNL0_STTS_WPTR_REG to tell 4965 where to find this area.
 *     Driver reads val0 to determine the latest Receive Buffer Descriptor (RBD)
 *     that has been filled by the 4965.
 *
 * Bit fields val0:
 * 31-12:  Not used
 * 11- 0:  Index of last filled Rx buffer descriptor (4965 writes, driver reads)
 *
 * Bit fields val1:
 * 31- 0:  Not used
 */
struct iwl4965_shared {
	struct iwl4965_sched_queue_byte_cnt_tbl
	 queues_byte_cnt_tbls[IWL_MAX_NUM_QUEUES];
	__le32 val0;

	/* __le32 rb_closed_stts_rb_num:12; */
#define IWL_rb_closed_stts_rb_num_POS 0
#define IWL_rb_closed_stts_rb_num_LEN 12
#define IWL_rb_closed_stts_rb_num_SYM val0
	/* __le32 rsrv1:4; */
	/* __le32 rb_closed_stts_rx_frame_num:12; */
#define IWL_rb_closed_stts_rx_frame_num_POS 16
#define IWL_rb_closed_stts_rx_frame_num_LEN 12
#define IWL_rb_closed_stts_rx_frame_num_SYM val0
	/* __le32 rsrv2:4; */

	__le32 val1;
	/* __le32 frame_finished_stts_rb_num:12; */
#define IWL_frame_finished_stts_rb_num_POS 0
#define IWL_frame_finished_stts_rb_num_LEN 12
#define IWL_frame_finished_stts_rb_num_SYM val1
	/* __le32 rsrv3:4; */
	/* __le32 frame_finished_stts_rx_frame_num:12; */
#define IWL_frame_finished_stts_rx_frame_num_POS 16
#define IWL_frame_finished_stts_rx_frame_num_LEN 12
#define IWL_frame_finished_stts_rx_frame_num_SYM val1
	/* __le32 rsrv4:4; */

	__le32 padding1;  /* so that allocation will be aligned to 16B */
	__le32 padding2;
} __attribute__ ((packed));

#endif /* __iwl4965_4965_hw_h__ */
