// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <hal_data.h>
#include <hal_btcoex.h>
#include <Mp_Precomp.h>

/* 		Global variables */

struct btc_coexist GLBtCoexist;
static u8 GLBtcWiFiInScanState;
static u8 GLBtcWiFiInIQKState;

/*  */
/* 		Debug related function */
/*  */
static u8 halbtcoutsrc_IsBtCoexistAvailable(struct btc_coexist *pBtCoexist)
{
	if (!pBtCoexist->bBinded || !pBtCoexist->Adapter)
		return false;

	return true;
}

static void halbtcoutsrc_LeaveLps(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;


	padapter = pBtCoexist->Adapter;

	pBtCoexist->btInfo.bBtCtrlLps = true;
	pBtCoexist->btInfo.bBtLpsOn = false;

	rtw_btcoex_LPS_Leave(padapter);
}

static void halbtcoutsrc_EnterLps(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;


	padapter = pBtCoexist->Adapter;

	pBtCoexist->btInfo.bBtCtrlLps = true;
	pBtCoexist->btInfo.bBtLpsOn = true;

	rtw_btcoex_LPS_Enter(padapter);
}

static void halbtcoutsrc_NormalLps(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;

	padapter = pBtCoexist->Adapter;

	if (pBtCoexist->btInfo.bBtCtrlLps) {
		pBtCoexist->btInfo.bBtLpsOn = false;
		rtw_btcoex_LPS_Leave(padapter);
		pBtCoexist->btInfo.bBtCtrlLps = false;

		/*  recover the LPS state to the original */
	}
}

/*
 *  Constraint:
 *   1. this function will request pwrctrl->lock
 */
static void halbtcoutsrc_LeaveLowPower(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;
	s32 ready;
	unsigned long stime;
	unsigned long utime;
	u32 timeout; /*  unit: ms */


	padapter = pBtCoexist->Adapter;
	ready = _FAIL;
#ifdef LPS_RPWM_WAIT_MS
	timeout = LPS_RPWM_WAIT_MS;
#else /*  !LPS_RPWM_WAIT_MS */
	timeout = 30;
#endif /*  !LPS_RPWM_WAIT_MS */

	stime = jiffies;
	do {
		ready = rtw_register_task_alive(padapter, BTCOEX_ALIVE);
		if (_SUCCESS == ready)
			break;

		utime = jiffies_to_msecs(jiffies - stime);
		if (utime > timeout)
			break;

		msleep(1);
	} while (1);
}

/*
 *  Constraint:
 *   1. this function will request pwrctrl->lock
 */
static void halbtcoutsrc_NormalLowPower(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;


	padapter = pBtCoexist->Adapter;
	rtw_unregister_task_alive(padapter, BTCOEX_ALIVE);
}

static void halbtcoutsrc_DisableLowPower(struct btc_coexist *pBtCoexist, u8 bLowPwrDisable)
{
	pBtCoexist->btInfo.bBtDisableLowPwr = bLowPwrDisable;
	if (bLowPwrDisable)
		halbtcoutsrc_LeaveLowPower(pBtCoexist);		/*  leave 32k low power. */
	else
		halbtcoutsrc_NormalLowPower(pBtCoexist);	/*  original 32k low power behavior. */
}

static void halbtcoutsrc_AggregationCheck(struct btc_coexist *pBtCoexist)
{
	struct adapter *padapter;
	bool bNeedToAct;


	padapter = pBtCoexist->Adapter;
	bNeedToAct = false;

	if (pBtCoexist->btInfo.bRejectAggPkt) {
		rtw_btcoex_RejectApAggregatedPacket(padapter, true);
	} else {
		if (pBtCoexist->btInfo.bPreBtCtrlAggBufSize !=
			pBtCoexist->btInfo.bBtCtrlAggBufSize) {
			bNeedToAct = true;
			pBtCoexist->btInfo.bPreBtCtrlAggBufSize = pBtCoexist->btInfo.bBtCtrlAggBufSize;
		}

		if (pBtCoexist->btInfo.bBtCtrlAggBufSize) {
			if (pBtCoexist->btInfo.preAggBufSize !=
				pBtCoexist->btInfo.aggBufSize){
				bNeedToAct = true;
			}
			pBtCoexist->btInfo.preAggBufSize = pBtCoexist->btInfo.aggBufSize;
		}

		if (bNeedToAct) {
			rtw_btcoex_RejectApAggregatedPacket(padapter, true);
			rtw_btcoex_RejectApAggregatedPacket(padapter, false);
		}
	}
}

