/** @file keyMgmtsta_rom.c
 *
 *  @brief This file defines key management function for sta
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
#include "wltypes.h"
#include "IEEE_types.h"
#include "hostsa_ext_def.h"
#include "authenticator.h"

#include "keyMgmtSta_rom.h"
#include "pmkCache_rom.h"
#include "crypt_new_rom.h"
#include "rc4_rom.h"
#include "aes_cmac_rom.h"
#include "sha1.h"
#include "md5.h"
#include "mrvl_sha256_crypto.h"
#include "wl_macros.h"

#define MIC_ERROR_QUIET_TIME_INTERVAL           60000000	/* 60 sec */
#define MIC_ERROR_CHECK_TIME_INTERVAL         60000000

void
supplicantSetAssocRsn_internal(phostsa_private priv, RSNConfig_t *pRsnConfig,
			       SecurityParams_t *pSecurityParams,
			       SecurityMode_t wpaType,
			       Cipher_t *pMcstCipher,
			       Cipher_t *pUcstCipher,
			       AkmSuite_t *pAkm,
			       IEEEtypes_RSNCapability_t *pRsnCap,
			       Cipher_t *pGrpMgmtCipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	memset(util_fns, &pRsnConfig->wpaType,
	       0x00, sizeof(pRsnConfig->wpaType));
	memset(util_fns, &pRsnConfig->ucstCipher,
	       0x00, sizeof(pRsnConfig->ucstCipher));
	memset(util_fns, &pRsnConfig->mcstCipher,
	       0x00, sizeof(pRsnConfig->mcstCipher));

	pRsnConfig->pmkidValid = 0;
	pRsnConfig->rsnCapValid = 0;
	pRsnConfig->grpMgmtCipherValid = 0;
	pRsnConfig->rsvd = 0;

	if (pSecurityParams->wpaType.wpa2 && wpaType.wpa2) {
		/* encryption mode is WPA2 */
		memcpy(util_fns, &pRsnConfig->AKM, (UINT8 *)pAkm,
		       sizeof(pRsnConfig->AKM));

		pRsnConfig->wpaType.wpa2 = 1;

		if (pRsnCap) {
			pRsnConfig->rsnCapValid = 1;
			memcpy(util_fns, &pRsnConfig->rsnCap, pRsnCap,
			       sizeof(pRsnConfig->rsnCap));
		}

		if (pGrpMgmtCipher) {
			pRsnConfig->grpMgmtCipherValid = 1;
			memcpy(util_fns, &pRsnConfig->grpMgmtCipher,
			       pGrpMgmtCipher,
			       sizeof(pRsnConfig->grpMgmtCipher));
		}
	} else if (pSecurityParams->wpaType.wpaNone && wpaType.wpaNone) {
		memcpy(util_fns, &pRsnConfig->AKM,
		       wpa_oui_none, sizeof(pRsnConfig->AKM));

		/* encryption mode is WPA None */
		pRsnConfig->wpaType.wpaNone = 1;

		if (pSecurityParams->mcstCipher.ccmp && pMcstCipher->ccmp) {
			pRsnConfig->mcstCipher.ccmp = 1;
		} else {
			pRsnConfig->mcstCipher.tkip = 1;
		}
	} else if (pSecurityParams->wpaType.wpa && wpaType.wpa) {
		/* encryption mode is WPA */
		memcpy(util_fns, &pRsnConfig->AKM, (UINT8 *)pAkm,
		       sizeof(pRsnConfig->AKM));

		pRsnConfig->wpaType.wpa = 1;
	} else if (pSecurityParams->wpaType.noRsn) {
		/* No encryption */
		pRsnConfig->wpaType.noRsn = 1;
	}

	if (pRsnConfig->wpaType.wpa || pRsnConfig->wpaType.wpa2) {
		if (pSecurityParams->ucstCipher.ccmp && pUcstCipher->ccmp) {
			pRsnConfig->ucstCipher.ccmp = 1;
		} else {
			pRsnConfig->ucstCipher.tkip = 1;
		}

		if (pSecurityParams->mcstCipher.ccmp && pMcstCipher->ccmp) {
			pRsnConfig->mcstCipher.ccmp = 1;
		} else if (pSecurityParams->mcstCipher.tkip &&
			   pMcstCipher->tkip) {
			pRsnConfig->mcstCipher.tkip = 1;
		} else if (pSecurityParams->mcstCipher.wep104 &&
			   pMcstCipher->wep104) {
			pRsnConfig->mcstCipher.wep104 = 1;
		} else {
			pRsnConfig->mcstCipher.wep40 = 1;
		}
	}

}

UINT16
keyMgmtFormatWpaRsnIe_internal(phostsa_private priv, RSNConfig_t *pRsnConfig,
			       UINT8 *pos,
			       IEEEtypes_MacAddr_t *pBssid,
			       IEEEtypes_MacAddr_t *pStaAddr,
			       UINT8 *pPmkid, BOOLEAN addPmkid)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	IEEEtypes_WPAElement_t *pWpaIe = (IEEEtypes_WPAElement_t *)pos;
	IEEEtypes_RSNElement_t *pRsnIe = (IEEEtypes_RSNElement_t *)pos;

	UINT32 ieSize = 0;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	UINT16 ptr_val;

	if (keyMgmtFormatWpaRsnIe_internal_hook(pRsnConfig,
						pos,
						pBssid,
						pStaAddr,
						pPmkid, addPmkid, &ptr_val)) {
		return ptr_val;
	}
