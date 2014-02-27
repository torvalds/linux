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

#include "odm_precomp.h"

#if (RTL8188E_FOR_TEST_CHIP > 1)
    #define READ_AND_CONFIG(ic, txt) do {\
                                            if (pDM_Odm->bIsMPChip)\
                                    		    READ_AND_CONFIG_MP(ic,txt);\
                                            else\
                                                READ_AND_CONFIG_TC(ic,txt);\
                                     } while(0)
#elif (RTL8188E_FOR_TEST_CHIP == 1)
    #define READ_AND_CONFIG     READ_AND_CONFIG_TC
#else
    #define READ_AND_CONFIG     READ_AND_CONFIG_MP
#endif

#define READ_AND_CONFIG_MP(ic, txt) (ODM_ReadAndConfig##txt##ic(pDM_Odm))
#define READ_AND_CONFIG_TC(ic, txt) (ODM_ReadAndConfig_TC##txt##ic(pDM_Odm))

u1Byte
odm_QueryRxPwrPercentage(
	IN		s1Byte		AntPower
	)
{
	if ((AntPower <= -100) || (AntPower >= 20))
	{
		return	0;
	}
	else if (AntPower >= 0)
	{
		return	100;
	}
	else
	{
		return	(100+AntPower);
	}
	
}

#if (DM_ODM_SUPPORT_TYPE != ODM_MP)
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
	s4Byte RetSig;
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	//if(pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) 
	{
		// Step 1. Scale mapping.
		// 20100611 Joseph: Re-tunning RSSI presentation for Lenovo.
		// 20100426 Joseph: Modify Signal strength mapping.
		// This modification makes the RSSI indication similar to Intel solution.
		// 20100414 Joseph: Tunning RSSI for Lenovo according to RTL8191SE.
		if(CurrSig >= 54 && CurrSig <= 100)
		{
			RetSig = 100;
		}
		else if(CurrSig>=42 && CurrSig <= 53 )
		{
			RetSig = 95;
		}
		else if(CurrSig>=36 && CurrSig <= 41 )
		{
			RetSig = 74 + ((CurrSig - 36) *20)/6;
		}
		else if(CurrSig>=33 && CurrSig <= 35 )
		{
			RetSig = 65 + ((CurrSig - 33) *8)/2;
		}
		else if(CurrSig>=18 && CurrSig <= 32 )
		{
			RetSig = 62 + ((CurrSig - 18) *2)/15;
		}
		else if(CurrSig>=15 && CurrSig <= 17 )
		{
			RetSig = 33 + ((CurrSig - 15) *28)/2;
		}
		else if(CurrSig>=10 && CurrSig <= 14 )
		{
			RetSig = 39;
		}
		else if(CurrSig>=8 && CurrSig <= 9 )
		{
			RetSig = 33;
		}
		else if(CurrSig <= 8 )
		{
			RetSig = 19;
		}
	}
#endif //ENDIF (DM_ODM_SUPPORT_TYPE == ODM_MP)
	return RetSig;
}

