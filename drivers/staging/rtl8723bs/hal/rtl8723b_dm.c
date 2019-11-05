// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
/*  Description: */
/*  This file is for 92CE/92CU dynamic mechanism only */

#define _RTL8723B_DM_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>

/*  Global var */

static void dm_CheckStatistics(struct adapter *Adapter)
{
}
/*  */
/*  functions */
/*  */
static void Init_ODM_ComInfo_8723b(struct adapter *Adapter)
{

	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	u8 cut_ver, fab_ver;

	/*  */
	/*  Init Value */
	/*  */
	memset(pDM_Odm, 0, sizeof(*pDM_Odm));

	pDM_Odm->Adapter = Adapter;
#define ODM_CE 0x04
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PLATFORM, ODM_CE);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_INTERFACE, RTW_SDIO);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PACKAGE_TYPE, pHalData->PackageType);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_IC_TYPE, ODM_RTL8723B);

	fab_ver = ODM_TSMC;
	cut_ver = ODM_CUT_A;

	DBG_871X("%s(): fab_ver =%d cut_ver =%d\n", __func__, fab_ver, cut_ver);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_FAB_VER, fab_ver);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_CUT_VER, cut_ver);
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(pHalData->VersionID));

	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_PATCH_ID, pHalData->CustomerID);
	/* 	ODM_CMNINFO_BINHCT_TEST only for MP Team */
	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_BWIFI_TEST, Adapter->registrypriv.wifi_spec);


	if (pHalData->rf_type == RF_1T1R) {
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T1R);
	} else if (pHalData->rf_type == RF_2T2R) {
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T2R);
	} else if (pHalData->rf_type == RF_1T2R) {
		ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T2R);
	}

	pdmpriv->InitODMFlag = ODM_RF_CALIBRATION|ODM_RF_TX_PWR_TRACK;

	ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pdmpriv->InitODMFlag);
}

static void Update_ODM_ComInfo_8723b(struct adapter *Adapter)
{
	struct mlme_ext_priv *pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	int i;
	u8 zero = 0;

	pdmpriv->InitODMFlag = 0
		| ODM_BB_DIG
		| ODM_BB_RA_MASK
		| ODM_BB_DYNAMIC_TXPWR
		| ODM_BB_FA_CNT
		| ODM_BB_RSSI_MONITOR
		| ODM_BB_CCK_PD
		| ODM_BB_PWR_SAVE
		| ODM_BB_CFO_TRACKING
		| ODM_MAC_EDCA_TURBO
		| ODM_RF_TX_PWR_TRACK
		| ODM_RF_CALIBRATION
#ifdef CONFIG_ODM_ADAPTIVITY
		| ODM_BB_ADAPTIVITY
#endif
		;

	/*  */
	/*  Pointer reference */
	/*  */
	/* ODM_CMNINFO_MAC_PHY_MODE pHalData->MacPhyMode92D */
	/* 	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_MAC_PHY_MODE,&(pDM_Odm->u8_temp)); */

	ODM_CmnInfoUpdate(pDM_Odm, ODM_CMNINFO_ABILITY, pdmpriv->InitODMFlag);

	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_TX_UNI, &(dvobj->traffic_stat.tx_bytes));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_RX_UNI, &(dvobj->traffic_stat.rx_bytes));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_WM_MODE, &(pmlmeext->cur_wireless_mode));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SEC_CHNL_OFFSET, &(pHalData->nCur40MhzPrimeSC));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SEC_MODE, &(Adapter->securitypriv.dot11PrivacyAlgrthm));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BW, &(pHalData->CurrentChannelBW));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_CHNL, &(pHalData->CurrentChannel));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_NET_CLOSED, &(Adapter->net_closed));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_MP_MODE, &zero);
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BAND, &(pHalData->CurrentBandType));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_FORCED_IGI_LB, &(pHalData->u1ForcedIgiLb));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_FORCED_RATE, &(pHalData->ForcedDataRate));

	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SCAN, &(pmlmepriv->bScanInProcess));
	ODM_CmnInfoHook(pDM_Odm, ODM_CMNINFO_POWER_SAVING, &(pwrctrlpriv->bpower_saving));


	for (i = 0; i < NUM_STA; i++)
		ODM_CmnInfoPtrArrayHook(pDM_Odm, ODM_CMNINFO_STA_STATUS, i, NULL);
}

void rtl8723b_InitHalDm(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T pDM_Odm = &(pHalData->odmpriv);

	pdmpriv->DM_Type = DM_Type_ByDriver;
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;

	pdmpriv->DMFlag |= DYNAMIC_FUNC_BT;

	pdmpriv->InitDMFlag = pdmpriv->DMFlag;

	Update_ODM_ComInfo_8723b(Adapter);

	ODM_DMInit(pDM_Odm);
}

