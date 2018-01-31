/* SPDX-License-Identifier: GPL-2.0 */
/*============================================================*/
/* Description:                                               */
/*                                                            */
/* This file is for 8814A TXBF mechanism                      */
/*                                                            */
/*============================================================*/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (BEAMFORMING_SUPPORT == 1)
#if (RTL8822B_SUPPORT == 1)

#if 0
VOID
HalTxbf8814A_GetBeamformcap(
	IN PADAPTER	Adapter
)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = GET_BEAMFORM_INFO(Adapter);
	BEAMFORMING_CAP	BeamformCap = BEAMFORMING_CAP_NONE;

	BeamformCap = phydm_Beamforming_GetBeamCap(pDM_Odm, pBeamformingInfo);

	if (BeamformCap == pBeamformingInfo->BeamformCap)
		return;
	else 
		pBeamformingInfo->BeamformCap = BeamformCap;

}

VOID
HalTxbf8814A_GetTxRate(
	IN	PADAPTER			Adapter
)
{

	HAL_DATA_TYPE				*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T					pDM_Odm = &pHalData->DM_OutSrc;
	PRT_BEAMFORMING_INFO			pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	PRT_BEAMFORMEE_ENTRY	pEntry;
	u4Byte		TxRptData = 0;
	u1Byte		DataRate = 0xFF;

	pEntry = &(pBeamInfo->BeamformeeEntry[pBeamInfo->BeamformeeCurIdx]);

	ReadSdramData_8814A(Adapter, (u1Byte)pEntry->MacId, LOC_8814A_CTRL_INFO, &TxRptData, 1);
	DataRate = (u1Byte)TxRptData;
	DataRate &= bMask7bits;   /*Bit7 indicates SGI*/
	
	pDM_Odm->TxBfDataRate = DataRate;

}

VOID
HalTxbf8814A_ResetTxPath(
	IN	PADAPTER			Adapter,
	IN	u1Byte				idx
)
{
#if DEV_BUS_TYPE == RT_USB_INTERFACE

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter); 
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc; 
	PRT_BEAMFORMING_INFO	pBeamformingInfo = GET_BEAMFORM_INFO(Adapter);
	RT_BEAMFORMEE_ENTRY	BeamformeeEntry;
	u1Byte	Nr_index = 0;
	
	if (idx < BEAMFORMEE_ENTRY_NUM)
		BeamformeeEntry = pBeamformingInfo->BeamformeeEntry[idx];
	else
		return;
	
	if ((pDM_Odm->LastUSBHub) != (RT_GetHubUSBMode(Adapter))) {	
		Nr_index = TxBF_Nr(halTxbf8814A_GetNtx(Adapter), BeamformeeEntry.CompSteeringNumofBFer);

		if (idx == 0) {
			switch (Nr_index) {			
			case 0:	
			break;

			case 1:			/*Nsts = 2	BC*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT3|BIT2|BIT1|BIT0, 0x6);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT7|BIT6|BIT5|BIT4, 0x6);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, 0x0000ff00, 0x10);				/*BC*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0x6);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x10);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskLWord, 0x1060);
			break;

			case 2:			/*Nsts = 3	BCD*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT3|BIT2|BIT1|BIT0, 0xe);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT7|BIT6|BIT5|BIT4, 0xe);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, 0x0000ff00, 0x90);				/*BCD*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT19|BIT18|BIT17|BIT16, 0xe);	/*3ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, 0xff00000, 0x90);					/*bcd*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0xe);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x90);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskDWord, 0x90e90e0);
			break;
			
			default:			/*Nr>3, same as Case 3*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT3|BIT2|BIT1|BIT0, 0xf);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT7|BIT6|BIT5|BIT4, 0xf);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, 0x0000ff00, 0x93);				/*BC*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, BIT19|BIT18|BIT17|BIT16, 0xf);	/*3ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF0, 0xff00000, 0x93);					/*bcd*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0xf);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x93);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskDWord, 0x93f93f0);
			break;
			}
		} else	{
			switch (Nr_index) {
			case 0:	
			break;

			case 1:			/*Nsts = 2	BC*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT3|BIT2|BIT1|BIT0, 0x6);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT7|BIT6|BIT5|BIT4, 0x6);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, 0x0000ff00, 0x10);				/*BC*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0x6);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x10);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskLWord, 0x1060);
			break;

			case 2:			/*Nsts = 3	BCD*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT3|BIT2|BIT1|BIT0, 0xe);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT7|BIT6|BIT5|BIT4, 0xe);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, 0x0000ff00, 0x90);				/*BC*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT19|BIT18|BIT17|BIT16, 0xe);	/*3ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, 0xff00000, 0x90);					/*bcd*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0xe);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x90);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskDWord, 0x90e90e0);
			break;
			
			default:			/*Nr>3, same as Case 3*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT3|BIT2|BIT1|BIT0, 0xf);		/*1ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT7|BIT6|BIT5|BIT4, 0xf);		/*2ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, 0x0000ff00, 0x93);				/*BC*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, BIT19|BIT18|BIT17|BIT16, 0xf);	/*3ss*/
			PHY_SetBBReg(Adapter, REG_BB_TXBF_ANT_SET_BF1, 0xff00000, 0x93);					/*bcd*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, BIT23|BIT22|BIT21|BIT20, 0xf);	/*set TxPath selection for 8814a BFer bug refine*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_1, bMaskByte3, 0x93);				/*if Bfer enable, always use 3Tx for all Spatial stream*/
			PHY_SetBBReg(Adapter, REG_BB_TX_PATH_SEL_2, bMaskDWord, 0x93f93f0);
			break;
		
			}
		}

			pDM_Odm->LastUSBHub = RT_GetHubUSBMode(Adapter);
	}
	else
		return;
#endif
}
#endif

