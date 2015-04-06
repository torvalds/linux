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
 *
 ******************************************************************************/
/*  */
/*  Description: */
/*  */
/*  This file is for 92CE/92CU dynamic mechanism only */
/*  */
/*  */
/*  */

/*  */
/*  include files */
/*  */
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#include <hal_intf.h>

#include <rtl8192d_hal.h>
/* avoid to warn in FreeBSD */
u32 EDCAParam[maxAP][3] =
{          /*  UL			DL */
	{0x5ea322, 0x00a630, 0x00a44f}, /* atheros AP */
	{0x5ea32b, 0x5ea42b, 0x5e4322}, /* broadcom AP */
	{0x3ea430, 0x00a630, 0x3ea44f}, /* cisco AP */
	{0x5ea44f, 0x00a44f, 0x5ea42b}, /* marvell AP */
	{0x5ea422, 0x00a44f, 0x00a44f}, /* ralink AP */
	{0xa44f, 0x5ea44f, 0x5e431c}, /* realtek AP */
	{0x5ea42b, 0xa630, 0x5e431c}, /* airgocap AP */
	{0x5ea42b, 0x5ea42b, 0x5ea42b}, /* unknown AP */
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
static void dm_DIGInit(
	struct rtw_adapter *	adapter
)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;

	dm_digtable->dig_enable_flag = true;
	dm_digtable->dig_ext_port_stage = DIG_EXT_PORT_STAGE_MAX;

	dm_digtable->curigvalue = 0x20;
	dm_digtable->preigvalue = 0x0;

	dm_digtable->curstaconnectstate = dm_digtable->prestaconnectstate = DIG_STA_DISCONNECT;
	dm_digtable->curmultistaconnectstate = DIG_MultiSTA_DISCONNECT;

	dm_digtable->rssilowthresh	= DM_DIG_THRESH_LOW;
	dm_digtable->rssihighthresh	= DM_DIG_THRESH_HIGH;

	dm_digtable->falowthresh	= DM_FALSEALARM_THRESH_LOW;
	dm_digtable->fahighthresh	= DM_FALSEALARM_THRESH_HIGH;

	dm_digtable->rx_gain_range_max = DM_DIG_MAX;
	dm_digtable->rx_gain_range_min = DM_DIG_MIN;
	dm_digtable->rx_gain_range_min_nolink = 0;

	dm_digtable->backoffval = DM_DIG_BACKOFF_DEFAULT;
	dm_digtable->backoffval_range_max = DM_DIG_BACKOFF_MAX;
	dm_digtable->backoffval_range_min = DM_DIG_BACKOFF_MIN;

	dm_digtable->precckpdstate = CCK_PD_STAGE_MAX;
	dm_digtable->curcckpdstate = CCK_PD_STAGE_LOWRSSI;
	dm_digtable->forbiddenigi = DM_DIG_MIN;

	dm_digtable->largefahit = 0;
	dm_digtable->recover_cnt = 0;
}

#ifdef CONFIG_DUALMAC_CONCURRENT
static bool
dm_DualMacGetParameterFromBuddyadapter(
		struct rtw_adapter *	adapter
)
{
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct mlme_priv *pbuddy_mlmepriv = &(Buddyadapter->mlmepriv);

	if (pHalData->MacPhyMode92D != DUALMAC_SINGLEPHY)
		return false;

	if (Buddyadapter == NULL)
		return false;

	if (pHalData->bSlaveOfDMSP)
		return false;

/* sherry sync with 92C_92D, 20110701 */
	if ((check_fwstate(pbuddy_mlmepriv, _FW_LINKED)) && (!check_fwstate(pmlmepriv, _FW_LINKED))
		&& (!check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE)))
		return true;
	else
		return false;
}
#endif

static void
odm_FalseAlarmCounterStatistics_ForSlaveOfDMSP(
	struct rtw_adapter *	adapter
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct FALSE_ALARM_STATISTICS *falsealmcnt = &(pdmpriv->falsealmcnt);
	struct dm_priv	*Buddydmpriv;
	struct FALSE_ALARM_STATISTICS *FlaseAlmCntBuddyadapter;

	if (Buddyadapter == NULL)
		return;

	if (adapter->DualMacConcurrent == false)
		return;

	Buddydmpriv = &GET_HAL_DATA(Buddyadapter)->dmpriv;
	FlaseAlmCntBuddyadapter = &(Buddydmpriv->falsealmcnt);

	falsealmcnt->Cnt_Fast_Fsync =FlaseAlmCntBuddyadapter->Cnt_Fast_Fsync;
	falsealmcnt->Cnt_SB_Search_fail =FlaseAlmCntBuddyadapter->Cnt_SB_Search_fail;
	falsealmcnt->Cnt_Parity_Fail = FlaseAlmCntBuddyadapter->Cnt_Parity_Fail;
	falsealmcnt->Cnt_Rate_Illegal = FlaseAlmCntBuddyadapter->Cnt_Rate_Illegal;
	falsealmcnt->Cnt_Crc8_fail = FlaseAlmCntBuddyadapter->Cnt_Crc8_fail;
	falsealmcnt->Cnt_Mcs_fail = FlaseAlmCntBuddyadapter->Cnt_Mcs_fail;

	falsealmcnt->Cnt_Ofdm_fail =	falsealmcnt->Cnt_Parity_Fail + falsealmcnt->Cnt_Rate_Illegal +
								falsealmcnt->Cnt_Crc8_fail + falsealmcnt->Cnt_Mcs_fail +
								falsealmcnt->Cnt_Fast_Fsync + falsealmcnt->Cnt_SB_Search_fail;

	/* hold cck counter */
	falsealmcnt->Cnt_Cck_fail = FlaseAlmCntBuddyadapter->Cnt_Cck_fail;

	falsealmcnt->Cnt_all = (	falsealmcnt->Cnt_Fast_Fsync +
						falsealmcnt->Cnt_SB_Search_fail +
						falsealmcnt->Cnt_Parity_Fail +
						falsealmcnt->Cnt_Rate_Illegal +
						falsealmcnt->Cnt_Crc8_fail +
						falsealmcnt->Cnt_Mcs_fail +
						falsealmcnt->Cnt_Cck_fail);

#endif
}

static void
odm_FalseAlarmCounterStatistics(
	struct rtw_adapter *	adapter
	)
{
	u32	ret_value;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct FALSE_ALARM_STATISTICS *falsealmcnt = &(pdmpriv->falsealmcnt);
	u8	BBReset;
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter * pbuddy_adapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pbuddy_pHalData = GET_HAL_DATA(pbuddy_adapter);
	struct mlme_priv *pbuddy_pmlmepriv = &(pbuddy_adapter->mlmepriv);
#endif /* CONFIG_CONCURRENT_MODE */
	/* hold ofdm counter */
	PHY_SetBBReg(adapter, rOFDM0_LSTF, BIT31, 1); /* hold page C counter */
	PHY_SetBBReg(adapter, rOFDM1_LSTF, BIT31, 1); /* hold page D counter */

	ret_value = PHY_QueryBBReg(adapter, rOFDM0_FrameSync, bMaskDWord);
	falsealmcnt->Cnt_Fast_Fsync = (ret_value&0xffff);
	falsealmcnt->Cnt_SB_Search_fail = ((ret_value&0xffff0000)>>16);
	ret_value = PHY_QueryBBReg(adapter, rOFDM_PHYCounter1, bMaskDWord);
	falsealmcnt->Cnt_Parity_Fail = ((ret_value&0xffff0000)>>16);
	ret_value = PHY_QueryBBReg(adapter, rOFDM_PHYCounter2, bMaskDWord);
	falsealmcnt->Cnt_Rate_Illegal = (ret_value&0xffff);
	falsealmcnt->Cnt_Crc8_fail = ((ret_value&0xffff0000)>>16);
	ret_value = PHY_QueryBBReg(adapter, rOFDM_PHYCounter3, bMaskDWord);
	falsealmcnt->Cnt_Mcs_fail = (ret_value&0xffff);

	falsealmcnt->Cnt_Ofdm_fail =	falsealmcnt->Cnt_Parity_Fail + falsealmcnt->Cnt_Rate_Illegal +
								falsealmcnt->Cnt_Crc8_fail + falsealmcnt->Cnt_Mcs_fail +
								falsealmcnt->Cnt_Fast_Fsync + falsealmcnt->Cnt_SB_Search_fail;

	if (pHalData->CurrentBandType92D != BAND_ON_5G)
	{
		/* hold cck counter */

		ret_value = PHY_QueryBBReg(adapter, rCCK0_FACounterLower, bMaskByte0);
		falsealmcnt->Cnt_Cck_fail = ret_value;

		ret_value = PHY_QueryBBReg(adapter, rCCK0_FACounterUpper, bMaskByte3);
		falsealmcnt->Cnt_Cck_fail +=  (ret_value& 0xff)<<8;
	}
	else
	{
		falsealmcnt->Cnt_Cck_fail = 0;
	}

	falsealmcnt->Cnt_all = (	falsealmcnt->Cnt_Fast_Fsync +
						falsealmcnt->Cnt_SB_Search_fail +
						falsealmcnt->Cnt_Parity_Fail +
						falsealmcnt->Cnt_Rate_Illegal +
						falsealmcnt->Cnt_Crc8_fail +
						falsealmcnt->Cnt_Mcs_fail +
						falsealmcnt->Cnt_Cck_fail);
	adapter->recvpriv.falsealmcnt_all = falsealmcnt->Cnt_all;
#ifdef CONFIG_CONCURRENT_MODE
	if (pbuddy_adapter)
		pbuddy_adapter->recvpriv.falsealmcnt_all = falsealmcnt->Cnt_all;
#endif /* CONFIG_CONCURRENT_MODE */

	/* reset false alarm counter registers */
	PHY_SetBBReg(adapter, rOFDM1_LSTF, 0x08000000, 1);
	PHY_SetBBReg(adapter, rOFDM1_LSTF, 0x08000000, 0);
	/* update ofdm counter */
	PHY_SetBBReg(adapter, rOFDM0_LSTF, BIT31, 0); /* update page C counter */
	PHY_SetBBReg(adapter, rOFDM1_LSTF, BIT31, 0); /* update page D counter */
	if (pHalData->CurrentBandType92D != BAND_ON_5G) {
		/* reset cck counter */
		PHY_SetBBReg(adapter, rCCK0_FalseAlarmReport, 0x0000c000, 0);
		/* enable cck counter */
		PHY_SetBBReg(adapter, rCCK0_FalseAlarmReport, 0x0000c000, 2);
	}

	/* BB Reset */
	if (IS_HARDWARE_TYPE_8192D(adapter))
	{
		if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if ((pHalData->CurrentBandType92D == BAND_ON_2_4G) && pHalData->bMasterOfDMSP && (check_fwstate(pmlmepriv, _FW_LINKED) == false))
			{
				/* before BB reset should do clock gated */
				rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
				BBReset = rtw_read8(adapter, 0x02);
				rtw_write8(adapter, 0x02, BBReset&(~BIT0));
				rtw_write8(adapter, 0x02, BBReset|BIT0);
				/* undo clock gated */
				rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
			}
		}
		else
		{
			if ((pHalData->CurrentBandType92D == BAND_ON_2_4G) &&(check_fwstate(pmlmepriv, _FW_LINKED) == false)
#ifdef CONFIG_CONCURRENT_MODE
				 && (check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == false)
#endif /* CONFIG_CONCURRENT_MODE */
			)
			{
				/* before BB reset should do clock gated */
				rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
				BBReset = rtw_read8(adapter, 0x02);
				rtw_write8(adapter, 0x02, BBReset&(~BIT0));
				rtw_write8(adapter, 0x02, BBReset|BIT0);
				/* undo clock gated */
				rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
			}
		}
	}
	else if (check_fwstate(pmlmepriv, _FW_LINKED) == false)
	{
		/* before BB reset should do clock gated */
		rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)|(BIT31));
		BBReset = rtw_read8(adapter, 0x02);
		rtw_write8(adapter, 0x02, BBReset&(~BIT0));
		rtw_write8(adapter, 0x02, BBReset|BIT0);
		/* undo clock gated */
		rtw_write32(adapter, rFPGA0_XCD_RFParameter, rtw_read32(adapter, rFPGA0_XCD_RFParameter)&(~BIT31));
	}
}

