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

void
phydm_init_debug_setting(
	struct PHY_DM_STRUCT		*p_dm_odm
)
{
	p_dm_odm->debug_level = ODM_DBG_TRACE;

	p_dm_odm->fw_debug_components = 0;
	p_dm_odm->debug_components			=
		\
#if DBG
		/*BB Functions*/
		/*									ODM_COMP_DIG					|*/
		/*									ODM_COMP_RA_MASK				|*/
		/*									ODM_COMP_DYNAMIC_TXPWR			|*/
		/*									ODM_COMP_FA_CNT				|*/
		/*									ODM_COMP_RSSI_MONITOR			|*/
		/*									ODM_COMP_SNIFFER				|*/
		/*									ODM_COMP_ANT_DIV				|*/
		/*									ODM_COMP_NOISY_DETECT			|*/
		/*									ODM_COMP_RATE_ADAPTIVE			|*/
		/*									ODM_COMP_PATH_DIV				|*/
		/*									ODM_COMP_DYNAMIC_PRICCA		|*/
		/*									ODM_COMP_MP					|*/
		/*									ODM_COMP_CFO_TRACKING			|*/
		/*									ODM_COMP_ACS					|*/
		/*									PHYDM_COMP_ADAPTIVITY			|*/
		/*									PHYDM_COMP_RA_DBG				|*/
		/*									PHYDM_COMP_TXBF					|*/

		/*MAC Functions*/
		/*									ODM_COMP_EDCA_TURBO			|*/
		/*									ODM_COMP_DYNAMIC_RX_PATH		|*/
		/*									ODM_FW_DEBUG_TRACE				|*/

		/*RF Functions*/
		/*									ODM_COMP_TX_PWR_TRACK			|*/
		/*									ODM_COMP_CALIBRATION			|*/

		/*Common*/
		/*									ODM_PHY_CONFIG					|*/
		/*									ODM_COMP_INIT					|*/
		/*									ODM_COMP_COMMON				|*/
		/*									ODM_COMP_API				|*/


#endif
		0;

	p_dm_odm->fw_buff_is_enpty = true;
	p_dm_odm->pre_c2h_seq = 0;
}

u8
phydm_set_bb_dbg_port(
	void			*p_dm_void,
	u8			curr_dbg_priority,
	u32			debug_port
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	dbg_port_result = FALSE;
		
	if (curr_dbg_priority > p_dm_odm->pre_dbg_priority) {

		if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(p_dm_odm, 0x8fc, MASKDWORD, debug_port);
			/**/
		} else /*if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)*/ {
			odm_set_bb_reg(p_dm_odm, 0x908, MASKDWORD, debug_port);
			/**/
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("DbgPort set success, Reg((0x%x)), Cur_priority=((%d)), Pre_priority=((%d))\n", debug_port, curr_dbg_priority, p_dm_odm->pre_dbg_priority));
		p_dm_odm->pre_dbg_priority = curr_dbg_priority;
		dbg_port_result = TRUE;
	}
		
	return dbg_port_result;
}

void
phydm_release_bb_dbg_port(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	p_dm_odm->pre_dbg_priority = BB_DBGPORT_RELEASE;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("Release BB dbg_port\n"));
}

u32
phydm_get_bb_dbg_port_value(
	void			*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	dbg_port_value = 0;

	if (p_dm_odm->support_ic_type & ODM_IC_11AC_SERIES) {
		dbg_port_value = odm_get_bb_reg(p_dm_odm, 0xfa0, MASKDWORD);
		/**/
	} else /*if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)*/ {
		dbg_port_value = odm_get_bb_reg(p_dm_odm, 0xdf4, MASKDWORD);
		/**/
	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_API, ODM_DBG_LOUD, ("dbg_port_value = 0x%x\n", dbg_port_value));
	return	dbg_port_value;
}

#if CONFIG_PHYDM_DEBUG_FUNCTION
void
phydm_bb_rx_hang_info(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	u32	value32 = 0;
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
		return;

	value32 = odm_get_bb_reg(p_dm_odm, 0xF80, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used,  "\r\n %-35s = 0x%x", "rptreg of sc/bw/ht/...", value32));

	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(p_dm_odm, 0x198c, BIT(2) | BIT(1) | BIT(0), 7);

	/* dbg_port = basic state machine */
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x000);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "basic state machine", value32));
	}

	/* dbg_port = state machine */
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x007);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "state machine", value32));
	}

	/* dbg_port = CCA-related*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x204);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "CCA-related", value32));
	}


	/* dbg_port = edcca/rxd*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x278);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "edcca/rxd", value32));
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x290);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx_state/mux_state/ADC_MASK_OFDM", value32));
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B2);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B8);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "bf-related", value32));
	}

	/* dbg_port = txon/rxd*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA03);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "txon/rxd", value32));
	}

	/* dbg_port = l_rate/l_length*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0B);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "l_rate/l_length", value32));
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0D);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rxd/rxd_hit", value32));
	}

	/* dbg_port = dis_cca*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAA0);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "dis_cca", value32));
	}


	/* dbg_port = tx*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAB0);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "tx", value32));
	}

	/* dbg_port = rx plcp*/
	{
		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD0);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD1);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD2);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));

		odm_set_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD3);
		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "0x8fc", value32));

		value32 = odm_get_bb_reg(p_dm_odm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = 0x%x", "rx plcp", value32));
	}

}

void
phydm_bb_debug_info_n_series(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	u32	value32 = 0, value32_1 = 0, value32_2 = 0, value32_3 = 0;
	u8	rf_gain_a = 0, rf_gain_b = 0, rf_gain_c = 0, rf_gain_d = 0;
	u8	rx_snr_a = 0, rx_snr_b = 0, rx_snr_c = 0, rx_snr_d = 0;

	s8    rxevm_0 = 0, rxevm_1 = 0;
	s32	short_cfo_a = 0, short_cfo_b = 0, long_cfo_a = 0, long_cfo_b = 0;
	s32	scfo_a = 0, scfo_b = 0, avg_cfo_a = 0, avg_cfo_b = 0;
	s32	cfo_end_a = 0, cfo_end_b = 0, acq_cfo_a = 0, acq_cfo_b = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s\n", "BB Report Info"));

	/*AGC result*/
	value32 = odm_get_bb_reg(p_dm_odm, 0xdd0, MASKDWORD);
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
	value32 = odm_get_bb_reg(p_dm_odm, 0xdd4, MASKDWORD);
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
	value32 = odm_get_bb_reg(p_dm_odm, 0xdd8, MASKDWORD);

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
	odm_set_bb_reg(p_dm_odm, 0xd00, BIT(26), 1);

	/*Short CFO*/
	value32 = odm_get_bb_reg(p_dm_odm, 0xdac, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xdb0, MASKDWORD);

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
	value32 = odm_get_bb_reg(p_dm_odm, 0xdb8, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xdb4, MASKDWORD);

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xdbc, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xde0, MASKDWORD);

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
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
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

	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {
		phydm_bb_debug_info_n_series(p_dm_odm, &used, output, &out_len);
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s\n", "BB Report Info"));

	/*BW & mode Detection*/

	value32 = odm_get_bb_reg(p_dm_odm, 0xf80, MASKDWORD);
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

	value32 = odm_get_bb_reg(p_dm_odm, 0xF90, MASKDWORD);
	pwdb = (u8)((value32 & MASKBYTE1) >> 8);
	pwdb = pwdb >> 1;
	sig_power = -110 + pwdb;
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM RX Signal Power(dB)", sig_power));

	value32 = odm_get_bb_reg(p_dm_odm, 0xd14, MASKDWORD);
	rx_snr_path_a = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_a = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_a *= 2;
	value32 = odm_get_bb_reg(p_dm_odm, 0xd54, MASKDWORD);
	rx_snr_path_b = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_b = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_b *= 2;
	value32 = odm_get_bb_reg(p_dm_odm, 0xd94, MASKDWORD);
	rx_snr_path_c = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_c = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_c *= 2;
	value32 = odm_get_bb_reg(p_dm_odm, 0xdd4, MASKDWORD);
	rx_snr_path_d = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_d = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_d *= 2;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "OFDM RX RF Gain(A/B/C/D)", rf_gain_path_a, rf_gain_path_b, rf_gain_path_c, rf_gain_path_d));


	/* RX counter related info*/

	value32 = odm_get_bb_reg(p_dm_odm, 0xF08, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM CCA counter", ((value32 & 0xFFFF0000) >> 16)));

	value32 = odm_get_bb_reg(p_dm_odm, 0xFD0, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "OFDM SBD Fail counter", value32 & 0xFFFF));

	value32 = odm_get_bb_reg(p_dm_odm, 0xFC4, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "VHT SIGA/SIGB CRC8 Fail counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	value32 = odm_get_bb_reg(p_dm_odm, 0xFCC, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d", "CCK CCA counter", value32 & 0xFFFF));

	value32 = odm_get_bb_reg(p_dm_odm, 0xFBC, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "LSIG (parity Fail/rate Illegal) counter", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	value32_1 = odm_get_bb_reg(p_dm_odm, 0xFC8, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm_odm, 0xFC0, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "HT/VHT MCS NOT SUPPORT counter", ((value32_2 & 0xFFFF0000) >> 16), value32_1 & 0xFFFF));

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(p_dm_odm, 0xF8c, MASKDWORD);
	RXEVM_0 = (s8)((value32 & MASKBYTE2) >> 16);
	RXEVM_0 /= 2;
	if (RXEVM_0 < -63)
		RXEVM_0 = 0;

	RXEVM_1 = (s8)((value32 & MASKBYTE3) >> 24);
	RXEVM_1 /= 2;
	value32 = odm_get_bb_reg(p_dm_odm, 0xF88, MASKDWORD);
	RXEVM_2 = (s8)((value32 & MASKBYTE2) >> 16);
	RXEVM_2 /= 2;

	if (RXEVM_1 < -63)
		RXEVM_1 = 0;
	if (RXEVM_2 < -63)
		RXEVM_2 = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)", RXEVM_0, RXEVM_1, RXEVM_2));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)", rx_snr_path_a, rx_snr_path_b, rx_snr_path_c, rx_snr_path_d));

	value32 = odm_get_bb_reg(p_dm_odm, 0xF8C, MASKDWORD);
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d / %d", "CSI_1st /CSI_2nd", value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16)));

	/*BW & mode Detection*/

	/*Reset Page F counter*/
	odm_set_bb_reg(p_dm_odm, 0xB58, BIT(0), 1);
	odm_set_bb_reg(p_dm_odm, 0xB58, BIT(0), 0);

	/*CFO Report Info*/
	/*Short CFO*/
	value32 = odm_get_bb_reg(p_dm_odm, 0xd0c, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xd4c, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm_odm, 0xd8c, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm_odm, 0xdcc, MASKDWORD);

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
	value32 = odm_get_bb_reg(p_dm_odm, 0xd10, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xd50, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm_odm, 0xd90, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm_odm, 0xdd0, MASKDWORD);

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xd14, MASKDWORD);
	value32_1 = odm_get_bb_reg(p_dm_odm, 0xd54, MASKDWORD);
	value32_2 = odm_get_bb_reg(p_dm_odm, 0xd94, MASKDWORD);
	value32_3 = odm_get_bb_reg(p_dm_odm, 0xdd4, MASKDWORD);

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xf20, MASKDWORD);  /*L SIG*/

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xf2c, MASKDWORD);  /*HT SIG*/
	if (RX_HT == 1) {

		HMCSS = (u8)(value32 & 0x7F);
		HRX_BW = (u8)(value32 & 0x80);
		h_length = (u16)((value32 >> 8) & 0xffff);
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "HT-SIG1"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x", "MCS/BW/length", HMCSS, HRX_BW, h_length));

	value32 = odm_get_bb_reg(p_dm_odm, 0xf30, MASKDWORD);  /*HT SIG*/

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xf2c, MASKDWORD);  /*VHT SIG A1*/
	if (RX_HT == 2) {
		/* value32 = odm_get_bb_reg(p_dm_odm, 0xf2c,MASKDWORD);*/
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

	value32 = odm_get_bb_reg(p_dm_odm, 0xf30, MASKDWORD);  /*VHT SIG*/

	if (RX_HT == 2) {
		/*value32 = odm_get_bb_reg(p_dm_odm, 0xf30,MASKDWORD); */  /*VHT SIG*/

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

	value32 = odm_get_bb_reg(p_dm_odm, 0xf34, MASKDWORD);  /*VHT SIG*/
	{
		v_length = (u16)(value32 & 0x1fffff);
		vbrsv = (u8)((value32 & 0x600000) >> 20);
		vb_tail = (u16)((value32 & 0x1f800000) >> 20);
		vbcrc = (u8)((value32 & 0x80000000) >> 28);

	}
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s", "VHT-SIG-B"));
	PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %x / %x / %x / %x", "length/Rsv/tail/CRC", v_length, vbrsv, vb_tail, vbcrc));

	/*for Condition number*/
	if (p_dm_odm->support_ic_type & ODM_RTL8822B) {
		s32	condition_num = 0;
		char *factor = NULL;

		odm_set_bb_reg(p_dm_odm, 0x1988, BIT(22), 0x1);	/*enable report condition number*/

		condition_num = odm_get_bb_reg(p_dm_odm, 0xf84, MASKDWORD);
		condition_num = (condition_num & 0x3ffff) >> 4;

		if (*p_dm_odm->p_band_width == ODM_BW80M)
			factor = "256/234";
		else if (*p_dm_odm->p_band_width == ODM_BW40M)
			factor = "128/108";
		else if (*p_dm_odm->p_band_width == ODM_BW20M) {
			if (RX_HT != 2 || RX_HT != 1)
				factor = "64/52";	/*HT or VHT*/
			else
				factor = "64/48";	/*legacy*/
		}

		PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = %d (factor = %s)", "Condition number", condition_num, factor));

	}

}
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)

