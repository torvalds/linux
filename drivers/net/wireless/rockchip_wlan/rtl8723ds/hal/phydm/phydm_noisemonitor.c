/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/

/* ************************************************************
 * include files
 * ************************************************************ */
#include "mp_precomp.h"
#include "phydm_precomp.h"

/* *************************************************
 * This function is for inband noise test utility only
 * To obtain the inband noise level(dbm), do the following.
 * 1. disable DIG and Power Saving
 * 2. Set initial gain = 0x1a
 * 3. Stop updating idle time pwer report (for driver read)
 *	- 0x80c[25]
 *
 * ************************************************* */

#define VALID_CNT				5

void phydm_set_noise_data_sum(struct noise_level *noise_data, u8 max_rf_path)
{
	u8 rf_path;

	for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
		if (noise_data->valid_cnt[rf_path])
			noise_data->sum[rf_path] /= noise_data->valid_cnt[rf_path];
		else
			noise_data->sum[rf_path]  = 0;
	}
}

s16 odm_inband_noise_monitor_n_series(struct PHY_DM_STRUCT	*p_dm, u8 is_pause_dig, u8 igi_value, u32 max_time)
{
	u32				tmp4b;
	u8				max_rf_path = 0, rf_path;
	u8				reg_c50, reg_c58, valid_done = 0;
	struct noise_level		noise_data;
	u64	start  = 0, func_start = 0,	func_end = 0;

	func_start = odm_get_current_time(p_dm);
	p_dm->noise_level.noise_all = 0;

	if ((p_dm->rf_type == RF_1T2R) || (p_dm->rf_type == RF_2T2R))
		max_rf_path = 2;
	else
		max_rf_path = 1;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("odm_DebugControlInbandNoise_Nseries() ==>\n"));

	odm_memory_set(p_dm, &noise_data, 0, sizeof(struct noise_level));
	/* step 1. Disable DIG && Set initial gain. */

	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);

	/* step 3. Get noise power level */
	start = odm_get_current_time(p_dm);
	while (1) {

		/* Stop updating idle time pwer report (for driver read) */
		odm_set_bb_reg(p_dm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 1);

		/* Read Noise Floor Report */
		tmp4b = odm_get_bb_reg(p_dm, 0x8f8, MASKDWORD);

		/* update idle time pwer report per 5us */
		odm_set_bb_reg(p_dm, REG_FPGA0_TX_GAIN_STAGE, BIT(25), 0);

		ODM_delay_us(5);

		noise_data.value[RF_PATH_A] = (u8)(tmp4b & 0xff);
		noise_data.value[RF_PATH_B]  = (u8)((tmp4b & 0xff00) >> 8);

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			noise_data.sval[rf_path] = (s8)noise_data.value[rf_path];
			noise_data.sval[rf_path] /= 2;
		}

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			if (noise_data.valid_cnt[rf_path] < VALID_CNT) {
				noise_data.valid_cnt[rf_path]++;
				noise_data.sum[rf_path] += noise_data.sval[rf_path];
				PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("rf_path:%d Valid sval = %d\n", rf_path, noise_data.sval[rf_path]));
				PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("Sum of sval = %d,\n", noise_data.sum[rf_path]));
				if (noise_data.valid_cnt[rf_path] == VALID_CNT)
					valid_done++;
			}
		}
		if ((valid_done == max_rf_path) || (odm_get_progressing_time(p_dm, start) > max_time)) {
			phydm_set_noise_data_sum(&noise_data, max_rf_path);
			break;
		}
	}
	reg_c50 = (u8)odm_get_bb_reg(p_dm, REG_OFDM_0_XA_AGC_CORE1, MASKBYTE0);
	reg_c50 &= ~BIT(7);
	p_dm->noise_level.noise[RF_PATH_A] = (s8)(-110 + reg_c50 + noise_data.sum[RF_PATH_A]);
	p_dm->noise_level.noise_all += p_dm->noise_level.noise[RF_PATH_A];

	if (max_rf_path == 2) {
		reg_c58 = (u8)odm_get_bb_reg(p_dm, REG_OFDM_0_XB_AGC_CORE1, MASKBYTE0);
		reg_c58 &= ~BIT(7);
		p_dm->noise_level.noise[RF_PATH_B] = (s8)(-110 + reg_c58 + noise_data.sum[RF_PATH_B]);
		p_dm->noise_level.noise_all += p_dm->noise_level.noise[RF_PATH_B];
	}
	p_dm->noise_level.noise_all /= max_rf_path;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("noise_a = %d, noise_b = %d, noise_all = %d\n",
		p_dm->noise_level.noise[RF_PATH_A], p_dm->noise_level.noise[RF_PATH_B],
		p_dm->noise_level.noise_all));

	/* step 4. Recover the Dig */
	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);
	func_end = odm_get_progressing_time(p_dm, func_start);

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("end\n"));
	return p_dm->noise_level.noise_all;

}