u1Byte
halTxbf8822B_GetNtx(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			Ntx = 0;

#if DEV_BUS_TYPE == RT_USB_INTERFACE
	if (pDM_Odm->SupportInterface == ODM_ITRF_USB) {
		if (*pDM_Odm->HubUsbMode == 2) {/*USB3.0*/
			if (pDM_Odm->RFType == ODM_4T4R)
				Ntx = 3;
			else if (pDM_Odm->RFType == ODM_3T3R)
				Ntx = 2;
			else
				Ntx = 1;
		} else if (*pDM_Odm->HubUsbMode == 1)	/*USB 2.0 always 2Tx*/
			Ntx = 1;
		else
			Ntx = 1;
	} else
#endif
	{
		if (pDM_Odm->RFType == ODM_4T4R)
			Ntx = 3;
		else if (pDM_Odm->RFType == ODM_3T3R)
			Ntx = 2;
		else
			Ntx = 1;
	}

	return Ntx;

}

u1Byte
halTxbf8822B_GetNrx(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte			Nrx = 0;

	if (pDM_Odm->RFType == ODM_4T4R)
		Nrx = 3;
	else if (pDM_Odm->RFType == ODM_3T3R)
		Nrx = 2;
	else if (pDM_Odm->RFType == ODM_2T2R)
		Nrx = 1;
	else if (pDM_Odm->RFType == ODM_2T3R)
		Nrx = 2;
	else if (pDM_Odm->RFType == ODM_2T4R)
		Nrx = 3;
	else if (pDM_Odm->RFType == ODM_1T1R)
		Nrx = 0;
	else if (pDM_Odm->RFType == ODM_1T2R)
		Nrx = 1;
	else
		Nrx = 0;

	return Nrx;
	
}

/***************SU & MU BFee Entry********************/
VOID
halTxbf8822B_RfMode(
	IN PVOID			pDM_VOID,
	IN	PRT_BEAMFORMING_INFO	pBeamformingInfo,
	IN	u1Byte					idx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte				i, Nr_index = 0;
	BOOLEAN				bSelfBeamformer = FALSE;
	BOOLEAN				bSelfBeamformee = FALSE;
	RT_BEAMFORMEE_ENTRY	BeamformeeEntry;

	if (idx < BEAMFORMEE_ENTRY_NUM)
		BeamformeeEntry = pBeamformingInfo->BeamformeeEntry[idx];
	else
		return;

	if (pDM_Odm->RFType == ODM_1T1R)
		return;

	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_B; i++) {
		ODM_SetRFReg(pDM_Odm, i, RF_WeLut_Jaguar, 0x80000, 0x1);
		/*RF Mode table write enable*/
	}

	if ((pBeamformingInfo->beamformee_su_cnt > 0) || (pBeamformingInfo->beamformee_mu_cnt > 0)) {
		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_B; i++) {
			ODM_SetRFReg(pDM_Odm, i, RF_ModeTableAddr, 0xfffff, 0x18000);
			/*Select RX mode*/
			ODM_SetRFReg(pDM_Odm, i, RF_ModeTableData0, 0xfffff, 0xBE77F);
			/*Set Table data*/
			ODM_SetRFReg(pDM_Odm, i, RF_ModeTableData1, 0xfffff, 0x226BF);
			/*Enable TXIQGEN in RX mode*/
		}
		ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff, 0xE26BF);
		/*Enable TXIQGEN in RX mode*/
	}

	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_B; i++) {
		ODM_SetRFReg(pDM_Odm, i, RF_WeLut_Jaguar, 0x80000, 0x0);
		/*RF Mode table write disable*/
	}

	if (pBeamformingInfo->beamformee_su_cnt > 0) {

		/*for 8814 19ac(idx 1), 19b4(idx 0), different Tx ant setting*/
		ODM_SetBBReg(pDM_Odm, REG_BB_TXBF_ANT_SET_BF1, BIT28|BIT29, 0x2);			/*enable BB TxBF ant mapping register*/
		
		if (idx == 0) {
			/*Nsts = 2	AB*/
			ODM_SetBBReg(pDM_Odm, REG_BB_TXBF_ANT_SET_BF0, 0xffff, 0x0433);
			ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_1, 0xfff00000, 0x043);
			/*ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_2, bMaskLWord, 0x430);*/

		} else {/*IDX =1*/
			ODM_SetBBReg(pDM_Odm, REG_BB_TXBF_ANT_SET_BF1, 0xffff, 0x0433);
			ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_1, 0xfff00000, 0x043);
			/*ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_2, bMaskLWord, 0x430;*/
		}
	} else {
		ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_1, 0xfff00000, 0x1); /*1SS by path-A*/
		ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_2, bMaskLWord, 0x430); /*2SS by path-A,B*/
	}
	
	if (pBeamformingInfo->beamformee_mu_cnt > 0) {
		/*MU STAs share the common setting*/
		ODM_SetBBReg(pDM_Odm, REG_BB_TXBF_ANT_SET_BF1, BIT31, 1);
		ODM_SetBBReg(pDM_Odm, REG_BB_TXBF_ANT_SET_BF1, 0xffff, 0x0433);
		ODM_SetBBReg(pDM_Odm, REG_BB_TX_PATH_SEL_1, 0xfff00000, 0x043);
	}

}
#if 0
VOID
halTxbf8822B_DownloadNDPA(
	IN	PADAPTER			Adapter,
	IN	u1Byte				Idx
	)
{
	u1Byte			u1bTmp = 0, tmpReg422 = 0;
	u1Byte			BcnValidReg = 0, count = 0, DLBcnCount = 0;
	u2Byte			Head_Page = 0x7FE;
	BOOLEAN			bSendBeacon = FALSE;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u2Byte			TxPageBndy = LAST_ENTRY_OF_TX_PKT_BUFFER_8814A; /*default reseved 1 page for the IC type which is undefined.*/
	PRT_BEAMFORMING_INFO	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry+Idx;

	pHalData->bFwDwRsvdPageInProgress = TRUE;
	Adapter->HalFunc.GetHalDefVarHandler(Adapter, HAL_DEF_TX_PAGE_BOUNDARY, (pu2Byte)&TxPageBndy);
	
	/*Set REG_CR bit 8. DMA beacon by SW.*/
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR_8814A+1);
	PlatformEFIOWrite1Byte(Adapter,  REG_CR_8814A+1, (u1bTmp|BIT0));


	/*Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.*/
	tmpReg422 = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL_8814A+2);
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL_8814A+2,  tmpReg422&(~BIT6));

	if (tmpReg422 & BIT6) {
		RT_TRACE(COMP_INIT, DBG_LOUD, ("SetBeamformDownloadNDPA_8814A(): There is an Adapter is sending beacon.\n"));
		bSendBeacon = TRUE;
	}

	/*0x204[11:0]	Beacon Head for TXDMA*/
	PlatformEFIOWrite2Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A, Head_Page);
	
	do {		
		/*Clear beacon valid check bit.*/
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A+1);
		PlatformEFIOWrite1Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A+1, (BcnValidReg|BIT7));
		
		/*download NDPA rsvd page.*/
		if (pBeamEntry->BeamformEntryCap & BEAMFORMER_CAP_VHT_SU)
			Beamforming_SendVHTNDPAPacket(pDM_Odm, pBeamEntry->MacAddr, pBeamEntry->AID, pBeamEntry->SoundBW, BEACON_QUEUE);
		else 
			Beamforming_SendHTNDPAPacket(pDM_Odm, pBeamEntry->MacAddr, pBeamEntry->SoundBW, BEACON_QUEUE);
	
		/*check rsvd page download OK.*/
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A + 1);
		count = 0;
		while (!(BcnValidReg & BIT7) && count < 20) {
			count++;
			delay_us(10);
			BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A+2);
		}
		DLBcnCount++;
	} while (!(BcnValidReg & BIT7) && DLBcnCount < 5);
	
	if (!(BcnValidReg & BIT0))
		RT_DISP(FBEAM, FBEAM_ERROR, ("%s Download RSVD page failed!\n", __func__));

	/*0x204[11:0]	Beacon Head for TXDMA*/
	PlatformEFIOWrite2Byte(Adapter, REG_FIFOPAGE_CTRL_2_8814A, TxPageBndy);

	/*To make sure that if there exists an adapter which would like to send beacon.*/
	/*If exists, the origianl value of 0x422[6] will be 1, we should check this to*/
	/*prevent from setting 0x422[6] to 0 after download reserved page, or it will cause */
	/*the beacon cannot be sent by HW.*/
	/*2010.06.23. Added by tynli.*/
	if (bSendBeacon)
		PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL_8814A+2, tmpReg422);

	/*Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.*/
	/*Clear CR[8] or beacon packet will not be send to TxBuf anymore.*/
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR_8814A+1);
	PlatformEFIOWrite1Byte(Adapter, REG_CR_8814A+1, (u1bTmp&(~BIT0)));

	pBeamEntry->BeamformEntryState = BEAMFORMING_ENTRY_STATE_PROGRESSED;

	pHalData->bFwDwRsvdPageInProgress = FALSE;
}

