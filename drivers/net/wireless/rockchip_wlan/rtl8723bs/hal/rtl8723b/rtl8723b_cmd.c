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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8723B_CMD_C_

#include <rtl8723b_hal.h>
#include "hal_com_h2c.h"

#define MAX_H2C_BOX_NUMS	4
#define MESSAGE_BOX_SIZE		4

#define RTL8723B_MAX_CMD_LEN	7
#define RTL8723B_EX_MESSAGE_BOX_SIZE	4

static u8 _is_fw_read_cmd_down(_adapter* padapter, u8 msgbox_num)
{
	u8	read_down = _FALSE;
	int 	retry_cnts = 100;

	u8 valid;

	//DBG_8192C(" _is_fw_read_cmd_down ,reg_1cc(%x),msg_box(%d)...\n",rtw_read8(padapter,REG_HMETFR),msgbox_num);

	do{
		valid = rtw_read8(padapter,REG_HMETFR) & BIT(msgbox_num);
		if(0 == valid ){
			read_down = _TRUE;
		}
		else
			rtw_msleep_os(1);
	}while( (!read_down) && (retry_cnts--));

	return read_down;

}


/*****************************************
* H2C Msg format :
*| 31 - 8		|7-5	| 4 - 0	|
*| h2c_msg 	|Class	|CMD_ID	|
*| 31-0						|
*| Ext msg					|
*
******************************************/
s32 FillH2CCmd8723B(PADAPTER padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer)
{
	u8 h2c_box_num;
	u32	msgbox_addr;
	u32 msgbox_ex_addr=0;
	PHAL_DATA_TYPE pHalData;
	u32	h2c_cmd = 0;
	u32	h2c_cmd_ex = 0;
	s32 ret = _FAIL;
	struct dvobj_priv *psdpriv = padapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
_func_enter_;

	padapter = GET_PRIMARY_ADAPTER(padapter);
	pHalData = GET_HAL_DATA(padapter);
#ifdef DBG_CHECK_FW_PS_STATE
#ifdef DBG_CHECK_FW_PS_STATE_H2C
	if(rtw_fw_ps_state(padapter) == _FAIL)
	{
		DBG_871X("%s: h2c doesn't leave 32k ElementID=%02x \n", __FUNCTION__, ElementID);
		pdbgpriv->dbg_h2c_leave32k_fail_cnt++;
	}

	//DBG_871X("H2C ElementID=%02x , pHalData->LastHMEBoxNum=%02x\n", ElementID, pHalData->LastHMEBoxNum);
#endif //DBG_CHECK_FW_PS_STATE_H2C
#endif //DBG_CHECK_FW_PS_STATE
	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

	if (!pCmdBuffer) {
		goto exit;
	}
	if(CmdLen > RTL8723B_MAX_CMD_LEN) {
		goto exit;
	}
	if (rtw_is_surprise_removed(padapter))
		goto exit;

	//pay attention to if  race condition happened in  H2C cmd setting.
	do{
		h2c_box_num = pHalData->LastHMEBoxNum;

		if(!_is_fw_read_cmd_down(padapter, h2c_box_num)){
			DBG_8192C(" fw read cmd failed...\n");
#ifdef DBG_CHECK_FW_PS_STATE
			DBG_871X("MAC_1C0=%08x, MAC_1C4=%08x, MAC_1C8=%08x, MAC_1CC=%08x\n", rtw_read32(padapter, 0x1c0), rtw_read32(padapter, 0x1c4)
				, rtw_read32(padapter, 0x1c8), rtw_read32(padapter, 0x1cc));
#endif //DBG_CHECK_FW_PS_STATE
			//DBG_8192C(" 0x1c0: 0x%8x\n", rtw_read32(padapter, 0x1c0));
			//DBG_8192C(" 0x1c4: 0x%8x\n", rtw_read32(padapter, 0x1c4));
			goto exit;
		}

		if(CmdLen<=3)
		{
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer, CmdLen );
		}
		else{
			_rtw_memcpy((u8*)(&h2c_cmd)+1, pCmdBuffer, 3);
			_rtw_memcpy((u8*)(&h2c_cmd_ex), pCmdBuffer+3, CmdLen-3);
//			*(u8*)(&h2c_cmd) |= BIT(7);
		}

		*(u8*)(&h2c_cmd) |= ElementID;

		if(CmdLen>3){
			msgbox_ex_addr = REG_HMEBOX_EXT0_8723B + (h2c_box_num *RTL8723B_EX_MESSAGE_BOX_SIZE);
			h2c_cmd_ex = le32_to_cpu( h2c_cmd_ex );
			rtw_write32(padapter, msgbox_ex_addr, h2c_cmd_ex);
		}
		msgbox_addr =REG_HMEBOX_0 + (h2c_box_num *MESSAGE_BOX_SIZE);
		h2c_cmd = le32_to_cpu( h2c_cmd );
		rtw_write32(padapter,msgbox_addr, h2c_cmd);

		//DBG_8192C("MSG_BOX:%d, CmdLen(%d), CmdID(0x%x), reg:0x%x =>h2c_cmd:0x%.8x, reg:0x%x =>h2c_cmd_ex:0x%.8x\n"
		// 	,pHalData->LastHMEBoxNum , CmdLen, ElementID, msgbox_addr, h2c_cmd, msgbox_ex_addr, h2c_cmd_ex);

		pHalData->LastHMEBoxNum = (h2c_box_num+1) % MAX_H2C_BOX_NUMS;

	}while(0);

	ret = _SUCCESS;

exit:

	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);	

_func_exit_;

	return ret;
}

static void ConstructBeacon(_adapter *padapter, u8 *pframe, u32 *pLength)
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
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
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

static void ConstructPSPoll(_adapter *padapter, u8 *pframe, u32 *pLength)
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
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);

	*pLength = 16;
}

