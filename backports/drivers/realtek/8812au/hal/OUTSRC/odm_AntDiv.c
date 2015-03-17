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

#if (defined(CONFIG_HW_ANTENNA_DIVERSITY))
VOID
odm_AntDiv_on_off( IN PDM_ODM_T	pDM_Odm ,IN u1Byte swch)
{
	if(pDM_Odm->AntDivType==S0S1_SW_ANTDIV || pDM_Odm->AntDivType==CGCS_RX_SW_ANTDIV) 
		return;

	if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) N-Series AntDiv Function\n",(swch==ANTDIV_ON)?"ON" : "OFF"));
		ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, swch); //OFDM AntDiv function block enable
		ODM_SetBBReg(pDM_Odm, 0xa00 , BIT15, swch); //CCK AntDiv function block enable
	}
	else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("(( Turn %s )) AC-Series AntDiv Function\n",(swch==ANTDIV_ON)?"ON" : "OFF"));
		if(pDM_Odm->SupportICType == ODM_RTL8812)
		{
			ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, swch); //OFDM AntDiv function block enable
			ODM_SetBBReg(pDM_Odm, 0xa00 , BIT15, swch); //CCK AntDiv function block enable
		}
		else
		{
		ODM_SetBBReg(pDM_Odm, 0x8D4 , BIT24, swch); //OFDM AntDiv function block enable
		ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, swch); //CCK AntDiv function block enable
	        }
         }
}

VOID
ODM_UpdateRxIdleAnt(IN PDM_ODM_T pDM_Odm, IN u1Byte Ant)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u4Byte	DefaultAnt, OptionalAnt,value32;

	#if (DM_ODM_SUPPORT_TYPE & (ODM_CE|ODM_WIN))
	PADAPTER 		pAdapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	#endif

	if(pDM_FatTable->RxIdleAnt != Ant)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Update Rx-Idle-Ant ] RxIdleAnt =%s\n",(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
		pDM_FatTable->RxIdleAnt = Ant;

		if(Ant == MAIN_ANT)
		{
			DefaultAnt   =  ANT1_2G; 
			OptionalAnt =  ANT2_2G; 
		}
		else
		{
			DefaultAnt  =   ANT2_2G;
			OptionalAnt =  ANT1_2G;
		}
	
		if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
		{
			if(pDM_Odm->SupportICType==ODM_RTL8192E)
			{
				ODM_SetBBReg(pDM_Odm, 0xB38 , BIT5|BIT4|BIT3, DefaultAnt); //Default RX
				ODM_SetBBReg(pDM_Odm, 0xB38 , BIT8|BIT7|BIT6, OptionalAnt);//Optional RX
			}
			else
			{
				ODM_SetBBReg(pDM_Odm, 0x864 , BIT5|BIT4|BIT3, DefaultAnt);	//Default RX
				ODM_SetBBReg(pDM_Odm, 0x864 , BIT8|BIT7|BIT6, OptionalAnt);	//Optional RX

				if(pDM_Odm->SupportICType == ODM_RTL8723B)
				{
					value32 = ODM_GetBBReg(pDM_Odm, 0x948, 0xFFF);
				
					if (value32 !=0x280)
						ODM_SetBBReg(pDM_Odm, 0x948 , BIT9, DefaultAnt);

					rtw_hal_set_tx_power_level(pAdapter, pHalData->CurrentChannel);
				}
				
			}
			ODM_SetBBReg(pDM_Odm, 0x860, BIT14|BIT13|BIT12, DefaultAnt);	        //Default TX	
		}
		else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
		{
			ODM_SetBBReg(pDM_Odm, 0xC08 , BIT21|BIT20|BIT19, DefaultAnt);	 //Default RX
			ODM_SetBBReg(pDM_Odm, 0xC08 , BIT24|BIT23|BIT22, OptionalAnt);//Optional RX
			ODM_SetBBReg(pDM_Odm, 0xC08 , BIT27|BIT26|BIT25, DefaultAnt);	 //Default TX
		}
		ODM_SetMACReg(pDM_Odm, 0x6D8 , BIT10|BIT9|BIT8, DefaultAnt);	//PathA Resp Tx
	}
	else// pDM_FatTable->RxIdleAnt == Ant
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[ Stay in Ori-Ant ]  RxIdleAnt =%s\n",(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
		pDM_FatTable->RxIdleAnt = Ant;
	}
}


VOID
odm_UpdateTxAnt(IN PDM_ODM_T pDM_Odm, IN u1Byte Ant, IN u4Byte MacId)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u1Byte	TxAnt;

	if(Ant == MAIN_ANT)
		TxAnt = ANT1_2G;
	else
		TxAnt = ANT2_2G;
	
	pDM_FatTable->antsel_a[MacId] = TxAnt&BIT0;
	pDM_FatTable->antsel_b[MacId] = (TxAnt&BIT1)>>1;
	pDM_FatTable->antsel_c[MacId] = (TxAnt&BIT2)>>2;
        #if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if (pDM_Odm->antdiv_rssi)
        {
		//panic_printk("[Tx from TxInfo]: MacID:(( %d )),  TxAnt = (( %s ))\n",MacId,(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT");
		//panic_printk("antsel_tr_mux=(( 3'b%d%d%d ))\n",	pDM_FatTable->antsel_c[MacId] , pDM_FatTable->antsel_b[MacId] , pDM_FatTable->antsel_a[MacId] );
	}
        #endif
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Tx from TxInfo]: MacID:(( %d )),  TxAnt = (( %s ))\n", 
	//					MacId,(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=(( 3'b%d%d%d ))\n",
							//pDM_FatTable->antsel_c[MacId] , pDM_FatTable->antsel_b[MacId] , pDM_FatTable->antsel_a[MacId] ));
	
}



#if (RTL8188E_SUPPORT == 1)


VOID
odm_RX_HWAntDiv_Init_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32;

	pDM_Odm->AntType = ODM_AUTO_ANT;

#if (MP_DRIVER == 1)
	        pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
	        ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 0); // disable HW AntDiv 
	        ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT31, 1);  // 1:CG, 0:CS
        	return;
#else
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  AntDivType=[CGCS_RX_HW_ANTDIV]\n"));
	
	//MAC Setting
	value32 = ODM_GetMACReg(pDM_Odm, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	ODM_SetMACReg(pDM_Odm, ODM_REG_ANTSEL_PIN_11N, bMaskDWord, value32|(BIT23|BIT25)); //Reg4C[25]=1, Reg4C[23]=1 for pin output
	//Pin Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_PIN_CTRL_11N , BIT9|BIT8, 0);//Reg870[8]=1'b0, Reg870[9]=1'b0 		//antsel antselb by HW
	ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT10, 0);	//Reg864[10]=1'b0 	//antsel2 by HW
	ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT22, 1);	//Regb2c[22]=1'b0 	//disable CS/CG switch
	ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT31, 1);	//Regb2c[31]=1'b1	//output at CG only
	//OFDM Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_ANTDIV_PARA1_11N , bMaskDWord, 0x000000a0);
	//CCK Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_BB_PWR_SAV4_11N , BIT7, 1); //Fix CCK PHY status report issue
	ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_ANTDIV_PARA2_11N , BIT4, 1); //CCK complete HW AntDiv within 64 samples	
	
	ODM_SetBBReg(pDM_Odm, ODM_REG_ANT_MAPPING1_11N , 0xFFFF, 0x0102);	//antenna mapping table

#endif
}

VOID
odm_TRX_HWAntDiv_Init_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32;
	
#if (MP_DRIVER == 1)
	        pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
	        ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 0); // disable HW AntDiv 
	        ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT5|BIT4|BIT3, 0); //Default RX   (0/1)
	        return;
#else

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  AntDivType=[CG_TRX_HW_ANTDIV (SPDT)]\n"));
	
	//MAC Setting
	value32 = ODM_GetMACReg(pDM_Odm, ODM_REG_ANTSEL_PIN_11N, bMaskDWord);
	ODM_SetMACReg(pDM_Odm, ODM_REG_ANTSEL_PIN_11N, bMaskDWord, value32|(BIT23|BIT25)); //Reg4C[25]=1, Reg4C[23]=1 for pin output
	//Pin Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_PIN_CTRL_11N , BIT9|BIT8, 0);//Reg870[8]=1'b0, Reg870[9]=1'b0 		//antsel antselb by HW
	ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT10, 0);	//Reg864[10]=1'b0 	//antsel2 by HW
	ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT22, 0);	//Regb2c[22]=1'b0 	//disable CS/CG switch
	ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT31, 1);	//Regb2c[31]=1'b1	//output at CG only
	//OFDM Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_ANTDIV_PARA1_11N , bMaskDWord, 0x000000a0);
	//CCK Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_BB_PWR_SAV4_11N , BIT7, 1); //Fix CCK PHY status report issue
	ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_ANTDIV_PARA2_11N , BIT4, 1); //CCK complete HW AntDiv within 64 samples

	//antenna mapping table
	if(!pDM_Odm->bIsMPChip) //testchip
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_RX_DEFUALT_A_11N , BIT10|BIT9|BIT8, 1);	//Reg858[10:8]=3'b001
		ODM_SetBBReg(pDM_Odm, ODM_REG_RX_DEFUALT_A_11N , BIT13|BIT12|BIT11, 2);	//Reg858[13:11]=3'b010
	}
	else //MPchip
		ODM_SetBBReg(pDM_Odm, ODM_REG_ANT_MAPPING1_11N , bMaskDWord, 0x0201);	//Reg914=3'b010, Reg915=3'b001
#endif
}

VOID
odm_Smart_HWAntDiv_Init_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32, i;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u4Byte	AntCombination = 2;

    ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  AntDivType=[CG_TRX_SMART_ANTDIV]\n"));
    
#if (MP_DRIVER == 1)
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("pDM_Odm->AntDivType: %d\n", pDM_Odm->AntDivType));
    return;
