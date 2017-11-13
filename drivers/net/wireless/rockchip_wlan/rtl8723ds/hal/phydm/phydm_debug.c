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
phydm_init_debug_setting(
	struct PHY_DM_STRUCT		*p_dm
)
{
	p_dm->debug_level = ODM_DBG_TRACE;

	p_dm->fw_debug_components = 0;
	p_dm->debug_components =
		\
#if DBG
		/*BB Functions*/
		/*									DBG_DIG					|*/
		/*									DBG_RA_MASK				|*/
		/*									DBG_DYN_TXPWR		|*/
		/*									DBG_FA_CNT				|*/
		/*									DBG_RSSI_MNTR			|*/
		/*									DBG_CCKPD					|*/
		/*									DBG_ANT_DIV				|*/
		/*									DBG_SMT_ANT				|*/
		/*									DBG_PWR_TRAIN					|*/
		/*									DBG_RA					|*/
		/*									DBG_PATH_DIV				|*/
		/*									DBG_DFS					|*/
		/*									DBG_DYN_ARFR			|*/
		/*									DBG_ADPTVTY			|*/
		/*									DBG_CFO_TRK		|*/
		/*									DBG_ENV_MNTR					|*/
		/*									DBG_PRI_CCA		|*/
		/*									DBG_ADPTV_SOML		|*/
		
		
		/*									DBG_DYN_RX_PATH		|*/
		/*									DBG_TMP					|*/
		/*									DBG_FW_TRACE			|*/
		/*									DBG_TXBF				|*/
		/*									DBG_COMMON_FLOW				|*/
		/*									ODM_COMP_TX_PWR_TRACK		|*/
		/*									ODM_COMP_CALIBRATION			|*/
		/*									ODM_COMP_MP					|*/
		/*									ODM_PHY_CONFIG					|*/
		/*									ODM_COMP_INIT					|*/
		/*									ODM_COMP_COMMON				|*/
		/*									ODM_COMP_API					|*/


#endif
		0;

	p_dm->fw_buff_is_enpty = true;
	p_dm->pre_c2h_seq = 0;
	p_dm->c2h_cmd_start = 0;
}

void
phydm_bb_dbg_port_header_sel(
	void			*p_dm_void,
	u32			header_idx
) {
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		
		odm_set_bb_reg(p_dm, 0x8f8, (BIT(25) | BIT(24) | BIT(23) | BIT(22)), header_idx);
		
		/*
		header_idx:
			(0:) '{ofdm_dbg[31:0]}'
			(1:) '{cca,crc32_fail,dbg_ofdm[29:0]}'
			(2:) '{vbon,crc32_fail,dbg_ofdm[29:0]}'
			(3:) '{cca,crc32_ok,dbg_ofdm[29:0]}'
			(4:) '{vbon,crc32_ok,dbg_ofdm[29:0]}'
			(5:) '{dbg_iqk_anta}'
			(6:) '{cca,ofdm_crc_ok,dbg_dp_anta[29:0]}'
			(7:) '{dbg_iqk_antb}'
			(8:) '{DBGOUT_RFC_b[31:0]}'
			(9:) '{DBGOUT_RFC_a[31:0]}'
			(a:) '{dbg_ofdm}'
			(b:) '{dbg_cck}'
		*/
	}
}

void
phydm_bb_dbg_port_clock_en(
	void			*p_dm_void,
	u8			enable
) {
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32		reg_value = 0;
	
	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8814A | ODM_RTL8814B)) {
		
		reg_value = (enable == true) ? 0x7 : 0;
		odm_set_bb_reg(p_dm, 0x198c, 0x7, reg_value); /*enable/disable debug port clock, for power saving*/
	}
}

u8
phydm_set_bb_dbg_port(
	void			*p_dm_void,
	u8			curr_dbg_priority,
	u32			debug_port
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	dbg_port_result = false;

	if (curr_dbg_priority > p_dm->pre_dbg_priority) {

		if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
			
			phydm_bb_dbg_port_clock_en(p_dm, true);
			
			odm_set_bb_reg(p_dm, 0x8fc, MASKDWORD, debug_port);
			/**/
		} else /*if (p_dm->support_ic_type & ODM_IC_11N_SERIES)*/ {
			odm_set_bb_reg(p_dm, 0x908, MASKDWORD, debug_port);
			/**/
		}
		PHYDM_DBG(p_dm, ODM_COMP_API, ("DbgPort ((0x%x)) set success, Cur_priority=((%d)), Pre_priority=((%d))\n", debug_port, curr_dbg_priority, p_dm->pre_dbg_priority));
		p_dm->pre_dbg_priority = curr_dbg_priority;
		dbg_port_result = true;
	}
		
	return dbg_port_result;
}

void
phydm_release_bb_dbg_port(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	phydm_bb_dbg_port_clock_en(p_dm, false);
	phydm_bb_dbg_port_header_sel(p_dm, 0);

	p_dm->pre_dbg_priority = BB_DBGPORT_RELEASE;
	PHYDM_DBG(p_dm, ODM_COMP_API, ("Release BB dbg_port\n"));
}

u32
phydm_get_bb_dbg_port_value(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	dbg_port_value = 0;

	if (p_dm->support_ic_type & ODM_IC_11AC_SERIES) {
		dbg_port_value = odm_get_bb_reg(p_dm, 0xfa0, MASKDWORD);
		/**/
	} else /*if (p_dm->support_ic_type & ODM_IC_11N_SERIES)*/ {
		dbg_port_value = odm_get_bb_reg(p_dm, 0xdf4, MASKDWORD);
		/**/
	}
	PHYDM_DBG(p_dm, ODM_COMP_API, ("dbg_port_value = 0x%x\n", dbg_port_value));
	return	dbg_port_value;
}

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION

void
phydm_bb_debug_info_n_series(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	u32	value32 = 0, value32_1 = 0;
	u8	rf_gain_a = 0, rf_gain_b = 0, rf_gain_c = 0, rf_gain_d = 0;
	u8	rx_snr_a = 0, rx_snr_b = 0, rx_snr_c = 0, rx_snr_d = 0;

	s8    rxevm_0 = 0, rxevm_1 = 0;
	s32	short_cfo_a = 0, short_cfo_b = 0, long_cfo_a = 0, long_cfo_b = 0;
	s32	scfo_a = 0, scfo_b = 0, avg_cfo_a = 0, avg_cfo_b = 0;
	s32	cfo_end_a = 0, cfo_end_b = 0, acq_cfo_a = 0, acq_cfo_b = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s\n", "BB Report Info"));

	/*AGC result*/
	value32 = odm_get_bb_reg(p_dm, 0xdd0, MASKDWORD);
	rf_gain_a = (u8)(value32 & 0x3f);
	rf_gain_a = rf_gain_a << 1;

	rf_gain_b = (u8)((value32 >> 8) & 0x3f);
	rf_gain_b = rf_gain_b << 1;

	rf_gain_c = (u8)((value32 >> 16) & 0x3f);
	rf_gain_c = rf_gain_c << 1;

	rf_gain_d = (u8)((value32 >> 24) & 0x3f);
	rf_gain_d = rf_gain_d << 1;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", rf_gain_a, rf_gain_b, rf_gain_c, rf_gain_d));

	/*SNR report*/
	value32 = odm_get_bb_reg(p_dm, 0xdd4, MASKDWORD);
	rx_snr_a = (u8)(value32 & 0xff);
	rx_snr_a = rx_snr_a >> 1;

	rx_snr_b = (u8)((value32 >> 8) & 0xff);
	rx_snr_b = rx_snr_b >> 1;

	rx_snr_c = (u8)((value32 >> 16) & 0xff);
	rx_snr_c = rx_snr_c >> 1;

	rx_snr_d = (u8)((value32 >> 24) & 0xff);
	rx_snr_d = rx_snr_d >> 1;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", rx_snr_a, rx_snr_b, rx_snr_c, rx_snr_d));

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(p_dm, 0xdd8, MASKDWORD);

	rxevm_0 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_0 /= 2;
	if (rxevm_0 < -63)
		rxevm_0 = 0;

	rxevm_1 = (s8)((value32 & MASKBYTE3) >> 24);
	rxevm_1 /= 2;
	if (rxevm_1 < -63)
		rxevm_1 = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "RXEVM (1ss/2ss)", rxevm_0, rxevm_1));

	/*CFO Report Info*/
	odm_set_bb_reg(p_dm, 0xd00, BIT(26), 1);

	/*Short CFO*/
	value32 = odm_get_bb_reg(p_dm, 0xdac, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xdb0, MASKDWORD);

	short_cfo_b = (s32)(value32 & 0xfff);			/*S(12,11)*/
	short_cfo_a = (s32)((value32 & 0x0fff0000) >> 16);

	long_cfo_b = (s32)(value32_1 & 0x1fff);		/*S(13,12)*/
	long_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	/*SFO 2's to dec*/
	if (short_cfo_a > 2047)
		short_cfo_a = short_cfo_a - 4096;
	if (short_cfo_b > 2047)
		short_cfo_b = short_cfo_b - 4096;

	short_cfo_a = (short_cfo_a * 312500) / 2048;
	short_cfo_b = (short_cfo_b * 312500) / 2048;

	/*LFO 2's to dec*/

	if (long_cfo_a > 4095)
		long_cfo_a = long_cfo_a - 8192;

	if (long_cfo_b > 4095)
		long_cfo_b = long_cfo_b - 8192;

	long_cfo_a = long_cfo_a * 312500 / 4096;
	long_cfo_b = long_cfo_b * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "CFO Report Info"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "Short CFO(Hz) <A/B>", short_cfo_a, short_cfo_b));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "Long CFO(Hz) <A/B>", long_cfo_a, long_cfo_b));

	/*SCFO*/
	value32 = odm_get_bb_reg(p_dm, 0xdb8, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xdb4, MASKDWORD);

	scfo_b = (s32)(value32 & 0x7ff);			/*S(11,10)*/
	scfo_a = (s32)((value32 & 0x07ff0000) >> 16);

	if (scfo_a > 1023)
		scfo_a = scfo_a - 2048;

	if (scfo_b > 1023)
		scfo_b = scfo_b - 2048;

	scfo_a = scfo_a * 312500 / 1024;
	scfo_b = scfo_b * 312500 / 1024;

	avg_cfo_b = (s32)(value32_1 & 0x1fff);	/*S(13,12)*/
	avg_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	if (avg_cfo_a > 4095)
		avg_cfo_a = avg_cfo_a - 8192;

	if (avg_cfo_b > 4095)
		avg_cfo_b = avg_cfo_b - 8192;

	avg_cfo_a = avg_cfo_a * 312500 / 4096;
	avg_cfo_b = avg_cfo_b * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "value SCFO(Hz) <A/B>", scfo_a, scfo_b));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "Avg CFO(Hz) <A/B>", avg_cfo_a, avg_cfo_b));

	value32 = odm_get_bb_reg(p_dm, 0xdbc, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xde0, MASKDWORD);

	cfo_end_b = (s32)(value32 & 0x1fff);		/*S(13,12)*/
	cfo_end_a = (s32)((value32 & 0x1fff0000) >> 16);

	if (cfo_end_a > 4095)
		cfo_end_a = cfo_end_a - 8192;

	if (cfo_end_b > 4095)
		cfo_end_b = cfo_end_b - 8192;

	cfo_end_a = cfo_end_a * 312500 / 4096;
	cfo_end_b = cfo_end_b * 312500 / 4096;

	acq_cfo_b = (s32)(value32_1 & 0x1fff);	/*S(13,12)*/
	acq_cfo_a = (s32)((value32_1 & 0x1fff0000) >> 16);

	if (acq_cfo_a > 4095)
		acq_cfo_a = acq_cfo_a - 8192;

	if (acq_cfo_b > 4095)
		acq_cfo_b = acq_cfo_b - 8192;

	acq_cfo_a = acq_cfo_a * 312500 / 4096;
	acq_cfo_b = acq_cfo_b * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "End CFO(Hz) <A/B>", cfo_end_a, cfo_end_b));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "ACQ CFO(Hz) <A/B>", acq_cfo_a, acq_cfo_b));

}