static void ConstructNullFunctionData(
	PADAPTER padapter,
	u8		*pframe,
	u32		*pLength,
	u8		*StaAddr,
	u8		bQoS,
	u8		AC,
	u8		bEosp,
	u8		bForcePowerSave)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16						*fctrl;
	u32						pktlen;
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
	struct wlan_network		*cur_network = &pmlmepriv->cur_network;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);


	//DBG_871X("%s:%d\n", __FUNCTION__, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;

	fctrl = &pwlanhdr->frame_ctl;
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
			_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, StaAddr, ETH_ALEN);
			break;
		case Ndis802_11APMode:
			SetFrDs(fctrl);
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);
			break;
		case Ndis802_11IBSS:
		default:
			_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
			_rtw_memcpy(pwlanhdr->addr3, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);
			break;
	}

	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE) {
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos*)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	} else {
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
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
// Description: Get the reserved page number in Tx packet buffer.
// Retrun value: the page number.
// 2012.08.09, by tynli.
//
u8 GetTxBufferRsvdPageNum8723B(_adapter *padapter, bool wowlan)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	RsvdPageNum=0;
	// default reseved 1 page for the IC type which is undefined.
	u8	TxPageBndy= LAST_ENTRY_OF_TX_PKT_BUFFER_8723B;

	rtw_hal_get_def_var(padapter, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&TxPageBndy);

	RsvdPageNum = LAST_ENTRY_OF_TX_PKT_BUFFER_8723B -TxPageBndy + 1;

	return RsvdPageNum;
}

static void rtl8723b_set_FwRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	u8 u1H2CRsvdPageParm[H2C_RSVDPAGE_LOC_LEN]={0};

	DBG_871X("8723BRsvdPageLoc: ProbeRsp=%d PsPoll=%d Null=%d QoSNull=%d BTNull=%d\n",  
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

static void rtl8723b_set_FwAoacRsvdPage_cmd(PADAPTER padapter, PRSVDPAGE_LOC rsvdpageloc)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8	res = 0, count = 0;
#ifdef CONFIG_WOWLAN	
	u8 u1H2CAoacRsvdPageParm[H2C_AOAC_RSVDPAGE_LOC_LEN]={0};

	DBG_871X("8723BAOACRsvdPageLoc: RWC=%d ArpRsp=%d NbrAdv=%d GtkRsp=%d GtkInfo=%d ProbeReq=%d NetworkList=%d\n",  
			rsvdpageloc->LocRemoteCtrlInfo, rsvdpageloc->LocArpRsp,
			rsvdpageloc->LocNbrAdv, rsvdpageloc->LocGTKRsp,
			rsvdpageloc->LocGTKInfo, rsvdpageloc->LocProbeReq,
			rsvdpageloc->LocNetList);

	if (check_fwstate(pmlmepriv, _FW_LINKED)) {
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_REMOTE_WAKE_CTRL_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocRemoteCtrlInfo);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_ARP_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocArpRsp);
		//SET_H2CCMD_AOAC_RSVDPAGE_LOC_NEIGHBOR_ADV(u1H2CAoacRsvdPageParm, rsvdpageloc->LocNbrAdv);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_RSP(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKRsp);
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKInfo);
#ifdef CONFIG_GTK_OL
		SET_H2CCMD_AOAC_RSVDPAGE_LOC_GTK_EXT_MEM(u1H2CAoacRsvdPageParm, rsvdpageloc->LocGTKEXTMEM);
#endif // CONFIG_GTK_OL
		RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CAoacRsvdPageParm:", u1H2CAoacRsvdPageParm, H2C_AOAC_RSVDPAGE_LOC_LEN);
		FillH2CCmd8723B(padapter, H2C_8723B_AOAC_RSVD_PAGE, H2C_AOAC_RSVDPAGE_LOC_LEN, u1H2CAoacRsvdPageParm);
	} else {
#ifdef CONFIG_PNO_SUPPORT
		if(!pwrpriv->pno_in_resume) {
			DBG_871X("NLO_INFO=%d\n", rsvdpageloc->LocPNOInfo);
			_rtw_memset(&u1H2CAoacRsvdPageParm, 0, sizeof(u1H2CAoacRsvdPageParm));
			SET_H2CCMD_AOAC_RSVDPAGE_LOC_NLO_INFO(u1H2CAoacRsvdPageParm, rsvdpageloc->LocPNOInfo);
			FillH2CCmd8723B(padapter, H2C_AOAC_RSVDPAGE3, H2C_AOAC_RSVDPAGE_LOC_LEN, u1H2CAoacRsvdPageParm);
			rtw_msleep_os(10);
		}
#endif
	}

#endif // CONFIG_WOWLAN
}

static void rtl8723b_set_FwKeepAlive_cmd(PADAPTER padapter, u8 benable, u8 pkt_type)
{
	u8 u1H2CKeepAliveParm[H2C_KEEP_ALIVE_CTRL_LEN]={0};
	u8 adopt = 1;
#ifdef CONFIG_PLATFORM_INTEL_BYT
        u8 check_period = 10;
#else
        u8 check_period = 5;
#endif 

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_ENABLE(u1H2CKeepAliveParm, benable);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_ADOPT(u1H2CKeepAliveParm, adopt);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_PKT_TYPE(u1H2CKeepAliveParm, pkt_type);
	SET_8723B_H2CCMD_KEEPALIVE_PARM_CHECK_PERIOD(u1H2CKeepAliveParm, check_period);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CKeepAliveParm:", u1H2CKeepAliveParm, H2C_KEEP_ALIVE_CTRL_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_KEEP_ALIVE, H2C_KEEP_ALIVE_CTRL_LEN, u1H2CKeepAliveParm);
}

static void rtl8723b_set_FwDisconDecision_cmd(PADAPTER padapter, u8 benable)
{
	u8 u1H2CDisconDecisionParm[H2C_DISCON_DECISION_LEN]={0};
	u8 adopt = 1, check_period = 10, trypkt_num = 0;

	DBG_871X("%s(): benable = %d\n", __func__, benable);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_ENABLE(u1H2CDisconDecisionParm, benable);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_ADOPT(u1H2CDisconDecisionParm, adopt);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_CHECK_PERIOD(u1H2CDisconDecisionParm, check_period);
	SET_8723B_H2CCMD_DISCONDECISION_PARM_TRY_PKT_NUM(u1H2CDisconDecisionParm, trypkt_num);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CDisconDecisionParm:", u1H2CDisconDecisionParm, H2C_DISCON_DECISION_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_DISCON_DECISION, H2C_DISCON_DECISION_LEN, u1H2CDisconDecisionParm);
}

