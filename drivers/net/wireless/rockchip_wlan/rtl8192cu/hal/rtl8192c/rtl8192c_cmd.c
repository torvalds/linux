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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8192C_CMD_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtw_byteorder.h>
#include <circ_buf.h>
#include <rtw_ioctl_set.h>

#include <rtl8192c_hal.h>


#define RTL92C_MAX_H2C_BOX_NUMS	4
#define RTL92C_MAX_CMD_LEN	5
#define MESSAGE_BOX_SIZE		4
#define EX_MESSAGE_BOX_SIZE	2

static u8 _is_fw_read_cmd_down(_adapter* padapter, u8 msgbox_num)
{
	u8	read_down = _FALSE;
	int 	retry_cnts = 100;
	
	u8 valid;

//	DBG_8192C(" _is_fw_read_cmd_down ,isnormal_chip(%x),reg_1cc(%x),msg_box(%d)...\n",isvern,rtw_read8(padapter,REG_HMETFR),msgbox_num);
	
	do{
		valid = rtw_read8(padapter,REG_HMETFR) & BIT(msgbox_num);
		if(0 == valid ){
			read_down = _TRUE;
		}			
	}while( (!read_down) && (retry_cnts--));

	return read_down;
	
}


/*****************************************
* H2C Msg format :
*| 31 - 8		|7		| 6 - 0	|	
*| h2c_msg 	|Ext_bit	|CMD_ID	|
*
******************************************/
int rtl8192c_FillH2CCmd(_adapter* padapter, u8 ElementID, u32 CmdLen, u8* pCmdBuffer)
{
	u8	bcmd_down = _FALSE;
	int 	retry_cnts = 100;
	u8	h2c_box_num;
	u32	msgbox_addr;
	u32  msgbox_ex_addr;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32	h2c_cmd = 0;
	u16	h2c_cmd_ex = 0;
	int ret = _FAIL;

_func_enter_;	

	padapter = GET_PRIMARY_ADAPTER(padapter);		
	pHalData = GET_HAL_DATA(padapter);

	if(padapter->bFWReady == _FALSE)
	{
		DBG_8192C("FillH2CCmd(): return H2C cmd because fw is not ready\n");
		return ret;
	}

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

	if(!pCmdBuffer){
		goto exit;
	}
	if(CmdLen > RTL92C_MAX_CMD_LEN){
		goto exit;
	}
	//pay attention to if  race condition happened in  H2C cmd setting.
	do{
		h2c_box_num = pHalData->LastHMEBoxNum;

		if(!_is_fw_read_cmd_down(padapter, h2c_box_num)){
			DBG_8192C(" fw read cmd failed...\n");
			goto exit;
		}

		if(CmdLen<=3)
		{
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer, CmdLen );
		}
		else{
			_rtw_memcpy((u8*)(&h2c_cmd_ex), pCmdBuffer, EX_MESSAGE_BOX_SIZE);
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer+2,( CmdLen-EX_MESSAGE_BOX_SIZE));
			*(u8*)(&h2c_cmd) |= BIT(7);
		}

		*(u8*)(&h2c_cmd) |= ElementID;

		if(h2c_cmd & BIT(7)){
			msgbox_ex_addr = REG_HMEBOX_EXT_0 + (h2c_box_num *EX_MESSAGE_BOX_SIZE);
			h2c_cmd_ex = cpu_to_le16( h2c_cmd_ex );
			rtw_write16(padapter, msgbox_ex_addr, h2c_cmd_ex);
		}
		msgbox_addr =REG_HMEBOX_0 + (h2c_box_num *MESSAGE_BOX_SIZE);
		h2c_cmd = cpu_to_le32( h2c_cmd );
		rtw_write32(padapter,msgbox_addr, h2c_cmd);

		bcmd_down = _TRUE;

	//	DBG_8192C("MSG_BOX:%d,CmdLen(%d), reg:0x%x =>h2c_cmd:0x%x, reg:0x%x =>h2c_cmd_ex:0x%x ..\n"
	//	 	,pHalData->LastHMEBoxNum ,CmdLen,msgbox_addr,h2c_cmd,msgbox_ex_addr,h2c_cmd_ex);

		 pHalData->LastHMEBoxNum = (h2c_box_num+1) % RTL92C_MAX_H2C_BOX_NUMS ;

	}while((!bcmd_down) && (retry_cnts--));
/*
	if(bcmd_down)
		DBG_8192C("H2C Cmd exe down. \n"	);	
	else
		DBG_8192C("H2C Cmd exe failed. \n"	);
*/
	ret = _SUCCESS;

exit:

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);	

_func_exit_;

	return ret;

}

