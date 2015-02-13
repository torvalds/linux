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
 
#ifndef	__PHYDMANTDECT_H__
#define    __PHYDMANTDECT_H__

#define ANTDECT_VERSION	"1.0"

#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN |ODM_CE))
//1 [1. Single Tone Method] ===================================================



VOID
ODM_SingleDualAntennaDefaultSetting(
	IN		PDM_ODM_T		pDM_Odm
	);

BOOLEAN
ODM_SingleDualAntennaDetection(
	IN		PDM_ODM_T		pDM_Odm,
	IN		u1Byte			mode
	);

//1 [2. Scan AP RSSI Method] ==================================================

VOID
odm_SwAntDetectInit(
	IN 		PDM_ODM_T 		pDM_Odm
	);


#define SwAntDivCheckBeforeLink	ODM_SwAntDivCheckBeforeLink

BOOLEAN 
ODM_SwAntDivCheckBeforeLink(
	IN		PDM_ODM_T		pDM_Odm
	);




//1 [3. PSD Method] ==========================================================


VOID
ODM_SingleDualAntennaDetection_PSD(
	IN	 PDM_ODM_T 	pDM_Odm
);






#endif
#endif


