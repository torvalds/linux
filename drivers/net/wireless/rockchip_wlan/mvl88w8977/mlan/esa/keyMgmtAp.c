/** @file keyMgntAP.c
 *
 *  @brief This file defined the eapol paket process and key management for authenticator
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

#include "wl_macros.h"
#include "wlpd.h"
#include "pass_phrase.h"
#include "sha1.h"
#include "crypt_new.h"
#include "parser.h"
#include "keyCommonDef.h"
#include  "keyMgmtStaTypes.h"
#include "AssocAp_srv_rom.h"
#include "pmkCache.h"
#include "keyMgmtApTypes.h"
#include "keyMgmtAp.h"
#include "rc4.h"
#include "authenticator_api.h"

//////////////////////
// STATIC FUNCTIONS
//////////////////////

//#ifdef AUTHENTICATOR
////////////////////////
// FORWARD DECLARATIONS
////////////////////////
Status_e GeneratePWKMsg3(hostsa_private *priv, cm_Connection *connPtr);
Status_e GenerateGrpMsg1(hostsa_private *priv, cm_Connection *connPtr);
Status_e GenerateApEapolMsg(hostsa_private *priv,
			    t_void *pconnPtr, keyMgmtState_e msgState);

static void
handleFailedHSK(t_void *priv, t_void *pconnPtr, IEEEtypes_ReasonCode_t reason)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_mlan_fns *pm_fns = &psapriv->mlan_fns;
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;

	KeyMgmtStopHskTimer(connPtr);
	pm_fns->hostsa_SendDeauth(pm_fns->pmlan_private, connPtr->mac_addr,
				  (t_u16)reason);
}

static void
incrementReplayCounter(apKeyMgmtInfoSta_t *pKeyMgmtInfo)
{
	if (++pKeyMgmtInfo->counterLo == 0) {
		pKeyMgmtInfo->counterHi++;
	}
}

int
isValidReplayCount(t_void *priv, apKeyMgmtInfoSta_t *pKeyMgmtInfo,
		   UINT8 *pRxReplayCount)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	UINT32 rxCounterHi;
	UINT32 rxCounterLo;

	memcpy(util_fns, &rxCounterHi, pRxReplayCount, 4);
	memcpy(util_fns, &rxCounterLo, pRxReplayCount + 4, 4);

	if ((pKeyMgmtInfo->counterHi == WORD_SWAP(rxCounterHi)) &&
	    (pKeyMgmtInfo->counterLo == WORD_SWAP(rxCounterLo))) {
		return 1;
	}
	return 0;
}

void
KeyMgmtSendGrpKeyMsgToAllSta(hostsa_private *priv)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	cm_Connection *connPtr = MNULL;
	t_void *sta_node = MNULL;

	ENTER();

	pm_fns->Hostsa_find_connection(priv->pmlan_private, (t_void *)&connPtr,
				       &sta_node);

	while (connPtr != MNULL) {
		pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;

		if (pKeyMgmtInfo->rom.keyMgmtState == HSK_END) {
			GenerateApEapolMsg(priv, connPtr,
					   GRP_REKEY_MSG1_PENDING);
		} else if ((pKeyMgmtInfo->rom.keyMgmtState == WAITING_4_GRPMSG2)
			   || (pKeyMgmtInfo->rom.staSecType.wpa2 &&
			       (pKeyMgmtInfo->rom.keyMgmtState ==
				WAITING_4_MSG4)) ||
			   (pKeyMgmtInfo->rom.keyMgmtState ==
			    WAITING_4_GRP_REKEY_MSG2)) {
			// TODO:How to handle group rekey if either Groupwise handshake
			// Group rekey is already in progress for this STA?
		}

		pm_fns->Hostsa_find_next_connection(priv->pmlan_private,
						    (t_void *)&connPtr,
						    &sta_node);
	}

	LEAVE();
}

UINT32
keyApi_ApUpdateKeyMaterial(void *priv, cm_Connection *connPtr,
			   BOOLEAN updateGrpKey)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	hostsa_mlan_fns *pm_fns = &psapriv->mlan_fns;
	BssConfig_t *pBssConfig = MNULL;
	KeyData_t *pKeyData = MNULL;
	//cipher_key_buf_t *pCipherKeyBuf;
	Cipher_t *pCipher = MNULL;
	//KeyData_t pwsKeyData;
	mlan_ds_encrypt_key encrypt_key;
	t_u8 bcast_addr[MAC_ADDR_SIZE] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	apInfo_t *pApInfo = &psapriv->apinfo;

	pBssConfig = &pApInfo->bssConfig;

	if (pBssConfig->SecType.wpa || pBssConfig->SecType.wpa2) {
		memset(util_fns, &encrypt_key, 0, sizeof(mlan_ds_encrypt_key));

		//connPtr->cmFlags.RSNEnabled = TRUE;
		if (updateGrpKey == TRUE) {
			pKeyData = &pApInfo->bssData.grpKeyData;
			pCipher = &pBssConfig->RsnConfig.mcstCipher;
		} else if (connPtr) {
			/*  pCipherKeyBuf = connPtr->pwTxRxCipherKeyBuf;
			   memcpy((void*)&pwsKeyData,
			   (void*)&pCipherKeyBuf->cipher_key.ckd.hskData.pwsKeyData,
			   sizeof(KeyData_t)); */
			pKeyData = &connPtr->hskData.pwsKeyData;
			pCipher =
				&connPtr->staData.keyMgmtInfo.rom.staUcstCipher;
		}

		if (updateGrpKey == TRUE) {
			memcpy(util_fns, encrypt_key.mac_addr, bcast_addr,
			       MAC_ADDR_SIZE);
			encrypt_key.key_flags |= KEY_FLAG_GROUP_KEY;
		} else if (connPtr) {
			memcpy(util_fns, encrypt_key.mac_addr,
			       connPtr->mac_addr, MAC_ADDR_SIZE);
			encrypt_key.key_flags |= KEY_FLAG_SET_TX_KEY;
		}

		if (!pKeyData || !pCipher) {
			PRINTM(MERROR, "Invalid KeyData or Cipher pointer!\n");
			return 1;
		}

		if (pCipher->ccmp) {
	    /**AES*/
			encrypt_key.key_len = TK_SIZE;
			memcpy(util_fns, encrypt_key.key_material,
			       pKeyData->Key, TK_SIZE);
		} else if (pCipher->tkip) {
	    /**TKIP*/
			encrypt_key.key_len =
				TK_SIZE + MIC_KEY_SIZE + MIC_KEY_SIZE;
			memcpy(util_fns, encrypt_key.key_material,
			       pKeyData->Key, TK_SIZE);
			memcpy(util_fns, &encrypt_key.key_material[TK_SIZE],
			       pKeyData->TxMICKey, MIC_KEY_SIZE);
			memcpy(util_fns,
			       &encrypt_key.key_material[TK_SIZE +
							 MIC_KEY_SIZE],
			       pKeyData->RxMICKey, MIC_KEY_SIZE);
		}
	/**set pn 0*/
		memset(util_fns, encrypt_key.pn, 0, PN_SIZE);

	/**key flag*/
		encrypt_key.key_flags |= KEY_FLAG_RX_SEQ_VALID;
	/**key index*/
		encrypt_key.key_index = pKeyData->KeyIndex;
	/**set command to fw update key*/
		pm_fns->hostsa_set_encrypt_key(psapriv->pmlan_private,
					       &encrypt_key);

	}

	return 0;
}

