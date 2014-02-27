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
#define _RTL8192D_CMD_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <cmd_osdep.h>
#include <mlme_osdep.h>
#include <rtw_byteorder.h>
#include <circ_buf.h>
#include <rtw_ioctl_set.h>

#include <rtl8192d_hal.h>

#ifndef PLATFORM_FREEBSD
static BOOLEAN
CheckWriteH2C(
	IN	PADAPTER		Adapter,
	IN	u8		BoxNum
)
{
	u8	valHMETFR;
	BOOLEAN	Result = _FALSE;
	
	valHMETFR = rtw_read8(Adapter, REG_HMETFR);

	//DbgPrint("CheckWriteH2C(): Reg[0x%2x] = %x\n",REG_HMETFR, valHMETFR);

	if(((valHMETFR>>BoxNum)&BIT0) == 1)
		Result = _TRUE;
	
	return Result;

}
#endif //PLATFORM_FREEBSD

static BOOLEAN
CheckFwReadLastH2C(
	IN	PADAPTER		Adapter,
	IN	u8		BoxNum
)
{
	u8	valHMETFR;
	BOOLEAN	 Result = _FALSE;
	
	valHMETFR = rtw_read8(Adapter, REG_HMETFR);
	//RT_TRACE(COMP_INIT,DBG_LOUD,("REG[%x] = %x\n", 	REG_HMETFR, valHMETFR));

	// Do not seperate to 91C and 88C, we use the same setting. Suggested by SD4 Filen. 2009.12.03.
	if(((valHMETFR>>BoxNum)&BIT0) == 0)
		Result = _TRUE;

	return Result;
}


//
// Description: 
//	Fill H2C command
//	BOX_0-4 Format: 
//	bit [31-8]	|     7		|  [6-0]
//	     RSVD	|  CMD_EXT	|  CMD_ID
//
//	BOX Extension 0-4 format:
//	bit 15-0: RSVD	
//

/*****************************************
* H2C Msg format :
*| 31 - 8		|7		| 6 - 0	|	
*| h2c_msg 	|Ext_bit	|CMD_ID	|
*
******************************************/
static void _FillH2CCmd92D(_adapter* padapter, u8 ElementID, u32 CmdLen, u8* pCmdBuffer)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	BoxNum;
	u16	BOXReg=0, BOXExtReg=0;
	u8	BoxContent[4], BoxExtContent[2];
	u8 	BufIndex=0;
	u8	U1btmp; //Read 0x1bf
	u8	bWriteSucess = _FALSE;
	u8	IsFwRead = _FALSE;
	u8	WaitH2cLimmit = 100;
	u8	WaitWriteH2cLimmit = 100;
	u8	idx=0;

