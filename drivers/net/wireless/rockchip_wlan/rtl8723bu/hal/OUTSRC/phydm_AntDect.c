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
// include files
//============================================================

#include "Mp_Precomp.h"
#include "phydm_precomp.h"

//#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN |ODM_CE))
#if(defined(CONFIG_ANT_DETECTION))

//IS_ANT_DETECT_SUPPORT_SINGLE_TONE(Adapter)	
//IS_ANT_DETECT_SUPPORT_RSSI(Adapter)		
//IS_ANT_DETECT_SUPPORT_PSD(Adapter)

//1 [1. Single Tone Method] ===================================================


VOID
odm_PHY_SaveAFERegisters(
	IN	PVOID		pDM_VOID,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegisterNum
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte	i;
	
	//RT_DISP(FINIT, INIT_IQK, ("Save ADDA parameters.\n"));
	for( i = 0 ; i < RegisterNum ; i++){
		AFEBackup[i] = ODM_GetBBReg(pDM_Odm, AFEReg[i], bMaskDWord);
	}
}

VOID
odm_PHY_ReloadAFERegisters(
	IN	PVOID		pDM_VOID,
	IN	pu4Byte		AFEReg,
	IN	pu4Byte		AFEBackup,
	IN	u4Byte		RegiesterNum
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte	i;

	//RT_DISP(FINIT, INIT_IQK, ("Reload ADDA power saving parameters !\n"));
	for(i = 0 ; i < RegiesterNum; i++)
	{
	
		ODM_SetBBReg(pDM_Odm, AFEReg[i], bMaskDWord, AFEBackup[i]);
	}
}

//
// Description:
//	Set Single/Dual Antenna default setting for products that do not do detection in advance.
//
// Added by Joseph, 2012.03.22
//
VOID
ODM_SingleDualAntennaDefaultSetting(
	IN		PVOID		pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;

	u1Byte btAntNum=BT_GetPgAntNum(pAdapter);
	// Set default antenna A and B status
	if(btAntNum == 2)
	{
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=TRUE;
	
	}
	else if(btAntNum == 1)
	{// Set antenna A as default
		pDM_SWAT_Table->ANTA_ON=TRUE;
		pDM_SWAT_Table->ANTB_ON=FALSE;
	
	}
	else
	{
		RT_ASSERT(FALSE, ("Incorrect antenna number!!\n"));
	}
}


