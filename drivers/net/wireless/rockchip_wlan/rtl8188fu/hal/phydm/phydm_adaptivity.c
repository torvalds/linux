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

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	#if WPP_SOFTWARE_TRACE
		#include "PhyDM_Adaptivity.tmh"
	#endif
#endif
#ifdef PHYDM_SUPPORT_ADAPTIVITY
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
boolean
phydm_check_channel_plan(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
	void *adapter = dm->adapter;
	PMGNT_INFO mgnt_info = &((PADAPTER)adapter)->MgntInfo;

	if (mgnt_info->RegEnableAdaptivity != 2)
		return false;

	if (!dm->carrier_sense_enable) { /*@check domain Code for adaptivity or CarrierSense*/
		if ((*dm->band_type == ODM_BAND_5G) &&
		    !(adapt->regulation_5g == REGULATION_ETSI || adapt->regulation_5g == REGULATION_WW)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "adaptivity skip 5G domain code : %d\n",
				  adapt->regulation_5g);
			return true;
		} else if ((*dm->band_type == ODM_BAND_2_4G) &&
			   !(adapt->regulation_2g == REGULATION_ETSI || adapt->regulation_2g == REGULATION_WW)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "adaptivity skip 2.4G domain code : %d\n",
				  adapt->regulation_2g);
			return true;
		} else if ((*dm->band_type != ODM_BAND_2_4G) && (*dm->band_type != ODM_BAND_5G)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "adaptivity neither 2G nor 5G band, return\n");
			return true;
		}
	} else {
		if ((*dm->band_type == ODM_BAND_5G) &&
		    !(adapt->regulation_5g == REGULATION_MKK || adapt->regulation_5g == REGULATION_WW)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "CarrierSense skip 5G domain code : %d\n",
				  adapt->regulation_5g);
			return true;
		} else if ((*dm->band_type == ODM_BAND_2_4G) &&
			   !(adapt->regulation_2g == REGULATION_MKK || adapt->regulation_2g == REGULATION_WW)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "CarrierSense skip 2.4G domain code : %d\n",
				  adapt->regulation_2g);
			return true;
		} else if ((*dm->band_type != ODM_BAND_2_4G) && (*dm->band_type != ODM_BAND_5G)) {
			PHYDM_DBG(dm, DBG_ADPTVTY,
				  "CarrierSense neither 2G nor 5G band, return\n");
			return true;
		}
	}

	return false;
}

boolean
phydm_soft_ap_special_set(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
	u8 disable_ap_adapt_setting = false;

	if (dm->soft_ap_mode != NULL) {
		if (*dm->soft_ap_mode != 0 &&
		    (dm->soft_ap_special_setting & BIT(0)))
			disable_ap_adapt_setting = true;
		else
			disable_ap_adapt_setting = false;
		PHYDM_DBG(dm, DBG_ADPTVTY,
			  "soft_ap_setting = %x, soft_ap = %d, dis_ap_adapt = %d\n",
			  dm->soft_ap_special_setting, *dm->soft_ap_mode,
			  disable_ap_adapt_setting);
	}

	return disable_ap_adapt_setting;
}

boolean
phydm_ap_num_check(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
	boolean dis_adapt = false;

	if  (dm->ap_total_num > adapt->ap_num_th) {
		dis_adapt = true;
		PHYDM_DBG(dm, DBG_ADPTVTY, "AP total num > %d, disable adapt\n",
			  adapt->ap_num_th);
	} else {
		dis_adapt = false;
	}
	return dis_adapt;
}
#endif

