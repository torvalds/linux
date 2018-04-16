/** @file keyMgmtApStaCommon.c
 *
 *  @brief This file defines common api for authenticator and supplicant.
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

const UINT8 wpa_oui_none[4] = { 0x00, 0x50, 0xf2, 0x00 };
const UINT8 wpa_oui01[4] = { 0x00, 0x50, 0xf2, 0x01 };
const UINT8 wpa_oui02[4] = { 0x00, 0x50, 0xf2, 0x02 };
const UINT8 wpa_oui03[4] = { 0x00, 0x50, 0xf2, 0x03 };
const UINT8 wpa_oui04[4] = { 0x00, 0x50, 0xf2, 0x04 };
const UINT8 wpa_oui05[4] = { 0x00, 0x50, 0xf2, 0x05 };
const UINT8 wpa_oui06[4] = { 0x00, 0x50, 0xf2, 0x06 };

const UINT8 wpa2_oui01[4] = { 0x00, 0x0f, 0xac, 0x01 };
const UINT8 wpa2_oui02[4] = { 0x00, 0x0f, 0xac, 0x02 };
const UINT8 wpa2_oui03[4] = { 0x00, 0x0f, 0xac, 0x03 };
const UINT8 wpa2_oui04[4] = { 0x00, 0x0f, 0xac, 0x04 };
const UINT8 wpa2_oui05[4] = { 0x00, 0x0f, 0xac, 0x05 };
const UINT8 wpa2_oui06[4] = { 0x00, 0x0f, 0xac, 0x06 };

const UINT8 wpa_oui[3] = { 0x00, 0x50, 0xf2 };
const UINT8 kde_oui[3] = { 0x00, 0x0f, 0xac };

/**
 *  @brief strlen
 *
 *  @param str		        A pointer to string
 *
 *  @return                 Length of string
 */
t_u32
wlan_strlen(const char *str)
{
	t_u32 i;

	for (i = 0; str[i] != 0; i++) {
	}
	return i;
}

static t_u32
srand_new(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	t_u32 sec, usec;

	ENTER();
	get_system_time(util_fns, &sec, &usec);
	sec = (sec & 0xFFFF) + (sec >> 16);
	usec = (usec & 0xFFFF) + (usec >> 16);

	LEAVE();
	return (usec << 16) | sec;
}

static unsigned int
rand_new(unsigned int seed, UINT32 randvaule)
{
	unsigned int next = seed;
	unsigned int result;

	next *= (3515245 * randvaule + randvaule * next);
	next += 12345 + randvaule * 7;
	result = (unsigned int)(next / 65536) % 2048;

	next *= (39018768 * randvaule + randvaule * next);
	next += 56789 + randvaule * 4;
	result <<= 10;
	result ^= (unsigned int)(next / 65536) % 1024;

	next *= (89042053 * randvaule + randvaule * next);
	next += 43728 + randvaule * 9;
	result <<= 10;
	result ^= (unsigned int)(next / 65536) % 1024;

	return result;
}

void
supplicantGenerateRand(hostsa_private *priv, UINT8 *dataOut, UINT32 length)
{
	UINT32 i;
	//UINT32 valueHi, valueLo;

	/* Read mac 0 timer.
	 ** Doesn't matter which one we read. We just need a good seed.
	 */
	//msi_wl_GetMCUCoreTimerTxTSF(&valueHi, &valueLo);
	//srand(valueLo);
	for (i = 0; i < length; i++) {
		//dataOut[i] = rand();
		dataOut[i] = rand_new(srand_new(priv), i + 1);
	}
}

void
SetEAPOLKeyDescTypeVersion(EAPOL_KeyMsg_Tx_t *pTxEapol,
			   BOOLEAN isWPA2, BOOLEAN isKDF, BOOLEAN nonTKIP)
{
	if (isWPA2) {
		/* WPA2 */
		pTxEapol->keyMsg.desc_type = 2;
	} else {
		/* WPA */
		pTxEapol->keyMsg.desc_type = 254;
	}

	if (isKDF) {
		/* 802.11r and 802.11w use SHA256-KDF and a different KeyDescVer */
		pTxEapol->keyMsg.key_info.KeyDescriptorVersion = 3;
	} else if (nonTKIP) {
		/* CCMP */
		pTxEapol->keyMsg.key_info.KeyDescriptorVersion = 2;
	} else {
		/* TKIP OR WEP */
		pTxEapol->keyMsg.key_info.KeyDescriptorVersion = 1;
	}
}

