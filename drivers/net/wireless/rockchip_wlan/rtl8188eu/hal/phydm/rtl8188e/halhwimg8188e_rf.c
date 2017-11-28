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
*                           RadioA.TXT
******************************************************************************/

u32 array_mp_8188e_radioa[] = {
	0x000, 0x00030000,
	0x008, 0x00084000,
	0x018, 0x00000407,
	0x019, 0x00000012,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x98000001,	0x00000000,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x98000001,	0x00000001,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x98000001,	0x00000002,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x98000400,	0x00000000,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x98000000,	0x00000000,	0x40000000,	0x00000000,
	0x01B, 0x00000084,
	0x90000400,	0x00000000,	0x40000000,	0x00000000,
	0xA0000000,	0x00000000,
	0xB0000000,	0x00000000,
	0x01E, 0x00080009,
	0x01F, 0x00000880,
	0x02F, 0x0001A060,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0x98000001,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x98000001,	0x00000001,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x98000001,	0x00000002,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x98000400,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0x98000000,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x000C0000,
	0x90000400,	0x00000000,	0x40000000,	0x00000000,
	0x03F, 0x00000000,
	0xA0000000,	0x00000000,
	0x03F, 0x00000000,
	0xB0000000,	0x00000000,
	0x042, 0x000060C0,
	0x057, 0x000D0000,
	0x058, 0x000BE180,
	0x067, 0x00001552,
	0x083, 0x00000000,
	0x0B0, 0x000FF8FC,
	0x0B1, 0x00054400,
	0x0B2, 0x000CCC19,
	0x0B4, 0x00043003,
	0x0B6, 0x0004953E,
	0x0B7, 0x0001C718,
	0x0B8, 0x000060FF,
	0x0B9, 0x00080001,
	0x0BA, 0x00040000,
	0x0BB, 0x00000400,
	0x0BF, 0x000C0000,
	0x0C2, 0x00002400,
	0x0C3, 0x00000009,
	0x0C4, 0x00040C91,
	0x0C5, 0x00099999,
	0x0C6, 0x000000A3,
	0x0C7, 0x00088820,
	0x0C8, 0x00076C06,
	0x0C9, 0x00000000,
	0x0CA, 0x00080000,
	0x0DF, 0x00000180,
	0x0EF, 0x000001A0,
	0x051, 0x0006B27D,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000001,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000001,	0x00000001,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000001,	0x00000002,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000400,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E4DD,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x98000000,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E49D,
	0x90000400,	0x00000000,	0x40000000,	0x00000000,
	0x052, 0x0007E4DD,
	0xA0000000,	0x00000000,
	0x052, 0x0007E49D,
	0xB0000000,	0x00000000,
	0x053, 0x00000073,
	0x056, 0x00051FF3,
	0x035, 0x00000086,
	0x035, 0x00000186,
	0x035, 0x00000286,
	0x036, 0x00001C25,
	0x036, 0x00009C25,
	0x036, 0x00011C25,
	0x036, 0x00019C25,
	0x0B6, 0x00048538,
	0x018, 0x00000C07,
	0x05A, 0x0004BD00,
	0x019, 0x000739D0,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x034, 0x0000A095,
	0x034, 0x00009092,
	0x034, 0x0000808E,
	0x034, 0x0000704F,
	0x034, 0x0000604C,
	0x034, 0x00005049,
	0x034, 0x0000400C,
	0x034, 0x00003009,
	0x034, 0x00002006,
	0x034, 0x00001003,
	0x034, 0x00000000,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x034, 0x0000A095,
	0x034, 0x00009092,
	0x034, 0x0000808E,
	0x034, 0x0000704F,
	0x034, 0x0000604C,
	0x034, 0x00005049,
	0x034, 0x0000400C,
	0x034, 0x00003009,
	0x034, 0x00002006,
	0x034, 0x00001003,
	0x034, 0x00000000,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x034, 0x0000A095,
	0x034, 0x00009092,
	0x034, 0x0000808E,
	0x034, 0x0000704F,
	0x034, 0x0000604C,
	0x034, 0x00005049,
	0x034, 0x0000400C,
	0x034, 0x00003009,
	0x034, 0x00002006,
	0x034, 0x00001003,
	0x034, 0x00000000,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x034, 0x0000A095,
	0x034, 0x00009092,
	0x034, 0x0000808E,
	0x034, 0x0000704F,
	0x034, 0x0000604C,
	0x034, 0x00005049,
	0x034, 0x0000400C,
	0x034, 0x00003009,
	0x034, 0x00002006,
	0x034, 0x00001003,
	0x034, 0x00000000,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x034, 0x0000A095,
	0x034, 0x00009092,
	0x034, 0x0000808E,
	0x034, 0x0000704F,
	0x034, 0x0000604C,
	0x034, 0x00005049,
	0x034, 0x0000400C,
	0x034, 0x00003009,
	0x034, 0x00002006,
	0x034, 0x00001003,
	0x034, 0x00000000,
	0xA0000000,	0x00000000,
	0x034, 0x0000ADF3,
	0x034, 0x00009DF0,
	0x034, 0x00008DED,
	0x034, 0x00007DEA,
	0x034, 0x00006DE7,
	0x034, 0x000054EE,
	0x034, 0x000044EB,
	0x034, 0x000034E8,
	0x034, 0x0000246B,
	0x034, 0x00001468,
	0x034, 0x0000006D,
	0xB0000000,	0x00000000,
	0x000, 0x00030159,
	0x084, 0x00068200,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00079F80,
	0x98000001,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x98000001,	0x00000001,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x98000001,	0x00000002,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x98000400,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00079F80,
	0x98000000,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x0008014E,
	0x087, 0x0004DF80,
	0x90000400,	0x00000000,	0x40000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0xA0000000,	0x00000000,
	0x086, 0x000000CE,
	0x087, 0x00048A00,
	0xB0000000,	0x00000000,
	0x08E, 0x00065540,
	0x08F, 0x00088000,
	0x0EF, 0x000020A0,
	0x88000003,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000003,	0x00000001,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000003,	0x00000002,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x90000003,	0x00000002,	0x40000000,	0x00000000,
	0x03B, 0x000F07B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D0020,
	0x03B, 0x000CF060,
	0x03B, 0x000B07E0,
	0x03B, 0x000A0010,
	0x03B, 0x00090000,
	0x03B, 0x0008F780,
	0x03B, 0x000727B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000001,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000001,	0x00000001,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000001,	0x00000002,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000400,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x90000002,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x90000001,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x90000001,	0x00000001,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x90000001,	0x00000002,	0x40000000,	0x00000000,
	0x03B, 0x000F07B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D0020,
	0x03B, 0x000CF060,
	0x03B, 0x000B07E0,
	0x03B, 0x000A0010,
	0x03B, 0x00090000,
	0x03B, 0x0008F780,
	0x03B, 0x000727B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0x98000000,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F6030,
	0x03B, 0x000E6030,
	0x03B, 0x000D6030,
	0x03B, 0x000C6030,
	0x03B, 0x000BF030,
	0x03B, 0x000A0020,
	0x03B, 0x00090090,
	0x03B, 0x0008F080,
	0x03B, 0x0007A730,
	0x03B, 0x000607B0,
	0x03B, 0x0005F770,
	0x03B, 0x00040060,
	0x03B, 0x00030090,
	0x03B, 0x00020780,
	0x03B, 0x000107A0,
	0x03B, 0x0000F760,
	0x0EF, 0x000000A0,
	0x90000400,	0x00000000,	0x40000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0xA0000000,	0x00000000,
	0x03B, 0x000F02B0,
	0x03B, 0x000EF7B0,
	0x03B, 0x000D4FB0,
	0x03B, 0x000CF060,
	0x03B, 0x000B0090,
	0x03B, 0x000A0080,
	0x03B, 0x00090080,
	0x03B, 0x0008F780,
	0x03B, 0x000722B0,
	0x03B, 0x0006F7B0,
	0x03B, 0x00054FB0,
	0x03B, 0x0004F060,
	0x03B, 0x00030090,
	0x03B, 0x00020080,
	0x03B, 0x00010080,
	0x03B, 0x0000F780,
	0x0EF, 0x000000A0,
	0xB0000000,	0x00000000,
	0x000, 0x00010159,
	0x018, 0x0000F407,
	0xFFE, 0x00000000,
	0xFFE, 0x00000000,
	0x01F, 0x00080003,
	0xFFE, 0x00000000,
	0xFFE, 0x00000000,
	0x01E, 0x00000001,
	0x01F, 0x00080000,
	0x000, 0x00033E60,

};