_func_enter_;	

	padapter = GET_PRIMARY_ADAPTER(padapter);		
	pHalData = GET_HAL_DATA(padapter);

	_enter_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

	//DBG_8192C("FillH2CCmd : ElementID=%d \n",ElementID);

	while(!bWriteSucess)
	{
		WaitWriteH2cLimmit--;
		if(WaitWriteH2cLimmit == 0)
		{	
			DBG_8192C("FillH2CCmd92C():Write H2C fail because no trigger for FW INT!!!!!!!!\n");
			break;
		}
	
		// 2. Find the last BOX number which has been writen.
		BoxNum = pHalData->LastHMEBoxNum;
		switch(BoxNum)
		{
			case 0:
				BOXReg = REG_HMEBOX_0;
				BOXExtReg = REG_HMEBOX_EXT_0;
				break;
			case 1:
				BOXReg = REG_HMEBOX_1;
				BOXExtReg = REG_HMEBOX_EXT_1;
				break;
			case 2:
				BOXReg = REG_HMEBOX_2;
				BOXExtReg = REG_HMEBOX_EXT_2;
				break;
			case 3:
				BOXReg = REG_HMEBOX_3;
				BOXExtReg = REG_HMEBOX_EXT_3;
				break;
			default:
				break;
		}

		// 3. Check if the box content is empty.
		IsFwRead = CheckFwReadLastH2C(padapter, BoxNum);
		while(!IsFwRead)
		{
			//wait until Fw read
			WaitH2cLimmit--;
			if(WaitH2cLimmit == 0)
			{
				DBG_8192C("FillH2CCmd92C(): Wating too long for FW read clear HMEBox(%d)!!!\n", BoxNum);
				break;
			}
			rtw_udelay_os(10); //us
			IsFwRead = CheckFwReadLastH2C(padapter, BoxNum);
			U1btmp = rtw_read8(padapter, 0x1BF);
			//DBG_8192C("FillH2CCmd92C(): Wating for FW read clear HMEBox(%d)!!! 0x1BF = %2x\n", BoxNum, U1btmp);
		}

		// If Fw has not read the last H2C cmd, break and give up this H2C.
		if(!IsFwRead)
		{
			DBG_8192C("FillH2CCmd92C():  Write H2C register BOX[%d] fail!!!!! Fw do not read. \n", BoxNum);
			break;
		}

		// 4. Fill the H2C cmd into box		
		_rtw_memset(BoxContent, 0, sizeof(BoxContent));
		_rtw_memset(BoxExtContent, 0, sizeof(BoxExtContent));
		
		BoxContent[0] = ElementID; // Fill element ID

		//DBG_8192C("FillH2CCmd92C():Write ElementID BOXReg(%4x) = %2x \n", BOXReg, ElementID);

		switch(CmdLen)
		{
			case 1:
			{
				BoxContent[0] &= ~(BIT7);
				_rtw_memcpy((u8 *)(BoxContent)+1, pCmdBuffer+BufIndex, 1);
				//PlatformEFIOWrite4Byte(Adapter, BOXReg, *((pu4Byte)BoxContent));
				//For Endian Free.
				for(idx= 0; idx < 4; idx++)
				{
					rtw_write8(padapter, BOXReg+idx, BoxContent[idx]);
				}
				break;
			}
			case 2:
			{	
				BoxContent[0] &= ~(BIT7);
				_rtw_memcpy((u8 *)(BoxContent)+1, pCmdBuffer+BufIndex, 2);
				//PlatformEFIOWrite4Byte(Adapter, BOXReg, *((pu4Byte)BoxContent));
				for(idx=0; idx < 4; idx++)
				{
					rtw_write8(padapter, BOXReg+idx, BoxContent[idx]);
				}
				break;
			}
			case 3:
			{
				BoxContent[0] &= ~(BIT7);
				_rtw_memcpy((u8 *)(BoxContent)+1, pCmdBuffer+BufIndex, 3);
				//PlatformEFIOWrite4Byte(Adapter, BOXReg, *((pu4Byte)BoxContent));
				for(idx = 0; idx < 4 ; idx++)
				{
					rtw_write8(padapter, BOXReg+idx, BoxContent[idx]);
				}
				break;
			}
			case 4:
			{
				BoxContent[0] |= (BIT7);
				_rtw_memcpy((u8 *)(BoxExtContent), pCmdBuffer+BufIndex, 2);
				_rtw_memcpy((u8 *)(BoxContent)+1, pCmdBuffer+BufIndex+2, 2);
				//PlatformEFIOWrite2Byte(Adapter, BOXExtReg, *((pu2Byte)BoxExtContent));
				//PlatformEFIOWrite4Byte(Adapter, BOXReg, *((pu4Byte)BoxContent));
				for(idx = 0 ; idx < 2 ; idx ++)
				{
					rtw_write8(padapter, BOXExtReg+idx, BoxExtContent[idx]);
				}		
				for(idx = 0 ; idx < 4 ; idx ++)
				{
					rtw_write8(padapter, BOXReg+idx, BoxContent[idx]);
				}
				break;
			}
			case 5:
			{
				BoxContent[0] |= (BIT7);
				_rtw_memcpy((u8 *)(BoxExtContent), pCmdBuffer+BufIndex, 2);
				_rtw_memcpy((u8 *)(BoxContent)+1, pCmdBuffer+BufIndex+2, 3);
				//PlatformEFIOWrite2Byte(Adapter, BOXExtReg, *((pu2Byte)BoxExtContent));
				//PlatformEFIOWrite4Byte(Adapter, BOXReg, *((pu4Byte)BoxContent));
				for(idx = 0 ; idx < 2 ; idx ++)
				{
					rtw_write8(padapter, BOXExtReg+idx, BoxExtContent[idx]);
				}
				for(idx = 0 ; idx < 4 ; idx ++)
				{
					rtw_write8(padapter, BOXReg+idx, BoxContent[idx]);
				}
				break;
			}
			default:
				break;
		}

		//DBG_8192C("FillH2CCmd(): BoxExtContent=0x%04x\n", *(u16*)BoxExtContent);		
		//DBG_8192C("FillH2CCmd(): BoxContent=0x%08x\n", *(u32*)BoxContent);

		// 5. Normal chip does not need to check if the H2C cmd has be written successfully.
		// 92D test chip does not need to check,
		bWriteSucess = _TRUE;

		// Record the next BoxNum
		pHalData->LastHMEBoxNum = BoxNum+1;
		if(pHalData->LastHMEBoxNum == 4) // loop to 0
			pHalData->LastHMEBoxNum = 0;
		
		//DBG_8192C("FillH2CCmd92C():pHalData->LastHMEBoxNum  = %d\n", pHalData->LastHMEBoxNum);
		
	}

     	_exit_critical_mutex(&(adapter_to_dvobj(padapter)->h2c_fwcmd_mutex), NULL);

