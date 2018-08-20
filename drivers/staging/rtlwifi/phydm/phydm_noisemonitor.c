// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/* ************************************************************
 * include files
 * *************************************************************/
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
 * **************************************************/

#define VALID_MIN -35
#define VALID_MAX 10
#define VALID_CNT 5

static inline void phydm_set_noise_data_sum(struct noise_level *noise_data,
					    u8 max_rf_path)
{
	u8 rf_path;

	for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path; rf_path++) {
		if (noise_data->valid_cnt[rf_path])
			noise_data->sum[rf_path] /=
				noise_data->valid_cnt[rf_path];
		else
			noise_data->sum[rf_path] = 0;
	}
}

static s16 odm_inband_noise_monitor_n_series(struct phy_dm_struct *dm,
					     u8 is_pause_dig, u8 igi_value,
					     u32 max_time)
{
	u32 tmp4b;
	u8 max_rf_path = 0, rf_path;
	u8 reg_c50, reg_c58, valid_done = 0;
	struct noise_level noise_data;
	u64 start = 0, func_start = 0, func_end = 0;

	func_start = odm_get_current_time(dm);
	dm->noise_level.noise_all = 0;

	if ((dm->rf_type == ODM_1T2R) || (dm->rf_type == ODM_2T2R))
		max_rf_path = 2;
	else
		max_rf_path = 1;

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "%s() ==>\n", __func__);

	odm_memory_set(dm, &noise_data, 0, sizeof(struct noise_level));

	/*  */
	/* step 1. Disable DIG && Set initial gain. */
	/*  */

	if (is_pause_dig)
		odm_pause_dig(dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);
	/*  */
	/* step 2. Disable all power save for read registers */
	/*  */
	/* dcmd_DebugControlPowerSave(adapter, PSDisable); */

	/*  */
	/* step 3. Get noise power level */
	/*  */
	start = odm_get_current_time(dm);
	while (1) {
		/* Stop updating idle time pwer report (for driver read) */
		odm_set_bb_reg(dm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 1);

		/* Read Noise Floor Report */
		tmp4b = odm_get_bb_reg(dm, 0x8f8, MASKDWORD);
		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "Noise Floor Report (0x8f8) = 0x%08x\n", tmp4b);

		/* update idle time pwer report per 5us */
		odm_set_bb_reg(dm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 0);

		noise_data.value[ODM_RF_PATH_A] = (u8)(tmp4b & 0xff);
		noise_data.value[ODM_RF_PATH_B] = (u8)((tmp4b & 0xff00) >> 8);

		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "value_a = 0x%x(%d), value_b = 0x%x(%d)\n",
			     noise_data.value[ODM_RF_PATH_A],
			     noise_data.value[ODM_RF_PATH_A],
			     noise_data.value[ODM_RF_PATH_B],
			     noise_data.value[ODM_RF_PATH_B]);

		for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path;
		     rf_path++) {
			noise_data.sval[rf_path] =
				(s8)noise_data.value[rf_path];
			noise_data.sval[rf_path] /= 2;
		}

		ODM_RT_TRACE(dm, ODM_COMP_COMMON, "sval_a = %d, sval_b = %d\n",
			     noise_data.sval[ODM_RF_PATH_A],
			     noise_data.sval[ODM_RF_PATH_B]);

		for (rf_path = ODM_RF_PATH_A; rf_path < max_rf_path;
		     rf_path++) {
			if (!(noise_data.valid_cnt[rf_path] < VALID_CNT) ||
			    !(noise_data.sval[rf_path] < VALID_MAX &&
			      noise_data.sval[rf_path] >= VALID_MIN)) {
				continue;
			}

			noise_data.valid_cnt[rf_path]++;
			noise_data.sum[rf_path] += noise_data.sval[rf_path];
			ODM_RT_TRACE(dm, ODM_COMP_COMMON,
				     "rf_path:%d Valid sval = %d\n", rf_path,
				     noise_data.sval[rf_path]);
			ODM_RT_TRACE(dm, ODM_COMP_COMMON, "Sum of sval = %d,\n",
				     noise_data.sum[rf_path]);
			if (noise_data.valid_cnt[rf_path] == VALID_CNT) {
				valid_done++;
				ODM_RT_TRACE(
					dm, ODM_COMP_COMMON,
					"After divided, rf_path:%d,sum = %d\n",
					rf_path, noise_data.sum[rf_path]);
			}
		}

		if ((valid_done == max_rf_path) ||
		    (odm_get_progressing_time(dm, start) > max_time)) {
			phydm_set_noise_data_sum(&noise_data, max_rf_path);
			break;
		}
	}
	reg_c50 = (u8)odm_get_bb_reg(dm, REG_OFDM_0_XA_AGC_CORE1, MASKBYTE0);
	reg_c50 &= ~BIT(7);
	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "0x%x = 0x%02x(%d)\n",
		     REG_OFDM_0_XA_AGC_CORE1, reg_c50, reg_c50);
	dm->noise_level.noise[ODM_RF_PATH_A] =
		(u8)(-110 + reg_c50 + noise_data.sum[ODM_RF_PATH_A]);
	dm->noise_level.noise_all += dm->noise_level.noise[ODM_RF_PATH_A];

	if (max_rf_path == 2) {
		reg_c58 = (u8)odm_get_bb_reg(dm, REG_OFDM_0_XB_AGC_CORE1,
					     MASKBYTE0);
		reg_c58 &= ~BIT(7);
		ODM_RT_TRACE(dm, ODM_COMP_COMMON, "0x%x = 0x%02x(%d)\n",
			     REG_OFDM_0_XB_AGC_CORE1, reg_c58, reg_c58);
		dm->noise_level.noise[ODM_RF_PATH_B] =
			(u8)(-110 + reg_c58 + noise_data.sum[ODM_RF_PATH_B]);
		dm->noise_level.noise_all +=
			dm->noise_level.noise[ODM_RF_PATH_B];
	}
	dm->noise_level.noise_all /= max_rf_path;

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "noise_a = %d, noise_b = %d\n",
		     dm->noise_level.noise[ODM_RF_PATH_A],
		     dm->noise_level.noise[ODM_RF_PATH_B]);

	/*  */
	/* step 4. Recover the Dig */
	/*  */
	if (is_pause_dig)
		odm_pause_dig(dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);
	func_end = odm_get_progressing_time(dm, func_start);

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "%s() <==\n", __func__);
	return dm->noise_level.noise_all;
}