#if CONFIG_PHYDM_DEBUG_FUNCTION
void phydm_sbd_check(
	struct PHY_DM_STRUCT					*p_dm_odm
)
{
	static u32	pkt_cnt = 0;
	static boolean sbd_state = 0;
	u32	sym_count, count, value32;

	if (sbd_state == 0) {
		pkt_cnt++;
		if (pkt_cnt % 5 == 0) { /*read SBD conter once every 5 packets*/
			odm_set_timer(p_dm_odm, &p_dm_odm->sbdcnt_timer, 0); /*ms*/
			sbd_state = 1;
		}
	} else { /*read counter*/
		value32 = odm_get_bb_reg(p_dm_odm, 0xF98, MASKDWORD);
		sym_count = (value32 & 0x7C000000) >> 26;
		count = (value32 & 0x3F00000) >> 20;
		dbg_print("#SBD#    sym_count   %d   count   %d\n", sym_count, count);
		sbd_state = 0;
	}
}
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/

void phydm_sbd_callback(
	struct timer_list		*p_timer
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct _ADAPTER		*adapter = (struct _ADAPTER *)p_timer->Adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;

#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_odm->sbdcnt_workitem);
#else
	phydm_sbd_check(p_dm_odm);
#endif
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void phydm_sbd_workitem_callback(
	void            *p_context
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct _ADAPTER	*p_adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;

	phydm_sbd_check(p_dm_odm);
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}
#endif
void
phydm_basic_dbg_message
(
	void			*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _FALSE_ALARM_STATISTICS *false_alm_cnt = (struct _FALSE_ALARM_STATISTICS *)phydm_get_structure(p_dm_odm, PHYDM_FALSEALMCNT);
	struct _CFO_TRACKING_				*p_cfo_track = (struct _CFO_TRACKING_ *)phydm_get_structure(p_dm_odm, PHYDM_CFOTRACK);
	struct _dynamic_initial_gain_threshold_	*p_dm_dig_table = &p_dm_odm->dm_dig_table;
	struct _rate_adaptive_table_	*p_ra_table = &p_dm_odm->dm_ra_table;
	u16	macid, phydm_macid, client_cnt = 0;
	struct sta_info	*p_entry;
	s32	tmp_val = 0;
	u8	tmp_val_u1 = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[PHYDM Common MSG] System up time: ((%d sec))----->\n", p_dm_odm->phydm_sys_up_time));

	if (p_dm_odm->is_linked) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("ID=%d, BW=((%d)), CH=((%d))\n", p_dm_odm->curr_station_id, 20<<(*(p_dm_odm->p_band_width)), *(p_dm_odm->p_channel)));

		/*Print RX rate*/
		if (p_dm_odm->rx_rate <= ODM_RATE11M) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCK AGC Report] LNA_idx = 0x%x, VGA_idx = 0x%x\n",
				p_dm_odm->cck_lna_idx, p_dm_odm->cck_vga_idx));
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM AGC Report] { 0x%x, 0x%x, 0x%x, 0x%x }\n",
				p_dm_odm->ofdm_agc_idx[0], p_dm_odm->ofdm_agc_idx[1], p_dm_odm->ofdm_agc_idx[2], p_dm_odm->ofdm_agc_idx[3]));
		}

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("RSSI: { %d,  %d,  %d,  %d },    rx_rate:",
			(p_dm_odm->RSSI_A == 0xff) ? 0 : p_dm_odm->RSSI_A,
			(p_dm_odm->RSSI_B == 0xff) ? 0 : p_dm_odm->RSSI_B,
			(p_dm_odm->RSSI_C == 0xff) ? 0 : p_dm_odm->RSSI_C,
			(p_dm_odm->RSSI_D == 0xff) ? 0 : p_dm_odm->RSSI_D));

		phydm_print_rate(p_dm_odm, p_dm_odm->rx_rate, ODM_COMP_COMMON);

		/*Print TX rate*/
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {

			p_entry = p_dm_odm->p_odm_sta_info[macid];
			if (IS_STA_VALID(p_entry)) {

				phydm_macid = (p_dm_odm->platform2phydm_macid_table[macid]);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("TXRate [%d]:", macid));
				phydm_print_rate(p_dm_odm, p_ra_table->link_tx_rate[macid], ODM_COMP_COMMON);

				client_cnt++;

				if (client_cnt == p_dm_odm->number_linked_client)
					break;
			}
		}

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("TP { TX, RX, total} = {%d, %d, %d }Mbps, traffic_load = (%d))\n",
			p_dm_odm->tx_tp, p_dm_odm->rx_tp, p_dm_odm->total_tp, p_dm_odm->traffic_load));

		tmp_val_u1 = (p_cfo_track->crystal_cap > p_cfo_track->def_x_cap) ? (p_cfo_track->crystal_cap - p_cfo_track->def_x_cap) : (p_cfo_track->def_x_cap - p_cfo_track->crystal_cap);
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("CFO_avg = ((%d kHz)) , CrystalCap_tracking = ((%s%d))\n",
			p_cfo_track->CFO_ave_pre, ((p_cfo_track->crystal_cap > p_cfo_track->def_x_cap) ? "+" : "-"), tmp_val_u1));

		/* Condition number */
#if (RTL8822B_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8822B) {
			tmp_val = phydm_get_condition_number_8822B(p_dm_odm);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("Condition number = ((%d))\n", tmp_val));
		}
#endif

#if (ODM_PHY_STATUS_NEW_TYPE_SUPPORT == 1)
		/*STBC or LDPC pkt*/
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("LDPC = %s, STBC = %s\n", (p_dm_odm->phy_dbg_info.is_ldpc_pkt) ? "Y" : "N", (p_dm_odm->phy_dbg_info.is_stbc_pkt) ? "Y" : "N"));
#endif
	} else
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("No Link !!!\n"));

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca, false_alm_cnt->cnt_cca_all));

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail, false_alm_cnt->cnt_all));

