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
*                           agc_tab.TXT
******************************************************************************/

u32 array_mp_8723b_agc_tab[] = {
		0xC78, 0xFD000001,
		0xC78, 0xFC010001,
		0xC78, 0xFB020001,
		0xC78, 0xFA030001,
		0xC78, 0xF9040001,
		0xC78, 0xF8050001,
		0xC78, 0xF7060001,
		0xC78, 0xF6070001,
		0xC78, 0xF5080001,
		0xC78, 0xF4090001,
		0xC78, 0xF30A0001,
		0xC78, 0xF20B0001,
		0xC78, 0xF10C0001,
		0xC78, 0xF00D0001,
		0xC78, 0xEF0E0001,
		0xC78, 0xEE0F0001,
		0xC78, 0xED100001,
		0xC78, 0xEC110001,
		0xC78, 0xEB120001,
		0xC78, 0xEA130001,
		0xC78, 0xE9140001,
		0xC78, 0xE8150001,
		0xC78, 0xE7160001,
		0xC78, 0xE6170001,
		0xC78, 0xE5180001,
		0xC78, 0xE4190001,
		0xC78, 0xE31A0001,
		0xC78, 0xA51B0001,
		0xC78, 0xA41C0001,
		0xC78, 0xA31D0001,
		0xC78, 0x671E0001,
		0xC78, 0x661F0001,
		0xC78, 0x65200001,
		0xC78, 0x64210001,
		0xC78, 0x63220001,
		0xC78, 0x4A230001,
		0xC78, 0x49240001,
		0xC78, 0x48250001,
		0xC78, 0x47260001,
		0xC78, 0x46270001,
		0xC78, 0x45280001,
		0xC78, 0x44290001,
		0xC78, 0x432A0001,
		0xC78, 0x422B0001,
		0xC78, 0x292C0001,
		0xC78, 0x282D0001,
		0xC78, 0x272E0001,
		0xC78, 0x262F0001,
		0xC78, 0x0A300001,
		0xC78, 0x09310001,
		0xC78, 0x08320001,
		0xC78, 0x07330001,
		0xC78, 0x06340001,
		0xC78, 0x05350001,
		0xC78, 0x04360001,
		0xC78, 0x03370001,
		0xC78, 0x02380001,
		0xC78, 0x01390001,
		0xC78, 0x013A0001,
		0xC78, 0x013B0001,
		0xC78, 0x013C0001,
		0xC78, 0x013D0001,
		0xC78, 0x013E0001,
		0xC78, 0x013F0001,
		0xC78, 0xFC400001,
		0xC78, 0xFB410001,
		0xC78, 0xFA420001,
		0xC78, 0xF9430001,
		0xC78, 0xF8440001,
		0xC78, 0xF7450001,
		0xC78, 0xF6460001,
		0xC78, 0xF5470001,
		0xC78, 0xF4480001,
		0xC78, 0xF3490001,
		0xC78, 0xF24A0001,
		0xC78, 0xF14B0001,
		0xC78, 0xF04C0001,
		0xC78, 0xEF4D0001,
		0xC78, 0xEE4E0001,
		0xC78, 0xED4F0001,
		0xC78, 0xEC500001,
		0xC78, 0xEB510001,
		0xC78, 0xEA520001,
		0xC78, 0xE9530001,
		0xC78, 0xE8540001,
		0xC78, 0xE7550001,
		0xC78, 0xE6560001,
		0xC78, 0xE5570001,
		0xC78, 0xE4580001,
		0xC78, 0xE3590001,
		0xC78, 0xA65A0001,
		0xC78, 0xA55B0001,
		0xC78, 0xA45C0001,
		0xC78, 0xA35D0001,
		0xC78, 0x675E0001,
		0xC78, 0x665F0001,
		0xC78, 0x65600001,
		0xC78, 0x64610001,
		0xC78, 0x63620001,
		0xC78, 0x62630001,
		0xC78, 0x61640001,
		0xC78, 0x48650001,
		0xC78, 0x47660001,
		0xC78, 0x46670001,
		0xC78, 0x45680001,
		0xC78, 0x44690001,
		0xC78, 0x436A0001,
		0xC78, 0x426B0001,
		0xC78, 0x286C0001,
		0xC78, 0x276D0001,
		0xC78, 0x266E0001,
		0xC78, 0x256F0001,
		0xC78, 0x24700001,
		0xC78, 0x09710001,
		0xC78, 0x08720001,
		0xC78, 0x07730001,
		0xC78, 0x06740001,
		0xC78, 0x05750001,
		0xC78, 0x04760001,
		0xC78, 0x03770001,
		0xC78, 0x02780001,
		0xC78, 0x01790001,
		0xC78, 0x017A0001,
		0xC78, 0x017B0001,
		0xC78, 0x017C0001,
		0xC78, 0x017D0001,
		0xC78, 0x017E0001,
		0xC78, 0x017F0001,
		0xC50, 0x69553422,
		0xC50, 0x69553420,
		0x824, 0x00390204,

};

