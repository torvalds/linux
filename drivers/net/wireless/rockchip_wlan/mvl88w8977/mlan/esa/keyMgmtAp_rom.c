/** @file keyMgmtAp_rom.c
 *
 *  @brief This file defines api for key managment
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
//Authenticator related function definitions
#include "wltypes.h"
#include "IEEE_types.h"

#include "hostsa_ext_def.h"
#include "authenticator.h"

#include "keyMgmtAp_rom.h"
#include "crypt_new_rom.h"
#include "keyCommonDef.h"
#include "pmkCache_rom.h"
#include "crypt_new_rom.h"
#include "rc4_rom.h"
#include "aes_cmac_rom.h"
#include "sha1.h"
#include "md5.h"
#include "mrvl_sha256_crypto.h"

UINT32
util_FindLowestBitSet(UINT32 val)
{
	UINT32 bitmap = 1;

	while (bitmap && (!(bitmap & val))) {
		bitmap <<= 1;
	}

	return bitmap;
}

UINT8
convertMrvlAuthToIEEEAuth(UINT32 mrvlauth)
{
	UINT8 auth;

	switch (mrvlauth) {
	case UAP_HOSTCMD_KEYMGMT_EAP:
		auth = IEEEtypes_RSN_AUTH_KEY_SUITE_8021X;
		break;
	case UAP_HOSTCMD_KEYMGMT_PSK:
		auth = IEEEtypes_RSN_AUTH_KEY_SUITE_PSK;
		break;
	case UAP_HOSTCMD_KEYMGMT_NONE:
	default:
		auth = IEEEtypes_RSN_AUTH_KEY_SUITE_RSVD;
		break;
	}
	return auth;
}

UINT32
convertIEEEAuthToMrvlAuth(UINT8 auth)
{
	UINT32 MrvlAuth = 0;
	switch (auth) {
	case IEEEtypes_RSN_AUTH_KEY_SUITE_8021X:
		MrvlAuth |= UAP_HOSTCMD_KEYMGMT_EAP;
		break;
	case IEEEtypes_RSN_AUTH_KEY_SUITE_PSK:
		MrvlAuth |= UAP_HOSTCMD_KEYMGMT_PSK;
		break;
	case IEEEtypes_RSN_AUTH_KEY_SUITE_RSVD:
	default:
		MrvlAuth = 0;
		break;
	}
	return MrvlAuth;
}

UINT8
convertMrvlCipherToIEEECipher(UINT8 mrvlcipher)
{
	UINT8 Cipher;
	switch (mrvlcipher) {
	case UAP_HOSTCMD_CIPHER_WEP40:
		Cipher = IEEEtypes_RSN_CIPHER_SUITE_WEP40;
		break;
	case UAP_HOSTCMD_CIPHER_WEP104:
		Cipher = IEEEtypes_RSN_CIPHER_SUITE_WEP104;
		break;
	case UAP_HOSTCMD_CIPHER_TKIP:
		Cipher = IEEEtypes_RSN_CIPHER_SUITE_TKIP;
		break;
	case UAP_HOSTCMD_CIPHER_CCMP:
		Cipher = IEEEtypes_RSN_CIPHER_SUITE_CCMP;
		break;
	default:
		Cipher = IEEEtypes_RSN_CIPHER_SUITE_NONE;
		break;
	}
	return Cipher;
}

UINT32
convertIEEECipherToMrvlCipher(UINT8 cipher)
{
	UINT32 MrvlCipher = 0;
	switch (cipher) {
	case IEEEtypes_RSN_CIPHER_SUITE_WEP40:
		MrvlCipher |= UAP_HOSTCMD_CIPHER_WEP40;
		break;
	case IEEEtypes_RSN_CIPHER_SUITE_TKIP:
		MrvlCipher |= UAP_HOSTCMD_CIPHER_TKIP;
		break;
	case IEEEtypes_RSN_CIPHER_SUITE_CCMP:
		MrvlCipher |= UAP_HOSTCMD_CIPHER_CCMP;
		break;
	case IEEEtypes_RSN_CIPHER_SUITE_WEP104:
		MrvlCipher |= UAP_HOSTCMD_CIPHER_WEP104;
		break;
	case IEEEtypes_RSN_CIPHER_SUITE_NONE:
	case IEEEtypes_RSN_CIPHER_SUITE_WRAP:
	default:
		MrvlCipher = 0;
		break;
	}
	return MrvlCipher;
}

void
GenerateGTK_internal(hostsa_private *priv, KeyData_t *grpKeyData,
		     UINT8 *nonce, UINT8 *StaMacAddr)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 inp_data[NONCE_SIZE + sizeof(IEEEtypes_MacAddr_t)];
	UINT8 prefix[] = "Group key expansion";
	UINT8 GTK[32];		//group transient key
	UINT8 grpMasterKey[32];

	if (!grpKeyData || !nonce) {
		return;
	}

	memcpy(util_fns, inp_data, StaMacAddr, sizeof(IEEEtypes_MacAddr_t));
	supplicantGenerateRand(priv, nonce, NONCE_SIZE);
	memcpy(util_fns, inp_data + sizeof(IEEEtypes_MacAddr_t), nonce,
	       NONCE_SIZE);
	supplicantGenerateRand(priv, grpMasterKey, sizeof(grpMasterKey));
	Mrvl_PRF((void *)priv, grpMasterKey,
		 sizeof(grpMasterKey),
		 prefix,
		 wlan_strlen((char *)prefix),
		 inp_data, sizeof(inp_data), GTK, sizeof(GTK));
	memcpy(util_fns, grpKeyData->Key, GTK, TK_SIZE);
	memcpy(util_fns, grpKeyData->TxMICKey, GTK + TK_SIZE, MIC_KEY_SIZE);
	memcpy(util_fns, grpKeyData->RxMICKey, GTK + TK_SIZE + MIC_KEY_SIZE,
	       MIC_KEY_SIZE);

}

/*
    Populates EAPOL frame based on Cipher, given Nonce, replay counters,
    and type, which encodes whether this is secure, part of WPA2 or WPA
    handshake.
    This is currently used for EAPOL sent from AP, msg1, msg3 and group
    key msg.
*/
void
PopulateKeyMsg(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *tx_eapol_ptr,
	       Cipher_t *Cipher,
	       UINT16 Type, UINT32 replay_cnt[2], UINT8 *Nonce)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	key_info_t *pKeyInfo;

	if (!tx_eapol_ptr || !Cipher) {
		return;
	}

	pKeyInfo = &tx_eapol_ptr->keyMsg.key_info;

	if (Cipher->tkip) {
		//TKIP unicast
		tx_eapol_ptr->keyMsg.key_length =
			SHORT_SWAP((TK_SIZE + TK_SIZE));
	} else if (Cipher->ccmp) {
		//CCMP unicast
		tx_eapol_ptr->keyMsg.key_length = SHORT_SWAP(TK_SIZE);
	}

	pKeyInfo->KeyAck = 1;

	if (Type & PAIRWISE_KEY_MSG) {
		pKeyInfo->KeyType = 1;
		if (Type & SECURE_HANDSHAKE_FLAG) {
			pKeyInfo->KeyMIC = 1;
			pKeyInfo->Install = 1;
			pKeyInfo->EncryptedKeyData = pKeyInfo->Secure =
				(Type & WPA2_HANDSHAKE) ? 1 : 0;
		}
	} else {
		pKeyInfo->Secure = 1;
		pKeyInfo->KeyMIC = 1;
		pKeyInfo->EncryptedKeyData = (Type & WPA2_HANDSHAKE) ? 1 : 0;
	}

	tx_eapol_ptr->keyMsg.replay_cnt[0] = WORD_SWAP(replay_cnt[0]);
	tx_eapol_ptr->keyMsg.replay_cnt[1] = WORD_SWAP(replay_cnt[1]);
	memcpy(util_fns, (void *)tx_eapol_ptr->keyMsg.key_nonce, Nonce,
	       NONCE_SIZE);

	DBG_HEXDUMP(MCMD_D, " nonce ",
		    (t_u8 *)tx_eapol_ptr->keyMsg.key_nonce, NONCE_SIZE);
}

