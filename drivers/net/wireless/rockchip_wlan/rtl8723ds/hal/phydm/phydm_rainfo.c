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
phydm_h2C_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			h2c_parameter[H2C_MAX_LENGTH] = {0};
	u8			phydm_h2c_id = (u8)dm_value[0];
	u8			i;
	u32			used = *_used;
	u32			out_len = *_out_len;

	PHYDM_SNPRINTF((output + used, out_len - used, "Phydm Send H2C_ID (( 0x%x))\n", phydm_h2c_id));
	for (i = 0; i < H2C_MAX_LENGTH; i++) {

		h2c_parameter[i] = (u8)dm_value[i + 1];
		PHYDM_SNPRINTF((output + used, out_len - used, "H2C: Byte[%d] = ((0x%x))\n", i, h2c_parameter[i]));
	}

	odm_fill_h2c_cmd(p_dm, phydm_h2c_id, H2C_MAX_LENGTH, h2c_parameter);
	
	*_used = used;
	*_out_len = out_len;
}

void
phydm_fw_fix_rate(
	void		*p_dm_void,
	u8		en, 
	u8		macid, 
	u8		bw, 
	u8		rate
	
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32	reg_u32_tmp;

	if (p_dm->support_ic_type & PHYDM_IC_8051_SERIES) {
		
		reg_u32_tmp = (bw << 24) | (rate << 16) | (macid << 8) | en;
		odm_set_bb_reg(p_dm, 0x4a0, MASKDWORD, reg_u32_tmp);
			
	} else {
	
		if (en == 1)
			reg_u32_tmp = (0x60 << 24) | (macid << 16) | (bw << 8) | rate;
		else
			reg_u32_tmp = 0x40000000;
			
		odm_set_bb_reg(p_dm, 0x450, MASKDWORD, reg_u32_tmp);
	}
	if (en == 1) {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("FW fix TX rate[id =%d], %dM, Rate(%d)=", macid, (20 << bw), rate));
		phydm_print_rate(p_dm, rate, ODM_COMP_API);
	} else {
		PHYDM_DBG(p_dm, ODM_COMP_API, ("Auto Rate\n"));
	}
}

void
phydm_ra_debug(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u32 used = *_used;
	u32 out_len = *_out_len;
	char	help[] = "-h";
	u32	var1[5] = {0};
	u8	i = 0;
	u32	reg_u32_tmp;

	for (i = 0; i < 5; i++) {
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 1], DCMD_DECIMAL, &var1[i]);
	}
	
	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "{1} {0:-,1:+} {ofst}: set offset\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "{1} {100}: show offset\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "{2} {en} {macid} {bw} {rate}: fw fix rate\n"));
		
	} else if (var1[0] == 1) { /*Adjust PCR offset*/

		if (var1[1] == 100) {
			PHYDM_SNPRINTF((output + used, out_len - used, "[Get] RA_ofst=((%s%d))\n", 
				((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

		} else if (var1[1] == 0) {
			p_ra_table->RA_offset_direction = 0;
			p_ra_table->RA_threshold_offset = (u8)var1[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[Set] RA_ofst=((-%d))\n", p_ra_table->RA_threshold_offset));
		} else if (var1[1] == 1) {
			p_ra_table->RA_offset_direction = 1;
			p_ra_table->RA_threshold_offset = (u8)var1[2];
			PHYDM_SNPRINTF((output + used, out_len - used, "[Set] RA_ofst=((+%d))\n", p_ra_table->RA_threshold_offset));
		}
		
	} else if (var1[0] == 2) { /*FW fix rate*/

		PHYDM_SNPRINTF((output + used, out_len - used, 
			"[FW fix TX Rate] {en, macid,bw,rate}={%d, %d, %d, 0x%x}", var1[1], var1[2], var1[3], var1[4]));
		
		phydm_fw_fix_rate(p_dm, (u8)var1[1], (u8)var1[2], (u8)var1[3], (u8)var1[4]);
		
	} else {
		PHYDM_SNPRINTF((output + used, out_len - used, "[Set] Error\n"));
		/**/
	}
	*_used = used;
	*_out_len = out_len;
}



void
odm_c2h_ra_para_report_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
#if (defined(CONFIG_RA_DBG_CMD))
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
#endif

	u8	para_idx = cmd_buf[0]; /*Retry Penalty, NH, NL*/
#if (defined(CONFIG_RA_DBG_CMD))
	u8	rate_type_start = cmd_buf[1];
	u8	rate_type_length = cmd_len - 2;
#endif
	u8	i;


	PHYDM_DBG(p_dm, DBG_RA, ("[ From FW C2H RA Para ]  cmd_buf[0]= (( %d ))\n", cmd_buf[0]));

#if (defined(CONFIG_RA_DBG_CMD))
	if (para_idx == RADBG_RTY_PENALTY) {
		PHYDM_DBG(p_dm, DBG_RA, (" |rate index|   |RTY Penality index|\n"));

		for (i = 0 ; i < (rate_type_length) ; i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RTY_P_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RTY_P[rate_type_start + i] = cmd_buf[2 + i];
			PHYDM_DBG(p_dm, DBG_RA, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RTY_P[rate_type_start + i]));
		}

	} else	if (para_idx == RADBG_N_HIGH) {
		/**/
		PHYDM_DBG(p_dm, DBG_RA, (" |rate index|    |N-High|\n"));


	} else if (para_idx == RADBG_N_LOW) {
		PHYDM_DBG(p_dm, DBG_RA, (" |rate index|   |N-Low|\n"));
		/**/
	} else if (para_idx == RADBG_RATE_UP_RTY_RATIO) {
		PHYDM_DBG(p_dm, DBG_RA, (" |rate index|   |rate Up RTY Ratio|\n"));

		for (i = 0; i < (rate_type_length); i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RATE_UP_RTY_RATIO_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RATE_UP_RTY_RATIO[rate_type_start + i] = cmd_buf[2 + i];
			PHYDM_DBG(p_dm, DBG_RA, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RATE_UP_RTY_RATIO[rate_type_start + i]));
		}
	} else	 if (para_idx == RADBG_RATE_DOWN_RTY_RATIO) {
		PHYDM_DBG(p_dm, DBG_RA, (" |rate index|   |rate Down RTY Ratio|\n"));

		for (i = 0; i < (rate_type_length); i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RATE_DOWN_RTY_RATIO_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RATE_DOWN_RTY_RATIO[rate_type_start + i] = cmd_buf[2 + i];
			PHYDM_DBG(p_dm, DBG_RA, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RATE_DOWN_RTY_RATIO[rate_type_start + i]));
		}
	} else
#endif
		if (para_idx == RADBG_DEBUG_MONITOR1) {
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("-------------------------------\n"));
			if (p_dm->support_ic_type & PHYDM_IC_3081_SERIES) {

				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "RSSI =", cmd_buf[1]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "rate =", cmd_buf[2] & 0x7f));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "SGI =", (cmd_buf[2] & 0x80) >> 7));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "BW =", cmd_buf[3]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "BW_max =", cmd_buf[4]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "multi_rate0 =", cmd_buf[5]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "multi_rate1 =", cmd_buf[6]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "DISRA =",	cmd_buf[7]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "VHT_EN =", cmd_buf[8]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "SGI_support =",	cmd_buf[9]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "try_ness =", cmd_buf[10]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "pre_rate =", cmd_buf[11]));
			} else {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "RSSI =", cmd_buf[1]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %x\n", "BW =", cmd_buf[2]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "DISRA =", cmd_buf[3]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "VHT_EN =", cmd_buf[4]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "Hightest rate =", cmd_buf[5]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "Lowest rate =", cmd_buf[6]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "SGI_support =", cmd_buf[7]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "Rate_ID =",	cmd_buf[8]));
			}
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("-------------------------------\n"));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR2) {
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("-------------------------------\n"));
			if (p_dm->support_ic_type & PHYDM_IC_3081_SERIES) {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "rate_id =", cmd_buf[1]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "highest_rate =", cmd_buf[2]));
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "lowest_rate =", cmd_buf[3]));

				for (i = 4; i <= 11; i++)
					PHYDM_DBG(p_dm, DBG_FW_TRACE, ("RAMASK =  0x%x\n", cmd_buf[i]));
			} else {
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %x%x  %x%x  %x%x  %x%x\n", "RA Mask:",
					cmd_buf[8], cmd_buf[7], cmd_buf[6], cmd_buf[5], cmd_buf[4], cmd_buf[3], cmd_buf[2], cmd_buf[1]));
			}
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("-------------------------------\n"));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR3) {

			for (i = 0; i < (cmd_len - 1); i++)
				PHYDM_DBG(p_dm, DBG_FW_TRACE, ("content[%d] = %d\n", i, cmd_buf[1 + i]));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR4)
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  {%d.%d}\n", "RA version =", cmd_buf[1], cmd_buf[2]));
		else if (para_idx == RADBG_DEBUG_MONITOR5) {
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "Current rate =", cmd_buf[1]));
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "Retry ratio =", cmd_buf[2]));
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  %d\n", "rate down ratio =", cmd_buf[3]));
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x\n", "highest rate =", cmd_buf[4]));
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  {0x%x 0x%x}\n", "Muti-try =", cmd_buf[5], cmd_buf[6]));
			PHYDM_DBG(p_dm, DBG_FW_TRACE, ("%5s  0x%x%x%x%x%x\n", "RA mask =", cmd_buf[11], cmd_buf[10], cmd_buf[9], cmd_buf[8], cmd_buf[7]));
		}
}

void
phydm_ra_dynamic_retry_count(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm->support_ability & ODM_BB_DYNAMIC_ARFR))
		return;

	/*PHYDM_DBG(p_dm, DBG_RA, ("p_dm->pre_b_noisy = %d\n", p_dm->pre_b_noisy ));*/
	if (p_dm->pre_b_noisy != p_dm->noisy_decision) {

		if (p_dm->noisy_decision) {
			PHYDM_DBG(p_dm, DBG_RA, ("Noisy Env. RA fallback\n"));
			odm_set_mac_reg(p_dm, 0x430, MASKDWORD, 0x0);
			odm_set_mac_reg(p_dm, 0x434, MASKDWORD, 0x04030201);
		} else {
			PHYDM_DBG(p_dm, DBG_RA, ("Clean Env. RA fallback\n"));
			odm_set_mac_reg(p_dm, 0x430, MASKDWORD, 0x01000000);
			odm_set_mac_reg(p_dm, 0x434, MASKDWORD, 0x06050402);
		}
		p_dm->pre_b_noisy = p_dm->noisy_decision;
	}
}

