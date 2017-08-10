/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#define _RTL8723B_CMD_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>
#include "hal_com_h2c.h"

#define MAX_H2C_BOX_NUMS	4
#define MESSAGE_BOX_SIZE	4

#define RTL8723B_MAX_CMD_LEN	7
#define RTL8723B_EX_MESSAGE_BOX_SIZE	4

static u8 _is_fw_read_cmd_down(struct adapter *padapter, u8 msgbox_num)
{
	u8 read_down = false;
	int retry_cnts = 100;

	u8 valid;

	/* DBG_8192C(" _is_fw_read_cmd_down , reg_1cc(%x), msg_box(%d)...\n", rtw_read8(padapter, REG_HMETFR), msgbox_num); */

	do {
		valid = rtw_read8(padapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid) {
			read_down = true;
		}
#ifdef CONFIG_WOWLAN
		else
			msleep(1);
#endif
	} while ((!read_down) && (retry_cnts--));

	return read_down;

}


/*****************************************
* H2C Msg format :
*| 31 - 8		|7-5	| 4 - 0	|
*| h2c_msg	|Class	|CMD_ID	|
*| 31-0						|
*| Ext msg					|
*
******************************************/
s32 FillH2CCmd8723B(struct adapter *padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 h2c_box_num;
	u32 msgbox_addr;
	u32 msgbox_ex_addr = 0;
	struct hal_com_data *pHalData;
	u32 h2c_cmd = 0;
	u32 h2c_cmd_ex = 0;
	s32 ret = _FAIL;

	padapter = GET_PRIMARY_ADAPTER(padapter);
	pHalData = GET_HAL_DATA(padapter);
	if (mutex_lock_interruptible(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex)))
		return ret;

	if (!pCmdBuffer) {
		goto exit;
	}

	if (CmdLen > RTL8723B_MAX_CMD_LEN) {
		goto exit;
	}

	if (padapter->bSurpriseRemoved == true)
		goto exit;

	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = pHalData->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(padapter, h2c_box_num)) {
			DBG_8192C(" fw read cmd failed...\n");
			/* DBG_8192C(" 0x1c0: 0x%8x\n", rtw_read32(padapter, 0x1c0)); */
			/* DBG_8192C(" 0x1c4: 0x%8x\n", rtw_read32(padapter, 0x1c4)); */
			goto exit;
		}

		if (CmdLen <= 3)
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer, CmdLen);
		else {
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer, 3);
			memcpy((u8 *)(&h2c_cmd_ex), pCmdBuffer+3, CmdLen-3);
/* 			*(u8 *)(&h2c_cmd) |= BIT(7); */
		}

		*(u8 *)(&h2c_cmd) |= ElementID;

		if (CmdLen > 3) {
			msgbox_ex_addr = REG_HMEBOX_EXT0_8723B + (h2c_box_num*RTL8723B_EX_MESSAGE_BOX_SIZE);
			rtw_write32(padapter, msgbox_ex_addr, h2c_cmd_ex);
		}
		msgbox_addr = REG_HMEBOX_0 + (h2c_box_num*MESSAGE_BOX_SIZE);
		rtw_write32(padapter, msgbox_addr, h2c_cmd);

		/* DBG_8192C("MSG_BOX:%d, CmdLen(%d), CmdID(0x%x), reg:0x%x =>h2c_cmd:0x%.8x, reg:0x%x =>h2c_cmd_ex:0x%.8x\n" */
		/* 	, pHalData->LastHMEBoxNum , CmdLen, ElementID, msgbox_addr, h2c_cmd, msgbox_ex_addr, h2c_cmd_ex); */

		pHalData->LastHMEBoxNum = (h2c_box_num+1) % MAX_H2C_BOX_NUMS;

	} while (0);

	ret = _SUCCESS;

exit:

	mutex_unlock(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex));
	return ret;
}

static void ConstructBeacon(struct adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 rate_len, pktlen;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *cur_network = &(pmlmeinfo->network);
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	/* DBG_871X("%s\n", __func__); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pktlen = sizeof (struct ieee80211_hdr_3addr);

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/*  beacon interval: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/*  capability info: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		/* DBG_871X("ie len =%d\n", cur_network->IELength); */
		pktlen += cur_network->IELength - sizeof(struct ndis_802_11_fix_ie);
		memcpy(pframe, cur_network->IEs+sizeof(struct ndis_802_11_fix_ie), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/*  SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	/* todo: ERP IE */


	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);


	/* todo:HT for adhoc */

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512) {
		DBG_871X("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	/* DBG_871X("%s bcn_sz =%d\n", __func__, pktlen); */

}

static void ConstructPSPoll(struct adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	/* DBG_871X("%s\n", __func__); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	/*  Frame control. */
	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	/*  AID. */
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	/*  BSSID. */
	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	/*  TA. */
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);

	*pLength = 16;
}

static void ConstructNullFunctionData(
	struct adapter *padapter,
	u8 *pframe,
	u32 *pLength,
	u8 *StaAddr,
	u8 bQoS,
	u8 AC,
	u8 bEosp,
	u8 bForcePowerSave
)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 pktlen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network *cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);


	/* DBG_871X("%s:%d\n", __func__, bForcePowerSave); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
		memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
		break;
	}

	SetSeqNum(pwlanhdr, 0);

	if (bQoS == true) {
		struct ieee80211_qos_hdr *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct ieee80211_qos_hdr *)pframe;
		SetPriority(&pwlanqoshdr->qos_ctrl, AC);
		SetEOSP(&pwlanqoshdr->qos_ctrl, bEosp);

		pktlen = sizeof(struct ieee80211_qos_hdr);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}


#ifdef CONFIG_WOWLAN
/*  */
/*  Description: */
/* 	Construct the ARP response packet to support ARP offload. */
/*  */
static void ConstructARPResponse(
	struct adapter *padapter,
	u8 *pframe,
	u32 *pLength,
	u8 *pIPAddress
)
{
	struct ieee80211_hdr	*pwlanhdr;
	__le16 *fctrl;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	static u8 	ARPLLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x08, 0x06};
	u8 		*pARPRspPkt = pframe;
	/* for TKIP Cal MIC */
	u8 		*payload = pframe;
	u8 	EncryptionHeadOverhead = 0;
	/* DBG_871X("%s:%d\n", __func__, bForcePowerSave); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	/*  */
	/*  MAC Header. */
	/*  */
	SetFrameType(fctrl, WIFI_DATA);
	/* SetFrameSubType(fctrl, 0); */
	SetToDs(fctrl);
	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);
	/* SET_80211_HDR_FRAME_CONTROL(pARPRspPkt, 0); */
	/* SET_80211_HDR_TYPE_AND_SUBTYPE(pARPRspPkt, Type_Data); */
	/* SET_80211_HDR_TO_DS(pARPRspPkt, 1); */
	/* SET_80211_HDR_ADDRESS1(pARPRspPkt, pMgntInfo->Bssid); */
	/* SET_80211_HDR_ADDRESS2(pARPRspPkt, Adapter->CurrentAddress); */
	/* SET_80211_HDR_ADDRESS3(pARPRspPkt, pMgntInfo->Bssid); */

	/* SET_80211_HDR_DURATION(pARPRspPkt, 0); */
	/* SET_80211_HDR_FRAGMENT_SEQUENCE(pARPRspPkt, 0); */
	*pLength = 24;

	/*  */
	/*  Security Header: leave space for it if necessary. */
	/*  */

	switch (psecuritypriv->dot11PrivacyAlgrthm) {
	case _WEP40_:
	case _WEP104_:
		EncryptionHeadOverhead = 4;
		break;
	case _TKIP_:
		EncryptionHeadOverhead = 8;
		break;
	case _AES_:
		EncryptionHeadOverhead = 8;
		break;
	default:
		EncryptionHeadOverhead = 0;
	}

	if (EncryptionHeadOverhead > 0) {
		memset(&(pframe[*pLength]), 0, EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		SetPrivacy(fctrl);
	}

	/*  */
	/*  Frame Body. */
	/*  */
	pARPRspPkt = (u8 *)(pframe + *pLength);
	payload = pARPRspPkt; /* Get Payload pointer */
	/*  LLC header */
	memcpy(pARPRspPkt, ARPLLCHeader, 8);
	*pLength += 8;

	/*  ARP element */
	pARPRspPkt += 8;
	SET_ARP_PKT_HW(pARPRspPkt, 0x0100);
	SET_ARP_PKT_PROTOCOL(pARPRspPkt, 0x0008);	/*  IP protocol */
	SET_ARP_PKT_HW_ADDR_LEN(pARPRspPkt, 6);
	SET_ARP_PKT_PROTOCOL_ADDR_LEN(pARPRspPkt, 4);
	SET_ARP_PKT_OPERATION(pARPRspPkt, 0x0200); /*  ARP response */
	SET_ARP_PKT_SENDER_MAC_ADDR(pARPRspPkt, myid(&(padapter->eeprompriv)));
	SET_ARP_PKT_SENDER_IP_ADDR(pARPRspPkt, pIPAddress);
	{
		SET_ARP_PKT_TARGET_MAC_ADDR(pARPRspPkt, get_my_bssid(&(pmlmeinfo->network)));
		SET_ARP_PKT_TARGET_IP_ADDR(pARPRspPkt, pIPAddress);
		DBG_871X("%s Target Mac Addr:" MAC_FMT "\n", __func__, MAC_ARG(get_my_bssid(&(pmlmeinfo->network))));
		DBG_871X("%s Target IP Addr" IP_FMT "\n", __func__, IP_ARG(pIPAddress));
	}

	*pLength += 28;

	if (psecuritypriv->dot11PrivacyAlgrthm == _TKIP_) {
		u8 mic[8];
		struct mic_data	micdata;
		struct sta_info *psta = NULL;
		u8 priority[4] = {
			0x0, 0x0, 0x0, 0x0
		};
		u8 null_key[16] = {
			0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
			0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
		};

		DBG_871X("%s(): Add MIC\n", __func__);

		psta = rtw_get_stainfo(&padapter->stapriv, get_my_bssid(&(pmlmeinfo->network)));
		if (psta != NULL) {
			if (!memcmp(&psta->dot11tkiptxmickey.skey[0], null_key, 16)) {
				DBG_871X("%s(): STA dot11tkiptxmickey == 0\n", __func__);
			}
			/* start to calculate the mic code */
			rtw_secmicsetkey(&micdata, &psta->dot11tkiptxmickey.skey[0]);
		}

		rtw_secmicappend(&micdata, pwlanhdr->addr3, 6);  /* DA */

		rtw_secmicappend(&micdata, pwlanhdr->addr2, 6); /* SA */

		priority[0] = 0;
		rtw_secmicappend(&micdata, &priority[0], 4);

		rtw_secmicappend(&micdata, payload, 36); /* payload length = 8 + 28 */

		rtw_secgetmic(&micdata, &(mic[0]));

		pARPRspPkt += 28;
		memcpy(pARPRspPkt, &(mic[0]), 8);

		*pLength += 8;
	}
}