VOID
halTxbf8822B_FwTxBFCmd(
	IN	PADAPTER	Adapter
	)
{
	u1Byte	Idx, Period = 0;
	u1Byte	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u1Byte	u1TxBFParm[3] = {0};

	PMGNT_INFO				pMgntInfo = &(Adapter->MgntInfo);
	PRT_BEAMFORMING_INFO	pBeamInfo = GET_BEAMFORM_INFO(Adapter);

	for (Idx = 0; Idx < BEAMFORMEE_ENTRY_NUM; Idx++) {
		if (pBeamInfo->BeamformeeEntry[Idx].bUsed && pBeamInfo->BeamformeeEntry[Idx].BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (pBeamInfo->BeamformeeEntry[Idx].bSound) {
				PageNum0 = 0xFE;
				PageNum1 = 0x07;
				Period = (u1Byte)(pBeamInfo->BeamformeeEntry[Idx].SoundPeriod);
			} else if (PageNum0 == 0xFF) {
				PageNum0 = 0xFF; /*stop sounding*/
				PageNum1 = 0x0F;
			}
		}
	}

	u1TxBFParm[0] = PageNum0;
	u1TxBFParm[1] = PageNum1;
	u1TxBFParm[2] = Period;
	FillH2CCmd(Adapter, PHYDM_H2C_TXBF, 3, u1TxBFParm);
	
	RT_DISP(FBEAM, FBEAM_FUN, ("@%s End, PageNum0 = 0x%x, PageNum1 = 0x%x Period = %d", __func__, PageNum0, PageNum1, Period));
}
#endif

VOID
HalTxbf8822B_Init(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte		u1bTmp;
	PRT_BEAMFORMING_INFO		pBeamformingInfo = &pDM_Odm->BeamformingInfo;

	ODM_SetBBReg(pDM_Odm, 0x14c0 , BIT16, 1); /*Enable P1 aggr new packet according to P0 transfer time*/
	ODM_SetBBReg(pDM_Odm, 0x14c0 , BIT15|BIT14|BIT13|BIT12, 1); /*MU Retry Limit*/
	ODM_SetBBReg(pDM_Odm, 0x14c0 , BIT7, 0); /*Disable Tx MU-MIMO until sounding done*/	
	ODM_SetBBReg(pDM_Odm, 0x14c0 , 0x3F, 0); /* Clear validity of MU STAs */
	ODM_Write1Byte(pDM_Odm, 0x167c , 0x70); /*MU-MIMO Option as default value*/
	ODM_Write2Byte(pDM_Odm, 0x1680 , 0); /*MU-MIMO Control as default value*/

	/* Set MU NDPA rate & BW source */
	/* 0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
	u1bTmp = ODM_Read1Byte(pDM_Odm, 0x42C);
	ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8822B, (u1bTmp|BIT6));
	/* 0x45F[7:0] = 0x10 (Rate=OFDM_6M, BW20) */
	ODM_Write1Byte(pDM_Odm, REG_NDPA_OPT_CTRL_8822B, 0x10);

	/* Init HW variable */
	pBeamformingInfo->RegMUTxCtrl = ODM_Read4Byte(pDM_Odm, 0x14c0);
}

