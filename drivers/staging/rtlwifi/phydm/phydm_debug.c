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
#include <linux/kernel.h>

bool phydm_api_set_txagc(struct phy_dm_struct *, u32, enum odm_rf_radio_path,
			 u8, bool);
static inline void phydm_check_dmval_txagc(struct phy_dm_struct *dm, u32 used,
					   u32 out_len, u32 *const dm_value,
					   char *output)
{
	if ((u8)dm_value[2] != 0xff) {
		if (phydm_api_set_txagc(dm, dm_value[3],
					(enum odm_rf_radio_path)dm_value[1],
					(u8)dm_value[2], true))
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %s%d   %s%x%s%x\n", "Write path-",
				       dm_value[1], "rate index-0x",
				       dm_value[2], " = 0x", dm_value[3]);
		else
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %s%d   %s%x%s\n", "Write path-",
				       (dm_value[1] & 0x1), "rate index-0x",
				       (dm_value[2] & 0x7f), " fail");
	} else {
		u8 i;
		u32 power_index;
		bool status = true;

		power_index = (dm_value[3] & 0x3f);

		if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
			power_index = (power_index << 24) |
				      (power_index << 16) | (power_index << 8) |
				      (power_index);
			for (i = 0; i < ODM_RATEVHTSS2MCS9; i += 4)
				status = (status &
					  phydm_api_set_txagc(
						  dm, power_index,
						  (enum odm_rf_radio_path)
							  dm_value[1],
						  i, false));
		} else if (dm->support_ic_type & ODM_RTL8197F) {
			for (i = 0; i <= ODM_RATEMCS15; i++)
				status = (status &
					  phydm_api_set_txagc(
						  dm, power_index,
						  (enum odm_rf_radio_path)
							  dm_value[1],
						  i, false));
		}

		if (status)
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %s%d   %s%x\n",
				       "Write all TXAGC of path-", dm_value[1],
				       " = 0x", dm_value[3]);
		else
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %s%d   %s\n",
				       "Write all TXAGC of path-", dm_value[1],
				       " fail");
	}
}

static inline void phydm_print_nhm_trigger(char *output, u32 used, u32 out_len,
					   struct ccx_info *ccx_info)
{
	int i;

	for (i = 0; i <= 10; i++) {
		if (i == 5)
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"\r\n NHM_th[%d] = 0x%x, echo_IGI = 0x%x", i,
				ccx_info->NHM_th[i], ccx_info->echo_IGI);
		else if (i == 10)
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n NHM_th[%d] = 0x%x\n", i,
				       ccx_info->NHM_th[i]);
		else
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n NHM_th[%d] = 0x%x", i,
				       ccx_info->NHM_th[i]);
	}
}

static inline void phydm_print_nhm_result(char *output, u32 used, u32 out_len,
					  struct ccx_info *ccx_info)
{
	int i;

	for (i = 0; i <= 11; i++) {
		if (i == 5)
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"\r\n nhm_result[%d] = %d, echo_IGI = 0x%x", i,
				ccx_info->NHM_result[i], ccx_info->echo_IGI);
		else if (i == 11)
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n nhm_result[%d] = %d\n", i,
				       ccx_info->NHM_result[i]);
		else
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n nhm_result[%d] = %d", i,
				       ccx_info->NHM_result[i]);
	}
}

static inline void phydm_print_csi(struct phy_dm_struct *dm, u32 used,
				   u32 out_len, char *output)
{
	int index, ptr;
	u32 dword_h, dword_l;

	for (index = 0; index < 80; index++) {
		ptr = index + 256;

		if (ptr > 311)
			ptr -= 312;

		odm_set_bb_reg(dm, 0x1910, 0x03FF0000, ptr); /*Select Address*/
		dword_h = odm_get_bb_reg(dm, 0xF74, MASKDWORD);
		dword_l = odm_get_bb_reg(dm, 0xF5C, MASKDWORD);

		if (index % 2 == 0)
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
				dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8,
				(dword_l & MASKBYTE2) >> 16,
				(dword_l & MASKBYTE3) >> 24,
				dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8,
				(dword_h & MASKBYTE2) >> 16,
				(dword_h & MASKBYTE3) >> 24);
		else
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"%02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n",
				dword_l & MASKBYTE0, (dword_l & MASKBYTE1) >> 8,
				(dword_l & MASKBYTE2) >> 16,
				(dword_l & MASKBYTE3) >> 24,
				dword_h & MASKBYTE0, (dword_h & MASKBYTE1) >> 8,
				(dword_h & MASKBYTE2) >> 16,
				(dword_h & MASKBYTE3) >> 24);
	}
}

void phydm_init_debug_setting(struct phy_dm_struct *dm)
{
	dm->debug_level = ODM_DBG_TRACE;

	dm->fw_debug_components = 0;
	dm->debug_components =

		0;

	dm->fw_buff_is_enpty = true;
	dm->pre_c2h_seq = 0;
}

u8 phydm_set_bb_dbg_port(void *dm_void, u8 curr_dbg_priority, u32 debug_port)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 dbg_port_result = false;

	if (curr_dbg_priority > dm->pre_dbg_priority) {
		if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			odm_set_bb_reg(dm, 0x8fc, MASKDWORD, debug_port);
			/**/
		} else /*if (dm->support_ic_type & ODM_IC_11N_SERIES)*/ {
			odm_set_bb_reg(dm, 0x908, MASKDWORD, debug_port);
			/**/
		}
		ODM_RT_TRACE(
			dm, ODM_COMP_API,
			"DbgPort set success, Reg((0x%x)), Cur_priority=((%d)), Pre_priority=((%d))\n",
			debug_port, curr_dbg_priority, dm->pre_dbg_priority);
		dm->pre_dbg_priority = curr_dbg_priority;
		dbg_port_result = true;
	}

	return dbg_port_result;
}

void phydm_release_bb_dbg_port(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	dm->pre_dbg_priority = BB_DBGPORT_RELEASE;
	ODM_RT_TRACE(dm, ODM_COMP_API, "Release BB dbg_port\n");
}

u32 phydm_get_bb_dbg_port_value(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 dbg_port_value = 0;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		dbg_port_value = odm_get_bb_reg(dm, 0xfa0, MASKDWORD);
		/**/
	} else /*if (dm->support_ic_type & ODM_IC_11N_SERIES)*/ {
		dbg_port_value = odm_get_bb_reg(dm, 0xdf4, MASKDWORD);
		/**/
	}
	ODM_RT_TRACE(dm, ODM_COMP_API, "dbg_port_value = 0x%x\n",
		     dbg_port_value);
	return dbg_port_value;
}

static void phydm_bb_rx_hang_info(void *dm_void, u32 *_used, char *output,
				  u32 *_out_len)
{
	u32 value32 = 0;
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		return;

	value32 = odm_get_bb_reg(dm, 0xF80, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = 0x%x",
		       "rptreg of sc/bw/ht/...", value32);

	if (dm->support_ic_type & ODM_RTL8822B)
		odm_set_bb_reg(dm, 0x198c, BIT(2) | BIT(1) | BIT(0), 7);

	/* dbg_port = basic state machine */
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x000);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "basic state machine",
			       value32);
	}

	/* dbg_port = state machine */
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x007);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "state machine", value32);
	}

	/* dbg_port = CCA-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x204);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "CCA-related", value32);
	}

	/* dbg_port = edcca/rxd*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x278);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "edcca/rxd", value32);
	}

	/* dbg_port = rx_state/mux_state/ADC_MASK_OFDM*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x290);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x",
			       "rx_state/mux_state/ADC_MASK_OFDM", value32);
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B2);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "bf-related", value32);
	}

	/* dbg_port = bf-related*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0x2B8);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "bf-related", value32);
	}

	/* dbg_port = txon/rxd*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA03);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "txon/rxd", value32);
	}

	/* dbg_port = l_rate/l_length*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0B);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "l_rate/l_length", value32);
	}

	/* dbg_port = rxd/rxd_hit*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xA0D);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "rxd/rxd_hit", value32);
	}

	/* dbg_port = dis_cca*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAA0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "dis_cca", value32);
	}

	/* dbg_port = tx*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAB0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "tx", value32);
	}

	/* dbg_port = rx plcp*/
	{
		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD0);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD1);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD2);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "rx plcp", value32);

		odm_set_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD, 0xAD3);
		value32 = odm_get_bb_reg(dm, ODM_REG_DBG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "0x8fc", value32);

		value32 = odm_get_bb_reg(dm, ODM_REG_RPT_11AC, MASKDWORD);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = 0x%x", "rx plcp", value32);
	}
}

