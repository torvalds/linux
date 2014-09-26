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

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#define	AFH_PSD		1	//0:normal PSD scan, 1: only do 20 pts PSD
#define	MODE_40M		0	//0:20M, 1:40M
#define	PSD_TH2		3  
#define	PSD_CHMIN		20   // Minimum channel number for BT AFH
#define	SIR_STEP_SIZE	3
#define   Smooth_Size_1 	5
#define	Smooth_TH_1	3
#define   Smooth_Size_2 	10
#define	Smooth_TH_2	4
#define   Smooth_Size_3 	20
#define	Smooth_TH_3	4
#define   Smooth_Step_Size 5
#define	Adaptive_SIR	1
#define	SCAN_INTERVAL	1500 //ms
#define	SYN_Length		5    // for 92D
	
#define	LNA_Low_Gain_1                      0x64
#define	LNA_Low_Gain_2                      0x5A
#define	LNA_Low_Gain_3                      0x58

#define	pw_th_10dB					0x0
#define	pw_th_16dB					0x3

#define	FA_RXHP_TH1                           5000
#define	FA_RXHP_TH2                           1500
#define	FA_RXHP_TH3                             800
#define	FA_RXHP_TH4                             600
#define	FA_RXHP_TH5                             500

#define	Idle_Mode					0
#define	High_TP_Mode				1
#define	Low_TP_Mode				2


VOID
odm_PSDMonitorInit(
	IN		PVOID			pDM_VOID
	)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;

	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PSD Monitor Setting
	//Which path in ADC/DAC is turnned on for PSD: both I/Q
	ODM_SetBBReg(pDM_Odm, ODM_PSDREG, BIT10|BIT11, 0x3);
	//Ageraged number: 8
	ODM_SetBBReg(pDM_Odm, ODM_PSDREG, BIT12|BIT13, 0x1);
	pDM_Odm->bPSDinProcess = FALSE;
	pDM_Odm->bUserAssignLevel = FALSE;
	pDM_Odm->bPSDactive = FALSE;
	//pDM_Odm->bDMInitialGainEnable=TRUE;		//change the initialization to DIGinit
	//Set Debug Port
	//PHY_SetBBReg(Adapter, 0x908, bMaskDWord, 0x803);
	//PHY_SetBBReg(Adapter, 0xB34, bMaskByte0, 0x00); // pause PSD
	//PHY_SetBBReg(Adapter, 0xB38, bMaskByte0, 10); //rescan
	//PHY_SetBBReg(Adapter, 0xB38, bMaskByte2|bMaskByte3, 100); //interval

	//PlatformSetTimer( Adapter, &pHalData->PSDTriggerTimer, 0); //ms
#endif
}

VOID
PatchDCTone(
	IN		PVOID			pDM_VOID,
	pu4Byte		PSD_report,
	u1Byte 		initial_gain_psd
)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	//HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PADAPTER	pAdapter;
	
	u4Byte	psd_report;

	//2 Switch to CH11 to patch CH9 and CH13 DC tone
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, 11);
	
	if(pDM_Odm->SupportICType== ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode) == ODM_SMSP)||(*(pDM_Odm->pMacPhyMode) == ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, 11);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, 0xfffff, 0x643BC);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, 0xfffff, 0x77C1A);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, 0xfffff, 0x41289);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, 0xfffff, 0x01840);
		}
		else
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, 0xfffff, 0x643BC);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, 0xfffff, 0x77C1A);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, 0xfffff, 0x41289);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, 0xfffff, 0x01840);
		}
	}
	
	//Ch9 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 96, initial_gain_psd);
	PSD_report[50] = psd_report;
	//Ch13 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 32, initial_gain_psd);
	PSD_report[70] = psd_report;
	
	//2 Switch to CH3 to patch CH1 and CH5 DC tone
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, 3);

	
	if(pDM_Odm->SupportICType==ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode) == ODM_SMSP)||(*(pDM_Odm->pMacPhyMode) == ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, 3);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x25, 0xfffff, 0x643BC);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, 0xfffff, 0x07C1A);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x2B, 0xfffff, 0x61289);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_B, 0x2C, 0xfffff, 0x01C41);
		}
		else
		{
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x25, 0xfffff, 0x643BC);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x26, 0xfffff, 0xFC038);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, 0xfffff, 0x07C1A);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x2B, 0xfffff, 0x61289);
			//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, 0x2C, 0xfffff, 0x01C41);
		}
	}
	
	//Ch1 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 96, initial_gain_psd);
	PSD_report[10] = psd_report;
	//Ch5 DC tone patch
	psd_report = GetPSDData(pDM_Odm, 32, initial_gain_psd);
	PSD_report[30] = psd_report;

}


VOID
GoodChannelDecision(
	IN		PVOID			pDM_VOID,
	pu4Byte		PSD_report,
	pu1Byte		PSD_bitmap,
	u1Byte 		RSSI_BT,
	pu1Byte		PSD_bitmap_memory)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRXHP_T			pRX_HP_Table = &pDM_Odm->DM_RXHP_Table;
	//s4Byte	TH1 =  SSBT-0x15;    // modify TH by Neil Chen
	s4Byte	TH1= RSSI_BT+0x14;
	s4Byte	TH2 = RSSI_BT+85;
	//u2Byte    TH3;
//	s4Byte	RegB34;
	u1Byte	bitmap, Smooth_size[3], Smooth_TH[3];
	//u1Byte	psd_bit;
	u4Byte	i,n,j, byte_idx, bit_idx, good_cnt, good_cnt_smoothing, Smooth_Interval[3];
	int 		start_byte_idx,start_bit_idx,cur_byte_idx, cur_bit_idx,NOW_byte_idx ;
	
