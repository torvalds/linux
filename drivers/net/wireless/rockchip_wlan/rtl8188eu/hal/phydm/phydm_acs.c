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

#if ( DM_ODM_SUPPORT_TYPE & ODM_AP )

VOID
phydm_AutoChannelSelectSettingAP(
    IN  PVOID   pDM_VOID,
    IN  u4Byte  setting,             // 0: STORE_DEFAULT_NHM_SETTING; 1: RESTORE_DEFAULT_NHM_SETTING, 2: ACS_NHM_SETTING
    IN  u4Byte  acs_step
)
{
    PDM_ODM_T           pDM_Odm = (PDM_ODM_T)pDM_VOID;
    prtl8192cd_priv       priv           = pDM_Odm->priv;
    PACS                    pACS         = &pDM_Odm->DM_ACS;

    ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("odm_AutoChannelSelectSettingAP()=========> \n"));

    //3 Store Default Setting
    if(setting == STORE_DEFAULT_NHM_SETTING)
    {
        ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("STORE_DEFAULT_NHM_SETTING\n"));
    
        if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)     // store Reg0x990, Reg0x994, Reg0x998, Reg0x99C, Reg0x9a0
        {
            pACS->Reg0x990 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TIMER_11AC);                // Reg0x990
            pACS->Reg0x994 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC);           // Reg0x994
            pACS->Reg0x998 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC);       // Reg0x998
            pACS->Reg0x99C = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC);       // Reg0x99c
            pACS->Reg0x9A0 = ODM_Read1Byte(pDM_Odm, ODM_REG_NHM_TH8_11AC);                   // Reg0x9a0, u1Byte            
        }
        else if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
        {
            pACS->Reg0x890 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N);             // Reg0x890
            pACS->Reg0x894 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N);                  // Reg0x894
            pACS->Reg0x898 = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N);         // Reg0x898
            pACS->Reg0x89C = ODM_Read4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N);         // Reg0x89c
            pACS->Reg0xE28 = ODM_Read1Byte(pDM_Odm, ODM_REG_NHM_TH8_11N);                     // Reg0xe28, u1Byte    
        }
    }

    //3 Restore Default Setting
    else if(setting == RESTORE_DEFAULT_NHM_SETTING)
    {
        ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("RESTORE_DEFAULT_NHM_SETTING\n"));
        
        if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)     // store Reg0x990, Reg0x994, Reg0x998, Reg0x99C, Reg0x9a0
        {
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TIMER_11AC,          pACS->Reg0x990);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC,     pACS->Reg0x994);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, pACS->Reg0x998);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, pACS->Reg0x99C);
            ODM_Write1Byte(pDM_Odm, ODM_REG_NHM_TH8_11AC,             pACS->Reg0x9A0);   
        }
        else if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
        {
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N,     pACS->Reg0x890);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N,          pACS->Reg0x894);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, pACS->Reg0x898);
            ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, pACS->Reg0x89C);
            ODM_Write1Byte(pDM_Odm, ODM_REG_NHM_TH8_11N,             pACS->Reg0xE28); 
        }        
    }

    //3 ACS Setting
    else if(setting == ACS_NHM_SETTING)
    {        
        ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("ACS_NHM_SETTING\n"));
        u2Byte  period;
        period = 0x61a8;
        pACS->ACS_Step = acs_step;
            
        if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
        {   
            //4 Set NHM period, 0x990[31:16]=0x61a8, Time duration for NHM unit: 4us, 0x61a8=100ms
            ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11AC+2, period);
            //4 Set NHM ignore_cca=1, ignore_txon=1, ccx_en=0
            ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC,BIT8|BIT9|BIT10, 3);
            
            if(pACS->ACS_Step == 0)
            {
                //4 Set IGI
                ODM_SetBBReg(pDM_Odm,0xc50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x3E);
                if (get_rf_mimo_mode(priv) != MIMO_1T1R)
					ODM_SetBBReg(pDM_Odm,0xe50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x3E);
                    
                //4 Set ACS NHM threshold
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0x82786e64);
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffff8c);
                ODM_Write1Byte(pDM_Odm, ODM_REG_NHM_TH8_11AC, 0xff);
                ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11AC+2, 0xffff);
                
            }
            else if(pACS->ACS_Step == 1)
            {
                //4 Set IGI
                ODM_SetBBReg(pDM_Odm,0xc50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x2A);
                if (get_rf_mimo_mode(priv) != MIMO_1T1R)
					ODM_SetBBReg(pDM_Odm,0xe50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x2A);

                //4 Set ACS NHM threshold
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11AC, 0x5a50463c);
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11AC, 0xffffff64);
                
            }

        }
        else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
        {
            //4 Set NHM period, 0x894[31:16]=0x61a8, Time duration for NHM unit: 4us, 0x61a8=100ms
            ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TIMER_11N+2, period);
            //4 Set NHM ignore_cca=1, ignore_txon=1, ccx_en=0
            ODM_SetBBReg(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N,BIT8|BIT9|BIT10, 3);
            
            if(pACS->ACS_Step == 0)
            {
                //4 Set IGI
                ODM_SetBBReg(pDM_Odm,0xc50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x3E);
                if (get_rf_mimo_mode(priv) != MIMO_1T1R)
					ODM_SetBBReg(pDM_Odm,0xc58,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x3E);
            
                //4 Set ACS NHM threshold
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, 0x82786e64);
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffff8c);
                ODM_Write1Byte(pDM_Odm, ODM_REG_NHM_TH8_11N, 0xff);
                ODM_Write2Byte(pDM_Odm, ODM_REG_NHM_TH9_TH10_11N+2, 0xffff);
                
            }
            else if(pACS->ACS_Step == 1)
            {
                //4 Set IGI
                ODM_SetBBReg(pDM_Odm,0xc50,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x2A);
                if (get_rf_mimo_mode(priv) != MIMO_1T1R)
					ODM_SetBBReg(pDM_Odm,0xc58,BIT0|BIT1|BIT2|BIT3|BIT4|BIT5|BIT6,0x2A);
            
                //4 Set ACS NHM threshold
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH3_TO_TH0_11N, 0x5a50463c);
                ODM_Write4Byte(pDM_Odm, ODM_REG_NHM_TH7_TO_TH4_11N, 0xffffff64);

            }            
        }
    }	
	
}

