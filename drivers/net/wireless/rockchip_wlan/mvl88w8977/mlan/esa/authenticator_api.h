/** @file Authenticator_api.h
 *
 *  @brief This file delare the main APIs for authenticator.
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
#ifndef _AUTHENTICATORAPI_H
#define _AUTHENTICATORAPI_H

/******************************************************
Change log:
    03/01/2014: Initial version
******************************************************/
#ifdef DRV_EMBEDDED_AUTHENTICATOR
extern t_u8 IsAuthenticatorEnabled(void *priv);
extern void AuthenitcatorInitBssConfig(void *priv);
extern void AuthenticatorKeyMgmtInit(void *priv, t_u8 *addr);
extern void AuthenticatorkeyClear(void *priv);
extern void authenticator_init_client(void *priv, void **ppconnPtr, t_u8 *mac);
extern void authenticator_free_client(void *priv, void *ppconnPtr);
extern void authenticator_get_sta_security_info(void *priv,
						t_void *pconnPtr, t_u8 *pIe,
						t_u8 ieLen);
extern t_void AuthenticatorSendEapolPacket(t_void *priv, t_void *pconnPtr);
extern t_u8 AuthenticatorProcessEapolPacket(void *priv, t_u8 *pbuf, t_u32 len);
extern t_u8 AuthenticatorBssConfig(void *priv, t_u8 *pbss_config, t_u8 appendIE,
				   t_u8 clearIE, t_u8 SetConfigToMlan);
#endif
mlan_status supplicant_authenticator_init(t_void **pphostsa_priv,
					  t_void *psa_util_fns,
					  t_void *psa_mlan_fns, t_u8 *addr);
mlan_status supplicant_authenticator_free(t_void *phostsa_priv);
#ifdef DRV_EMBEDDED_SUPPLICANT
extern void supplicantClrEncryptKey(void *priv);
extern void *processRsnWpaInfo(void *priv, void *prsnwpa_ie);
extern void pmkCacheDeletePMK(void *priv, t_u8 *pBssid);
extern void supplicantInitSession(void *priv,
				  t_u8 *pSsid,
				  t_u16 len, t_u8 *pBssid, t_u8 *pStaAddr);
extern void supplicantStopSessionTimer(void *priv);
extern t_u8 supplicantIsEnabled(void *priv);
extern void supplicantQueryPassphraseAndEnable(void *priv, t_u8 *pbuf);
extern void supplicantDisable(void *priv);
extern t_u8 ProcessEAPoLPkt(void *priv, mlan_buffer *pmbuf);
extern t_u8 EAPoLKeyPkt_Validation(mlan_buffer *pmbuf);
extern void SupplicantClearPMK(void *priv, void *pPassphrase);
extern t_u16 SupplicantSetPassphrase(void *priv, void *pPassphraseBuf);
extern void SupplicantQueryPassphrase(void *priv, void *pPassphraseBuf);
extern t_u8 supplicantFormatRsnWpaTlv(void *priv, void *rsn_wpa_ie,
				      void *rsn_ie_tlv);
#endif
#endif	/**_AUTHENTICATORAPI_H*/