#else

	for(i=0; i<6; i++)
	{
		pDM_FatTable->Bssid[i] = 0;
		pDM_FatTable->antSumRSSI[i] = 0;
		pDM_FatTable->antRSSIcnt[i] = 0;
		pDM_FatTable->antAveRSSI[i] = 0;
	}
	pDM_FatTable->TrainIdx = 0;
	pDM_FatTable->FAT_State = FAT_NORMAL_STATE;

	//MAC Setting
	value32 = ODM_GetMACReg(pDM_Odm, 0x4c, bMaskDWord);
	ODM_SetMACReg(pDM_Odm, 0x4c, bMaskDWord, value32|(BIT23|BIT25)); //Reg4C[25]=1, Reg4C[23]=1 for pin output
	value32 = ODM_GetMACReg(pDM_Odm,  0x7B4, bMaskDWord);
	ODM_SetMACReg(pDM_Odm, 0x7b4, bMaskDWord, value32|(BIT16|BIT17)); //Reg7B4[16]=1 enable antenna training, Reg7B4[17]=1 enable A2 match
	//value32 = PlatformEFIORead4Byte(Adapter, 0x7B4);
	//PlatformEFIOWrite4Byte(Adapter, 0x7b4, value32|BIT18);	//append MACID in reponse packet

	//Match MAC ADDR
	ODM_SetMACReg(pDM_Odm, 0x7b4, 0xFFFF, 0);
	ODM_SetMACReg(pDM_Odm, 0x7b0, bMaskDWord, 0);
	
	ODM_SetBBReg(pDM_Odm, 0x870 , BIT9|BIT8, 0);//Reg870[8]=1'b0, Reg870[9]=1'b0 		//antsel antselb by HW
	ODM_SetBBReg(pDM_Odm, 0x864 , BIT10, 0);	//Reg864[10]=1'b0 	//antsel2 by HW
	ODM_SetBBReg(pDM_Odm, 0xb2c , BIT22, 0);	//Regb2c[22]=1'b0 	//disable CS/CG switch
	ODM_SetBBReg(pDM_Odm, 0xb2c , BIT31, 1);	//Regb2c[31]=1'b1	//output at CG only
	ODM_SetBBReg(pDM_Odm, 0xca4 , bMaskDWord, 0x000000a0);
	
	//antenna mapping table
	if(AntCombination == 2)
	{
		if(!pDM_Odm->bIsMPChip) //testchip
		{
			ODM_SetBBReg(pDM_Odm, 0x858 , BIT10|BIT9|BIT8, 1);	//Reg858[10:8]=3'b001
			ODM_SetBBReg(pDM_Odm, 0x858 , BIT13|BIT12|BIT11, 2);	//Reg858[13:11]=3'b010
		}
		else //MPchip
		{
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte0, 1);
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte1, 2);
		}
	}
	else if(AntCombination == 7)
	{
		if(!pDM_Odm->bIsMPChip) //testchip
		{
			ODM_SetBBReg(pDM_Odm, 0x858 , BIT10|BIT9|BIT8, 0);	//Reg858[10:8]=3'b000
			ODM_SetBBReg(pDM_Odm, 0x858 , BIT13|BIT12|BIT11, 1);	//Reg858[13:11]=3'b001
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT16, 0);
			ODM_SetBBReg(pDM_Odm, 0x858 , BIT15|BIT14, 2);	//(Reg878[0],Reg858[14:15])=3'b010
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT19|BIT18|BIT17, 3);//Reg878[3:1]=3b'011
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT22|BIT21|BIT20, 4);//Reg878[6:4]=3b'100
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT25|BIT24|BIT23, 5);//Reg878[9:7]=3b'101 
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT28|BIT27|BIT26, 6);//Reg878[12:10]=3b'110 
			ODM_SetBBReg(pDM_Odm, 0x878 , BIT31|BIT30|BIT29, 7);//Reg878[15:13]=3b'111
		}
		else //MPchip
		{
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte0, 0);
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte1, 1);	
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte2, 2);
			ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte3, 3);
			ODM_SetBBReg(pDM_Odm, 0x918 , bMaskByte0, 4);
			ODM_SetBBReg(pDM_Odm, 0x918 , bMaskByte1, 5);
			ODM_SetBBReg(pDM_Odm, 0x918 , bMaskByte2, 6);
			ODM_SetBBReg(pDM_Odm, 0x918 , bMaskByte3, 7);
		}
	}

	//Default Ant Setting when no fast training
	ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 1); //Reg80c[21]=1'b1		//from TX Info
	ODM_SetBBReg(pDM_Odm, 0x864 , BIT5|BIT4|BIT3, 0);	//Default RX
	ODM_SetBBReg(pDM_Odm, 0x864 , BIT8|BIT7|BIT6, 1);	//Optional RX
	//ODM_SetBBReg(pDM_Odm, 0x860 , BIT14|BIT13|BIT12, 1);	//Default TX

	//Enter Traing state
	ODM_SetBBReg(pDM_Odm, 0x864 , BIT2|BIT1|BIT0, (AntCombination-1));	//Reg864[2:0]=3'd6	//ant combination=reg864[2:0]+1
	//ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 0); //RegC50[7]=1'b0		//disable HW AntDiv
	//ODM_SetBBReg(pDM_Odm,  0xe08 , BIT16, 0); //RegE08[16]=1'b0		//disable fast training
	//ODM_SetBBReg(pDM_Odm, 0xe08 , BIT16, 1);	//RegE08[16]=1'b1		//enable fast training
	ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 1);	//RegC50[7]=1'b1 		//enable HW AntDiv

	//SW Control
	//PHY_SetBBReg(Adapter, 0x864 , BIT10, 1);
	//PHY_SetBBReg(Adapter, 0x870 , BIT9, 1);
	//PHY_SetBBReg(Adapter, 0x870 , BIT8, 1);
	//PHY_SetBBReg(Adapter, 0x864 , BIT11, 1);
	//PHY_SetBBReg(Adapter, 0x860 , BIT9, 0);
	//PHY_SetBBReg(Adapter, 0x860 , BIT8, 0);

#endif
}
#endif //#if (RTL8188E_SUPPORT == 1)


#if (RTL8192E_SUPPORT == 1)
VOID
odm_RX_HWAntDiv_Init_92E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	
#if (MP_DRIVER == 1)
        //pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
	odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
        ODM_SetBBReg(pDM_Odm, 0xc50 , BIT8, 0); //r_rxdiv_enable_anta  Regc50[8]=1'b0  0: control by c50[9]
        ODM_SetBBReg(pDM_Odm, 0xc50 , BIT9, 1);  // 1:CG, 0:CS
        return;
#endif

	 ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8192E AntDiv_Init =>  AntDivType=[CGCS_RX_HW_ANTDIV]\n"));
	
	 //Pin Settings
	 ODM_SetBBReg(pDM_Odm, 0x870 , BIT8, 0);//Reg870[8]=1'b0,    // "antsel" is controled by HWs
	 ODM_SetBBReg(pDM_Odm, 0xc50 , BIT8, 1); //Regc50[8]=1'b1  //" CS/CG switching" is controled by HWs

	 //Mapping table
	 ODM_SetBBReg(pDM_Odm, 0x914 , 0xFFFF, 0x0100); //antenna mapping table
	  
	 //OFDM Settings
	 ODM_SetBBReg(pDM_Odm, 0xca4 , 0x7FF, 0xA0); //thershold
	 ODM_SetBBReg(pDM_Odm, 0xca4 , 0x7FF000, 0x0); //bias
	 
	 //CCK Settings
	 ODM_SetBBReg(pDM_Odm, 0xa04 , 0xF000000, 0); //Select which path to receive for CCK_1 & CCK_2
	 ODM_SetBBReg(pDM_Odm, 0xb34 , BIT30, 1); //(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0
	 ODM_SetBBReg(pDM_Odm, 0xa74 , BIT7, 1); //Fix CCK PHY status report issue
	 ODM_SetBBReg(pDM_Odm, 0xa0c , BIT4, 1); //CCK complete HW AntDiv within 64 samples 	 
}

VOID
odm_TRX_HWAntDiv_Init_92E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	
#if (MP_DRIVER == 1)
        //pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
	odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
        ODM_SetBBReg(pDM_Odm, 0xc50 , BIT8, 0); //r_rxdiv_enable_anta  Regc50[8]=1'b0  0: control by c50[9]
        ODM_SetBBReg(pDM_Odm, 0xc50 , BIT9, 1);  // 1:CG, 0:CS
        return;
#endif

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	 pDM_Odm->antdiv_rssi=0;
#endif

	 ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8192E AntDiv_Init =>  AntDivType=[CG_TRX_HW_ANTDIV]\n"));
	
	//3 --RFE pin setting---------
	//[MAC]
	ODM_SetMACReg(pDM_Odm, 0x38, BIT11, 1);            //DBG PAD Driving control (GPIO 8)
	ODM_SetMACReg(pDM_Odm, 0x4c, BIT23, 0);            //path-A , RFE_CTRL_3 & RFE_CTRL_4
	//[BB]
	ODM_SetBBReg(pDM_Odm, 0x944 , BIT4|BIT3, 0x3);     //RFE_buffer
	ODM_SetBBReg(pDM_Odm, 0x940 , BIT7|BIT6, 0x0); // r_rfe_path_sel_   (RFE_CTRL_3)
	ODM_SetBBReg(pDM_Odm, 0x940 , BIT9|BIT8, 0x0); // r_rfe_path_sel_   (RFE_CTRL_4)
	ODM_SetBBReg(pDM_Odm, 0x944 , BIT31, 0);     //RFE_buffer
	ODM_SetBBReg(pDM_Odm, 0x92C , BIT3, 0);     //rfe_inv  (RFE_CTRL_3)
	ODM_SetBBReg(pDM_Odm, 0x92C , BIT4, 1);     //rfe_inv  (RFE_CTRL_4)
	ODM_SetBBReg(pDM_Odm, 0x930 , 0xFF000, 0x88);           //path-A , RFE_CTRL_3 & 4=> ANTSEL[0]
	//3 -------------------------
	
	 //Pin Settings
	ODM_SetBBReg(pDM_Odm, 0xC50 , BIT8, 0);	   //path-A   	//disable CS/CG switch
	ODM_SetBBReg(pDM_Odm, 0xC50 , BIT9, 1);	   //path-A   	//output at CG only
	ODM_SetBBReg(pDM_Odm, 0x870 , BIT9|BIT8, 0);  //path-A  	//antsel antselb by HW
	ODM_SetBBReg(pDM_Odm, 0xB38 , BIT10, 0);	   //path-A    	//antsel2 by HW	
 
	//Mapping table
	 ODM_SetBBReg(pDM_Odm, 0x914 , 0xFFFF, 0x0100); //antenna mapping table
	  
	 //OFDM Settings
	 ODM_SetBBReg(pDM_Odm, 0xca4 , 0x7FF, 0xA0); //thershold
	 ODM_SetBBReg(pDM_Odm, 0xca4 , 0x7FF000, 0x0); //bias
	 
	 //CCK Settings
	 ODM_SetBBReg(pDM_Odm, 0xa04 , 0xF000000, 0); //Select which path to receive for CCK_1 & CCK_2
	 ODM_SetBBReg(pDM_Odm, 0xb34 , BIT30, 1); //(92E) ANTSEL_CCK_opt = r_en_antsel_cck? ANTSEL_CCK: 1'b0
	 ODM_SetBBReg(pDM_Odm, 0xa74 , BIT7, 1); //Fix CCK PHY status report issue
	 ODM_SetBBReg(pDM_Odm, 0xa0c , BIT4, 1); //CCK complete HW AntDiv within 64 samples 

	 //Timming issue
	 ODM_SetBBReg(pDM_Odm, 0xE20 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)
}

VOID
odm_Smart_HWAntDiv_Init_92E(
	IN		PDM_ODM_T		pDM_Odm
)
{
    ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8188E AntDiv_Init =>  AntDivType=[CG_TRX_SMART_ANTDIV]\n"));
}
#endif //#if (RTL8192E_SUPPORT == 1)


#if (RTL8723B_SUPPORT == 1)
VOID
odm_TRX_HWAntDiv_Init_8723B(
	IN		PDM_ODM_T		pDM_Odm
)
{
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8723B AntDiv_Init =>  AntDivType=[CG_TRX_HW_ANTDIV(DPDT)]\n"));
      
	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte1, 1);
	
	//OFDM HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xCA4 , 0x7FF, 0xa0); //thershold
	ODM_SetBBReg(pDM_Odm, 0xCA4 , 0x7FF000, 0x00); //bias
		
	//CCK HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xA74 , BIT7, 1); //patch for clk from 88M to 80M
	ODM_SetBBReg(pDM_Odm, 0xA0C , BIT4, 1); //do 64 samples
	
	//BT Coexistence
	ODM_SetBBReg(pDM_Odm, 0x864, BIT12, 0); //keep antsel_map when GNT_BT = 1
	ODM_SetBBReg(pDM_Odm, 0x874 , BIT23, 0); //Disable hw antsw & fast_train.antsw when GNT_BT=1

        //Output Pin Settings
	ODM_SetBBReg(pDM_Odm, 0x870 , BIT8, 0); //
		
	ODM_SetBBReg(pDM_Odm, 0x948 , BIT6, 0); //WL_BB_SEL_BTG_TRXG_anta,  (1: HW CTRL  0: SW CTRL)
	ODM_SetBBReg(pDM_Odm, 0x948 , BIT7, 0);
		
	ODM_SetMACReg(pDM_Odm, 0x40 , BIT3, 1);
	ODM_SetMACReg(pDM_Odm, 0x38 , BIT11, 1);
	ODM_SetMACReg(pDM_Odm, 0x4C ,  BIT24|BIT23, 2); //select DPDT_P and DPDT_N as output pin
		
	ODM_SetBBReg(pDM_Odm, 0x944 , BIT0|BIT1, 3); //in/out
	ODM_SetBBReg(pDM_Odm, 0x944 , BIT31, 0); //

	ODM_SetBBReg(pDM_Odm, 0x92C , BIT1, 0); //DPDT_P non-inverse
	ODM_SetBBReg(pDM_Odm, 0x92C , BIT0, 1); //DPDT_N inverse

	ODM_SetBBReg(pDM_Odm, 0x930 , 0xF0, 8); // DPDT_P = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0x930 , 0xF, 8); // DPDT_N = ANTSEL[0]

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0xE20 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)

	//2 [--For HW Bug Setting]
	if(pDM_Odm->AntType == ODM_AUTO_ANT)
		ODM_SetBBReg(pDM_Odm, 0xA00 , BIT15, 0); //CCK AntDiv function block enable

	//ODM_SetBBReg(pDM_Odm, 0x80C , BIT21, 0); //TX Ant  by Reg


}

	

