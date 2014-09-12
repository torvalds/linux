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

#include "../odm_precomp.h"

#if (RTL8188E_SUPPORT == 1)

VOID
ODM_DIG_LowerBound_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pDIG_T		pDM_DigTable = &pDM_Odm->DM_DigTable;

	if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
	{
		pDM_DigTable->rx_gain_range_min = (u1Byte) pDM_DigTable->AntDiv_RSSI_max;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_DIG_LowerBound_88E(): pDM_DigTable->AntDiv_RSSI_max=%d \n",pDM_DigTable->AntDiv_RSSI_max));
	}
	//If only one Entry connected
}


//3============================================================
//3 Dynamic Primary CCA
//3============================================================

VOID
odm_PrimaryCCA_Init(
	IN		PDM_ODM_T		pDM_Odm)
{
	pPri_CCA_T		PrimaryCCA = &(pDM_Odm->DM_PriCCA);  
	PrimaryCCA->DupRTS_flag = 0;
	PrimaryCCA->intf_flag = 0;
	PrimaryCCA->intf_type = 0;
	PrimaryCCA->Monitor_flag = 0;
	PrimaryCCA->PriCCA_flag = 0;
}

BOOLEAN
ODM_DynamicPrimaryCCA_DupRTS(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	pPri_CCA_T		PrimaryCCA = &(pDM_Odm->DM_PriCCA);  
	
	return	PrimaryCCA->DupRTS_flag;
}

VOID
odm_DynamicPrimaryCCA(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	PADAPTER	Adapter =  pDM_Odm->Adapter;	// for NIC

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))	
	PRT_WLAN_STA	pEntry;
#endif	

	PFALSE_ALARM_STATISTICS		FalseAlmCnt = &(pDM_Odm->FalseAlmCnt);
	pPri_CCA_T		PrimaryCCA = &(pDM_Odm->DM_PriCCA);  
	
	BOOLEAN		Is40MHz;
	BOOLEAN		Client_40MHz = FALSE, Client_tmp = FALSE;      // connected client BW  
	BOOLEAN		bConnected = FALSE;		// connected or not
	static u1Byte	Client_40MHz_pre = 0;
	static u8Byte	lastTxOkCnt = 0;
	static u8Byte	lastRxOkCnt = 0;
	static u4Byte	Counter = 0;
	static u1Byte	Delay = 1;
	u8Byte		curTxOkCnt;
	u8Byte		curRxOkCnt;
	u1Byte		SecCHOffset;
	u1Byte		i;
	
#if((DM_ODM_SUPPORT_TYPE==ODM_ADSL) ||( DM_ODM_SUPPORT_TYPE==ODM_CE))
	return;
#endif

	if(pDM_Odm->SupportICType != ODM_RTL8188E) 
		return;

	Is40MHz = *(pDM_Odm->pBandWidth);
	SecCHOffset = *(pDM_Odm->pSecChOffset);
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Second CH Offset = %d\n", SecCHOffset));
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if(Is40MHz==1)
		SecCHOffset = SecCHOffset%2+1;     // NIC's definition is reverse to AP   1:secondary below,  2: secondary above
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Second CH Offset = %d\n", SecCHOffset));
	//3 Check Current WLAN Traffic
	curTxOkCnt = Adapter->TxStats.NumTxBytesUnicast - lastTxOkCnt;
	curRxOkCnt = Adapter->RxStats.NumRxBytesUnicast - lastRxOkCnt;
	lastTxOkCnt = Adapter->TxStats.NumTxBytesUnicast;
	lastRxOkCnt = Adapter->RxStats.NumRxBytesUnicast;	
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	//3 Check Current WLAN Traffic
	curTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast)-lastTxOkCnt;
	curRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast)-lastRxOkCnt;
	lastTxOkCnt = *(pDM_Odm->pNumTxBytesUnicast);
	lastRxOkCnt = *(pDM_Odm->pNumRxBytesUnicast);
