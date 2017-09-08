//============================================================
// Description:
//
// This file is for 8812/8821/8811 TXBF mechanism
//
//============================================================
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
VOID
HalTxbf8812A_setNDPArate(
	IN PVOID			pDM_VOID,
	IN u1Byte	BW,
	IN u1Byte	Rate
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	ODM_Write1Byte(pDM_Odm, REG_NDPA_OPT_CTRL_8812A,  (Rate << 2 | BW));	

}

VOID
halTxbfJaguar_RfMode(
	IN PVOID			pDM_VOID,
	IN PRT_BEAMFORMING_INFO	pBeamInfo
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	if (pDM_Odm->RFType == ODM_1T1R)
		return;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] set TxIQGen\n", __func__));

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000, 0x1);	/*RF Mode table write enable*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000, 0x1);	/*RF Mode table write enable*/

	if (pBeamInfo->beamformee_su_cnt > 0) {
		// Paath_A
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableAddr, 0x78000, 0x3);		/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff, 0x3F7FF);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0xE26BF);	/*Enable TXIQGEN in RX mode*/
		// Path_B
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableAddr, 0x78000, 0x3);		/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff, 0x3F7FF);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff, 0xE26BF);	/*Enable TXIQGEN in RX mode*/
	} else {
		// Paath_A
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableAddr, 0x78000, 0x3);		/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff, 0x3F7FF);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0xC26BF);	/*Disable TXIQGEN in RX mode*/
		// Path_B
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableAddr, 0x78000, 0x3);		/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff, 0x3F7FF);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff, 0xC26BF);	/*Disable TXIQGEN in RX mode*/
	}

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000, 0x0);	/*RF Mode table write disable*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000, 0x0);	/*RF Mode table write disable*/

	if (pBeamInfo->beamformee_su_cnt > 0)
		ODM_SetBBReg(pDM_Odm, rTxPath_Jaguar, bMaskByte1, 0x33);
	else
		ODM_SetBBReg(pDM_Odm, rTxPath_Jaguar, bMaskByte1, 0x11);
}


VOID
halTxbfJaguar_DownloadNDPA(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			u1bTmp = 0, tmpReg422 = 0, Head_Page;
	u1Byte			BcnValidReg = 0, count = 0, DLBcnCount = 0;
	BOOLEAN			bSendBeacon = FALSE;
	u1Byte			TxPageBndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8812;	/*default reseved 1 page for the IC type which is undefined.*/
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry + Idx;
	PADAPTER		Adapter = pDM_Odm->Adapter;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*pDM_Odm->pbFwDwRsvdPageInProgress = TRUE;
#endif
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (Idx == 0)
		Head_Page = 0xFE;
	else
		Head_Page = 0xFE;

	Adapter->HalFunc.GetHalDefVarHandler(Adapter, HAL_DEF_TX_PAGE_BOUNDARY, (pu1Byte)&TxPageBndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1bTmp = ODM_Read1Byte(pDM_Odm, REG_CR_8812A + 1);
	ODM_Write1Byte(pDM_Odm,  REG_CR_8812A + 1, (u1bTmp | BIT0));


	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmpReg422 = ODM_Read1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8812A + 2);
	ODM_Write1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8812A + 2,  tmpReg422 & (~BIT6));

	if (tmpReg422 & BIT6) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("SetBeamformDownloadNDPA_8812(): There is an Adapter is sending beacon.\n"));
		bSendBeacon = TRUE;
	}

	/*TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA*/
	ODM_Write1Byte(pDM_Odm, REG_TDECTRL_8812A + 1, Head_Page);

	do {
		/*Clear beacon valid check bit.*/
		BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_TDECTRL_8812A + 2);
		ODM_Write1Byte(pDM_Odm, REG_TDECTRL_8812A + 2, (BcnValidReg | BIT0));

		/*download NDPA rsvd page.*/
		if (pBeamEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
			Beamforming_SendVHTNDPAPacket(pDM_Odm, pBeamEntry->MacAddr, pBeamEntry->AID, pBeamEntry->SoundBW, BEACON_QUEUE);
		else
			Beamforming_SendHTNDPAPacket(pDM_Odm, pBeamEntry->MacAddr, pBeamEntry->SoundBW, BEACON_QUEUE);

		/*check rsvd page download OK.*/
		BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_TDECTRL_8812A + 2);
		count = 0;
		while (!(BcnValidReg & BIT0) && count < 20) {
			count++;
			ODM_delay_ms(10);
			BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_TDECTRL_8812A + 2);
		}
		DLBcnCount++;
	} while (!(BcnValidReg & BIT0) && DLBcnCount < 5);

	if (!(BcnValidReg & BIT0))
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s Download RSVD page failed!\n", __func__));

	/*TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA*/
	ODM_Write1Byte(pDM_Odm, REG_TDECTRL_8812A + 1, TxPageBndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause*/
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (bSendBeacon)
		ODM_Write1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8812A + 2, tmpReg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1bTmp = ODM_Read1Byte(pDM_Odm, REG_CR_8812A + 1);
	ODM_Write1Byte(pDM_Odm, REG_CR_8812A + 1, (u1bTmp & (~BIT0)));

	pBeamEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSED;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*pDM_Odm->pbFwDwRsvdPageInProgress = FALSE;
#endif
}