void rtl8723b_set_FwMacIdConfig_cmd(_adapter* padapter, u8 mac_id, u8 raid, u8 bw, u8 sgi, u32 mask)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8 u1H2CMacIdConfigParm[H2C_MACID_CFG_LEN]={0};

	DBG_871X("%s(): mac_id=%d raid=0x%x bw=%d mask=0x%x\n", __func__, mac_id, raid, bw, mask);
	
_func_enter_;

	SET_8723B_H2CCMD_MACID_CFG_MACID(u1H2CMacIdConfigParm, mac_id);
	SET_8723B_H2CCMD_MACID_CFG_RAID(u1H2CMacIdConfigParm, raid);
	SET_8723B_H2CCMD_MACID_CFG_SGI_EN(u1H2CMacIdConfigParm, (sgi)? 1:0);
	SET_8723B_H2CCMD_MACID_CFG_BW(u1H2CMacIdConfigParm, bw);

	//DisableTXPowerTraining
	if(pHalData->bDisableTXPowerTraining){
		SET_8723B_H2CCMD_MACID_CFG_DISPT(u1H2CMacIdConfigParm,1);
		DBG_871X("%s,Disable PWT by driver\n",__FUNCTION__);
	}
	else{
		PDM_ODM_T	pDM_OutSrc = &pHalData->odmpriv;

		if(pDM_OutSrc->bDisablePowerTraining){
			SET_8723B_H2CCMD_MACID_CFG_DISPT(u1H2CMacIdConfigParm,1);
			DBG_871X("%s,Disable PWT by DM\n",__FUNCTION__);	
		}
	}	
		
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK0(u1H2CMacIdConfigParm, (u8)(mask & 0x000000ff));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK1(u1H2CMacIdConfigParm, (u8)((mask & 0x0000ff00) >>8));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK2(u1H2CMacIdConfigParm, (u8)((mask & 0x00ff0000) >> 16));
	SET_8723B_H2CCMD_MACID_CFG_RATE_MASK3(u1H2CMacIdConfigParm, (u8)((mask & 0xff000000) >> 24));
	
	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CMacIdConfigParm:", u1H2CMacIdConfigParm, H2C_MACID_CFG_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_MACID_CFG, H2C_MACID_CFG_LEN, u1H2CMacIdConfigParm);

_func_exit_;
}

void rtl8723b_set_FwRssiSetting_cmd(_adapter*padapter, u8 *param)
{
	u8 u1H2CRssiSettingParm[H2C_RSSI_SETTING_LEN]={0};
	u8 mac_id = *param;
	u8 rssi = *(param+2);
	u8 uldl_state = 0;

_func_enter_;
	//DBG_871X("%s(): param=%.2x-%.2x-%.2x\n", __func__, *param, *(param+1), *(param+2));
	//DBG_871X("%s(): mac_id=%d rssi=%d\n", __func__, mac_id, rssi);

	SET_8723B_H2CCMD_RSSI_SETTING_MACID(u1H2CRssiSettingParm, mac_id);
	SET_8723B_H2CCMD_RSSI_SETTING_RSSI(u1H2CRssiSettingParm, rssi);
	SET_8723B_H2CCMD_RSSI_SETTING_ULDL_STATE(u1H2CRssiSettingParm, uldl_state);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_notice_, "u1H2CRssiSettingParm:", u1H2CRssiSettingParm, H2C_RSSI_SETTING_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_RSSI_SETTING, H2C_RSSI_SETTING_LEN, u1H2CRssiSettingParm);

_func_exit_;
}

void rtl8723b_set_FwAPReqRPT_cmd(PADAPTER padapter, u32 need_ack)
{
	u8 u1H2CApReqRptParm[H2C_AP_REQ_TXRPT_LEN]={0};
	u8 macid1 = 1, macid2 = 0;

	DBG_871X("%s(): need_ack = %d\n", __func__, need_ack);

	SET_8723B_H2CCMD_APREQRPT_PARM_MACID1(u1H2CApReqRptParm, macid1);
	SET_8723B_H2CCMD_APREQRPT_PARM_MACID2(u1H2CApReqRptParm, macid2);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CApReqRptParm:", u1H2CApReqRptParm, H2C_AP_REQ_TXRPT_LEN);
	FillH2CCmd8723B(padapter, H2C_8723B_AP_REQ_TXRPT, H2C_AP_REQ_TXRPT_LEN, u1H2CApReqRptParm);
}

void rtl8723b_set_FwPwrMode_cmd(PADAPTER padapter, u8 psmode)
{
	int i;
	u8 smart_ps = 0;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	u8 u1H2CPwrModeParm[H2C_PWRMODE_LEN]={0};
	u8 PowerState=0, awake_intvl = 1, byte5 = 0, rlbm = 0;
#ifdef CONFIG_P2P
	struct wifidirect_info *wdinfo = &(padapter->wdinfo);
#endif // CONFIG_P2P

_func_enter_;

	if(pwrpriv->dtim > 0)
		DBG_871X("%s(): FW LPS mode = %d, SmartPS=%d, dtim=%d\n", __func__, psmode, pwrpriv->smart_ps, pwrpriv->dtim);
	else
		DBG_871X("%s(): FW LPS mode = %d, SmartPS=%d\n", __func__, psmode, pwrpriv->smart_ps);

	if(psmode == PS_MODE_MIN)
	{
		rlbm = 0;
		awake_intvl = 2;
		smart_ps = pwrpriv->smart_ps;
	}
	else if(psmode == PS_MODE_MAX)
	{
		rlbm = 1;
		awake_intvl = 2;
		smart_ps = pwrpriv->smart_ps;
	}
	else if(psmode == PS_MODE_DTIM) //For WOWLAN LPS, DTIM = (awake_intvl - 1)
	{
		if(pwrpriv->dtim > 0 && pwrpriv->dtim < 16)
			awake_intvl = pwrpriv->dtim+1;//DTIM = (awake_intvl - 1)
		else
			awake_intvl = 4;//DTIM=3


		rlbm = 2;
		smart_ps = pwrpriv->smart_ps;
	}
	else
	{
		rlbm = 2;
		awake_intvl = 4;
		smart_ps = pwrpriv->smart_ps;
	}	

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(wdinfo, P2P_STATE_NONE)) {
		awake_intvl = 2;
		rlbm = 1;
	}
