/** @file keyCommonDef.h
 *
 *  @brief This file contains normal data type for key management
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
#ifndef _KEYMGMT_COMMON_H_
#define _KEYMGMT_COMMON_H_

#include "wltypes.h"
#include "IEEE_types.h"
#include "wl_mib_rom.h"
#include "KeyApiStaDefs.h"

#define NONCE_SIZE            32
#define EAPOL_MIC_KEY_SIZE    16
#define EAPOL_MIC_SIZE        16
#define EAPOL_ENCR_KEY_SIZE   16
#define MAC_ADDR_SIZE         6
#define TK_SIZE               16
#define HDR_8021x_LEN         4
#define KEYMGMTTIMEOUTVAL     10
#define TDLS_MIC_KEY_SIZE     16

#define EAPOL_PROTOCOL_V1 1
#define EAPOL_PROTOCOL_V2 2

#define UAP_HOSTCMD_KEYMGMT_EAP 	BIT0
#define UAP_HOSTCMD_KEYMGMT_PSK 	BIT1
#define UAP_HOSTCMD_KEYMGMT_NONE 	BIT2
#define UAP_HOSTCMD_KEYMGMT_PSK_SHA256	BIT8

#define UAP_HOSTCMD_CIPHER_WEP40 0x01
#define UAP_HOSTCMD_CIPHER_WEP104 0x02
#define UAP_HOSTCMD_CIPHER_TKIP 0x04
#define UAP_HOSTCMD_CIPHER_CCMP 0x08
#define UAP_HOSTCMD_CIPHER_MASK 0x0F

typedef struct {
	UINT8 Key[TK_SIZE];
	UINT8 RxMICKey[8];
	UINT8 TxMICKey[8];
	UINT32 TxIV32;
	UINT16 TxIV16;
	UINT16 KeyIndex;
} KeyData_t;

#define MAX_WEP_KEYS                        4
/* This structure is used in rom and existing fields should not be changed */
/* This structure is already aligned and hence packing is not needed */

typedef struct cipher_key_hdr_t {
	IEEEtypes_MacAddr_t macAddr;
	UINT8 keyDirection;
	UINT8 keyType:4;
	UINT8 version:4;
	UINT16 keyLen;
	UINT8 keyState;
	UINT8 keyInfo;
} cipher_key_hdr_t;

/* This structure is used in rom and existing fields should not be changed */
typedef struct tkip_aes_key_data_t {
	// key material information (TKIP/AES/WEP)
	UINT8 key[CRYPTO_KEY_LEN_MAX];
	UINT8 txMICKey[MIC_KEY_LEN_MAX];
	UINT8 rxMICKey[MIC_KEY_LEN_MAX];
	UINT32 hiReplayCounter32;	//!< initialized by host
	UINT16 loReplayCounter16;	//!< initialized by host
	UINT32 txIV32;		//!< controlled by FW
	UINT16 txIV16;		//!< controlled by FW
	UINT32 TKIPMicLeftValue;
	UINT32 TKIPMicRightValue;

	/* HW new design for 8682 only to support interleaving
	 * FW need to save these value and
	 * restore for next fragment
	 */
	UINT32 TKIPMicData0Value;
	UINT32 TKIPMicData1Value;
	UINT32 TKIPMicData2Value;
	UINT8 keyIdx;
	UINT8 reserved[3];

} tkip_aes_key_data_t;

/* This structure is used in rom and existing fields should not be changed */
typedef struct wep_key_data_t {
	MIB_WEP_DEFAULT_KEYS WepDefaultKeys[MAX_WEP_KEYS];
	UINT8 default_key_idx;
	UINT8 keyCfg;
	UINT8 Reserved;
} wep_key_data_t;

/* This structure is used in rom and existing fields should not be changed */
typedef struct {
	UINT8 key_idx;
	UINT8 mickey[WAPI_MIC_LEN];
	UINT8 rawkey[WAPI_KEY_LEN];
} wapi_key_detail_t;

/* cipher_key_t -> tkip_aes is much bigger than wapi_key_data_t and
 * since wapi_key_data_t is not used by ROM it is ok to change this size. */