//	RegB34 = PHY_QueryBBReg(Adapter,0xB34, bMaskDWord)&0xFF;

	if((pDM_Odm->SupportICType == ODM_RTL8192C)||(pDM_Odm->SupportICType == ODM_RTL8192D))
       {
            TH1 = RSSI_BT + 0x14;  
	}

	Smooth_size[0]=Smooth_Size_1;
	Smooth_size[1]=Smooth_Size_2;
	Smooth_size[2]=Smooth_Size_3;
	Smooth_TH[0]=Smooth_TH_1;
	Smooth_TH[1]=Smooth_TH_2;
	Smooth_TH[2]=Smooth_TH_3;
	Smooth_Interval[0]=16;
	Smooth_Interval[1]=15;
	Smooth_Interval[2]=13;
	good_cnt = 0;
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		//2 Threshold  

		if(RSSI_BT >=41)
			TH1 = 113;	
		else if(RSSI_BT >=38)   // >= -15dBm
			TH1 = 105;                              //0x69
		else if((RSSI_BT >=33)&(RSSI_BT <38))
			TH1 = 99+(RSSI_BT-33);         //0x63
		else if((RSSI_BT >=26)&(RSSI_BT<33))
			TH1 = 99-(33-RSSI_BT)+2;     //0x5e
	 	else if((RSSI_BT >=24)&(RSSI_BT<26))
			TH1 = 88-((RSSI_BT-24)*3);   //0x58
		else if((RSSI_BT >=18)&(RSSI_BT<24))
			TH1 = 77+((RSSI_BT-18)*2);
		else if((RSSI_BT >=14)&(RSSI_BT<18))
			TH1 = 63+((RSSI_BT-14)*2);
		else if((RSSI_BT >=8)&(RSSI_BT<14))
			TH1 = 58+((RSSI_BT-8)*2);
		else if((RSSI_BT >=3)&(RSSI_BT<8))
			TH1 = 52+(RSSI_BT-3);
		else
			TH1 = 51;
	}

	for (i = 0; i< 10; i++)
		PSD_bitmap[i] = 0;
	

	 // Add By Gary
       for (i=0; i<80; i++)
	   	pRX_HP_Table->PSD_bitmap_RXHP[i] = 0;
	// End



	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		TH1 =TH1-SIR_STEP_SIZE;
	}
	while (good_cnt < PSD_CHMIN)
	{
		good_cnt = 0;
		if(pDM_Odm->SupportICType==ODM_RTL8723A)
		{
		if(TH1 ==TH2)
			break;
		if((TH1+SIR_STEP_SIZE) < TH2)
			TH1 += SIR_STEP_SIZE;
		else
			TH1 = TH2;
		}
		else
		{
			if(TH1==(RSSI_BT+0x1E))
             		     break;    
   			if((TH1+2) < (RSSI_BT+0x1E))
				TH1+=3;
		     	else
				TH1 = RSSI_BT+0x1E;	
             
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD: decision threshold is: %d", TH1));
			 
		for (i = 0; i< 80; i++)
		{
			if((s4Byte)(PSD_report[i]) < TH1)
			{
				byte_idx = i / 8;
				bit_idx = i -8*byte_idx;
				bitmap = PSD_bitmap[byte_idx];
				PSD_bitmap[byte_idx] = bitmap | (u1Byte) (1 << bit_idx);
			}
		}

#if DBG
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: before smoothing\n"));
		for(n=0;n<10;n++)
		{
			//DbgPrint("PSD_bitmap[%u]=%x\n", n, PSD_bitmap[n]);
			for (i = 0; i<8; i++)
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD_bitmap[%u] =   %d\n", 2402+n*8+i, (PSD_bitmap[n]&BIT(i))>>i));
		}
#endif
	
		//1 Start of smoothing function

		for (j=0;j<3;j++)
		{
			start_byte_idx=0;
			start_bit_idx=0;
			for(n=0; n<Smooth_Interval[j]; n++)
			{
				good_cnt_smoothing = 0;
				cur_bit_idx = start_bit_idx;
				cur_byte_idx = start_byte_idx;
				for ( i=0; i < Smooth_size[j]; i++)
				{
					NOW_byte_idx = cur_byte_idx + (i+cur_bit_idx)/8;
					if ( (PSD_bitmap[NOW_byte_idx]& BIT( (cur_bit_idx + i)%8)) != 0)
						good_cnt_smoothing++;

				}

				if( good_cnt_smoothing < Smooth_TH[j] )
				{
					cur_bit_idx = start_bit_idx;
					cur_byte_idx = start_byte_idx;
					for ( i=0; i< Smooth_size[j] ; i++)
					{	
						NOW_byte_idx = cur_byte_idx + (i+cur_bit_idx)/8;				
						PSD_bitmap[NOW_byte_idx] = PSD_bitmap[NOW_byte_idx] & (~BIT( (cur_bit_idx + i)%8));
					}
				}
				start_bit_idx =  start_bit_idx + Smooth_Step_Size;
				while ( (start_bit_idx)  > 7 )
				{
					start_byte_idx= start_byte_idx+start_bit_idx/8;
					start_bit_idx = start_bit_idx%8;
				}
			}

			ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: after %u smoothing", j+1));
			for(n=0;n<10;n++)
			{
				for (i = 0; i<8; i++)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD_bitmap[%u] =   %d\n", 2402+n*8+i, (PSD_bitmap[n]&BIT(i))>>i));
					
					if ( ((PSD_bitmap[n]&BIT(i))>>i) ==1)  //----- Add By Gary
					{
	                                   pRX_HP_Table->PSD_bitmap_RXHP[8*n+i] = 1;
					}                                                  // ------end by Gary
				}
			}

		}

	
		good_cnt = 0;
		for ( i = 0; i < 10; i++)
		{
			for (n = 0; n < 8; n++)
				if((PSD_bitmap[i]& BIT(n)) != 0)
					good_cnt++;
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, ODM_COMP_PSD,("PSD: good channel cnt = %u",good_cnt));
	}

	//RT_TRACE(ODM_COMP_PSD, DBG_LOUD,("PSD: SSBT=%d, TH2=%d, TH1=%d",SSBT,TH2,TH1));
	for (i = 0; i <10; i++)
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: PSD_bitmap[%u]=%x",i,PSD_bitmap[i]));
/*	
	//Update bitmap memory
	for(i = 0; i < 80; i++)
	{
		byte_idx = i / 8;
		bit_idx = i -8*byte_idx;
		psd_bit = (PSD_bitmap[byte_idx] & BIT(bit_idx)) >> bit_idx;
		bitmap = PSD_bitmap_memory[i]; 
		PSD_bitmap_memory[i] = (bitmap << 1) |psd_bit;
	}
*/
}