//2 8723A ANT DETECT
//
// Description:
//	Implement IQK single tone for RF DPK loopback and BB PSD scanning. 
//	This function is cooperated with BB team Neil. 
//
// Added by Roger, 2011.12.15
//
BOOLEAN
ODM_SingleDualAntennaDetection(
	IN		PVOID		pDM_VOID,
	IN		u1Byte			mode
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u4Byte		CurrentChannel,RfLoopReg;
	u1Byte		n;
	u4Byte		Reg88c, Regc08, Reg874, Regc50, Reg948, Regb2c, Reg92c, Reg930, Reg064, AFE_rRx_Wait_CCA;
	u1Byte		initial_gain = 0x5a;
	u4Byte		PSD_report_tmp;
	u4Byte		AntA_report = 0x0, AntB_report = 0x0,AntO_report=0x0,temp;
	BOOLEAN		bResult = TRUE;
	u4Byte		AFE_Backup[16];
	u4Byte		AFE_REG_8723A[16] = {
					rRx_Wait_CCA, 	rTx_CCK_RFON, 
					rTx_CCK_BBON, 	rTx_OFDM_RFON,
					rTx_OFDM_BBON, 	rTx_To_Rx,
					rTx_To_Tx, 		rRx_CCK, 
					rRx_OFDM, 		rRx_Wait_RIFS, 
					rRx_TO_Rx,		rStandby,
					rSleep,			rPMPD_ANAEN, 	
					rFPGA0_XCD_SwitchControl, rBlue_Tooth};

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection()============> \n"));	

	
	if(!(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C|ODM_RTL8723B)))
		return bResult;

	// Retrieve antenna detection registry info, added by Roger, 2012.11.27.
	if(!IS_ANT_DETECT_SUPPORT_SINGLE_TONE(pAdapter))
		return bResult;

	if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		//Which path in ADC/DAC is turnned on for PSD: both I/Q
		ODM_SetBBReg(pDM_Odm, 0x808, BIT10|BIT11, 0x3);
		//Ageraged number: 8
		ODM_SetBBReg(pDM_Odm, 0x808, BIT12|BIT13, 0x1);
		//pts = 128;
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
	}

	//1 Backup Current RF/BB Settings	
	
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask);
	RfLoopReg = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask);
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		Reg92c = ODM_GetBBReg(pDM_Odm, rDPDT_control, bMaskDWord);	
		Reg930 = ODM_GetBBReg(pDM_Odm, rfe_ctrl_anta_src, bMaskDWord);
		Reg948 = ODM_GetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord);
		Regb2c = ODM_GetBBReg(pDM_Odm, rAGC_table_select, bMaskDWord);
		Reg064 = ODM_GetMACReg(pDM_Odm, rSYM_WLBT_PAPE_SEL, BIT29);
		ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x1);
		ODM_SetBBReg(pDM_Odm, rfe_ctrl_anta_src, 0xff, 0x77);
		ODM_SetMACReg(pDM_Odm, rSYM_WLBT_PAPE_SEL, BIT29, 0x1);  //dbg 7
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0x3c0, 0x0);//dbg 8
		ODM_SetBBReg(pDM_Odm, rAGC_table_select, BIT31, 0x0);
	}

	ODM_StallExecution(10);
	
	//Store A Path Register 88c, c08, 874, c50
	Reg88c = ODM_GetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord);
	Regc08 = ODM_GetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord);
	Reg874 = ODM_GetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord);
	Regc50 = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord);	
	
	// Store AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	odm_PHY_SaveAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		AFE_rRx_Wait_CCA = ODM_GetBBReg(pDM_Odm, rRx_Wait_CCA,bMaskDWord);
	
	//Set PSD 128 pts
	ODM_SetBBReg(pDM_Odm, rFPGA0_PSDFunction, BIT14|BIT15, 0x0);  //128 pts
	
	// To SET CH1 to do
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask, 0x7401);     //Channel 1
	
	// AFE all on step
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_CCK_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_RFON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_OFDM_BBON, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rTx_To_Tx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_CCK, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_OFDM, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_Wait_RIFS, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rRx_TO_Rx, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rStandby, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rSleep, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rPMPD_ANAEN, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_SwitchControl, bMaskDWord, 0x6FDB25A4);
		ODM_SetBBReg(pDM_Odm, rBlue_Tooth, bMaskDWord, 0x6FDB25A4);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, 0x01c00016);
	}

	// 3 wire Disable
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, 0xCCF000C0);
	
	//BB IQK Setting
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, 0x000800E4);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, 0x22208000);

	//IQK setting tone@ 4.34Mhz
	ODM_SetBBReg(pDM_Odm, rTx_IQK_Tone_A, bMaskDWord, 0x10008C1C);
	ODM_SetBBReg(pDM_Odm, rTx_IQK, bMaskDWord, 0x01007c00);	

	//Page B init
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x00080000);
	ODM_SetBBReg(pDM_Odm, rConfig_AntA, bMaskDWord, 0x0f600000);
	ODM_SetBBReg(pDM_Odm, rRx_IQK, bMaskDWord, 0x01004800);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_Tone_A, bMaskDWord, 0x10008c1f);
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	{
	ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82150008);
	ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28150008);
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_SetBBReg(pDM_Odm, rTx_IQK_PI_A, bMaskDWord, 0x82150016);
		ODM_SetBBReg(pDM_Odm, rRx_IQK_PI_A, bMaskDWord, 0x28150016);
	}
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Rsp, bMaskDWord, 0x001028d0);	
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, 0x7f, initial_gain);

	//RF loop Setting
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x0, 0xFFFFF, 0x50008);	
	
	//IQK Single tone start
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x808000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf9000000);
	ODM_SetBBReg(pDM_Odm, rIQK_AGC_Pts, bMaskDWord, 0xf8000000);
	
	ODM_StallExecution(10000);

	// PSD report of antenna A
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp >AntA_report)
			AntA_report=PSD_report_tmp;
	}

	 // change to Antenna B
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_B); 
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		//ODM_SetBBReg(pDM_Odm, rDPDT_control, 0x3, 0x2);
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0xfff, 0x280);
		ODM_SetBBReg(pDM_Odm, rAGC_table_select, BIT31, 0x1);
	}

	ODM_StallExecution(10);	

	// PSD report of antenna B
	PSD_report_tmp=0x0;
	for (n=0;n<2;n++)
 	{
 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
		if(PSD_report_tmp > AntB_report)
			AntB_report=PSD_report_tmp;
	}

	// change to open case
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
	{
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, 0);  // change to Antenna A

		ODM_StallExecution(10);	
		
		// PSD report of open case
		PSD_report_tmp=0x0;
		for (n=0;n<2;n++)
	 	{
	 		PSD_report_tmp =  GetPSDData(pDM_Odm, 14, initial_gain);	
			if(PSD_report_tmp > AntO_report)
				AntO_report=PSD_report_tmp;
		}
	}
	//Close IQK Single Tone function
	ODM_SetBBReg(pDM_Odm, rFPGA0_IQK, 0xffffff00, 0x000000);

	//1 Return to antanna A
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		ODM_SetBBReg(pDM_Odm, rFPGA0_XA_RFInterfaceOE, ODM_DPDT, Antenna_A);  // change to Antenna A
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		// external DPDT
		ODM_SetBBReg(pDM_Odm, rDPDT_control, bMaskDWord, Reg92c);

		//internal S0/S1
		ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord, Reg948);
		ODM_SetBBReg(pDM_Odm, rAGC_table_select, bMaskDWord, Regb2c);
		ODM_SetBBReg(pDM_Odm, rfe_ctrl_anta_src, bMaskDWord, Reg930);
		ODM_SetMACReg(pDM_Odm, rSYM_WLBT_PAPE_SEL, BIT29, Reg064);
	}
	
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, bMaskDWord, Reg88c);
	ODM_SetBBReg(pDM_Odm, rOFDM0_TRMuxPar, bMaskDWord, Regc08);
	ODM_SetBBReg(pDM_Odm, rFPGA0_XCD_RFInterfaceSW, bMaskDWord, Reg874);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, 0x7F, 0x40);
	ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord, Regc50);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,CurrentChannel);
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x00, bRFRegOffsetMask,RfLoopReg);

	//Reload AFE Registers
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8192C))
		odm_PHY_ReloadAFERegisters(pDM_Odm, AFE_REG_8723A, AFE_Backup, 16);	
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ODM_SetBBReg(pDM_Odm, rRx_Wait_CCA, bMaskDWord, AFE_rRx_Wait_CCA);

	if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		//2 Test Ant B based on Ant A is ON
		if(mode==ANTTESTB)
		{
			if(AntA_report >=	100)
			{
				if(AntB_report > (AntA_report+1))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));		
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Dual Antenna is A and B\n"));	
				}	
			}
			else
			{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
				pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
				bResult = FALSE;
			}
		}	
		//2 Test Ant A and B based on DPDT Open
		else if(mode==ANTTESTALL)
		{
			if((AntO_report >=100) && (AntO_report <=118))
			{
				if(AntA_report > (AntO_report+1))
				{
					pDM_SWAT_Table->ANTA_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTA_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant A is ON\n"));
				}

				if(AntB_report > (AntO_report+2))
				{
					pDM_SWAT_Table->ANTB_ON=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is OFF\n"));
				}	
				else
				{
					pDM_SWAT_Table->ANTB_ON=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Ant B is ON\n"));
				}
				
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_O[%d]= %d \n", 2416, AntO_report));
				
				pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
				pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
				pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
				pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
				}
			else
				{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
				bResult = FALSE;
			}
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192C)
	{
		if(AntA_report >=	100)
		{
			if(AntB_report > (AntA_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=FALSE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_B);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna B\n"));		
			}	
			else if(AntA_report > (AntB_report+2))
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=FALSE;
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, 0x300, Antenna_A);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Single Antenna A\n"));
			}	
			else
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
			}
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
			pDM_SWAT_Table->ANTA_ON=TRUE; // Set Antenna A on as default 
			pDM_SWAT_Table->ANTB_ON=FALSE; // Set Antenna B off as default 
			bResult = FALSE;
		}
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_A[%d]= %d \n", 2416, AntA_report));	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("psd_report_B[%d]= %d \n", 2416, AntB_report));	
		
		//2 Test Ant B based on Ant A is ON
		if((AntA_report >= 100) && (AntB_report >= 100) && (AntA_report <= 135) && (AntB_report <= 135))
		{
			u1Byte TH1=2, TH2=6;
		
			if((AntA_report - AntB_report < TH1) || (AntB_report - AntA_report < TH1))
			{
				pDM_SWAT_Table->ANTA_ON=TRUE;
				pDM_SWAT_Table->ANTB_ON=TRUE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Dual Antenna\n"));
			}
			else if(((AntA_report - AntB_report >= TH1) && (AntA_report - AntB_report <= TH2)) || 
				((AntB_report - AntA_report >= TH1) && (AntB_report - AntA_report <= TH2)))
			{
				pDM_SWAT_Table->ANTA_ON=FALSE;
				pDM_SWAT_Table->ANTB_ON=FALSE;
				bResult = FALSE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection(): Need to check again\n"));
			}
			else
			{
				pDM_SWAT_Table->ANTA_ON = TRUE;
				pDM_SWAT_Table->ANTB_ON=FALSE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SingleDualAntennaDetection(): Single Antenna\n"));
			}
			pDM_Odm->AntDetectedInfo.bAntDetected= TRUE;
			pDM_Odm->AntDetectedInfo.dBForAntA = AntA_report;
			pDM_Odm->AntDetectedInfo.dBForAntB = AntB_report;
			pDM_Odm->AntDetectedInfo.dBForAntO = AntO_report;
				
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("return FALSE!!\n"));
			bResult = FALSE;
		}
	}
	return bResult;

}



