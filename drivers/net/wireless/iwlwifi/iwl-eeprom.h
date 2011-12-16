/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * Copyright(c) 2005 - 2011 Intel Corporation. All rights reserved.
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

#include <net/mac80211.h>

struct iwl_priv;
struct iwl_shared;

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
#define EEPROM_SKU_CAP_BAND_24GHZ			(1 << 4)
#define EEPROM_SKU_CAP_BAND_52GHZ			(1 << 5)
#define EEPROM_SKU_CAP_11N_ENABLE	                (1 << 6)
#define EEPROM_SKU_CAP_AMT_ENABLE			(1 << 7)
#define EEPROM_SKU_CAP_IPAN_ENABLE	                (1 << 8)

/* *regulatory* channel data format in eeprom, one for each channel.
 * There are separate entries for HT40 (40 MHz) vs. normal (20 MHz) channels. */
struct iwl_eeprom_channel {
	u8 flags;		/* EEPROM_CHANNEL_* flags copied from EEPROM */
	s8 max_power_avg;	/* max power (dBm) on this chnl, limit 31 */
} __packed;

enum iwl_eeprom_enhanced_txpwr_flags {
	IWL_EEPROM_ENH_TXP_FL_VALID		= BIT(0),
	IWL_EEPROM_ENH_TXP_FL_BAND_52G		= BIT(1),
	IWL_EEPROM_ENH_TXP_FL_OFDM		= BIT(2),
	IWL_EEPROM_ENH_TXP_FL_40MHZ		= BIT(3),
	IWL_EEPROM_ENH_TXP_FL_HT_AP		= BIT(4),
	IWL_EEPROM_ENH_TXP_FL_RES1		= BIT(5),
	IWL_EEPROM_ENH_TXP_FL_RES2		= BIT(6),
	IWL_EEPROM_ENH_TXP_FL_COMMON_TYPE	= BIT(7),
};

/**
 * iwl_eeprom_enhanced_txpwr structure
 *    This structure presents the enhanced regulatory tx power limit layout
 *    in eeprom image
 *    Enhanced regulatory tx power portion of eeprom image can be broken down
 *    into individual structures; each one is 8 bytes in size and contain the
 *    following information
 * @flags: entry flags
 * @channel: channel number
 * @chain_a_max_pwr: chain a max power in 1/2 dBm
 * @chain_b_max_pwr: chain b max power in 1/2 dBm
 * @chain_c_max_pwr: chain c max power in 1/2 dBm
 * @delta_20_in_40: 20-in-40 deltas (hi/lo)
 * @mimo2_max_pwr: mimo2 max power in 1/2 dBm
 * @mimo3_max_pwr: mimo3 max power in 1/2 dBm
 *
 */
struct iwl_eeprom_enhanced_txpwr {
	u8 flags;
	u8 channel;
	s8 chain_a_max;
	s8 chain_b_max;
	s8 chain_c_max;
	u8 delta_20_in_40;
	s8 mimo2_max;
	s8 mimo3_max;
} __packed;

/* calibration */
struct iwl_eeprom_calib_hdr {
	u8 version;
	u8 pa_type;
	__le16 voltage;
} __packed;

#define EEPROM_CALIB_ALL	(INDIRECT_ADDRESS | INDIRECT_CALIBRATION)
#define EEPROM_XTAL		((2*0x128) | EEPROM_CALIB_ALL)

/* temperature */
#define EEPROM_KELVIN_TEMPERATURE	((2*0x12A) | EEPROM_CALIB_ALL)
#define EEPROM_RAW_TEMPERATURE		((2*0x12B) | EEPROM_CALIB_ALL)


/* agn links */
#define EEPROM_LINK_HOST             (2*0x64)
#define EEPROM_LINK_GENERAL          (2*0x65)
#define EEPROM_LINK_REGULATORY       (2*0x66)
#define EEPROM_LINK_CALIBRATION      (2*0x67)
#define EEPROM_LINK_PROCESS_ADJST    (2*0x68)
#define EEPROM_LINK_OTHERS           (2*0x69)
#define EEPROM_LINK_TXP_LIMIT        (2*0x6a)
#define EEPROM_LINK_TXP_LIMIT_SIZE   (2*0x6b)

/* agn regulatory - indirect access */
#define EEPROM_REG_BAND_1_CHANNELS       ((0x08)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 28 bytes */
#define EEPROM_REG_BAND_2_CHANNELS       ((0x26)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 26 bytes */
#define EEPROM_REG_BAND_3_CHANNELS       ((0x42)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 24 bytes */
#define EEPROM_REG_BAND_4_CHANNELS       ((0x5C)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 22 bytes */
#define EEPROM_REG_BAND_5_CHANNELS       ((0x74)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 12 bytes */
#define EEPROM_REG_BAND_24_HT40_CHANNELS  ((0x82)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 14  bytes */
#define EEPROM_REG_BAND_52_HT40_CHANNELS  ((0x92)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 22  bytes */

/* 6000 regulatory - indirect access */
#define EEPROM_6000_REG_BAND_24_HT40_CHANNELS  ((0x80)\
		| INDIRECT_ADDRESS | INDIRECT_REGULATORY)   /* 14  bytes */

/* 5000 Specific */
#define EEPROM_5000_TX_POWER_VERSION    (4)
#define EEPROM_5000_EEPROM_VERSION	(0x11A)

/* 5050 Specific */
#define EEPROM_5050_TX_POWER_VERSION    (4)
#define EEPROM_5050_EEPROM_VERSION	(0x21E)

/* 1000 Specific */
#define EEPROM_1000_TX_POWER_VERSION    (4)
#define EEPROM_1000_EEPROM_VERSION	(0x15C)

