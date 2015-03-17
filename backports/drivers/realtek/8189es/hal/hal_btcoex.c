/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
#define __HAL_BTCOEX_C__

#ifdef CONFIG_BT_COEXIST

#include <hal_data.h>
#include <hal_btcoex.h>
#include <Mp_Precomp.h>

//====================================
//		Global variables
//====================================
const char *const BtProfileString[] =
{
	"NONE",
	"A2DP",
	"PAN",
	"HID",
	"SCO",
};

const char *const BtSpecString[] =
{
	"1.0b",
	"1.1",
	"1.2",
	"2.0+EDR",
	"2.1+EDR",
	"3.0+HS",
	"4.0",
};

const char *const BtLinkRoleString[] =
{
	"Master",
	"Slave",
};

const char *const h2cStaString[] =
{
	"successful",
	"h2c busy",
	"rf off",
	"fw not read",
};

const char *const ioStaString[] =
{
	"IO_STATUS_SUCCESS",
	"IO_STATUS_FAIL_CANNOT_IO",
	"IO_STATUS_FAIL_RF_OFF",
	"IO_STATUS_FAIL_FW_READ_CLEAR_TIMEOUT",
	"IO_STATUS_FAIL_WAIT_IO_EVENT_TIMEOUT",
	"IO_STATUS_INVALID_LEN",
	"IO_STATUS_IO_IDLE_QUEUE_EMPTY",
	"IO_STATUS_IO_INSERT_WAIT_QUEUE_FAIL",
	"IO_STATUS_UNKNOWN_FAIL",
	"IO_STATUS_WRONG_LEVEL",
	"IO_STATUS_H2C_STOPPED",
};

BTC_COEXIST GLBtCoexist;
u8 GLBtcWiFiInScanState;
u8 GLBtcWiFiInIQKState;

u32 GLBtcDbgType[BTC_MSG_MAX];
u8 GLBtcDbgBuf[BT_TMP_BUF_SIZE];

typedef struct _btcoexdbginfo
{
	u8 *info;
	u32 size; // buffer total size
	u32 len; // now used length
} BTCDBGINFO, *PBTCDBGINFO;

BTCDBGINFO GLBtcDbgInfo;

#define	BT_Operation(Adapter)						_FALSE

static void DBG_BT_INFO_INIT(PBTCDBGINFO pinfo, u8 *pbuf, u32 size)
{
	if (NULL == pinfo) return;

	_rtw_memset(pinfo, 0, sizeof(BTCDBGINFO));

	if (pbuf && size) {
		pinfo->info = pbuf;
		pinfo->size = size;
	}
}

void DBG_BT_INFO(u8 *dbgmsg)
{
	PBTCDBGINFO pinfo;
	u32 msglen, buflen;
	u8 *pbuf;


	pinfo = &GLBtcDbgInfo;

	if (NULL == pinfo->info)
		return;

	msglen = strlen(dbgmsg);
	if (pinfo->len + msglen > pinfo->size)
		return;

	pbuf = pinfo->info + pinfo->len;
	_rtw_memcpy(pbuf, dbgmsg, msglen);
	pinfo->len += msglen;
}

//====================================
//		Debug related function
//====================================
static u8 halbtcoutsrc_IsBtCoexistAvailable(PBTC_COEXIST pBtCoexist)
{
	if (!pBtCoexist->bBinded ||
		NULL == pBtCoexist->Adapter)
	{
		return _FALSE;
	}
	return _TRUE;
}

static void halbtcoutsrc_DbgInit(void)
{
	u8	i;

	for (i=0; i<BTC_MSG_MAX; i++)
		GLBtcDbgType[i] = 0;

	GLBtcDbgType[BTC_MSG_INTERFACE]			= 	\
//			INTF_INIT								|
//			INTF_NOTIFY							|
			0;

	GLBtcDbgType[BTC_MSG_ALGORITHM]			= 	\
//			ALGO_BT_RSSI_STATE					|
//			ALGO_WIFI_RSSI_STATE					|
//			ALGO_BT_MONITOR						|
//			ALGO_TRACE							|
//			ALGO_TRACE_FW						|
//			ALGO_TRACE_FW_DETAIL				|
//			ALGO_TRACE_FW_EXEC					|
//			ALGO_TRACE_SW						|
//			ALGO_TRACE_SW_DETAIL				|
//			ALGO_TRACE_SW_EXEC					|
			0;
}

static u8 halbtcoutsrc_IsHwMailboxExist(PBTC_COEXIST pBtCoexist)
{
	if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		return _FALSE;
	}
	else
		return _TRUE;
}

static void halbtcoutsrc_LeaveLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;


	padapter = pBtCoexist->Adapter;

	pBtCoexist->btInfo.bBtCtrlLps = _TRUE;
	pBtCoexist->btInfo.bBtLpsOn = _FALSE;

	rtw_btcoex_LPS_Leave(padapter);
}

void halbtcoutsrc_EnterLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;


	padapter = pBtCoexist->Adapter;

	pBtCoexist->btInfo.bBtCtrlLps = _TRUE;
	pBtCoexist->btInfo.bBtLpsOn = _TRUE;

	rtw_btcoex_LPS_Enter(padapter);
}

void halbtcoutsrc_NormalLps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;


	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Normal LPS behavior!!!\n"));

	padapter = pBtCoexist->Adapter;

	if (pBtCoexist->btInfo.bBtCtrlLps)
	{
		pBtCoexist->btInfo.bBtLpsOn = _FALSE;
		rtw_btcoex_LPS_Leave(padapter);
		pBtCoexist->btInfo.bBtCtrlLps = _FALSE;

		// recover the LPS state to the original
#if 0
		padapter->HalFunc.UpdateLPSStatusHandler(
			padapter,
			pPSC->RegLeisurePsMode,
			pPSC->RegPowerSaveMode);
#endif
	}
}

/*
 *  Constraint:
 *	   1. this function will request pwrctrl->lock
 */
void halbtcoutsrc_LeaveLowPower(PBTC_COEXIST pBtCoexist)
{
#ifdef CONFIG_LPS_LCLK
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrl;
	s32 ready;
	u32 stime;
	s32 utime;
	u32 timeout; // unit: ms


	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	pwrctrl = adapter_to_pwrctl(padapter);
	ready = _FAIL;
#ifdef LPS_RPWM_WAIT_MS
	timeout = LPS_RPWM_WAIT_MS;
#else // !LPS_RPWM_WAIT_MS
	timeout = 30;
#endif // !LPS_RPWM_WAIT_MS

	stime = rtw_get_current_time();
	do {
		ready = rtw_register_task_alive(padapter, BTCOEX_ALIVE);
		if (_SUCCESS == ready)
			break;

		utime = rtw_get_passing_time_ms(stime);
		if (utime > timeout)
			break;

		rtw_msleep_os(1);
	} while (1);
#endif // CONFIG_LPS_LCLK
}

/*
 *  Constraint:
 *	   1. this function will request pwrctrl->lock
 */
void halbtcoutsrc_NormalLowPower(PBTC_COEXIST pBtCoexist)
{
#ifdef CONFIG_LPS_LCLK
	PADAPTER padapter;


	padapter = pBtCoexist->Adapter;
	rtw_unregister_task_alive(padapter, BTCOEX_ALIVE);
#endif // CONFIG_LPS_LCLK
}

void halbtcoutsrc_DisableLowPower(PBTC_COEXIST pBtCoexist, u8 bLowPwrDisable)
{
	pBtCoexist->btInfo.bBtDisableLowPwr = bLowPwrDisable;
	if (bLowPwrDisable)
		halbtcoutsrc_LeaveLowPower(pBtCoexist);		// leave 32k low power.
	else
		halbtcoutsrc_NormalLowPower(pBtCoexist);	// original 32k low power behavior.
}

