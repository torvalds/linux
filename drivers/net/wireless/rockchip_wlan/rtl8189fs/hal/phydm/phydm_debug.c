/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

/*@************************************************************
 * include files
 ************************************************************/

#include "mp_precomp.h"
#include "phydm_precomp.h"

void phydm_init_debug_setting(struct dm_struct *dm)
{
	dm->fw_debug_components = 0;
	dm->debug_components =

#if DBG
	/*@BB Functions*/
	/*@DBG_DIG					|*/
	/*@DBG_RA_MASK					|*/
	/*@DBG_DYN_TXPWR				|*/
	/*@DBG_FA_CNT					|*/
	/*@DBG_RSSI_MNTR				|*/
	/*@DBG_CCKPD					|*/
	/*@DBG_ANT_DIV					|*/
	/*@DBG_SMT_ANT					|*/
	/*@DBG_PWR_TRAIN				|*/
	/*@DBG_RA					|*/
	/*@DBG_PATH_DIV					|*/
	/*@DBG_DFS					|*/
	/*@DBG_DYN_ARFR					|*/
	/*@DBG_ADPTVTY					|*/
	/*@DBG_CFO_TRK					|*/
	/*@DBG_ENV_MNTR					|*/
	/*@DBG_PRI_CCA					|*/
	/*@DBG_ADPTV_SOML				|*/
	/*@DBG_LNA_SAT_CHK				|*/
	/*@DBG_PHY_STATUS				|*/
	/*@DBG_TMP					|*/
	/*@DBG_FW_TRACE					|*/
	/*@DBG_TXBF					|*/
	/*@DBG_COMMON_FLOW				|*/
	/*@ODM_PHY_CONFIG				|*/
	/*@ODM_COMP_INIT				|*/
	/*@DBG_CMN					|*/
	/*@ODM_COMP_API					|*/
#endif
	0;

	dm->fw_buff_is_enpty = true;
	dm->pre_c2h_seq = 0;
	dm->c2h_cmd_start = 0;
	dm->cmn_dbg_msg_cnt = PHYDM_WATCH_DOG_PERIOD;
	dm->cmn_dbg_msg_period = PHYDM_WATCH_DOG_PERIOD;
	phydm_reset_rx_rate_distribution(dm);
}

void phydm_bb_dbg_port_header_sel(void *dm_void, u32 header_idx)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x8f8, 0x3c00000, header_idx);

		/*@
		 * header_idx:
		 *	(0:) '{ofdm_dbg[31:0]}'
		 *	(1:) '{cca,crc32_fail,dbg_ofdm[29:0]}'
		 *	(2:) '{vbon,crc32_fail,dbg_ofdm[29:0]}'
		 *	(3:) '{cca,crc32_ok,dbg_ofdm[29:0]}'
		 *	(4:) '{vbon,crc32_ok,dbg_ofdm[29:0]}'
		 *	(5:) '{dbg_iqk_anta}'
		 *	(6:) '{cca,ofdm_crc_ok,dbg_dp_anta[29:0]}'
		 *	(7:) '{dbg_iqk_antb}'
		 *	(8:) '{DBGOUT_RFC_b[31:0]}'
		 *	(9:) '{DBGOUT_RFC_a[31:0]}'
		 *	(a:) '{dbg_ofdm}'
		 *	(b:) '{dbg_cck}'
		 */
	}
}

void phydm_bb_dbg_port_clock_en(void *dm_void, u8 enable)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 reg_value = 0;

	if (dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8814A | ODM_RTL8814B |
	    ODM_RTL8195B)) {
		/*@enable/disable debug port clock, for power saving*/
		reg_value = enable ? 0x7 : 0;
		odm_set_bb_reg(dm, R_0x198c, 0x7, reg_value);
	}
}

u32 phydm_get_bb_dbg_port_idx(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		phydm_bb_dbg_port_clock_en(dm, true);
		val = odm_get_bb_reg(dm, R_0x8fc, MASKDWORD);
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		val = odm_get_bb_reg(dm, R_0x1c3c, 0xfff00);
	} else { /*@if (dm->support_ic_type & ODM_IC_11N_SERIES)*/
		val = odm_get_bb_reg(dm, R_0x908, MASKDWORD);
	}
	return val;
}

u8 phydm_set_bb_dbg_port(void *dm_void, u8 curr_dbg_priority, u32 debug_port)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 dbg_port_result = false;

	if (curr_dbg_priority > dm->pre_dbg_priority) {
		if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			phydm_bb_dbg_port_clock_en(dm, true);

			odm_set_bb_reg(dm, R_0x8fc, MASKDWORD, debug_port);
		} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			odm_set_bb_reg(dm, R_0x1c3c, 0xfff00, debug_port);

		} else { /*@if (dm->support_ic_type & ODM_IC_11N_SERIES)*/
			odm_set_bb_reg(dm, R_0x908, MASKDWORD, debug_port);
		}
		PHYDM_DBG(dm, ODM_COMP_API,
			  "DbgPort ((0x%x)) set success, Cur_priority=((%d)), Pre_priority=((%d))\n",
			  debug_port, curr_dbg_priority, dm->pre_dbg_priority);
		dm->pre_dbg_priority = curr_dbg_priority;
		dbg_port_result = true;
	}

	return dbg_port_result;
}

void phydm_release_bb_dbg_port(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	phydm_bb_dbg_port_clock_en(dm, false);
	phydm_bb_dbg_port_header_sel(dm, 0);

	dm->pre_dbg_priority = DBGPORT_RELEASE;
	PHYDM_DBG(dm, ODM_COMP_API, "Release BB dbg_port\n");
}

u32 phydm_get_bb_dbg_port_val(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 dbg_port_value = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES)
		dbg_port_value = odm_get_bb_reg(dm, R_0xfa0, MASKDWORD);
	else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		dbg_port_value = odm_get_bb_reg(dm, R_0x2dbc, MASKDWORD);
	else /*@if (dm->support_ic_type & ODM_IC_11N_SERIES)*/
		dbg_port_value = odm_get_bb_reg(dm, R_0xdf4, MASKDWORD);

	PHYDM_DBG(dm, ODM_COMP_API, "dbg_port_value = 0x%x\n", dbg_port_value);
	return dbg_port_value;
}

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
#if (ODM_IC_11N_SERIES_SUPPORT)
void phydm_bb_hw_dbg_info_n(void *dm_void, u32 *_used, char *output,
			    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 value32 = 0, value32_1 = 0;
	u8 rf_gain_a = 0, rf_gain_b = 0, rf_gain_c = 0, rf_gain_d = 0;
	u8 rx_snr_a = 0, rx_snr_b = 0, rx_snr_c = 0, rx_snr_d = 0;
	s8 rxevm_0 = 0, rxevm_1 = 0;
	#if 1
	struct phydm_cfo_rpt cfo;
	u8 i = 0;
	#else
	s32 short_cfo_a = 0, short_cfo_b = 0, long_cfo_a = 0, long_cfo_b = 0;
	s32 scfo_a = 0, scfo_b = 0, avg_cfo_a = 0, avg_cfo_b = 0;
	s32 cfo_end_a = 0, cfo_end_b = 0, acq_cfo_a = 0, acq_cfo_b = 0;
	#endif

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s\n",
		 "BB Report Info");

	/*@AGC result*/
	value32 = odm_get_bb_reg(dm, R_0xdd0, MASKDWORD);
	rf_gain_a = (u8)(value32 & 0x3f);
	rf_gain_a = rf_gain_a << 1;

	rf_gain_b = (u8)((value32 >> 8) & 0x3f);
	rf_gain_b = rf_gain_b << 1;

	rf_gain_c = (u8)((value32 >> 16) & 0x3f);
	rf_gain_c = rf_gain_c << 1;

	rf_gain_d = (u8)((value32 >> 24) & 0x3f);
	rf_gain_d = rf_gain_d << 1;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)",
		 rf_gain_a, rf_gain_b, rf_gain_c, rf_gain_d);

	/*SNR report*/
	value32 = odm_get_bb_reg(dm, R_0xdd4, MASKDWORD);
	rx_snr_a = (u8)(value32 & 0xff);
	rx_snr_a = rx_snr_a >> 1;

	rx_snr_b = (u8)((value32 >> 8) & 0xff);
	rx_snr_b = rx_snr_b >> 1;

	rx_snr_c = (u8)((value32 >> 16) & 0xff);
	rx_snr_c = rx_snr_c >> 1;

	rx_snr_d = (u8)((value32 >> 24) & 0xff);
	rx_snr_d = rx_snr_d >> 1;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)",
		 rx_snr_a, rx_snr_b, rx_snr_c, rx_snr_d);

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(dm, R_0xdd8, MASKDWORD);

	rxevm_0 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_0 /= 2;
	if (rxevm_0 < -63)
		rxevm_0 = 0;

	rxevm_1 = (s8)((value32 & MASKBYTE3) >> 24);
	rxevm_1 /= 2;
	if (rxevm_1 < -63)
		rxevm_1 = 0;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "RXEVM (1ss/2ss)", rxevm_0, rxevm_1);

#if 1
	phydm_get_cfo_info(dm, &cfo);
	for (i = 0; i < dm->num_rf_path; i++) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %s[%d] %-28s = {%d, %d, %d, %d, %d}",
			 "CFO", i, "{S, L, Sec, Acq, End}",
			 cfo.cfo_rpt_s[i], cfo.cfo_rpt_l[i], cfo.cfo_rpt_sec[i],
			 cfo.cfo_rpt_acq[i], cfo.cfo_rpt_end[i]);
	}
#else
	/*@CFO Report Info*/
	odm_set_bb_reg(dm, R_0xd00, BIT(26), 1);

	/*Short CFO*/
	value32 = odm_get_bb_reg(dm, R_0xdac, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, R_0xdb0, MASKDWORD);

	short_cfo_b = (s32)(value32 & 0xfff); /*S(12,11)*/
	short_cfo_a = (s32)((value32 & 0x0fff0000) >> 16);

	long_cfo_b = (s32)(value32_1 & 0x1fff); /*S(13,12)*/
	long_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	/*SFO 2's to dec*/
	if (short_cfo_a > 2047)
		short_cfo_a = short_cfo_a - 4096;
	if (short_cfo_b > 2047)
		short_cfo_b = short_cfo_b - 4096;

	short_cfo_a = (short_cfo_a * 312500) / 2048;
	short_cfo_b = (short_cfo_b * 312500) / 2048;

	/*@LFO 2's to dec*/

	if (long_cfo_a > 4095)
		long_cfo_a = long_cfo_a - 8192;

	if (long_cfo_b > 4095)
		long_cfo_b = long_cfo_b - 8192;

	long_cfo_a = long_cfo_a * 312500 / 4096;
	long_cfo_b = long_cfo_b * 312500 / 4096;

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s",
		 "CFO Report Info");
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "Short CFO(Hz) <A/B>", short_cfo_a,
		 short_cfo_b);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "Long CFO(Hz) <A/B>", long_cfo_a,
		 long_cfo_b);

	/*SCFO*/
	value32 = odm_get_bb_reg(dm, R_0xdb8, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, R_0xdb4, MASKDWORD);

	scfo_b = (s32)(value32 & 0x7ff); /*S(11,10)*/
	scfo_a = (s32)((value32 & 0x07ff0000) >> 16);

	if (scfo_a > 1023)
		scfo_a = scfo_a - 2048;

	if (scfo_b > 1023)
		scfo_b = scfo_b - 2048;

	scfo_a = scfo_a * 312500 / 1024;
	scfo_b = scfo_b * 312500 / 1024;

	avg_cfo_b = (s32)(value32_1 & 0x1fff); /*S(13,12)*/
	avg_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	if (avg_cfo_a > 4095)
		avg_cfo_a = avg_cfo_a - 8192;

	if (avg_cfo_b > 4095)
		avg_cfo_b = avg_cfo_b - 8192;

	avg_cfo_a = avg_cfo_a * 312500 / 4096;
	avg_cfo_b = avg_cfo_b * 312500 / 4096;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "value SCFO(Hz) <A/B>", scfo_a,
		 scfo_b);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "Avg CFO(Hz) <A/B>", avg_cfo_a,
		 avg_cfo_b);

	value32 = odm_get_bb_reg(dm, R_0xdbc, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, R_0xde0, MASKDWORD);

	cfo_end_b = (s32)(value32 & 0x1fff); /*S(13,12)*/
	cfo_end_a = (s32)((value32 & 0x1fff0000) >> 16);

	if (cfo_end_a > 4095)
		cfo_end_a = cfo_end_a - 8192;

	if (cfo_end_b > 4095)
		cfo_end_b = cfo_end_b - 8192;

	cfo_end_a = cfo_end_a * 312500 / 4096;
	cfo_end_b = cfo_end_b * 312500 / 4096;

	acq_cfo_b = (s32)(value32_1 & 0x1fff); /*S(13,12)*/
	acq_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	if (acq_cfo_a > 4095)
		acq_cfo_a = acq_cfo_a - 8192;

	if (acq_cfo_b > 4095)
		acq_cfo_b = acq_cfo_b - 8192;

	acq_cfo_a = acq_cfo_a * 312500 / 4096;
	acq_cfo_b = acq_cfo_b * 312500 / 4096;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "End CFO(Hz) <A/B>", cfo_end_a,
		 cfo_end_b);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "ACQ CFO(Hz) <A/B>", acq_cfo_a,
		 acq_cfo_b);
#endif
}
#endif

#if (ODM_IC_11AC_SERIES_SUPPORT)
#if (RTL8822B_SUPPORT)
void phydm_bb_hw_dbg_info_8822b(void *dm_void, u32 *_used, char *output,
				u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 condi_num = 0;
	u8 i = 0;

	if (!(dm->support_ic_type == ODM_RTL8822B))
		return;

	condi_num = phydm_get_condi_num_8822b(dm);
	phydm_get_condi_num_acc_8822b(dm);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d.%.4d", "condi_num",
		 condi_num >> 4, phydm_show_fraction_num(condi_num & 0xf, 4));

	for (i = 0; i < CN_CNT_MAX; i++) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n Tone_num[CN>%d]%-21s = %d",
			 i, " ", dm->phy_dbg_info.condi_num_cdf[i]);
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

void phydm_bb_hw_dbg_info_ac(void *dm_void, u32 *_used, char *output,
			     u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char *tmp_string = NULL;
	u8 rx_ht_bw, rx_vht_bw, rxsc, rx_ht, bw_idx = 0;
	static u8 v_rx_bw;
	u32 value32, value32_1, value32_2, value32_3;
	struct phydm_cfo_rpt cfo;
	u8 i = 0;
	static u8 tail, parity, rsv, vrsv, smooth, htsound, agg;
	static u8 stbc, vstbc, fec, fecext, sgi, sgiext, htltf, vgid, v_nsts;
	static u8 vtxops, vrsv2, vbrsv, bf, vbcrc;
	static u16 h_length, htcrc8, length;
	static u16 vpaid;
	static u16 v_length, vhtcrc8, v_mcss, v_tail, vb_tail;
	static u8 hmcss, hrx_bw;
	u8 pwdb;
	s8 rxevm_0, rxevm_1, rxevm_2;
	u8 rf_gain[4];
	u8 rx_snr[4];
	s32 sig_power;

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s\n",
		 "BB Report Info");

	/*@ [BW & Mode] =====================================================*/

	value32 = odm_get_bb_reg(dm, R_0xf80, MASKDWORD);
	rx_ht = (u8)((value32 & 0x180) >> 7);

	if (rx_ht == AD_VHT_MODE) {
		tmp_string = "VHT";
		bw_idx = (u8)((value32 >> 1) & 0x3);
	} else if (rx_ht == AD_HT_MODE) {
		tmp_string = "HT";
		bw_idx = (u8)(value32 & 0x1);
	} else {
		tmp_string = "Legacy";
		bw_idx = 0;
	}
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s %s %dM", "mode", tmp_string, (20 << bw_idx));

	if (rx_ht != AD_LEGACY_MODE) {
		rxsc = (u8)(value32 & 0x78);

		if (rxsc == 0)
			tmp_string = "duplicate/full bw";
		else if (rxsc == 1)
			tmp_string = "usc20-1";
		else if (rxsc == 2)
			tmp_string = "lsc20-1";
		else if (rxsc == 3)
			tmp_string = "usc20-2";
		else if (rxsc == 4)
			tmp_string = "lsc20-2";
		else if (rxsc == 9)
			tmp_string = "usc40";
		else if (rxsc == 10)
			tmp_string = "lsc40";

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "  %-35s", tmp_string);
	}

	/*@ [RX signal power and AGC related info] ==========================*/

	pwdb = (u8)odm_get_bb_reg(dm, R_0xf90, MASKBYTE1);
	sig_power = -110 + (pwdb >> 1);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power);

	value32 = odm_get_bb_reg(dm, R_0xd14, MASKDWORD);
	rx_snr[RF_PATH_A] = (u8)(value32 & 0xFF) >> 1; /*@ S(8,1)*/
	rf_gain[RF_PATH_A] = (s8)(((value32 & MASKBYTE1) >> 8) * 2);

	value32 = odm_get_bb_reg(dm, R_0xd54, MASKDWORD);
	rx_snr[RF_PATH_B] = (u8)(value32 & 0xFF) >> 1; /*@ S(8,1)*/
	rf_gain[RF_PATH_B] = (s8)(((value32 & MASKBYTE1) >> 8) * 2);

	value32 = odm_get_bb_reg(dm, R_0xd94, MASKDWORD);
	rx_snr[RF_PATH_C] = (u8)(value32 & 0xFF) >> 1; /*@ S(8,1)*/
	rf_gain[RF_PATH_C] = (s8)(((value32 & MASKBYTE1) >> 8) * 2);

	value32 = odm_get_bb_reg(dm, R_0xdd4, MASKDWORD);
	rx_snr[RF_PATH_D] = (u8)(value32 & 0xFF) >> 1; /*@ S(8,1)*/
	rf_gain[RF_PATH_D] = (s8)(((value32 & MASKBYTE1) >> 8) * 2);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)",
		 rf_gain[RF_PATH_A], rf_gain[RF_PATH_B],
		 rf_gain[RF_PATH_C], rf_gain[RF_PATH_D]);

	/*@ [RX counter Info] ===============================================*/

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "OFDM CCA cnt",
		 odm_get_bb_reg(dm, R_0xf08, 0xFFFF0000));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "OFDM SBD Fail cnt",
		 odm_get_bb_reg(dm, R_0xfd0, 0xFFFF));

	value32 = odm_get_bb_reg(dm, R_0xfc4, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail cnt",
		 value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "CCK CCA cnt",
		 odm_get_bb_reg(dm, R_0xfcc, 0xFFFF));

	value32 = odm_get_bb_reg(dm, R_0xfbc, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d",
		 "LSIG (parity Fail/rate Illegal) cnt", value32 & 0xFFFF,
		 ((value32 & 0xFFFF0000) >> 16));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT cnt",
		 odm_get_bb_reg(dm, R_0xfc0, (0xFFFF0000 >> 16)),
		 odm_get_bb_reg(dm, R_0xfc8, 0xFFFF));

	/*@ [PostFFT Info] =================================================*/
	value32 = odm_get_bb_reg(dm, R_0xf8c, MASKDWORD);
	rxevm_0 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_0 /= 2;
	if (rxevm_0 < -63)
		rxevm_0 = 0;

	rxevm_1 = (s8)((value32 & MASKBYTE3) >> 24);
	rxevm_1 /= 2;
	value32 = odm_get_bb_reg(dm, R_0xf88, MASKDWORD);
	rxevm_2 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_2 /= 2;

	if (rxevm_1 < -63)
		rxevm_1 = 0;
	if (rxevm_2 < -63)
		rxevm_2 = 0;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", rxevm_0,
		 rxevm_1, rxevm_2);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D dB)",
		 rx_snr[RF_PATH_A], rx_snr[RF_PATH_B],
		 rx_snr[RF_PATH_C], rx_snr[RF_PATH_D]);

	value32 = odm_get_bb_reg(dm, R_0xf8c, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32 & 0xFFFF,
		 ((value32 & 0xFFFF0000) >> 16));

	/*@ [CFO Report Info] ===============================================*/
	phydm_get_cfo_info(dm, &cfo);
	for (i = 0; i < dm->num_rf_path; i++) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %s[%d] %-28s = {%d, %d, %d, %d, %d}",
			 "CFO", i, "{S, L, Sec, Acq, End}",
			 cfo.cfo_rpt_s[i], cfo.cfo_rpt_l[i], cfo.cfo_rpt_sec[i],
			 cfo.cfo_rpt_acq[i], cfo.cfo_rpt_end[i]);
	}

	/*@ [L-SIG Content] =================================================*/
	value32 = odm_get_bb_reg(dm, R_0xf20, MASKDWORD);

	tail = (u8)((value32 & 0xfc0000) >> 18);/*@[23:18]*/
	parity = (u8)((value32 & 0x20000) >> 17);/*@[17]*/
	length = (u16)((value32 & 0x1ffe0) >> 5);/*@[16:5]*/
	rsv = (u8)((value32 & 0x10) >> 4);/*@[4]*/

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s",
		 "L-SIG");
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d M", "rate",
		 phydm_get_l_sig_rate(dm, (u8)(value32 & 0x0f)));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %x / %d / %d", "Rsv/length/parity", rsv, length,
		 parity);

	if (rx_ht == AD_HT_MODE) {
	/*@ [HT SIG 1] ======================================================*/
		value32 = odm_get_bb_reg(dm, R_0xf2c, MASKDWORD);

		hmcss = (u8)(value32 & 0x7F);
		hrx_bw = (u8)((value32 & 0x80) >> 7);
		h_length = (u16)((value32 & 0x0fff00) >> 8);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "HT-SIG1");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %d / %d / %d", "MCS/BW/length",
			 hmcss, hrx_bw, h_length);
	/*@ [HT SIG 2] ======================================================*/
		value32 = odm_get_bb_reg(dm, R_0xf30, MASKDWORD);
		smooth = (u8)(value32 & 0x01);
		htsound = (u8)((value32 & 0x02) >> 1);
		rsv = (u8)((value32 & 0x04) >> 2);
		agg = (u8)((value32 & 0x08) >> 3);
		stbc = (u8)((value32 & 0x30) >> 4);
		fec = (u8)((value32 & 0x40) >> 6);
		sgi = (u8)((value32 & 0x80) >> 7);
		htltf = (u8)((value32 & 0x300) >> 8);
		htcrc8 = (u16)((value32 & 0x3fc00) >> 10);
		tail = (u8)((value32 & 0xfc0000) >> 18);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s",
			 "HT-SIG2");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x",
			 "Smooth/NoSound/Rsv/Aggregate/STBC/LDPC",
			 smooth, htsound, rsv, agg, stbc, fec);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x",
			 "SGI/E-HT-LTFs/CRC/tail",
			 sgi, htltf, htcrc8, tail);
	} else if (rx_ht == AD_VHT_MODE) {
	/*@ [VHT SIG A1] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0xf2c, MASKDWORD);

		v_rx_bw = (u8)(value32 & 0x03);
		vrsv = (u8)((value32 & 0x04) >> 2);
		vstbc = (u8)((value32 & 0x08) >> 3);
		vgid = (u8)((value32 & 0x3f0) >> 4);
		v_nsts = (u8)(((value32 & 0x1c00) >> 10) + 1);
		vpaid = (u16)((value32 & 0x3fe000) >> 13);
		vtxops = (u8)((value32 & 0x400000) >> 22);
		vrsv2 = (u8)((value32 & 0x800000) >> 23);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s",
			 "VHT-SIG-A1");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x / %x",
			 "BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", v_rx_bw,
			 vrsv, vstbc, vgid, v_nsts, vpaid, vtxops, vrsv2);

	/*@ [VHT SIG A2] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0xf30, MASKDWORD);

		/* @sgi=(u8)(value32&0x01); */
		sgiext = (u8)(value32 & 0x03);
		/* @fec = (u8)(value32&0x04); */
		fecext = (u8)((value32 & 0x0C) >> 2);

		v_mcss = (u8)((value32 & 0xf0) >> 4);
		bf = (u8)((value32 & 0x100) >> 8);
		vrsv = (u8)((value32 & 0x200) >> 9);
		vhtcrc8 = (u16)((value32 & 0x3fc00) >> 10);
		v_tail = (u8)((value32 & 0xfc0000) >> 18);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "VHT-SIG-A2");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x",
			 "SGI/FEC/MCS/BF/Rsv/CRC/tail",
			 sgiext, fecext, v_mcss, bf, vrsv, vhtcrc8, v_tail);

	/*@ [VHT SIG B] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0xf34, MASKDWORD);

		#if 0
		v_length = (u16)(value32 & 0x1fffff);
		vbrsv = (u8)((value32 & 0x600000) >> 21);
		vb_tail = (u16)((value32 & 0x1f800000) >> 23);
		vbcrc = (u8)((value32 & 0x80000000) >> 31);
		#endif

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "VHT-SIG-B");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x",
			 "Codeword", value32);

		#if 0
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x",
			 "length/Rsv/tail/CRC",
			 v_length, vbrsv, vb_tail, vbcrc);
		#endif
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
void phydm_bb_hw_dbg_info_jgr3(void *dm_void, u32 *_used, char *output,
			       u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char *tmp_string = NULL;
	u8 rx_ht_bw = 0, rx_vht_bw = 0, rx_ht = 0;
	static u8 v_rx_bw;
	u32 value32 = 0;
	u8 i = 0;
	static u8 tail, parity, rsv, vrsv, smooth, htsound, agg;
	static u8 stbc, vstbc, fec, fecext, sgi, sgiext, htltf, vgid, v_nsts;
	static u8 vtxops, vrsv2, vbrsv, bf, vbcrc;
	static u16 h_length, htcrc8, length;
	static u16 vpaid;
	static u16 v_length, vhtcrc8, v_mcss, v_tail, vb_tail;
	static u8 hmcss, hrx_bw;

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s\n",
		 "BB Report Info");

	/*@ [Mode] =====================================================*/

	value32 = odm_get_bb_reg(dm, R_0x2c20, MASKDWORD);
	rx_ht = (u8)((value32 & 0xC0000) >> 18);
	if (rx_ht == AD_VHT_MODE)
		tmp_string = "VHT";
	else if (rx_ht == AD_HT_MODE)
		tmp_string = "HT";
	else
		tmp_string = "Legacy";

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s %s", "mode", tmp_string);
	/*@ [RX counter Info] ===============================================*/

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "CCK CCA cnt",
		 odm_get_bb_reg(dm, R_0x2c08, 0xFFFF));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "OFDM CCA cnt",
		 odm_get_bb_reg(dm, R_0x2c08, 0xFFFF0000));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d", "OFDM SBD Fail cnt",
		 odm_get_bb_reg(dm, R_0x2d20, 0xFFFF0000));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d",
		 "LSIG (parity Fail/rate Illegal) cnt",
		 odm_get_bb_reg(dm, R_0x2d04, 0xFFFF0000),
		 odm_get_bb_reg(dm, R_0x2d08, 0xFFFF));

	value32 = odm_get_bb_reg(dm, R_0x2d10, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT cnt",
		 value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));

	value32 = odm_get_bb_reg(dm, R_0x2d0c, MASKDWORD);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail cnt",
		 value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));
	/*@ [L-SIG Content] =================================================*/
	value32 = odm_get_bb_reg(dm, R_0x2c20, MASKDWORD);

	parity = (u8)((value32 & 0x20000) >> 17);/*@[17]*/
	length = (u16)((value32 & 0x1ffe0) >> 5);/*@[16:5]*/
	rsv = (u8)((value32 & 0x10) >> 4);/*@[4]*/

	PDM_SNPF(out_len, used, output + used, out_len - used, "\r\n %-35s",
		 "L-SIG");
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %d M", "rate",
		 phydm_get_l_sig_rate(dm, (u8)(value32 & 0x0f)));

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\r\n %-35s = %x / %d / %d", "Rsv/length/parity", rsv, length,
		 parity);

	if (rx_ht == AD_HT_MODE) {
	/*@ [HT SIG 1] ======================================================*/
		value32 = odm_get_bb_reg(dm, R_0x2c2c, MASKDWORD);

		hmcss = (u8)(value32 & 0x7F);
		hrx_bw = (u8)((value32 & 0x80) >> 7);
		h_length = (u16)((value32 & 0x0fff00) >> 8);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "HT-SIG1");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %d / %d / %d", "MCS/BW/length",
			 hmcss, hrx_bw, h_length);
	/*@ [HT SIG 2] ======================================================*/
		value32 = odm_get_bb_reg(dm, R_0x2c30, MASKDWORD);
		smooth = (u8)(value32 & 0x01);
		htsound = (u8)((value32 & 0x02) >> 1);
		rsv = (u8)((value32 & 0x04) >> 2);
		agg = (u8)((value32 & 0x08) >> 3);
		stbc = (u8)((value32 & 0x30) >> 4);
		fec = (u8)((value32 & 0x40) >> 6);
		sgi = (u8)((value32 & 0x80) >> 7);
		htltf = (u8)((value32 & 0x300) >> 8);
		htcrc8 = (u16)((value32 & 0x3fc00) >> 10);
		tail = (u8)((value32 & 0xfc0000) >> 18);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s",
			 "HT-SIG2");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x",
			 "Smooth/NoSound/Rsv/Aggregate/STBC/LDPC",
			 smooth, htsound, rsv, agg, stbc, fec);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x",
			 "SGI/E-HT-LTFs/CRC/tail",
			 sgi, htltf, htcrc8, tail);
	} else if (rx_ht == AD_VHT_MODE) {
	/*@ [VHT SIG A1] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0x2c2c, MASKDWORD);

		v_rx_bw = (u8)(value32 & 0x03);
		vrsv = (u8)((value32 & 0x04) >> 2);
		vstbc = (u8)((value32 & 0x08) >> 3);
		vgid = (u8)((value32 & 0x3f0) >> 4);
		v_nsts = (u8)(((value32 & 0x1c00) >> 10) + 1);
		vpaid = (u16)((value32 & 0x3fe000) >> 13);
		vtxops = (u8)((value32 & 0x400000) >> 22);
		vrsv2 = (u8)((value32 & 0x800000) >> 23);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s",
			 "VHT-SIG-A1");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x / %x",
			 "BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", v_rx_bw,
			 vrsv, vstbc, vgid, v_nsts, vpaid, vtxops, vrsv2);

	/*@ [VHT SIG A2] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0x2c30, MASKDWORD);

		/* @sgi=(u8)(value32&0x01); */
		sgiext = (u8)(value32 & 0x03);
		/* @fec = (u8)(value32&0x04); */
		fecext = (u8)((value32 & 0x0C) >> 2);

		v_mcss = (u8)((value32 & 0xf0) >> 4);
		bf = (u8)((value32 & 0x100) >> 8);
		vrsv = (u8)((value32 & 0x200) >> 9);
		vhtcrc8 = (u16)((value32 & 0x3fc00) >> 10);
		v_tail = (u8)((value32 & 0xfc0000) >> 18);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "VHT-SIG-A2");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x",
			 "SGI/FEC/MCS/BF/Rsv/CRC/tail",
			 sgiext, fecext, v_mcss, bf, vrsv, vhtcrc8, v_tail);

	/*@ [VHT SIG B] ====================================================*/
		value32 = odm_get_bb_reg(dm, R_0x2c34, MASKDWORD);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s", "VHT-SIG-B");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x",
			 "Codeword", value32);

		if (v_rx_bw == 0) {
			v_length = (u16)(value32 & 0x1ffff);
			vbrsv = (u8)((value32 & 0xE0000) >> 17);
			vb_tail = (u16)((value32 & 0x03F00000) >> 20);
		} else if (v_rx_bw == 1) {
			v_length = (u16)(value32 & 0x7FFFF);
			vbrsv = (u8)((value32 & 0x180000) >> 19);
			vb_tail = (u16)((value32 & 0x07E00000) >> 21);
		} else if (v_rx_bw == 2) {
			v_length = (u16)(value32 & 0x1fffff);
			vbrsv = (u8)((value32 & 0x600000) >> 21);
			vb_tail = (u16)((value32 & 0x1f800000) >> 23);
		}
		vbcrc = (u8)((value32 & 0x80000000) >> 31);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\r\n %-35s = %x / %x / %x / %x",
			 "length/Rsv/tail/CRC",
			 v_length, vbrsv, vb_tail, vbcrc);
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

