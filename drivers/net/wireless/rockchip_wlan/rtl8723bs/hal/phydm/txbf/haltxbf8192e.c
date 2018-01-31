/* SPDX-License-Identifier: GPL-2.0 */
//============================================================
// Description:
//
// This file is for 8192E TXBF mechanism
//
//============================================================
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8192E_SUPPORT == 1)

VOID
HalTxbf8192E_setNDPArate(
	IN PVOID			pDM_VOID,
	IN u1Byte	BW,
	IN u1Byte	Rate
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	
	ODM_Write1Byte(pDM_Odm, REG_NDPA_OPT_CTRL_8192E,  (Rate << 2 | BW));	

}

VOID
halTxbf8192E_RfMode(
	IN PVOID			pDM_VOID,
	IN PRT_BEAMFORMING_INFO	pBeamInfo
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN				bSelfBeamformer = FALSE;
	BOOLEAN				bSelfBeamformee = FALSE;
	BEAMFORMING_CAP	BeamformCap = BEAMFORMING_CAP_NONE;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pDM_Odm->RFType == ODM_1T1R)
		return;

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x1); /*RF Mode table write enable*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_WE_LUT, 0x80000, 0x1); /*RF Mode table write enable*/
	
	if (pBeamInfo->beamformee_su_cnt > 0) {
		/*Path_A*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableAddr, 0xfffff, 0x18000);	/*Select RX mode  0x30=0x18000*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff, 0x0000f);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0x77fc2);	/*Enable TXIQGEN in RX mode*/
		/*Path_B*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableAddr, 0xfffff, 0x18000);	/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff, 0x0000f);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff, 0x77fc2);	/*Enable TXIQGEN in RX mode*/
	} else {
		/*Path_A*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableAddr, 0xfffff, 0x18000);	/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff, 0x0000f);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0x77f82);	/*Disable TXIQGEN in RX mode*/
		/*Path_B*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableAddr, 0xfffff, 0x18000);	/*Select RX mode*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff, 0x0000f);	/*Set Table data*/
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff, 0x77f82);	/*Disable TXIQGEN in RX mode*/
	}

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_WE_LUT, 0x80000, 0x0);	/*RF Mode table write disable*/
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_WE_LUT, 0x80000, 0x0);	/*RF Mode table write disable*/

	if (pBeamInfo->beamformee_su_cnt > 0) {
		ODM_SetBBReg(pDM_Odm, rFPGA1_TxInfo, bMaskDWord, 0x83321333);
		ODM_SetBBReg(pDM_Odm, rCCK0_AFESetting, bMaskByte3, 0xc1);
	} else
		ODM_SetBBReg(pDM_Odm, rFPGA1_TxInfo, bMaskDWord, 0x81121313);
}



VOID
halTxbf8192E_FwTxBFCmd(
	IN PVOID			pDM_VOID
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte	Idx, Period0 = 0, Period1 = 0;
	u1Byte	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u1Byte	u1TxBFParm[3] = {0};
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;

	for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
		if (pBeamInfo->BeamformeeEntry[Idx].BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (Idx == 0) {
				if (pBeamInfo->BeamformeeEntry[Idx].bSound)
					PageNum0 = 0xFE;
				else
					PageNum0 = 0xFF; //stop sounding
				Period0 = (u1Byte)(pBeamInfo->BeamformeeEntry[Idx].SoundPeriod);
			} else if (Idx == 1) {
				if (pBeamInfo->BeamformeeEntry[Idx].bSound)
					PageNum1 = 0xFE;
				else
					PageNum1 = 0xFF; //stop sounding
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
halTxbf8192E_DownloadNDPA(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			u1bTmp = 0, tmpReg422 = 0, Head_Page;
	u1Byte			BcnValidReg = 0, count = 0, DLBcnCount = 0;
	BOOLEAN			bSendBeacon = FALSE;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	u1Byte			TxPageBndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8812;	
	/*default reseved 1 page for the IC type which is undefined.*/
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry + Idx;
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*pDM_Odm->pbFwDwRsvdPageInProgress = TRUE;
#endif
	if (Idx == 0)
		Head_Page = 0xFE;
	else
		Head_Page = 0xFE;

	Adapter->HalFunc.GetHalDefVarHandler(Adapter, HAL_DEF_TX_PAGE_BOUNDARY, (pu1Byte)&TxPageBndy);

	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1bTmp = ODM_Read1Byte(pDM_Odm, REG_CR_8192E+1);
	ODM_Write1Byte(pDM_Odm,  REG_CR_8192E+1, (u1bTmp | BIT0));

	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmpReg422 = ODM_Read1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8192E+2);
	ODM_Write1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8192E+2,  tmpReg422 & (~BIT6));

	if (tmpReg422 & BIT6) {
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_WARNING, ("%s There is an Adapter is sending beacon.\n", __func__));
		bSendBeacon = TRUE;
	}

	/*TDECTRL[15:8] 0x209[7:0] = 0xFE/0xFD	NDPA Head for TXDMA*/
	ODM_Write1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+1, Head_Page);

	do {
		/*Clear beacon valid check bit.*/
		BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+2);
		ODM_Write1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+2, (BcnValidReg | BIT0));

		// download NDPA rsvd page.
		Beamforming_SendHTNDPAPacket(pDM_Odm, pBeamEntry->MacAddr, pBeamEntry->SoundBW, BEACON_QUEUE);