s4Byte
odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Netcore(	
	IN OUT PDM_ODM_T pDM_Odm,
	s4Byte CurrSig 
)
{
	s4Byte RetSig;
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	//if(pDM_Odm->SupportInterface  == ODM_ITRF_USB)
	{
		// Netcore request this modification because 2009.04.13 SU driver use it. 
		if(CurrSig >= 31 && CurrSig <= 100)
		{
			RetSig = 100;
		}	
		else if(CurrSig >= 21 && CurrSig <= 30)
		{
			RetSig = 90 + ((CurrSig - 20) / 1);
		}
		else if(CurrSig >= 11 && CurrSig <= 20)
		{
			RetSig = 80 + ((CurrSig - 10) / 1);
		}
		else if(CurrSig >= 7 && CurrSig <= 10)
		{
			RetSig = 69 + (CurrSig - 7);
		}
		else if(CurrSig == 6)
		{
			RetSig = 54;
		}
		else if(CurrSig == 5)
		{
			RetSig = 45;
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
#endif //ENDIF (DM_ODM_SUPPORT_TYPE == ODM_MP)
	return RetSig;
}


s4Byte
odm_SignalScaleMapping_92CSeries(	
	IN OUT PDM_ODM_T pDM_Odm,
	IN s4Byte CurrSig 
)
{
	s4Byte RetSig;
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE) 
	if(pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) 
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
	if((pDM_Odm->SupportInterface  == ODM_ITRF_USB) || (pDM_Odm->SupportInterface  == ODM_ITRF_SDIO) )
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
	if(	(pDM_Odm->SupportPlatform == ODM_MP) && 
		(pDM_Odm->SupportInterface  != ODM_ITRF_PCIE) && //USB & SDIO
		(pDM_Odm->PatchID==10))//pMgntInfo->CustomerID == RT_CID_819x_Netcore
	{
		return odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Netcore(pDM_Odm,CurrSig);
	}
	else if(	(pDM_Odm->SupportPlatform == ODM_MP) && 
			(pDM_Odm->SupportInterface  == ODM_ITRF_PCIE) &&
			(pDM_Odm->PatchID==19))//pMgntInfo->CustomerID == RT_CID_819x_Lenovo)
	{
		return odm_SignalScaleMapping_92CSeries_patch_RT_CID_819x_Lenovo(pDM_Odm, CurrSig);
	}
	else{		
		return odm_SignalScaleMapping_92CSeries(pDM_Odm,CurrSig);
	}
	
}
#endif

//pMgntInfo->CustomerID == RT_CID_819x_Lenovo
static u1Byte odm_SQ_process_patch_RT_CID_819x_Lenovo(
	IN PDM_ODM_T	pDM_Odm,
	IN u1Byte 		isCCKrate,
	IN u1Byte 		PWDB_ALL,
	IN u1Byte 		path,
	IN u1Byte 		RSSI
)
{
	u1Byte	SQ;
#if (DM_ODM_SUPPORT_TYPE &  ODM_MP)			
	// mapping to 5 bars for vista signal strength
	// signal quality in driver will be displayed to signal strength
	if(isCCKrate){
		// in vista.
		if(PWDB_ALL >= 50)
			SQ = 100;
		else if(PWDB_ALL >= 35 && PWDB_ALL < 50)				
			SQ = 80;
		else if(PWDB_ALL >= 22 && PWDB_ALL < 35)
			SQ = 60;
		else if(PWDB_ALL >= 18 && PWDB_ALL < 22)
			SQ = 40;
		else
			SQ = 20;
	}
	else{//OFDM rate		
		
		// mapping to 5 bars for vista signal strength
		// signal quality in driver will be displayed to signal strength
		// in vista.
		if(RSSI >= 50)
			SQ = 100;
		else if(RSSI >= 35 && RSSI < 50)
			SQ = 80;
		else if(RSSI >= 22 && RSSI < 35)
			SQ = 60;
		else if(RSSI >= 18 && RSSI < 22)
			SQ = 40;
		else
			SQ = 20;			
	}
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
	//ret_val /= 2;

	//ODM_RTPRINT(FRX, RX_PHY_SQ, ("EVMdbToPercentage92C Value=%d / %x \n", ret_val, ret_val));
		
	if(ret_val >= 0)
		ret_val = 0;
	if(ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val*=3;

	if(ret_val == 99)
		ret_val = 100;

	return(ret_val);
}
			


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
	u1Byte				isCCKrate=0;	
	u1Byte				rf_rx_num = 0;
	u1Byte				cck_highpwr = 0;
	u1Byte				LNA_idx, VGA_idx;
	
	PPHY_STATUS_RPT_8192CD_T pPhyStaRpt = (PPHY_STATUS_RPT_8192CD_T)pPhyStatus;	

	isCCKrate = ((pPktinfo->Rate >= DESC92C_RATE1M ) && (pPktinfo->Rate <= DESC92C_RATE11M ))?TRUE :FALSE;
	
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_A] = -1;
	pPhyInfo->RxMIMOSignalQuality[ODM_RF_PATH_B] = -1;


	if(isCCKrate)
	{
		u1Byte report;
		u1Byte cck_agc_rpt;
		
		pDM_Odm->PhyDbgInfo.NumQryPhyStatusCCK++;
		// 
		// (1)Hardware does not provide RSSI for CCK
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//

		//if(pHalData->eRFPowerState == eRfOn)
			cck_highpwr = pDM_Odm->bCckHighPower;
		//else
		//	cck_highpwr = FALSE;

		cck_agc_rpt =  pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a ;
		
		//2011.11.28 LukeLee: 88E use different LNA & VGA gain table
		//The RSSI formula should be modified according to the gain table
		//In 88E, cck_highpwr is always set to 1
		if(pDM_Odm->SupportICType & (ODM_RTL8188E|ODM_RTL8812))
		{
			LNA_idx = ((cck_agc_rpt & 0xE0) >>5);
			VGA_idx = (cck_agc_rpt & 0x1F); 
			switch(LNA_idx)
			{
				case 7:
					if(VGA_idx <= 27)
						rx_pwr_all = -100 + 2*(27-VGA_idx); //VGA_idx = 27~2
					else
						rx_pwr_all = -100;
					break;
				case 6:
						rx_pwr_all = -48 + 2*(2-VGA_idx); //VGA_idx = 2~0
					break;
				case 5:
						rx_pwr_all = -42 + 2*(7-VGA_idx); //VGA_idx = 7~5
					break;
				case 4:
						rx_pwr_all = -36 + 2*(7-VGA_idx); //VGA_idx = 7~4
					break;
				case 3:
						//rx_pwr_all = -28 + 2*(7-VGA_idx); //VGA_idx = 7~0
						rx_pwr_all = -24 + 2*(7-VGA_idx); //VGA_idx = 7~0
					break;
				case 2:
					if(cck_highpwr)
						rx_pwr_all = -12 + 2*(5-VGA_idx); //VGA_idx = 5~0
					else
						rx_pwr_all = -6+ 2*(5-VGA_idx);
					break;
				case 1:
						rx_pwr_all = 8-2*VGA_idx;
					break;
				case 0:
						rx_pwr_all = 14-2*VGA_idx;
					break;
				default:
					//DbgPrint("CCK Exception default\n");
					break;
			}
			rx_pwr_all += 6;
			PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);
			if(cck_highpwr == FALSE)
			{
				if(PWDB_ALL >= 80)
					PWDB_ALL = ((PWDB_ALL-80)<<1)+((PWDB_ALL-80)>>1)+80;
				else if((PWDB_ALL <= 78) && (PWDB_ALL >= 20))
					PWDB_ALL += 3;
				if(PWDB_ALL>100)
					PWDB_ALL = 100;
			}
		}
		else
		{
			if(!cck_highpwr)
			{			
				report =( cck_agc_rpt & 0xc0 )>>6;
				switch(report)
				{
					// 03312009 modified by cosa
					// Modify the RF RNA gain value to -40, -20, -2, 14 by Jenyu's suggestion
					// Note: different RF with the different RNA gain.
					case 0x3:
						rx_pwr_all = -46 - (cck_agc_rpt & 0x3e);
						break;
					case 0x2:
						rx_pwr_all = -26 - (cck_agc_rpt & 0x3e);
						break;
					case 0x1:
						rx_pwr_all = -12 - (cck_agc_rpt & 0x3e);
						break;
					case 0x0:
						rx_pwr_all = 16 - (cck_agc_rpt & 0x3e);
						break;
				}
			}
			else
			{
				//report = pDrvInfo->cfosho[0] & 0x60;			
				//report = pPhyStaRpt->cck_agc_rpt_ofdm_cfosho_a& 0x60;
				
				report = (cck_agc_rpt & 0x60)>>5;
				switch(report)
				{
					case 0x3:
						rx_pwr_all = -46 - ((cck_agc_rpt & 0x1f)<<1) ;
						break;
					case 0x2:
						rx_pwr_all = -26 - ((cck_agc_rpt & 0x1f)<<1);
						break;
					case 0x1:
						rx_pwr_all = -12 - ((cck_agc_rpt & 0x1f)<<1) ;
						break;
					case 0x0:
						rx_pwr_all = 16 - ((cck_agc_rpt & 0x1f)<<1) ;
						break;
				}
			}

			PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);

			//Modification for ext-LNA board
			if(pDM_Odm->BoardType & (ODM_BOARD_EXT_LNA | ODM_BOARD_EXT_PA))
			{
				if((cck_agc_rpt>>7) == 0){
					PWDB_ALL = (PWDB_ALL>94)?100:(PWDB_ALL +6);
				}
				else	
	                   {
					if(PWDB_ALL > 38)
						PWDB_ALL -= 16;
					else
						PWDB_ALL = (PWDB_ALL<=16)?(PWDB_ALL>>2):(PWDB_ALL -12);
				}             

				//CCK modification
				if(PWDB_ALL > 25 && PWDB_ALL <= 60)
					PWDB_ALL += 6;
				//else if (PWDB_ALL <= 25)
				//	PWDB_ALL += 8;
			}
			else//Modification for int-LNA board
			{
				if(PWDB_ALL > 99)
				  	PWDB_ALL -= 8;
				else if(PWDB_ALL > 50 && PWDB_ALL <= 68)
					PWDB_ALL += 4;
			}
		}
	
		pPhyInfo->RxPWDBAll = PWDB_ALL;
