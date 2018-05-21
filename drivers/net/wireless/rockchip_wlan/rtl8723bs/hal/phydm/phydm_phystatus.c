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

void
phydm_rx_statistic_cal(
	struct PHY_DM_STRUCT				*p_phydm,
	u8									*p_phy_status,
	struct phydm_perpkt_info_struct				*p_pktinfo
)
{
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
	struct _phy_status_rpt_jaguar2_type1	*p_phy_sta_rpt = (struct _phy_status_rpt_jaguar2_type1 *)p_phy_status;
#endif
	u8									date_rate = (p_pktinfo->data_rate & 0x7f);

	if (date_rate <= ODM_RATE54M) {

		p_phydm->phy_dbg_info.num_qry_legacy_pkt[date_rate]++;
		/**/
	} else if (date_rate <= ODM_RATEMCS31) {

		p_phydm->phy_dbg_info.num_qry_ht_pkt[date_rate - ODM_RATEMCS0]++;
		p_phydm->phy_dbg_info.ht_pkt_not_zero = true;

	}
	#if ODM_IC_11AC_SERIES_SUPPORT
	else if (date_rate <= ODM_RATEVHTSS4MCS9) {

		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		if ((p_phy_sta_rpt->gid != 0) && (p_phy_sta_rpt->gid != 63) && (p_phydm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE)) {

			p_phydm->phy_dbg_info.num_qry_mu_vht_pkt[date_rate - ODM_RATEVHTSS1MCS0]++;
			if (p_pktinfo->ppdu_cnt < 4) {
				p_phydm->phy_dbg_info.num_of_ppdu[p_pktinfo->ppdu_cnt] = date_rate | BIT(7);
				p_phydm->phy_dbg_info.gid_num[p_pktinfo->ppdu_cnt] = p_phy_sta_rpt->gid;
			}
		} else
		#endif
		{
			p_phydm->phy_dbg_info.num_qry_vht_pkt[date_rate - ODM_RATEVHTSS1MCS0]++;
			p_phydm->phy_dbg_info.vht_pkt_not_zero = true;
			#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
			if (p_pktinfo->ppdu_cnt < 4) {
				p_phydm->phy_dbg_info.num_of_ppdu[p_pktinfo->ppdu_cnt] = date_rate;
				p_phydm->phy_dbg_info.gid_num[p_pktinfo->ppdu_cnt] = p_phy_sta_rpt->gid;
			}
			#endif
		}
	}
	#endif
}

void
phydm_reset_phystatus_avg(
	struct PHY_DM_STRUCT	*p_dm
)
{
	struct phydm_phystatus_avg		*p_dbg_avg = &(p_dm->phy_dbg_info.phystatus_statistic_avg);

	odm_memory_set(p_dm, &(p_dbg_avg->rssi_cck_avg), 0, sizeof(struct phydm_phystatus_avg));
}

void
phydm_reset_phystatus_statistic(
	struct PHY_DM_STRUCT	*p_dm
)
{
	struct phydm_phystatus_statistic		*p_dbg_statistic = &(p_dm->phy_dbg_info.phystatus_statistic_info);

	odm_memory_set(p_dm, &(p_dbg_statistic->rssi_cck_sum), 0, sizeof(struct phydm_phystatus_statistic));
}

void
phydm_avg_phystatus_index(
	struct PHY_DM_STRUCT			*p_dm,
	struct phydm_phyinfo_struct		*p_phy_info,
	struct phydm_perpkt_info_struct			*p_pktinfo
)
{
	u8	rate_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);
	struct phydm_phystatus_statistic		*p_dbg_statistic = &(p_dm->phy_dbg_info.phystatus_statistic_info);

	if (p_pktinfo->data_rate <= ODM_RATE11M) {

		/*RSSI*/
		p_dbg_statistic->rssi_cck_sum += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_cck_cnt++;
	} else if (p_pktinfo->data_rate <= ODM_RATE54M) {

		/*evm*/
		p_dbg_statistic->evm_ofdm_sum += p_phy_info->rx_mimo_evm_dbm[0];

		/*SNR*/
		p_dbg_statistic->snr_ofdm_sum += p_phy_info->rx_snr[0];

		/*RSSI*/
		p_dbg_statistic->rssi_ofdm_sum += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_ofdm_cnt++;
	} else if (rate_ss == 1) {

		/*evm*/
		p_dbg_statistic->evm_1ss_sum += p_phy_info->rx_mimo_evm_dbm[0];

		/*SNR*/
		p_dbg_statistic->snr_1ss_sum += p_phy_info->rx_snr[0];

		p_dbg_statistic->rssi_1ss_sum += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_1ss_cnt++;
	} else if (rate_ss == 2) {
		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		/*evm*/
		p_dbg_statistic->evm_2ss_sum[0] += p_phy_info->rx_mimo_evm_dbm[0];
		p_dbg_statistic->evm_2ss_sum[1] += p_phy_info->rx_mimo_evm_dbm[1];

		/*SNR*/
		p_dbg_statistic->snr_2ss_sum[0] += p_phy_info->rx_snr[0];
		p_dbg_statistic->snr_2ss_sum[1] += p_phy_info->rx_snr[1];

		/*RSSI*/
		p_dbg_statistic->rssi_2ss_sum[0] += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_2ss_sum[1] += p_phy_info->rx_mimo_signal_strength[1];
		p_dbg_statistic->rssi_2ss_cnt++;
		#endif
	} else if (rate_ss == 3) {
		#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		/*evm*/
		p_dbg_statistic->evm_3ss_sum[0] += p_phy_info->rx_mimo_evm_dbm[0];
		p_dbg_statistic->evm_3ss_sum[1] += p_phy_info->rx_mimo_evm_dbm[1];
		p_dbg_statistic->evm_3ss_sum[2] += p_phy_info->rx_mimo_evm_dbm[2];

		/*SNR*/
		p_dbg_statistic->snr_3ss_sum[0] += p_phy_info->rx_snr[0];
		p_dbg_statistic->snr_3ss_sum[1] += p_phy_info->rx_snr[1];
		p_dbg_statistic->snr_3ss_sum[2] += p_phy_info->rx_snr[2];

		/*RSSI*/
		p_dbg_statistic->rssi_3ss_sum[0] += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_3ss_sum[1] += p_phy_info->rx_mimo_signal_strength[1];
		p_dbg_statistic->rssi_3ss_sum[2] += p_phy_info->rx_mimo_signal_strength[2];
		p_dbg_statistic->rssi_3ss_cnt++;
		#endif
	} else if (rate_ss == 4) {
		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		/*evm*/
		p_dbg_statistic->evm_4ss_sum[0] += p_phy_info->rx_mimo_evm_dbm[0];
		p_dbg_statistic->evm_4ss_sum[1] += p_phy_info->rx_mimo_evm_dbm[1];
		p_dbg_statistic->evm_4ss_sum[2] += p_phy_info->rx_mimo_evm_dbm[2];
		p_dbg_statistic->evm_4ss_sum[3] += p_phy_info->rx_mimo_evm_dbm[3];

		/*SNR*/
		p_dbg_statistic->snr_4ss_sum[0] += p_phy_info->rx_snr[0];
		p_dbg_statistic->snr_4ss_sum[1] += p_phy_info->rx_snr[1];
		p_dbg_statistic->snr_4ss_sum[2] += p_phy_info->rx_snr[2];
		p_dbg_statistic->snr_4ss_sum[3] += p_phy_info->rx_snr[3];

		/*RSSI*/
		p_dbg_statistic->rssi_4ss_sum[0] += p_phy_info->rx_mimo_signal_strength[0];
		p_dbg_statistic->rssi_4ss_sum[1] += p_phy_info->rx_mimo_signal_strength[1];
		p_dbg_statistic->rssi_4ss_sum[2] += p_phy_info->rx_mimo_signal_strength[2];
		p_dbg_statistic->rssi_4ss_sum[3] += p_phy_info->rx_mimo_signal_strength[3];
		p_dbg_statistic->rssi_4ss_cnt++;
		#endif
	}
}

u8 phydm_get_signal_quality(
	struct phydm_phyinfo_struct *p_phy_info,
	struct PHY_DM_STRUCT *p_dm,
	struct _phy_status_rpt_8192cd *p_phy_sta_rpt
	)
{
	u8 SQ_rpt;
	u8 result = 0;

	if (p_phy_info->rx_pwdb_all > 40 && !p_dm->is_in_hct_test)
		result = 100;
	else {
		SQ_rpt = p_phy_sta_rpt->cck_sig_qual_ofdm_pwdb_all;

		if (SQ_rpt > 64)
			result = 0;
		else if (SQ_rpt < 20)
			result = 100;
		else
			result = ((64 - SQ_rpt) * 100) / 44;

	}

	return result;
}

u8
phydm_query_rx_pwr_percentage(
	s8		ant_power
)
{
	if ((ant_power <= -100) || (ant_power >= 20))
		return	0;
	else if (ant_power >= 0)
		return	100;
	else
		return 100 + ant_power;
}


s32
phydm_signal_scale_mapping_92c_series(
	struct PHY_DM_STRUCT *p_dm,
	s32 curr_sig
)
{
	s32 ret_sig = 0;
#if (DEV_BUS_TYPE == RT_PCI_INTERFACE)
	if (p_dm->support_interface  == ODM_ITRF_PCIE) {
		/* step 1. Scale mapping. */
		if (curr_sig >= 61 && curr_sig <= 100)
			ret_sig = 90 + ((curr_sig - 60) / 4);
		else if (curr_sig >= 41 && curr_sig <= 60)
			ret_sig = 78 + ((curr_sig - 40) / 2);
		else if (curr_sig >= 31 && curr_sig <= 40)
			ret_sig = 66 + (curr_sig - 30);
		else if (curr_sig >= 21 && curr_sig <= 30)
			ret_sig = 54 + (curr_sig - 20);
		else if (curr_sig >= 5 && curr_sig <= 20)
			ret_sig = 42 + (((curr_sig - 5) * 2) / 3);
		else if (curr_sig == 4)
			ret_sig = 36;
		else if (curr_sig == 3)
			ret_sig = 27;
		else if (curr_sig == 2)
			ret_sig = 18;
		else if (curr_sig == 1)
			ret_sig = 9;
		else
			ret_sig = curr_sig;
	}
#endif

#if ((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	if ((p_dm->support_interface  == ODM_ITRF_USB) || (p_dm->support_interface  == ODM_ITRF_SDIO)) {
		if (curr_sig >= 51 && curr_sig <= 100)
			ret_sig = 100;
		else if (curr_sig >= 41 && curr_sig <= 50)
			ret_sig = 80 + ((curr_sig - 40) * 2);
		else if (curr_sig >= 31 && curr_sig <= 40)
			ret_sig = 66 + (curr_sig - 30);
		else if (curr_sig >= 21 && curr_sig <= 30)
			ret_sig = 54 + (curr_sig - 20);
		else if (curr_sig >= 10 && curr_sig <= 20)
			ret_sig = 42 + (((curr_sig - 10) * 2) / 3);
		else if (curr_sig >= 5 && curr_sig <= 9)
			ret_sig = 22 + (((curr_sig - 5) * 3) / 2);
		else if (curr_sig >= 1 && curr_sig <= 4)
			ret_sig = 6 + (((curr_sig - 1) * 3) / 2);
		else
			ret_sig = curr_sig;
	}

#endif
	return ret_sig;
}

s32
phydm_signal_scale_mapping(
	struct PHY_DM_STRUCT *p_dm,
	s32 curr_sig
)
{
#ifdef CONFIG_SIGNAL_SCALE_MAPPING
		return phydm_signal_scale_mapping_92c_series(p_dm, curr_sig);
#else
		return curr_sig;
#endif

}

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
static u8 phydm_sq_patch_rt_cid_819x_lenovo(
	struct PHY_DM_STRUCT	*p_dm,
	u8		is_cck_rate,
	u8		PWDB_ALL,
	u8		path,
	u8		RSSI
)
{
	u8	SQ = 0;

	if (is_cck_rate) {

		if (IS_HARDWARE_TYPE_8192E(p_dm->adapter)) {

			/*  */
			/* <Roger_Notes> Expected signal strength and bars indication at Lenovo lab. 2013.04.11 */
			/* 802.11n, 802.11b, 802.11g only at channel 6 */
			/*  */
			/*		Attenuation (dB)	OS Signal Bars	RSSI by Xirrus (dBm) */
			/*			50				5			-49 */
			/*			55				5			-49 */
			/*			60				5			-50 */
			/*			65				5			-51 */
			/*			70				5			-52 */
			/*			75				5			-54 */
			/*			80				5			-55 */
			/*			85				4			-60 */
			/*			90				3			-63 */
			/*			95				3			-65 */
			/*			100				2			-67 */
			/*			102				2			-67 */
			/*			104				1			-70 */
			/*  */

			if (PWDB_ALL >= 50)
				SQ = 100;
			else if (PWDB_ALL >= 35 && PWDB_ALL < 50)
				SQ = 80;
			else if (PWDB_ALL >= 31 && PWDB_ALL < 35)
				SQ = 60;
			else if (PWDB_ALL >= 22 && PWDB_ALL < 31)
				SQ = 40;
			else if (PWDB_ALL >= 18 && PWDB_ALL < 22)
				SQ = 20;
			else
				SQ = 10;
		} else {
			if (PWDB_ALL >= 50)
				SQ = 100;
			else if (PWDB_ALL >= 35 && PWDB_ALL < 50)
				SQ = 80;
			else if (PWDB_ALL >= 22 && PWDB_ALL < 35)
				SQ = 60;
			else if (PWDB_ALL >= 18 && PWDB_ALL < 22)
				SQ = 40;
			else
				SQ = 10;
		}

	} else {
		/* OFDM rate */

		if (IS_HARDWARE_TYPE_8192E(p_dm->adapter)) {
			if (RSSI >= 45)
				SQ = 100;
			else if (RSSI >= 22 && RSSI < 45)
				SQ = 80;
			else if (RSSI >= 18 && RSSI < 22)
				SQ = 40;
			else
				SQ = 20;
		} else {
			if (RSSI >= 45)
				SQ = 100;
			else if (RSSI >= 22 && RSSI < 45)
				SQ = 80;
			else if (RSSI >= 18 && RSSI < 22)
				SQ = 40;
			else
				SQ = 20;
		}
	}

	RT_TRACE(COMP_DBG, DBG_TRACE, ("is_cck_rate(%#d), PWDB_ALL(%#d), RSSI(%#d), SQ(%#d)\n", is_cck_rate, PWDB_ALL, RSSI, SQ));


	return SQ;
}