void
phydm_bb_debug_info(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	char *tmp_string = NULL;

	u8	RX_HT_BW, RX_VHT_BW, RXSC, RX_HT, RX_BW;
	static u8 v_rx_bw ;
	u32	value32, value32_1, value32_2, value32_3;
	s32	SFO_A, SFO_B, SFO_C, SFO_D;
	s32	LFO_A, LFO_B, LFO_C, LFO_D;
	static u8	MCSS, tail, parity, rsv, vrsv, idx, smooth, htsound, agg, stbc, vstbc, fec, fecext, sgi, sgiext, htltf, vgid, v_nsts, vtxops, vrsv2, vbrsv, bf, vbcrc;
	static u16	h_length, htcrc8, length;
	static u16 vpaid;
	static u16	v_length, vhtcrc8, v_mcss, v_tail, vb_tail;
	static u8	HMCSS, HRX_BW;

	u8    pwdb;
	s8    RXEVM_0, RXEVM_1, RXEVM_2 ;
	u8    rf_gain_path_a, rf_gain_path_b, rf_gain_path_c, rf_gain_path_d;
	u8    rx_snr_path_a, rx_snr_path_b, rx_snr_path_c, rx_snr_path_d;
	s32    sig_power;

	const char *L_rate[8] = {"6M", "9M", "12M", "18M", "24M", "36M", "48M", "54M"};

#if 0
	const double evm_comp_20M = 0.579919469776867; /* 10*log10(64.0/56.0) */
	const double evm_comp_40M = 0.503051183113957; /* 10*log10(128.0/114.0) */
	const double evm_comp_80M = 0.244245993314183; /* 10*log10(256.0/242.0) */
	const double evm_comp_160M = 0.244245993314183; /* 10*log10(512.0/484.0) */
#endif

	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		phydm_bb_debug_info_n_series(p_dm, &used, output, &out_len);
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s\n", "BB Report Info"));

	/*BW & mode Detection*/

	value32 = odm_get_bb_reg(p_dm, 0xf80, MASKDWORD);
	value32_2 = value32;
	RX_HT_BW = (u8)(value32 & 0x1);
	RX_VHT_BW = (u8)((value32 >> 1) & 0x3);
	RXSC = (u8)(value32 & 0x78);
	value32_1 = (value32 & 0x180) >> 7;
	RX_HT = (u8)(value32_1);

	RX_BW = 0;

	if (RX_HT == 2) {
		if (RX_VHT_BW == 0)
			tmp_string = "20M";
		else if (RX_VHT_BW == 1)
			tmp_string = "40M";
		else
			tmp_string = "80M";
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s %s", "mode", "VHT", tmp_string));
		RX_BW = RX_VHT_BW;
	} else if (RX_HT == 1) {
		if (RX_HT_BW == 0)
			tmp_string = "20M";
		else if (RX_HT_BW == 1)
			tmp_string = "40M";
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s %s", "mode", "HT", tmp_string));
		RX_BW = RX_HT_BW;
	} else
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s %s", "mode", "Legacy"));

	if (RX_HT != 0) {
		if (RXSC == 0)
			tmp_string = "duplicate/full bw";
		else if (RXSC == 1)
			tmp_string = "usc20-1";
		else if (RXSC == 2)
			tmp_string = "lsc20-1";
		else if (RXSC == 3)
			tmp_string = "usc20-2";
		else if (RXSC == 4)
			tmp_string = "lsc20-2";
		else if (RXSC == 9)
			tmp_string = "usc40";
		else if (RXSC == 10)
			tmp_string = "lsc40";
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s", tmp_string));
	}

	/* RX signal power and AGC related info*/

	value32 = odm_get_bb_reg(p_dm, 0xF90, MASKDWORD);
	pwdb = (u8)((value32 & MASKBYTE1) >> 8);
	pwdb = pwdb >> 1;
	sig_power = -110 + pwdb;
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power));

	value32 = odm_get_bb_reg(p_dm, 0xd14, MASKDWORD);
	rx_snr_path_a = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_a = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_a *= 2;
	value32 = odm_get_bb_reg(p_dm, 0xd54, MASKDWORD);
	rx_snr_path_b = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_b = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_b *= 2;
	value32 = odm_get_bb_reg(p_dm, 0xd94, MASKDWORD);
	rx_snr_path_c = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_c = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_c *= 2;
	value32 = odm_get_bb_reg(p_dm, 0xdd4, MASKDWORD);
	rx_snr_path_d = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_d = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_d *= 2;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", rf_gain_path_a, rf_gain_path_b, rf_gain_path_c, rf_gain_path_d));


	/* RX counter related info*/

	value32 = odm_get_bb_reg(p_dm, 0xF08, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM CCA counter", ((value32 & 0xFFFF0000) >> 16)));

	value32 = odm_get_bb_reg(p_dm, 0xFD0, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM SBD Fail counter", value32 & 0xFFFF));

	value32 = odm_get_bb_reg(p_dm, 0xFC4, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	value32 = odm_get_bb_reg(p_dm, 0xFCC, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "CCK CCA counter", value32 & 0xFFFF));

	value32 = odm_get_bb_reg(p_dm, 0xFBC, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "LSIG (parity Fail/rate Illegal) counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	value32_1 = odm_get_bb_reg(p_dm, 0xFC8, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm, 0xFC0, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2 & 0xFFFF0000) >> 16), value32_1 & 0xFFFF));

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(p_dm, 0xF8c, MASKDWORD);
	RXEVM_0 = (s8)((value32 & MASKBYTE2) >> 16);
	RXEVM_0 /= 2;
	if (RXEVM_0 < -63)
		RXEVM_0 = 0;

	RXEVM_1 = (s8)((value32 & MASKBYTE3) >> 24);
	RXEVM_1 /= 2;
	value32 = odm_get_bb_reg(p_dm, 0xF88, MASKDWORD);
	RXEVM_2 = (s8)((value32 & MASKBYTE2) >> 16);
	RXEVM_2 /= 2;

	if (RXEVM_1 < -63)
		RXEVM_1 = 0;
	if (RXEVM_2 < -63)
		RXEVM_2 = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", rx_snr_path_a, rx_snr_path_b, rx_snr_path_c, rx_snr_path_d));

	value32 = odm_get_bb_reg(p_dm, 0xF8C, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	/*BW & mode Detection*/

	/*Reset Page F counter*/
	odm_set_bb_reg(p_dm, 0xB58, BIT(0), 1);
	odm_set_bb_reg(p_dm, 0xB58, BIT(0), 0);

	/*CFO Report Info*/
	/*Short CFO*/
	value32 = odm_get_bb_reg(p_dm, 0xd0c, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xd4c, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm, 0xd8c, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm, 0xdcc, MASKDWORD);

	SFO_A = (s32)(value32 & 0xfff);
	SFO_B = (s32)(value32_1 & 0xfff);
	SFO_C = (s32)(value32_2 & 0xfff);
	SFO_D = (s32)(value32_3 & 0xfff);

	LFO_A = (s32)(value32 >> 16);
	LFO_B = (s32)(value32_1 >> 16);
	LFO_C = (s32)(value32_2 >> 16);
	LFO_D = (s32)(value32_3 >> 16);

	/*SFO 2's to dec*/
	if (SFO_A > 2047)
		SFO_A = SFO_A - 4096;
	SFO_A = (SFO_A * 312500) / 2048;
	if (SFO_B > 2047)
		SFO_B = SFO_B - 4096;
	SFO_B = (SFO_B * 312500) / 2048;
	if (SFO_C > 2047)
		SFO_C = SFO_C - 4096;
	SFO_C = (SFO_C * 312500) / 2048;
	if (SFO_D > 2047)
		SFO_D = SFO_D - 4096;
	SFO_D = (SFO_D * 312500) / 2048;

	/*LFO 2's to dec*/

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;
	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "CFO Report Info"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "Short CFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "Long CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	/*SCFO*/
	value32 = odm_get_bb_reg(p_dm, 0xd10, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xd50, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm, 0xd90, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm, 0xdd0, MASKDWORD);

	SFO_A = (s32)(value32 & 0x7ff);
	SFO_B = (s32)(value32_1 & 0x7ff);
	SFO_C = (s32)(value32_2 & 0x7ff);
	SFO_D = (s32)(value32_3 & 0x7ff);

	if (SFO_A > 1023)
		SFO_A = SFO_A - 2048;

	if (SFO_B > 2047)
		SFO_B = SFO_B - 4096;

	if (SFO_C > 2047)
		SFO_C = SFO_C - 4096;

	if (SFO_D > 2047)
		SFO_D = SFO_D - 4096;

	SFO_A = SFO_A * 312500 / 1024;
	SFO_B = SFO_B * 312500 / 1024;
	SFO_C = SFO_C * 312500 / 1024;
	SFO_D = SFO_D * 312500 / 1024;

	LFO_A = (s32)(value32 >> 16);
	LFO_B = (s32)(value32_1 >> 16);
	LFO_C = (s32)(value32_2 >> 16);
	LFO_D = (s32)(value32_3 >> 16);

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;
	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "value SCFO(Hz) <A/B/C/D>", SFO_A, SFO_B, SFO_C, SFO_D));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "ACQ CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	value32 = odm_get_bb_reg(p_dm, 0xd14, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm, 0xd54, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm, 0xd94, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm, 0xdd4, MASKDWORD);

	LFO_A = (s32)(value32 >> 16);
	LFO_B = (s32)(value32_1 >> 16);
	LFO_C = (s32)(value32_2 >> 16);
	LFO_D = (s32)(value32_3 >> 16);

	if (LFO_A > 4095)
		LFO_A = LFO_A - 8192;

	if (LFO_B > 4095)
		LFO_B = LFO_B - 8192;

	if (LFO_C > 4095)
		LFO_C = LFO_C - 8192;

	if (LFO_D > 4095)
		LFO_D = LFO_D - 8192;

	LFO_A = LFO_A * 312500 / 4096;
	LFO_B = LFO_B * 312500 / 4096;
	LFO_C = LFO_C * 312500 / 4096;
	LFO_D = LFO_D * 312500 / 4096;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d /%d", "End CFO(Hz) <A/B/C/D>", LFO_A, LFO_B, LFO_C, LFO_D));

	value32 = odm_get_bb_reg(p_dm, 0xf20, MASKDWORD);  /*L SIG*/

	tail = (u8)((value32 & 0xfc0000) >> 16);
	parity = (u8)((value32 & 0x20000) >> 16);
	length = (u16)((value32 & 0x1ffe00) >> 8);
	rsv = (u8)(value32 & 0x10);
	MCSS = (u8)(value32 & 0x0f);

	switch (MCSS) {
	case 0x0b:
		idx = 0;
		break;
	case 0x0f:
		idx = 1;
		break;
	case 0x0a:
		idx = 2;
		break;
	case 0x0e:
		idx = 3;
		break;
	case 0x09:
		idx = 4;
		break;
	case 0x08:
		idx = 5;
		break;
	case 0x0c:
		idx = 6;
		break;
	default:
		idx = 6;
		break;

	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "L-SIG"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s : %s", "rate", L_rate[idx]));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x", "Rsv/length/parity", rsv, RX_BW, length));

	value32 = odm_get_bb_reg(p_dm, 0xf2c, MASKDWORD);  /*HT SIG*/
	if (RX_HT == 1) {

		HMCSS = (u8)(value32 & 0x7F);
		HRX_BW = (u8)(value32 & 0x80);
		h_length = (u16)((value32 >> 8) & 0xffff);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "HT-SIG1"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x", "MCS/BW/length", HMCSS, HRX_BW, h_length));

	value32 = odm_get_bb_reg(p_dm, 0xf30, MASKDWORD);  /*HT SIG*/

	if (RX_HT == 1) {
		smooth = (u8)(value32 & 0x01);
		htsound = (u8)(value32 & 0x02);
		rsv = (u8)(value32 & 0x04);
		agg = (u8)(value32 & 0x08);
		stbc = (u8)(value32 & 0x30);
		fec = (u8)(value32 & 0x40);
		sgi = (u8)(value32 & 0x80);
		htltf = (u8)((value32 & 0x300) >> 8);
		htcrc8 = (u16)((value32 & 0x3fc00) >> 8);
		tail = (u8)((value32 & 0xfc0000) >> 16);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "HT-SIG2"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x", "Smooth/NoSound/Rsv/Aggregate/STBC/LDPC", smooth, htsound, rsv, agg, stbc, fec));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x", "SGI/E-HT-LTFs/CRC/tail", sgi, htltf, htcrc8, tail));

	value32 = odm_get_bb_reg(p_dm, 0xf2c, MASKDWORD);  /*VHT SIG A1*/
	if (RX_HT == 2) {
		/* value32 = odm_get_bb_reg(p_dm, 0xf2c,MASKDWORD);*/
		v_rx_bw = (u8)(value32 & 0x03);
		vrsv = (u8)(value32 & 0x04);
		vstbc = (u8)(value32 & 0x08);
		vgid = (u8)((value32 & 0x3f0) >> 4);
		v_nsts = (u8)(((value32 & 0x1c00) >> 8) + 1);
		vpaid = (u16)(value32 & 0x3fe);
		vtxops = (u8)((value32 & 0x400000) >> 20);
		vrsv2 = (u8)((value32 & 0x800000) >> 20);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-A1"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x / %x", "BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", v_rx_bw, vrsv, vstbc, vgid, v_nsts, vpaid, vtxops, vrsv2));

	value32 = odm_get_bb_reg(p_dm, 0xf30, MASKDWORD);  /*VHT SIG*/

	if (RX_HT == 2) {
		/*value32 = odm_get_bb_reg(p_dm, 0xf30,MASKDWORD); */  /*VHT SIG*/

		/* sgi=(u8)(value32&0x01); */
		sgiext = (u8)(value32 & 0x03);
		/* fec = (u8)(value32&0x04); */
		fecext = (u8)(value32 & 0x0C);

		v_mcss = (u8)(value32 & 0xf0);
		bf = (u8)((value32 & 0x100) >> 8);
		vrsv = (u8)((value32 & 0x200) >> 8);
		vhtcrc8 = (u16)((value32 & 0x3fc00) >> 8);
		v_tail = (u8)((value32 & 0xfc0000) >> 16);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-A2"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x", "SGI/FEC/MCS/BF/Rsv/CRC/tail", sgiext, fecext, v_mcss, bf, vrsv, vhtcrc8, v_tail));

	value32 = odm_get_bb_reg(p_dm, 0xf34, MASKDWORD);  /*VHT SIG*/
	{
		v_length = (u16)(value32 & 0x1fffff);
		vbrsv = (u8)((value32 & 0x600000) >> 20);
		vb_tail = (u16)((value32 & 0x1f800000) >> 20);
		vbcrc = (u8)((value32 & 0x80000000) >> 28);

	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-B"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x", "length/Rsv/tail/CRC", v_length, vbrsv, vb_tail, vbcrc));

	/*for Condition number*/
	if (p_dm->support_ic_type & ODM_RTL8822B) {
		s32	condition_num = 0;
		char *factor = NULL;

		odm_set_bb_reg(p_dm, 0x1988, BIT(22), 0x1);	/*enable report condition number*/

		condition_num = odm_get_bb_reg(p_dm, 0xf84, MASKDWORD);
		condition_num = (condition_num & 0x3ffff) >> 4;

		if (*p_dm->p_band_width == CHANNEL_WIDTH_80)
			factor = "256/234";
		else if (*p_dm->p_band_width == CHANNEL_WIDTH_40)
			factor = "128/108";
		else if (*p_dm->p_band_width == CHANNEL_WIDTH_20) {
			if (RX_HT != 2 || RX_HT != 1)
				factor = "64/52";	/*HT or VHT*/
			else
				factor = "64/48";	/*legacy*/
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d (factor = %s)", "Condition number", condition_num, factor));

	}
	*_used = used;
	*_out_len = out_len;

}
#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
void phydm_sbd_check(
	struct PHY_DM_STRUCT					*p_dm
)
{
	static u32	pkt_cnt = 0;
	static boolean sbd_state = 0;
	u32	sym_count, count, value32;

	if (sbd_state == 0) {
		pkt_cnt++;
		if (pkt_cnt % 5 == 0) { /*read SBD conter once every 5 packets*/
			odm_set_timer(p_dm, &p_dm->sbdcnt_timer, 0); /*ms*/
			sbd_state = 1;
		}
	} else { /*read counter*/
		value32 = odm_get_bb_reg(p_dm, 0xF98, MASKDWORD);
		sym_count = (value32 & 0x7C000000) >> 26;
		count = (value32 & 0x3F00000) >> 20;
		dbg_print("#SBD#    sym_count   %d   count   %d\n", sym_count, count);
		sbd_state = 0;
	}
}
#endif