#if(DEV_BUS_TYPE == RT_PCI_INTERFACE)
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_MGQ_TXBD_NUM_8192E+3);
		count = 0;
		while ((count < 20) && (u1bTmp & BIT4)) {
			count++;
			ODM_delay_us(10);
			u1bTmp = ODM_Read1Byte(pDM_Odm, REG_MGQ_TXBD_NUM_8192E+3);
		}
		ODM_Write1Byte(pDM_Odm, REG_MGQ_TXBD_NUM_8192E+3, u1bTmp | BIT4);
#endif

		/*check rsvd page download OK.*/
		BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+2);
		count = 0;
		while (!(BcnValidReg & BIT0) && count < 20) {
			count++;
			ODM_delay_us(10);
			BcnValidReg = ODM_Read1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+2);
		}
		DLBcnCount++;
	} while (!(BcnValidReg & BIT0) && DLBcnCount < 5);

	if (!(BcnValidReg & BIT0))
		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_WARNING, ("%s Download RSVD page failed!\n", __func__));

	/*TDECTRL[15:8] 0x209[7:0] = 0xF9	Beacon Head for TXDMA*/
	ODM_Write1Byte(pDM_Odm, REG_DWBCN0_CTRL_8192E+1, TxPageBndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause*/
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (bSendBeacon)
		ODM_Write1Byte(pDM_Odm, REG_FWHW_TXQ_CTRL_8192E+2, tmpReg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1bTmp = ODM_Read1Byte(pDM_Odm, REG_CR_8192E+1);
	ODM_Write1Byte(pDM_Odm, REG_CR_8192E+1, (u1bTmp & (~BIT0)));

	pBeamEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSED;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	*pDM_Odm->pbFwDwRsvdPageInProgress = FALSE;
#endif
}


VOID
HalTxbf8192E_Enter(
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

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	halTxbf8192E_RfMode(pDM_Odm, pBeamformingInfo);

	if (pDM_Odm->RFType == ODM_2T2R)
		ODM_Write4Byte(pDM_Odm, 0xd80, 0x00000000);		/*Nc =2*/

	if ((pBeamformingInfo->beamformer_su_cnt > 0) && (BFerIdx < BEAMFORMER_ENTRY_NUM)) {
		BeamformerEntry = pBeamformingInfo->BeamformerEntry[BFerIdx];

		/*Sounding protocol control*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8192E, 0xCB);

		/*MAC address/Partial AID of Beamformer*/
		if (BFerIdx == 0) {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_ASSOCIATED_BFMER0_INFO_8192E+i), BeamformerEntry.MacAddr[i]);
		} else {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_ASSOCIATED_BFMER1_INFO_8192E+i), BeamformerEntry.MacAddr[i]);
		}

		/*CSI report parameters of Beamformer Default use Nc = 2*/
		CSI_Param = 0x03090309;

		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8192E, CSI_Param);
		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW40_8192E, CSI_Param);
		ODM_Write4Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW80_8192E, CSI_Param);

		/*Timeout value for MAC to leave NDP_RX_standby_state (60 us, Test chip) (80 us,  MP chip)*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8192E+3, 0x50);

	}

	if ((pBeamformingInfo->beamformee_su_cnt > 0) && (BFeeIdx < BEAMFORMEE_ENTRY_NUM)) {
		BeamformeeEntry = pBeamformingInfo->BeamformeeEntry[BFeeIdx];

		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
			STAid = BeamformeeEntry.MacId;
		else
			STAid = BeamformeeEntry.P_AID;

		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s], STAid=0x%X\n", __func__, STAid));

		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (BFeeIdx == 0) {
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8192E, STAid);
			ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8192E+3, ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8192E+3) | BIT4 | BIT6 | BIT7);
		} else
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8192E+2, STAid | BIT12 | BIT14 | BIT15);

		/*CSI report parameters of Beamformee*/
		if (BFeeIdx == 0) {
			/*Get BIT24 & BIT25*/
			u1Byte tmp = ODM_Read1Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E+3) & 0x3;
			
			ODM_Write1Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E+3, tmp | 0x60);
			ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E, STAid | BIT9);
		} else {
			/*Set BIT25*/
			ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2, STAid | 0xE200);
		}
			phydm_Beamforming_Notify(pDM_Odm);

	}
}