#endif // CONFIG_P2P

	if(padapter->registrypriv.wifi_spec==1)
	{
		awake_intvl = 2;
		rlbm = 1;
	}

	if (psmode > 0)
	{
#ifdef CONFIG_BT_COEXIST
		if (rtw_btcoex_IsBtControlLps(padapter) == _TRUE)
		{
			PowerState = rtw_btcoex_RpwmVal(padapter);
			byte5 = rtw_btcoex_LpsVal(padapter);

			if ((rlbm == 2) && (byte5 & BIT(4)))
			{
				// Keep awake interval to 1 to prevent from
				// decreasing coex performance
				awake_intvl = 2;
				rlbm = 2;
			}
		}
		else
#endif // CONFIG_BT_COEXIST
		{
			PowerState = 0x00;// AllON(0x0C), RFON(0x04), RFOFF(0x00)
			byte5 = 0x40;
		}
	}
	else
	{
		PowerState = 0x0C;// AllON(0x0C), RFON(0x04), RFOFF(0x00)
		byte5 = 0x40;
	}

	SET_8723B_H2CCMD_PWRMODE_PARM_MODE(u1H2CPwrModeParm, (psmode>0)?1:0);
	SET_8723B_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CPwrModeParm, smart_ps);
	SET_8723B_H2CCMD_PWRMODE_PARM_RLBM(u1H2CPwrModeParm, rlbm);
	SET_8723B_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CPwrModeParm, awake_intvl);
	SET_8723B_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CPwrModeParm, padapter->registrypriv.uapsd_enable);
	SET_8723B_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CPwrModeParm, PowerState);
	SET_8723B_H2CCMD_PWRMODE_PARM_BYTE5(u1H2CPwrModeParm, byte5);
#ifdef CONFIG_LPS_LCLK
	if(psmode != PS_MODE_ACTIVE)
	{
		if(pmlmeext ->adaptive_tsf_done == _FALSE && pmlmeext->bcn_cnt>0)
		{
			u8 ratio_20_delay, ratio_80_delay;

			//byte 6 for adaptive_early_32k
			//[0:3] = DrvBcnEarly  (ms) , [4:7] = DrvBcnTimeOut  (ms)
			// 20% for DrvBcnEarly, 80% for DrvBcnTimeOut
			ratio_20_delay = 0;
			ratio_80_delay = 0;
			pmlmeext->DrvBcnEarly = 0xff;
			pmlmeext->DrvBcnTimeOut = 0xff;

			//DBG_871X("%s(): bcn_cnt = %d\n", __func__, pmlmeext->bcn_cnt);

			for(i=0; i<9; i++)
			{
				pmlmeext->bcn_delay_ratio[i] = (pmlmeext->bcn_delay_cnt[i] * 100) /pmlmeext->bcn_cnt;

				//DBG_871X("%s(): bcn_delay_cnt[%d]=%d, bcn_delay_ratio[%d] = %d\n", __func__, i, pmlmeext->bcn_delay_cnt[i]
				//	,i ,pmlmeext->bcn_delay_ratio[i]);
	
				ratio_20_delay += pmlmeext->bcn_delay_ratio[i];
				ratio_80_delay += pmlmeext->bcn_delay_ratio[i];

				if(ratio_20_delay > 20 && pmlmeext->DrvBcnEarly == 0xff)
				{
					pmlmeext->DrvBcnEarly = i;
					//DBG_871X("%s(): DrvBcnEarly = %d\n", __func__, pmlmeext->DrvBcnEarly);
				}	

				if(ratio_80_delay > 80 && pmlmeext->DrvBcnTimeOut == 0xff)
				{
					pmlmeext->DrvBcnTimeOut = i;
					//DBG_871X("%s(): DrvBcnTimeOut = %d\n", __func__, pmlmeext->DrvBcnTimeOut);
				}

				//reset adaptive_early_32k cnt
				pmlmeext->bcn_delay_cnt[i] = 0;
				pmlmeext->bcn_delay_ratio[i] = 0;
			
			}

			pmlmeext->bcn_cnt = 0;
			pmlmeext ->adaptive_tsf_done = _TRUE;

		}
		else
		{
			//DBG_871X("%s(): DrvBcnEarly = %d\n", __func__, pmlmeext->DrvBcnEarly);
			//DBG_871X("%s(): DrvBcnTimeOut = %d\n", __func__, pmlmeext->DrvBcnTimeOut);
		}

/* offload to FW if fw version > v15.10
		pmlmeext->DrvBcnEarly=0;
		pmlmeext->DrvBcnTimeOut=7;

		if((pmlmeext->DrvBcnEarly!=0Xff) && (pmlmeext->DrvBcnTimeOut!=0xff))
			u1H2CPwrModeParm[H2C_PWRMODE_LEN-1] = BIT(0) | ((pmlmeext->DrvBcnEarly<<1)&0x0E) |((pmlmeext->DrvBcnTimeOut<<4)&0xf0) ;
*/

	}
#endif

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_RecordPwrMode(padapter, u1H2CPwrModeParm, H2C_PWRMODE_LEN);
#endif // CONFIG_BT_COEXIST

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CPwrModeParm:", u1H2CPwrModeParm, H2C_PWRMODE_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_SET_PWR_MODE, H2C_PWRMODE_LEN, u1H2CPwrModeParm);
_func_exit_;
}