#ifdef CONFIG_PNO_SUPPORT
static void ConstructPnoInfo(
	struct adapter *padapter, u8 *pframe, u32 *pLength
)
{

	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	u8 *pPnoInfoPkt = pframe;
	pPnoInfoPkt = (u8 *)(pframe + *pLength);
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_num, 4);

	*pLength += 4;
	pPnoInfoPkt += 4;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_period, 4);

	*pLength += 4;
	pPnoInfoPkt += 4;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->fast_scan_iterations, 4);

	*pLength += 4;
	pPnoInfoPkt += 4;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->slow_scan_period, 4);

	*pLength += 4;
	pPnoInfoPkt += 4;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_length,
			MAX_PNO_LIST_COUNT);

	*pLength += MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_cipher_info,
			MAX_PNO_LIST_COUNT);

	*pLength += MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
	memcpy(pPnoInfoPkt, &pwrctl->pnlo_info->ssid_channel_info,
			MAX_PNO_LIST_COUNT);

	*pLength += MAX_PNO_LIST_COUNT;
	pPnoInfoPkt += MAX_PNO_LIST_COUNT;
}

static void ConstructSSIDList(
	struct adapter *padapter, u8 *pframe, u32 *pLength
)
{
	int i = 0;
	u8 *pSSIDListPkt = pframe;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	pSSIDListPkt = (u8 *)(pframe + *pLength);

	for (i = 0; i < pwrctl->pnlo_info->ssid_num ; i++) {
		memcpy(pSSIDListPkt, &pwrctl->pno_ssid_list->node[i].SSID,
			pwrctl->pnlo_info->ssid_length[i]);

		*pLength += WLAN_SSID_MAXLEN;
		pSSIDListPkt += WLAN_SSID_MAXLEN;
	}
}

static void ConstructScanInfo(
	struct adapter *padapter, u8 *pframe, u32 *pLength
)
{
	int i = 0;
	u8 *pScanInfoPkt = pframe;
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	pScanInfoPkt = (u8 *)(pframe + *pLength);

	memcpy(pScanInfoPkt, &pwrctl->pscan_info->channel_num, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_ch, 1);


	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_bw, 1);


	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_40_offset, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->orig_80_offset, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->periodScan, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->period_scan_time, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->enableRFE, 1);

	*pLength += 1;
	pScanInfoPkt += 1;
	memcpy(pScanInfoPkt, &pwrctl->pscan_info->rfe_type, 8);

	*pLength += 8;
	pScanInfoPkt += 8;

	for (i = 0; i < MAX_SCAN_LIST_COUNT; i++) {
		memcpy(pScanInfoPkt, &pwrctl->pscan_info->ssid_channel_info[i], 4);
		*pLength += 4;
		pScanInfoPkt += 4;
	}
}
#endif

#ifdef CONFIG_GTK_OL
static void ConstructGTKResponse(
	struct adapter *padapter, u8 *pframe, u32 *pLength
)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct security_priv *psecuritypriv = &padapter->securitypriv;
	static u8 LLCHeader[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};
	static u8 GTKbody_a[11] = {0x01, 0x03, 0x00, 0x5F, 0x02, 0x03, 0x12, 0x00, 0x10, 0x42, 0x0B};
	u8 *pGTKRspPkt = pframe;
	u8 EncryptionHeadOverhead = 0;
	/* DBG_871X("%s:%d\n", __func__, bForcePowerSave); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	/*  */
	/*  MAC Header. */
	/*  */
	SetFrameType(fctrl, WIFI_DATA);
	/* SetFrameSubType(fctrl, 0); */
	SetToDs(fctrl);
	memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetDuration(pwlanhdr, 0);

	*pLength = 24;

	/*  */
	/*  Security Header: leave space for it if necessary. */
	/*  */

	switch (psecuritypriv->dot11PrivacyAlgrthm) {
	case _WEP40_:
	case _WEP104_:
		EncryptionHeadOverhead = 4;
		break;
	case _TKIP_:
		EncryptionHeadOverhead = 8;
		break;
	case _AES_:
		EncryptionHeadOverhead = 8;
		break;
	default:
		EncryptionHeadOverhead = 0;
	}

	if (EncryptionHeadOverhead > 0) {
		memset(&(pframe[*pLength]), 0, EncryptionHeadOverhead);
		*pLength += EncryptionHeadOverhead;
		/* GTK's privacy bit is done by FW */
		/* SetPrivacy(fctrl); */
	}

	/*  */
	/*  Frame Body. */
	/*  */
	pGTKRspPkt =  (u8 *)(pframe + *pLength);
	/*  LLC header */
	memcpy(pGTKRspPkt, LLCHeader, 8);
	*pLength += 8;

	/*  GTK element */
	pGTKRspPkt += 8;

	/* GTK frame body after LLC, part 1 */
	memcpy(pGTKRspPkt, GTKbody_a, 11);
	*pLength += 11;
	pGTKRspPkt += 11;
	/* GTK frame body after LLC, part 2 */
	memset(&(pframe[*pLength]), 0, 88);
	*pLength += 88;
	pGTKRspPkt += 88;

}
#endif /* CONFIG_GTK_OL */

#ifdef CONFIG_PNO_SUPPORT
static void ConstructProbeReq(struct adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u32 pktlen;
	unsigned char *mac;
	unsigned char bssrate[NumRates];
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	int bssrate_len = 0;
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	pwlanhdr = (struct ieee80211_hdr *)pframe;
	mac = myid(&(padapter->eeprompriv));

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;

	/* broadcast probe request frame */
	memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr3, bc_addr, ETH_ALEN);

	memcpy(pwlanhdr->addr2, mac, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(pframe, WIFI_PROBEREQ);

	pktlen = sizeof(struct ieee80211_hdr_3addr);
	pframe += pktlen;

	pframe = rtw_set_ie(pframe, _SSID_IE_, 0, NULL, &pktlen);

	get_rate_set(padapter, bssrate, &bssrate_len);

	if (bssrate_len > 8) {
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, 8, bssrate, &pktlen);
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (bssrate_len - 8), (bssrate + 8), &pktlen);
	} else
		pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, bssrate_len, bssrate, &pktlen);

	*pLength = pktlen;
}
#endif /* CONFIG_PNO_SUPPORT */
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
static void ConstructProbeRsp(struct adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, bool bHideSSID)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u8 *mac, *bssid;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *cur_network = &(pmlmeinfo->network);
	u8 *pwps_ie;
	uint wps_ielen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	/* DBG_871X("%s\n", __func__); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(padapter->eeprompriv));
	bssid = cur_network->MacAddress;

	fctrl = &(pwlanhdr->frame_control);
	*(fctrl) = 0;
	memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	DBG_871X("%s FW Mac Addr:" MAC_FMT "\n", __func__, MAC_ARG(mac));
	DBG_871X("%s FW IP Addr" IP_FMT "\n", __func__, IP_ARG(StaAddr));

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct ieee80211_hdr_3addr);
	pframe += pktlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

	pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_,
			cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

	/* inerset & update wps_probe_resp_ie */
	if ((pmlmepriv->wps_probe_resp_ie != NULL) && pwps_ie && (wps_ielen > 0)) {
		uint wps_offset, remainder_ielen;
		u8 *premainder_ie;

		wps_offset = (uint)(pwps_ie - cur_network->IEs);

		premainder_ie = pwps_ie + wps_ielen;

		remainder_ielen = cur_network->IELength - wps_offset - wps_ielen;

		memcpy(pframe, cur_network->IEs, wps_offset);
		pframe += wps_offset;
		pktlen += wps_offset;

		wps_ielen = (uint)pmlmepriv->wps_probe_resp_ie[1];/* to get ie data len */
		if ((wps_offset+wps_ielen+2) <= MAX_IE_SZ) {
			memcpy(pframe, pmlmepriv->wps_probe_resp_ie, wps_ielen+2);
			pframe += wps_ielen+2;
			pktlen += wps_ielen+2;
		}

		if ((wps_offset+wps_ielen+2+remainder_ielen) <= MAX_IE_SZ) {
			memcpy(pframe, premainder_ie, remainder_ielen);
			pframe += remainder_ielen;
			pktlen += remainder_ielen;
		}
	} else {
		memcpy(pframe, cur_network->IEs, cur_network->IELength);
		pframe += cur_network->IELength;
		pktlen += cur_network->IELength;
	}

	/* retrieve SSID IE from cur_network->Ssid */
	{
		u8 *ssid_ie;
		sint ssid_ielen;
		sint ssid_ielen_diff;
		u8 buf[MAX_IE_SZ];
		u8 *ies = pframe + sizeof(struct ieee80211_hdr_3addr);

		ssid_ie = rtw_get_ie(ies+_FIXED_IE_LENGTH_, _SSID_IE_, &ssid_ielen,
					(pframe-ies)-_FIXED_IE_LENGTH_);

		ssid_ielen_diff = cur_network->Ssid.SsidLength - ssid_ielen;

		if (ssid_ie &&	cur_network->Ssid.SsidLength) {
			uint remainder_ielen;
			u8 *remainder_ie;
			remainder_ie = ssid_ie+2;
			remainder_ielen = (pframe-remainder_ie);

			if (remainder_ielen > MAX_IE_SZ) {
				DBG_871X_LEVEL(_drv_warning_, FUNC_ADPT_FMT" remainder_ielen > MAX_IE_SZ\n", FUNC_ADPT_ARG(padapter));
				remainder_ielen = MAX_IE_SZ;
			}

			memcpy(buf, remainder_ie, remainder_ielen);
			memcpy(remainder_ie+ssid_ielen_diff, buf, remainder_ielen);
			*(ssid_ie+1) = cur_network->Ssid.SsidLength;
			memcpy(ssid_ie+2, cur_network->Ssid.Ssid, cur_network->Ssid.SsidLength);
			pframe += ssid_ielen_diff;
			pktlen += ssid_ielen_diff;
		}
	}

	*pLength = pktlen;

}
#endif /*  CONFIG_AP_WOWLAN */

