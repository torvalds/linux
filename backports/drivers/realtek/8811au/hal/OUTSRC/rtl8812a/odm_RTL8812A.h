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

#ifndef	__ODM_RTL8812A_H__
#define __ODM_RTL8812A_H__

VOID
ODM_PathStatistics_8812A(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u4Byte			MacId,
	IN		u4Byte			RSSI_A,
	IN		u4Byte			RSSI_B
);

VOID
ODM_PathDiversityInit_8812A(	IN	PDM_ODM_T 	pDM_Odm);

VOID
ODM_PathDiversity_8812A(	IN	PDM_ODM_T 	pDM_Odm);

VOID
ODM_SetTxPathByTxInfo_8812A(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
);

 #endif