u8 phydm_get_l_sig_rate(void *dm_void, u8 rate_idx_l_sig)
{
	u8 rate_idx = 0xff;

	switch (rate_idx_l_sig) {
	case 0x0b:
		rate_idx = 6;
		break;
	case 0x0f:
		rate_idx = 9;
		break;
	case 0x0a:
		rate_idx = 12;
		break;
	case 0x0e:
		rate_idx = 18;
		break;
	case 0x09:
		rate_idx = 24;
		break;
	case 0x0d:
		rate_idx = 36;
		break;
	case 0x08:
		rate_idx = 48;
		break;
	case 0x0c:
		rate_idx = 54;
		break;
	default:
		rate_idx = 0xff;
		break;
	}

	return rate_idx;
}

void phydm_bb_hw_dbg_info(void *dm_void, char input[][16], u32 *_used,
			  char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	switch (dm->ic_ip_series) {
	#if (ODM_IC_11N_SERIES_SUPPORT)
	case PHYDM_IC_N:
		phydm_bb_hw_dbg_info_n(dm, &used, output, &out_len);
		break;
	#endif

	#if (ODM_IC_11AC_SERIES_SUPPORT)
	case PHYDM_IC_AC:
		phydm_bb_hw_dbg_info_ac(dm, &used, output, &out_len);
		phydm_reset_bb_hw_cnt(dm);
		#if (RTL8822B_SUPPORT)
		phydm_bb_hw_dbg_info_8822b(dm, &used, output, &out_len);
		#endif
		break;
	#endif

	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	case PHYDM_IC_JGR3:
		phydm_bb_hw_dbg_info_jgr3(dm, &used, output, &out_len);
		phydm_reset_bb_hw_cnt(dm);
		break;
	#endif
	default:
		break;
	}

	*_used = used;
	*_out_len = out_len;
}

#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

void phydm_dm_summary_cli_win(void *dm_void, char *buf, u8 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct phydm_cfo_track_struct *cfo_t = &dm->dm_cfo_track;
	struct cmn_sta_info *sta = NULL;
	struct ra_sta_info *ra = NULL;
	struct dtp_info *dtp = NULL;
	u64 comp = dm->support_ability;
	u64 pause_comp = dm->pause_ability;

	if (!dm->is_linked) {
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "[%s]No Link !!!\n", __func__);
		RT_PRINT(buf);
		return;
	}

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "00.(%s) %-12s: IGI=0x%x, Dyn_Rng=0x%x~0x%x, FA_th={%d,%d,%d}\n",
		   ((comp & ODM_BB_DIG) ?
		   ((pause_comp & ODM_BB_DIG) ? "P" : "V") : "."),
		   "DIG",
		   dig_t->cur_ig_value,
		   dig_t->rx_gain_range_min, dig_t->rx_gain_range_max,
		   dig_t->fa_th[0], dig_t->fa_th[1], dig_t->fa_th[2]);
        RT_PRINT(buf);

	sta = dm->phydm_sta_info[macid];
	if (is_sta_active(sta)) {
		RT_PRINT(buf);

		ra = &sta->ra_info;
		dtp = &sta->dtp_stat;

		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "01.(%s) %-12s: rssi_lv=%d, mask=0x%llx\n",
			   ((comp & ODM_BB_RA_MASK) ?
			   ((pause_comp & ODM_BB_RA_MASK) ? "P" : "V") : "."),
			   "RaMask",
			   ra->rssi_level, ra->ramask);
		RT_PRINT(buf);

		#ifdef CONFIG_DYNAMIC_TX_TWR
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "02.(%s) %-12s: pwr_lv=%d\n",
			   ((comp & ODM_BB_DYNAMIC_TXPWR) ?
			   ((pause_comp & ODM_BB_DYNAMIC_TXPWR) ? "P" : "V") : "."),
			   "DynTxPwr",
			   dtp->sta_tx_high_power_lvl);
		RT_PRINT(buf);
		#endif
	}

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "05.(%s) %-12s: cck_pd_lv=%d\n",
		   ((comp & ODM_BB_CCK_PD) ?
		   ((pause_comp & ODM_BB_CCK_PD) ? "P" : "V") : "."),
		   "CCK_PD", dm->dm_cckpd_table.cck_pd_lv);
	RT_PRINT(buf);

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "06.(%s) %-12s: div_type=%d, curr_ant=%s\n",
		   ((comp & ODM_BB_ANT_DIV) ?
		   ((pause_comp & ODM_BB_ANT_DIV) ? "P" : "V") : "."),
		   "ANT_DIV",
		   dm->ant_div_type,
		   (dm->dm_fat_table.rx_idle_ant == MAIN_ANT) ? "MAIN" : "AUX");
	RT_PRINT(buf);
#endif

#ifdef PHYDM_POWER_TRAINING_SUPPORT
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "08.(%s) %-12s: PT_score=%d, disable_PT=%d\n",
		   ((comp & ODM_BB_PWR_TRAIN) ?
		   ((pause_comp & ODM_BB_PWR_TRAIN) ? "P" : "V") : "."),
		   "PwrTrain",
		   dm->pow_train_table.pow_train_score,
		   dm->is_disable_power_training);
	RT_PRINT(buf);
#endif

#ifdef CONFIG_PHYDM_DFS_MASTER
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "11.(%s) %-12s: dbg_mode=%d, region_domain=%d\n",
		   ((comp & ODM_BB_DFS) ?
		   ((pause_comp & ODM_BB_DFS) ? "P" : "V") : "."),
		   "DFS",
		   dm->dfs.dbg_mode, dm->dfs_region_domain);
	RT_PRINT(buf);
#endif
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "13.(%s) %-12s: th{l2h, h2l}={%d, %d}, edcca_flag=%d\n",
		   ((comp & ODM_BB_ADAPTIVITY) ?
		   ((pause_comp & ODM_BB_ADAPTIVITY) ? "P" : "V") : "."),
		   "Adaptivity",
		   dm->adaptivity.th_l2h, dm->adaptivity.th_h2l,
		   dm->false_alm_cnt.edcca_flag);
	RT_PRINT(buf);
#endif
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "14.(%s) %-12s: CFO_avg=%d kHz, CFO_traking=%s%d\n",
		   ((comp & ODM_BB_CFO_TRACKING) ?
		   ((pause_comp & ODM_BB_CFO_TRACKING) ? "P" : "V") : "."),
		   "CfoTrack",
		   cfo_t->CFO_ave_pre,
		   ((cfo_t->crystal_cap > cfo_t->def_x_cap) ? "+" : "-"),
		   DIFF_2(cfo_t->crystal_cap, cfo_t->def_x_cap));
	RT_PRINT(buf);

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "15.(%s) %-12s: ratio{nhm, clm}={%d, %d}\n",
		   ((comp & ODM_BB_ENV_MONITOR) ?
		   ((pause_comp & ODM_BB_ENV_MONITOR) ? "P" : "V") : "."),
		   "EnvMntr",
		   dm->dm_ccx_info.nhm_ratio, dm->dm_ccx_info.clm_ratio);
	RT_PRINT(buf);
#ifdef PHYDM_PRIMARY_CCA
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "16.(%s) %-12s: CCA @ (%s SB)\n",
		   ((comp & ODM_BB_PRIMARY_CCA) ?
		   ((pause_comp & ODM_BB_PRIMARY_CCA) ? "P" : "V") : "."),
		   "PriCCA",
		   ((dm->dm_pri_cca.mf_state == MF_USC_LSC) ? "D" :
		   ((dm->dm_pri_cca.mf_state == MF_LSC) ? "L" : "U")));
	RT_PRINT(buf);
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "17.(%s) %-12s: soml_en = %s\n",
		   ((comp & ODM_BB_ADAPTIVE_SOML) ?
		   ((pause_comp & ODM_BB_ADAPTIVE_SOML) ? "P" : "V") : "."),
		   "A-SOML",
		   (dm->dm_soml_table.soml_last_state == SOML_ON) ?
		   "ON" : "OFF");
	RT_PRINT(buf);
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "18.(%s) %-12s:\n",
		   ((comp & ODM_BB_LNA_SAT_CHK) ?
		   ((pause_comp & ODM_BB_LNA_SAT_CHK) ? "P" : "V") : "."),
		   "LNA_SAT_CHK");
	RT_PRINT(buf);
#endif
}

void phydm_basic_dbg_msg_cli_win(void *dm_void, char *buf)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fa_struct *fa_t = &dm->false_alm_cnt;
	struct phydm_cfo_track_struct *cfo_t = &dm->dm_cfo_track;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg->physts_statistic_info;
	struct phydm_phystatus_avg *dbg_avg = &dbg->phystatus_statistic_avg;
	char *rate_type = NULL;
	u8 tmp_rssi_avg[4];
	u8 tmp_snr_avg[4];
	u8 tmp_evm_avg[4];
	u32 tmp_cnt = 0;
	u8 macid, target_macid = 0;
	u8 i = 0;
	u8 rate_num = dm->num_rf_path;
	u8 ss_ofst = 0;
	struct cmn_sta_info *entry = NULL;
	char dbg_buf[PHYDM_SNPRINT_SIZE] = {0};

	if (dm->debug_components & DBG_CMN)
		return;

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n PHYDM Common Dbg Msg --------->");
	RT_PRINT(buf);
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n System up time=%d", dm->phydm_sys_up_time);
	RT_PRINT(buf);

	if (dm->is_linked) {
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n ID=((%d)), BW=((%d)), fc=((CH-%d))",
			   dm->curr_station_id, 20 << *dm->band_width, *dm->channel);
		RT_PRINT(buf);

		if (((*dm->channel <= 14) && (*dm->band_width == CHANNEL_WIDTH_40)) &&
		    (dm->support_ic_type & ODM_IC_11N_SERIES)) {
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n Primary CCA at ((%s SB))",
				   (*dm->sec_ch_offset == SECOND_CH_AT_LSB) ? "U" : "L");
			RT_PRINT(buf);
		}

		if ((dm->support_ic_type & PHYSTS_2ND_TYPE_IC) || dm->rx_rate > ODM_RATE11M) {
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [AGC Idx] {0x%x, 0x%x, 0x%x, 0x%x}",
				   dm->ofdm_agc_idx[0], dm->ofdm_agc_idx[1],
				   dm->ofdm_agc_idx[2], dm->ofdm_agc_idx[3]);
			RT_PRINT(buf);
		} else {
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [CCK AGC Idx] {LNA,VGA}={0x%x, 0x%x}",
				   dm->cck_lna_idx, dm->cck_vga_idx);
			RT_PRINT(buf);
		}

		phydm_print_rate_2_buff(dm, dm->rx_rate, dbg_buf, PHYDM_SNPRINT_SIZE);
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n RSSI:{%d, %d, %d, %d}, RxRate:%s (0x%x)",
			   (dm->rssi_a == 0xff) ? 0 : dm->rssi_a,
			   (dm->rssi_b == 0xff) ? 0 : dm->rssi_b,
			   (dm->rssi_c == 0xff) ? 0 : dm->rssi_c,
			   (dm->rssi_d == 0xff) ? 0 : dm->rssi_d,
			  dbg_buf, dm->rx_rate);
		RT_PRINT(buf);

		phydm_print_rate_2_buff(dm, dm->phy_dbg_info.beacon_phy_rate, dbg_buf, PHYDM_SNPRINT_SIZE);
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n Beacon_cnt=%d, rate_idx:%s (0x%x)",
			   dm->phy_dbg_info.beacon_cnt_in_period,
			   dbg_buf,
			   dm->phy_dbg_info.beacon_phy_rate);
		RT_PRINT(buf);

		/*Show phydm_rx_rate_distribution;*/
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [RxRate Cnt] =============>");
		RT_PRINT(buf);

		/*@======CCK=================================================*/
		if (*dm->channel <= 14) {
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * CCK = {%d, %d, %d, %d}",
				   dbg->num_qry_legacy_pkt[0], dbg->num_qry_legacy_pkt[1],
				   dbg->num_qry_legacy_pkt[2], dbg->num_qry_legacy_pkt[3]);
			RT_PRINT(buf);
		}
		/*@======OFDM================================================*/
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * OFDM = {%d, %d, %d, %d, %d, %d, %d, %d}",
			   dbg->num_qry_legacy_pkt[4], dbg->num_qry_legacy_pkt[5],
			   dbg->num_qry_legacy_pkt[6], dbg->num_qry_legacy_pkt[7],
			   dbg->num_qry_legacy_pkt[8], dbg->num_qry_legacy_pkt[9],
			   dbg->num_qry_legacy_pkt[10], dbg->num_qry_legacy_pkt[11]);
		RT_PRINT(buf);

		/*@======HT==================================================*/
		if (dbg->ht_pkt_not_zero) {
			for (i = 0; i < rate_num; i++) {
				ss_ofst = (i << 3);

				RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}",
					   (ss_ofst), (ss_ofst + 7),
					   dbg->num_qry_ht_pkt[ss_ofst + 0], dbg->num_qry_ht_pkt[ss_ofst + 1],
					   dbg->num_qry_ht_pkt[ss_ofst + 2], dbg->num_qry_ht_pkt[ss_ofst + 3],
					   dbg->num_qry_ht_pkt[ss_ofst + 4], dbg->num_qry_ht_pkt[ss_ofst + 5],
					   dbg->num_qry_ht_pkt[ss_ofst + 6], dbg->num_qry_ht_pkt[ss_ofst + 7]);
				RT_PRINT(buf);
			}

			if (dbg->low_bw_20_occur) {
				for (i = 0; i < rate_num; i++) {
					ss_ofst = (i << 3);

					RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * [Low BW 20M] HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}",
						   (ss_ofst), (ss_ofst + 7),
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 0], dbg->num_qry_pkt_sc_20m[ss_ofst + 1],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 2], dbg->num_qry_pkt_sc_20m[ss_ofst + 3],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 4], dbg->num_qry_pkt_sc_20m[ss_ofst + 5],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 6], dbg->num_qry_pkt_sc_20m[ss_ofst + 7]);
					RT_PRINT(buf);
				}
			}
		}

#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
		/*@======VHT=================================================*/
		if (dbg->vht_pkt_not_zero) {
			for (i = 0; i < rate_num; i++) {
				ss_ofst = 10 * i;

				RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}",
					   (i + 1),
					   dbg->num_qry_vht_pkt[ss_ofst + 0], dbg->num_qry_vht_pkt[ss_ofst + 1],
					   dbg->num_qry_vht_pkt[ss_ofst + 2], dbg->num_qry_vht_pkt[ss_ofst + 3],
					   dbg->num_qry_vht_pkt[ss_ofst + 4], dbg->num_qry_vht_pkt[ss_ofst + 5],
					   dbg->num_qry_vht_pkt[ss_ofst + 6], dbg->num_qry_vht_pkt[ss_ofst + 7],
					   dbg->num_qry_vht_pkt[ss_ofst + 8], dbg->num_qry_vht_pkt[ss_ofst + 9]);
				RT_PRINT(buf);
			}

			if (dbg->low_bw_20_occur) {
				for (i = 0; i < rate_num; i++) {
					ss_ofst = 10 * i;

					RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n *[Low BW 20M] VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}",
						   (i + 1),
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 0], dbg->num_qry_pkt_sc_20m[ss_ofst + 1],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 2], dbg->num_qry_pkt_sc_20m[ss_ofst + 3],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 4], dbg->num_qry_pkt_sc_20m[ss_ofst + 5],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 6], dbg->num_qry_pkt_sc_20m[ss_ofst + 7],
						   dbg->num_qry_pkt_sc_20m[ss_ofst + 8], dbg->num_qry_pkt_sc_20m[ss_ofst + 9]);
					RT_PRINT(buf);
				}
			}

			if (dbg->low_bw_40_occur) {
				for (i = 0; i < rate_num; i++) {
					ss_ofst = 10 * i;

					RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n *[Low BW 40M] VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}",
						   (i + 1),
						   dbg->num_qry_pkt_sc_40m[ss_ofst + 0], dbg->num_qry_pkt_sc_40m[ss_ofst + 1],
						   dbg->num_qry_pkt_sc_40m[ss_ofst + 2], dbg->num_qry_pkt_sc_40m[ss_ofst + 3],
						   dbg->num_qry_pkt_sc_40m[ss_ofst + 4], dbg->num_qry_pkt_sc_40m[ss_ofst + 5],
						   dbg->num_qry_pkt_sc_40m[ss_ofst + 6], dbg->num_qry_pkt_sc_40m[ss_ofst + 7],
						   dbg->num_qry_pkt_sc_40m[ss_ofst + 8], dbg->num_qry_pkt_sc_40m[ss_ofst + 9]);
					RT_PRINT(buf);
				}
			}
		}