/*  To check if reserved page content is destroyed by beacon beacuse beacon is too large. */
/*  2010.06.23. Added by tynli. */
void CheckFwRsvdPageContent(struct adapter *Adapter)
{
}

static void rtl8723b_set_FwRsvdPage_cmd(struct adapter *padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	u8 u1H2CRsvdPageParm[H2C_RSVDPAGE_LOC_LEN] = {0};

	DBG_871X("8723BRsvdPageLoc: ProbeRsp =%d PsPoll =%d Null =%d QoSNull =%d BTNull =%d\n",
		rsvdpageloc->LocProbeRsp, rsvdpageloc->LocPsPoll,
		rsvdpageloc->LocNullData, rsvdpageloc->LocQosNull,
		rsvdpageloc->LocBTQosNull);

	SET_8723B_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1H2CRsvdPageParm, rsvdpageloc->LocProbeRsp);
	SET_8723B_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1H2CRsvdPageParm, rsvdpageloc->LocPsPoll);
	SET_8723B_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocNullData);
	SET_8723B_H2CCMD_RSVDPAGE_LOC_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocQosNull);
	SET_8723B_H2CCMD_RSVDPAGE_LOC_BT_QOS_NULL_DATA(u1H2CRsvdPageParm, rsvdpageloc->LocBTQosNull);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CRsvdPageParm:", u1H2CRsvdPageParm, H2C_RSVDPAGE_LOC_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_RSVD_PAGE, H2C_RSVDPAGE_LOC_LEN, u1H2CRsvdPageParm);
}

static void rtl8723b_set_FwAoacRsvdPage_cmd(struct adapter *padapter, PRSVDPAGE_LOC rsvdpageloc)
{
#ifdef CONFIG_WOWLAN
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};

	DBG_871X("8723BAOACRsvdPageLoc: RWC =%d ArpRsp =%d NbrAdv =%d GtkRsp =%d GtkInfo =%d ProbeReq =%d NetworkList =%d\n",
			rsvdpageloc->LocRemoteCtrlInfo, rsvdpageloc->LocArpRsp,
			rsvdpageloc->LocNbrAdv, rsvdpageloc->LocGTKRsp,
			rsvdpageloc->LocGTKInfo, rsvdpageloc->LocProbeReq,
			rsvdpageloc->LocNetList);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
		/* SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm, rsvdpageloc->LocNbrAdv); */
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKRsp);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKInfo);
#ifdef CONFIG_GTK_OL
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_EXT_MEM(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKEXTMEM);
#endif /*  CONFIG_GTK_OL */
		RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CAoacRsvdPageParm:", u1H2CAoacRsvdPageParm, H2C_AOAC_RSVDPAGE_LOC_LEN);
		FillH2CCmd8723B(padapter, H2C_8723B_AOAC_RSVD_PAGE, H2C_AOAC_RSVDPAGE_LOC_LEN, u1H2CAoacRsvdPageParm);
	} else {
#ifdef CONFIG_PNO_SUPPORT
		if (!pwrpriv->pno_in_resume) {
			DBG_871X("NLO_INFO =%d\n", rsvdpageloc->LocPNOInfo);
			memset(&u1H2CAoacRsvdPageParm, 0, sizeof(u1H2CAoacRsvdPageParm));
			SET_H2CCMD_AOAC_RSVDPAGE_LOC_NLO_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocPNOInfo);
			FillH2CCmd8723B(padapter, H2C_AOAC_RSVDPAGE3, H2C_AOAC_RSVDPAGE_LOC_LEN, u1H2CAoacRsvdPageParm);
			msleep(10);
		}
#endif
	}

#endif /*  CONFIG_WOWLAN */
}

#ifdef CONFIG_AP_WOWLAN
static void rtl8723b_set_ap_wow_rsvdpage_cmd(
	struct adapter *padapter, PRSVDPAGE_LOC rsvdpageloc
)
{
	u8 header;
	u8 rsvdparm[H2C_AOAC_RSVDPAGE_LOC_LEN] = {0};

	header = rtw_read8(padapter, REG_BCNQ_BDNY);

	DBG_871X("%s: beacon: %d, probeRsp: %d, header:0x%02x\n", __func__,
			rsvdpageloc->LocApOffloadBCN,
			rsvdpageloc->LocProbeRsp,
			header);

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_BCN(rsvdparm,
			rsvdpageloc->LocApOffloadBCN + header);

	FillH2CCmd8723B(padapter, H2C_8723B_BCN_RSVDPAGE,
			H2C_BCN_RSVDPAGE_LEN, rsvdparm);

	msleep(10);

	memset(&rsvdparm, 0, sizeof(rsvdparm));

	SET_H2CCMD_AP_WOWLAN_RSVDPAGE_LOC_ProbeRsp(
			rsvdparm,
			rsvdpageloc->LocProbeRsp + header);

	FillH2CCmd8723B(padapter, H2C_8723B_PROBERSP_RSVDPAGE,
			H2C_PROBERSP_RSVDPAGE_LEN, rsvdparm);

	msleep(10);
}
#endif /* CONFIG_AP_WOWLAN */

void rtl8723b_set_FwMediaStatusRpt_cmd(struct adapter *padapter, u8 mstatus, u8 macid)
{
	u8 u1H2CMediaStatusRptParm[H2C_MEDIA_STATUS_RPT_LEN] = {0};
	u8 macid_end = 0;

	DBG_871X("%s(): mstatus = %d macid =%d\n", __func__, mstatus, macid);

	SET_8723B_H2CCMD_MSRRPT_PARM_OPMODE(u1H2CMediaStatusRptParm, mstatus);
	SET_8723B_H2CCMD_MSRRPT_PARM_MACID_IND(u1H2CMediaStatusRptParm, 0);
	SET_8723B_H2CCMD_MSRRPT_PARM_MACID(u1H2CMediaStatusRptParm, macid);
	SET_8723B_H2CCMD_MSRRPT_PARM_MACID_END(u1H2CMediaStatusRptParm, macid_end);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CMediaStatusRptParm:", u1H2CMediaStatusRptParm, H2C_MEDIA_STATUS_RPT_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_MEDIA_STATUS_RPT, H2C_MEDIA_STATUS_RPT_LEN, u1H2CMediaStatusRptParm);
}

#ifdef CONFIG_WOWLAN
static void rtl8723b_set_FwKeepAlive_cmd(struct adapter *padapter, u8 benable, u8 pkt_type)
{
	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN] = {0};
	u8 adopt = 1, check_period = 5;

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, benable);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CKeepAliveParm:", u1H2CKeepAliveParm, H2C_KEEP_ALIVE_CTRL_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_KEEP_ALIVE, H2C_KEEP_ALIVE_CTRL_LEN, u1H2CKeepAliveParm);
}

static void rtl8723b_set_FwDisconDecision_cmd(struct adapter *padapter, u8 benable)
{
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN] = {0};
	u8 adopt = 1, check_period = 10, trypkt_num = 0;

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, benable);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CDisconDecisionParm:", u1H2CDisconDecisionParm, H2C_DISCON_DECISION_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_DISCON_DECISION, H2C_DISCON_DECISION_LEN, u1H2CDisconDecisionParm);
}
#endif /*  CONFIG_WOWLAN */