static void
odm_FindMinimumRSSI_Dmsp(
	struct rtw_adapter *	adapter
)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	rssi_val_min_back_for_mac0;
	bool		bGetValueFromBuddyadapter = dm_DualMacGetParameterFromBuddyadapter(adapter);
	bool		rest_rssi = false;
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	struct dm_priv	*Buddydmpriv;

	if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
	{
		if (Buddyadapter!= NULL)
		{
			Buddydmpriv = &GET_HAL_DATA(Buddyadapter)->dmpriv;
			if (pHalData->bSlaveOfDMSP)
			{
				Buddydmpriv->RssiValMinForAnotherMacOfDMSP = pdmpriv->MinUndecoratedPWDBForDM;
			}
			else
			{
				if (bGetValueFromBuddyadapter)
				{
					rest_rssi = true;
					rssi_val_min_back_for_mac0 = pdmpriv->MinUndecoratedPWDBForDM;
					pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->RssiValMinForAnotherMacOfDMSP;
				}
			}
		}

	}

	if (rest_rssi)
	{
		rest_rssi = false;
		pdmpriv->MinUndecoratedPWDBForDM = rssi_val_min_back_for_mac0;
	}
#endif
}

static void
odm_FindMinimumRSSI_92D(
struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_priv	*pmlmepriv = &adapter->mlmepriv;

	/* 1 1.Determine the minimum RSSI */
	if ((check_fwstate(pmlmepriv, _FW_LINKED) == false) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->MinUndecoratedPWDBForDM = 0;
	}
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)	/*  Default port */
	{
		if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		}
		else
		{
			pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->UndecoratedSmoothedPWDB;
		}
	}
	else /*  associated entry pwdb */
	{
		pdmpriv->MinUndecoratedPWDBForDM = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	}

	odm_FindMinimumRSSI_Dmsp(adapter);

}

static u8
odm_initial_gain_MinPWDB(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	rssi_val_min = 0;
	if (pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0)
		rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	else
		rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;

	return (u8)rssi_val_min;
}

static void
DM_Write_DIG_DMSP(
	struct rtw_adapter *	adapter
	)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	bool		bGetValueFromOtherMac = dm_DualMacGetParameterFromBuddyadapter(adapter);
	struct dm_priv	*Buddydmpriv;

	if (Buddyadapter == NULL)
	{
		if (pHalData->bMasterOfDMSP)
		{
			PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, dm_digtable->curigvalue);
			PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, dm_digtable->curigvalue);
			dm_digtable->preigvalue = dm_digtable->curigvalue;
		}
		else
		{
			dm_digtable->preigvalue = dm_digtable->curigvalue;
		}
		return;
	}

	if (bGetValueFromOtherMac)
	{
		if (pdmpriv->bWriteDigForAnotherMacOfDMSP)
		{
			pdmpriv->bWriteDigForAnotherMacOfDMSP = false;
			PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, pdmpriv->CurDigValueForAnotherMacOfDMSP);
			PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, pdmpriv->CurDigValueForAnotherMacOfDMSP);
		}
	}

	Buddydmpriv = &GET_HAL_DATA(Buddyadapter)->dmpriv;

	if (dm_digtable->preigvalue != dm_digtable->curigvalue)
	{
		/*  Set initial gain. */
		/*  20100211 Joseph: Set only BIT0~BIT6 for DIG. BIT7 is the function switch of Antenna diversity. */
		/*  Just not to modified it for SD3 testing. */
		 if (pHalData->bSlaveOfDMSP)
		 {
			Buddydmpriv->bWriteDigForAnotherMacOfDMSP = true;
			Buddydmpriv->CurDigValueForAnotherMacOfDMSP =  dm_digtable->curigvalue;
		 }
		else
		{
			if (!bGetValueFromOtherMac)
			{
				PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, dm_digtable->curigvalue);
				PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, dm_digtable->curigvalue);
			}
		}
		dm_digtable->preigvalue = dm_digtable->curigvalue;
	}
#endif
}

static void
DM_Write_DIG(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;

	if (dm_digtable->dig_enable_flag == false)
		return;

	if ((dm_digtable->preigvalue != dm_digtable->curigvalue) || (adapter->bForceWriteInitGain))
	{
		/*  Set initial gain. */
		/*  20100211 Joseph: Set only BIT0~BIT6 for DIG. BIT7 is the function switch of Antenna diversity. */
		/*  Just not to modified it for SD3 testing. */
		PHY_SetBBReg(adapter, rOFDM0_XAAGCCore1, 0x7f, dm_digtable->curigvalue);
		PHY_SetBBReg(adapter, rOFDM0_XBAGCCore1, 0x7f, dm_digtable->curigvalue);
		if (dm_digtable->curigvalue != 0x17)
			dm_digtable->preigvalue = dm_digtable->curigvalue;
	}
}

static void odm_DIG(
	struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv	*pmlmepriv = &(adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct registry_priv	 *pregistrypriv = &adapter->registrypriv;
	struct FALSE_ALARM_STATISTICS *falsealmcnt = &(pdmpriv->falsealmcnt);
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;
	static u8	DIG_Dynamic_MIN_0 = 0x25;
	static u8	DIG_Dynamic_MIN_1 = 0x25;
	u8	DIG_Dynamic_MIN;
	static bool	bMediaConnect_0 = false;
	static bool	bMediaConnect_1 = false;
	bool		FirstConnect;
	u8	TxRate = rtw_read8(adapter, REG_INIDATA_RATE_SEL);
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter * pbuddy_adapter = adapter->pbuddy_adapter;
	struct hal_data_8192du *pbuddy_pHalData = GET_HAL_DATA(pbuddy_adapter);
	struct mlme_priv	*pbuddy_pmlmepriv = &(pbuddy_adapter->mlmepriv);
	struct dm_priv	*pbuddy_pdmpriv = &pbuddy_pHalData->dmpriv;
#endif /* CONFIG_CONCURRENT_MODE */

	if (IS_HARDWARE_TYPE_8192D(adapter))
	{
		if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if (pHalData->bMasterOfDMSP)
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true) && (bMediaConnect_0 == false);
			}
			else
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_1;
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true) && (bMediaConnect_1 == false);
			}
		}
		else
		{
			if (pHalData->CurrentBandType92D==BAND_ON_5G)
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
#ifdef CONFIG_CONCURRENT_MODE
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true ||check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true
				) && (bMediaConnect_0 == false);
#else
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true) && (bMediaConnect_0 == false);
#endif /* CONFIG_CONCURRENT_MODE */
			}
			else
			{
				DIG_Dynamic_MIN = DIG_Dynamic_MIN_1;
#ifdef CONFIG_CONCURRENT_MODE
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true
				) && (bMediaConnect_1 == false);
#else
				FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true) && (bMediaConnect_1 == false);
#endif /* CONFIG_CONCURRENT_MODE */
			}
		}
	}
	else
	{
		DIG_Dynamic_MIN = DIG_Dynamic_MIN_0;
#ifdef CONFIG_CONCURRENT_MODE
		FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true
		) && (bMediaConnect_0 == false);
#else
		FirstConnect = (check_fwstate(pmlmepriv, _FW_LINKED) == true) && (bMediaConnect_0 == false);
#endif /* CONFIG_CONCURRENT_MODE */
	}

#ifndef CONFIG_CONCURRENT_MODE
	if (pdmpriv->bDMInitialGainEnable == false)
		return;
#endif /* CONFIG_CONCURRENT_MODE */

#ifdef CONFIG_CONCURRENT_MODE
	if (!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG) || !(pbuddy_pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
#else
	if (!(pdmpriv->DMFlag & DYNAMIC_FUNC_DIG))
#endif /* CONFIG_CONCURRENT_MODE */
		return;

#ifdef CONFIG_CONCURRENT_MODE
	if (adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS || pbuddy_adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)
#else
	if (adapter->mlmeextpriv.sitesurvey_res.state == SCAN_PROCESS)
#endif /* CONFIG_CONCURRENT_MODE */
		return;

	/* 1 Boundary Decision */
#ifdef CONFIG_CONCURRENT_MODE
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true)
#else
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
#endif /* CONFIG_CONCURRENT_MODE */
	{
		/* 2 Get minimum RSSI value among associated devices */
		dm_digtable->rssi_val_min = odm_initial_gain_MinPWDB(adapter);

		/* 2 Modify DIG upper bound */
		if ((dm_digtable->rssi_val_min + 20) > DM_DIG_MAX)
			dm_digtable->rx_gain_range_max = DM_DIG_MAX;
		else
			dm_digtable->rx_gain_range_max = dm_digtable->rssi_val_min + 20;
		/* 2 Modify DIG lower bound */
		if ((falsealmcnt->Cnt_all > 500)&&(DIG_Dynamic_MIN < 0x25))
			DIG_Dynamic_MIN++;
		if ((falsealmcnt->Cnt_all < 500)&&(DIG_Dynamic_MIN > DM_DIG_MIN))
			DIG_Dynamic_MIN--;
		if ((dm_digtable->rssi_val_min < 8) && (DIG_Dynamic_MIN > DM_DIG_MIN))
			DIG_Dynamic_MIN--;
	} else {
		dm_digtable->rx_gain_range_max = DM_DIG_MAX;
		DIG_Dynamic_MIN = DM_DIG_MIN;
	}

	/* 1 Modify DIG lower bound, deal with abnorally large false alarm */
	if (falsealmcnt->Cnt_all > 10000)
	{
		dm_digtable->largefahit++;
		if (dm_digtable->forbiddenigi < dm_digtable->curigvalue)
		{
			dm_digtable->forbiddenigi = dm_digtable->curigvalue;
			dm_digtable->largefahit = 1;
		}

		if (dm_digtable->largefahit >= 3)
		{
			if ((dm_digtable->forbiddenigi+1) >dm_digtable->rx_gain_range_max)
				dm_digtable->rx_gain_range_min = dm_digtable->rx_gain_range_max;
			else
				dm_digtable->rx_gain_range_min = (dm_digtable->forbiddenigi + 1);
			dm_digtable->recover_cnt = 3600; /* 3600=2hr */
		}

	}
	else
	{
		/* Recovery mechanism for IGI lower bound */
		if (dm_digtable->recover_cnt != 0)
			dm_digtable->recover_cnt --;
		else
		{
			if (dm_digtable->largefahit == 0)
			{
				if ((dm_digtable->forbiddenigi -1) < DIG_Dynamic_MIN) /* DM_DIG_MIN) */
				{
					dm_digtable->forbiddenigi = DIG_Dynamic_MIN; /* DM_DIG_MIN; */
					dm_digtable->rx_gain_range_min = DIG_Dynamic_MIN; /* DM_DIG_MIN; */
				}
				else
				{
					dm_digtable->forbiddenigi --;
					dm_digtable->rx_gain_range_min = (dm_digtable->forbiddenigi + 1);
				}
			}
			else if (dm_digtable->largefahit == 3)
			{
				dm_digtable->largefahit = 0;
			}
		}

	}

	/* 1 Adjust initial gain by false alarm */