VOID
HalTxbf8822B_Enter(
	IN PVOID			pDM_VOID,
	IN u1Byte				BFerBFeeIdx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte					i = 0;
	u1Byte					BFerIdx = (BFerBFeeIdx & 0xF0)>>4;
	u1Byte					BFeeIdx = (BFerBFeeIdx & 0xF);
	u2Byte					CSI_Param = 0;
	PRT_BEAMFORMING_INFO		pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamformeeEntry;
	PRT_BEAMFORMER_ENTRY	pBeamformerEntry;
	u2Byte					value16, STAid = 0;
	u1Byte					Nc_index = 0, Nr_index = 0, grouping = 0, codebookinfo = 0, coefficientsize = 0;
	u4Byte					gid_valid, user_position_l, user_position_h;
	u4Byte					mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};
	u1Byte					u1bTmp;
	u4Byte					u4bTmp;
	
	RT_DISP(FBEAM, FBEAM_FUN, ("%s: BFerBFeeIdx=%d, BFerIdx=%d, BFeeIdx=%d\n", __func__, BFerBFeeIdx, BFerIdx, BFeeIdx));

	/*************SU BFer Entry Init*************/
	if ((pBeamformingInfo->beamformer_su_cnt > 0) && (BFerIdx < BEAMFORMER_ENTRY_NUM)) {
		pBeamformerEntry = &pBeamformingInfo->BeamformerEntry[BFerIdx];
		pBeamformerEntry->is_mu_ap = FALSE;
		/*Sounding protocol control*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B, 0xDB);	
	
		
		for (i = 0; i < MAX_BEAMFORMER_SU; i++) {
			if ((pBeamformingInfo->beamformer_su_reg_maping & BIT(i)) == 0) {
				pBeamformingInfo->beamformer_su_reg_maping |= BIT(i);
				pBeamformerEntry->su_reg_index = i;
				break;
			}
		}
		
		/*MAC address/Partial AID of Beamformer*/
		if (pBeamformerEntry->su_reg_index == 0) {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_ASSOCIATED_BFMER0_INFO_8822B+i), pBeamformerEntry->MacAddr[i]);
		} else {
			for (i = 0; i < 6 ; i++)
				ODM_Write1Byte(pDM_Odm, (REG_ASSOCIATED_BFMER1_INFO_8822B+i), pBeamformerEntry->MacAddr[i]);
		}

		/*CSI report parameters of Beamformer*/
		Nc_index = halTxbf8822B_GetNrx(pDM_Odm);	/*for 8814A Nrx = 3(4 Ant), min=0(1 Ant)*/
		Nr_index = pBeamformerEntry->NumofSoundingDim;	/*0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so Nr_index don't care*/
		
		grouping = 0;

		/*for ac = 1, for n = 3*/
		if (pBeamformerEntry->BeamformEntryCap & BEAMFORMEE_CAP_VHT_SU)
			codebookinfo = 1;	
		else if (pBeamformerEntry->BeamformEntryCap & BEAMFORMEE_CAP_HT_EXPLICIT)
			codebookinfo = 3;	

		coefficientsize = 3;

		CSI_Param = (u2Byte)((coefficientsize<<10)|(codebookinfo<<8)|(grouping<<6)|(Nr_index<<3)|(Nc_index));

		if (BFerIdx == 0)
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8822B, CSI_Param);
		else
			ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8822B+2, CSI_Param);
		/*ndp_rx_standby_timer, 8814 need > 0x56, suggest from Dvaid*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8814A+3, 0x70);
	
	}

	/*************SU BFee Entry Init*************/
	if ((pBeamformingInfo->beamformee_su_cnt > 0) && (BFeeIdx < BEAMFORMEE_ENTRY_NUM)) {
		pBeamformeeEntry = &pBeamformingInfo->BeamformeeEntry[BFeeIdx];
		pBeamformeeEntry->is_mu_sta = FALSE;
		halTxbf8822B_RfMode(pDM_Odm, pBeamformingInfo, BFeeIdx);
		
		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
			STAid = pBeamformeeEntry->MacId;
		else 
			STAid = pBeamformeeEntry->P_AID;

		for (i = 0; i < MAX_BEAMFORMEE_SU; i++) {
			if ((pBeamformingInfo->beamformee_su_reg_maping & BIT(i)) == 0) {
				pBeamformingInfo->beamformee_su_reg_maping |= BIT(i);
				pBeamformeeEntry->su_reg_index = i;
				break;
			}
		}
		
		/*P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt*/
		if (pBeamformeeEntry->su_reg_index == 0) {	
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8822B, STAid);	
			ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3, ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3)|BIT4|BIT6|BIT7);
		} else {
			ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8822B+2, STAid | BIT14 | BIT15 | BIT12);
		}	

		/*CSI report parameters of Beamformee*/
		if (pBeamformeeEntry->su_reg_index == 0) {
			/*Get BIT24 & BIT25*/
			u1Byte	tmp = ODM_Read1Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B+3) & 0x3;
			
			ODM_Write1Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B + 3, tmp | 0x60);
			ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B, STAid | BIT9);
		} else		
			ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B+2, STAid | 0xE200);	/*Set BIT25*/
			
			phydm_Beamforming_Notify(pDM_Odm);
	}

	/*************MU BFer Entry Init*************/
	if ((pBeamformingInfo->beamformer_mu_cnt > 0) && (BFerIdx < BEAMFORMER_ENTRY_NUM)) {
		pBeamformerEntry = &pBeamformingInfo->BeamformerEntry[BFerIdx];
		pBeamformingInfo->mu_ap_index = BFerIdx;
		pBeamformerEntry->is_mu_ap = TRUE;
		for (i = 0; i < 8; i++)
			pBeamformerEntry->gid_valid[i] = 0;
		for (i = 0; i < 16; i++)
			pBeamformerEntry->user_position[i] = 0;
		
		/*Sounding protocol control*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B, 0xDB);	
		
		/* MAC address */
		for (i = 0; i < 6 ; i++)
			ODM_Write1Byte(pDM_Odm, (REG_ASSOCIATED_BFMER0_INFO_8822B+i), pBeamformerEntry->MacAddr[i]);

		/* Set partial AID */
		ODM_Write2Byte(pDM_Odm, (REG_ASSOCIATED_BFMER0_INFO_8822B+6), pBeamformerEntry->P_AID);

		/* Fill our AID to 0x1680[11:0] and [13:12] = 2b'00, BF report segment select to 3895 bytes*/
		u1bTmp = ODM_Read1Byte(pDM_Odm, 0x1680);
		u1bTmp = (pBeamformerEntry->AID)&0xFFF;
		ODM_Write1Byte(pDM_Odm, 0x1680, u1bTmp);

		/* Set 80us for leaving ndp_rx_standby_state */
		ODM_Write1Byte(pDM_Odm, 0x71B, 0x50);
		
		/* Set 0x6A0[14] = 1 to accept action_no_ack */
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_RXFLTMAP0_8822B+1);
		u1bTmp |= 0x40;
		ODM_Write1Byte(pDM_Odm, REG_RXFLTMAP0_8822B+1, u1bTmp);
		/* Set 0x6A2[5:4] = 1 to NDPA and BF report poll */
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_RXFLTMAP1_8822B);
		u1bTmp |= 0x30;
		ODM_Write1Byte(pDM_Odm, REG_RXFLTMAP1_8822B, u1bTmp);
		
		/*CSI report parameters of Beamformer*/
		Nc_index = halTxbf8822B_GetNrx(pDM_Odm);	/* Depend on RF type */
		Nr_index = 1;	/*0x718[7] = 1 use Nsts, 0x718[7] = 0 use reg setting. as Bfee, we use Nsts, so Nr_index don't care*/
		grouping = 0; /*no grouping*/
		codebookinfo = 1; /*7 bit for psi, 9 bit for phi*/
		coefficientsize = 0; /*This is nothing really matter*/ 
		CSI_Param = (u2Byte)((coefficientsize<<10)|(codebookinfo<<8)|(grouping<<6)|(Nr_index<<3)|(Nc_index));
		ODM_Write2Byte(pDM_Odm, 0x6F4, CSI_Param);

	}
	
	/*************MU BFee Entry Init*************/
	if ((pBeamformingInfo->beamformee_mu_cnt > 0) && (BFeeIdx < BEAMFORMEE_ENTRY_NUM)) {
		pBeamformeeEntry = &pBeamformingInfo->BeamformeeEntry[BFeeIdx];
		pBeamformeeEntry->is_mu_sta = TRUE;
		for (i = 0; i < MAX_BEAMFORMEE_MU; i++) {
			if ((pBeamformingInfo->beamformee_mu_reg_maping & BIT(i)) == 0) {
				pBeamformingInfo->beamformee_mu_reg_maping |= BIT(i);
				pBeamformeeEntry->mu_reg_index = i;
				break;
			}
		}

		if (pBeamformeeEntry->mu_reg_index == 0xFF) {
			/* There is no valid bit in beamformee_mu_reg_maping */
			RT_DISP(FBEAM, FBEAM_FUN, ("%s: ERROR! There is no valid bit in beamformee_mu_reg_maping!\n", __func__));
			return;
		}
		
		/*User position table*/
		switch (pBeamformeeEntry->mu_reg_index) {
		case 0:
			gid_valid = 0x7fe;
			user_position_l = 0x111110;
			user_position_h = 0x0;
			break;
		case 1:
			gid_valid = 0x7f806;
			user_position_l = 0x11000004;
			user_position_h = 0x11;
			break;
		case 2:
			gid_valid = 0x1f81818;
			user_position_l = 0x400040;
			user_position_h = 0x11100;
			break;
		case 3:
			gid_valid = 0x1e186060;
			user_position_l = 0x4000400;
			user_position_h = 0x1100040;
			break;
		case 4:
			gid_valid = 0x66618180;
			user_position_l = 0x40004000;
			user_position_h = 0x10040400;
			break;
		case 5:
			gid_valid = 0x79860600;
			user_position_l = 0x40000;
			user_position_h = 0x4404004;
			break;
		}

		for (i = 0; i < 8; i++) {
			if (i < 4) {
				pBeamformeeEntry->gid_valid[i] = (u1Byte)(gid_valid & 0xFF);
				gid_valid = (gid_valid >> 8);
			} else
				pBeamformeeEntry->gid_valid[i] = 0;
		}
		for (i = 0; i < 16; i++) {
			if (i < 4) {
				pBeamformeeEntry->user_position[i] = (u1Byte)(user_position_l & 0xFF);
				user_position_l = user_position_l >> 8;
			} else if (i < 8) {
				pBeamformeeEntry->user_position[i] = (u1Byte)(user_position_h & 0xFF);
				user_position_h = user_position_h >> 8;
			} else
				pBeamformeeEntry->user_position[i] = 0;
		}

		/*Sounding protocol control*/
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B, 0xDB);	

		/*select MU STA table*/
		pBeamformingInfo->RegMUTxCtrl &= ~(BIT8|BIT9|BIT10);
		pBeamformingInfo->RegMUTxCtrl |= (pBeamformeeEntry->mu_reg_index << 8)&(BIT8|BIT9|BIT10);
		ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);	
		
		ODM_SetBBReg(pDM_Odm, 0x14c4 , bMaskDWord, 0); /*Reset gid_valid table*/
		ODM_SetBBReg(pDM_Odm, 0x14c8 , bMaskDWord, user_position_l);
		ODM_SetBBReg(pDM_Odm, 0x14cc , bMaskDWord, user_position_h);

		/*set validity of MU STAs*/		
		pBeamformingInfo->RegMUTxCtrl &= 0xFFFFFFC0;
		pBeamformingInfo->RegMUTxCtrl |= pBeamformingInfo->beamformee_mu_reg_maping&0x3F;
		ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);	

		value16 = ODM_Read2Byte(pDM_Odm, mu_reg[pBeamformeeEntry->mu_reg_index]);
		value16 &= 0xFE00; /*Clear PAID*/
		value16 |= BIT9; /*Enable MU BFee*/
		value16 |= pBeamformeeEntry->P_AID;
		ODM_Write2Byte(pDM_Odm, mu_reg[pBeamformeeEntry->mu_reg_index] , value16);
		
		/* 0x42C[30] = 1 (0: from Tx desc, 1: from 0x45F) */
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3);
		u1bTmp |= 0xD0; /* Set bit 28, 30, 31 to 3b'111*/
		ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3, u1bTmp);
		/* Set NDPA to 6M*/
		ODM_Write1Byte(pDM_Odm, REG_NDPA_RATE_8822B, 0x4); /* 6M */

		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_NDPA_OPT_CTRL_8822B);
		u1bTmp &= 0xFC; /* Clear bit 0, 1*/
		ODM_Write1Byte(pDM_Odm, REG_NDPA_OPT_CTRL_8822B, u1bTmp);

		u4bTmp = ODM_Read4Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B);
		u4bTmp = ((u4bTmp & 0xFF0000FF) | 0x020200); /* Set [23:8] to 0x0202 */
		ODM_Write4Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B, u4bTmp);	

		/* Set 0x6A0[14] = 1 to accept action_no_ack */
		u1bTmp = ODM_Read1Byte(pDM_Odm, REG_RXFLTMAP0_8822B+1);
		u1bTmp |= 0x40;
		ODM_Write1Byte(pDM_Odm, REG_RXFLTMAP0_8822B+1, u1bTmp);
		/* End of MAC registers setting */
		
		halTxbf8822B_RfMode(pDM_Odm, pBeamformingInfo, BFeeIdx);