void rtl8723b_set_FwMacIdConfig_cmd(struct adapter *padapter, u8 mac_id, u8 raid, u8 bw, u8 sgi, u32 mask)
{
	u8 u1H2CMacIdConfigParm[H2C_MACID_CFG_LEN] = {0};

	DBG_871X("%s(): mac_id =%d raid = 0x%x bw =%d mask = 0x%x\n", __func__, mac_id, raid, bw, mask);

	SET_8723B_H2CCMD_MACID_CFG_MACID(u1H2CMacIdConfigParm, mac_id);
	SET_8723B_H2CCMD_MACID_CFG_RAID(u1H2CMacIdConfigParm, raid);
	SET_8723B_H2CCMD_MACID_CFG_SGI_EN(u1H2CMacIdConfigParm, sgi ? 1 : 0);
	SET_8723B_H2CCMD_MACID_CFG_BW(u1H2CMacIdConfigParm, bw);
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK0(u1H2CMacIdConfigParm, (u8)(mask & 0x000000ff));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK1(u1H2CMacIdConfigParm, (u8)((mask & 0x0000ff00) >> 8));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK2(u1H2CMacIdConfigParm, (u8)((mask & 0x00ff0000) >> 16));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK3(u1H2CMacIdConfigParm, (u8)((mask & 0xff000000) >> 24));

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CMacIdConfigParm:", u1H2CMacIdConfigParm, H2C_MACID_CFG_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_MACID_CFG, H2C_MACID_CFG_LEN, u1H2CMacIdConfigParm);
}

static void rtl8723b_set_FwRssiSetting_cmd(struct adapter *padapter, u8 *param)
{
	u8 u1H2CRssiSettingParm[H2C_RSSI_SETTING_LEN] = {0};
	u8 mac_id = *param;
	u8 rssi = *(param+2);
	u8 uldl_state = 0;

	/* DBG_871X("%s(): param =%.2x-%.2x-%.2x\n", __func__, *param, *(param+1), *(param+2)); */
	/* DBG_871X("%s(): mac_id =%d rssi =%d\n", __func__, mac_id, rssi); */

	SET_8723B_H2CCMD_RSSI_SETTING_MACID(u1H2CRssiSettingParm, mac_id);
	SET_8723B_H2CCMD_RSSI_SETTING_RSSI(u1H2CRssiSettingParm, rssi);
	SET_8723B_H2CCMD_RSSI_SETTING_ULDL_STATE(u1H2CRssiSettingParm, uldl_state);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_notice_, "u1H2CRssiSettingParm:", u1H2CRssiSettingParm, H2C_RSSI_SETTING_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_RSSI_SETTING, H2C_RSSI_SETTING_LEN, u1H2CRssiSettingParm);
}

void rtl8723b_set_FwPwrMode_cmd(struct adapter *padapter, u8 psmode)
{
	int i;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	u8 u1H2CPwrModeParm[H2C_PWRMODE_LEN] = {0};
	u8 PowerState = 0, awake_intvl = 1, byte5 = 0, rlbm = 0;

	if (pwrpriv->dtim > 0)
		DBG_871X("%s(): FW LPS mode = %d, SmartPS =%d, dtim =%d\n", __func__, psmode, pwrpriv->smart_ps, pwrpriv->dtim);
	else
		DBG_871X("%s(): FW LPS mode = %d, SmartPS =%d\n", __func__, psmode, pwrpriv->smart_ps);

#ifdef CONFIG_WOWLAN
	if (psmode == PS_MODE_DTIM) { /* For WOWLAN LPS, DTIM = (awake_intvl - 1) */
		awake_intvl = 3;/* DTIM =2 */
		rlbm = 2;
	} else
#endif /* CONFIG_WOWLAN */
	{
		if (pwrpriv->dtim > 0 && pwrpriv->dtim < 16)
			awake_intvl = pwrpriv->dtim+1;/* DTIM = (awake_intvl - 1) */
		else
			awake_intvl = 3;/* DTIM =2 */

		rlbm = 2;
	}


	if (padapter->registrypriv.wifi_spec == 1) {
		awake_intvl = 2;
		rlbm = 2;
	}

	if (psmode > 0) {
		if (rtw_btcoex_IsBtControlLps(padapter) == true) {
			PowerState = rtw_btcoex_RpwmVal(padapter);
			byte5 = rtw_btcoex_LpsVal(padapter);

			if ((rlbm == 2) && (byte5 & BIT(4))) {
				/*  Keep awake interval to 1 to prevent from */
				/*  decreasing coex performance */
				awake_intvl = 2;
				rlbm = 2;
			}
		} else {
			PowerState = 0x00;/*  AllON(0x0C), RFON(0x04), RFOFF(0x00) */
			byte5 = 0x40;
		}
	} else {
		PowerState = 0x0C;/*  AllON(0x0C), RFON(0x04), RFOFF(0x00) */
		byte5 = 0x40;
	}

	SET_8723B_H2CCMD_PWRMODE_PARM_MODE(u1H2CPwrModeParm, (psmode > 0) ? 1 : 0);
	SET_8723B_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CPwrModeParm, pwrpriv->smart_ps);
	SET_8723B_H2CCMD_PWRMODE_PARM_RLBM(u1H2CPwrModeParm, rlbm);
	SET_8723B_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CPwrModeParm, awake_intvl);
	SET_8723B_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CPwrModeParm, padapter->registrypriv.uapsd_enable);
	SET_8723B_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CPwrModeParm, PowerState);
	SET_8723B_H2CCMD_PWRMODE_PARM_BYTE5(u1H2CPwrModeParm, byte5);
	if (psmode != PS_MODE_ACTIVE) {
		if (pmlmeext->adaptive_tsf_done == false && pmlmeext->bcn_cnt > 0) {
			u8 ratio_20_delay, ratio_80_delay;

			/* byte 6 for adaptive_early_32k */
			/* 0:3] = DrvBcnEarly  (ms) , [4:7] = DrvBcnTimeOut  (ms) */
			/*  20% for DrvBcnEarly, 80% for DrvBcnTimeOut */
			ratio_20_delay = 0;
			ratio_80_delay = 0;
			pmlmeext->DrvBcnEarly = 0xff;
			pmlmeext->DrvBcnTimeOut = 0xff;

			DBG_871X("%s(): bcn_cnt = %d\n", __func__, pmlmeext->bcn_cnt);

			for (i = 0; i < 9; i++) {
				pmlmeext->bcn_delay_ratio[i] = (pmlmeext->bcn_delay_cnt[i]*100)/pmlmeext->bcn_cnt;

				DBG_871X(
					"%s(): bcn_delay_cnt[%d]=%d, bcn_delay_ratio[%d] = %d\n",
					__func__,
					i,
					pmlmeext->bcn_delay_cnt[i],
					i,
					pmlmeext->bcn_delay_ratio[i]
				);

				ratio_20_delay += pmlmeext->bcn_delay_ratio[i];
				ratio_80_delay += pmlmeext->bcn_delay_ratio[i];

				if (ratio_20_delay > 20 && pmlmeext->DrvBcnEarly == 0xff) {
					pmlmeext->DrvBcnEarly = i;
					DBG_871X("%s(): DrvBcnEarly = %d\n", __func__, pmlmeext->DrvBcnEarly);
				}

				if (ratio_80_delay > 80 && pmlmeext->DrvBcnTimeOut == 0xff) {
					pmlmeext->DrvBcnTimeOut = i;
					DBG_871X("%s(): DrvBcnTimeOut = %d\n", __func__, pmlmeext->DrvBcnTimeOut);
				}

				/* reset adaptive_early_32k cnt */
				pmlmeext->bcn_delay_cnt[i] = 0;
				pmlmeext->bcn_delay_ratio[i] = 0;

			}

			pmlmeext->bcn_cnt = 0;
			pmlmeext->adaptive_tsf_done = true;

		} else {
			DBG_871X("%s(): DrvBcnEarly = %d\n", __func__, pmlmeext->DrvBcnEarly);
			DBG_871X("%s(): DrvBcnTimeOut = %d\n", __func__, pmlmeext->DrvBcnTimeOut);
		}

/* offload to FW if fw version > v15.10
		pmlmeext->DrvBcnEarly = 0;
		pmlmeext->DrvBcnTimeOut =7;

		if ((pmlmeext->DrvBcnEarly!= 0Xff) && (pmlmeext->DrvBcnTimeOut!= 0xff))
			u1H2CPwrModeParm[H2C_PWRMODE_LEN-1] = BIT(0) | ((pmlmeext->DrvBcnEarly<<1)&0x0E) |((pmlmeext->DrvBcnTimeOut<<4)&0xf0) ;
*/

	}

	rtw_btcoex_RecordPwrMode(padapter, u1H2CPwrModeParm, H2C_PWRMODE_LEN);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CPwrModeParm:", u1H2CPwrModeParm, H2C_PWRMODE_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_SET_PWR_MODE, H2C_PWRMODE_LEN, u1H2CPwrModeParm);
}

void rtl8723b_set_FwPsTuneParam_cmd(struct adapter *padapter)
{
	u8 u1H2CPsTuneParm[H2C_PSTUNEPARAM_LEN] = {0};
	u8 bcn_to_limit = 10; /* 10 * 100 * awakeinterval (ms) */
	u8 dtim_timeout = 5; /* ms wait broadcast data timer */
	u8 ps_timeout = 20;  /* ms Keep awake when tx */
	u8 dtim_period = 3;

	/* DBG_871X("%s(): FW LPS mode = %d\n", __func__, psmode); */

	SET_8723B_H2CCMD_PSTUNE_PARM_BCN_TO_LIMIT(u1H2CPsTuneParm, bcn_to_limit);
	SET_8723B_H2CCMD_PSTUNE_PARM_DTIM_TIMEOUT(u1H2CPsTuneParm, dtim_timeout);
	SET_8723B_H2CCMD_PSTUNE_PARM_PS_TIMEOUT(u1H2CPsTuneParm, ps_timeout);
	SET_8723B_H2CCMD_PSTUNE_PARM_ADOPT(u1H2CPsTuneParm, 1);
	SET_8723B_H2CCMD_PSTUNE_PARM_DTIM_PERIOD(u1H2CPsTuneParm, dtim_period);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CPsTuneParm:", u1H2CPsTuneParm, H2C_PSTUNEPARAM_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_PS_TUNING_PARA, H2C_PSTUNEPARAM_LEN, u1H2CPsTuneParm);
}