static s16 odm_inband_noise_monitor_ac_series(struct phy_dm_struct *dm,
					      u8 is_pause_dig, u8 igi_value,
					      u32 max_time)
{
	s32 rxi_buf_anta, rxq_buf_anta; /*rxi_buf_antb, rxq_buf_antb;*/
	s32 value32, pwdb_A = 0, sval, noise, sum;
	bool pd_flag;
	u8 valid_cnt;
	u64 start = 0, func_start = 0, func_end = 0;

	if (!(dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8814A)))
		return 0;

	func_start = odm_get_current_time(dm);
	dm->noise_level.noise_all = 0;

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "%s() ==>\n", __func__);

	/* step 1. Disable DIG && Set initial gain. */
	if (is_pause_dig)
		odm_pause_dig(dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);

	/* step 2. Disable all power save for read registers */
	/*dcmd_DebugControlPowerSave(adapter, PSDisable); */

	/* step 3. Get noise power level */
	start = odm_get_current_time(dm);

	/* reset counters */
	sum = 0;
	valid_cnt = 0;

	/* step 3. Get noise power level */
	while (1) {
		/*Set IGI=0x1C */
		odm_write_dig(dm, 0x1C);
		/*stop CK320&CK88 */
		odm_set_bb_reg(dm, 0x8B4, BIT(6), 1);
		/*Read path-A */
		odm_set_bb_reg(dm, 0x8FC, MASKDWORD, 0x200); /*set debug port*/
		value32 = odm_get_bb_reg(dm, 0xFA0,
					 MASKDWORD); /*read debug port*/

		rxi_buf_anta = (value32 & 0xFFC00) >>
			       10; /*rxi_buf_anta=RegFA0[19:10]*/
		rxq_buf_anta = value32 & 0x3FF; /*rxq_buf_anta=RegFA0[19:10]*/

		pd_flag = (bool)((value32 & BIT(31)) >> 31);

		/*Not in packet detection period or Tx state */
		if ((!pd_flag) || (rxi_buf_anta != 0x200)) {
			/*sign conversion*/
			rxi_buf_anta = odm_sign_conversion(rxi_buf_anta, 10);
			rxq_buf_anta = odm_sign_conversion(rxq_buf_anta, 10);

			pwdb_A = odm_pwdb_conversion(
				rxi_buf_anta * rxi_buf_anta +
					rxq_buf_anta * rxq_buf_anta,
				20, 18); /*S(10,9)*S(10,9)=S(20,18)*/

			ODM_RT_TRACE(
				dm, ODM_COMP_COMMON,
				"pwdb_A= %d dB, rxi_buf_anta= 0x%x, rxq_buf_anta= 0x%x\n",
				pwdb_A, rxi_buf_anta & 0x3FF,
				rxq_buf_anta & 0x3FF);
		}
		/*Start CK320&CK88*/
		odm_set_bb_reg(dm, 0x8B4, BIT(6), 0);
		/*BB Reset*/
		odm_write_1byte(dm, 0x02, odm_read_1byte(dm, 0x02) & (~BIT(0)));
		odm_write_1byte(dm, 0x02, odm_read_1byte(dm, 0x02) | BIT(0));
		/*PMAC Reset*/
		odm_write_1byte(dm, 0xB03,
				odm_read_1byte(dm, 0xB03) & (~BIT(0)));
		odm_write_1byte(dm, 0xB03, odm_read_1byte(dm, 0xB03) | BIT(0));
		/*CCK Reset*/
		if (odm_read_1byte(dm, 0x80B) & BIT(4)) {
			odm_write_1byte(dm, 0x80B,
					odm_read_1byte(dm, 0x80B) & (~BIT(4)));
			odm_write_1byte(dm, 0x80B,
					odm_read_1byte(dm, 0x80B) | BIT(4));
		}

		sval = pwdb_A;

		if ((sval < 0 && sval >= -27) && (valid_cnt < VALID_CNT)) {
			valid_cnt++;
			sum += sval;
			ODM_RT_TRACE(dm, ODM_COMP_COMMON, "Valid sval = %d\n",
				     sval);
			ODM_RT_TRACE(dm, ODM_COMP_COMMON, "Sum of sval = %d,\n",
				     sum);
			if ((valid_cnt >= VALID_CNT) ||
			    (odm_get_progressing_time(dm, start) > max_time)) {
				sum /= VALID_CNT;
				ODM_RT_TRACE(dm, ODM_COMP_COMMON,
					     "After divided, sum = %d\n", sum);
				break;
			}
		}
	}

	/*ADC backoff is 12dB,*/
	/*Ptarget=0x1C-110=-82dBm*/
	noise = sum + 12 + 0x1C - 110;

	/*Offset*/
	noise = noise - 3;
	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "noise = %d\n", noise);
	dm->noise_level.noise_all = (s16)noise;

	/* step 4. Recover the Dig*/
	if (is_pause_dig)
		odm_pause_dig(dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);

	func_end = odm_get_progressing_time(dm, func_start);

	ODM_RT_TRACE(dm, ODM_COMP_COMMON, "%s() <==\n", __func__);

	return dm->noise_level.noise_all;
}

s16 odm_inband_noise_monitor(void *dm_void, u8 is_pause_dig, u8 igi_value,
			     u32 max_time)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		return odm_inband_noise_monitor_ac_series(dm, is_pause_dig,
							  igi_value, max_time);
	else
		return odm_inband_noise_monitor_n_series(dm, is_pause_dig,
							 igi_value, max_time);
}
