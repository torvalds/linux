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

//============================================================
// include files
//============================================================
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>

#include <rtl8192c_hal.h>
#include "../dm.h"
#ifdef CONFIG_INTEL_PROXIM
#include "../proxim/intel_proxim.h"	
#endif
//============================================================
// Global var
//============================================================
static u32 EDCAParam[maxAP][3] =
{          // UL			DL
	{0x5ea322, 0x00a630, 0x00a44f}, //atheros AP
	{0x5ea32b, 0x5ea42b, 0x5e4322}, //broadcom AP
	{0x3ea430, 0x00a630, 0x3ea44f}, //cisco AP
	{0x5ea44f, 0x00a44f, 0x5ea42b}, //marvell AP
	{0x5ea422, 0x00a44f, 0x00a44f}, //ralink AP
	//{0x5ea44f, 0x5ea44f, 0x5ea44f}, //realtek AP
	{0xa44f, 0x5ea44f, 0x5e431c}, //realtek AP
	{0x5ea42b, 0xa630, 0x5e431c}, //airgocap AP
	{0x5ea42b, 0x5ea42b, 0x5ea42b}, //unknown AP
//	{0x5e4322, 0x00a44f, 0x5ea44f}, //unknown AP
};


/*-----------------------------------------------------------------------------
 * Function:	dm_DIGInit()
 *
 * Overview:	Set DIG scheme init value.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *
 *---------------------------------------------------------------------------*/
static void	dm_DIGInit(
	IN	PADAPTER	pAdapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	

	pDigTable->Dig_Enable_Flag = _TRUE;
	pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_MAX;
	
	pDigTable->CurIGValue = 0x20;
	pDigTable->PreIGValue = 0x0;

	pDigTable->CurSTAConnectState = pDigTable->PreSTAConnectState = DIG_STA_DISCONNECT;
	pDigTable->CurMultiSTAConnectState = DIG_MultiSTA_DISCONNECT;

	pDigTable->RssiLowThresh 	= DM_DIG_THRESH_LOW;
	pDigTable->RssiHighThresh 	= DM_DIG_THRESH_HIGH;

	pDigTable->FALowThresh	= DM_FALSEALARM_THRESH_LOW;
	pDigTable->FAHighThresh	= DM_FALSEALARM_THRESH_HIGH;

	
	pDigTable->rx_gain_range_max = DM_DIG_MAX;
	pDigTable->rx_gain_range_min = DM_DIG_MIN;
	pDigTable->rx_gain_range_min_nolink = 0;

	pDigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;
	pDigTable->BackoffVal_range_max = DM_DIG_BACKOFF_MAX;
	pDigTable->BackoffVal_range_min = DM_DIG_BACKOFF_MIN;

	pDigTable->PreCCKPDState = CCK_PD_STAGE_MAX;
	pDigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;

	pDigTable->ForbiddenIGI = DM_DIG_MIN;
	pDigTable->LargeFAHit = 0;
	pDigTable->Recover_cnt = 0;
	pdmpriv->DIG_Dynamic_MIN  = 0x25; //for FUNAI_TV
}


static u8 dm_initial_gain_MinPWDB(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T			*pDigTable = &pdmpriv->DM_DigTable;
	int	Rssi_val_min = 0;
	
	if((pDigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT) &&
		(pDigTable->CurSTAConnectState == DIG_STA_CONNECT) )
	{
		if(pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0)
#ifdef CONFIG_CONCURRENT_MODE
			Rssi_val_min  =  (pdmpriv->UndecoratedSmoothedPWDB+pdmpriv->EntryMinUndecoratedSmoothedPWDB)/2;
#else
			Rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;		
#endif //CONFIG_CONCURRENT_MODE
		else
			Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	}
	else if(pDigTable->CurSTAConnectState == DIG_STA_CONNECT || 
			pDigTable->CurSTAConnectState == DIG_STA_BEFORE_CONNECT) 
		Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	else if(pDigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT)
		Rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;

	//printk("%s CurMultiSTAConnectState(0x%02x) UndecoratedSmoothedPWDB(%d),EntryMinUndecoratedSmoothedPWDB(%d)\n"
	//,__FUNCTION__,pDigTable->CurSTAConnectState,
	//pdmpriv->UndecoratedSmoothedPWDB,pdmpriv->EntryMinUndecoratedSmoothedPWDB);

	return (u8)Rssi_val_min;
}


static VOID 
dm_FalseAlarmCounterStatistics(
	IN	PADAPTER	Adapter
	)
{
	u32 ret_value;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif //CONFIG_CONCURRENT_MODE
	
	ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter1, bMaskDWord);
       FalseAlmCnt->Cnt_Parity_Fail = ((ret_value&0xffff0000)>>16);	

       ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter2, bMaskDWord);
	FalseAlmCnt->Cnt_Rate_Illegal = (ret_value&0xffff);
	FalseAlmCnt->Cnt_Crc8_fail = ((ret_value&0xffff0000)>>16);
	ret_value = PHY_QueryBBReg(Adapter, rOFDM_PHYCounter3, bMaskDWord);
	FalseAlmCnt->Cnt_Mcs_fail = (ret_value&0xffff);
	ret_value = PHY_QueryBBReg(Adapter, rOFDM0_FrameSync, bMaskDWord);
	FalseAlmCnt->Cnt_Fast_Fsync = (ret_value&0xffff);
	FalseAlmCnt->Cnt_SB_Search_fail = ((ret_value&0xffff0000)>>16);

	FalseAlmCnt->Cnt_Ofdm_fail = 	FalseAlmCnt->Cnt_Parity_Fail + FalseAlmCnt->Cnt_Rate_Illegal +
								FalseAlmCnt->Cnt_Crc8_fail + FalseAlmCnt->Cnt_Mcs_fail+
								FalseAlmCnt->Cnt_Fast_Fsync + FalseAlmCnt->Cnt_SB_Search_fail;

	
	//hold cck counter
	PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, BIT(14), 1);
	
	ret_value = PHY_QueryBBReg(Adapter, rCCK0_FACounterLower, bMaskByte0);
	FalseAlmCnt->Cnt_Cck_fail = ret_value;

	ret_value = PHY_QueryBBReg(Adapter, rCCK0_FACounterUpper, bMaskByte3);
	FalseAlmCnt->Cnt_Cck_fail +=  (ret_value& 0xff)<<8;
	
	FalseAlmCnt->Cnt_all = (	FalseAlmCnt->Cnt_Parity_Fail +
						FalseAlmCnt->Cnt_Rate_Illegal +
						FalseAlmCnt->Cnt_Crc8_fail +
						FalseAlmCnt->Cnt_Mcs_fail +
						FalseAlmCnt->Cnt_Cck_fail);	

	Adapter->recvpriv.FalseAlmCnt_all = FalseAlmCnt->Cnt_all;
#ifdef CONFIG_CONCURRENT_MODE
	if(pbuddy_adapter)
		pbuddy_adapter->recvpriv.FalseAlmCnt_all = FalseAlmCnt->Cnt_all;
#endif //CONFIG_CONCURRENT_MODE

	//reset false alarm counter registers
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0x08000000, 1);
	PHY_SetBBReg(Adapter, rOFDM1_LSTF, 0x08000000, 0);
	//reset cck counter
	PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, 0x0000c000, 0);
	//enable cck counter
	PHY_SetBBReg(Adapter, rCCK0_FalseAlarmReport, 0x0000c000, 2);

	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Parity_Fail = %ld, Cnt_Rate_Illegal = %ld, Cnt_Crc8_fail = %ld, Cnt_Mcs_fail = %ld\n", 
	//			FalseAlmCnt->Cnt_Parity_Fail, FalseAlmCnt->Cnt_Rate_Illegal, FalseAlmCnt->Cnt_Crc8_fail, FalseAlmCnt->Cnt_Mcs_fail) );
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Ofdm_fail = %ld, Cnt_Cck_fail = %ld, Cnt_all = %ld\n", 
	//			FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_all) );
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("Cnt_Ofdm_fail = %ld, Cnt_Cck_fail = %ld, Cnt_all = %ld\n", 
	//			FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_all) );
}


static VOID
DM_Write_DIG(
	IN	PADAPTER	pAdapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	
#ifdef CONFIG_CONCURRENT_MODE
	if(rtw_buddy_adapter_up(pAdapter))
	{
		PADAPTER pbuddy_adapter = pAdapter->pbuddy_adapter;
		PHAL_DATA_TYPE	pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);
		struct dm_priv *pbuddy_dmpriv = &pbuddy_HalData->dmpriv;
		DIG_T	*pbuddy_DigTable = &pbuddy_dmpriv->DM_DigTable;

		//sync IGValue
		pbuddy_DigTable->PreIGValue = pDigTable->PreIGValue;
		pbuddy_DigTable->CurIGValue = pDigTable->CurIGValue;
	}
#endif //CONFIG_CONCURRENT_MODE	


	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CurIGValue = 0x%lx, PreIGValue = 0x%lx, BackoffVal = %d\n", 
	//			DM_DigTable.CurIGValue, DM_DigTable.PreIGValue, DM_DigTable.BackoffVal));

	if (pDigTable->Dig_Enable_Flag == _FALSE)
	{
		//RT_TRACE(	COMP_DIG, DBG_LOUD, ("DIG is disabled\n"));
		pDigTable->PreIGValue = 0x17;
		return;
	}

	if( (pDigTable->PreIGValue != pDigTable->CurIGValue) || ( pAdapter->bForceWriteInitGain ) )
	{
		// Set initial gain.
		//PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0, pDigTable->CurIGValue);
		//PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, bMaskByte0, pDigTable->CurIGValue);
		//printk("%s DIG(0x%02x)\n",__FUNCTION__,pDigTable->CurIGValue);

#if defined CONFIG_WIDI_DIG_3E && defined CONFIG_INTEL_WIDI
		if( pAdapter->mlmepriv.widi_enable == _TRUE )
		{
			PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, 0x3e);
			PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, 0x3e);
		}
		else
#endif //defined CONFIG_WIDI_DIG_3E && defined CONFIG_INTEL_WIDI
		{
			PHY_SetBBReg(pAdapter, rOFDM0_XAAGCCore1, 0x7f, pDigTable->CurIGValue);
			PHY_SetBBReg(pAdapter, rOFDM0_XBAGCCore1, 0x7f, pDigTable->CurIGValue);
		}
		pDigTable->PreIGValue = pDigTable->CurIGValue;
	}
}


static VOID 
dm_CtrlInitGainByFA(
	IN	PADAPTER	pAdapter
)	
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);

	u8	value_IGI = pDigTable->CurIGValue;
	
	if(FalseAlmCnt->Cnt_all < DM_DIG_FA_TH0)	
		value_IGI --;
	else if(FalseAlmCnt->Cnt_all < DM_DIG_FA_TH1)	
		value_IGI += 0;
	else if(FalseAlmCnt->Cnt_all < DM_DIG_FA_TH2)	
		value_IGI ++;
	else if(FalseAlmCnt->Cnt_all >= DM_DIG_FA_TH2)	
		value_IGI +=2;
	
	if(value_IGI > DM_DIG_FA_UPPER)			
		value_IGI = DM_DIG_FA_UPPER;
	if(value_IGI < DM_DIG_FA_LOWER)
		value_IGI = DM_DIG_FA_LOWER;

	if(FalseAlmCnt->Cnt_all > 10000)
		value_IGI = DM_DIG_FA_UPPER;
	
	pDigTable->CurIGValue = value_IGI;
	
	DM_Write_DIG(pAdapter);
	
}

#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
VOID dm_CtrlInitGainByRssi( IN PADAPTER pAdapter) 
{ 

	u32 isBT;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
#ifdef CONFIG_DM_ADAPTIVITY
	u8 Adap_IGI_Upper = pdmpriv->IGI_target + 30 + (u8) pdmpriv->TH_L2H_ini -(u8) pdmpriv->TH_EDCCA_HL_diff;
#endif

	 //modify DIG upper bound
	if((pDigTable->Rssi_val_min + 20) > DM_DIG_MAX )
		pDigTable->rx_gain_range_max = DM_DIG_MAX;
 	else
  		pDigTable->rx_gain_range_max = pDigTable->Rssi_val_min + 20;

	//modify DIG lower bound	
	if((FalseAlmCnt->Cnt_all > 500)&&(pdmpriv->DIG_Dynamic_MIN < 0x25))
  		pdmpriv->DIG_Dynamic_MIN++;
 	if((FalseAlmCnt->Cnt_all < 500)&&(pdmpriv->DIG_Dynamic_MIN > DM_DIG_MIN))
  		pdmpriv->DIG_Dynamic_MIN--;
	if((pDigTable->Rssi_val_min < 8) && (pdmpriv->DIG_Dynamic_MIN > DM_DIG_MIN))
		pdmpriv->DIG_Dynamic_MIN--;
	
 	//modify DIG lower bound, deal with abnorally large false alarm
 	if(FalseAlmCnt->Cnt_all > 10000)
 	{
 		//RT_TRACE(COMP_DIG, DBG_LOUD, ("dm_DIG(): Abnornally false alarm case. \n"));
  		pDigTable->LargeFAHit++;
 		 if(pDigTable->ForbiddenIGI < pDigTable->CurIGValue)
  		{
   			pDigTable->ForbiddenIGI = pDigTable->CurIGValue;
	  		pDigTable->LargeFAHit = 1;
	  	}
	  	if(pDigTable->LargeFAHit >= 3)
	  	{
			if((pDigTable->ForbiddenIGI+1) >pDigTable->rx_gain_range_max)
		    		pDigTable->rx_gain_range_min = pDigTable->rx_gain_range_max;
		   	else
		    		pDigTable->rx_gain_range_min = (pDigTable->ForbiddenIGI + 1);
		   	pDigTable->Recover_cnt = 3600; //3600=2hr
		  }
	 }
	 else
	 {
	  //Recovery mechanism for IGI lower bound
	  	if(pDigTable->Recover_cnt != 0){
	   		pDigTable->Recover_cnt --;
	  	}
	  	else
	 	{
			if(pDigTable->LargeFAHit == 0 )
	   		{
	   			if((pDigTable->ForbiddenIGI -1) < pdmpriv->DIG_Dynamic_MIN) //DM_DIG_MIN)
			    	{
			     		pDigTable->ForbiddenIGI = pdmpriv->DIG_Dynamic_MIN; //DM_DIG_MIN;
			     		pDigTable->rx_gain_range_min = pdmpriv->DIG_Dynamic_MIN; //DM_DIG_MIN;
			    	}
			    	else
			    	{
			     		pDigTable->ForbiddenIGI --;
			     		pDigTable->rx_gain_range_min = (pDigTable->ForbiddenIGI + 1);
			    	}
	   		}
	   		else if(pDigTable->LargeFAHit == 3 )
	   		{
	    			pDigTable->LargeFAHit = 0;
	   		}
	  	}
	 }
 #ifdef CONFIG_USB_HCI
	if(FalseAlmCnt->Cnt_all < 250)
	{
#endif
		//DBG_8192C("===> dm_CtrlInitGainByRssi, Enter DIG by SS mode\n");
		
		isBT = rtw_read8(pAdapter, 0x4fd) & 0x01;

		if(!isBT){

			if(FalseAlmCnt->Cnt_all > pDigTable->FAHighThresh)
			{
				if((pDigTable->BackoffVal -2) < pDigTable->BackoffVal_range_min)
					pDigTable->BackoffVal = pDigTable->BackoffVal_range_min;
				else
					pDigTable->BackoffVal -= 2; 
			}	
			else if(FalseAlmCnt->Cnt_all < pDigTable->FALowThresh)
			{
				if((pDigTable->BackoffVal+2) > pDigTable->BackoffVal_range_max)
					pDigTable->BackoffVal = pDigTable->BackoffVal_range_max;
				else
					pDigTable->BackoffVal +=2;
			}	
		}
		else
			pDigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;

		pDigTable->CurIGValue = pDigTable->Rssi_val_min+10-pDigTable->BackoffVal;	

		//DBG_8192C("Rssi_val_min = %x BackoffVal %x\n",pDigTable->Rssi_val_min, pDigTable->BackoffVal);
#ifdef CONFIG_USB_HCI
	}
	else
	{		
		//DBG_8192C("===> dm_CtrlInitGainByRssi, Enter DIG by FA mode\n");
		//DBG_8192C("RSSI = 0x%x", pDigTable->Rssi_val_min);

		//Adjust initial gain by false alarm		
		if(FalseAlmCnt->Cnt_all > 1000)
			pDigTable->CurIGValue = pDigTable ->PreIGValue+2;
		else if (FalseAlmCnt->Cnt_all > 750)
			pDigTable->CurIGValue = pDigTable->PreIGValue+1;
		else if(FalseAlmCnt->Cnt_all < 500)
			pDigTable->CurIGValue = pDigTable->PreIGValue-1;	
	}
#endif

	//Check initial gain by upper/lower bound
	if(pDigTable->CurIGValue >pDigTable->rx_gain_range_max)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_max;

	if(pDigTable->CurIGValue < pDigTable->rx_gain_range_min)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_min;

#ifdef CONFIG_DM_ADAPTIVITY
	if(pdmpriv->DMFlag & DYNAMIC_FUNC_ADAPTIVITY)
	{
		if(pDigTable->CurIGValue > Adap_IGI_Upper)
			pDigTable->CurIGValue = Adap_IGI_Upper;

		if(pdmpriv->IGI_LowerBound != 0)
		{
			if(pDigTable->CurIGValue < pdmpriv->IGI_LowerBound)
				pDigTable->CurIGValue = pdmpriv->IGI_LowerBound;
		}
		LOG_LEVEL(_drv_info_, FUNC_ADPT_FMT": pdmpriv->IGI_LowerBound = %d\n",
			FUNC_ADPT_ARG(pAdapter), pdmpriv->IGI_LowerBound);
	}
#endif /* CONFIG_DM_ADAPTIVITY */

	//printk("%s => rx_gain_range_max(0x%02x) rx_gain_range_min(0x%02x)\n",__FUNCTION__,
	//	pDigTable->rx_gain_range_max,pDigTable->rx_gain_range_min);
	//printk("%s CurIGValue(0x%02x)  <====\n",__FUNCTION__,pDigTable->CurIGValue );		

	DM_Write_DIG(pAdapter);

}
#else
static VOID dm_CtrlInitGainByRssi(IN	PADAPTER	pAdapter)	
{
	u32 isBT;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	u8 RSSI_tmp = dm_initial_gain_MinPWDB(pAdapter);
#ifdef CONFIG_DM_ADAPTIVITY
	u8 Adap_IGI_Upper = pdmpriv->IGI_target + 30 + (u8) pdmpriv->TH_L2H_ini -(u8) pdmpriv->TH_EDCCA_HL_diff;
#endif

	//modify DIG upper bound
	if((pDigTable->Rssi_val_min + 20) > DM_DIG_MAX )
		pDigTable->rx_gain_range_max = DM_DIG_MAX;
	else
		pDigTable->rx_gain_range_max = pDigTable->Rssi_val_min + 20;
	//printk("%s Rssi_val_min(0x%02x),rx_gain_range_max(0x%02x)\n",__FUNCTION__,pDigTable->Rssi_val_min,pDigTable->rx_gain_range_max);
	//modify DIG lower bound, deal with abnorally large false alarm
	if(FalseAlmCnt->Cnt_all > 10000)
	{
		//RT_TRACE(COMP_DIG, DBG_LOUD, ("dm_DIG(): Abnornally false alarm case. \n"));

		pDigTable->LargeFAHit++;
		if(pDigTable->ForbiddenIGI < pDigTable->CurIGValue)
		{
			pDigTable->ForbiddenIGI = pDigTable->CurIGValue;
			pDigTable->LargeFAHit = 1;
		}

		if(pDigTable->LargeFAHit >= 3)
		{
			if((pDigTable->ForbiddenIGI+1) > pDigTable->rx_gain_range_max)
				pDigTable->rx_gain_range_min = pDigTable->rx_gain_range_max;
			else
				pDigTable->rx_gain_range_min = (pDigTable->ForbiddenIGI + 1);
			pDigTable->Recover_cnt = 3600; //3600=2hr
		}
	}
	else
	{
		//Recovery mechanism for IGI lower bound
		if(pDigTable->Recover_cnt != 0)
			pDigTable->Recover_cnt --;
		else
		{
			if(pDigTable->LargeFAHit == 0 )
			{
				if((pDigTable->ForbiddenIGI -1) < DM_DIG_MIN)
				{
					pDigTable->ForbiddenIGI = DM_DIG_MIN;
					pDigTable->rx_gain_range_min = DM_DIG_MIN;
				}
				else
				{
					pDigTable->ForbiddenIGI --;
					pDigTable->rx_gain_range_min = (pDigTable->ForbiddenIGI + 1);
				}
			}
			else if(pDigTable->LargeFAHit == 3 )
			{
				pDigTable->LargeFAHit = 0;
			}
		}
	}

	//RT_TRACE(COMP_DIG, DBG_LOUD, ("DM_DigTable.ForbiddenIGI = 0x%x, DM_DigTable.LargeFAHit = 0x%x\n",pDigTable->ForbiddenIGI, pDigTable->LargeFAHit));
	//RT_TRACE(COMP_DIG, DBG_LOUD, ("DM_DigTable.rx_gain_range_max = 0x%x, DM_DigTable.rx_gain_range_min = 0x%x\n",pDigTable->rx_gain_range_max, pDigTable->rx_gain_range_min));

#ifdef CONFIG_USB_HCI
	if(FalseAlmCnt->Cnt_all < 250)
	{
#endif
		//DBG_8192C("===> dm_CtrlInitGainByRssi, Enter DIG by SS mode\n");
		
		isBT = rtw_read8(pAdapter, 0x4fd) & 0x01;

		if(!isBT){

			if(FalseAlmCnt->Cnt_all > pDigTable->FAHighThresh)
			{
				if((pDigTable->BackoffVal -2) < pDigTable->BackoffVal_range_min)
					pDigTable->BackoffVal = pDigTable->BackoffVal_range_min;
				else
					pDigTable->BackoffVal -= 2; 
			}	
			else if(FalseAlmCnt->Cnt_all < pDigTable->FALowThresh)
			{
				if((pDigTable->BackoffVal+2) > pDigTable->BackoffVal_range_max)
					pDigTable->BackoffVal = pDigTable->BackoffVal_range_max;
				else
					pDigTable->BackoffVal +=2;
			}	
		}
		else
			pDigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;

		pDigTable->CurIGValue = pDigTable->Rssi_val_min+10-pDigTable->BackoffVal;	

		//DBG_8192C("Rssi_val_min = %x BackoffVal %x\n",pDigTable->Rssi_val_min, pDigTable->BackoffVal);
#ifdef CONFIG_USB_HCI
	}
	else
	{		
		//DBG_8192C("===> dm_CtrlInitGainByRssi, Enter DIG by FA mode\n");
		//DBG_8192C("RSSI = 0x%x", pDigTable->Rssi_val_min);

		//Adjust initial gain by false alarm		
		if(FalseAlmCnt->Cnt_all > 1000)
			pDigTable->CurIGValue = pDigTable ->PreIGValue+2;
		else if (FalseAlmCnt->Cnt_all > 750)
			pDigTable->CurIGValue = pDigTable->PreIGValue+1;
		else if(FalseAlmCnt->Cnt_all < 500)
			pDigTable->CurIGValue = pDigTable->PreIGValue-1;	
	}
#endif

	if(RSSI_tmp <= DM_DIG_MIN)
		pDigTable->rx_gain_range_min = DM_DIG_MIN;
	else if(RSSI_tmp >= DM_DIG_MAX)
		pDigTable->rx_gain_range_min = DM_DIG_MAX;
	else
		pDigTable->rx_gain_range_min = RSSI_tmp;


	//Check initial gain by upper/lower bound
	if(pDigTable->CurIGValue >pDigTable->rx_gain_range_max)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_max;

	if(pDigTable->CurIGValue < pDigTable->rx_gain_range_min)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_min;

#ifdef CONFIG_DM_ADAPTIVITY
	if(pdmpriv->DMFlag & DYNAMIC_FUNC_ADAPTIVITY)
	{
		if(pDigTable->CurIGValue > Adap_IGI_Upper)
			pDigTable->CurIGValue = Adap_IGI_Upper;

		if(pdmpriv->IGI_LowerBound != 0)
		{
			if(pDigTable->CurIGValue < pdmpriv->IGI_LowerBound)
				pDigTable->CurIGValue = pdmpriv->IGI_LowerBound;
		}
		LOG_LEVEL(_drv_info_, FUNC_ADPT_FMT": pdmpriv->IGI_LowerBound = %d\n",
			FUNC_ADPT_ARG(pAdapter), pdmpriv->IGI_LowerBound);
	}
#endif /* CONFIG_DM_ADAPTIVITY */

	//printk("%s => rx_gain_range_max(0x%02x) rx_gain_range_min(0x%02x)\n",__FUNCTION__,
	//	pDigTable->rx_gain_range_max,pDigTable->rx_gain_range_min);
	//printk("%s CurIGValue(0x%02x)  <====\n",__FUNCTION__,pDigTable->CurIGValue );		

	DM_Write_DIG(pAdapter);

}
#endif