static u8 phydm_sq_patch_rt_cid_819x_acer(
	struct PHY_DM_STRUCT	*p_dm,
	u8		is_cck_rate,
	u8		PWDB_ALL,
	u8		path,
	u8		RSSI
)
{
	u8	SQ = 0;

	if (is_cck_rate) {

		RT_TRACE(COMP_DBG, DBG_WARNING, ("odm_SQ_process_patch_RT_Acer\n"));

#if OS_WIN_FROM_WIN8(OS_VERSION)

		if (PWDB_ALL >= 50)
			SQ = 100;
		else if (PWDB_ALL >= 35 && PWDB_ALL < 50)
			SQ = 80;
		else if (PWDB_ALL >= 30 && PWDB_ALL < 35)
			SQ = 60;
		else if (PWDB_ALL >= 25 && PWDB_ALL < 30)
			SQ = 40;
		else if (PWDB_ALL >= 20 && PWDB_ALL < 25)
			SQ = 20;
		else
			SQ = 10;
#else
		if (PWDB_ALL >= 50)
			SQ = 100;
		else if (PWDB_ALL >= 35 && PWDB_ALL < 50)
			SQ = 80;
		else if (PWDB_ALL >= 30 && PWDB_ALL < 35)
			SQ = 60;
		else if (PWDB_ALL >= 25 && PWDB_ALL < 30)
			SQ = 40;
		else if (PWDB_ALL >= 20 && PWDB_ALL < 25)
			SQ = 20;
		else
			SQ = 10;

		if (PWDB_ALL == 0) /* Abnormal case, do not indicate the value above 20 on Win7 */
			SQ = 20;
#endif



	} else {
		/* OFDM rate */

		if (IS_HARDWARE_TYPE_8192E(p_dm->adapter)) {
			if (RSSI >= 45)
				SQ = 100;
			else if (RSSI >= 22 && RSSI < 45)
				SQ = 80;
			else if (RSSI >= 18 && RSSI < 22)
				SQ = 40;
			else
				SQ = 20;
		} else {
			if (RSSI >= 35)
				SQ = 100;
			else if (RSSI >= 30 && RSSI < 35)
				SQ = 80;
			else if (RSSI >= 25 && RSSI < 30)
				SQ = 40;
			else
				SQ = 20;
		}
	}

	RT_TRACE(COMP_DBG, DBG_LOUD, ("is_cck_rate(%#d), PWDB_ALL(%#d), RSSI(%#d), SQ(%#d)\n", is_cck_rate, PWDB_ALL, RSSI, SQ));


	return SQ;
}
#endif

static u8
phydm_evm_db_to_percentage(
	s8 value
)
{
	/*  */
	/* -33dB~0dB to 0%~99% */
	/*  */
	s8 ret_val;

	ret_val = value;
	ret_val /= 2;

	/*dbg_print("value=%d\n", value);*/
	/*ODM_RT_DISP(FRX, RX_PHY_SQ, ("EVMdbToPercentage92C value=%d / %x\n", ret_val, ret_val));*/
#ifdef ODM_EVM_ENHANCE_ANTDIV
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -40)
		ret_val = -40;

	ret_val = 0 - ret_val;
	ret_val *= 3;
#else
	if (ret_val >= 0)
		ret_val = 0;

	if (ret_val <= -33)
		ret_val = -33;

	ret_val = 0 - ret_val;
	ret_val *= 3;

	if (ret_val == 99)
		ret_val = 100;
#endif

	return (u8)ret_val;
}

static u8
phydm_evm_dbm_jaguar_series(
	s8 value
)
{
	s8 ret_val = value;

	/* -33dB~0dB to 33dB ~ 0dB */
	if (ret_val == -128)
		ret_val = 127;
	else if (ret_val < 0)
		ret_val = 0 - ret_val;

	ret_val  = ret_val >> 1;
	return (u8)ret_val;
}

static s16
phydm_cfo(
	s8 value
)
{
	s16  ret_val;

	if (value < 0) {
		ret_val = 0 - value;
		ret_val = (ret_val << 1) + (ret_val >> 1) ;  /* *2.5~=312.5/2^7 */
		ret_val = ret_val | BIT(12);  /* set bit12 as 1 for negative cfo */
	} else {
		ret_val = value;
		ret_val = (ret_val << 1) + (ret_val >> 1) ; /* *2.5~=312.5/2^7 */
	}
	return ret_val;
}

s8
phydm_cck_rssi_convert(
	struct PHY_DM_STRUCT	*p_dm,
	u16		lna_idx,
	u8		vga_idx
)
{
	return (p_dm->cck_lna_gain_table[lna_idx] - (vga_idx << 1));
}

void
phydm_get_cck_rssi_table_from_reg(
	struct PHY_DM_STRUCT	*p_dm
)
{
	u8	used_lna_idx_tmp;
	u32	reg_0xa80 = 0x7431, reg_0xabc = 0xcbe5edfd; /*example: {-53, -43, -33, -27, -19, -13, -3, 1}*/ /*{0xCB, 0xD5, 0xDF, 0xE5, 0xED, 0xF3, 0xFD, 0x2}*/
	u8	i;

	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("CCK LNA Gain table init\n"));

	if (!(p_dm->support_ic_type & (ODM_RTL8197F)))
		return;

	reg_0xa80 = odm_get_bb_reg(p_dm, 0xa80, 0xFFFF);
	reg_0xabc = odm_get_bb_reg(p_dm, 0xabc, MASKDWORD);

	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("reg_0xa80 = 0x%x\n", reg_0xa80));
	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("reg_0xabc = 0x%x\n", reg_0xabc));

	for (i = 0; i <= 3; i++) {
		used_lna_idx_tmp = (u8)((reg_0xa80 >> (4*i)) & 0x7);
		p_dm->cck_lna_gain_table[used_lna_idx_tmp] = (s8)((reg_0xabc >> (8*i)) & 0xff);
	}

	PHYDM_DBG(p_dm, ODM_COMP_INIT, ("cck_lna_gain_table = {%d,%d,%d,%d,%d,%d,%d,%d}\n",
		p_dm->cck_lna_gain_table[0],
		p_dm->cck_lna_gain_table[1],
		p_dm->cck_lna_gain_table[2],
		p_dm->cck_lna_gain_table[3],
		p_dm->cck_lna_gain_table[4],
		p_dm->cck_lna_gain_table[5],
		p_dm->cck_lna_gain_table[6],
		p_dm->cck_lna_gain_table[7]));

}

u8
phydm_rate_to_num_ss(
	struct PHY_DM_STRUCT		*p_dm,
	u8			data_rate
)
{
	u8	num_ss = 1;

	if (data_rate  <= ODM_RATE54M)
		num_ss = 1;
	else if (data_rate  <= ODM_RATEMCS31)
		num_ss = ((data_rate  - ODM_RATEMCS0) >> 3) + 1;
	else if (data_rate  <= ODM_RATEVHTSS1MCS9)
		num_ss = 1;
	else if (data_rate  <= ODM_RATEVHTSS2MCS9)
		num_ss = 2;
	else if (data_rate  <= ODM_RATEVHTSS3MCS9)
		num_ss = 3;
	else if (data_rate  <= ODM_RATEVHTSS4MCS9)
		num_ss = 4;

	return num_ss;
}



#if (RTL8703B_SUPPORT == 1)
s8
phydm_cck_rssi_8703B(
	u16	LNA_idx,
	u8	VGA_idx
)
{
	s8	rx_pwr_all = 0x00;

	switch (LNA_idx) {
	case 0xf:
		rx_pwr_all = -48 - (2 * VGA_idx);
		break;
	case 0xb:
		rx_pwr_all = -42 - (2 * VGA_idx); /*TBD*/
		break;
	case 0xa:
		rx_pwr_all = -36 - (2 * VGA_idx);
		break;
	case 8:
		rx_pwr_all = -32 - (2 * VGA_idx);
		break;
	case 7:
		rx_pwr_all = -19 - (2 * VGA_idx);
		break;
	case 4:
		rx_pwr_all = -6 - (2 * VGA_idx);
		break;
	case 0:
		rx_pwr_all = -2 - (2 * VGA_idx);
		break;
	default:
		/*rx_pwr_all = -53+(2*(31-VGA_idx));*/
		/*dbg_print("wrong LNA index\n");*/
		break;

	}
	return	rx_pwr_all;
}
#endif

#if (RTL8195A_SUPPORT == 1)
s8
phydm_cck_rssi_8195a(
	struct PHY_DM_STRUCT	*p_dm,
	u16		LNA_idx,
	u8		VGA_idx
)
{
	s8	rx_pwr_all = 0;
	s8	lna_gain = 0;
	s8	lna_gain_table_0[8] = {0, -8, -15, -22, -29, -36, -45, -54};
	s8	lna_gain_table_1[8] = {0, -8, -15, -22, -29, -36, -45, -54};/*use 8195A to calibrate this table. 2016.06.24, Dino*/

	if (p_dm->cck_agc_report_type == 0)
		lna_gain = lna_gain_table_0[LNA_idx];
	else
		lna_gain = lna_gain_table_1[LNA_idx];

	rx_pwr_all = lna_gain - (2 * VGA_idx);

	return	rx_pwr_all;
}
#endif

#if (RTL8192E_SUPPORT == 1)
s8
phydm_cck_rssi_8192e(
	struct PHY_DM_STRUCT	*p_dm,
	u16		LNA_idx,
	u8		VGA_idx
)
{
	s8	rx_pwr_all = 0;
	s8	lna_gain = 0;
	s8	lna_gain_table_0[8] = {15, 9, -10, -21, -23, -27, -43, -44};
	s8	lna_gain_table_1[8] = {24, 18, 13, -4, -11, -18, -31, -36};/*use 8192EU to calibrate this table. 2015.12.15, Dino*/

	if (p_dm->cck_agc_report_type == 0)
		lna_gain = lna_gain_table_0[LNA_idx];
	else
		lna_gain = lna_gain_table_1[LNA_idx];

	rx_pwr_all = lna_gain - (2 * VGA_idx);

	return	rx_pwr_all;
}
#endif

#if (RTL8188E_SUPPORT == 1)
s8
phydm_cck_rssi_8188e(
	struct PHY_DM_STRUCT	*p_dm,
	u16		LNA_idx,
	u8		VGA_idx
)
{
	s8	rx_pwr_all = 0;
	s8	lna_gain = 0;
	s8	lna_gain_table_0[8] = {17, -1, -13, -29, -32, -35, -38, -41};/*only use lna0/1/2/3/7*/
	s8	lna_gain_table_1[8] = {29, 20, 12, 3, -6, -15, -24, -33}; /*only use lna3 /7*/

	if (p_dm->cut_version >= ODM_CUT_I) /*SMIC*/
		lna_gain = lna_gain_table_0[LNA_idx];
	else	 /*TSMC*/
		lna_gain = lna_gain_table_1[LNA_idx];

	rx_pwr_all = lna_gain - (2 * VGA_idx);

	return	rx_pwr_all;
}
#endif

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
void
phydm_rx_phy_status92c_series_parsing(
	struct PHY_DM_STRUCT					*p_dm,
	struct phydm_phyinfo_struct			*p_phy_info,
	u8						*p_phy_status,
	struct phydm_perpkt_info_struct			*p_pktinfo
)
{
	u8				i, max_spatial_stream;
	s8				rx_pwr[4], rx_pwr_all = 0;
	u8				EVM, PWDB_ALL = 0, PWDB_ALL_BT;
	u8				RSSI, total_rssi = 0;
	boolean				is_cck_rate = false;
	u8				rf_rx_num = 0;
	u8				LNA_idx = 0;
	u8				VGA_idx = 0;
	u8				cck_agc_rpt;
	u8				num_ss;
	u8				stream_rxevm_tmp = 0;
	struct _phy_status_rpt_8192cd *p_phy_sta_rpt = (struct _phy_status_rpt_8192cd *)p_phy_status;

	is_cck_rate = (p_pktinfo->data_rate <= ODM_RATE11M) ? true : false;
	p_dm->rate_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);

	if (p_pktinfo->is_to_self)
		p_dm->curr_station_id = p_pktinfo->station_id;

	p_phy_info->rx_mimo_signal_quality[RF_PATH_A] = -1;
	p_phy_info->rx_mimo_signal_quality[RF_PATH_B] = -1;


	if (is_cck_rate) {
		p_dm->phy_dbg_info.num_qry_phy_status_cck++;
		cck_agc_rpt = p_phy_sta_rpt->cck_agc_rpt_ofdm_cfosho_a;

		if (p_dm->support_ic_type & (ODM_RTL8703B)) {

#if (RTL8703B_SUPPORT == 1)
			if (p_dm->cck_agc_report_type == 1) {  /*4 bit LNA*/

				u8 cck_agc_rpt_b = (p_phy_sta_rpt->cck_rpt_b_ofdm_cfosho_b & BIT(7)) ? 1 : 0;

				LNA_idx = (cck_agc_rpt_b << 3) | ((cck_agc_rpt & 0xE0) >> 5);
				VGA_idx = (cck_agc_rpt & 0x1F);

				rx_pwr_all = phydm_cck_rssi_8703B(LNA_idx, VGA_idx);
			}
#endif
		} else { /*3 bit LNA*/

			LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
			VGA_idx = (cck_agc_rpt & 0x1F);

			if (p_dm->support_ic_type & (ODM_RTL8188E)) {

#if (RTL8188E_SUPPORT == 1)
				rx_pwr_all = phydm_cck_rssi_8188e(p_dm, LNA_idx, VGA_idx);
				/**/
#endif
			}
#if (RTL8192E_SUPPORT == 1)
			else if (p_dm->support_ic_type & (ODM_RTL8192E)) {

				rx_pwr_all = phydm_cck_rssi_8192e(p_dm, LNA_idx, VGA_idx);
				/**/
			}
#endif
#if (RTL8723B_SUPPORT == 1)
			else if (p_dm->support_ic_type & (ODM_RTL8723B)) {

				rx_pwr_all = odm_CCKRSSI_8723B(LNA_idx, VGA_idx);
				/**/
			}
#endif
#if (RTL8188F_SUPPORT == 1)
			else if (p_dm->support_ic_type & (ODM_RTL8188F)) {

				rx_pwr_all = odm_CCKRSSI_8188F(LNA_idx, VGA_idx);
				/**/
			}
#endif
#if (RTL8195A_SUPPORT == 1)
			else if (p_dm->support_ic_type & (ODM_RTL8195A)) {

				rx_pwr_all = phydm_cck_rssi_8195a(LNA_idx, VGA_idx);
				/**/
			}
#endif
		}

		PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("ext_lna_gain (( %d )), LNA_idx: (( 0x%x )), VGA_idx: (( 0x%x )), rx_pwr_all: (( %d ))\n",
			p_dm->ext_lna_gain, LNA_idx, VGA_idx, rx_pwr_all));

		if (p_dm->board_type & ODM_BOARD_EXT_LNA)
			rx_pwr_all -= p_dm->ext_lna_gain;

		PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);

		if (p_pktinfo->is_to_self) {
			p_dm->cck_lna_idx = LNA_idx;
			p_dm->cck_vga_idx = VGA_idx;
		}
		p_phy_info->rx_pwdb_all = PWDB_ALL;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		p_phy_info->bt_rx_rssi_percentage = PWDB_ALL;
		p_phy_info->recv_signal_power = rx_pwr_all;
