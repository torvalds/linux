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

#include "mp_precomp.h"
#include "phydm_precomp.h"

#define READ_AND_CONFIG_MP(ic, txt) (ODM_ReadAndConfig_MP_##ic##txt(pDM_Odm))
#define READ_AND_CONFIG_TC(ic, txt) (ODM_ReadAndConfig_TC_##ic##txt(pDM_Odm))


#if (PHYDM_TESTCHIP_SUPPORT == 1)
#define READ_AND_CONFIG(ic, txt) do {\
                                            if (pDM_Odm->bIsMPChip)\
                                    		    READ_AND_CONFIG_MP(ic,txt);\
                                            else\
                                                READ_AND_CONFIG_TC(ic,txt);\
                                    } while(0)
#else
  #define READ_AND_CONFIG     READ_AND_CONFIG_MP
#endif


#define READ_FIRMWARE_MP(ic, txt) 		(ODM_ReadFirmware_MP_##ic##txt(pDM_Odm, pFirmware, pSize))
#define READ_FIRMWARE_TC(ic, txt) 		(ODM_ReadFirmware_TC_##ic##txt(pDM_Odm, pFirmware, pSize))		

#if (PHYDM_TESTCHIP_SUPPORT == 1)
#define READ_FIRMWARE(ic, txt) do {\
						if (pDM_Odm->bIsMPChip)\
							READ_FIRMWARE_MP(ic,txt);\
						else\
							READ_FIRMWARE_TC(ic,txt);\
					} while(0) 
#else
#define READ_FIRMWARE     READ_FIRMWARE_MP
#endif
						
#define GET_VERSION_MP(ic, txt) 		(ODM_GetVersion_MP_##ic##txt())
#define GET_VERSION_TC(ic, txt) 		(ODM_GetVersion_TC_##ic##txt())

#if (PHYDM_TESTCHIP_SUPPORT == 1)
#define GET_VERSION(ic, txt) (pDM_Odm->bIsMPChip?GET_VERSION_MP(ic,txt):GET_VERSION_TC(ic,txt))
#else
#define GET_VERSION(ic, txt) GET_VERSION_MP(ic, txt)
#endif

u1Byte
odm_QueryRxPwrPercentage(
	IN		s1Byte		AntPower
	)
{
	if ((AntPower <= -100) || (AntPower >= 20))
		return	0;
	else if (AntPower >= 0)
		return	100;
	else
		return	(100 + AntPower);
}


//
// 2012/01/12 MH MOve some signal strength smooth method to MP HAL layer.
// IF other SW team do not support the feature, remove this section.??
//
s4Byte
odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Lenovo(	
	IN OUT PDM_ODM_T pDM_Odm,
	s4Byte CurrSig 
)
{	
	s4Byte RetSig = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//if(pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) 
	{
		// Step 1. Scale mapping.
		// 20100611 Joseph: Re-tunning RSSI presentation for Lenovo.
		// 20100426 Joseph: Modify Signal strength mapping.
		// This modification makes the RSSI indication similar to Intel solution.
		// 20100414 Joseph: Tunning RSSI for Lenovo according to RTL8191SE.
		if (CurrSig >= 54 && CurrSig <= 100)
			RetSig = 100;
		else if (CurrSig >= 42 && CurrSig <= 53)
			RetSig = 95;
		else if (CurrSig >= 36 && CurrSig <= 41)
			RetSig = 74 + ((CurrSig - 36) * 20) / 6;
		else if (CurrSig >= 33 && CurrSig <= 35)
			RetSig = 65 + ((CurrSig - 33) * 8) / 2;
		else if (CurrSig >= 18 && CurrSig <= 32)
			RetSig = 62 + ((CurrSig - 18) * 2) / 15;
		else if (CurrSig >= 15 && CurrSig <= 17)
			RetSig = 33 + ((CurrSig - 15) * 28) / 2;
		else if (CurrSig >= 10 && CurrSig <= 14)
			RetSig = 39;
		else if (CurrSig >= 8 && CurrSig <= 9)
			RetSig = 33;
		else if (CurrSig <= 8)
			RetSig = 19;
	}
#endif //ENDIF (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	return RetSig;
}

s4Byte
odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Netcore(	
	IN OUT PDM_ODM_T pDM_Odm,
	s4Byte CurrSig 
)
{
	s4Byte RetSig = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	//if(pDM_Odm->SupportInterface  == ODM_ITRF_USB)
	{
		// Netcore request this modification because 2009.04.13 SU driver use it. 
		if (CurrSig >= 31 && CurrSig <= 100)
			RetSig = 100;
		else if (CurrSig >= 21 && CurrSig <= 30)
			RetSig = 90 + ((CurrSig - 20) / 1);
		else if (CurrSig >= 11 && CurrSig <= 20)
			RetSig = 80 + ((CurrSig - 10) / 1);
		else if (CurrSig >= 7 && CurrSig <= 10)
			RetSig = 69 + (CurrSig - 7);
		else if (CurrSig == 6)
			RetSig = 54;
		else if (CurrSig == 5)
			RetSig = 45;
		else if (CurrSig == 4)
			RetSig = 36;
		else if (CurrSig == 3)
			RetSig = 27;
		else if (CurrSig == 2)
			RetSig = 18;
		else if (CurrSig == 1)
			RetSig = 9;
		else
			RetSig = CurrSig;
	}
#endif //ENDIF (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	return RetSig;
}


s4Byte
odm_SignalScaleMapping_92CSeries(	
	IN OUT PDM_ODM_T pDM_Odm,
	IN s4Byte CurrSig 
)
{
	s4Byte RetSig = 0; 
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) 
	if (pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) 
	{
		// Step 1. Scale mapping.
		if(CurrSig >= 61 && CurrSig <= 100)
		{
			RetSig = 90 + ((CurrSig - 60) / 4);
		}
		else if(CurrSig >= 41 && CurrSig <= 60)
		{
			RetSig = 78 + ((CurrSig - 40) / 2);
		}
		else if(CurrSig >= 31 && CurrSig <= 40)
		{
			RetSig = 66 + (CurrSig - 30);
		}
		else if(CurrSig >= 21 && CurrSig <= 30)
		{
			RetSig = 54 + (CurrSig - 20);
		}
		else if(CurrSig >= 5 && CurrSig <= 20)
		{
			RetSig = 42 + (((CurrSig - 5) * 2) / 3);
		}
		else if(CurrSig == 4)
		{
			RetSig = 36;
		}
		else if(CurrSig == 3)
		{
			RetSig = 27;
		}
		else if(CurrSig == 2)
		{
			RetSig = 18;
		}
		else if(CurrSig == 1)
		{
			RetSig = 9;
		}
		else
		{
			RetSig = CurrSig;
		}
	}
#endif

#if ((DEV_BUS_TYPE == RT_USB_INTERFACE) ||(DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	if((pDM_Odm->SupportInterface  == ODM_ITRF_USB) || (pDM_Odm->SupportInterface  == ODM_ITRF_SDIO))
	{
		if(CurrSig >= 51 && CurrSig <= 100)
		{
			RetSig = 100;
		}
		else if(CurrSig >= 41 && CurrSig <= 50)
		{
			RetSig = 80 + ((CurrSig - 40)*2);
		}
		else if(CurrSig >= 31 && CurrSig <= 40)
		{
			RetSig = 66 + (CurrSig - 30);
		}
		else if(CurrSig >= 21 && CurrSig <= 30)
		{
			RetSig = 54 + (CurrSig - 20);
		}
		else if(CurrSig >= 10 && CurrSig <= 20)
		{
			RetSig = 42 + (((CurrSig - 10) * 2) / 3);
		}
		else if(CurrSig >= 5 && CurrSig <= 9)
		{
			RetSig = 22 + (((CurrSig - 5) * 3) / 2);
		}
		else if(CurrSig >= 1 && CurrSig <= 4)
		{
			RetSig = 6 + (((CurrSig - 1) * 3) / 2);
		}
		else
		{
			RetSig = CurrSig;
		}
	}

#endif
	return RetSig;
}
s4Byte
odm_SignalScaleMapping(	
	IN OUT PDM_ODM_T pDM_Odm,
	IN	s4Byte CurrSig 
)
{	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if(	(pDM_Odm->SupportPlatform == ODM_WIN) && 
		(pDM_Odm->SupportInterface  != ODM_ITRF_PCIE) && //USB & SDIO
		(pDM_Odm->PatchID==10))//pMgntInfo->CustomerID == RT_CID_819x_Netcore
	{
		return odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Netcore(pDM_Odm,CurrSig);
	}
	else if(	(pDM_Odm->SupportPlatform == ODM_WIN) && 
			(pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) &&
			(pDM_Odm->PatchID==19))//pMgntInfo->CustomerID == RT_CID_819x_Lenovo)
	{
		return odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Lenovo(pDM_Odm, CurrSig);
	}else
#endif
		{
#ifdef CONFIG_SIGNAL_SCALE_MAPPING		
		return odm_SignalScaleMapping_92CSeries(pDM_Odm,CurrSig);
#else
		return CurrSig;
#endif
	}
	
}



static u1Byte odm_SQ_process_patch_RT_CID_819x_Lenovo(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte 		isCCKrate,
	IN u1Byte 		PWDB_ALL,
	IN u1Byte 		path,
	IN u1Byte 		RSSI
)
{
	u1Byte	SQ = 0;
#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)			

	if(isCCKrate){
		
		if (IS_HARDWARE_TYPE_8192E(pDM_Odm->Adapter)) {

			//
			// <Roger_Notes> Expected signal strength and bars indication at Lenovo lab. 2013.04.11
			// 802.11n, 802.11b, 802.11g only at channel 6
			//
			//		Attenuation (dB)	OS Signal Bars	RSSI by Xirrus (dBm)
			//			50				5			-49
			//			55				5			-49
			//			60				5			-50
			//			65				5			-51
			//			70				5			-52
			//			75				5			-54
			//			80				5			-55
			//			85				4			-60
			//			90				3			-63
			//			95				3			-65
			//			100				2			-67
			//			102				2			-67
			//			104				1			-70
			//			

			if(PWDB_ALL >= 50)
				SQ = 100;
			else if(PWDB_ALL >= 35 && PWDB_ALL < 50)				
				SQ = 80;
			else if(PWDB_ALL >= 31 && PWDB_ALL < 35)
				SQ = 60;
			else if(PWDB_ALL >= 22 && PWDB_ALL < 31)
				SQ = 40;
			else if(PWDB_ALL >= 18 && PWDB_ALL < 22)
				SQ = 20;
			else
				SQ = 10;
		} else {
			if (PWDB_ALL >= 50)
				SQ = 100;
			else if (PWDB_ALL >= 35 && PWDB_ALL < 50)				
				SQ = 80;
			else if (PWDB_ALL >= 22 && PWDB_ALL < 35)
				SQ = 60;
			else if (PWDB_ALL >= 18 && PWDB_ALL < 22)
				SQ = 40;
			else
				SQ = 10;
		}
		
	}
	else
	{//OFDM rate		

		if (IS_HARDWARE_TYPE_8192E(pDM_Odm->Adapter)) {
			if(RSSI >= 45)
				SQ = 100;
			else if(RSSI >= 22 && RSSI < 45)
				SQ = 80;
			else if(RSSI >= 18 && RSSI < 22)
				SQ = 40;
			else
			SQ = 20;
		} else {
			if(RSSI >= 45)
			SQ = 100;
			else if(RSSI >= 22 && RSSI < 45)
			SQ = 80;
		else if(RSSI >= 18 && RSSI < 22)
			SQ = 40;
		else
			SQ = 20;			
		}
	}

	RT_TRACE(COMP_DBG, DBG_TRACE, ("isCCKrate(%#d), PWDB_ALL(%#d), RSSI(%#d), SQ(%#d)\n", isCCKrate, PWDB_ALL, RSSI, SQ));
	
#endif
	return SQ;
}

static u1Byte odm_SQ_process_patch_RT_CID_819x_Acer(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte 		isCCKrate,
	IN u1Byte 		PWDB_ALL,
	IN u1Byte 		path,
	IN u1Byte 		RSSI
)
{
	u1Byte	SQ = 0;
	
#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)			

	if(isCCKrate){

			RT_TRACE(COMP_DBG, DBG_WARNING, ("odm_SQ_process_patch_RT_Acer\n"));
			
#if OS_WIN_FROM_WIN8(OS_VERSION)	

			if(PWDB_ALL >= 50)
				SQ = 100;
			else if(PWDB_ALL >= 35 && PWDB_ALL < 50)				
				SQ = 80;
			else if(PWDB_ALL >= 30 && PWDB_ALL < 35)
				SQ = 60;
			else if(PWDB_ALL >= 25 && PWDB_ALL < 30)
				SQ = 40;
			else if(PWDB_ALL >= 20 && PWDB_ALL < 25)
				SQ = 20;
			else
				SQ = 10;	
#else
			if(PWDB_ALL >= 50)
				SQ = 100;
			else if(PWDB_ALL >= 35 && PWDB_ALL < 50)				
				SQ = 80;
			else if(PWDB_ALL >= 30 && PWDB_ALL < 35)
				SQ = 60;
			else if(PWDB_ALL >= 25 && PWDB_ALL < 30)
				SQ = 40;
			else if(PWDB_ALL >= 20 && PWDB_ALL < 25)
				SQ = 20;
			else
				SQ = 10;	

			if(PWDB_ALL == 0)// Abnormal case, do not indicate the value above 20 on Win7
				SQ = 20;
#endif		

		
		
	}
	else
	{//OFDM rate		

		if (IS_HARDWARE_TYPE_8192E(pDM_Odm->Adapter)) {
			if(RSSI >= 45)
				SQ = 100;
			else if(RSSI >= 22 && RSSI < 45)
				SQ = 80;
			else if(RSSI >= 18 && RSSI < 22)
				SQ = 40;
			else
			SQ = 20;
	}
		else
		{
			if(RSSI >= 35)
			SQ = 100;
			else if(RSSI >= 30 && RSSI < 35)
			SQ = 80;
		else if(RSSI >= 25 && RSSI < 30)
			SQ = 40;
		else
			SQ = 20;			
	}
	}

	RT_TRACE(COMP_DBG, DBG_LOUD, ("isCCKrate(%#d), PWDB_ALL(%#d), RSSI(%#d), SQ(%#d)\n", isCCKrate, PWDB_ALL, RSSI, SQ));
	
#endif
	return SQ;
}
			
static u1Byte 
odm_EVMdbToPercentage(
    IN		s1Byte Value
    )
{
	//
	// -33dB~0dB to 0%~99%
	//
	s1Byte ret_val;
    
	ret_val = Value;
	ret_val /= 2;

	/*DbgPrint("Value=%d\n", Value);*/
	/*ODM_RT_DISP(FRX, RX_PHY_SQ, ("EVMdbToPercentage92C Value=%d / %x\n", ret_val, ret_val));*/
#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -40)
		ret_val = -40;

	ret_val = 0 - ret_val;
	ret_val *= 3;
#else
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;
#endif

	return (u1Byte)ret_val;
}
			
static u1Byte 
odm_EVMdbm_JaguarSeries(
	IN  s1Byte Value
	)
{
	s1Byte ret_val = Value;
	
	// -33dB~0dB to 33dB ~ 0dB
	if(ret_val == -128)
		ret_val = 127;
	else if (ret_val < 0)
		ret_val = 0 - ret_val;
	
	ret_val  = ret_val >> 1;
	return (u1Byte)ret_val;
}

static s2Byte
odm_Cfo(
  IN s1Byte Value
)
{
	s2Byte  ret_val;

	if (Value < 0)
	{
		ret_val = 0 - Value;
		ret_val = (ret_val << 1) + (ret_val >> 1) ;  //  *2.5~=312.5/2^7
		ret_val = ret_val | BIT12;  // set bit12 as 1 for negative cfo
	}
	else
	{
		ret_val = Value;
		ret_val = (ret_val << 1) + (ret_val>>1) ;  //  *2.5~=312.5/2^7
	}
	return ret_val;
}

u1Byte
phydm_rate_to_num_ss(
	IN OUT	PDM_ODM_T		pDM_Odm,
	IN		u1Byte			DataRate
)
{
	u1Byte	num_ss = 1;

	if (DataRate  <= ODM_RATE54M)
		num_ss = 1;
	else if (DataRate  <= ODM_RATEMCS31)
		num_ss = ((DataRate  - ODM_RATEMCS0) >> 3)+1;
	else if (DataRate  <= ODM_RATEVHTSS1MCS9)
		num_ss = 1;
	else if (DataRate  <= ODM_RATEVHTSS2MCS9)
		num_ss = 2;
	else if (DataRate  <= ODM_RATEVHTSS3MCS9)
		num_ss = 3;
	else if (DataRate  <= ODM_RATEVHTSS4MCS9)
		num_ss = 4;

	return num_ss;
}

#if(ODM_IC_11N_SERIES_SUPPORT == 1)

