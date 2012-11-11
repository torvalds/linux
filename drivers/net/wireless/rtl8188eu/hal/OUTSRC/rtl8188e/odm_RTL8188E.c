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

#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
VOID
odm_RX_HWAntDivInit(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32;

        #if (MP_DRIVER == 1)
        pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
        ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 0); // disable HW AntDiv 
        ODM_SetBBReg(pDM_Odm, ODM_REG_LNA_SWITCH_11N , BIT31, 1);  // 1:CG, 0:CS
        return;
        #endif
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_RX_HWAntDivInit() \n"));
	
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
	ODM_UpdateRxIdleAnt_88E(pDM_Odm, MAIN_ANT);
	ODM_SetBBReg(pDM_Odm, ODM_REG_ANT_MAPPING1_11N , 0xFFFF, 0x0201);	//antenna mapping table
	
	//ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 1);	//Enable HW AntDiv
	//ODM_SetBBReg(pDM_Odm, 0xa00 , BIT15, 1); //Enable CCK AntDiv
}

VOID
odm_TRX_HWAntDivInit(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32;
	
        #if (MP_DRIVER == 1)
        pDM_Odm->AntDivType = CGCS_RX_SW_ANTDIV;
        ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 0); // disable HW AntDiv 
        ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT5|BIT4|BIT3, 0); //Default RX   (0/1)
        return;
        #endif

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_TRX_HWAntDivInit() \n"));
	
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
	//Tx Settings
	ODM_SetBBReg(pDM_Odm, ODM_REG_TX_ANT_CTRL_11N , BIT21, 0); //Reg80c[21]=1'b0		//from TX Reg
	ODM_UpdateRxIdleAnt_88E(pDM_Odm, MAIN_ANT);

	//antenna mapping table
	if(!pDM_Odm->bIsMPChip) //testchip
	{
		ODM_SetBBReg(pDM_Odm, ODM_REG_RX_DEFUALT_A_11N , BIT10|BIT9|BIT8, 1);	//Reg858[10:8]=3'b001
		ODM_SetBBReg(pDM_Odm, ODM_REG_RX_DEFUALT_A_11N , BIT13|BIT12|BIT11, 2);	//Reg858[13:11]=3'b010
	}
	else //MPchip
		ODM_SetBBReg(pDM_Odm, ODM_REG_ANT_MAPPING1_11N , bMaskDWord, 0x0201);	//Reg914=3'b010, Reg915=3'b001

	//ODM_SetBBReg(pDM_Odm, 0xc50 , BIT7, 1);	//Enable HW AntDiv
	//ODM_SetBBReg(pDM_Odm, 0xa00 , BIT15, 1); //Enable CCK AntDiv
}

VOID
odm_FastAntTrainingInit(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	value32, i;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u4Byte	AntCombination = 2;

    ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("odm_FastAntTrainingInit() \n"));
    
#if (MP_DRIVER == 1)
    ODM_RT_TRACE(pDM_Odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("pDM_Odm->AntDivType: %d\n", pDM_Odm->AntDivType));
    return;
#endif

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
}

VOID
ODM_AntennaDiversityInit_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
/*
	//2012.03.27 LukeLee: For temp use, should be removed later
	//pDM_Odm->AntDivType = CG_TRX_HW_ANTDIV;
	//{
		PADAPTER		Adapter = pDM_Odm->Adapter;
		HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(Adapter);
		//pHalData->AntDivCfg = 1;
	//}
*/
	if(pDM_Odm->SupportICType != ODM_RTL8188E)
		return;

	//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_Odm->AntDivType=%d, pHalData->AntDivCfg=%d\n",
	//	pDM_Odm->AntDivType, pHalData->AntDivCfg));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_Odm->AntDivType=%d\n",pDM_Odm->AntDivType));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_Odm->bIsMPChip=%s\n",(pDM_Odm->bIsMPChip?"TRUE":"FALSE")));

	if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
		odm_RX_HWAntDivInit(pDM_Odm);
	else if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
		odm_TRX_HWAntDivInit(pDM_Odm);
	else if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
		odm_FastAntTrainingInit(pDM_Odm);
}