#endif

		phydm_reset_rx_rate_distribution(dm);

		//1 Show phydm_avg_phystatus_val
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [Avg PHY Statistic] ==============>");
		RT_PRINT(buf);
#if 1
		phydm_get_avg_phystatus_val(dm);
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
			   "[Beacon]", dbg_s->rssi_beacon_cnt, dbg_avg->rssi_beacon_avg);
		RT_PRINT(buf);
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
			   "[CCK]", dbg_s->rssi_cck_cnt, dbg_avg->rssi_cck_avg);
		RT_PRINT(buf);

		for (i = 0; i <= 4; i++) {
			if (i > dm->num_rf_path)
				break;

			odm_memory_set(dm, tmp_rssi_avg, 0, 4);
			odm_memory_set(dm, tmp_snr_avg, 0, 4);
			odm_memory_set(dm, tmp_evm_avg, 0, 4);

			#if (defined(PHYDM_COMPILE_ABOVE_4SS))
			if (i == 4) {
				rate_type = "[4-SS]";
				tmp_cnt = dbg_s->rssi_4ss_cnt;
				odm_move_memory(dm, tmp_rssi_avg, dbg_avg->rssi_4ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_4ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_evm_avg, dbg_avg->evm_4ss_avg, 4);
			} else
			#endif
			#if (defined(PHYDM_COMPILE_ABOVE_3SS))
			if (i == 3) {
				rate_type = "[3-SS]";
				tmp_cnt = dbg_s->rssi_3ss_cnt;
				odm_move_memory(dm, tmp_rssi_avg, dbg_avg->rssi_3ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_3ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_evm_avg, dbg_avg->evm_3ss_avg, 3);
			} else
			#endif
			#if (defined(PHYDM_COMPILE_ABOVE_2SS))
			if (i == 2) {
				rate_type = "[2-SS]";
				tmp_cnt = dbg_s->rssi_2ss_cnt;
				odm_move_memory(dm, tmp_rssi_avg, dbg_avg->rssi_2ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_2ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_evm_avg, dbg_avg->evm_2ss_avg, 2);
			} else
			#endif
			if (i == 1) {
				rate_type = "[1-SS]";
				tmp_cnt = dbg_s->rssi_1ss_cnt;
				odm_move_memory(dm, tmp_rssi_avg, dbg_avg->rssi_1ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_1ss_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_evm_avg, &dbg_avg->evm_1ss_avg, 1);
			} else {
				rate_type = "[L-OFDM]";
				tmp_cnt = dbg_s->rssi_ofdm_cnt;
				odm_move_memory(dm, tmp_rssi_avg, dbg_avg->rssi_ofdm_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_ofdm_avg, dm->num_rf_path);
				odm_move_memory(dm, tmp_evm_avg, &dbg_avg->evm_ofdm_avg, 1);
			}

			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE,
				   "* %-8s Cnt=((%.3d)) RSSI:{%.2d, %.2d, %.2d, %.2d} SNR:{%.2d, %.2d, %.2d, %.2d} EVM:{-%.2d, -%.2d, -%.2d, -%.2d}\n",
				    rate_type, tmp_cnt,
				    tmp_rssi_avg[0], tmp_rssi_avg[1], tmp_rssi_avg[2], tmp_rssi_avg[3],
				    tmp_snr_avg[0], tmp_snr_avg[1], tmp_snr_avg[2], tmp_snr_avg[3],
				    tmp_evm_avg[0], tmp_evm_avg[1], tmp_evm_avg[2], tmp_evm_avg[3]);
			RT_PRINT(buf);
		}
#else
		phydm_reset_phystatus_avg(dm);

		/*@CCK*/
		dbg_avg->rssi_cck_avg = (u8)((dbg_s->rssi_cck_cnt != 0) ? (dbg_s->rssi_cck_sum / dbg_s->rssi_cck_cnt) : 0);
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * cck Cnt= ((%d)) RSSI:{%d}",
			   dbg_s->rssi_cck_cnt, dbg_avg->rssi_cck_avg);
		RT_PRINT(buf);

		/*OFDM*/
		if (dbg_s->rssi_ofdm_cnt != 0) {
			dbg_avg->rssi_ofdm_avg = (u8)(dbg_s->rssi_ofdm_sum / dbg_s->rssi_ofdm_cnt);
			dbg_avg->evm_ofdm_avg = (u8)(dbg_s->evm_ofdm_sum / dbg_s->rssi_ofdm_cnt);
			dbg_avg->snr_ofdm_avg = (u8)(dbg_s->snr_ofdm_sum / dbg_s->rssi_ofdm_cnt);
		}

		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * ofdm Cnt= ((%d)) RSSI:{%d} EVM:{%d} SNR:{%d}",
			   dbg_s->rssi_ofdm_cnt, dbg_avg->rssi_ofdm_avg,
			   dbg_avg->evm_ofdm_avg, dbg_avg->snr_ofdm_avg);
		RT_PRINT(buf);

		if (dbg_s->rssi_1ss_cnt != 0) {
			dbg_avg->rssi_1ss_avg = (u8)(dbg_s->rssi_1ss_sum / dbg_s->rssi_1ss_cnt);
			dbg_avg->evm_1ss_avg = (u8)(dbg_s->evm_1ss_sum / dbg_s->rssi_1ss_cnt);
			dbg_avg->snr_1ss_avg = (u8)(dbg_s->snr_1ss_sum / dbg_s->rssi_1ss_cnt);
		}

		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * 1-ss Cnt= ((%d)) RSSI:{%d} EVM:{%d} SNR:{%d}",
			   dbg_s->rssi_1ss_cnt, dbg_avg->rssi_1ss_avg,
			   dbg_avg->evm_1ss_avg, dbg_avg->snr_1ss_avg);
		RT_PRINT(buf);

#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		if (dm->support_ic_type & (PHYDM_IC_ABOVE_2SS)) {
			if (dbg_s->rssi_2ss_cnt != 0) {
				dbg_avg->rssi_2ss_avg[0] = (u8)(dbg_s->rssi_2ss_sum[0] / dbg_s->rssi_2ss_cnt);
				dbg_avg->rssi_2ss_avg[1] = (u8)(dbg_s->rssi_2ss_sum[1] / dbg_s->rssi_2ss_cnt);

				dbg_avg->evm_2ss_avg[0] = (u8)(dbg_s->evm_2ss_sum[0] / dbg_s->rssi_2ss_cnt);
				dbg_avg->evm_2ss_avg[1] = (u8)(dbg_s->evm_2ss_sum[1] / dbg_s->rssi_2ss_cnt);

				dbg_avg->snr_2ss_avg[0] = (u8)(dbg_s->snr_2ss_sum[0] / dbg_s->rssi_2ss_cnt);
				dbg_avg->snr_2ss_avg[1] = (u8)(dbg_s->snr_2ss_sum[1] / dbg_s->rssi_2ss_cnt);
			}

			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * 2-ss Cnt= ((%d)) RSSI:{%d, %d}, EVM:{%d, %d}, SNR:{%d, %d}",
				   dbg_s->rssi_2ss_cnt, dbg_avg->rssi_2ss_avg[0],
				   dbg_avg->rssi_2ss_avg[1], dbg_avg->evm_2ss_avg[0],
				   dbg_avg->evm_2ss_avg[1], dbg_avg->snr_2ss_avg[0],
				   dbg_avg->snr_2ss_avg[1]);
			RT_PRINT(buf);
		}
#endif

#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		if (dm->support_ic_type & (PHYDM_IC_ABOVE_3SS)) {
			if (dbg_s->rssi_3ss_cnt != 0) {
				dbg_avg->rssi_3ss_avg[0] = (u8)(dbg_s->rssi_3ss_sum[0] / dbg_s->rssi_3ss_cnt);
				dbg_avg->rssi_3ss_avg[1] = (u8)(dbg_s->rssi_3ss_sum[1] / dbg_s->rssi_3ss_cnt);
				dbg_avg->rssi_3ss_avg[2] = (u8)(dbg_s->rssi_3ss_sum[2] / dbg_s->rssi_3ss_cnt);

				dbg_avg->evm_3ss_avg[0] = (u8)(dbg_s->evm_3ss_sum[0] / dbg_s->rssi_3ss_cnt);
				dbg_avg->evm_3ss_avg[1] = (u8)(dbg_s->evm_3ss_sum[1] / dbg_s->rssi_3ss_cnt);
				dbg_avg->evm_3ss_avg[2] = (u8)(dbg_s->evm_3ss_sum[2] / dbg_s->rssi_3ss_cnt);

				dbg_avg->snr_3ss_avg[0] = (u8)(dbg_s->snr_3ss_sum[0] / dbg_s->rssi_3ss_cnt);
				dbg_avg->snr_3ss_avg[1] = (u8)(dbg_s->snr_3ss_sum[1] / dbg_s->rssi_3ss_cnt);
				dbg_avg->snr_3ss_avg[2] = (u8)(dbg_s->snr_3ss_sum[2] / dbg_s->rssi_3ss_cnt);
			}

			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * 3-ss Cnt= ((%d)) RSSI:{%d, %d, %d} EVM:{%d, %d, %d} SNR:{%d, %d, %d}",
				   dbg_s->rssi_3ss_cnt, dbg_avg->rssi_3ss_avg[0],
				   dbg_avg->rssi_3ss_avg[1], dbg_avg->rssi_3ss_avg[2],
				   dbg_avg->evm_3ss_avg[0], dbg_avg->evm_3ss_avg[1],
				   dbg_avg->evm_3ss_avg[2], dbg_avg->snr_3ss_avg[0],
				   dbg_avg->snr_3ss_avg[1], dbg_avg->snr_3ss_avg[2]);
			RT_PRINT(buf);
		}
#endif

#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
			if (dbg_s->rssi_4ss_cnt != 0) {
				dbg_avg->rssi_4ss_avg[0] = (u8)(dbg_s->rssi_4ss_sum[0] / dbg_s->rssi_4ss_cnt);
				dbg_avg->rssi_4ss_avg[1] = (u8)(dbg_s->rssi_4ss_sum[1] / dbg_s->rssi_4ss_cnt);
				dbg_avg->rssi_4ss_avg[2] = (u8)(dbg_s->rssi_4ss_sum[2] / dbg_s->rssi_4ss_cnt);
				dbg_avg->rssi_4ss_avg[3] = (u8)(dbg_s->rssi_4ss_sum[3] / dbg_s->rssi_4ss_cnt);

				dbg_avg->evm_4ss_avg[0] = (u8)(dbg_s->evm_4ss_sum[0] / dbg_s->rssi_4ss_cnt);
				dbg_avg->evm_4ss_avg[1] = (u8)(dbg_s->evm_4ss_sum[1] / dbg_s->rssi_4ss_cnt);
				dbg_avg->evm_4ss_avg[2] = (u8)(dbg_s->evm_4ss_sum[2] / dbg_s->rssi_4ss_cnt);
				dbg_avg->evm_4ss_avg[3] = (u8)(dbg_s->evm_4ss_sum[3] / dbg_s->rssi_4ss_cnt);

				dbg_avg->snr_4ss_avg[0] = (u8)(dbg_s->snr_4ss_sum[0] / dbg_s->rssi_4ss_cnt);
				dbg_avg->snr_4ss_avg[1] = (u8)(dbg_s->snr_4ss_sum[1] / dbg_s->rssi_4ss_cnt);
				dbg_avg->snr_4ss_avg[2] = (u8)(dbg_s->snr_4ss_sum[2] / dbg_s->rssi_4ss_cnt);
				dbg_avg->snr_4ss_avg[3] = (u8)(dbg_s->snr_4ss_sum[3] / dbg_s->rssi_4ss_cnt);
			}

			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n * 4-ss Cnt= ((%d)) RSSI:{%d, %d, %d, %d} EVM:{%d, %d, %d, %d} SNR:{%d, %d, %d, %d}",
				   dbg_s->rssi_4ss_cnt, dbg_avg->rssi_4ss_avg[0],
				   dbg_avg->rssi_4ss_avg[1], dbg_avg->rssi_4ss_avg[2],
				   dbg_avg->rssi_4ss_avg[3], dbg_avg->evm_4ss_avg[0],
				   dbg_avg->evm_4ss_avg[1], dbg_avg->evm_4ss_avg[2],
				   dbg_avg->evm_4ss_avg[3], dbg_avg->snr_4ss_avg[0],
				   dbg_avg->snr_4ss_avg[1], dbg_avg->snr_4ss_avg[2],
				   dbg_avg->snr_4ss_avg[3]);
			RT_PRINT(buf);
		}
#endif
#endif
		phydm_reset_phystatus_statistic(dm);
		/*@----------------------------------------------------------*/

		/*Print TX rate*/
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
			entry = dm->phydm_sta_info[macid];

			if (is_sta_active(entry)) {
				phydm_print_rate_2_buff(dm, entry->ra_info.curr_tx_rate, dbg_buf, PHYDM_SNPRINT_SIZE);
				RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n TxRate[%d]=%s (0x%x)", macid, dbg_buf, entry->ra_info.curr_tx_rate);
				RT_PRINT(buf);
				target_macid = macid;
				break;
			}
		}

		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE,
			   "\r\n TP {Tx, Rx, Total} = {%d, %d, %d}Mbps, Traffic_Load=(%d))",
			   dm->tx_tp, dm->rx_tp, dm->total_tp, dm->traffic_load);
		RT_PRINT(buf);

		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n CFO_avg=((%d kHz)), CFO_traking = ((%s%d))",
			   cfo_t->CFO_ave_pre,
			   ((cfo_t->crystal_cap > cfo_t->def_x_cap) ? "+" : "-"),
			   DIFF_2(cfo_t->crystal_cap, cfo_t->def_x_cap));
		RT_PRINT(buf);

		/* @Condition number */
		#if (RTL8822B_SUPPORT)
		if (dm->support_ic_type == ODM_RTL8822B) {
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n Condi_Num=((%d.%.4d))",
				   dm->phy_dbg_info.condi_num >> 4,
				   phydm_show_fraction_num(dm->phy_dbg_info.condi_num & 0xf, 4));
			RT_PRINT(buf);
		}
		#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT || defined(PHYSTS_3RD_TYPE_SUPPORT))
		/*STBC or LDPC pkt*/
		if (dm->support_ic_type & (PHYSTS_2ND_TYPE_IC |
					   PHYSTS_3RD_TYPE_IC))
			RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n Coding: LDPC=((%s)), STBC=((%s))",
				   (dm->phy_dbg_info.is_ldpc_pkt) ? "Y" : "N",
				   (dm->phy_dbg_info.is_stbc_pkt) ? "Y" : "N");
			RT_PRINT(buf);
#endif

	} else {
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n No Link !!!");
		RT_PRINT(buf);
	}

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}",
		   fa_t->cnt_cck_cca, fa_t->cnt_ofdm_cca, fa_t->cnt_cca_all);
	RT_PRINT(buf);

	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE, "\r\n [FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}",
		   fa_t->cnt_cck_fail, fa_t->cnt_ofdm_fail, fa_t->cnt_all);
	RT_PRINT(buf);

	#if (ODM_IC_11N_SERIES_SUPPORT)
	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE,
			   "\r\n [OFDM FA Detail] Parity_Fail=%d, Rate_Illegal=%d, CRC8=%d, MCS_fail=%d, Fast_sync=%d, SB_Search_fail=%d",
			   fa_t->cnt_parity_fail, fa_t->cnt_rate_illegal,
			   fa_t->cnt_crc8_fail, fa_t->cnt_mcs_fail,
			   fa_t->cnt_fast_fsync, fa_t->cnt_sb_search_fail);
		RT_PRINT(buf);
	}
	#endif
	RT_SPRINTF(buf, DBGM_CLI_BUF_SIZE,
		   "\r\n is_linked = %d, Num_client = %d, rssi_min = %d, IGI = 0x%x, bNoisy=%d\n",
		   dm->is_linked, dm->number_linked_client, dm->rssi_min,
		   dm->dm_dig_table.cur_ig_value, dm->noisy_decision);
	RT_PRINT(buf);

	phydm_dm_summary_cli_win(dm, buf, target_macid);
}

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
void phydm_sbd_check(
	struct dm_struct *dm)
{
	static u32 pkt_cnt;
	static boolean sbd_state;
	u32 sym_count, count, value32;

	if (sbd_state == 0) {
		pkt_cnt++;
		/*read SBD conter once every 5 packets*/
		if (pkt_cnt % 5 == 0) {
			odm_set_timer(dm, &dm->sbdcnt_timer, 0); /*@ms*/
			sbd_state = 1;
		}
	} else { /*read counter*/
		value32 = odm_get_bb_reg(dm, R_0xf98, MASKDWORD);
		sym_count = (value32 & 0x7C000000) >> 26;
		count = (value32 & 0x3F00000) >> 20;
		pr_debug("#SBD# sym_count %d count %d\n", sym_count, count);
		sbd_state = 0;
	}
}
#endif

void phydm_sbd_callback(
	struct phydm_timer_list *timer)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	void *adapter = timer->Adapter;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA((PADAPTER)adapter);
	struct dm_struct *dm = &hal_data->DM_OutSrc;

#if USE_WORKITEM
	odm_schedule_work_item(&dm->sbdcnt_workitem);
#else
	phydm_sbd_check(dm);
#endif
#endif
}

void phydm_sbd_workitem_callback(
	void *context)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	void *adapter = (void *)context;
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA((PADAPTER)adapter);
	struct dm_struct *dm = &hal_data->DM_OutSrc;

	phydm_sbd_check(dm);
#endif
}
#endif

void phydm_reset_rx_rate_distribution(struct dm_struct *dm)
{
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;

	odm_memory_set(dm, &dbg->num_qry_legacy_pkt[0], 0,
		       (LEGACY_RATE_NUM * 2));
	odm_memory_set(dm, &dbg->num_qry_ht_pkt[0], 0,
		       (HT_RATE_NUM * 2));
	odm_memory_set(dm, &dbg->num_qry_pkt_sc_20m[0], 0,
		       (LOW_BW_RATE_NUM * 2));

	dbg->ht_pkt_not_zero = false;
	dbg->low_bw_20_occur = false;

#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	odm_memory_set(dm, &dbg->num_qry_vht_pkt[0], 0, VHT_RATE_NUM * 2);
	odm_memory_set(dm, &dbg->num_qry_pkt_sc_40m[0], 0, LOW_BW_RATE_NUM * 2);
	#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1) || (defined(PHYSTS_3RD_TYPE_SUPPORT))
	odm_memory_set(dm, &dbg->num_mu_vht_pkt[0], 0, VHT_RATE_NUM * 2);
	#endif
	dbg->vht_pkt_not_zero = false;
	dbg->low_bw_40_occur = false;
#endif
}

void phydm_rx_rate_distribution(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u8 i = 0;
	u8 rate_num = dm->num_rf_path, ss_ofst = 0;

	PHYDM_DBG(dm, DBG_CMN, "[RxRate Cnt] =============>\n");

	/*@======CCK=========================================================*/
	if (*dm->channel <= 14) {
		PHYDM_DBG(dm, DBG_CMN, "* CCK = {%d, %d, %d, %d}\n",
			  dbg->num_qry_legacy_pkt[0],
			  dbg->num_qry_legacy_pkt[1],
			  dbg->num_qry_legacy_pkt[2],
			  dbg->num_qry_legacy_pkt[3]);
	}
	/*@======OFDM========================================================*/
	PHYDM_DBG(dm, DBG_CMN, "* OFDM = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
		  dbg->num_qry_legacy_pkt[4], dbg->num_qry_legacy_pkt[5],
		  dbg->num_qry_legacy_pkt[6], dbg->num_qry_legacy_pkt[7],
		  dbg->num_qry_legacy_pkt[8], dbg->num_qry_legacy_pkt[9],
		  dbg->num_qry_legacy_pkt[10], dbg->num_qry_legacy_pkt[11]);

	/*@======HT==========================================================*/
	if (dbg->ht_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = (i << 3);

			PHYDM_DBG(dm, DBG_CMN,
				  "* HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (ss_ofst), (ss_ofst + 7),
				  dbg->num_qry_ht_pkt[ss_ofst + 0],
				  dbg->num_qry_ht_pkt[ss_ofst + 1],
				  dbg->num_qry_ht_pkt[ss_ofst + 2],
				  dbg->num_qry_ht_pkt[ss_ofst + 3],
				  dbg->num_qry_ht_pkt[ss_ofst + 4],
				  dbg->num_qry_ht_pkt[ss_ofst + 5],
				  dbg->num_qry_ht_pkt[ss_ofst + 6],
				  dbg->num_qry_ht_pkt[ss_ofst + 7]);
		}

		if (dbg->low_bw_20_occur) {
			for (i = 0; i < rate_num; i++) {
				ss_ofst = (i << 3);

				PHYDM_DBG(dm, DBG_CMN,
					  "* [Low BW 20M] HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
					  (ss_ofst), (ss_ofst + 7),
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 0],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 1],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 2],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 3],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 4],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 5],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 6],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 7]);
			}
		}
	}

#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	/*@======VHT==========================================================*/
	if (dbg->vht_pkt_not_zero) {
		for (i = 0; i < rate_num; i++) {
			ss_ofst = 10 * i;

			PHYDM_DBG(dm, DBG_CMN,
				  "* VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				  (i + 1),
				  dbg->num_qry_vht_pkt[ss_ofst + 0],
				  dbg->num_qry_vht_pkt[ss_ofst + 1],
				  dbg->num_qry_vht_pkt[ss_ofst + 2],
				  dbg->num_qry_vht_pkt[ss_ofst + 3],
				  dbg->num_qry_vht_pkt[ss_ofst + 4],
				  dbg->num_qry_vht_pkt[ss_ofst + 5],
				  dbg->num_qry_vht_pkt[ss_ofst + 6],
				  dbg->num_qry_vht_pkt[ss_ofst + 7],
				  dbg->num_qry_vht_pkt[ss_ofst + 8],
				  dbg->num_qry_vht_pkt[ss_ofst + 9]);
		}

		if (dbg->low_bw_20_occur) {
			for (i = 0; i < rate_num; i++) {
				ss_ofst = 10 * i;

				PHYDM_DBG(dm, DBG_CMN,
					  "*[Low BW 20M] VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
					  (i + 1),
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 0],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 1],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 2],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 3],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 4],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 5],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 6],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 7],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 8],
					  dbg->num_qry_pkt_sc_20m[ss_ofst + 9]);
			}
		}

		if (dbg->low_bw_40_occur) {
			for (i = 0; i < rate_num; i++) {
				ss_ofst = 10 * i;

				PHYDM_DBG(dm, DBG_CMN,
					  "*[Low BW 40M] VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
					  (i + 1),
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 0],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 1],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 2],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 3],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 4],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 5],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 6],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 7],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 8],
					  dbg->num_qry_pkt_sc_40m[ss_ofst + 9]);
			}
		}
	}
#endif
}