#if (RTL8703B_SUPPORT == 1)
s1Byte
odm_CCKRSSI_8703B(
	IN		u2Byte	LNA_idx,
	IN		u1Byte	VGA_idx
	)
{
	s1Byte	rx_pwr_all = 0x00;
	
	switch (LNA_idx) {
	case 0xf:
		rx_pwr_all = -48 - (2 * VGA_idx);
		break;		
	case 0xb:
		rx_pwr_all = -42 - (2 * VGA_idx); /*TBD*/
		break;
	case 0xa:
		rx_pwr_all = -36 - (2 * VGA_idx);
		break;
	case 8:
		rx_pwr_all = -32 - (2 * VGA_idx);
		break;
	case 7:	
		rx_pwr_all = -19 - (2 * VGA_idx);
		break;		
	case 4:	
		rx_pwr_all = -6 - (2 * VGA_idx);
		break;
	case 0:	
		rx_pwr_all = -2 - (2 * VGA_idx);
		break;
	default:
	/*rx_pwr_all = -53+(2*(31-VGA_idx));*/
	/*DbgPrint("wrong LNA index\n");*/
		break;
			
	}
	return	rx_pwr_all;
}
#endif

#if (RTL8195A_SUPPORT == 1)
s1Byte
odm_CCKRSSI_8195A(
	IN OUT	PDM_ODM_T	pDM_Odm,
	IN		u2Byte		LNA_idx,
	IN		u1Byte		VGA_idx
	)
{
	s1Byte	rx_pwr_all = 0;
	s1Byte	lna_gain = 0;
	s1Byte	lna_gain_table_0[8] = {0, -8, -15, -22, -29, -36, -45, -54};	
	s1Byte	lna_gain_table_1[8] = {0, -8, -15, -22, -29, -36, -45, -54};/*use 8195A to calibrate this table. 2016.06.24, Dino*/

	if (pDM_Odm->cck_agc_report_type == 0)
		lna_gain = lna_gain_table_0[LNA_idx];
	else
		lna_gain = lna_gain_table_1[LNA_idx];

	rx_pwr_all = lna_gain - (2 * VGA_idx);
	
	return	rx_pwr_all;
}
#endif

#if (RTL8192E_SUPPORT == 1)
s1Byte
odm_CCKRSSI_8192E(
	IN OUT	PDM_ODM_T	pDM_Odm,
	IN		u2Byte		LNA_idx,
	IN		u1Byte		VGA_idx
	)
{
	s1Byte	rx_pwr_all = 0;
	s1Byte	lna_gain = 0;
	s1Byte	lna_gain_table_0[8] = {15, 9, -10, -21, -23, -27, -43, -44};	
	s1Byte	lna_gain_table_1[8] = {24, 18, 13, -4, -11, -18, -31, -36};/*use 8192EU to calibrate this table. 2015.12.15, Dino*/

	if (pDM_Odm->cck_agc_report_type == 0)
		lna_gain = lna_gain_table_0[LNA_idx];
	else
		lna_gain = lna_gain_table_1[LNA_idx];

	rx_pwr_all = lna_gain - (2 * VGA_idx);
	
	return	rx_pwr_all;
}
#endif

#if (RTL8188E_SUPPORT == 1)
s1Byte
odm_CCKRSSI_8188E(
	IN OUT	PDM_ODM_T	pDM_Odm,
	IN		u2Byte		LNA_idx,
	IN		u1Byte		VGA_idx
	)
{
	s1Byte	rx_pwr_all = 0;
	s1Byte	lna_gain = 0;
	s1Byte	lna_gain_table_0[8] = {17, -1, -13, -29, -32, -35, -38, -41};/*only use lna0/1/2/3/7*/
	s1Byte	lna_gain_table_1[8] = {29, 20, 12, 3, -6, -15, -24, -33}; /*only use lna3 /7*/

	if (pDM_Odm->CutVersion >= ODM_CUT_I) /*SMIC*/
		lna_gain = lna_gain_table_0[LNA_idx];
	else	 /*TSMC*/		
		lna_gain = lna_gain_table_1[LNA_idx];
		
	rx_pwr_all = lna_gain - (2 * VGA_idx);
	
	return	rx_pwr_all;
}
#endif

VOID
odm_RxPhyStatus92CSeries_Parsing(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,		
	IN 		pu1Byte						pPhyStatus,
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{							
	SWAT_T				*pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	u1Byte				i, Max_spatial_stream;
	s1Byte				rx_pwr[4], rx_pwr_all=0;
	u1Byte				EVM, PWDB_ALL = 0, PWDB_ALL_BT;
	u1Byte				RSSI, total_rssi=0;
	BOOLEAN				isCCKrate=FALSE;	
	u1Byte				rf_rx_num = 0;
	u1Byte				cck_highpwr = 0;
	u1Byte				LNA_idx = 0;
	u1Byte				VGA_idx = 0;
	u1Byte				cck_agc_rpt;
	u1Byte				num_ss;
	PPHY_STATUS_RPT_8192CD_T pPhyStaRpt = (PPHY_STATUS_RPT_8192CD_T)pPhyStatus;

	isCCKrate = (pPktinfo->DataRate <= ODM_RATE11M) ? TRUE : FALSE;

	if (pPktinfo->bToSelf)
		pDM_Odm->curr_station_id = pPktinfo->StationID;
	
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_A] = -1;
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_B] = -1;


	if(isCCKrate)
	{
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK++;
		cck_agc_rpt =  pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a ;
		
		if (pDM_Odm->SupportICType & (ODM_RTL8703B)) {
			
			#if (RTL8703B_SUPPORT == 1)
			if (pDM_Odm->cck_agc_report_type == 1) {  /*4 bit LNA*/

				u1Byte cck_agc_rpt_b = (pPhyStaRpt->cck_rpt_b_ofdm_cfosho_b & BIT7) ? 1 : 0;
								
				LNA_idx = (cck_agc_rpt_b << 3) | ((cck_agc_rpt & 0xE0) >> 5);
				VGA_idx = (cck_agc_rpt & 0x1F);
				
				rx_pwr_all = odm_CCKRSSI_8703B(LNA_idx, VGA_idx);			
			}
			#endif
		} else { /*3 bit LNA*/
		
			LNA_idx = ((cck_agc_rpt & 0xE0) >>5);
			VGA_idx = (cck_agc_rpt & 0x1F);
			
			if (pDM_Odm->SupportICType & (ODM_RTL8188E)) {
			
				#if (RTL8188E_SUPPORT == 1)	
				rx_pwr_all = odm_CCKRSSI_8188E(pDM_Odm, LNA_idx, VGA_idx);
				/**/
				#endif
			}	
			#if (RTL8192E_SUPPORT == 1)	
			else if (pDM_Odm->SupportICType & (ODM_RTL8192E)) {
				
				rx_pwr_all = odm_CCKRSSI_8192E(pDM_Odm, LNA_idx, VGA_idx);
				/**/
			}
			#endif
			#if (RTL8723B_SUPPORT == 1)
			else if (pDM_Odm->SupportICType & (ODM_RTL8723B)) {
							
				rx_pwr_all = odm_CCKRSSI_8723B(LNA_idx,VGA_idx);
				/**/
			} 
			#endif
			#if (RTL8188F_SUPPORT == 1)
			else if (pDM_Odm->SupportICType & (ODM_RTL8188F)) {
				
				rx_pwr_all = odm_CCKRSSI_8188F(LNA_idx, VGA_idx);
				/**/
			}
			#endif
			#if (RTL8195A_SUPPORT == 1)
			else if (pDM_Odm->SupportICType & (ODM_RTL8195A)) {
				
				rx_pwr_all = odm_CCKRSSI_8195A(LNA_idx, VGA_idx);
				/**/
			}
			#endif			
		}
			
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("ExtLNAGain (( %d )), LNA_idx: (( 0x%x )), VGA_idx: (( 0x%x )), rx_pwr_all: (( %d ))\n", 
			pDM_Odm->ExtLNAGain, LNA_idx, VGA_idx, rx_pwr_all));
		
		if (pDM_Odm->BoardType & ODM_BOARD_EXT_LNA) 
			rx_pwr_all -= pDM_Odm->ExtLNAGain;

		PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		
		if (pPktinfo->bToSelf) {
			pDM_Odm->cck_lna_idx = LNA_idx;
			pDM_Odm->cck_vga_idx = VGA_idx;
		}
		pPhyInfo->RxPWDBAll = PWDB_ALL;

#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
#endif		
		//
		// (3) Get Signal Quality (EVM)
		//
		//if(pPktinfo->bPacketMatchBSSID)
		{
			u1Byte	SQ,SQ_rpt;			
			
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)			
			if((pDM_Odm->SupportPlatform == ODM_WIN) &&
				(pDM_Odm->PatchID==RT_CID_819x_Lenovo)){
				SQ = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm,isCCKrate,PWDB_ALL,0,0);
			}else if((pDM_Odm->SupportPlatform == ODM_WIN) &&
				(pDM_Odm->PatchID==RT_CID_819x_Acer))
			{
				SQ = odm_SQ_process_patch_RT_CID_819x_Acer(pDM_Odm,isCCKrate,PWDB_ALL,0,0);
			}else 
#endif
			if(pPhyInfo->RxPWDBAll > 40 && !pDM_Odm->bInHctTest){
				SQ = 100;
			}
			else{						
				SQ_rpt = pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all;
					
				if(SQ_rpt > 64)
					SQ = 0;
				else if (SQ_rpt < 20)
					SQ = 100;
				else
					SQ = ((64-SQ_rpt) * 100) / 44;
			
			}
			
			//DbgPrint("cck SQ = %d\n", SQ);
			pPhyInfo->SignalQuality = SQ;
			pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_A] = SQ;
			pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_B] = -1;
		}

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			if (i == 0)
				pPhyInfo->RxMIMOSignalStrength[0] = PWDB_ALL;
			else
				pPhyInfo->RxMIMOSignalStrength[1] = 0;
		}
	}
	else //2 is OFDM rate
	{
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

		// 
		// (1)Get RSSI for HT rate
		//
		
       	 for(i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX; i++)   
		{
			// 2008/01/30 MH we will judge RF RX path now.
			if (pDM_Odm->RFPathRxEnable & BIT(i))
				rf_rx_num++;
			//else
				//continue;

			rx_pwr[i] = ((pPhyStaRpt->path_agc[i].gain& 0x3F)*2) - 110;
			
			if (pPktinfo->bToSelf) {
				pDM_Odm->ofdm_agc_idx[i] = (pPhyStaRpt->path_agc[i].gain & 0x3F);
				/**/
			}

		#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
			pPhyInfo->RxPwr[i] = rx_pwr[i];
		#endif	

			/* Translate DBM to percentage. */
			RSSI = odm_QueryRxPwrPercentage(rx_pwr[i]);
			total_rssi += RSSI;
			//RT_DISP(FRX, RX_PHY_SS, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI));
	
			pPhyInfo->RxMIMOSignalStrength[i] =(u1Byte) RSSI;

		#if (DM_ODM_SUPPORT_TYPE &  (/*ODM_WIN|*/ODM_CE|ODM_AP))
			//Get Rx snr value in DB		
			pPhyInfo->RxSNR[i] = pDM_Odm->PhyDbgInfo.RxSNRdB[i] = (s4Byte)(pPhyStaRpt->path_rxsnr[i]/2);
		#endif
		
			/* Record Signal Strength for next packet */
			//if(pPktinfo->bPacketMatchBSSID)
			{				
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)				
				if((pDM_Odm->SupportPlatform == ODM_WIN) &&
					(pDM_Odm->PatchID==RT_CID_819x_Lenovo))
				{
					if(i==ODM_RF_PATH_A)
						pPhyInfo->SignalQuality = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm,isCCKrate,PWDB_ALL,i,RSSI);
				
				}		
				else if((pDM_Odm->SupportPlatform == ODM_WIN) &&
					(pDM_Odm->PatchID==RT_CID_819x_Acer))
				{
					pPhyInfo->SignalQuality = odm_SQ_process_patch_RT_CID_819x_Acer(pDM_Odm,isCCKrate,PWDB_ALL,0,RSSI);
				}	
#endif				
			}
		}
		
		
		//
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//
		rx_pwr_all = (((pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all) >> 1 )& 0x7f) -110;		
		
		PWDB_ALL_BT = PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);	
	
	
		pPhyInfo->RxPWDBAll = PWDB_ALL;
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("ODM OFDM RSSI=%d\n",pPhyInfo->RxPWDBAll));
	#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL_BT;
		pPhyInfo->RxPower = rx_pwr_all;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
	#endif
		
		if((pDM_Odm->SupportPlatform == ODM_WIN) &&(pDM_Odm->PatchID==19)){
			//do nothing	
		}else if((pDM_Odm->SupportPlatform == ODM_WIN) &&(pDM_Odm->PatchID==25)){
			//do nothing	
		}
		else{//pMgntInfo->CustomerID != RT_CID_819x_Lenovo
			//
			// (3)EVM of HT rate
			//
			if(pPktinfo->DataRate >=ODM_RATEMCS8 && pPktinfo->DataRate <=ODM_RATEMCS15)
				Max_spatial_stream = 2; //both spatial stream make sense
			else
				Max_spatial_stream = 1; //only spatial stream 1 makes sense

			for(i=0; i<Max_spatial_stream; i++)
			{
				// Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment
				// fill most significant bit to "zero" when doing shifting operation which may change a negative 
				// value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.			
				EVM = odm_EVMdbToPercentage( (pPhyStaRpt->stream_rxevm[i] ));	//dbm

				//GET_RX_STATUS_DESC_RX_MCS(pDesc), pDrvInfo->rxevm[i], "%", EVM));
				
				//if(pPktinfo->bPacketMatchBSSID)
				{
					if(i==ODM_RF_PATH_A) // Fill value in RFD, Get the first spatial stream only
					{						
						pPhyInfo->SignalQuality = (u1Byte)(EVM & 0xff);
					}					
					pPhyInfo->RxMIMOSignalQuality[i] = (u1Byte)(EVM & 0xff);
				}
			}
		}

		num_ss =phydm_rate_to_num_ss(pDM_Odm, pPktinfo->DataRate );
		ODM_ParsingCFO(pDM_Odm, pPktinfo, pPhyStaRpt->path_cfotail, num_ss);
		
	}
#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
	//UI BSS List signal strength(in percentage), make it good looking, from 0~100.
	//It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().
	if(isCCKrate)
	{		
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		// 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/	
		pPhyInfo->SignalStrength = SignalScaleProc(pDM_Odm->Adapter, PWDB_ALL, TRUE, TRUE);
#else
		pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, PWDB_ALL));/*PWDB_ALL;*/
#endif /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
	}
	else
	{	
		if (rf_rx_num != 0)
		{			
		#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			// 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/	
			pPhyInfo->SignalStrength = SignalScaleProc(pDM_Odm->Adapter, (total_rssi /= rf_rx_num), TRUE, FALSE);
		#else
			pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, total_rssi /= rf_rx_num));
		#endif
		}
	}
#endif /*#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))*/

	//DbgPrint("isCCKrate = %d, pPhyInfo->RxPWDBAll = %d, pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a = 0x%x\n", 
		//isCCKrate, pPhyInfo->RxPWDBAll, pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a);

	//For 92C/92D HW (Hybrid) Antenna Diversity
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))	
	//For 88E HW Antenna Diversity
	pDM_Odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->ant_sel;
	pDM_Odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->ant_sel_b;
	pDM_Odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antsel_rx_keep_2;
#endif
}
#endif

#if	ODM_IC_11AC_SERIES_SUPPORT

VOID
odm_RxPhyBWJaguarSeries_Parsing(
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN		PODM_PACKET_INFO_T			pPktinfo,
	IN		PPHY_STATUS_RPT_8812_T		pPhyStaRpt
)
{

	if(pPktinfo->DataRate <= ODM_RATE54M) {
		switch (pPhyStaRpt->r_RFMOD) {
		case 1:
			if (pPhyStaRpt->sub_chnl == 0)
				pPhyInfo->BandWidth = 1;
			else
				pPhyInfo->BandWidth = 0;
			break;

		case 2:
			if (pPhyStaRpt->sub_chnl == 0)
				pPhyInfo->BandWidth = 2;
			else if (pPhyStaRpt->sub_chnl == 9 || pPhyStaRpt->sub_chnl == 10)
				pPhyInfo->BandWidth = 1;
			else
				pPhyInfo->BandWidth = 0;
			break;

		default:
		case 0:
			pPhyInfo->BandWidth = 0;
			break;
		}
	}

}