void phydm_dig_up_bound_lmt_en(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;

	if (!(dm->support_ability & ODM_BB_ADAPTIVITY) ||
	    !dm->is_linked ||
	    !adapt->is_adapt_en) {
		adapt->igi_up_bound_lmt_cnt = 0;
		adapt->igi_lmt_en = false;
		return;
	}

	if (dm->total_tp > 1) {
		adapt->igi_lmt_en = true;
		adapt->igi_up_bound_lmt_cnt = adapt->igi_up_bound_lmt_val;
		PHYDM_DBG(dm, DBG_ADPTVTY,
			  "TP >1, Start limit IGI upper bound\n");
	} else {
		if (adapt->igi_up_bound_lmt_cnt == 0)
			adapt->igi_lmt_en = false;
		else
			adapt->igi_up_bound_lmt_cnt--;
	}

	PHYDM_DBG(dm, DBG_ADPTVTY, "IGI_lmt_cnt = %d\n",
		  adapt->igi_up_bound_lmt_cnt);
}

void phydm_check_adaptivity(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;

	if (!(dm->support_ability & ODM_BB_ADAPTIVITY)) {
		adapt->is_adapt_en = false;
		PHYDM_DBG(dm, DBG_ADPTVTY, "adaptivity disable\n");
		return;
	}

	adapt->is_adapt_en = true;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (phydm_check_channel_plan(dm) ||
	    phydm_ap_num_check(dm) ||
	    phydm_soft_ap_special_set(dm))
		adapt->is_adapt_en = false;
#endif
}

void phydm_set_edcca_threshold(void *dm_void, s8 H2L, s8 L2H)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		odm_set_bb_reg(dm, R_0x84c, MASKBYTE2, (u8)L2H + 0x80);
		odm_set_bb_reg(dm, R_0x84c, MASKBYTE3, (u8)H2L + 0x80);
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		odm_set_bb_reg(dm, R_0xc4c, MASKBYTE0, (u8)L2H);
		odm_set_bb_reg(dm, R_0xc4c, MASKBYTE2, (u8)H2L);
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		odm_set_bb_reg(dm, R_0x8a4, MASKBYTE0, (u8)L2H);
		odm_set_bb_reg(dm, R_0x8a4, MASKBYTE1, (u8)H2L);
	}
}

void phydm_mac_edcca_state(void *dm_void, enum phydm_mac_edcca_type state)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (state == PHYDM_IGNORE_EDCCA) {
		odm_set_mac_reg(dm, R_0x520, BIT(15), 1); /*@ignore EDCCA*/
#if 0
		/*odm_set_mac_reg(dm, REG_RD_CTRL, BIT(11), 0);*/
#endif
	} else { /*@don't set MAC ignore EDCCA signal*/
		odm_set_mac_reg(dm, R_0x520, BIT(15), 0); /*@don't ignore EDCCA*/
#if 0
		/*odm_set_mac_reg(dm, REG_RD_CTRL, BIT(11), 1);*/
#endif
	}
	PHYDM_DBG(dm, DBG_ADPTVTY, "EDCCA enable state = %d\n", state);
}

void phydm_search_pwdb_lower_bound(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
	u32 value32 = 0, reg_value32 = 0;
	u8 cnt = 0, try_count = 0;
	u8 tx_edcca1 = 0;
	boolean is_adjust = true;
	s8 th_l2h_dmc, th_h2l_dmc, igi_target = 0x32;
	s8 diff = 0;
	s8 IGI = adapt->igi_base + 30 + dm->th_l2h_ini - dm->th_edcca_hl_diff;

	halrf_rf_lna_setting(dm, HALRF_LNA_DISABLE);
	diff = igi_target - IGI;
	th_l2h_dmc = dm->th_l2h_ini + diff;
	if (th_l2h_dmc > 10)
		th_l2h_dmc = 10;

	th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
	phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
	ODM_delay_ms(30);

	while (is_adjust) {
		/*@check CCA status*/
		/*set debug port to 0x0*/
		if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1, 0x0)) {
			reg_value32 = phydm_get_bb_dbg_port_val(dm);

			while (reg_value32 & BIT(3) && try_count < 3) {
				ODM_delay_ms(3);
				try_count = try_count + 1;
				reg_value32 = phydm_get_bb_dbg_port_val(dm);
			}
			phydm_release_bb_dbg_port(dm);
			try_count = 0;
		}

		/*@count EDCCA signal = 1 times*/
		for (cnt = 0; cnt < 20; cnt++) {
			if (phydm_set_bb_dbg_port(dm, DBGPORT_PRI_1,
						  adapt->adaptivity_dbg_port)) {
				value32 = phydm_get_bb_dbg_port_val(dm);
				phydm_release_bb_dbg_port(dm);
			}

			if (value32 & BIT(30) && dm->support_ic_type &
						 (ODM_RTL8723B | ODM_RTL8188E))
				tx_edcca1 = tx_edcca1 + 1;
			else if (value32 & BIT(29))
				tx_edcca1 = tx_edcca1 + 1;
		}

		if (tx_edcca1 > 1) {
			IGI = IGI - 1;
			th_l2h_dmc = th_l2h_dmc + 1;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
			phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
			tx_edcca1 = 0;
			if (th_l2h_dmc == 10)
				is_adjust = false;

		} else {
			is_adjust = false;
		}
	}

	adapt->adapt_igi_up = IGI - ADAPT_DC_BACKOFF;
	adapt->h2l_lb = th_h2l_dmc + ADAPT_DC_BACKOFF;
	adapt->l2h_lb = th_l2h_dmc + ADAPT_DC_BACKOFF;

	halrf_rf_lna_setting(dm, HALRF_LNA_ENABLE);
	phydm_set_edcca_threshold(dm, 0x7f, 0x7f); /*resume to no link state*/
}