#ifdef CONFIG_TDLS
#ifdef CONFIG_TDLS_CH_SW
void rtl8723b_set_BcnEarly_C2H_Rpt_cmd(PADAPTER padapter, u8 enable)
{
	u8	u1H2CSetPwrMode[H2C_PWRMODE_LEN] = {0};

	SET_8723B_H2CCMD_PWRMODE_PARM_MODE(u1H2CSetPwrMode, 1);
	SET_8723B_H2CCMD_PWRMODE_PARM_RLBM(u1H2CSetPwrMode, 1);
	SET_8723B_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CSetPwrMode, 0);
	SET_8723B_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CSetPwrMode, 0);
	SET_8723B_H2CCMD_PWRMODE_PARM_ALL_QUEUE_UAPSD(u1H2CSetPwrMode, 0);
	SET_8723B_H2CCMD_PWRMODE_PARM_BCN_EARLY_C2H_RPT(u1H2CSetPwrMode, enable);
	SET_8723B_H2CCMD_PWRMODE_PARM_PWR_STATE(u1H2CSetPwrMode, 0x0C);
	SET_8723B_H2CCMD_PWRMODE_PARM_BYTE5(u1H2CSetPwrMode, 0);
	FillH2CCmd8723B(padapter, H2C_8723B_SET_PWR_MODE, sizeof(u1H2CSetPwrMode), u1H2CSetPwrMode);
}
#endif
#endif

void rtl8723b_set_FwPsTuneParam_cmd(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	u8 u1H2CPsTuneParm[H2C_PSTUNEPARAM_LEN]={0};
	u8 bcn_to_limit = 10; //10 * 100 * awakeinterval (ms)
	u8 dtim_timeout = 5; //ms //wait broadcast data timer
	u8 ps_timeout = 20;  //ms //Keep awake when tx
	u8 dtim_period = 3; 

_func_enter_;
	//DBG_871X("%s(): FW LPS mode = %d\n", __func__, psmode);

	SET_8723B_H2CCMD_PSTUNE_PARM_BCN_TO_LIMIT(u1H2CPsTuneParm, bcn_to_limit);
	SET_8723B_H2CCMD_PSTUNE_PARM_DTIM_TIMEOUT(u1H2CPsTuneParm, dtim_timeout);
	SET_8723B_H2CCMD_PSTUNE_PARM_PS_TIMEOUT(u1H2CPsTuneParm, ps_timeout);
	SET_8723B_H2CCMD_PSTUNE_PARM_ADOPT(u1H2CPsTuneParm, 1);
	SET_8723B_H2CCMD_PSTUNE_PARM_DTIM_PERIOD(u1H2CPsTuneParm, dtim_period);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CPsTuneParm:", u1H2CPsTuneParm, H2C_PSTUNEPARAM_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_PS_TUNING_PARA, H2C_PSTUNEPARAM_LEN, u1H2CPsTuneParm);
_func_exit_;
}

void rtl8723b_set_FwBtMpOper_cmd(PADAPTER padapter, u8 idx, u8 ver, u8 reqnum, u8 *param)
{
	u8 u1H2CBtMpOperParm[H2C_BTMP_OPER_LEN] = {0};

_func_enter_;

	DBG_8192C("%s: idx=%d ver=%d reqnum=%d param1=0x%02x param2=0x%02x\n", __FUNCTION__, idx, ver, reqnum, param[0], param[1]);

	SET_8723B_H2CCMD_BT_MPOPER_VER(u1H2CBtMpOperParm, ver);
	SET_8723B_H2CCMD_BT_MPOPER_REQNUM(u1H2CBtMpOperParm, reqnum);
	SET_8723B_H2CCMD_BT_MPOPER_IDX(u1H2CBtMpOperParm, idx);
	SET_8723B_H2CCMD_BT_MPOPER_PARAM1(u1H2CBtMpOperParm, param[0]);
	SET_8723B_H2CCMD_BT_MPOPER_PARAM2(u1H2CBtMpOperParm, param[1]);
	SET_8723B_H2CCMD_BT_MPOPER_PARAM3(u1H2CBtMpOperParm, param[2]);

	RT_PRINT_DATA(_module_hal_init_c_, _drv_always_, "u1H2CBtMpOperParm:", u1H2CBtMpOperParm, H2C_BTMP_OPER_LEN);

	FillH2CCmd8723B(padapter, H2C_8723B_BT_MP_OPER, H2C_BTMP_OPER_LEN, u1H2CBtMpOperParm);
_func_exit_;
}

void rtl8723b_set_FwPwrModeInIPS_cmd(PADAPTER padapter, u8 cmd_param)
{
	//u8 cmd_param; //BIT0:enable, BIT1:NoConnect32k

	DBG_871X("%s()\n", __func__);

	FillH2CCmd8723B(padapter, H2C_8723B_FWLPS_IN_IPS_, 1, &cmd_param);

}


static s32 rtl8723b_set_FwLowPwrLps_cmd(PADAPTER padapter, u8 enable)
{
	//TODO
	return _FALSE;	
}

void rtl8723b_download_rsvd_page(PADAPTER padapter, u8 mstatus)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	BOOLEAN		bcn_valid = _FALSE;
	u8	DLBcnCount=0;
	u32 poll = 0;
	u8 val8;

