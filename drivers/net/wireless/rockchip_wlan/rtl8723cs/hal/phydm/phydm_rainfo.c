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
phydm_h2C_debug(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
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

	odm_fill_h2c_cmd(p_dm_odm, phydm_h2c_id, H2C_MAX_LENGTH, h2c_parameter);

}

#if (defined(CONFIG_RA_DBG_CMD))
void
odm_ra_para_adjust_send_h2c(
	void	*p_dm_void
)
{

	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	u8			h2c_parameter[6] = {0};

	h2c_parameter[0] = RA_FIRST_MACID;

	if (p_ra_table->ra_para_feedback_req) { /*h2c_parameter[5]=1 ; ask FW for all RA parameters*/
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C] Ask FW for RA parameter\n"));
		h2c_parameter[5] |= BIT(1); /*ask FW to report RA parameters*/
		h2c_parameter[1] = p_ra_table->para_idx; /*p_ra_table->para_idx;*/
		p_ra_table->ra_para_feedback_req = 0;
	} else {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[H2C] Send H2C to FW for modifying RA parameter\n"));

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
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" h2c_parameter[1] = 0x%x\n", h2c_parameter[1]));
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" h2c_parameter[2] = 0x%x\n", h2c_parameter[2]));
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" h2c_parameter[3] = 0x%x\n", h2c_parameter[3]));
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" h2c_parameter[4] = 0x%x\n", h2c_parameter[4]));
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" h2c_parameter[5] = 0x%x\n", h2c_parameter[5]));

	odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RA_PARA_ADJUST, 6, h2c_parameter);

}


void
odm_ra_para_adjust(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
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
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("Change RA Papa[%d], rate[ %d ],   ((%d))  ->  ((%d))\n", p_ra_table->para_idx, rate_idx, pre_value, value));
	odm_ra_para_adjust_send_h2c(p_dm_odm);
}

void
phydm_ra_print_msg(
	void		*p_dm_void,
	u8		*value,
	u8		*value_default,
	u8		*modify_note
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	u32 i;

	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index| |Current-value| |Default-value| |Modify?|\n"));
	for (i = 0 ; i <= (p_ra_table->rate_length); i++) {
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("     [ %d ]  %20d  %25d  %20s\n", i, value[i], value_default[i], ((modify_note[i] == 1) ? "V" : " .  ")));
#else
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("     [ %d ]  %10d  %14d  %14s\n", i, value[i], value_default[i], ((modify_note[i] == 1) ? "V" : " .  ")));
#endif
	}

}

void
odm_RA_debug(
	void		*p_dm_void,
	u32		*const dm_value
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	p_ra_table->is_ra_dbg_init = false;

	if (dm_value[0] == 100) { /*1 Print RA Parameters*/
		u8	default_pointer_value;
		u8	*pvalue;
		u8	*pvalue_default;
		u8	*pmodify_note;

		pvalue = pvalue_default = pmodify_note = &default_pointer_value;

		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("\n------------------------------------------------------------------------------------\n"));

		if (dm_value[1] == RADBG_RTY_PENALTY) { /* [1]*/
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" [1] RTY_PENALTY\n"));
			pvalue		=	&(p_ra_table->RTY_P[0]);
			pvalue_default	=	&(p_ra_table->RTY_P_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RTY_P_modify_note[0]);
		} else if (dm_value[1] == RADBG_N_HIGH)   /* [2]*/
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" [2] N_HIGH\n"));

		else if (dm_value[1] == RADBG_N_LOW)   /*[3]*/
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" [3] N_LOW\n"));

		else if (dm_value[1] == RADBG_RATE_UP_RTY_RATIO) { /* [8]*/
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" [8] RATE_UP_RTY_RATIO\n"));
			pvalue		=	&(p_ra_table->RATE_UP_RTY_RATIO[0]);
			pvalue_default	=	&(p_ra_table->RATE_UP_RTY_RATIO_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RATE_UP_RTY_RATIO_modify_note[0]);
		} else if (dm_value[1] == RADBG_RATE_DOWN_RTY_RATIO) { /* [9]*/
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" [9] RATE_DOWN_RTY_RATIO\n"));
			pvalue		=	&(p_ra_table->RATE_DOWN_RTY_RATIO[0]);
			pvalue_default	=	&(p_ra_table->RATE_DOWN_RTY_RATIO_default[0]);
			pmodify_note	=	(u8 *)&(p_ra_table->RATE_DOWN_RTY_RATIO_modify_note[0]);
		}

		phydm_ra_print_msg(p_dm_odm, pvalue, pvalue_default, pmodify_note);
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("\n------------------------------------------------------------------------------------\n\n"));

	} else if (dm_value[0] == 101) {
		p_ra_table->para_idx = (u8)dm_value[1];

		p_ra_table->ra_para_feedback_req = 1;
		odm_ra_para_adjust_send_h2c(p_dm_odm);
	} else {
		p_ra_table->para_idx = (u8)dm_value[0];
		p_ra_table->rate_idx  = (u8)dm_value[1];
		p_ra_table->value = (u8)dm_value[2];

		odm_ra_para_adjust(p_dm_odm);
	}
}

void
odm_ra_para_adjust_init(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
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
	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("odm_ra_para_adjust_init\n"));

/* JJ ADD 20161014 */
	if (p_dm_odm->support_ic_type & (ODM_RTL8188F | ODM_RTL8195A | ODM_RTL8703B | ODM_RTL8723B | ODM_RTL8188E | ODM_RTL8723D | ODM_RTL8710B))
		p_ra_table->rate_length = rate_size_ht_1ss;
	else if (p_dm_odm->support_ic_type & (ODM_RTL8192E | ODM_RTL8197F))
		p_ra_table->rate_length = rate_size_ht_2ss;
	else if (p_dm_odm->support_ic_type & (ODM_RTL8821 | ODM_RTL8881A | ODM_RTL8821C))
		p_ra_table->rate_length = rate_size_ht_1ss + rate_size_vht_1ss;
	else if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8822B))
		p_ra_table->rate_length = rate_size_ht_2ss + rate_size_vht_2ss;
	else if (p_dm_odm->support_ic_type == ODM_RTL8814A)
		p_ra_table->rate_length = rate_size_ht_3ss + rate_size_vht_3ss;
	else
		p_ra_table->rate_length = rate_size_ht_1ss;

	p_ra_table->is_ra_dbg_init = true;
	for (i = 0; i < 3; i++) {
		p_ra_table->ra_para_feedback_req = 1;
		p_ra_table->para_idx	=	ra_para_pool_u8[i];
		odm_ra_para_adjust_send_h2c(p_dm_odm);
	}
}

#else

void
phydm_RA_debug_PCR(
	void		*p_dm_void,
	u32		*const dm_value,
	u32		*_used,
	char			*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	u32 used = *_used;
	u32 out_len = *_out_len;

	if (dm_value[0] == 100) {
		PHYDM_SNPRINTF((output + used, out_len - used, "[Get] PCR RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));
		/**/
	} else if (dm_value[0] == 0) {
		p_ra_table->RA_offset_direction = 0;
		p_ra_table->RA_threshold_offset = (u8)dm_value[1];
		PHYDM_SNPRINTF((output + used, out_len - used, "[Set] PCR RA_threshold_offset = (( -%d ))\n", p_ra_table->RA_threshold_offset));
	} else if (dm_value[0] == 1) {
		p_ra_table->RA_offset_direction = 1;
		p_ra_table->RA_threshold_offset = (u8)dm_value[1];
		PHYDM_SNPRINTF((output + used, out_len - used, "[Set] PCR RA_threshold_offset = (( +%d ))\n", p_ra_table->RA_threshold_offset));
	} else {
		PHYDM_SNPRINTF((output + used, out_len - used, "[Set] Error\n"));
		/**/
	}

}

#endif /*#if (defined(CONFIG_RA_DBG_CMD))*/

void
odm_c2h_ra_para_report_handler(
	void	*p_dm_void,
	u8	*cmd_buf,
	u8	cmd_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	u8	para_idx = cmd_buf[0]; /*Retry Penalty, NH, NL*/
	u8	rate_type_start = cmd_buf[1];
	u8	rate_type_length = cmd_len - 2;
	u8	i;


	ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[ From FW C2H RA Para ]  cmd_buf[0]= (( %d ))\n", cmd_buf[0]));

#if (defined(CONFIG_RA_DBG_CMD))
	if (para_idx == RADBG_RTY_PENALTY) {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index|   |RTY Penality index|\n"));

		for (i = 0 ; i < (rate_type_length) ; i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RTY_P_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RTY_P[rate_type_start + i] = cmd_buf[2 + i];
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RTY_P[rate_type_start + i]));
		}

	} else	if (para_idx == RADBG_N_HIGH) {
		/**/
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index|    |N-High|\n"));


	} else if (para_idx == RADBG_N_LOW) {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index|   |N-Low|\n"));
		/**/
	} else if (para_idx == RADBG_RATE_UP_RTY_RATIO) {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index|   |rate Up RTY Ratio|\n"));

		for (i = 0; i < (rate_type_length); i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RATE_UP_RTY_RATIO_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RATE_UP_RTY_RATIO[rate_type_start + i] = cmd_buf[2 + i];
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RATE_UP_RTY_RATIO[rate_type_start + i]));
		}
	} else	 if (para_idx == RADBG_RATE_DOWN_RTY_RATIO) {
		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" |rate index|   |rate Down RTY Ratio|\n"));

		for (i = 0; i < (rate_type_length); i++) {
			if (p_ra_table->is_ra_dbg_init)
				p_ra_table->RATE_DOWN_RTY_RATIO_default[rate_type_start + i] = cmd_buf[2 + i];

			p_ra_table->RATE_DOWN_RTY_RATIO[rate_type_start + i] = cmd_buf[2 + i];
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("%8d  %15d\n", (rate_type_start + i), p_ra_table->RATE_DOWN_RTY_RATIO[rate_type_start + i]));
		}
	} else
#endif
		if (para_idx == RADBG_DEBUG_MONITOR1) {
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("-------------------------------\n"));
			if (p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES) {

				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "RSSI =", cmd_buf[1]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "rate =", cmd_buf[2] & 0x7f));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "SGI =", (cmd_buf[2] & 0x80) >> 7));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "BW =", cmd_buf[3]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "BW_max =", cmd_buf[4]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "multi_rate0 =", cmd_buf[5]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "multi_rate1 =", cmd_buf[6]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "DISRA =",	cmd_buf[7]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "VHT_EN =", cmd_buf[8]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "SGI_support =",	cmd_buf[9]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "try_ness =", cmd_buf[10]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "pre_rate =", cmd_buf[11]));
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "RSSI =", cmd_buf[1]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %x\n", "BW =", cmd_buf[2]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "DISRA =", cmd_buf[3]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "VHT_EN =", cmd_buf[4]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "Hightest rate =", cmd_buf[5]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "Lowest rate =", cmd_buf[6]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "SGI_support =", cmd_buf[7]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "Rate_ID =",	cmd_buf[8]));;
			}
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("-------------------------------\n"));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR2) {
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("-------------------------------\n"));
			if (p_dm_odm->support_ic_type & PHYDM_IC_3081_SERIES) {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "rate_id =", cmd_buf[1]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "highest_rate =", cmd_buf[2]));
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "lowest_rate =", cmd_buf[3]));

				for (i = 4; i <= 11; i++)
					ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("RAMASK =  0x%x\n", cmd_buf[i]));
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %x%x  %x%x  %x%x  %x%x\n", "RA Mask:",
					cmd_buf[8], cmd_buf[7], cmd_buf[6], cmd_buf[5], cmd_buf[4], cmd_buf[3], cmd_buf[2], cmd_buf[1]));
			}
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("-------------------------------\n"));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR3) {

			for (i = 0; i < (cmd_len - 1); i++)
				ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("content[%d] = %d\n", i, cmd_buf[1 + i]));
		} else	 if (para_idx == RADBG_DEBUG_MONITOR4)
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  {%d.%d}\n", "RA version =", cmd_buf[1], cmd_buf[2]));
		else if (para_idx == RADBG_DEBUG_MONITOR5) {
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "Current rate =", cmd_buf[1]));
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "Retry ratio =", cmd_buf[2]));
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  %d\n", "rate down ratio =", cmd_buf[3]));
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x\n", "highest rate =", cmd_buf[4]));
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  {0x%x 0x%x}\n", "Muti-try =", cmd_buf[5], cmd_buf[6]));
			ODM_RT_TRACE(p_dm_odm, ODM_FW_DEBUG_TRACE, ODM_DBG_LOUD, ("%5s  0x%x%x%x%x%x\n", "RA mask =", cmd_buf[11], cmd_buf[10], cmd_buf[9], cmd_buf[8], cmd_buf[7]));
		}
}

void
phydm_ra_dynamic_retry_count(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	struct sta_info		*p_entry;
	u8	i, retry_offset;
	u32	ma_rx_tp;

	if (!(p_dm_odm->support_ability & ODM_BB_DYNAMIC_ARFR))
		return;

	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("p_dm_odm->pre_b_noisy = %d\n", p_dm_odm->pre_b_noisy ));*/
	if (p_dm_odm->pre_b_noisy != p_dm_odm->noisy_decision) {

		if (p_dm_odm->noisy_decision) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("->Noisy Env. RA fallback value\n"));
			odm_set_mac_reg(p_dm_odm, 0x430, MASKDWORD, 0x0);
			odm_set_mac_reg(p_dm_odm, 0x434, MASKDWORD, 0x04030201);
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("->Clean Env. RA fallback value\n"));
			odm_set_mac_reg(p_dm_odm, 0x430, MASKDWORD, 0x01000000);
			odm_set_mac_reg(p_dm_odm, 0x434, MASKDWORD, 0x06050402);
		}
		p_dm_odm->pre_b_noisy = p_dm_odm->noisy_decision;
	}
}

#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))

void
phydm_retry_limit_table_bound(
	void	*p_dm_void,
	u8	*retry_limit,
	u8	offset
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;

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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	u8			i;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN) /*support all IC platform*/