VOID
halTxbfJaguar_FwTxBFCmd(
	IN PVOID			pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	Idx, Period0 = 0, Period1 = 0;
	u1Byte	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u1Byte	u1TxBFParm[3] = {0};
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;

	for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
		/*Modified by David*/
		if (pBeamInfo->BeamformeeEntry[Idx].bUsed && pBeamInfo->BeamformeeEntry[Idx].BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (Idx == 0) {
				if (pBeamInfo->BeamformeeEntry[Idx].bSound)
					PageNum0 = 0xFE;
				else
					PageNum0 = 0xFF; /*stop sounding*/
				Period0 = (u1Byte)(pBeamInfo->BeamformeeEntry[Idx].SoundPeriod);
			} else if (Idx == 1) {
				if (pBeamInfo->BeamformeeEntry[Idx].bSound)
					PageNum1 = 0xFE;
				else
					PageNum1 = 0xFF; /*stop sounding*/
				Period1 = (u1Byte)(pBeamInfo->BeamformeeEntry[Idx].SoundPeriod);
			}
		}
	}

	u1TxBFParm[0] = PageNum0;
	u1TxBFParm[1] = PageNum1;
	u1TxBFParm[2] = (Period1 << 4) | Period0;
	ODM_FillH2CCmd(pDM_Odm, PHYDM_H2C_TXBF, 3, u1TxBFParm);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, 
		("[%s] PageNum0 = %d Period0 = %d, PageNum1 = %d Period1 %d\n", __func__, PageNum0, Period0, PageNum1, Period1));
}


VOID
HalTxbfJaguar_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				BFerBFeeIdx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					i = 0;
	u1Byte					BFerIdx = (BFerBFeeIdx & 0xF0) >> 4;
	u1Byte					BFeeIdx = (BFerBFeeIdx & 0xF);
	u4Byte					CSI_Param;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	RT_BEAMFORMEE_ENTRY	BeamformeeEntry;
	RT_BEAMFORMER_ENTRY	BeamformerEntry;
	u2Byte					STAid = 0;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]Start!\n", __func__));

	halTxbfJaguar_RfMode(pDM_Odm, pBeamformingInfo);

	if (pDM_Odm->RFType == ODM_2T2R)
		ODM_SetBBReg(pDM_Odm, ODM_REG_CSI_CONTENT_VALUE, bMaskDWord, 0x00000000);	/*Nc =2*/
	else
		ODM_SetBBReg(pDM_Odm, ODM_REG_CSI_CONTENT_VALUE, bMaskDWord, 0x01081008);	/*Nc =1*/

	if ((pBeamformingInfo->beamformer_su_cnt > 0) && (BFerIdx < BEAMFORMER_ENTRY_NUM)) {
		BeamformerEntry = pBeamformingInfo->BeamformerEntry[BFerIdx];

		/*Sounding protocol control*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8812A, 0xCB);

		/*MAC address/Partial AID of Beamformer*/
		if (BFerIdx == 0) {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_BFMER0_INFO_8812A + i), BeamformerEntry.MacAddr[i]);
			/*CSI report use legacy ofdm so don't need to fill P_AID. */
			/*PlatformEFIOWrite2Byte(Adapter, REG_BFMER0_INFO_8812A+6, BeamformEntry.P_AID); */
		} else {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_BFMER1_INFO_8812A + i), BeamformerEntry.MacAddr[i]);
			/*CSI report use legacy ofdm so don't need to fill P_AID.*/
			/*PlatformEFIOWrite2Byte(Adapter, REG_BFMER1_INFO_8812A+6, BeamformEntry.P_AID);*/
		}

		/*CSI report parameters of Beamformee*/
		if (BeamformerEntry.BeamformEntryCap & BEAMFORMEE_CAP_VHT_SU) {
			if (pDM_Odm->RFType == ODM_2T2R)
				CSI_Param = 0x01090109;
			else
				CSI_Param = 0x01080108;
		} else {
			if (pDM_Odm->RFType == ODM_2T2R)
				CSI_Param = 0x03090309;
			else
				CSI_Param = 0x03080308;
		}

		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8812A, CSI_Param);
		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW40_8812A, CSI_Param);
		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW80_8812A, CSI_Param);

		/*Timeout value for MAC to leave NDP_RX_standby_state (60 us, Test chip) (80 us,  MP chip)*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8812A + 3, 0x50);
	}


	if ((pBeamformingInfo->beamformee_su_cnt > 0) && (BFeeIdx < BEAMFORMEE_ENTRY_NUM)) {
		BeamformeeEntry = pBeamformingInfo->BeamformeeEntry[BFeeIdx];

		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
			STAid = BeamformeeEntry.MacId;
		else
			STAid = BeamformeeEntry.P_AID;

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (BFeeIdx == 0) {
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8812A, STAid);
			ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8812A + 3, ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8812A + 3) | BIT4 | BIT6 | BIT7);
		} else
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8812A + 2, STAid | BIT12 | BIT14 | BIT15);

		/*CSI report parameters of Beamformee*/
		if (BFeeIdx == 0) {
			/*Get BIT24 & BIT25*/
			u1Byte	tmp = ODM_Read1Byte(pDM_Odm, REG_BFMEE_SEL_8812A + 3) & 0x3;

			ODM_Write1Byte(pDM_Odm, REG_BFMEE_SEL_8812A + 3, tmp | 0x60);
			ODM_Write2Byte(pDM_Odm, REG_BFMEE_SEL_8812A, STAid | BIT9);
		} else {
			/*Set BIT25*/
			ODM_Write2Byte(pDM_Odm, REG_BFMEE_SEL_8812A + 2, STAid | 0xE200);
		}
			phydm_Beamforming_Notify(pDM_Odm);
	}
}