VOID
odm_RxPhyStatusJaguarSeries_Parsing(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN 		pu1Byte						pPhyStatus,
	IN		PODM_PACKET_INFO_T			pPktinfo
)
{
	u1Byte					i, Max_spatial_stream;
	s1Byte					rx_pwr[4], rx_pwr_all = 0;
	u1Byte					EVM, EVMdbm, PWDB_ALL = 0, PWDB_ALL_BT;
	u1Byte					RSSI, avg_rssi = 0, best_rssi = 0, second_rssi = 0;
	u1Byte					isCCKrate = 0;	
	u1Byte					rf_rx_num = 0;
	u1Byte					cck_highpwr = 0;
	u1Byte					LNA_idx, VGA_idx;
	PPHY_STATUS_RPT_8812_T pPhyStaRpt = (PPHY_STATUS_RPT_8812_T)pPhyStatus;
	pFAT_T					pDM_FatTable = &pDM_Odm->DM_FatTable;
	u1Byte					num_ss;

	odm_RxPhyBWJaguarSeries_Parsing(pPhyInfo, pPktinfo, pPhyStaRpt);

	if (pPktinfo->DataRate <= ODM_RATE11M)
		isCCKrate = TRUE;
	else
		isCCKrate = FALSE;

	if (pPktinfo->bToSelf)
		pDM_Odm->curr_station_id = pPktinfo->StationID;
	else
		pDM_Odm->curr_station_id = 0xff;

	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_A] = -1;
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_B] = -1;
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_C] = -1;
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_D] = -1;

	if (isCCKrate) {
		u1Byte cck_agc_rpt;
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK++;

		/*(1)Hardware does not provide RSSI for CCK*/
		/*(2)PWDB, Average PWDB calculated by hardware (for rate adaptive)*/

		/*if(pHalData->eRFPowerState == eRfOn)*/
		cck_highpwr = pDM_Odm->bCckHighPower;
		/*else*/
		/*cck_highpwr = FALSE;*/

		cck_agc_rpt =  pPhyStaRpt->cfosho[0] ;
		LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
		VGA_idx = (cck_agc_rpt & 0x1F);

		if (pDM_Odm->SupportICType == ODM_RTL8812) {
			switch (LNA_idx) {
			case 7:
				if (VGA_idx <= 27)
					rx_pwr_all = -100 + 2 * (27 - VGA_idx); /*VGA_idx = 27~2*/
				else
					rx_pwr_all = -100;
				break;
			case 6:
				rx_pwr_all = -48 + 2 * (2 - VGA_idx); /*VGA_idx = 2~0*/
				break;
			case 5:
				rx_pwr_all = -42 + 2 * (7 - VGA_idx); /*VGA_idx = 7~5*/
				break;
			case 4:
				rx_pwr_all = -36 + 2 * (7 - VGA_idx); /*VGA_idx = 7~4*/
				break;
			case 3:
				/*rx_pwr_all = -28 + 2*(7-VGA_idx); VGA_idx = 7~0*/
				rx_pwr_all = -24 + 2 * (7 - VGA_idx); /*VGA_idx = 7~0*/
				break;
			case 2:
				if (cck_highpwr)
					rx_pwr_all = -12 + 2 * (5 - VGA_idx); /*VGA_idx = 5~0*/
				else
					rx_pwr_all = -6 + 2 * (5 - VGA_idx);
				break;
			case 1:
				rx_pwr_all = 8 - 2 * VGA_idx;
				break;
			case 0:
				rx_pwr_all = 14 - 2 * VGA_idx;
				break;
			default:
				/*DbgPrint("CCK Exception default\n");*/
				break;
			}
			rx_pwr_all += 6;
			PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);

			if (cck_highpwr == FALSE) {
				if (PWDB_ALL >= 80)
					PWDB_ALL = ((PWDB_ALL - 80) << 1) + ((PWDB_ALL - 80) >> 1) + 80;
				else if ((PWDB_ALL <= 78) && (PWDB_ALL >= 20))
					PWDB_ALL += 3;
				if (PWDB_ALL > 100)
					PWDB_ALL = 100;
			}
		} else if (pDM_Odm->SupportICType & (ODM_RTL8821 | ODM_RTL8881A)) {
			s1Byte Pout = -6;

			switch (LNA_idx) {
			case 5:
				rx_pwr_all = Pout - 32 - (2 * VGA_idx);
				break;
			case 4:
				rx_pwr_all = Pout - 24 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = Pout - 11 - (2 * VGA_idx);
				break;
			case 1:
				rx_pwr_all = Pout + 5 - (2 * VGA_idx);
				break;
			case 0:
				rx_pwr_all = Pout + 21 - (2 * VGA_idx);
				break;
			}
			PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		} else if (pDM_Odm->SupportICType == ODM_RTL8814A || pDM_Odm->SupportICType == ODM_RTL8822B) {
			s1Byte Pout = -6;

			switch (LNA_idx) {
			/*CCK only use LNA: 2, 3, 5, 7*/
			case 7:
				rx_pwr_all = Pout - 32 - (2 * VGA_idx);
				break;
			case 5:
				rx_pwr_all = Pout - 22 - (2 * VGA_idx);
				break;
			case 3:
				rx_pwr_all = Pout - 2 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = Pout + 5 - (2 * VGA_idx);
				break;
			/*case 6:*/
			/*rx_pwr_all = Pout -26 - (2*VGA_idx);*/
			/*break;*/
			/*case 4:*/
			/*rx_pwr_all = Pout - 8 - (2*VGA_idx);*/
			/*break;*/
			/*case 1:*/
			/*rx_pwr_all = Pout + 21 - (2*VGA_idx);*/
			/*break;*/
			/*case 0:*/
			/*rx_pwr_all = Pout + 10 - (2*VGA_idx);*/
/*			//	break;*/
			default:
/*				//DbgPrint("CCK Exception default\n");*/
				break;
			}
			PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
		}

		pDM_Odm->cck_lna_idx = LNA_idx;
		pDM_Odm->cck_vga_idx = VGA_idx;
		pPhyInfo->RxPWDBAll = PWDB_ALL;
/*		//if(pPktinfo->StationID == 0)*/
/*		//{*/
/*		//	DbgPrint("CCK: LNA_idx = %d, VGA_idx = %d, pPhyInfo->RxPWDBAll = %d\n",*/
/*		//		LNA_idx, VGA_idx, pPhyInfo->RxPWDBAll);*/
/*		//}*/
#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
#endif
		/*(3) Get Signal Quality (EVM)*/
		/*if (pPktinfo->bPacketMatchBSSID)*/
		{
			u1Byte	SQ, SQ_rpt;

			if ((pDM_Odm->SupportPlatform == ODM_WIN) &&
				(pDM_Odm->PatchID == RT_CID_819x_Lenovo)) {
				SQ = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm, isCCKrate, PWDB_ALL, 0, 0);
			} else if (pPhyInfo->RxPWDBAll > 40 && !pDM_Odm->bInHctTest) {
				SQ = 100;
			} else {
				SQ_rpt = pPhyStaRpt->pwdb_all;

				if (SQ_rpt > 64)
					SQ = 0;
				else if (SQ_rpt < 20)
					SQ = 100;
				else
					SQ = ((64 - SQ_rpt) * 100) / 44;
			}

/*			//DbgPrint("cck SQ = %d\n", SQ);*/
			pPhyInfo->SignalQuality = SQ;
			pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_A] = SQ;
		}

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (i == 0)
				pPhyInfo->RxMIMOSignalStrength[0] = PWDB_ALL;
			else
				pPhyInfo->RxMIMOSignalStrength[i] = 0;
		}
	} else {		 
		/*is OFDM rate*/
		pDM_FatTable->hw_antsw_occur = pPhyStaRpt->hw_antsw_occur;
		
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

		/*(1)Get RSSI for OFDM rate*/

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			/*2008/01/30 MH we will judge RF RX path now.*/
/*			//DbgPrint("pDM_Odm->RFPathRxEnable = %x\n", pDM_Odm->RFPathRxEnable);*/
			if (pDM_Odm->RFPathRxEnable & BIT(i))
				rf_rx_num++;
/*			//else*/
/*			//continue;*/
			/*2012.05.25 LukeLee: Testchip AGC report is wrong, it should be restored back to old formula in MP chip*/
/*			//if((pDM_Odm->SupportICType & (ODM_RTL8812|ODM_RTL8821)) && (!pDM_Odm->bIsMPChip))*/
			if (i < ODM_RF_PATH_C)
				rx_pwr[i] = (pPhyStaRpt->gain_trsw[i] & 0x7F) - 110;
			else
				rx_pwr[i] = (pPhyStaRpt->gain_trsw_cd[i - 2] & 0x7F) - 110;
/*			//else*/
			/*rx_pwr[i] = ((pPhyStaRpt->gain_trsw[i]& 0x3F)*2) - 110;  OLD FORMULA*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
			pPhyInfo->RxPwr[i] = rx_pwr[i];
#endif

			/* Translate DBM to percentage. */
			RSSI = odm_QueryRxPwrPercentage(rx_pwr[i]);	
		
			/*total_rssi += RSSI;*/
			/*Get the best two RSSI*/
			if (RSSI > best_rssi && RSSI > second_rssi) {
				second_rssi = best_rssi;
				best_rssi = RSSI;
			} else if (RSSI > second_rssi && RSSI <= best_rssi)
				second_rssi = RSSI;

			/*RT_DISP(FRX, RX_PHY_SS, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI));*/

			pPhyInfo->RxMIMOSignalStrength[i] = (u1Byte) RSSI;


			/*Get Rx snr value in DB*/
			if (i < ODM_RF_PATH_C)
				pPhyInfo->RxSNR[i] = pDM_Odm->PhyDbgInfo.RxSNRdB[i] = pPhyStaRpt->rxsnr[i] / 2;
			else if (pDM_Odm->SupportICType & (ODM_RTL8814A | ODM_RTL8822B))
				pPhyInfo->RxSNR[i] = pDM_Odm->PhyDbgInfo.RxSNRdB[i] = pPhyStaRpt->csi_current[i - 2] / 2;

#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
			/*(2) CFO_short  & CFO_tail*/
			if (i < ODM_RF_PATH_C) {
				pPhyInfo->Cfo_short[i] = odm_Cfo((pPhyStaRpt->cfosho[i]));
				pPhyInfo->Cfo_tail[i] = odm_Cfo((pPhyStaRpt->cfotail[i]));
			}
#endif
			/* Record Signal Strength for next packet */
			if (pPktinfo->bPacketMatchBSSID) {
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
				if ((pDM_Odm->SupportPlatform == ODM_WIN) &&
					(pDM_Odm->PatchID == RT_CID_819x_Lenovo)) {
					if (i == ODM_RF_PATH_A)
						pPhyInfo->SignalQuality = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm, isCCKrate, PWDB_ALL, i, RSSI);

				}
#endif
			}
		}

		/*(3)PWDB, Average PWDB calculated by hardware (for rate adaptive)*/

		/*2012.05.25 LukeLee: Testchip AGC report is wrong, it should be restored back to old formula in MP chip*/
		if ((pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A)) && (!pDM_Odm->bIsMPChip))
			rx_pwr_all = (pPhyStaRpt->pwdb_all & 0x7f) - 110;
		else
			rx_pwr_all = (((pPhyStaRpt->pwdb_all) >> 1) & 0x7f) - 110;	 /*OLD FORMULA*/

		PWDB_ALL_BT = PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);

		pPhyInfo->RxPWDBAll = PWDB_ALL;
		/*ODM_RT_TRACE(pDM_Odm,ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("ODM OFDM RSSI=%d\n",pPhyInfo->RxPWDBAll));*/
#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL_BT;
		pPhyInfo->RxPower = rx_pwr_all;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
#endif

		if ((pDM_Odm->SupportPlatform == ODM_WIN) && (pDM_Odm->PatchID == 19)) {
			/*do nothing*/
		} else {
			/*pMgntInfo->CustomerID != RT_CID_819x_Lenovo*/

			/*(4)EVM of OFDM rate*/
			
			if ((pPktinfo->DataRate >= ODM_RATEMCS8) &&
				(pPktinfo->DataRate <= ODM_RATEMCS15))
				Max_spatial_stream = 2;
			else if ((pPktinfo->DataRate >= ODM_RATEVHTSS2MCS0) &&
					 (pPktinfo->DataRate <= ODM_RATEVHTSS2MCS9))
				Max_spatial_stream = 2;
			else if ((pPktinfo->DataRate >= ODM_RATEMCS16) &&
					 (pPktinfo->DataRate <= ODM_RATEMCS23))
				Max_spatial_stream = 3;
			else if ((pPktinfo->DataRate >= ODM_RATEVHTSS3MCS0) &&
					 (pPktinfo->DataRate <= ODM_RATEVHTSS3MCS9))
				Max_spatial_stream = 3;
			else
				Max_spatial_stream = 1;

			/*if (pPktinfo->bPacketMatchBSSID) */
			{
				/*DbgPrint("pPktinfo->DataRate = %d\n", pPktinfo->DataRate);*/

				for (i = 0; i < Max_spatial_stream; i++) {
					/*Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment*/
					/*fill most significant bit to "zero" when doing shifting operation which may change a negative*/
					/*value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.*/

					if (pPktinfo->DataRate >= ODM_RATE6M && pPktinfo->DataRate <= ODM_RATE54M) {
						if (i == ODM_RF_PATH_A) {
							EVM = odm_EVMdbToPercentage((pPhyStaRpt->sigevm));	/*dbm*/
							EVM += 20;
							if (EVM > 100)
								EVM = 100;
						}
					} else {
						if (i < ODM_RF_PATH_C) {
							if (pPhyStaRpt->rxevm[i] == -128)
								pPhyStaRpt->rxevm[i] = -25;
							EVM = odm_EVMdbToPercentage((pPhyStaRpt->rxevm[i]));	/*dbm*/
						} else {
							if (pPhyStaRpt->rxevm_cd[i - 2] == -128){
								pPhyStaRpt->rxevm_cd[i - 2] = -25;
							}
							EVM = odm_EVMdbToPercentage((pPhyStaRpt->rxevm_cd[i - 2]));	/*dbm*/
						}
					}

					if (i < ODM_RF_PATH_C)
						EVMdbm = odm_EVMdbm_JaguarSeries(pPhyStaRpt->rxevm[i]);
					else
						EVMdbm = odm_EVMdbm_JaguarSeries(pPhyStaRpt->rxevm_cd[i - 2]);
					/*RT_DISP(FRX, RX_PHY_SQ, ("RXRATE=%x RXEVM=%x EVM=%s%d\n",*/
					/*pPktinfo->DataRate, pPhyStaRpt->rxevm[i], "%", EVM));*/

					{
						if (i == ODM_RF_PATH_A) { 
							/*Fill value in RFD, Get the first spatial stream only*/
							pPhyInfo->SignalQuality = EVM;
						}
						pPhyInfo->RxMIMOSignalQuality[i] = EVM;
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
						pPhyInfo->RxMIMOEVMdbm[i] = EVMdbm;
#endif
					}
				}
			}
		}

		num_ss =phydm_rate_to_num_ss(pDM_Odm, pPktinfo->DataRate );
		ODM_ParsingCFO(pDM_Odm, pPktinfo, pPhyStaRpt->cfotail, num_ss);

	}
/*	//DbgPrint("isCCKrate= %d, pPhyInfo->SignalStrength=%d % PWDB_AL=%d rf_rx_num=%d\n", isCCKrate, pPhyInfo->SignalStrength, PWDB_ALL, rf_rx_num);*/

#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
	/*UI BSS List signal strength(in percentage), make it good looking, from 0~100.*/
	/*It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().*/
	if (isCCKrate) {
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		/*2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/*/
		pPhyInfo->SignalStrength = SignalScaleProc(pDM_Odm->Adapter, PWDB_ALL, FALSE, TRUE);
#else
		pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, PWDB_ALL));/*PWDB_ALL;*/
#endif
	} else {	
		if (rf_rx_num != 0) {
			/* 2015/01 Sean, use the best two RSSI only, suggested by Ynlin and ChenYu.*/
			if (rf_rx_num == 1)
				avg_rssi = best_rssi;
			else
				avg_rssi = (best_rssi + second_rssi)/2;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			/* 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/*/	
			pPhyInfo->SignalStrength = SignalScaleProc(pDM_Odm->Adapter, avg_rssi, FALSE, FALSE);
#else
			pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, avg_rssi));
#endif
		}
	}
#endif
	pDM_Odm->RxPWDBAve = pDM_Odm->RxPWDBAve + pPhyInfo->RxPWDBAll;

	pDM_Odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->antidx_anta;
	pDM_Odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->antidx_antb;
	pDM_Odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antidx_antc;
	pDM_Odm->DM_FatTable.antsel_rx_keep_3 = pPhyStaRpt->antidx_antd;
	/*ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("StaID[%d]:  antidx_anta = ((%d)), MatchBSSID =  ((%d))\n", pPktinfo->StationID, pPhyStaRpt->antidx_anta, pPktinfo->bPacketMatchBSSID));*/