static u8 halbtcoutsrc_IsWifiBusy(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv;


	pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == true) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true)
			return true;
		if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
			return true;
	}

	return false;
}

static u32 _halbtcoutsrc_GetWifiLinkStatus(struct adapter *padapter)
{
	struct mlme_priv *pmlmepriv;
	u8 bp2p;
	u32 portConnectedStatus;


	pmlmepriv = &padapter->mlmepriv;
	bp2p = false;
	portConnectedStatus = 0;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == true) {
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
			if (bp2p)
				portConnectedStatus |= WIFI_P2P_GO_CONNECTED;
			else
				portConnectedStatus |= WIFI_AP_CONNECTED;
		} else {
			if (bp2p)
				portConnectedStatus |= WIFI_P2P_GC_CONNECTED;
			else
				portConnectedStatus |= WIFI_STA_CONNECTED;
		}
	}

	return portConnectedStatus;
}

static u32 halbtcoutsrc_GetWifiLinkStatus(struct btc_coexist *pBtCoexist)
{
	/*  */
	/*  return value: */
	/*  [31:16]=> connected port number */
	/*  [15:0]=> port connected bit define */
	/*  */

	struct adapter *padapter;
	u32 retVal;
	u32 portConnectedStatus, numOfConnectedPort;


	padapter = pBtCoexist->Adapter;
	portConnectedStatus = 0;
	numOfConnectedPort = 0;

	retVal = _halbtcoutsrc_GetWifiLinkStatus(padapter);
	if (retVal) {
		portConnectedStatus |= retVal;
		numOfConnectedPort++;
	}

	retVal = (numOfConnectedPort << 16) | portConnectedStatus;

	return retVal;
}

static u32 halbtcoutsrc_GetBtPatchVer(struct btc_coexist *pBtCoexist)
{
	return pBtCoexist->btInfo.btRealFwVer;
}

static s32 halbtcoutsrc_GetWifiRssi(struct adapter *padapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);

	return pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;
}

static u8 halbtcoutsrc_GetWifiScanAPNum(struct adapter *padapter)
{
	struct mlme_ext_priv *pmlmeext;
	static u8 scan_AP_num;

	pmlmeext = &padapter->mlmeextpriv;

	if (!GLBtcWiFiInScanState) {
		if (pmlmeext->sitesurvey_res.bss_cnt > 0xFF)
			scan_AP_num = 0xFF;
		else
			scan_AP_num = (u8)pmlmeext->sitesurvey_res.bss_cnt;
	}

	return scan_AP_num;
}