#if (ODM_IC_11N_SERIES_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",
			false_alm_cnt->cnt_parity_fail, false_alm_cnt->cnt_rate_illegal, false_alm_cnt->cnt_crc8_fail, false_alm_cnt->cnt_mcs_fail, false_alm_cnt->cnt_fast_fsync, false_alm_cnt->cnt_sb_search_fail));
	}
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("is_linked = %d, Num_client = %d, rssi_min = %d, current_igi = 0x%x, bNoisy=%d\n\n",
		p_dm_odm->is_linked, p_dm_odm->number_linked_client, p_dm_odm->rssi_min, p_dm_dig_table->cur_ig_value, p_dm_odm->noisy_decision));

	/*
		temp_reg = odm_get_bb_reg(p_dm_odm, 0xDD0, MASKBYTE0);
		ODM_RT_TRACE(p_dm_odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xDD0 = 0x%x\n",temp_reg));

		temp_reg = odm_get_bb_reg(p_dm_odm, 0xDDc, MASKBYTE1);
		ODM_RT_TRACE(p_dm_odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xDDD = 0x%x\n",temp_reg));

		temp_reg = odm_get_bb_reg(p_dm_odm, 0xc50, MASKBYTE0);
		ODM_RT_TRACE(p_dm_odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("0xC50 = 0x%x\n",temp_reg));

		temp_reg = odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, 0x0, 0x3fe0);
		ODM_RT_TRACE(p_dm_odm,ODM_COMP_COMMON, ODM_DBG_LOUD, ("RF 0x0[13:5] = 0x%x\n\n",temp_reg));
	*/

#endif
}


void phydm_basic_profile(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	char  *cut = NULL;
	char *ic_type = NULL;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32	commit_ver = 0;
	u32	date = 0;
	char	*commit_by = NULL;
	u32	release_ver = 0;

	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% Basic Profile %"));

	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		ic_type = "RTL8188E";
		date = RELEASE_DATE_8188E;
		commit_by = COMMIT_BY_8188E;
		release_ver = RELEASE_VERSION_8188E;
#endif
	}
#if (RTL8812A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		ic_type = "RTL8812A";
		date = RELEASE_DATE_8812A;
		commit_by = COMMIT_BY_8812A;
		release_ver = RELEASE_VERSION_8812A;
	}
#endif
#if (RTL8821A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821) {
		ic_type = "RTL8821A";
		date = RELEASE_DATE_8821A;
		commit_by = COMMIT_BY_8821A;
		release_ver = RELEASE_VERSION_8821A;
	}
#endif
#if (RTL8192E_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		ic_type = "RTL8192E";
		date = RELEASE_DATE_8192E;
		commit_by = COMMIT_BY_8192E;
		release_ver = RELEASE_VERSION_8192E;
	}
#endif
#if (RTL8723B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
		ic_type = "RTL8723B";
		date = RELEASE_DATE_8723B;
		commit_by = COMMIT_BY_8723B;
		release_ver = RELEASE_VERSION_8723B;
	}
#endif
#if (RTL8814A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8814A) {
		ic_type = "RTL8814A";
		date = RELEASE_DATE_8814A;
		commit_by = COMMIT_BY_8814A;
		release_ver = RELEASE_VERSION_8814A;
	}
#endif
#if (RTL8881A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8881A) {
		ic_type = "RTL8881A";
		/**/
	}
#endif
#if (RTL8822B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8822B) {
		ic_type = "RTL8822B";
		date = RELEASE_DATE_8822B;
		commit_by = COMMIT_BY_8822B;
		release_ver = RELEASE_VERSION_8822B;
	}
#endif
#if (RTL8197F_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8197F) {
		ic_type = "RTL8197F";
		date = RELEASE_DATE_8197F;
		commit_by = COMMIT_BY_8197F;
		release_ver = RELEASE_VERSION_8197F;
	}
#endif

#if (RTL8703B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8703B) {

		ic_type = "RTL8703B";
		date = RELEASE_DATE_8703B;
		commit_by = COMMIT_BY_8703B;
		release_ver = RELEASE_VERSION_8703B;

	}
#endif
#if (RTL8195A_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8195A) {
		ic_type = "RTL8195A";
		/**/
	}
#endif
#if (RTL8188F_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8188F) {
		ic_type = "RTL8188F";
		date = RELEASE_DATE_8188F;
		commit_by = COMMIT_BY_8188F;
		release_ver = RELEASE_VERSION_8188F;
	}
#endif
#if (RTL8723D_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8723D) {
		ic_type = "RTL8723D";
		date = RELEASE_DATE_8723D;
		commit_by = COMMIT_BY_8723D;
		release_ver = RELEASE_VERSION_8723D;
		/**/
	}
#endif

/* JJ ADD 20161014 */
#if (RTL8710B_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8710B) {
		ic_type = "RTL8710B";
		date = RELEASE_DATE_8710B;
		commit_by = COMMIT_BY_8710B;
		release_ver = RELEASE_VERSION_8710B;
		/**/
	}
#endif

#if (RTL8821C_SUPPORT == 1)
	else if (p_dm_odm->support_ic_type == ODM_RTL8821C) {
		ic_type = "RTL8821C";
		date = RELEASE_DATE_8821C;
		commit_by = COMMIT_BY_8821C;
		release_ver = RELEASE_VERSION_8821C;
	}
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s (MP Chip: %s)\n", "IC type", ic_type, p_dm_odm->is_mp_chip ? "Yes" : "No"));

	if (p_dm_odm->cut_version == ODM_CUT_A)
		cut = "A";
	else if (p_dm_odm->cut_version == ODM_CUT_B)
		cut = "B";
	else if (p_dm_odm->cut_version == ODM_CUT_C)
		cut = "C";
	else if (p_dm_odm->cut_version == ODM_CUT_D)
		cut = "D";
	else if (p_dm_odm->cut_version == ODM_CUT_E)
		cut = "E";
	else if (p_dm_odm->cut_version == ODM_CUT_F)
		cut = "F";
	else if (p_dm_odm->cut_version == ODM_CUT_I)
		cut = "I";
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "cut version", cut));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter version", odm_get_hw_img_version(p_dm_odm)));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter Commit date", date));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY Parameter Commit by", commit_by));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d\n", "PHY Parameter Release version", release_ver));

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	{
		struct _ADAPTER		*adapter = p_dm_odm->adapter;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW version", adapter->MgntInfo.FirmwareVersion, adapter->MgntInfo.FirmwareSubVersion));
	}
#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	{
		struct rtl8192cd_priv *priv = p_dm_odm->priv;
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW version", priv->pshare->fw_version, priv->pshare->fw_sub_version));
	}
#else
	{
		struct _ADAPTER		*adapter = p_dm_odm->adapter;
		HAL_DATA_TYPE		*p_hal_data = GET_HAL_DATA(adapter);
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %d (Subversion: %d)\n", "FW version", p_hal_data->firmware_version, p_hal_data->firmware_sub_version));
	}
#endif
	/* 1 PHY DM version List */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "% PHYDM version %"));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Code base", PHYDM_CODE_BASE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Release Date", PHYDM_RELEASE_DATE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "adaptivity", ADAPTIVITY_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "DIG", DIG_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic BB PowerSaving", DYNAMIC_BBPWRSAV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "CFO Tracking", CFO_TRACKING_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Antenna Diversity", ANTDIV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Power Tracking", POWRTRACKING_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic TxPower", DYNAMIC_TXPWR_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "RA Info", RAINFO_VERSION));
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Antenna Detection", ANTDECT_VERSION));
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Auto channel Selection", ACS_VERSION));
#if PHYDM_SUPPORT_EDCA
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "EDCA Turbo", EDCATURBO_VERSION));
#endif
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "path Diversity", PATHDIV_VERSION));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "LA mode", DYNAMIC_LA_MODE));
	PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "Dynamic RX path", DYNAMIC_RX_PATH_VERSION));

#if (RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8822B", PHY_CONFIG_VERSION_8822B));

#endif
#if (RTL8197F_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		PHYDM_SNPRINTF((output + used, out_len - used, "  %-35s: %s\n", "PHY config 8197F", PHY_CONFIG_VERSION_8197F));
#endif
	*_used = used;
	*_out_len = out_len;
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

#if CONFIG_PHYDM_DEBUG_FUNCTION
void
phydm_fw_trace_en_h2c(
	void		*p_dm_void,
	boolean		enable,
	u32		fw_debug_component,
	u32		monitor_mode,
	u32		macid
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			h2c_parameter[7] = {0};
	u8			cmd_length;

	if (p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES) {

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


	ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("---->\n"));
	if (monitor_mode == 0)
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d ))\n", enable));
	else
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n", enable, monitor_mode, macid));
	odm_fill_h2c_cmd(p_dm_odm, PHYDM_H2C_FW_TRACE_EN, cmd_length, h2c_parameter);
}


#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
boolean
phydm_api_set_txagc(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u32					power_index,
	enum odm_rf_radio_path_e		path,
	u8					hw_rate,
	boolean					is_single_rate
)
{
	boolean		ret = false;

#if ((RTL8822B_SUPPORT == 1) || (RTL8821C_SUPPORT == 1))
	if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
		if (is_single_rate) {
#if (RTL8822B_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8822B)
				ret = phydm_write_txagc_1byte_8822b(p_dm_odm, power_index, path, hw_rate);
#endif
#if (RTL8821C_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8821C)
				ret = phydm_write_txagc_1byte_8821c(p_dm_odm, power_index, path, hw_rate);
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			set_current_tx_agc(p_dm_odm->priv, path, hw_rate, (u8)power_index);
#endif

		} else {
			u8	i;
#if (RTL8822B_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8822B)
				ret = config_phydm_write_txagc_8822b(p_dm_odm, power_index, path, hw_rate);
#endif
#if (RTL8821C_SUPPORT == 1)
			if (p_dm_odm->support_ic_type == ODM_RTL8821C)
				ret = config_phydm_write_txagc_8821c(p_dm_odm, power_index, path, hw_rate);
#endif
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
			for (i = 0; i < 4; i++)
				set_current_tx_agc(p_dm_odm->priv, path, (hw_rate + i), (u8)power_index);
#endif
		}
	}
