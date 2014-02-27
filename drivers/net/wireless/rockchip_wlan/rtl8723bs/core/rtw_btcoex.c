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
#ifdef CONFIG_BT_COEXIST

#include <rtw_btcoex.h>
#include <hal_btcoex.h>


void rtw_btcoex_Initialize(PADAPTER padapter)
{
	hal_btcoex_Initialize(padapter);
}

void rtw_btcoex_HAL_Initialize(PADAPTER padapter)
{
	hal_btcoex_InitHwConfig(padapter);
}

void rtw_btcoex_IpsNotify(PADAPTER padapter, u8 type)
{
	hal_btcoex_IpsNotify(padapter, type);
}

void rtw_btcoex_LpsNotify(PADAPTER padapter, u8 type)
{
	hal_btcoex_LpsNotify(padapter, type);
}

void rtw_btcoex_ScanNotify(PADAPTER padapter, u8 type)
{
#ifdef CONFIG_CONCURRENT_MODE
	if ((_FALSE == type) && (padapter->pbuddy_adapter))
	{
		PADAPTER pbuddy = padapter->pbuddy_adapter;
		if (check_fwstate(&pbuddy->mlmepriv, WIFI_SITE_MONITOR) == _TRUE)
			return;
	}
#endif

	hal_btcoex_ScanNotify(padapter, type);
}

void rtw_btcoex_ConnectNotify(PADAPTER padapter, u8 action)
{
#ifdef DBG_CONFIG_ERROR_RESET
	if (_TRUE == rtw_hal_sreset_inprogress(padapter))
	{
		DBG_8192C(FUNC_ADPT_FMT ": [BTCoex] under reset, skip notify!\n",
			FUNC_ADPT_ARG(padapter));
		return;
	}
#endif // DBG_CONFIG_ERROR_RESET
		
#ifdef CONFIG_CONCURRENT_MODE
	if ((_FALSE == action) && (padapter->pbuddy_adapter))
	{
		PADAPTER pbuddy = padapter->pbuddy_adapter;
		if (check_fwstate(&pbuddy->mlmepriv, WIFI_UNDER_LINKING) == _TRUE)
			return;
	}
#endif

	hal_btcoex_ConnectNotify(padapter, action);
}

void rtw_btcoex_MediaStatusNotify(PADAPTER padapter, u8 mediaStatus)
{
#ifdef DBG_CONFIG_ERROR_RESET
	if (_TRUE == rtw_hal_sreset_inprogress(padapter))
	{
		DBG_8192C(FUNC_ADPT_FMT ": [BTCoex] under reset, skip notify!\n",
			FUNC_ADPT_ARG(padapter));
		return;
	}
#endif // DBG_CONFIG_ERROR_RESET

#ifdef CONFIG_CONCURRENT_MODE
	if ((RT_MEDIA_DISCONNECT == mediaStatus) && (padapter->pbuddy_adapter))
	{
		PADAPTER pbuddy = padapter->pbuddy_adapter;
		if (check_fwstate(&pbuddy->mlmepriv, WIFI_ASOC_STATE) == _TRUE)
			return;
	}
#endif // CONFIG_CONCURRENT_MODE

	if ((RT_MEDIA_CONNECT == mediaStatus)
		&& (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == _TRUE))
	{
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_RSVD_PAGE, NULL);
	}

	hal_btcoex_MediaStatusNotify(padapter, mediaStatus);
}

void rtw_btcoex_SpecialPacketNotify(PADAPTER padapter, u8 pktType)
{
	hal_btcoex_SpecialPacketNotify(padapter, pktType);
}

void rtw_btcoex_BtInfoNotify(PADAPTER padapter, u8 length, u8 *tmpBuf)
{
	hal_btcoex_BtInfoNotify(padapter, length, tmpBuf);
}

void rtw_btcoex_SuspendNotify(PADAPTER padapter, u8 state)
{
	hal_btcoex_SuspendNotify(padapter, state);
}

void rtw_btcoex_HaltNotify(PADAPTER padapter)
{
	if (_FALSE == padapter->bup)
	{
		DBG_871X(FUNC_ADPT_FMT ": bup=%d Skip!\n",
			FUNC_ADPT_ARG(padapter), padapter->bup);

		return;
	}

	if (_TRUE == padapter->bSurpriseRemoved)
	{
		DBG_871X(FUNC_ADPT_FMT ": bSurpriseRemoved=%d Skip!\n",
			FUNC_ADPT_ARG(padapter), padapter->bSurpriseRemoved);

		return;
	}

	hal_btcoex_HaltNotify(padapter);
}

void rtw_btcoex_SwitchGntBt(PADAPTER padapter)
{
	hal_btcoex_SwitchGntBt(padapter);	
}

void rtw_btcoex_Switch(PADAPTER padapter, u8 enable)
{
	hal_btcoex_SetBTCoexist(padapter, enable);
}

u8 rtw_btcoex_IsBtDisabled(PADAPTER padapter)
{
	return hal_btcoex_IsBtDisabled(padapter);
}