#ifdef CONFIG_CONCURRENT_MODE
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true)
#else
	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
#endif /* CONFIG_CONCURRENT_MODE */
	{
		if (FirstConnect) {
			dm_digtable->curigvalue = dm_digtable->rssi_val_min;
		} else {
			if (IS_HARDWARE_TYPE_8192D(adapter)) {
				if (falsealmcnt->Cnt_all > DM_DIG_FA_TH2_92D)
					dm_digtable->curigvalue = dm_digtable->preigvalue+2;
				else if (falsealmcnt->Cnt_all > DM_DIG_FA_TH1_92D)
					dm_digtable->curigvalue = dm_digtable->preigvalue+1;
				else if (falsealmcnt->Cnt_all < DM_DIG_FA_TH0_92D)
					dm_digtable->curigvalue =dm_digtable->preigvalue-1;
			} else {
				if (falsealmcnt->Cnt_all > DM_DIG_FA_TH2)
					dm_digtable->curigvalue = dm_digtable->preigvalue+2;
				else if (falsealmcnt->Cnt_all > DM_DIG_FA_TH1)
					dm_digtable->curigvalue = dm_digtable->preigvalue+1;
				else if (falsealmcnt->Cnt_all < DM_DIG_FA_TH0)
					dm_digtable->curigvalue = dm_digtable->preigvalue-1;
			}
		}
	} else {
		/*	There is no network interface connects to AP. */
		if (0 == dm_digtable->rx_gain_range_min_nolink) {
			/*	First time to enter odm_DIG function and set the default value to rx_gain_range_min_nolink */
			dm_digtable->rx_gain_range_min_nolink = 0x30;
		} else {
			if ((falsealmcnt->Cnt_all > 1000) && (falsealmcnt->Cnt_all < 2000)) {
				dm_digtable->rx_gain_range_min_nolink = ((dm_digtable->rx_gain_range_min_nolink + 1) > 0x3e) ?
							0x3e : (dm_digtable->rx_gain_range_min_nolink + 1) ;
			} else if (falsealmcnt->Cnt_all >= 2000) {
				dm_digtable->rx_gain_range_min_nolink = ((dm_digtable->rx_gain_range_min_nolink + 2) > 0x3e) ?
							0x3e : (dm_digtable->rx_gain_range_min_nolink + 2) ;
			} else if (falsealmcnt->Cnt_all < 500) {
				dm_digtable->rx_gain_range_min_nolink = ((dm_digtable->rx_gain_range_min_nolink - 1) < 0x1e) ?
							0x1e : (dm_digtable->rx_gain_range_min_nolink - 1) ;
			}
		}

		dm_digtable->curigvalue = dm_digtable->rx_gain_range_min_nolink;
	}
	/* 1 Check initial gain by upper/lower bound */
	if (dm_digtable->curigvalue > dm_digtable->rx_gain_range_max)
		dm_digtable->curigvalue = dm_digtable->rx_gain_range_max;

	if (dm_digtable->curigvalue < dm_digtable->rx_gain_range_min)
		dm_digtable->curigvalue = dm_digtable->rx_gain_range_min;

	if (adapter->bRxRSSIDisplay)
	{
		DBG_8192D("Modify DIG algorithm for DMP DIG: RxGainMin = %X, RxGainMax = %X\n",
			dm_digtable->rx_gain_range_min,
			dm_digtable->rx_gain_range_max);
	}

	if (IS_HARDWARE_TYPE_8192D(adapter))
	{
		/* sherry  delete DualMacSmartConncurrent 20110517 */
		if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			DM_Write_DIG_DMSP(adapter);
			if (pHalData->bMasterOfDMSP)
			{
				bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
				DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
				bMediaConnect_1 = check_fwstate(pmlmepriv, _FW_LINKED);
				DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
		else
		{
			DM_Write_DIG(adapter);
			if (pHalData->CurrentBandType92D==BAND_ON_5G)
			{
#ifdef CONFIG_CONCURRENT_MODE
				bMediaConnect_0 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
				bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif /* CONFIG_CONCURRENT_MODE */
				DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
			}
			else
			{
#ifdef CONFIG_CONCURRENT_MODE
				bMediaConnect_1 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
				bMediaConnect_1 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif /* CONFIG_CONCURRENT_MODE */
				DIG_Dynamic_MIN_1 = DIG_Dynamic_MIN;
			}
		}
	}
	else
	{
		DM_Write_DIG(adapter);
#ifdef CONFIG_CONCURRENT_MODE
		bMediaConnect_0 = (check_fwstate(pmlmepriv, _FW_LINKED) || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED));
#else
		bMediaConnect_0 = check_fwstate(pmlmepriv, _FW_LINKED);
#endif /* CONFIG_CONCURRENT_MODE */
		DIG_Dynamic_MIN_0 = DIG_Dynamic_MIN;
	}

	if ((pregistrypriv->lowrate_two_xmit) && IS_HARDWARE_TYPE_8192D(adapter) &&
		(pHalData->MacPhyMode92D != DUALMAC_DUALPHY) && (!pregistrypriv->special_rf_path))
	{
		/* for Use 2 path Tx to transmit MCS0~7 and legacy mode */
#ifdef CONFIG_CONCURRENT_MODE
		if (check_fwstate(pmlmepriv, _FW_LINKED) == true || check_fwstate(pbuddy_pmlmepriv, _FW_LINKED) == true)
#else
		if (check_fwstate(pmlmepriv, _FW_LINKED) == true)
#endif /* CONFIG_CONCURRENT_MODE */
		{
			if (dm_digtable->rssi_val_min  <= 30)   /* low rate 2T2R settings */
			{
				/* Reg90C=0x83321333 (OFDM 2T) */
				/* RegA07=0xc1            (CCK 2T2R) */
				/* RegA11=0x9b            (no switch polarity of two antenna) */
				/* RegA20=0x10            (extend CS ratio as X1.125) */
				/* RegA2E=0xdf             (MRC on) */
				/* RegA2F=0x10            (CDD 90ns for path-B) */
				/* RegA75=0x01            (antenna selection enable) */

				rtw_write32(adapter, 0x90C, 0x83321333);
				rtw_write8(adapter, 0xA07, 0xc1);
				rtw_write8(adapter, 0xA11, 0x9b);
				rtw_write8(adapter, 0xA20, 0x10);
				rtw_write8(adapter, 0xA2E, 0xdf);
				rtw_write8(adapter, 0xA2F, 0x10);
				rtw_write8(adapter, 0xA75, 0x01);

			}
			else if (dm_digtable->rssi_val_min  >= 35)  /* low rate 1T1R Settings */
			{
				/* Reg90C=0x81121313 */
				/* RegA07=0x80 */
				/* RegA11=0xbb */
				/* RegA20=0x00 */
				/* RegA2E=0xd3 */
				/* RegA2F=0x00 */
				/* RegA75=0x00 */

				rtw_write32(adapter, 0x90C, 0x81121313);
				rtw_write8(adapter, 0xA07, 0x80);
				rtw_write8(adapter, 0xA11, 0xbb);
				rtw_write8(adapter, 0xA20, 0x00);
				rtw_write8(adapter, 0xA2E, 0xd3);
				rtw_write8(adapter, 0xA2F, 0x00);
				rtw_write8(adapter, 0xA75, 0x00);
			}

		}

	}

}

static u8
dm_initial_gain_MinPWDB(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	s32	rssi_val_min = 0;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;

	if ((dm_digtable->curmultistaconnectstate == DIG_MultiSTA_CONNECT) &&
	   (dm_digtable->curstaconnectstate == DIG_STA_CONNECT)) {
		if (pdmpriv->EntryMinUndecoratedSmoothedPWDB != 0)
			rssi_val_min  =  (pdmpriv->EntryMinUndecoratedSmoothedPWDB > pdmpriv->UndecoratedSmoothedPWDB)?
					pdmpriv->UndecoratedSmoothedPWDB:pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		else
			rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	}
	else if (	dm_digtable->curstaconnectstate == DIG_STA_CONNECT ||
			dm_digtable->curstaconnectstate == DIG_STA_BEFORE_CONNECT)
		rssi_val_min = pdmpriv->UndecoratedSmoothedPWDB;
	else if (dm_digtable->curmultistaconnectstate == DIG_MultiSTA_CONNECT)
		rssi_val_min = pdmpriv->EntryMinUndecoratedSmoothedPWDB;

	return (u8)rssi_val_min;
}

static void dm_CCK_PacketDetectionThresh_DMSP(
	struct rtw_adapter *	adapter)
{
#ifdef CONFIG_DUALMAC_CONCURRENT
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	bool		bGetValueFromBuddyadapter = dm_DualMacGetParameterFromBuddyadapter(adapter);
	struct dm_priv	*Buddydmpriv;

	if (dm_digtable->curstaconnectstate == DIG_STA_CONNECT)
	{
		dm_digtable->rssi_val_min = dm_initial_gain_MinPWDB(adapter);
		if (dm_digtable->precckpdstate == CCK_PD_STAGE_LOWRSSI)
		{
			if (dm_digtable->rssi_val_min <= 25)
				dm_digtable->curcckpdstate = CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->curcckpdstate = CCK_PD_STAGE_HIGHRSSI;
		}
		else {
			if (dm_digtable->rssi_val_min <= 20)
				dm_digtable->curcckpdstate = CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->curcckpdstate = CCK_PD_STAGE_HIGHRSSI;
		}
	}
	else
		dm_digtable->curcckpdstate=CCK_PD_STAGE_MAX;