static void phydm_bb_debug_info_n_series(void *dm_void, u32 *_used,
					 char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	u32 value32 = 0, value32_1 = 0;
	u8 rf_gain_a = 0, rf_gain_b = 0, rf_gain_c = 0, rf_gain_d = 0;
	u8 rx_snr_a = 0, rx_snr_b = 0, rx_snr_c = 0, rx_snr_d = 0;

	s8 rxevm_0 = 0, rxevm_1 = 0;
	s32 short_cfo_a = 0, short_cfo_b = 0, long_cfo_a = 0, long_cfo_b = 0;
	s32 scfo_a = 0, scfo_b = 0, avg_cfo_a = 0, avg_cfo_b = 0;
	s32 cfo_end_a = 0, cfo_end_b = 0, acq_cfo_a = 0, acq_cfo_b = 0;

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s\n",
		       "BB Report Info");

	/*AGC result*/
	value32 = odm_get_bb_reg(dm, 0xdd0, MASKDWORD);
	rf_gain_a = (u8)(value32 & 0x3f);
	rf_gain_a = rf_gain_a << 1;

	rf_gain_b = (u8)((value32 >> 8) & 0x3f);
	rf_gain_b = rf_gain_b << 1;

	rf_gain_c = (u8)((value32 >> 16) & 0x3f);
	rf_gain_c = rf_gain_c << 1;

	rf_gain_d = (u8)((value32 >> 24) & 0x3f);
	rf_gain_d = rf_gain_d << 1;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d / %d",
		       "OFDM RX RF Gain(A/B/C/D)", rf_gain_a, rf_gain_b,
		       rf_gain_c, rf_gain_d);

	/*SNR report*/
	value32 = odm_get_bb_reg(dm, 0xdd4, MASKDWORD);
	rx_snr_a = (u8)(value32 & 0xff);
	rx_snr_a = rx_snr_a >> 1;

	rx_snr_b = (u8)((value32 >> 8) & 0xff);
	rx_snr_b = rx_snr_b >> 1;

	rx_snr_c = (u8)((value32 >> 16) & 0xff);
	rx_snr_c = rx_snr_c >> 1;

	rx_snr_d = (u8)((value32 >> 24) & 0xff);
	rx_snr_d = rx_snr_d >> 1;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)",
		       rx_snr_a, rx_snr_b, rx_snr_c, rx_snr_d);

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(dm, 0xdd8, MASKDWORD);

	rxevm_0 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_0 /= 2;
	if (rxevm_0 < -63)
		rxevm_0 = 0;

	rxevm_1 = (s8)((value32 & MASKBYTE3) >> 24);
	rxevm_1 /= 2;
	if (rxevm_1 < -63)
		rxevm_1 = 0;

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "RXEVM (1ss/2ss)", rxevm_0, rxevm_1);

	/*CFO Report Info*/
	odm_set_bb_reg(dm, 0xd00, BIT(26), 1);

	/*Short CFO*/
	value32 = odm_get_bb_reg(dm, 0xdac, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xdb0, MASKDWORD);

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

	/*LFO 2's to dec*/

	if (long_cfo_a > 4095)
		long_cfo_a = long_cfo_a - 8192;

	if (long_cfo_b > 4095)
		long_cfo_b = long_cfo_b - 8192;

	long_cfo_a = long_cfo_a * 312500 / 4096;
	long_cfo_b = long_cfo_b * 312500 / 4096;

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s",
		       "CFO Report Info");
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "Short CFO(Hz) <A/B>", short_cfo_a, short_cfo_b);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "Long CFO(Hz) <A/B>", long_cfo_a, long_cfo_b);

	/*SCFO*/
	value32 = odm_get_bb_reg(dm, 0xdb8, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xdb4, MASKDWORD);

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

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "value SCFO(Hz) <A/B>", scfo_a, scfo_b);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "Avg CFO(Hz) <A/B>", avg_cfo_a, avg_cfo_b);

	value32 = odm_get_bb_reg(dm, 0xdbc, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xde0, MASKDWORD);

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

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "End CFO(Hz) <A/B>", cfo_end_a, cfo_end_b);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "ACQ CFO(Hz) <A/B>", acq_cfo_a, acq_cfo_b);
}

