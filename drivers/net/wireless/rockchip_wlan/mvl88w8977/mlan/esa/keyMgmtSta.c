/** @file keyMgmtSta.c
 *
 *  @brief This file defines key management API for sta
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
#include "wl_macros.h"
#include "keyMgmtSta.h"
#include "pass_phrase.h"
#include "wlpd.h"
#include "KeyApiStaDefs.h"
#include "crypt_new.h"
#include "pmkCache.h"
#include "sha1.h"
#include "md5.h"
#include "rc4.h"
#include "aes_cmac.h"
#include "mrvl_sha256_crypto.h"

#include "keyMgmtSta_rom.h"
#include "tlv.h"

#define DEAUTH_DELAY_TIME_INTERVAL            40000	/* 40 ms */

#define PWK_MSG1_RETRIES       7

/* 10 seconds timeout for completing RSN key handshake */
//#define RSNSECUREDTIMEOUT  10000000
#define RSNSECUREDTIMEOUT  MRVDRV_TIMER_10S

//static void SendMICFailReport_sta(cm_ConnectionInfo_t* connPtr,
//                                  keyMgmtInfoSta_t* pKeyMgmtInfoSta,
//                                  BOOLEAN isUnicast);
#if 0
static BufferReturnNotify_t keyMgmtKeyGroupTxDone(phostsa_private priv);
static BufferReturnNotify_t keyMgmtKeyPairwiseTxDone(phostsa_private priv);
static BufferReturnNotify_t keyMgmtKeyPairAndGroupTxDone(phostsa_private priv);
#endif
static void keyMgmtKeyGroupTxDone(phostsa_private priv);
static void keyMgmtKeyPairwiseTxDone(phostsa_private priv);
static void keyMgmtKeyPairAndGroupTxDone(phostsa_private priv);

static supplicantData_t keyMgmt_SuppData[MAX_SUPPLICANT_SESSIONS];

void FillKeyMaterialStruct(phostsa_private priv,
			   UINT16 key_len, UINT8 isPairwise, KeyData_t *pKey);
void FillGrpKeyMaterialStruct(phostsa_private priv,
			      UINT16 keyType,
			      UINT8 *pn,
			      UINT8 keyIdx, UINT8 keyLen, KeyData_t *pKey);
UINT16 keyMgmtFormatWpaRsnIe(phostsa_private priv,
			     UINT8 *pos,
			     IEEEtypes_MacAddr_t *pBssid,
			     IEEEtypes_MacAddr_t *pStaAddr,
			     UINT8 *pPmkid, BOOLEAN addPmkid);
t_u8 supplicantIsEnabled(void *priv);

void
allocSupplicantData(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	int i = 0;

	if (psapriv->suppData) {
		return;
	}
	//if (pm_fns->bss_type == MLAN_BSS_TYPE_STA)
	{

//        int_sta = os_if_save_EnterCriticalSection();
		for (i = 0; i < MAX_SUPPLICANT_SESSIONS; i++) {
			if (keyMgmt_SuppData[i].inUse == FALSE) {
				keyMgmt_SuppData[i].inUse = TRUE;
				supplicantInit((void *)psapriv,
					       &keyMgmt_SuppData[i]);
				psapriv->suppData =
					(void *)&keyMgmt_SuppData[i];
				break;
			}
		}
//        os_if_save_ExitCriticalSection(int_sta);

//        os_ASSERT(connPtr->suppData);
	}

}

void
freeSupplicantData(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	struct supplicantData *suppData = psapriv->suppData;

	if (suppData != NULL) {
		suppData->inUse = FALSE;
		suppData = NULL;
	}
}

mlan_status
initSupplicantTimer(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	if (util_fns->moal_init_timer(util_fns->pmoal_handle,
				      &psapriv->suppData->keyMgmtInfoSta.
				      rsnTimer,
				      keyMgmtStaRsnSecuredTimeoutHandler,
				      psapriv) != MLAN_STATUS_SUCCESS) {
		ret = MLAN_STATUS_FAILURE;
		return ret;
	}

	return ret;
}

void
freeSupplicantTimer(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	if (psapriv->suppData->keyMgmtInfoSta.rsnTimer) {
		util_fns->moal_free_timer(util_fns->pmoal_handle,
					  psapriv->suppData->keyMgmtInfoSta.
					  rsnTimer);
		psapriv->suppData->keyMgmtInfoSta.rsnTimer = NULL;
	}
}

//#if defined(PSK_SUPPLICANT) || defined (WPA_NONE)
void
keyMgmtSendDeauth2Peer(phostsa_private priv, UINT16 reason)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;

	/* Assumes we are sending to AP */
	//keyMgmtSendDeauth((cm_ConnectionInfo_t*)connPtr,
	//                  &((cm_ConnectionInfo_t*)connPtr)->suppData->localBssid,
	//                  reason);
#if 0
	ret = wlan_prepare_cmd(priv,
			       HostCmd_CMD_802_11_DEAUTHENTICATE,
			       HostCmd_ACT_GEN_SET,
			       0, NULL, &priv->suppData->localBssid);

	if (ret == MLAN_STATUS_SUCCESS)
		wlan_recv_event(priv, MLAN_EVENT_ID_DRV_DEFER_HANDLING, MNULL);
#endif
	pm_fns->hostsa_StaSendDeauth(pm_fns->pmlan_private,
				     (t_u8 *)&priv->suppData->localBssid,
				     reason);
}

BOOLEAN
keyMgmtProcessMsgExt(phostsa_private priv, keyMgmtInfoSta_t *pKeyMgmtInfoSta,
		     EAPOL_KeyMsg_t *pKeyMsg)
{
	if (pKeyMsg->key_info.KeyType && pKeyMsg->key_info.KeyMIC) {
		/* PWK Msg #3 processing */
#ifdef DOT11R
		if (supplicantAkmWpa2Ft
		    (priv,
		     &pKeyMgmtInfoSta->connPtr->suppData->customMIB_RSNConfig.
		     AKM)) {
			if (dot11r_process_pwk_msg3(pKeyMsg) == FALSE) {
				return FALSE;
			}
		}
#endif

#ifdef CCX_MFP
		if (ccx_mfp_process_pwk_msg3(pKeyMsg) == FALSE) {
			return FALSE;
		}
#endif
	}

	/*
	 **  KDE processing for Msg#3 and for any Group Key rotations
	 */
	if (pKeyMsg->key_info.EncryptedKeyData) {
#ifdef DOT11W
		KDE_t *pKde;

		/* Parse IGTK for 11w */
		pKde = parseKeyKDE_DataType(priv, pKeyMsg->key_data,
					    pKeyMsg->key_material_len,
					    KDE_DATA_TYPE_IGTK);
		if (pKde) {
			//Not install same iGtk
			if (!memcmp(&priv->util_fns,
				    pKeyMgmtInfoSta->IGtk.Key,
				    (UINT8 *)(((IGtkKde_t *)pKde->data)->IGtk),
				    MIN(sizeof(pKeyMgmtInfoSta->IGtk.Key),
					(pKde->length - 12)))) {
				return TRUE;
			} else {
				keyMgmtSetIGtk(priv, pKeyMgmtInfoSta,
					       (IGtkKde_t *)pKde->data,
					       pKde->length);
			}
#if 0
			hostEventPrintHex(assocAgent_getConnPtr(),
					  "SetIGTK", keyMgmtGetIGtk(), 16);
#endif
		}
#endif
	}

	return TRUE;
}

#ifdef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
EAPOL_KeyMsg_t *
patch_ProcessRxEAPOL_GrpMsg1(phostsa_private priv, mlan_buffer *pmbuf,
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

		if (keyMgmtProcessMsgExt(priv, pKeyMgmtInfoSta, pKeyMsg) ==
		    FALSE) {
			return NULL;
		}
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

#ifdef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
EAPOL_KeyMsg_t *
patch_ProcessRxEAPOL_PwkMsg3(phostsa_private priv, mlan_buffer *pmbuf,
			     keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	EAPOL_KeyMsg_t *pKeyMsg;

	pKeyMsg = GetKeyMsgNonceFromEAPOL(priv, pmbuf, pKeyMgmtInfoSta);
	if (!pKeyMsg) {
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
//        pKeyMgmtInfoSta->rsnTimer = 0;

		KeyMgmtSta_ApplyKEK(priv, pKeyMsg,
				    &pKeyMgmtInfoSta->GRKey,
				    pKeyMgmtInfoSta->EAPOL_Encr_Key);

		if (keyMgmtProcessMsgExt(priv, pKeyMgmtInfoSta, pKeyMsg) ==
		    FALSE) {
			return NULL;
		}

		parseKeyDataGTK(priv, pKeyMsg->key_data,
				pKeyMsg->key_material_len,
				&pKeyMgmtInfoSta->GRKey);

	}
	return pKeyMsg;
}
#endif

/* This routine must be called after mlmeStaInit_UR
** It assumes that parent session structures are initialized
** (vmacEntry_ur and mlmeStaInfo_URepeater)
*/
void
KeyMgmtInitSta(phostsa_private priv)
{
	KeyMgmtSta_InitSession(priv, &priv->suppData->keyMgmtInfoSta);
}

Status_e
GeneratePWKMsg2(phostsa_private priv, mlan_buffer *pmbuf,
		UINT8 *pSNonce, UINT8 *pEAPOLMICKey, UINT8 forceKeyDescVersion)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_Tx_t *pTxEapol = MNULL;
	UINT16 frameLen;
	UINT16 packet_len = 0;
	BOOLEAN rsnIeAdded = FALSE;
	EAPOL_KeyMsg_t *pRxEapol =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));
	struct supplicantData *suppData = priv->suppData;
	pmlan_buffer newbuf = MNULL;
	UINT8 intf_hr_len =
		pm_fns->Hostsa_get_intf_hr_len(pm_fns->pmlan_private);

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	newbuf = pm_fns->hostsa_alloc_mlan_buffer(pm_fns->pmlan_adapter,
						  MLAN_TX_DATA_BUF_SIZE_2K, 0,
						  MOAL_MALLOC_BUFFER);
	if (newbuf) {
		newbuf->bss_index = pmbuf->bss_index;
		newbuf->buf_type = pmbuf->buf_type;
		newbuf->priority = pmbuf->priority;
		newbuf->in_ts_sec = pmbuf->in_ts_sec;
		newbuf->in_ts_usec = pmbuf->in_ts_usec;
		newbuf->data_offset =
			(sizeof(TxPD) + intf_hr_len + DMA_ALIGNMENT);
	}

	if (newbuf == NULL) {
		PRINTM(MERROR, "GeneratePWKMsg2 newbuf=NULL\n");
		return FAIL;
	}

	pTxEapol = (EAPOL_KeyMsg_Tx_t *)(newbuf->pbuf + newbuf->data_offset);
	KeyMgmtSta_PrepareEAPOLFrame(priv, pTxEapol,
				     pRxEapol,
				     (t_u8 *)&suppData->localBssid,
				     (t_u8 *)&suppData->localStaAddr, pSNonce);