	if (bGetValueFromBuddyadapter)
	{
		DBG_8192D("dm_CCK_PacketDetectionThresh_DMSP(): mac 1 connect,mac 0 disconnect case\n");
		if (pdmpriv->bChangeCCKPDStateForAnotherMacOfDMSP)
		{
			DBG_8192D("dm_CCK_PacketDetectionThresh_DMSP(): mac 0 set for mac1\n");
			if (pdmpriv->curcckpdstateForAnotherMacOfDMSP == CCK_PD_STAGE_LOWRSSI)
			{
				PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0x83);
			}
			else
			{
				PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0xcd);

			}
			pdmpriv->bChangeCCKPDStateForAnotherMacOfDMSP = false;
		}
	}

	if (dm_digtable->precckpdstate != dm_digtable->curcckpdstate)
	{
		if (Buddyadapter == NULL)
		{
			DBG_8192D("dm_CCK_PacketDetectionThresh_DMSP(): Buddyadapter == NULL case\n");
			if (pHalData->bSlaveOfDMSP)
			{
				dm_digtable->precckpdstate = dm_digtable->curcckpdstate;
			}
			else
			{
				if (dm_digtable->curcckpdstate == CCK_PD_STAGE_LOWRSSI)
				{
					PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0x83);
				}
				else
				{
					PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0xcd);

				}
				dm_digtable->precckpdstate = dm_digtable->curcckpdstate;
			}
			return;
		}

		if (pHalData->bSlaveOfDMSP)
		{
			Buddydmpriv = &GET_HAL_DATA(Buddyadapter)->dmpriv;
			DBG_8192D("dm_CCK_PacketDetectionThresh_DMSP(): bslave case\n");
			Buddydmpriv->bChangeCCKPDStateForAnotherMacOfDMSP = true;
			Buddydmpriv->curcckpdstateForAnotherMacOfDMSP = dm_digtable->curcckpdstate;
		}
		else
		{
			if (!bGetValueFromBuddyadapter)
			{
				DBG_8192D("dm_CCK_PacketDetectionThresh_DMSP(): mac 0 set for mac0\n");
				if (dm_digtable->curcckpdstate == CCK_PD_STAGE_LOWRSSI)
				{
					PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0x83);
				}
				else
				{
					PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0xcd);

				}
			}
		}
		dm_digtable->precckpdstate = dm_digtable->curcckpdstate;
	}
#endif
}

static void dm_CCK_PacketDetectionThresh(struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct DIG_T *dm_digtable = &pdmpriv->DM_DigTable;

	if (dm_digtable->curstaconnectstate == DIG_STA_CONNECT) {
		if (dm_digtable->precckpdstate == CCK_PD_STAGE_LOWRSSI) {
			if (pdmpriv->MinUndecoratedPWDBForDM <= 25)
				dm_digtable->curcckpdstate = CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->curcckpdstate = CCK_PD_STAGE_HIGHRSSI;
		} else {
			if (pdmpriv->MinUndecoratedPWDBForDM <= 20)
				dm_digtable->curcckpdstate = CCK_PD_STAGE_LOWRSSI;
			else
				dm_digtable->curcckpdstate = CCK_PD_STAGE_HIGHRSSI;
		}
	} else {
		dm_digtable->curcckpdstate=CCK_PD_STAGE_LOWRSSI;
	}

	if (dm_digtable->precckpdstate != dm_digtable->curcckpdstate) {
		if (dm_digtable->curcckpdstate == CCK_PD_STAGE_LOWRSSI)
			PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0x83);
		else
			PHY_SetBBReg(adapter, rCCK0_CCA, maskbyte2, 0xcd);
		dm_digtable->precckpdstate = dm_digtable->curcckpdstate;
	}
}

static void dm_1R_CCA(struct rtw_adapter *adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct PS_T *dm_pstable = &pdmpriv->DM_PSTable;
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

      if (pHalData->CurrentBandType92D == BAND_ON_5G)
      {
		if (pdmpriv->MinUndecoratedPWDBForDM != 0)
		{
			if (dm_pstable->preccastate == CCA_2R || dm_pstable->preccastate == CCA_MAX)
			{
				if (pdmpriv->MinUndecoratedPWDBForDM >= 35)
					dm_pstable->curccastate = CCA_1R;
				else
					dm_pstable->curccastate = CCA_2R;

			}
			else {
				if (pdmpriv->MinUndecoratedPWDBForDM <= 30)
					dm_pstable->curccastate = CCA_2R;
				else
					dm_pstable->curccastate = CCA_1R;
			}
		}
		else	/* disconnect */
		{
			dm_pstable->curccastate=CCA_MAX;
		}

		if (dm_pstable->preccastate != dm_pstable->curccastate)
		{
			if (dm_pstable->curccastate == CCA_1R)
			{
				if (pHalData->rf_type == RF_2T2R)
				{
					if (pregistrypriv->special_rf_path == 1) /*  path A only */
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
					else if (pregistrypriv->special_rf_path == 2) /* path B only */
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
					else
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x13);
					/* PHY_SetBBReg(adapter, 0xe70, bMaskByte3, 0x20); */
				}
				else
				{
					if (pregistrypriv->special_rf_path == 1) /*  path A only */
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
					else if (pregistrypriv->special_rf_path == 2) /* path B only */
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
					else
						PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x23);
				}
			} else if (dm_pstable->curccastate == CCA_2R || dm_pstable->curccastate == CCA_MAX) {
				if (pregistrypriv->special_rf_path == 1) /*  path A only */
					PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x11);
				else if (pregistrypriv->special_rf_path == 2) /* path B only */
					PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x22);
				else
					PHY_SetBBReg(adapter, rOFDM0_TRxPathEnable  , bMaskByte0, 0x33);
			}
			dm_pstable->preccastate = dm_pstable->curccastate;
		}
	}
}

static void dm_InitDynamicTxPower(struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	pdmpriv->bDynamicTxPowerEnable = false;

	pdmpriv->LastDTPLvl = TxHighPwrLevel_Normal;
	pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
}

static void odm_DynamicTxPower_92D(struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct mlme_priv *pmlmepriv = &(adapter->mlmepriv);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int	UndecoratedSmoothedPWDB;

#ifdef CONFIG_DUALMAC_CONCURRENT
	struct rtw_adapter *	Buddyadapter = adapter->pbuddy_adapter;
	struct dm_priv	*pbuddy_dmpriv = NULL;
	bool		bGetValueFromBuddyadapter = dm_DualMacGetParameterFromBuddyadapter(adapter);
	u8		HighPowerLvlBackForMac0 = TxHighPwrLevel_Level1;
#endif

	/*  If dynamic high power is disabled. */
	if ((pdmpriv->bDynamicTxPowerEnable != true) ||
		(!(pdmpriv->DMFlag & DYNAMIC_FUNC_HP)))
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		return;
	}

	/*  STA not connected and AP not connected */
	if ((check_fwstate(pmlmepriv, _FW_LINKED) != true) &&
		(pdmpriv->EntryMinUndecoratedSmoothedPWDB == 0))
	{
		pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;

		/* the LastDTPlvl should reset when disconnect, */
		/* otherwise the tx power level wouldn't change when disconnect and connect again. */
		/*  Maddest 20091220. */
		pdmpriv->LastDTPLvl=TxHighPwrLevel_Normal;
		return;
	}

	if (check_fwstate(pmlmepriv, _FW_LINKED) == true)	/*  Default port */
	{
		/* todo: AP Mode */
		if ((check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE) == true) ||
		       (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) == true))
		{
			UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
		}
		else
		{
			UndecoratedSmoothedPWDB = pdmpriv->UndecoratedSmoothedPWDB;
		}
	}
	else /*  associated entry pwdb */
	{
		UndecoratedSmoothedPWDB = pdmpriv->EntryMinUndecoratedSmoothedPWDB;
	}

	if (pHalData->CurrentBandType92D == BAND_ON_5G) {
		if (UndecoratedSmoothedPWDB >= 0x33)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
		}
		else if ((UndecoratedSmoothedPWDB <0x33) &&
			(UndecoratedSmoothedPWDB >= 0x2b))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
		}
		else if (UndecoratedSmoothedPWDB < 0x2b)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		}
	}
	else
	{
		if (UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL2)
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level2;
		}
		else if ((UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL2-3)) &&
			(UndecoratedSmoothedPWDB >= TX_POWER_NEAR_FIELD_THRESH_LVL1))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Level1;
		}
		else if (UndecoratedSmoothedPWDB < (TX_POWER_NEAR_FIELD_THRESH_LVL1-5))
		{
			pdmpriv->DynamicTxHighPowerLvl = TxHighPwrLevel_Normal;
		}
	}

#ifdef CONFIG_DUALMAC_CONCURRENT
	if (bGetValueFromBuddyadapter)
	{
		DBG_8192D("dm_DynamicTxPower() mac 0 for mac 1\n");
		if (pdmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP)
		{
			DBG_8192D("dm_DynamicTxPower() change value\n");
			HighPowerLvlBackForMac0 = pdmpriv->DynamicTxHighPowerLvl;
			pdmpriv->DynamicTxHighPowerLvl = pdmpriv->CurTxHighLvlForAnotherMacOfDMSP;
			PHY_SetTxPowerLevel8192D(adapter, pHalData->CurrentChannel);
			pdmpriv->DynamicTxHighPowerLvl = HighPowerLvlBackForMac0;
			pdmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP = false;
		}
	}
#endif

	if ((pdmpriv->DynamicTxHighPowerLvl != pdmpriv->LastDTPLvl))
	{
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (adapter->DualMacConcurrent == true)
		{
			if (Buddyadapter == NULL)
			{
				DBG_8192D("dm_DynamicTxPower() Buddyadapter == NULL case\n");
				if (!pHalData->bSlaveOfDMSP)
				{
					PHY_SetTxPowerLevel8192D(adapter, pHalData->CurrentChannel);
				}
			}
			else
			{
				if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
				{
					DBG_8192D("dm_DynamicTxPower() Buddyadapter DMSP\n");
					if (pHalData->bSlaveOfDMSP)
					{
						DBG_8192D("dm_DynamicTxPower() bslave case\n");
						pbuddy_dmpriv = &GET_HAL_DATA(Buddyadapter)->dmpriv;
						pbuddy_dmpriv->bChangeTxHighPowerLvlForAnotherMacOfDMSP = true;
						pbuddy_dmpriv->CurTxHighLvlForAnotherMacOfDMSP = pdmpriv->DynamicTxHighPowerLvl;
					}
					else
					{
						DBG_8192D("dm_DynamicTxPower() master case\n");
						if (!bGetValueFromBuddyadapter)
						{
							DBG_8192D("dm_DynamicTxPower() mac 0 for mac 0\n");
							PHY_SetTxPowerLevel8192D(adapter, pHalData->CurrentChannel);
						}
					}
				}
				else
				{
					DBG_8192D("dm_DynamicTxPower() Buddyadapter DMDP\n");
					PHY_SetTxPowerLevel8192D(adapter, pHalData->CurrentChannel);
				}
			}
		}
		else
#endif
		{
			PHY_SetTxPowerLevel8192D(adapter, pHalData->CurrentChannel);
		}
	}
	pdmpriv->LastDTPLvl = pdmpriv->DynamicTxHighPowerLvl;
}

