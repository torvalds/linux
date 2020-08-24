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
 * ************************************************************
 */

#include "mp_precomp.h"
#include "phydm_precomp.h"

void halrf_basic_profile(void *dm_void, u32 *_used, char *output, u32 *_out_len)
{
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 rf_release_ver = 0;

	switch (dm->support_ic_type) {
#if (RTL8814A_SUPPORT)
	case ODM_RTL8814A:
		rf_release_ver = RF_RELEASE_VERSION_8814A;
		break;
#endif

#if (RTL8821C_SUPPORT)
	case ODM_RTL8821C:
		rf_release_ver = RF_RELEASE_VERSION_8821C;
		break;
#endif

#if (RTL8822B_SUPPORT)
	case ODM_RTL8822B:
		rf_release_ver = RF_RELEASE_VERSION_8822B;
		break;
#endif

#if (RTL8822C_SUPPORT)
	case ODM_RTL8822C:
		rf_release_ver = RF_RELEASE_VERSION_8822C;
		break;
#endif

#if (RTL8814B_SUPPORT)
	case ODM_RTL8814B:
		rf_release_ver = RF_RELEASE_VERSION_8814B;
		break;
#endif

#if (RTL8812F_SUPPORT)
	case ODM_RTL8812F:
		rf_release_ver = RF_RELEASE_VERSION_8812F;
		break;
#endif

#if (RTL8198F_SUPPORT)
	case ODM_RTL8198F:
		rf_release_ver = RF_RELEASE_VERSION_8198F;
		break;
#endif

#if (RTL8197F_SUPPORT)
	case ODM_RTL8197F:
		rf_release_ver = RF_RELEASE_VERSION_8197F;
		break;
#endif

#if (RTL8192F_SUPPORT)
	case ODM_RTL8192F:
		rf_release_ver = RF_RELEASE_VERSION_8192F;
		break;
#endif

#if (RTL8710B_SUPPORT)
	case ODM_RTL8710B:
		rf_release_ver = RF_RELEASE_VERSION_8710B;
		break;
#endif

#if (RTL8195B_SUPPORT)
	case ODM_RTL8195B:
		rf_release_ver = RF_RELEASE_VERSION_8195B;
		break;
#endif
	}

	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %d\n",
		 "RF Para Release Ver", rf_release_ver);

	/* HAL RF version List */
	PDM_SNPF(out_len, used, output + used, out_len - used, "%-35s\n",
		 "% HAL RF version %");
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "Power Tracking", HALRF_POWRTRACKING_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "  %-35s: %s %s\n", "IQK",
		 (dm->fw_offload_ability & PHYDM_RF_IQK_OFFLOAD) ? "FW" :
		 HALRF_IQK_VER,
		 (halrf_match_iqk_version(dm_void)) ? "(match)" : "(mismatch)");

	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "LCK", HALRF_LCK_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "DPK", HALRF_DPK_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "TSSI", HALRF_TSSI_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "KFREE", HALRF_KFREE_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "TX 2G Current Calibration", HALRF_PABIASK_VER);
	PDM_SNPF(out_len, used, output + used, out_len - used, "  %-35s: %s\n",
		 "RFK Init. Parameter", HALRF_RFK_INIT_VER);

	*_used = used;
	*_out_len = out_len;
#endif
}