VOID
odm_S0S1_SWAntDiv_Init_8723B(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T		pDM_FatTable = &pDM_Odm->DM_FatTable;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8723B AntDiv_Init => AntDivType=[ S0S1_SW_AntDiv] \n"));

	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0x914 , bMaskByte1, 1);
	
	//Output Pin Settings
	//ODM_SetBBReg(pDM_Odm, 0x948 , BIT6, 0x1); 
	ODM_SetBBReg(pDM_Odm, 0x870 , BIT9|BIT8, 0); 

	pDM_FatTable->bBecomeLinked  =FALSE;
	pDM_SWAT_Table->try_flag = 0xff;	
	pDM_SWAT_Table->Double_chk_flag = 0;
	pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0xE20 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)
	
	//2 [--For HW Bug Setting]
	ODM_SetBBReg(pDM_Odm, 0x80C , BIT21, 0); //TX Ant  by Reg

}
#endif //#if (RTL8723B_SUPPORT == 1)


#if (RTL8821A_SUPPORT == 1)
VOID
odm_TRX_HWAntDiv_Init_8821A(
	IN		PDM_ODM_T		pDM_Odm
)
{
	
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	

	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	pAdapter->HalFunc.GetHalDefVarHandler(pAdapter, HAL_DEF_5G_ANT_SELECT, (pu1Byte)(&pDM_Odm->AntType));	
#else
	pDM_Odm->AntType = ODM_AUTO_ANT;
#endif
	pAdapter->HalFunc.GetHalDefVarHandler(pAdapter, HAL_DEF_5G_ANT_SELECT, (pu1Byte)(&pDM_Odm->AntType));	

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821A AntDiv_Init => AntDivType=[ CG_TRX_HW_ANTDIV (DPDT)] \n"));

	//Output Pin Settings
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT25, 0);

	ODM_SetMACReg(pDM_Odm, 0x64 , BIT29, 1); //PAPE by WLAN control
	ODM_SetMACReg(pDM_Odm, 0x64 , BIT28, 1); //LNAON by WLAN control

	ODM_SetBBReg(pDM_Odm, 0xCB0 , bMaskDWord, 0x77775745);
	ODM_SetBBReg(pDM_Odm, 0xCB8 , BIT16, 0);
	
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT23, 0); //select DPDT_P and DPDT_N as output pin
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT24, 1); //by WLAN control
	ODM_SetBBReg(pDM_Odm, 0xCB4 , 0xF, 8); // DPDT_P = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB4 , 0xF0, 8); // DPDT_N = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT29, 0); //DPDT_P non-inverse
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT28, 1); //DPDT_N inverse

	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte1, 1);

	//Set ANT1_8821A as MAIN_ANT
	if((pDM_Odm->AntType == ODM_FIX_MAIN_ANT) || (pDM_Odm->AntType == ODM_AUTO_ANT))
		ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
	else
		ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);

	//OFDM HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF, 0xA0); //thershold
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF000, 0x10); //bias
		
	//CCK HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xA74 , BIT7, 1); //patch for clk from 88M to 80M
	ODM_SetBBReg(pDM_Odm, 0xA0C , BIT4, 1); //do 64 samples

	ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function block enable

	//BT Coexistence
	ODM_SetBBReg(pDM_Odm, 0xCAC , BIT9, 1); //keep antsel_map when GNT_BT = 1
	ODM_SetBBReg(pDM_Odm, 0x804 , BIT4, 1); //Disable hw antsw & fast_train.antsw when GNT_BT=1

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0x818 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)
	ODM_SetBBReg(pDM_Odm, 0x8CC , BIT20|BIT19|BIT18, 3); //settling time of antdiv by RF LNA = 100ns

	//response TX ant by RX ant
	ODM_SetMACReg(pDM_Odm, 0x668 , BIT3, 1);
	
	//2 [--For HW Bug Setting]
	if(pDM_Odm->AntType == ODM_AUTO_ANT)
		ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function block enable
			
}

VOID
odm_S0S1_SWAntDiv_Init_8821A(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pSWAT_T		pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T		pDM_FatTable = &pDM_Odm->DM_FatTable;



#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)	

	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	pAdapter->HalFunc.GetHalDefVarHandler(pAdapter, HAL_DEF_5G_ANT_SELECT, (pu1Byte)(&pDM_Odm->AntType));	
#else
	pDM_Odm->AntType = ODM_AUTO_ANT;
#endif

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8821A AntDiv_Init => AntDivType=[ S0S1_SW_AntDiv] \n"));

	//Output Pin Settings
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT25, 0);

	ODM_SetMACReg(pDM_Odm, 0x64 , BIT29, 1); //PAPE by WLAN control
	ODM_SetMACReg(pDM_Odm, 0x64 , BIT28, 1); //LNAON by WLAN control

	ODM_SetBBReg(pDM_Odm, 0xCB0 , bMaskDWord, 0x77775745);
	ODM_SetBBReg(pDM_Odm, 0xCB8 , BIT16, 0);
	
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT23, 0); //select DPDT_P and DPDT_N as output pin
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT24, 1); //by WLAN control
	ODM_SetBBReg(pDM_Odm, 0xCB4 , 0xF, 8); // DPDT_P = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB4 , 0xF0, 8); // DPDT_N = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT29, 0); //DPDT_P non-inverse
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT28, 1); //DPDT_N inverse

	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte1, 1);

	//Set ANT1_8821A as MAIN_ANT
	if((pDM_Odm->AntType == ODM_FIX_MAIN_ANT) || (pDM_Odm->AntType == ODM_AUTO_ANT))
		ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
	else
		ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);

	//OFDM HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF, 0xA0); //thershold
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF000, 0x10); //bias
		
	//CCK HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xA74 , BIT7, 1); //patch for clk from 88M to 80M
	ODM_SetBBReg(pDM_Odm, 0xA0C , BIT4, 1); //do 64 samples

	ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function block enable

	//BT Coexistence
	ODM_SetBBReg(pDM_Odm, 0xCAC , BIT9, 1); //keep antsel_map when GNT_BT = 1
	ODM_SetBBReg(pDM_Odm, 0x804 , BIT4, 1); //Disable hw antsw & fast_train.antsw when GNT_BT=1

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0x818 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)
	ODM_SetBBReg(pDM_Odm, 0x8CC , BIT20|BIT19|BIT18, 3); //settling time of antdiv by RF LNA = 100ns

	//response TX ant by RX ant
	ODM_SetMACReg(pDM_Odm, 0x668 , BIT3, 1);
	
	//2 [--For HW Bug Setting]
	if(pDM_Odm->AntType == ODM_AUTO_ANT)
		ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function block enable

		
	ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 0); 
	
	pDM_SWAT_Table->try_flag = 0xff;	
	pDM_SWAT_Table->Double_chk_flag = 0;
	pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;
	pDM_SWAT_Table->PreAntenna = MAIN_ANT;
	pDM_SWAT_Table->SWAS_NoLink_State = 0;

}
#endif //#if (RTL8821A_SUPPORT == 1)

#if (RTL8881A_SUPPORT == 1)
VOID
odm_RX_HWAntDiv_Init_8881A(
	IN		PDM_ODM_T		pDM_Odm
)
{
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8881A AntDiv_Init => AntDivType=[ CGCS_RX_HW_ANTDIV] \n"));

}

VOID
odm_TRX_HWAntDiv_Init_8881A(
	IN		PDM_ODM_T		pDM_Odm
)
{

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8881A AntDiv_Init => AntDivType=[ CG_TRX_HW_ANTDIV (SPDT)] \n"));

	//Output Pin Settings
	// [SPDT related]
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT25, 0);
	ODM_SetMACReg(pDM_Odm, 0x4C , BIT26, 0);
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT31, 0); //delay buffer
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT22, 0); 
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT24, 1);
	ODM_SetBBReg(pDM_Odm, 0xCB0 , 0xF00, 8); // DPDT_P = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB0 , 0xF0000, 8); // DPDT_N = ANTSEL[0]	
	
	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte1, 1);

	//OFDM HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF, 0xA0); //thershold
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF000, 0x0); //bias
	ODM_SetBBReg(pDM_Odm, 0x8CC , BIT20|BIT19|BIT18, 3); //settling time of antdiv by RF LNA = 100ns
	
	//CCK HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xA74 , BIT7, 1); //patch for clk from 88M to 80M
	ODM_SetBBReg(pDM_Odm, 0xA0C , BIT4, 1); //do 64 samples

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0x818 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)

	//2 [--For HW Bug Setting]

	ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 0); //TX Ant  by Reg //  A-cut bug
}

#endif //#if (RTL8881A_SUPPORT == 1)


#if (RTL8812A_SUPPORT == 1)
VOID
odm_TRX_HWAntDiv_Init_8812A(
	IN		PDM_ODM_T		pDM_Odm
)
{
	 ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***8812A AntDiv_Init => AntDivType=[ CG_TRX_HW_ANTDIV (SPDT)] \n"));

	//3 //3 --RFE pin setting---------
	//[BB]
	ODM_SetBBReg(pDM_Odm, 0x900 , BIT10|BIT9|BIT8, 0x0);	  //disable SW switch
	ODM_SetBBReg(pDM_Odm, 0x900 , BIT17|BIT16, 0x0);	 
	ODM_SetBBReg(pDM_Odm, 0x974 , BIT7|BIT6, 0x3);     // in/out
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT31, 0); //delay buffer
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT26, 0); 
	ODM_SetBBReg(pDM_Odm, 0xCB4 , BIT27, 1);
	ODM_SetBBReg(pDM_Odm, 0xCB0 , 0xF000000, 8); // DPDT_P = ANTSEL[0]
	ODM_SetBBReg(pDM_Odm, 0xCB0 , 0xF0000000, 8); // DPDT_N = ANTSEL[0]
	//3 -------------------------

	//Mapping Table
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte0, 0);
	ODM_SetBBReg(pDM_Odm, 0xCA4 , bMaskByte1, 1);

	//OFDM HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF, 0xA0); //thershold
	ODM_SetBBReg(pDM_Odm, 0x8D4 , 0x7FF000, 0x0); //bias
	ODM_SetBBReg(pDM_Odm, 0x8CC , BIT20|BIT19|BIT18, 3); //settling time of antdiv by RF LNA = 100ns
	
	//CCK HW AntDiv Parameters
	ODM_SetBBReg(pDM_Odm, 0xA74 , BIT7, 1); //patch for clk from 88M to 80M
	ODM_SetBBReg(pDM_Odm, 0xA0C , BIT4, 1); //do 64 samples

	//Timming issue
	ODM_SetBBReg(pDM_Odm, 0x818 , BIT23|BIT22|BIT21|BIT20, 8); //keep antidx after tx for ACK ( unit x 32 mu sec)

	//2 [--For HW Bug Setting]

	ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 0); //TX Ant  by Reg //  A-cut bug
	
}

#endif //#if (RTL8812A_SUPPORT == 1)