u8 rtl8192c_h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf)
{	
	u8 ElementID, CmdLen;
	u8 *pCmdBuffer;
	struct cmd_msg_parm  *pcmdmsg;
	
	if(!pbuf)
		return H2C_PARAMETERS_ERROR;

	pcmdmsg = (struct cmd_msg_parm*)pbuf;
	ElementID = pcmdmsg->eid;
	CmdLen = pcmdmsg->sz;
	pCmdBuffer = pcmdmsg->buf;

	rtl8192c_FillH2CCmd(padapter, ElementID, CmdLen, pCmdBuffer);

	return H2C_SUCCESS;
}

#if defined(CONFIG_AUTOSUSPEND) && defined(SUPPORT_HW_RFOFF_DETECTED)
u8 rtl8192c_set_FwSelectSuspend_cmd(_adapter *padapter ,u8 bfwpoll, u16 period)
{
	u8	res=_SUCCESS;
	struct H2C_SS_RFOFF_PARAM param;
	DBG_8192C("==>%s bfwpoll(%x)\n",__FUNCTION__,bfwpoll);
	param.gpio_period = period;//Polling GPIO_11 period time
	param.ROFOn = (_TRUE == bfwpoll)?1:0;
	rtl8192c_FillH2CCmd(padapter, SELECTIVE_SUSPEND_ROF_CMD, sizeof(param), (u8*)(&param));		
	return res;
}
#endif //CONFIG_AUTOSUSPEND && SUPPORT_HW_RFOFF_DETECTED

u8 rtl8192c_set_rssi_cmd(_adapter*padapter, u8 *param)
{	
	u8	res=_SUCCESS;

_func_enter_;

	*((u32*) param ) = cpu_to_le32( *((u32*) param ) );

	rtl8192c_FillH2CCmd(padapter, RSSI_SETTING_EID, 3, param);

_func_exit_;

	return res;
}

u8 rtl8192c_set_raid_cmd(_adapter*padapter, u32 mask, u8 arg)
{	
	u8	buf[5];
	u8	res=_SUCCESS;
	
_func_enter_;	
	
	_rtw_memset(buf, 0, 5);
	mask = cpu_to_le32( mask );
	_rtw_memcpy(buf, &mask, 4);
	buf[4]  = arg;

	rtl8192c_FillH2CCmd(padapter, MACID_CONFIG_EID, 5, buf);
	
_func_exit_;

	return res;

}

//bitmap[0:27] = tx_rate_bitmap
//bitmap[28:31]= Rate Adaptive id
//arg[0:4] = macid
//arg[5] = Short GI
void rtl8192c_Add_RateATid(PADAPTER pAdapter, u32 bitmap, u8 arg)
{	
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
		
	if(pHalData->fw_ractrl == _TRUE)
	{
		rtl8192c_set_raid_cmd(pAdapter, bitmap, arg);
	}
	else
	{
		u8 macid, init_rate, shortGIrate=_FALSE;

		init_rate = get_highest_rate_idx(bitmap&0x0fffffff)&0x3f;
		
		macid = arg&0x1f;
		
		shortGIrate = (arg&BIT(5)) ? _TRUE:_FALSE;
		
		if (shortGIrate==_TRUE)
			init_rate |= BIT(6);

		rtw_write8(pAdapter, (REG_INIDATA_RATE_SEL+macid), (u8)init_rate);		
	}

}

void rtl8192c_set_FwPwrMode_cmd(_adapter*padapter, u8 Mode)
{
	SETPWRMODE_PARM H2CSetPwrMode;
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	
_func_enter_;

	DBG_871X("%s(): Mode = %d, SmartPS = %d\n", __FUNCTION__,Mode,pwrpriv->smart_ps);

	H2CSetPwrMode.Mode = Mode;

	H2CSetPwrMode.SmartPS = pwrpriv->smart_ps;

	H2CSetPwrMode.BcnPassTime = 1;//pPSC->RegMaxLPSAwakeIntvl;

	rtl8192c_FillH2CCmd(padapter, SET_PWRMODE_EID, sizeof(H2CSetPwrMode), (u8 *)&H2CSetPwrMode);
	
_func_exit_;
}