_func_exit_;
}

VOID
FillH2CCmd92D(	
	IN	PADAPTER	Adapter,
	IN	u8 	ElementID,
	IN	u32 	CmdLen,
	IN	u8*	pCmdBuffer
)
{
	u32	tmpCmdBuf[2];	

	//Adapter = ADJUST_TO_ADAPTIVE_ADAPTER(Adapter, TRUE);

	if(Adapter->bFWReady == _FALSE)
	{
		DBG_8192C("FillH2CCmd92D(): return H2C cmd because of Fw download fail!!!\n");
		return;
	}

	_rtw_memset(tmpCmdBuf, 0, 8);
	_rtw_memcpy(tmpCmdBuf, pCmdBuffer, CmdLen);

	_FillH2CCmd92D(Adapter, ElementID, CmdLen, (u8 *)&tmpCmdBuf);

	return;
}

u8 rtl8192d_h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf);
u8 rtl8192d_h2c_msg_hdl(_adapter *padapter, unsigned char *pbuf)
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

	FillH2CCmd92D(padapter, ElementID, CmdLen, pCmdBuffer);

	return H2C_SUCCESS;
}


u8 rtl8192d_set_raid_cmd(_adapter*padapter, u32 mask, u8 arg)
{	
	u8	buf[5];
	u8	res=_SUCCESS;
	
_func_enter_;	
	
	_rtw_memset(buf, 0, 5);
	mask = cpu_to_le32( mask );
	_rtw_memcpy(buf, &mask, 4);
	buf[4]  = arg;

	FillH2CCmd92D(padapter, H2C_RA_MASK, 5, buf);
	
_func_exit_;

	return res;

}