_func_enter_;

	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d mstatus(%x)\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), mstatus);

	if(mstatus == RT_MEDIA_CONNECT)
	{
		BOOLEAN bRecover = _FALSE;
		u8 v8;

		// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		// Suggested by filen. Added by tynli.
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));

		// set REG_CR bit 8
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 |= BIT(0); // ENSWBCN
		rtw_write8(padapter,  REG_CR+1, v8);

		// Disable Hw protection for a time which revserd for Hw sending beacon.
		// Fix download reserved page packet fail that access collision with the protection time.
		// 2010.05.11. Added by tynli.
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 &= ~EN_BCN_FUNCTION;
		val8 |= DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
		if (pHalData->RegFwHwTxQCtrl & BIT(6))
			bRecover = _TRUE;

		// To tell Hw the packet is not a real beacon frame.
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl & ~BIT(6));
		pHalData->RegFwHwTxQCtrl &= ~BIT(6);

		// Clear beacon valid check bit.
		rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

		DLBcnCount = 0;
		poll = 0;
		do
		{
			// download rsvd page.
			rtw_hal_set_fw_rsvd_page(padapter, 0);
			DLBcnCount++;
			do
			{
				rtw_yield_os();
				//rtw_mdelay_os(10);
				// check rsvd page download OK.
				rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, (u8*)(&bcn_valid));
				poll++;
			} while (!bcn_valid && (poll%10) != 0 && !RTW_CANNOT_RUN(padapter));
			
		} while (!bcn_valid && DLBcnCount <= 100 && !RTW_CANNOT_RUN(padapter));

		if (RTW_CANNOT_RUN(padapter))
			;
		else if(!bcn_valid)
			DBG_871X(ADPT_FMT": 1 DL RSVD page failed! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter) ,DLBcnCount, poll);
		else {
			struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
			pwrctl->fw_psmode_iface_id = padapter->iface_id;
			DBG_871X(ADPT_FMT": 1 DL RSVD page success! DLBcnCount:%u, poll:%u\n",
				ADPT_ARG(padapter), DLBcnCount, poll);
		}

		// 2010.05.11. Added by tynli.
		val8 = rtw_read8(padapter, REG_BCN_CTRL);
		val8 |= EN_BCN_FUNCTION;
		val8 &= ~DIS_TSF_UDT;
		rtw_write8(padapter, REG_BCN_CTRL, val8);

		// To make sure that if there exists an adapter which would like to send beacon.
		// If exists, the origianl value of 0x422[6] will be 1, we should check this to
		// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
		// the beacon cannot be sent by HW.
		// 2010.06.23. Added by tynli.
		if(bRecover)
		{
			rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl | BIT(6));
			pHalData->RegFwHwTxQCtrl |= BIT(6);
		}

		// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
#ifndef CONFIG_PCI_HCI
		v8 = rtw_read8(padapter, REG_CR+1);
		v8 &= ~BIT(0); // ~ENSWBCN
		rtw_write8(padapter, REG_CR+1, v8);
#endif
	}

_func_exit_;
}

void rtl8723b_set_rssi_cmd(_adapter*padapter, u8 *param)
{
	rtl8723b_set_FwRssiSetting_cmd(padapter, param);
}

void rtl8723b_set_FwJoinBssRpt_cmd(PADAPTER padapter, u8 mstatus)
{
	struct sta_info *psta = NULL;
	struct pwrctrl_priv *ppwrpriv = adapter_to_pwrctl(padapter);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;

	if(mstatus == 1)
		rtl8723b_download_rsvd_page(padapter, RT_MEDIA_CONNECT);
}

//arg[0] = macid
//arg[1] = raid
//arg[2] = shortGIrate
//arg[3] = init_rate
void rtl8723b_Add_RateATid(PADAPTER pAdapter, u64 rate_bitmap, u8 *arg, u8 rssi_level)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct macid_ctl_t *macid_ctl = &pAdapter->dvobj->macid_ctl;
	struct sta_info	*psta = NULL;
	u8 mac_id = arg[0];
	u8 raid = arg[1];
	u8 shortGI = arg[2];
	u8 bw;
	u32 bitmap = (u32) rate_bitmap;
	u32 mask = bitmap&0x0FFFFFFF;

	if (mac_id < macid_ctl->num)
		psta = macid_ctl->sta[mac_id];
	if (psta == NULL) {
		DBG_871X_LEVEL(_drv_always_, FUNC_ADPT_FMT" macid:%u, sta is NULL\n"
			, FUNC_ADPT_ARG(pAdapter), mac_id);
		return;
	}

	bw = psta->bw_mode;

	if(rssi_level != DM_RATR_STA_INIT)
		mask = ODM_Get_Rate_Bitmap(&pHalData->odmpriv, mac_id, mask, rssi_level);		

	DBG_871X("%s(): mac_id=%d raid=0x%x bw=%d mask=0x%x\n", __func__, mac_id, raid, bw, mask);
	rtl8723b_set_FwMacIdConfig_cmd(pAdapter, mac_id, raid, bw, shortGI, mask);
}

#if 0
void rtl8723b_fw_try_ap_cmd(PADAPTER padapter, u32 need_ack)
{
	rtl8723b_set_FwAPReqRPT_cmd(padapter, need_ack);
}
#endif

#ifdef CONFIG_BT_COEXIST
static void ConstructBtNullFunctionData(
	PADAPTER padapter,
	u8 *pframe,
	u32 *pLength,
	u8 *StaAddr,
	u8 bQoS,
	u8 AC,
	u8 bEosp,
	u8 bForcePowerSave)
{
	struct rtw_ieee80211_hdr *pwlanhdr;
	u16 *fctrl;
	u32 pktlen;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u8 bssid[ETH_ALEN];


	DBG_871X("+" FUNC_ADPT_FMT ": qos=%d eosp=%d ps=%d\n",
		FUNC_ADPT_ARG(padapter), bQoS, bEosp, bForcePowerSave);

	pwlanhdr = (struct rtw_ieee80211_hdr*)pframe;
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	if (NULL == StaAddr)
	{
		_rtw_memcpy(bssid, adapter_mac_addr(padapter), ETH_ALEN);
		StaAddr = bssid;
	}

	fctrl = &pwlanhdr->frame_ctl;
	*fctrl = 0;
	if (bForcePowerSave)
		SetPwrMgt(fctrl);

	SetFrDs(fctrl);
	_rtw_memcpy(pwlanhdr->addr1, StaAddr, ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr2, adapter_mac_addr(padapter), ETH_ALEN);
	_rtw_memcpy(pwlanhdr->addr3, adapter_mac_addr(padapter), ETH_ALEN);

	SetDuration(pwlanhdr, 0);
	SetSeqNum(pwlanhdr, 0);

	if (bQoS == _TRUE)
	{
		struct rtw_ieee80211_hdr_3addr_qos *pwlanqoshdr;

		SetFrameSubType(pframe, WIFI_QOS_DATA_NULL);

		pwlanqoshdr = (struct rtw_ieee80211_hdr_3addr_qos*)pframe;
		SetPriority(&pwlanqoshdr->qc, AC);
		SetEOSP(&pwlanqoshdr->qc, bEosp);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr_qos);
	}
	else
	{
		SetFrameSubType(pframe, WIFI_DATA_NULL);

		pktlen = sizeof(struct rtw_ieee80211_hdr_3addr);
	}

	*pLength = pktlen;
}

