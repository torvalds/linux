/** @file Authenticator_api.c
 *
 *  @brief This file defines the main APIs for authenticator.
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
#include "keyMgmtAp.h"

/*********************
	Local Variables
 *********************/

/*********************
	Global Variables
 *********************/

/*********************
	Local Functions
 *********************/

/*********************
	Global Functions
 *********************/
#ifdef DRV_EMBEDDED_SUPPLICANT
extern void allocSupplicantData(void *phostsa_priv);
extern void freeSupplicantData(void *phostsa_priv);
extern mlan_status initSupplicantTimer(void *priv);
extern void keyMgmtSta_RomInit(void);
extern void freeSupplicantTimer(void *priv);
#endif

/*********************
	Utility Handler
 *********************/
#ifdef DRV_EMBEDDED_AUTHENTICATOR
static UINT32
util_CountBits(UINT32 val)
{
	UINT32 count = 0;

	for (count = 0; val; count++) {
		val &= (val - 1);
	}
	return count;
}

static void
initMicErrorParams(UINT32 wpa, MIC_Error_t *pApMicError)
{
	if (wpa) {
		pApMicError->MICCounterMeasureEnabled = 1;
		pApMicError->disableStaAsso = 0;
		pApMicError->status = NO_MIC_FAILURE;
	}
}

/**
 *  @brief whether authenticator is enabled or not
 *
 *  @param priv   A void pointer to phostsa private struct
 *
 *  @return
 */
t_u8
IsAuthenticatorEnabled(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	apInfo_t *pApInfo = NULL;
	apRsnConfig_t *pRsnConfig = NULL;
	t_u8 ret = 0;

	ENTER();

	if (!psapriv) {
		LEAVE();
		return ret;
	}

	pApInfo = &psapriv->apinfo;
	if (pApInfo == NULL) {
		LEAVE();
		return ret;
	}

	pRsnConfig = &pApInfo->bssConfig.RsnConfig;

	// If Passphrase lengh is nonozero then
	// authenticator in the driver is to be used,
	// otherwise authenticator in the host is not to be used.
	if (pRsnConfig->PSKPassPhraseLen != 0) {
		ret = 1;
	}

	LEAVE();
	return ret;
}

/**
 *  @brief BSS configure initialized
 *
 *  @param priv   A void pointer to phostsa private struct
 *
 *  @return
 */
void
AuthenitcatorInitBssConfig(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = MNULL;
	apInfo_t *pApInfo = MNULL;
	BssConfig_t *pBssConfig = MNULL;
	apRsnConfig_t *pRsnConfig = MNULL;

	ENTER();

	if (!psapriv) {
		LEAVE();
		return;
	}
	util_fns = &psapriv->util_fns;
	pApInfo = &psapriv->apinfo;
	pBssConfig = &pApInfo->bssConfig;
	pRsnConfig = &pBssConfig->RsnConfig;

	memset(util_fns, pBssConfig, 0x00, sizeof(BssConfig_t));
    /**default ap ssid*/
	pBssConfig->SsIdLen = wlan_strlen(AP_DEFAULT_SSID);
	memset(util_fns, pBssConfig->SsId, 0x00, IEEEtypes_SSID_SIZE);
	memcpy(util_fns, pBssConfig->SsId, AP_DEFAULT_SSID,
	       IEEEtypes_SSID_SIZE);
    /**default retry times ans timeout*/
	pRsnConfig->PwsHskTimeOut = PWS_HSK_TIMEOUT;
	pRsnConfig->MaxPwsHskRetries = PWS_HSK_RETRIES;
	pRsnConfig->GrpHskTimeOut = GRP_HSK_TIMEOUT;
	pRsnConfig->MaxGrpHskRetries = GRP_HSK_RETRIES;
    /**Group key rekey time*/
	pRsnConfig->GrpReKeyTime = GRP_REKEY_TIME;

	LEAVE();
}

/**
 *  @brief get bss config
 *
 *  @param priv   A void pointer to phostsa private struct
 *  @param pbss_config a pointer to mlan_uap_bss_param
 *  @param SetConfigToMlan  1 set releated config to mlan , 0 get related config from mlan
 *
 *  @return
 */