static void PWDB_Monitor(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	int	i;
	int	tmpEntryMaxPWDB=0, tmpEntryMinPWDB=0xff;
	u8	sta_cnt=0;
	u32	PWDB_rssi[NUM_STA]={0};/* 0~15]:MACID, [16~31]:PWDB_rssi */

	if (check_fwstate(&adapter->mlmepriv, WIFI_AP_STATE|WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true)
	{
		struct list_head *plist, *phead;
		struct sta_info *psta;
		struct sta_priv *pstapriv = &adapter->stapriv;
		u8 bcast_addr[ETH_ALEN]= {0xff,0xff,0xff,0xff,0xff,0xff};

		spin_lock_bh(&pstapriv->sta_hash_lock);

		for (i=0; i< NUM_STA; i++)
		{
			phead = &(pstapriv->sta_hash[i]);
			plist = phead->next;

			while ((rtw_end_of_queue_search(phead, plist)) == false)
			{
				psta = container_of(plist, struct sta_info, hash_list);

				plist = plist->next;

				if (_rtw_memcmp(psta	->hwaddr, bcast_addr, ETH_ALEN) ||
					_rtw_memcmp(psta->hwaddr, myid(&adapter->eeprompriv), ETH_ALEN))
					continue;

				if (psta->state & WIFI_ASOC_STATE)
				{

					if (psta->rssi_stat.UndecoratedSmoothedPWDB < tmpEntryMinPWDB)
						tmpEntryMinPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					if (psta->rssi_stat.UndecoratedSmoothedPWDB > tmpEntryMaxPWDB)
						tmpEntryMaxPWDB = psta->rssi_stat.UndecoratedSmoothedPWDB;

					PWDB_rssi[sta_cnt++] = (psta->mac_id | (psta->rssi_stat.UndecoratedSmoothedPWDB<<16) | ((adapter->stapriv.asoc_sta_count+1) << 8));
				}
			}
		}

		spin_unlock_bh(&pstapriv->sta_hash_lock);

		if (pHalData->fw_ractrl == true)
		{
			/*  Report every sta's RSSI to FW */
			for (i=0; i< sta_cnt; i++)
			{
				FillH2CCmd92D(adapter, H2C_RSSI_REPORT, 3, (u8 *)(&PWDB_rssi[i]));
			}
		}
	}

	if (tmpEntryMaxPWDB != 0)	/*  If associated entry is found */
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = tmpEntryMaxPWDB;
	}
	else
	{
		pdmpriv->EntryMaxUndecoratedSmoothedPWDB = 0;
	}

	if (tmpEntryMinPWDB != 0xff) /*  If associated entry is found */
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = tmpEntryMinPWDB;
	}
	else
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}

	if (check_fwstate(&adapter->mlmepriv, WIFI_STATION_STATE) == true
		&& check_fwstate(&adapter->mlmepriv, _FW_LINKED) == true)
	{
		/*  Indicate Rx signal strength to FW. */
		if (pHalData->fw_ractrl == true)
		{
			u32	temp = 0;

			temp = pdmpriv->UndecoratedSmoothedPWDB;
			temp = temp << 16;

			/*  fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1 */

			/*  Commented by Kurt 20120705 */
			/*  We could set max mac_id to FW without checking how many STAs we allocated */
			/*  It's recommanded by SD3 */
			/*  Original: temp = temp | ((adapter->stapriv.asoc_sta_count+1) << 8); */
			temp = temp | ((32) << 8);

			FillH2CCmd92D(adapter, H2C_RSSI_REPORT, 3, (u8 *)(&temp));
		}
		else
		{
			rtw_write8(adapter, 0x4fe, (u8)pdmpriv->UndecoratedSmoothedPWDB);
		}
	}
}

static void
DM_InitEdcaTurbo(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	pHalData->bCurrentTurboEDCA = false;
	adapter->recvpriv.bIsAnyNonBEPkts = false;
}	/*  DM_InitEdcaTurbo */

static void dm_CheckEdcaTurbo(struct rtw_adapter *adapter)
{
	u32	trafficIndex;
	u32	edca_param;
	u64	cur_tx_bytes = 0;
	u64	cur_rx_bytes = 0;
	u32	EDCA_BE[2] = {0x5ea42b, 0x5ea42b};
	u8	bbtchange = false;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv *pdmpriv = &pHalData->dmpriv;
	struct xmit_priv *pxmitpriv = &(adapter->xmitpriv);
	struct recv_priv *precvpriv = &(adapter->recvpriv);
	struct registry_priv *pregpriv = &adapter->registrypriv;
	struct mlme_ext_priv *pmlmeext = &(adapter->mlmeextpriv);
	struct mlme_ext_info *pmlmeinfo = &(pmlmeext->mlmext_info);

	if (IS_92D_SINGLEPHY(pHalData->VersionID))
	{
		EDCA_BE[UP_LINK] = 0x5ea42b;
		EDCA_BE[DOWN_LINK] = 0x5ea42b;
	}
	else
	{
		EDCA_BE[UP_LINK] = 0x6ea42b;
		EDCA_BE[DOWN_LINK] = 0x6ea42b;
	}

	if ((pregpriv->wifi_spec == 1) || (pmlmeext->cur_wireless_mode == WIRELESS_11B))
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

	if (pmlmeinfo->assoc_AP_vendor >= maxAP)
	{
		goto dm_CheckEdcaTurbo_EXIT;
	}

	/*  Check if the status needs to be changed. */
	if ((bbtchange) || (!precvpriv->bIsAnyNonBEPkts))
	{
		cur_tx_bytes = pxmitpriv->tx_bytes - pxmitpriv->last_tx_bytes;
		cur_rx_bytes = precvpriv->rx_bytes - precvpriv->last_rx_bytes;

		/* traffic, TX or RX */
		if ((pmlmeinfo->assoc_AP_vendor == ralinkAP)||(pmlmeinfo->assoc_AP_vendor == atherosAP))
		{
			if (cur_tx_bytes > (cur_rx_bytes << 2))
			{ /*  Uplink TP is present. */
				trafficIndex = UP_LINK;
			}
			else
			{ /*  Balance TP is present. */
				trafficIndex = DOWN_LINK;
			}
		}
		else
		{
			if (cur_rx_bytes > (cur_tx_bytes << 2))
			{ /*  Downlink TP is present. */
				trafficIndex = DOWN_LINK;
			}
			else
			{ /*  Balance TP is present. */
				trafficIndex = UP_LINK;
			}
		}

		if ((pdmpriv->prv_traffic_idx != trafficIndex) || (!pHalData->bCurrentTurboEDCA))
		{
			{
				if ((pmlmeinfo->assoc_AP_vendor == ciscoAP) && (pmlmeext->cur_wireless_mode & (WIRELESS_11_24N)))
				{
					edca_param = EDCAParam[pmlmeinfo->assoc_AP_vendor][trafficIndex];
				}
				else if ((pmlmeinfo->assoc_AP_vendor == airgocapAP) &&
					((pmlmeext->cur_wireless_mode == WIRELESS_11G) ||(pmlmeext->cur_wireless_mode == WIRELESS_11BG)))
				{
					if (trafficIndex == DOWN_LINK)
						edca_param = 0xa630;
					else
						edca_param = EDCA_BE[trafficIndex];
				}
				else if ((pmlmeinfo->assoc_AP_vendor == atherosAP) &&
					(pmlmeext->cur_wireless_mode&WIRELESS_11_5N))
				{
					if (trafficIndex == DOWN_LINK)
						edca_param = 0xa42b;
					else
						edca_param = EDCA_BE[trafficIndex];
				}
				else
				{
					edca_param = EDCA_BE[trafficIndex];
				}
			}

			rtw_write32(adapter, REG_EDCA_BE_PARAM, edca_param);

			pdmpriv->prv_traffic_idx = trafficIndex;
		}

		pHalData->bCurrentTurboEDCA = true;
	}
	else
	{
		/*  */
		/*  Turn Off EDCA turbo here. */
		/*  Restore original EDCA according to the declaration of AP. */
		/*  */
		 if (pHalData->bCurrentTurboEDCA)
		{
			rtw_write32(adapter, REG_EDCA_BE_PARAM, pHalData->AcParam_BE);
			pHalData->bCurrentTurboEDCA = false;
		}
	}

dm_CheckEdcaTurbo_EXIT:
	/*  Set variables for next time. */
	precvpriv->bIsAnyNonBEPkts = false;
	pxmitpriv->last_tx_bytes = pxmitpriv->tx_bytes;
	precvpriv->last_rx_bytes = precvpriv->rx_bytes;
}	/*  dm_CheckEdcaTurbo */

static void dm_InitDynamicBBPowerSaving(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct PS_T *dm_pstable = &pdmpriv->DM_PSTable;

	dm_pstable->preccastate = CCA_MAX;
	dm_pstable->curccastate = CCA_MAX;
	dm_pstable->prerfstate = RF_MAX;
	dm_pstable->currfstate = RF_MAX;
}

static void
dm_DynamicBBPowerSaving(
struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);

	/* 1 Power Saving for 92C */
	if (IS_92D_SINGLEPHY(pHalData->VersionID))
	{
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (!pHalData->bSlaveOfDMSP)
#endif
			dm_1R_CCA(adapter);
	}
}

static	void
dm_RXGainTrackingCallback_ThermalMeter_92D(
	struct rtw_adapter *	adapter)
{
	u8			index_mapping[Rx_index_mapping_NUM] = {
						0x0f, 0x0f, 0x0f, 0x0f, 0x0b,
						0x0a, 0x09, 0x08, 0x07, 0x06,
						0x05, 0x04, 0x04, 0x03, 0x02
					};

	u8			eRFPath;
	u32			u4tmp;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	u4tmp = (index_mapping[(pHalData->EEPROMThermalMeter - pdmpriv->ThermalValue_RxGain)]) << 12;

	for (eRFPath = RF_PATH_A; eRFPath <pHalData->NumTotalRFPath; eRFPath++) {
		PHY_SetRFReg(adapter, (enum RF_RADIO_PATH_E)eRFPath, RF_RXRF_A3, bRFRegOffsetMask, (pdmpriv->RegRF3C[eRFPath]&(~(0xF000)))|u4tmp);
	}
};

