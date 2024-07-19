/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef _RTW_HT_H_
#define _RTW_HT_H_


struct ht_priv {
	u8 ht_option;
	u8 ampdu_enable;/* for enable Tx A-MPDU */
	u8 tx_amsdu_enable;/* for enable Tx A-MSDU */
	u8 bss_coexist;/* for 20/40 Bss coexist */

	/* u8 baddbareq_issued[16]; */
	u32 tx_amsdu_maxlen; /*  1: 8k, 0:4k ; default:8k, for tx */
	u32 rx_ampdu_maxlen; /* for rx reordering ctrl win_sz, updated when join_callback. */

	u8 rx_ampdu_min_spacing;

	u8 ch_offset;/* PRIME_CHNL_OFFSET */
	u8 sgi_20m;
	u8 sgi_40m;

	/* for processing Tx A-MPDU */
	u8 agg_enable_bitmap;
	/* u8 ADDBA_retry_count; */
	u8 candidate_tid_bitmap;

	u8 ldpc_cap;
	u8 stbc_cap;
	u8 beamform_cap;

	struct ieee80211_ht_cap ht_cap;

};

enum {
	HT_AGG_SIZE_8K = 0,
	HT_AGG_SIZE_16K = 1,
	HT_AGG_SIZE_32K = 2,
	HT_AGG_SIZE_64K = 3,
};

enum {
	RT_HT_CAP_USE_TURBO_AGGR = 0x01,
	RT_HT_CAP_USE_LONG_PREAMBLE = 0x02,
	RT_HT_CAP_USE_AMPDU = 0x04,
	RT_HT_CAP_USE_WOW = 0x8,
	RT_HT_CAP_USE_SOFTAP = 0x10,
	RT_HT_CAP_USE_92SE = 0x20,
	RT_HT_CAP_USE_88C_92C = 0x40,
	RT_HT_CAP_USE_AP_CLIENT_MODE = 0x80,	/*  AP team request to reserve this bit, by Emily */
};

enum {
	RT_HT_CAP_USE_VIDEO_CLIENT = 0x01,
	RT_HT_CAP_USE_JAGUAR_BCUT = 0x02,
	RT_HT_CAP_USE_JAGUAR_CCUT = 0x04,
};

#define	LDPC_HT_ENABLE_RX			BIT0
#define	LDPC_HT_ENABLE_TX			BIT1
#define	LDPC_HT_CAP_TX				BIT3

#define	STBC_HT_ENABLE_RX			BIT0
#define	STBC_HT_ENABLE_TX			BIT1
#define	STBC_HT_CAP_TX				BIT3

#define	BEAMFORMING_HT_BEAMFORMER_ENABLE	BIT0	/*  Declare our NIC supports beamformer */
#define	BEAMFORMING_HT_BEAMFORMEE_ENABLE	BIT1	/*  Declare our NIC supports beamformee */

/*  20/40 BSS Coexist */
#define SET_EXT_CAPABILITY_ELE_BSS_COEXIST(_pEleStart, _val)			SET_BITS_TO_LE_1BYTE((_pEleStart), 0, 1, _val)

#define GET_HT_CAPABILITY_ELE_LDPC_CAP(_pEleStart)				LE_BITS_TO_1BYTE(_pEleStart, 0, 1)
#define GET_HT_CAPABILITY_ELE_TX_STBC(_pEleStart)					LE_BITS_TO_1BYTE(_pEleStart, 7, 1)

#define GET_HT_CAPABILITY_ELE_RX_STBC(_pEleStart)					LE_BITS_TO_1BYTE((_pEleStart)+1, 0, 2)

#endif	/* _RTL871X_HT_H_ */
