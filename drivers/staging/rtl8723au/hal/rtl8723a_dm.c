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
 ******************************************************************************/
/*  */
/*  Description: */
/*  */
/*  This file is for 92CE/92CU dynamic mechanism only */
/*  */
/*  */
/*  */
#define _RTL8723A_DM_C_

/*  */
/*  include files */
/*  */
#include <osdep_service.h>
#include <drv_types.h>

#include <rtl8723a_hal.h>
#include <usb_ops_linux.h>

/*  */
/*  Global var */
/*  */

static void dm_CheckPbcGPIO(struct rtw_adapter *padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = false;

	if (!padapter->registrypriv.hw_wps_pbc)
		return;

	tmp1byte = rtl8723au_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	/* enable GPIO[2] as output mode */
	rtl8723au_write8(padapter, GPIO_IO_SEL, tmp1byte);

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	/* reset the floating voltage level */
	rtl8723au_write8(padapter,  GPIO_IN, tmp1byte);

	tmp1byte = rtl8723au_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	/* enable GPIO[2] as input mode */
	rtl8723au_write8(padapter, GPIO_IO_SEL, tmp1byte);

	tmp1byte = rtl8723au_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff)
		return;

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
		bPbcPressed = true;

	if (bPbcPressed) {
		/*  Here we only set bPbcPressed to true */
		/*  After trigger PBC, the variable will be set to false */
		DBG_8723A("CheckPbcGPIO - PBC is pressed\n");

		if (padapter->pid[0] == 0) {
			/* 0 is the default value and it means the application
			 * monitors the HW PBC doesn't privde its pid to driver.
			 */
			return;
		}

		kill_pid(find_vpid(padapter->pid[0]), SIGUSR1, 1);
	}
}

/*  Initialize GPIO setting registers */
/*  functions */
static void Init_ODM_ComInfo_8723a(struct rtw_adapter *Adapter)
{

	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;
	u8	cut_ver, fab_ver;

	/*  */
	/*  Init Value */
	/*  */
	memset(pDM_Odm, 0, sizeof(*pDM_Odm));

	pDM_Odm->Adapter = Adapter;
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_PLATFORM, 0x04);
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_INTERFACE, RTW_USB);/* RTL871X_HCI_TYPE */

	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_IC_TYPE, ODM_RTL8723A);

	if (IS_8723A_A_CUT(pHalData->VersionID)) {
		fab_ver = ODM_UMC;
		cut_ver = ODM_CUT_A;
	} else if (IS_8723A_B_CUT(pHalData->VersionID)) {
		fab_ver = ODM_UMC;
		cut_ver = ODM_CUT_B;
	} else {
		fab_ver = ODM_TSMC;
		cut_ver = ODM_CUT_A;
	}
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_FAB_VER, fab_ver);
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_CUT_VER, cut_ver);
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(pHalData->VersionID));

	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_BOARD_TYPE, pHalData->BoardType);

	if (pHalData->BoardType == BOARD_USB_High_PA) {
		ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_EXT_LNA, true);
		ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_EXT_PA, true);
	}
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_PATCH_ID, pHalData->CustomerID);
	ODM_CmnInfoInit23a(pDM_Odm, ODM_CMNINFO_BWIFI_TEST, Adapter->registrypriv.wifi_spec);

	if (pHalData->rf_type == RF_1T1R)
		ODM_CmnInfoUpdate23a(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T1R);
	else if (pHalData->rf_type == RF_2T2R)
		ODM_CmnInfoUpdate23a(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_2T2R);
	else if (pHalData->rf_type == RF_1T2R)
		ODM_CmnInfoUpdate23a(pDM_Odm, ODM_CMNINFO_RF_TYPE, ODM_1T2R);
}

static void Update_ODM_ComInfo_8723a(struct rtw_adapter *Adapter)
{
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int i;
	pdmpriv->InitODMFlag =	ODM_BB_DIG		|
				ODM_BB_RA_MASK		|
				ODM_BB_DYNAMIC_TXPWR	|
				ODM_BB_FA_CNT		|
				ODM_BB_RSSI_MONITOR	|
				ODM_BB_CCK_PD		|
				ODM_BB_PWR_SAVE		|
				ODM_MAC_EDCA_TURBO	|
				ODM_RF_TX_PWR_TRACK	|
				ODM_RF_CALIBRATION;
	/*  Pointer reference */
	rtl8723a_odm_support_ability_set(Adapter, DYNAMIC_ALL_FUNC_ENABLE);

	ODM23a_CmnInfoHook(pDM_Odm, ODM_CMNINFO_BW,
			   &pHalData->CurrentChannelBW);
	ODM23a_CmnInfoHook(pDM_Odm, ODM_CMNINFO_CHNL,
			   &pHalData->CurrentChannel);
	ODM23a_CmnInfoHook(pDM_Odm, ODM_CMNINFO_SCAN, &pmlmepriv->bScanInProcess);
	ODM23a_CmnInfoHook(pDM_Odm, ODM_CMNINFO_POWER_SAVING,
			   &pwrctrlpriv->bpower_saving);

	for (i = 0; i < NUM_STA; i++)
		ODM_CmnInfoPtrArrayHook23a(pDM_Odm, ODM_CMNINFO_STA_STATUS, i, NULL);
}

void rtl8723a_InitHalDm(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct dm_odm_t *pDM_Odm = &pHalData->odmpriv;
	u8	i;

	Update_ODM_ComInfo_8723a(Adapter);
	ODM23a_DMInit(pDM_Odm);
	/*  Save REG_INIDATA_RATE_SEL value for TXDESC. */
	for (i = 0; i < 32; i++)
		pdmpriv->INIDATA_RATE[i] = rtl8723au_read8(Adapter, REG_INIDATA_RATE_SEL+i) & 0x3f;
}

void
rtl8723a_HalDmWatchDog(
	struct rtw_adapter *Adapter
	)
{
	bool		bFwCurrentInPSMode = false;
	bool		bFwPSAwake = true;
	u8 bLinked = false;
	u8 hw_init_completed = false;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	hw_init_completed = Adapter->hw_init_completed;

	if (hw_init_completed == false)
		goto skip_dm;

	bFwCurrentInPSMode = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
	bFwPSAwake = rtl8723a_get_fwlps_rf_on(Adapter);

	if (!bFwCurrentInPSMode && bFwPSAwake) {
		/*  Read REG_INIDATA_RATE_SEL value for TXDESC. */
		if (check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE)) {
			pdmpriv->INIDATA_RATE[0] = rtl8723au_read8(Adapter, REG_INIDATA_RATE_SEL) & 0x3f;
		} else {
			u8	i;
			for (i = 1 ; i < (Adapter->stapriv.asoc_sta_count + 1); i++)
				pdmpriv->INIDATA_RATE[i] = rtl8723au_read8(Adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
		}
	}

	/* ODM */
	if (rtw_linked_check(Adapter))
		bLinked = true;

	ODM_CmnInfoUpdate23a(&pHalData->odmpriv, ODM_CMNINFO_LINK, bLinked);
	ODM_DMWatchdog23a(pHalData);

skip_dm:

	/*  Check GPIO to determine current RF on/off and Pbc status. */
	/*  Check Hardware Radio ON/OFF or not */
	dm_CheckPbcGPIO(Adapter);
}

void rtl8723a_init_dm_priv(struct rtw_adapter *Adapter)
{
	struct hal_data_8723a *pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	memset(pdmpriv, 0, sizeof(struct dm_priv));
	Init_ODM_ComInfo_8723a(Adapter);
}
