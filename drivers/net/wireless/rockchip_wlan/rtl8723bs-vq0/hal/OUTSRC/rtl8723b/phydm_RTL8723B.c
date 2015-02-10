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

//============================================================
// include files
//============================================================

#include "Mp_Precomp.h"
#include "../phydm_precomp.h"

#if (RTL8723B_SUPPORT == 1)

 s1Byte
odm_CCKRSSI_8723B(
	IN		u1Byte	LNA_idx,
	IN		u1Byte	VGA_idx
	)
{
	s1Byte	rx_pwr_all=0x00;
	switch(LNA_idx)
	{
		//46  53 73 95 201301231630
		// 46 53 77 99 201301241630
		
		case 6:	
                        rx_pwr_all = -34 - (2 * VGA_idx);
			break;
		case 4:	
                        rx_pwr_all = -14 - (2 * VGA_idx);
			break;
		case 1:	
                        rx_pwr_all = 6 - (2 * VGA_idx);
			break;
		case 0:	
                        rx_pwr_all = 16 - (2 * VGA_idx);	
			break;
		default:
                        //rx_pwr_all = -53+(2*(31-VGA_idx));
                        //DbgPrint("wrong LNA index\n");
			break;
			
	}
	return	rx_pwr_all;
}

#endif		// end if RTL8723B 