static u8 halbtcoutsrc_Get(void *pBtcContext, u8 getType, void *pOutBuf)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;
	struct hal_com_data *pHalData;
	struct mlme_ext_priv *mlmeext;
	u8 *pu8;
	s32 *pS4Tmp;
	u32 *pU4Tmp;
	u8 ret;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return false;

	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	mlmeext = &padapter->mlmeextpriv;
	pu8 = pOutBuf;
	pS4Tmp = pOutBuf;
	pU4Tmp = pOutBuf;
	ret = true;

	switch (getType) {
	case BTC_GET_BL_HS_OPERATION:
		*pu8 = false;
		ret = false;
		break;

	case BTC_GET_BL_HS_CONNECTING:
		*pu8 = false;
		ret = false;
		break;

	case BTC_GET_BL_WIFI_CONNECTED:
		*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE);
		break;

	case BTC_GET_BL_WIFI_BUSY:
		*pu8 = halbtcoutsrc_IsWifiBusy(padapter);
		break;

	case BTC_GET_BL_WIFI_SCAN:
		/* Use the value of the new variable GLBtcWiFiInScanState to judge whether WiFi is in scan state or not, since the originally used flag
			WIFI_SITE_MONITOR in fwstate may not be cleared in time */
		*pu8 = GLBtcWiFiInScanState;
		break;

	case BTC_GET_BL_WIFI_LINK:
		*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING);
		break;

	case BTC_GET_BL_WIFI_ROAM:
		*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING);
		break;

	case BTC_GET_BL_WIFI_4_WAY_PROGRESS:
		*pu8 = false;
		break;

	case BTC_GET_BL_WIFI_AP_MODE_ENABLE:
		*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE);
		break;

	case BTC_GET_BL_WIFI_ENABLE_ENCRYPTION:
		*pu8 = padapter->securitypriv.dot11PrivacyAlgrthm != 0;
		break;

	case BTC_GET_BL_WIFI_UNDER_B_MODE:
		if (mlmeext->cur_wireless_mode == WIRELESS_11B)
			*pu8 = true;
		else
			*pu8 = false;
		break;

	case BTC_GET_BL_WIFI_IS_IN_MP_MODE:
		*pu8 = false;
		break;

	case BTC_GET_BL_EXT_SWITCH:
		*pu8 = false;
		break;

	case BTC_GET_S4_WIFI_RSSI:
		*pS4Tmp = halbtcoutsrc_GetWifiRssi(padapter);
		break;

	case BTC_GET_S4_HS_RSSI:
		*pS4Tmp = 0;
		ret = false;
		break;

	case BTC_GET_U4_WIFI_BW:
		if (is_legacy_only(mlmeext->cur_wireless_mode))
			*pU4Tmp = BTC_WIFI_BW_LEGACY;
		else if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
			*pU4Tmp = BTC_WIFI_BW_HT20;
		else
			*pU4Tmp = BTC_WIFI_BW_HT40;
		break;

	case BTC_GET_U4_WIFI_TRAFFIC_DIRECTION:
		{
			struct rt_link_detect_t *plinkinfo;
			plinkinfo = &padapter->mlmepriv.LinkDetectInfo;

			if (plinkinfo->NumTxOkInPeriod > plinkinfo->NumRxOkInPeriod)
				*pU4Tmp = BTC_WIFI_TRAFFIC_TX;
			else
				*pU4Tmp = BTC_WIFI_TRAFFIC_RX;
		}
		break;

	case BTC_GET_U4_WIFI_FW_VER:
		*pU4Tmp = pHalData->FirmwareVersion << 16;
		*pU4Tmp |= pHalData->FirmwareSubVersion;
		break;

	case BTC_GET_U4_WIFI_LINK_STATUS:
		*pU4Tmp = halbtcoutsrc_GetWifiLinkStatus(pBtCoexist);
		break;

	case BTC_GET_U4_BT_PATCH_VER:
		*pU4Tmp = halbtcoutsrc_GetBtPatchVer(pBtCoexist);
		break;

	case BTC_GET_U1_WIFI_DOT11_CHNL:
		*pu8 = padapter->mlmeextpriv.cur_channel;
		break;

	case BTC_GET_U1_WIFI_CENTRAL_CHNL:
		*pu8 = pHalData->CurrentChannel;
		break;

	case BTC_GET_U1_WIFI_HS_CHNL:
		*pu8 = 0;
		ret = false;
		break;

	case BTC_GET_U1_MAC_PHY_MODE:
		*pu8 = BTC_SMSP;
/* 			*pU1Tmp = BTC_DMSP; */
/* 			*pU1Tmp = BTC_DMDP; */
/* 			*pU1Tmp = BTC_MP_UNKNOWN; */
		break;

	case BTC_GET_U1_AP_NUM:
		*pu8 = halbtcoutsrc_GetWifiScanAPNum(padapter);
		break;

	/* 1Ant =========== */
	case BTC_GET_U1_LPS_MODE:
		*pu8 = padapter->dvobj->pwrctl_priv.pwr_mode;
		break;

	default:
		ret = false;
		break;
	}

	return ret;
}