/*
    Function to prepare KDE in EAPOL frame .
    Used by the AP to encapsulate GTK
*/

void
prepareKDE(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *tx_eapol_ptr,
	   KeyData_t *grKey, Cipher_t *cipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	KDE_t *pKeyDataWPA2;
	GTK_KDE_t *pGTK_IE;
	UINT8 RsnIE_len = 0, PadLen = 0;
	UINT8 *buf_p;

	if (!tx_eapol_ptr || !grKey || !cipher) {
		return;
	}

	RsnIE_len = tx_eapol_ptr->keyMsg.key_material_len;
	buf_p = (UINT8 *)(tx_eapol_ptr->keyMsg.key_data + RsnIE_len);

	pKeyDataWPA2 = (KDE_t *)buf_p;
	pKeyDataWPA2->type = 0xdd;
	pKeyDataWPA2->length = KEYDATA_SIZE;
	pKeyDataWPA2->OUI[0] = kde_oui[0];
	pKeyDataWPA2->OUI[1] = kde_oui[1];
	pKeyDataWPA2->OUI[2] = kde_oui[2];
	pKeyDataWPA2->dataType = 1;
	buf_p = buf_p + KDE_SIZE;

	pGTK_IE = (GTK_KDE_t *)buf_p;
	pGTK_IE->KeyID = 1;
	buf_p = buf_p + GTK_IE_SIZE;

	// copy over GTK
	memcpy(util_fns, (void *)buf_p, (void *)grKey->Key, TK_SIZE);
	buf_p = buf_p + TK_SIZE;

	if (cipher->tkip) {
		pKeyDataWPA2->length += (MIC_KEY_SIZE + MIC_KEY_SIZE);
		memcpy(util_fns, (void *)buf_p, (void *)grKey->TxMICKey,
		       MIC_KEY_SIZE);
		buf_p = buf_p + MIC_KEY_SIZE;
		memcpy(util_fns, (void *)buf_p, (void *)grKey->RxMICKey,
		       MIC_KEY_SIZE);
		buf_p = buf_p + MIC_KEY_SIZE;
	}

	tx_eapol_ptr->keyMsg.key_material_len += (pKeyDataWPA2->length
						  + KDE_IE_SIZE);

	PadLen = ((8 - ((tx_eapol_ptr->keyMsg.key_material_len) % 8)) % 8);
	if (PadLen) {
		*(buf_p) = 0xdd;
		memset(util_fns, (void *)(buf_p + 1), 0, PadLen - 1);
		tx_eapol_ptr->keyMsg.key_material_len += PadLen;
	}

}