typedef struct {
	wapi_key_detail_t key;
	UINT8 pn_inc;
	UINT8 TxPN[WAPI_PN_LEN];
	UINT8 RxPN[WAPI_PN_LEN];
	UINT8 *pLastKey;	//keep the orig cipher_key_t pointer
} wapi_key_data_t;

typedef struct {
	UINT8 ANonce[NONCE_SIZE];
	KeyData_t pwsKeyData;
} eapolHskData_t;

/* This structure is used in rom and existing fields should not be changed */
typedef struct cipher_key_t {
	cipher_key_hdr_t hdr;

	union ckd {
		tkip_aes_key_data_t tkip_aes;
		wep_key_data_t wep;
		wapi_key_data_t wapi;
		eapolHskData_t hskData;
	} ckd;
} cipher_key_t;

typedef MLAN_PACK_START struct {
	UINT8 protocol_ver;
	IEEEtypes_8021x_PacketType_e pckt_type;
	UINT16 pckt_body_len;
} MLAN_PACK_END Hdr_8021x_t;

typedef MLAN_PACK_START struct {
	/* don't change this order.  It is set to match the
	 ** endianness of the message
	 */

	/* Byte 1 */
	UINT16 KeyMIC:1;	/* Bit  8     */
	UINT16 Secure:1;	/* Bit  9     */
	UINT16 Error:1;		/* Bit  10    */
	UINT16 Request:1;	/* Bit  11    */
	UINT16 EncryptedKeyData:1;	/* Bit  12    */
	UINT16 Reserved:3;	/* Bits 13-15 */

	/* Byte 0 */
	UINT16 KeyDescriptorVersion:3;	/* Bits 0-2   */
	UINT16 KeyType:1;	/* Bit  3     */
	UINT16 KeyIndex:2;	/* Bits 4-5   */
	UINT16 Install:1;	/* Bit  6     */
	UINT16 KeyAck:1;	/* Bit  7     */

} MLAN_PACK_END key_info_t;

#define KEY_DESCRIPTOR_HMAC_MD5_RC4  (1U << 0)
#define KEY_DESCRIPTOR_HMAC_SHA1_AES (1U << 1)
#define EAPOL_KeyMsg_Len  (100)
/* WPA2 GTK IE */
typedef MLAN_PACK_START struct {
	UINT8 KeyID:2;
	UINT8 Tx:1;
	UINT8 rsvd:5;
	UINT8 rsvd1;
	UINT8 GTK[1];
} MLAN_PACK_END GTK_KDE_t;

/* WPA2 Key Data */
typedef MLAN_PACK_START struct {
	UINT8 type;
	UINT8 length;
	UINT8 OUI[3];
	UINT8 dataType;
	UINT8 data[1];
} MLAN_PACK_END KDE_t;

typedef MLAN_PACK_START struct {
	uint8 llc[3];
	uint8 snap_oui[3];
	uint16 snap_type;
} MLAN_PACK_END llc_snap_t;

typedef MLAN_PACK_START struct {
	Hdr_8021x_t hdr_8021x;
	UINT8 desc_type;
	key_info_t key_info;
	UINT16 key_length;
	UINT32 replay_cnt[2];
	UINT8 key_nonce[NONCE_SIZE];	/*32 bytes */
	UINT8 EAPOL_key_IV[16];
	UINT8 key_RSC[8];
	UINT8 key_ID[8];
	UINT8 key_MIC[EAPOL_MIC_KEY_SIZE];
	UINT16 key_material_len;
	UINT8 key_data[1];
} MLAN_PACK_END EAPOL_KeyMsg_t;

typedef MLAN_PACK_START struct {
	Hdr_8021x_t hdr_8021x;
	IEEEtypes_8021x_CodeType_e code;
	UINT8 identifier;
	UINT16 length;
	UINT8 data[1];
} MLAN_PACK_END EAP_PacketMsg_t;

typedef MLAN_PACK_START struct {
	ether_hdr_t ethHdr;
	EAPOL_KeyMsg_t keyMsg;
} MLAN_PACK_END EAPOL_KeyMsg_Tx_t;

#endif