void
phydm_print_rate(
	void	*p_dm_void,
	u8	rate,
	u32	dbg_component
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		legacy_table[12] = {1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54};
	u8		rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8		vht_en = (rate_idx >= ODM_RATEVHTSS1MCS0) ? 1 : 0;
	u8		b_sgi = (rate & 0x80) >> 7;

	PHYDM_DBG_F(p_dm, dbg_component, ("( %s%s%s%s%d%s%s)\n",
		((rate_idx >= ODM_RATEVHTSS1MCS0) && (rate_idx <= ODM_RATEVHTSS1MCS9)) ? "VHT 1ss  " : "",
		((rate_idx >= ODM_RATEVHTSS2MCS0) && (rate_idx <= ODM_RATEVHTSS2MCS9)) ? "VHT 2ss " : "",
		((rate_idx >= ODM_RATEVHTSS3MCS0) && (rate_idx <= ODM_RATEVHTSS3MCS9)) ? "VHT 3ss " : "",
			(rate_idx >= ODM_RATEMCS0) ? "MCS " : "",
		(vht_en) ? ((rate_idx - ODM_RATEVHTSS1MCS0) % 10) : ((rate_idx >= ODM_RATEMCS0) ? (rate_idx - ODM_RATEMCS0) : ((rate_idx <= ODM_RATE54M) ? legacy_table[rate_idx] : 0)),
			(b_sgi) ? "-S" : "  ",
			(rate_idx >= ODM_RATEMCS0) ? "" : "M"));
}