void
ComputeEAPOL_MIC(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
		 UINT16 data_length,
		 UINT8 *MIC_Key, UINT8 MIC_Key_length, UINT8 micKeyDescVersion)
{
	int len = data_length;
	UINT8 *pMicData;

	pMicData = (UINT8 *)pKeyMsg;

	/* Allow the caller to override the algorithm used to get by some Cisco
	 **    CCX bugs where the wrong MIC algorithm is used
	 */
	if (micKeyDescVersion == 0) {
		/* Algorithm not specified, use proper one from key_info */
		micKeyDescVersion = pKeyMsg->key_info.KeyDescriptorVersion;
	}

	switch (micKeyDescVersion) {
	case 3:
		/* AES-128-CMAC */
		mrvl_aes_cmac(priv, MIC_Key, pMicData, len,
			      (UINT8 *)pKeyMsg->key_MIC);
		break;

	case 2:
		/* CCMP */
		Mrvl_hmac_sha1((t_void *)priv, &pMicData,
			       &len,
			       1,
			       MIC_Key,
			       (int)MIC_Key_length,
			       (UINT8 *)pKeyMsg->key_MIC, EAPOL_MIC_SIZE);
		break;

	default:
	case 1:
		/* TKIP  or WEP */
		Mrvl_hmac_md5((t_void *)priv, pMicData,
			      data_length,
			      MIC_Key,
			      (int)MIC_Key_length, (UINT8 *)pKeyMsg->key_MIC);
		break;
	}
}

/* Returns TRUE if EAPOL MIC check passes, FALSE otherwise. */
BOOLEAN
IsEAPOL_MICValid(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg, UINT8 *pMICKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 msgMIC[EAPOL_MIC_SIZE];

	/* pull the MIC */
	memcpy(util_fns, (UINT8 *)msgMIC, (UINT8 *)pKeyMsg->key_MIC,
	       EAPOL_MIC_SIZE);

	/* zero the MIC key field before calculating the data */
	memset(util_fns, (UINT8 *)pKeyMsg->key_MIC, 0x00, EAPOL_MIC_SIZE);

	ComputeEAPOL_MIC(priv, pKeyMsg, (ntohs(pKeyMsg->hdr_8021x.pckt_body_len)
					 + sizeof(pKeyMsg->hdr_8021x)),
			 pMICKey, EAPOL_MIC_KEY_SIZE, 0);

	if (memcmp(util_fns, (UINT8 *)pKeyMsg->key_MIC, msgMIC, EAPOL_MIC_SIZE)) {
#ifdef KEYMSG_DEBUG
		hostEventPrintf(assocAgent_getConnPtr(),
				"EAPOL MIC Failure: cmac(%d), sha1(%d), md5(%d)",
				(pKeyMsg->key_info.KeyDescriptorVersion == 3),
				(pKeyMsg->key_info.KeyDescriptorVersion == 2),
				(pKeyMsg->key_info.KeyDescriptorVersion == 1));
#endif
		/* MIC Failure */
		return FALSE;
	}

	return TRUE;
}

void
supplicantConstructContext(phostsa_private priv, UINT8 *pAddr1,
			   UINT8 *pAddr2,
			   UINT8 *pNonce1, UINT8 *pNonce2, UINT8 *pContext)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	if (memcmp(util_fns, pAddr1, pAddr2, 6) < 0) {
		memcpy(util_fns, pContext, pAddr1, sizeof(IEEEtypes_MacAddr_t));
		memcpy(util_fns, (pContext + 6), pAddr2,
		       sizeof(IEEEtypes_MacAddr_t));
	} else {
		memcpy(util_fns, pContext, pAddr2, sizeof(IEEEtypes_MacAddr_t));
		memcpy(util_fns, (pContext + 6), pAddr1,
		       sizeof(IEEEtypes_MacAddr_t));
	}

	if (memcmp(util_fns, pNonce1, pNonce2, NONCE_SIZE) < 0) {
		memcpy(util_fns, pContext + 6 + 6, pNonce1, NONCE_SIZE);
		memcpy(util_fns, pContext + 6 + 6 + NONCE_SIZE, pNonce2,
		       NONCE_SIZE);
	} else {
		memcpy(util_fns, pContext + 6 + 6, pNonce2, NONCE_SIZE);
		memcpy(util_fns, pContext + 6 + 6 + NONCE_SIZE, pNonce1,
		       NONCE_SIZE);
	}
}

