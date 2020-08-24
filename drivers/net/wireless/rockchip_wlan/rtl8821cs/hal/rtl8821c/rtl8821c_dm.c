/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2017 Realtek Corporation.
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
 *****************************************************************************/
/* ************************************************************
 * Description:
 *
 * This file is for 8821C dynamic mechanism
 *
 *
 * ************************************************************ */
#define _RTL8812C_DM_C_

/* ************************************************************
 * include files
 * ************************************************************
 */

#include <drv_types.h>
#include <rtl8821c_hal.h>

/* ************************************************************
 * Global var
 * ************************************************************ */
#ifdef CONFIG_SUPPORT_HW_WPS_PBC
static void dm_CheckPbcGPIO(PADAPTER adapter)
{
	u8 tmp1byte;
	u8 bPbcPressed = _FALSE;

	if (!adapter->registrypriv.hw_wps_pbc)
		return;
	
#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(adapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IO_SEL, tmp1byte); /* enable GPIO[2] as output mode */

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IN, tmp1byte); /* reset the floating voltage level */

	tmp1byte = rtw_read8(adapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(adapter, GPIO_IO_SEL, tmp1byte); /* enable GPIO[2] as input mode */

	tmp1byte = rtw_read8(adapter, GPIO_IN);
	if (tmp1byte == 0xff)
		return;
	
	if (tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT)
		bPbcPressed = _TRUE;
#else
	tmp1byte = rtw_read8(adapter, GPIO_IN);
	
	if ((tmp1byte == 0xff) || adapter->init_adpt_in_progress)
		return;
	
	if ((tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT) == 0)
		bPbcPressed = _TRUE;
#endif
	
	if (_TRUE == bPbcPressed) {
	/*
	 * Here we only set bPbcPressed to true
	 * After trigger PBC, the variable will be set to false
	 */
		RTW_INFO("CheckPbcGPIO - PBC is pressed\n");
			rtw_request_wps_pbc_event(adapter);
	}
}
#endif /* CONFIG_SUPPORT_HW_WPS_PBC */
	
	
#ifdef CONFIG_PCI_HCI
/*
 * Description:
 *	Perform interrupt migration dynamically to reduce CPU utilization.
 *
 * Assumption:
 *	1. Do not enable migration under WIFI test.
 */
void dm_InterruptMigration(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;
	BOOLEAN bCurrentIntMt, bCurrentACIntDisable;
	BOOLEAN IntMtToSet = _FALSE;
	BOOLEAN ACIntToSet = _FALSE;


	/* Retrieve current interrupt migration and Tx four ACs IMR settings first. */
	bCurrentIntMt = hal->bInterruptMigration;
	bCurrentACIntDisable = hal->bDisableTxInt;

	/*
	 * <Roger_Notes> Currently we use busy traffic for reference instead of RxIntOK counts to prevent non-linear Rx statistics
	 * when interrupt migration is set before. 2010.03.05.
	 */
	if (!adapter->registrypriv.wifi_spec
		&& (check_fwstate(pmlmepriv, WIFI_ASOC_STATE) == _TRUE)
		&& pmlmepriv->LinkDetectInfo.bHigherBusyTraffic) {
		IntMtToSet = _TRUE;

		/* To check whether we should disable Tx interrupt or not. */
		if (pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic)
			ACIntToSet = _TRUE;
	}

	/* Update current settings. */
	if (bCurrentIntMt != IntMtToSet) {
		RTW_INFO("%s: Update interrrupt migration(%d)\n", __FUNCTION__, IntMtToSet);
		if (IntMtToSet) {
			/*
			 * <Roger_Notes> Set interrrupt migration timer and corresponging Tx/Rx counter.
			 * timer 25ns*0xfa0=100us for 0xf packets.
			 * 2010.03.05.
			 */
			rtw_write32(adapter, REG_INT_MIG, 0xff000fa0); /* 0x306:Rx, 0x307:Tx */
			hal->bInterruptMigration = IntMtToSet;
		} else {
			/* Reset all interrupt migration settings. */
			rtw_write32(adapter, REG_INT_MIG, 0);
			hal->bInterruptMigration = IntMtToSet;
		}
	}
}
#endif /* CONFIG_PCI_HCI */
	
/*
 * ============================================================
 * functions
 * ============================================================
 */
static void init_phydm_cominfo(PADAPTER adapter)
{
	PHAL_DATA_TYPE hal = GET_HAL_DATA(adapter);
	struct dm_struct *pDM_Odm = &hal->odmpriv;
	u8 cut_ver = ODM_CUT_A, fab_ver = ODM_TSMC;

	Init_ODM_ComInfo(adapter);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_PACKAGE_TYPE, hal->PackageType);

	if (IS_CHIP_VENDOR_TSMC(hal->version_id))
		fab_ver = ODM_TSMC;
	else if (IS_CHIP_VENDOR_UMC(hal->version_id))
		fab_ver = ODM_UMC;
	else if (IS_CHIP_VENDOR_SMIC(hal->version_id))
		fab_ver = ODM_UMC + 1;
	else
		RTW_INFO("%s: unknown Fv=%d !!\n",
			 __FUNCTION__, GET_CVID_MANUFACTUER(hal->version_id));

	if (IS_A_CUT(hal->version_id))
		cut_ver = ODM_CUT_A;
	else if (IS_B_CUT(hal->version_id))
		cut_ver = ODM_CUT_B;
	else if (IS_C_CUT(hal->version_id))
		cut_ver = ODM_CUT_C;
	else if (IS_D_CUT(hal->version_id))
		cut_ver = ODM_CUT_D;
	else if (IS_E_CUT(hal->version_id))
		cut_ver = ODM_CUT_E;
	else if (IS_F_CUT(hal->version_id))
		cut_ver = ODM_CUT_F;
	else if (IS_I_CUT(hal->version_id))
		cut_ver = ODM_CUT_I;
	else if (IS_J_CUT(hal->version_id))
		cut_ver = ODM_CUT_J;
	else if (IS_K_CUT(hal->version_id))
		cut_ver = ODM_CUT_K;
	else
		RTW_INFO("%s: unknown Cv=%d !!\n",
			 __FUNCTION__, GET_CVID_CUT_VERSION(hal->version_id));

	RTW_INFO("%s: Fv=%d Cv=%d\n", __FUNCTION__, fab_ver, cut_ver);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_FAB_VER, fab_ver);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_CUT_VER, cut_ver);

}
	
