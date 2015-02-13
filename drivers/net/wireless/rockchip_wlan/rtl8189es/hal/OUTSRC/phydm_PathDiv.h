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
 
#ifndef	__PHYDMPATHDIV_H__
#define    __PHYDMPATHDIV_H__

#define PATHDIV_VERSION	"1.0"

VOID	
odm_PathDiversityInit(
	IN	PVOID	pDM_VOID
	);

VOID    
odm_PathDiversity(
	IN	PVOID	pDM_VOID
	);

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN)) 

//#define   PATHDIV_ENABLE 	 1
#define dm_PathDiv_RSSI_Check	ODM_PathDivChkPerPktRssi
#define PathDivCheckBeforeLink8192C	ODM_PathDiversityBeforeLink92C

typedef struct _PathDiv_Parameter_define_
{
	u4Byte org_5g_RegE30;
	u4Byte org_5g_RegC14;
	u4Byte org_5g_RegCA0;
	u4Byte swt_5g_RegE30;
	u4Byte swt_5g_RegC14;
	u4Byte swt_5g_RegCA0;
	//for 2G IQK information
	u4Byte org_2g_RegC80;
	u4Byte org_2g_RegC4C;
	u4Byte org_2g_RegC94;
	u4Byte org_2g_RegC14;
	u4Byte org_2g_RegCA0;

	u4Byte swt_2g_RegC80;
	u4Byte swt_2g_RegC4C;
	u4Byte swt_2g_RegC94;
	u4Byte swt_2g_RegC14;
	u4Byte swt_2g_RegCA0;
}PATHDIV_PARA,*pPATHDIV_PARA;

VOID	
odm_PathDiversityInit_92C(
	IN	PADAPTER	Adapter
	);

VOID	
odm_2TPathDiversityInit_92C(
	IN	PADAPTER	Adapter
	);

VOID	
odm_1TPathDiversityInit_92C(	
	IN	PADAPTER	Adapter
	);

BOOLEAN
odm_IsConnected_92C(
	IN	PADAPTER	Adapter
	);

BOOLEAN 
ODM_PathDiversityBeforeLink92C(
	//IN	PADAPTER	Adapter
	IN		PDM_ODM_T		pDM_Odm
	);

VOID	
odm_PathDiversityAfterLink_92C(
	IN	PADAPTER	Adapter
	);

VOID
odm_SetRespPath_92C(	
	IN	PADAPTER	Adapter, 	
	IN	u1Byte	DefaultRespPath
	);

VOID	
odm_OFDMTXPathDiversity_92C(
	IN	PADAPTER	Adapter
	);

VOID	
odm_CCKTXPathDiversity_92C(	
	IN	PADAPTER	Adapter
	);

VOID	
odm_ResetPathDiversity_92C(	
	IN	PADAPTER	Adapter
	);

VOID
odm_CCKTXPathDiversityCallback(
	PRT_TIMER		pTimer
	);

VOID
odm_CCKTXPathDiversityWorkItemCallback(
	IN PVOID            pContext
	);

VOID
odm_PathDivChkAntSwitchCallback(
	PRT_TIMER		pTimer
	);

VOID
odm_PathDivChkAntSwitchWorkitemCallback(
	IN PVOID            pContext
	);


VOID 
odm_PathDivChkAntSwitch(
	PDM_ODM_T    pDM_Odm
	);

VOID
ODM_CCKPathDiversityChkPerPktRssi(
	PADAPTER		Adapter,
	BOOLEAN			bIsDefPort,
	BOOLEAN			bMatchBSSID,
	PRT_WLAN_STA	pEntry,
	PRT_RFD			pRfd,
	pu1Byte			pDesc
	);

VOID 
ODM_PathDivChkPerPktRssi(
	PADAPTER		Adapter,
	BOOLEAN			bIsDefPort,
	BOOLEAN			bMatchBSSID,
	PRT_WLAN_STA	pEntry,
	PRT_RFD			pRfd	
	);

VOID
ODM_PathDivRestAfterLink(
	IN	PDM_ODM_T		pDM_Odm
	);

VOID
ODM_FillTXPathInTXDESC(
		IN	PADAPTER	Adapter,
		IN	PRT_TCB		pTcb,
		IN	pu1Byte		pDesc
	);

VOID
odm_PathDivInit_92D(
	IN	PDM_ODM_T 	pDM_Odm
	);

u1Byte
odm_SwAntDivSelectScanChnl(
	IN	PADAPTER	Adapter
	);

VOID
odm_SwAntDivConstructScanChnl(
	IN	PADAPTER	Adapter,
	IN	u1Byte		ScanChnl
	);
	
 #endif       //#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN)) 
 
 
 #endif		 //#ifndef  __ODMPATHDIV_H__

