/** @file wl_mib_rom.h
 *
 *  @brieThis file contains the MIB structure definitions
 *          based on IEEE 802.11 specification.
 *
 * Copyright (C) 2014-2017, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

/******************************************************
Change log:
    03/07/2014: Initial version
******************************************************/
#if !defined(WL_MIB_ROM_H__)

#define WL_MIB_ROM_H__
#include "IEEE_types.h"

#define MIB_EDCA_MSDU_LIFETIME_DEFAULT 512
#define WEP_KEY_USER_INPUT              13			/** Also defined in keyApiStaTypes.h */

/*-----------------------------------------*/

/* PHY Supported Transmit Data Rates Table */

/*-----------------------------------------*/

typedef struct MIB_PhySuppDataRatesTx_s {
	UINT8 SuppDataRatesTxIdx;	/*1 to IEEEtypes_MAX_DATA_RATES_G */
	UINT8 SuppDataRatesTxVal;	/*2 to 127 */
} MIB_PHY_SUPP_DATA_RATES_TX;

/*------------------------*/
/* WEP Default Keys Table */
/*------------------------*/
/* This struct is used in ROM and it should not be changed at all */
typedef struct MIB_WepDefaultKeys_s {
	UINT8 WepDefaultKeyIdx;	/* 1 to 4 */
	UINT8 WepDefaultKeyType;	/*   */
	UINT8 WepDefaultKeyValue[WEP_KEY_USER_INPUT];	/* 5 byte string */
} MIB_WEP_DEFAULT_KEYS;

typedef struct {
	/* Maximum lifetime of an MSDU from when it enters the MAC, 802.11e */
	UINT16 MSDULifetime;	/* 0 to 500, 500 default */
} MIB_EDCA_CONFIG;

#endif /* _WL_MIB_ROM_H_ */
