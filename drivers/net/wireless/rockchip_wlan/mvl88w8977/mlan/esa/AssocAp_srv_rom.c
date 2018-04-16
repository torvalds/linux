/** @file AssocAp_src_rom.c
 *
 *  @brief This file defines the function for checking security type and ie
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
#include "hostsa_ext_def.h"
#include "IEEE_types.h"

#include "authenticator.h"
#include "AssocAp_srv_rom.h"
#include "parser.h"
#include "keyMgmtAp.h"

BOOLEAN
AssocSrvAp_checkCipherSupport(Cipher_t cipher, Cipher_t allowedCiphers)
{
	BOOLEAN match = FALSE;

	if (cipher.ccmp && (allowedCiphers.ccmp)) {
		match = TRUE;
	} else if (cipher.tkip && (allowedCiphers.tkip)) {
		match = TRUE;
	} else if (cipher.wep40 && (allowedCiphers.wep40)) {
		match = TRUE;
	} else if (cipher.wep104 && (allowedCiphers.wep104)) {
		match = TRUE;
	}

	return match;
}

UINT16
AssocSrvAp_checkAkm(phostsa_private priv, AkmSuite_t *pAkm, UINT16 allowedAkms)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT16 matchedAkms;

	matchedAkms = 0;

	if ((memcmp(util_fns, pAkm->akmOui, wpa_oui, sizeof(wpa_oui)) != 0) &&
	    ((memcmp(util_fns, pAkm->akmOui, kde_oui, sizeof(kde_oui)) != 0))) {
		return matchedAkms;
	}

	switch (pAkm->akmType) {
	case AKM_1X:
		matchedAkms = (allowedAkms & UAP_HOSTCMD_KEYMGMT_EAP);
		break;

	case AKM_PSK:
		matchedAkms = (allowedAkms & UAP_HOSTCMD_KEYMGMT_PSK);
		break;
	case AKM_SHA256_PSK:
		matchedAkms = (allowedAkms & UAP_HOSTCMD_KEYMGMT_PSK_SHA256);
		break;
	default:
		break;
	}

	return matchedAkms;
}

WL_STATUS
assocSrvAp_validate4WayHandshakeIe(phostsa_private priv,
				   SecurityMode_t secType,
				   Cipher_t pwCipher,
				   Cipher_t grpCipher,
				   apKeyMgmtInfoStaRom_t *pKeyMgmtInfo,
				   UINT8 akmType,
				   UINT16 rsnCap, Cipher_t config_mcstCipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	if (memcmp
	    (util_fns, (void *)&secType, (void *)&pKeyMgmtInfo->staSecType,
	     sizeof(secType)) != 0) {
		return FAIL;
	}

	if (memcmp(util_fns, (void *)&grpCipher,
		   (void *)&config_mcstCipher, sizeof(grpCipher)) != 0) {
		return FAIL;
	}

	if (memcmp
	    (util_fns, (void *)&pwCipher, (void *)&pKeyMgmtInfo->staUcstCipher,
	     sizeof(pwCipher)) != 0) {
		return FAIL;
	}

	if (akmType != pKeyMgmtInfo->staAkmType) {

		return FAIL;
	}

	return SUCCESS;
}

void
AssocSrvAp_InitKeyMgmtInfo(phostsa_private priv,
			   apKeyMgmtInfoStaRom_t *pKeyMgmtInfo,
			   SecurityMode_t *secType, Cipher_t *pwCipher,
			   UINT16 staRsnCap, UINT8 akmType)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	pKeyMgmtInfo->keyMgmtState = HSK_NOT_STARTED;
	memcpy(util_fns, (void *)&pKeyMgmtInfo->staSecType, (void *)secType,
	       sizeof(SecurityMode_t));
	memcpy(util_fns, (void *)&pKeyMgmtInfo->staUcstCipher, (void *)pwCipher,
	       sizeof(Cipher_t));
	pKeyMgmtInfo->staAkmType = akmType;
	if (secType->wpa2) {
		pKeyMgmtInfo->staRsnCap = staRsnCap;
	}
}

void
AssocSrvAp_InitStaKeyInfo(cm_Connection *connPtr,
			  SecurityMode_t *secType,
			  Cipher_t *pwCipher, UINT16 staRsnCap, UINT8 akmType)
{
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;

	phostsa_private priv = (phostsa_private)connPtr->priv;
	hostsa_util_fns *util_fns = &priv->util_fns;

	KeyMgmtStopHskTimer(connPtr);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	memset(util_fns, (void *)pKeyMgmtInfo, 0x00,
	       sizeof(apKeyMgmtInfoSta_t));
	AssocSrvAp_InitKeyMgmtInfo(priv, &pKeyMgmtInfo->rom, secType, pwCipher,
				   staRsnCap, akmType);
	pKeyMgmtInfo->EAPOLProtoVersion = EAPOL_PROTOCOL_V1;
}

WL_STATUS
assocSrvAp_checkRsnWpa(cm_Connection *connPtr,
		       apKeyMgmtInfoStaRom_t *pKeyMgmtInfo,
		       Cipher_t apWpaCipher,
		       Cipher_t apWpa2Cipher,
		       Cipher_t apMcstCipher,
		       UINT16 apAuthKey,
		       SecurityMode_t *pSecType,
		       IEEEtypes_RSNElement_t *pRsn,
		       IEEEtypes_WPAElement_t *pWpa,
		       BOOLEAN validate4WayHandshakeIE)
{
	phostsa_private priv = (phostsa_private)connPtr->priv;
	hostsa_util_fns *util_fns = &priv->util_fns;
	WL_STATUS result = SUCCESS;
	Cipher_t apCipher;
	Cipher_t pwCipher;
	Cipher_t grpCipher;
	SecurityMode_t wpaType;
	AkmSuite_t akm[AKM_SUITE_MAX];
	UINT8 minimumRsnLen;

	/* staRsnCap field is only used to compare RsnCap received in AssocRequest
	   with rsnCap received in 4 way handshake PWKMsg2.

	   we use 0xFFFF signature. If rsnCap is not present in pRsn,
	   signature 0xFFFF would be saved in pKeyMgmtInfo->staRsnCap.
	 */

	union {
		UINT16 shortInt;
		IEEEtypes_RSNCapability_t cfg;
	} staRsnCap;

	memset(util_fns, &wpaType, 0x00, sizeof(wpaType));
	memset(util_fns, &apCipher, 0x00, sizeof(apCipher));
	memset(util_fns, &pwCipher, 0x00, sizeof(pwCipher));
	memset(util_fns, &grpCipher, 0x00, sizeof(grpCipher));
	staRsnCap.shortInt = 0xFFFF;

	if (pRsn && (pSecType->wpa2 == 1)) {

		/*
		   In pRsn , All elements after Ver field are optional per the spec.

		   we reject Assoc Request, if GrpKeyCipher, pwsKey and AuthKey
		   is not present.

		   we can rely on minimum length check as we are rejecting Assoc Request
		   having pwsKeyCnt > 1 and AuthKeyCnt > 1

		 */

		minimumRsnLen = (unsigned long)&pRsn->RsnCap -
			(unsigned long)&pRsn->Ver;

		if (pRsn->Len < minimumRsnLen) {
			PRINTM(MERROR, "pRsn->Len %x < minimumRsnLen %x\n",
			       pRsn->Len, minimumRsnLen);
			return FAIL;
		}

		if (pRsn->PwsKeyCnt == 1 && pRsn->AuthKeyCnt == 1) {
			apCipher = apWpa2Cipher;

			supplicantParseRsnIe(priv, pRsn,
					     &wpaType,
					     &grpCipher,
					     &pwCipher,
					     akm,
					     NELEMENTS(akm),
					     &staRsnCap.cfg, NULL);
		} else {
			PRINTM(MERROR,
			       "pRsn->PwsKeyCnt  %x  pRsn->AuthKeyCnt %x\n",
			       pRsn->PwsKeyCnt, pRsn->AuthKeyCnt);
			result = FAIL;
		}
	} else if (pWpa && (pSecType->wpa == 1)) {
		if (pWpa->PwsKeyCnt == 1 && pWpa->AuthKeyCnt == 1) {
			apCipher = apWpaCipher;
			supplicantParseWpaIe(priv, pWpa,
					     &wpaType,
					     &grpCipher,
					     &pwCipher, akm, NELEMENTS(akm));
		} else {
			PRINTM(MERROR,
			       "pWpa->PwsKeyCnt  %x pWpa->AuthKeyCnt %x\n",
			       pWpa->PwsKeyCnt, pWpa->AuthKeyCnt);
			result = FAIL;
		}
	} else {
		PRINTM(MERROR, "No wpa or rsn\n");
		result = FAIL;
	}

	if ((pwCipher.ccmp == 0) && (pwCipher.tkip == 0)) {
		PRINTM(MERROR,
		       "(pwCipher.ccmp(%x) == 0) && (pwCipher.tkip(%x) == 0)\n",
		       pwCipher.ccmp, pwCipher.tkip);
		result = FAIL;
	}

	if ((grpCipher.ccmp == 0) && (grpCipher.tkip == 0)) {
		PRINTM(MERROR,
		       "((grpCipher.ccmp(%x) == 0) && (grpCipher.tkip(%x) == 0))\n",
		       grpCipher.ccmp, grpCipher.tkip);
		result = FAIL;
	}
	DBG_HEXDUMP(MCMD_D, " akm", (t_u8 *)&akm[0], sizeof(AkmSuite_t));
	if (SUCCESS == result) {
#ifdef DOT11W
		if (staRsnCap.shortInt != 0xFFFF) {
			/* Save the peer STA PMF capability, which will later used to enable PMF */
			connPtr->staData.peerPMFCapable = staRsnCap.cfg.MFPC;
		}
#endif
		if (validate4WayHandshakeIE == MFALSE) {
			if ((AssocSrvAp_checkCipherSupport(pwCipher, apCipher)
			     == TRUE) &&
			    (AssocSrvAp_checkCipherSupport
			     (grpCipher, apMcstCipher)
			     == TRUE) &&
			    (AssocSrvAp_checkAkm(priv, akm, apAuthKey) != 0)) {
				AssocSrvAp_InitStaKeyInfo(connPtr, &wpaType,
							  &pwCipher,
							  staRsnCap.shortInt,
							  akm[0].akmType);
			} else {
				result = FAIL;
			}
		} else {
			result = assocSrvAp_validate4WayHandshakeIe(priv,
								    wpaType,
								    pwCipher,
								    grpCipher,
								    pKeyMgmtInfo,
								    akm[0].
								    akmType,
								    staRsnCap.
								    shortInt,
								    apMcstCipher);
		}
	}
	return result;
}