#else
#if ((RTL8192E_SUPPORT == 1) || (RTL8723B_SUPPORT == 1) || (RTL8188E_SUPPORT == 1))
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

#elif (RTL8821A_SUPPORT == 1) || (RTL8881A_SUPPORT == 1)

#elif (RTL8812A_SUPPORT == 1)

#elif (RTL8814A_SUPPORT == 1)

#else

#endif
#endif

	memcpy(&(p_ra_table->per_rate_retrylimit_20M[0]), &(per_rate_retrylimit_table_20M[0]), ODM_NUM_RATE_IDX);
	memcpy(&(p_ra_table->per_rate_retrylimit_40M[0]), &(per_rate_retrylimit_table_40M[0]), ODM_NUM_RATE_IDX);

	for (i = 0; i < ODM_NUM_RATE_IDX; i++) {
		phydm_retry_limit_table_bound(p_dm_odm, &(p_ra_table->per_rate_retrylimit_20M[i]), 0);
		phydm_retry_limit_table_bound(p_dm_odm, &(p_ra_table->per_rate_retrylimit_40M[i]), 0);
	}
}

void
phydm_ra_dynamic_retry_limit_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	p_ra_table->retry_descend_num = RA_RETRY_DESCEND_NUM;
	p_ra_table->retrylimit_low = RA_RETRY_LIMIT_LOW;
	p_ra_table->retrylimit_high = RA_RETRY_LIMIT_HIGH;

	phydm_reset_retry_limit_table(p_dm_odm);

}

#endif

void
phydm_ra_dynamic_retry_limit(
	void	*p_dm_void
)
{
#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	struct sta_info		*p_entry;
	u8	i, retry_offset;
	u32	ma_rx_tp;


	if (p_dm_odm->pre_number_active_client == p_dm_odm->number_active_client) {

		ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, (" pre_number_active_client ==  number_active_client\n"));
		return;

	} else {
		if (p_dm_odm->number_active_client == 1) {
			phydm_reset_retry_limit_table(p_dm_odm);
			ODM_RT_TRACE(p_dm_odm, PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("one client only->reset to default value\n"));
		} else {

			retry_offset = p_dm_odm->number_active_client * p_ra_table->retry_descend_num;

			for (i = 0; i < ODM_NUM_RATE_IDX; i++) {

				phydm_retry_limit_table_bound(p_dm_odm, &(p_ra_table->per_rate_retrylimit_20M[i]), retry_offset);
				phydm_retry_limit_table_bound(p_dm_odm, &(p_ra_table->per_rate_retrylimit_40M[i]), retry_offset);
			}
		}
	}
#endif
}

#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))
void
phydm_ra_dynamic_rate_id_on_assoc(
	void	*p_dm_void,
	u8	wireless_mode,
	u8	init_rate_id
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("[ON ASSOC] rf_mode = ((0x%x)), wireless_mode = ((0x%x)), init_rate_id = ((0x%x))\n", p_dm_odm->rf_type, wireless_mode, init_rate_id));

	if ((p_dm_odm->rf_type == ODM_2T2R) | (p_dm_odm->rf_type == ODM_2T2R_GREEN) | (p_dm_odm->rf_type == ODM_2T3R) | (p_dm_odm->rf_type == ODM_2T4R)) {

		if ((p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E)) &&
		    (wireless_mode & (ODM_WM_N24G | ODM_WM_N5G))
		   ) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("[ON ASSOC] set N-2SS ARFR5 table\n"));
			odm_set_mac_reg(p_dm_odm, 0x4a4, MASKDWORD, 0xfc1ffff);	/*N-2SS, ARFR5, rate_id = 0xe*/
			odm_set_mac_reg(p_dm_odm, 0x4a8, MASKDWORD, 0x0);		/*N-2SS, ARFR5, rate_id = 0xe*/
		} else if ((p_dm_odm->support_ic_type & (ODM_RTL8812)) &&
			(wireless_mode & (ODM_WM_AC_5G | ODM_WM_AC_24G | ODM_WM_AC_ONLY))
			  ) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("[ON ASSOC] set AC-2SS ARFR0 table\n"));
			odm_set_mac_reg(p_dm_odm, 0x444, MASKDWORD, 0x0fff);	/*AC-2SS, ARFR0, rate_id = 0x9*/
			odm_set_mac_reg(p_dm_odm, 0x448, MASKDWORD, 0xff01f000);		/*AC-2SS, ARFR0, rate_id = 0x9*/
		}
	}

}

void
phydm_ra_dynamic_rate_id_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E)) {

		odm_set_mac_reg(p_dm_odm, 0x4a4, MASKDWORD, 0xfc1ffff);	/*N-2SS, ARFR5, rate_id = 0xe*/
		odm_set_mac_reg(p_dm_odm, 0x4a8, MASKDWORD, 0x0);		/*N-2SS, ARFR5, rate_id = 0xe*/

		odm_set_mac_reg(p_dm_odm, 0x444, MASKDWORD, 0x0fff);		/*AC-2SS, ARFR0, rate_id = 0x9*/
		odm_set_mac_reg(p_dm_odm, 0x448, MASKDWORD, 0xff01f000);	/*AC-2SS, ARFR0, rate_id = 0x9*/
	}
}

void
phydm_update_rate_id(
	void	*p_dm_void,
	u8	rate,
	u8	platform_macid
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	u8		current_tx_ss;
	u8		rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8		wireless_mode;
	u8		phydm_macid;
	struct sta_info	*p_entry;


#if	0
	if (rate_idx >= ODM_RATEVHTSS2MCS0) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate[%d]: (( VHT2SS-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEVHTSS2MCS0)));
		/*dummy for SD4 check patch*/
	} else if (rate_idx >= ODM_RATEVHTSS1MCS0) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate[%d]: (( VHT1SS-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEVHTSS1MCS0)));
		/*dummy for SD4 check patch*/
	} else if (rate_idx >= ODM_RATEMCS0) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate[%d]: (( HT-MCS%d ))\n", platform_macid, (rate_idx - ODM_RATEMCS0)));
		/*dummy for SD4 check patch*/
	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate[%d]: (( HT-MCS%d ))\n", platform_macid, rate_idx));
		/*dummy for SD4 check patch*/
	}
#endif

	phydm_macid = p_dm_odm->platform2phydm_macid_table[platform_macid];
	p_entry = p_dm_odm->p_odm_sta_info[phydm_macid];

	if (IS_STA_VALID(p_entry)) {
		wireless_mode = p_entry->wireless_mode;

		if ((p_dm_odm->rf_type  == ODM_2T2R) | (p_dm_odm->rf_type  == ODM_2T2R_GREEN) | (p_dm_odm->rf_type  == ODM_2T3R) | (p_dm_odm->rf_type  == ODM_2T4R)) {

			p_entry->ratr_idx = p_entry->ratr_idx_init;
			if (wireless_mode & (ODM_WM_N24G | ODM_WM_N5G)) { /*N mode*/
				if (rate_idx >= ODM_RATEMCS8 && rate_idx <= ODM_RATEMCS15) { /*2SS mode*/

					p_entry->ratr_idx = ARFR_5_RATE_ID;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("ARFR_5\n"));
				}
			} else if (wireless_mode & (ODM_WM_AC_5G | ODM_WM_AC_24G | ODM_WM_AC_ONLY)) {/*AC mode*/
				if (rate_idx >= ODM_RATEVHTSS2MCS0 && rate_idx <= ODM_RATEVHTSS2MCS9) {/*2SS mode*/

					p_entry->ratr_idx = ARFR_0_RATE_ID;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("ARFR_0\n"));
				}
			}
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("UPdate_RateID[%d]: (( 0x%x ))\n", platform_macid, p_entry->ratr_idx));
		}
	}

}
#endif

void
phydm_print_rate(
	void	*p_dm_void,
	u8	rate,
	u32	dbg_component
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8		legacy_table[12] = {1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54};
	u8		rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8		vht_en = (rate_idx >= ODM_RATEVHTSS1MCS0) ? 1 : 0;
	u8		b_sgi = (rate & 0x80) >> 7;

	ODM_RT_TRACE_F(p_dm_odm, dbg_component, ODM_DBG_LOUD, ("( %s%s%s%s%d%s%s)\n",
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
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	u8	legacy_table[12] = {1, 2, 5, 11, 6, 9, 12, 18, 24, 36, 48, 54};
	u8	macid = cmd_buf[1];
	u8	rate = cmd_buf[0];
	u8	rate_idx = rate & 0x7f; /*remove bit7 SGI*/
	u8	pre_rate = p_ra_table->link_tx_rate[macid];
	u8	rate_order;
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm_odm->adapter;

	GET_HAL_DATA(adapter)->CurrentRARate = HwRateToMRate(rate_idx);
#endif


	if (cmd_len >= 4) {
		if (cmd_buf[3] == 0) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("TX Init-rate Update[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 0xff) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("FW Level: Fix rate[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 1) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Try Success[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 2) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Try Fail & Try Again[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 3) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("rate Back[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 4) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("start rate by RSSI[%d]:", macid));
			/**/
		} else if (cmd_buf[3] == 5) {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Try rate[%d]:", macid));
			/**/
		}
	} else {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("Tx rate Update[%d]:", macid));
		/**/
	}

	/*phydm_print_rate(p_dm_odm, pre_rate_idx, ODM_COMP_RATE_ADAPTIVE);*/
	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, (">\n",macid );*/
	phydm_print_rate(p_dm_odm, rate, ODM_COMP_RATE_ADAPTIVE);

	p_ra_table->link_tx_rate[macid] = rate;

	/*trigger power training*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

	rate_order = phydm_rate_order_compute(p_dm_odm, rate_idx);

	if ((p_dm_odm->is_one_entry_only) ||
	    ((rate_order > p_ra_table->highest_client_tx_order) && (p_ra_table->power_tracking_flag == 1))
	   ) {
		phydm_update_pwr_track(p_dm_odm, rate_idx);
		p_ra_table->power_tracking_flag = 0;
	}

#endif

	/*trigger dynamic rate ID*/
#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))
	if (p_dm_odm->support_ic_type & (ODM_RTL8812 | ODM_RTL8192E))
		phydm_update_rate_id(p_dm_odm, rate, macid);
#endif

}

void
odm_rssi_monitor_init(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	p_ra_table->PT_collision_pre = true;	/*used in odm_dynamic_arfb_select(WIN only)*/

	p_hal_data->UndecoratedSmoothedPWDB = -1;
	p_hal_data->ra_rpt_linked = false;
#endif

	p_ra_table->firstconnect = false;


#endif
}

void
odm_ra_post_action_on_assoc(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	/*
		p_dm_odm->h2c_rarpt_connect = 1;
		odm_rssi_monitor_check(p_dm_odm);
		p_dm_odm->h2c_rarpt_connect = 0;
	*/
}

void
phydm_init_ra_info(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

#if (RTL8822B_SUPPORT == 1)
	if (p_dm_odm->support_ic_type == ODM_RTL8822B) {
		u32	ret_value;

		ret_value = odm_get_bb_reg(p_dm_odm, 0x4c8, MASKBYTE2);
		odm_set_bb_reg(p_dm_odm, 0x4cc, MASKBYTE3, (ret_value - 1));
	}
#endif
}

void
phydm_modify_RA_PCR_threshold(
	void		*p_dm_void,
	u8		RA_offset_direction,
	u8		RA_threshold_offset

)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	p_ra_table->RA_offset_direction = RA_offset_direction;
	p_ra_table->RA_threshold_offset = RA_threshold_offset;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Set RA_threshold_offset = (( %s%d ))\n", ((RA_threshold_offset == 0) ? " " : ((RA_offset_direction) ? "+" : "-")), RA_threshold_offset));
}

void
odm_rssi_monitor_check_mp(
	void	*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	u8			h2c_parameter[H2C_0X42_LENGTH] = {0};
	u32			i;
	boolean			is_ext_ra_info = true;
	u8			cmdlen = H2C_0X42_LENGTH;
	u8			tx_bf_en = 0, stbc_en = 0;

	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct sta_info		*p_entry = NULL;
	s32			tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;
	PMGNT_INFO		p_mgnt_info = &adapter->MgntInfo;
	PMGNT_INFO		p_default_mgnt_info = &adapter->MgntInfo;
	u64			cur_tx_ok_cnt = 0, cur_rx_ok_cnt = 0;
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
	enum beamforming_cap beamform_cap = BEAMFORMING_CAP_NONE;
#endif
#endif
	struct _ADAPTER	*p_loop_adapter = GetDefaultAdapter(adapter);

	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
		is_ext_ra_info = false;
		cmdlen = 3;
	}

	while (p_loop_adapter) {

		if (p_loop_adapter != NULL) {
			p_mgnt_info = &p_loop_adapter->MgntInfo;
			cur_tx_ok_cnt = p_loop_adapter->TxStats.NumTxBytesUnicast - p_mgnt_info->lastTxOkCnt;
			cur_rx_ok_cnt = p_loop_adapter->RxStats.NumRxBytesUnicast - p_mgnt_info->lastRxOkCnt;
			p_mgnt_info->lastTxOkCnt = cur_tx_ok_cnt;
			p_mgnt_info->lastRxOkCnt = cur_rx_ok_cnt;
		}

		for (i = 0; i < ASSOCIATE_ENTRY_NUM; i++) {

			if (IsAPModeExist(p_loop_adapter)) {
				if (GetFirstExtAdapter(p_loop_adapter) != NULL &&
				    GetFirstExtAdapter(p_loop_adapter) == p_loop_adapter)
				p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
				else if (GetFirstGOPort(p_loop_adapter) != NULL &&
					 IsFirstGoAdapter(p_loop_adapter))
				p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
			} else {
				if (GetDefaultAdapter(p_loop_adapter) == p_loop_adapter)
					p_entry = AsocEntry_EnumStation(p_loop_adapter, i);
			}

			if (p_entry != NULL) {
				if (p_entry->bAssociated) {

					RT_DISP_ADDR(FDM, DM_PWDB, ("p_entry->mac_addr ="), p_entry->MacAddr);
					RT_DISP(FDM, DM_PWDB, ("p_entry->rssi = 0x%x(%d)\n",
						p_entry->rssi_stat.undecorated_smoothed_pwdb, p_entry->rssi_stat.undecorated_smoothed_pwdb));

					/* 2 BF_en */
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
					beamform_cap = phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm_odm, p_entry->AssociatedMacId);
					if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
						tx_bf_en = 1;
#else
					if (Beamform_GetSupportBeamformerCap(GetDefaultAdapter(adapter), p_entry))
						tx_bf_en = 1;
#endif
#endif
					/* 2 STBC_en */
					if ((IS_WIRELESS_MODE_AC(adapter) && TEST_FLAG(p_entry->VHTInfo.STBC, STBC_VHT_ENABLE_TX)) ||
					    TEST_FLAG(p_entry->HTInfo.STBC, STBC_HT_ENABLE_TX))
						stbc_en = 1;

					if (p_entry->rssi_stat.undecorated_smoothed_pwdb < tmp_entry_min_pwdb)
						tmp_entry_min_pwdb = p_entry->rssi_stat.undecorated_smoothed_pwdb;
					if (p_entry->rssi_stat.undecorated_smoothed_pwdb > tmp_entry_max_pwdb)
						tmp_entry_max_pwdb = p_entry->rssi_stat.undecorated_smoothed_pwdb;

					h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

					if (is_ext_ra_info) {
						if (cur_rx_ok_cnt > (cur_tx_ok_cnt * 6))
							h2c_parameter[3] |= RAINFO_BE_RX_STATE;

						if (tx_bf_en)
							h2c_parameter[3] |= RAINFO_BF_STATE;
						else {
							if (stbc_en)
								h2c_parameter[3] |= RAINFO_STBC_STATE;
						}

						if (p_dm_odm->noisy_decision)
							h2c_parameter[3] |= RAINFO_NOISY_STATE;
						else
							h2c_parameter[3] &= (~RAINFO_NOISY_STATE);
#if 1
						if (p_dm_odm->h2c_rarpt_connect) {
							h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
							ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("h2c_rarpt_connect = (( %d ))\n", p_dm_odm->h2c_rarpt_connect));
						}
#else

						if (p_entry->rssi_stat.ra_rpt_linked == false) {
							h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
							p_entry->rssi_stat.ra_rpt_linked = true;

							ODM_RT_TRACE(p_dm_odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("RA First Link, RSSI[%d] = ((%d))\n",
								p_entry->associated_mac_id, p_entry->rssi_stat.undecorated_smoothed_pwdb));
						}
#endif
					}

					h2c_parameter[2] = (u8)(p_entry->rssi_stat.undecorated_smoothed_pwdb & 0xFF);
					/* h2c_parameter[1] = 0x20;   */ /* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1 */
					h2c_parameter[0] = (p_entry->AssociatedMacId);

					odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
				}
			} else
				break;
		}

		p_loop_adapter = GetNextExtAdapter(p_loop_adapter);
	}


	/*Default port*/
	if (tmp_entry_max_pwdb != 0) {	/* If associated entry is found */
		p_hal_data->EntryMaxUndecoratedSmoothedPWDB = tmp_entry_max_pwdb;
		RT_DISP(FDM, DM_PWDB, ("EntryMaxPWDB = 0x%x(%d)\n",	tmp_entry_max_pwdb, tmp_entry_max_pwdb));
	} else
		p_hal_data->EntryMaxUndecoratedSmoothedPWDB = 0;

	if (tmp_entry_min_pwdb != 0xff) { /* If associated entry is found */
		p_hal_data->EntryMinUndecoratedSmoothedPWDB = tmp_entry_min_pwdb;
		RT_DISP(FDM, DM_PWDB, ("EntryMinPWDB = 0x%x(%d)\n", tmp_entry_min_pwdb, tmp_entry_min_pwdb));

	} else
		p_hal_data->EntryMinUndecoratedSmoothedPWDB = 0;

	/* Default porti sent RSSI to FW */
	if (p_hal_data->bUseRAMask) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("1 RA First Link, RSSI[%d] = ((%d)) , ra_rpt_linked = ((%d))\n",
			WIN_DEFAULT_PORT_MACID, p_hal_data->UndecoratedSmoothedPWDB, p_hal_data->ra_rpt_linked));
		if (p_hal_data->UndecoratedSmoothedPWDB > 0) {

			PRT_HIGH_THROUGHPUT			p_ht_info = GET_HT_INFO(p_default_mgnt_info);
			PRT_VERY_HIGH_THROUGHPUT	p_vht_info = GET_VHT_INFO(p_default_mgnt_info);

			/* BF_en*/
#if (BEAMFORMING_SUPPORT == 1)
#ifndef BEAMFORMING_VERSION_1
			beamform_cap = phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm_odm, p_default_mgnt_info->m_mac_id);

			if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
				tx_bf_en = 1;