VOID
odm_HW_AntDiv(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	i,MinMaxRSSI=0xFF, AntDivMaxRSSI=0, MaxRSSI=0, LocalMaxRSSI;
	u4Byte	Main_RSSI, Aux_RSSI, pkt_ratio_m=0, pkt_ratio_a=0,pkt_threshold=10;
	u1Byte	RxIdleAnt=0, TargetAnt=7;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
	PSTA_INFO_T   	pEntry;

	if(!pDM_Odm->bLinked) //bLinked==False
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));
	
		#if(DM_ODM_SUPPORT_TYPE  == ODM_AP)
			if (pDM_Odm->antdiv_rssi)
				panic_printk("[No Link!!!]\n");
		#endif
	
		if(pDM_FatTable->bBecomeLinked == TRUE)
		{
			odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
			ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);

			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;
		}
		return;
	}	
	else
	{
		if(pDM_FatTable->bBecomeLinked ==FALSE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));
			odm_AntDiv_on_off(pDM_Odm, ANTDIV_ON);
			if(pDM_Odm->SupportICType == ODM_RTL8821 )
				ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function disable
				
			#if(DM_ODM_SUPPORT_TYPE  == ODM_AP)
			else if(pDM_Odm->SupportICType == ODM_RTL8881 )
				ODM_SetBBReg(pDM_Odm, 0x800 , BIT25, 0); //CCK AntDiv function disable
			#endif
			
			else if(pDM_Odm->SupportICType == ODM_RTL8723B ||pDM_Odm->SupportICType == ODM_RTL8812)
				ODM_SetBBReg(pDM_Odm, 0xA00 , BIT15, 0); //CCK AntDiv function disable
			
			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;

			if(pDM_Odm->SupportICType==ODM_RTL8723B && pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
			{
				ODM_SetBBReg(pDM_Odm, 0x930 , 0xF0, 8); // DPDT_P = ANTSEL[0]   // for 8723B AntDiv function patch.  BB  Dino  130412	
				ODM_SetBBReg(pDM_Odm, 0x930 , 0xF, 8); // DPDT_N = ANTSEL[0]
			}
		}	
	}	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("\n[HW AntDiv] Start =>\n"));
	   
	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
		{
			//2 Caculate RSSI per Antenna
			Main_RSSI = (pDM_FatTable->MainAnt_Cnt[i]!=0)?(pDM_FatTable->MainAnt_Sum[i]/pDM_FatTable->MainAnt_Cnt[i]):0;
			Aux_RSSI = (pDM_FatTable->AuxAnt_Cnt[i]!=0)?(pDM_FatTable->AuxAnt_Sum[i]/pDM_FatTable->AuxAnt_Cnt[i]):0;
			TargetAnt = (Main_RSSI==Aux_RSSI)?pDM_FatTable->RxIdleAnt:((Main_RSSI>=Aux_RSSI)?MAIN_ANT:AUX_ANT);
			/*
			if( pDM_FatTable->MainAnt_Cnt[i]!=0 && pDM_FatTable->AuxAnt_Cnt[i]!=0 )
			{
			pkt_ratio_m=( pDM_FatTable->MainAnt_Cnt[i] / pDM_FatTable->AuxAnt_Cnt[i] );
			pkt_ratio_a=( pDM_FatTable->AuxAnt_Cnt[i] / pDM_FatTable->MainAnt_Cnt[i] );
				
				if (pkt_ratio_m >= pkt_threshold)
					TargetAnt=MAIN_ANT;
				
				else if(pkt_ratio_a >= pkt_threshold)
					TargetAnt=AUX_ANT;
			}
			*/			
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("*** SupportICType=[%u] \n",pDM_Odm->SupportICType));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Main_Cnt = (( %u ))  , Main_RSSI= ((  %u )) \n", pDM_FatTable->MainAnt_Cnt[i], Main_RSSI));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Aux_Cnt   = (( %u ))  , Aux_RSSI = ((  %u )) \n", pDM_FatTable->AuxAnt_Cnt[i]  , Aux_RSSI ));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** MAC ID:[ %u ] , TargetAnt = (( %s )) \n", i ,( TargetAnt ==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("*** Phy_AntSel_A=[ %d, %d, %d] \n",((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT2)>>2,
				                                                                              ((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT1) >>1, ((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT0)));
			#if(DM_ODM_SUPPORT_TYPE  == ODM_AP)
			if (pDM_Odm->antdiv_rssi)
			{
				panic_printk("*** SupportICType=[%lu] \n",pDM_Odm->SupportICType);
				//panic_printk("*** Phy_AntSel_A=[ %d, %d, %d] \n",((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT2)>>2,
				//	((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT1) >>1, ((pDM_Odm->DM_FatTable.antsel_rx_keep_0)&BIT0));
				//panic_printk("*** Phy_AntSel_B=[ %d, %d, %d] \n",((pDM_Odm->DM_FatTable.antsel_rx_keep_1)&BIT2)>>2,
				//	((pDM_Odm->DM_FatTable.antsel_rx_keep_1)&BIT1) >>1, ((pDM_Odm->DM_FatTable.antsel_rx_keep_1)&BIT0))
				panic_printk("*** Client[ %lu ] , Main_Cnt = (( %lu ))  , Main_RSSI= ((  %lu )) \n",i, pDM_FatTable->MainAnt_Cnt[i], Main_RSSI);
				panic_printk("*** Client[ %lu ] , Aux_Cnt   = (( %lu ))  , Aux_RSSI = ((  %lu )) \n" ,i, pDM_FatTable->AuxAnt_Cnt[i] , Aux_RSSI);
			}
			#endif


			LocalMaxRSSI = (Main_RSSI>Aux_RSSI)?Main_RSSI:Aux_RSSI;
			//2 Select MaxRSSI for DIG
			if((LocalMaxRSSI > AntDivMaxRSSI) && (LocalMaxRSSI < 40))
				AntDivMaxRSSI = LocalMaxRSSI;
			if(LocalMaxRSSI > MaxRSSI)
				MaxRSSI = LocalMaxRSSI;

			//2 Select RX Idle Antenna
			if ( (LocalMaxRSSI != 0) &&  (LocalMaxRSSI < MinMaxRSSI) )
			{
				RxIdleAnt = TargetAnt;
				MinMaxRSSI = LocalMaxRSSI;
			}
			/*
			if((pDM_FatTable->RxIdleAnt == MAIN_ANT) && (Main_RSSI == 0))
				Main_RSSI = Aux_RSSI;
			else if((pDM_FatTable->RxIdleAnt == AUX_ANT) && (Aux_RSSI == 0))
				Aux_RSSI = Main_RSSI;
		
			LocalMinRSSI = (Main_RSSI>Aux_RSSI)?Aux_RSSI:Main_RSSI;
			if(LocalMinRSSI < MinRSSI)
			{
				MinRSSI = LocalMinRSSI;
				RxIdleAnt = TargetAnt;
			}	
			*/
			//2 Select TX Antenna

			#if TX_BY_REG
			
			#else
				if(pDM_Odm->AntDivType != CGCS_RX_HW_ANTDIV)
					odm_UpdateTxAnt(pDM_Odm, TargetAnt, i);
			#endif

		}
		pDM_FatTable->MainAnt_Sum[i] = 0;
		pDM_FatTable->AuxAnt_Sum[i] = 0;
		pDM_FatTable->MainAnt_Cnt[i] = 0;
		pDM_FatTable->AuxAnt_Cnt[i] = 0;
	}
       
	//2 Set RX Idle Antenna
	ODM_UpdateRxIdleAnt(pDM_Odm, RxIdleAnt);

	#if(DM_ODM_SUPPORT_TYPE  == ODM_AP)
		if (pDM_Odm->antdiv_rssi)
			panic_printk("*** RxIdleAnt = (( %s )) \n \n", ( RxIdleAnt ==MAIN_ANT)?"MAIN_ANT":"AUX_ANT");
	#endif
	
	pDM_DigTable->AntDiv_RSSI_max = AntDivMaxRSSI;
	pDM_DigTable->RSSI_max = MaxRSSI;
}



