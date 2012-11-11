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
//============================================================
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================
#define _RTL8188E_DM_C_

//============================================================
// include files
//============================================================
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <rtl8188e_hal.h>

//============================================================
// Global var
//============================================================


static VOID
dm_CheckProtection(
	IN	PADAPTER	Adapter
	)
{
#if 0
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u1Byte			CurRate, RateThreshold;

	if(pMgntInfo->pHTInfo->bCurBW40MHz)
		RateThreshold = MGN_MCS1;
	else
		RateThreshold = MGN_MCS3;

	if(Adapter->TxStats.CurrentInitTxRate <= RateThreshold)
	{
		pMgntInfo->bDmDisableProtect = TRUE;
		DbgPrint("Forced disable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	}
	else
	{
		pMgntInfo->bDmDisableProtect = FALSE;
		DbgPrint("Enable protect: %x\n", Adapter->TxStats.CurrentInitTxRate);
	}
#endif
}

static VOID
dm_CheckStatistics(
	IN	PADAPTER	Adapter
	)
{
#if 0
	if(!Adapter->MgntInfo.bMediaConnect)
		return;

	//2008.12.10 tynli Add for getting Current_Tx_Rate_Reg flexibly.
	rtw_hal_get_hwreg( Adapter, HW_VAR_INIT_TX_RATE, (pu1Byte)(&Adapter->TxStats.CurrentInitTxRate) );

	// Calculate current Tx Rate(Successful transmited!!)

	// Calculate current Rx Rate(Successful received!!)

	//for tx tx retry count
	rtw_hal_get_hwreg( Adapter, HW_VAR_RETRY_COUNT, (pu1Byte)(&Adapter->TxStats.NumTxRetryCount) );
#endif
}

static void dm_CheckPbcGPIO(_adapter *padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = _FALSE;

	if(!padapter->registrypriv.hw_wps_pbc)
		return;

#ifdef CONFIG_USB_HCI
	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	//enable GPIO[2] as output mode

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter,  GPIO_IN, tmp1byte);		//reset the floating voltage level

	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	//enable GPIO[2] as input mode

	tmp1byte =rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff)
		return ;

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
	{
		bPbcPressed = _TRUE;
	}
#else
	tmp1byte = rtw_read8(padapter, GPIO_IN);
	//RT_TRACE(COMP_IO, DBG_TRACE, ("dm_CheckPbcGPIO - %x\n", tmp1byte));

	if (tmp1byte == 0xff || padapter->init_adpt_in_progress)
		return ;

	if((tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)==0)
	{
		bPbcPressed = _TRUE;
	}
#endif

	if( _TRUE == bPbcPressed)
	{
		// Here we only set bPbcPressed to true
		// After trigger PBC, the variable will be set to false
		DBG_8192C("CheckPbcGPIO - PBC is pressed\n");

#ifdef RTK_DMP_PLATFORM
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,12))
		kobject_uevent(&padapter->pnetdev->dev.kobj, KOBJ_NET_PBC);
#else
		kobject_hotplug(&padapter->pnetdev->class_dev.kobj, KOBJ_NET_PBC);
#endif
#else

		if ( padapter->pid[0] == 0 )
		{	//	0 is the default value and it means the application monitors the HW PBC doesn't privde its pid to driver.
			return;
		}

#ifdef PLATFORM_LINUX
		rtw_signal_process(padapter->pid[0], SIGUSR1);
#endif
#endif
	}
}