void halbtcoutsrc_AggregationCheck(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;
	BOOLEAN bNeedToAct;


	padapter = pBtCoexist->Adapter;
	bNeedToAct = _FALSE;

	if (pBtCoexist->btInfo.bRejectAggPkt)
		rtw_btcoex_RejectApAggregatedPacket(padapter, _TRUE);
	else
	{
		if (pBtCoexist->btInfo.bPreBtCtrlAggBufSize !=
			pBtCoexist->btInfo.bBtCtrlAggBufSize)
		{
			bNeedToAct = _TRUE;
			pBtCoexist->btInfo.bPreBtCtrlAggBufSize = pBtCoexist->btInfo.bBtCtrlAggBufSize;
		}

		if (pBtCoexist->btInfo.bBtCtrlAggBufSize)
		{
			if (pBtCoexist->btInfo.preAggBufSize !=
				pBtCoexist->btInfo.aggBufSize)
			{
				bNeedToAct = _TRUE;
			}
			pBtCoexist->btInfo.preAggBufSize = pBtCoexist->btInfo.aggBufSize;
		}

		if (bNeedToAct)
		{
			rtw_btcoex_RejectApAggregatedPacket(padapter, _TRUE);
			rtw_btcoex_RejectApAggregatedPacket(padapter, _FALSE);
		}
	}
}

u8 halbtcoutsrc_IsWifiBusy(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;


	pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
	{
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			return _TRUE;
		if (_TRUE == pmlmepriv->LinkDetectInfo.bBusyTraffic)
			return _TRUE;
	}

#if defined(CONFIG_CONCURRENT_MODE) || defined(CONFIG_DUALMAC_CONCURRENT)
	pmlmepriv = &padapter->pbuddy_adapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
	{
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
			return _TRUE;
		if (_TRUE == pmlmepriv->LinkDetectInfo.bBusyTraffic)
			return _TRUE;
	}
#endif

	return _FALSE;
}

static u32 _halbtcoutsrc_GetWifiLinkStatus(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	u8 bp2p;
	u32 portConnectedStatus;


	pmlmepriv = &padapter->mlmepriv;
	bp2p = _FALSE;
	portConnectedStatus = 0;

#ifdef CONFIG_P2P
	if (!rtw_p2p_chk_state(&padapter->wdinfo, P2P_STATE_NONE))
		bp2p = _TRUE;
#endif // CONFIG_P2P

	if (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
	{
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
		{
			if (_TRUE == bp2p)
				portConnectedStatus |= WIFI_P2P_GO_CONNECTED;
			else
				portConnectedStatus |= WIFI_AP_CONNECTED;
		}
		else
		{
			if (_TRUE == bp2p)
				portConnectedStatus |= WIFI_P2P_GC_CONNECTED;
			else
				portConnectedStatus |= WIFI_STA_CONNECTED;
		}
	}

	return portConnectedStatus;
}

u32 halbtcoutsrc_GetWifiLinkStatus(PBTC_COEXIST pBtCoexist)
{
	//=================================
	// return value:
	// [31:16]=> connected port number
	// [15:0]=> port connected bit define
	//================================

	PADAPTER padapter;
	u32 retVal;
	u32 portConnectedStatus, numOfConnectedPort;


	padapter = pBtCoexist->Adapter;
	retVal = 0;
	portConnectedStatus = 0;
	numOfConnectedPort = 0;

	retVal = _halbtcoutsrc_GetWifiLinkStatus(padapter);
	if (retVal)
	{
		portConnectedStatus |= retVal;
		numOfConnectedPort++;
	}

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->pbuddy_adapter)
	{
		retVal = _halbtcoutsrc_GetWifiLinkStatus(padapter->pbuddy_adapter);
		if (retVal)
		{
			portConnectedStatus |= retVal;
			numOfConnectedPort++;
		}
	}
#endif // CONFIG_CONCURRENT_MODE

	retVal = (numOfConnectedPort << 16) | portConnectedStatus;
	
	return retVal;
}

u32 halbtcoutsrc_GetBtPatchVer(PBTC_COEXIST pBtCoexist)
{
	u16 btRealFwVer = 0x0;
	u8 btFwVer = 0x0;
	u8 cnt = 0;

#if 0
	if (!pBtCoexist->btInfo.btRealFwVer && cnt<=5)
	{
		if (halbtcoutsrc_IsHwMailboxExist(pBtCoexist))
		{	// mailbox exists, through mailbox
			if (NDBG_GetBtFwVersion(pBtCoexist->Adapter, &btRealFwVer, &btFwVer))
			{
				pBtCoexist->btInfo.btRealFwVer = btRealFwVer;
				pBtCoexist->btInfo.btFwVer = btFwVer;
			}
			else
			{
				pBtCoexist->btInfo.btRealFwVer = 0x0;
				pBtCoexist->btInfo.btFwVer = 0x0;
			}
		}
		else	// no mailbox, query bt patch version through stack.
		{
			u1Byte	dataLen=2;
			u1Byte	buf[4] = {0};
			buf[0] = 0x0;	// OP_Code
			buf[1] = 0x0;	// OP_Code_Length
			BT_SendEventExtBtCoexControl(pBtCoexist->Adapter, _FALSE, dataLen, &buf[0]);
		}
		cnt++;
	}
#endif
	return pBtCoexist->btInfo.btRealFwVer;
}

s32 halbtcoutsrc_GetWifiRssi(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	s32 UndecoratedSmoothedPWDB = 0;


	pHalData = GET_HAL_DATA(padapter);

	UndecoratedSmoothedPWDB = pHalData->dmpriv.EntryMinUndecoratedSmoothedPWDB;

	return UndecoratedSmoothedPWDB;
}

static u8 halbtcoutsrc_GetWifiScanAPNum(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	struct mlme_ext_priv *pmlmeext;
	static u8 scan_AP_num = 0;


	pmlmepriv = &padapter->mlmepriv;
	pmlmeext = &padapter->mlmeextpriv;

	if (check_fwstate(pmlmepriv, WIFI_SITE_MONITOR) == _FALSE) {
		if (pmlmeext->sitesurvey_res.bss_cnt > 0xFF)
			scan_AP_num = 0xFF;
		else
			scan_AP_num = (u8)pmlmeext->sitesurvey_res.bss_cnt;
	}

	return scan_AP_num;
}

u8 halbtcoutsrc_Get(void *pBtcContext, u8 getType, void *pOutBuf)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	struct mlme_ext_priv *mlmeext;
	u8 bSoftApExist, bVwifiExist;
	u8 *pu8;
	s32 *pS4Tmp;
	u32 *pU4Tmp;
	u8 *pU1Tmp;
	u8 ret;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return _FALSE;

	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	mlmeext = &padapter->mlmeextpriv;
	bSoftApExist = _FALSE;
	bVwifiExist = _FALSE;
	pu8 = (u8*)pOutBuf;
	pS4Tmp = (s32*)pOutBuf;
	pU4Tmp = (u32*)pOutBuf;
	pU1Tmp = (u8*)pOutBuf;
	ret = _TRUE;

	switch (getType)
	{
		case BTC_GET_BL_HS_OPERATION:
			*pu8 = _FALSE;
			ret = _FALSE;
			break;

		case BTC_GET_BL_HS_CONNECTING:
			*pu8 = _FALSE;
			ret = _FALSE;
			break;

		case BTC_GET_BL_WIFI_CONNECTED:
			*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE);
#ifdef CONFIG_CONCURRENT_MODE
			if ((_FALSE == *pu8) && padapter->pbuddy_adapter)
			{
				*pu8 = check_fwstate(&padapter->pbuddy_adapter->mlmepriv, WIFI_ASOC_STATE);
			}
#endif // CONFIG_CONCURRENT_MODE
			break;

		case BTC_GET_BL_WIFI_BUSY:
			*pu8 = halbtcoutsrc_IsWifiBusy(padapter);
			break;

		case BTC_GET_BL_WIFI_SCAN:
#if 0			
			*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_SITE_MONITOR);
#ifdef CONFIG_CONCURRENT_MODE
			if ((_FALSE == *pu8) && padapter->pbuddy_adapter)
			{
				*pu8 = check_fwstate(&padapter->pbuddy_adapter->mlmepriv, WIFI_SITE_MONITOR);
			}