VOID
HalTxbf8192E_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;

	halTxbf8192E_RfMode(pDM_Odm, pBeamInfo);

	/*	Clear P_AID of Beamformee
	* 	Clear MAC addresss of Beamformer
	*	Clear Associated Bfmee Sel
	*/
	if (pBeamInfo->BeamformCap == BEAMFORMING_CAP_NONE)
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8192E, 0xC8);

	if (Idx == 0) {
		ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8192E, 0);
		ODM_Write4Byte(pDM_Odm, REG_ASSOCIATED_BFMER0_INFO_8192E, 0);
		ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMER0_INFO_8192E+4, 0);
		ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E, 0);
	} else {
		ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8192E+2, ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8192E+2) & 0xF000);
		ODM_Write4Byte(pDM_Odm, REG_ASSOCIATED_BFMER1_INFO_8192E, 0);
		ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMER1_INFO_8192E+4, 0);
		ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2, ODM_Read2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8192E+2) & 0x60);
	}

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Idx %d\n", __func__, Idx));
}


VOID
HalTxbf8192E_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u2Byte					BeamCtrlVal;
	u4Byte					BeamCtrlReg;
	PRT_BEAMFORMING_INFO	pBeamInfo =  &pDM_Odm->BeamformingInfo;
	RT_BEAMFORMEE_ENTRY	BeamformEntry = pBeamInfo->BeamformeeEntry[Idx];

	if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
		BeamCtrlVal = BeamformEntry.MacId;
	else
		BeamCtrlVal = BeamformEntry.P_AID;

	if (Idx == 0)
		BeamCtrlReg = REG_TXBF_CTRL_8192E;
	else {
		BeamCtrlReg = REG_TXBF_CTRL_8192E+2;
		BeamCtrlVal |= BIT12 | BIT14 | BIT15;
	}

	if (BeamformEntry.BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
		if (BeamformEntry.SoundBW == CHANNEL_WIDTH_20)
			BeamCtrlVal |= BIT9;
		else if (BeamformEntry.SoundBW == CHANNEL_WIDTH_40)
			BeamCtrlVal |= BIT10;
	} else
		BeamCtrlVal &= ~(BIT9 | BIT10 | BIT11);

	ODM_Write2Byte(pDM_Odm, BeamCtrlReg, BeamCtrlVal);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Idx %d BeamCtrlReg %x BeamCtrlVal %x\n", __func__, Idx, BeamCtrlReg, BeamCtrlVal));
}


VOID
HalTxbf8192E_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry + Idx;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("[%s] Start!\n", __func__));

	if (pBeamEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		halTxbf8192E_DownloadNDPA(pDM_Odm, Idx);

	halTxbf8192E_FwTxBFCmd(pDM_Odm);
}

#endif	/* #if (RTL8192E_SUPPORT == 1)*/

#endif

