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
#ifndef	__ODM_RTL8192E_H__
#define __ODM_RTL8192E_H__

#define	OFDMCCA_TH	500
#define	BW_Ind_Bias	500
#define	MF_USC			2
#define	MF_LSC			1
#define	MF_USC_LSC		0
#define	Monitor_TIME	30

VOID
odm_Write_Dynamic_CCA(
	IN	PDM_ODM_T		pDM_Odm,
	IN	u1Byte			CurrentMFstate
	);

VOID
odm_PrimaryCCA_Check_Init(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_DynamicPrimaryCCA_Check(
	IN		PDM_ODM_T		pDM_Odm
	);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

VOID
odm_DynamicPrimaryCCAMP(
	IN		PDM_ODM_T		pDM_Odm
	);

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

VOID
odm_DynamicPrimaryCCAAP(
	IN		PDM_ODM_T		pDM_Odm
	);

VOID
odm_Intf_Detection(
	IN		PDM_ODM_T		pDM_Odm
	);

#endif

#endif
