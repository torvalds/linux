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
#define _RTL8723A_CMD_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtw_ioctl_set.h>
#include <rtl8723a_hal.h>

#define RTL92C_MAX_H2C_BOX_NUMS		4
#define RTL92C_MAX_CMD_LEN		5
#define MESSAGE_BOX_SIZE		4
#define EX_MESSAGE_BOX_SIZE		2

static u8 _is_fw_read_cmd_down(struct rtw_adapter *padapter, u8 msgbox_num)
{
	u8 read_down = false;
	int	retry_cnts = 100;
	u8 valid;

	do {
		valid = rtw_read8(padapter, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid)
			read_down = true;
	} while ((!read_down) && (retry_cnts--));

	return read_down;
}

/*****************************************
* H2C Msg format :
*| 31 - 8		|7		| 6 - 0	|
*| h2c_msg	|Ext_bit	|CMD_ID	|
*
******************************************/
s32 FillH2CCmd(struct rtw_adapter *padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 bcmd_down = false;
	s32 retry_cnts = 100;
	u8 h2c_box_num;
	u32 msgbox_addr;
	u32 msgbox_ex_addr;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	u32 h2c_cmd = 0;
	u16 h2c_cmd_ex = 0;
	s32 ret = _FAIL;

	padapter = GET_PRIMARY_ADAPTER(padapter);
	pHalData = GET_HAL_DATA(padapter);

	mutex_lock(&adapter_to_dvobj(padapter)->h2c_fwcmd_mutex);

	if (!pCmdBuffer)
		goto exit;
	if (CmdLen > RTL92C_MAX_CMD_LEN)
		goto exit;
	if (padapter->bSurpriseRemoved == true)
		goto exit;

	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = pHalData->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(padapter, h2c_box_num)) {
			DBG_8723A(" fw read cmd failed...\n");
			goto exit;
		}

		if (CmdLen <= 3) {
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer, CmdLen);
		} else {
			memcpy((u8 *)(&h2c_cmd_ex), pCmdBuffer, EX_MESSAGE_BOX_SIZE);
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer+2, (CmdLen-EX_MESSAGE_BOX_SIZE));
			*(u8 *)(&h2c_cmd) |= BIT(7);
		}

		*(u8 *)(&h2c_cmd) |= ElementID;

		if (h2c_cmd & BIT(7)) {
			msgbox_ex_addr = REG_HMEBOX_EXT_0 + (h2c_box_num * EX_MESSAGE_BOX_SIZE);
			h2c_cmd_ex = le16_to_cpu(h2c_cmd_ex);
			rtw_write16(padapter, msgbox_ex_addr, h2c_cmd_ex);
		}
		msgbox_addr = REG_HMEBOX_0 + (h2c_box_num * MESSAGE_BOX_SIZE);
		h2c_cmd = le32_to_cpu(h2c_cmd);
		rtw_write32(padapter, msgbox_addr, h2c_cmd);

		bcmd_down = true;

		pHalData->LastHMEBoxNum = (h2c_box_num+1) % RTL92C_MAX_H2C_BOX_NUMS;

	} while ((!bcmd_down) && (retry_cnts--));

	ret = _SUCCESS;

exit:
	mutex_unlock(&adapter_to_dvobj(padapter)->h2c_fwcmd_mutex);
	return ret;
}

u8 rtl8723a_set_rssi_cmd(struct rtw_adapter *padapter, u8 *param)
{
	u8 res = _SUCCESS;

	*((u32 *)param) = cpu_to_le32(*((u32 *)param));

	FillH2CCmd(padapter, RSSI_SETTING_EID, 3, param);

	return res;
}

u8 rtl8723a_set_raid_cmd(struct rtw_adapter *padapter, u32 mask, u8 arg)
{
	u8 buf[5];
	u8 res = _SUCCESS;

	memset(buf, 0, 5);
	mask = cpu_to_le32(mask);
	memcpy(buf, &mask, 4);
	buf[4]  = arg;

	FillH2CCmd(padapter, MACID_CONFIG_EID, 5, buf);

	return res;

}

