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

/*============================================================
 include files
============================================================*/

#include "mp_precomp.h"
#include "../phydm_precomp.h"

#if (RTL8723D_SUPPORT == 1)

s1Byte 
odm_CCKRSSI_8723D(
	IN		u1Byte	LNA_idx, 
	IN		u1Byte	VGA_idx
	)
{
	s1Byte	rx_pwr_all = 0x00;	
	
	switch (LNA_idx) {

	case 0xf:
		rx_pwr_all = -46 - (2 * VGA_idx);
		break;			
	case 0xa:
		rx_pwr_all = -20 - (2 * VGA_idx);
		break;
	case 7:	
		rx_pwr_all = -10 - (2 * VGA_idx);
		break;		
	case 4:	
		rx_pwr_all = 4 - (2 * VGA_idx);
		break;
	default:
		break;
	}

	return rx_pwr_all;

}

#endif