/*		DbgPrint("pPhyStaRpt->antidx_anta = %d, pPhyStaRpt->antidx_antb = %d\n",*/
/*			pPhyStaRpt->antidx_anta, pPhyStaRpt->antidx_antb);*/
/*		DbgPrint("----------------------------\n");*/
/*		DbgPrint("pPktinfo->StationID=%d, pPktinfo->DataRate=0x%x\n",pPktinfo->StationID, pPktinfo->DataRate);*/
/*		DbgPrint("pPhyStaRpt->r_RFMOD = %d\n", pPhyStaRpt->r_RFMOD);*/
/*		DbgPrint("pPhyStaRpt->gain_trsw[0]=0x%x, pPhyStaRpt->gain_trsw[1]=0x%x\n",*/
/*				pPhyStaRpt->gain_trsw[0],pPhyStaRpt->gain_trsw[1]);*/
/*		DbgPrint("pPhyStaRpt->gain_trsw[2]=0x%x, pPhyStaRpt->gain_trsw[3]=0x%x\n",*/
/*				pPhyStaRpt->gain_trsw_cd[0],pPhyStaRpt->gain_trsw_cd[1]);*/
/*		DbgPrint("pPhyStaRpt->pwdb_all = 0x%x, pPhyInfo->RxPWDBAll = %d\n", pPhyStaRpt->pwdb_all, pPhyInfo->RxPWDBAll);*/
/*		DbgPrint("pPhyStaRpt->cfotail[i] = 0x%x, pPhyStaRpt->CFO_tail[i] = 0x%x\n", pPhyStaRpt->cfotail[0], pPhyStaRpt->cfotail[1]);*/
/*		DbgPrint("pPhyStaRpt->rxevm[0] = %d, pPhyStaRpt->rxevm[1] = %d\n", pPhyStaRpt->rxevm[0], pPhyStaRpt->rxevm[1]);*/
/*		DbgPrint("pPhyStaRpt->rxevm[2] = %d, pPhyStaRpt->rxevm[3] = %d\n", pPhyStaRpt->rxevm_cd[0], pPhyStaRpt->rxevm_cd[1]);*/
/*		DbgPrint("pPhyInfo->RxMIMOSignalStrength[0]=%d, pPhyInfo->RxMIMOSignalStrength[1]=%d, RxPWDBAll=%d\n",*/
/*				pPhyInfo->RxMIMOSignalStrength[0], pPhyInfo->RxMIMOSignalStrength[1], pPhyInfo->RxPWDBAll);*/
/*		DbgPrint("pPhyInfo->RxMIMOSignalStrength[2]=%d, pPhyInfo->RxMIMOSignalStrength[3]=%d\n",*/
/*				pPhyInfo->RxMIMOSignalStrength[2], pPhyInfo->RxMIMOSignalStrength[3]);*/
/*		DbgPrint("ppPhyInfo->RxMIMOSignalQuality[0]=%d, pPhyInfo->RxMIMOSignalQuality[1]=%d\n",*/
/*				pPhyInfo->RxMIMOSignalQuality[0], pPhyInfo->RxMIMOSignalQuality[1]);*/
/*		DbgPrint("ppPhyInfo->RxMIMOSignalQuality[2]=%d, pPhyInfo->RxMIMOSignalQuality[3]=%d\n",*/
/*				pPhyInfo->RxMIMOSignalQuality[2], pPhyInfo->RxMIMOSignalQuality[3]);*/

}

#endif

VOID
phydm_reset_rssi_for_dm(
	IN OUT	PDM_ODM_T	pDM_Odm,
	IN		u1Byte		station_id
	)
{
	PSTA_INFO_T			pEntry;
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	#endif

	pEntry = pDM_Odm->pODM_StaInfo[station_id];

	if (!IS_STA_VALID(pEntry)) {
		/**/
		return;
	}
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("Reset RSSI for macid = (( %d ))\n", station_id));

	
	pEntry->rssi_stat.UndecoratedSmoothedCCK = -1;
	pEntry->rssi_stat.UndecoratedSmoothedOFDM = -1;
	pEntry->rssi_stat.UndecoratedSmoothedPWDB = -1;
	pEntry->rssi_stat.OFDM_pkt = 0;
	pEntry->rssi_stat.CCK_pkt = 0;
	pEntry->rssi_stat.CCK_sum_power = 0;
	pEntry->rssi_stat.bsend_rssi = RA_RSSI_STATE_INIT;
	pEntry->rssi_stat.PacketMap = 0;
	pEntry->rssi_stat.ValidBit = 0;

	/*in WIN Driver: sta_ID==0 -> pEntry==NULL -> default port HAL_Data*/
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	pEntry->bUsed = 0;
	if (station_id == 0) { 
		
		pHalData->UndecoratedSmoothedPWDB = -1;
		/**/
	}
	#endif

}

VOID
odm_Init_RSSIForDM(
	IN OUT	PDM_ODM_T	pDM_Odm
	)
{

}

VOID
odm_Process_RSSIForDM(	
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	
	s4Byte			UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK, UndecoratedSmoothedOFDM, RSSI_Ave, CCK_pkt;
	u1Byte			i, isCCKrate=0;	
	u1Byte			RSSI_max, RSSI_min;
	u4Byte			Weighting=0;
	u1Byte			send_rssi_2_fw = 0;
	PSTA_INFO_T			pEntry;
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))	
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	#endif

	if (pPktinfo->StationID >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_S0S1_SwAntDivByCtrlFrame_ProcessRSSI(pDM_Odm, pPhyInfo, pPktinfo);
	#endif

	//
	// 2012/05/30 MH/Luke.Lee Add some description 
	// In windows driver: AP/IBSS mode STA
	//
	//if (pDM_Odm->SupportPlatform == ODM_WIN)
	//{
	//	pEntry = pDM_Odm->pODM_StaInfo[pDM_Odm->pAidMap[pPktinfo->StationID-1]];			
	//}
	//else
		pEntry = pDM_Odm->pODM_StaInfo[pPktinfo->StationID];							

	if (!IS_STA_VALID(pEntry)) {	
		return;
		/**/
	}
		
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))	
	if ((pDM_Odm->SupportAbility & ODM_BB_ANT_DIV) &&
		(pDM_FatTable->enable_ctrl_frame_antdiv)
	)
	{
		if (pPktinfo->bPacketMatchBSSID)
			pDM_Odm->data_frame_num++;
				
		if ((pDM_FatTable->use_ctrl_frame_antdiv)) {
			if (!pPktinfo->bToSelf)/*data frame + CTRL frame*/
				return;
		} else {
			if ((!pPktinfo->bPacketMatchBSSID))/*data frame only*/
				return;
		}	
	} else
#endif
	{
		if ((!pPktinfo->bPacketMatchBSSID))/*data frame only*/
			return;
	}

	if(pPktinfo->bPacketBeacon)
		pDM_Odm->PhyDbgInfo.NumQryBeaconPkt++;
	
	isCCKrate = (pPktinfo->DataRate <= ODM_RATE11M )?TRUE :FALSE;
	pDM_Odm->RxRate = pPktinfo->DataRate;

	//--------------Statistic for antenna/path diversity------------------
	if(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)
	{
		#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
			ODM_Process_RSSIForAntDiv(pDM_Odm,pPhyInfo,pPktinfo);
		#endif
	}
	#if(defined(CONFIG_PATH_DIVERSITY))
	else if(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV)
	{
		phydm_process_rssi_for_path_div(pDM_Odm,pPhyInfo,pPktinfo);
	}
	#endif
	//-----------------Smart Antenna Debug Message------------------//
	
	UndecoratedSmoothedCCK =  pEntry->rssi_stat.UndecoratedSmoothedCCK;
	UndecoratedSmoothedOFDM = pEntry->rssi_stat.UndecoratedSmoothedOFDM;
	UndecoratedSmoothedPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;	
	
	if(pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon)
	{

		if(!isCCKrate)//ofdm rate
		{
#if (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1)
			if (pDM_Odm->SupportICType & (ODM_RTL8814A|ODM_RTL8822B)) {
				u1Byte RX_count = 0;
				u4Byte RSSI_linear = 0;

				if (pDM_Odm->RXAntStatus & ODM_RF_A) {
					pDM_Odm->RSSI_A = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					RX_count++;
					RSSI_linear += odm_ConvertTo_linear(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A]);
				} else
					pDM_Odm->RSSI_A = 0;

				if (pDM_Odm->RXAntStatus & ODM_RF_B) {
					pDM_Odm->RSSI_B = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
					RX_count++;
					RSSI_linear += odm_ConvertTo_linear(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B]);
				} else
					pDM_Odm->RSSI_B = 0;
				
				if (pDM_Odm->RXAntStatus & ODM_RF_C) {
					pDM_Odm->RSSI_C = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_C];
					RX_count++;
					RSSI_linear += odm_ConvertTo_linear(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_C]);
				} else
					pDM_Odm->RSSI_C = 0;

				if (pDM_Odm->RXAntStatus & ODM_RF_D) {
					pDM_Odm->RSSI_D = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_D];
					RX_count++;
					RSSI_linear += odm_ConvertTo_linear(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_D]);
				} else
					pDM_Odm->RSSI_D = 0;

				/* Calculate average RSSI */
				switch (RX_count) {
				case 2:
					RSSI_linear = (RSSI_linear >> 1);
					break;
				case 3:
					RSSI_linear = ((RSSI_linear) + (RSSI_linear << 1) + (RSSI_linear << 3)) >> 5;	/* RSSI_linear/3 ~ RSSI_linear*11/32 */
					break;
				case 4:
					RSSI_linear = (RSSI_linear >> 2);
					break;
				}			
				RSSI_Ave = odm_ConvertTo_dB(RSSI_linear);
			} else
#endif
			{
				if (pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B] == 0) {
					RSSI_Ave = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					pDM_Odm->RSSI_A = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					pDM_Odm->RSSI_B = 0;
				} else {
					/*DbgPrint("pRfd->Status.RxMIMOSignalStrength[0] = %d, pRfd->Status.RxMIMOSignalStrength[1] = %d\n",*/ 
						/*pRfd->Status.RxMIMOSignalStrength[0], pRfd->Status.RxMIMOSignalStrength[1]);*/
					pDM_Odm->RSSI_A =  pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					pDM_Odm->RSSI_B = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
				
					if (pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A] > pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B]) {
						RSSI_max = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
						RSSI_min = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
					} else {
						RSSI_max = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
						RSSI_min = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					}
					if ((RSSI_max - RSSI_min) < 3)
						RSSI_Ave = RSSI_max;
					else if ((RSSI_max - RSSI_min) < 6)
						RSSI_Ave = RSSI_max - 1;
					else if ((RSSI_max - RSSI_min) < 10)
						RSSI_Ave = RSSI_max - 2;
					else
						RSSI_Ave = RSSI_max - 3;
				}
			}
					
			//1 Process OFDM RSSI
			if(UndecoratedSmoothedOFDM <= 0)	// initialize
			{
				UndecoratedSmoothedOFDM = pPhyInfo->RxPWDBAll;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("OFDM_INIT: (( %d ))\n", UndecoratedSmoothedOFDM));
			}
			else
			{
				if(pPhyInfo->RxPWDBAll > (u4Byte)UndecoratedSmoothedOFDM)
				{
					UndecoratedSmoothedOFDM = 	
							( ((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) + 
							(RSSI_Ave)) /(Rx_Smooth_Factor);
					UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM + 1;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("OFDM_1: (( %d ))\n", UndecoratedSmoothedOFDM));
				}
				else
				{
					UndecoratedSmoothedOFDM = 	
							( ((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) + 
							(RSSI_Ave)) /(Rx_Smooth_Factor);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("OFDM_2: (( %d ))\n", UndecoratedSmoothedOFDM));
				}
			}				
			if (pEntry->rssi_stat.OFDM_pkt != 64) {
				i = 63;
				pEntry->rssi_stat.OFDM_pkt -= (u1Byte)(((pEntry->rssi_stat.PacketMap>>i)&BIT0)-1);
			}
			pEntry->rssi_stat.PacketMap = (pEntry->rssi_stat.PacketMap<<1) | BIT0;			
										
		}
		else
		{
			RSSI_Ave = pPhyInfo->RxPWDBAll;
			pDM_Odm->RSSI_A = (u1Byte) pPhyInfo->RxPWDBAll;
			pDM_Odm->RSSI_B = 0xFF;
			pDM_Odm->RSSI_C = 0xFF;
			pDM_Odm->RSSI_D = 0xFF;

			if (pEntry->rssi_stat.CCK_pkt <= 63)
				pEntry->rssi_stat.CCK_pkt++;

			//1 Process CCK RSSI
			if(UndecoratedSmoothedCCK <= 0)	// initialize
			{
				UndecoratedSmoothedCCK = pPhyInfo->RxPWDBAll;
				pEntry->rssi_stat.CCK_sum_power = (u2Byte)pPhyInfo->RxPWDBAll ; /*reset*/
				pEntry->rssi_stat.CCK_pkt = 1; /*reset*/
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("CCK_INIT: (( %d ))\n", UndecoratedSmoothedCCK));
			} else if (pEntry->rssi_stat.CCK_pkt <= CCK_RSSI_INIT_COUNT) {
				
				pEntry->rssi_stat.CCK_sum_power = pEntry->rssi_stat.CCK_sum_power + (u2Byte)pPhyInfo->RxPWDBAll;
				UndecoratedSmoothedCCK = pEntry->rssi_stat.CCK_sum_power/pEntry->rssi_stat.CCK_pkt;
				
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("CCK_0: (( %d )), SumPow = (( %d )), CCK_pkt = (( %d ))\n", 
					UndecoratedSmoothedCCK, pEntry->rssi_stat.CCK_sum_power, pEntry->rssi_stat.CCK_pkt));
			}
			else
			{
				if(pPhyInfo->RxPWDBAll > (u4Byte)UndecoratedSmoothedCCK)
				{
					UndecoratedSmoothedCCK = 	
							( ((UndecoratedSmoothedCCK)*(Rx_Smooth_Factor-1)) + 
							(pPhyInfo->RxPWDBAll)) /(Rx_Smooth_Factor);
					UndecoratedSmoothedCCK = UndecoratedSmoothedCCK + 1;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("CCK_1: (( %d ))\n", UndecoratedSmoothedCCK));
				}
				else
				{
					UndecoratedSmoothedCCK = 	
							( ((UndecoratedSmoothedCCK)*(Rx_Smooth_Factor-1)) + 
							(pPhyInfo->RxPWDBAll)) /(Rx_Smooth_Factor);
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("CCK_2: (( %d ))\n", UndecoratedSmoothedCCK));
				}
			}
			i = 63;
			pEntry->rssi_stat.OFDM_pkt -= (u1Byte)((pEntry->rssi_stat.PacketMap>>i)&BIT0);			
			pEntry->rssi_stat.PacketMap = pEntry->rssi_stat.PacketMap<<1;
		}

		//if(pEntry)
		{
			//2011.07.28 LukeLee: modified to prevent unstable CCK RSSI
			if (pEntry->rssi_stat.OFDM_pkt == 64) { /* speed up when all packets are OFDM*/
				UndecoratedSmoothedPWDB = UndecoratedSmoothedOFDM;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("PWDB_0[%d] = (( %d ))\n", pPktinfo->StationID, UndecoratedSmoothedCCK));
			} else {
				if (pEntry->rssi_stat.ValidBit < 64)
					pEntry->rssi_stat.ValidBit++;

				if (pEntry->rssi_stat.ValidBit == 64) {
					Weighting = ((pEntry->rssi_stat.OFDM_pkt) > 4) ? 64 : (pEntry->rssi_stat.OFDM_pkt<<4);
					UndecoratedSmoothedPWDB = (Weighting*UndecoratedSmoothedOFDM+(64-Weighting)*UndecoratedSmoothedCCK)>>6;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("PWDB_1[%d] = (( %d )), W = (( %d ))\n", pPktinfo->StationID, UndecoratedSmoothedCCK, Weighting));
				} else {
					if (pEntry->rssi_stat.ValidBit != 0)
						UndecoratedSmoothedPWDB = (pEntry->rssi_stat.OFDM_pkt*UndecoratedSmoothedOFDM+(pEntry->rssi_stat.ValidBit-pEntry->rssi_stat.OFDM_pkt)*UndecoratedSmoothedCCK)/pEntry->rssi_stat.ValidBit;
					else
						UndecoratedSmoothedPWDB = 0;

					ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("PWDB_2[%d] = (( %d )), OFDM_pkt = (( %d )), Valid_Bit = (( %d ))\n", pPktinfo->StationID, UndecoratedSmoothedCCK, pEntry->rssi_stat.OFDM_pkt, pEntry->rssi_stat.ValidBit));
				}
			}
			
			
			if ((pEntry->rssi_stat.OFDM_pkt >= 1 || pEntry->rssi_stat.CCK_pkt >= 5) && (pEntry->rssi_stat.bsend_rssi == RA_RSSI_STATE_INIT)) {
			
				send_rssi_2_fw = 1;
				pEntry->rssi_stat.bsend_rssi = RA_RSSI_STATE_SEND;
			}

			pEntry->rssi_stat.UndecoratedSmoothedCCK = UndecoratedSmoothedCCK;
			pEntry->rssi_stat.UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM;
			pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;


			
			if (send_rssi_2_fw) /* Trigger init rate by RSSI */ {
				
				if (pEntry->rssi_stat.OFDM_pkt != 0)
					pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedOFDM;

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("[Send to FW] PWDB = (( %d )), OFDM_pkt = (( %d )), CCK_pkt = (( %d ))\n", 
					UndecoratedSmoothedPWDB, pEntry->rssi_stat.OFDM_pkt, pEntry->rssi_stat.CCK_pkt));
				
				#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
				phydm_ra_rssi_rpt_wk(pDM_Odm);
				#endif
			}
			

			/*in WIN Driver: sta_ID==0 -> pEntry==NULL -> default port HAL_Data*/
			#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))	
				
				if (pPktinfo->StationID == 0) { 
					/**/
					pHalData->UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
				}
			#endif
			
			//DbgPrint("OFDM_pkt=%d, Weighting=%d\n", OFDM_pkt, Weighting);
			//DbgPrint("UndecoratedSmoothedOFDM=%d, UndecoratedSmoothedPWDB=%d, UndecoratedSmoothedCCK=%d\n", 
			//	UndecoratedSmoothedOFDM, UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK);
			
		}
	
	}
}


