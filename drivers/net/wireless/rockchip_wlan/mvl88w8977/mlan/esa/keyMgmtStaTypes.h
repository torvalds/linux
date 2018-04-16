/** @file keyMgmtstaType.h
 *
 *  @brief This file defines data types structrue for key managment.
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
#ifndef _KEYMGMTSTATYPES_H_
#define _KEYMGMTSTATYPES_H_

#define WPA_AES_KEY_LEN     16
#define WPA_TKIP_KEY_LEN    32
#define WPA_WEP104_KEY_LEN  13
#define WPA_WEP40_KEY_LEN   5

#define KEY_TYPE_WEP  0
#define KEY_TYPE_TKIP 1
#define KEY_TYPE_AES  2
#define KEY_TYPE_WAPI 3
#define KEY_TYPE_AES_CMAC	4

/* This struct definition is used in ROM and should be kept intact */
typedef struct {
	UINT8 wep40:1;
	UINT8 wep104:1;
	UINT8 tkip:1;
	UINT8 ccmp:1;

	UINT8 rsvd:4;

} Cipher_t;

/* This struct definition is used in ROM and should be kept intact */
typedef MLAN_PACK_START struct {
	UINT16 noRsn:1;
	UINT16 wepStatic:1;
	UINT16 wepDynamic:1;
	UINT16 wpa:1;
	UINT16 wpaNone:1;
	UINT16 wpa2:1;
	UINT16 cckm:1;
	UINT16 wapi:1;
	UINT16 rsvd:8;

} MLAN_PACK_END SecurityMode_t;

typedef MLAN_PACK_START enum {
	AKM_NONE = 0,

	AKM_1X = 1,
	AKM_PSK = 2,
	AKM_FT_1X = 3,
	AKM_FT_PSK = 4,
	AKM_SHA256_1X = 5,
	AKM_SHA256_PSK = 6,
	AKM_TDLS = 7,

	AKM_CCKM = 99,		/* Make CCKM a unique AKM enumeration */

	AKM_WPA_MAX = 2,	/* Only AKM types 1 and 2 are expected for WPA */
	AKM_RSN_MAX = 6,	/* AKM types 1 to 6 are valid for RSN */

	AKM_SUITE_MAX = 5	/* Used to size max parsing of AKMs from a BCN */
} MLAN_PACK_END AkmType_e;

typedef AkmType_e AkmTypePacked_e;

typedef struct {
	UINT8 akmOui[3];
	AkmTypePacked_e akmType;

} AkmSuite_t;

typedef struct {
	SecurityMode_t wpaType;
	Cipher_t mcstCipher;
	Cipher_t ucstCipher;

} SecurityParams_t;

typedef enum {
	host_PSK_SUPP_MIC_FAIL_TIMEOUT = 0x0001,
	host_PSK_SUPP_FOURWAY_HANDSHAKE_FAIL = 0x0002
} host_psk_suppMsg_e;

typedef struct {
	UINT8 keyId[2];
	UINT8 IPN[6];
	UINT8 IGtk[32];
} IGtkKde_t;

#endif
