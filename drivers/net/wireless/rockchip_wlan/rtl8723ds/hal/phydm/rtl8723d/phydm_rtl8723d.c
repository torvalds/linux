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

/*============================================================
 include files
============================================================*/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8723D_SUPPORT == 1)

s8
odm_cckrssi_8723d(
	u8	lna_idx,
	u8	vga_idx
)
{
	s8	rx_pwr_all = 0x00;

	switch (lna_idx) {

	case 0xf:
		rx_pwr_all = -46 - (2 * vga_idx);
		break;
	case 0xa:
		rx_pwr_all = -20 - (2 * vga_idx);
		break;
	case 7:
		rx_pwr_all = -10 - (2 * vga_idx);
		break;
	case 4:
		rx_pwr_all = 4 - (2 * vga_idx);
		break;
	default:
		break;
	}

	return rx_pwr_all;

}

#endif
