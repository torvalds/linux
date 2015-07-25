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

//#include "Mp_Precomp.h"

#include "../odm_precomp.h"

#if (RTL8812A_SUPPORT == 1)

VOID
odm_UpdateTxPath_8812A(IN PDM_ODM_T pDM_Odm, IN u1Byte Path)
{
	pPATHDIV_T	pDM_PathDiv = &pDM_Odm->DM_PathDiv;

	if(pDM_PathDiv->RespTxPath != Path)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("Need to Update Tx Path\n"));
		
		if(Path == ODM_RF_PATH_A)
		{
			ODM_SetBBReg(pDM_Odm, 0x80c , 0xFFF0, 0x111); //Tx by Reg
			ODM_SetBBReg(pDM_Odm, 0x6d8 , BIT7|BIT6, 1); //Resp Tx by Txinfo
		}
		else
		{
			ODM_SetBBReg(pDM_Odm, 0x80c , 0xFFF0, 0x222); //Tx by Reg
			ODM_SetBBReg(pDM_Odm, 0x6d8 , BIT7|BIT6, 2); //Resp Tx by Txinfo
		}
	}
	pDM_PathDiv->RespTxPath = Path;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("Path=%s\n",(Path==ODM_RF_PATH_A)?"ODM_RF_PATH_A":"ODM_RF_PATH_B"));
}

VOID
ODM_PathStatistics_8812A(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			MacId,
	IN		u4Byte			RSSI_A,
	IN		u4Byte			RSSI_B
)
{
	pPATHDIV_T	pDM_PathDiv = &pDM_Odm->DM_PathDiv;

	pDM_PathDiv->PathA_Sum[MacId]+=RSSI_A;
	pDM_PathDiv->PathA_Cnt[MacId]++;

	pDM_PathDiv->PathB_Sum[MacId]+=RSSI_B;
	pDM_PathDiv->PathB_Cnt[MacId]++;
}

VOID
ODM_PathDiversityInit_8812A(
	IN	PDM_ODM_T 	pDM_Odm
)
{
	u4Byte	i;
	pPATHDIV_T	pDM_PathDiv = &pDM_Odm->DM_PathDiv;
	
	ODM_SetBBReg(pDM_Odm, 0x80c , BIT29, 1); //Tx path from Reg
	ODM_SetBBReg(pDM_Odm, 0x80c , 0xFFF0, 0x111); //Tx by Reg
	ODM_SetBBReg(pDM_Odm, 0x6d8 , BIT7|BIT6, 1); //Resp Tx by Txinfo
	odm_UpdateTxPath_8812A(pDM_Odm, ODM_RF_PATH_A);

	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pDM_PathDiv->PathSel[i] = 1; // TxInfo default at path-A
	}
}



VOID
ODM_PathDiversity_8812A(
	IN	PDM_ODM_T 	pDM_Odm
	)
{
	u4Byte	i, RssiAvgA=0, RssiAvgB=0, LocalMinRSSI, MinRSSI=0xFF;
	u1Byte	TxRespPath=0, TargetPath;
	pPATHDIV_T	pDM_PathDiv = &pDM_Odm->DM_PathDiv;
	PSTA_INFO_T   	pEntry;


	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("Odm_PathDiversity_8812A() =>\n"));
	   
	for (i=0; i<ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		pEntry = pDM_Odm->pODM_StaInfo[i];
		if(IS_STA_VALID(pEntry))
		{
			//2 Caculate RSSI per Path
			RssiAvgA = (pDM_PathDiv->PathA_Cnt[i]!=0)?(pDM_PathDiv->PathA_Sum[i]/pDM_PathDiv->PathA_Cnt[i]):0;
			RssiAvgB = (pDM_PathDiv->PathB_Cnt[i]!=0)?(pDM_PathDiv->PathB_Sum[i]/pDM_PathDiv->PathB_Cnt[i]):0;
			TargetPath = (RssiAvgA==RssiAvgB)?pDM_PathDiv->RespTxPath:((RssiAvgA>=RssiAvgB)?ODM_RF_PATH_A:ODM_RF_PATH_B);
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("MacID=%d, PathA_Sum=%d, PathA_Cnt=%d\n", i, pDM_PathDiv->PathA_Sum[i], pDM_PathDiv->PathA_Cnt[i]));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("MacID=%d, PathB_Sum=%d, PathB_Cnt=%d\n",i, pDM_PathDiv->PathB_Sum[i], pDM_PathDiv->PathB_Cnt[i]));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("MacID=%d, RssiAvgA= %d, RssiAvgB= %d\n", i, RssiAvgA, RssiAvgB));

			//2 Select Resp Tx Path
			LocalMinRSSI = (RssiAvgA>RssiAvgB)?RssiAvgB:RssiAvgA;
			if(LocalMinRSSI < MinRSSI)
			{
				MinRSSI = LocalMinRSSI;
				TxRespPath = TargetPath;
			}	

			//2 Select Tx DESC
			if(TargetPath == ODM_RF_PATH_A)
				pDM_PathDiv->PathSel[i] = 1;
			else
				pDM_PathDiv->PathSel[i] = 2;

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD, ("Tx from TxInfo, TargetPath=%s\n", 
								(TargetPath==ODM_RF_PATH_A)?"ODM_RF_PATH_A":"ODM_RF_PATH_B"));
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, ODM_DBG_LOUD,("pDM_PathDiv->PathSel[%d] = %d\n", i, pDM_PathDiv->PathSel[i]));
				
		}
		pDM_PathDiv->PathA_Cnt[i] = 0;
		pDM_PathDiv->PathA_Sum[i] = 0;
		pDM_PathDiv->PathB_Cnt[i] = 0;
		pDM_PathDiv->PathB_Sum[i] = 0;
	}
       
	//2 Update Tx Path
	odm_UpdateTxPath_8812A(pDM_Odm, TxRespPath);

}


#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
VOID
ODM_SetTxPathByTxInfo_8812A(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
)
{
	pPATHDIV_T	pDM_PathDiv = &pDM_Odm->DM_PathDiv;

	if((pDM_Odm->SupportICType != ODM_RTL8812)||(!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV)))
		return;

	SET_TX_DESC_TX_ANT_8812(pDesc, pDM_PathDiv->PathSel[macId]);
}
#else// (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID
ODM_SetTxPathByTxInfo_8812A(
	IN		PDM_ODM_T		pDM_Odm	
)
{

}
#endif


#endif //#if (RTL8812A_SUPPORT == 1)