/* 091212 chiyokolin */
static	void
dm_TXPowerTrackingCallback_ThermalMeter_92D(
            struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8		ThermalValue = 0, delta, delta_LCK, delta_IQK, delta_RxGain, index, offset;
	u8		ThermalValue_AVG_count = 0;
	u32		ThermalValue_AVG = 0;
	s32		ele_A = 0, ele_D, TempCCk, X, value32;
	s32		Y, ele_C;
	s8		OFDM_index[2], CCK_index=0, OFDM_index_old[2], CCK_index_old=0;
	s32		i = 0;
	bool		is2T = IS_92D_SINGLEPHY(pHalData->VersionID);
	bool		bInteralPA = false;

	u8		OFDM_min_index = 6, OFDM_min_index_internalPA = 12, rf; /* OFDM BB Swing should be less than +3.0dB, which is required by Arthur */
	u8		Indexforchannel = rtl8192d_GetRightChnlPlaceforIQK(pHalData->CurrentChannel);
	u8 index_mapping[5][index_mapping_NUM] = {
		{0, 1, 3, 6, 8, 9,			/* 5G, path A/MAC 0, decrease power */
		11, 13, 14, 16, 17, 18, 18},
		{0, 2, 4, 5, 7, 10,			/* 5G, path A/MAC 0, increase power */
		12, 14, 16, 18, 18, 18, 18},
		{0, 2, 3, 6, 8, 9,			/* 5G, path B/MAC 1, decrease power */
		11, 13, 14, 16, 17, 18, 18},
		{0, 2, 4, 5, 7, 10,			/* 5G, path B/MAC 1, increase power */
		13, 16, 16, 18, 18, 18, 18},
		{0, 1, 2, 3, 4, 5,			/* 2.4G, for decreas power */
		6, 7, 7, 8, 9, 10, 10},
	};

	u8 index_mapping_internalPA[8][index_mapping_NUM] = {
		{0, 1, 2, 4, 6, 7,			/* 5G, path A/MAC 0, ch36-64, decrease power */
		9, 11, 12, 14, 15, 16, 16},
		{0, 2, 4, 5, 7, 10,			/* 5G, path A/MAC 0, ch36-64, increase power */
		12, 14, 16, 18, 18, 18, 18},
		{0, 1, 2, 3, 5, 6,			/* 5G, path A/MAC 0, ch100-165, decrease power */
		8, 10, 11, 13, 14, 15, 15},
		{0, 2, 4, 5, 7, 10,			/* 5G, path A/MAC 0, ch100-165, increase power */
		12, 14, 16, 18, 18, 18, 18},
		{0, 1, 2, 4, 6, 7,			/* 5G, path B/MAC 1, ch36-64, decrease power */
		9, 11, 12, 14, 15, 16, 16},
		{0, 2, 4, 5, 7, 10,			/* 5G, path B/MAC 1, ch36-64, increase power */
		13, 16, 16, 18, 18, 18, 18},
		{0, 1, 2, 3, 5, 6,			/* 5G, path B/MAC 1, ch100-165, decrease power */
		8, 9, 10, 12, 13, 14, 14},
		{0, 2, 4, 5, 7, 10,			/* 5G, path B/MAC 1, ch100-165, increase power */
		13, 16, 16, 18, 18, 18, 18},
	};

	pdmpriv->TXPowerTrackingCallbackCnt++;	/* cosa add for debug */
	pdmpriv->bTXPowerTrackingInit = true;

	if (pHalData->CurrentChannel == 14 && !pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = true;
	else if (pHalData->CurrentChannel != 14 && pdmpriv->bCCKinCH14)
		pdmpriv->bCCKinCH14 = false;

	ThermalValue = (u8)PHY_QueryRFReg(adapter, RF_PATH_A, RF_T_METER, 0xf800);	/* 0x42: RF Reg[15:11] 92D */

	rtl8192d_PHY_APCalibrate(adapter, (ThermalValue - pHalData->EEPROMThermalMeter));/* notes:EEPROMThermalMeter is a fixed value from efuse/eeprom */

	if (is2T)
		rf = 2;
	else
		rf = 1;

	if (ThermalValue)
	{
		{
			/* Query OFDM path A default setting */
			ele_D = PHY_QueryBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord)&bMaskOFDM_D;
			for (i=0; i<OFDM_TABLE_SIZE_92D; i++)	/* find the index */
			{
				if (ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
				{
					OFDM_index_old[0] = (u8)i;
					break;
				}
			}

			/* Query OFDM path B default setting */
			if (is2T)
			{
				ele_D = PHY_QueryBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord)&bMaskOFDM_D;
				for (i=0; i<OFDM_TABLE_SIZE_92D; i++)	/* find the index */
				{
					if (ele_D == (OFDMSwingTable[i]&bMaskOFDM_D))
					{
						OFDM_index_old[1] = (u8)i;
						break;
					}
				}
			}

			if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
			{
				/* Query CCK default setting From 0xa24 */
				TempCCk = pdmpriv->RegA24;

				for (i=0 ; i<CCK_TABLE_SIZE ; i++)
				{
					if (pdmpriv->bCCKinCH14)
					{
						if (_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch14[i][2], 4)==true)
						{
							CCK_index_old =(u8)i;
							break;
						}
					}
					else
					{
						if (_rtw_memcmp((void*)&TempCCk, (void*)&CCKSwingTable_Ch1_Ch13[i][2], 4)==true)
						{
							CCK_index_old =(u8)i;
							break;
						}
					}
				}
			}
			else
			{
				TempCCk = 0x090e1317;
				CCK_index_old = 12;
			}

			if (!pdmpriv->ThermalValue)
			{
				pdmpriv->ThermalValue = pHalData->EEPROMThermalMeter;
				pdmpriv->ThermalValue_LCK = ThermalValue;
				pdmpriv->ThermalValue_IQK = ThermalValue;
				pdmpriv->ThermalValue_RxGain = pHalData->EEPROMThermalMeter;

				for (i = 0; i < rf; i++)
					pdmpriv->OFDM_index[i] = OFDM_index_old[i];
				pdmpriv->CCK_index = CCK_index_old;
			}

			if (pdmpriv->bReloadtxpowerindex)
			{
				DBG_8192D("reload ofdm index for band switch\n");
			}

			/* calculate average thermal meter */
			{
				pdmpriv->ThermalValue_AVG[pdmpriv->ThermalValue_AVG_index] = ThermalValue;
				pdmpriv->ThermalValue_AVG_index++;
				if (pdmpriv->ThermalValue_AVG_index == AVG_THERMAL_NUM)
					pdmpriv->ThermalValue_AVG_index = 0;

				for (i = 0; i < AVG_THERMAL_NUM; i++)
				{
					if (pdmpriv->ThermalValue_AVG[i])
					{
						ThermalValue_AVG += pdmpriv->ThermalValue_AVG[i];
						ThermalValue_AVG_count++;
					}
				}

				if (ThermalValue_AVG_count)
					ThermalValue = (u8)(ThermalValue_AVG / ThermalValue_AVG_count);
			}
		}

		if (pdmpriv->bReloadtxpowerindex)
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);
			pdmpriv->bReloadtxpowerindex = false;
			pdmpriv->bDoneTxpower = false;
		}
		else if (pdmpriv->bDoneTxpower)
		{
			delta = (ThermalValue > pdmpriv->ThermalValue)?(ThermalValue - pdmpriv->ThermalValue):(pdmpriv->ThermalValue - ThermalValue);
		}
		else
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);
		}
		delta_LCK = (ThermalValue > pdmpriv->ThermalValue_LCK)?(ThermalValue - pdmpriv->ThermalValue_LCK):(pdmpriv->ThermalValue_LCK - ThermalValue);
		delta_IQK = (ThermalValue > pdmpriv->ThermalValue_IQK)?(ThermalValue - pdmpriv->ThermalValue_IQK):(pdmpriv->ThermalValue_IQK - ThermalValue);
		delta_RxGain = (ThermalValue > pdmpriv->ThermalValue_RxGain)?(ThermalValue - pdmpriv->ThermalValue_RxGain):(pdmpriv->ThermalValue_RxGain - ThermalValue);

		if ((delta_LCK > pdmpriv->Delta_LCK) && (pdmpriv->Delta_LCK != 0))
		{
			pdmpriv->ThermalValue_LCK = ThermalValue;
			rtl8192d_PHY_LCCalibrate(adapter);
		}

		if (delta > 0 && pdmpriv->TxPowerTrackControl)
		{
			delta = ThermalValue > pHalData->EEPROMThermalMeter?(ThermalValue - pHalData->EEPROMThermalMeter):(pHalData->EEPROMThermalMeter - ThermalValue);

			/* calculate new OFDM / CCK offset */
			{
				if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					offset = 4;

					if (delta > index_mapping_NUM-1)
						index = index_mapping[offset][index_mapping_NUM-1];
					else
						index = index_mapping[offset][delta];

					if (ThermalValue > pHalData->EEPROMThermalMeter)
					{
						for (i = 0; i < rf; i++)
							OFDM_index[i] = pdmpriv->OFDM_index[i] -delta;
						CCK_index = pdmpriv->CCK_index -delta;
					}
					else
					{
						for (i = 0; i < rf; i++)
							OFDM_index[i] = pdmpriv->OFDM_index[i] + index;
						CCK_index = pdmpriv->CCK_index + index;
					}
				}
				else if (pHalData->CurrentBandType92D == BAND_ON_5G)
				{
					for (i = 0; i < rf; i++)
					{
						if (pHalData->MacPhyMode92D == DUALMAC_DUALPHY &&
							pHalData->interfaceIndex == 1)		/* MAC 1 5G */
							bInteralPA = pHalData->InternalPA5G[1];
						else
							bInteralPA = pHalData->InternalPA5G[i];

						if (bInteralPA)
						{
							if (pHalData->interfaceIndex == 1 || i == rf)
								offset = 4;
							else
								offset = 0;

							if (pHalData->CurrentChannel >= 100 && pHalData->CurrentChannel <= 165)
								offset += 2;
						}
						else
						{
							if (pHalData->interfaceIndex == 1 || i == rf)
								offset = 2;
							else
								offset = 0;
						}

						if (ThermalValue > pHalData->EEPROMThermalMeter)	/* set larger Tx power */
							offset++;

						if (bInteralPA)
						{
							if (delta > index_mapping_NUM-1)
								index = index_mapping_internalPA[offset][index_mapping_NUM-1];
							else
								index = index_mapping_internalPA[offset][delta];
						}
						else
						{
							if (delta > index_mapping_NUM-1)
								index = index_mapping[offset][index_mapping_NUM-1];
							else
								index = index_mapping[offset][delta];
						}

						if (ThermalValue > pHalData->EEPROMThermalMeter)	/* set larger Tx power */
						{
							if (bInteralPA && ThermalValue > 0x12)
							{
								OFDM_index[i] = pdmpriv->OFDM_index[i] -((delta/2)*3+(delta%2));
							}
							else
							{
								OFDM_index[i] = pdmpriv->OFDM_index[i] -index;
							}
						}
						else
						{
							OFDM_index[i] = pdmpriv->OFDM_index[i] + index;
						}
					}
				}

				/*if (is2T)
				{
					DBG_8192D("temp OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n",
						pdmpriv->OFDM_index[0], pdmpriv->OFDM_index[1], pdmpriv->CCK_index);
				}
				else
				{
					DBG_8192D("temp OFDM_A_index=0x%x, CCK_index=0x%x\n",
						pdmpriv->OFDM_index[0], pdmpriv->CCK_index);
				}*/

				for (i = 0; i < rf; i++)
				{
					if (OFDM_index[i] > OFDM_TABLE_SIZE_92D-1)
					{
						OFDM_index[i] = OFDM_TABLE_SIZE_92D-1;
					}
					else if (bInteralPA || pHalData->CurrentBandType92D == BAND_ON_2_4G)
					{
						if (OFDM_index[i] < OFDM_min_index_internalPA)
							OFDM_index[i] = OFDM_min_index_internalPA;
					}
					else if (OFDM_index[i] < OFDM_min_index)
					{
						OFDM_index[i] = OFDM_min_index;
					}
				}

				if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					if (CCK_index > CCK_TABLE_SIZE-1)
						CCK_index = CCK_TABLE_SIZE-1;
					else if (CCK_index < 0)
						CCK_index = 0;
				}

				/*if (is2T)
				{
					DBG_8192D("new OFDM_A_index=0x%x, OFDM_B_index=0x%x, CCK_index=0x%x\n",
						OFDM_index[0], OFDM_index[1], CCK_index);
				}
				else
				{
					DBG_8192D("new OFDM_A_index=0x%x, CCK_index=0x%x\n",
						OFDM_index[0], CCK_index);
				}*/
			}

			/* Config by SwingTable */
			if (pdmpriv->TxPowerTrackControl && !pHalData->bNOPG)
			{
				pdmpriv->bDoneTxpower = true;

				/* Adujst OFDM Ant_A according to IQK result */
				ele_D = (OFDMSwingTable[(u8)OFDM_index[0]] & 0xFFC00000)>>22;
				X = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][0];
				Y = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][1];

				if (X != 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					if ((X & 0x00000200) != 0)
						X = X | 0xFFFFFC00;
					ele_A = ((X * ele_D)>>8)&0x000003FF;

					/* new element C = element D x Y */
					if ((Y & 0x00000200) != 0)
						Y = Y | 0xFFFFFC00;
					ele_C = ((Y * ele_D)>>8)&0x000003FF;

					/* wirte new elements A, C, D to regC80 and regC94, element B is always 0 */
					value32 = (ele_D<<22)|((ele_C&0x3F)<<16)|ele_A;
					PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, value32);

					value32 = (ele_C&0x000003C0)>>6;
					PHY_SetBBReg(adapter, rOFDM0_XCTxAFE, bMaskH4Bits, value32);

					value32 = ((X * ele_D)>>7)&0x01;
					PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT24, value32);

				}
				else
				{
					PHY_SetBBReg(adapter, rOFDM0_XATxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)OFDM_index[0]]);
					PHY_SetBBReg(adapter, rOFDM0_XCTxAFE, bMaskH4Bits, 0x00);
					PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT24, 0x00);
				}

				if (pHalData->CurrentBandType92D == BAND_ON_2_4G)
				{
					/* Adjust CCK according to IQK result */
					if (!pdmpriv->bCCKinCH14) {
						rtw_write8(adapter, 0xa22, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][0]);
						rtw_write8(adapter, 0xa23, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][1]);
						rtw_write8(adapter, 0xa24, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][2]);
						rtw_write8(adapter, 0xa25, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][3]);
						rtw_write8(adapter, 0xa26, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][4]);
						rtw_write8(adapter, 0xa27, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][5]);
						rtw_write8(adapter, 0xa28, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][6]);
						rtw_write8(adapter, 0xa29, CCKSwingTable_Ch1_Ch13[(u8)CCK_index][7]);
					}
					else {
						rtw_write8(adapter, 0xa22, CCKSwingTable_Ch14[(u8)CCK_index][0]);
						rtw_write8(adapter, 0xa23, CCKSwingTable_Ch14[(u8)CCK_index][1]);
						rtw_write8(adapter, 0xa24, CCKSwingTable_Ch14[(u8)CCK_index][2]);
						rtw_write8(adapter, 0xa25, CCKSwingTable_Ch14[(u8)CCK_index][3]);
						rtw_write8(adapter, 0xa26, CCKSwingTable_Ch14[(u8)CCK_index][4]);
						rtw_write8(adapter, 0xa27, CCKSwingTable_Ch14[(u8)CCK_index][5]);
						rtw_write8(adapter, 0xa28, CCKSwingTable_Ch14[(u8)CCK_index][6]);
						rtw_write8(adapter, 0xa29, CCKSwingTable_Ch14[(u8)CCK_index][7]);
					}
				}

				if (is2T)
				{
					ele_D = (OFDMSwingTable[(u8)OFDM_index[1]] & 0xFFC00000)>>22;

					/* new element A = element D x X */
					X = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][4];
					Y = pHalData->IQKMatrixRegSetting[Indexforchannel].Value[0][5];

					if (X != 0 && pHalData->CurrentBandType92D == BAND_ON_2_4G)
					{
						if ((X & 0x00000200) != 0)	/* consider minus */
							X = X | 0xFFFFFC00;
						ele_A = ((X * ele_D)>>8)&0x000003FF;

						/* new element C = element D x Y */
						if ((Y & 0x00000200) != 0)
							Y = Y | 0xFFFFFC00;
						ele_C = ((Y * ele_D)>>8)&0x00003FF;

						/* wirte new elements A, C, D to regC88 and regC9C, element B is always 0 */
						value32=(ele_D<<22)|((ele_C&0x3F)<<16) |ele_A;
						PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, value32);

						value32 = (ele_C&0x000003C0)>>6;
						PHY_SetBBReg(adapter, rOFDM0_XDTxAFE, bMaskH4Bits, value32);

						value32 = ((X * ele_D)>>7)&0x01;
						PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT28, value32);

					}
					else
					{
						PHY_SetBBReg(adapter, rOFDM0_XBTxIQImbalance, bMaskDWord, OFDMSwingTable[(u8)OFDM_index[1]]);
						PHY_SetBBReg(adapter, rOFDM0_XDTxAFE, bMaskH4Bits, 0x00);
						PHY_SetBBReg(adapter, rOFDM0_ECCAThreshold, BIT28, 0x00);
					}

				}

			}
		}

		if ((delta_IQK > pdmpriv->Delta_IQK) && (pdmpriv->Delta_IQK != 0))
		{
			rtl8192d_PHY_ResetIQKResult(adapter);
#ifdef CONFIG_CONCURRENT_MODE
			if (rtw_buddy_adapter_up(adapter)) {
				rtl8192d_PHY_ResetIQKResult(adapter->pbuddy_adapter);
			}
#endif
			pdmpriv->ThermalValue_IQK = ThermalValue;
			rtl8192d_PHY_IQCalibrate(adapter);
		}

		if (delta_RxGain > 0 && pHalData->CurrentBandType92D == BAND_ON_5G
			&& ThermalValue <= pHalData->EEPROMThermalMeter && pHalData->bNOPG == false)
		{
			pdmpriv->ThermalValue_RxGain = ThermalValue;
			dm_RXGainTrackingCallback_ThermalMeter_92D(adapter);
		}

		/* update thermal meter value */
		if (pdmpriv->TxPowerTrackControl)
		{
			pdmpriv->ThermalValue = ThermalValue;
		}

	}

	pdmpriv->TXPowercount = 0;
}