#if(ODM_IC_11N_SERIES_SUPPORT ==1)
//
// Endianness before calling this API
//
VOID
ODM_PhyStatusQuery_92CSeries(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T				pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	odm_RxPhyStatus92CSeries_Parsing(pDM_Odm, pPhyInfo, pPhyStatus, pPktinfo);
	odm_Process_RSSIForDM(pDM_Odm, pPhyInfo, pPktinfo);
}
#endif


//
// Endianness before calling this API
//
#if	ODM_IC_11AC_SERIES_SUPPORT

VOID
ODM_PhyStatusQuery_JaguarSeries(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	odm_RxPhyStatusJaguarSeries_Parsing(pDM_Odm, pPhyInfo,	pPhyStatus, pPktinfo);
	odm_Process_RSSIForDM(pDM_Odm, pPhyInfo, pPktinfo);
	
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	/*phydm_sbd_check(pDM_Odm);*/
	#endif
}
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
phydm_normal_driver_rx_sniffer(
	IN OUT	PDM_ODM_T			pDM_Odm,
	IN		pu1Byte				pDesc,
	IN		PRT_RFD_STATUS		pRtRfdStatus,
	IN		pu1Byte				pDrvInfo,
	IN		u1Byte				PHYStatus
	)
{
	#if (defined(CONFIG_PHYDM_RX_SNIFFER_PARSING)) 
	u4Byte		*pMsg;
	u2Byte		seq_num;
	pFAT_T		pDM_FatTable = &pDM_Odm->DM_FatTable;

	if (pRtRfdStatus->PacketReportType != NORMAL_RX)
		return;
	
	if (!pDM_Odm->bLinked) {
		if (pRtRfdStatus->bHwError)
			return;
	}

	if (!(pDM_FatTable->FAT_State == FAT_TRAINING_STATE))
		return;
	
	if (PHYStatus == TRUE) {

		if ((pDM_Odm->rx_pkt_type == Type_BlockAck) || (pDM_Odm->rx_pkt_type == Type_RTS) || (pDM_Odm->rx_pkt_type == Type_CTS))
			seq_num = 0;
		else
			seq_num = pRtRfdStatus->Seq_Num;

		ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, ("%04d , %01s , Rate=0x%02x , L=%04d , %s , %s", 
			seq_num,
			/*pRtRfdStatus->MacID,*/
			((pRtRfdStatus->bCRC) ? "C" : (pRtRfdStatus->bIsAMPDU) ? "A" : "_"),
			pRtRfdStatus->DataRate,
			pRtRfdStatus->Length,
			((pRtRfdStatus->BandWidth == 0) ? "20M":((pRtRfdStatus->BandWidth == 1) ? "40M" : "80M")),
			((pRtRfdStatus->bLDPC) ? "LDP" : "BCC")
		));		
		
		if (pDM_Odm->rx_pkt_type == Type_Asoc_Req) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "AS_REQ"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Asoc_Rsp) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "AS_RSP"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Probe_Req) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "PR_REQ"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Probe_Rsp) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "PR_RSP"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Deauth) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "DEAUTH"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Beacon) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "BEACON"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_BlockAckReq) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "BA_REQ"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_RTS) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "__RTS_"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_CTS) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "__CTS_"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Ack) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "__ACK_"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_BlockAck) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "__BA__"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Data) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "_DATA_"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_Data_Ack) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "Data_Ack"));
			/**/
		} else if (pDM_Odm->rx_pkt_type == Type_QosData) {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [%s]", "QoS_Data"));
			/**/
		} else {
			ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [0x%x]", pDM_Odm->rx_pkt_type));
			/**/
		}

		ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , [RSSI=%d,%d,%d,%d ]",
			pDM_Odm->RSSI_A,
			pDM_Odm->RSSI_B,
			pDM_Odm->RSSI_C,
			pDM_Odm->RSSI_D
		));
		
		pMsg = (pu4Byte)pDrvInfo;
		
		ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, (" , P-STS[28:0]=%08x-%08x-%08x-%08x-%08x-%08x-%08x\n",
			pMsg[6], pMsg[5], pMsg[4], pMsg[3], pMsg[2], pMsg[1], pMsg[1]));
	} else {
	
		ODM_RT_TRACE_F(pDM_Odm, ODM_COMP_SNIFFER, ODM_DBG_LOUD, ("%04d , %01s , Rate=0x%02x , L=%04d , %s , %s\n", 
			pRtRfdStatus->Seq_Num,
			/*pRtRfdStatus->MacID,*/
			((pRtRfdStatus->bCRC) ? "C" : (pRtRfdStatus->bIsAMPDU) ? "A" : "_"),
			pRtRfdStatus->DataRate,
			pRtRfdStatus->Length,
			((pRtRfdStatus->BandWidth == 0) ? "20M" : ((pRtRfdStatus->BandWidth == 1) ? "40M" : "80M")),
			((pRtRfdStatus->bLDPC) ? "LDP" : "BCC")
		));		
	}


	#endif
}
#endif

VOID
ODM_PhyStatusQuery(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
	if (pDM_Odm->SupportICType & ODM_IC_PHY_STATUE_NEW_TYPE) {
		phydm_RxPhyStatusNewType(pDM_Odm, pPhyStatus, pPktinfo, pPhyInfo);
		return;
	}
#endif

#if	ODM_IC_11AC_SERIES_SUPPORT
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		ODM_PhyStatusQuery_JaguarSeries(pDM_Odm, pPhyInfo, pPhyStatus, pPktinfo);
#endif

#if	ODM_IC_11N_SERIES_SUPPORT
	if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES )
		ODM_PhyStatusQuery_92CSeries(pDM_Odm,pPhyInfo,pPhyStatus,pPktinfo);
#endif
}
	
// For future use.
VOID
ODM_MacStatusQuery(
	IN OUT	PDM_ODM_T					pDM_Odm,
	IN 		pu1Byte						pMacStatus,
	IN		u1Byte						MacID,	
	IN		BOOLEAN						bPacketMatchBSSID,
	IN		BOOLEAN						bPacketToSelf,
	IN		BOOLEAN						bPacketBeacon
	)
{
	// 2011/10/19 Driver team will handle in the future.
	
}


//
// If you want to add a new IC, Please follow below template and generate a new one.
// 
//

HAL_STATUS
ODM_ConfigRFWithHeaderFile(
	IN 	PDM_ODM_T	        	pDM_Odm,
	IN 	ODM_RF_Config_Type 		ConfigType,
	IN 	ODM_RF_RADIO_PATH_E 	eRFPath
    )
{
#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)	
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);	
#endif

   ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
		 		("===>ODM_ConfigRFWithHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
				("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));

//1 AP doesn't use PHYDM power tracking table in these ICs
#if (DM_ODM_SUPPORT_TYPE !=  ODM_AP)
#if (RTL8812A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8812)
	{
		if(ConfigType == CONFIG_RF_RADIO) {
			if(eRFPath == ODM_RF_PATH_A){
				READ_AND_CONFIG_MP(8812A,_RadioA);
			}
			else if(eRFPath == ODM_RF_PATH_B){
				READ_AND_CONFIG_MP(8812A,_RadioB);
			}
		}
		else if(ConfigType == CONFIG_RF_TXPWR_LMT) {
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) && (DEV_BUS_TYPE == RT_PCI_INTERFACE)
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
			if ((pHalData->EEPROMSVID == 0x17AA && pHalData->EEPROMSMID == 0xA811) ||
				(pHalData->EEPROMSVID == 0x10EC && pHalData->EEPROMSMID == 0xA812) ||
				(pHalData->EEPROMSVID == 0x10EC && pHalData->EEPROMSMID == 0x8812))
				READ_AND_CONFIG_MP(8812A,_TXPWR_LMT_HM812A03);
			else
			#endif				
			READ_AND_CONFIG_MP(8812A,_TXPWR_LMT);
		}
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821)
	{
		if(ConfigType == CONFIG_RF_RADIO) {
	 		if(eRFPath == ODM_RF_PATH_A){
				READ_AND_CONFIG_MP(8821A,_RadioA);
			}
		}
		else if(ConfigType == CONFIG_RF_TXPWR_LMT) {
			if (pDM_Odm->SupportInterface == ODM_ITRF_USB) {
				if (pDM_Odm->ExtPA5G || pDM_Odm->ExtLNA5G)
					READ_AND_CONFIG_MP(8821A,_TXPWR_LMT_8811AU_FEM);
				else
					READ_AND_CONFIG_MP(8821A,_TXPWR_LMT_8811AU_IPA);				
			} 
			else {
				#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)
				if (pMgntInfo->CustomerID == RT_CID_8821AE_ASUS_MB)
					READ_AND_CONFIG_MP(8821A,_TXPWR_LMT_8821A_SAR_8mm);
				else if (pMgntInfo->CustomerID == RT_CID_ASUS_NB)
					READ_AND_CONFIG_MP(8821A,_TXPWR_LMT_8821A_SAR_5mm);
				else
				#endif
					READ_AND_CONFIG_MP(8821A,_TXPWR_LMT_8821A);			
			}
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("<===8821_ODM_ConfigRFWithHeaderFile\n"));
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		if(ConfigType == CONFIG_RF_RADIO) {
		 	if(eRFPath == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8192E,_RadioA);
			else if(eRFPath == ODM_RF_PATH_B)
				READ_AND_CONFIG_MP(8192E,_RadioB);
		} else if (ConfigType == CONFIG_RF_TXPWR_LMT) {
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) && (DEV_BUS_TYPE == RT_PCI_INTERFACE)	/*Refine by Vincent Lan for 5mm SAR pwr limit*/
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

			if ((pHalData->EEPROMSVID == 0x11AD && pHalData->EEPROMSMID == 0x8192) || 
				(pHalData->EEPROMSVID == 0x11AD && pHalData->EEPROMSMID == 0x8193))
				READ_AND_CONFIG_MP(8192E, _TXPWR_LMT_8192E_SAR_5mm);
			else
#endif	
				READ_AND_CONFIG_MP(8192E, _TXPWR_LMT);
		}
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723D) {
		if (ConfigType == CONFIG_RF_RADIO) {
			if (eRFPath == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8723D, _RadioA);
		} else if (ConfigType == CONFIG_RF_TXPWR_LMT) 
			READ_AND_CONFIG_MP(8723D, _TXPWR_LMT);		
	}
#endif
#endif//(DM_ODM_SUPPORT_TYPE !=  ODM_AP)

//1 All platforms support
#if (RTL8188E_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		if(ConfigType == CONFIG_RF_RADIO) {
			if(eRFPath == ODM_RF_PATH_A)
					READ_AND_CONFIG_MP(8188E,_RadioA);
		}
		else if(ConfigType == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8188E,_TXPWR_LMT);
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723B) {
		if (ConfigType == CONFIG_RF_RADIO)
			READ_AND_CONFIG_MP(8723B, _RadioA);
		else if (ConfigType == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8723B, _TXPWR_LMT);
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8814A)
	{
		if(ConfigType == CONFIG_RF_RADIO) {
		 	if(eRFPath == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8814A,_RadioA);
			else if(eRFPath == ODM_RF_PATH_B)
				READ_AND_CONFIG_MP(8814A,_RadioB);
			else if(eRFPath == ODM_RF_PATH_C)
				READ_AND_CONFIG_MP(8814A,_RadioC);
			else if(eRFPath == ODM_RF_PATH_D)
				READ_AND_CONFIG_MP(8814A,_RadioD);
		}	
		else if(ConfigType == CONFIG_RF_TXPWR_LMT) 
			READ_AND_CONFIG_MP(8814A,_TXPWR_LMT);
	}
#endif
#if (RTL8703B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		if (ConfigType == CONFIG_RF_RADIO) {
			if (eRFPath == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8703B, _RadioA);
		}	
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8188F) {
		if (ConfigType == CONFIG_RF_RADIO) {
			if (eRFPath == ODM_RF_PATH_A)
				READ_AND_CONFIG_MP(8188F, _RadioA);
		} else if (ConfigType == CONFIG_RF_TXPWR_LMT)
			READ_AND_CONFIG_MP(8188F, _TXPWR_LMT);
	}
#endif
#if (RTL8822B_SUPPORT == 1)
		if (pDM_Odm->SupportICType == ODM_RTL8822B) {
			if (ConfigType == CONFIG_RF_RADIO) {
				if (eRFPath == ODM_RF_PATH_A)
					READ_AND_CONFIG_MP(8822B, _RadioA);
				else if (eRFPath == ODM_RF_PATH_B)
					READ_AND_CONFIG_MP(8822B, _RadioB);
			} else if(ConfigType == CONFIG_RF_TXPWR_LMT)
				if (pDM_Odm->RFEType == 5)
					READ_AND_CONFIG_MP(8822B, _TXPWR_LMT_type5);
				else 
					READ_AND_CONFIG_MP(8822B, _TXPWR_LMT);	
		}
#endif

#if (RTL8197F_SUPPORT == 1)
		if (pDM_Odm->SupportICType == ODM_RTL8197F) {
			if (ConfigType == CONFIG_RF_RADIO) {
				if (eRFPath == ODM_RF_PATH_A)
					READ_AND_CONFIG_MP(8197F, _RadioA);
				else if (eRFPath == ODM_RF_PATH_B)
					READ_AND_CONFIG_MP(8197F, _RadioB);
			}	
		}
#endif

#if (RTL8821C_SUPPORT == 1)
		if (pDM_Odm->SupportICType == ODM_RTL8821C) {
			if (ConfigType == CONFIG_RF_RADIO) {
				if (eRFPath == ODM_RF_PATH_A)
					READ_AND_CONFIG(8821C, _RadioA);
			} else if (ConfigType == CONFIG_RF_TXPWR_LMT)
				READ_AND_CONFIG(8821C, _TXPWR_LMT);
		}
#endif

	return HAL_STATUS_SUCCESS;
}

HAL_STATUS
ODM_ConfigRFWithTxPwrTrackHeaderFile(
	IN 	PDM_ODM_T	        	pDM_Odm
    )
{
   	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
		 		 ("===>ODM_ConfigRFWithTxPwrTrackHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
   	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
				 ("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				 pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));


//1 AP doesn't use PHYDM power tracking table in these ICs
#if (DM_ODM_SUPPORT_TYPE !=  ODM_AP)
#if RTL8821A_SUPPORT
	if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
		if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8821A,_TxPowerTrack_PCIE);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8821A,_TxPowerTrack_USB);			
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8821A,_TxPowerTrack_SDIO);
	}
#endif
#if RTL8812A_SUPPORT	
	if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8812A,_TxPowerTrack_PCIE);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_USB) {
			if (pDM_Odm->RFEType == 3 && pDM_Odm->bIsMPChip) 
				READ_AND_CONFIG_MP(8812A,_TxPowerTrack_RFE3);	
			else
				READ_AND_CONFIG_MP(8812A,_TxPowerTrack_USB);	
		}
		
	}
#endif	
#if RTL8192E_SUPPORT 	
	if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8192E,_TxPowerTrack_PCIE);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8192E,_TxPowerTrack_USB); 
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8192E,_TxPowerTrack_SDIO); 
	}
#endif
#if RTL8723D_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8723D) {
		if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8723D, _TxPowerTrack_PCIE);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8723D, _TxPowerTrack_USB); 
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8723D, _TxPowerTrack_SDIO); 
		
		READ_AND_CONFIG_MP(8723D, _TxXtalTrack);
	}
#endif
#if RTL8188E_SUPPORT 	
	if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		if (PHY_QueryMacReg(pDM_Odm->Adapter, 0xF0, 0xF000) >= 8) {		/*if 0xF0[15:12] >= 8, SMIC*/
			if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_PCIE_ICUT);
			else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_USB_ICUT);
			else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_SDIO_ICUT);
		} else {	/*else 0xF0[15:12] < 8, TSMC*/
			if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_PCIE);
			else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_USB);
			else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
				READ_AND_CONFIG_MP(8188E, _TxPowerTrack_SDIO);
		}
		
	}
#endif
#endif//(DM_ODM_SUPPORT_TYPE !=  ODM_AP)

//1 All platforms support
#if RTL8723B_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8723B) {
		if (pDM_Odm->SupportInterface == ODM_ITRF_PCIE)
			READ_AND_CONFIG_MP(8723B, _TxPowerTrack_PCIE);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8723B, _TxPowerTrack_USB);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8723B, _TxPowerTrack_SDIO);
	}
#endif	
#if RTL8814A_SUPPORT
	if(pDM_Odm->SupportICType == ODM_RTL8814A) 
	{
		if(pDM_Odm->RFEType == 0)
			READ_AND_CONFIG_MP(8814A,_TxPowerTrack_Type0);
		else if(pDM_Odm->RFEType == 2)
			READ_AND_CONFIG_MP(8814A,_TxPowerTrack_Type2);
		else if (pDM_Odm->RFEType == 5)
			READ_AND_CONFIG_MP(8814A, _TxPowerTrack_Type5);
		else
			READ_AND_CONFIG_MP(8814A,_TxPowerTrack);
			
		READ_AND_CONFIG_MP(8814A, _TxPowerTSSI);
	}
#endif
#if RTL8703B_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8703B, _TxPowerTrack_USB);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8703B, _TxPowerTrack_SDIO);		

		READ_AND_CONFIG_MP(8703B, _TxXtalTrack);
	}
#endif

