// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

/*  This file is for 92CE/92CU dynamic mechanism only */
#define _RTL8188E_DM_C_

#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/rtl8188e_hal.h"

/*  Initialize GPIO setting registers */
static void dm_InitGPIOSetting(struct adapter *Adapter)
{
	u8	tmp1byte;

	tmp1byte = rtw_read8(Adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);

	rtw_write8(Adapter, REG_GPIO_MUXCFG, tmp1byte);
}

/*  */
/*  functions */
/*  */
static void Update_ODM_ComInfo_88E(struct adapter *Adapter)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct hal_data_8188e *hal_data = &Adapter->haldata;
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	int i;

	pdmpriv->InitODMFlag = ODM_BB_RSSI_MONITOR;
	if (hal_data->AntDivCfg)
		pdmpriv->InitODMFlag |= ODM_BB_ANT_DIV;

	dm_odm->SupportAbility = pdmpriv->InitODMFlag;

	dm_odm->pWirelessMode = &pmlmeext->cur_wireless_mode;
	dm_odm->pSecChOffset = &hal_data->nCur40MhzPrimeSC;
	dm_odm->pBandWidth = &hal_data->CurrentChannelBW;
	dm_odm->pChannel = &hal_data->CurrentChannel;
	dm_odm->pbScanInProcess = &pmlmepriv->bScanInProcess;
	dm_odm->pbPowerSaving = &pwrctrlpriv->bpower_saving;

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_RF_ANTENNA_TYPE, hal_data->TRxAntDivType);

	for (i = 0; i < NUM_STA; i++)
		dm_odm->pODM_StaInfo[i] = NULL;
}

void rtl8188e_InitHalDm(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = &Adapter->haldata;
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;

	dm_InitGPIOSetting(Adapter);
	Update_ODM_ComInfo_88E(Adapter);
	ODM_DMInit(dm_odm);
}

void rtl8188e_HalDmWatchDog(struct adapter *Adapter)
{
	u8 hw_init_completed = Adapter->hw_init_completed;
	struct hal_data_8188e *hal_data = &Adapter->haldata;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
	u8 bLinked = false;

	if (!hw_init_completed)
		return;

	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE)) ||
	    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))) {
		if (Adapter->stapriv.asoc_sta_count > 2)
			bLinked = true;
	} else {/* Station mode */
		if (check_fwstate(pmlmepriv, _FW_LINKED))
			bLinked = true;
	}

	hal_data->odmpriv.bLinked = bLinked;
	ODM_DMWatchdog(&hal_data->odmpriv);
}

void rtl8188e_init_dm_priv(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = &Adapter->haldata;
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;

	memset(pdmpriv, 0, sizeof(struct dm_priv));
	memset(dm_odm, 0, sizeof(*dm_odm));

	dm_odm->Adapter = Adapter;
	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(hal_data->VersionID));
	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_RF_ANTENNA_TYPE, hal_data->TRxAntDivType);
}

/*  Add new function to reset the state of antenna diversity before link. */
/*  Compare RSSI for deciding antenna */
void AntDivCompare8188E(struct adapter *Adapter, struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src)
{
	struct hal_data_8188e *hal_data = &Adapter->haldata;

	if (0 != hal_data->AntDivCfg) {
		/* select optimum_antenna for before linked =>For antenna diversity */
		if (dst->Rssi >=  src->Rssi) {/* keep org parameter */
			src->Rssi = dst->Rssi;
			src->PhyInfo.Optimum_antenna = dst->PhyInfo.Optimum_antenna;
		}
	}
}

/*  Add new function to reset the state of antenna diversity before link. */
u8 AntDivBeforeLink8188E(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = &Adapter->haldata;
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;
	struct sw_ant_switch *dm_swat_tbl = &dm_odm->DM_SWAT_Table;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	/*  Condition that does not need to use antenna diversity. */
	if (hal_data->AntDivCfg == 0)
		return false;

	if (check_fwstate(pmlmepriv, _FW_LINKED))
		return false;

	if (dm_swat_tbl->SWAS_NoLink_State == 0) {
		/* switch channel */
		dm_swat_tbl->SWAS_NoLink_State = 1;
		dm_swat_tbl->CurAntenna = (dm_swat_tbl->CurAntenna == Antenna_A) ? Antenna_B : Antenna_A;

		rtw_antenna_select_cmd(Adapter, dm_swat_tbl->CurAntenna, false);
		return true;
	} else {
		dm_swat_tbl->SWAS_NoLink_State = 0;
		return false;
	}
}