VOID
odm_PSD_Monitor(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	unsigned int 		pts, start_point, stop_point;
	u1Byte			initial_gain ;
	static u1Byte		PSD_bitmap_memory[80], init_memory = 0;
	static u1Byte 		psd_cnt=0;
	static u4Byte		PSD_report[80], PSD_report_tmp;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u1Byte 			H2C_PSD_DATA[5]={0,0,0,0,0};
	static u1Byte		H2C_PSD_DATA_last[5] ={0,0,0,0,0};
	u1Byte			idx[20]={96,99,102,106,109,112,115,118,122,125,
					0,3,6,10,13,16,19,22,26,29};
	u1Byte			n, i, channel, BBReset,tone_idx;
	u1Byte			PSD_bitmap[10], SSBT=0,initial_gain_psd=0, RSSI_BT=0, initialGainUpper;
	s4Byte    			PSD_skip_start, PSD_skip_stop;
	u4Byte			CurrentChannel, RXIQI, RxIdleLowPwr, wlan_channel;
	u4Byte			ReScan, Interval, Is40MHz;
	u8Byte			curTxOkCnt, curRxOkCnt;
	int 				cur_byte_idx, cur_bit_idx;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PMGNT_INFO      	pMgntInfo = &Adapter->MgntInfo;
	

	if(*pDM_Odm->pbDriverIsGoingToPnpSetPowerSleep)
 	{
  		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("pbDriverIsGoingToPnpSetPowerSleep!!!!!!!!!!!!!!!\n"));
  		return;
 	}

	
	if( (*(pDM_Odm->pbScanInProcess)) ||
		pDM_Odm->bLinkInProcess)
	{
		if((pDM_Odm->SupportICType==ODM_RTL8723A)&(pDM_Odm->SupportInterface==ODM_ITRF_PCIE))
		{
			ODM_SetTimer( pDM_Odm, &pDM_Odm->PSDTimer, 1500); //ms	
			//psd_cnt=0;
		}
		return;
	}

	if(pDM_Odm->bBtHsOperation)
	{
		ReScan = 1;
		Interval = SCAN_INTERVAL;
	}
	else
	{
	ReScan = PSD_RESCAN;
	Interval = SCAN_INTERVAL;
	}

	//1 Initialization
	if(init_memory == 0)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Init memory\n"));
		for(i = 0; i < 80; i++)
			PSD_bitmap_memory[i] = 0xFF; // channel is always good
		init_memory = 1;
	}
	if(psd_cnt == 0)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Enter dm_PSD_Monitor\n"));
		for(i = 0; i < 80; i++)
			PSD_report[i] = 0;
	}

	//1 Backup Current Settings
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
/*
	if(pDM_Odm->SupportICType==ODM_RTL8192D)
	{
		//2 Record Current synthesizer parameters based on current channel
		if((*pDM_Odm->MacPhyMode92D == SINGLEMAC_SINGLEPHY)||(*pDM_Odm->MacPhyMode92D == DUALMAC_SINGLEPHY))
		{
			SYN_RF25 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(Adapter, ODM_RF_PATH_B, 0x2C, bMaskDWord);
       	}
		else     // DualMAC_DualPHY 2G
		{
			SYN_RF25 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(Adapter, ODM_RF_PATH_A, 0x2C, bMaskDWord);
		}
	}
*/
	//RXIQI = PHY_QueryBBReg(Adapter, 0xC14, bMaskDWord);
	RXIQI = ODM_GetBBReg(pDM_Odm, 0xC14, bMaskDWord);

	//RxIdleLowPwr = (PHY_QueryBBReg(Adapter, 0x818, bMaskDWord)&BIT28)>>28;
	RxIdleLowPwr = (ODM_GetBBReg(pDM_Odm, 0x818, bMaskDWord)&BIT28)>>28;

	//2???
	if(CHNL_RUN_ABOVE_40MHZ(pMgntInfo))
		Is40MHz = TRUE;
	else
		Is40MHz = FALSE;

	ODM_RT_TRACE(pDM_Odm,	ODM_COMP_PSD, DBG_LOUD,("PSD Scan Start\n"));
	//1 Turn off CCK
	//PHY_SetBBReg(Adapter, rFPGA0_RFMOD, BIT24, 0);
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0);
	//1 Turn off TX
	//Pause TX Queue
	//PlatformEFIOWrite1Byte(Adapter, REG_TXPAUSE, 0xFF);
	ODM_Write1Byte(pDM_Odm,REG_TXPAUSE, 0xFF);
	
	//Force RX to stop TX immediately
	//PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);

	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);
	//1 Turn off RX
	//Rx AGC off  RegC70[0]=0, RegC7C[20]=0
	//PHY_SetBBReg(Adapter, 0xC70, BIT0, 0);
	//PHY_SetBBReg(Adapter, 0xC7C, BIT20, 0);

	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 0);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 0);

	
	//Turn off CCA
	//PHY_SetBBReg(Adapter, 0xC14, bMaskDWord, 0x0);
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, 0x0);
	
	//BB Reset
	//BBReset = PlatformEFIORead1Byte(Adapter, 0x02);
	BBReset = ODM_Read1Byte(pDM_Odm, 0x02);
	
	//PlatformEFIOWrite1Byte(Adapter, 0x02, BBReset&(~BIT0));
	//PlatformEFIOWrite1Byte(Adapter, 0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 1); //clock gated to prevent from AGC table mess 
	ODM_Write1Byte(pDM_Odm,  0x02, BBReset&(~BIT0));
	ODM_Write1Byte(pDM_Odm,  0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 0);
	
	//1 Leave RX idle low power
	//PHY_SetBBReg(Adapter, 0x818, BIT28, 0x0);

	ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 0x0);
	//1 Fix initial gain
	//if (IS_HARDWARE_TYPE_8723AE(Adapter))
	//RSSI_BT = pHalData->RSSI_BT;
       //else if((IS_HARDWARE_TYPE_8192C(Adapter))||(IS_HARDWARE_TYPE_8192D(Adapter)))      // Add by Gary
       //    RSSI_BT = RSSI_BT_new;

	if((pDM_Odm->SupportICType==ODM_RTL8723A)&(pDM_Odm->SupportInterface==ODM_ITRF_PCIE))
	RSSI_BT=pDM_Odm->RSSI_BT;		//need to check C2H to pDM_Odm RSSI BT

	if(RSSI_BT>=47)
		RSSI_BT=47;
	   
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
	       //Neil add--2011--10--12
		//2 Initial Gain index 
		if(RSSI_BT >=35)   // >= -15dBm
			initial_gain_psd = RSSI_BT*2;
		else if((RSSI_BT >=33)&(RSSI_BT<35))
			initial_gain_psd = RSSI_BT*2+6;
		else if((RSSI_BT >=24)&(RSSI_BT<33))
			initial_gain_psd = 70-(33-RSSI_BT);
	 	else if((RSSI_BT >=19)&(RSSI_BT<24))
			initial_gain_psd = 64-((24-RSSI_BT)*4);
		else if((RSSI_BT >=14)&(RSSI_BT<19))
			initial_gain_psd = 44-((18-RSSI_BT)*2);
		else if((RSSI_BT >=8)&(RSSI_BT<14))
			initial_gain_psd = 35-(14-RSSI_BT);
		else
			initial_gain_psd = 0x1B;
	}
	else
	{
	
		//need to do	
         	initial_gain_psd = pDM_Odm->RSSI_Min;    // PSD report based on RSSI
           	//}  	
	}
	//if(RSSI_BT<0x17)
	//	RSSI_BT +=3;
	//DbgPrint("PSD: RSSI_BT= %d\n", RSSI_BT);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));

	//initialGainUpper = 0x5E;  //Modify by neil chen
	
	if(pDM_Odm->bUserAssignLevel)
	{
		pDM_Odm->bUserAssignLevel = FALSE;
		initialGainUpper = 0x7f;
	}
	else
	{
		initialGainUpper = 0x5E;
	}
	
	/*
	if (initial_gain_psd < 0x1a)
		initial_gain_psd = 0x1a;
	if (initial_gain_psd > initialGainUpper)
		initial_gain_psd = initialGainUpper;
	*/

	//if(pDM_Odm->SupportICType==ODM_RTL8723A)
	SSBT = RSSI_BT  * 2 +0x3E;
	
	
	//if(IS_HARDWARE_TYPE_8723AE(Adapter))
	//	SSBT = RSSI_BT  * 2 +0x3E;
	//else if((IS_HARDWARE_TYPE_8192C(Adapter))||(IS_HARDWARE_TYPE_8192D(Adapter)))   // Add by Gary
	//{
	//	RSSI_BT = initial_gain_psd;
	//	SSBT = RSSI_BT;
	//}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: SSBT= %d\n", SSBT));
	ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD: initial gain= 0x%x\n", initial_gain_psd));
	//DbgPrint("PSD: SSBT= %d", SSBT);
	//need to do
	pDM_Odm->bDMInitialGainEnable = FALSE;
	initial_gain =(u1Byte) (ODM_GetBBReg(pDM_Odm, 0xc50, bMaskDWord) & 0x7F);
	
        // make sure the initial gain is under the correct range.
	//initial_gain_psd &= 0x7f;
	ODM_Write_DIG(pDM_Odm, initial_gain_psd);
	//1 Turn off 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0xF);

	//pts value = 128, 256, 512, 1024
	pts = 128;

	if(pts == 128)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
		start_point = 64;
		stop_point = 192;
	}
	else if(pts == 256)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x1);
		start_point = 128;
		stop_point = 384;
	}
	else if(pts == 512)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x2);
		start_point = 256;
		stop_point = 768;
	}
	else
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x3);
		start_point = 512;
		stop_point = 1536;
	}
	

