/*
 * Indices for 802.11 a/b/g/n/ac 1-3 chain symmetric transmit rates
 *
 * Copyright (C) 1999-2012, Broadcom Corporation
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
 * $Id: bcmwifi_rates.h 252708 2011-04-12 06:45:56Z $
 */

#ifndef _bcmwifi_rates_h_
#define _bcmwifi_rates_h_

#ifdef __cplusplus
extern "C" {
#endif 


#define WL_RATESET_SZ_DSSS		4
#define WL_RATESET_SZ_OFDM		8
#define WL_RATESET_SZ_HT_MCS	8
#define WL_RATESET_SZ_VHT_MCS	10

#define WL_TX_CHAINS_MAX	3

#define WL_RATE_DISABLED		(-128) 


typedef enum wl_tx_bw {
	WL_TX_BW_20,
	WL_TX_BW_40,
	WL_TX_BW_80,
	WL_TX_BW_20IN40,
	WL_TX_BW_20IN80,
	WL_TX_BW_40IN80,
	WL_TX_BW_ALL
} wl_tx_bw_t;



typedef enum wl_tx_mode {
	WL_TX_MODE_NONE,
	WL_TX_MODE_STBC,
	WL_TX_MODE_CDD,
	WL_TX_MODE_SDM
} wl_tx_mode_t;



typedef enum wl_tx_chains {
	WL_TX_CHAINS_1 = 1,
	WL_TX_CHAINS_2,
	WL_TX_CHAINS_3
} wl_tx_chains_t;



typedef enum wl_tx_nss {
	WL_TX_NSS_1 = 1,
	WL_TX_NSS_2,
	WL_TX_NSS_3
} wl_tx_nss_t;


typedef enum clm_rates {
	

	
	WL_RATE_1X1_DSSS_1         = 0,
	WL_RATE_1X1_DSSS_2         = 1,
	WL_RATE_1X1_DSSS_5_5       = 2,
	WL_RATE_1X1_DSSS_11        = 3,

	WL_RATE_1X1_OFDM_6         = 4,
	WL_RATE_1X1_OFDM_9         = 5,
	WL_RATE_1X1_OFDM_12        = 6,
	WL_RATE_1X1_OFDM_18        = 7,
	WL_RATE_1X1_OFDM_24        = 8,
	WL_RATE_1X1_OFDM_36        = 9,
	WL_RATE_1X1_OFDM_48        = 10,
	WL_RATE_1X1_OFDM_54        = 11,

	WL_RATE_1X1_MCS0           = 12,
	WL_RATE_1X1_MCS1           = 13,
	WL_RATE_1X1_MCS2           = 14,
	WL_RATE_1X1_MCS3           = 15,
	WL_RATE_1X1_MCS4           = 16,
	WL_RATE_1X1_MCS5           = 17,
	WL_RATE_1X1_MCS6           = 18,
	WL_RATE_1X1_MCS7           = 19,

	WL_RATE_1X1_VHT0SS1        = 12,
	WL_RATE_1X1_VHT1SS1        = 13,
	WL_RATE_1X1_VHT2SS1        = 14,
	WL_RATE_1X1_VHT3SS1        = 15,
	WL_RATE_1X1_VHT4SS1        = 16,
	WL_RATE_1X1_VHT5SS1        = 17,
	WL_RATE_1X1_VHT6SS1        = 18,
	WL_RATE_1X1_VHT7SS1        = 19,
	WL_RATE_1X1_VHT8SS1        = 20,
	WL_RATE_1X1_VHT9SS1        = 21,


	

	
	WL_RATE_1X2_DSSS_1         = 22,
	WL_RATE_1X2_DSSS_2         = 23,
	WL_RATE_1X2_DSSS_5_5       = 24,
	WL_RATE_1X2_DSSS_11        = 25,

	WL_RATE_1X2_CDD_OFDM_6     = 26,
	WL_RATE_1X2_CDD_OFDM_9     = 27,
	WL_RATE_1X2_CDD_OFDM_12    = 28,
	WL_RATE_1X2_CDD_OFDM_18    = 29,
	WL_RATE_1X2_CDD_OFDM_24    = 30,
	WL_RATE_1X2_CDD_OFDM_36    = 31,
	WL_RATE_1X2_CDD_OFDM_48    = 32,
	WL_RATE_1X2_CDD_OFDM_54    = 33,

	WL_RATE_1X2_CDD_MCS0       = 34,
	WL_RATE_1X2_CDD_MCS1       = 35,
	WL_RATE_1X2_CDD_MCS2       = 36,
	WL_RATE_1X2_CDD_MCS3       = 37,
	WL_RATE_1X2_CDD_MCS4       = 38,
	WL_RATE_1X2_CDD_MCS5       = 39,
	WL_RATE_1X2_CDD_MCS6       = 40,
	WL_RATE_1X2_CDD_MCS7       = 41,

	WL_RATE_1X2_VHT0SS1        = 34,
	WL_RATE_1X2_VHT1SS1        = 35,
	WL_RATE_1X2_VHT2SS1        = 36,
	WL_RATE_1X2_VHT3SS1        = 37,
	WL_RATE_1X2_VHT4SS1        = 38,
	WL_RATE_1X2_VHT5SS1        = 39,
	WL_RATE_1X2_VHT6SS1        = 40,
	WL_RATE_1X2_VHT7SS1        = 41,
	WL_RATE_1X2_VHT8SS1        = 42,
	WL_RATE_1X2_VHT9SS1        = 43,

	
	WL_RATE_2X2_STBC_MCS0      = 44,
	WL_RATE_2X2_STBC_MCS1      = 45,
	WL_RATE_2X2_STBC_MCS2      = 46,
	WL_RATE_2X2_STBC_MCS3      = 47,
	WL_RATE_2X2_STBC_MCS4      = 48,
	WL_RATE_2X2_STBC_MCS5      = 49,
	WL_RATE_2X2_STBC_MCS6      = 50,
	WL_RATE_2X2_STBC_MCS7      = 51,

	WL_RATE_2X2_STBC_VHT0SS1   = 44,
	WL_RATE_2X2_STBC_VHT1SS1   = 45,
	WL_RATE_2X2_STBC_VHT2SS1   = 46,
	WL_RATE_2X2_STBC_VHT3SS1   = 47,
	WL_RATE_2X2_STBC_VHT4SS1   = 48,
	WL_RATE_2X2_STBC_VHT5SS1   = 49,
	WL_RATE_2X2_STBC_VHT6SS1   = 50,
	WL_RATE_2X2_STBC_VHT7SS1   = 51,
	WL_RATE_2X2_STBC_VHT8SS1   = 52,
	WL_RATE_2X2_STBC_VHT9SS1   = 53,

	WL_RATE_2X2_SDM_MCS8       = 54,
	WL_RATE_2X2_SDM_MCS9       = 55,
	WL_RATE_2X2_SDM_MCS10      = 56,
	WL_RATE_2X2_SDM_MCS11      = 57,
	WL_RATE_2X2_SDM_MCS12      = 58,
	WL_RATE_2X2_SDM_MCS13      = 59,
	WL_RATE_2X2_SDM_MCS14      = 60,
	WL_RATE_2X2_SDM_MCS15      = 61,

	WL_RATE_2X2_VHT0SS2        = 54,
	WL_RATE_2X2_VHT1SS2        = 55,
	WL_RATE_2X2_VHT2SS2        = 56,
	WL_RATE_2X2_VHT3SS2        = 57,
	WL_RATE_2X2_VHT4SS2        = 58,
	WL_RATE_2X2_VHT5SS2        = 59,
	WL_RATE_2X2_VHT6SS2        = 60,
	WL_RATE_2X2_VHT7SS2        = 61,
	WL_RATE_2X2_VHT8SS2        = 62,
	WL_RATE_2X2_VHT9SS2        = 63,


	

	
	WL_RATE_1X3_DSSS_1         = 64,
	WL_RATE_1X3_DSSS_2         = 65,
	WL_RATE_1X3_DSSS_5_5       = 66,
	WL_RATE_1X3_DSSS_11        = 67,

	WL_RATE_1X3_CDD_OFDM_6     = 68,
	WL_RATE_1X3_CDD_OFDM_9     = 69,
	WL_RATE_1X3_CDD_OFDM_12    = 70,
	WL_RATE_1X3_CDD_OFDM_18    = 71,
	WL_RATE_1X3_CDD_OFDM_24    = 72,
	WL_RATE_1X3_CDD_OFDM_36    = 73,
	WL_RATE_1X3_CDD_OFDM_48    = 74,
	WL_RATE_1X3_CDD_OFDM_54    = 75,

	WL_RATE_1X3_CDD_MCS0       = 76,
	WL_RATE_1X3_CDD_MCS1       = 77,
	WL_RATE_1X3_CDD_MCS2       = 78,
	WL_RATE_1X3_CDD_MCS3       = 79,
	WL_RATE_1X3_CDD_MCS4       = 80,
	WL_RATE_1X3_CDD_MCS5       = 81,
	WL_RATE_1X3_CDD_MCS6       = 82,
	WL_RATE_1X3_CDD_MCS7       = 83,

	WL_RATE_1X3_VHT0SS1        = 76,
	WL_RATE_1X3_VHT1SS1        = 77,
	WL_RATE_1X3_VHT2SS1        = 78,
	WL_RATE_1X3_VHT3SS1        = 79,
	WL_RATE_1X3_VHT4SS1        = 80,
	WL_RATE_1X3_VHT5SS1        = 81,
	WL_RATE_1X3_VHT6SS1        = 82,
	WL_RATE_1X3_VHT7SS1        = 83,
	WL_RATE_1X3_VHT8SS1        = 84,
	WL_RATE_1X3_VHT9SS1        = 85,

	
	WL_RATE_2X3_STBC_MCS0      = 86,
	WL_RATE_2X3_STBC_MCS1      = 87,
	WL_RATE_2X3_STBC_MCS2      = 88,
	WL_RATE_2X3_STBC_MCS3      = 89,
	WL_RATE_2X3_STBC_MCS4      = 90,
	WL_RATE_2X3_STBC_MCS5      = 91,
	WL_RATE_2X3_STBC_MCS6      = 92,
	WL_RATE_2X3_STBC_MCS7      = 93,

	WL_RATE_2X3_STBC_VHT0SS1   = 86,
	WL_RATE_2X3_STBC_VHT1SS1   = 87,
	WL_RATE_2X3_STBC_VHT2SS1   = 88,
	WL_RATE_2X3_STBC_VHT3SS1   = 89,
	WL_RATE_2X3_STBC_VHT4SS1   = 90,
	WL_RATE_2X3_STBC_VHT5SS1   = 91,
	WL_RATE_2X3_STBC_VHT6SS1   = 92,
	WL_RATE_2X3_STBC_VHT7SS1   = 93,
	WL_RATE_2X3_STBC_VHT8SS1   = 94,
	WL_RATE_2X3_STBC_VHT9SS1   = 95,

	WL_RATE_2X3_SDM_MCS8       = 96,
	WL_RATE_2X3_SDM_MCS9       = 97,
	WL_RATE_2X3_SDM_MCS10      = 98,
	WL_RATE_2X3_SDM_MCS11      = 99,
	WL_RATE_2X3_SDM_MCS12      = 100,
	WL_RATE_2X3_SDM_MCS13      = 101,
	WL_RATE_2X3_SDM_MCS14      = 102,
	WL_RATE_2X3_SDM_MCS15      = 103,

	WL_RATE_2X3_VHT0SS2        = 96,
	WL_RATE_2X3_VHT1SS2        = 97,
	WL_RATE_2X3_VHT2SS2        = 98,
	WL_RATE_2X3_VHT3SS2        = 99,
	WL_RATE_2X3_VHT4SS2        = 100,
	WL_RATE_2X3_VHT5SS2        = 101,
	WL_RATE_2X3_VHT6SS2        = 102,
	WL_RATE_2X3_VHT7SS2        = 103,
	WL_RATE_2X3_VHT8SS2        = 104,
	WL_RATE_2X3_VHT9SS2        = 105,

	
	WL_RATE_3X3_SDM_MCS16      = 106,
	WL_RATE_3X3_SDM_MCS17      = 107,
	WL_RATE_3X3_SDM_MCS18      = 108,
	WL_RATE_3X3_SDM_MCS19      = 109,
	WL_RATE_3X3_SDM_MCS20      = 110,
	WL_RATE_3X3_SDM_MCS21      = 111,
	WL_RATE_3X3_SDM_MCS22      = 112,
	WL_RATE_3X3_SDM_MCS23      = 113,

	WL_RATE_3X3_VHT0SS3        = 106,
	WL_RATE_3X3_VHT1SS3        = 107,
	WL_RATE_3X3_VHT2SS3        = 108,
	WL_RATE_3X3_VHT3SS3        = 109,
	WL_RATE_3X3_VHT4SS3        = 110,
	WL_RATE_3X3_VHT5SS3        = 111,
	WL_RATE_3X3_VHT6SS3        = 112,
	WL_RATE_3X3_VHT7SS3        = 113,
	WL_RATE_3X3_VHT8SS3        = 114,
	WL_RATE_3X3_VHT9SS3        = 115,

	
	WL_NUMRATES                = 116
} clm_rates_t;

#ifdef __cplusplus
}
#endif 

#endif 