#endif


#if (RTL8197F_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_write_txagc_8197f(p_dm_odm, power_index, path, hw_rate);
#endif

	return ret;
}

u8
phydm_api_get_txagc(
	struct PHY_DM_STRUCT				*p_dm_odm,
	enum odm_rf_radio_path_e	path,
	u8					hw_rate
)
{
	u8	ret = 0;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_read_txagc_8822b(p_dm_odm, path, hw_rate);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_read_txagc_8197f(p_dm_odm, path, hw_rate);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8821C)
		ret = config_phydm_read_txagc_8821c(p_dm_odm, path, hw_rate);
#endif

	return ret;
}


boolean
phydm_api_switch_bw_channel(
	struct PHY_DM_STRUCT				*p_dm_odm,
	u8					central_ch,
	u8					primary_ch_idx,
	enum odm_bw_e				bandwidth
)
{
	boolean		ret = false;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_switch_channel_bw_8822b(p_dm_odm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_switch_channel_bw_8197f(p_dm_odm, central_ch, primary_ch_idx, bandwidth);
#endif

#if (RTL8821C_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8821C)
		ret = config_phydm_switch_channel_bw_8821c(p_dm_odm, central_ch, primary_ch_idx, bandwidth);
#endif

	return ret;
}

boolean
phydm_api_trx_mode(
	struct PHY_DM_STRUCT				*p_dm_odm,
	enum odm_rf_path_e			tx_path,
	enum odm_rf_path_e			rx_path,
	boolean					is_tx2_path
)
{
	boolean		ret = false;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_trx_mode_8822b(p_dm_odm, tx_path, rx_path, is_tx2_path);
#endif

#if (RTL8197F_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & ODM_RTL8197F)
		ret = config_phydm_trx_mode_8197f(p_dm_odm, tx_path, rx_path, is_tx2_path);
#endif

	return ret;
}
#endif

void
phydm_get_per_path_txagc(
	void			*p_dm_void,
	u8			path,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			rate_idx;
	u8			txagc;
	u32			used = *_used;
	u32			out_len = *_out_len;

#if ((RTL8822B_SUPPORT == 1) || (RTL8197F_SUPPORT == 1) || (RTL8821C_SUPPORT == 1))
	if (((p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) && (path <= ODM_RF_PATH_B)) ||
	    ((p_dm_odm->support_ic_type & (ODM_RTL8821C)) && (path <= ODM_RF_PATH_A))) {
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

			txagc = phydm_api_get_txagc(p_dm_odm, (enum odm_rf_radio_path_e) path, rate_idx);
			if (config_phydm_read_txagc_check(txagc))
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%02x    ", txagc));
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "  0x%s    ", "xx"));
		}
	}
#endif
}


void
phydm_get_txagc(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/* path-A */
	PHYDM_SNPRINTF((output + used, out_len - used, "%-35s\n", "path-A===================="));
	phydm_get_per_path_txagc(p_dm_odm, ODM_RF_PATH_A, _used, output, _out_len);

	/* path-B */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-B===================="));
	phydm_get_per_path_txagc(p_dm_odm, ODM_RF_PATH_B, _used, output, _out_len);

	/* path-C */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-C===================="));
	phydm_get_per_path_txagc(p_dm_odm, ODM_RF_PATH_C, _used, output, _out_len);

	/* path-D */
	PHYDM_SNPRINTF((output + used, out_len - used, "\n%-35s\n", "path-D===================="));
	phydm_get_per_path_txagc(p_dm_odm, ODM_RF_PATH_D, _used, output, _out_len);

}

void
phydm_set_txagc(
	void			*p_dm_void,
	u32			*const dm_value,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/*dm_value[1] = path*/
	/*dm_value[2] = hw_rate*/
	/*dm_value[3] = power_index*/

#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
	if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C)) {
		if (dm_value[1] <= 1) {
			if ((u8)dm_value[2] != 0xff) {
				if (phydm_api_set_txagc(p_dm_odm, dm_value[3], (enum odm_rf_radio_path_e) dm_value[1], (u8)dm_value[2], true))
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s%x\n", "Write path-", dm_value[1], "rate index-0x", dm_value[2], " = 0x", dm_value[3]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "  %s%d   %s%x%s\n", "Write path-", (dm_value[1] & 0x1), "rate index-0x", (dm_value[2] & 0x7f), " fail"));
			} else {
				u8	i;
				u32	power_index;
				boolean	status = true;

				power_index = (dm_value[3] & 0x3f);

				if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
					power_index = (power_index << 24) | (power_index << 16) | (power_index << 8) | (power_index);

					for (i = 0; i < ODM_RATEVHTSS2MCS9; i += 4)
						status = (status & phydm_api_set_txagc(p_dm_odm, power_index, (enum odm_rf_radio_path_e) dm_value[1], i, false));
				} else if (p_dm_odm->support_ic_type & ODM_RTL8197F) {
					for (i = 0; i <= ODM_RATEMCS15; i++)
						status = (status & phydm_api_set_txagc(p_dm_odm, power_index, (enum odm_rf_radio_path_e) dm_value[1], i, false));
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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			pre_debug_components, one = 1;
	u32			used = *_used;
	u32			out_len = *_out_len;

	pre_debug_components = p_dm_odm->debug_components;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Debug Message] PhyDM Selection"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))DIG\n", ((p_dm_odm->debug_components & ODM_COMP_DIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))RA_MASK\n", ((p_dm_odm->debug_components & ODM_COMP_RA_MASK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))DYNAMIC_TXPWR\n", ((p_dm_odm->debug_components & ODM_COMP_DYNAMIC_TXPWR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))FA_CNT\n", ((p_dm_odm->debug_components & ODM_COMP_FA_CNT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "04. (( %s ))RSSI_MONITOR\n", ((p_dm_odm->debug_components & ODM_COMP_RSSI_MONITOR) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "05. (( %s ))SNIFFER\n", ((p_dm_odm->debug_components & ODM_COMP_SNIFFER) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "06. (( %s ))ANT_DIV\n", ((p_dm_odm->debug_components & ODM_COMP_ANT_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "07. (( %s ))DFS\n", ((p_dm_odm->debug_components & ODM_COMP_DFS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "08. (( %s ))NOISY_DETECT\n", ((p_dm_odm->debug_components & ODM_COMP_NOISY_DETECT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "09. (( %s ))RATE_ADAPTIVE\n", ((p_dm_odm->debug_components & ODM_COMP_RATE_ADAPTIVE) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "10. (( %s ))PATH_DIV\n", ((p_dm_odm->debug_components & ODM_COMP_PATH_DIV) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "12. (( %s ))DYNAMIC_PRICCA\n", ((p_dm_odm->debug_components & ODM_COMP_DYNAMIC_PRICCA) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "14. (( %s ))MP\n", ((p_dm_odm->debug_components & ODM_COMP_MP) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "15. (( %s ))struct _CFO_TRACKING_\n", ((p_dm_odm->debug_components & ODM_COMP_CFO_TRACKING) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "16. (( %s ))struct _ACS_\n", ((p_dm_odm->debug_components & ODM_COMP_ACS) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "17. (( %s ))ADAPTIVITY\n", ((p_dm_odm->debug_components & PHYDM_COMP_ADAPTIVITY) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "18. (( %s ))RA_DBG\n", ((p_dm_odm->debug_components & PHYDM_COMP_RA_DBG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "19. (( %s ))TXBF\n", ((p_dm_odm->debug_components & PHYDM_COMP_TXBF) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "20. (( %s ))EDCA_TURBO\n", ((p_dm_odm->debug_components & ODM_COMP_EDCA_TURBO) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "22. (( %s ))FW_DEBUG_TRACE\n", ((p_dm_odm->debug_components & ODM_FW_DEBUG_TRACE) ? ("V") : ("."))));

		PHYDM_SNPRINTF((output + used, out_len - used, "24. (( %s ))TX_PWR_TRACK\n", ((p_dm_odm->debug_components & ODM_COMP_TX_PWR_TRACK) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "26. (( %s ))CALIBRATION\n", ((p_dm_odm->debug_components & ODM_COMP_CALIBRATION) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "28. (( %s ))PHY_CONFIG\n", ((p_dm_odm->debug_components & ODM_PHY_CONFIG) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "29. (( %s ))INIT\n", ((p_dm_odm->debug_components & ODM_COMP_INIT) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "30. (( %s ))COMMON\n", ((p_dm_odm->debug_components & ODM_COMP_COMMON) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "31. (( %s ))API\n", ((p_dm_odm->debug_components & ODM_COMP_API) ? ("V") : ("."))));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else if (dm_value[0] == 101) {
		p_dm_odm->debug_components = 0;
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Disable all debug components"));
	} else {
		if (dm_value[1] == 1)   /*enable*/
			p_dm_odm->debug_components |= (one << dm_value[0]);
		else if (dm_value[1] == 2)   /*disable*/
			p_dm_odm->debug_components &= ~(one << dm_value[0]);
		else
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
	}
	PHYDM_SNPRINTF((output + used, out_len - used, "pre-DbgComponents = 0x%x\n", pre_debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "Curr-DbgComponents = 0x%x\n", p_dm_odm->debug_components));
	PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			pre_fw_debug_components, one = 1;
	u32			used = *_used;
	u32			out_len = *_out_len;

	pre_fw_debug_components = p_dm_odm->fw_debug_components;

	PHYDM_SNPRINTF((output + used, out_len - used, "\n%s\n", "================================"));
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[FW Debug Component]"));
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));
		PHYDM_SNPRINTF((output + used, out_len - used, "00. (( %s ))RA\n", ((p_dm_odm->fw_debug_components & PHYDM_FW_COMP_RA) ? ("V") : ("."))));

		if (p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES) {
			PHYDM_SNPRINTF((output + used, out_len - used, "01. (( %s ))MU\n", ((p_dm_odm->fw_debug_components & PHYDM_FW_COMP_MU) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "02. (( %s ))path Div\n", ((p_dm_odm->fw_debug_components & PHYDM_FW_COMP_PHY_CONFIG) ? ("V") : ("."))));
			PHYDM_SNPRINTF((output + used, out_len - used, "03. (( %s ))Phy Config\n", ((p_dm_odm->fw_debug_components & PHYDM_FW_COMP_PHY_CONFIG) ? ("V") : ("."))));
		}
		PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "================================"));

	} else {
		if (dm_value[0] == 101) {
			p_dm_odm->fw_debug_components = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "Clear all fw debug components"));
		} else {
			if (dm_value[1] == 1)   /*enable*/
				p_dm_odm->fw_debug_components |= (one << dm_value[0]);
			else if (dm_value[1] == 2)   /*disable*/
				p_dm_odm->fw_debug_components &= ~(one << dm_value[0]);
			else
				PHYDM_SNPRINTF((output + used, out_len - used, "%s\n", "[Warning!!!]  1:enable,  2:disable"));
		}

		if (p_dm_odm->fw_debug_components == 0) {
			p_dm_odm->debug_components &= ~ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(p_dm_odm, false, p_dm_odm->fw_debug_components, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
		} else {
			p_dm_odm->debug_components |= ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(p_dm_odm, true, p_dm_odm->fw_debug_components, dm_value[2], dm_value[3]); /*H2C to enable C2H Msg*/
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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			addr = 0;
	u32			used = *_used;
	u32			out_len = *_out_len;


	/* BB Reg, For Nseries IC we only need to dump page8 to pageF using 3 digits*/
	for (addr = 0x800; addr < 0xfff; addr += 4) {
		if (p_dm_odm->support_ic_type & ODM_IC_11N_SERIES)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%03x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));
		else
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));
	}

	if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8814A | ODM_RTL8821C)) {

		if (p_dm_odm->rf_type > ODM_2T2R) {
			for (addr = 0x1800; addr < 0x18ff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));
		}

		if (p_dm_odm->rf_type > ODM_3T3R) {
			for (addr = 0x1a00; addr < 0x1aff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));
		}

		for (addr = 0x1900; addr < 0x19ff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));

		for (addr = 0x1c00; addr < 0x1cff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));

		for (addr = 0x1f00; addr < 0x1fff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));
	}
}