static VOID
dm_initial_gain_Multi_STA(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &(pAdapter->mlmepriv);
	DIG_T			*pDigTable = &pdmpriv->DM_DigTable;
	int				rssi_strength =  pdmpriv->EntryMinUndecoratedSmoothedPWDB;	
	BOOLEAN			bMulti_STA = _FALSE;
	
#ifdef CONFIG_CONCURRENT_MODE
	//AP Mode
	if(check_buddy_fwstate(pAdapter, WIFI_AP_STATE) == _TRUE && (rssi_strength !=0))
	{
		bMulti_STA = _TRUE;
	}	
	else if(pDigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT && rssi_strength==0) //STA+STA MODE
	{
		bMulti_STA = _TRUE;
		rssi_strength = pdmpriv->UndecoratedSmoothedPWDB;		
	}
#endif //CONFIG_CONCURRENT_MODE


	//ADHOC and AP Mode
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		bMulti_STA = _TRUE;
	}


	if((bMulti_STA == _FALSE) 
		|| (pDigTable->CurSTAConnectState == DIG_STA_DISCONNECT))	 
	{
		pdmpriv->binitialized = _FALSE;
		pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_MAX;
		return;
	}	
	else if(pdmpriv->binitialized == _FALSE)
	{
		pdmpriv->binitialized = _TRUE;
		pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_0;
		pDigTable->CurIGValue = 0x20;
		DM_Write_DIG(pAdapter);
	}

	// Initial gain control by ap mode 
	if(pDigTable->CurMultiSTAConnectState == DIG_MultiSTA_CONNECT)
	{
		if (	(rssi_strength < pDigTable->RssiLowThresh) 	&& 
			(pDigTable->Dig_Ext_Port_Stage != DIG_EXT_PORT_STAGE_1))
		{					
			// Set to dig value to 0x20 for Luke's opinion after disable dig
			if(pDigTable->Dig_Ext_Port_Stage == DIG_EXT_PORT_STAGE_2)
			{
				pDigTable->CurIGValue = 0x20;
				DM_Write_DIG(pAdapter);				
			}	
			pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_1;	
		}	
		else if (rssi_strength > pDigTable->RssiHighThresh)
		{
			pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_2;
			dm_CtrlInitGainByFA(pAdapter);
		} 
	}	
	else if(pDigTable->Dig_Ext_Port_Stage != DIG_EXT_PORT_STAGE_0)
	{
		pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_0;
		pDigTable->CurIGValue = 0x20;
		DM_Write_DIG(pAdapter);
	}

	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CurMultiSTAConnectState = %x Dig_Ext_Port_Stage %x\n", 
	//			DM_DigTable.CurMultiSTAConnectState, DM_DigTable.Dig_Ext_Port_Stage));
}

static VOID 
dm_initial_gain_STA_beforelinked(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	PFALSE_ALARM_STATISTICS pFalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	
	//CurrentIGI = pDM_DigTable->rx_gain_range_min;//pDM_DigTable->CurIGValue = pDM_DigTable->rx_gain_range_min
	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_DIG(): DIG BeforeLink\n"));
	//2012.03.30 LukeLee: enable DIG before link but with very high thresholds
	//	Updated by Albert 2012/09/27
	//	Copy the same rule from 8192du code.
      	if( pFalseAlmCnt->Cnt_all > 2000 )
		pDigTable->CurIGValue += 2;
	else if ( ( pFalseAlmCnt->Cnt_all > 1000 ) && ( pFalseAlmCnt->Cnt_all <= 1000 ) )
		pDigTable->CurIGValue += 1;
	else if(pFalseAlmCnt->Cnt_all < 500)
		 pDigTable->CurIGValue -= 1;

	//Check initial gain by upper/lower bound
	if(pDigTable->CurIGValue >pDigTable->rx_gain_range_max)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_max;

	if(pDigTable->CurIGValue < pDigTable->rx_gain_range_min)
		pDigTable->CurIGValue = pDigTable->rx_gain_range_min;
	
	printk("%s ==> FalseAlmCnt->Cnt_all:%d CurIGValue:0x%02x \n",__FUNCTION__,pFalseAlmCnt->Cnt_all ,pDigTable->CurIGValue);		 
}

static VOID 
dm_initial_gain_STA(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;
	
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("PreSTAConnectState = %x, CurSTAConnectState = %x\n", 
	//			DM_DigTable.PreSTAConnectState, DM_DigTable.CurSTAConnectState));


	if(pDigTable->PreSTAConnectState == pDigTable->CurSTAConnectState||
	   pDigTable->CurSTAConnectState == DIG_STA_BEFORE_CONNECT ||
	   pDigTable->CurSTAConnectState == DIG_STA_CONNECT)
	{
		// beforeconnect -> beforeconnect or  connect -> connect
		// (dis)connect -> beforeconnect
		// disconnect -> connecct or beforeconnect -> connect
		if(pDigTable->CurSTAConnectState != DIG_STA_DISCONNECT)
		{
			pDigTable->Rssi_val_min = dm_initial_gain_MinPWDB(pAdapter);
			dm_CtrlInitGainByRssi(pAdapter);
		}	
#if 0
		else if((wdev_to_priv(pAdapter->rtw_wdev))->p2p_enabled == _TRUE 
				&& pAdapter->wdinfo.driver_interface == DRIVER_CFG80211)
		{
			//pDigTable->CurIGValue = 0x30;
			DM_Write_DIG(pAdapter);
		}
#endif
		else{ // pDigTable->CurSTAConnectState == DIG_STA_DISCONNECT 
		#ifdef CONFIG_BEFORE_LINKED_DIG
			//printk("%s==> ##1 CurIGI(0x%02x),PreIGValue(0x%02x) \n",__FUNCTION__,pDigTable->CurIGValue,pDigTable->PreIGValue );
			dm_initial_gain_STA_beforelinked(pAdapter);
			DM_Write_DIG(pAdapter);
		#endif //CONFIG_BEFORE_LINKED_DIG
		}
	}
	else	
	{		
		// connect -> disconnect or beforeconnect -> disconnect
		pDigTable->Rssi_val_min = 0;
		pDigTable->Dig_Ext_Port_Stage = DIG_EXT_PORT_STAGE_MAX;
		pDigTable->BackoffVal = DM_DIG_BACKOFF_DEFAULT;
		pDigTable->CurIGValue = 0x20;
		pDigTable->PreIGValue = 0;	
		#ifdef CONFIG_BEFORE_LINKED_DIG			
		//printk("%s==> ##2 CurIGI(0x%02x),PreIGValue(0x%02x) \n",__FUNCTION__,pDigTable->CurIGValue,pDigTable->PreIGValue );	
		dm_initial_gain_STA_beforelinked(pAdapter);			 
 		#endif //CONFIG_BEFORE_LINKED_DIG
		

		DM_Write_DIG(pAdapter);
	}

}


static void dm_CCK_PacketDetectionThresh(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = &(pdmpriv->FalseAlmCnt);
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;

	if(pDigTable->CurSTAConnectState == DIG_STA_CONNECT)
	{
		pDigTable->Rssi_val_min = dm_initial_gain_MinPWDB(pAdapter);
		if(pDigTable->PreCCKPDState == CCK_PD_STAGE_LowRssi)
		{
			if(pDigTable->Rssi_val_min <= 25)
				pDigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
		else{
			if(pDigTable->Rssi_val_min <= 20)
				pDigTable->CurCCKPDState = CCK_PD_STAGE_LowRssi;
			else
				pDigTable->CurCCKPDState = CCK_PD_STAGE_HighRssi;
		}
	}
	else
		pDigTable->CurCCKPDState=CCK_PD_STAGE_MAX;
	
	if(pDigTable->PreCCKPDState != pDigTable->CurCCKPDState)
	{
		if((pDigTable->CurCCKPDState == CCK_PD_STAGE_LowRssi)||
			(pDigTable->CurCCKPDState == CCK_PD_STAGE_MAX))
		{
			PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0x83);
				
			//PHY_SetBBReg(pAdapter, rCCK0_System, bMaskByte1, 0x40);
			//if(IS_92C_SERIAL(pHalData->VersionID))
				//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd7);
		}
		else
		{
			PHY_SetBBReg(pAdapter, rCCK0_CCA, bMaskByte2, 0xcd);
			//PHY_SetBBReg(pAdapter,rCCK0_System, bMaskByte1, 0x47);
			//if(IS_92C_SERIAL(pHalData->VersionID))
				//PHY_SetBBReg(pAdapter, rCCK0_FalseAlarmReport , bMaskByte2, 0xd3);
		}

		pDigTable->PreCCKPDState = pDigTable->CurCCKPDState;
	}
	
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("CCKPDStage=%x\n",pDigTable->CurCCKPDState));
	//RT_TRACE(	COMP_DIG, DBG_LOUD, ("is92C=%x\n",IS_92C_SERIAL(pHalData->VersionID)));
	
}


static	void
dm_CtrlInitGainByTwoPort(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &(pAdapter->mlmepriv);
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY) == _TRUE)
	{
#ifdef CONFIG_IOCTL_CFG80211
		if((wdev_to_priv(pAdapter->rtw_wdev))->p2p_enabled == _TRUE)
		{
		}
		else
#endif
			return;
	}

	// Decide the current status and if modify initial gain or not
	if (check_fwstate(pmlmepriv, _FW_UNDER_LINKING) == _TRUE)
	{
		pDigTable->CurSTAConnectState = DIG_STA_BEFORE_CONNECT;
	}
	else if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) 
	{
		pDigTable->CurSTAConnectState = DIG_STA_CONNECT;
	}	
	else
	{
		pDigTable->CurSTAConnectState = DIG_STA_DISCONNECT;
	}
	

	pDigTable->CurMultiSTAConnectState = DIG_MultiSTA_DISCONNECT;
	if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		if((is_IBSS_empty(pAdapter)==_FAIL) && (pAdapter->stapriv.asoc_sta_count > 2))
			pDigTable->CurMultiSTAConnectState = DIG_MultiSTA_CONNECT;
	}
	
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
	{
		if(pAdapter->stapriv.asoc_sta_count > 2)
			pDigTable->CurMultiSTAConnectState = DIG_MultiSTA_CONNECT;					
	}

#ifdef CONFIG_CONCURRENT_MODE
	if(check_buddy_fwstate(pAdapter, WIFI_AP_STATE) == _TRUE)
	{
		PADAPTER pbuddy_adapter = pAdapter->pbuddy_adapter;
		
		if(pbuddy_adapter->stapriv.asoc_sta_count > 2)
		{
			pDigTable->CurSTAConnectState = DIG_STA_CONNECT;
			pDigTable->CurMultiSTAConnectState = DIG_MultiSTA_CONNECT;	
		}	
	}	
	else if(check_buddy_fwstate(pAdapter, WIFI_STATION_STATE) == _TRUE	&& 
		check_buddy_fwstate(pAdapter, _FW_LINKED) == _TRUE)
	{
		pDigTable->CurSTAConnectState = DIG_STA_CONNECT;

	}
#endif //CONFIG_CONCURRENT_MODE


	dm_initial_gain_STA(pAdapter);
	dm_initial_gain_Multi_STA(pAdapter);
	//Baron temp DIG solution for DMP
	//dm_CtrlInitGainByFA(pAdapter);

	dm_CCK_PacketDetectionThresh(pAdapter);

	pDigTable->PreSTAConnectState = pDigTable->CurSTAConnectState;
	
}


static void dm_DIG(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;

	//Read 0x0c50; Initial gain
	pDigTable->PreIGValue = (u8)PHY_QueryBBReg(pAdapter, rOFDM0_XAAGCCore1, bMaskByte0);

	//RTPRINT(FDM, DM_Monitor, ("dm_DIG() ==>\n"));
	
	if(pdmpriv->bDMInitialGainEnable == _FALSE)
		return;
	
	//if(pDigTable->Dig_Enable_Flag == _FALSE)
	//	return;
	
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
		return;
	
	//RTPRINT(FDM, DM_Monitor, ("dm_DIG() progress \n"));

	dm_CtrlInitGainByTwoPort(pAdapter);
	
	//RTPRINT(FDM, DM_Monitor, ("dm_DIG() <==\n"));
}

static void dm_SavePowerIndex(IN	PADAPTER	Adapter)
{
	u8			index;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
	for(index = 0; index< 6; index++)
		pdmpriv->PowerIndex_backup[index] = rtw_read8(Adapter, Power_Index_REG[index]);
}

static void dm_RestorePowerIndex(IN	PADAPTER	Adapter)
{
	u8			index;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u32			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
	for(index = 0; index< 6; index++)
		rtw_write8(Adapter, Power_Index_REG[index], pdmpriv->PowerIndex_backup[index]);
}

static void dm_WritePowerIndex(
		IN	PADAPTER	Adapter, 
		IN 	u8		Value)
{
	u8			index;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			Power_Index_REG[6] = {0xc90, 0xc91, 0xc92, 0xc98, 0xc99, 0xc9a};
	
	for(index = 0; index< 6; index++)
		rtw_write8(Adapter, Power_Index_REG[index], Value);
}

static void dm_InitDynamicTxPower(IN	PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

#ifdef CONFIG_USB_HCI
#ifdef CONFIG_INTEL_PROXIM
	if((pHalData->BoardType == BOARD_USB_High_PA)||(Adapter->proximity.proxim_support==_TRUE))
#else
	if(pHalData->BoardType == BOARD_USB_High_PA)
#endif
	{
		dm_SavePowerIndex(Adapter);
		pdmpriv->bDynamicTxPowerEnable = _TRUE;
	}		
	else	
#else
		pdmpriv->bDynamicTxPowerEnable = _FALSE;
#endif

	pdmpriv->LastDTPLvl = TxHighPwrLevel_Normal;
	pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
}


static void dm_DynamicTxPower(IN	PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	int	UndecoratedSmoothedPWDB;

	if(!pdmpriv->bDynamicTxPowerEnable)
		return;

	// If dynamic high power is disabled.
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_HP) )
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	// STA not connected and AP not connected
	if((check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE) &&	
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("Not connected to any \n"));
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		//the LastDTPlvl should reset when disconnect, 
		//otherwise the tx power level wouldn't change when disconnect and connect again.
		// Maddest 20091220.
		pdmpriv->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}
#ifdef CONFIG_INTEL_PROXIM
	if(Adapter->proximity.proxim_on== _TRUE){
		struct proximity_priv *prox_priv=Adapter->proximity.proximity_priv;
	// Intel set fixed tx power 
	printk("\n %s  Adapter->proximity.proxim_on=%d prox_priv->proxim_modeinfo->power_output=%d \n",__FUNCTION__,Adapter->proximity.proxim_on,prox_priv->proxim_modeinfo->power_output);
	if(prox_priv!=NULL){
	if(prox_priv->proxim_modeinfo->power_output> 0)
	
	{
		switch(prox_priv->proxim_modeinfo->power_output){
			case 1:
				pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_100;
				printk("TxHighPwrLevel_100\n");
				break;
			case 2:
				pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_70;
				printk("TxHighPwrLevel_70\n");
				break;
			case 3:
				pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_50;
				printk("TxHighPwrLevel_50\n");
				break;
			case 4:
				pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_35;
				printk("TxHighPwrLevel_35\n");
				break;
			case 5:
				pdmpriv->DynamicTxHighPowerLvl  = TxHighPwrLevel_15;
				printk("TxHighPwrLevel_15\n");
				break;
			default:
				pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_100;
				printk("TxHighPwrLevel_100\n");
				break;
		}		
	}
	}
	}
	else
#endif		
{
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
		//todo: AP Mode
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
		       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))
		{
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("AP Client PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
		else
		{
			UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("STA Default Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
		}
	}
	else // associated entry pwdb
	{	
		UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("AP Ext Port PWDB = 0x%x \n", UndecoratedSmoothedPWDB));
	}
		
	if(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x0)\n"));
	}
	else if((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
		(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1) )
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Level1 (TxPwr=0x10)\n"));
	}
	else if(UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		//RT_TRACE(COMP_HIPWR, DBG_LOUD, ("TxHighPwrLevel_Normal\n"));
	}
}
	if( (pdmpriv->DynamicTxHighPowerLvl != pdmpriv->LastDTPLvl) )
	{
		PHY_SetTxPowerLevel8192C(Adapter, pHalData->CurrentChannel);
		if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal) // HP1 -> Normal  or HP2 -> Normal
			dm_RestorePowerIndex(Adapter);
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level1)
			dm_WritePowerIndex(Adapter, 0x14);
		else if(pdmpriv->DynamicTxHighPowerLvl == TxHighPwrLevel_Level2)
			dm_WritePowerIndex(Adapter, 0x10);
	}
	pdmpriv->LastDTPLvl = pdmpriv->DynamicTxHighPowerLvl;
	
}


static VOID
DM_ChangeDynamicInitGainThresh(
	IN	PADAPTER	pAdapter,
	IN	u32		DM_Type,
	IN	u32		DM_Value)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DIG_T	*pDigTable = &pdmpriv->DM_DigTable;

	if (DM_Type == DIG_TYPE_THRESH_HIGH)
	{
		pDigTable->RssiHighThresh = DM_Value;		
	}
	else if (DM_Type == DIG_TYPE_THRESH_LOW)
	{
		pDigTable->RssiLowThresh = DM_Value;
	}
	else if (DM_Type == DIG_TYPE_ENABLE)
	{
		pDigTable->Dig_Enable_Flag = _TRUE;
	}	
	else if (DM_Type == DIG_TYPE_DISABLE)
	{
		pDigTable->Dig_Enable_Flag = _FALSE;
	}	
	else if (DM_Type == DIG_TYPE_BACKOFF)
	{
		if(DM_Value > 30)
			DM_Value = 30;
		pDigTable->BackoffVal = (u8)DM_Value;
	}
	else if(DM_Type == DIG_TYPE_RX_GAIN_MIN)
	{
		if(DM_Value == 0)
			DM_Value = 0x1;
		pDigTable->rx_gain_range_min = (u8)DM_Value;
	}
	else if(DM_Type == DIG_TYPE_RX_GAIN_MAX)
	{
		if(DM_Value > 0x50)
			DM_Value = 0x50;
		pDigTable->rx_gain_range_max = (u8)DM_Value;
	}
}	/* DM_ChangeDynamicInitGainThresh */