void
GenerateGTK(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	apInfo_t *pApInfo = &psapriv->apinfo;
	BssData_t *pBssData = MNULL;

	pBssData = &pApInfo->bssData;

	GenerateGTK_internal(psapriv, &pBssData->grpKeyData,
			     pBssData->GNonce, psapriv->curr_addr);
}

void
ReInitGTK(t_void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	apInfo_t *pApInfo = &psapriv->apinfo;
	BssData_t *pBssData;

	pBssData = &pApInfo->bssData;

	/*
	   Disabled for interop
	   Not all clients like this
	   pBssData->grpKeyData.KeyIndex = (pBssData->grpKeyData.KeyIndex & 3) + 1;
	 */

	ROM_InitGTK(psapriv, &pBssData->grpKeyData,
		    pBssData->GNonce, psapriv->curr_addr);

	keyApi_ApUpdateKeyMaterial(priv, MNULL, MTRUE);
}

void
KeyMgmtGrpRekeyCountUpdate(t_void *context)
{
	phostsa_private psapriv = (phostsa_private)context;
	apInfo_t *pApInfo = &psapriv->apinfo;
	hostsa_util_fns *putil_fns = &psapriv->util_fns;

	ENTER();

	if (psapriv->GrpRekeyTimerIsSet &&
	    pApInfo->bssData.grpRekeyCntRemaining) {
		//Periodic group rekey is configured.
		pApInfo->bssData.grpRekeyCntRemaining--;
		if (!pApInfo->bssData.grpRekeyCntRemaining) {
			//Group rekey timeout hit.
			pApInfo->bssData.grpRekeyCntRemaining
				= pApInfo->bssData.grpRekeyCntConfigured;

			ReInitGTK((t_void *)psapriv);

			KeyMgmtSendGrpKeyMsgToAllSta(psapriv);
		}

	/**start Group rekey timer*/
		putil_fns->moal_start_timer(putil_fns->pmoal_handle,
					    psapriv->GrpRekeytimer,
					    MFALSE, MRVDRV_TIMER_60S);
	}

	LEAVE();
}