void
odm_read_and_config_mp_8723b_agc_tab(
	struct	PHY_DM_STRUCT *p_dm
)
{
	u32	i = 0;
	u8	c_cond;
	boolean	is_matched = true, is_skipped = false;
	u32	array_len = sizeof(array_mp_8723b_agc_tab)/sizeof(u32);
	u32	*array = array_mp_8723b_agc_tab;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8723b_agc_tab\n"));

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
				odm_config_bb_agc_8723b(p_dm, v1, MASKDWORD, v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8723b_agc_tab(void)
{
		return 29;
}

/******************************************************************************
*                           phy_reg.TXT
******************************************************************************/

u32 array_mp_8723b_phy_reg[] = {
		0x800, 0x80040000,
		0x804, 0x00000003,
		0x808, 0x0000FC00,
		0x80C, 0x0000000A,
		0x810, 0x10001331,
		0x814, 0x020C3D10,
		0x818, 0x02200385,
		0x81C, 0x00000000,
		0x820, 0x01000100,
		0x824, 0x00190204,
		0x828, 0x00000000,
		0x82C, 0x00000000,
		0x830, 0x00000000,
		0x834, 0x00000000,
		0x838, 0x00000000,
		0x83C, 0x00000000,
		0x840, 0x00010000,
		0x844, 0x00000000,
		0x848, 0x00000000,
		0x84C, 0x00000000,
		0x850, 0x00000000,
		0x854, 0x00000000,
		0x858, 0x569A11A9,
		0x85C, 0x01000014,
		0x860, 0x66F60110,
		0x864, 0x061F0649,
		0x868, 0x00000000,
		0x86C, 0x27272700,
		0x870, 0x07000760,
		0x874, 0x25004000,
		0x878, 0x00000808,
		0x87C, 0x00000000,
		0x880, 0xB0000C1C,
		0x884, 0x00000001,
		0x888, 0x00000000,
		0x88C, 0xCCC000C0,
		0x890, 0x00000800,
		0x894, 0xFFFFFFFE,
		0x898, 0x40302010,
		0x89C, 0x00706050,
		0x900, 0x00000000,
		0x904, 0x00000023,
		0x908, 0x00000000,
		0x90C, 0x81121111,
		0x910, 0x00000002,
		0x914, 0x00000201,
		0xA00, 0x00D047C8,
		0xA04, 0x80FF800C,
		0xA08, 0x8C838300,
		0xA0C, 0x2E7F120F,
		0xA10, 0x9500BB78,
		0xA14, 0x1114D028,
		0xA18, 0x00881117,
		0xA1C, 0x89140F00,
		0xA20, 0x1A1B0000,
		0xA24, 0x090E1317,
		0xA28, 0x00000204,
		0xA2C, 0x00D30000,
		0xA70, 0x101FBF00,
		0xA74, 0x00000007,
		0xA78, 0x00000900,
		0xA7C, 0x225B0606,
		0xA80, 0x21806490,
		0xB2C, 0x00000000,
		0xC00, 0x48071D40,
		0xC04, 0x03A05611,
		0xC08, 0x000000E4,
		0xC0C, 0x6C6C6C6C,
		0xC10, 0x08800000,
		0xC14, 0x40000100,
		0xC18, 0x08800000,
		0xC1C, 0x40000100,
		0xC20, 0x00000000,
		0xC24, 0x00000000,
		0xC28, 0x00000000,
		0xC2C, 0x00000000,
		0xC30, 0x69E9AC44,
		0xC34, 0x469652AF,
		0xC38, 0x49795994,
		0xC3C, 0x0A97971C,
		0xC40, 0x1F7C403F,
		0xC44, 0x000100B7,
		0xC48, 0xEC020107,
		0xC4C, 0x007F037F,
		0xC50, 0x69553420,
		0xC54, 0x43BC0094,
		0xC58, 0x00013147,
		0xC5C, 0x00250492,
		0xC60, 0x00000000,
		0xC64, 0x5112848B,
		0xC68, 0x47C00BFF,
		0xC6C, 0x00000036,
		0xC70, 0x2C7F000D,
		0xC74, 0x020610DB,
		0xC78, 0x0000001F,
		0xC7C, 0x00B91612,
		0xC80, 0x390000E4,
		0xC84, 0x21F60000,
		0xC88, 0x40000100,
		0xC8C, 0x20200000,
		0xC90, 0x00020E1A,
		0xC94, 0x00000000,
		0xC98, 0x00020E1A,
		0xC9C, 0x00007F7F,
		0xCA0, 0x00000000,
		0xCA4, 0x000300A0,
		0xCA8, 0x00000000,
		0xCAC, 0x00000000,
		0xCB0, 0x00000000,
		0xCB4, 0x00000000,
		0xCB8, 0x00000000,
		0xCBC, 0x28000000,
		0xCC0, 0x00000000,
		0xCC4, 0x00000000,
		0xCC8, 0x00000000,
		0xCCC, 0x00000000,
		0xCD0, 0x00000000,
		0xCD4, 0x00000000,
		0xCD8, 0x64B22427,
		0xCDC, 0x00766932,
		0xCE0, 0x00222222,
		0xCE4, 0x00000000,
		0xCE8, 0x37644302,
		0xCEC, 0x2F97D40C,
		0xD00, 0x00000740,
		0xD04, 0x40020401,
		0xD08, 0x0000907F,
		0xD0C, 0x20010201,
		0xD10, 0xA0633333,
		0xD14, 0x3333BC53,
		0xD18, 0x7A8F5B6F,
		0xD2C, 0xCC979975,
		0xD30, 0x00000000,
		0xD34, 0x80608000,
		0xD38, 0x00000000,
		0xD3C, 0x00127353,
		0xD40, 0x00000000,
		0xD44, 0x00000000,
		0xD48, 0x00000000,
		0xD4C, 0x00000000,
		0xD50, 0x6437140A,
		0xD54, 0x00000000,
		0xD58, 0x00000282,
		0xD5C, 0x30032064,
		0xD60, 0x4653DE68,
		0xD64, 0x04518A3C,
		0xD68, 0x00002101,
		0xD6C, 0x2A201C16,
		0xD70, 0x1812362E,
		0xD74, 0x322C2220,
		0xD78, 0x000E3C24,
		0xE00, 0x2D2D2D2D,
		0xE04, 0x2D2D2D2D,
		0xE08, 0x0390272D,
		0xE10, 0x2D2D2D2D,
		0xE14, 0x2D2D2D2D,
		0xE18, 0x2D2D2D2D,
		0xE1C, 0x2D2D2D2D,
		0xE28, 0x00000000,
		0xE30, 0x1000DC1F,
		0xE34, 0x10008C1F,
		0xE38, 0x02140102,
		0xE3C, 0x681604C2,
		0xE40, 0x01007C00,
		0xE44, 0x01004800,
		0xE48, 0xFB000000,
		0xE4C, 0x000028D1,
		0xE50, 0x1000DC1F,
		0xE54, 0x10008C1F,
		0xE58, 0x02140102,
		0xE5C, 0x28160D05,
		0xE60, 0x00000048,
		0xE68, 0x001B2556,
		0xE6C, 0x00C00096,
		0xE70, 0x00C00096,
		0xE74, 0x01000056,
		0xE78, 0x01000014,
		0xE7C, 0x01000056,
		0xE80, 0x01000014,
		0xE84, 0x00C00096,
		0xE88, 0x01000056,
		0xE8C, 0x00C00096,
		0xED0, 0x00C00096,
		0xED4, 0x00C00096,
		0xED8, 0x00C00096,
		0xEDC, 0x000000D6,
		0xEE0, 0x000000D6,
		0xEEC, 0x01C00016,
		0xF14, 0x00000003,
		0xF4C, 0x00000000,
		0xF00, 0x00000300,
		0x820, 0x01000100,
		0x800, 0x83040000,

};

void
odm_read_and_config_mp_8723b_phy_reg(
	struct	PHY_DM_STRUCT *p_dm
)
{
	u32	i = 0;
	u8	c_cond;
	boolean	is_matched = true, is_skipped = false;
	u32	array_len = sizeof(array_mp_8723b_phy_reg)/sizeof(u32);
	u32	*array = array_mp_8723b_phy_reg;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8723b_phy_reg\n"));

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
				odm_config_bb_phy_8723b(p_dm, v1, MASKDWORD, v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8723b_phy_reg(void)
{
		return 29;
}

/******************************************************************************
*                           phy_reg_pg.TXT
******************************************************************************/

u32 array_mp_8723b_phy_reg_pg[] = {
	0, 0, 0, 0x00000e08, 0x0000ff00, 0x00003800,
	0, 0, 0, 0x0000086c, 0xffffff00, 0x32343600,
	0, 0, 0, 0x00000e00, 0xffffffff, 0x40424444,
	0, 0, 0, 0x00000e04, 0xffffffff, 0x28323638,
	0, 0, 0, 0x00000e10, 0xffffffff, 0x38404244,
	0, 0, 0, 0x00000e14, 0xffffffff, 0x26303436
};

void
odm_read_and_config_mp_8723b_phy_reg_pg(
	struct PHY_DM_STRUCT	*p_dm
)
{
	u32	i = 0;
	u32	array_len = sizeof(array_mp_8723b_phy_reg_pg)/sizeof(u32);
	u32	*array = array_mp_8723b_phy_reg_pg;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER	*adapter = p_dm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	PlatformZeroMemory(p_hal_data->BufOfLinesPwrByRate, MAX_LINES_HWCONFIG_TXT*MAX_BYTES_LINE_HWCONFIG_TXT);
	p_hal_data->nLinesReadPwrByRate = array_len/6;
#endif

	ODM_RT_TRACE(p_dm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8723b_phy_reg_pg\n"));

	p_dm->phy_reg_pg_version = 1;
	p_dm->phy_reg_pg_value_type = PHY_REG_PG_EXACT_VALUE;

	for (i = 0; i < array_len; i += 6) {
		u32	v1 = array[i];
		u32	v2 = array[i+1];
		u32	v3 = array[i+2];
		u32	v4 = array[i+3];
		u32	v5 = array[i+4];
		u32	v6 = array[i+5];

		odm_config_bb_phy_reg_pg_8723b(p_dm, v1, v2, v3, v4, v5, v6);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	rsprintf((char *)p_hal_data->BufOfLinesPwrByRate[i/6], 100, "%s, %s, %s, 0x%X, 0x%08X, 0x%08X,",
		(v1 == 0?"2.4G":"  5G"), (v2 == 0?"A":"B"), (v3 == 0?"1Tx":"2Tx"), v4, v5, v6);
#endif
	}
}



#endif /* end of HWIMG_SUPPORT*/