#else
			if (Beamform_GetSupportBeamformerCap(GetDefaultAdapter(adapter), NULL))
				tx_bf_en = 1;
#endif
#endif

			/* STBC_en*/
			if ((IS_WIRELESS_MODE_AC(adapter) && TEST_FLAG(p_vht_info->VhtCurStbc, STBC_VHT_ENABLE_TX)) ||
			    TEST_FLAG(p_ht_info->HtCurStbc, STBC_HT_ENABLE_TX))
				stbc_en = 1;

			h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

			if (is_ext_ra_info) {
				if (tx_bf_en)
					h2c_parameter[3] |= RAINFO_BF_STATE;
				else {
					if (stbc_en)
						h2c_parameter[3] |= RAINFO_STBC_STATE;
				}

#if 1
				if (p_dm_odm->h2c_rarpt_connect) {
					h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("h2c_rarpt_connect = (( %d ))\n", p_dm_odm->h2c_rarpt_connect));
				}
#else
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("2 RA First Link, RSSI[%d] = ((%d)) , ra_rpt_linked = ((%d))\n",
					WIN_DEFAULT_PORT_MACID, p_hal_data->undecorated_smoothed_pwdb, p_hal_data->ra_rpt_linked));

				if (p_hal_data->ra_rpt_linked == false) {

					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RSSI_MONITOR, ODM_DBG_LOUD, ("3 RA First Link, RSSI[%d] = ((%d)) , ra_rpt_linked = ((%d))\n",
						WIN_DEFAULT_PORT_MACID, p_hal_data->undecorated_smoothed_pwdb, p_hal_data->ra_rpt_linked));

					h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
					p_hal_data->ra_rpt_linked = true;


				}
#endif

				if (p_dm_odm->noisy_decision == 1) {
					h2c_parameter[3] |= RAINFO_NOISY_STATE;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_NOISY_DETECT, ODM_DBG_LOUD, ("[RSSIMonitorCheckMP] Send H2C to FW\n"));
				} else
					h2c_parameter[3] &= (~RAINFO_NOISY_STATE);

				ODM_RT_TRACE(p_dm_odm, ODM_COMP_NOISY_DETECT, ODM_DBG_LOUD, ("[RSSIMonitorCheckMP] h2c_parameter=%x\n", h2c_parameter[3]));
			}

			h2c_parameter[2] = (u8)(p_hal_data->UndecoratedSmoothedPWDB & 0xFF);
			/*h2c_parameter[1] = 0x20;*/	/* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1*/
			h2c_parameter[0] = WIN_DEFAULT_PORT_MACID;		/* fw v12 cmdid 5:use max macid ,for nic ,default macid is 0 ,max macid is 1*/

			odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
		}

		/* BT 3.0 HS mode rssi */
		if (p_dm_odm->is_bt_hs_operation) {
			h2c_parameter[2] = p_dm_odm->bt_hs_rssi;
			/* h2c_parameter[1] = 0x0; */
			h2c_parameter[0] = WIN_BT_PORT_MACID;

			odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
		}
	} else
		PlatformEFIOWrite1Byte(adapter, 0x4fe, (u8)p_hal_data->UndecoratedSmoothedPWDB);

	if ((p_dm_odm->support_ic_type == ODM_RTL8812) || (p_dm_odm->support_ic_type == ODM_RTL8192E))
		odm_rssi_dump_to_register(p_dm_odm);


	{
		struct _ADAPTER *p_loop_adapter = GetDefaultAdapter(adapter);
		boolean		default_pointer_value, *p_is_link_temp = &default_pointer_value;
		s32	global_rssi_min = 0xFF, local_rssi_min;
		boolean		is_link = false;

		while (p_loop_adapter) {
			local_rssi_min = phydm_find_minimum_rssi(p_dm_odm, p_loop_adapter, p_is_link_temp);
			/* dbg_print("p_hal_data->is_linked=%d, local_rssi_min=%d\n", p_hal_data->is_linked, local_rssi_min); */

			if (*p_is_link_temp)
				is_link = true;

			if ((local_rssi_min < global_rssi_min) && (*p_is_link_temp))
				global_rssi_min = local_rssi_min;

			p_loop_adapter = GetNextExtAdapter(p_loop_adapter);
		}

		p_hal_data->bLinked = is_link;
		odm_cmn_info_update(&p_hal_data->DM_OutSrc, ODM_CMNINFO_LINK, (u64)is_link);

		if (is_link)
			odm_cmn_info_update(&p_hal_data->DM_OutSrc, ODM_CMNINFO_RSSI_MIN, (u64)global_rssi_min);
		else
			odm_cmn_info_update(&p_hal_data->DM_OutSrc, ODM_CMNINFO_RSSI_MIN, 0);

	}

#endif	/*  #if (DM_ODM_SUPPORT_TYPE == ODM_WIN) */
}

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
/*H2C_RSSI_REPORT*/
s8 phydm_rssi_report(struct PHY_DM_STRUCT *p_dm_odm, u8 mac_id)
{
	struct _ADAPTER *adapter = p_dm_odm->adapter;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapter);
	HAL_DATA_TYPE *p_hal_data = GET_HAL_DATA(adapter);
	u8 h2c_parameter[H2C_0X42_LENGTH] = {0};
	u8 UL_DL_STATE = 0, STBC_TX = 0, tx_bf_en = 0;
	u8 cmdlen = H2C_0X42_LENGTH, first_connect = _FALSE;
	u64	cur_tx_ok_cnt = 0, cur_rx_ok_cnt = 0;
	struct sta_info *p_entry = p_dm_odm->p_odm_sta_info[mac_id];

	if (!IS_STA_VALID(p_entry))
		return _FAIL;

	if (mac_id != p_entry->mac_id) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("%s mac_id:%u:%u invalid\n", __func__, mac_id, p_entry->mac_id));
		rtw_warn_on(1);
		return _FAIL;
	}

	if (IS_MCAST(p_entry->hwaddr))  /*if(psta->mac_id ==1)*/
		return _FAIL;

	if (p_dm_odm->is_in_lps_pg)
		return _FAIL;

	if (p_entry->rssi_stat.undecorated_smoothed_pwdb == (-1)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("%s mac_id:%u, mac:"MAC_FMT", rssi == -1\n", __func__, p_entry->mac_id, MAC_ARG(p_entry->hwaddr)));
		return _FAIL;
	}

	cur_tx_ok_cnt = pdvobjpriv->traffic_stat.cur_tx_bytes;
	cur_rx_ok_cnt = pdvobjpriv->traffic_stat.cur_rx_bytes;
	if (cur_rx_ok_cnt > (cur_tx_ok_cnt * 6))
		UL_DL_STATE = 1;
	else
		UL_DL_STATE = 0;

#ifdef CONFIG_BEAMFORMING
	{
#if (BEAMFORMING_SUPPORT == 1)
		enum beamforming_cap beamform_cap = phydm_beamforming_get_entry_beam_cap_by_mac_id(p_dm_odm, p_entry->mac_id);
#else/*for drv beamforming*/
		enum beamforming_cap beamform_cap = beamforming_get_entry_beam_cap_by_mac_id(&adapter->mlmepriv, p_entry->mac_id);
#endif

		if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))
			tx_bf_en = 1;
		else
			tx_bf_en = 0;
	}
#endif /*#ifdef CONFIG_BEAMFORMING*/

	if (tx_bf_en)
		STBC_TX = 0;
	else {
#ifdef CONFIG_80211AC_VHT
		if (is_supported_vht(p_entry->wireless_mode))
			STBC_TX = TEST_FLAG(p_entry->vhtpriv.stbc_cap, STBC_VHT_ENABLE_TX);
		else
#endif
			STBC_TX = TEST_FLAG(p_entry->htpriv.stbc_cap, STBC_HT_ENABLE_TX);
	}

	h2c_parameter[0] = (u8)(p_entry->mac_id & 0xFF);
	h2c_parameter[2] = p_entry->rssi_stat.undecorated_smoothed_pwdb & 0x7F;

	if (UL_DL_STATE)
		h2c_parameter[3] |= RAINFO_BE_RX_STATE;

	if (tx_bf_en)
		h2c_parameter[3] |= RAINFO_BF_STATE;
	if (STBC_TX)
		h2c_parameter[3] |= RAINFO_STBC_STATE;
	if (p_dm_odm->noisy_decision)
		h2c_parameter[3] |= RAINFO_NOISY_STATE;

	if ((p_entry->ra_rpt_linked == _FALSE) && (p_entry->rssi_stat.is_send_rssi == RA_RSSI_STATE_SEND)) {
		h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
		p_entry->ra_rpt_linked = _TRUE;
		p_entry->rssi_stat.is_send_rssi = RA_RSSI_STATE_HOLD;
		first_connect = _TRUE;
	}

	h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

#if 1
	if (first_connect) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("%s mac_id:%u, mac:"MAC_FMT", rssi:%d\n", __func__,
			p_entry->mac_id, MAC_ARG(p_entry->hwaddr), p_entry->rssi_stat.undecorated_smoothed_pwdb));

		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("%s RAINFO - TP:%s, TxBF:%s, STBC:%s, Noisy:%s, Firstcont:%s\n", __func__,
			(UL_DL_STATE) ? "DL" : "UL", (tx_bf_en) ? "EN" : "DIS", (STBC_TX) ? "EN" : "DIS",
			(p_dm_odm->noisy_decision) ? "True" : "False", (first_connect) ? "True" : "False"));
	}