#endif

	if (pRsnConfig->wpaType.wpa2) {
		/* encryption mode is WPA2 */
		pRsnIe->ElementId = ELEM_ID_RSN;
		pRsnIe->Len = (sizeof(pRsnIe->Ver)
			       + sizeof(pRsnIe->GrpKeyCipher)
			       + sizeof(pRsnIe->PwsKeyCnt)
			       + sizeof(pRsnIe->PwsKeyCipherList)
			       + sizeof(pRsnIe->AuthKeyCnt)
			       + sizeof(pRsnIe->AuthKeyList));

		memcpy(util_fns, (void *)pRsnIe->AuthKeyList,
		       &pRsnConfig->AKM, sizeof(pRsnIe->AuthKeyList));

		pRsnIe->Ver = 1;

		pRsnIe->PwsKeyCnt = 1;
		pRsnIe->AuthKeyCnt = 1;

		if (pRsnConfig->ucstCipher.ccmp) {
			/* unicast cipher is aes */
			memcpy(util_fns, (void *)pRsnIe->PwsKeyCipherList,
			       wpa2_oui04, sizeof(pRsnIe->PwsKeyCipherList));
		} else {
			/* if not AES the TKIP */
			memcpy(util_fns, (void *)pRsnIe->PwsKeyCipherList,
			       wpa2_oui02, sizeof(pRsnIe->PwsKeyCipherList));
		}

		if (pRsnConfig->mcstCipher.ccmp) {
			/* multicast cipher is aes */
			memcpy(util_fns, (void *)pRsnIe->GrpKeyCipher,
			       wpa2_oui04, sizeof(pRsnIe->GrpKeyCipher));
		} else if (pRsnConfig->mcstCipher.tkip) {
			/* multicast cipher is tkip */
			memcpy(util_fns, (void *)pRsnIe->GrpKeyCipher,
			       wpa2_oui02, sizeof(pRsnIe->GrpKeyCipher));
		} else if (pRsnConfig->mcstCipher.wep104) {
			/* multicast cipher is WEP 104 */
			memcpy(util_fns, (void *)pRsnIe->GrpKeyCipher,
			       wpa2_oui05, sizeof(pRsnIe->GrpKeyCipher));
		} else {
			/* multicast cipher is WEP 40 */
			memcpy(util_fns, (void *)pRsnIe->GrpKeyCipher,
			       wpa2_oui01, sizeof(pRsnIe->GrpKeyCipher));
		}
		if (addPmkid && ((!pRsnConfig->pmkidValid && pBssid) || pPmkid)) {
			if (pPmkid) {
				memcpy(util_fns, pRsnConfig->PMKID, pPmkid,
				       sizeof(pRsnConfig->PMKID));
				pRsnConfig->pmkidValid = TRUE;
			} else {
				pRsnConfig->pmkidValid =
					supplicantGetPmkid(priv, pBssid,
							   pStaAddr,
							   &pRsnConfig->AKM,
							   pRsnConfig->PMKID);
			}
		}

		if (pRsnConfig->rsnCapValid
		    || pRsnConfig->pmkidValid
		    || pRsnConfig->grpMgmtCipherValid) {
			memcpy(util_fns, &pRsnIe->RsnCap,
			       &pRsnConfig->rsnCap, sizeof(pRsnIe->RsnCap));

			pRsnIe->Len += sizeof(pRsnIe->RsnCap);
		}

		if (pRsnConfig->pmkidValid || pRsnConfig->grpMgmtCipherValid) {
			pRsnIe->PMKIDCnt = 0;
			pRsnIe->Len += sizeof(pRsnIe->PMKIDCnt);

			if (pRsnConfig->pmkidValid) {
				/* Add PMKID to the RSN if not an EAPOL msg */
				pRsnIe->PMKIDCnt = 1;

				memcpy(util_fns, (UINT8 *)pRsnIe->PMKIDList,
				       pRsnConfig->PMKID,
				       sizeof(pRsnIe->PMKIDList));

				pRsnIe->Len += sizeof(pRsnIe->PMKIDList);
			}
		}

		if (pRsnConfig->grpMgmtCipherValid) {
			memcpy(util_fns, pRsnIe->GrpMgmtCipher,
			       &pRsnConfig->grpMgmtCipher,
			       sizeof(pRsnIe->GrpMgmtCipher));

			pRsnIe->Len += sizeof(pRsnIe->GrpMgmtCipher);
		}

		ieSize = sizeof(pRsnIe->ElementId) + sizeof(pRsnIe->Len);
		ieSize += pRsnIe->Len;
	} else if (pRsnConfig->wpaType.wpaNone || pRsnConfig->wpaType.wpa) {
		/* encryption mode is WPA */
		pWpaIe->ElementId = ELEM_ID_VENDOR_SPECIFIC;
		pWpaIe->Len = (sizeof(IEEEtypes_WPAElement_t)
			       - sizeof(pWpaIe->ElementId)
			       - sizeof(pWpaIe->Len));

		memcpy(util_fns, pWpaIe->OuiType, wpa_oui01,
		       sizeof(pWpaIe->OuiType));

		pWpaIe->Ver = 1;

		pWpaIe->PwsKeyCnt = 1;
		pWpaIe->AuthKeyCnt = 1;

		memcpy(util_fns, (void *)pWpaIe->AuthKeyList,
		       &pRsnConfig->AKM, sizeof(pWpaIe->AuthKeyList));

		if (pRsnConfig->wpaType.wpaNone) {
			memcpy(util_fns, (void *)pWpaIe->PwsKeyCipherList,
			       wpa_oui_none, sizeof(pWpaIe->PwsKeyCipherList));

			if (pRsnConfig->mcstCipher.tkip) {
				/* multicast cipher is tkip */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui02, sizeof(pWpaIe->GrpKeyCipher));
			} else if (pRsnConfig->mcstCipher.ccmp) {
				/* multicast cipher is aes */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui04, sizeof(pWpaIe->GrpKeyCipher));
			}
		} else {
			if (pRsnConfig->ucstCipher.ccmp) {
				/* unicast cipher is aes */
				memcpy(util_fns,
				       (void *)pWpaIe->PwsKeyCipherList,
				       wpa_oui04,
				       sizeof(pWpaIe->PwsKeyCipherList));
			} else {
				/* unicast cipher is tkip */
				memcpy(util_fns,
				       (void *)pWpaIe->PwsKeyCipherList,
				       wpa_oui02,
				       sizeof(pWpaIe->PwsKeyCipherList));
			}

			if (pRsnConfig->mcstCipher.ccmp) {
				/* multicast cipher is aes */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui04, sizeof(pWpaIe->GrpKeyCipher));
			} else if (pRsnConfig->mcstCipher.tkip) {
				/* multicast cipher is tkip */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui02, sizeof(pWpaIe->GrpKeyCipher));
			} else if (pRsnConfig->mcstCipher.wep104) {
				/* multicast cipher is wep 104 */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui05, sizeof(pWpaIe->GrpKeyCipher));
			} else {
				/* multicast cipher is wep 40 */
				memcpy(util_fns, (void *)pWpaIe->GrpKeyCipher,
				       wpa_oui01, sizeof(pWpaIe->GrpKeyCipher));
			}
		}

		ieSize = sizeof(pWpaIe->ElementId) + sizeof(pWpaIe->Len);
		ieSize += pWpaIe->Len;
	}

	return ieSize;
}