void
AuthenticatorGetBssConfig(hostsa_private *psapriv, t_u8 *pbss_config,
			  t_u8 SetConfigToMlan)
{
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	apInfo_t *pApInfo = &psapriv->apinfo;
	mlan_uap_bss_param *bss_config = (mlan_uap_bss_param *)pbss_config;
	BssConfig_t *pBssConfig = MNULL;
	apRsnConfig_t *pRsnConfig = MNULL;
	t_u8 zero_mac[] = { 0, 0, 0, 0, 0, 0 };

	ENTER();

	pBssConfig = &pApInfo->bssConfig;
	pRsnConfig = &pBssConfig->RsnConfig;

    /**set bss config to mlan*/
	if (SetConfigToMlan) {
		if ((pRsnConfig->PwsHskTimeOut != 0) &&
		    (pRsnConfig->PwsHskTimeOut < (MAX_VALID_DWORD))) {
			bss_config->pairwise_update_timeout =
				pRsnConfig->PwsHskTimeOut;
		}

		if ((pRsnConfig->MaxPwsHskRetries != 0) &&
		    (pRsnConfig->MaxPwsHskRetries < (MAX_VALID_DWORD)))
			bss_config->pwk_retries = pRsnConfig->MaxPwsHskRetries;

		if ((pRsnConfig->GrpHskTimeOut != 0) &&
		    (pRsnConfig->GrpHskTimeOut < (MAX_VALID_DWORD)))
			bss_config->groupwise_update_timeout =
				pRsnConfig->GrpHskTimeOut;

		if ((pRsnConfig->MaxGrpHskRetries != 0) &&
		    (pRsnConfig->MaxGrpHskRetries < (MAX_VALID_DWORD)))
			bss_config->gwk_retries = pRsnConfig->MaxGrpHskRetries;

		if (pRsnConfig->PSKPassPhraseLen != 0) {
			bss_config->wpa_cfg.length =
				pRsnConfig->PSKPassPhraseLen;
			memcpy(util_fns, bss_config->wpa_cfg.passphrase,
			       pRsnConfig->PSKPassPhrase,
			       pRsnConfig->PSKPassPhraseLen);
		}

		if ((pRsnConfig->GrpReKeyTime != 0) &&
		    (pRsnConfig->GrpReKeyTime < MAX_GRP_TIMER))
			bss_config->wpa_cfg.gk_rekey_time =
				pRsnConfig->GrpReKeyTime;

		LEAVE();
		return;
	}

	if (((pBssConfig->SsIdLen != bss_config->ssid.ssid_len) ||
	     (memcmp(util_fns, pBssConfig->SsId,
		     bss_config->ssid.ssid, bss_config->ssid.ssid_len) != 0)) &&
	    bss_config->ssid.ssid_len) {
		pApInfo->bssData.updatePassPhrase = 1;
		memcpy(util_fns, pBssConfig->SsId, bss_config->ssid.ssid,
		       bss_config->ssid.ssid_len);
		pBssConfig->SsIdLen = bss_config->ssid.ssid_len;
	}

	if (memcmp
	    (util_fns, zero_mac, &bss_config->mac_addr, MLAN_MAC_ADDR_LENGTH)) {
		memset(util_fns, psapriv->curr_addr, 0x00,
		       MLAN_MAC_ADDR_LENGTH);
		memcpy(util_fns, psapriv->curr_addr, &bss_config->mac_addr,
		       MLAN_MAC_ADDR_LENGTH);
	}

	if ((bss_config->max_sta_count != 0) &&
	    (bss_config->max_sta_count <= MAX_STA_COUNT))
		pBssConfig->MaxStaSupported = bss_config->max_sta_count;

	if ((bss_config->pairwise_update_timeout != 0) &&
	    (bss_config->pairwise_update_timeout < (MAX_VALID_DWORD))) {
		pRsnConfig->PwsHskTimeOut = bss_config->pairwise_update_timeout;
	}

	if ((bss_config->pwk_retries != 0) &&
	    (bss_config->pwk_retries < (MAX_VALID_DWORD)))
		pRsnConfig->MaxPwsHskRetries = bss_config->pwk_retries;

	if ((bss_config->groupwise_update_timeout != 0) &&
	    (bss_config->groupwise_update_timeout < (MAX_VALID_DWORD)))
		pRsnConfig->GrpHskTimeOut =
			bss_config->groupwise_update_timeout;

	if ((bss_config->gwk_retries != 0) &&
	    (bss_config->gwk_retries < (MAX_VALID_DWORD)))
		pRsnConfig->MaxGrpHskRetries = bss_config->gwk_retries;

	if ((bss_config->auth_mode <= MLAN_AUTH_MODE_SHARED) ||
	    (bss_config->auth_mode == MLAN_AUTH_MODE_AUTO))
		pBssConfig->AuthType = bss_config->auth_mode;

	memcpy(util_fns, &pBssConfig->SecType, &bss_config->protocol,
	       sizeof(pBssConfig->SecType));

	if ((bss_config->protocol & PROTOCOL_WPA) ||
	    (bss_config->protocol & PROTOCOL_WPA2) ||
	    (bss_config->protocol & PROTOCOL_EAP)) {
		pRsnConfig->AuthKey = bss_config->key_mgmt;
		pRsnConfig->AuthKeyCount = util_CountBits(pRsnConfig->AuthKey);
		memcpy(util_fns, (char *)&pRsnConfig->Akmp,
		       (char *)&bss_config->key_mgmt_operation,
		       sizeof(bss_config->key_mgmt_operation));

		if ((bss_config->wpa_cfg.
		     pairwise_cipher_wpa & VALID_CIPHER_BITMAP) &&
		    (bss_config->wpa_cfg.pairwise_cipher_wpa != 0xff)) {
			memset(util_fns, (t_u8 *)&pRsnConfig->wpaUcstCipher,
			       0x00, sizeof(Cipher_t));
			memcpy(util_fns, (t_u8 *)&pRsnConfig->wpaUcstCipher,
			       (t_u8 *)&bss_config->wpa_cfg.pairwise_cipher_wpa,
			       sizeof(Cipher_t));
			pRsnConfig->wpaUcstCipherCount =
				util_CountBits(bss_config->wpa_cfg.
					       pairwise_cipher_wpa);
		}

		if ((bss_config->wpa_cfg.
		     pairwise_cipher_wpa2 & VALID_CIPHER_BITMAP) &&
		    (bss_config->wpa_cfg.pairwise_cipher_wpa2 != 0xff)) {
			memset(util_fns, (t_u8 *)&pRsnConfig->wpa2UcstCipher,
			       0x00, sizeof(Cipher_t));
			memcpy(util_fns, (t_u8 *)&pRsnConfig->wpa2UcstCipher,
			       (t_u8 *)&bss_config->wpa_cfg.
			       pairwise_cipher_wpa2, sizeof(Cipher_t));
			pRsnConfig->wpa2UcstCipherCount =
				util_CountBits(bss_config->wpa_cfg.
					       pairwise_cipher_wpa2);
		}
		DBG_HEXDUMP(MCMD_D, " wpa2UcstCipher",
			    (t_u8 *)&pRsnConfig->wpa2UcstCipher,
			    sizeof(Cipher_t));
		DBG_HEXDUMP(MCMD_D, " wpaUcastCipher",
			    (t_u8 *)&pRsnConfig->wpaUcstCipher,
			    sizeof(Cipher_t));

		if (bss_config->wpa_cfg.group_cipher & VALID_CIPHER_BITMAP) {
			memset(util_fns, (t_u8 *)&pRsnConfig->mcstCipher, 0x00,
			       sizeof(Cipher_t));
			memcpy(util_fns, (t_u8 *)&pRsnConfig->mcstCipher,
			       (t_u8 *)&bss_config->wpa_cfg.group_cipher,
			       sizeof(Cipher_t));
			pRsnConfig->mcstCipherCount =
				util_CountBits(bss_config->wpa_cfg.
					       group_cipher);
		}

		if (bss_config->wpa_cfg.rsn_protection <= MTRUE)
			pRsnConfig->RSNReplayProtEn =
				bss_config->wpa_cfg.
				rsn_protection ? MTRUE : MFALSE;

	}

	if (((pRsnConfig->PSKPassPhraseLen != bss_config->wpa_cfg.length) ||
	     (memcmp(util_fns, pRsnConfig->PSKPassPhrase,
		     bss_config->wpa_cfg.passphrase,
		     bss_config->wpa_cfg.length) != 0)) &&
	    bss_config->wpa_cfg.length) {
		pApInfo->bssData.updatePassPhrase = 1;
		pRsnConfig->PSKPassPhraseLen = bss_config->wpa_cfg.length;
		memset(util_fns, pRsnConfig->PSKPassPhrase,
		       0x00, PSK_PASS_PHRASE_LEN_MAX);
		memcpy(util_fns, pRsnConfig->PSKPassPhrase,
		       bss_config->wpa_cfg.passphrase,
		       bss_config->wpa_cfg.length);
	}

	if ((bss_config->wpa_cfg.gk_rekey_time != 0) &&
	    (bss_config->wpa_cfg.gk_rekey_time < MAX_GRP_TIMER))
		pRsnConfig->GrpReKeyTime = bss_config->wpa_cfg.gk_rekey_time;

	LEAVE();
}