VOID
ODM_UpdateRxIdleAnt_88E(IN PDM_ODM_T pDM_Odm, IN u1Byte Ant)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u4Byte	DefaultAnt, OptionalAnt;

	if(pDM_FatTable->RxIdleAnt != Ant)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Need to Update Rx Idle Ant\n"));
		if(Ant == MAIN_ANT)
		{
			DefaultAnt = (pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)?MAIN_ANT_CG_TRX:MAIN_ANT_CGCS_RX;
			OptionalAnt = (pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)?AUX_ANT_CG_TRX:AUX_ANT_CGCS_RX;
		}
		else
		{
			DefaultAnt = (pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)?AUX_ANT_CG_TRX:AUX_ANT_CGCS_RX;
			OptionalAnt = (pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)?MAIN_ANT_CG_TRX:MAIN_ANT_CGCS_RX;
		}

		if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
		{
			ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT5|BIT4|BIT3, DefaultAnt);	//Default RX
			ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT8|BIT7|BIT6, OptionalAnt);		//Optional RX
			ODM_SetBBReg(pDM_Odm, ODM_REG_ANTSEL_CTRL_11N , BIT14|BIT13|BIT12, DefaultAnt);	//Default TX
			ODM_SetMACReg(pDM_Odm, ODM_REG_RESP_TX_11N , BIT6|BIT7, DefaultAnt);	//Resp Tx
			
		}
		else if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
		{
			ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT5|BIT4|BIT3, DefaultAnt);	//Default RX
			ODM_SetBBReg(pDM_Odm, ODM_REG_RX_ANT_CTRL_11N , BIT8|BIT7|BIT6, OptionalAnt);		//Optional RX
		}
	}
	pDM_FatTable->RxIdleAnt = Ant;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("RxIdleAnt=%s\n",(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
}


VOID
odm_UpdateTxAnt_88E(IN PDM_ODM_T pDM_Odm, IN u1Byte Ant, IN u4Byte MacId)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	u1Byte	TargetAnt;

	if(Ant == MAIN_ANT)
		TargetAnt = MAIN_ANT_CG_TRX;
	else
		TargetAnt = AUX_ANT_CG_TRX;
	
	pDM_FatTable->antsel_a[MacId] = TargetAnt&BIT0;
	pDM_FatTable->antsel_b[MacId] = (TargetAnt&BIT1)>>1;
	pDM_FatTable->antsel_c[MacId] = (TargetAnt&BIT2)>>2;

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Tx from TxInfo, TargetAnt=%s\n", 
						(Ant==MAIN_ANT)?"MAIN_ANT":"AUX_ANT"));
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("antsel_tr_mux=3'b%d%d%d\n",
							pDM_FatTable->antsel_c[MacId] , pDM_FatTable->antsel_b[MacId] , pDM_FatTable->antsel_a[MacId] ));
}

#if (DM_ODM_SUPPORT_TYPE  & (ODM_MP|ODM_CE))
VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
	)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;

	if((pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)||(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV))
	{
		SET_TX_DESC_ANTSEL_A_88E(pDesc, pDM_FatTable->antsel_a[macId]);
		SET_TX_DESC_ANTSEL_B_88E(pDesc, pDM_FatTable->antsel_b[macId]);
		SET_TX_DESC_ANTSEL_C_88E(pDesc, pDM_FatTable->antsel_c[macId]);
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_SetTxAntByTxInfo_88E_WIN(): MacID=%d, antsel_tr_mux=3'b%d%d%d\n", 
		//	macId, pDM_FatTable->antsel_c[macId], pDM_FatTable->antsel_b[macId], pDM_FatTable->antsel_a[macId]));
	}
}
#else// (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm
		)
{
}
#endif