#if (SUPPORT_MU_BF == 1)
		/*Special for plugfest*/
		delay_ms(50); /* wait for 4-way handshake ending*/
		SendSWVHTGIDMgntFrame(pDM_Odm, pBeamformeeEntry->MacAddr, BFeeIdx);
#endif		

		phydm_Beamforming_Notify(pDM_Odm);

	}

}


VOID
HalTxbf8822B_Leave(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMER_ENTRY	pBeamformerEntry; 
	PRT_BEAMFORMEE_ENTRY	pBeamformeeEntry;
	u4Byte					mu_reg[6] = {0x1684, 0x1686, 0x1688, 0x168a, 0x168c, 0x168e};

	if (Idx < BEAMFORMER_ENTRY_NUM) {
		pBeamformerEntry = &pBeamformingInfo->BeamformerEntry[Idx];
		pBeamformeeEntry = &pBeamformingInfo->BeamformeeEntry[Idx];
	} else
		return;

	/*Clear P_AID of Beamformee*/
	/*Clear MAC address of Beamformer*/
	/*Clear Associated Bfmee Sel*/

	if (pBeamformerEntry->BeamformEntryCap == BEAMFORMING_CAP_NONE) {
		ODM_Write1Byte(pDM_Odm, REG_SND_PTCL_CTRL_8822B, 0xD8);	
		if (pBeamformerEntry->is_mu_ap == 0) { /*SU BFer */
			if (pBeamformerEntry->su_reg_index == 0) {	
				ODM_Write4Byte(pDM_Odm, REG_ASSOCIATED_BFMER0_INFO_8822B, 0);
				ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMER0_INFO_8822B+4, 0);
				ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8822B, 0);
			} else {
				ODM_Write4Byte(pDM_Odm, REG_ASSOCIATED_BFMER1_INFO_8822B, 0);
				ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMER1_INFO_8822B+4, 0);
				ODM_Write2Byte(pDM_Odm, REG_CSI_RPT_PARAM_BW20_8822B+2, 0);
			}
			pBeamformingInfo->beamformer_su_reg_maping &= ~(BIT(pBeamformerEntry->su_reg_index));
			pBeamformerEntry->su_reg_index = 0xFF;
		} else { /*MU BFer */
			/*set validity of MU STA0 and MU STA1*/
			pBeamformingInfo->RegMUTxCtrl &= 0xFFFFFFC0;
			ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
			
			ODM_Memory_Set(pDM_Odm, pBeamformerEntry->gid_valid, 0, 8);
			ODM_Memory_Set(pDM_Odm, pBeamformerEntry->user_position, 0, 16);
			pBeamformerEntry->is_mu_ap = FALSE;
		}
	}

	if (pBeamformeeEntry->BeamformEntryCap == BEAMFORMING_CAP_NONE) {
		halTxbf8822B_RfMode(pDM_Odm, pBeamformingInfo, Idx);
		if (pBeamformeeEntry->is_mu_sta == 0) { /*SU BFee*/
			if (pBeamformeeEntry->su_reg_index == 0) {	
				ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8822B, 0x0);	
				ODM_Write1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3, ODM_Read1Byte(pDM_Odm, REG_TXBF_CTRL_8822B+3)|BIT4|BIT6|BIT7);
				ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B, 0);
			} else {
				ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8822B+2, 0x0 | BIT14 | BIT15 | BIT12);

				ODM_Write2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B+2, 
				ODM_Read2Byte(pDM_Odm, REG_ASSOCIATED_BFMEE_SEL_8822B+2) & 0x60);
			}
			pBeamformingInfo->beamformee_su_reg_maping &= ~(BIT(pBeamformeeEntry->su_reg_index));
			pBeamformeeEntry->su_reg_index = 0xFF;
		} else { /*MU BFee */
			/*Disable sending NDPA & BF-rpt-poll to this BFee*/
			ODM_Write2Byte(pDM_Odm, mu_reg[pBeamformeeEntry->mu_reg_index] , 0);
			/*set validity of MU STA*/
			pBeamformingInfo->RegMUTxCtrl &= ~(BIT(pBeamformeeEntry->mu_reg_index));
			ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
			
			
			pBeamformeeEntry->is_mu_sta = FALSE;
			pBeamformingInfo->beamformee_mu_reg_maping &= ~(BIT(pBeamformeeEntry->mu_reg_index));
			pBeamformeeEntry->mu_reg_index = 0xFF;
		}
	}
}