void
phydm_c2h_ra_report_handler(
	void	*p_dm_void,
	u8   *cmd_buf,
	u8   cmd_len
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;
	u8	macid = cmd_buf[1];
	u8	rate = cmd_buf[0];
	u8	rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8	rate_order;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];

	if (cmd_len >=6) {
		p_ra_table->ra_ratio[macid] = cmd_buf[6];
		PHYDM_DBG(p_dm, DBG_RA, ("RA retry ratio: [%d]:", p_ra_table->ra_ratio[macid]));
			/**/
	} else if (cmd_len >= 4) {
		if (cmd_buf[3] == 0) {
			PHYDM_DBG(p_dm, DBG_RA, ("TX Init-rate Update[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 0xff) {
			PHYDM_DBG(p_dm, DBG_RA, ("FW Level: Fix rate[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 1) {
			PHYDM_DBG(p_dm, DBG_RA, ("Try Success[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 2) {
			PHYDM_DBG(p_dm, DBG_RA, ("Try Fail & Try Again[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 3) {
			PHYDM_DBG(p_dm, DBG_RA, ("rate Back[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 4) {
			PHYDM_DBG(p_dm, DBG_RA, ("start rate by RSSI[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 5) {
			PHYDM_DBG(p_dm, DBG_RA, ("Try rate[%d]:", macid));
			/**/
		}
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("Tx rate Update[%d]:", macid));
		/**/
	}

	/*phydm_print_rate(p_dm, pre_rate_idx, DBG_RA);*/
	/*PHYDM_DBG(p_dm, DBG_RA, (">\n",macid );*/
	phydm_print_rate(p_dm, rate, DBG_RA);
	if (macid >= 128) {
		u8 gid_index = macid - 128;
		p_ra_table->mu1_rate[gid_index] = rate;
	}
	
	/*p_ra_table->link_tx_rate[macid] = rate;*/
		
	if (is_sta_active(p_sta)) {
		p_sta->ra_info.curr_tx_rate = rate;
		/**/
	}

	/*trigger power training*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

	rate_order = phydm_rate_order_compute(p_dm, rate_idx);

	if ((p_dm->is_one_entry_only) ||
		((rate_order > p_ra_table->highest_client_tx_order) && (p_ra_table->power_tracking_flag == 1))
		) {
		halrf_update_pwr_track(p_dm, rate_idx);
		p_ra_table->power_tracking_flag = 0;
	}

#endif

	/*trigger dynamic rate ID*/
/*#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))*/	/*dino will refine here later*/
#if 0
	if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E))
		phydm_update_rate_id(p_dm, rate, macid);
#endif

}

void
odm_ra_post_action_on_assoc(
	void	*p_dm_void
)
{
#if 0
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	p_dm->h2c_rarpt_connect = 1;
	phydm_rssi_monitor_check(p_dm);
	p_dm->h2c_rarpt_connect = 0;
#endif
}

void
phydm_modify_RA_PCR_threshold(
	void		*p_dm_void,
	u8		RA_offset_direction,
	u8		RA_threshold_offset

)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;

	p_ra_table->RA_offset_direction = RA_offset_direction;
	p_ra_table->RA_threshold_offset = RA_threshold_offset;
	PHYDM_DBG(p_dm, DBG_RA, ("Set RA_threshold_offset = (( %s%d ))\n", ((RA_threshold_offset == 0) ? " " : ((RA_offset_direction) ? "+" : "-")), RA_threshold_offset));
}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void
odm_refresh_rate_adaptive_mask_mp(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT				*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_					*p_ra_table = &p_dm->dm_ra_table;
	struct _ADAPTER				*p_adapter	 =  p_dm->adapter;
	struct _ADAPTER				*p_target_adapter = NULL;
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(p_adapter);
	PMGNT_INFO				p_mgnt_info = GetDefaultMgntInfo(p_adapter);
	struct _ADAPTER			*p_loop_adapter = GetDefaultAdapter(p_adapter);
	PMGNT_INFO					p_loop_mgnt_info = &(p_loop_adapter->MgntInfo);
	HAL_DATA_TYPE				*p_loop_hal_data = GET_HAL_DATA(p_loop_adapter);
	
	u32		i;
	struct sta_info *p_entry;
	u8		ratr_state_new;

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("%s ======>\n", __func__));

	if (p_adapter->bDriverStopped) {
		PHYDM_DBG(p_dm, DBG_RA_MASK, ("driver is going to unload\n"));
		return;
	}

	if (!p_hal_data->bUseRAMask) {
		PHYDM_DBG(p_dm, DBG_RA_MASK, ("driver does not control rate adaptive mask\n"));
		return;
	}

	/* if default port is connected, update RA table for default port (infrastructure mode only) */
	/* Need to consider other ports for P2P cases*/

	while(p_loop_adapter){

		p_loop_mgnt_info = &(p_loop_adapter->MgntInfo);
		p_loop_hal_data = GET_HAL_DATA(p_loop_adapter);
	
		if (p_loop_mgnt_info->mAssoc && (!ACTING_AS_AP(p_loop_adapter))) {
			odm_refresh_ldpc_rts_mp(p_loop_adapter, p_dm, p_loop_mgnt_info->mMacId, p_loop_mgnt_info->IOTPeer, p_loop_hal_data->UndecoratedSmoothedPWDB);
		/*PHYDM_DBG(p_dm, DBG_RA_MASK, ("Infrasture mode\n"));*/

			ratr_state_new = phydm_rssi_lv_dec(p_dm, p_loop_hal_data->UndecoratedSmoothedPWDB, p_loop_mgnt_info->Ratr_State);

			if ((p_loop_mgnt_info->Ratr_State != ratr_state_new) || (p_ra_table->up_ramask_cnt >= FORCED_UPDATE_RAMASK_PERIOD)) {

				p_ra_table->up_ramask_cnt = 0;
				PHYDM_PRINT_ADDR(p_dm, DBG_RA_MASK, ("Target AP addr :"), p_loop_mgnt_info->Bssid);
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("Update RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n\n",
					p_mgnt_info->Ratr_State, ratr_state_new, p_loop_hal_data->UndecoratedSmoothedPWDB));

				p_loop_mgnt_info->Ratr_State = ratr_state_new;
				p_adapter->HalFunc.UpdateHalRAMaskHandler(p_loop_adapter, p_loop_mgnt_info->mMacId, NULL, ratr_state_new);
			} else {
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
				/**/
			}
		}

		p_loop_adapter = GetNextExtAdapter(p_loop_adapter);
	}

	/*  */
	/* The following part configure AP/VWifi/IBSS rate adaptive mask. */
	/*  */

	if (p_mgnt_info->mIbss)	/* Target: AP/IBSS peer. */
		p_target_adapter = GetDefaultAdapter(p_adapter);
	else
		p_target_adapter = GetFirstAPAdapter(p_adapter);

	/* if extension port (softap) is started, updaet RA table for more than one clients associate */
	if (p_target_adapter != NULL) {
		for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {

			p_entry = AsocEntry_EnumStation(p_target_adapter, i);

			if (is_sta_active((&GET_STA_INFO(p_entry)))) {

				odm_refresh_ldpc_rts_mp(p_target_adapter, p_dm, GET_STA_INFO(p_entry).mac_id, p_entry->IOTPeer, GET_STA_INFO(p_entry).rssi_stat.rssi);

				ratr_state_new = phydm_rssi_lv_dec(p_dm, GET_STA_INFO(p_entry).rssi_stat.rssi, GET_STA_INFO(p_entry).ra_info.rssi_level);

				if ((GET_STA_INFO(p_entry).ra_info.rssi_level != ratr_state_new) || (p_ra_table->up_ramask_cnt >= FORCED_UPDATE_RAMASK_PERIOD)) {

					p_ra_table->up_ramask_cnt = 0;
					PHYDM_PRINT_ADDR(p_dm, DBG_RA_MASK, ("Target AP addr :"), GET_STA_INFO(p_entry).mac_addr);
					PHYDM_DBG(p_dm, DBG_RA_MASK, ("Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n",
						GET_STA_INFO(p_entry).ra_info.rssi_level, ratr_state_new,  GET_STA_INFO(p_entry).rssi_stat.rssi));

					GET_STA_INFO(p_entry).ra_info.rssi_level = ratr_state_new;
					p_adapter->HalFunc.UpdateHalRAMaskHandler(p_target_adapter, GET_STA_INFO(p_entry).mac_id, p_entry, ratr_state_new);
				} else {
					PHYDM_DBG(p_dm, DBG_RA_MASK, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
					/**/
				}

			}
		}
	}
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

void
odm_refresh_rate_adaptive_mask_ap(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	struct rtl8192cd_priv *priv = p_dm->priv;
	struct aid_obj *aidarray;
	u32		i;
	struct sta_info *p_entry;
	struct cmn_sta_info	*p_sta;
	u8		ratr_state_new;

	if (priv->up_time % 2)
		return;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_entry = p_dm->p_odm_sta_info[i];
		p_sta = p_dm->p_phydm_sta_info[i];

		if (is_sta_active(p_sta)) {

			#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
			aidarray = container_of(p_entry, struct aid_obj, station);
			priv = aidarray->priv;
			#endif

			if (!priv->pmib->dot11StationConfigEntry.autoRate)
				continue;

			ratr_state_new = phydm_rssi_lv_dec(p_dm, (u32)p_sta->rssi_stat.rssi, p_sta->ra_info.rssi_level);

			if ((p_sta->ra_info.rssi_level != ratr_state_new) || (p_ra_table->up_ramask_cnt >= FORCED_UPDATE_RAMASK_PERIOD)) {

				p_ra_table->up_ramask_cnt = 0;
				PHYDM_PRINT_ADDR(p_dm, DBG_RA_MASK, ("Target AP addr :"), p_sta->mac_addr);
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n", p_sta->ra_info.rssi_level, ratr_state_new, p_sta->rssi_stat.rssi));

				p_sta->ra_info.rssi_level = ratr_state_new;
				phydm_gen_ramask_h2c_AP(p_dm, priv, p_entry, p_sta->ra_info.rssi_level);
			} else {
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
				/**/
			}
		}
	}
}
#endif

void
phydm_rate_adaptive_mask_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO		p_mgnt_info = &p_dm->adapter->MgntInfo;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_dm->adapter);

	p_mgnt_info->Ratr_State = DM_RATR_STA_INIT;

	if (p_mgnt_info->DM_Type == dm_type_by_driver)
		p_hal_data->bUseRAMask = true;
	else
		p_hal_data->bUseRAMask = false;

#endif

	p_ra_t->ldpc_thres = 35;
	p_ra_t->up_ramask_cnt = 0;
	p_ra_t->up_ramask_cnt_tmp = 0;

}

void
phydm_refresh_rate_adaptive_mask(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("%s ======>\n", __func__));

	if (!(p_dm->support_ability & ODM_BB_RA_MASK)) {
		PHYDM_DBG(p_dm, DBG_RA_MASK, ("Return: Not support\n"));
		return;
	}

	if (!p_dm->is_linked)
		return;

	p_ra_t->up_ramask_cnt++;
	/*p_ra_t->up_ramask_cnt_tmp++;*/
	

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

	odm_refresh_rate_adaptive_mask_mp(p_dm);

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

	odm_refresh_rate_adaptive_mask_ap(p_dm);

#else /*(DM_ODM_SUPPORT_TYPE == ODM_CE)*/

	phydm_ra_mask_watchdog(p_dm);

#endif
	
}

void
phydm_show_sta_info(
	void		*p_dm_void,
	char		input[][16],
	u32		*_used,
	char		*output,
	u32		*_out_len,
	u32		input_num
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info		*p_sta = NULL;
	struct ra_sta_info			*p_ra = NULL;
	#ifdef CONFIG_BEAMFORMING
	struct bf_cmn_info		*p_bf = NULL;
	#endif
	char		help[] = "-h";
	u32		var1[10] = {0};
	u32		used = *_used;
	u32		out_len = *_out_len;
	u32		i, macid_start, macid_end;
	u8		tatal_sta_num = 0;

	PHYDM_SSCANF(input[1], DCMD_DECIMAL, &var1[0]);

	if ((strcmp(input[1], help) == 0)) {
		PHYDM_SNPRINTF((output + used, out_len - used, "All STA: {1}\n"));
		PHYDM_SNPRINTF((output + used, out_len - used, "STA[macid]: {2} {macid}\n"));
		return;
	} else if (var1[0] == 1) {
		macid_start = 0;
		macid_end = ODM_ASSOCIATE_ENTRY_NUM;
	} else if (var1[0] == 2) {
		macid_start = var1[1];
		macid_end = var1[1];
	} else {
		PHYDM_SNPRINTF((output + used, out_len - used, "Warning input value!\n"));
		return;
	}
		
	for (i = macid_start; i < macid_end; i++) {
		
		p_sta = p_dm->p_phydm_sta_info[i];


		if (!is_sta_active(p_sta))
			continue;

		p_ra = &(p_sta->ra_info);
		#ifdef CONFIG_BEAMFORMING
		p_bf = &(p_sta->bf_info);
		#endif

		tatal_sta_num++;

		PHYDM_SNPRINTF((output + used, out_len - used, "==[MACID: %d]============>\n", p_sta->mac_id));
		PHYDM_SNPRINTF((output + used, out_len - used, "AID:%d\n", p_sta->aid));
		PHYDM_SNPRINTF((output + used, out_len - used, "ADDR:%x-%x-%x-%x-%x-%x\n", 
		p_sta->mac_addr[5], p_sta->mac_addr[4], p_sta->mac_addr[3], p_sta->mac_addr[2], p_sta->mac_addr[1], p_sta->mac_addr[0]));
		PHYDM_SNPRINTF((output + used, out_len - used, "DM_ctrl:0x%x\n", p_sta->dm_ctrl));
		PHYDM_SNPRINTF((output + used, out_len - used, "BW:%d, MIMO_Type:0x%x\n", p_sta->bw_mode, p_sta->mimo_type));
		PHYDM_SNPRINTF((output + used, out_len - used, "STBC_en:%d, LDPC_en=%d\n", p_sta->stbc_en, p_sta->ldpc_en));

		/*[RSSI Info]*/
		PHYDM_SNPRINTF((output + used, out_len - used, "RSSI{All, OFDM, CCK}={%d, %d, %d}\n", 
			p_sta->rssi_stat.rssi, p_sta->rssi_stat.rssi_ofdm, p_sta->rssi_stat.rssi_cck));

		/*[RA Info]*/
		PHYDM_SNPRINTF((output + used, out_len - used, "Rate_ID:%d, RSSI_LV:%d, ra_bw:%d, SGI_en:%d\n", 
			p_ra->rate_id, p_ra->rssi_level, p_ra->ra_bw_mode, p_ra->is_support_sgi));

		PHYDM_SNPRINTF((output + used, out_len - used, "VHT_en:%d, Wireless_set=0x%x, sm_ps=%d\n", 
			p_ra->is_vht_enable, p_sta->support_wireless_set, p_sta->sm_ps));

		PHYDM_SNPRINTF((output + used, out_len - used, "Dis{RA, PT}={%d, %d}, TxRx:%d, Noisy:%d\n", 
			p_ra->disable_ra, p_ra->disable_pt, p_ra->txrx_state, p_ra->is_noisy));
		
		PHYDM_SNPRINTF((output + used, out_len - used, "TX{Rate, BW}={%d, %d}, RTY:%d\n", 
			p_ra->curr_tx_rate, p_ra->curr_tx_bw, p_ra->curr_retry_ratio));
	
		PHYDM_SNPRINTF((output + used, out_len - used, "RA_MAsk:0x%llx\n", p_ra->ramask));
		
		/*[TP]*/
		PHYDM_SNPRINTF((output + used, out_len - used, "TP{TX,RX}={%d, %d}\n", 
			p_sta->tx_moving_average_tp, p_sta->rx_moving_average_tp));

		#ifdef CONFIG_BEAMFORMING
		/*[Beamforming]*/
		PHYDM_SNPRINTF((output + used, out_len - used, "CAP{HT,VHT}={0x%x, 0x%x}\n", 
			p_bf->ht_beamform_cap, p_bf->vht_beamform_cap));
		PHYDM_SNPRINTF((output + used, out_len - used, "{p_aid,g_id}={0x%x, 0x%x}\n\n", 
			p_bf->p_aid, p_bf->g_id));
		#endif
	}

	if (tatal_sta_num == 0) {
		PHYDM_SNPRINTF((output + used, out_len - used, "No Linked STA\n"));
	}
	
	*_used = used;
	*_out_len = out_len;
}

#ifdef	PHYDM_3RD_REFORM_RA_MASK

u8
phydm_get_tx_stream_num(
	void		*p_dm_void,
	enum 	rf_type	mimo_type
	
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	tx_num = 1;

	if (mimo_type == RF_1T1R || mimo_type == RF_1T2R)
		tx_num = 1;
	else if (mimo_type == RF_2T2R || mimo_type == RF_2T3R  || mimo_type == RF_2T4R)
		tx_num = 2;
	else if (mimo_type == RF_3T3R || mimo_type == RF_3T4R)
		tx_num = 3;
	else if (mimo_type == RF_4T4R)
		tx_num = 4;
	else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warrning] no mimo_type is found\n"));
	}
	return tx_num;
}

u64
phydm_get_bb_mod_ra_mask(
	void		*p_dm_void,
	u8		macid
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info		*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info			*p_ra = NULL;
	enum channel_width		bw = 0;
	enum wireless_set			wireless_mode = 0;
	u8		tx_stream_num = 1;
	u8		rssi_lv = 0;
	u64		ra_mask_bitmap = 0;
	
	if (is_sta_active(p_sta)) {
		
		p_ra = &(p_sta->ra_info);
		bw = p_ra->ra_bw_mode;
		wireless_mode = p_sta->support_wireless_set;
		tx_stream_num = phydm_get_tx_stream_num(p_dm, p_sta->mimo_type);
		rssi_lv = p_ra->rssi_level;
		ra_mask_bitmap = p_ra->ramask;
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warning] %s invalid sta_info\n", __func__));
		return 0;
	}

	PHYDM_DBG(p_dm, DBG_RA, ("macid=%d ori_RA_Mask= 0x%llx\n", macid, ra_mask_bitmap));
	PHYDM_DBG(p_dm, DBG_RA, ("wireless_mode=0x%x, tx_stream_num=%d, BW=%d, MimoPs=%d, rssi_lv=%d\n",
		wireless_mode, tx_stream_num, bw, p_sta->sm_ps, rssi_lv));
	
	if (p_sta->sm_ps == SM_PS_STATIC) /*mimo_ps_enable*/
		tx_stream_num = 1;


	/*[Modify RA Mask by Wireless Mode]*/

	if (wireless_mode == WIRELESS_CCK)								/*B mode*/
		ra_mask_bitmap &= 0x0000000f;
	else if (wireless_mode == WIRELESS_OFDM)							/*G mode*/
		ra_mask_bitmap &= 0x00000ff0;
	else if (wireless_mode == (WIRELESS_CCK | WIRELESS_OFDM))			/*BG mode*/
		ra_mask_bitmap &= 0x00000ff5;
	else if (wireless_mode == (WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_HT)) {
																	/*N_2G*/
		if (tx_stream_num == 1) {
			if (bw == CHANNEL_WIDTH_40)
				ra_mask_bitmap &= 0x000ff015;
			else
				ra_mask_bitmap &= 0x000ff005;
		} else if (tx_stream_num == 2) {

			if (bw == CHANNEL_WIDTH_40)
				ra_mask_bitmap &= 0x0ffff015;
			else
				ra_mask_bitmap &= 0x0ffff005;
		} else if (tx_stream_num == 3)
			ra_mask_bitmap &= 0xffffff015;
	} else if (wireless_mode ==  (WIRELESS_OFDM | WIRELESS_HT)) {		/*N_5G*/
	
		if (tx_stream_num == 1) {
			if (bw == CHANNEL_WIDTH_40)
				ra_mask_bitmap &= 0x000ff030;
			else
				ra_mask_bitmap &= 0x000ff010;
		} else if (tx_stream_num == 2) {

			if (bw == CHANNEL_WIDTH_40)
				ra_mask_bitmap &= 0x0ffff030;
			else
				ra_mask_bitmap &= 0x0ffff010;
		} else if (tx_stream_num == 3)
			ra_mask_bitmap &= 0xffffff010;
	} else if (wireless_mode ==  (WIRELESS_CCK |WIRELESS_OFDM | WIRELESS_VHT)) {
																	/*AC_2G*/
		if (tx_stream_num == 1)
			ra_mask_bitmap &= 0x003ff015;
		else if (tx_stream_num == 2)
			ra_mask_bitmap &= 0xfffff015;
		else if (tx_stream_num == 3)
			ra_mask_bitmap &= 0x3fffffff010;
		

		if (bw == CHANNEL_WIDTH_20) {/* AC 20MHz doesn't support MCS9 */
			ra_mask_bitmap &= 0x1ff7fdfffff;
		}
	} else if (wireless_mode ==  (WIRELESS_OFDM | WIRELESS_VHT)) {		/*AC_5G*/
	
		if (tx_stream_num == 1)
			ra_mask_bitmap &= 0x003ff010;
		else if (tx_stream_num == 2)
			ra_mask_bitmap &= 0xfffff010;
		else  if (tx_stream_num == 3)
			ra_mask_bitmap &= 0x3fffffff010;

		if (bw == CHANNEL_WIDTH_20) /* AC 20MHz doesn't support MCS9 */
			ra_mask_bitmap &= 0x1ff7fdfffff;
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warrning] No RA mask is found\n"));
		/**/
	}
	
	PHYDM_DBG(p_dm, DBG_RA, ("Mod by mode=0x%llx\n", ra_mask_bitmap));

	
	/*[Modify RA Mask by RSSI level]*/
	if (wireless_mode != WIRELESS_CCK) {

		if (rssi_lv == 0)
			ra_mask_bitmap &=  0xffffffff;
		else if (rssi_lv == 1)
			ra_mask_bitmap &=  0xfffffff0;
		else if (rssi_lv == 2)
			ra_mask_bitmap &=  0xffffefe0;
		else if (rssi_lv == 3)
			ra_mask_bitmap &=  0xffffcfc0;
		else if (rssi_lv == 4)
			ra_mask_bitmap &=  0xffff8f80;
		else if (rssi_lv >= 5)
			ra_mask_bitmap &=  0xffff0f00;

	}
	PHYDM_DBG(p_dm, DBG_RA, ("Mod by RSSI=0x%llx\n", ra_mask_bitmap));

	return ra_mask_bitmap;
}

