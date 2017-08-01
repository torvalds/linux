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
#include "phydm_noisemonitor.h"

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

#if (DM_ODM_SUPPORT_TYPE &  (ODM_CE|ODM_WIN))

s2Byte odm_InbandNoise_Monitor_NSeries(PDM_ODM_T	pDM_Odm,u8 bPauseDIG,u8 IGIValue,u32 max_time)
{
	u4Byte				tmp4b;	
	u1Byte				max_rf_path=0,rf_path;	
	u1Byte				reg_c50, reg_c58,valid_done=0;	
	struct noise_level		noise_data;
	u8Byte	start  = 0, func_start = 0,	func_end = 0;

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
		odm_PauseDIG(pDM_Odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, IGIValue);
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
	reg_c50 = (u1Byte)ODM_GetBBReg(pDM_Odm, rOFDM0_XAAGCCore1, bMaskByte0);
	reg_c50 &= ~BIT7;
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("0x%x = 0x%02x(%d)\n", rOFDM0_XAAGCCore1, reg_c50, reg_c50));
	pDM_Odm->noise_level.noise[ODM_RF_PATH_A] = (u1Byte)(-110 + reg_c50 + noise_data.sum[ODM_RF_PATH_A]);
	pDM_Odm->noise_level.noise_all += pDM_Odm->noise_level.noise[ODM_RF_PATH_A];
		
	if(max_rf_path == 2){
		reg_c58 = (u1Byte)ODM_GetBBReg(pDM_Odm, rOFDM0_XBAGCCore1, bMaskByte0);
		reg_c58 &= ~BIT7;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_COMMON, ODM_DBG_LOUD,("0x%x = 0x%02x(%d)\n", rOFDM0_XBAGCCore1, reg_c58, reg_c58));
		pDM_Odm->noise_level.noise[ODM_RF_PATH_B] = (u1Byte)(-110 + reg_c58 + noise_data.sum[ODM_RF_PATH_B]);
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
		odm_PauseDIG(pDM_Odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, IGIValue);
	}	
	func_end = ODM_GetProgressingTime(pDM_Odm,func_start) ;	
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_DebugControlInbandNoise_Nseries() <==\n"));
	return pDM_Odm->noise_level.noise_all;

}

