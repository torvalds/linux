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
 
#ifndef	__PHYDMEDCATURBOCHECK_H__
#define    __PHYDMEDCATURBOCHECK_H__

/*#define EDCATURBO_VERSION	"2.1"*/
#define EDCATURBO_VERSION	"2.2"	/*2015.01.13*/

typedef struct _EDCA_TURBO_
{
	BOOLEAN bCurrentTurboEDCA;
	BOOLEAN bIsCurRDLState;

	#if(DM_ODM_SUPPORT_TYPE == ODM_CE	)
	u4Byte	prv_traffic_idx; // edca turbo
	#endif

}EDCA_T,*pEDCA_T;

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
static u4Byte edca_setting_UL[HT_IOT_PEER_MAX] = 
// UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU        MARVELL	92U_AP		SELF_AP(DownLink/Tx)
{ 0x5e4322, 		0xa44f, 		0x5e4322,		0x5ea32b,  		0x5ea422, 	0x5ea322,	0x3ea430,	0x5ea42b, 0x5ea44f,	0x5e4322,	0x5e4322};


static u4Byte edca_setting_DL[HT_IOT_PEER_MAX] = 
// UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU,       MARVELL	92U_AP		SELF_AP(UpLink/Rx)
{ 0xa44f, 		0x5ea44f, 	0x5e4322, 		0x5ea42b, 		0xa44f, 		0xa630, 		0x5ea630,	0x5ea42b, 0xa44f,		0xa42b,		0xa42b};

static u4Byte edca_setting_DL_GMode[HT_IOT_PEER_MAX] = 
// UNKNOWN		REALTEK_90	REALTEK_92SE	BROADCOM		RALINK		ATHEROS		CISCO		MERU,       MARVELL	92U_AP		SELF_AP
{ 0x4322, 		0xa44f, 		0x5e4322,		0xa42b, 			0x5e4322, 	0x4322, 		0xa42b,		0x5ea42b, 0xa44f,		0x5e4322,	0x5ea42b};

#endif



VOID
odm_EdcaTurboCheck(
	IN 	PVOID	 	pDM_VOID
	);
VOID
ODM_EdcaTurboInit(
	IN 	PVOID	 	pDM_VOID
);

#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
VOID
odm_EdcaTurboCheckMP(
	IN 	PVOID	 	pDM_VOID
	);

//check if edca turbo is disabled
BOOLEAN
odm_IsEdcaTurboDisable(
	IN 	PVOID	 	pDM_VOID
);
//choose edca paramter for special IOT case
VOID 
ODM_EdcaParaSelByIot(
	IN		PVOID					pDM_VOID,
	OUT	u4Byte		*EDCA_BE_UL,
	OUT u4Byte		*EDCA_BE_DL
	);
//check if it is UL or DL
VOID
odm_EdcaChooseTrafficIdx( 
	IN 	PVOID	 	pDM_VOID,
	IN	u8Byte  			cur_tx_bytes,  
	IN	u8Byte  			cur_rx_bytes, 
	IN	BOOLEAN 		bBiasOnRx,
	OUT BOOLEAN 		*pbIsCurRDLState
	);

#elif (DM_ODM_SUPPORT_TYPE==ODM_CE)
VOID
odm_EdcaTurboCheckCE(
	IN 	PVOID	 	pDM_VOID
	);
#endif

#endif