void
KeyMgmtInit(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	apInfo_t *pApInfo = &psapriv->apinfo;
	UINT8 *pPassPhrase;
	UINT8 *pPskValue;
	UINT8 passPhraseLen;

	pPassPhrase = pApInfo->bssConfig.RsnConfig.PSKPassPhrase;
	pPskValue = pApInfo->bssConfig.RsnConfig.PSKValue;
	passPhraseLen = pApInfo->bssConfig.RsnConfig.PSKPassPhraseLen;

	ROM_InitGTK(psapriv, &pApInfo->bssData.grpKeyData,
		    pApInfo->bssData.GNonce, psapriv->curr_addr);

	if (pApInfo->bssData.updatePassPhrase == MTRUE) {
		pmkCacheGeneratePSK(priv, pApInfo->bssConfig.SsId,
				    pApInfo->bssConfig.SsIdLen,
				    (char *)pPassPhrase, pPskValue);

		pApInfo->bssData.updatePassPhrase = MFALSE;
	}
}

void
KeyMgmtHskTimeout(t_void *context)
{
	cm_Connection *connPtr = (cm_Connection *)context;
	phostsa_private priv = (phostsa_private)connPtr->priv;
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	apRsnConfig_t *pRsnConfig;
	IEEEtypes_ReasonCode_t reason;
	BOOLEAN maxRetriesDone = MFALSE;
	apInfo_t *pApInfo = &priv->apinfo;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	pRsnConfig = &pApInfo->bssConfig.RsnConfig;

	connPtr->timer_is_set = 0;
	/* Assume when this function gets called pKeyMgmtInfo->keyMgmtState
	 ** will not be in HSK_NOT_STARTED or HSK_END
	 */
	if (pKeyMgmtInfo->rom.keyMgmtState <= WAITING_4_MSG4) {
		if (pKeyMgmtInfo->numHskTries >= pRsnConfig->MaxPwsHskRetries) {
			maxRetriesDone = MTRUE;
			reason = IEEEtypes_REASON_4WAY_HANDSHK_TIMEOUT;
		}
	} else {
		if (pKeyMgmtInfo->numHskTries >= pRsnConfig->MaxGrpHskRetries) {
			maxRetriesDone = MTRUE;
			reason = IEEEtypes_REASON_GRP_KEY_UPD_TIMEOUT;
		}
	}

	if (maxRetriesDone) {
		// Some STAs do not respond to PWK Msg1 if the EAPOL Proto Version is 1
		// in 802.1X header, hence switch to v2 after all attempts with v1 fail
		// for PWK Msg1. Set the HskTimeoutCtn to 1 to get the same "retries"
		// as with v1.
		if (((WAITING_4_MSG2 == pKeyMgmtInfo->rom.keyMgmtState)
		     || (MSG1_PENDING == pKeyMgmtInfo->rom.keyMgmtState))
		    && (EAPOL_PROTOCOL_V1 == pKeyMgmtInfo->EAPOLProtoVersion)) {
			pKeyMgmtInfo->numHskTries = 1;
			pKeyMgmtInfo->EAPOLProtoVersion = EAPOL_PROTOCOL_V2;
			GenerateApEapolMsg(priv, connPtr,
					   pKeyMgmtInfo->rom.keyMgmtState);
		} else {
			pm_fns->hostsa_SendDeauth(priv->pmlan_private,
						  connPtr->mac_addr,
						  (t_u16)reason);
			pKeyMgmtInfo->rom.keyMgmtState = HSK_END;
		}
	} else {
		GenerateApEapolMsg(priv, connPtr,
				   pKeyMgmtInfo->rom.keyMgmtState);
	}

	return;
}