void
phydm_dump_all_reg(
	void			*p_dm_void,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			addr = 0;
	u32			used = *_used;
	u32			out_len = *_out_len;

	/* dump MAC register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "MAC==========\n"));
	for (addr = 0; addr < 0x7ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));

	for (addr = 0x1000; addr < 0x17ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%04x 0x%08x\n", addr, odm_get_bb_reg(p_dm_odm, addr, MASKDWORD)));

	/* dump BB register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "BB==========\n"));
	phydm_dump_bb_reg(p_dm_odm, &used, output, &out_len);

	/* dump RF register */
	PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-A==========\n"));
	for (addr = 0; addr < 0xFF; addr++)
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_A, addr, RFREGOFFSETMASK)));

	if (p_dm_odm->rf_type > ODM_1T1R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-B==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_B, addr, RFREGOFFSETMASK)));
	}

	if (p_dm_odm->rf_type > ODM_2T2R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-C==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_C, addr, RFREGOFFSETMASK)));
	}

	if (p_dm_odm->rf_type > ODM_3T3R) {
		PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "RF-D==========\n"));
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF((output + used, out_len - used, "0x%02x 0x%05x\n", addr, odm_get_rf_reg(p_dm_odm, ODM_RF_PATH_D, addr, RFREGOFFSETMASK)));
	}
}

void
phydm_enable_big_jump(
	struct PHY_DM_STRUCT	*p_dm_odm,
	boolean		state
)
{
#if (RTL8822B_SUPPORT == 1)
	struct _dynamic_initial_gain_threshold_			*p_dm_dig_table = &p_dm_odm->dm_dig_table;

	if (state == false) {
		p_dm_odm->dm_dig_table.enable_adjust_big_jump = false;
		odm_set_bb_reg(p_dm_odm, 0x8c8, 0xfe, ((p_dm_dig_table->big_jump_step3 << 5) | (p_dm_dig_table->big_jump_step2 << 3) | p_dm_dig_table->big_jump_step1));
	} else
		p_dm_odm->dm_dig_table.enable_adjust_big_jump = true;
#endif
}

#if (RTL8822B_SUPPORT == 1)

void
phydm_show_rx_rate(
	struct PHY_DM_STRUCT			*p_dm_odm,
	u32			*_used,
	char				*output,
	u32			*_out_len
)
{
	u32			used = *_used;
	u32			out_len = *_out_len;

	PHYDM_SNPRINTF((output + used, out_len - used, "=====Rx SU rate Statistics=====\n"));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[0], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[1], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[2], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[3]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[4], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[5], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[6], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[7]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[8], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[9]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[10], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[11], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[12], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[13]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[14], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[15], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[16], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[17]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_vht_pkt[18], p_dm_odm->phy_dbg_info.num_qry_vht_pkt[19]));

	PHYDM_SNPRINTF((output + used, out_len - used, "=====Rx MU rate Statistics=====\n"));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[0], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[1], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[2], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[3]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[4], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[5], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[6], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[7]));
	PHYDM_SNPRINTF((output + used, out_len - used, "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[8], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[9]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[10], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[11], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[12], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[13]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[14], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[15], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[16], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[17]));
	PHYDM_SNPRINTF((output + used, out_len - used, "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[18], p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[19]));

}

#endif


struct _PHYDM_COMMAND {
	char name[16];
	u8 id;
};

enum PHYDM_CMD_ID {
	PHYDM_HELP,
	PHYDM_DEMO,
	PHYDM_RA,
	PHYDM_PROFILE,
	PHYDM_ANTDIV,
	PHYDM_PATHDIV,
	PHYDM_DEBUG,
	PHYDM_FW_DEBUG,
	PHYDM_SUPPORT_ABILITY,
	PHYDM_GET_TXAGC,
	PHYDM_SET_TXAGC,
	PHYDM_SMART_ANT,
	PHYDM_API,
	PHYDM_TRX_PATH,
	PHYDM_LA_MODE,
	PHYDM_DUMP_REG,
	PHYDM_MU_MIMO,
	PHYDM_HANG,
	PHYDM_BIG_JUMP,
	PHYDM_SHOW_RXRATE,
	PHYDM_NBI_EN,
	PHYDM_CSI_MASK_EN,
	PHYDM_DFS,
	PHYDM_IQK,
	PHYDM_NHM,
	PHYDM_CLM,
	PHYDM_BB_INFO,
	PHYDM_TXBF,
	PHYDM_PAUSE_DIG_EN,
	PHYDM_H2C,
	PHYDM_ANT_SWITCH,
	PHYDM_DYNAMIC_RA_PATH,
	PHYDM_PSD,
	PHYDM_DEBUG_PORT
};

struct _PHYDM_COMMAND phy_dm_ary[] = {
	{"-h", PHYDM_HELP},		/*do not move this element to other position*/
	{"demo", PHYDM_DEMO},	/*do not move this element to other position*/
	{"ra", PHYDM_RA},
	{"profile", PHYDM_PROFILE},
	{"antdiv", PHYDM_ANTDIV},
	{"pathdiv", PHYDM_PATHDIV},
	{"dbg", PHYDM_DEBUG},
	{"fw_dbg", PHYDM_FW_DEBUG},
	{"ability", PHYDM_SUPPORT_ABILITY},
	{"get_txagc", PHYDM_GET_TXAGC},
	{"set_txagc", PHYDM_SET_TXAGC},
	{"smtant", PHYDM_SMART_ANT},
	{"api", PHYDM_API},
	{"trxpath", PHYDM_TRX_PATH},
	{"lamode", PHYDM_LA_MODE},
	{"dumpreg", PHYDM_DUMP_REG},
	{"mu", PHYDM_MU_MIMO},
	{"hang", PHYDM_HANG},
	{"bigjump", PHYDM_BIG_JUMP},
	{"rxrate", PHYDM_SHOW_RXRATE},
	{"nbi", PHYDM_NBI_EN},
	{"csi_mask", PHYDM_CSI_MASK_EN},
	{"dfs", PHYDM_DFS},
	{"iqk", PHYDM_IQK},
	{"nhm", PHYDM_NHM},
	{"clm", PHYDM_CLM},
	{"bbinfo", PHYDM_BB_INFO},
	{"txbf", PHYDM_TXBF},
	{"pause_dig", PHYDM_PAUSE_DIG_EN},
	{"h2c", PHYDM_H2C},
	{"ant_switch", PHYDM_ANT_SWITCH},
	{"drp", PHYDM_DYNAMIC_RA_PATH},
	{"psd", PHYDM_PSD},
	{"dbgport", PHYDM_DEBUG_PORT}
};

#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/

void
phydm_cmd_parser(
	struct PHY_DM_STRUCT	*p_dm_odm,
	char		input[][MAX_ARGV],
	u32	input_num,
	u8	flag,
	char		*output,
	u32	out_len
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	u32 used = 0;
	u8 id = 0;
	int var1[10] = {0};
	int i, input_idx = 0, phydm_ary_size;
	char help[] = "-h";

	if (flag == 0) {
		PHYDM_SNPRINTF((output + used, out_len - used, "GET, nothing to print\n"));
		return;
	}

	PHYDM_SNPRINTF((output + used, out_len - used, "\n"));

	/* Parsing Cmd ID */
	if (input_num) {

		phydm_ary_size = sizeof(phy_dm_ary) / sizeof(struct _PHYDM_COMMAND);
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

	case PHYDM_RA:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);

				/*PHYDM_SNPRINTF((output + used, out_len - used, "new SET, RA_var[%d]= (( %d ))\n", i, var1[i]));*/
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			/*PHYDM_SNPRINTF((output+used, out_len-used, "odm_RA_debug\n"));*/
#if (defined(CONFIG_RA_DBG_CMD))
			odm_RA_debug((void *)p_dm_odm, (u32 *) var1);
#else
			phydm_RA_debug_PCR(p_dm_odm, (u32 *)var1, &used, output, &out_len);
#endif
		}


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
			phydm_antdiv_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);
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
			odm_pathdiv_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);
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
			phydm_debug_trace(p_dm_odm, (u32 *)var1, &used, output, &out_len);
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
			phydm_fw_debug_trace(p_dm_odm, (u32 *)var1, &used, output, &out_len);

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
			phydm_support_ability_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);
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
#if (defined(CONFIG_HL_SMART_ANTENNA_TYPE1)) || (defined(CONFIG_HL_SMART_ANTENNA_TYPE2))
			phydm_hl_smart_ant_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);
