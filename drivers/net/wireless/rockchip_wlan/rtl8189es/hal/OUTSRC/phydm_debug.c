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


VOID 
PHYDM_InitDebugSetting(
	IN		PDM_ODM_T		pDM_Odm
	)
{
pDM_Odm->DebugLevel				= 	ODM_DBG_TRACE;

pDM_Odm->DebugComponents			= 
\
#if DBG
//BB Functions
//									ODM_COMP_DIG					|
//									ODM_COMP_RA_MASK				|
//									ODM_COMP_DYNAMIC_TXPWR		|
//									ODM_COMP_FA_CNT				|
//									ODM_COMP_RSSI_MONITOR			|
//									ODM_COMP_CCK_PD				|
//									ODM_COMP_ANT_DIV				|
//									ODM_COMP_PWR_SAVE				|
//									ODM_COMP_PWR_TRAIN			|
//									ODM_COMP_RATE_ADAPTIVE		|
//									ODM_COMP_PATH_DIV				|
//									ODM_COMP_DYNAMIC_PRICCA		|
//									ODM_COMP_RXHP					|
//									ODM_COMP_MP 					|
//									ODM_COMP_CFO_TRACKING		|
//									ODM_COMP_ACS					|
//									PHYDM_COMP_ADAPTIVITY			|

//MAC Functions
//									ODM_COMP_EDCA_TURBO			|
//									ODM_COMP_EARLY_MODE			|
//RF Functions
//									ODM_COMP_TX_PWR_TRACK		|
//									ODM_COMP_RX_GAIN_TRACK		|
//									ODM_COMP_CALIBRATION			|
//Common
//									ODM_COMP_COMMON				|
//									ODM_COMP_INIT					|
//									ODM_COMP_PSD					|
#endif
									0;
}

#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)