u8
phydm_get_rate_id(
	void			*p_dm_void,
	u8			macid
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info		*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info			*p_ra =NULL;
	enum channel_width		bw = 0;
	enum wireless_set			wireless_mode = 0;
	u8	tx_stream_num = 1;
	u8	rate_id_idx = PHYDM_BGN_20M_1SS;

	if (is_sta_active(p_sta)) {
		
		p_ra = &(p_sta->ra_info);
		bw = p_ra->ra_bw_mode;
		wireless_mode = p_sta->support_wireless_set;
		tx_stream_num = phydm_get_tx_stream_num(p_dm, p_sta->mimo_type);

	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warning] %s: invalid sta_info\n", __func__));
		return 0;
	}

	PHYDM_DBG(p_dm, DBG_RA, ("macid=%d, wireless_set=0x%x, tx_stream_num=%d, BW=0x%x\n",
			macid, wireless_mode, tx_stream_num, bw));

	if (wireless_mode == WIRELESS_CCK)								/*B mode*/
		rate_id_idx = PHYDM_B_20M;
	else if (wireless_mode ==  WIRELESS_OFDM)						/*G mode*/
		rate_id_idx = PHYDM_G;
	else if (wireless_mode ==  (WIRELESS_CCK | WIRELESS_OFDM))			/*BG mode*/
		rate_id_idx = PHYDM_BG;
	else if (wireless_mode ==  (WIRELESS_OFDM | WIRELESS_HT)) {		/*GN mode*/
	
		if (tx_stream_num == 1)
			rate_id_idx = PHYDM_GN_N1SS;
		else if (tx_stream_num == 2)
			rate_id_idx = PHYDM_GN_N2SS;
		else if (tx_stream_num == 3)
			rate_id_idx = PHYDM_ARFR5_N_3SS;
	} else if (wireless_mode == (WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_HT)) {	/*BGN mode*/
	

		if (bw == CHANNEL_WIDTH_40) {

			if (tx_stream_num == 1)
				rate_id_idx = PHYDM_BGN_40M_1SS;
			else if (tx_stream_num == 2)
				rate_id_idx = PHYDM_BGN_40M_2SS;
			else if (tx_stream_num == 3)
				rate_id_idx = PHYDM_ARFR5_N_3SS;

		} else {

			if (tx_stream_num == 1)
				rate_id_idx = PHYDM_BGN_20M_1SS;
			else if (tx_stream_num == 2)
				rate_id_idx = PHYDM_BGN_20M_2SS;
			else if (tx_stream_num == 3)
				rate_id_idx = PHYDM_ARFR5_N_3SS;
		}
	} else if (wireless_mode == (WIRELESS_OFDM | WIRELESS_VHT)) {	/*AC mode*/
	
		if (tx_stream_num == 1)
			rate_id_idx = PHYDM_ARFR1_AC_1SS;
		else if (tx_stream_num == 2)
			rate_id_idx = PHYDM_ARFR0_AC_2SS;
		else if (tx_stream_num == 3)
			rate_id_idx = PHYDM_ARFR4_AC_3SS;
	} else if (wireless_mode == (WIRELESS_CCK | WIRELESS_OFDM | WIRELESS_VHT)) {	/*AC 2.4G mode*/
	
		if (bw >= CHANNEL_WIDTH_80) {
			if (tx_stream_num == 1)
				rate_id_idx = PHYDM_ARFR1_AC_1SS;
			else if (tx_stream_num == 2)
				rate_id_idx = PHYDM_ARFR0_AC_2SS;
			else if (tx_stream_num == 3)
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		} else {

			if (tx_stream_num == 1)
				rate_id_idx = PHYDM_ARFR2_AC_2G_1SS;
			else if (tx_stream_num == 2)
				rate_id_idx = PHYDM_ARFR3_AC_2G_2SS;
			else if (tx_stream_num == 3)
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		}
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warrning] No rate_id is found\n"));
		rate_id_idx = 0;
	}
	
	PHYDM_DBG(p_dm, DBG_RA, ("Rate_ID=((0x%x))\n", rate_id_idx));

	return rate_id_idx;
}

void
phydm_ra_h2c(
	void	*p_dm_void,
	u8	macid,
	u8	dis_ra,
	u8	dis_pt,
	u8	no_update_bw,
	u8	init_ra_lv,
	u64	ra_mask
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info				*p_ra = NULL;
	u8		h2c_val[H2C_MAX_LENGTH] = {0};

	if (is_sta_active(p_sta)) {
		p_ra = &(p_sta->ra_info);
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warning] %s invalid sta_info\n", __func__));
		return;
	}
	
	PHYDM_DBG(p_dm, DBG_RA, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_RA, ("MACID=%d\n", p_sta->mac_id));

	if (p_dm->is_disable_power_training == true)
		dis_pt = true;
	else if (p_dm->is_disable_power_training == false)
		dis_pt = false;

	h2c_val[0] = p_sta->mac_id;
	h2c_val[1] = (p_ra->rate_id & 0x1f) | ((init_ra_lv & 0x3) << 5) | (p_ra->is_support_sgi << 7);
	h2c_val[2] = (u8)((p_ra->ra_bw_mode) | (((p_sta->ldpc_en) ? 1 : 0) << 2) | 
					((no_update_bw & 0x1) << 3) | (p_ra->is_vht_enable << 4) | 
					((dis_pt & 0x1) << 6) | ((dis_ra & 0x1) << 7));
	
	h2c_val[3] = (u8)(ra_mask & 0xff);
	h2c_val[4] = (u8)((ra_mask & 0xff00) >> 8);
	h2c_val[5] = (u8)((ra_mask & 0xff0000) >> 16);
	h2c_val[6] = (u8)((ra_mask & 0xff000000) >> 24);

	PHYDM_DBG(p_dm, DBG_RA, ("PHYDM h2c[0x40]=0x%x %x %x %x %x %x %x\n",
		h2c_val[6], h2c_val[5], h2c_val[4], h2c_val[3], h2c_val[2], h2c_val[1], h2c_val[0]));

	odm_fill_h2c_cmd(p_dm, PHYDM_H2C_RA_MASK, H2C_MAX_LENGTH, h2c_val);

	#if (defined(PHYDM_COMPILE_ABOVE_3SS))
	if (p_dm->support_ic_type & (PHYDM_IC_ABOVE_3SS)) {
		
		h2c_val[3] = (u8)((ra_mask >> 32) & 0x000000ff);
		h2c_val[4] = (u8)(((ra_mask >> 32) & 0x0000ff00) >> 8);
		h2c_val[5] = (u8)(((ra_mask >> 32) & 0x00ff0000) >> 16);
		h2c_val[6] = (u8)(((ra_mask >> 32) & 0xff000000) >> 24);

		PHYDM_DBG(p_dm, DBG_RA, ("PHYDM h2c[0x46]=0x%x %x %x %x %x %x %x\n",
		h2c_val[6], h2c_val[5], h2c_val[4], h2c_val[3], h2c_val[2], h2c_val[1], h2c_val[0]));
		
		odm_fill_h2c_cmd(p_dm, PHYDM_RA_MASK_ABOVE_3SS, 5, h2c_val);
	}
	#endif
}

void
phydm_ra_registed(
	void	*p_dm_void,
	u8	macid,
	u8	rssi_from_assoc
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info				*p_ra = NULL;
	u8	init_ra_lv;
	u64	ra_mask;

	if (is_sta_active(p_sta)) {
		p_ra = &(p_sta->ra_info);
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warning] %s invalid sta_info\n", __func__));
		return;
	}

	PHYDM_DBG(p_dm, DBG_RA, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_RA, ("MACID=%d\n", p_sta->mac_id));


	#if (RTL8188E_SUPPORT == 1) && (RATE_ADAPTIVE_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E)
		phydm_get_rate_id_88e(p_dm, macid);
	else
	#endif
	{
		p_ra->rate_id = phydm_get_rate_id(p_dm, macid);
	}
	
	/*p_ra->is_vht_enable = (p_sta->support_wireless_set | WIRELESS_VHT) ? 1 : 0;*/
	/*p_ra->disable_ra = 0;*/
	/*p_ra->disable_pt = 0;*/
	ra_mask = phydm_get_bb_mod_ra_mask(p_dm, macid);


	if (rssi_from_assoc > 40)
		init_ra_lv = 3;
	else if (rssi_from_assoc > 20)
		init_ra_lv = 2;
	else
		init_ra_lv = 1;

	if (p_ra_t->record_ra_info)
		p_ra_t->record_ra_info(p_dm, macid, p_sta, ra_mask);

	#if (RTL8188E_SUPPORT == 1) && (RATE_ADAPTIVE_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8188E)
		/*Driver RA*/
		odm_ra_update_rate_info_8188e(p_dm, macid, p_ra->rate_id, (u32)ra_mask, p_ra->is_support_sgi);
	else
	#endif
	{
		/*FW RA*/
		phydm_ra_h2c(p_dm, macid, p_ra->disable_ra, p_ra->disable_pt, 0, init_ra_lv, ra_mask);
	}

	

}

