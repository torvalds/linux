// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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

	if (padapter->bSurpriseRemoved)
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
	pktlen = sizeof(struct ieee80211_hdr_3addr);

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
	pframe = rtw_set_ie(pframe, WLAN_EID_SSID, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, WLAN_EID_SUPP_RATES, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, WLAN_EID_DS_PARAMS, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, WLAN_EID_IBSS_PARAMS, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}


	/* todo: ERP IE */


	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie(pframe, WLAN_EID_EXT_SUPP_RATES, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);


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

	if (bQoS) {
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

	DBG_871X("%s FW Mac Addr:%pM\n", __func__, MAC_ARG(mac));
	DBG_871X("%s FW IP Addr:%pI4\n", __func__, IP_ARG(StaAddr));

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct ieee80211_hdr_3addr);
	pframe += pktlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

	pwps_ie = rtw_get_wps_ie(cur_network->IEs+_FIXED_IE_LENGTH_,
			cur_network->IELength-_FIXED_IE_LENGTH_, NULL, &wps_ielen);

	/* inerset & update wps_probe_resp_ie */
	if (pmlmepriv->wps_probe_resp_ie && pwps_ie && (wps_ielen > 0)) {
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
		signed int ssid_ielen;
		signed int ssid_ielen_diff;
		u8 buf[MAX_IE_SZ];
		u8 *ies = pframe + sizeof(struct ieee80211_hdr_3addr);

		ssid_ie = rtw_get_ie(ies+_FIXED_IE_LENGTH_, WLAN_EID_SSID, &ssid_ielen,
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

/*
 * To check if reserved page content is destroyed by beacon because beacon
 * is too large.
 */
/* 2010.06.23. Added by tynli. */
void CheckFwRsvdPageContent(struct adapter *Adapter)
{
}

static void rtl8723b_set_FwRsvdPage_cmd(struct adapter *padapter, struct RSVDPAGE_LOC *rsvdpageloc)
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

static void rtl8723b_set_FwAoacRsvdPage_cmd(struct adapter *padapter, struct RSVDPAGE_LOC *rsvdpageloc)
{
}

#ifdef CONFIG_AP_WOWLAN
static void rtl8723b_set_ap_wow_rsvdpage_cmd(
	struct adapter *padapter, struct RSVDPAGE_LOC *rsvdpageloc
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

void rtl8723b_set_rssi_cmd(struct adapter *padapter, u8 *param)
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

	if (pwrpriv->dtim > 0 && pwrpriv->dtim < 16)
		awake_intvl = pwrpriv->dtim+1;/* DTIM = (awake_intvl - 1) */
	else
		awake_intvl = 3;/* DTIM =2 */

	rlbm = 2;

	if (padapter->registrypriv.wifi_spec == 1) {
		awake_intvl = 2;
		rlbm = 2;
	}

	if (psmode > 0) {
		if (hal_btcoex_IsBtControlLps(padapter) == true) {
			PowerState = hal_btcoex_RpwmVal(padapter);
			byte5 = hal_btcoex_LpsVal(padapter);

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
		if (!pmlmeext->adaptive_tsf_done && pmlmeext->bcn_cnt > 0) {
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

	hal_btcoex_RecordPwrMode(padapter, u1H2CPwrModeParm, H2C_PWRMODE_LEN);

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

#ifdef CONFIG_AP_WOWLAN
static void rtl8723b_set_FwAPWoWlanCtrl_Cmd(struct adapter *padapter, u8 bFuncEn)
{
	u8 u1H2CAPWoWlanCtrlParm[H2C_WOWLAN_LEN] = {0};
	u8 gpionum = 0, gpio_dur = 0;
	u8 gpio_high_active = 1; /* 0: low active, 1: high active */
	u8 gpio_pulse = bFuncEn;

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

void rtl8723b_set_ap_wowlan_cmd(struct adapter *padapter, u8 enable)
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
}
#endif /* CONFIG_AP_WOWLAN */

/*
 * Description: Fill the reserved packets that FW will use to RSVD page.
 * Now we just send 4 types packet to rsvd page.
 * (1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
 *
 * Input:
 *
 * bDLFinished - false: At the first time we will send all the packets as
 * a large packet to Hw, so we need to set the packet length to total length.
 *
 * true: At the second time, we should send the first packet (default:beacon)
 * to Hw again and set the length in descriptor to the real beacon length.
 */
/* 2009.10.15 by tynli. */
static void rtl8723b_set_FwRsvdPagePkt(
	struct adapter *padapter, bool bDLFinished
)
{
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

	struct RSVDPAGE_LOC RsvdPageLoc;

	/* DBG_871X("%s---->\n", __func__); */

	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pwrctl = adapter_to_pwrctl(padapter);

	RsvdPageNum = BCNQ_PAGE_NUM_8723B + WOWLAN_PAGE_NUM_8723B;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (!pcmdframe) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(struct RSVDPAGE_LOC));

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

	TotalPacketLen = BufIndex + BTQosNullLength;

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
	}
	return;

error:

	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

#ifdef CONFIG_AP_WOWLAN
/*
 * Description: Fill the reserved packets that FW will use to RSVD page.
 * Now we just send 2 types packet to rsvd page. (1)Beacon, (2)ProbeRsp.
 *
 * Input: bDLFinished
 *
 * false: At the first time we will send all the packets as a large packet to
 * Hw, so we need to set the packet length to total length.
 *
 * true: At the second time, we should send the first packet (default:beacon)
 * to Hw again and set the length in descriptor to the real beacon length.
 */
/* 2009.10.15 by tynli. */
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
	struct RSVDPAGE_LOC RsvdPageLoc;

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
	if (!pcmdframe) {
		DBG_871X("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(struct RSVDPAGE_LOC));

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
			DBG_871X("%s: 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			DBG_871X("%s: 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
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
	if (!psta)
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
	u8 bssid[ETH_ALEN];


	DBG_871X("+" FUNC_ADPT_FMT ": qos =%d eosp =%d ps =%d\n",
		FUNC_ADPT_ARG(padapter), bQoS, bEosp, bForcePowerSave);

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	if (!StaAddr) {
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

	if (bQoS) {
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
	struct xmit_frame *pcmdframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	u32 BeaconLength = 0;
	u32 BTQosNullLength = 0;
	u8 *ReservedPagePacket;
	u8 TxDescLen, TxDescOffset;
	u8 TotalPageNum = 0, CurtPktPageNum = 0, RsvdPageNum = 0;
	u16 BufIndex, PageSize;
	u32 TotalPacketLen, MaxRsvdPageBufSize = 0;
	struct RSVDPAGE_LOC RsvdPageLoc;


/* 	DBG_8192C("+" FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(padapter)); */

	pxmitpriv = &padapter->xmitpriv;
	TxDescLen = TXDESC_SIZE;
	TxDescOffset = TXDESC_OFFSET;
	PageSize = PAGE_SIZE_TX_8723B;

	RsvdPageNum = BCNQ_PAGE_NUM_8723B;
	MaxRsvdPageBufSize = RsvdPageNum*PageSize;

	pcmdframe = rtw_alloc_cmdxmitframe(pxmitpriv);
	if (!pcmdframe) {
		DBG_8192C("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	memset(&RsvdPageLoc, 0, sizeof(struct RSVDPAGE_LOC));

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

	if (bcn_valid) {
		struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
		pwrctl->fw_psmode_iface_id = padapter->iface_id;
		DBG_8192C("%s: DL RSVD page success! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
	} else {
		DBG_8192C("%s: DL RSVD page fail! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
		DBG_8192C("%s: DL RSVD page fail! bSurpriseRemoved =%d\n",
			ADPT_ARG(padapter), padapter->bSurpriseRemoved);
		DBG_8192C("%s: DL RSVD page fail! bDriverStopped =%d\n",
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
