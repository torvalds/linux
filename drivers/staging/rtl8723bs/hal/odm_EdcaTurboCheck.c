// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

static u32 edca_setting_DL_GMode[HT_IOT_PEER_MAX] = {
/*UNKNOWN, REALTEK_90, ALTEK_92SE	BROADCOM, LINK	ATHEROS,
 *CISCO, MERU, MARVELL, 92U_AP, SELF_AP
 */
	0x4322, 0xa44f, 0x5e4322, 0xa42b, 0x5e4322, 0x4322,
	0xa42b, 0x5ea42b, 0xa44f, 0x5e4322, 0x5ea42b
};

static u32 edca_setting_UL[HT_IOT_PEER_MAX] = {
/*UNKNOWN, REALTEK_90, REALTEK_92SE, BROADCOM, RALINK, ATHEROS,
 *CISCO, MERU, MARVELL, 92U_AP, SELF_AP(DownLink/Tx)
 */
	0x5e4322, 0xa44f, 0x5e4322, 0x5ea32b, 0x5ea422,	0x5ea322,
	0x3ea430, 0x5ea42b, 0x5ea44f, 0x5e4322, 0x5e4322};

static u32 edca_setting_DL[HT_IOT_PEER_MAX] = {
/*UNKNOWN, REALTEK_90, REALTEK_92SE, BROADCOM, RALINK, ATHEROS,
 *CISCO, MERU, MARVELL, 92U_AP, SELF_AP(UpLink/Rx)
 */
	0xa44f, 0x5ea44f, 0x5e4322, 0x5ea42b, 0xa44f, 0xa630,
	0x5ea630, 0x5ea42b, 0xa44f, 0xa42b, 0xa42b};

void ODM_EdcaTurboInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	struct adapter *Adapter = pDM_Odm->Adapter;

	pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = false;
	pDM_Odm->DM_EDCA_Table.bIsCurRDLState = false;
	Adapter->recvpriv.bIsAnyNonBEPkts = false;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("Orginial VO PARAM: 0x%x\n",
		      rtw_read32(pDM_Odm->Adapter, ODM_EDCA_VO_PARAM)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("Orginial VI PARAM: 0x%x\n",
		      rtw_read32(pDM_Odm->Adapter, ODM_EDCA_VI_PARAM)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("Orginial BE PARAM: 0x%x\n",
		      rtw_read32(pDM_Odm->Adapter, ODM_EDCA_BE_PARAM)));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("Orginial BK PARAM: 0x%x\n",
		      rtw_read32(pDM_Odm->Adapter, ODM_EDCA_BK_PARAM)));
}	/*  ODM_InitEdcaTurbo */

void odm_EdcaTurboCheck(void *pDM_VOID)
{
	/*  In HW integration first stage, we provide 4 different handles to
	 *  operate at the same time. In stage2/3, we need to prove universal
	 *  interface and merge all HW dynamic mechanism.
	 */
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("odm_EdcaTurboCheck ========================>\n"));

	if (!(pDM_Odm->SupportAbility & ODM_MAC_EDCA_TURBO))
		return;

	odm_EdcaTurboCheckCE(pDM_Odm);
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_EDCA_TURBO, ODM_DBG_LOUD,
		     ("<========================odm_EdcaTurboCheck\n"));
}	/*  odm_CheckEdcaTurbo */

void odm_EdcaTurboCheckCE(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	struct adapter *Adapter = pDM_Odm->Adapter;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(Adapter);
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	struct registry_priv *pregpriv = &Adapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 EDCA_BE_UL = 0x5ea42b;
	u32 EDCA_BE_DL = 0x5ea42b;
	u32 iot_peer = 0;
	u8 wirelessmode = 0xFF;		/* invalid value */
	u32 trafficIndex;
	u32 edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u8 bbtchange = false;
	u8 biasonrx = false;
	struct hal_com_data	*pHalData = GET_HAL_DATA(Adapter);

	if (!pDM_Odm->bLinked) {
		precvpriv->bIsAnyNonBEPkts = false;
		return;
	}

	if (pregpriv->wifi_spec == 1) {
		precvpriv->bIsAnyNonBEPkts = false;
		return;
	}

	if (pDM_Odm->pwirelessmode)
		wirelessmode = *(pDM_Odm->pwirelessmode);

	iot_peer = pmlmeinfo->assoc_AP_vendor;

	if (iot_peer >=  HT_IOT_PEER_MAX) {
		precvpriv->bIsAnyNonBEPkts = false;
		return;
	}

	/*  Check if the status needs to be changed. */
	if ((bbtchange) || (!precvpriv->bIsAnyNonBEPkts)) {
		cur_tx_bytes = pdvobjpriv->traffic_stat.cur_tx_bytes;
		cur_rx_bytes = pdvobjpriv->traffic_stat.cur_rx_bytes;

		/* traffic, TX or RX */
		if (biasonrx) {
			if (cur_tx_bytes > (cur_rx_bytes << 2)) {
				/*  Uplink TP is present. */
				trafficIndex = UP_LINK;
			} else { /*  Balance TP is present. */
				trafficIndex = DOWN_LINK;
			}
		} else {
			if (cur_rx_bytes > (cur_tx_bytes << 2)) {
				/*  Downlink TP is present. */
				trafficIndex = DOWN_LINK;
			} else { /*  Balance TP is present. */
				trafficIndex = UP_LINK;
			}
		}

		/* 92D txop can't be set to 0x3e for cisco1250 */
		if ((iot_peer == HT_IOT_PEER_CISCO) &&
		    (wirelessmode == ODM_WM_N24G)) {
			EDCA_BE_DL = edca_setting_DL[iot_peer];
			EDCA_BE_UL = edca_setting_UL[iot_peer];
		} else if ((iot_peer == HT_IOT_PEER_CISCO) &&
			   ((wirelessmode == ODM_WM_G) ||
			    (wirelessmode == (ODM_WM_B | ODM_WM_G)) ||
			    (wirelessmode == ODM_WM_A) ||
			    (wirelessmode == ODM_WM_B))) {
			EDCA_BE_DL = edca_setting_DL_GMode[iot_peer];
		} else if ((iot_peer == HT_IOT_PEER_AIRGO) &&
			   ((wirelessmode == ODM_WM_G) ||
			    (wirelessmode == ODM_WM_A))) {
			EDCA_BE_DL = 0xa630;
		} else if (iot_peer == HT_IOT_PEER_MARVELL) {
			EDCA_BE_DL = edca_setting_DL[iot_peer];
			EDCA_BE_UL = edca_setting_UL[iot_peer];
		} else if (iot_peer == HT_IOT_PEER_ATHEROS) {
			/*  Set DL EDCA for Atheros peer to 0x3ea42b. */
			EDCA_BE_DL = edca_setting_DL[iot_peer];
		}

		if (trafficIndex == DOWN_LINK)
			edca_param = EDCA_BE_DL;
		else
			edca_param = EDCA_BE_UL;

		rtw_write32(Adapter, REG_EDCA_BE_PARAM, edca_param);

		pDM_Odm->DM_EDCA_Table.prv_traffic_idx = trafficIndex;

		pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = true;
	} else {
		/*  Turn Off EDCA turbo here. */
		/*  Restore original EDCA according to the declaration of AP. */
		 if (pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA) {
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, pHalData->AcParam_BE);
			pDM_Odm->DM_EDCA_Table.bCurrentTurboEDCA = false;
		}
	}
}