void phydm_sbd_callback(
	struct timer_list		*p_timer
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_timer->Adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;

	#if USE_WORKITEM
	odm_schedule_work_item(&p_dm->sbdcnt_workitem);
	#else
	phydm_sbd_check(p_dm);
	#endif
#endif
}

void phydm_sbd_workitem_callback(
	void            *p_context
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct _ADAPTER	*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm = &p_hal_data->DM_OutSrc;

	phydm_sbd_check(p_dm);
#endif
}
#endif

void
phydm_reset_rx_rate_distribution(
	struct PHY_DM_STRUCT	*p_dm_odm
)
{
	struct _odm_phy_dbg_info_		*p_dbg = &(p_dm_odm->phy_dbg_info);

	odm_memory_set(p_dm_odm, &(p_dbg->num_qry_legacy_pkt[0]), 0, (LEGACY_RATE_NUM * 2));
	odm_memory_set(p_dm_odm, &(p_dbg->num_qry_ht_pkt[0]), 0, (HT_RATE_NUM * 2));
	p_dbg->ht_pkt_not_zero = false;
	
#if	ODM_IC_11AC_SERIES_SUPPORT
	odm_memory_set(p_dm_odm, &(p_dbg->num_qry_vht_pkt[0]), 0, (VHT_RATE_NUM * 2));
	p_dbg->vht_pkt_not_zero = false;
#endif
}

void
phydm_rx_rate_distribution
(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _odm_phy_dbg_info_		*p_dbg = &(p_dm->phy_dbg_info);
	u8	i = 0, j = 0;
	u8	rate_num = 1, rate_ss_shift = 0;

	if (p_dm->support_ic_type & ODM_IC_4SS)
		rate_num = 4;
	else if (p_dm->support_ic_type & ODM_IC_3SS)
		rate_num = 3;
	else if (p_dm->support_ic_type & ODM_IC_2SS)
		rate_num = 2;

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[RxRate Cnt] =============>\n"));

	/*======CCK=============================================================*/
	if (*(p_dm->p_channel) <= 14) {
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* CCK = {%d, %d, %d, %d}\n",
			p_dbg->num_qry_legacy_pkt[0],
			p_dbg->num_qry_legacy_pkt[1],
			p_dbg->num_qry_legacy_pkt[2],
			p_dbg->num_qry_legacy_pkt[3]
			));
	}
	/*======OFDM============================================================*/
	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* OFDM = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
		p_dbg->num_qry_legacy_pkt[4], p_dbg->num_qry_legacy_pkt[5],
		p_dbg->num_qry_legacy_pkt[6], p_dbg->num_qry_legacy_pkt[7],
		p_dbg->num_qry_legacy_pkt[8], p_dbg->num_qry_legacy_pkt[9],
		p_dbg->num_qry_legacy_pkt[10], p_dbg->num_qry_legacy_pkt[11]));

	/*======HT==============================================================*/
	if (p_dbg->ht_pkt_not_zero) {
		
		for (i = 0; i < rate_num; i++) {
			
			rate_ss_shift = (i << 3);
			
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* HT MCS[%d :%d ] = {%d, %d, %d, %d, %d, %d, %d, %d}\n",
				(rate_ss_shift), (rate_ss_shift+7),
				p_dbg->num_qry_ht_pkt[rate_ss_shift + 0], p_dbg->num_qry_ht_pkt[rate_ss_shift + 1],
				p_dbg->num_qry_ht_pkt[rate_ss_shift + 2], p_dbg->num_qry_ht_pkt[rate_ss_shift + 3],
				p_dbg->num_qry_ht_pkt[rate_ss_shift + 4], p_dbg->num_qry_ht_pkt[rate_ss_shift + 5],
				p_dbg->num_qry_ht_pkt[rate_ss_shift + 6], p_dbg->num_qry_ht_pkt[rate_ss_shift + 7]));

		}
	}
	
#if	ODM_IC_11AC_SERIES_SUPPORT
	/*======VHT=============================================================*/
	if (p_dbg->vht_pkt_not_zero){
		
		for (i = 0; i < rate_num; i++) {
			
			rate_ss_shift = 10 * i;
			
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* VHT-%d ss MCS[0:9] = {%d, %d, %d, %d, %d, %d, %d, %d, %d, %d}\n",
				(i + 1),
				p_dbg->num_qry_vht_pkt[rate_ss_shift + 0], p_dbg->num_qry_vht_pkt[rate_ss_shift + 1],
				p_dbg->num_qry_vht_pkt[rate_ss_shift + 2], p_dbg->num_qry_vht_pkt[rate_ss_shift + 3],
				p_dbg->num_qry_vht_pkt[rate_ss_shift + 4], p_dbg->num_qry_vht_pkt[rate_ss_shift + 5],
				p_dbg->num_qry_vht_pkt[rate_ss_shift + 6], p_dbg->num_qry_vht_pkt[rate_ss_shift + 7],
				p_dbg->num_qry_vht_pkt[rate_ss_shift + 8], p_dbg->num_qry_vht_pkt[rate_ss_shift + 9]));

		}
	}
#endif
	
}

void
phydm_get_avg_phystatus_val
(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_phystatus_statistic		*p_dbg_statistic = &(p_dm->phy_dbg_info.phystatus_statistic_info);
	struct phydm_phystatus_avg		*p_dbg_avg = &(p_dm->phy_dbg_info.phystatus_statistic_avg);
	
	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[Avg PHY Statistic] ==============>\n"));

	phydm_reset_phystatus_avg(p_dm);

	/*CCK*/
	p_dbg_avg->rssi_cck_avg = (u8)((p_dbg_statistic->rssi_cck_cnt != 0) ? (p_dbg_statistic->rssi_cck_sum/p_dbg_statistic->rssi_cck_cnt) : 0);
	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* cck Cnt= ((%d)) RSSI:{%d}\n", p_dbg_statistic->rssi_cck_cnt, p_dbg_avg->rssi_cck_avg));
	
	/*OFDM*/
	if (p_dbg_statistic->rssi_ofdm_cnt != 0) {
		p_dbg_avg->rssi_ofdm_avg = (u8)(p_dbg_statistic->rssi_ofdm_sum/p_dbg_statistic->rssi_ofdm_cnt);
		p_dbg_avg->evm_ofdm_avg = (u8)(p_dbg_statistic->evm_ofdm_sum/p_dbg_statistic->rssi_ofdm_cnt);
		p_dbg_avg->snr_ofdm_avg = (u8)(p_dbg_statistic->snr_ofdm_sum/p_dbg_statistic->rssi_ofdm_cnt);
	}

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* ofdm Cnt= ((%d)) RSSI:{%d} EVM:{%d} SNR:{%d}\n",
		p_dbg_statistic->rssi_ofdm_cnt, p_dbg_avg->rssi_ofdm_avg, p_dbg_avg->evm_ofdm_avg, p_dbg_avg->snr_ofdm_avg));
	
	if (p_dbg_statistic->rssi_1ss_cnt != 0) {
		p_dbg_avg->rssi_1ss_avg = (u8)(p_dbg_statistic->rssi_1ss_sum/p_dbg_statistic->rssi_1ss_cnt);
		p_dbg_avg->evm_1ss_avg = (u8)(p_dbg_statistic->evm_1ss_sum/p_dbg_statistic->rssi_1ss_cnt);
		p_dbg_avg->snr_1ss_avg = (u8)(p_dbg_statistic->snr_1ss_sum/p_dbg_statistic->rssi_1ss_cnt);
	}

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* 1-ss Cnt= ((%d)) RSSI:{%d} EVM:{%d} SNR:{%d}\n",
		p_dbg_statistic->rssi_1ss_cnt, p_dbg_avg->rssi_1ss_avg, p_dbg_avg->evm_1ss_avg, p_dbg_avg->snr_1ss_avg));

	#if (defined(PHYDM_COMPILE_ABOVE_2SS))
	if (p_dm->support_ic_type & (PHYDM_IC_ABOVE_2SS)) {

		if (p_dbg_statistic->rssi_2ss_cnt != 0) {
			p_dbg_avg->rssi_2ss_avg[0] = (u8)(p_dbg_statistic->rssi_2ss_sum[0] /p_dbg_statistic->rssi_2ss_cnt);
			p_dbg_avg->rssi_2ss_avg[1] = (u8)(p_dbg_statistic->rssi_2ss_sum[1] /p_dbg_statistic->rssi_2ss_cnt);
			
			p_dbg_avg->evm_2ss_avg[0] = (u8)(p_dbg_statistic->evm_2ss_sum[0] /p_dbg_statistic->rssi_2ss_cnt);
			p_dbg_avg->evm_2ss_avg[1] = (u8)(p_dbg_statistic->evm_2ss_sum[1] /p_dbg_statistic->rssi_2ss_cnt);
			
			p_dbg_avg->snr_2ss_avg[0] = (u8)(p_dbg_statistic->snr_2ss_sum[0] /p_dbg_statistic->rssi_2ss_cnt);
			p_dbg_avg->snr_2ss_avg[1] = (u8)(p_dbg_statistic->snr_2ss_sum[1] /p_dbg_statistic->rssi_2ss_cnt);
		}
		
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* 2-ss Cnt= ((%d)) RSSI:{%d, %d}, EVM:{%d, %d}, SNR:{%d, %d}\n",
			p_dbg_statistic->rssi_2ss_cnt, 
			p_dbg_avg->rssi_2ss_avg[0], p_dbg_avg->rssi_2ss_avg[1], 
			p_dbg_avg->evm_2ss_avg[0], p_dbg_avg->evm_2ss_avg[1], 
			p_dbg_avg->snr_2ss_avg[0], p_dbg_avg->snr_2ss_avg[1]));
	}
	#endif

	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if (p_dm->support_ic_type & (PHYDM_IC_ABOVE_3SS)) {

		if (p_dbg_statistic->rssi_3ss_cnt != 0) {
			p_dbg_avg->rssi_3ss_avg[0] = (u8)(p_dbg_statistic->rssi_3ss_sum[0] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->rssi_3ss_avg[1] = (u8)(p_dbg_statistic->rssi_3ss_sum[1] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->rssi_3ss_avg[2] = (u8)(p_dbg_statistic->rssi_3ss_sum[2] /p_dbg_statistic->rssi_3ss_cnt);
			
			p_dbg_avg->evm_3ss_avg[0] = (u8)(p_dbg_statistic->evm_3ss_sum[0] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->evm_3ss_avg[1] = (u8)(p_dbg_statistic->evm_3ss_sum[1] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->evm_3ss_avg[2] = (u8)(p_dbg_statistic->evm_3ss_sum[2] /p_dbg_statistic->rssi_3ss_cnt);

			p_dbg_avg->snr_3ss_avg[0] = (u8)(p_dbg_statistic->snr_3ss_sum[0] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->snr_3ss_avg[1] = (u8)(p_dbg_statistic->snr_3ss_sum[1] /p_dbg_statistic->rssi_3ss_cnt);
			p_dbg_avg->snr_3ss_avg[2] = (u8)(p_dbg_statistic->snr_3ss_sum[2] /p_dbg_statistic->rssi_3ss_cnt);
		}
		
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* 3-ss Cnt= ((%d)) RSSI:{%d, %d, %d} EVM:{%d, %d, %d} SNR:{%d, %d, %d}\n",
			p_dbg_statistic->rssi_3ss_cnt, 
			p_dbg_avg->rssi_3ss_avg[0], p_dbg_avg->rssi_3ss_avg[1], p_dbg_avg->rssi_3ss_avg[2],
			p_dbg_avg->evm_3ss_avg[0], p_dbg_avg->evm_3ss_avg[1], p_dbg_avg->evm_3ss_avg[2],
			p_dbg_avg->snr_3ss_avg[0], p_dbg_avg->snr_3ss_avg[1], p_dbg_avg->snr_3ss_avg[2]));
	}
	#endif

	#if (defined(PHYDM_COMPILE_ABOVE_4SS))
	if (p_dm->support_ic_type & PHYDM_IC_ABOVE_4SS) {

		if (p_dbg_statistic->rssi_4ss_cnt != 0) {
			p_dbg_avg->rssi_4ss_avg[0] = (u8)(p_dbg_statistic->rssi_4ss_sum[0] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->rssi_4ss_avg[1] = (u8)(p_dbg_statistic->rssi_4ss_sum[1] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->rssi_4ss_avg[2] = (u8)(p_dbg_statistic->rssi_4ss_sum[2] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->rssi_4ss_avg[3] = (u8)(p_dbg_statistic->rssi_4ss_sum[3] /p_dbg_statistic->rssi_4ss_cnt);

			p_dbg_avg->evm_4ss_avg[0] = (u8)(p_dbg_statistic->evm_4ss_sum[0] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->evm_4ss_avg[1] = (u8)(p_dbg_statistic->evm_4ss_sum[1] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->evm_4ss_avg[2] = (u8)(p_dbg_statistic->evm_4ss_sum[2] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->evm_4ss_avg[3] = (u8)(p_dbg_statistic->evm_4ss_sum[3] /p_dbg_statistic->rssi_4ss_cnt);

			p_dbg_avg->snr_4ss_avg[0] = (u8)(p_dbg_statistic->snr_4ss_sum[0] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->snr_4ss_avg[1] = (u8)(p_dbg_statistic->snr_4ss_sum[1] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->snr_4ss_avg[2] = (u8)(p_dbg_statistic->snr_4ss_sum[2] /p_dbg_statistic->rssi_4ss_cnt);
			p_dbg_avg->snr_4ss_avg[3] = (u8)(p_dbg_statistic->snr_4ss_sum[3] /p_dbg_statistic->rssi_4ss_cnt);
		}
		
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("* 4-ss Cnt= ((%d)) RSSI:{%d, %d, %d, %d} EVM:{%d, %d, %d, %d} SNR:{%d, %d, %d, %d}\n",
			p_dbg_statistic->rssi_4ss_cnt, 
			p_dbg_avg->rssi_4ss_avg[0], p_dbg_avg->rssi_4ss_avg[1], p_dbg_avg->rssi_4ss_avg[2], p_dbg_avg->rssi_4ss_avg[3],
			p_dbg_avg->evm_4ss_avg[0], p_dbg_avg->evm_4ss_avg[1], p_dbg_avg->evm_4ss_avg[2], p_dbg_avg->evm_4ss_avg[3],
			p_dbg_avg->snr_4ss_avg[0], p_dbg_avg->snr_4ss_avg[1], p_dbg_avg->snr_4ss_avg[2], p_dbg_avg->snr_4ss_avg[3]));
	}
	#endif

	

}

void
phydm_get_phy_statistic(
	void		*p_dm_void
)
{
	struct	PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	
	phydm_rx_rate_distribution(p_dm);
	phydm_reset_rx_rate_distribution(p_dm);
	
	phydm_get_avg_phystatus_val(p_dm);
	phydm_reset_phystatus_statistic(p_dm);
};

void
phydm_basic_dbg_message
(
	void			*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_fa_struct *false_alm_cnt = (struct phydm_fa_struct *)phydm_get_structure(p_dm, PHYDM_FALSEALMCNT);
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);
	struct phydm_dig_struct	*p_dig_t = &p_dm->dm_dig_table;
	struct _rate_adaptive_table_	*p_ra_table = &p_dm->dm_ra_table;
	u16	macid, phydm_macid, client_cnt = 0;
	struct cmn_sta_info	*p_entry = NULL;
	s32	tmp_val = 0;
	u8	tmp_val_u1 = 0;

	if (!(p_dm->debug_components & ODM_COMP_COMMON))
		return;

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[PHYDM Common MSG] System up time: ((%d sec))----->\n", p_dm->phydm_sys_up_time));

	if (p_dm->is_linked) {
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("ID=((%d)), BW=((%d)), fc=((CH-%d))\n", p_dm->curr_station_id, 20<<(*(p_dm->p_band_width)), *(p_dm->p_channel)));

		if ((*(p_dm->p_channel) <= 14) && (*(p_dm->p_band_width) == CHANNEL_WIDTH_40)) {
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("Primary CCA at ((%s SB))\n",
				(((*(p_dm->p_sec_ch_offset)) == SECOND_CH_AT_LSB)?"U":"L")));
		}

		if ((p_dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE) || p_dm->rx_rate > ODM_RATE11M) {
			
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[AGC Idx] {0x%x, 0x%x, 0x%x, 0x%x}\n",
				p_dm->ofdm_agc_idx[0], p_dm->ofdm_agc_idx[1], p_dm->ofdm_agc_idx[2], p_dm->ofdm_agc_idx[3]));
		} else {
		
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[CCK AGC Idx] {LNA, VGA}={0x%x, 0x%x}\n",
				p_dm->cck_lna_idx, p_dm->cck_vga_idx));
		}

		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("RSSI:{%d, %d, %d, %d}, RxRate:",
			(p_dm->RSSI_A == 0xff) ? 0 : p_dm->RSSI_A,
			(p_dm->RSSI_B == 0xff) ? 0 : p_dm->RSSI_B,
			(p_dm->RSSI_C == 0xff) ? 0 : p_dm->RSSI_C,
			(p_dm->RSSI_D == 0xff) ? 0 : p_dm->RSSI_D));

		phydm_print_rate(p_dm, p_dm->rx_rate, ODM_COMP_COMMON);

		phydm_get_phy_statistic(p_dm);

		/*Print TX rate*/
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {

			p_entry = p_dm->p_phydm_sta_info[macid];
			if (!is_sta_active(p_entry)) {
				continue;
			}

			phydm_macid = (p_dm->phydm_macid_table[macid]);
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("TxRate[%d]:", macid));
			phydm_print_rate(p_dm, p_entry->ra_info.curr_tx_rate, ODM_COMP_COMMON);

			client_cnt++;

			if (client_cnt >= p_dm->number_linked_client)
				break;
		}

		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("TP {Tx, Rx, Total} = {%d, %d, %d}Mbps, Traffic_Load=(%d))\n",
			p_dm->tx_tp, p_dm->rx_tp, p_dm->total_tp, p_dm->traffic_load));

		tmp_val_u1 = (p_cfo_track->crystal_cap > p_cfo_track->def_x_cap) ? (p_cfo_track->crystal_cap - p_cfo_track->def_x_cap) : (p_cfo_track->def_x_cap - p_cfo_track->crystal_cap);
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("CFO_avg = ((%d kHz)) , CFO_tracking = ((%s%d))\n",
			p_cfo_track->CFO_ave_pre, ((p_cfo_track->crystal_cap > p_cfo_track->def_x_cap) ? "+" : "-"), tmp_val_u1));

		/* Condition number */
	#if (RTL8822B_SUPPORT == 1)
		if (p_dm->support_ic_type == ODM_RTL8822B) {
			tmp_val = phydm_get_condition_number_8822B(p_dm);
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("Condi_Num=((%d))\n", tmp_val));
		}
	#endif

	#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		/*STBC or LDPC pkt*/
		if (p_dm->support_ic_type & ODM_IC_PHY_STATUE_NEW_TYPE)
			PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("Coding: LDPC=((%s)), STBC=((%s))\n", (p_dm->phy_dbg_info.is_ldpc_pkt) ? "Y" : "N", (p_dm->phy_dbg_info.is_stbc_pkt) ? "Y" : "N"));
	#endif
	} else
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("No Link !!!\n"));

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca, false_alm_cnt->cnt_cca_all));

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail, false_alm_cnt->cnt_all));

	#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_IC_11N_SERIES) {
		PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",
			false_alm_cnt->cnt_parity_fail, false_alm_cnt->cnt_rate_illegal, false_alm_cnt->cnt_crc8_fail, false_alm_cnt->cnt_mcs_fail, false_alm_cnt->cnt_fast_fsync, false_alm_cnt->cnt_sb_search_fail));
	}
	#endif

	PHYDM_DBG(p_dm, ODM_COMP_COMMON, ("is_linked = %d, Num_client = %d, rssi_min = %d, IGI = 0x%x, bNoisy=%d\n\n",
		p_dm->is_linked, p_dm->number_linked_client, p_dm->rssi_min, p_dig_t->cur_ig_value, p_dm->noisy_decision));