#if RTL8188F_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8188F) {
		if (pDM_Odm->SupportInterface == ODM_ITRF_USB)
			READ_AND_CONFIG_MP(8188F, _TxPowerTrack_USB);
		else if (pDM_Odm->SupportInterface == ODM_ITRF_SDIO)
			READ_AND_CONFIG_MP(8188F, _TxPowerTrack_SDIO);
	}
#endif

#if RTL8822B_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8822B) {
		if (pDM_Odm->RFEType == 0)
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack_type0);
		else if (pDM_Odm->RFEType == 1)
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack_type1);
		else if ((pDM_Odm->RFEType == 3) || (pDM_Odm->RFEType == 5))
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack_Type3_Type5);
		else if (pDM_Odm->RFEType == 6)
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack_type6);
		else if (pDM_Odm->RFEType == 7)
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack_type7);
		else
			READ_AND_CONFIG_MP(8822B, _TxPowerTrack);
	}
#endif

#if RTL8197F_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8197F) {
		if (pDM_Odm->RFEType == 0)
			READ_AND_CONFIG_MP(8197F, _TxPowerTrack_Type0);
		else if (pDM_Odm->RFEType == 1)
			READ_AND_CONFIG_MP(8197F, _TxPowerTrack_Type1);
		else
			READ_AND_CONFIG_MP(8197F, _TxPowerTrack);
	}
#endif	

#if RTL8821C_SUPPORT
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
			READ_AND_CONFIG(8821C, _TxPowerTrack);
#endif

	return HAL_STATUS_SUCCESS;
}

HAL_STATUS
ODM_ConfigBBWithHeaderFile(
	IN 	PDM_ODM_T	             	pDM_Odm,
	IN 	ODM_BB_Config_Type 		ConfigType
	)
{
#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)	
	PADAPTER		Adapter = pDM_Odm->Adapter;
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);	
#endif

//1 AP doesn't use PHYDM initialization in these ICs
#if (DM_ODM_SUPPORT_TYPE !=  ODM_AP)	
#if (RTL8812A_SUPPORT == 1) 
	if(pDM_Odm->SupportICType == ODM_RTL8812)
	{
		if (ConfigType == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG_MP(8812A, _PHY_REG);
		} else if (ConfigType == CONFIG_BB_AGC_TAB) {
			READ_AND_CONFIG_MP(8812A, _AGC_TAB);
		} else if (ConfigType == CONFIG_BB_PHY_REG_PG) {
			if (pDM_Odm->RFEType == 3 && pDM_Odm->bIsMPChip) 
				READ_AND_CONFIG_MP(8812A, _PHY_REG_PG_ASUS);
			#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)
			else if (pMgntInfo->CustomerID == RT_CID_WNC_NEC && pDM_Odm->bIsMPChip) 
				READ_AND_CONFIG_MP(8812A, _PHY_REG_PG_NEC);
			#if RT_PLATFORM == PLATFORM_MACOSX
			/*{1827}{1024} for BUFFALO power by rate table. Isaiah 2013-11-29*/
			else if (pMgntInfo->CustomerID == RT_CID_DNI_BUFFALO)
				READ_AND_CONFIG_MP(8812A, _PHY_REG_PG_DNI);
			/* TP-Link T4UH, Isaiah 2015-03-16*/
			else if (pMgntInfo->CustomerID == RT_CID_TPLINK_HPWR) {
				DbgPrint("RT_CID_TPLINK_HPWR:: _PHY_REG_PG_TPLINK\n");
				READ_AND_CONFIG_MP(8812A, _PHY_REG_PG_TPLINK);
			}
			#endif
			#endif			
			else
				READ_AND_CONFIG_MP(8812A, _PHY_REG_PG);
		}
		else if(ConfigType == CONFIG_BB_PHY_REG_MP){
			READ_AND_CONFIG_MP(8812A,_PHY_REG_MP);
		}
		else if(ConfigType == CONFIG_BB_AGC_TAB_DIFF)
		{
			if ((36 <= *pDM_Odm->pChannel)  && (*pDM_Odm->pChannel  <= 64)) 
				AGC_DIFF_CONFIG_MP(8812A,LB);
			else if (100 <= *pDM_Odm->pChannel) 
				AGC_DIFF_CONFIG_MP(8812A,HB);
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:Rtl8812AGCTABArray\n"));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8812PHY_REGArray\n"));
	}		
#endif
#if (RTL8821A_SUPPORT == 1) 
	if (pDM_Odm->SupportICType == ODM_RTL8821) {
		if (ConfigType == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG_MP(8821A, _PHY_REG);
		} else if (ConfigType == CONFIG_BB_AGC_TAB) {
			READ_AND_CONFIG_MP(8821A, _AGC_TAB);
		} else if (ConfigType == CONFIG_BB_PHY_REG_PG) {
			#if (DM_ODM_SUPPORT_TYPE & ODM_WIN) 
			#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

			if ((pHalData->EEPROMSVID == 0x1043 && pHalData->EEPROMSMID == 0x207F))
				READ_AND_CONFIG_MP(8821A, _PHY_REG_PG_E202SA);
			else
			#endif
			#if (RT_PLATFORM == PLATFORM_MACOSX)
			/*{1827}{1022} for BUFFALO power by rate table. Isaiah 2013-10-18*/
			if (pMgntInfo->CustomerID == RT_CID_DNI_BUFFALO) {
				/*{1024} for BUFFALO power by rate table. (JP/US)*/
				if (pMgntInfo->ChannelPlan == RT_CHANNEL_DOMAIN_US_2G_CANADA_5G)
					READ_AND_CONFIG_MP(8821A, _PHY_REG_PG_DNI_US);
				else
					READ_AND_CONFIG_MP(8821A, _PHY_REG_PG_DNI_JP);
			} else
			#endif
			#endif
			READ_AND_CONFIG_MP(8821A,_PHY_REG_PG);
		}		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:Rtl8821AGCTABArray\n"));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8821PHY_REGArray\n"));
	}		
#endif
#if (RTL8192E_SUPPORT == 1)
	if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		if(ConfigType == CONFIG_BB_PHY_REG){
			READ_AND_CONFIG_MP(8192E,_PHY_REG);
		}else if(ConfigType == CONFIG_BB_AGC_TAB){
			READ_AND_CONFIG_MP(8192E,_AGC_TAB);
		}else if(ConfigType == CONFIG_BB_PHY_REG_PG){
			READ_AND_CONFIG_MP(8192E,_PHY_REG_PG);
		}
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723D) {
		if (ConfigType == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8723D, _PHY_REG);
		else if (ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8723D, _AGC_TAB);
		else if (ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8723D, _PHY_REG_PG);
	}
#endif
#endif//(DM_ODM_SUPPORT_TYPE !=  ODM_AP)


//1 All platforms support
#if (RTL8188E_SUPPORT == 1)
	if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		if(ConfigType == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8188E,_PHY_REG);
		else if(ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8188E,_AGC_TAB);
		else if(ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8188E,_PHY_REG_PG);
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723B) {
		if (ConfigType == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG_MP(8723B, _PHY_REG);
		} else if (ConfigType == CONFIG_BB_AGC_TAB) {
			READ_AND_CONFIG_MP(8723B, _AGC_TAB);
		} else if (ConfigType == CONFIG_BB_PHY_REG_PG) {
			READ_AND_CONFIG_MP(8723B, _PHY_REG_PG);
		}
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	if(pDM_Odm->SupportICType == ODM_RTL8814A)
	{
		if(ConfigType == CONFIG_BB_PHY_REG){
			READ_AND_CONFIG_MP(8814A,_PHY_REG);
		}else if(ConfigType == CONFIG_BB_AGC_TAB){
			READ_AND_CONFIG_MP(8814A,_AGC_TAB);
		}else if(ConfigType == CONFIG_BB_PHY_REG_PG){
			READ_AND_CONFIG_MP(8814A,_PHY_REG_PG);
		}else if(ConfigType == CONFIG_BB_PHY_REG_MP){
			READ_AND_CONFIG_MP(8814A,_PHY_REG_MP);
		}
	}
#endif
#if (RTL8703B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		if (ConfigType == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8703B, _PHY_REG);
		else if (ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8703B, _AGC_TAB);
		else if (ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8703B, _PHY_REG_PG);
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8188F) {
		if (ConfigType == CONFIG_BB_PHY_REG) 
			READ_AND_CONFIG_MP(8188F, _PHY_REG);
		else if (ConfigType == CONFIG_BB_AGC_TAB) 
			READ_AND_CONFIG_MP(8188F, _AGC_TAB);
		else if (ConfigType == CONFIG_BB_PHY_REG_PG) 
			READ_AND_CONFIG_MP(8188F, _PHY_REG_PG);
	}
#endif
#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8822B) {
		if (ConfigType == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG_MP(8822B, _PHY_REG);
		else if (ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8822B, _AGC_TAB);
		else if (ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8822B, _PHY_REG_PG);
		/*else if (ConfigType == CONFIG_BB_PHY_REG_MP)*/
			/*READ_AND_CONFIG_MP(8822B, _PHY_REG_MP);*/
	}
#endif

#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8197F) {
		if (ConfigType == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG_MP(8197F, _PHY_REG);
			if (pDM_Odm->CutVersion == ODM_CUT_A)
				phydm_phypara_a_cut(pDM_Odm);
		} else if (ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG_MP(8197F, _AGC_TAB);
/*		else if(ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG_MP(8197F, _PHY_REG_PG);
		else if(ConfigType == CONFIG_BB_PHY_REG_MP)
			READ_AND_CONFIG_MP(8197F, _PHY_REG_MP); */
	}
#endif

#if (RTL8821C_SUPPORT == 1) 
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
	{
		if (ConfigType == CONFIG_BB_PHY_REG) {
			READ_AND_CONFIG(8821C, _PHY_REG);
		} else if (ConfigType == CONFIG_BB_AGC_TAB) { 
		    READ_AND_CONFIG(8821C, _AGC_TAB);
			/* According to RFEtype, choosing correct AGC table*/
			if (pDM_Odm->RFEType == 2 || pDM_Odm->RFEType == 4 || pDM_Odm->RFEType == 7)
				AGC_DIFF_CONFIG_MP(8821C, BTG);
		} else if (ConfigType == CONFIG_BB_PHY_REG_PG) {
			READ_AND_CONFIG(8821C, _PHY_REG_PG);
		}
	}		
#endif

#if (RTL8195A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8195A)
	{
		if(ConfigType == CONFIG_BB_PHY_REG)
			READ_AND_CONFIG(8195A, _PHY_REG);
		else if(ConfigType == CONFIG_BB_AGC_TAB)
			READ_AND_CONFIG(8195A, _AGC_TAB);
		else if(ConfigType == CONFIG_BB_PHY_REG_PG)
			READ_AND_CONFIG(8195A, _PHY_REG_PG);
	}
#endif

	return HAL_STATUS_SUCCESS; 
}                 

HAL_STATUS
ODM_ConfigMACWithHeaderFile(
	IN 	PDM_ODM_T	pDM_Odm
	)
{
#if (DM_ODM_SUPPORT_TYPE &  ODM_WIN)	
	PADAPTER		Adapter = pDM_Odm->Adapter;
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
		 		("===>ODM_ConfigMACWithHeaderFile (%s)\n", (pDM_Odm->bIsMPChip) ? "MPChip" : "TestChip"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, 
				("pDM_Odm->SupportPlatform: 0x%X, pDM_Odm->SupportInterface: 0x%X, pDM_Odm->BoardType: 0x%X\n",
				pDM_Odm->SupportPlatform, pDM_Odm->SupportInterface, pDM_Odm->BoardType));

//1 AP doesn't use PHYDM initialization in these ICs
#if (DM_ODM_SUPPORT_TYPE !=  ODM_AP)	
#if (RTL8812A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8812){
		READ_AND_CONFIG_MP(8812A, _MAC_REG);
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821){
		READ_AND_CONFIG_MP(8821A, _MAC_REG);

		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("<===8821_ODM_ConfigMACwithHeaderFile\n"));
	}
#endif
#if (RTL8192E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8192E){
		READ_AND_CONFIG_MP(8192E, _MAC_REG);
	}
#endif
#if (RTL8723D_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8723D)
		READ_AND_CONFIG_MP(8723D, _MAC_REG);
#endif
#endif//(DM_ODM_SUPPORT_TYPE !=  ODM_AP)

//1 All platforms support
#if (RTL8188E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188E){
		READ_AND_CONFIG_MP(8188E, _MAC_REG);
	}
#endif
#if (RTL8723B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8723B) {
		READ_AND_CONFIG_MP(8723B, _MAC_REG);
	}
#endif
#if (RTL8814A_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8814A){
		READ_AND_CONFIG_MP(8814A, _MAC_REG);
	}
#endif
#if (RTL8703B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8703B)
		READ_AND_CONFIG_MP(8703B, _MAC_REG);
#endif
#if (RTL8188F_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188F) 
		READ_AND_CONFIG_MP(8188F, _MAC_REG);
#endif
#if (RTL8822B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8822B)
		READ_AND_CONFIG_MP(8822B, _MAC_REG);
#endif

#if (RTL8197F_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8197F)
		READ_AND_CONFIG_MP(8197F, _MAC_REG);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		READ_AND_CONFIG(8821C, _MAC_REG);
#endif

#if (RTL8195A_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8195A)
		READ_AND_CONFIG_MP(8195A, _MAC_REG);
#endif

	return HAL_STATUS_SUCCESS;    
} 

HAL_STATUS
ODM_ConfigFWWithHeaderFile(
	IN 	PDM_ODM_T			pDM_Odm,
	IN 	ODM_FW_Config_Type 	ConfigType,
	OUT u1Byte				*pFirmware,
	OUT u4Byte				*pSize
	)
{
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)

#if (RTL8188E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
	{
	#ifdef CONFIG_SFW_SUPPORTED
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8188E_T,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8188E_T,_FW_WoWLAN);
		else if(ConfigType == CONFIG_FW_NIC_2)
			READ_FIRMWARE_MP(8188E_S,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN_2)
			READ_FIRMWARE_MP(8188E_S,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		if (ConfigType == CONFIG_FW_AP)
			READ_FIRMWARE_MP(8188E_T,_FW_AP);
		else if (ConfigType == CONFIG_FW_AP_2)
			READ_FIRMWARE_MP(8188E_S,_FW_AP);
		#endif //CONFIG_AP_WOWLAN
	#else
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8188E_T,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8188E_T,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP)
			READ_FIRMWARE_MP(8188E_T,_FW_AP);
		#endif //CONFIG_AP_WOWLAN
	#endif
	}
#endif
#if (RTL8723B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8723B,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8723B,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE(8723B,_FW_AP_WoWLAN);
		#endif
		
	}
#endif //#if (RTL8723B_SUPPORT == 1)  
#if (RTL8812A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8812)
	{
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8812A,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8812A,_FW_WoWLAN);
		else if (ConfigType == CONFIG_FW_BT)
			READ_FIRMWARE_MP(8812A,_FW_NIC_BT);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE(8812A,_FW_AP);
		#endif
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821){
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8821A,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8821A,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE_MP(8821A , _FW_AP);
		#endif /*CONFIG_AP_WOWLAN*/
		else if (ConfigType == CONFIG_FW_BT)
			READ_FIRMWARE_MP(8821A,_FW_NIC_BT);
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8192E,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8192E,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE_MP(8192E,_FW_AP);
		#endif
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723D) {
		if (ConfigType == CONFIG_FW_NIC) {
			READ_FIRMWARE_MP(8723D, _FW_NIC);
		} else if (ConfigType == CONFIG_FW_WoWLAN) {
			READ_FIRMWARE_MP(8723D, _FW_WoWLAN);
		}
	}
#endif
/*#if (RTL8814A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8814A)
	{
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8814A, _FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8814A, _FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE_MP(8814A, _FW_AP);
		#endif
	}
#endif */

#if (RTL8814A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8814A)
	{
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8814A,_FW_NIC);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE_MP(8814A,_FW_AP);
		#endif
	}
#endif

#if (RTL8703B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8703B) {
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8703B, _FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8703B, _FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE(8703B, _FW_AP_WoWLAN);
		#endif
	}
#endif

#if (RTL8188F_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8188F) {
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8188F, _FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8188F, _FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP)
			READ_FIRMWARE_MP(8188F, _FW_AP);
		#endif
	}
#endif

#if (RTL8822B_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8822B) {
		
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8822B,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8822B, _FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE(8822B,_FW_AP);
		#endif
	}
#endif

#if (RTL8197F_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8197F) {
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8197F, _FW_NIC);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE(8197F , _FW_AP);
		#endif
	}
#endif

#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN))
#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C){
		if (ConfigType == CONFIG_FW_NIC)
			READ_FIRMWARE_MP(8821C,_FW_NIC);
		else if (ConfigType == CONFIG_FW_WoWLAN)
			READ_FIRMWARE_MP(8821C,_FW_WoWLAN);
		#ifdef CONFIG_AP_WOWLAN
		else if (ConfigType == CONFIG_FW_AP_WoWLAN)
			READ_FIRMWARE_MP(8821C , _FW_AP);
		#endif /*CONFIG_AP_WOWLAN*/
	}
#endif
#endif

#endif//(DM_ODM_SUPPORT_TYPE != ODM_AP)
	return HAL_STATUS_SUCCESS;    
} 