/**
 *  @brief get bss config for authenticator and append wpa/rsn ie to FW
 *
 *  @param priv                  A void pointer to phostsa private struct
 *  @param pbss_config           a pointer to mlan_uap_bss_param
 *  @param appendIE              1 append rsn/wpa ie to fw, 0  not append
 *  @param clearIE               1 clear rsn/wpa ie to fw, 0  not clear
 *  @param SetConfigToMlan       1 set releated config to mlan , 0 get related config from mlan
*
 *
 *  @return
 */
t_u8
AuthenticatorBssConfig(void *priv, t_u8 *pbss_config, t_u8 appendIE,
		       t_u8 clearIE, t_u8 SetConfigToMlan)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = MNULL;
	hostsa_mlan_fns *pm_fns = MNULL;
	apInfo_t *pApInfo = MNULL;
	BssConfig_t *pBssConfig = MNULL;
	apRsnConfig_t *pRsnConfig = MNULL;
	t_u16 ielen = 0;
	t_u8 ret = 0;

	ENTER();

	if (!psapriv) {
		LEAVE();
		return ret;
	}
	util_fns = &psapriv->util_fns;
	pm_fns = &psapriv->mlan_fns;
	pApInfo = &psapriv->apinfo;

	pBssConfig = &pApInfo->bssConfig;
	pRsnConfig = &pBssConfig->RsnConfig;

	if (pbss_config)
		AuthenticatorGetBssConfig(psapriv, pbss_config,
					  SetConfigToMlan);

	if (appendIE && pRsnConfig->PSKPassPhraseLen &&
	    (pBssConfig->SecType.wpa || pBssConfig->SecType.wpa2 ||
	     pBssConfig->SecType.wpaNone)) {
		PRINTM(MMSG, "authenticator set mgmt IE\n");
		if (pBssConfig->wpa_ielen) {
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->wpa_ie,
						   pBssConfig->wpa_ielen, 1);
			pBssConfig->wpa_ielen = 0;
			memset(util_fns, pBssConfig->wpa_ie, 0x00, MAX_IE_SIZE);
		}
	    /**construct wpa or RSN ie*/
		if (pBssConfig->SecType.wpa) {
			memset(util_fns, pBssConfig->wpa_ie, 0x00, MAX_IE_SIZE);
			ielen = keyMgmtAp_FormatWPARSN_IE(psapriv,
							  (IEEEtypes_InfoElementHdr_t
							   *)pBssConfig->wpa_ie,
							  0,
							  &pBssConfig->
							  RsnConfig.
							  wpaUcstCipher,
							  pBssConfig->RsnConfig.
							  wpaUcstCipherCount,
							  &pBssConfig->
							  RsnConfig.mcstCipher,
							  pBssConfig->RsnConfig.
							  AuthKey,
							  pBssConfig->RsnConfig.
							  AuthKeyCount);
			pBssConfig->wpa_ielen = ielen;
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->wpa_ie, ielen,
						   0);
		}

		if (pBssConfig->rsn_ielen) {
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->rsn_ie,
						   pBssConfig->rsn_ielen, 1);
			pBssConfig->rsn_ielen = 0;
			memset(util_fns, pBssConfig->rsn_ie, 0x00, MAX_IE_SIZE);
		}

		if (pBssConfig->SecType.wpa2) {
			memset(util_fns, pBssConfig->rsn_ie, 0x00, MAX_IE_SIZE);
			ielen = keyMgmtAp_FormatWPARSN_IE(psapriv,
							  (IEEEtypes_InfoElementHdr_t
							   *)pBssConfig->rsn_ie,
							  1,
							  &pBssConfig->
							  RsnConfig.
							  wpa2UcstCipher,
							  pBssConfig->RsnConfig.
							  wpa2UcstCipherCount,
							  &pBssConfig->
							  RsnConfig.mcstCipher,
							  pBssConfig->RsnConfig.
							  AuthKey,
							  pBssConfig->RsnConfig.
							  AuthKeyCount);
			pBssConfig->rsn_ielen = ielen;
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->rsn_ie, ielen,
						   0);
		}
	}

	if (clearIE) {
		PRINTM(MMSG, "authenticator clear mgmt IE\n");
		if (pBssConfig->wpa_ielen) {
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->wpa_ie,
						   pBssConfig->wpa_ielen, 1);
			pBssConfig->wpa_ielen = 0;
			memset(util_fns, pBssConfig->wpa_ie, 0x00, MAX_IE_SIZE);
		}
		if (pBssConfig->rsn_ielen) {
			pm_fns->Hostsa_set_mgmt_ie(psapriv->pmlan_private,
						   pBssConfig->rsn_ie,
						   pBssConfig->rsn_ielen, 1);
			pBssConfig->rsn_ielen = 0;
			memset(util_fns, pBssConfig->rsn_ie, 0x00, MAX_IE_SIZE);
		}
	}
	LEAVE();
	return ret;
}

