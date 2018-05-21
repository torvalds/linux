/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 * This file is for 92CE/92CU dynamic mechanism only
 *
 *
 * ************************************************************ */
#define _RTL8723B_DM_C_

/* ************************************************************
 * include files
 * ************************************************************ */
#include <rtl8723b_hal.h>

/* ************************************************************
 * Global var
 * ************************************************************ */


static VOID
dm_CheckProtection(
	IN	PADAPTER	Adapter
)
{
#if 0
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u1Byte			CurRate, RateThreshold;

	if (pMgntInfo->pHTInfo->bCurBW40MHz)
		RateThreshold = MGN_MCS1;
	else
		RateThreshold = MGN_MCS3;

	if (Adapter->TxStats.CurrentInitTxRate <= RateThreshold) {
		pMgntInfo->bDmDisableProtect = TRUE;
		dbg_print("Forced disable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	} else {
		pMgntInfo->bDmDisableProtect = FALSE;
		dbg_print("Enable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	}
#endif
}

#ifdef CONFIG_SUPPORT_HW_WPS_PBC
static void dm_CheckPbcGPIO(_adapter *padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = _FALSE;

	if (!padapter->registrypriv.hw_wps_pbc)
		return;

#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as output mode */

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter,  GPIO_IN, tmp1byte);		/* reset the floating voltage level */

	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as input mode */

	tmp1byte = rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff)
		return ;

	if (tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT)
		bPbcPressed = _TRUE;
#else
	tmp1byte = rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff || padapter->init_adpt_in_progress)
		return ;

	if ((tmp1byte & HAL_8192C_HW_GPIO_WPS_BIT) == 0)
		bPbcPressed = _TRUE;
#endif

	if (_TRUE == bPbcPressed) {
		/* Here we only set bPbcPressed to true */
		/* After trigger PBC, the variable will be set to false */
		RTW_INFO("CheckPbcGPIO - PBC is pressed\n");
		rtw_request_wps_pbc_event(padapter);
	}
}
#endif /* #ifdef CONFIG_SUPPORT_HW_WPS_PBC */


#ifdef CONFIG_PCI_HCI
/*
 *	Description:
 *		Perform interrupt migration dynamically to reduce CPU utilization.
 *
 *	Assumption:
 *		1. Do not enable migration under WIFI test.
 *
 *	Created by Roger, 2010.03.05.
 *   */
VOID
dm_InterruptMigration(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	BOOLEAN			bCurrentIntMt, bCurrentACIntDisable;
	BOOLEAN			IntMtToSet = _FALSE;
	BOOLEAN			ACIntToSet = _FALSE;


	/* Retrieve current interrupt migration and Tx four ACs IMR settings first. */
	bCurrentIntMt = pHalData->bInterruptMigration;
	bCurrentACIntDisable = pHalData->bDisableTxInt;

	/*  */
	/* <Roger_Notes> Currently we use busy traffic for reference instead of RxIntOK counts to prevent non-linear Rx statistics */
	/* when interrupt migration is set before. 2010.03.05. */
	/*  */
	if (!Adapter->registrypriv.wifi_spec &&
	    (check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) &&
	    pmlmepriv->LinkDetectInfo.bHigherBusyTraffic) {
		IntMtToSet = _TRUE;

		/* To check whether we should disable Tx interrupt or not. */
		if (pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic)
			ACIntToSet = _TRUE;
	}

	/* Update current settings. */
	if (bCurrentIntMt != IntMtToSet) {
		RTW_INFO("%s(): Update interrrupt migration(%d)\n", __FUNCTION__, IntMtToSet);
		if (IntMtToSet) {
			/*  */
			/* <Roger_Notes> Set interrrupt migration timer and corresponging Tx/Rx counter. */
			/* timer 25ns*0xfa0=100us for 0xf packets. */
			/* 2010.03.05. */
			/*  */
			rtw_write32(Adapter, REG_INT_MIG, 0xff000fa0);/* 0x306:Rx, 0x307:Tx */
			pHalData->bInterruptMigration = IntMtToSet;
		} else {
			/* Reset all interrupt migration settings. */
			rtw_write32(Adapter, REG_INT_MIG, 0);
			pHalData->bInterruptMigration = IntMtToSet;
		}
	}

#if 0
	if (bCurrentACIntDisable != ACIntToSet) {
		RTW_INFO("%s(): Update AC interrrupt(%d)\n", __FUNCTION__, ACIntToSet);
		if (ACIntToSet) { /*  Disable four ACs interrupts. */
			/* */
			/*  <Roger_Notes> Disable VO, VI, BE and BK four AC interrupts to gain more efficient CPU utilization. */
			/*  When extremely highly Rx OK occurs, we will disable Tx interrupts. */
			/*  2010.03.05. */
			/* */
			UpdateInterruptMask8192CE(Adapter, 0, RT_AC_INT_MASKS);
			pHalData->bDisableTxInt = ACIntToSet;
		} else { /*  Enable four ACs interrupts. */
			UpdateInterruptMask8192CE(Adapter, RT_AC_INT_MASKS, 0);
			pHalData->bDisableTxInt = ACIntToSet;
		}
	}
#endif

}

