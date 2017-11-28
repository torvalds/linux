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

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"
#include "phydm_noisemonitor.h"

/* *************************************************
 * This function is for inband noise test utility only
 * To obtain the inband noise level(dbm), do the following.
 * 1. disable DIG and Power Saving
 * 2. Set initial gain = 0x1a
 * 3. Stop updating idle time pwer report (for driver read)
 *	- 0x80c[25]
 *
 * ************************************************* */

#define VALID_MIN				-35
#define VALID_MAX			10
#define VALID_CNT				5

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_WIN))

s16 odm_inband_noise_monitor_n_series(struct PHY_DM_STRUCT	*p_dm_odm, u8 is_pause_dig, u8 igi_value, u32 max_time)
{
	u32				tmp4b;
	u8				max_rf_path = 0, rf_path;
	u8				reg_c50, reg_c58, valid_done = 0;
	struct noise_level		noise_data;
	u64	start  = 0, func_start = 0,	func_end = 0;

	func_start = odm_get_current_time(p_dm_odm);
	p_dm_odm->noise_level.noise_all = 0;

	if ((p_dm_odm->rf_type == ODM_1T2R) || (p_dm_odm->rf_type == ODM_2T2R))
		max_rf_path = 2;
	else
		max_rf_path = 1;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_DebugControlInbandNoise_Nseries() ==>\n"));

	odm_memory_set(p_dm_odm, &noise_data, 0, sizeof(struct noise_level));

	/*  */
	/* step 1. Disable DIG && Set initial gain. */
	/*  */

	if (is_pause_dig)
		odm_pause_dig(p_dm_odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);
	/*  */
	/* step 2. Disable all power save for read registers */
	/*  */
	/* dcmd_DebugControlPowerSave(p_adapter, PSDisable); */

	/*  */
	/* step 3. Get noise power level */
	/*  */
	start = odm_get_current_time(p_dm_odm);
	while (1) {

		/* Stop updating idle time pwer report (for driver read) */
		odm_set_bb_reg(p_dm_odm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 1);

		/* Read Noise Floor Report */
		tmp4b = odm_get_bb_reg(p_dm_odm, 0x8f8, MASKDWORD);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Noise Floor Report (0x8f8) = 0x%08x\n", tmp4b));

		/* odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XA_AGC_CORE1, MASKBYTE0, TestInitialGain); */
		/* if(max_rf_path == 2) */
		/*	odm_set_bb_reg(p_dm_odm, REG_OFDM_0_XB_AGC_CORE1, MASKBYTE0, TestInitialGain); */

		/* update idle time pwer report per 5us */
		odm_set_bb_reg(p_dm_odm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 0);

		noise_data.value[ODM_RF_PATH_A] = (u8)(tmp4b & 0xff);
		noise_data.value[ODM_RF_PATH_B]  = (u8)((tmp4b & 0xff00) >> 8);

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("value_a = 0x%x(%d), value_b = 0x%x(%d)\n",
			noise_data.value[ODM_RF_PATH_A], noise_data.value[ODM_RF_PATH_A], noise_data.value[ODM_RF_PATH_B], noise_data.value[ODM_RF_PATH_B]));

		for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			noise_data.sval[rf_path] = (s8)noise_data.value[rf_path];
			noise_data.sval[rf_path] /= 2;
		}


		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("sval_a = %d, sval_b = %d\n",
			noise_data.sval[ODM_RF_PATH_A], noise_data.sval[ODM_RF_PATH_B]));
		/* ODM_delay_ms(10); */
		/* ODM_sleep_ms(10); */

		for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			if ((noise_data.valid_cnt[rf_path] < VALID_CNT) && (noise_data.sval[rf_path] < VALID_MAX && noise_data.sval[rf_path] >= VALID_MIN)) {
				noise_data.valid_cnt[rf_path]++;
				noise_data.sum[rf_path] += noise_data.sval[rf_path];
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("rf_path:%d Valid sval = %d\n", rf_path, noise_data.sval[rf_path]));
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Sum of sval = %d,\n", noise_data.sum[rf_path]));
				if (noise_data.valid_cnt[rf_path] == VALID_CNT) {
					valid_done++;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("After divided, rf_path:%d,sum = %d\n", rf_path, noise_data.sum[rf_path]));
				}

			}

		}

		/* printk("####### valid_done:%d #############\n",valid_done); */
		if ((valid_done == max_rf_path) || (odm_get_progressing_time(p_dm_odm, start) > max_time)) {
			for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) {
				/* printk("%s PATH_%d - sum = %d, VALID_CNT = %d\n",__FUNCTION__,rf_path,noise_data.sum[rf_path], noise_data.valid_cnt[rf_path]); */
				if (noise_data.valid_cnt[rf_path])
					noise_data.sum[rf_path] /= noise_data.valid_cnt[rf_path];
				else
					noise_data.sum[rf_path]  = 0;
			}
			break;
		}
	}
	reg_c50 = (u8)odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XA_AGC_CORE1, MASKBYTE0);
	reg_c50 &= ~BIT(7);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("0x%x = 0x%02x(%d)\n", REG_OFDM_0_XA_AGC_CORE1, reg_c50, reg_c50));
	p_dm_odm->noise_level.noise[ODM_RF_PATH_A] = (u8)(-110 + reg_c50 + noise_data.sum[ODM_RF_PATH_A]);
	p_dm_odm->noise_level.noise_all += p_dm_odm->noise_level.noise[ODM_RF_PATH_A];

	if (max_rf_path == 2) {
		reg_c58 = (u8)odm_get_bb_reg(p_dm_odm, REG_OFDM_0_XB_AGC_CORE1, MASKBYTE0);
		reg_c58 &= ~BIT(7);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("0x%x = 0x%02x(%d)\n", REG_OFDM_0_XB_AGC_CORE1, reg_c58, reg_c58));
		p_dm_odm->noise_level.noise[ODM_RF_PATH_B] = (u8)(-110 + reg_c58 + noise_data.sum[ODM_RF_PATH_B]);
		p_dm_odm->noise_level.noise_all += p_dm_odm->noise_level.noise[ODM_RF_PATH_B];
	}
	p_dm_odm->noise_level.noise_all /= max_rf_path;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("noise_a = %d, noise_b = %d\n",
			p_dm_odm->noise_level.noise[ODM_RF_PATH_A],
			p_dm_odm->noise_level.noise[ODM_RF_PATH_B]));

	/*  */
	/* step 4. Recover the Dig */
	/*  */
	if (is_pause_dig)
		odm_pause_dig(p_dm_odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);
	func_end = odm_get_progressing_time(p_dm_odm, func_start) ;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_DebugControlInbandNoise_Nseries() <==\n"));
	return p_dm_odm->noise_level.noise_all;

}