static void phydm_bb_debug_info(void *dm_void, u32 *_used, char *output,
				u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	char *tmp_string = NULL;

	u8 rx_ht_bw, rx_vht_bw, rxsc, rx_ht, rx_bw;
	static u8 v_rx_bw;
	u32 value32, value32_1, value32_2, value32_3;
	s32 sfo_a, sfo_b, sfo_c, sfo_d;
	s32 lfo_a, lfo_b, lfo_c, lfo_d;
	static u8 MCSS, tail, parity, rsv, vrsv, idx, smooth, htsound, agg,
		stbc, vstbc, fec, fecext, sgi, sgiext, htltf, vgid, v_nsts,
		vtxops, vrsv2, vbrsv, bf, vbcrc;
	static u16 h_length, htcrc8, length;
	static u16 vpaid;
	static u16 v_length, vhtcrc8, v_mcss, v_tail, vb_tail;
	static u8 hmcss, hrx_bw;

	u8 pwdb;
	s8 rxevm_0, rxevm_1, rxevm_2;
	u8 rf_gain_path_a, rf_gain_path_b, rf_gain_path_c, rf_gain_path_d;
	u8 rx_snr_path_a, rx_snr_path_b, rx_snr_path_c, rx_snr_path_d;
	s32 sig_power;

	const char *L_rate[8] = {"6M",  "9M",  "12M", "18M",
				 "24M", "36M", "48M", "54M"};

	if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		phydm_bb_debug_info_n_series(dm, &used, output, &out_len);
		return;
	}

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s\n",
		       "BB Report Info");

	/*BW & mode Detection*/

	value32 = odm_get_bb_reg(dm, 0xf80, MASKDWORD);
	value32_2 = value32;
	rx_ht_bw = (u8)(value32 & 0x1);
	rx_vht_bw = (u8)((value32 >> 1) & 0x3);
	rxsc = (u8)(value32 & 0x78);
	value32_1 = (value32 & 0x180) >> 7;
	rx_ht = (u8)(value32_1);

	rx_bw = 0;

	if (rx_ht == 2) {
		if (rx_vht_bw == 0)
			tmp_string = "20M";
		else if (rx_vht_bw == 1)
			tmp_string = "40M";
		else
			tmp_string = "80M";
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s %s %s", "mode", "VHT", tmp_string);
		rx_bw = rx_vht_bw;
	} else if (rx_ht == 1) {
		if (rx_ht_bw == 0)
			tmp_string = "20M";
		else if (rx_ht_bw == 1)
			tmp_string = "40M";
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s %s %s", "mode", "HT", tmp_string);
		rx_bw = rx_ht_bw;
	} else {
		PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s %s",
			       "mode", "Legacy");
	}
	if (rx_ht != 0) {
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
		PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s",
			       tmp_string);
	}

	/* RX signal power and AGC related info*/

	value32 = odm_get_bb_reg(dm, 0xF90, MASKDWORD);
	pwdb = (u8)((value32 & MASKBYTE1) >> 8);
	pwdb = pwdb >> 1;
	sig_power = -110 + pwdb;
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d",
		       "OFDM RX Signal Power(dB)", sig_power);

	value32 = odm_get_bb_reg(dm, 0xd14, MASKDWORD);
	rx_snr_path_a = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_a = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_a *= 2;
	value32 = odm_get_bb_reg(dm, 0xd54, MASKDWORD);
	rx_snr_path_b = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_b = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_b *= 2;
	value32 = odm_get_bb_reg(dm, 0xd94, MASKDWORD);
	rx_snr_path_c = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_c = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_c *= 2;
	value32 = odm_get_bb_reg(dm, 0xdd4, MASKDWORD);
	rx_snr_path_d = (u8)(value32 & 0xFF) >> 1;
	rf_gain_path_d = (s8)((value32 & MASKBYTE1) >> 8);
	rf_gain_path_d *= 2;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d / %d",
		       "OFDM RX RF Gain(A/B/C/D)", rf_gain_path_a,
		       rf_gain_path_b, rf_gain_path_c, rf_gain_path_d);

	/* RX counter related info*/

	value32 = odm_get_bb_reg(dm, 0xF08, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d",
		       "OFDM CCA counter", ((value32 & 0xFFFF0000) >> 16));

	value32 = odm_get_bb_reg(dm, 0xFD0, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d",
		       "OFDM SBD Fail counter", value32 & 0xFFFF);

	value32 = odm_get_bb_reg(dm, 0xFC4, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "VHT SIGA/SIGB CRC8 Fail counter", value32 & 0xFFFF,
		       ((value32 & 0xFFFF0000) >> 16));

	value32 = odm_get_bb_reg(dm, 0xFCC, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d",
		       "CCK CCA counter", value32 & 0xFFFF);

	value32 = odm_get_bb_reg(dm, 0xFBC, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "LSIG (parity Fail/rate Illegal) counter",
		       value32 & 0xFFFF, ((value32 & 0xFFFF0000) >> 16));

	value32_1 = odm_get_bb_reg(dm, 0xFC8, MASKDWORD);
	value32_2 = odm_get_bb_reg(dm, 0xFC0, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "HT/VHT MCS NOT SUPPORT counter",
		       ((value32_2 & 0xFFFF0000) >> 16), value32_1 & 0xFFFF);

	/* PostFFT related info*/
	value32 = odm_get_bb_reg(dm, 0xF8c, MASKDWORD);
	rxevm_0 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_0 /= 2;
	if (rxevm_0 < -63)
		rxevm_0 = 0;

	rxevm_1 = (s8)((value32 & MASKBYTE3) >> 24);
	rxevm_1 /= 2;
	value32 = odm_get_bb_reg(dm, 0xF88, MASKDWORD);
	rxevm_2 = (s8)((value32 & MASKBYTE2) >> 16);
	rxevm_2 /= 2;

	if (rxevm_1 < -63)
		rxevm_1 = 0;
	if (rxevm_2 < -63)
		rxevm_2 = 0;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d", "RXEVM (1ss/2ss/3ss)",
		       rxevm_0, rxevm_1, rxevm_2);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d / %d", "RXSNR(A/B/C/D, dB)",
		       rx_snr_path_a, rx_snr_path_b, rx_snr_path_c,
		       rx_snr_path_d);

	value32 = odm_get_bb_reg(dm, 0xF8C, MASKDWORD);
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s = %d / %d",
		       "CSI_1st /CSI_2nd", value32 & 0xFFFF,
		       ((value32 & 0xFFFF0000) >> 16));

	/*BW & mode Detection*/

	/*Reset Page F counter*/
	odm_set_bb_reg(dm, 0xB58, BIT(0), 1);
	odm_set_bb_reg(dm, 0xB58, BIT(0), 0);

	/*CFO Report Info*/
	/*Short CFO*/
	value32 = odm_get_bb_reg(dm, 0xd0c, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xd4c, MASKDWORD);
	value32_2 = odm_get_bb_reg(dm, 0xd8c, MASKDWORD);
	value32_3 = odm_get_bb_reg(dm, 0xdcc, MASKDWORD);

	sfo_a = (s32)(value32 & 0xfff);
	sfo_b = (s32)(value32_1 & 0xfff);
	sfo_c = (s32)(value32_2 & 0xfff);
	sfo_d = (s32)(value32_3 & 0xfff);

	lfo_a = (s32)(value32 >> 16);
	lfo_b = (s32)(value32_1 >> 16);
	lfo_c = (s32)(value32_2 >> 16);
	lfo_d = (s32)(value32_3 >> 16);

	/*SFO 2's to dec*/
	if (sfo_a > 2047)
		sfo_a = sfo_a - 4096;
	sfo_a = (sfo_a * 312500) / 2048;
	if (sfo_b > 2047)
		sfo_b = sfo_b - 4096;
	sfo_b = (sfo_b * 312500) / 2048;
	if (sfo_c > 2047)
		sfo_c = sfo_c - 4096;
	sfo_c = (sfo_c * 312500) / 2048;
	if (sfo_d > 2047)
		sfo_d = sfo_d - 4096;
	sfo_d = (sfo_d * 312500) / 2048;

	/*LFO 2's to dec*/

	if (lfo_a > 4095)
		lfo_a = lfo_a - 8192;

	if (lfo_b > 4095)
		lfo_b = lfo_b - 8192;

	if (lfo_c > 4095)
		lfo_c = lfo_c - 8192;

	if (lfo_d > 4095)
		lfo_d = lfo_d - 8192;
	lfo_a = lfo_a * 312500 / 4096;
	lfo_b = lfo_b * 312500 / 4096;
	lfo_c = lfo_c * 312500 / 4096;
	lfo_d = lfo_d * 312500 / 4096;
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s",
		       "CFO Report Info");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d /%d",
		       "Short CFO(Hz) <A/B/C/D>", sfo_a, sfo_b, sfo_c, sfo_d);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d /%d",
		       "Long CFO(Hz) <A/B/C/D>", lfo_a, lfo_b, lfo_c, lfo_d);

	/*SCFO*/
	value32 = odm_get_bb_reg(dm, 0xd10, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xd50, MASKDWORD);
	value32_2 = odm_get_bb_reg(dm, 0xd90, MASKDWORD);
	value32_3 = odm_get_bb_reg(dm, 0xdd0, MASKDWORD);

	sfo_a = (s32)(value32 & 0x7ff);
	sfo_b = (s32)(value32_1 & 0x7ff);
	sfo_c = (s32)(value32_2 & 0x7ff);
	sfo_d = (s32)(value32_3 & 0x7ff);

	if (sfo_a > 1023)
		sfo_a = sfo_a - 2048;

	if (sfo_b > 2047)
		sfo_b = sfo_b - 4096;

	if (sfo_c > 2047)
		sfo_c = sfo_c - 4096;

	if (sfo_d > 2047)
		sfo_d = sfo_d - 4096;

	sfo_a = sfo_a * 312500 / 1024;
	sfo_b = sfo_b * 312500 / 1024;
	sfo_c = sfo_c * 312500 / 1024;
	sfo_d = sfo_d * 312500 / 1024;

	lfo_a = (s32)(value32 >> 16);
	lfo_b = (s32)(value32_1 >> 16);
	lfo_c = (s32)(value32_2 >> 16);
	lfo_d = (s32)(value32_3 >> 16);

	if (lfo_a > 4095)
		lfo_a = lfo_a - 8192;

	if (lfo_b > 4095)
		lfo_b = lfo_b - 8192;

	if (lfo_c > 4095)
		lfo_c = lfo_c - 8192;

	if (lfo_d > 4095)
		lfo_d = lfo_d - 8192;
	lfo_a = lfo_a * 312500 / 4096;
	lfo_b = lfo_b * 312500 / 4096;
	lfo_c = lfo_c * 312500 / 4096;
	lfo_d = lfo_d * 312500 / 4096;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d /%d",
		       "value SCFO(Hz) <A/B/C/D>", sfo_a, sfo_b, sfo_c, sfo_d);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d /%d", "ACQ CFO(Hz) <A/B/C/D>",
		       lfo_a, lfo_b, lfo_c, lfo_d);

	value32 = odm_get_bb_reg(dm, 0xd14, MASKDWORD);
	value32_1 = odm_get_bb_reg(dm, 0xd54, MASKDWORD);
	value32_2 = odm_get_bb_reg(dm, 0xd94, MASKDWORD);
	value32_3 = odm_get_bb_reg(dm, 0xdd4, MASKDWORD);

	lfo_a = (s32)(value32 >> 16);
	lfo_b = (s32)(value32_1 >> 16);
	lfo_c = (s32)(value32_2 >> 16);
	lfo_d = (s32)(value32_3 >> 16);

	if (lfo_a > 4095)
		lfo_a = lfo_a - 8192;

	if (lfo_b > 4095)
		lfo_b = lfo_b - 8192;

	if (lfo_c > 4095)
		lfo_c = lfo_c - 8192;

	if (lfo_d > 4095)
		lfo_d = lfo_d - 8192;

	lfo_a = lfo_a * 312500 / 4096;
	lfo_b = lfo_b * 312500 / 4096;
	lfo_c = lfo_c * 312500 / 4096;
	lfo_d = lfo_d * 312500 / 4096;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %d / %d / %d /%d", "End CFO(Hz) <A/B/C/D>",
		       lfo_a, lfo_b, lfo_c, lfo_d);

	value32 = odm_get_bb_reg(dm, 0xf20, MASKDWORD); /*L SIG*/

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

	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s", "L-SIG");
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s : %s", "rate",
		       L_rate[idx]);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x", "Rsv/length/parity", rsv,
		       rx_bw, length);

	value32 = odm_get_bb_reg(dm, 0xf2c, MASKDWORD); /*HT SIG*/
	if (rx_ht == 1) {
		hmcss = (u8)(value32 & 0x7F);
		hrx_bw = (u8)(value32 & 0x80);
		h_length = (u16)((value32 >> 8) & 0xffff);
	}
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s", "HT-SIG1");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x", "MCS/BW/length", hmcss,
		       hrx_bw, h_length);

	value32 = odm_get_bb_reg(dm, 0xf30, MASKDWORD); /*HT SIG*/

	if (rx_ht == 1) {
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
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s", "HT-SIG2");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x / %x / %x / %x",
		       "Smooth/NoSound/Rsv/Aggregate/STBC/LDPC", smooth,
		       htsound, rsv, agg, stbc, fec);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x / %x",
		       "SGI/E-HT-LTFs/CRC/tail", sgi, htltf, htcrc8, tail);

	value32 = odm_get_bb_reg(dm, 0xf2c, MASKDWORD); /*VHT SIG A1*/
	if (rx_ht == 2) {
		/* value32 = odm_get_bb_reg(dm, 0xf2c,MASKDWORD);*/
		v_rx_bw = (u8)(value32 & 0x03);
		vrsv = (u8)(value32 & 0x04);
		vstbc = (u8)(value32 & 0x08);
		vgid = (u8)((value32 & 0x3f0) >> 4);
		v_nsts = (u8)(((value32 & 0x1c00) >> 8) + 1);
		vpaid = (u16)(value32 & 0x3fe);
		vtxops = (u8)((value32 & 0x400000) >> 20);
		vrsv2 = (u8)((value32 & 0x800000) >> 20);
	}
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s",
		       "VHT-SIG-A1");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x / %x",
		       "BW/Rsv1/STBC/GID/Nsts/PAID/TXOPPS/Rsv2", v_rx_bw, vrsv,
		       vstbc, vgid, v_nsts, vpaid, vtxops, vrsv2);

	value32 = odm_get_bb_reg(dm, 0xf30, MASKDWORD); /*VHT SIG*/

	if (rx_ht == 2) {
		/*value32 = odm_get_bb_reg(dm, 0xf30,MASKDWORD); */ /*VHT SIG*/

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
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s",
		       "VHT-SIG-A2");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x / %x / %x / %x / %x",
		       "SGI/FEC/MCS/BF/Rsv/CRC/tail", sgiext, fecext, v_mcss,
		       bf, vrsv, vhtcrc8, v_tail);

	value32 = odm_get_bb_reg(dm, 0xf34, MASKDWORD); /*VHT SIG*/
	{
		v_length = (u16)(value32 & 0x1fffff);
		vbrsv = (u8)((value32 & 0x600000) >> 20);
		vb_tail = (u16)((value32 & 0x1f800000) >> 20);
		vbcrc = (u8)((value32 & 0x80000000) >> 28);
	}
	PHYDM_SNPRINTF(output + used, out_len - used, "\r\n %-35s",
		       "VHT-SIG-B");
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "\r\n %-35s = %x / %x / %x / %x", "length/Rsv/tail/CRC",
		       v_length, vbrsv, vb_tail, vbcrc);

	/*for Condition number*/
	if (dm->support_ic_type & ODM_RTL8822B) {
		s32 condition_num = 0;
		char *factor = NULL;

		/*enable report condition number*/
		odm_set_bb_reg(dm, 0x1988, BIT(22), 0x1);

		condition_num = odm_get_bb_reg(dm, 0xf84, MASKDWORD);
		condition_num = (condition_num & 0x3ffff) >> 4;

		if (*dm->band_width == ODM_BW80M) {
			factor = "256/234";
		} else if (*dm->band_width == ODM_BW40M) {
			factor = "128/108";
		} else if (*dm->band_width == ODM_BW20M) {
			if (rx_ht == 2 || rx_ht == 1)
				factor = "64/52"; /*HT or VHT*/
			else
				factor = "64/48"; /*legacy*/
		}

		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n %-35s = %d (factor = %s)",
			       "Condition number", condition_num, factor);
	}
}

