/** @file keyMgntsta_rom.h
 *
 *  @brief This file contains key management function for sta
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
#ifndef _KEY_MGMT_STA_ROM_H_
#define _KEY_MGMT_STA_ROM_H_

#include "keyCommonDef.h"
#include "KeyApiStaDefs.h"
#include "IEEE_types.h"
#include "keyApiStaTypes.h"
//#include "keyMgmtStaHostTypes.h"
#include "authenticator.h"
#include "keyMgmtApStaCommon.h"

/* Timer ID passed back to the caller when starting a timer. */
typedef UINT32 MicroTimerId_t;

/* Callback function registered when starting a timer */
typedef void (*MicroTimerCallback_t) (MicroTimerId_t, UINT32);
#define PMKID_LEN 16

extern void updateApReplayCounter(phostsa_private priv,
				  keyMgmtInfoSta_t *pKeyMgmtStaInfo,
				  UINT8 *pRxReplayCount);

extern KDE_t *parseKeyKDE(phostsa_private priv,
			  IEEEtypes_InfoElementHdr_t *pIe);

extern void supplicantGenerateSha1Pmkid(phostsa_private priv, UINT8 *pPMK,
					IEEEtypes_MacAddr_t *pBssid,
					IEEEtypes_MacAddr_t *pSta,
					UINT8 *pPMKID);

extern void FillKeyMaterialStruct_internal(phostsa_private priv,
					   key_MgtMaterial_t *p_keyMgtData,
					   UINT16 key_len, UINT8 isPairwise,
					   KeyData_t *pKey);

extern BOOLEAN (*supplicantSetAssocRsn_internal_hook) (RSNConfig_t *pRsnConfig,
						       SecurityParams_t
						       *pSecurityParams,
						       SecurityMode_t wpaType,
						       Cipher_t *pMcstCipher,
						       Cipher_t *pUcstCipher,
						       AkmSuite_t *pAkm,
						       IEEEtypes_RSNCapability_t
						       *pRsnCap,
						       Cipher_t
						       *pGrpMgmtCipher);

extern void supplicantSetAssocRsn_internal(phostsa_private priv,
					   RSNConfig_t *pRsnConfig,
					   SecurityParams_t *pSecurityParams,
					   SecurityMode_t wpaType,
					   Cipher_t *pMcstCipher,
					   Cipher_t *pUcstCipher,
					   AkmSuite_t *pAkm,
					   IEEEtypes_RSNCapability_t *pRsnCap,
					   Cipher_t *pGrpMgmtCipher);
#if 0
extern BOOLEAN (*keyMgmtFormatWpaRsnIe_internal_hook) (RSNConfig_t *pRsnConfig,
						       UINT8 *pos,
						       IEEEtypes_MacAddr_t
						       *pBssid,
						       IEEEtypes_MacAddr_t
						       *pStaAddr, UINT8 *pPmkid,
						       BOOLEAN addPmkid,
						       UINT16 *ptr_val);
#endif
extern UINT16 keyMgmtFormatWpaRsnIe_internal(phostsa_private priv,
					     RSNConfig_t *pRsnConfig,
					     UINT8 *pos,
					     IEEEtypes_MacAddr_t *pBssid,
					     IEEEtypes_MacAddr_t *pStaAddr,
					     UINT8 *pPmkid, BOOLEAN addPmkid);
#if 0
extern BOOLEAN (*install_wpa_none_keys_internal_hook) (key_MgtMaterial_t
						       *p_keyMgtData,
						       UINT8 *pPMK, UINT8 type,
						       UINT8 unicast);
#endif
extern void install_wpa_none_keys_internal(phostsa_private priv,
					   key_MgtMaterial_t *p_keyMgtData,
					   UINT8 *pPMK, UINT8 type,
					   UINT8 unicast);
#if 0
extern BOOLEAN (*keyMgmtGetKeySize_internal_hook) (RSNConfig_t *pRsnConfig,
						   UINT8 isPairwise,
						   UINT16 *ptr_val);
#endif
extern UINT16 keyMgmtGetKeySize_internal(RSNConfig_t *pRsnConfig,
					 UINT8 isPairwise);
extern BOOLEAN supplicantAkmIsWpaWpa2(phostsa_private priv, AkmSuite_t *pAkm);
extern BOOLEAN supplicantAkmIsWpaWpa2Psk(phostsa_private priv,
					 AkmSuite_t *pAkm);
extern BOOLEAN supplicantAkmUsesKdf(phostsa_private priv, AkmSuite_t *pAkm);
#if 0
extern
BOOLEAN (*parseKeyKDE_DataType_hook) (UINT8 *pData,
				      SINT32 dataLen,
				      IEEEtypes_KDEDataType_e KDEDataType,
				      UINT32 *ptr_val);
#endif
extern KDE_t *parseKeyKDE_DataType(phostsa_private priv, UINT8 *pData,
				   SINT32 dataLen,
				   IEEEtypes_KDEDataType_e KDEDataType);
#if 0
extern BOOLEAN (*parseKeyDataGTK_hook) (UINT8 *pKey,
					UINT16 len,
					KeyData_t *pGRKey, UINT32 *ptr_val);
#endif
extern KDE_t *parseKeyDataGTK(phostsa_private priv, UINT8 *pKey, UINT16 len,
			      KeyData_t *pGRKey);

extern BOOLEAN IsEAPOL_MICValid(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
				UINT8 *pMICKey);
#if 0
extern BOOLEAN (*KeyMgmtSta_ApplyKEK_hook) (EAPOL_KeyMsg_t *pKeyMsg,
					    KeyData_t *pGRKey,
					    UINT8 *EAPOL_Encr_Key);