static	void
dm_InitializeTXPowerTracking_ThermalMeter(
	struct rtw_adapter *		adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	{
		pdmpriv->bTXPowerTracking = true;
		pdmpriv->TXPowercount = 0;
		pdmpriv->bTXPowerTrackingInit = false;
#if	(MP_DRIVER != 1)		/* for mp driver, turn off txpwrtracking as default */
		pdmpriv->TxPowerTrackControl = true;
#endif
	}
	MSG_8192D("pdmpriv->TxPowerTrackControl = %d\n", pdmpriv->TxPowerTrackControl);
}

static void
DM_InitializeTXPowerTracking(
	struct rtw_adapter *		adapter)
{

	{
		dm_InitializeTXPowerTracking_ThermalMeter(adapter);
	}
}

/*  */
/*	Description: */
/*		- Dispatch TxPower Tracking direct call ONLY for 92s. */
/*		- We shall NOT schedule Workitem within PASSIVE LEVEL, which will cause system resource */
/*		   leakage under some platform. */
/*  */
/*	Assumption: */
/*		PASSIVE_LEVEL when this routine is called. */
/*  */
/*	Added by Roger, 2009.06.18. */
/*  */
static void
DM_TXPowerTracking92CDirectCall(
            struct rtw_adapter *		adapter)
{
	dm_TXPowerTrackingCallback_ThermalMeter_92D(adapter);
}

static void
dm_CheckTXPowerTracking_ThermalMeter(
	struct rtw_adapter *		adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;

	if (!(pdmpriv->DMFlag & DYNAMIC_FUNC_SS))
	{
		return;
	}

	if (!pdmpriv->bTXPowerTracking /*|| (!pHalData->TxPowerTrackControl && pHalData->bAPKdone)*/)
	{
		return;
	}

	if (!pdmpriv->TM_Trigger)		/* at least delay 1 sec */
	{

		PHY_SetRFReg(adapter, RF_PATH_A, RF_T_METER, BIT17 | BIT16, 0x03);
		pdmpriv->TM_Trigger = 1;
		return;
	}
	else
	{
		DM_TXPowerTracking92CDirectCall(adapter); /* Using direct call is instead, added by Roger, 2009.06.18. */
		pdmpriv->TM_Trigger = 0;
	}
}

void
rtl8192d_dm_CheckTXPowerTracking(
	struct rtw_adapter *		adapter)
{

#if DISABLE_BB_RF
	return;
#endif
	dm_CheckTXPowerTracking_ThermalMeter(adapter);
}

/*-----------------------------------------------------------------------------
 * Function:	dm_CheckPbcGPIO()
 *
 * Overview:	Check if PBC button is pressed.
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	03/11/2008	hpfan	Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void	dm_CheckPbcGPIO(struct rtw_adapter * padapter)
{
	u8	tmp1byte;
	u8	bPbcPressed = false;
	int i=0;

	if (!padapter->registrypriv.hw_wps_pbc)
		return;

	do
	{
		i++;
	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte |= (HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as output mode */

	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter,  GPIO_IN, tmp1byte);		/* reset the floating voltage level */

	tmp1byte = rtw_read8(padapter, GPIO_IO_SEL);
	tmp1byte &= ~(HAL_8192C_HW_GPIO_WPS_BIT);
	rtw_write8(padapter, GPIO_IO_SEL, tmp1byte);	/* enable GPIO[2] as input mode */

	tmp1byte =rtw_read8(padapter, GPIO_IN);

	if (tmp1byte == 0xff)
	{
		bPbcPressed = false;
		break ;
	}

	if (tmp1byte&HAL_8192C_HW_GPIO_WPS_BIT)
	{
		bPbcPressed = true;

		if (i<=3)
			rtw_msleep_os(50);
	}
	}while (i<=3 && bPbcPressed == true);

	if (true == bPbcPressed)
	{
		/*  Here we only set bPbcPressed to true */
		/*  After trigger PBC, the variable will be set to false */
		DBG_8192D("CheckPbcGPIO - PBC is pressed, try_cnt=%d\n", i-1);

		if (padapter->pid[0] == 0)
		{	/*	0 is the default value and it means the application monitors the HW PBC doesn't privde its pid to driver. */
			return;
		}

		rtw_signal_process(padapter->pid[0], SIGUSR1);
	}
}

static void dm_InitRateAdaptiveMask(struct rtw_adapter *	adapter)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct rate_adaptive *ra = (struct rate_adaptive *)&pdmpriv->RateAdaptive;

	ra->RATRState = DM_RATR_STA_INIT;
	ra->PreRATRState = DM_RATR_STA_INIT;

	if (pdmpriv->DM_Type == DM_Type_ByDriver)
		pdmpriv->bUseRAMask = true;
	else
		pdmpriv->bUseRAMask = false;
}