s16
odm_inband_noise_monitor_ac_series(struct PHY_DM_STRUCT	*p_dm_odm, u8 is_pause_dig, u8 igi_value, u32 max_time
				  )
{
	s32          rxi_buf_anta, rxq_buf_anta; /*rxi_buf_antb, rxq_buf_antb;*/
	s32	        value32, pwdb_A = 0, sval, noise, sum;
	bool	        pd_flag;
	u8			i, valid_cnt;
	u64	start = 0, func_start = 0, func_end = 0;


	if (!(p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8814A)))
		return 0;

	func_start = odm_get_current_time(p_dm_odm);
	p_dm_odm->noise_level.noise_all = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_inband_noise_monitor_ac_series() ==>\n"));

	/* step 1. Disable DIG && Set initial gain. */
	if (is_pause_dig)
		odm_pause_dig(p_dm_odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);

	/* step 2. Disable all power save for read registers */
	/*dcmd_DebugControlPowerSave(p_adapter, PSDisable); */

	/* step 3. Get noise power level */
	start = odm_get_current_time(p_dm_odm);

	/* reset counters */
	sum = 0;
	valid_cnt = 0;

	/* step 3. Get noise power level */
	while (1) {
		/*Set IGI=0x1C */
		odm_write_dig(p_dm_odm, 0x1C);
		/*stop CK320&CK88 */
		odm_set_bb_reg(p_dm_odm, 0x8B4, BIT(6), 1);
		/*Read path-A */
		odm_set_bb_reg(p_dm_odm, 0x8FC, MASKDWORD, 0x200); /*set debug port*/
		value32 = odm_get_bb_reg(p_dm_odm, 0xFA0, MASKDWORD); /*read debug port*/

		rxi_buf_anta = (value32 & 0xFFC00) >> 10; /*rxi_buf_anta=RegFA0[19:10]*/
		rxq_buf_anta = value32 & 0x3FF; /*rxq_buf_anta=RegFA0[19:10]*/

		pd_flag = (bool)((value32 & BIT(31)) >> 31);

		/*Not in packet detection period or Tx state */
		if ((!pd_flag) || (rxi_buf_anta != 0x200)) {
			/*sign conversion*/
			rxi_buf_anta = odm_sign_conversion(rxi_buf_anta, 10);
			rxq_buf_anta = odm_sign_conversion(rxq_buf_anta, 10);

			pwdb_A = odm_pwdb_conversion(rxi_buf_anta * rxi_buf_anta + rxq_buf_anta * rxq_buf_anta, 20, 18); /*S(10,9)*S(10,9)=S(20,18)*/

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("pwdb_A= %d dB, rxi_buf_anta= 0x%x, rxq_buf_anta= 0x%x\n", pwdb_A, rxi_buf_anta & 0x3FF, rxq_buf_anta & 0x3FF));
		}
		/*Start CK320&CK88*/
		odm_set_bb_reg(p_dm_odm, 0x8B4, BIT(6), 0);
		/*BB Reset*/
		odm_write_1byte(p_dm_odm, 0x02, odm_read_1byte(p_dm_odm, 0x02) & (~BIT(0)));
		odm_write_1byte(p_dm_odm, 0x02, odm_read_1byte(p_dm_odm, 0x02) | BIT(0));
		/*PMAC Reset*/
		odm_write_1byte(p_dm_odm, 0xB03, odm_read_1byte(p_dm_odm, 0xB03) & (~BIT(0)));
		odm_write_1byte(p_dm_odm, 0xB03, odm_read_1byte(p_dm_odm, 0xB03) | BIT(0));
		/*CCK Reset*/
		if (odm_read_1byte(p_dm_odm, 0x80B) & BIT(4)) {
			odm_write_1byte(p_dm_odm, 0x80B, odm_read_1byte(p_dm_odm, 0x80B) & (~BIT(4)));
			odm_write_1byte(p_dm_odm, 0x80B, odm_read_1byte(p_dm_odm, 0x80B) | BIT(4));
		}

		sval = pwdb_A;

		if (sval < 0 && sval >= -27) {
			if (valid_cnt < VALID_CNT) {
				valid_cnt++;
				sum += sval;
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Valid sval = %d\n", sval));
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Sum of sval = %d,\n", sum));
				if ((valid_cnt >= VALID_CNT) || (odm_get_progressing_time(p_dm_odm, start) > max_time)) {
					sum /= VALID_CNT;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("After divided, sum = %d\n", sum));
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
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("noise = %d\n", noise));
	p_dm_odm->noise_level.noise_all = (s16)noise;

	/* step 4. Recover the Dig*/
	if (is_pause_dig)
		odm_pause_dig(p_dm_odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);

	func_end = odm_get_progressing_time(p_dm_odm, func_start);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("odm_inband_noise_monitor_ac_series() <==\n"));

	return p_dm_odm->noise_level.noise_all;
}



s16
odm_inband_noise_monitor(void *p_dm_void, u8 is_pause_dig, u8 igi_value, u32 max_time)
{

	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES)
		return odm_inband_noise_monitor_ac_series(p_dm_odm, is_pause_dig, igi_value, max_time);
	else
		return odm_inband_noise_monitor_n_series(p_dm_odm, is_pause_dig, igi_value, max_time);
}

#endif