#endif
}


void phydm_basic_profile(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	char  *cut = NULL;
	char *ic_type = NULL;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32	date = 0;
	char	*commit_by = NULL;
	u32	release_ver = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% Basic Profile %"));

	if (p_dm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		ic_type = "RTL8188E";
		date = RELEASE_DATE_8188E;
		commit_by = COMMIT_BY_8188E;
		release_ver = RELEASE_VERSION_8188E;
#endif
	}
#if (RTL8812A_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8812) {
		ic_type = "RTL8812A";
		date = RELEASE_DATE_8812A;
		commit_by = COMMIT_BY_8812A;
		release_ver = RELEASE_VERSION_8812A;
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8821) {
		ic_type = "RTL8821A";
		date = RELEASE_DATE_8821A;
		commit_by = COMMIT_BY_8821A;
		release_ver = RELEASE_VERSION_8821A;
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8192E) {
		ic_type = "RTL8192E";
		date = RELEASE_DATE_8192E;
		commit_by = COMMIT_BY_8192E;
		release_ver = RELEASE_VERSION_8192E;
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8723B) {
		ic_type = "RTL8723B";
		date = RELEASE_DATE_8723B;
		commit_by = COMMIT_BY_8723B;
		release_ver = RELEASE_VERSION_8723B;
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8814A) {
		ic_type = "RTL8814A";
		date = RELEASE_DATE_8814A;
		commit_by = COMMIT_BY_8814A;
		release_ver = RELEASE_VERSION_8814A;
	}
#endif
#if (RTL8881A_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8881A) {
		ic_type = "RTL8881A";
		/**/
	}
#endif
#if (RTL8822B_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8822B) {
		ic_type = "RTL8822B";
		date = RELEASE_DATE_8822B;
		commit_by = COMMIT_BY_8822B;
		release_ver = RELEASE_VERSION_8822B;
	}
#endif
#if (RTL8197F_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8197F) {
		ic_type = "RTL8197F";
		date = RELEASE_DATE_8197F;
		commit_by = COMMIT_BY_8197F;
		release_ver = RELEASE_VERSION_8197F;
	}
#endif

#if (RTL8703B_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8703B) {

		ic_type = "RTL8703B";
		date = RELEASE_DATE_8703B;
		commit_by = COMMIT_BY_8703B;
		release_ver = RELEASE_VERSION_8703B;

	}
#endif
#if (RTL8195A_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8195A) {
		ic_type = "RTL8195A";
		/**/
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8188F) {
		ic_type = "RTL8188F";
		date = RELEASE_DATE_8188F;
		commit_by = COMMIT_BY_8188F;
		release_ver = RELEASE_VERSION_8188F;
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8723D) {
		ic_type = "RTL8723D";
		date = RELEASE_DATE_8723D;
		commit_by = COMMIT_BY_8723D;
		release_ver = RELEASE_VERSION_8723D;
		/**/
	}
#endif

/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8710B) {
		ic_type = "RTL8710B";
		date = RELEASE_DATE_8710B;
		commit_by = COMMIT_BY_8710B;
		release_ver = RELEASE_VERSION_8710B;
		/**/
	}
#endif

#if (RTL8821C_SUPPORT == 1)
	else if (p_dm->support_ic_type == ODM_RTL8821C) {
		ic_type = "RTL8821C";
		date = RELEASE_DATE_8821C;
		commit_by = COMMIT_BY_8821C;
		release_ver = RELEASE_VERSION_8821C;
	}
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s (MP Chip: %s)\n", "IC type", ic_type, p_dm->is_mp_chip ? "Yes" : "No"));

	if (p_dm->cut_version == ODM_CUT_A)
		cut = "A";
	else if (p_dm->cut_version == ODM_CUT_B)
		cut = "B";
	else if (p_dm->cut_version == ODM_CUT_C)
		cut = "C";
	else if (p_dm->cut_version == ODM_CUT_D)
		cut = "D";
	else if (p_dm->cut_version == ODM_CUT_E)
		cut = "E";
	else if (p_dm->cut_version == ODM_CUT_F)
		cut = "F";
	else if (p_dm->cut_version == ODM_CUT_I)
		cut = "I";

	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "RFE type", p_dm->rfe_type));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Cut Ver", cut));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Para Ver", odm_get_hw_img_version(p_dm)));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Para Commit date", date));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY Para Commit by", commit_by));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Para Release Ver", release_ver));

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{
		struct _ADAPTER		*adapter = p_dm->adapter;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Ver", adapter->MgntInfo.FirmwareVersion, adapter->MgntInfo.FirmwareSubVersion));
	}
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	{
		struct rtl8192cd_priv *priv = p_dm->priv;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Ver", priv->pshare->fw_version, priv->pshare->fw_sub_version));
	}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)
	{
		struct rtl_priv *rtlpriv = (struct rtl_priv *)p_dm->adapter;
		struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Ver", rtlhal->fw_version, rtlhal->fw_subversion));
	}
#else
	{
		struct _ADAPTER		*adapter = p_dm->adapter;
		HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW Ver", p_hal_data->firmware_version, p_hal_data->firmware_sub_version));
	}
#endif
	/* 1 PHY DM version List */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% PHYDM version %"));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Code base", PHYDM_CODE_BASE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Release Date", PHYDM_RELEASE_DATE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Adaptivity", ADAPTIVITY_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "DIG", DIG_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "CFO Tracking", CFO_TRACKING_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "AntDiv", ANTDIV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic TxPower", DYNAMIC_TXPWR_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "RA Info", RAINFO_VERSION));
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "AntDetect", ANTDECT_VERSION));
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "ACS", ACS_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PathDiv", PATHDIV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "LA mode", DYNAMIC_LA_MODE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Primary CCA", PRIMARYCCA_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "DFS", DFS_VERSION));

#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8822B)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8822B", PHY_CONFIG_VERSION_8822B));

#endif
#if (RTL8197F_SUPPORT == 1)
	if (p_dm->support_ic_type & ODM_RTL8197F)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8197F", PHY_CONFIG_VERSION_8197F));