BOOLEAN
Encrypt_keyData(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *tx_eapol_ptr,
		UINT8 *EAPOL_Encr_Key, Cipher_t *cipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 key[16];
	UINT8 cipherText[400] = { 0 };

	if (!tx_eapol_ptr || !EAPOL_Encr_Key || !cipher) {
		return FALSE;
	}

	if (cipher->ccmp) {
		// Pairwise is CCMP
		memcpy(util_fns, (void *)key, EAPOL_Encr_Key, 16);

		// use AES-only mode from AEU HW to perform AES wrap
		MRVL_AesWrap(key, 2, tx_eapol_ptr->keyMsg.key_material_len / 8,
			     tx_eapol_ptr->keyMsg.key_data, NULL, cipherText);

		tx_eapol_ptr->keyMsg.key_material_len += 8;
		memcpy(util_fns, (void *)tx_eapol_ptr->keyMsg.key_data,
		       cipherText, tx_eapol_ptr->keyMsg.key_material_len);
	} else if (cipher->tkip) {
		// Pairwise is TKIP
		supplicantGenerateRand(priv,
				       (UINT8 *)tx_eapol_ptr->keyMsg.
				       EAPOL_key_IV, 16);
		RC4_Encrypt((t_void *)priv, (unsigned char *)EAPOL_Encr_Key,
			    (unsigned char *)&tx_eapol_ptr->keyMsg.EAPOL_key_IV,
			    16, (unsigned char *)&tx_eapol_ptr->keyMsg.key_data,
			    (unsigned short)tx_eapol_ptr->keyMsg.
			    key_material_len, 256);
	} else {
		return FALSE;
	}

	return TRUE;
}