VOID
HalTxbfJaguar_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	RT_BEAMFORMER_ENTRY	BeamformerEntry;
	RT_BEAMFORMEE_ENTRY	BeamformeeEntry;
	
	if (Idx < BEAMFORMER_ENTRY_NUM) {
		BeamformerEntry = pBeamformingInfo->BeamformerEntry[Idx];
		BeamformeeEntry = pBeamformingInfo->BeamformeeEntry[Idx];
	} else
		return;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s]Start!, IDx = %d\n", __func__, Idx));

	/*Clear P_AID of Beamformee*/
	/*Clear MAC address of Beamformer*/
	/*Clear Associated Bfmee Sel*/
	
	if (BeamformerEntry.BeamformEntryCap == BEAMFORMING_CAP_NONE) {
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8812A, 0xC8);
		if (Idx == 0) {
			ODM_Write4Byte(pDM_Odm, REG_BFMER0_INFO_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_BFMER0_INFO_8812A + 4, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW40_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW80_8812A, 0);
		} else {
			ODM_Write4Byte(pDM_Odm, REG_BFMER1_INFO_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_BFMER1_INFO_8812A + 4, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW40_8812A, 0);
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW80_8812A, 0);
		}
	}

	if (BeamformeeEntry.BeamformEntryCap == BEAMFORMING_CAP_NONE) {
		halTxbfJaguar_RfMode(pDM_Odm, pBeamformingInfo);
		if (Idx == 0) {
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8812A, 0x0);
			ODM_Write2Byte(pDM_Odm, REG_BFMEE_SEL_8812A, 0);
		} else {
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8812A + 2, ODM_Read2Byte(pDM_Odm, REG_TXBF_CTRL_8812A + 2) & 0xF000);
			ODM_Write2Byte(pDM_Odm, REG_BFMEE_SEL_8812A + 2, ODM_Read2Byte(pDM_Odm, REG_BFMEE_SEL_8812A + 2) & 0x60);
		}
	}
	
}


VOID
HalTxbfJaguar_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u2Byte					BeamCtrlVal;
	u4Byte					BeamCtrlReg;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	RT_BEAMFORMEE_ENTRY	BeamformEntry = pBeamInfo->BeamformeeEntry[Idx];

	if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
		BeamCtrlVal = BeamformEntry.MacId;
	else
		BeamCtrlVal = BeamformEntry.P_AID;

	if (Idx == 0)
		BeamCtrlReg = REG_TXBF_CTRL_8812A;
	else {
		BeamCtrlReg = REG_TXBF_CTRL_8812A + 2;
		BeamCtrlVal |= BIT12 | BIT14 | BIT15;
	}

	if (BeamformEntry.BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
		if (BeamformEntry.SoundBW == CHANNEL_WIDTH_20)
			BeamCtrlVal |= BIT9;
		else if (BeamformEntry.SoundBW == CHANNEL_WIDTH_40)
			BeamCtrlVal |= (BIT9 | BIT10);
		else if (BeamformEntry.SoundBW == CHANNEL_WIDTH_80)
			BeamCtrlVal |= (BIT9 | BIT10 | BIT11);
	} else
		BeamCtrlVal &= ~(BIT9 | BIT10 | BIT11);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] BeamCtrlVal = 0x%x!\n", __func__, BeamCtrlVal));

	ODM_Write2Byte(pDM_Odm, BeamCtrlReg, BeamCtrlVal);
}