u16 phydm_rx_utility(void *dm_void, u16 avg_phy_rate, u8 rx_max_ss,
		     enum channel_width bw)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u16 utility_primitive = 0, utility = 0;

	if (dbg->ht_pkt_not_zero) {
	/*@ MCS7 20M: tp = 65, 1000/65 = 15.38, 65*15.5 = 1007*/
		utility_primitive = avg_phy_rate * 15 + (avg_phy_rate >> 1);
	}
#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	else if (dbg->vht_pkt_not_zero) {
	/*@ VHT 1SS MCS9(fake) 20M: tp = 90, 1000/90 = 11.11, 65*11.125 = 1001*/
		utility_primitive = avg_phy_rate * 11 + (avg_phy_rate >> 3);
	}
#endif
	else {
	/*@ 54M, 1000/54 = 18.5, 54*18.5 = 999*/
		utility_primitive = avg_phy_rate * 18 + (avg_phy_rate >> 1);
	}

	utility = (utility_primitive / rx_max_ss) >> bw;

	if (utility > 1000)
		utility = 1000;

	return utility;
}

u16 phydm_rx_avg_phy_rate(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u8 i = 0, rate_num = 0, rate_base = 0;
	u16 rate = 0, avg_phy_rate = 0;
	u32 pkt_cnt = 0, phy_rate_sum = 0;

	if (dbg->ht_pkt_not_zero) {
		rate_num = HT_RATE_NUM;
		rate_base = ODM_RATEMCS0;
		for (i = 0; i < rate_num; i++) {
			rate = phy_rate_table[i + rate_base] << *dm->band_width;
			phy_rate_sum += dbg->num_qry_ht_pkt[i] * rate;
			pkt_cnt += dbg->num_qry_ht_pkt[i];
		}
	}
#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	else if (dbg->vht_pkt_not_zero) {
		rate_num = VHT_RATE_NUM;
		rate_base = ODM_RATEVHTSS1MCS0;
		for (i = 0; i < rate_num; i++) {
			rate = phy_rate_table[i + rate_base] << *dm->band_width;
			phy_rate_sum += dbg->num_qry_vht_pkt[i] * rate;
			pkt_cnt += dbg->num_qry_vht_pkt[i];
		}
	}
#endif
	else {
		for (i = ODM_RATE1M; i <= ODM_RATE54M; i++) {
			/*SKIP 1M & 6M for beacon case*/
			if (*dm->channel < 36 && i == ODM_RATE1M)
				continue;

			if (*dm->channel >= 36 && i == ODM_RATE6M)
				continue;

			rate = phy_rate_table[i];
			phy_rate_sum += dbg->num_qry_legacy_pkt[i] * rate;
			pkt_cnt += dbg->num_qry_legacy_pkt[i];
		}
	}

#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	if (dbg->low_bw_40_occur) {
		for (i = 0; i < LOW_BW_RATE_NUM; i++) {
			rate = phy_rate_table[i + rate_base]
			       << CHANNEL_WIDTH_40;
			phy_rate_sum += dbg->num_qry_pkt_sc_40m[i] * rate;
			pkt_cnt += dbg->num_qry_pkt_sc_40m[i];
		}
	}
#endif

	if (dbg->low_bw_20_occur) {
		for (i = 0; i < LOW_BW_RATE_NUM; i++) {
			rate = phy_rate_table[i + rate_base];
			phy_rate_sum += dbg->num_qry_pkt_sc_20m[i] * rate;
			pkt_cnt += dbg->num_qry_pkt_sc_20m[i];
		}
	}

	avg_phy_rate = (pkt_cnt == 0) ? 0 : (u16)(phy_rate_sum / pkt_cnt);

	return avg_phy_rate;
}

void phydm_print_hist_2_buf(void *dm_void, u16 *val, u16 len, char *buf,
			    u16 buf_size)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (len == PHY_HIST_SIZE) {
		PHYDM_SNPRINTF(buf, buf_size,
			       "[%.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d]",
			       val[0], val[1], val[2], val[3], val[4],
			       val[5], val[6], val[7], val[8], val[9],
			       val[10], val[11]);
	} else if (len == (PHY_HIST_SIZE - 1)) {
		PHYDM_SNPRINTF(buf, buf_size,
			       "[%.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d, %.2d]",
			       val[0], val[1], val[2], val[3], val[4],
			       val[5], val[6], val[7], val[8], val[9],
			       val[10]);
	}
}

void phydm_nss_hitogram(void *dm_void, enum PDM_RATE_TYPE rate_type)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	char buf[PHYDM_SNPRINT_SIZE] = {0};
	u16 buf_size = PHYDM_SNPRINT_SIZE;
	u16 h_size = PHY_HIST_SIZE;
	u16 *evm_hist = &dbg_s->evm_1ss_hist[0];
	u16 *snr_hist = &dbg_s->snr_1ss_hist[0];
	u8 i = 0;
	u8 ss = phydm_rate_type_2_num_ss(dm, rate_type);

	for (i = 0; i < ss; i++) {
		if (rate_type == PDM_1SS) {
			evm_hist = &dbg_s->evm_1ss_hist[0];
			snr_hist = &dbg_s->snr_1ss_hist[0];
		} else if (rate_type == PDM_2SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_2SS))
			evm_hist = &dbg_s->evm_2ss_hist[i][0];
			snr_hist = &dbg_s->snr_2ss_hist[i][0];
			#endif
		} else if (rate_type == PDM_3SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_3SS))
			evm_hist = &dbg_s->evm_3ss_hist[i][0];
			snr_hist = &dbg_s->snr_3ss_hist[i][0];
			#endif
		} else if (rate_type == PDM_4SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_4SS))
			evm_hist = &dbg_s->evm_4ss_hist[i][0];
			snr_hist = &dbg_s->snr_4ss_hist[i][0];
			#endif
		}

		phydm_print_hist_2_buf(dm, evm_hist, h_size, buf, buf_size);
		PHYDM_DBG(dm, DBG_CMN, "[%d-SS][EVM][%d]=%s\n", ss, i, buf);
		phydm_print_hist_2_buf(dm, snr_hist, h_size, buf, buf_size);
		PHYDM_DBG(dm, DBG_CMN, "[%d-SS][SNR][%d]=%s\n",  ss, i, buf);
	}
}

void phydm_show_phy_hitogram(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	char buf[PHYDM_SNPRINT_SIZE] = {0};
	u16 buf_size = PHYDM_SNPRINT_SIZE;
	u16 th_size = PHY_HIST_SIZE - 1;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_CMN, "[PHY Histogram] ==============>\n");
/*@===[Threshold]=============================================================*/
	phydm_print_hist_2_buf(dm, dbg_i->evm_hist_th, th_size, buf, buf_size);
	PHYDM_DBG(dm, DBG_CMN, "%-16s=%s\n", "[EVM_TH]", buf);

	phydm_print_hist_2_buf(dm, dbg_i->snr_hist_th, th_size, buf, buf_size);
	PHYDM_DBG(dm, DBG_CMN, "%-16s=%s\n", "[SNR_TH]", buf);
/*@===[OFDM]==================================================================*/
	if (dbg_s->rssi_ofdm_cnt) {
		phydm_print_hist_2_buf(dm, dbg_s->evm_ofdm_hist, PHY_HIST_SIZE,
				       buf, buf_size);
		PHYDM_DBG(dm, DBG_CMN, "%-14s=%s\n", "[OFDM][EVM]", buf);

		phydm_print_hist_2_buf(dm, dbg_s->snr_ofdm_hist, PHY_HIST_SIZE,
				       buf, buf_size);
		PHYDM_DBG(dm, DBG_CMN, "%-14s=%s\n", "[OFDM][SNR]", buf);
	}
/*@===[1-SS]==================================================================*/
	if (dbg_s->rssi_1ss_cnt)
		phydm_nss_hitogram(dm, PDM_1SS);
/*@===[2-SS]==================================================================*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if ((dm->support_ic_type & PHYDM_IC_ABOVE_2SS) && dbg_s->rssi_2ss_cnt)
		phydm_nss_hitogram(dm, PDM_2SS);
	#endif
/*@===[3-SS]==================================================================*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if ((dm->support_ic_type & PHYDM_IC_ABOVE_3SS) && dbg_s->rssi_3ss_cnt)
		phydm_nss_hitogram(dm, PDM_3SS);
	#endif
/*@===[4-SS]==================================================================*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS && dbg_s->rssi_4ss_cnt)
		phydm_nss_hitogram(dm, PDM_4SS);
	#endif
}

void phydm_avg_phy_val_nss(void *dm_void, u8 nss)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	struct phydm_phystatus_avg *dbg_avg = &dbg_i->phystatus_statistic_avg;
	char *rate_type = NULL;
	u32 *tmp_cnt = NULL;
	u8 *tmp_rssi_avg = NULL;
	u32 *tmp_rssi_sum = NULL;
	u8 *tmp_snr_avg = NULL;
	u32 *tmp_snr_sum = NULL;
	u8 *tmp_evm_avg = NULL;
	u32 *tmp_evm_sum = NULL;
	u8 evm_rpt_show[RF_PATH_MEM_SIZE];
	u8 i = 0;

	odm_memory_set(dm, &evm_rpt_show[0], 0, RF_PATH_MEM_SIZE);

	switch (nss) {
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	case 4:
		rate_type = "[4-SS]";
		tmp_cnt = &dbg_s->rssi_4ss_cnt;
		tmp_rssi_avg = &dbg_avg->rssi_4ss_avg[0];
		tmp_snr_avg = &dbg_avg->snr_4ss_avg[0];
		tmp_rssi_sum = &dbg_s->rssi_4ss_sum[0];
		tmp_snr_sum = &dbg_s->snr_4ss_sum[0];
		tmp_evm_avg = &dbg_avg->evm_4ss_avg[0];
		tmp_evm_sum = &dbg_s->evm_4ss_sum[0];
		break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	case 3:
		rate_type = "[3-SS]";
		tmp_cnt = &dbg_s->rssi_3ss_cnt;
		tmp_rssi_avg = &dbg_avg->rssi_3ss_avg[0];
		tmp_snr_avg = &dbg_avg->snr_3ss_avg[0];
		tmp_rssi_sum = &dbg_s->rssi_3ss_sum[0];
		tmp_snr_sum = &dbg_s->snr_3ss_sum[0];
		tmp_evm_avg = &dbg_avg->evm_3ss_avg[0];
		tmp_evm_sum = &dbg_s->evm_3ss_sum[0];
		break;
	#endif
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	case 2:
		rate_type = "[2-SS]";
		tmp_cnt = &dbg_s->rssi_2ss_cnt;
		tmp_rssi_avg = &dbg_avg->rssi_2ss_avg[0];
		tmp_snr_avg = &dbg_avg->snr_2ss_avg[0];
		tmp_rssi_sum = &dbg_s->rssi_2ss_sum[0];
		tmp_snr_sum = &dbg_s->snr_2ss_sum[0];
		tmp_evm_avg = &dbg_avg->evm_2ss_avg[0];
		tmp_evm_sum = &dbg_s->evm_2ss_sum[0];
		break;
	#endif
	case 1:
		rate_type = "[1-SS]";
		tmp_cnt = &dbg_s->rssi_1ss_cnt;
		tmp_rssi_avg = &dbg_avg->rssi_1ss_avg[0];
		tmp_snr_avg = &dbg_avg->snr_1ss_avg[0];
		tmp_rssi_sum = &dbg_s->rssi_1ss_sum[0];
		tmp_snr_sum = &dbg_s->snr_1ss_sum[0];
		tmp_evm_avg = &dbg_avg->evm_1ss_avg;
		tmp_evm_sum = &dbg_s->evm_1ss_sum;
		break;
	default:
		rate_type = "[L-OFDM]";
		tmp_cnt = &dbg_s->rssi_ofdm_cnt;
		tmp_rssi_avg = &dbg_avg->rssi_ofdm_avg[0];
		tmp_snr_avg = &dbg_avg->snr_ofdm_avg[0];
		tmp_rssi_sum = &dbg_s->rssi_ofdm_sum[0];
		tmp_snr_sum = &dbg_s->snr_ofdm_sum[0];
		tmp_evm_avg = &dbg_avg->evm_ofdm_avg;
		tmp_evm_sum = &dbg_s->evm_ofdm_sum;
		break;
	}

	if (*tmp_cnt != 0) {
		for (i = 0; i < dm->num_rf_path; i++) {
			tmp_rssi_avg[i] = (u8)(tmp_rssi_sum[i] / *tmp_cnt);
			tmp_snr_avg[i] = (u8)(tmp_snr_sum[i] / *tmp_cnt);
		}

		if (nss > 1) {
			for (i = 0; i < nss; i++) {
				tmp_evm_avg[i] = (u8)(tmp_evm_sum[i] /
						      *tmp_cnt);
				evm_rpt_show[i] = tmp_evm_avg[i];
			}
		} else {
			*tmp_evm_avg = (u8)(*tmp_evm_sum / *tmp_cnt);
			evm_rpt_show[0] = *tmp_evm_avg;
		}
	}

#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	PHYDM_DBG(dm, DBG_CMN,
		  "* %-8s Cnt=((%.3d)) RSSI:{%.2d, %.2d, %.2d, %.2d} SNR:{%.2d, %.2d, %.2d, %.2d} EVM:{-%.2d, -%.2d, -%.2d, -%.2d}\n",
		  rate_type, *tmp_cnt,
		  tmp_rssi_avg[0], tmp_rssi_avg[1], tmp_rssi_avg[2],
		  tmp_rssi_avg[3], tmp_snr_avg[0], tmp_snr_avg[1],
		  tmp_snr_avg[2], tmp_snr_avg[3], evm_rpt_show[0],
		  evm_rpt_show[1], evm_rpt_show[2], evm_rpt_show[3]);
#elif (defined(PHYDM_COMPILE_ABOVE_3SS))
	PHYDM_DBG(dm, DBG_CMN,
		  "* %-8s Cnt=((%.3d)) RSSI:{%.2d, %.2d, %.2d} SNR:{%.2d, %.2d, %.2d} EVM:{-%.2d, -%.2d, -%.2d}\n",
		  rate_type, *tmp_cnt,
		  tmp_rssi_avg[0], tmp_rssi_avg[1], tmp_rssi_avg[2],
		  tmp_snr_avg[0], tmp_snr_avg[1], tmp_snr_avg[2],
		  evm_rpt_show[0], evm_rpt_show[1], evm_rpt_show[2]);
#elif (defined(PHYDM_COMPILE_ABOVE_2SS))
	PHYDM_DBG(dm, DBG_CMN,
		  "* %-8s Cnt= ((%.3d)) RSSI:{%.2d, %.2d} SNR:{%.2d, %.2d} EVM:{-%.2d, -%.2d}\n",
		  rate_type, *tmp_cnt,
		  tmp_rssi_avg[0], tmp_rssi_avg[1],
		  tmp_snr_avg[0], tmp_snr_avg[1],
		  evm_rpt_show[0], evm_rpt_show[1]);
#else
	PHYDM_DBG(dm, DBG_CMN,
		  "* %-8s Cnt= ((%.3d)) RSSI:{%.2d} SNR:{%.2d} EVM:{-%.2d}\n",
		  rate_type, *tmp_cnt,
		  tmp_rssi_avg[0], tmp_snr_avg[0], evm_rpt_show[0]);
#endif
}

void phydm_get_avg_phystatus_val(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	struct phydm_phystatus_avg *dbg_avg = &dbg_i->phystatus_statistic_avg;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_CMN, "[PHY Avg] ==============>\n");
	phydm_reset_phystatus_avg(dm);

	/*@===[Beacon]===*/
	if (dbg_s->rssi_beacon_cnt) {
		dbg_avg->rssi_beacon_avg = (u8)(dbg_s->rssi_beacon_sum /
						dbg_s->rssi_beacon_cnt);
	}
	PHYDM_DBG(dm, DBG_CMN, "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
		  "[Beacon]", dbg_s->rssi_beacon_cnt, dbg_avg->rssi_beacon_avg);

	/*@===[CCK]===*/
	if (dbg_s->rssi_cck_cnt) {
		dbg_avg->rssi_cck_avg = (u8)(dbg_s->rssi_cck_sum /
					     dbg_s->rssi_cck_cnt);
	}
	PHYDM_DBG(dm, DBG_CMN, "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
		  "[CCK]", dbg_s->rssi_cck_cnt, dbg_avg->rssi_cck_avg);

#if 1
	for (i = 0; i <= 4; i++) {
		if (i > dm->num_rf_path)
			break;

		phydm_avg_phy_val_nss(dm, i);
	}
#else
	/*@===[OFDM]===*/
	phydm_avg_phy_val_nss(dm, 0);
	/*@===[1-SS]===*/
	phydm_avg_phy_val_nss(dm, 1);
	/*@===[2-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & (PHYDM_IC_ABOVE_2SS))
		phydm_avg_phy_val_nss(dm, 2);
	#endif
	/*@===[3-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if (dm->support_ic_type & (PHYDM_IC_ABOVE_3SS))
		phydm_avg_phy_val_nss(dm, 3);
	#endif
	/*@===[4-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS)
		phydm_avg_phy_val_nss(dm, 4);
	#endif
#endif
}

void phydm_get_phy_statistic(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct cmn_sta_info *sta = dm->phydm_sta_info[dm->one_entry_macid];
	enum channel_width bw;
	u16 avg_phy_rate = 0;
	u16 utility = 0;
	u8 rx_ss = 1;

	avg_phy_rate = phydm_rx_avg_phy_rate(dm);

	if (dm->is_one_entry_only && is_sta_active(sta)) {
		rx_ss = phydm_get_rx_stream_num(dm, sta->mimo_type);
		bw = sta->bw_mode;
		utility = phydm_rx_utility(dm, avg_phy_rate, rx_ss, bw);
	}
	PHYDM_DBG(dm, DBG_CMN, "Avg_rx_rate = %d, rx_utility=( %d / 1000 )\n",
		  avg_phy_rate, utility);

	phydm_rx_rate_distribution(dm);
	phydm_reset_rx_rate_distribution(dm);

	phydm_show_phy_hitogram(dm);
	phydm_get_avg_phystatus_val(dm);
	phydm_reset_phystatus_statistic(dm);
};

void phydm_basic_dbg_msg_linked(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_cfo_track_struct *cfo_t = &dm->dm_cfo_track;
	struct odm_phy_dbg_info *dbg_t = &dm->phy_dbg_info;
	u16 macid, client_cnt = 0;
	u8 rate = 0;
	struct cmn_sta_info *entry = NULL;
	char dbg_buf[PHYDM_SNPRINT_SIZE] = {0};
	struct phydm_cfo_rpt cfo;
	u8 i = 0;

	PHYDM_DBG(dm, DBG_CMN, "ID=((%d)), BW=((%d)), fc=((CH-%d))\n",
		  dm->curr_station_id, 20 << *dm->band_width, *dm->channel);

	#ifdef ODM_IC_11N_SERIES_SUPPORT
	#ifdef PHYDM_PRIMARY_CCA
	if (((*dm->channel <= 14) && (*dm->band_width == CHANNEL_WIDTH_40)) &&
	    (dm->support_ic_type & ODM_IC_11N_SERIES)) {
		PHYDM_DBG(dm, DBG_CMN, "Primary CCA at ((%s SB))\n",
			  ((*dm->sec_ch_offset == SECOND_CH_AT_LSB) ? "U" :
			  "L"));
	}
	#endif
	#endif

	if ((dm->support_ic_type & PHYSTS_2ND_TYPE_IC) ||
	    dm->rx_rate > ODM_RATE11M) {
		PHYDM_DBG(dm, DBG_CMN, "[AGC Idx] {0x%x, 0x%x, 0x%x, 0x%x}\n",
			  dm->ofdm_agc_idx[0], dm->ofdm_agc_idx[1],
			  dm->ofdm_agc_idx[2], dm->ofdm_agc_idx[3]);
	} else {
		PHYDM_DBG(dm, DBG_CMN, "[CCK AGC Idx] {LNA,VGA}={0x%x, 0x%x}\n",
			  dm->cck_lna_idx, dm->cck_vga_idx);
	}

	phydm_print_rate_2_buff(dm, dm->rx_rate, dbg_buf, PHYDM_SNPRINT_SIZE);
	PHYDM_DBG(dm, DBG_CMN, "RSSI:{%d, %d, %d, %d}, RxRate:%s (0x%x)\n",
		  (dm->rssi_a == 0xff) ? 0 : dm->rssi_a,
		  (dm->rssi_b == 0xff) ? 0 : dm->rssi_b,
		  (dm->rssi_c == 0xff) ? 0 : dm->rssi_c,
		  (dm->rssi_d == 0xff) ? 0 : dm->rssi_d,
		  dbg_buf, dm->rx_rate);

	rate = dbg_t->beacon_phy_rate;
	phydm_print_rate_2_buff(dm, rate, dbg_buf, PHYDM_SNPRINT_SIZE);

	PHYDM_DBG(dm, DBG_CMN, "Beacon_cnt=%d, rate_idx=%s (0x%x)\n",
		  dbg_t->num_qry_beacon_pkt, dbg_buf, dbg_t->beacon_phy_rate);

	phydm_get_phy_statistic(dm);

	PHYDM_DBG(dm, DBG_CMN,
		  "rxsc_idx {Legacy, 20, 40, 80} = {%d, %d, %d, %d}\n",
		  dm->rxsc_l, dm->rxsc_20, dm->rxsc_40, dm->rxsc_80);

	/*Print TX rate*/
	for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
		entry = dm->phydm_sta_info[macid];

		if (!is_sta_active(entry))
			continue;

		rate = entry->ra_info.curr_tx_rate;
		phydm_print_rate_2_buff(dm, rate, dbg_buf, PHYDM_SNPRINT_SIZE);
		PHYDM_DBG(dm, DBG_CMN, "TxRate[%d]=%s (0x%x)\n",
			  macid, dbg_buf, entry->ra_info.curr_tx_rate);

		client_cnt++;

		if (client_cnt >= dm->number_linked_client)
			break;
	}

	PHYDM_DBG(dm, DBG_CMN,
		  "TP {Tx, Rx, Total} = {%d, %d, %d}Mbps, Traffic_Load=(%d))\n",
		  dm->tx_tp, dm->rx_tp, dm->total_tp, dm->traffic_load);

	PHYDM_DBG(dm, DBG_CMN, "CFO_avg=((%d kHz)), CFO_traking = ((%s%d))\n",
		  cfo_t->CFO_ave_pre,
		  ((cfo_t->crystal_cap > cfo_t->def_x_cap) ? "+" : "-"),
		  DIFF_2(cfo_t->crystal_cap, cfo_t->def_x_cap));

	/* @CFO report */
	switch (dm->ic_ip_series) {
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	case PHYDM_IC_JGR3:
		PHYDM_DBG(dm, DBG_CMN, "cfo_tail = {%d, %d, %d, %d}\n",
			  dbg_t->cfo_tail[0], dbg_t->cfo_tail[1],
			  dbg_t->cfo_tail[2], dbg_t->cfo_tail[3]);
		break;
	#endif
	default:
		phydm_get_cfo_info(dm, &cfo);
		for (i = 0; i < dm->num_rf_path; i++) {
			PHYDM_DBG(dm, DBG_CMN,
				  "CFO[%d] {S, L, Sec, Acq, End} = {%d, %d, %d, %d, %d}\n",
				  i, cfo.cfo_rpt_s[i], cfo.cfo_rpt_l[i],
				  cfo.cfo_rpt_sec[i], cfo.cfo_rpt_acq[i],
				  cfo.cfo_rpt_end[i]);
		}
		break;
	}

/* @Condition number */
#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type == ODM_RTL8822B) {
		PHYDM_DBG(dm, DBG_CMN, "Condi_Num=((%d.%.4d)), %d\n",
			  dbg_t->condi_num >> 4,
			  phydm_show_fraction_num(dbg_t->condi_num & 0xf, 4),
			  dbg_t->condi_num);
	}