#if (DM_ODM_SUPPORT_TYPE &  (ODM_MP|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
#endif		
		//
		// (3) Get Signal Quality (EVM)
		//
		if(pPktinfo->bPacketMatchBSSID)
		{
			u1Byte	SQ,SQ_rpt;			
			
			if((pDM_Odm->SupportPlatform == ODM_MP) &&(pDM_Odm->PatchID==19)){//pMgntInfo->CustomerID == RT_CID_819x_Lenovo
				SQ = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm,isCCKrate,PWDB_ALL,0,0);
			}
			else if(pPhyInfo->RxPWDBAll > 40 && !pDM_Odm->bInHctTest){
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
	}
	else //is OFDM rate
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
			
		#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))
			pPhyInfo->RxPwr[i] = rx_pwr[i];
		#endif	

			/* Translate DBM to percentage. */
			RSSI = odm_QueryRxPwrPercentage(rx_pwr[i]);	
			total_rssi += RSSI;
			//RTPRINT(FRX, RX_PHY_SS, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI));

			//Modification for ext-LNA board
			if(pDM_Odm->BoardType & (ODM_BOARD_EXT_LNA | ODM_BOARD_EXT_PA))
			{
				if((pPhyStaRpt->path_agc[i].trsw) == 1)
					RSSI = (RSSI>94)?100:(RSSI +6);
				else
					RSSI = (RSSI<=16)?(RSSI>>3):(RSSI -16);

				if((RSSI <= 34) && (RSSI >=4))
					RSSI -= 4;
			}		
		
			pPhyInfo->RxMIMOSignalStrength[i] =(u1Byte) RSSI;

		#if (DM_ODM_SUPPORT_TYPE &  (/*ODM_MP|*/ODM_CE|ODM_AP|ODM_ADSL))
			//Get Rx snr value in DB		
			pPhyInfo->RxSNR[i] = pDM_Odm->PhyDbgInfo.RxSNRdB[i] = (s4Byte)(pPhyStaRpt->path_rxsnr[i]/2);
		#endif
		
			/* Record Signal Strength for next packet */
			if(pPktinfo->bPacketMatchBSSID)
			{				
				if((pDM_Odm->SupportPlatform == ODM_MP) &&(pDM_Odm->PatchID==19))
				{
					if(i==ODM_RF_PATH_A)
						pPhyInfo->SignalQuality = odm_SQ_process_patch_RT_CID_819x_Lenovo(pDM_Odm,isCCKrate,PWDB_ALL,i,RSSI);
				
				}		
				
			}
		}
		
		
		//
		// (2)PWDB, Average PWDB cacluated by hardware (for rate adaptive)
		//
		rx_pwr_all = (((pPhyStaRpt->cck_sig_qual_ofdm_pwdb_all) >> 1 )& 0x7f) -110;		
		//RTPRINT(FRX, RX_PHY_SS, ("PWDB_ALL=%d\n",	PWDB_ALL));		

		PWDB_ALL_BT = PWDB_ALL = odm_QueryRxPwrPercentage(rx_pwr_all);	
		//RTPRINT(FRX, RX_PHY_SS, ("PWDB_ALL=%d\n",PWDB_ALL));		
	
		pPhyInfo->RxPWDBAll = PWDB_ALL;
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("ODM OFDM RSSI=%d\n",pPhyInfo->RxPWDBAll));
	#if (DM_ODM_SUPPORT_TYPE &  (ODM_MP|ODM_CE))
		pPhyInfo->BTRxRSSIPercentage = PWDB_ALL_BT;
		pPhyInfo->RxPower = rx_pwr_all;
		pPhyInfo->RecvSignalPower = rx_pwr_all;
	#endif
		
		if((pDM_Odm->SupportPlatform == ODM_MP) &&(pDM_Odm->PatchID==19)){
			//do nothing	
		}
		else{//pMgntInfo->CustomerID != RT_CID_819x_Lenovo
			//
			// (3)EVM of HT rate
			//
			if(pPktinfo->Rate >=DESC92C_RATEMCS8 && pPktinfo->Rate <=DESC92C_RATEMCS15)
				Max_spatial_stream = 2; //both spatial stream make sense
			else
				Max_spatial_stream = 1; //only spatial stream 1 makes sense

			for(i=0; i<Max_spatial_stream; i++)
			{
				// Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment
				// fill most significant bit to "zero" when doing shifting operation which may change a negative 
				// value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.			
				EVM = odm_EVMdbToPercentage( (pPhyStaRpt->stream_rxevm[i] ));	//dbm

				//RTPRINT(FRX, RX_PHY_SQ, ("RXRATE=%x RXEVM=%x EVM=%s%d\n", 
				//GET_RX_STATUS_DESC_RX_MCS(pDesc), pDrvInfo->rxevm[i], "%", EVM));
				
				if(pPktinfo->bPacketMatchBSSID)
				{
					if(i==ODM_RF_PATH_A) // Fill value in RFD, Get the first spatial stream only
					{						
						pPhyInfo->SignalQuality = (u1Byte)(EVM & 0xff);
					}					
					pPhyInfo->RxMIMOSignalQuality[i] = (u1Byte)(EVM & 0xff);
				}
			}
		}

	}
