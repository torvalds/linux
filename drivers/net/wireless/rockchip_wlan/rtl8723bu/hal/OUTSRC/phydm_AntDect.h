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

#define ANTDECT_VERSION	"2.0" //2014.11.04

#if(defined(CONFIG_ANT_DETECTION))
//#if( DM_ODM_SUPPORT_TYPE & (ODM_WIN |ODM_CE))
//ANT Test
#define		ANTTESTALL		0x00	/*Ant A or B will be Testing*/   
#define		ANTTESTA		0x01	/*Ant A will be Testing*/	
#define		ANTTESTB		0x02	/*Ant B will be testing*/

#define	MAX_ANTENNA_DETECTION_CNT	10 


typedef struct _ANT_DETECTED_INFO{
	BOOLEAN			bAntDetected;
	u4Byte			dBForAntA;
	u4Byte			dBForAntB;
	u4Byte			dBForAntO;
}ANT_DETECTED_INFO, *PANT_DETECTED_INFO;


typedef enum tag_SW_Antenna_Switch_Definition
{
	Antenna_A = 1,
	Antenna_B = 2,	
	Antenna_MAX = 3,
}DM_SWAS_E;



//1 [1. Single Tone Method] ===================================================



VOID
ODM_SingleDualAntennaDefaultSetting(
	IN		PVOID		pDM_VOID
	);

BOOLEAN
ODM_SingleDualAntennaDetection(
	IN		PVOID		pDM_VOID,
	IN		u1Byte			mode
	);

//1 [2. Scan AP RSSI Method] ==================================================

#define SwAntDivCheckBeforeLink	ODM_SwAntDivCheckBeforeLink

BOOLEAN 
ODM_SwAntDivCheckBeforeLink(
	IN		PVOID		pDM_VOID
	);




//1 [3. PSD Method] ==========================================================


VOID
ODM_SingleDualAntennaDetection_PSD(
	IN		PVOID		pDM_VOID
);

#endif

VOID
odm_SwAntDetectInit(
	IN		PVOID		pDM_VOID
	);


#endif


