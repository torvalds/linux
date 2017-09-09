/******************************************************************************
 *
 * Copyright(c) 2007 - 2016  Realtek Corporation.
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

/*Image2HeaderVersion: 3.2*/
#include "../mp_precomp.h"
#include "../phydm_precomp.h"

static bool check_positive(struct phy_dm_struct *dm, const u32 condition1,
			   const u32 condition2, const u32 condition3,
			   const u32 condition4)
{
	u8 _board_type = ((dm->board_type & BIT(4)) >> 4) << 0 | /* _GLNA*/
			 ((dm->board_type & BIT(3)) >> 3) << 1 | /* _GPA*/
			 ((dm->board_type & BIT(7)) >> 7) << 2 | /* _ALNA*/
			 ((dm->board_type & BIT(6)) >> 6) << 3 | /* _APA */
			 ((dm->board_type & BIT(2)) >> 2) << 4; /* _BT*/

	u32 cond1 = condition1, cond2 = condition2, cond3 = condition3,
	    cond4 = condition4;

	u8 cut_version_for_para =
		(dm->cut_version == ODM_CUT_A) ? 14 : dm->cut_version;
	u8 pkg_type_for_para = (dm->package_type == 0) ? 14 : dm->package_type;

	u32 driver1 = cut_version_for_para << 24 |
		      (dm->support_interface & 0xF0) << 16 |
		      dm->support_platform << 16 | pkg_type_for_para << 12 |
		      (dm->support_interface & 0x0F) << 8 | _board_type;

	u32 driver2 = (dm->type_glna & 0xFF) << 0 | (dm->type_gpa & 0xFF) << 8 |
		      (dm->type_alna & 0xFF) << 16 |
		      (dm->type_apa & 0xFF) << 24;

	u32 driver3 = 0;

	u32 driver4 = (dm->type_glna & 0xFF00) >> 8 | (dm->type_gpa & 0xFF00) |
		      (dm->type_alna & 0xFF00) << 8 |
		      (dm->type_apa & 0xFF00) << 16;

	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"===> %s (cond1, cond2, cond3, cond4) = (0x%X 0x%X 0x%X 0x%X)\n",
		__func__, cond1, cond2, cond3, cond4);
	ODM_RT_TRACE(
		dm, ODM_COMP_INIT,
		"===> %s (driver1, driver2, driver3, driver4) = (0x%X 0x%X 0x%X 0x%X)\n",
		__func__, driver1, driver2, driver3, driver4);

	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "	(Platform, Interface) = (0x%X, 0x%X)\n",
		     dm->support_platform, dm->support_interface);
	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "	(Board, Package) = (0x%X, 0x%X)\n",
		     dm->board_type, dm->package_type);

	/*============== value Defined Check ===============*/
	/*QFN type [15:12] and cut version [27:24] need to do value check*/

	if (((cond1 & 0x0000F000) != 0) &&
	    ((cond1 & 0x0000F000) != (driver1 & 0x0000F000)))
		return false;
	if (((cond1 & 0x0F000000) != 0) &&
	    ((cond1 & 0x0F000000) != (driver1 & 0x0F000000)))
		return false;

	/*=============== Bit Defined Check ================*/
	/* We don't care [31:28] */

	cond1 &= 0x00FF0FFF;
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

		if (((cond2 & bit_mask) == (driver2 & bit_mask)) &&
		    ((cond4 & bit_mask) ==
		     (driver4 &
		      bit_mask))) /* board_type of each RF path is matched*/
			return true;
		else
			return false;
	} else {
		return false;
	}
}

/******************************************************************************
 *                           mac_reg.TXT
 ******************************************************************************/

