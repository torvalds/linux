// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#include <drv_types.h>
#include <rtw_debug.h>
#include <rtw_btcoex.h>
#include <hal_btcoex.h>

void rtw_btcoex_IpsNotify(struct adapter *padapter, u8 type)
{
	hal_btcoex_IpsNotify(padapter, type);
}

void rtw_btcoex_LpsNotify(struct adapter *padapter, u8 type)
{
	hal_btcoex_LpsNotify(padapter, type);
}

void rtw_btcoex_ScanNotify(struct adapter *padapter, u8 type)
{
	hal_btcoex_ScanNotify(padapter, type);
}

void rtw_btcoex_ConnectNotify(struct adapter *padapter, u8 action)
{
	hal_btcoex_ConnectNotify(padapter, action);
}

void rtw_btcoex_MediaStatusNotify(struct adapter *padapter, u8 mediaStatus)
{
	if ((mediaStatus == RT_MEDIA_CONNECT)
		&& (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)) {
		rtw_hal_set_hwreg(padapter, HW_VAR_DL_RSVD_PAGE, NULL);
	}

	hal_btcoex_MediaStatusNotify(padapter, mediaStatus);
}

void rtw_btcoex_SpecialPacketNotify(struct adapter *padapter, u8 pktType)
{
	hal_btcoex_SpecialPacketNotify(padapter, pktType);
}

void rtw_btcoex_IQKNotify(struct adapter *padapter, u8 state)
{
	hal_btcoex_IQKNotify(padapter, state);
}

void rtw_btcoex_BtInfoNotify(struct adapter *padapter, u8 length, u8 *tmpBuf)
{
	hal_btcoex_BtInfoNotify(padapter, length, tmpBuf);
}

void rtw_btcoex_SuspendNotify(struct adapter *padapter, u8 state)
{
	hal_btcoex_SuspendNotify(padapter, state);
}

void rtw_btcoex_HaltNotify(struct adapter *padapter)
{
	if (!padapter->bup) {
		DBG_871X(FUNC_ADPT_FMT ": bup =%d Skip!\n",
			FUNC_ADPT_ARG(padapter), padapter->bup);

		return;
	}

	if (padapter->bSurpriseRemoved) {
		DBG_871X(FUNC_ADPT_FMT ": bSurpriseRemoved =%d Skip!\n",
			FUNC_ADPT_ARG(padapter), padapter->bSurpriseRemoved);

		return;
	}

	hal_btcoex_HaltNotify(padapter);
}

u8 rtw_btcoex_IsBtDisabled(struct adapter *padapter)
{
	return hal_btcoex_IsBtDisabled(padapter);
}

void rtw_btcoex_Handler(struct adapter *padapter)
{
	hal_btcoex_Handler(padapter);
}

s32 rtw_btcoex_IsBTCoexCtrlAMPDUSize(struct adapter *padapter)
{
	s32 coexctrl;

	coexctrl = hal_btcoex_IsBTCoexCtrlAMPDUSize(padapter);

	return coexctrl;
}

void rtw_btcoex_SetManualControl(struct adapter *padapter, u8 manual)
{
	hal_btcoex_SetManualControl(padapter, manual);
}

u8 rtw_btcoex_IsBtControlLps(struct adapter *padapter)
{
	return hal_btcoex_IsBtControlLps(padapter);
}

u8 rtw_btcoex_IsLpsOn(struct adapter *padapter)
{
	return hal_btcoex_IsLpsOn(padapter);
}

u8 rtw_btcoex_RpwmVal(struct adapter *padapter)
{
	return hal_btcoex_RpwmVal(padapter);
}

u8 rtw_btcoex_LpsVal(struct adapter *padapter)
{
	return hal_btcoex_LpsVal(padapter);
}

void rtw_btcoex_SetBTCoexist(struct adapter *padapter, u8 bBtExist)
{
	hal_btcoex_SetBTCoexist(padapter, bBtExist);
}

void rtw_btcoex_SetChipType(struct adapter *padapter, u8 chipType)
{
	hal_btcoex_SetChipType(padapter, chipType);
}

void rtw_btcoex_SetPGAntNum(struct adapter *padapter, u8 antNum)
{
	hal_btcoex_SetPgAntNum(padapter, antNum);
}

void rtw_btcoex_SetSingleAntPath(struct adapter *padapter, u8 singleAntPath)
{
	hal_btcoex_SetSingleAntPath(padapter, singleAntPath);
}

u32 rtw_btcoex_GetRaMask(struct adapter *padapter)
{
	return hal_btcoex_GetRaMask(padapter);
}

void rtw_btcoex_RecordPwrMode(struct adapter *padapter, u8 *pCmdBuf, u8 cmdLen)
{
	hal_btcoex_RecordPwrMode(padapter, pCmdBuf, cmdLen);
}

void rtw_btcoex_DisplayBtCoexInfo(struct adapter *padapter, u8 *pbuf, u32 bufsize)
{
	hal_btcoex_DisplayBtCoexInfo(padapter, pbuf, bufsize);
}

void rtw_btcoex_SetDBG(struct adapter *padapter, u32 *pDbgModule)
{
	hal_btcoex_SetDBG(padapter, pDbgModule);
}

u32 rtw_btcoex_GetDBG(struct adapter *padapter, u8 *pStrBuf, u32 bufSize)
{
	return hal_btcoex_GetDBG(padapter, pStrBuf, bufSize);
}

/*  ================================================== */
/*  Below Functions are called by BT-Coex */
/*  ================================================== */
void rtw_btcoex_RejectApAggregatedPacket(struct adapter *padapter, u8 enable)
{
	struct mlme_ext_info *pmlmeinfo;
	struct sta_info *psta;

	pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;
	psta = rtw_get_stainfo(&padapter->stapriv, get_bssid(&padapter->mlmepriv));

	if (enable) {
		pmlmeinfo->accept_addba_req = false;
		if (psta)
			send_delba(padapter, 0, psta->hwaddr);
	} else {
		pmlmeinfo->accept_addba_req = true;
	}
}

void rtw_btcoex_LPS_Enter(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv;
	u8 lpsVal;


	pwrpriv = adapter_to_pwrctl(padapter);

	pwrpriv->bpower_saving = true;
	lpsVal = rtw_btcoex_LpsVal(padapter);
	rtw_set_ps_mode(padapter, PS_MODE_MIN, 0, lpsVal, "BTCOEX");
}

void rtw_btcoex_LPS_Leave(struct adapter *padapter)
{
	struct pwrctrl_priv *pwrpriv;


	pwrpriv = adapter_to_pwrctl(padapter);

	if (pwrpriv->pwr_mode != PS_MODE_ACTIVE) {
		rtw_set_ps_mode(padapter, PS_MODE_ACTIVE, 0, 0, "BTCOEX");
		LPS_RF_ON_check(padapter, 100);
		pwrpriv->bpower_saving = false;
	}
}
