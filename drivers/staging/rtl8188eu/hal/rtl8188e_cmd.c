/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#define _RTL8188E_CMD_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <rtw_ioctl_set.h>

#include <rtl8188e_hal.h>

#define RTL88E_MAX_H2C_BOX_NUMS		4
#define RTL88E_MAX_CMD_LEN		7
#define RTL88E_MESSAGE_BOX_SIZE		4
#define RTL88E_EX_MESSAGE_BOX_SIZE	4

static u8 _is_fw_read_cmd_down(struct adapter *adapt, u8 msgbox_num)
{
	u8 read_down = false;
	int	retry_cnts = 100;

	u8 valid;

	do {
		valid = usb_read8(adapt, REG_HMETFR) & BIT(msgbox_num);
		if (0 == valid)
			read_down = true;
	} while ((!read_down) && (retry_cnts--));

	return read_down;
}

/*****************************************
* H2C Msg format :
* 0x1DF - 0x1D0
*| 31 - 8	| 7-5	 4 - 0	|
*| h2c_msg	|Class_ID CMD_ID	|
*
* Extend 0x1FF - 0x1F0
*|31 - 0	  |
*|ext_msg|
******************************************/
static s32 FillH2CCmd_88E(struct adapter *adapt, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 bcmd_down = false;
	s32 retry_cnts = 100;
	u8 h2c_box_num;
	u32 msgbox_addr;
	u32 msgbox_ex_addr;
	u8 cmd_idx, ext_cmd_len;
	u32 h2c_cmd = 0;
	u32 h2c_cmd_ex = 0;
	s32 ret = _FAIL;


	if (!adapt->bFWReady) {
		DBG_88E("FillH2CCmd_88E(): return H2C cmd because fw is not ready\n");
		return ret;
	}

	if (!pCmdBuffer)
		goto exit;
	if (CmdLen > RTL88E_MAX_CMD_LEN)
		goto exit;
	if (adapt->bSurpriseRemoved)
		goto exit;

	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = adapt->HalData->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(adapt, h2c_box_num)) {
			DBG_88E(" fw read cmd failed...\n");
			goto exit;
		}

		*(u8 *)(&h2c_cmd) = ElementID;

		if (CmdLen <= 3) {
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer, CmdLen);
		} else {
			memcpy((u8 *)(&h2c_cmd)+1, pCmdBuffer, 3);
			ext_cmd_len = CmdLen-3;
			memcpy((u8 *)(&h2c_cmd_ex), pCmdBuffer+3, ext_cmd_len);

			/* Write Ext command */
			msgbox_ex_addr = REG_HMEBOX_EXT_0 + (h2c_box_num * RTL88E_EX_MESSAGE_BOX_SIZE);
			for (cmd_idx = 0; cmd_idx < ext_cmd_len; cmd_idx++) {
				usb_write8(adapt, msgbox_ex_addr+cmd_idx, *((u8 *)(&h2c_cmd_ex)+cmd_idx));
			}
		}
		/*  Write command */
		msgbox_addr = REG_HMEBOX_0 + (h2c_box_num * RTL88E_MESSAGE_BOX_SIZE);
		for (cmd_idx = 0; cmd_idx < RTL88E_MESSAGE_BOX_SIZE; cmd_idx++) {
			usb_write8(adapt, msgbox_addr+cmd_idx, *((u8 *)(&h2c_cmd)+cmd_idx));
		}
		bcmd_down = true;

		adapt->HalData->LastHMEBoxNum =
			(h2c_box_num+1) % RTL88E_MAX_H2C_BOX_NUMS;

	} while ((!bcmd_down) && (retry_cnts--));

	ret = _SUCCESS;

exit:


	return ret;
}