#ifdef CONFIG_PCI_HCI
//
//	Description:
//		Perform interrupt migration dynamically to reduce CPU utilization.
//
//	Assumption:
//		1. Do not enable migration under WIFI test.
//
//	Created by Roger, 2010.03.05.
//
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


	// Retrieve current interrupt migration and Tx four ACs IMR settings first.
	bCurrentIntMt = pHalData->bInterruptMigration;
	bCurrentACIntDisable = pHalData->bDisableTxInt;

	//
	// <Roger_Notes> Currently we use busy traffic for reference instead of RxIntOK counts to prevent non-linear Rx statistics
	// when interrupt migration is set before. 2010.03.05.
	//
	if(!Adapter->registrypriv.wifi_spec &&
		(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE) &&
		pmlmepriv->LinkDetectInfo.bHigherBusyTraffic)
	{
		IntMtToSet = _TRUE;

		// To check whether we should disable Tx interrupt or not.
		if(pmlmepriv->LinkDetectInfo.bHigherBusyRxTraffic )
			ACIntToSet = _TRUE;
	}

	//Update current settings.
	if( bCurrentIntMt != IntMtToSet ){
		DBG_8192C("%s(): Update interrrupt migration(%d)\n",__FUNCTION__,IntMtToSet);
		if(IntMtToSet)
		{
			//
			// <Roger_Notes> Set interrrupt migration timer and corresponging Tx/Rx counter.
			// timer 25ns*0xfa0=100us for 0xf packets.
			// 2010.03.05.
			//
			rtw_write32(Adapter, REG_INT_MIG, 0xff000fa0);// 0x306:Rx, 0x307:Tx
			pHalData->bInterruptMigration = IntMtToSet;
		}
		else
		{
			// Reset all interrupt migration settings.
			rtw_write32(Adapter, REG_INT_MIG, 0);
			pHalData->bInterruptMigration = IntMtToSet;
		}
	}

	/*if( bCurrentACIntDisable != ACIntToSet ){
		DBG_8192C("%s(): Update AC interrrupt(%d)\n",__FUNCTION__,ACIntToSet);
		if(ACIntToSet) // Disable four ACs interrupts.
		{
			//
			// <Roger_Notes> Disable VO, VI, BE and BK four AC interrupts to gain more efficient CPU utilization.
			// When extremely highly Rx OK occurs, we will disable Tx interrupts.
			// 2010.03.05.
			//
			UpdateInterruptMask8192CE( Adapter, 0, RT_AC_INT_MASKS );
			pHalData->bDisableTxInt = ACIntToSet;
		}
		else// Enable four ACs interrupts.
		{
			UpdateInterruptMask8192CE( Adapter, RT_AC_INT_MASKS, 0 );
			pHalData->bDisableTxInt = ACIntToSet;
		}
	}*/

}

#endif

//
// Initialize GPIO setting registers
//
static void
dm_InitGPIOSetting(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);

	u8	tmp1byte;

	tmp1byte = rtw_read8(Adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);

#ifdef CONFIG_BT_COEXIST
	// UMB-B cut bug. We need to support the modification.
	if (IS_81xxC_VENDOR_UMC_B_CUT(pHalData->VersionID) &&
		pHalData->bt_coexist.BT_Coexist)
	{
		tmp1byte |= (BIT5);
	}
#endif
	rtw_write8(Adapter, REG_GPIO_MUXCFG, tmp1byte);

}

//============================================================
// functions
//============================================================
static void Init_ODM_ComInfo_88E(PADAPTER	Adapter)
{

	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	u8	cut_ver,fab_ver;
	
	//
	// Init Value
	//
	_rtw_memset(pDM_Odm,0,sizeof(pDM_Odm));
	
	pDM_Odm->Adapter = Adapter;	
	
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_PLATFORM,ODM_CE);

	
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_INTERFACE,Adapter->interface_type);//RTL871X_HCI_TYPE
	
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_IC_TYPE,ODM_RTL8188E);

	fab_ver = ODM_TSMC;
	cut_ver = ODM_CUT_A;	

	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_FAB_VER,fab_ver);		
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_CUT_VER,cut_ver);

	ODM_CmnInfoInit(pDM_Odm,	ODM_CMNINFO_MP_TEST_CHIP,IS_NORMAL_CHIP(pHalData->VersionID));
	
#if 0	
//#ifdef CONFIG_USB_HCI	
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_BOARD_TYPE,pHalData->BoardType);

	if(pHalData->BoardType == BOARD_USB_High_PA){
		ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_EXT_LNA,_TRUE);
		ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_EXT_PA,_TRUE);
	}