void phydm_basic_dbg_message(void *dm_void)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	struct false_alarm_stat *false_alm_cnt =
		(struct false_alarm_stat *)phydm_get_structure(
			dm, PHYDM_FALSEALMCNT);
	struct cfo_tracking *cfo_track =
		(struct cfo_tracking *)phydm_get_structure(dm, PHYDM_CFOTRACK);
	struct dig_thres *dig_tab = &dm->dm_dig_table;
	struct ra_table *ra_tab = &dm->dm_ra_table;
	u16 macid, phydm_macid, client_cnt = 0;
	struct rtl_sta_info *entry;
	s32 tmp_val = 0;
	u8 tmp_val_u1 = 0;

	ODM_RT_TRACE(dm, ODM_COMP_COMMON,
		     "[PHYDM Common MSG] System up time: ((%d sec))----->\n",
		     dm->phydm_sys_up_time);

	if (dm->is_linked) {
		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "ID=%d, BW=((%d)), CH=((%d))\n",
			     dm->curr_station_id, 20 << *dm->band_width,
			     *dm->channel);

		/*Print RX rate*/
		if (dm->rx_rate <= ODM_RATE11M)
			ODM_RT_TRACE(
				dm, ODM_COMP_COMMON,
				"[CCK AGC Report] LNA_idx = 0x%x, VGA_idx = 0x%x\n",
				dm->cck_lna_idx, dm->cck_vga_idx);
		else
			ODM_RT_TRACE(
				dm, ODM_COMP_COMMON,
				"[OFDM AGC Report] { 0x%x, 0x%x, 0x%x, 0x%x }\n",
				dm->ofdm_agc_idx[0], dm->ofdm_agc_idx[1],
				dm->ofdm_agc_idx[2], dm->ofdm_agc_idx[3]);

		ODM_RT_TRACE(dm, ODM_COMP_COMMON,
			     "RSSI: { %d,  %d,  %d,  %d },    rx_rate:",
			     (dm->rssi_a == 0xff) ? 0 : dm->rssi_a,
			     (dm->rssi_b == 0xff) ? 0 : dm->rssi_b,
			     (dm->rssi_c == 0xff) ? 0 : dm->rssi_c,
			     (dm->rssi_d == 0xff) ? 0 : dm->rssi_d);

		phydm_print_rate(dm, dm->rx_rate, ODM_COMP_COMMON);

		/*Print TX rate*/
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
			entry = dm->odm_sta_info[macid];
			if (!IS_STA_VALID(entry))
				continue;

			phydm_macid = (dm->platform2phydm_macid_table[macid]);
			ODM_RT_TRACE(dm, ODM_COMP_COMMON, "TXRate [%d]:",
				     macid);
			phydm_print_rate(dm, ra_tab->link_tx_rate[macid],
					 ODM_COMP_COMMON);

			client_cnt++;

			if (client_cnt == dm->number_linked_client)
				break;
		}

		ODM_RT_TRACE(
			dm, ODM_COMP_COMMON,
			"TP { TX, RX, total} = {%d, %d, %d }Mbps, traffic_load = (%d))\n",
			dm->tx_tp, dm->rx_tp, dm->total_tp, dm->traffic_load);

		tmp_val_u1 =
			(cfo_track->crystal_cap > cfo_track->def_x_cap) ?
				(cfo_track->crystal_cap -
				 cfo_track->def_x_cap) :
				(cfo_track->def_x_cap - cfo_track->crystal_cap);
		ODM_RT_TRACE(
			dm, ODM_COMP_COMMON,
			"CFO_avg = ((%d kHz)) , CrystalCap_tracking = ((%s%d))\n",
			cfo_track->CFO_ave_pre,
			((cfo_track->crystal_cap > cfo_track->def_x_cap) ? "+" :
									   "-"),
			tmp_val_u1);

		/* Condition number */
		if (dm->support_ic_type == ODM_RTL8822B) {
			tmp_val = phydm_get_condition_number_8822B(dm);
			ODM_RT_TRACE(dm, ODM_COMP_COMMON,
				     "Condition number = ((%d))\n", tmp_val);
		}

		/*STBC or LDPC pkt*/
		ODM_RT_TRACE(dm, ODM_COMP_COMMON, "LDPC = %s, STBC = %s\n",
			     (dm->phy_dbg_info.is_ldpc_pkt) ? "Y" : "N",
			     (dm->phy_dbg_info.is_stbc_pkt) ? "Y" : "N");
	} else {
		ODM_RT_TRACE(dm, ODM_COMP_COMMON, "No Link !!!\n");
	}

	ODM_RT_TRACE(dm, ODM_COMP_COMMON,
		     "[CCA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		     false_alm_cnt->cnt_cck_cca, false_alm_cnt->cnt_ofdm_cca,
		     false_alm_cnt->cnt_cca_all);

	ODM_RT_TRACE(dm, ODM_COMP_COMMON,
		     "[FA Cnt] {CCK, OFDM, Total} = {%d, %d, %d}\n",
		     false_alm_cnt->cnt_cck_fail, false_alm_cnt->cnt_ofdm_fail,
		     false_alm_cnt->cnt_all);

	if (dm->support_ic_type & ODM_IC_11N_SERIES)
		ODM_RT_TRACE(
			dm, ODM_COMP_COMMON,
			"[OFDM FA Detail] Parity_Fail = (( %d )), Rate_Illegal = (( %d )), CRC8_fail = (( %d )), Mcs_fail = (( %d )), Fast_Fsync = (( %d )), SB_Search_fail = (( %d ))\n",
			false_alm_cnt->cnt_parity_fail,
			false_alm_cnt->cnt_rate_illegal,
			false_alm_cnt->cnt_crc8_fail,
			false_alm_cnt->cnt_mcs_fail,
			false_alm_cnt->cnt_fast_fsync,
			false_alm_cnt->cnt_sb_search_fail);

	ODM_RT_TRACE(
		dm, ODM_COMP_COMMON,
		"is_linked = %d, Num_client = %d, rssi_min = %d, current_igi = 0x%x, bNoisy=%d\n\n",
		dm->is_linked, dm->number_linked_client, dm->rssi_min,
		dig_tab->cur_ig_value, dm->noisy_decision);
}

void phydm_basic_profile(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	char *cut = NULL;
	char *ic_type = NULL;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 date = 0;
	char *commit_by = NULL;
	u32 release_ver = 0;

	PHYDM_SNPRINTF(output + used, out_len - used, "%-35s\n",
		       "% Basic Profile %");

	if (dm->support_ic_type == ODM_RTL8188E) {
	} else if (dm->support_ic_type == ODM_RTL8822B) {
		ic_type = "RTL8822B";
		date = RELEASE_DATE_8822B;
		commit_by = COMMIT_BY_8822B;
		release_ver = RELEASE_VERSION_8822B;
	}

	/* JJ ADD 20161014 */

	PHYDM_SNPRINTF(output + used, out_len - used,
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
	else if (dm->cut_version == ODM_CUT_I)
		cut = "I";
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "cut version", cut);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %d\n",
		       "PHY Parameter version", odm_get_hw_img_version(dm));
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %d\n",
		       "PHY Parameter Commit date", date);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "PHY Parameter Commit by", commit_by);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %d\n",
		       "PHY Parameter Release version", release_ver);

	{
		struct rtl_priv *rtlpriv = (struct rtl_priv *)dm->adapter;
		struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

		PHYDM_SNPRINTF(output + used, out_len - used,
			       "  %-35s: %d (Subversion: %d)\n", "FW version",
			       rtlhal->fw_version, rtlhal->fw_subversion);
	}
	/* 1 PHY DM version List */
	PHYDM_SNPRINTF(output + used, out_len - used, "%-35s\n",
		       "% PHYDM version %");
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Code base", PHYDM_CODE_BASE);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Release Date", PHYDM_RELEASE_DATE);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "adaptivity", ADAPTIVITY_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n", "DIG",
		       DIG_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Dynamic BB PowerSaving", DYNAMIC_BBPWRSAV_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "CFO Tracking", CFO_TRACKING_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Antenna Diversity", ANTDIV_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Power Tracking", POWRTRACKING_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Dynamic TxPower", DYNAMIC_TXPWR_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "RA Info", RAINFO_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Auto channel Selection", ACS_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "EDCA Turbo", EDCATURBO_VERSION);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "LA mode", DYNAMIC_LA_MODE);
	PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
		       "Dynamic RX path", DYNAMIC_RX_PATH_VERSION);

	if (dm->support_ic_type & ODM_RTL8822B)
		PHYDM_SNPRINTF(output + used, out_len - used, "  %-35s: %s\n",
			       "PHY config 8822B", PHY_CONFIG_VERSION_8822B);

	*_used = used;
	*_out_len = out_len;
}

void phydm_fw_trace_en_h2c(void *dm_void, bool enable, u32 fw_debug_component,
			   u32 monitor_mode, u32 macid)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 h2c_parameter[7] = {0};
	u8 cmd_length;

	if (dm->support_ic_type & PHYDM_IC_3081_SERIES) {
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

	ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "---->\n");
	if (monitor_mode == 0)
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "[H2C] FW_debug_en: (( %d ))\n", enable);
	else
		ODM_RT_TRACE(
			dm, ODM_FW_DEBUG_TRACE,
			"[H2C] FW_debug_en: (( %d )), mode: (( %d )), macid: (( %d ))\n",
			enable, monitor_mode, macid);
	odm_fill_h2c_cmd(dm, PHYDM_H2C_FW_TRACE_EN, cmd_length, h2c_parameter);
}

bool phydm_api_set_txagc(struct phy_dm_struct *dm, u32 power_index,
			 enum odm_rf_radio_path path, u8 hw_rate,
			 bool is_single_rate)
{
	bool ret = false;

	if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C)) {
		if (is_single_rate) {
			if (dm->support_ic_type == ODM_RTL8822B)
				ret = phydm_write_txagc_1byte_8822b(
					dm, power_index, path, hw_rate);

		} else {
			if (dm->support_ic_type == ODM_RTL8822B)
				ret = config_phydm_write_txagc_8822b(
					dm, power_index, path, hw_rate);
		}
	}

	return ret;
}

static u8 phydm_api_get_txagc(struct phy_dm_struct *dm,
			      enum odm_rf_radio_path path, u8 hw_rate)
{
	u8 ret = 0;