#endif
#ifdef PHYSTS_3RD_TYPE_SUPPORT
	if (dm->support_ic_type & PHYSTS_3RD_TYPE_IC) {
		PHYDM_DBG(dm, DBG_CMN, "Condi_Num=((%d.%4d dB))\n",
			  dbg_t->condi_num >> 1,
			  phydm_show_fraction_num(dbg_t->condi_num & 0x1, 1));
	}
#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT || defined(PHYSTS_3RD_TYPE_SUPPORT))
	/*STBC or LDPC pkt*/
	if (dm->support_ic_type & (PHYSTS_2ND_TYPE_IC | PHYSTS_3RD_TYPE_IC))
		PHYDM_DBG(dm, DBG_CMN, "Coding: LDPC=((%s)), STBC=((%s))\n",
			  (dbg_t->is_ldpc_pkt) ? "Y" : "N",
			  (dbg_t->is_stbc_pkt) ? "Y" : "N");
#endif
}

void phydm_dm_summary(void *dm_void, u8 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct phydm_cfo_track_struct *cfo_t = &dm->dm_cfo_track;
	struct cmn_sta_info *sta = NULL;
	struct ra_sta_info *ra = NULL;
	struct dtp_info *dtp = NULL;
	u64 comp = dm->support_ability;
	u64 pause_comp = dm->pause_ability;

	if (!(dm->debug_components & DBG_DM_SUMMARY))
		return;

	if (!dm->is_linked) {
		pr_debug("[%s]No Link !!!\n", __func__);
		return;
	}

	sta = dm->phydm_sta_info[macid];

	if (!is_sta_active(sta)) {
		pr_debug("[Warning] %s invalid STA, macid=%d\n",
			 __func__, macid);
		return;
	}

	ra = &sta->ra_info;
	dtp = &sta->dtp_stat;
	pr_debug("[%s]===========>\n", __func__);

	pr_debug("00.(%s) %-12s: IGI=0x%x, Dyn_Rng=0x%x~0x%x, FA_th={%d,%d,%d}\n",
		 ((comp & ODM_BB_DIG) ?
		 ((pause_comp & ODM_BB_DIG) ? "P" : "V") : "."),
		 "DIG",
		 dig_t->cur_ig_value,
		 dig_t->rx_gain_range_min, dig_t->rx_gain_range_max,
		 dig_t->fa_th[0], dig_t->fa_th[1], dig_t->fa_th[2]);

	pr_debug("01.(%s) %-12s: rssi_lv=%d, mask=0x%llx\n",
		 ((comp & ODM_BB_RA_MASK) ?
		 ((pause_comp & ODM_BB_RA_MASK) ? "P" : "V") : "."),
		 "RaMask",
		 ra->rssi_level, ra->ramask);

#ifdef CONFIG_DYNAMIC_TX_TWR
	pr_debug("02.(%s) %-12s: pwr_lv=%d\n",
		 ((comp & ODM_BB_DYNAMIC_TXPWR) ?
		 ((pause_comp & ODM_BB_DYNAMIC_TXPWR) ? "P" : "V") : "."),
		 "DynTxPwr",
		 dtp->sta_tx_high_power_lvl);
#endif

	pr_debug("05.(%s) %-12s: cck_pd_lv=%d\n",
		 ((comp & ODM_BB_CCK_PD) ?
		 ((pause_comp & ODM_BB_CCK_PD) ? "P" : "V") : "."),
		 "CCK_PD", dm->dm_cckpd_table.cck_pd_lv);

#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	pr_debug("06.(%s) %-12s: div_type=%d, curr_ant=%s\n",
		 ((comp & ODM_BB_ANT_DIV) ?
		 ((pause_comp & ODM_BB_ANT_DIV) ? "P" : "V") : "."),
		 "ANT_DIV",
		 dm->ant_div_type,
		 (dm->dm_fat_table.rx_idle_ant == MAIN_ANT) ? "MAIN" : "AUX");
#endif

#ifdef PHYDM_POWER_TRAINING_SUPPORT
	pr_debug("08.(%s) %-12s: PT_score=%d, disable_PT=%d\n",
		 ((comp & ODM_BB_PWR_TRAIN) ?
		 ((pause_comp & ODM_BB_PWR_TRAIN) ? "P" : "V") : "."),
		 "PwrTrain",
		 dm->pow_train_table.pow_train_score,
		 dm->is_disable_power_training);
#endif

#ifdef CONFIG_PHYDM_DFS_MASTER
	pr_debug("11.(%s) %-12s: dbg_mode=%d, region_domain=%d\n",
		 ((comp & ODM_BB_DFS) ?
		 ((pause_comp & ODM_BB_DFS) ? "P" : "V") : "."),
		 "DFS",
		 dm->dfs.dbg_mode, dm->dfs_region_domain);
#endif
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	pr_debug("13.(%s) %-12s: th{l2h, h2l}={%d, %d}, edcca_flag=%d\n",
		 ((comp & ODM_BB_ADAPTIVITY) ?
		 ((pause_comp & ODM_BB_ADAPTIVITY) ? "P" : "V") : "."),
		 "Adaptivity",
		 dm->adaptivity.th_l2h, dm->adaptivity.th_h2l,
		 dm->false_alm_cnt.edcca_flag);
#endif
	pr_debug("14.(%s) %-12s: CFO_avg=%d kHz, CFO_traking=%s%d\n",
		 ((comp & ODM_BB_CFO_TRACKING) ?
		 ((pause_comp & ODM_BB_CFO_TRACKING) ? "P" : "V") : "."),
		 "CfoTrack",
		 cfo_t->CFO_ave_pre,
		 ((cfo_t->crystal_cap > cfo_t->def_x_cap) ? "+" : "-"),
		 DIFF_2(cfo_t->crystal_cap, cfo_t->def_x_cap));

	pr_debug("15.(%s) %-12s: ratio{nhm, clm}={%d, %d}\n",
		 ((comp & ODM_BB_ENV_MONITOR) ?
		 ((pause_comp & ODM_BB_ENV_MONITOR) ? "P" : "V") : "."),
		 "EnvMntr",
		 dm->dm_ccx_info.nhm_ratio, dm->dm_ccx_info.clm_ratio);

#ifdef PHYDM_PRIMARY_CCA
	pr_debug("16.(%s) %-12s: CCA @ (%s SB)\n",
		 ((comp & ODM_BB_PRIMARY_CCA) ?
		 ((pause_comp & ODM_BB_PRIMARY_CCA) ? "P" : "V") : "."),
		 "PriCCA",
		 ((dm->dm_pri_cca.mf_state == MF_USC_LSC) ? "D" :
		 ((dm->dm_pri_cca.mf_state == MF_LSC) ? "L" : "U")));
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	pr_debug("17.(%s) %-12s: soml_en = %s\n",
		 ((comp & ODM_BB_ADAPTIVE_SOML) ?
		 ((pause_comp & ODM_BB_ADAPTIVE_SOML) ? "P" : "V") : "."),
		 "A-SOML",
		 (dm->dm_soml_table.soml_last_state == SOML_ON) ?
		 "ON" : "OFF");
#endif
#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
	pr_debug("18.(%s) %-12s:\n",
		 ((comp & ODM_BB_LNA_SAT_CHK) ?
		 ((pause_comp & ODM_BB_LNA_SAT_CHK) ? "P" : "V") : "."),
		 "LNA_SAT_CHK");
#endif
}

void phydm_basic_dbg_message(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_fa_struct *fa_t = &dm->false_alm_cnt;

	#ifdef ROKU_PRIVATE
	/*if (!(dm->debug_components & DBG_CMN))*/
	/*	return;				*/
	# else
	if (!(dm->debug_components & DBG_CMN))
		return;
	#endif /*ROKU_PRIVATE*/

	if (dm->cmn_dbg_msg_cnt >= dm->cmn_dbg_msg_period) {
		dm->cmn_dbg_msg_cnt = PHYDM_WATCH_DOG_PERIOD;
	} else {
		dm->cmn_dbg_msg_cnt += PHYDM_WATCH_DOG_PERIOD;
		return;
	}

	PHYDM_DBG(dm, DBG_CMN, "[%s] System up time: ((%d sec))---->\n",
		  __func__, dm->phydm_sys_up_time);

	if (dm->is_linked)
		phydm_basic_dbg_msg_linked(dm);
	else
		PHYDM_DBG(dm, DBG_CMN, "No Link !!!\n");

	PHYDM_DBG(dm, DBG_CMN, "[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		  fa_t->cnt_cck_cca, fa_t->cnt_ofdm_cca, fa_t->cnt_cca_all);

	PHYDM_DBG(dm, DBG_CMN, "[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		  fa_t->cnt_cck_fail, fa_t->cnt_ofdm_fail, fa_t->cnt_all);

	PHYDM_DBG(dm, DBG_CMN,
		  "[OFDM FA Detail] Parity_Fail=%d, Rate_Illegal=%d, CRC8=%d, MCS_fail=%d, Fast_sync=%d, SB_Search_fail=%d\n",
		  fa_t->cnt_parity_fail, fa_t->cnt_rate_illegal,
		  fa_t->cnt_crc8_fail, fa_t->cnt_mcs_fail,
		  fa_t->cnt_fast_fsync, fa_t->cnt_sb_search_fail);

#if (ODM_IC_11AC_SERIES_SUPPORT || defined(PHYDM_IC_JGR3_SERIES_SUPPORT))
	if (dm->support_ic_type & (ODM_IC_11AC_SERIES | ODM_IC_JGR3_SERIES)) {
		PHYDM_DBG(dm, DBG_CMN,
			  "[OFDM FA Detail VHT] CRC8_VHT=%d, MCS_Fail_VHT=%d\n",
			  fa_t->cnt_crc8_fail_vht, fa_t->cnt_mcs_fail_vht);
	}
#endif

	PHYDM_DBG(dm, DBG_CMN,
		  "is_linked = %d, Num_client = %d, rssi_min = %d, IGI = 0x%x, bNoisy=%d\n\n",
		  dm->is_linked, dm->number_linked_client, dm->rssi_min,
		  dm->dm_dig_table.cur_ig_value, dm->noisy_decision);
}

void phydm_basic_profile(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char *cut = NULL;
	char *ic_type = NULL;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 date = 0;
	char *commit_by = NULL;
	u32 release_ver = 0;

	PDM_SNPF(out_len, used, output + used, out_len - used, "%-35s\n",
		 "% Basic Profile %");

	if (dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT)
		ic_type = "RTL8188E";
		date = RELEASE_DATE_8188E;
		commit_by = COMMIT_BY_8188E;
		release_ver = RELEASE_VERSION_8188E;
#endif
#if (RTL8812A_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8812) {
		ic_type = "RTL8812A";
		date = RELEASE_DATE_8812A;
		commit_by = COMMIT_BY_8812A;
		release_ver = RELEASE_VERSION_8812A;
#endif
#if (RTL8821A_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8821) {
		ic_type = "RTL8821A";
		date = RELEASE_DATE_8821A;
		commit_by = COMMIT_BY_8821A;
		release_ver = RELEASE_VERSION_8821A;
#endif
#if (RTL8192E_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8192E) {
		ic_type = "RTL8192E";
		date = RELEASE_DATE_8192E;
		commit_by = COMMIT_BY_8192E;
		release_ver = RELEASE_VERSION_8192E;
#endif
#if (RTL8723B_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8723B) {
		ic_type = "RTL8723B";
		date = RELEASE_DATE_8723B;
		commit_by = COMMIT_BY_8723B;
		release_ver = RELEASE_VERSION_8723B;
#endif
#if (RTL8814A_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8814A) {
		ic_type = "RTL8814A";
		date = RELEASE_DATE_8814A;
		commit_by = COMMIT_BY_8814A;
		release_ver = RELEASE_VERSION_8814A;
#endif
#if (RTL8881A_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8881A) {
		ic_type = "RTL8881A";
#endif
#if (RTL8822B_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8822B) {
		ic_type = "RTL8822B";
		date = RELEASE_DATE_8822B;
		commit_by = COMMIT_BY_8822B;
		release_ver = RELEASE_VERSION_8822B;
#endif
#if (RTL8197F_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8197F) {
		ic_type = "RTL8197F";
		date = RELEASE_DATE_8197F;
		commit_by = COMMIT_BY_8197F;
		release_ver = RELEASE_VERSION_8197F;
#endif
#if (RTL8703B_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8703B) {
		ic_type = "RTL8703B";
		date = RELEASE_DATE_8703B;
		commit_by = COMMIT_BY_8703B;
		release_ver = RELEASE_VERSION_8703B;
#endif
#if (RTL8195A_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8195A) {
		ic_type = "RTL8195A";
#endif
#if (RTL8188F_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8188F) {
		ic_type = "RTL8188F";
		date = RELEASE_DATE_8188F;
		commit_by = COMMIT_BY_8188F;
		release_ver = RELEASE_VERSION_8188F;
#endif
#if (RTL8723D_SUPPORT)
	} else if (dm->support_ic_type == ODM_RTL8723D) {
		ic_type = "RTL8723D";
		date = RELEASE_DATE_8723D;
		commit_by = COMMIT_BY_8723D;
		release_ver = RELEASE_VERSION_8723D;
#endif
	}

/* @JJ ADD 20161014 */
#if (RTL8710B_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8710B) {
		ic_type = "RTL8710B";
		date = RELEASE_DATE_8710B;
		commit_by = COMMIT_BY_8710B;
		release_ver = RELEASE_VERSION_8710B;
	}
#endif

#if (RTL8721D_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8721D) {
		ic_type = "RTL8721D";
		date = RELEASE_DATE_8721D;
		commit_by = COMMIT_BY_8721D;
		release_ver = RELEASE_VERSION_8721D;
	}
#endif
#if (RTL8821C_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8821C) {
		ic_type = "RTL8821C";
		date = RELEASE_DATE_8821C;
		commit_by = COMMIT_BY_8821C;
		release_ver = RELEASE_VERSION_8821C;
	}
#endif

/*@jj add 20170822*/
#if (RTL8192F_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8192F) {
		ic_type = "RTL8192F";
		date = RELEASE_DATE_8192F;
		commit_by = COMMIT_BY_8192F;
		release_ver = RELEASE_VERSION_8192F;
	}
#endif

#if (RTL8198F_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8198F) {
		ic_type = "RTL8198F";
		date = RELEASE_DATE_8198F;
		commit_by = COMMIT_BY_8198F;
		release_ver = RELEASE_VERSION_8198F;
	}
#endif

#if (RTL8822C_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8822C) {
		ic_type = "RTL8822C";
		date = RELEASE_DATE_8822C;
		commit_by = COMMIT_BY_8822C;
		release_ver = RELEASE_VERSION_8822C;
	}
#endif

#if (RTL8812F_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8812F) {
		ic_type = "RTL8812F";
		date = RELEASE_DATE_8812F;
		commit_by = COMMIT_BY_8812F;
		release_ver = RELEASE_VERSION_8812F;
	}
#endif

#if (RTL8197G_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8197G) {
		ic_type = "RTL8197G";
		date = RELEASE_DATE_8197G;
		commit_by = COMMIT_BY_8197G;
		release_ver = RELEASE_VERSION_8197G;
	}
#endif

#if (RTL8814B_SUPPORT)
	else if (dm->support_ic_type == ODM_RTL8814B) {
		ic_type = "RTL8814B";
		date = RELEASE_DATE_8814B;
		commit_by = COMMIT_BY_8814B;
		release_ver = RELEASE_VERSION_8814B;
	}
#endif

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "  %-35s: %s (MP Chip: %s)\n", "IC type", ic_type,
		 dm->is_mp_chip ? "Yes" : "No");

	if (dm->cut_version == ODM_CUT_A)
		cut = "A";
	else if (dm->cut_version == ODM_CUT_B)
		cut = "B";
	else if (dm->cut_version == ODM_CUT_C)
		cut = "C";
	else if (dm->cut_version == ODM_CUT_D)
		cut = "D";
	else if (dm->cut_version == ODM_CUT_E)
		cut = "E";
	else if (dm->cut_version == ODM_CUT_F)
		cut = "F";
	else if (dm->cut_version == ODM_CUT_G)
		cut = "G";
	else if (dm->cut_version == ODM_CUT_H)
		cut = "H";
	else if (dm->cut_version == ODM_CUT_I)
		cut = "I";
	else if (dm->cut_version == ODM_CUT_J)
		cut = "J";
	else if (dm->cut_version == ODM_CUT_K)
		cut = "K";
	else if (dm->cut_version == ODM_CUT_L)
		cut = "L";
	else if (dm->cut_version == ODM_CUT_M)
		cut = "M";
	else if (dm->cut_version == ODM_CUT_N)
		cut = "N";
	else if (dm->cut_version == ODM_CUT_O)
		cut = "O";
	else if (dm->cut_version == ODM_CUT_TEST)
		cut = "TEST";
	else
		cut = "UNKNOWN";

	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %d\n",
		 "RFE type", dm->rfe_type);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Cut Ver", cut);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %d\n",
		 "PHY Para Ver", odm_get_hw_img_version(dm));
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %d\n",
		 "PHY Para Commit date", date);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "PHY Para Commit by", commit_by);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %d\n",
		 "PHY Para Release Ver", release_ver);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "  %-35s: %d (Subversion: %d)\n", "FW Ver", dm->fw_version,
		 dm->fw_sub_version);

	/* @1 PHY DM version List */
	PDM_SNPF(out_len, used, output + used, out_len - used, "%-35s\n",
		 "% PHYDM version %");
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Code base", PHYDM_CODE_BASE);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Release Date", PHYDM_RELEASE_DATE);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Adaptivity", ADAPTIVITY_VERSION);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "DIG", DIG_VERSION);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "CFO Tracking", CFO_TRACKING_VERSION);
#ifdef CONFIG_PHYDM_ANTENNA_DIVERSITY
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "AntDiv", ANTDIV_VERSION);
#endif
#ifdef CONFIG_DYNAMIC_TX_TWR
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Dynamic TxPower", DYNAMIC_TXPWR_VERSION);
#endif
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "RA Info", RAINFO_VERSION);
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "AntDetect", ANTDECT_VERSION);
#endif
#ifdef CONFIG_PATH_DIVERSITY
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "PathDiv", PATHDIV_VERSION);
#endif
#ifdef CONFIG_ADAPTIVE_SOML
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Adaptive SOML", ADAPTIVE_SOML_VERSION);
#endif
#if (PHYDM_LA_MODE_SUPPORT)
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "LA mode", DYNAMIC_LA_MODE);
#endif
#ifdef PHYDM_PRIMARY_CCA
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Primary CCA", PRIMARYCCA_VERSION);
#endif
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "DFS", DFS_VERSION);

#if (RTL8822B_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8822B)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "  %-35s: %s\n", "PHY config 8822B",
			 PHY_CONFIG_VERSION_8822B);

#endif
#if (RTL8197F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8197F)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "  %-35s: %s\n", "PHY config 8197F",
			 PHY_CONFIG_VERSION_8197F);
#endif

/*@jj add 20170822*/
#if (RTL8192F_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8192F)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "  %-35s: %s\n", "PHY config 8192F",
			 PHY_CONFIG_VERSION_8192F);
#endif
#if (RTL8721D_SUPPORT)
	if (dm->support_ic_type & ODM_RTL8721D)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "  %-35s: %s\n", "PHY config 8721D",
			 PHY_CONFIG_VERSION_8721D);
#endif

	*_used = used;
	*_out_len = out_len;

#endif /*@#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
void phydm_fw_trace_en_h2c(void *dm_void, boolean enable,
			   u32 fw_dbg_comp, u32 monitor_mode, u32 macid)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 h2c_parameter[7] = {0};
	u8 cmd_length;

	if (dm->support_ic_type & PHYDM_IC_3081_SERIES) {
		h2c_parameter[0] = enable;
		h2c_parameter[1] = (u8)(fw_dbg_comp & MASKBYTE0);
		h2c_parameter[2] = (u8)((fw_dbg_comp & MASKBYTE1) >> 8);
		h2c_parameter[3] = (u8)((fw_dbg_comp & MASKBYTE2) >> 16);
		h2c_parameter[4] = (u8)((fw_dbg_comp & MASKBYTE3) >> 24);
		h2c_parameter[5] = (u8)monitor_mode;
		h2c_parameter[6] = (u8)macid;
		cmd_length = 7;

	} else {
		h2c_parameter[0] = enable;
		h2c_parameter[1] = (u8)monitor_mode;
		h2c_parameter[2] = (u8)macid;
		cmd_length = 3;
	}

	PHYDM_DBG(dm, DBG_FW_TRACE,
		  "[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n",
		  enable, monitor_mode, macid);

	odm_fill_h2c_cmd(dm, PHYDM_H2C_FW_TRACE_EN, cmd_length, h2c_parameter);
}

void phydm_get_per_path_txagc(void *dm_void, u8 path, u32 *_used, char *output,
			      u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 rate_idx = 0;
	u8 txagc = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

#ifdef PHYDM_COMMON_API_SUPPORT
	if (!(dm->support_ic_type & CMN_API_SUPPORT_IC))
		return;

	if (dm->num_rf_path == 1 && path > RF_PATH_A)
		return;
	else if (dm->num_rf_path == 2 && path > RF_PATH_B)
		return;
	else if (dm->num_rf_path == 3 && path > RF_PATH_C)
		return;
	else if (dm->num_rf_path == 4 && path > RF_PATH_D)
		return;

	for (rate_idx = 0; rate_idx <= 0x53; rate_idx++) {
		if (!(dm->support_ic_type & PHYDM_IC_ABOVE_3SS) &&
		    ((rate_idx >= ODM_RATEMCS16 &&
		    rate_idx < ODM_RATEVHTSS1MCS0) ||
		    rate_idx >= ODM_RATEVHTSS3MCS0))
			continue;

		if (rate_idx == ODM_RATE1M)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "  %-35s\n", "CCK====>");
		else if (rate_idx == ODM_RATE6M)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "OFDM====>");
		else if (rate_idx == ODM_RATEMCS0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "HT 1ss====>");
		else if (rate_idx == ODM_RATEMCS8)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "HT 2ss====>");
		else if (rate_idx == ODM_RATEMCS16)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "HT 3ss====>");
		else if (rate_idx == ODM_RATEMCS24)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "HT 4ss====>");
		else if (rate_idx == ODM_RATEVHTSS1MCS0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "VHT 1ss====>");
		else if (rate_idx == ODM_RATEVHTSS2MCS0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "VHT 2ss====>");
		else if (rate_idx == ODM_RATEVHTSS3MCS0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "VHT 3ss====>");
		else if (rate_idx == ODM_RATEVHTSS4MCS0)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\n  %-35s\n", "VHT 4ss====>");

		txagc = phydm_api_get_txagc(dm, (enum rf_path)path, rate_idx);
		if (config_phydm_read_txagc_check(txagc))
			PDM_SNPF(out_len, used, output + used,
				 out_len - used, "  0x%02x    ", txagc);
		else
			PDM_SNPF(out_len, used, output + used,
				 out_len - used, "  0x%s    ", "xx");
	}
#endif

	*_used = used;
	*_out_len = out_len;
}

void phydm_get_txagc(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* path-A */
	PDM_SNPF(out_len, used, output + used, out_len - used, "%-35s\n",
		 "path-A====================");
	phydm_get_per_path_txagc(dm, RF_PATH_A, &used, output, &out_len);

	/* path-B */
	PDM_SNPF(out_len, used, output + used, out_len - used, "\n%-35s\n",
		 "path-B====================");
	phydm_get_per_path_txagc(dm, RF_PATH_B, &used, output, &out_len);

	/* path-C */
	PDM_SNPF(out_len, used, output + used, out_len - used, "\n%-35s\n",
		 "path-C====================");
	phydm_get_per_path_txagc(dm, RF_PATH_C, &used, output, &out_len);

	/* path-D */
	PDM_SNPF(out_len, used, output + used, out_len - used, "\n%-35s\n",
		 "path-D====================");
	phydm_get_per_path_txagc(dm, RF_PATH_D, &used, output, &out_len);

	*_used = used;
	*_out_len = out_len;
}