void rtl8821c_phy_init_dm_priv(PADAPTER adapter)
{
	struct dm_struct *phydm = adapter_to_phydm(adapter);

	init_phydm_cominfo(adapter);
	odm_init_all_timers(phydm);
}
	
void rtl8821c_phy_deinit_dm_priv(PADAPTER adapter)
{
	struct dm_struct *phydm = adapter_to_phydm(adapter);

	odm_cancel_all_timers(phydm);
}

void rtl8821c_phy_init_haldm(PADAPTER adapter)
{
	rtw_phydm_init(adapter);
}

static void check_rxfifo_full(PADAPTER adapter)
{
	struct dvobj_priv *psdpriv = adapter->dvobj;
	struct debug_priv *pdbgpriv = &psdpriv->drv_dbg;
	struct registry_priv *regsty = &adapter->registrypriv;
	u8 val8 = 0;

	if (regsty->check_hw_status == 1) {
		/* switch counter to RX fifo */
		val8 = rtw_read8(adapter, REG_RXERR_RPT_8821C + 3);
		rtw_write8(adapter, REG_RXERR_RPT_8821C + 3, (val8 | 0xa0));

		pdbgpriv->dbg_rx_fifo_last_overflow = pdbgpriv->dbg_rx_fifo_curr_overflow;
		pdbgpriv->dbg_rx_fifo_curr_overflow = rtw_read16(adapter, REG_RXERR_RPT_8821C);
		if (pdbgpriv->dbg_rx_fifo_curr_overflow >= pdbgpriv->dbg_rx_fifo_last_overflow)
			pdbgpriv->dbg_rx_fifo_diff_overflow =
				pdbgpriv->dbg_rx_fifo_curr_overflow - pdbgpriv->dbg_rx_fifo_last_overflow;
		else
			pdbgpriv->dbg_rx_fifo_diff_overflow =
				(0xFFFF - pdbgpriv->dbg_rx_fifo_last_overflow)
				+ pdbgpriv->dbg_rx_fifo_curr_overflow;

	}
}


void rtl8821c_phy_haldm_watchdog(PADAPTER Adapter)
{
	BOOLEAN bFwCurrentInPSMode = _FALSE;
	u8 bFwPSAwake = _TRUE;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);
	u8 lps_changed = _FALSE;
	u8 in_lps = _FALSE;
	PADAPTER current_lps_iface = NULL, iface = NULL;
	struct dvobj_priv *dvobj = adapter_to_dvobj(Adapter);
	u8 i = 0;

	if (!rtw_is_hw_init_completed(Adapter))
		goto skip_dm;

#ifdef CONFIG_LPS
	bFwCurrentInPSMode = pwrpriv->bFwCurrentInPSMode;
	rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, &bFwPSAwake);
#endif

#ifdef CONFIG_P2P_PS
	/* Fw is under p2p powersaving mode, driver should stop dynamic mechanism.
	 modifed by thomas. 2011.06.11.*/
	if (Adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = _FALSE;
#endif /*CONFIG_P2P_PS*/

	if ((rtw_is_hw_init_completed(Adapter))
		&& ((!bFwCurrentInPSMode) && bFwPSAwake)) {
		
		/* check rx fifo */
		check_rxfifo_full(Adapter);

		/* Dynamically switch RTS/CTS protection.*/
	}

#ifdef CONFIG_LPS
	if (pwrpriv->bLeisurePs && bFwCurrentInPSMode && pwrpriv->pwr_mode != PS_MODE_ACTIVE
#ifdef CONFIG_WMMPS_STA	
		&& !rtw_is_wmmps_mode(Adapter)
#endif /* CONFIG_WMMPS_STA */
	) {
		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (pwrpriv->current_lps_hw_port_id == rtw_hal_get_port(iface))
				current_lps_iface = iface;
		}

		lps_changed = _TRUE;
		in_lps = _TRUE;
		LPS_Leave(current_lps_iface, LPS_CTRL_PHYDM);
	}
#endif

#ifdef CONFIG_BEAMFORMING
#ifdef RTW_BEAMFORMING_VERSION_2
	if (check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) &&
			check_fwstate(&Adapter->mlmepriv, WIFI_ASOC_STATE))
		rtw_hal_beamforming_config_csirate(Adapter);
#endif
#endif

#ifdef CONFIG_DISABLE_ODM
	goto skip_dm;
#endif

	rtw_phydm_watchdog(Adapter, in_lps);

skip_dm:

#ifdef CONFIG_LPS
	if (lps_changed)
		LPS_Enter(current_lps_iface, LPS_CTRL_PHYDM);
#endif
#ifdef CONFIG_SUPPORT_HW_WPS_PBC
	/* Check GPIO to determine current Pbc status.*/
	dm_CheckPbcGPIO(Adapter);
#endif
	return;
}