#endif
#endif
		}

		break;

	case PHYDM_API:
#if (RTL8822B_SUPPORT == 1 || RTL8197F_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		{
			if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C)) {
				boolean	is_enable_dbg_mode;
				u8 central_ch, primary_ch_idx, bandwidth;

				for (i = 0; i < 4; i++) {
					if (input[i + 1])
						PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				}

				is_enable_dbg_mode = (boolean)var1[0];
				central_ch = (u8) var1[1];
				primary_ch_idx = (u8) var1[2];
				bandwidth = (enum odm_bw_e) var1[3];

				if (is_enable_dbg_mode) {
					p_dm_odm->is_disable_phy_api = false;
					phydm_api_switch_bw_channel(p_dm_odm, central_ch, primary_ch_idx, (enum odm_bw_e) bandwidth);
					p_dm_odm->is_disable_phy_api = true;
					PHYDM_SNPRINTF((output + used, out_len - used, "central_ch = %d, primary_ch_idx = %d, bandwidth = %d\n", central_ch, primary_ch_idx, bandwidth));
				} else {
					p_dm_odm->is_disable_phy_api = false;
					PHYDM_SNPRINTF((output + used, out_len - used, "Disable API debug mode\n"));
				}
			} else
				PHYDM_SNPRINTF((output + used, out_len - used, "This IC doesn't support PHYDM API function\n"));
		}
#else
		PHYDM_SNPRINTF((output + used, out_len - used, "This IC doesn't support PHYDM API function\n"));
#endif
		break;

	case PHYDM_PROFILE: /*echo profile, >cmd*/
		phydm_basic_profile(p_dm_odm, &used, output, &out_len);
		break;

	case PHYDM_GET_TXAGC:
		phydm_get_txagc(p_dm_odm, &used, output, &out_len);
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
				p_dm_odm->is_disable_phy_api = false;
				phydm_set_txagc(p_dm_odm, (u32 *)var1, &used, output, &out_len);
				p_dm_odm->is_disable_phy_api = true;
			} else {
				p_dm_odm->is_disable_phy_api = false;
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
		if (p_dm_odm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
			u8		tx_path, rx_path;
			boolean		is_enable_dbg_mode, is_tx2_path;

			is_enable_dbg_mode = (boolean)var1[0];
			tx_path = (u8) var1[1];
			rx_path = (u8) var1[2];
			is_tx2_path = (boolean) var1[3];

			if (is_enable_dbg_mode) {
				p_dm_odm->is_disable_phy_api = false;
				phydm_api_trx_mode(p_dm_odm, (enum odm_rf_path_e) tx_path, (enum odm_rf_path_e) rx_path, is_tx2_path);
				p_dm_odm->is_disable_phy_api = true;
				PHYDM_SNPRINTF((output + used, out_len - used, "tx_path = 0x%x, rx_path = 0x%x, is_tx2_path = %d\n", tx_path, rx_path, is_tx2_path));
			} else {
				p_dm_odm->is_disable_phy_api = false;
				PHYDM_SNPRINTF((output + used, out_len - used, "Disable API debug mode\n"));
			}
		} else
#endif
			phydm_config_trx_path(p_dm_odm, (u32 *)var1, &used, output, &out_len);

		break;

	case PHYDM_LA_MODE:

#if (PHYDM_LA_MODE_SUPPORT == 1)
		p_dm_odm->support_ability &= ~(ODM_BB_FA_CNT);
		phydm_lamode_trigger_setting(p_dm_odm, &input[0], &used, output, &out_len, input_num);
		p_dm_odm->support_ability |= ODM_BB_FA_CNT;
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
			phydm_dump_bb_reg(p_dm_odm, &used, output, &out_len);
		else if (type == 1)
			phydm_dump_all_reg(p_dm_odm, &used, output, &out_len);
	}
	break;

	case PHYDM_MU_MIMO:
#if (RTL8822B_SUPPORT == 1)

		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		else
			var1[0] = 0;

		if (var1[0] == 1) {
			int index, ptr;
			u32 dword_h, dword_l;

			PHYDM_SNPRINTF((output + used, out_len - used, "Get MU BFee CSI\n"));
			odm_set_bb_reg(p_dm_odm, 0x9e8, BIT(17) | BIT16, 2); /*Read BFee*/
			odm_set_bb_reg(p_dm_odm, 0x1910, BIT(15), 1); /*Select BFee's CSI report*/
			odm_set_bb_reg(p_dm_odm, 0x19b8, BIT(6), 1); /*set as CSI report*/
			odm_set_bb_reg(p_dm_odm, 0x19a8, 0xFFFF, 0xFFFF); /*disable gated_clk*/

			for (index = 0; index < 80; index++) {
				ptr = index + 256;
				if (ptr > 311)
					ptr -= 312;
				odm_set_bb_reg(p_dm_odm, 0x1910, 0x03FF0000, ptr); /*Select Address*/
				dword_h = odm_get_bb_reg(p_dm_odm, 0xF74, MASKDWORD);
				dword_l = odm_get_bb_reg(p_dm_odm, 0xF5C, MASKDWORD);
				if (index % 2 == 0)
					PHYDM_SNPRINTF((output + used, out_len - used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
						dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8, (dword_l & MASKBYTE2) >> 16, (dword_l & MASKBYTE3) >> 24,
						dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8, (dword_h & MASKBYTE2) >> 16, (dword_h & MASKBYTE3) >> 24));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
						dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8, (dword_l & MASKBYTE2) >> 16, (dword_l & MASKBYTE3) >> 24,
						dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8, (dword_h & MASKBYTE2) >> 16, (dword_h & MASKBYTE3) >> 24));
			}
		} else if (var1[0] == 2) {
			int index, ptr;
			u32 dword_h, dword_l;

			PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);
			PHYDM_SNPRINTF((output + used, out_len - used, "Get MU BFer's STA%d CSI\n", var1[1]));
			odm_set_bb_reg(p_dm_odm, 0x9e8, BIT(24), 0); /*Read BFer*/
			odm_set_bb_reg(p_dm_odm, 0x9e8, BIT(25), 1); /*enable Read/Write RAM*/
			odm_set_bb_reg(p_dm_odm, 0x9e8, BIT(30) | BIT29 | BIT28, var1[1]); /*read which STA's CSI report*/
			odm_set_bb_reg(p_dm_odm, 0x1910, BIT(15), 0); /*select BFer's CSI*/
			odm_set_bb_reg(p_dm_odm, 0x19e0, 0x00003FC0, 0xFF); /*disable gated_clk*/

			for (index = 0; index < 80; index++) {
				ptr = index + 256;
				if (ptr > 311)
					ptr -= 312;
				odm_set_bb_reg(p_dm_odm, 0x1910, 0x03FF0000, ptr); /*Select Address*/
				dword_h = odm_get_bb_reg(p_dm_odm, 0xF74, MASKDWORD);
				dword_l = odm_get_bb_reg(p_dm_odm, 0xF5C, MASKDWORD);
				if (index % 2 == 0)
					PHYDM_SNPRINTF((output + used, out_len - used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
						dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8, (dword_l & MASKBYTE2) >> 16, (dword_l & MASKBYTE3) >> 24,
						dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8, (dword_h & MASKBYTE2) >> 16, (dword_h & MASKBYTE3) >> 24));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
						dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8, (dword_l & MASKBYTE2) >> 16, (dword_l & MASKBYTE3) >> 24,
						dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8, (dword_h & MASKBYTE2) >> 16, (dword_h & MASKBYTE3) >> 24));

				PHYDM_SNPRINTF((output + used, out_len - used, "ptr=%d : 0x%8x  %8x\n", ptr, dword_h, dword_l));
			}

		}
#endif
		break;

	case PHYDM_BIG_JUMP:
	{
#if (RTL8822B_SUPPORT == 1)
		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			phydm_enable_big_jump(p_dm_odm, (boolean)(var1[0]));
		} else
			PHYDM_SNPRINTF((output + used, out_len - used, "unknown command!\n"));
#else
		PHYDM_SNPRINTF((output + used, out_len - used, "The command is only for 8822B!\n"));
#endif
		break;
	}

	case PHYDM_HANG:
		phydm_bb_rx_hang_info(p_dm_odm, &used, output, &out_len);
		break;

	case PHYDM_SHOW_RXRATE:
