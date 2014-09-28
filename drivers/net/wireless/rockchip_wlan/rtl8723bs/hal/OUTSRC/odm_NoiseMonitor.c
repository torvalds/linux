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
#include "odm_precomp.h"

//=================================================
// This function is for inband noise test utility only
// To obtain the inband noise level(dbm), do the following.
// 1. disable DIG and Power Saving 
// 2. Set initial gain = 0x1a
// 3. Stop updating idle time pwer report (for driver read)
//	- 0x80c[25]
//
//=================================================

#define Valid_Min				-35
#define Valid_Max			10
#define ValidCnt				5	

s2Byte odm_InbandNoise_Monitor_NSeries(PDM_ODM_T	pDM_Odm,u8 bPauseDIG,u8 IGIValue,u32 max_time)
{
	u4Byte				tmp4b;	
	u1Byte				max_rf_path=0,rf_path;	
	u1Byte				reg_c50, reg_c58,valid_done=0;	
	struct noise_level		noise_data;
	u32 start  = 0, 	func_start=0,	func_end = 0;

	func_start = ODM_GetCurrentTime(pDM_Odm);
	pDM_Odm->noise_level.noise_all = 0;
	
	if((pDM_Odm->RFType == ODM_1T2R) ||(pDM_Odm->RFType == ODM_2T2R))	
		max_rf_path = 2;
	else
		max_rf_path = 1;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("odm_DebugControlInbandNoise_Nseries() ==> \n"));

	ODM_Memory_Set(pDM_Odm,&noise_data,0,sizeof(struct noise_level));
	
	//
	// Step 1. Disable DIG && Set initial gain.
	//
	
	if(bPauseDIG)
	{
		odm_PauseDIG(pDM_Odm,ODM_PAUSE_DIG,IGIValue);
	}
	//
	// Step 2. Disable all power save for read registers
	//
	//dcmd_DebugControlPowerSave(pAdapter, PSDisable);

	//
	// Step 3. Get noise power level
	//
	start = ODM_GetCurrentTime(pDM_Odm);
	while(1)
	{
		
		//Stop updating idle time pwer report (for driver read)
		ODM_SetBBReg(pDM_Odm, rFPGA0_TxGainStage, BIT25, 1);	
		
		//Read Noise Floor Report
		tmp4b = ODM_GetBBReg(pDM_Odm, 0x8f8,bMaskDWord );
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("Noise Floor Report (0x8f8) = 0x%08x\n", tmp4b));
		
		//ODM_SetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0, TestInitialGain);
		//if(max_rf_path == 2)
		//	ODM_SetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0, TestInitialGain);
		
		//update idle time pwer report per 5us
		ODM_SetBBReg(pDM_Odm, rFPGA0_TxGainStage, BIT25, 0);
		
		noise_data.value[ODM_RF_PATH_A] = (u1Byte)(tmp4b&0xff);		
		noise_data.value[ODM_RF_PATH_B]  = (u1Byte)((tmp4b&0xff00)>>8);
	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("value_a = 0x%x(%d), value_b = 0x%x(%d)\n", 
			noise_data.value[ODM_RF_PATH_A], noise_data.value[ODM_RF_PATH_A], noise_data.value[ODM_RF_PATH_B], noise_data.value[ODM_RF_PATH_B]));

		 for(rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) 
		 {
		 	noise_data.sval[rf_path] = (s1Byte)noise_data.value[rf_path];
			noise_data.sval[rf_path] /= 2;
		 }	
		 	
		
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("sval_a = %d, sval_b = %d\n", 
			noise_data.sval[ODM_RF_PATH_A], noise_data.sval[ODM_RF_PATH_B]));
		//ODM_delay_ms(10);
		//ODM_sleep_ms(10);

		for(rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) 
		{
			if( (noise_data.valid_cnt[rf_path] < ValidCnt) && (noise_data.sval[rf_path] < Valid_Max && noise_data.sval[rf_path] >= Valid_Min))
			{
				noise_data.valid_cnt[rf_path]++;
				noise_data.sum[rf_path] += noise_data.sval[rf_path];
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("RF_Path:%d Valid sval = %d\n", rf_path,noise_data.sval[rf_path]));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("Sum of sval = %d, \n", noise_data.sum[rf_path]));
				if(noise_data.valid_cnt[rf_path] == ValidCnt)
				{				
					valid_done++;
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("After divided, RF_Path:%d ,sum = %d \n", rf_path,noise_data.sum[rf_path]));
				}				
			
			}
			
		}

		//printk("####### valid_done:%d #############\n",valid_done);
		if ((valid_done==max_rf_path) || (ODM_GetProgressingTime(pDM_Odm,start) > max_time))
		{
			for(rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++)
			{ 
				//printk("%s PATH_%d - sum = %d, valid_cnt = %d \n",__FUNCTION__,rf_path,noise_data.sum[rf_path], noise_data.valid_cnt[rf_path]);
				if(noise_data.valid_cnt[rf_path])
					noise_data.sum[rf_path] /= noise_data.valid_cnt[rf_path];		
				else
					noise_data.sum[rf_path]  = 0;
			}
			break;
		}
	}
	reg_c50 = (s4Byte)ODM_GetBBReg(pDM_Odm,rOFDM0_XAAGCCore1,bMaskByte0);
	reg_c50 &= ~BIT7;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("0x%x = 0x%02x(%d)\n", rOFDM0_XAAGCCore1, reg_c50, reg_c50));
	pDM_Odm->noise_level.noise[ODM_RF_PATH_A] = -110 + reg_c50 + noise_data.sum[ODM_RF_PATH_A];
	pDM_Odm->noise_level.noise_all += pDM_Odm->noise_level.noise[ODM_RF_PATH_A];
		
	if(max_rf_path == 2){
		reg_c58 = (s4Byte)ODM_GetBBReg(pDM_Odm,rOFDM0_XBAGCCore1,bMaskByte0);
		reg_c58 &= ~BIT7;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("0x%x = 0x%02x(%d)\n", rOFDM0_XBAGCCore1, reg_c58, reg_c58));
		pDM_Odm->noise_level.noise[ODM_RF_PATH_B] = -110 + reg_c58 + noise_data.sum[ODM_RF_PATH_B];
		pDM_Odm->noise_level.noise_all += pDM_Odm->noise_level.noise[ODM_RF_PATH_B];
	}
	pDM_Odm->noise_level.noise_all /= max_rf_path;
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("noise_a = %d, noise_b = %d\n", 
		pDM_Odm->noise_level.noise[ODM_RF_PATH_A],
		pDM_Odm->noise_level.noise[ODM_RF_PATH_B]));

	//
	// Step 4. Recover the Dig
	//
	if(bPauseDIG)
	{
		odm_PauseDIG(pDM_Odm,ODM_RESUME_DIG,IGIValue);
	}	
	func_end = ODM_GetProgressingTime(pDM_Odm,func_start) ;	
	//printk("%s noise_a = %d, noise_b = %d noise_all:%d (%d ms)\n",__FUNCTION__,
	//	pDM_Odm->noise_level.noise[ODM_RF_PATH_A],
	//	pDM_Odm->noise_level.noise[ODM_RF_PATH_B],
	//	pDM_Odm->noise_level.noise_all,func_end);	
	
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("odm_DebugControlInbandNoise_Nseries() <== \n"));
	return pDM_Odm->noise_level.noise_all;

}
s2Byte ODM_InbandNoise_Monitor(PVOID pDM_VOID,u8 bPauseDIG,u8 IGIValue,u32 max_time)
{

	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if(pDM_Odm->SupportICType & ODM_IC_11AC_SERIES )
	{
		//odm_InbandNoise_Monitor_JaguarSeries(pDM_Odm,bPauseDIG,IGIValue,max_time);
		return 0;
	}
	else
	{
		return odm_InbandNoise_Monitor_NSeries(pDM_VOID,bPauseDIG,IGIValue,max_time);
	}
}