VOID
phydm_GetNHMStatisticsAP(
    IN  PVOID       pDM_VOID,
    IN  u4Byte      idx,                // @ 2G, Real channel number = idx+1
    IN  u4Byte      acs_step
)
{
    PDM_ODM_T	    pDM_Odm = (PDM_ODM_T)pDM_VOID;
    prtl8192cd_priv     priv    = pDM_Odm->priv;
    PACS                  pACS    = &pDM_Odm->DM_ACS;
    u4Byte                value32 = 0;
    u1Byte                i;

    pACS->ACS_Step = acs_step;

    if(pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
    {
        //4 Check if NHM result is ready        
        for (i=0; i<20; i++) {
            
            ODM_delay_ms(1);
            if ( ODM_GetBBReg(pDM_Odm,rFPGA0_PSDReport,BIT17) )
                break;
        }
        
        //4 Get NHM Statistics        
        if ( pACS->ACS_Step==1 ) {
            
            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT7_TO_CNT4_11N);
            
            pACS->NHM_Cnt[idx][9] = (value32 & bMaskByte1) >> 8;
            pACS->NHM_Cnt[idx][8] = (value32 & bMaskByte0);

            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT_11N);    // ODM_REG_NHM_CNT3_TO_CNT0_11N

            pACS->NHM_Cnt[idx][7] = (value32 & bMaskByte3) >> 24;
            pACS->NHM_Cnt[idx][6] = (value32 & bMaskByte2) >> 16;
            pACS->NHM_Cnt[idx][5] = (value32 & bMaskByte1) >> 8;

        } else if (pACS->ACS_Step==2) {
        
            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT_11N);   // ODM_REG_NHM_CNT3_TO_CNT0_11N

            pACS->NHM_Cnt[idx][4] = ODM_Read1Byte(pDM_Odm, ODM_REG_NHM_CNT7_TO_CNT4_11N);            
            pACS->NHM_Cnt[idx][3] = (value32 & bMaskByte3) >> 24;
            pACS->NHM_Cnt[idx][2] = (value32 & bMaskByte2) >> 16;
            pACS->NHM_Cnt[idx][1] = (value32 & bMaskByte1) >> 8;
            pACS->NHM_Cnt[idx][0] = (value32 & bMaskByte0);
        }
    }
    else if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
    {
        //4 Check if NHM result is ready        
        for (i=0; i<20; i++) {
            
            ODM_delay_ms(1);
            if (ODM_GetBBReg(pDM_Odm,ODM_REG_NHM_DUR_READY_11AC,BIT17))
                break;
        }
    
        if ( pACS->ACS_Step==1 ) {
            
            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT7_TO_CNT4_11AC);
            
            pACS->NHM_Cnt[idx][9] = (value32 & bMaskByte1) >> 8;
            pACS->NHM_Cnt[idx][8] = (value32 & bMaskByte0);

            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT_11AC);     // ODM_REG_NHM_CNT3_TO_CNT0_11AC

            pACS->NHM_Cnt[idx][7] = (value32 & bMaskByte3) >> 24;
            pACS->NHM_Cnt[idx][6] = (value32 & bMaskByte2) >> 16;
            pACS->NHM_Cnt[idx][5] = (value32 & bMaskByte1) >> 8;

        } else if (pACS->ACS_Step==2) {
        
            value32 = ODM_Read4Byte(pDM_Odm,ODM_REG_NHM_CNT_11AC);      // ODM_REG_NHM_CNT3_TO_CNT0_11AC

            pACS->NHM_Cnt[idx][4] = ODM_Read1Byte(pDM_Odm, ODM_REG_NHM_CNT7_TO_CNT4_11AC);            
            pACS->NHM_Cnt[idx][3] = (value32 & bMaskByte3) >> 24;
            pACS->NHM_Cnt[idx][2] = (value32 & bMaskByte2) >> 16;
            pACS->NHM_Cnt[idx][1] = (value32 & bMaskByte1) >> 8;
            pACS->NHM_Cnt[idx][0] = (value32 & bMaskByte0);
        }            
    }

}


