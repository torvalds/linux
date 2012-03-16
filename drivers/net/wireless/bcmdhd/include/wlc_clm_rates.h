/*
 * Indices for 802.11 a/b/g/n/ac 1-3 chain symmetric transmit rates
 * Copyright (C) 2012, Broadcom Corporation
 * All Rights Reserved.
 * 
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom Corporation;
 * the contents of this file may not be disclosed to third parties, copied
 * or duplicated in any form, in whole or in part, without the prior
 * written permission of Broadcom Corporation.
 *
 * $Id: wlc_clm_rates.h 252708 2011-04-12 06:45:56Z $
 */

#ifndef _WLC_CLM_RATES_H_
#define _WLC_CLM_RATES_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum clm_rates {
	/************
	 * 1 chain  *
	 ************
	 */

	/* 1 Stream */
	CLM_RATE_1X1_DSSS_1         = 0,
	CLM_RATE_1X1_DSSS_2         = 1,
	CLM_RATE_1X1_DSSS_5_5       = 2,
	CLM_RATE_1X1_DSSS_11        = 3,

	CLM_RATE_1X1_OFDM_6         = 4,
	CLM_RATE_1X1_OFDM_9         = 5,
	CLM_RATE_1X1_OFDM_12        = 6,
	CLM_RATE_1X1_OFDM_18        = 7,
	CLM_RATE_1X1_OFDM_24        = 8,
	CLM_RATE_1X1_OFDM_36        = 9,
	CLM_RATE_1X1_OFDM_48        = 10,
	CLM_RATE_1X1_OFDM_54        = 11,

	CLM_RATE_1X1_MCS0           = 12,
	CLM_RATE_1X1_MCS1           = 13,
	CLM_RATE_1X1_MCS2           = 14,
	CLM_RATE_1X1_MCS3           = 15,
	CLM_RATE_1X1_MCS4           = 16,
	CLM_RATE_1X1_MCS5           = 17,
	CLM_RATE_1X1_MCS6           = 18,
	CLM_RATE_1X1_MCS7           = 19,

	CLM_RATE_1X1_VHT0SS1        = 12,
	CLM_RATE_1X1_VHT1SS1        = 13,
	CLM_RATE_1X1_VHT2SS1        = 14,
	CLM_RATE_1X1_VHT3SS1        = 15,
	CLM_RATE_1X1_VHT4SS1        = 16,
	CLM_RATE_1X1_VHT5SS1        = 17,
	CLM_RATE_1X1_VHT6SS1        = 18,
	CLM_RATE_1X1_VHT7SS1        = 19,
	CLM_RATE_1X1_VHT8SS1        = 20,
	CLM_RATE_1X1_VHT9SS1        = 21,


	/************
	 * 2 chains *
	 ************
	 */

	/* 1 Stream expanded + 1 */
	CLM_RATE_1X2_DSSS_1         = 22,
	CLM_RATE_1X2_DSSS_2         = 23,
	CLM_RATE_1X2_DSSS_5_5       = 24,
	CLM_RATE_1X2_DSSS_11        = 25,

	CLM_RATE_1X2_CDD_OFDM_6     = 26,
	CLM_RATE_1X2_CDD_OFDM_9     = 27,
	CLM_RATE_1X2_CDD_OFDM_12    = 28,
	CLM_RATE_1X2_CDD_OFDM_18    = 29,
	CLM_RATE_1X2_CDD_OFDM_24    = 30,
	CLM_RATE_1X2_CDD_OFDM_36    = 31,
	CLM_RATE_1X2_CDD_OFDM_48    = 32,
	CLM_RATE_1X2_CDD_OFDM_54    = 33,

	CLM_RATE_1X2_CDD_MCS0       = 34,
	CLM_RATE_1X2_CDD_MCS1       = 35,
	CLM_RATE_1X2_CDD_MCS2       = 36,
	CLM_RATE_1X2_CDD_MCS3       = 37,
	CLM_RATE_1X2_CDD_MCS4       = 38,
	CLM_RATE_1X2_CDD_MCS5       = 39,
	CLM_RATE_1X2_CDD_MCS6       = 40,
	CLM_RATE_1X2_CDD_MCS7       = 41,

	CLM_RATE_1X2_VHT0SS1        = 34,
	CLM_RATE_1X2_VHT1SS1        = 35,
	CLM_RATE_1X2_VHT2SS1        = 36,
	CLM_RATE_1X2_VHT3SS1        = 37,
	CLM_RATE_1X2_VHT4SS1        = 38,
	CLM_RATE_1X2_VHT5SS1        = 39,
	CLM_RATE_1X2_VHT6SS1        = 40,
	CLM_RATE_1X2_VHT7SS1        = 41,
	CLM_RATE_1X2_VHT8SS1        = 42,
	CLM_RATE_1X2_VHT9SS1        = 43,

	/* 2 Streams */
	CLM_RATE_2X2_STBC_MCS0      = 44,
	CLM_RATE_2X2_STBC_MCS1      = 45,
	CLM_RATE_2X2_STBC_MCS2      = 46,
	CLM_RATE_2X2_STBC_MCS3      = 47,
	CLM_RATE_2X2_STBC_MCS4      = 48,
	CLM_RATE_2X2_STBC_MCS5      = 49,
	CLM_RATE_2X2_STBC_MCS6      = 50,
	CLM_RATE_2X2_STBC_MCS7      = 51,

	CLM_RATE_2X2_STBC_VHT0SS1   = 44,
	CLM_RATE_2X2_STBC_VHT1SS1   = 45,
	CLM_RATE_2X2_STBC_VHT2SS1   = 46,
	CLM_RATE_2X2_STBC_VHT3SS1   = 47,
	CLM_RATE_2X2_STBC_VHT4SS1   = 48,
	CLM_RATE_2X2_STBC_VHT5SS1   = 49,
	CLM_RATE_2X2_STBC_VHT6SS1   = 50,
	CLM_RATE_2X2_STBC_VHT7SS1   = 51,
	CLM_RATE_2X2_STBC_VHT8SS1   = 52,
	CLM_RATE_2X2_STBC_VHT9SS1   = 53,

	CLM_RATE_2X2_SDM_MCS8       = 54,
	CLM_RATE_2X2_SDM_MCS9       = 55,
	CLM_RATE_2X2_SDM_MCS10      = 56,
	CLM_RATE_2X2_SDM_MCS11      = 57,
	CLM_RATE_2X2_SDM_MCS12      = 58,
	CLM_RATE_2X2_SDM_MCS13      = 59,
	CLM_RATE_2X2_SDM_MCS14      = 60,
	CLM_RATE_2X2_SDM_MCS15      = 61,

	CLM_RATE_2X2_VHT0SS2        = 54,
	CLM_RATE_2X2_VHT1SS2        = 55,
	CLM_RATE_2X2_VHT2SS2        = 56,
	CLM_RATE_2X2_VHT3SS2        = 57,
	CLM_RATE_2X2_VHT4SS2        = 58,
	CLM_RATE_2X2_VHT5SS2        = 59,
	CLM_RATE_2X2_VHT6SS2        = 60,
	CLM_RATE_2X2_VHT7SS2        = 61,
	CLM_RATE_2X2_VHT8SS2        = 62,
	CLM_RATE_2X2_VHT9SS2        = 63,


	/************
	 * 3 chains *
	 ************
	 */

	/* 1 Stream expanded + 2 */
	CLM_RATE_1X3_DSSS_1         = 64,
	CLM_RATE_1X3_DSSS_2         = 65,
	CLM_RATE_1X3_DSSS_5_5       = 66,
	CLM_RATE_1X3_DSSS_11        = 67,

	CLM_RATE_1X3_CDD_OFDM_6     = 68,
	CLM_RATE_1X3_CDD_OFDM_9     = 69,
	CLM_RATE_1X3_CDD_OFDM_12    = 70,
	CLM_RATE_1X3_CDD_OFDM_18    = 71,
	CLM_RATE_1X3_CDD_OFDM_24    = 72,
	CLM_RATE_1X3_CDD_OFDM_36    = 73,
	CLM_RATE_1X3_CDD_OFDM_48    = 74,
	CLM_RATE_1X3_CDD_OFDM_54    = 75,

	CLM_RATE_1X3_CDD_MCS0       = 76,
	CLM_RATE_1X3_CDD_MCS1       = 77,
	CLM_RATE_1X3_CDD_MCS2       = 78,
	CLM_RATE_1X3_CDD_MCS3       = 79,
	CLM_RATE_1X3_CDD_MCS4       = 80,
	CLM_RATE_1X3_CDD_MCS5       = 81,
	CLM_RATE_1X3_CDD_MCS6       = 82,
	CLM_RATE_1X3_CDD_MCS7       = 83,

	CLM_RATE_1X3_VHT0SS1        = 76,
	CLM_RATE_1X3_VHT1SS1        = 77,
	CLM_RATE_1X3_VHT2SS1        = 78,
	CLM_RATE_1X3_VHT3SS1        = 79,
	CLM_RATE_1X3_VHT4SS1        = 80,
	CLM_RATE_1X3_VHT5SS1        = 81,
	CLM_RATE_1X3_VHT6SS1        = 82,
	CLM_RATE_1X3_VHT7SS1        = 83,
	CLM_RATE_1X3_VHT8SS1        = 84,
	CLM_RATE_1X3_VHT9SS1        = 85,

	/* 2 Streams expanded + 1 */
	CLM_RATE_2X3_STBC_MCS0      = 86,
	CLM_RATE_2X3_STBC_MCS1      = 87,
	CLM_RATE_2X3_STBC_MCS2      = 88,
	CLM_RATE_2X3_STBC_MCS3      = 89,
	CLM_RATE_2X3_STBC_MCS4      = 90,
	CLM_RATE_2X3_STBC_MCS5      = 91,
	CLM_RATE_2X3_STBC_MCS6      = 92,
	CLM_RATE_2X3_STBC_MCS7      = 93,

	CLM_RATE_2X3_STBC_VHT0SS1   = 86,
	CLM_RATE_2X3_STBC_VHT1SS1   = 87,
	CLM_RATE_2X3_STBC_VHT2SS1   = 88,
	CLM_RATE_2X3_STBC_VHT3SS1   = 89,
	CLM_RATE_2X3_STBC_VHT4SS1   = 90,
	CLM_RATE_2X3_STBC_VHT5SS1   = 91,
	CLM_RATE_2X3_STBC_VHT6SS1   = 92,
	CLM_RATE_2X3_STBC_VHT7SS1   = 93,
	CLM_RATE_2X3_STBC_VHT8SS1   = 94,
	CLM_RATE_2X3_STBC_VHT9SS1   = 95,

	CLM_RATE_2X3_SDM_MCS8       = 96,
	CLM_RATE_2X3_SDM_MCS9       = 97,
	CLM_RATE_2X3_SDM_MCS10      = 98,
	CLM_RATE_2X3_SDM_MCS11      = 99,
	CLM_RATE_2X3_SDM_MCS12      = 100,
	CLM_RATE_2X3_SDM_MCS13      = 101,
	CLM_RATE_2X3_SDM_MCS14      = 102,
	CLM_RATE_2X3_SDM_MCS15      = 103,

	CLM_RATE_2X3_VHT0SS2        = 96,
	CLM_RATE_2X3_VHT1SS2        = 97,
	CLM_RATE_2X3_VHT2SS2        = 98,
	CLM_RATE_2X3_VHT3SS2        = 99,
	CLM_RATE_2X3_VHT4SS2        = 100,
	CLM_RATE_2X3_VHT5SS2        = 101,
	CLM_RATE_2X3_VHT6SS2        = 102,
	CLM_RATE_2X3_VHT7SS2        = 103,
	CLM_RATE_2X3_VHT8SS2        = 104,
	CLM_RATE_2X3_VHT9SS2        = 105,

	/* 3 Streams */
	CLM_RATE_3X3_SDM_MCS16      = 106,
	CLM_RATE_3X3_SDM_MCS17      = 107,
	CLM_RATE_3X3_SDM_MCS18      = 108,
	CLM_RATE_3X3_SDM_MCS19      = 109,
	CLM_RATE_3X3_SDM_MCS20      = 110,
	CLM_RATE_3X3_SDM_MCS21      = 111,
	CLM_RATE_3X3_SDM_MCS22      = 112,
	CLM_RATE_3X3_SDM_MCS23      = 113,

	CLM_RATE_3X3_VHT0SS3        = 106,
	CLM_RATE_3X3_VHT1SS3        = 107,
	CLM_RATE_3X3_VHT2SS3        = 108,
	CLM_RATE_3X3_VHT3SS3        = 109,
	CLM_RATE_3X3_VHT4SS3        = 110,
	CLM_RATE_3X3_VHT5SS3        = 111,
	CLM_RATE_3X3_VHT6SS3        = 112,
	CLM_RATE_3X3_VHT7SS3        = 113,
	CLM_RATE_3X3_VHT8SS3        = 114,
	CLM_RATE_3X3_VHT9SS3        = 115,

	/* Number of rate codes */
	CLM_NUMRATES				= 116,
	} clm_rates_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WLC_CLM_RATES_H_ */