void
install_wpa_none_keys_internal(phostsa_private priv,
			       key_MgtMaterial_t *pKeyMgtData, UINT8 *pPMK,
			       UINT8 type, UINT8 unicast)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (install_wpa_none_keys_internal_hook(pKeyMgtData,
						pPMK, type, unicast)) {
		return;
	}
#endif

	memset(util_fns, (void *)pKeyMgtData, 0, sizeof(key_MgtMaterial_t));

	if (unicast) {
		pKeyMgtData->keyInfo = (KEY_INFO_MULTICAST | KEY_INFO_ENABLED);
	} else {
		pKeyMgtData->keyInfo = (KEY_INFO_UNICAST | KEY_INFO_ENABLED);
	}

	if (type) {
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.AES.key, pPMK,
		       16);
		pKeyMgtData->keyType = KEY_TYPE_AES;
		pKeyMgtData->keyLen = WPA_AES_KEY_LEN;
	} else {
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.key, pPMK,
		       16);
		pPMK += 16;
		pKeyMgtData->keyType = KEY_TYPE_TKIP;

		/* in WPA none the TX & RX MIC key is the same */
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.txMicKey,
		       pPMK, 8);
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.rxMicKey,
		       pPMK, 8);

		pKeyMgtData->keyLen = WPA_TKIP_KEY_LEN;
	}

}

UINT16
keyMgmtGetKeySize_internal(RSNConfig_t *pRsnConfig, UINT8 isPairwise)
{
	/* default to TKIP key size */
	UINT16 retval = 32;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (keyMgmtGetKeySize_internal_hook(pRsnConfig, isPairwise, &retval)) {
		return retval;
	}
#endif

	if (isPairwise) {
		if (pRsnConfig->ucstCipher.ccmp) {
			retval = 16;
		}
	} else {
		if (pRsnConfig->mcstCipher.ccmp) {
			retval = 16;
		} else if (pRsnConfig->mcstCipher.wep104) {
			retval = 13;
		} else if (pRsnConfig->mcstCipher.wep40) {
			retval = 5;
		}
	}
	return retval;
}

//#if defined(PSK_SUPPLICANT) || defined (WPA_NONE)
void
supplicantGenerateSha1Pmkid(phostsa_private priv, UINT8 *pPMK,
			    IEEEtypes_MacAddr_t *pBssid,
			    IEEEtypes_MacAddr_t *pSta, UINT8 *pPMKID)
{
	char pmkidString[] = "PMK Name";
	void *pText[3];

	int len[3] = { 8, 6, 6 };

	pText[0] = pmkidString;
	pText[1] = pBssid;
	pText[2] = pSta;

	Mrvl_hmac_sha1((void *)priv, (UINT8 **)pText, len, 3, pPMK, 32,	/* PMK size is always 32 bytes */
		       pPMKID, 16);
}

int
isApReplayCounterFresh(phostsa_private priv, keyMgmtInfoSta_t *pKeyMgmtInfoSta,
		       UINT8 *pRxReplayCount)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT32 tmpHi;
	UINT32 tmpLo;
	UINT32 rxCountHi;
	UINT32 rxCountLo;

	/* initialize the value as stale */
	int retVal = 0;

	memcpy(util_fns, &tmpHi, pRxReplayCount, 4);
	memcpy(util_fns, &tmpLo, pRxReplayCount + 4, 4);

	rxCountHi = ntohl(tmpHi);
	rxCountLo = ntohl(tmpLo);

	/* check hi dword first */
	if (rxCountHi > pKeyMgmtInfoSta->apCounterHi) {
		retVal = 1;
	} else if (rxCountHi == pKeyMgmtInfoSta->apCounterHi) {
		/* hi dword is equal, check lo dword */
		if (rxCountLo > pKeyMgmtInfoSta->apCounterLo) {
			retVal = 1;
		} else if (rxCountLo == pKeyMgmtInfoSta->apCounterLo) {

			/* Counters are equal. Check special case of zero. */
			if ((rxCountHi == 0) && (rxCountLo == 0)) {
				if (!pKeyMgmtInfoSta->apCounterZeroDone) {
					retVal = 1;
				}
			}
		}
	}

	return retVal;
}

void
updateApReplayCounter(phostsa_private priv, keyMgmtInfoSta_t *pKeyMgmtStaInfo,
		      UINT8 *pRxReplayCount)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT32 tmpHi;
	UINT32 tmpLo;
	UINT32 rxCountHi;
	UINT32 rxCountLo;

	memcpy(util_fns, &tmpHi, pRxReplayCount, 4);
	memcpy(util_fns, &tmpLo, pRxReplayCount + 4, 4);

	rxCountHi = ntohl(tmpHi);
	rxCountLo = ntohl(tmpLo);

	pKeyMgmtStaInfo->apCounterHi = rxCountHi;
	pKeyMgmtStaInfo->apCounterLo = rxCountLo;

	if ((rxCountHi == 0) && (rxCountLo == 0)) {
		pKeyMgmtStaInfo->apCounterZeroDone = 1;
	}
}