//#define ACS_DEBUG_INFO //acs debug default off
/*
int phydm_AutoChannelSelectAP( 
    IN   PVOID   pDM_VOID,
    IN   u4Byte  ACS_Type,                      // 0: RXCount_Type, 1:NHM_Type
    IN   u4Byte  available_chnl_num        // amount of all channels
    )
{
    PDM_ODM_T               pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PACS                    pACS    = &pDM_Odm->DM_ACS;
    prtl8192cd_priv			priv    = pDM_Odm->priv;
    
    static u4Byte           score2G[MAX_2G_CHANNEL_NUM], score5G[MAX_5G_CHANNEL_NUM];
    u4Byte                  score[MAX_BSS_NUM], use_nhm = 0;
    u4Byte                  minScore=0xffffffff;
    u4Byte                  tmpScore, tmpIdx=0;
    u4Byte                  traffic_check = 0;
    u4Byte                  fa_count_weighting = 1;
    int                     i, j, idx=0, idx_2G_end=-1, idx_5G_begin=-1, minChan=0;
	struct bss_desc *pBss=NULL;

#ifdef _DEBUG_RTL8192CD_
	char tmpbuf[400];
	int len=0;
#endif

	memset(score2G, '\0', sizeof(score2G));
	memset(score5G, '\0', sizeof(score5G));

	for (i=0; i<priv->available_chnl_num; i++) {
		if (priv->available_chnl[i] <= 14)
			idx_2G_end = i;
		else
			break;
	}

	for (i=0; i<priv->available_chnl_num; i++) {
		if (priv->available_chnl[i] > 14) {
			idx_5G_begin = i;
			break;
		}
	}

// DELETE
#ifndef CONFIG_RTL_NEW_AUTOCH
	for (i=0; i<priv->site_survey->count; i++) {
		pBss = &priv->site_survey->bss[i];
		for (idx=0; idx<priv->available_chnl_num; idx++) {
			if (pBss->channel == priv->available_chnl[idx]) {
				if (pBss->channel <= 14)
					setChannelScore(idx, score2G, 0, MAX_2G_CHANNEL_NUM-1);
				else
					score5G[idx - idx_5G_begin] += 5;
				break;
			}
		}
	}
#endif

	if (idx_2G_end >= 0)
		for (i=0; i<=idx_2G_end; i++)
			score[i] = score2G[i];
	if (idx_5G_begin >= 0)
		for (i=idx_5G_begin; i<priv->available_chnl_num; i++)
			score[i] = score5G[i - idx_5G_begin];
		
#ifdef CONFIG_RTL_NEW_AUTOCH
	{
		u4Byte y, ch_begin=0, ch_end= priv->available_chnl_num;

		u4Byte do_ap_check = 1, ap_ratio = 0;
		
		if (idx_2G_end >= 0) 
			ch_end = idx_2G_end+1;
		if (idx_5G_begin >= 0)  
			ch_begin = idx_5G_begin;

#ifdef ACS_DEBUG_INFO//for debug
		printk("\n");
		for (y=ch_begin; y<ch_end; y++)
			printk("1. init: chnl[%d] 20M_rx[%d] 40M_rx[%d] fa_cnt[%d] score[%d]\n",
				priv->available_chnl[y], 
				priv->chnl_ss_mac_rx_count[y], 
				priv->chnl_ss_mac_rx_count_40M[y],
				priv->chnl_ss_fa_count[y],
				score[y]);
		printk("\n");
#endif

#if defined(CONFIG_RTL_88E_SUPPORT) || defined(CONFIG_WLAN_HAL_8192EE)
        if( pDM_Odm->SupportICType&(ODM_RTL8188E|ODM_RTL8192E)&& priv->pmib->dot11RFEntry.acs_type )
		{
			u4Byte tmp_score[MAX_BSS_NUM];
			memcpy(tmp_score, score, sizeof(score));
			if (find_clean_channel(priv, ch_begin, ch_end, tmp_score)) {
				//memcpy(score, tmp_score, sizeof(score));
#ifdef _DEBUG_RTL8192CD_
				printk("!! Found clean channel, select minimum FA channel\n");
#endif
				goto USE_CLN_CH;
			}
#ifdef _DEBUG_RTL8192CD_
			printk("!! Not found clean channel, use NHM algorithm\n");
#endif
			use_nhm = 1;
USE_CLN_CH:
			for (y=ch_begin; y<ch_end; y++) {
				for (i=0; i<=9; i++) {
					u4Byte val32 = priv->nhm_cnt[y][i];
					for (j=0; j<i; j++)
						val32 *= 3;
					score[y] += val32;
				}

#ifdef _DEBUG_RTL8192CD_				
				printk("nhm_cnt_%d: H<-[ %3d %3d %3d %3d %3d %3d %3d %3d %3d %3d]->L, score: %d\n", 
					y+1, priv->nhm_cnt[y][9], priv->nhm_cnt[y][8], priv->nhm_cnt[y][7], 
					priv->nhm_cnt[y][6], priv->nhm_cnt[y][5], priv->nhm_cnt[y][4],
					priv->nhm_cnt[y][3], priv->nhm_cnt[y][2], priv->nhm_cnt[y][1],
					priv->nhm_cnt[y][0], score[y]);
#endif
			}

			if (!use_nhm)
				memcpy(score, tmp_score, sizeof(score));
			
			goto choose_ch;
		}
#endif

            // For each channel, weighting behind channels with MAC RX counter
            //For each channel, weighting the channel with FA counter

		for (y=ch_begin; y<ch_end; y++) {
			score[y] += 8 * priv->chnl_ss_mac_rx_count[y];
			if (priv->chnl_ss_mac_rx_count[y] > 30)
				do_ap_check = 0;
			if( priv->chnl_ss_mac_rx_count[y] > MAC_RX_COUNT_THRESHOLD )
				traffic_check = 1;
			
#ifdef RTK_5G_SUPPORT
			if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
#endif
			{
				if ((int)(y-4) >= (int)ch_begin)
					score[y-4] += 2 * priv->chnl_ss_mac_rx_count[y];				
				if ((int)(y-3) >= (int)ch_begin)
					score[y-3] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y-2) >= (int)ch_begin)
					score[y-2] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y-1) >= (int)ch_begin)
					score[y-1] += 10 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y+1) < (int)ch_end)
					score[y+1] += 10 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y+2) < (int)ch_end)
					score[y+2] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y+3) < (int)ch_end)
					score[y+3] += 8 * priv->chnl_ss_mac_rx_count[y];
				if ((int)(y+4) < (int)ch_end)
					score[y+4] += 2 * priv->chnl_ss_mac_rx_count[y];
			}

			//this is for CH_LOAD caculation
			if( priv->chnl_ss_cca_count[y] > priv->chnl_ss_fa_count[y])
				priv->chnl_ss_cca_count[y]-= priv->chnl_ss_fa_count[y];
			else
				priv->chnl_ss_cca_count[y] = 0;
		}

#ifdef ACS_DEBUG_INFO//for debug
		printk("\n");
		for (y=ch_begin; y<ch_end; y++)
			printk("2. after 20M check: chnl[%d] score[%d]\n",priv->available_chnl[y], score[y]);
		printk("\n");
#endif	

		for (y=ch_begin; y<ch_end; y++) {
			if (priv->chnl_ss_mac_rx_count_40M[y]) {
				score[y] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
				if (priv->chnl_ss_mac_rx_count_40M[y] > 30)
					do_ap_check = 0;
				if( priv->chnl_ss_mac_rx_count_40M[y] > MAC_RX_COUNT_THRESHOLD )
					traffic_check = 1;
				
#ifdef RTK_5G_SUPPORT
				if (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
#endif
				{
					if ((int)(y-6) >= (int)ch_begin)
						score[y-6] += 1 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y-5) >= (int)ch_begin)
						score[y-5] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y-4) >= (int)ch_begin)
						score[y-4] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y-3) >= (int)ch_begin)
						score[y-3] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y-2) >= (int)ch_begin)
						score[y-2] += (5 * priv->chnl_ss_mac_rx_count_40M[y])/2;
					if ((int)(y-1) >= (int)ch_begin)
						score[y-1] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y+1) < (int)ch_end)
						score[y+1] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y+2) < (int)ch_end)
						score[y+2] += (5 * priv->chnl_ss_mac_rx_count_40M[y])/2;
					if ((int)(y+3) < (int)ch_end)
						score[y+3] += 5 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y+4) < (int)ch_end)
						score[y+4] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y+5) < (int)ch_end)
						score[y+5] += 4 * priv->chnl_ss_mac_rx_count_40M[y];
					if ((int)(y+6) < (int)ch_end)
						score[y+6] += 1 * priv->chnl_ss_mac_rx_count_40M[y];
				}
			}
		}

#ifdef ACS_DEBUG_INFO//for debug
		printk("\n");
		for (y=ch_begin; y<ch_end; y++)
			printk("3. after 40M check: chnl[%d] score[%d]\n",priv->available_chnl[y], score[y]);
		printk("\n");
		printk("4. do_ap_check=%d traffic_check=%d\n", do_ap_check, traffic_check);
		printk("\n");
#endif

		if( traffic_check == 0)
			fa_count_weighting = 5;
		else
			fa_count_weighting = 1;

		for (y=ch_begin; y<ch_end; y++) {
			score[y] += fa_count_weighting * priv->chnl_ss_fa_count[y];
		}

#ifdef ACS_DEBUG_INFO//for debug
		printk("\n");
		for (y=ch_begin; y<ch_end; y++)
			printk("5. after fa check: chnl[%d] score[%d]\n",priv->available_chnl[y], score[y]);
		printk("\n");
#endif			

		if (do_ap_check) {
			for (i=0; i<priv->site_survey->count; i++) {				
				pBss = &priv->site_survey->bss[i];
				for (y=ch_begin; y<ch_end; y++) {
					if (pBss->channel == priv->available_chnl[y]) {
						if (pBss->channel <= 14) {
#ifdef ACS_DEBUG_INFO//for debug
						printk("\n");
						printk("chnl[%d] has ap rssi=%d bw[0x%02x]\n",
							pBss->channel, pBss->rssi, pBss->t_stamp[1]);
						printk("\n");
#endif
							if (pBss->rssi > 60)
								ap_ratio = 4;
							else if (pBss->rssi > 35)
								ap_ratio = 2;
							else
								ap_ratio = 1;
							
							if ((pBss->t_stamp[1] & 0x6) == 0) {
								score[y] += 50 * ap_ratio;
								if ((int)(y-4) >= (int)ch_begin)
									score[y-4] += 10 * ap_ratio;
								if ((int)(y-3) >= (int)ch_begin)
									score[y-3] += 20 * ap_ratio;
								if ((int)(y-2) >= (int)ch_begin)
									score[y-2] += 30 * ap_ratio;
								if ((int)(y-1) >= (int)ch_begin)
									score[y-1] += 40 * ap_ratio;
								if ((int)(y+1) < (int)ch_end)
									score[y+1] += 40 * ap_ratio;
								if ((int)(y+2) < (int)ch_end)
									score[y+2] += 30 * ap_ratio;
								if ((int)(y+3) < (int)ch_end)
									score[y+3] += 20 * ap_ratio;
								if ((int)(y+4) < (int)ch_end)
									score[y+4] += 10 * ap_ratio;
							}	
							else if ((pBss->t_stamp[1] & 0x4) == 0) {
								score[y] += 50 * ap_ratio;
								if ((int)(y-3) >= (int)ch_begin)
									score[y-3] += 20 * ap_ratio;
								if ((int)(y-2) >= (int)ch_begin)
									score[y-2] += 30 * ap_ratio;
								if ((int)(y-1) >= (int)ch_begin)
									score[y-1] += 40 * ap_ratio;
								if ((int)(y+1) < (int)ch_end)
									score[y+1] += 50 * ap_ratio;
								if ((int)(y+2) < (int)ch_end)
									score[y+2] += 50 * ap_ratio;
								if ((int)(y+3) < (int)ch_end)
									score[y+3] += 50 * ap_ratio;
								if ((int)(y+4) < (int)ch_end)
									score[y+4] += 50 * ap_ratio;
								if ((int)(y+5) < (int)ch_end)
									score[y+5] += 40 * ap_ratio;
								if ((int)(y+6) < (int)ch_end)
									score[y+6] += 30 * ap_ratio;
								if ((int)(y+7) < (int)ch_end)
									score[y+7] += 20 * ap_ratio;	
							}	
							else {
								score[y] += 50 * ap_ratio;
								if ((int)(y-7) >= (int)ch_begin)
									score[y-7] += 20 * ap_ratio;
								if ((int)(y-6) >= (int)ch_begin)
									score[y-6] += 30 * ap_ratio;
								if ((int)(y-5) >= (int)ch_begin)
									score[y-5] += 40 * ap_ratio;
								if ((int)(y-4) >= (int)ch_begin)
									score[y-4] += 50 * ap_ratio;
								if ((int)(y-3) >= (int)ch_begin)
									score[y-3] += 50 * ap_ratio;
								if ((int)(y-2) >= (int)ch_begin)
									score[y-2] += 50 * ap_ratio;
								if ((int)(y-1) >= (int)ch_begin)
									score[y-1] += 50 * ap_ratio;
								if ((int)(y+1) < (int)ch_end)
									score[y+1] += 40 * ap_ratio;
								if ((int)(y+2) < (int)ch_end)
									score[y+2] += 30 * ap_ratio;
								if ((int)(y+3) < (int)ch_end)
									score[y+3] += 20 * ap_ratio;
							}	
						}	
						else {
							if ((pBss->t_stamp[1] & 0x6) == 0) {
								score[y] += 500;
							}
							else if ((pBss->t_stamp[1] & 0x4) == 0) {
								score[y] += 500;
								if ((int)(y+1) < (int)ch_end)
									score[y+1] += 500;
							}
							else {	
								score[y] += 500;
								if ((int)(y-1) >= (int)ch_begin)
									score[y-1] += 500;
							}
						}
						break;
					}
				}
			}
		}

#ifdef ACS_DEBUG_INFO//for debug
		printk("\n");
		for (y=ch_begin; y<ch_end; y++)
			printk("6. after ap check: chnl[%d]:%d\n", priv->available_chnl[y],score[y]);
		printk("\n");
#endif		

#ifdef 	SS_CH_LOAD_PROC

		// caculate noise level -- suggested by wilson
		for (y=ch_begin; y<ch_end; y++)  {
			int fa_lv=0, cca_lv=0;
			if (priv->chnl_ss_fa_count[y]>1000) {
				fa_lv = 100;
			} else if (priv->chnl_ss_fa_count[y]>500) {
				fa_lv = 34 * (priv->chnl_ss_fa_count[y]-500) / 500 + 66;
			} else if (priv->chnl_ss_fa_count[y]>200) {
				fa_lv = 33 * (priv->chnl_ss_fa_count[y] - 200) / 300 + 33;
			} else if (priv->chnl_ss_fa_count[y]>100) {
				fa_lv = 18 * (priv->chnl_ss_fa_count[y] - 100) / 100 + 15;
			} else {
				fa_lv = 15 * priv->chnl_ss_fa_count[y] / 100;
			} 
			if (priv->chnl_ss_cca_count[y]>400) {
				cca_lv = 100;
			} else if (priv->chnl_ss_cca_count[y]>200) {
				cca_lv = 34 * (priv->chnl_ss_cca_count[y] - 200) / 200 + 66;
			} else if (priv->chnl_ss_cca_count[y]>80) {
				cca_lv = 33 * (priv->chnl_ss_cca_count[y] - 80) / 120 + 33;
			} else if (priv->chnl_ss_cca_count[y]>40) {
				cca_lv = 18 * (priv->chnl_ss_cca_count[y] - 40) / 40 + 15;
			} else {
				cca_lv = 15 * priv->chnl_ss_cca_count[y] / 40;
			}

			priv->chnl_ss_load[y] = (((fa_lv > cca_lv)? fa_lv : cca_lv)*75+((score[y]>100)?100:score[y])*25)/100;

			DEBUG_INFO("ch:%d f=%d (%d), c=%d (%d), fl=%d, cl=%d, sc=%d, cu=%d\n", 
					priv->available_chnl[y],
					priv->chnl_ss_fa_count[y], fa_thd,
					priv->chnl_ss_cca_count[y], cca_thd,
					fa_lv, 
					cca_lv,
					score[y],					
					priv->chnl_ss_load[y]);
			
		}		
#endif		
	}
#endif

choose_ch:

#ifdef DFS
	// heavy weighted DFS channel
	if (idx_5G_begin >= 0){
		for (i=idx_5G_begin; i<priv->available_chnl_num; i++) {
			if (!priv->pmib->dot11DFSEntry.disable_DFS && is_DFS_channel(priv->available_chnl[i]) 
			&& (score[i]!= 0xffffffff)){
					score[i] += 1600; 
		}
	}
	}
#endif


//prevent Auto Channel selecting wrong channel in 40M mode-----------------
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N)
		&& priv->pshare->is_40m_bw) {
#if 0
		if (GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset == 1) {
			//Upper Primary Channel, cannot select the two lowest channels
			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) {
				score[0] = 0xffffffff;
				score[1] = 0xffffffff;
				score[2] = 0xffffffff;
				score[3] = 0xffffffff;
				score[4] = 0xffffffff;

				score[13] = 0xffffffff;
				score[12] = 0xffffffff;
				score[11] = 0xffffffff;
			}

//			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11A) {
//				score[idx_5G_begin] = 0xffffffff;
//				score[idx_5G_begin + 1] = 0xffffffff;
//			}
		}
		else if (GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset == 2) {
			//Lower Primary Channel, cannot select the two highest channels
			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11G) {
				score[0] = 0xffffffff;
				score[1] = 0xffffffff;
				score[2] = 0xffffffff;

				score[13] = 0xffffffff;
				score[12] = 0xffffffff;
				score[11] = 0xffffffff;
				score[10] = 0xffffffff;
				score[9] = 0xffffffff;
			}

//			if (priv->pmib->dot11BssType.net_work_type & WIRELESS_11A) {
//				score[priv->available_chnl_num - 2] = 0xffffffff;
//				score[priv->available_chnl_num - 1] = 0xffffffff;
//			}
		}
#endif
		for (i=0; i<=idx_2G_end; ++i)
			if (priv->available_chnl[i] == 14)
				score[i] = 0xffffffff;		// mask chan14

#ifdef RTK_5G_SUPPORT
		if (idx_5G_begin >= 0) {
			for (i=idx_5G_begin; i<priv->available_chnl_num; i++) {
				int ch = priv->available_chnl[i];
				if(priv->available_chnl[i] > 144) 
					--ch;
				if((ch%4) || ch==140 || ch == 164 )	//mask ch 140, ch 165, ch 184...
					score[i] = 0xffffffff;
			}
		}
#endif

		
	}

	if (priv->pmib->dot11RFEntry.disable_ch1213) {
		for (i=0; i<=idx_2G_end; ++i) {
			int ch = priv->available_chnl[i];
			if ((ch == 12) || (ch == 13))
				score[i] = 0xffffffff;
		}
	}

	if (((priv->pmib->dot11StationConfigEntry.dot11RegDomain == DOMAIN_GLOBAL) ||
			(priv->pmib->dot11StationConfigEntry.dot11RegDomain == DOMAIN_WORLD_WIDE)) &&
		 (idx_2G_end >= 11) && (idx_2G_end < 14)) {
		score[13] = 0xffffffff;	// mask chan14
		score[12] = 0xffffffff; // mask chan13
		score[11] = 0xffffffff; // mask chan12
	}
	
//------------------------------------------------------------------

#ifdef _DEBUG_RTL8192CD_
	for (i=0; i<priv->available_chnl_num; i++) {
		len += sprintf(tmpbuf+len, "ch%d:%u ", priv->available_chnl[i], score[i]);		
	}
	strcat(tmpbuf, "\n");
	panic_printk("%s", tmpbuf);

#endif

	if ( (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_5G)
		&& (priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_80)) 
	{
		for (i=0; i<priv->available_chnl_num; i++) {
			if (is80MChannel(priv->available_chnl, priv->available_chnl_num, priv->available_chnl[i])) {
				tmpScore = 0;
				for (j=0; j<4; j++) {
					if ((tmpScore != 0xffffffff) && (score[i+j] != 0xffffffff))
						tmpScore += score[i+j];
					else
						tmpScore = 0xffffffff;
				}
				tmpScore = tmpScore / 4;
				if (minScore > tmpScore) {
					minScore = tmpScore;

					tmpScore = 0xffffffff;
					for (j=0; j<4; j++) {
						if (score[i+j] < tmpScore) {
							tmpScore = score[i+j];
							tmpIdx = i+j;
						}
					}

					idx = tmpIdx;
				}
				i += 3;
			}
		}
		if (minScore == 0xffffffff) {
			// there is no 80M channels
			priv->pshare->is_40m_bw = HT_CHANNEL_WIDTH_20;
			for (i=0; i<priv->available_chnl_num; i++) {
				if (score[i] < minScore) {
					minScore = score[i];
					idx = i;
				}
			}
		}
	}
	else if( (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_5G)
			&& (priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_20_40))
 	{
		for (i=0; i<priv->available_chnl_num; i++) {
			if(is40MChannel(priv->available_chnl,priv->available_chnl_num,priv->available_chnl[i])) {
				tmpScore = 0;
				for(j=0;j<2;j++) {
					if ((tmpScore != 0xffffffff) && (score[i+j] != 0xffffffff))
						tmpScore += score[i+j];
					else
						tmpScore = 0xffffffff;
				}
				tmpScore = tmpScore / 2;
				if(minScore > tmpScore) {
					minScore = tmpScore;

					tmpScore = 0xffffffff;
					for (j=0; j<2; j++) {
						if (score[i+j] < tmpScore) {
							tmpScore = score[i+j];
							tmpIdx = i+j;
						}
					}

					idx = tmpIdx;
				}
				i += 1;
			}
		}
		if (minScore == 0xffffffff) {
			// there is no 40M channels
			priv->pshare->is_40m_bw = HT_CHANNEL_WIDTH_20;
			for (i=0; i<priv->available_chnl_num; i++) {
				if (score[i] < minScore) {
					minScore = score[i];
					idx = i;
				}
			}
		}
	}
	else if( (priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_2G)
			&& (priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_20_40)
			&& (priv->available_chnl_num >= 8) )
	{
		u4Byte groupScore[14];

		memset(groupScore, 0xff , sizeof(groupScore));
		for (i=0; i<priv->available_chnl_num-4; i++) {
			if (score[i] != 0xffffffff && score[i+4] != 0xffffffff) {
				groupScore[i] = score[i] + score[i+4];
				DEBUG_INFO("groupScore, ch %d,%d: %d\n", i+1, i+5, groupScore[i]);
				if (groupScore[i] < minScore) {
#ifdef AUTOCH_SS_SPEEDUP
					if(priv->pmib->miscEntry.autoch_1611_enable)
					{
						if(priv->available_chnl[i]==1 || priv->available_chnl[i]==6 || priv->available_chnl[i]==11)
						{
							minScore = groupScore[i];
							idx = i;
						}
					}
					else
#endif
					{					
						minScore = groupScore[i];
						idx = i;
					}
				}
			}
		}

		if (score[idx] < score[idx+4]) {
			GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
			priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;			
		} else {
			idx = idx + 4;
			GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
			priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;			
		}
	}
	else 
	{
		for (i=0; i<priv->available_chnl_num; i++) {
			if (score[i] < minScore) {
#ifdef AUTOCH_SS_SPEEDUP
				if(priv->pmib->miscEntry.autoch_1611_enable)
				{
					if(priv->available_chnl[i]==1 || priv->available_chnl[i]==6 || priv->available_chnl[i]==11)
					{
						minScore = score[i];
						idx = i;
					}
				}
				else
#endif
				{				
					minScore = score[i];
					idx = i;
				}
			}
		}
	}

	if (IS_A_CUT_8881A(priv) &&
		(priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_80)) {
		if ((priv->available_chnl[idx] == 36) ||
			(priv->available_chnl[idx] == 52) ||
			(priv->available_chnl[idx] == 100) ||
			(priv->available_chnl[idx] == 116) ||
			(priv->available_chnl[idx] == 132) ||
			(priv->available_chnl[idx] == 149) ||
			(priv->available_chnl[idx] == 165))
			idx++;
		else if ((priv->available_chnl[idx] == 48) ||
			(priv->available_chnl[idx] == 64) ||
			(priv->available_chnl[idx] == 112) ||
			(priv->available_chnl[idx] == 128) ||
			(priv->available_chnl[idx] == 144) ||
			(priv->available_chnl[idx] == 161) ||
			(priv->available_chnl[idx] == 177))
			idx--;
	}

	minChan = priv->available_chnl[idx];

	// skip channel 14 if don't support ofdm
	if ((priv->pmib->dot11RFEntry.disable_ch14_ofdm) &&
			(minChan == 14)) {
		score[idx] = 0xffffffff;
		
		minScore = 0xffffffff;
		for (i=0; i<priv->available_chnl_num; i++) {
			if (score[i] < minScore) {
				minScore = score[i];
				idx = i;
			}
		}
		minChan = priv->available_chnl[idx];
	}

#if 0
	//Check if selected channel available for 80M/40M BW or NOT ?
	if(priv->pmib->dot11RFEntry.phyBandSelect == PHY_BAND_5G)
	{
		if(priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_80)
		{
			if(!is80MChannel(priv->available_chnl,priv->available_chnl_num,minChan))
			{
				//printk("BW=80M, selected channel = %d is unavaliable! reduce to 40M\n", minChan);
				//priv->pmib->dot11nConfigEntry.dot11nUse40M = HT_CHANNEL_WIDTH_20_40;
				priv->pshare->is_40m_bw = HT_CHANNEL_WIDTH_20_40;
			}
		}
			
		if(priv->pmib->dot11nConfigEntry.dot11nUse40M == HT_CHANNEL_WIDTH_20_40)
		{
			if(!is40MChannel(priv->available_chnl,priv->available_chnl_num,minChan))
			{
				//printk("BW=40M, selected channel = %d is unavaliable! reduce to 20M\n", minChan);
				//priv->pmib->dot11nConfigEntry.dot11nUse40M = HT_CHANNEL_WIDTH_20;
				priv->pshare->is_40m_bw = HT_CHANNEL_WIDTH_20;
			}
		}
	}
#endif

#ifdef CONFIG_RTL_NEW_AUTOCH
	RTL_W32(RXERR_RPT, RXERR_RPT_RST);
#endif

// auto adjust contro-sideband
	if ((priv->pmib->dot11BssType.net_work_type & WIRELESS_11N)
			&& (priv->pshare->is_40m_bw ==1 || priv->pshare->is_40m_bw ==2)) {

#ifdef RTK_5G_SUPPORT
		if (priv->pmib->dot11RFEntry.phyBandSelect & PHY_BAND_5G) {
			if( (minChan>144) ? ((minChan-1)%8) : (minChan%8)) {
				GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
				priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
			} else {
				GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
				priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
			}

		} else
#endif		
		{
#if 0
#ifdef CONFIG_RTL_NEW_AUTOCH
			unsigned int ch_max;

			if (priv->available_chnl[idx_2G_end] >= 13)
				ch_max = 13;
			else
				ch_max = priv->available_chnl[idx_2G_end];

			if ((minChan >= 5) && (minChan <= (ch_max-5))) {
				if (score[minChan+4] > score[minChan-4]) { // what if some channels were cancelled?
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
				} else {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
				}
			} else
#endif
			{
				if (minChan < 5) {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_ABOVE;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_ABOVE;
				}
				else if (minChan > 7) {
					GET_MIB(priv)->dot11nConfigEntry.dot11n2ndChOffset = HT_2NDCH_OFFSET_BELOW;
					priv->pshare->offset_2nd_chan	= HT_2NDCH_OFFSET_BELOW;
				}
			}
#endif
		}
	}
//-----------------------

#if defined(__ECOS) && defined(CONFIG_SDIO_HCI)
	panic_printk("Auto channel choose ch:%d\n", minChan);
#else
#ifdef _DEBUG_RTL8192CD_
	panic_printk("Auto channel choose ch:%d\n", minChan);
#endif
#endif
#ifdef ACS_DEBUG_INFO//for debug
	printk("7. minChan:%d 2nd_offset:%d\n", minChan, priv->pshare->offset_2nd_chan);
#endif

	return minChan;
}
*/