void
KeyMgmtStartHskTimer(void *context)
{
	cm_Connection *connPtr = (cm_Connection *)context;
	phostsa_private psapriv = (phostsa_private)(connPtr->priv);
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	apInfo_t *pApInfo = MNULL;
	UINT32 timeoutInms;
	apRsnConfig_t *pRsnConfig;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	pApInfo = &psapriv->apinfo;
	pRsnConfig = &pApInfo->bssConfig.RsnConfig;
	if ((connPtr->staData.keyMgmtInfo.rom.keyMgmtState >= MSG1_PENDING)
	    && (connPtr->staData.keyMgmtInfo.rom.keyMgmtState <=
		WAITING_4_MSG4)) {
		timeoutInms = pRsnConfig->PwsHskTimeOut;
	} else if ((connPtr->staData.keyMgmtInfo.rom.keyMgmtState >=
		    GRPMSG1_PENDING)
		   && (connPtr->staData.keyMgmtInfo.rom.keyMgmtState <=
		       WAITING_4_GRP_REKEY_MSG2)) {
		timeoutInms = pRsnConfig->GrpHskTimeOut;
	} else {
		//EAPOL HSK is not in progress. No need to start HSK timer
		return;
	}

	// Retry happen, increase timeout to at least 1 sec
	if (connPtr->staData.keyMgmtInfo.numHskTries > 0) {
		timeoutInms = MAX(AP_RETRY_EAPOL_HSK_TIMEOUT, timeoutInms);
	}

	/* if STA is in PS1 then we are using max(STA_PS_EAPOL_HSK_TIMEOUT,
	 * HSKtimeout)for timeout instead of configured timeout value
	 */
	/* if(PWR_MODE_PWR_SAVE == connPtr->staData.pwrSaveInfo.mode)
	   {
	   timeoutInms = MAX(STA_PS_EAPOL_HSK_TIMEOUT, timeoutInms);
	   }
	 */
	util_fns->moal_start_timer(util_fns->pmoal_handle,
				   connPtr->HskTimer, MFALSE, timeoutInms);
	connPtr->timer_is_set = 1;
}

void
KeyMgmtStopHskTimer(t_void *pconnPtr)
{
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;
	phostsa_private priv = (phostsa_private)connPtr->priv;
	hostsa_util_fns *util_fns = &priv->util_fns;

	util_fns->moal_stop_timer(util_fns->pmoal_handle, connPtr->HskTimer);

	connPtr->timer_is_set = 0;
}

void
PrepDefaultEapolMsg(phostsa_private priv, cm_Connection *connPtr,
		    EAPOL_KeyMsg_Tx_t **pTxEapolPtr, pmlan_buffer pmbuf)
{
	hostsa_util_fns *util_fns = &priv->util_fns;
	apInfo_t *pApInfo = &priv->apinfo;
	EAPOL_KeyMsg_Tx_t *tx_eapol_ptr;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	UINT8 intf_hr_len =
		pm_fns->Hostsa_get_intf_hr_len(pm_fns->pmlan_private);
#define UAP_EAPOL_PRIORITY 7	/* Voice */

	ENTER();

	pmbuf->priority = UAP_EAPOL_PRIORITY;
	pmbuf->buf_type = MLAN_BUF_TYPE_DATA;
	pmbuf->data_offset = (sizeof(UapTxPD) + intf_hr_len + DMA_ALIGNMENT);
	tx_eapol_ptr =
		(EAPOL_KeyMsg_Tx_t *)((UINT8 *)pmbuf->pbuf +
				      pmbuf->data_offset);
	memset(util_fns, (UINT8 *)tx_eapol_ptr, 0x00,
	       sizeof(EAPOL_KeyMsg_Tx_t));

	formEAPOLEthHdr(priv, tx_eapol_ptr, connPtr->mac_addr, priv->curr_addr);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;

	SetEAPOLKeyDescTypeVersion(tx_eapol_ptr,
				   pKeyMgmtInfo->rom.staSecType.wpa2,
				   MFALSE,
				   (pKeyMgmtInfo->rom.staUcstCipher.ccmp ||
				    pApInfo->bssConfig.RsnConfig.mcstCipher.
				    ccmp));

	*pTxEapolPtr = tx_eapol_ptr;

	LEAVE();
}

Status_e
GeneratePWKMsg1(hostsa_private *priv, cm_Connection *connPtr)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_Tx_t *tx_eapol_ptr;
	UINT16 frameLen = 0, packet_len = 0;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	UINT32 replay_cnt[2];
	eapolHskData_t *pHskData;
	pmlan_buffer pmbuf = MNULL;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	pmbuf = pm_fns->hostsa_alloc_mlan_buffer(priv->pmlan_adapter,
						 MLAN_TX_DATA_BUF_SIZE_2K, 0,
						 MOAL_MALLOC_BUFFER);
	if (pmbuf == NULL) {
		PRINTM(MERROR, "allocate buffer fail for eapol \n");
		return FAIL;
	}

	PrepDefaultEapolMsg(priv, connPtr, &tx_eapol_ptr, pmbuf);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	pHskData = &connPtr->hskData;

	incrementReplayCounter(pKeyMgmtInfo);
	replay_cnt[0] = pKeyMgmtInfo->counterHi;
	replay_cnt[1] = pKeyMgmtInfo->counterLo;

	supplicantGenerateRand(priv, pHskData->ANonce, NONCE_SIZE);

	PopulateKeyMsg(priv, tx_eapol_ptr,
		       &pKeyMgmtInfo->rom.staUcstCipher,
		       PAIRWISE_KEY_MSG, replay_cnt, pHskData->ANonce);

	frameLen = EAPOL_KeyMsg_Len - sizeof(Hdr_8021x_t)
		- sizeof(tx_eapol_ptr->keyMsg.key_data)
		+ tx_eapol_ptr->keyMsg.key_material_len;	//key_mtr_len is 0 here

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);

	tx_eapol_ptr->keyMsg.hdr_8021x.protocol_ver
		= pKeyMgmtInfo->EAPOLProtoVersion;
	tx_eapol_ptr->keyMsg.hdr_8021x.pckt_type
		= IEEE_8021X_PACKET_TYPE_EAPOL_KEY;
	tx_eapol_ptr->keyMsg.hdr_8021x.pckt_body_len = htons(frameLen);

	UpdateEAPOLWcbLenAndTransmit(priv, pmbuf, packet_len);
	return SUCCESS;
}