#if (DM_ODM_SUPPORT_TYPE &  (ODM_MP|ODM_CE))
	//UI BSS List signal strength(in percentage), make it good looking, from 0~100.
	//It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().
	if(isCCKrate)
	{		
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
		// 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/	
		pPhyInfo->SignalStrength = (u1Byte)(SignalScaleMapping(pDM_Odm->Adapter, PWDB_ALL));//PWDB_ALL;
#else
		pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, PWDB_ALL));//PWDB_ALL;
#endif
	}
	else
	{	
		if (rf_rx_num != 0)
		{			
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
			// 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/	
			pPhyInfo->SignalStrength = (u1Byte)(SignalScaleMapping(pDM_Odm->Adapter, total_rssi/=rf_rx_num));//PWDB_ALL;
#else
			pPhyInfo->SignalStrength = (u1Byte)(odm_SignalScaleMapping(pDM_Odm, total_rssi/=rf_rx_num));
#endif
		}
	}
#endif

	//For 92C/92D HW (Hybrid) Antenna Diversity
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))	
	pDM_SWAT_Table->antsel = pPhyStaRpt->ant_sel;
	//For 88E HW Antenna Diversity
	pDM_Odm->DM_FatTable.antsel_rx_keep_0 = pPhyStaRpt->ant_sel;
	pDM_Odm->DM_FatTable.antsel_rx_keep_1 = pPhyStaRpt->ant_sel_b;
	pDM_Odm->DM_FatTable.antsel_rx_keep_2 = pPhyStaRpt->antsel_rx_keep_2;
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
	IN		PODM_PHY_INFO_T			pPhyInfo,
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	
	s4Byte			UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK, UndecoratedSmoothedOFDM, RSSI_Ave;
	u1Byte			isCCKrate=0;	
	u1Byte			RSSI_max, RSSI_min, i;
	u4Byte			OFDM_pkt=0; 
	u4Byte			Weighting=0;
	
	PSTA_INFO_T   	pEntry;
	
	if(pPktinfo->StationID == 0xFF)
		return;

	// 2011/11/17 MH Need to debug 
	//if (pDM_Odm->SupportPlatform == ODM_MP)
	{
		
	}
	
	pEntry = pDM_Odm->pODM_StaInfo[pPktinfo->StationID];			
	if(!IS_STA_VALID(pEntry) ){		
		return;
	}
	if((!pPktinfo->bPacketMatchBSSID) )
	{
		return;
	}

	isCCKrate = ((pPktinfo->Rate >= DESC92C_RATE1M ) && (pPktinfo->Rate <= DESC92C_RATE11M ))?TRUE :FALSE;
       	if(pPktinfo->bPacketBeacon)
	    pDM_Odm->PhyDbgInfo.NumQryBeaconPkt++;
	
	pDM_Odm->RxRate = pPktinfo->Rate;
