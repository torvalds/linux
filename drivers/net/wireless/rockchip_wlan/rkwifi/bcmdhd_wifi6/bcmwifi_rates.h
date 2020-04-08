/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Indices for 802.11 a/b/g/n/ac 1-3 chain symmetric transmit rates
 *
 * Copyright (C) 1999-2019, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: bcmwifi_rates.h 697006 2017-05-01 19:13:40Z $
 */

#ifndef _bcmwifi_rates_h_
#define _bcmwifi_rates_h_

#include <typedefs.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define WL_RATESET_SZ_DSSS		4
#define WL_RATESET_SZ_OFDM		8
#define WL_RATESET_SZ_VHT_MCS		10
#define WL_RATESET_SZ_VHT_MCS_P		12	/* 10 VHT rates + 2 proprietary rates */
#define WL_RATESET_SZ_HE_MCS		12	/* 12 HE rates (mcs 0-11) */

#define WL_RATESET_SZ_HT_MCS	8

#define WL_RATESET_SZ_HT_IOCTL	8	/* MAC histogram, compatibility with wl utility */

#define WL_TX_CHAINS_MAX		4

#define WL_RATE_DISABLED		(-128) /* Power value corresponding to unsupported rate */

/* Transmit channel bandwidths */
typedef enum wl_tx_bw {
	WL_TX_BW_20,
	WL_TX_BW_40,
	WL_TX_BW_80,
	WL_TX_BW_20IN40,
	WL_TX_BW_20IN80,
	WL_TX_BW_40IN80,
	WL_TX_BW_160,
	WL_TX_BW_20IN160,
	WL_TX_BW_40IN160,
	WL_TX_BW_80IN160,
	WL_TX_BW_ALL,
	WL_TX_BW_8080,
	WL_TX_BW_8080CHAN2,
	WL_TX_BW_20IN8080,
	WL_TX_BW_40IN8080,
	WL_TX_BW_80IN8080,
	WL_TX_BW_2P5,
	WL_TX_BW_5,
	WL_TX_BW_10
} wl_tx_bw_t;

/*
 * Transmit modes.
 * Not all modes are listed here, only those required for disambiguation. e.g. SPEXP is not listed
 */
typedef enum wl_tx_mode {
	WL_TX_MODE_NONE,
	WL_TX_MODE_STBC,
	WL_TX_MODE_CDD,
	WL_TX_MODE_TXBF,
	WL_NUM_TX_MODES
} wl_tx_mode_t;

/* Number of transmit chains */
typedef enum wl_tx_chains {
	WL_TX_CHAINS_1 = 1,
	WL_TX_CHAINS_2,
	WL_TX_CHAINS_3,
	WL_TX_CHAINS_4
} wl_tx_chains_t;

/* Number of transmit streams */
typedef enum wl_tx_nss {
	WL_TX_NSS_1 = 1,
	WL_TX_NSS_2,
	WL_TX_NSS_3,
	WL_TX_NSS_4
} wl_tx_nss_t;

/* This enum maps each rate to a CLM index */