#endif

VOID
phydm_CLMInit(
	IN		PVOID			pDM_VOID,
	IN		u2Byte			sampleNum			/*unit : 4us*/
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;		

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_TIME_PERIOD_11AC, bMaskLWord, sampleNum);	/*4us sample 1 time*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT8, 0x1);							/*Enable CCX for CLM*/
	} else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_TIME_PERIOD_11N, bMaskLWord, sampleNum);	/*4us sample 1 time*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT8, 0x1);								/*Enable CCX for CLM*/	
	}

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("[%s] : CLM sampleNum = %d\n", __func__, sampleNum));
		
}

VOID
phydm_CLMtrigger(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT0, 0x0);	/*Trigger CLM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11AC, BIT0, 0x1);
	} else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES) {
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT0, 0x0);	/*Trigger CLM*/
		ODM_SetBBReg(pDM_Odm, ODM_REG_CLM_11N, BIT0, 0x1);
	}
}

BOOLEAN
phydm_checkCLMready(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32 = 0;
	BOOLEAN			ret = FALSE;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11AC, bMaskDWord);				/*make sure CLM calc is ready*/
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_READY_11N, bMaskDWord);				/*make sure CLM calc is ready*/

	if (value32 & BIT16)
		ret = TRUE;
	else
		ret = FALSE;

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("[%s] : CLM ready = %d\n", __func__, ret));

	return ret;
}

u2Byte
phydm_getCLMresult(
	IN		PVOID			pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte			value32 = 0;
	u2Byte			results = 0;
	
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11AC, bMaskDWord);				/*read CLM calc result*/
	else if (pDM_Odm->SupportICType & ODM_IC_11N_SERIES)
		value32 = ODM_GetBBReg(pDM_Odm, ODM_REG_CLM_RESULT_11N, bMaskDWord);				/*read CLM calc result*/

	results = (u2Byte)(value32 & bMaskLWord);

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_ACS, ODM_DBG_LOUD, ("[%s] : CLM result = %d\n", __func__, results));
	
	return results;
/*results are number of CCA times in sampleNum*/
}