void
FillKeyMaterialStruct_internal(phostsa_private priv,
			       key_MgtMaterial_t *pKeyMgtData, UINT16 key_len,
			       UINT8 isPairwise, KeyData_t *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 keyInfo;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	if (FillKeyMaterialStruct_internal_hook(pKeyMgtData,
						key_len, isPairwise, pKey)) {
		return;
	}
#endif

	/* Update key material */
	memset(util_fns, (void *)pKeyMgtData, 0x00, sizeof(key_MgtMaterial_t));

	/* check the key type is pairwise */
	if (isPairwise) {
		keyInfo = KEY_INFO_UNICAST;
	} else {
		keyInfo = KEY_INFO_MULTICAST;
	}

	if (key_len == (UINT16)WPA_AES_KEY_LEN) {
		/* AES */
		pKeyMgtData->keyType = KEY_TYPE_AES;
		pKeyMgtData->keyInfo = keyInfo | KEY_INFO_ENABLED;

		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.AES.key,
		       pKey->Key, key_len);

	} else if (key_len == WPA_TKIP_KEY_LEN) {
		pKeyMgtData->keyType = KEY_TYPE_TKIP;
		pKeyMgtData->keyInfo = keyInfo | KEY_INFO_ENABLED;

		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.key,
		       pKey->Key, TK_SIZE);
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.txMicKey,
		       pKey->TxMICKey, 8);
		memcpy(util_fns, (UINT8 *)pKeyMgtData->keyEncypt.TKIP.rxMicKey,
		       pKey->RxMICKey, 8);
	} else if (key_len == WPA_WEP104_KEY_LEN ||
		   key_len == WPA_WEP40_KEY_LEN) {
		pKeyMgtData->keyType = KEY_TYPE_WEP;
		pKeyMgtData->keyInfo = keyInfo | KEY_INFO_ENABLED;

		if (isPairwise) {
			pKeyMgtData->keyEncypt.WEP.keyIndex = 0;
			pKeyMgtData->keyEncypt.WEP.isDefaultTx = 1;
		} else {
			/* use the Key index provided */
			pKeyMgtData->keyEncypt.WEP.keyIndex = pKey->KeyIndex;
			pKeyMgtData->keyEncypt.WEP.isDefaultTx = 0;
		}
		memcpy(util_fns, (UINT8 *)(pKeyMgtData->keyEncypt.WEP.key),
		       pKey->Key, key_len);
	} else {
		/* Key length does not match
		 ** don't send down anything
		 */
		return;
	}

	pKeyMgtData->keyLen = key_len;
}

//#endif

/*
**  This function checks if the given element pointer parameter
**  is a KDE or not (returns NULL)
*/
KDE_t *
parseKeyKDE(phostsa_private priv, IEEEtypes_InfoElementHdr_t *pIe)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	KDE_t *pKde = NULL;

	if (pIe->ElementId == ELEM_ID_VENDOR_SPECIFIC) {
		pKde = (KDE_t *)pIe;

		if (pKde->length > sizeof(KDE_t) &&
		    !memcmp(util_fns, (void *)pKde->OUI, kde_oui,
			    sizeof(kde_oui))) {
			return pKde;
		}
	}

	return NULL;

}

/* This function searches KDE_DATA_TYPE_XXX in KDE. We can collect all such
**  KDE_DATA_TYPE_XXX in one pass, however, there seems not much benefit
**  as in general there would not be many KDE_DATA_TYPE_XXX present.
**  Returns NULL, when KDE_DATA_TYPE_XXX not found.
*/

KDE_t *
parseKeyKDE_DataType(phostsa_private priv, UINT8 *pData,
		     SINT32 dataLen, IEEEtypes_KDEDataType_e KDEDataType)
{

	IEEEtypes_InfoElementHdr_t *pIe;
	KDE_t *pKde;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	UINT32 ptr_val;

	if (parseKeyKDE_DataType_hook(pData, dataLen, KDEDataType, &ptr_val)) {
		return (KDE_t *)ptr_val;
	}
#endif

	if (pData == NULL) {
		return NULL;
	}

	while (dataLen > (SINT32)sizeof(IEEEtypes_InfoElementHdr_t)) {
		pIe = (IEEEtypes_InfoElementHdr_t *)pData;

		if (pIe->ElementId == ELEM_ID_VENDOR_SPECIFIC) {
			pKde = parseKeyKDE(priv, pIe);

			if ((pKde != NULL) && (pKde->dataType == KDEDataType)) {
				return pKde;
			} else if (pIe->Len == 0) {
				/* the rest is padding, so adjust the length
				 ** to stop the processing loop
				 */
				dataLen = sizeof(IEEEtypes_InfoElementHdr_t);
			}
		}

		dataLen -= (pIe->Len + sizeof(IEEEtypes_InfoElementHdr_t));
		pData += (pIe->Len + sizeof(IEEEtypes_InfoElementHdr_t));
	}

	return NULL;
}

KDE_t *
parseKeyDataGTK(phostsa_private priv, UINT8 *pKey, UINT16 len,
		KeyData_t *pGRKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	GTK_KDE_t *pGtk;
	KDE_t *pKde;
#if 0				//!defined(REMOVE_PATCH_HOOKS)
	UINT32 ptr_val;

	if (parseKeyDataGTK_hook(pKey, len, pGRKey, &ptr_val)) {
		return (KDE_t *)ptr_val;
	}
#endif

	/* parse KDE GTK */
	pKde = parseKeyKDE_DataType(priv, pKey, len, KDE_DATA_TYPE_GTK);

	if (pKde) {
		/* GTK KDE */
		pGtk = (GTK_KDE_t *)pKde->data;

		/* The KDE overhead is 6 bytes */
		memcpy(util_fns, pGRKey->Key, (void *)pGtk->GTK,
		       pKde->length - 6);

		/* save the group key index */
		pGRKey->KeyIndex = pGtk->KeyID;
	}

	return pKde;
}