/**
 *  @brief initialize key
 *
 *  @param priv   A void pointer to phostsa private struct
 *
 *  @return
 */
void
AuthenticatorKeyMgmtInit(void *priv, t_u8 *addr)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	apInfo_t *pApInfo = &psapriv->apinfo;
	BssConfig_t *pBssConfig = MNULL;

	ENTER();

	pBssConfig = &pApInfo->bssConfig;
    /**mac address */
	memcpy(util_fns, psapriv->curr_addr, addr, MLAN_MAC_ADDR_LENGTH);
    /**Reset Group Key*/
	memset(util_fns, &pBssConfig->pGtkKeyBuf, 0x00,
	       sizeof(pBssConfig->pGtkKeyBuf));
    /**key init */
	KeyMgmtInit(priv);
    /**Group key update time,*/
	if (pBssConfig->RsnConfig.GrpReKeyTime) {
		pApInfo->bssData.grpRekeyCntConfigured = pApInfo->bssConfig.RsnConfig.GrpReKeyTime / 60;
							       /**every 60s grpRekeyCntRemaining -- */

		pApInfo->bssData.grpRekeyCntRemaining
			= pApInfo->bssData.grpRekeyCntConfigured;
	}

	keyApi_ApUpdateKeyMaterial(priv, MNULL, MTRUE);

    /**start Group rekey timer*/
	if (pBssConfig->RsnConfig.GrpReKeyTime && !psapriv->GrpRekeyTimerIsSet) {
		util_fns->moal_start_timer(util_fns->pmoal_handle,
					   psapriv->GrpRekeytimer,
					   MFALSE, MRVDRV_TIMER_60S);
		psapriv->GrpRekeyTimerIsSet = MTRUE;
	}

	LEAVE();
}