/* bitmap[0:27] = tx_rate_bitmap */
/* bitmap[28:31]= Rate Adaptive id */
/* arg[0:4] = macid */
/* arg[5] = Short GI */
void rtl8723a_add_rateatid(struct rtw_adapter *pAdapter, u32 bitmap, u8 arg, u8 rssi_level)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(pAdapter);
	u8 macid = arg&0x1f;
	u8 raid = (bitmap>>28) & 0x0f;

	bitmap &= 0x0fffffff;
	if (rssi_level != DM_RATR_STA_INIT)
		bitmap = ODM_Get_Rate_Bitmap23a(&pHalData->odmpriv, macid, bitmap, rssi_level);

	bitmap |= ((raid<<28)&0xf0000000);

	if (pHalData->fw_ractrl == true) {
		rtl8723a_set_raid_cmd(pAdapter, bitmap, arg);
	} else {
		u8 init_rate, shortGIrate = false;

		init_rate = get_highest_rate_idx23a(bitmap&0x0fffffff)&0x3f;

		shortGIrate = (arg&BIT(5)) ? true:false;

		if (shortGIrate == true)
			init_rate |= BIT(6);

		rtw_write8(pAdapter, (REG_INIDATA_RATE_SEL+macid), (u8)init_rate);
	}
}

void rtl8723a_set_FwPwrMode_cmd(struct rtw_adapter *padapter, u8 Mode)
{
	struct setpwrmode_parm H2CSetPwrMode;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);

	DBG_8723A("%s: Mode =%d SmartPS =%d UAPSD =%d BcnMode = 0x%02x\n", __FUNCTION__,
			Mode, pwrpriv->smart_ps, padapter->registrypriv.uapsd_enable, pwrpriv->bcn_ant_mode);

	/*  Forece leave RF low power mode for 1T1R to
	    prevent conficting setting in Fw power */
	/*  saving sequence. 2010.06.07. Added by tynli.
	    Suggested by SD3 yschang. */
	if ((Mode != PS_MODE_ACTIVE) &&
	    (!IS_92C_SERIAL(pHalData->VersionID))) {
		ODM_RF_Saving23a(&pHalData->odmpriv, true);
	}

	H2CSetPwrMode.Mode = Mode;
	H2CSetPwrMode.SmartPS = pwrpriv->smart_ps;
	H2CSetPwrMode.AwakeInterval = 1;
	H2CSetPwrMode.bAllQueueUAPSD = padapter->registrypriv.uapsd_enable;
	H2CSetPwrMode.BcnAntMode = pwrpriv->bcn_ant_mode;

	FillH2CCmd(padapter, SET_PWRMODE_EID, sizeof(H2CSetPwrMode), (u8 *)&H2CSetPwrMode);

}

static void ConstructBeacon(struct rtw_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u32 rate_len, pktlen;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	/* DBG_8723A("%s\n", __FUNCTION__); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid23a(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	/* pmlmeext->mgnt_seq++; */
	SetFrameSubType(pframe, WIFI_BEACON);

	pframe += sizeof(struct ieee80211_hdr_3addr);
	pktlen = sizeof (struct ieee80211_hdr_3addr);

	/* timestamp will be inserted by hardware */
	pframe += 8;
	pktlen += 8;

	/*  beacon interval: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval23a_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	/*  capability info: 2 bytes */
	memcpy(pframe, (unsigned char *)(rtw_get_capability23a_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE) {
		/* DBG_8723A("ie len =%d\n", cur_network->IELength); */
		pktlen += cur_network->IELength - sizeof(struct ndis_802_11_fixed_ies);
		memcpy(pframe, cur_network->IEs+sizeof(struct ndis_802_11_fixed_ies), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/*  SSID */
	pframe = rtw_set_ie23a(pframe, _SSID_IE_, cur_network->Ssid.ssid_len,
			    cur_network->Ssid.ssid, &pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len23a(cur_network->SupportedRates);
	pframe = rtw_set_ie23a(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ?
			       8 : rate_len), cur_network->SupportedRates, &pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie23a(pframe, _DSSET_IE_, 1, (unsigned char *)&cur_network->Configuration.DSConfig, &pktlen);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
		/* ATIMWindow = cur->Configuration.ATIMWindow; */
		ATIMWindow = 0;
		pframe = rtw_set_ie23a(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}

	/* todo: ERP IE */

	/*  EXTERNDED SUPPORTED RATE */
	if (rate_len > 8)
		pframe = rtw_set_ie23a(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);

	/* todo:HT for adhoc */

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512) {
		DBG_8723A("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	/* DBG_8723A("%s bcn_sz =%d\n", __FUNCTION__, pktlen); */

}

static void ConstructPSPoll(struct rtw_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	/*  Frame control. */
	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	/*  AID. */
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	/*  BSSID. */
	memcpy(pwlanhdr->addr1, get_my_bssid23a(&pmlmeinfo->network), ETH_ALEN);

	/*  TA. */
	memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);

	*pLength = 16;
}

static void ConstructNullFunctionData(
	struct rtw_adapter *padapter,
	u8 *pframe,
	u32 *pLength,
	u8 *StaAddr,
	u8 bQoS,
	u8 AC,
	u8 bEosp,
	u8 bForcePowerSave)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u32 pktlen;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		memcpy(pwlanhdr->addr1,
		       get_my_bssid23a(&pmlmeinfo->network), ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv),
		       ETH_ALEN);
		memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2,
		       get_my_bssid23a(&pmlmeinfo->network), ETH_ALEN);
		memcpy(pwlanhdr->addr3, myid(&padapter->eeprompriv),
		       ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&padapter->eeprompriv), ETH_ALEN);
		memcpy(pwlanhdr->addr3,
		       get_my_bssid23a(&pmlmeinfo->network), ETH_ALEN);
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

static void ConstructProbeRsp(struct rtw_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, bool bHideSSID)
{
	struct ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u8 *mac, *bssid;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex *cur_network = &pmlmeinfo->network;

	/* DBG_8723A("%s\n", __FUNCTION__); */

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&padapter->eeprompriv);
	bssid = cur_network->MacAddress;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);

	pktlen = sizeof(struct ieee80211_hdr_3addr);
	pframe += pktlen;

	if (cur_network->IELength > MAX_IE_SZ)
		return;

	memcpy(pframe, cur_network->IEs, cur_network->IELength);
	pframe += cur_network->IELength;
	pktlen += cur_network->IELength;

	*pLength = pktlen;
}