UINT16
KeyMgmtSta_PopulateEAPOLLengthMic(phostsa_private priv,
				  EAPOL_KeyMsg_Tx_t *pTxEapol,
				  UINT8 *pEAPOLMICKey,
				  UINT8 eapolProtocolVersion,
				  UINT8 forceKeyDescVersion)
{
	UINT16 frameLen;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (KeyMgmtSta_PopulateEAPOLLengthMic_hook(pTxEapol,
						   pEAPOLMICKey,
						   eapolProtocolVersion,
						   forceKeyDescVersion,
						   &frameLen)) {
		return frameLen;
	}
#endif

	if (!pTxEapol) {
		return 0;
	}

	frameLen = sizeof(pTxEapol->keyMsg);
	frameLen -= sizeof(pTxEapol->keyMsg.hdr_8021x);
	frameLen -= sizeof(pTxEapol->keyMsg.key_data);
	frameLen += pTxEapol->keyMsg.key_material_len;

	pTxEapol->keyMsg.hdr_8021x.protocol_ver = eapolProtocolVersion;
	pTxEapol->keyMsg.hdr_8021x.pckt_type = IEEE_8021X_PACKET_TYPE_EAPOL_KEY;
	pTxEapol->keyMsg.hdr_8021x.pckt_body_len = htons(frameLen);

	pTxEapol->keyMsg.key_material_len
		= htons(pTxEapol->keyMsg.key_material_len);

	ComputeEAPOL_MIC(priv, &pTxEapol->keyMsg,
			 frameLen + sizeof(pTxEapol->keyMsg.hdr_8021x),
			 pEAPOLMICKey, EAPOL_MIC_KEY_SIZE, forceKeyDescVersion);

	return frameLen;
}

/*
**  This function generates the Pairwise transient key
*/
void
KeyMgmt_DerivePTK(phostsa_private priv, UINT8 *pAddr1,
		  UINT8 *pAddr2,
		  UINT8 *pNonce1,
		  UINT8 *pNonce2, UINT8 *pPTK, UINT8 *pPMK, BOOLEAN use_kdf)
{
	UINT8 *pContext;
	char *prefix;

	/* pPTK is expected to be an encryption pool buffer (at least 500 bytes).
	 **
	 ** Use the first portion for the ptk output.  Use memory in the end of
	 **   the buffer for the context construction (76 bytes).
	 **
	 ** The sha256 routine assumes available memory after the context for its
	 **   own sha256 output.  Space after the context (76 bytes) is required
	 **   for 2 digests (2 * 32).  pContext must have at least 76 + 64 bytes
	 **   available.
	 */
	pContext = pPTK + 200;

	supplicantConstructContext(priv, pAddr1, pAddr2, pNonce1, pNonce2,
				   pContext);

	prefix = "Pairwise key expansion";

	if (use_kdf) {
		mrvl_sha256_crypto_kdf((t_void *)priv, pPMK, PMK_LEN_MAX, prefix, 22,	/* strlen(prefix) */
				       pContext, 76,	/* sizeof constructed context */
				       pPTK, 384);
	} else {
		Mrvl_PRF((void *)priv, pPMK, PMK_LEN_MAX, (UINT8 *)prefix, 22,	/* strlen(prefix) */
			 pContext, 76,	/* sizeof constructed context */
			 pPTK, 64);
	}
}

void
KeyMgmtSta_DeriveKeys(hostsa_private *priv, UINT8 *pPMK,
		      UINT8 *da,
		      UINT8 *sa,
		      UINT8 *ANonce,
		      UINT8 *SNonce,
		      UINT8 *EAPOL_MIC_Key,
		      UINT8 *EAPOL_Encr_Key,
		      KeyData_t *newPWKey, BOOLEAN use_kdf)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
//    phostsa_private psapriv = (phostsa_private) priv;
	//   hostsa_util_fns  *util_fns = &psapriv->util_fns;
	//BufferDesc_t* pBufDesc = NULL;
	UINT8 buf[500] = { 0 };
	TkipPtk_t *pPtk;