s16
phydm_idle_noise_measurement_ac(
	struct PHY_DM_STRUCT	*p_dm,
	u8	is_pause_dig,
	u8	igi_value,
	u32	max_time
	)
{
	u32				tmp4b;
	u8				max_rf_path = 0, rf_path;
	u8				reg_c50, reg_e50, valid_done = 0;
	u64				start  = 0, func_start = 0, func_end = 0;
	struct noise_level	noise_data;

	func_start = odm_get_current_time(p_dm);
	p_dm->noise_level.noise_all = 0;

	if ((p_dm->rf_type == RF_1T2R) || (p_dm->rf_type == RF_2T2R))
		max_rf_path = 2;
	else
		max_rf_path = 1;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("phydm_idle_noise_measurement_ac==>\n"));

	odm_memory_set(p_dm, &noise_data, 0, sizeof(struct noise_level));

	/*Step 1. Disable DIG && Set initial gain.*/

	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);

	/*Step 2. Get noise power level*/
	start = odm_get_current_time(p_dm);

	while (1) {

		/*Stop updating idle time pwer report (for driver read)*/
		odm_set_bb_reg(p_dm, 0x9e4, BIT(30), 0x1);

		/*Read Noise Floor Report*/
		tmp4b = odm_get_bb_reg(p_dm, 0xff0, MASKDWORD);

		/*update idle time pwer report per 5us*/
		odm_set_bb_reg(p_dm, 0x9e4, BIT(30), 0x0);

		ODM_delay_us(5);

		noise_data.value[RF_PATH_A] = (u8)(tmp4b & 0xff);
		noise_data.value[RF_PATH_B] = (u8)((tmp4b & 0xff00) >> 8);

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			noise_data.sval[rf_path] = (s8)noise_data.value[rf_path];
			noise_data.sval[rf_path] = noise_data.sval[rf_path] >> 1;
		}

		for (rf_path = RF_PATH_A; rf_path < max_rf_path; rf_path++) {
			if (noise_data.valid_cnt[rf_path] < VALID_CNT) {
				noise_data.valid_cnt[rf_path]++;
				noise_data.sum[rf_path] += noise_data.sval[rf_path];
				PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("Path:%d Valid sval = %d\n", rf_path, noise_data.sval[rf_path]));
				PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("Sum of sval = %d\n", noise_data.sum[rf_path]));
				if (noise_data.valid_cnt[rf_path] == VALID_CNT)
					valid_done++;
			}
		}

		if ((valid_done == max_rf_path) || (odm_get_progressing_time(p_dm, start) > max_time)) {
			phydm_set_noise_data_sum(&noise_data, max_rf_path);
			break;
		}
	}
	reg_c50 = (u8)odm_get_bb_reg(p_dm, 0xc50, MASKBYTE0);
	reg_c50 &= ~BIT(7);
	p_dm->noise_level.noise[RF_PATH_A] = (s8)(-110 + reg_c50 + noise_data.sum[RF_PATH_A]);
	p_dm->noise_level.noise_all += p_dm->noise_level.noise[RF_PATH_A];

	if (max_rf_path == 2) {
		reg_e50 = (u8)odm_get_bb_reg(p_dm, 0xe50, MASKBYTE0);
		reg_e50 &= ~BIT(7);
		p_dm->noise_level.noise[RF_PATH_B] = (s8)(-110 + reg_e50 + noise_data.sum[RF_PATH_B]);
		p_dm->noise_level.noise_all += p_dm->noise_level.noise[RF_PATH_B];
	}
	p_dm->noise_level.noise_all /= max_rf_path;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("noise_a = %d, noise_b = %d, noise_all = %d\n",
		p_dm->noise_level.noise[RF_PATH_A], p_dm->noise_level.noise[RF_PATH_B],
		p_dm->noise_level.noise_all));

	/*Step 3. Recover the Dig*/
	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);
	func_end = odm_get_progressing_time(p_dm, func_start);

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("end\n"));
	return p_dm->noise_level.noise_all;

}