static VOID PWDB_Monitor(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int	i;
	int	tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u8 	sta_cnt=0;
	u32 PWDB_rssi[NUM_STA]={0};//[0~15]:MACID, [16~31]:PWDB_rssi

	if(check_fwstate(&Adapter->mlmepriv, _FW_LINKED) != _TRUE)
		return;


	if(check_fwstate(&Adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == _TRUE)
	{
		_irqL irqL;
		_list	*plist, *phead;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &Adapter->stapriv;
		u8 bcast_addr[ETH_ALEN]= {0xff,0xff,0xff,0xff,0xff,0xff};
	
		_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

		for(i=0; i< NUM_STA; i++)
		{
			phead = &(pstapriv->sta_hash[i]);
			plist = get_next(phead);
		
			while ((rtw_end_of_queue_search(phead, plist)) == _FALSE)
			{
				psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

				plist = get_next(plist);

				if(_rtw_memcmp(psta	->hwaddr, bcast_addr, ETH_ALEN) || 
					_rtw_memcmp(psta->hwaddr, myid(&Adapter->eeprompriv), ETH_ALEN))
					continue;

				if(psta->state & WIFI_ASOC_STATE)
				{
					
					if(psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if(psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16));
				}
			
			}

		}
	
		_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);


		
		if(pHalData->fw_ractrl == _TRUE)
		{
			// Report every sta's RSSI to FW
			for(i=0; i< sta_cnt; i++)
			{
				rtl8192c_set_rssi_cmd(Adapter, (u8*)&PWDB_rssi[i]);
			}
		}

	}



	if(tmpEntryMaxPWDB != 0)	// If associated entry is found
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;		
	}
	else
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = 0;
	}
	
	if(tmpEntryMinPWDB != 0xff) // If associated entry is found
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;		
	}
	else
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}


	if(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
	{
	
		if(pHalData->fw_ractrl == _TRUE)
		{
			u32 param = (u32)(pdmpriv->UndecoratedSmoothedPWDB<<16);
		
			param |= 0;//macid=0 for sta mode;
			
			rtl8192c_set_rssi_cmd(Adapter, (u8*)&param);
		}
	}

}


static void
DM_InitEdcaTurbo(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	pHalData->bCurrentTurboEDCA = _FALSE;
	Adapter->recvpriv.bIsAnyNonBEPkts = _FALSE;

}


static void
dm_CheckEdcaTurbo(
	IN	PADAPTER	Adapter
	)
{
	u32 	trafficIndex;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u8	bbtchange = _FALSE;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	struct xmit_priv		*pxmitpriv = &(Adapter->xmitpriv);
	struct recv_priv		*precvpriv = &(Adapter->recvpriv);
	struct registry_priv	*pregpriv = &Adapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);	
#endif


	if ((pregpriv->wifi_spec == 1) || (pmlmeinfo->HT_enable == 0))
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

	if (pmlmeinfo->assoc_AP_vendor >= maxAP)
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

#ifdef CONFIG_BT_COEXIST
	if(pbtpriv->BT_Coexist)
	{
		if( (pbtpriv->BT_EDCA[UP_LINK]!=0) ||  (pbtpriv->BT_EDCA[DOWN_LINK]!=0))
		{
			bbtchange = _TRUE;		
		}
	}
#endif

	// Check if the status needs to be changed.
	if((bbtchange) || (!precvpriv->bIsAnyNonBEPkts) )
	{
		cur_tx_bytes = pxmitpriv->tx_bytes - pxmitpriv->last_tx_bytes;
		cur_rx_bytes = precvpriv->rx_bytes - precvpriv->last_rx_bytes;

		//traffic, TX or RX
		if((pmlmeinfo->assoc_AP_vendor == ralinkAP)||(pmlmeinfo->assoc_AP_vendor == atherosAP))
		{
			if (cur_tx_bytes > (cur_rx_bytes << 2))
			{ // Uplink TP is present.
				trafficIndex = UP_LINK; 
			}
			else
			{ // Balance TP is present.
				trafficIndex = DOWN_LINK;
			}
		}
		else
		{
			if (cur_rx_bytes > (cur_tx_bytes << 2))
			{ // Downlink TP is present.
				trafficIndex = DOWN_LINK;
			}
			else
			{ // Balance TP is present.
				trafficIndex = UP_LINK;
			}
		}

		if ((pdmpriv->prv_traffic_idx != trafficIndex) || (!pHalData->bCurrentTurboEDCA))
		{
#ifdef CONFIG_BT_COEXIST
			if(_TRUE == bbtchange)
			{
				edca_param = pbtpriv->BT_EDCA[trafficIndex];
			}
			else
#endif
			{
#if 0
				//adjust EDCA parameter for BE queue
				edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
#else

				if((pmlmeinfo->assoc_AP_vendor == ciscoAP) && (pmlmeext->cur_wireless_mode & WIRELESS_11_24N))
				{
					edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
				}
				else
				{
					edca_param = EDCAParam[unknownAP][trafficIndex];
				}
#endif
			}

#ifdef CONFIG_PCI_HCI
			if(IS_92C_SERIAL(pHalData->VersionID))
			{
				edca_param = 0x60a42b;
			}
			else
			{
				edca_param = 0x6ea42b;
			}
#endif
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, edca_param);

			pdmpriv->prv_traffic_idx = trafficIndex;
		}
		
		pHalData->bCurrentTurboEDCA = _TRUE;
	}
	else
	{
		//
		// Turn Off EDCA turbo here.
		// Restore original EDCA according to the declaration of AP.
		//
		 if(pHalData->bCurrentTurboEDCA)
		{
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, pHalData->AcParam_BE);
			pHalData->bCurrentTurboEDCA = _FALSE;
		}
	}

dm_CheckEdcaTurbo_EXIT:
	// Set variables for next time.
	precvpriv->bIsAnyNonBEPkts = _FALSE;
	pxmitpriv->last_tx_bytes = pxmitpriv->tx_bytes;
	precvpriv->last_rx_bytes = precvpriv->rx_bytes;

}

#define DPK_DELTA_MAPPING_NUM	13
#define index_mapping_HP_NUM		15

static	VOID
dm_TXPowerTrackingCallback_ThermalMeter_92C(
            IN PADAPTER	Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8			ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_HP, TimeOut = 100;
	int 			ele_A, ele_D, TempCCk, X, value32;
	int			Y, ele_C;
	s8			OFDM_index[2], CCK_index = 0, OFDM_index_old[2], CCK_index_old = 0;
	int			i = 0;
	BOOLEAN		is2T = IS_92C_SERIAL(pHalData->VersionID);

#if MP_DRIVER == 1
	PMPT_CONTEXT	pMptCtx = &(Adapter->mppriv.MptCtx);	
	u8			*TxPwrLevel = pMptCtx->TxPwrLevel;
#endif
	u8			OFDM_min_index = 6, rf; //OFDM BB Swing should be less than +3.0dB, which is required by Arthur
#if 0
	u32			DPK_delta_mapping[2][DPK_DELTA_MAPPING_NUM] = {
					{0x1c, 0x1c, 0x1d, 0x1d, 0x1e, 
					 0x1f, 0x00, 0x00, 0x01, 0x01,
					 0x02, 0x02, 0x03},
					{0x1c, 0x1d, 0x1e, 0x1e, 0x1e,
					 0x1f, 0x00, 0x00, 0x01, 0x02,
					 0x02, 0x03, 0x03}};
#endif
#ifdef CONFIG_USB_HCI
	u8			ThermalValue_HP_count = 0;
	u32			ThermalValue_HP = 0;
	s32			index_mapping_HP[index_mapping_HP_NUM] = {
					0,	1,	3,	4,	6,	
					7,	9,	10,	12,	13,	
					15,	16,	18,	19,	21
					};

	s8			index_HP;
#endif

	pdmpriv->TXPowerTrackingCallbackCnt++;	//cosa add for debug
	pdmpriv->bTXPowerTrackingInit = _TRUE;

	if(pHalData->CurrentChannel == 14 && !pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _TRUE;
	else if(pHalData->CurrentChannel != 14 && pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = _FALSE;

	//DBG_8192C("===>dm_TXPowerTrackingCallback_ThermalMeter_92C\n");

	ThermalValue = (u8)PHY_QueryRFReg(Adapter, RF_PATH_A, RF_T_METER, 0x1f);	// 0x24: RF Reg[4:0]	

	//DBG_8192C("\n\nReadback Thermal Meter = 0x%x pre thermal meter 0x%x EEPROMthermalmeter 0x%x\n",ThermalValue,pdmpriv->ThermalValue,  pHalData->EEPROMThermalMeter);

	rtl8192c_PHY_APCalibrate(Adapter, (ThermalValue - pHalData->EEPROMThermalMeter));

	if(is2T)
		rf = 2;
	else
		rf = 1;
	
	if(ThermalValue)
	{
//		if(!pHalData->ThermalValue)
		{
			//Query OFDM path A default setting 		
			ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
			{
				if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u8)i;
					//DBG_8192C("Initial pathA ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n", rOFDM0_XATxIQImbalance, ele_D, OFDM_index_old[0]);
					break;
				}
			}

			//Query OFDM path B default setting 
			if(is2T)
			{
				ele_D = PHY_QueryBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for(i=0; i<OFDM_TABLE_SIZE_92C; i++)	//find the index
				{
					if(ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u8)i;
						//DBG_8192C("Initial pathB ele_D reg0x%x = 0x%x, OFDM_index=0x%x\n",rOFDM0_XBTxIQImbalance, ele_D, OFDM_index_old[1]);
						break;
					}
				}
			}

			//Query CCK default setting From 0xa24
			TempCCk = PHY_QueryBBReg(Adapter, rCCK0_TxFilter2, bMaskDWord)&bMaskCCK;
			for(i=0 ; i<CCK_TABLE_SIZE ; i++)
			{
				if(pdmpriv->bCCKinCH14)
				{
					if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==_TRUE)
					{
						CCK_index_old =(u8)i;
						//DBG_8192C("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch 14 %d\n", rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14);
						break;
					}
				}
				else
				{
					if(_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==_TRUE)
					{
						CCK_index_old =(u8)i;
						//DBG_8192C("Initial reg0x%x = 0x%x, CCK_index=0x%x, ch14 %d\n", rCCK0_TxFilter2, TempCCk, CCK_index_old, pdmpriv->bCCKinCH14);
						break;
					}			
				}
			}	

			if(!pdmpriv->ThermalValue)
			{
				pdmpriv->ThermalValue = pHalData->EEPROMThermalMeter;
				pdmpriv->ThermalValue_LCK = ThermalValue;
				pdmpriv->ThermalValue_IQK = ThermalValue;
				pdmpriv->ThermalValue_DPK = pHalData->EEPROMThermalMeter;

#ifdef CONFIG_USB_HCI
				for(i = 0; i < rf; i++)
					pdmpriv->OFDM_index_HP[i] = pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index_HP = pdmpriv->CCK_index = CCK_index_old;
#else
				for(i = 0; i < rf; i++)
					pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index = CCK_index_old;
#endif
			}

#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType == BOARD_USB_High_PA)
			{
				pdmpriv->ThermalValue_HP[pdmpriv->ThermalValue_HP_index] = ThermalValue;
				pdmpriv->ThermalValue_HP_index++;
				if(pdmpriv->ThermalValue_HP_index == HP_THERMAL_NUM)
					pdmpriv->ThermalValue_HP_index = 0;

				for(i = 0; i < HP_THERMAL_NUM; i++)
				{
					if(pdmpriv->ThermalValue_HP[i])
					{
						ThermalValue_HP += pdmpriv->ThermalValue_HP[i];
						ThermalValue_HP_count++;
					}			
				}
		
				if(ThermalValue_HP_count)
					ThermalValue = (u8)(ThermalValue_HP / ThermalValue_HP_count);
			}
#endif
		}

		delta = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
#ifdef CONFIG_USB_HCI
		if(pHalData->BoardType == BOARD_USB_High_PA)
		{
			if(pdmpriv->bDoneTxpower)
				delta_HP = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
			else
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
		}
		else
#endif	
		{
			delta_HP = 0;			
		}
		delta_LCK = (ThermalValue > pdmpriv->ThermalValue_LCK)?(ThermalValue - pdmpriv->ThermalValue_LCK):(pdmpriv->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pdmpriv->ThermalValue_IQK)?(ThermalValue - pdmpriv->ThermalValue_IQK):(pdmpriv->ThermalValue_IQK - ThermalValue);

		//DBG_8192C("Readback Thermal Meter = 0x%lx pre thermal meter 0x%lx EEPROMthermalmeter 0x%lx delta 0x%lx delta_LCK 0x%lx delta_IQK 0x%lx\n", ThermalValue, pHalData->ThermalValue, pHalData->EEPROMThermalMeter, delta, delta_LCK, delta_IQK);

		if(delta_LCK > 1)
		{
			pdmpriv->ThermalValue_LCK = ThermalValue;
			rtl8192c_PHY_LCCalibrate(Adapter);
		}
		
		if((delta > 0 || delta_HP > 0) && pdmpriv->TxPowerTrackControl)
		{
#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType == BOARD_USB_High_PA)
			{
				pdmpriv->bDoneTxpower = _TRUE;
				delta_HP = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);						
				
				if(delta_HP > index_mapping_HP_NUM-1)					
					index_HP = index_mapping_HP[index_mapping_HP_NUM-1];
				else
					index_HP = index_mapping_HP[delta_HP];
				
				if(ThermalValue > pHalData->EEPROMThermalMeter)	//set larger Tx power
				{
					for(i = 0; i < rf; i++)
					 	OFDM_index[i] = pdmpriv->OFDM_index_HP[i] - index_HP;
					CCK_index = pdmpriv->CCK_index_HP -index_HP;						
				}
				else
				{
					for(i = 0; i < rf; i++)
						OFDM_index[i] = pdmpriv->OFDM_index_HP[i] + index_HP;
					CCK_index = pdmpriv->CCK_index_HP + index_HP;						
				}	
				
				delta_HP = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
				
			}
			else
#endif
			{
				if(ThermalValue > pdmpriv->ThermalValue)
				{ 
					for(i = 0; i < rf; i++)
					 	pdmpriv->OFDM_index[i] -= delta;
					pdmpriv->CCK_index -= delta;
				}
				else
				{
					for(i = 0; i < rf; i++)			
						pdmpriv->OFDM_index[i] += delta;
					pdmpriv->CCK_index += delta;
				}
			}

			/*if(is2T)
			{
				DBG_8192C("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					pdmpriv->OFDM_index[0], pdmpriv->OFDM_index[1], pdmpriv->CCK_index);
			}
			else
			{
				DBG_8192C("temp OFDM_A_index=0x%x, CCK_index=0x%x\n",
					pdmpriv->OFDM_index[0], pdmpriv->CCK_index);
			}*/

			//no adjust
#ifdef CONFIG_USB_HCI
			if(pHalData->BoardType != BOARD_USB_High_PA)
#endif
			{
				if(ThermalValue > pHalData->EEPROMThermalMeter)
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pdmpriv->OFDM_index[i]+1;
					CCK_index = pdmpriv->CCK_index+1;			
				}
				else
				{
					for(i = 0; i < rf; i++)			
						OFDM_index[i] = pdmpriv->OFDM_index[i];
					CCK_index = pdmpriv->CCK_index;						
				}

#if MP_DRIVER == 1
				for(i = 0; i < rf; i++)
				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								OFDM_index[i] -= 1;					
							else 
								OFDM_index[i] -= 2;					
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							OFDM_index[i] += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							OFDM_index[i] -= 1;					
						else 
							OFDM_index[i] -= 2;								
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						OFDM_index[i] -= 1;								
					}
				}

				{
					if(TxPwrLevel[i] >=0 && TxPwrLevel[i] <=26)
					{
						if(ThermalValue > pHalData->EEPROMThermalMeter)
						{
							if (delta < 5)
								CCK_index -= 1; 				
							else 
								CCK_index -= 2; 				
						}
						else if(delta > 5 && ThermalValue < pHalData->EEPROMThermalMeter)
						{
							CCK_index += 1;
						}
					}
					else if (TxPwrLevel[i] >= 27 && TxPwrLevel[i] <= 32 && ThermalValue > pHalData->EEPROMThermalMeter)
					{
						if (delta < 5)
							CCK_index -= 1; 				
						else 
							CCK_index -= 2; 							
					}
					else if (TxPwrLevel[i] >= 32 && TxPwrLevel[i] <= 38 && ThermalValue > pHalData->EEPROMThermalMeter && delta > 5)
					{
						CCK_index -= 1; 							
					}
				}
#endif
			}

			for(i = 0; i < rf; i++)
			{
				if(OFDM_index[i] > (OFDM_TABLE_SIZE_92C-1))
					OFDM_index[i] = (OFDM_TABLE_SIZE_92C-1);
				else if (OFDM_index[i] < OFDM_min_index)
					OFDM_index[i] = OFDM_min_index;
			}
						
			if(CCK_index > (CCK_TABLE_SIZE-1))
				CCK_index = (CCK_TABLE_SIZE-1);
			else if (CCK_index < 0)
				CCK_index = 0;		

			/*if(is2T)
			{
				DBG_8192C("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], OFDM_index[1], CCK_index);
			}
			else
			{
				DBG_8192C("new OFDM_A_index=0x%x, CCK_index=0x%x\n", 
					OFDM_index[0], CCK_index);
			}*/
		}

		if(pdmpriv->TxPowerTrackControl && (delta != 0 || delta_HP != 0))
		{
			//Adujst OFDM Ant_A according to IQK result
			ele_D = (OFDMSwingTable[OFDM_index[0]] & 0xFFC00000)>>22;
			X = pdmpriv->RegE94;
			Y = pdmpriv->RegE9C;		

			if(X != 0)
			{
				if ((X & 0x00000200) != 0)
					X = X | 0xFFFFFC00;
				ele_A = ((X * ele_D)>>8)&0x000003FF;
					
				//new element C = element D x Y
				if ((Y & 0x00000200) != 0)
					Y = Y | 0xFFFFFC00;
				ele_C = ((Y * ele_D)>>8)&0x000003FF;
				
				//wirte new elements A, C, D to regC80 and regC94, element B is always 0
				value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);
				
				value32 = (ele_C&0x000003C0)>>6;
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

				value32 = ((X * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31, value32);

				value32 = ((Y * ele_D)>>7)&0x01;
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT29, value32);

			}
			else
			{
				PHY_SetBBReg(Adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[0]]);				
				PHY_SetBBReg(Adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
				PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT31|BIT29, 0x00);
			}

			//RTPRINT(FINIT, INIT_IQK, ("TxPwrTracking path A: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D));		

			//Adjust CCK according to IQK result
			if(!pdmpriv->bCCKinCH14){
				rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch1_Ch13[CCK_index][0]);
				rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch1_Ch13[CCK_index][1]);
				rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch1_Ch13[CCK_index][2]);
				rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch1_Ch13[CCK_index][3]);
				rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch1_Ch13[CCK_index][4]);
				rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch1_Ch13[CCK_index][5]);
				rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch1_Ch13[CCK_index][6]);
				rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch1_Ch13[CCK_index][7]);
			}
			else{
				rtw_write8(Adapter, 0xa22, CCKSwingTable_Ch14[CCK_index][0]);
				rtw_write8(Adapter, 0xa23, CCKSwingTable_Ch14[CCK_index][1]);
				rtw_write8(Adapter, 0xa24, CCKSwingTable_Ch14[CCK_index][2]);
				rtw_write8(Adapter, 0xa25, CCKSwingTable_Ch14[CCK_index][3]);
				rtw_write8(Adapter, 0xa26, CCKSwingTable_Ch14[CCK_index][4]);
				rtw_write8(Adapter, 0xa27, CCKSwingTable_Ch14[CCK_index][5]);
				rtw_write8(Adapter, 0xa28, CCKSwingTable_Ch14[CCK_index][6]);
				rtw_write8(Adapter, 0xa29, CCKSwingTable_Ch14[CCK_index][7]);	
			}		

			if(is2T)
			{						
				ele_D = (OFDMSwingTable[(u8)OFDM_index[1]] & 0xFFC00000)>>22;
				
				//new element A = element D x X
				X = pdmpriv->RegEB4;
				Y = pdmpriv->RegEBC;
				
				if(X != 0){
					if ((X & 0x00000200) != 0)	//consider minus
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;
					
					//new element C = element D x Y
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x00003FF;
					
					//wirte new elements A, C, D to regC88 and regC9C, element B is always 0
					value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);	

					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27, value32);

					value32 = ((Y * ele_D)>>7)&0x01;
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT25, value32);

				}
				else{
					PHY_SetBBReg(Adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[OFDM_index[1]]);										
					PHY_SetBBReg(Adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
					PHY_SetBBReg(Adapter, rOFDM0_ECCAThreshold, BIT27|BIT25, 0x00);
				}

				//DBG_8192C("TxPwrTracking path B: X = 0x%x, Y = 0x%x ele_A = 0x%x ele_C = 0x%x ele_D = 0x%x\n", X, Y, ele_A, ele_C, ele_D);
			}

			/*
			DBG_8192C("TxPwrTracking 0xc80 = 0x%x, 0xc94 = 0x%x RF 0x24 = 0x%x\n", \
					PHY_QueryBBReg(Adapter, 0xc80, bMaskDWord),\
					PHY_QueryBBReg(Adapter, 0xc94, bMaskDWord), \
					PHY_QueryRFReg(Adapter, RF_PATH_A, 0x24, bMaskDWord));
			*/
		}

#if MP_DRIVER == 1
		if(delta_IQK > 1)
#else
		if(delta_IQK > 3)