static u8 halbtcoutsrc_Set(void *pBtcContext, u8 setType, void *pInBuf)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;
	u8 *pu8;
	u32 *pU4Tmp;
	u8 ret;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;
	pu8 = pInBuf;
	pU4Tmp = pInBuf;
	ret = true;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return false;

	switch (setType) {
	/*  set some u8 type variables. */
	case BTC_SET_BL_BT_DISABLE:
		pBtCoexist->btInfo.bBtDisabled = *pu8;
		break;

	case BTC_SET_BL_BT_TRAFFIC_BUSY:
		pBtCoexist->btInfo.bBtBusy = *pu8;
		break;

	case BTC_SET_BL_BT_LIMITED_DIG:
		pBtCoexist->btInfo.bLimitedDig = *pu8;
		break;

	case BTC_SET_BL_FORCE_TO_ROAM:
		pBtCoexist->btInfo.bForceToRoam = *pu8;
		break;

	case BTC_SET_BL_TO_REJ_AP_AGG_PKT:
		pBtCoexist->btInfo.bRejectAggPkt = *pu8;
		break;

	case BTC_SET_BL_BT_CTRL_AGG_SIZE:
		pBtCoexist->btInfo.bBtCtrlAggBufSize = *pu8;
		break;

	case BTC_SET_BL_INC_SCAN_DEV_NUM:
		pBtCoexist->btInfo.bIncreaseScanDevNum = *pu8;
		break;

	case BTC_SET_BL_BT_TX_RX_MASK:
		pBtCoexist->btInfo.bBtTxRxMask = *pu8;
		break;

	/*  set some u8 type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON:
		pBtCoexist->btInfo.rssiAdjustForAgcTableOn = *pu8;
		break;

	case BTC_SET_U1_AGG_BUF_SIZE:
		pBtCoexist->btInfo.aggBufSize = *pu8;
		break;

	/*  the following are some action which will be triggered */
	case BTC_SET_ACT_GET_BT_RSSI:
		ret = false;
		break;

	case BTC_SET_ACT_AGGREGATE_CTRL:
		halbtcoutsrc_AggregationCheck(pBtCoexist);
		break;

	/* 1Ant =========== */
	/*  set some u8 type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE:
		pBtCoexist->btInfo.rssiAdjustFor1AntCoexType = *pu8;
		break;

	case BTC_SET_U1_LPS_VAL:
		pBtCoexist->btInfo.lpsVal = *pu8;
		break;

	case BTC_SET_U1_RPWM_VAL:
		pBtCoexist->btInfo.rpwmVal = *pu8;
		break;

	/*  the following are some action which will be triggered */
	case BTC_SET_ACT_LEAVE_LPS:
		halbtcoutsrc_LeaveLps(pBtCoexist);
		break;

	case BTC_SET_ACT_ENTER_LPS:
		halbtcoutsrc_EnterLps(pBtCoexist);
		break;

	case BTC_SET_ACT_NORMAL_LPS:
		halbtcoutsrc_NormalLps(pBtCoexist);
		break;

	case BTC_SET_ACT_DISABLE_LOW_POWER:
		halbtcoutsrc_DisableLowPower(pBtCoexist, *pu8);
		break;

	case BTC_SET_ACT_UPDATE_RAMASK:
		pBtCoexist->btInfo.raMask = *pU4Tmp;

		if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == true) {
			struct sta_info *psta;
			struct wlan_bssid_ex *cur_network;

			cur_network = &padapter->mlmeextpriv.mlmext_info.network;
			psta = rtw_get_stainfo(&padapter->stapriv, cur_network->mac_address);
			rtw_hal_update_ra_mask(psta, 0);
		}
		break;

	case BTC_SET_ACT_SEND_MIMO_PS:
		ret = false;
		break;

	case BTC_SET_ACT_CTRL_BT_INFO:
		ret = false;
		break;

	case BTC_SET_ACT_CTRL_BT_COEX:
		ret = false;
		break;
	case BTC_SET_ACT_CTRL_8723B_ANT:
		ret = false;
		break;
	/*  */
	default:
		ret = false;
		break;
	}

	return ret;
}

/*  */
/* 		IO related function */
/*  */
static u8 halbtcoutsrc_Read1Byte(void *pBtcContext, u32 RegAddr)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return rtw_read8(padapter, RegAddr);
}

static u16 halbtcoutsrc_Read2Byte(void *pBtcContext, u32 RegAddr)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read16(padapter, RegAddr);
}

static u32 halbtcoutsrc_Read4Byte(void *pBtcContext, u32 RegAddr)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read32(padapter, RegAddr);
}

static void halbtcoutsrc_Write1Byte(void *pBtcContext, u32 RegAddr, u8 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write8(padapter, RegAddr, Data);
}

static void halbtcoutsrc_BitMaskWrite1Byte(void *pBtcContext, u32 regAddr, u8 bitMask, u8 data1b)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;
	u8 originalValue, bitShift;
	u8 i;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;
	originalValue = 0;
	bitShift = 0;

	if (bitMask != 0xFF) {
		originalValue = rtw_read8(padapter, regAddr);

		for (i = 0; i <= 7; i++) {
			if ((bitMask >> i) & 0x1)
				break;
		}
		bitShift = i;

		data1b = (originalValue & ~bitMask) | ((data1b << bitShift) & bitMask);
	}

	rtw_write8(padapter, regAddr, data1b);
}