#endif
		
		/* (3) Get Signal Quality (EVM) */
		
		
		{
			u8	SQ;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO)
				SQ = phydm_sq_patch_rt_cid_819x_lenovo(p_dm, is_cck_rate, PWDB_ALL, 0, 0);
			else if (p_dm->iot_table.win_patch_id == RT_CID_819X_ACER)
				SQ = phydm_sq_patch_rt_cid_819x_acer(p_dm, is_cck_rate, PWDB_ALL, 0, 0);
			else
#endif
				SQ = phydm_get_signal_quality(p_phy_info, p_dm, p_phy_sta_rpt);

			/* dbg_print("cck SQ = %d\n", SQ); */
			p_phy_info->signal_quality = SQ;
			p_phy_info->rx_mimo_signal_quality[RF_PATH_A] = SQ;
			p_phy_info->rx_mimo_signal_quality[RF_PATH_B] = -1;
		}

		for (i = RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			if (i == 0)
				p_phy_info->rx_mimo_signal_strength[0] = PWDB_ALL;
			else
				p_phy_info->rx_mimo_signal_strength[1] = 0;
		}
	} else { /* 2 is OFDM rate */
		p_dm->phy_dbg_info.num_qry_phy_status_ofdm++;

		/*  */
		/* (1)Get RSSI for HT rate */
		/*  */

		for (i = RF_PATH_A; i < ODM_RF_PATH_MAX; i++) {
			/* 2008/01/30 MH we will judge RF RX path now. */
			if (p_dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;
			/* else */
			/* continue; */

			rx_pwr[i] = ((p_phy_sta_rpt->path_agc[i].gain & 0x3F) * 2) - 110;

			if (p_pktinfo->is_to_self) {
				p_dm->ofdm_agc_idx[i] = (p_phy_sta_rpt->path_agc[i].gain & 0x3F);
				/**/
			}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
			p_phy_info->rx_pwr[i] = rx_pwr[i];
#endif

			/* Translate DBM to percentage. */
			RSSI = phydm_query_rx_pwr_percentage(rx_pwr[i]);
			total_rssi += RSSI;
			/* RT_DISP(FRX, RX_PHY_SS, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI)); */

			p_phy_info->rx_mimo_signal_strength[i] = (u8) RSSI;

			/* Get Rx snr value in DB */
			p_phy_info->rx_snr[i] = p_dm->phy_dbg_info.rx_snr_db[i] = (s8)(p_phy_sta_rpt->path_rxsnr[i] / 2);

			/* Record Signal Strength for next packet */

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			if (i == RF_PATH_A) {
				if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
					p_phy_info->signal_quality = phydm_sq_patch_rt_cid_819x_lenovo(p_dm, is_cck_rate, PWDB_ALL, i, RSSI);
				} else if (p_dm->iot_table.win_patch_id == RT_CID_819X_ACER)
					p_phy_info->signal_quality = phydm_sq_patch_rt_cid_819x_acer(p_dm, is_cck_rate, PWDB_ALL, 0, RSSI);
			}
#endif
		}


		/*  */
		/* (2)PWDB, Average PWDB calculated by hardware (for rate adaptive) */
		/*  */
		rx_pwr_all = (((p_phy_sta_rpt->cck_sig_qual_ofdm_pwdb_all) >> 1) & 0x7f) - 110;

		PWDB_ALL_BT = PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);


		p_phy_info->rx_pwdb_all = PWDB_ALL;
		/* PHYDM_DBG(p_dm,DBG_RSSI_MNTR, ("ODM OFDM RSSI=%d\n",p_phy_info->rx_pwdb_all)); */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		p_phy_info->bt_rx_rssi_percentage = PWDB_ALL_BT;
		p_phy_info->rx_power = rx_pwr_all;
		p_phy_info->recv_signal_power = rx_pwr_all;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
			/* do nothing */
		} else if (p_dm->iot_table.win_patch_id == RT_CID_819X_ACER) {
			/* do nothing */
		} else 
#endif
		{
			/* (3)EVM of HT rate */

			if (p_pktinfo->data_rate >= ODM_RATEMCS8 && p_pktinfo->data_rate <= ODM_RATEMCS15)
				max_spatial_stream = 2; /* both spatial stream make sense */
			else
				max_spatial_stream = 1; /* only spatial stream 1 makes sense */

			for (i = 0; i < max_spatial_stream; i++) {
				/* Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment */
				/* fill most significant bit to "zero" when doing shifting operation which may change a negative */
				/* value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore. */
				EVM = phydm_evm_db_to_percentage((p_phy_sta_rpt->stream_rxevm[i]));	/* dbm */

				/* GET_RX_STATUS_DESC_RX_MCS(p_desc), p_drv_info->rxevm[i], "%", EVM)); */

				if (i == RF_PATH_A) /* Fill value in RFD, Get the first spatial stream only */
					p_phy_info->signal_quality = (u8)(EVM & 0xff);
				
				p_phy_info->rx_mimo_signal_quality[i] = (u8)(EVM & 0xff);

				if (p_phy_sta_rpt->stream_rxevm[i] < 0)
					stream_rxevm_tmp = (u8)(0 - (p_phy_sta_rpt->stream_rxevm[i]));

				if (stream_rxevm_tmp == 64)
					stream_rxevm_tmp = 0;

				p_phy_info->rx_mimo_evm_dbm[i] = stream_rxevm_tmp;

			}
		}

		num_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);
		odm_parsing_cfo(p_dm, p_pktinfo, p_phy_sta_rpt->path_cfotail, num_ss);

	}
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	/* UI BSS List signal strength(in percentage), make it good looking, from 0~100. */
	/* It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp(). */
	if (is_cck_rate) {
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		/* 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/ */
		p_phy_info->signal_strength = SignalScaleProc(p_dm->adapter, PWDB_ALL, true, true);
#else
		p_phy_info->signal_strength = (u8)(phydm_signal_scale_mapping(p_dm, PWDB_ALL));/*PWDB_ALL;*/
#endif /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/
	} else {
		if (rf_rx_num != 0) {
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			/* 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/ */
			p_phy_info->signal_strength = SignalScaleProc(p_dm->adapter, (total_rssi /= rf_rx_num), true, false);
#else
			p_phy_info->signal_strength = (u8)(phydm_signal_scale_mapping(p_dm, total_rssi /= rf_rx_num));
#endif
		}
	}
#endif /*#if (DM_ODM_SUPPORT_TYPE &  (ODM_WIN|ODM_CE))*/

	/* dbg_print("is_cck_rate = %d, p_phy_info->rx_pwdb_all = %d, p_phy_sta_rpt->cck_agc_rpt_ofdm_cfosho_a = 0x%x\n", */
	/* is_cck_rate, p_phy_info->rx_pwdb_all, p_phy_sta_rpt->cck_agc_rpt_ofdm_cfosho_a); */

	/* For 92C/92D HW (Hybrid) Antenna Diversity */
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	/* For 88E HW Antenna Diversity */
	p_dm->dm_fat_table.antsel_rx_keep_0 = p_phy_sta_rpt->ant_sel;
	p_dm->dm_fat_table.antsel_rx_keep_1 = p_phy_sta_rpt->ant_sel_b;
	p_dm->dm_fat_table.antsel_rx_keep_2 = p_phy_sta_rpt->antsel_rx_keep_2;
#endif

	if (p_pktinfo->is_packet_match_bssid) {
		phydm_avg_phystatus_index(p_dm, p_phy_info, p_pktinfo);
		phydm_rx_statistic_cal(p_dm, p_phy_status, p_pktinfo);
	}

}
#endif

#if	ODM_IC_11AC_SERIES_SUPPORT

void
phydm_rx_phy_bw_jaguar_series_parsing(
	struct phydm_phyinfo_struct			*p_phy_info,
	struct phydm_perpkt_info_struct			*p_pktinfo,
	struct _phy_status_rpt_8812		*p_phy_sta_rpt
)
{

	if (p_pktinfo->data_rate <= ODM_RATE54M) {
		switch (p_phy_sta_rpt->r_RFMOD) {
		case 1:
			if (p_phy_sta_rpt->sub_chnl == 0)
				p_phy_info->band_width = 1;
			else
				p_phy_info->band_width = 0;
			break;

		case 2:
			if (p_phy_sta_rpt->sub_chnl == 0)
				p_phy_info->band_width = 2;
			else if (p_phy_sta_rpt->sub_chnl == 9 || p_phy_sta_rpt->sub_chnl == 10)
				p_phy_info->band_width = 1;
			else
				p_phy_info->band_width = 0;
			break;

		default:
		case 0:
			p_phy_info->band_width = 0;
			break;
		}
	}

}

void
phydm_rx_phy_status_jaguar_series_parsing(
	struct PHY_DM_STRUCT		*p_dm,
	struct phydm_phyinfo_struct	*p_phy_info,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct		*p_pktinfo
)
{
	u8					i, max_spatial_stream;
	s8					rx_pwr[4], rx_pwr_all = 0;
	u8					EVM = 0, evm_dbm, PWDB_ALL = 0, PWDB_ALL_BT;
	u8					RSSI, avg_rssi = 0, best_rssi = 0, second_rssi = 0;
	u8					is_cck_rate = 0;
	u8					rf_rx_num = 0;
	u8					cck_highpwr = 0;
	u8					LNA_idx, VGA_idx;
	struct _phy_status_rpt_8812 *p_phy_sta_rpt = (struct _phy_status_rpt_8812 *)p_phy_status;
	struct phydm_fat_struct					*p_dm_fat_table = &p_dm->dm_fat_table;
	u8					num_ss;

	phydm_rx_phy_bw_jaguar_series_parsing(p_phy_info, p_pktinfo, p_phy_sta_rpt);

	is_cck_rate = (p_pktinfo->data_rate <= ODM_RATE11M) ? true : false;
	p_dm->rate_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);

	if (p_pktinfo->is_to_self)
		p_dm->curr_station_id = p_pktinfo->station_id;
	else
		p_dm->curr_station_id = 0xff;

	p_phy_info->rx_mimo_signal_quality[RF_PATH_A] = -1;
	p_phy_info->rx_mimo_signal_quality[RF_PATH_B] = -1;
	p_phy_info->rx_mimo_signal_quality[RF_PATH_C] = -1;
	p_phy_info->rx_mimo_signal_quality[RF_PATH_D] = -1;

	if (is_cck_rate) {
		u8 cck_agc_rpt;

		p_dm->phy_dbg_info.num_qry_phy_status_cck++;

		/*(1)Hardware does not provide RSSI for CCK*/
		/*(2)PWDB, Average PWDB calculated by hardware (for rate adaptive)*/

		/*if(p_hal_data->e_rf_power_state == e_rf_on)*/
		cck_highpwr = p_dm->is_cck_high_power;
		/*else*/
		/*cck_highpwr = false;*/

		cck_agc_rpt = p_phy_sta_rpt->cfosho[0];
		LNA_idx = ((cck_agc_rpt & 0xE0) >> 5);
		VGA_idx = (cck_agc_rpt & 0x1F);

		if (p_dm->support_ic_type == ODM_RTL8812) {
			switch (LNA_idx) {
			case 7:
				if (VGA_idx <= 27)
					rx_pwr_all = -100 + 2 * (27 - VGA_idx); /*VGA_idx = 27~2*/
				else
					rx_pwr_all = -100;
				break;
			case 6:
				rx_pwr_all = -48 + 2 * (2 - VGA_idx); /*VGA_idx = 2~0*/
				break;
			case 5:
				rx_pwr_all = -42 + 2 * (7 - VGA_idx); /*VGA_idx = 7~5*/
				break;
			case 4:
				rx_pwr_all = -36 + 2 * (7 - VGA_idx); /*VGA_idx = 7~4*/
				break;
			case 3:
				/*rx_pwr_all = -28 + 2*(7-VGA_idx); VGA_idx = 7~0*/
				rx_pwr_all = -24 + 2 * (7 - VGA_idx); /*VGA_idx = 7~0*/
				break;
			case 2:
				if (cck_highpwr)
					rx_pwr_all = -12 + 2 * (5 - VGA_idx); /*VGA_idx = 5~0*/
				else
					rx_pwr_all = -6 + 2 * (5 - VGA_idx);
				break;
			case 1:
				rx_pwr_all = 8 - 2 * VGA_idx;
				break;
			case 0:
				rx_pwr_all = 14 - 2 * VGA_idx;
				break;
			default:
				/*dbg_print("CCK Exception default\n");*/
				break;
			}
			rx_pwr_all += 6;
			PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);

			if (cck_highpwr == false) {
				if (PWDB_ALL >= 80)
					PWDB_ALL = ((PWDB_ALL - 80) << 1) + ((PWDB_ALL - 80) >> 1) + 80;
				else if ((PWDB_ALL <= 78) && (PWDB_ALL >= 20))
					PWDB_ALL += 3;
				if (PWDB_ALL > 100)
					PWDB_ALL = 100;
			}
		} else if (p_dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A)) {
			s8 pout = -6;

			switch (LNA_idx) {
			case 5:
				rx_pwr_all = pout - 32 - (2 * VGA_idx);
				break;
			case 4:
				rx_pwr_all = pout - 24 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = pout - 11 - (2 * VGA_idx);
				break;
			case 1:
				rx_pwr_all = pout + 5 - (2 * VGA_idx);
				break;
			case 0:
				rx_pwr_all = pout + 21 - (2 * VGA_idx);
				break;
			}
			PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);
		} else if (p_dm->support_ic_type == ODM_RTL8814A || p_dm->support_ic_type == ODM_RTL8822B) {
			s8 pout = -6;

			switch (LNA_idx) {
			/*CCK only use LNA: 2, 3, 5, 7*/
			case 7:
				rx_pwr_all = pout - 32 - (2 * VGA_idx);
				break;
			case 5:
				rx_pwr_all = pout - 22 - (2 * VGA_idx);
				break;
			case 3:
				rx_pwr_all = pout - 2 - (2 * VGA_idx);
				break;
			case 2:
				rx_pwr_all = pout + 5 - (2 * VGA_idx);
				break;
			/*case 6:*/
			/*rx_pwr_all = pout -26 - (2*VGA_idx);*/
			/*break;*/
			/*case 4:*/
			/*rx_pwr_all = pout - 8 - (2*VGA_idx);*/
			/*break;*/
			/*case 1:*/
			/*rx_pwr_all = pout + 21 - (2*VGA_idx);*/
			/*break;*/
			/*case 0:*/
			/*rx_pwr_all = pout + 10 - (2*VGA_idx);*/
			/*	break; */
			default:
				/* dbg_print("CCK Exception default\n"); */
				break;
			}
			PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);
		}

		p_dm->cck_lna_idx = LNA_idx;
		p_dm->cck_vga_idx = VGA_idx;
		p_phy_info->rx_pwdb_all = PWDB_ALL;
		/* if(p_pktinfo->station_id == 0) */
		/* { */
		/*	dbg_print("CCK: LNA_idx = %d, VGA_idx = %d, p_phy_info->rx_pwdb_all = %d\n", */
		/*		LNA_idx, VGA_idx, p_phy_info->rx_pwdb_all); */
		/* } */
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		p_phy_info->bt_rx_rssi_percentage = PWDB_ALL;
		p_phy_info->recv_signal_power = rx_pwr_all;