#endif
		{
			pdmpriv->ThermalValue_IQK = ThermalValue;
			rtl8192c_PHY_IQCalibrate(Adapter,_FALSE);
		}

		//update thermal meter value
		if(pdmpriv->TxPowerTrackControl)
			pdmpriv->ThermalValue = ThermalValue;

	}

	//DBG_8192C("<===dm_TXPowerTrackingCallback_ThermalMeter_92C\n");
	
	pdmpriv->TXPowercount = 0;

}


static	VOID
dm_InitializeTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//pMgntInfo->bTXPowerTracking = _TRUE;
	pdmpriv->TXPowercount = 0;
	pdmpriv->bTXPowerTrackingInit = _FALSE;
	pdmpriv->ThermalValue = 0;
	
#if	(MP_DRIVER != 1)	//for mp driver, turn off txpwrtracking as default
	pdmpriv->TxPowerTrackControl = _TRUE;
#endif
	
	MSG_8192C("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl);
}


static VOID
DM_InitializeTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	dm_InitializeTXPowerTracking_ThermalMeter(Adapter);	
}	

//
//	Description:
//		- Dispatch TxPower Tracking direct call ONLY for 92s.
//		- We shall NOT schedule Workitem within PASSIVE LEVEL, which will cause system resource
//		   leakage under some platform.
//
//	Assumption:
//		PASSIVE_LEVEL when this routine is called.
//
//	Added by Roger, 2009.06.18.
//
static VOID
DM_TXPowerTracking92CDirectCall(
            IN	PADAPTER		Adapter)
{	
	dm_TXPowerTrackingCallback_ThermalMeter_92C(Adapter);
}

static VOID
dm_CheckTXPowerTracking_ThermalMeter(
	IN	PADAPTER		Adapter)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	//u1Byte					TxPowerCheckCnt = 5;	//10 sec

	//if(!pMgntInfo->bTXPowerTracking /*|| (!pdmpriv->TxPowerTrackControl && pdmpriv->bAPKdone)*/)
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_SS))
	{
		return;
	}

	if(!pdmpriv->TM_Trigger)		//at least delay 1 sec
	{
		//pHalData->TxPowerCheckCnt++;	//cosa add for debug
		PHY_SetRFReg(Adapter, RF_PATH_A, RF_T_METER, bRFRegOffsetMask, 0x60);
		//DBG_8192C("Trigger 92C Thermal Meter!!\n");
		
		pdmpriv->TM_Trigger = 1;
		return;
		
	}
	else
	{
		//DBG_8192C("Schedule TxPowerTracking direct call!!\n");
		DM_TXPowerTracking92CDirectCall(Adapter); //Using direct call is instead, added by Roger, 2009.06.18.
		pdmpriv->TM_Trigger = 0;
	}

}


VOID
rtl8192c_dm_CheckTXPowerTracking(
	IN	PADAPTER		Adapter)
{
	dm_CheckTXPowerTracking_ThermalMeter(Adapter);
}

#ifdef CONFIG_BT_COEXIST
static BOOLEAN BT_BTStateChange(PADAPTER Adapter)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	struct registry_priv	*registry_par = &Adapter->registrypriv;
	
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);
	
	u32 		Polling, Ratio_Tx, Ratio_PRI;
	u32 			BT_Tx, BT_PRI;
	u8			BT_State;
	static u8		ServiceTypeCnt = 0;
	u8			CurServiceType;
	static u8		LastServiceType = BT_Idle;

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)	
		return _FALSE;
	
	BT_State = rtw_read8(Adapter, 0x4fd);
/*
	temp = PlatformEFIORead4Byte(Adapter, 0x488);
	BT_Tx = (u2Byte)(((temp<<8)&0xff00)+((temp>>8)&0xff));
	BT_PRI = (u2Byte)(((temp>>8)&0xff00)+((temp>>24)&0xff));

	temp = PlatformEFIORead4Byte(Adapter, 0x48c);
	Polling = ((temp<<8)&0xff000000) + ((temp>>8)&0x00ff0000) + 
			((temp<<8)&0x0000ff00) + ((temp>>8)&0x000000ff);
	
*/
	BT_Tx = rtw_read32(Adapter, 0x488);
	
	DBG_8192C("Ratio 0x488  =%x\n", BT_Tx);
	BT_Tx =BT_Tx & 0x00ffffff;
	//RTPRINT(FBT, BT_TRACE, ("Ratio BT_Tx  =%x\n", BT_Tx));

	BT_PRI = rtw_read32(Adapter, 0x48c);
	
	DBG_8192C("Ratio 0x48c  =%x\n", BT_PRI);
	BT_PRI =BT_PRI & 0x00ffffff;
	//RTPRINT(FBT, BT_TRACE, ("Ratio BT_PRI  =%x\n", BT_PRI));


	Polling = rtw_read32(Adapter, 0x490);
	//RTPRINT(FBT, BT_TRACE, ("Ratio 0x490  =%x\n", Polling));


	if(BT_Tx==0xffffffff && BT_PRI==0xffffffff && Polling==0xffffffff && BT_State==0xff)
		return _FALSE;

	BT_State &= BIT0;

	if(BT_State != pbtpriv->BT_CUR_State)
	{
		pbtpriv->BT_CUR_State = BT_State;
	
		if(registry_par->bt_sco == 3)
		{
			ServiceTypeCnt = 0;
		
			pbtpriv->BT_Service = BT_Idle;

			DBG_8192C("BT_%s\n", BT_State?"ON":"OFF");

			BT_State = BT_State | 
					((pbtpriv->BT_Ant_isolation==1)?0:BIT1) |BIT2;

			rtw_write8(Adapter, 0x4fd, BT_State);
			DBG_8192C("BT set 0x4fd to %x\n", BT_State);
		}
		
		return _TRUE;
	}
	DBG_8192C("bRegBT_Sco =  %d\n",registry_par->bt_sco);

	Ratio_Tx = BT_Tx*1000/Polling;
	Ratio_PRI = BT_PRI*1000/Polling;

	pbtpriv->Ratio_Tx=Ratio_Tx;
	pbtpriv->Ratio_PRI=Ratio_PRI;
	
	DBG_8192C("Ratio_Tx=%d\n", Ratio_Tx);
	DBG_8192C("Ratio_PRI=%d\n", Ratio_PRI);

	
	if(BT_State && registry_par->bt_sco==3)
	{
		DBG_8192C("bt_sco  ==3 Follow Counter\n");
//		if(BT_Tx==0xffff && BT_PRI==0xffff && Polling==0xffffffff)
//		{
//			ServiceTypeCnt = 0;
//			return FALSE;
//		}
//		else
		{
		/*
			Ratio_Tx = BT_Tx*1000/Polling;
			Ratio_PRI = BT_PRI*1000/Polling;

			pHalData->bt_coexist.Ratio_Tx=Ratio_Tx;
			pHalData->bt_coexist.Ratio_PRI=Ratio_PRI;
			
			RTPRINT(FBT, BT_TRACE, ("Ratio_Tx=%d\n", Ratio_Tx));
			RTPRINT(FBT, BT_TRACE, ("Ratio_PRI=%d\n", Ratio_PRI));

		*/	
			if((Ratio_Tx < 30)  && (Ratio_PRI < 30)) 
			  	CurServiceType = BT_Idle;
			else if((Ratio_PRI > 110) && (Ratio_PRI < 250))
				CurServiceType = BT_SCO;
			else if((Ratio_Tx >= 200)&&(Ratio_PRI >= 200))
				CurServiceType = BT_Busy;
			else if((Ratio_Tx >=350) && (Ratio_Tx < 500))
				CurServiceType = BT_OtherBusy;
			else if(Ratio_Tx >=500)
				CurServiceType = BT_PAN;
			else
				CurServiceType=BT_OtherAction;
		}

/*		if(pHalData->bt_coexist.bStopCount)
		{
			ServiceTypeCnt=0;
			pHalData->bt_coexist.bStopCount=FALSE;
		}
*/
//		if(CurServiceType == BT_OtherBusy)
		{
			ServiceTypeCnt=2;
			LastServiceType=CurServiceType;
		}
#if 0
		else if(CurServiceType == LastServiceType)
		{
			if(ServiceTypeCnt<3)
				ServiceTypeCnt++;
		}
		else
		{
			ServiceTypeCnt = 0;
			LastServiceType = CurServiceType;
		}
#endif

		if(ServiceTypeCnt==2)
		{
			pbtpriv->BT_Service = LastServiceType;
			BT_State = BT_State | 
					((pbtpriv->BT_Ant_isolation==1)?0:BIT1) |
					//((pbtpriv->BT_Service==BT_SCO)?0:BIT2);
					((pbtpriv->BT_Service!=BT_Idle)?0:BIT2);

			//if(pbtpriv->BT_Service==BT_Busy)
			//	BT_State&= ~(BIT2);

			if(pbtpriv->BT_Service==BT_SCO)
			{
				DBG_8192C("BT TYPE Set to  ==> BT_SCO\n");
			}
			else if(pbtpriv->BT_Service==BT_Idle)
			{
				DBG_8192C("BT TYPE Set to  ==> BT_Idle\n");
			}
			else if(pbtpriv->BT_Service==BT_OtherAction)
			{
				DBG_8192C("BT TYPE Set to  ==> BT_OtherAction\n");
			}
			else if(pbtpriv->BT_Service==BT_Busy)
			{
				DBG_8192C("BT TYPE Set to  ==> BT_Busy\n");
			}
			else if(pbtpriv->BT_Service==BT_PAN)
			{
				DBG_8192C("BT TYPE Set to  ==> BT_PAN\n");
			}
			else
			{
				DBG_8192C("BT TYPE Set to ==> BT_OtherBusy\n");
			}
				
			//Add interrupt migration when bt is not in idel state (no traffic).
			//suggestion by Victor.
			if(pbtpriv->BT_Service!=BT_Idle)//EDCA_VI_PARAM modify
			{
			
				rtw_write16(Adapter, 0x504, 0x0ccc);
				rtw_write8(Adapter, 0x506, 0x54);
				rtw_write8(Adapter, 0x507, 0x54);
			
			}
			else
			{
				rtw_write8(Adapter, 0x506, 0x00);
				rtw_write8(Adapter, 0x507, 0x00);			
			}
				
			rtw_write8(Adapter, 0x4fd, BT_State);
			DBG_8192C("BT_SCO set 0x4fd to %x\n", BT_State);
			return _TRUE;
		}
	}

	return _FALSE;

}

static BOOLEAN
BT_WifiConnectChange(
	IN	PADAPTER	Adapter
	)
{
	struct mlme_priv *pmlmepriv = &(Adapter->mlmepriv);
//	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	static BOOLEAN	bMediaConnect = _FALSE;

	//if(!pMgntInfo->bMediaConnect || MgntRoamingInProgress(pMgntInfo))
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)	
	{
		bMediaConnect = _FALSE;
	}
	else
	{
		if(!bMediaConnect)
		{
			bMediaConnect = _TRUE;
			return _TRUE;
		}
		bMediaConnect = _TRUE;
	}

	return _FALSE;
}

#define BT_RSSI_STATE_NORMAL_POWER	BIT0
#define BT_RSSI_STATE_AMDPU_OFF		BIT1
#define BT_RSSI_STATE_SPECIAL_LOW	BIT2
#define BT_RSSI_STATE_BG_EDCA_LOW	BIT3

static s32 GET_UNDECORATED_AVERAGE_RSSI(PADAPTER	Adapter)	
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32 	average_rssi;
	
	if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE|WIFI_AP_STATE))
	{	
		average_rssi = 	pdmpriv->EntryMinUndecoratedSmoothedPWDB;	
	}
	else
	{
		average_rssi = 	pdmpriv->UndecoratedSmoothedPWDB;
	}
	return average_rssi;
}

static u8 BT_RssiStateChange(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv		*pmlmepriv = &(Adapter->mlmepriv);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	//PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	s32			UndecoratedSmoothedPWDB;
	u8			CurrBtRssiState = 0x00;




	//if(pMgntInfo->bMediaConnect)	// Default port
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	
	{
		UndecoratedSmoothedPWDB = GET_UNDECORATED_AVERAGE_RSSI(Adapter);
	}
	else // associated entry pwdb
	{
		if(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0)
			UndecoratedSmoothedPWDB = 100;	// No any RSSI information. Assume to be MAX.
		else
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	}

	// Check RSSI to determine HighPower/NormalPower state for BT coexistence.
	if(UndecoratedSmoothedPWDB >= 67)
		CurrBtRssiState &= (~BT_RSSI_STATE_NORMAL_POWER);
	else if(UndecoratedSmoothedPWDB < 62)
		CurrBtRssiState |= BT_RSSI_STATE_NORMAL_POWER;

	// Check RSSI to determine AMPDU setting for BT coexistence.
	if(UndecoratedSmoothedPWDB >= 40)
		CurrBtRssiState &= (~BT_RSSI_STATE_AMDPU_OFF);
	else if(UndecoratedSmoothedPWDB <= 32)
		CurrBtRssiState |= BT_RSSI_STATE_AMDPU_OFF;

	// Marked RSSI state. It will be used to determine BT coexistence setting later.
	if(UndecoratedSmoothedPWDB < 35)
		CurrBtRssiState |=  BT_RSSI_STATE_SPECIAL_LOW;
	else
		CurrBtRssiState &= (~BT_RSSI_STATE_SPECIAL_LOW);

	// Check BT state related to BT_Idle in B/G mode.
	if(UndecoratedSmoothedPWDB < 15)
		CurrBtRssiState |=  BT_RSSI_STATE_BG_EDCA_LOW;
	else
		CurrBtRssiState &= (~BT_RSSI_STATE_BG_EDCA_LOW);
	
	if(CurrBtRssiState != pbtpriv->BtRssiState)
	{
		pbtpriv->BtRssiState = CurrBtRssiState;
		return _TRUE;
	}
	else
	{
		return _FALSE;
	}
}

static void dm_BTCoexist(PADAPTER Adapter )
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv			*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	 		*pmlmepriv = &(Adapter->mlmepriv);
	struct mlme_ext_info		*pmlmeinfo = &Adapter->mlmeextpriv.mlmext_info;
	struct mlme_ext_priv		*pmlmeext = &Adapter->mlmeextpriv;

	struct btcoexist_priv		*pbtpriv = &(pHalData->bt_coexist);
	//PMGNT_INFO				pMgntInfo = &Adapter->MgntInfo;
	//PRT_HIGH_THROUGHPUT		pHTInfo = GET_HT_INFO(pMgntInfo);
	
	//PRX_TS_RECORD	pRxTs = NULL;
	u8			BT_gpio_mux;

	BOOLEAN		bWifiConnectChange, bBtStateChange,bRssiStateChange;

	if(pbtpriv->bCOBT == _FALSE)		return;

	if(!( pdmpriv->DMFlag & DYNAMIC_FUNC_BT)) return;
	
	if( (pbtpriv->BT_Coexist) &&(pbtpriv->BT_CoexistType == BT_CSR_BC4) && (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _FALSE)	)
	{
		bWifiConnectChange = BT_WifiConnectChange(Adapter);
		bBtStateChange	= BT_BTStateChange(Adapter);
		bRssiStateChange 	= BT_RssiStateChange(Adapter);
		
		DBG_8192C("bWifiConnectChange %d, bBtStateChange  %d,bRssiStateChange  %d\n",
			bWifiConnectChange,bBtStateChange,bRssiStateChange);

		// add by hpfan for debug message
		BT_gpio_mux = rtw_read8(Adapter, REG_GPIO_MUXCFG);
		DBG_8192C("BTCoexit Reg_0x40 (%2x)\n", BT_gpio_mux);

		if( bWifiConnectChange ||bBtStateChange  ||bRssiStateChange )
		{
			if(pbtpriv->BT_CUR_State)
			{
				
				// Do not allow receiving A-MPDU aggregation.
				if(pbtpriv->BT_Ampdu)// 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU.
				{
			
					if(pmlmeinfo->assoc_AP_vendor == ciscoAP)	
					{
						if(pbtpriv->BT_Service!=BT_Idle)
						{
							if(pmlmeinfo->bAcceptAddbaReq)
							{
								DBG_8192C("BT_Disallow AMPDU \n");	
								pmlmeinfo->bAcceptAddbaReq = _FALSE;
								send_delba(Adapter,0, get_my_bssid(&(pmlmeinfo->network)));
							}
						}
						else
						{
							if(!pmlmeinfo->bAcceptAddbaReq)
							{
								DBG_8192C("BT_Allow AMPDU  RSSI >=40\n");	
								pmlmeinfo->bAcceptAddbaReq = _TRUE;
							}
						}
					}
					else
					{
						if(!pmlmeinfo->bAcceptAddbaReq)
						{
							DBG_8192C("BT_Allow AMPDU BT Idle\n");	
							pmlmeinfo->bAcceptAddbaReq = _TRUE;
						}
					}
				}
				
#if 0
				else if((pHalData->bt_coexist.BT_Service==BT_SCO) || (pHalData->bt_coexist.BT_Service==BT_Busy))
				{				
					if(pHalData->bt_coexist.BtRssiState & BT_RSSI_STATE_AMDPU_OFF)
					{
						if(pMgntInfo->bBT_Ampdu && pHTInfo->bAcceptAddbaReq)
						{
							RTPRINT(FBT, BT_TRACE, ("BT_Disallow AMPDU RSSI <=32\n"));	
							pHTInfo->bAcceptAddbaReq = FALSE;
							if(GetTs(Adapter, (PTS_COMMON_INFO*)(&pRxTs), pMgntInfo->Bssid, 0, RX_DIR, FALSE))
								TsInitDelBA(Adapter, (PTS_COMMON_INFO)pRxTs, RX_DIR);
						}
					}
					else
					{
						if(pMgntInfo->bBT_Ampdu && !pHTInfo->bAcceptAddbaReq)
						{
							RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU  RSSI >=40\n"));	
							pHTInfo->bAcceptAddbaReq = TRUE;
						}
					}
				}
				else
				{
					if(pMgntInfo->bBT_Ampdu && !pHTInfo->bAcceptAddbaReq)
					{
						RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU BT not in SCO or BUSY\n"));	
						pHTInfo->bAcceptAddbaReq = TRUE;
					}
				}
#endif

				if(pbtpriv->BT_Ant_isolation)
				{			
					DBG_8192C("BT_IsolationLow\n");

// 20100427 Joseph: Do not adjust Rate adaptive for BT coexist suggested by SD3.
#if 0
					RTPRINT(FBT, BT_TRACE, ("BT_Update Rate table\n"));
					if(pMgntInfo->bUseRAMask)
					{
						// 20100407 Joseph: Fix rate adaptive modification for BT coexist.
						// This fix is not complete yet. It shall also consider VWifi and Adhoc case,
						// which connect with multiple STAs.
						Adapter->HalFunc.UpdateHalRAMaskHandler(
												Adapter,
												FALSE,
												0,
												NULL,
												NULL,
												pMgntInfo->RateAdaptive.RATRState,
												RAMask_Normal);
					}
					else
					{
						Adapter->HalFunc.UpdateHalRATRTableHandler(
									Adapter, 
									&pMgntInfo->dot11OperationalRateSet,
									pMgntInfo->dot11HTOperationalRateSet,NULL);
					}
#endif

					// 20100415 Joseph: Modify BT coexist mechanism suggested by Yaying.
					// Now we only enable HW BT coexist when BT in "Busy" state.
					if(1)//pMgntInfo->LinkDetectInfo.NumRecvDataInPeriod >= 20)
					{
						if((pmlmeinfo->assoc_AP_vendor == ciscoAP)	&&
							pbtpriv->BT_Service==BT_OtherAction)
						{
							DBG_8192C("BT_Turn ON Coexist\n");
							rtw_write8(Adapter, REG_GPIO_MUXCFG, 0xa0);	
						}
						else
						{
							if((pbtpriv->BT_Service==BT_Busy) &&
								(pbtpriv->BtRssiState & BT_RSSI_STATE_NORMAL_POWER))
							{
								DBG_8192C("BT_Turn ON Coexist\n");
								rtw_write8(Adapter, REG_GPIO_MUXCFG, 0xa0);
							}
							else if((pbtpriv->BT_Service==BT_OtherAction) &&
									(pbtpriv->BtRssiState & BT_RSSI_STATE_SPECIAL_LOW))
							{
								DBG_8192C("BT_Turn ON Coexist\n");
								rtw_write8(Adapter, REG_GPIO_MUXCFG, 0xa0);	
							}
							else if(pbtpriv->BT_Service==BT_PAN)
							{
								DBG_8192C("BT_Turn ON Coexist\n");
								rtw_write8(Adapter, REG_GPIO_MUXCFG, 0x00);	
							}
							else
							{
								DBG_8192C("BT_Turn OFF Coexist\n");
								rtw_write8(Adapter, REG_GPIO_MUXCFG, 0x00);
							}
						}
					}
					else
					{
						DBG_8192C("BT: There is no Wifi traffic!! Turn off Coexist\n");
						rtw_write8(Adapter, REG_GPIO_MUXCFG, 0x00);
					}

					if(1)//pMgntInfo->LinkDetectInfo.NumRecvDataInPeriod >= 20)
					{
						if(pbtpriv->BT_Service==BT_PAN)
						{
							DBG_8192C("BT_Turn ON Coexist(Reg0x44 = 0x10100)\n");
							rtw_write32(Adapter, REG_GPIO_PIN_CTRL, 0x10100);	
						}
						else
						{
							DBG_8192C("BT_Turn OFF Coexist(Reg0x44 = 0x0)\n");
							rtw_write32(Adapter, REG_GPIO_PIN_CTRL, 0x0);	
						}
					}
					else
					{
						DBG_8192C("BT: There is no Wifi traffic!! Turn off Coexist(Reg0x44 = 0x0)\n");
						rtw_write32(Adapter, REG_GPIO_PIN_CTRL, 0x0);	
					}

					// 20100430 Joseph: Integrate the BT coexistence EDCA tuning here.
					if(pbtpriv->BtRssiState & BT_RSSI_STATE_NORMAL_POWER)
					{
						if(pbtpriv->BT_Service==BT_OtherBusy)
						{
							//pbtpriv->BtEdcaUL = 0x5ea72b;
							//pbtpriv->BtEdcaDL = 0x5ea72b;
							pbtpriv->BT_EDCA[UP_LINK] = 0x5ea72b;
							pbtpriv->BT_EDCA[DOWN_LINK] = 0x5ea72b;							
							
							DBG_8192C("BT in BT_OtherBusy state Tx (%d) >350 parameter(0x%x) = 0x%x\n", pbtpriv->Ratio_Tx ,REG_EDCA_BE_PARAM, 0x5ea72b);
						}
						else if(pbtpriv->BT_Service==BT_Busy)
						{
							//pbtpriv->BtEdcaUL = 0x5eb82f;
							//pbtpriv->BtEdcaDL = 0x5eb82f;

							pbtpriv->BT_EDCA[UP_LINK] = 0x5eb82f;
							pbtpriv->BT_EDCA[DOWN_LINK] = 0x5eb82f;							
							
							DBG_8192C("BT in BT_Busy state parameter(0x%x) = 0x%x\n", REG_EDCA_BE_PARAM, 0x5eb82f);		
						}
						else if(pbtpriv->BT_Service==BT_SCO)
						{
							if(pbtpriv->Ratio_Tx>160)
							{
								//pbtpriv->BtEdcaUL = 0x5ea72f;
								//pbtpriv->BtEdcaDL = 0x5ea72f;
								pbtpriv->BT_EDCA[UP_LINK] = 0x5ea72f;
								pbtpriv->BT_EDCA[DOWN_LINK] = 0x5ea72f;							
								DBG_8192C("BT in BT_SCO state Tx (%d) >160 parameter(0x%x) = 0x%x\n",pbtpriv->Ratio_Tx, REG_EDCA_BE_PARAM, 0x5ea72f);
							}
							else
							{
								//pbtpriv->BtEdcaUL = 0x5ea32b;
								//pbtpriv->BtEdcaDL = 0x5ea42b;

								pbtpriv->BT_EDCA[UP_LINK] = 0x5ea32b;
								pbtpriv->BT_EDCA[DOWN_LINK] = 0x5ea42b;						
								
								DBG_8192C("BT in BT_SCO state Tx (%d) <160 parameter(0x%x) = 0x%x\n", pbtpriv->Ratio_Tx,REG_EDCA_BE_PARAM, 0x5ea32f);
							}									
						}
						else
						{
							// BT coexistence mechanism does not control EDCA parameter.
							//pbtpriv->BtEdcaUL = 0;
							//pbtpriv->BtEdcaDL = 0;

							pbtpriv->BT_EDCA[UP_LINK] = 0;
							pbtpriv->BT_EDCA[DOWN_LINK] = 0;							
							DBG_8192C("BT in  State  %d  and parameter(0x%x) use original setting.\n",pbtpriv->BT_Service, REG_EDCA_BE_PARAM);
						}

						if((pbtpriv->BT_Service!=BT_Idle) &&
							(pmlmeext->cur_wireless_mode  == WIRELESS_MODE_G) &&
							(pbtpriv->BtRssiState & BT_RSSI_STATE_BG_EDCA_LOW))
						{
							//pbtpriv->BtEdcaUL = 0x5eb82b;
							//pbtpriv->BtEdcaDL = 0x5eb82b;

							pbtpriv->BT_EDCA[UP_LINK] = 0x5eb82b;
							pbtpriv->BT_EDCA[DOWN_LINK] = 0x5eb82b;							
							
							DBG_8192C("BT set parameter(0x%x) = 0x%x\n", REG_EDCA_BE_PARAM, 0x5eb82b);		
						}
					}
					else
					{
						// BT coexistence mechanism does not control EDCA parameter.
						//pbtpriv->BtEdcaUL = 0;
						//pbtpriv->BtEdcaDL = 0;

						pbtpriv->BT_EDCA[UP_LINK] = 0;
						pbtpriv->BT_EDCA[DOWN_LINK] = 0;					
					}

					// 20100415 Joseph: Set RF register 0x1E and 0x1F for BT coexist suggested by Yaying.
					if(pbtpriv->BT_Service!=BT_Idle)
					{
						DBG_8192C("BT Set RfReg0x1E[7:4] = 0x%x \n", 0xf);
						PHY_SetRFReg(Adapter, PathA, 0x1e, 0xf0, 0xf);
						//RTPRINT(FBT, BT_TRACE, ("BT Set RfReg0x1E[7:4] = 0x%x \n", 0xf));
						//PHY_SetRFReg(Adapter, PathA, 0x1f, 0xf0, 0xf);
					}
					else
					{
						DBG_8192C("BT Set RfReg0x1E[7:4] = 0x%x \n",pbtpriv->BtRfRegOrigin1E);
						PHY_SetRFReg(Adapter, PathA, 0x1e, 0xf0, pbtpriv->BtRfRegOrigin1E);
						//RTPRINT(FBT, BT_TRACE, ("BT Set RfReg0x1F[7:4] = 0x%x \n", pHalData->bt_coexist.BtRfRegOrigin1F));
						//PHY_SetRFReg(Adapter, PathA, 0x1f, 0xf0, pHalData->bt_coexist.BtRfRegOrigin1F);
					}	
				}
				else
				{
					DBG_8192C("BT_IsolationHigh\n");
					// Do nothing.
				}			
			}
			else
			{
			
				if(pbtpriv->BT_Ampdu && !pmlmeinfo->bAcceptAddbaReq)
				{
					DBG_8192C("BT_Allow AMPDU bt is off\n");	
					pmlmeinfo->bAcceptAddbaReq = _TRUE;
				}
			
				DBG_8192C("BT_Turn OFF Coexist bt is off \n");
				rtw_write8(Adapter, REG_GPIO_MUXCFG, 0x00);

				DBG_8192C("BT Set RfReg0x1E[7:4] = 0x%x \n", pbtpriv->BtRfRegOrigin1E);
				PHY_SetRFReg(Adapter, PathA, 0x1e, 0xf0, pbtpriv->BtRfRegOrigin1E);
				//RTPRINT(FBT, BT_TRACE, ("BT Set RfReg0x1F[7:4] = 0x%x \n", pHalData->bt_coexist.BtRfRegOrigin1F));
				//PHY_SetRFReg(Adapter, PathA, 0x1f, 0xf0, pHalData->bt_coexist.BtRfRegOrigin1F);

				// BT coexistence mechanism does not control EDCA parameter since BT is disabled.
				//pbtpriv->BtEdcaUL = 0;
				//pbtpriv->BtEdcaDL = 0;
				pbtpriv->BT_EDCA[UP_LINK] = 0;
				pbtpriv->BT_EDCA[DOWN_LINK] = 0;				
				

// 20100427 Joseph: Do not adjust Rate adaptive for BT coexist suggested by SD3.
#if 0
				RTPRINT(FBT, BT_TRACE, ("BT_Update Rate table\n"));
				if(pMgntInfo->bUseRAMask)
				{
					// 20100407 Joseph: Fix rate adaptive modification for BT coexist.
					// This fix is not complete yet. It shall also consider VWifi and Adhoc case,
					// which connect with multiple STAs.
					Adapter->HalFunc.UpdateHalRAMaskHandler(
											Adapter,
											FALSE,
											0,
											NULL,
											NULL,
											pMgntInfo->RateAdaptive.RATRState,
											RAMask_Normal);
				}
				else
				{
					Adapter->HalFunc.UpdateHalRATRTableHandler(
								Adapter, 
								&pMgntInfo->dot11OperationalRateSet,
								pMgntInfo->dot11HTOperationalRateSet,NULL);
				}
#endif
			}
		}
	}
}