s2Byte	
odm_InbandNoise_Monitor_ACSeries(PDM_ODM_T	pDM_Odm, u8 bPauseDIG, u8 IGIValue, u32 max_time
	)
{
	s4Byte          rxi_buf_anta, rxq_buf_anta; /*rxi_buf_antb, rxq_buf_antb;*/
	s4Byte	        value32, pwdb_A = 0, sval, noise, sum;
	BOOLEAN	        pd_flag;
	u1Byte			i, valid_cnt;
	u8Byte	start = 0, func_start = 0, func_end = 0;


	if (!(pDM_Odm->SupportICType & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8814A)))
		return 0;
	
	func_start = ODM_GetCurrentTime(pDM_Odm);
	pDM_Odm->noise_level.noise_all = 0;
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_InbandNoise_Monitor_ACSeries() ==>\n"));
	
	/* Step 1. Disable DIG && Set initial gain. */
	if (bPauseDIG)
		odm_PauseDIG(pDM_Odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, IGIValue);

	/* Step 2. Disable all power save for read registers */
	/*dcmd_DebugControlPowerSave(pAdapter, PSDisable); */

	/* Step 3. Get noise power level */
	start = ODM_GetCurrentTime(pDM_Odm);

	/* reset counters */
	sum = 0;
	valid_cnt = 0;

	/* Step 3. Get noise power level */
	while (1) {
		/*Set IGI=0x1C */
		ODM_Write_DIG(pDM_Odm, 0x1C);
		/*stop CK320&CK88 */
		ODM_SetBBReg(pDM_Odm, 0x8B4, BIT6, 1);
		/*Read Path-A */
		ODM_SetBBReg(pDM_Odm, 0x8FC, bMaskDWord, 0x200); /*set debug port*/
		value32 = ODM_GetBBReg(pDM_Odm, 0xFA0, bMaskDWord); /*read debug port*/
		
		rxi_buf_anta = (value32 & 0xFFC00) >> 10; /*rxi_buf_anta=RegFA0[19:10]*/
		rxq_buf_anta = value32 & 0x3FF; /*rxq_buf_anta=RegFA0[19:10]*/

		pd_flag = (BOOLEAN) ((value32 & BIT31) >> 31);

		/*Not in packet detection period or Tx state */
		if ((!pd_flag) || (rxi_buf_anta != 0x200)) {
			/*sign conversion*/
			rxi_buf_anta = ODM_SignConversion(rxi_buf_anta, 10);
			rxq_buf_anta = ODM_SignConversion(rxq_buf_anta, 10);

			pwdb_A = ODM_PWdB_Conversion(rxi_buf_anta * rxi_buf_anta + rxq_buf_anta * rxq_buf_anta, 20, 18); /*S(10,9)*S(10,9)=S(20,18)*/

			ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("pwdb_A= %d dB, rxi_buf_anta= 0x%x, rxq_buf_anta= 0x%x\n", pwdb_A, rxi_buf_anta & 0x3FF, rxq_buf_anta & 0x3FF));
		}
		/*Start CK320&CK88*/
		ODM_SetBBReg(pDM_Odm, 0x8B4, BIT6, 0);
		/*BB Reset*/
		ODM_Write1Byte(pDM_Odm, 0x02, ODM_Read1Byte(pDM_Odm, 0x02) & (~BIT0));
		ODM_Write1Byte(pDM_Odm, 0x02, ODM_Read1Byte(pDM_Odm, 0x02) | BIT0);
		/*PMAC Reset*/
		ODM_Write1Byte(pDM_Odm, 0xB03, ODM_Read1Byte(pDM_Odm, 0xB03) & (~BIT0));
		ODM_Write1Byte(pDM_Odm, 0xB03, ODM_Read1Byte(pDM_Odm, 0xB03) | BIT0);
		/*CCK Reset*/
		if (ODM_Read1Byte(pDM_Odm, 0x80B) & BIT4) {
			ODM_Write1Byte(pDM_Odm, 0x80B, ODM_Read1Byte(pDM_Odm, 0x80B) & (~BIT4));
			ODM_Write1Byte(pDM_Odm, 0x80B, ODM_Read1Byte(pDM_Odm, 0x80B) | BIT4);		
		}

		sval = pwdb_A;

		if (sval < 0 && sval >= -27) {
			if (valid_cnt < ValidCnt) {
				valid_cnt++;
				sum += sval;
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Valid sval = %d\n", sval));
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Sum of sval = %d,\n", sum));
				if ((valid_cnt >= ValidCnt) || (ODM_GetProgressingTime(pDM_Odm, start) > max_time)) {
					sum /= valid_cnt;
					ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("After divided, sum = %d\n", sum)); 
					break;
				}
			}
		}
	}

	/*ADC backoff is 12dB,*/ 
	/*Ptarget=0x1C-110=-82dBm*/
	noise = sum + 12 + 0x1C - 110; 
	
	/*Offset*/
	noise = noise - 3;
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("noise = %d\n", noise));
	pDM_Odm->noise_level.noise_all = (s2Byte)noise;

	/* Step 4. Recover the Dig*/
	if (bPauseDIG)
		odm_PauseDIG(pDM_Odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, IGIValue);
	
	func_end = ODM_GetProgressingTime(pDM_Odm, func_start);
	
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_InbandNoise_Monitor_ACSeries() <==\n"));

	return pDM_Odm->noise_level.noise_all;
}



s2Byte
ODM_InbandNoise_Monitor(PVOID pDM_VOID, u8 bPauseDIG, u8 IGIValue, u32 max_time)
{

	PDM_ODM_T	pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if (pDM_Odm->SupportICType & ODM_IC_11AC_SERIES)
		return odm_InbandNoise_Monitor_ACSeries(pDM_Odm, bPauseDIG, IGIValue, max_time);
	else
		return odm_InbandNoise_Monitor_NSeries(pDM_Odm, bPauseDIG, IGIValue, max_time);
}

#endif