#endif // CONFIG_CONCURRENT_MODE
#else		
			/* Use the value of the new variable GLBtcWiFiInScanState to judge whether WiFi is in scan state or not, since the originally used flag 
				WIFI_SITE_MONITOR in fwstate may not be cleared in time */
			*pu8 = GLBtcWiFiInScanState;
#endif
			break;

		case BTC_GET_BL_WIFI_LINK:
			*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING);
#ifdef CONFIG_CONCURRENT_MODE
			if ((_FALSE == *pu8) && padapter->pbuddy_adapter)
			{
				*pu8 = check_fwstate(&padapter->pbuddy_adapter->mlmepriv, WIFI_UNDER_LINKING);
			}
#endif // CONFIG_CONCURRENT_MODE
			break;

		case BTC_GET_BL_WIFI_ROAM:
			*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_UNDER_LINKING);
#ifdef CONFIG_CONCURRENT_MODE
			if ((_FALSE == *pu8) && padapter->pbuddy_adapter)
			{
				*pu8 = check_fwstate(&padapter->pbuddy_adapter->mlmepriv, WIFI_UNDER_LINKING);
			}
#endif // CONFIG_CONCURRENT_MODE
			break;

		case BTC_GET_BL_WIFI_4_WAY_PROGRESS:
			*pu8 = _FALSE;
			break;

		case BTC_GET_BL_WIFI_UNDER_5G:
			*pu8 = (pHalData->CurrentBandType == 1)? _TRUE : _FALSE;
			break;

		case BTC_GET_BL_WIFI_AP_MODE_ENABLE:
			*pu8 = check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE);
#ifdef CONFIG_CONCURRENT_MODE
			if ((_FALSE == *pu8) && padapter->pbuddy_adapter)
			{
				*pu8 = check_fwstate(&padapter->pbuddy_adapter->mlmepriv, WIFI_AP_STATE);
			}
#endif // CONFIG_CONCURRENT_MODE
			break;

		case BTC_GET_BL_WIFI_ENABLE_ENCRYPTION:
			*pu8 = padapter->securitypriv.dot11PrivacyAlgrthm == 0? _FALSE: _TRUE;
			break;

		case BTC_GET_BL_WIFI_UNDER_B_MODE:
			if (mlmeext->cur_wireless_mode == WIRELESS_11B)
				*pu8 = _TRUE;
			else
				*pu8 = _FALSE;
			break;

		case BTC_GET_BL_EXT_SWITCH:
			*pu8 = _FALSE;
			break;

		case BTC_GET_S4_WIFI_RSSI:
			*pS4Tmp = halbtcoutsrc_GetWifiRssi(padapter);
			break;

		case BTC_GET_S4_HS_RSSI:
			*pS4Tmp = 0;
			ret = _FALSE;
			break;

		case BTC_GET_U4_WIFI_BW:
			if (IsLegacyOnly(mlmeext->cur_wireless_mode))
				*pU4Tmp = BTC_WIFI_BW_LEGACY;
			else if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_20)
				*pU4Tmp = BTC_WIFI_BW_HT20;
			else if (pHalData->CurrentChannelBW == CHANNEL_WIDTH_40)
				*pU4Tmp = BTC_WIFI_BW_HT40;
			else
				*pU4Tmp = BTC_WIFI_BW_HT40; /* todo */
			break;

		case BTC_GET_U4_WIFI_TRAFFIC_DIRECTION:
			{
				PRT_LINK_DETECT_T plinkinfo;
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
			*pU1Tmp = padapter->mlmeextpriv.cur_channel;
			break;

		case BTC_GET_U1_WIFI_CENTRAL_CHNL:
			*pU1Tmp = pHalData->CurrentChannel;
			break;

		case BTC_GET_U1_WIFI_HS_CHNL:
			*pU1Tmp = 0;
			ret = _FALSE;
			break;

		case BTC_GET_U1_MAC_PHY_MODE:
			*pU1Tmp = BTC_SMSP;
//			*pU1Tmp = BTC_DMSP;
//			*pU1Tmp = BTC_DMDP;
//			*pU1Tmp = BTC_MP_UNKNOWN;
			break;

		case BTC_GET_U1_AP_NUM:
			*pU1Tmp = halbtcoutsrc_GetWifiScanAPNum(padapter);
			break;

		//=======1Ant===========
		case BTC_GET_U1_LPS_MODE:
			*pU1Tmp = padapter->dvobj->pwrctl_priv.pwr_mode;
			break;

		default:
			ret = _FALSE;
			break;
	}

	return ret;
}

u8 halbtcoutsrc_Set(void *pBtcContext, u8 setType, void *pInBuf)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	PHAL_DATA_TYPE pHalData;
	u8 *pu8;
	u8 *pU1Tmp;
	u32	*pU4Tmp;
	u8 ret;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;
	pHalData = GET_HAL_DATA(padapter);
	pu8 = (u8*)pInBuf;
	pU1Tmp = (u8*)pInBuf;
	pU4Tmp = (u32*)pInBuf;
	ret = _TRUE;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return _FALSE;

	switch (setType)
	{
		// set some u8 type variables.
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

		// set some u8 type variables.
		case BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON:
			pBtCoexist->btInfo.rssiAdjustForAgcTableOn = *pU1Tmp;
			break;

		case BTC_SET_U1_AGG_BUF_SIZE:
			pBtCoexist->btInfo.aggBufSize = *pU1Tmp;
			break;

		// the following are some action which will be triggered
		case BTC_SET_ACT_GET_BT_RSSI:
#if 0
			BT_SendGetBtRssiEvent(padapter);
#else
			ret = _FALSE;
#endif
			break;

		case BTC_SET_ACT_AGGREGATE_CTRL:
			halbtcoutsrc_AggregationCheck(pBtCoexist);
			break;

		//=======1Ant===========
		// set some u8 type variables.
		case BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE:
			pBtCoexist->btInfo.rssiAdjustFor1AntCoexType = *pU1Tmp;
			break;

		case BTC_SET_U1_LPS_VAL:
			pBtCoexist->btInfo.lpsVal = *pU1Tmp;
			break;

		case BTC_SET_U1_RPWM_VAL:
			pBtCoexist->btInfo.rpwmVal = *pU1Tmp;
			break;

		// the following are some action which will be triggered
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

			if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
			{
				struct sta_info *psta;
				PWLAN_BSSID_EX cur_network;

				cur_network = &padapter->mlmeextpriv.mlmext_info.network;
				psta = rtw_get_stainfo(&padapter->stapriv, cur_network->MacAddress);
				rtw_hal_update_ra_mask(psta, 0);
			}
			break;

		case BTC_SET_ACT_SEND_MIMO_PS:
#if 0 // not implement yet
			{
				u8 newMimoPsMode = *pU1Tmp;
				if (check_fwstate(&padapter->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
					SendMimoPsFrame(padapter, padapter->MgntInfo.Bssid, newMimoPsMode);
			}
#else
			ret = _FALSE;
#endif
			break;

		case BTC_SET_ACT_CTRL_BT_INFO:
#if 0
			{
				u8 dataLen = *pU1Tmp;
				u8 tmpBuf[20];
				if (dataLen)
				{
					_rtw_memcpy(tmpBuf, pU1Tmp+1, dataLen);
				}
//				BT_SendEventExtBtInfoControl(padapter, dataLen, &tmpBuf[0]);
			}
#else
			ret = _FALSE;
#endif
			break;

		case BTC_SET_ACT_CTRL_BT_COEX:
#if 0
			{
				u8 dataLen = *pU1Tmp;
				u8 tmpBuf[20];
				if (dataLen)
				{
					_rtw_memcpy(tmpBuf, pU1Tmp+1, dataLen);
				}
//				BT_SendEventExtBtCoexControl(padapter, _FALSE, dataLen, &tmpBuf[0]);
			}
#else
			ret = _FALSE;
#endif
			break;

		//=====================
		default:
			ret = _FALSE;
			break;
	}

	return ret;
}

void halbtcoutsrc_DisplayCoexStatistics(PBTC_COEXIST pBtCoexist)
{
#if 0
	PADAPTER padapter = (PADAPTER)pBtCoexist->Adapter;
	PBT_MGNT pBtMgnt = &padapter->MgntInfo.BtInfo.BtMgnt;
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8 i;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Statistics]============");
	CL_PRINTF(cliBuf);

#if (H2C_USE_IO_THREAD != 1)
	for(i=0; i<H2C_STATUS_MAX; i++)
	{
		if (pHalData->h2cStatistics[i])
		{
			CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = [%s] = %d", "H2C statistics", \
				h2cStaString[i], pHalData->h2cStatistics[i]);
			CL_PRINTF(cliBuf);
		}
	}

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x", "lastHMEBoxNum", \
		pHalData->LastHMEBoxNum);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x / 0x%x", "LastSuccessFwEid/FirstfailedFwEid", \
		pHalData->lastSuccessH2cEid, pHalData->firstFailedH2cEid);
	CL_PRINTF(cliBuf);