static void dm_InitBtCoexistDM(	PADAPTER	Adapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);

	if( !pbtpriv->BT_Coexist ) return;
	
	pbtpriv->BtRfRegOrigin1E = (u8)PHY_QueryRFReg(Adapter, PathA, 0x1e, 0xf0);
	pbtpriv->BtRfRegOrigin1F = (u8)PHY_QueryRFReg(Adapter, PathA, 0x1f, 0xf0);
}

void rtl8192c_set_dm_bt_coexist(_adapter *padapter, u8 bStart)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	
	pbtpriv->bCOBT = bStart;
	send_delba(padapter,0, get_my_bssid(&(pmlmeinfo->network)));
	send_delba(padapter,1, get_my_bssid(&(pmlmeinfo->network)));
	
}

void rtl8192c_issue_delete_ba(_adapter *padapter, u8 dir)
{
	struct mlme_ext_info		*pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;
	DBG_8192C("issue_delete_ba : %s...\n",(dir==0)?"RX_DIR":"TX_DIR");
	send_delba(padapter,dir, get_my_bssid(&(pmlmeinfo->network)));
}

#endif

#if 0//def CONFIG_PCI_HCI

BOOLEAN
BT_BTStateChange(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	
	u4Byte 			temp, Polling, Ratio_Tx, Ratio_PRI;
	u4Byte 			BT_Tx, BT_PRI;
	u1Byte			BT_State;
	static u1Byte		ServiceTypeCnt = 0;
	u1Byte			CurServiceType;
	static u1Byte		LastServiceType = BT_Idle;

	if(!pMgntInfo->bMediaConnect)
		return FALSE;
	
	BT_State = PlatformEFIORead1Byte(Adapter, 0x4fd);
/*
	temp = PlatformEFIORead4Byte(Adapter, 0x488);
	BT_Tx = (u2Byte)(((temp<<8)&0xff00)+((temp>>8)&0xff));
	BT_PRI = (u2Byte)(((temp>>8)&0xff00)+((temp>>24)&0xff));

	temp = PlatformEFIORead4Byte(Adapter, 0x48c);
	Polling = ((temp<<8)&0xff000000) + ((temp>>8)&0x00ff0000) + 
			((temp<<8)&0x0000ff00) + ((temp>>8)&0x000000ff);
	
*/
	BT_Tx = PlatformEFIORead4Byte(Adapter, 0x488);
	
	RTPRINT(FBT, BT_TRACE, ("Ratio 0x488  =%x\n", BT_Tx));
	BT_Tx =BT_Tx & 0x00ffffff;
	//RTPRINT(FBT, BT_TRACE, ("Ratio BT_Tx  =%x\n", BT_Tx));

	BT_PRI = PlatformEFIORead4Byte(Adapter, 0x48c);
	
	RTPRINT(FBT, BT_TRACE, ("Ratio Ratio 0x48c  =%x\n", BT_PRI));
	BT_PRI =BT_PRI & 0x00ffffff;
	//RTPRINT(FBT, BT_TRACE, ("Ratio BT_PRI  =%x\n", BT_PRI));


	Polling = PlatformEFIORead4Byte(Adapter, 0x490);
	//RTPRINT(FBT, BT_TRACE, ("Ratio 0x490  =%x\n", Polling));


	if(BT_Tx==0xffffffff && BT_PRI==0xffffffff && Polling==0xffffffffff && BT_State==0xff)
		return FALSE;

	BT_State &= BIT0;

	if(BT_State != pHalData->bt_coexist.BT_CUR_State)
	{
		pHalData->bt_coexist.BT_CUR_State = BT_State;
	
		if(pMgntInfo->bRegBT_Sco == 3)
		{
			ServiceTypeCnt = 0;
		
			pHalData->bt_coexist.BT_Service = BT_Idle;

			RTPRINT(FBT, BT_TRACE, ("BT_%s\n", BT_State?"ON":"OFF"));

			BT_State = BT_State | 
					((pHalData->bt_coexist.BT_Ant_isolation==1)?0:BIT1) |BIT2;

			PlatformEFIOWrite1Byte(Adapter, 0x4fd, BT_State);
			RTPRINT(FBT, BT_TRACE, ("BT set 0x4fd to %x\n", BT_State));
		}
		
		return TRUE;
	}
	RTPRINT(FBT, BT_TRACE, ("bRegBT_Sco   %d\n", pMgntInfo->bRegBT_Sco));

	Ratio_Tx = BT_Tx*1000/Polling;
	Ratio_PRI = BT_PRI*1000/Polling;

	pHalData->bt_coexist.Ratio_Tx=Ratio_Tx;
	pHalData->bt_coexist.Ratio_PRI=Ratio_PRI;
	
	RTPRINT(FBT, BT_TRACE, ("Ratio_Tx=%d\n", Ratio_Tx));
	RTPRINT(FBT, BT_TRACE, ("Ratio_PRI=%d\n", Ratio_PRI));

	
	if(BT_State && pMgntInfo->bRegBT_Sco==3)
	{
		RTPRINT(FBT, BT_TRACE, ("bRegBT_Sco  ==3 Follow Counter\n"));
//		if(BT_Tx==0xffff && BT_PRI==0xffff && Polling==0xffffffff)
//		{
//			ServiceTypeCnt = 0;
//			return FALSE;
//		}
//		else
		{
		/*
			Ratio_Tx = BT_Tx*1000/Polling;
			Ratio_PRI = BT_PRI*1000/Polling;

			pHalData->bt_coexist.Ratio_Tx=Ratio_Tx;
			pHalData->bt_coexist.Ratio_PRI=Ratio_PRI;
			
			RTPRINT(FBT, BT_TRACE, ("Ratio_Tx=%d\n", Ratio_Tx));
			RTPRINT(FBT, BT_TRACE, ("Ratio_PRI=%d\n", Ratio_PRI));

		*/	
			if((Ratio_Tx <= 50)  && (Ratio_PRI <= 50)) 
			  	CurServiceType = BT_Idle;
			else if((Ratio_PRI > 150) && (Ratio_PRI < 200))
				CurServiceType = BT_SCO;
			else if((Ratio_Tx >= 200)&&(Ratio_PRI >= 200))
				CurServiceType = BT_Busy;
			else if(Ratio_Tx >= 350)
				CurServiceType = BT_OtherBusy;
			else
				CurServiceType=BT_OtherAction;

		}
/*		if(pHalData->bt_coexist.bStopCount)
		{
			ServiceTypeCnt=0;
			pHalData->bt_coexist.bStopCount=FALSE;
		}
*/
		if(CurServiceType == BT_OtherBusy)
		{
			ServiceTypeCnt=2;
			LastServiceType=CurServiceType;
		}
		else if(CurServiceType == LastServiceType)
		{
			if(ServiceTypeCnt<3)
				ServiceTypeCnt++;
		}
		else
		{
			ServiceTypeCnt = 0;
			LastServiceType = CurServiceType;
		}

		if(ServiceTypeCnt==2)
		{
			pHalData->bt_coexist.BT_Service = LastServiceType;
			BT_State = BT_State | 
					((pHalData->bt_coexist.BT_Ant_isolation==1)?0:BIT1) |
					((pHalData->bt_coexist.BT_Service==BT_SCO)?0:BIT2);

			if(pHalData->bt_coexist.BT_Service==BT_Busy)
				BT_State&= ~(BIT2);

			if(pHalData->bt_coexist.BT_Service==BT_SCO)
			{
				RTPRINT(FBT, BT_TRACE, ("BT TYPE Set to  ==> BT_SCO\n"));
			}
			else if(pHalData->bt_coexist.BT_Service==BT_Idle)
			{
				RTPRINT(FBT, BT_TRACE, ("BT TYPE Set to  ==> BT_Idle\n"));
			}
			else if(pHalData->bt_coexist.BT_Service==BT_OtherAction)
			{
				RTPRINT(FBT, BT_TRACE, ("BT TYPE Set to  ==> BT_OtherAction\n"));
			}
			else if(pHalData->bt_coexist.BT_Service==BT_Busy)
			{
				RTPRINT(FBT, BT_TRACE, ("BT TYPE Set to  ==> BT_Busy\n"));
			}
			else
			{
				RTPRINT(FBT, BT_TRACE, ("BT TYPE Set to ==> BT_OtherBusy\n"));
			}
				
			//Add interrupt migration when bt is not in idel state (no traffic).
			//suggestion by Victor.
			if(pHalData->bt_coexist.BT_Service!=BT_Idle)
			{
			
				PlatformEFIOWrite2Byte(Adapter, 0x504, 0x0ccc);
				PlatformEFIOWrite1Byte(Adapter, 0x506, 0x54);
				PlatformEFIOWrite1Byte(Adapter, 0x507, 0x54);
			
			}
			else
			{
				PlatformEFIOWrite1Byte(Adapter, 0x506, 0x00);
				PlatformEFIOWrite1Byte(Adapter, 0x507, 0x00);			
			}
				
			PlatformEFIOWrite1Byte(Adapter, 0x4fd, BT_State);
			RTPRINT(FBT, BT_TRACE, ("BT_SCO set 0x4fd to %x\n", BT_State));
			return TRUE;
		}
	}

	return FALSE;

}

BOOLEAN
BT_WifiConnectChange(
	IN	PADAPTER	Adapter
	)
{
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	static BOOLEAN	bMediaConnect = FALSE;

	if(!pMgntInfo->bMediaConnect || MgntRoamingInProgress(pMgntInfo))
	{
		bMediaConnect = FALSE;
	}
	else
	{
		if(!bMediaConnect)
		{
			bMediaConnect = TRUE;
			return TRUE;
		}
		bMediaConnect = TRUE;
	}

	return FALSE;
}

BOOLEAN
BT_RSSIChangeWithAMPDU(
	IN	PADAPTER	Adapter
	)
{
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(!Adapter->pNdisCommon->bRegBT_Ampdu || !Adapter->pNdisCommon->bRegAcceptAddbaReq)
		return FALSE;

	RTPRINT(FBT, BT_TRACE, ("RSSI is %d\n",pHalData->UndecoratedSmoothedPWDB));
	
	if((pHalData->UndecoratedSmoothedPWDB<=32) && pMgntInfo->pHTInfo->bAcceptAddbaReq)
	{
		RTPRINT(FBT, BT_TRACE, ("BT_Disallow AMPDU RSSI <=32  Need change\n"));				
		return TRUE;

	}
	else if((pHalData->UndecoratedSmoothedPWDB>=40) && !pMgntInfo->pHTInfo->bAcceptAddbaReq )
	{
		RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU RSSI >=40, Need change\n"));
		return TRUE;
	}
	else 
		return FALSE;

}


VOID
dm_BTCoexist(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	static u1Byte		LastTxPowerLvl = 0xff;
	PRX_TS_RECORD	pRxTs = NULL;

	BOOLEAN			bWifiConnectChange, bBtStateChange,bRSSIChangeWithAMPDU;

	if( (pHalData->bt_coexist.BluetoothCoexist) &&
		(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4) && 
		(!ACTING_AS_AP(Adapter))	)
	{
		bWifiConnectChange = BT_WifiConnectChange(Adapter);
		bBtStateChange = BT_BTStateChange(Adapter);
		bRSSIChangeWithAMPDU = BT_RSSIChangeWithAMPDU(Adapter);
		RTPRINT(FBT, BT_TRACE, ("bWifiConnectChange %d, bBtStateChange  %d,LastTxPowerLvl  %x,  DynamicTxHighPowerLvl  %x\n",
			bWifiConnectChange,bBtStateChange,LastTxPowerLvl,pHalData->DynamicTxHighPowerLvl));
		if( bWifiConnectChange ||bBtStateChange  ||
			(LastTxPowerLvl != pHalData->DynamicTxHighPowerLvl)	||bRSSIChangeWithAMPDU)
		{
			LastTxPowerLvl = pHalData->DynamicTxHighPowerLvl;

			if(pHalData->bt_coexist.BT_CUR_State)
			{
				// Do not allow receiving A-MPDU aggregation.
				if((pHalData->bt_coexist.BT_Service==BT_SCO) || (pHalData->bt_coexist.BT_Service==BT_Busy))
				{
					if(pHalData->UndecoratedSmoothedPWDB<=32)
					{
						if(Adapter->pNdisCommon->bRegBT_Ampdu && Adapter->pNdisCommon->bRegAcceptAddbaReq)
						{
							RTPRINT(FBT, BT_TRACE, ("BT_Disallow AMPDU RSSI <=32\n"));	
							pMgntInfo->pHTInfo->bAcceptAddbaReq = FALSE;
							if(GetTs(Adapter, (PTS_COMMON_INFO*)(&pRxTs), pMgntInfo->Bssid, 0, RX_DIR, FALSE))
								TsInitDelBA(Adapter, (PTS_COMMON_INFO)pRxTs, RX_DIR);
						}
					}
					else if(pHalData->UndecoratedSmoothedPWDB>=40)
					{
						if(Adapter->pNdisCommon->bRegBT_Ampdu && Adapter->pNdisCommon->bRegAcceptAddbaReq)
						{
							RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU  RSSI >=40\n"));	
							pMgntInfo->pHTInfo->bAcceptAddbaReq = TRUE;
						}
					}
				}
				else
				{
					if(Adapter->pNdisCommon->bRegBT_Ampdu && Adapter->pNdisCommon->bRegAcceptAddbaReq)
					{
						RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU BT not in SCO or BUSY\n"));	
						pMgntInfo->pHTInfo->bAcceptAddbaReq = TRUE;
					}
				}

				if(pHalData->bt_coexist.BT_Ant_isolation)
				{			
					RTPRINT(FBT, BT_TRACE, ("BT_IsolationLow\n"));
					RTPRINT(FBT, BT_TRACE, ("BT_Update Rate table\n"));
					Adapter->HalFunc.UpdateHalRATRTableHandler(
								Adapter, 
								&pMgntInfo->dot11OperationalRateSet,
								pMgntInfo->dot11HTOperationalRateSet,NULL);
					
					if(pHalData->bt_coexist.BT_Service==BT_SCO)
					{

						RTPRINT(FBT, BT_TRACE, ("BT_Turn OFF Coexist with SCO \n"));
						PlatformEFIOWrite1Byte(Adapter, REG_GPIO_MUXCFG, 0x14);					
					}
					else if(pHalData->DynamicTxHighPowerLvl == TxHighPwrLevel_Normal)
					{
						RTPRINT(FBT, BT_TRACE, ("BT_Turn ON Coexist\n"));
						PlatformEFIOWrite1Byte(Adapter, REG_GPIO_MUXCFG, 0xb4);
					}
					else
					{
						RTPRINT(FBT, BT_TRACE, ("BT_Turn OFF Coexist\n"));
						PlatformEFIOWrite1Byte(Adapter, REG_GPIO_MUXCFG, 0x14);
					}
				}
				else
				{
					RTPRINT(FBT, BT_TRACE, ("BT_IsolationHigh\n"));
					// Do nothing.
				}
			}
			else
			{
				if(Adapter->pNdisCommon->bRegBT_Ampdu && Adapter->pNdisCommon->bRegAcceptAddbaReq)
				{
					RTPRINT(FBT, BT_TRACE, ("BT_Allow AMPDU bt is off\n"));	
					pMgntInfo->pHTInfo->bAcceptAddbaReq = TRUE;
				}

				RTPRINT(FBT, BT_TRACE, ("BT_Turn OFF Coexist bt is off \n"));
				PlatformEFIOWrite1Byte(Adapter, REG_GPIO_MUXCFG, 0x14);

				RTPRINT(FBT, BT_TRACE, ("BT_Update Rate table\n"));
				Adapter->HalFunc.UpdateHalRATRTableHandler(
							Adapter, 
							&pMgntInfo->dot11OperationalRateSet,
							pMgntInfo->dot11HTOperationalRateSet,NULL);
			}
		}
	}
}
#endif