	if (dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_read_txagc_8822b(dm, path, hw_rate);

	return ret;
}

static bool phydm_api_switch_bw_channel(struct phy_dm_struct *dm, u8 central_ch,
					u8 primary_ch_idx,
					enum odm_bw bandwidth)
{
	bool ret = false;

	if (dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_switch_channel_bw_8822b(
			dm, central_ch, primary_ch_idx, bandwidth);

	return ret;
}

bool phydm_api_trx_mode(struct phy_dm_struct *dm, enum odm_rf_path tx_path,
			enum odm_rf_path rx_path, bool is_tx2_path)
{
	bool ret = false;

	if (dm->support_ic_type & ODM_RTL8822B)
		ret = config_phydm_trx_mode_8822b(dm, tx_path, rx_path,
						  is_tx2_path);

	return ret;
}

static void phydm_get_per_path_txagc(void *dm_void, u8 path, u32 *_used,
				     char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 rate_idx;
	u8 txagc;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (((dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) &&
	     path <= ODM_RF_PATH_B) ||
	    ((dm->support_ic_type & (ODM_RTL8821C)) &&
	     path <= ODM_RF_PATH_A)) {
		for (rate_idx = 0; rate_idx <= 0x53; rate_idx++) {
			if (rate_idx == ODM_RATE1M)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "  %-35s\n", "CCK====>");
			else if (rate_idx == ODM_RATE6M)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "OFDM====>");
			else if (rate_idx == ODM_RATEMCS0)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "HT 1ss====>");
			else if (rate_idx == ODM_RATEMCS8)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "HT 2ss====>");
			else if (rate_idx == ODM_RATEMCS16)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "HT 3ss====>");
			else if (rate_idx == ODM_RATEMCS24)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "HT 4ss====>");
			else if (rate_idx == ODM_RATEVHTSS1MCS0)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "VHT 1ss====>");
			else if (rate_idx == ODM_RATEVHTSS2MCS0)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "VHT 2ss====>");
			else if (rate_idx == ODM_RATEVHTSS3MCS0)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "VHT 3ss====>");
			else if (rate_idx == ODM_RATEVHTSS4MCS0)
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "\n  %-35s\n", "VHT 4ss====>");

			txagc = phydm_api_get_txagc(
				dm, (enum odm_rf_radio_path)path, rate_idx);
			if (config_phydm_read_txagc_check(txagc))
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "  0x%02x    ", txagc);
			else
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "  0x%s    ", "xx");
		}
	}
}

static void phydm_get_txagc(void *dm_void, u32 *_used, char *output,
			    u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* path-A */
	PHYDM_SNPRINTF(output + used, out_len - used, "%-35s\n",
		       "path-A====================");
	phydm_get_per_path_txagc(dm, ODM_RF_PATH_A, _used, output, _out_len);

	/* path-B */
	PHYDM_SNPRINTF(output + used, out_len - used, "\n%-35s\n",
		       "path-B====================");
	phydm_get_per_path_txagc(dm, ODM_RF_PATH_B, _used, output, _out_len);

	/* path-C */
	PHYDM_SNPRINTF(output + used, out_len - used, "\n%-35s\n",
		       "path-C====================");
	phydm_get_per_path_txagc(dm, ODM_RF_PATH_C, _used, output, _out_len);

	/* path-D */
	PHYDM_SNPRINTF(output + used, out_len - used, "\n%-35s\n",
		       "path-D====================");
	phydm_get_per_path_txagc(dm, ODM_RF_PATH_D, _used, output, _out_len);
}

static void phydm_set_txagc(void *dm_void, u32 *const dm_value, u32 *_used,
			    char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/*dm_value[1] = path*/
	/*dm_value[2] = hw_rate*/
	/*dm_value[3] = power_index*/

	if (dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C)) {
		if (dm_value[1] <= 1) {
			phydm_check_dmval_txagc(dm, used, out_len, dm_value,
						output);
		} else {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %s%d   %s%x%s\n", "Write path-",
				       (dm_value[1] & 0x1), "rate index-0x",
				       (dm_value[2] & 0x7f), " fail");
		}
	}
}