#ifdef DOT11R
	if (dot11r_is_ft_akm(&connPtr->suppData->customMIB_RSNConfig.AKM)) {
		dot11r_process_pwk_msg2(connPtr, &pTxEapol->keyMsg);
		rsnIeAdded = TRUE;
	}
#endif

	if (!rsnIeAdded && (pTxEapol->keyMsg.desc_type != 1)) {
		/* Add the RSN/WPA IE if not dynamic WEP */
		pTxEapol->keyMsg.key_material_len
			= keyMgmtFormatWpaRsnIe(priv,
						(UINT8 *)&pTxEapol->keyMsg.
						key_data, &suppData->localBssid,
						&suppData->localStaAddr, NULL,
						FALSE);
	}
#ifdef CCX_MFP
	ccx_mfp_process_pwk_msg2(&pTxEapol->keyMsg);
#endif

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(priv, pTxEapol,
						     pEAPOLMICKey,
						     EAPOL_PROTOCOL_V1,
						     forceKeyDescVersion);

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);
	UpdateEAPOLWcbLenAndTransmit(priv, newbuf, packet_len);

	PRINTM(MMSG, "LEAVE: %s\n", __FUNCTION__);
	return SUCCESS;
}

Status_e
GeneratePWKMsg4(phostsa_private priv, mlan_buffer *pmbuf,
		keyMgmtInfoSta_t *pKeyMgmtInfoSta, BOOLEAN groupKeyReceived)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	struct supplicantData *suppData = priv->suppData;
	EAPOL_KeyMsg_Tx_t *pTxEapol;
	UINT16 frameLen;
	UINT16 packet_len = 0;
	EAPOL_KeyMsg_t *pRxEapol =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));
	pmlan_buffer newbuf = MNULL;
	UINT8 intf_hr_len =
		pm_fns->Hostsa_get_intf_hr_len(pm_fns->pmlan_private);

	PRINTM(MMSG, "Enter GeneratePWKMsg4\n");

	newbuf = pm_fns->hostsa_alloc_mlan_buffer(pm_fns->pmlan_adapter,
						  MLAN_TX_DATA_BUF_SIZE_2K, 0,
						  MOAL_MALLOC_BUFFER);
	if (newbuf) {
		newbuf->bss_index = pmbuf->bss_index;
		newbuf->buf_type = pmbuf->buf_type;
		newbuf->priority = pmbuf->priority;
		newbuf->in_ts_sec = pmbuf->in_ts_sec;
		newbuf->in_ts_usec = pmbuf->in_ts_usec;
		newbuf->data_offset =
			(sizeof(TxPD) + intf_hr_len + DMA_ALIGNMENT);
	}

	if (newbuf == NULL) {
		PRINTM(MERROR, "GeneratePWKMsg4 newbuf=NULL\n");
		return FAIL;
	}

	pTxEapol = (EAPOL_KeyMsg_Tx_t *)(newbuf->pbuf + newbuf->data_offset);

	KeyMgmtSta_PrepareEAPOLFrame(priv, pTxEapol,
				     pRxEapol,
				     (t_u8 *)&suppData->localBssid,
				     (t_u8 *)&suppData->localStaAddr, NULL);

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(priv, pTxEapol,
						     pKeyMgmtInfoSta->
						     EAPOL_MIC_Key,
						     EAPOL_PROTOCOL_V1, 0);

	/* Set the BuffDesc free callback so the PSK supplicant can determine
	 **  if the 4th message was successfully received by the AP.  Allows
	 **  the supplicant to hold off switching/setting the new key until
	 **  it is sure the AP has acknowledged the handshake completion
	 */
#if 0
	if (pKeyMgmtInfoSta->RSNDataTrafficEnabled) {
		pBufDesc->isCB = 1;
		if (groupKeyReceived) {
			pBufDesc->freeCallback = keyMgmtKeyPairAndGroupTxDone;
		} else {
			pBufDesc->freeCallback = keyMgmtKeyPairwiseTxDone;
		}
	} else {
#endif

		if (groupKeyReceived) {
			keyMgmtKeyPairAndGroupTxDone(priv);
		} else {
			keyMgmtKeyPairwiseTxDone(priv);
		}
#if 0
	}
#endif

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);
	UpdateEAPOLWcbLenAndTransmit(priv, newbuf, packet_len);

	PRINTM(MMSG, "Leave GeneratePWKMsg4\n");
	return SUCCESS;
}

Status_e
GenerateGrpMsg2(phostsa_private priv, mlan_buffer *pmbuf,
		keyMgmtInfoSta_t *pKeyMgmtInfoSta)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_t *pRxEapol =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));
	EAPOL_KeyMsg_Tx_t *pTxEapol;
	UINT16 frameLen;
	UINT16 packet_len = 0;
	struct supplicantData *suppData = priv->suppData;
	pmlan_buffer newbuf = MNULL;
	UINT8 intf_hr_len =
		pm_fns->Hostsa_get_intf_hr_len(pm_fns->pmlan_private);

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	newbuf = pm_fns->hostsa_alloc_mlan_buffer(pm_fns->pmlan_adapter,
						  MLAN_TX_DATA_BUF_SIZE_2K, 0,
						  MOAL_MALLOC_BUFFER);
	if (newbuf) {
		newbuf->bss_index = pmbuf->bss_index;
		newbuf->buf_type = pmbuf->buf_type;
		newbuf->priority = pmbuf->priority;
		newbuf->in_ts_sec = pmbuf->in_ts_sec;
		newbuf->in_ts_usec = pmbuf->in_ts_usec;
		newbuf->data_offset =
			(sizeof(TxPD) + intf_hr_len + DMA_ALIGNMENT);
	}

	if (newbuf == NULL) {
		PRINTM(MERROR, "GenerateGrpMsg2 newbuf=NULL\n");
		return FAIL;
	}

	pTxEapol = (EAPOL_KeyMsg_Tx_t *)(newbuf->pbuf + newbuf->data_offset);

	KeyMgmtSta_PrepareEAPOLFrame(priv, pTxEapol,
				     pRxEapol,
				     (t_u8 *)&suppData->localBssid,
				     (t_u8 *)&suppData->localStaAddr, NULL);

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(priv, pTxEapol,
						     pKeyMgmtInfoSta->
						     EAPOL_MIC_Key,
						     EAPOL_PROTOCOL_V1, 0);
//    pBufDesc->isCB = 1;
//    pBufDesc->freeCallback = keyMgmtKeyGroupTxDone;
	keyMgmtKeyGroupTxDone(priv);

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);
	UpdateEAPOLWcbLenAndTransmit(priv, newbuf, packet_len);

	PRINTM(MMSG, "LEAVE: %s\n", __FUNCTION__);
	return SUCCESS;
}