s16
odm_inband_noise_monitor_ac_series(
	struct PHY_DM_STRUCT	*p_dm,
	u8 is_pause_dig,
	u8 igi_value,
	u32 max_time
	)
{
	s32          rxi_buf_anta, rxq_buf_anta; /*rxi_buf_antb, rxq_buf_antb;*/
	s32	        value32, pwdb_A = 0, sval, noise, sum = 0;
	boolean	        pd_flag;
	u8		valid_cnt = 0;
	u64	start = 0, func_start = 0, func_end = 0;

	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C))
		return phydm_idle_noise_measurement_ac(p_dm, is_pause_dig, igi_value, max_time);

	if (!(p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8814A)))
		return 0;

	func_start = odm_get_current_time(p_dm);
	p_dm->noise_level.noise_all = 0;

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("odm_inband_noise_monitor_ac_series() ==>\n"));

	/* step 1. Disable DIG && Set initial gain. */
	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_1, igi_value);

	/* step 3. Get noise power level */
	start = odm_get_current_time(p_dm);

	/* step 3. Get noise power level */
	while (1) {
		/*Set IGI=0x1C */
		odm_write_dig(p_dm, 0x1C);
		/*stop CK320&CK88 */
		odm_set_bb_reg(p_dm, 0x8B4, BIT(6), 1);
		/*Read path-A */
		odm_set_bb_reg(p_dm, 0x8FC, MASKDWORD, 0x200); /*set debug port*/
		value32 = odm_get_bb_reg(p_dm, 0xFA0, MASKDWORD); /*read debug port*/

		rxi_buf_anta = (value32 & 0xFFC00) >> 10; /*rxi_buf_anta=RegFA0[19:10]*/
		rxq_buf_anta = value32 & 0x3FF; /*rxq_buf_anta=RegFA0[19:10]*/

		pd_flag = (boolean)((value32 & BIT(31)) >> 31);

		/*Not in packet detection period or Tx state */
		if ((!pd_flag) || (rxi_buf_anta != 0x200)) {
			/*sign conversion*/
			rxi_buf_anta = odm_sign_conversion(rxi_buf_anta, 10);
			rxq_buf_anta = odm_sign_conversion(rxq_buf_anta, 10);

			pwdb_A = odm_pwdb_conversion(rxi_buf_anta * rxi_buf_anta + rxq_buf_anta * rxq_buf_anta, 20, 18); /*S(10,9)*S(10,9)=S(20,18)*/

			PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("pwdb_A= %d dB, rxi_buf_anta= 0x%x, rxq_buf_anta= 0x%x\n", pwdb_A, rxi_buf_anta & 0x3FF, rxq_buf_anta & 0x3FF));
		}
		/*Start CK320&CK88*/
		odm_set_bb_reg(p_dm, 0x8B4, BIT(6), 0);
		/*BB Reset*/
		odm_write_1byte(p_dm, 0x02, odm_read_1byte(p_dm, 0x02) & (~BIT(0)));
		odm_write_1byte(p_dm, 0x02, odm_read_1byte(p_dm, 0x02) | BIT(0));
		/*PMAC Reset*/
		odm_write_1byte(p_dm, 0xB03, odm_read_1byte(p_dm, 0xB03) & (~BIT(0)));
		odm_write_1byte(p_dm, 0xB03, odm_read_1byte(p_dm, 0xB03) | BIT(0));
		/*CCK Reset*/
		if (odm_read_1byte(p_dm, 0x80B) & BIT(4)) {
			odm_write_1byte(p_dm, 0x80B, odm_read_1byte(p_dm, 0x80B) & (~BIT(4)));
			odm_write_1byte(p_dm, 0x80B, odm_read_1byte(p_dm, 0x80B) | BIT(4));
		}

		sval = pwdb_A;

		if ((sval < 0 && sval >= -27) && (valid_cnt < VALID_CNT)){
			valid_cnt++;
			sum += sval;
			PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("Valid sval = %d\n", sval));
			PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("Sum of sval = %d,\n", sum));
			if ((valid_cnt >= VALID_CNT) || (odm_get_progressing_time(p_dm, start) > max_time)) {
				sum /= VALID_CNT;
				PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("After divided, sum = %d\n", sum));
				break;
			}
		}
	}

	/*ADC backoff is 12dB,*/
	/*Ptarget=0x1C-110=-82dBm*/
	noise = sum + 12 + 0x1C - 110;

	/*Offset*/
	noise = noise - 3;
	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("noise = %d\n", noise));
	p_dm->noise_level.noise_all = (s16)noise;

	/* step 4. Recover the Dig*/
	if (is_pause_dig)
		odm_pause_dig(p_dm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_1, igi_value);

	func_end = odm_get_progressing_time(p_dm, func_start);

	PHYDM_DBG(p_dm, DBG_ENV_MNTR, ("odm_inband_noise_monitor_ac_series() <==\n"));

	return p_dm->noise_level.noise_all;
}



