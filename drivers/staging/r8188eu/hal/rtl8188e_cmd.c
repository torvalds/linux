// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_CMD_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/recv_osdep.h"
#include "../include/mlme_osdep.h"
#include "../include/rtw_ioctl_set.h"

#include "../include/rtl8188e_hal.h"

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
		valid = rtw_read8(adapt, REG_HMETFR) & BIT(msgbox_num);
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
	struct hal_data_8188e *haldata = &adapt->haldata;
	u8 cmd_idx, ext_cmd_len;
	u32 h2c_cmd = 0;
	u32 h2c_cmd_ex = 0;

	if (!adapt->bFWReady)
		return _FAIL;

	if (!pCmdBuffer || CmdLen > RTL88E_MAX_CMD_LEN || adapt->bSurpriseRemoved)
		return _FAIL;

	/* pay attention to if  race condition happened in  H2C cmd setting. */
	do {
		h2c_box_num = haldata->LastHMEBoxNum;

		if (!_is_fw_read_cmd_down(adapt, h2c_box_num))
			return _FAIL;

		*(u8 *)(&h2c_cmd) = ElementID;

		if (CmdLen <= 3) {
			memcpy((u8 *)(&h2c_cmd) + 1, pCmdBuffer, CmdLen);
		} else {
			memcpy((u8 *)(&h2c_cmd) + 1, pCmdBuffer, 3);
			ext_cmd_len = CmdLen - 3;
			memcpy((u8 *)(&h2c_cmd_ex), pCmdBuffer + 3, ext_cmd_len);

			/* Write Ext command */
			msgbox_ex_addr = REG_HMEBOX_EXT_0 + (h2c_box_num * RTL88E_EX_MESSAGE_BOX_SIZE);
			for (cmd_idx = 0; cmd_idx < ext_cmd_len; cmd_idx++) {
				rtw_write8(adapt, msgbox_ex_addr + cmd_idx, *((u8 *)(&h2c_cmd_ex) + cmd_idx));
			}
		}
		/*  Write command */
		msgbox_addr = REG_HMEBOX_0 + (h2c_box_num * RTL88E_MESSAGE_BOX_SIZE);
		for (cmd_idx = 0; cmd_idx < RTL88E_MESSAGE_BOX_SIZE; cmd_idx++) {
			rtw_write8(adapt, msgbox_addr + cmd_idx, *((u8 *)(&h2c_cmd) + cmd_idx));
		}
		bcmd_down = true;

		haldata->LastHMEBoxNum = (h2c_box_num + 1) % RTL88E_MAX_H2C_BOX_NUMS;

	} while ((!bcmd_down) && (retry_cnts--));

	return _SUCCESS;
}

u8 rtl8188e_set_raid_cmd(struct adapter *adapt, u32 mask)
{
	u8 buf[3];
	u8 res = _SUCCESS;
	struct hal_data_8188e *haldata = &adapt->haldata;

	if (haldata->fw_ractrl) {
		__le32 lmask;

		memset(buf, 0, 3);
		lmask = cpu_to_le32(mask);
		memcpy(buf, &lmask, 3);

		FillH2CCmd_88E(adapt, H2C_DM_MACID_CFG, 3, buf);
	} else {
		res = _FAIL;
	}

	return res;
}

/* bitmap[0:27] = tx_rate_bitmap */
/* bitmap[28:31]= Rate Adaptive id */
/* arg[0:4] = macid */
/* arg[5] = Short GI */
void rtl8188e_Add_RateATid(struct adapter *pAdapter, u32 bitmap, u8 arg, u8 rssi_level)
{
	struct hal_data_8188e *haldata = &pAdapter->haldata;

	u8 macid, raid, short_gi_rate = false;

	macid = arg & 0x1f;

	raid = (bitmap >> 28) & 0x0f;
	bitmap &= 0x0fffffff;

	if (rssi_level != DM_RATR_STA_INIT)
		bitmap = ODM_Get_Rate_Bitmap(&haldata->odmpriv, macid, bitmap, rssi_level);

	bitmap |= ((raid << 28) & 0xf0000000);

	short_gi_rate = (arg & BIT(5)) ? true : false;

	raid = (bitmap >> 28) & 0x0f;

	bitmap &= 0x0fffffff;

	ODM_RA_UpdateRateInfo_8188E(&haldata->odmpriv, macid, raid, bitmap, short_gi_rate);
}