void rtw_btcoex_Handler(PADAPTER padapter)
{
#if defined(CONFIG_CONCURRENT_MODE)
	if (padapter->adapter_type != PRIMARY_ADAPTER)
		return;
#endif

	hal_btcoex_Hanlder(padapter);
}

s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(PADAPTER padapter)
{
	s32 coexctrl;

	coexctrl = hal_btcoex_IsBTCoexCtrlAMPDUSize(padapter);

	return coexctrl;
}

u32 rtw_btcoex_GetAMPDUSize(PADAPTER padapter)
{
	u32 size;

	size = hal_btcoex_GetAMPDUSize(padapter);

	return size;
}

void rtw_btcoex_SetManualControl(PADAPTER padapter, u8 manual)
{
	if (_TRUE == manual)
	{
		hal_btcoex_SetManualControl(padapter, _TRUE);
	}
	else
	{
		hal_btcoex_SetManualControl(padapter, _FALSE);
	}
}

u8 rtw_btcoex_1Ant(PADAPTER padapter)
{
	return hal_btcoex_1Ant(padapter);
}

u8 rtw_btcoex_IsBtControlLps(PADAPTER padapter)
{
	return hal_btcoex_IsBtControlLps(padapter);
}

u8 rtw_btcoex_IsLpsOn(PADAPTER padapter)
{
	return hal_btcoex_IsLpsOn(padapter);
}

u8 rtw_btcoex_RpwmVal(PADAPTER padapter)
{
	return hal_btcoex_RpwmVal(padapter);
}

u8 rtw_btcoex_LpsVal(PADAPTER padapter)
{
	return hal_btcoex_LpsVal(padapter);
}

void rtw_btcoex_SetBTCoexist(PADAPTER padapter, u8 bBtExist)
{
	hal_btcoex_SetBTCoexist(padapter, bBtExist);
}

void rtw_btcoex_SetChipType(PADAPTER padapter, u8 chipType)
{
	hal_btcoex_SetChipType(padapter, chipType);
}

void rtw_btcoex_SetPGAntNum(PADAPTER padapter, u8 antNum)
{
	hal_btcoex_SetPgAntNum(padapter, antNum);
}

u8 rtw_btcoex_GetPGAntNum(PADAPTER padapter)
{
	return hal_btcoex_GetPgAntNum(padapter);
}

u32 rtw_btcoex_GetRaMask(PADAPTER padapter)
{
	return hal_btcoex_GetRaMask(padapter);
}

void rtw_btcoex_RecordPwrMode(PADAPTER padapter, u8 *pCmdBuf, u8 cmdLen)
{
	hal_btcoex_RecordPwrMode(padapter, pCmdBuf, cmdLen);
}

void rtw_btcoex_DisplayBtCoexInfo(PADAPTER padapter, u8 *pbuf, u32 bufsize)
{
	hal_btcoex_DisplayBtCoexInfo(padapter, pbuf, bufsize);
}

void rtw_btcoex_SetDBG(PADAPTER padapter, u32 *pDbgModule)
{
	hal_btcoex_SetDBG(padapter, pDbgModule);
}

u32 rtw_btcoex_GetDBG(PADAPTER padapter, u8 *pStrBuf, u32 bufSize)
{
	return hal_btcoex_GetDBG(padapter, pStrBuf, bufSize);
}

u8 rtw_btcoex_IncreaseScanDeviceNum(PADAPTER padapter)
{
	return hal_btcoex_IncreaseScanDeviceNum(padapter);
}

u8 rtw_btcoex_IsBtLinkExist(PADAPTER padapter)
{
	return hal_btcoex_IsBtLinkExist(padapter);
}

// ==================================================
// Below Functions are called by BT-Coex
// ==================================================
void rtw_btcoex_RejectApAggregatedPacket(PADAPTER padapter, u8 enable)
{
	struct mlme_ext_info *pmlmeinfo;
	struct sta_info *psta;

	pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;
	psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));

	if (_TRUE == enable)
	{
		pmlmeinfo->bAcceptAddbaReq = _FALSE;
		send_delba(padapter, 0, psta->hwaddr);
	}
	else
	{
		pmlmeinfo->bAcceptAddbaReq = _TRUE;
	}
}

void rtw_btcoex_LPS_Enter(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv;
	u8 lpsVal;


	pwrpriv = adapter_to_pwrctl(padapter);

	pwrpriv->bpower_saving = _TRUE;
	lpsVal = rtw_btcoex_LpsVal(padapter);
	rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, lpsVal, "BTCOEX");
}

void rtw_btcoex_LPS_Leave(PADAPTER padapter)
{
	struct pwrctrl_priv *pwrpriv;


	pwrpriv = adapter_to_pwrctl(padapter);

	if (pwrpriv->pwr_mode != PS_MODE_ACTIVE)
	{
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "BTCOEX");
		LPS_RF_ON_check(padapter, 100);
		pwrpriv->bpower_saving = _FALSE;
	}
}
#endif // CONFIG_BT_COEXIST