boolean
phydm_re_search_condition(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adaptivity = &dm->adaptivity;
	u8 adaptivity_igi_upper = adaptivity->adapt_igi_up + ADAPT_DC_BACKOFF;

	if (adaptivity_igi_upper <= 0x26)
		return true;
	else
		return false;
}

void phydm_set_l2h_th_ini(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
		if (dm->support_ic_type &
		    (ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8814A))
			dm->th_l2h_ini = 0xf2;
		else
			dm->th_l2h_ini = 0xef;
	} else  if (dm->support_ic_type & ODM_RTL8822C) {
		dm->th_l2h_ini = 0x2d;
	} else  if (dm->support_ic_type & ODM_RTL8814B) {
		dm->th_l2h_ini = 0x31;
	} else {
		dm->th_l2h_ini = 0xf5;
	}
}

void phydm_set_l2h_th_ini_carrier_sense(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
		dm->th_l2h_ini = 0x3c;
	else
		dm->th_l2h_ini = 0xa;
}

void phydm_set_forgetting_factor(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (dm->support_ic_type &
		    (ODM_RTL8821C | ODM_RTL8822B | ODM_RTL8814A))
			odm_set_bb_reg(dm, R_0x8a0, BIT(1) | BIT(0), 0);
	}
}

void phydm_set_pwdb_mode(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (dm->support_ic_type & ODM_RTL8822B)
			odm_set_bb_reg(dm, R_0x8dc, BIT(5), 0x1);
		else if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F))
			odm_set_bb_reg(dm, R_0xce8, BIT(13), 0x1);
		else if (dm->support_ic_type & ODM_IC_JGR3_SERIES)
			odm_set_bb_reg(dm, R_0x844, BIT(30) | BIT(29), 0x0);
	}
}