#endif
		/*(3) Get Signal Quality (EVM)*/
		/*if (p_pktinfo->is_packet_match_bssid)*/
		{
			u8	SQ, SQ_rpt;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO)
				SQ = phydm_sq_patch_rt_cid_819x_lenovo(p_dm, is_cck_rate, PWDB_ALL, 0, 0);
			else 
#endif
			if (p_phy_info->rx_pwdb_all > 40 && !p_dm->is_in_hct_test)
				SQ = 100;
			else {
				SQ_rpt = p_phy_sta_rpt->pwdb_all;

				if (SQ_rpt > 64)
					SQ = 0;
				else if (SQ_rpt < 20)
					SQ = 100;
				else
					SQ = ((64 - SQ_rpt) * 100) / 44;
			}

			/* dbg_print("cck SQ = %d\n", SQ); */
			p_phy_info->signal_quality = SQ;
			p_phy_info->rx_mimo_signal_quality[RF_PATH_A] = SQ;
		}

		for (i = RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (i == 0)
				p_phy_info->rx_mimo_signal_strength[0] = PWDB_ALL;
			else
				p_phy_info->rx_mimo_signal_strength[i] = 0;
		}
	} else {
		/*is OFDM rate*/
		p_dm_fat_table->hw_antsw_occur = p_phy_sta_rpt->hw_antsw_occur;

		p_dm->phy_dbg_info.num_qry_phy_status_ofdm++;

		/*(1)Get RSSI for OFDM rate*/

		for (i = RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			/*2008/01/30 MH we will judge RF RX path now.*/
			/* dbg_print("p_dm->rf_path_rx_enable = %x\n", p_dm->rf_path_rx_enable); */
			if (p_dm->rf_path_rx_enable & BIT(i))
				rf_rx_num++;
			/* else */
			/* continue; */
			/*2012.05.25 LukeLee: Testchip AGC report is wrong, it should be restored back to old formula in MP chip*/
			/* if((p_dm->support_ic_type & (ODM_RTL8812|ODM_RTL8821)) && (!p_dm->is_mp_chip)) */
			if (i < RF_PATH_C) {
				rx_pwr[i] = (p_phy_sta_rpt->gain_trsw[i] & 0x7F) - 110;

				if (p_pktinfo->is_to_self)
					p_dm->ofdm_agc_idx[i] = p_phy_sta_rpt->gain_trsw[i];

			} else
				rx_pwr[i] = (p_phy_sta_rpt->gain_trsw_cd[i - 2] & 0x7F) - 110;
			/* else */
			/*rx_pwr[i] = ((p_phy_sta_rpt->gain_trsw[i]& 0x3F)*2) - 110;  OLD FORMULA*/

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
			p_phy_info->rx_pwr[i] = rx_pwr[i];
#endif

			/* Translate DBM to percentage. */
			RSSI = phydm_query_rx_pwr_percentage(rx_pwr[i]);

			/*total_rssi += RSSI;*/
			/*Get the best two RSSI*/
			if (RSSI > best_rssi && RSSI > second_rssi) {
				second_rssi = best_rssi;
				best_rssi = RSSI;
			} else if (RSSI > second_rssi && RSSI <= best_rssi)
				second_rssi = RSSI;

			/*RT_DISP(FRX, RX_PHY_SS, ("RF-%d RXPWR=%x RSSI=%d\n", i, rx_pwr[i], RSSI));*/

			p_phy_info->rx_mimo_signal_strength[i] = (u8) RSSI;


			/*Get Rx snr value in DB*/
			if (i < RF_PATH_C)
				p_phy_info->rx_snr[i] = p_dm->phy_dbg_info.rx_snr_db[i] = p_phy_sta_rpt->rxsnr[i] / 2;
			else if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B))
				p_phy_info->rx_snr[i] = p_dm->phy_dbg_info.rx_snr_db[i] = p_phy_sta_rpt->csi_current[i - 2] / 2;

#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
			/*(2) CFO_short  & CFO_tail*/
			if (i < RF_PATH_C) {
				p_phy_info->cfo_short[i] = phydm_cfo((p_phy_sta_rpt->cfosho[i]));
				p_phy_info->cfo_tail[i] = phydm_cfo((p_phy_sta_rpt->cfotail[i]));
			}
#endif
			/* Record Signal Strength for next packet */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			if (p_pktinfo->is_packet_match_bssid && (i == RF_PATH_A)) {
				if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
					p_phy_info->signal_quality = phydm_sq_patch_rt_cid_819x_lenovo(p_dm, is_cck_rate, PWDB_ALL, i, RSSI);
				}
			}
#endif
		}

		/*(3)PWDB, Average PWDB calculated by hardware (for rate adaptive)*/

		/*2012.05.25 LukeLee: Testchip AGC report is wrong, it should be restored back to old formula in MP chip*/
		if ((p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8881A)) && (!p_dm->is_mp_chip))
			rx_pwr_all = (p_phy_sta_rpt->pwdb_all & 0x7f) - 110;
		else
			rx_pwr_all = (((p_phy_sta_rpt->pwdb_all) >> 1) & 0x7f) - 110;	 /*OLD FORMULA*/

		PWDB_ALL_BT = PWDB_ALL = phydm_query_rx_pwr_percentage(rx_pwr_all);

		p_phy_info->rx_pwdb_all = PWDB_ALL;
		/*PHYDM_DBG(p_dm,DBG_RSSI_MNTR, ("ODM OFDM RSSI=%d\n",p_phy_info->rx_pwdb_all));*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
		p_phy_info->bt_rx_rssi_percentage = PWDB_ALL_BT;
		p_phy_info->rx_power = rx_pwr_all;
		p_phy_info->recv_signal_power = rx_pwr_all;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		if (p_dm->iot_table.win_patch_id == RT_CID_819X_LENOVO) {
			/*do nothing*/
		} else 
#endif
		{
			/*(4)EVM of OFDM rate*/

			if ((p_pktinfo->data_rate >= ODM_RATEMCS8) &&
				(p_pktinfo->data_rate <= ODM_RATEMCS15))
				max_spatial_stream = 2;
			else if ((p_pktinfo->data_rate >= ODM_RATEVHTSS2MCS0) &&
				 (p_pktinfo->data_rate <= ODM_RATEVHTSS2MCS9))
				max_spatial_stream = 2;
			else if ((p_pktinfo->data_rate >= ODM_RATEMCS16) &&
				 (p_pktinfo->data_rate <= ODM_RATEMCS23))
				max_spatial_stream = 3;
			else if ((p_pktinfo->data_rate >= ODM_RATEVHTSS3MCS0) &&
				 (p_pktinfo->data_rate <= ODM_RATEVHTSS3MCS9))
				max_spatial_stream = 3;
			else
				max_spatial_stream = 1;

			/*if (p_pktinfo->is_packet_match_bssid) */
			{
				/*dbg_print("p_pktinfo->data_rate = %d\n", p_pktinfo->data_rate);*/

				for (i = 0; i < max_spatial_stream; i++) {
					/*Do not use shift operation like "rx_evmX >>= 1" because the compilor of free build environment*/
					/*fill most significant bit to "zero" when doing shifting operation which may change a negative*/
					/*value to positive one, then the dbm value (which is supposed to be negative)  is not correct anymore.*/

					if (p_pktinfo->data_rate >= ODM_RATE6M && p_pktinfo->data_rate <= ODM_RATE54M) {
						if (i == RF_PATH_A) {
							EVM = phydm_evm_db_to_percentage((p_phy_sta_rpt->sigevm));	/*dbm*/
							EVM += 20;
							if (EVM > 100)
								EVM = 100;
						}
					} else {
						if (i < RF_PATH_C) {
							if (p_phy_sta_rpt->rxevm[i] == -128)
								p_phy_sta_rpt->rxevm[i] = -25;
							EVM = phydm_evm_db_to_percentage((p_phy_sta_rpt->rxevm[i]));	/*dbm*/
						} else {
							if (p_phy_sta_rpt->rxevm_cd[i - 2] == -128)
								p_phy_sta_rpt->rxevm_cd[i - 2] = -25;
							EVM = phydm_evm_db_to_percentage((p_phy_sta_rpt->rxevm_cd[i - 2]));	/*dbm*/
						}
					}

					if (i < RF_PATH_C)
						evm_dbm = phydm_evm_dbm_jaguar_series(p_phy_sta_rpt->rxevm[i]);
					else
						evm_dbm = phydm_evm_dbm_jaguar_series(p_phy_sta_rpt->rxevm_cd[i - 2]);
					/*RT_DISP(FRX, RX_PHY_SQ, ("RXRATE=%x RXEVM=%x EVM=%s%d\n",*/
					/*p_pktinfo->data_rate, p_phy_sta_rpt->rxevm[i], "%", EVM));*/

					{
						if (i == RF_PATH_A) {
							/*Fill value in RFD, Get the first spatial stream only*/
							p_phy_info->signal_quality = EVM;
						}
						p_phy_info->rx_mimo_signal_quality[i] = EVM;
#if (DM_ODM_SUPPORT_TYPE != ODM_AP)
						p_phy_info->rx_mimo_evm_dbm[i] = evm_dbm;
#endif
					}
				}
			}
		}

		num_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);
		odm_parsing_cfo(p_dm, p_pktinfo, p_phy_sta_rpt->cfotail, num_ss);

	}
	/* dbg_print("is_cck_rate= %d, p_phy_info->signal_strength=%d % PWDB_AL=%d rf_rx_num=%d\n", is_cck_rate, p_phy_info->signal_strength, PWDB_ALL, rf_rx_num); */

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	/*UI BSS List signal strength(in percentage), make it good looking, from 0~100.*/
	/*It is assigned to the BSS List in GetValueFromBeaconOrProbeRsp().*/
	if (is_cck_rate) {
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		/*2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/*/
		p_phy_info->signal_strength = SignalScaleProc(p_dm->adapter, PWDB_ALL, false, true);
#else
		p_phy_info->signal_strength = (u8)(phydm_signal_scale_mapping(p_dm, PWDB_ALL));/*PWDB_ALL;*/
#endif
	} else {
		if (rf_rx_num != 0) {
			/* 2015/01 Sean, use the best two RSSI only, suggested by Ynlin and ChenYu.*/
			if (rf_rx_num == 1)
				avg_rssi = best_rssi;
			else
				avg_rssi = (best_rssi + second_rssi) / 2;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
			/* 2012/01/12 MH Use customeris signal strength from HalComRxdDesc.c/*/
			p_phy_info->signal_strength = SignalScaleProc(p_dm->adapter, avg_rssi, false, false);
#else
			p_phy_info->signal_strength = (u8)(phydm_signal_scale_mapping(p_dm, avg_rssi));
#endif
		}
	}