/*-----------------------------------------------------------------------------
 * Function:	dm_CheckRfCtrlGPIO()
 *
 * Overview:	Copy 8187B template for 9xseries.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	01/10/2008	MHC		Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
static VOID
dm_CheckRfCtrlGPIO(
	IN	PADAPTER	Adapter
	)
{
#if 0
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
#if defined (CONFIG_USB_HCI) || defined (CONFIG_SDIO_HCI)
	#ifdef CONFIG_USB_HCI
		// 2010/08/12 MH Add for CU selective suspend.
		PRT_USB_DEVICE		pDevice = GET_RT_USB_DEVICE(Adapter);
	#else
		PRT_SDIO_DEVICE	pDevice = GET_RT_SDIO_DEVICE(Adapter);
	#endif
#endif

	if(!Adapter->MgntInfo.PowerSaveControl.bGpioRfSw)
		return;

	RTPRINT(FPWR, PWRHW, ("dm_CheckRfCtrlGPIO \n"));

#if defined (CONFIG_USB_HCI) || defined (CONFIG_SDIO_HCI)
	// Walk around for DTM test, we will not enable HW - radio on/off because r/w
	// page 1 register before Lextra bus is enabled cause system fails when resuming
	// from S4. 20080218, Emily
	if(Adapter->bInHctTest)
		return;

//#if ((HAL_CODE_BASE == RTL8192_S) )
	//Adapter->HalFunc.GPIOChangeRFHandler(Adapter, GPIORF_POLLING);
//#else
	// 2010/07/27 MH Only Minicard and support selective suspend, we can not turn off all MAC power to 
	// stop 8051. For dongle and minicard, we both support selective suspend mode.
	//if(pDevice->RegUsbSS && Adapter->HalFunc.GetInterfaceSelectionHandler(Adapter) == INTF_SEL2_MINICARD)

	//
	// 2010/08/12 MH We support severl power consumption combination as below.
	//
	// Power consumption combination  
	//	SS Enable: (LPS disable + IPS + SW/HW radio off)
	//	1. Dongle + PDN  (support HW radio off)
	//	2. Dongle + Normal  (No HW radio off)
	//	3. MiniCard + PDN (support HW radio off)
	//	4. MiniCard + Normal (support HW radio off)
	//
	//	SS Disable: (LPS + IPS + SW/HW radio off)
	//	1. Dongle + PDN  (support HW radio off)
	//	2. Dongle + Normal  (No HW radio off)
	//	3. MiniCard + PDN (support HW radio off)
	//	4. MiniCard + Normal (support HW radio off)
	//
	//	For Power down module detection. We need to read power register no matter
	//	dongle or minicard, we will add the item is the detection method.
	//
	//
	//vivi add du case
	if ((IS_HARDWARE_TYPE_8192CU(Adapter)||IS_HARDWARE_TYPE_8192DU(Adapter))
		&& pDevice->RegUsbSS)
	{
		RT_TRACE(COMP_RF, DBG_LOUD, ("USB SS Enabled\n"));
		if (SUPPORT_HW_RADIO_DETECT(Adapter))
		{	// Support HW radio detection
			RT_TRACE(COMP_RF, DBG_LOUD, ("USB Card Type 2/3/4 support GPIO Detect\n"));
			GpioDetectTimerStart(Adapter);
		}
		else
		{	// Dongle does not support HW radio detection.?? In the fufure??
			RT_TRACE(COMP_RF, DBG_LOUD, ("USB DONGLE Non-GPIO-Detect\n"));				
		}			
	}
	else if (IS_HARDWARE_TYPE_8192CU(Adapter) ||
		IS_HARDWARE_TYPE_8723AU(Adapter)||
		IS_HARDWARE_TYPE_8192DU(Adapter) ||
		IS_HARDWARE_TYPE_8723AS(Adapter))
	{	// Not support Selective suspend 
		RT_TRACE(COMP_RF, DBG_LOUD, ("USB SS Disable\n"));
		if (SUPPORT_HW_RADIO_DETECT(Adapter))
		{
			RT_TRACE(COMP_RF, DBG_LOUD, ("USB Card Type 2/3/4 support GPIO Detect\n"));
			PlatformScheduleWorkItem( &(pHalData->GPIOChangeRFWorkItem) );
		}
		else
		{
			RT_TRACE(COMP_RF, DBG_LOUD, ("USB DONGLE Non-GPIO-Detect\n"));
		}
	}
	else
	{	// CE only support noemal HW radio detection now. Support timers GPIO detection in SE/CU.		
		PlatformScheduleWorkItem( &(pHalData->GPIOChangeRFWorkItem) );
	}
//#endif
#else if defined CONFIG_PCI_HCI
	if(Adapter->bInHctTest)
		return;

	// CE only support noemal HW radio detection now. We support timers GPIO detection in SE.		
	PlatformScheduleWorkItem( &(pHalData->GPIOChangeRFWorkItem) );
#endif
#endif
}	/* dm_CheckRfCtrlGPIO */

static VOID
dm_InitRateAdaptiveMask(
	IN	PADAPTER	Adapter	
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PRATE_ADAPTIVE	pRA = (PRATE_ADAPTIVE)&pdmpriv->RateAdaptive;
	
	pRA->RATRState = DM_RATR_STA_INIT;
	pRA->PreRATRState = DM_RATR_STA_INIT;

	if (pdmpriv->DM_Type == DM_Type_ByDriver)
		pdmpriv->bUseRAMask = _TRUE;
	else
		pdmpriv->bUseRAMask = _FALSE;	
}

/*-----------------------------------------------------------------------------
 * Function:	dm_RefreshRateAdaptiveMask()
 *
 * Overview:	Update rate table mask according to rssi
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/27/2009	hpfan	Create Version 0.  
 *
 *---------------------------------------------------------------------------*/
static VOID
dm_RefreshRateAdaptiveMask(	IN	PADAPTER	pAdapter)
{
#if 0
	PADAPTER 				pTargetAdapter;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(pAdapter);
	PMGNT_INFO				pMgntInfo = &(ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, TRUE)->MgntInfo);
	PRATE_ADAPTIVE			pRA = (PRATE_ADAPTIVE)&pMgntInfo->RateAdaptive;
	u4Byte					LowRSSIThreshForRA = 0, HighRSSIThreshForRA = 0;

	if(pAdapter->bDriverStopped)
	{
		RT_TRACE(COMP_RATR, DBG_TRACE, ("<---- dm_RefreshRateAdaptiveMask(): driver is going to unload\n"));
		return;
	}

	if(!pMgntInfo->bUseRAMask)
	{
		RT_TRACE(COMP_RATR, DBG_LOUD, ("<---- dm_RefreshRateAdaptiveMask(): driver does not control rate adaptive mask\n"));
		return;
	}

	// if default port is connected, update RA table for default port (infrastructure mode only)
	if(pAdapter->MgntInfo.mAssoc && (!ACTING_AS_AP(pAdapter)))
	{
		
		// decide rastate according to rssi
		switch (pRA->PreRATRState)
		{
			case DM_RATR_STA_HIGH:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 20;
				break;
			
			case DM_RATR_STA_MIDDLE:
				HighRSSIThreshForRA = 55;
				LowRSSIThreshForRA = 20;
				break;
			
			case DM_RATR_STA_LOW:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 25;
				break;

			default:
				HighRSSIThreshForRA = 50;
				LowRSSIThreshForRA = 20;
				break;
		}

		if(pHalData->UndecoratedSmoothedPWDB > (s4Byte)HighRSSIThreshForRA)
			pRA->RATRState = DM_RATR_STA_HIGH;
		else if(pHalData->UndecoratedSmoothedPWDB > (s4Byte)LowRSSIThreshForRA)
			pRA->RATRState = DM_RATR_STA_MIDDLE;
		else
			pRA->RATRState = DM_RATR_STA_LOW;

		if(pRA->PreRATRState != pRA->RATRState)
		{
			RT_PRINT_ADDR(COMP_RATR, DBG_LOUD, ("Target AP addr : "), pMgntInfo->Bssid);
			RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI = %d\n", pHalData->UndecoratedSmoothedPWDB));
			RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI_LEVEL = %d\n", pRA->RATRState));
			RT_TRACE(COMP_RATR, DBG_LOUD, ("PreState = %d, CurState = %d\n", pRA->PreRATRState, pRA->RATRState));
			pAdapter->HalFunc.UpdateHalRAMaskHandler(
									pAdapter,
									FALSE,
									0,
									NULL,
									NULL,
									pRA->RATRState);
			pRA->PreRATRState = pRA->RATRState;
		}
	}

	//
	// The following part configure AP/VWifi/IBSS rate adaptive mask.
	//
	if(ACTING_AS_AP(pAdapter) || ACTING_AS_IBSS(pAdapter))
	{
		pTargetAdapter = pAdapter;
	}
	else
	{
		pTargetAdapter = ADJUST_TO_ADAPTIVE_ADAPTER(pAdapter, FALSE);
		if(!ACTING_AS_AP(pTargetAdapter))
			pTargetAdapter = NULL;
	}

	// if extension port (softap) is started, updaet RA table for more than one clients associate
	if(pTargetAdapter != NULL)
	{
		int	i;
		PRT_WLAN_STA	pEntry;
		PRATE_ADAPTIVE     pEntryRA;

		for(i = 0; i < ASSOCIATE_ENTRY_NUM; i++)
		{
			if(	pTargetAdapter->MgntInfo.AsocEntry[i].bUsed && pTargetAdapter->MgntInfo.AsocEntry[i].bAssociated)
			{
				pEntry = pTargetAdapter->MgntInfo.AsocEntry+i;
				pEntryRA = &pEntry->RateAdaptive;

				switch (pEntryRA->PreRATRState)
				{
					case DM_RATR_STA_HIGH:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 20;
					}
					break;
					
					case DM_RATR_STA_MIDDLE:
					{
						HighRSSIThreshForRA = 55;
						LowRSSIThreshForRA = 20;
					}
					break;
					
					case DM_RATR_STA_LOW:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 25;
					}
					break;

					default:
					{
						HighRSSIThreshForRA = 50;
						LowRSSIThreshForRA = 20;
					}
				}

				if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > (s4Byte)HighRSSIThreshForRA)
					pEntryRA->RATRState = DM_RATR_STA_HIGH;
				else if(pEntry->rssi_stat.UndecoratedSmoothedPWDB > (s4Byte)LowRSSIThreshForRA)
					pEntryRA->RATRState = DM_RATR_STA_MIDDLE;
				else
					pEntryRA->RATRState = DM_RATR_STA_LOW;

				if(pEntryRA->PreRATRState != pEntryRA->RATRState)
				{
					RT_PRINT_ADDR(COMP_RATR, DBG_LOUD, ("AsocEntry addr : "), pEntry->MacAddr);
					RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI = %d\n", pEntry->rssi_stat.UndecoratedSmoothedPWDB));
					RT_TRACE(COMP_RATR, DBG_LOUD, ("RSSI_LEVEL = %d\n", pEntryRA->RATRState));
					RT_TRACE(COMP_RATR, DBG_LOUD, ("PreState = %d, CurState = %d\n", pEntryRA->PreRATRState, pEntryRA->RATRState));
					pAdapter->HalFunc.UpdateHalRAMaskHandler(
											pTargetAdapter,
											FALSE,
											pEntry->AID+1,
											pEntry->MacAddr,
											pEntry,
											pEntryRA->RATRState);
					pEntryRA->PreRATRState = pEntryRA->RATRState;
				}

			}
		}
	}
#endif	
}

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
	int i=0;

	if(!padapter->registrypriv.hw_wps_pbc)
		return;

	do
	{
		i++;
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
	{
		bPbcPressed = _FALSE;
		break ;
	}	

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
	{
		bPbcPressed = _TRUE;

		if(i<=3)
			rtw_msleep_os(50);
	}
#else
	tmp1byte = rtw_read8(padapter, GPIO_IN);
	//RT_TRACE(COMP_IO, DBG_TRACE, ("dm_CheckPbcGPIO - %x\n", tmp1byte));

	if (tmp1byte == 0xff || padapter->init_adpt_in_progress)
	{
		bPbcPressed = _FALSE;
		break ;
	}

	if((tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)==0)
	{
		bPbcPressed = _TRUE;

		if(i<=3)
			rtw_msleep_os(50);
	}
#endif		

	}while(i<=3 && bPbcPressed == _TRUE);
	
	if( _TRUE == bPbcPressed)
	{
		// Here we only set bPbcPressed to true
		// After trigger PBC, the variable will be set to false		
		DBG_8192C("CheckPbcGPIO - PBC is pressed, try_cnt=%d\n", i-1);
                
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

static void update_EDCA_param(_adapter *padapter)
{
	u32 	trafficIndex;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(padapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
	struct xmit_priv		*pxmitpriv = &(padapter->xmitpriv);
	struct recv_priv		*precvpriv = &(padapter->recvpriv);
	struct registry_priv	*pregpriv = &padapter->registrypriv;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);	
	u8	bbtchange = _FALSE;
#endif
	

	//DBG_871X("%s\n", __FUNCTION__);

	//associated AP
	if ((pregpriv->wifi_spec == 1) || (pmlmeinfo->HT_enable == 0))
	{
		return;
	}
	
	if (pmlmeinfo->assoc_AP_vendor >= maxAP)
	{
		return;
	}

	cur_tx_bytes = pxmitpriv->tx_bytes - pxmitpriv->last_tx_bytes;
	cur_rx_bytes = precvpriv->rx_bytes - precvpriv->last_rx_bytes;

	//traffic, TX or RX
	if((pmlmeinfo->assoc_AP_vendor == ralinkAP)||(pmlmeinfo->assoc_AP_vendor == atherosAP))
	{
		if (cur_tx_bytes > (cur_rx_bytes << 2))
		{ // Uplink TP is present.
			trafficIndex = UP_LINK; 
		}
		else
		{ // Balance TP is present.
			trafficIndex = DOWN_LINK;
		}
	}
	else
	{
		if (cur_rx_bytes > (cur_tx_bytes << 2))
		{ // Downlink TP is present.
			trafficIndex = DOWN_LINK;
		}
		else
		{ // Balance TP is present.
			trafficIndex = UP_LINK;
		}
	}

#ifdef CONFIG_BT_COEXIST
	if(pbtpriv->BT_Coexist)
	{
		if( (pbtpriv->BT_EDCA[UP_LINK]!=0) ||  (pbtpriv->BT_EDCA[DOWN_LINK]!=0))
		{
			bbtchange = _TRUE;
		}
	}
#endif

	if (pdmpriv->prv_traffic_idx != trafficIndex)
	{
#if 0
#ifdef CONFIG_BT_COEXIST
		if(_TRUE == bbtchange)		
			rtw_write32(padapter, REG_EDCA_BE_PARAM, pbtpriv->BT_EDCA[trafficIndex]);		
		else
#endif
		//adjust EDCA parameter for BE queue
		//fire_write_MAC_cmd(padapter, EDCA_BE_PARAM, EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex]);
		rtw_write32(padapter, REG_EDCA_BE_PARAM, EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex]);

#else
		if((pmlmeinfo->assoc_AP_vendor == ciscoAP) && (pmlmeext->cur_wireless_mode & WIRELESS_11_24N))
		{
			edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
		}
		else if((pmlmeinfo->assoc_AP_vendor == airgocapAP) &&
			((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||(pmlmeext->cur_wireless_mode == WIRELESS_11BG)))
		{
			edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
		}
		else
		{
			edca_param = EDCAParam[unknownAP][trafficIndex];
		}

#ifdef CONFIG_BT_COEXIST
		if(_TRUE == bbtchange)		
			edca_param = pbtpriv->BT_EDCA[trafficIndex];
#endif

		rtw_write32(padapter, REG_EDCA_BE_PARAM, edca_param);
#endif
		pdmpriv->prv_traffic_idx = trafficIndex;
	}
	
//exit_update_EDCA_param:	

	pxmitpriv->last_tx_bytes = pxmitpriv->tx_bytes;
	precvpriv->last_rx_bytes = precvpriv->rx_bytes;

	return;
}

static void dm_InitDynamicBBPowerSaving(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PS_T	*pPSTable = &pdmpriv->DM_PSTable;

	pPSTable->PreCCAState = CCA_MAX;
	pPSTable->CurCCAState = CCA_MAX;
	pPSTable->PreRFState = RF_MAX;
	pPSTable->CurRFState = RF_MAX;
	pPSTable->Rssi_val_min = 0;
}

static void dm_1R_CCA(
	IN	PADAPTER	pAdapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PS_T	*pPSTable = &pdmpriv->DM_PSTable;

	if(pPSTable->Rssi_val_min != 0)
	{
		if(pPSTable->PreCCAState == CCA_2R)
		{
			if(pPSTable->Rssi_val_min >= 35)
				pPSTable->CurCCAState = CCA_1R;
			else
				pPSTable->CurCCAState = CCA_2R;
		}
		else{
			if(pPSTable->Rssi_val_min <= 30)
				pPSTable->CurCCAState = CCA_2R;
			else
				pPSTable->CurCCAState = CCA_1R;
		}
	}
	else
		pPSTable->CurCCAState=CCA_MAX;

	if(pPSTable->PreCCAState != pPSTable->CurCCAState)
	{
		if(pPSTable->CurCCAState == CCA_1R)
		{
			if(pHalData->rf_type == RF_2T2R)
			{
				PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x13);
				PHY_SetBBReg(pAdapter, 0xe70, bMaskByte3, 0x20);
			}
			else
			{
				PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x23);
				PHY_SetBBReg(pAdapter, 0xe70, 0x7fc00000, 0x10c); // Set RegE70[30:22] = 9b'100001100
			}
		}
		else
		{
			PHY_SetBBReg(pAdapter, rOFDM0_TRxPathEnable, bMaskByte0, 0x33);
			PHY_SetBBReg(pAdapter,0xe70, bMaskByte3, 0x63);
		}
		pPSTable->PreCCAState = pPSTable->CurCCAState;
	}
	//DBG_8192C("dm_1R_CCA(): CCAStage=%x\n", pPSTable->CurCCAState);
}

void
rtl8192c_dm_RF_Saving(
	IN	PADAPTER	pAdapter,
	IN	u8	bForceInNormal 
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PS_T	*pPSTable = &pdmpriv->DM_PSTable;

	if(pdmpriv->initialize == 0){
		pdmpriv->rf_saving_Reg874 = (PHY_QueryBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, bMaskDWord)&0x1CC000)>>14;
		pdmpriv->rf_saving_RegC70 = (PHY_QueryBBReg(pAdapter, rOFDM0_AGCParameter1, bMaskDWord)&BIT3)>>3;
		pdmpriv->rf_saving_Reg85C = (PHY_QueryBBReg(pAdapter, rFPGA0_XCD_SwitchControl, bMaskDWord)&0xFF000000)>>24;
		pdmpriv->rf_saving_RegA74 = (PHY_QueryBBReg(pAdapter, 0xa74, bMaskDWord)&0xF000)>>12;
		//Reg818 = PHY_QueryBBReg(pAdapter, 0x818, bMaskDWord);
		pdmpriv->initialize = 1;
	}

	if(!bForceInNormal)
	{
		if(pPSTable->Rssi_val_min != 0)
		{
			 
			if(pPSTable->PreRFState == RF_Normal)
			{
			#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
				if(pPSTable->Rssi_val_min >= 50)
			#else
				if(pPSTable->Rssi_val_min >= 30)
			#endif
					pPSTable->CurRFState = RF_Save;
				else
					pPSTable->CurRFState = RF_Normal;
			}
			else{
			#ifdef CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
				if(pPSTable->Rssi_val_min <= 45)
			#else
				if(pPSTable->Rssi_val_min <= 25)
			#endif
					pPSTable->CurRFState = RF_Normal;
				else
					pPSTable->CurRFState = RF_Save;
			}
		}
		else
			pPSTable->CurRFState=RF_MAX;
	}
	else
	{
		pPSTable->CurRFState = RF_Normal;
	}
	
	if(pPSTable->PreRFState != pPSTable->CurRFState)
	{
		if(pPSTable->CurRFState == RF_Save)
		{
			PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW  , 0x1C0000, 0x2); //Reg874[20:18]=3'b010
			PHY_SetBBReg(pAdapter, rOFDM0_AGCParameter1, BIT3, 0); //RegC70[3]=1'b0
			PHY_SetBBReg(pAdapter, rFPGA0_XCD_SwitchControl, 0xFF000000, 0x63); //Reg85C[31:24]=0x63
			PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW, 0xC000, 0x2); //Reg874[15:14]=2'b10
			PHY_SetBBReg(pAdapter, 0xa74, 0xF000, 0x3); //RegA75[7:4]=0x3
			PHY_SetBBReg(pAdapter, 0x818, BIT28, 0x0); //Reg818[28]=1'b0
			PHY_SetBBReg(pAdapter, 0x818, BIT28, 0x1); //Reg818[28]=1'b1
			DBG_8192C("%s(): RF_Save\n", __FUNCTION__);
		}
		else
		{
			PHY_SetBBReg(pAdapter, rFPGA0_XCD_RFInterfaceSW  , 0x1CC000, pdmpriv->rf_saving_Reg874);
			PHY_SetBBReg(pAdapter, rOFDM0_AGCParameter1, BIT3, pdmpriv->rf_saving_RegC70);
			PHY_SetBBReg(pAdapter, rFPGA0_XCD_SwitchControl, 0xFF000000, pdmpriv->rf_saving_Reg85C);
			PHY_SetBBReg(pAdapter, 0xa74, 0xF000, pdmpriv->rf_saving_RegA74);
			PHY_SetBBReg(pAdapter, 0x818, BIT28, 0x0);
			DBG_8192C("%s(): RF_Normal\n", __FUNCTION__);
		}
		pPSTable->PreRFState = pPSTable->CurRFState;
	}
}