void rtl8723b_set_FwPwrModeInIPS_cmd(struct adapter *padapter, u8 cmd_param)
{
	/* BIT0:enable, BIT1:NoConnect32k */

	DBG_871X("%s()\n", __func__);

	FillH2CCmd8723B(padapter, H2C_8723B_FWLPS_IN_IPS_, 1, &cmd_param);
}

#ifdef CONFIG_WOWLAN
static void rtl8723b_set_FwWoWlanCtrl_Cmd(struct adapter *padapter, u8 bFuncEn)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	u8 u1H2CWoWlanCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 discont_wake = 1, gpionum = 0, gpio_dur = 0, hw_unicast = 0;
	u8 sdio_wakeup_enable = 1;
	u8 gpio_high_active = 0; /* 0: low active, 1: high active */
	u8 magic_pkt = 0;

#ifdef CONFIG_GPIO_WAKEUP
	gpionum = WAKEUP_GPIO_IDX;
	sdio_wakeup_enable = 0;
#endif

#ifdef CONFIG_PNO_SUPPORT
	if (!ppwrpriv->wowlan_pno_enable)
		magic_pkt = 1;
#endif

	if (psecpriv->dot11PrivacyAlgrthm == _WEP40_ || psecpriv->dot11PrivacyAlgrthm == _WEP104_)
		hw_unicast = 1;

	DBG_871X("%s(): bFuncEn =%d\n", __func__, bFuncEn);

	SET_H2CCMD_WOWLAN_FUNC_ENABLE(u1H2CWoWlanCtrlParm, bFuncEn);
	SET_H2CCMD_WOWLAN_PATTERN_MATCH_ENABLE(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_MAGIC_PKT_ENABLE(u1H2CWoWlanCtrlParm, magic_pkt);
	SET_H2CCMD_WOWLAN_UNICAST_PKT_ENABLE(u1H2CWoWlanCtrlParm, hw_unicast);
	SET_H2CCMD_WOWLAN_ALL_PKT_DROP(u1H2CWoWlanCtrlParm, 0);
	SET_H2CCMD_WOWLAN_GPIO_ACTIVE(u1H2CWoWlanCtrlParm, gpio_high_active);
	SET_H2CCMD_WOWLAN_DISCONNECT_WAKE_UP(u1H2CWoWlanCtrlParm, discont_wake);
	SET_H2CCMD_WOWLAN_GPIONUM(u1H2CWoWlanCtrlParm, gpionum);
	SET_H2CCMD_WOWLAN_DATAPIN_WAKE_UP(u1H2CWoWlanCtrlParm, sdio_wakeup_enable);
	SET_H2CCMD_WOWLAN_GPIO_DURATION(u1H2CWoWlanCtrlParm, gpio_dur);
	/* SET_H2CCMD_WOWLAN_GPIO_PULSE_EN(u1H2CWoWlanCtrlParm, 1); */
	SET_H2CCMD_WOWLAN_GPIO_PULSE_COUNT(u1H2CWoWlanCtrlParm, 0x09);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CWoWlanCtrlParm:", u1H2CWoWlanCtrlParm, H2C_WOWLAN_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_WOWLAN, H2C_WOWLAN_LEN, u1H2CWoWlanCtrlParm);
}

static void rtl8723b_set_FwRemoteWakeCtrl_Cmd(struct adapter *padapter, u8 benable)
{
	u8 u1H2CRemoteWakeCtrlParm[H2C_REMOTE_WAKE_CTRL_LEN] = {0};
	struct security_priv *psecuritypriv = &(padapter->securitypriv);
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);

	DBG_871X("%s(): Enable =%d\n", __func__, benable);

	if (!ppwrpriv->wowlan_pno_enable) {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(u1H2CRemoteWakeCtrlParm, benable);
		SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 1);
#ifdef CONFIG_GTK_OL
		if (psecuritypriv->binstallKCK_KEK &&
		    psecuritypriv->dot11PrivacyAlgrthm == _AES_) {
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 1);
		} else {
			DBG_871X("no kck or security is not AES\n");
			SET_H2CCMD_REMOTE_WAKE_CTRL_GTK_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, 0);
		}
#endif /* CONFIG_GTK_OL */

		SET_H2CCMD_REMOTE_WAKE_CTRL_FW_UNICAST_EN(u1H2CRemoteWakeCtrlParm, 1);

		if ((psecuritypriv->dot11PrivacyAlgrthm == _AES_) ||
		    (psecuritypriv->dot11PrivacyAlgrthm == _NO_PRIVACY_))
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(u1H2CRemoteWakeCtrlParm, 0);
		else
			SET_H2CCMD_REMOTE_WAKE_CTRL_ARP_ACTION(u1H2CRemoteWakeCtrlParm, 1);
	}
#ifdef CONFIG_PNO_SUPPORT
	else {
		SET_H2CCMD_REMOTE_WAKECTRL_ENABLE(u1H2CRemoteWakeCtrlParm, benable);
		SET_H2CCMD_REMOTE_WAKE_CTRL_NLO_OFFLOAD_EN(u1H2CRemoteWakeCtrlParm, benable);
	}
#endif
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CRemoteWakeCtrlParm:", u1H2CRemoteWakeCtrlParm, H2C_REMOTE_WAKE_CTRL_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_REMOTE_WAKE_CTRL,
		H2C_REMOTE_WAKE_CTRL_LEN, u1H2CRemoteWakeCtrlParm);
#ifdef CONFIG_PNO_SUPPORT
	if (ppwrpriv->wowlan_pno_enable && ppwrpriv->pno_in_resume == false) {
		res = rtw_read8(padapter, REG_PNO_STATUS);
		DBG_871X("cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", res);
		while (!(res&BIT(7)) && count < 25) {
			DBG_871X("[%d] cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", count, res);
			res = rtw_read8(padapter, REG_PNO_STATUS);
			count++;
			msleep(2);
		}
		DBG_871X("cmd: 0x81 REG_PNO_STATUS: 0x%02x\n", res);
	}
#endif /* CONFIG_PNO_SUPPORT */
}

static void rtl8723b_set_FwAOACGlobalInfo_Cmd(struct adapter *padapter,  u8 group_alg, u8 pairwise_alg)
{
	u8 u1H2CAOACGlobalInfoParm[H2C_AOAC_GLOBAL_INFO_LEN] = {0};

	DBG_871X("%s(): group_alg =%d pairwise_alg =%d\n", __func__, group_alg, pairwise_alg);

	SET_H2CCMD_AOAC_GLOBAL_INFO_PAIRWISE_ENC_ALG(u1H2CAOACGlobalInfoParm, pairwise_alg);
	SET_H2CCMD_AOAC_GLOBAL_INFO_GROUP_ENC_ALG(u1H2CAOACGlobalInfoParm, group_alg);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CAOACGlobalInfoParm:", u1H2CAOACGlobalInfoParm, H2C_AOAC_GLOBAL_INFO_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_AOAC_GLOBAL_INFO, H2C_AOAC_GLOBAL_INFO_LEN, u1H2CAOACGlobalInfoParm);
}

#ifdef CONFIG_PNO_SUPPORT
static void rtl8723b_set_FwScanOffloadInfo_cmd(struct adapter *padapter, PRSVDPAGE_LOC rsvdpageloc, u8 enable)
{
	u8 u1H2CScanOffloadInfoParm[H2C_SCAN_OFFLOAD_CTRL_LEN] = {0};
	u8 res = 0, count = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	DBG_871X("%s: loc_probe_packet:%d, loc_scan_info: %d loc_ssid_info:%d\n",
		__func__, rsvdpageloc->LocProbePacket, rsvdpageloc->LocScanInfo, rsvdpageloc->LocSSIDInfo);

	SET_H2CCMD_AOAC_NLO_FUN_EN(u1H2CScanOffloadInfoParm, enable);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_SCAN_INFO(u1H2CScanOffloadInfoParm, rsvdpageloc->LocScanInfo);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_PROBE_PACKET(u1H2CScanOffloadInfoParm, rsvdpageloc->LocProbePacket);
	SET_H2CCMD_AOAC_RSVDPAGE_LOC_SSID_INFO(u1H2CScanOffloadInfoParm, rsvdpageloc->LocSSIDInfo);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CScanOffloadInfoParm:", u1H2CScanOffloadInfoParm, H2C_SCAN_OFFLOAD_CTRL_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_D0_SCAN_OFFLOAD_INFO, H2C_SCAN_OFFLOAD_CTRL_LEN, u1H2CScanOffloadInfoParm);

	msleep(20);
}
#endif /* CONFIG_PNO_SUPPORT */

static void rtl8723b_set_FwWoWlanRelated_cmd(struct adapter *padapter, u8 enable)
{
	struct security_priv *psecpriv = &padapter->securitypriv;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sta_info *psta = NULL;
	u8 pkt_type = 0;

	DBG_871X_LEVEL(_drv_always_, "+%s()+: enable =%d\n", __func__, enable);
	if (enable) {
		rtl8723b_set_FwAOACGlobalInfo_Cmd(padapter, psecpriv->dot118021XGrpPrivacy, psecpriv->dot11PrivacyAlgrthm);

		rtl8723b_set_FwJoinBssRpt_cmd(padapter, RT_MEDIA_CONNECT);	/* RT_MEDIA_CONNECT will confuse in the future */

		if (!(ppwrpriv->wowlan_pno_enable)) {
			psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(pmlmepriv));
			if (psta != NULL)
				rtl8723b_set_FwMediaStatusRpt_cmd(padapter, RT_MEDIA_CONNECT, psta->mac_id);
		} else
			DBG_871X("%s(): Disconnected, no FwMediaStatusRpt CONNECT\n", __func__);

		msleep(2);

		if (!(ppwrpriv->wowlan_pno_enable)) {
			rtl8723b_set_FwDisconDecision_cmd(padapter, enable);
			msleep(2);

			if ((psecpriv->dot11PrivacyAlgrthm != _WEP40_) || (psecpriv->dot11PrivacyAlgrthm != _WEP104_))
				pkt_type = 1;

			rtl8723b_set_FwKeepAlive_cmd(padapter, enable, pkt_type);
			msleep(2);
		}

		rtl8723b_set_FwWoWlanCtrl_Cmd(padapter, enable);
		msleep(2);

		rtl8723b_set_FwRemoteWakeCtrl_Cmd(padapter, enable);
	} else {
		rtl8723b_set_FwRemoteWakeCtrl_Cmd(padapter, enable);
		msleep(2);
		rtl8723b_set_FwWoWlanCtrl_Cmd(padapter, enable);
	}

	DBG_871X_LEVEL(_drv_always_, "-%s()-\n", __func__);
}

