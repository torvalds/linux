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
#ifndef	__ODM_RTL8188E_H__
#define __ODM_RTL8188E_H__


#define	MAIN_ANT_CG_TRX	1
#define	AUX_ANT_CG_TRX	0
#define	MAIN_ANT_CGCS_RX	0
#define	AUX_ANT_CGCS_RX	1

VOID
ODM_DIG_LowerBound_88E(
	IN		PDM_ODM_T		pDM_Odm
);




#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))

#define SwAntDivResetBeforeLink		ODM_SwAntDivResetBeforeLink

VOID ODM_SwAntDivResetBeforeLink(IN	PDM_ODM_T	pDM_Odm);

VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm,
	IN		pu1Byte			pDesc,
	IN		u1Byte			macId	
);
#else// (DM_ODM_SUPPORT_TYPE == ODM_AP)
VOID
ODM_SetTxAntByTxInfo_88E(
	IN		PDM_ODM_T		pDM_Odm	
);
#endif

VOID
odm_PrimaryCCA_Init(
	IN		PDM_ODM_T		pDM_Odm);

BOOLEAN
ODM_DynamicPrimaryCCA_DupRTS(
	IN		PDM_ODM_T		pDM_Odm);

VOID
odm_DynamicPrimaryCCA(
	IN		PDM_ODM_T		pDM_Odm);

#endif