BOOLEAN
KeyMgmtStaHsk_Recvd_PWKMsg1(phostsa_private priv, mlan_buffer *pmbuf,
			    IEEEtypes_MacAddr_t *sa, IEEEtypes_MacAddr_t *da)
{
	EAPOL_KeyMsg_t *pKeyMsg = NULL;
	struct supplicantData *suppData = priv->suppData;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &suppData->keyMgmtInfoSta;
	UINT8 *pPmk;
	BOOLEAN msgProcessed;
	BOOLEAN genPwkMsg2;
	BOOLEAN retval;
	UINT32 uMaxRetry = 5;	// MAX_SUPPLICANT_INIT_TIMEOUT

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	msgProcessed = FALSE;
	genPwkMsg2 = TRUE;
	retval = FALSE;

//#ifdef PSK_SUPPLICANT
	/* Wait for supplicant data to be initialized, which will complete
	 * after set channel/DPD trainign is complete
	 */
	while (uMaxRetry-- && (suppData->suppInitialized != TRUE)) {
//        OSATaskSleep(1);
	}
//#endif

	pKeyMsg = GetKeyMsgNonceFromEAPOL(priv, pmbuf, pKeyMgmtInfoSta);
	if (!pKeyMsg) {
		PRINTM(MERROR, "KeyMgmtStaHsk_Recvd_PWKMsg1 pKeyMsg is NULL\n");
		return FALSE;
	}
#ifdef DOT11R
	if (!msgProcessed &&
	    dot11r_is_ft_akm(&connPtr->suppData->customMIB_RSNConfig.AKM)) {
		dot11r_process_pwk_msg1(connPtr,
					sa,
					da,
					pKeyMgmtInfoSta->SNonce,
					pKeyMgmtInfoSta->ANonce);

		msgProcessed = TRUE;
		retval = TRUE;
	}
#endif
#ifdef PSK_SUPPLICANT_CCKM
	if (!msgProcessed && ccx_is_cckm_enabled(connPtr)) {
		retval = cckm_Recvd_PWKMsg1(connPtr,
					    sa,
					    da,
					    pEAPoLBufDesc,
					    pKeyMgmtInfoSta->SNonce,
					    (UINT8 *)connPtr->suppData->
					    hashSsId.SsId,
					    connPtr->suppData->hashSsId.Len);
		genPwkMsg2 = FALSE;
		msgProcessed = TRUE;
	}
#endif

	if (!msgProcessed
	    && supplicantAkmIsWpaWpa2(priv, &suppData->customMIB_RSNConfig.AKM))
	{
		if (supplicantAkmIsWpaWpa2Psk(priv,
					      &suppData->customMIB_RSNConfig.
					      AKM)) {
			/* Selected AKM Suite is PSK based */
			pPmk = pmkCacheFindPSK((void *)priv,
					       (UINT8 *)suppData->hashSsId.SsId,
					       suppData->hashSsId.Len);
		} else {
			pPmk = pmkCacheFindPMK(priv, &suppData->localBssid);
		}

		if (!pPmk) {
			PRINTM(MERROR,
			       "KeyMgmtStaHsk_Recvd_PWKMsg1 pPmk is NULL\n");
			return FALSE;
		}

		KeyMgmtSta_DeriveKeys(priv, pPmk,
				      (UINT8 *)da,
				      (UINT8 *)sa,
				      pKeyMgmtInfoSta->ANonce,
				      pKeyMgmtInfoSta->SNonce,
				      pKeyMgmtInfoSta->EAPOL_MIC_Key,
				      pKeyMgmtInfoSta->EAPOL_Encr_Key,
				      &pKeyMgmtInfoSta->newPWKey,
				      supplicantAkmUsesKdf(priv,
							   &suppData->
							   customMIB_RSNConfig.
							   AKM));

		retval = TRUE;

		/* PMKID checking not used by embedded supplicant.
		 ** Commenting out the code in case it needs to be
		 ** readded later.
		 */
#if 0
		/* Need to check for PMKID response */
		if (pKeyMsg->desc_type == 2) {
			if (keyLen) {
				KDE_t *pKde;

				/* Parse PMKID though it's _not used_ now */
				pKde = parseKeyKDE_DataType(pKeyMsg->key_data,
							    keyLen,
							    KDE_DATA_TYPE_PMKID);

				if (pKde
				    && gcustomMIB_RSNConfig.pmkidValid
				    && memcmp(pKde->data,
					      gcustomMIB_RSNConfig.PMKID,
					      sizeof(gcustomMIB_RSNConfig.
						     PMKID))) {
					/* PMKID could be invalid if generated based on an
					 ** old key. A new key should have been negotiated
					 ** We should regenerate PMKID and check it.
					 */
				}
			}
		}
#endif
	}

	if (genPwkMsg2) {
		/* construct Message 2 */
		if (GeneratePWKMsg2(priv, pmbuf,
				    pKeyMgmtInfoSta->SNonce,
				    pKeyMgmtInfoSta->EAPOL_MIC_Key,
				    0) != SUCCESS) {
			PRINTM(MERROR,
			       "KeyMgmtStaHsk_Recvd_PWKMsg1 GeneratePWKMsg2 Fail\n");
			retval = FALSE;
		}
	}

	if (retval == TRUE) {
#ifdef MIB_STATS
		INC_MIB_STAT(connPtr, eapolSentFrmFwCnt);
#endif
		updateApReplayCounter(priv, pKeyMgmtInfoSta,
				      (UINT8 *)pKeyMsg->replay_cnt);

		pKeyMgmtInfoSta->RSNSecured = FALSE;
	}

	PRINTM(MMSG, "LEAVE: %s\n", __FUNCTION__);
	return retval;
}

EAPOL_KeyMsg_t const *
KeyMgmtStaHsk_Recvd_PWKMsg3(phostsa_private priv, mlan_buffer *pmbuf)
{
	EAPOL_KeyMsg_t *pKeyMsg;
//    cm_ConnectionInfo_t* connPtr = pEAPoLBufDesc->intf.connPtr;
	struct supplicantData *suppData = priv->suppData;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &suppData->keyMgmtInfoSta;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

#ifdef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
	pKeyMsg = patch_ProcessRxEAPOL_PwkMsg3(pEAPoLBufDesc, pKeyMgmtInfoSta);
#else
	pKeyMsg = ProcessRxEAPOL_PwkMsg3(priv, pmbuf, pKeyMgmtInfoSta);
#endif
	if (!pKeyMsg) {
		PRINTM(MERROR, "KeyMgmtStaHsk_Recvd_PWKMsg3 pKeyMsg is NULL\n");
		return NULL;
	}

	/* construct Message 4 */
	if (GeneratePWKMsg4(priv, pmbuf,
			    pKeyMgmtInfoSta,
			    (pKeyMsg->desc_type == 2)) != SUCCESS) {
		PRINTM(MERROR,
		       "KeyMgmtStaHsk_Recvd_PWKMsg3 GeneratePWKMsg4 Fail\n");
		return pKeyMsg;
	}
#ifdef MIB_STATS
	INC_MIB_STAT(connPtr, eapolSentFrmFwCnt);
#endif

	updateApReplayCounter(priv, pKeyMgmtInfoSta,
			      (UINT8 *)pKeyMsg->replay_cnt);

	PRINTM(MMSG, "LEAVE: %s\n", __FUNCTION__);
	return NULL;
}

EAPOL_KeyMsg_t const *
KeyMgmtStaHsk_Recvd_GrpMsg1(phostsa_private priv, mlan_buffer *pmbuf)
{
	EAPOL_KeyMsg_t *pKeyMsg;
	struct supplicantData *suppData = priv->suppData;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &suppData->keyMgmtInfoSta;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

#ifdef WAR_ROM_BUG50312_SIMUL_INFRA_WFD
	pKeyMsg = patch_ProcessRxEAPOL_GrpMsg1(priv, pKeyMgmtInfoSta);
#else
	pKeyMsg = ProcessRxEAPOL_GrpMsg1(priv, pmbuf, pKeyMgmtInfoSta);
#endif
	if (!pKeyMsg) {
		PRINTM(MERROR, "KeyMgmtStaHsk_Recvd_GrpMsg1 pKeyMsg is NULL\n");
		return NULL;
	}
	/* construct Message Grp Msg2 */
	if (GenerateGrpMsg2(priv, pmbuf, pKeyMgmtInfoSta) != SUCCESS) {
		PRINTM(MERROR,
		       "KeyMgmtStaHsk_Recvd_GrpMsg1 GenerateGrpMsg2 Fail\n");
		return NULL;
	}
#ifdef MIB_STATS
	INC_MIB_STAT(connPtr, eapolSentFrmFwCnt);
#endif

	updateApReplayCounter(priv, pKeyMgmtInfoSta,
			      (UINT8 *)pKeyMsg->replay_cnt);

	PRINTM(MMSG, "LEAVE: %s\n", __FUNCTION__);
	return pKeyMsg;
}

void
ProcessKeyMgmtDataSta(phostsa_private priv, mlan_buffer *pmbuf,
		      IEEEtypes_MacAddr_t *sa, IEEEtypes_MacAddr_t *da)
{
	UINT8 retry;
	EAPOL_KeyMsg_t *pKeyMsg =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));

	retry = 0;

	if (pKeyMsg->key_info.KeyType) {
		/* PWK */
		if (pKeyMsg->key_info.KeyMIC) {
			/* 3rd msg in seq */
			KeyMgmtStaHsk_Recvd_PWKMsg3(priv, pmbuf);
		} else {
			while ((KeyMgmtStaHsk_Recvd_PWKMsg1(priv, pmbuf, sa, da)
				== FALSE)
			       && (retry < PWK_MSG1_RETRIES)) {
				/* Delay and retry Msg1 processing in case failure was
				 **  due to the host not having time to program a PMK
				 **  yet for 802.1x AKMPs
				 */
				//hal_WaitInUs(100);
				retry++;
			}
			//KeyMgmtStaHsk_Recvd_PWKMsg1(priv, pmbuf, sa, da);

		}
	} else {
		/* GRP */
		if (!KeyMgmtStaHsk_Recvd_GrpMsg1(priv, pmbuf)) {
#if defined(MEF_ENH) && defined(VISTA_802_11_DRIVER_INTERFACE)
			hostsleep_initiate_wakeup_with_reason
				(WOL_GRP_KEY_REFRESH_ERROR,
				 WOL_VACUOUS_PATTERN_ID);
#endif
		}
	}

}

#if 0
/*
**  This function send a MIC failure event to the AP
*/
void
SendMICFailReport_sta(cm_ConnectionInfo_t * connPtr,
		      keyMgmtInfoSta_t *pKeyMgmtInfoSta, BOOLEAN isUnicast)
{
	EAPOL_KeyMsg_Tx_t *pTxEapol;
	UINT16 frameLen;
	UINT32 int_sta;
	BufferDesc_t *pBufDesc;

	if (pKeyMgmtInfoSta->staCounterHi == 0xffffffff
	    && pKeyMgmtInfoSta->staCounterLo == 0xffffffff) {
		KeyMgmtResetCounter(pKeyMgmtInfoSta);
		return;
	}

	int_sta = os_if_save_EnterCriticalSection();

	/* Since there is a MIC failure drop all packets in Tx queue. */
	while ((pBufDesc = (BufferDesc_t *) getq(&wlan_data_q)) != NULL) {
		/* Do nothing here. We are just dropping the packet
		 **   and releasing the queue.
		 */
		mrvl_HandleTxDone(pBufDesc, 0);
	}

	os_if_save_ExitCriticalSection(int_sta);

	pBufDesc = GetTxEAPOLBuffer(connPtr, &pTxEapol, NULL);

	if (pBufDesc == NULL) {
		return;
	}
	KeyMgmtSta_PrepareEAPOLMicErrFrame(pTxEapol,
					   isUnicast,
					   &connPtr->suppData->localBssid,
					   &connPtr->suppData->localStaAddr,
					   pKeyMgmtInfoSta);
	SetEAPOLKeyDescTypeVersion(pTxEapol,
				   connPtr->suppData->customMIB_RSNConfig.
				   wpaType.wpa2,
				   supplicantAkmUsesKdf(&connPtr->suppData->
							customMIB_RSNConfig.
							AKM),
				   connPtr->suppData->customMIB_RSNConfig.
				   ucstCipher.ccmp);

	if (pKeyMgmtInfoSta->staCounterLo++ == 0) {
		pKeyMgmtInfoSta->staCounterHi++;
	}

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(pTxEapol,
						     pKeyMgmtInfoSta->
						     EAPOL_MIC_Key,
						     EAPOL_PROTOCOL_V1, 0);

	UpdateEAPOLWcbLenAndTransmit(pBufDesc, frameLen);
}
#endif