void phydm_adaptivity_debug(void *dm_void, char input[][16], u32 *_used,
			    char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adaptivity = &dm->adaptivity;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char help[] = "-h";
	u32 dm_value[10] = {0};
	u8 i = 0, input_idx = 0;
	u32 reg_value32 = 0;
	s8 h2l_diff = 0;

	for (i = 0; i < 5; i++) {
		if (input[i + 1]) {
			PHYDM_SSCANF(input[i + 1], DCMD_HEX, &dm_value[i]);
			input_idx++;
		}
	}
	if (strcmp(input[1], help) == 0) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Show adaptivity message: {0}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Enter debug mode: {1} {th_l2h_ini} {th_edcca_hl_diff}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Leave debug mode: {2}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable EDCCA thr: {3}\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Enable EDCCA thr: {4}\n");
		goto out;
	}

	if (input_idx == 0)
		return;

	if (dm_value[0] == PHYDM_ADAPT_DEBUG) {
		adaptivity->debug_mode = true;
		if (dm_value[1] != 0)
			dm->th_l2h_ini = (s8)dm_value[1];
		if (dm_value[2] != 0)
			dm->th_edcca_hl_diff = (s8)dm_value[2];
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "th_l2h_ini = %d, th_edcca_hl_diff = %d\n",
			 dm->th_l2h_ini, dm->th_edcca_hl_diff);
	} else if (dm_value[0] == PHYDM_ADAPT_RESUME) {
		adaptivity->debug_mode = false;
		dm->th_l2h_ini = adaptivity->th_l2h_ini_backup;
		dm->th_edcca_hl_diff = adaptivity->th_edcca_hl_diff_backup;
	} else if (dm_value[0] == PHYDM_EDCCA_TH_PAUSE) {
		adaptivity->edcca_en = false;
	} else if (dm_value[0] == PHYDM_EDCCA_TH_RESUME) {
		adaptivity->edcca_en = true;
	} else if (dm_value[0] == PHYDM_ADAPT_MSG) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "debug_mode = %s, th_l2h_ini = %d\n",
			 (adaptivity->debug_mode ? "TRUE" : "FALSE"),
			 dm->th_l2h_ini);
		if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
			reg_value32 = odm_get_bb_reg(dm, R_0x84c, MASKDWORD);
			h2l_diff = (s8)((0x00ff0000 & reg_value32) >> 16) -
				   (s8)((0xff000000 & reg_value32) >> 24);
		} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
			reg_value32 = odm_get_bb_reg(dm, R_0xc4c, MASKDWORD);
			h2l_diff = (s8)(0x000000ff & reg_value32) -
				   (s8)((0x00ff0000 & reg_value32) >> 16);
		} else if (dm->support_ic_type & ODM_IC_11AC_SERIES) {
			reg_value32 = odm_get_bb_reg(dm, R_0x8a4, MASKDWORD);
			h2l_diff = (s8)(0x000000ff & reg_value32) -
				   (s8)((0x0000ff00 & reg_value32) >> 8);
		}

		if (h2l_diff == 7)
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "adaptivity enable\n");
		else
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "adaptivity disable\n");
	}

out:
	*_used = used;
	*_out_len = out_len;
}

void phydm_set_edcca_val(void *dm_void, u32 *val_buf, u8 val_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;

	if (val_len != 2) {
		PHYDM_DBG(dm, ODM_COMP_API,
			  "[Error][adaptivity]Need val_len = 2\n");
		return;
	}
	phydm_set_edcca_threshold(dm, (s8)val_buf[1], (s8)val_buf[0]);
}

boolean phydm_edcca_abort(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void *adapter = dm->adapter;
	u32 is_fw_in_psmode = false;
#endif

	if (dm->pause_ability & ODM_BB_ADAPTIVITY) {
		PHYDM_DBG(dm, DBG_ADPTVTY, "Return: Pause ADPTVTY in LV=%d\n",
			  dm->pause_lv_table.lv_adapt);
		return true;
	}

	if (!adapt->edcca_en) {
		PHYDM_DBG(dm, DBG_ADPTVTY, "Disable EDCCA!!!\n");
		return true;
	}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	((PADAPTER)adapter)->HalFunc.GetHwRegHandler(adapter,
						      HW_VAR_FW_PSMODE_STATUS,
						      (u8 *)(&is_fw_in_psmode));

	/*@Disable EDCCA while under LPS mode, added by Roger, 2012.09.14.*/
	if (is_fw_in_psmode)
		return true;
#endif

	return false;
}
#endif
void phydm_set_edcca_threshold_api(void *dm_void, u8 IGI)
{
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adaptivity = &dm->adaptivity;
	s8 th_l2h_dmc = 0, th_h2l_dmc = 0;
	s8 diff = 0, igi_target = 0x32;

	if (dm->support_ability & ODM_BB_ADAPTIVITY) {
		if (!(dm->support_ic_type & ODM_IC_PWDB_EDCCA)) {
			if (adaptivity->adjust_l2h > IGI)
				diff = adaptivity->adjust_l2h - IGI;

			th_l2h_dmc = dm->th_l2h_ini - diff + igi_target;
			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
		} else {
			diff = igi_target - (s8)IGI;
			th_l2h_dmc = dm->th_l2h_ini + diff;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;

			/*replace lower bound to prevent EDCCA always equal 1*/
			if (th_h2l_dmc < adaptivity->h2l_lb)
				th_h2l_dmc = adaptivity->h2l_lb;
			if (th_l2h_dmc < adaptivity->l2h_lb)
				th_l2h_dmc = adaptivity->l2h_lb;
		}

		PHYDM_DBG(dm, DBG_ADPTVTY,
			  "API :IGI=0x%x, th_l2h_dmc = %d, th_h2l_dmc = %d\n",
			  IGI, th_l2h_dmc, th_h2l_dmc);

		phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);
	}
