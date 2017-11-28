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

/*Image2HeaderVersion: 2.18*/
#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8188E_SUPPORT == 1)
static bool
check_positive(
	struct PHY_DM_STRUCT     *p_dm_odm,
	const u32  condition1,
	const u32  condition2,
	const u32  condition3,
	const u32  condition4
)
{
	u8    _board_type = ((p_dm_odm->board_type & BIT(4)) >> 4) << 0 | /* _GLNA*/
		    ((p_dm_odm->board_type & BIT(3)) >> 3) << 1 | /* _GPA*/
		    ((p_dm_odm->board_type & BIT(7)) >> 7) << 2 | /* _ALNA*/
		    ((p_dm_odm->board_type & BIT(6)) >> 6) << 3 | /* _APA */
		    ((p_dm_odm->board_type & BIT(2)) >> 2) << 4;  /* _BT*/

	u32	cond1   = condition1, cond2 = condition2, cond3 = condition3, cond4 = condition4;
	u32    driver1 = p_dm_odm->cut_version       << 24 |
			 (p_dm_odm->support_interface & 0xF0) << 16 |
			 p_dm_odm->support_platform  << 16 |
			 p_dm_odm->package_type      << 12 |
			 (p_dm_odm->support_interface & 0x0F) << 8  |
			 _board_type;

	u32    driver2 = (p_dm_odm->type_glna & 0xFF) <<  0 |
			 (p_dm_odm->type_gpa & 0xFF)  <<  8 |
			 (p_dm_odm->type_alna & 0xFF) << 16 |
			 (p_dm_odm->type_apa & 0xFF)  << 24;

	u32    driver3 = 0;

	u32    driver4 = (p_dm_odm->type_glna & 0xFF00) >>  8 |
			 (p_dm_odm->type_gpa & 0xFF00) |
			 (p_dm_odm->type_alna & 0xFF00) << 8 |
			 (p_dm_odm->type_apa & 0xFF00)  << 16;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE,
		("===> check_positive (cond1, cond2, cond3, cond4) = (0x%X 0x%X 0x%X 0x%X)\n", cond1, cond2, cond3, cond4));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE,
		("===> check_positive (driver1, driver2, driver3, driver4) = (0x%X 0x%X 0x%X 0x%X)\n", driver1, driver2, driver3, driver4));

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE,
		("	(Platform, Interface) = (0x%X, 0x%X)\n", p_dm_odm->support_platform, p_dm_odm->support_interface));
	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_TRACE,
		("	(Board, Package) = (0x%X, 0x%X)\n", p_dm_odm->board_type, p_dm_odm->package_type));


	/*============== value Defined Check ===============*/
	/*QFN type [15:12] and cut version [27:24] need to do value check*/

	if (((cond1 & 0x0000F000) != 0) && ((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return false;
	if (((cond1 & 0x0F000000) != 0) && ((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return false;

	/*=============== Bit Defined Check ================*/
	/* We don't care [31:28] */

	cond1   &= 0x00FF0FFF;
	driver1 &= 0x00FF0FFF;

	if ((cond1 & driver1) == cond1) {
		u32 bit_mask = 0;

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
static bool
check_negative(
	struct PHY_DM_STRUCT     *p_dm_odm,
	const u32  condition1,
	const u32  condition2
)
{
	return true;
}

/******************************************************************************
*                           MAC_REG.TXT
******************************************************************************/

u32 array_mp_8188e_mac_reg[] = {
	0x026, 0x00000041,
	0x027, 0x00000035,
	0x80000002,	0x00000000,	0x40000000,	0x00000000,
	0x040, 0x0000000C,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x040, 0x0000000C,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x040, 0x0000000C,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x040, 0x0000000C,
	0xA0000000,	0x00000000,
	0x040, 0x00000000,
	0xB0000000,	0x00000000,
	0x421, 0x0000000F,
	0x428, 0x0000000A,
	0x429, 0x00000010,
	0x430, 0x00000000,
	0x431, 0x00000001,
	0x432, 0x00000002,
	0x433, 0x00000004,
	0x434, 0x00000005,
	0x435, 0x00000006,
	0x436, 0x00000007,
	0x437, 0x00000008,
	0x438, 0x00000000,
	0x439, 0x00000000,
	0x43A, 0x00000001,
	0x43B, 0x00000002,
	0x43C, 0x00000004,
	0x43D, 0x00000005,
	0x43E, 0x00000006,
	0x43F, 0x00000007,
	0x440, 0x0000005D,
	0x441, 0x00000001,
	0x442, 0x00000000,
	0x444, 0x00000015,
	0x445, 0x000000F0,
	0x446, 0x0000000F,
	0x447, 0x00000000,
	0x458, 0x00000041,
	0x459, 0x000000A8,
	0x45A, 0x00000072,
	0x45B, 0x000000B9,
	0x460, 0x00000066,
	0x461, 0x00000066,
	0x480, 0x00000008,
	0x4C8, 0x000000FF,
	0x4C9, 0x00000008,
	0x4CC, 0x000000FF,
	0x4CD, 0x000000FF,
	0x4CE, 0x00000001,
	0x4D3, 0x00000001,
	0x500, 0x00000026,
	0x501, 0x000000A2,
	0x502, 0x0000002F,
	0x503, 0x00000000,
	0x504, 0x00000028,
	0x505, 0x000000A3,
	0x506, 0x0000005E,
	0x507, 0x00000000,
	0x508, 0x0000002B,
	0x509, 0x000000A4,
	0x50A, 0x0000005E,
	0x50B, 0x00000000,
	0x50C, 0x0000004F,
	0x50D, 0x000000A4,
	0x50E, 0x00000000,
	0x50F, 0x00000000,
	0x512, 0x0000001C,
	0x514, 0x0000000A,
	0x516, 0x0000000A,
	0x525, 0x0000004F,
	0x550, 0x00000010,
	0x551, 0x00000010,
	0x559, 0x00000002,
	0x55D, 0x000000FF,
	0x605, 0x00000030,
	0x608, 0x0000000E,
	0x609, 0x0000002A,
	0x620, 0x000000FF,
	0x621, 0x000000FF,
	0x622, 0x000000FF,
	0x623, 0x000000FF,
	0x624, 0x000000FF,
	0x625, 0x000000FF,
	0x626, 0x000000FF,
	0x627, 0x000000FF,
	0x63C, 0x00000008,
	0x63D, 0x00000008,
	0x63E, 0x0000000C,
	0x63F, 0x0000000C,
	0x640, 0x00000040,
	0x652, 0x00000020,
	0x66E, 0x00000005,
	0x700, 0x00000021,
	0x701, 0x00000043,
	0x702, 0x00000065,
	0x703, 0x00000087,
	0x708, 0x00000021,
	0x709, 0x00000043,
	0x70A, 0x00000065,
	0x70B, 0x00000087,

};

void
odm_read_and_config_mp_8188e_mac_reg(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	u32     i         = 0;
	u8     c_cond;
	bool is_matched = true, is_skipped = false;
	u32     array_len    = sizeof(array_mp_8188e_mac_reg) / sizeof(u32);
	u32    *array       = array_mp_8188e_mac_reg;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8188e_mac_reg\n"));

	while ((i + 1) < array_len) {
		v1 = array[i];
		v2 = array[i + 1];

		if (v1 & (BIT(31) | BIT30)) {/*positive & negative condition*/
			if (v1 & BIT(31)) {/* positive condition*/
				c_cond  = (u8)((v1 & (BIT(29) | BIT(28))) >> 28);
				if (c_cond == COND_ENDIF) {/*end*/
					is_matched = true;
					is_skipped = false;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ENDIF\n"));
				} else if (c_cond == COND_ELSE) { /*else*/
					is_matched = is_skipped ? false : true;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("ELSE\n"));
				} else {/*if , else if*/
					pre_v1 = v1;
					pre_v2 = v2;
					ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("IF or ELSE IF\n"));
				}
			} else if (v1 & BIT(30)) { /*negative condition*/
				if (is_skipped == false) {
					if (check_positive(p_dm_odm, pre_v1, pre_v2, v1, v2)) {
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
				odm_config_mac_8188e(p_dm_odm, v1, (u8)v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8188e_mac_reg(void)
{
	return 70;
}

#endif /* end of HWIMG_SUPPORT*/