/**
 *  @brief clear key
 *
 *  @param priv   A void pointer to phostsa private struct
 *
 *  @return
 */
void
AuthenticatorkeyClear(void *priv)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *putil_fns = &psapriv->util_fns;
	apInfo_t *pApInfo = &psapriv->apinfo;
	BssConfig_t *pBssConfig = &pApInfo->bssConfig;

	if (psapriv->GrpRekeyTimerIsSet) {
		psapriv->GrpRekeyTimerIsSet = MFALSE;
		putil_fns->moal_stop_timer(putil_fns->pmoal_handle,
					   psapriv->GrpRekeytimer);
	/**Reset Group Key*/
		memset(putil_fns, &pBssConfig->pGtkKeyBuf, 0x00,
		       sizeof(pBssConfig->pGtkKeyBuf));
	}
}

/**
 *  @brief process received eapol packet
 *
 *  @param psapriv   A void pointer to phostsa private struct
 *  @param pbuf      a pointer to packet buf
 *  @param len        buffer len
 *  @
 *  @return
 */
t_u8
AuthenticatorProcessEapolPacket(t_void *psapriv, t_u8 *pbuf, t_u32 len)
{
	phostsa_private priv = (phostsa_private)psapriv;
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	EAPOL_KeyMsg_t *rx_eapol_ptr;
	Status_e status = FAIL;
	cm_Connection *connPtr = MNULL;
	cm_Connection *pconnPtr = MNULL;
	t_u8 *sta_addr = pbuf + MLAN_MAC_ADDR_LENGTH;
	t_u8 ret = 0;
	t_u32 eapol_pkt_len, least_len;

	ENTER();

	rx_eapol_ptr = (EAPOL_KeyMsg_t *)(pbuf + ETHII_HEADER_LEN);

	if (rx_eapol_ptr->hdr_8021x.pckt_type !=
	    IEEE_8021X_PACKET_TYPE_EAPOL_KEY) {
		ret = 1;
		return ret;

	}

	eapol_pkt_len = len - ETHII_HEADER_LEN;
	least_len = sizeof(EAPOL_KeyMsg_t) - sizeof(rx_eapol_ptr->key_data);
	if (eapol_pkt_len < least_len) {
		PRINTM(MERROR,
		       "Invalid EAPOL Key Packet, received len: %u, least len: %u\n",
		       eapol_pkt_len, least_len);
		ret = 1;
		return ret;
	}

	pm_fns->Hostsa_get_station_entry(priv->pmlan_private, sta_addr,
					 (t_void *)&pconnPtr);
	connPtr = (cm_Connection *)pconnPtr;
	if (connPtr) {
		// rx_eapol_ptr = (EAPOL_KeyMsg_t *)(pbuf + ETHII_HEADER_LEN);

		if (rx_eapol_ptr->key_info.Error) {
			ApMicCounterMeasureInvoke((t_void *)connPtr);
			return ret;
		}

		if (!isValidReplayCount(priv, &connPtr->staData.keyMgmtInfo,
					(UINT8 *)&rx_eapol_ptr->replay_cnt[0]))
		{
			return ret;
		}

		if (connPtr->staData.keyMgmtInfo.rom.keyMgmtState ==
		    WAITING_4_MSG2) {
			status = ProcessPWKMsg2(priv, connPtr, pbuf, len);
		} else if (connPtr->staData.keyMgmtInfo.rom.keyMgmtState ==
			   WAITING_4_MSG4) {
			status = ProcessPWKMsg4(priv, connPtr, pbuf, len);
		} else if ((connPtr->staData.keyMgmtInfo.rom.keyMgmtState
			    == WAITING_4_GRPMSG2)
			   || (connPtr->staData.keyMgmtInfo.rom.keyMgmtState
			       == WAITING_4_GRP_REKEY_MSG2)) {
			status = ProcessGrpMsg2(priv, connPtr, pbuf, len);
		}
	}
	LEAVE();
	return ret;
}