static void
dm_DynamicBBPowerSaving(
IN	PADAPTER	pAdapter
	)
{	

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct mlme_priv	*pmlmepriv = &pAdapter->mlmepriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PS_T	*pPSTable = &pdmpriv->DM_PSTable;

	//1 1.Determine the minimum RSSI 
	if((check_fwstate(pmlmepriv, _FW_LINKED) != _TRUE) &&	
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pPSTable->Rssi_val_min = 0;
		//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any \n"));
	}
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	// Default port
	{
		//if(ACTING_AS_AP(pAdapter) || pMgntInfo->mIbss)
		 if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == _TRUE) ||
			       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == _TRUE))	//todo: AP Mode
		{
			pPSTable->Rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("AP Client PWDB = 0x%lx \n", pPSTable->Rssi_val_min));
		}
		else
		{
			pPSTable->Rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
			//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("STA Default Port PWDB = 0x%lx \n", pPSTable->Rssi_val_min));
		}
	}
	else // associated entry pwdb
	{	
		pPSTable->Rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		//RT_TRACE(COMP_BB_POWERSAVING, DBG_LOUD, ("AP Ext Port PWDB = 0x%lx \n", pPSTable->Rssi_val_min));
	}
	
	//1 2.Power Saving for 92C
	if(IS_92C_SERIAL(pHalData->VersionID))
	{
		//dm_1R_CCA(pAdapter);
	}
	
	// 20100628 Joseph: Turn off BB power save for 88CE because it makesthroughput unstable.
	// 20100831 Joseph: Turn ON BB power save again after modifying AGC delay from 900ns to 600ns. 
	//1 3.Power Saving for 88C
	else
	{
		rtl8192c_dm_RF_Saving(pAdapter, _FALSE);
	}
}


#ifdef CONFIG_ANTENNA_DIVERSITY
// Add new function to reset the state of antenna diversity before link.
//
void SwAntDivResetBeforeLink8192C(IN PADAPTER Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	SWAT_T *pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;
	
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
}

// Compare RSSI for deciding antenna
void	SwAntDivCompare8192C(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if((0 != pHalData->AntDivCfg) && (!IS_92C_SERIAL(pHalData->VersionID)) )
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
u8 SwAntDivBeforeLink8192C(IN PADAPTER Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	SWAT_T			*pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;
	struct mlme_priv	*pmlmepriv = &(Adapter->mlmepriv);
	
	// Condition that does not need to use antenna diversity.
	if(IS_92C_SERIAL(pHalData->VersionID) ||(pHalData->AntDivCfg==0))
	{
		//DBG_8192C("SwAntDivBeforeLink8192C(): No AntDiv Mechanism.\n");
		return _FALSE;
	}

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)	
	{
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		return _FALSE;
	}
	// Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF.
/*	
	if(pHalData->eRFPowerState!=eRfOn || pMgntInfo->RFChangeInProgress || pMgntInfo->bMediaConnect)
	{
	
	
		RT_TRACE(COMP_SWAS, DBG_LOUD, 
				("SwAntDivCheckBeforeLink8192C(): RFChangeInProgress(%x), eRFPowerState(%x)\n", 
				pMgntInfo->RFChangeInProgress,
				pHalData->eRFPowerState));
	
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		
		return FALSE;
	}
*/	
	
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
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
//
// 20100514 Luke/Joseph:
// Add new function to reset antenna diversity state after link.
//
void
SwAntDivRestAfterLink8192C(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	SWAT_T			*pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;

	if(IS_92C_SERIAL(pHalData->VersionID) ||(pHalData->AntDivCfg==0))	
		return;

	//DBG_8192C("======>   SwAntDivRestAfterLink <========== \n");
	pHalData->RSSI_cnt_A= 0;
	pHalData->RSSI_cnt_B= 0;
	pHalData->RSSI_test = _FALSE;
	
	pDM_SWAT_Table->try_flag = 0xff;
	pDM_SWAT_Table->RSSI_Trying = 0;	
	pDM_SWAT_Table->SelectAntennaMap=0xAA;
	pDM_SWAT_Table->CurAntenna = pHalData->CurAntenna;
	pDM_SWAT_Table->PreAntenna = pHalData->CurAntenna;
		
	pdmpriv->lastTxOkCnt=0;
	pdmpriv->lastRxOkCnt=0;

	pdmpriv->TXByteCnt_A=0;
	pdmpriv->TXByteCnt_B=0;
	pdmpriv->RXByteCnt_A=0;
	pdmpriv->RXByteCnt_B=0;
	pdmpriv->DoubleComfirm=0;	
	pdmpriv->TrafficLoad = TRAFFIC_LOW;
	
}


//
// 20100514 Luke/Joseph:
// Add new function for antenna diversity after link.
// This is the main function of antenna diversity after link.
// This function is called in HalDmWatchDog() and dm_SW_AntennaSwitchCallback().
// HalDmWatchDog() calls this function with SWAW_STEP_PEAK to initialize the antenna test.
// In SWAW_STEP_PEAK, another antenna and a 500ms timer will be set for testing.
// After 500ms, dm_SW_AntennaSwitchCallback() calls this function to compare the signal just
// listened on the air with the RSSI of original antenna.
// It chooses the antenna with better RSSI.
// There is also a aged policy for error trying. Each error trying will cost more 5 seconds waiting 
// penalty to get next try.
//
static VOID
dm_SW_AntennaSwitch(
	PADAPTER	Adapter,
	u8			Step
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	SWAT_T	*pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;
	s32			curRSSI=100, RSSI_A, RSSI_B;
	u64			curTxOkCnt, curRxOkCnt;
	u64			CurByteCnt = 0, PreByteCnt = 0;	
	u8		nextAntenna = 0;
	u8			Score_A=0, Score_B=0;
	u8			i;

	// Condition that does not need to use antenna diversity.
	if(IS_92C_SERIAL(pHalData->VersionID) ||(pHalData->AntDivCfg==0))
	{
		//RT_TRACE(COMP_SWAS, DBG_LOUD, ("dm_SW_AntennaSwitch(): No AntDiv Mechanism.\n"));
		return;
	}
	// If dynamic ant_div is disabled.
	if(!(pdmpriv->DMFlag & DYNAMIC_FUNC_ANT_DIV) )
	{	
		return;
	}
	
	if (check_fwstate(&Adapter->mlmepriv, _FW_LINKED)	==_FALSE)
		return;
#if 0 //to do
	// Radio off: Status reset to default and return.
	if(pHalData->eRFPowerState==eRfOff)
	{
		SwAntDivRestAfterLink(Adapter);
		return;
	}
#endif
	//DBG_8192C("\n............................ %s.........................\n",__FUNCTION__);
	// Handling step mismatch condition.
	// Peak step is not finished at last time. Recover the variable and check again.
	if( Step != pDM_SWAT_Table->try_flag	)
	{
		SwAntDivRestAfterLink8192C(Adapter);
	}


	if(pDM_SWAT_Table->try_flag == 0xff)
	{
#if 0
		// Select RSSI checking target
		if(pMgntInfo->mAssoc && !ACTING_AS_AP(Adapter))
		{
			// Target: Infrastructure mode AP.
			pHalData->RSSI_target = NULL;
			RT_TRACE(COMP_SWAS, DBG_LOUD, ("dm_SW_AntennaSwitch(): RSSI_target is DEF AP!\n"));
		}
		else
		{
			u8			index = 0;
			PRT_WLAN_STA	pEntry = NULL;
			PADAPTER		pTargetAdapter = NULL;
		
			if(	pMgntInfo->mIbss || ACTING_AS_AP(Adapter) )
			{
				// Target: AP/IBSS peer.
				pTargetAdapter = Adapter;
			}
			else if(ACTING_AS_AP(ADJUST_TO_ADAPTIVE_ADAPTER(Adapter, FALSE)))
			{
				// Target: VWIFI peer.
				pTargetAdapter = ADJUST_TO_ADAPTIVE_ADAPTER(Adapter, FALSE);
			}

			if(pTargetAdapter != NULL)
			{
				for(index=0; index<ASSOCIATE_ENTRY_NUM; index++)
				{
					pEntry = AsocEntry_EnumStation(pTargetAdapter, index);
					if(pEntry != NULL)
					{
						if(pEntry->bAssociated)
							break;			
					}
				}
			}

			if(pEntry == NULL)
			{
				SwAntDivRestAfterLink(Adapter);
				RT_TRACE(COMP_SWAS, DBG_LOUD, ("dm_SW_AntennaSwitch(): No Link.\n"));
				return;
			}
			else
			{
				pHalData->RSSI_target = pEntry;
				RT_TRACE(COMP_SWAS, DBG_LOUD, ("dm_SW_AntennaSwitch(): RSSI_target is PEER STA\n"));
			}
		}
			
			
#endif
		
		pHalData->RSSI_cnt_A= 0;
		pHalData->RSSI_cnt_B= 0;
		pDM_SWAT_Table->try_flag = 0;
	//	DBG_8192C("dm_SW_AntennaSwitch(): Set try_flag to 0 prepare for peak!\n");
		return;
	}
	else
	{
		curTxOkCnt = Adapter->xmitpriv.tx_bytes - pdmpriv->lastTxOkCnt;
		curRxOkCnt = Adapter->recvpriv.rx_bytes - pdmpriv->lastRxOkCnt;

		pdmpriv->lastTxOkCnt = Adapter->xmitpriv.tx_bytes ;
		pdmpriv->lastRxOkCnt = Adapter->recvpriv.rx_bytes ;

		if(pDM_SWAT_Table->try_flag == 1)
		{
			if(pDM_SWAT_Table->CurAntenna == Antenna_A)
		{
				pdmpriv->TXByteCnt_A += curTxOkCnt;
				pdmpriv->RXByteCnt_A += curRxOkCnt;
				//DBG_8192C("#####  TXByteCnt_A(%lld) , RXByteCnt_A(%lld) ####\n",pdmpriv->TXByteCnt_A,pdmpriv->RXByteCnt_A);
			}
			else
			{
				pdmpriv->TXByteCnt_B += curTxOkCnt;
				pdmpriv->RXByteCnt_B += curRxOkCnt;
				//DBG_8192C("#####  TXByteCnt_B(%lld) , RXByteCnt_B(%lld) ####\n",pdmpriv->TXByteCnt_B,pdmpriv->RXByteCnt_B);
			}
		
			nextAntenna = (pDM_SWAT_Table->CurAntenna == Antenna_A)? Antenna_B : Antenna_A;
			pDM_SWAT_Table->RSSI_Trying--;
			//DBG_8192C("RSSI_Trying = %d\n",pDM_SWAT_Table->RSSI_Trying);
			
			if(pDM_SWAT_Table->RSSI_Trying == 0)
			{
				CurByteCnt = (pDM_SWAT_Table->CurAntenna == Antenna_A)? (pdmpriv->TXByteCnt_A+pdmpriv->RXByteCnt_A) : (pdmpriv->TXByteCnt_B+pdmpriv->RXByteCnt_B);
				PreByteCnt = (pDM_SWAT_Table->CurAntenna == Antenna_A)? (pdmpriv->TXByteCnt_B+pdmpriv->RXByteCnt_B) : (pdmpriv->TXByteCnt_A+pdmpriv->RXByteCnt_A);

				//DBG_8192C("CurByteCnt = %lld\n", CurByteCnt);
				//DBG_8192C("PreByteCnt = %lld\n",PreByteCnt);		
				
				if(pdmpriv->TrafficLoad == TRAFFIC_HIGH)
				{
					PreByteCnt = PreByteCnt*9;	//normalize:Cur=90ms:Pre=10ms					
				}
				else if(pdmpriv->TrafficLoad == TRAFFIC_LOW)
				{					
					//CurByteCnt = CurByteCnt/2;
					CurByteCnt = CurByteCnt>>1;//normalize:100ms:50ms					
				}


				//DBG_8192C("After DIV=>CurByteCnt = %lld\n", CurByteCnt);
				//DBG_8192C("PreByteCnt = %lld\n",PreByteCnt);		

				if(pHalData->RSSI_cnt_A > 0)
					RSSI_A = pHalData->RSSI_sum_A/pHalData->RSSI_cnt_A; 
				else
					RSSI_A = 0;
				if(pHalData->RSSI_cnt_B > 0)
					RSSI_B = pHalData->RSSI_sum_B/pHalData->RSSI_cnt_B; 
				else
					RSSI_B = 0;
				
				curRSSI = (pDM_SWAT_Table->CurAntenna == Antenna_A)? RSSI_A : RSSI_B;
				pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->CurAntenna == Antenna_A)? RSSI_B : RSSI_A;
				//DBG_8192C("Luke:PreRSSI = %d, CurRSSI = %d\n",pDM_SWAT_Table->PreRSSI, curRSSI);
				//DBG_8192C("SWAS: preAntenna= %s, curAntenna= %s \n", 
				//(pDM_SWAT_Table->PreAntenna == Antenna_A?"A":"B"), (pDM_SWAT_Table->CurAntenna == Antenna_A?"A":"B"));
				//DBG_8192C("Luke:RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
					//RSSI_A, pHalData->RSSI_cnt_A, RSSI_B, pHalData->RSSI_cnt_B);
			}

			}
			else
			{
		
			if(pHalData->RSSI_cnt_A > 0)
				RSSI_A = pHalData->RSSI_sum_A/pHalData->RSSI_cnt_A; 
			else
				RSSI_A = 0;
			if(pHalData->RSSI_cnt_B > 0)
				RSSI_B = pHalData->RSSI_sum_B/pHalData->RSSI_cnt_B; 
			else
				RSSI_B = 0;
			curRSSI = (pDM_SWAT_Table->CurAntenna == Antenna_A)? RSSI_A : RSSI_B;
			pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->PreAntenna == Antenna_A)? RSSI_A : RSSI_B;
			//DBG_8192C("Ekul:PreRSSI = %d, CurRSSI = %d\n", pDM_SWAT_Table->PreRSSI, curRSSI);
			//DBG_8192C("SWAS: preAntenna= %s, curAntenna= %s \n", 
			//(pDM_SWAT_Table->PreAntenna == Antenna_A?"A":"B"), (pDM_SWAT_Table->CurAntenna == Antenna_A?"A":"B"));

			//DBG_8192C("Ekul:RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
			//	RSSI_A, pHalData->RSSI_cnt_A, RSSI_B, pHalData->RSSI_cnt_B);
			//RT_TRACE(COMP_SWAS, DBG_LOUD, ("Ekul:curTxOkCnt = %d\n", curTxOkCnt));
			//RT_TRACE(COMP_SWAS, DBG_LOUD, ("Ekul:curRxOkCnt = %d\n", curRxOkCnt));
			}

		//1 Trying State
		if((pDM_SWAT_Table->try_flag == 1)&&(pDM_SWAT_Table->RSSI_Trying == 0))
		{

			if(pDM_SWAT_Table->TestMode == TP_MODE)
			{
				//DBG_8192C("SWAS: TestMode = TP_MODE\n");
				//DBG_8192C("TRY:CurByteCnt = %lld\n", CurByteCnt);
				//DBG_8192C("TRY:PreByteCnt = %lld\n",PreByteCnt);		
				if(CurByteCnt < PreByteCnt)
				{
					if(pDM_SWAT_Table->CurAntenna == Antenna_A)
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
					else
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
				}
				else
				{
					if(pDM_SWAT_Table->CurAntenna == Antenna_A)
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
					else
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
				}
				for (i= 0; i<8; i++)
				{
					if(((pDM_SWAT_Table->SelectAntennaMap>>i)&BIT0) == 1)
						Score_A++;
					else
						Score_B++;
				}
				//DBG_8192C("SelectAntennaMap=%x\n ",pDM_SWAT_Table->SelectAntennaMap);
				//DBG_8192C("Score_A=%d, Score_B=%d\n", Score_A, Score_B);
				
				if(pDM_SWAT_Table->CurAntenna == Antenna_A)
				{
					nextAntenna = (Score_A > Score_B)?Antenna_A:Antenna_B;
				}
				else
				{
					nextAntenna = (Score_B > Score_A)?Antenna_B:Antenna_A;
				}
				//RT_TRACE(COMP_SWAS, DBG_LOUD, ("nextAntenna=%s\n",(nextAntenna==Antenna_A)?"A":"B"));
				//RT_TRACE(COMP_SWAS, DBG_LOUD, ("preAntenna= %s, curAntenna= %s \n", 
					//(DM_SWAT_Table.PreAntenna == Antenna_A?"A":"B"), (DM_SWAT_Table.CurAntenna == Antenna_A?"A":"B")));

				if(nextAntenna != pDM_SWAT_Table->CurAntenna)
				{
					//DBG_8192C("SWAS: Switch back to another antenna\n");
				}
				else
				{
					//DBG_8192C("SWAS: current anntena is good\n");
				}	
			}

			if(pDM_SWAT_Table->TestMode == RSSI_MODE)
			{	
				//DBG_8192C("SWAS: TestMode = RSSI_MODE\n");
				pDM_SWAT_Table->SelectAntennaMap=0xAA;
				if(curRSSI < pDM_SWAT_Table->PreRSSI) //Current antenna is worse than previous antenna
				{
					//DBG_8192C("SWAS: Switch back to another antenna\n");
					nextAntenna = (pDM_SWAT_Table->CurAntenna == Antenna_A)? Antenna_B : Antenna_A;
				}
				else // current anntena is good
				{
					nextAntenna = pDM_SWAT_Table->CurAntenna;
					//DBG_8192C("SWAS: current anntena is good\n");
				}
				}
				pDM_SWAT_Table->try_flag = 0;
				pHalData->RSSI_test = _FALSE;
				pHalData->RSSI_sum_A = 0;
				pHalData->RSSI_cnt_A = 0;
				pHalData->RSSI_sum_B = 0;
				pHalData->RSSI_cnt_B = 0;
				pdmpriv->TXByteCnt_A = 0;
				pdmpriv->TXByteCnt_B = 0;
				pdmpriv->RXByteCnt_A = 0;
				pdmpriv->RXByteCnt_B = 0;
			
			}

		//1 Normal State
		else if(pDM_SWAT_Table->try_flag == 0)
			{
			if(pdmpriv->TrafficLoad == TRAFFIC_HIGH)
				{
				if(((curTxOkCnt+curRxOkCnt)>>1) > 1875000)
					pdmpriv->TrafficLoad = TRAFFIC_HIGH;
				else
					pdmpriv->TrafficLoad = TRAFFIC_LOW;
			}
			else if(pdmpriv->TrafficLoad == TRAFFIC_LOW)
				{
				if(((curTxOkCnt+curRxOkCnt)>>1) > 1875000)
					pdmpriv->TrafficLoad = TRAFFIC_HIGH;
				else
					pdmpriv->TrafficLoad = TRAFFIC_LOW;
			}
			if(pdmpriv->TrafficLoad == TRAFFIC_HIGH)
				pDM_SWAT_Table->bTriggerAntennaSwitch = 0;
			//DBG_8192C("Normal:TrafficLoad = %lld\n", curTxOkCnt+curRxOkCnt);

			//Prepare To Try Antenna		
					nextAntenna = (pDM_SWAT_Table->CurAntenna == Antenna_A)? Antenna_B : Antenna_A;
					pDM_SWAT_Table->try_flag = 1;
					pHalData->RSSI_test = _TRUE;
			if((curRxOkCnt+curTxOkCnt) > 1000)
			{
				pDM_SWAT_Table->RSSI_Trying = 4;
				pDM_SWAT_Table->TestMode = TP_MODE;
				}
				else
				{
				pDM_SWAT_Table->RSSI_Trying = 2;
				pDM_SWAT_Table->TestMode = RSSI_MODE;

			}
			//DBG_8192C("SWAS: Normal State -> Begin Trying! TestMode=%s\n",(pDM_SWAT_Table->TestMode == TP_MODE)?"TP":"RSSI");
			
			
			pHalData->RSSI_sum_A = 0;
			pHalData->RSSI_cnt_A = 0;
			pHalData->RSSI_sum_B = 0;
			pHalData->RSSI_cnt_B = 0;
		}
	}

	//1 4.Change TRX antenna
	if(nextAntenna != pDM_SWAT_Table->CurAntenna)
	{
		//DBG_8192C("@@@@@@@@ SWAS: Change TX Antenna!\n ");		
		rtw_antenna_select_cmd(Adapter, nextAntenna, 1);
	}

	//1 5.Reset Statistics
	pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
	pDM_SWAT_Table->CurAntenna = nextAntenna;
	pDM_SWAT_Table->PreRSSI = curRSSI;


	//1 6.Set next timer

	if(pDM_SWAT_Table->RSSI_Trying == 0)
		return;

	if(pDM_SWAT_Table->RSSI_Trying%2 == 0)
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(pdmpriv->TrafficLoad == TRAFFIC_HIGH)
			{
				_set_timer(&pdmpriv->SwAntennaSwitchTimer,10 ); //ms
				//DBG_8192C("dm_SW_AntennaSwitch(): Test another antenna for 10 ms\n");
			}
			else if(pdmpriv->TrafficLoad == TRAFFIC_LOW)
			{
				_set_timer(&pdmpriv->SwAntennaSwitchTimer, 50 ); //ms
				//DBG_8192C("dm_SW_AntennaSwitch(): Test another antenna for 50 ms\n");
			}
	}
	else
	{
			_set_timer(&pdmpriv->SwAntennaSwitchTimer, 500 ); //ms
			//DBG_8192C("dm_SW_AntennaSwitch(): Test another antenna for 500 ms\n");
		}
	}
	else
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(pdmpriv->TrafficLoad == TRAFFIC_HIGH)			
				_set_timer(&pdmpriv->SwAntennaSwitchTimer,90 ); //ms			
			else if(pdmpriv->TrafficLoad == TRAFFIC_LOW)
				_set_timer(&pdmpriv->SwAntennaSwitchTimer,100 ); //ms
		}
		else
		{
			_set_timer(&pdmpriv->SwAntennaSwitchTimer,500 ); //ms
			//DBG_8192C("dm_SW_AntennaSwitch(): Test another antenna for 500 ms\n");
		}
	}