#ifdef WAR_ROM_BUG57216_QUIET_TIME_INTERVAL
/* This function assumes that argument state would be either
    NO_MIC_FAILURE or FIRST_MIC_FAIL_IN_60_SEC
    It must not be called with state othe than these two
*/
#define MIC_ERROR_QUIET_TIME_INTERVAL           60000000	/* 60 sec */
#define MIC_ERROR_CHECK_TIME_INTERVAL         60000000

void
KeyMgmtSta_handleMICErr(MIC_Fail_State_e state,
			keyMgmtInfoSta_t *pKeyMgmtInfoSta,
			MicroTimerCallback_t callback, UINT8 flags)
{
	UINT32 expiry;
	UINT32 int_save = tx_interrupt_control(TX_INT_DISABLE);

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
	tx_interrupt_control(int_save);
#if 0
	microTimerStop(pKeyMgmtInfoSta->micTimer);

	microTimerStart(callback,
			(UINT32)pKeyMgmtInfoSta,
			expiry, &pKeyMgmtInfoSta->micTimer, flags);
#endif
}
#endif

#if 0
void
supplicantMICCounterMeasureInvoke(cm_ConnectionInfo_t * connPtr,
				  BOOLEAN isUnicast)
{
	MIC_Fail_State_e state;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &connPtr->suppData->keyMgmtInfoSta;

	if (pKeyMgmtInfoSta->sta_MIC_Error.MICCounterMeasureEnabled) {
		state = pKeyMgmtInfoSta->sta_MIC_Error.status;

		/* Watchdog and clear any pending TX packets to ensure that
		 ** We are able to get a TX buffer
		 */
		tx_watchdog_recovery();
		SendMICFailReport_sta(connPtr, pKeyMgmtInfoSta, isUnicast);

		switch (state) {
		case NO_MIC_FAILURE:
			/* Received 1st MIC failure */
			/* Noneed to check if timer is active. It will not be active
			 ** cause this is the first state
			 */
			KeyMgmtSta_handleMICErr(state,
						pKeyMgmtInfoSta,
						MicErrTimerExp_Sta,
						MICRO_TIMER_FLAG_KILL_ON_PS_ENTRY);

			connPtr->suppData->customMIB_RSNStats.
				TKIPLocalMICFailures++;

			break;

		case FIRST_MIC_FAIL_IN_60_SEC:
			/* Received 2 MIC failures within 60 sec. Do deauth from AP */
			connPtr->suppData->customMIB_RSNStats.
				TKIPCounterMeasuresInvoked++;

			KeyMgmtSta_handleMICErr(state,
						pKeyMgmtInfoSta,
						MicErrTimerExp_Sta,
						MICRO_TIMER_FLAG_KILL_ON_PS_ENTRY);

			/* Is this really needed? */
			pKeyMgmtInfoSta->connPtr = connPtr;

			KeyMgmtSta_handleMICDeauthTimer(pKeyMgmtInfoSta,
							DeauthDelayTimerExp_Sta,
							DEAUTH_DELAY_TIME_INTERVAL,
							MICRO_TIMER_FLAG_KILL_ON_PS_ENTRY);

			break;

		case SECOND_MIC_FAIL_IN_60_SEC:
			/*No need to do anything. Everything has been taken care of by
			 ** the above state
			 */

		default:
			break;
		}
	}
	return;
}
#endif
/*
  Start the key Management session
*/
void
keyMgmtSta_StartSession(phostsa_private priv,
			IEEEtypes_MacAddr_t *pBssid,
			IEEEtypes_MacAddr_t *pStaAddr)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	keyMgmtInfoSta_t *pKeyMgmtInfoSta = &priv->suppData->keyMgmtInfoSta;

	//pKeyMgmtInfoSta->psapriv = priv;

	memcpy(util_fns, &priv->suppData->localStaAddr,
	       pStaAddr, sizeof(priv->suppData->localStaAddr));
	memcpy(util_fns, &priv->suppData->localBssid,
	       pBssid, sizeof(priv->suppData->localBssid));

	keyMgmtSta_StartSession_internal(priv, pKeyMgmtInfoSta,
					 //keyMgmtStaRsnSecuredTimeoutHandler,
					 RSNSECUREDTIMEOUT, 0);	//MICRO_TIMER_FLAG_KILL_ON_PS_ENTRY);

}

void
supplicantClrEncryptKey(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_mlan_fns *pm_fns = NULL;

	if (!psapriv)
		return;

	pm_fns = &psapriv->mlan_fns;
	pm_fns->hostsa_clr_encrypt_key(psapriv->pmlan_private);
}