//3 Skip WLAN channels if WLAN busy

	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast) - lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast) - lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);	

	PSD_skip_start=80;
	PSD_skip_stop = 0;
	wlan_channel = CurrentChannel & 0x0f;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD: current channel: %x, BW:%d \n", wlan_channel, Is40MHz));
	if(pDM_Odm->SupportICType==ODM_RTL8723A)
	{
		if(pDM_Odm->bBtHsOperation)
		{
			if(pDM_Odm->bLinked)
			{
				if(Is40MHz)
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
				}
				else
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-10;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+18; 
				}
			}
			else
			{
				// mask for 40MHz
				PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
				PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
			}
			if(PSD_skip_start < 0)
				PSD_skip_start = 0;
			if(PSD_skip_stop >80)
				PSD_skip_stop = 80;
		}
		else
		{
			if((curRxOkCnt+curTxOkCnt) > 5)
			{
				if(Is40MHz)
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-2;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+4;
				}
				else
				{
					PSD_skip_start = ((wlan_channel-1)*5 -Is40MHz*10)-10;  // Modify by Neil to add 10 chs to mask
					PSD_skip_stop = (PSD_skip_start + (1+Is40MHz)*20)+18; 
				}
				
				if(PSD_skip_start < 0)
					PSD_skip_start = 0;
				if(PSD_skip_stop >80)
					PSD_skip_stop = 80;
			}
		}
	}
#if 0	
	else
	{
		if((curRxOkCnt+curTxOkCnt) > 1000)
		{
			PSD_skip_start = (wlan_channel-1)*5 -Is40MHz*10;
			PSD_skip_stop = PSD_skip_start + (1+Is40MHz)*20;
		}
	}   
#endif  //Reove RXHP Issue
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD: Skip tone from %d to %d \n", PSD_skip_start, PSD_skip_stop));

 	for (n=0;n<80;n++)
 	{
 		if((n%20)==0)
 		{
			channel = (n/20)*4 + 1;
					
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
				}
		tone_idx = n%20;
		if ((n>=PSD_skip_start) && (n<PSD_skip_stop))
		{	
			PSD_report[n] = SSBT;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD:Tone %d skipped \n", n));
		}
		else
		{
			PSD_report_tmp =  GetPSDData(pDM_Odm, idx[tone_idx], initial_gain_psd);

			if ( PSD_report_tmp > PSD_report[n])
				PSD_report[n] = PSD_report_tmp;
				
		}
	}

	PatchDCTone(pDM_Odm, PSD_report, initial_gain_psd);
      
       //----end
	//1 Turn on RX
	//Rx AGC on
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 1);
	//CCK on
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 1);
	//1 Turn on TX
	//Resume TX Queue
	
	ODM_Write1Byte(pDM_Odm,REG_TXPAUSE, 0x00);
	//Turn on 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0x0);
	//1 Restore Current Settings
	//Resume DIG
	pDM_Odm->bDMInitialGainEnable = TRUE;
	
	ODM_Write_DIG(pDM_Odm, initial_gain);

	// restore originl center frequency
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, CurrentChannel);

	//Turn on CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, RXIQI);
	//Restore RX idle low power
	if(RxIdleLowPwr == TRUE)
		ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 1);
	
	psd_cnt++;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD:psd_cnt = %d \n",psd_cnt));
	if (psd_cnt < ReScan)
		ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, Interval);		
	else
	{
		psd_cnt = 0;
		for(i=0;i<80;i++)
			//DbgPrint("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]);
			RT_TRACE(	ODM_COMP_PSD, DBG_LOUD,("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]));


		GoodChannelDecision(pDM_Odm, PSD_report, PSD_bitmap,RSSI_BT, PSD_bitmap_memory);

		if(pDM_Odm->SupportICType==ODM_RTL8723A)
		{
			cur_byte_idx=0;
			cur_bit_idx=0;

			//2 Restore H2C PSD Data to Last Data
		  	H2C_PSD_DATA_last[0] = H2C_PSD_DATA[0];
			H2C_PSD_DATA_last[1] = H2C_PSD_DATA[1];
			H2C_PSD_DATA_last[2] = H2C_PSD_DATA[2];
			H2C_PSD_DATA_last[3] = H2C_PSD_DATA[3];
			H2C_PSD_DATA_last[4] = H2C_PSD_DATA[4];

	
			//2 Translate 80bit channel map to 40bit channel	
			for ( i=0;i<5;i++)
			{
				for(n=0;n<8;n++)
				{
					cur_byte_idx = i*2 + n/4;
					cur_bit_idx = (n%4)*2;
					if ( ((PSD_bitmap[cur_byte_idx]& BIT(cur_bit_idx)) != 0) && ((PSD_bitmap[cur_byte_idx]& BIT(cur_bit_idx+1)) != 0))
						H2C_PSD_DATA[i] = H2C_PSD_DATA[i] | (u1Byte) (1 << n);
				}
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("H2C_PSD_DATA[%d]=0x%x\n" ,i, H2C_PSD_DATA[i]));
			}
	
			//3 To Compare the difference
			for ( i=0;i<5;i++)
			{
				if(H2C_PSD_DATA[i] !=H2C_PSD_DATA_last[i])
				{
					FW_FillH2CCmd(Adapter, H2C_92C_PSD_RESULT, 5, H2C_PSD_DATA);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_PSD, DBG_LOUD,("Need to Update the AFH Map \n"));
					break;
				}
				else
				{
					if(i==5)
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Not need to Update\n"));	
				}
			}
			if(pDM_Odm->bBtHsOperation)
			{
				ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, 10000);
				ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Leave dm_PSD_Monitor\n"));		
			}
			else
			{
				ODM_SetTimer(pDM_Odm, &pDM_Odm->PSDTimer, 1500);
				ODM_RT_TRACE(	pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("Leave dm_PSD_Monitor\n"));		
		}
	}
    }
}
/*
//Neil for Get BT RSSI
// Be Triggered by BT C2H CMD
VOID
ODM_PSDGetRSSI(
	IN	u1Byte	RSSI_BT)
{


}

*/

VOID
ODM_PSDMonitor(
	IN		PVOID			pDM_VOID
	)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	
	//if(IS_HARDWARE_TYPE_8723AE(Adapter))
	
	if(pDM_Odm->SupportICType == ODM_RTL8723A)   //may need to add other IC type
	{
		if(pDM_Odm->SupportInterface==ODM_ITRF_PCIE)
		{
			if(!pDM_Odm->bBtEnabled) //need to check upper layer connection
			{
				pDM_Odm->bPSDactive=FALSE;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD, ("odm_PSDMonitor, return for BT is disabled!!!\n"));
		   		return; 
			}

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD, ("odm_PSDMonitor\n"));
		//{
			pDM_Odm->bPSDinProcess = TRUE;
	 		pDM_Odm->bPSDactive=TRUE;
			odm_PSD_Monitor(pDM_Odm);
			pDM_Odm->bPSDinProcess = FALSE;
		}	
	}	

}
VOID
odm_PSDMonitorCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
       HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	PlatformScheduleWorkItem(&pHalData->PSDMonitorWorkitem);
}