void
odm_read_and_config_mp_8188e_radioa(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	u32     i         = 0;
	u8     c_cond;
	bool is_matched = true, is_skipped = false;
	u32     array_len    = sizeof(array_mp_8188e_radioa) / sizeof(u32);
	u32    *array       = array_mp_8188e_radioa;

	u32	v1 = 0, v2 = 0, pre_v1 = 0, pre_v2 = 0;

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8188e_radioa\n"));

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
				odm_config_rf_radio_a_8188e(p_dm_odm, v1, v2);
		}
		i = i + 2;
	}
}

u32
odm_get_version_mp_8188e_radioa(void)
{
	return 70;
}

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8188e[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8188e[]    = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8188e[] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8188e[] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8188e[] = {0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 5, 6, 6, 6, 7, 8, 8, 9, 9, 9, 10, 10, 10, 10, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8188e[] = {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9};
#endif

void
odm_read_and_config_mp_8188e_txpowertrack_ap(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
#if (DM_ODM_SUPPORT_TYPE & (ODM_AP))
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_ap_8188e, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_PCI_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
#endif

void
odm_read_and_config_mp_8188e_txpowertrack_pcie(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
#if DEV_BUS_TYPE == RT_PCI_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_8188e, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           TxPowerTrack_PCIE_ICUT.TXT
******************************************************************************/

u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};

void
odm_read_and_config_mp_8188e_txpowertrack_pcie_icut(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_pcie_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
}

/******************************************************************************
*                           TxPowerTrack_SDIO.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
#endif

void
odm_read_and_config_mp_8188e_txpowertrack_sdio(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
#if DEV_BUS_TYPE == RT_SDIO_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_8188e, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           TxPowerTrack_SDIO_ICUT.TXT
******************************************************************************/

u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};

void
odm_read_and_config_mp_8188e_txpowertrack_sdio_icut(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_sdio_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
}

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

#if DEV_BUS_TYPE == RT_USB_INTERFACE
u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8188e[]    = {0, 0, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 5, 5, 5, 6, 6, 7, 7, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8188e[]    = {0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 7, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8188e[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8188e[] = {0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 6, 6, 6, 6, 7, 7};
#endif

void
odm_read_and_config_mp_8188e_txpowertrack_usb(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
#if DEV_BUS_TYPE == RT_USB_INTERFACE
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_8188e, DELTA_SWINGIDX_SIZE * 3);
#endif
}

/******************************************************************************
*                           TxPowerTrack_USB_ICUT.TXT
******************************************************************************/

u8 g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 17, 17, 17, 17, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
	{0, 1, 2, 3, 3, 5, 5, 6, 6, 7, 8, 9, 10, 11, 11, 12, 13, 14, 14, 15, 15, 16, 16, 17, 17, 18, 18, 18, 18, 18},
};
u8 g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_icut_8188e[][DELTA_SWINGIDX_SIZE] = {
	{0, 1, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
	{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15},
};
u8 g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_icut_8188e[]    = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 7, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
u8 g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_icut_8188e[]    = {0, 0, 0, 1, 2, 2, 2, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 9, 9, 9, 9, 10, 11, 11, 11, 11, 11};
u8 g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};
u8 g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_icut_8188e[] = {0, 1, 2, 2, 2, 3, 3, 4, 4, 4, 5, 5, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
u8 g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_icut_8188e[] = {0, 0, 0, 1, 1, 1, 2, 3, 3, 3, 4, 4, 5, 5, 6, 6, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11, 12, 12, 12, 12};

void
odm_read_and_config_mp_8188e_txpowertrack_usb_icut(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	struct odm_rf_calibration_structure  *p_rf_calibrate_info = &(p_dm_odm->rf_calibrate_info);

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> ODM_ReadAndConfig_MP_MP_8188E\n"));


	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_p, g_delta_swing_table_idx_mp_2ga_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2ga_n, g_delta_swing_table_idx_mp_2ga_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_p, g_delta_swing_table_idx_mp_2gb_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2gb_n, g_delta_swing_table_idx_mp_2gb_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_p, g_delta_swing_table_idx_mp_2g_cck_a_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_a_n, g_delta_swing_table_idx_mp_2g_cck_a_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_p, g_delta_swing_table_idx_mp_2g_cck_b_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_2g_cck_b_n, g_delta_swing_table_idx_mp_2g_cck_b_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE);

	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_p, g_delta_swing_table_idx_mp_5ga_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5ga_n, g_delta_swing_table_idx_mp_5ga_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_p, g_delta_swing_table_idx_mp_5gb_p_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
	odm_move_memory(p_dm_odm, p_rf_calibrate_info->delta_swing_table_idx_5gb_n, g_delta_swing_table_idx_mp_5gb_n_txpowertrack_usb_icut_8188e, DELTA_SWINGIDX_SIZE * 3);
}

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

const char *array_mp_8188e_txpwr_lmt[] = {
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
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "63",
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
	"FCC", "2.4G", "20M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "12", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "63",
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
	"FCC", "2.4G", "40M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "26",
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
odm_read_and_config_mp_8188e_txpwr_lmt(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	u32     i           = 0;
	u32     array_len    = sizeof(array_mp_8188e_txpwr_lmt) / sizeof(u8 *);
	u8 **array      = (u8 **)array_mp_8188e_txpwr_lmt;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	PlatformZeroMemory(p_hal_data->BufOfLinesPwrLmt, MAX_LINES_HWCONFIG_TXT * MAX_BYTES_LINE_HWCONFIG_TXT);
	p_hal_data->nLinesReadPwrLmt = array_len / 7;
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8188e_txpwr_lmt\n"));

	for (i = 0; i < array_len; i += 7) {
		u8 *regulation = array[i];
		u8 *band = array[i + 1];
		u8 *bandwidth = array[i + 2];
		u8 *rate = array[i + 3];
		u8 *rf_path = array[i + 4];
		u8 *chnl = array[i + 5];
		u8 *val = array[i + 6];

		odm_config_bb_txpwr_lmt_8188e(p_dm_odm, regulation, band, bandwidth, rate, rf_path, chnl, val);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		rsprintf((char *)p_hal_data->BufOfLinesPwrLmt[i / 7], 100, "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\",",
			 regulation, band, bandwidth, rate, rf_path, chnl, val);
#endif
	}

}

/******************************************************************************
*                           TXPWR_LMT_88EE_M2_for_MSI.TXT
******************************************************************************/

const char *array_mp_8188e_txpwr_lmt_88ee_m2_for_msi[] = {
	"FCC", "2.4G", "20M", "CCK", "1T", "01", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "01", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "01", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "02", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "02", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "02", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "03", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "03", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "03", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "04", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "04", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "04", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "05", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "05", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "05", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "06", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "06", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "06", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "07", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "07", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "07", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "08", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "08", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "08", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "09", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "09", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "09", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "10", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "10", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "10", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "11", "32",
	"ETSI", "2.4G", "20M", "CCK", "1T", "11", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "11", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "12", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "12", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "13", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "13", "28",
	"MKK", "2.4G", "20M", "CCK", "1T", "13", "32",
	"FCC", "2.4G", "20M", "CCK", "1T", "14", "63",
	"ETSI", "2.4G", "20M", "CCK", "1T", "14", "63",
	"MKK", "2.4G", "20M", "CCK", "1T", "14", "32",
	"FCC", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "01", "28",
	"MKK", "2.4G", "20M", "OFDM", "1T", "01", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "02", "28",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "02", "28",
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
	"FCC", "2.4G", "20M", "OFDM", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"MKK", "2.4G", "20M", "OFDM", "1T", "12", "30",
	"FCC", "2.4G", "20M", "OFDM", "1T", "13", "63",
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
	"FCC", "2.4G", "20M", "HT", "1T", "12", "63",
	"ETSI", "2.4G", "20M", "HT", "1T", "12", "30",
	"MKK", "2.4G", "20M", "HT", "1T", "12", "30",
	"FCC", "2.4G", "20M", "HT", "1T", "13", "63",
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
	"FCC", "2.4G", "40M", "HT", "1T", "10", "26",
	"ETSI", "2.4G", "40M", "HT", "1T", "10", "26",
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
odm_read_and_config_mp_8188e_txpwr_lmt_88e_e_m2_for_msi(
	struct PHY_DM_STRUCT  *p_dm_odm
)
{
	u32     i           = 0;
	u32     array_len    = sizeof(array_mp_8188e_txpwr_lmt_88ee_m2_for_msi) / sizeof(u8 *);
	u8 **array      = (u8 **)array_mp_8188e_txpwr_lmt_88ee_m2_for_msi;

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	struct _ADAPTER		*adapter = p_dm_odm->adapter;
	HAL_DATA_TYPE	*p_hal_data = GET_HAL_DATA(adapter);

	PlatformZeroMemory(p_hal_data->BufOfLinesPwrLmt, MAX_LINES_HWCONFIG_TXT * MAX_BYTES_LINE_HWCONFIG_TXT);
	p_hal_data->nLinesReadPwrLmt = array_len / 7;
#endif

	ODM_RT_TRACE(p_dm_odm, ODM_COMP_INIT, ODM_DBG_LOUD, ("===> odm_read_and_config_mp_8188e_txpwr_lmt_88e_e_m2_for_msi\n"));

	for (i = 0; i < array_len; i += 7) {
		u8 *regulation = array[i];
		u8 *band = array[i + 1];
		u8 *bandwidth = array[i + 2];
		u8 *rate = array[i + 3];
		u8 *rf_path = array[i + 4];
		u8 *chnl = array[i + 5];
		u8 *val = array[i + 6];

		odm_config_bb_txpwr_lmt_8188e(p_dm_odm, regulation, band, bandwidth, rate, rf_path, chnl, val);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		rsprintf((char *)p_hal_data->BufOfLinesPwrLmt[i / 7], 100, "\"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\", \"%s\",",
			 regulation, band, bandwidth, rate, rf_path, chnl, val);
#endif
	}

}

#endif /* end of HWIMG_SUPPORT*/