static void phydm_debug_trace(void *dm_void, u32 *const dm_value, u32 *_used,
			      char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 pre_debug_components, one = 1;
	u32 used = *_used;
	u32 out_len = *_out_len;

	pre_debug_components = dm->debug_components;

	PHYDM_SNPRINTF(output + used, out_len - used, "\n%s\n",
		       "================================");
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "[Debug Message] PhyDM Selection");
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "00. (( %s ))DIG\n",
			       ((dm->debug_components & ODM_COMP_DIG) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "01. (( %s ))RA_MASK\n",
			((dm->debug_components & ODM_COMP_RA_MASK) ? ("V") :
								     (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used,
			"02. (( %s ))DYNAMIC_TXPWR\n",
			((dm->debug_components & ODM_COMP_DYNAMIC_TXPWR) ?
				 ("V") :
				 (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "03. (( %s ))FA_CNT\n",
			((dm->debug_components & ODM_COMP_FA_CNT) ? ("V") :
								    (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "04. (( %s ))RSSI_MONITOR\n",
			       ((dm->debug_components & ODM_COMP_RSSI_MONITOR) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "05. (( %s ))SNIFFER\n",
			((dm->debug_components & ODM_COMP_SNIFFER) ? ("V") :
								     (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "06. (( %s ))ANT_DIV\n",
			((dm->debug_components & ODM_COMP_ANT_DIV) ? ("V") :
								     (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "07. (( %s ))DFS\n",
			       ((dm->debug_components & ODM_COMP_DFS) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "08. (( %s ))NOISY_DETECT\n",
			       ((dm->debug_components & ODM_COMP_NOISY_DETECT) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used,
			"09. (( %s ))RATE_ADAPTIVE\n",
			((dm->debug_components & ODM_COMP_RATE_ADAPTIVE) ?
				 ("V") :
				 (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "10. (( %s ))PATH_DIV\n",
			((dm->debug_components & ODM_COMP_PATH_DIV) ? ("V") :
								      (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used,
			"12. (( %s ))DYNAMIC_PRICCA\n",
			((dm->debug_components & ODM_COMP_DYNAMIC_PRICCA) ?
				 ("V") :
				 (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "14. (( %s ))MP\n",
			((dm->debug_components & ODM_COMP_MP) ? ("V") : (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "15. (( %s ))struct cfo_tracking\n",
			       ((dm->debug_components & ODM_COMP_CFO_TRACKING) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "16. (( %s ))struct acs_info\n",
			       ((dm->debug_components & ODM_COMP_ACS) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "17. (( %s ))ADAPTIVITY\n",
			       ((dm->debug_components & PHYDM_COMP_ADAPTIVITY) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "18. (( %s ))RA_DBG\n",
			((dm->debug_components & PHYDM_COMP_RA_DBG) ? ("V") :
								      (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "19. (( %s ))TXBF\n",
			((dm->debug_components & PHYDM_COMP_TXBF) ? ("V") :
								    (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "20. (( %s ))EDCA_TURBO\n",
			       ((dm->debug_components & ODM_COMP_EDCA_TURBO) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "22. (( %s ))FW_DEBUG_TRACE\n",
			       ((dm->debug_components & ODM_FW_DEBUG_TRACE) ?
					("V") :
					(".")));

		PHYDM_SNPRINTF(output + used, out_len - used,
			       "24. (( %s ))TX_PWR_TRACK\n",
			       ((dm->debug_components & ODM_COMP_TX_PWR_TRACK) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "26. (( %s ))CALIBRATION\n",
			       ((dm->debug_components & ODM_COMP_CALIBRATION) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "28. (( %s ))PHY_CONFIG\n",
			       ((dm->debug_components & ODM_PHY_CONFIG) ?
					("V") :
					(".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "29. (( %s ))INIT\n",
			((dm->debug_components & ODM_COMP_INIT) ? ("V") :
								  (".")));
		PHYDM_SNPRINTF(
			output + used, out_len - used, "30. (( %s ))COMMON\n",
			((dm->debug_components & ODM_COMP_COMMON) ? ("V") :
								    (".")));
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "31. (( %s ))API\n",
			       ((dm->debug_components & ODM_COMP_API) ? ("V") :
									(".")));
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");

	} else if (dm_value[0] == 101) {
		dm->debug_components = 0;
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "Disable all debug components");
	} else {
		if (dm_value[1] == 1) /*enable*/
			dm->debug_components |= (one << dm_value[0]);
		else if (dm_value[1] == 2) /*disable*/
			dm->debug_components &= ~(one << dm_value[0]);
		else
			PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
				       "[Warning!!!]  1:enable,  2:disable");
	}
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "pre-DbgComponents = 0x%x\n", pre_debug_components);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "Curr-DbgComponents = 0x%x\n", dm->debug_components);
	PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
		       "================================");
}

static void phydm_fw_debug_trace(void *dm_void, u32 *const dm_value, u32 *_used,
				 char *output, u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 pre_fw_debug_components, one = 1;
	u32 used = *_used;
	u32 out_len = *_out_len;

	pre_fw_debug_components = dm->fw_debug_components;

	PHYDM_SNPRINTF(output + used, out_len - used, "\n%s\n",
		       "================================");
	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "[FW Debug Component]");
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");
		PHYDM_SNPRINTF(
			output + used, out_len - used, "00. (( %s ))RA\n",
			((dm->fw_debug_components & PHYDM_FW_COMP_RA) ? ("V") :
									(".")));

		if (dm->support_ic_type & PHYDM_IC_3081_SERIES) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"01. (( %s ))MU\n",
				((dm->fw_debug_components & PHYDM_FW_COMP_MU) ?
					 ("V") :
					 (".")));
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "02. (( %s ))path Div\n",
				       ((dm->fw_debug_components &
					 PHYDM_FW_COMP_PHY_CONFIG) ?
						("V") :
						(".")));
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "03. (( %s ))Phy Config\n",
				       ((dm->fw_debug_components &
					 PHYDM_FW_COMP_PHY_CONFIG) ?
						("V") :
						(".")));
		}
		PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
			       "================================");

	} else {
		if (dm_value[0] == 101) {
			dm->fw_debug_components = 0;
			PHYDM_SNPRINTF(output + used, out_len - used, "%s\n",
				       "Clear all fw debug components");
		} else {
			if (dm_value[1] == 1) /*enable*/
				dm->fw_debug_components |= (one << dm_value[0]);
			else if (dm_value[1] == 2) /*disable*/
				dm->fw_debug_components &=
					~(one << dm_value[0]);
			else
				PHYDM_SNPRINTF(
					output + used, out_len - used, "%s\n",
					"[Warning!!!]  1:enable,  2:disable");
		}

		if (dm->fw_debug_components == 0) {
			dm->debug_components &= ~ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(
				dm, false, dm->fw_debug_components, dm_value[2],
				dm_value[3]); /*H2C to enable C2H Msg*/
		} else {
			dm->debug_components |= ODM_FW_DEBUG_TRACE;
			phydm_fw_trace_en_h2c(
				dm, true, dm->fw_debug_components, dm_value[2],
				dm_value[3]); /*H2C to enable C2H Msg*/
		}
	}
}

static void phydm_dump_bb_reg(void *dm_void, u32 *_used, char *output,
			      u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* For Nseries IC we only need to dump page8 to pageF using 3 digits*/
	for (addr = 0x800; addr < 0xfff; addr += 4) {
		if (dm->support_ic_type & ODM_IC_11N_SERIES)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%03x 0x%08x\n", addr,
				odm_get_bb_reg(dm, addr, MASKDWORD));
		else
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%04x 0x%08x\n", addr,
				odm_get_bb_reg(dm, addr, MASKDWORD));
	}

	if (dm->support_ic_type &
	    (ODM_RTL8822B | ODM_RTL8814A | ODM_RTL8821C)) {
		if (dm->rf_type > ODM_2T2R) {
			for (addr = 0x1800; addr < 0x18ff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF(
					output + used, out_len - used,
					"0x%04x 0x%08x\n", addr,
					odm_get_bb_reg(dm, addr, MASKDWORD));
		}

		if (dm->rf_type > ODM_3T3R) {
			for (addr = 0x1a00; addr < 0x1aff; addr += 4)
				PHYDM_VAST_INFO_SNPRINTF(
					output + used, out_len - used,
					"0x%04x 0x%08x\n", addr,
					odm_get_bb_reg(dm, addr, MASKDWORD));
		}

		for (addr = 0x1900; addr < 0x19ff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%04x 0x%08x\n", addr,
				odm_get_bb_reg(dm, addr, MASKDWORD));

		for (addr = 0x1c00; addr < 0x1cff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%04x 0x%08x\n", addr,
				odm_get_bb_reg(dm, addr, MASKDWORD));

		for (addr = 0x1f00; addr < 0x1fff; addr += 4)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%04x 0x%08x\n", addr,
				odm_get_bb_reg(dm, addr, MASKDWORD));
	}
}

static void phydm_dump_all_reg(void *dm_void, u32 *_used, char *output,
			       u32 *_out_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u32 addr = 0;
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* dump MAC register */
	PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
				 "MAC==========\n");
	for (addr = 0; addr < 0x7ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "0x%04x 0x%08x\n", addr,
					 odm_get_bb_reg(dm, addr, MASKDWORD));

	for (addr = 0x1000; addr < 0x17ff; addr += 4)
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "0x%04x 0x%08x\n", addr,
					 odm_get_bb_reg(dm, addr, MASKDWORD));

	/* dump BB register */
	PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
				 "BB==========\n");
	phydm_dump_bb_reg(dm, &used, output, &out_len);

	/* dump RF register */
	PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
				 "RF-A==========\n");
	for (addr = 0; addr < 0xFF; addr++)
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "0x%02x 0x%05x\n", addr,
					 odm_get_rf_reg(dm, ODM_RF_PATH_A, addr,
							RFREGOFFSETMASK));

	if (dm->rf_type > ODM_1T1R) {
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "RF-B==========\n");
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%02x 0x%05x\n", addr,
				odm_get_rf_reg(dm, ODM_RF_PATH_B, addr,
					       RFREGOFFSETMASK));
	}

	if (dm->rf_type > ODM_2T2R) {
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "RF-C==========\n");
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%02x 0x%05x\n", addr,
				odm_get_rf_reg(dm, ODM_RF_PATH_C, addr,
					       RFREGOFFSETMASK));
	}

	if (dm->rf_type > ODM_3T3R) {
		PHYDM_VAST_INFO_SNPRINTF(output + used, out_len - used,
					 "RF-D==========\n");
		for (addr = 0; addr < 0xFF; addr++)
			PHYDM_VAST_INFO_SNPRINTF(
				output + used, out_len - used,
				"0x%02x 0x%05x\n", addr,
				odm_get_rf_reg(dm, ODM_RF_PATH_D, addr,
					       RFREGOFFSETMASK));
	}
}

static void phydm_enable_big_jump(struct phy_dm_struct *dm, bool state)
{
	struct dig_thres *dig_tab = &dm->dm_dig_table;

	if (!state) {
		dm->dm_dig_table.enable_adjust_big_jump = false;
		odm_set_bb_reg(dm, 0x8c8, 0xfe,
			       ((dig_tab->big_jump_step3 << 5) |
				(dig_tab->big_jump_step2 << 3) |
				dig_tab->big_jump_step1));
	} else {
		dm->dm_dig_table.enable_adjust_big_jump = true;
	}
}

static void phydm_show_rx_rate(struct phy_dm_struct *dm, u32 *_used,
			       char *output, u32 *_out_len)
{
	u32 used = *_used;
	u32 out_len = *_out_len;

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "=====Rx SU rate Statistics=====\n");
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		dm->phy_dbg_info.num_qry_vht_pkt[0],
		dm->phy_dbg_info.num_qry_vht_pkt[1],
		dm->phy_dbg_info.num_qry_vht_pkt[2],
		dm->phy_dbg_info.num_qry_vht_pkt[3]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		dm->phy_dbg_info.num_qry_vht_pkt[4],
		dm->phy_dbg_info.num_qry_vht_pkt[5],
		dm->phy_dbg_info.num_qry_vht_pkt[6],
		dm->phy_dbg_info.num_qry_vht_pkt[7]);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		       dm->phy_dbg_info.num_qry_vht_pkt[8],
		       dm->phy_dbg_info.num_qry_vht_pkt[9]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		dm->phy_dbg_info.num_qry_vht_pkt[10],
		dm->phy_dbg_info.num_qry_vht_pkt[11],
		dm->phy_dbg_info.num_qry_vht_pkt[12],
		dm->phy_dbg_info.num_qry_vht_pkt[13]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		dm->phy_dbg_info.num_qry_vht_pkt[14],
		dm->phy_dbg_info.num_qry_vht_pkt[15],
		dm->phy_dbg_info.num_qry_vht_pkt[16],
		dm->phy_dbg_info.num_qry_vht_pkt[17]);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		       dm->phy_dbg_info.num_qry_vht_pkt[18],
		       dm->phy_dbg_info.num_qry_vht_pkt[19]);

	PHYDM_SNPRINTF(output + used, out_len - used,
		       "=====Rx MU rate Statistics=====\n");
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"1SS MCS0 = %d, 1SS MCS1 = %d, 1SS MCS2 = %d, 1SS MCS 3 = %d\n",
		dm->phy_dbg_info.num_qry_mu_vht_pkt[0],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[1],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[2],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[3]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"1SS MCS4 = %d, 1SS MCS5 = %d, 1SS MCS6 = %d, 1SS MCS 7 = %d\n",
		dm->phy_dbg_info.num_qry_mu_vht_pkt[4],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[5],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[6],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[7]);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "1SS MCS8 = %d, 1SS MCS9 = %d\n",
		       dm->phy_dbg_info.num_qry_mu_vht_pkt[8],
		       dm->phy_dbg_info.num_qry_mu_vht_pkt[9]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"2SS MCS0 = %d, 2SS MCS1 = %d, 2SS MCS2 = %d, 2SS MCS 3 = %d\n",
		dm->phy_dbg_info.num_qry_mu_vht_pkt[10],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[11],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[12],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[13]);
	PHYDM_SNPRINTF(
		output + used, out_len - used,
		"2SS MCS4 = %d, 2SS MCS5 = %d, 2SS MCS6 = %d, 2SS MCS 7 = %d\n",
		dm->phy_dbg_info.num_qry_mu_vht_pkt[14],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[15],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[16],
		dm->phy_dbg_info.num_qry_mu_vht_pkt[17]);
	PHYDM_SNPRINTF(output + used, out_len - used,
		       "2SS MCS8 = %d, 2SS MCS9 = %d\n",
		       dm->phy_dbg_info.num_qry_mu_vht_pkt[18],
		       dm->phy_dbg_info.num_qry_mu_vht_pkt[19]);
}

struct phydm_command {
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

static struct phydm_command phy_dm_ary[] = {
	{"-h", PHYDM_HELP}, /*do not move this element to other position*/
	{"demo", PHYDM_DEMO}, /*do not move this element to other position*/
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
	{"dbgport", PHYDM_DEBUG_PORT},
};

void phydm_cmd_parser(struct phy_dm_struct *dm, char input[][MAX_ARGV],
		      u32 input_num, u8 flag, char *output, u32 out_len)
{
	u32 used = 0;
	u8 id = 0;
	int var1[10] = {0};
	int i, input_idx = 0, phydm_ary_size;
	char help[] = "-h";

	bool is_enable_dbg_mode;
	u8 central_ch, primary_ch_idx, bandwidth;

	if (flag == 0) {
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "GET, nothing to print\n");
		return;
	}

	PHYDM_SNPRINTF(output + used, out_len - used, "\n");

	/* Parsing Cmd ID */
	if (input_num) {
		phydm_ary_size = ARRAY_SIZE(phy_dm_ary);
		for (i = 0; i < phydm_ary_size; i++) {
			if (strcmp(phy_dm_ary[i].name, input[0]) == 0) {
				id = phy_dm_ary[i].id;
				break;
			}
		}
		if (i == phydm_ary_size) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "SET, command not found!\n");
			return;
		}
	}

	switch (id) {
	case PHYDM_HELP: {
		PHYDM_SNPRINTF(output + used, out_len - used, "BB cmd ==>\n");
		for (i = 0; i < phydm_ary_size - 2; i++) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "  %-5d: %s\n", i,
				       phy_dm_ary[i + 2].name);
			/**/
		}
	} break;

	case PHYDM_DEMO: { /*echo demo 10 0x3a z abcde >cmd*/
		u32 directory = 0;

		char char_temp;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &directory);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "Decimal value = %d\n", directory);
		PHYDM_SSCANF(input[2], DCMD_HEX, &directory);
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "Hex value = 0x%x\n", directory);
		PHYDM_SSCANF(input[3], DCMD_CHAR, &char_temp);
		PHYDM_SNPRINTF(output + used, out_len - used, "Char = %c\n",
			       char_temp);
		PHYDM_SNPRINTF(output + used, out_len - used, "String = %s\n",
			       input[4]);
	} break;

	case PHYDM_RA:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);

				input_idx++;
			}
		}

		if (input_idx >= 1) {
			phydm_RA_debug_PCR(dm, (u32 *)var1, &used, output,
					   &out_len);
		}

		break;

	case PHYDM_ANTDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				input_idx++;
			}
		}

		break;

	case PHYDM_PATHDIV:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);

				input_idx++;
			}
		}

		break;

	case PHYDM_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);

				input_idx++;
			}
		}

		if (input_idx >= 1) {
			phydm_debug_trace(dm, (u32 *)var1, &used, output,
					  &out_len);
		}

		break;

	case PHYDM_FW_DEBUG:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1)
			phydm_fw_debug_trace(dm, (u32 *)var1, &used, output,
					     &out_len);

		break;

	case PHYDM_SUPPORT_ABILITY:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);

				input_idx++;
			}
		}

		if (input_idx >= 1) {
			phydm_support_ability_debug(dm, (u32 *)var1, &used,
						    output, &out_len);
		}

		break;

	case PHYDM_SMART_ANT:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		break;

	case PHYDM_API:
		if (!(dm->support_ic_type &
		      (ODM_RTL8822B | ODM_RTL8197F | ODM_RTL8821C))) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"This IC doesn't support PHYDM API function\n");
		}

		for (i = 0; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
		}

		is_enable_dbg_mode = (bool)var1[0];
		central_ch = (u8)var1[1];
		primary_ch_idx = (u8)var1[2];
		bandwidth = (enum odm_bw)var1[3];

		if (is_enable_dbg_mode) {
			dm->is_disable_phy_api = false;
			phydm_api_switch_bw_channel(dm, central_ch,
						    primary_ch_idx,
						    (enum odm_bw)bandwidth);
			dm->is_disable_phy_api = true;
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"central_ch = %d, primary_ch_idx = %d, bandwidth = %d\n",
				central_ch, primary_ch_idx, bandwidth);
		} else {
			dm->is_disable_phy_api = false;
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Disable API debug mode\n");
		}
		break;

	case PHYDM_PROFILE: /*echo profile, >cmd*/
		phydm_basic_profile(dm, &used, output, &out_len);
		break;

	case PHYDM_GET_TXAGC:
		phydm_get_txagc(dm, &used, output, &out_len);
		break;

	case PHYDM_SET_TXAGC: {
		bool is_enable_dbg_mode;

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if ((strcmp(input[1], help) == 0)) {
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"{En} {pathA~D(0~3)} {rate_idx(Hex), All_rate:0xff} {txagc_idx (Hex)}\n");
			/**/

		} else {
			is_enable_dbg_mode = (bool)var1[0];
			if (is_enable_dbg_mode) {
				dm->is_disable_phy_api = false;
				phydm_set_txagc(dm, (u32 *)var1, &used, output,
						&out_len);
				dm->is_disable_phy_api = true;
			} else {
				dm->is_disable_phy_api = false;
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "Disable API debug mode\n");
			}
		}
	} break;

	case PHYDM_TRX_PATH:

		for (i = 0; i < 4; i++) {
			if (input[i + 1])
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
		}
		if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8197F)) {
			u8 tx_path, rx_path;
			bool is_enable_dbg_mode, is_tx2_path;

			is_enable_dbg_mode = (bool)var1[0];
			tx_path = (u8)var1[1];
			rx_path = (u8)var1[2];
			is_tx2_path = (bool)var1[3];

			if (is_enable_dbg_mode) {
				dm->is_disable_phy_api = false;
				phydm_api_trx_mode(
					dm, (enum odm_rf_path)tx_path,
					(enum odm_rf_path)rx_path, is_tx2_path);
				dm->is_disable_phy_api = true;
				PHYDM_SNPRINTF(
					output + used, out_len - used,
					"tx_path = 0x%x, rx_path = 0x%x, is_tx2_path = %d\n",
					tx_path, rx_path, is_tx2_path);
			} else {
				dm->is_disable_phy_api = false;
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "Disable API debug mode\n");
			}
		} else {
			phydm_config_trx_path(dm, (u32 *)var1, &used, output,
					      &out_len);
		}
		break;

	case PHYDM_LA_MODE:

		dm->support_ability &= ~(ODM_BB_FA_CNT);
		phydm_lamode_trigger_setting(dm, &input[0], &used, output,
					     &out_len, input_num);
		dm->support_ability |= ODM_BB_FA_CNT;

		break;

	case PHYDM_DUMP_REG: {
		u8 type = 0;

		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			type = (u8)var1[0];
		}

		if (type == 0)
			phydm_dump_bb_reg(dm, &used, output, &out_len);
		else if (type == 1)
			phydm_dump_all_reg(dm, &used, output, &out_len);
	} break;

	case PHYDM_MU_MIMO:

		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
		else
			var1[0] = 0;

		if (var1[0] == 1) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Get MU BFee CSI\n");
			odm_set_bb_reg(dm, 0x9e8, BIT(17) | BIT(16),
				       2); /*Read BFee*/
			odm_set_bb_reg(dm, 0x1910, BIT(15),
				       1); /*Select BFee's CSI report*/
			odm_set_bb_reg(dm, 0x19b8, BIT(6),
				       1); /*set as CSI report*/
			odm_set_bb_reg(dm, 0x19a8, 0xFFFF,
				       0xFFFF); /*disable gated_clk*/
			phydm_print_csi(dm, used, out_len, output);

		} else if (var1[0] == 2) {
			PHYDM_SSCANF(input[2], DCMD_DECIMAL, &var1[1]);
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Get MU BFer's STA%d CSI\n", var1[1]);
			odm_set_bb_reg(dm, 0x9e8, BIT(24), 0); /*Read BFer*/
			odm_set_bb_reg(dm, 0x9e8, BIT(25),
				       1); /*enable Read/Write RAM*/
			odm_set_bb_reg(dm, 0x9e8, BIT(30) | BIT(29) | BIT(28),
				       var1[1]); /*read which STA's CSI report*/
			odm_set_bb_reg(dm, 0x1910, BIT(15),
				       0); /*select BFer's CSI*/
			odm_set_bb_reg(dm, 0x19e0, 0x00003FC0,
				       0xFF); /*disable gated_clk*/
			phydm_print_csi(dm, used, out_len, output);
		}
		break;

	case PHYDM_BIG_JUMP: {
		if (input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			phydm_enable_big_jump(dm, (bool)(var1[0]));
		} else {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "unknown command!\n");
		}
		break;
	}

	case PHYDM_HANG:
		phydm_bb_rx_hang_info(dm, &used, output, &out_len);
		break;

	case PHYDM_SHOW_RXRATE: {
		u8 rate_idx;

		if (input[1])
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (var1[0] == 1) {
			phydm_show_rx_rate(dm, &used, output, &out_len);
		} else {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Reset Rx rate counter\n");

			for (rate_idx = 0; rate_idx < 40; rate_idx++) {
				dm->phy_dbg_info.num_qry_vht_pkt[rate_idx] = 0;
				dm->phy_dbg_info.num_qry_mu_vht_pkt[rate_idx] =
					0;
			}
		}
	} break;

	case PHYDM_NBI_EN:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			phydm_api_debug(dm, PHYDM_API_NBI, (u32 *)var1, &used,
					output, &out_len);
			/**/
		}

		break;

	case PHYDM_CSI_MASK_EN:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			phydm_api_debug(dm, PHYDM_API_CSI_MASK, (u32 *)var1,
					&used, output, &out_len);
			/**/
		}

		break;

	case PHYDM_DFS:
		break;

	case PHYDM_IQK:
		break;

	case PHYDM_NHM: {
		u8 target_rssi;
		u16 nhm_period = 0xC350; /* 200ms */
		u8 IGI;
		struct ccx_info *ccx_info = &dm->dm_ccx_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (input_num == 1) {
			ccx_info->echo_NHM_en = false;
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Trigger NHM: echo nhm 1\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r (Exclude CCA)\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Trigger NHM: echo nhm 2\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r (Include CCA)\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Get NHM results: echo nhm 3\n");

			return;
		}

		/* NMH trigger */
		if (var1[0] <= 2 && var1[0] != 0) {
			ccx_info->echo_NHM_en = true;
			ccx_info->echo_IGI =
				(u8)odm_get_bb_reg(dm, 0xC50, MASKBYTE0);

			target_rssi = ccx_info->echo_IGI - 10;

			ccx_info->NHM_th[0] = (target_rssi - 15 + 10) * 2;

			for (i = 1; i <= 10; i++)
				ccx_info->NHM_th[i] =
					ccx_info->NHM_th[0] + 6 * i;

			/* 4 1. store previous NHM setting */
			phydm_nhm_setting(dm, STORE_NHM_SETTING);

			/* 4 2. Set NHM period, 0x990[31:16]=0xC350,
			 * Time duration for NHM unit: 4us, 0xC350=200ms
			 */
			ccx_info->NHM_period = nhm_period;

			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Monitor NHM for %d us",
				       nhm_period * 4);

			/* 4 3. Set NHM inexclude_txon, inexclude_cca, ccx_en */

			ccx_info->nhm_inexclude_cca = (var1[0] == 1) ?
							      NHM_EXCLUDE_CCA :
							      NHM_INCLUDE_CCA;
			ccx_info->nhm_inexclude_txon = NHM_EXCLUDE_TXON;

			phydm_nhm_setting(dm, SET_NHM_SETTING);
			phydm_print_nhm_trigger(output, used, out_len,
						ccx_info);

			/* 4 4. Trigger NHM */
			phydm_nhm_trigger(dm);
		}

		/*Get NHM results*/
		else if (var1[0] == 3) {
			IGI = (u8)odm_get_bb_reg(dm, 0xC50, MASKBYTE0);

			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Cur_IGI = 0x%x", IGI);

			phydm_get_nhm_result(dm);

			/* 4 Resotre NHM setting */
			phydm_nhm_setting(dm, RESTORE_NHM_SETTING);
			phydm_print_nhm_result(output, used, out_len, ccx_info);

			ccx_info->echo_NHM_en = false;
		} else {
			ccx_info->echo_NHM_en = false;
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Trigger NHM: echo nhm 1\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r (Exclude CCA)\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Trigger NHM: echo nhm 2\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r (Include CCA)\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Get NHM results: echo nhm 3\n");

			return;
		}
	} break;

	case PHYDM_CLM: {
		struct ccx_info *ccx_info = &dm->dm_ccx_info;

		PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

		if (input_num == 1) {
			ccx_info->echo_CLM_en = false;
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Trigger CLM: echo clm 1\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Get CLM results: echo clm 2\n");
			return;
		}

		/* Set & trigger CLM */
		if (var1[0] == 1) {
			ccx_info->echo_CLM_en = true;
			ccx_info->CLM_period = 0xC350; /*100ms*/
			phydm_clm_setting(dm);
			phydm_clm_trigger(dm);
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n Monitor CLM for 200ms\n");
		}

		/* Get CLM results */
		else if (var1[0] == 2) {
			ccx_info->echo_CLM_en = false;
			phydm_get_cl_mresult(dm);
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r\n CLM_result = %d us\n",
				       ccx_info->CLM_result * 4);

		} else {
			ccx_info->echo_CLM_en = false;
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\n\r Error command !\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Trigger CLM: echo clm 1\n");
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "\r Get CLM results: echo clm 2\n");
		}
	} break;

	case PHYDM_BB_INFO: {
		s32 value32 = 0;

		phydm_bb_debug_info(dm, &used, output, &out_len);

		if (dm->support_ic_type & ODM_RTL8822B && input[1]) {
			PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);
			odm_set_bb_reg(dm, 0x1988, 0x003fff00, var1[0]);
			value32 = odm_get_bb_reg(dm, 0xf84, MASKDWORD);
			value32 = (value32 & 0xff000000) >> 24;
			PHYDM_SNPRINTF(
				output + used, out_len - used,
				"\r\n %-35s = condition num = %d, subcarriers = %d\n",
				"Over condition num subcarrier", var1[0],
				value32);
			odm_set_bb_reg(dm, 0x1988, BIT(22),
				       0x0); /*disable report condition number*/
		}
	} break;

	case PHYDM_TXBF: {
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "\r\n no TxBF !!\n");
	} break;

	case PHYDM_PAUSE_DIG_EN:

		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_HEX, &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			if (var1[0] == 0) {
				odm_pause_dig(dm, PHYDM_PAUSE,
					      PHYDM_PAUSE_LEVEL_7, (u8)var1[1]);
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "Set IGI_value = ((%x))\n",
					       var1[1]);
			} else if (var1[0] == 1) {
				odm_pause_dig(dm, PHYDM_RESUME,
					      PHYDM_PAUSE_LEVEL_7, (u8)var1[1]);
				PHYDM_SNPRINTF(output + used, out_len - used,
					       "Resume IGI_value\n");
			} else {
				PHYDM_SNPRINTF(
					output + used, out_len - used,
					"echo  (1:pause, 2resume)  (IGI_value)\n");
			}
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
			phydm_h2C_debug(dm, (u32 *)var1, &used, output,
					&out_len);

		break;

	case PHYDM_ANT_SWITCH:

		for (i = 0; i < 8; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL,
					     &var1[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Not Support IC");
		}

		break;

	case PHYDM_DYNAMIC_RA_PATH:

		PHYDM_SNPRINTF(output + used, out_len - used, "Not Support IC");

		break;

	case PHYDM_PSD:

		phydm_psd_debug(dm, &input[0], &used, output, &out_len,
				input_num);

		break;

	case PHYDM_DEBUG_PORT: {
		u32 dbg_port_value;

		PHYDM_SSCANF(input[1], DCMD_HEX, &var1[0]);

		if (phydm_set_bb_dbg_port(dm, BB_DBGPORT_PRIORITY_3,
					  var1[0])) { /*set debug port to 0x0*/

			dbg_port_value = phydm_get_bb_dbg_port_value(dm);
			phydm_release_bb_dbg_port(dm);

			PHYDM_SNPRINTF(output + used, out_len - used,
				       "Debug Port[0x%x] = ((0x%x))\n", var1[1],
				       dbg_port_value);
		}
	} break;

	default:
		PHYDM_SNPRINTF(output + used, out_len - used,
			       "SET, unknown command!\n");
		break;
	}
}