#if (RTL8723B_SUPPORT == 1)||(RTL8821A_SUPPORT == 1)
VOID
odm_S0S1_SwAntDiv(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			Step
	)
{
	u4Byte			i,MinMaxRSSI=0xFF, LocalMaxRSSI,LocalMinRSSI;
	u4Byte			Main_RSSI, Aux_RSSI;
	u1Byte			reset_period=10, SWAntDiv_threshold=35;
	u1Byte			HighTraffic_TrainTime_U=0x32,HighTraffic_TrainTime_L,Train_time_temp;
	u1Byte			LowTraffic_TrainTime_U=200,LowTraffic_TrainTime_L;
	u1Byte			RxIdleAnt, TargetAnt, nextAnt;
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;	
	PSTA_INFO_T		pEntry=NULL;
	//static u1Byte		reset_idx;
	u4Byte			value32;
	PADAPTER		Adapter	 =  pDM_Odm->Adapter;
	u8Byte			curTxOkCnt=0, curRxOkCnt=0,TxCntOffset, RxCntOffset;
	
	if(!pDM_Odm->bLinked) //bLinked==False
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[No Link!!!]\n"));
		if(pDM_FatTable->bBecomeLinked == TRUE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set REG 948[9:6]=0x0 \n"));
			if(pDM_Odm->SupportICType == ODM_RTL8723B)
				ODM_SetBBReg(pDM_Odm, 0x948 , BIT9|BIT8|BIT7|BIT6, 0x0); 
			
			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;
		}
		return;
	}
	else
	{
		if(pDM_FatTable->bBecomeLinked ==FALSE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Linked !!!]\n"));
			
			if(pDM_Odm->SupportICType == ODM_RTL8723B)
			{
				value32 = ODM_GetBBReg(pDM_Odm, 0x864, BIT5|BIT4|BIT3);
				
				if (value32==0x0)
					ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
				else if (value32==0x1)
					ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
				
				ODM_SetBBReg(pDM_Odm, 0x948 , BIT6, 0x1);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Set REG 948[6]=0x1 , Set REG 864[5:3]=0x%x \n",value32 ));
			}

			pDM_SWAT_Table->lastTxOkCnt = 0; 
			pDM_SWAT_Table->lastRxOkCnt =0; 
			TxCntOffset = Adapter->TxStats.NumTxBytesUnicast;
			RxCntOffset = Adapter->RxStats.NumRxBytesUnicast;
			
			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;
		}
		else
		{
			TxCntOffset = 0;
			RxCntOffset = 0;
		}
	}
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[%d] { try_flag=(( %d )), Step=(( %d )), Double_chk_flag = (( %d )) }\n",
		__LINE__,pDM_SWAT_Table->try_flag,Step,pDM_SWAT_Table->Double_chk_flag));

	// Handling step mismatch condition.
	// Peak step is not finished at last time. Recover the variable and check again.
	if(	Step != pDM_SWAT_Table->try_flag	)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[Step != try_flag]    Need to Reset After Link\n"));
		ODM_SwAntDivRestAfterLink(pDM_Odm);
	}

	if(pDM_SWAT_Table->try_flag == 0xff) 
	{	
		pDM_SWAT_Table->try_flag = 0;
		pDM_SWAT_Table->Train_time_flag=0;
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("[set try_flag = 0]  Prepare for peak!\n\n"));
		return;
	}	
	else//if( try_flag != 0xff ) 
	{
		//1 Normal State (Begin Trying)
		if(pDM_SWAT_Table->try_flag == 0) 
		{
		
			//---trafic decision---
			curTxOkCnt = Adapter->TxStats.NumTxBytesUnicast - pDM_SWAT_Table->lastTxOkCnt - TxCntOffset;
			curRxOkCnt =Adapter->RxStats.NumRxBytesUnicast - pDM_SWAT_Table->lastRxOkCnt - RxCntOffset;
			pDM_SWAT_Table->lastTxOkCnt = Adapter->TxStats.NumTxBytesUnicast;
			pDM_SWAT_Table->lastRxOkCnt = Adapter->RxStats.NumRxBytesUnicast;
			
			if (curTxOkCnt > 1875000 || curRxOkCnt > 1875000)//if(PlatformDivision64(curTxOkCnt+curRxOkCnt, 2) > 1875000)  ( 1.875M * 8bit ) / 2= 7.5M bits /sec )
			{
				pDM_SWAT_Table->TrafficLoad = TRAFFIC_HIGH;
				Train_time_temp=pDM_SWAT_Table->Train_time ;
				
				if(pDM_SWAT_Table->Train_time_flag==3)
				{
					HighTraffic_TrainTime_L=0xa;
					
					if(Train_time_temp<=16)
						Train_time_temp=HighTraffic_TrainTime_L;
					else
						Train_time_temp-=16;
					
				}				
				else if(pDM_SWAT_Table->Train_time_flag==2)
				{
					Train_time_temp-=8;
					HighTraffic_TrainTime_L=0xf;
				}	
				else if(pDM_SWAT_Table->Train_time_flag==1)
				{
					Train_time_temp-=4;
					HighTraffic_TrainTime_L=0x1e;
				}
				else if(pDM_SWAT_Table->Train_time_flag==0)
				{
					Train_time_temp+=8;
					HighTraffic_TrainTime_L=0x28;
				}

				
				//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** Train_time_temp = ((%d))\n",Train_time_temp));

				//--
				if(Train_time_temp > HighTraffic_TrainTime_U)
					Train_time_temp=HighTraffic_TrainTime_U;
				
				else if(Train_time_temp < HighTraffic_TrainTime_L)
					Train_time_temp=HighTraffic_TrainTime_L;

				pDM_SWAT_Table->Train_time = Train_time_temp; //50ms~10ms
				
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("  Train_time_flag=((%d)) , Train_time=((%d)) \n",pDM_SWAT_Table->Train_time_flag, pDM_SWAT_Table->Train_time));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("  [HIGH Traffic]  \n" ));
			}
			else if (curTxOkCnt > 125000 || curRxOkCnt > 125000) // ( 0.125M * 8bit ) / 2 =  0.5M bits /sec )
			{
				pDM_SWAT_Table->TrafficLoad = TRAFFIC_LOW;
				Train_time_temp=pDM_SWAT_Table->Train_time ;

				if(pDM_SWAT_Table->Train_time_flag==3)
				{
					LowTraffic_TrainTime_L=10;
					if(Train_time_temp<50)
						Train_time_temp=LowTraffic_TrainTime_L;
					else
						Train_time_temp-=50;
				}				
				else if(pDM_SWAT_Table->Train_time_flag==2)
				{
					Train_time_temp-=30;
					LowTraffic_TrainTime_L=36;
				}	
				else if(pDM_SWAT_Table->Train_time_flag==1)
				{
					Train_time_temp-=10;
					LowTraffic_TrainTime_L=40;
				}
				else
					Train_time_temp+=10;	

				//--
				if(Train_time_temp >= LowTraffic_TrainTime_U)
					Train_time_temp=LowTraffic_TrainTime_U;
				
				else if(Train_time_temp <= LowTraffic_TrainTime_L)
					Train_time_temp=LowTraffic_TrainTime_L;

				pDM_SWAT_Table->Train_time = Train_time_temp; //50ms~20ms

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("  Train_time_flag=((%d)) , Train_time=((%d)) \n",pDM_SWAT_Table->Train_time_flag, pDM_SWAT_Table->Train_time));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("  [Low Traffic]  \n" ));
			}
			else
			{
				pDM_SWAT_Table->TrafficLoad = TRAFFIC_UltraLOW;
				pDM_SWAT_Table->Train_time = 0xc8; //200ms
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("  [Ultra-Low Traffic]  \n" ));
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TxOkCnt=(( %llu )), RxOkCnt=(( %llu )) \n", 
				curTxOkCnt ,curRxOkCnt ));
				
			//-----------------
		
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,(" Current MinMaxRSSI is ((%d)) \n",pDM_FatTable->MinMaxRSSI));

                        //---reset index---
			if(pDM_SWAT_Table->reset_idx>=reset_period)
			{
				pDM_FatTable->MinMaxRSSI=0; //
				pDM_SWAT_Table->reset_idx=0;
			}
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("reset_idx = (( %d )) \n",pDM_SWAT_Table->reset_idx ));
			//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("reset_idx=%d\n",pDM_SWAT_Table->reset_idx));
			pDM_SWAT_Table->reset_idx++;

			//---double check flag---
			if(pDM_FatTable->MinMaxRSSI > SWAntDiv_threshold && pDM_SWAT_Table->Double_chk_flag== 0)
			{			
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,(" MinMaxRSSI is ((%d)), and > %d \n",
					pDM_FatTable->MinMaxRSSI,SWAntDiv_threshold));

				pDM_SWAT_Table->Double_chk_flag =1;
				pDM_SWAT_Table->try_flag = 1; 
			        pDM_SWAT_Table->RSSI_Trying = 0;

				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" Test the current Ant for (( %d )) ms again \n", pDM_SWAT_Table->Train_time));
				ODM_UpdateRxIdleAnt(pDM_Odm, pDM_FatTable->RxIdleAnt);	
				ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer_8723B, pDM_SWAT_Table->Train_time ); //ms	
				return;
			}
			
			nextAnt = (pDM_FatTable->RxIdleAnt == MAIN_ANT)? AUX_ANT : MAIN_ANT;

			pDM_SWAT_Table->try_flag = 1;
			
			if(pDM_SWAT_Table->reset_idx<=1)
				pDM_SWAT_Table->RSSI_Trying = 2;
			else
				pDM_SWAT_Table->RSSI_Trying = 1;
			
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("[set try_flag=1]  Normal State:  Begin Trying!! \n"));
									
		}
	
		else if(pDM_SWAT_Table->try_flag == 1 && pDM_SWAT_Table->Double_chk_flag== 0)
		{	
			nextAnt = (pDM_FatTable->RxIdleAnt  == MAIN_ANT)? AUX_ANT : MAIN_ANT;		
			pDM_SWAT_Table->RSSI_Trying--;
		}
		
		//1 Decision State
		if((pDM_SWAT_Table->try_flag == 1)&&(pDM_SWAT_Table->RSSI_Trying == 0) )
		{
			
			for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
			{
				pEntry = pDM_Odm->pODM_StaInfo[i];
				if(IS_STA_VALID(pEntry))
				{
					//2 Caculate RSSI per Antenna
					Main_RSSI = (pDM_FatTable->MainAnt_Cnt[i]!=0)?(pDM_FatTable->MainAnt_Sum[i]/pDM_FatTable->MainAnt_Cnt[i]):0;
					Aux_RSSI = (pDM_FatTable->AuxAnt_Cnt[i]!=0)?(pDM_FatTable->AuxAnt_Sum[i]/pDM_FatTable->AuxAnt_Cnt[i]):0;
					
					if(pDM_FatTable->MainAnt_Cnt[i]<=1 && pDM_FatTable->CCK_counter_main>=1)
						Main_RSSI=0;	
					
					if(pDM_FatTable->AuxAnt_Cnt[i]<=1 && pDM_FatTable->CCK_counter_aux>=1)
						Aux_RSSI=0;

					TargetAnt = (Main_RSSI==Aux_RSSI)?pDM_SWAT_Table->PreAntenna:((Main_RSSI>=Aux_RSSI)?MAIN_ANT:AUX_ANT);
					LocalMaxRSSI = (Main_RSSI>=Aux_RSSI) ? Main_RSSI : Aux_RSSI;
					LocalMinRSSI = (Main_RSSI>=Aux_RSSI) ? Aux_RSSI : Main_RSSI;
					
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  CCK_counter_main = (( %d ))  , CCK_counter_aux= ((  %d )) \n", pDM_FatTable->CCK_counter_main, pDM_FatTable->CCK_counter_aux));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  OFDM_counter_main = (( %d ))  , OFDM_counter_aux= ((  %d )) \n", pDM_FatTable->OFDM_counter_main, pDM_FatTable->OFDM_counter_aux));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Main_Cnt = (( %d ))  , Main_RSSI= ((  %d )) \n", pDM_FatTable->MainAnt_Cnt[i], Main_RSSI));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("***  Aux_Cnt   = (( %d ))  , Aux_RSSI = ((  %d )) \n", pDM_FatTable->AuxAnt_Cnt[i]  , Aux_RSSI ));
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** MAC ID:[ %d ] , TargetAnt = (( %s )) \n", i ,( TargetAnt ==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
					
					//2 Select RX Idle Antenna
					
					if (LocalMaxRSSI != 0 && LocalMaxRSSI < MinMaxRSSI)
					{
							RxIdleAnt = TargetAnt;
							MinMaxRSSI = LocalMaxRSSI;
							ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("*** LocalMaxRSSI-LocalMinRSSI = ((%d))\n",(LocalMaxRSSI-LocalMinRSSI)));
					
							if((LocalMaxRSSI-LocalMinRSSI)>8)
							{
								if(LocalMinRSSI != 0)
									pDM_SWAT_Table->Train_time_flag=3;
								else
								{
									if(MinMaxRSSI > SWAntDiv_threshold)
										pDM_SWAT_Table->Train_time_flag=0;
									else
										pDM_SWAT_Table->Train_time_flag=3;
								}
							}
							else if((LocalMaxRSSI-LocalMinRSSI)>5)
								pDM_SWAT_Table->Train_time_flag=2;
							else if((LocalMaxRSSI-LocalMinRSSI)>2)
								pDM_SWAT_Table->Train_time_flag=1;
							else
								pDM_SWAT_Table->Train_time_flag=0;
							
					}
					
					//2 Select TX Antenna
					if(TargetAnt == MAIN_ANT)
						pDM_FatTable->antsel_a[i] = ANT1_2G;
					else
						pDM_FatTable->antsel_a[i] = ANT2_2G;
			
				}
					pDM_FatTable->MainAnt_Sum[i] = 0;
					pDM_FatTable->AuxAnt_Sum[i] = 0;
					pDM_FatTable->MainAnt_Cnt[i] = 0;
					pDM_FatTable->AuxAnt_Cnt[i] = 0;
					pDM_FatTable->CCK_counter_main=0;
					pDM_FatTable->CCK_counter_aux=0;
					pDM_FatTable->OFDM_counter_main=0;
					pDM_FatTable->OFDM_counter_aux=0;

			}
		
			
			pDM_FatTable->MinMaxRSSI=MinMaxRSSI;
			pDM_SWAT_Table->try_flag = 0;
						
			if( pDM_SWAT_Table->Double_chk_flag==1)
			{
				pDM_SWAT_Table->Double_chk_flag=0;
				if(pDM_FatTable->MinMaxRSSI > SWAntDiv_threshold)
				{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,(" [Double check] MinMaxRSSI ((%d)) > %d again!! \n",
						pDM_FatTable->MinMaxRSSI,SWAntDiv_threshold));
					
					ODM_UpdateRxIdleAnt(pDM_Odm, RxIdleAnt);	
					
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("[reset try_flag = 0] Training accomplished !!!] \n\n\n"));
					return;
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,(" [Double check] MinMaxRSSI ((%d)) <= %d !! \n",
						pDM_FatTable->MinMaxRSSI,SWAntDiv_threshold));

					nextAnt = (pDM_FatTable->RxIdleAnt  == MAIN_ANT)? AUX_ANT : MAIN_ANT;
					pDM_SWAT_Table->try_flag = 0; 
					pDM_SWAT_Table->reset_idx=reset_period;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("[set try_flag=0]  Normal State:  Need to tryg again!! \n\n\n"));
					return;
				}
			}
			else
			{
				pDM_SWAT_Table->PreAntenna =RxIdleAnt;
				ODM_UpdateRxIdleAnt(pDM_Odm, RxIdleAnt );
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("[reset try_flag = 0] Training accomplished !!!] \n\n\n"));
			        return;
			}
			
		}

	}

	//1 4.Change TRX antenna

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("RSSI_Trying = (( %d )),    Ant: (( %s )) >>> (( %s )) \n",
		pDM_SWAT_Table->RSSI_Trying, (pDM_FatTable->RxIdleAnt  == MAIN_ANT?"MAIN":"AUX"),(nextAnt == MAIN_ANT?"MAIN":"AUX")));
		
	ODM_UpdateRxIdleAnt(pDM_Odm, nextAnt);

	//1 5.Reset Statistics

	pDM_FatTable->RxIdleAnt  = nextAnt;

	//1 6.Set next timer   (Trying State)
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV, ODM_DBG_LOUD, (" Test ((%s)) Ant for (( %d )) ms \n", (nextAnt == MAIN_ANT?"MAIN":"AUX"), pDM_SWAT_Table->Train_time));
	ODM_SetTimer(pDM_Odm,&pDM_SWAT_Table->SwAntennaSwitchTimer_8723B, pDM_SWAT_Table->Train_time ); //ms
}


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
ODM_SW_AntDiv_Callback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pSWAT_T			pDM_SWAT_Table = &pHalData->DM_OutSrc.DM_SWAT_Table;

	#if DEV_BUS_TYPE==RT_PCI_INTERFACE
		#if USE_WORKITEM
			ODM_ScheduleWorkItem(&pDM_SWAT_Table->SwAntennaSwitchWorkitem_8723B);
		#else
			{
			//DbgPrint("SW_antdiv_Callback");
			odm_S0S1_SwAntDiv(&pHalData->DM_OutSrc, SWAW_STEP_DETERMINE);
			}
		#endif
	#else
	ODM_ScheduleWorkItem(&pDM_SWAT_Table->SwAntennaSwitchWorkitem_8723B);
	#endif
}
VOID
ODM_SW_AntDiv_WorkitemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER		pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	//DbgPrint("SW_antdiv_Workitem_Callback");
	odm_S0S1_SwAntDiv(&pHalData->DM_OutSrc, SWAW_STEP_DETERMINE);
}
#endif  //#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
VOID
ODM_SW_AntDiv_Callback(void *FunctionContext)
{
	PDM_ODM_T	pDM_Odm= (PDM_ODM_T)FunctionContext;
	PADAPTER	padapter = pDM_Odm->Adapter;
	if(padapter->net_closed == _TRUE)
	    return;
	//odm_S0S1_SwAntDiv(pDM_Odm, SWAW_STEP_DETERMINE);	
}
#endif  //#if (DM_ODM_SUPPORT_TYPE == ODM_CE)