void
phydm_ra_offline(
	void	*p_dm_void,
	u8	macid
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = p_dm->p_phydm_sta_info[macid];
	struct ra_sta_info				*p_ra = NULL;

	if (is_sta_active(p_sta)) {
		p_ra = &(p_sta->ra_info);
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[Warning] %s invalid sta_info\n", __func__));
		return;
	}

	PHYDM_DBG(p_dm, DBG_RA, ("%s ======>\n", __func__));
	PHYDM_DBG(p_dm, DBG_RA, ("MACID=%d\n", p_sta->mac_id));

	odm_memory_set(p_dm, &(p_ra->rate_id), 0, sizeof(struct ra_sta_info));
	p_ra->disable_ra = 1;
	p_ra->disable_pt = 1;

	if (p_ra_t->record_ra_info)
		p_ra_t->record_ra_info(p_dm, macid, p_sta, 0);

	if (p_dm->support_ic_type != ODM_RTL8188E)
		phydm_ra_h2c(p_dm, macid, p_ra->disable_ra, p_ra->disable_pt, 0, 0, 0);
}

void
phydm_ra_mask_watchdog(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = NULL;
	struct ra_sta_info				*p_ra = NULL;
	u8		macid;
	u64		ra_mask;
	u8		rssi_lv_new;

	if (!(p_dm->support_ability & ODM_BB_RA_MASK))
		return;
	
	if (((!p_dm->is_linked)) || (p_dm->phydm_sys_up_time % 2) == 1)
		return;

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("%s ======>\n", __func__));
	
	p_ra_t->up_ramask_cnt++;

	for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {
		
		p_sta = p_dm->p_phydm_sta_info[macid];
		
		if (!is_sta_active(p_sta))
			continue;

		p_ra = &(p_sta->ra_info);

		if (p_ra->disable_ra)
			continue;


		/*to be modified*/
		#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
		if ((p_dm->support_ic_type == ODM_RTL8812) ||
			((p_dm->support_ic_type == ODM_RTL8821) && (p_dm->cut_version == ODM_CUT_A))
			) {
			
			if (p_sta->rssi_stat.rssi < p_ra_t->ldpc_thres) {
				
				#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
				set_ra_ldpc_8812(p_sta, true);		/*LDPC TX enable*/
				#endif
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("RSSI=%d, ldpc_en =TRUE\n", p_sta->rssi_stat.rssi));
				
			} else if (p_sta->rssi_stat.rssi > (p_ra_t->ldpc_thres + 3)) {

				#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
				set_ra_ldpc_8812(p_sta, false);	/*LDPC TX disable*/
				#endif
				PHYDM_DBG(p_dm, DBG_RA_MASK, ("RSSI=%d, ldpc_en =FALSE\n", p_sta->rssi_stat.rssi));
			}	
		}
		#endif

		rssi_lv_new = phydm_rssi_lv_dec(p_dm, (u32)p_sta->rssi_stat.rssi, p_ra->rssi_level);

		if ((p_ra->rssi_level != rssi_lv_new) || 
			(p_ra_t->up_ramask_cnt >= FORCED_UPDATE_RAMASK_PERIOD)) {

			PHYDM_DBG(p_dm, DBG_RA_MASK, ("RSSI LV:((%d))->((%d))\n", p_ra->rssi_level, rssi_lv_new));
			
			p_ra->rssi_level = rssi_lv_new;
			p_ra_t->up_ramask_cnt = 0;
			
			ra_mask = phydm_get_bb_mod_ra_mask(p_dm, macid);

			if (p_ra_t->record_ra_info)
				p_ra_t->record_ra_info(p_dm, macid, p_sta, ra_mask);

			#if (RTL8188E_SUPPORT == 1) && (RATE_ADAPTIVE_SUPPORT == 1)
			if (p_dm->support_ic_type == ODM_RTL8188E)
				/*Driver RA*/
				odm_ra_update_rate_info_8188e(p_dm, macid, p_ra->rate_id, (u32)ra_mask, p_ra->is_support_sgi);
			else
			#endif
			{
				/*FW RA*/
				phydm_ra_h2c(p_dm, macid, p_ra->disable_ra, p_ra->disable_pt, 1, 0, ra_mask);
			}
		}
	}

}
#endif

u8
phydm_vht_en_mapping(
	void			*p_dm_void,
	u32			wireless_mode
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			vht_en_out = 0;

	if ((wireless_mode == PHYDM_WIRELESS_MODE_AC_5G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_24G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_ONLY)
	   ) {
		vht_en_out = 1;
		/**/
	}

	PHYDM_DBG(p_dm, DBG_RA, ("wireless_mode= (( 0x%x )), VHT_EN= (( %d ))\n", wireless_mode, vht_en_out));
	return vht_en_out;
}

u8
phydm_rate_id_mapping(
	void			*p_dm_void,
	u32			wireless_mode,
	u8			rf_type,
	u8			bw
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			rate_id_idx = 0;

	PHYDM_DBG(p_dm, DBG_RA, ("wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x ))\n",
			wireless_mode, rf_type, bw));


	switch (wireless_mode) {

	case PHYDM_WIRELESS_MODE_N_24G:
	{

		if (bw == CHANNEL_WIDTH_40) {

			if (rf_type == RF_1T1R)
				rate_id_idx = PHYDM_BGN_40M_1SS;
			else if (rf_type == RF_2T2R)
				rate_id_idx = PHYDM_BGN_40M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;

		} else {

			if (rf_type == RF_1T1R)
				rate_id_idx = PHYDM_BGN_20M_1SS;
			else if (rf_type == RF_2T2R)
				rate_id_idx = PHYDM_BGN_20M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;
		}
	}
	break;

	case PHYDM_WIRELESS_MODE_N_5G:
	{
		if (rf_type == RF_1T1R)
			rate_id_idx = PHYDM_GN_N1SS;
		else if (rf_type == RF_2T2R)
			rate_id_idx = PHYDM_GN_N2SS;
		else
			rate_id_idx = PHYDM_ARFR5_N_3SS;
	}

	break;

	case PHYDM_WIRELESS_MODE_G:
		rate_id_idx = PHYDM_BG;
		break;

	case PHYDM_WIRELESS_MODE_A:
		rate_id_idx = PHYDM_G;
		break;

	case PHYDM_WIRELESS_MODE_B:
		rate_id_idx = PHYDM_B_20M;
		break;


	case PHYDM_WIRELESS_MODE_AC_5G:
	case PHYDM_WIRELESS_MODE_AC_ONLY:
	{
		if (rf_type == RF_1T1R)
			rate_id_idx = PHYDM_ARFR1_AC_1SS;
		else if (rf_type == RF_2T2R)
			rate_id_idx = PHYDM_ARFR0_AC_2SS;
		else
			rate_id_idx = PHYDM_ARFR4_AC_3SS;
	}
	break;

	case PHYDM_WIRELESS_MODE_AC_24G:
	{
		/*Becareful to set "Lowest rate" while using PHYDM_ARFR4_AC_3SS in 2.4G/5G*/
		if (bw >= CHANNEL_WIDTH_80) {
			if (rf_type == RF_1T1R)
				rate_id_idx = PHYDM_ARFR1_AC_1SS;
			else if (rf_type == RF_2T2R)
				rate_id_idx = PHYDM_ARFR0_AC_2SS;
			else
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		} else {

			if (rf_type == RF_1T1R)
				rate_id_idx = PHYDM_ARFR2_AC_2G_1SS;
			else if (rf_type == RF_2T2R)
				rate_id_idx = PHYDM_ARFR3_AC_2G_2SS;
			else
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		}
	}
	break;

	default:
		rate_id_idx = 0;
		break;
	}

	PHYDM_DBG(p_dm, DBG_RA, ("RA rate ID = (( 0x%x ))\n", rate_id_idx));

	return rate_id_idx;
}

void
phydm_update_hal_ra_mask(
	void			*p_dm_void,
	u32			wireless_mode,
	u8			rf_type,
	u8			bw,
	u8			mimo_ps_enable,
	u8			disable_cck_rate,
	u32			*ratr_bitmap_msb_in,
	u32			*ratr_bitmap_lsb_in,
	u8			tx_rate_level
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			ratr_bitmap = *ratr_bitmap_lsb_in, ratr_bitmap_msb = *ratr_bitmap_msb_in;

	/*PHYDM_DBG(p_dm, DBG_RA_MASK, ("phydm_rf_type = (( %x )), rf_type = (( %x ))\n", phydm_rf_type, rf_type));*/
	PHYDM_DBG(p_dm, DBG_RA_MASK, ("Platfoem original RA Mask = (( 0x %x | %x ))\n", ratr_bitmap_msb, ratr_bitmap));

	switch (wireless_mode) {

	case PHYDM_WIRELESS_MODE_B:
	{
		ratr_bitmap &= 0x0000000f;
	}
	break;

	case PHYDM_WIRELESS_MODE_G:
	{
		ratr_bitmap &= 0x00000ff5;
	}
	break;

	case PHYDM_WIRELESS_MODE_A:
	{
		ratr_bitmap &= 0x00000ff0;
	}
	break;

	case PHYDM_WIRELESS_MODE_N_24G:
	case PHYDM_WIRELESS_MODE_N_5G:
	{
		if (mimo_ps_enable)
			rf_type = RF_1T1R;

		if (rf_type == RF_1T1R) {

			if (bw == CHANNEL_WIDTH_40)
				ratr_bitmap &= 0x000ff015;
			else
				ratr_bitmap &= 0x000ff005;
		} else if (rf_type == RF_2T2R || rf_type == RF_2T4R || rf_type == RF_2T3R) {

			if (bw == CHANNEL_WIDTH_40)
				ratr_bitmap &= 0x0ffff015;
			else
				ratr_bitmap &= 0x0ffff005;
		} else { /*3T*/

			ratr_bitmap &= 0xfffff015;
			ratr_bitmap_msb &= 0xf;
		}
	}
	break;

	case PHYDM_WIRELESS_MODE_AC_24G:
	{
		if (rf_type == RF_1T1R)
			ratr_bitmap &= 0x003ff015;
		else if (rf_type == RF_2T2R || rf_type == RF_2T4R || rf_type == RF_2T3R)
			ratr_bitmap &= 0xfffff015;
		else {/*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (bw == CHANNEL_WIDTH_20) {/* AC 20MHz doesn't support MCS9 */
			ratr_bitmap &= 0x7fdfffff;
			ratr_bitmap_msb &= 0x1ff;
		}
	}
	break;

	case PHYDM_WIRELESS_MODE_AC_5G:
	{
		if (rf_type == RF_1T1R)
			ratr_bitmap &= 0x003ff010;
		else if (rf_type == RF_2T2R || rf_type == RF_2T4R || rf_type == RF_2T3R)
			ratr_bitmap &= 0xfffff010;
		else {/*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (bw == CHANNEL_WIDTH_20) {/* AC 20MHz doesn't support MCS9 */
			ratr_bitmap &= 0x7fdfffff;
			ratr_bitmap_msb &= 0x1ff;
		}
	}
	break;

	default:
		break;
	}

	if (wireless_mode != PHYDM_WIRELESS_MODE_B) {

		if (tx_rate_level == 0)
			ratr_bitmap &=  0xffffffff;
		else if (tx_rate_level == 1)
			ratr_bitmap &=  0xfffffff0;
		else if (tx_rate_level == 2)
			ratr_bitmap &=  0xffffefe0;
		else if (tx_rate_level == 3)
			ratr_bitmap &=  0xffffcfc0;
		else if (tx_rate_level == 4)
			ratr_bitmap &=  0xffff8f80;
		else if (tx_rate_level >= 5)
			ratr_bitmap &=  0xffff0f00;

	}

	if (disable_cck_rate)
		ratr_bitmap &= 0xfffffff0;

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x )), MimoPs_en = (( %d )), tx_rate_level= (( 0x%x ))\n",
		wireless_mode, rf_type, bw, mimo_ps_enable, tx_rate_level));

	/*PHYDM_DBG(p_dm, DBG_RA_MASK, ("111 Phydm modified RA Mask = (( 0x %x | %x ))\n", ratr_bitmap_msb, ratr_bitmap));*/

	*ratr_bitmap_lsb_in = ratr_bitmap;
	*ratr_bitmap_msb_in = ratr_bitmap_msb;
	PHYDM_DBG(p_dm, DBG_RA_MASK, ("Phydm modified RA Mask = (( 0x %x | %x ))\n", *ratr_bitmap_msb_in, *ratr_bitmap_lsb_in));

}