#endif
extern void KeyMgmtSta_ApplyKEK(phostsa_private priv, EAPOL_KeyMsg_t *pKeyMsg,
				KeyData_t *pGRKey, UINT8 *EAPOL_Encr_Key);
#if 0
extern
BOOLEAN (*KeyMgmtSta_IsRxEAPOLValid_hook) (keyMgmtInfoSta_t *pKeyMgmtInfoSta,
					   EAPOL_KeyMsg_t *pKeyMsg,
					   BOOLEAN *ptr_val);
#endif
extern BOOLEAN KeyMgmtSta_IsRxEAPOLValid(phostsa_private priv,
					 keyMgmtInfoSta_t *pKeyMgmtInfoSta,
					 EAPOL_KeyMsg_t *pKeyMsg);
extern void KeyMgmtSta_PrepareEAPOLFrame(phostsa_private priv,
					 EAPOL_KeyMsg_Tx_t *pTxEapol,
					 EAPOL_KeyMsg_t *pRxEapol, t_u8 *da,
					 t_u8 *sa, UINT8 *pSNonce);
extern UINT16 KeyMgmtSta_PopulateEAPOLLengthMic(phostsa_private priv,
						EAPOL_KeyMsg_Tx_t *pTxEapol,
						UINT8 *pEAPOLMICKey,
						UINT8 eapolProtocolVersion,
						UINT8 forceKeyDescVersion);
extern
void KeyMgmtSta_PrepareEAPOLMicErrFrame(phostsa_private priv,
					EAPOL_KeyMsg_Tx_t *pTxEapol,
					BOOLEAN isUnicast,
					IEEEtypes_MacAddr_t *da,
					IEEEtypes_MacAddr_t *sa,
					keyMgmtInfoSta_t *pKeyMgmtInfoSta);

extern BOOLEAN supplicantAkmWpa2Ft(phostsa_private priv, AkmSuite_t *pAkm);

extern BOOLEAN supplicantAkmUsesSha256Pmkid(phostsa_private priv,
					    AkmSuite_t *pAkm);

extern void supplicantGenerateSha256Pmkid(phostsa_private priv, UINT8 *pPMK,
					  IEEEtypes_MacAddr_t *pBssid,
					  IEEEtypes_MacAddr_t *pSta,
					  UINT8 *pPMKID);

extern BOOLEAN supplicantGetPmkid(phostsa_private priv,
				  IEEEtypes_MacAddr_t *pBssid,
				  IEEEtypes_MacAddr_t *pStaAddr,
				  AkmSuite_t *pAkm, UINT8 *pPMKID);

extern void KeyMgmt_DerivePTK(phostsa_private priv, UINT8 *pAddr1,
			      UINT8 *pAddr2,
			      UINT8 *pNonce1,
			      UINT8 *pNonce2,
			      UINT8 *pPTK, UINT8 *pPMK, BOOLEAN use_kdf);

extern void SetEAPOLKeyDescTypeVersion(EAPOL_KeyMsg_Tx_t *pTxEapol,
				       BOOLEAN isWPA2,
				       BOOLEAN isKDF, BOOLEAN nonTKIP);

extern void KeyMgmtResetCounter(keyMgmtInfoSta_t *pKeyMgmtInfo);

extern void keyMgmtSta_StartSession_internal(phostsa_private priv,
					     keyMgmtInfoSta_t *pKeyMgmtInfoSta,
					     //MicroTimerCallback_t callback,
					     UINT32 expiry, UINT8 flags);

extern void KeyMgmtSta_handleMICDeauthTimer(keyMgmtInfoSta_t *pKeyMgmtInfoSta,
					    MicroTimerCallback_t callback,
					    UINT32 expiry, UINT8 flags);

extern void KeyMgmtSta_handleMICErr(MIC_Fail_State_e state,
				    keyMgmtInfoSta_t *pKeyMgmtInfoSta,
				    MicroTimerCallback_t callback, UINT8 flags);

extern void DeauthDelayTimerExp_Sta(t_void *context);
extern void keyMgmtStaRsnSecuredTimeoutHandler(t_void *context);
extern void keyMgmtSendDeauth2Peer(phostsa_private priv, UINT16 reason);
extern void supplicantGenerateRand(hostsa_private *priv, UINT8 *dataOut,
				   UINT32 length);

extern EAPOL_KeyMsg_t *GetKeyMsgNonceFromEAPOL(phostsa_private priv,
					       mlan_buffer *pmbuf,
					       keyMgmtInfoSta_t
					       *pKeyMgmtInfoSta);

extern EAPOL_KeyMsg_t *ProcessRxEAPOL_PwkMsg3(phostsa_private priv,
					      mlan_buffer *pmbuf,
					      keyMgmtInfoSta_t
					      *pKeyMgmtInfoSta);

extern EAPOL_KeyMsg_t *ProcessRxEAPOL_GrpMsg1(phostsa_private priv,
					      mlan_buffer *pmbuf,
					      keyMgmtInfoSta_t
					      *pKeyMgmtInfoSta);

extern void KeyMgmtSta_InitSession(phostsa_private priv,
				   keyMgmtInfoSta_t *pKeyMgmtInfoSta);

extern void supplicantParseMcstCipher(phostsa_private priv,
				      Cipher_t *pMcstCipherOut,
				      UINT8 *pGrpKeyCipher);

extern void supplicantParseUcstCipher(phostsa_private priv,
				      Cipher_t *pUcstCipherOut, UINT8 pwsKeyCnt,
				      UINT8 *pPwsKeyCipherList);

#endif