//1 [2. Scan AP RSSI Method] ==================================================




BOOLEAN
ODM_SwAntDivCheckBeforeLink(
	IN		PVOID		pDM_VOID
	)
{

#if (RT_MEM_SIZE_LEVEL != RT_MEM_SIZE_MINIMUM)

	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	s1Byte			Score = 0;
	PRT_WLAN_BSS	pTmpBssDesc, pTestBssDesc;
	u1Byte 			power_target = 10, power_target_L = 9, power_target_H = 16;
	u1Byte			tmp_power_diff = 0,power_diff = 0,avg_power_diff = 0,max_power_diff = 0,min_power_diff = 0xff;
	u2Byte			index, counter = 0;
	static u1Byte		ScanChannel;
	u8Byte			tStamp_diff = 0;		
	u4Byte			tmp_SWAS_NoLink_BK_Reg948;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ANTA_ON = (( %d )) , ANTB_ON = (( %d )) \n",pDM_Odm->DM_SWAT_Table.ANTA_ON ,pDM_Odm->DM_SWAT_Table.ANTB_ON ));

	//if(HP id)
	{
		if(pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult==TRUE && pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("8723B RSSI-based Antenna Detection is done\n"));
			return FALSE;
		}
		
		if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			if(pDM_SWAT_Table->SWAS_NoLink_BK_Reg948 == 0xff)
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg948 = ODM_Read4Byte(pDM_Odm, rS0S1_PathSwitch );
		}
	}

	if (pDM_Odm->Adapter == NULL)  //For BSOD when plug/unplug fast.  //By YJ,120413
	{	// The ODM structure is not initialized.
		return FALSE;
	}

	// Retrieve antenna detection registry info, added by Roger, 2012.11.27.
	if(!IS_ANT_DETECT_SUPPORT_RSSI(Adapter))
	{
		return FALSE;
	}
	else
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Antenna Detection: RSSI Method\n"));	
	}

	// Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF.
	PlatformAcquireSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	if(pHalData->eRFPowerState!=eRfOn || pMgntInfo->RFChangeInProgress || pMgntInfo->bMediaConnect)
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
				("ODM_SwAntDivCheckBeforeLink(): RFChangeInProgress(%x), eRFPowerState(%x)\n", 
				pMgntInfo->RFChangeInProgress, pHalData->eRFPowerState));
	
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		
		return FALSE;
	}
	else
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("pDM_SWAT_Table->SWAS_NoLink_State = %d\n", pDM_SWAT_Table->SWAS_NoLink_State));
	//1 Run AntDiv mechanism "Before Link" part.
	if(pDM_SWAT_Table->SWAS_NoLink_State == 0)
	{
		//1 Prepare to do Scan again to check current antenna state.

		// Set check state to next step.
		pDM_SWAT_Table->SWAS_NoLink_State = 1;
	
		// Copy Current Scan list.
		pMgntInfo->tmpNumBssDesc = pMgntInfo->NumBssDesc;
		PlatformMoveMemory((PVOID)Adapter->MgntInfo.tmpbssDesc, (PVOID)pMgntInfo->bssDesc, sizeof(RT_WLAN_BSS)*MAX_BSS_DESC);
		
		// Go back to scan function again.
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Scan one more time\n"));
		pMgntInfo->ScanStep=0;
		pMgntInfo->bScanAntDetect = TRUE;
		ScanChannel = odm_SwAntDivSelectScanChnl(Adapter);

		
		if(pDM_Odm->SupportICType & (ODM_RTL8188E|ODM_RTL8821))
		{
			if(pDM_FatTable->RxIdleAnt == MAIN_ANT)
				ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
			else
				ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			if(ScanChannel == 0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
					("ODM_SwAntDivCheckBeforeLink(): No AP List Avaiable, Using Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"AUX_ANT":"MAIN_ANT"));

				if(IS_5G_WIRELESS_MODE(pMgntInfo->dot11CurrentWirelessMode))
				{
					pDM_SWAT_Table->Ant5G = pDM_FatTable->RxIdleAnt;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant5G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
				}
				else
				{
					pDM_SWAT_Table->Ant2G = pDM_FatTable->RxIdleAnt;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant2G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
				}
				return FALSE;
			}

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
				("ODM_SwAntDivCheckBeforeLink: Change to %s for testing.\n", ((pDM_FatTable->RxIdleAnt == MAIN_ANT)?"MAIN_ANT":"AUX_ANT")));
		}
		else if(pDM_Odm->SupportICType & (ODM_RTL8192C|ODM_RTL8723B))
		{
			if(pDM_Odm->SupportICType == ODM_RTL8192C)
			{
			// Switch Antenna to another one.
			pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
			pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?AUX_ANT:MAIN_ANT;
			
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
				ODM_SetBBReg(pDM_Odm,  rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
			}
			else if(pDM_Odm->SupportICType == ODM_RTL8723B)
			{
				// Switch Antenna to another one.
				
				tmp_SWAS_NoLink_BK_Reg948 = ODM_Read4Byte(pDM_Odm, rS0S1_PathSwitch );
				
				if( (pDM_SWAT_Table->CurAntenna = MAIN_ANT) && (tmp_SWAS_NoLink_BK_Reg948==0x200))
				{
					ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0xfff, 0x280);
					ODM_SetBBReg(pDM_Odm, rAGC_table_select, BIT31, 0x1);
					pDM_SWAT_Table->CurAntenna = AUX_ANT;
			}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Reg[948]= (( %x )) was in wrong state\n", tmp_SWAS_NoLink_BK_Reg948 ));
					return FALSE;
				}
				ODM_StallExecution(10);
		
			}
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Change to (( %s-ant))  for testing.\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"MAIN":"AUX"));
		}
		
		odm_SwAntDivConstructScanChnl(Adapter, ScanChannel);
		PlatformSetTimer(Adapter, &pMgntInfo->ScanTimer, 5);

		return TRUE;
	}
	else //pDM_SWAT_Table->SWAS_NoLink_State == 1
	{
		//1 ScanComple() is called after antenna swiched.
		//1 Check scan result and determine which antenna is going
		//1 to be used.

		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,(" tmpNumBssDesc= (( %d )) \n",pMgntInfo->tmpNumBssDesc));// debug for Dino
		
		for(index = 0; index < pMgntInfo->tmpNumBssDesc; index++)
		{
			pTmpBssDesc = &(pMgntInfo->tmpbssDesc[index]); // Antenna 1
			pTestBssDesc = &(pMgntInfo->bssDesc[index]); // Antenna 2

			if(PlatformCompareMemory(pTestBssDesc->bdBssIdBuf, pTmpBssDesc->bdBssIdBuf, 6)!=0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): ERROR!! This shall not happen.\n"));
				continue;
			}

			if(pDM_Odm->SupportICType != ODM_RTL8723B)
			{
				if(pTmpBssDesc->ChannelNumber == ScanChannel)
				{
			if(pTmpBssDesc->RecvSignalPower > pTestBssDesc->RecvSignalPower)
			{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Compare scan entry: Score++\n"));
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
			
				Score++;
				PlatformMoveMemory(pTestBssDesc, pTmpBssDesc, sizeof(RT_WLAN_BSS));
			}
			else if(pTmpBssDesc->RecvSignalPower < pTestBssDesc->RecvSignalPower)
			{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink: Compare scan entry: Score--\n"));
						RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
				Score--;
			}
					else
					{
						if(pTestBssDesc->bdTstamp - pTmpBssDesc->bdTstamp < 5000)
						{
							RT_PRINT_STR(COMP_SCAN, DBG_WARNING, "GetScanInfo(): new Bss SSID:", pTmpBssDesc->bdSsIdBuf, pTmpBssDesc->bdSsIdLen);
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("at ch %d, Original: %d, Test: %d\n", pTmpBssDesc->ChannelNumber, pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("The 2nd Antenna didn't get this AP\n\n"));
						}
					}
				}
			}
			else // 8723B
			{ 
				if(pTmpBssDesc->ChannelNumber == ScanChannel)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ChannelNumber == ScanChannel -> (( %d )) \n", pTmpBssDesc->ChannelNumber ));
				
					if(pTmpBssDesc->RecvSignalPower > pTestBssDesc->RecvSignalPower) // Pow(Ant1) > Pow(Ant2)
					{
						counter++;
						tmp_power_diff=(u1Byte)(pTmpBssDesc->RecvSignalPower - pTestBssDesc->RecvSignalPower);
						power_diff = power_diff + tmp_power_diff;	
						
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf);

						//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("tmp_power_diff: (( %d)),max_power_diff: (( %d)),min_power_diff: (( %d))  \n", tmp_power_diff,max_power_diff,min_power_diff));
						if(tmp_power_diff > max_power_diff)
							max_power_diff=tmp_power_diff;
						if(tmp_power_diff < min_power_diff)
							min_power_diff=tmp_power_diff;
						//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("max_power_diff: (( %d)),min_power_diff: (( %d))  \n",max_power_diff,min_power_diff));
						
						PlatformMoveMemory(pTestBssDesc, pTmpBssDesc, sizeof(RT_WLAN_BSS));
					}
					else if(pTestBssDesc->RecvSignalPower > pTmpBssDesc->RecvSignalPower) // Pow(Ant1) < Pow(Ant2)
					{
						counter++;
						tmp_power_diff=(u1Byte)(pTestBssDesc->RecvSignalPower - pTmpBssDesc->RecvSignalPower);
						power_diff = power_diff + tmp_power_diff;						
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
						ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf);							
						if(tmp_power_diff > max_power_diff)
							max_power_diff=tmp_power_diff;
						if(tmp_power_diff < min_power_diff)
							min_power_diff=tmp_power_diff;							
					}
					else // Pow(Ant1) = Pow(Ant2)
					{
						if(pTestBssDesc->bdTstamp > pTmpBssDesc->bdTstamp) //  Stamp(Ant1) < Stamp(Ant2) 
					{
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("time_diff: %lld\n", (pTestBssDesc->bdTstamp-pTmpBssDesc->bdTstamp)/1000));
						if(pTestBssDesc->bdTstamp - pTmpBssDesc->bdTstamp > 5000)
						{
							counter++;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
							ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("SSID:"), pTmpBssDesc->bdSsIdBuf);
								ODM_PRINT_ADDR(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("BSSID:"), pTmpBssDesc->bdBssIdBuf);
								min_power_diff = 0;
						}
					}
						else
						{
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Error !!!]: Time_diff: %lld\n", (pTestBssDesc->bdTstamp-pTmpBssDesc->bdTstamp)/1000));
						}
				}
			}
		}
		}

		if(pDM_Odm->SupportICType & (ODM_RTL8188E|ODM_RTL8821))
		{
			if(pMgntInfo->NumBssDesc!=0 && Score<0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
							("ODM_SwAntDivCheckBeforeLink(): Using Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
						("ODM_SwAntDivCheckBeforeLink(): Remain Ant(%s)\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"AUX_ANT":"MAIN_ANT"));

				if(pDM_FatTable->RxIdleAnt == MAIN_ANT)
					ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
				else
					ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			}
			
			if(IS_5G_WIRELESS_MODE(pMgntInfo->dot11CurrentWirelessMode))
			{
				pDM_SWAT_Table->Ant5G = pDM_FatTable->RxIdleAnt;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant5G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
			else
			{
				pDM_SWAT_Table->Ant2G = pDM_FatTable->RxIdleAnt;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_SWAT_Table->Ant2G=%s\n", (pDM_FatTable->RxIdleAnt==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
			}
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		{
			if(counter == 0)
			{	
				if(pDM_Odm->DM_SWAT_Table.Pre_Aux_FailDetec == FALSE)
				{
					pDM_Odm->DM_SWAT_Table.Pre_Aux_FailDetec = TRUE;
					pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=FALSE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Counter=(( 0 )) , [[ Cannot find any AP with Aux-ant ]] ->  Scan Target-channel again  \n"));	

                                        //3 [ Scan again ]
					odm_SwAntDivConstructScanChnl(Adapter, ScanChannel);
					PlatformSetTimer(Adapter, &pMgntInfo->ScanTimer, 5);
					return TRUE;
				}
				else// Pre_Aux_FailDetec == TRUE
				{
					//2 [ Single Antenna ]
					pDM_Odm->DM_SWAT_Table.Pre_Aux_FailDetec = FALSE;
					pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=TRUE;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Counter=(( 0 )) , [[  Still cannot find any AP ]] \n"));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): Single antenna\n"));	
				}
				pDM_Odm->DM_SWAT_Table.Aux_FailDetec_Counter++;
			}
			else
			{
				pDM_Odm->DM_SWAT_Table.Pre_Aux_FailDetec = FALSE;
				
				if(counter==3)
				{
					avg_power_diff = ((power_diff-max_power_diff - min_power_diff)>>1)+ ((max_power_diff + min_power_diff)>>2);
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("counter: (( %d )) ,  power_diff: (( %d )) \n", counter, power_diff));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ counter==3 ] Modified avg_power_diff: (( %d )) , max_power_diff: (( %d )) ,  min_power_diff: (( %d )) \n", avg_power_diff,max_power_diff, min_power_diff));
				}
				else if(counter>=4)
				{
					avg_power_diff=(power_diff-max_power_diff - min_power_diff) / (counter - 2);
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("counter: (( %d )) ,  power_diff: (( %d )) \n", counter, power_diff));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ counter>=4 ] Modified avg_power_diff: (( %d )) , max_power_diff: (( %d )) ,  min_power_diff: (( %d )) \n", avg_power_diff,max_power_diff, min_power_diff));
					
				}
				else//counter==1,2
				{
					avg_power_diff=power_diff/counter;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("avg_power_diff: (( %d )) , counter: (( %d )) ,  power_diff: (( %d )) \n", avg_power_diff,counter, power_diff));
				}

				//2 [ Retry ]
				if( (avg_power_diff >=power_target_L) && (avg_power_diff <=power_target_H)  )
				{
					pDM_Odm->DM_SWAT_Table.Retry_Counter++;
					
					if(pDM_Odm->DM_SWAT_Table.Retry_Counter<=3)
					{
						pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=FALSE;
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[[ Low confidence result ]] avg_power_diff= (( %d ))  ->  Scan Target-channel again ]] \n", avg_power_diff));	

					         //3 [ Scan again ]
						odm_SwAntDivConstructScanChnl(Adapter, ScanChannel);
						PlatformSetTimer(Adapter, &pMgntInfo->ScanTimer, 5);
						return TRUE;					         
					}
					else
			{
						pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=TRUE;
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[[ Still Low confidence result ]]  (( Retry_Counter > 3 )) \n"));
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): Single antenna\n"));
					}
					
				}
				//2 [ Dual Antenna ]
				else if( (pMgntInfo->NumBssDesc != 0) && (avg_power_diff < power_target_L)   ) 
				{
					pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=TRUE;
					if(pDM_Odm->DM_SWAT_Table.ANTB_ON == FALSE)
					{
						pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
						pDM_Odm->DM_SWAT_Table.ANTB_ON = TRUE;
					}
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SwAntDivCheckBeforeLink(): Dual antenna\n"));
					pDM_Odm->DM_SWAT_Table.Dual_Ant_Counter++;

					// set bt coexDM from 1ant coexDM to 2ant coexDM
					BT_SetBtCoexAntNum(Adapter, BT_COEX_ANT_TYPE_DETECTED, 2);
					
					//3 [ Init antenna diversity ]
					pDM_Odm->SupportAbility |= ODM_BB_ANT_DIV; 
					ODM_AntDivInit(pDM_Odm);
				}
				//2 [ Single Antenna ]
				else if(avg_power_diff > power_target_H)
				{
					pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult=TRUE;
					if(pDM_Odm->DM_SWAT_Table.ANTB_ON == TRUE)
					{
						pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
						pDM_Odm->DM_SWAT_Table.ANTB_ON = FALSE;
						//BT_SetBtCoexAntNum(Adapter, BT_COEX_ANT_TYPE_DETECTED, 1);
					}
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): Single antenna\n"));
					pDM_Odm->DM_SWAT_Table.Single_Ant_Counter++;
				}
			}
			//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("bResult=(( %d ))\n",pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("Dual_Ant_Counter = (( %d )), Single_Ant_Counter = (( %d )) , Retry_Counter = (( %d )) , Aux_FailDetec_Counter = (( %d ))\n\n\n",
			pDM_Odm->DM_SWAT_Table.Dual_Ant_Counter,pDM_Odm->DM_SWAT_Table.Single_Ant_Counter,pDM_Odm->DM_SWAT_Table.Retry_Counter,pDM_Odm->DM_SWAT_Table.Aux_FailDetec_Counter));

			//2 recover the antenna setting

			if(pDM_Odm->DM_SWAT_Table.ANTB_ON == FALSE)
				ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, 0xfff, (pDM_SWAT_Table->SWAS_NoLink_BK_Reg948));
			
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("bResult=(( %d )), Recover  Reg[948]= (( %x )) \n\n",pDM_Odm->DM_SWAT_Table.RSSI_AntDect_bResult, pDM_SWAT_Table->SWAS_NoLink_BK_Reg948 ));

			
		}
		else if(pDM_Odm->SupportICType == ODM_RTL8192C)
		{
			if(pMgntInfo->NumBssDesc!=0 && Score<=0)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,
					("ODM_SwAntDivCheckBeforeLink(): Using Ant(%s)\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"MAIN":"AUX"));

				pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
			}
			else
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, 
					("ODM_SwAntDivCheckBeforeLink(): Remain Ant(%s)\n", (pDM_SWAT_Table->CurAntenna==MAIN_ANT)?"AUX":"MAIN"));

				pDM_SWAT_Table->CurAntenna = pDM_SWAT_Table->PreAntenna;

				//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, DM_SWAT_Table.CurAntenna);
				pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
				PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
			}
		}
		
		// Check state reset to default and wait for next time.
		pDM_SWAT_Table->SWAS_NoLink_State = 0;
		pMgntInfo->bScanAntDetect = FALSE;

		return FALSE;
	}