#endif
	*_used = used;
	*_out_len = out_len;
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
void
phydm_fw_trace_en_h2c(
	void		*p_dm_void,
	boolean		enable,
	u32		fw_debug_component,
	u32		monitor_mode,
	u32		macid
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			h2c_parameter[7] = {0};
	u8			cmd_length;

	if (p_dm->support_ic_type & PHYDM_IC_3081_SERIES) {

		h2c_parameter[0] = enable;
		h2c_parameter[1] = (u8)(fw_debug_component & MASKBYTE0);
		h2c_parameter[2] = (u8)((fw_debug_component & MASKBYTE1) >> 8);
		h2c_parameter[3] = (u8)((fw_debug_component & MASKBYTE2) >> 16);
		h2c_parameter[4] = (u8)((fw_debug_component & MASKBYTE3) >> 24);
		h2c_parameter[5] = (u8)monitor_mode;
		h2c_parameter[6] = (u8)macid;
		cmd_length = 7;

	} else {

		h2c_parameter[0] = enable;
		h2c_parameter[1] = (u8)monitor_mode;
		h2c_parameter[2] = (u8)macid;
		cmd_length = 3;
	}


	PHYDM_DBG(p_dm, DBG_FW_TRACE, ("---->\n"));
	if (monitor_mode == 0)
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[H2C] FW_debug_en: (( %d ))\n", enable));
	else
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n", enable, monitor_mode, macid));
	odm_fill_h2c_cmd(p_dm, PHYDM_H2C_FW_TRACE_EN, cmd_length, h2c_parameter);
}

void
phydm_get_per_path_txagc(
	void			*p_dm_void,
	u8			path,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			rate_idx;
	u8			txagc;
	u32			used = *_used;
	u32			out_len = *_out_len;

#ifdef PHYDM_COMMON_API_SUPPORT
	if (((p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) && (path <= RF_PATH_B)) ||
		((p_dm->support_ic_type & (ODM_RTL8821C)) && (path <= RF_PATH_A))) {
		for (rate_idx = 0; rate_idx <= 0x53; rate_idx++) {
			if (rate_idx == ODM_RATE1M)
				PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s\n", "CCK====>"));
			else if (rate_idx == ODM_RATE6M)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "OFDM====>"));
			else if (rate_idx == ODM_RATEMCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 1ss====>"));
			else if (rate_idx == ODM_RATEMCS8)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 2ss====>"));
			else if (rate_idx == ODM_RATEMCS16)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 3ss====>"));
			else if (rate_idx == ODM_RATEMCS24)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "HT 4ss====>"));
			else if (rate_idx == ODM_RATEVHTSS1MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 1ss====>"));
			else if (rate_idx == ODM_RATEVHTSS2MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 2ss====>"));
			else if (rate_idx == ODM_RATEVHTSS3MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 3ss====>"));
			else if (rate_idx == ODM_RATEVHTSS4MCS0)
				PHYDM_SNPRINTF((output + used, out_len - used, "\n  %-35s\n", "VHT 4ss====>"));

			txagc = phydm_api_get_txagc(p_dm, (enum rf_path) path, rate_idx);
			if (config_phydm_read_txagc_check(txagc))
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%02x    ", txagc));
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%s    ", "xx"));
		}
	}
#endif

	*_used = used;
	*_out_len = out_len;

}


void
phydm_get_txagc(
	void			*p_dm_void,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/* path-A */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "path-A===================="));
	phydm_get_per_path_txagc(p_dm, RF_PATH_A, &used, output, &out_len);

	/* path-B */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-B===================="));
	phydm_get_per_path_txagc(p_dm, RF_PATH_B, &used, output, &out_len);

	/* path-C */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-C===================="));
	phydm_get_per_path_txagc(p_dm, RF_PATH_C, &used, output, &out_len);

	/* path-D */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-D===================="));
	phydm_get_per_path_txagc(p_dm, RF_PATH_D, &used, output, &out_len);

	*_used = used;
	*_out_len = out_len;

}

void
phydm_set_txagc(
	void			*p_dm_void,
	u32			*const dm_value,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/*dm_value[1] = path*/
	/*dm_value[2] = hw_rate*/
	/*dm_value[3] = power_index*/

#ifdef PHYDM_COMMON_API_SUPPORT
	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C)) {
		if (dm_value[1] <= 1) {
			if ((u8)dm_value[2] != 0xff) {
				if (phydm_api_set_txagc(p_dm, dm_value[3], (enum rf_path) dm_value[1], (u8)dm_value[2], true))
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s%x\n", "Write path-", dm_value[1], "rate index-0x", dm_value[2], " = 0x", dm_value[3]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[1] & 0x1), "rate index-0x", (dm_value[2] & 0x7f), " fail"));
			} else {
				u8	i;
				u32	power_index;
				boolean	status = true;

				power_index = (dm_value[3] & 0x3f);

				if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
					power_index = (power_index << 24) | (power_index << 16) | (power_index << 8) | (power_index);

					for (i = 0; i < ODM_RATEVHTSS2MCS9; i += 4)
						status = (status & phydm_api_set_txagc(p_dm, power_index, (enum rf_path) dm_value[1], i, false));
				} else if (p_dm->support_ic_type & ODM_RTL8197F) {
					for (i = 0; i <= ODM_RATEMCS15; i++)
						status = (status & phydm_api_set_txagc(p_dm, power_index, (enum rf_path) dm_value[1], i, false));
				}

				if (status)
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x\n", "Write all TXAGC of path-", dm_value[1], " = 0x", dm_value[3]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s\n", "Write all TXAGC of path-", dm_value[1], " fail"));
			}
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[1] & 0x1), "rate index-0x", (dm_value[2] & 0x7f), " fail"));
	}
#endif
	*_used = used;
	*_out_len = out_len;
}

void
phydm_debug_trace(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u64			pre_debug_components, one = 1;
	u32			used = *_used;
	u32			out_len = *_out_len;

	pre_debug_components = p_dm->debug_components;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Debug Message] PhyDM Selection"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))DIG\n", ((p_dm->debug_components & DBG_DIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))RA_MASK\n", ((p_dm->debug_components & DBG_RA_MASK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))DYNAMIC_TXPWR\n", ((p_dm->debug_components & DBG_DYN_TXPWR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))FA_CNT\n", ((p_dm->debug_components & DBG_FA_CNT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "04. (( %s ))RSSI_MONITOR\n", ((p_dm->debug_components & DBG_RSSI_MNTR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "05. (( %s ))CCKPD\n", ((p_dm->debug_components & DBG_CCKPD) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "06. (( %s ))ANT_DIV\n", ((p_dm->debug_components & DBG_ANT_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "07. (( %s ))SMT_ANT\n", ((p_dm->debug_components & DBG_SMT_ANT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "08. (( %s ))PWR_TRAIN\n", ((p_dm->debug_components & F08_PWR_TRAIN) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "09. (( %s ))RA\n", ((p_dm->debug_components & DBG_RA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "10. (( %s ))PATH_DIV\n", ((p_dm->debug_components & DBG_PATH_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "11. (( %s ))DFS\n", ((p_dm->debug_components & DBG_DFS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "12. (( %s ))DYN_ARFR\n", ((p_dm->debug_components & DBG_DYN_ARFR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "13. (( %s ))ADAPTIVITY\n", ((p_dm->debug_components & DBG_ADPTVTY) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "14. (( %s ))CFO_TRK\n", ((p_dm->debug_components & DBG_CFO_TRK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "15. (( %s ))ENV_MNTR\n", ((p_dm->debug_components & DBG_ENV_MNTR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "16. (( %s ))PRI_CCA\n", ((p_dm->debug_components & DBG_PRI_CCA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "17. (( %s ))ADPTV_SOML\n", ((p_dm->debug_components & DBG_ADPTV_SOML) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "18. (( %s ))LNA_SAT_CHK\n", ((p_dm->debug_components & DBG_LNA_SAT_CHK) ? ("V") : ("."))));
		/*PHYDM_SNPRINTF((output + used, out_len - used, "19. (( %s ))TBD1\n", ((p_dm->debug_components & DBG_TBD1) ? ("V") : ("."))));*/
		PHYDM_SNPRINTF((output + used, out_len - used, "20. (( %s ))DRP\n", ((p_dm->debug_components & DBG_DYN_RX_PATH) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "21. (( %s ))TMP\n", ((p_dm->debug_components & DBG_TMP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "22. (( %s ))FW_DEBUG_TRACE\n", ((p_dm->debug_components & DBG_FW_TRACE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "23. (( %s ))TXBF\n", ((p_dm->debug_components & DBG_TXBF) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "24. (( %s ))COMMON_FLOW\n", ((p_dm->debug_components & DBG_COMMON_FLOW) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "25. (( %s ))TX_PWR_TRK\n", ((p_dm->debug_components & ODM_COMP_TX_PWR_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "26. (( %s ))CALIBRATION\n", ((p_dm->debug_components & ODM_COMP_CALIBRATION) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "27. (( %s ))MP\n", ((p_dm->debug_components & ODM_COMP_MP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "28. (( %s ))PHY_CONFIG\n", ((p_dm->debug_components & ODM_PHY_CONFIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "29. (( %s ))INIT\n", ((p_dm->debug_components & ODM_COMP_INIT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "30. (( %s ))COMMON\n", ((p_dm->debug_components & ODM_COMP_COMMON) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "31. (( %s ))API\n", ((p_dm->debug_components & ODM_COMP_API) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else if (dm_value[0] == 101) {
		p_dm->debug_components = 0;
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Disable all debug components"));
	} else {
		if (dm_value[1] == 1)   /*enable*/
			p_dm->debug_components |= (one << dm_value[0]);
		else if (dm_value[1] == 2)   /*disable*/
			p_dm->debug_components &= ~(one << dm_value[0]);
		else
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "pre-DbgComponents = 0x%llx\n", pre_debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-DbgComponents = 0x%llx\n", p_dm->debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	*_used = used;
	*_out_len = out_len;
}

void
phydm_fw_debug_trace(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			pre_fw_debug_components, one = 1;
	u32			used = *_used;
	u32			out_len = *_out_len;

	pre_fw_debug_components = p_dm->fw_debug_components;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[FW Debug Component]"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))RA\n", ((p_dm->fw_debug_components & PHYDM_FW_COMP_RA) ? ("V") : ("."))));

		if (p_dm->support_ic_type & PHYDM_IC_3081_SERIES) {
			PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))MU\n", ((p_dm->fw_debug_components & PHYDM_FW_COMP_MU) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))path Div\n", ((p_dm->fw_debug_components & PHYDM_FW_COMP_PATH_DIV) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))Power training\n", ((p_dm->fw_debug_components & PHYDM_FW_COMP_PT) ? ("V") : ("."))));
		}
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else {
		if (dm_value[0] == 101) {
			p_dm->fw_debug_components = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Clear all fw debug components"));
		} else {
			if (dm_value[1] == 1)   /*enable*/
				p_dm->fw_debug_components |= (one << dm_value[0]);
			else if (dm_value[1] == 2)   /*disable*/
				p_dm->fw_debug_components &= ~(one << dm_value[0]);
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
		}

		if (p_dm->fw_debug_components == 0) {
			p_dm->debug_components &= ~DBG_FW_TRACE;
			phydm_fw_trace_en_h2c(p_dm, false, p_dm->fw_debug_components, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
		} else {
			p_dm->debug_components |= DBG_FW_TRACE;
			phydm_fw_trace_en_h2c(p_dm, true, p_dm->fw_debug_components, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
		}
	}
}

void
phydm_dump_bb_reg(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			addr = 0;
	u32			used = *_used;
	u32			out_len = *_out_len;


	/* BB Reg, For Nseries IC we only need to dump page8 to pageF using 3 digits*/
	for (addr = 0x800; addr < 0xfff; addr += 4) {
		if (p_dm->support_ic_type & ODM_IC_11N_SERIES)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%03x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));
		else
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));
	}

	if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8814A | ODM_RTL8821C)) {

		if (p_dm->rf_type > RF_2T2R) {
			for (addr = 0x1800; addr < 0x18ff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));
		}

		if (p_dm->rf_type > RF_3T3R) {
			for (addr = 0x1a00; addr < 0x1aff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));
		}

		for (addr = 0x1900; addr < 0x19ff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));

		for (addr = 0x1c00; addr < 0x1cff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));

		for (addr = 0x1f00; addr < 0x1fff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));
	}

	*_used = used;
	*_out_len = out_len;
}

void
phydm_dump_all_reg(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			addr = 0;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/* dump MAC register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "MAC==========\n"));
	for (addr = 0; addr < 0x7ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));

	for (addr = 0x1000; addr < 0x17ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm, addr, MASKDWORD)));

	/* dump BB register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "BB==========\n"));
	phydm_dump_bb_reg(p_dm, &used, output, &out_len);

	/* dump RF register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-A==========\n"));
	for (addr = 0; addr < 0xFF; addr++)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm, RF_PATH_A, addr, RFREGOFFSETMASK)));

	if (p_dm->rf_type > RF_1T1R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-B==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm, RF_PATH_B, addr, RFREGOFFSETMASK)));
	}

	if (p_dm->rf_type > RF_2T2R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-C==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm, RF_PATH_C, addr, RFREGOFFSETMASK)));
	}

	if (p_dm->rf_type > RF_3T3R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-D==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm, RF_PATH_D, addr, RFREGOFFSETMASK)));
	}

	*_used = used;
	*_out_len = out_len;
}

void
phydm_enable_big_jump(
	struct PHY_DM_STRUCT	*p_dm,
	boolean		state
)
{
#if (RTL8822B_SUPPORT == 1)
	struct phydm_dig_struct			*p_dig_t = &p_dm->dm_dig_table;

	if (state == false) {
		p_dm->dm_dig_table.enable_adjust_big_jump = false;
		odm_set_bb_reg(p_dm, 0x8c8, 0xfe, ((p_dig_t->big_jump_step3 << 5) | (p_dig_t->big_jump_step2 << 3) | p_dig_t->big_jump_step1));
	} else
		p_dm->dm_dig_table.enable_adjust_big_jump = true;
#endif
}

#if (RTL8822B_SUPPORT == 1 | RTL8821C_SUPPORT == 1 | RTL8814B_SUPPORT == 1)

void
phydm_show_rx_rate(
	struct PHY_DM_STRUCT			*p_dm,
	u32			*_used,
	char			*output,
	u32			*_out_len
)
{
	u32			used = *_used;
	u32			out_len = *_out_len;

	PHYDM_SNPRINTF((output + used, out_len - used, "=====Rx SU rate Statistics=====\n"));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[0], p_dm->phy_dbg_info.num_qry_vht_pkt[1], p_dm->phy_dbg_info.num_qry_vht_pkt[2], p_dm->phy_dbg_info.num_qry_vht_pkt[3]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[4], p_dm->phy_dbg_info.num_qry_vht_pkt[5], p_dm->phy_dbg_info.num_qry_vht_pkt[6], p_dm->phy_dbg_info.num_qry_vht_pkt[7]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[8], p_dm->phy_dbg_info.num_qry_vht_pkt[9]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[10], p_dm->phy_dbg_info.num_qry_vht_pkt[11], p_dm->phy_dbg_info.num_qry_vht_pkt[12], p_dm->phy_dbg_info.num_qry_vht_pkt[13]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[14], p_dm->phy_dbg_info.num_qry_vht_pkt[15], p_dm->phy_dbg_info.num_qry_vht_pkt[16], p_dm->phy_dbg_info.num_qry_vht_pkt[17]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		p_dm->phy_dbg_info.num_qry_vht_pkt[18], p_dm->phy_dbg_info.num_qry_vht_pkt[19]));

	PHYDM_SNPRINTF((output + used, out_len - used, "=====Rx MU rate Statistics=====\n"));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[0], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[1], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[2], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[3]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[4], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[5], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[6], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[7]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[8], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[9]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[10], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[11], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[12], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[13]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[14], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[15], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[16], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[17]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		p_dm->phy_dbg_info.num_qry_mu_vht_pkt[18], p_dm->phy_dbg_info.num_qry_mu_vht_pkt[19]));

	*_used = used;
	*_out_len = out_len;
}

#endif

void
phydm_api_adjust(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u8		i;
	boolean	is_enable_dbg_mode;
	u8 central_ch, primary_ch_idx;
	enum channel_width	bandwidth;
	
#ifdef PHYDM_COMMON_API_SUPPORT

	if ((strcmp(input[1], help) == 0)) {
		
		PHYDM_SNPRINTF((output + used, out_len - used, "{en} {ch_num} {prm_ch 1/2/3/4/9/10} {0:20M, 1:40M, 2:80M}\n"));
		
	} else {
	
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C)) {

			for (i = 0; i < 4; i++) {
				if (input[i + 1])
					PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
			}

			is_enable_dbg_mode = (boolean)var1[0];
			central_ch = (u8) var1[1];
			primary_ch_idx = (u8) var1[2];
			bandwidth = (enum channel_width)var1[3];

			if (is_enable_dbg_mode) {
				p_dm->is_disable_phy_api = false;
				phydm_api_switch_bw_channel(p_dm, central_ch, primary_ch_idx, bandwidth);
				p_dm->is_disable_phy_api = true;
				PHYDM_SNPRINTF((output + used, out_len - used, "central_ch = %d, primary_ch_idx = %d, bandwidth = %d\n", central_ch, primary_ch_idx, bandwidth));
			} else {
				p_dm->is_disable_phy_api = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Disable API debug mode\n"));
			}
		} else {
			PHYDM_SNPRINTF((output + used, out_len - used, "This IC doesn't support PHYDM API function\n"));
			/**/
		}
	}

#else
		PHYDM_SNPRINTF((output + used, out_len - used, "This IC doesn't support PHYDM API function\n"));
#endif

	*_used = used;
	*_out_len = out_len;
}

void
phydm_parameter_adjust(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct phydm_cfo_track_struct				*p_cfo_track = (struct phydm_cfo_track_struct *)phydm_get_structure(p_dm, PHYDM_CFOTRACK);
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u8		i;

	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "1. X_cap = ((0x%x))\n", p_cfo_track->crystal_cap));

	} else {
	
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 0) {

			PHYDM_SSCANF(input[2], DCMD_HEX, &var1[1]);
			phydm_set_crystal_cap(p_dm, (u8)var1[1]);
			PHYDM_SNPRINTF((output + used, out_len - used, "X_cap = ((0x%x))\n", p_cfo_track->crystal_cap));
		}
	}
	*_used = used;
	*_out_len = out_len;
}