VOID
odm_PSDMonitorWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	ODM_PSDMonitor(pDM_Odm);
}


 //cosa debug tool need to modify

VOID
ODM_PSDDbgControl(
	IN	PADAPTER	Adapter,
	IN	u4Byte		mode,
	IN	u4Byte		btRssi
	)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD, (" Monitor mode=%d, btRssi=%d\n", mode, btRssi));
	if(mode)
	{
		pDM_Odm->RSSI_BT = (u1Byte)btRssi;
		pDM_Odm->bUserAssignLevel = TRUE;
		ODM_SetTimer( pDM_Odm, &pDM_Odm->PSDTimer, 0); //ms		
	}
	else
	{
		ODM_CancelTimer(pDM_Odm, &pDM_Odm->PSDTimer);
	}
#endif
}


//#if(DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)

void	odm_RXHPInit(
	IN		PVOID			pDM_VOID)
{
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)|(DEV_BUS_TYPE == RT_USB_INTERFACE)
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
   	u1Byte			index;

	pRX_HP_Table->RXHP_enable = TRUE;
	pRX_HP_Table->RXHP_flag = 0;
	pRX_HP_Table->PSD_func_trigger = 0;
	pRX_HP_Table->Pre_IGI = 0x20;
	pRX_HP_Table->Cur_IGI = 0x20;
	pRX_HP_Table->Cur_pw_th = pw_th_10dB;
	pRX_HP_Table->Pre_pw_th = pw_th_10dB;
	for(index=0; index<80; index++)
		pRX_HP_Table->PSD_bitmap_RXHP[index] = 1;

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)
	pRX_HP_Table->TP_Mode = Idle_Mode;
#endif
#endif
}

VOID
odm_PSD_RXHP(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	PADAPTER		Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	unsigned int 		pts, start_point, stop_point, initial_gain ;
	static u1Byte		PSD_bitmap_memory[80], init_memory = 0;
	static u1Byte 		psd_cnt=0;
	static u4Byte		PSD_report[80], PSD_report_tmp;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u1Byte			idx[20]={96,99,102,106,109,112,115,118,122,125,
					0,3,6,10,13,16,19,22,26,29};
	u1Byte			n, i, channel, BBReset,tone_idx;
	u1Byte			PSD_bitmap[10]/*, SSBT=0*/,initial_gain_psd=0, RSSI_BT=0, initialGainUpper;
	s4Byte    			PSD_skip_start, PSD_skip_stop;
	u4Byte			CurrentChannel, RXIQI, RxIdleLowPwr, wlan_channel;
	u4Byte			ReScan, Interval, Is40MHz;
	u8Byte			curTxOkCnt, curRxOkCnt;
	//--------------2G band synthesizer for 92D switch RF channel using----------------- 
	u1Byte			group_idx=0;
	u4Byte			SYN_RF25=0, SYN_RF26=0, SYN_RF27=0, SYN_RF2B=0, SYN_RF2C=0;
	u4Byte			SYN[5] = {0x25, 0x26, 0x27, 0x2B, 0x2C};    // synthesizer RF register for 2G channel
	u4Byte			SYN_group[3][5] = {{0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840},     // For CH1,2,4,9,10.11.12   {0x643BC, 0xFC038, 0x77C1A, 0x41289, 0x01840}
									    {0x643BC, 0xFC038, 0x07C1A, 0x41289, 0x01840},     // For CH3,13,14
									    {0x243BC, 0xFC438, 0x07C1A, 0x4128B, 0x0FC41}};   // For Ch5,6,7,8
       //--------------------- Add by Gary for Debug setting ----------------------
  	u1Byte                 RSSI_BT_new = (u1Byte) ODM_GetBBReg(pDM_Odm, 0xB9C, 0xFF);
       u1Byte                 rssi_ctrl = (u1Byte) ODM_GetBBReg(pDM_Odm, 0xB38, 0xFF);
       //---------------------------------------------------------------------
	
	if(pMgntInfo->bScanInProgress)
	{
		return;
	}

	ReScan = PSD_RESCAN;
	Interval = SCAN_INTERVAL;


	//1 Initialization
	if(init_memory == 0)
	{
		RT_TRACE(	ODM_COMP_PSD, DBG_LOUD,("Init memory\n"));
		for(i = 0; i < 80; i++)
			PSD_bitmap_memory[i] = 0xFF; // channel is always good
		init_memory = 1;
	}
	if(psd_cnt == 0)
	{
		RT_TRACE(ODM_COMP_PSD, DBG_LOUD,("Enter dm_PSD_Monitor\n"));
		for(i = 0; i < 80; i++)
			PSD_report[i] = 0;
	}

	//1 Backup Current Settings
	CurrentChannel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask);
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		//2 Record Current synthesizer parameters based on current channel
		if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))
		{
			SYN_RF25 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, bMaskDWord);
       	}
		else     // DualMAC_DualPHY 2G
		{
			SYN_RF25 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, bMaskDWord);
			SYN_RF26 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, bMaskDWord);
			SYN_RF27 = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, bMaskDWord);
			SYN_RF2B = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, bMaskDWord);
			SYN_RF2C = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, bMaskDWord);
		}
	}
	RXIQI = ODM_GetBBReg(pDM_Odm, 0xC14, bMaskDWord);
	RxIdleLowPwr = (ODM_GetBBReg(pDM_Odm, 0x818, bMaskDWord)&BIT28)>>28;
	Is40MHz = *(pDM_Odm->pBandWidth);
	ODM_RT_TRACE(pDM_Odm,	ODM_COMP_PSD, DBG_LOUD,("PSD Scan Start\n"));
	//1 Turn off CCK
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 0);
	//1 Turn off TX
	//Pause TX Queue
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0xFF);
	//Force RX to stop TX immediately
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_AC, bRFRegOffsetMask, 0x32E13);
	//1 Turn off RX
	//Rx AGC off  RegC70[0]=0, RegC7C[20]=0
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 0);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 0);
	//Turn off CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, 0x0);
	//BB Reset
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 1); //clock gated to prevent from AGC table mess 
	BBReset = ODM_Read1Byte(pDM_Odm, 0x02);
	ODM_Write1Byte(pDM_Odm, 0x02, BBReset&(~BIT0));
	ODM_Write1Byte(pDM_Odm, 0x02, BBReset|BIT0);
	ODM_SetBBReg(pDM_Odm, 0x87C, BIT31, 0);
	//1 Leave RX idle low power
	ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 0x0);
	//1 Fix initial gain
      	RSSI_BT = RSSI_BT_new;
	RT_TRACE(ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	if(rssi_ctrl == 1)        // just for debug!!
		initial_gain_psd = RSSI_BT_new; 
     	else
		initial_gain_psd = pDM_Odm->RSSI_Min;    // PSD report based on RSSI
	
	RT_TRACE(ODM_COMP_PSD, DBG_LOUD,("PSD: RSSI_BT= %d\n", RSSI_BT));
	
	initialGainUpper = 0x54;
	
	RSSI_BT = initial_gain_psd;
	//SSBT = RSSI_BT;
	
	//RT_TRACE(	ODM_COMP_PSD, DBG_LOUD,("PSD: SSBT= %d\n", SSBT));
	RT_TRACE(	ODM_COMP_PSD, DBG_LOUD,("PSD: initial gain= 0x%x\n", initial_gain_psd));
	
	pDM_Odm->bDMInitialGainEnable = FALSE;		
	initial_gain = ODM_GetBBReg(pDM_Odm, 0xc50, bMaskDWord) & 0x7F;
	//ODM_SetBBReg(pDM_Odm, 0xc50, 0x7F, initial_gain_psd);	
	ODM_Write_DIG(pDM_Odm, initial_gain_psd);
	//1 Turn off 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0xF);

	//pts value = 128, 256, 512, 1024
	pts = 128;

	if(pts == 128)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x0);
		start_point = 64;
		stop_point = 192;
	}
	else if(pts == 256)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x1);
		start_point = 128;
		stop_point = 384;
	}
	else if(pts == 512)
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x2);
		start_point = 256;
		stop_point = 768;
	}
	else
	{
		ODM_SetBBReg(pDM_Odm, 0x808, BIT14|BIT15, 0x3);
		start_point = 512;
		stop_point = 1536;
	}
	