/***********SU & MU BFee Entry Only when souding done****************/
VOID
HalTxbf8822B_Status(
	IN PVOID			pDM_VOID,
	IN u1Byte				Idx
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u2Byte					BeamCtrlVal, tmpVal;
	u4Byte					BeamCtrlReg;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMEE_ENTRY	pBeamformEntry;
	BOOLEAN	is_mu_sounding = pBeamformingInfo->is_mu_sounding, is_bitmap_ready = FALSE;
	u16 bitmap;
	u8 idx, gid, i;
	u8 id1, id0;
	u32 gid_valid[6] = {0};
	u32 user_position_lsb[6] = {0};
	u32 user_position_msb[6] = {0};
	u32 value32;

	if (Idx < BEAMFORMEE_ENTRY_NUM)
		pBeamformEntry = &pBeamformingInfo->BeamformeeEntry[Idx];
	else
		return;
	
	/*SU sounding done */
	if (is_mu_sounding == FALSE) {

		if (phydm_actingDetermine(pDM_Odm, PhyDM_ACTING_AS_IBSS))
			BeamCtrlVal = pBeamformEntry->MacId;
		else 
			BeamCtrlVal = pBeamformEntry->P_AID;

		ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s, BeamformEntry.BeamformEntryState = %d", __func__, pBeamformEntry->BeamformEntryState));

		if (pBeamformEntry->su_reg_index == 0) {
			BeamCtrlReg = REG_TXBF_CTRL_8822B;
		} else {
			BeamCtrlReg = REG_TXBF_CTRL_8822B+2;
			BeamCtrlVal |= BIT12|BIT14|BIT15;
		}

		if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			if (pBeamformEntry->SoundBW == CHANNEL_WIDTH_20)
				BeamCtrlVal |= BIT9;
			else if (pBeamformEntry->SoundBW == CHANNEL_WIDTH_40)
				BeamCtrlVal |= (BIT9|BIT10);
			else if (pBeamformEntry->SoundBW == CHANNEL_WIDTH_80)
				BeamCtrlVal |= (BIT9|BIT10|BIT11);		
		} else {
			ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("@%s, Don't apply Vmatrix",  __func__));
			BeamCtrlVal &= ~(BIT9|BIT10|BIT11);
		}

		ODM_Write2Byte(pDM_Odm, BeamCtrlReg, BeamCtrlVal);
		/*disable NDP packet use beamforming */
		tmpVal = ODM_Read2Byte(pDM_Odm, REG_TXBF_CTRL_8822B);
		ODM_Write2Byte(pDM_Odm, REG_TXBF_CTRL_8822B, tmpVal|BIT15);
	} else {
		/*MU sounding done */
		if (pBeamformEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSED) {
			/*value32 = ODM_GetBBReg(pDM_Odm, 0xF4C, 0xFFFF0000);*/
			value32 = 1;
			
			is_bitmap_ready = (BOOLEAN)((value32 & BIT15) >> 15);
			bitmap = (u16)(value32 & 0x3FFF);
		
			for (idx = 0; idx < 15; idx++) {
				if (idx < 5) {/*bit0~4*/
					id0 = 0;
					id1 = (u8)(idx + 1);
				} else if (idx < 9) { /*bit5~8*/
					id0 = 1;
					id1 = (u8)(idx - 3);
				} else if (idx < 12) { /*bit9~11*/
					id0 = 2;
					id1 = (u8)(idx - 6);
				} else if (idx < 14) { /*bit12~13*/	
					id0 = 3;
					id1 = (u8)(idx - 8);
				} else { /*bit14*/
					id0 = 4;
					id1 = (u8)(idx - 9);
				}
				if (bitmap & BIT(idx)) {
					/*Pair 1*/
					gid = (idx << 1) + 1;
					gid_valid[id0] |= (BIT(gid));
					gid_valid[id1] |= (BIT(gid));
					/*Pair 2*/
					gid += 1;
					gid_valid[id0] |= (BIT(gid));
					gid_valid[id1] |= (BIT(gid));
				} else {
					/*Pair 1*/
					gid = (idx << 1) + 1;
					gid_valid[id0] &= ~(BIT(gid));
					gid_valid[id1] &= ~(BIT(gid));
					/*Pair 2*/
					gid += 1;
					gid_valid[id0] &= ~(BIT(gid));
					gid_valid[id1] &= ~(BIT(gid));
				}
			}

			for (i = 0; i < BEAMFORMEE_ENTRY_NUM; i++) {
				pBeamformEntry = &pBeamformingInfo->BeamformeeEntry[i];
				if ((pBeamformEntry->is_mu_sta) && (pBeamformEntry->mu_reg_index < 6)) {
					value32 = gid_valid[pBeamformEntry->mu_reg_index];
					for (idx = 0; idx < 4; idx++) {
						pBeamformEntry->gid_valid[idx] = (u8)(value32 & 0xFF);
						value32 = (value32 >> 8);
					}
				}
			}

			for (idx = 0; idx < 6; idx++) {
				pBeamformingInfo->RegMUTxCtrl |= ~(BIT8|BIT9|BIT10);
				pBeamformingInfo->RegMUTxCtrl |= ((idx<<8)&(BIT8|BIT9|BIT10));
				ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
				ODM_SetMACReg(pDM_Odm, 0x14C4, bMaskDWord, gid_valid[idx]); /*set MU STA gid valid table*/
			}

			/*Enable TxMU PPDU*/
			pBeamformingInfo->RegMUTxCtrl |= BIT7;
			ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
		}
	}
}