#endif

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d", "c2hIsr/c2hIntr/clr1AF/noRdy/noBuf", \
		pHalData->InterruptLog.nIMR_C2HCMD, DBG_Var.c2hInterruptCnt, DBG_Var.c2hClrReadC2hCnt,
		DBG_Var.c2hNotReadyCnt, DBG_Var.c2hBufAlloFailCnt);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "c2hPacket", \
		DBG_Var.c2hPacketCnt);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "Periodical/ DbgCtrl", \
		pBtCoexist->statistics.cntPeriodical, pBtCoexist->statistics.cntDbgCtrl);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "InitHw/InitCoexDm/", \
		pBtCoexist->statistics.cntInitHwConfig, pBtCoexist->statistics.cntInitCoexDm);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d/ %d/ %d/ %d", "Ips/Lps/Scan/Connect/Mstatus", \
		pBtCoexist->statistics.cntIpsNotify, pBtCoexist->statistics.cntLpsNotify,
		pBtCoexist->statistics.cntScanNotify, pBtCoexist->statistics.cntConnectNotify,
		pBtCoexist->statistics.cntMediaStatusNotify);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d/ %d", "Special pkt/Bt info", \
		pBtCoexist->statistics.cntSpecialPacketNotify, pBtCoexist->statistics.cntBtInfoNotify);
	CL_PRINTF(cliBuf);
#endif
}

void halbtcoutsrc_DisplayBtLinkInfo(PBTC_COEXIST pBtCoexist)
{
#if 0
	PADAPTER padapter = (PADAPTER)pBtCoexist->Adapter;
	PBT_MGNT pBtMgnt = &padapter->MgntInfo.BtInfo.BtMgnt;
	u8 *cliBuf = pBtCoexist->cliBuf;
	u8 i;


	if (pBtCoexist->stackInfo.bProfileNotified)
	{
		for (i=0; i<pBtMgnt->ExtConfig.NumberOfACL; i++)
		{
			if (pBtMgnt->ExtConfig.HCIExtensionVer >= 1)
			{
				CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s/ %s", "Bt link type/spec/role", \
					BtProfileString[pBtMgnt->ExtConfig.aclLink[i].BTProfile],
					BtSpecString[pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec],
					BtLinkRoleString[pBtMgnt->ExtConfig.aclLink[i].linkRole]);
				CL_PRINTF(cliBuf);				}
			else
			{
				CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s/ %s", "Bt link type/spec", \
					BtProfileString[pBtMgnt->ExtConfig.aclLink[i].BTProfile],
					BtSpecString[pBtMgnt->ExtConfig.aclLink[i].BTCoreSpec]);
				CL_PRINTF(cliBuf);
			}
		}
	}
#endif
}

void halbtcoutsrc_DisplayFwPwrModeCmd(PBTC_COEXIST pBtCoexist)
{
	u8 *cliBuf = pBtCoexist->cliBuf;

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x %02x ", "Power mode cmd ", \
		pBtCoexist->pwrModeVal[0], pBtCoexist->pwrModeVal[1],
		pBtCoexist->pwrModeVal[2], pBtCoexist->pwrModeVal[3],
		pBtCoexist->pwrModeVal[4], pBtCoexist->pwrModeVal[5]);
	CL_PRINTF(cliBuf);
}

//====================================
//		IO related function
//====================================
u8 halbtcoutsrc_Read1Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return rtw_read8(padapter, RegAddr);
}

u16 halbtcoutsrc_Read2Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read16(padapter, RegAddr);
}

u32 halbtcoutsrc_Read4Byte(void *pBtcContext, u32 RegAddr)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return	rtw_read32(padapter, RegAddr);
}

void halbtcoutsrc_Write1Byte(void *pBtcContext, u32 RegAddr, u8 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write8(padapter, RegAddr, Data);
}

void halbtcoutsrc_BitMaskWrite1Byte(void *pBtcContext, u32 regAddr, u8 bitMask, u8 data1b)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	u8 originalValue, bitShift;
	u8 i;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;
	originalValue = 0;
	bitShift = 0;

	if (bitMask != 0xFF)
	{
		originalValue = rtw_read8(padapter, regAddr);

		for (i=0; i<=7; i++)
		{
			if ((bitMask>>i)&0x1)
				break;
		}
		bitShift = i;

		data1b = (originalValue & ~bitMask) | ((data1b << bitShift) & bitMask);
	}

	rtw_write8(padapter, regAddr, data1b);
}

void halbtcoutsrc_Write2Byte(void *pBtcContext, u32 RegAddr, u16 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write16(padapter, RegAddr, Data);
}

void halbtcoutsrc_Write4Byte(void *pBtcContext, u32 RegAddr, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_write32(padapter, RegAddr, Data);
}

void halbtcoutsrc_SetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	PHY_SetBBReg(padapter, RegAddr, BitMask, Data);
}


u32 halbtcoutsrc_GetBbReg(void *pBtcContext, u32 RegAddr, u32 BitMask)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return PHY_QueryBBReg(padapter, RegAddr, BitMask);
}

void halbtcoutsrc_SetRfReg(void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	PHY_SetRFReg(padapter, eRFPath, RegAddr, BitMask, Data);
}

u32 halbtcoutsrc_GetRfReg(void *pBtcContext, u8 eRFPath, u32 RegAddr, u32 BitMask)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	return PHY_QueryRFReg(padapter, eRFPath, RegAddr, BitMask);
}

void halbtcoutsrc_SetBtReg(void *pBtcContext, u8 RegType, u32 RegAddr, u32 Data)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;
	u8 CmdBuffer1[4] = {0};
	u8 CmdBuffer2[4] = {0};
	u8* AddrToSet = (u8*)&RegAddr;
	u8* ValueToSet = (u8*)&Data;	
	u8 OperVer = 0;
	u8 ReqNum = 0;

	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	if (IS_HARDWARE_TYPE_8723B(padapter))
	{
		CmdBuffer1[0] |= (OperVer & 0x0f);						/* Set OperVer */
		CmdBuffer1[0] |= ((ReqNum << 4) & 0xf0);				/* Set ReqNum */
		CmdBuffer1[1] = 0x0d; 									/* Set OpCode to BT_LO_OP_WRITE_REG_VALUE */
		CmdBuffer1[2] = ValueToSet[0]; 							/* Set WriteRegValue */
		rtw_hal_fill_h2c_cmd(padapter, 0x67, 4, &(CmdBuffer1[0]));

		rtw_msleep_os(200);
		ReqNum++;

		CmdBuffer2[0] |= (OperVer & 0x0f);						/* Set OperVer */
		CmdBuffer2[0] |= ((ReqNum << 4) & 0xf0);				/* Set ReqNum */
		CmdBuffer2[1] = 0x0c; 									/* Set OpCode of BT_LO_OP_WRITE_REG_ADDR */
		CmdBuffer2[3] = AddrToSet[0];							/* Set WriteRegAddr */
		rtw_hal_fill_h2c_cmd(padapter, 0x67, 4, &(CmdBuffer2[0]));
	}
}

void halbtcoutsrc_FillH2cCmd(void *pBtcContext, u8 elementId, u32 cmdLen, u8 *pCmdBuffer)
{
	PBTC_COEXIST pBtCoexist;
	PADAPTER padapter;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	padapter = pBtCoexist->Adapter;

	rtw_hal_fill_h2c_cmd(padapter, elementId, cmdLen, pCmdBuffer);
}