void
KeyMgmtAp_DerivePTK(hostsa_private *priv,
		    UINT8 *pPMK,
		    t_u8 *da,
		    t_u8 *sa,
		    UINT8 *ANonce,
		    UINT8 *SNonce,
		    UINT8 *EAPOL_MIC_Key,
		    UINT8 *EAPOL_Encr_Key, KeyData_t *newPWKey, BOOLEAN use_kdf)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 tmp[MIC_KEY_SIZE];

	// call STA PTK generation funciton first
	KeyMgmtSta_DeriveKeys(priv, pPMK,
			      da,
			      sa,
			      ANonce,
			      SNonce,
			      EAPOL_MIC_Key, EAPOL_Encr_Key, newPWKey, use_kdf);

	// We need to swap Rx/Tx Keys for AP

	memcpy(util_fns, tmp, newPWKey->RxMICKey, MIC_KEY_SIZE);
	memcpy(util_fns, newPWKey->RxMICKey, newPWKey->TxMICKey, MIC_KEY_SIZE);
	memcpy(util_fns, newPWKey->TxMICKey, tmp, MIC_KEY_SIZE);

}

BOOLEAN
KeyData_CopyWPAWP2(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL, void *pIe)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	IEEEtypes_InfoElementHdr_t *pElement =
		(IEEEtypes_InfoElementHdr_t *)pIe;

	if (!pIe) {
		return FALSE;
	}

	pTxEAPOL->keyMsg.key_material_len =
		pElement->Len + sizeof(IEEEtypes_InfoElementHdr_t);

	memcpy(util_fns, (void *)pTxEAPOL->keyMsg.key_data,
	       pIe, pTxEAPOL->keyMsg.key_material_len);

	return TRUE;
}

BOOLEAN
KeyData_UpdateKeyMaterial(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL,
			  SecurityMode_t *pSecType, void *pWPA, void *pWPA2)
{

	if (pSecType->wpa || pSecType->wpaNone) {
		if (KeyData_CopyWPAWP2(priv, pTxEAPOL, pWPA) == FALSE) {
			return FALSE;
		}
	} else if (pSecType->wpa2) {
		if (KeyData_CopyWPAWP2(priv, pTxEAPOL, pWPA2) == FALSE) {
			return FALSE;
		}
	}

	return TRUE;
}

void
KeyData_AddGTK(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL,
	       KeyData_t *grKey, Cipher_t *cipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 *buf_p;
	buf_p = (UINT8 *)pTxEAPOL->keyMsg.key_data;
	memcpy(util_fns, (void *)buf_p, (void *)grKey, TK_SIZE);

	buf_p = buf_p + TK_SIZE;

	pTxEAPOL->keyMsg.key_material_len += TK_SIZE;

	if (cipher->tkip) {
		memcpy(util_fns, (void *)buf_p, (void *)grKey->TxMICKey,
		       MIC_KEY_SIZE);
		buf_p = buf_p + MIC_KEY_SIZE;
		memcpy(util_fns, (void *)buf_p, (void *)grKey->RxMICKey,
		       MIC_KEY_SIZE);
		pTxEAPOL->keyMsg.key_material_len += (MIC_KEY_SIZE +
						      MIC_KEY_SIZE);
	}
}

/* Returns FALSE if security type is other than WPA* */
BOOLEAN
KeyData_AddKey(hostsa_private *priv, EAPOL_KeyMsg_Tx_t *pTxEAPOL,
	       SecurityMode_t *pSecType, KeyData_t *grKey, Cipher_t *cipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	BOOLEAN status = FALSE;

	pTxEAPOL->keyMsg.key_info.KeyIndex = grKey->KeyIndex;

	pTxEAPOL->keyMsg.key_RSC[0] = grKey->TxIV16 & 0x00FF;
	pTxEAPOL->keyMsg.key_RSC[1] = (grKey->TxIV16 >> 8) & 0x00FF;
	memcpy(util_fns, (void *)(pTxEAPOL->keyMsg.key_RSC + 2),
	       &grKey->TxIV32, 4);

	if (pSecType->wpa || pSecType->wpaNone) {
		KeyData_AddGTK(priv, pTxEAPOL, grKey, cipher);
		status = TRUE;
	} else if (pSecType->wpa2) {
		prepareKDE(priv, pTxEAPOL, grKey, cipher);
		status = TRUE;
	}
	return status;
}

void
ROM_InitGTK(hostsa_private *priv, KeyData_t *grpKeyData, UINT8 *nonce,
	    UINT8 *StaMacAddr)
{
	grpKeyData->KeyIndex = 1;
	grpKeyData->TxIV16 = 1;
	grpKeyData->TxIV32 = 0;

	GenerateGTK_internal(priv, grpKeyData, nonce, StaMacAddr);
}