#endif //#if (RTL8723B_SUPPORT == 1)


#if(RTL8188E_SUPPORT == 1  || RTL8192E_SUPPORT == 1)
#if (!(DM_ODM_SUPPORT_TYPE == ODM_CE))
VOID
odm_SetNextMACAddrTarget(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	PSTA_INFO_T   	pEntry;
	//u1Byte	Bssid[6];
	u4Byte	value32, i;

	//
	//2012.03.26 LukeLee: The MAC address is changed according to MACID in turn
	//
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_SetNextMACAddrTarget() ==>\n"));
	if(pDM_Odm->bLinked)
	{
		for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			if((pDM_FatTable->TrainIdx+1) == ODM_ASSOCIATE_ENTRY_NUM)
				pDM_FatTable->TrainIdx = 0;
			else
				pDM_FatTable->TrainIdx++;
			
			pEntry = pDM_Odm->pODM_StaInfo[pDM_FatTable->TrainIdx];
			if(IS_STA_VALID(pEntry))
			{
				//Match MAC ADDR
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
				value32 = (pEntry->hwaddr[5]<<8)|pEntry->hwaddr[4];
#else
				value32 = (pEntry->MacAddr[5]<<8)|pEntry->MacAddr[4];
#endif
				ODM_SetMACReg(pDM_Odm, 0x7b4, 0xFFFF, value32);
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
				value32 = (pEntry->hwaddr[3]<<24)|(pEntry->hwaddr[2]<<16) |(pEntry->hwaddr[1]<<8) |pEntry->hwaddr[0];
#else
				value32 = (pEntry->MacAddr[3]<<24)|(pEntry->MacAddr[2]<<16) |(pEntry->MacAddr[1]<<8) |pEntry->MacAddr[0];
#endif
				ODM_SetMACReg(pDM_Odm, 0x7b0, bMaskDWord, value32);

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_FatTable->TrainIdx=%lu\n",pDM_FatTable->TrainIdx));
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP|ODM_ADSL))
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training MAC Addr = %x:%x:%x:%x:%x:%x\n",
					pEntry->hwaddr[5],pEntry->hwaddr[4],pEntry->hwaddr[3],pEntry->hwaddr[2],pEntry->hwaddr[1],pEntry->hwaddr[0]));
#else
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Training MAC Addr = %x:%x:%x:%x:%x:%x\n",
					pEntry->MacAddr[5],pEntry->MacAddr[4],pEntry->MacAddr[3],pEntry->MacAddr[2],pEntry->MacAddr[1],pEntry->MacAddr[0]));
#endif

				break;
			}
		}
		
	}

#if 0
	//
	//2012.03.26 LukeLee: This should be removed later, the MAC address is changed according to MACID in turn
	//
	#if( DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{		
		PADAPTER	Adapter =  pDM_Odm->Adapter;
		PMGNT_INFO	pMgntInfo = &Adapter->MgntInfo;

		for (i=0; i<6; i++)
		{
			Bssid[i] = pMgntInfo->Bssid[i];
			//DbgPrint("Bssid[%d]=%x\n", i, Bssid[i]);
		}
	}
	#endif

	//odm_SetNextMACAddrTarget(pDM_Odm);
	
	//1 Select MAC Address Filter
	for (i=0; i<6; i++)
	{
		if(Bssid[i] != pDM_FatTable->Bssid[i])
		{
			bMatchBSSID = FALSE;
			break;
		}
	}
	if(bMatchBSSID == FALSE)
	{
		//Match MAC ADDR
		value32 = (Bssid[5]<<8)|Bssid[4];
		ODM_SetMACReg(pDM_Odm, 0x7b4, 0xFFFF, value32);
		value32 = (Bssid[3]<<24)|(Bssid[2]<<16) |(Bssid[1]<<8) |Bssid[0];
		ODM_SetMACReg(pDM_Odm, 0x7b0, bMaskDWord, value32);
	}

	return bMatchBSSID;
#endif
				
}

VOID
odm_FastAntTraining(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	i, MaxRSSI=0;
	u1Byte	TargetAnt=2;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	BOOLEAN	bPktFilterMacth = FALSE;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("==>odm_FastAntTraining()\n"));

	//1 TRAINING STATE
	if(pDM_FatTable->FAT_State == FAT_TRAINING_STATE)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Enter FAT_TRAINING_STATE\n"));
		//2 Caculate RSSI per Antenna
		for (i=0; i<7; i++)
		{
			if(pDM_FatTable->antRSSIcnt[i] == 0)
				pDM_FatTable->antAveRSSI[i] = 0;
			else
			{
			pDM_FatTable->antAveRSSI[i] = pDM_FatTable->antSumRSSI[i] /pDM_FatTable->antRSSIcnt[i];
				bPktFilterMacth = TRUE;
			}
			if(pDM_FatTable->antAveRSSI[i] > MaxRSSI)
			{
				MaxRSSI = pDM_FatTable->antAveRSSI[i];
				TargetAnt = (u1Byte) i;
			}

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_FatTable->antAveRSSI[%lu] = %lu, pDM_FatTable->antRSSIcnt[%lu] = %lu\n",
				i, pDM_FatTable->antAveRSSI[i], i, pDM_FatTable->antRSSIcnt[i]));
		}

		//2 Select TRX Antenna
		if(bPktFilterMacth == FALSE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("None Packet is matched\n"));

			ODM_SetBBReg(pDM_Odm, 0xe08 , BIT16, 0);	//RegE08[16]=1'b0		//disable fast training
			ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 0);		//RegC50[7]=1'b0 		//disable HW AntDiv
		}
		else
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TargetAnt=%d, MaxRSSI=%lu\n",TargetAnt,MaxRSSI));

			ODM_SetBBReg(pDM_Odm, 0xe08 , BIT16, 0);	//RegE08[16]=1'b0		//disable fast training
			//ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 0);		//RegC50[7]=1'b0 		//disable HW AntDiv
			ODM_SetBBReg(pDM_Odm, 0x864 , BIT8|BIT7|BIT6, TargetAnt);	//Default RX is Omni, Optional RX is the best decision by FAT
			//ODM_SetBBReg(pDM_Odm, 0x860 , BIT14|BIT13|BIT12, TargetAnt);	//Default TX
			ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 1); //Reg80c[21]=1'b1		//from TX Info

#if 0
			pEntry = pDM_Odm->pODM_StaInfo[pDM_FatTable->TrainIdx];

			if(IS_STA_VALID(pEntry))
			{
				pEntry->antsel_a = TargetAnt&BIT0;
				pEntry->antsel_b = (TargetAnt&BIT1)>>1;
				pEntry->antsel_c = (TargetAnt&BIT2)>>2;
			}
#else
			pDM_FatTable->antsel_a[pDM_FatTable->TrainIdx] = TargetAnt&BIT0;
			pDM_FatTable->antsel_b[pDM_FatTable->TrainIdx] = (TargetAnt&BIT1)>>1;
			pDM_FatTable->antsel_c[pDM_FatTable->TrainIdx] = (TargetAnt&BIT2)>>2;
#endif


			if(TargetAnt == 0)
				ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 0);		//RegC50[7]=1'b0 		//disable HW AntDiv

		}

		//2 Reset Counter
		for(i=0; i<7; i++)
		{
			pDM_FatTable->antSumRSSI[i] = 0;
			pDM_FatTable->antRSSIcnt[i] = 0;
		}
		
		pDM_FatTable->FAT_State = FAT_NORMAL_STATE;
		return;
	}

	//1 NORMAL STATE
	if(pDM_FatTable->FAT_State == FAT_NORMAL_STATE)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Enter FAT_NORMAL_STATE\n"));

		odm_SetNextMACAddrTarget(pDM_Odm);

#if 0
				pEntry = pDM_Odm->pODM_StaInfo[pDM_FatTable->TrainIdx];
				if(IS_STA_VALID(pEntry))
				{
					pEntry->antsel_a = TargetAnt&BIT0;
					pEntry->antsel_b = (TargetAnt&BIT1)>>1;
					pEntry->antsel_c = (TargetAnt&BIT2)>>2;
				}
#endif

		//2 Prepare Training
		pDM_FatTable->FAT_State = FAT_TRAINING_STATE;
		ODM_SetBBReg(pDM_Odm, 0xe08 , BIT16, 1);	//RegE08[16]=1'b1		//enable fast training
		ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 1);	//RegC50[7]=1'b1 		//enable HW AntDiv
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Start FAT_TRAINING_STATE\n"));
		ODM_SetTimer(pDM_Odm,&pDM_Odm->FastAntTrainingTimer, 500 ); //ms
		
	}
		
}

VOID
odm_FastAntTrainingCallback(
	IN		PDM_ODM_T		pDM_Odm
)
{

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	PADAPTER	padapter = pDM_Odm->Adapter;
	if(padapter->net_closed == _TRUE)
	    return;
	//if(*pDM_Odm->pbNet_closed == TRUE)
	   // return;
#endif

#if USE_WORKITEM
	ODM_ScheduleWorkItem(&pDM_Odm->FastAntTrainingWorkitem);
#else
	odm_FastAntTraining(pDM_Odm);
#endif
}

VOID
odm_FastAntTrainingWorkItemCallback(
	IN		PDM_ODM_T		pDM_Odm
)
{
	odm_FastAntTraining(pDM_Odm);
}
#endif

#endif