#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if ((RTL8192C_SUPPORT == 1) ||(RTL8192D_SUPPORT == 1))
	if(pDM_Odm->SupportICType & ODM_RTL8192C|ODM_RTL8192D)
	{
			if(pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon)
			{
				//if(pPktinfo->bPacketBeacon)
				//{
				//	DbgPrint("This is beacon, isCCKrate=%d\n", isCCKrate);
				//}
				ODM_AntselStatistics_88C(pDM_Odm, pPktinfo->StationID,  pPhyInfo->RxPWDBAll, isCCKrate);
			}
	}
#endif
	//-----------------Smart Antenna Debug Message------------------//
#if (RTL8188E_SUPPORT == 1)
	if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		u1Byte	antsel_tr_mux;
		pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;

		if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
		{
			if(pDM_FatTable->FAT_State == FAT_TRAINING_STATE)
			{
				if(pPktinfo->bPacketToSelf)	//(pPktinfo->bPacketMatchBSSID && (!pPktinfo->bPacketBeacon))
				{
					antsel_tr_mux = (pDM_FatTable->antsel_rx_keep_2<<2) |(pDM_FatTable->antsel_rx_keep_1 <<1) |pDM_FatTable->antsel_rx_keep_0;
					pDM_FatTable->antSumRSSI[antsel_tr_mux] += pPhyInfo->RxPWDBAll;
					pDM_FatTable->antRSSIcnt[antsel_tr_mux]++;
						//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("isCCKrate=%d, PWDB_ALL=%d\n",isCCKrate, pPhyInfo->RxPWDBAll));
						//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=3'b%d%d%d\n",
							//pDM_FatTable->antsel_rx_keep_2, pDM_FatTable->antsel_rx_keep_1, pDM_FatTable->antsel_rx_keep_0));
					
				}
			}
		}
		else if((pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)||(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV))
		{
			if(pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon)
			{
				antsel_tr_mux = (pDM_FatTable->antsel_rx_keep_2<<2) |(pDM_FatTable->antsel_rx_keep_1 <<1) |pDM_FatTable->antsel_rx_keep_0;
				//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=3'b%d%d%d\n",
				//			pDM_FatTable->antsel_rx_keep_2, pDM_FatTable->antsel_rx_keep_1, pDM_FatTable->antsel_rx_keep_0));

				ODM_AntselStatistics_88E(pDM_Odm, antsel_tr_mux, pPktinfo->StationID, pPhyInfo->RxPWDBAll);
			}
		}
		
	}