//bitmap[0:27] = tx_rate_bitmap
//bitmap[28:31]= Rate Adaptive id
//arg[0:4] = macid
//arg[5] = Short GI
void rtl8192d_Add_RateATid(PADAPTER pAdapter, u32 bitmap, u8 arg)
{	
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
		
	if(pHalData->fw_ractrl == _TRUE)
	{
		rtl8192d_set_raid_cmd(pAdapter, bitmap, arg);
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


void rtl8192d_set_FwPwrMode_cmd(_adapter*padapter, u8 Mode)
{
	struct pwrctrl_priv *pwrpriv = &padapter->pwrctrlpriv;
	u8	u1H2CSetPwrMode[3]={0};
	u8	beacon_interval = 1;
	
_func_enter_;

	DBG_871X("%s(): Mode = %d, SmartPS = %d\n", __FUNCTION__,Mode,pwrpriv->smart_ps);

	SET_H2CCMD_PWRMODE_PARM_MODE(u1H2CSetPwrMode, Mode);
	SET_H2CCMD_PWRMODE_PARM_SMART_PS(u1H2CSetPwrMode, pwrpriv->smart_ps);
	SET_H2CCMD_PWRMODE_PARM_BCN_PASS_TIME(u1H2CSetPwrMode, beacon_interval);

	FillH2CCmd92D(padapter, H2C_SETPWRMODE, 3, u1H2CSetPwrMode);

_func_exit_;
}

void ConstructBeacon(_adapter *padapter, u8 *pframe, u32 *pLength);
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
		DBG_871X("ie len=%u\n", cur_network->IELength);
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

	//DBG_871X("%s bcn_sz=%u\n", __FUNCTION__, pktlen);

}

void ConstructPSPoll(_adapter *padapter, u8 *pframe, u32 *pLength);
void ConstructPSPoll(_adapter *padapter, u8 *pframe, u32 *pLength)
{
	struct rtw_ieee80211_hdr	*pwlanhdr;
	u16					*fctrl;
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
	SetDuration(pframe, (pmlmeinfo->aid| 0xc000));

	// BSSID.
	_rtw_memcpy(pwlanhdr->addr1, get_my_bssid(&(pmlmeinfo->network)), ETH_ALEN);

	// TA.
	_rtw_memcpy(pwlanhdr->addr2, myid(&(padapter->eeprompriv)), ETH_ALEN);

	*pLength = 16;
}

void ConstructNullFunctionData(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bForcePowerSave);
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

void ConstructProbeRsp(_adapter *padapter, u8 *pframe, u32 *pLength, u8 *StaAddr, BOOLEAN bHideSSID);
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
FillFakeTxDescriptor92D(
	IN PADAPTER		Adapter,
	IN u8*			pDesc,
	IN u32			BufferLen,
	IN BOOLEAN		IsPsPoll
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
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

	if(pHalData->CurrentBandType92D == BAND_ON_5G)
		ptxdesc->txdw5 |= cpu_to_le32(BIT(2));// use OFDM 6Mbps

#ifdef CONFIG_USB_HCI
	// USB interface drop packet if the checksum of descriptor isn't correct.
	// Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	rtl8192du_cal_txdesc_chksum(ptxdesc);
#endif

	RT_PRINT_DATA(_module_rtl8712_cmd_c_, _drv_info_, "FillFakeTxDescriptor92D(): H2C Tx Desc Content ----->\n", pDesc, TXDESC_SIZE);
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
void SetFwRsvdPagePkt(PADAPTER Adapter, BOOLEAN bDLFinished);
void SetFwRsvdPagePkt(PADAPTER Adapter, BOOLEAN bDLFinished)
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
	u8	u1RsvdPageLoc[3]={0};
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

	RT_PRINT_DATA(_module_rtl8712_cmd_c_, _drv_info_, 
		"SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: BCN\n", 
		&ReservedPagePacket[BufIndex], (BeaconLength+BufIndex));

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
	
	FillFakeTxDescriptor92D(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], PSPollLength, _TRUE);

	RT_PRINT_DATA(_module_rtl8712_cmd_c_, _drv_info_, 
		"SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: PS-POLL\n", 
		&ReservedPagePacket[BufIndex-TxDescLen], (PSPollLength+TxDescLen));

	SET_H2CCMD_RSVDPAGE_LOC_PSPOLL(u1RsvdPageLoc, PageNum );

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
	
	FillFakeTxDescriptor92D(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], NullFunctionDataLength, _FALSE);

	SET_H2CCMD_RSVDPAGE_LOC_NULL_DATA(u1RsvdPageLoc, PageNum);

	RT_PRINT_DATA(_module_rtl8712_cmd_c_, _drv_info_, 
		"SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: NULL DATA \n", 
		&ReservedPagePacket[BufIndex-TxDescLen], (NullFunctionDataLength+TxDescLen));
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
	
	FillFakeTxDescriptor92D(Adapter, &ReservedPagePacket[BufIndex-TxDescLen], ProbeRspLength, _FALSE);

	SET_H2CCMD_RSVDPAGE_LOC_PROBE_RSP(u1RsvdPageLoc, PageNum);

	RT_PRINT_DATA(_module_rtl8712_cmd_c_, _drv_info_, 
		"SetFwRsvdPagePkt(): HW_VAR_SET_TX_CMD: PROBE RSP \n", 
		&ReservedPagePacket[BufIndex-TxDescLen], (ProbeRspLength-TxDescLen));

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
		FillH2CCmd92D(Adapter, H2C_RSVDPAGE, sizeof(u1RsvdPageLoc), u1RsvdPageLoc);
		//FillH2CCmd92D(Adapter, H2C_RSVDPAGE, sizeof(RsvdPageLoc), (u8 *)&RsvdPageLoc);
	}

	rtw_mfree(ReservedPagePacket,1000);
}