u8
phydm_rssi_lv_dec(
	void			*p_dm_void,
	u32			rssi,
	u8			ratr_state
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	rssi_lv_table[RA_FLOOR_TABLE_SIZE] = {20, 34, 38, 42, 46, 50, 100}; /*MCS0 ~ MCS4 , VHT1SS MCS0 ~ MCS4 , G 6M~24M*/
	u8	new_rssi_lv = 0;
	u8	i;

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("curr RA level=(%d), Table_ori=[%d, %d, %d, %d, %d, %d]\n",
		ratr_state, rssi_lv_table[0], rssi_lv_table[1], rssi_lv_table[2], rssi_lv_table[3], rssi_lv_table[4], rssi_lv_table[5]));

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {

		if (i >= (ratr_state))
			rssi_lv_table[i] += RA_FLOOR_UP_GAP;
	}

	PHYDM_DBG(p_dm, DBG_RA_MASK, ("RSSI=(%d), Table_mod=[%d, %d, %d, %d, %d, %d]\n",
		rssi, rssi_lv_table[0], rssi_lv_table[1], rssi_lv_table[2], rssi_lv_table[3], rssi_lv_table[4], rssi_lv_table[5]));

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {

		if (rssi < rssi_lv_table[i]) {
			new_rssi_lv = i;
			break;
		}
	}
	return	new_rssi_lv;
}

u8
phydm_rate_order_compute(
	void	*p_dm_void,
	u8	rate_idx
)
{
	u8		rate_order = 0;

	if (rate_idx >= ODM_RATEVHTSS4MCS0) {

		rate_idx -= ODM_RATEVHTSS4MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS3MCS0) {

		rate_idx -= ODM_RATEVHTSS3MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS2MCS0) {

		rate_idx -= ODM_RATEVHTSS2MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEVHTSS1MCS0) {

		rate_idx -= ODM_RATEVHTSS1MCS0;
		/**/
	} else if (rate_idx >= ODM_RATEMCS24) {

		rate_idx -= ODM_RATEMCS24;
		/**/
	} else if (rate_idx >= ODM_RATEMCS16) {

		rate_idx -= ODM_RATEMCS16;
		/**/
	} else if (rate_idx >= ODM_RATEMCS8) {

		rate_idx -= ODM_RATEMCS8;
		/**/
	}
	rate_order = rate_idx;

	return rate_order;

}

void
phydm_ra_common_info_update(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;
	struct cmn_sta_info			*p_sta = NULL;
	u16		macid;
	u8		rate_order_tmp;
	u8		cnt = 0;

	p_ra_table->highest_client_tx_order = 0;
	p_ra_table->power_tracking_flag = 1;

	if (p_dm->number_linked_client != 0) {
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {

			p_sta = p_dm->p_phydm_sta_info[macid];
			
			if (is_sta_active(p_sta)) {
			
				rate_order_tmp = phydm_rate_order_compute(p_dm, (p_sta->ra_info.curr_tx_rate & 0x7f));

				if (rate_order_tmp >= (p_ra_table->highest_client_tx_order)) {
					p_ra_table->highest_client_tx_order = rate_order_tmp;
					p_ra_table->highest_client_tx_rate_order = macid;
				}

				cnt++;

				if (cnt == p_dm->number_linked_client)
					break;
			}
		}
		PHYDM_DBG(p_dm, DBG_RA, ("MACID[%d], Highest Tx order Update for power traking: %d\n", (p_ra_table->highest_client_tx_rate_order), (p_ra_table->highest_client_tx_order)));
	}
}

void
phydm_ra_info_watchdog(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	phydm_ra_common_info_update(p_dm);
	#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	phydm_ra_dynamic_retry_limit(p_dm);
	#endif
	phydm_ra_dynamic_retry_count(p_dm);
	phydm_refresh_rate_adaptive_mask(p_dm);

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	odm_refresh_basic_rate_mask(p_dm);
	#endif
}

void
phydm_ra_info_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;

	p_ra_table->highest_client_tx_rate_order = 0;
	p_ra_table->highest_client_tx_order = 0;
	p_ra_table->RA_threshold_offset = 0;
	p_ra_table->RA_offset_direction = 0;
	
#if (RTL8822B_SUPPORT == 1)
	if (p_dm->support_ic_type == ODM_RTL8822B) {
		u32	ret_value;

		ret_value = odm_get_bb_reg(p_dm, 0x4c8, MASKBYTE2);
		odm_set_bb_reg(p_dm, 0x4cc, MASKBYTE3, (ret_value - 1));
	}
#endif
	
	#ifdef CONFIG_RA_DYNAMIC_RTY_LIMIT
	phydm_ra_dynamic_retry_limit_init(p_dm);
	#endif

	#ifdef CONFIG_RA_DYNAMIC_RATE_ID
	phydm_ra_dynamic_rate_id_init(p_dm);
	#endif

	#ifdef CONFIG_RA_DBG_CMD
	odm_ra_para_adjust_init(p_dm);
	#endif

	phydm_rate_adaptive_mask_init(p_dm);
	
}

u8
odm_find_rts_rate(
	void			*p_dm_void,
	u8			tx_rate,
	boolean		is_erp_protect
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	rts_ini_rate = ODM_RATE6M;

	if (is_erp_protect) /* use CCK rate as RTS*/
		rts_ini_rate = ODM_RATE1M;
	else {
		switch (tx_rate) {
		case ODM_RATEVHTSS3MCS9:
		case ODM_RATEVHTSS3MCS8:
		case ODM_RATEVHTSS3MCS7:
		case ODM_RATEVHTSS3MCS6:
		case ODM_RATEVHTSS3MCS5:
		case ODM_RATEVHTSS3MCS4:
		case ODM_RATEVHTSS3MCS3:
		case ODM_RATEVHTSS2MCS9:
		case ODM_RATEVHTSS2MCS8:
		case ODM_RATEVHTSS2MCS7:
		case ODM_RATEVHTSS2MCS6:
		case ODM_RATEVHTSS2MCS5:
		case ODM_RATEVHTSS2MCS4:
		case ODM_RATEVHTSS2MCS3:
		case ODM_RATEVHTSS1MCS9:
		case ODM_RATEVHTSS1MCS8:
		case ODM_RATEVHTSS1MCS7:
		case ODM_RATEVHTSS1MCS6:
		case ODM_RATEVHTSS1MCS5:
		case ODM_RATEVHTSS1MCS4:
		case ODM_RATEVHTSS1MCS3:
		case ODM_RATEMCS15:
		case ODM_RATEMCS14:
		case ODM_RATEMCS13:
		case ODM_RATEMCS12:
		case ODM_RATEMCS11:
		case ODM_RATEMCS7:
		case ODM_RATEMCS6:
		case ODM_RATEMCS5:
		case ODM_RATEMCS4:
		case ODM_RATEMCS3:
		case ODM_RATE54M:
		case ODM_RATE48M:
		case ODM_RATE36M:
		case ODM_RATE24M:
			rts_ini_rate = ODM_RATE24M;
			break;
		case ODM_RATEVHTSS3MCS2:
		case ODM_RATEVHTSS3MCS1:
		case ODM_RATEVHTSS2MCS2:
		case ODM_RATEVHTSS2MCS1:
		case ODM_RATEVHTSS1MCS2:
		case ODM_RATEVHTSS1MCS1:
		case ODM_RATEMCS10:
		case ODM_RATEMCS9:
		case ODM_RATEMCS2:
		case ODM_RATEMCS1:
		case ODM_RATE18M:
		case ODM_RATE12M:
			rts_ini_rate = ODM_RATE12M;
			break;
		case ODM_RATEVHTSS3MCS0:
		case ODM_RATEVHTSS2MCS0:
		case ODM_RATEVHTSS1MCS0:
		case ODM_RATEMCS8:
		case ODM_RATEMCS0:
		case ODM_RATE9M:
		case ODM_RATE6M:
			rts_ini_rate = ODM_RATE6M;
			break;
		case ODM_RATE11M:
		case ODM_RATE5_5M:
		case ODM_RATE2M:
		case ODM_RATE1M:
			rts_ini_rate = ODM_RATE1M;
			break;
		default:
			rts_ini_rate = ODM_RATE6M;
			break;
		}
	}

	if (*p_dm->p_band_type == ODM_BAND_5G) {
		if (rts_ini_rate < ODM_RATE6M)
			rts_ini_rate = ODM_RATE6M;
	}
	return rts_ini_rate;

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

void
odm_refresh_basic_rate_mask(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter	 =  p_dm->adapter;
	static u8		stage = 0;
	u8			cur_stage = 0;
	OCTET_STRING	os_rate_set;
	PMGNT_INFO		p_mgnt_info = GetDefaultMgntInfo(adapter);
	u8			rate_set[5] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M, MGN_6M};

	if (p_dm->support_ic_type != ODM_RTL8812 && p_dm->support_ic_type != ODM_RTL8821)
		return;

	if (p_dm->is_linked == false)	/* unlink Default port information */
		cur_stage = 0;
	else if (p_dm->rssi_min < 40)	/* link RSSI  < 40% */
		cur_stage = 1;
	else if (p_dm->rssi_min > 45)	/* link RSSI > 45% */
		cur_stage = 3;
	else
		cur_stage = 2;					/* link  25% <= RSSI <= 30% */

	if (cur_stage != stage) {
		if (cur_stage == 1) {
			FillOctetString(os_rate_set, rate_set, 5);
			FilterSupportRate(p_mgnt_info->mBrates, &os_rate_set, false);
			phydm_set_hw_reg_handler_interface(p_dm, HW_VAR_BASIC_RATE, (u8 *)&os_rate_set);
		} else if (cur_stage == 3 && (stage == 1 || stage == 2))
			phydm_set_hw_reg_handler_interface(p_dm, HW_VAR_BASIC_RATE, (u8 *)(&p_mgnt_info->mBrates));
	}

	stage = cur_stage;
}