#endif
#endif //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
	//-----------------Smart Antenna Debug Message------------------//
	
	UndecoratedSmoothedCCK =  pEntry->rssi_stat.UndecoratedSmoothedCCK;
	UndecoratedSmoothedOFDM = pEntry->rssi_stat.UndecoratedSmoothedOFDM;
	UndecoratedSmoothedPWDB = pEntry->rssi_stat.UndecoratedSmoothedPWDB;	
	
	if(pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon)
	{

		if(!isCCKrate)//ofdm rate
		{
			if(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B] == 0){
				RSSI_Ave = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_A = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_B = 0;
			}
			else
			{
				//DbgPrint("pRfd->Status.RxMIMOSignalStrength[0] = %d, pRfd->Status.RxMIMOSignalStrength[1] = %d \n", 
					//pRfd->Status.RxMIMOSignalStrength[0], pRfd->Status.RxMIMOSignalStrength[1]);
				pDM_Odm->RSSI_A =  pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
				pDM_Odm->RSSI_B = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
			
				if(pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A] > pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B])
				{
					RSSI_max = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
					RSSI_min = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
				}
				else
				{
					RSSI_max = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_B];
					RSSI_min = pPhyInfo->RxMIMOSignalStrength[ODM_RF_PATH_A];
				}
				if((RSSI_max -RSSI_min) < 3)
					RSSI_Ave = RSSI_max;
				else if((RSSI_max -RSSI_min) < 6)
					RSSI_Ave = RSSI_max - 1;
				else if((RSSI_max -RSSI_min) < 10)
					RSSI_Ave = RSSI_max - 2;
				else
					RSSI_Ave = RSSI_max - 3;
			}
					
			//1 Process OFDM RSSI
			if(UndecoratedSmoothedOFDM <= 0)	// initialize
			{
				UndecoratedSmoothedOFDM = pPhyInfo->RxPWDBAll;
			}
			else
			{
				if(pPhyInfo->RxPWDBAll > (u4Byte)UndecoratedSmoothedOFDM)
				{
					UndecoratedSmoothedOFDM = 	
							( ((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) + 
							(RSSI_Ave)) /(Rx_Smooth_Factor);
					UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM + 1;
				}
				else
				{
					UndecoratedSmoothedOFDM = 	
							( ((UndecoratedSmoothedOFDM)*(Rx_Smooth_Factor-1)) + 
							(RSSI_Ave)) /(Rx_Smooth_Factor);
				}
			}				
			
			pEntry->rssi_stat.PacketMap = (pEntry->rssi_stat.PacketMap<<1) | BIT0;			
										
		}
		else
		{
			RSSI_Ave = pPhyInfo->RxPWDBAll;
			pDM_Odm->RSSI_A = (u1Byte) pPhyInfo->RxPWDBAll;
			pDM_Odm->RSSI_B = 0xFF;

			//1 Process CCK RSSI
			if(UndecoratedSmoothedCCK <= 0)	// initialize
			{
				UndecoratedSmoothedCCK = pPhyInfo->RxPWDBAll;
			}
			else
			{
				if(pPhyInfo->RxPWDBAll > (u4Byte)UndecoratedSmoothedCCK)
				{
					UndecoratedSmoothedCCK = 	
							( ((UndecoratedSmoothedCCK)*(Rx_Smooth_Factor-1)) + 
							(pPhyInfo->RxPWDBAll)) /(Rx_Smooth_Factor);
					UndecoratedSmoothedCCK = UndecoratedSmoothedCCK + 1;
				}
				else
				{
					UndecoratedSmoothedCCK = 	
							( ((UndecoratedSmoothedCCK)*(Rx_Smooth_Factor-1)) + 
							(pPhyInfo->RxPWDBAll)) /(Rx_Smooth_Factor);
				}
			}
			pEntry->rssi_stat.PacketMap = pEntry->rssi_stat.PacketMap<<1;			
		}

		//if(pEntry)
		{
			//2011.07.28 LukeLee: modified to prevent unstable CCK RSSI
			if(pEntry->rssi_stat.ValidBit >= 64)
				pEntry->rssi_stat.ValidBit = 64;
			else
				pEntry->rssi_stat.ValidBit++;
			
			for(i=0; i<pEntry->rssi_stat.ValidBit; i++)
				OFDM_pkt += (u1Byte)(pEntry->rssi_stat.PacketMap>>i)&BIT0;
			
			if(pEntry->rssi_stat.ValidBit == 64)
			{
				Weighting = ((OFDM_pkt<<4) > 64)?64:(OFDM_pkt<<4);
				UndecoratedSmoothedPWDB = (Weighting*UndecoratedSmoothedOFDM+(64-Weighting)*UndecoratedSmoothedCCK)>>6;
			}
			else
			{
				if(pEntry->rssi_stat.ValidBit != 0)
					UndecoratedSmoothedPWDB = (OFDM_pkt*UndecoratedSmoothedOFDM+(pEntry->rssi_stat.ValidBit-OFDM_pkt)*UndecoratedSmoothedCCK)/pEntry->rssi_stat.ValidBit;
				else
					UndecoratedSmoothedPWDB = 0;
			}

			pEntry->rssi_stat.UndecoratedSmoothedCCK = UndecoratedSmoothedCCK;
			pEntry->rssi_stat.UndecoratedSmoothedOFDM = UndecoratedSmoothedOFDM;
			pEntry->rssi_stat.UndecoratedSmoothedPWDB = UndecoratedSmoothedPWDB;

			//DbgPrint("OFDM_pkt=%d, Weighting=%d\n", OFDM_pkt, Weighting);
			//DbgPrint("UndecoratedSmoothedOFDM=%d, UndecoratedSmoothedPWDB=%d, UndecoratedSmoothedCCK=%d\n", 
			//	UndecoratedSmoothedOFDM, UndecoratedSmoothedPWDB, UndecoratedSmoothedCCK);
			
		}
	
	}
}

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
	
	odm_RxPhyStatus92CSeries_Parsing(
							pDM_Odm,
							pPhyInfo,
							pPhyStatus,
							pPktinfo);

	if( pDM_Odm->RSSI_test == TRUE)
	{
		// Select the packets to do RSSI checking for antenna switching.
		if(pPktinfo->bPacketToSelf || pPktinfo->bPacketBeacon )
		{
				/*
			#if 0//(DM_ODM_SUPPORT_TYPE == ODM_MP)
			dm_SWAW_RSSI_Check(
				Adapter, 
				(tmppAdapter!=NULL)?(tmppAdapter==Adapter):TRUE,
				bPacketMatchBSSID,
				pEntry,
				pRfd);
			#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
			// Select the packets to do RSSI checking for antenna switching.
			//odm_SwAntDivRSSICheck8192C(padapter, precvframe->u.hdr.attrib.RxPWDBAll);
			#endif
				*/
				ODM_SwAntDivChkPerPktRssi(pDM_Odm,pPktinfo->StationID,pPhyInfo);
		}	
	}
	else
	{
		odm_Process_RSSIForDM(pDM_Odm,pPhyInfo,pPktinfo);
	}
	
}