void rtl8192d_set_FwJoinBssReport_cmd(_adapter* padapter, u8 mstatus)
{
	u8	u1JoinBssRptParm[1]={0};
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	BOOLEAN	bRecover = _FALSE;
	
_func_enter_;

	DBG_871X("%s\n", __FUNCTION__);

	if(mstatus == 1)
	{
		// We should set AID, correct TSF, HW seq enable before set JoinBssReport to Fw in 88/92C.
		// Suggested by filen. Added by tynli.
		rtw_write16(padapter, REG_BCN_PSR_RPT, (0xC000|pmlmeinfo->aid));
		// Do not set TSF again here or vWiFi beacon DMA INT will not work.
		//rtw_hal_set_hwreg(Adapter, HW_VAR_CORRECT_TSF, (pu1Byte)(&bTypeIbss));
		// Hw sequende enable by dedault. 2010.06.23. by tynli.

		//set REG_CR bit 8
		pHalData->RegCR_1 |= BIT0;
		rtw_write8(padapter,  REG_CR+1, pHalData->RegCR_1);

		// Disable Hw protection for a time which revserd for Hw sending beacon.
		// Fix download reserved page packet fail that access collision with the protection time.
		// 2010.05.11. Added by tynli.
		//SetBcnCtrlReg(Adapter, 0, BIT3);
		//SetBcnCtrlReg(Adapter, BIT4, 0);
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~BIT(3)));
		rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(4));

		// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
		if(pHalData->RegFwHwTxQCtrl&BIT6)
			bRecover = _TRUE;
		rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT6)));
		pHalData->RegFwHwTxQCtrl &= (~BIT6);
		SetFwRsvdPagePkt(padapter, 0);
				
		// 2010.05.11. Added by tynli.
		//SetBcnCtrlReg(Adapter, BIT3, 0);
		//SetBcnCtrlReg(Adapter, 0, BIT4);
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
		pHalData->RegCR_1 &= (~BIT0);
		rtw_write8(padapter,  REG_CR+1, pHalData->RegCR_1);
	}

	SET_H2CCMD_JOINBSSRPT_PARM_OPMODE(u1JoinBssRptParm, mstatus);
	
	FillH2CCmd92D(padapter, H2C_JOINBSSRPT, 1, u1JoinBssRptParm);

_func_exit_;
}

#ifdef CONFIG_P2P_PS
void rtl8192d_set_p2p_ctw_period_cmd(_adapter* padapter, u8 ctwindow)
{
	u8	CTWPeriod = ctwindow;

	FillH2CCmd92D(padapter, H2C_P2P_PS_CTW_CMD, 1, (u8 *)(&CTWPeriod));
	
}

void rtl8192d_set_p2p_ps_offload_cmd(_adapter* padapter, u8 p2p_ps_state)
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
				rtl8192d_set_p2p_ctw_period_cmd(padapter, ctwindow);
				//rtw_write16(padapter, REG_ATIMWND, ctwindow);
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

	FillH2CCmd92D(padapter, H2C_P2P_PS_OFFLOAD, 1, (u8 *)p2p_ps_offload);

_func_exit_;

}
#endif // CONFIG_P2P_PS



#ifdef CONFIG_TSF_RESET_OFFLOAD
/*
	ask FW to Reset sync register at Beacon early interrupt
*/
u8 rtl8192d_reset_tsf(_adapter *padapter, u8 reset_port )
{	
	u8	buf[2];
	u8	res=_SUCCESS;
	
_func_enter_;
	if (IFACE_PORT0==reset_port) {
		buf[0] = 0x1; buf[1] = 0;
	
	} else{
		buf[0] = 0x0; buf[1] = 0x1;
	}
	FillH2CCmd92D(padapter, H2C_92D_RESET_TSF, 2, buf);
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
	rtl8192d_reset_tsf(Adapter, reset_port);

	while ((reset_cnt_after == reset_cnt_before ) && (loop_cnt < 10)) {
		rtw_msleep_os(100);
		loop_cnt++;
		reset_cnt_after = rtw_read8(Adapter, reg_reset_tsf_cnt);
	}

	return(loop_cnt >= 10) ? _FAIL : _TRUE;
}


#endif	// CONFIG_TSF_RESET_OFFLOAD

#ifdef CONFIG_WOWLAN