//3 Skip WLAN channels if WLAN busy
	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast) - lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast) - lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);
	
	PSD_skip_start=80;
	PSD_skip_stop = 0;
	wlan_channel = CurrentChannel & 0x0f;

	RT_TRACE(ODM_COMP_PSD,DBG_LOUD,("PSD: current channel: %x, BW:%d \n", wlan_channel, Is40MHz));
	
	if((curRxOkCnt+curTxOkCnt) > 1000)
	{
		PSD_skip_start = (wlan_channel-1)*5 -Is40MHz*10;
		PSD_skip_stop = PSD_skip_start + (1+Is40MHz)*20;
	}

	RT_TRACE(ODM_COMP_PSD,DBG_LOUD,("PSD: Skip tone from %d to %d \n", PSD_skip_start, PSD_skip_stop));

 	for (n=0;n<80;n++)
 	{
 		if((n%20)==0)
 		{
			channel = (n/20)*4 + 1;
			if(pDM_Odm->SupportICType == ODM_RTL8192D)
			{
				switch(channel)
				{
					case 1: 
					case 9:
						group_idx = 0;
						break;
					case 5:
						group_idx = 2;
						break;
					case 13:
				 		group_idx = 1;
						break;
				}
				if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))   
		{
					for(i = 0; i < SYN_Length; i++)
						ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, SYN[i], bMaskDWord, SYN_group[group_idx][i]);

					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, 0x3FF, channel);
				}
				else  // DualMAC_DualPHY 2G
			{
					for(i = 0; i < SYN_Length; i++)
						ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, SYN[i], bMaskDWord, SYN_group[group_idx][i]);   
					
					ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
				}
			}
			else
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x3FF, channel);
			}	
		tone_idx = n%20;
		if ((n>=PSD_skip_start) && (n<PSD_skip_stop))
		{	
			PSD_report[n] = initial_gain_psd;//SSBT;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD,DBG_LOUD,("PSD:Tone %d skipped \n", n));
		}
		else
		{
			PSD_report_tmp =  GetPSDData(pDM_Odm, idx[tone_idx], initial_gain_psd);

			if ( PSD_report_tmp > PSD_report[n])
				PSD_report[n] = PSD_report_tmp;
				
		}
	}

	PatchDCTone(pDM_Odm, PSD_report, initial_gain_psd);
      
       //----end
	//1 Turn on RX
	//Rx AGC on
	ODM_SetBBReg(pDM_Odm, 0xC70, BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xC7C, BIT20, 1);
	//CCK on
	ODM_SetBBReg(pDM_Odm, rFPGA0_RFMOD, BIT24, 1);
	//1 Turn on TX
	//Resume TX Queue
	ODM_Write1Byte(pDM_Odm, REG_TXPAUSE, 0x00);
	//Turn on 3-wire
	ODM_SetBBReg(pDM_Odm, 0x88c, BIT20|BIT21|BIT22|BIT23, 0x0);
	//1 Restore Current Settings
	//Resume DIG
	pDM_Odm->bDMInitialGainEnable= TRUE;
	//ODM_SetBBReg(pDM_Odm, 0xc50, 0x7F, initial_gain);
	ODM_Write_DIG(pDM_Odm,(u1Byte) initial_gain);
	// restore originl center frequency
	ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, bRFRegOffsetMask, CurrentChannel);
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		if((*(pDM_Odm->pMacPhyMode)==ODM_SMSP)||(*(pDM_Odm->pMacPhyMode)==ODM_DMSP))
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, RF_CHNLBW, bMaskDWord, CurrentChannel);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x25, bMaskDWord, SYN_RF25);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x26, bMaskDWord, SYN_RF26);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x27, bMaskDWord, SYN_RF27);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2B, bMaskDWord, SYN_RF2B);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_B, 0x2C, bMaskDWord, SYN_RF2C);
		}
		else     // DualMAC_DualPHY
		{
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x25, bMaskDWord, SYN_RF25);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x26, bMaskDWord, SYN_RF26);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x27, bMaskDWord, SYN_RF27);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2B, bMaskDWord, SYN_RF2B);
			ODM_SetRFReg(pDM_Odm, ODM_RF_PATH_A, 0x2C, bMaskDWord, SYN_RF2C);
		}
	}
	//Turn on CCA
	ODM_SetBBReg(pDM_Odm, 0xC14, bMaskDWord, RXIQI);
	//Restore RX idle low power
	if(RxIdleLowPwr == TRUE)
		ODM_SetBBReg(pDM_Odm, 0x818, BIT28, 1);
	
	psd_cnt++;
	//gPrint("psd cnt=%d\n", psd_cnt);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PSD, DBG_LOUD,("PSD:psd_cnt = %d \n",psd_cnt));
	if (psd_cnt < ReScan)
	{
		ODM_SetTimer(pDM_Odm, &pRX_HP_Table->PSDTimer, Interval);  //ms
	}
	else
	{	
		psd_cnt = 0;
		for(i=0;i<80;i++)
			RT_TRACE(	ODM_COMP_PSD, DBG_LOUD,("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]));
			//DbgPrint("psd_report[%d]=     %d \n", 2402+i, PSD_report[i]);

		GoodChannelDecision(pDM_Odm, PSD_report, PSD_bitmap,RSSI_BT, PSD_bitmap_memory);

	}
}

void odm_Write_RXHP(
	IN		PVOID			pDM_VOID)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pRXHP_T		pRX_HP_Table = &pDM_Odm->DM_RXHP_Table;
	u4Byte		currentIGI;

	if(pRX_HP_Table->Cur_IGI != pRX_HP_Table->Pre_IGI)
	{
		ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);
	     	ODM_SetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);	
	}
	
	if(pRX_HP_Table->Cur_pw_th != pRX_HP_Table->Pre_pw_th)
{
		ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore2, BIT8|BIT9, pRX_HP_Table->Cur_pw_th);  // RegC54[9:8]=2'b11:  AGC Flow 3
	}

	if(pRX_HP_Table->RXHP_flag == 0)
	{
		pRX_HP_Table->Cur_IGI = 0x20;
	}
	else
	{
		currentIGI = ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0);
		if(currentIGI<0x50)
		{
			ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);
	     		ODM_SetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0, pRX_HP_Table->Cur_IGI);	
		}
	}
	pRX_HP_Table->Pre_IGI = pRX_HP_Table->Cur_IGI;
	pRX_HP_Table->Pre_pw_th = pRX_HP_Table->Cur_pw_th;

}