SINT32
assocSrvAp_CheckSecurity(cm_Connection *connPtr,
			 IEEEtypes_WPSElement_t *pWps,
			 IEEEtypes_RSNElement_t *pRsn,
			 IEEEtypes_WPAElement_t *pWpa,
			 IEEEtypes_WAPIElement_t *pWapi,
			 IEEEtypes_StatusCode_t *pResult)
{
	phostsa_private priv = (phostsa_private)connPtr->priv;
	apInfo_t *pApInfo = &priv->apinfo;
	BssConfig_t *pBssConfig = NULL;
	SINT32 retval = MLME_FAILURE;

	*pResult = IEEEtypes_STATUS_INVALID_RSN_CAPABILITIES;

	pBssConfig = &pApInfo->bssConfig;

	PRINTM(MMSG, "assocSrvAp_CheckSecurity Sectyep wpa %x wpa2 %x\n",
	       pBssConfig->SecType.wpa, pBssConfig->SecType.wpa2);
	if ((pBssConfig->SecType.wpa == 1) || (pBssConfig->SecType.wpa2 == 1)) {
		apKeyMgmtInfoSta_t *pKeyMgmtInfo =
			&connPtr->staData.keyMgmtInfo;
		Cipher_t wpaUcastCipher = pBssConfig->RsnConfig.wpaUcstCipher;
		Cipher_t wpa2UcastCipher = pBssConfig->RsnConfig.wpa2UcstCipher;
		DBG_HEXDUMP(MCMD_D, " wpa2UcstCipher",
			    (t_u8 *)&wpa2UcastCipher, sizeof(Cipher_t));
		DBG_HEXDUMP(MCMD_D, " wpaUcastCipher",
			    (t_u8 *)&wpaUcastCipher, sizeof(Cipher_t));
		connPtr->staData.RSNEnabled = 0;
		if (assocSrvAp_checkRsnWpa(connPtr, &pKeyMgmtInfo->rom,
					   wpaUcastCipher,
					   wpa2UcastCipher,
					   pBssConfig->RsnConfig.mcstCipher,
					   pBssConfig->RsnConfig.AuthKey,
					   &pBssConfig->SecType, pRsn, pWpa,
					   MFALSE) == SUCCESS) {
			retval = MLME_SUCCESS;
			connPtr->staData.RSNEnabled = 1;
		}
	} else if (pBssConfig->SecType.wepStatic == 1) {
		if (!pRsn || !pWpa) {
			retval = MLME_SUCCESS;
		}
	} else if (pBssConfig->SecType.wapi) {
		/*       if (wapi_ie_check(pBssConfig, pWapi, pResult))
		   {
		   *pResult = 0;
		   retval = MLME_SUCCESS;
		   } */
	}

	return retval;
}