typedef enum clm_rates {
	/************
	* 1 chain  *
	************
	*/

	/* 1 Stream */
	WL_RATE_1X1_DSSS_1            = 0,
	WL_RATE_1X1_DSSS_2            = 1,
	WL_RATE_1X1_DSSS_5_5          = 2,
	WL_RATE_1X1_DSSS_11           = 3,

	WL_RATE_1X1_OFDM_6            = 4,
	WL_RATE_1X1_OFDM_9            = 5,
	WL_RATE_1X1_OFDM_12           = 6,
	WL_RATE_1X1_OFDM_18           = 7,
	WL_RATE_1X1_OFDM_24           = 8,
	WL_RATE_1X1_OFDM_36           = 9,
	WL_RATE_1X1_OFDM_48           = 10,
	WL_RATE_1X1_OFDM_54           = 11,

	WL_RATE_1X1_MCS0              = 12,
	WL_RATE_1X1_MCS1              = 13,
	WL_RATE_1X1_MCS2              = 14,
	WL_RATE_1X1_MCS3              = 15,
	WL_RATE_1X1_MCS4              = 16,
	WL_RATE_1X1_MCS5              = 17,
	WL_RATE_1X1_MCS6              = 18,
	WL_RATE_1X1_MCS7              = 19,
	WL_RATE_P_1X1_MCS87           = 20,
	WL_RATE_P_1X1_MCS88           = 21,

	WL_RATE_1X1_VHT0SS1           = 12,
	WL_RATE_1X1_VHT1SS1           = 13,
	WL_RATE_1X1_VHT2SS1           = 14,
	WL_RATE_1X1_VHT3SS1           = 15,
	WL_RATE_1X1_VHT4SS1           = 16,
	WL_RATE_1X1_VHT5SS1           = 17,
	WL_RATE_1X1_VHT6SS1           = 18,
	WL_RATE_1X1_VHT7SS1           = 19,
	WL_RATE_1X1_VHT8SS1           = 20,
	WL_RATE_1X1_VHT9SS1           = 21,
	WL_RATE_P_1X1_VHT10SS1        = 22,
	WL_RATE_P_1X1_VHT11SS1        = 23,

	/************
	* 2 chains *
	************
	*/

	/* 1 Stream expanded + 1 */
	WL_RATE_1X2_DSSS_1            = 24,
	WL_RATE_1X2_DSSS_2            = 25,
	WL_RATE_1X2_DSSS_5_5          = 26,
	WL_RATE_1X2_DSSS_11           = 27,

	WL_RATE_1X2_CDD_OFDM_6        = 28,
	WL_RATE_1X2_CDD_OFDM_9        = 29,
	WL_RATE_1X2_CDD_OFDM_12       = 30,
	WL_RATE_1X2_CDD_OFDM_18       = 31,
	WL_RATE_1X2_CDD_OFDM_24       = 32,
	WL_RATE_1X2_CDD_OFDM_36       = 33,
	WL_RATE_1X2_CDD_OFDM_48       = 34,
	WL_RATE_1X2_CDD_OFDM_54       = 35,

	WL_RATE_1X2_CDD_MCS0          = 36,
	WL_RATE_1X2_CDD_MCS1          = 37,
	WL_RATE_1X2_CDD_MCS2          = 38,
	WL_RATE_1X2_CDD_MCS3          = 39,
	WL_RATE_1X2_CDD_MCS4          = 40,
	WL_RATE_1X2_CDD_MCS5          = 41,
	WL_RATE_1X2_CDD_MCS6          = 42,
	WL_RATE_1X2_CDD_MCS7          = 43,
	WL_RATE_P_1X2_CDD_MCS87       = 44,
	WL_RATE_P_1X2_CDD_MCS88       = 45,

	WL_RATE_1X2_VHT0SS1           = 36,
	WL_RATE_1X2_VHT1SS1           = 37,
	WL_RATE_1X2_VHT2SS1           = 38,
	WL_RATE_1X2_VHT3SS1           = 39,
	WL_RATE_1X2_VHT4SS1           = 40,
	WL_RATE_1X2_VHT5SS1           = 41,
	WL_RATE_1X2_VHT6SS1           = 42,
	WL_RATE_1X2_VHT7SS1           = 43,
	WL_RATE_1X2_VHT8SS1           = 44,
	WL_RATE_1X2_VHT9SS1           = 45,
	WL_RATE_P_1X2_VHT10SS1        = 46,
	WL_RATE_P_1X2_VHT11SS1        = 47,

	/* 2 Streams */
	WL_RATE_2X2_STBC_MCS0         = 48,
	WL_RATE_2X2_STBC_MCS1         = 49,
	WL_RATE_2X2_STBC_MCS2         = 50,
	WL_RATE_2X2_STBC_MCS3         = 51,
	WL_RATE_2X2_STBC_MCS4         = 52,
	WL_RATE_2X2_STBC_MCS5         = 53,
	WL_RATE_2X2_STBC_MCS6         = 54,
	WL_RATE_2X2_STBC_MCS7         = 55,
	WL_RATE_P_2X2_STBC_MCS87      = 56,
	WL_RATE_P_2X2_STBC_MCS88      = 57,

	WL_RATE_2X2_STBC_VHT0SS1      = 48,
	WL_RATE_2X2_STBC_VHT1SS1      = 49,
	WL_RATE_2X2_STBC_VHT2SS1      = 50,
	WL_RATE_2X2_STBC_VHT3SS1      = 51,
	WL_RATE_2X2_STBC_VHT4SS1      = 52,
	WL_RATE_2X2_STBC_VHT5SS1      = 53,
	WL_RATE_2X2_STBC_VHT6SS1      = 54,
	WL_RATE_2X2_STBC_VHT7SS1      = 55,
	WL_RATE_2X2_STBC_VHT8SS1      = 56,
	WL_RATE_2X2_STBC_VHT9SS1      = 57,
	WL_RATE_P_2X2_STBC_VHT10SS1   = 58,
	WL_RATE_P_2X2_STBC_VHT11SS1   = 59,

	WL_RATE_2X2_SDM_MCS8          = 60,
	WL_RATE_2X2_SDM_MCS9          = 61,
	WL_RATE_2X2_SDM_MCS10         = 62,
	WL_RATE_2X2_SDM_MCS11         = 63,
	WL_RATE_2X2_SDM_MCS12         = 64,
	WL_RATE_2X2_SDM_MCS13         = 65,
	WL_RATE_2X2_SDM_MCS14         = 66,
	WL_RATE_2X2_SDM_MCS15         = 67,
	WL_RATE_P_2X2_SDM_MCS99       = 68,
	WL_RATE_P_2X2_SDM_MCS100      = 69,

	WL_RATE_2X2_VHT0SS2           = 60,
	WL_RATE_2X2_VHT1SS2           = 61,
	WL_RATE_2X2_VHT2SS2           = 62,
	WL_RATE_2X2_VHT3SS2           = 63,
	WL_RATE_2X2_VHT4SS2           = 64,
	WL_RATE_2X2_VHT5SS2           = 65,
	WL_RATE_2X2_VHT6SS2           = 66,
	WL_RATE_2X2_VHT7SS2           = 67,
	WL_RATE_2X2_VHT8SS2           = 68,
	WL_RATE_2X2_VHT9SS2           = 69,
	WL_RATE_P_2X2_VHT10SS2        = 70,
	WL_RATE_P_2X2_VHT11SS2        = 71,

	/****************************
	 * TX Beamforming, 2 chains *
	 ****************************
	 */

	/* 1 Stream expanded + 1 */
	WL_RATE_1X2_TXBF_OFDM_6       = 72,
	WL_RATE_1X2_TXBF_OFDM_9       = 73,
	WL_RATE_1X2_TXBF_OFDM_12      = 74,
	WL_RATE_1X2_TXBF_OFDM_18      = 75,
	WL_RATE_1X2_TXBF_OFDM_24      = 76,
	WL_RATE_1X2_TXBF_OFDM_36      = 77,
	WL_RATE_1X2_TXBF_OFDM_48      = 78,
	WL_RATE_1X2_TXBF_OFDM_54      = 79,

	WL_RATE_1X2_TXBF_MCS0         = 80,
	WL_RATE_1X2_TXBF_MCS1         = 81,
	WL_RATE_1X2_TXBF_MCS2         = 82,
	WL_RATE_1X2_TXBF_MCS3         = 83,
	WL_RATE_1X2_TXBF_MCS4         = 84,
	WL_RATE_1X2_TXBF_MCS5         = 85,
	WL_RATE_1X2_TXBF_MCS6         = 86,
	WL_RATE_1X2_TXBF_MCS7         = 87,
	WL_RATE_P_1X2_TXBF_MCS87      = 88,
	WL_RATE_P_1X2_TXBF_MCS88      = 89,

	WL_RATE_1X2_TXBF_VHT0SS1      = 80,
	WL_RATE_1X2_TXBF_VHT1SS1      = 81,
	WL_RATE_1X2_TXBF_VHT2SS1      = 82,
	WL_RATE_1X2_TXBF_VHT3SS1      = 83,
	WL_RATE_1X2_TXBF_VHT4SS1      = 84,
	WL_RATE_1X2_TXBF_VHT5SS1      = 85,
	WL_RATE_1X2_TXBF_VHT6SS1      = 86,
	WL_RATE_1X2_TXBF_VHT7SS1      = 87,
	WL_RATE_1X2_TXBF_VHT8SS1      = 88,
	WL_RATE_1X2_TXBF_VHT9SS1      = 89,
	WL_RATE_P_1X2_TXBF_VHT10SS1   = 90,
	WL_RATE_P_1X2_TXBF_VHT11SS1   = 91,

	/* 2 Streams */
	WL_RATE_2X2_TXBF_SDM_MCS8     = 92,
	WL_RATE_2X2_TXBF_SDM_MCS9     = 93,
	WL_RATE_2X2_TXBF_SDM_MCS10    = 94,
	WL_RATE_2X2_TXBF_SDM_MCS11    = 95,
	WL_RATE_2X2_TXBF_SDM_MCS12    = 96,
	WL_RATE_2X2_TXBF_SDM_MCS13    = 97,
	WL_RATE_2X2_TXBF_SDM_MCS14    = 98,
	WL_RATE_2X2_TXBF_SDM_MCS15    = 99,
	WL_RATE_P_2X2_TXBF_SDM_MCS99  = 100,
	WL_RATE_P_2X2_TXBF_SDM_MCS100 = 101,

	WL_RATE_2X2_TXBF_VHT0SS2      = 92,
	WL_RATE_2X2_TXBF_VHT1SS2      = 93,
	WL_RATE_2X2_TXBF_VHT2SS2      = 94,
	WL_RATE_2X2_TXBF_VHT3SS2      = 95,
	WL_RATE_2X2_TXBF_VHT4SS2      = 96,
	WL_RATE_2X2_TXBF_VHT5SS2      = 97,
	WL_RATE_2X2_TXBF_VHT6SS2      = 98,
	WL_RATE_2X2_TXBF_VHT7SS2      = 99,
	WL_RATE_2X2_TXBF_VHT8SS2      = 100,
	WL_RATE_2X2_TXBF_VHT9SS2      = 101,
	WL_RATE_P_2X2_TXBF_VHT10SS2   = 102,
	WL_RATE_P_2X2_TXBF_VHT11SS2   = 103,

	/************
	* 3 chains *
	************
	*/

	/* 1 Stream expanded + 2 */
	WL_RATE_1X3_DSSS_1            = 104,
	WL_RATE_1X3_DSSS_2            = 105,
	WL_RATE_1X3_DSSS_5_5          = 106,
	WL_RATE_1X3_DSSS_11           = 107,

	WL_RATE_1X3_CDD_OFDM_6        = 108,
	WL_RATE_1X3_CDD_OFDM_9        = 109,
	WL_RATE_1X3_CDD_OFDM_12       = 110,
	WL_RATE_1X3_CDD_OFDM_18       = 111,
	WL_RATE_1X3_CDD_OFDM_24       = 112,
	WL_RATE_1X3_CDD_OFDM_36       = 113,
	WL_RATE_1X3_CDD_OFDM_48       = 114,
	WL_RATE_1X3_CDD_OFDM_54       = 115,

	WL_RATE_1X3_CDD_MCS0          = 116,
	WL_RATE_1X3_CDD_MCS1          = 117,
	WL_RATE_1X3_CDD_MCS2          = 118,
	WL_RATE_1X3_CDD_MCS3          = 119,
	WL_RATE_1X3_CDD_MCS4          = 120,
	WL_RATE_1X3_CDD_MCS5          = 121,
	WL_RATE_1X3_CDD_MCS6          = 122,
	WL_RATE_1X3_CDD_MCS7          = 123,
	WL_RATE_P_1X3_CDD_MCS87       = 124,
	WL_RATE_P_1X3_CDD_MCS88       = 125,

	WL_RATE_1X3_VHT0SS1           = 116,
	WL_RATE_1X3_VHT1SS1           = 117,
	WL_RATE_1X3_VHT2SS1           = 118,
	WL_RATE_1X3_VHT3SS1           = 119,
	WL_RATE_1X3_VHT4SS1           = 120,
	WL_RATE_1X3_VHT5SS1           = 121,
	WL_RATE_1X3_VHT6SS1           = 122,
	WL_RATE_1X3_VHT7SS1           = 123,
	WL_RATE_1X3_VHT8SS1           = 124,
	WL_RATE_1X3_VHT9SS1           = 125,
	WL_RATE_P_1X3_VHT10SS1        = 126,
	WL_RATE_P_1X3_VHT11SS1        = 127,

	/* 2 Streams expanded + 1 */
	WL_RATE_2X3_STBC_MCS0         = 128,
	WL_RATE_2X3_STBC_MCS1         = 129,
	WL_RATE_2X3_STBC_MCS2         = 130,
	WL_RATE_2X3_STBC_MCS3         = 131,
	WL_RATE_2X3_STBC_MCS4         = 132,
	WL_RATE_2X3_STBC_MCS5         = 133,
	WL_RATE_2X3_STBC_MCS6         = 134,
	WL_RATE_2X3_STBC_MCS7         = 135,
	WL_RATE_P_2X3_STBC_MCS87      = 136,
	WL_RATE_P_2X3_STBC_MCS88      = 137,

	WL_RATE_2X3_STBC_VHT0SS1      = 128,
	WL_RATE_2X3_STBC_VHT1SS1      = 129,
	WL_RATE_2X3_STBC_VHT2SS1      = 130,
	WL_RATE_2X3_STBC_VHT3SS1      = 131,
	WL_RATE_2X3_STBC_VHT4SS1      = 132,
	WL_RATE_2X3_STBC_VHT5SS1      = 133,
	WL_RATE_2X3_STBC_VHT6SS1      = 134,
	WL_RATE_2X3_STBC_VHT7SS1      = 135,
	WL_RATE_2X3_STBC_VHT8SS1      = 136,
	WL_RATE_2X3_STBC_VHT9SS1      = 137,
	WL_RATE_P_2X3_STBC_VHT10SS1   = 138,
	WL_RATE_P_2X3_STBC_VHT11SS1   = 139,

	WL_RATE_2X3_SDM_MCS8          = 140,
	WL_RATE_2X3_SDM_MCS9          = 141,
	WL_RATE_2X3_SDM_MCS10         = 142,
	WL_RATE_2X3_SDM_MCS11         = 143,
	WL_RATE_2X3_SDM_MCS12         = 144,
	WL_RATE_2X3_SDM_MCS13         = 145,
	WL_RATE_2X3_SDM_MCS14         = 146,
	WL_RATE_2X3_SDM_MCS15         = 147,
	WL_RATE_P_2X3_SDM_MCS99       = 148,
	WL_RATE_P_2X3_SDM_MCS100      = 149,

	WL_RATE_2X3_VHT0SS2           = 140,
	WL_RATE_2X3_VHT1SS2           = 141,
	WL_RATE_2X3_VHT2SS2           = 142,
	WL_RATE_2X3_VHT3SS2           = 143,
	WL_RATE_2X3_VHT4SS2           = 144,
	WL_RATE_2X3_VHT5SS2           = 145,
	WL_RATE_2X3_VHT6SS2           = 146,
	WL_RATE_2X3_VHT7SS2           = 147,
	WL_RATE_2X3_VHT8SS2           = 148,
	WL_RATE_2X3_VHT9SS2           = 149,
	WL_RATE_P_2X3_VHT10SS2        = 150,
	WL_RATE_P_2X3_VHT11SS2        = 151,

	/* 3 Streams */
	WL_RATE_3X3_SDM_MCS16         = 152,
	WL_RATE_3X3_SDM_MCS17         = 153,
	WL_RATE_3X3_SDM_MCS18         = 154,
	WL_RATE_3X3_SDM_MCS19         = 155,
	WL_RATE_3X3_SDM_MCS20         = 156,
	WL_RATE_3X3_SDM_MCS21         = 157,
	WL_RATE_3X3_SDM_MCS22         = 158,
	WL_RATE_3X3_SDM_MCS23         = 159,
	WL_RATE_P_3X3_SDM_MCS101      = 160,
	WL_RATE_P_3X3_SDM_MCS102      = 161,

	WL_RATE_3X3_VHT0SS3           = 152,
	WL_RATE_3X3_VHT1SS3           = 153,
	WL_RATE_3X3_VHT2SS3           = 154,
	WL_RATE_3X3_VHT3SS3           = 155,
	WL_RATE_3X3_VHT4SS3           = 156,
	WL_RATE_3X3_VHT5SS3           = 157,
	WL_RATE_3X3_VHT6SS3           = 158,
	WL_RATE_3X3_VHT7SS3           = 159,
	WL_RATE_3X3_VHT8SS3           = 160,
	WL_RATE_3X3_VHT9SS3           = 161,
	WL_RATE_P_3X3_VHT10SS3        = 162,
	WL_RATE_P_3X3_VHT11SS3        = 163,

	/****************************
	 * TX Beamforming, 3 chains *
	 ****************************
	 */

	/* 1 Stream expanded + 2 */
	WL_RATE_1X3_TXBF_OFDM_6       = 164,
	WL_RATE_1X3_TXBF_OFDM_9       = 165,
	WL_RATE_1X3_TXBF_OFDM_12      = 166,
	WL_RATE_1X3_TXBF_OFDM_18      = 167,
	WL_RATE_1X3_TXBF_OFDM_24      = 168,
	WL_RATE_1X3_TXBF_OFDM_36      = 169,
	WL_RATE_1X3_TXBF_OFDM_48      = 170,
	WL_RATE_1X3_TXBF_OFDM_54      = 171,

	WL_RATE_1X3_TXBF_MCS0         = 172,
	WL_RATE_1X3_TXBF_MCS1         = 173,
	WL_RATE_1X3_TXBF_MCS2         = 174,
	WL_RATE_1X3_TXBF_MCS3         = 175,
	WL_RATE_1X3_TXBF_MCS4         = 176,
	WL_RATE_1X3_TXBF_MCS5         = 177,
	WL_RATE_1X3_TXBF_MCS6         = 178,
	WL_RATE_1X3_TXBF_MCS7         = 179,
	WL_RATE_P_1X3_TXBF_MCS87      = 180,
	WL_RATE_P_1X3_TXBF_MCS88      = 181,

	WL_RATE_1X3_TXBF_VHT0SS1      = 172,
	WL_RATE_1X3_TXBF_VHT1SS1      = 173,
	WL_RATE_1X3_TXBF_VHT2SS1      = 174,
	WL_RATE_1X3_TXBF_VHT3SS1      = 175,
	WL_RATE_1X3_TXBF_VHT4SS1      = 176,
	WL_RATE_1X3_TXBF_VHT5SS1      = 177,
	WL_RATE_1X3_TXBF_VHT6SS1      = 178,
	WL_RATE_1X3_TXBF_VHT7SS1      = 179,
	WL_RATE_1X3_TXBF_VHT8SS1      = 180,
	WL_RATE_1X3_TXBF_VHT9SS1      = 181,
	WL_RATE_P_1X3_TXBF_VHT10SS1   = 182,
	WL_RATE_P_1X3_TXBF_VHT11SS1   = 183,

	/* 2 Streams expanded + 1 */
	WL_RATE_2X3_TXBF_SDM_MCS8     = 184,
	WL_RATE_2X3_TXBF_SDM_MCS9     = 185,
	WL_RATE_2X3_TXBF_SDM_MCS10    = 186,
	WL_RATE_2X3_TXBF_SDM_MCS11    = 187,
	WL_RATE_2X3_TXBF_SDM_MCS12    = 188,
	WL_RATE_2X3_TXBF_SDM_MCS13    = 189,
	WL_RATE_2X3_TXBF_SDM_MCS14    = 190,
	WL_RATE_2X3_TXBF_SDM_MCS15    = 191,
	WL_RATE_P_2X3_TXBF_SDM_MCS99  = 192,
	WL_RATE_P_2X3_TXBF_SDM_MCS100 = 193,

	WL_RATE_2X3_TXBF_VHT0SS2      = 184,
	WL_RATE_2X3_TXBF_VHT1SS2      = 185,
	WL_RATE_2X3_TXBF_VHT2SS2      = 186,
	WL_RATE_2X3_TXBF_VHT3SS2      = 187,
	WL_RATE_2X3_TXBF_VHT4SS2      = 188,
	WL_RATE_2X3_TXBF_VHT5SS2      = 189,
	WL_RATE_2X3_TXBF_VHT6SS2      = 190,
	WL_RATE_2X3_TXBF_VHT7SS2      = 191,
	WL_RATE_2X3_TXBF_VHT8SS2      = 192,
	WL_RATE_2X3_TXBF_VHT9SS2      = 193,
	WL_RATE_P_2X3_TXBF_VHT10SS2   = 194,
	WL_RATE_P_2X3_TXBF_VHT11SS2   = 195,

	/* 3 Streams */
	WL_RATE_3X3_TXBF_SDM_MCS16    = 196,
	WL_RATE_3X3_TXBF_SDM_MCS17    = 197,
	WL_RATE_3X3_TXBF_SDM_MCS18    = 198,
	WL_RATE_3X3_TXBF_SDM_MCS19    = 199,
	WL_RATE_3X3_TXBF_SDM_MCS20    = 200,
	WL_RATE_3X3_TXBF_SDM_MCS21    = 201,
	WL_RATE_3X3_TXBF_SDM_MCS22    = 202,
	WL_RATE_3X3_TXBF_SDM_MCS23    = 203,
	WL_RATE_P_3X3_TXBF_SDM_MCS101 = 204,
	WL_RATE_P_3X3_TXBF_SDM_MCS102 = 205,

	WL_RATE_3X3_TXBF_VHT0SS3      = 196,
	WL_RATE_3X3_TXBF_VHT1SS3      = 197,
	WL_RATE_3X3_TXBF_VHT2SS3      = 198,
	WL_RATE_3X3_TXBF_VHT3SS3      = 199,
	WL_RATE_3X3_TXBF_VHT4SS3      = 200,
	WL_RATE_3X3_TXBF_VHT5SS3      = 201,
	WL_RATE_3X3_TXBF_VHT6SS3      = 202,
	WL_RATE_3X3_TXBF_VHT7SS3      = 203,
	WL_RATE_3X3_TXBF_VHT8SS3      = 204,
	WL_RATE_3X3_TXBF_VHT9SS3      = 205,
	WL_RATE_P_3X3_TXBF_VHT10SS3   = 206,
	WL_RATE_P_3X3_TXBF_VHT11SS3   = 207,

	/************
	* 4 chains *
	************
	*/

	/* 1 Stream expanded + 3 */
	WL_RATE_1X4_DSSS_1            = 208,
	WL_RATE_1X4_DSSS_2            = 209,
	WL_RATE_1X4_DSSS_5_5          = 210,
	WL_RATE_1X4_DSSS_11           = 211,

	WL_RATE_1X4_CDD_OFDM_6        = 212,
	WL_RATE_1X4_CDD_OFDM_9        = 213,
	WL_RATE_1X4_CDD_OFDM_12       = 214,
	WL_RATE_1X4_CDD_OFDM_18       = 215,
	WL_RATE_1X4_CDD_OFDM_24       = 216,
	WL_RATE_1X4_CDD_OFDM_36       = 217,
	WL_RATE_1X4_CDD_OFDM_48       = 218,
	WL_RATE_1X4_CDD_OFDM_54       = 219,

	WL_RATE_1X4_CDD_MCS0          = 220,
	WL_RATE_1X4_CDD_MCS1          = 221,
	WL_RATE_1X4_CDD_MCS2          = 222,
	WL_RATE_1X4_CDD_MCS3          = 223,
	WL_RATE_1X4_CDD_MCS4          = 224,
	WL_RATE_1X4_CDD_MCS5          = 225,
	WL_RATE_1X4_CDD_MCS6          = 226,
	WL_RATE_1X4_CDD_MCS7          = 227,
	WL_RATE_P_1X4_CDD_MCS87       = 228,
	WL_RATE_P_1X4_CDD_MCS88       = 229,

	WL_RATE_1X4_VHT0SS1           = 220,
	WL_RATE_1X4_VHT1SS1           = 221,
	WL_RATE_1X4_VHT2SS1           = 222,
	WL_RATE_1X4_VHT3SS1           = 223,
	WL_RATE_1X4_VHT4SS1           = 224,
	WL_RATE_1X4_VHT5SS1           = 225,
	WL_RATE_1X4_VHT6SS1           = 226,
	WL_RATE_1X4_VHT7SS1           = 227,
	WL_RATE_1X4_VHT8SS1           = 228,
	WL_RATE_1X4_VHT9SS1           = 229,
	WL_RATE_P_1X4_VHT10SS1        = 230,
	WL_RATE_P_1X4_VHT11SS1        = 231,

	/* 2 Streams expanded + 2 */
	WL_RATE_2X4_STBC_MCS0         = 232,
	WL_RATE_2X4_STBC_MCS1         = 233,
	WL_RATE_2X4_STBC_MCS2         = 234,
	WL_RATE_2X4_STBC_MCS3         = 235,
	WL_RATE_2X4_STBC_MCS4         = 236,
	WL_RATE_2X4_STBC_MCS5         = 237,
	WL_RATE_2X4_STBC_MCS6         = 238,
	WL_RATE_2X4_STBC_MCS7         = 239,
	WL_RATE_P_2X4_STBC_MCS87      = 240,
	WL_RATE_P_2X4_STBC_MCS88      = 241,

	WL_RATE_2X4_STBC_VHT0SS1      = 232,
	WL_RATE_2X4_STBC_VHT1SS1      = 233,
	WL_RATE_2X4_STBC_VHT2SS1      = 234,
	WL_RATE_2X4_STBC_VHT3SS1      = 235,
	WL_RATE_2X4_STBC_VHT4SS1      = 236,
	WL_RATE_2X4_STBC_VHT5SS1      = 237,
	WL_RATE_2X4_STBC_VHT6SS1      = 238,
	WL_RATE_2X4_STBC_VHT7SS1      = 239,
	WL_RATE_2X4_STBC_VHT8SS1      = 240,
	WL_RATE_2X4_STBC_VHT9SS1      = 241,
	WL_RATE_P_2X4_STBC_VHT10SS1   = 242,
	WL_RATE_P_2X4_STBC_VHT11SS1   = 243,

	WL_RATE_2X4_SDM_MCS8          = 244,
	WL_RATE_2X4_SDM_MCS9          = 245,
	WL_RATE_2X4_SDM_MCS10         = 246,
	WL_RATE_2X4_SDM_MCS11         = 247,
	WL_RATE_2X4_SDM_MCS12         = 248,
	WL_RATE_2X4_SDM_MCS13         = 249,
	WL_RATE_2X4_SDM_MCS14         = 250,
	WL_RATE_2X4_SDM_MCS15         = 251,
	WL_RATE_P_2X4_SDM_MCS99       = 252,
	WL_RATE_P_2X4_SDM_MCS100      = 253,

	WL_RATE_2X4_VHT0SS2           = 244,
	WL_RATE_2X4_VHT1SS2           = 245,
	WL_RATE_2X4_VHT2SS2           = 246,
	WL_RATE_2X4_VHT3SS2           = 247,
	WL_RATE_2X4_VHT4SS2           = 248,
	WL_RATE_2X4_VHT5SS2           = 249,
	WL_RATE_2X4_VHT6SS2           = 250,
	WL_RATE_2X4_VHT7SS2           = 251,
	WL_RATE_2X4_VHT8SS2           = 252,
	WL_RATE_2X4_VHT9SS2           = 253,
	WL_RATE_P_2X4_VHT10SS2        = 254,
	WL_RATE_P_2X4_VHT11SS2        = 255,

	/* 3 Streams expanded + 1 */
	WL_RATE_3X4_SDM_MCS16         = 256,
	WL_RATE_3X4_SDM_MCS17         = 257,
	WL_RATE_3X4_SDM_MCS18         = 258,
	WL_RATE_3X4_SDM_MCS19         = 259,
	WL_RATE_3X4_SDM_MCS20         = 260,
	WL_RATE_3X4_SDM_MCS21         = 261,
	WL_RATE_3X4_SDM_MCS22         = 262,
	WL_RATE_3X4_SDM_MCS23         = 263,
	WL_RATE_P_3X4_SDM_MCS101      = 264,
	WL_RATE_P_3X4_SDM_MCS102      = 265,

	WL_RATE_3X4_VHT0SS3           = 256,
	WL_RATE_3X4_VHT1SS3           = 257,
	WL_RATE_3X4_VHT2SS3           = 258,
	WL_RATE_3X4_VHT3SS3           = 259,
	WL_RATE_3X4_VHT4SS3           = 260,
	WL_RATE_3X4_VHT5SS3           = 261,
	WL_RATE_3X4_VHT6SS3           = 262,
	WL_RATE_3X4_VHT7SS3           = 263,
	WL_RATE_3X4_VHT8SS3           = 264,
	WL_RATE_3X4_VHT9SS3           = 265,
	WL_RATE_P_3X4_VHT10SS3        = 266,
	WL_RATE_P_3X4_VHT11SS3        = 267,

	/* 4 Streams */
	WL_RATE_4X4_SDM_MCS24         = 268,
	WL_RATE_4X4_SDM_MCS25         = 269,
	WL_RATE_4X4_SDM_MCS26         = 270,
	WL_RATE_4X4_SDM_MCS27         = 271,
	WL_RATE_4X4_SDM_MCS28         = 272,
	WL_RATE_4X4_SDM_MCS29         = 273,
	WL_RATE_4X4_SDM_MCS30         = 274,
	WL_RATE_4X4_SDM_MCS31         = 275,
	WL_RATE_P_4X4_SDM_MCS103      = 276,
	WL_RATE_P_4X4_SDM_MCS104      = 277,

	WL_RATE_4X4_VHT0SS4           = 268,
	WL_RATE_4X4_VHT1SS4           = 269,
	WL_RATE_4X4_VHT2SS4           = 270,
	WL_RATE_4X4_VHT3SS4           = 271,
	WL_RATE_4X4_VHT4SS4           = 272,
	WL_RATE_4X4_VHT5SS4           = 273,
	WL_RATE_4X4_VHT6SS4           = 274,
	WL_RATE_4X4_VHT7SS4           = 275,
	WL_RATE_4X4_VHT8SS4           = 276,
	WL_RATE_4X4_VHT9SS4           = 277,
	WL_RATE_P_4X4_VHT10SS4        = 278,
	WL_RATE_P_4X4_VHT11SS4        = 279,

	/****************************
	 * TX Beamforming, 4 chains *
	 ****************************
	 */

	/* 1 Stream expanded + 3 */
	WL_RATE_1X4_TXBF_OFDM_6       = 280,
	WL_RATE_1X4_TXBF_OFDM_9       = 281,
	WL_RATE_1X4_TXBF_OFDM_12      = 282,
	WL_RATE_1X4_TXBF_OFDM_18      = 283,
	WL_RATE_1X4_TXBF_OFDM_24      = 284,
	WL_RATE_1X4_TXBF_OFDM_36      = 285,
	WL_RATE_1X4_TXBF_OFDM_48      = 286,
	WL_RATE_1X4_TXBF_OFDM_54      = 287,

	WL_RATE_1X4_TXBF_MCS0         = 288,
	WL_RATE_1X4_TXBF_MCS1         = 289,
	WL_RATE_1X4_TXBF_MCS2         = 290,
	WL_RATE_1X4_TXBF_MCS3         = 291,
	WL_RATE_1X4_TXBF_MCS4         = 292,
	WL_RATE_1X4_TXBF_MCS5         = 293,
	WL_RATE_1X4_TXBF_MCS6         = 294,
	WL_RATE_1X4_TXBF_MCS7         = 295,
	WL_RATE_P_1X4_TXBF_MCS87      = 296,
	WL_RATE_P_1X4_TXBF_MCS88      = 297,

	WL_RATE_1X4_TXBF_VHT0SS1      = 288,
	WL_RATE_1X4_TXBF_VHT1SS1      = 289,
	WL_RATE_1X4_TXBF_VHT2SS1      = 290,
	WL_RATE_1X4_TXBF_VHT3SS1      = 291,
	WL_RATE_1X4_TXBF_VHT4SS1      = 292,
	WL_RATE_1X4_TXBF_VHT5SS1      = 293,
	WL_RATE_1X4_TXBF_VHT6SS1      = 294,
	WL_RATE_1X4_TXBF_VHT7SS1      = 295,
	WL_RATE_1X4_TXBF_VHT8SS1      = 296,
	WL_RATE_1X4_TXBF_VHT9SS1      = 297,
	WL_RATE_P_1X4_TXBF_VHT10SS1   = 298,
	WL_RATE_P_1X4_TXBF_VHT11SS1   = 299,

	/* 2 Streams expanded + 2 */
	WL_RATE_2X4_TXBF_SDM_MCS8     = 300,
	WL_RATE_2X4_TXBF_SDM_MCS9     = 301,
	WL_RATE_2X4_TXBF_SDM_MCS10    = 302,
	WL_RATE_2X4_TXBF_SDM_MCS11    = 303,
	WL_RATE_2X4_TXBF_SDM_MCS12    = 304,
	WL_RATE_2X4_TXBF_SDM_MCS13    = 305,
	WL_RATE_2X4_TXBF_SDM_MCS14    = 306,
	WL_RATE_2X4_TXBF_SDM_MCS15    = 307,
	WL_RATE_P_2X4_TXBF_SDM_MCS99  = 308,
	WL_RATE_P_2X4_TXBF_SDM_MCS100 = 309,

	WL_RATE_2X4_TXBF_VHT0SS2      = 300,
	WL_RATE_2X4_TXBF_VHT1SS2      = 301,
	WL_RATE_2X4_TXBF_VHT2SS2      = 302,
	WL_RATE_2X4_TXBF_VHT3SS2      = 303,
	WL_RATE_2X4_TXBF_VHT4SS2      = 304,
	WL_RATE_2X4_TXBF_VHT5SS2      = 305,
	WL_RATE_2X4_TXBF_VHT6SS2      = 306,
	WL_RATE_2X4_TXBF_VHT7SS2      = 307,
	WL_RATE_2X4_TXBF_VHT8SS2      = 308,
	WL_RATE_2X4_TXBF_VHT9SS2      = 309,
	WL_RATE_P_2X4_TXBF_VHT10SS2   = 310,
	WL_RATE_P_2X4_TXBF_VHT11SS2   = 311,

	/* 3 Streams expanded + 1 */
	WL_RATE_3X4_TXBF_SDM_MCS16    = 312,
	WL_RATE_3X4_TXBF_SDM_MCS17    = 313,
	WL_RATE_3X4_TXBF_SDM_MCS18    = 314,
	WL_RATE_3X4_TXBF_SDM_MCS19    = 315,
	WL_RATE_3X4_TXBF_SDM_MCS20    = 316,
	WL_RATE_3X4_TXBF_SDM_MCS21    = 317,
	WL_RATE_3X4_TXBF_SDM_MCS22    = 318,
	WL_RATE_3X4_TXBF_SDM_MCS23    = 319,
	WL_RATE_P_3X4_TXBF_SDM_MCS101 = 320,
	WL_RATE_P_3X4_TXBF_SDM_MCS102 = 321,

	WL_RATE_3X4_TXBF_VHT0SS3      = 312,
	WL_RATE_3X4_TXBF_VHT1SS3      = 313,
	WL_RATE_3X4_TXBF_VHT2SS3      = 314,
	WL_RATE_3X4_TXBF_VHT3SS3      = 315,
	WL_RATE_3X4_TXBF_VHT4SS3      = 316,
	WL_RATE_3X4_TXBF_VHT5SS3      = 317,
	WL_RATE_3X4_TXBF_VHT6SS3      = 318,
	WL_RATE_3X4_TXBF_VHT7SS3      = 319,
	WL_RATE_P_3X4_TXBF_VHT8SS3    = 320,
	WL_RATE_P_3X4_TXBF_VHT9SS3    = 321,
	WL_RATE_P_3X4_TXBF_VHT10SS3   = 322,
	WL_RATE_P_3X4_TXBF_VHT11SS3   = 323,

	/* 4 Streams */
	WL_RATE_4X4_TXBF_SDM_MCS24    = 324,
	WL_RATE_4X4_TXBF_SDM_MCS25    = 325,
	WL_RATE_4X4_TXBF_SDM_MCS26    = 326,
	WL_RATE_4X4_TXBF_SDM_MCS27    = 327,
	WL_RATE_4X4_TXBF_SDM_MCS28    = 328,
	WL_RATE_4X4_TXBF_SDM_MCS29    = 329,
	WL_RATE_4X4_TXBF_SDM_MCS30    = 330,
	WL_RATE_4X4_TXBF_SDM_MCS31    = 331,
	WL_RATE_P_4X4_TXBF_SDM_MCS103 = 332,
	WL_RATE_P_4X4_TXBF_SDM_MCS104 = 333,

	WL_RATE_4X4_TXBF_VHT0SS4      = 324,
	WL_RATE_4X4_TXBF_VHT1SS4      = 325,
	WL_RATE_4X4_TXBF_VHT2SS4      = 326,
	WL_RATE_4X4_TXBF_VHT3SS4      = 327,
	WL_RATE_4X4_TXBF_VHT4SS4      = 328,
	WL_RATE_4X4_TXBF_VHT5SS4      = 329,
	WL_RATE_4X4_TXBF_VHT6SS4      = 330,
	WL_RATE_4X4_TXBF_VHT7SS4      = 331,
	WL_RATE_P_4X4_TXBF_VHT8SS4    = 332,
	WL_RATE_P_4X4_TXBF_VHT9SS4    = 333,
	WL_RATE_P_4X4_TXBF_VHT10SS4   = 334,
	WL_RATE_P_4X4_TXBF_VHT11SS4   = 335

} clm_rates_t;