static void halbtcoutsrc_Write2Byte(void *pBtcContext, u32 RegAddr, u16 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write16(padapter, RegAddr, Data);
}

static void halbtcoutsrc_Write4Byte(void *pBtcContext, u32 RegAddr, u32 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write32(padapter, RegAddr, Data);
}

static void halbtcoutsrc_WriteLocalReg1Byte(void *pBtcContext, u32 RegAddr, u8 Data)
{
	struct btc_coexist *pBtCoexist = (struct btc_coexist *)pBtcContext;
	struct adapter *Adapter = pBtCoexist->Adapter;

	if (BTC_INTF_SDIO == pBtCoexist->chipInterface)
		rtw_write8(Adapter, SDIO_LOCAL_BASE | RegAddr, Data);
	else
		rtw_write8(Adapter, RegAddr, Data);
}

static void halbtcoutsrc_SetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	PHY_SetBBReg(padapter, RegAddr, BitMask, Data);
}


static u32 halbtcoutsrc_GetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return PHY_QueryBBReg(padapter, RegAddr, BitMask);
}

static void halbtcoutsrc_SetRfReg(void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	PHY_SetRFReg(padapter, eRFPath, RegAddr, BitMask, Data);
}

static u32 halbtcoutsrc_GetRfReg(void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return PHY_QueryRFReg(padapter, eRFPath, RegAddr, BitMask);
}

static void halbtcoutsrc_SetBtReg(void *pBtcContext, u8 RegType, u32 RegAddr, u32 Data)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;
	u8 CmdBuffer1[4] = {0};
	u8 CmdBuffer2[4] = {0};
	u8 *AddrToSet = (u8 *)&RegAddr;
	u8 *ValueToSet = (u8 *)&Data;
	u8 OperVer = 0;
	u8 ReqNum = 0;

	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	CmdBuffer1[0] |= (OperVer & 0x0f);						/* Set OperVer */
	CmdBuffer1[0] |= ((ReqNum << 4) & 0xf0);				/* Set ReqNum */
	CmdBuffer1[1] = 0x0d;									/* Set OpCode to BT_LO_OP_WRITE_REG_VALUE */
	CmdBuffer1[2] = ValueToSet[0];							/* Set WriteRegValue */
	rtw_hal_fill_h2c_cmd(padapter, 0x67, 4, &(CmdBuffer1[0]));

	msleep(200);
	ReqNum++;

	CmdBuffer2[0] |= (OperVer & 0x0f);						/* Set OperVer */
	CmdBuffer2[0] |= ((ReqNum << 4) & 0xf0);				/* Set ReqNum */
	CmdBuffer2[1] = 0x0c;									/* Set OpCode of BT_LO_OP_WRITE_REG_ADDR */
	CmdBuffer2[3] = AddrToSet[0];							/* Set WriteRegAddr */
	rtw_hal_fill_h2c_cmd(padapter, 0x67, 4, &(CmdBuffer2[0]));
}

static u32 halbtcoutsrc_GetBtReg(void *pBtcContext, u8 RegType, u32 RegAddr)
{
	/* To be implemented. Always return 0 temporarily */
	return 0;
}

static void halbtcoutsrc_FillH2cCmd(void *pBtcContext, u8 elementId, u32 cmdLen, u8 *pCmdBuffer)
{
	struct btc_coexist *pBtCoexist;
	struct adapter *padapter;


	pBtCoexist = (struct btc_coexist *)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_hal_fill_h2c_cmd(padapter, elementId, cmdLen, pCmdBuffer);
}

/*  */
/* 		Extern functions called by other module */
/*  */
static u8 EXhalbtcoutsrc_BindBtCoexWithAdapter(void *padapter)
{
	struct btc_coexist *pBtCoexist = &GLBtCoexist;

	if (pBtCoexist->bBinded)
		return false;
	else
		pBtCoexist->bBinded = true;

	pBtCoexist->statistics.cntBind++;

	pBtCoexist->Adapter = padapter;

	pBtCoexist->stackInfo.bProfileNotified = false;

	pBtCoexist->btInfo.bBtCtrlAggBufSize = false;
	pBtCoexist->btInfo.aggBufSize = 5;

	pBtCoexist->btInfo.bIncreaseScanDevNum = false;

	/*  set default antenna position to main  port */
	pBtCoexist->boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;

	return true;
}