void rtl8723b_HalDmWatchDog(struct adapter *Adapter)
{
	bool bFwCurrentInPSMode = false;
	bool bFwPSAwake = true;
	u8 hw_init_completed = false;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);

	hw_init_completed = Adapter->hw_init_completed;

	if (hw_init_completed == false)
		goto skip_dm;

	bFwCurrentInPSMode = adapter_to_pwrctl(Adapter)->bFwCurrentInPSMode;
	rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));

	if (
		(hw_init_completed == true) &&
		((!bFwCurrentInPSMode) && bFwPSAwake)
	) {
		/*  */
		/*  Calculate Tx/Rx statistics. */
		/*  */
		dm_CheckStatistics(Adapter);
		rtw_hal_check_rxfifo_full(Adapter);
	}

	/* ODM */
	if (hw_init_completed == true) {
		u8 bLinked = false;
		u8 bsta_state = false;
		bool bBtDisabled = true;

		if (rtw_linked_check(Adapter)) {
			bLinked = true;
			if (check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE))
				bsta_state = true;
		}

		ODM_CmnInfoUpdate(&pHalData->odmpriv, ODM_CMNINFO_LINK, bLinked);
		ODM_CmnInfoUpdate(&pHalData->odmpriv, ODM_CMNINFO_STATION_STATE, bsta_state);

		/* ODM_CmnInfoUpdate(&pHalData->odmpriv , ODM_CMNINFO_RSSI_MIN, pdmpriv->MinUndecoratedPWDBForDM); */

		bBtDisabled = hal_btcoex_IsBtDisabled(Adapter);

		ODM_CmnInfoUpdate(&pHalData->odmpriv, ODM_CMNINFO_BT_ENABLED,
				  !bBtDisabled);

		ODM_DMWatchdog(&pHalData->odmpriv);
	}

skip_dm:
	return;
}

void rtl8723b_hal_dm_in_lps(struct adapter *padapter)
{
	u32 PWDB_rssi = 0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct sta_info *psta = NULL;

	DBG_871X("%s, RSSI_Min =%d\n", __func__, pDM_Odm->RSSI_Min);

	/* update IGI */
	ODM_Write_DIG(pDM_Odm, pDM_Odm->RSSI_Min);


	/* set rssi to fw */
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (psta && (psta->rssi_stat.UndecoratedSmoothedPWDB > 0)) {
		PWDB_rssi = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16));

		rtl8723b_set_rssi_cmd(padapter, (u8 *)&PWDB_rssi);
	}

}

void rtl8723b_HalDmWatchDog_in_LPS(struct adapter *Adapter)
{
	u8 bLinked = false;
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T pDM_Odm = &pHalData->odmpriv;
	pDIG_T pDM_DigTable = &pDM_Odm->DM_DigTable;
	struct sta_priv *pstapriv = &Adapter->stapriv;
	struct sta_info *psta = NULL;

	if (Adapter->hw_init_completed == false)
		goto skip_lps_dm;


	if (rtw_linked_check(Adapter))
		bLinked = true;

	ODM_CmnInfoUpdate(&pHalData->odmpriv, ODM_CMNINFO_LINK, bLinked);

	if (bLinked == false)
		goto skip_lps_dm;

	if (!(pDM_Odm->SupportAbility & ODM_BB_RSSI_MONITOR))
		goto skip_lps_dm;


	/* ODM_DMWatchdog(&pHalData->odmpriv); */
	/* Do DIG by RSSI In LPS-32K */

      /* 1 Find MIN-RSSI */
	psta = rtw_get_stainfo(pstapriv, get_bssid(pmlmepriv));
	if (!psta)
		goto skip_lps_dm;

	pdmpriv->EntryMinUndecoratedSmoothedPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

	DBG_871X("CurIGValue =%d, EntryMinUndecoratedSmoothedPWDB = %d\n", pDM_DigTable->CurIGValue, pdmpriv->EntryMinUndecoratedSmoothedPWDB);

	if (pdmpriv->EntryMinUndecoratedSmoothedPWDB <= 0)
		goto skip_lps_dm;

	pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;

	pDM_Odm->RSSI_Min = pdmpriv->MinUndecoratedPWDBForDM;

	/* if (pDM_DigTable->CurIGValue != pDM_Odm->RSSI_Min) */
	if (
		(pDM_DigTable->CurIGValue > pDM_Odm->RSSI_Min + 5) ||
		(pDM_DigTable->CurIGValue < pDM_Odm->RSSI_Min - 5)
	)
		rtw_dm_in_lps_wk_cmd(Adapter);


skip_lps_dm:

	return;

}

void rtl8723b_init_dm_priv(struct adapter *Adapter)
{
	struct hal_com_data *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;

	memset(pdmpriv, 0, sizeof(struct dm_priv));
	Init_ODM_ComInfo_8723b(Adapter);
}