#endif	
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_PATCH_ID,pHalData->CustomerID);
	//	ODM_CMNINFO_BINHCT_TEST only for MP Team
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_BWIFI_TEST,Adapter->registrypriv.wifi_spec);
		
	
	if(pHalData->rf_type == RF_1T1R){
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_1T1R);		
	}
	else if(pHalData->rf_type == RF_2T2R){
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_2T2R);		
	}
	else if(pHalData->rf_type == RF_1T2R){		
		ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_RF_TYPE,ODM_1T2R);		
	}	

 	ODM_CmnInfoInit(pDM_Odm, ODM_CMNINFO_RF_ANTENNA_TYPE, pHalData->TRxAntDivType);
	
	#ifdef CONFIG_DISABLE_ODM
	pdmpriv->InitODMFlag = 0;
	#else
	pdmpriv->InitODMFlag =	ODM_RF_CALIBRATION		|
							ODM_RF_TX_PWR_TRACK	//|
							;	
	//if(pHalData->AntDivCfg)
	//	pdmpriv->InitODMFlag |= ODM_BB_ANT_DIV;
	#endif	

	ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_ABILITY,pdmpriv->InitODMFlag);
	
}
static void Update_ODM_ComInfo_88E(PADAPTER	Adapter)
{
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	struct pwrctrl_priv *pwrctrlpriv = &Adapter->pwrctrlpriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;	
	int i;	
	#ifdef CONFIG_DISABLE_ODM
	pdmpriv->InitODMFlag = 0;
	#else //CONFIG_DISABLE_ODM
	
	pdmpriv->InitODMFlag =	ODM_BB_DIG				|
#ifdef	CONFIG_ODM_REFRESH_RAMASK
							ODM_BB_RA_MASK		|
#endif							
							ODM_BB_DYNAMIC_TXPWR	|
							ODM_BB_FA_CNT			|
							ODM_BB_RSSI_MONITOR	|
							ODM_BB_CCK_PD			|							
							ODM_BB_PWR_SAVE		|							
							ODM_MAC_EDCA_TURBO	|
							ODM_RF_CALIBRATION		|
							ODM_RF_TX_PWR_TRACK	
							;	
	if(pHalData->AntDivCfg)
		pdmpriv->InitODMFlag |= ODM_BB_ANT_DIV;

	#if (MP_DRIVER==1)
		pdmpriv->InitODMFlag = 	ODM_RF_CALIBRATION	|
								ODM_RF_TX_PWR_TRACK;	
	#endif//(MP_DRIVER==1)
	
	#endif//CONFIG_DISABLE_ODM	
	ODM_CmnInfoUpdate(pDM_Odm,ODM_CMNINFO_ABILITY,pdmpriv->InitODMFlag);
	
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_TX_UNI,&(Adapter->xmitpriv.tx_bytes));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_RX_UNI,&(Adapter->recvpriv.rx_bytes));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_WM_MODE,&(pmlmeext->cur_wireless_mode));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SEC_CHNL_OFFSET,&(pHalData->nCur40MhzPrimeSC));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SEC_MODE,&(Adapter->securitypriv.dot11PrivacyAlgrthm));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BW,&(pHalData->CurrentChannelBW ));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_CHNL,&( pHalData->CurrentChannel));	
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_NET_CLOSED,&( Adapter->net_closed));

	//================= only for 8192D   =================
	/*
	//pHalData->CurrentBandType92D
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BAND,&(pDM_Odm->u1Byte_temp));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_DMSP_GET_VALUE,&(pDM_Odm->u1Byte_temp));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BUDDY_ADAPTOR,&(pDM_Odm->PADAPTER_temp));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_DMSP_IS_MASTER,&(pDM_Odm->u1Byte_temp));
	//================= only for 8192D   =================
	// driver havn't those variable now
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BT_OPERATION,&(pDM_Odm->u1Byte_temp));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_BT_DISABLE_EDCA,&(pDM_Odm->u1Byte_temp));
	*/
	
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_SCAN,&(pmlmepriv->bScanInProcess));
	ODM_CmnInfoHook(pDM_Odm,ODM_CMNINFO_POWER_SAVING,&(pwrctrlpriv->bpower_saving));
	ODM_CmnInfoInit(pDM_Odm,ODM_CMNINFO_RF_ANTENNA_TYPE,NO_ANTDIV);

	for(i=0; i< NUM_STA; i++)
	{
		//pDM_Odm->pODM_StaInfo[i] = NULL;
		ODM_CmnInfoPtrArrayHook(pDM_Odm, ODM_CMNINFO_STA_STATUS,i,NULL);
	}	
}

void
rtl8188e_InitHalDm(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
	u8	i;

#ifdef CONFIG_USB_HCI
	dm_InitGPIOSetting(Adapter);
#endif

	pdmpriv->DM_Type = DM_Type_ByDriver;
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
	
	Update_ODM_ComInfo_88E(Adapter);
	ODM_DMInit(pDM_Odm);

	Adapter->fix_rate = 0xFF;

}