/*  */
/*  Initialize GPIO setting registers */
/*  */
static void
dm_InitGPIOSetting(
	struct rtw_adapter *	adapter
	)
{
	u8	tmp1byte;

	tmp1byte = rtw_read8(adapter, REG_GPIO_MUXCFG);
	tmp1byte &= (GPIOSEL_GPIO | ~GPIOSEL_ENBT);
	rtw_write8(adapter, REG_GPIO_MUXCFG, tmp1byte);
}

/*  */
/*  functions */
/*  */
void rtl8192d_init_dm_priv(struct rtw_adapter * adapter)
{
}

void rtl8192d_deinit_dm_priv(struct rtw_adapter * adapter)
{
}

void
rtl8192d_InitHalDm(
	struct rtw_adapter *	adapter
	)
{
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	u8	i;

	pdmpriv->DM_Type = DM_Type_ByDriver;
	pdmpriv->DMFlag = DYNAMIC_FUNC_DISABLE;
	pdmpriv->UndecoratedSmoothedPWDB = 0;

	/* 1 DIG INIT */
	pdmpriv->bDMInitialGainEnable = true;
	pdmpriv->DMFlag |= DYNAMIC_FUNC_DIG;
	dm_DIGInit(adapter);

	/* 2 DynamicTxPower INIT */
	pdmpriv->DMFlag |= DYNAMIC_FUNC_HP;
	dm_InitDynamicTxPower(adapter);

	/* 3 */
	DM_InitEdcaTurbo(adapter);/* moved to  linked_status_chk() */

	/* 4 RateAdaptive INIT */
	dm_InitRateAdaptiveMask(adapter);

	/* 5 Tx Power Tracking Init. */
	pdmpriv->DMFlag |= DYNAMIC_FUNC_SS;
	DM_InitializeTXPowerTracking(adapter);

	dm_InitDynamicBBPowerSaving(adapter);

	dm_InitGPIOSetting(adapter);

	pdmpriv->DMFlag_tmp = pdmpriv->DMFlag;

	/*  Save REG_INIDATA_RATE_SEL value for TXDESC. */
	for (i = 0 ; i<32 ; i++)
	{
		pdmpriv->INIDATA_RATE[i] = rtw_read8(adapter, REG_INIDATA_RATE_SEL+i) & 0x3f;
	}
}

#ifdef CONFIG_CONCURRENT_MODE
static void FindMinimumRSSI(struct rtw_adapter * adapter)
{
	struct hal_data_8192du *pbuddy_HalData;
	struct dm_priv *pbuddy_dmpriv;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct rtw_adapter * pbuddy_adapter = adapter->pbuddy_adapter;
	struct mlme_priv *pmlmepriv = &adapter->mlmepriv;

	if (!rtw_buddy_adapter_up(adapter))
		return;

	pbuddy_HalData = GET_HAL_DATA(pbuddy_adapter);
	pbuddy_dmpriv = &pbuddy_HalData->dmpriv;

	/* get min. [PWDB] when both interfaces are connected */
	if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true
		&& adapter->stapriv.asoc_sta_count > 2
		&& check_buddy_fwstate(adapter, _FW_LINKED)) ||
		(check_buddy_fwstate(adapter, WIFI_AP_STATE) == true
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2
		&& check_fwstate(pmlmepriv, _FW_LINKED) == true) ||
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)
		&& check_buddy_fwstate(adapter,WIFI_STATION_STATE)
		&& check_buddy_fwstate(adapter,_FW_LINKED)))
	{
		/* select smaller PWDB */
		if (pdmpriv->UndecoratedSmoothedPWDB > pbuddy_dmpriv->UndecoratedSmoothedPWDB)
			pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}/* secondary interface is connected */
	else if ((check_buddy_fwstate(adapter, WIFI_AP_STATE) == true
		&& pbuddy_adapter->stapriv.asoc_sta_count > 2) ||
		(check_buddy_fwstate(adapter,WIFI_STATION_STATE)
		&& check_buddy_fwstate(adapter,_FW_LINKED)))
	{
		/* select buddy PWDB */
		pdmpriv->UndecoratedSmoothedPWDB = pbuddy_dmpriv->UndecoratedSmoothedPWDB;
	}
	/* primary is connected */
	else if ((check_fwstate(pmlmepriv, WIFI_AP_STATE) == true
		&& adapter->stapriv.asoc_sta_count > 2) ||
		(check_fwstate(pmlmepriv, WIFI_STATION_STATE)
		&& check_fwstate(pmlmepriv, _FW_LINKED)))
	{
		/* set buddy PWDB to 0 */
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}
	/* both interfaces are not connected */
	else
	{
		pdmpriv->UndecoratedSmoothedPWDB = 0;
		pbuddy_dmpriv->UndecoratedSmoothedPWDB = 0;
	}

	/* primary interface is ap mode */
	if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true && adapter->stapriv.asoc_sta_count > 2)
	{
		pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}/* secondary interface is ap mode */
	else if (check_buddy_fwstate(adapter, WIFI_AP_STATE) == true && pbuddy_adapter->stapriv.asoc_sta_count > 2)
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB;
	}
	else /* both interfaces are not ap mode */
	{
		pdmpriv->EntryMinUndecoratedSmoothedPWDB = pbuddy_dmpriv->EntryMinUndecoratedSmoothedPWDB = 0;
	}
}
#endif /* CONFIG_CONCURRENT_MODE */

void
rtl8192d_HalDmWatchDog(
	struct rtw_adapter *	adapter
	)
{
	bool		bFwCurrentInPSMode = false;
	bool		bFwPSAwake = true;
	u8 hw_init_completed = false;
	struct hal_data_8192du *pHalData = GET_HAL_DATA(adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
#ifdef CONFIG_CONCURRENT_MODE
	struct rtw_adapter * pbuddy_adapter = adapter->pbuddy_adapter;
#endif /* CONFIG_CONCURRENT_MODE */

#ifdef CONFIG_DUALMAC_CONCURRENT
	if ((pHalData->bInModeSwitchProcess))
	{
		DBG_8192D("HalDmWatchDog(): During dual mac mode switch or slave mac\n");
		return;
	}
#endif

	#if defined(CONFIG_CONCURRENT_MODE)
	if (adapter->isprimary == false && pbuddy_adapter) {
		hw_init_completed = pbuddy_adapter->hw_init_completed;
	} else
	#endif
	{
		hw_init_completed = adapter->hw_init_completed;
	}

	if (hw_init_completed == false)
		goto skip_dm;

#ifdef CONFIG_LPS
	#if defined(CONFIG_CONCURRENT_MODE)
	if (adapter->iface_type != IFACE_PORT0 && pbuddy_adapter) {
		bFwCurrentInPSMode = pbuddy_adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(pbuddy_adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	} else
	#endif
	{
		bFwCurrentInPSMode = adapter->pwrctrlpriv.bFwCurrentInPSMode;
		rtw_hal_get_hwreg(adapter, HW_VAR_FWLPS_RF_ON, (u8 *)(&bFwPSAwake));
	}
#endif

	/*  Stop dynamic mechanism when: */
	/*  1. RF is OFF. (No need to do DM.) */
	/*  2. Fw is under power saving mode for FwLPS. (Prevent from SW/FW I/O racing.) */
	/*  3. IPS workitem is scheduled. (Prevent from IPS sequence to be swapped with DM. */
	/*      Sometimes DM execution time is longer than 100ms such that the assertion */
	/*      in MgntActSet_RF_State() called by InactivePsWorkItem will be triggered by */
	/*      wating to long for RFChangeInProgress.) */
	/*  4. RFChangeInProgress is TRUE. (Prevent from broken by IPS/HW/SW Rf off.) */
	/*  Noted by tynli. 2010.06.01. */
	if ((hw_init_completed == true)
		&& ((!bFwCurrentInPSMode) && bFwPSAwake))
	{
		/*  */
		/*  For PWDB monitor and record some value for later use. */
		/*  */
		PWDB_Monitor(adapter);
#ifdef CONFIG_CONCURRENT_MODE
		if (adapter->adapter_type > PRIMARY_ADAPTER)
			goto _record_initrate;
		FindMinimumRSSI(adapter);
#endif /* CONFIG_CONCURRENT_MODE */
		/*  */
		/*  Dynamic Initial Gain mechanism. */
		/*  */
/* sherry delete flag 20110517 */
		ACQUIRE_GLOBAL_MUTEX(GlobalMutexForGlobaladapterList);
		if (pHalData->bSlaveOfDMSP)
		{
			odm_FalseAlarmCounterStatistics_ForSlaveOfDMSP(adapter);
		}
		else
		{
			odm_FalseAlarmCounterStatistics(adapter);
		}
		RELEASE_GLOBAL_MUTEX(GlobalMutexForGlobaladapterList);
#ifndef CONFIG_CONCURRENT_MODE
		odm_FindMinimumRSSI_92D(adapter);
#endif /* CONFIG_CONCURRENT_MODE */
		odm_DIG(adapter);
		if (pHalData->MacPhyMode92D == DUALMAC_SINGLEPHY)
		{
			if (pHalData->CurrentBandType92D != BAND_ON_5G)
				dm_CCK_PacketDetectionThresh_DMSP(adapter);
		}
		else
		{
			if (pHalData->CurrentBandType92D != BAND_ON_5G)
				dm_CCK_PacketDetectionThresh(adapter);
		}

		/*  */
		/*  Dynamic Tx Power mechanism. */
		/*  */
		odm_DynamicTxPower_92D(adapter);

		/*  */
		/*  Tx Power Tracking. */
		/*  */
		/* TX power tracking will make 92de DMDP MAC0's throughput bad. */
#ifdef CONFIG_DUALMAC_CONCURRENT
		if (!pHalData->bSlaveOfDMSP || adapter->DualMacConcurrent == false)
#endif
			rtl8192d_dm_CheckTXPowerTracking(adapter);

		/*  EDCA turbo */
		/* update the EDCA paramter according to the Tx/RX mode */
		dm_CheckEdcaTurbo(adapter);

		/*  */
		/* Dynamic BB Power Saving Mechanism */
		/* vivi, 20101014, to pass DTM item: softap_excludeunencrypted_ext.htm */
		/* temporarily disable it advised for performance test by yn,2010-11-03. */
		dm_DynamicBBPowerSaving(adapter);

_record_initrate:

		/*  Read REG_INIDATA_RATE_SEL value for TXDESC. */
		if (check_fwstate(&adapter->mlmepriv, WIFI_STATION_STATE)) {
			pdmpriv->INIDATA_RATE[0] = rtw_read8(adapter, REG_INIDATA_RATE_SEL) & 0x3f;

		} else {
			u8	i;
			for (i=1 ; i < (adapter->stapriv.asoc_sta_count + 1); i++)
				pdmpriv->INIDATA_RATE[i] = rtw_read8(adapter, (REG_INIDATA_RATE_SEL+i)) & 0x3f;
		}
	}

skip_dm:

	/*  Check GPIO to determine current RF on/off and Pbc status. */
	/*  Not enable for 92CU now!!! */
	/*  Check Hardware Radio ON/OFF or not */

	dm_CheckPbcGPIO(adapter);				/*  Add by hpfan 2008-03-11 */

}