#endif

	if (p_hal_data->fw_ractrl == _TRUE) {
#if (RTL8188E_SUPPORT == 1)
		if (p_dm_odm->support_ic_type == ODM_RTL8188E)
			cmdlen = 3;
#endif
		odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);
	} else {
#if ((RTL8188E_SUPPORT == 1) && (RATE_ADAPTIVE_SUPPORT == 1))
		if (p_dm_odm->support_ic_type == ODM_RTL8188E)
			odm_ra_set_rssi_8188e(p_dm_odm, (u8)(p_entry->mac_id & 0xFF), p_entry->rssi_stat.undecorated_smoothed_pwdb & 0x7F);
#endif
	}
	return _SUCCESS;
}

void phydm_ra_rssi_rpt_wk_hdl(void *p_context)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_context;
	int i;
	u8 mac_id = 0xFF;
	struct sta_info	*p_entry = NULL;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_entry = p_dm_odm->p_odm_sta_info[i];
		if (IS_STA_VALID(p_entry)) {
			if (IS_MCAST(p_entry->hwaddr))  /*if(psta->mac_id ==1)*/
				continue;
			if (p_entry->ra_rpt_linked == _FALSE) {
				mac_id = i;
				break;
			}
		}
	}
	if (mac_id != 0xFF)
		phydm_rssi_report(p_dm_odm, mac_id);
}
void phydm_ra_rssi_rpt_wk(void *p_context)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_context;

	rtw_run_in_thread_cmd(p_dm_odm->adapter, phydm_ra_rssi_rpt_wk_hdl, p_dm_odm);
}
#endif

void
odm_rssi_monitor_check_ce(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct sta_info           *p_entry;
	int	i;
	int	tmp_entry_max_pwdb = 0, tmp_entry_min_pwdb = 0xff;
	u8	sta_cnt = 0;

	if (p_dm_odm->is_linked != _TRUE)
		return;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_entry = p_dm_odm->p_odm_sta_info[i];
		if (IS_STA_VALID(p_entry)) {
			if (IS_MCAST(p_entry->hwaddr))  /*if(psta->mac_id ==1)*/
				continue;

			if (p_entry->rssi_stat.undecorated_smoothed_pwdb == (-1))
				continue;

			if (p_entry->rssi_stat.undecorated_smoothed_pwdb < tmp_entry_min_pwdb)
				tmp_entry_min_pwdb = p_entry->rssi_stat.undecorated_smoothed_pwdb;

			if (p_entry->rssi_stat.undecorated_smoothed_pwdb > tmp_entry_max_pwdb)
				tmp_entry_max_pwdb = p_entry->rssi_stat.undecorated_smoothed_pwdb;

			if (phydm_rssi_report(p_dm_odm, i))
				sta_cnt++;
		}
	}

	if (tmp_entry_max_pwdb != 0)	/* If associated entry is found */
		p_hal_data->entry_max_undecorated_smoothed_pwdb = tmp_entry_max_pwdb;
	else
		p_hal_data->entry_max_undecorated_smoothed_pwdb = 0;

	if (tmp_entry_min_pwdb != 0xff) /* If associated entry is found */
		p_hal_data->entry_min_undecorated_smoothed_pwdb = tmp_entry_min_pwdb;
	else
		p_hal_data->entry_min_undecorated_smoothed_pwdb = 0;

	find_minimum_rssi(adapter);/* get pdmpriv->min_undecorated_pwdb_for_dm */

	p_dm_odm->rssi_min = p_hal_data->min_undecorated_pwdb_for_dm;
	/* odm_cmn_info_update(&p_hal_data->odmpriv,ODM_CMNINFO_RSSI_MIN, pdmpriv->min_undecorated_pwdb_for_dm); */
#endif/* if (DM_ODM_SUPPORT_TYPE == ODM_CE) */
}


void
odm_rssi_monitor_check_ap(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#if (RTL8812A_SUPPORT || RTL8881A_SUPPORT || RTL8192E_SUPPORT || RTL8814A_SUPPORT || RTL8197F_SUPPORT)

	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	u8			h2c_parameter[H2C_0X42_LENGTH] = {0};
	u32			 i;
	boolean			is_ext_ra_info = true;
	u8			cmdlen = H2C_0X42_LENGTH;
	u8			tx_bf_en = 0, stbc_en = 0;

	struct rtl8192cd_priv	*priv		= p_dm_odm->priv;
	struct sta_info	*pstat;
	boolean			act_bfer = false;

#if (BEAMFORMING_SUPPORT == 1)
	u8	idx = 0xff;
#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY))
	struct _BF_DIV_COEX_	*p_dm_bdc_table = &p_dm_odm->dm_bdc_table;
	p_dm_bdc_table->num_txbfee_client = 0;
	p_dm_bdc_table->num_txbfer_client = 0;
#endif
#endif
	if (!p_dm_odm->h2c_rarpt_connect && (priv->up_time % 2))
		return;

	if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
		is_ext_ra_info = false;
		cmdlen = 3;
	}

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		pstat = p_dm_odm->p_odm_sta_info[i];

		if (IS_STA_VALID(pstat)) {
			if (pstat->sta_in_firmware != 1)
				continue;

			/* 2 BF_en */
#if (BEAMFORMING_SUPPORT == 1)
			BEAMFORMING_CAP beamform_cap = Beamforming_GetEntryBeamCapByMacId(priv, pstat->aid);
			PRT_BEAMFORMING_ENTRY	p_entry = Beamforming_GetEntryByMacId(priv, pstat->aid, &idx);

			if (beamform_cap & (BEAMFORMER_CAP_HT_EXPLICIT | BEAMFORMER_CAP_VHT_SU))	{

				if (p_entry->Sounding_En)
					tx_bf_en = 1;
				else
					tx_bf_en = 0;

				act_bfer = true;
			}

#if (defined(CONFIG_PHYDM_ANTENNA_DIVERSITY)) /*BDC*/
			if (act_bfer == true) {
				p_dm_bdc_table->w_bfee_client[i] = 1; /* AP act as BFer */
				p_dm_bdc_table->num_txbfee_client++;
			} else {
				p_dm_bdc_table->w_bfee_client[i] = 0; /* AP act as BFer */
			}

			if ((beamform_cap & BEAMFORMEE_CAP_HT_EXPLICIT) || (beamform_cap & BEAMFORMEE_CAP_VHT_SU)) {
				p_dm_bdc_table->w_bfer_client[i] = 1; /* AP act as BFee */
				p_dm_bdc_table->num_txbfer_client++;
			} else {
				p_dm_bdc_table->w_bfer_client[i] = 0; /* AP act as BFer */
			}
#endif
#endif

			/* 2 STBC_en */
			if ((priv->pmib->dot11nConfigEntry.dot11nSTBC) &&
			    ((pstat->ht_cap_buf.ht_cap_info & cpu_to_le16(_HTCAP_RX_STBC_CAP_))
#ifdef RTK_AC_SUPPORT
			     || (pstat->vht_cap_buf.vht_cap_info & cpu_to_le32(_VHTCAP_RX_STBC_CAP_))
#endif
			    ))
				stbc_en = 1;

			/* 2 RAINFO */

			h2c_parameter[4] = (p_ra_table->RA_threshold_offset & 0x7f) | (p_ra_table->RA_offset_direction << 7);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RA_threshold_offset = (( %s%d ))\n", ((p_ra_table->RA_threshold_offset == 0) ? " " : ((p_ra_table->RA_offset_direction) ? "+" : "-")), p_ra_table->RA_threshold_offset));

			if (is_ext_ra_info) {
				if ((pstat->rx_avarage)  > ((pstat->tx_avarage) * 6))
					h2c_parameter[3] |= RAINFO_BE_RX_STATE;

				if (tx_bf_en)
					h2c_parameter[3] |= RAINFO_BF_STATE;
				else {
					if (stbc_en)
						h2c_parameter[3] |= RAINFO_STBC_STATE;
				}

				if (p_dm_odm->noisy_decision)
					h2c_parameter[3] |= RAINFO_NOISY_STATE;
				else
					h2c_parameter[3] &= (~RAINFO_NOISY_STATE);

				if (pstat->H2C_rssi_rpt) {
					h2c_parameter[3] |= RAINFO_INIT_RSSI_RATE_STATE;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("[RA Init] set Init rate by RSSI, STA %d\n", pstat->aid));
				}

				/*ODM_RT_TRACE(p_dm_odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[RAINFO] H2C_Para[3] = %x\n",h2c_parameter[3]));*/
			}

			h2c_parameter[2] = (u8)(pstat->rssi & 0xFF);
			h2c_parameter[0] = REMAP_AID(pstat);

			ODM_RT_TRACE(p_dm_odm, ODM_COMP_COMMON, ODM_DBG_LOUD, ("h2c_parameter[3]=%d\n", h2c_parameter[3]));

			/* ODM_RT_TRACE(p_dm_odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[RSSI] H2C_Para[2] = %x,\n",h2c_parameter[2])); */
			/* ODM_RT_TRACE(p_dm_odm,PHYDM_COMP_RA_DBG, ODM_DBG_LOUD, ("[MACID] H2C_Para[0] = %x,\n",h2c_parameter[0])); */

			odm_fill_h2c_cmd(p_dm_odm, ODM_H2C_RSSI_REPORT, cmdlen, h2c_parameter);

		}
	}

#endif
#endif

}

void
odm_rssi_monitor_check(
	void		*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (!(p_dm_odm->support_ability & ODM_BB_RSSI_MONITOR))
		return;

	switch	(p_dm_odm->support_platform) {
	case	ODM_WIN:
		odm_rssi_monitor_check_mp(p_dm_odm);
		break;

	case	ODM_CE:
		odm_rssi_monitor_check_ce(p_dm_odm);
		break;

	case	ODM_AP:
		odm_rssi_monitor_check_ap(p_dm_odm);
		break;

	default:
		break;
	}

}

void
odm_rate_adaptive_mask_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ODM_RATE_ADAPTIVE	*p_odm_ra = &p_dm_odm->rate_adaptive;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	PMGNT_INFO		p_mgnt_info = &p_dm_odm->adapter->MgntInfo;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_dm_odm->adapter);

	p_mgnt_info->Ratr_State = DM_RATR_STA_INIT;

	if (p_mgnt_info->DM_Type == dm_type_by_driver)
		p_hal_data->bUseRAMask = true;
	else
		p_hal_data->bUseRAMask = false;

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	p_odm_ra->type = dm_type_by_driver;
	if (p_odm_ra->type == dm_type_by_driver)
		p_dm_odm->is_use_ra_mask = _TRUE;
	else
		p_dm_odm->is_use_ra_mask = _FALSE;
#endif

	p_odm_ra->ratr_state = DM_RATR_STA_INIT;

#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)
	if (p_dm_odm->support_ic_type == ODM_RTL8812)
		p_odm_ra->ldpc_thres = 50;
	else
		p_odm_ra->ldpc_thres = 35;

	p_odm_ra->rts_thres = 35;

#elif (DM_ODM_SUPPORT_TYPE & ODM_CE)
	p_odm_ra->ldpc_thres = 35;
	p_odm_ra->is_use_ldpc = false;

#else
	p_odm_ra->ultra_low_rssi_thresh = 9;

#endif

	p_odm_ra->high_rssi_thresh = 50;
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && \
	((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	p_odm_ra->low_rssi_thresh = 23;
#else
	p_odm_ra->low_rssi_thresh = 20;
#endif
}
/*-----------------------------------------------------------------------------
 * Function:	odm_refresh_rate_adaptive_mask()
 *
 * Overview:	Update rate table mask according to rssi
 *
 * Input:		NONE
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	05/27/2009	hpfan	Create version 0.
 *
 *---------------------------------------------------------------------------*/
void
odm_refresh_rate_adaptive_mask(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	if (!p_dm_odm->is_linked)
		return;

	if (!(p_dm_odm->support_ability & ODM_BB_RA_MASK)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("odm_refresh_rate_adaptive_mask(): Return cos not supported\n"));
		return;
	}

	p_ra_table->force_update_ra_mask_count++;
	/*  */
	/* 2011/09/29 MH In HW integration first stage, we provide 4 different handle to operate */
	/* at the same time. In the stage2/3, we need to prive universal interface and merge all */
	/* HW dynamic mechanism. */
	/*  */
	switch	(p_dm_odm->support_platform) {
	case	ODM_WIN:
		odm_refresh_rate_adaptive_mask_mp(p_dm_odm);
		break;

	case	ODM_CE:
		odm_refresh_rate_adaptive_mask_ce(p_dm_odm);
		break;

	case	ODM_AP:
		odm_refresh_rate_adaptive_mask_apadsl(p_dm_odm);
		break;
	}

}

u8
phydm_trans_platform_bw(
	void		*p_dm_void,
	u8		BW
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (BW == CHANNEL_WIDTH_20)
		BW = PHYDM_BW_20;

	else if (BW == CHANNEL_WIDTH_40)
		BW = PHYDM_BW_40;

	else if (BW == CHANNEL_WIDTH_80)
		BW = PHYDM_BW_80;

	else if (BW == CHANNEL_WIDTH_160)
		BW = PHYDM_BW_160;

	else if (BW == CHANNEL_WIDTH_80_80)
		BW = PHYDM_BW_80_80;

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	if (BW == HT_CHANNEL_WIDTH_20)
		BW = PHYDM_BW_20;

	else if (BW == HT_CHANNEL_WIDTH_20_40)
		BW = PHYDM_BW_40;

	else if (BW == HT_CHANNEL_WIDTH_80)
		BW = PHYDM_BW_80;

	else if (BW == HT_CHANNEL_WIDTH_160)
		BW = PHYDM_BW_160;

	else if (BW == HT_CHANNEL_WIDTH_10)
		BW = PHYDM_BW_10;

	else if (BW == HT_CHANNEL_WIDTH_5)
		BW = PHYDM_BW_5;

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	if (BW == CHANNEL_WIDTH_20)
		BW = PHYDM_BW_20;

	else if (BW == CHANNEL_WIDTH_40)
		BW = PHYDM_BW_40;

	else if (BW == CHANNEL_WIDTH_80)
		BW = PHYDM_BW_80;

	else if (BW == CHANNEL_WIDTH_160)
		BW = PHYDM_BW_160;

	else if (BW == CHANNEL_WIDTH_80_80)
		BW = PHYDM_BW_80_80;
#endif

	return BW;

}