void ConstructBeacon(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					rate_len, pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	u8	bc_addr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	
	_rtw_memcpy(pwlanhdr->addr1, bc_addr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(cur_network), ETH_ALEN);

	SetSeqNum(pwlanhdr, 0/*pmlmeext->mgnt_seq*/);
	//pmlmeext->mgnt_seq++;
	SetFrameSubType(pframe, WIFI_BEACON);
	
	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);	
	pktlen = sizeof (struct rtw_ieee80211_hdr_3addr);
	
	//timestamp will be inserted by hardware
	pframe += 8;
	pktlen += 8;

	// beacon interval: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_beacon_interval_from_ie(cur_network->IEs)), 2); 

	pframe += 2;
	pktlen += 2;

	// capability info: 2 bytes
	_rtw_memcpy(pframe, (unsigned char *)(rtw_get_capability_from_ie(cur_network->IEs)), 2);

	pframe += 2;
	pktlen += 2;

	if( (pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
	{
		//DBG_871X("ie len=%d\n", cur_network->IELength);
		pktlen += cur_network->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		_rtw_memcpy(pframe, cur_network->IEs+sizeof(NDIS_802_11_FIXED_IEs), pktlen);
		
		goto _ConstructBeacon;
	}

	//below for ad-hoc mode

	// SSID
	pframe = rtw_set_ie(pframe, _SSID_IE_, cur_network->Ssid.SsidLength, cur_network->Ssid.Ssid, &pktlen);

	// supported rates...
	rate_len = rtw_get_rateset_len(cur_network->SupportedRates);
	pframe = rtw_set_ie(pframe, _SUPPORTEDRATES_IE_, ((rate_len > 8)? 8: rate_len), cur_network->SupportedRates, &pktlen);

	// DS parameter set
	pframe = rtw_set_ie(pframe, _DSSET_IE_, 1, (unsigned char *)&(cur_network->Configuration.DSConfig), &pktlen);

	if( (pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE)
	{
		u32 ATIMWindow;
		// IBSS Parameter Set...
		//ATIMWindow = cur->Configuration.ATIMWindow;
		ATIMWindow = 0;
		pframe = rtw_set_ie(pframe, _IBSS_PARA_IE_, 2, (unsigned char *)(&ATIMWindow), &pktlen);
	}	


	//todo: ERP IE
	
	
	// EXTERNDED SUPPORTED RATE
	if (rate_len > 8)
	{
		pframe = rtw_set_ie(pframe, _EXT_SUPPORTEDRATES_IE_, (rate_len - 8), (cur_network->SupportedRates + 8), &pktlen);
	}


	//todo:HT for adhoc

_ConstructBeacon:

	if ((pktlen + TXDESC_SIZE) > 512)
	{
		DBG_871X("beacon frame too large\n");
		return;
	}

	*pLength = pktlen;

	//DBG_871X("%s bcn_sz=%d\n", __FUNCTION__, pktlen);

}

void ConstructPSPoll(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//DBG_871X("%s\n", __FUNCTION__);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	// Frame control.
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	SetPwrMgt(fctrl);
	SetFrameSubType(pframe, WIFI_PSPOLL);

	// AID.
	SetDuration(pframe, (pmlmeinfo->aid | 0xc000));

	// BSSID.
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	// TA.
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);

	*pLength = 16;
}

void ConstructNullFunctionData(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
	u32					pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network	*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;

	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	if (bForcePowerSave)
	{
		SetPwrMgt(fctrl);
	}

	switch(cur_network->network.InfrastructureMode)
	{			
		case Ndis802_11Infrastructure:
			SetToDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
			break;
		case Ndis802_11APMode:
			SetFrDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, myid(&(padapter->eeprompriv)), ETH_ALEN);
			break;
		case Ndis802_11IBSS:
		default:
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			break;
	}

	SetSeqNum(pwlanhdr, 0);

	SetFrameSubType(pframe, WIFI_DATA_NULL);

	pframe += sizeof(struct rtw_ieee80211_hdr_3addr);
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);

	*pLength = pktlen;
}

void ConstructProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bHideSSID)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;	
	u8					*mac, *bssid;
	u32					pktlen;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX 		*cur_network = &(pmlmeinfo->network);
	
	
	//DBG_871X("%s\n", __FUNCTION__);
	
	pwlanhdr = (struct rtw_ieee80211_hdr *)pframe;	
	
	mac = myid(&(padapter->eeprompriv));
	bssid = cur_network->MacAddress;
	
	fctrl = &(pwlanhdr->frame_ctl);
	*(fctrl) = 0;
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, mac, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, bssid, ETH_ALEN);

	SetSeqNum(pwlanhdr, 0);
	SetFrameSubType(fctrl, WIFI_PROBERSP);
	
	pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	pframe += pktlen;

	if(cur_network->IELength>MAX_IE_SZ)
		return;

	_rtw_memcpy(pframe, cur_network->IEs, cur_network->IELength);
	pframe += cur_network->IELength;
	pktlen += cur_network->IELength;
	
	*pLength = pktlen;
}

//
// Description: In normal chip, we should send some packet to Hw which will be used by Fw
//			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then 
//			Fw can tell Hw to send these packet derectly.
// Added by tynli. 2009.10.15.
//
static VOID
FillFakeTxDescriptor92C(
	IN PADAPTER		Adapter,
	IN u8*			pDesc,
	IN u32			BufferLen,
	IN BOOLEAN		IsPsPoll
)
{
	struct tx_desc	*ptxdesc = (struct tx_desc *)pDesc;

	// Clear all status
	_rtw_memset(pDesc, 0, 32);

	//offset 0
	ptxdesc->txdw0 |= cpu_to_le32( OWN | FSG | LSG); //own, bFirstSeg, bLastSeg;

	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000); //32 bytes for TX Desc

	ptxdesc->txdw0 |= cpu_to_le32(BufferLen&0x0000ffff); // Buffer size + command header

	//offset 4
	ptxdesc->txdw1 |= cpu_to_le32((QSLT_MGNT<<QSEL_SHT)&0x00001f00); // Fixed queue of Mgnt queue

	//Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw.
	if(IsPsPoll)
	{
		ptxdesc->txdw1 |= cpu_to_le32(NAVUSEHDR);
	}
	else
	{
		ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); // Hw set sequence number
		ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); //set bit3 to 1. Suugested by TimChen. 2009.12.29.
	}

	//offset 16
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));//driver uses rate