static void SetFwRsvdPagePkt_BTCoex(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct xmit_frame *pcmdframe;	
	struct pkt_attrib *pattrib;
	struct xmit_priv *pxmitpriv;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u32	BeaconLength = 0;
	u32	BTQosNullLength = 0;
	u8 *ReservedPagePacket;
	u8 TxDescLen, TxDescOffset;
	u8 TotalPageNum=0, CurtPktPageNum=0, RsvdPageNum=0;
	u16	BufIndex, PageSize;
	u32	TotalPacketLen, MaxRsvdPageBufSize=0;
	RSVDPAGE_LOC RsvdPageLoc;


//	DBG_8192C("+" FUNC_ADPT_FMT "\n", FUNC_ADPT_ARG(padapter));

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
		DBG_8192C("%s: alloc ReservedPagePacket fail!\n", __FUNCTION__);
		return;
	}

	ReservedPagePacket = pcmdframe->buf_addr;
	_rtw_memset(&RsvdPageLoc, 0, sizeof(RSVDPAGE_LOC));

	//3 (1) beacon
	BufIndex = TxDescOffset;
	ConstructBeacon(padapter, &ReservedPagePacket[BufIndex], &BeaconLength);

	// When we count the first page size, we need to reserve description size for the RSVD
	// packet, it will be filled in front of the packet in TXPKTBUF.
	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BeaconLength);
	//If we don't add 1 more page, the WOWLAN function has a problem. Baron thinks it's a bug of firmware
	if (CurtPktPageNum == 1)
	{
		CurtPktPageNum += 1;
	}
	TotalPageNum += CurtPktPageNum;

	BufIndex += (CurtPktPageNum*PageSize);

	// Jump to lastest page
	if (BufIndex < (MaxRsvdPageBufSize - PageSize))
	{
		BufIndex = TxDescOffset + (MaxRsvdPageBufSize - PageSize);
		TotalPageNum = BCNQ_PAGE_NUM_8723B - 1;
	}

	//3 (6) BT Qos null data
	RsvdPageLoc.LocBTQosNull = TotalPageNum;
	ConstructBtNullFunctionData(
		padapter,
		&ReservedPagePacket[BufIndex],
		&BTQosNullLength,
		NULL,
		_TRUE, 0, 0, _FALSE);
	rtl8723b_fill_fake_txdesc(padapter, &ReservedPagePacket[BufIndex-TxDescLen], BTQosNullLength, _FALSE, _TRUE, _FALSE);

	CurtPktPageNum = (u8)PageNum_128(TxDescLen + BTQosNullLength);

	TotalPageNum += CurtPktPageNum;

	TotalPacketLen = BufIndex + BTQosNullLength;
	if (TotalPacketLen > MaxRsvdPageBufSize)
	{
		DBG_8192C(FUNC_ADPT_FMT ": ERROR: The rsvd page size is not enough!!TotalPacketLen %d, MaxRsvdPageBufSize %d\n",
			FUNC_ADPT_ARG(padapter), TotalPacketLen, MaxRsvdPageBufSize);
		goto error;
	}

	// update attribute
	pattrib = &pcmdframe->attrib;
	update_mgntframe_attrib(padapter, pattrib);
	pattrib->qsel = QSLT_BEACON;
	pattrib->pktlen = pattrib->last_txcmdsz = TotalPacketLen - TxDescOffset;
#ifdef CONFIG_PCI_HCI
	dump_mgntframe(padapter, pcmdframe);
#else
	dump_mgntframe_and_wait(padapter, pcmdframe, 100);
#endif

//	DBG_8192C(FUNC_ADPT_FMT ": Set RSVD page location to Fw, TotalPacketLen(%d), TotalPageNum(%d)\n",
//		FUNC_ADPT_ARG(padapter), TotalPacketLen, TotalPageNum);
	rtl8723b_set_FwRsvdPage_cmd(padapter, &RsvdPageLoc);
	rtl8723b_set_FwAoacRsvdPage_cmd(padapter, &RsvdPageLoc);

	return;

error:
	rtw_free_xmitframe(pxmitpriv, pcmdframe);
}