//
// Endianness before calling this API
//
VOID
ODM_PhyStatusQuery_JaguarSeries(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T			pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
	
	
}

VOID
ODM_PhyStatusQuery(
	IN OUT	PDM_ODM_T					pDM_Odm,
	OUT		PODM_PHY_INFO_T				pPhyInfo,
	IN 		pu1Byte						pPhyStatus,	
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
#if 0  // How to jaguar jugar series??
	if(pDM_Odm->SupportICType >= ODM_RTL8195 )
	{
		ODM_PhyStatusQuery_JaguarSeries(pDM_Odm,pPhyInfo,pPhyStatus,pPktinfo);
	}
	else
#endif
	{
		ODM_PhyStatusQuery_92CSeries(pDM_Odm,pPhyInfo,pPhyStatus,pPktinfo);
	}
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

#if (DM_ODM_SUPPORT_TYPE &  (ODM_MP|ODM_CE|ODM_AP))

HAL_STATUS
ODM_ConfigRFWithHeaderFile(
	IN 	PDM_ODM_T	        	pDM_Odm,
	IN 	ODM_RF_RADIO_PATH_E 	Content,
	IN 	ODM_RF_RADIO_PATH_E 	eRFPath
    )
{
	//RT_STATUS	rtStatus = RT_STATUS_SUCCESS;


    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===>ODM_ConfigRFWithHeaderFile\n"));
#if (RTL8723A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		if(eRFPath == ODM_RF_PATH_A)
			READ_AND_CONFIG_MP(8723A,_RadioA_1T_);
	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_A:Rtl8723RadioA_1TArray\n"));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_B:Rtl8723RadioB_1TArray\n"));
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("ODM_ConfigRFWithHeaderFile: Radio No %x\n", eRFPath));
	//rtStatus = RT_STATUS_SUCCESS;