/* 6x00 Specific */
#define EEPROM_6000_TX_POWER_VERSION    (4)
#define EEPROM_6000_EEPROM_VERSION	(0x423)

/* 6x50 Specific */
#define EEPROM_6050_TX_POWER_VERSION    (4)
#define EEPROM_6050_EEPROM_VERSION	(0x532)

/* 6150 Specific */
#define EEPROM_6150_TX_POWER_VERSION    (6)
#define EEPROM_6150_EEPROM_VERSION	(0x553)

/* 6x05 Specific */
#define EEPROM_6005_TX_POWER_VERSION    (6)
#define EEPROM_6005_EEPROM_VERSION	(0x709)

/* 6x30 Specific */
#define EEPROM_6030_TX_POWER_VERSION    (6)
#define EEPROM_6030_EEPROM_VERSION	(0x709)

/* 2x00 Specific */
#define EEPROM_2000_TX_POWER_VERSION    (6)
#define EEPROM_2000_EEPROM_VERSION	(0x805)

/* 6x35 Specific */
#define EEPROM_6035_TX_POWER_VERSION    (6)
#define EEPROM_6035_EEPROM_VERSION	(0x753)


/* OTP */
/* lower blocks contain EEPROM image and calibration data */
#define OTP_LOW_IMAGE_SIZE		(2 * 512 * sizeof(u16)) /* 2 KB */
/* high blocks contain PAPD data */
#define OTP_HIGH_IMAGE_SIZE_6x00        (6 * 512 * sizeof(u16)) /* 6 KB */
#define OTP_HIGH_IMAGE_SIZE_1000        (0x200 * sizeof(u16)) /* 1024 bytes */
#define OTP_MAX_LL_ITEMS_1000		(3)	/* OTP blocks for 1000 */
#define OTP_MAX_LL_ITEMS_6x00		(4)	/* OTP blocks for 6x00 */
#define OTP_MAX_LL_ITEMS_6x50		(7)	/* OTP blocks for 6x50 */
#define OTP_MAX_LL_ITEMS_2x00		(4)	/* OTP blocks for 2x00 */

/* 2.4 GHz */
extern const u8 iwl_eeprom_band_1[14];

#define ADDRESS_MSK                 0x0000FFFF
#define INDIRECT_TYPE_MSK           0x000F0000
#define INDIRECT_HOST               0x00010000
#define INDIRECT_GENERAL            0x00020000
#define INDIRECT_REGULATORY         0x00030000
#define INDIRECT_CALIBRATION        0x00040000
#define INDIRECT_PROCESS_ADJST      0x00050000
#define INDIRECT_OTHERS             0x00060000
#define INDIRECT_TXP_LIMIT          0x00070000
#define INDIRECT_TXP_LIMIT_SIZE     0x00080000
#define INDIRECT_ADDRESS            0x00100000

/* General */
#define EEPROM_DEVICE_ID                    (2*0x08)	/* 2 bytes */
#define EEPROM_SUBSYSTEM_ID		    (2*0x0A)	/* 2 bytes */
#define EEPROM_MAC_ADDRESS                  (2*0x15)	/* 6  bytes */
#define EEPROM_BOARD_REVISION               (2*0x35)	/* 2  bytes */
#define EEPROM_BOARD_PBA_NUMBER             (2*0x3B+1)	/* 9  bytes */
#define EEPROM_VERSION                      (2*0x44)	/* 2  bytes */
#define EEPROM_SKU_CAP                      (2*0x45)	/* 2  bytes */
#define EEPROM_OEM_MODE                     (2*0x46)	/* 2  bytes */
#define EEPROM_RADIO_CONFIG                 (2*0x48)	/* 2  bytes */
#define EEPROM_NUM_MAC_ADDRESS              (2*0x4C)	/* 2  bytes */

/* The following masks are to be applied on EEPROM_RADIO_CONFIG */
#define EEPROM_RF_CFG_TYPE_MSK(x)   (x & 0x3)         /* bits 0-1   */
#define EEPROM_RF_CFG_STEP_MSK(x)   ((x >> 2)  & 0x3) /* bits 2-3   */
#define EEPROM_RF_CFG_DASH_MSK(x)   ((x >> 4)  & 0x3) /* bits 4-5   */
#define EEPROM_RF_CFG_PNUM_MSK(x)   ((x >> 6)  & 0x3) /* bits 6-7   */
#define EEPROM_RF_CFG_TX_ANT_MSK(x) ((x >> 8)  & 0xF) /* bits 8-11  */
#define EEPROM_RF_CFG_RX_ANT_MSK(x) ((x >> 12) & 0xF) /* bits 12-15 */

#define EEPROM_RF_CONFIG_TYPE_MAX	0x3

#define EEPROM_REGULATORY_BAND_NO_HT40			(0)

struct iwl_eeprom_ops {
	const u32 regulatory_bands[7];
	void (*update_enhanced_txpower) (struct iwl_priv *priv);
};


int iwl_eeprom_init(struct iwl_priv *priv, u32 hw_rev);
void iwl_eeprom_free(struct iwl_shared *shrd);
int  iwl_eeprom_check_version(struct iwl_priv *priv);
int  iwl_eeprom_check_sku(struct iwl_priv *priv);
const u8 *iwl_eeprom_query_addr(const struct iwl_shared *shrd, size_t offset);
u16 iwl_eeprom_query16(const struct iwl_shared *shrd, size_t offset);
int iwl_init_channel_map(struct iwl_priv *priv);
void iwl_free_channel_map(struct iwl_priv *priv);
const struct iwl_channel_info *iwl_get_channel_info(
		const struct iwl_priv *priv,
		enum ieee80211_band band, u16 channel);
void iwl_rf_config(struct iwl_priv *priv);

#endif  /* __iwl_eeprom_h__ */