s32 phydm_cmd(struct phy_dm_struct *dm, char *input, u32 in_len, u8 flag,
	      char *output, u32 out_len)
{
	char *token;
	u32 argc = 0;
	char argv[MAX_ARGC][MAX_ARGV];

	do {
		token = strsep(&input, ", ");
		if (token) {
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

void phydm_fw_trace_handler(void *dm_void, u8 *cmd_buf, u8 cmd_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	/*u8	debug_trace_11byte[60];*/
	u8 freg_num, c2h_seq, buf_0 = 0;

	if (!(dm->support_ic_type & PHYDM_IC_3081_SERIES))
		return;

	if (cmd_len > 12)
		return;

	buf_0 = cmd_buf[0];
	freg_num = (buf_0 & 0xf);
	c2h_seq = (buf_0 & 0xf0) >> 4;

	if (c2h_seq != dm->pre_c2h_seq && !dm->fw_buff_is_enpty) {
		dm->fw_debug_trace[dm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "[FW Dbg Queue Overflow] %s\n",
			     dm->fw_debug_trace);
		dm->c2h_cmd_start = 0;
	}

	if ((cmd_len - 1) > (60 - dm->c2h_cmd_start)) {
		dm->fw_debug_trace[dm->c2h_cmd_start] = '\0';
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
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

		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "[FW DBG Msg] %s\n",
			     dm->fw_debug_trace);
		/*dbg_print("[FW DBG Msg] %s\n", dm->fw_debug_trace);*/
		dm->c2h_cmd_start = 0;
		dm->fw_buff_is_enpty = true;
	}

	dm->pre_c2h_seq = c2h_seq;
}

void phydm_fw_trace_handler_code(void *dm_void, u8 *buffer, u8 cmd_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;
	u8 function = buffer[0];
	u8 dbg_num = buffer[1];
	u16 content_0 = (((u16)buffer[3]) << 8) | ((u16)buffer[2]);
	u16 content_1 = (((u16)buffer[5]) << 8) | ((u16)buffer[4]);
	u16 content_2 = (((u16)buffer[7]) << 8) | ((u16)buffer[6]);
	u16 content_3 = (((u16)buffer[9]) << 8) | ((u16)buffer[8]);
	u16 content_4 = (((u16)buffer[11]) << 8) | ((u16)buffer[10]);

	if (cmd_len > 12)
		ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
			     "[FW Msg] Invalid cmd length (( %d )) >12\n",
			     cmd_len);

	/*--------------------------------------------*/
	ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE,
		     "[FW][general][%d, %d, %d] = {%d, %d, %d, %d}\n", function,
		     dbg_num, content_0, content_1, content_2, content_3,
		     content_4);
	/*--------------------------------------------*/
}