/**
 *  @brief send eapol packet
 *
 *  @param psapriv        A void pointer to phostsa private struct
 *  @param pconnPtr      a pointer to connection
 *  @
 *  @return
 */
t_void
AuthenticatorSendEapolPacket(t_void *priv, t_void *pconnPtr)
{
	phostsa_private psapriv = (phostsa_private)priv;
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;
	apInfo_t *pApInfo = &psapriv->apinfo;

	ENTER();
    /**check whether wpa/rsn is used*/
	if (!connPtr->staData.RSNEnabled)
		return;

    /**init Mic error parameters*/
	initMicErrorParams(pApInfo->bssConfig.SecType.wpa,
			   &connPtr->staData.apMicError);
	connPtr->staData.keyMgmtInfo.rom.keyMgmtState = MSG1_PENDING;
	//If it is in **_pending state
	if (((connPtr->staData.keyMgmtInfo.rom.keyMgmtState) & 0x1) != 0) {
		GenerateApEapolMsg(psapriv, connPtr,
				   connPtr->staData.keyMgmtInfo.rom.
				   keyMgmtState);

	}

	LEAVE();
}

/**
 *  @brief get station security infor from (re)assocaite request
 *
 *  @param priv             A void pointer to phostsa private struct
 *  @param pconnPtr      a pointer to connection
 *  @param pIe              a pointer to associate request
 *  @param ieLen           len of ie
 *
 *  @return
 */