void
odm_refresh_ldpc_rts_mp(
	struct _ADAPTER			*p_adapter,
	struct PHY_DM_STRUCT			*p_dm,
	u8				m_mac_id,
	u8				iot_peer,
	s32				undecorated_smoothed_pwdb
)
{
	boolean					is_ctl_ldpc = false;
	struct _rate_adaptive_table_	*p_ra_t = &p_dm->dm_ra_table;

	if (p_dm->support_ic_type != ODM_RTL8821 && p_dm->support_ic_type != ODM_RTL8812)
		return;

	if ((p_dm->support_ic_type == ODM_RTL8821) && (p_dm->cut_version == ODM_CUT_A))
		is_ctl_ldpc = true;
	else if (p_dm->support_ic_type == ODM_RTL8812 &&
		 iot_peer == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP)
		is_ctl_ldpc = true;

	if (is_ctl_ldpc) {
		if (undecorated_smoothed_pwdb < (p_ra_t->ldpc_thres - 5))
			MgntSet_TX_LDPC(p_adapter, m_mac_id, true);
		else if (undecorated_smoothed_pwdb > p_ra_t->ldpc_thres)
			MgntSet_TX_LDPC(p_adapter, m_mac_id, false);
	}
}

void
odm_rate_adaptive_state_ap_init(
	void		*PADAPTER_VOID,
	struct cmn_sta_info*p_entry
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)PADAPTER_VOID;
	p_entry->ra_info.rssi_level = DM_RATR_STA_INIT;
}

#elif (DM_ODM_SUPPORT_TYPE & ODM_AP)

void
phydm_gen_ramask_h2c_AP(
	void			*p_dm_void,
	struct rtl8192cd_priv *priv,
	struct sta_info *p_entry,
	u8			rssi_level
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type == ODM_RTL8812) {

		#if (RTL8812A_SUPPORT == 1)
		UpdateHalRAMask8812(priv, p_entry, rssi_level);
		/**/
		#endif
	} else if (p_dm->support_ic_type == ODM_RTL8188E) {

		#if (RTL8188E_SUPPORT == 1)
		#ifdef TXREPORT
		add_RATid(priv, p_entry);
		/**/
		#endif
		#endif
	} else {
		#ifdef CONFIG_WLAN_HAL
		GET_HAL_INTERFACE(priv)->UpdateHalRAMaskHandler(priv, p_entry, rssi_level);
		#endif
	} 
}

#endif

#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))

void
phydm_retry_limit_table_bound(
	void	*p_dm_void,
	u8	*retry_limit,
	u8	offset
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;

	if (*retry_limit >  offset) {

		*retry_limit -= offset;

		if (*retry_limit < p_ra_table->retrylimit_low)
			*retry_limit = p_ra_table->retrylimit_low;
		else if (*retry_limit > p_ra_table->retrylimit_high)
			*retry_limit = p_ra_table->retrylimit_high;
	} else
		*retry_limit = p_ra_table->retrylimit_low;
}

void
phydm_reset_retry_limit_table(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;
	u8			i;

	u8 per_rate_retrylimit_table_20M[ODM_RATEMCS15 + 1] = {
		1, 1, 2, 4,					/*CCK*/
		2, 2, 4, 6, 8, 12, 16, 18,		/*OFDM*/
		2, 4, 6, 8, 12, 18, 20, 22,		/*20M HT-1SS*/
		2, 4, 6, 8, 12, 18, 20, 22		/*20M HT-2SS*/
	};
	u8 per_rate_retrylimit_table_40M[ODM_RATEMCS15 + 1] = {
		1, 1, 2, 4,					/*CCK*/
		2, 2, 4, 6, 8, 12, 16, 18,		/*OFDM*/
		4, 8, 12, 16, 24, 32, 32, 32,		/*40M HT-1SS*/
		4, 8, 12, 16, 24, 32, 32, 32		/*40M HT-2SS*/
	};

	memcpy(&(p_ra_table->per_rate_retrylimit_20M[0]), &(per_rate_retrylimit_table_20M[0]), ODM_NUM_RATE_IDX);
	memcpy(&(p_ra_table->per_rate_retrylimit_40M[0]), &(per_rate_retrylimit_table_40M[0]), ODM_NUM_RATE_IDX);

	for (i = 0; i < ODM_NUM_RATE_IDX; i++) {
		phydm_retry_limit_table_bound(p_dm, &(p_ra_table->per_rate_retrylimit_20M[i]), 0);
		phydm_retry_limit_table_bound(p_dm, &(p_ra_table->per_rate_retrylimit_40M[i]), 0);
	}
}

void
phydm_ra_dynamic_retry_limit_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;

	p_ra_table->retry_descend_num = RA_RETRY_DESCEND_NUM;
	p_ra_table->retrylimit_low = RA_RETRY_LIMIT_LOW;
	p_ra_table->retrylimit_high = RA_RETRY_LIMIT_HIGH;

	phydm_reset_retry_limit_table(p_dm);

}

void
phydm_ra_dynamic_retry_limit(
	void	*p_dm_void
)
{

	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;
	u8	i, retry_offset;
	u32	ma_rx_tp;


	if (p_dm->pre_number_active_client == p_dm->number_active_client) {

		PHYDM_DBG(p_dm, DBG_RA, (" pre_number_active_client ==  number_active_client\n"));
		return;

	} else {
		if (p_dm->number_active_client == 1) {
			phydm_reset_retry_limit_table(p_dm);
			PHYDM_DBG(p_dm, DBG_RA, ("one client only->reset to default value\n"));
		} else {

			retry_offset = p_dm->number_active_client * p_ra_table->retry_descend_num;

			for (i = 0; i < ODM_NUM_RATE_IDX; i++) {

				phydm_retry_limit_table_bound(p_dm, &(p_ra_table->per_rate_retrylimit_20M[i]), retry_offset);
				phydm_retry_limit_table_bound(p_dm, &(p_ra_table->per_rate_retrylimit_40M[i]), retry_offset);
			}
		}
	}
}
#endif

#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))
void
phydm_ra_dynamic_rate_id_on_assoc(
	void	*p_dm_void,
	u8	wireless_mode,
	u8	init_rate_id
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	PHYDM_DBG(p_dm, DBG_RA, ("[ON ASSOC] rf_mode = ((0x%x)), wireless_mode = ((0x%x)), init_rate_id = ((0x%x))\n", p_dm->rf_type, wireless_mode, init_rate_id));

	if ((p_dm->rf_type == RF_2T2R) || (p_dm->rf_type == RF_2T3R) || (p_dm->rf_type == RF_2T4R)) {

		if ((p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E)) &&
		    (wireless_mode & (ODM_WM_N24G | ODM_WM_N5G))
		   ) {
			PHYDM_DBG(p_dm, DBG_RA, ("[ON ASSOC] set N-2SS ARFR5 table\n"));
			odm_set_mac_reg(p_dm, 0x4a4, MASKDWORD, 0xfc1ffff);	/*N-2SS, ARFR5, rate_id = 0xe*/
			odm_set_mac_reg(p_dm, 0x4a8, MASKDWORD, 0x0);		/*N-2SS, ARFR5, rate_id = 0xe*/
		} else if ((p_dm->support_ic_type & (ODM_RTL8812)) &&
			(wireless_mode & (ODM_WM_AC_5G | ODM_WM_AC_24G | ODM_WM_AC_ONLY))
			  ) {
			PHYDM_DBG(p_dm, DBG_RA, ("[ON ASSOC] set AC-2SS ARFR0 table\n"));
			odm_set_mac_reg(p_dm, 0x444, MASKDWORD, 0x0fff);	/*AC-2SS, ARFR0, rate_id = 0x9*/
			odm_set_mac_reg(p_dm, 0x448, MASKDWORD, 0xff01f000);		/*AC-2SS, ARFR0, rate_id = 0x9*/
		}
	}

}

void
phydm_ra_dynamic_rate_id_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E)) {

		odm_set_mac_reg(p_dm, 0x4a4, MASKDWORD, 0xfc1ffff);	/*N-2SS, ARFR5, rate_id = 0xe*/
		odm_set_mac_reg(p_dm, 0x4a8, MASKDWORD, 0x0);		/*N-2SS, ARFR5, rate_id = 0xe*/

		odm_set_mac_reg(p_dm, 0x444, MASKDWORD, 0x0fff);		/*AC-2SS, ARFR0, rate_id = 0x9*/
		odm_set_mac_reg(p_dm, 0x448, MASKDWORD, 0xff01f000);	/*AC-2SS, ARFR0, rate_id = 0x9*/
	}
}

