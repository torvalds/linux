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
/*  */
/*  Description: */
/*  */
/*  This file is for 92CE/92CU dynamic mechanism only */
/*  */
/*  */
/*  */
#define _RTL8188E_DM_C_

#include <osdep_service.h>
#include <drv_types.h>

#include <rtl8188e_hal.h>

static void dm_CheckStatistics(struct adapter *Adapter)
{
}

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
static void Init_ODM_ComInfo_88E(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	struct odm_dm_struct *dm_odm = &(hal_data->odmpriv);
	u8 cut_ver, fab_ver;

	/*  Init Value */
	memset(dm_odm, 0, sizeof(*dm_odm));

	dm_odm->Adapter = Adapter;

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_PLATFORM, ODM_CE);

	if (Adapter->interface_type == RTW_GSPI)
		ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_INTERFACE, ODM_ITRF_SDIO);
	else
		ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_INTERFACE, Adapter->interface_type);/* RTL871X_HCI_TYPE */

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_IC_TYPE, ODM_RTL8188E);

	fab_ver = ODM_TSMC;
	cut_ver = ODM_CUT_A;

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_FAB_VER, fab_ver);
	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_CUT_VER, cut_ver);

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_MP_TEST_CHIP, IS_NORMAL_CHIP(hal_data->VersionID));

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_PATCH_ID, hal_data->CustomerID);
	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_BWIFI_TEST, Adapter->registrypriv.wifi_spec);

	if (hal_data->rf_type == RF_1T1R)
		ODM_CmnInfoUpdate(dm_odm, ODM_CMNINFO_RF_TYPE, ODM_1T1R);
	else if (hal_data->rf_type == RF_2T2R)
		ODM_CmnInfoUpdate(dm_odm, ODM_CMNINFO_RF_TYPE, ODM_2T2R);
	else if (hal_data->rf_type == RF_1T2R)
		ODM_CmnInfoUpdate(dm_odm, ODM_CMNINFO_RF_TYPE, ODM_1T2R);

	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_RF_ANTENNA_TYPE, hal_data->TRxAntDivType);

	pdmpriv->InitODMFlag =	ODM_RF_CALIBRATION |
				ODM_RF_TX_PWR_TRACK;

	ODM_CmnInfoUpdate(dm_odm, ODM_CMNINFO_ABILITY, pdmpriv->InitODMFlag);
}

static void Update_ODM_ComInfo_88E(struct adapter *Adapter)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);
	struct odm_dm_struct *dm_odm = &(hal_data->odmpriv);
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	int i;

	pdmpriv->InitODMFlag =	ODM_BB_DIG |
				ODM_BB_RA_MASK |
				ODM_BB_DYNAMIC_TXPWR |
				ODM_BB_FA_CNT |
				ODM_BB_RSSI_MONITOR |
				ODM_BB_CCK_PD |
				ODM_BB_PWR_SAVE |
				ODM_MAC_EDCA_TURBO |
				ODM_RF_CALIBRATION |
				ODM_RF_TX_PWR_TRACK;
	if (hal_data->AntDivCfg)
		pdmpriv->InitODMFlag |= ODM_BB_ANT_DIV;

	if (Adapter->registrypriv.mp_mode == 1) {
		pdmpriv->InitODMFlag =	ODM_RF_CALIBRATION |
					ODM_RF_TX_PWR_TRACK;
	}

	ODM_CmnInfoUpdate(dm_odm, ODM_CMNINFO_ABILITY, pdmpriv->InitODMFlag);

	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_TX_UNI, &(Adapter->xmitpriv.tx_bytes));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_RX_UNI, &(Adapter->recvpriv.rx_bytes));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_WM_MODE, &(pmlmeext->cur_wireless_mode));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_SEC_CHNL_OFFSET, &(hal_data->nCur40MhzPrimeSC));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_SEC_MODE, &(Adapter->securitypriv.dot11PrivacyAlgrthm));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_BW, &(hal_data->CurrentChannelBW));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_CHNL, &(hal_data->CurrentChannel));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_NET_CLOSED, &(Adapter->net_closed));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_MP_MODE, &(Adapter->registrypriv.mp_mode));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_SCAN, &(pmlmepriv->bScanInProcess));
	ODM_CmnInfoHook(dm_odm, ODM_CMNINFO_POWER_SAVING, &(pwrctrlpriv->bpower_saving));
	ODM_CmnInfoInit(dm_odm, ODM_CMNINFO_RF_ANTENNA_TYPE, hal_data->TRxAntDivType);

	for (i = 0; i < NUM_STA; i++)
		ODM_CmnInfoPtrArrayHook(dm_odm, ODM_CMNINFO_STA_STATUS, i, NULL);
}