/*  To check if reserved page content is destroyed by beacon beacuse beacon is too large. */
void CheckFwRsvdPageContent23a(struct rtw_adapter *Adapter)
{
}

/*  */
/*  Description: Fill the reserved packets that FW will use to RSVD page. */
/*			Now we just send 4 types packet to rsvd page. */
/*			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp. */
/*	Input: */
/*	    bDLFinished - false: At the first time we will send all the packets as a large packet to Hw, */
/*						so we need to set the packet length to total lengh. */
/*			      true: At the second time, we should send the first packet (default:beacon) */
/*						to Hw again and set the lengh in descriptor to the real beacon lengh. */
/*  2009.10.15 by tynli. */
static void SetFwRsvdPagePkt(struct rtw_adapter *padapter, bool bDLFinished)
{
	struct hal_data_8723a *pHalData;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u32 BeaconLength = 0, ProbeRspLength = 0, PSPollLength;
	u32 NullDataLength, QosNullLength, BTQosNullLength;
	u8 *ReservedPagePacket;
	u8 PageNum, PageNeed, TxDescLen;
	u16 BufIndex;
	u32 TotalPacketLen;
	struct rsvdpage_loc	RsvdPageLoc;

	DBG_8723A("%s\n", __FUNCTION__);

	ReservedPagePacket = kzalloc(1000, GFP_KERNEL);
	if (ReservedPagePacket == NULL) {
		DBG_8723A("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	TxDescLen = TXDESC_SIZE;
	PageNum = 0;

	/* 3 (1) beacon */
	BufIndex = TXDESC_OFFSET;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	/*  When we count the first page size, we need to reserve description size for the RSVD */
	/*  packet, it will be filled in front of the packet in TXPKTBUF. */
	PageNeed = (u8)PageNum_128(TxDescLen + BeaconLength);
	/*  To reserved 2 pages for beacon buffer. 2010.06.24. */
	if (PageNeed == 1)
		PageNeed += 1;
	PageNum += PageNeed;
	pHalData->FwRsvdPageStartOffset = PageNum;

	BufIndex += PageNeed*128;

	/* 3 (2) ps-poll */
	RsvdPageLoc.LocPsPoll = PageNum;
	ConstructPSPoll(padapter, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, true, false);

	PageNeed = (u8)PageNum_128(TxDescLen + PSPollLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (3) null data */
	RsvdPageLoc.LocNullData = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&NullDataLength,
		get_my_bssid23a(&pmlmeinfo->network),
		false, 0, 0, false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + NullDataLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (4) probe response */
	RsvdPageLoc.LocProbeRsp = PageNum;
	ConstructProbeRsp(
		padapter,
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		get_my_bssid23a(&pmlmeinfo->network),
		false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + ProbeRspLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (5) Qos null data */
	RsvdPageLoc.LocQosNull = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&QosNullLength,
		get_my_bssid23a(&pmlmeinfo->network),
		true, 0, 0, false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + QosNullLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (6) BT Qos null data */
	RsvdPageLoc.LocBTQosNull = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		get_my_bssid23a(&pmlmeinfo->network),
		true, 0, 0, false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, false, true);

	TotalPacketLen = BufIndex + BTQosNullLength;

	pmgntframe = alloc_mgtxmitframe23a(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/*  update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->qsel = 0x10;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TXDESC_OFFSET;
	memcpy(pmgntframe->buf_addr, ReservedPagePacket, TotalPacketLen);

	rtw_hal_mgnt_xmit23a(padapter, pmgntframe);

	DBG_8723A("%s: Set RSVD page location to Fw\n", __FUNCTION__);
	FillH2CCmd(padapter, RSVD_PAGE_EID, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);

exit:
	kfree(ReservedPagePacket);
}

void rtl8723a_set_FwJoinBssReport_cmd(struct rtw_adapter *padapter, u8 mstatus)
{
	struct joinbssrpt_parm	JoinBssRptParm;
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;

	DBG_8723A("%s mstatus(%x)\n", __FUNCTION__, mstatus);

	if (mstatus == 1) {
		bool bRecover = false;
		u8 v8;

		/*  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		/*  Suggested by filen. Added by tynli. */
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		/*  Do not set TSF again here or vWiFi beacon DMA INT will not work. */
		/* correct_TSF23a(padapter, pmlmeext); */
		/*  Hw sequende enable by dedault. 2010.06.23. by tynli. */
		/* rtw_write16(padapter, REG_NQOS_SEQ, ((pmlmeext->mgnt_seq+100)&0xFFF)); */
		/* rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF); */

		/*  set REG_CR bit 8 */
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 |= BIT(0); /*  ENSWBCN */
		rtw_write8(padapter,  REG_CR+1, v8);

		/*  Disable Hw protection for a time which revserd for Hw sending beacon. */
		/*  Fix download reserved page packet fail that access collision with the protection time. */
		/*  2010.05.11. Added by tynli. */
/*			SetBcnCtrlReg23a(padapter, 0, BIT(3)); */
/*			SetBcnCtrlReg23a(padapter, BIT(4), 0); */
		SetBcnCtrlReg23a(padapter, BIT(4), BIT(3));

		/*  Set FWHW_TXQ_CTRL 0x422[6]= 0 to tell Hw the packet is not a real beacon frame. */
		if (pHalData->RegFwHwTxQCtrl & BIT(6))
			bRecover = true;

		/*  To tell Hw the packet is not a real beacon frame. */
		/* U1bTmp = rtw_read8(padapter, REG_FWHW_TXQ_CTRL+2); */
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl & ~BIT(6));
		pHalData->RegFwHwTxQCtrl &= ~BIT(6);
		SetFwRsvdPagePkt(padapter, 0);

		/*  2010.05.11. Added by tynli. */
		SetBcnCtrlReg23a(padapter, BIT(3), BIT(4));

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

	JoinBssRptParm.OpMode = mstatus;

	FillH2CCmd(padapter, JOINBSS_RPT_EID, sizeof(JoinBssRptParm), (u8 *)&JoinBssRptParm);

}

#ifdef CONFIG_8723AU_BT_COEXIST
static void SetFwRsvdPagePkt_BTCoex(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	struct xmit_frame *pmgntframe;
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u8 fakemac[6] = {0x00, 0xe0, 0x4c, 0x00, 0x00, 0x00};
	u32 NullDataLength, BTQosNullLength;
	u8 *ReservedPagePacket;
	u8 PageNum, PageNeed, TxDescLen;
	u16 BufIndex;
	u32 TotalPacketLen;
	struct rsvdpage_loc	RsvdPageLoc;

	DBG_8723A("+%s\n", __FUNCTION__);

	ReservedPagePacket = kzalloc(1024, GFP_KERNEL);
	if (ReservedPagePacket == NULL) {
		DBG_8723A("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	TxDescLen = TXDESC_SIZE;
	PageNum = 0;

	/* 3 (1) beacon */
	BufIndex = TXDESC_OFFSET;
	/*  skip Beacon Packet */
	PageNeed = 3;

	PageNum += PageNeed;
	pHalData->FwRsvdPageStartOffset = PageNum;

	BufIndex += PageNeed*128;

	/* 3 (3) null data */
	RsvdPageLoc.LocNullData = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&NullDataLength,
		fakemac,
		false, 0, 0, false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + NullDataLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (6) BT Qos null data */
	RsvdPageLoc.LocBTQosNull = PageNum;
	ConstructNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		fakemac,
		true, 0, 0, false);
	rtl8723a_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, false, true);

	TotalPacketLen = BufIndex + BTQosNullLength;

	pmgntframe = alloc_mgtxmitframe23a(pxmitpriv);
	if (pmgntframe == NULL)
		goto exit;

	/*  update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib23a(padapter, pattrib);
	pattrib->qsel = 0x10;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TXDESC_OFFSET;
	memcpy(pmgntframe->buf_addr, ReservedPagePacket, TotalPacketLen);

	rtw_hal_mgnt_xmit23a(padapter, pmgntframe);

	DBG_8723A("%s: Set RSVD page location to Fw\n", __FUNCTION__);
	FillH2CCmd(padapter, RSVD_PAGE_EID, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);

exit:
	kfree(ReservedPagePacket);
}

void rtl8723a_set_BTCoex_AP_mode_FwRsvdPkt_cmd(struct rtw_adapter *padapter)
{
	struct hal_data_8723a *pHalData;
	u8 bRecover = false;

	DBG_8723A("+%s\n", __FUNCTION__);

	pHalData = GET_HAL_DATA(padapter);

	/*  Set FWHW_TXQ_CTRL 0x422[6]= 0 to tell Hw the packet is not a real beacon frame. */
	if (pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover = true;

	/*  To tell Hw the packet is not a real beacon frame. */
	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	SetFwRsvdPagePkt_BTCoex(padapter);

	/*  To make sure that if there exists an adapter which would like to send beacon. */
	/*  If exists, the origianl value of 0x422[6] will be 1, we should check this to */
	/*  prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/*  the beacon cannot be sent by HW. */
	/*  2010.06.23. Added by tynli. */
	if (bRecover) {
		pHalData->RegFwHwTxQCtrl |= BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	}
}
#endif

#ifdef CONFIG_8723AU_P2P
void rtl8723a_set_p2p_ps_offload_cmd(struct rtw_adapter *padapter, u8 p2p_ps_state)
{
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);
	struct wifidirect_info *pwdinfo = &padapter->wdinfo;
	struct P2P_PS_Offload_t	*p2p_ps_offload = &pHalData->p2p_ps_offload;
	u8 i;

	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		DBG_8723A("P2P_PS_DISABLE \n");
		memset(p2p_ps_offload, 0, 1);
		break;
	case P2P_PS_ENABLE:
		DBG_8723A("P2P_PS_ENABLE \n");
		/*  update CTWindow value. */
		if (pwdinfo->ctwindow > 0) {
			p2p_ps_offload->CTWindow_En = 1;
			rtw_write8(padapter, REG_P2P_CTWIN, pwdinfo->ctwindow);
		}

		/*  hw only support 2 set of NoA */
		for (i = 0; i < pwdinfo->noa_num; i++) {
			/*  To control the register setting for which NOA */
			rtw_write8(padapter, REG_NOA_DESC_SEL, (i << 4));
			if (i == 0)
				p2p_ps_offload->NoA0_En = 1;
			else
				p2p_ps_offload->NoA1_En = 1;

			/*  config P2P NoA Descriptor Register */
			rtw_write32(padapter, REG_NOA_DESC_DURATION, pwdinfo->noa_duration[i]);

			rtw_write32(padapter, REG_NOA_DESC_INTERVAL, pwdinfo->noa_interval[i]);

			rtw_write32(padapter, REG_NOA_DESC_START, pwdinfo->noa_start_time[i]);

			rtw_write8(padapter, REG_NOA_DESC_COUNT, pwdinfo->noa_count[i]);
		}

		if ((pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0)) {
			/*  rst p2p circuit */
			rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(4));

			p2p_ps_offload->Offload_En = 1;

			if (rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO)) {
				p2p_ps_offload->role = 1;
				p2p_ps_offload->AllStaSleep = 0;
			} else {
				p2p_ps_offload->role = 0;
			}

			p2p_ps_offload->discovery = 0;
		}
		break;
	case P2P_PS_SCAN:
		DBG_8723A("P2P_PS_SCAN \n");
		p2p_ps_offload->discovery = 1;
		break;
	case P2P_PS_SCAN_DONE:
		DBG_8723A("P2P_PS_SCAN_DONE \n");
		p2p_ps_offload->discovery = 0;
		pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
		break;
	default:
		break;
	}

	FillH2CCmd(padapter, P2P_PS_OFFLOAD_EID, 1, (u8 *)p2p_ps_offload);
}
#endif /* CONFIG_8723AU_P2P */