s16
odm_inband_noise_monitor(
	void *p_dm_void,
	u8 is_pause_dig,
	u8 igi_value,
	u32 max_time
	)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	igi_value = 0x32;	/*since HW ability is about +15~-35, we fix IGI = -60 for maximum coverage*/

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES)
		return odm_inband_noise_monitor_ac_series(p_dm, is_pause_dig, igi_value, max_time);
	else
		return odm_inband_noise_monitor_n_series(p_dm, is_pause_dig, igi_value, max_time);
}

void
phydm_noisy_detection(
	void *p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32  total_fa_cnt, total_cca_cnt;
	u32  score = 0, i, score_smooth;

	total_cca_cnt = p_dm->false_alm_cnt.cnt_cca_all;
	total_fa_cnt  = p_dm->false_alm_cnt.cnt_all;

#if 0
	if (total_fa_cnt * 16 >= total_cca_cnt * 14)    /*  87.5 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 12) /*  75 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 10) /*  56.25 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 8) /*  50 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 7) /*  43.75 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 6) /*  37.5 */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 5) /*  31.25% */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 4) /*  25% */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 3) /*  18.75% */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 2) /*  12.5% */
		;
	else if (total_fa_cnt * 16 >= total_cca_cnt * 1) /*  6.25% */
		;
#endif
	for (i = 0; i <= 16; i++) {
		if (total_fa_cnt * 16 >= total_cca_cnt * (16 - i)) {
			score = 16 - i;
			break;
		}
	}

	/* noisy_decision_smooth = noisy_decision_smooth>>1 + (score<<3)>>1; */
	p_dm->noisy_decision_smooth = (p_dm->noisy_decision_smooth >> 1) + (score << 2);

	/* Round the noisy_decision_smooth: +"3" comes from (2^3)/2-1 */
	score_smooth = (total_cca_cnt >= 300) ? ((p_dm->noisy_decision_smooth + 3) >> 3) : 0;

	p_dm->noisy_decision = (score_smooth >= 3) ? 1 : 0;
	
	PHYDM_DBG(p_dm, DBG_ENV_MNTR,
		("[NoisyDetection] CCA_cnt=%d,FA_cnt=%d, noisy_dec_smooth=%d, score=%d, score_smooth=%d, noisy_dec=%d\n",
		total_cca_cnt, total_fa_cnt, p_dm->noisy_decision_smooth, score, score_smooth, p_dm->noisy_decision));

}