/*Only used for MU BFer Entry when get GID management frame (self is as MU STA)*/
VOID
HalTxbf8822B_ConfigGtab(
	IN PVOID			pDM_VOID
	)
{
	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PRT_BEAMFORMING_INFO	pBeamformingInfo = &pDM_Odm->BeamformingInfo;
	PRT_BEAMFORMER_ENTRY	pBeamformerEntry = NULL;
	u4Byte		gid_valid = 0, user_position_l = 0, user_position_h = 0, i;

	if (pBeamformingInfo->mu_ap_index < BEAMFORMER_ENTRY_NUM)
		pBeamformerEntry = &pBeamformingInfo->BeamformerEntry[pBeamformingInfo->mu_ap_index];
	else
		return;

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s==>\n", __func__));

	/*For GID 0~31*/
	for (i = 0; i < 4; i++)
		gid_valid |= (pBeamformerEntry->gid_valid[i] << (i<<3));
	for (i = 0; i < 8; i++) {
		if (i < 4)
			user_position_l |= (pBeamformerEntry->user_position[i] << (i << 3));
		else
			user_position_h |= (pBeamformerEntry->user_position[i] << ((i - 4)<<3));
	}
	/*select MU STA0 table*/
	pBeamformingInfo->RegMUTxCtrl &= ~(BIT8|BIT9|BIT10);
	ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
	ODM_SetBBReg(pDM_Odm, 0x14c4, bMaskDWord, gid_valid); 
	ODM_SetBBReg(pDM_Odm, 0x14c8, bMaskDWord, user_position_l);
	ODM_SetBBReg(pDM_Odm, 0x14cc, bMaskDWord, user_position_h);

	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: STA0: gid_valid = 0x%x, user_position_l = 0x%x, user_position_h = 0x%x\n",
		__func__, gid_valid, user_position_l, user_position_h));

	gid_valid = 0;
	user_position_l = 0;
	user_position_h = 0;

	/*For GID 32~64*/
	for (i = 4; i < 8; i++)
		gid_valid |= (pBeamformerEntry->gid_valid[i] << ((i - 4)<<3));
	for (i = 8; i < 16; i++) {
		if (i < 4)
			user_position_l |= (pBeamformerEntry->user_position[i] << ((i - 8) << 3));
		else
			user_position_h |= (pBeamformerEntry->user_position[i] << ((i - 12) << 3));
	}
	/*select MU STA1 table*/
	pBeamformingInfo->RegMUTxCtrl &= ~(BIT8|BIT9|BIT10);
	pBeamformingInfo->RegMUTxCtrl |= BIT8;
	ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
	ODM_SetBBReg(pDM_Odm, 0x14c4, bMaskDWord, gid_valid); 
	ODM_SetBBReg(pDM_Odm, 0x14c8, bMaskDWord, user_position_l);
	ODM_SetBBReg(pDM_Odm, 0x14cc, bMaskDWord, user_position_h);
	
	ODM_RT_TRACE(pDM_Odm, PHYDM_COMP_TXBF, ODM_DBG_LOUD, ("%s: STA1: gid_valid = 0x%x, user_position_l = 0x%x, user_position_h = 0x%x\n",
		__func__, gid_valid, user_position_l, user_position_h));

	/* Set validity of MU STA0 and MU STA1*/
	pBeamformingInfo->RegMUTxCtrl &= 0xFFFFFFC0;
	pBeamformingInfo->RegMUTxCtrl |= 0x3; /* STA0, STA1*/
	ODM_Write4Byte(pDM_Odm, 0x14c0, pBeamformingInfo->RegMUTxCtrl);
	
}