VOID
ODM_AntselStatistics_88E(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			antsel_tr_mux,
	IN		u4Byte			MacId,
	IN		u1Byte			RxPWDBAll
)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
	{
		if(antsel_tr_mux == MAIN_ANT_CG_TRX)
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
	else if(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV)
	{
		if(antsel_tr_mux == MAIN_ANT_CGCS_RX)
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
}

#define	TX_BY_REG	0
VOID
odm_HWAntDiv(
	IN		PDM_ODM_T		pDM_Odm
)
{
	u4Byte	i, MinRSSI=0xFF, AntDivMaxRSSI=0, MaxRSSI=0, LocalMinRSSI, LocalMaxRSSI;
	u4Byte	Main_RSSI, Aux_RSSI;
	u1Byte	RxIdleAnt=0, TargetAnt=7;
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	pDIG_T	pDM_DigTable = &pDM_Odm->DM_DigTable;
	BOOLEAN	bMatchBSSID;
	BOOLEAN	bPktFilterMacth = FALSE;
	PSTA_INFO_T   	pEntry;

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
		{
			//2 Caculate RSSI per Antenna
			Main_RSSI = (pDM_FatTable->MainAnt_Cnt[i]!=0)?(pDM_FatTable->MainAnt_Sum[i]/pDM_FatTable->MainAnt_Cnt[i]):0;
			Aux_RSSI = (pDM_FatTable->AuxAnt_Cnt[i]!=0)?(pDM_FatTable->AuxAnt_Sum[i]/pDM_FatTable->AuxAnt_Cnt[i]):0;
			TargetAnt = (Main_RSSI>=Aux_RSSI)?MAIN_ANT:AUX_ANT;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MacID=%d, MainAnt_Sum=%d, MainAnt_Cnt=%d\n", i, pDM_FatTable->MainAnt_Sum[i], pDM_FatTable->MainAnt_Cnt[i]));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MacID=%d, AuxAnt_Sum=%d, AuxAnt_Cnt=%d\n",i, pDM_FatTable->AuxAnt_Sum[i], pDM_FatTable->AuxAnt_Cnt[i]));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("MacID=%d, Main_RSSI= %d, Aux_RSSI= %d\n", i, Main_RSSI, Aux_RSSI));

			//2 Select MaxRSSI for DIG
			LocalMaxRSSI = (Main_RSSI>Aux_RSSI)?Main_RSSI:Aux_RSSI;
			if((LocalMaxRSSI > AntDivMaxRSSI) && (LocalMaxRSSI < 40))
				AntDivMaxRSSI = LocalMaxRSSI;
			if(LocalMaxRSSI > MaxRSSI)
				MaxRSSI = LocalMaxRSSI;

			//2 Select RX Idle Antenna
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
#if TX_BY_REG
			
#else
			//2 Select TRX Antenna
			if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
				odm_UpdateTxAnt_88E(pDM_Odm, TargetAnt, i);
#endif			
		}
		pDM_FatTable->MainAnt_Sum[i] = 0;
		pDM_FatTable->AuxAnt_Sum[i] = 0;
		pDM_FatTable->MainAnt_Cnt[i] = 0;
		pDM_FatTable->AuxAnt_Cnt[i] = 0;
	}

	//2 Set RX Idle Antenna
	ODM_UpdateRxIdleAnt_88E(pDM_Odm, RxIdleAnt);

	pDM_DigTable->AntDiv_RSSI_max = AntDivMaxRSSI;
	pDM_DigTable->RSSI_max = MaxRSSI;
}


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

				ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_FatTable->TrainIdx=%d\n",pDM_FatTable->TrainIdx));
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
	#if( DM_ODM_SUPPORT_TYPE & ODM_MP)
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
	PSTA_INFO_T   	pEntry;


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

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("pDM_FatTable->antAveRSSI[%d] = %d, pDM_FatTable->antRSSIcnt[%d] = %d\n",
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
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("TargetAnt=%d, MaxRSSI=%d\n",TargetAnt,MaxRSSI));

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