void phydm_set_txagc(void *dm_void, u32 *const val, u32 *_used,
		     char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;
	u32 pow = 0; /*power index*/
	u8 vht_start_rate = ODM_RATEVHTSS1MCS0;
	boolean rpt = true;
	enum rf_path path = RF_PATH_A;

/*@val[1] = path*/
/*@val[2] = hw_rate*/
/*@val[3] = power_index*/

#ifdef PHYDM_COMMON_API_SUPPORT
	if (!(dm->support_ic_type & CMN_API_SUPPORT_IC))
		return;

	path = (enum rf_path)val[1];

	if (val[1] >= dm->num_rf_path) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Write path-%d rate_idx-0x%x fail\n", val[1], val[2]);
	} else if ((u8)val[2] != 0xff) {
		if (phydm_api_set_txagc(dm, val[3], path, (u8)val[2], true))
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Write path-%d rate_idx-0x%x = 0x%x\n",
				 val[1], val[2], val[3]);
		else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Write path-%d rate index-0x%x fail\n",
				 val[1], val[2]);
	} else {

		if (dm->support_ic_type &
		    (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8195B)) {
			pow = (val[3] & 0x3f);
			pow = BYTE_DUPLICATE_2_DWORD(pow);

			for (i = 0; i < ODM_RATEVHTSS2MCS9; i += 4)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
		} else if (dm->support_ic_type &
			   (ODM_RTL8197F | ODM_RTL8192F)) {
			pow = (val[3] & 0x3f);
			for (i = 0; i <= ODM_RATEMCS15; i++)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
		} else if (dm->support_ic_type & ODM_RTL8198F) {
			pow = (val[3] & 0x7f);
			for (i = 0; i <= ODM_RATEVHTSS4MCS9; i++)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
		} else if (dm->support_ic_type &
			   (ODM_RTL8822C | ODM_RTL8812F | ODM_RTL8197G)) {
			pow = (val[3] & 0x7f);
			for (i = 0; i <= ODM_RATEMCS15; i++)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
			for (i = vht_start_rate; i <= ODM_RATEVHTSS2MCS9; i++)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
		} else if (dm->support_ic_type &
			   (ODM_RTL8721D)) {
			pow = (val[3] & 0x3f);
			for (i = 0; i <= ODM_RATEMCS7; i++)
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 0);
		}

		if (rpt)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Write all TXAGC of path-%d = 0x%x\n",
				 val[1], val[3]);
		else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Write all TXAGC of path-%d fail\n", val[1]);
	}

#endif
	*_used = used;
	*_out_len = out_len;
}

void phydm_shift_txagc(void *dm_void, u32 *const val, u32 *_used, char *output,
		       u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i = 0;
	u32 pow = 0; /*Power index*/
	boolean rpt = true;
	u8 vht_start_rate = ODM_RATEVHTSS1MCS0;
	enum rf_path path = RF_PATH_A;

#ifdef PHYDM_COMMON_API_SUPPORT
	if (!(dm->support_ic_type & CMN_API_SUPPORT_IC))
		return;

	if (val[1] >= dm->num_rf_path) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Write path-%d fail\n", val[1]);
		return;
	}

	path = (enum rf_path)val[1];

	if ((u8)val[2] == 0) {
	/*@{0:-, 1:+} {Pwr Offset}*/
		if (dm->support_ic_type & (ODM_RTL8195B | ODM_RTL8821C)) {
			for (i = 0; i <= ODM_RATEMCS7; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
			for (i = vht_start_rate; i <= ODM_RATEVHTSS1MCS9; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type & (ODM_RTL8822B)) {
			for (i = 0; i <= ODM_RATEMCS15; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
			for (i = vht_start_rate; i <= ODM_RATEVHTSS2MCS9; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type &
			   (ODM_RTL8197F | ODM_RTL8192F)) {
			for (i = 0; i <= ODM_RATEMCS15; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			rpt &= phydm_api_shift_txagc(dm, val[3], path, 0);
		} else if (dm->support_ic_type &
			   (ODM_RTL8721D)) {
			for (i = 0; i <= ODM_RATEMCS7; i++) {
				pow = phydm_api_get_txagc(dm, path, i) - val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		}
	} else if ((u8)val[2] == 1) {
	/*@{0:-, 1:+} {Pwr Offset}*/
		if (dm->support_ic_type & (ODM_RTL8195B | ODM_RTL8821C)) {
			for (i = 0; i <= ODM_RATEMCS7; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
			for (i = vht_start_rate; i <= ODM_RATEVHTSS1MCS9; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type & (ODM_RTL8822B)) {
			for (i = 0; i <= ODM_RATEMCS15; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
			for (i = vht_start_rate; i <= ODM_RATEVHTSS2MCS9; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type &
			   (ODM_RTL8197F | ODM_RTL8192F)) {
			for (i = 0; i <= ODM_RATEMCS15; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type & ODM_RTL8721D) {
			for (i = 0; i <= ODM_RATEMCS7; i++) {
				pow = phydm_api_get_txagc(dm, path, i) + val[3];
				rpt &= phydm_api_set_txagc(dm, pow, path, i, 1);
			}
		} else if (dm->support_ic_type &
			   (ODM_RTL8822C | ODM_RTL8812F | ODM_RTL8197G)) {
			rpt &= phydm_api_shift_txagc(dm, val[3], path, 1);
		}
	}
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[All rate] Set Path-%d Pow_idx: %s %d\n",
			 val[1], (val[2] ? "+" : "-"), val[3]);
	else
	#endif
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[All rate] Set Path-%d Pow_idx: %s %d(%d.%s dB)\n",
			 val[1], (val[2] ? "+" : "-"), val[3], val[3] >> 1,
			 ((val[3] & 1) ? "5" : "0"));

#endif
	*_used = used;
	*_out_len = out_len;
}

void phydm_set_txagc_dbg(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 var1[10] = {0};
	char help[] = "-h";
	u8 i = 0, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
			input_idx++;
		}
	}

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{Dis:0, En:1} {pathA~D(0~3)} {rate_idx(Hex), All_rate:0xff} {txagc_idx (Hex)}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{Pwr Shift(All rate):2} {pathA~D(0~3)} {0:-, 1:+} {Pwr Offset(Hex)}\n");
	} else if (var1[0] == 0) {
		dm->is_disable_phy_api = false;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable API debug mode\n");
	} else if (var1[0] == 1) {
		dm->is_disable_phy_api = false;
		#ifdef CONFIG_TXAGC_DEBUG_8822C
		config_phydm_write_txagc_8822c(dm, var1[3],
					       (enum rf_path)var1[1],
					       (u8)var1[2]);
		#else
		phydm_set_txagc(dm, (u32 *)var1, &used, output, &out_len);
		#endif
		dm->is_disable_phy_api = true;
	} else if (var1[0] == 2) {
		PHYDM_SSCANF(input[4], DCMD_HEX, &var1[3]);
		dm->is_disable_phy_api = false;
		phydm_shift_txagc(dm, (u32 *)var1, &used, output, &out_len);
		dm->is_disable_phy_api = true;
	}
	#ifdef CONFIG_TXAGC_DEBUG_8822C
	else if (var1[0] == 3) {
		dm->is_disable_phy_api = false;
		phydm_txagc_tab_buff_show_8822c(dm);
		dm->is_disable_phy_api = true;
	} else if (var1[0] == 4) {
		dm->is_disable_phy_api = false;
		config_phydm_set_txagc_to_hw_8822c(dm);
		dm->is_disable_phy_api = true;
	}
	#endif

	*_used = used;
	*_out_len = out_len;
}

void phydm_debug_trace(void *dm_void, char input[][16], u32 *_used,
		       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u64 pre_debug_components, one = 1;
	u64 comp = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	u8 i = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &val[i]);
	}
	comp = dm->debug_components;
	pre_debug_components = dm->debug_components;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\n================================\n");
	if (val[0] == 100) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[DBG MSG] Component Selection\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "00. (( %s ))DIG\n",
			 ((comp & DBG_DIG) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "01. (( %s ))RA_MASK\n",
			 ((comp & DBG_RA_MASK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "02. (( %s ))DYN_TXPWR\n",
			 ((comp & DBG_DYN_TXPWR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "03. (( %s ))FA_CNT\n",
			 ((comp & DBG_FA_CNT) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "04. (( %s ))RSSI_MNTR\n",
			 ((comp & DBG_RSSI_MNTR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "05. (( %s ))CCKPD\n",
			 ((comp & DBG_CCKPD) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "06. (( %s ))ANT_DIV\n",
			 ((comp & DBG_ANT_DIV) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "07. (( %s ))SMT_ANT\n",
			 ((comp & DBG_SMT_ANT) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "08. (( %s ))PWR_TRAIN\n",
			 ((comp & DBG_PWR_TRAIN) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "09. (( %s ))RA\n",
			 ((comp & DBG_RA) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "10. (( %s ))PATH_DIV\n",
			 ((comp & DBG_PATH_DIV) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "11. (( %s ))DFS\n",
			 ((comp & DBG_DFS) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "12. (( %s ))DYN_ARFR\n",
			 ((comp & DBG_DYN_ARFR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "13. (( %s ))ADAPTIVITY\n",
			 ((comp & DBG_ADPTVTY) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "14. (( %s ))CFO_TRK\n",
			 ((comp & DBG_CFO_TRK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "15. (( %s ))ENV_MNTR\n",
			 ((comp & DBG_ENV_MNTR) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "16. (( %s ))PRI_CCA\n",
			 ((comp & DBG_PRI_CCA) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "17. (( %s ))ADPTV_SOML\n",
			 ((comp & DBG_ADPTV_SOML) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "18. (( %s ))LNA_SAT_CHK\n",
			 ((comp & DBG_LNA_SAT_CHK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "20. (( %s ))PHY_STATUS\n",
			 ((comp & DBG_PHY_STATUS) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "21. (( %s ))TMP\n",
			 ((comp & DBG_TMP) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "22. (( %s ))FW_DBG_TRACE\n",
			 ((comp & DBG_FW_TRACE) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "23. (( %s ))TXBF\n",
			 ((comp & DBG_TXBF) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "24. (( %s ))COMMON_FLOW\n",
			 ((comp & DBG_COMMON_FLOW) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "28. (( %s ))PHY_CONFIG\n",
			 ((comp & ODM_PHY_CONFIG) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "29. (( %s ))INIT\n",
			 ((comp & ODM_COMP_INIT) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "30. (( %s ))COMMON\n",
			 ((comp & DBG_CMN) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "31. (( %s ))API\n",
			 ((comp & ODM_COMP_API) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "================================\n");

	} else if (val[0] == 101) {
		dm->debug_components = 0;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable all debug components\n");
	} else {
		if (val[1] == 1) /*@enable*/
			dm->debug_components |= (one << val[0]);
		else if (val[1] == 2) /*@disable*/
			dm->debug_components &= ~(one << val[0]);
		else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "[Warning]  1:on,  2:off\n");

		if ((BIT(val[0]) == DBG_PHY_STATUS) && val[1] == 1) {
			dm->phy_dbg_info.show_phy_sts_all_pkt = (u8)val[2];
			dm->phy_dbg_info.show_phy_sts_max_cnt = (u16)val[3];

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "show_all_pkt=%d, show_max_num=%d\n\n",
				 dm->phy_dbg_info.show_phy_sts_all_pkt,
				 dm->phy_dbg_info.show_phy_sts_max_cnt);

		} else if ((BIT(val[0]) == DBG_CMN) && (val[1] == 1)) {
			dm->cmn_dbg_msg_period = (u8)val[2];

			if (dm->cmn_dbg_msg_period < PHYDM_WATCH_DOG_PERIOD)
				dm->cmn_dbg_msg_period = PHYDM_WATCH_DOG_PERIOD;

			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "cmn_dbg_msg_period=%d\n",
				 dm->cmn_dbg_msg_period);
		}
	}
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "pre-DbgComponents = 0x%llx\n", pre_debug_components);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "Curr-DbgComponents = 0x%llx\n", dm->debug_components);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "================================\n");

	*_used = used;
	*_out_len = out_len;
}

void phydm_fw_debug_trace(void *dm_void, char input[][16], u32 *_used,
			  char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 val[10] = {0};
	u8 i, input_idx = 0;
	char help[] = "-h";
	u32 pre_fw_debug_components = 0, one = 1;
	u32 comp = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &val[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	pre_fw_debug_components = dm->fw_debug_components;
	comp = dm->fw_debug_components;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
				 "{dbg_comp} {1:en, 2:dis} {mode} {macid}\n");
	} else {
		if (val[0] == 101) {
			dm->fw_debug_components = 0;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "%s\n", "Clear all fw debug components");
		} else {
			if (val[1] == 1) /*@enable*/
				dm->fw_debug_components |= (one << val[0]);
			else if (val[1] == 2) /*@disable*/
				dm->fw_debug_components &= ~(one << val[0]);
			else
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "%s\n",
					 "[Warning!!!]  1:enable,  2:disable");
		}

		comp = dm->fw_debug_components;

		if (comp == 0) {
			dm->debug_components &= ~DBG_FW_TRACE;
			/*@H2C to enable C2H Msg*/
			phydm_fw_trace_en_h2c(dm, false, comp, val[2], val[3]);
		} else {
			dm->debug_components |= DBG_FW_TRACE;
			/*@H2C to enable C2H Msg*/
			phydm_fw_trace_en_h2c(dm, true, comp, val[2], val[3]);
		}
	}
}

#if (ODM_IC_11N_SERIES_SUPPORT)
void phydm_dump_bb_reg_n(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/*@For Nseries IC we only need to dump page8 to pageF using 3 digits*/
	for (addr = 0x800; addr < 0xfff; addr += 4) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%03x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

#if (ODM_IC_11AC_SERIES_SUPPORT)
void phydm_dump_bb_reg_ac(void *dm_void, u32 *_used, char *output,
			  u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	for (addr = 0x800; addr < 0xfff; addr += 4) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	if (!(dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8814A | ODM_RTL8821C)))
		goto rpt_reg;

	if (dm->rf_type > RF_2T2R) {
		for (addr = 0x1800; addr < 0x18ff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n",
				      addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	if (dm->rf_type > RF_3T3R) {
		for (addr = 0x1a00; addr < 0x1aff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n",
				      addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	for (addr = 0x1900; addr < 0x19ff; addr += 4)
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));

	for (addr = 0x1c00; addr < 0x1cff; addr += 4)
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));

	for (addr = 0x1f00; addr < 0x1fff; addr += 4)
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));

rpt_reg:

	*_used = used;
	*_out_len = out_len;
}

#endif

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
void phydm_dump_bb_reg_jgr3(void *dm_void, u32 *_used, char *output,
			    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		for (addr = 0x800; addr < 0xdff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n", addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));

		for (addr = 0x1800; addr < 0x1aff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n", addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));

		for (addr = 0x1c00; addr < 0x1eff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n", addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));

		for (addr = 0x4000; addr < 0x41ff; addr += 4)
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n", addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_dump_bb_reg2_jgr3(void *dm_void, u32 *_used, char *output,
			     u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES))
		return;

	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {
		for (addr = 0x5000; addr < 0x53ff; addr += 4) {
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%04x 0x%08x\n",
				      addr,
				      odm_get_bb_reg(dm, addr, MASKDWORD));
		}
	}
	#endif
	/* @Do not change the order of page-2C/2D*/
	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "------ BB report-register start ------\n");
	for (addr = 0x2c00; addr < 0x2dff; addr += 4) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_get_per_path_anapar_jgr3(void *dm_void, u8 path, u32 *_used,
				    char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 state = 0;
	u8 state_bp = 0;
	u32 control_bb = 0;
	u32 control_pow = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 reg_idx = 0;
	u32 dbgport_idx = 0;
	u32 dbgport_val = 0;

	PDM_SNPF(out_len, used, output + used, out_len - used, "path-%d:\n",
		 path);

	if (path == RF_PATH_A) {
		reg_idx = R_0x1830;
		dbgport_idx = 0x9F0;
	} else if (path == RF_PATH_B) {
		reg_idx = R_0x4130;
		dbgport_idx = 0xBF0;
	} else if (path == RF_PATH_C) {
		reg_idx = R_0x5230;
		dbgport_idx = 0xDF0;
	} else if (path == RF_PATH_D) {
		reg_idx = R_0x5330;
		dbgport_idx = 0xFF0;
	}

	state_bp = (u8)odm_get_bb_reg(dm, reg_idx, 0xf00000);
	odm_set_bb_reg(dm, reg_idx, 0x38000000, 0x5); /* @read en*/

	for (state = 0; state <= 0xf; state++) {
		odm_set_bb_reg(dm, reg_idx, 0xF00000, state);
		if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, dbgport_idx)) {
			dbgport_val = phydm_get_bb_dbg_port_val(dm);
			phydm_release_bb_dbg_port(dm);
		} else {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "state:0x%x = read dbg_port error!\n", state);
		}
		control_bb = (dbgport_val & 0xFFFF0) >> 4;
		control_pow = dbgport_val & 0xF;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "state:0x%x = control_bb:0x%x pow_bb:0x%x\n",
			 state, control_bb, control_pow);
	}
	odm_set_bb_reg(dm, reg_idx, 0xf00000, state_bp);
	odm_set_bb_reg(dm, reg_idx, 0x38000000, 0x6); /* @write en*/

	*_used = used;
	*_out_len = out_len;
}

#endif

void phydm_dump_bb_reg(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "BB==========\n");
	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "------ BB control register start ------\n");

	switch (dm->ic_ip_series) {
	#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	case PHYDM_IC_JGR3:
		phydm_dump_bb_reg_jgr3(dm, &used, output, &out_len);
		break;
	#endif

	#if (ODM_IC_11AC_SERIES_SUPPORT == 1)
	case PHYDM_IC_AC:
		phydm_dump_bb_reg_ac(dm, &used, output, &out_len);
		break;
	#endif

	#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	case PHYDM_IC_N:
		phydm_dump_bb_reg_n(dm, &used, output, &out_len);
		break;
	#endif

	default:
		break;
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_dump_rf_reg(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 reg = 0;

	/* @dump RF register */
	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "RF-A==========\n");

	for (addr = 0; addr <= 0xFF; addr++) {
		reg = odm_get_rf_reg(dm, RF_PATH_A, addr, RFREG_MASK);
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%02x 0x%05x\n", addr, reg);
		}

#ifdef PHYDM_COMPILE_ABOVE_2SS
	if (dm->rf_type > RF_1T1R) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "RF-B==========\n");

		for (addr = 0; addr <= 0xFF; addr++) {
			reg = odm_get_rf_reg(dm, RF_PATH_B, addr, RFREG_MASK);
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%02x 0x%05x\n",
				      addr, reg);
		}
	}
#endif

#ifdef PHYDM_COMPILE_ABOVE_3SS
	if (dm->rf_type > RF_2T2R) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "RF-C==========\n");

		for (addr = 0; addr <= 0xFF; addr++) {
			reg = odm_get_rf_reg(dm, RF_PATH_C, addr, RFREG_MASK);
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%02x 0x%05x\n",
				      addr, reg);
		}
	}
#endif

#ifdef PHYDM_COMPILE_ABOVE_4SS
	if (dm->rf_type > RF_3T3R) {
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "RF-D==========\n");

		for (addr = 0; addr <= 0xFF; addr++) {
			reg = odm_get_rf_reg(dm, RF_PATH_D, addr, RFREG_MASK);
			PDM_VAST_SNPF(out_len, used, output + used,
				      out_len - used, "0x%02x 0x%05x\n",
				      addr, reg);
		}
	}
#endif

	*_used = used;
	*_out_len = out_len;
}

void phydm_dump_mac_reg(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* @dump MAC register */
	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "MAC==========\n");

	for (addr = 0; addr < 0x7ff; addr += 4)
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));

	for (addr = 0x1000; addr < 0x17ff; addr += 4)
		PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
			      "0x%04x 0x%08x\n",
			      addr, odm_get_bb_reg(dm, addr, MASKDWORD));

	*_used = used;
	*_out_len = out_len;
}

void phydm_dump_reg(void *dm_void, char input[][16], u32 *_used, char *output,
		    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 addr = 0;

	if (input[1])
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "dumpreg {0:all, 1:BB, 2:RF, 3:MAC 4:BB2 for jgr3}\n");
		else
		#endif
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "dumpreg {0:all, 1:BB, 2:RF, 3:MAC}\n");
	} else if (var1[0] == 0) {
		phydm_dump_mac_reg(dm, &used, output, &out_len);
		phydm_dump_bb_reg(dm, &used, output, &out_len);
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->ic_ip_series == PHYDM_IC_JGR3)
			phydm_dump_bb_reg2_jgr3(dm, &used, output, &out_len);
		#endif

		phydm_dump_rf_reg(dm, &used, output, &out_len);
	} else if (var1[0] == 1) {
		phydm_dump_bb_reg(dm, &used, output, &out_len);
	} else if (var1[0] == 2) {
		phydm_dump_rf_reg(dm, &used, output, &out_len);
	} else if (var1[0] == 3) {
		phydm_dump_mac_reg(dm, &used, output, &out_len);
	} else if (var1[0] == 4) {
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->ic_ip_series == PHYDM_IC_JGR3)
			phydm_dump_bb_reg2_jgr3(dm, &used, output, &out_len);
		#endif
	}

	*_used = used;
	*_out_len = out_len;
}

void phydm_enable_big_jump(void *dm_void, char input[][16], u32 *_used,
			   char *output, u32 *_out_len)
{
#if (RTL8822B_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	u32 dm_value[10] = {0};
	u8 i, input_idx = 0;
	u32 val;

	if (!(dm->support_ic_type & ODM_RTL8822B))
		return;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &dm_value[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if (dm_value[0] == 0) {
		dm->dm_dig_table.enable_adjust_big_jump = false;

		val = (dig_t->big_jump_step3 << 5) |
		      (dig_t->big_jump_step2 << 3) |
		      dig_t->big_jump_step1;

		odm_set_bb_reg(dm, R_0x8c8, 0xfe, val);
	} else {
		dm->dm_dig_table.enable_adjust_big_jump = true;
	}
#endif
}

void phydm_show_rx_rate(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len)
{
#if (RTL8822B_SUPPORT || RTL8821C_SUPPORT || RTL8814B_SUPPORT ||\
	RTL8195B_SUPPORT || RTL8822C_SUPPORT)
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg = &dm->phy_dbg_info;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 var1[10] = {0};
	char help[] = "-h";
	u8 i, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{1: show Rx rate, 0:reset counter}\n");
		*_used = used;
		*_out_len = out_len;
		return;

	} else if (var1[0] == 0) {
		phydm_reset_rx_rate_distribution(dm);
		*_used = used;
		*_out_len = out_len;
		return;
	}

	/* @==Show SU Rate====================================================*/
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "=====Rx SU rate Statistics=====\n");
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[SU][1SS] {%d, %d, %d, %d | %d, %d, %d, %d | %d, %d}\n",
		 dbg->num_qry_vht_pkt[0], dbg->num_qry_vht_pkt[1],
		 dbg->num_qry_vht_pkt[2], dbg->num_qry_vht_pkt[3],
		 dbg->num_qry_vht_pkt[4], dbg->num_qry_vht_pkt[5],
		 dbg->num_qry_vht_pkt[6], dbg->num_qry_vht_pkt[7],
		 dbg->num_qry_vht_pkt[8], dbg->num_qry_vht_pkt[9]);

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & (PHYDM_IC_ABOVE_2SS)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[SU][2SS] {%d, %d, %d, %d | %d, %d, %d, %d | %d, %d}\n",
			 dbg->num_qry_vht_pkt[10], dbg->num_qry_vht_pkt[11],
			 dbg->num_qry_vht_pkt[12], dbg->num_qry_vht_pkt[13],
			 dbg->num_qry_vht_pkt[14], dbg->num_qry_vht_pkt[15],
			 dbg->num_qry_vht_pkt[16], dbg->num_qry_vht_pkt[17],
			 dbg->num_qry_vht_pkt[18], dbg->num_qry_vht_pkt[19]);
	}
	#endif
	/* @==Show MU Rate====================================================*/