struct _PHYDM_COMMAND {
	char name[16];
	u8 id;
};

enum PHYDM_CMD_ID {
	PHYDM_HELP,
	PHYDM_DEMO,
	PHYDM_DIG,
	PHYDM_RA,
	PHYDM_PROFILE,
	PHYDM_ANTDIV,
	PHYDM_PATHDIV,
	PHYDM_DEBUG,
	PHYDM_FW_DEBUG,
	PHYDM_SUPPORT_ABILITY,
	PHYDM_RF_SUPPORTABILITY,
	PHYDM_RF_PROFILE,
	PHYDM_RF_IQK_INFO,
	PHYDM_IQK,
	PHYDM_IQK_DEBUG,
	PHYDM_GET_TXAGC,
	PHYDM_SET_TXAGC,
	PHYDM_SMART_ANT,
	PHYDM_API,
	PHYDM_TRX_PATH,
	PHYDM_LA_MODE,
	PHYDM_DUMP_REG,
	PHYDM_AUTO_DBG,
	PHYDM_BIG_JUMP,
	PHYDM_SHOW_RXRATE,
	PHYDM_NBI_EN,
	PHYDM_CSI_MASK_EN,
	PHYDM_DFS_DEBUG,
	PHYDM_NHM,
	PHYDM_CLM,
	PHYDM_BB_INFO,
	PHYDM_TXBF,
	PHYDM_H2C,
	PHYDM_ANT_SWITCH,
	PHYDM_DYNAMIC_RA_PATH,
	PHYDM_ADAPTIVE_SOML,
	PHYDM_PSD,
	PHYDM_DEBUG_PORT,
	PHYDM_DIS_HTSTF_CONTROL,
	PHYDM_TUNE_PARAMETER,
	PHYDM_ADAPTIVITY_DEBUG,
	PHYDM_DIS_DYM_ANT_WEIGHTING,
	PHYDM_FORECE_PT_STATE,
	PHYDM_DIS_RXHP_CTR,
	PHYDM_STA_INFO,
	PHYDM_PAUSE_FUNC
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"-h", PHYDM_HELP},		/*do not move this element to other position*/
	{"demo", PHYDM_DEMO},	/*do not move this element to other position*/
	{"dig", PHYDM_DIG},	
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"antdiv", PHYDM_ANTDIV},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"fw_dbg", PHYDM_FW_DEBUG},
	{"ability", PHYDM_SUPPORT_ABILITY},
	{"rf_ability", PHYDM_RF_SUPPORTABILITY},
	{"rf_profile", PHYDM_RF_PROFILE},
	{"iqk_info", PHYDM_RF_IQK_INFO},
	{"iqk", PHYDM_IQK},
	{"iqk_dbg", PHYDM_IQK_DEBUG},
	{"get_txagc", PHYDM_GET_TXAGC},
	{"set_txagc", PHYDM_SET_TXAGC},
	{"smtant", PHYDM_SMART_ANT},
	{"api", PHYDM_API},
	{"trxpath", PHYDM_TRX_PATH},
	{"lamode", PHYDM_LA_MODE},
	{"dumpreg", PHYDM_DUMP_REG},
	{"auto_dbg", PHYDM_AUTO_DBG},
	{"bigjump", PHYDM_BIG_JUMP},
	{"rxrate", PHYDM_SHOW_RXRATE},
	{"nbi", PHYDM_NBI_EN},
	{"csi_mask", PHYDM_CSI_MASK_EN},
	{"dfs", PHYDM_DFS_DEBUG},
	{"nhm", PHYDM_NHM},
	{"clm", PHYDM_CLM},
	{"bbinfo", PHYDM_BB_INFO},
	{"txbf", PHYDM_TXBF},
	{"h2c", PHYDM_H2C},
	{"ant_switch", PHYDM_ANT_SWITCH},
	{"drp", PHYDM_DYNAMIC_RA_PATH},
	{"soml", PHYDM_ADAPTIVE_SOML},
	{"psd", PHYDM_PSD},
	{"dbgport", PHYDM_DEBUG_PORT},
	{"dis_htstf", PHYDM_DIS_HTSTF_CONTROL},
	{"tune_para", PHYDM_TUNE_PARAMETER},
	{"adapt_debug", PHYDM_ADAPTIVITY_DEBUG},
	{"dis_dym_ant_wgt", PHYDM_DIS_DYM_ANT_WEIGHTING},
	{"force_pt_state", PHYDM_FORECE_PT_STATE},
	{"dis_drxhp", PHYDM_DIS_RXHP_CTR},
	{"sta_info", PHYDM_STA_INFO},
	{"pause", PHYDM_PAUSE_FUNC}
};

#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/

void
phydm_cmd_parser(
	struct PHY_DM_STRUCT	*p_dm,
	char		input[][MAX_ARGV],
	u32	input_num,
	u8	flag,
	char		*output,
	u32	out_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	u32 used = 0;
	u8 id = 0;
	int var1[10] = {0};
	int i, input_idx = 0, phydm_ary_size = sizeof(phy_dm_ary) / sizeof(struct _PHYDM_COMMAND);
	char help[] = "-h";

	if (flag == 0) {
		PHYDM_SNPRINTF((output + used, out_len - used, "GET, nothing to print\n"));
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\n"));

	/* Parsing Cmd ID */
	if (input_num) {

		for (i = 0; i < phydm_ary_size; i++) {
			if (strcmp(phy_dm_ary[i].name, input[0]) == 0) {
				id = phy_dm_ary[i].id;
				break;
			}
		}
		if (i == phydm_ary_size) {
			PHYDM_SNPRINTF((output + used, out_len - used, "SET, command not found!\n"));
			return;
		}
	}

	switch (id) {

	case PHYDM_HELP:
	{
		PHYDM_SNPRINTF((output + used, out_len - used, "BB cmd ==>\n"));
		for (i = 0; i < phydm_ary_size - 2; i++) {

			PHYDM_SNPRINTF((output + used, out_len - used, "  %-5d: %s\n", i, phy_dm_ary[i + 2].name));
			/**/
		}
	}
	break;

	case PHYDM_DEMO: { /*echo demo 10 0x3a z abcde >cmd*/
		u32 directory = 0;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
		char char_temp;
#else
		u32 char_temp = ' ';
#endif

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &directory);
		PHYDM_SNPRINTF((output + used, out_len - used, "Decimal value = %d\n", directory));
		PHYDM_SSCANF(input[2], DCMD_HEX, &directory);
		PHYDM_SNPRINTF((output + used, out_len - used, "Hex value = 0x%x\n", directory));
		PHYDM_SSCANF(input[3], DCMD_CHAR, &char_temp);
		PHYDM_SNPRINTF((output + used, out_len - used, "Char = %c\n", char_temp));
		PHYDM_SNPRINTF((output + used, out_len - used, "String = %s\n", input[4]));
	}
	break;
	
	case PHYDM_DIG:

		phydm_dig_debug(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	case PHYDM_RA:
		phydm_ra_debug(p_dm, &input[0], &used, output, &out_len);
		break;

	case PHYDM_ANTDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, PATHDIV_var[%d]= (( %d ))\n", i, var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_PATHDIV_debug\n"));*/
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
			phydm_antdiv_debug(p_dm, (u32 *)var1, &used, output, &out_len);
#endif
		}

		break;

	case PHYDM_PATHDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, PATHDIV_var[%d]= (( %d ))\n", i, var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_PATHDIV_debug\n"));*/
#if (defined(CONFIG_PATH_DIVERSITY))
			odm_pathdiv_debug(p_dm, (u32 *)var1, &used, output, &out_len);