void rtl8188e_InitHalDm(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	struct odm_dm_struct *dm_odm = &(hal_data->odmpriv);

	dm_InitGPIOSetting(Adapter);
	pdmpriv->DM_Type = DM_Type_ByDriver;
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
	Update_ODM_ComInfo_88E(Adapter);
	ODM_DMInit(dm_odm);
	Adapter->fix_rate = 0xFF;
}

void rtl8188e_HalDmWatchDog(struct adapter *Adapter)
{
	bool fw_cur_in_ps = false;
	bool fw_ps_awake = true;
	u8 hw_init_completed = false;
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);


	hw_init_completed = Adapter->hw_init_completed;

	if (!hw_init_completed)
		goto skip_dm;

	fw_cur_in_ps = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
	rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&fw_ps_awake));

	/*  Fw is under p2p powersaving mode, driver should stop dynamic mechanism. */
	/*  modifed by thomas. 2011.06.11. */
	if (Adapter->wdinfo.p2p_ps_mode)
		fw_ps_awake = false;

	if (hw_init_completed && ((!fw_cur_in_ps) && fw_ps_awake)) {
		/*  Calculate Tx/Rx statistics. */
		dm_CheckStatistics(Adapter);


	}

	/* ODM */
	if (hw_init_completed) {
		struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
		u8 bLinked = false;

		if ((check_fwstate(pmlmepriv, WIFI_AP_STATE)) ||
		    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE | WIFI_ADHOC_MASTER_STATE))) {
			if (Adapter->stapriv.asoc_sta_count > 2)
				bLinked = true;
		} else {/* Station mode */
			if (check_fwstate(pmlmepriv, _FW_LINKED))
				bLinked = true;
		}

		ODM_CmnInfoUpdate(&hal_data->odmpriv, ODM_CMNINFO_LINK, bLinked);
		ODM_DMWatchdog(&hal_data->odmpriv);
	}
skip_dm:
	/*  Check GPIO to determine current RF on/off and Pbc status. */
	/*  Check Hardware Radio ON/OFF or not */
	return;
}

void rtl8188e_init_dm_priv(struct adapter *Adapter)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &hal_data->dmpriv;
	struct odm_dm_struct *podmpriv = &hal_data->odmpriv;

	memset(pdmpriv, 0, sizeof(struct dm_priv));
	Init_ODM_ComInfo_88E(Adapter);
	ODM_InitDebugSetting(podmpriv);
}

void rtl8188e_deinit_dm_priv(struct adapter *Adapter)
{
}

/*  Add new function to reset the state of antenna diversity before link. */
/*  Compare RSSI for deciding antenna */
void AntDivCompare8188E(struct adapter *Adapter, struct wlan_bssid_ex *dst, struct wlan_bssid_ex *src)
{
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);

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
	struct hal_data_8188e *hal_data = GET_HAL_DATA(Adapter);
	struct odm_dm_struct *dm_odm = &hal_data->odmpriv;
	struct sw_ant_switch *dm_swat_tbl = &dm_odm->DM_SWAT_Table;
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);

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