#endif
#if (RTL8188E_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		if(IS_VENDOR_8188E_I_CUT_SERIES(pDM_Odm->Adapter))
			READ_AND_CONFIG(8188E,_RadioA_1T_ICUT_);
		else
			READ_AND_CONFIG(8188E,_RadioA_1T_);
		
	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_A:Rtl8188ERadioA_1TArray\n"));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> ODM_ConfigRFWithHeaderFile() Radio_B:Rtl8188ERadioB_1TArray\n"));
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_TRACE, ("ODM_ConfigRFWithHeaderFile: Radio No %x\n", eRFPath));
	//rtStatus = RT_STATUS_SUCCESS;
#endif
	return HAL_STATUS_SUCCESS;
}


HAL_STATUS
ODM_ConfigBBWithHeaderFile(
	IN 	PDM_ODM_T	             	pDM_Odm,
	IN 	ODM_BB_Config_Type 		ConfigType
	)
{
#if (RTL8723A_SUPPORT == 1) 
    if(pDM_Odm->SupportICType == ODM_RTL8723A)
	{

		if(ConfigType == CONFIG_BB_PHY_REG)
		{
			READ_AND_CONFIG_MP(8723A,_PHY_REG_1T_);
		}
		else if(ConfigType == CONFIG_BB_AGC_TAB)
		{
			READ_AND_CONFIG_MP(8723A,_AGC_TAB_1T_);
		}
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() phy:Rtl8723AGCTAB_1TArray\n"));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8723PHY_REG_1TArray\n"));
	}		
#endif

#if (RTL8188E_SUPPORT == 1)
    if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{

		if(ConfigType == CONFIG_BB_PHY_REG)
		{
			if(IS_VENDOR_8188E_I_CUT_SERIES(pDM_Odm->Adapter))
				READ_AND_CONFIG(8188E,_PHY_REG_1T_ICUT_);
			else
				READ_AND_CONFIG(8188E,_PHY_REG_1T_);
		}
		else if(ConfigType == CONFIG_BB_AGC_TAB)
		{
			if(IS_VENDOR_8188E_I_CUT_SERIES(pDM_Odm->Adapter))
				READ_AND_CONFIG(8188E,_AGC_TAB_1T_ICUT_);
			else
				READ_AND_CONFIG(8188E,_AGC_TAB_1T_);
		}
		else if(ConfigType == CONFIG_BB_PHY_REG_PG)
		{
			READ_AND_CONFIG(8188E,_PHY_REG_PG_);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_INIT, ODM_DBG_LOUD, (" ===> phy_ConfigBBWithHeaderFile() agc:Rtl8188EPHY_REG_PGArray\n"));
		}
	}
#endif

	return HAL_STATUS_SUCCESS; 
}                 

HAL_STATUS
ODM_ConfigMACWithHeaderFile(
	IN 	PDM_ODM_T	pDM_Odm
	)
{
	u1Byte result = HAL_STATUS_SUCCESS;
#if (RTL8723A_SUPPORT == 1)
	if (pDM_Odm->SupportICType == ODM_RTL8723A)
	{
		READ_AND_CONFIG_MP(8723A,_MAC_REG_);
	}
#endif
#if (RTL8188E_SUPPORT == 1)  
	if (pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		if(IS_VENDOR_8188E_I_CUT_SERIES(pDM_Odm->Adapter))
			READ_AND_CONFIG(8188E,_MAC_REG_ICUT_);
		else			
			result =READ_AND_CONFIG(8188E,_MAC_REG_);
	}
#endif
	 
	return result;    
} 


#endif // end of (#if DM_ODM_SUPPORT_TYPE)

