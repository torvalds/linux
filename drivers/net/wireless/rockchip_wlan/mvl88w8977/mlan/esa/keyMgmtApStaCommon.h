/** @file keyMgmtApStaCommon.h
 *
 *  @brief This file contains common api for authenticator and supplicant.
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
#ifndef KEYMGMTAPSTACOMMON_H__
#define KEYMGMTAPSTACOMMON_H__
//Authenticator related data structures, function prototypes

#include "wltypes.h"
#include "IEEE_types.h"
#include "sha1.h"
#include "keyMgmtStaTypes.h"
#include "wl_macros.h"
#include "keyMgmtApTypes.h"
#include "rc4_rom.h"

#include "keyCommonDef.h"
#include "authenticator.h"

extern t_u32 wlan_strlen(const char *str);
extern void supplicantGenerateRand(hostsa_private *priv, UINT8 *dataOut,
				   UINT32 length);
extern void SetEAPOLKeyDescTypeVersion(EAPOL_KeyMsg_Tx_t *pTxEapol,
				       BOOLEAN isWPA2, BOOLEAN isKDF,
				       BOOLEAN nonTKIP);
extern void ComputeEAPOL_MIC(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
			     UINT16 data_length, UINT8 *MIC_Key,
			     UINT8 MIC_Key_length, UINT8 micKeyDescVersion);
extern BOOLEAN IsEAPOL_MICValid(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
				UINT8 *pMICKey);
extern void supplicantConstructContext(phostsa_private priv, UINT8 *pAddr1,
				       UINT8 *pAddr2, UINT8 *pNonce1,
				       UINT8 *pNonce2, UINT8 *pContext);
extern UINT16 KeyMgmtSta_PopulateEAPOLLengthMic(phostsa_private priv,
						EAPOL_KeyMsg_Tx_t *pTxEapol,
						UINT8 *pEAPOLMICKey,
						UINT8 eapolProtocolVersion,
						UINT8 forceKeyDescVersion);
extern void KeyMgmt_DerivePTK(phostsa_private priv, UINT8 *pAddr1,
			      UINT8 *pAddr2, UINT8 *pNonce1, UINT8 *pNonce2,
			      UINT8 *pPTK, UINT8 *pPMK, BOOLEAN use_kdf);
extern void KeyMgmtSta_DeriveKeys(hostsa_private *priv, UINT8 *pPMK, UINT8 *da,
				  UINT8 *sa, UINT8 *ANonce, UINT8 *SNonce,
				  UINT8 *EAPOL_MIC_Key, UINT8 *EAPOL_Encr_Key,
				  KeyData_t *newPWKey, BOOLEAN use_kdf);
extern void UpdateEAPOLWcbLenAndTransmit(hostsa_private *priv,
					 pmlan_buffer pmbuf, UINT16 frameLen);
extern void formEAPOLEthHdr(phostsa_private priv, EAPOL_KeyMsg_Tx_t *pTxEapol,
			    t_u8 *da, t_u8 *sa);
extern void supplicantParseWpaIe(phostsa_private priv,
				 IEEEtypes_WPAElement_t *pIe,
				 SecurityMode_t *pWpaType,
				 Cipher_t *pMcstCipher, Cipher_t *pUcstCipher,
				 AkmSuite_t *pAkmList, UINT8 akmOutMax);
extern void supplicantParseRsnIe(phostsa_private priv,
				 IEEEtypes_RSNElement_t *pRsnIe,
				 SecurityMode_t *pWpaTypeOut,
				 Cipher_t *pMcstCipherOut,
				 Cipher_t *pUcstCipherOut,
				 AkmSuite_t *pAkmListOut, UINT8 akmOutMax,
				 IEEEtypes_RSNCapability_t *pRsnCapOut,
				 Cipher_t *pGrpMgmtCipherOut);
#endif //KEYMGMTAPSTACOMMON_H__