void halbtcoutsrc_DisplayDbgMsg(void *pBtcContext, u8 dispType)
{
	PBTC_COEXIST pBtCoexist;


	pBtCoexist = (PBTC_COEXIST)pBtcContext;
	switch(dispType)
	{
		case BTC_DBG_DISP_COEX_STATISTICS:
			halbtcoutsrc_DisplayCoexStatistics(pBtCoexist);
			break;
		case BTC_DBG_DISP_BT_LINK_INFO:
			halbtcoutsrc_DisplayBtLinkInfo(pBtCoexist);
			break;
		case BTC_DBG_DISP_FW_PWR_MODE_CMD:
			halbtcoutsrc_DisplayFwPwrModeCmd(pBtCoexist);
			break;
		default:
			break;
	}
}

u8 halbtcoutsrc_UnderIps(PBTC_COEXIST pBtCoexist)
{
	PADAPTER padapter;
	struct pwrctrl_priv *pwrpriv;
	u8 bMacPwrCtrlOn;

	padapter = pBtCoexist->Adapter;
	pwrpriv = &padapter->dvobj->pwrctl_priv;
	bMacPwrCtrlOn = _FALSE;

	if ((_TRUE == pwrpriv->bips_processing)
		&& (IPS_NONE != pwrpriv->ips_mode_req)
		)
	{
		return _TRUE;
	}

	if (rf_off == pwrpriv->rf_pwrstate)
	{
		return _TRUE;
	}

	rtw_hal_get_hwreg(padapter, HW_VAR_APFM_ON_MAC, &bMacPwrCtrlOn);
	if (_FALSE == bMacPwrCtrlOn)
	{
		return _TRUE;
	}

	return _FALSE;
}

//====================================
//		Extern functions called by other module
//====================================
u8 EXhalbtcoutsrc_InitlizeVariables(void *padapter)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;


	pBtCoexist->statistics.cntBind++;

	halbtcoutsrc_DbgInit();

	if (pBtCoexist->bBinded)
		return _FALSE;
	else
		pBtCoexist->bBinded = _TRUE;

#ifdef CONFIG_PCI_HCI
	pBtCoexist->chipInterface = BTC_INTF_PCI;
#elif defined(CONFIG_USB_HCI)
	pBtCoexist->chipInterface = BTC_INTF_USB;
#elif defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	pBtCoexist->chipInterface = BTC_INTF_SDIO;
#else
	pBtCoexist->chipInterface = BTC_INTF_UNKNOWN;
#endif

	if (NULL == pBtCoexist->Adapter)
	{
		pBtCoexist->Adapter = padapter;
	}

	pBtCoexist->stackInfo.bProfileNotified = _FALSE;

	pBtCoexist->fBtcRead1Byte = halbtcoutsrc_Read1Byte;
	pBtCoexist->fBtcWrite1Byte = halbtcoutsrc_Write1Byte;
	pBtCoexist->fBtcWrite1ByteBitMask = halbtcoutsrc_BitMaskWrite1Byte;
	pBtCoexist->fBtcRead2Byte = halbtcoutsrc_Read2Byte;
	pBtCoexist->fBtcWrite2Byte = halbtcoutsrc_Write2Byte;
	pBtCoexist->fBtcRead4Byte = halbtcoutsrc_Read4Byte;
	pBtCoexist->fBtcWrite4Byte = halbtcoutsrc_Write4Byte;

	pBtCoexist->fBtcSetBbReg = halbtcoutsrc_SetBbReg;
	pBtCoexist->fBtcGetBbReg = halbtcoutsrc_GetBbReg;

	pBtCoexist->fBtcSetRfReg = halbtcoutsrc_SetRfReg;
	pBtCoexist->fBtcGetRfReg = halbtcoutsrc_GetRfReg;

	pBtCoexist->fBtcSetBtReg = halbtcoutsrc_SetBtReg;

	pBtCoexist->fBtcFillH2c = halbtcoutsrc_FillH2cCmd;
	pBtCoexist->fBtcDispDbgMsg = halbtcoutsrc_DisplayDbgMsg;

	pBtCoexist->fBtcGet = halbtcoutsrc_Get;
	pBtCoexist->fBtcSet = halbtcoutsrc_Set;

	pBtCoexist->cliBuf = GLBtcDbgBuf;

	pBtCoexist->btInfo.bBtCtrlAggBufSize = _FALSE;
	pBtCoexist->btInfo.aggBufSize = 5;

	pBtCoexist->btInfo.bIncreaseScanDevNum = _FALSE;

	GLBtcWiFiInScanState = _FALSE;

	GLBtcWiFiInIQKState = _FALSE;

	return _TRUE;
}

