/** @file supplicant.c
 *
 *  @brief This file defines the API for supplicant
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
#include "keyMgmtSta.h"
#include "keyCommonDef.h"
#include "keyMgmtSta.h"
#include "pmkCache.h"

t_u8
EAPoLKeyPkt_Validation(mlan_buffer *pmbuf)
{
	t_u32 recvd_pkt_len, eapol_pkt_len;
	EAPOL_KeyMsg_t *pKeyMsg = NULL;

	pKeyMsg =
		(EAPOL_KeyMsg_t *)(pmbuf->pbuf + pmbuf->data_offset +
				   sizeof(ether_hdr_t));
	/* Received eapol pkt length: DataLen - 802.3 header */
	recvd_pkt_len = pmbuf->data_len - sizeof(ether_hdr_t);
	/* 8021.X header + EAPOL key pkt header */
	eapol_pkt_len = sizeof(EAPOL_KeyMsg_t) - sizeof(pKeyMsg->key_data);

	if (recvd_pkt_len < eapol_pkt_len) {
		PRINTM(MERROR,
		       "Invalid EAPOL Key Msg, received length: %u, least length: %u\n",
		       recvd_pkt_len, eapol_pkt_len);
		return 1;
	}
	/* Todo: other validation check */

	return 0;
}

static __inline void
ProcessEAPoLKeyPkt(phostsa_private priv, mlan_buffer *pmbuf,
		   IEEEtypes_MacAddr_t *sa, IEEEtypes_MacAddr_t *da)
{
	hostsa_mlan_fns *pm_fns = &priv->mlan_fns;
	t_u8 bss_role = pm_fns->Hostsa_get_bss_role(pm_fns->pmlan_private);

	PRINTM(MMSG, "ProcessEAPoLKeyPk bss_type=%x bss_role=%x\n",
	       pm_fns->bss_type, bss_role);

#ifdef MIB_STATS
	INC_MIB_STAT(connPtr, eapolRxForESUPPCnt);
#endif

	if (EAPoLKeyPkt_Validation(pmbuf) != 0)
		return;

	switch (bss_role) {
#ifdef BTAMP
	case CONNECTION_TYPE_BTAMP:
		ProcessKeyMgmtDataAmp(bufDesc);
		break;
#endif

	case MLAN_BSS_ROLE_STA:
		/*key data */
		ProcessKeyMgmtDataSta(priv, pmbuf, sa, da);
		break;

	default:
#ifdef AUTHENTICATOR
		if (WIFI_DIRECT_MODE_GO == connPtr->DeviceMode) {
			ProcessKeyMgmtDataAp(bufDesc);
		}
#endif

		break;
	}
}

t_u8
ProcessEAPoLPkt(void *priv, mlan_buffer *pmbuf)
{
	phostsa_private psapriv = (phostsa_private)priv;
	ether_hdr_t *pEthHdr =
		(ether_hdr_t *)(pmbuf->pbuf + pmbuf->data_offset);
	EAP_PacketMsg_t *pEapPkt = NULL;
	UINT8 fPacketProcessed = 0;

	pEapPkt = (EAP_PacketMsg_t *)((t_u8 *)pEthHdr + sizeof(ether_hdr_t));
	switch (pEapPkt->hdr_8021x.pckt_type) {
	case IEEE_8021X_PACKET_TYPE_EAPOL_KEY:
		ProcessEAPoLKeyPkt(psapriv, pmbuf,
				   (IEEEtypes_MacAddr_t *)pEthHdr->sa,
				   (IEEEtypes_MacAddr_t *)pEthHdr->da);
		fPacketProcessed = 1;
		break;

#if 0
	case IEEE_8021X_PACKET_TYPE_EAP_PACKET:
		{
			if (WIFI_DIRECT_MODE_CLIENT == connPtr->DeviceMode
			    || WIFI_DIRECT_MODE_DEVICE == connPtr->DeviceMode) {
				if (pEapPkt->code ==
				    IEEE_8021X_CODE_TYPE_REQUEST) {
					assocAgent_eapRequestRx(sa);
				}
			}
		}
		break;
#endif
	default:
		break;
	}
//    CLEAN_FLUSH_CACHED_SQMEM((UINT32)(pEapPkt), bufDesc->DataLen);
	return fPacketProcessed;
}

Status_e
supplicantRestoreDefaults(void *priv)
{
	pmkCacheInit(priv);
	return SUCCESS;
}

/* This can also be removed*/
//#pragma arm section code = ".init"
void
supplicantFuncInit(void *priv)
{
	supplicantRestoreDefaults(priv);
}

//#pragma arm section code
//#endif /* PSK_SUPPLICANT */