void hal_btcoex_Initialize(void *padapter)
{
	struct btc_coexist *pBtCoexist;

	memset(&GLBtCoexist, 0, sizeof(GLBtCoexist));

	pBtCoexist = &GLBtCoexist;

	/* pBtCoexist->statistics.cntBind++; */

	pBtCoexist->chipInterface = BTC_INTF_SDIO;

	EXhalbtcoutsrc_BindBtCoexWithAdapter(padapter);

	pBtCoexist->fBtcRead1Byte = halbtcoutsrc_Read1Byte;
	pBtCoexist->fBtcWrite1Byte = halbtcoutsrc_Write1Byte;
	pBtCoexist->fBtcWrite1ByteBitMask = halbtcoutsrc_BitMaskWrite1Byte;
	pBtCoexist->fBtcRead2Byte = halbtcoutsrc_Read2Byte;
	pBtCoexist->fBtcWrite2Byte = halbtcoutsrc_Write2Byte;
	pBtCoexist->fBtcRead4Byte = halbtcoutsrc_Read4Byte;
	pBtCoexist->fBtcWrite4Byte = halbtcoutsrc_Write4Byte;
	pBtCoexist->fBtcWriteLocalReg1Byte = halbtcoutsrc_WriteLocalReg1Byte;

	pBtCoexist->fBtcSetBbReg = halbtcoutsrc_SetBbReg;
	pBtCoexist->fBtcGetBbReg = halbtcoutsrc_GetBbReg;

	pBtCoexist->fBtcSetRfReg = halbtcoutsrc_SetRfReg;
	pBtCoexist->fBtcGetRfReg = halbtcoutsrc_GetRfReg;

	pBtCoexist->fBtcFillH2c = halbtcoutsrc_FillH2cCmd;

	pBtCoexist->fBtcGet = halbtcoutsrc_Get;
	pBtCoexist->fBtcSet = halbtcoutsrc_Set;
	pBtCoexist->fBtcGetBtReg = halbtcoutsrc_GetBtReg;
	pBtCoexist->fBtcSetBtReg = halbtcoutsrc_SetBtReg;

	pBtCoexist->boardInfo.singleAntPath = 0;

	GLBtcWiFiInScanState = false;

	GLBtcWiFiInIQKState = false;
}

void EXhalbtcoutsrc_PowerOnSetting(struct btc_coexist *pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	/* Power on setting function is only added in 8723B currently */
	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_PowerOnSetting(pBtCoexist);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_PowerOnSetting(pBtCoexist);
}

void EXhalbtcoutsrc_InitHwConfig(struct btc_coexist *pBtCoexist, u8 bWifiOnly)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntInitHwConfig++;

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_InitHwConfig(pBtCoexist, bWifiOnly);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_InitHwConfig(pBtCoexist, bWifiOnly);
}

void EXhalbtcoutsrc_InitCoexDm(struct btc_coexist *pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntInitCoexDm++;

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_InitCoexDm(pBtCoexist);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_InitCoexDm(pBtCoexist);

	pBtCoexist->bInitilized = true;
}