void rtl8723b_set_wowlan_cmd(struct adapter *padapter, u8 enable)
{
	rtl8723b_set_FwWoWlanRelated_cmd(padapter, enable);
}
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_AP_WOWLAN
static void rtl8723b_set_FwAPWoWlanCtrl_Cmd(struct adapter *padapter, u8 bFuncEn)
{
	u8 u1H2CAPWoWlanCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 gpionum = 0, gpio_dur = 0;
	u8 gpio_high_active = 1; /* 0: low active, 1: high active */
	u8 gpio_pulse = bFuncEn;
#ifdef CONFIG_GPIO_WAKEUP
	gpionum = WAKEUP_GPIO_IDX;
#endif

	DBG_871X("%s(): bFuncEn =%d\n", __func__, bFuncEn);

	if (bFuncEn)
		gpio_dur = 16;
	else
		gpio_dur = 0;

	SET_H2CCMD_AP_WOW_GPIO_CTRL_INDEX(u1H2CAPWoWlanCtrlParm,
			gpionum);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_PLUS(u1H2CAPWoWlanCtrlParm,
			gpio_pulse);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_HIGH_ACTIVE(u1H2CAPWoWlanCtrlParm,
			gpio_high_active);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_EN(u1H2CAPWoWlanCtrlParm,
			bFuncEn);
	SET_H2CCMD_AP_WOW_GPIO_CTRL_DURATION(u1H2CAPWoWlanCtrlParm,
			gpio_dur);

	FillH2CCmd8723B(padapter, H2C_8723B_AP_WOW_GPIO_CTRL,
			H2C_AP_WOW_GPIO_CTRL_LEN, u1H2CAPWoWlanCtrlParm);
}

static void rtl8723b_set_Fw_AP_Offload_Cmd(struct adapter *padapter, u8 bFuncEn)
{
	u8 u1H2CAPOffloadCtrlParm[H2C_WOWLAN_LEN] = {0};

	DBG_871X("%s(): bFuncEn =%d\n", __func__, bFuncEn);

	SET_H2CCMD_AP_WOWLAN_EN(u1H2CAPOffloadCtrlParm, bFuncEn);

	FillH2CCmd8723B(padapter, H2C_8723B_AP_OFFLOAD,
			H2C_AP_OFFLOAD_LEN, u1H2CAPOffloadCtrlParm);
}

static void rtl8723b_set_AP_FwWoWlan_cmd(struct adapter *padapter, u8 enable)
{
	DBG_871X_LEVEL(_drv_always_, "+%s()+: enable =%d\n", __func__, enable);
	if (enable) {
		rtl8723b_set_FwJoinBssRpt_cmd(padapter, RT_MEDIA_CONNECT);
		issue_beacon(padapter, 0);
	}

	rtl8723b_set_FwAPWoWlanCtrl_Cmd(padapter, enable);
	msleep(10);
	rtl8723b_set_Fw_AP_Offload_Cmd(padapter, enable);
	msleep(10);
	DBG_871X_LEVEL(_drv_always_, "-%s()-\n", __func__);
	return ;
}

void rtl8723b_set_ap_wowlan_cmd(struct adapter *padapter, u8 enable)
{
	rtl8723b_set_AP_FwWoWlan_cmd(padapter, enable);
}
#endif /* CONFIG_AP_WOWLAN */

/*  */
/*  Description: Fill the reserved packets that FW will use to RSVD page. */
/* 			Now we just send 4 types packet to rsvd page. */
/* 			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp. */
/* 	Input: */
/* 	    bDLFinished - false: At the first time we will send all the packets as a large packet to Hw, */
/* 						so we need to set the packet length to total lengh. */
/* 			      true: At the second time, we should send the first packet (default:beacon) */
/* 						to Hw again and set the lengh in descriptor to the real beacon lengh. */
/*  2009.10.15 by tynli. */
static void rtl8723b_set_FwRsvdPagePkt(
	struct adapter *padapter, bool bDLFinished
)
{
	struct hal_com_data *pHalData;
	struct xmit_frame *pcmdframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u32 BeaconLength = 0, PSPollLength = 0;
	u32 NullDataLength = 0, QosNullLength = 0, BTQosNullLength = 0;
	u8 *ReservedPagePacket;
	u8 TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8 TotalPageNum = 0, CurtPktPageNum = 0, RsvdPageNum = 0;
	u16 BufIndex, PageSize = 128;
	u32 TotalPacketLen, MaxRsvdPageBufSize = 0;
	RSVDPAGE_LOC RsvdPageLoc;
#ifdef CONFIG_WOWLAN
	u32 ARPLegnth = 0, GTKLegnth = 0;
	u8 currentip[4];
	u8 cur_dot11txpn[8];
#ifdef CONFIG_GTK_OL
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta;
	u8 kek[RTW_KEK_LEN];
	u8 kck[RTW_KCK_LEN];
#endif
#endif

	/* DBG_871X("%s---->\n", __func__); */

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);

	RsvdPageNum = BCNQ_PAGE_NUM_8723B + WOWLAN_PAGE_NUM_8723B;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	/* 3 (1) beacon */
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	/*  When we count the first page size, we need to reserve description size for the RSVD */
	/*  packet, it will be filled in front of the packet in TXPKTBUF. */
	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BeaconLength);
	/* If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware */
	if (CurtPktPageNum == 1)
		CurtPktPageNum += 1;

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3 (2) ps-poll */
	RsvdPageLoc.LocPsPoll = TotalPageNum;
	ConstructPSPoll(padapter, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, true, false, false);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: PS-POLL %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (PSPollLength+TxDescLen)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + PSPollLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3 (3) null data */
	RsvdPageLoc.LocNullData = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&NullDataLength,
		get_my_bssid(&pmlmeinfo->network),
		false, 0, 0, false
	);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, false, false, false);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: NULL DATA %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (NullDataLength+TxDescLen)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + NullDataLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3 (5) Qos null data */
	RsvdPageLoc.LocQosNull = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&QosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		true, 0, 0, false
	);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, false, false, false);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: QOS NULL DATA %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (QosNullLength+TxDescLen)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + QosNullLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3 (6) BT Qos null data */
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		get_my_bssid(&pmlmeinfo->network),
		true, 0, 0, false
	);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, false, true, false);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: BT QOS NULL DATA %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (BTQosNullLength+TxDescLen)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BTQosNullLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

#ifdef CONFIG_WOWLAN
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
	/* if (pwrctl->wowlan_mode == true) { */
		/* BufIndex += (CurtPktPageNum*PageSize); */

	/* 3(7) ARP RSP */
	rtw_get_current_ip_address(padapter, currentip);
	RsvdPageLoc.LocArpRsp = TotalPageNum;
	{
	ConstructARPResponse(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ARPLegnth,
		currentip
		);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], ARPLegnth, false, false, true);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: ARP RSP %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (ARPLegnth+TxDescLen)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + ARPLegnth);
	}
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3(8) SEC IV */
	rtw_get_sec_iv(padapter, cur_dot11txpn, get_my_bssid(&pmlmeinfo->network));
	RsvdPageLoc.LocRemoteCtrlInfo = TotalPageNum;
	memcpy(ReservedPagePacket+BufIndex-TxDescLen, cur_dot11txpn, _AES_IV_LEN_);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: SEC IV %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], _AES_IV_LEN_); */

	CurtPktPageNum = (u8)PageNum_128(_AES_IV_LEN_);

	TotalPageNum += CurtPktPageNum;

#ifdef CONFIG_GTK_OL
	BufIndex += (CurtPktPageNum*PageSize);

	/* if the ap staion info. exists, get the kek, kck from staion info. */
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta == NULL) {
		memset(kek, 0, RTW_KEK_LEN);
		memset(kck, 0, RTW_KCK_LEN);
		DBG_8192C("%s, KEK, KCK download rsvd page all zero\n", __func__);
	} else {
		memcpy(kek, psta->kek, RTW_KEK_LEN);
		memcpy(kck, psta->kck, RTW_KCK_LEN);
	}

	/* 3(9) KEK, KCK */
	RsvdPageLoc.LocGTKInfo = TotalPageNum;
	memcpy(ReservedPagePacket+BufIndex-TxDescLen, kck, RTW_KCK_LEN);
	memcpy(ReservedPagePacket+BufIndex-TxDescLen+RTW_KCK_LEN, kek, RTW_KEK_LEN);

	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: KEK KCK %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + RTW_KCK_LEN + RTW_KEK_LEN);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 3(10) GTK Response */
	RsvdPageLoc.LocGTKRsp = TotalPageNum;
	ConstructGTKResponse(
		padapter,
		&ReservedPagePacket[BufIndex],
		&GTKLegnth
	);

	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], GTKLegnth, false, false, true);
	/* DBG_871X("%s(): HW_VAR_SET_TX_CMD: GTK RSP %p %d\n", */
	/* 	__func__, &ReservedPagePacket[BufIndex-TxDescLen], (TxDescLen + GTKLegnth)); */

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + GTKLegnth);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* below page is empty for GTK extension memory */
	/* 3(11) GTK EXT MEM */
	RsvdPageLoc.LocGTKEXTMEM = TotalPageNum;

	CurtPktPageNum = 2;

	TotalPageNum += CurtPktPageNum;

	TotalPacketLen = BufIndex-TxDescLen + 256; /* extension memory for FW */