// returns 0 on success, or error code
Status_e
ProcessPWKMsg2(hostsa_private *priv,
	       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len)
{
	EAPOL_KeyMsg_t *rx_eapol_ptr;
	UINT8 *PMK;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	BssConfig_t *pBssConfig = NULL;
	IEPointers_t iePointers;
	UINT32 ieLen;
	UINT8 *pIe = NULL;
	apInfo_t *pApInfo = &priv->apinfo;
	Cipher_t wpaUcastCipher;
	Cipher_t wpa2UcastCipher;
	eapolHskData_t *pHskData = &connPtr->hskData;

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	rx_eapol_ptr = (EAPOL_KeyMsg_t *)(pbuf + ETHII_HEADER_LEN);

	pBssConfig = &pApInfo->bssConfig;
	// compare the RSN IE from assoc req to current
	pIe = (UINT8 *)rx_eapol_ptr->key_data;
	ieLen = pIe[1] + sizeof(IEEEtypes_InfoElementHdr_t);
	GetIEPointers((void *)priv, pIe, ieLen, &iePointers);
	wpaUcastCipher = pBssConfig->RsnConfig.wpaUcstCipher;
	wpa2UcastCipher = pBssConfig->RsnConfig.wpa2UcstCipher;
	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	if (assocSrvAp_checkRsnWpa(connPtr, &pKeyMgmtInfo->rom,
				   wpaUcastCipher,
				   wpa2UcastCipher,
				   pBssConfig->RsnConfig.mcstCipher,
				   pBssConfig->RsnConfig.AuthKey,
				   &pKeyMgmtInfo->rom.staSecType,
				   iePointers.pRsn,
				   iePointers.pWpa, TRUE) == FAIL) {
		handleFailedHSK(priv, connPtr, IEEEtypes_REASON_IE_4WAY_DIFF);
		return FAIL;
	}

	PMK = pApInfo->bssConfig.RsnConfig.PSKValue;

	KeyMgmtAp_DerivePTK(priv, PMK,
			    connPtr->mac_addr,
			    priv->curr_addr,
			    pHskData->ANonce,
			    rx_eapol_ptr->key_nonce,
			    pKeyMgmtInfo->EAPOL_MIC_Key,
			    pKeyMgmtInfo->EAPOL_Encr_Key,
			    &pHskData->pwsKeyData, MFALSE);

	if (!IsEAPOL_MICValid(priv, rx_eapol_ptr, pKeyMgmtInfo->EAPOL_MIC_Key)) {
		return FAIL;
	}

	KeyMgmtStopHskTimer(connPtr);
	connPtr->staData.keyMgmtInfo.numHskTries = 0;

	return GenerateApEapolMsg(priv, connPtr, MSG3_PENDING);
}