void EXhalbtcoutsrc_IpsNotify(struct btc_coexist *pBtCoexist, u8 type)
{
	u8 ipsType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntIpsNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (IPS_NONE == type)
		ipsType = BTC_IPS_LEAVE;
	else
		ipsType = BTC_IPS_ENTER;

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_IpsNotify(pBtCoexist, ipsType);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_IpsNotify(pBtCoexist, ipsType);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_LpsNotify(struct btc_coexist *pBtCoexist, u8 type)
{
	u8 lpsType;


	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntLpsNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (PS_MODE_ACTIVE == type)
		lpsType = BTC_LPS_DISABLE;
	else
		lpsType = BTC_LPS_ENABLE;

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_LpsNotify(pBtCoexist, lpsType);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_LpsNotify(pBtCoexist, lpsType);
}

void EXhalbtcoutsrc_ScanNotify(struct btc_coexist *pBtCoexist, u8 type)
{
	u8 scanType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntScanNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (type) {
		scanType = BTC_SCAN_START;
		GLBtcWiFiInScanState = true;
	} else {
		scanType = BTC_SCAN_FINISH;
		GLBtcWiFiInScanState = false;
	}

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_ScanNotify(pBtCoexist, scanType);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_ScanNotify(pBtCoexist, scanType);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_ConnectNotify(struct btc_coexist *pBtCoexist, u8 action)
{
	u8 assoType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntConnectNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (action)
		assoType = BTC_ASSOCIATE_START;
	else
		assoType = BTC_ASSOCIATE_FINISH;

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_ConnectNotify(pBtCoexist, assoType);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_ConnectNotify(pBtCoexist, assoType);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_MediaStatusNotify(struct btc_coexist *pBtCoexist, enum
	rt_media_status mediaStatus)
{
	u8 mStatus;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntMediaStatusNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (RT_MEDIA_CONNECT == mediaStatus)
		mStatus = BTC_MEDIA_CONNECT;
	else
		mStatus = BTC_MEDIA_DISCONNECT;

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, mStatus);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_MediaStatusNotify(pBtCoexist, mStatus);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_SpecialPacketNotify(struct btc_coexist *pBtCoexist, u8 pktType)
{
	u8 packetType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntSpecialPacketNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (PACKET_DHCP == pktType) {
		packetType = BTC_PACKET_DHCP;
	} else if (PACKET_EAPOL == pktType) {
		packetType = BTC_PACKET_EAPOL;
	} else if (PACKET_ARP == pktType) {
		packetType = BTC_PACKET_ARP;
	} else {
		return;
	}

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_SpecialPacketNotify(pBtCoexist, packetType);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_SpecialPacketNotify(pBtCoexist, packetType);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_BtInfoNotify(struct btc_coexist *pBtCoexist, u8 *tmpBuf, u8 length)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntBtInfoNotify++;

	/*  All notify is called in cmd thread, don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_BtInfoNotify(pBtCoexist, tmpBuf, length);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_HaltNotify(struct btc_coexist *pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_HaltNotify(pBtCoexist);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_HaltNotify(pBtCoexist);

	pBtCoexist->bBinded = false;
}

void EXhalbtcoutsrc_PnpNotify(struct btc_coexist *pBtCoexist, u8 pnpState)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	/*  */
	/*  currently only 1ant we have to do the notification, */
	/*  once pnp is notified to sleep state, we have to leave LPS that we can sleep normally. */
	/*  */

	if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_PnpNotify(pBtCoexist, pnpState);
	else if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_PnpNotify(pBtCoexist, pnpState);
}

void EXhalbtcoutsrc_Periodical(struct btc_coexist *pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntPeriodical++;

	/*  Periodical should be called in cmd thread, */
	/*  don't need to leave low power again */
/* 	halbtcoutsrc_LeaveLowPower(pBtCoexist); */

	if (pBtCoexist->boardInfo.btdmAntNum == 2)
		EXhalbtc8723b2ant_Periodical(pBtCoexist);
	else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		EXhalbtc8723b1ant_Periodical(pBtCoexist);

/* 	halbtcoutsrc_NormalLowPower(pBtCoexist); */
}

void EXhalbtcoutsrc_SetAntNum(u8 type, u8 antNum)
{
	if (BT_COEX_ANT_TYPE_PG == type) {
		GLBtCoexist.boardInfo.pgAntNum = antNum;
		GLBtCoexist.boardInfo.btdmAntNum = antNum;
	} else if (BT_COEX_ANT_TYPE_ANTDIV == type) {
		GLBtCoexist.boardInfo.btdmAntNum = antNum;
		/* GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT; */
	} else if (BT_COEX_ANT_TYPE_DETECTED == type) {
		GLBtCoexist.boardInfo.btdmAntNum = antNum;
		/* GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT; */
	}
}

/*  */
/*  Currently used by 8723b only, S0 or S1 */
/*  */
void EXhalbtcoutsrc_SetSingleAntPath(u8 singleAntPath)
{
	GLBtCoexist.boardInfo.singleAntPath = singleAntPath;
}

/*
 * Description:
 *Run BT-Coexist mechanism or not
 *
 */
void hal_btcoex_SetBTCoexist(struct adapter *padapter, u8 bBtExist)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->bt_coexist.bBtExist = bBtExist;
}

/*
 * Dewcription:
 *Check is co-exist mechanism enabled or not
 *
 * Return:
 *true	Enable BT co-exist mechanism
 *false	Disable BT co-exist mechanism
 */
bool hal_btcoex_IsBtExist(struct adapter *padapter)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);
	return pHalData->bt_coexist.bBtExist;
}

bool hal_btcoex_IsBtDisabled(struct adapter *padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return true;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return true;
	else
		return false;
}