//	RT_TRACE(COMP_SWAS, DBG_LOUD, ("SWAS: -----The End-----\n "));

}

//
// 20100514 Luke/Joseph:
// Callback function for 500ms antenna test trying.
//
static void dm_SW_AntennaSwitchCallback(void *FunctionContext)
{
	_adapter *padapter = (_adapter *)FunctionContext;

	if(padapter->net_closed == _TRUE)
			return;
	// Only 
	dm_SW_AntennaSwitch(padapter, SWAW_STEP_DETERMINE);
}


//
// 20100722
// This function is used to gather the RSSI information for antenna testing.
// It selects the RSSI of the peer STA that we want to know.
//
void SwAntDivRSSICheck8192C(_adapter *padapter ,u32 RxPWDBAll)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);	
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	SWAT_T	*pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;

	if(IS_92C_SERIAL(pHalData->VersionID) ||pHalData->AntDivCfg==0)
		return;
	
	if(check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE)		
	{			
		if(pDM_SWAT_Table->CurAntenna == Antenna_A)
		{			
			pHalData->RSSI_sum_A += RxPWDBAll;
			pHalData->RSSI_cnt_A++;
		}
		else
		{
			pHalData->RSSI_sum_B+= RxPWDBAll;
			pHalData->RSSI_cnt_B++;
		
		}
		//DBG_8192C("%s Ant_(%s),RSSI_sum(%d),RSSI_cnt(%d)\n",__FUNCTION__,(2==pHalData->CurAntenna)?"A":"B",pHalData->RSSI_sum,pHalData->RSSI_cnt);
	}
	
}



static VOID
dm_SW_AntennaSwitchInit(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	SWAT_T	*pDM_SWAT_Table = &pdmpriv->DM_SWAT_Table;

	pHalData->RSSI_sum_A = 0;	
	pHalData->RSSI_sum_B = 0;
	pHalData->RSSI_cnt_A = 0;
	pHalData->RSSI_cnt_B = 0;

	pDM_SWAT_Table->CurAntenna = pHalData->CurAntenna;
	pDM_SWAT_Table->PreAntenna = pHalData->CurAntenna;
	pDM_SWAT_Table->try_flag = 0xff;
	pDM_SWAT_Table->PreRSSI = 0;
	pDM_SWAT_Table->bTriggerAntennaSwitch = 0;	
	pDM_SWAT_Table->SelectAntennaMap=0xAA;
	
	// Move the timer initialization to InitializeVariables function.
	//PlatformInitializeTimer(Adapter, &pMgntInfo->SwAntennaSwitchTimer, (RT_TIMER_CALL_BACK)dm_SW_AntennaSwitchCallback, NULL, "SwAntennaSwitchTimer");	
}

#endif

//#define	RSSI_CCK	0
//#define	RSSI_OFDM	1
static void dm_RSSIMonitorInit(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	pdmpriv->OFDM_Pkt_Cnt = 0;
	pdmpriv->RSSI_Select = RSSI_DEFAULT;
}

static void dm_RSSIMonitorCheck(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	if(check_fwstate(pmlmepriv, _FW_LINKED) == _FALSE)
		return;
		
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE |WIFI_ADHOC_STATE) == _TRUE )
	{
		if(Adapter->stapriv.asoc_sta_count < 2)
			return;			
	}		
	
	if(pdmpriv->OFDM_Pkt_Cnt == 0)
		pdmpriv->RSSI_Select = RSSI_CCK;
	else
		pdmpriv->RSSI_Select = RSSI_OFDM;

	pdmpriv->OFDM_Pkt_Cnt = 0;	
	//DBG_8192C("RSSI_Select=%s OFDM_Pkt_Cnt(%d)\n",
		//(pdmpriv->RSSI_Select == RSSI_OFDM)?"RSSI_OFDM":"RSSI_CCK",
		//pdmpriv->OFDM_Pkt_Cnt);
}

//============================================================
// functions
//============================================================
void rtl8192c_init_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	//_rtw_memset(pdmpriv, 0, sizeof(struct dm_priv));

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	_init_timer(&(pdmpriv->SwAntennaSwitchTimer),  Adapter->pnetdev , dm_SW_AntennaSwitchCallback, Adapter);
#endif
}

void rtl8192c_deinit_dm_priv(IN PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	_cancel_timer_ex(&pdmpriv->SwAntennaSwitchTimer);
#endif
}
#ifdef CONFIG_HW_ANTENNA_DIVERSITY
void dm_InitHybridAntDiv(IN PADAPTER Adapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	if(IS_92C_SERIAL(pHalData->VersionID) ||pHalData->AntDivCfg==0)
		return;
	
	//Set OFDM HW RX Antenna Diversity
	PHY_SetBBReg(Adapter,0xc50, BIT7, 1); //Enable Hardware antenna switch
	PHY_SetBBReg(Adapter,0x870, BIT9|BIT8, 0); //Enable hardware control of "ANT_SEL" & "ANT_SELB"
	PHY_SetBBReg(Adapter,0xCA4, BIT11, 0); //Switch to another antenna by checking pwdb threshold
	PHY_SetBBReg(Adapter,0xCA4, 0x7FF, 0x080); //Pwdb threshold=8dB
	PHY_SetBBReg(Adapter,0xC54, BIT23, 1); //Decide final antenna by comparing 2 antennas' pwdb
	PHY_SetBBReg(Adapter,0x874, BIT23, 0); //No update ANTSEL during GNT_BT=1
	PHY_SetBBReg(Adapter,0x80C, BIT21, 1); //TX atenna selection from tx_info
	//Set CCK HW RX Antenna Diversity
	PHY_SetBBReg(Adapter,0xA00, BIT15, 1);//Enable antenna diversity
	PHY_SetBBReg(Adapter,0xA0C, BIT4, 0); //Antenna diversity decision period = 32 sample
	PHY_SetBBReg(Adapter,0xA0C, 0xf, 0xf); //Threshold for antenna diversity. Check another antenna power if input power < ANT_lim*4
	PHY_SetBBReg(Adapter,0xA10, BIT13, 1); //polarity ana_A=1 and ana_B=0
	PHY_SetBBReg(Adapter,0xA14, 0x1f, 0x8); //default antenna power = inpwr*(0.5 + r_ant_step/16)
	
	pHalData->CCK_Ant1_Cnt = 0;
	pHalData->CCK_Ant2_Cnt = 0;
	pHalData->OFDM_Ant1_Cnt = 0;
	pHalData->OFDM_Ant2_Cnt = 0;
}


#define 	RxDefaultAnt1		0x65a9
#define	RxDefaultAnt2		0x569a

void dm_SelectRXDefault(IN	PADAPTER	Adapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	
	if(IS_92C_SERIAL(pHalData->VersionID) ||pHalData->AntDivCfg==0)
		return;
	
	//DbgPrint(" Ant1_Cnt=%d, Ant2_Cnt=%d\n", pHalData->Ant1_Cnt, pHalData->Ant2_Cnt);
	//DBG_8192C(" CCK_Ant1_Cnt = %d,  CCK_Ant2_Cnt = %d\n", pHalData->CCK_Ant1_Cnt, pHalData->CCK_Ant2_Cnt);
	//DBG_8192C(" OFDM_Ant1_Cnt = %d,  OFDM_Ant2_Cnt = %d\n", pHalData->OFDM_Ant1_Cnt, pHalData->OFDM_Ant2_Cnt);
	if((pHalData->OFDM_Ant1_Cnt == 0) && (pHalData->OFDM_Ant2_Cnt == 0)) 
	{
		if((pHalData->CCK_Ant1_Cnt + pHalData->CCK_Ant2_Cnt) >=10 )
		{
			if(pHalData->CCK_Ant1_Cnt > (5*pHalData->CCK_Ant2_Cnt))
			{
				DBG_8192C(" RX Default = Ant1\n");
				PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt1);
			}
			else if(pHalData->CCK_Ant2_Cnt > (5*pHalData->CCK_Ant1_Cnt))
			{
				DBG_8192C(" RX Default = Ant2\n");
				PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt2);
			}
			else if(pHalData->CCK_Ant1_Cnt > pHalData->CCK_Ant2_Cnt)
			{
				DBG_8192C(" RX Default = Ant2\n");
				PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt2);
			}
			else
			{
				DBG_8192C(" RX Default = Ant1\n");
				PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt1);
			}
			pHalData->CCK_Ant1_Cnt = 0;
			pHalData->CCK_Ant2_Cnt = 0;
			pHalData->OFDM_Ant1_Cnt = 0;
			pHalData->OFDM_Ant2_Cnt = 0;
		}
	}
	else
	{
		if(pHalData->OFDM_Ant1_Cnt > pHalData->OFDM_Ant2_Cnt)
		{
			DBG_8192C(" RX Default = Ant1\n");
			PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt1);
		}
		else
		{
			DBG_8192C(" RX Default = Ant2\n");
			PHY_SetBBReg(Adapter, 0x858, 0xFFFF, RxDefaultAnt2);
		}
		pHalData->CCK_Ant1_Cnt = 0;
		pHalData->CCK_Ant2_Cnt = 0;
		pHalData->OFDM_Ant1_Cnt = 0;
		pHalData->OFDM_Ant2_Cnt = 0;
	}


}

#endif

void
rtl8192c_InitHalDm(
	IN	PADAPTER	Adapter
	)
{
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	i;

#ifdef CONFIG_USB_HCI
	dm_InitGPIOSetting(Adapter);
#endif

	pdmpriv->DM_Type = DM_Type_ByDriver;	
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
	pdmpriv->UndecoratedSmoothedPWDB = (-1);
	pdmpriv->UndecoratedSmoothedCCK = (-1);
	
	
	//.1 DIG INIT
	pdmpriv->bDMInitialGainEnable = _TRUE;
	pdmpriv->DMFlag |= DYNAMIC_FUNC_DIG;
	dm_DIGInit(Adapter);

	//.2 DynamicTxPower INIT
	pdmpriv->DMFlag |= DYNAMIC_FUNC_HP;
	dm_InitDynamicTxPower(Adapter);

	//.3
	DM_InitEdcaTurbo(Adapter);

	//.4 RateAdaptive INIT
	dm_InitRateAdaptiveMask(Adapter);

	//.5 Tx Power Tracking Init.
	pdmpriv->DMFlag |= DYNAMIC_FUNC_SS;
	DM_InitializeTXPowerTracking(Adapter);

#ifdef CONFIG_BT_COEXIST
	pdmpriv->DMFlag |= DYNAMIC_FUNC_BT;
	dm_InitBtCoexistDM(Adapter);
#endif

	dm_InitDynamicBBPowerSaving(Adapter);

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	pdmpriv->DMFlag |= DYNAMIC_FUNC_ANT_DIV;
	dm_SW_AntennaSwitchInit(Adapter);
#endif

#ifdef CONFIG_HW_ANTENNA_DIVERSITY
	pdmpriv->DMFlag |= DYNAMIC_FUNC_ANT_DIV;
	dm_InitHybridAntDiv(Adapter);
#endif

	dm_RSSIMonitorInit(Adapter);

	pdmpriv->DMFlag_tmp = pdmpriv->DMFlag;

	// Save REG_INIDATA_RATE_SEL value for TXDESC.
	for(i = 0 ; i<32 ; i++)
	{
		pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, REG_INIDATA_RATE_SEL+i) & 0x3f;
	}

#ifdef CONFIG_DM_ADAPTIVITY
	pdmpriv->DMFlag |= DYNAMIC_FUNC_ADAPTIVITY;
	dm_adaptivity_init(Adapter);
#endif

}

#ifdef CONFIG_CONCURRENT_MODE
static void FindMinimumRSSI(PADAPTER Adapter)
{
	PHAL_DATA_TYPE	pbuddy_HalData;
	struct dm_priv *pbuddy_dmpriv;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;

	if(!rtw_buddy_adapter_up(Adapter))
		return;
	
	pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);
	pbuddy_dmpriv = &pbuddy_HalData->dmpriv;

	//get min. [PWDB] when both interfaces are connected
	if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE 
		&& Adapter->stapriv.asoc_sta_count > 2 
		&& check_buddy_fwstate(Adapter, _FW_LINKED)) || 
		(check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE 
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2
		&& check_fwstate(pmlmepriv, _FW_LINKED) == _TRUE) ||
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE) 
		&& check_fwstate(pmlmepriv, _FW_LINKED)
		&& check_buddy_fwstate(Adapter,WIFI_STATION_STATE) 
		&& check_buddy_fwstate(Adapter,_FW_LINKED)))
	{
		if(pdmpriv->UndecoratedSmoothedPWDB > pbuddy_dmpriv->UndecoratedSmoothedPWDB)
			pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}//primary interface is not connected
	else if((check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE 
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2) || 
		(check_buddy_fwstate(Adapter,WIFI_STATION_STATE) 
		&& check_buddy_fwstate(Adapter,_FW_LINKED)))
	{
		pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}
	//secondary is not connected
	else if((check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE 
		&& Adapter->stapriv.asoc_sta_count > 2) || 
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE) 
		&& check_fwstate(pmlmepriv, _FW_LINKED)))
	{
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}
	//both interfaces are not connected
	else
	{
		pdmpriv->UndecoratedSmoothedPWDB = 0;
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}
		
	//primary interface is ap mode
	if(check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE && Adapter->stapriv.asoc_sta_count > 2)
	{
		pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}//secondary interface is ap mode
	else if(check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE && pbuddy_adapter->stapriv.asoc_sta_count > 2)
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB;
	}
	else //both interfaces are not ap mode
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}

} 

#endif //CONFIG_CONCURRENT_MODE

VOID
rtl8192c_HalDmWatchDog(
	IN	PADAPTER	Adapter
	)
{
	BOOLEAN		bFwCurrentInPSMode = _FALSE;
	BOOLEAN		bFwPSAwake = _TRUE;
	u8 hw_init_completed = _FALSE;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
#ifdef CONFIG_CONCURRENT_MODE
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;
#endif //CONFIG_CONCURRENT_MODE

	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->isprimary == _FALSE && pbuddy_adapter) {
		hw_init_completed = pbuddy_adapter->hw_init_completed;
	} else
	#endif
	{
		hw_init_completed = Adapter->hw_init_completed;
	}

	if (hw_init_completed == _FALSE)
		goto skip_dm;

#ifdef CONFIG_LPS
	#if defined(CONFIG_CONCURRENT_MODE)
	if (Adapter->iface_type != IFACE_PORT0 && pbuddy_adapter) {
		bFwCurrentInPSMode = pbuddy_adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(pbuddy_adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	} else
	#endif
	{
		bFwCurrentInPSMode = Adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(Adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	}
#endif

#ifdef CONFIG_P2P_PS
	// Fw is under p2p powersaving mode, driver should stop dynamic mechanism.
	// modifed by thomas. 2011.06.11.
	if(Adapter->wdinfo.p2p_ps_mode)
		bFwPSAwake = _FALSE;
#endif // CONFIG_P2P_PS

	// Stop dynamic mechanism when:
	// 1. RF is OFF. (No need to do DM.)
	// 2. Fw is under power saving mode for FwLPS. (Prevent from SW/FW I/O racing.)
	// 3. IPS workitem is scheduled. (Prevent from IPS sequence to be swapped with DM.
	//     Sometimes DM execution time is longer than 100ms such that the assertion
	//     in MgntActSet_RF_State() called by InactivePsWorkItem will be triggered by 
	//     wating to long for RFChangeInProgress.)
	// 4. RFChangeInProgress is TRUE. (Prevent from broken by IPS/HW/SW Rf off.)
	// Noted by tynli. 2010.06.01.
	//if(rfState == eRfOn)
	if( (hw_init_completed == _TRUE) 
		&& ((!bFwCurrentInPSMode) && bFwPSAwake))
	{
		//
		// Calculate Tx/Rx statistics.
		//
		dm_CheckStatistics(Adapter);

		//
		// For PWDB monitor and record some value for later use.
		//
		PWDB_Monitor(Adapter);

		dm_RSSIMonitorCheck(Adapter);

#ifdef CONFIG_CONCURRENT_MODE
		if(Adapter->adapter_type > PRIMARY_ADAPTER)
			goto _record_initrate;

		FindMinimumRSSI(Adapter);
#endif

		//
		// Dynamic Initial Gain mechanism.
		//
		dm_FalseAlarmCounterStatistics(Adapter);
		dm_DIG(Adapter);
		dm_adaptivity(Adapter);

		//
		//Dynamic BB Power Saving Mechanism
		//
		dm_DynamicBBPowerSaving(Adapter);

		//
		// Dynamic Tx Power mechanism.
		//
		dm_DynamicTxPower(Adapter);

		//
		// Tx Power Tracking.
		//
#if MP_DRIVER == 0
#ifdef CONFIG_BUSY_TRAFFIC_SKIP_PWR_TRACK
		if(pmlmepriv->LinkDetectInfo.bBusyTraffic == _FALSE)
#endif //CONFIG_BUSY_TRAFFIC_SKIP_PWR_TRACK
#endif
			rtl8192c_dm_CheckTXPowerTracking(Adapter);

		//
		// Rate Adaptive by Rx Signal Strength mechanism.
		//
		dm_RefreshRateAdaptiveMask(Adapter);

#ifdef CONFIG_BT_COEXIST
		//BT-Coexist
		dm_BTCoexist(Adapter);
#endif

		// EDCA turbo
		//update the EDCA paramter according to the Tx/RX mode
		//update_EDCA_param(Adapter);
		dm_CheckEdcaTurbo(Adapter);

		//
		// Dynamically switch RTS/CTS protection.
		//
		//dm_CheckProtection(Adapter);

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
		//
		// Software Antenna diversity
		//
		dm_SW_AntennaSwitch(Adapter, SWAW_STEP_PEAK);
#endif

#ifdef CONFIG_HW_ANTENNA_DIVERSITY
		//Hybrid Antenna Diversity
		dm_SelectRXDefault(Adapter);
#endif

#ifdef CONFIG_PCI_HCI
		// 20100630 Joseph: Disable Interrupt Migration mechanism temporarily because it degrades Rx throughput.
		// Tx Migration settings.
		//dm_InterruptMigration(Adapter);

		//if(Adapter->HalFunc.TxCheckStuckHandler(Adapter))
		//	PlatformScheduleWorkItem(&(GET_HAL_DATA(Adapter)->HalResetWorkItem));
#endif


_record_initrate:

		// Read REG_INIDATA_RATE_SEL value for TXDESC.
		if(check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			pdmpriv->INIDATA_RATE[0] = rtw_read8(Adapter, REG_INIDATA_RATE_SEL) & 0x3f;

#ifdef CONFIG_TDLS
			if(Adapter->tdlsinfo.setup_state == TDLS_LINKED_STATE)
			{
				u8 i=1;
				for(; i < (Adapter->tdlsinfo.macid_index) ; i++)
				{
					pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
				}
			}
#endif //CONFIG_TDLS

		}
		else
		{
			u8	i;
			for(i=1 ; i < (Adapter->stapriv.asoc_sta_count + 1); i++)
			{
				pdmpriv->INIDATA_RATE[i] = rtw_read8(Adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
			}
		}
	}

skip_dm:

	// Check GPIO to determine current RF on/off and Pbc status.
	// Check Hardware Radio ON/OFF or not	
	//if(Adapter->MgntInfo.PowerSaveControl.bGpioRfSw)
	//{
		//RTPRINT(FPWR, PWRHW, ("dm_CheckRfCtrlGPIO \n"));
	//	dm_CheckRfCtrlGPIO(Adapter);
	//}

#ifdef CONFIG_PCI_HCI
	if(pHalData->bGpioHwWpsPbc)
#endif
	{
		dm_CheckPbcGPIO(Adapter);				// Add by hpfan 2008-03-11
	}

}