void
KeyMgmtSta_ApplyKEK(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
		    KeyData_t *pGRKey, UINT8 *EAPOL_Encr_Key)
{
#if 0
#if !defined(REMOVE_PATCH_HOOKS)
	if (KeyMgmtSta_ApplyKEK_hook(pKeyMsg, pGRKey, EAPOL_Encr_Key)) {
		return;
	}
#endif
#endif
	pGRKey->TxIV16 = pKeyMsg->key_RSC[1] << 8;
	pGRKey->TxIV16 |= pKeyMsg->key_RSC[0];
	pGRKey->TxIV32 = 0xFFFFFFFF;

	pKeyMsg->key_material_len = ntohs(pKeyMsg->key_material_len) & 0xFFFF;

	switch (pKeyMsg->key_info.KeyDescriptorVersion) {
		/*
		 ** Key Descriptor Version 2 or 3: AES key wrap, defined in IETF
		 **   RFC 3394, shall be used to encrypt the Key Data field using
		 **   the KEK field from the derived PTK.
		 */
	case 3:
	case 2:
		/* CCMP */
		MRVL_AesUnWrap(EAPOL_Encr_Key,
			       2,
			       pKeyMsg->key_material_len / 8 - 1,
			       (UINT8 *)pKeyMsg->key_data,
			       NULL, (UINT8 *)pKeyMsg->key_data);

		/* AES key wrap has 8 extra bytes that come out
		 ** due to the default IV
		 */
		pKeyMsg->key_material_len -= 8;
		break;

		/*
		 ** Key Descriptor Version 1: ARC4 is used to encrypt the Key Data
		 **   field using the KEK field from the derived PTK
		 */
	default:
	case 1:
		/* TKIP or WEP */
		/* Skip the first 256 bytes of the RC4 Stream */
		RC4_Encrypt((void *)priv, EAPOL_Encr_Key,
			    (UINT8 *)pKeyMsg->EAPOL_key_IV,
			    sizeof(pKeyMsg->EAPOL_key_IV),
			    (UINT8 *)pKeyMsg->key_data,
			    pKeyMsg->key_material_len, 256);
		break;
	}

}

/*
**  Verifies the received EAPOL frame during 4-way handshake or
**  group key handshake
*/
BOOLEAN
KeyMgmtSta_IsRxEAPOLValid(phostsa_private priv,
			  keyMgmtInfoSta_t *pKeyMgmtInfoSta,
			  EAPOL_KeyMsg_t *pKeyMsg)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

#if 0				//!defined(REMOVE_PATCH_HOOKS)
	BOOLEAN ptr_val;

	if (KeyMgmtSta_IsRxEAPOLValid_hook(pKeyMgmtInfoSta, pKeyMsg, &ptr_val)) {
		return ptr_val;
	}
#endif

	if (!pKeyMgmtInfoSta || !pKeyMsg) {
		PRINTM(MERROR, "KeyMgmtSta_IsRxEAPOLValid input not valid\n");
		return FALSE;
	}

	if (!isApReplayCounterFresh
	    (priv, pKeyMgmtInfoSta, (UINT8 *)pKeyMsg->replay_cnt)) {
		PRINTM(MERROR,
		       "KeyMgmtSta_IsRxEAPOLValid isApReplayCounterFresh Fail\n");
		return FALSE;
	}

	/* Check if we have to verify MIC */

	if (pKeyMsg->key_info.KeyMIC) {
		/* We have to verify MIC, if keyType is 1 it's the 3rd message in
		   4-way handshake, in that case verify ANonce.
		 */

		if ((pKeyMsg->key_info.KeyType == 1) &&
		    memcmp(util_fns, (UINT8 *)&pKeyMsg->key_nonce,
			   (UINT8 *)pKeyMgmtInfoSta->ANonce, NONCE_SIZE) != 0) {
			/* Dropping the packet, return some error msg. */
			PRINTM(MERROR,
			       "KeyMgmtSta_IsRxEAPOLValid Nonce check Fail\n");
			return FALSE;
		}

		if (!IsEAPOL_MICValid
		    (priv, pKeyMsg, pKeyMgmtInfoSta->EAPOL_MIC_Key)) {
			/* MIC failed */
			PRINTM(MERROR,
			       "KeyMgmtSta_IsRxEAPOLValid MIC check Fail\n");
			return FALSE;
		}

	}
	return TRUE;

}

/*
**  This function populates EAPOL frame fileds that are common
**  to message 2, 4 of 4-way handshake and group key hadshake
**  message 2.
**  Any of these fields in general should not change, and if needed
**  can be overwritten in the caller function.
*/
void
KeyMgmtSta_PrepareEAPOLFrame(phostsa_private priv, EAPOL_KeyMsg_Tx_t *pTxEapol,
			     EAPOL_KeyMsg_t *pRxEapol,
			     t_u8 *da, t_u8 *sa, UINT8 *pSNonce)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (!pTxEapol || !pRxEapol) {
		return;
	}
	memset(util_fns, (UINT8 *)pTxEapol, 0x00, sizeof(EAPOL_KeyMsg_Tx_t));

	formEAPOLEthHdr(priv, pTxEapol, da, sa);

	pTxEapol->keyMsg.desc_type = pRxEapol->desc_type;
	pTxEapol->keyMsg.key_info.KeyType = pRxEapol->key_info.KeyType;
	pTxEapol->keyMsg.key_info.KeyMIC = 1;
	pTxEapol->keyMsg.key_info.Secure = pRxEapol->key_info.Secure;
	pTxEapol->keyMsg.replay_cnt[0] = pRxEapol->replay_cnt[0];
	pTxEapol->keyMsg.replay_cnt[1] = pRxEapol->replay_cnt[1];

	pTxEapol->keyMsg.key_info.KeyDescriptorVersion
		= pRxEapol->key_info.KeyDescriptorVersion;

	// Only for  4-w handshake message 2.
	if (pSNonce) {
		memcpy(util_fns, (UINT8 *)pTxEapol->keyMsg.key_nonce, pSNonce,
		       NONCE_SIZE);
	}
}