#ifdef CONFIG_USB_HCI
	// USB interface drop packet if the checksum of descriptor isn't correct.
	// Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	rtl8192cu_cal_txdesc_chksum(ptxdesc);
#endif
	
	//RT_PRINT_DATA(COMP_CMD, DBG_TRACE, "TxFillCmdDesc8192C(): H2C Tx Cmd Content ----->\n", pDesc, TX_DESC_SIZE);
}

// To check if reserved page content is destroyed by beacon beacuse beacon is too large.
// 2010.06.23. Added by tynli.
VOID
CheckFwRsvdPageContent(
	IN	PADAPTER		Adapter
)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);
	u32	MaxBcnPageNum;	
	
 	if(pHalData->FwRsvdPageStartOffset != 0)
 	{
 		/*MaxBcnPageNum = PageNum_128(pMgntInfo->MaxBeaconSize);
		RT_ASSERT((MaxBcnPageNum <= pHalData->FwRsvdPageStartOffset), 
			("CheckFwRsvdPageContent(): The reserved page content has been"\
			"destroyed by beacon!!! MaxBcnPageNum(%d) FwRsvdPageStartOffset(%d)\n!",
			MaxBcnPageNum, pHalData->FwRsvdPageStartOffset));*/
 	}
}

//
// Description: Fill the reserved packets that FW will use to RSVD page. 
//			Now we just send 4 types packet to rsvd page.
//			(1)Beacon, (2)Ps-poll, (3)Null data, (4)ProbeRsp.
//	Input: 
//	    bDLFinished - FALSE: At the first time we will send all the packets as a large packet to Hw,
//				 		so we need to set the packet length to total lengh.
//			      TRUE: At the second time, we should send the first packet (default:beacon)
//						to Hw again and set the lengh in descriptor to the real beacon lengh.
// 2009.10.15 by tynli.
static void SetFwRsvdPagePkt(PADAPTER Adapter, BOOLEAN bDLFinished)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	struct xmit_priv	*pxmitpriv = &(Adapter->xmitpriv);
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32	BeaconLength, ProbeRspLength, PSPollLength, NullFunctionDataLength;
	u8	*ReservedPagePacket;
	u8	PageNum=0, U1bTmp, TxDescLen=0, TxDescOffset=0;
	u16	BufIndex=0;
	u32	TotalPacketLen;
	RSVDPAGE_LOC	RsvdPageLoc;
	BOOLEAN	bDLOK = _FALSE;

	DBG_871X("%s\n", __FUNCTION__);

	ReservedPagePacket = (u8*)rtw_malloc(1000);
	if(ReservedPagePacket == NULL){
		DBG_871X("%s(): alloc ReservedPagePacket fail !!!\n", __FUNCTION__);
		return;
	}

	_rtw_memset(ReservedPagePacket, 0, 1000);

	TxDescLen = 32;//TX_DESC_SIZE;

#ifdef CONFIG_USB_HCI
	BufIndex = TXDESC_OFFSET;
	TxDescOffset = TxDescLen+8; //Shift index for 8 bytes because the dummy bytes in the first descipstor.
#else
	BufIndex = 0;
	TxDescOffset = 0;
#endif

	//(1) beacon
	ConstructBeacon(Adapter,&ReservedPagePacket[BufIndex],&BeaconLength);

	//DBG_8192C("SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: BCN\n", &ReservedPagePacket[BufIndex], (BeaconLength+BufIndex));

//--------------------------------------------------------------------

	// When we count the first page size, we need to reserve description size for the RSVD 
	// packet, it will be filled in front of the packet in TXPKTBUF.
	U1bTmp = (u8)PageNum_128(BeaconLength+TxDescLen);
	PageNum += U1bTmp;
	// To reserved 2 pages for beacon buffer. 2010.06.24.
	if(PageNum == 1)
		PageNum+=1;
	pHalData->FwRsvdPageStartOffset = PageNum;

	BufIndex = (PageNum*128) + TxDescOffset;
		
	//(2) ps-poll
	ConstructPSPoll(Adapter, &ReservedPagePacket[BufIndex],&PSPollLength);
	
	FillFakeTxDescriptor92C(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, _TRUE);

	//DBG_8192C("SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: PS-POLL\n", &ReservedPagePacket[BufIndex-TxDescLen], (PSPollLength+TxDescLen));

	RsvdPageLoc.LocPsPoll = PageNum;