#endif

	//==================Debug Message====================
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("TP = %llu\n", curTxOkCnt+curRxOkCnt));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Is40MHz = %d\n", Is40MHz));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("BW_LSC = %d\n", FalseAlmCnt->Cnt_BW_LSC));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("BW_USC = %d\n", FalseAlmCnt->Cnt_BW_USC));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCA OFDM = %d\n", FalseAlmCnt->Cnt_OFDM_CCA));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCA CCK = %d\n", FalseAlmCnt->Cnt_CCK_CCA));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("OFDM FA = %d\n", FalseAlmCnt->Cnt_Ofdm_fail));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("CCK FA = %d\n", FalseAlmCnt->Cnt_Cck_fail));
	//================================================
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (ACTING_AS_AP(Adapter))   // primary cca process only do at AP mode
#endif
	{
		
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("ACTING as AP mode=%d\n", ACTING_AS_AP(Adapter)));
		//3 To get entry's connection and BW infomation status. 
		for(i=0;i<ASSOCIATE_ENTRY_NUM;i++)
		{
			if(IsAPModeExist(Adapter)&&GetFirstExtAdapter(Adapter)!=NULL)
				pEntry=AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
			else
				pEntry=AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);
			if(pEntry!=NULL)
			{
				Client_tmp = pEntry->BandWidth;   // client BW
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Client_BW=%d\n", Client_tmp));
				if(Client_tmp>Client_40MHz)
					Client_40MHz = Client_tmp;     // 40M/20M coexist => 40M priority is High
				
				if(pEntry->bAssociated)
				{
					bConnected=TRUE;    // client is connected or not
					break;
				}
			}
			else
			{
				break;
			}
		}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
		//3 To get entry's connection and BW infomation status. 

		PSTA_INFO_T pstat;

		for(i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			pstat = pDM_Odm->pODM_StaInfo[i];
			if(IS_STA_VALID(pstat) )
			{			
				Client_tmp = pstat->tx_bw;  
				if(Client_tmp>Client_40MHz)
					Client_40MHz = Client_tmp;     // 40M/20M coexist => 40M priority is High
				
				bConnected = TRUE;
			}
		}