void
KeyMgmtSta_PrepareEAPOLMicErrFrame(phostsa_private priv,
				   EAPOL_KeyMsg_Tx_t *pTxEapol,
				   BOOLEAN isUnicast, IEEEtypes_MacAddr_t *da,
				   IEEEtypes_MacAddr_t *sa,
				   keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (!pTxEapol || !pKeyMgmtInfoSta) {
		return;
	}

	memset(util_fns, (UINT8 *)pTxEapol, 0x00, sizeof(EAPOL_KeyMsg_Tx_t));

	formEAPOLEthHdr(priv, pTxEapol, (t_u8 *)da, (t_u8 *)sa);

	pTxEapol->keyMsg.key_info.KeyType = isUnicast;
	pTxEapol->keyMsg.key_info.KeyMIC = 1;
	pTxEapol->keyMsg.key_info.Secure = 1;
	pTxEapol->keyMsg.key_info.Error = 1;
	pTxEapol->keyMsg.key_info.Request = 1;
	pTxEapol->keyMsg.replay_cnt[0] = htonl(pKeyMgmtInfoSta->staCounterHi);
	pTxEapol->keyMsg.replay_cnt[1] = htonl(pKeyMgmtInfoSta->staCounterLo);

}

BOOLEAN
supplicantAkmIsWpaWpa2(phostsa_private priv, AkmSuite_t *pAkm)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (!memcmp(util_fns, pAkm->akmOui, wpa_oui, sizeof(wpa_oui)) ||
	    !memcmp(util_fns, pAkm->akmOui, kde_oui, sizeof(kde_oui))) {
		return TRUE;
	}

	return FALSE;
}

BOOLEAN
supplicantAkmIsWpa2(phostsa_private priv, AkmSuite_t *pAkm)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (memcmp(util_fns, pAkm->akmOui, kde_oui, sizeof(kde_oui)) == 0) {
		return TRUE;
	}

	return FALSE;
}

BOOLEAN
supplicantAkmIsWpaWpa2Psk(phostsa_private priv, AkmSuite_t *pAkm)
{
	if (supplicantAkmIsWpaWpa2(priv, pAkm)) {
		return ((pAkm->akmType == AKM_PSK) ||
			(pAkm->akmType == AKM_SHA256_PSK) ||
			(pAkm->akmType == AKM_FT_PSK));
	}

	return FALSE;
}

BOOLEAN
supplicantAkmUsesKdf(phostsa_private priv, AkmSuite_t *pAkm)
{
	if (supplicantAkmIsWpa2(priv, pAkm)) {
		if ((pAkm->akmType == AKM_SHA256_PSK) ||
		    (pAkm->akmType == AKM_SHA256_1X) ||
		    (pAkm->akmType == AKM_FT_PSK) ||
		    (pAkm->akmType == AKM_FT_1X)) {
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN
supplicantAkmWpa2Ft(phostsa_private priv, AkmSuite_t *pAkm)
{
	if (supplicantAkmIsWpa2(priv, pAkm)) {
		if ((pAkm->akmType == AKM_FT_PSK) ||
		    (pAkm->akmType == AKM_FT_1X)) {
			return TRUE;
		}
	}

	return FALSE;
}

BOOLEAN
supplicantAkmUsesSha256Pmkid(phostsa_private priv, AkmSuite_t *pAkm)
{
	if (supplicantAkmIsWpa2(priv, pAkm)) {
		if ((pAkm->akmType == AKM_SHA256_PSK) ||
		    (pAkm->akmType == AKM_SHA256_1X)) {
			return TRUE;
		}
	}

	return FALSE;
}

void
supplicantGenerateSha256Pmkid(phostsa_private priv, UINT8 *pPMK,
			      IEEEtypes_MacAddr_t *pBssid,
			      IEEEtypes_MacAddr_t *pSta, UINT8 *pPMKID)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 *pOutput;
	UINT8 *vectors[3];
	size_t vectLen[NELEMENTS(vectors)];
	UINT8 buf[500] = { 0 };

	pOutput = (UINT8 *)buf;

	/*
	 **  PMKID = Truncate-128(SHA-256("PMK Name" || AA || SPA)
	 */
	vectors[0] = (UINT8 *)"PMK Name";
	vectLen[0] = 8;		/* strlen("PMK Name") */

	vectors[1] = (UINT8 *)pBssid;
	vectLen[1] = sizeof(IEEEtypes_MacAddr_t);

	vectors[2] = (UINT8 *)pSta;
	vectLen[2] = sizeof(IEEEtypes_MacAddr_t);

	mrvl_sha256_crypto_vector((void *)priv, NELEMENTS(vectors), vectors,
				  vectLen, pOutput);

	memcpy(util_fns, pPMKID, pOutput, PMKID_LEN);

}

BOOLEAN
supplicantGetPmkid(phostsa_private priv, IEEEtypes_MacAddr_t *pBssid,
		   IEEEtypes_MacAddr_t *pStaAddr,
		   AkmSuite_t *pAkm, UINT8 *pPMKID)
{
	BOOLEAN retval;
	UINT8 *pPMK;

	retval = FALSE;

	if (!supplicantAkmIsWpaWpa2Psk(priv, pAkm)) {
		pPMK = pmkCacheFindPMK((void *)priv, pBssid);

		/* found the PMK so generate the PMKID */
		if (pPMK) {
			if (supplicantAkmUsesSha256Pmkid(priv, pAkm)) {
				supplicantGenerateSha256Pmkid(priv, pPMK,
							      pBssid, pStaAddr,
							      pPMKID);
			} else {
				supplicantGenerateSha1Pmkid(priv, pPMK, pBssid,
							    pStaAddr, pPMKID);
			}

			retval = TRUE;
		}
	}

	return retval;
}

EAPOL_KeyMsg_t *
GetKeyMsgNonceFromEAPOL(phostsa_private priv, mlan_buffer *pmbuf,
			keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	EAPOL_KeyMsg_t *pKeyMsg =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));;

	if (!KeyMgmtSta_IsRxEAPOLValid(priv, pKeyMgmtInfoSta, pKeyMsg)) {
		PRINTM(MERROR, "KeyMgmtSta_IsRxEAPOLValid Fail\n");
		return NULL;
	}
	/* Generate Nonce if this is first PWK Message */
	if (pKeyMsg->key_info.KeyMIC == 0) {
		memcpy(util_fns, pKeyMgmtInfoSta->ANonce,
		       pKeyMsg->key_nonce, NONCE_SIZE);

		supplicantGenerateRand(priv, pKeyMgmtInfoSta->SNonce,
				       NONCE_SIZE);
	}
	return pKeyMsg;
}