#else
	TotalPacketLen = BufIndex-TxDescLen + sizeof (union pn48); /* IV len */
#endif /* CONFIG_GTK_OL */
	} else
#endif /* CONFIG_WOWLAN */
	{
#ifdef CONFIG_PNO_SUPPORT
		if (pwrctl->pno_in_resume == false && pwrctl->pno_inited == true) {
			/* Probe Request */
			RsvdPageLoc.LocProbePacket = TotalPageNum;
			ConstructProbeReq(
				padapter,
				&ReservedPagePacket[BufIndex],
				&ProbeReqLength);

			rtl8723b_fill_fake_txdesc(padapter,
				&ReservedPagePacket[BufIndex-TxDescLen],
				ProbeReqLength, false, false, false);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("probe req pkt =>\n");
			for (gj = 0; gj < ProbeReqLength+TxDescLen; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj+1)%8 == 0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum =
				(u8)PageNum_128(TxDescLen + ProbeReqLength);

			TotalPageNum += CurtPktPageNum;

			BufIndex += (CurtPktPageNum*PageSize);

			/* PNO INFO Page */
			RsvdPageLoc.LocPNOInfo = TotalPageNum;
			ConstructPnoInfo(padapter, &ReservedPagePacket[BufIndex-TxDescLen], &PNOLength);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("PNO pkt =>\n");
			for (gj = 0; gj < PNOLength; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%8 == 0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif

			CurtPktPageNum = (u8)PageNum_128(PNOLength);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			/* SSID List Page */
			RsvdPageLoc.LocSSIDInfo = TotalPageNum;
			ConstructSSIDList(padapter, &ReservedPagePacket[BufIndex-TxDescLen], &SSIDLegnth);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("SSID list pkt =>\n");
			for (gj = 0; gj < SSIDLegnth; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%8 == 0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum = (u8)PageNum_128(SSIDLegnth);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			/* Scan Info Page */
			RsvdPageLoc.LocScanInfo = TotalPageNum;
			ConstructScanInfo(padapter, &ReservedPagePacket[BufIndex-TxDescLen], &ScanInfoLength);
#ifdef CONFIG_PNO_SET_DEBUG
	{
			int gj;
			printk("Scan info pkt =>\n");
			for (gj = 0; gj < ScanInfoLength; gj++) {
				printk(" %02x ", ReservedPagePacket[BufIndex-TxDescLen+gj]);
				if ((gj + 1)%8 == 0)
					printk("\n");
			}
			printk(" <=end\n");
	}
#endif
			CurtPktPageNum = (u8)PageNum_128(ScanInfoLength);
			TotalPageNum += CurtPktPageNum;
			BufIndex += (CurtPktPageNum*PageSize);

			TotalPacketLen = BufIndex + ScanInfoLength;
		} else {
		TotalPacketLen = BufIndex + BTQosNullLength;
	}
#else /* CONFIG_PNO_SUPPORT */
		TotalPacketLen = BufIndex + BTQosNullLength;
#endif
	}

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(): ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n", __func__,
			TotalPacketLen, MaxRsvdPageBufSize);
		goto error;
	} else {
		/*  update attribute */
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = 0x10;
		pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
	}

	DBG_871X("%s: Set RSVD page location to Fw , TotalPacketLen(%d), TotalPageNum(%d)\n", __func__, TotalPacketLen, TotalPageNum);
	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		rtl8723b_set_FwRsvdPage_cmd(padapter, &RsvdPageLoc);
		rtl8723b_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);
	} else {
		rtl8723b_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);
#ifdef CONFIG_PNO_SUPPORT
		if (pwrctl->pno_in_resume)
			rtl8723b_set_FwScanOffloadInfo_cmd(padapter,
					&RsvdPageLoc, 0);
		else
			rtl8723b_set_FwScanOffloadInfo_cmd(padapter,
					&RsvdPageLoc, 1);
#endif
	}
	return;

error:

	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

#ifdef CONFIG_AP_WOWLAN
/*  */
/* Description: Fill the reserved packets that FW will use to RSVD page. */
/* Now we just send 2 types packet to rsvd page. (1)Beacon, (2)ProbeRsp. */
/*  */
/* Input: bDLFinished */
/*  */
/* false: At the first time we will send all the packets as a large packet to Hw, */
/* 	 so we need to set the packet length to total lengh. */
/*  */
/* true: At the second time, we should send the first packet (default:beacon) */
/* 	to Hw again and set the lengh in descriptor to the real beacon lengh. */
/*  2009.10.15 by tynli. */
static void rtl8723b_set_AP_FwRsvdPagePkt(
	struct adapter *padapter, bool bDLFinished
)
{
	struct hal_com_data *pHalData;
	struct xmit_frame *pcmdframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	struct pwrctrl_priv *pwrctl;
	u32 BeaconLength = 0, ProbeRspLength = 0;
	u8 *ReservedPagePacket;
	u8 TxDescLen = TXDESC_SIZE, TxDescOffset = TXDESC_OFFSET;
	u8 TotalPageNum = 0, CurtPktPageNum = 0, RsvdPageNum = 0;
	u8 currentip[4];
	u16 BufIndex, PageSize = 128;
	u32 TotalPacketLen = 0, MaxRsvdPageBufSize = 0;
	RSVDPAGE_LOC RsvdPageLoc;

	/* DBG_871X("%s---->\n", __func__); */
	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type =%d\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter));

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);

	RsvdPageNum = BCNQ_PAGE_NUM_8723B + AP_WOWLAN_PAGE_NUM_8723B;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	/* 3 (1) beacon */
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	/*  When we count the first page size, we need to reserve description size for the RSVD */
	/*  packet, it will be filled in front of the packet in TXPKTBUF. */
	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BeaconLength);
	/* If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware */
	if (CurtPktPageNum == 1)
		CurtPktPageNum += 1;
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/* 2 (4) probe response */
	RsvdPageLoc.LocProbeRsp = TotalPageNum;

	rtw_get_current_ip_address(padapter, currentip);

	ConstructProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		currentip,
		false);
	rtl8723b_fill_fake_txdesc(padapter,
			&ReservedPagePacket[BufIndex-TxDescLen],
			ProbeRspLength,
			false, false, false);

	DBG_871X("%s(): HW_VAR_SET_TX_CMD: PROBE RSP %p %d\n",
		__func__, &ReservedPagePacket[BufIndex-TxDescLen],
		(ProbeRspLength+TxDescLen));

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + ProbeRspLength);

	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	TotalPacketLen = BufIndex + ProbeRspLength;

	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_871X("%s(): ERROR: The rsvd page size is not enough \
				!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
				__func__, TotalPacketLen, MaxRsvdPageBufSize);
		goto error;
	} else {
		/*  update attribute */
		pattrib = &pcmdframe->attrib;
		update_mgntframe_attrib(padapter, pattrib);
		pattrib->qsel = 0x10;
		pattrib->pktlen = TotalPacketLen - TxDescOffset;
		pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
		dump_mgntframe_and_wait(padapter, pcmdframe, 100);
	}

	DBG_871X("%s: Set RSVD page location to Fw , TotalPacketLen(%d), TotalPageNum(%d)\n", __func__, TotalPacketLen, TotalPageNum);
	rtl8723b_set_ap_wow_rsvdpage_cmd(padapter, &RsvdPageLoc);

	return;
error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}
#endif /* CONFIG_AP_WOWLAN */

void rtl8723b_download_rsvd_page(struct adapter *padapter, u8 mstatus)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
#ifdef CONFIG_AP_WOWLAN
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
#endif
	struct mlme_ext_priv *pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	bool bcn_valid = false;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8;

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type =%d mstatus(%x)\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), mstatus);

	if (mstatus == RT_MEDIA_CONNECT) {
		bool bRecover = false;
		u8 v8;

		/*  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		/*  Suggested by filen. Added by tynli. */
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));

		/*  set REG_CR bit 8 */
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 |= BIT(0); /*  ENSWBCN */
		rtw_write8(padapter, REG_CR+1, v8);

		/*  Disable Hw protection for a time which revserd for Hw sending beacon. */
		/*  Fix download reserved page packet fail that access collision with the protection time. */
		/*  2010.05.11. Added by tynli. */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~EN_BCN_FUNCTION;
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		/*  Set FWHW_TXQ_CTRL 0x422[6]= 0 to tell Hw the packet is not a real beacon frame. */
		if (pHalData->RegFwHwTxQCtrl & BIT(6))
			bRecover = true;

		/*  To tell Hw the packet is not a real beacon frame. */
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl & ~BIT(6));
		pHalData->RegFwHwTxQCtrl &= ~BIT(6);

		/*  Clear beacon valid check bit. */
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do {
#ifdef CONFIG_AP_WOWLAN
			if (pwrpriv->wowlan_ap_mode)
				rtl8723b_set_AP_FwRsvdPagePkt(padapter, 0);
			else
				rtl8723b_set_FwRsvdPagePkt(padapter, 0);
#else
			/*  download rsvd page. */
			rtl8723b_set_FwRsvdPagePkt(padapter, 0);
#endif
			DLBcnCount++;
			do {
				yield();
				/* mdelay(10); */
				/*  check rsvd page download OK. */
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8 *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll%10) != 0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

		} while (!bcn_valid && DLBcnCount <= 100 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

		if (padapter->bSurpriseRemoved || padapter->bDriverStopped) {
		} else if (!bcn_valid)
			DBG_871X(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			DBG_871X(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		}

		/*  2010.05.11. Added by tynli. */
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= EN_BCN_FUNCTION;
		val8 &= ~DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		/*  To make sure that if there exists an adapter which would like to send beacon. */
		/*  If exists, the origianl value of 0x422[6] will be 1, we should check this to */
		/*  prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
		/*  the beacon cannot be sent by HW. */
		/*  2010.06.23. Added by tynli. */
		if (bRecover) {
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl | BIT(6));
			pHalData->RegFwHwTxQCtrl |= BIT(6);
		}

		/*  Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 &= ~BIT(0); /*  ~ENSWBCN */
		rtw_write8(padapter, REG_CR+1, v8);
	}
}