void odm_RXHP(
	IN		PVOID			pDM_VOID)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) | (DEV_BUS_TYPE == RT_USB_INTERFACE)
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PADAPTER	Adapter =  pDM_Odm->Adapter;
	PMGNT_INFO	pMgntInfo = &(Adapter->MgntInfo);
	pDIG_T		pDM_DigTable = &pDM_Odm->DM_DigTable;
	pRXHP_T		pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
       PFALSE_ALARM_STATISTICS		FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure( pDM_Odm , PhyDM_FalseAlmCnt);
	
	u1Byte              	i, j, sum;
	u1Byte			Is40MHz;
	s1Byte              	Intf_diff_idx, MIN_Intf_diff_idx = 16;   
       s4Byte              	cur_channel;    
       u1Byte              	ch_map_intf_5M[17] = {0};     
       static u4Byte		FA_TH = 0;	
	static u1Byte      	psd_intf_flag = 0;
	static s4Byte      	curRssi = 0;                
       static s4Byte  		preRssi = 0;                                                                
	static u1Byte		PSDTriggerCnt = 1;
	
	u1Byte			RX_HP_enable = (u1Byte)(ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore2, bMaskDWord)>>31);   // for debug!!

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)	
	static s8Byte  		lastTxOkCnt = 0, lastRxOkCnt = 0;  
       s8Byte			curTxOkCnt, curRxOkCnt;
	s8Byte			curTPOkCnt;
	s8Byte			TP_Acc3, TP_Acc5;
	static s8Byte		TP_Buff[5] = {0};
	static u1Byte		pre_state = 0, pre_state_flag = 0;
	static u1Byte		Intf_HighTP_flag = 0, De_counter = 16; 
	static u1Byte		TP_Degrade_flag = 0;
#endif	   
	static u1Byte		LatchCnt = 0;
	
	if(pDM_Odm->SupportICType & (ODM_RTL8723A|ODM_RTL8188E))
		return;
	//AGC RX High Power Mode is only applied on 2G band in 92D!!!
	if(pDM_Odm->SupportICType == ODM_RTL8192D)
	{
		if(*(pDM_Odm->pBandType) != ODM_BAND_2_4G)
			return;
	}

	if(!(pDM_Odm->SupportAbility & ODM_BB_RXHP))
		return;


	//RX HP ON/OFF
	if(RX_HP_enable == 1)
		pRX_HP_Table->RXHP_enable = FALSE;
	else
		pRX_HP_Table->RXHP_enable = TRUE;

	if(pRX_HP_Table->RXHP_enable == FALSE)
	{
		if(pRX_HP_Table->RXHP_flag == 1)
		{
			pRX_HP_Table->RXHP_flag = 0;
			psd_intf_flag = 0;
		}
		return;
	}

#if(DEV_BUS_TYPE == RT_USB_INTERFACE)	
	//2 Record current TP for USB interface
	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast)-lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast)-lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);

	curTPOkCnt = curTxOkCnt+curRxOkCnt;
	TP_Buff[0] = curTPOkCnt;    // current TP  
	TP_Acc3 = PlatformDivision64((TP_Buff[1]+TP_Buff[2]+TP_Buff[3]), 3);
	TP_Acc5 = PlatformDivision64((TP_Buff[0]+TP_Buff[1]+TP_Buff[2]+TP_Buff[3]+TP_Buff[4]), 5);
	
	if(TP_Acc5 < 1000)
		pRX_HP_Table->TP_Mode = Idle_Mode;
	else if((1000 < TP_Acc5)&&(TP_Acc5 < 3750000))
		pRX_HP_Table->TP_Mode = Low_TP_Mode;
	else
		pRX_HP_Table->TP_Mode = High_TP_Mode;

	ODM_RT_TRACE(pDM_Odm, 	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP TP Mode = %d\n", pRX_HP_Table->TP_Mode));
	// Since TP result would be sampled every 2 sec, it needs to delay 4sec to wait PSD processing.
	// When LatchCnt = 0, we would Get PSD result.
	if(TP_Degrade_flag == 1)
	{
		LatchCnt--;
		if(LatchCnt == 0)
		{
			TP_Degrade_flag = 0;
		}
	}
	// When PSD function triggered by TP degrade 20%, and Interference Flag = 1
	// Set a De_counter to wait IGI = upper bound. If time is UP, the Interference flag will be pull down.
	if(Intf_HighTP_flag == 1)
	{
		De_counter--;
		if(De_counter == 0)
		{
			Intf_HighTP_flag = 0;
			psd_intf_flag = 0;
		}
	}
#endif

	//2 AGC RX High Power Mode by PSD only applied to STA Mode
	//3 NOT applied 1. Ad Hoc Mode.
	//3 NOT applied 2. AP Mode
	if ((pMgntInfo->mAssoc) && (!pMgntInfo->mIbss) && (!ACTING_AS_AP(Adapter)))
	{    
		Is40MHz = *(pDM_Odm->pBandWidth);
		curRssi = pDM_Odm->RSSI_Min;
		cur_channel = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A, RF_CHNLBW, 0x0fff) & 0x0f;
		ODM_RT_TRACE(pDM_Odm, 	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP RX HP flag = %d\n", pRX_HP_Table->RXHP_flag));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP FA = %d\n", FalseAlmCnt->Cnt_all));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP cur RSSI = %d, pre RSSI=%d\n", curRssi, preRssi));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP current CH = %d\n", cur_channel));
		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RXHP Is 40MHz = %d\n", Is40MHz));
       	//2 PSD function would be triggered 
       	//3 1. Every 4 sec for PCIE
       	//3 2. Before TP Mode (Idle TP<4kbps) for USB
       	//3 3. After TP Mode (High TP) for USB 
		if((curRssi > 68) && (pRX_HP_Table->RXHP_flag == 0))	// Only RSSI>TH and RX_HP_flag=0 will Do PSD process 
		{
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
			//2 Before TP Mode ==> PSD would be trigger every 4 sec
			if(pRX_HP_Table->TP_Mode == Idle_Mode)		//2.1 less wlan traffic <4kbps
			{
#endif
				if(PSDTriggerCnt == 1)       
				{    	
					odm_PSD_RXHP(pDM_Odm);
					pRX_HP_Table->PSD_func_trigger = 1;
					PSDTriggerCnt = 0;
				}
				else
				{
             				PSDTriggerCnt++;
				}
#if(DEV_BUS_TYPE == RT_USB_INTERFACE)
			}	
			//2 After TP Mode ==> Check if TP degrade larger than 20% would trigger PSD function
			if(pRX_HP_Table->TP_Mode == High_TP_Mode)
			{
				if((pre_state_flag == 0)&&(LatchCnt == 0)) 
				{
					// TP var < 5%
					if((((curTPOkCnt-TP_Acc3)*20)<(TP_Acc3))&&(((curTPOkCnt-TP_Acc3)*20)>(-TP_Acc3)))
					{
						pre_state++;
						if(pre_state == 3)      // hit pre_state condition => consecutive 3 times
						{
							pre_state_flag = 1;
							pre_state = 0;
						}

					}
					else
					{
						pre_state = 0;
					}
				}
				//3 If pre_state_flag=1 ==> start to monitor TP degrade 20%
				if(pre_state_flag == 1)		
				{
					if(((TP_Acc3-curTPOkCnt)*5)>(TP_Acc3))      // degrade 20%
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
					else if(((TP_Buff[2]-curTPOkCnt)*5)>TP_Buff[2])
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
					else if(((TP_Buff[3]-curTPOkCnt)*5)>TP_Buff[3])
					{
						odm_PSD_RXHP(pDM_Odm);
						pRX_HP_Table->PSD_func_trigger = 1;
						TP_Degrade_flag = 1;
						LatchCnt = 2;
						pre_state_flag = 0;
					}
				}
			}
#endif
}