UINT32
keyApi_UpdateKeyMaterial(void *priv, key_MgtMaterial_t *keyMgtData_p)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	hostsa_mlan_fns *pm_fns = &psapriv->mlan_fns;
	//UINT8 wepKeyIndex;
	mlan_ds_encrypt_key encrypt_key;
	t_u8 bcast_addr[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	memset(util_fns, &encrypt_key, 0, sizeof(mlan_ds_encrypt_key));

	PRINTM(MMSG, "keyApi_UpdateKeyMaterial keyType=%x keyLen=%x\n",
	       keyMgtData_p->keyType, keyMgtData_p->keyLen);
	switch (keyMgtData_p->keyType) {
	case KEY_TYPE_TKIP:
	case KEY_TYPE_AES:
		/* The Key Info definition for TKIP and AES is the same */
		if (keyMgtData_p->keyInfo & KEY_INFO_UNICAST) {
			/* Unicast Key */
			//SET_KEY_STATE_ENABLED(pwkey,
			//                      (keyMgtData_p->keyInfo
			//                        & KEY_INFO_ENABLED)? TRUE : FALSE);
			//pwkey->hdr.keyType = keyMgtData_p->keyType;
			//pwkey->hdr.keyDirection = KEY_DIRECTION_RXTX;
			//pwkey->hdr.keyLen = keyMgtData_p->keyLen;

			//if (IS_KEY_STATE_ENABLED(pwkey))
			//{
			//    ramHook_keyApiSta_setConnDataTrafficEnabled(connPtr, TRUE);
			//     ramHook_keyApiSta_setConnCurPktTxEnabled(connPtr, TRUE);
			//     SET_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(pwkey, TRUE);
			// }
			encrypt_key.key_flags |= KEY_FLAG_SET_TX_KEY;
			encrypt_key.key_len = keyMgtData_p->keyLen;
			memcpy(util_fns, encrypt_key.mac_addr,
			       psapriv->suppData->localBssid, MAC_ADDR_SIZE);

			/* The Key Material is different */
			if (keyMgtData_p->keyLen &&
			    (keyMgtData_p->keyType == KEY_TYPE_TKIP)) {
				/* Update key if included */
				memcpy(util_fns,
				       (void *)encrypt_key.key_material,
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.key, TK_SIZE);
				memcpy(util_fns,
				       (void *)&encrypt_key.
				       key_material[TK_SIZE],
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.txMicKey, MIC_KEY_SIZE);
				memcpy(util_fns,
				       (void *)&encrypt_key.
				       key_material[TK_SIZE + MIC_KEY_SIZE],
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.rxMicKey, MIC_KEY_SIZE);
			} else if (keyMgtData_p->keyLen &&
				   (keyMgtData_p->keyType == KEY_TYPE_AES)) {
				/* Update key if included */
				memcpy(util_fns,
				       (uint8 *)encrypt_key.key_material,
				       (uint8 *)keyMgtData_p->keyEncypt.AES.key,
				       TK_SIZE);

				/* duplicate to group key,
				 * for adhoc aes to use.
				 */
				//if (!IS_KEY_STATE_ENABLED(gwkey))
				// {
				/* Multicast Key */
				//   SET_KEY_STATE_ENABLED(gwkey,
				//                          (keyMgtData_p->keyInfo
				//                          & KEY_INFO_ENABLED) ? TRUE : FALSE);
				//   gwkey->hdr.keyType = keyMgtData_p->keyType;
				//   gwkey->hdr.keyDirection = KEY_DIRECTION_RXTX;
				//   gwkey->hdr.keyLen = keyMgtData_p->keyLen;
				//    if (IS_KEY_STATE_ENABLED(gwkey))
				//    {
				//       gwkey->ckd.tkip_aes.loReplayCounter16 = 0;
				//      gwkey->ckd.tkip_aes.hiReplayCounter32 = 0xffffffff;
				//   }
				//   memcpy((uint8*)gwkey->ckd.tkip_aes.key,
				//         (uint8*)keyMgtData_p->keyEncypt.AES.key,
				//         TK_SIZE);
				// }
			}
		}

		if (keyMgtData_p->keyInfo & KEY_INFO_MULTICAST) {
			/* Multicast Key */
			//SET_KEY_STATE_ENABLED(gwkey,
			//                      (keyMgtData_p->
			//                       keyInfo & KEY_INFO_ENABLED) ? TRUE :
			//                      FALSE);
			//gwkey->hdr.keyType = keyMgtData_p->keyType;
			//gwkey->hdr.keyDirection = KEY_DIRECTION_RXTX;
			//gwkey->hdr.keyLen = keyMgtData_p->keyLen;
			//if (IS_KEY_STATE_ENABLED(gwkey))
			//{
			//     gwkey->ckd.tkip_aes.loReplayCounter16 = 0;
			//    gwkey->ckd.tkip_aes.hiReplayCounter32 = 0xffffffff;

			//    if (!IS_KEY_STATE_ENABLED(pwkey))
			//     {
			//        gwkey->ckd.tkip_aes.txIV32 = 0x0;
			//       gwkey->ckd.tkip_aes.txIV16 = 0x1;
			//       ramHook_keyApiSta_setConnDataTrafficEnabled(connPtr, TRUE);
			//      ramHook_keyApiSta_setConnCurPktTxEnabled(connPtr, TRUE);
			//  }
			// }
			encrypt_key.key_flags |= KEY_FLAG_GROUP_KEY;
			encrypt_key.key_len = keyMgtData_p->keyLen;
			memcpy(util_fns, encrypt_key.mac_addr, bcast_addr,
			       MAC_ADDR_SIZE);

			if (keyMgtData_p->keyLen &&
			    (keyMgtData_p->keyType == KEY_TYPE_TKIP)) {
				/* Update key if included */
				memcpy(util_fns,
				       (void *)encrypt_key.key_material,
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.key, TK_SIZE);
				memcpy(util_fns,
				       (void *)&encrypt_key.
				       key_material[TK_SIZE],
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.txMicKey, MIC_KEY_SIZE);
				memcpy(util_fns,
				       (void *)&encrypt_key.
				       key_material[TK_SIZE + MIC_KEY_SIZE],
				       (const void *)keyMgtData_p->keyEncypt.
				       TKIP.rxMicKey, MIC_KEY_SIZE);
			} else if (keyMgtData_p->keyLen &&
				   (keyMgtData_p->keyType == KEY_TYPE_AES)) {
				/* Update key if included */
				memcpy(util_fns,
				       (uint8 *)encrypt_key.key_material,
				       (uint8 *)keyMgtData_p->keyEncypt.AES.key,
				       TK_SIZE);
			}
		}
	/**set pn 0*/
		memset(util_fns, encrypt_key.pn, 0, sizeof(encrypt_key.pn));
	/**key flag*/
		encrypt_key.key_flags |= KEY_FLAG_RX_SEQ_VALID;

		//ramHook_keyApi_PalladiumHook1(connPtr);
	/**set command to fw update key*/
		pm_fns->hostsa_set_encrypt_key((void *)psapriv->pmlan_private,
					       &encrypt_key);

		break;

#ifndef WAR_ROM_BUG54733_PMF_SUPPORT
	case KEY_TYPE_AES_CMAC:
		if ( /*NULL != igwkey && */
			(keyMgtData_p->keyInfo & KEY_INFO_MULTICAST_IGTK)) {
			/* Multicast Key */
			//SET_KEY_STATE_ENABLED(igwkey,
			//                                        (keyMgtData_p->keyInfo
			//                                        & KEY_INFO_ENABLED) ? TRUE : FALSE);
			//igwkey->hdr.keyType = keyMgtData_p->keyType;
			//igwkey->hdr.keyDirection = KEY_DIRECTION_RXTX;
			//igwkey->hdr.keyLen = keyMgtData_p->keyLen;
			if (keyMgtData_p->keyLen) {
				/* Update IPN if included */
				//memcpy((UINT8 *)&igwkey->ckd.tkip_aes.loReplayCounter16,
				//              (UINT8 *)&keyMgtData_p->keyEncypt.iGTK.ipn[0],
				//              sizeof(igwkey->ckd.tkip_aes.loReplayCounter16));

				//memcpy((UINT8 *)&igwkey->ckd.tkip_aes.hiReplayCounter32,
				//              (UINT8 *)&keyMgtData_p->keyEncypt.iGTK.ipn[2],
				//              sizeof(igwkey->ckd.tkip_aes.hiReplayCounter32));

				/* Update key if included */
				//memcpy((UINT8 *)igwkey->ckd.tkip_aes.key,
				//         (UINT8 *)keyMgtData_p->keyEncypt.iGTK.key,
				//         CRYPTO_AES_CMAC_KEY_LEN);
				memcpy(util_fns,
				       (uint8 *)encrypt_key.key_material,
				       (UINT8 *)keyMgtData_p->keyEncypt.iGTK.
				       key, CRYPTO_AES_CMAC_KEY_LEN);
			}
			/**set pn 0*/
			memset(util_fns, encrypt_key.pn, 0,
			       sizeof(encrypt_key.pn));
			/**key flag*/
			encrypt_key.key_flags |= KEY_FLAG_RX_SEQ_VALID;

			//ramHook_keyApi_PalladiumHook1(connPtr);
			/**set command to fw update key*/
			pm_fns->hostsa_set_encrypt_key(psapriv->pmlan_private,
						       &encrypt_key);
		}
		break;
#endif
	}

	return 0;
}

void
FillKeyMaterialStruct(phostsa_private priv,
		      UINT16 key_len, UINT8 isPairwise, KeyData_t *pKey)
{
	key_MgtMaterial_t keyMgtData;

#ifdef MIB_STATS
	if (isPairwise) {
		INC_MIB_STAT(connPtr, PTKSentFrmESUPPCnt);
	} else {
		INC_MIB_STAT(connPtr, GTKSentFrmESUPPCnt);
	}
#endif

	FillKeyMaterialStruct_internal(priv, &keyMgtData, key_len, isPairwise,
				       pKey);
	keyApi_UpdateKeyMaterial(priv, &keyMgtData);
}

void
FillGrpKeyMaterialStruct(phostsa_private priv,
			 UINT16 keyType,
			 UINT8 *pn, UINT8 keyIdx, UINT8 keyLen, KeyData_t *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	key_MgtMaterial_t keyMgtData;

	if (keyType == KDE_DATA_TYPE_IGTK) {
		memset(util_fns, (void *)&keyMgtData, 0x00, sizeof(keyMgtData));

		keyMgtData.keyType = KEY_TYPE_AES_CMAC;
		keyMgtData.keyInfo = KEY_INFO_MULTICAST_IGTK | KEY_INFO_ENABLED;
		keyMgtData.keyLen = keyLen;

		memcpy(util_fns, keyMgtData.keyEncypt.iGTK.ipn, pn,
		       CRYPTO_AES_CMAC_IPN_LEN);
		memcpy(util_fns, keyMgtData.keyEncypt.iGTK.key, pKey->Key,
		       keyLen);
	} else {
		FillKeyMaterialStruct_internal(priv, &keyMgtData, keyLen, FALSE,
					       pKey);
	}

	keyApi_UpdateKeyMaterial(priv, &keyMgtData);
}

void
supplicantInitSession(void *priv,
		      t_u8 *pSsid, t_u16 len, t_u8 *pBssid, t_u8 *pStaAddr)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	if (supplicantIsEnabled((void *)psapriv)) {
		KeyMgmtInitSta(psapriv);
		memcpy(util_fns, (void *)psapriv->suppData->hashSsId.SsId,
		       pSsid, len);
		psapriv->suppData->hashSsId.Len = len;
		keyMgmtSta_StartSession(psapriv, (IEEEtypes_MacAddr_t *)pBssid,
					(IEEEtypes_MacAddr_t *)pStaAddr);
		psapriv->suppData->suppInitialized = TRUE;
		psapriv->gtk_installed = 0;
	}
}

UINT8
supplicantIsCounterMeasuresActive(phostsa_private priv)
{
	return priv->suppData->keyMgmtInfoSta.sta_MIC_Error.disableStaAsso;
}

//#endif

void
init_customApp_mibs(phostsa_private priv, supplicantData_t *suppData)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memset(util_fns, &suppData->customMIB_RSNStats,
	       0x00, sizeof(suppData->customMIB_RSNStats));
	memset(util_fns, &suppData->customMIB_RSNConfig,
	       0x00, sizeof(suppData->customMIB_RSNConfig));
	/* keep noRsn = 1 as default setting */
	suppData->customMIB_RSNConfig.wpaType.noRsn = 1;

}

SecurityMode_t
supplicantCurrentSecurityMode(phostsa_private priv)
{
	return (priv->suppData->customMIB_RSNConfig.wpaType);
}

AkmSuite_t *
supplicantCurrentAkmSuite(phostsa_private priv)
{
	return &priv->suppData->customMIB_RSNConfig.AKM;
}

//#pragma arm section code = ".wlandatapathcode"
t_u8
supplicantIsEnabled(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;

	if (!psapriv || psapriv->suppData == NULL) {
		return 0;
	}

	return (psapriv->suppData->customMIB_RSNConfig.RSNEnabled);
}

//#pragma arm section code

void
supplicantDisable(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;

	if (!supplicantIsEnabled((void *)psapriv)) {
		return;
	}
	psapriv->suppData->customMIB_RSNConfig.RSNEnabled = 0;
	init_customApp_mibs(psapriv, psapriv->suppData);

	PRINTM(MMSG, "supplicantDisable RSNEnabled=%x\n",
	       psapriv->suppData->customMIB_RSNConfig.RSNEnabled);
}