#endif
	p_dm->rx_pwdb_ave = p_dm->rx_pwdb_ave + p_phy_info->rx_pwdb_all;

	p_dm->dm_fat_table.antsel_rx_keep_0 = p_phy_sta_rpt->antidx_anta;
	p_dm->dm_fat_table.antsel_rx_keep_1 = p_phy_sta_rpt->antidx_antb;
	p_dm->dm_fat_table.antsel_rx_keep_2 = p_phy_sta_rpt->antidx_antc;
	p_dm->dm_fat_table.antsel_rx_keep_3 = p_phy_sta_rpt->antidx_antd;

	if (p_pktinfo->is_packet_match_bssid) {
		phydm_avg_phystatus_index(p_dm, p_phy_info, p_pktinfo);
		phydm_rx_statistic_cal(p_dm, p_phy_status, p_pktinfo);
	}
	/*PHYDM_DBG(p_dm, DBG_ANT_DIV, ("StaID[%d]:  antidx_anta = ((%d)), MatchBSSID =  ((%d))\n", p_pktinfo->station_id, p_phy_sta_rpt->antidx_anta, p_pktinfo->is_packet_match_bssid));*/


	/*		dbg_print("p_phy_sta_rpt->antidx_anta = %d, p_phy_sta_rpt->antidx_antb = %d\n",*/
	/*			p_phy_sta_rpt->antidx_anta, p_phy_sta_rpt->antidx_antb);*/
	/*		dbg_print("----------------------------\n");*/
	/*		dbg_print("p_pktinfo->station_id=%d, p_pktinfo->data_rate=0x%x\n",p_pktinfo->station_id, p_pktinfo->data_rate);*/
	/*		dbg_print("p_phy_sta_rpt->r_RFMOD = %d\n", p_phy_sta_rpt->r_RFMOD);*/
	/*		dbg_print("p_phy_sta_rpt->gain_trsw[0]=0x%x, p_phy_sta_rpt->gain_trsw[1]=0x%x\n",*/
	/*				p_phy_sta_rpt->gain_trsw[0],p_phy_sta_rpt->gain_trsw[1]);*/
	/*		dbg_print("p_phy_sta_rpt->gain_trsw[2]=0x%x, p_phy_sta_rpt->gain_trsw[3]=0x%x\n",*/
	/*				p_phy_sta_rpt->gain_trsw_cd[0],p_phy_sta_rpt->gain_trsw_cd[1]);*/
	/*		dbg_print("p_phy_sta_rpt->pwdb_all = 0x%x, p_phy_info->rx_pwdb_all = %d\n", p_phy_sta_rpt->pwdb_all, p_phy_info->rx_pwdb_all);*/
	/*		dbg_print("p_phy_sta_rpt->cfotail[i] = 0x%x, p_phy_sta_rpt->CFO_tail[i] = 0x%x\n", p_phy_sta_rpt->cfotail[0], p_phy_sta_rpt->cfotail[1]);*/
	/*		dbg_print("p_phy_sta_rpt->rxevm[0] = %d, p_phy_sta_rpt->rxevm[1] = %d\n", p_phy_sta_rpt->rxevm[0], p_phy_sta_rpt->rxevm[1]);*/
	/*		dbg_print("p_phy_sta_rpt->rxevm[2] = %d, p_phy_sta_rpt->rxevm[3] = %d\n", p_phy_sta_rpt->rxevm_cd[0], p_phy_sta_rpt->rxevm_cd[1]);*/
	/*		dbg_print("p_phy_info->rx_mimo_signal_strength[0]=%d, p_phy_info->rx_mimo_signal_strength[1]=%d, rx_pwdb_all=%d\n",*/
	/*				p_phy_info->rx_mimo_signal_strength[0], p_phy_info->rx_mimo_signal_strength[1], p_phy_info->rx_pwdb_all);*/
	/*		dbg_print("p_phy_info->rx_mimo_signal_strength[2]=%d, p_phy_info->rx_mimo_signal_strength[3]=%d\n",*/
	/*				p_phy_info->rx_mimo_signal_strength[2], p_phy_info->rx_mimo_signal_strength[3]);*/
	/*		dbg_print("ppPhyInfo->rx_mimo_signal_quality[0]=%d, p_phy_info->rx_mimo_signal_quality[1]=%d\n",*/
	/*				p_phy_info->rx_mimo_signal_quality[0], p_phy_info->rx_mimo_signal_quality[1]);*/
	/*		dbg_print("ppPhyInfo->rx_mimo_signal_quality[2]=%d, p_phy_info->rx_mimo_signal_quality[3]=%d\n",*/
	/*				p_phy_info->rx_mimo_signal_quality[2], p_phy_info->rx_mimo_signal_quality[3]);*/

}

#endif

void
phydm_reset_rssi_for_dm(
	struct PHY_DM_STRUCT	*p_dm,
	u8		station_id
)
{
	struct cmn_sta_info		*p_sta;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
#endif
	p_sta = p_dm->p_phydm_sta_info[station_id];

	if (!is_sta_active(p_sta)) {
		/**/
		return;
	}
	PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("Reset RSSI for macid = (( %d ))\n", station_id));


	p_sta->rssi_stat.rssi_cck = -1;
	p_sta->rssi_stat.rssi_ofdm = -1;
	p_sta->rssi_stat.rssi = -1;
	p_sta->rssi_stat.ofdm_pkt_cnt = 0;
	p_sta->rssi_stat.cck_pkt_cnt = 0;
	p_sta->rssi_stat.cck_sum_power = 0;
	p_sta->rssi_stat.is_send_rssi = RA_RSSI_STATE_INIT;
	p_sta->rssi_stat.packet_map = 0;
	p_sta->rssi_stat.valid_bit = 0;

	/*in WIN Driver: sta_ID==0->p_entry==NULL -> default port HAL_Data*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	p_sta->dm_ctrl = p_sta->dm_ctrl & (~STA_DM_CTRL_ACTIVE);
	if (station_id == 0) {

		p_hal_data->UndecoratedSmoothedPWDB = -1;
		/**/
	}
#endif

}

void
phydm_process_rssi_for_dm(
	struct PHY_DM_STRUCT					*p_dm,
	struct phydm_phyinfo_struct			*p_phy_info,
	struct phydm_perpkt_info_struct			*p_pktinfo
)
{

	s32			rssi_ave;
	s8			undecorated_smoothed_pwdb, undecorated_smoothed_cck, undecorated_smoothed_ofdm;
	u8			i, is_cck_rate = 0;
	u8			RSSI_max, RSSI_min;
	u32			weighting = 0;
	u8			send_rssi_2_fw = 0;
	struct cmn_sta_info		*p_sta;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct phydm_fat_struct			*p_dm_fat_table = &p_dm->dm_fat_table;
	struct _ADAPTER		*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
#endif

	if (p_pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

#ifdef CONFIG_S0S1_SW_ANTENNA_DIVERSITY
	odm_s0s1_sw_ant_div_by_ctrl_frame_process_rssi(p_dm, p_phy_info, p_pktinfo);
#endif

	p_sta = p_dm->p_phydm_sta_info[p_pktinfo->station_id];

	if (!is_sta_active(p_sta)) {
		return;
		/**/
	}

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	if ((p_dm->support_ability & ODM_BB_ANT_DIV) &&
	    (p_dm_fat_table->enable_ctrl_frame_antdiv)
	   ) {
		if (p_pktinfo->is_packet_match_bssid)
			p_dm->data_frame_num++;

		if ((p_dm_fat_table->use_ctrl_frame_antdiv)) {
			if (!p_pktinfo->is_to_self)/*data frame + CTRL frame*/
				return;
		} else {
			if ((!p_pktinfo->is_packet_match_bssid))/*data frame only*/
				return;
		}
	} else
#endif
	{
		if ((!p_pktinfo->is_packet_match_bssid))/*data frame only*/
			return;
	}

	if (p_pktinfo->is_packet_beacon)
		p_dm->phy_dbg_info.num_qry_beacon_pkt++;

	is_cck_rate = (p_pktinfo->data_rate <= ODM_RATE11M) ? true : false;
	p_dm->rx_rate = p_pktinfo->data_rate;

	/* --------------Statistic for antenna/path diversity------------------ */
	if (p_dm->support_ability & ODM_BB_ANT_DIV) {
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
		odm_process_rssi_for_ant_div(p_dm, p_phy_info, p_pktinfo);
#endif
	}
#if (defined(CONFIG_PATH_DIVERSITY))
	else if (p_dm->support_ability & ODM_BB_PATH_DIV)
		phydm_process_rssi_for_path_div(p_dm, p_phy_info, p_pktinfo);
#endif
	/* -----------------Smart Antenna Debug Message------------------ */

	undecorated_smoothed_cck =  p_sta->rssi_stat.rssi_cck;
	undecorated_smoothed_ofdm = p_sta->rssi_stat.rssi_ofdm;
	undecorated_smoothed_pwdb = p_sta->rssi_stat.rssi;

	if (p_pktinfo->is_packet_to_self || p_pktinfo->is_packet_beacon) {

		if (!is_cck_rate) { /* ofdm rate */
#if (RTL8814A_SUPPORT == 1) || (RTL8822B_SUPPORT == 1)
			if (p_dm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B)) {
				u8 RX_count = 0;
				u32 RSSI_linear = 0;

				if (p_dm->rx_ant_status & BB_PATH_A) {
					p_dm->RSSI_A = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
					RX_count++;
					RSSI_linear += odm_convert_to_linear(p_phy_info->rx_mimo_signal_strength[RF_PATH_A]);
				} else
					p_dm->RSSI_A = 0;

				if (p_dm->rx_ant_status & BB_PATH_B) {
					p_dm->RSSI_B = p_phy_info->rx_mimo_signal_strength[RF_PATH_B];
					RX_count++;
					RSSI_linear += odm_convert_to_linear(p_phy_info->rx_mimo_signal_strength[RF_PATH_B]);
				} else
					p_dm->RSSI_B = 0;

				if (p_dm->rx_ant_status & BB_PATH_C) {
					p_dm->RSSI_C = p_phy_info->rx_mimo_signal_strength[RF_PATH_C];
					RX_count++;
					RSSI_linear += odm_convert_to_linear(p_phy_info->rx_mimo_signal_strength[RF_PATH_C]);
				} else
					p_dm->RSSI_C = 0;

				if (p_dm->rx_ant_status & BB_PATH_D) {
					p_dm->RSSI_D = p_phy_info->rx_mimo_signal_strength[RF_PATH_D];
					RX_count++;
					RSSI_linear += odm_convert_to_linear(p_phy_info->rx_mimo_signal_strength[RF_PATH_D]);
				} else
					p_dm->RSSI_D = 0;

				/* Calculate average RSSI */
				switch (RX_count) {
				case 2:
					RSSI_linear = (RSSI_linear >> 1);
					break;
				case 3:
					RSSI_linear = ((RSSI_linear) + (RSSI_linear << 1) + (RSSI_linear << 3)) >> 5;	/* RSSI_linear/3 ~ RSSI_linear*11/32 */
					break;
				case 4:
					RSSI_linear = (RSSI_linear >> 2);
					break;
				}
				rssi_ave = odm_convert_to_db(RSSI_linear);
			} else
#endif
			{
				if (p_phy_info->rx_mimo_signal_strength[RF_PATH_B] == 0) {
					rssi_ave = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
					p_dm->RSSI_A = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
					p_dm->RSSI_B = 0;
				} else {
					/*dbg_print("p_rfd->status.rx_mimo_signal_strength[0] = %d, p_rfd->status.rx_mimo_signal_strength[1] = %d\n",*/
					/*p_rfd->status.rx_mimo_signal_strength[0], p_rfd->status.rx_mimo_signal_strength[1]);*/
					p_dm->RSSI_A =  p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
					p_dm->RSSI_B = p_phy_info->rx_mimo_signal_strength[RF_PATH_B];

					if (p_phy_info->rx_mimo_signal_strength[RF_PATH_A] > p_phy_info->rx_mimo_signal_strength[RF_PATH_B]) {
						RSSI_max = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
						RSSI_min = p_phy_info->rx_mimo_signal_strength[RF_PATH_B];
					} else {
						RSSI_max = p_phy_info->rx_mimo_signal_strength[RF_PATH_B];
						RSSI_min = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
					}
					if ((RSSI_max - RSSI_min) < 3)
						rssi_ave = RSSI_max;
					else if ((RSSI_max - RSSI_min) < 6)
						rssi_ave = RSSI_max - 1;
					else if ((RSSI_max - RSSI_min) < 10)
						rssi_ave = RSSI_max - 2;
					else
						rssi_ave = RSSI_max - 3;
				}
			}

			/* 1 Process OFDM RSSI */
			if (undecorated_smoothed_ofdm <= 0) {	/* initialize */
				undecorated_smoothed_ofdm = (s8)p_phy_info->rx_pwdb_all;
				PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("OFDM_INIT: (( %d ))\n", undecorated_smoothed_ofdm));
			} else {
				if (p_phy_info->rx_pwdb_all > (u32)undecorated_smoothed_ofdm) {
					undecorated_smoothed_ofdm =
						(s8)((((undecorated_smoothed_ofdm)*(RX_SMOOTH_FACTOR - 1)) +
						(rssi_ave)) / (RX_SMOOTH_FACTOR));
					undecorated_smoothed_ofdm = undecorated_smoothed_ofdm + 1;
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("OFDM_1: (( %d ))\n", undecorated_smoothed_ofdm));
				} else {
					undecorated_smoothed_ofdm =
						(s8)((((undecorated_smoothed_ofdm)*(RX_SMOOTH_FACTOR - 1)) +
						(rssi_ave)) / (RX_SMOOTH_FACTOR));
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("OFDM_2: (( %d ))\n", undecorated_smoothed_ofdm));
				}
			}
			if (p_sta->rssi_stat.ofdm_pkt_cnt != 64) {
				i = 63;
				p_sta->rssi_stat.ofdm_pkt_cnt -= (u8)(((p_sta->rssi_stat.packet_map >> i) & BIT(0)) - 1);
			}
			p_sta->rssi_stat.packet_map = (p_sta->rssi_stat.packet_map << 1) | BIT(0);

		} else {
			rssi_ave = p_phy_info->rx_pwdb_all;
			p_dm->RSSI_A = (u8) p_phy_info->rx_pwdb_all;
			p_dm->RSSI_B = 0xFF;
			p_dm->RSSI_C = 0xFF;
			p_dm->RSSI_D = 0xFF;

			if (p_sta->rssi_stat.cck_pkt_cnt <= 63)
				p_sta->rssi_stat.cck_pkt_cnt++;

			/* 1 Process CCK RSSI */
			if (undecorated_smoothed_cck <= 0) {	/* initialize */
				undecorated_smoothed_cck = (s8)p_phy_info->rx_pwdb_all;
				p_sta->rssi_stat.cck_sum_power = (u16)p_phy_info->rx_pwdb_all ; /*reset*/
				p_sta->rssi_stat.cck_pkt_cnt = 1; /*reset*/
				PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("CCK_INIT: (( %d ))\n", undecorated_smoothed_cck));
			} else if (p_sta->rssi_stat.cck_pkt_cnt <= CCK_RSSI_INIT_COUNT) {

				p_sta->rssi_stat.cck_sum_power = p_sta->rssi_stat.cck_sum_power + (u16)p_phy_info->rx_pwdb_all;
				undecorated_smoothed_cck = p_sta->rssi_stat.cck_sum_power / p_sta->rssi_stat.cck_pkt_cnt;

				PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("CCK_0: (( %d )), SumPow = (( %d )), cck_pkt = (( %d ))\n",
					undecorated_smoothed_cck, p_sta->rssi_stat.cck_sum_power, p_sta->rssi_stat.cck_pkt_cnt));
			} else {
				if (p_phy_info->rx_pwdb_all > (u32)undecorated_smoothed_cck) {
					undecorated_smoothed_cck =
						(s8)((((undecorated_smoothed_cck)*(RX_SMOOTH_FACTOR - 1)) +
						(p_phy_info->rx_pwdb_all)) / (RX_SMOOTH_FACTOR));
					undecorated_smoothed_cck = undecorated_smoothed_cck + 1;
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("CCK_1: (( %d ))\n", undecorated_smoothed_cck));
				} else {
					undecorated_smoothed_cck =
						(s8)((((undecorated_smoothed_cck)*(RX_SMOOTH_FACTOR - 1)) +
						(p_phy_info->rx_pwdb_all)) / (RX_SMOOTH_FACTOR));
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("CCK_2: (( %d ))\n", undecorated_smoothed_cck));
				}
			}
			i = 63;
			p_sta->rssi_stat.ofdm_pkt_cnt -= (u8)((p_sta->rssi_stat.packet_map >> i) & BIT(0));
			p_sta->rssi_stat.packet_map = p_sta->rssi_stat.packet_map << 1;
		}

		/* if(p_entry) */
		{
			/* 2011.07.28 LukeLee: modified to prevent unstable CCK RSSI */
			if (p_sta->rssi_stat.ofdm_pkt_cnt == 64) { /* speed up when all packets are OFDM*/
				undecorated_smoothed_pwdb = undecorated_smoothed_ofdm;
				PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("PWDB_0[%d] = (( %d ))\n", p_pktinfo->station_id, undecorated_smoothed_cck));
			} else {
				if (p_sta->rssi_stat.valid_bit < 64)
					p_sta->rssi_stat.valid_bit++;

				if (p_sta->rssi_stat.valid_bit == 64) {
					weighting = ((p_sta->rssi_stat.ofdm_pkt_cnt) > 4) ? 64 : (p_sta->rssi_stat.ofdm_pkt_cnt << 4);
					undecorated_smoothed_pwdb = (s8)((weighting * undecorated_smoothed_ofdm + (64 - weighting) * undecorated_smoothed_cck) >> 6);
					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("PWDB_1[%d] = (( %d )), W = (( %d ))\n", p_pktinfo->station_id, undecorated_smoothed_cck, weighting));
				} else {
					if (p_sta->rssi_stat.valid_bit != 0)
						undecorated_smoothed_pwdb =
						(p_sta->rssi_stat.ofdm_pkt_cnt * undecorated_smoothed_ofdm + (p_sta->rssi_stat.valid_bit - p_sta->rssi_stat.ofdm_pkt_cnt) * undecorated_smoothed_cck) / p_sta->rssi_stat.valid_bit;
					else
						undecorated_smoothed_pwdb = 0;

					PHYDM_DBG(p_dm, DBG_RSSI_MNTR, ("PWDB_2[%d] = (( %d )), ofdm_pkt = (( %d )), Valid_Bit = (( %d ))\n",
						p_pktinfo->station_id, undecorated_smoothed_cck, p_sta->rssi_stat.ofdm_pkt_cnt, p_sta->rssi_stat.valid_bit));
				}
			}


			if ((p_sta->rssi_stat.ofdm_pkt_cnt >= 1 || p_sta->rssi_stat.cck_pkt_cnt >= 5) && (p_sta->rssi_stat.is_send_rssi == RA_RSSI_STATE_INIT)) {

				send_rssi_2_fw = 1;
				p_sta->rssi_stat.is_send_rssi = RA_RSSI_STATE_SEND;
			}

			p_sta->rssi_stat.rssi_cck = undecorated_smoothed_cck;
			p_sta->rssi_stat.rssi_ofdm = undecorated_smoothed_ofdm;
			p_sta->rssi_stat.rssi = undecorated_smoothed_pwdb;



			if (send_rssi_2_fw) { /* Trigger init rate by RSSI */

				if (p_sta->rssi_stat.ofdm_pkt_cnt != 0)
					p_sta->rssi_stat.rssi = undecorated_smoothed_ofdm;

				PHYDM_DBG(p_dm, DBG_RSSI_MNTR,
					("[Send to FW] PWDB = (( %d )), ofdm_pkt = (( %d )), cck_pkt = (( %d ))\n",
					undecorated_smoothed_pwdb, p_sta->rssi_stat.ofdm_pkt_cnt, p_sta->rssi_stat.cck_pkt_cnt));

			}


			/*in WIN Driver: sta_ID==0->p_entry==NULL -> default port HAL_Data*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))

			if (p_pktinfo->station_id == 0) {
				/**/
				p_hal_data->UndecoratedSmoothedPWDB = undecorated_smoothed_pwdb;
			}