#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
		for (i=0;i<4;i++)
		{
			TP_Buff[4-i] = TP_Buff[3-i];
		}
#endif
		//2 Update PSD bitmap according to PSD report 
		if((pRX_HP_Table->PSD_func_trigger == 1)&&(LatchCnt == 0))
    		{	
           		//2 Separate 80M bandwidth into 16 group with smaller 5M BW.
			for (i = 0 ; i < 16 ; i++)
           		{
				sum = 0;
				for(j = 0; j < 5 ; j++)
                			sum += pRX_HP_Table->PSD_bitmap_RXHP[5*i + j];
            
                		if(sum < 5)
                		{
                			ch_map_intf_5M[i] = 1;  // interference flag
                		}
           		}
			//=============just for debug=========================
			//for(i=0;i<16;i++)
				//DbgPrint("RX HP: ch_map_intf_5M[%d] = %d\n", i, ch_map_intf_5M[i]);
			//===============================================
			//2 Mask target channel 5M index
	    		for(i = 0; i < (4+4*Is40MHz) ; i++)
           		{
				ch_map_intf_5M[cur_channel - (1+2*Is40MHz) + i] = 0;  
           		}
				
           		psd_intf_flag = 0;
	    		for(i = 0; i < 16; i++)
           		{
         			if(ch_map_intf_5M[i] == 1)
	              	{
	              		psd_intf_flag = 1;            // interference is detected!!!	
	              		break;
         			}
	    		}
				
#if (DEV_BUS_TYPE == RT_USB_INTERFACE)
			if(pRX_HP_Table->TP_Mode!=Idle_Mode)
			{
				if(psd_intf_flag == 1)     // to avoid psd_intf_flag always 1
				{
					Intf_HighTP_flag = 1;
					De_counter = 32;     // 0x1E -> 0x3E needs 32 times by each IGI step =1
				}
			}
#endif
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP psd_intf_flag = %d\n", psd_intf_flag));
			//2 Distance between target channel and interference
           		for(i = 0; i < 16; i++)
          		{
				if(ch_map_intf_5M[i] == 1)
                		{
					Intf_diff_idx = ((cur_channel+Is40MHz-(i+1))>0) ? (s1Byte)(cur_channel-2*Is40MHz-(i-2)) : (s1Byte)((i+1)-(cur_channel+2*Is40MHz));  
                      		if(Intf_diff_idx < MIN_Intf_diff_idx)
						MIN_Intf_diff_idx = Intf_diff_idx;    // the min difference index between interference and target
		  		}
	    		}
	    		ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP MIN_Intf_diff_idx = %d\n", MIN_Intf_diff_idx)); 
			//2 Choose False Alarm Threshold
			switch (MIN_Intf_diff_idx){
      				case 0: 
	   			case 1:
	        		case 2:
	        		case 3:	 	 
                 			FA_TH = FA_RXHP_TH1;  
                     		break;
	        		case 4:				// CH5
	        		case 5:				// CH6
		   			FA_TH = FA_RXHP_TH2;	
               			break;
	        		case 6:				// CH7
	        		case 7:				// CH8
		      			FA_TH = FA_RXHP_TH3;
                    			break; 
               		case 8:				// CH9
	        		case 9:				//CH10
		      			FA_TH = FA_RXHP_TH4;
                    			break; 	
	        		case 10:
	        		case 11:
	        		case 12:
	        		case 13:	 
	        		case 14:
	      			case 15:	 	
		      			FA_TH = FA_RXHP_TH5;
                    			break;  		
       		}	
			ODM_RT_TRACE(pDM_Odm,	ODM_COMP_RXHP, ODM_DBG_LOUD, ("RX HP FA_TH = %d\n", FA_TH));
			pRX_HP_Table->PSD_func_trigger = 0;
		}
		//1 Monitor RSSI variation to choose the suitable IGI or Exit AGC RX High Power Mode
         	if(pRX_HP_Table->RXHP_flag == 1)
         	{
              	if ((curRssi > 80)&&(preRssi < 80))
              	{ 
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_1;
              	}
              	else if ((curRssi < 80)&&(preRssi > 80))
              	{
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
			}
	       	else if ((curRssi > 72)&&(preRssi < 72))
	      		{
                		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
	       	}
              	else if ((curRssi < 72)&&( preRssi > 72))
	     		{
                   		pRX_HP_Table->Cur_IGI = LNA_Low_Gain_3;
	       	}
	       	else if (curRssi < 68)		 //RSSI is NOT large enough!!==> Exit AGC RX High Power Mode
	       	{
                   		pRX_HP_Table->Cur_pw_th = pw_th_10dB;
				pRX_HP_Table->RXHP_flag = 0;    // Back to Normal DIG Mode		  
				psd_intf_flag = 0;
			}
		}
		else    // pRX_HP_Table->RXHP_flag == 0
		{
			//1 Decide whether to enter AGC RX High Power Mode
			if ((curRssi > 70) && (psd_intf_flag == 1) && (FalseAlmCnt->Cnt_all > FA_TH) &&  
				(pDM_DigTable->CurIGValue == pDM_DigTable->rx_gain_range_max))
			{
             			if (curRssi > 80)
             			{
					pRX_HP_Table->Cur_IGI = LNA_Low_Gain_1;
				}
				else if (curRssi > 72) 
              		{
               			pRX_HP_Table->Cur_IGI = LNA_Low_Gain_2;
				}
             			else
            			{
                   			pRX_HP_Table->Cur_IGI = LNA_Low_Gain_3;
				}
           			pRX_HP_Table->Cur_pw_th = pw_th_16dB;		//RegC54[9:8]=2'b11: to enter AGC Flow 3
				pRX_HP_Table->First_time_enter = TRUE;
				pRX_HP_Table->RXHP_flag = 1;    //	RXHP_flag=1: AGC RX High Power Mode, RXHP_flag=0: Normal DIG Mode
			}
		}
		preRssi = curRssi; 
		odm_Write_RXHP(pDM_Odm);	
	}
#endif //#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN))
#endif //#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) | (DEV_BUS_TYPE == RT_USB_INTERFACE)
}


VOID
odm_PSD_RXHPCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	pRXHP_T			pRX_HP_Table  = &pDM_Odm->DM_RXHP_Table;
	
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
	#if USE_WORKITEM
	ODM_ScheduleWorkItem(&pRX_HP_Table->PSDTimeWorkitem);
	#else
	odm_PSD_RXHP(pDM_Odm);
	#endif
#else
	ODM_ScheduleWorkItem(&pRX_HP_Table->PSDTimeWorkitem);
#endif
	
	}

VOID
odm_PSD_RXHPWorkitemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	
	odm_PSD_RXHP(pDM_Odm);
}

#endif //#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)


 