static void
FindMinimumRSSI_88e(
IN	PADAPTER	pAdapter
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &pAdapter->mlmepriv;

	//1 1.Determine the minimum RSSI 


#ifdef CONFIG_CONCURRENT_MODE
	//	FindMinimumRSSI()	per-adapter	
	{		
		PADAPTER pbuddy_adapter = pAdapter->pbuddy_adapter;
		PHAL_DATA_TYPE	pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);
		struct dm_priv *pbuddy_dmpriv = &pbuddy_HalData->dmpriv;
	
		if((pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0) &&
                  (pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB != 0))
      		{

			if(pdmpriv->EntryMinUndecoratedSmoothedPWDB > pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB)
				pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB;
             }
		else                         
		{
			if(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0)
			      pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB;
			       
		}
 		#if 0
		if((pdmpriv->UndecoratedSmoothedPWDB != (-1)) &&
			 (pbuddy_dmpriv->UndecoratedSmoothedPWDB != (-1)))
		{
			
			if((pdmpriv->UndecoratedSmoothedPWDB > pbuddy_dmpriv->UndecoratedSmoothedPWDB) &&
				(pbuddy_dmpriv->UndecoratedSmoothedPWDB!=0))
			            pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
		}
		else                         
		{
			if((pdmpriv->UndecoratedSmoothedPWDB == (-1)) && (pbuddy_dmpriv->UndecoratedSmoothedPWDB!=0))
			      pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;                               
		}                      
		#endif
	}
#endif

	if((check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->MinUndecoratedPWDBForDM = 0;
		//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any \n"));
	}
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
		#if 0
		if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("AP Client PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
		}
		else//for STA mode
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->UndecoratedSmoothedPWDB;
			//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
		}
		#else
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		#endif
	}
	else // associated entry pwdb
	{	
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//ODM_RT_TRACE(pDM_Odm,COMP_BB_POWERSAVING, DBG_LOUD, ("AP Ext Port or disconnet PWDB = 0x%x \n", pHalData->MinUndecoratedPWDBForDM));
	}

	//FindMinimumRSSI_Dmsp(pAdapter);
	//DBG_8192C("%s=>MinUndecoratedPWDBForDM(%d)\n",__FUNCTION__,pdmpriv->MinUndecoratedPWDBForDM);
	//ODM_RT_TRACE(pDM_Odm,COMP_DIG, DBG_LOUD, ("MinUndecoratedPWDBForDM =%d\n",pHalData->MinUndecoratedPWDBForDM));
}

VOID
rtl8188e_HalDmWatchDog(
	IN	PADAPTER	Adapter
	)
{
	BOOLEAN		bFwCurrentInPSMode = _FALSE;
	BOOLEAN		bFwPSAwake = _TRUE;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif //CONFIG_CONCURRENT_MODE

	_func_enter_;
#ifdef CONFIG_LPS
	bFwCurrentInPSMode = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
	rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
#endif

#ifdef CONFIG_P2P
	// Fw is under p2p powersaving mode, driver should stop dynamic mechanism.
	// modifed by thomas. 2011.06.11.
	if(Adapter->wdinfo.p2p_ps_enable)
		bFwPSAwake = _FALSE;
#endif //CONFIG_P2P

	if( (Adapter->hw_init_completed == _TRUE)
		&& ((!bFwCurrentInPSMode) && bFwPSAwake))
	{
#ifdef CONFIG_CONCURRENT_MODE
		if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE) &&
				check_fwstate(&pbuddy_adapter->mlmepriv, _FW_LINKED))
		{
			if(Adapter->iface_type == IFACE_PORT1)
			{
				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));
				//BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(3));
			}
			else if(Adapter->iface_type == IFACE_PORT0)
			{
				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));
				//BCN0 TSF will sync to BCN1 TSF with offset(0x518) if if2_sta linked
				rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(2));
			}
			else
				DBG_8192C("Error Condition\n");
		}
#endif
		//
		// Calculate Tx/Rx statistics.
		//
		dm_CheckStatistics(Adapter);
	
	
#ifdef CONFIG_CONCURRENT_MODE
		if(Adapter->adapter_type > PRIMARY_ADAPTER)
			goto _record_initrate;
#endif
	
		//
		// Dynamically switch RTS/CTS protection.
		//
		//dm_CheckProtection(Adapter);

#ifdef CONFIG_PCI_HCI
		// 20100630 Joseph: Disable Interrupt Migration mechanism temporarily because it degrades Rx throughput.
		// Tx Migration settings.
		//dm_InterruptMigration(Adapter);

		//if(Adapter->HalFunc.TxCheckStuckHandler(Adapter))
		//	PlatformScheduleWorkItem(&(GET_HAL_DATA(Adapter)->HalResetWorkItem));