#if 0
#if !defined(REMOVE_PATCH_HOOKS)
	if (KeyMgmtSta_DeriveKeys_hook(pPMK,
				       da,
				       sa,
				       ANonce,
				       SNonce,
				       EAPOL_MIC_Key,
				       EAPOL_Encr_Key, newPWKey, use_kdf)) {
		return;
	}
#endif
#endif
	if (!pPMK || !EAPOL_MIC_Key || !newPWKey) {
		return;
	}
#if 0
	/* Wait forever ensures a buffer */
	pBufDesc = (BufferDesc_t *) bml_AllocBuffer(ramHook_encrPoolConfig,
						    500, BML_WAIT_FOREVER);
	pPtk = (TkipPtk_t *)BML_DATA_PTR(pBufDesc);
#endif
	pPtk = (TkipPtk_t *)buf;

	KeyMgmt_DerivePTK(priv, sa, da, ANonce, SNonce, (UINT8 *)pPtk, pPMK,
			  use_kdf);

	memcpy(util_fns, EAPOL_MIC_Key, pPtk->kck, sizeof(pPtk->kck));
	memcpy(util_fns, EAPOL_Encr_Key, pPtk->kek, sizeof(pPtk->kek));
	memcpy(util_fns, newPWKey->Key, pPtk->tk, sizeof(pPtk->tk));

	memcpy(util_fns, newPWKey->RxMICKey,
	       pPtk->rxMicKey, sizeof(pPtk->rxMicKey));

	memcpy(util_fns, newPWKey->TxMICKey,
	       pPtk->txMicKey, sizeof(pPtk->txMicKey));

//    bml_FreeBuffer((UINT32)pBufDesc);
}

void
UpdateEAPOLWcbLenAndTransmit(hostsa_private *priv, pmlan_buffer pmbuf,
			     UINT16 frameLen)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;

	pm_fns->hostsa_tx_packet(priv->pmlan_private, pmbuf, frameLen);
}

void
formEAPOLEthHdr(phostsa_private priv, EAPOL_KeyMsg_Tx_t *pTxEapol,
		t_u8 *da, t_u8 *sa)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	memcpy(util_fns, (void *)pTxEapol->ethHdr.da, da,
	       IEEEtypes_ADDRESS_SIZE);
	memcpy(util_fns, (void *)pTxEapol->ethHdr.sa, sa,
	       IEEEtypes_ADDRESS_SIZE);
	pTxEapol->ethHdr.type = 0x8E88;
}

void
supplicantParseWpaIe(phostsa_private priv, IEEEtypes_WPAElement_t *pIe,
		     SecurityMode_t *pWpaType,
		     Cipher_t *pMcstCipher,
		     Cipher_t *pUcstCipher,
		     AkmSuite_t *pAkmList, UINT8 akmOutMax)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	IEEEtypes_WPAElement_t *pTemp = pIe;
	int count;
	int akmCount = akmOutMax;
	AkmSuite_t *pAkm = pAkmList;

	memset(util_fns, pMcstCipher, 0x00, sizeof(Cipher_t));
	memset(util_fns, pUcstCipher, 0x00, sizeof(Cipher_t));
	memset(util_fns, pAkmList, 0x00, akmOutMax * sizeof(AkmSuite_t));
	memset(util_fns, pWpaType, 0x00, sizeof(SecurityMode_t));

	pWpaType->wpa = 1;

	/* record the AP's multicast cipher */
	if (!memcmp
	    (util_fns, (char *)pTemp->GrpKeyCipher, wpa_oui02,
	     sizeof(wpa_oui02))) {
		/* WPA TKIP */
		pMcstCipher->tkip = 1;
	} else if (!memcmp
		   (util_fns, (char *)pTemp->GrpKeyCipher, wpa_oui04,
		    sizeof(wpa_oui04))) {
		/* WPA AES */
		pMcstCipher->ccmp = 1;
	} else if (!memcmp
		   (util_fns, (char *)pTemp->GrpKeyCipher, wpa_oui01,
		    sizeof(wpa_oui01))) {
		/* WPA WEP 40 */
		pMcstCipher->wep40 = 1;
	} else if (!memcmp
		   (util_fns, (char *)pTemp->GrpKeyCipher, wpa_oui05,
		    sizeof(wpa_oui05))) {
		/* WPA WEP 104 */
		pMcstCipher->wep104 = 1;
	}

	count = wlan_le16_to_cpu(pTemp->PwsKeyCnt);

	while (count) {
		/* record the AP's unicast cipher */
		if (!memcmp(util_fns, (char *)pTemp->PwsKeyCipherList,
			    wpa_oui02, sizeof(wpa_oui02))) {
			/* WPA TKIP */
			pUcstCipher->tkip = 1;
		} else if (!memcmp(util_fns, (char *)pTemp->PwsKeyCipherList,
				   wpa_oui04, sizeof(wpa_oui04))) {
			/* WPA AES */
			pUcstCipher->ccmp = 1;
		}
		count--;

		if (count) {
			pTemp = (IEEEtypes_WPAElement_t *)((UINT8 *)pTemp +
							   sizeof(pTemp->
								  PwsKeyCipherList));
		}
	}

	count = wlan_le16_to_cpu(pTemp->AuthKeyCnt);

	while (count) {
		if (akmCount) {
			/* Store the AKM */
			memcpy(util_fns, pAkm,
			       (char *)pTemp->AuthKeyList,
			       sizeof(pTemp->AuthKeyList));
			pAkm++;
			akmCount--;
		}

		count--;

		if (count) {
			pTemp = (IEEEtypes_WPAElement_t *)((UINT8 *)pTemp
							   +
							   sizeof(pTemp->
								  AuthKeyList));
		}
	}

	if (!memcmp(util_fns, pAkmList, wpa_oui_none, sizeof(wpa_oui_none))) {
		pWpaType->wpaNone = 1;
	}
}