#endif

			/* dbg_print("ofdm_pkt=%d, weighting=%d\n", ofdm_pkt_cnt, weighting); */
			/* dbg_print("undecorated_smoothed_ofdm=%d, undecorated_smoothed_pwdb=%d, undecorated_smoothed_cck=%d\n", */
			/*	undecorated_smoothed_ofdm, undecorated_smoothed_pwdb, undecorated_smoothed_cck); */

		}

	}
}

/*
 * Endianness before calling this API
 *   */

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
/* For 8822B only!! need to move to FW finally */
/*==============================================*/

boolean
phydm_query_is_mu_api(
	struct PHY_DM_STRUCT					*p_phydm,
	u8							ppdu_idx,
	u8							*p_data_rate,
	u8							*p_gid
)
{
	u8	data_rate = 0, gid = 0;
	boolean is_mu = false;

	data_rate = p_phydm->phy_dbg_info.num_of_ppdu[ppdu_idx];
	gid = p_phydm->phy_dbg_info.gid_num[ppdu_idx];

	if (data_rate & BIT(7)) {
		is_mu = true;
		data_rate = data_rate & ~(BIT(7));
	} else
		is_mu = false;

	*p_data_rate = data_rate;
	*p_gid = gid;

	return is_mu;

}

void
phydm_reset_phy_info(
	struct PHY_DM_STRUCT					*p_phydm,
	struct phydm_phyinfo_struct			*p_phy_info
)
{
	p_phy_info->rx_pwdb_all = 0;
	p_phy_info->signal_quality = 0;
	p_phy_info->band_width = 0;
	p_phy_info->rx_count = 0;
	odm_memory_set(p_phydm, p_phy_info->rx_mimo_signal_quality, 0, 4);
	odm_memory_set(p_phydm, p_phy_info->rx_mimo_signal_strength, 0, 4);
	odm_memory_set(p_phydm, p_phy_info->rx_snr, 0, 4);

	p_phy_info->rx_power = -110;
	p_phy_info->recv_signal_power = -110;
	p_phy_info->bt_rx_rssi_percentage = 0;
	p_phy_info->signal_strength = 0;
	p_phy_info->channel = 0;
	p_phy_info->is_mu_packet = 0;
	p_phy_info->is_beamformed = 0;
	p_phy_info->rxsc = 0;
	odm_memory_set(p_phydm, p_phy_info->rx_pwr, -110, 4);
	/*odm_memory_set(p_phydm, p_phy_info->rx_mimo_evm_dbm, 0, 4);*/
	odm_memory_set(p_phydm, p_phy_info->cfo_short, 0, 8);
	odm_memory_set(p_phydm, p_phy_info->cfo_tail, 0, 8);

	odm_memory_set(p_phydm, p_phy_info->rx_mimo_evm_dbm, 0, 4);
}

void
phydm_set_per_path_phy_info(
	u8							rx_path,
	s8							rx_pwr,
	s8							rx_evm,
	s8							cfo_tail,
	s8							rx_snr,
	struct phydm_phyinfo_struct	*p_phy_info
)
{
	u8			evm_dbm = 0;
	u8			evm_percentage = 0;

	/* SNR is S(8,1), EVM is S(8,1), CFO is S(8,7) */

	if (rx_evm < 0) {
		/* Calculate EVM in dBm */
		evm_dbm = ((u8)(0 - rx_evm) >> 1);

		/* Calculate EVM in percentage */
		if (evm_dbm >= 34)
			evm_percentage = 100;
		else
			evm_percentage = (evm_dbm << 1) + (evm_dbm);
	}

	p_phy_info->rx_pwr[rx_path] = rx_pwr;

	/* CFO = CFO_tail * 312.5 / 2^7 ~= CFO tail * 39/512 (kHz)*/
	p_phy_info->cfo_tail[rx_path] = cfo_tail;
	p_phy_info->cfo_tail[rx_path] = ((p_phy_info->cfo_tail[rx_path] << 5) + (p_phy_info->cfo_tail[rx_path] << 2) +
		(p_phy_info->cfo_tail[rx_path] << 1) + (p_phy_info->cfo_tail[rx_path])) >> 9;

	if (evm_dbm == 64)
		evm_dbm = 0; /*if 1SS rate, evm_dbm [2nd stream] =64*/

	p_phy_info->rx_mimo_evm_dbm[rx_path] = evm_dbm;

	p_phy_info->rx_mimo_signal_strength[rx_path] = phydm_query_rx_pwr_percentage(rx_pwr);
	p_phy_info->rx_mimo_signal_quality[rx_path] = evm_percentage;
	p_phy_info->rx_snr[rx_path] = rx_snr >> 1;

#if 0
	/* if (p_pktinfo->is_packet_match_bssid) */
	{
		dbg_print("path (%d)--------\n", rx_path);
		dbg_print("rx_pwr = %d, Signal strength = %d\n", p_phy_info->rx_pwr[rx_path], p_phy_info->rx_mimo_signal_strength[rx_path]);
		dbg_print("evm_dbm = %d, Signal quality = %d\n", p_phy_info->rx_mimo_evm_dbm[rx_path], p_phy_info->rx_mimo_signal_quality[rx_path]);
		dbg_print("CFO = %d, SNR = %d\n", p_phy_info->cfo_tail[rx_path], p_phy_info->rx_snr[rx_path]);
	}
#endif
}

void
phydm_set_common_phy_info(
	s8							rx_power,
	u8							channel,
	boolean							is_beamformed,
	boolean							is_mu_packet,
	u8							bandwidth,
	u8							signal_quality,
	u8							rxsc,
	struct phydm_phyinfo_struct				*p_phy_info
)
{
	p_phy_info->rx_power = rx_power;											/* RSSI in dB */
	p_phy_info->recv_signal_power = rx_power;										/* RSSI in dB */
	p_phy_info->channel = channel;												/* channel number */
	p_phy_info->is_beamformed = is_beamformed;									/* apply BF */
	p_phy_info->is_mu_packet = is_mu_packet;										/* MU packet */
	p_phy_info->rxsc = rxsc;

	p_phy_info->rx_pwdb_all = phydm_query_rx_pwr_percentage(rx_power);				/* RSSI in percentage */
	p_phy_info->signal_quality = signal_quality;										/* signal quality */
	p_phy_info->band_width = bandwidth;											/* bandwidth */

#if 0
	/* if (p_pktinfo->is_packet_match_bssid) */
	{
		dbg_print("rx_pwdb_all = %d, rx_power = %d, recv_signal_power = %d\n", p_phy_info->rx_pwdb_all, p_phy_info->rx_power, p_phy_info->recv_signal_power);
		dbg_print("signal_quality = %d\n", p_phy_info->signal_quality);
		dbg_print("is_beamformed = %d, is_mu_packet = %d, rx_count = %d\n", p_phy_info->is_beamformed, p_phy_info->is_mu_packet, p_phy_info->rx_count + 1);
		dbg_print("channel = %d, rxsc = %d, band_width = %d\n", channel, rxsc, bandwidth);
	}
#endif
}

void
phydm_get_rx_phy_status_type0(
	struct PHY_DM_STRUCT		*p_dm,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct		*p_pktinfo,
	struct phydm_phyinfo_struct	*p_phy_info
)
{
	/* type 0 is used for cck packet */
	struct _phy_status_rpt_jaguar2_type0	*p_phy_sta_rpt = (struct _phy_status_rpt_jaguar2_type0 *)p_phy_status;
	u8							SQ = 0;
	s8							rx_power = p_phy_sta_rpt->pwdb - 110;


	if (p_dm->support_ic_type & ODM_RTL8723D) {
#if (RTL8723D_SUPPORT == 1)
		rx_power = p_phy_sta_rpt->pwdb - 97;
#endif
	}
/*#if (RTL8710B_SUPPORT == 1)*/
	/*if (p_dm->support_ic_type & ODM_RTL8710B)*/
		/*rx_power = p_phy_sta_rpt->pwdb - 97;*/
/*#endif*/

#if (RTL8821C_SUPPORT == 1)
	else if (p_dm->support_ic_type & ODM_RTL8821C) {
		if (p_phy_sta_rpt->pwdb >= -57)
			rx_power = p_phy_sta_rpt->pwdb - 100;
		else
			rx_power = p_phy_sta_rpt->pwdb - 102;
	}
#endif

	if (p_pktinfo->is_to_self) {
		p_dm->ofdm_agc_idx[0] = p_phy_sta_rpt->pwdb;
		p_dm->ofdm_agc_idx[1] = 0;
		p_dm->ofdm_agc_idx[2] = 0;
		p_dm->ofdm_agc_idx[3] = 0;
	}


	/* Calculate Signal Quality*/
	if (p_pktinfo->is_packet_match_bssid) {
		if (p_phy_sta_rpt->signal_quality >= 64)
			SQ = 0;
		else if (p_phy_sta_rpt->signal_quality <= 20)
			SQ = 100;
		else {
			/* mapping to 2~99% */
			SQ = 64 - p_phy_sta_rpt->signal_quality;
			SQ = ((SQ << 3) + SQ) >> 2;
		}
	}

	/* Modify CCK PWDB if old AGC */
	if (p_dm->cck_new_agc == false) {

		#if (RTL8197F_SUPPORT == 1)
		if (p_dm->support_ic_type & ODM_RTL8197F)
			rx_power = phydm_cck_rssi_convert(p_dm, p_phy_sta_rpt->lna_l, p_phy_sta_rpt->vga);
		else
		#endif
		{
			u8	lna_idx, vga_idx;

			lna_idx = ((p_phy_sta_rpt->lna_h << 3) | p_phy_sta_rpt->lna_l);
			vga_idx = p_phy_sta_rpt->vga;

			#if (RTL8723D_SUPPORT == 1)
			if (p_dm->support_ic_type & ODM_RTL8723D)
				rx_power = odm_cckrssi_8723d(lna_idx, vga_idx);
			#endif

			#if (RTL8710B_SUPPORT == 1)
			if (p_dm->support_ic_type & ODM_RTL8710B)
				rx_power = odm_cckrssi_8710b(lna_idx, vga_idx);
			#endif

			#if (RTL8822B_SUPPORT == 1)
			/* Need to do !! */
			/*if (p_dm->support_ic_type & ODM_RTL8822B) */
			/*rx_power = odm_CCKRSSI_8822B(LNA_idx, VGA_idx);*/
			#endif
		}

	}

	/* Confirm CCK RSSI */
	#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F) {
		u8	bb_pwr_th_l = 5; /* round( 31*0.15 ) */
		u8	bb_pwr_th_h = 27; /* round( 31*0.85 ) */

		if ((p_phy_sta_rpt->bb_power < bb_pwr_th_l) || (p_phy_sta_rpt->bb_power > bb_pwr_th_h))
			rx_power = 0; /* Error RSSI for CCK ; set 100*/
	}
	#endif

	/* Update CCK packet counter */
	p_dm->phy_dbg_info.num_qry_phy_status_cck++;

	/*CCK no STBC and LDPC*/
	p_dm->phy_dbg_info.is_ldpc_pkt = false;
	p_dm->phy_dbg_info.is_stbc_pkt = false;

	/* Update Common information */
	phydm_set_common_phy_info(rx_power, p_phy_sta_rpt->channel, false,
		  false, CHANNEL_WIDTH_20, SQ, p_phy_sta_rpt->rxsc, p_phy_info);

	/* Update CCK pwdb */
	phydm_set_per_path_phy_info(RF_PATH_A, rx_power, 0, 0, 0, p_phy_info);					/* Update per-path information */

	p_dm->dm_fat_table.antsel_rx_keep_0 = p_phy_sta_rpt->antidx_a;
	p_dm->dm_fat_table.antsel_rx_keep_1 = p_phy_sta_rpt->antidx_b;
	p_dm->dm_fat_table.antsel_rx_keep_2 = p_phy_sta_rpt->antidx_c;
	p_dm->dm_fat_table.antsel_rx_keep_3 = p_phy_sta_rpt->antidx_d;