void
supplicantQueryPassphraseAndEnable(void *priv, t_u8 *pbuf)
{
	phostsa_private psapriv = (phostsa_private)priv;
	pmkElement_t *pPMKElement = MNULL;
	mlan_ssid_bssid *ssid_bssid = (mlan_ssid_bssid *)pbuf;
	mlan_802_11_ssid *pssid = &ssid_bssid->ssid;

	if (!psapriv || psapriv->suppData == NULL)
		return;
	if (!ssid_bssid)
		return;
	if (!pssid->ssid_len)
		return;
	/* extract the PSK from the cache entry */
	pPMKElement =
		pmkCacheFindPSKElement((void *)psapriv, pssid->ssid,
				       pssid->ssid_len);
	if (pPMKElement)
		psapriv->suppData->customMIB_RSNConfig.RSNEnabled = 1;
	else
		psapriv->suppData->customMIB_RSNConfig.RSNEnabled = 0;

	PRINTM(MMSG,
	       "supplicantQueryPassphraseAndEnable RSNEnabled=%x ssid=%s\n",
	       psapriv->suppData->customMIB_RSNConfig.RSNEnabled, pssid->ssid);
}

void
supplicantSetAssocRsn(phostsa_private priv,
		      SecurityMode_t wpaType,
		      Cipher_t *pMcstCipher,
		      Cipher_t *pUcstCipher,
		      AkmSuite_t *pAkm,
		      IEEEtypes_RSNCapability_t *pRsnCap,
		      Cipher_t *pGrpMgmtCipher)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	IEEEtypes_RSNCapability_t rsnCap;

	if (pRsnCap == NULL) {
		/* It is being added as an IOT workaround for APs that
		 * do not properly handle association requests that omit
		 * the RSN Capability field in the RSN IE
		 */
		memset(util_fns, &rsnCap, 0x00, sizeof(rsnCap));
		pRsnCap = &rsnCap;
	}

	supplicantSetAssocRsn_internal(priv,
				       &priv->suppData->customMIB_RSNConfig,
				       &priv->suppData->currParams,
				       wpaType,
				       pMcstCipher,
				       pUcstCipher,
				       pAkm, pRsnCap, pGrpMgmtCipher);
}

UINT16
keyMgmtFormatWpaRsnIe(phostsa_private priv,
		      UINT8 *pos,
		      IEEEtypes_MacAddr_t *pBssid,
		      IEEEtypes_MacAddr_t *pStaAddr,
		      UINT8 *pPmkid, BOOLEAN addPmkid)
{
	struct supplicantData *suppData = priv->suppData;

	return keyMgmtFormatWpaRsnIe_internal(priv,
					      &suppData->customMIB_RSNConfig,
					      pos,
					      pBssid,
					      pStaAddr, pPmkid, addPmkid);
}

static void
install_wpa_none_keys(phostsa_private priv, UINT8 type, UINT8 unicast)
{
	UINT8 *pPMK;
	key_MgtMaterial_t keyMgtData;

	pPMK = pmkCacheFindPSK((void *)priv,
			       (UINT8 *)priv->suppData->hashSsId.SsId,
			       priv->suppData->hashSsId.Len);
	if (pPMK == NULL) {
		return;
	}

	install_wpa_none_keys_internal(priv, &keyMgtData, pPMK, type, unicast);

	keyApi_UpdateKeyMaterial(priv, &keyMgtData);

	/* there's no timer or other to initialize */
	KeyMgmtInitSta(priv);
	priv->suppData->keyMgmtInfoSta.RSNSecured = TRUE;
}

void
supplicantInstallWpaNoneKeys(phostsa_private priv)
{
	if (priv->suppData->customMIB_RSNConfig.RSNEnabled
	    && priv->suppData->customMIB_RSNConfig.wpaType.wpaNone) {
		install_wpa_none_keys(priv,
				      priv->suppData->customMIB_RSNConfig.
				      mcstCipher.ccmp, 0);
		install_wpa_none_keys(priv,
				      priv->suppData->customMIB_RSNConfig.
				      mcstCipher.ccmp, 1);
	}
}

void
supplicantSetProfile(phostsa_private priv,
		     SecurityMode_t wpaType,
		     Cipher_t mcstCipher, Cipher_t ucstCipher)
{
	priv->suppData->currParams.wpaType = wpaType;
	priv->suppData->currParams.mcstCipher = mcstCipher;
	priv->suppData->currParams.ucstCipher = ucstCipher;
}

void
supplicantGetProfile(phostsa_private priv,
		     SecurityMode_t *pWpaType,
		     Cipher_t *pMcstCipher, Cipher_t *pUcstCipher)
{
	*pWpaType = priv->suppData->currParams.wpaType;
	*pMcstCipher = priv->suppData->currParams.mcstCipher;
	*pUcstCipher = priv->suppData->currParams.ucstCipher;
}

void
supplicantGetProfileCurrent(phostsa_private priv,
			    SecurityMode_t *pWpaType,
			    Cipher_t *pMcstCipher, Cipher_t *pUcstCipher)
{
	*pWpaType = priv->suppData->customMIB_RSNConfig.wpaType;
	*pMcstCipher = priv->suppData->customMIB_RSNConfig.mcstCipher;
	*pUcstCipher = priv->suppData->customMIB_RSNConfig.ucstCipher;
}

void
supplicantInit(void *priv, supplicantData_t *suppData)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;

	init_customApp_mibs(priv, suppData);

	memset(util_fns, &suppData->currParams, 0xff, sizeof(SecurityParams_t));
	memset(util_fns, &suppData->keyMgmtInfoSta, 0,
	       sizeof(keyMgmtInfoSta_t));
	suppData->keyMgmtInfoSta.sta_MIC_Error.disableStaAsso = 0;
	suppData->keyMgmtInfoSta.sta_MIC_Error.MICCounterMeasureEnabled = 1;
	suppData->keyMgmtInfoSta.sta_MIC_Error.status = NO_MIC_FAILURE;
	KeyMgmtResetCounter(&suppData->keyMgmtInfoSta);
}

void
supplicantStopSessionTimer(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = NULL;

	if (!psapriv || psapriv->suppData == NULL) {
		return;
	}

	util_fns = &psapriv->util_fns;
	if (psapriv->suppData->keyMgmtInfoSta.rsnTimer) {
		util_fns->moal_stop_timer(util_fns->pmoal_handle,
					  psapriv->suppData->keyMgmtInfoSta.
					  rsnTimer);
		//priv->suppData->keyMgmtInfoSta.rsnTimer = 0;
	}
}

void
supplicantSmeResetNotification(phostsa_private priv)
{
	supplicantStopSessionTimer(priv);
}

UINT16
keyMgmtGetKeySize(phostsa_private priv, UINT8 isPairwise)
{
	return keyMgmtGetKeySize_internal(&priv->suppData->customMIB_RSNConfig,
					  isPairwise);
}

void
keyMgmtSetMICKey(phostsa_private priv, UINT8 *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, priv->suppData->keyMgmtInfoSta.EAPOL_MIC_Key,
	       pKey, sizeof(priv->suppData->keyMgmtInfoSta.EAPOL_MIC_Key));
}

UINT8 *
keyMgmtGetMICKey(phostsa_private priv)
{
	return (priv->suppData->keyMgmtInfoSta.EAPOL_MIC_Key);
}

void
keyMgmtSetEAPOLEncrKey(phostsa_private priv, UINT8 *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, priv->suppData->keyMgmtInfoSta.EAPOL_Encr_Key,
	       pKey, sizeof(priv->suppData->keyMgmtInfoSta.EAPOL_Encr_Key));
}

void
keyMgmtSetTemporalKeyOnly(phostsa_private priv, UINT8 *pTk)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, &priv->suppData->keyMgmtInfoSta.newPWKey.Key,
	       pTk, sizeof(priv->suppData->keyMgmtInfoSta.newPWKey.Key));
}

UINT8 *
keyMgmtGetEAPOLEncrKey(phostsa_private priv)
{
	return (priv->suppData->keyMgmtInfoSta.EAPOL_Encr_Key);
}

void
keyMgmtSetPairwiseKey(phostsa_private priv, KeyData_t *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, &priv->suppData->keyMgmtInfoSta.newPWKey,
	       pKey, sizeof(priv->suppData->keyMgmtInfoSta.newPWKey));
}

void
keyMgmtSetGroupKey(phostsa_private priv, KeyData_t *pKey)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, &priv->suppData->keyMgmtInfoSta.GRKey,
	       pKey, sizeof(priv->suppData->keyMgmtInfoSta.GRKey));

	FillKeyMaterialStruct(priv,
			      keyMgmtGetKeySize(priv, FALSE), FALSE, pKey);
}

void
keyMgmtSetGtk(phostsa_private priv, IEEEtypes_GtkElement_t * pGtk, UINT8 *pKek)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 encrKeyLen;

	/* Determine the encrypted key field length from the IE length */
	encrKeyLen = pGtk->Len - (sizeof(pGtk->KeyInfo) +
				  sizeof(pGtk->KeyLen) + sizeof(pGtk->RSC));

	MRVL_AesUnWrap(pKek,
		       2,
		       encrKeyLen / 8 - 1,
		       (UINT8 *)pGtk->Key, NULL, (UINT8 *)pGtk->Key);

	memcpy(util_fns, &priv->suppData->keyMgmtInfoSta.GRKey.Key,
	       (UINT8 *)pGtk->Key,
	       sizeof(priv->suppData->keyMgmtInfoSta.GRKey.Key));

	FillGrpKeyMaterialStruct(priv,
				 KDE_DATA_TYPE_GTK,
				 pGtk->RSC,
				 pGtk->KeyInfo.KeyId,
				 pGtk->KeyLen,
				 &priv->suppData->keyMgmtInfoSta.GRKey);
}