void EXhalbtcoutsrc_InitHwConfig(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntInitHwConfig++;

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_InitHwConfig(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_InitHwConfig(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_InitHwConfig(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_InitHwConfig(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_InitHwConfig(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_InitHwConfig(pBtCoexist);
	}
}

void EXhalbtcoutsrc_InitCoexDm(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntInitCoexDm++;

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_InitCoexDm(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_InitCoexDm(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_InitCoexDm(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_InitCoexDm(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_InitCoexDm(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_InitCoexDm(pBtCoexist);
	}

	pBtCoexist->bInitilized = _TRUE;
}

void EXhalbtcoutsrc_IpsNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8	ipsType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntIpsNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (IPS_NONE == type)
		ipsType = BTC_IPS_LEAVE;
	else
		ipsType = BTC_IPS_ENTER;

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_IpsNotify(pBtCoexist, ipsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_IpsNotify(pBtCoexist, ipsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_IpsNotify(pBtCoexist, ipsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_IpsNotify(pBtCoexist, ipsType);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_IpsNotify(pBtCoexist, ipsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_IpsNotify(pBtCoexist, ipsType);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_LpsNotify(PBTC_COEXIST pBtCoexist, u8 type)
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

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_LpsNotify(pBtCoexist, lpsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_LpsNotify(pBtCoexist, lpsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_LpsNotify(pBtCoexist, lpsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_LpsNotify(pBtCoexist, lpsType);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_LpsNotify(pBtCoexist, lpsType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_LpsNotify(pBtCoexist, lpsType);
	}
}

void EXhalbtcoutsrc_ScanNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
	u8	scanType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntScanNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (type)
	{
		scanType = BTC_SCAN_START;
		GLBtcWiFiInScanState = _TRUE;
	}
	else
	{
		scanType = BTC_SCAN_FINISH;
		GLBtcWiFiInScanState = _FALSE;
	}

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_ScanNotify(pBtCoexist, scanType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_ScanNotify(pBtCoexist, scanType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_ScanNotify(pBtCoexist, scanType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_ScanNotify(pBtCoexist, scanType);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_ScanNotify(pBtCoexist, scanType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_ScanNotify(pBtCoexist, scanType);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_ConnectNotify(PBTC_COEXIST pBtCoexist, u8 action)
{
	u8	assoType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntConnectNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (action)
		assoType = BTC_ASSOCIATE_START;
	else
		assoType = BTC_ASSOCIATE_FINISH;

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_ConnectNotify(pBtCoexist, assoType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_ConnectNotify(pBtCoexist, assoType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_ConnectNotify(pBtCoexist, assoType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_ConnectNotify(pBtCoexist, assoType);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_ConnectNotify(pBtCoexist, assoType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_ConnectNotify(pBtCoexist, assoType);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_MediaStatusNotify(PBTC_COEXIST pBtCoexist, RT_MEDIA_STATUS mediaStatus)
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

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_MediaStatusNotify(pBtCoexist, mStatus);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_MediaStatusNotify(pBtCoexist, mStatus);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_MediaStatusNotify(pBtCoexist, mStatus);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_MediaStatusNotify(pBtCoexist, mStatus);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_MediaStatusNotify(pBtCoexist, mStatus);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_MediaStatusNotify(pBtCoexist, mStatus);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_SpecialPacketNotify(PBTC_COEXIST pBtCoexist, u8 pktType)
{
	u8	packetType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntSpecialPacketNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if (PACKET_DHCP == pktType)
		packetType = BTC_PACKET_DHCP;
	else if (PACKET_EAPOL == pktType)
		packetType = BTC_PACKET_EAPOL;
	else if (PACKET_ARP == pktType)
		packetType = BTC_PACKET_ARP;
	else
	{
		packetType = BTC_PACKET_UNKNOWN;
		return;
	}

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_SpecialPacketNotify(pBtCoexist, packetType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_SpecialPacketNotify(pBtCoexist, packetType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_SpecialPacketNotify(pBtCoexist, packetType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_SpecialPacketNotify(pBtCoexist, packetType);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_SpecialPacketNotify(pBtCoexist, packetType);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_SpecialPacketNotify(pBtCoexist, packetType);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_BtInfoNotify(PBTC_COEXIST pBtCoexist, u8 *tmpBuf, u8 length)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntBtInfoNotify++;

	// All notify is called in cmd thread, don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_BtInfoNotify(pBtCoexist, tmpBuf, length);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_StackOperationNotify(PBTC_COEXIST pBtCoexist, u8 type)
{
#if 0
	u8	stackOpType;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntStackOperationNotify++;
	if (pBtCoexist->bManualControl)
		return;

	if ((HCI_BT_OP_INQUIRY_START == type) ||
		(HCI_BT_OP_PAGING_START == type) ||
		(HCI_BT_OP_PAIRING_START == type))
	{
		stackOpType = BTC_STACK_OP_INQ_PAGE_PAIR_START;
	}
	else if ((HCI_BT_OP_INQUIRY_FINISH == type) ||
		(HCI_BT_OP_PAGING_SUCCESS == type) ||
		(HCI_BT_OP_PAGING_UNSUCCESS == type) ||
		(HCI_BT_OP_PAIRING_FINISH == type) )
	{
		stackOpType = BTC_STACK_OP_INQ_PAGE_PAIR_FINISH;
	}
	else
	{
		stackOpType = BTC_STACK_OP_NONE;
	}

	if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_StackOperationNotify(pBtCoexist, stackOpType);
	}
#endif
}

void EXhalbtcoutsrc_HaltNotify(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_HaltNotify(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_HaltNotify(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_HaltNotify(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723a1ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_HaltNotify(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_HaltNotify(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_HaltNotify(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_HaltNotify(pBtCoexist);
	}
}

void EXhalbtcoutsrc_SwitchGntBt(PBTC_COEXIST pBtCoexist)
{
	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
		{
			halbtcoutsrc_SetBtReg(pBtCoexist, 0, 0x3c, 0x01); //BT goto standby while GNT_BT 1-->0
		}
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		{
			halbtcoutsrc_SetBtReg(pBtCoexist, 0, 0x3c, 0x15); //BT goto standby while GNT_BT 1-->0
		}
	}
}

void EXhalbtcoutsrc_PnpNotify(PBTC_COEXIST pBtCoexist, u8 pnpState)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	//
	// currently only 1ant we have to do the notification,
	// once pnp is notified to sleep state, we have to leave LPS that we can sleep normally.
	//

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_PnpNotify(pBtCoexist,pnpState);
	}
	else if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_PnpNotify(pBtCoexist,pnpState);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_PnpNotify(pBtCoexist, pnpState);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_PnpNotify(pBtCoexist, pnpState);
	}
}

void EXhalbtcoutsrc_CoexDmSwitch(PBTC_COEXIST pBtCoexist, BOOLEAN antInverse)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntCoexDmSwitch++;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
		{
			pBtCoexist->bStopCoexDm = TRUE;
			EXhalbtc8723b1ant_CoexDmReset(pBtCoexist);
			EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_DETECTED, 2, antInverse);
			EXhalbtc8723b2ant_InitHwConfig(pBtCoexist);
			EXhalbtc8723b2ant_InitCoexDm(pBtCoexist);
			pBtCoexist->bStopCoexDm = FALSE;
		}
	}

	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_Periodical(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;
	pBtCoexist->statistics.cntPeriodical++;

	// Periodical should be called in cmd thread,
	// don't need to leave low power again
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_Periodical(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		{
			if (!halbtcoutsrc_UnderIps(pBtCoexist))
			{
				EXhalbtc8821a1ant_Periodical(pBtCoexist);
			}
		}
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_Periodical(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_Periodical(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_Periodical(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
		{
			if (!halbtcoutsrc_UnderIps(pBtCoexist))
				EXhalbtc8723a1ant_Periodical(pBtCoexist);
		}
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_Periodical(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_Periodical(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_Periodical(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_Periodical(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_Periodical(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_Periodical(pBtCoexist);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_DbgControl(PBTC_COEXIST pBtCoexist, u8 opCode, u8 opLen, u8 *pData)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->statistics.cntDbgCtrl++;

	// This function doesn't be called yet,
	// default no need to leave low power to avoid deadlock
//	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_DbgControl(pBtCoexist, opCode, opLen, pData);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_DbgControl(pBtCoexist, opCode, opLen, pData);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_DbgControl(pBtCoexist, opCode, opLen, pData);
	}

//	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

void EXhalbtcoutsrc_StackUpdateProfileInfo(void)
{
#if 0
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;
	PADAPTER padapter = (PADAPTER)GLBtCoexist.padapter;
	PBT_MGNT pBtMgnt = &padapter->MgntInfo.BtInfo.BtMgnt;
	u8 i;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->stackInfo.bProfileNotified = _TRUE;

	pBtCoexist->stackInfo.numOfLink =
		pBtMgnt->ExtConfig.NumberOfACL+pBtMgnt->ExtConfig.NumberOfSCO;

	// reset first
	pBtCoexist->stackInfo.bBtLinkExist = _FALSE;
	pBtCoexist->stackInfo.bScoExist = _FALSE;
	pBtCoexist->stackInfo.bAclExist = _FALSE;
	pBtCoexist->stackInfo.bA2dpExist = _FALSE;
	pBtCoexist->stackInfo.bHidExist = _FALSE;
	pBtCoexist->stackInfo.numOfHid = 0;
	pBtCoexist->stackInfo.bPanExist = _FALSE;

	if (!pBtMgnt->ExtConfig.NumberOfACL)
		pBtCoexist->stackInfo.minBtRssi = 0;

	if (pBtCoexist->stackInfo.numOfLink)
	{
		pBtCoexist->stackInfo.bBtLinkExist = _TRUE;
		if (pBtMgnt->ExtConfig.NumberOfSCO)
			pBtCoexist->stackInfo.bScoExist = _TRUE;
		if (pBtMgnt->ExtConfig.NumberOfACL)
			pBtCoexist->stackInfo.bAclExist = _TRUE;
	}

	for (i=0; i<pBtMgnt->ExtConfig.NumberOfACL; i++)
	{
		if (BT_PROFILE_A2DP == pBtMgnt->ExtConfig.aclLink[i].BTProfile)
		{
			pBtCoexist->stackInfo.bA2dpExist = _TRUE;
		}
		else if (BT_PROFILE_PAN == pBtMgnt->ExtConfig.aclLink[i].BTProfile)
		{
			pBtCoexist->stackInfo.bPanExist = _TRUE;
		}
		else if (BT_PROFILE_HID == pBtMgnt->ExtConfig.aclLink[i].BTProfile)
		{
			pBtCoexist->stackInfo.bHidExist = _TRUE;
			pBtCoexist->stackInfo.numOfHid++;
		}
		else
		{
			pBtCoexist->stackInfo.bUnknownAclExist = _TRUE;
		}
	}
#endif
}

void EXhalbtcoutsrc_UpdateMinBtRssi(s8 btRssi)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->stackInfo.minBtRssi = btRssi;
}

void EXhalbtcoutsrc_SetHciVersion(u16 hciVersion)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->stackInfo.hciVersion = hciVersion;
}

void EXhalbtcoutsrc_SetBtPatchVersion(u16 btHciVersion, u16 btPatchVersion)
{
	PBTC_COEXIST pBtCoexist = &GLBtCoexist;

	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	pBtCoexist->btInfo.btRealFwVer = btPatchVersion;
	pBtCoexist->btInfo.btHciVer = btHciVersion;
}

void EXhalbtcoutsrc_SetBtExist(u8 bBtExist)
{
	GLBtCoexist.boardInfo.bBtExist = bBtExist;
}

void EXhalbtcoutsrc_SetChipType(u8 chipType)
{
	switch(chipType)
	{
		default:
		case BT_2WIRE:
		case BT_ISSC_3WIRE:
		case BT_ACCEL:
		case BT_RTL8756:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_UNDEF;
			break;
		case BT_CSR_BC4:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_CSR_BC4;
			break;
		case BT_CSR_BC8:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_CSR_BC8;
			break;
		case BT_RTL8723A:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_RTL8723A;
			break;
		case BT_RTL8821:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_RTL8821;
			break;
		case BT_RTL8723B:
			GLBtCoexist.boardInfo.btChipType = BTC_CHIP_RTL8723B;
			break;
	}
}

void EXhalbtcoutsrc_SetAntNum(u8 type, u8 antNum, BOOLEAN antInverse)
{
	if (BT_COEX_ANT_TYPE_PG == type)
	{
		GLBtCoexist.boardInfo.pgAntNum = antNum;
		GLBtCoexist.boardInfo.btdmAntNum = antNum;

		//The antenna position: Main (default) or Aux for pgAntNum=2 && btdmAntNum =1
		//The antenna position should be determined by auto-detect mechanism
		// The following is assumed to main, and those must be modified if y auto-detect mechanism is ready
		if ((GLBtCoexist.boardInfo.pgAntNum == 2) && (GLBtCoexist.boardInfo.btdmAntNum == 1) )
			GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
		else
			GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
	}
	else if (BT_COEX_ANT_TYPE_ANTDIV == type)
	{
		GLBtCoexist.boardInfo.btdmAntNum = antNum;
		GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
	}
	else if (BT_COEX_ANT_TYPE_DETECTED == type)
	{
		GLBtCoexist.boardInfo.btdmAntNum = antNum;
		GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
	}

	if (antInverse == _TRUE)
	{
		GLBtCoexist.boardInfo.btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
	}
}

void EXhalbtcoutsrc_DisplayBtCoexInfo(PBTC_COEXIST pBtCoexist)
{
	if (!halbtcoutsrc_IsBtCoexistAvailable(pBtCoexist))
		return;

	halbtcoutsrc_LeaveLowPower(pBtCoexist);

	if (IS_HARDWARE_TYPE_8821(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8821a2ant_DisplayCoexInfo(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8821a1ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723B(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723b2ant_DisplayCoexInfo(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723b1ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8723A(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8723a2ant_DisplayCoexInfo(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8723a1ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192C(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8188c2ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192D(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192d2ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8192E(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8192e2ant_DisplayCoexInfo(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8192e1ant_DisplayCoexInfo(pBtCoexist);
	}
	else if (IS_HARDWARE_TYPE_8812(pBtCoexist->Adapter))
	{
		if (pBtCoexist->boardInfo.btdmAntNum == 2)
			EXhalbtc8812a2ant_DisplayCoexInfo(pBtCoexist);
		else if (pBtCoexist->boardInfo.btdmAntNum == 1)
			EXhalbtc8812a1ant_DisplayCoexInfo(pBtCoexist);
	}

	halbtcoutsrc_NormalLowPower(pBtCoexist);
}

static void halbt_InitHwConfig92C(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 u1Tmp;


	pHalData = GET_HAL_DATA(padapter);
	if( (pHalData->bt_coexist.btChipType == BT_CSR_BC4) ||
		(pHalData->bt_coexist.btChipType == BT_CSR_BC8))
	{
		if (pHalData->rf_type == RF_1T1R)
		{
			// Config to 1T1R
			u1Tmp = rtw_read8(padapter, rOFDM0_TRxPathEnable);
			u1Tmp &= ~BIT(1);
			rtw_write8(padapter, rOFDM0_TRxPathEnable, u1Tmp);
			RT_DISP(FBT, BT_TRACE, ("[BTCoex], BT write 0xC04 = 0x%x\n", u1Tmp));

			u1Tmp = rtw_read8(padapter, rOFDM1_TRxPathEnable);
			u1Tmp &= ~BIT(1);
			rtw_write8(padapter, rOFDM1_TRxPathEnable, u1Tmp);
			RT_DISP(FBT, BT_TRACE, ("[BTCoex], BT write 0xD04 = 0x%x\n", u1Tmp));
		}
	}
}

static void halbt_InitHwConfig92D(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	u8 u1Tmp;

	pHalData = GET_HAL_DATA(padapter);
	if ((pHalData->bt_coexist.btChipType == BT_CSR_BC4) ||
		(pHalData->bt_coexist.btChipType == BT_CSR_BC8))
	{
		if (pHalData->rf_type == RF_1T1R)
		{
			// Config to 1T1R
			u1Tmp = rtw_read8(padapter, rOFDM0_TRxPathEnable);
			u1Tmp &= ~BIT(1);
			rtw_write8(padapter, rOFDM0_TRxPathEnable, u1Tmp);
			RT_DISP(FBT, BT_TRACE, ("[BTCoex], BT write 0xC04 = 0x%x\n", u1Tmp));

			u1Tmp = rtw_read8(padapter, rOFDM1_TRxPathEnable);
			u1Tmp &= ~BIT(1);
			rtw_write8(padapter, rOFDM1_TRxPathEnable, u1Tmp);
			RT_DISP(FBT, BT_TRACE, ("[BTCoex], BT write 0xD04 = 0x%x\n", u1Tmp));
		}
	}
}

/*
 * Description:
 *	Run BT-Coexist mechansim or not
 *
 */
void hal_btcoex_SetBTCoexist(PADAPTER padapter, u8 bBtExist)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->bt_coexist.bBtExist = bBtExist;

	EXhalbtcoutsrc_SetBtExist(bBtExist);
}

/*
 * Dewcription:
 *	Check is co-exist mechanism enabled or not
 *
 * Return:
 *	_TRUE	Enable BT co-exist mechanism
 *	_FALSE	Disable BT co-exist mechanism
 */
u8 hal_btcoex_IsBtExist(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	return pHalData->bt_coexist.bBtExist;
}

u8 hal_btcoex_IsBtDisabled(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return _TRUE;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return _TRUE;
	else
		return _FALSE;
}

void hal_btcoex_SetChipType(PADAPTER padapter, u8 chipType)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	pHalData->bt_coexist.btChipType = chipType;

	EXhalbtcoutsrc_SetChipType(chipType);
}

u8 hal_btcoex_GetChipType(PADAPTER padapter)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);
	return pHalData->bt_coexist.btChipType;
}

void hal_btcoex_SetPgAntNum(PADAPTER padapter, u8 antNum, BOOLEAN antInverse)
{
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->bt_coexist.btTotalAntNum = antNum;
	EXhalbtcoutsrc_SetAntNum(BT_COEX_ANT_TYPE_PG, antNum, antInverse);
}

u8 hal_btcoex_GetPgAntNum(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	return pHalData->bt_coexist.btTotalAntNum;
}

u8 hal_btcoex_Initialize(PADAPTER padapter)
{
	u8 ret1;
	u8 ret2;


	_rtw_memset(&GLBtCoexist, 0, sizeof(GLBtCoexist));
	ret1 = EXhalbtcoutsrc_InitlizeVariables((void*)padapter);
	ret2 = (ret1==_TRUE) ? _TRUE : _FALSE;

	return ret2;
}

void hal_btcoex_InitHwConfig(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return;

	if (IS_HARDWARE_TYPE_8192C(padapter))
	{
		halbt_InitHwConfig92C(padapter);
	}
	else if(IS_HARDWARE_TYPE_8192D(padapter))
	{
		halbt_InitHwConfig92D(padapter);
	}

	EXhalbtcoutsrc_InitHwConfig(&GLBtCoexist);
	EXhalbtcoutsrc_InitCoexDm(&GLBtCoexist);
}

void hal_btcoex_IpsNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_IpsNotify(&GLBtCoexist, type);
}

void hal_btcoex_LpsNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_LpsNotify(&GLBtCoexist, type);
}

void hal_btcoex_ScanNotify(PADAPTER padapter, u8 type)
{
	EXhalbtcoutsrc_ScanNotify(&GLBtCoexist, type);
}

void hal_btcoex_ConnectNotify(PADAPTER padapter, u8 action)
{
	EXhalbtcoutsrc_ConnectNotify(&GLBtCoexist, action);
}

void hal_btcoex_MediaStatusNotify(PADAPTER padapter, u8 mediaStatus)
{
	EXhalbtcoutsrc_MediaStatusNotify(&GLBtCoexist, mediaStatus);
}

void hal_btcoex_SpecialPacketNotify(PADAPTER padapter, u8 pktType)
{
	EXhalbtcoutsrc_SpecialPacketNotify(&GLBtCoexist, pktType);
}

void hal_btcoex_IQKNotify(PADAPTER padapter, u8 state)
{
	GLBtcWiFiInIQKState = state;
}

void hal_btcoex_BtInfoNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	if (GLBtcWiFiInIQKState == _TRUE)
		return;
	
	EXhalbtcoutsrc_BtInfoNotify(&GLBtCoexist, tmpBuf, length);
}

void hal_btcoex_SuspendNotify(PADAPTER padapter, u8 state)
{
	if (state == 1)
		state = BTC_WIFI_PNP_SLEEP;
	else
		state = BTC_WIFI_PNP_WAKE_UP;

	EXhalbtcoutsrc_PnpNotify(&GLBtCoexist, state);
}

void hal_btcoex_HaltNotify(PADAPTER padapter)
{
	EXhalbtcoutsrc_HaltNotify(&GLBtCoexist);
}

void hal_btcoex_SwitchGntBt(PADAPTER padapter)
{
	EXhalbtcoutsrc_SwitchGntBt(&GLBtCoexist);
}

void hal_btcoex_Hanlder(PADAPTER padapter)
{
	EXhalbtcoutsrc_Periodical(&GLBtCoexist);
}

s32 hal_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER padapter)
{
	return (s32)GLBtCoexist.btInfo.bBtCtrlAggBufSize;
}

u32 hal_btcoex_GetAMPDUSize(PADAPTER padapter)
{
	return (u32)GLBtCoexist.btInfo.aggBufSize;
}

void hal_btcoex_SetManualControl(PADAPTER padapter, u8 bmanual)
{
	GLBtCoexist.bManualControl = bmanual;
}

u8 hal_btcoex_1Ant(PADAPTER padapter)
{
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.boardInfo.btdmAntNum == 1)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsBtControlLps(PADAPTER padapter)
{
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return _FALSE;

	if (GLBtCoexist.btInfo.bBtCtrlLps)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsLpsOn(PADAPTER padapter)
{
	if (hal_btcoex_IsBtExist(padapter) == _FALSE)
		return _FALSE;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return _FALSE;

	if (GLBtCoexist.btInfo.bBtLpsOn)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_RpwmVal(PADAPTER padapter)
{
	return GLBtCoexist.btInfo.rpwmVal;
}

u8 hal_btcoex_LpsVal(PADAPTER padapter)
{
	return GLBtCoexist.btInfo.lpsVal;
}

u32 hal_btcoex_GetRaMask(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return 0;

	if (GLBtCoexist.btInfo.bBtDisabled)
		return 0;

	if (GLBtCoexist.boardInfo.btdmAntNum != 1)
		return 0;

	return GLBtCoexist.btInfo.raMask;
}

void hal_btcoex_RecordPwrMode(PADAPTER padapter, u8 *pCmdBuf, u8 cmdLen)
{
	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_EXEC, ("[BTCoex], FW write pwrModeCmd=0x%04x%08x\n",
		pCmdBuf[0]<<8|pCmdBuf[1],
		pCmdBuf[2]<<24|pCmdBuf[3]<<16|pCmdBuf[4]<<8|pCmdBuf[5]));

	_rtw_memcpy(GLBtCoexist.pwrModeVal, pCmdBuf, cmdLen);
}

void hal_btcoex_DisplayBtCoexInfo(PADAPTER padapter, u8 *pbuf, u32 bufsize)
{
	PBTCDBGINFO pinfo;


	pinfo = &GLBtcDbgInfo;
	DBG_BT_INFO_INIT(pinfo, pbuf, bufsize);
	EXhalbtcoutsrc_DisplayBtCoexInfo(&GLBtCoexist);
	DBG_BT_INFO_INIT(pinfo, NULL, 0);
}

void hal_btcoex_SetDBG(PADAPTER padapter, u32 *pDbgModule)
{
	u32 i;


	if (NULL == pDbgModule)
		return;

	for (i=0; i<BTC_MSG_MAX; i++)
		GLBtcDbgType[i] = pDbgModule[i];
}

u32 hal_btcoex_GetDBG(PADAPTER padapter, u8 *pStrBuf, u32 bufSize)
{
	s32 count;
	u8 *pstr;
	u32 leftSize;


	if ((NULL == pStrBuf) || (0 == bufSize))
		return 0;

	count = 0;
	pstr = pStrBuf;
	leftSize = bufSize;
//	DBG_871X(FUNC_ADPT_FMT ": bufsize=%d\n", FUNC_ADPT_ARG(padapter), bufSize);

	count = rtw_sprintf(pstr, leftSize, "#define DBG\t%d\n", DBG);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize, "BTCOEX Debug Setting:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize,
		"INTERFACE / ALGORITHM: 0x%08X / 0x%08X\n\n",
		GLBtcDbgType[BTC_MSG_INTERFACE],
		GLBtcDbgType[BTC_MSG_ALGORITHM]);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

	count = rtw_sprintf(pstr, leftSize, "INTERFACE Debug Setting Definition:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[0]=%d for INTF_INIT\n",
		GLBtcDbgType[BTC_MSG_INTERFACE]&INTF_INIT?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[2]=%d for INTF_NOTIFY\n\n",
		GLBtcDbgType[BTC_MSG_INTERFACE]&INTF_NOTIFY?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	
	count = rtw_sprintf(pstr, leftSize, "ALGORITHM Debug Setting Definition:\n");
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[0]=%d for BT_RSSI_STATE\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_BT_RSSI_STATE?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[1]=%d for WIFI_RSSI_STATE\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_WIFI_RSSI_STATE?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[2]=%d for BT_MONITOR\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_BT_MONITOR?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[3]=%d for TRACE\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[4]=%d for TRACE_FW\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_FW?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[5]=%d for TRACE_FW_DETAIL\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_FW_DETAIL?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[6]=%d for TRACE_FW_EXEC\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_FW_EXEC?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[7]=%d for TRACE_SW\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_SW?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[8]=%d for TRACE_SW_DETAIL\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_SW_DETAIL?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;
	count = rtw_sprintf(pstr, leftSize, "\tbit[9]=%d for TRACE_SW_EXEC\n",
		GLBtcDbgType[BTC_MSG_ALGORITHM]&ALGO_TRACE_SW_EXEC?1:0);
	if ((count < 0) || (count >= leftSize))
		goto exit;
	pstr += count;
	leftSize -= count;

exit:
	count = pstr - pStrBuf;
//	DBG_871X(FUNC_ADPT_FMT ": usedsize=%d\n", FUNC_ADPT_ARG(padapter), count);

	return count;
}

u8 hal_btcoex_IncreaseScanDeviceNum(PADAPTER padapter)
{
	if (!hal_btcoex_IsBtExist(padapter))
		return _FALSE;

	if (GLBtCoexist.btInfo.bIncreaseScanDevNum)
		return _TRUE;

	return _FALSE;
}

u8 hal_btcoex_IsBtLinkExist(PADAPTER padapter)
{
	if (GLBtCoexist.btLinkInfo.bBtLinkExist)
		return _TRUE;

	return _FALSE;
}
#endif // CONFIG_BT_COEXIST