u4Byte 
ODM_GetHWImgVersion(
	IN	PDM_ODM_T	pDM_Odm
	)
{
    u4Byte  Version=0;

//1 AP doesn't use PHYDM initialization in these ICs
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
#if (RTL8821A_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8821)
		Version = GET_VERSION_MP(8821A,_MAC_REG);
#endif
#if (RTL8192E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8192E)
		Version = GET_VERSION_MP(8192E,_MAC_REG);
#endif
#if (RTL8812A_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8812)
		Version = GET_VERSION_MP(8812A,_MAC_REG);
#endif
#if (RTL8723D_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8723D)
		Version = GET_VERSION_MP(8723D, _MAC_REG);
#endif
#endif //(DM_ODM_SUPPORT_TYPE != ODM_AP)

/*1 All platforms support*/
#if (RTL8188E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
		Version = GET_VERSION_MP(8188E,_MAC_REG);
#endif
#if (RTL8723B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8723B)
		Version = GET_VERSION_MP(8723B, _MAC_REG);
#endif
#if (RTL8814A_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8814A)
		Version = GET_VERSION_MP(8814A,_MAC_REG);
#endif
#if (RTL8703B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8703B)
		Version = GET_VERSION_MP(8703B, _MAC_REG);
#endif
#if (RTL8188F_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188F)
		Version = GET_VERSION_MP(8188F, _MAC_REG);
#endif
#if (RTL8822B_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8822B)
		Version = GET_VERSION_MP(8822B, _MAC_REG);
#endif

#if (RTL8197F_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8197F)
		Version = GET_VERSION_MP(8197F, _MAC_REG);
#endif

#if (RTL8821C_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		Version = GET_VERSION(8821C, _MAC_REG);
#endif

	return Version;
}

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
/* For 8822B only!! need to move to FW finally */
/*==============================================*/

VOID
phydm_ResetPhyInfo(
	IN		PDM_ODM_T					pPhydm,
	OUT		PODM_PHY_INFO_T			pPhyInfo
)
{
	pPhyInfo->RxPWDBAll = 0;
	pPhyInfo->SignalQuality = 0;
	pPhyInfo->BandWidth = 0;
	pPhyInfo->RxCount = 0;
	ODM_Memory_Set(pPhydm, pPhyInfo->RxMIMOSignalQuality, 0 , 4);
	ODM_Memory_Set(pPhydm, pPhyInfo->RxMIMOSignalStrength, 0, 4);
	ODM_Memory_Set(pPhydm, pPhyInfo->RxSNR, 0, 4);

#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))	
	pPhyInfo->RxPower = -110;
	pPhyInfo->RecvSignalPower = -110;
	pPhyInfo->BTRxRSSIPercentage = 0;
	pPhyInfo->SignalStrength = 0;
	pPhyInfo->btCoexPwrAdjust = 0;
	pPhyInfo->channel = 0;
	pPhyInfo->bMuPacket = 0;
	pPhyInfo->bBeamformed = 0;
	pPhyInfo->rxsc = 0;
	ODM_Memory_Set(pPhydm, pPhyInfo->RxPwr, -110, 4);
	ODM_Memory_Set(pPhydm, pPhyInfo->RxMIMOEVMdbm, 0, 4);
	ODM_Memory_Set(pPhydm, pPhyInfo->Cfo_short, 0, 8);
	ODM_Memory_Set(pPhydm, pPhyInfo->Cfo_tail, 0, 8);
#endif
}

VOID
phydm_SetPerPathPhyInfo(
	IN		u1Byte							RxPath,
	IN		s1Byte							RxPwr,
	IN		s1Byte							RxEVM,
	IN		s1Byte							Cfo_tail,
	IN		s1Byte							RxSNR,
	OUT		PODM_PHY_INFO_T				pPhyInfo
)
{
	u1Byte			EVMdBm = 0;
	u1Byte			EVMPercentage = 0;

	/* SNR is S(8,1), EVM is S(8,1), CFO is S(8,7) */
	
	if (RxEVM < 0) {
		/* Calculate EVM in dBm */
		EVMdBm = ((u1Byte)(0 - RxEVM) >> 1);

		/* Calculate EVM in percentage */
		if (EVMdBm >= 33)
			EVMPercentage = 100;
		else 
			EVMPercentage = (EVMdBm << 1) + (EVMdBm);
	}
	

#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
	pPhyInfo->RxPwr[RxPath] = RxPwr;
	pPhyInfo->RxMIMOEVMdbm[RxPath] = EVMdBm;

	/* CFO = CFO_tail * 312.5 / 2^7 ~= CFO tail * 39/512 (kHz)*/
	pPhyInfo->Cfo_tail[RxPath] = Cfo_tail;
	pPhyInfo->Cfo_tail[RxPath] = ((pPhyInfo->Cfo_tail[RxPath] << 5) + (pPhyInfo->Cfo_tail[RxPath] << 2) +
		(pPhyInfo->Cfo_tail[RxPath] << 1) + (pPhyInfo->Cfo_tail[RxPath])) >> 9;
#endif

	pPhyInfo->RxMIMOSignalStrength[RxPath] = odm_QueryRxPwrPercentage(RxPwr);
	pPhyInfo->RxMIMOSignalQuality[RxPath] = EVMPercentage;
	pPhyInfo->RxSNR[RxPath] = RxSNR >> 1;

/*
	//if (pPktinfo->bPacketMatchBSSID) 
	{
		DbgPrint("Path (%d)--------\n", RxPath);
		DbgPrint("RxPwr = %d, Signal strength = %d\n", pPhyInfo->RxPwr[RxPath], pPhyInfo->RxMIMOSignalStrength[RxPath]);
		DbgPrint("EVMdBm = %d, Signal quality = %d\n", pPhyInfo->RxMIMOEVMdbm[RxPath], pPhyInfo->RxMIMOSignalQuality[RxPath]);
		DbgPrint("CFO = %d, SNR = %d\n", pPhyInfo->Cfo_tail[RxPath], pPhyInfo->RxSNR[RxPath]);
	}	
*/
}

VOID
phydm_SetCommonPhyInfo(
	IN		s1Byte							RxPower,
	IN		u1Byte							channel,
	IN		BOOLEAN							bBeamformed,
	IN		BOOLEAN							bMuPacket,
	IN		u1Byte							bandwidth,
	IN		u1Byte							signalQuality,
	IN		u1Byte							rxsc,
	OUT		PODM_PHY_INFO_T				pPhyInfo
)
{
#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))
	pPhyInfo->RxPower = RxPower;											/* RSSI in dB */
	pPhyInfo->RecvSignalPower = RxPower;										/* RSSI in dB */
	pPhyInfo->channel = channel;												/* channel number */
	pPhyInfo->bBeamformed = bBeamformed;									/* apply BF */
	pPhyInfo->bMuPacket = bMuPacket;										/* MU packet */
	pPhyInfo->rxsc = rxsc;
#endif
	pPhyInfo->RxPWDBAll = odm_QueryRxPwrPercentage(RxPower);				/* RSSI in percentage */
	pPhyInfo->SignalQuality = signalQuality;										/* signal quality */
	pPhyInfo->BandWidth = bandwidth;											/* bandwidth */

/*
	//if (pPktinfo->bPacketMatchBSSID)
	{
		DbgPrint("RxPWDBAll = %d, RxPower = %d, RecvSignalPower = %d\n", pPhyInfo->RxPWDBAll, pPhyInfo->RxPower, pPhyInfo->RecvSignalPower);
		DbgPrint("SignalQuality = %d\n", pPhyInfo->SignalQuality);
		DbgPrint("bBeamformed = %d, bMuPacket = %d, RxCount = %d\n", pPhyInfo->bBeamformed, pPhyInfo->bMuPacket, pPhyInfo->RxCount + 1);
		DbgPrint("channel = %d, rxsc = %d, BandWidth = %d\n", channel, rxsc, bandwidth);
	}
*/
}

VOID
phydm_GetRxPhyStatusType0(
	IN		PDM_ODM_T						pDM_Odm,
	IN		pu1Byte							pPhyStatus,
	IN		PODM_PACKET_INFO_T				pPktinfo,
	OUT		PODM_PHY_INFO_T				pPhyInfo
)
{
	/* Type 0 is used for cck packet */
	
	PPHY_STATUS_RPT_JAGUAR2_TYPE0	pPhyStaRpt = (PPHY_STATUS_RPT_JAGUAR2_TYPE0)pPhyStatus;
	u1Byte							i, SQ = 0;
	s1Byte							RxPower = pPhyStaRpt->pwdb - 110;


#if (RTL8723D_SUPPORT == 1)
		if (pDM_Odm->SupportICType & ODM_RTL8723D)
			RxPower = pPhyStaRpt->pwdb - 97;
#endif

	/* Calculate Signal Quality*/
	if (pPktinfo->bPacketMatchBSSID) {
		if (pPhyStaRpt->signal_quality >= 64)
			SQ = 0;
		else if (pPhyStaRpt->signal_quality <= 20)
			SQ = 100;
		else {
			/* mapping to 2~99% */
			SQ = 64 - pPhyStaRpt->signal_quality;
			SQ = ((SQ << 3) + SQ) >> 2;
		}
	}

	/* Modify CCK PWDB if old AGC */
	if (pDM_Odm->cck_new_agc == FALSE) {
		u1Byte	LNA_idx, VGA_idx;

#if (RTL8197F_SUPPORT == 1)		
		if (pDM_Odm->SupportICType & ODM_RTL8197F)
			LNA_idx = pPhyStaRpt->lna_l;
		else
#endif
			LNA_idx = ((pPhyStaRpt->lna_h << 3) | pPhyStaRpt->lna_l);
		VGA_idx = pPhyStaRpt->vga;
	
#if (RTL8723D_SUPPORT == 1)
		if (pDM_Odm->SupportICType & ODM_RTL8723D)
			RxPower = odm_CCKRSSI_8723D(LNA_idx, VGA_idx);
#endif
#if (RTL8822B_SUPPORT == 1)
		/* Need to do !! */
		/*if (pDM_Odm->SupportICType & ODM_RTL8822B) */
			/*RxPower = odm_CCKRSSI_8822B(LNA_idx, VGA_idx);*/
#endif
#if (RTL8197F_SUPPORT == 1)
		if (pDM_Odm->SupportICType & ODM_RTL8197F) 
			RxPower = odm_CCKRSSI_8197F(pDM_Odm, LNA_idx, VGA_idx);
#endif
	}

	/* Update CCK packet counter */
	pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK++;

	/*CCK no STBC and LDPC*/
	pDM_Odm->PhyDbgInfo.bLdpcPkt = FALSE;
	pDM_Odm->PhyDbgInfo.bStbcPkt = FALSE;

	/* Update Common information */
	phydm_SetCommonPhyInfo(RxPower, pPhyStaRpt->channel, FALSE, 
		FALSE, ODM_BW20M, SQ, pPhyStaRpt->rxsc, pPhyInfo);

	/* Update CCK pwdb */
	phydm_SetPerPathPhyInfo(ODM_RF_PATH_A, RxPower, 0, 0, 0, pPhyInfo);					/* Update per-path information */

	pDM_Odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->antidx_a;
	pDM_Odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->antidx_b;
	pDM_Odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antidx_c;
	pDM_Odm->DM_FatTable.antsel_rx_keep_3 = pPhyStaRpt->antidx_d;
/*
	//if (pPktinfo->bPacketMatchBSSID)
	{
		DbgPrint("pwdb = 0x%x, MP gain index = 0x%x, TRSW = 0x%x\n", pPhyStaRpt->pwdb, pPhyStaRpt->gain, pPhyStaRpt->trsw);
		DbgPrint("channel = %d, band = %d, rxsc = %d\n", pPhyStaRpt->channel, pPhyStaRpt->band, pPhyStaRpt->rxsc);
		DbgPrint("agc_table = 0x%x, agc_rpt 0x%x, bb_power = 0x%x\n", pPhyStaRpt->agc_table, pPhyStaRpt->agc_rpt, pPhyStaRpt->bb_power);
		DbgPrint("length = %d, SQ = %d\n", pPhyStaRpt->length, pPhyStaRpt->signal_quality);
		DbgPrint("antidx a = 0x%x, b = 0x%x, c = 0x%x, d = 0x%x\n", pPhyStaRpt->antidx_a, pPhyStaRpt->antidx_b, pPhyStaRpt->antidx_c, pPhyStaRpt->antidx_d);
		DbgPrint("rsvd_0 = 0x%x, rsvd_1 = 0x%x, rsvd_2 = 0x%x\n", pPhyStaRpt->rsvd_0, pPhyStaRpt->rsvd_1, pPhyStaRpt->rsvd_2);
		DbgPrint("rsvd_3 = 0x%x, rsvd_4 = 0x%x, rsvd_5 = 0x%x\n", pPhyStaRpt->rsvd_3, pPhyStaRpt->rsvd_4, pPhyStaRpt->rsvd_5);
		DbgPrint("rsvd_6 = 0x%x, rsvd_7 = 0x%x, rsvd_8 = 0x%x\n", pPhyStaRpt->rsvd_6, pPhyStaRpt->rsvd_7, pPhyStaRpt->rsvd_8);
	}
*/
}

