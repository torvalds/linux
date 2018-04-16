/** @file KeyApiStaTypes.h
 *
 *  @brief This file  defines some of the macros and types
 *  Please do not change those macros and types.
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
#ifndef KEY_API_STA_TYPES_H__
#define KEY_API_STA_TYPES_H__

#include "KeyApiStaDefs.h"
#include "wl_mib.h"
#include "keyCommonDef.h"

/******************************** Pool ID 11 ********************************/
#define POOL_ID11_BLOCK_POOL_CNT 1
#define POOL_ID11_BUF0_SZ        CIPHER_KEYS_BUF_SIZE
#define POOL_ID11_BUF0_CNT       CIPHER_KEYS_BUF_NUM
#define POOL_ID11_BUF0_REAL_SZ  (POOL_ID11_BUF0_SZ)
#define POOL_ID11_BUF0_TOT_SZ   (POOL_ID11_BUF0_REAL_SZ * POOL_ID11_BUF0_CNT)
#define POOL_ID11_SZ            (POOL_ID11_BUF0_TOT_SZ)
#define POOL_ID11_OVERHEAD      ((POOL_ID11_BUF0_CNT) * sizeof(uint32))

#define KEY_STATE_ENABLE                    0x01
#define KEY_STATE_FORCE_EAPOL_UNENCRYPTED   0x02

#define KEY_ACTION_ADD                      0x01
#define KEY_ACTION_DELETE                   0x02
#define KEY_ACTION_FLUSHALL                 0xFF

#define KEY_DIRECTION_UNSPEC                (0x0)
#define KEY_DIRECTION_RX                    (0x1)
#define KEY_DIRECTION_TX                    (0x2)
#define KEY_DIRECTION_RXTX                  (KEY_DIRECTION_RX               \
                                             | KEY_DIRECTION_TX)

#define KEY_INFO_PWK                        (0x1)
#define KEY_INFO_GWK                        (0x2)
#define KEY_INFO_IGWK                       (0x40)
#define KEY_INFO_KEY_MAPPING_KEY            (0x4)
#define KEY_INFO_WEP_SUBTYPE                (0x8)
#define KEY_INFO_GLOBAL                     (0x10)

/*
  we use BIT6 and BIT7 of
  keyInfo field of cipher_key_hdr_t to pass keyID.
*/

#define CIPHERKEY_KEY_ID_MASK   (BIT6 | BIT7)
#define CIPHERKEY_KEY_ID_OFFSET 6

#define KEY_INFO_DEFAULT_KEY                (~KEY_INFO_KEY_MAPPING_KEY)

#define VERSION_0                            0
#define VERSION_2                            2

#define IS_KEY_INFO_WEP_SUBTYPE_104BIT(x)                                   \
                        ( (x) ?                                             \
                            ((x)->hdr.keyInfo & KEY_INFO_WEP_SUBTYPE) : 0 )

#define IS_KEY_INFO_WEP_SUBTYPE_40BIT(x)                                    \
                    ( (x) ?                                                 \
                        (!((x)->hdr.keyInfo & KEY_INFO_WEP_SUBTYPE)) : 0 )

#define SET_KEY_INFO_WEP_SUBTYPE_104BIT(x)                                  \
                    ( (x) ? ((x)->hdr.keyInfo |= KEY_INFO_WEP_SUBTYPE) : 0 )

#define SET_KEY_INFO_WEP_SUBTYPE_40BIT(x)                                   \
                    ( (x) ? ((x)->hdr.keyInfo &= ~KEY_INFO_WEP_SUBTYPE) : 0 )

#define SET_KEY_INFO_PWKGWK(x)                                              \
                    ( (x) ?                                                 \
                        ((x)->hdr.keyInfo |= KEY_INFO_PWK|KEY_INFO_GWK) : 0)

#define SET_KEY_INFO_PWK(x)                                                 \
                    ( (x) ? ((x)->hdr.keyInfo |= KEY_INFO_PWK,              \
                            ((x)->hdr.keyInfo &= ~KEY_INFO_GWK)) : 0 )

#define SET_KEY_INFO_GWK(x)                                                 \
                    ( (x) ? ((x)->hdr.keyInfo |= KEY_INFO_GWK,              \
                            ((x)->hdr.keyInfo &= ~KEY_INFO_PWK)): 0 )

#define SET_KEY_INFO_GLOBAL(x)                                              \
            ( (x) ? ((x)->hdr.keyInfo |= KEY_INFO_GLOBAL) : 0 )

#define IS_KEY_INFO_GLOBAL(x)                                               \
            ( (x) ? ((x)->hdr.keyInfo & KEY_INFO_GLOBAL) : 0 )

#define SET_KEY_STATE_ENABLED(x, state)     (((state) == TRUE) ?            \
            ((x)->hdr.keyState |= KEY_STATE_ENABLE)                         \
            : ((x)->hdr.keyState &= ~KEY_STATE_ENABLE) )

#define IS_KEY_STATE_ENABLED(x)                                             \
            ( (x) ? ((x)->hdr.keyState & KEY_STATE_ENABLE) : 0 )

#define IS_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(x)                             \
            ( (x) ?                                                         \
                ((x)->hdr.keyState & KEY_STATE_FORCE_EAPOL_UNENCRYPTED) : 0)

#define SET_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(x,state)                      \
            (((state) == TRUE) ?                                            \
            ((x)->hdr.keyState |= KEY_STATE_FORCE_EAPOL_UNENCRYPTED)        \
            : ((x)->hdr.keyState &= ~KEY_STATE_FORCE_EAPOL_UNENCRYPTED) )

typedef MLAN_PACK_START struct {
	IEEEtypes_MacAddr_t macAddr;
	UINT8 keyDirection;
	UINT8 keyType;
	UINT16 keyLen;
	UINT8 keyAction;
	UINT8 keyInfo;
} MLAN_PACK_END nwf_key_mgthdr_t;

/* host command for GET/SET Key Material */
typedef MLAN_PACK_START struct {
	UINT16 Action;
	nwf_key_mgthdr_t key_material;
} MLAN_PACK_END host_802_11_NWF_Key_Material_t;

/* host command for set wep parameters in Vista uses different struct
   from the one we use in FW */

typedef MLAN_PACK_START struct host_802_11_wep_key {
	UINT8 WepDefaultKeyIdx;	/* 1 to 4 */
	UINT8 WepDefaultKeyValue[WEP_KEY_USER_INPUT];	/* 5 byte string */
} MLAN_PACK_END host_802_11_wep_key_t;

typedef struct host_802_11_wep_key_data_t {
	host_802_11_wep_key_t WepDefaultKeys[MAX_WEP_KEYS];
	UINT8 default_key_idx;
	UINT8 Reserved1[3];
} host_802_11_wep_key_data_t;

typedef UINT32 buffer_t;

typedef struct cipher_key_buf {
	struct cipher_key_buf_t *prev;
	struct cipher_key_buf_t *next;

	cipher_key_t cipher_key;
} cipher_key_buf_t;

#define CIPHER_KEYS_BUF_SIZE    (sizeof(cipher_key_buf_t))

#define CIPHER_KEYS_BUF_NUM     (CM_MAX_CONNECTIONS)

#endif /* KEY_API_STA_H__ end */