void rtl8188e_set_FwPwrMode_cmd(struct adapter *adapt, u8 Mode)
{
	struct setpwrmode_parm H2CSetPwrMode;
	struct pwrctrl_priv *pwrpriv = &adapt->pwrctrlpriv;
	u8 RLBM = 0; /*  0:Min, 1:Max, 2:User define */

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

	H2CSetPwrMode.SmartPS_RLBM = (((pwrpriv->smart_ps << 4) & 0xf0) | (RLBM & 0x0f));

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
	u16 mst_rpt = le16_to_cpu(mstatus_rpt);

	FillH2CCmd_88E(adapt, H2C_COM_MEDIA_STATUS_RPT, sizeof(mst_rpt), (u8 *)&mst_rpt);
}

static void ConstructBeacon(struct adapter *adapt, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u32 rate_len, pktlen;
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex		*cur_network = &pmlmeinfo->network;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;

	eth_broadcast_addr(pwlanhdr->addr1);
	memcpy(pwlanhdr->addr2, myid(&adapt->eeprompriv), ETH_ALEN);
	memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

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

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_AP_STATE) {
		pktlen += cur_network->IELength - sizeof(struct ndis_802_11_fixed_ie);
		memcpy(pframe, cur_network->IEs + sizeof(struct ndis_802_11_fixed_ie), pktlen);

		goto _ConstructBeacon;
	}

	/* below for ad-hoc mode */

	/*  SSID */
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	/*  supported rates... */
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8) ? 8 : rate_len), cur_network->SupportedRates, &pktlen);

	/*  DS parameter set */
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&cur_network->Configuration.DSConfig, &pktlen);

	if ((pmlmeinfo->state & 0x03) == WIFI_FW_ADHOC_STATE) {
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

	if ((pktlen + TXDESC_SIZE) > 512)
		return;

	*pLength = pktlen;
}

static void ConstructPSPoll(struct adapter *adapt, u8 *pframe, u32 *pLength)
{
	struct ieee80211_hdr *pwlanhdr;
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	__le16 *fctrl;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	/*  Frame control. */
	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	/*  AID. */
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	/*  BSSID. */
	memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);

	/*  TA. */
	memcpy(pwlanhdr->addr2, myid(&adapt->eeprompriv), ETH_ALEN);

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
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	fctrl = &pwlanhdr->frame_control;
	*(fctrl) = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	switch (cur_network->network.InfrastructureMode) {
	case Ndis802_11Infrastructure:
		SetToDs(fctrl);
		memcpy(pwlanhdr->addr1, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&adapt->eeprompriv), ETH_ALEN);
		memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
		break;
	case Ndis802_11APMode:
		SetFrDs(fctrl);
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
		memcpy(pwlanhdr->addr3, myid(&adapt->eeprompriv), ETH_ALEN);
		break;
	case Ndis802_11IBSS:
	default:
		memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
		memcpy(pwlanhdr->addr2, myid(&adapt->eeprompriv), ETH_ALEN);
		memcpy(pwlanhdr->addr3, get_my_bssid(&pmlmeinfo->network), ETH_ALEN);
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

		pktlen = sizeof(struct ieee80211_qos_hdr);
	}

	*pLength = pktlen;
}