void
keyMgmtSetIGtk(phostsa_private priv, keyMgmtInfoSta_t *pKeyMgmtInfoSta,
	       IGtkKde_t *pIGtkKde, UINT8 iGtkKdeLen)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	UINT8 iGtkLen;

	iGtkLen = iGtkKdeLen - 12;	/* OUI + dataType + keyId + IPN = 12 bytes */

	memcpy(util_fns, &pKeyMgmtInfoSta->IGtk.Key,
	       (UINT8 *)pIGtkKde->IGtk,
	       MIN(sizeof(pKeyMgmtInfoSta->IGtk.Key), iGtkLen));

	FillGrpKeyMaterialStruct(priv,
				 KDE_DATA_TYPE_IGTK,
				 pIGtkKde->IPN,
				 pIGtkKde->keyId[0],
				 iGtkLen, &pKeyMgmtInfoSta->IGtk);

}

UINT8 *
keyMgmtGetIGtk(phostsa_private priv)
{
	return (priv->suppData->keyMgmtInfoSta.IGtk.Key);
}

void
keyMgmtPlumbPairwiseKey(phostsa_private priv)
{
	hostsa_util_fns *util_fns = &priv->util_fns;

	memcpy(util_fns, &priv->suppData->keyMgmtInfoSta.PWKey,
	       &priv->suppData->keyMgmtInfoSta.newPWKey,
	       sizeof(priv->suppData->keyMgmtInfoSta.PWKey));

	FillKeyMaterialStruct(priv,
			      keyMgmtGetKeySize(priv, TRUE),
			      TRUE, &priv->suppData->keyMgmtInfoSta.PWKey);
}

#if 0
#pragma arm section code = ".wlandatapathcode"
void
keyMgmtSuccessfulDecryptNotify(cm_ConnectionInfo_t * connPtr,
			       cipher_key_t *pRxCipherKey)
{
	if (supplicantIsEnabled(connPtr)) {
		connPtr->suppData->keyMgmtInfoSta.pRxDecryptKey = pRxCipherKey;

		if (connPtr->suppData->keyMgmtInfoSta.pRxDecryptKey &&
		    (!connPtr->suppData->customMIB_RSNConfig.RSNEnabled ||
		     (connPtr->suppData->customMIB_RSNConfig.RSNEnabled &&
		      connPtr->suppData->keyMgmtInfoSta.pwkHandshakeComplete)))
		{
			SET_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(connPtr->
							      suppData->
							      keyMgmtInfoSta.
							      pRxDecryptKey,
							      FALSE);
		}
	} else {
		if (pRxCipherKey &&
		    (!connPtr->cmFlags.RSNEnabled ||
		     (connPtr->cmFlags.RSNEnabled &&
		      connPtr->cmFlags.gDataTrafficEnabled))) {
			SET_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(pRxCipherKey,
							      FALSE);
		}
	}
}

#pragma arm section code
#endif
static void
keyMgmtKeyGroupTxDone(phostsa_private priv)
{
	if (priv->gtk_installed)
		return;
	/*
	 **  if (!pBufDesc || (pBufDesc->rsvd & 0x00FF == 0))
	 **
	 **  Removed check to verify the 4th message was a success.  If we
	 **   miss the ACK from the 4th(WPA2)/2nd(WPA) message, but the AP
	 **   received it, then it won't retry and we will be stuck waiting for
	 **   a session timeout.
	 **
	 **  Could add back later if we retry the message in case of TX failure.
	 */
	FillKeyMaterialStruct(priv,
			      keyMgmtGetKeySize(priv, FALSE),
			      FALSE, &priv->suppData->keyMgmtInfoSta.GRKey);

	priv->suppData->keyMgmtInfoSta.RSNDataTrafficEnabled = TRUE;

	if (priv->suppData->keyMgmtInfoSta.RSNSecured == FALSE) {
		priv->suppData->keyMgmtInfoSta.RSNSecured = TRUE;

		keyMgmtControlledPortOpen(priv);
	}
#ifdef MULTI_CH_SW
	chmgr_UnlockCh(connPtr, 0);
#endif

	//return NULL;
}

static void
keyMgmtKeyPairwiseTxDone(phostsa_private priv)
{
	if (!priv->suppData->keyMgmtInfoSta.pwkHandshakeComplete) {
		/*
		 **  if (!pBufDesc || (pBufDesc->rsvd & 0x00FF == 0))
		 **
		 **  Removed check to verify the 4th message was a success.  If we
		 **   miss the ACK from the 4th(WPA2) message, but the AP
		 **   received it, then it won't retry and we will be stuck waiting for
		 **   a session timeout.
		 **
		 **  Could add back later if we retry the message in case of TX failure.
		 */
		keyMgmtPlumbPairwiseKey(priv);

		priv->suppData->keyMgmtInfoSta.pwkHandshakeComplete = TRUE;

		if (priv->suppData->keyMgmtInfoSta.pRxDecryptKey &&
		    priv->suppData->keyMgmtInfoSta.pwkHandshakeComplete) {
			SET_KEY_STATE_FORCE_EAPOL_UNENCRYPTED(priv->suppData->
							      keyMgmtInfoSta.
							      pRxDecryptKey,
							      FALSE);
		}
	}
}

static
	void
keyMgmtKeyPairAndGroupTxDone(phostsa_private priv)
{
	if (!priv->suppData->keyMgmtInfoSta.pwkHandshakeComplete) {
		keyMgmtKeyPairwiseTxDone(priv);
		keyMgmtKeyGroupTxDone(priv);
	}
}

void
keyMgmtControlledPortOpen(phostsa_private priv)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;

	supplicantStopSessionTimer((void *)priv);

	pm_fns->Hostsa_StaControlledPortOpen(pm_fns->pmlan_private);
}

#ifdef WAR_ROM_BUG42707_RSN_IE_LEN_CHECK

