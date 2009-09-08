/*
 * Atheros AR9170 driver
 *
 * EEPROM layout
 *
 * Copyright 2008, Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __AR9170_EEPROM_H
#define __AR9170_EEPROM_H

#define AR5416_MAX_CHAINS		2
#define AR5416_MODAL_SPURS		5

struct ar9170_eeprom_modal {
	__le32	antCtrlChain[AR5416_MAX_CHAINS];
	__le32	antCtrlCommon;
	s8	antennaGainCh[AR5416_MAX_CHAINS];
	u8	switchSettling;
	u8	txRxAttenCh[AR5416_MAX_CHAINS];
	u8	rxTxMarginCh[AR5416_MAX_CHAINS];
	s8	adcDesiredSize;
	s8	pgaDesiredSize;
	u8	xlnaGainCh[AR5416_MAX_CHAINS];
	u8	txEndToXpaOff;
	u8	txEndToRxOn;
	u8	txFrameToXpaOn;
	u8	thresh62;
	s8	noiseFloorThreshCh[AR5416_MAX_CHAINS];
	u8	xpdGain;
	u8	xpd;
	s8	iqCalICh[AR5416_MAX_CHAINS];
	s8	iqCalQCh[AR5416_MAX_CHAINS];
	u8	pdGainOverlap;
	u8	ob;
	u8	db;
	u8	xpaBiasLvl;
	u8	pwrDecreaseFor2Chain;
	u8	pwrDecreaseFor3Chain;
	u8	txFrameToDataStart;
	u8	txFrameToPaOn;
	u8	ht40PowerIncForPdadc;
	u8	bswAtten[AR5416_MAX_CHAINS];
	u8	bswMargin[AR5416_MAX_CHAINS];
	u8	swSettleHt40;
	u8	reserved[22];
	struct spur_channel {
		__le16 spurChan;
		u8	spurRangeLow;
		u8	spurRangeHigh;
	} __packed spur_channels[AR5416_MODAL_SPURS];
} __packed;

#define AR5416_NUM_PD_GAINS		4
#define AR5416_PD_GAIN_ICEPTS		5

struct ar9170_calibration_data_per_freq {
	u8	pwr_pdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
	u8	vpd_pdg[AR5416_NUM_PD_GAINS][AR5416_PD_GAIN_ICEPTS];
} __packed;

#define AR5416_NUM_5G_CAL_PIERS		8
#define AR5416_NUM_2G_CAL_PIERS		4

#define AR5416_NUM_5G_TARGET_PWRS	8
#define AR5416_NUM_2G_CCK_TARGET_PWRS	3
#define AR5416_NUM_2G_OFDM_TARGET_PWRS	4
#define AR5416_MAX_NUM_TGT_PWRS		8

struct ar9170_calibration_target_power_legacy {
	u8	freq;
	u8	power[4];
} __packed;

struct ar9170_calibration_target_power_ht {
	u8	freq;
	u8	power[8];
} __packed;

#define AR5416_NUM_CTLS			24

struct ar9170_calctl_edges {
	u8	channel;
#define AR9170_CALCTL_EDGE_FLAGS	0xC0
	u8	power_flags;
} __packed;

#define AR5416_NUM_BAND_EDGES		8

struct ar9170_calctl_data {
	struct ar9170_calctl_edges
		control_edges[AR5416_MAX_CHAINS][AR5416_NUM_BAND_EDGES];
} __packed;


struct ar9170_eeprom {
	__le16	length;
	__le16	checksum;
	__le16	version;
	u8	operating_flags;
#define AR9170_OPFLAG_5GHZ 		1
#define AR9170_OPFLAG_2GHZ 		2
	u8	misc;
	__le16	reg_domain[2];
	u8	mac_address[6];
	u8	rx_mask;
	u8	tx_mask;
	__le16	rf_silent;
	__le16	bluetooth_options;
	__le16	device_capabilities;
	__le32	build_number;
	u8	deviceType;
	u8	reserved[33];

	u8	customer_data[64];

	struct ar9170_eeprom_modal
		modal_header[2];

	u8	cal_freq_pier_5G[AR5416_NUM_5G_CAL_PIERS];
	u8	cal_freq_pier_2G[AR5416_NUM_2G_CAL_PIERS];

	struct ar9170_calibration_data_per_freq
		cal_pier_data_5G[AR5416_MAX_CHAINS][AR5416_NUM_5G_CAL_PIERS],
		cal_pier_data_2G[AR5416_MAX_CHAINS][AR5416_NUM_2G_CAL_PIERS];

	/* power calibration data */
	struct ar9170_calibration_target_power_legacy
		cal_tgt_pwr_5G[AR5416_NUM_5G_TARGET_PWRS];
	struct ar9170_calibration_target_power_ht
		cal_tgt_pwr_5G_ht20[AR5416_NUM_5G_TARGET_PWRS],
		cal_tgt_pwr_5G_ht40[AR5416_NUM_5G_TARGET_PWRS];

	struct ar9170_calibration_target_power_legacy
		cal_tgt_pwr_2G_cck[AR5416_NUM_2G_CCK_TARGET_PWRS],
		cal_tgt_pwr_2G_ofdm[AR5416_NUM_2G_OFDM_TARGET_PWRS];
	struct ar9170_calibration_target_power_ht
		cal_tgt_pwr_2G_ht20[AR5416_NUM_2G_OFDM_TARGET_PWRS],
		cal_tgt_pwr_2G_ht40[AR5416_NUM_2G_OFDM_TARGET_PWRS];

	/* conformance testing limits */
	u8	ctl_index[AR5416_NUM_CTLS];
	struct ar9170_calctl_data
		ctl_data[AR5416_NUM_CTLS];

	u8	pad;
	__le16	subsystem_id;
} __packed;

#endif /* __AR9170_EEPROM_H */