/* Number of rate codes */
#define WL_NUMRATES 336

/* MCS rates */
#define WLC_MAX_VHT_MCS	11	/**< Std VHT MCS 0-9 plus prop VHT MCS 10-11 */
#define WLC_MAX_HE_MCS	11	/**< Std HE MCS 0-11 */

/* Convert encoded rate value in plcp header to numerical rates in 500 KHz increments */
#define OFDM_PHY2MAC_RATE(rlpt)         plcp_ofdm_rate_tbl[(rlpt) & 0x7]
#define CCK_PHY2MAC_RATE(signal)	((signal)/5)

/* given a proprietary MCS, get number of spatial streams */
#define GET_PROPRIETARY_11N_MCS_NSS(mcs) (1 + ((mcs) - 85) / 8)

#define GET_11N_MCS_NSS(mcs) ((mcs) < 32 ? (1 + ((mcs) / 8)) : \
			      ((mcs) == 32 ? 1 : GET_PROPRIETARY_11N_MCS_NSS(mcs)))

#define IS_PROPRIETARY_11N_MCS(mcs)	FALSE
#define IS_PROPRIETARY_11N_SS_MCS(mcs)	FALSE /**< is proprietary HT single stream MCS */

/* Store HE mcs map for all NSS in a compact form:
 *
 * bit[0:2] mcs code for NSS 1
 * bit[3:5] mcs code for NSS 2
 * ...
 * bit[21:23] mcs code for NSS 8
 */