VOID
ODM_AntDivInit(
	IN PDM_ODM_T	pDM_Odm 
	)
{
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;


	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!]   Not Support Antenna Diversity Function\n"));
		return;
	}
        //---
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if(pDM_FatTable->AntDiv_2G_5G == ODM_ANTDIV_2G)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[2G AntDiv Init]: Only Support 2G Antenna Diversity Function\n"));
		if(!(pDM_Odm->SupportICType & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	}
	else 	if(pDM_FatTable->AntDiv_2G_5G == ODM_ANTDIV_5G)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[5G AntDiv Init]: Only Support 5G Antenna Diversity Function\n"));
		if(!(pDM_Odm->SupportICType & ODM_ANTDIV_5G_SUPPORT_IC))
			return;
	}
	else 	if(pDM_FatTable->AntDiv_2G_5G == (ODM_ANTDIV_2G|ODM_ANTDIV_5G))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[2G & 5G AntDiv Init]:Support Both 2G & 5G Antenna Diversity Function\n"));
	}

	 pDM_Odm->antdiv_rssi=0;

#endif	
	//---
	
	//2 [--General---]
	pDM_Odm->antdiv_period=0;
	pDM_Odm->antdiv_select=0;
	pDM_SWAT_Table->Ant5G = MAIN_ANT;
	pDM_SWAT_Table->Ant2G = MAIN_ANT;
	pDM_FatTable->CCK_counter_main=0;
	pDM_FatTable->CCK_counter_aux=0;
	pDM_FatTable->OFDM_counter_main=0;
	pDM_FatTable->OFDM_counter_aux=0;
	
	//3 [Set MAIN_ANT as default antenna if Auto-Ant enable]
	if (pDM_Odm->antdiv_select==1)
		pDM_Odm->AntType = ODM_FIX_MAIN_ANT;
	else if (pDM_Odm->antdiv_select==2)
		pDM_Odm->AntType = ODM_FIX_AUX_ANT;
	else if(pDM_Odm->antdiv_select==0)
		pDM_Odm->AntType = ODM_AUTO_ANT;
	
	if(pDM_Odm->AntType == ODM_AUTO_ANT)
	{
		odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
		ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
	}
	else
	{
		odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
		
		if(pDM_Odm->AntType == ODM_FIX_MAIN_ANT)
		{
			ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			return;
		}
		else if(pDM_Odm->AntType == ODM_FIX_AUX_ANT)
		{
			ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
			return;
		}
	}
	//---
	if(pDM_Odm->AntDivType != CGCS_RX_HW_ANTDIV)
	{
		if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
		{
			#if TX_BY_REG
			ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 0); //Reg80c[21]=1'b0		//from Reg
			#else
			ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 1);
			#endif
		}	
		else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
		{
			#if TX_BY_REG
			ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 0); 
			#else
			ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 1); 
			#endif
		}
	}
		
	//2 [--88E---]
	if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
	#if (RTL8188E_SUPPORT == 1)
		//pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
		//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
		//pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;

		if( (pDM_Odm->AntDivType != CGCS_RX_HW_ANTDIV)  && (pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV) && (pDM_Odm->AntDivType != CG_TRX_SMART_ANTDIV))
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!]  88E Not Supprrt This AntDiv Type\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
			return;
		}
		
		if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
			odm_RX_HWAntDiv_Init_88E(pDM_Odm);
		else if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
			odm_TRX_HWAntDiv_Init_88E(pDM_Odm);
		else if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
			odm_Smart_HWAntDiv_Init_88E(pDM_Odm);
	#endif	
	}
	
	//2 [--92E---]
	#if (RTL8192E_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{	
		//pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
		//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
		//pDM_Odm->AntDivType = CG_TRX_SMART_ANTDIV;

		if( (pDM_Odm->AntDivType != CGCS_RX_HW_ANTDIV) && (pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV)   && (pDM_Odm->AntDivType != CG_TRX_SMART_ANTDIV))
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!]  8192E Not Supprrt This AntDiv Type\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
			return;
		}
		
		if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
			odm_RX_HWAntDiv_Init_92E(pDM_Odm);
		else if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
			odm_TRX_HWAntDiv_Init_92E(pDM_Odm);
		else if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
			odm_Smart_HWAntDiv_Init_92E(pDM_Odm);
	
	}
	#endif	
	
	//2 [--8723B---]
	#if (RTL8723B_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{		
		//pDM_Odm->AntDivType = S0S1_SW_ANTDIV;
		//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;

		if(pDM_Odm->AntDivType != S0S1_SW_ANTDIV && pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!] 8723B  Not Supprrt This AntDiv Type\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
			return;
		}
			
		if( pDM_Odm->AntDivType==S0S1_SW_ANTDIV)
			odm_S0S1_SWAntDiv_Init_8723B(pDM_Odm);
		else if(pDM_Odm->AntDivType==CG_TRX_HW_ANTDIV)
			odm_TRX_HWAntDiv_Init_8723B(pDM_Odm);		
	}
	#endif
	
	//2 [--8811A 8821A---]
	#if (RTL8821A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
		//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
		pDM_Odm->AntDivType = S0S1_SW_ANTDIV;
			
		if( pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV && pDM_Odm->AntDivType != S0S1_SW_ANTDIV)
		{
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!] 8821A & 8811A  Not Supprrt This AntDiv Type\n"));
			pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
			return;
		}
		if(pDM_Odm->AntDivType==CG_TRX_HW_ANTDIV)	
			odm_TRX_HWAntDiv_Init_8821A(pDM_Odm);
		else if( pDM_Odm->AntDivType==S0S1_SW_ANTDIV)
			odm_S0S1_SWAntDiv_Init_8821A(pDM_Odm);
	}
	#endif
	
	//2 [--8881A---]
	#if (RTL8881A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8881A)
	{
			//pDM_Odm->AntDivType = CGCS_RX_HW_ANTDIV;
			//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
			
			if(pDM_Odm->AntDivType != CGCS_RX_HW_ANTDIV && pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV)
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!] 8881A  Not Supprrt This AntDiv Type\n"));
				pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
				return;
			}
			if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
				odm_RX_HWAntDiv_Init_8881A(pDM_Odm);
			else if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
				odm_TRX_HWAntDiv_Init_8881A(pDM_Odm);	
	}
	#endif
	
	//2 [--8812---]
	#if (RTL8812A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8812)
	{	
			//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
			
			if( pDM_Odm->AntDivType != CG_TRX_HW_ANTDIV)
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!] 8812A  Not Supprrt This AntDiv Type\n"));
				pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
				return;
			}
			odm_TRX_HWAntDiv_Init_8812A(pDM_Odm);
	}
	#endif
	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("*** SupportICType=[%lu] \n",pDM_Odm->SupportICType));
	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("*** AntDiv SupportAbility=[%lu] \n",(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)>>6));
	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("*** AntDiv Type=[%d] \n",pDM_Odm->AntDivType));

}

VOID
ODM_AntDiv(
	IN		PDM_ODM_T		pDM_Odm
)
{	
	PADAPTER		pAdapter	= pDM_Odm->Adapter;
	pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;

//#if (DM_ODM_SUPPORT_TYPE == ODM_AP)	
	if(*pDM_Odm->pBandType == ODM_BAND_5G )
	{
		if(pDM_FatTable->idx_AntDiv_counter_5G <  pDM_Odm->antdiv_period )
		{
			pDM_FatTable->idx_AntDiv_counter_5G++;
			return;
		}
		else
			pDM_FatTable->idx_AntDiv_counter_5G=0;
	}
	else 	if(*pDM_Odm->pBandType == ODM_BAND_2_4G )
	{
		if(pDM_FatTable->idx_AntDiv_counter_2G <  pDM_Odm->antdiv_period )
		{
			pDM_FatTable->idx_AntDiv_counter_2G++;
			return;
		}
		else
			pDM_FatTable->idx_AntDiv_counter_2G=0;
	}
//#endif	
	//----------
	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[Return!!!]   Not Support Antenna Diversity Function\n"));
		return;
	}

	//----------
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if(pAdapter->MgntInfo.AntennaTest)
		return;
	
        {
	#if (BEAMFORMING_SUPPORT == 1)			
	        BEAMFORMING_CAP		BeamformCap = (pAdapter->MgntInfo.BeamformingInfo.BeamformCap);

		if( BeamformCap & BEAMFORMEE_CAP ) //  BFmee On  &&   Div On ->  Div Off
		{	
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ AntDiv : OFF ]   BFmee ==1 \n"));
			if(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)
			{
				odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);
				pDM_Odm->SupportAbility &= ~(ODM_BB_ANT_DIV);
				return;
			}
		}
		else // BFmee Off   &&   Div Off ->  Div On
	#endif
		{
			if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)  &&  pDM_Odm->bLinked) 
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ AntDiv : ON ]   BFmee ==0 \n"));
				if((pDM_Odm->AntDivType!=S0S1_SW_ANTDIV) )
					odm_AntDiv_on_off(pDM_Odm, ANTDIV_ON);
				
				pDM_Odm->SupportAbility |= (ODM_BB_ANT_DIV);
			}
		}
        }
#endif

	//----------
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	if(pDM_FatTable->AntDiv_2G_5G == ODM_ANTDIV_2G)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 2G AntDiv Running ]\n"));
		if(!(pDM_Odm->SupportICType & ODM_ANTDIV_2G_SUPPORT_IC))
			return;
	}
	else if(pDM_FatTable->AntDiv_2G_5G == ODM_ANTDIV_5G)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 5G AntDiv Running ]\n"));
		if(!(pDM_Odm->SupportICType & ODM_ANTDIV_5G_SUPPORT_IC))
		return;
	}
	else if(pDM_FatTable->AntDiv_2G_5G == (ODM_ANTDIV_2G|ODM_ANTDIV_5G))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("[ 2G & 5G AntDiv Running ]\n"));
	}