#ifndef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
EAPOL_KeyMsg_t *
ProcessRxEAPOL_PwkMsg3(phostsa_private priv, mlan_buffer *pmbuf,
		       keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	EAPOL_KeyMsg_t *pKeyMsg;

	pKeyMsg = GetKeyMsgNonceFromEAPOL(priv, pmbuf, pKeyMgmtInfoSta);
	if (!pKeyMsg) {
		PRINTM(MERROR, "ProcessRxEAPOL_PwkMsg3 pKeyMsg is NULL\n");
		return NULL;
	}
	pKeyMgmtInfoSta->newPWKey.TxIV16 = 1;
	pKeyMgmtInfoSta->newPWKey.TxIV32 = 0;

	/* look for group key once the pairwise has been plumbed */
	if (pKeyMsg->key_info.EncryptedKeyData) {
		/* I think the timer stop should be moved later on
		   in case ramHook_Process_CCX_MFP_11r returns
		   FALSE
		 */

//        microTimerStop(pKeyMgmtInfoSta->rsnTimer);
		util_fns->moal_stop_timer(util_fns->pmoal_handle,
					  pKeyMgmtInfoSta->rsnTimer);
		//pKeyMgmtInfoSta->rsnTimer = 0;

		KeyMgmtSta_ApplyKEK(priv, pKeyMsg,
				    &pKeyMgmtInfoSta->GRKey,
				    pKeyMgmtInfoSta->EAPOL_Encr_Key);
#if 0
		if (ramHook_keyMgmtProcessMsgExt(pKeyMgmtInfoSta, pKeyMsg) ==
		    FALSE) {
			return NULL;
		}
#endif
		parseKeyDataGTK(priv, pKeyMsg->key_data,
				pKeyMsg->key_material_len,
				&pKeyMgmtInfoSta->GRKey);

	}
	return pKeyMsg;
}
#endif

#ifndef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
EAPOL_KeyMsg_t *
ProcessRxEAPOL_GrpMsg1(phostsa_private priv, mlan_buffer *pmbuf,
		       keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	EAPOL_KeyMsg_t *pKeyMsg;
	KeyData_t GRKey;
	pKeyMsg = GetKeyMsgNonceFromEAPOL(priv, pmbuf, pKeyMgmtInfoSta);
	if (!pKeyMsg) {
		return NULL;
	}

	KeyMgmtSta_ApplyKEK(priv, pKeyMsg,
			    &pKeyMgmtInfoSta->GRKey,
			    pKeyMgmtInfoSta->EAPOL_Encr_Key);

	pKeyMgmtInfoSta->RSNDataTrafficEnabled = 1;
	//microTimerStop(pKeyMgmtInfoSta->rsnTimer);
	util_fns->moal_stop_timer(util_fns->pmoal_handle,
				  pKeyMgmtInfoSta->rsnTimer);
	//pKeyMgmtInfoSta->rsnTimer = 0;

	/* Decrypt the group key */
	if (pKeyMsg->desc_type == 2) {
		/* WPA2 */
		/* handle it according to 802.11i GTK frame format */
		parseKeyDataGTK(priv, pKeyMsg->key_data,
				pKeyMsg->key_material_len, &GRKey);
		/* Not install same GTK */
		if (!memcmp
		    (util_fns, pKeyMgmtInfoSta->GRKey.Key, GRKey.Key,
		     TK_SIZE)) {
			priv->gtk_installed = 1;
		} else {
			memcpy(util_fns, &pKeyMgmtInfoSta->GRKey, &GRKey,
			       sizeof(KeyData_t));
			pKeyMgmtInfoSta->GRKey.TxIV16 = GRKey.TxIV16;
			pKeyMgmtInfoSta->GRKey.TxIV32 = 0xFFFFFFFF;
			priv->gtk_installed = 0;
		}
#if 0
		if (ramHook_keyMgmtProcessMsgExt(pKeyMgmtInfoSta, pKeyMsg) ==
		    FALSE) {
			return NULL;
		}
#endif
	} else {
		/* WPA or Dynamic WEP */
		if (!memcmp
		    (util_fns, pKeyMgmtInfoSta->GRKey.Key, pKeyMsg->key_data,
		     pKeyMsg->key_material_len)) {
			priv->gtk_installed = 1;
		} else {
			memcpy(util_fns, pKeyMgmtInfoSta->GRKey.Key,
			       pKeyMsg->key_data, pKeyMsg->key_material_len);

			pKeyMgmtInfoSta->GRKey.KeyIndex =
				pKeyMsg->key_info.KeyIndex;
		}
	}

	return pKeyMsg;
}
#endif

void
KeyMgmtResetCounter(keyMgmtInfoSta_t *pKeyMgmtInfo)
{
	if (pKeyMgmtInfo) {
		pKeyMgmtInfo->staCounterHi = 0;
		pKeyMgmtInfo->staCounterLo = 0;
	}
}

/*
**  This code executes when the MIC failure timer timesout
*/
void
MicErrTimerExp_Sta(t_void *context)
{
	phostsa_private psapriv = (phostsa_private)context;
	keyMgmtInfoSta_t *pKeyMgmtInfo = &psapriv->suppData->keyMgmtInfoSta;

	if (pKeyMgmtInfo) {
		//if (pKeyMgmtInfo->micTimer == timerId)
		{
			if (pKeyMgmtInfo->sta_MIC_Error.status
			    == SECOND_MIC_FAIL_IN_60_SEC) {
				//ramHook_keyMgmtSendTkipQuietOver(data);
			}

			pKeyMgmtInfo->sta_MIC_Error.status = NO_MIC_FAILURE;
			pKeyMgmtInfo->sta_MIC_Error.disableStaAsso = 0;
		}

		pKeyMgmtInfo->micTimer = 0;
	}
}