Status_e
GeneratePWKMsg3(hostsa_private *priv, cm_Connection *connPtr)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_Tx_t *tx_eapol_ptr;
	UINT16 frameLen = 0, packet_len = 0;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	apInfo_t *pApInfo = &priv->apinfo;
	BssConfig_t *pBssConfig = MNULL;
	UINT32 replay_cnt[2];
	eapolHskData_t *pHskData;
	pmlan_buffer pmbuf = MNULL;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	pmbuf = pm_fns->hostsa_alloc_mlan_buffer(priv->pmlan_adapter,
						 MLAN_TX_DATA_BUF_SIZE_2K, 0,
						 MOAL_MALLOC_BUFFER);
	if (pmbuf == NULL) {
		PRINTM(MERROR, "allocate buffer fail for eapol \n");
		return FAIL;
	}

	PrepDefaultEapolMsg(priv, connPtr, &tx_eapol_ptr, pmbuf);

	pBssConfig = &pApInfo->bssConfig;

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	pHskData = &connPtr->hskData;

	incrementReplayCounter(pKeyMgmtInfo);

	replay_cnt[0] = pKeyMgmtInfo->counterHi;
	replay_cnt[1] = pKeyMgmtInfo->counterLo;

	PopulateKeyMsg(priv, tx_eapol_ptr,
		       &pKeyMgmtInfo->rom.staUcstCipher,
		       ((PAIRWISE_KEY_MSG | SECURE_HANDSHAKE_FLAG) |
			((pKeyMgmtInfo->rom.staSecType.
			  wpa2) ? WPA2_HANDSHAKE : 0)), replay_cnt,
		       pHskData->ANonce);

	/*if (pKeyMgmtInfo->staSecType.wpa2)
	   {
	   // Netgear WAG511 and USB55 cards don't like this field set to
	   // anything other than zero. Hence hard code this value to zero
	   // in all outbound EAPOL frames...
	   // The client is now vulnerable to replay attacks from the point
	   // it receives EAP-message3 till reception of first management
	   // frame from uAP.

	   tx_eapol_ptr->keyMsg.key_RSC[0] =
	   pApInfo->bssConfig.grpKeyData.TxIV16 & 0x00FF;
	   tx_eapol_ptr->keyMsg.key_RSC[1] =
	   (pApInfo->bssConfig.grpKeyData.TxIV16 >> 8) & 0x00FF;
	   memcpy((void*)(tx_eapol_ptr->keyMsg.key_RSC + 2),
	   &pApInfo->bssData.grpKeyData.TxIV32, 4);
	   } */
/*
    pBcnFrame = (dot11MgtFrame_t *)BML_DATA_PTR(connPtr->pBcnBufferDesc);
    if (pKeyMgmtInfo->rom.staSecType.wpa)
    {
        pWpaIE = syncSrv_ParseAttrib(pBcnFrame,
                                     ELEM_ID_VENDOR_SPECIFIC,
                                     IEEE_MSG_BEACON,
                                     (UINT8 *)wpa_oui01,
                                     sizeof(wpa_oui01));
    }
    else if (pKeyMgmtInfo->rom.staSecType.wpa2)
    {
        pWpa2IE = syncSrv_ParseAttrib(pBcnFrame,
                                      ELEM_ID_RSN,
                                      IEEE_MSG_BEACON,
                                      NULL,
                                      0);
    }*/
	if (!KeyData_UpdateKeyMaterial(priv, tx_eapol_ptr,
				       &pKeyMgmtInfo->rom.staSecType,
				       pBssConfig->wpa_ie,
				       pBssConfig->rsn_ie)) {
		/* We have WPA/WPA2 enabled but no corresponding IE */
		pm_fns->hostsa_free_mlan_buffer(pm_fns->pmlan_adapter, pmbuf);
		return FAIL;
	}

	if (pKeyMgmtInfo->rom.staSecType.wpa2) {	// WPA2
		prepareKDE(priv, tx_eapol_ptr,
			   &pApInfo->bssData.grpKeyData,
			   &pApInfo->bssConfig.RsnConfig.mcstCipher);

		if (!Encrypt_keyData((t_void *)priv, tx_eapol_ptr,
				     pKeyMgmtInfo->EAPOL_Encr_Key,
				     &pKeyMgmtInfo->rom.staUcstCipher))
		{
			pm_fns->hostsa_free_mlan_buffer(pm_fns->pmlan_adapter,
							pmbuf);
			return FAIL;
		}
	}

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(priv,
						     tx_eapol_ptr,
						     pKeyMgmtInfo->
						     EAPOL_MIC_Key,
						     pKeyMgmtInfo->
						     EAPOLProtoVersion,
						     tx_eapol_ptr->keyMsg.
						     key_info.
						     KeyDescriptorVersion);

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);
	UpdateEAPOLWcbLenAndTransmit(priv, pmbuf, packet_len);
	return SUCCESS;
}

Status_e
ProcessPWKMsg4(hostsa_private *priv,
	       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_t *rx_eapol_ptr;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	eapolHskData_t *pHskData = &connPtr->hskData;

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;
	rx_eapol_ptr = (EAPOL_KeyMsg_t *)(pbuf + ETHII_HEADER_LEN);
	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	if (!IsEAPOL_MICValid(priv, rx_eapol_ptr, pKeyMgmtInfo->EAPOL_MIC_Key)) {
		return FAIL;
	}
	pHskData->pwsKeyData.TxIV16 = 0x0001;
	pHskData->pwsKeyData.TxIV32 = 0;

	if (keyApi_ApUpdateKeyMaterial(priv, connPtr, FALSE)) {
		handleFailedHSK(priv, connPtr, IEEEtypes_REASON_UNSPEC);
		return FAIL;
	}

	KeyMgmtStopHskTimer(connPtr);
	connPtr->staData.keyMgmtInfo.numHskTries = 0;
	if (pKeyMgmtInfo->rom.staSecType.wpa2) {
		pm_fns->Hostsa_sendEventRsnConnect(priv->pmlan_private,
						   connPtr->mac_addr);
		pKeyMgmtInfo->rom.keyMgmtState = HSK_END;
	} else {
		return GenerateApEapolMsg(priv, connPtr, GRPMSG1_PENDING);
	}

	return SUCCESS;
}