VOID
ODM_AntennaDiversity_88E(
	IN		PDM_ODM_T		pDM_Odm
)
{
	pFAT_T	pDM_FatTable = &pDM_Odm->DM_FatTable;
	if((pDM_Odm->SupportICType != ODM_RTL8188E) || (!(pDM_Odm->SupportAbility & ODM_BB_ANT_DIV)))
	{
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_AntennaDiversity_88E: Not Support 88E AntDiv\n"));
		return;	
	}

	if(!pDM_Odm->bLinked)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("ODM_AntennaDiversity_88E(): No Link.\n"));
		if(pDM_FatTable->bBecomeLinked == TRUE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Need to Turn off HW AntDiv\n"));
			ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 0);	//RegC50[7]=1'b1 		//enable HW AntDiv
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_ANTDIV_PARA1_11N , BIT15, 0); //Enable CCK AntDiv
			if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
				ODM_SetBBReg(pDM_Odm, ODM_REG_TX_ANT_CTRL_11N , BIT21, 0); //Reg80c[21]=1'b0		//from TX Reg
			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;
		}
		return;
	}
	else
	{
		if(pDM_FatTable->bBecomeLinked ==FALSE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Need to Turn on HW AntDiv\n"));
			//Because HW AntDiv is disabled before Link, we enable HW AntDiv after link
			ODM_SetBBReg(pDM_Odm, ODM_REG_IGI_A_11N , BIT7, 1);	//RegC50[7]=1'b1 		//enable HW AntDiv
			ODM_SetBBReg(pDM_Odm, ODM_REG_CCK_ANTDIV_PARA1_11N , BIT15, 1); //Enable CCK AntDiv
			//ODM_SetMACReg(pDM_Odm, 0x7B4 , BIT18, 1); //Response Tx by current HW antdiv
	if(pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)
			{
#if TX_BY_REG
				ODM_SetBBReg(pDM_Odm, ODM_REG_TX_ANT_CTRL_11N , BIT21, 0); //Reg80c[21]=1'b0		//from Reg
#else
				ODM_SetBBReg(pDM_Odm, ODM_REG_TX_ANT_CTRL_11N , BIT21, 1); //Reg80c[21]=1'b1		//from TX Info
#endif
			}
			pDM_FatTable->bBecomeLinked = pDM_Odm->bLinked;
		}
	}

	if((pDM_Odm->AntDivType == CG_TRX_HW_ANTDIV)||(pDM_Odm->AntDivType == CGCS_RX_HW_ANTDIV))
		odm_HWAntDiv(pDM_Odm);
	#if (!(DM_ODM_SUPPORT_TYPE == ODM_CE))
	else if(pDM_Odm->AntDivType == CG_TRX_SMART_ANTDIV)
		odm_FastAntTraining(pDM_Odm);
	#endif
}


/*
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
VOID
odm_FastAntTrainingCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//#if DEV_BUS_TYPE==RT_PCI_INTERFACE
	//#if USE_WORKITEM
	//PlatformScheduleWorkItem(&pHalData->SwAntennaSwitchWorkitem);
	//#else
	odm_FastAntTraining(&pHalData->DM_OutSrc);
	//#endif
	//#else
	//PlatformScheduleWorkItem(&pHalData->SwAntennaSwitchWorkitem);
	//#endif
	
}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
VOID odm_FastAntTrainingCallback(void *FunctionContext)
{
	PDM_ODM_T	pDM_Odm= (PDM_ODM_T)FunctionContext;
	PADAPTER	padapter = pDM_Odm->Adapter;
	if(padapter->net_closed == _TRUE)
	    return;
	odm_FastAntTraining(pDM_Odm);
}
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID odm_FastAntTrainingCallback(void *FunctionContext)
{
	PDM_ODM_T	pDM_Odm= (PDM_ODM_T)FunctionContext;
	odm_FastAntTraining(pDM_Odm);
}

#endif
*/

#else //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP|ODM_CE))
VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
	)
{
}
#else// (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm
		)
{
}
#endif
#endif //#if(defined(CONFIG_HW_ANTENNA_DIVERSITY))
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
	prtl8192cd_priv	priv		= pDM_Odm->priv;	// for AP

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
#if (DM_ODM_SUPPORT_TYPE & (ODM_MP))	
	PMGNT_INFO	pMgntInfo = &(Adapter->MgntInfo);
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
	
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
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
	
#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
	if (ACTING_AS_AP(Adapter))   // primary cca process only do at AP mode
#endif
	{
		
	#if (DM_ODM_SUPPORT_TYPE == ODM_MP)
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
				Client_tmp = pEntry->HTInfo.bBw40MHz;   // client BW
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
ODM_UpdateRxIdleAnt_88E(IN PDM_ODM_T pDM_Odm, IN u1Byte Ant)
{
}
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

