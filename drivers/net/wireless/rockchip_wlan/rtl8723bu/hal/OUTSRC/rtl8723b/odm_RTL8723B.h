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
#ifndef	__ODM_RTL8723B_H__
#define __ODM_RTL8723B_H__

#define	DM_DIG_MIN_NIC_8723	0x1C

VOID 
odm_DIG_8723(IN		PDM_ODM_T		pDM_Odm);

s1Byte
odm_CCKRSSI_8723B(
	IN		u1Byte	LNA_idx,
	IN		u1Byte	VGA_idx
	);



#endif