static u32 array_mp_8822b_mac_reg[] = {
	0x029,  0x000000F9, 0x420,  0x00000080, 0x421,  0x0000000F,
	0x428,  0x0000000A, 0x429,  0x00000010, 0x430,  0x00000000,
	0x431,  0x00000000, 0x432,  0x00000000, 0x433,  0x00000001,
	0x434,  0x00000004, 0x435,  0x00000005, 0x436,  0x00000007,
	0x437,  0x00000008, 0x43C,  0x00000004, 0x43D,  0x00000005,
	0x43E,  0x00000007, 0x43F,  0x00000008, 0x440,  0x0000005D,
	0x441,  0x00000001, 0x442,  0x00000000, 0x444,  0x00000010,
	0x445,  0x000000F0, 0x446,  0x00000001, 0x447,  0x000000FE,
	0x448,  0x00000000, 0x449,  0x00000000, 0x44A,  0x00000000,
	0x44B,  0x00000040, 0x44C,  0x00000010, 0x44D,  0x000000F0,
	0x44E,  0x0000003F, 0x44F,  0x00000000, 0x450,  0x00000000,
	0x451,  0x00000000, 0x452,  0x00000000, 0x453,  0x00000040,
	0x455,  0x00000070, 0x45E,  0x00000004, 0x49C,  0x00000010,
	0x49D,  0x000000F0, 0x49E,  0x00000000, 0x49F,  0x00000006,
	0x4A0,  0x000000E0, 0x4A1,  0x00000003, 0x4A2,  0x00000000,
	0x4A3,  0x00000040, 0x4A4,  0x00000015, 0x4A5,  0x000000F0,
	0x4A6,  0x00000000, 0x4A7,  0x00000006, 0x4A8,  0x000000E0,
	0x4A9,  0x00000000, 0x4AA,  0x00000000, 0x4AB,  0x00000000,
	0x7DA,  0x00000008, 0x1448, 0x00000006, 0x144A, 0x00000006,
	0x144C, 0x00000006, 0x144E, 0x00000006, 0x4C8,  0x000000FF,
	0x4C9,  0x00000008, 0x4CA,  0x00000020, 0x4CB,  0x00000020,
	0x4CC,  0x000000FF, 0x4CD,  0x000000FF, 0x4CE,  0x00000001,
	0x4CF,  0x00000008, 0x500,  0x00000026, 0x501,  0x000000A2,
	0x502,  0x0000002F, 0x503,  0x00000000, 0x504,  0x00000028,
	0x505,  0x000000A3, 0x506,  0x0000005E, 0x507,  0x00000000,
	0x508,  0x0000002B, 0x509,  0x000000A4, 0x50A,  0x0000005E,
	0x50B,  0x00000000, 0x50C,  0x0000004F, 0x50D,  0x000000A4,
	0x50E,  0x00000000, 0x50F,  0x00000000, 0x512,  0x0000001C,
	0x514,  0x0000000A, 0x516,  0x0000000A, 0x521,  0x0000002F,
	0x525,  0x0000004F, 0x551,  0x00000010, 0x559,  0x00000002,
	0x55C,  0x00000050, 0x55D,  0x000000FF, 0x577,  0x0000000B,
	0x5BE,  0x00000064, 0x605,  0x00000030, 0x608,  0x0000000E,
	0x609,  0x00000022, 0x60C,  0x00000018, 0x6A0,  0x000000FF,
	0x6A1,  0x000000FF, 0x6A2,  0x000000FF, 0x6A3,  0x000000FF,
	0x6A4,  0x000000FF, 0x6A5,  0x000000FF, 0x6DE,  0x00000084,
	0x620,  0x000000FF, 0x621,  0x000000FF, 0x622,  0x000000FF,
	0x623,  0x000000FF, 0x624,  0x000000FF, 0x625,  0x000000FF,
	0x626,  0x000000FF, 0x627,  0x000000FF, 0x638,  0x00000050,
	0x63C,  0x0000000A, 0x63D,  0x0000000A, 0x63E,  0x0000000E,
	0x63F,  0x0000000E, 0x640,  0x00000040, 0x642,  0x00000040,
	0x643,  0x00000000, 0x652,  0x000000C8, 0x66E,  0x00000005,
	0x718,  0x00000040, 0x7D4,  0x00000098,

};

void odm_read_and_config_mp_8822b_mac_reg(struct phy_dm_struct *dm)
{
	u32 i = 0;
	u8 c_cond;
	bool is_matched = true, is_skipped = false;
	u32 array_len = sizeof(array_mp_8822b_mac_reg) / sizeof(u32);
	u32 *array = array_mp_8822b_mac_reg;

	u32 v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(dm, ODM_COMP_INIT,
		     "===> %s\n", __func__);

	for (; (i + 1) < array_len; i = i + 2) {
		v1 = array[i];
		v2 = array[i + 1];

		if (v1 & BIT(31)) { /* positive condition*/
			c_cond = (u8)((v1 & (BIT(29) | BIT(28))) >> 28);
			if (c_cond == COND_ENDIF) { /*end*/
				is_matched = true;
				is_skipped = false;
				ODM_RT_TRACE(dm, ODM_COMP_INIT, "ENDIF\n");
			} else if (c_cond == COND_ELSE) { /*else*/
				is_matched = is_skipped ? false : true;
				ODM_RT_TRACE(dm, ODM_COMP_INIT, "ELSE\n");
			} else { /*if , else if*/
				pre_v1 = v1;
				pre_v2 = v2;
				ODM_RT_TRACE(dm, ODM_COMP_INIT,
					     "IF or ELSE IF\n");
			}
		} else if (v1 & BIT(30)) { /*negative condition*/
			if (is_skipped) {
				is_matched = false;
				continue;
			}

			if (check_positive(dm, pre_v1, pre_v2, v1, v2)) {
				is_matched = true;
				is_skipped = true;
			} else {
				is_matched = false;
				is_skipped = false;
			}
		} else if (is_matched) {
			odm_config_mac_8822b(dm, v1, (u8)v2);
		}
	}
}

u32 odm_get_version_mp_8822b_mac_reg(void) { return 67; }