static u1Byte	BbDbgBuf[BB_TMP_BUF_SIZE];
VOID
phydm_BB_Debug_Info(IN PDM_ODM_T pDM_Odm)
{

	u1Byte	RX_HT_BW, RX_VHT_BW, RXSC, RX_HT, RX_BW;
	static u1Byte vRX_BW ;
	u4Byte	value32, value32_1, value32_2, value32_3;
	s4Byte	SFO_A, SFO_B, SFO_C, SFO_D;
	s4Byte	LFO_A, LFO_B, LFO_C, LFO_D;
	static u1Byte	MCSS,Tail,Parity,rsv,vrsv,idx,smooth,htsound,agg,stbc,vstbc,fec,fecext,sgi,sgiext,htltf,vgid,vNsts,vtxops,vrsv2,vbrsv,bf,vbcrc;
	static u2Byte	HLength,htcrc8,Length;
	static u2Byte vpaid;
	static u2Byte	vLength,vhtcrc8,vMCSS,vTail,vbTail;
	static u1Byte	HMCSS,HRX_BW;

	
	u1Byte    pwDB;
	s1Byte    RXEVM_0, RXEVM_1, RXEVM_2 ;
	u1Byte    RF_gain_pathA, RF_gain_pathB, RF_gain_pathC, RF_gain_pathD;
	u1Byte    RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD;
       s4Byte    sig_power;
       const char *RXHT_table[] = {"legacy", "HT", "VHT"};
	const char *BW_table[] = {"20M", "40M", "80M"};
	const char *RXSC_table[] = {"duplicate/full bw", "usc20-1", "lsc20-1", "usc20-2", "lsc20-2",  "usc40", "lsc40"};

	const char *L_rate[]={"6M","9M","12M","18M","24M","36M","48M","54M"}; 

	
	/*
	const double evm_comp_20M = 0.579919469776867; //10*log10(64.0/56.0)
	const double evm_comp_40M = 0.503051183113957; //10*log10(128.0/114.0)
	const double evm_comp_80M = 0.244245993314183; //10*log10(256.0/242.0)
	const double evm_comp_160M = 0.244245993314183; //10*log10(512.0/484.0)
       */

	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		return;

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s \n", "BB Report Info");
	DCMD_Printf(BbDbgBuf);

       //BW & Mode Detection
	///////////////////////////////////////////////////////			
	value32 = ODM_GetBBReg(pDM_Odm, 0xf80 ,bMaskDWord);
	value32_2 =value32; 
	RX_HT_BW = (u1Byte)(value32&0x1)	;
	RX_VHT_BW = (u1Byte)((value32>>1)&0x3);
	RXSC = (u1Byte)(value32&0x78);
	value32_1= (value32&0x180)>>7;
	RX_HT = (u1Byte)(value32_1);
	/*		
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "F80", value32_2);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT_BW", RX_HT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_VHT_BW", RX_VHT_BW);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_SC", RXSC);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "RX_HT", RX_HT);
	DCMD_Printf(BbDbgBuf);
	*/
	
	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  RX_HT:%s ", RXHT_table[RX_HT]);
	//DCMD_Printf(BbDbgBuf);
	RX_BW = 0;

	if(RX_HT == 2)
	{
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: VHT Mode");
		DCMD_Printf(BbDbgBuf);
		if(RX_VHT_BW==0)
		{
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		}	
		else if(RX_VHT_BW==1)
		{
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		}
		else
		{
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=80M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_VHT_BW;
	}
	else if(RX_HT == 1)
	{
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: HT Mode");
		DCMD_Printf(BbDbgBuf);
		if(RX_HT_BW==0)
		{
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=20M");
			DCMD_Printf(BbDbgBuf);
		}	
		else if(RX_HT_BW==1)
		{
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "     BW=40M");
			DCMD_Printf(BbDbgBuf);
		}
		RX_BW = RX_HT_BW;
	}
	else
	{
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  Mode: Legeacy Mode");
		DCMD_Printf(BbDbgBuf);
	}

	if(RX_HT !=0)
	{
		if(RXSC==0)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  duplicate/full bw");
		else if(RXSC==1)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-1");
		else if(RXSC==2)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-1");
		else if(RXSC==3)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc20-2");
		else if(RXSC==4)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc20-2");
		else if(RXSC==9)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  usc40");
		else if(RXSC==10)
			rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  lsc40");
		DCMD_Printf(BbDbgBuf);
	}
	/*
	if(RX_HT == 2){
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_VHT_BW]);
		RX_BW = RX_VHT_BW;
		}
	else if(RX_HT == 1){		
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  BW:%s", BW_table[RX_HT_BW]);
		RX_BW = RX_HT_BW;
		}
	else
		rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE,  "");
	
	DCMD_Printf(BbDbgBuf);	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "  RXSC:%s", RXSC_table[RXSC]);		
	DCMD_Printf(BbDbgBuf);
	*/
	///////////////////////////////////////////////////////	
	
//	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "dB Conversion: 10log(65)", ODM_PWdB_Conversion(65,10,0));
//	DCMD_Printf(BbDbgBuf);

        // RX signal power and AGC related info
        ///////////////////////////////////////////////////////
       value32 = ODM_GetBBReg(pDM_Odm, 0xF90 ,bMaskDWord);
	pwDB = (u1Byte) ((value32 & bMaskByte1) >> 8);
	pwDB=pwDB>>1;
	sig_power = -110+pwDB;
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power);
	DCMD_Printf(BbDbgBuf);


	value32 = ODM_GetBBReg(pDM_Odm, 0xd14 ,bMaskDWord);
	RX_SNR_pathA = (u1Byte)(value32&0xFF)>>1;
	RF_gain_pathA = (s1Byte) ((value32 & bMaskByte1) >> 8);
	RF_gain_pathA *=2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xd54 ,bMaskDWord);
	RX_SNR_pathB = (u1Byte)(value32&0xFF)>>1;
	RF_gain_pathB = (s1Byte) ((value32 & bMaskByte1) >> 8);
	RF_gain_pathB *=2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xd94 ,bMaskDWord);
	RX_SNR_pathC = (u1Byte)(value32&0xFF)>>1;
	RF_gain_pathC = (s1Byte) ((value32 & bMaskByte1) >> 8);
	RF_gain_pathC *=2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xdd4 ,bMaskDWord);
	RX_SNR_pathD = (u1Byte)(value32&0xFF)>>1;
	RF_gain_pathD = (s1Byte) ((value32 & bMaskByte1) >> 8);
	RF_gain_pathD *=2;
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", RF_gain_pathA, RF_gain_pathA, RF_gain_pathC, RF_gain_pathD);
	DCMD_Printf(BbDbgBuf);	
        ///////////////////////////////////////////////////////

	// RX Counter related info
        ///////////////////////////////////////////////////////	
	value32 = ODM_GetBBReg(pDM_Odm, 0xF08 ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM CCA Counter", ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xFD0 ,bMaskDWord);
       rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "OFDM SBD Fail Counter", value32&0xFFFF);
	DCMD_Printf(BbDbgBuf);
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xFC4 ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xFCC ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "CCK CCA Counter", value32&0xFFFF);
	DCMD_Printf(BbDbgBuf);
	
	value32 = ODM_GetBBReg(pDM_Odm, 0xFBC ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "LSIG (\"Parity Fail\"/\"Rate Illegal\") Counter", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);

	value32_1 = ODM_GetBBReg(pDM_Odm, 0xFC8 ,bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xFC0 ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2&0xFFFF0000)>>16), value32_1&0xFFFF);
	DCMD_Printf(BbDbgBuf);
	///////////////////////////////////////////////////////
	
	// PostFFT related info
	///////////////////////////////////////////////////////

	value32 = ODM_GetBBReg(pDM_Odm, 0xF8c ,bMaskDWord);
	RXEVM_0 = (s1Byte) ((value32 & bMaskByte2) >> 16);
	RXEVM_0 /=2;
	if(RXEVM_0 < -63)
		RXEVM_0=0;
	
	DCMD_Printf(BbDbgBuf);
	RXEVM_1 = (s1Byte) ((value32 & bMaskByte3) >> 24);
	RXEVM_1 /=2;
	value32 = ODM_GetBBReg(pDM_Odm, 0xF88 ,bMaskDWord);
	RXEVM_2 = (s1Byte) ((value32 & bMaskByte2) >> 16);
	RXEVM_2 /=2;

	if(RXEVM_1 < -63)
		RXEVM_1=0;
	if(RXEVM_2 < -63)
		RXEVM_2=0;
	
	/*
	if(RX_BW == 0){
		RXEVM_0 -= evm_comp_20M;
		RXEVM_1 -= evm_comp_20M;
		RXEVM_2 -= evm_comp_20M;		
		}
	else if(RX_BW == 1){
		RXEVM_0 -= evm_comp_40M;
		RXEVM_1 -= evm_comp_40M;
		RXEVM_2 -= evm_comp_40M;		
		}
	else if (RX_BW == 2){
		RXEVM_0 -= evm_comp_80M;
		RXEVM_1 -= evm_comp_80M;
		RXEVM_2 -= evm_comp_80M;		
		}
		*/
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2);
	DCMD_Printf(BbDbgBuf);

