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
******************************************************************************/

/*Image2HeaderVersion: 3.5.1*/
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8723B_SUPPORT == 1)
static boolean
check_positive(
	struct PHY_DM_STRUCT *p_dm,
	const u32	condition1,
	const u32	condition2,
	const u32	condition3,
	const u32	condition4
)
{
	u8	_board_type = ((p_dm->board_type & BIT(4)) >> 4) << 0 | /* _GLNA*/
			((p_dm->board_type & BIT(3)) >> 3) << 1 | /* _GPA*/
			((p_dm->board_type & BIT(7)) >> 7) << 2 | /* _ALNA*/
			((p_dm->board_type & BIT(6)) >> 6) << 3 | /* _APA */
			((p_dm->board_type & BIT(2)) >> 2) << 4 | /* _BT*/
			((p_dm->board_type & BIT(1)) >> 1) << 5 | /* _NGFF*/
			((p_dm->board_type & BIT(5)) >> 5) << 6;  /* _TRSWT*/

	u32	cond1 = condition1, cond2 = condition2, cond3 = condition3, cond4 = condition4;

	u8	cut_version_for_para = (p_dm->cut_version ==  ODM_CUT_A) ? 15 : p_dm->cut_version;
	u8	pkg_type_for_para = (p_dm->package_type == 0) ? 15 : p_dm->package_type;

	u32	driver1 = cut_version_for_para << 24 |
			(p_dm->support_interface & 0xF0) << 16 |
			p_dm->support_platform << 16 |
			pkg_type_for_para << 12 |
			(p_dm->support_interface & 0x0F) << 8  |
			_board_type;

	u32	driver2 = (p_dm->type_glna & 0xFF) <<  0 |
			(p_dm->type_gpa & 0xFF)  <<  8 |
			(p_dm->type_alna & 0xFF) << 16 |
			(p_dm->type_apa & 0xFF)  << 24;

	u32	driver3 = 0;

	u32	driver4 = (p_dm->type_glna & 0xFF00) >>  8 |
			(p_dm->type_gpa & 0xFF00) |
			(p_dm->type_alna & 0xFF00) << 8 |
			(p_dm->type_apa & 0xFF00)  << 16;

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_TRACE,
	("===> check_positive (cond1, cond2, cond3, cond4) = (0x%X 0x%X 0x%X 0x%X)\n", cond1, cond2, cond3, cond4));
	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_TRACE,
	("===> check_positive (driver1, driver2, driver3, driver4) = (0x%X 0x%X 0x%X 0x%X)\n", driver1, driver2, driver3, driver4));

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_TRACE,
	("	(Platform, Interface) = (0x%X, 0x%X)\n", p_dm->support_platform, p_dm->support_interface));
	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_TRACE,
	("	(Board, Package) = (0x%X, 0x%X)\n", p_dm->board_type, p_dm->package_type));


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
static boolean
check_negative(
	struct PHY_DM_STRUCT *p_dm,
	const u32	condition1,
	const u32	condition2
)
{
	return true;
}

/******************************************************************************
*                           radioa.TXT
******************************************************************************/