#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT) || (defined(PHYSTS_3RD_TYPE_SUPPORT))
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "=====Rx MU rate Statistics=====\n");
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[MU][1SS] {%d, %d, %d, %d | %d, %d, %d, %d | %d, %d}\n",
		 dbg->num_mu_vht_pkt[0], dbg->num_mu_vht_pkt[1],
		 dbg->num_mu_vht_pkt[2], dbg->num_mu_vht_pkt[3],
		 dbg->num_mu_vht_pkt[4], dbg->num_mu_vht_pkt[5],
		 dbg->num_mu_vht_pkt[6], dbg->num_mu_vht_pkt[7],
		 dbg->num_mu_vht_pkt[8], dbg->num_mu_vht_pkt[9]);

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & (PHYDM_IC_ABOVE_2SS)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[MU][2SS] {%d, %d, %d, %d | %d, %d, %d, %d | %d, %d}\n",
			 dbg->num_mu_vht_pkt[10], dbg->num_mu_vht_pkt[11],
			 dbg->num_mu_vht_pkt[12], dbg->num_mu_vht_pkt[13],
			 dbg->num_mu_vht_pkt[14], dbg->num_mu_vht_pkt[15],
			 dbg->num_mu_vht_pkt[16], dbg->num_mu_vht_pkt[17],
			 dbg->num_mu_vht_pkt[18], dbg->num_mu_vht_pkt[19]);
	}
	#endif
#endif
	*_used = used;
	*_out_len = out_len;
#endif
}

void phydm_per_tone_evm(void *dm_void, char input[][16], u32 *_used,
			char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 i, j;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 var1[4] = {0};
	u32 val, tone_num, round;
	s8 rxevm_0, rxevm_1;
	s32 avg_num, evm_tone_0[256] = {0}, evm_tone_1[256] = {0};
	s32 rxevm_sum_0, rxevm_sum_1;

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		pr_debug("n series not support yet !\n");
		return;
	}

	for (i = 0; i < 4; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}

	avg_num = var1[0];
	round = var1[1];

	if (!dm->is_linked) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "No Link !!\n");

		*_used = used;
		*_out_len = out_len;

		return;
	}

	pr_debug("ID=((%d)), BW=((%d)), fc=((CH-%d))\n", dm->curr_station_id,
		 20 << *dm->band_width, *dm->channel);
	pr_debug("avg_num =((%d)), round =((%d))\n", avg_num, round);
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	watchdog_stop(dm->priv);
#endif
	for (j = 0; j < round; j++) {
		pr_debug("\nround((%d))\n", (j + 1));
		if (*dm->band_width == CHANNEL_WIDTH_20) {
			for (tone_num = 228; tone_num <= 255; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;
					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(-%-3d) RXEVM(1ss/2ss)=%d, %d\n",
					 (256 - tone_num), evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}

			for (tone_num = 1; tone_num <= 28; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;
					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(%-3d) RXEVM(1ss/2ss)=%d, %d\n",
					 tone_num, evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}
		} else if (*dm->band_width == CHANNEL_WIDTH_40) {
			for (tone_num = 198; tone_num <= 254; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;

					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(-%-3d) RXEVM(1ss/2ss)=%d, %d\n",
					 (256 - tone_num), evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}

			for (tone_num = 2; tone_num <= 58; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;
					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(%-3d) RXEVM(1ss/2ss)=%d, %d\n",
					 tone_num, evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}
		} else if (*dm->band_width == CHANNEL_WIDTH_80) {
			for (tone_num = 134; tone_num <= 254; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;
					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(-%-3d) RXEVM(1ss/2ss)=%d, %d\n",
					 (256 - tone_num), evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}

			for (tone_num = 2; tone_num <= 122; tone_num++) {
				odm_set_bb_reg(dm, R_0x8c4, 0xff8, tone_num);
				rxevm_sum_0 = 0;
				rxevm_sum_1 = 0;
				for (i = 0; i < avg_num; i++) {
					val = odm_read_4byte(dm, R_0xf8c);

					rxevm_0 = (s8)((val & MASKBYTE2) >> 16);
					rxevm_0 = (rxevm_0 / 2);
					if (rxevm_0 < -63)
						rxevm_0 = 0;

					rxevm_1 = (s8)((val & MASKBYTE3) >> 24);
					rxevm_1 = (rxevm_1 / 2);
					if (rxevm_1 < -63)
						rxevm_1 = 0;
					rxevm_sum_0 += rxevm_0;
					rxevm_sum_1 += rxevm_1;
					ODM_delay_ms(1);
				}
				evm_tone_0[tone_num] = (rxevm_sum_0 / avg_num);
				evm_tone_1[tone_num] = (rxevm_sum_1 / avg_num);
				pr_debug("Tone(%-3d) RXEVM (1ss/2ss)=%d, %d\n",
					 tone_num, evm_tone_0[tone_num],
					 evm_tone_1[tone_num]);
			}
		}
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_bw_ch_adjust(void *dm_void, char input[][16],
			u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u8 i;
	boolean is_enable_dbg_mode;
	u8 central_ch, primary_ch_idx;
	enum channel_width bw;

#ifdef PHYDM_COMMON_API_SUPPORT

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{en} {CH} {pr_ch_idx 1/2/3/4/9/10} {0:20M,1:40M,2:80M}\n");
		goto out;
	}

	if (!(dm->support_ic_type & CMN_API_SUPPORT_IC)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Not support this API\n");
		goto out;
	}

	for (i = 0; i < 4; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}

	is_enable_dbg_mode = (boolean)var1[0];
	central_ch = (u8)var1[1];
	primary_ch_idx = (u8)var1[2];
	bw = (enum channel_width)var1[3];

	if (is_enable_dbg_mode) {
		dm->is_disable_phy_api = false;
		phydm_api_switch_bw_channel(dm, central_ch, primary_ch_idx, bw);
		dm->is_disable_phy_api = true;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "central_ch = %d, primary_ch_idx = %d, bw = %d\n",
			 central_ch, primary_ch_idx, bw);
	}
out:
#endif

	*_used = used;
	*_out_len = out_len;
}

void phydm_ext_rf_element_ctrl(void *dm_void, char input[][16], u32 *_used,
			       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 val[10] = {0};
	u8 i = 0, input_idx = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &val[i]);
			input_idx++;
		}
	}

	if (input_idx == 0)
		return;

	if (val[0] == 1) /*@ext switch*/ {
		phydm_set_ext_switch(dm, val[1]);
	}
}

void phydm_print_dbgport(void *dm_void, char input[][16], u32 *_used,
			 char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 dbg_port_value = 0;
	u8 val[32];
	u8 tmp = 0;
	u8 i;

	if (strcmp(input[1], help) == 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{dbg_port_idx}\n");
		goto out;
	}

	PHYDM_SSCANF(input[1], DCMD_HEX, &var1[0]);

	dm->debug_components |= ODM_COMP_API;
	if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_3, var1[0])) {
		dbg_port_value = phydm_get_bb_dbg_port_val(dm);
		phydm_release_bb_dbg_port(dm);

		for (i = 0; i < 32; i++)
			val[i] = (u8)((dbg_port_value & BIT(i)) >> i);

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Dbg Port[0x%x] = ((0x%x))\n", var1[0],
			 dbg_port_value);

		for (i = 4; i != 0; i--) {
			tmp = 8 * (i - 1);
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "val[%d:%d] = 8b'%d %d %d %d %d %d %d %d\n",
				 tmp + 7, tmp, val[tmp + 7], val[tmp + 6],
				 val[tmp + 5], val[tmp + 4], val[tmp + 3],
				 val[tmp + 2], val[tmp + 1], val[tmp + 0]);
		}
	}
	dm->debug_components &= (~ODM_COMP_API);
out:
	*_used = used;
	*_out_len = out_len;
}

void phydm_get_anapar_table(void *dm_void, u32 *_used, char *output,
			    u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	enum rf_path i = RF_PATH_A;

#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
	if (!(dm->support_ic_type & ODM_IC_JGR3_SERIES))
		return;

	PDM_VAST_SNPF(out_len, used, output + used, out_len - used,
		      "------ Analog parameters start ------\n");

	for (i = RF_PATH_A; i < (enum rf_path)dm->num_rf_path; i++)
		phydm_get_per_path_anapar_jgr3(dm, i, &used, output, &out_len);
#endif

	*_used = used;
	*_out_len = out_len;
}

void phydm_dd_dbg_dump(void *dm_void, char input[][16], u32 *_used,
		       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "dump: {1}\n");
		return;
	} else if (var1[0] == 1) {
		/*[Reg]*/
		phydm_dump_mac_reg(dm, &used, output, &out_len);
		phydm_dump_bb_reg(dm, &used, output, &out_len);
		#ifdef PHYDM_IC_JGR3_SERIES_SUPPORT
		if (dm->ic_ip_series == PHYDM_IC_JGR3)
			phydm_dump_bb_reg2_jgr3(dm, &used, output, &out_len);
		#endif

		phydm_dump_rf_reg(dm, &used, output, &out_len);
		/*[Dbg Port]*/
		#ifdef PHYDM_AUTO_DEGBUG
		phydm_dbg_port_dump(dm, &used, output, &out_len);
		#endif
		/*[Analog Parameters]*/
		phydm_get_anapar_table(dm, &used, output, &out_len);
	}
}

void phydm_nss_hitogram_mp(void *dm_void, enum PDM_RATE_TYPE rate_type,
			   u32 *_used, char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char buf[PHYDM_SNPRINT_SIZE] = {0};
	u16 buf_size = PHYDM_SNPRINT_SIZE;
	u16 h_size = PHY_HIST_SIZE;
	u16 *evm_hist = &dbg_s->evm_1ss_hist[0];
	u16 *snr_hist = &dbg_s->snr_1ss_hist[0];
	u8 i = 0;
	u8 ss = phydm_rate_type_2_num_ss(dm, rate_type);

	if (rate_type == PDM_OFDM) {
		phydm_print_hist_2_buf(dm, dbg_s->evm_ofdm_hist, PHY_HIST_SIZE,
				       buf, buf_size);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "%-14s=%s\n", "[OFDM][EVM]", buf);

		phydm_print_hist_2_buf(dm, dbg_s->snr_ofdm_hist, PHY_HIST_SIZE,
				       buf, buf_size);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "%-14s=%s\n", "[OFDM][SNR]", buf);

		*_used = used;
		*_out_len = out_len;
		return;
	}

	for (i = 0; i < ss; i++) {
		if (rate_type == PDM_1SS) {
			evm_hist = &dbg_s->evm_1ss_hist[0];
			snr_hist = &dbg_s->snr_1ss_hist[0];
		} else if (rate_type == PDM_2SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_2SS))
			evm_hist = &dbg_s->evm_2ss_hist[i][0];
			snr_hist = &dbg_s->snr_2ss_hist[i][0];
			#endif
		} else if (rate_type == PDM_3SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_3SS))
			evm_hist = &dbg_s->evm_3ss_hist[i][0];
			snr_hist = &dbg_s->snr_3ss_hist[i][0];
			#endif
		} else if (rate_type == PDM_4SS) {
			#if (defined(PHYDM_COMPILE_ABOVE_4SS))
			evm_hist = &dbg_s->evm_4ss_hist[i][0];
			snr_hist = &dbg_s->snr_4ss_hist[i][0];
			#endif
		}

		phydm_print_hist_2_buf(dm, evm_hist, h_size, buf, buf_size);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[%d-SS][EVM][%d]=%s\n", ss, i, buf);
		phydm_print_hist_2_buf(dm, snr_hist, h_size, buf, buf_size);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "[%d-SS][SNR][%d]=%s\n",  ss, i, buf);
	}
	*_used = used;
	*_out_len = out_len;
}

void phydm_mp_dbg(void *dm_void, char input[][16], u32 *_used, char *output,
		  u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct odm_phy_dbg_info *dbg_i = &dm->phy_dbg_info;
	struct phydm_phystatus_statistic *dbg_s = &dbg_i->physts_statistic_info;
	struct phydm_phystatus_avg *dbg_avg = &dbg_i->phystatus_statistic_avg;
	char *rate_type = NULL;
	u8 tmp_rssi_avg[4];
	u8 tmp_snr_avg[4];
	u8 tmp_evm_avg[4];
	u32 tmp_cnt = 0;
	char buf[PHYDM_SNPRINT_SIZE] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 var1[10] = {0};
	u16 buf_size = PHYDM_SNPRINT_SIZE;
	u16 th_size = PHY_HIST_SIZE - 1;
	u8 i = 0;

	if (!(*dm->mp_mode))
		return;

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "BW=((%d)), fc=((CH-%d))\n",
		 20 << *dm->band_width, *dm->channel);

	/*@===[PHY Histogram]================================================*/
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[PHY Histogram] ==============>\n");
	/*@===[Threshold]===*/
	phydm_print_hist_2_buf(dm, dbg_i->evm_hist_th, th_size, buf, buf_size);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "%-16s=%s\n", "[EVM_TH]", buf);
	phydm_print_hist_2_buf(dm, dbg_i->snr_hist_th, th_size, buf, buf_size);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "%-16s=%s\n", "[SNR_TH]", buf);
	/*@===[OFDM]===*/
	phydm_nss_hitogram_mp(dm, PDM_OFDM, &used, output, &out_len);
	/*@===[1-SS]===*/
	phydm_nss_hitogram_mp(dm, PDM_1SS, &used, output, &out_len);
	/*@===[2-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_2SS)
		phydm_nss_hitogram_mp(dm, PDM_2SS, &used, output, &out_len);
	#endif
	/*@===[3-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_3SS)
		phydm_nss_hitogram_mp(dm, PDM_3SS, &used, output, &out_len);
	#endif
	/*@===[4-SS]===*/
	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (dm->support_ic_type & PHYDM_IC_ABOVE_4SS)
		phydm_nss_hitogram_mp(dm, PDM_4SS, &used, output, &out_len);
	#endif
	/*@===[PHY Avg]======================================================*/
	phydm_get_avg_phystatus_val(dm);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "[PHY Avg] ==============>\n");

	phydm_get_avg_phystatus_val(dm);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
		 "[Beacon]", dbg_s->rssi_beacon_cnt, dbg_avg->rssi_beacon_avg);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "* %-8s Cnt=((%.3d)) RSSI:{%.2d}\n",
		 "[CCK]", dbg_s->rssi_cck_cnt, dbg_avg->rssi_cck_avg);

	for (i = 0; i <= 4; i++) {
		if (i > dm->num_rf_path)
			break;

		odm_memory_set(dm, tmp_rssi_avg, 0, 4);
		odm_memory_set(dm, tmp_snr_avg, 0, 4);
		odm_memory_set(dm, tmp_evm_avg, 0, 4);

		switch (i) {
		#if (defined(PHYDM_COMPILE_ABOVE_4SS))
		case 4:
			rate_type = "[4-SS]";
			tmp_cnt = dbg_s->rssi_4ss_cnt;
			odm_move_memory(dm, tmp_rssi_avg,
					dbg_avg->rssi_4ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_snr_avg,
					dbg_avg->snr_4ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_evm_avg, dbg_avg->evm_4ss_avg,
					4);
			break;
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_3SS))
		case 3:
			rate_type = "[3-SS]";
			tmp_cnt = dbg_s->rssi_3ss_cnt;
			odm_move_memory(dm, tmp_rssi_avg,
					dbg_avg->rssi_3ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_snr_avg,
					dbg_avg->snr_3ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_evm_avg,
					dbg_avg->evm_3ss_avg, 3);
			break;
		#endif
		#if (defined(PHYDM_COMPILE_ABOVE_2SS))
		case 2:
			rate_type = "[2-SS]";
			tmp_cnt = dbg_s->rssi_2ss_cnt;
			odm_move_memory(dm, tmp_rssi_avg,
					dbg_avg->rssi_2ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_snr_avg, dbg_avg->snr_2ss_avg,
					dm->num_rf_path);
			odm_move_memory(dm, tmp_evm_avg,
					dbg_avg->evm_2ss_avg, 2);
			break;
		#endif
		case 1:
			rate_type = "[1-SS]";
			tmp_cnt = dbg_s->rssi_1ss_cnt;
			odm_move_memory(dm, tmp_rssi_avg,
					dbg_avg->rssi_1ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_snr_avg,
					dbg_avg->snr_1ss_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_evm_avg,
					&dbg_avg->evm_1ss_avg, 1);
			break;
		default:
			rate_type = "[L-OFDM]";
			tmp_cnt = dbg_s->rssi_ofdm_cnt;
			odm_move_memory(dm, tmp_rssi_avg,
					dbg_avg->rssi_ofdm_avg,
					dm->num_rf_path);
			odm_move_memory(dm, tmp_snr_avg,
					dbg_avg->snr_ofdm_avg, dm->num_rf_path);
			odm_move_memory(dm, tmp_evm_avg,
					&dbg_avg->evm_ofdm_avg, 1);
			break;
		}

		PDM_SNPF(out_len, used, output + used, out_len - used,
			   "* %-8s Cnt=((%.3d)) RSSI:{%.2d, %.2d, %.2d, %.2d} SNR:{%.2d, %.2d, %.2d, %.2d} EVM:{-%.2d, -%.2d, -%.2d, -%.2d}\n",
			    rate_type, tmp_cnt,
			    tmp_rssi_avg[0], tmp_rssi_avg[1],
			    tmp_rssi_avg[2], tmp_rssi_avg[3],
			    tmp_snr_avg[0], tmp_snr_avg[1],
			    tmp_snr_avg[2], tmp_snr_avg[3],
			    tmp_evm_avg[0], tmp_evm_avg[1],
			    tmp_evm_avg[2], tmp_evm_avg[3]);
	}

	phydm_reset_phystatus_statistic(dm);

	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "rxsc_idx {Legacy, 20, 40, 80} = {%d, %d, %d, %d}\n",
		 dm->rxsc_l, dm->rxsc_20, dm->rxsc_40, dm->rxsc_80);

	*_used = used;
	*_out_len = out_len;
}

#if RTL8814B_SUPPORT
void phydm_spur_detect_dbg(void *dm_void, char input[][16], u32 *_used,
			   char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	char help[] = "-h";
	u32 var1[10] = {0};
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 i;

	if ((strcmp(input[1], help) == 0)) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{0: Auto spur detect(NBI+CSI), 1:NBI only,");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "2: CSI only, 3: Disable}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "{NBI path(0~3) | CSI wgt (0~7)}\n");
	} else {
		for (i = 0; i < 10; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
		}

		if (var1[0] == 1)
			dm->dsde_sel = DET_NBI;
		else if (var1[0] == 2)
			dm->dsde_sel = DET_CSI;
		else if (var1[0] == 3)
			dm->dsde_sel = DET_DISABLE;
		else
			dm->dsde_sel = DET_AUTO;

		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "spur detect mode = %d\n", dm->dsde_sel);

		if (dm->dsde_sel == DET_NBI) {
			if (var1[1] < 4) {
				dm->nbi_path_sel = (u8)var1[1];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "NBI set path %d\n",
					 dm->nbi_path_sel);
			} else {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "path setting fail\n");
			}
		} else if (dm->dsde_sel == DET_CSI) {
			if (var1[1] < 8) {
				dm->csi_wgt = (u8)var1[1];
				PDM_SNPF(out_len, used, output + used,
					 out_len - used, "CSI wgt %d\n",
					 dm->csi_wgt);
			} else {
				PDM_SNPF(out_len, used, output + used,
					 out_len - used,
					 "CSI wgt setting fail\n");
			}
		}
	}

	*_used = used;
	*_out_len = out_len;
}
#endif

struct phydm_command {
	char name[16];
	u8 id;
};

enum PHYDM_CMD_ID {
	PHYDM_HELP,
	PHYDM_DEMO,
	PHYDM_RF_CMD,
	PHYDM_DIG,
	PHYDM_RA,
	PHYDM_PROFILE,
	PHYDM_ANTDIV,
	PHYDM_PATHDIV,
	PHYDM_DEBUG,
	PHYDM_MP_DEBUG,
	PHYDM_FW_DEBUG,
	PHYDM_SUPPORT_ABILITY,
	PHYDM_GET_TXAGC,
	PHYDM_SET_TXAGC,
	PHYDM_SMART_ANT,
	PHYDM_CH_BW,
	PHYDM_TRX_PATH,
	PHYDM_LA_MODE,
	PHYDM_DUMP_REG,
	PHYDM_AUTO_DBG,
	PHYDM_DD_DBG,
	PHYDM_BIG_JUMP,
	PHYDM_SHOW_RXRATE,
	PHYDM_NBI_EN,
	PHYDM_CSI_MASK_EN,
	PHYDM_DFS_DEBUG,
	PHYDM_DFS_HIST,
	PHYDM_NHM,
	PHYDM_CLM,
	PHYDM_FAHM,
	PHYDM_ENV_MNTR,
	PHYDM_BB_INFO,
	//PHYDM_TXBF,
	PHYDM_H2C,
	PHYDM_EXT_RF_E_CTRL,
	PHYDM_ADAPTIVE_SOML,
	PHYDM_PSD,
	PHYDM_DEBUG_PORT,
	PHYDM_DIS_HTSTF_CONTROL,
	PHYDM_CFO_TRK,
	PHYDM_ADAPTIVITY_DEBUG,
	PHYDM_DIS_DYM_ANT_WEIGHTING,
	PHYDM_FORECE_PT_STATE,
	PHYDM_STA_INFO,
	PHYDM_PAUSE_FUNC,
	PHYDM_PER_TONE_EVM,
	PHYDM_DYN_TXPWR,
	PHYDM_LNA_SAT,
	PHYDM_ANAPAR,
	PHYDM_BEAM_FORMING,
#if RTL8814B_SUPPORT
	PHYDM_SPUR_DETECT
#endif
};

struct phydm_command phy_dm_ary[] = {
	{"-h", PHYDM_HELP}, /*@do not move this element to other position*/
	{"demo", PHYDM_DEMO}, /*@do not move this element to other position*/
	{"rf", PHYDM_RF_CMD},
	{"dig", PHYDM_DIG},
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"antdiv", PHYDM_ANTDIV},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"mp_dbg", PHYDM_MP_DEBUG},
	{"fw_dbg", PHYDM_FW_DEBUG},
	{"ability", PHYDM_SUPPORT_ABILITY},
	{"get_txagc", PHYDM_GET_TXAGC},
	{"set_txagc", PHYDM_SET_TXAGC},
	{"smtant", PHYDM_SMART_ANT},
	{"ch_bw", PHYDM_CH_BW},
	{"trxpath", PHYDM_TRX_PATH},
	{"lamode", PHYDM_LA_MODE},
	{"dumpreg", PHYDM_DUMP_REG},
	{"auto_dbg", PHYDM_AUTO_DBG},
	{"dd_dbg", PHYDM_DD_DBG},
	{"bigjump", PHYDM_BIG_JUMP},
	{"rxrate", PHYDM_SHOW_RXRATE},
	{"nbi", PHYDM_NBI_EN},
	{"csi_mask", PHYDM_CSI_MASK_EN},
	{"dfs", PHYDM_DFS_DEBUG},
	{"dfs_hist", PHYDM_DFS_HIST},
	{"nhm", PHYDM_NHM},
	{"clm", PHYDM_CLM},
	{"fahm", PHYDM_FAHM},
	{"env_mntr", PHYDM_ENV_MNTR},
	{"bbinfo", PHYDM_BB_INFO},
	//{"txbf", PHYDM_TXBF},
	{"h2c", PHYDM_H2C},
	{"ext_rfe", PHYDM_EXT_RF_E_CTRL},
	{"soml", PHYDM_ADAPTIVE_SOML},
	{"psd", PHYDM_PSD},
	{"dbgport", PHYDM_DEBUG_PORT},
	{"dis_htstf", PHYDM_DIS_HTSTF_CONTROL},
	{"cfo_trk", PHYDM_CFO_TRK},
	{"adapt_debug", PHYDM_ADAPTIVITY_DEBUG},
	{"dis_dym_ant_wgt", PHYDM_DIS_DYM_ANT_WEIGHTING},
	{"force_pt_state", PHYDM_FORECE_PT_STATE},
	{"sta_info", PHYDM_STA_INFO},
	{"pause", PHYDM_PAUSE_FUNC},
	{"evm", PHYDM_PER_TONE_EVM},
	{"dyn_txpwr", PHYDM_DYN_TXPWR},
	{"lna_sat", PHYDM_LNA_SAT},
	{"anapar", PHYDM_ANAPAR},
	{"bf", PHYDM_BEAM_FORMING},