//	value32 = ODM_GetBBReg(pDM_Odm, 0xD14 ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", RX_SNR_pathA, RX_SNR_pathB, RX_SNR_pathC, RX_SNR_pathD);
	DCMD_Printf(BbDbgBuf);
//	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "B_RXSNR", (value32&0xFF00)>>9);
//	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xF8C ,bMaskDWord);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32&0xFFFF, ((value32&0xFFFF0000)>>16));
	DCMD_Printf(BbDbgBuf);
	///////////////////////////////////////////////////////			
	
	//BW & Mode Detection

	//Reset Page F Counter
	ODM_SetBBReg(pDM_Odm, 0xB58 ,BIT0, 1);
	ODM_SetBBReg(pDM_Odm, 0xB58 ,BIT0, 0);
	
	//CFO Report Info
	//Short CFO
	value32 = ODM_GetBBReg(pDM_Odm, 0xd0c ,bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd4c ,bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd8c ,bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdcc ,bMaskDWord);

	SFO_A=(s4Byte)(value32&bMask12Bits);
	SFO_B=(s4Byte)(value32_1&bMask12Bits);
	SFO_C=(s4Byte)(value32_2&bMask12Bits);
	SFO_D=(s4Byte)(value32_3&bMask12Bits);

	LFO_A=(s4Byte)(value32>>16);
	LFO_B=(s4Byte)(value32_1>>16);
	LFO_C=(s4Byte)(value32_2>>16);
	LFO_D=(s4Byte)(value32_3>>16);

	//SFO 2's to dec
	if(SFO_A >2047)
	{
		SFO_A=SFO_A-4096;
	}
	SFO_A=(SFO_A*312500)/2048;
	
	if(SFO_B >2047)
	{
		SFO_B=SFO_B-4096;
	}
	SFO_B=(SFO_B*312500)/2048;
	if(SFO_C >2047)
	{
		SFO_C=SFO_C-4096;
	}
	SFO_C=(SFO_C*312500)/2048;
	if(SFO_D >2047)
	{
		SFO_D=SFO_D-4096;
	}
	SFO_D=(SFO_D*312500)/2048;

	//LFO 2's to dec
	
	if(LFO_A >4095)
	{
		LFO_A=LFO_A-8192;
	}
	
	if(LFO_B >4095)
	{
		LFO_B=LFO_B-8192;
	}

	if(LFO_C>4095)
	{
		LFO_C=LFO_C-8192;
	}

	if(LFO_D >4095)
	{
		LFO_D=LFO_D-8192;
	}
	LFO_A=LFO_A*312500/4096;
	LFO_B=LFO_B*312500/4096;
	LFO_C=LFO_C*312500/4096;
	LFO_D=LFO_D*312500/4096;
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "CFO Report Info");
	DCMD_Printf(BbDbgBuf);
	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Short CFO(Hz) <A/B/C/D>", SFO_A,SFO_B,SFO_C,SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Long CFO(Hz) <A/B/C/D>", LFO_A,LFO_B,LFO_C,LFO_D);
	DCMD_Printf(BbDbgBuf);

	//SCFO
	value32 = ODM_GetBBReg(pDM_Odm, 0xd10 ,bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd50 ,bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd90 ,bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdd0 ,bMaskDWord);

	SFO_A=(s4Byte)(value32&0x7ff);
	SFO_B=(s4Byte)(value32_1&0x7ff);
	SFO_C=(s4Byte)(value32_2&0x7ff);
	SFO_D=(s4Byte)(value32_3&0x7ff);

	if(SFO_A >1023)
	{
		SFO_A=SFO_A-2048;
	}
	
	if(SFO_B >2047)
	{
		SFO_B=SFO_B-4096;
}

	if(SFO_C >2047)
	{
		SFO_C=SFO_C-4096;
	}

	if(SFO_D >2047)
	{
		SFO_D=SFO_D-4096;
	}
	
	SFO_A=SFO_A*312500/1024;
	SFO_B=SFO_B*312500/1024;
	SFO_C=SFO_C*312500/1024;
	SFO_D=SFO_D*312500/1024;

	LFO_A=(s4Byte)(value32>>16);
	LFO_B=(s4Byte)(value32_1>>16);
	LFO_C=(s4Byte)(value32_2>>16);
	LFO_D=(s4Byte)(value32_3>>16);

	if(LFO_A >4095)
	{
		LFO_A=LFO_A-8192;
	}
	
	if(LFO_B >4095)
	{
		LFO_B=LFO_B-8192;
	}

	if(LFO_C>4095)
	{
		LFO_C=LFO_C-8192;
	}

	if(LFO_D >4095)
	{
		LFO_D=LFO_D-8192;
	}
	LFO_A=LFO_A*312500/4096;
	LFO_B=LFO_B*312500/4096;
	LFO_C=LFO_C*312500/4096;
	LFO_D=LFO_D*312500/4096;
	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  Value SCFO(Hz) <A/B/C/D>", SFO_A,SFO_B,SFO_C,SFO_D);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  ACQ CFO(Hz) <A/B/C/D>", LFO_A,LFO_B,LFO_C,LFO_D);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xd14 ,bMaskDWord);
	value32_1 = ODM_GetBBReg(pDM_Odm, 0xd54 ,bMaskDWord);
	value32_2 = ODM_GetBBReg(pDM_Odm, 0xd94 ,bMaskDWord);
	value32_3 = ODM_GetBBReg(pDM_Odm, 0xdd4 ,bMaskDWord);

	LFO_A=(s4Byte)(value32>>16);
	LFO_B=(s4Byte)(value32_1>>16);
	LFO_C=(s4Byte)(value32_2>>16);
	LFO_D=(s4Byte)(value32_3>>16);

	if(LFO_A >4095)
	{
		LFO_A=LFO_A-8192;
	}
	
	if(LFO_B >4095)
	{
		LFO_B=LFO_B-8192;
	}

	if(LFO_C>4095)
	{
		LFO_C=LFO_C-8192;
	}

	if(LFO_D >4095)
	{
		LFO_D=LFO_D-8192;
	}
	LFO_A=LFO_A*312500/4096;
	LFO_B=LFO_B*312500/4096;
	LFO_C=LFO_C*312500/4096;
	LFO_D=LFO_D*312500/4096;
	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d / %d/ %d/ %d", "  End CFO(Hz) <A/B/C/D>", LFO_A,LFO_B,LFO_C,LFO_D);
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xf20 ,bMaskDWord);   //L SIG
	
	Tail=(u1Byte)((value32&0xfc0000)>>16);
	Parity = (u1Byte)((value32&0x20000)>>16);
	Length =(u2Byte)((value32&0x1ffe00)>>8);
	rsv = (u1Byte)(value32&0x10);
	MCSS=(u1Byte)(value32&0x0f);

	switch(MCSS)
	{
		case 0x0b:
			idx=0;
		break;
		case 0x0f:
			idx=1;
		break;
		case 0x0a:
			idx=2;
		break;
		case 0x0e:
			idx=3;
		break;
		case 0x09:
			idx=4;
		break;
		case 0x08:
			idx=5;
		break;
		case 0x0c:
			idx=6;
		break;
		default:
			idx=6;
		break;
			
	}

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "L-SIG");
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n   Rate:%s", L_rate[idx]);		
	DCMD_Printf(BbDbgBuf);	

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x/ %x /%x", "  Rsv/Length/Parity",rsv,RX_BW,Length);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG1");
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c ,bMaskDWord);   //HT SIG
	if(RX_HT == 1)
	{
	
		HMCSS=(u1Byte)(value32&0x7F);
		HRX_BW = (u1Byte)(value32&0x80);
		HLength =(u2Byte)((value32>>8)&0xffff);
	}
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x", "  MCS/BW/Length",HMCSS,HRX_BW,HLength);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "HT-SIG2");
	DCMD_Printf(BbDbgBuf);

	value32 = ODM_GetBBReg(pDM_Odm, 0xf30 ,bMaskDWord);   //HT SIG

	if(RX_HT == 1)
	{
		smooth = (u1Byte)(value32&0x01);
		htsound =  (u1Byte)(value32&0x02);
		rsv=(u1Byte)(value32&0x04);
		agg =(u1Byte)(value32&0x08);
		stbc =(u1Byte)(value32&0x30);
		fec=(u1Byte)(value32&0x40);
		sgi=(u1Byte)(value32&0x80);
		htltf=(u1Byte)((value32&0x300)>>8);
		htcrc8=(u2Byte)((value32&0x3fc00)>>8);
		Tail=(u1Byte)((value32&0xfc0000)>>16);

	
	}
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x", "  Smooth/NoSound/Rsv/Aggregate/STBC/LDPC",smooth,htsound,rsv,agg,stbc,fec);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x", "  SGI/E-HT-LTFs/CRC/Tail",sgi,htltf,htcrc8,Tail);
	DCMD_Printf(BbDbgBuf);

	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A1");
	DCMD_Printf(BbDbgBuf);
	value32 = ODM_GetBBReg(pDM_Odm, 0xf2c ,bMaskDWord);   //VHT SIG A1
	if(RX_HT == 2)
	{
		//value32 = ODM_GetBBReg(pDM_Odm, 0xf2c ,bMaskDWord);   //VHT SIG A1
		vRX_BW=(u1Byte)(value32&0x03);
		vrsv=(u1Byte)(value32&0x04);
		vstbc =(u1Byte)(value32&0x08);
		vgid = (u1Byte)((value32&0x3f0)>>4);
		vNsts = (u1Byte)(((value32&0x1c00)>>8)+1);
		vpaid = (u2Byte)(value32&0x3fe);
		vtxops =(u1Byte)((value32&0x400000)>>20);
		vrsv2 = (u1Byte)((value32&0x800000)>>20);
	}

	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F2C", value32);
	//DCMD_Printf(BbDbgBuf);

	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x /%x /%x", "  BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2",vRX_BW,vrsv,vstbc,vgid,vNsts,vpaid,vtxops,vrsv2);
	DCMD_Printf(BbDbgBuf);
	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-A2");
	DCMD_Printf(BbDbgBuf);
	value32 = ODM_GetBBReg(pDM_Odm, 0xf30 ,bMaskDWord);   //VHT SIG


	if(RX_HT == 2)
	{
		//value32 = ODM_GetBBReg(pDM_Odm, 0xf30 ,bMaskDWord);   //VHT SIG

		//sgi=(u1Byte)(value32&0x01);
		sgiext =(u1Byte)(value32&0x03);
		//fec = (u1Byte)(value32&0x04);
		fecext = (u1Byte)(value32&0x0C);

		vMCSS =(u1Byte)(value32&0xf0); 
		bf = (u1Byte)((value32&0x100)>>8); 
		vrsv =(u1Byte)((value32&0x200)>>8);  
		vhtcrc8=(u2Byte)((value32&0x3fc00)>>8);
		vTail=(u1Byte)((value32&0xfc0000)>>16);
	}
	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F30", value32);
	//DCMD_Printf(BbDbgBuf);
	
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/ %x/ %x/ %x", "  SGI/FEC/MCS/BF/Rsv/CRC/Tail",sgiext,fecext,vMCSS,bf,vrsv,vhtcrc8,vTail);
	DCMD_Printf(BbDbgBuf);

	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "VHT-SIG-B");
	DCMD_Printf(BbDbgBuf);
	value32 = ODM_GetBBReg(pDM_Odm, 0xf34 ,bMaskDWord);   //VHT SIG
	{
		vLength=(u2Byte)(value32&0x1fffff);
		vbrsv = (u1Byte)((value32&0x600000)>>20);
		vbTail =(u2Byte)((value32&0x1f800000)>>20);
		vbcrc = (u1Byte)((value32&0x80000000)>>28);
	
	}
	//rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x", "F34", value32);
	//DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %x / %x/ %x/ %x/", "  Length/Rsv/Tail/CRC",vLength,vbrsv,vbTail,vbcrc);
	DCMD_Printf(BbDbgBuf);
	
		
}