BOOLEAN
patch_supplicantParseRsnIe(phostsa_private priv, IEEEtypes_RSNElement_t *pRsnIe,
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

	UINT8 *pGrpMgmtCipher;

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
	memcpy(util_fns, &pwsKeyCnt, pIeData, sizeof(pwsKeyCnt));
	pIeData += sizeof(pRsnIe->PwsKeyCnt);

	pPwsKeyCipherList = pIeData;
	pIeData += pwsKeyCnt * sizeof(pRsnIe->PwsKeyCipherList);
	supplicantParseUcstCipher(priv, pUcstCipherOut, pwsKeyCnt,
				  pPwsKeyCipherList);

	/* Parse and return the AKM list */
	memcpy(util_fns, &authKeyCnt, pIeData, sizeof(authKeyCnt));
	pIeData += sizeof(pRsnIe->AuthKeyCnt);

	pAuthKeyList = pIeData;
	pIeData += authKeyCnt * sizeof(pRsnIe->AuthKeyList);
	memset(util_fns, pAkmListOut, 0x00, akmOutMax * sizeof(AkmSuite_t));
	memcpy(util_fns, pAkmListOut,
	       pAuthKeyList,
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
		pIeData += sizeof(pRsnIe->PMKIDCnt);

		/* Check if the PMKID List is included */
		if (pIeData < pIeEnd) {
			/* pPMKIDList = pIeData; <-- Currently not used in parsing */
			pIeData += *pPMKIDCnt * sizeof(pRsnIe->PMKIDList);
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

	return TRUE;
}

#endif

//#pragma arm section code = ".init"
void
keyMgmtSta_RomInit(void)
{
//#if defined(PSK_SUPPLICANT) || defined (WPA_NONE)
	//ramHook_keyMgmtProcessMsgExt = keyMgmtProcessMsgExt;
	//ramHook_keyMgmtSendDeauth = keyMgmtSendDeauth2Peer;
//#endif

#ifdef WAR_ROM_BUG42707_RSN_IE_LEN_CHECK
	supplicantParseRsnIe_hook = patch_supplicantParseRsnIe;
#endif

}

//#pragma arm section code

#if defined(BTAMP)
UINT8 *
parseKeyDataField(cm_ConnectionInfo_t * connPtr, UINT8 *pKey, UINT16 len)
{
	keyMgmtInfoSta_t *pKeyMgmtInfoSta;
	KDE_t *pKde;

	pKeyMgmtInfoSta = &connPtr->suppData->keyMgmtInfoSta;

	/* parse KDE GTK */
	pKde = parseKeyDataGTK(pKey, len, &pKeyMgmtInfoSta->GRKey);

	/* Parse PMKID though it's _not used_ now */

	pKde = parseKeyKDE_DataType(pKey, len, KDE_DATA_TYPE_PMKID);

	if (pKde) {
		/* PMKID KDE */
		return (UINT8 *)pKde->data;
	}

	return NULL;
}

/* Add RSN IE to a frame body */
UINT16
btampAddRsnIe(cm_ConnectionInfo_t * connPtr, IEEEtypes_RSNElement_t *pRsnIe)
{
	const uint8 wpa2_psk[4] = { 0x00, 0x0f, 0xac, 0x02 };	/* WPA2 PSK */
	UINT16 ieSize;
	IEEEtypes_RSNCapability_t rsncap;
	SecurityMode_t securityMode;
	Cipher_t mcstWpa2;
	Cipher_t ucstWpa2;
	AkmSuite_t *pAkmWpa2;

	memset(util_fns, &securityMode, 0x00, sizeof(securityMode));
	memset(util_fns, &mcstWpa2, 0x00, sizeof(mcstWpa2));
	memset(util_fns, &ucstWpa2, 0x00, sizeof(ucstWpa2));
	memset(util_fns, &rsncap, 0, sizeof(rsncap));

	mcstWpa2.ccmp = 1;
	ucstWpa2.ccmp = 1;
	securityMode.wpa2 = 1;

	pAkmWpa2 = (AkmSuite_t *)wpa2_psk;

	supplicantSetProfile(connPtr, securityMode, mcstWpa2, ucstWpa2);

	supplicantSetAssocRsn(connPtr, securityMode, &mcstWpa2, &ucstWpa2,
			      pAkmWpa2, &rsncap, NULL);

	ieSize = keyMgmtFormatWpaRsnIe(connPtr,
				       (UINT8 *)pRsnIe,
				       NULL, NULL, NULL, FALSE);
	return ieSize;
}
#endif
#if 0
void
supplicantParseAndFormatRsnIe(phostsa_private priv,
			      IEEEtypes_RSNElement_t *pRsnIe,
			      SecurityMode_t *pWpaTypeOut,
			      Cipher_t *pMcstCipherOut,
			      Cipher_t *pUcstCipherOut, AkmSuite_t *pAkmListOut,
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

	UINT8 *pGrpMgmtCipher;

//longl add
	UINT8 *pos = NULL;
	UINT8 cp_size = 0;
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
	memset(util_fns, (UINT8 *)priv->suppData->wpa_rsn_ie, 0x00,
	       MAX_IE_SIZE);
	pos = (UINT8 *)priv->suppData->wpa_rsn_ie;

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

	cp_size = pIeData - (UINT8 *)pRsnIe;
	memcpy(util_fns, pos, (UINT8 *)pRsnIe, cp_size);
	pos += cp_size;

	/* Parse the pairwise key cipher list */
	memcpy(util_fns, &pwsKeyCnt, pIeData, sizeof(pwsKeyCnt));
	pIeData += sizeof(pRsnIe->PwsKeyCnt);

	if (pwsKeyCnt > 0) {
		(*(UINT16 *)pos) = (UINT16)1;
		pos += sizeof(UINT16);
	}

	pPwsKeyCipherList = pIeData;
	pIeData += pwsKeyCnt * sizeof(pRsnIe->PwsKeyCipherList);
	supplicantParseUcstCipher(priv, pUcstCipherOut, pwsKeyCnt,
				  pPwsKeyCipherList);

	if (pUcstCipherOut->ccmp == 1) {
		memcpy(util_fns, pos, wpa2_oui04, sizeof(wpa2_oui04));
		pos += sizeof(wpa2_oui04);
	} else if (pUcstCipherOut->tkip == 1) {
		memcpy(util_fns, pos, wpa2_oui02, sizeof(wpa2_oui02));
		pos += sizeof(wpa2_oui02);
	}
	if ((pUcstCipherOut->ccmp == 1) && (pUcstCipherOut->tkip == 1))
		pUcstCipherOut->tkip = 0;

	cp_size = pIeEnd - pIeData;
	memcpy(util_fns, pos, pIeData, cp_size);
	pos += cp_size;
	((IEEEtypes_RSNElement_t *)(priv->suppData->wpa_rsn_ie))->Len =
		pos - (UINT8 *)priv->suppData->wpa_rsn_ie -
		sizeof(IEEEtypes_InfoElementHdr_t);

	/* Parse and return the AKM list */
	memcpy(util_fns, &authKeyCnt, pIeData, sizeof(authKeyCnt));
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
		pIeData += sizeof(pRsnIe->PMKIDCnt);

		/* Check if the PMKID List is included */
		if (pIeData < pIeEnd) {
			/* pPMKIDList = pIeData; <-- Currently not used in parsing */
			pIeData += *pPMKIDCnt * sizeof(pRsnIe->PMKIDList);
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

void
supplicantParseAndFormatWpaIe(phostsa_private priv, IEEEtypes_WPAElement_t *pIe,
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
	UINT8 *pos = NULL;
	UINT8 cp_size = 0;
	UINT8 *pIeEnd =
		(UINT8 *)pIe + sizeof(IEEEtypes_InfoElementHdr_t) + pIe->Len;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	memset(util_fns, pMcstCipher, 0x00, sizeof(Cipher_t));
	memset(util_fns, pUcstCipher, 0x00, sizeof(Cipher_t));
	memset(util_fns, pAkmList, 0x00, akmOutMax * sizeof(AkmSuite_t));
	memset(util_fns, pWpaType, 0x00, sizeof(SecurityMode_t));
	memset(util_fns, (UINT8 *)priv->suppData->wpa_rsn_ie, 0x00,
	       MAX_IE_SIZE);
	pos = (UINT8 *)priv->suppData->wpa_rsn_ie;

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

	cp_size = (UINT8 *)(&pTemp->PwsKeyCnt) - (UINT8 *)pIe;
	memcpy(util_fns, pos, (UINT8 *)pIe, cp_size);
	pos += cp_size;

	count = pTemp->PwsKeyCnt;

	if (count > 0) {
		(*(UINT16 *)pos) = (UINT16)1;
		pos += sizeof(UINT16);
	}

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

	if (pUcstCipher->ccmp == 1) {
		memcpy(util_fns, pos, wpa_oui04, sizeof(wpa_oui04));
		pos += sizeof(wpa_oui04);
	} else if (pUcstCipher->tkip == 1) {
		memcpy(util_fns, pos, wpa_oui02, sizeof(wpa_oui02));
		pos += sizeof(wpa_oui02);
	}
	if ((pUcstCipher->ccmp == 1) && (pUcstCipher->tkip == 1))
		pUcstCipher->tkip = 0;

	cp_size = pIeEnd - (UINT8 *)(&pTemp->AuthKeyCnt);
	memcpy(util_fns, pos, &pTemp->AuthKeyCnt, cp_size);
	pos += cp_size;
	((IEEEtypes_RSNElement_t *)(priv->suppData->wpa_rsn_ie))->Len =
		pos - (UINT8 *)priv->suppData->wpa_rsn_ie -
		sizeof(IEEEtypes_InfoElementHdr_t);

	count = pTemp->AuthKeyCnt;

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
#endif

void *
processRsnWpaInfo(void *priv, void *prsnwpa_ie)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	Cipher_t mcstCipher;
	Cipher_t ucstCipher;
	SecurityMode_t wpaType;
	AkmSuite_t akmList[AKM_SUITE_MAX];
	IEEEtypes_RSNCapability_t rsnCap;
	t_u8 type = ((IEEEtypes_InfoElementHdr_t *)prsnwpa_ie)->ElementId;

	if (supplicantIsEnabled((void *)psapriv)) {

		memset(util_fns, &wpaType, 0x00, sizeof(wpaType));

		if (type == ELEM_ID_RSN || type == ELEM_ID_VENDOR_SPECIFIC) {
			if (type == ELEM_ID_RSN) {
				supplicantParseRsnIe(psapriv,
						     (IEEEtypes_RSNElement_t *)
						     prsnwpa_ie, &wpaType,
						     &mcstCipher, &ucstCipher,
						     akmList,
						     NELEMENTS(akmList),
						     &rsnCap, NULL);
			} else if (type == ELEM_ID_VENDOR_SPECIFIC) {
				supplicantParseWpaIe(psapriv,
						     (IEEEtypes_WPAElement_t *)
						     prsnwpa_ie, &wpaType,
						     &mcstCipher, &ucstCipher,
						     akmList,
						     NELEMENTS(akmList));
			}

			if (wpaType.wpa2 || wpaType.wpa) {
				memset(util_fns, &rsnCap, 0x00, sizeof(rsnCap));

				supplicantSetProfile(psapriv, wpaType,
						     mcstCipher, ucstCipher);

				supplicantSetAssocRsn(psapriv,
						      wpaType,
						      &mcstCipher,
						      &ucstCipher,
						      akmList, &rsnCap, NULL);

				memset(util_fns,
				       (UINT8 *)psapriv->suppData->wpa_rsn_ie,
				       0x00, MAX_IE_SIZE);
				if (keyMgmtFormatWpaRsnIe
				    (psapriv,
				     (UINT8 *)&psapriv->suppData->wpa_rsn_ie,
				     &psapriv->suppData->localBssid,
				     &psapriv->suppData->localStaAddr, NULL,
				     FALSE))
					return (void *)(psapriv->suppData->
							wpa_rsn_ie);
			}
		}

	}
	return NULL;
}

t_u8
supplicantFormatRsnWpaTlv(void *priv, void *rsn_wpa_ie, void *rsn_ie_tlv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	void *supp_rsn_wpa_ie = NULL;
	MrvlIEGeneric_t *prsn_ie = (MrvlIEGeneric_t *)rsn_ie_tlv;
	t_u8 total_len = 0;

	if (rsn_wpa_ie) {
		supp_rsn_wpa_ie =
			processRsnWpaInfo((void *)psapriv, rsn_wpa_ie);
		if (!supp_rsn_wpa_ie)
			return total_len;

		/* WPA_IE or RSN_IE */
		prsn_ie->IEParam.Type =
			(t_u16)(((IEEEtypes_InfoElementHdr_t *)
				 supp_rsn_wpa_ie)->ElementId);
		prsn_ie->IEParam.Type = prsn_ie->IEParam.Type & 0x00FF;
		prsn_ie->IEParam.Type = wlan_cpu_to_le16(prsn_ie->IEParam.Type);
		prsn_ie->IEParam.Length =
			(t_u16)(((IEEEtypes_InfoElementHdr_t *)
				 supp_rsn_wpa_ie)->Len);
		prsn_ie->IEParam.Length = prsn_ie->IEParam.Length & 0x00FF;
		if (prsn_ie->IEParam.Length <= MAX_IE_SIZE) {
			memcpy(util_fns, rsn_ie_tlv + sizeof(prsn_ie->IEParam),
			       (t_u8 *)supp_rsn_wpa_ie +
			       sizeof(IEEEtypes_InfoElementHdr_t),
			       prsn_ie->IEParam.Length);
		} else
			return total_len;

		HEXDUMP("ASSOC_CMD: RSN IE", (t_u8 *)rsn_ie_tlv,
			sizeof(prsn_ie->IEParam) + prsn_ie->IEParam.Length);
		total_len += sizeof(prsn_ie->IEParam) + prsn_ie->IEParam.Length;
		prsn_ie->IEParam.Length =
			wlan_cpu_to_le16(prsn_ie->IEParam.Length);
	}
	return total_len;
}