#if (RTL8822B_SUPPORT == 1)
		{
			u8	rate_idx;

			if (input[1])
				PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

			if (var1[0] == 1)
				phydm_show_rx_rate(p_dm_odm, &used, output, &out_len);
			else {
				PHYDM_SNPRINTF((output + used, out_len - used, "Reset Rx rate counter\n"));

				for (rate_idx = 0; rate_idx < 40; rate_idx++) {
					p_dm_odm->phy_dbg_info.num_qry_vht_pkt[rate_idx] = 0;
					p_dm_odm->phy_dbg_info.num_qry_mu_vht_pkt[rate_idx] = 0;
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

			phydm_api_debug(p_dm_odm, PHYDM_API_NBI, (u32 *)var1, &used, output, &out_len);
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

			phydm_api_debug(p_dm_odm, PHYDM_API_CSI_MASK, (u32 *)var1, &used, output, &out_len);
			/**/
		}


		break;

	case PHYDM_DFS:
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
		{
			u32 var[6] = {0};

			for (i = 0; i < 6; i++) {
				if (input[i + 1]) {
					PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var[i]);
					input_idx++;
				}
			}

			if (input_idx >= 1)
				phydm_dfs_debug(p_dm_odm, var, &used, output, &out_len);
		}
#endif
		break;

	case PHYDM_IQK:
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
		phy_iq_calibrate(p_dm_odm->priv);
		PHYDM_SNPRINTF((output + used, out_len - used, "IQK !!\n"));
#elif (DM_ODM_SUPPORT_TYPE & ODM_WIN)
		PHY_IQCalibrate(p_dm_odm->adapter, false);
		PHYDM_SNPRINTF((output + used, out_len - used, "IQK !!\n"));
#endif
		break;

	case PHYDM_NHM:
	{
		u8		target_rssi;
		u32		value32;
		u16		nhm_period = 0xC350;	/* 200ms */
		u8		IGI;
		struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (input_num == 1) {

			ccx_info->echo_NHM_en = false;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger NHM: echo nhm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Exclude CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger NHM: echo nhm 2\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r (Include CCA)\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get NHM results: echo nhm 3\n"));

			return;
		}

		/* NMH trigger */
		if ((var1[0] <= 2) && (var1[0] != 0)) {

			ccx_info->echo_NHM_en = true;
			ccx_info->echo_IGI = (u8)odm_get_bb_reg(p_dm_odm, 0xC50, MASKBYTE0);

			target_rssi = ccx_info->echo_IGI - 10;

			ccx_info->NHM_th[0] = (target_rssi - 15 + 10) * 2;

			for (i = 1; i <= 10; i++)
				ccx_info->NHM_th[i] = ccx_info->NHM_th[0] + 6 * i;

			/* 4 1. store previous NHM setting */
			phydm_nhm_setting(p_dm_odm, STORE_NHM_SETTING);

			/* 4 2. Set NHM period, 0x990[31:16]=0xC350, Time duration for NHM unit: 4us, 0xC350=200ms */
			ccx_info->NHM_period = nhm_period;

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Monitor NHM for %d us", nhm_period * 4));

			/* 4 3. Set NHM inexclude_txon, inexclude_cca, ccx_en */


			ccx_info->nhm_inexclude_cca = (var1[0] == 1) ? NHM_EXCLUDE_CCA : NHM_INCLUDE_CCA;
			ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;

			phydm_nhm_setting(p_dm_odm, SET_NHM_SETTING);

			for (i = 0; i <= 10; i++) {

				if (i == 5)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x, echo_IGI = 0x%x", i, ccx_info->NHM_th[i], ccx_info->echo_IGI));
				else if (i == 10)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x\n", i, ccx_info->NHM_th[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n NHM_th[%d] = 0x%x", i, ccx_info->NHM_th[i]));
			}

			/* 4 4. Trigger NHM */
			phydm_nhm_trigger(p_dm_odm);

		}

		/*Get NHM results*/
		else if (var1[0] == 3) {

			IGI = (u8)odm_get_bb_reg(p_dm_odm, 0xC50, MASKBYTE0);

			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Cur_IGI = 0x%x", IGI));

			phydm_get_nhm_result(p_dm_odm);

			/* 4 Resotre NHM setting */
			phydm_nhm_setting(p_dm_odm, RESTORE_NHM_SETTING);

			for (i = 0; i <= 11; i++) {

				if (i == 5)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d, echo_IGI = 0x%x", i, ccx_info->NHM_result[i], ccx_info->echo_IGI));
				else if (i == 11)
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d\n", i, ccx_info->NHM_result[i]));
				else
					PHYDM_SNPRINTF((output + used, out_len - used, "\r\n nhm_result[%d] = %d", i, ccx_info->NHM_result[i]));
			}

			ccx_info->echo_NHM_en = false;
		} else {

			ccx_info->echo_NHM_en = false;
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
	{
		struct _CCX_INFO	*ccx_info = &p_dm_odm->dm_ccx_info;
		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		/* PHYDM_SNPRINTF((output + used, out_len - used, "\r\n input_num = %d\n", input_num)); */

		if (input_num == 1) {

			ccx_info->echo_CLM_en = false;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Trigger CLM: echo clm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get CLM results: echo clm 2\n"));
			return;
		}

		/* Set & trigger CLM */
		if (var1[0] == 1) {

			ccx_info->echo_CLM_en = true;
			ccx_info->CLM_period = 0xC350;		/*100ms*/
			phydm_clm_setting(p_dm_odm);
			phydm_clm_trigger(p_dm_odm);
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n Monitor CLM for 200ms\n"));
		}

		/* Get CLM results */
		else if (var1[0] == 2) {

			ccx_info->echo_CLM_en = false;
			phydm_get_cl_mresult(p_dm_odm);
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n CLM_result = %d us\n", ccx_info->CLM_result * 4));

		} else {

			ccx_info->echo_CLM_en = false;
			PHYDM_SNPRINTF((output + used, out_len - used, "\n\r Error command !\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Trigger CLM: echo clm 1\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "\r Get CLM results: echo clm 2\n"));
		}
	}
	break;

	case PHYDM_BB_INFO:
	{
		s32 value32 = 0;

		phydm_bb_debug_info(p_dm_odm, &used, output, &out_len);

		if (p_dm_odm->support_ic_type & ODM_RTL8822B && input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			odm_set_bb_reg(p_dm_odm, 0x1988, 0x003fff00, var1[0]);
			value32 = odm_get_bb_reg(p_dm_odm, 0xf84, MASKDWORD);
			value32 = (value32 & 0xff000000) >> 24;
			PHYDM_SNPRINTF((output + used, out_len - used, "\r\n %-35s = condition num = %d, subcarriers = %d\n", "Over condition num subcarrier", var1[0], value32));
			odm_set_bb_reg(p_dm_odm, 0x1988, BIT(22), 0x0);	/*disable report condition number*/
		}
	}
	break;

	case PHYDM_TXBF:
	{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
#if (BEAMFORMING_SUPPORT == 1)
		struct _RT_BEAMFORMING_INFO	*p_beamforming_info = &p_dm_odm->beamforming_info;

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

	case PHYDM_PAUSE_DIG_EN:


		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			if (var1[0] == 0) {
				odm_pause_dig(p_dm_odm, PHYDM_PAUSE, PHYDM_PAUSE_LEVEL_7, (u8)var1[1]);
				PHYDM_SNPRINTF((output + used, out_len - used, "Set IGI_value = ((%x))\n", var1[1]));
			} else if (var1[0] == 1) {
				odm_pause_dig(p_dm_odm, PHYDM_RESUME, PHYDM_PAUSE_LEVEL_7, (u8)var1[1]);
				PHYDM_SNPRINTF((output + used, out_len - used, "Resume IGI_value\n"));
			} else
				PHYDM_SNPRINTF((output + used, out_len - used, "echo  (1:pause, 2resume)  (IGI_value)\n"));
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
			phydm_h2C_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);


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
			phydm_set_ext_switch(p_dm_odm, (u32 *)var1, &used, output, &out_len);
#else
			PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
#endif
		}


		break;

	case PHYDM_DYNAMIC_RA_PATH:

#if (CONFIG_DYNAMIC_RX_PATH == 1)
		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_drp_debug(p_dm_odm, (u32 *)var1, &used, output, &out_len);

#else
		PHYDM_SNPRINTF((output + used, out_len - used, "Not Support IC"));
#endif

		break;

	case PHYDM_PSD:

		#if (CONFIG_PSD_TOOL== 1)
		phydm_psd_debug(p_dm_odm, &input[0], &used, output, &out_len, input_num);
		#endif

		break;
		
	case PHYDM_DEBUG_PORT:
		{
			u32	dbg_port_value;

			PHYDM_SSCANF(input[1], DCMD_HEX, &var1[0]);
			
			if (phydm_set_bb_dbg_port(p_dm_odm, BB_DBGPORT_PRIORITY_3, var1[0])) {/*set debug port to 0x0*/

				dbg_port_value = phydm_get_bb_dbg_port_value(p_dm_odm);
				phydm_release_bb_dbg_port(p_dm_odm);
				
				PHYDM_SNPRINTF((output + used, out_len - used, "Debug Port[0x%x] = ((0x%x))\n", var1[1], dbg_port_value));
			}
		}
		break;
		
	default:
		PHYDM_SNPRINTF((output + used, out_len - used, "SET, unknown command!\n"));
		break;

	}
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
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
	struct PHY_DM_STRUCT	*p_dm_odm,
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

	phydm_cmd_parser(p_dm_odm, argv, argc, flag, output, out_len);

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
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	/*u8	debug_trace_11byte[60];*/
	u8		freg_num, c2h_seq, buf_0 = 0;

	if (!(p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES))
		return;

	if (cmd_len > 12)
		return;

	buf_0 = cmd_buf[0];
	freg_num = (buf_0 & 0xf);
	c2h_seq = (buf_0 & 0xf0) >> 4;
	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] freg_num = (( %d )), c2h_seq = (( %d ))\n", freg_num,c2h_seq ));*/

	/*strncpy(debug_trace_11byte,&cmd_buf[1],(cmd_len-1));*/
	/*debug_trace_11byte[cmd_len-1] = '\0';*/
	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] %s\n", debug_trace_11byte));*/
	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] cmd_len = (( %d ))\n", cmd_len));*/
	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW debug message] c2h_cmd_start  = (( %d ))\n", p_dm_odm->c2h_cmd_start));*/



	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("pre_seq = (( %d )), current_seq = (( %d ))\n", p_dm_odm->pre_c2h_seq, c2h_seq));*/
	/*ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("fw_buff_is_enpty = (( %d ))\n", p_dm_odm->fw_buff_is_enpty));*/

	if ((c2h_seq != p_dm_odm->pre_c2h_seq)  &&  p_dm_odm->fw_buff_is_enpty == false) {
		p_dm_odm->fw_debug_trace[p_dm_odm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW Dbg Queue Overflow] %s\n", p_dm_odm->fw_debug_trace));
		p_dm_odm->c2h_cmd_start = 0;
	}

	if ((cmd_len - 1) > (60 - p_dm_odm->c2h_cmd_start)) {
		p_dm_odm->fw_debug_trace[p_dm_odm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW Dbg Queue error: wrong C2H length] %s\n", p_dm_odm->fw_debug_trace));
		p_dm_odm->c2h_cmd_start = 0;
		return;
	}

	strncpy((char *)&(p_dm_odm->fw_debug_trace[p_dm_odm->c2h_cmd_start]), (char *)&cmd_buf[1], (cmd_len - 1));
	p_dm_odm->c2h_cmd_start += (cmd_len - 1);
	p_dm_odm->fw_buff_is_enpty = false;

	if (freg_num == 0 || p_dm_odm->c2h_cmd_start >= 60) {
		if (p_dm_odm->c2h_cmd_start < 60)
			p_dm_odm->fw_debug_trace[p_dm_odm->c2h_cmd_start] = '\0';
		else
			p_dm_odm->fw_debug_trace[59] = '\0';

		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s\n", p_dm_odm->fw_debug_trace));
		/*dbg_print("[FW DBG Msg] %s\n", p_dm_odm->fw_debug_trace);*/
		p_dm_odm->c2h_cmd_start = 0;
		p_dm_odm->fw_buff_is_enpty = true;
	}

	p_dm_odm->pre_c2h_seq = c2h_seq;
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void
phydm_fw_trace_handler_code(
	void	*p_dm_void,
	u8	*buffer,
	u8	cmd_len
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	function = buffer[0];
	u8	dbg_num = buffer[1];
	u16	content_0 = (((u16)buffer[3]) << 8) | ((u16)buffer[2]);
	u16	content_1 = (((u16)buffer[5]) << 8) | ((u16)buffer[4]);
	u16	content_2 = (((u16)buffer[7]) << 8) | ((u16)buffer[6]);
	u16	content_3 = (((u16)buffer[9]) << 8) | ((u16)buffer[8]);
	u16	content_4 = (((u16)buffer[11]) << 8) | ((u16)buffer[10]);

	if (cmd_len > 12)
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW Msg] Invalid cmd length (( %d )) >12\n", cmd_len));

	/* ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE,ODM_DBG_LOUD,("[FW Msg] Func=((%d)),  num=((%d)), ct_0=((%d)), ct_1=((%d)), ct_2=((%d)), ct_3=((%d)), ct_4=((%d))\n", */
	/*	function, dbg_num, content_0, content_1, content_2, content_3, content_4)); */

	/*--------------------------------------------*/