u32 array_mp_8723b_radioa[] = {
		0x000, 0x00010000,
		0x0B0, 0x000DFFE0,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x0B1, 0x00000018,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x0B2, 0x00084C00,
		0x0B5, 0x0000D2CC,
		0x0B6, 0x000925AA,
		0x0B7, 0x00000010,
		0x0B8, 0x0000907F,
		0x05C, 0x00000002,
		0x07C, 0x00000002,
		0x07E, 0x00000005,
		0x08B, 0x0006FC00,
		0x0B0, 0x000FF9F0,
		0x01C, 0x000739D2,
		0x01E, 0x00000000,
		0x0DF, 0x00000780,
		0x050, 0x00067435,
	0x80002000,	0x00000000,	0x40000000,	0x00000000,
		0x051, 0x0006F10E,
		0x052, 0x000007D3,
	0x90003000,	0x00000000,	0x40000000,	0x00000000,
		0x051, 0x0006F10E,
		0x052, 0x000007D3,
	0x90004000,	0x00000000,	0x40000000,	0x00000000,
		0x051, 0x0006F10E,
		0x052, 0x000007D3,
	0xA0000000,	0x00000000,
		0x051, 0x0006B04E,
		0x052, 0x000007D2,
	0xB0000000,	0x00000000,
		0x053, 0x00000000,
		0x054, 0x00050400,
		0x055, 0x0004026E,
		0x0DD, 0x0000004C,
		0x070, 0x00067435,
	0x80002000,	0x00000000,	0x40000000,	0x00000000,
		0x071, 0x0006F10E,
		0x072, 0x000007D3,
	0x90003000,	0x00000000,	0x40000000,	0x00000000,
		0x071, 0x0006F10E,
		0x072, 0x000007D3,
	0x90004000,	0x00000000,	0x40000000,	0x00000000,
		0x071, 0x0006F10E,
		0x072, 0x000007D3,
	0xA0000000,	0x00000000,
		0x071, 0x0006B04E,
		0x072, 0x000007D2,
	0xB0000000,	0x00000000,
		0x073, 0x00000000,
		0x074, 0x00050400,
		0x075, 0x0004026E,
		0x0EF, 0x00000100,
		0x034, 0x0000ADD7,
		0x035, 0x00005C00,
		0x034, 0x00009DD4,
		0x035, 0x00005000,
		0x034, 0x00008DD1,
		0x035, 0x00004400,
		0x034, 0x00007DCE,
		0x035, 0x00003800,
		0x034, 0x00006CD1,
		0x035, 0x00004400,
		0x034, 0x00005CCE,
		0x035, 0x00003800,
		0x034, 0x000048CE,
		0x035, 0x00004400,
		0x034, 0x000034CE,
		0x035, 0x00003800,
		0x034, 0x00002451,
		0x035, 0x00004400,
		0x034, 0x0000144E,
		0x035, 0x00003800,
		0x034, 0x00000051,
		0x035, 0x00004400,
		0x0EF, 0x00000000,
		0x0EF, 0x00000100,
		0x0ED, 0x00000010,
		0x044, 0x0000ADD7,
		0x044, 0x00009DD4,
		0x044, 0x00008DD1,
		0x044, 0x00007DCE,
		0x044, 0x00006CC1,
		0x044, 0x00005CCE,
		0x044, 0x000044D1,
		0x044, 0x000034CE,
		0x044, 0x00002451,
		0x044, 0x0000144E,
		0x044, 0x00000051,
		0x0EF, 0x00000000,
		0x0ED, 0x00000000,
		0x07F, 0x00020080,
		0x0EF, 0x00002000,
		0x03B, 0x000389EF,
		0x03B, 0x000302FE,
		0x03B, 0x00028CE6,
		0x03B, 0x000200BC,
		0x03B, 0x000188A5,
		0x03B, 0x00010FBC,
		0x03B, 0x00008F71,
		0x03B, 0x00000900,
		0x0EF, 0x00000000,
		0x0ED, 0x00000001,
		0x040, 0x000380EF,
		0x040, 0x000302FE,
		0x040, 0x00028CE6,
		0x040, 0x000200BC,
		0x040, 0x000188A5,
		0x040, 0x00010FBC,
		0x040, 0x00008F71,
		0x040, 0x00000900,
		0x0ED, 0x00000000,
		0x082, 0x00080000,
		0x083, 0x00008000,
		0x084, 0x00049F80,
		0x085, 0x00068000,
		0x0A2, 0x00080000,
		0x0A3, 0x00008000,
		0x0A4, 0x00048D80,
		0x0A5, 0x00068000,
		0x0ED, 0x00000002,
		0x0EF, 0x00000002,
		0x056, 0x00000032,
		0x076, 0x00000032,
		0x01F, 0x00001008,
		0x001, 0x00000780,

};