#endif
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("bConnected=%d\n", bConnected));
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Is Client 40MHz=%d\n", Client_40MHz));
		//1 Monitor whether the interference exists or not 
		if(PrimaryCCA->Monitor_flag == 1)
		{
			if(SecCHOffset == 1)       // secondary channel is below the primary channel
			{
				if((FalseAlmCnt->Cnt_OFDM_CCA > 500)&&(FalseAlmCnt->Cnt_BW_LSC > FalseAlmCnt->Cnt_BW_USC+500))
				{
					if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
					{
						PrimaryCCA->intf_type = 1;
						PrimaryCCA->PriCCA_flag = 1;
						ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 2);   // USC MF
						if(PrimaryCCA->DupRTS_flag == 1)
							PrimaryCCA->DupRTS_flag = 0;
					}
					else
					{
						PrimaryCCA->intf_type = 2;
						if(PrimaryCCA->DupRTS_flag == 0)
							PrimaryCCA->DupRTS_flag = 1;
					}
				
				}
				else   // interferecne disappear
				{
					PrimaryCCA->DupRTS_flag = 0;
					PrimaryCCA->intf_flag = 0;
					PrimaryCCA->intf_type = 0;
				}
			}
			else if(SecCHOffset == 2)  // secondary channel is above the primary channel
			{
				if((FalseAlmCnt->Cnt_OFDM_CCA > 500)&&(FalseAlmCnt->Cnt_BW_USC > FalseAlmCnt->Cnt_BW_LSC+500))
				{
					if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
					{
						PrimaryCCA->intf_type = 1;
						PrimaryCCA->PriCCA_flag = 1;
						ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 1);  // LSC MF
						if(PrimaryCCA->DupRTS_flag == 1)
							PrimaryCCA->DupRTS_flag = 0;
					}
					else
					{
						PrimaryCCA->intf_type = 2;
						if(PrimaryCCA->DupRTS_flag == 0)
							PrimaryCCA->DupRTS_flag = 1;
					}
				
				}
				else   // interferecne disappear
				{
					PrimaryCCA->DupRTS_flag = 0;
					PrimaryCCA->intf_flag = 0;
					PrimaryCCA->intf_type = 0;
				}


			}
			PrimaryCCA->Monitor_flag = 0;
		}

		//1 Dynamic Primary CCA Main Function
		if(PrimaryCCA->Monitor_flag == 0)
		{
			if(Is40MHz)    		// if RFBW==40M mode which require to process primary cca
			{
				//2 STA is NOT Connected
				if(!bConnected)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("STA NOT Connected!!!!\n"));
			
					if(PrimaryCCA->PriCCA_flag == 1)		// reset primary cca when STA is disconnected
					{
						PrimaryCCA->PriCCA_flag = 0;
						ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 0);
					}
					if(PrimaryCCA->DupRTS_flag == 1)		// reset Duplicate RTS when STA is disconnected
						PrimaryCCA->DupRTS_flag = 0;

					if(SecCHOffset == 1)   // secondary channel is below the primary channel
					{
						if((FalseAlmCnt->Cnt_OFDM_CCA > 800)&&(FalseAlmCnt->Cnt_BW_LSC*5 > FalseAlmCnt->Cnt_BW_USC*9))
						{
							PrimaryCCA->intf_flag = 1;    // secondary channel interference is detected!!!
							if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
								PrimaryCCA->intf_type = 1;    	// interference is shift
							else
								PrimaryCCA->intf_type = 2;    	// interference is in-band
						}
						else    
						{
							PrimaryCCA->intf_flag = 0;
							PrimaryCCA->intf_type = 0;  
						}
					}
					else if(SecCHOffset == 2)    // secondary channel is above the primary channel
					{
						if((FalseAlmCnt->Cnt_OFDM_CCA > 800)&&(FalseAlmCnt->Cnt_BW_USC*5 > FalseAlmCnt->Cnt_BW_LSC*9))
						{
							PrimaryCCA->intf_flag = 1;    // secondary channel interference is detected!!!
							if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
								PrimaryCCA->intf_type = 1;    	// interference is shift
							else
								PrimaryCCA->intf_type = 2;    	// interference is in-band
						}
						else    
						{
							PrimaryCCA->intf_flag = 0;
							PrimaryCCA->intf_type = 0;  
						}
					}
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("PrimaryCCA=%d\n",PrimaryCCA->PriCCA_flag));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Intf_Type=%d\n", PrimaryCCA->intf_type));
				}
				//2 STA is Connected
				else
				{
					if(Client_40MHz == 0)		//3 // client BW = 20MHz
					{
						if(PrimaryCCA->PriCCA_flag == 0)
						{
							PrimaryCCA->PriCCA_flag = 1;
							if(SecCHOffset==1)
								ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 2);
							else if(SecCHOffset==2)
								ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 1);
						}
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("STA Connected 20M!!! PrimaryCCA=%d\n", PrimaryCCA->PriCCA_flag));
					}
					else		//3 // client BW = 40MHz
					{
						if(PrimaryCCA->intf_flag == 1)    // interference is detected!!
						{
							if(PrimaryCCA->intf_type == 1)
							{
								if(PrimaryCCA->PriCCA_flag!=1)
								{
									PrimaryCCA->PriCCA_flag = 1;
									if(SecCHOffset==1)
										ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 2);
									else if(SecCHOffset==2)
										ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 1);
								}
							}
							else if(PrimaryCCA->intf_type == 2)
							{
								if(PrimaryCCA->DupRTS_flag!=1)
									PrimaryCCA->DupRTS_flag = 1;
							}
						}
						else   // if intf_flag==0
						{
							if((curTxOkCnt+curRxOkCnt)<10000)   //idle mode or TP traffic is very low
							{
								if(SecCHOffset == 1)
								{
									if((FalseAlmCnt->Cnt_OFDM_CCA > 800)&&(FalseAlmCnt->Cnt_BW_LSC*5 > FalseAlmCnt->Cnt_BW_USC*9))
									{
										PrimaryCCA->intf_flag = 1;
										if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)		
											PrimaryCCA->intf_type = 1;    	// interference is shift
										else
											PrimaryCCA->intf_type = 2;    	// interference is in-band
									}
								}
								else if(SecCHOffset == 2)
								{
									if((FalseAlmCnt->Cnt_OFDM_CCA > 800)&&(FalseAlmCnt->Cnt_BW_USC*5 > FalseAlmCnt->Cnt_BW_LSC*9))
									{
										PrimaryCCA->intf_flag = 1;
										if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)		
											PrimaryCCA->intf_type = 1;    	// interference is shift
										else
											PrimaryCCA->intf_type = 2;    	// interference is in-band
									}

								}
							}
							else     // TP Traffic is High
							{
								if(SecCHOffset == 1)
								{
									if(FalseAlmCnt->Cnt_BW_LSC > (FalseAlmCnt->Cnt_BW_USC+500))
									{	
										if(Delay == 0)    // add delay to avoid interference occurring abruptly, jump one time
										{
											PrimaryCCA->intf_flag = 1;
											if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
												PrimaryCCA->intf_type = 1;    	// interference is shift
											else
												PrimaryCCA->intf_type = 2;    	// interference is in-band
											Delay = 1;
										}
										else
											Delay = 0;
									}	
								}	
								else if(SecCHOffset == 2)
								{
									if(FalseAlmCnt->Cnt_BW_USC > (FalseAlmCnt->Cnt_BW_LSC+500))
									{	
										if(Delay == 0)    // add delay to avoid interference occurring abruptly
										{
											PrimaryCCA->intf_flag = 1;
											if(FalseAlmCnt->Cnt_Ofdm_fail > FalseAlmCnt->Cnt_OFDM_CCA>>1)
												PrimaryCCA->intf_type = 1;    	// interference is shift
											else
												PrimaryCCA->intf_type = 2;    	// interference is in-band
											Delay = 1;
										}
										else
											Delay = 0;
									}	
								}
							}
						}
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Primary CCA=%d\n", PrimaryCCA->PriCCA_flag));
						ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Duplicate RTS=%d\n", PrimaryCCA->DupRTS_flag));
					}

				}// end of connected
			}
		}
		//1 Dynamic Primary CCA Monitor Counter
		if((PrimaryCCA->PriCCA_flag == 1)||(PrimaryCCA->DupRTS_flag == 1))
		{
			if(Client_40MHz == 0)     // client=20M no need to monitor primary cca flag  
			{
				Client_40MHz_pre = Client_40MHz;
				return;
			}
			Counter++;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_DYNAMIC_PRICCA, ODM_DBG_LOUD, ("Counter=%d\n", Counter));
			if((Counter == 30)||((Client_40MHz -Client_40MHz_pre)==1))      // Every 60 sec to monitor one time
			{
				PrimaryCCA->Monitor_flag = 1;     // monitor flag is triggered!!!!!
				if(PrimaryCCA->PriCCA_flag == 1)
				{
					PrimaryCCA->PriCCA_flag = 0;
					ODM_SetBBReg(pDM_Odm, 0xc6c, BIT8|BIT7, 0);
				}
				Counter = 0;
			}
		}
	}

	Client_40MHz_pre = Client_40MHz;
}
#else //#if (RTL8188E_SUPPORT == 1)

VOID
odm_PrimaryCCA_Init(
	IN		PDM_ODM_T		pDM_Odm)
{
}
VOID
odm_DynamicPrimaryCCA(
	IN		PDM_ODM_T		pDM_Odm
	)
{
}
BOOLEAN
ODM_DynamicPrimaryCCA_DupRTS(
	IN		PDM_ODM_T		pDM_Odm
	)
{
	return FALSE;
}
#endif //#if (RTL8188E_SUPPORT == 1)