void
phydm_update_rate_id(
	void	*p_dm_void,
	u8	rate,
	u8	platform_macid
)
{
#if 0

	struct PHY_DM_STRUCT	*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm->dm_ra_table;
	u8		current_tx_ss;
	u8		rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8		wireless_mode;
	u8		phydm_macid;
	struct sta_info	*p_entry;
	struct cmn_sta_info	*p_sta;


#if	0
	if (rate_idx >= ODM_RATEVHTSS2MCS0) {
		PHYDM_DBG(p_dm, DBG_RA, ("rate[%d]: (( VHT2SS-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEVHTSS2MCS0)));
		/*dummy for SD4 check patch*/
	} else if (rate_idx >= ODM_RATEVHTSS1MCS0) {
		PHYDM_DBG(p_dm, DBG_RA, ("rate[%d]: (( VHT1SS-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEVHTSS1MCS0)));
		/*dummy for SD4 check patch*/
	} else if (rate_idx >= ODM_RATEMCS0) {
		PHYDM_DBG(p_dm, DBG_RA, ("rate[%d]: (( HT-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEMCS0)));
		/*dummy for SD4 check patch*/
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("rate[%d]: (( HT-MCS%d ))\n", platform_macid, rate_idx));
		/*dummy for SD4 check patch*/
	}
#endif

	phydm_macid = p_dm->phydm_macid_table[platform_macid];
	p_entry = p_dm->p_odm_sta_info[phydm_macid];
	p_sta = p_dm->p_phydm_sta_info[phydm_macid];

	if (is_sta_active(p_sta)) {
		wireless_mode = p_entry->wireless_mode;

		if ((p_dm->rf_type  == RF_2T2R) || (p_dm->rf_type  == RF_2T3R) || (p_dm->rf_type  == RF_2T4R)) {

			if (wireless_mode & (ODM_WM_N24G | ODM_WM_N5G)) { /*N mode*/
				if (rate_idx >= ODM_RATEMCS8 && rate_idx <= ODM_RATEMCS15) { /*2SS mode*/

					p_sta->ra_info.rate_id  = ARFR_5_RATE_ID;
					PHYDM_DBG(p_dm, DBG_RA, ("ARFR_5\n"));
				}
			} else if (wireless_mode & (ODM_WM_AC_5G | ODM_WM_AC_24G | ODM_WM_AC_ONLY)) {/*AC mode*/
				if (rate_idx >= ODM_RATEVHTSS2MCS0 && rate_idx <= ODM_RATEVHTSS2MCS9) {/*2SS mode*/

					p_sta->ra_info.rate_id  = ARFR_0_RATE_ID;
					PHYDM_DBG(p_dm, DBG_RA, ("ARFR_0\n"));
				}
			} else
				p_sta->ra_info.rate_id  = ARFR_0_RATE_ID;

			PHYDM_DBG(p_dm, DBG_RA, ("UPdate_RateID[%d]: (( 0x%x ))\n", platform_macid, p_sta->ra_info.rate_id));
		}
	}
#endif
}

#endif

#if (defined(CONFIG_RA_DBG_CMD))
void
odm_ra_para_adjust_send_h2c(
	void	*p_dm_void
)
{

	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u8			h2c_parameter[6] = {0};

	h2c_parameter[0] = RA_FIRST_MACID;

	if (p_ra_table->ra_para_feedback_req) { /*h2c_parameter[5]=1 ; ask FW for all RA parameters*/
		PHYDM_DBG(p_dm, DBG_RA, ("[H2C] Ask FW for RA parameter\n"));
		h2c_parameter[5] |= BIT(1); /*ask FW to report RA parameters*/
		h2c_parameter[1] = p_ra_table->para_idx; /*p_ra_table->para_idx;*/
		p_ra_table->ra_para_feedback_req = 0;
	} else {
		PHYDM_DBG(p_dm, DBG_RA, ("[H2C] Send H2C to FW for modifying RA parameter\n"));

		h2c_parameter[1] =  p_ra_table->para_idx;
		h2c_parameter[2] =  p_ra_table->rate_idx;
		/* [8 bit]*/
		if (p_ra_table->para_idx == RADBG_RTY_PENALTY || p_ra_table->para_idx == RADBG_RATE_UP_RTY_RATIO || p_ra_table->para_idx == RADBG_RATE_DOWN_RTY_RATIO) {
			h2c_parameter[3] = p_ra_table->value;
			h2c_parameter[4] = 0;
		}
		/* [16 bit]*/
		else {
			h2c_parameter[3] = (u8)(((p_ra_table->value_16) & 0xf0) >> 4); /*byte1*/
			h2c_parameter[4] = (u8)((p_ra_table->value_16) & 0x0f);	/*byte0*/
		}
	}
	PHYDM_DBG(p_dm, DBG_RA, (" h2c_parameter[1] = 0x%x\n", h2c_parameter[1]));
	PHYDM_DBG(p_dm, DBG_RA, (" h2c_parameter[2] = 0x%x\n", h2c_parameter[2]));
	PHYDM_DBG(p_dm, DBG_RA, (" h2c_parameter[3] = 0x%x\n", h2c_parameter[3]));
	PHYDM_DBG(p_dm, DBG_RA, (" h2c_parameter[4] = 0x%x\n", h2c_parameter[4]));
	PHYDM_DBG(p_dm, DBG_RA, (" h2c_parameter[5] = 0x%x\n", h2c_parameter[5]));

	odm_fill_h2c_cmd(p_dm, ODM_H2C_RA_PARA_ADJUST, 6, h2c_parameter);

}


void
odm_ra_para_adjust(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u8			rate_idx = p_ra_table->rate_idx;
	u8			value = p_ra_table->value;
	u8			pre_value = 0xff;

	if (p_ra_table->para_idx == RADBG_RTY_PENALTY) {
		pre_value = p_ra_table->RTY_P[rate_idx];
		p_ra_table->RTY_P[rate_idx] = value;
		p_ra_table->RTY_P_modify_note[rate_idx] = 1;
	} else if (p_ra_table->para_idx == RADBG_N_HIGH) {

	} else if (p_ra_table->para_idx == RADBG_N_LOW) {

	} else if (p_ra_table->para_idx == RADBG_RATE_UP_RTY_RATIO) {
		pre_value = p_ra_table->RATE_UP_RTY_RATIO[rate_idx];
		p_ra_table->RATE_UP_RTY_RATIO[rate_idx] = value;
		p_ra_table->RATE_UP_RTY_RATIO_modify_note[rate_idx] = 1;
	} else if (p_ra_table->para_idx == RADBG_RATE_DOWN_RTY_RATIO) {
		pre_value = p_ra_table->RATE_DOWN_RTY_RATIO[rate_idx];
		p_ra_table->RATE_DOWN_RTY_RATIO[rate_idx] = value;
		p_ra_table->RATE_DOWN_RTY_RATIO_modify_note[rate_idx] = 1;
	}
	PHYDM_DBG(p_dm, DBG_RA, ("Change RA Papa[%d], rate[ %d ],   ((%d))  ->  ((%d))\n", p_ra_table->para_idx, rate_idx, pre_value, value));
	odm_ra_para_adjust_send_h2c(p_dm);
}

void
phydm_ra_print_msg(
	void		*p_dm_void,
	u8		*value,
	u8		*value_default,
	u8		*modify_note
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u32 i;

	PHYDM_DBG(p_dm, DBG_RA, (" |rate index| |Current-value| |Default-value| |Modify?|\n"));
	for (i = 0 ; i <= (p_ra_table->rate_length); i++) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		PHYDM_DBG(p_dm, DBG_RA, ("     [ %d ]  %20d  %25d  %20s\n", i, value[i], value_default[i], ((modify_note[i] == 1) ? "V" : " .  ")));
#else
		PHYDM_DBG(p_dm, DBG_RA, ("     [ %d ]  %10d  %14d  %14s\n", i, value[i], value_default[i], ((modify_note[i] == 1) ? "V" : " .  ")));
#endif
	}

}

void
odm_RA_debug(
	void		*p_dm_void,
	u32		*const dm_value
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;

	p_ra_table->is_ra_dbg_init = false;

	if (dm_value[0] == 100) { /*1 Print RA Parameters*/
		u8	default_pointer_value;
		u8	*pvalue;
		u8	*pvalue_default;
		u8	*pmodify_note;

		pvalue = pvalue_default = pmodify_note = &default_pointer_value;

		PHYDM_DBG(p_dm, DBG_RA, ("\n------------------------------------------------------------------------------------\n"));

		if (dm_value[1] == RADBG_RTY_PENALTY) { /* [1]*/
			PHYDM_DBG(p_dm, DBG_RA, (" [1] RTY_PENALTY\n"));
			pvalue		=	&(p_ra_table->RTY_P[0]);
			pvalue_default	=	&(p_ra_table->RTY_P_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RTY_P_modify_note[0]);
		} else if (dm_value[1] == RADBG_N_HIGH)   /* [2]*/
			PHYDM_DBG(p_dm, DBG_RA, (" [2] N_HIGH\n"));

		else if (dm_value[1] == RADBG_N_LOW)   /*[3]*/
			PHYDM_DBG(p_dm, DBG_RA, (" [3] N_LOW\n"));

		else if (dm_value[1] == RADBG_RATE_UP_RTY_RATIO) { /* [8]*/
			PHYDM_DBG(p_dm, DBG_RA, (" [8] RATE_UP_RTY_RATIO\n"));
			pvalue		=	&(p_ra_table->RATE_UP_RTY_RATIO[0]);
			pvalue_default	=	&(p_ra_table->RATE_UP_RTY_RATIO_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RATE_UP_RTY_RATIO_modify_note[0]);
		} else if (dm_value[1] == RADBG_RATE_DOWN_RTY_RATIO) { /* [9]*/
			PHYDM_DBG(p_dm, DBG_RA, (" [9] RATE_DOWN_RTY_RATIO\n"));
			pvalue		=	&(p_ra_table->RATE_DOWN_RTY_RATIO[0]);
			pvalue_default	=	&(p_ra_table->RATE_DOWN_RTY_RATIO_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RATE_DOWN_RTY_RATIO_modify_note[0]);
		}

		phydm_ra_print_msg(p_dm, pvalue, pvalue_default, pmodify_note);
		PHYDM_DBG(p_dm, DBG_RA, ("\n------------------------------------------------------------------------------------\n\n"));

	} else if (dm_value[0] == 101) {
		p_ra_table->para_idx = (u8)dm_value[1];

		p_ra_table->ra_para_feedback_req = 1;
		odm_ra_para_adjust_send_h2c(p_dm);
	} else {
		p_ra_table->para_idx = (u8)dm_value[0];
		p_ra_table->rate_idx  = (u8)dm_value[1];
		p_ra_table->value = (u8)dm_value[2];

		odm_ra_para_adjust(p_dm);
	}
}

void
odm_ra_para_adjust_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm->dm_ra_table;
	u8			i;
	u8			ra_para_pool_u8[3] = { RADBG_RTY_PENALTY,  RADBG_RATE_UP_RTY_RATIO, RADBG_RATE_DOWN_RTY_RATIO};
	u8			rate_size_ht_1ss = 20, rate_size_ht_2ss = 28, rate_size_ht_3ss = 36;	 /*4+8+8+8+8 =36*/
	u8			rate_size_vht_1ss = 10, rate_size_vht_2ss = 20, rate_size_vht_3ss = 30;	 /*10 + 10 +10 =30*/
#if 0
	/* RTY_PENALTY		=	1,   u8 */
	/* N_HIGH 				=	2, */
	/* N_LOW				=	3, */
	/* RATE_UP_TABLE		=	4, */
	/* RATE_DOWN_TABLE	=	5, */
	/* TRYING_NECESSARY	=	6, */
	/* DROPING_NECESSARY =	7, */
	/* RATE_UP_RTY_RATIO	=	8,  u8 */
	/* RATE_DOWN_RTY_RATIO=	9,  u8 */
	/* ALL_PARA		=	0xff */

#endif
	PHYDM_DBG(p_dm, DBG_RA, ("odm_ra_para_adjust_init\n"));

/* JJ ADD 20161014 */
	if (p_dm->support_ic_type & (ODM_RTL8188F | ODM_RTL8195A | ODM_RTL8703B | ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8723D | ODM_RTL8710B))
		p_ra_table->rate_length = rate_size_ht_1ss;
	else if (p_dm->support_ic_type & (ODM_RTL8192E | ODM_RTL8197F))
		p_ra_table->rate_length = rate_size_ht_2ss;
	else if (p_dm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8821C))
		p_ra_table->rate_length = rate_size_ht_1ss + rate_size_vht_1ss;
	else if (p_dm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B))
		p_ra_table->rate_length = rate_size_ht_2ss + rate_size_vht_2ss;
	else if (p_dm->support_ic_type == ODM_RTL8814A)
		p_ra_table->rate_length = rate_size_ht_3ss + rate_size_vht_3ss;
	else
		p_ra_table->rate_length = rate_size_ht_1ss;

	p_ra_table->is_ra_dbg_init = true;
	for (i = 0; i < 3; i++) {
		p_ra_table->ra_para_feedback_req = 1;
		p_ra_table->para_idx	=	ra_para_pool_u8[i];
		odm_ra_para_adjust_send_h2c(p_dm);
	}
}

#endif /*#if (defined(CONFIG_RA_DBG_CMD))*/