void
supplicantParseMcstCipher(phostsa_private priv, Cipher_t *pMcstCipherOut,
			  UINT8 *pGrpKeyCipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	memset(util_fns, pMcstCipherOut, 0x00, sizeof(Cipher_t));

	/* record the AP's multicast cipher */
	if (!memcmp(util_fns, pGrpKeyCipher, wpa2_oui02, sizeof(wpa2_oui02))) {
		/* WPA2 TKIP */
		pMcstCipherOut->tkip = 1;
	} else if (!memcmp
		   (util_fns, pGrpKeyCipher, wpa2_oui04, sizeof(wpa2_oui04))) {
		/* WPA2 AES */
		pMcstCipherOut->ccmp = 1;
	} else if (!memcmp
		   (util_fns, pGrpKeyCipher, wpa2_oui01, sizeof(wpa2_oui01))) {
		/* WPA2 WEP 40 */
		pMcstCipherOut->wep40 = 1;
	} else if (!memcmp
		   (util_fns, pGrpKeyCipher, wpa2_oui05, sizeof(wpa2_oui05))) {
		/* WPA2 WEP 104 */
		pMcstCipherOut->wep104 = 1;
	}
}

void
supplicantParseUcstCipher(phostsa_private priv, Cipher_t *pUcstCipherOut,
			  UINT8 pwsKeyCnt, UINT8 *pPwsKeyCipherList)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 count;

	memset(util_fns, pUcstCipherOut, 0x00, sizeof(Cipher_t));

	/* Cycle through the PwsKeyCipherList and record each unicast cipher */
	for (count = 0; count < pwsKeyCnt; count++) {
		/* record the AP's unicast cipher */
		if (!memcmp(util_fns, pPwsKeyCipherList + (count * 4),
			    wpa2_oui02, sizeof(wpa2_oui02))) {
			/* WPA2 TKIP */
			pUcstCipherOut->tkip = 1;
		} else if (!memcmp(util_fns, pPwsKeyCipherList + (count * 4),
				   wpa2_oui04, sizeof(wpa2_oui04))) {
			/* WPA2 AES */
			pUcstCipherOut->ccmp = 1;
		}
	}
}

void
supplicantParseRsnIe(phostsa_private priv, IEEEtypes_RSNElement_t *pRsnIe,
		     SecurityMode_t *pWpaTypeOut,
		     Cipher_t *pMcstCipherOut,
		     Cipher_t *pUcstCipherOut,
		     AkmSuite_t *pAkmListOut,
		     UINT8 akmOutMax,
		     IEEEtypes_RSNCapability_t *pRsnCapOut,
		     Cipher_t *pGrpMgmtCipherOut)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 *pIeData;
	UINT8 *pIeEnd;
	UINT8 *pGrpKeyCipher;
	UINT16 pwsKeyCnt;
	UINT8 *pPwsKeyCipherList;
	UINT16 authKeyCnt;
	UINT8 *pAuthKeyList;

	IEEEtypes_RSNCapability_t *pRsnCap;

	UINT16 *pPMKIDCnt;
	UINT16 PMKIDCnt;

	UINT8 *pGrpMgmtCipher;