#endif
		}

		break;

	case PHYDM_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, Debug_var[%d]= (( %d ))\n", i, var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_debug_comp\n"));*/
			phydm_debug_trace(p_dm, (u32 *)var1, &used, output, &out_len);
		}


		break;

	case PHYDM_FW_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_fw_debug_trace(p_dm, (u32 *)var1, &used, output, &out_len);

		break;

	case PHYDM_SUPPORT_ABILITY:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output+used, out_len-used, "new SET, support ablity_var[%d]= (( %d ))\n", i, var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "support ablity\n"));*/
			phydm_support_ability_debug(p_dm, (u32 *)var1, &used, output, &out_len);
		}

		break;

	case PHYDM_RF_SUPPORTABILITY:
		halrf_support_ability_debug(p_dm, &input[0], &used, output, &out_len);
		break;

	case PHYDM_RF_PROFILE:
		halrf_basic_profile(p_dm, &used, output, &out_len);
		break;
		
	case PHYDM_RF_IQK_INFO:
		#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C))
			halrf_iqk_info_dump(p_dm, &used, output, &out_len);
		#endif
		break;

	case PHYDM_IQK:

		PHYDM_SNPRINTF((output + used, out_len - used, "TRX IQK Trigger\n"));
		halrf_iqk_trigger(p_dm, false);

		#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C))
			halrf_iqk_info_dump(p_dm, &used, output, &out_len);
		#endif
		
		break;
		
	case PHYDM_IQK_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
			if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C))
				halrf_iqk_debug(p_dm, (u32 *)var1, &used, output, &out_len);
			#endif
		}
		break;
		
	case PHYDM_SMART_ANT:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
	#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))

		#ifdef CONFIG_HL_SMART_ANTENNA_TYPE2
		phydm_hl_smart_ant_debug_type2(p_dm, &input[0], &used, output, &out_len, input_num);
		#elif (defined(CONFIG_HL_SMART_ANTENNA_TYPE1))
		phydm_hl_smart_ant_debug(p_dm, &input[0], &used, output, &out_len, input_num);
		#endif
	
	#endif

	#if (defined(CONFIG_CUMITEK_SMART_ANTENNA))
		phydm_cumitek_smt_ant_debug(p_dm, &input[0], &used, output, &out_len, input_num);
	#endif
		}

		break;

	case PHYDM_API:
		phydm_api_adjust(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	case PHYDM_PROFILE:
		phydm_basic_profile(p_dm, &used, output, &out_len);
		break;

	case PHYDM_GET_TXAGC:
		phydm_get_txagc(p_dm, &used, output, &out_len);
		break;

	case PHYDM_SET_TXAGC:
	{
		boolean		is_enable_dbg_mode;

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if ((strcmp(input[1], help) == 0)) {
			PHYDM_SNPRINTF((output + used, out_len - used, "{En} {pathA~D(0~3)} {rate_idx(Hex), All_rate:0xff} {txagc_idx (Hex)}\n"));
			/**/

		} else {

			is_enable_dbg_mode = (boolean)var1[0];
			if (is_enable_dbg_mode) {
				p_dm->is_disable_phy_api = false;
				phydm_set_txagc(p_dm, (u32 *)var1, &used, output, &out_len);
				p_dm->is_disable_phy_api = true;
			} else {
				p_dm->is_disable_phy_api = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Disable API debug mode\n"));
			}
		}
	}
	break;

	case PHYDM_TRX_PATH:

		for (i = 0; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
		}
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1)
		if (p_dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
			u8		tx_path, rx_path;
			boolean		is_enable_dbg_mode, is_tx2_path;

			is_enable_dbg_mode = (boolean)var1[0];
			tx_path = (u8) var1[1];
			rx_path = (u8) var1[2];
			is_tx2_path = (boolean) var1[3];

			if (is_enable_dbg_mode) {
				p_dm->is_disable_phy_api = false;
				phydm_api_trx_mode(p_dm, (enum bb_path) tx_path, (enum bb_path) rx_path, is_tx2_path);
				p_dm->is_disable_phy_api = true;
				PHYDM_SNPRINTF((output + used, out_len - used, "tx_path = 0x%x, rx_path = 0x%x, is_tx2_path = %d\n", tx_path, rx_path, is_tx2_path));
			} else {
				p_dm->is_disable_phy_api = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Disable API debug mode\n"));
			}
		} else
#endif
			phydm_config_trx_path(p_dm, (u32 *)var1, &used, output, &out_len);

		break;

	case PHYDM_LA_MODE:

		#if (PHYDM_LA_MODE_SUPPORT == 1)
		phydm_lamode_trigger_setting(p_dm, &input[0], &used, output, &out_len, input_num);
		#else
		PHYDM_SNPRINTF((output + used, out_len - used, "This IC doesn't support LA mode\n"));
		#endif

		break;

	case PHYDM_DUMP_REG:
	{
		u8	type = 0;

		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			type = (u8)var1[0];
		}

		if (type == 0)
			phydm_dump_bb_reg(p_dm, &used, output, &out_len);
		else if (type == 1)
			phydm_dump_all_reg(p_dm, &used, output, &out_len);
	}
	break;

	case PHYDM_BIG_JUMP:
	{
#if (RTL8822B_SUPPORT == 1)
		if (p_dm->support_ic_type & ODM_RTL8822B) {
			if (input[1]) {
				PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
				phydm_enable_big_jump(p_dm, (boolean)(var1[0]));
			} else
				PHYDM_SNPRINTF((output + used, out_len - used, "unknown command!\n"));
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "The command is only for 8822B!\n"));
#endif
		break;
	}

	case PHYDM_AUTO_DBG:
		#ifdef PHYDM_AUTO_DEGBUG
		phydm_auto_dbg_console(p_dm, &input[0], &used, output, &out_len, input_num);
		#endif
		break;

	case PHYDM_SHOW_RXRATE:
#if (RTL8822B_SUPPORT == 1 | RTL8821C_SUPPORT == 1 | RTL8814B_SUPPORT == 1)
		if (p_dm->support_ic_type & PHYDM_IC_SUPPORT_MU_BFEE) {
			u8	rate_idx;

			if (input[1])
				PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

			if (var1[0] == 1)
				phydm_show_rx_rate(p_dm, &used, output, &out_len);
			else {
				PHYDM_SNPRINTF((output + used, out_len - used, "Reset Rx rate counter\n"));

				for (rate_idx = 0; rate_idx < 40; rate_idx++) {
					p_dm->phy_dbg_info.num_qry_vht_pkt[rate_idx] = 0;
					p_dm->phy_dbg_info.num_qry_mu_vht_pkt[rate_idx] = 0;
				}
			}
		}
#endif
		break;

	case PHYDM_NBI_EN:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {

			phydm_api_debug(p_dm, PHYDM_API_NBI, (u32 *)var1, &used, output, &out_len);
			/**/
		}


		break;

	case PHYDM_CSI_MASK_EN:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {

			phydm_api_debug(p_dm, PHYDM_API_CSI_MASK, (u32 *)var1, &used, output, &out_len);
			/**/
		}


		break;

	case PHYDM_DFS_DEBUG:
#ifdef CONFIG_PHYDM_DFS_MASTER
		{
			u32 var[4] = {0};

			for (i = 0; i < 4; i++) {
				if (input[i + 1]) {
					PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var[i]);
					input_idx++;
				}
			}

			if (input_idx >= 1)
				phydm_dfs_debug(p_dm, var, &used, output, &out_len);
		}
#endif
		break;

	case PHYDM_NHM:
	{
		u8		target_rssi;
		u16		nhm_period = 0xC350;	/* 200ms */
		u8		IGI;
		struct _CCX_INFO	*ccx_info = &p_dm->dm_ccx_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (input_num == 1) {

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger NHM: echo nhm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Exclude CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger NHM: echo nhm 2\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Include CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get NHM results: echo nhm 3\n"));

			return;
		}

		/* NMH trigger */
		if ((var1[0] <= 2) && (var1[0] != 0)) {

			ccx_info->echo_igi = (u8)odm_get_bb_reg(p_dm, 0xC50, MASKBYTE0);

			target_rssi = ccx_info->echo_igi - 10;

			ccx_info->nhm_th[0] = (target_rssi - 15 + 10) * 2; /*Unit: PWdB U(8,1)*/

			for (i = 1; i <= 10; i++)
				ccx_info->nhm_th[i] = ccx_info->nhm_th[0] + 6 * i;

			/* 4 1. store previous NHM setting */
			phydm_nhm_setting(p_dm, STORE_NHM_SETTING);

			/* 4 2. Set NHM period, 0x990[31:16]=0xC350, Time duration for NHM unit: 4us, 0xC350=200ms */
			ccx_info->nhm_period = nhm_period;

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Monitor NHM for %d us", nhm_period * 4));

			/* 4 3. Set NHM inexclude_txon, inexclude_cca, ccx_en */


			ccx_info->nhm_inexclude_cca = (var1[0] == 1) ? NHM_EXCLUDE_CCA : NHM_INCLUDE_CCA;
			ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;

			phydm_nhm_setting(p_dm, SET_NHM_SETTING);

			for (i = 0; i <= 10; i++) {

				if (i == 5)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x, echo_igi = 0x%x", i, ccx_info->nhm_th[i], ccx_info->echo_igi));
				else if (i == 10)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x\n", i, ccx_info->nhm_th[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x", i, ccx_info->nhm_th[i]));
			}

			/* 4 4. Trigger NHM */
			phydm_nhm_trigger(p_dm);

		}

		/*Get NHM results*/
		else if (var1[0] == 3) {

			IGI = (u8)odm_get_bb_reg(p_dm, 0xC50, MASKBYTE0);

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Cur_IGI = 0x%x", IGI));

			phydm_get_nhm_result(p_dm);

			/* 4 Resotre NHM setting */
			phydm_nhm_setting(p_dm, RESTORE_NHM_SETTING);

			for (i = 0; i <= 11; i++) {

				if (i == 5)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d, echo_igi = 0x%x", i, ccx_info->nhm_result[i], ccx_info->echo_igi));
				else if (i == 11)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d\n", i, ccx_info->nhm_result[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d", i, ccx_info->nhm_result[i]));
			}

		} else {

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger NHM: echo nhm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Exclude CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger NHM: echo nhm 2\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Include CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get NHM results: echo nhm 3\n"));

			return;
		}
	}
	break;

	case PHYDM_CLM:
		phydm_clm_dbg(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	#ifdef FAHM_SUPPORT
	case PHYDM_FAHM:
		phydm_fahm_dbg(p_dm, &input[0], &used, output, &out_len, input_num);
	break;
	#endif

	case PHYDM_BB_INFO:
	{
		s32 value32 = 0;

		phydm_bb_debug_info(p_dm, &used, output, &out_len);

		if (p_dm->support_ic_type & ODM_RTL8822B && input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			odm_set_bb_reg(p_dm, 0x1988, 0x003fff00, var1[0]);
			value32 = odm_get_bb_reg(p_dm, 0xf84, MASKDWORD);
			value32 = (value32 & 0xff000000) >> 24;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = condition num = %d, subcarriers = %d\n", "Over condition num subcarrier", var1[0], value32));
			odm_set_bb_reg(p_dm, 0x1988, BIT(22), 0x0);	/*disable report condition number*/
		}
	}
	break;

	case PHYDM_TXBF:
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
		struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm->beamforming_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		if (var1[0] == 0) {
			p_beamforming_info->apply_v_matrix = false;
			p_beamforming_info->snding3ss = true;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n dont apply V matrix and 3SS 789 snding\n"));
		} else if (var1[0] == 1) {
			p_beamforming_info->apply_v_matrix = true;
			p_beamforming_info->snding3ss = true;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n apply V matrix and 3SS 789 snding\n"));
		} else if (var1[0] == 2) {
			p_beamforming_info->apply_v_matrix = true;
			p_beamforming_info->snding3ss = false;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n default txbf setting\n"));
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n unknown cmd!!\n"));
#else
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n no TxBF !!\n"));
#endif
#endif
	}
		break;

	case PHYDM_H2C:

		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_h2C_debug(p_dm, (u32 *)var1, &used, output, &out_len);


		break;

	case PHYDM_ANT_SWITCH:

		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {

#if (RTL8821A_SUPPORT == 1)
			phydm_set_ext_switch(p_dm, (u32 *)var1, &used, output, &out_len);
#else
			PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
#endif
		}


		break;

	case PHYDM_DYNAMIC_RA_PATH:

#ifdef CONFIG_DYNAMIC_RX_PATH
		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_drp_debug(p_dm, (u32 *)var1, &used, output, &out_len);

#else
		PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
#endif

		break;

	case PHYDM_ADAPTIVE_SOML:

#ifdef CONFIG_ADAPTIVE_SOML
		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_soml_debug(p_dm, (u32 *)var1, &used, output, &out_len);

#else
		PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
#endif

		break;

	case PHYDM_PSD:

		#ifdef CONFIG_PSD_TOOL
		phydm_psd_debug(p_dm, &input[0], &used, output, &out_len, input_num);
		#endif

		break;
		
	case PHYDM_DEBUG_PORT:
		{
			u32	dbg_port_value;

			PHYDM_SSCANF(input[1], DCMD_HEX, &var1[0]);

			p_dm->debug_components |= ODM_COMP_API;
			if (phydm_set_bb_dbg_port(p_dm, BB_DBGPORT_PRIORITY_3, var1[0])) {/*set debug port to 0x0*/

				dbg_port_value = phydm_get_bb_dbg_port_value(p_dm);
				phydm_release_bb_dbg_port(p_dm);
				
				PHYDM_SNPRINTF((output + used, out_len - used, "Dbg Port[0x%x] = ((0x%x))\n", var1[0], dbg_port_value));
			}
			p_dm->debug_components &= (~ODM_COMP_API);
		}
		break;
		
	case PHYDM_DIS_HTSTF_CONTROL:
		{
			if (input[1])
				PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

			if (var1[0] == 1) {
				
				/* setting being false is for debug */
				p_dm->bhtstfdisabled = true;
				PHYDM_SNPRINTF((output + used, out_len - used, "Dynamic HT-STF Gain Control is Disable\n"));
			}
			else {
				
				/* default setting should be true, always be dynamic control*/
				p_dm->bhtstfdisabled = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Dynamic HT-STF Gain Control is Enable\n"));
			}
		}
		break;
		
	case PHYDM_TUNE_PARAMETER:
		phydm_parameter_adjust(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	case PHYDM_ADAPTIVITY_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_adaptivity_debug(p_dm, (u32 *)var1, &used, output, &out_len);

		break;

	case PHYDM_DIS_DYM_ANT_WEIGHTING:	
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		if (input_num == 1) {		
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Disable dynamic antenna weighting: echo dis_dym_ant_weighting 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Enable dynamic antenna weighting: echo dis_dym_ant_weighting 0\n"));
			return;
		}
		
		if (var1[0] == 1) {
			p_dm->is_disable_dym_ant_weighting = 1;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Disable dynmic antenna weighting !\n"));
		} else if(var1[0] == 0) {
			p_dm->is_disable_dym_ant_weighting = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Enable dynmic antenna weighting !\n"));
		} else {
			p_dm->is_disable_dym_ant_weighting = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Enable dynmic antenna weighting !\n"));
		}
		break;

	case PHYDM_FORECE_PT_STATE:
		{

		#ifdef PHYDM_POWER_TRAINING_SUPPORT	
			phydm_pow_train_debug(p_dm, &input[0], &used, output, &out_len, input_num);
		#else
			PHYDM_SNPRINTF((output + used, out_len - used, "Pow training: Not Support\n"));
		#endif
		
		break;
		}

	case PHYDM_DIS_RXHP_CTR:
		{
			if (input[1])
				PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

			if (var1[0] == 1) {
				/* the setting being on is at debug mode to disconnect RxHP seeting with SoML on/odd */
				p_dm->disrxhpsoml = true;
				PHYDM_SNPRINTF((output + used, out_len - used, "Dynamic RxHP Control with SoML on/off is Disable\n"));
			}
			else if (var1[0] == 0) {
				/* default setting, RxHP setting will follow SoML on/off setting */
				p_dm->disrxhpsoml = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Dynamic RxHP Control with SoML on/off is Enable\n"));
			}
			else {
				p_dm->disrxhpsoml = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Default Setting, Dynamic RxHP Control with SoML on/off is Enable\n"));
			}
		}
		break;
		
	case PHYDM_STA_INFO:
		phydm_show_sta_info(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	case PHYDM_PAUSE_FUNC:
		phydm_pause_func_console(p_dm, &input[0], &used, output, &out_len, input_num);
		break;

	default:
		PHYDM_SNPRINTF((output + used, out_len - used, "SET, unknown command!\n"));
		break;

	}
#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

#ifdef __ECOS
char *strsep(char **s, const char *ct)
{
	char *sbegin = *s;
	char *end;

	if (sbegin == NULL)
		return NULL;

	end = strpbrk(sbegin, ct);
	if (end)
		*end++ = '\0';
	*s = end;
	return sbegin;
}
#endif

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_AP))
s32
phydm_cmd(
	struct PHY_DM_STRUCT	*p_dm,
	char		*input,
	u32	in_len,
	u8	flag,
	char	*output,
	u32	out_len
)
{
	char *token;
	u32	argc = 0;
	char		argv[MAX_ARGC][MAX_ARGV];

	do {
		token = strsep(&input, ", ");
		if (token) {
			strcpy(argv[argc], token);
			argc++;
		} else
			break;
	} while (argc < MAX_ARGC);

	if (argc == 1)
		argv[0][strlen(argv[0]) - 1] = '\0';

	phydm_cmd_parser(p_dm, argv, argc, flag, output, out_len);

	return 0;
}
#endif


