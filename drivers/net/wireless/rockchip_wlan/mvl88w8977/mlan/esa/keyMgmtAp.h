/** @file keyMgntAP.h
 *
 *  @brief This file contains the eapol paket process and key management for authenticator
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
#ifndef _KEYMGMTAP_H_
#define _KEYMGMTAP_H_
//Authenticator related data structures, function prototypes

#include "wltypes.h"
#include "keyMgmtAp_rom.h"

#define STA_PS_EAPOL_HSK_TIMEOUT 3000	//ms
#define AP_RETRY_EAPOL_HSK_TIMEOUT 1000	//ms

extern void KeyMgmtInit(void *priv);
extern void ReInitGTK(void *priv);
int isValidReplayCount(t_void *priv, apKeyMgmtInfoSta_t *pKeyMgmtInfo,
		       UINT8 *pRxReplayCount);

extern void KeyMgmtGrpRekeyCountUpdate(t_void *context);
extern void KeyMgmtStartHskTimer(void *context);
extern void KeyMgmtStopHskTimer(void *context);
extern void KeyMgmtHskTimeout(t_void *context);
extern UINT32 keyApi_ApUpdateKeyMaterial(void *priv, cm_Connection *connPtr,
					 BOOLEAN updateGrpKey);

extern Status_e ProcessPWKMsg2(hostsa_private *priv,
			       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len);
extern Status_e ProcessPWKMsg4(hostsa_private *priv,
			       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len);
extern Status_e ProcessGrpMsg2(hostsa_private *priv,
			       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len);
extern Status_e GenerateApEapolMsg(hostsa_private *priv,
				   t_void *pconnPtr, keyMgmtState_e msgState);
extern void ApMicErrTimerExpCb(t_void *context);
extern void ApMicCounterMeasureInvoke(t_void *pconnPtr);
extern t_u16 keyMgmtAp_FormatWPARSN_IE(void *priv,
				       IEEEtypes_InfoElementHdr_t *pIe,
				       UINT8 isRsn,
				       Cipher_t *pCipher,
				       UINT8 cipherCount,
				       Cipher_t *pMcastCipher,
				       UINT16 authKey, UINT16 authKeyCount);
#endif //_KEYMGMTAP_H_