VOID phydm_BasicProfile(
	IN		PVOID			pDM_VOID
	)
{
        PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
        PADAPTER		       Adapter = pDM_Odm->Adapter;
        char* Cut = NULL;
	char* ICType = NULL;

        rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n%-35s", "% Basic Profile %");
	DCMD_Printf(BbDbgBuf);

	if(pDM_Odm->SupportICType == ODM_RTL8192C)
		ICType = "RTL8192C";
	else if(pDM_Odm->SupportICType == ODM_RTL8192D)
		ICType = "RTL8192D";
	else if(pDM_Odm->SupportICType == ODM_RTL8723A)
		ICType = "RTL8723A";
	else if(pDM_Odm->SupportICType == ODM_RTL8188E)
		ICType = "RTL8188E";
	else if(pDM_Odm->SupportICType == ODM_RTL8812)
		ICType = "RTL8812A";
	else if(pDM_Odm->SupportICType == ODM_RTL8821)
		ICType = "RTL8821A";
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
		ICType = "RTL8192E";
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
		ICType = "RTL8723B";
	else if(pDM_Odm->SupportICType == ODM_RTL8814A)
		ICType = "RTL8814A";
	else if(pDM_Odm->SupportICType == ODM_RTL8881A)
		ICType = "RTL8881A";
	else if(pDM_Odm->SupportICType == ODM_RTL8821B)
		ICType = "RTL8821B";
	else if(pDM_Odm->SupportICType == ODM_RTL8822B)
		ICType = "RTL8822B";
	else if(pDM_Odm->SupportICType == ODM_RTL8703B)
		ICType = "RTL8703B";
	else if(pDM_Odm->SupportICType == ODM_RTL8195A)
		ICType = "RTL8195A";
	else if(pDM_Odm->SupportICType == ODM_RTL8188F)
		ICType = "RTL8188F";
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s (MP Chip: %s)","IC Type", ICType, pDM_Odm->bIsMPChip?"Yes":"No");		
	DCMD_Printf(BbDbgBuf);

        if(pDM_Odm->CutVersion==ODM_CUT_A)
            Cut = "A";
        else if(pDM_Odm->CutVersion==ODM_CUT_B)
            Cut = "B";
        else if(pDM_Odm->CutVersion==ODM_CUT_C)
            Cut = "C";
        else if(pDM_Odm->CutVersion==ODM_CUT_D)
            Cut = "D";
        else if(pDM_Odm->CutVersion==ODM_CUT_E)
            Cut = "E";
        else if(pDM_Odm->CutVersion==ODM_CUT_F)
            Cut = "F";
        else if(pDM_Odm->CutVersion==ODM_CUT_I)
            Cut = "I";
        rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Cut Version", Cut);		
	DCMD_Printf(BbDbgBuf);
        rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %d","PHY Parameter Version", ODM_GetHWImgVersion(pDM_Odm));		
	DCMD_Printf(BbDbgBuf);
        rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %d (Subversion: %d)","FW Version", Adapter->MgntInfo.FirmwareVersion, Adapter->MgntInfo.FirmwareSubVersion);		
	DCMD_Printf(BbDbgBuf);

	//1 PHY DM Version List
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n%-35s","% PHYDM Version %");
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Adaptivity", ADAPTIVITY_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","DIG", DIG_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Dynamic BB PowerSaving", DYNAMIC_BBPWRSAV_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","CFO Tracking", CFO_TRACKING_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Antenna Diversity", ANTDIV_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Power Tracking", POWRTRACKING_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Dynamic TxPower", DYNAMIC_TXPWR_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","RA Info", RAINFO_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Antenna Detection", ANTDECT_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Auto Channel Selection", ACS_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","EDCA Turbo", EDCATURBO_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","Path Diversity", PATHDIV_VERSION);
	DCMD_Printf(BbDbgBuf);
	rsprintf(BbDbgBuf, BT_TMP_BUF_SIZE, "\r\n  %-35s: %s","RxHP", RXHP_VERSION);
	DCMD_Printf(BbDbgBuf);

}
#endif
VOID
phydm_BasicDbgMessage
(
	IN		PVOID			pDM_VOID
	)
{
#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
        PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PFALSE_ALARM_STATISTICS FalseAlmCnt = (PFALSE_ALARM_STATISTICS)PhyDM_Get_Structure( pDM_Odm , PHYDM_FALSEALMCNT);
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_BasicDbgMsg==>\n"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("bLinked = %d, RSSI_Min = %d, CurrentIGI = 0x%x \n",
		pDM_Odm->bLinked, pDM_Odm->RSSI_Min, pDM_DigTable->CurIGValue) );
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("Cnt_Cck_fail = %d, Cnt_Ofdm_fail = %d, Total False Alarm = %d\n",	
		FalseAlmCnt->Cnt_Cck_fail, FalseAlmCnt->Cnt_Ofdm_fail, FalseAlmCnt->Cnt_all));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RxRate = 0x%x, RSSI_A = %d, RSSI_B = %d\n", 
		pDM_Odm->RxRate, pDM_Odm->RSSI_A, pDM_Odm->RSSI_B));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI_C = %d, RSSI_D = %d\n", pDM_Odm->RSSI_C, pDM_Odm->RSSI_D));
