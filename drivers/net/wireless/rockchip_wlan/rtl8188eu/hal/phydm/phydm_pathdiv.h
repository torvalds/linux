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
/*#define PATHDIV_VERSION "2.0" //2014.11.04*/
#define PATHDIV_VERSION	"3.0" /*2015.01.13 Dino*/

#if(defined(CONFIG_PATH_DIVERSITY))
#define USE_PATH_A_AS_DEFAULT_ANT   //for 8814 dynamic TX path selection

#define	NUM_RESET_DTP_PERIOD 5
#define	ANT_DECT_RSSI_TH 3 

#define PATH_A 1
#define PATH_B 2
#define PATH_C 3
#define PATH_D 4

#define PHYDM_AUTO_PATH	0
#define PHYDM_FIX_PATH		1

#define NUM_CHOOSE2_FROM4 6
#define NUM_CHOOSE3_FROM4 4


#define		PHYDM_A		 BIT0
#define		PHYDM_B		 BIT1
#define		PHYDM_C		 BIT2
#define		PHYDM_D		 BIT3
#define		PHYDM_AB	 (BIT0 | BIT1)  // 0
#define		PHYDM_AC	 (BIT0 | BIT2)  // 1
#define		PHYDM_AD	 (BIT0 | BIT3)  // 2
#define		PHYDM_BC	 (BIT1 | BIT2)  // 3
#define		PHYDM_BD	 (BIT1 | BIT3)  // 4
#define		PHYDM_CD	 (BIT2 | BIT3)  // 5

#define		PHYDM_ABC	 (BIT0 | BIT1 | BIT2) /* 0*/
#define		PHYDM_ABD	 (BIT0 | BIT1 | BIT3) /* 1*/
#define		PHYDM_ACD	 (BIT0 | BIT2 | BIT3) /* 2*/
#define		PHYDM_BCD	 (BIT1 | BIT2 | BIT3) /* 3*/

#define		PHYDM_ABCD	 (BIT0 | BIT1 | BIT2 | BIT3)


typedef enum dtp_state
{
	PHYDM_DTP_INIT=1,
	PHYDM_DTP_RUNNING_1

}PHYDM_DTP_STATE;

typedef enum path_div_type
{
	PHYDM_2R_PATH_DIV = 1,
	PHYDM_4R_PATH_DIV = 2
}PHYDM_PATH_DIV_TYPE;

VOID
phydm_process_rssi_for_path_div(	
	IN OUT		PVOID			pDM_VOID,	
	IN			PVOID			p_phy_info_void,
	IN			PVOID			p_pkt_info_void
	);

typedef struct _ODM_PATH_DIVERSITY_
{
	u1Byte	RespTxPath;
	u1Byte	PathSel[ODM_ASSOCIATE_ENTRY_NUM];
	u4Byte	PathA_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u4Byte	PathB_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u2Byte	PathA_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u2Byte	PathB_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u1Byte	path_div_type;
  #if RTL8814A_SUPPORT
	
	u4Byte	path_a_sum_all;
	u4Byte	path_b_sum_all;
	u4Byte	path_c_sum_all;
	u4Byte	path_d_sum_all;

	u4Byte	path_a_cnt_all;
	u4Byte	path_b_cnt_all;
	u4Byte	path_c_cnt_all;
	u4Byte	path_d_cnt_all;
	
	u1Byte	dtp_period;
	BOOLEAN	bBecomeLinked;
	BOOLEAN	is_u3_mode;
	u1Byte	num_tx_path;
	u1Byte	default_path;
	u1Byte	num_candidate;
	u1Byte	ant_candidate_1;
	u1Byte	ant_candidate_2;
	u1Byte	ant_candidate_3;
	u1Byte     dtp_state;
	u1Byte	dtp_check_patha_counter;
	BOOLEAN	fix_path_bfer;
	u1Byte	search_space_2[NUM_CHOOSE2_FROM4];
	u1Byte	search_space_3[NUM_CHOOSE3_FROM4];
	
	u1Byte	pre_tx_path;
	u1Byte	use_path_a_as_default_ant;
	BOOLEAN is_pathA_exist;

  #endif
}PATHDIV_T, *pPATHDIV_T;


#endif //#if(defined(CONFIG_PATH_DIVERSITY))

VOID
phydm_c2h_dtp_handler(
	 IN	PVOID	pDM_VOID,
	 IN 	pu1Byte   CmdBuf,
	 IN 	u1Byte	CmdLen
	);

VOID
odm_PathDiversityInit(
	IN	PVOID	pDM_VOID
	);

VOID
odm_PathDiversity(
	IN	PVOID	pDM_VOID
	);

VOID
odm_pathdiv_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char		*output,
	IN		u4Byte		*_out_len
	);



//1 [OLD IC]--------------------------------------------------------------------------------






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