VOID
HalTxbfJaguar_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry + Idx;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pBeamEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		halTxbfJaguar_DownloadNDPA(pDM_Odm, Idx);

	halTxbfJaguar_FwTxBFCmd(pDM_Odm);
}


VOID
HalTxbfJaguar_Patch(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Operation
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pBeamInfo->BeamformCap == BEAMFORMING_CAP_NONE)
		return;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (Operation == SCAN_OPT_BACKUP_BAND0)
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8812A, 0xC8);
	else if (Operation == SCAN_OPT_RESTORE)
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8812A, 0xCB);
#endif
}

VOID
HalTxbfJaguar_Clk_8812A(
	IN PVOID			pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u2Byte	u2btmp;
	u1Byte	Count = 0, u1btmp;
	PADAPTER	Adapter = pDM_Odm->Adapter;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (*(pDM_Odm->pbScanInProcess)) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] return by Scan\n", __func__));
		return;
	}
#if DEV_BUS_TYPE == RT_PCI_INTERFACE	
	/*Stop PCIe TxDMA*/
	ODM_Write1Byte(pDM_Odm, REG_PCIE_CTRL_REG_8812A + 1, 0xFE);
#endif

	/*Stop Usb TxDMA*/
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISABLE_FUNC(Adapter, DF_TX_BIT);
	PlatformReturnAllPendingTxPackets(Adapter);
#else
	rtw_write_port_cancel(Adapter);
#endif

	/*Wait TXFF empty*/
	for (Count = 0; Count < 100; Count++) {
		u2btmp = ODM_Read2Byte(pDM_Odm, REG_TXPKT_EMPTY_8812A);
		u2btmp = u2btmp & 0xfff;
		if (u2btmp != 0xfff) {
			ODM_delay_ms(10);
			continue;
		} else
			break;
	}

	/*TX pause*/
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE_8812A, 0xFF);

	/*Wait TX State Machine OK*/
	for (Count = 0; Count < 100; Count++) {
		if (ODM_Read4Byte(pDM_Odm, REG_SCH_TXCMD_8812A) != 0)
			continue;
		else
			break;
	}


	/*Stop RX DMA path*/
	u1btmp = ODM_Read1Byte(pDM_Odm, REG_RXDMA_CONTROL_8812A);
	ODM_Write1Byte(pDM_Odm, REG_RXDMA_CONTROL_8812A, u1btmp | BIT2);

	for (Count = 0; Count < 100; Count++) {
		u1btmp = ODM_Read1Byte(pDM_Odm, REG_RXDMA_CONTROL_8812A);
		if (u1btmp & BIT1)
			break;
		else
			ODM_delay_ms(10);
	}

	/*Disable clock*/
	ODM_Write1Byte(pDM_Odm, REG_SYS_CLKR_8812A + 1, 0xf0);
	/*Disable 320M*/
	ODM_Write1Byte(pDM_Odm, REG_AFE_PLL_CTRL_8812A + 3, 0x8);
	/*Enable 320M*/
	ODM_Write1Byte(pDM_Odm, REG_AFE_PLL_CTRL_8812A + 3, 0xa);
	/*Enable clock*/
	ODM_Write1Byte(pDM_Odm, REG_SYS_CLKR_8812A + 1, 0xfc);


	/*Release Tx pause*/
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE_8812A, 0);

	/*Enable RX DMA path*/
	u1btmp = ODM_Read1Byte(pDM_Odm, REG_RXDMA_CONTROL_8812A);
	ODM_Write1Byte(pDM_Odm, REG_RXDMA_CONTROL_8812A, u1btmp & (~BIT2));
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	/*Enable PCIe TxDMA*/
	ODM_Write1Byte(pDM_Odm, REG_PCIE_CTRL_REG_8812A + 1, 0);
#endif
	/*Start Usb TxDMA*/
	RT_ENABLE_FUNC(Adapter, DF_TX_BIT);
}

#endif



#endif