#if 0
	/* if (p_pktinfo->is_packet_match_bssid) */
	{
		dbg_print("pwdb = 0x%x, MP gain index = 0x%x, TRSW = 0x%x\n", p_phy_sta_rpt->pwdb, p_phy_sta_rpt->gain, p_phy_sta_rpt->trsw);
		dbg_print("channel = %d, band = %d, rxsc = %d\n", p_phy_sta_rpt->channel, p_phy_sta_rpt->band, p_phy_sta_rpt->rxsc);
		dbg_print("agc_table = 0x%x, agc_rpt 0x%x, bb_power = 0x%x\n", p_phy_sta_rpt->agc_table, p_phy_sta_rpt->agc_rpt, p_phy_sta_rpt->bb_power);
		dbg_print("length = %d, SQ = %d\n", p_phy_sta_rpt->length, p_phy_sta_rpt->signal_quality);
		dbg_print("antidx a = 0x%x, b = 0x%x, c = 0x%x, d = 0x%x\n", p_phy_sta_rpt->antidx_a, p_phy_sta_rpt->antidx_b, p_phy_sta_rpt->antidx_c, p_phy_sta_rpt->antidx_d);
		dbg_print("rsvd_0 = 0x%x, rsvd_1 = 0x%x, rsvd_2 = 0x%x\n", p_phy_sta_rpt->rsvd_0, p_phy_sta_rpt->rsvd_1, p_phy_sta_rpt->rsvd_2);
		dbg_print("rsvd_3 = 0x%x, rsvd_4 = 0x%x, rsvd_5 = 0x%x\n", p_phy_sta_rpt->rsvd_3, p_phy_sta_rpt->rsvd_4, p_phy_sta_rpt->rsvd_5);
		dbg_print("rsvd_6 = 0x%x, rsvd_7 = 0x%x, rsvd_8 = 0x%x\n", p_phy_sta_rpt->rsvd_6, p_phy_sta_rpt->rsvd_7, p_phy_sta_rpt->rsvd_8);
	}
#endif
}