VOID
phydm_GetRxPhyStatusType1(
	IN		PDM_ODM_T						pDM_Odm,
	IN		pu1Byte							pPhyStatus,
	IN		PODM_PACKET_INFO_T				pPktinfo,
	OUT		PODM_PHY_INFO_T				pPhyInfo
)
{
	/* Type 1 is used for ofdm packet */

	PPHY_STATUS_RPT_JAGUAR2_TYPE1	pPhyStaRpt = (PPHY_STATUS_RPT_JAGUAR2_TYPE1)pPhyStatus;
	s1Byte							rx_pwr_db = -120;
	u1Byte							i, rxsc, bw = ODM_BW20M, RxCount = 0;
	BOOLEAN							bMU;
	u1Byte							num_ss;

	/* Update OFDM packet counter */
	pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

	/* Update per-path information */
	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (pDM_Odm->RXAntStatus & BIT(i)) {
			s1Byte	rx_path_pwr_db;

			/* RX path counter */
			RxCount++;

			/* Update per-path information (RSSI_dB RSSI_percentage EVM SNR CFO SQ) */
			/* EVM report is reported by stream, not path */
			rx_path_pwr_db = pPhyStaRpt->pwdb[i] - 110;					/* per-path pwdb in dB domain */
			phydm_SetPerPathPhyInfo(i, rx_path_pwr_db, pPhyStaRpt->rxevm[RxCount - 1], 
				pPhyStaRpt->cfo_tail[i], pPhyStaRpt->rxsnr[i], pPhyInfo);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (RxCount > 0)
		pPhyInfo->RxCount = RxCount - 1;
	
	/* Check if MU packet or not */
	if ((pPhyStaRpt->gid != 0) && (pPhyStaRpt->gid != 63)) {
		bMU = TRUE;
		pDM_Odm->PhyDbgInfo.NumQryMuPkt++;
	} else
		bMU = FALSE;

	/* Count BF packet */
	pDM_Odm->PhyDbgInfo.NumQryBfPkt = pDM_Odm->PhyDbgInfo.NumQryBfPkt + pPhyStaRpt->beamformed;

	/*STBC or LDPC pkt*/
	pDM_Odm->PhyDbgInfo.bLdpcPkt = pPhyStaRpt->ldpc;
	pDM_Odm->PhyDbgInfo.bStbcPkt = pPhyStaRpt->stbc;

	/* Check sub-channel */
	if ((pPktinfo->DataRate > ODM_RATE11M) && (pPktinfo->DataRate < ODM_RATEMCS0))
		rxsc = pPhyStaRpt->l_rxsc;
	else
		rxsc = pPhyStaRpt->ht_rxsc;

	/* Check RX bandwidth */
	if (pDM_Odm->SupportICType & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = ODM_BW20M;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = ODM_BW40M;
		else if (rxsc >= 13)
			bw = ODM_BW80M;
		else
			bw = pPhyStaRpt->rf_mode;
	} else if (pDM_Odm->SupportICType & (ODM_RTL8197F|ODM_RTL8723D)) {
		if (pPhyStaRpt->rf_mode == 0)
			bw = ODM_BW20M;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = ODM_BW20M;
		else
			bw = ODM_BW40M;
	}

	/* Update packet information */
	phydm_SetCommonPhyInfo(rx_pwr_db, pPhyStaRpt->channel, (BOOLEAN)pPhyStaRpt->beamformed,
		bMU, bw, odm_EVMdbToPercentage(pPhyStaRpt->rxevm[0]), rxsc, pPhyInfo);

	num_ss =phydm_rate_to_num_ss(pDM_Odm, pPktinfo->DataRate );
	
	ODM_ParsingCFO(pDM_Odm, pPktinfo, pPhyStaRpt->cfo_tail, num_ss);
	pDM_Odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->antidx_a;
	pDM_Odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->antidx_b;
	pDM_Odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antidx_c;
	pDM_Odm->DM_FatTable.antsel_rx_keep_3 = pPhyStaRpt->antidx_d;

	if (pPktinfo->bPacketMatchBSSID) {
/*
		DbgPrint("channel = %d, band = %d, l_rxsc = %d, ht_rxsc = %d, rf_mode = %d\n", pPhyStaRpt->channel, pPhyStaRpt->band, pPhyStaRpt->l_rxsc, pPhyStaRpt->ht_rxsc, pPhyStaRpt->rf_mode);
		DbgPrint("Antidx A = %d, B = %d, C = %d, D = %d\n", pPhyStaRpt->antidx_a, pPhyStaRpt->antidx_b, pPhyStaRpt->antidx_c, pPhyStaRpt->antidx_d);
		DbgPrint("pwdb A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->pwdb[0], pPhyStaRpt->pwdb[1], pPhyStaRpt->pwdb[2], pPhyStaRpt->pwdb[3]);
		DbgPrint("EVM  A: %d, B: %d, C: %d, D: %d\n", pPhyStaRpt->rxevm[0], pPhyStaRpt->rxevm[1], pPhyStaRpt->rxevm[2], pPhyStaRpt->rxevm[3]);
		DbgPrint("SNR  A: %d, B: %d, C: %d, D: %d\n", pPhyStaRpt->rxsnr[0], pPhyStaRpt->rxsnr[1], pPhyStaRpt->rxsnr[2], pPhyStaRpt->rxsnr[3]);
		DbgPrint("CFO  A: %d, B: %d, C: %d, D: %d\n", pPhyStaRpt->cfo_tail[0], pPhyStaRpt->cfo_tail[1], pPhyStaRpt->cfo_tail[2], pPhyStaRpt->cfo_tail[3]);
		DbgPrint("paid = %d, gid = %d, length = %d\n", (pPhyStaRpt->paid + (pPhyStaRpt->paid_msb<<8)), pPhyStaRpt->gid, pPhyStaRpt->lsig_length);
		DbgPrint("ldpc: %d, stbc: %d, bf: %d, gnt_bt: %d, antsw: %d\n", pPhyStaRpt->ldpc, pPhyStaRpt->stbc, pPhyStaRpt->beamformed, pPhyStaRpt->gnt_bt, pPhyStaRpt->hw_antsw_occu);
		DbgPrint("NBI: %d, pos: %d\n", pPhyStaRpt->nb_intf_flag, (pPhyStaRpt->intf_pos + (pPhyStaRpt->intf_pos_msb<<8)));
		DbgPrint("rsvd_0 = %d, rsvd_1 = %d, rsvd_2 = %d, rsvd_3 = %d, rsvd_4 = %d, rsvd_5 = %d\n", pPhyStaRpt->rsvd_0, pPhyStaRpt->rsvd_1, pPhyStaRpt->rsvd_2, pPhyStaRpt->rsvd_3, pPhyStaRpt->rsvd_4, pPhyStaRpt->rsvd_5);
*/
		if ((pPhyStaRpt->gid != 0) && (pPhyStaRpt->gid != 63)) {
			if (pPktinfo->DataRate >= ODM_RATEVHTSS1MCS0)
				pDM_Odm->PhyDbgInfo.NumQryMuVhtPkt[pPktinfo->DataRate - 0x2C]++;
		} else {
			if (pPktinfo->DataRate >= ODM_RATEVHTSS1MCS0)
				pDM_Odm->PhyDbgInfo.NumQryVhtPkt[pPktinfo->DataRate - 0x2C]++;
		}
	}
/*
	DbgPrint("phydm_GetRxPhyStatusType1   pPktinfo->bPacketMatchBSSID = %d\n", pPktinfo->bPacketMatchBSSID);
	DbgPrint("pPktinfo->DataRate = 0x%x\n", pPktinfo->DataRate);
*/
}

VOID
phydm_GetRxPhyStatusType2(
	IN		PDM_ODM_T						pDM_Odm,
	IN		pu1Byte							pPhyStatus,
	IN		PODM_PACKET_INFO_T				pPktinfo,
	OUT		PODM_PHY_INFO_T				pPhyInfo
)
{
	PPHY_STATUS_RPT_JAGUAR2_TYPE2	pPhyStaRpt = (PPHY_STATUS_RPT_JAGUAR2_TYPE2)pPhyStatus;
	s1Byte							rx_pwr_db = -120;
	u1Byte							i, rxsc, bw = ODM_BW20M, RxCount = 0;

	/* Update OFDM packet counter */
	pDM_Odm->PhyDbgInfo.NumQryPhyStatusOFDM++;

	/* Update per-path information */
	for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (pDM_Odm->RXAntStatus & BIT(i)) {
			s1Byte	rx_path_pwr_db;

			/* RX path counter */
			RxCount++;

			/* Update per-path information (RSSI_dB RSSI_percentage EVM SNR CFO SQ) */
#if (RTL8197F_SUPPORT == 1)
		if ((pDM_Odm->SupportICType & ODM_RTL8197F) && (pPhyStaRpt->pwdb[i] == 0x7f)) { /*for 97f workaround*/
			
			if (i == ODM_RF_PATH_A) {
				rx_path_pwr_db = (pPhyStaRpt->gain_a)<<1;
				rx_path_pwr_db = rx_path_pwr_db - 110;
			} else if (i == ODM_RF_PATH_B) {
				rx_path_pwr_db = (pPhyStaRpt->gain_b)<<1;
				rx_path_pwr_db = rx_path_pwr_db - 110;
			} else
				rx_path_pwr_db = 0;
		} else
#endif
			rx_path_pwr_db = pPhyStaRpt->pwdb[i] - 110;					/* per-path pwdb in dB domain */
		
			phydm_SetPerPathPhyInfo(i, rx_path_pwr_db, 0, 0, 0, pPhyInfo);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (RxCount > 0)
		pPhyInfo->RxCount = RxCount - 1;
	
	/* Check RX sub-channel */
	if ((pPktinfo->DataRate > ODM_RATE11M) && (pPktinfo->DataRate < ODM_RATEMCS0))
		rxsc = pPhyStaRpt->l_rxsc;
	else
		rxsc = pPhyStaRpt->ht_rxsc;

	/*STBC or LDPC pkt*/
	pDM_Odm->PhyDbgInfo.bLdpcPkt = pPhyStaRpt->ldpc;
	pDM_Odm->PhyDbgInfo.bStbcPkt = pPhyStaRpt->stbc;

	/* Check RX bandwidth */
	/* the BW information of sc=0 is useless, because there is no information of RF mode*/

	if (pDM_Odm->SupportICType & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = ODM_BW20M;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = ODM_BW40M;
		else if (rxsc >= 13)
			bw = ODM_BW80M;
		else
			bw = ODM_BW20M;
	} else if (pDM_Odm->SupportICType & (ODM_RTL8197F|ODM_RTL8723D)) {
		if (rxsc == 3)
			bw = ODM_BW40M;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = ODM_BW20M;
		else
			bw = ODM_BW20M;
	}

	/* Update packet information */
	phydm_SetCommonPhyInfo(rx_pwr_db, pPhyStaRpt->channel, (BOOLEAN)pPhyStaRpt->beamformed,
		FALSE, bw, 0, rxsc, pPhyInfo);

/*
	//if (pPktinfo->bPacketMatchBSSID)
	{
		DbgPrint("channel = %d, band = %d, l_rxsc = %d, ht_rxsc = %d\n", pPhyStaRpt->channel, pPhyStaRpt->band, pPhyStaRpt->l_rxsc, pPhyStaRpt->ht_rxsc);
		DbgPrint("pwdb A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->pwdb[0], pPhyStaRpt->pwdb[1], pPhyStaRpt->pwdb[2], pPhyStaRpt->pwdb[3]);
		DbgPrint("Agc table A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->agc_table_a, pPhyStaRpt->agc_table_b, pPhyStaRpt->agc_table_c, pPhyStaRpt->agc_table_d);
		DbgPrint("Gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->gain_a, pPhyStaRpt->gain_b, pPhyStaRpt->gain_c, pPhyStaRpt->gain_d);
		DbgPrint("TRSW A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->trsw_a, pPhyStaRpt->trsw_b, pPhyStaRpt->trsw_c, pPhyStaRpt->trsw_d);
		DbgPrint("AAGC step A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->aagc_step_a, pPhyStaRpt->aagc_step_b, pPhyStaRpt->aagc_step_c, pPhyStaRpt->aagc_step_d);
		DbgPrint("HT AAGC gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->ht_aagc_gain[0], pPhyStaRpt->ht_aagc_gain[1], pPhyStaRpt->ht_aagc_gain[2], pPhyStaRpt->ht_aagc_gain[3]);
		DbgPrint("DAGC gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", pPhyStaRpt->dagc_gain[0], pPhyStaRpt->dagc_gain[1], pPhyStaRpt->dagc_gain[2], pPhyStaRpt->dagc_gain[3]);
		DbgPrint("ldpc: %d, stbc: %d, bf: %d, gnt_bt: %d, antsw: %d\n", pPhyStaRpt->ldpc, pPhyStaRpt->stbc, pPhyStaRpt->beamformed, pPhyStaRpt->gnt_bt, pPhyStaRpt->hw_antsw_occu);
		DbgPrint("counter: %d, syn_count: %d\n", pPhyStaRpt->counter, pPhyStaRpt->syn_count);
		DbgPrint("cnt_cca2agc_rdy: %d, cnt_pw2cca: %d, shift_l_map\n", pPhyStaRpt->cnt_cca2agc_rdy, pPhyStaRpt->cnt_pw2cca, pPhyStaRpt->shift_l_map);
		DbgPrint("rsvd_0 = %d, rsvd_1 = %d, rsvd_2 = %d, rsvd_3 = %d, rsvd_4 = %d, rsvd_5 = %d\n", pPhyStaRpt->rsvd_0, pPhyStaRpt->rsvd_1, pPhyStaRpt->rsvd_2, pPhyStaRpt->rsvd_3, pPhyStaRpt->rsvd_4);
		DbgPrint("rsvd_5 = %d, rsvd_6 = %d, rsvd_6 = %d\n", pPhyStaRpt->rsvd_5, pPhyStaRpt->rsvd_6, pPhyStaRpt->rsvd_7);
	}
*/
}

VOID
phydm_GetRxPhyStatusType5(
	IN		pu1Byte				pPhyStatus
)
{
/*
	DbgPrint("DW0: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 3), *(pPhyStatus + 2), *(pPhyStatus + 1), *(pPhyStatus + 0));
	DbgPrint("DW1: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 7), *(pPhyStatus + 6), *(pPhyStatus + 5), *(pPhyStatus + 4));
	DbgPrint("DW2: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 11), *(pPhyStatus + 10), *(pPhyStatus + 9), *(pPhyStatus + 8));
	DbgPrint("DW3: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 15), *(pPhyStatus + 14), *(pPhyStatus + 13), *(pPhyStatus + 12));
	DbgPrint("DW4: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 19), *(pPhyStatus + 18), *(pPhyStatus + 17), *(pPhyStatus + 16));
	DbgPrint("DW5: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 23), *(pPhyStatus + 22), *(pPhyStatus + 21), *(pPhyStatus + 20));
	DbgPrint("DW6: 0x%02x%02x%02x%02x\n", *(pPhyStatus + 27), *(pPhyStatus + 26), *(pPhyStatus + 25), *(pPhyStatus + 24));
*/
}

VOID
phydm_Process_RSSIForDMNewType(	
	IN OUT	PDM_ODM_T					pDM_Odm,
	IN		PODM_PHY_INFO_T			pPhyInfo,
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	s4Byte				UndecoratedSmoothedPWDB, AccumulatePWDB;
	u4Byte				RSSI_Ave;
	u1Byte				i;
	PSTA_INFO_T			pEntry;
	u1Byte				scaling_factor = 4;
	
	if (pPktinfo->StationID >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	pEntry = pDM_Odm->pODM_StaInfo[pPktinfo->StationID];							

	if (!IS_STA_VALID(pEntry))
		return;

	if ((!pPktinfo->bPacketMatchBSSID))/*data frame only*/
		return;

	if (pPktinfo->bPacketBeacon)
		pDM_Odm->PhyDbgInfo.NumQryBeaconPkt++;

	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	if (pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)
		ODM_Process_RSSIForAntDiv(pDM_Odm,pPhyInfo,pPktinfo);
	#endif
	
	if (pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon) {
		u4Byte RSSI_linear = 0;

		pDM_Odm->RxRate = pPktinfo->DataRate;
		UndecoratedSmoothedPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;
		AccumulatePWDB = pDM_Odm->AccumulatePWDB[pPktinfo->StationID];
		pDM_Odm->RSSI_A = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
		pDM_Odm->RSSI_B = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
		pDM_Odm->RSSI_C = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_C];
		pDM_Odm->RSSI_D = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_D];

		for (i = ODM_RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (pPhyInfo->RxMIMOSignalStrength[i] != 0)
				RSSI_linear += odm_ConvertTo_linear(pPhyInfo->RxMIMOSignalStrength[i]);
		}

		switch (pPhyInfo->RxCount + 1) {
		case 2:
			RSSI_linear = (RSSI_linear >> 1);
			break;
		case 3:
			RSSI_linear = ((RSSI_linear) + (RSSI_linear << 1) + (RSSI_linear << 3)) >> 5;	/* RSSI_linear/3 ~ RSSI_linear*11/32 */
			break;
		case 4:
			RSSI_linear = (RSSI_linear >> 2);
			break;
		}
		RSSI_Ave = odm_ConvertTo_dB(RSSI_linear);

		if (UndecoratedSmoothedPWDB <= 0) {
			AccumulatePWDB = (pPhyInfo->RxPWDBAll << scaling_factor);
			UndecoratedSmoothedPWDB = pPhyInfo->RxPWDBAll;
		} else {
			AccumulatePWDB = AccumulatePWDB - (AccumulatePWDB>>scaling_factor) + RSSI_Ave;
			UndecoratedSmoothedPWDB = (AccumulatePWDB + (1<<(scaling_factor - 1)))>>scaling_factor;
		}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
		if (pEntry->rssi_stat.UndecoratedSmoothedPWDB == -1)
			phydm_ra_rssi_rpt_wk(pDM_Odm);
#endif
		pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
		pDM_Odm->AccumulatePWDB[pPktinfo->StationID] = AccumulatePWDB;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))		
		if (pPktinfo->StationID == 0) { 
			HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pDM_Odm->Adapter);
			
			pHalData->UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;
		}
#endif
	}
}

VOID
phydm_RxPhyStatusNewType(
	IN		PDM_ODM_T					pPhydm,
	IN		pu1Byte						pPhyStatus,
	IN		PODM_PACKET_INFO_T			pPktinfo,
	OUT		PODM_PHY_INFO_T			pPhyInfo
)
{
	u1Byte		phy_status_type = (*pPhyStatus & 0xf);

	/*DbgPrint("phydm_RxPhyStatusNewType================> (page: %d)\n", phy_status_type);*/
	
	/* Memory reset */
	phydm_ResetPhyInfo(pPhydm, pPhyInfo);

	/* Phy status parsing */
	switch (phy_status_type) {
	case 0:
	{
		phydm_GetRxPhyStatusType0(pPhydm, pPhyStatus, pPktinfo, pPhyInfo);
		break;
	}
	case 1:
	{
		phydm_GetRxPhyStatusType1(pPhydm, pPhyStatus, pPktinfo, pPhyInfo);
		break;
	}
	case 2:
	{
		phydm_GetRxPhyStatusType2(pPhydm, pPhyStatus, pPktinfo, pPhyInfo);
		break;
	}
/*
	case 5:
	{
		phydm_GetRxPhyStatusType5(pPhyStatus);
		return;
	}
*/
	default:
		return;
	}

	/* Update signal strength to UI, and pPhyInfo->RxPWDBAll is the maximum RSSI of all path */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	pPhyInfo->SignalStrength = SignalScaleProc(pPhydm->Adapter, pPhyInfo->RxPWDBAll, FALSE, FALSE);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pPhydm, pPhyInfo->RxPWDBAll));
#endif

	/* Calculate average RSSI and smoothed RSSI */
	phydm_Process_RSSIForDMNewType(pPhydm, pPhyInfo, pPktinfo);

}
/*==============================================*/
#endif

u4Byte
query_phydm_trx_capability(
	IN		PDM_ODM_T					pDM_Odm
)
{
	u4Byte value32 = 0xFFFFFFFF;

	#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		value32 = query_phydm_trx_capability_8821c(pDM_Odm);
	#endif

	return value32;
}

u4Byte
query_phydm_stbc_capability(
	IN		PDM_ODM_T					pDM_Odm
)
{
	u4Byte value32 = 0xFFFFFFFF;

	#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		value32 = query_phydm_stbc_capability_8821c(pDM_Odm);
	#endif

	return value32;
}

u4Byte
query_phydm_ldpc_capability(
	IN		PDM_ODM_T					pDM_Odm
)
{
	u4Byte value32 = 0xFFFFFFFF;

	#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		value32 = query_phydm_ldpc_capability_8821c(pDM_Odm);
	#endif

	return value32;
}

u4Byte
query_phydm_txbf_parameters(
	IN		PDM_ODM_T					pDM_Odm
)
{
	u4Byte value32 = 0xFFFFFFFF;

	#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		value32 = query_phydm_txbf_parameters_8821c(pDM_Odm);
	#endif

	return value32;
}

u4Byte
query_phydm_txbf_capability(
	IN		PDM_ODM_T					pDM_Odm
)
{
	u4Byte value32 = 0xFFFFFFFF;

	#if (RTL8821C_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8821C)
		value32 = query_phydm_txbf_capability_8821c(pDM_Odm);
	#endif

	return value32;
}