void rtl8192d_set_wowlan_cmd(_adapter* padapter)
{
	u8	res=_SUCCESS;
	u32 test=0;
	struct recv_priv	*precvpriv = &padapter->recvpriv;

	SETWOWLAN_PARM pwowlan_parm;
	struct pwrctrl_priv *pwrpriv=&padapter->pwrctrlpriv;
	
_func_enter_;

	pwowlan_parm.mode =0;
	pwowlan_parm.gpio_index=0;
	pwowlan_parm.gpio_duration=0;
	pwowlan_parm.second_mode =0;
	pwowlan_parm.reserve=0;


	
	if(pwrpriv->wowlan_mode ==_TRUE){
		//pause RX DMA
		test = rtw_read8(padapter, REG_RXPKT_NUM+2);
		test |= BIT(2);
		rtw_write8(padapter, REG_RXPKT_NUM+2, test);
		//286 BIT(1) , not 1(means idle) do rx urb
		test = rtw_read8(padapter, REG_RXPKT_NUM+2) & BIT(1);
		//printk("line(%d) 0x286=%d\n", __LINE__, rtw_read8(padapter, REG_RXPKT_NUM+2));
		//check DMA idle?
		while(test != BIT(1))
		{
			tasklet_schedule(&precvpriv->recv_tasklet);
			test = rtw_read8(padapter, REG_RXPKT_NUM+2) & BIT(1);
			rtw_msleep_os(10);
			//printk("line(%d) 0x286=%d\n", __LINE__, test);
		}
		//mask usb se0 reset by Alex and DD
		test = rtw_read8(padapter, 0xf8);
		test &= ~(BIT(3)|BIT(4));
		rtw_write8(padapter, 0xf8, test);

		pwowlan_parm.mode |=FW_WOWLAN_FUN_EN;
		//printk("\n %s 1.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		if(pwrpriv->wowlan_pattern ==_TRUE){
			pwowlan_parm.mode |= FW_WOWLAN_PATTERN_MATCH;
		//printk("\n %s 2.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		if(pwrpriv->wowlan_magic ==_TRUE){
			//pwowlan_parm.mode |=FW_WOWLAN_MAGIC_PKT;
		//printk("\n %s 3.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		if(pwrpriv->wowlan_unicast ==_TRUE){
			pwowlan_parm.mode |=FW_WOWLAN_UNICAST;
		//printk("\n %s 4.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		}
		
		rtl8192d_set_FwJoinBssReport_cmd(padapter, 1);
		
		//WOWLAN_GPIO_ACTIVE means GPIO high active
		//pwowlan_parm.mode |=FW_WOWLAN_GPIO_ACTIVE;
		pwowlan_parm.mode |=FW_WOWLAN_REKEY_WAKEUP;
		pwowlan_parm.mode |=FW_WOWLAN_DEAUTH_WAKEUP;
		
		//GPIO 0
		pwowlan_parm.gpio_index=0;
		
		//duration unit is 64us
		pwowlan_parm.gpio_duration=0xff;
		
		pwowlan_parm.second_mode|=FW_WOWLAN_GPIO_WAKEUP_EN;
		pwowlan_parm.second_mode|=FW_FW_PARSE_MAGIC_PKT;
		//printk("\n %s 5.pwowlan_parm.mode=0x%x \n",__FUNCTION__,pwowlan_parm.mode );
		{	u8 *ptr=(u8 *)&pwowlan_parm;
			printk("\n %s H2C_WO_WLAN=%x %02x:%02x:%02x:%02x:%02x \n",__FUNCTION__,H2C_WO_WLAN_CMD,ptr[0],ptr[1],ptr[2],ptr[3],ptr[4] );
		}
		FillH2CCmd92D(padapter, H2C_WO_WLAN_CMD, 4, (u8 *)&pwowlan_parm);
			
		
		//keep alive period = 3 * 10 BCN interval
		pwowlan_parm.mode =3;
		pwowlan_parm.gpio_index=3;
		FillH2CCmd92D(padapter, KEEP_ALIVE_CONTROL_CMD, 2, (u8 *)&pwowlan_parm);
		printk("%s after KEEP_ALIVE_CONTROL_CMD register 0x81=%x \n",__FUNCTION__,rtw_read8(padapter, 0x85));

		pwowlan_parm.mode =1;
		pwowlan_parm.gpio_index=0;
		pwowlan_parm.gpio_duration=0;
		FillH2CCmd92D(padapter, DISCONNECT_DECISION_CTRL_CMD, 3, (u8 *)&pwowlan_parm);
		printk("%s after DISCONNECT_DECISION_CTRL_CMD register 0x81=%x \n",__FUNCTION__,rtw_read8(padapter, 0x85));
		
		//enable GPIO wakeup
		pwowlan_parm.mode =1;
		pwowlan_parm.gpio_index=0;
		pwowlan_parm.gpio_duration=0;
		FillH2CCmd92D(padapter, REMOTE_WAKE_CTRL_CMD, 1, (u8 *)&pwowlan_parm);
		printk("%s after DISCONNECT_DECISION_CTRL_CMD register \n",__FUNCTION__);

	}
	else
		FillH2CCmd92D(padapter, H2C_WO_WLAN_CMD, 4, (u8 *)&pwowlan_parm);

	
_func_exit_;

	return ;

}

#endif  //CONFIG_WOWLAN