#endif

	//----------

	if (pDM_Odm->antdiv_select==1)
		pDM_Odm->AntType = ODM_FIX_MAIN_ANT;
	else if (pDM_Odm->antdiv_select==2)
		pDM_Odm->AntType = ODM_FIX_AUX_ANT;
	else  if (pDM_Odm->antdiv_select==0)
		pDM_Odm->AntType = ODM_AUTO_ANT;

	//ODM_RT_TRACE(pDM_Odm, ODM_COMP_ANT_DIV,ODM_DBG_LOUD,("AntType= (( %d )) , pre_AntType= (( %d ))  \n",pDM_Odm->AntType,pDM_Odm->pre_AntType));
	
	if(pDM_Odm->AntType != ODM_AUTO_ANT)
	{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Fix Antenna at (( %s ))\n",(pDM_Odm->AntType == ODM_FIX_MAIN_ANT)?"MAIN":"AUX"));
			
		if(pDM_Odm->AntType != pDM_Odm->pre_AntType)
		{
			odm_AntDiv_on_off(pDM_Odm, ANTDIV_OFF);

			if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
				ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 0);
			else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
				ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 0); 
						
			if(pDM_Odm->AntType == ODM_FIX_MAIN_ANT)
				ODM_UpdateRxIdleAnt(pDM_Odm, MAIN_ANT);
			else if(pDM_Odm->AntType == ODM_FIX_AUX_ANT)
				ODM_UpdateRxIdleAnt(pDM_Odm, AUX_ANT);
		}
		pDM_Odm->pre_AntType=pDM_Odm->AntType; 
		return;
	}
	else
	{
		if(pDM_Odm->AntType != pDM_Odm->pre_AntType)
		{
			odm_AntDiv_on_off(pDM_Odm, ANTDIV_ON);
			 if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
				ODM_SetBBReg(pDM_Odm, 0x80c , BIT21, 1);
			else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
				ODM_SetBBReg(pDM_Odm, 0x900 , BIT18, 1); 
		}
		pDM_Odm->pre_AntType=pDM_Odm->AntType;
	}
	 
	
	//3 -----------------------------------------------------------------------------------------------------------
	//2 [--88E---]
	if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
		#if (RTL8188E_SUPPORT == 1)
		if(pDM_Odm->AntDivType==CG_TRX_HW_ANTDIV ||pDM_Odm->AntDivType==CGCS_RX_HW_ANTDIV)
			odm_HW_AntDiv(pDM_Odm);
		#if (!(DM_ODM_SUPPORT_TYPE == ODM_CE))
		else if (pDM_Odm->AntDivType==CG_TRX_SMART_ANTDIV)
			odm_FastAntTraining(pDM_Odm);	
		#endif
		#endif
	}
	//2 [--92E---]	
	#if (RTL8192E_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{
		if(pDM_Odm->AntDivType==CGCS_RX_HW_ANTDIV)
			odm_HW_AntDiv(pDM_Odm);
		#if (!(DM_ODM_SUPPORT_TYPE == ODM_CE))
		else if (pDM_Odm->AntDivType==CG_TRX_SMART_ANTDIV)
			odm_FastAntTraining(pDM_Odm);	
		#endif
	}
	#endif

	#if (RTL8723B_SUPPORT == 1)	
	//2 [--8723B---]
	else if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
		if (pDM_Odm->AntDivType==S0S1_SW_ANTDIV)
			odm_S0S1_SwAntDiv(pDM_Odm, SWAW_STEP_PEAK);
		else if (pDM_Odm->AntDivType==CG_TRX_HW_ANTDIV)
			odm_HW_AntDiv(pDM_Odm);
	}
	#endif
	
	//2 [--8821A---]
	#if (RTL8821A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
		if(pDM_Odm->bBtDisabled)  //BT disabled
		{
			if(pDM_Odm->AntDivType == S0S1_SW_ANTDIV)
			{
			pDM_Odm->AntDivType=CG_TRX_HW_ANTDIV;
			ODM_SetBBReg(pDM_Odm, 0x8D4 , BIT24, 1); 
			}
		}	
		else //BT enabled
		{
			if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
			{
			pDM_Odm->AntDivType=S0S1_SW_ANTDIV;
			ODM_SetBBReg(pDM_Odm, 0x8D4 , BIT24, 0); 
			}	
		}	
	
		if (pDM_Odm->AntDivType==S0S1_SW_ANTDIV)
			odm_S0S1_SwAntDiv(pDM_Odm, SWAW_STEP_PEAK);
		else if (pDM_Odm->AntDivType==CG_TRX_HW_ANTDIV)
		odm_HW_AntDiv(pDM_Odm);
	}
	#endif
	//2 [--8881A---]
	#if (RTL8881A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8881A)		
		odm_HW_AntDiv(pDM_Odm);
	#endif
	//2 [--8812A---]
	#if (RTL8812A_SUPPORT == 1)
	else if(pDM_Odm->SupportICType == ODM_RTL8812)
		odm_HW_AntDiv(pDM_Odm);
	#endif
}


VOID
odm_AntselStatistics(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			antsel_tr_mux,
	IN		u4Byte			MacId,
	IN		u4Byte			RxPWDBAll
)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;

	if(antsel_tr_mux == ANT1_2G)
	{
		pDM_FatTable->MainAnt_Sum[MacId]+=RxPWDBAll;
		pDM_FatTable->MainAnt_Cnt[MacId]++;
	}
	else
	{
		pDM_FatTable->AuxAnt_Sum[MacId]+=RxPWDBAll;
		pDM_FatTable->AuxAnt_Cnt[MacId]++;
	}
}


VOID
ODM_Process_RSSIForAntDiv(	
	IN OUT	PDM_ODM_T					pDM_Odm,
	IN		PODM_PHY_INFO_T				pPhyInfo,
	IN		PODM_PACKET_INFO_T			pPktinfo
	)
{
u1Byte			isCCKrate=0,CCKMaxRate=DESC_RATE11M;
pFAT_T			pDM_FatTable = &pDM_Odm->DM_FatTable;

#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN))
	u4Byte			RxPower_Ant0, RxPower_Ant1;	
#else
	u1Byte			RxPower_Ant0, RxPower_Ant1;	
#endif

	if(pDM_Odm->SupportICType & ODM_N_ANTDIV_SUPPORT)
		CCKMaxRate=DESC_RATE11M;
	else if(pDM_Odm->SupportICType & ODM_AC_ANTDIV_SUPPORT)
		CCKMaxRate=DESC_RATE11M;
	isCCKrate = (pPktinfo->DataRate <= CCKMaxRate)?TRUE:FALSE;

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
		
	if(  (pDM_Odm->SupportICType == ODM_RTL8192E||pDM_Odm->SupportICType == ODM_RTL8812)   && (pPktinfo->DataRate > CCKMaxRate) )
	{
		RxPower_Ant0 = pPhyInfo->RxMIMOSignalStrength[0];
		RxPower_Ant1= pPhyInfo->RxMIMOSignalStrength[1];
	}
	else
		RxPower_Ant0=pPhyInfo->RxPWDBAll;
	
	if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
	{
		if( (pDM_Odm->SupportICType & ODM_SMART_ANT_SUPPORT) &&  pPktinfo->bPacketToSelf   && pDM_FatTable->FAT_State == FAT_TRAINING_STATE )//(pPktinfo->bPacketMatchBSSID && (!pPktinfo->bPacketBeacon))
		{
			u1Byte	antsel_tr_mux;
			antsel_tr_mux = (pDM_FatTable->antsel_rx_keep_2<<2) |(pDM_FatTable->antsel_rx_keep_1 <<1) |pDM_FatTable->antsel_rx_keep_0;
			pDM_FatTable->antSumRSSI[antsel_tr_mux] += RxPower_Ant0;
			pDM_FatTable->antRSSIcnt[antsel_tr_mux]++;
		}
	}
	else //AntDivType != CG_TRX_SMART_ANTDIV 
	{
		if(  ( pDM_Odm->SupportICType & ODM_ANTDIV_SUPPORT ) &&  (pPktinfo->bPacketToSelf || pPktinfo->bPacketMatchBSSID)  )
		{
			 if(pDM_Odm->SupportICType == ODM_RTL8188E || pDM_Odm->SupportICType == ODM_RTL8192E)
				odm_AntselStatistics(pDM_Odm, pDM_FatTable->antsel_rx_keep_0, pPktinfo->StationID,RxPower_Ant0);
			else// SupportICType == ODM_RTL8821 and ODM_RTL8723B and ODM_RTL8812)
			{
				if(isCCKrate && (pDM_Odm->AntDivType == S0S1_SW_ANTDIV))
				{
				 	pDM_FatTable->antsel_rx_keep_0 = (pDM_FatTable->RxIdleAnt == MAIN_ANT) ? ANT1_2G : ANT2_2G;


						if(pDM_FatTable->antsel_rx_keep_0==ANT1_2G)
							pDM_FatTable->CCK_counter_main++;
						else// if(pDM_FatTable->antsel_rx_keep_0==ANT2_2G)
							pDM_FatTable->CCK_counter_aux++;

					odm_AntselStatistics(pDM_Odm, pDM_FatTable->antsel_rx_keep_0, pPktinfo->StationID, RxPower_Ant0);
				}
				else
				{

					if(pDM_FatTable->antsel_rx_keep_0==ANT1_2G)
						pDM_FatTable->OFDM_counter_main++;
					else// if(pDM_FatTable->antsel_rx_keep_0==ANT2_2G)
						pDM_FatTable->OFDM_counter_aux++;
					odm_AntselStatistics(pDM_Odm, pDM_FatTable->antsel_rx_keep_0, pPktinfo->StationID, RxPower_Ant0);
			}
		}
	}
	}
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("isCCKrate=%d, PWDB_ALL=%d\n",isCCKrate, pPhyInfo->RxPWDBAll));
	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=3'b%d%d%d\n",pDM_FatTable->antsel_rx_keep_2, pDM_FatTable->antsel_rx_keep_1, pDM_FatTable->antsel_rx_keep_0));
}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
VOID
ODM_SetTxAntByTxInfo(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;

	if(!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV))
		return;

	if(pDM_Odm->AntDivType==CGCS_RX_HW_ANTDIV)
		return;


	if(pDM_Odm->SupportICType == ODM_RTL8723B)
	{
#if (RTL8723B_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8723B(pDesc, pDM_FatTable->antsel_a[macId]);
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8723B] SetTxAntByTxInfo_WIN: MacID=%d, antsel_tr_mux=3'b%d%d%d\n", 
			//macId, pDM_FatTable->antsel_c[macId], pDM_FatTable->antsel_b[macId], pDM_FatTable->antsel_a[macId]));
#endif
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8821)
	{
#if (RTL8821A_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_8812(pDesc, pDM_FatTable->antsel_a[macId]);
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8821A] SetTxAntByTxInfo_WIN: MacID=%d, antsel_tr_mux=3'b%d%d%d\n", 
			//macId, pDM_FatTable->antsel_c[macId], pDM_FatTable->antsel_b[macId], pDM_FatTable->antsel_a[macId]));
#endif
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8188E)
	{
#if (RTL8188E_SUPPORT == 1)
		SET_TX_DESC_ANTSEL_A_88E(pDesc, pDM_FatTable->antsel_a[macId]);
		SET_TX_DESC_ANTSEL_B_88E(pDesc, pDM_FatTable->antsel_b[macId]);
		SET_TX_DESC_ANTSEL_C_88E(pDesc, pDM_FatTable->antsel_c[macId]);
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("[8188E] SetTxAntByTxInfo_WIN: MacID=%d, antsel_tr_mux=3'b%d%d%d\n", 
			//macId, pDM_FatTable->antsel_c[macId], pDM_FatTable->antsel_b[macId], pDM_FatTable->antsel_a[macId]));
#endif
	}
	else if(pDM_Odm->SupportICType == ODM_RTL8192E)
	{

	
	}
}
#else// (DM_ODM_SUPPORT_TYPE == ODM_AP)

VOID
ODM_SetTxAntByTxInfo(
	//IN		PDM_ODM_T		pDM_Odm,
	struct	rtl8192cd_priv		*priv,
	struct 	tx_desc			*pdesc,
	struct	tx_insn			*txcfg,
	unsigned short			aid	
)
{
	pFAT_T		pDM_FatTable = &priv->pshare->_dmODM.DM_FatTable;
	u4Byte		SupportICType=priv->pshare->_dmODM.SupportICType;

	if(SupportICType == ODM_RTL8881A)
	{
		//panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******   \n",__FUNCTION__,__LINE__);	
		pdesc->Dword6 &= set_desc(~ (BIT(18)|BIT(17)|BIT(16)));	
		pdesc->Dword6 |= set_desc(pDM_FatTable->antsel_a[aid]<<16);
	}
	else if(SupportICType == ODM_RTL8192E)
	{
		//panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8192E******   \n",__FUNCTION__,__LINE__);	
		pdesc->Dword6 &= set_desc(~ (BIT(18)|BIT(17)|BIT(16)));	
		pdesc->Dword6 |= set_desc(pDM_FatTable->antsel_a[aid]<<16);
	}
	else if(SupportICType == ODM_RTL8812)
	{
		//3 [path-A]
		//panic_printk("[%s] [%d]   ******ODM_SetTxAntByTxInfo_8881E******   \n",__FUNCTION__,__LINE__);
			
		pdesc->Dword6 &= set_desc(~ BIT(16));
		pdesc->Dword6 &= set_desc(~ BIT(17));
		pdesc->Dword6 &= set_desc(~ BIT(18));
		if(txcfg->pstat)
		{
			pdesc->Dword6 |= set_desc(pDM_FatTable->antsel_a[aid]<<16);
			pdesc->Dword6 |= set_desc(pDM_FatTable->antsel_b[aid]<<17);
			pdesc->Dword6 |= set_desc(pDM_FatTable->antsel_c[aid]<<18);
		}
	}
}
#endif

#else

VOID ODM_AntDivInit(	IN PDM_ODM_T	pDM_Odm ){}
VOID ODM_AntDiv(	IN PDM_ODM_T		pDM_Odm){}

#endif //#if (defined(CONFIG_HW_ANTENNA_DIVERSITY))


