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

#include "mp_precomp.h"
#include "../phydm_precomp.h"
#if (RTL8703B_SUPPORT == 1)
s8 phydm_cck_rssi_8703b(struct dm_struct *dm, u16 lna_idx, u8 vga_idx)
{
	s8 rx_pwr_all = 0x00;

	switch (lna_idx) {
	case 0xf:
		rx_pwr_all = -48 - (2 * vga_idx);
		break;
	case 0xb:
		rx_pwr_all = -42 - (2 * vga_idx); /*TBD*/
		break;
	case 0xa:
		rx_pwr_all = -36 - (2 * vga_idx);
		break;
	case 8:
		rx_pwr_all = -32 - (2 * vga_idx);
		break;
	case 7:
		rx_pwr_all = -19 - (2 * vga_idx);
		break;
	case 4:
		rx_pwr_all = -6 - (2 * vga_idx);
		break;
	case 0:
		rx_pwr_all = -2 - (2 * vga_idx);
		break;
	default:
		/*rx_pwr_all = -53+(2*(31-vga_idx));*/
		/*dbg_print("wrong LNA index\n");*/
		break;
	}
	return rx_pwr_all;
}
#endif