#endif

/*
 * Initialize GPIO setting registers
 *   */
static void
dm_InitGPIOSetting(
	IN	PADAPTER	Adapter
)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	u8	tmp1byte;

	tmp1byte = rtw_read8(Adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);

	rtw_write8(Adapter, REG_GPIO_MUXCFG, tmp1byte);
}
/* ************************************************************
 * functions
 * ************************************************************ */
static void Init_ODM_ComInfo_8723b(PADAPTER	Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &(pHalData->odmpriv);
	u8	cut_ver, fab_ver;

	Init_ODM_ComInfo(Adapter);

	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_PACKAGE_TYPE, pHalData->PackageType);

	fab_ver = ODM_TSMC;
	cut_ver = ODM_CUT_A;

	RTW_INFO("%s(): fab_ver=%d cut_ver=%d\n", __func__, fab_ver, cut_ver);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_FAB_VER, fab_ver);
	odm_cmn_info_init(pDM_Odm, ODM_CMNINFO_CUT_VER, cut_ver);
}

void
rtl8723b_InitHalDm(
	IN	PADAPTER	Adapter
)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT		*pDM_Odm = &(pHalData->odmpriv);

#ifdef CONFIG_USB_HCI
	dm_InitGPIOSetting(Adapter);
#endif
	odm_dm_init(pDM_Odm);

}

VOID
rtl8723b_HalDmWatchDog(
	IN	PADAPTER	Adapter
)
{
	BOOLEAN		bFwCurrentInPSMode = _FALSE;
	u8 bFwPSAwake = _TRUE;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);

#ifdef CONFIG_MP_INCLUDED
	/* #if MP_DRIVER */
	if (Adapter->registrypriv.mp_mode == 1 && Adapter->mppriv.mp_dm == 0) /* for MP power tracking */
		return;
	/* #endif */
#endif
	if (!rtw_is_hw_init_completed(Adapter))
		goto skip_dm;

#ifdef CONFIG_LPS
	bFwCurrentInPSMode = adapter_to_pwrctl(Adapter)->bFwCurrentInPSMode;
	rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, &bFwPSAwake);
#endif

#ifdef CONFIG_P2P
	/* Fw is under p2p powersaving mode, driver should stop dynamic mechanism. */
	/* modifed by thomas. 2011.06.11. */
	if (Adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = _FALSE;
#endif /* CONFIG_P2P */


	if ((rtw_is_hw_init_completed(Adapter))
	    && ((!bFwCurrentInPSMode) && bFwPSAwake)) {

		rtw_hal_check_rxfifo_full(Adapter);
		/*  */
		/* Dynamically switch RTS/CTS protection. */
		/*  */
		/* dm_CheckProtection(Adapter); */

#ifdef CONFIG_PCI_HCI
		/* 20100630 Joseph: Disable Interrupt Migration mechanism temporarily because it degrades Rx throughput. */
		/* Tx Migration settings. */
		/* dm_InterruptMigration(Adapter); */

		/* if(Adapter->HalFunc.TxCheckStuckHandler(Adapter)) */
		/*	PlatformScheduleWorkItem(&(GET_HAL_DATA(Adapter)->HalResetWorkItem)); */
#endif
	}

#ifdef CONFIG_DISABLE_ODM
	goto skip_dm;
#endif
	rtw_phydm_watchdog(Adapter);

skip_dm:

	/* Check GPIO to determine current RF on/off and Pbc status. */
	/* Check Hardware Radio ON/OFF or not */
	/* if(Adapter->MgntInfo.PowerSaveControl.bGpioRfSw) */
	/* { */
	/* RTPRINT(FPWR, PWRHW, ("dm_CheckRfCtrlGPIO\n")); */
	/*	dm_CheckRfCtrlGPIO(Adapter); */
	/* } */
#ifdef CONFIG_SUPPORT_HW_WPS_PBC
	dm_CheckPbcGPIO(Adapter);
#endif
	return;
}

void rtl8723b_init_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT		*podmpriv = &pHalData->odmpriv;
	Init_ODM_ComInfo_8723b(Adapter);
	odm_init_all_timers(podmpriv);

}

void rtl8723b_deinit_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct PHY_DM_STRUCT		*podmpriv = &pHalData->odmpriv;

	odm_cancel_all_timers(podmpriv);

}