#endif
}

#if( DM_ODM_SUPPORT_TYPE & ODM_CE)
struct _PHYDM_COMMAND {
	char name[16];
	u1Byte id;
};

enum PHYDM_CMD_ID {
	PHYDM_ANTDIV,
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"antdiv", PHYDM_ANTDIV},
};

s4Byte
PhyDM_Cmd(
	IN PDM_ODM_T	pDM_Odm,
	IN char		*input,
	IN u4Byte	in_len,
	IN u1Byte	flag,
	OUT char	*output,
	IN u4Byte	out_len
	)
{
	u4Byte used = 0;

	if (flag == 0) {
		if (out_len > used)
			used += snprintf(output+used, out_len-used, "GET, nothing to print\n");
	} else {
		char *token;
		u1Byte id = 0;
		int var = 0;

		token = strsep(&input, ", ");
		if (token) {
			int n, i;
			n = sizeof(phy_dm_ary)/sizeof(struct _PHYDM_COMMAND);
			for (i = 0; i < n; i++) {
				if (strcmp(phy_dm_ary[i].name, token) == 0) {
					id = phy_dm_ary[i].id;
					break;
				}
			}
			if (i == n) {
				if (out_len > used)
					used += snprintf(output+used, out_len-used, "SET, command not found!\n");
				goto exit;
			}
		}

		switch (id) {
		case PHYDM_ANTDIV:
			token = strsep(&input, ", ");
			sscanf(token, "%d", &var);
			if (out_len > used)
				used += snprintf(output+used, out_len-used, "SET, old antdiv_select=%d\n", pDM_Odm->antdiv_select);
			pDM_Odm->antdiv_select = var;
			if (out_len > used)
				used += snprintf(output+used, out_len-used, "SET, new antdiv_select=%d\n", pDM_Odm->antdiv_select);
			break;

		default:
			if (out_len > used)
				used += snprintf(output+used, out_len-used, "SET, unknown command!\n");
			break;
		}
	}

exit:
	return 0;
}
#endif
