/* SPDX-License-Identifier: GPL-2.0 */
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

/*Image2HeaderVersion: 3.5.2*/
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8188F_SUPPORT == 1)
static boolean
check_positive(
	struct dm_struct *dm,
	const u32	condition1,
	const u32	condition2,
	const u32	condition3,
	const u32	condition4
)
{
	u8	_board_type = ((dm->board_type & BIT(4)) >> 4) << 0 | /* _GLNA*/
			((dm->board_type & BIT(3)) >> 3) << 1 | /* _GPA*/
			((dm->board_type & BIT(7)) >> 7) << 2 | /* _ALNA*/
			((dm->board_type & BIT(6)) >> 6) << 3 | /* _APA */
			((dm->board_type & BIT(2)) >> 2) << 4 | /* _BT*/
			((dm->board_type & BIT(1)) >> 1) << 5 | /* _NGFF*/
			((dm->board_type & BIT(5)) >> 5) << 6;  /* _TRSWT*/

	u32	cond1 = condition1, cond2 = condition2, cond3 = condition3, cond4 = condition4;

	u8	cut_version_for_para = (dm->cut_version ==  ODM_CUT_A) ? 15 : dm->cut_version;
	u8	pkg_type_for_para = (dm->package_type == 0) ? 15 : dm->package_type;

	u32	driver1 = cut_version_for_para << 24 |
			(dm->support_interface & 0xF0) << 16 |
			dm->support_platform << 16 |
			pkg_type_for_para << 12 |
			(dm->support_interface & 0x0F) << 8  |
			_board_type;

	u32	driver2 = (dm->type_glna & 0xFF) <<  0 |
			(dm->type_gpa & 0xFF)  <<  8 |
			(dm->type_alna & 0xFF) << 16 |
			(dm->type_apa & 0xFF)  << 24;

	u32	driver3 = 0;

	u32	driver4 = (dm->type_glna & 0xFF00) >>  8 |
			(dm->type_gpa & 0xFF00) |
			(dm->type_alna & 0xFF00) << 8 |
			(dm->type_apa & 0xFF00)  << 16;

	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "===> %s (cond1, cond2, cond3, cond4) = (0x%X 0x%X 0x%X 0x%X)\n",
		  __func__, cond1, cond2, cond3, cond4);
	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "===> %s (driver1, driver2, driver3, driver4) = (0x%X 0x%X 0x%X 0x%X)\n",
		  __func__, driver1, driver2, driver3, driver4);

	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "	(Platform, Interface) = (0x%X, 0x%X)\n",
		  dm->support_platform, dm->support_interface);
	PHYDM_DBG(dm, ODM_COMP_INIT,
		  "	(Board, Package) = (0x%X, 0x%X)\n", dm->board_type,
		  dm->package_type);


	/*============== value Defined Check ===============*/
	/*QFN type [15:12] and cut version [27:24] need to do value check*/

	if (((cond1 & 0x0000F000) != 0) && ((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return false;
	if (((cond1 & 0x0F000000) != 0) && ((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return false;

	/*=============== Bit Defined Check ================*/
	/* We don't care [31:28] */

	cond1 &= 0x00FF0FFF;
	driver1 &= 0x00FF0FFF;

	if ((cond1 & driver1) == cond1) {
		u32	bit_mask = 0;

		if ((cond1 & 0x0F) == 0) /* board_type is DONTCARE*/
			return true;

		if ((cond1 & BIT(0)) != 0) /*GLNA*/
			bit_mask |= 0x000000FF;
		if ((cond1 & BIT(1)) != 0) /*GPA*/
			bit_mask |= 0x0000FF00;
		if ((cond1 & BIT(2)) != 0) /*ALNA*/
			bit_mask |= 0x00FF0000;
		if ((cond1 & BIT(3)) != 0) /*APA*/
			bit_mask |= 0xFF000000;

		if (((cond2 & bit_mask) == (driver2 & bit_mask)) && ((cond4 & bit_mask) == (driver4 & bit_mask)))  /* board_type of each RF path is matched*/
			return true;
		else
			return false;
	} else
		return false;
}

/******************************************************************************
*                           radioa.TXT
******************************************************************************/

u32 array_mp_8188f_radioa[] = {
		0x000, 0x00030000,
		0x008, 0x00008400,
		0x018, 0x00000407,
		0x019, 0x00000012,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0x01B, 0x00000C6C,
	0xA0000000,	0x00000000,
		0x01B, 0x00001C6C,
	0xB0000000,	0x00000000,
		0x01E, 0x00080009,
		0x01F, 0x00000880,
		0x02F, 0x0001A060,
		0x03F, 0x00028000,
		0x042, 0x000060C0,
		0x057, 0x000D0000,
		0x058, 0x000C0160,
		0x067, 0x00001552,
		0x083, 0x00000000,
		0x0B0, 0x000FF9F0,
		0x0B1, 0x00022218,
		0x0B2, 0x00034C00,
	0x8c000400,	0x00000000,	0x40000000,	0x00000000,
		0x0B4, 0x0004486B,
	0x9c000000,	0x00000000,	0x40000000,	0x00000000,
		0x0B4, 0x0004486B,
	0xA0000000,	0x00000000,
		0x0B4, 0x0004484B,
	0xB0000000,	0x00000000,
		0x0B5, 0x0000112A,
		0x0B6, 0x0000053E,
		0x0B7, 0x00010408,
	0x8c000400,	0x00000000,	0x40000000,	0x00000000,
		0x0B8, 0x000100AF,
	0x9c000000,	0x00000000,	0x40000000,	0x00000000,
		0x0B8, 0x000100AF,
	0xA0000000,	0x00000000,
		0x0B8, 0x00010200,
	0xB0000000,	0x00000000,
		0x0B9, 0x00080001,
		0x0BA, 0x00040001,
		0x0BB, 0x00000400,
		0x0BF, 0x000C0000,
		0x0C2, 0x00002400,
		0x0C3, 0x00000009,
		0x0C4, 0x00040C91,
		0x0C5, 0x00099999,
		0x0C6, 0x000000A3,
		0x0C7, 0x0008F820,
		0x0C8, 0x00076C06,
		0x0C9, 0x00000000,
		0x0CA, 0x00080000,
		0x0DF, 0x00000180,
		0x0EF, 0x000001A0,
	0x8f000000,	0x00000000,	0x40000000,	0x00000000,
		0x051, 0x000E8333,
	0xA0000000,	0x00000000,
		0x051, 0x000E8231,
	0xB0000000,	0x00000000,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0x052, 0x000FAC88,
	0x9f000000,	0x00000000,	0x40000000,	0x00000000,
		0x052, 0x000FAC2C,
	0xA0000000,	0x00000000,
		0x052, 0x000FAC2F,
	0xB0000000,	0x00000000,
	0x8f000000,	0x00000000,	0x40000000,	0x00000000,
		0x053, 0x00000103,
	0xA0000000,	0x00000000,
		0x053, 0x000001C1,
	0xB0000000,	0x00000000,
		0x054, 0x00055007,
		0x056, 0x000517F0,
	0x8f000000,	0x00000000,	0x40000000,	0x00000000,
		0x035, 0x00000099,
		0x035, 0x00000199,
		0x035, 0x00000299,
	0xA0000000,	0x00000000,
		0x035, 0x00000090,
		0x035, 0x00000190,
		0x035, 0x00000290,
	0xB0000000,	0x00000000,
	0x8b000000,	0x00000000,	0x40000000,	0x00000000,
		0x05F, 0x00023500,
	0x9c000400,	0x00000000,	0x40000000,	0x00000000,
		0x05F, 0x00023504,
	0x9c000000,	0x00000000,	0x40000000,	0x00000000,
		0x05F, 0x00023504,
	0xA0000000,	0x00000000,
		0x05F, 0x000FFFFF,
	0xB0000000,	0x00000000,
	0x8f000000,	0x00000000,	0x40000000,	0x00000000,
		0x036, 0x00000064,
		0x036, 0x00008064,
		0x036, 0x00010064,
		0x036, 0x00018064,
	0x9c000400,	0x00000000,	0x40000000,	0x00000000,
		0x036, 0x00001068,
		0x036, 0x00009068,
		0x036, 0x00011068,
		0x036, 0x00019068,
	0x9c000000,	0x00000000,	0x40000000,	0x00000000,
		0x036, 0x00001068,
		0x036, 0x00009068,
		0x036, 0x00011068,
		0x036, 0x00019068,
	0xA0000000,	0x00000000,
		0x036, 0x00001064,
		0x036, 0x00009064,
		0x036, 0x00011064,
		0x036, 0x00019064,
	0xB0000000,	0x00000000,
		0x018, 0x00000C07,
		0x05A, 0x00048000,
		0x019, 0x000739D0,
	0x80000400,	0x00000000,	0x40000000,	0x00000000,
		0x034, 0x0000ADD2,
		0x034, 0x00009DCF,
		0x034, 0x00008CF2,
		0x034, 0x00007CEF,
		0x034, 0x00006CEC,
		0x034, 0x00005CE9,
		0x034, 0x00004CCE,
		0x034, 0x00003CCB,
		0x034, 0x00002CC8,
		0x034, 0x00001C4B,
		0x034, 0x00000C48,
	0x9f000000,	0x00000000,	0x40000000,	0x00000000,
		0x034, 0x0000ADD6,
		0x034, 0x00009DD3,
		0x034, 0x00008CF4,
		0x034, 0x00007CF1,
		0x034, 0x00006CEE,
		0x034, 0x00005CEB,
		0x034, 0x00004CCE,
		0x034, 0x00003CCB,
		0x034, 0x00002CC8,
		0x034, 0x00001C4B,
		0x034, 0x00000C48,
	0xA0000000,	0x00000000,
		0x034, 0x0000ADD2,
		0x034, 0x00009DD0,
		0x034, 0x00008CF2,
		0x034, 0x00007CEF,
		0x034, 0x00006CEC,
		0x034, 0x00005CD1,
		0x034, 0x00004CCE,
		0x034, 0x00003CCB,
		0x034, 0x00002CC8,
		0x034, 0x00001C4B,
		0x034, 0x00000C48,
	0xB0000000,	0x00000000,
		0x000, 0x00030159,
		0x084, 0x00048000,
		0x086, 0x0000002A,
		0x087, 0x00000025,
		0x08E, 0x00065540,
		0x08F, 0x00088000,
		0x0EF, 0x000020A0,
		0x03B, 0x000F0F00,
		0x03B, 0x000E0B00,
		0x03B, 0x000D0900,
		0x03B, 0x000C0700,
		0x03B, 0x000B0600,
		0x03B, 0x000A0400,
		0x03B, 0x00090200,
		0x03B, 0x00080000,
		0x03B, 0x0007BF00,
		0x03B, 0x00060B00,
		0x03B, 0x0005C900,
		0x03B, 0x00040700,
		0x03B, 0x00030600,
		0x03B, 0x0002D500,
		0x03B, 0x00010200,
		0x03B, 0x0000E000,
		0x0EF, 0x000000A0,
		0x0EF, 0x00000010,
		0x03B, 0x0000C0A8,
		0x03B, 0x00010400,
		0x0EF, 0x00000000,
		0x0EF, 0x00080000,
		0x030, 0x00010000,
		0x031, 0x0000000F,
		0x032, 0x00007EFE,
		0x0EF, 0x00000000,
		0x000, 0x00010159,
		0x018, 0x0000FC07,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x01F, 0x00080003,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x01E, 0x00000001,
		0x01F, 0x00080000,
		0x000, 0x00033D95,

};

void
odm_read_and_config_mp_8188f_radioa(struct dm_struct *dm)
{
	u32	i = 0;
	u8	c_cond;
	boolean	is_matched = true, is_skipped = false;
	u32	array_len = sizeof(array_mp_8188f_radioa) / sizeof(u32);
	u32	*array = array_mp_8188f_radioa;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> %s\n", __func__);

	while ((i + 1) < array_len) {
		v1 = array[i];
		v2 = array[i + 1];

		if (v1 & (BIT(31) | BIT(30))) {/*positive & negative condition*/
			if (v1 & BIT(31)) {/* positive condition*/
				c_cond  = (u8)((v1 & (BIT(29) | BIT(28))) >> 28);
				if (c_cond == COND_ENDIF) {/*end*/
					is_matched = true;
					is_skipped = false;
					PHYDM_DBG(dm, ODM_COMP_INIT, "ENDIF\n");
				} else if (c_cond == COND_ELSE) { /*else*/
					is_matched = is_skipped ? false : true;
					PHYDM_DBG(dm, ODM_COMP_INIT, "ELSE\n");
				} else {/*if , else if*/
					pre_v1 = v1;
					pre_v2 = v2;
					PHYDM_DBG(dm, ODM_COMP_INIT, "IF or ELSE IF\n");
				}
			} else if (v1 & BIT(30)) { /*negative condition*/
				if (is_skipped == false) {
					if (check_positive(dm, pre_v1, pre_v2, v1, v2)) {
						is_matched = true;
						is_skipped = true;
					} else {
						is_matched = false;
						is_skipped = false;
					}
				} else
					is_matched = false;
			}
		} else {
			if (is_matched)
				odm_config_rf_radio_a_8188f(dm, v1, v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8188f_radioa(void)
{
		return 38;
}

/******************************************************************************
*                           txpowertrack_ap.TXT
******************************************************************************/

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8188f[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8188f[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8188f[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8188f[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8188f[] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8188f[] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8188f[] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8188f[] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
#endif

void
odm_read_and_config_mp_8188f_txpowertrack_ap(struct dm_struct *dm)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct dm_rf_calibration_struct  *cali_info = &(dm->rf_calibrate_info);

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> ODM_ReadAndConfig_MP_mp_8188f\n");


	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8188f, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           txpowertrack_sdio.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8188f[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8188f[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8188f[]    = {0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 9, 10, 11, 12, 13, 14, 15, 16, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8188f[]    = {0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8188f[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8188f[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8188f[] = {0, 1, 2, 3, 4, 6, 7, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 16, 16, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8188f[] = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 7, 8, 9, 10, 11, 12, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14};
#endif

void
odm_read_and_config_mp_8188f_txpowertrack_sdio(struct dm_struct *dm)
{
#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	struct dm_rf_calibration_struct  *cali_info = &(dm->rf_calibrate_info);

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> ODM_ReadAndConfig_MP_mp_8188f\n");


	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8188f, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           txpowertrack_usb.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_USB_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8188f[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8188f[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8188f[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8188f[]    = {0, 1, 1, 2, 3, 4, 4, 4, 5, 6, 7, 8, 8, 9, 10, 11, 12, 13, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8188f[]    = {0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8188f[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8188f[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8188f[] = {0, 1, 2, 3, 4, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8188f[] = {0, 0, 1, 2, 2, 3, 3, 4, 6, 6, 7, 8, 8, 10, 10, 11, 13, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15};
#endif

void
odm_read_and_config_mp_8188f_txpowertrack_usb(struct dm_struct *dm)
{
#if DEV_BUS_TYPE == RT_USB_INTERFACE
	struct dm_rf_calibration_struct  *cali_info = &(dm->rf_calibrate_info);

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> ODM_ReadAndConfig_MP_mp_8188f\n");


	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE);

	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(dm, cali_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8188f, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           txpwr_lmt.TXT
******************************************************************************/

const char *array_mp_8188f_txpwr_lmt[] = {
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "11", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "11", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "12", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "12", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "12", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "13", "26",
	"ETSI", "2.4G", "20M", "CCK", "1T", "13", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "13", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "14", "63",
	"MKK", "2.4G", "20M", "CCK", "1T", "14", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "02", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "03", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "03", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "03", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "04", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "04", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "04", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "05", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "05", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "05", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "06", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "06", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "06", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "07", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "07", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "07", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "08", "30",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "08", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "08", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "09", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "09", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "09", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "10", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "10", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "10", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "11", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "11", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "11", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "24",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "16",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "13", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "13", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"MKK", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "1T", "01", "28",
	"ETSI", "2.4G", "20M", "HT", "1T", "01", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "01", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "02", "28",
	"ETSI", "2.4G", "20M", "HT", "1T", "02", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "02", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "03", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "03", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "03", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "04", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "04", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "04", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "05", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "05", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "05", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "06", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "06", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "06", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "07", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "07", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "07", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "08", "30",
	"ETSI", "2.4G", "20M", "HT", "1T", "08", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "08", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "09", "28",
	"ETSI", "2.4G", "20M", "HT", "1T", "09", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "09", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "10", "28",
	"ETSI", "2.4G", "20M", "HT", "1T", "10", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "10", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "11", "28",
	"ETSI", "2.4G", "20M", "HT", "1T", "11", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "11", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "12", "24",
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "12", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "16",
	"ETSI", "2.4G", "20M", "HT", "1T", "13", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "13", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "14", "63",
	"MKK", "2.4G", "20M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "2T", "01", "28",
	"ETSI", "2.4G", "20M", "HT", "2T", "01", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "01", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "02", "28",
	"ETSI", "2.4G", "20M", "HT", "2T", "02", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "02", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "03", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "03", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "03", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "04", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "04", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "04", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "05", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "05", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "05", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "06", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "06", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "06", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "07", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "07", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "07", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "08", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "08", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "08", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "09", "28",
	"ETSI", "2.4G", "20M", "HT", "2T", "09", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "09", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "10", "28",
	"ETSI", "2.4G", "20M", "HT", "2T", "10", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "10", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "11", "28",
	"ETSI", "2.4G", "20M", "HT", "2T", "11", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "11", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "12", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "12", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "13", "30",
	"MKK", "2.4G", "20M", "HT", "2T", "13", "30",
	"FCC", "2.4G", "20M", "HT", "2T", "14", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "14", "63",
	"MKK", "2.4G", "20M", "HT", "2T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "01", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "01", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "02", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "02", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "1T", "03", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "03", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "03", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "04", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "04", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "04", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "05", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "05", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "05", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "06", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "06", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "06", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "07", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "07", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "07", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "08", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "08", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "08", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "09", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "09", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "09", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "10", "24",
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "10", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "11", "10",
	"ETSI", "2.4G", "40M", "HT", "1T", "11", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "11", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "12", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "12", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "13", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "13", "26",
	"MKK", "2.4G", "40M", "HT", "1T", "13", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "14", "63",
	"ETSI", "2.4G", "40M", "HT", "1T", "14", "63",
	"MKK", "2.4G", "40M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "01", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "01", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "01", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "02", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "02", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "02", "63",
	"FCC", "2.4G", "40M", "HT", "2T", "03", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "03", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "03", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "04", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "04", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "04", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "05", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "05", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "05", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "06", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "06", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "06", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "07", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "07", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "07", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "08", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "08", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "08", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "09", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "09", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "09", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "10", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "10", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "10", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "11", "26",
	"ETSI", "2.4G", "40M", "HT", "2T", "11", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "11", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "12", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "12", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "13", "26",
	"MKK", "2.4G", "40M", "HT", "2T", "13", "26",
	"FCC", "2.4G", "40M", "HT", "2T", "14", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "14", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "14", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "36", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "36", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "36", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "40", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "40", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "40", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "44", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "44", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "44", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "48", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "48", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "48", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "52", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "52", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "52", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "56", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "56", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "56", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "60", "32",
	"ETSI", "5G", "20M", "OFDM", "1T", "60", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "60", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "64", "28",
	"ETSI", "5G", "20M", "OFDM", "1T", "64", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "64", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "100", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "100", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "100", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "114", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "114", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "114", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "108", "32",
	"ETSI", "5G", "20M", "OFDM", "1T", "108", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "108", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "112", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "112", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "112", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "116", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "116", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "116", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "120", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "120", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "120", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "124", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "124", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "124", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "128", "32",
	"ETSI", "5G", "20M", "OFDM", "1T", "128", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "128", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "132", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "132", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "132", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "136", "30",
	"ETSI", "5G", "20M", "OFDM", "1T", "136", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "136", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "140", "28",
	"ETSI", "5G", "20M", "OFDM", "1T", "140", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "140", "32",
	"FCC", "5G", "20M", "OFDM", "1T", "149", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "149", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "149", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "153", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "153", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "153", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "157", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "157", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "157", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "161", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "161", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "161", "63",
	"FCC", "5G", "20M", "OFDM", "1T", "165", "34",
	"ETSI", "5G", "20M", "OFDM", "1T", "165", "32",
	"MKK", "5G", "20M", "OFDM", "1T", "165", "63",
	"FCC", "5G", "20M", "HT", "1T", "36", "30",
	"ETSI", "5G", "20M", "HT", "1T", "36", "32",
	"MKK", "5G", "20M", "HT", "1T", "36", "32",
	"FCC", "5G", "20M", "HT", "1T", "40", "30",
	"ETSI", "5G", "20M", "HT", "1T", "40", "32",
	"MKK", "5G", "20M", "HT", "1T", "40", "32",
	"FCC", "5G", "20M", "HT", "1T", "44", "30",
	"ETSI", "5G", "20M", "HT", "1T", "44", "32",
	"MKK", "5G", "20M", "HT", "1T", "44", "32",
	"FCC", "5G", "20M", "HT", "1T", "48", "30",
	"ETSI", "5G", "20M", "HT", "1T", "48", "32",
	"MKK", "5G", "20M", "HT", "1T", "48", "32",
	"FCC", "5G", "20M", "HT", "1T", "52", "34",
	"ETSI", "5G", "20M", "HT", "1T", "52", "32",
	"MKK", "5G", "20M", "HT", "1T", "52", "32",
	"FCC", "5G", "20M", "HT", "1T", "56", "34",
	"ETSI", "5G", "20M", "HT", "1T", "56", "32",
	"MKK", "5G", "20M", "HT", "1T", "56", "32",
	"FCC", "5G", "20M", "HT", "1T", "60", "32",
	"ETSI", "5G", "20M", "HT", "1T", "60", "32",
	"MKK", "5G", "20M", "HT", "1T", "60", "32",
	"FCC", "5G", "20M", "HT", "1T", "64", "28",
	"ETSI", "5G", "20M", "HT", "1T", "64", "32",
	"MKK", "5G", "20M", "HT", "1T", "64", "32",
	"FCC", "5G", "20M", "HT", "1T", "100", "30",
	"ETSI", "5G", "20M", "HT", "1T", "100", "32",
	"MKK", "5G", "20M", "HT", "1T", "100", "32",
	"FCC", "5G", "20M", "HT", "1T", "114", "30",
	"ETSI", "5G", "20M", "HT", "1T", "114", "32",
	"MKK", "5G", "20M", "HT", "1T", "114", "32",
	"FCC", "5G", "20M", "HT", "1T", "108", "32",
	"ETSI", "5G", "20M", "HT", "1T", "108", "32",
	"MKK", "5G", "20M", "HT", "1T", "108", "32",
	"FCC", "5G", "20M", "HT", "1T", "112", "34",
	"ETSI", "5G", "20M", "HT", "1T", "112", "32",
	"MKK", "5G", "20M", "HT", "1T", "112", "32",
	"FCC", "5G", "20M", "HT", "1T", "116", "34",
	"ETSI", "5G", "20M", "HT", "1T", "116", "32",
	"MKK", "5G", "20M", "HT", "1T", "116", "32",
	"FCC", "5G", "20M", "HT", "1T", "120", "34",
	"ETSI", "5G", "20M", "HT", "1T", "120", "32",
	"MKK", "5G", "20M", "HT", "1T", "120", "32",
	"FCC", "5G", "20M", "HT", "1T", "124", "34",
	"ETSI", "5G", "20M", "HT", "1T", "124", "32",
	"MKK", "5G", "20M", "HT", "1T", "124", "32",
	"FCC", "5G", "20M", "HT", "1T", "128", "32",
	"ETSI", "5G", "20M", "HT", "1T", "128", "32",
	"MKK", "5G", "20M", "HT", "1T", "128", "32",
	"FCC", "5G", "20M", "HT", "1T", "132", "30",
	"ETSI", "5G", "20M", "HT", "1T", "132", "32",
	"MKK", "5G", "20M", "HT", "1T", "132", "32",
	"FCC", "5G", "20M", "HT", "1T", "136", "30",
	"ETSI", "5G", "20M", "HT", "1T", "136", "32",
	"MKK", "5G", "20M", "HT", "1T", "136", "32",
	"FCC", "5G", "20M", "HT", "1T", "140", "28",
	"ETSI", "5G", "20M", "HT", "1T", "140", "32",
	"MKK", "5G", "20M", "HT", "1T", "140", "32",
	"FCC", "5G", "20M", "HT", "1T", "149", "34",
	"ETSI", "5G", "20M", "HT", "1T", "149", "32",
	"MKK", "5G", "20M", "HT", "1T", "149", "63",
	"FCC", "5G", "20M", "HT", "1T", "153", "34",
	"ETSI", "5G", "20M", "HT", "1T", "153", "32",
	"MKK", "5G", "20M", "HT", "1T", "153", "63",
	"FCC", "5G", "20M", "HT", "1T", "157", "34",
	"ETSI", "5G", "20M", "HT", "1T", "157", "32",
	"MKK", "5G", "20M", "HT", "1T", "157", "63",
	"FCC", "5G", "20M", "HT", "1T", "161", "34",
	"ETSI", "5G", "20M", "HT", "1T", "161", "32",
	"MKK", "5G", "20M", "HT", "1T", "161", "63",
	"FCC", "5G", "20M", "HT", "1T", "165", "34",
	"ETSI", "5G", "20M", "HT", "1T", "165", "32",
	"MKK", "5G", "20M", "HT", "1T", "165", "63",
	"FCC", "5G", "20M", "HT", "2T", "36", "28",
	"ETSI", "5G", "20M", "HT", "2T", "36", "30",
	"MKK", "5G", "20M", "HT", "2T", "36", "30",
	"FCC", "5G", "20M", "HT", "2T", "40", "28",
	"ETSI", "5G", "20M", "HT", "2T", "40", "30",
	"MKK", "5G", "20M", "HT", "2T", "40", "30",
	"FCC", "5G", "20M", "HT", "2T", "44", "28",
	"ETSI", "5G", "20M", "HT", "2T", "44", "30",
	"MKK", "5G", "20M", "HT", "2T", "44", "30",
	"FCC", "5G", "20M", "HT", "2T", "48", "28",
	"ETSI", "5G", "20M", "HT", "2T", "48", "30",
	"MKK", "5G", "20M", "HT", "2T", "48", "30",
	"FCC", "5G", "20M", "HT", "2T", "52", "34",
	"ETSI", "5G", "20M", "HT", "2T", "52", "30",
	"MKK", "5G", "20M", "HT", "2T", "52", "30",
	"FCC", "5G", "20M", "HT", "2T", "56", "32",
	"ETSI", "5G", "20M", "HT", "2T", "56", "30",
	"MKK", "5G", "20M", "HT", "2T", "56", "30",
	"FCC", "5G", "20M", "HT", "2T", "60", "30",
	"ETSI", "5G", "20M", "HT", "2T", "60", "30",
	"MKK", "5G", "20M", "HT", "2T", "60", "30",
	"FCC", "5G", "20M", "HT", "2T", "64", "26",
	"ETSI", "5G", "20M", "HT", "2T", "64", "30",
	"MKK", "5G", "20M", "HT", "2T", "64", "30",
	"FCC", "5G", "20M", "HT", "2T", "100", "28",
	"ETSI", "5G", "20M", "HT", "2T", "100", "30",
	"MKK", "5G", "20M", "HT", "2T", "100", "30",
	"FCC", "5G", "20M", "HT", "2T", "114", "28",
	"ETSI", "5G", "20M", "HT", "2T", "114", "30",
	"MKK", "5G", "20M", "HT", "2T", "114", "30",
	"FCC", "5G", "20M", "HT", "2T", "108", "30",
	"ETSI", "5G", "20M", "HT", "2T", "108", "30",
	"MKK", "5G", "20M", "HT", "2T", "108", "30",
	"FCC", "5G", "20M", "HT", "2T", "112", "32",
	"ETSI", "5G", "20M", "HT", "2T", "112", "30",
	"MKK", "5G", "20M", "HT", "2T", "112", "30",
	"FCC", "5G", "20M", "HT", "2T", "116", "32",
	"ETSI", "5G", "20M", "HT", "2T", "116", "30",
	"MKK", "5G", "20M", "HT", "2T", "116", "30",
	"FCC", "5G", "20M", "HT", "2T", "120", "34",
	"ETSI", "5G", "20M", "HT", "2T", "120", "30",
	"MKK", "5G", "20M", "HT", "2T", "120", "30",
	"FCC", "5G", "20M", "HT", "2T", "124", "32",
	"ETSI", "5G", "20M", "HT", "2T", "124", "30",
	"MKK", "5G", "20M", "HT", "2T", "124", "30",
	"FCC", "5G", "20M", "HT", "2T", "128", "30",
	"ETSI", "5G", "20M", "HT", "2T", "128", "30",
	"MKK", "5G", "20M", "HT", "2T", "128", "30",
	"FCC", "5G", "20M", "HT", "2T", "132", "28",
	"ETSI", "5G", "20M", "HT", "2T", "132", "30",
	"MKK", "5G", "20M", "HT", "2T", "132", "30",
	"FCC", "5G", "20M", "HT", "2T", "136", "28",
	"ETSI", "5G", "20M", "HT", "2T", "136", "30",
	"MKK", "5G", "20M", "HT", "2T", "136", "30",
	"FCC", "5G", "20M", "HT", "2T", "140", "26",
	"ETSI", "5G", "20M", "HT", "2T", "140", "30",
	"MKK", "5G", "20M", "HT", "2T", "140", "30",
	"FCC", "5G", "20M", "HT", "2T", "149", "34",
	"ETSI", "5G", "20M", "HT", "2T", "149", "30",
	"MKK", "5G", "20M", "HT", "2T", "149", "63",
	"FCC", "5G", "20M", "HT", "2T", "153", "34",
	"ETSI", "5G", "20M", "HT", "2T", "153", "30",
	"MKK", "5G", "20M", "HT", "2T", "153", "63",
	"FCC", "5G", "20M", "HT", "2T", "157", "34",
	"ETSI", "5G", "20M", "HT", "2T", "157", "30",
	"MKK", "5G", "20M", "HT", "2T", "157", "63",
	"FCC", "5G", "20M", "HT", "2T", "161", "34",
	"ETSI", "5G", "20M", "HT", "2T", "161", "30",
	"MKK", "5G", "20M", "HT", "2T", "161", "63",
	"FCC", "5G", "20M", "HT", "2T", "165", "34",
	"ETSI", "5G", "20M", "HT", "2T", "165", "30",
	"MKK", "5G", "20M", "HT", "2T", "165", "63",
	"FCC", "5G", "40M", "HT", "1T", "38", "30",
	"ETSI", "5G", "40M", "HT", "1T", "38", "32",
	"MKK", "5G", "40M", "HT", "1T", "38", "32",
	"FCC", "5G", "40M", "HT", "1T", "46", "30",
	"ETSI", "5G", "40M", "HT", "1T", "46", "32",
	"MKK", "5G", "40M", "HT", "1T", "46", "32",
	"FCC", "5G", "40M", "HT", "1T", "54", "32",
	"ETSI", "5G", "40M", "HT", "1T", "54", "32",
	"MKK", "5G", "40M", "HT", "1T", "54", "32",
	"FCC", "5G", "40M", "HT", "1T", "62", "32",
	"ETSI", "5G", "40M", "HT", "1T", "62", "32",
	"MKK", "5G", "40M", "HT", "1T", "62", "32",
	"FCC", "5G", "40M", "HT", "1T", "102", "28",
	"ETSI", "5G", "40M", "HT", "1T", "102", "32",
	"MKK", "5G", "40M", "HT", "1T", "102", "32",
	"FCC", "5G", "40M", "HT", "1T", "110", "32",
	"ETSI", "5G", "40M", "HT", "1T", "110", "32",
	"MKK", "5G", "40M", "HT", "1T", "110", "32",
	"FCC", "5G", "40M", "HT", "1T", "118", "34",
	"ETSI", "5G", "40M", "HT", "1T", "118", "32",
	"MKK", "5G", "40M", "HT", "1T", "118", "32",
	"FCC", "5G", "40M", "HT", "1T", "126", "34",
	"ETSI", "5G", "40M", "HT", "1T", "126", "32",
	"MKK", "5G", "40M", "HT", "1T", "126", "32",
	"FCC", "5G", "40M", "HT", "1T", "134", "32",
	"ETSI", "5G", "40M", "HT", "1T", "134", "32",
	"MKK", "5G", "40M", "HT", "1T", "134", "32",
	"FCC", "5G", "40M", "HT", "1T", "151", "34",
	"ETSI", "5G", "40M", "HT", "1T", "151", "32",
	"MKK", "5G", "40M", "HT", "1T", "151", "63",
	"FCC", "5G", "40M", "HT", "1T", "159", "34",
	"ETSI", "5G", "40M", "HT", "1T", "159", "32",
	"MKK", "5G", "40M", "HT", "1T", "159", "63",
	"FCC", "5G", "40M", "HT", "2T", "38", "28",
	"ETSI", "5G", "40M", "HT", "2T", "38", "30",
	"MKK", "5G", "40M", "HT", "2T", "38", "30",
	"FCC", "5G", "40M", "HT", "2T", "46", "28",
	"ETSI", "5G", "40M", "HT", "2T", "46", "30",
	"MKK", "5G", "40M", "HT", "2T", "46", "30",
	"FCC", "5G", "40M", "HT", "2T", "54", "30",
	"ETSI", "5G", "40M", "HT", "2T", "54", "30",
	"MKK", "5G", "40M", "HT", "2T", "54", "30",
	"FCC", "5G", "40M", "HT", "2T", "62", "30",
	"ETSI", "5G", "40M", "HT", "2T", "62", "30",
	"MKK", "5G", "40M", "HT", "2T", "62", "30",
	"FCC", "5G", "40M", "HT", "2T", "102", "26",
	"ETSI", "5G", "40M", "HT", "2T", "102", "30",
	"MKK", "5G", "40M", "HT", "2T", "102", "30",
	"FCC", "5G", "40M", "HT", "2T", "110", "30",
	"ETSI", "5G", "40M", "HT", "2T", "110", "30",
	"MKK", "5G", "40M", "HT", "2T", "110", "30",
	"FCC", "5G", "40M", "HT", "2T", "118", "34",
	"ETSI", "5G", "40M", "HT", "2T", "118", "30",
	"MKK", "5G", "40M", "HT", "2T", "118", "30",
	"FCC", "5G", "40M", "HT", "2T", "126", "32",
	"ETSI", "5G", "40M", "HT", "2T", "126", "30",
	"MKK", "5G", "40M", "HT", "2T", "126", "30",
	"FCC", "5G", "40M", "HT", "2T", "134", "30",
	"ETSI", "5G", "40M", "HT", "2T", "134", "30",
	"MKK", "5G", "40M", "HT", "2T", "134", "30",
	"FCC", "5G", "40M", "HT", "2T", "151", "34",
	"ETSI", "5G", "40M", "HT", "2T", "151", "30",
	"MKK", "5G", "40M", "HT", "2T", "151", "63",
	"FCC", "5G", "40M", "HT", "2T", "159", "34",
	"ETSI", "5G", "40M", "HT", "2T", "159", "30",
	"MKK", "5G", "40M", "HT", "2T", "159", "63",
	"FCC", "5G", "80M", "VHT", "1T", "42", "30",
	"ETSI", "5G", "80M", "VHT", "1T", "42", "32",
	"MKK", "5G", "80M", "VHT", "1T", "42", "32",
	"FCC", "5G", "80M", "VHT", "1T", "58", "28",
	"ETSI", "5G", "80M", "VHT", "1T", "58", "32",
	"MKK", "5G", "80M", "VHT", "1T", "58", "32",
	"FCC", "5G", "80M", "VHT", "1T", "106", "30",
	"ETSI", "5G", "80M", "VHT", "1T", "106", "32",
	"MKK", "5G", "80M", "VHT", "1T", "106", "32",
	"FCC", "5G", "80M", "VHT", "1T", "122", "34",
	"ETSI", "5G", "80M", "VHT", "1T", "122", "32",
	"MKK", "5G", "80M", "VHT", "1T", "122", "32",
	"FCC", "5G", "80M", "VHT", "1T", "155", "34",
	"ETSI", "5G", "80M", "VHT", "1T", "155", "32",
	"MKK", "5G", "80M", "VHT", "1T", "155", "63",
	"FCC", "5G", "80M", "VHT", "2T", "42", "28",
	"ETSI", "5G", "80M", "VHT", "2T", "42", "30",
	"MKK", "5G", "80M", "VHT", "2T", "42", "30",
	"FCC", "5G", "80M", "VHT", "2T", "58", "26",
	"ETSI", "5G", "80M", "VHT", "2T", "58", "30",
	"MKK", "5G", "80M", "VHT", "2T", "58", "30",
	"FCC", "5G", "80M", "VHT", "2T", "106", "28",
	"ETSI", "5G", "80M", "VHT", "2T", "106", "30",
	"MKK", "5G", "80M", "VHT", "2T", "106", "30",
	"FCC", "5G", "80M", "VHT", "2T", "122", "32",
	"ETSI", "5G", "80M", "VHT", "2T", "122", "30",
	"MKK", "5G", "80M", "VHT", "2T", "122", "30",
	"FCC", "5G", "80M", "VHT", "2T", "155", "34",
	"ETSI", "5G", "80M", "VHT", "2T", "155", "30",
	"MKK", "5G", "80M", "VHT", "2T", "155", "63"
};

void
odm_read_and_config_mp_8188f_txpwr_lmt(struct dm_struct *dm)
{
	u32	i = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	u32	array_len = sizeof(array_mp_8188f_txpwr_lmt) / sizeof(u8);
	u8	*array = (u8 *)array_mp_8188f_txpwr_lmt;
#else
	u32	array_len = sizeof(array_mp_8188f_txpwr_lmt) / sizeof(u8 *);
	u8	**array = (u8 **)array_mp_8188f_txpwr_lmt;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	void	*adapter = dm->adapter;
	HAL_DATA_TYPE	*hal_data = GET_HAL_DATA(((PADAPTER)adapter));

	PlatformZeroMemory(hal_data->BufOfLinesPwrLmt, MAX_LINES_HWCONFIG_TXT * MAX_BYTES_LINE_HWCONFIG_TXT);
	hal_data->nLinesReadPwrLmt = array_len / 7;
#endif

	PHYDM_DBG(dm, ODM_COMP_INIT, "===> %s\n", __func__);

	for (i = 0; i < array_len; i += 7) {
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
		u8	regulation = array[i];
		u8	band = array[i + 1];
		u8	bandwidth = array[i + 2];
		u8	rate = array[i + 3];
		u8	rf_path = array[i + 4];
		u8	chnl = array[i + 5];
		u8	val = array[i + 6];
#else
		u8	*regulation = array[i];
		u8	*band = array[i + 1];
		u8	*bandwidth = array[i + 2];
		u8	*rate = array[i + 3];
		u8	*rf_path = array[i + 4];
		u8	*chnl = array[i + 5];
		u8	*val = array[i + 6];
#endif

		odm_config_bb_txpwr_lmt_8188f(dm, regulation, band, bandwidth, rate, rf_path, chnl, val);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		rsprintf((char *)hal_data->BufOfLinesPwrLmt[i / 7], 100, "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\",",
		regulation, band, bandwidth, rate, rf_path, chnl, val);
#endif
	}
}

#endif /* end of HWIMG_SUPPORT*/