u8
phydm_trans_platform_rf_type(
	void		*p_dm_void,
	u8		rf_type
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	if (rf_type == RF_1T2R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_2T4R)
		rf_type = PHYDM_RF_2T4R;

	else if (rf_type == RF_2T2R)
		rf_type = PHYDM_RF_2T2R;

	else if (rf_type == RF_1T1R)
		rf_type = PHYDM_RF_1T1R;

	else if (rf_type == RF_2T2R_GREEN)
		rf_type = PHYDM_RF_2T2R_GREEN;

	else if (rf_type == RF_3T3R)
		rf_type = PHYDM_RF_3T3R;

	else if (rf_type == RF_4T4R)
		rf_type = PHYDM_RF_4T4R;

	else if (rf_type == RF_2T3R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_3T4R)
		rf_type = PHYDM_RF_3T4R;

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	if (rf_type == MIMO_1T2R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == MIMO_2T4R)
		rf_type = PHYDM_RF_2T4R;

	else if (rf_type == MIMO_2T2R)
		rf_type = PHYDM_RF_2T2R;

	else if (rf_type == MIMO_1T1R)
		rf_type = PHYDM_RF_1T1R;

	else if (rf_type == MIMO_3T3R)
		rf_type = PHYDM_RF_3T3R;

	else if (rf_type == MIMO_4T4R)
		rf_type = PHYDM_RF_4T4R;

	else if (rf_type == MIMO_2T3R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == MIMO_3T4R)
		rf_type = PHYDM_RF_3T4R;

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	if (rf_type == RF_1T2R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_2T4R)
		rf_type = PHYDM_RF_2T4R;

	else if (rf_type == RF_2T2R)
		rf_type = PHYDM_RF_2T2R;

	else if (rf_type == RF_1T1R)
		rf_type = PHYDM_RF_1T1R;

	else if (rf_type == RF_2T2R_GREEN)
		rf_type = PHYDM_RF_2T2R_GREEN;

	else if (rf_type == RF_3T3R)
		rf_type = PHYDM_RF_3T3R;

	else if (rf_type == RF_4T4R)
		rf_type = PHYDM_RF_4T4R;

	else if (rf_type == RF_2T3R)
		rf_type = PHYDM_RF_1T2R;

	else if (rf_type == RF_3T4R)
		rf_type = PHYDM_RF_3T4R;

#endif

	return rf_type;

}

u32
phydm_trans_platform_wireless_mode(
	void		*p_dm_void,
	u32		wireless_mode
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

	if (wireless_mode == WIRELESS_11A)
		wireless_mode = PHYDM_WIRELESS_MODE_A;

	else if (wireless_mode == WIRELESS_11B)
		wireless_mode = PHYDM_WIRELESS_MODE_B;

	else if ((wireless_mode == WIRELESS_11G) || (wireless_mode == WIRELESS_11BG))
		wireless_mode = PHYDM_WIRELESS_MODE_G;

	else if (wireless_mode == WIRELESS_AUTO)
		wireless_mode = PHYDM_WIRELESS_MODE_AUTO;

	else if ((wireless_mode == WIRELESS_11_24N) || (wireless_mode == WIRELESS_11G_24N) || (wireless_mode == WIRELESS_11B_24N) ||
		(wireless_mode == WIRELESS_11BG_24N) || (wireless_mode == WIRELESS_MODE_24G) || (wireless_mode == WIRELESS_11ABGN) || (wireless_mode == WIRELESS_11AGN))
		wireless_mode = PHYDM_WIRELESS_MODE_N_24G;

	else if ((wireless_mode == WIRELESS_11_5N) || (wireless_mode == WIRELESS_11A_5N))
		wireless_mode = PHYDM_WIRELESS_MODE_N_5G;

	else if ((wireless_mode == WIRELESS_11AC) || (wireless_mode == WIRELESS_11_5AC) || (wireless_mode == WIRELESS_MODE_5G))
		wireless_mode = PHYDM_WIRELESS_MODE_AC_5G;

	else if (wireless_mode == WIRELESS_11_24AC)
		wireless_mode = PHYDM_WIRELESS_MODE_AC_24G;

	else if (wireless_mode == WIRELESS_11AC)
		wireless_mode = PHYDM_WIRELESS_MODE_AC_ONLY;

	else if (wireless_mode == WIRELESS_MODE_MAX)
		wireless_mode = PHYDM_WIRELESS_MODE_MAX;
	else
		wireless_mode = PHYDM_WIRELESS_MODE_UNKNOWN;
#endif

	return wireless_mode;

}

u8
phydm_vht_en_mapping(
	void			*p_dm_void,
	u32			wireless_mode
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			vht_en_out = 0;

	if ((wireless_mode == PHYDM_WIRELESS_MODE_AC_5G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_24G) ||
	    (wireless_mode == PHYDM_WIRELESS_MODE_AC_ONLY)
	   ) {
		vht_en_out = 1;
		/**/
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("wireless_mode= (( 0x%x )), VHT_EN= (( %d ))\n", wireless_mode, vht_en_out));
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
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			rate_id_idx = 0;
	u8			phydm_BW;
	u8			phydm_rf_type;

	phydm_BW = phydm_trans_platform_bw(p_dm_odm, bw);
	phydm_rf_type = phydm_trans_platform_rf_type(p_dm_odm, rf_type);
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	wireless_mode = phydm_trans_platform_wireless_mode(p_dm_odm, wireless_mode);
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x ))\n",
			wireless_mode, phydm_rf_type, phydm_BW));


	switch (wireless_mode) {

	case PHYDM_WIRELESS_MODE_N_24G:
	{

		if (phydm_BW == PHYDM_BW_40) {

			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_BGN_40M_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_BGN_40M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;

		} else {

			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_BGN_20M_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_BGN_20M_2SS;
			else
				rate_id_idx = PHYDM_ARFR5_N_3SS;
		}
	}
	break;

	case PHYDM_WIRELESS_MODE_N_5G:
	{
		if (phydm_rf_type == PHYDM_RF_1T1R)
			rate_id_idx = PHYDM_GN_N1SS;
		else if (phydm_rf_type == PHYDM_RF_2T2R)
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
		if (phydm_rf_type == PHYDM_RF_1T1R)
			rate_id_idx = PHYDM_ARFR1_AC_1SS;
		else if (phydm_rf_type == PHYDM_RF_2T2R)
			rate_id_idx = PHYDM_ARFR0_AC_2SS;
		else
			rate_id_idx = PHYDM_ARFR4_AC_3SS;
	}
	break;

	case PHYDM_WIRELESS_MODE_AC_24G:
	{
		/*Becareful to set "Lowest rate" while using PHYDM_ARFR4_AC_3SS in 2.4G/5G*/
		if (phydm_BW >= PHYDM_BW_80) {
			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_ARFR1_AC_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
				rate_id_idx = PHYDM_ARFR0_AC_2SS;
			else
				rate_id_idx = PHYDM_ARFR4_AC_3SS;
		} else {

			if (phydm_rf_type == PHYDM_RF_1T1R)
				rate_id_idx = PHYDM_ARFR2_AC_2G_1SS;
			else if (phydm_rf_type == PHYDM_RF_2T2R)
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

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RA rate ID = (( 0x%x ))\n", rate_id_idx));

	return rate_id_idx;
}

void
phydm_update_hal_ra_mask(
	void			*p_dm_void,
	u32			wireless_mode,
	u8			rf_type,
	u8			BW,
	u8			mimo_ps_enable,
	u8			disable_cck_rate,
	u32			*ratr_bitmap_msb_in,
	u32			*ratr_bitmap_lsb_in,
	u8			tx_rate_level
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32			mask_rate_threshold;
	u8			phydm_rf_type;
	u8			phydm_BW;
	u32			ratr_bitmap = *ratr_bitmap_lsb_in, ratr_bitmap_msb = *ratr_bitmap_msb_in;
	/*struct _ODM_RATE_ADAPTIVE*		p_ra = &(p_dm_odm->rate_adaptive);*/

#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	wireless_mode = phydm_trans_platform_wireless_mode(p_dm_odm, wireless_mode);
#endif

	phydm_rf_type = phydm_trans_platform_rf_type(p_dm_odm, rf_type);
	phydm_BW = phydm_trans_platform_bw(p_dm_odm, BW);

	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("phydm_rf_type = (( %x )), rf_type = (( %x ))\n", phydm_rf_type, rf_type));*/
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Platfoem original RA Mask = (( 0x %x | %x ))\n", ratr_bitmap_msb, ratr_bitmap));

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
			phydm_rf_type = PHYDM_RF_1T1R;

		if (phydm_rf_type == PHYDM_RF_1T1R) {

			if (phydm_BW == PHYDM_BW_40)
				ratr_bitmap &= 0x000ff015;
			else
				ratr_bitmap &= 0x000ff005;
		} else if (phydm_rf_type == PHYDM_RF_2T2R || phydm_rf_type == PHYDM_RF_2T4R || phydm_rf_type == PHYDM_RF_2T3R) {

			if (phydm_BW == PHYDM_BW_40)
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
		if (phydm_rf_type == PHYDM_RF_1T1R)
			ratr_bitmap &= 0x003ff015;
		else if (phydm_rf_type == PHYDM_RF_2T2R || phydm_rf_type == PHYDM_RF_2T4R || phydm_rf_type == PHYDM_RF_2T3R)
			ratr_bitmap &= 0xfffff015;
		else {/*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (phydm_BW == PHYDM_BW_20) {/* AC 20MHz doesn't support MCS9 */
			ratr_bitmap &= 0x7fdfffff;
			ratr_bitmap_msb &= 0x1ff;
		}
	}
	break;

	case PHYDM_WIRELESS_MODE_AC_5G:
	{
		if (phydm_rf_type == PHYDM_RF_1T1R)
			ratr_bitmap &= 0x003ff010;
		else if (phydm_rf_type == PHYDM_RF_2T2R || phydm_rf_type == PHYDM_RF_2T4R || phydm_rf_type == PHYDM_RF_2T3R)
			ratr_bitmap &= 0xfffff010;
		else {/*3T*/

			ratr_bitmap &= 0xfffff010;
			ratr_bitmap_msb &= 0x3ff;
		}

		if (phydm_BW == PHYDM_BW_20) {/* AC 20MHz doesn't support MCS9 */
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

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("wireless_mode= (( 0x%x )), rf_type = (( 0x%x )), BW = (( 0x%x )), MimoPs_en = (( %d )), tx_rate_level= (( 0x%x ))\n",
		wireless_mode, phydm_rf_type, phydm_BW, mimo_ps_enable, tx_rate_level));

	/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("111 Phydm modified RA Mask = (( 0x %x | %x ))\n", ratr_bitmap_msb, ratr_bitmap));*/

	*ratr_bitmap_lsb_in = ratr_bitmap;
	*ratr_bitmap_msb_in = ratr_bitmap_msb;
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Phydm modified RA Mask = (( 0x %x | %x ))\n", *ratr_bitmap_msb_in, *ratr_bitmap_lsb_in));

}

u8
phydm_RA_level_decision(
	void			*p_dm_void,
	u32			rssi,
	u8			ratr_state
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8	ra_lowest_rate;
	u8	ra_rate_floor_table[RA_FLOOR_TABLE_SIZE] = {20, 34, 38, 42, 46, 50, 100}; /*MCS0 ~ MCS4 , VHT1SS MCS0 ~ MCS4 , G 6M~24M*/
	u8	new_ratr_state = 0;
	u8	i;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("curr RA level = ((%d)), Rate_floor_table ori [ %d , %d, %d , %d, %d, %d]\n", ratr_state,
		ra_rate_floor_table[0], ra_rate_floor_table[1], ra_rate_floor_table[2], ra_rate_floor_table[3], ra_rate_floor_table[4], ra_rate_floor_table[5]));

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {

		if (i >= (ratr_state))
			ra_rate_floor_table[i] += RA_FLOOR_UP_GAP;
	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI = ((%d)), Rate_floor_table_mod [ %d , %d, %d , %d, %d, %d]\n",
		rssi, ra_rate_floor_table[0], ra_rate_floor_table[1], ra_rate_floor_table[2], ra_rate_floor_table[3], ra_rate_floor_table[4], ra_rate_floor_table[5]));

	for (i = 0; i < RA_FLOOR_TABLE_SIZE; i++) {

		if (rssi < ra_rate_floor_table[i]) {
			new_ratr_state = i;
			break;
		}
	}



	return	new_ratr_state;

}

void
odm_refresh_rate_adaptive_mask_mp(
	void		*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT				*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_					*p_ra_table = &p_dm_odm->dm_ra_table;
	struct _ADAPTER				*p_adapter	 =  p_dm_odm->adapter;
	struct _ADAPTER				*p_target_adapter = NULL;
	HAL_DATA_TYPE			*p_hal_data = GET_HAL_DATA(p_adapter);
	PMGNT_INFO				p_mgnt_info = GetDefaultMgntInfo(p_adapter);
	u32		i;
	struct sta_info *p_entry;
	u8		ratr_state_new;

	if (p_adapter->bDriverStopped) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_refresh_rate_adaptive_mask(): driver is going to unload\n"));
		return;
	}

	if (!p_hal_data->bUseRAMask) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_refresh_rate_adaptive_mask(): driver does not control rate adaptive mask\n"));
		return;
	}

	/* if default port is connected, update RA table for default port (infrastructure mode only) */
	if (p_mgnt_info->mAssoc && (!ACTING_AS_AP(p_adapter))) {
		odm_refresh_ldpc_rts_mp(p_adapter, p_dm_odm, p_mgnt_info->mMacId,  p_mgnt_info->IOTPeer, p_hal_data->UndecoratedSmoothedPWDB);
		/*ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Infrasture mode\n"));*/

#if RA_MASK_PHYDMLIZE_WIN
		ratr_state_new = phydm_RA_level_decision(p_dm_odm, p_hal_data->UndecoratedSmoothedPWDB, p_mgnt_info->Ratr_State);

		if ((p_mgnt_info->Ratr_State != ratr_state_new) || (p_ra_table->force_update_ra_mask_count >= FORCED_UPDATE_RAMASK_PERIOD)) {

			p_ra_table->force_update_ra_mask_count = 0;
			ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr :"), p_mgnt_info->Bssid);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Update RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n\n",
				p_mgnt_info->Ratr_State, ratr_state_new, p_hal_data->UndecoratedSmoothedPWDB));

			p_mgnt_info->Ratr_State = ratr_state_new;
			p_adapter->HalFunc.UpdateHalRAMaskHandler(p_adapter, p_mgnt_info->mMacId, NULL, ratr_state_new);
		} else {
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
			/**/
		}