Status_e
GenerateGrpMsg1(hostsa_private *priv, cm_Connection *connPtr)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_Tx_t *tx_eapol_ptr;
	UINT16 frameLen = 0, packet_len = 0;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;
	apInfo_t *pApInfo = &priv->apinfo;
	UINT32 replay_cnt[2];
	pmlan_buffer pmbuf = MNULL;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	pmbuf = pm_fns->hostsa_alloc_mlan_buffer(priv->pmlan_adapter,
						 MLAN_TX_DATA_BUF_SIZE_2K, 0,
						 MOAL_MALLOC_BUFFER);
	if (pmbuf == NULL) {
		PRINTM(MERROR, "allocate buffer fail for eapol \n");
		return FAIL;
	}

	PrepDefaultEapolMsg(priv, connPtr, &tx_eapol_ptr, pmbuf);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;

	incrementReplayCounter(pKeyMgmtInfo);

	replay_cnt[0] = pKeyMgmtInfo->counterHi;
	replay_cnt[1] = pKeyMgmtInfo->counterLo;

	PopulateKeyMsg(priv, tx_eapol_ptr,
		       &pApInfo->bssConfig.RsnConfig.mcstCipher,
		       (SECURE_HANDSHAKE_FLAG |
			((pKeyMgmtInfo->rom.staSecType.
			  wpa2) ? WPA2_HANDSHAKE : 0)), replay_cnt,
		       pApInfo->bssData.GNonce);

	KeyData_AddKey(priv,
		       tx_eapol_ptr,
		       &pKeyMgmtInfo->rom.staSecType,
		       &pApInfo->bssData.grpKeyData,
		       &pApInfo->bssConfig.RsnConfig.mcstCipher);
	if (!Encrypt_keyData(priv, tx_eapol_ptr,
			     pKeyMgmtInfo->EAPOL_Encr_Key,
			     &pKeyMgmtInfo->rom.staUcstCipher))
	{
		// nothing here.
	}

	frameLen = KeyMgmtSta_PopulateEAPOLLengthMic(priv,
						     tx_eapol_ptr,
						     pKeyMgmtInfo->
						     EAPOL_MIC_Key,
						     pKeyMgmtInfo->
						     EAPOLProtoVersion,
						     tx_eapol_ptr->keyMsg.
						     key_info.
						     KeyDescriptorVersion);

	packet_len = frameLen + sizeof(Hdr_8021x_t) + sizeof(ether_hdr_t);

	UpdateEAPOLWcbLenAndTransmit(priv, pmbuf, packet_len);
	return SUCCESS;
}

Status_e
ProcessGrpMsg2(hostsa_private *priv,
	       cm_Connection *connPtr, t_u8 *pbuf, t_u32 len)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_t *rx_eapol_ptr;
	apKeyMgmtInfoSta_t *pKeyMgmtInfo;

	PRINTM(MMSG, "ENTER: %s\n", __FUNCTION__);

	rx_eapol_ptr = (EAPOL_KeyMsg_t *)(pbuf + ETHII_HEADER_LEN);

	pKeyMgmtInfo = &connPtr->staData.keyMgmtInfo;

	if (!IsEAPOL_MICValid(priv, rx_eapol_ptr, pKeyMgmtInfo->EAPOL_MIC_Key)) {
		handleFailedHSK(priv, connPtr, IEEEtypes_REASON_IE_4WAY_DIFF);
		return FAIL;
	}
	KeyMgmtStopHskTimer(connPtr);
	connPtr->staData.keyMgmtInfo.numHskTries = 0;

	if (WAITING_4_GRPMSG2 == pKeyMgmtInfo->rom.keyMgmtState) {
		/* sendEventRsnConnect(connPtr, pKeyMgmtInfo); */
		pm_fns->Hostsa_sendEventRsnConnect(priv->pmlan_private,
						   connPtr->mac_addr);
	}

	pKeyMgmtInfo->rom.keyMgmtState = HSK_END;

	return SUCCESS;
}