/* bitmap[0:27] = tx_rate_bitmap */
/* bitmap[28:31]= Rate Adaptive id */
/* arg[0:4] = macid */
/* arg[5] = Short GI */
void rtw_hal_add_ra_tid(struct adapter *pAdapter, u32 bitmap, u8 arg, u8 rssi_level)
{
	struct odm_dm_struct *odmpriv = &pAdapter->HalData->odmpriv;

	u8 macid, init_rate, raid, shortGIrate = false;

	macid = arg&0x1f;

	raid = (bitmap>>28) & 0x0f;
	bitmap &= 0x0fffffff;

	if (rssi_level != DM_RATR_STA_INIT)
		bitmap = ODM_Get_Rate_Bitmap(odmpriv, macid, bitmap, rssi_level);

	bitmap |= ((raid<<28)&0xf0000000);

	init_rate = get_highest_rate_idx(bitmap&0x0fffffff)&0x3f;

	shortGIrate = (arg & BIT(5)) ? true : false;

	if (shortGIrate)
		init_rate |= BIT(6);

	raid = (bitmap>>28) & 0x0f;

	bitmap &= 0x0fffffff;

	DBG_88E("%s=> mac_id:%d, raid:%d, ra_bitmap=0x%x, shortGIrate=0x%02x\n",
		__func__, macid, raid, bitmap, shortGIrate);

	ODM_RA_UpdateRateInfo_8188E(odmpriv, macid, raid, bitmap, shortGIrate);
}

void rtl8188e_set_FwPwrMode_cmd(struct adapter *adapt, u8 Mode)
{
	struct setpwrmode_parm H2CSetPwrMode;
	struct pwrctrl_priv *pwrpriv = &adapt->pwrctrlpriv;
	u8 RLBM = 0; /*  0:Min, 1:Max, 2:User define */

	DBG_88E("%s: Mode=%d SmartPS=%d UAPSD=%d\n", __func__,
		Mode, pwrpriv->smart_ps, adapt->registrypriv.uapsd_enable);

	switch (Mode) {
	case PS_MODE_ACTIVE:
		H2CSetPwrMode.Mode = 0;
		break;
	case PS_MODE_MIN:
		H2CSetPwrMode.Mode = 1;
		break;
	case PS_MODE_MAX:
		RLBM = 1;
		H2CSetPwrMode.Mode = 1;
		break;
	case PS_MODE_DTIM:
		RLBM = 2;
		H2CSetPwrMode.Mode = 1;
		break;
	case PS_MODE_UAPSD_WMM:
		H2CSetPwrMode.Mode = 2;
		break;
	default:
		H2CSetPwrMode.Mode = 0;
		break;
	}

	H2CSetPwrMode.SmartPS_RLBM = (((pwrpriv->smart_ps<<4)&0xf0) | (RLBM & 0x0f));

	H2CSetPwrMode.AwakeInterval = 1;

	H2CSetPwrMode.bAllQueueUAPSD = adapt->registrypriv.uapsd_enable;

	if (Mode > 0)
		H2CSetPwrMode.PwrState = 0x00;/*  AllON(0x0C), RFON(0x04), RFOFF(0x00) */
	else
		H2CSetPwrMode.PwrState = 0x0C;/*  AllON(0x0C), RFON(0x04), RFOFF(0x00) */

	FillH2CCmd_88E(adapt, H2C_PS_PWR_MODE, sizeof(H2CSetPwrMode), (u8 *)&H2CSetPwrMode);

}

void rtl8188e_set_FwMediaStatus_cmd(struct adapter *adapt, __le16 mstatus_rpt)
{
	u8 opmode, macid;
	u16 mst_rpt = le16_to_cpu(mstatus_rpt);

	opmode = (u8)mst_rpt;
	macid = (u8)(mst_rpt >> 8);

	DBG_88E("### %s: MStatus=%x MACID=%d\n", __func__, opmode, macid);
	FillH2CCmd_88E(adapt, H2C_COM_MEDIA_STATUS_RPT, sizeof(mst_rpt), (u8 *)&mst_rpt);
}