//------------------------------------------------------------------
			
	U1bTmp = (u8)PageNum_128(PSPollLength+TxDescLen);
	PageNum += U1bTmp;

	BufIndex = (PageNum*128) + TxDescOffset;

	//(3) null data
	ConstructNullFunctionData(
		Adapter, 
		&ReservedPagePacket[BufIndex],
		&NullFunctionDataLength,
		get_my_bssid(&(pmlmeinfo->network)),
		_FALSE);
	
	FillFakeTxDescriptor92C(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], NullFunctionDataLength, _FALSE);

	RsvdPageLoc.LocNullData = PageNum;
	
	//DBG_8192C("SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: NULL DATA \n", &ReservedPagePacket[BufIndex-TxDescLen], (NullFunctionDataLength+TxDescLen));
//------------------------------------------------------------------

	U1bTmp = (u8)PageNum_128(NullFunctionDataLength+TxDescLen);
	PageNum += U1bTmp;

	BufIndex = (PageNum*128) + TxDescOffset;
	
	//(4) probe response
	ConstructProbeRsp(
		Adapter, 
		&ReservedPagePacket[BufIndex],
		&ProbeRspLength,
		get_my_bssid(&(pmlmeinfo->network)),
		_FALSE);
	
	FillFakeTxDescriptor92C(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, _FALSE);

	RsvdPageLoc.LocProbeRsp = PageNum;

	//DBG_8192C("SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: PROBE RSP \n", &ReservedPagePacket[BufIndex-TxDescLen], (ProbeRspLength-TxDescLen));

//------------------------------------------------------------------

	U1bTmp = (u8)PageNum_128(ProbeRspLength+TxDescLen);

	PageNum += U1bTmp;

	TotalPacketLen = (PageNum*128);

	if ((pmgntframe = alloc_mgtxmitframe(pxmitpriv)) == NULL)
	{
		return;
	}

	//update attribute
	pattrib = &pmgntframe->attrib;
	update_mgntframe_attrib(Adapter, pattrib);
	pattrib->qsel = 0x10;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescLen;
	_rtw_memcpy(pmgntframe->buf_addr, ReservedPagePacket, TotalPacketLen);

	rtw_hal_mgnt_xmit(Adapter, pmgntframe);

	bDLOK = _TRUE;

	if(bDLOK)
	{
		DBG_871X("Set RSVD page location to Fw.\n");
		rtl8192c_FillH2CCmd(Adapter, RSVD_PAGE_EID, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);
	}

	rtw_mfree(ReservedPagePacket,1000);

}

void rtl8192c_set_FwJoinBssReport_cmd(_adapter* padapter, u8 mstatus)
{
	JOINBSSRPT_PARM	JoinBssRptParm;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
_func_enter_;

	DBG_871X("%s mstatus(%x)\n", __FUNCTION__,mstatus);

	if(mstatus == 1)
	{
		BOOLEAN bRecover = _FALSE;
	
		// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		// Suggested by filen. Added by tynli.
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		// Do not set TSF again here or vWiFi beacon DMA INT will not work.
		//correct_TSF(padapter, pmlmeext);
		// Hw sequende enable by dedault. 2010.06.23. by tynli.
		//rtw_write16(padapter, REG_NQOS_SEQ, ((pmlmeext->mgnt_seq+100)&0xFFF));
		//rtw_write8(padapter, REG_HWSEQ_CTRL, 0xFF);

		//set REG_CR bit 8
		//U1bTmp = rtw_read8(padapter, REG_CR+1);
		rtw_write8(padapter,  REG_CR+1, 0x03);

		// Disable Hw protection for a time which revserd for Hw sending beacon.
		// Fix download reserved page packet fail that access collision with the protection time.
		// 2010.05.11. Added by tynli.
		//SetBcnCtrlReg(padapter, 0, BIT3);
		//SetBcnCtrlReg(padapter, BIT4, 0);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~BIT(3)));
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(4));

		// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
		if(pHalData->RegFwHwTxQCtrl&BIT6)
			bRecover = _TRUE;

		// To tell Hw the packet is not a real beacon frame.
		//U1bTmp = rtw_read8(padapter, REG_FWHW_TXQ_CTRL+2);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT6)));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);
		SetFwRsvdPagePkt(padapter, 0);

		// 2010.05.11. Added by tynli.
		//SetBcnCtrlReg(padapter, BIT3, 0);
		//SetBcnCtrlReg(padapter, 0, BIT4);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(3));
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~BIT(4)));

		// To make sure that if there exists an adapter which would like to send beacon.
		// If exists, the origianl value of 0x422[6] will be 1, we should check this to
		// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause 
		// the beacon cannot be sent by HW.
		// 2010.06.23. Added by tynli.
		if(bRecover)
		{
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl|BIT6));
			pHalData->RegFwHwTxQCtrl |= BIT6;
		}

		// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
		rtw_write8(padapter, REG_CR+1, 0x02);
	}

	JoinBssRptParm.OpMode = mstatus;

	rtl8192c_FillH2CCmd(padapter, JOINBSS_RPT_EID, sizeof(JoinBssRptParm), (u8 *)&JoinBssRptParm);
	