void phydm_fw_trace_handler_8051(void *dm_void, u8 *buffer, u8 cmd_len)
{
	struct phy_dm_struct *dm = (struct phy_dm_struct *)dm_void;

	int i = 0;
	u8 extend_c2h_sub_id = 0, extend_c2h_dbg_len = 0,
	   extend_c2h_dbg_seq = 0;
	u8 fw_debug_trace[128];
	u8 *extend_c2h_dbg_content = NULL;

	if (cmd_len > 127)
		return;

	extend_c2h_sub_id = buffer[0];
	extend_c2h_dbg_len = buffer[1];
	extend_c2h_dbg_content = buffer + 2; /*DbgSeq+DbgContent  for show HEX*/

go_backfor_aggre_dbg_pkt:
	i = 0;
	extend_c2h_dbg_seq = buffer[2];
	extend_c2h_dbg_content = buffer + 3;

	for (;; i++) {
		fw_debug_trace[i] = extend_c2h_dbg_content[i];
		if (extend_c2h_dbg_content[i + 1] == '\0') {
			fw_debug_trace[i + 1] = '\0';
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "[FW DBG Msg] %s",
				     &fw_debug_trace[0]);
			break;
		} else if (extend_c2h_dbg_content[i] == '\n') {
			fw_debug_trace[i + 1] = '\0';
			ODM_RT_TRACE(dm, ODM_FW_DEBUG_TRACE, "[FW DBG Msg] %s",
				     &fw_debug_trace[0]);
			buffer = extend_c2h_dbg_content + i + 3;
			goto go_backfor_aggre_dbg_pkt;
		}
	}
}