#endif
}

void phydm_adaptivity_info_init(void *dm_void, enum phydm_adapinfo cmn_info,
				u32 value)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adaptivity = &dm->adaptivity;

	switch (cmn_info) {
	case PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE:
		dm->carrier_sense_enable = (boolean)value;
		break;
	case PHYDM_ADAPINFO_TH_L2H_INI:
		dm->th_l2h_ini = (s8)value;
		break;
	case PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF:
		dm->th_edcca_hl_diff = (s8)value;
		break;
	case PHYDM_ADAPINFO_AP_NUM_TH:
		adaptivity->ap_num_th = (u8)value;
		break;
	default:
		break;
	}
}

void phydm_adaptivity_info_update(void *dm_void, enum phydm_adapinfo cmn_info,
				  u32 value)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;

	/*This init variable may be changed in run time.*/
	switch (cmn_info) {
	case PHYDM_ADAPINFO_DOMAIN_CODE_2G:
		adapt->regulation_2g = (u8)value;
		break;
	case PHYDM_ADAPINFO_DOMAIN_CODE_5G:
		adapt->regulation_5g = (u8)value;
		break;
	default:
		break;
	}
}

void phydm_adaptivity_init(void *dm_void)
{
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_adaptivity_struct *adaptivity = &dm->adaptivity;

#if (DM_ODM_SUPPORT_TYPE & (ODM_CE | ODM_WIN))

	if (!dm->carrier_sense_enable) {
		if (dm->th_l2h_ini == 0)
			phydm_set_l2h_th_ini(dm);
	} else {
		phydm_set_l2h_th_ini_carrier_sense(dm);
	}

	if (dm->th_edcca_hl_diff == 0)
		dm->th_edcca_hl_diff = 7;
#if (DM_ODM_SUPPORT_TYPE & (ODM_CE))
	if (dm->wifi_test || *dm->mp_mode)
#else
	if (dm->wifi_test & RT_WIFI_LOGO) /*@AP side use mib control*/
#endif
		/*@even no adaptivity, we still enable EDCCA*/
		adaptivity->edcca_en = false;
	else
		adaptivity->edcca_en = true;

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)
	if (dm->carrier_sense_enable) {
		phydm_set_l2h_th_ini_carrier_sense(dm);
		dm->th_edcca_hl_diff = 7;
	} else {
		dm->th_l2h_ini = dm->TH_L2H_default; /*set by mib*/
		dm->th_edcca_hl_diff = dm->th_edcca_hl_diff_default;
	}

	adaptivity->edcca_en = true;