void rtl8723b_set_rssi_cmd(struct adapter *padapter, u8 *param)
{
	rtl8723b_set_FwRssiSetting_cmd(padapter, param);
}

void rtl8723b_set_FwJoinBssRpt_cmd(struct adapter *padapter, u8 mstatus)
{
	if (mstatus == 1)
		rtl8723b_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
}

/* arg[0] = macid */
/* arg[1] = raid */
/* arg[2] = shortGIrate */
/* arg[3] = init_rate */
void rtl8723b_Add_RateATid(
	struct adapter *padapter,
	u32 bitmap,
	u8 *arg,
	u8 rssi_level
)
{
	struct hal_com_data	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);
	struct sta_info *psta;
	u8 mac_id = arg[0];
	u8 raid = arg[1];
	u8 shortGI = arg[2];
	u8 bw;
	u32 mask = bitmap&0x0FFFFFFF;

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (psta == NULL)
		return;

	bw = psta->bw_mode;

	if (rssi_level != DM_RATR_STA_INIT)
		mask = ODM_Get_Rate_Bitmap(&pHalData->odmpriv, mac_id, mask, rssi_level);

	DBG_871X("%s(): mac_id =%d raid = 0x%x bw =%d mask = 0x%x\n", __func__, mac_id, raid, bw, mask);
	rtl8723b_set_FwMacIdConfig_cmd(padapter, mac_id, raid, bw, shortGI, mask);
}

static void ConstructBtNullFunctionData(
	struct adapter *padapter,
	u8 *pframe,
	u32 *pLength,
	u8 *StaAddr,
	u8 bQoS,
	u8 AC,
	u8 bEosp,
	u8 bForcePowerSave
)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u8 bssid[ETH_ALEN];


	DBG_871X("+" FUNC_ADPT_FMT ": qos =%d eosp =%d ps =%d\n",
		FUNC_ADPT_ARG(padapter), bQoS, bEosp, bForcePowerSave);

	pwlanhdr = (struct ieee80211_hdr *)pframe;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if (NULL == StaAddr) {
		memcpy(bssid, myid(&padapter->eeprompriv), ETH_ALEN);
		StaAddr = bssid;
	}

	fctrl = &pwlanhdr->frame_control;
	*fctrl = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	SetFrDs(fctrl);
	memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
	memcpy(pwlanhdr->addr3, myid(&padapter->eeprompriv), ETH_ALEN);

	SetDuration(pwlanhdr, 0);
	SetSeqNum(pwlanhdr, 0);

	if (bQoS == true) {
		struct ieee80211_qos_hdr *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct ieee80211_qos_hdr *)pframe;
		SetPriority(&pwlanqoshdr->qos_ctrl, AC);
		SetEOSP(&pwlanqoshdr->qos_ctrl, bEosp);

		pktlen = sizeof(struct ieee80211_qos_hdr);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

static void SetFwRsvdPagePkt_BTCoex(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	struct xmit_frame *pcmdframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u32 BeaconLength = 0;
	u32 BTQosNullLength = 0;
	u8 *ReservedPagePacket;
	u8 TxDescLen, TxDescOffset;
	u8 TotalPageNum = 0, CurtPktPageNum = 0, RsvdPageNum = 0;
	u16 BufIndex, PageSize;
	u32 TotalPacketLen, MaxRsvdPageBufSize = 0;
	RSVDPAGE_LOC RsvdPageLoc;


/* 	DBG_8192C("+" FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(padapter)); */

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	TxDescLen = TXDESC_SIZE;
	TxDescOffset = TXDESC_OFFSET;
	PageSize = PAGE_SIZE_TX_8723B;

	RsvdPageNum = BCNQ_PAGE_NUM_8723B;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (pcmdframe == NULL) {
		DBG_8192C("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	/* 3 (1) beacon */
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	/*  When we count the first page size, we need to reserve description size for the RSVD */
	/*  packet, it will be filled in front of the packet in TXPKTBUF. */
	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BeaconLength);
	/* If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware */
	if (CurtPktPageNum == 1)
		CurtPktPageNum += 1;
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	/*  Jump to lastest page */
	if (BufIndex < (MaxRsvdPageBufSize - PageSize)) {
		BufIndex = TxDescOffset + (MaxRsvdPageBufSize - PageSize);
		TotalPageNum = BCNQ_PAGE_NUM_8723B - 1;
	}

	/* 3 (6) BT Qos null data */
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	ConstructBtNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		NULL,
		true, 0, 0, false
	);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, false, true, false);

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BTQosNullLength);

	TotalPageNum += CurtPktPageNum;

	TotalPacketLen = BufIndex + BTQosNullLength;
	if (TotalPacketLen > MaxRsvdPageBufSize) {
		DBG_8192C(FUNC_ADPT_FMT ": ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
			FUNC_ADPT_ARG(padapter), TotalPacketLen, MaxRsvdPageBufSize);
		goto error;
	}

	/*  update attribute */
	pattrib = &pcmdframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = 0x10;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
	dump_mgntframe_and_wait(padapter, pcmdframe, 100);

/* 	DBG_8192C(FUNC_ADPT_FMT ": Set RSVD page location to Fw, TotalPacketLen(%d), TotalPageNum(%d)\n", */
/* 		FUNC_ADPT_ARG(padapter), TotalPacketLen, TotalPageNum); */
	rtl8723b_set_FwRsvdPage_cmd(padapter, &RsvdPageLoc);
	rtl8723b_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);

	return;

error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

void rtl8723b_download_BTCoex_AP_mode_rsvd_page(struct adapter *padapter)
{
	struct hal_com_data *pHalData;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u8 bRecover = false;
	u8 bcn_valid = false;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8;


	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type =%d fw_state = 0x%08X\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), get_fwstate(&padapter->mlmepriv));

#ifdef DEBUG
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == false) {
		DBG_8192C(FUNC_ADPT_FMT ": [WARNING] not in AP mode!!\n",
			FUNC_ADPT_ARG(padapter));
	}
#endif /*  DEBUG */

	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	/*  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
	/*  Suggested by filen. Added by tynli. */
	rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));

	/*  set REG_CR bit 8 */
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 |= BIT(0); /*  ENSWBCN */
	rtw_write8(padapter,  REG_CR+1, val8);

	/*  Disable Hw protection for a time which revserd for Hw sending beacon. */
	/*  Fix download reserved page packet fail that access collision with the protection time. */
	/*  2010.05.11. Added by tynli. */
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 &= ~EN_BCN_FUNCTION;
	val8 |= DIS_TSF_UDT;
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	/*  Set FWHW_TXQ_CTRL 0x422[6]= 0 to tell Hw the packet is not a real beacon frame. */
	if (pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover = true;

	/*  To tell Hw the packet is not a real beacon frame. */
	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);

	/*  Clear beacon valid check bit. */
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

	DLBcnCount = 0;
	poll = 0;
	do {
		SetFwRsvdPagePkt_BTCoex(padapter);
		DLBcnCount++;
		do {
			yield();
/* 			mdelay(10); */
			/*  check rsvd page download OK. */
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, &bcn_valid);
			poll++;
		} while (!bcn_valid && (poll%10) != 0 && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);
	} while (!bcn_valid && (DLBcnCount <= 100) && !padapter->bSurpriseRemoved && !padapter->bDriverStopped);

	if (true == bcn_valid) {
		struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
		pwrctl->fw_psmode_iface_id = padapter->iface_id;
		DBG_8192C(ADPT_FMT": DL RSVD page success! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
	} else {
		DBG_8192C(ADPT_FMT": DL RSVD page fail! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
		DBG_8192C(ADPT_FMT": DL RSVD page fail! bSurpriseRemoved =%d\n",
			ADPT_ARG(padapter), padapter->bSurpriseRemoved);
		DBG_8192C(ADPT_FMT": DL RSVD page fail! bDriverStopped =%d\n",
			ADPT_ARG(padapter), padapter->bDriverStopped);
	}

	/*  2010.05.11. Added by tynli. */
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 |= EN_BCN_FUNCTION;
	val8 &= ~DIS_TSF_UDT;
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	/*  To make sure that if there exists an adapter which would like to send beacon. */
	/*  If exists, the origianl value of 0x422[6] will be 1, we should check this to */
	/*  prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/*  the beacon cannot be sent by HW. */
	/*  2010.06.23. Added by tynli. */
	if (bRecover) {
		pHalData->RegFwHwTxQCtrl |= BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	}

	/*  Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 &= ~BIT(0); /*  ~ENSWBCN */
	rtw_write8(padapter, REG_CR+1, val8);
}