void
phydm_get_rx_phy_status_type1(
	struct PHY_DM_STRUCT		*p_dm,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct		*p_pktinfo,
	struct phydm_phyinfo_struct	*p_phy_info
)
{
	/* type 1 is used for ofdm packet */

	struct _phy_status_rpt_jaguar2_type1	*p_phy_sta_rpt = (struct _phy_status_rpt_jaguar2_type1 *)p_phy_status;
	s8							rx_pwr_db = -120;
	u8							i, rxsc, bw = CHANNEL_WIDTH_20, rx_count = 0;
	boolean						is_mu;
	u8							num_ss;

	/* Update OFDM packet counter */
	p_dm->phy_dbg_info.num_qry_phy_status_ofdm++;

	/* Update per-path information */
	for (i = RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (p_dm->rx_ant_status & BIT(i)) {
			s8	rx_path_pwr_db;

			/* RX path counter */
			rx_count++;

			/* Update per-path information (RSSI_dB RSSI_percentage EVM SNR CFO SQ) */
			/* EVM report is reported by stream, not path */
			rx_path_pwr_db = p_phy_sta_rpt->pwdb[i] - 110;					/* per-path pwdb in dB domain */

			if (p_pktinfo->is_to_self)
				p_dm->ofdm_agc_idx[i] = p_phy_sta_rpt->pwdb[i];

			phydm_set_per_path_phy_info(i, rx_path_pwr_db, p_phy_sta_rpt->rxevm[rx_count - 1],
				p_phy_sta_rpt->cfo_tail[i], p_phy_sta_rpt->rxsnr[i], p_phy_info);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		p_phy_info->rx_count = rx_count - 1;

	/* Check if MU packet or not */
	if ((p_phy_sta_rpt->gid != 0) && (p_phy_sta_rpt->gid != 63)) {
		is_mu = true;
		p_dm->phy_dbg_info.num_qry_mu_pkt++;
	} else
		is_mu = false;

	/* count BF packet */
	p_dm->phy_dbg_info.num_qry_bf_pkt = p_dm->phy_dbg_info.num_qry_bf_pkt + p_phy_sta_rpt->beamformed;

	/*STBC or LDPC pkt*/
	p_dm->phy_dbg_info.is_ldpc_pkt = p_phy_sta_rpt->ldpc;
	p_dm->phy_dbg_info.is_stbc_pkt = p_phy_sta_rpt->stbc;

	/* Check sub-channel */
	if ((p_pktinfo->data_rate > ODM_RATE11M) && (p_pktinfo->data_rate < ODM_RATEMCS0))
		rxsc = p_phy_sta_rpt->l_rxsc;
	else
		rxsc = p_phy_sta_rpt->ht_rxsc;

	/* Check RX bandwidth */
	if (p_dm->support_ic_type & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = CHANNEL_WIDTH_40;
		else if (rxsc >= 13)
			bw = CHANNEL_WIDTH_80;
		else
			bw = p_phy_sta_rpt->rf_mode;
	} else if (p_dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8723D | ODM_RTL8710B)) {
		if (p_phy_sta_rpt->rf_mode == 0)
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = CHANNEL_WIDTH_20;
		else
			bw = CHANNEL_WIDTH_40;
	}

	/* Update packet information */
	phydm_set_common_phy_info(rx_pwr_db, p_phy_sta_rpt->channel, (boolean)p_phy_sta_rpt->beamformed,
		is_mu, bw, p_phy_info->rx_mimo_signal_quality[0], rxsc, p_phy_info);

	num_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);

	odm_parsing_cfo(p_dm, p_pktinfo, p_phy_sta_rpt->cfo_tail, num_ss);
	p_dm->dm_fat_table.antsel_rx_keep_0 = p_phy_sta_rpt->antidx_a;
	p_dm->dm_fat_table.antsel_rx_keep_1 = p_phy_sta_rpt->antidx_b;
	p_dm->dm_fat_table.antsel_rx_keep_2 = p_phy_sta_rpt->antidx_c;
	p_dm->dm_fat_table.antsel_rx_keep_3 = p_phy_sta_rpt->antidx_d;

	#if 0
	if (p_pktinfo->is_packet_match_bssid) {

		dbg_print("channel = %d, band = %d, l_rxsc = %d, ht_rxsc = %d, rf_mode = %d\n", p_phy_sta_rpt->channel, p_phy_sta_rpt->band, p_phy_sta_rpt->l_rxsc, p_phy_sta_rpt->ht_rxsc, p_phy_sta_rpt->rf_mode);
		dbg_print("Antidx A = %d, B = %d, C = %d, D = %d\n", p_phy_sta_rpt->antidx_a, p_phy_sta_rpt->antidx_b, p_phy_sta_rpt->antidx_c, p_phy_sta_rpt->antidx_d);
		dbg_print("pwdb A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->pwdb[0], p_phy_sta_rpt->pwdb[1], p_phy_sta_rpt->pwdb[2], p_phy_sta_rpt->pwdb[3]);
		dbg_print("EVM  A: %d, B: %d, C: %d, D: %d\n", p_phy_sta_rpt->rxevm[0], p_phy_sta_rpt->rxevm[1], p_phy_sta_rpt->rxevm[2], p_phy_sta_rpt->rxevm[3]);
		dbg_print("SNR  A: %d, B: %d, C: %d, D: %d\n", p_phy_sta_rpt->rxsnr[0], p_phy_sta_rpt->rxsnr[1], p_phy_sta_rpt->rxsnr[2], p_phy_sta_rpt->rxsnr[3]);
		dbg_print("CFO  A: %d, B: %d, C: %d, D: %d\n", p_phy_sta_rpt->cfo_tail[0], p_phy_sta_rpt->cfo_tail[1], p_phy_sta_rpt->cfo_tail[2], p_phy_sta_rpt->cfo_tail[3]);
		dbg_print("paid = %d, gid = %d, length = %d\n", (p_phy_sta_rpt->paid + (p_phy_sta_rpt->paid_msb<<8)), p_phy_sta_rpt->gid, p_phy_sta_rpt->lsig_length);
		dbg_print("ldpc: %d, stbc: %d, bf: %d, gnt_bt: %d, antsw: %d\n", p_phy_sta_rpt->ldpc, p_phy_sta_rpt->stbc, p_phy_sta_rpt->beamformed, p_phy_sta_rpt->gnt_bt, p_phy_sta_rpt->hw_antsw_occu);
		dbg_print("NBI: %d, pos: %d\n", p_phy_sta_rpt->nb_intf_flag, (p_phy_sta_rpt->intf_pos + (p_phy_sta_rpt->intf_pos_msb<<8)));
		dbg_print("rsvd_0 = %d, rsvd_1 = %d, rsvd_2 = %d, rsvd_3 = %d, rsvd_4 = %d, rsvd_5 = %d\n", p_phy_sta_rpt->rsvd_0, p_phy_sta_rpt->rsvd_1, p_phy_sta_rpt->rsvd_2, p_phy_sta_rpt->rsvd_3, p_phy_sta_rpt->rsvd_4, p_phy_sta_rpt->rsvd_5);

	}

	dbg_print("phydm_get_rx_phy_status_type1   p_pktinfo->is_packet_match_bssid = %d\n", p_pktinfo->is_packet_match_bssid);
	dbg_print("p_pktinfo->data_rate = 0x%x\n", p_pktinfo->data_rate);
	#endif
}

void
phydm_get_rx_phy_status_type2(
	struct PHY_DM_STRUCT						*p_dm,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct				*p_pktinfo,
	struct phydm_phyinfo_struct				*p_phy_info
)
{
	struct _phy_status_rpt_jaguar2_type2	*p_phy_sta_rpt = (struct _phy_status_rpt_jaguar2_type2 *)p_phy_status;
	s8							rx_pwr_db = -120;
	u8							i, rxsc, bw = CHANNEL_WIDTH_20, rx_count = 0;

	/* Update OFDM packet counter */
	p_dm->phy_dbg_info.num_qry_phy_status_ofdm++;

	/* Update per-path information */
	for (i = RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
		if (p_dm->rx_ant_status & BIT(i)) {
			s8	rx_path_pwr_db;

			/* RX path counter */
			rx_count++;

			/* Update per-path information (RSSI_dB RSSI_percentage EVM SNR CFO SQ) */
#if (RTL8197F_SUPPORT == 1)
			if ((p_dm->support_ic_type & ODM_RTL8197F) && (p_phy_sta_rpt->pwdb[i] == 0x7f)) { /*for 97f workaround*/

				if (i == RF_PATH_A) {
					rx_path_pwr_db = (p_phy_sta_rpt->gain_a) << 1;
					rx_path_pwr_db = rx_path_pwr_db - 110;
				} else if (i == RF_PATH_B) {
					rx_path_pwr_db = (p_phy_sta_rpt->gain_b) << 1;
					rx_path_pwr_db = rx_path_pwr_db - 110;
				} else
					rx_path_pwr_db = 0;
			} else
#endif
				rx_path_pwr_db = p_phy_sta_rpt->pwdb[i] - 110;					/* per-path pwdb in dB domain */

			phydm_set_per_path_phy_info(i, rx_path_pwr_db, 0, 0, 0, p_phy_info);

			/* search maximum pwdb */
			if (rx_path_pwr_db > rx_pwr_db)
				rx_pwr_db = rx_path_pwr_db;
		}
	}

	/* mapping RX counter from 1~4 to 0~3 */
	if (rx_count > 0)
		p_phy_info->rx_count = rx_count - 1;

	/* Check RX sub-channel */
	if ((p_pktinfo->data_rate > ODM_RATE11M) && (p_pktinfo->data_rate < ODM_RATEMCS0))
		rxsc = p_phy_sta_rpt->l_rxsc;
	else
		rxsc = p_phy_sta_rpt->ht_rxsc;

	/*STBC or LDPC pkt*/
	p_dm->phy_dbg_info.is_ldpc_pkt = p_phy_sta_rpt->ldpc;
	p_dm->phy_dbg_info.is_stbc_pkt = p_phy_sta_rpt->stbc;

	/* Check RX bandwidth */
	/* the BW information of sc=0 is useless, because there is no information of RF mode*/

	if (p_dm->support_ic_type & ODM_RTL8822B) {
		if ((rxsc >= 1) && (rxsc <= 8))
			bw = CHANNEL_WIDTH_20;
		else if ((rxsc >= 9) && (rxsc <= 12))
			bw = CHANNEL_WIDTH_40;
		else if (rxsc >= 13)
			bw = CHANNEL_WIDTH_80;
		else
			bw = CHANNEL_WIDTH_20;
	} else if (p_dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8723D | ODM_RTL8710B)) {/* JJ ADD 20161014 */
		if (rxsc == 3)
			bw = CHANNEL_WIDTH_40;
		else if ((rxsc == 1) || (rxsc == 2))
			bw = CHANNEL_WIDTH_20;
		else
			bw = CHANNEL_WIDTH_20;
	}

	/* Update packet information */
	phydm_set_common_phy_info(rx_pwr_db, p_phy_sta_rpt->channel, (boolean)p_phy_sta_rpt->beamformed,
				  false, bw, 0, rxsc, p_phy_info);

#if 0
	/* if (p_pktinfo->is_packet_match_bssid) */
	{
		dbg_print("channel = %d, band = %d, l_rxsc = %d, ht_rxsc = %d\n", p_phy_sta_rpt->channel, p_phy_sta_rpt->band, p_phy_sta_rpt->l_rxsc, p_phy_sta_rpt->ht_rxsc);
		dbg_print("pwdb A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->pwdb[0], p_phy_sta_rpt->pwdb[1], p_phy_sta_rpt->pwdb[2], p_phy_sta_rpt->pwdb[3]);
		dbg_print("Agc table A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->agc_table_a, p_phy_sta_rpt->agc_table_b, p_phy_sta_rpt->agc_table_c, p_phy_sta_rpt->agc_table_d);
		dbg_print("Gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->gain_a, p_phy_sta_rpt->gain_b, p_phy_sta_rpt->gain_c, p_phy_sta_rpt->gain_d);
		dbg_print("TRSW A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->trsw_a, p_phy_sta_rpt->trsw_b, p_phy_sta_rpt->trsw_c, p_phy_sta_rpt->trsw_d);
		dbg_print("AAGC step A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->aagc_step_a, p_phy_sta_rpt->aagc_step_b, p_phy_sta_rpt->aagc_step_c, p_phy_sta_rpt->aagc_step_d);
		dbg_print("HT AAGC gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->ht_aagc_gain[0], p_phy_sta_rpt->ht_aagc_gain[1], p_phy_sta_rpt->ht_aagc_gain[2], p_phy_sta_rpt->ht_aagc_gain[3]);
		dbg_print("DAGC gain A: 0x%x, B: 0x%x, C: 0x%x, D: 0x%x\n", p_phy_sta_rpt->dagc_gain[0], p_phy_sta_rpt->dagc_gain[1], p_phy_sta_rpt->dagc_gain[2], p_phy_sta_rpt->dagc_gain[3]);
		dbg_print("ldpc: %d, stbc: %d, bf: %d, gnt_bt: %d, antsw: %d\n", p_phy_sta_rpt->ldpc, p_phy_sta_rpt->stbc, p_phy_sta_rpt->beamformed, p_phy_sta_rpt->gnt_bt, p_phy_sta_rpt->hw_antsw_occu);
		dbg_print("counter: %d, syn_count: %d\n", p_phy_sta_rpt->counter, p_phy_sta_rpt->syn_count);
		dbg_print("cnt_cca2agc_rdy: %d, cnt_pw2cca: %d, shift_l_map\n", p_phy_sta_rpt->cnt_cca2agc_rdy, p_phy_sta_rpt->cnt_pw2cca, p_phy_sta_rpt->shift_l_map);
		dbg_print("rsvd_0 = %d, rsvd_1 = %d, rsvd_2 = %d, rsvd_3 = %d, rsvd_4 = %d, rsvd_5 = %d\n", p_phy_sta_rpt->rsvd_0, p_phy_sta_rpt->rsvd_1, p_phy_sta_rpt->rsvd_2, p_phy_sta_rpt->rsvd_3, p_phy_sta_rpt->rsvd_4);
		dbg_print("rsvd_5 = %d, rsvd_6 = %d, rsvd_6 = %d\n", p_phy_sta_rpt->rsvd_5, p_phy_sta_rpt->rsvd_6, p_phy_sta_rpt->rsvd_7);
	}
#endif
}

void
phydm_get_rx_phy_status_type5(
	u8				*p_phy_status
)
{
	/*
		dbg_print("DW0: 0x%02x%02x%02x%02x\n", *(p_phy_status + 3), *(p_phy_status + 2), *(p_phy_status + 1), *(p_phy_status + 0));
		dbg_print("DW1: 0x%02x%02x%02x%02x\n", *(p_phy_status + 7), *(p_phy_status + 6), *(p_phy_status + 5), *(p_phy_status + 4));
		dbg_print("DW2: 0x%02x%02x%02x%02x\n", *(p_phy_status + 11), *(p_phy_status + 10), *(p_phy_status + 9), *(p_phy_status + 8));
		dbg_print("DW3: 0x%02x%02x%02x%02x\n", *(p_phy_status + 15), *(p_phy_status + 14), *(p_phy_status + 13), *(p_phy_status + 12));
		dbg_print("DW4: 0x%02x%02x%02x%02x\n", *(p_phy_status + 19), *(p_phy_status + 18), *(p_phy_status + 17), *(p_phy_status + 16));
		dbg_print("DW5: 0x%02x%02x%02x%02x\n", *(p_phy_status + 23), *(p_phy_status + 22), *(p_phy_status + 21), *(p_phy_status + 20));
		dbg_print("DW6: 0x%02x%02x%02x%02x\n", *(p_phy_status + 27), *(p_phy_status + 26), *(p_phy_status + 25), *(p_phy_status + 24));
	*/
}

void
phydm_process_rssi_for_dm_new_type(
	struct PHY_DM_STRUCT			*p_dm,
	struct phydm_phyinfo_struct		*p_phy_info,
	struct phydm_perpkt_info_struct			*p_pktinfo
)
{
	s32				undecorated_smoothed_pwdb, accumulate_pwdb;
	u32				rssi_ave, rssi_linear = 0;
	u8				i;
	struct cmn_sta_info	*p_sta;
	u8				scaling_factor = 4;
	u8				is_cck_rate = (p_pktinfo->data_rate <= ODM_RATE11M) ? true : false;

	if (p_pktinfo->station_id >= ODM_ASSOCIATE_ENTRY_NUM)
		return;

	p_sta = p_dm->p_phydm_sta_info[p_pktinfo->station_id];

	if (!is_sta_active(p_sta))
		return;

	if ((!p_pktinfo->is_packet_match_bssid))/*data frame only*/
		return;

	if (p_pktinfo->is_packet_beacon)
		p_dm->phy_dbg_info.num_qry_beacon_pkt++;

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	if (p_dm->support_ability & ODM_BB_ANT_DIV)
		odm_process_rssi_for_ant_div(p_dm, p_phy_info, p_pktinfo);
#endif

#ifdef CONFIG_DYNAMIC_RX_PATH
	phydm_process_phy_status_for_dynamic_rx_path(p_dm, p_phy_info, p_pktinfo);
#endif

	if (p_pktinfo->is_packet_to_self || p_pktinfo->is_packet_beacon) {

		p_dm->rx_rate = p_pktinfo->data_rate;
		undecorated_smoothed_pwdb = p_sta->rssi_stat.rssi;
		accumulate_pwdb = p_dm->accumulate_pwdb[p_pktinfo->station_id];
		p_dm->RSSI_A = p_phy_info->rx_mimo_signal_strength[RF_PATH_A];
		p_dm->RSSI_B = p_phy_info->rx_mimo_signal_strength[RF_PATH_B];
		p_dm->RSSI_C = p_phy_info->rx_mimo_signal_strength[RF_PATH_C];
		p_dm->RSSI_D = p_phy_info->rx_mimo_signal_strength[RF_PATH_D];

		for (i = RF_PATH_A; i < ODM_RF_PATH_MAX_JAGUAR; i++) {
			if (p_phy_info->rx_mimo_signal_strength[i] != 0)
				rssi_linear += odm_convert_to_linear(p_phy_info->rx_mimo_signal_strength[i]);
		}

		switch (p_phy_info->rx_count + 1) {
		case 2:
			rssi_linear = (rssi_linear >> 1);
			break;
		case 3:
			rssi_linear = ((rssi_linear) + (rssi_linear << 1) + (rssi_linear << 3)) >> 5;	/* RSSI_linear/3 ~ RSSI_linear*11/32 */
			break;
		case 4:
			rssi_linear = (rssi_linear >> 2);
			break;
		}
		rssi_ave = odm_convert_to_db(rssi_linear);

		if (undecorated_smoothed_pwdb <= 0) {
			accumulate_pwdb = (p_phy_info->rx_pwdb_all << scaling_factor);
			undecorated_smoothed_pwdb = p_phy_info->rx_pwdb_all;
		} else {
			accumulate_pwdb = accumulate_pwdb - (accumulate_pwdb >> scaling_factor) + rssi_ave;
			undecorated_smoothed_pwdb = (accumulate_pwdb + (1 << (scaling_factor - 1))) >> scaling_factor;
		}

		p_sta->rssi_stat.rssi = (s8)undecorated_smoothed_pwdb;

		if (is_cck_rate)
			p_sta->rssi_stat.rssi_cck = (s8)rssi_ave;
		else
			p_sta->rssi_stat.rssi_ofdm = (s8)rssi_ave;


		p_dm->accumulate_pwdb[p_pktinfo->station_id] = accumulate_pwdb;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		if (p_pktinfo->station_id == 0) {
			HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_dm->adapter);

			p_hal_data->UndecoratedSmoothedPWDB = undecorated_smoothed_pwdb;
		}
#endif
	}
}

void
phydm_rx_phy_status_new_type(
	void							*p_dm_void,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct		*p_pktinfo,
	struct phydm_phyinfo_struct	*p_phy_info
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#ifdef PHYDM_PHYSTAUS_SMP_MODE
	struct pkt_process_info			*p_pkt_process = &(p_dm->pkt_proc_struct);
#endif
	u8		phy_status_type = (*p_phy_status & 0xf);

#ifdef PHYDM_PHYSTAUS_SMP_MODE
	if (p_pkt_process->phystatus_smp_mode_en && phy_status_type != 0) {

		if (p_pkt_process->pre_ppdu_cnt == p_pktinfo->ppdu_cnt)
			return;

		p_pkt_process->pre_ppdu_cnt = p_pktinfo->ppdu_cnt;
	}
#endif

	/*dbg_print("phydm_rx_phy_status_new_type================> (page: %d)\n", phy_status_type);*/

	/* Memory reset */
	phydm_reset_phy_info(p_dm, p_phy_info);
	p_dm->rate_ss = phydm_rate_to_num_ss(p_dm, p_pktinfo->data_rate);

	/* Phy status parsing */
	switch (phy_status_type) {
	case 0:	/*CCK*/
		phydm_get_rx_phy_status_type0(p_dm, p_phy_status, p_pktinfo, p_phy_info);
		break;
	case 1:
		phydm_get_rx_phy_status_type1(p_dm, p_phy_status, p_pktinfo, p_phy_info);
		break;
	case 2:
		phydm_get_rx_phy_status_type2(p_dm, p_phy_status, p_pktinfo, p_phy_info);
		break;
#if 0
	case 5:
		phydm_get_rx_phy_status_type5(p_phy_status);
		return;
#endif
	default:
		return;
	}

	if (p_pktinfo->is_packet_match_bssid) {
		phydm_avg_phystatus_index(p_dm, p_phy_info, p_pktinfo);
		phydm_rx_statistic_cal(p_dm, p_phy_status, p_pktinfo);
	}

	/* Update signal strength to UI, and p_phy_info->rx_pwdb_all is the maximum RSSI of all path */
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	p_phy_info->signal_strength = SignalScaleProc(p_dm->adapter, p_phy_info->rx_pwdb_all, false, false);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	p_phy_info->signal_strength = (u8)(phydm_signal_scale_mapping(p_dm, p_phy_info->rx_pwdb_all));
#endif



}
/*==============================================*/
#endif

void
odm_phy_status_query(
	struct PHY_DM_STRUCT		*p_dm,
	struct phydm_phyinfo_struct	*p_phy_info,
	u8							*p_phy_status,
	struct phydm_perpkt_info_struct		*p_pktinfo
)
{

	if (p_dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE) {
		#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		phydm_rx_phy_status_new_type(p_dm, p_phy_status, p_pktinfo, p_phy_info);
		phydm_process_rssi_for_dm_new_type(p_dm, p_phy_info, p_pktinfo);
		#endif
	} else if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		#if	ODM_IC_11AC_SERIES_SUPPORT
		phydm_rx_phy_status_jaguar_series_parsing(p_dm, p_phy_info, p_phy_status, p_pktinfo);
		phydm_process_rssi_for_dm(p_dm, p_phy_info, p_pktinfo);
		#endif
	} else if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		#if	ODM_IC_11N_SERIES_SUPPORT
		phydm_rx_phy_status92c_series_parsing(p_dm, p_phy_info, p_phy_status, p_pktinfo);
		phydm_process_rssi_for_dm(p_dm, p_phy_info, p_pktinfo);
		#endif
	}
}

void
phydm_rx_phy_status_init(
	void			*p_dm_void
)
{
#ifdef PHYDM_PHYSTAUS_SMP_MODE
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct pkt_process_info			*p_pkt_process = &(p_dm->pkt_proc_struct);

	if (p_dm->support_ic_type == ODM_RTL8822B) {

		p_pkt_process->phystatus_smp_mode_en = 1;
		p_pkt_process->pre_ppdu_cnt = 0xff;

		odm_set_mac_reg(p_dm, 0x60f, BIT(7), 1); /*phystatus sampling mode enable*/

		odm_set_bb_reg(p_dm, 0x9e4, 0x3ff, 0x0); /*First update timming*/
		odm_set_bb_reg(p_dm, 0x9e4, 0xfc00, 0x0); /*Update Sampling time*/
	}
#endif
}