void
DeauthDelayTimerExp_Sta(t_void *context)
{
	phostsa_private psapriv = (phostsa_private)context;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &psapriv->suppData->keyMgmtInfoSta;

	if (pKeyMgmtInfoSta) {
		//if (pKeyMgmtInfoSta->deauthDelayTimer == timerId)
		{
			if (pKeyMgmtInfoSta->sta_MIC_Error.status
			    == SECOND_MIC_FAIL_IN_60_SEC) {
				//ramHook_keyMgmtSendDeauth(psapriv,
				//                          IEEEtypes_REASON_MIC_FAILURE);
				keyMgmtSendDeauth2Peer(psapriv,
						       IEEEtypes_REASON_MIC_FAILURE);
			}
		}

		pKeyMgmtInfoSta->deauthDelayTimer = 0;
	}
}

/*
**  Key Management timeout handler
*/
void
keyMgmtStaRsnSecuredTimeoutHandler(t_void *context)
{
	phostsa_private psapriv = (phostsa_private)context;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &psapriv->suppData->keyMgmtInfoSta;

	if (pKeyMgmtInfoSta) {
		//if (pKeyMgmtInfoSta->rsnTimer == timerId)
		{
			if (pKeyMgmtInfoSta->RSNSecured == FALSE) {
				/* Clear timer before calling the timeout so the rsnTimer
				 **  can't be cancelled during the sme state clearing.
				 **  (caused timer re-entrancy failure).
				 */
				//pKeyMgmtInfoSta->rsnTimer = 0;
				//ramHook_keyMgmtSendDeauth(
				//    psapriv,
				//    IEEEtypes_REASON_4WAY_HANDSHK_TIMEOUT);
				keyMgmtSendDeauth2Peer(psapriv,
						       IEEEtypes_REASON_4WAY_HANDSHK_TIMEOUT);
			}
		}
		//pKeyMgmtInfoSta->rsnTimer = 0;
	}
}

void
keyMgmtSta_StartSession_internal(phostsa_private priv,
				 keyMgmtInfoSta_t *pKeyMgmtInfoSta,
				 //MicroTimerCallback_t callback,
				 UINT32 expiry, UINT8 flags)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (!pKeyMgmtInfoSta->sta_MIC_Error.disableStaAsso) {
		//microTimerStop(pKeyMgmtInfoSta->rsnTimer);
		util_fns->moal_stop_timer(util_fns->pmoal_handle,
					  pKeyMgmtInfoSta->rsnTimer);
		//pKeyMgmtInfoSta->rsnTimer = 0;
		//microTimerStart(callback,
		//                 (UINT32)pKeyMgmtInfoSta,
		//                 expiry,
		//                 &pKeyMgmtInfoSta->rsnTimer,
		//                 flags);
		util_fns->moal_start_timer(util_fns->pmoal_handle,
					   pKeyMgmtInfoSta->rsnTimer, MFALSE,
					   expiry);
	}

	/* reset the authenticator replay counter */
	pKeyMgmtInfoSta->apCounterLo = 0;
	pKeyMgmtInfoSta->apCounterHi = 0;
	pKeyMgmtInfoSta->apCounterZeroDone = 0;
}

void
KeyMgmtSta_handleMICDeauthTimer(keyMgmtInfoSta_t *pKeyMgmtInfoSta,
				MicroTimerCallback_t callback,
				UINT32 expiry, UINT8 flags)
{
#if 0
	microTimerStop(pKeyMgmtInfoSta->deauthDelayTimer);

	microTimerStart(callback,
			(UINT32)pKeyMgmtInfoSta,
			expiry, &pKeyMgmtInfoSta->deauthDelayTimer, flags);
#endif
}

#ifndef WAR_ROM_BUG57216_QUIET_TIME_INTERVAL
/* This function assumes that argument state would be either
    NO_MIC_FAILURE or FIRST_MIC_FAIL_IN_60_SEC
    It must not be called with state othe than these two
*/
void
KeyMgmtSta_handleMICErr(MIC_Fail_State_e state,
			keyMgmtInfoSta_t *pKeyMgmtInfoSta,
			MicroTimerCallback_t callback, UINT8 flags)
{
	UINT32 expiry;
//    UINT32 int_save = tx_interrupt_control(TX_INT_DISABLE);

	if (state == NO_MIC_FAILURE) {
		/* First MIC failure */
		pKeyMgmtInfoSta->sta_MIC_Error.status =
			FIRST_MIC_FAIL_IN_60_SEC;
		expiry = MIC_ERROR_CHECK_TIME_INTERVAL;
	} else {
		/* Received 2 MIC failures within 60 sec. Do deauth from AP */
		pKeyMgmtInfoSta->sta_MIC_Error.disableStaAsso = 1;
		pKeyMgmtInfoSta->sta_MIC_Error.status =
			SECOND_MIC_FAIL_IN_60_SEC;
		pKeyMgmtInfoSta->apCounterHi = 0;
		pKeyMgmtInfoSta->apCounterLo = 0;
		expiry = MIC_ERROR_QUIET_TIME_INTERVAL;
	}
//    tx_interrupt_control(int_save);
#if 0
	microTimerStop(pKeyMgmtInfoSta->micTimer);

	microTimerStart(callback,
			(UINT32)pKeyMgmtInfoSta,
			expiry, &pKeyMgmtInfoSta->micTimer, flags);
#endif
}
#endif

/*
**  Initialize the Key Mgmt session
*/
void
KeyMgmtSta_InitSession(phostsa_private priv, keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	pKeyMgmtInfoSta->RSNDataTrafficEnabled = FALSE;
	pKeyMgmtInfoSta->RSNSecured = FALSE;
	pKeyMgmtInfoSta->pRxDecryptKey = NULL;
	pKeyMgmtInfoSta->pwkHandshakeComplete = FALSE;

	if (!pKeyMgmtInfoSta->sta_MIC_Error.disableStaAsso) {
//        microTimerStop(pKeyMgmtInfoSta->micTimer);
		pKeyMgmtInfoSta->micTimer = 0;
//        microTimerStop(pKeyMgmtInfoSta->deauthDelayTimer);
		pKeyMgmtInfoSta->deauthDelayTimer = 0;
	}
//    microTimerStop(pKeyMgmtInfoSta->rsnTimer);
	util_fns->moal_stop_timer(util_fns->pmoal_handle,
				  pKeyMgmtInfoSta->rsnTimer);

	//pKeyMgmtInfoSta->rsnTimer = 0;
}