#if (CONFIG_RA_FW_DBG_CODE)
	if (function == RATE_DECISION) {
		if (dbg_num == 0) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RA_CNT=((%d))  Max_device=((%d))--------------------------->\n", content_1, content_2));
			else if (content_0 == 2)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Check RA macid= ((%d)), MediaStatus=((%d)), Dis_RA=((%d)),  try_bit=((0x%x))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Check RA  total=((%d)),  drop=((0x%x)), TXRPT_TRY_bit=((%x)), bNoisy=((%x))\n", content_1, content_2, content_3, content_4));
		} else if (dbg_num == 1) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RTY[0,1,2,3]=[ %d , %d , %d , %d ]\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RTY[4]=[ %d ], drop=(( %d )), total=(( %d )), current_rate=((0x %x ))", content_1, content_2, content_3, content_4));
				phydm_print_rate(p_dm_odm, (u8)content_4, ODM_FW_DEBUG_TRACE);
			} else if (content_0 == 3)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] penality_idx=(( %d ))\n", content_1));
			else if (content_0 == 4)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RSSI=(( %d )), ra_stage = (( %d ))\n", content_1, content_2));
		}

		else if (dbg_num == 3) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( DOWN ))  total=((%d)),  total>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP ))  total_acc=((%d)),  total_acc>>1=((%d)), R4+R3+R2 = ((%d)), RateDownHold = ((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 3)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP )) ((rate Down Hold))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 4)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( UP )) ((tota_accl<5 skip))  RA_CNT=((%d))\n", content_1));
			else if (content_0 == 8)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] Fast_RA (( Reset Tx Rpt )) RA_CNT=((%d))\n", content_1));
		}

		else if (dbg_num == 4) {
			if (content_0 == 3) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] RER_CNT   PCR_ori =(( %d )),  ratio_ori =(( %d )), pcr_updown_bitmap =(( 0x%x )), pcr_var_diff =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			} else if (content_0 == 4) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] pcr_shift_value =(( %s%d )), rate_down_threshold =(( %d )), rate_up_threshold =(( %d ))\n", ((content_1) ? "+" : "-"), content_2, content_3, content_4));
				/**/
			} else if (content_0 == 5) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] pcr_mean =(( %d )), PCR_VAR =(( %d )), offset =(( %d )), decision_offset_p =(( %d ))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}

		else if (dbg_num == 5) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] (( UP))  Nsc=(( %d )), N_High=(( %d )), RateUp_Waiting=(( %d )), RateUp_Fail=(( %d ))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 2)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((DOWN))  Nsc=(( %d )), N_Low=(( %d ))\n", content_1, content_2));
			else if (content_0 == 3)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((HOLD))  Nsc=((%d)), N_High=((%d)), N_Low=((%d)), Reset_CNT=((%d))\n", content_1, content_2, content_3, content_4));
		} else if (dbg_num == 0x60) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  macid=((%d)), BUPDATE[macid]=((%d))\n", content_1, content_2));
			else if (content_0 == 4)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  pass=((%d)), rty_num=((%d)), drop=((%d)), total=((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 5)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW] ((AP RPT))  PASS=((%d)), RTY_NUM=((%d)), DROP=((%d)), TOTAL=((%d))\n", content_1, content_2, content_3, content_4));
		}
	}
	/*--------------------------------------------*/
	else if (function == INIT_RA_TABLE) {
		if (dbg_num == 3)
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][INIT_RA_INFO] Ra_init, RA_SKIP_CNT = (( %d ))\n", content_0));

	}
	/*--------------------------------------------*/
	else if (function == RATE_UP) {
		if (dbg_num == 2) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((Highest rate->return)), macid=((%d))  Nsc=((%d))\n", content_1, content_2));
		} else if (dbg_num == 5) {
			if (content_0 == 0)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((rate UP)), up_rate_tmp=((0x%x)), rate_idx=((0x%x)), SGI_en=((%d)),  SGI=((%d))\n", content_1, content_2, content_3, content_4));
			else if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateUp]  ((rate UP)), rate_1=((0x%x)), rate_2=((0x%x)), BW=((%d)), Try_Bit=((%d))\n", content_1, content_2, content_3, content_4));
		}

	}
	/*--------------------------------------------*/
	else if (function == RATE_DOWN) {
		if (dbg_num == 5) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][RateDownStep]  ((rate Down)), macid=((%d)), rate1=((0x%x)),  rate2=((0x%x)), BW=((%d))\n", content_1, content_2, content_3, content_4));
		}
	} else if (function == TRY_DONE) {
		if (dbg_num == 1) {
			if (content_0 == 1) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try succsess )) macid=((%d)), Try_Done_cnt=((%d))\n", content_1, content_2));
				/**/
			}
		} else if (dbg_num == 2) {
			if (content_0 == 1) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][Try Done]  ((try)) macid=((%d)), Try_Done_cnt=((%d)),  rate_2=((%d)),  try_succes=((%d))\n", content_1, content_2, content_3, content_4));
				/**/
			}
		}
	}
	/*--------------------------------------------*/
	else if (function == RA_H2C) {
		if (dbg_num == 1) {
			if (content_0 == 0) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x49]  fw_trace_en=((%d)), mode =((%d)),  macid=((%d))\n", content_1, content_2, content_3));
				/**/
				/*C2H_RA_Dbg_code(F_RA_H2C,1,0, SysMib.ODM.DEBUG.fw_trace_en, mode, macid, 0);    //RA MASK*/
			}
#if 0
			else if (dbg_num == 2) {

				if (content_0 == 1) {
					ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]  MACID=((%d)), rate ID=((%d)),  SGI=((%d)),  BW=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 2) {
					ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]   VHT_en=((%d)), Disable_PowerTraining=((%d)),  Disable_RA=((%d)),  No_Update=((%d))\n", content_1, content_2, content_3, content_4));
					/**/
				} else if (content_0 == 3) {
					ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][H2C=0x40]   RA_MSK=[%x | %x | %x | %x ]\n", content_1, content_2, content_3, content_4));
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
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  ((1)), SPE_STATIS=((0x%x))---------->\n", content_3));
		} else if (dbg_num == 2) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  RTY_all=((%d))\n", content_1));
		} else if (dbg_num == 3) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID1[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 4) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID2[%d], TOTAL=((%d)),  RTY=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 5) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID1[%d], PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
		} else if (dbg_num == 6) {
			if (content_0 == 1)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][AP RPT]  MACID2[%d],, PASS=((%d)),  DROP=((%d))\n", content_3, content_1, content_2));
		}
	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
		/**/
	}
#else
	ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function, dbg_num, content_0, content_1, content_2, content_3, content_4));
#endif
	/*--------------------------------------------*/

#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}

void
phydm_fw_trace_handler_8051(
	void	*p_dm_void,
	u8	*buffer,
	u8	cmd_len
)
{
#if CONFIG_PHYDM_DEBUG_FUNCTION
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
#if 0
	if (cmd_len >= 3)
		cmd_buf[cmd_len - 1] = '\0';
	ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s\n", &(cmd_buf[3])));
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
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			break;
		} else if (extend_c2h_dbg_content[i] == '\n') {
			fw_debug_trace[i + 1] = '\0';
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("[FW DBG Msg] %s", &(fw_debug_trace[0])));
			buffer = extend_c2h_dbg_content + i + 3;
			goto go_backfor_aggre_dbg_pkt;
		}
	}


#endif
#endif /*#if CONFIG_PHYDM_DEBUG_FUNCTION*/
}