_func_exit_;
}

#ifdef CONFIG_P2P_PS
void rtl8192c_set_p2p_ctw_period_cmd(_adapter* padapter, u8 ctwindow)
{
	struct P2P_PS_CTWPeriod_t p2p_ps_ctw;

	p2p_ps_ctw.CTWPeriod = ctwindow;

	rtl8192c_FillH2CCmd(padapter, P2P_PS_CTW_CMD_EID, 1, (u8 *)(&p2p_ps_ctw));
	
}

void rtl8192c_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct pwrctrl_priv		*pwrpriv = &padapter->pwrctrlpriv;
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	struct P2P_PS_Offload_t	*p2p_ps_offload = &pHalData->p2p_ps_offload;
	u8	i;
	u16	ctwindow;
	u32	start_time, tsf_low;

_func_enter_;

	switch(p2p_ps_state)
	{
		case P2P_PS_DISABLE:
			DBG_8192C("P2P_PS_DISABLE \n");
			_rtw_memset(p2p_ps_offload, 0 ,1);
			break;
		case P2P_PS_ENABLE:
			DBG_8192C("P2P_PS_ENABLE \n");
			// update CTWindow value.
			if( pwdinfo->ctwindow > 0 )
			{
				p2p_ps_offload->CTWindow_En = 1;
				ctwindow = pwdinfo->ctwindow;
				if(IS_HARDWARE_TYPE_8723A(padapter))
				{
					//rtw_write16(padapter, REG_ATIMWND, ctwindow);
				}
				else
				{
					rtl8192c_set_p2p_ctw_period_cmd(padapter, ctwindow);
				}
			}

			// hw only support 2 set of NoA
			for( i=0 ; i<pwdinfo->noa_num ; i++)
			{
				// To control the register setting for which NOA
				rtw_write8(padapter, 0x5CF, (i << 4));
				if(i == 0)
					p2p_ps_offload->NoA0_En = 1;
				else
					p2p_ps_offload->NoA1_En = 1;

				// config P2P NoA Descriptor Register
				rtw_write32(padapter, 0x5E0, pwdinfo->noa_duration[i]);

				rtw_write32(padapter, 0x5E4, pwdinfo->noa_interval[i]);

				//Get Current TSF value
				tsf_low = rtw_read32(padapter, REG_TSFTR);

				start_time = pwdinfo->noa_start_time[i];
				if(pwdinfo->noa_count[i] != 1)
				{
					while( start_time <= (tsf_low+(50*1024) ) )
					{
						start_time += pwdinfo->noa_interval[i];
						if(pwdinfo->noa_count[i] != 255)
							pwdinfo->noa_count[i]--;
					}
				}
				//DBG_8192C("%s(): start_time = %x\n",__FUNCTION__,start_time);
				rtw_write32(padapter, 0x5E8, start_time);

				rtw_write8(padapter, 0x5EC, pwdinfo->noa_count[i]);
			}

			if( (pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0) )
			{
				// rst p2p circuit
				rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(4));

				p2p_ps_offload->Offload_En = 1;

				if(rtw_p2p_chk_role(pwdinfo, P2P_ROLE_GO))
				{
					p2p_ps_offload->role= 1;
					p2p_ps_offload->AllStaSleep = 0;
				}
				else
				{
					p2p_ps_offload->role= 0;
				}

				p2p_ps_offload->discovery = 0;
			}
			break;
		case P2P_PS_SCAN:
			DBG_8192C("P2P_PS_SCAN \n");
			p2p_ps_offload->discovery = 1;
			break;
		case P2P_PS_SCAN_DONE:
			DBG_8192C("P2P_PS_SCAN_DONE \n");
			p2p_ps_offload->discovery = 0;
			pwdinfo->p2p_ps_state = P2P_PS_ENABLE;
			break;
		default:
			break;
	}

	rtl8192c_FillH2CCmd(padapter, P2P_PS_OFFLOAD_EID, 1, (u8 *)p2p_ps_offload);

_func_exit_;

}
#endif // CONFIG_P2P_PS