#endif
		_record_initrate:
	_func_exit_;	
	}


	//ODM
	if (Adapter->hw_init_completed == _TRUE)
	{
		struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
		u8	bLinked=_FALSE;
		#ifdef CONFIG_DISABLE_ODM
		pHalData->odmpriv.SupportAbility = 0;
		#endif
			
		if(	(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE))
		{				
			if(Adapter->stapriv.asoc_sta_count > 2)
				bLinked = _TRUE;
		}
		else{//Station mode
			if(check_fwstate(pmlmepriv, _FW_LINKED)== _TRUE)
				bLinked = _TRUE;
		}

		ODM_CmnInfoUpdate(&pHalData->odmpriv ,ODM_CMNINFO_LINK, bLinked);

		FindMinimumRSSI_88e(Adapter);
		ODM_CmnInfoUpdate(&pHalData->odmpriv ,ODM_CMNINFO_RSSI_MIN, pdmpriv->MinUndecoratedPWDBForDM);
				
		ODM_DMWatchdog(&pHalData->odmpriv);
			
	}

	

	// Check GPIO to determine current RF on/off and Pbc status.
	// Check Hardware Radio ON/OFF or not
#ifdef CONFIG_PCI_HCI
	if(pHalData->bGpioHwWpsPbc)
#endif
	{
		//temp removed
		//dm_CheckPbcGPIO(Adapter);				
	}
	return;
}

void rtl8188e_init_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T 		podmpriv = &pHalData->odmpriv;
	_rtw_memset(pdmpriv, 0, sizeof(struct dm_priv));
	//_rtw_spinlock_init(&(pHalData->odm_stainfo_lock));
	Init_ODM_ComInfo_88E(Adapter);
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	//_init_timer(&(pdmpriv->SwAntennaSwitchTimer),  Adapter->pnetdev , odm_SW_AntennaSwitchCallback, Adapter);	
	ODM_InitAllTimers(podmpriv );	
#endif
	ODM_InitDebugSetting(podmpriv);	
}

void rtl8188e_deinit_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PDM_ODM_T 		podmpriv = &pHalData->odmpriv;
	//_rtw_spinlock_free(&pHalData->odm_stainfo_lock);
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	//_cancel_timer_ex(&pdmpriv->SwAntennaSwitchTimer);	
	ODM_CancelAllTimers(podmpriv);	
#endif
}


#ifdef CONFIG_ANTENNA_DIVERSITY
// Add new function to reset the state of antenna diversity before link.
//
// Compare RSSI for deciding antenna
void	AntDivCompare8188E(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	//PADAPTER Adapter = pDM_Odm->Adapter ;
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	if(0 != pHalData->AntDivCfg )
	{
		//DBG_8192C("update_network=> orgRSSI(%d)(%d),newRSSI(%d)(%d)\n",dst->Rssi,query_rx_pwr_percentage(dst->Rssi),
		//	src->Rssi,query_rx_pwr_percentage(src->Rssi));
		//select optimum_antenna for before linked =>For antenna diversity
		if(dst->Rssi >=  src->Rssi )//keep org parameter
		{
			src->Rssi = dst->Rssi;
			src->PhyInfo.Optimum_antenna = dst->PhyInfo.Optimum_antenna;						
		}
	}
}

// Add new function to reset the state of antenna diversity before link.
u8 AntDivBeforeLink8188E(PADAPTER Adapter )
{
	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	PDM_ODM_T 	pDM_Odm =&pHalData->odmpriv;
	SWAT_T		*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	
	// Condition that does not need to use antenna diversity.
	if(pHalData->AntDivCfg==0)
	{
		//DBG_8192C("odm_AntDivBeforeLink8192C(): No AntDiv Mechanism.\n");
		return _FALSE;
	}

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	
	{		
		return _FALSE;
	}


	if(pDM_SWAT_Table->SWAS_NoLink_State == 0){
		//switch channel
		pDM_SWAT_Table->SWAS_NoLink_State = 1;
		pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==Antenna_A)?Antenna_B:Antenna_A;

		//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, pDM_SWAT_Table->CurAntenna);
		rtw_antenna_select_cmd(Adapter, pDM_SWAT_Table->CurAntenna, _FALSE);
		//DBG_8192C("%s change antenna to ANT_( %s ).....\n",__FUNCTION__, (pDM_SWAT_Table->CurAntenna==Antenna_A)?"A":"B");
		return _TRUE;
	}
	else
	{
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		return _FALSE;
	}	

}
#endif