void rtl8723b_download_BTCoex_AP_mode_rsvd_page(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct mlme_ext_priv *pmlmeext;
	struct mlme_ext_info *pmlmeinfo;
	u8 bRecover = _FALSE;
	u8 bcn_valid = _FALSE;
	u8 DLBcnCount = 0;
	u32 poll = 0;
	u8 val8;


	DBG_8192C("+" FUNC_ADPT_FMT ": iface_type=%d fw_state=0x%08X\n",
		FUNC_ADPT_ARG(padapter), get_iface_type(padapter), get_fwstate(&padapter->mlmepriv));

#ifdef CONFIG_DEBUG
	if (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _FALSE)
	{
		DBG_8192C(FUNC_ADPT_FMT ": [WARNING] not in AP mode!!\n",
			FUNC_ADPT_ARG(padapter));
	}
#endif // CONFIG_DEBUG

	pHalData = GET_HAL_DATA(padapter);
	pmlmeext = &padapter->mlmeextpriv;
	pmlmeinfo = &pmlmeext->mlmext_info;

	// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
	// Suggested by filen. Added by tynli.
	rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
	
	// set REG_CR bit 8
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 |= BIT(0); // ENSWBCN
	rtw_write8(padapter,  REG_CR+1, val8);
	
	// Disable Hw protection for a time which revserd for Hw sending beacon.
	// Fix download reserved page packet fail that access collision with the protection time.
	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 &= ~EN_BCN_FUNCTION;
	val8 |= DIS_TSF_UDT;
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
	if (pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover = _TRUE;

	// To tell Hw the packet is not a real beacon frame.
	pHalData->RegFwHwTxQCtrl &= ~BIT(6);
	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);

	// Clear beacon valid check bit.
	rtw_hal_set_hwreg(padapter, HW_VAR_BCN_VALID, NULL);
	rtw_hal_set_hwreg(padapter, HW_VAR_DL_BCN_SEL, NULL);

	DLBcnCount = 0;
	poll = 0;
	do {
		SetFwRsvdPagePkt_BTCoex(padapter);
		DLBcnCount++;
		do {
			rtw_yield_os();
//			rtw_mdelay_os(10);
			// check rsvd page download OK.
			rtw_hal_get_hwreg(padapter, HW_VAR_BCN_VALID, &bcn_valid);
			poll++;
		} while (!bcn_valid && (poll%10) != 0 && !RTW_CANNOT_RUN(padapter));
	} while (!bcn_valid && (DLBcnCount <= 100) && !RTW_CANNOT_RUN(padapter));

	if (_TRUE == bcn_valid)
	{
		struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
		pwrctl->fw_psmode_iface_id = padapter->iface_id;
		DBG_8192C(ADPT_FMT": DL RSVD page success! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
	}
	else
	{
		DBG_8192C(ADPT_FMT": DL RSVD page fail! DLBcnCount:%d, poll:%d\n",
			ADPT_ARG(padapter), DLBcnCount, poll);
		DBG_8192C(ADPT_FMT": DL RSVD page fail! bSurpriseRemoved=%s\n",
			ADPT_ARG(padapter), rtw_is_surprise_removed(padapter)?"True":"False");
		DBG_8192C(ADPT_FMT": DL RSVD page fail! bDriverStopped=%s\n",
			ADPT_ARG(padapter), rtw_is_drv_stopped(padapter)?"True":"False");
	}

	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(padapter, REG_BCN_CTRL);
	val8 |= EN_BCN_FUNCTION;
	val8 &= ~DIS_TSF_UDT;
	rtw_write8(padapter, REG_BCN_CTRL, val8);

	// To make sure that if there exists an adapter which would like to send beacon.
	// If exists, the origianl value of 0x422[6] will be 1, we should check this to
	// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause
	// the beacon cannot be sent by HW.
	// 2010.06.23. Added by tynli.
	if (bRecover)
	{
		pHalData->RegFwHwTxQCtrl |= BIT(6);
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, pHalData->RegFwHwTxQCtrl);
	}

	// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
#ifndef CONFIG_PCI_HCI
	val8 = rtw_read8(padapter, REG_CR+1);
	val8 &= ~BIT(0); // ~ENSWBCN
	rtw_write8(padapter, REG_CR+1, val8);
#endif
}
#endif // CONFIG_BT_COEXIST

#ifdef CONFIG_P2P
void rtl8723b_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv		*pwrpriv = adapter_to_pwrctl(padapter);
	struct wifidirect_info	*pwdinfo = &( padapter->wdinfo );
	struct P2P_PS_Offload_t	*p2p_ps_offload = (struct P2P_PS_Offload_t	*)(&pHalData->p2p_ps_offload);
	u8	i;

_func_enter_;

#if 1
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
				rtw_write8(padapter, REG_P2P_CTWIN, pwdinfo->ctwindow);
			}

			// hw only support 2 set of NoA
			for( i=0 ; i<pwdinfo->noa_num ; i++)
			{
				// To control the register setting for which NOA
				rtw_write8(padapter, REG_NOA_DESC_SEL, (i << 4));
				if(i == 0)
					p2p_ps_offload->NoA0_En = 1;
				else
					p2p_ps_offload->NoA1_En = 1;

				// config P2P NoA Descriptor Register
				//DBG_8192C("%s(): noa_duration = %x\n",__FUNCTION__,pwdinfo->noa_duration[i]);
				rtw_write32(padapter, REG_NOA_DESC_DURATION, pwdinfo->noa_duration[i]);

				//DBG_8192C("%s(): noa_interval = %x\n",__FUNCTION__,pwdinfo->noa_interval[i]);
				rtw_write32(padapter, REG_NOA_DESC_INTERVAL, pwdinfo->noa_interval[i]);

				//DBG_8192C("%s(): start_time = %x\n",__FUNCTION__,pwdinfo->noa_start_time[i]);
				rtw_write32(padapter, REG_NOA_DESC_START, pwdinfo->noa_start_time[i]);

				//DBG_8192C("%s(): noa_count = %x\n",__FUNCTION__,pwdinfo->noa_count[i]);
				rtw_write8(padapter, REG_NOA_DESC_COUNT, pwdinfo->noa_count[i]);
			}

			if( (pwdinfo->opp_ps == 1) || (pwdinfo->noa_num > 0) )
			{
				// rst p2p circuit
				rtw_write8(padapter, REG_DUAL_TSF_RST, BIT(4));

				p2p_ps_offload->Offload_En = 1;

				if(pwdinfo->role == P2P_ROLE_GO)
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

	FillH2CCmd8723B(padapter, H2C_8723B_P2P_PS_OFFLOAD, 1, (u8 *)p2p_ps_offload);
#endif

_func_exit_;

}
#endif //CONFIG_P2P


#ifdef CONFIG_TSF_RESET_OFFLOAD
/*
	ask FW to Reset sync register at Beacon early interrupt
*/
u8 rtl8723b_reset_tsf(_adapter *padapter, u8 reset_port )
{
	u8	buf[2];
	u8	res=_SUCCESS;

_func_enter_;
	if (IFACE_PORT0==reset_port) {
		buf[0] = 0x1; buf[1] = 0;

	} else{
		buf[0] = 0x0; buf[1] = 0x1;
	}
	FillH2CCmd8723B(padapter, H2C_8723B_RESET_TSF, 2, buf);
_func_exit_;

	return res;
}
#endif	// CONFIG_TSF_RESET_OFFLOAD