#endif

	adaptivity->is_adapt_en = false; /*@decide enable or not*/
	adaptivity->debug_mode = false;
	adaptivity->th_l2h_ini_backup = dm->th_l2h_ini;
	adaptivity->th_edcca_hl_diff_backup = dm->th_edcca_hl_diff;
	adaptivity->igi_base = 0x32;
	adaptivity->adapt_igi_up = 0;
	adaptivity->h2l_lb = 0;
	adaptivity->l2h_lb = 0;
	adaptivity->adjust_l2h = 0;
	adaptivity->th_l2h = 0x7f;
	adaptivity->th_h2l = 0x7f;
	phydm_mac_edcca_state(dm, PHYDM_DONT_IGNORE_EDCCA);

	if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		adaptivity->adaptivity_dbg_port = 0x000;
		odm_set_bb_reg(dm, R_0x1d6c, BIT(0), 1);
	} else if (dm->support_ic_type & ODM_IC_11N_SERIES) {
		adaptivity->adaptivity_dbg_port = 0x208;
	} else {
		adaptivity->adaptivity_dbg_port = 0x209;
	}
	if (dm->support_ic_type & ODM_IC_11N_SERIES &&
	    !(dm->support_ic_type & ODM_IC_PWDB_EDCCA)) {
		/*@interfernce need > 2^x us, and then EDCCA will be 1*/
#if 0
		/*odm_set_bb_reg(dm, 0x948, 0x1c00, 0x7);*/
#endif
		if (dm->support_ic_type & (ODM_RTL8197F | ODM_RTL8192F)) {
			/*set to page B1*/
			odm_set_bb_reg(dm, R_0xe28, BIT(30), 0x1);
			/*@0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
			odm_set_bb_reg(dm, R_0xbc0, BIT(27) | BIT(26), 0x1);
			odm_set_bb_reg(dm, R_0xe28, BIT(30), 0x0);
		} else {
			/*@0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
			odm_set_bb_reg(dm, R_0xe24, BIT(21) | BIT(20), 0x1);
		}
	} else if (dm->support_ic_type & ODM_IC_11AC_SERIES &&
		   !(dm->support_ic_type & ODM_IC_PWDB_EDCCA)) {
		/*@interfernce need > 2^x us, and then EDCCA will be 1*/
#if 0
		/*odm_set_bb_reg(dm, 0x900, 0x70000000, 0x7);*/
#endif
		/*@0:rx_dfir, 1: dcnf_out, 2 :rx_iq, 3: rx_nbi_nf_out*/
		odm_set_bb_reg(dm, R_0x944, BIT(29) | BIT(28), 0x1);
	}

	if (dm->support_ic_type & ODM_IC_PWDB_EDCCA) {
		phydm_search_pwdb_lower_bound(dm);
		if (phydm_re_search_condition(dm))
			phydm_search_pwdb_lower_bound(dm);
	} else {
		/*resume to no link state*/
		phydm_set_edcca_threshold(dm, 0x7f, 0x7f);
	}

	/*@forgetting factor setting*/
	phydm_set_forgetting_factor(dm);

	/*pwdb mode setting with 0: mean, 1:max*/
	phydm_set_pwdb_mode(dm);

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	adaptivity->igi_up_bound_lmt_val = 180;
#else
	adaptivity->igi_up_bound_lmt_val = 90;
#endif
	adaptivity->igi_up_bound_lmt_cnt = 0;
	adaptivity->igi_lmt_en = false;
#endif
}

