/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8822B_DEF_H__
#define __RTL8822B_DEF_H__

#define RX_DESC_NUM_8822BE	512

#define HAL_PRIME_CHNL_OFFSET_DONT_CARE	0
#define HAL_PRIME_CHNL_OFFSET_LOWER	1
#define HAL_PRIME_CHNL_OFFSET_UPPER	2

#define RX_MPDU_QUEUE	0

#define IS_HT_RATE(_rate) (_rate >= DESC_RATEMCS0)
#define IS_CCK_RATE(_rate) (_rate >= DESC_RATE1M && _rate <= DESC_RATE11M)
#define IS_OFDM_RATE(_rate) (_rate >= DESC_RATE6M && _rate <= DESC_RATE54M)
#define IS_1T_RATE(_rate)                                                      \
	((_rate >= DESC_RATE1M && _rate <= DESC_RATEMCS7) ||                   \
	 (_rate >= DESC_RATEVHT1SS_MCS0 && _rate <= DESC_RATEVHT1SS_MCS9))
#define IS_2T_RATE(_rate)                                                      \
	((_rate >= DESC_RATEMCS8 && _rate <= DESC_RATEMCS15) ||                \
	 (_rate >= DESC_RATEVHT2SS_MCS0 && _rate <= DESC_RATEVHT2SS_MCS9))

#define IS_1T_RATESEC(_rs)                                                     \
	((_rs == CCK) || (_rs == OFDM) || (_rs == HT_MCS0_MCS7) ||             \
	 (_rs == VHT_1SSMCS0_1SSMCS9))
#define IS_2T_RATESEC(_rs)                                                     \
	((_rs == HT_MCS8_MCS15) || (_rs == VHT_2SSMCS0_2SSMCS9))

enum rx_packet_type {
	NORMAL_RX,
	C2H_PACKET,
};

enum rtl_desc_qsel {
	QSLT_BK	= 0x2,
	QSLT_BE	= 0x0,
	QSLT_VI	= 0x5,
	QSLT_VO	= 0x7,
	QSLT_BEACON	= 0x10,
	QSLT_HIGH	= 0x11,
	QSLT_MGNT	= 0x12,
	QSLT_CMD	= 0x13,
};

enum vht_data_sc {
	VHT_DATA_SC_DONOT_CARE	= 0,
	VHT_DATA_SC_20_UPPER_OF_80MHZ	= 1,
	VHT_DATA_SC_20_LOWER_OF_80MHZ	= 2,
	VHT_DATA_SC_20_UPPERST_OF_80MHZ	= 3,
	VHT_DATA_SC_20_LOWEST_OF_80MHZ	= 4,
	VHT_DATA_SC_20_RECV1	= 5,
	VHT_DATA_SC_20_RECV2	= 6,
	VHT_DATA_SC_20_RECV3	= 7,
	VHT_DATA_SC_20_RECV4	= 8,
	VHT_DATA_SC_40_UPPER_OF_80MHZ	= 9,
	VHT_DATA_SC_40_LOWER_OF_80MHZ	= 10,
};
#endif