static void ConstructBeacon(struct adapter *adapt, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 rate_len, pktlen;
	struct mlme_ext_priv *pmlmeext = &(adapt->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex		*cur_network = &(pmlmeinfo->network);
	u8 bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	ether_addr_copy(pwlanhdr->addr1, bc_addr);
	ether_addr_copy(pwlanhdr->addr2, myid(&(adapt->eeprompriv)));
	ether_addr_copy(pwlanhdr->addr3, cur_network->MacAddress);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
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
		pktlen += cur_network->IELength - sizeof(struct ndis_802_11_fixed_ie);
		memcpy(pframe, cur_network->IEs+sizeof(struct ndis_802_11_fixed_ie), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/*  SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, min_t(u32, rate_len, 8), cur_network->SupportedRates, &pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if ((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) {
		u32 ATIMWindow;
		/*  IBSS Parameter Set... */
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
		DBG_88E("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;
}

static void ConstructPSPoll(struct adapter *adapt, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	struct mlme_ext_priv *pmlmeext = &(adapt->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	__le16 *fctrl;
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	/*  Frame control. */
	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	/*  AID. */
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	/*  BSSID. */
	ether_addr_copy(pwlanhdr->addr1, pnetwork->MacAddress);

	/*  TA. */
	ether_addr_copy(pwlanhdr->addr2, myid(&(adapt->eeprompriv)));

	*pLength = 16;
}

static void ConstructNullFunctionData(struct adapter *adapt, u8 *pframe,
	u32 *pLength,
	u8 *StaAddr,
	u8 bQoS,
	u8 AC,
	u8 bEosp,
	u8 bForcePowerSave)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 pktlen;
	struct mlme_priv *pmlmepriv = &adapt->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv *pmlmeext = &(adapt->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex *pnetwork = &(pmlmeinfo->network);

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		ether_addr_copy(pwlanhdr->addr1, pnetwork->MacAddress);
		ether_addr_copy(pwlanhdr->addr2, myid(&(adapt->eeprompriv)));
		ether_addr_copy(pwlanhdr->addr3, StaAddr);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		ether_addr_copy(pwlanhdr->addr1, StaAddr);
		ether_addr_copy(pwlanhdr->addr2, pnetwork->MacAddress);
		ether_addr_copy(pwlanhdr->addr3, myid(&(adapt->eeprompriv)));
		break;
	case Ndis802_11IBSS:
	default:
		ether_addr_copy(pwlanhdr->addr1, StaAddr);
		ether_addr_copy(pwlanhdr->addr2, myid(&(adapt->eeprompriv)));
		ether_addr_copy(pwlanhdr->addr3, pnetwork->MacAddress);
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

static void ConstructProbeRsp(struct adapter *adapt, u8 *pframe, u32 *pLength, u8 *StaAddr, bool bHideSSID)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u8 *mac, *bssid;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext = &(adapt->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct wlan_bssid_ex	*cur_network = &(pmlmeinfo->network);

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&(adapt->eeprompriv));
	bssid = cur_network->MacAddress;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	ether_addr_copy(pwlanhdr->addr1, StaAddr);
	ether_addr_copy(pwlanhdr->addr2, mac);
	ether_addr_copy(pwlanhdr->addr3, bssid);

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

/*  */
/*  Description: Fill the reserved packets that FW will use to RSVD page. */
/*			Now we just send 4 types packet to rsvd page. */
/*			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp. */
/*	Input: */
/*	    bDLFinished - false: At the first time we will send all the packets as a large packet to Hw, */
/*						so we need to set the packet length to total length. */
/*			      true: At the second time, we should send the first packet (default:beacon) */
/*						to Hw again and set the length in descriptor to the real beacon length. */
/*  2009.10.15 by tynli. */
static void SetFwRsvdPagePkt(struct adapter *adapt, bool bDLFinished)
{
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info	*pmlmeinfo;
	u32 BeaconLength = 0, ProbeRspLength = 0, PSPollLength;
	u32 NullDataLength, QosNullLength;
	u8 *ReservedPagePacket;
	u8 PageNum, PageNeed, TxDescLen;
	u16 BufIndex;
	u32 TotalPacketLen;
	struct rsvdpage_loc RsvdPageLoc;
	struct wlan_bssid_ex *pnetwork;

	DBG_88E("%s\n", __func__);
	ReservedPagePacket = kzalloc(1000, GFP_KERNEL);
	if (!ReservedPagePacket) {
		DBG_88E("%s: alloc ReservedPagePacket fail!\n", __func__);
		return;
	}

	pxmitpriv = &adapt->xmitpriv;
	pmlmeext = &adapt->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;
	pnetwork = &(pmlmeinfo->network);

	TxDescLen = TXDESC_SIZE;
	PageNum = 0;

	/* 3 (1) beacon * 2 pages */
	BufIndex = TXDESC_OFFSET;
	ConstructBeacon(adapt, &ReservedPagePacket[BufIndex], &BeaconLength);

	/*  When we count the first page size, we need to reserve description size for the RSVD */
	/*  packet, it will be filled in front of the packet in TXPKTBUF. */
	PageNeed = (u8)PageNum_128(TxDescLen + BeaconLength);
	/*  To reserved 2 pages for beacon buffer. 2010.06.24. */
	if (PageNeed == 1)
		PageNeed += 1;
	PageNum += PageNeed;
	adapt->HalData->FwRsvdPageStartOffset = PageNum;

	BufIndex += PageNeed*128;

	/* 3 (2) ps-poll *1 page */
	RsvdPageLoc.LocPsPoll = PageNum;
	ConstructPSPoll(adapt, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, true, false);

	PageNeed = (u8)PageNum_128(TxDescLen + PSPollLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (3) null data * 1 page */
	RsvdPageLoc.LocNullData = PageNum;
	ConstructNullFunctionData(adapt, &ReservedPagePacket[BufIndex], &NullDataLength, pnetwork->MacAddress, false, 0, 0, false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex-TxDescLen], NullDataLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + NullDataLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (4) probe response * 1page */
	RsvdPageLoc.LocProbeRsp = PageNum;
	ConstructProbeRsp(adapt, &ReservedPagePacket[BufIndex], &ProbeRspLength, pnetwork->MacAddress, false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + ProbeRspLength);
	PageNum += PageNeed;

	BufIndex += PageNeed*128;

	/* 3 (5) Qos null data */
	RsvdPageLoc.LocQosNull = PageNum;
	ConstructNullFunctionData(adapt, &ReservedPagePacket[BufIndex],
				  &QosNullLength, pnetwork->MacAddress, true, 0, 0, false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex-TxDescLen], QosNullLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + QosNullLength);
	PageNum += PageNeed;

	TotalPacketLen = BufIndex + QosNullLength;
	pmgntframe = alloc_mgtxmitframe(pxmitpriv);
	if (!pmgntframe)
		goto exit;

	/*  update attribute */
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(adapt, pattrib);
	pattrib->qsel = 0x10;
	pattrib->last_txcmdsz = TotalPacketLen - TXDESC_OFFSET;
	pattrib->pktlen = pattrib->last_txcmdsz;
	memcpy(pmgntframe->buf_addr, ReservedPagePacket, TotalPacketLen);

	rtw_hal_mgnt_xmit(adapt, pmgntframe);

	DBG_88E("%s: Set RSVD page location to Fw\n", __func__);
	FillH2CCmd_88E(adapt, H2C_COM_RSVD_PAGE, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);

exit:
	kfree(ReservedPagePacket);
}

void rtl8188e_set_FwJoinBssReport_cmd(struct adapter *adapt, u8 mstatus)
{
	struct hal_data_8188e *haldata = adapt->HalData;
	struct mlme_ext_priv *pmlmeext = &(adapt->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	bool	bSendBeacon = false;
	bool	bcn_valid = false;
	u8 DLBcnCount = 0;
	u32 poll = 0;


	DBG_88E("%s mstatus(%x)\n", __func__, mstatus);

	if (mstatus == 1) {
		/*  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		/*  Suggested by filen. Added by tynli. */
		usb_write16(adapt, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		/*  Do not set TSF again here or vWiFi beacon DMA INT will not work. */

		/* Set REG_CR bit 8. DMA beacon by SW. */
		haldata->RegCR_1 |= BIT(0);
		usb_write8(adapt,  REG_CR+1, haldata->RegCR_1);

		/*  Disable Hw protection for a time which revserd for Hw sending beacon. */
		/*  Fix download reserved page packet fail that access collision with the protection time. */
		/*  2010.05.11. Added by tynli. */
		usb_write8(adapt, REG_BCN_CTRL, usb_read8(adapt, REG_BCN_CTRL)&(~BIT(3)));
		usb_write8(adapt, REG_BCN_CTRL, usb_read8(adapt, REG_BCN_CTRL) | BIT(4));

		if (haldata->RegFwHwTxQCtrl & BIT(6)) {
			DBG_88E("HalDownloadRSVDPage(): There is an Adapter is sending beacon.\n");
			bSendBeacon = true;
		}

		/*  Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		usb_write8(adapt, REG_FWHW_TXQ_CTRL+2, (haldata->RegFwHwTxQCtrl&(~BIT(6))));
		haldata->RegFwHwTxQCtrl &= (~BIT(6));

		/*  Clear beacon valid check bit. */
		rtw_hal_set_hwreg(adapt, HW_VAR_BCN_VALID, NULL);
		DLBcnCount = 0;
		poll = 0;
		do {
			/*  download rsvd page. */
			SetFwRsvdPagePkt(adapt, false);
			DLBcnCount++;
			do {
				yield();
				/* mdelay(10); */
				/*  check rsvd page download OK. */
				rtw_hal_get_hwreg(adapt, HW_VAR_BCN_VALID, (u8 *)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll%10) != 0 && !adapt->bSurpriseRemoved && !adapt->bDriverStopped);
		} while (!bcn_valid && DLBcnCount <= 100 && !adapt->bSurpriseRemoved && !adapt->bDriverStopped);

		if (adapt->bSurpriseRemoved || adapt->bDriverStopped)
			;
		else if (!bcn_valid)
			DBG_88E("%s: 1 Download RSVD page failed! DLBcnCount:%u, poll:%u\n", __func__, DLBcnCount, poll);
		else
			DBG_88E("%s: 1 Download RSVD success! DLBcnCount:%u, poll:%u\n", __func__, DLBcnCount, poll);
		/*  */
		/*  We just can send the reserved page twice during the time that Tx thread is stopped (e.g. pnpsetpower) */
		/*  because we need to free the Tx BCN Desc which is used by the first reserved page packet. */
		/*  At run time, we cannot get the Tx Desc until it is released in TxHandleInterrupt() so we will return */
		/*  the beacon TCB in the following code. 2011.11.23. by tynli. */
		/*  */

		/*  Enable Bcn */
		usb_write8(adapt, REG_BCN_CTRL, usb_read8(adapt, REG_BCN_CTRL) | BIT(3));
		usb_write8(adapt, REG_BCN_CTRL, usb_read8(adapt, REG_BCN_CTRL)&(~BIT(4)));

		/*  To make sure that if there exists an adapter which would like to send beacon. */
		/*  If exists, the origianl value of 0x422[6] will be 1, we should check this to */
		/*  prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
		/*  the beacon cannot be sent by HW. */
		/*  2010.06.23. Added by tynli. */
		if (bSendBeacon) {
			usb_write8(adapt, REG_FWHW_TXQ_CTRL+2, (haldata->RegFwHwTxQCtrl | BIT(6)));
			haldata->RegFwHwTxQCtrl |= BIT(6);
		}

		/*  Update RSVD page location H2C to Fw. */
		if (bcn_valid) {
			rtw_hal_set_hwreg(adapt, HW_VAR_BCN_VALID, NULL);
			DBG_88E("Set RSVD page location to Fw.\n");
		}

		/*  Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli. */
		/*  Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		haldata->RegCR_1 &= (~BIT(0));
		usb_write8(adapt,  REG_CR+1, haldata->RegCR_1);
	}
}