t_u16
keyMgmtAp_FormatWPARSN_IE_internal(phostsa_private priv,
				   IEEEtypes_InfoElementHdr_t *pIe,
				   UINT8 isRsn,
				   Cipher_t *pCipher,
				   UINT8 cipherCnt,
				   Cipher_t *pMcastCipher,
				   UINT16 authKey, UINT16 authKeyCnt)
{
	phostsa_util_fns util_fns = &priv->util_fns;
	int i;
	UINT8 *pBuf = NULL;
	IEEEtypes_RSNElement_t *pRsn = (IEEEtypes_RSNElement_t *)pIe;
	IEEEtypes_WPAElement_t *pWpa = (IEEEtypes_WPAElement_t *)pIe;

	UINT16 bitPos = 0;
	UINT16 authKeyBitmap = authKey;
	UINT8 ucastBitmap = *((UINT8 *)pCipher);
	UINT8 mcastBitmap = *((UINT8 *)pMcastCipher);
	UINT8 oui[3];
	UINT16 ieLength = 0;

	pIe->ElementId = (isRsn == 1) ? ELEM_ID_RSN : ELEM_ID_VENDOR_SPECIFIC;

	if (isRsn) {
		memcpy(util_fns, (void *)&oui, (void *)&kde_oui, sizeof(oui));
		pBuf = (UINT8 *)&pRsn->Ver;
	} else {
		memcpy(util_fns, (void *)&oui, (void *)&wpa_oui01, sizeof(oui));
		memcpy(util_fns, (void *)&pWpa->OuiType, (void *)&wpa_oui01,
		       sizeof(wpa_oui01));
		pBuf = (UINT8 *)&pWpa->Ver;
	}

	pBuf[0] = 0x1;
	pBuf[1] = 0x0;
	pBuf += 2;

	//filling group cipher
	memcpy(util_fns, (void *)pBuf, (void *)&oui, sizeof(oui));

	if (mcastBitmap) {
		bitPos = util_FindLowestBitSet(mcastBitmap);
	}

	pBuf[3] = convertMrvlCipherToIEEECipher(bitPos);
	pBuf += 4;

	pBuf[0] = (cipherCnt >> 0) & 0xFF;
	pBuf[1] = (cipherCnt >> 16) & 0xFF;
	pBuf += 2;

	for (i = 0; i < cipherCnt; i++) {
		pBuf[0] = oui[0];
		pBuf[1] = oui[1];
		pBuf[2] = oui[2];

		bitPos = util_FindLowestBitSet(ucastBitmap);

		pBuf[3] = convertMrvlCipherToIEEECipher(bitPos);

		ucastBitmap &= ~bitPos;

		pBuf += 4;
	}

	pBuf[0] = (authKeyCnt >> 0) & 0xFF;
	pBuf[1] = (authKeyCnt >> 16) & 0xFF;
	pBuf += 2;

	for (i = 0; i < authKeyCnt; i++) {
		pBuf[0] = oui[0];
		pBuf[1] = oui[1];
		pBuf[2] = oui[2];

		bitPos = util_FindLowestBitSet(authKeyBitmap);

		pBuf[3] = convertMrvlAuthToIEEEAuth(bitPos);

		authKeyBitmap &= ~bitPos;
		pBuf += 4;
	}

	if (isRsn) {
		pBuf[0] = 0x0;
		pBuf[1] = 0x0;
		pBuf += 2;
	}

	ieLength = (unsigned long)pBuf - (unsigned long)pIe;
	pIe->Len = ieLength - sizeof(IEEEtypes_InfoElementHdr_t);
	DBG_HEXDUMP(MCMD_D, "RSN or WPA IE", (t_u8 *)pIe, ieLength);
	return ieLength;
}

/* Ideally one day this function should re-use client code */
t_u16
keyMgmtAp_FormatWPARSN_IE(hostsa_private *priv,
			  IEEEtypes_InfoElementHdr_t *pIe,
			  UINT8 isRsn,
			  Cipher_t *pCipher,
			  UINT8 cipherCount,
			  Cipher_t *pMcastCipher,
			  UINT16 authKey, UINT16 authKeyCount)
{

	UINT16 ieLength;

	ieLength = keyMgmtAp_FormatWPARSN_IE_internal(priv, pIe,
						      isRsn,
						      pCipher,
						      cipherCount,
						      pMcastCipher,
						      authKey, authKeyCount);

	return ieLength;
}