#if 0
#if !defined(REMOVE_PATCH_HOOKS)
	if (supplicantParseRsnIe_hook(pRsnIe,
				      pWpaTypeOut,
				      pMcstCipherOut,
				      pUcstCipherOut,
				      pAkmListOut,
				      akmOutMax,
				      pRsnCapOut, pGrpMgmtCipherOut)) {
		return;
	}
#endif
#endif
	memset(util_fns, pWpaTypeOut, 0x00, sizeof(SecurityMode_t));

	pWpaTypeOut->wpa2 = 1;

	/* Set the start and end of the IE data */
	pIeData = (UINT8 *)&pRsnIe->Ver;
	pIeEnd = pIeData + pRsnIe->Len;

	/* Skip past the version field */
	pIeData += sizeof(pRsnIe->Ver);

	/* Parse the group key cipher list */
	pGrpKeyCipher = pIeData;
	pIeData += sizeof(pRsnIe->GrpKeyCipher);
	supplicantParseMcstCipher(priv, pMcstCipherOut, pGrpKeyCipher);

	/* Parse the pairwise key cipher list */
	pwsKeyCnt = wlan_le16_to_cpu(*(UINT16 *)pIeData);
	pIeData += sizeof(pRsnIe->PwsKeyCnt);

	pPwsKeyCipherList = pIeData;
	pIeData += pwsKeyCnt * sizeof(pRsnIe->PwsKeyCipherList);
	supplicantParseUcstCipher(priv, pUcstCipherOut, pwsKeyCnt,
				  pPwsKeyCipherList);

	/* Parse and return the AKM list */
	authKeyCnt = wlan_le16_to_cpu(*(UINT16 *)pIeData);
	pIeData += sizeof(pRsnIe->AuthKeyCnt);

	pAuthKeyList = pIeData;
	pIeData += authKeyCnt * sizeof(pRsnIe->AuthKeyList);
	memset(util_fns, pAkmListOut, 0x00, akmOutMax * sizeof(AkmSuite_t));
	memcpy(util_fns, pAkmListOut,
	       pAuthKeyList,
	       MIN(authKeyCnt, akmOutMax) * sizeof(pRsnIe->AuthKeyList));

	DBG_HEXDUMP(MCMD_D, " pAuthKeyList",
		    (t_u8 *)pAuthKeyList, MIN(authKeyCnt,
					      akmOutMax) *
		    sizeof(pRsnIe->AuthKeyList));
	DBG_HEXDUMP(MCMD_D, " pAuthKeyList", (t_u8 *)pAkmListOut,
		    MIN(authKeyCnt, akmOutMax) * sizeof(pRsnIe->AuthKeyList));
	/* Check if the RSN Capability is included */
	if (pIeData < pIeEnd) {
		pRsnCap = (IEEEtypes_RSNCapability_t *)pIeData;
		pIeData += sizeof(pRsnIe->RsnCap);

		if (pRsnCapOut) {
			memcpy(util_fns, pRsnCapOut, pRsnCap,
			       sizeof(IEEEtypes_RSNCapability_t));
		}
	}

	/* Check if the PMKID count is included */
	if (pIeData < pIeEnd) {
		pPMKIDCnt = (UINT16 *)pIeData;
		PMKIDCnt = wlan_le16_to_cpu(*pPMKIDCnt);
		pIeData += sizeof(pRsnIe->PMKIDCnt);

		/* Check if the PMKID List is included */
		if (pIeData < pIeEnd) {
			/* pPMKIDList = pIeData; <-- Currently not used in parsing */
			pIeData += PMKIDCnt * sizeof(pRsnIe->PMKIDList);
		}
	}

	/* Check if the Group Mgmt Cipher is included */
	if (pIeData < pIeEnd) {
		pGrpMgmtCipher = pIeData;

		if (pGrpMgmtCipherOut) {
			memcpy(util_fns, pGrpMgmtCipherOut,
			       pGrpMgmtCipher, sizeof(pRsnIe->GrpMgmtCipher));
		}
	}
}