void halrf_debug_trace(void *dm_void, char input[][16], u32 *_used,
		       char *output, u32 *_out_len)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;
	u32 one = 1;
	u32 used = *_used;
	u32 out_len = *_out_len;
	u32 rf_var[10] = {0};
	u8 i;

	for (i = 0; i < 5; i++)
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 2], DCMD_DECIMAL, &rf_var[i]);

	if (rf_var[0] == 100) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "\n[DBG MSG] RF Selection\n");
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "00. (( %s ))TX_PWR_TRACK\n",
			 ((rf->rf_dbg_comp & DBG_RF_TX_PWR_TRACK) ? ("V") :
			 (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "01. (( %s ))IQK\n",
			 ((rf->rf_dbg_comp & DBG_RF_IQK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "02. (( %s ))LCK\n",
			 ((rf->rf_dbg_comp & DBG_RF_LCK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "03. (( %s ))DPK\n",
			 ((rf->rf_dbg_comp & DBG_RF_DPK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "04. (( %s ))TXGAPK\n",
			 ((rf->rf_dbg_comp & DBG_RF_TXGAPK) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "29. (( %s ))MP\n",
			 ((rf->rf_dbg_comp & DBG_RF_MP) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "30. (( %s ))TMP\n",
			 ((rf->rf_dbg_comp & DBG_RF_TMP) ? ("V") : (".")));
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "31. (( %s ))INIT\n",
			 ((rf->rf_dbg_comp & DBG_RF_INIT) ? ("V") : (".")));

	} else if (rf_var[0] == 101) {
		rf->rf_dbg_comp = 0;
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "Disable all DBG COMP\n");
	} else {
		if (rf_var[1] == 1) /*enable*/
			rf->rf_dbg_comp |= (one << rf_var[0]);
		else if (rf_var[1] == 2) /*disable*/
			rf->rf_dbg_comp &= ~(one << rf_var[0]);
	}
	PDM_SNPF(out_len, used, output + used, out_len - used,
		 "\nCurr-RF_Dbg_Comp = 0x%x\n", rf->rf_dbg_comp);

	*_used = used;
	*_out_len = out_len;
}

void halrf_dack_debug_cmd(void *dm_void, char input[][16])
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;
	u32 dm_value[10] = {0};
	u8 i;

	for (i = 0; i < 7; i++)
		if (input[i + 1])
			PHYDM_SSCANF(input[i + 2], DCMD_DECIMAL, &dm_value[i]);

	if (dm_value[0] == 1)
		halrf_dack_trigger(dm, true);
	else			
		halrf_dack_trigger(dm, false);	
}

struct halrf_command {
	char name[16];
	u8 id;
};

enum halrf_CMD_ID {
	HALRF_HELP,
	HALRF_SUPPORTABILITY,
	HALRF_DBG_COMP,
	HALRF_PROFILE,
	HALRF_IQK_INFO,
	HALRF_IQK,
	HALRF_IQK_DEBUG,
	HALRF_DPK,
	HALRF_DACK,
	HALRF_DACK_DEBUG,
	HALRF_DUMP_RFK_REG,
#ifdef CONFIG_2G_BAND_SHIFT
	HAL_BAND_SHIFT,
#endif
};

struct halrf_command halrf_cmd_ary[] = {
	{"-h", HALRF_HELP},
	{"ability", HALRF_SUPPORTABILITY},
	{"dbg", HALRF_DBG_COMP},
	{"profile", HALRF_PROFILE},
	{"iqk_info", HALRF_IQK_INFO},
	{"iqk", HALRF_IQK},
	{"iqk_dbg", HALRF_IQK_DEBUG},
	{"dpk", HALRF_DPK},
	{"dack", HALRF_DACK},
	{"dack_dbg", HALRF_DACK_DEBUG},
	{"dump_rfk_reg", HALRF_DUMP_RFK_REG},
#ifdef CONFIG_2G_BAND_SHIFT
	{"band_shift", HAL_BAND_SHIFT},
#endif
};

void halrf_cmd_parser(void *dm_void, char input[][16], u32 *_used, char *output,
		      u32 *_out_len, u32 input_num)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
#ifdef CONFIG_PHYDM_DEBUG_FUNCTION
	u8 id = 0;
	u32 rf_var[10] = {0};
	u32 i, input_idx = 0;
	u32 halrf_ary_size =
			sizeof(halrf_cmd_ary) / sizeof(struct halrf_command);
	u32 used = *_used;
	u32 out_len = *_out_len;

	/* Parsing Cmd ID */
	for (i = 0; i < halrf_ary_size; i++) {
		if (strcmp(halrf_cmd_ary[i].name, input[1]) == 0) {
			id = halrf_cmd_ary[i].id;
			break;
		}
	}

	if (i == halrf_ary_size) {
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "RF Cmd not found\n");
		return;
	}

	switch (id) {
	case HALRF_HELP:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "RF cmd ==>\n");

		for (i = 0; i < halrf_ary_size - 1; i++) {
			PDM_SNPF(out_len, used, output + used, out_len - used,
				 "  %-5d: %s\n", i, halrf_cmd_ary[i + 1].name);
		}
		break;
	case HALRF_SUPPORTABILITY:
		halrf_support_ability_debug(dm, &input[0], &used, output,
					    &out_len);
		break;
#ifdef CONFIG_2G_BAND_SHIFT
	case HAL_BAND_SHIFT:
		halrf_support_band_shift_debug(dm, &input[0], &used, output,
					       &out_len);
		break;
#endif
	case HALRF_DBG_COMP:
		halrf_debug_trace(dm, &input[0], &used, output, &out_len);
		break;
	case HALRF_PROFILE:
		halrf_basic_profile(dm, &used, output, &out_len);
		break;
	case HALRF_IQK_INFO:
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		halrf_iqk_info_dump(dm, &used, output, &out_len);
#endif
		break;
	case HALRF_IQK:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "TRX IQK Trigger\n");
		halrf_iqk_trigger(dm, false);
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1)
		halrf_iqk_info_dump(dm, &used, output, &out_len);
#endif
		break;
	case HALRF_IQK_DEBUG:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "IQK DEBUG!!!!!\n");
		for (i = 0; i < 5; i++) {
			if (input[i + 1]) {
				PHYDM_SSCANF(input[i + 2], DCMD_HEX,
					     &rf_var[i]);
				input_idx++;
			}
		}

		if (input_idx >= 1) {
#if (RTL8822B_SUPPORT == 1 || RTL8821C_SUPPORT == 1 || RTL8822C_SUPPORT == 1 || RTL8814B_SUPPORT == 1)
			if (dm->support_ic_type & (ODM_RTL8822B | ODM_RTL8821C | ODM_RTL8822C | ODM_RTL8814B))
				halrf_iqk_debug(dm, (u32 *)rf_var, &used,
						output, &out_len);
#endif
		}
		break;
	case HALRF_DPK:
		halrf_dpk_debug_cmd(dm, input, &used, output, &out_len);
		break;
	case HALRF_DACK:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "DACK Trigger\n");
		halrf_dack_debug_cmd(dm, &input[0]);
		break;
	case HALRF_DACK_DEBUG:
		PDM_SNPF(out_len, used, output + used, out_len - used,
			 "DACK DEBUG\n");
		halrf_dack_dbg(dm);
		break;
	case HALRF_DUMP_RFK_REG:
		halrf_dump_rfk_reg(dm, input, &used, output, &out_len);
		break;
	default:
		break;
	}

	*_used = used;
	*_out_len = out_len;
#endif
}

void halrf_init_debug_setting(void *dm_void)
{
	struct dm_struct *dm = (struct dm_struct *)dm_void;
	struct _hal_rf_ *rf = &dm->rf_table;

	rf->rf_dbg_comp =

#if DBG
#if 0
	/*DBG_RF_TX_PWR_TRACK	|*/
	/*DBG_RF_IQK		| */
	/*DBG_RF_LCK		| */
	/*DBG_RF_DPK		| */
	/*DBG_RF_DACK		| */
	/*DBG_RF_TXGAPK		| */
	/*DBG_RF_MP			| */
	/*DBG_RF_TMP		| */
	/*DBG_RF_INIT		| */
#endif
#endif
	0;
}