Status_e
GenerateApEapolMsg(hostsa_private *priv,
		   t_void *pconnPtr, keyMgmtState_e msgState)
{
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;
	Status_e status;
	// OSASysContext prevContext;

	if (connPtr->timer_is_set) {
		KeyMgmtStopHskTimer((t_void *)connPtr);
	}
	/* If msgState is any waiting_** state,
	 ** it will decrease to corresponding **_pending state.
	 */
	/* Note: it will reduce the if checks
	 */
	if ((msgState & 0x1) == 0) {
		msgState--;
	}
	connPtr->staData.keyMgmtInfo.rom.keyMgmtState = msgState;

	if (msgState == MSG1_PENDING) {
		status = GeneratePWKMsg1(priv, connPtr);
	} else if (msgState == MSG3_PENDING) {
		status = GeneratePWKMsg3(priv, connPtr);
	} else if ((msgState == GRPMSG1_PENDING)
		   || (msgState == GRP_REKEY_MSG1_PENDING)) {
		status = GenerateGrpMsg1(priv, connPtr);
	} else {
		//This should not happen
		return FAIL;
	}
	if (SUCCESS == status) {
		connPtr->staData.keyMgmtInfo.rom.keyMgmtState++;
	}

	if (SUCCESS == status) {
		connPtr->staData.keyMgmtInfo.numHskTries++;
		/* we are starting the timer irrespective of whether the msg generation is
		   sucessful or not. This is because, if the msg generation fails because
		   of buffer unavailabilty then we can re-try the msg after the timeout
		   period. */
		if (!connPtr->timer_is_set)
			KeyMgmtStartHskTimer(connPtr);
	}

	return status;
}

//#endif // AUTHENTICATOR

void
ApMicErrTimerExpCb(t_void *context)
{
	cm_Connection *connPtr = (cm_Connection *)context;
	phostsa_private priv;
	// UINT32 int_save;
	apInfo_t *pApInfo;

	if (connPtr == NULL) {
		//no AP connection. Do nothing, just return
		return;
	}
	priv = (phostsa_private)connPtr->priv;
	if (!priv)
		return;
	pApInfo = &priv->apinfo;

	switch (connPtr->staData.apMicError.status) {
	case FIRST_MIC_FAIL_IN_60_SEC:
		connPtr->staData.apMicError.status = NO_MIC_FAILURE;
		break;
	case SECOND_MIC_FAIL_IN_60_SEC:
		if ((pApInfo->bssConfig.RsnConfig.mcstCipher.tkip) &&
		    IsAuthenticatorEnabled(priv)) {
			// re-enable the group re-key timer
			pApInfo->bssData.grpRekeyCntRemaining
				= pApInfo->bssData.grpRekeyCntConfigured;
		}
		connPtr->staData.apMicError.status = NO_MIC_FAILURE;
		connPtr->staData.apMicError.disableStaAsso = 0;

		break;
	default:
		break;
	}
}

void
ApMicCounterMeasureInvoke(t_void *pconnPtr)
{
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;
	phostsa_private priv = (phostsa_private)connPtr->priv;
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	hostsa_util_fns *util_fns = &priv->util_fns;
	apInfo_t *pApInfo = &priv->apinfo;

	if (connPtr->staData.apMicError.MICCounterMeasureEnabled) {
		switch (connPtr->staData.apMicError.status) {
		case NO_MIC_FAILURE:
			util_fns->moal_start_timer(util_fns->pmoal_handle,
						   connPtr->staData.apMicTimer,
						   MFALSE, MRVDRV_TIMER_60S);
			connPtr->staData.apMicError.status =
				FIRST_MIC_FAIL_IN_60_SEC;
			break;

		case FIRST_MIC_FAIL_IN_60_SEC:
			connPtr->staData.apMicError.disableStaAsso = 1;
			connPtr->staData.apMicError.status =
				SECOND_MIC_FAIL_IN_60_SEC;
			//start timer for 60 seconds
			util_fns->moal_stop_timer(util_fns->pmoal_handle,
						  connPtr->staData.apMicTimer);
			util_fns->moal_start_timer(util_fns->pmoal_handle,
						   connPtr->staData.apMicTimer,
						   MFALSE, MRVDRV_TIMER_60S);

			/*  smeAPStateMgr_sendSmeMsg(connPtr, MlmeApBcastDisassoc); */
			pm_fns->Hostsa_DisAssocAllSta(priv->pmlan_private,
						      IEEEtypes_REASON_MIC_FAILURE);
			// if current GTK is tkip
			if ((pApInfo->bssConfig.RsnConfig.mcstCipher.tkip) &&
			    IsAuthenticatorEnabled(priv)) {
				//Disable periodic group rekey and re-init GTK.
				priv->GrpRekeyTimerIsSet = MFALSE;
				pApInfo->bssData.grpRekeyCntRemaining = 0;
				ReInitGTK(priv);
			}
			break;

		default:
			break;
		}
	}
	return;
}
