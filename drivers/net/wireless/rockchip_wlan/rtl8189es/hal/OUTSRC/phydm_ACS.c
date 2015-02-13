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


u1Byte
ODM_GetAutoChannelSelectResult(
	IN		PVOID			pDM_VOID,
	IN		u1Byte			Band
)
{
	PDM_ODM_T				pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PACS					pACS = &pDM_Odm->DM_ACS;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	if(Band == ODM_BAND_2_4G)
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("[ACS] ODM_GetAutoChannelSelectResult(): CleanChannel_2G(%d)\n", pACS->CleanChannel_2G));
		return (u1Byte)pACS->CleanChannel_2G;	
	}
	else
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("[ACS] ODM_GetAutoChannelSelectResult(): CleanChannel_5G(%d)\n", pACS->CleanChannel_5G));
		return (u1Byte)pACS->CleanChannel_5G;	
	}
#else
	return (u1Byte)pACS->CleanChannel_2G;
#endif

}

VOID
odm_AutoChannelSelectSetting(
	IN		PVOID			pDM_VOID,
	IN		BOOLEAN			IsEnable
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u2Byte						period = 0x2710;// 40ms in default
	u2Byte						NHMType = 0x7;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelectSetting()=========> \n"));

	if(IsEnable)
	{//20 ms
		period = 0x1388;
		NHMType = 0x1;
	}

	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
	{
		//PHY parameters initialize for ac series
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11AC+2, period);	//0x990[31:16]=0x2710	Time duration for NHM unit: 4us, 0x2710=40ms
		//ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC, BIT8|BIT9|BIT10, NHMType);	//0x994[9:8]=3			enable CCX
	}
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
	{
		//PHY parameters initialize for n series
		ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N+2, period);	//0x894[31:16]=0x2710	Time duration for NHM unit: 4us, 0x2710=40ms
		//ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N, BIT10|BIT9|BIT8, NHMType);	//0x890[9:8]=3			enable CCX		
	}
#endif
}

VOID
odm_AutoChannelSelectInit(
	IN		PVOID			pDM_VOID
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PACS						pACS = &pDM_Odm->DM_ACS;
	u1Byte						i;

	if(!(pDM_Odm->SupportAbility & ODM_BB_NHM_CNT))
		return;

	if(pACS->bForceACSResult)
		return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelectInit()=========> \n"));

	pACS->CleanChannel_2G = 1;
	pACS->CleanChannel_5G = 36;

	for (i = 0; i < ODM_MAX_CHANNEL_2G; ++i)
	{
		pACS->Channel_Info_2G[0][i] = 0;
		pACS->Channel_Info_2G[1][i] = 0;
	}

	if(pDM_Odm->SupportICType & (ODM_IC_11AC_SERIES|ODM_RTL8192D))
	{
		for (i = 0; i < ODM_MAX_CHANNEL_5G; ++i)
		{
			pACS->Channel_Info_5G[0][i] = 0;
			pACS->Channel_Info_5G[1][i] = 0;
		}
	}
#endif
}

VOID
odm_AutoChannelSelectReset(
	IN		PVOID			pDM_VOID
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PACS						pACS = &pDM_Odm->DM_ACS;

	if(!(pDM_Odm->SupportAbility & ODM_BB_NHM_CNT))
		return;

	if(pACS->bForceACSResult)
		return;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelectReset()=========> \n"));

	odm_AutoChannelSelectSetting(pDM_Odm,TRUE);// for 20ms measurement
	Phydm_NHMCounterStatisticsReset(pDM_Odm);
#endif
}

VOID
odm_AutoChannelSelect(
	IN		PVOID			pDM_VOID,
	IN		u1Byte			Channel
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	PDM_ODM_T					pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PACS						pACS = &pDM_Odm->DM_ACS;
	u1Byte						ChannelIDX = 0, SearchIDX = 0;
	u2Byte						MaxScore=0;

	if(!(pDM_Odm->SupportAbility & ODM_BB_NHM_CNT))
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_AutoChannelSelect(): Return: SupportAbility ODM_BB_NHM_CNT is disabled\n"));
		return;
	}

	if(pACS->bForceACSResult)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_DIG, ODM_DBG_LOUD, ("odm_AutoChannelSelect(): Force 2G clean channel = %d, 5G clean channel = %d\n",
			pACS->CleanChannel_2G, pACS->CleanChannel_5G));
		return;
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelect(): Channel = %d=========> \n", Channel));

	Phydm_GetNHMCounterStatistics(pDM_Odm);
	odm_AutoChannelSelectSetting(pDM_Odm,FALSE);

	if(Channel >=1 && Channel <=14)
	{
		ChannelIDX = Channel - 1;
		pACS->Channel_Info_2G[1][ChannelIDX]++;
		
		if(pACS->Channel_Info_2G[1][ChannelIDX] >= 2)
			pACS->Channel_Info_2G[0][ChannelIDX] = (pACS->Channel_Info_2G[0][ChannelIDX] >> 1) + 
			(pACS->Channel_Info_2G[0][ChannelIDX] >> 2) + (pDM_Odm->NHM_cnt_0>>2);
		else
			pACS->Channel_Info_2G[0][ChannelIDX] = pDM_Odm->NHM_cnt_0;
	
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelect(): NHM_cnt_0 = %d \n", pDM_Odm->NHM_cnt_0));
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelect(): Channel_Info[0][%d] = %d, Channel_Info[1][%d] = %d\n", ChannelIDX, pACS->Channel_Info_2G[0][ChannelIDX], ChannelIDX, pACS->Channel_Info_2G[1][ChannelIDX]));

		for(SearchIDX = 0; SearchIDX < ODM_MAX_CHANNEL_2G; SearchIDX++)
		{
			if(pACS->Channel_Info_2G[1][SearchIDX] != 0)
			{
				if(pACS->Channel_Info_2G[0][SearchIDX] >= MaxScore)
				{
					MaxScore = pACS->Channel_Info_2G[0][SearchIDX];
					pACS->CleanChannel_2G = SearchIDX+1;
				}
			}
		}
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("(1)odm_AutoChannelSelect(): 2G: CleanChannel_2G = %d, MaxScore = %d \n", 
			pACS->CleanChannel_2G, MaxScore));

	}
	else if(Channel >= 36)
	{
		// Need to do
		pACS->CleanChannel_5G = Channel;
	}
#endif
}