void hal_btcoex_SetPgAntNum(struct adapter *padapter, u8 antNum)
{
	struct hal_com_data *pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.btTotalAntNum = antNum;
	EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_PG, antNum);
}

void hal_btcoex_SetSingleAntPath(struct adapter *padapter, u8 singleAntPath)
{
	EXhalbtcoutsrc_SetSingleAntPath(singleAntPath);
}

void hal_btcoex_PowerOnSetting(struct adapter *padapter)
{
	EXhalbtcoutsrc_PowerOnSetting(&GLBtCoexist);
}

void hal_btcoex_InitHwConfig(struct adapter *padapter, u8 bWifiOnly)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return;

	EXhalbtcoutsrc_InitHwConfig(&GLBtCoexist, bWifiOnly);
	EXhalbtcoutsrc_InitCoexDm(&GLBtCoexist);
}

void hal_btcoex_IpsNotify(struct adapter *padapter, u8 type)
{
	EXhalbtcoutsrc_IpsNotify(&GLBtCoexist, type);
}

void hal_btcoex_LpsNotify(struct adapter *padapter, u8 type)
{
	EXhalbtcoutsrc_LpsNotify(&GLBtCoexist, type);
}

void hal_btcoex_ScanNotify(struct adapter *padapter, u8 type)
{
	EXhalbtcoutsrc_ScanNotify(&GLBtCoexist, type);
}

void hal_btcoex_ConnectNotify(struct adapter *padapter, u8 action)
{
	EXhalbtcoutsrc_ConnectNotify(&GLBtCoexist, action);
}

void hal_btcoex_MediaStatusNotify(struct adapter *padapter, u8 mediaStatus)
{
	EXhalbtcoutsrc_MediaStatusNotify(&GLBtCoexist, mediaStatus);
}

void hal_btcoex_SpecialPacketNotify(struct adapter *padapter, u8 pktType)
{
	EXhalbtcoutsrc_SpecialPacketNotify(&GLBtCoexist, pktType);
}

void hal_btcoex_IQKNotify(struct adapter *padapter, u8 state)
{
	GLBtcWiFiInIQKState = state;
}

void hal_btcoex_BtInfoNotify(struct adapter *padapter, u8 length, u8 *tmpBuf)
{
	if (GLBtcWiFiInIQKState)
		return;

	EXhalbtcoutsrc_BtInfoNotify(&GLBtCoexist, tmpBuf, length);
}

void hal_btcoex_SuspendNotify(struct adapter *padapter, u8 state)
{
	if (state == 1)
		state = BTC_WIFI_PNP_SLEEP;
	else
		state = BTC_WIFI_PNP_WAKE_UP;

	EXhalbtcoutsrc_PnpNotify(&GLBtCoexist, state);
}

void hal_btcoex_HaltNotify(struct adapter *padapter)
{
	EXhalbtcoutsrc_HaltNotify(&GLBtCoexist);
}

void hal_btcoex_Handler(struct adapter *padapter)
{
	EXhalbtcoutsrc_Periodical(&GLBtCoexist);
}

s32 hal_btcoex_IsBTCoexCtrlAMPDUSize(struct adapter *padapter)
{
	return (s32)GLBtCoexist.btInfo.bBtCtrlAggBufSize;
}

bool hal_btcoex_IsBtControlLps(struct adapter *padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return false;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return false;

	if (GLBtCoexist.btInfo.bBtCtrlLps)
		return true;

	return false;
}

bool hal_btcoex_IsLpsOn(struct adapter *padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return false;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return false;

	if (GLBtCoexist.btInfo.bBtLpsOn)
		return true;

	return false;
}

u8 hal_btcoex_RpwmVal(struct adapter *padapter)
{
	return GLBtCoexist.btInfo.rpwmVal;
}

u8 hal_btcoex_LpsVal(struct adapter *padapter)
{
	return GLBtCoexist.btInfo.lpsVal;
}

u32 hal_btcoex_GetRaMask(struct adapter *padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return 0;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return 0;

	if (GLBtCoexist.boardInfo.btdmAntNum != 1)
		return 0;

	return GLBtCoexist.btInfo.raMask;
}

void hal_btcoex_RecordPwrMode(struct adapter *padapter, u8 *pCmdBuf, u8 cmdLen)
{
	memcpy(GLBtCoexist.pwrModeVal, pCmdBuf, cmdLen);
}