#else
		return	FALSE;
#endif

return FALSE;
}






//1 [3. PSD Method] ==========================================================




u4Byte
odm_GetPSDData(
	IN 	PVOID			pDM_VOID,
	IN u2Byte			point,
	IN u1Byte 		initial_gain)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			psd_report;
	
	ODM_SetBBReg(pDM_Odm, 0x808, 0x3FF, point);
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 1);  //Start PSD calculation, Reg808[22]=0->1
	ODM_StallExecution(150);//Wait for HW PSD report
	ODM_SetBBReg(pDM_Odm, 0x808, BIT22, 0);//Stop PSD calculation,  Reg808[22]=1->0
	psd_report = ODM_GetBBReg(pDM_Odm,0x8B4, bMaskDWord) & 0x0000FFFF;//Read PSD report, Reg8B4[15:0]
	
	psd_report = (u4Byte) (odm_ConvertTo_dB(psd_report));//+(u4Byte)(initial_gain);
	return psd_report;
}



VOID
ODM_SingleDualAntennaDetection_PSD(
	IN	 PVOID 	pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER	pAdapter	 =  pDM_Odm->Adapter;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u4Byte	Channel_ori;
	u1Byte 	initial_gain = 0x36;
	u1Byte	tone_idx;
	u1Byte	Tone_lenth_1=7, Tone_lenth_2=4;
	u2Byte	Tone_idx_1[7]={88, 104, 120, 8, 24, 40, 56};
	u2Byte	Tone_idx_2[4]={8, 24, 40, 56};
	u4Byte	PSD_report_Main[11]={0}, PSD_report_Aux[11]={0};
	//u1Byte	Tone_lenth_1=4, Tone_lenth_2=2;
	//u2Byte	Tone_idx_1[4]={88, 120, 24, 56};
	//u2Byte	Tone_idx_2[2]={ 24,  56};
	//u4Byte	PSD_report_Main[6]={0}, PSD_report_Aux[6]={0};

	u4Byte	PSD_report_temp,MAX_PSD_report_Main=0,MAX_PSD_report_Aux=0;
	u4Byte	PSD_power_threshold;
	u4Byte	Main_psd_result=0, Aux_psd_result=0;
	u4Byte	Regc50, Reg948, Regb2c,Regc14,Reg908;
	u4Byte	i=0,test_num=8;
	

	if(pDM_Odm->SupportICType != ODM_RTL8723B)
		return;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SingleDualAntennaDetection_PSD()============> \n"));	
	
	//2 [ Backup Current RF/BB Settings ]	
	
	Channel_ori = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, bRFRegOffsetMask);
	Reg948 = ODM_GetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord);
	Regb2c =  ODM_GetBBReg(pDM_Odm, rAGC_table_select, bMaskDWord);
	Regc50 = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskDWord);
	Regc14 = ODM_GetBBReg(pDM_Odm, 0xc14, bMaskDWord);
	Reg908 = ODM_GetBBReg(pDM_Odm, 0x908, bMaskDWord);

	//2 [ Setting for doing PSD function (CH4)]
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0); //disable whole CCK block
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF); // Turn off TX  ->  Pause TX Queue
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, 0x0); // [ Set IQK Matrix = 0 ] equivalent to [ Turn off CCA]

	// PHYTXON while loop
	ODM_SetBBReg(pDM_Odm, 0x908, bMaskDWord, 0x803); 
	while (ODM_GetBBReg(pDM_Odm, 0xdf4, BIT6)) 
	{
		i++;
		if (i > 1000000) 
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Wait in %s() more than %d times!\n", __FUNCTION__, i));	
			break;
		}
	}
	
	ODM_SetBBReg(pDM_Odm, 0xc50, 0x7f, initial_gain);  
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, 0x7ff, 0x04);     // Set RF to CH4 & 40M
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, 0xf00000, 0xf);	// 3 wire Disable    88c[23:20]=0xf
	ODM_SetBBReg(pDM_Odm, rFPGA0_PSDFunction, BIT14|BIT15, 0x0);  //128 pt	//Set PSD 128 ptss
	ODM_StallExecution(3000);	
	
	
	//2 [ Doing PSD Function in (CH4)]
	
        //Antenna A
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Switch to Main-ant   (CH4)\n"));
	ODM_SetBBReg(pDM_Odm, 0x948, 0xfff, 0x200);
	ODM_StallExecution(10);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("dbg\n"));
	for (i=0;i<test_num;i++)
	{	
		for (tone_idx=0;tone_idx<Tone_lenth_1;tone_idx++)
		{
			PSD_report_temp = odm_GetPSDData(pDM_Odm, Tone_idx_1[tone_idx], initial_gain);
			//if(  PSD_report_temp>PSD_report_Main[tone_idx]  )
				PSD_report_Main[tone_idx]+=PSD_report_temp;
		}
	}
        //Antenna B
       ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Switch to Aux-ant   (CH4)\n"));
       ODM_SetBBReg(pDM_Odm, 0x948, 0xfff, 0x280);
       ODM_StallExecution(10);	
	for (i=0;i<test_num;i++)
	{
		for (tone_idx=0;tone_idx<Tone_lenth_1;tone_idx++)
		{
			PSD_report_temp = odm_GetPSDData(pDM_Odm, Tone_idx_1[tone_idx], initial_gain);
			//if(  PSD_report_temp>PSD_report_Aux[tone_idx]  )
				PSD_report_Aux[tone_idx]+=PSD_report_temp;
		}
	}
	//2 [ Doing PSD Function in (CH8)]

	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, 0xf00000, 0x0);	// 3 wire enable    88c[23:20]=0x0
	ODM_StallExecution(3000);	
	
	ODM_SetBBReg(pDM_Odm, 0xc50, 0x7f, initial_gain);  
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, ODM_CHANNEL, 0x7ff, 0x04);     // Set RF to CH8 & 40M
	
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, 0xf00000, 0xf);	// 3 wire Disable    88c[23:20]=0xf
	ODM_StallExecution(3000);

        //Antenna A
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Switch to Main-ant   (CH8)\n"));
	ODM_SetBBReg(pDM_Odm, 0x948, 0xfff, 0x200);
	ODM_StallExecution(10);

	for (i=0;i<test_num;i++)
	{
		for (tone_idx=0;tone_idx<Tone_lenth_2;tone_idx++)
		{
			PSD_report_temp = odm_GetPSDData(pDM_Odm, Tone_idx_2[tone_idx], initial_gain);
			//if(  PSD_report_temp>PSD_report_Main[tone_idx]  )
				PSD_report_Main[Tone_lenth_1+tone_idx]+=PSD_report_temp;
		}
	}

        //Antenna B
        ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Switch to Aux-ant   (CH8)\n"));
        ODM_SetBBReg(pDM_Odm, 0x948, 0xfff, 0x280);
	ODM_StallExecution(10);	

	for (i=0;i<test_num;i++)
	{
		for (tone_idx=0;tone_idx<Tone_lenth_2;tone_idx++)
		{
			PSD_report_temp = odm_GetPSDData(pDM_Odm, Tone_idx_2[tone_idx], initial_gain);
			//if(  PSD_report_temp>PSD_report_Aux[tone_idx]  )
				PSD_report_Aux[Tone_lenth_1+tone_idx]+=PSD_report_temp;
		}
	}

        //2 [ Calculate Result ]

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\nMain PSD Result: (ALL) \n"));
	for (tone_idx=0;tone_idx<(Tone_lenth_1+Tone_lenth_2);tone_idx++)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Tone-%d]: %d, \n",(tone_idx+1), PSD_report_Main[tone_idx] ));
		Main_psd_result+= PSD_report_Main[tone_idx];
		if(PSD_report_Main[tone_idx]>MAX_PSD_report_Main)
			MAX_PSD_report_Main=PSD_report_Main[tone_idx];
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("--------------------------- \nTotal_Main= (( %d ))\n", Main_psd_result));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MAX_Main = (( %d ))\n", MAX_PSD_report_Main));
	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\nAux PSD Result: (ALL) \n"));
	for (tone_idx=0;tone_idx<(Tone_lenth_1+Tone_lenth_2);tone_idx++)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Tone-%d]: %d, \n",(tone_idx+1), PSD_report_Aux[tone_idx] ));
		Aux_psd_result+= PSD_report_Aux[tone_idx];
		if(PSD_report_Aux[tone_idx]>MAX_PSD_report_Aux)
			MAX_PSD_report_Aux=PSD_report_Aux[tone_idx];
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("--------------------------- \nTotal_Aux= (( %d ))\n", Aux_psd_result));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MAX_Aux = (( %d ))\n\n", MAX_PSD_report_Aux));
		
	//Main_psd_result=Main_psd_result-MAX_PSD_report_Main;
	//Aux_psd_result=Aux_psd_result-MAX_PSD_report_Aux;
	PSD_power_threshold=(Main_psd_result*7)>>3;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Main_result , Aux_result ] = [ %d , %d ], PSD_power_threshold=(( %d ))\n", Main_psd_result, Aux_psd_result,PSD_power_threshold));
	
	//3 [ Dual Antenna ]
	 if(Aux_psd_result >= PSD_power_threshold   ) 
	{
		if(pDM_Odm->DM_SWAT_Table.ANTB_ON == FALSE)
		{
			pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
			pDM_Odm->DM_SWAT_Table.ANTB_ON = TRUE;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("ODM_SwAntDivCheckBeforeLink(): Dual antenna\n"));
		
		// set bt coexDM from 1ant coexDM to 2ant coexDM
		//BT_SetBtCoexAntNum(pAdapter, BT_COEX_ANT_TYPE_DETECTED, 2);
					
		// Init antenna diversity
		pDM_Odm->SupportAbility |= ODM_BB_ANT_DIV; 
		ODM_AntDivInit(pDM_Odm);
		}
	//3 [ Single Antenna ]
	else
	{
		if(pDM_Odm->DM_SWAT_Table.ANTB_ON == TRUE)
		{
			pDM_Odm->DM_SWAT_Table.ANTA_ON = TRUE;
			pDM_Odm->DM_SWAT_Table.ANTB_ON = FALSE;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SwAntDivCheckBeforeLink(): Single antenna\n"));
	}

	//2 [ Recover all parameters ]
	
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask,Channel_ori);	
	ODM_SetBBReg(pDM_Odm, rFPGA0_AnalogParameter4, 0xf00000, 0x0);	// 3 wire enable    88c[23:20]=0x0
	ODM_SetBBReg(pDM_Odm, 0xc50, 0x7f, Regc50);  	
	
	ODM_SetBBReg(pDM_Odm, rS0S1_PathSwitch, bMaskDWord, Reg948);
	ODM_SetBBReg(pDM_Odm, rAGC_table_select, bMaskDWord, Regb2c);

	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 1); //enable whole CCK block
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x0); //Turn on TX 	// Resume TX Queue
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, Regc14); // [ Set IQK Matrix = 0 ] equivalent to [ Turn on CCA]
	ODM_SetBBReg(pDM_Odm, 0x908, bMaskDWord, Reg908); 
	
	return;

}

#endif
void
odm_SwAntDetectInit(
	IN		PVOID		pDM_VOID
	)
{
#if(defined(CONFIG_ANT_DETECTION))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	//pDM_SWAT_Table->SWAS_NoLink_BK_Reg92c = ODM_Read4Byte(pDM_Odm, rDPDT_control);
	//pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	//pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;
	pDM_SWAT_Table->Pre_Aux_FailDetec = FALSE;
	pDM_SWAT_Table->SWAS_NoLink_BK_Reg948 = 0xff;
#endif
}