#else
		if (odm_ra_state_check(p_dm_odm, p_hal_data->UndecoratedSmoothedPWDB, p_mgnt_info->bSetTXPowerTrainingByOid, &p_mgnt_info->Ratr_State)) {
			ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr : "), p_mgnt_info->Bssid);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", p_hal_data->UndecoratedSmoothedPWDB, p_mgnt_info->Ratr_State));
			p_adapter->HalFunc.UpdateHalRAMaskHandler(p_adapter, p_mgnt_info->mMacId, NULL, p_mgnt_info->Ratr_State);
		} else if (p_dm_odm->is_change_state) {
			ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr : "), p_mgnt_info->Bssid);
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training state, is_disable_power_training = %d\n", p_dm_odm->is_disable_power_training));
			p_adapter->HalFunc.UpdateHalRAMaskHandler(p_adapter, p_mgnt_info->mMacId, NULL, p_mgnt_info->Ratr_State);
		}
#endif
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

			if (IS_STA_VALID(p_entry)) {

				odm_refresh_ldpc_rts_mp(p_adapter, p_dm_odm, p_entry->AssociatedMacId, p_entry->IOTPeer, p_entry->rssi_stat.undecorated_smoothed_pwdb);

#if RA_MASK_PHYDMLIZE_WIN
				ratr_state_new = phydm_RA_level_decision(p_dm_odm, p_entry->rssi_stat.undecorated_smoothed_pwdb, p_entry->Ratr_State);

				if ((p_entry->Ratr_State != ratr_state_new) || (p_ra_table->force_update_ra_mask_count >= FORCED_UPDATE_RAMASK_PERIOD)) {

					p_ra_table->force_update_ra_mask_count = 0;
					ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr :"), p_entry->MacAddr);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n",
						p_entry->Ratr_State, ratr_state_new,  p_entry->rssi_stat.undecorated_smoothed_pwdb));

					p_entry->Ratr_State = ratr_state_new;
					p_adapter->HalFunc.UpdateHalRAMaskHandler(p_adapter, p_entry->AssociatedMacId, NULL, ratr_state_new);
				} else {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
					/**/
				}


#else

				if (odm_ra_state_check(p_dm_odm, p_entry->rssi_stat.undecorated_smoothed_pwdb, p_mgnt_info->bSetTXPowerTrainingByOid, &p_entry->Ratr_State)) {
					ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), p_entry->mac_addr);
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", p_entry->rssi_stat.undecorated_smoothed_pwdb, p_entry->Ratr_State));
					p_adapter->hal_func.update_hal_ra_mask_handler(p_target_adapter, p_entry->AssociatedMacId, p_entry, p_entry->Ratr_State);
				} else if (p_dm_odm->is_change_state) {
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training state, is_disable_power_training = %d\n", p_dm_odm->is_disable_power_training));
					p_adapter->HalFunc.UpdateHalRAMaskHandler(p_adapter, p_mgnt_info->mMacId, NULL, p_mgnt_info->Ratr_State);
				}
#endif

			}
		}
	}

#if RA_MASK_PHYDMLIZE_WIN

#else
	if (p_mgnt_info->bSetTXPowerTrainingByOid)
		p_mgnt_info->bSetTXPowerTrainingByOid = false;
#endif
#endif	/*  #if (DM_ODM_SUPPORT_TYPE == ODM_WIN) */
}


void
odm_refresh_rate_adaptive_mask_ce(
	void	*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	struct _ADAPTER	*p_adapter	 =  p_dm_odm->adapter;
	struct _ODM_RATE_ADAPTIVE		*p_ra = &p_dm_odm->rate_adaptive;
	u32		i;
	struct sta_info *p_entry;
	u8		ratr_state_new;

	if (RTW_CANNOT_RUN(p_adapter)) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_TRACE, ("<---- odm_refresh_rate_adaptive_mask(): driver is going to unload\n"));
		return;
	}

	if (!p_dm_odm->is_use_ra_mask) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("<---- odm_refresh_rate_adaptive_mask(): driver does not control rate adaptive mask\n"));
		return;
	}

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {

		p_entry = p_dm_odm->p_odm_sta_info[i];

		if (IS_STA_VALID(p_entry)) {

			if (IS_MCAST(p_entry->hwaddr))
				continue;

#if ((RTL8812A_SUPPORT == 1) || (RTL8821A_SUPPORT == 1))
			if ((p_dm_odm->support_ic_type == ODM_RTL8812) || (p_dm_odm->support_ic_type == ODM_RTL8821)) {
				if (p_entry->rssi_stat.undecorated_smoothed_pwdb < p_ra->ldpc_thres) {
					p_ra->is_use_ldpc = true;
					p_ra->is_lower_rts_rate = true;
					if ((p_dm_odm->support_ic_type == ODM_RTL8821) && (p_dm_odm->cut_version == ODM_CUT_A))
						set_ra_ldpc_8812(p_entry, true);
					/* dbg_print("RSSI=%d, is_use_ldpc = true\n", p_hal_data->undecorated_smoothed_pwdb); */
				} else if (p_entry->rssi_stat.undecorated_smoothed_pwdb > (p_ra->ldpc_thres - 5)) {
					p_ra->is_use_ldpc = false;
					p_ra->is_lower_rts_rate = false;
					if ((p_dm_odm->support_ic_type == ODM_RTL8821) && (p_dm_odm->cut_version == ODM_CUT_A))
						set_ra_ldpc_8812(p_entry, false);
					/* dbg_print("RSSI=%d, is_use_ldpc = false\n", p_hal_data->undecorated_smoothed_pwdb); */
				}
			}
#endif

#if RA_MASK_PHYDMLIZE_CE
			ratr_state_new = phydm_RA_level_decision(p_dm_odm, p_entry->rssi_stat.undecorated_smoothed_pwdb, p_entry->rssi_level);

			if ((p_entry->rssi_level != ratr_state_new) || (p_ra_table->force_update_ra_mask_count >= FORCED_UPDATE_RAMASK_PERIOD)) {

				p_ra_table->force_update_ra_mask_count = 0;
				/*ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr :"), pstat->hwaddr);*/
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n",
					p_entry->rssi_level, ratr_state_new, p_entry->rssi_stat.undecorated_smoothed_pwdb));

				p_entry->rssi_level = ratr_state_new;
				rtw_hal_update_ra_mask(p_entry, p_entry->rssi_level, _FALSE);
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
				/**/
			}
#else
			if (true == odm_ra_state_check(p_dm_odm, p_entry->rssi_stat.undecorated_smoothed_pwdb, false, &p_entry->rssi_level)) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", p_entry->rssi_stat.undecorated_smoothed_pwdb, p_entry->rssi_level));
				/* printk("RSSI:%d, RSSI_LEVEL:%d\n", pstat->rssi_stat.undecorated_smoothed_pwdb, pstat->rssi_level); */
				rtw_hal_update_ra_mask(p_entry, p_entry->rssi_level, _FALSE);
			} else if (p_dm_odm->is_change_state) {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Change Power Training state, is_disable_power_training = %d\n", p_dm_odm->is_disable_power_training));
				rtw_hal_update_ra_mask(p_entry, p_entry->rssi_level, _FALSE);
			}
#endif

		}
	}

#endif
}

#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
void
phydm_gen_ramask_h2c_AP(
	void			*p_dm_void,
	struct rtl8192cd_priv *priv,
	struct sta_info *p_entry,
	u8			rssi_level
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	if (p_dm_odm->support_ic_type == ODM_RTL8812) {

#if (RTL8812A_SUPPORT == 1)
		UpdateHalRAMask8812(priv, p_entry, rssi_level);
		/**/
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8188E) {

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

void
odm_refresh_rate_adaptive_mask_apadsl(
	void	*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;
	struct rtl8192cd_priv *priv = p_dm_odm->priv;
	struct aid_obj *aidarray;
	u32		i;
	struct sta_info *p_entry;
	u8		ratr_state_new;

	if (priv->up_time % 2)
		return;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		p_entry = p_dm_odm->p_odm_sta_info[i];

		if (IS_STA_VALID(p_entry)) {

#if defined(UNIVERSAL_REPEATER) || defined(MBSSID)
			aidarray = container_of(p_entry, struct aid_obj, station);
			priv = aidarray->priv;
#endif

			if (!priv->pmib->dot11StationConfigEntry.autoRate)
				continue;

#if RA_MASK_PHYDMLIZE_AP
			ratr_state_new = phydm_RA_level_decision(p_dm_odm, (u32)p_entry->rssi, p_entry->rssi_level);

			if ((p_entry->rssi_level != ratr_state_new) || (p_ra_table->force_update_ra_mask_count >= FORCED_UPDATE_RAMASK_PERIOD)) {

				p_ra_table->force_update_ra_mask_count = 0;
				ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target AP addr :"), p_entry->hwaddr);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Update Tx RA Level: ((%x)) -> ((%x)),  RSSI = ((%d))\n", p_entry->rssi_level, ratr_state_new, p_entry->rssi));

				p_entry->rssi_level = ratr_state_new;
				phydm_gen_ramask_h2c_AP(p_dm_odm, priv, p_entry, p_entry->rssi_level);
			} else {
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Stay in RA level  = (( %d ))\n\n", ratr_state_new));
				/**/
			}

#else
			if (odm_ra_state_check(p_dm_odm, (s32)p_entry->rssi, false, &p_entry->rssi_level)) {
				ODM_PRINT_ADDR(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("Target STA addr : "), p_entry->hwaddr);
				ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI:%d, RSSI_LEVEL:%d\n", p_entry->rssi, p_entry->rssi_level));

#ifdef CONFIG_WLAN_HAL
				if (IS_HAL_CHIP(priv)) {
#ifdef WDS
					/*if(!(pstat->state & WIFI_WDS))*/	/*if WDS donot setting*/
#endif
					GET_HAL_INTERFACE(priv)->update_hal_ra_mask_handler(priv, p_entry, p_entry->rssi_level);
				} else
#endif

#ifdef CONFIG_RTL_8812_SUPPORT
					if (GET_CHIP_VER(priv) == VERSION_8812E)
						update_hal_ra_mask8812(priv, p_entry, 3);
					else
#endif
					{
#ifdef CONFIG_RTL_88E_SUPPORT
						if (GET_CHIP_VER(priv) == VERSION_8188E) {
#ifdef TXREPORT
							add_ra_tid(priv, p_entry);
#endif
						}
#endif


					}
			}
#endif /*#ifdef RA_MASK_PHYDMLIZE*/

		}
	}
#endif /*#if (DM_ODM_SUPPORT_TYPE & ODM_AP)*/
}

void
odm_refresh_basic_rate_mask(
	void	*p_dm_void
)
{
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter	 =  p_dm_odm->adapter;
	static u8		stage = 0;
	u8			cur_stage = 0;
	OCTET_STRING	os_rate_set;
	PMGNT_INFO		p_mgnt_info = GetDefaultMgntInfo(adapter);
	u8			rate_set[5] = {MGN_1M, MGN_2M, MGN_5_5M, MGN_11M, MGN_6M};

	if (p_dm_odm->support_ic_type != ODM_RTL8812 && p_dm_odm->support_ic_type != ODM_RTL8821)
		return;

	if (p_dm_odm->is_linked == false)	/* unlink Default port information */
		cur_stage = 0;
	else if (p_dm_odm->rssi_min < 40)	/* link RSSI  < 40% */
		cur_stage = 1;
	else if (p_dm_odm->rssi_min > 45)	/* link RSSI > 45% */
		cur_stage = 3;
	else
		cur_stage = 2;					/* link  25% <= RSSI <= 30% */

	if (cur_stage != stage) {
		if (cur_stage == 1) {
			FillOctetString(os_rate_set, rate_set, 5);
			FilterSupportRate(p_mgnt_info->mBrates, &os_rate_set, false);
			phydm_set_hw_reg_handler_interface(p_dm_odm, HW_VAR_BASIC_RATE, (u8 *)&os_rate_set);
		} else if (cur_stage == 3 && (stage == 1 || stage == 2))
			phydm_set_hw_reg_handler_interface(p_dm_odm, HW_VAR_BASIC_RATE, (u8 *)(&p_mgnt_info->mBrates));
	}

	stage = cur_stage;
#endif
}

u8
phydm_rate_order_compute(
	void	*p_dm_void,
	u8	rate_idx
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
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
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;
	u16		macid;
	u8		rate_order_tmp;
	u8		cnt = 0;

	p_ra_table->highest_client_tx_order = 0;
	p_ra_table->power_tracking_flag = 1;

	if (p_dm_odm->number_linked_client != 0) {
		for (macid = 0; macid < ODM_ASSOCIATE_ENTRY_NUM; macid++) {

			rate_order_tmp = phydm_rate_order_compute(p_dm_odm, ((p_ra_table->link_tx_rate[macid]) & 0x7f));

			if (rate_order_tmp >= (p_ra_table->highest_client_tx_order)) {
				p_ra_table->highest_client_tx_order = rate_order_tmp;
				p_ra_table->highest_client_tx_rate_order = macid;
			}

			cnt++;

			if (cnt == p_dm_odm->number_linked_client)
				break;
		}
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("MACID[%d], Highest Tx order Update for power traking: %d\n", (p_ra_table->highest_client_tx_rate_order), (p_ra_table->highest_client_tx_order)));
	}
}