void
odm_read_and_config_mp_8723b_radioa(
	struct	PHY_DM_STRUCT *p_dm
)
{
	u32	i = 0;
	u8	c_cond;
	boolean	is_matched = true, is_skipped = false;
	u32	array_len = sizeof(array_mp_8723b_radioa)/sizeof(u32);
	u32	*array = array_mp_8723b_radioa;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8723b_radioa\n"));

	while ((i + 1) < array_len) {
		v1 = array[i];
		v2 = array[i + 1];

		if (v1 & (BIT(31) | BIT(30))) {/*positive & negative condition*/
			if (v1 & BIT(31)) {/* positive condition*/
				c_cond  = (u8)((v1 & (BIT(29)|BIT(28))) >> 28);
				if (c_cond == COND_ENDIF) {/*end*/
					is_matched = true;
					is_skipped = false;
					ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ENDIF\n"));
				} else if (c_cond == COND_ELSE) { /*else*/
					is_matched = is_skipped?false:true;
					ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ELSE\n"));
				} else {/*if , else if*/
					pre_v1 = v1;
					pre_v2 = v2;
					ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IF or ELSE IF\n"));
				}
			} else if (v1 & BIT(30)) { /*negative condition*/
				if (is_skipped == false) {
					if (check_positive(p_dm, pre_v1, pre_v2, v1, v2)) {
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
				odm_config_rf_radio_a_8723b(p_dm, v1, v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8723b_radioa(void)
{
		return 29;
}

/******************************************************************************
*                           txpowertrack_ap.TXT
******************************************************************************/

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
#endif

void
odm_read_and_config_mp_8723b_txpowertrack_ap(
	struct PHY_DM_STRUCT	 *p_dm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_mp_8723b\n"));


	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8723b, DELTA_SWINGIDX_SIZE*3);
#endif
}

/******************************************************************************
*                           txpowertrack_pcie.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
#endif

void
odm_read_and_config_mp_8723b_txpowertrack_pcie(
	struct PHY_DM_STRUCT	 *p_dm
)
{
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_mp_8723b\n"));


	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_8723b, DELTA_SWINGIDX_SIZE*3);
#endif
}

/******************************************************************************
*                           txpowertrack_sdio.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8723b[]    = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8723b[]    = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 10, 11, 11, 12, 12, 13, 13, 14, 15, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
#endif

void
odm_read_and_config_mp_8723b_txpowertrack_sdio(
	struct PHY_DM_STRUCT	 *p_dm
)
{
#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_mp_8723b\n"));


	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8723b, DELTA_SWINGIDX_SIZE*3);
#endif
}

/******************************************************************************
*                           txpowertrack_usb.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_USB_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 2, 2, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 16, 17, 17, 18, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 19, 20, 20, 20},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 14, 14, 14, 14, 14},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 6, 7, 7, 8, 8, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 16, 16, 16, 16, 16, 16},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 16, 16, 16, 16, 16},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8723b[][DELTA_SWINGIDX_SIZE] = {
	{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
	{0, 1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12, 13, 14, 15, 15, 16, 17, 18, 18, 19, 20, 21, 21, 21},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 10, 11, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8723b[]    = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8723b[] = {0, 0, 1, 2, 2, 3, 3, 4, 4, 5, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8723b[] = {0, 0, 1, 2, 2, 2, 3, 3, 3, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 9, 10, 10, 11, 11, 12, 12, 13, 14, 15};
#endif

void
odm_read_and_config_mp_8723b_txpowertrack_usb(
	struct PHY_DM_STRUCT	 *p_dm
)
{
#if DEV_BUS_TYPE == RT_USB_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_mp_8723b\n"));


	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE*3);
	odm_move_memory(p_dm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8723b, DELTA_SWINGIDX_SIZE*3);
#endif
}

/******************************************************************************
*                           txpwr_lmt.TXT
******************************************************************************/

const char *array_mp_8723b_txpwr_lmt[] = {
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "30",
	"ETSI", "2.4G", "20M", "CCK", "1T", "11", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "11", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "12", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "12", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "13", "26",
	"MKK", "2.4G", "20M", "CCK", "1T", "13", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "14", "63",
	"MKK", "2.4G", "20M", "CCK", "1T", "14", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "02", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "03", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "04", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "05", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "06", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "07", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "08", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "09", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "10", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "10", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "10", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "11", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "11", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "11", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "32",
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "13", "28",
	"MKK", "2.4G", "20M", "OFDM", "1T", "13", "28",
	"FCC", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"MKK", "2.4G", "20M", "OFDM", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "1T", "01", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "01", "26",
	"MKK", "2.4G", "20M", "HT", "1T", "01", "28",
	"FCC", "2.4G", "20M", "HT", "1T", "02", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "02", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "03", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "04", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "05", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "06", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "07", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "08", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "HT", "1T", "09", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "10", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "11", "26",
	"ETSI", "2.4G", "20M", "HT", "1T", "11", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "32",
	"MKK", "2.4G", "20M", "HT", "1T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "13", "26",
	"MKK", "2.4G", "20M", "HT", "1T", "13", "28",
	"FCC", "2.4G", "20M", "HT", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "14", "63",
	"MKK", "2.4G", "20M", "HT", "1T", "14", "63",
	"FCC", "2.4G", "20M", "HT", "2T", "01", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "01", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "01", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "02", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "02", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "02", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "03", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "03", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "03", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "04", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "04", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "04", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "05", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "05", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "05", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "06", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "06", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "06", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "07", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "07", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "07", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "08", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "08", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "08", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "09", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "09", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "09", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "10", "32",
	"ETSI", "2.4G", "20M", "HT", "2T", "10", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "10", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "11", "30",
	"ETSI", "2.4G", "20M", "HT", "2T", "11", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "11", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "12", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "20M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "20M", "HT", "2T", "13", "32",
	"MKK", "2.4G", "20M", "HT", "2T", "13", "32",
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
	"ETSI", "2.4G", "40M", "HT", "1T", "04", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "04", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "05", "28",
	"ETSI", "2.4G", "40M", "HT", "1T", "05", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "05", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "06", "28",
	"ETSI", "2.4G", "40M", "HT", "1T", "06", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "06", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "07", "28",
	"ETSI", "2.4G", "40M", "HT", "1T", "07", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "07", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "08", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "08", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "08", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "09", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "09", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "09", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "28",
	"MKK", "2.4G", "40M", "HT", "1T", "10", "26",
	"FCC", "2.4G", "40M", "HT", "1T", "11", "26",
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
	"FCC", "2.4G", "40M", "HT", "2T", "03", "30",
	"ETSI", "2.4G", "40M", "HT", "2T", "03", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "03", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "04", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "04", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "04", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "05", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "05", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "05", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "06", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "06", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "06", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "07", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "07", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "07", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "08", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "08", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "08", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "09", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "09", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "09", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "10", "32",
	"ETSI", "2.4G", "40M", "HT", "2T", "10", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "10", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "11", "30",
	"ETSI", "2.4G", "40M", "HT", "2T", "11", "30",
	"MKK", "2.4G", "40M", "HT", "2T", "11", "30",
	"FCC", "2.4G", "40M", "HT", "2T", "12", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "12", "32",
	"MKK", "2.4G", "40M", "HT", "2T", "12", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "13", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "13", "32",
	"MKK", "2.4G", "40M", "HT", "2T", "13", "32",
	"FCC", "2.4G", "40M", "HT", "2T", "14", "63",
	"ETSI", "2.4G", "40M", "HT", "2T", "14", "63",
	"MKK", "2.4G", "40M", "HT", "2T", "14", "63"
};

void
odm_read_and_config_mp_8723b_txpwr_lmt(
	struct PHY_DM_STRUCT	*p_dm
)
{
	u32	i = 0;
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	u32	array_len = sizeof(array_mp_8723b_txpwr_lmt)/sizeof(u8);
	u8	*array = (u8 *)array_mp_8723b_txpwr_lmt;
#else
	u32	array_len = sizeof(array_mp_8723b_txpwr_lmt)/sizeof(u8 *);
	u8	**array = (u8 **)array_mp_8723b_txpwr_lmt;
#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	PlatformZeroMemory(p_hal_data->BufOfLinesPwrLmt, MAX_LINES_HWCONFIG_TXT*MAX_BYTES_LINE_HWCONFIG_TXT);
	p_hal_data->nLinesReadPwrLmt = array_len/7;
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8723b_txpwr_lmt\n"));

	for (i = 0; i < array_len; i += 7) {
#if (DM_ODM_SUPPORT_TYPE == ODM_IOT)
		u8	regulation = array[i];
		u8	band = array[i+1];
		u8	bandwidth = array[i+2];
		u8	rate = array[i+3];
		u8	rf_path = array[i+4];
		u8	chnl = array[i+5];
		u8	val = array[i+6];
#else
		u8	*regulation = array[i];
		u8	*band = array[i+1];
		u8	*bandwidth = array[i+2];
		u8	*rate = array[i+3];
		u8	*rf_path = array[i+4];
		u8	*chnl = array[i+5];
		u8	*val = array[i+6];
#endif

		odm_config_bb_txpwr_lmt_8723b(p_dm, regulation, band, bandwidth, rate, rf_path, chnl, val);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		rsprintf((char *)p_hal_data->BufOfLinesPwrLmt[i/7], 100, "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\",",
		regulation, band, bandwidth, rate, rf_path, chnl, val);
#endif
	}

}

#endif /* end of HWIMG_SUPPORT*/