#if RTL8814B_SUPPORT
	{"spur_detect", PHYDM_SPUR_DETECT}
#endif
	};

#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/

void phydm_cmd_parser(struct dm_struct *dm, char input[][MAX_ARGV],
		      u32 input_num, u8 flag, char *output, u32 out_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	u32 used = 0;
	u8 id = 0;
	u32 var1[10] = {0};
	u32 i;
	u32 phydm_ary_size = sizeof(phy_dm_ary) / sizeof(struct phydm_command);

	if (flag == 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "GET, nothing to print\n");
		return;
	}

	PDM_SNPF(out_len, used, output + used, out_len - used, "\n");

	/* Parsing Cmd ID */
	if (input_num) {
		for (i = 0; i < phydm_ary_size; i++) {
			if (strcmp(phy_dm_ary[i].name, input[0]) == 0) {
				id = phy_dm_ary[i].id;
				break;
			}
		}
		if (i == phydm_ary_size) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "PHYDM command not found!\n");
			return;
		}
	}

	switch (id) {
	case PHYDM_HELP: {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "BB cmd ==>\n");

		for (i = 0; i < phydm_ary_size - 2; i++)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "  %-5d: %s\n", i, phy_dm_ary[i + 2].name);
	} break;

	case PHYDM_DEMO: { /*@echo demo 10 0x3a z abcde >cmd*/
		u32 directory = 0;

		#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
		char char_temp;
		#else
		u32 char_temp = ' ';
		#endif

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &directory);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Decimal value = %d\n", directory);
		PHYDM_SSCANF(input[2], DCMD_HEX, &directory);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Hex value = 0x%x\n", directory);
		PHYDM_SSCANF(input[3], DCMD_CHAR, &char_temp);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Char = %c\n", char_temp);
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "String = %s\n", input[4]);
	} break;
	case PHYDM_RF_CMD:
		halrf_cmd_parser(dm, input, &used, output, &out_len, input_num);
		break;

	case PHYDM_DIG:
		phydm_dig_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_RA:
		phydm_ra_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_ANTDIV:
		#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
		phydm_antdiv_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_PATHDIV:
		#if (defined(CONFIG_PATH_DIVERSITY))
		phydm_pathdiv_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_DEBUG:
		phydm_debug_trace(dm, input, &used, output, &out_len);
		break;

	case PHYDM_MP_DEBUG:
		phydm_mp_dbg(dm, input, &used, output, &out_len);
		break;

	case PHYDM_FW_DEBUG:
		phydm_fw_debug_trace(dm, input, &used, output, &out_len);
		break;

	case PHYDM_SUPPORT_ABILITY:
		phydm_supportability_en(dm, input, &used, output, &out_len);
		break;

	case PHYDM_SMART_ANT:
		#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))

		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		phydm_hl_smt_ant_dbg_type2(dm, input, &used, output, &out_len);
		#elif (defined(CONFIG_HL_SMART_ANTENNA_TYPE1))
		phydm_hl_smart_ant_debug(dm, input, &used, output, &out_len);
		#endif

		#elif (defined(CONFIG_CUMITEK_SMART_ANTENNA))
		phydm_cumitek_smt_ant_debug(dm, input, &used, output, &out_len);
		#endif

		break;

	case PHYDM_CH_BW:
		phydm_bw_ch_adjust(dm, input, &used, output, &out_len);
		break;

	case PHYDM_PROFILE:
		phydm_basic_profile(dm, &used, output, &out_len);
		break;

	case PHYDM_GET_TXAGC:
		phydm_get_txagc(dm, &used, output, &out_len);
		break;

	case PHYDM_SET_TXAGC:
		phydm_set_txagc_dbg(dm, input, &used, output, &out_len);
		break;

	case PHYDM_TRX_PATH:
		phydm_config_trx_path(dm, input, &used, output, &out_len);
		break;

	case PHYDM_LA_MODE:
		#if (PHYDM_LA_MODE_SUPPORT)
		phydm_la_cmd(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_DUMP_REG:
		phydm_dump_reg(dm, input, &used, output, &out_len);
		break;

	case PHYDM_BIG_JUMP:
		phydm_enable_big_jump(dm, input, &used, output, &out_len);
		break;

	case PHYDM_AUTO_DBG:
		#ifdef PHYDM_AUTO_DEGBUG
		phydm_auto_dbg_console(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_DD_DBG:
		phydm_dd_dbg_dump(dm, input, &used, output, &out_len);
		break;

	case PHYDM_SHOW_RXRATE:
		phydm_show_rx_rate(dm, input, &used, output, &out_len);
		break;

	case PHYDM_NBI_EN:
		phydm_nbi_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_CSI_MASK_EN:
		phydm_csi_debug(dm, input, &used, output, &out_len);
		break;

	#ifdef CONFIG_PHYDM_DFS_MASTER
	case PHYDM_DFS_DEBUG:
		phydm_dfs_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_DFS_HIST:
		phydm_dfs_hist_dbg(dm, input, &used, output, &out_len);
		break;
	#endif

	case PHYDM_NHM:
		#ifdef NHM_SUPPORT
		phydm_nhm_dbg(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_CLM:
		#ifdef CLM_SUPPORT
		phydm_clm_dbg(dm, input, &used, output, &out_len);
		#endif
		break;

	#ifdef FAHM_SUPPORT
	case PHYDM_FAHM:
		phydm_fahm_dbg(dm, input, &used, output, &out_len);
		break;
	#endif

	case PHYDM_ENV_MNTR:
		phydm_env_mntr_dbg(dm, input, &used, output, &out_len);
		break;

	case PHYDM_BB_INFO:
		phydm_bb_hw_dbg_info(dm, input, &used, output, &out_len);
		break;
	/*
	case PHYDM_TXBF: {
	#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	#ifdef PHYDM_BEAMFORMING_SUPPORT
		struct _RT_BEAMFORMING_INFO *beamforming_info = NULL;

		beamforming_info = &dm->beamforming_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		if (var1[0] == 0) {
			beamforming_info->apply_v_matrix = false;
			beamforming_info->snding3ss = true;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\r\n dont apply V matrix and 3SS 789 snding\n");
		} else if (var1[0] == 1) {
			beamforming_info->apply_v_matrix = true;
			beamforming_info->snding3ss = true;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\r\n apply V matrix and 3SS 789 snding\n");
		} else if (var1[0] == 2) {
			beamforming_info->apply_v_matrix = true;
			beamforming_info->snding3ss = false;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\r\n default txbf setting\n");
		} else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "\r\n unknown cmd!!\n");
	#endif
	#endif
	} break;
	*/
	case PHYDM_H2C:
		phydm_h2C_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_EXT_RF_E_CTRL:
		phydm_ext_rf_element_ctrl(dm, input, &used, output, &out_len);
		break;

	case PHYDM_ADAPTIVE_SOML:
		#ifdef CONFIG_ADAPTIVE_SOML
		phydm_soml_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_PSD:

		#ifdef CONFIG_PSD_TOOL
		phydm_psd_debug(dm, input, &used, output, &out_len);
		#endif

		break;

	case PHYDM_DEBUG_PORT:
		phydm_print_dbgport(dm, input, &used, output, &out_len);
		break;

	case PHYDM_DIS_HTSTF_CONTROL: {
		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 1) {
			/* setting being false is for debug */
			dm->bhtstfdisabled = true;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Dynamic HT-STF Gain Control is Disable\n");
		} else {
			/* @default setting should be true,
			 * always be dynamic control
			 */
			dm->bhtstfdisabled = false;
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "Dynamic HT-STF Gain Control is Enable\n");
		}
	} break;

	case PHYDM_CFO_TRK:
		phydm_cfo_tracking_debug(dm, input, &used, output, &out_len);
		break;

	case PHYDM_ADAPTIVITY_DEBUG:
		#ifdef PHYDM_SUPPORT_ADAPTIVITY
		phydm_adaptivity_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_DIS_DYM_ANT_WEIGHTING:
		#ifdef DYN_ANT_WEIGHTING_SUPPORT
		phydm_ant_weight_dbg(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_FORECE_PT_STATE:
		#ifdef PHYDM_POWER_TRAINING_SUPPORT
		phydm_pow_train_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_STA_INFO:
		phydm_show_sta_info(dm, input, &used, output, &out_len);
		break;

	case PHYDM_PAUSE_FUNC:
		phydm_pause_func_console(dm, input, &used, output, &out_len);
		break;

	case PHYDM_PER_TONE_EVM:
		phydm_per_tone_evm(dm, input, &used, output, &out_len);
		break;

	#ifdef CONFIG_DYNAMIC_TX_TWR
	case PHYDM_DYN_TXPWR:
		phydm_dtp_debug(dm, input, &used, output, &out_len);
		break;
	#endif

	case PHYDM_LNA_SAT:
		#ifdef PHYDM_LNA_SAT_CHK_SUPPORT
		phydm_lna_sat_debug(dm, input, &used, output, &out_len);
		#endif
		break;

	case PHYDM_ANAPAR:
		phydm_get_anapar_table(dm, &used, output, &out_len);
		break;
	case PHYDM_BEAM_FORMING:
		#ifdef CONFIG_BB_TXBF_API
		phydm_bf_debug(dm, input, &used, output, &out_len);
		#endif
		break;

#if RTL8814B_SUPPORT
	case PHYDM_SPUR_DETECT:
		phydm_spur_detect_dbg(dm, input, &used, output, &out_len);
		break;
#endif
	default:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Do not support this command\n");
		break;
	}
#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

#if defined __ECOS || defined __ICCARM__
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (!sbegin)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP | ODM_IOT))
s32 phydm_cmd(struct dm_struct *dm, char *input, u32 in_len, u8 flag,
	      char *output, u32 out_len)
{
	char *token;
	u32 argc = 0;
	char argv[MAX_ARGC][MAX_ARGV];

	do {
		token = strsep(&input, ", ");
		if (token) {
			if (strlen(token) <= MAX_ARGV)
				strcpy(argv[argc], token);

			argc++;
		} else {
			break;
		}
	} while (argc < MAX_ARGC);

	if (argc == 1)
		argv[0][strlen(argv[0]) - 1] = '\0';

	phydm_cmd_parser(dm, argv, argc, flag, output, out_len);

	return 0;
}
#endif

void phydm_fw_trace_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	/*@u8	debug_trace_11byte[60];*/
	u8 freg_num, c2h_seq, buf_0 = 0;

	if (!(dm->support_ic_type & PHYDM_IC_3081_SERIES))
		return;

	if (cmd_len > 12 || cmd_len == 0) {
		pr_debug("[Warning] Error C2H cmd_len=%d\n", cmd_len);
		return;
	}

	buf_0 = cmd_buf[0];
	freg_num = (buf_0 & 0xf);
	c2h_seq = (buf_0 & 0xf0) >> 4;

	#if 0
	PHYDM_DBG(dm, DBG_FW_TRACE,
		  "[FW debug message] freg_num = (( %d )), c2h_seq=(( %d ))\n",
		  freg_num, c2h_seq);

	strncpy(debug_trace_11byte, &cmd_buf[1], (cmd_len - 1));
	debug_trace_11byte[cmd_len - 1] = '\0';
	PHYDM_DBG(dm, DBG_FW_TRACE, "[FW debug message] %s\n",
		  debug_trace_11byte);
	PHYDM_DBG(dm, DBG_FW_TRACE, "[FW debug message] cmd_len = (( %d ))\n",
		  cmd_len);
	PHYDM_DBG(dm, DBG_FW_TRACE, "[FW debug message] c2h_cmd_start=((%d))\n",
		  dm->c2h_cmd_start);

	PHYDM_DBG(dm, DBG_FW_TRACE, "pre_seq = (( %d )), current_seq=((%d))\n",
		  dm->pre_c2h_seq, c2h_seq);
	PHYDM_DBG(dm, DBG_FW_TRACE, "fw_buff_is_enpty = (( %d ))\n",
		  dm->fw_buff_is_enpty);
	#endif

	if (c2h_seq != dm->pre_c2h_seq && dm->fw_buff_is_enpty == false) {
		dm->fw_debug_trace[dm->c2h_cmd_start] = '\0';
		PHYDM_DBG(dm, DBG_FW_TRACE, "[FW Dbg Queue Overflow] %s\n",
			  dm->fw_debug_trace);
		dm->c2h_cmd_start = 0;
	}

	if ((cmd_len - 1) > (60 - dm->c2h_cmd_start)) {
		dm->fw_debug_trace[dm->c2h_cmd_start] = '\0';
		PHYDM_DBG(dm, DBG_FW_TRACE,
			  "[FW Dbg Queue error: wrong C2H length] %s\n",
			  dm->fw_debug_trace);
		dm->c2h_cmd_start = 0;
		return;
	}

	strncpy((char *)&dm->fw_debug_trace[dm->c2h_cmd_start],
		(char *)&cmd_buf[1], (cmd_len - 1));
	dm->c2h_cmd_start += (cmd_len - 1);
	dm->fw_buff_is_enpty = false;

	if (freg_num == 0 || dm->c2h_cmd_start >= 60) {
		if (dm->c2h_cmd_start < 60)
			dm->fw_debug_trace[dm->c2h_cmd_start] = '\0';
		else
			dm->fw_debug_trace[59] = '\0';

		PHYDM_DBG(dm, DBG_FW_TRACE, "[FW DBG Msg] %s\n",
			  dm->fw_debug_trace);
#if 0
		/*@dbg_print("[FW DBG Msg] %s\n", dm->fw_debug_trace);*/
#endif
		dm->c2h_cmd_start = 0;
		dm->fw_buff_is_enpty = true;
	}

	dm->pre_c2h_seq = c2h_seq;
#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void phydm_fw_trace_handler_code(void *dm_void, u8 *buffer, u8 cmd_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u8 function = buffer[0];
	u8 dbg_num = buffer[1];
	u16 content_0 = (((u16)buffer[3]) << 8) | ((u16)buffer[2]);
	u16 content_1 = (((u16)buffer[5]) << 8) | ((u16)buffer[4]);
	u16 content_2 = (((u16)buffer[7]) << 8) | ((u16)buffer[6]);
	u16 content_3 = (((u16)buffer[9]) << 8) | ((u16)buffer[8]);
	u16 content_4 = (((u16)buffer[11]) << 8) | ((u16)buffer[10]);

	if (cmd_len > 12)
		PHYDM_DBG(dm, DBG_FW_TRACE,
			  "[FW Msg] Invalid cmd length (( %d )) >12\n",
			  cmd_len);
/*@--------------------------------------------*/
#ifdef CONFIG_RA_FW_DBG_CODE
	if (function == RATE_DECISION) {
		if (dbg_num == 0) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] RA_CNT=((%d))  Max_device=((%d))--------------------------->\n",
					  content_1, content_2);
			else if (content_0 == 2)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Check RA macid= ((%d)), MediaStatus=((%d)), Dis_RA=((%d)),  try_bit=((0x%x))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 3)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Check RA  total=((%d)),  drop=((0x%x)), TXRPT_TRY_bit=((%x)), bNoisy=((%x))\n",
					  content_1, content_2, content_3,
					  content_4);
		} else if (dbg_num == 1) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] RTY[0,1,2,3]=[ %d , %d , %d , %d ]\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 2) {
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] RTY[4]=[ %d ], drop=(( %d )), total=(( %d )), current_rate=((0x %x ))",
					  content_1, content_2, content_3,
					  content_4);
				phydm_print_rate(dm, (u8)content_4,
						 DBG_FW_TRACE);
			} else if (content_0 == 3)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] penality_idx=(( %d ))\n",
					  content_1);
			else if (content_0 == 4)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] RSSI=(( %d )), ra_stage = (( %d ))\n",
					  content_1, content_2);
		} else if (dbg_num == 3) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Fast_RA (( DOWN ))  total=((%d)),  total>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 2)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Fast_RA (( UP ))  total_acc=((%d)),  total_acc>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 3)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Fast_RA (( UP )) ((rate Down Hold))  RA_CNT=((%d))\n",
					  content_1);
			else if (content_0 == 4)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Fast_RA (( UP )) ((tota_accl<5 skip))  RA_CNT=((%d))\n",
					  content_1);
			else if (content_0 == 8)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] Fast_RA (( Reset Tx Rpt )) RA_CNT=((%d))\n",
					  content_1);
		} else if (dbg_num == 4) {
			if (content_0 == 3)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] RER_CNT   PCR_ori =(( %d )),  ratio_ori =(( %d )), pcr_updown_bitmap =(( 0x%x )), pcr_var_diff =(( %d ))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 4)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] pcr_shift_value =(( %s%d )), rate_down_threshold =(( %d )), rate_up_threshold =(( %d ))\n",
					  ((content_1) ? "+" : "-"), content_2,
					  content_3, content_4);
			else if (content_0 == 5)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] pcr_mean =(( %d )), PCR_VAR =(( %d )), offset =(( %d )), decision_offset_p =(( %d ))\n",
					  content_1, content_2, content_3,
					  content_4);
		} else if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] (( UP))  Nsc=(( %d )), N_High=(( %d )), RateUp_Waiting=(( %d )), RateUp_Fail=(( %d ))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 2)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] ((DOWN))  Nsc=(( %d )), N_Low=(( %d ))\n",
					  content_1, content_2);
			else if (content_0 == 3)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] ((HOLD))  Nsc=((%d)), N_High=((%d)), N_Low=((%d)), Reset_CNT=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
		} else if (dbg_num == 0x60) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] ((AP RPT))  macid=((%d)), BUPDATE[macid]=((%d))\n",
					  content_1, content_2);
			else if (content_0 == 4)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] ((AP RPT))  pass=((%d)), rty_num=((%d)), drop=((%d)), total=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 5)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW] ((AP RPT))  PASS=((%d)), RTY_NUM=((%d)), DROP=((%d)), TOTAL=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
		}
	} else if (function == INIT_RA_TABLE) {
		if (dbg_num == 3)
			PHYDM_DBG(dm, DBG_FW_TRACE,
				  "[FW][INIT_RA_INFO] Ra_init, RA_SKIP_CNT = (( %d ))\n",
				  content_0);
	} else if (function == RATE_UP) {
		if (dbg_num == 2) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][RateUp]  ((Highest rate->return)), macid=((%d))  Nsc=((%d))\n",
					  content_1, content_2);
		} else if (dbg_num == 5) {
			if (content_0 == 0)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][RateUp]  ((rate UP)), up_rate_tmp=((0x%x)), rate_idx=((0x%x)), SGI_en=((%d)),  SGI=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
			else if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][RateUp]  ((rate UP)), rate_1=((0x%x)), rate_2=((0x%x)), BW=((%d)), Try_Bit=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
		}
	} else if (function == RATE_DOWN) {
		if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][RateDownStep]  ((rate Down)), macid=((%d)), rate1=((0x%x)),  rate2=((0x%x)), BW=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
		}
	} else if (function == TRY_DONE) {
		if (dbg_num == 1) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][Try Done]  ((try succsess )) macid=((%d)), Try_Done_cnt=((%d))\n",
					  content_1, content_2);
		} else if (dbg_num == 2) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][Try Done]  ((try)) macid=((%d)), Try_Done_cnt=((%d)),  rate_2=((%d)),  try_succes=((%d))\n",
					  content_1, content_2, content_3,
					  content_4);
		}
	} else if (function == RA_H2C) {
		if (dbg_num == 1) {
			if (content_0 == 0)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][H2C=0x49]  fw_trace_en=((%d)), mode =((%d)),  macid=((%d))\n",
					  content_1, content_2, content_3);
		}
	} else if (function == F_RATE_AP_RPT) {
		if (dbg_num == 1) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  ((1)), SPE_STATIS=((0x%x))---------->\n",
					  content_3);
		} else if (dbg_num == 2) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  RTY_all=((%d))\n",
					  content_1);
		} else if (dbg_num == 3) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  MACID1[%d], TOTAL=((%d)),  RTY=((%d))\n",
					  content_3, content_1, content_2);
		} else if (dbg_num == 4) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  MACID2[%d], TOTAL=((%d)),  RTY=((%d))\n",
					  content_3, content_1, content_2);
		} else if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  MACID1[%d], PASS=((%d)),  DROP=((%d))\n",
					  content_3, content_1, content_2);
		} else if (dbg_num == 6) {
			if (content_0 == 1)
				PHYDM_DBG(dm, DBG_FW_TRACE,
					  "[FW][AP RPT]  MACID2[%d],, PASS=((%d)),  DROP=((%d))\n",
					  content_3, content_1, content_2);
		}
	} else if (function == DBC_FW_CLM) {
		PHYDM_DBG(dm, DBG_FW_TRACE,
			  "[FW][CLM][%d, %d] = {%d, %d, %d, %d}\n", dbg_num,
			  content_0, content_1, content_2, content_3,
			  content_4);
	} else {
		PHYDM_DBG(dm, DBG_FW_TRACE,
			  "[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n",
			  function, dbg_num, content_0, content_1, content_2,
			  content_3, content_4);
	}
#else
	PHYDM_DBG(dm, DBG_FW_TRACE,
		  "[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function,
		  dbg_num, content_0, content_1, content_2, content_3,
		  content_4);
#endif
/*@--------------------------------------------*/

#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void phydm_fw_trace_handler_8051(void *dm_void, u8 *buffer, u8 cmd_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#if 0
	if (cmd_len >= 3)
		cmd_buf[cmd_len - 1] = '\0';
	PHYDM_DBG(dm, DBG_FW_TRACE, "[FW DBG Msg] %s\n", &cmd_buf[3]);
#else

	int i = 0;
	u8 extend_c2h_sub_id = 0, extend_c2h_dbg_len = 0;
	u8 extend_c2h_dbg_seq = 0;
	u8 fw_debug_trace[128];
	u8 *extend_c2h_dbg_content = 0;

	if (cmd_len > 127)
		return;

	extend_c2h_sub_id = buffer[0];
	extend_c2h_dbg_len = buffer[1];
	extend_c2h_dbg_content = buffer + 2; /*@DbgSeq+DbgContent for show HEX*/

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISP(FC2H, C2H_Summary, ("[Extend C2H packet], Extend_c2hSubId=0x%x, extend_c2h_dbg_len=%d\n",
				    extend_c2h_sub_id, extend_c2h_dbg_len));

	RT_DISP_DATA(FC2H, C2H_Summary, "[Extend C2H packet], Content Hex:", extend_c2h_dbg_content, cmd_len - 2);
#endif

go_backfor_aggre_dbg_pkt:
	i = 0;
	extend_c2h_dbg_seq = buffer[2];
	extend_c2h_dbg_content = buffer + 3;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	RT_DISP(FC2H, C2H_Summary, ("[RTKFW, SEQ= %d] :", extend_c2h_dbg_seq));
#endif

	for (;; i++) {
		fw_debug_trace[i] = extend_c2h_dbg_content[i];
		if (extend_c2h_dbg_content[i + 1] == '\0') {
			fw_debug_trace[i + 1] = '\0';
			PHYDM_DBG(dm, DBG_FW_TRACE, "[FW DBG Msg] %s",
				  &fw_debug_trace[0]);
			break;
		} else if (extend_c2h_dbg_content[i] == '\n') {
			fw_debug_trace[i + 1] = '\0';
			PHYDM_DBG(dm, DBG_FW_TRACE, "[FW DBG Msg] %s",
				  &fw_debug_trace[0]);
			buffer = extend_c2h_dbg_content + i + 3;
			goto go_backfor_aggre_dbg_pkt;
		}
	}

#endif
#endif /*@#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}