void
phydm_ra_info_watchdog(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	phydm_ra_common_info_update(p_dm_odm);
	phydm_ra_dynamic_retry_limit(p_dm_odm);
	phydm_ra_dynamic_retry_count(p_dm_odm);
	odm_refresh_rate_adaptive_mask(p_dm_odm);
	odm_refresh_basic_rate_mask(p_dm_odm);
}

void
phydm_ra_info_init(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT	*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_		*p_ra_table = &p_dm_odm->dm_ra_table;

	p_ra_table->highest_client_tx_rate_order = 0;
	p_ra_table->highest_client_tx_order = 0;
	p_ra_table->RA_threshold_offset = 0;
	p_ra_table->RA_offset_direction = 0;

#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	phydm_ra_dynamic_retry_limit_init(p_dm_odm);
#endif

#if (defined(CONFIG_RA_DYNAMIC_RATE_ID))
	phydm_ra_dynamic_rate_id_init(p_dm_odm);
#endif
#if (defined(CONFIG_RA_DBG_CMD))
	odm_ra_para_adjust_init(p_dm_odm);
#endif

}


#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))
u8
odm_find_rts_rate(
	void			*p_dm_void,
	u8			tx_rate,
	boolean			is_erp_protect
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
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

	if (*p_dm_odm->p_band_type == 1) {
		if (rts_ini_rate < ODM_RATE6M)
			rts_ini_rate = ODM_RATE6M;
	}
	return rts_ini_rate;

}

void
odm_set_ra_dm_arfb_by_noisy(
	struct PHY_DM_STRUCT	*p_dm_odm
)
{
#if 0

	/*dbg_print("DM_ARFB ====>\n");*/
	if (p_dm_odm->is_noisy_state) {
		odm_write_4byte(p_dm_odm, 0x430, 0x00000000);
		odm_write_4byte(p_dm_odm, 0x434, 0x05040200);
		/*dbg_print("DM_ARFB ====> Noisy state\n");*/
	} else {
		odm_write_4byte(p_dm_odm, 0x430, 0x02010000);
		odm_write_4byte(p_dm_odm, 0x434, 0x07050403);
		/*dbg_print("DM_ARFB ====> Clean state\n");*/
	}
#endif
}

void
odm_update_noisy_state(
	void		*p_dm_void,
	boolean		is_noisy_state_from_c2h
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

/* JJ ADD 20161014 */
	/*dbg_print("Get C2H Command! NoisyState=0x%x\n ", is_noisy_state_from_c2h);*/
	if (p_dm_odm->support_ic_type == ODM_RTL8821  || p_dm_odm->support_ic_type == ODM_RTL8812  ||
	    p_dm_odm->support_ic_type == ODM_RTL8723B || p_dm_odm->support_ic_type == ODM_RTL8192E || p_dm_odm->support_ic_type == ODM_RTL8188E || p_dm_odm->support_ic_type == ODM_RTL8723D || p_dm_odm->support_ic_type == ODM_RTL8710B)
		p_dm_odm->is_noisy_state = is_noisy_state_from_c2h;
	odm_set_ra_dm_arfb_by_noisy(p_dm_odm);
};

void
phydm_update_pwr_track(
	void		*p_dm_void,
	u8		rate
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8			path_idx = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_TX_PWR_TRACK, ODM_DBG_LOUD, ("Pwr Track Get rate=0x%x\n", rate));

	p_dm_odm->tx_rate = rate;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
#if USE_WORKITEM
	odm_schedule_work_item(&p_dm_odm->ra_rpt_workitem);
#else
	if (p_dm_odm->support_ic_type == ODM_RTL8821) {
#if (RTL8821A_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr8821a(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		for (path_idx = ODM_RF_PATH_A; path_idx < MAX_PATH_NUM_8812A; path_idx++) {
#if (RTL8812A_SUPPORT == 1)
			odm_tx_pwr_track_set_pwr8812a(p_dm_odm, MIX_MODE, path_idx, 0);
#endif
		}
	} else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
#if (RTL8723B_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr_8723b(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
#endif
	} else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		for (path_idx = ODM_RF_PATH_A; path_idx < MAX_PATH_NUM_8192E; path_idx++) {
#if (RTL8192E_SUPPORT == 1)
			odm_tx_pwr_track_set_pwr92_e(p_dm_odm, MIX_MODE, path_idx, 0);
#endif
		}
	} else if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
#if (RTL8188E_SUPPORT == 1)
		odm_tx_pwr_track_set_pwr88_e(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
#endif
	}
#endif
#else
	odm_schedule_work_item(&p_dm_odm->ra_rpt_workitem);
#endif
#endif

}

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

s32
phydm_find_minimum_rssi(
	struct PHY_DM_STRUCT		*p_dm_odm,
	struct _ADAPTER		*p_adapter,
	OUT	boolean			*p_is_link_temp

)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	PMGNT_INFO		p_mgnt_info = &(p_adapter->MgntInfo);
	boolean			act_as_ap = ACTING_AS_AP(p_adapter);

	/* 1.Determine the minimum RSSI */
	if ((!p_mgnt_info->bMediaConnect) ||
	    (act_as_ap && (p_hal_data->EntryMinUndecoratedSmoothedPWDB == 0))) {/* We should check AP mode and Entry info.into consideration, revised by Roger, 2013.10.18*/

		p_hal_data->MinUndecoratedPWDBForDM = 0;
		*p_is_link_temp = false;

	} else
		*p_is_link_temp = true;


	if (p_mgnt_info->bMediaConnect) {	/* Default port*/

		if (act_as_ap || p_mgnt_info->mIbss) {
			p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
			/**/
		} else {
			p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->UndecoratedSmoothedPWDB;
			/**/
		}
	} else { /* associated entry pwdb*/
		p_hal_data->MinUndecoratedPWDBForDM = p_hal_data->EntryMinUndecoratedSmoothedPWDB;
		/**/
	}

	return p_hal_data->MinUndecoratedPWDBForDM;
}

void
odm_update_init_rate_work_item_callback(
	void	*p_context
)
{
	struct _ADAPTER	*adapter = (struct _ADAPTER *)p_context;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &p_hal_data->DM_OutSrc;
	u8		p = 0;

	if (p_dm_odm->support_ic_type == ODM_RTL8821) {
		odm_tx_pwr_track_set_pwr8821a(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
		/**/
	} else if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8812A; p++) {    /*DOn't know how to include &c*/

			odm_tx_pwr_track_set_pwr8812a(p_dm_odm, MIX_MODE, p, 0);
			/**/
		}
	} else if (p_dm_odm->support_ic_type == ODM_RTL8723B) {
		odm_tx_pwr_track_set_pwr_8723b(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
		/**/
	} else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		for (p = ODM_RF_PATH_A; p < MAX_PATH_NUM_8192E; p++) {   /*DOn't know how to include &c*/
			odm_tx_pwr_track_set_pwr92_e(p_dm_odm, MIX_MODE, p, 0);
			/**/
		}
	} else if (p_dm_odm->support_ic_type == ODM_RTL8188E) {
		odm_tx_pwr_track_set_pwr88_e(p_dm_odm, MIX_MODE, ODM_RF_PATH_A, 0);
		/**/
	}
}

void
odm_rssi_dump_to_register(
	void	*p_dm_void
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ADAPTER		*adapter = p_dm_odm->adapter;

	if (p_dm_odm->support_ic_type == ODM_RTL8812) {
		PlatformEFIOWrite1Byte(adapter, REG_A_RSSI_DUMP_JAGUAR, adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(adapter, REG_B_RSSI_DUMP_JAGUAR, adapter->RxStats.RxRSSIPercentage[1]);

		/* Rx EVM*/
		PlatformEFIOWrite1Byte(adapter, REG_S1_RXEVM_DUMP_JAGUAR, adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(adapter, REG_S2_RXEVM_DUMP_JAGUAR, adapter->RxStats.RxEVMdbm[1]);

		/* Rx SNR*/
		PlatformEFIOWrite1Byte(adapter, REG_A_RX_SNR_DUMP_JAGUAR, (u8)(adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(adapter, REG_B_RX_SNR_DUMP_JAGUAR, (u8)(adapter->RxStats.RxSNRdB[1]));

		/* Rx Cfo_Short*/
		PlatformEFIOWrite2Byte(adapter, REG_A_CFO_SHORT_DUMP_JAGUAR, adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(adapter, REG_B_CFO_SHORT_DUMP_JAGUAR, adapter->RxStats.RxCfoShort[1]);

		/* Rx Cfo_Tail*/
		PlatformEFIOWrite2Byte(adapter, REG_A_CFO_LONG_DUMP_JAGUAR, adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(adapter, REG_B_CFO_LONG_DUMP_JAGUAR, adapter->RxStats.RxCfoTail[1]);
	} else if (p_dm_odm->support_ic_type == ODM_RTL8192E) {
		PlatformEFIOWrite1Byte(adapter, REG_A_RSSI_DUMP_92E, adapter->RxStats.RxRSSIPercentage[0]);
		PlatformEFIOWrite1Byte(adapter, REG_B_RSSI_DUMP_92E, adapter->RxStats.RxRSSIPercentage[1]);
		/* Rx EVM*/
		PlatformEFIOWrite1Byte(adapter, REG_S1_RXEVM_DUMP_92E, adapter->RxStats.RxEVMdbm[0]);
		PlatformEFIOWrite1Byte(adapter, REG_S2_RXEVM_DUMP_92E, adapter->RxStats.RxEVMdbm[1]);
		/* Rx SNR*/
		PlatformEFIOWrite1Byte(adapter, REG_A_RX_SNR_DUMP_92E, (u8)(adapter->RxStats.RxSNRdB[0]));
		PlatformEFIOWrite1Byte(adapter, REG_B_RX_SNR_DUMP_92E, (u8)(adapter->RxStats.RxSNRdB[1]));
		/* Rx Cfo_Short*/
		PlatformEFIOWrite2Byte(adapter, REG_A_CFO_SHORT_DUMP_92E, adapter->RxStats.RxCfoShort[0]);
		PlatformEFIOWrite2Byte(adapter, REG_B_CFO_SHORT_DUMP_92E, adapter->RxStats.RxCfoShort[1]);
		/* Rx Cfo_Tail*/
		PlatformEFIOWrite2Byte(adapter, REG_A_CFO_LONG_DUMP_92E, adapter->RxStats.RxCfoTail[0]);
		PlatformEFIOWrite2Byte(adapter, REG_B_CFO_LONG_DUMP_92E, adapter->RxStats.RxCfoTail[1]);
	}
}

void
odm_refresh_ldpc_rts_mp(
	struct _ADAPTER			*p_adapter,
	struct PHY_DM_STRUCT			*p_dm_odm,
	u8				m_mac_id,
	u8				iot_peer,
	s32				undecorated_smoothed_pwdb
)
{
	boolean					is_ctl_ldpc = false;
	struct _ODM_RATE_ADAPTIVE		*p_ra = &p_dm_odm->rate_adaptive;

	if (p_dm_odm->support_ic_type != ODM_RTL8821 && p_dm_odm->support_ic_type != ODM_RTL8812)
		return;

	if ((p_dm_odm->support_ic_type == ODM_RTL8821) && (p_dm_odm->cut_version == ODM_CUT_A))
		is_ctl_ldpc = true;
	else if (p_dm_odm->support_ic_type == ODM_RTL8812 &&
		 iot_peer == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP)
		is_ctl_ldpc = true;

	if (is_ctl_ldpc) {
		if (undecorated_smoothed_pwdb < (p_ra->ldpc_thres - 5))
			MgntSet_TX_LDPC(p_adapter, m_mac_id, true);
		else if (undecorated_smoothed_pwdb > p_ra->ldpc_thres)
			MgntSet_TX_LDPC(p_adapter, m_mac_id, false);
	}

	if (undecorated_smoothed_pwdb < (p_ra->rts_thres - 5))
		p_ra->is_lower_rts_rate = true;
	else if (undecorated_smoothed_pwdb > p_ra->rts_thres)
		p_ra->is_lower_rts_rate = false;
}

#if 0
void
odm_dynamic_arfb_select(
	void		*p_dm_void,
	u8			rate,
	boolean			collision_state
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _rate_adaptive_table_			*p_ra_table = &p_dm_odm->dm_ra_table;

	if (p_dm_odm->support_ic_type != ODM_RTL8192E)
		return;

	if (collision_state == p_ra_table->PT_collision_pre)
		return;

	if (rate >= DESC_RATEMCS8  && rate <= DESC_RATEMCS12) {
		if (collision_state == 1) {
			if (rate == DESC_RATEMCS12) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x0);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x07060501);
			} else if (rate == DESC_RATEMCS11) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x0);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x07070605);
			} else if (rate == DESC_RATEMCS10) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x0);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x08080706);
			} else if (rate == DESC_RATEMCS9) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x0);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x08080707);
			} else {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x0);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x09090808);
			}
		} else { /* collision_state == 0*/
			if (rate == DESC_RATEMCS12) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x05010000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x09080706);
			} else if (rate == DESC_RATEMCS11) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x06050000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x09080807);
			} else if (rate == DESC_RATEMCS10) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x07060000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x0a090908);
			} else if (rate == DESC_RATEMCS9) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x07070000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x0a090808);
			} else {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x08080000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x0b0a0909);
			}
		}
	} else { /* MCS13~MCS15,  1SS, G-mode*/
		if (collision_state == 1) {
			if (rate == DESC_RATEMCS15) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x00000000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x05040302);
			} else if (rate == DESC_RATEMCS14) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x00000000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x06050302);
			} else if (rate == DESC_RATEMCS13) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x00000000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x07060502);
			} else {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x00000000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x06050402);
			}
		} else { /* collision_state == 0 */
			if (rate == DESC_RATEMCS15) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x03020000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x07060504);
			} else if (rate == DESC_RATEMCS14) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x03020000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x08070605);
			} else if (rate == DESC_RATEMCS13) {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x05020000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x09080706);
			} else {

				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E, 0x04020000);
				odm_write_4byte(p_dm_odm, REG_DARFRC_8192E+4, 0x08070605);
			}
		}
	}
	p_ra_table->PT_collision_pre = collision_state;
}
#endif