#ifdef CONFIG_IOL
#include <rtw_iol.h>
int rtl8192c_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms)
{
	IO_OFFLOAD_LOC	IoOffloadLoc;
	u32 start_time = rtw_get_current_time();
	u32 passing_time_ms;
	u8 polling_ret;
	int ret = _FAIL;

	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;

	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);

	IoOffloadLoc.LocCmd = 0;
	if(_SUCCESS != rtl8192c_FillH2CCmd(adapter, H2C_92C_IO_OFFLOAD, sizeof(IO_OFFLOAD_LOC), (u8 *)&IoOffloadLoc))
		goto exit;

	//polling if the IO offloading is done
	while( (passing_time_ms=rtw_get_passing_time_ms(start_time)) <= max_wating_ms) {
		#if 0 //C2H
		if(0xff == rtw_read8(adapter, REG_C2HEVT_CLEAR))
			break;
		#else// 0x1c3
		if(0x00 != (polling_ret=rtw_read8(adapter, 0x1c3)))
			break;
		#endif
		rtw_msleep_os(5);
	}
	#if 0 //debug
	DBG_871X("IOL %s, polling_ret:0x%02x, 0x1c0=0x%08x, 0x1c4=0x%08x, 0x1cc=0x%08x, 0x1e8=0x%08x, 0x130=0x%08x, 0x134=0x%08x\n"
			, polling_ret==0xff?"success":"error"
			, polling_ret
			, rtw_read32(adapter, 0x1c0)
			, rtw_read32(adapter, 0x1c4)
			, rtw_read32(adapter, 0x1cc)
			, rtw_read32(adapter, 0x1e8)
			, rtw_read32(adapter, 0x130)
			, rtw_read32(adapter, 0x134)
	);
	rtw_write32(adapter, 0x1c0, 0x0);
	#endif

	if(polling_ret == 0xff)
		ret =_SUCCESS;
	else {
		DBG_871X("IOL %s, polling_ret:0x%02x\n"
			//", 0x1c0=0x%08x, 0x1c4=0x%08x, 0x1cc=0x%08x, 0x1e8=0x%08x, 0x130=0x%08x, 0x134=0x%08x\n"
			, polling_ret==0xff?"success":"error"
			, polling_ret
			//, rtw_read32(adapter, 0x1c0)
			//, rtw_read32(adapter, 0x1c4)
			//, rtw_read32(adapter, 0x1cc)
			//, rtw_read32(adapter, 0x1e8)
			//, rtw_read32(adapter, 0x130)
			//, rtw_read32(adapter, 0x134)
		);
		#if 0 //debug
		rtw_write16(adapter, 0x1c4, 0x0000); 
		rtw_msleep_os(10);
		DBG_871X("after reset, 0x1c4=0x%08x\n", rtw_read32(adapter, 0x1c4));
		#endif

	}

	{
		#if 0 //C2H
		u32 c2h_evt;
		int i;
		c2h_evt = rtw_read32(adapter, REG_C2HEVT_MSG_NORMAL);
		DBG_871X("%s io-offloading complete, in %ums: 0x%08x\n", __FUNCTION__, passing_time_ms, c2h_evt);
		rtw_write8(adapter, REG_C2HEVT_CLEAR, 0x0);
		#else// 0x1c3
		DBG_871X("IOL %s complete in %ums\n", __FUNCTION__, passing_time_ms);
		rtw_write8(adapter, 0x1c3, 0x0);
		#endif
	}

exit:
	return ret;

}
#endif //CONFIG_IOL


#ifdef CONFIG_BEACON_DISABLE_OFFLOAD
/* 
 rtl8192c_dis_beacon_fun_cmd()
	This function shall only be called by PORT1.
	PORT0's beacon function can't be disabled, because it's used by RA function in FW/HW.
	
	// Still has the REG_BCN_CTRL_1 modified by unknowned party issue in case of Primary Interface + PORT1 combination.
*/
u8 rtl8192c_dis_beacon_fun_cmd(_adapter* padapter)
{	
	u8	buf[2];
	u8	res=_SUCCESS;
	
_func_enter_;	
	
	_rtw_memset(buf, 0, sizeof(buf));

	if (padapter->iface_type == IFACE_PORT0) {
		//buf[0] = 0x1;
		DBG_871X("%s(): ERROR! padapter->iface_type = %d\n", __FUNCTION__, padapter->iface_type);
		return _FAIL;		
	} else
		buf[1] = 0x1;

	rtl8192c_FillH2CCmd(padapter, H2C_92C_DISABLE_BCN_FUNC, 2, buf);
	
_func_exit_;

	return res;

}
#endif  // CONFIG_BEACON_DISABLE_OFFLOAD


#ifdef CONFIG_TSF_RESET_OFFLOAD
/*
	ask FW to Reset sync register at Beacon early interrupt
*/
u8 rtl8192c_reset_tsf(_adapter *padapter, u8 reset_port )
{	
	u8	buf[2];
	u8	res=_SUCCESS;
	
_func_enter_;
	if (IFACE_PORT0==reset_port) {
		buf[0] = 0x1; buf[1] = 0;
	
	} else{
		buf[0] = 0x0; buf[1] = 0x1;
	}
	rtl8192c_FillH2CCmd(padapter, H2C_92C_RESET_TSF, 2, buf);
_func_exit_;

	return res;
}

