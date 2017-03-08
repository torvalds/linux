
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
 
#ifndef	__PHYDMADAPTIVITY_H__
#define    __PHYDMADAPTIVITY_H__

#define ADAPTIVITY_VERSION	"9.3.3"	/*20151230 changed by Kevin, modify 0x524[11] when adaptivity is enabled*/

#define PwdBUpperBound	7
#define DFIRloss	5

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN))
typedef enum _tag_PhyDM_REGULATION_Type {
	REGULATION_FCC = 0,
	REGULATION_MKK = 1,
	REGULATION_ETSI = 2,
	REGULATION_WW = 3,	
	
	MAX_REGULATION_NUM = 4
} PhyDM_REGULATION_TYPE;
#endif

typedef enum _PHYDM_ADAPTIVITY_Info_Definition {
	PHYDM_ADAPINFO_CARRIER_SENSE_ENABLE = 0,
	PHYDM_ADAPINFO_DCBACKOFF,
	PHYDM_ADAPINFO_DYNAMICLINKADAPTIVITY,
	PHYDM_ADAPINFO_TH_L2H_INI,
	PHYDM_ADAPINFO_TH_EDCCA_HL_DIFF,
	PHYDM_ADAPINFO_AP_NUM_TH

} PHYDM_ADAPINFO_E;



typedef enum tag_PhyDM_set_LNA {
	PhyDM_disable_LNA		= 0,
	PhyDM_enable_LNA		= 1,
} PhyDM_set_LNA;


typedef enum tag_PhyDM_TRx_MUX_Type
{
	PhyDM_SHUTDOWN			= 0,
	PhyDM_STANDBY_MODE		= 1,
	PhyDM_TX_MODE			= 2,
	PhyDM_RX_MODE			= 3
}PhyDM_Trx_MUX_Type;

typedef enum tag_PhyDM_MACEDCCA_Type
{
	PhyDM_IGNORE_EDCCA			= 0,
	PhyDM_DONT_IGNORE_EDCCA	= 1
}PhyDM_MACEDCCA_Type;

typedef struct _ADAPTIVITY_STATISTICS {
	s1Byte			TH_L2H_ini_backup;
	s1Byte			TH_EDCCA_HL_diff_backup;
	s1Byte			IGI_Base;
	u1Byte			IGI_target;
	u1Byte			NHMWait;
	s1Byte			H2L_lb;
	s1Byte			L2H_lb;
	BOOLEAN			bFirstLink;
	BOOLEAN			bCheck;
	BOOLEAN			DynamicLinkAdaptivity;
	u1Byte			APNumTH;
	u1Byte			AdajustIGILevel;
	BOOLEAN			AcsForAdaptivity;
	s1Byte			backupL2H;
	s1Byte			backupH2L;
	BOOLEAN			bStopEDCCA;
#if (DM_ODM_SUPPORT_TYPE & ODM_WIN)	
	RT_WORK_ITEM	phydm_pauseEDCCAWorkItem;
	RT_WORK_ITEM	phydm_resumeEDCCAWorkItem;
#endif
} ADAPTIVITY_STATISTICS, *PADAPTIVITY_STATISTICS;

VOID
phydm_pauseEDCCA(
	IN	PVOID	pDM_VOID,
	IN	BOOLEAN	bPasueEDCCA
);

VOID
Phydm_CheckAdaptivity(
	IN		PVOID			pDM_VOID
	);

VOID
Phydm_CheckEnvironment(
	IN		PVOID					pDM_VOID
	);

VOID
Phydm_NHMCounterStatisticsInit(
	IN		PVOID					pDM_VOID
	);

VOID
Phydm_NHMCounterStatistics(
	IN		PVOID					pDM_VOID
	);

VOID
Phydm_NHMCounterStatisticsReset(
	IN		PVOID			pDM_VOID
);

VOID
Phydm_GetNHMCounterStatistics(
	IN		PVOID			pDM_VOID
);

VOID
Phydm_MACEDCCAState(
	IN	PVOID					pDM_VOID,
	IN	PhyDM_MACEDCCA_Type		State
);

VOID
Phydm_SetEDCCAThreshold(
	IN		PVOID		pDM_VOID,
	IN		s1Byte		H2L,
	IN		s1Byte		L2H
);

VOID
Phydm_SetTRxMux(
	IN		PVOID			pDM_VOID,
	IN		PhyDM_Trx_MUX_Type			txMode,
	IN		PhyDM_Trx_MUX_Type			rxMode
);	

BOOLEAN
Phydm_CalNHMcnt(
	IN		PVOID		pDM_VOID
);

VOID
Phydm_SearchPwdBLowerBound(
	IN		PVOID					pDM_VOID
);

VOID
phydm_adaptivityInfoInit(
	IN	PVOID			pDM_VOID,
	IN	PHYDM_ADAPINFO_E	CmnInfo,
	IN	u4Byte				Value	
	);

VOID 
Phydm_AdaptivityInit(
	IN		PVOID					pDM_VOID
	);

VOID
Phydm_Adaptivity(
	IN		PVOID					pDM_VOID,
	IN		u1Byte					IGI
	);

VOID
phydm_setEDCCAThresholdAPI(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	IGI
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
Phydm_DisableEDCCA(
	IN		PVOID					pDM_VOID
);

VOID
Phydm_DynamicEDCCA(
	IN		PVOID					pDM_VOID
);

VOID
Phydm_AdaptivityBSOD(
	IN		PVOID					pDM_VOID
);

#endif

VOID 
phydm_pauseEDCCA_WorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	);

VOID 
phydm_resumeEDCCA_WorkItemCallback(
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	PADAPTER		Adapter
#else
	IN PVOID			pDM_VOID
#endif
	);

#endif