/**
 * 3 bits are used for encoding each NSS mcs map (HE MCS MAP is 24 bits)
 */
#define HE_CAP_MCS_CODE_NONE		7

/* macros to access above compact format */
#define HE_CAP_MCS_NSS_SET_MASK		0x00ffffff /* Field is to be 24 bits long */
#define HE_CAP_MCS_NSS_GET_SS_IDX(nss) (((nss)-1) * HE_CAP_MCS_CODE_SIZE)
#define HE_CAP_MCS_NSS_GET_MCS(nss, mcs_nss_map) \
	(((mcs_nss_map) >> HE_CAP_MCS_NSS_GET_SS_IDX(nss)) & HE_CAP_MCS_CODE_MASK)
#define HE_CAP_MCS_NSS_SET_MCS(nss, mcs_code, mcs_nss_map) \
	do { \
	(mcs_nss_map) &= (~(HE_CAP_MCS_CODE_MASK << HE_CAP_MCS_NSS_GET_SS_IDX(nss))); \
	(mcs_nss_map) |= (((mcs_code) & HE_CAP_MCS_CODE_MASK) << HE_CAP_MCS_NSS_GET_SS_IDX(nss)); \
	(mcs_nss_map) &= (HE_CAP_MCS_NSS_SET_MASK); \
	} while (0)

extern const uint8 plcp_ofdm_rate_tbl[];

uint8 wf_get_single_stream_mcs(uint mcs);

uint8 wf_vht_plcp_to_rate(uint8 *plcp);
uint wf_mcs_to_rate(uint mcs, uint nss, uint bw, int sgi);
uint wf_he_mcs_to_rate(uint mcs, uint nss, uint bw, uint gi, bool dcm);
uint wf_mcs_to_Ndbps(uint mcs, uint nss, uint bw);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _bcmwifi_rates_h_ */