void
phydm_fw_trace_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	/*u8	debug_trace_11byte[60];*/
	u8		freg_num, c2h_seq, buf_0 = 0;


	if (!(p_dm->support_ic_type & PHYDM_IC_3081_SERIES))
		return;

	if ((cmd_len > 12) || (cmd_len == 0)) {
		dbg_print("[Warning] Error C2H cmd_len=%d\n", cmd_len);
		return;
	}

	buf_0 = cmd_buf[0];
	freg_num = (buf_0 & 0xf);
	c2h_seq = (buf_0 & 0xf0) >> 4;
	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("[FW debug message] freg_num = (( %d )), c2h_seq = (( %d ))\n", freg_num,c2h_seq ));*/

	/*strncpy(debug_trace_11byte,&cmd_buf[1],(cmd_len-1));*/
	/*debug_trace_11byte[cmd_len-1] = '\0';*/
	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("[FW debug message] %s\n", debug_trace_11byte));*/
	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("[FW debug message] cmd_len = (( %d ))\n", cmd_len));*/
	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("[FW debug message] c2h_cmd_start  = (( %d ))\n", p_dm->c2h_cmd_start));*/



	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("pre_seq = (( %d )), current_seq = (( %d ))\n", p_dm->pre_c2h_seq, c2h_seq));*/
	/*PHYDM_DBG(p_dm, DBG_FW_TRACE,("fw_buff_is_enpty = (( %d ))\n", p_dm->fw_buff_is_enpty));*/

	if ((c2h_seq != p_dm->pre_c2h_seq)  &&  p_dm->fw_buff_is_enpty == false) {
		p_dm->fw_debug_trace[p_dm->c2h_cmd_start] = '\0';
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW Dbg Queue Overflow] %s\n", p_dm->fw_debug_trace));
		p_dm->c2h_cmd_start = 0;
	}

	if ((cmd_len - 1) > (60 - p_dm->c2h_cmd_start)) {
		p_dm->fw_debug_trace[p_dm->c2h_cmd_start] = '\0';
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW Dbg Queue error: wrong C2H length] %s\n", p_dm->fw_debug_trace));
		p_dm->c2h_cmd_start = 0;
		return;
	}

	strncpy((char *)&(p_dm->fw_debug_trace[p_dm->c2h_cmd_start]), (char *)&cmd_buf[1], (cmd_len - 1));
	p_dm->c2h_cmd_start += (cmd_len - 1);
	p_dm->fw_buff_is_enpty = false;

	if (freg_num == 0 || p_dm->c2h_cmd_start >= 60) {
		if (p_dm->c2h_cmd_start < 60)
			p_dm->fw_debug_trace[p_dm->c2h_cmd_start] = '\0';
		else
			p_dm->fw_debug_trace[59] = '\0';

		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW DBG Msg] %s\n", p_dm->fw_debug_trace));
		/*dbg_print("[FW DBG Msg] %s\n", p_dm->fw_debug_trace);*/
		p_dm->c2h_cmd_start = 0;
		p_dm->fw_buff_is_enpty = true;
	}

	p_dm->pre_c2h_seq = c2h_seq;
#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void
phydm_fw_trace_handler_code(
	void	*p_dm_void,
	u8	*buffer,
	u8	cmd_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	function = buffer[0];
	u8	dbg_num = buffer[1];
	u16	content_0 = (((u16)buffer[3]) << 8) | ((u16)buffer[2]);
	u16	content_1 = (((u16)buffer[5]) << 8) | ((u16)buffer[4]);
	u16	content_2 = (((u16)buffer[7]) << 8) | ((u16)buffer[6]);
	u16	content_3 = (((u16)buffer[9]) << 8) | ((u16)buffer[8]);
	u16	content_4 = (((u16)buffer[11]) << 8) | ((u16)buffer[10]);

	if (cmd_len > 12)
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW Msg] Invalid cmd length (( %d )) >12\n", cmd_len));

	/* PHYDM_DBG(p_dm, DBG_FW_TRACE,("[FW Msg] Func=((%d)),  num=((%d)), ct_0=((%d)), ct_1=((%d)), ct_2=((%d)), ct_3=((%d)), ct_4=((%d))\n", */
	/*	function, dbg_num, content_0, content_1, content_2, content_3, content_4)); */

	/*--------------------------------------------*/
#ifdef CONFIG_RA_FW_DBG_CODE
	if (function == RATE_DECISION) {
		if (dbg_num == 0) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] RA_CNT=((%d))  Max_device=((%d))--------------------------->\n", content_1, content_2));
			else if (content_0 == 2)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Check RA macid= ((%d)), MediaStatus=((%d)), Dis_RA=((%d)),  try_bit=((0x%x))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Check RA  total=((%d)),  drop=((0x%x)), TXRPT_TRY_bit=((%x)), bNoisy=((%x))\n", content_1, content_2, content_3, content_4));
		} else if (dbg_num == 1) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] RTY[0,1,2,3]=[ %d , %d , %d , %d ]\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] RTY[4]=[ %d ], drop=(( %d )), total=(( %d )), current_rate=((0x %x ))", content_1, content_2, content_3, content_4));
				phydm_print_rate(p_dm, (u8)content_4, DBG_FW_TRACE);
			} else if (content_0 == 3)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] penality_idx=(( %d ))\n", content_1));
			else if (content_0 == 4)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] RSSI=(( %d )), ra_stage = (( %d ))\n", content_1, content_2));
		}

		else if (dbg_num == 3) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Fast_RA (( DOWN ))  total=((%d)),  total>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Fast_RA (( UP ))  total_acc=((%d)),  total_acc>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Fast_RA (( UP )) ((rate Down Hold))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 4)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Fast_RA (( UP )) ((tota_accl<5 skip))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 8)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] Fast_RA (( Reset Tx Rpt )) RA_CNT=((%d))\n", content_1));
		}

		else if (dbg_num == 4) {
			if (content_0 == 3) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] RER_CNT   PCR_ori =(( %d )),  ratio_ori =(( %d )), pcr_updown_bitmap =(( 0x%x )), pcr_var_diff =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			} else if (content_0 == 4) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] pcr_shift_value =(( %s%d )), rate_down_threshold =(( %d )), rate_up_threshold =(( %d ))\n", ((content_1) ? "+" : "-"), content_2, content_3, content_4));
				/**/
			} else if (content_0 == 5) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] pcr_mean =(( %d )), PCR_VAR =(( %d )), offset =(( %d )), decision_offset_p =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}

		else if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] (( UP))  Nsc=(( %d )), N_High=(( %d )), RateUp_Waiting=(( %d )), RateUp_Fail=(( %d ))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] ((DOWN))  Nsc=(( %d )), N_Low=(( %d ))\n", content_1, content_2));
			else if (content_0 == 3)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] ((HOLD))  Nsc=((%d)), N_High=((%d)), N_Low=((%d)), Reset_CNT=((%d))\n", content_1, content_2, content_3, content_4));
		} else if (dbg_num == 0x60) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] ((AP RPT))  macid=((%d)), BUPDATE[macid]=((%d))\n", content_1, content_2));
			else if (content_0 == 4)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] ((AP RPT))  pass=((%d)), rty_num=((%d)), drop=((%d)), total=((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 5)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW] ((AP RPT))  PASS=((%d)), RTY_NUM=((%d)), DROP=((%d)), TOTAL=((%d))\n", content_1, content_2, content_3, content_4));
		}
	}
	/*--------------------------------------------*/
	else if (function == INIT_RA_TABLE) {
		if (dbg_num == 3)
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][INIT_RA_INFO] Ra_init, RA_SKIP_CNT = (( %d ))\n", content_0));

	}
	/*--------------------------------------------*/
	else if (function == RATE_UP) {
		if (dbg_num == 2) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][RateUp]  ((Highest rate->return)), macid=((%d))  Nsc=((%d))\n", content_1, content_2));
		} else if (dbg_num == 5) {
			if (content_0 == 0)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][RateUp]  ((rate UP)), up_rate_tmp=((0x%x)), rate_idx=((0x%x)), SGI_en=((%d)),  SGI=((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][RateUp]  ((rate UP)), rate_1=((0x%x)), rate_2=((0x%x)), BW=((%d)), Try_Bit=((%d))\n", content_1, content_2, content_3, content_4));
		}

	}
	/*--------------------------------------------*/
	else if (function == RATE_DOWN) {
		if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][RateDownStep]  ((rate Down)), macid=((%d)), rate1=((0x%x)),  rate2=((0x%x)), BW=((%d))\n", content_1, content_2, content_3, content_4));
		}
	} else if (function == TRY_DONE) {
		if (dbg_num == 1) {
			if (content_0 == 1) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][Try Done]  ((try succsess )) macid=((%d)), Try_Done_cnt=((%d))\n", content_1, content_2));
				/**/
			}
		} else if (dbg_num == 2) {
			if (content_0 == 1) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][Try Done]  ((try)) macid=((%d)), Try_Done_cnt=((%d)),  rate_2=((%d)),  try_succes=((%d))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}
	}
	/*--------------------------------------------*/
	else if (function == RA_H2C) {
		if (dbg_num == 1) {
			if (content_0 == 0) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][H2C=0x49]  fw_trace_en=((%d)), mode =((%d)),  macid=((%d))\n", content_1, content_2, content_3));
				/**/
				/*C2H_RA_Dbg_code(F_RA_H2C,1,0, SysMib.ODM.DEBUG.fw_trace_en, mode, macid, 0);    //RA MASK*/
			}
#if 0
			else if (dbg_num == 2) {

				if (content_0 == 1) {
					PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][H2C=0x40]  MACID=((%d)), rate ID=((%d)),  SGI=((%d)),  BW=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 2) {
					PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][H2C=0x40]   VHT_en=((%d)), Disable_PowerTraining=((%d)),  Disable_RA=((%d)),  No_Update=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 3) {
					PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][H2C=0x40]   RA_MSK=[%x | %x | %x | %x ]\n", content_1, content_2, content_3, content_4));
					/**/
				}
			}
#endif
		}
	}
	/*--------------------------------------------*/
	else if (function == F_RATE_AP_RPT) {
		if (dbg_num == 1) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  ((1)), SPE_STATIS=((0x%x))---------->\n", content_3));
		} else if (dbg_num == 2) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  RTY_all=((%d))\n", content_1));
		} else if (dbg_num == 3) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  MACID1[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 4) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  MACID2[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 5) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  MACID1[%d], PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 6) {
			if (content_0 == 1)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][AP RPT]  MACID2[%d],, PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
		}
	} else {
		PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
		/**/
	}
#else
	PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
#endif
	/*--------------------------------------------*/

#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void
phydm_fw_trace_handler_8051(
	void	*p_dm_void,
	u8	*buffer,
	u8	cmd_len
)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if 0
	if (cmd_len >= 3)
		cmd_buf[cmd_len - 1] = '\0';
	PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW DBG Msg] %s\n", &(cmd_buf[3])));
#else

	int i = 0;
	u8	extend_c2h_sub_id = 0, extend_c2h_dbg_len = 0, extend_c2h_dbg_seq = 0;
	u8	fw_debug_trace[128];
	u8	*extend_c2h_dbg_content = 0;

	if (cmd_len > 127)
		return;

	extend_c2h_sub_id = buffer[0];
	extend_c2h_dbg_len = buffer[1];
	extend_c2h_dbg_content = buffer + 2; /*DbgSeq+DbgContent  for show HEX*/

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

	for (; ; i++) {
		fw_debug_trace[i] = extend_c2h_dbg_content[i];
		if (extend_c2h_dbg_content[i + 1] == '\0') {
			fw_debug_trace[i + 1] = '\0';
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			break;
		} else if (extend_c2h_dbg_content[i] == '\n') {
			fw_debug_trace[i + 1] = '\0';
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			buffer = extend_c2h_dbg_content + i + 3;
			goto go_backfor_aggre_dbg_pkt;
		}
	}


#endif
#endif /*#ifdef CONFIG_PHYDM_DEBUG_FUNCTION*/
}