static void ConstructProbeRsp(struct adapter *adapt, u8 *pframe, u32 *pLength, u8 *StaAddr, bool bHideSSID)
{
	struct ieee80211_hdr *pwlanhdr;
	__le16 *fctrl;
	u8 *mac, *bssid;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	struct wlan_bssid_ex	*cur_network = &pmlmeinfo->network;

	pwlanhdr = (struct ieee80211_hdr *)pframe;

	mac = myid(&adapt->eeprompriv);
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

/*  To check if reserved page content is destroyed by beacon because beacon is too large. */
/*  2010.06.23. Added by tynli. */
void CheckFwRsvdPageContent(struct adapter *Adapter)
{
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

	ReservedPagePacket = kzalloc(1000, GFP_KERNEL);
	if (!ReservedPagePacket)
		return;

	pxmitpriv = &adapt->xmitpriv;
	pmlmeext = &adapt->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

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

	BufIndex += PageNeed * 128;

	/* 3 (2) ps-poll *1 page */
	RsvdPageLoc.LocPsPoll = PageNum;
	ConstructPSPoll(adapt, &ReservedPagePacket[BufIndex], &PSPollLength);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex - TxDescLen], PSPollLength, true, false);

	PageNeed = (u8)PageNum_128(TxDescLen + PSPollLength);
	PageNum += PageNeed;

	BufIndex += PageNeed * 128;

	/* 3 (3) null data * 1 page */
	RsvdPageLoc.LocNullData = PageNum;
	ConstructNullFunctionData(adapt, &ReservedPagePacket[BufIndex], &NullDataLength, get_my_bssid(&pmlmeinfo->network), false, 0, 0, false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex - TxDescLen], NullDataLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + NullDataLength);
	PageNum += PageNeed;

	BufIndex += PageNeed * 128;

	/* 3 (4) probe response * 1page */
	RsvdPageLoc.LocProbeRsp = PageNum;
	ConstructProbeRsp(adapt, &ReservedPagePacket[BufIndex], &ProbeRspLength, get_my_bssid(&pmlmeinfo->network), false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex - TxDescLen], ProbeRspLength, false, false);

	PageNeed = (u8)PageNum_128(TxDescLen + ProbeRspLength);
	PageNum += PageNeed;

	BufIndex += PageNeed * 128;

	/* 3 (5) Qos null data */
	RsvdPageLoc.LocQosNull = PageNum;
	ConstructNullFunctionData(adapt, &ReservedPagePacket[BufIndex],
				  &QosNullLength, get_my_bssid(&pmlmeinfo->network), true, 0, 0, false);
	rtl8188e_fill_fake_txdesc(adapt, &ReservedPagePacket[BufIndex - TxDescLen], QosNullLength, false, false);

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

	rtl8188eu_mgnt_xmit(adapt, pmgntframe);

	FillH2CCmd_88E(adapt, H2C_COM_RSVD_PAGE, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);

exit:
	kfree(ReservedPagePacket);
}

void rtl8188e_set_FwJoinBssReport_cmd(struct adapter *adapt, u8 mstatus)
{
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct mlme_ext_priv *pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	bool	bSendBeacon = false;
	bool	bcn_valid = false;
	u8 DLBcnCount = 0;
	u32 poll = 0;

	if (mstatus == 1) {
		/*  We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C. */
		/*  Suggested by filen. Added by tynli. */
		rtw_write16(adapt, REG_BCN_PSR_RPT, (0xC000 | pmlmeinfo->aid));
		/*  Do not set TSF again here or vWiFi beacon DMA INT will not work. */

		/* Set REG_CR bit 8. DMA beacon by SW. */
		haldata->RegCR_1 |= BIT(0);
		rtw_write8(adapt,  REG_CR + 1, haldata->RegCR_1);

		/*  Disable Hw protection for a time which revserd for Hw sending beacon. */
		/*  Fix download reserved page packet fail that access collision with the protection time. */
		/*  2010.05.11. Added by tynli. */
		rtw_write8(adapt, REG_BCN_CTRL, rtw_read8(adapt, REG_BCN_CTRL) & (~BIT(3)));
		rtw_write8(adapt, REG_BCN_CTRL, rtw_read8(adapt, REG_BCN_CTRL) | BIT(4));

		if (haldata->RegFwHwTxQCtrl & BIT(6))
			bSendBeacon = true;

		/*  Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame. */
		rtw_write8(adapt, REG_FWHW_TXQ_CTRL + 2, (haldata->RegFwHwTxQCtrl & (~BIT(6))));
		haldata->RegFwHwTxQCtrl &= (~BIT(6));

		clear_beacon_valid_bit(adapt);
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
				bcn_valid = get_beacon_valid_bit(adapt);
				poll++;
			} while (!bcn_valid && (poll % 10) != 0 && !adapt->bSurpriseRemoved && !adapt->bDriverStopped);
		} while (!bcn_valid && DLBcnCount <= 100 && !adapt->bSurpriseRemoved && !adapt->bDriverStopped);

		/*  */
		/*  We just can send the reserved page twice during the time that Tx thread is stopped (e.g. pnpsetpower) */
		/*  because we need to free the Tx BCN Desc which is used by the first reserved page packet. */
		/*  At run time, we cannot get the Tx Desc until it is released in TxHandleInterrupt() so we will return */
		/*  the beacon TCB in the following code. 2011.11.23. by tynli. */
		/*  */

		/*  Enable Bcn */
		rtw_write8(adapt, REG_BCN_CTRL, rtw_read8(adapt, REG_BCN_CTRL) | BIT(3));
		rtw_write8(adapt, REG_BCN_CTRL, rtw_read8(adapt, REG_BCN_CTRL) & (~BIT(4)));

		/*  To make sure that if there exists an adapter which would like to send beacon. */
		/*  If exists, the origianl value of 0x422[6] will be 1, we should check this to */
		/*  prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
		/*  the beacon cannot be sent by HW. */
		/*  2010.06.23. Added by tynli. */
		if (bSendBeacon) {
			rtw_write8(adapt, REG_FWHW_TXQ_CTRL + 2, (haldata->RegFwHwTxQCtrl | BIT(6)));
			haldata->RegFwHwTxQCtrl |= BIT(6);
		}

		/*  Update RSVD page location H2C to Fw. */
		if (bcn_valid)
			clear_beacon_valid_bit(adapt);

		/*  Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli. */
		/*  Clear CR[8] or beacon packet will not be send to TxBuf anymore. */
		haldata->RegCR_1 &= (~BIT(0));
		rtw_write8(adapt,  REG_CR + 1, haldata->RegCR_1);
	}

}