void
authenticator_get_sta_security_info(void *priv,
				    t_void *pconnPtr, t_u8 *pIe, t_u8 ieLen)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	cm_Connection *connPtr = (cm_Connection *)pconnPtr;
	IEPointers_t iePointers;
	IEEEtypes_StatusCode_t retcode;

	ENTER();

	ieLen = GetIEPointers((t_void *)priv, pIe, ieLen, &iePointers);

	assocSrvAp_CheckSecurity(connPtr, iePointers.pWps,
				 iePointers.pRsn, iePointers.pWpa,
				 iePointers.pWapi, &retcode);
	/* clean key */
	memset(util_fns, (t_u8 *)&connPtr->hskData, 0x00,
	       sizeof(eapolHskData_t));

	LEAVE();
}

/**
 *  @brief initialize client
 *
 *  @param priv             A void pointer to phostsa private struct
 *  @param pconnPtr      a pointer to pointer to connection
 *  @param mac            a pointer to mac address
 *
 *  @return
 */
void
authenticator_init_client(void *priv, void **ppconnPtr, t_u8 *mac)
{
	phostsa_private psapriv = (hostsa_private *)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	cm_Connection *connPtr = MNULL;
	mlan_status ret = MLAN_STATUS_SUCCESS;

	ENTER();

	ret = malloc(util_fns, sizeof(cm_Connection), (t_u8 **)&connPtr);
	if ((ret != MLAN_STATUS_SUCCESS) || (!connPtr)) {
		PRINTM(MERROR, "%s: could not allocate hostsa_private.\n",
		       __FUNCTION__);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	memset(util_fns, connPtr, 0x00, sizeof(cm_Connection));
	connPtr->priv = priv;
	memcpy(util_fns, connPtr->mac_addr, mac, MLAN_MAC_ADDR_LENGTH);

	util_fns->moal_init_timer(util_fns->pmoal_handle, &connPtr->HskTimer,
				  KeyMgmtHskTimeout, connPtr);
	util_fns->moal_init_timer(util_fns->pmoal_handle,
				  &connPtr->staData.apMicTimer,
				  ApMicErrTimerExpCb, connPtr);

done:
	if (ret == MLAN_STATUS_SUCCESS)
		*ppconnPtr = (void *)connPtr;
	else
		*ppconnPtr = MNULL;
	LEAVE();

}

/**
 *  @brief free client
 *
 *  @param priv             A void pointer to phostsa private struct
 *  @param pconnPtr      a pointer to connection
 *
 *  @return
 */
void
authenticator_free_client(void *priv, void *ppconnPtr)
{
	phostsa_private psapriv = (phostsa_private)priv;
	hostsa_util_fns *util_fns = &psapriv->util_fns;
	cm_Connection *connPtr = (cm_Connection *)ppconnPtr;

	ENTER();

	if (connPtr) {
		util_fns->moal_stop_timer(util_fns->pmoal_handle,
					  connPtr->HskTimer);
		util_fns->moal_stop_timer(util_fns->pmoal_handle,
					  connPtr->staData.apMicTimer);

		util_fns->moal_free_timer(util_fns->pmoal_handle,
					  connPtr->HskTimer);
		util_fns->moal_free_timer(util_fns->pmoal_handle,
					  connPtr->staData.apMicTimer);
		free(util_fns, (t_u8 *)connPtr);
	}
	LEAVE();
}
#endif

/**
 *  @brief Init hostsa data
 *
 *  @param pphostsa_priv   A pointer to pointer to a hostsa private data structure
 *  @param psa_util_fns      A pointer to hostsa utility functions table
 *  @param psa_mlan_fns   A pointer to MLAN APIs table
 *  @param addr                a pointer to address
 *
 *  @return             MLAN_STATUS_SUCCESS or MLAN_STATUS_FAILURE
 */
mlan_status
supplicant_authenticator_init(t_void **pphostsa_priv, t_void *psa_util_fns,
			      t_void *psa_mlan_fns, t_u8 *addr)
{
	hostsa_util_fns *putil_fns = (hostsa_util_fns *)psa_util_fns;
	hostsa_mlan_fns *pmlan_fns = (hostsa_mlan_fns *)psa_mlan_fns;
	mlan_status ret;
	hostsa_private *priv = MNULL;

	ENTER();

	ret = malloc(putil_fns, sizeof(hostsa_private), (t_u8 **)&priv);
	if ((ret != MLAN_STATUS_SUCCESS) || (!priv)) {
		PRINTM(MERROR, "%s: could not allocate hostsa_private.\n",
		       __FUNCTION__);
		ret = MLAN_STATUS_FAILURE;
		goto done;
	}

	memset(putil_fns, priv, 0x00, sizeof(hostsa_private));
	memset(putil_fns, &priv->apinfo, 0x00, sizeof(apInfo_t));

	memcpy(putil_fns, &priv->util_fns, putil_fns, sizeof(hostsa_util_fns));
	memcpy(putil_fns, &priv->mlan_fns, pmlan_fns, sizeof(hostsa_mlan_fns));

	priv->pmlan_adapter = pmlan_fns->pmlan_adapter;
	priv->pmlan_private = pmlan_fns->pmlan_private;

	memcpy(putil_fns, priv->curr_addr, addr, MLAN_MAC_ADDR_LENGTH);
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	putil_fns->moal_init_timer(putil_fns->pmoal_handle,
				   &priv->GrpRekeytimer,
				   KeyMgmtGrpRekeyCountUpdate, priv);
   /**Bss configure initialize*/
	AuthenitcatorInitBssConfig(priv);
#endif
#ifdef DRV_EMBEDDED_SUPPLICANT
	priv->suppData = MNULL;
	allocSupplicantData(priv);
	ret = initSupplicantTimer(priv);
	if (ret != MLAN_STATUS_SUCCESS) {
		goto done;
	}
	pmkCacheInit(priv);
	pmkCacheRomInit();
	keyMgmtSta_RomInit();
#endif

done:
	if (ret == MLAN_STATUS_SUCCESS)
		*pphostsa_priv = (t_void *)priv;
	else
		*pphostsa_priv = MNULL;
	LEAVE();
	return ret;
}

/**
 *  @brief Cleanup hostsa data
 *
 *  @param phostsa_priv    A pointer to a hostsa private data structure
 *
 *  @return             MLAN_STATUS_SUCCESS
 */
mlan_status
supplicant_authenticator_free(t_void *phostsa_priv)
{
	mlan_status ret = MLAN_STATUS_SUCCESS;
	phostsa_private priv = (hostsa_private *)phostsa_priv;
	hostsa_util_fns *putil_fns = &priv->util_fns;

	ENTER();

	if (!phostsa_priv)
		goto done;
#ifdef DRV_EMBEDDED_SUPPLICANT
	freeSupplicantData(priv);
	freeSupplicantTimer(priv);
#endif
#ifdef DRV_EMBEDDED_AUTHENTICATOR
	putil_fns->moal_stop_timer(putil_fns->pmoal_handle,
				   priv->GrpRekeytimer);
	putil_fns->moal_free_timer(putil_fns->pmoal_handle,
				   priv->GrpRekeytimer);
#endif
	free(putil_fns, (t_u8 *)priv);
done:
	LEAVE();
	return ret;
}