int reset_tsf(PADAPTER Adapter, u8 reset_port )
{
	u8 reset_cnt_before = 0, reset_cnt_after = 0, loop_cnt = 0;
	u32 reg_reset_tsf_cnt = (IFACE_PORT0==reset_port) ?
				REG_FW_RESET_TSF_CNT_0:REG_FW_RESET_TSF_CNT_1;

	rtw_scan_abort(Adapter->pbuddy_adapter);	/*	site survey will cause reset_tsf fail	*/
	reset_cnt_after = reset_cnt_before = rtw_read8(Adapter,reg_reset_tsf_cnt);
	rtl8192c_reset_tsf(Adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before ) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(Adapter, reg_reset_tsf_cnt);
	}

	return(loop_cnt >= 10) ? _FAIL : _TRUE;
}


#endif	// CONFIG_TSF_RESET_OFFLOAD

#ifdef CONFIG_WOWLAN

void rtl8192c_set_wowlan_cmd(_adapter* padapter)
{
	u8	res=_SUCCESS;
	SETWOWLAN_PARM pwowlan_parm;
	struct pwrctrl_priv *pwrpriv=&padapter->pwrctrlpriv;
	
_func_enter_;

	pwowlan_parm.mode =0;
	pwowlan_parm.gpio_index=0;
	pwowlan_parm.gpio_duration=0;
	pwowlan_parm.second_mode =0;
	pwowlan_parm.reserve=0;
	
	if(pwrpriv->wowlan_mode ==_TRUE){
		pwowlan_parm.mode |=FW_WOWLAN_FUN_EN;
		//printk("\n %s 1.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		if(pwrpriv->wowlan_pattern ==_TRUE){
			pwowlan_parm.mode |= FW_WOWLAN_PATTERN_MATCH;
		//printk("\n %s 2.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		if(pwrpriv->wowlan_magic ==_TRUE){
			pwowlan_parm.mode |=FW_WOWLAN_MAGIC_PKT;
		//printk("\n %s 3.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		if(pwrpriv->wowlan_unicast ==_TRUE){
			pwowlan_parm.mode |=FW_WOWLAN_UNICAST;
		//printk("\n %s 4.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		//WOWLAN_GPIO_ACTIVE means GPIO high active
		//pwowlan_parm.mode |=FW_WOWLAN_GPIO_ACTIVE;
		pwowlan_parm.mode |=FW_WOWLAN_REKEY_WAKEUP;
		pwowlan_parm.mode |=FW_WOWLAN_DEAUTH_WAKEUP;
		
		rtl8192c_set_FwJoinBssReport_cmd( padapter, 1);
		
		//GPIO3
		pwowlan_parm.gpio_index=3;
		
		//duration unit is 64us
		pwowlan_parm.gpio_duration=0xff;
		//
		pwowlan_parm.second_mode|=FW_WOWLAN_GPIO_WAKEUP_EN;
		//printk("\n %s 5.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		{	u8 *ptr=(u8 *)&pwowlan_parm;
			printk("\n %s H2C_WO_WLAN=%x %02x:%02x:%02x:%02x:%02x \n",__FUNCTION__,H2C_WO_WLAN_CMD,ptr[0],ptr[1],ptr[2],ptr[3],ptr[4] );
		}
		rtl8192c_FillH2CCmd(padapter, H2C_WO_WLAN_CMD, 4, (u8 *)&pwowlan_parm);

		//keep alive period = 3 * 10 BCN interval
		pwowlan_parm.mode =3;
		pwowlan_parm.gpio_index=3;
		rtl8192c_FillH2CCmd(padapter, KEEP_ALIVE_CONTROL_CMD, 2, (u8 *)&pwowlan_parm);
		printk("%s after KEEP_ALIVE_CONTROL_CMD register 0x81=%x \n",__FUNCTION__,rtw_read8(padapter, 0x81));

		pwowlan_parm.mode =1;
		pwowlan_parm.gpio_index=0;
		pwowlan_parm.gpio_duration=0;
		rtl8192c_FillH2CCmd(padapter, DISCONNECT_DECISION_CTRL_CMD, 3, (u8 *)&pwowlan_parm);
		printk("%s after DISCONNECT_DECISION_CTRL_CMD register 0x81=%x \n",__FUNCTION__,rtw_read8(padapter, 0x81));
		
		//enable GPIO wakeup
		pwowlan_parm.mode =1;
		pwowlan_parm.gpio_index=0;
		pwowlan_parm.gpio_duration=0;
		rtl8192c_FillH2CCmd(padapter, REMOTE_WAKE_CTRL_CMD, 3, (u8 *)&pwowlan_parm);
	}
	else
		rtl8192c_FillH2CCmd(padapter, H2C_WO_WLAN_CMD, 3, (u8 *)&pwowlan_parm);

	
_func_exit_;

	return ;

}

#endif  //CONFIG_WOWLAN