void
odm_rate_adaptive_state_ap_init(
	void		*PADAPTER_VOID,
	struct sta_info	*p_entry
)
{
	struct _ADAPTER		*adapter = (struct _ADAPTER *)PADAPTER_VOID;
	p_entry->Ratr_State = DM_RATR_STA_INIT;
}
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) /*#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)*/

static void
find_minimum_rssi(
	struct _ADAPTER	*p_adapter
)
{
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(p_adapter);
	struct PHY_DM_STRUCT		*p_dm_odm = &(p_hal_data->odmpriv);

	/*Determine the minimum RSSI*/

	if ((p_dm_odm->is_linked != _TRUE) &&
	    (p_hal_data->entry_min_undecorated_smoothed_pwdb == 0)) {
		p_hal_data->min_undecorated_pwdb_for_dm = 0;
		/*ODM_RT_TRACE(p_dm_odm,COMP_BB_POWERSAVING, DBG_LOUD, ("Not connected to any\n"));*/
	} else
		p_hal_data->min_undecorated_pwdb_for_dm = p_hal_data->entry_min_undecorated_smoothed_pwdb;

	/*DBG_8192C("%s=>min_undecorated_pwdb_for_dm(%d)\n",__FUNCTION__,pdmpriv->min_undecorated_pwdb_for_dm);*/
	/*ODM_RT_TRACE(p_dm_odm,COMP_DIG, DBG_LOUD, ("min_undecorated_pwdb_for_dm =%d\n",p_hal_data->min_undecorated_pwdb_for_dm));*/
}

u64
phydm_get_rate_bitmap_ex(
	void		*p_dm_void,
	u32		macid,
	u64		ra_mask,
	u8		rssi_level,
	u64	*dm_ra_mask,
	u8	*dm_rte_id
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct sta_info	*p_entry;
	u64	rate_bitmap = 0;
	u8	wireless_mode;

	p_entry = p_dm_odm->p_odm_sta_info[macid];
	if (!IS_STA_VALID(p_entry))
		return ra_mask;
	wireless_mode = p_entry->wireless_mode;
	switch (wireless_mode) {
	case ODM_WM_B:
		if (ra_mask & 0x000000000000000c) /* 11M or 5.5M enable */
			rate_bitmap = 0x000000000000000d;
		else
			rate_bitmap = 0x000000000000000f;
		break;

	case (ODM_WM_G):
	case (ODM_WM_A):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x0000000000000f00;
		else
			rate_bitmap = 0x0000000000000ff0;
		break;

	case (ODM_WM_B|ODM_WM_G):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x0000000000000f00;
		else if (rssi_level == DM_RATR_STA_MIDDLE)
			rate_bitmap = 0x0000000000000ff0;
		else
			rate_bitmap = 0x0000000000000ff5;
		break;

	case (ODM_WM_B|ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_B|ODM_WM_N24G):
	case (ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_A|ODM_WM_N5G):
	{
		if (p_dm_odm->rf_type == ODM_1T2R || p_dm_odm->rf_type == ODM_1T1R) {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x00000000000f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x00000000000ff000;
			else {
				if (*(p_dm_odm->p_band_width) == ODM_BW40M)
					rate_bitmap = 0x00000000000ff015;
				else
					rate_bitmap = 0x00000000000ff005;
			}
		} else if (p_dm_odm->rf_type == ODM_2T2R  || p_dm_odm->rf_type == ODM_2T3R  || p_dm_odm->rf_type == ODM_2T4R) {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x000000000f8f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x000000000f8ff000;
			else {
				if (*(p_dm_odm->p_band_width) == ODM_BW40M)
					rate_bitmap = 0x000000000f8ff015;
				else
					rate_bitmap = 0x000000000f8ff005;
			}
		} else {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x0000000f0f0f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x0000000fcfcfe000;
			else {
				if (*(p_dm_odm->p_band_width) == ODM_BW40M)
					rate_bitmap = 0x0000000ffffff015;
				else
					rate_bitmap = 0x0000000ffffff005;
			}
		}
	}
	break;

	case (ODM_WM_AC|ODM_WM_G):
		if (rssi_level == 1)
			rate_bitmap = 0x00000000fc3f0000;
		else if (rssi_level == 2)
			rate_bitmap = 0x00000000fffff000;
		else
			rate_bitmap = 0x00000000ffffffff;
		break;

	case (ODM_WM_AC|ODM_WM_A):

		if (p_dm_odm->rf_type == ODM_1T2R || p_dm_odm->rf_type == ODM_1T1R) {
			if (rssi_level == 1)				/* add by Gary for ac-series */
				rate_bitmap = 0x00000000003f8000;
			else if (rssi_level == 2)
				rate_bitmap = 0x00000000003fe000;
			else
				rate_bitmap = 0x00000000003ff010;
		} else if (p_dm_odm->rf_type == ODM_2T2R  || p_dm_odm->rf_type == ODM_2T3R  || p_dm_odm->rf_type == ODM_2T4R) {
			if (rssi_level == 1)				/* add by Gary for ac-series */
				rate_bitmap = 0x00000000fe3f8000;       /* VHT 2SS MCS3~9 */
			else if (rssi_level == 2)
				rate_bitmap = 0x00000000fffff000;       /* VHT 2SS MCS0~9 */
			else
				rate_bitmap = 0x00000000fffff010;       /* All */
		} else {
			if (rssi_level == 1)				/* add by Gary for ac-series */
				rate_bitmap = 0x000003f8fe3f8000ULL;       /* VHT 3SS MCS3~9 */
			else if (rssi_level == 2)
				rate_bitmap = 0x000003fffffff000ULL;       /* VHT3SS MCS0~9 */
			else
				rate_bitmap = 0x000003fffffff010ULL;       /* All */
		}
		break;

	default:
		if (p_dm_odm->rf_type == ODM_1T2R || p_dm_odm->rf_type == ODM_1T1R)
			rate_bitmap = 0x00000000000fffff;
		else if (p_dm_odm->rf_type == ODM_2T2R  || p_dm_odm->rf_type == ODM_2T3R  || p_dm_odm->rf_type == ODM_2T4R)
			rate_bitmap = 0x000000000fffffff;
		else
			rate_bitmap = 0x0000003fffffffffULL;
		break;

	}
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, (" ==> rssi_level:0x%02x, wireless_mode:0x%02x, rate_bitmap:0x%016llx\n", rssi_level, wireless_mode, rate_bitmap));

	return ra_mask & rate_bitmap;
}


u32
odm_get_rate_bitmap(
	void		*p_dm_void,
	u32		macid,
	u32		ra_mask,
	u8		rssi_level
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct sta_info	*p_entry;
	u32	rate_bitmap = 0;
	u8	wireless_mode;
	/* u8 	wireless_mode =*(p_dm_odm->p_wireless_mode); */


	p_entry = p_dm_odm->p_odm_sta_info[macid];
	if (!IS_STA_VALID(p_entry))
		return ra_mask;

	wireless_mode = p_entry->wireless_mode;

	switch (wireless_mode) {
	case ODM_WM_B:
		if (ra_mask & 0x0000000c)		/* 11M or 5.5M enable */
			rate_bitmap = 0x0000000d;
		else
			rate_bitmap = 0x0000000f;
		break;

	case (ODM_WM_G):
	case (ODM_WM_A):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x00000f00;
		else
			rate_bitmap = 0x00000ff0;
		break;

	case (ODM_WM_B|ODM_WM_G):
		if (rssi_level == DM_RATR_STA_HIGH)
			rate_bitmap = 0x00000f00;
		else if (rssi_level == DM_RATR_STA_MIDDLE)
			rate_bitmap = 0x00000ff0;
		else
			rate_bitmap = 0x00000ff5;
		break;

	case (ODM_WM_B|ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_B|ODM_WM_N24G):
	case (ODM_WM_G|ODM_WM_N24G):
	case (ODM_WM_A|ODM_WM_N5G):
	{
		if (p_dm_odm->rf_type == ODM_1T2R || p_dm_odm->rf_type == ODM_1T1R) {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x000f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x000ff000;
			else {
				if (*(p_dm_odm->p_band_width) == ODM_BW40M)
					rate_bitmap = 0x000ff015;
				else
					rate_bitmap = 0x000ff005;
			}
		} else {
			if (rssi_level == DM_RATR_STA_HIGH)
				rate_bitmap = 0x0f8f0000;
			else if (rssi_level == DM_RATR_STA_MIDDLE)
				rate_bitmap = 0x0f8ff000;
			else {
				if (*(p_dm_odm->p_band_width) == ODM_BW40M)
					rate_bitmap = 0x0f8ff015;
				else
					rate_bitmap = 0x0f8ff005;
			}
		}
	}
	break;

	case (ODM_WM_AC|ODM_WM_G):
		if (rssi_level == 1)
			rate_bitmap = 0xfc3f0000;
		else if (rssi_level == 2)
			rate_bitmap = 0xfffff000;
		else
			rate_bitmap = 0xffffffff;
		break;

	case (ODM_WM_AC|ODM_WM_A):

		if (p_dm_odm->rf_type == RF_1T1R) {
			if (rssi_level == 1)				/* add by Gary for ac-series */
				rate_bitmap = 0x003f8000;
			else if (rssi_level == 2)
				rate_bitmap = 0x003ff000;
			else
				rate_bitmap = 0x003ff010;
		} else {
			if (rssi_level == 1)				/* add by Gary for ac-series */
				rate_bitmap = 0xfe3f8000;       /* VHT 2SS MCS3~9 */
			else if (rssi_level == 2)
				rate_bitmap = 0xfffff000;       /* VHT 2SS MCS0~9 */
			else
				rate_bitmap = 0xfffff010;       /* All */
		}
		break;

	default:
		if (p_dm_odm->rf_type == RF_1T2R)
			rate_bitmap = 0x000fffff;
		else
			rate_bitmap = 0x0fffffff;
		break;

	}

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RATE_ADAPTIVE, ODM_DBG_LOUD, ("%s ==> rssi_level:0x%02x, wireless_mode:0x%02x, rate_bitmap:0x%08x\n", __func__, rssi_level, wireless_mode, rate_bitmap));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, (" ==> rssi_level:0x%02x, wireless_mode:0x%02x, rate_bitmap:0x%08x\n", rssi_level, wireless_mode, rate_bitmap));

	return ra_mask & rate_bitmap;

}

#endif /* #if (DM_ODM_SUPPORT_TYPE == ODM_CE) */

#elif (DM_ODM_SUPPORT_TYPE & (ODM_AP))


#endif /*#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))*/


/* RA_MASK_PHYDMLIZE, will delete it later*/

#if (RA_MASK_PHYDMLIZE_CE || RA_MASK_PHYDMLIZE_AP || RA_MASK_PHYDMLIZE_WIN)

boolean
odm_ra_state_check(
	void			*p_dm_void,
	s32			RSSI,
	boolean			is_force_update,
	u8			*p_ra_tr_state
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	struct _ODM_RATE_ADAPTIVE *p_ra = &p_dm_odm->rate_adaptive;
	const u8 go_up_gap = 5;
	u8 high_rssi_thresh_for_ra = p_ra->high_rssi_thresh;
	u8 low_rssi_thresh_for_ra = p_ra->low_rssi_thresh;
	u8 ratr_state;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("RSSI= (( %d )), Current_RSSI_level = (( %d ))\n", RSSI, *p_ra_tr_state));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("[Ori RA RSSI Thresh]  High= (( %d )), Low = (( %d ))\n", high_rssi_thresh_for_ra, low_rssi_thresh_for_ra));
	/* threshold Adjustment:*/
	/* when RSSI state trends to go up one or two levels, make sure RSSI is high enough.*/
	/* Here go_up_gap is added to solve the boundary's level alternation issue.*/
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	u8 ultra_low_rssi_thresh_for_ra = p_ra->ultra_low_rssi_thresh;

	if (p_dm_odm->support_ic_type == ODM_RTL8881A)
		low_rssi_thresh_for_ra = 30;		/* for LDPC / BCC switch*/
#endif

	switch (*p_ra_tr_state) {
	case DM_RATR_STA_INIT:
	case DM_RATR_STA_HIGH:
		break;

	case DM_RATR_STA_MIDDLE:
		high_rssi_thresh_for_ra += go_up_gap;
		break;

	case DM_RATR_STA_LOW:
		high_rssi_thresh_for_ra += go_up_gap;
		low_rssi_thresh_for_ra += go_up_gap;
		break;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	case DM_RATR_STA_ULTRA_LOW:
		high_rssi_thresh_for_ra += go_up_gap;
		low_rssi_thresh_for_ra += go_up_gap;
		ultra_low_rssi_thresh_for_ra += go_up_gap;
		break;
#endif

	default:
		ODM_RT_ASSERT(p_dm_odm, false, ("wrong rssi level setting %d !", *p_ra_tr_state));
		break;
	}

	/* Decide ratr_state by RSSI.*/
	if (RSSI > high_rssi_thresh_for_ra)
		ratr_state = DM_RATR_STA_HIGH;
	else if (RSSI > low_rssi_thresh_for_ra)
		ratr_state = DM_RATR_STA_MIDDLE;

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	else if (RSSI > ultra_low_rssi_thresh_for_ra)
		ratr_state = DM_RATR_STA_LOW;
	else
		ratr_state = DM_RATR_STA_ULTRA_LOW;
#else
	else
		ratr_state = DM_RATR_STA_LOW;
#endif
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("[Mod RA RSSI Thresh]  High= (( %d )), Low = (( %d ))\n", high_rssi_thresh_for_ra, low_rssi_thresh_for_ra));
	/*printk("==>%s,ratr_state:0x%02x,RSSI:%d\n",__FUNCTION__,ratr_state,RSSI);*/

	if (*p_ra_tr_state != ratr_state || is_force_update) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_RA_MASK, ODM_DBG_LOUD, ("[RSSI Level Update] %d->%d\n", *p_ra_tr_state, ratr_state));
		*p_ra_tr_state = ratr_state;
		return true;
	}

	return false;
}

#endif