void rtl8188e_set_p2p_ps_offload_cmd(struct adapter *adapt, u8 p2p_ps_state)
{
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct wifidirect_info	*pwdinfo = &adapt->wdinfo;
	struct P2P_PS_Offload_t	*p2p_ps_offload = &haldata->p2p_ps_offload;
	u8 i;

	switch (p2p_ps_state) {
	case P2P_PS_DISABLE:
		memset(p2p_ps_offload, 0, 1);
		break;
	case P2P_PS_ENABLE:
		/*  update CTWindow value. */
		if (pwdinfo->ctwindow > 0) {
			p2p_ps_offload->CTWindow_En = 1;
			rtw_write8(adapt, REG_P2P_CTWIN, pwdinfo->ctwindow);
		}

		/*  hw only support 2 set of NoA */
		for (i = 0; i < pwdinfo->noa_num; i++) {
			/*  To control the register setting for which NOA */
			rtw_write8(adapt, REG_NOA_DESC_SEL, (i << 4));
			if (i == 0)
				p2p_ps_offload->NoA0_En = 1;
			else
				p2p_ps_offload->NoA1_En = 1;

			/*  config P2P NoA Descriptor Register */
			rtw_write32(adapt, REG_NOA_DESC_DURATION, pwdinfo->noa_duration[i]);
			rtw_write32(adapt, REG_NOA_DESC_INTERVAL, pwdinfo->noa_interval[i]);
			rtw_write32(adapt, REG_NOA_DESC_START, pwdinfo->noa_start_time[i]);
			rtw_write8(adapt, REG_NOA_DESC_COUNT, pwdinfo->noa_count[i]);
		}

		if ((pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0)) {
			/*  rst p2p circuit */
			rtw_write8(adapt, REG_DUAL_TSF_RST, BIT(4));

			p2p_ps_offload->Offload_En = 1;

			if (pwdinfo->role == P2P_ROLE_GO) {
				p2p_ps_offload->role = 1;
				p2p_ps_offload->AllStaSleep = 0;
			} else {
				p2p_ps_offload->role = 0;
			}

			p2p_ps_offload->discovery = 0;
		}
		break;
	case P2P_PS_SCAN:
		p2p_ps_offload->discovery = 1;
		break;
	case P2P_PS_SCAN_DONE:
		p2p_ps_offload->discovery = 0;
		pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
		break;
	default:
		break;
	}

	FillH2CCmd_88E(adapt, H2C_PS_P2P_OFFLOAD, 1, (u8 *)p2p_ps_offload);
}