void phydm_adaptivity(void *dm_void)
{
#ifdef PHYDM_SUPPORT_ADAPTIVITY
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct phydm_dig_struct *dig_t = &dm->dm_dig_table;
	struct phydm_adaptivity_struct *adapt = &dm->adaptivity;
	u8 igi = dig_t->cur_ig_value;
	s8 th_l2h_dmc = 0, th_h2l_dmc = 0;
	s8 diff = 0, igi_target = adapt->igi_base;

	if (phydm_edcca_abort(dm))
		return;

	/*@fix AC series when enable EDCCA hang issue*/
	if (dm->support_ic_type & ODM_RTL8812) {
		odm_set_bb_reg(dm, R_0x800, BIT(10), 1); /*@ADC_mask disable*/
		odm_set_bb_reg(dm, R_0x800, BIT(10), 0); /*@ADC_mask enable*/
	}

	if (!adapt->debug_mode)
		phydm_check_adaptivity(dm); /*@Check adaptivity enable*/

	PHYDM_DBG(dm, DBG_ADPTVTY, "%s ====>\n", __func__);
	PHYDM_DBG(dm, DBG_ADPTVTY, "th_l2h_ini = %d, th_edcca_hl_diff = %d\n",
		  dm->th_l2h_ini, dm->th_edcca_hl_diff);
	PHYDM_DBG(dm, DBG_ADPTVTY, "is_adapt_en = %d, debug_mode = %d\n",
		  adapt->is_adapt_en, adapt->debug_mode);

	if (dm->support_ic_type & ODM_IC_PWDB_EDCCA) {
		if (adapt->is_adapt_en) {
			/*@Limit IGI upper bound for adaptivity*/
			phydm_dig_up_bound_lmt_en(dm);
			diff = igi_target - (s8)igi;
			th_l2h_dmc = dm->th_l2h_ini + diff;
			if (th_l2h_dmc > 10)
				th_l2h_dmc = 10;

			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
		} else {
			th_l2h_dmc = 0x46 - igi;
			th_h2l_dmc = th_l2h_dmc - EDCCA_HL_DIFF_NORMAL;
		}
		/*replace lower bound to prevent EDCCA always equal 1*/
		if (th_h2l_dmc < adapt->h2l_lb)
			th_h2l_dmc = adapt->h2l_lb;
		if (th_l2h_dmc < adapt->l2h_lb)
			th_l2h_dmc = adapt->l2h_lb;
		PHYDM_DBG(dm, DBG_ADPTVTY,
			  "adapt_igi_up=0x%x, h2l_lb = 0x%x, l2h_lb = 0x%x\n",
			  adapt->adapt_igi_up, adapt->h2l_lb, adapt->l2h_lb);
	} else if (dm->support_ic_type & ODM_IC_JGR3_SERIES) {
		if (adapt->is_adapt_en) {
			/*need to prevent pwdB clipping*/
			adapt->adjust_l2h = (u8)(dm->th_l2h_ini - ADC_BACKOFF);
			diff = adapt->adjust_l2h > igi ?
			       adapt->adjust_l2h - igi :
			       0;
			th_l2h_dmc = dm->th_l2h_ini - diff;
			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
		} else {
			th_l2h_dmc = igi + TH_L2H_DIFF_IGI > EDCCA_TH_L2H_LB ?
				     igi + TH_L2H_DIFF_IGI :
				     EDCCA_TH_L2H_LB;
			th_h2l_dmc = th_l2h_dmc - EDCCA_HL_DIFF_NORMAL;
		}
	} else {
		if (adapt->is_adapt_en) {
			/*need to consider PwdB upper bound for 8814 later IC*/
			adapt->adjust_l2h = (u8)(dm->th_l2h_ini + igi_target -
					    PWDB_UPPER_BOUND + DFIR_LOSS);
			diff = adapt->adjust_l2h > igi ?
			       adapt->adjust_l2h - igi :
			       0;
			th_l2h_dmc = dm->th_l2h_ini - diff + igi_target;
			th_h2l_dmc = th_l2h_dmc - dm->th_edcca_hl_diff;
		} else {
			th_l2h_dmc = igi + TH_L2H_DIFF_IGI > EDCCA_TH_L2H_LB ?
				     igi + TH_L2H_DIFF_IGI :
				     EDCCA_TH_L2H_LB;
			th_h2l_dmc = th_l2h_dmc - EDCCA_HL_DIFF_NORMAL;
		}
	}

	adapt->th_l2h = th_l2h_dmc;
	adapt->th_h2l = th_h2l_dmc;
	PHYDM_DBG(dm, DBG_ADPTVTY, "IGI=0x%x, th_l2h_dmc=%d, th_h2l_dmc=%d\n",
		  igi, th_l2h_dmc, th_h2l_dmc);
	phydm_set_edcca_threshold(dm, th_h2l_dmc, th_l2h_dmc);

	if (adapt->is_adapt_en)
		odm_set_mac_reg(dm, REG_RD_CTRL, BIT(11), 1);

	return;
#endif
}