#if 0
/*This function translate the bitmap to GTAB*/
VOID
haltxbf8822b_gtab_translation(
	IN PDM_ODM_T			pDM_Odm
) 
{
	u8 idx, gid;
	u8 id1, id0;
	u32 gid_valid[6] = {0};
	u32 user_position_lsb[6] = {0};
	u32 user_position_msb[6] = {0};
	
	for (idx = 0; idx < 15; idx++) {
		if (idx < 5) {/*bit0~4*/
			id0 = 0;
			id1 = (u8)(idx + 1);
		} else if (idx < 9) { /*bit5~8*/
			id0 = 1;
			id1 = (u8)(idx - 3);
		} else if (idx < 12) { /*bit9~11*/
			id0 = 2;
			id1 = (u8)(idx - 6);
		} else if (idx < 14) { /*bit12~13*/	
			id0 = 3;
			id1 = (u8)(idx - 8);
		} else { /*bit14*/
			id0 = 4;
			id1 = (u8)(idx - 9);
		}

		/*Pair 1*/
		gid = (idx << 1) + 1;
		gid_valid[id0] |= (1 << gid);
		gid_valid[id1] |= (1 << gid);
		if (gid < 16) {
			/*user_position_lsb[id0] |= (0 << (gid << 1));*/
			user_position_lsb[id1] |= (1 << (gid << 1));
		} else {
			/*user_position_msb[id0] |= (0 << ((gid - 16) << 1));*/
			user_position_msb[id1] |= (1 << ((gid - 16) << 1));
		}
		
		/*Pair 2*/
		gid += 1;
		gid_valid[id0] |= (1 << gid);
		gid_valid[id1] |= (1 << gid);
		if (gid < 16) {
			user_position_lsb[id0] |= (1 << (gid << 1));
			/*user_position_lsb[id1] |= (0 << (gid << 1));*/
		} else {
			user_position_msb[id0] |= (1 << ((gid - 16) << 1));
			/*user_position_msb[id1] |= (0 << ((gid - 16) << 1));*/
		}

	}


	for (idx = 0; idx < 6; idx++) {
		/*DbgPrint("gid_valid[%d] = 0x%x\n", idx, gid_valid[idx]);
		DbgPrint("user_position[%d] = 0x%x   %x\n", idx, user_position_msb[idx], user_position_lsb[idx]);*/
	}
}
#endif

VOID
HalTxbf8822B_FwTxBF(
	IN PVOID			pDM_VOID,
	IN	u1Byte				Idx
	)
{
#if 0
	PRT_BEAMFORMING_INFO	pBeamInfo = GET_BEAMFORM_INFO(Adapter);
	PRT_BEAMFORMEE_ENTRY	pBeamEntry = pBeamInfo->BeamformeeEntry+Idx;

	if (pBeamEntry->BeamformEntryState == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		halTxbf8822B_DownloadNDPA(Adapter, Idx);

	halTxbf8822B_FwTxBFCmd(Adapter);
#endif
}

#else	/* (RTL8822B_SUPPORT == 1)*/

#endif	/* (RTL8822B_SUPPORT == 1)*/

#endif /*(BEAMFORMING_SUPPORT == 1)*/

