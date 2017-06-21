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
 
#ifndef	__PHYDMACS_H__
#define    __PHYDMACS_H__

#define ACS_VERSION	"1.0"

#define ODM_MAX_CHANNEL_2G			14
#define ODM_MAX_CHANNEL_5G			24

// For phydm_AutoChannelSelectSettingAP()
#define STORE_DEFAULT_NHM_SETTING               0
#define RESTORE_DEFAULT_NHM_SETTING             1
#define ACS_NHM_SETTING                         2

typedef struct _ACS_
{
	BOOLEAN		bForceACSResult;
	u1Byte		CleanChannel_2G;
	u1Byte		CleanChannel_5G;
	u2Byte		Channel_Info_2G[2][ODM_MAX_CHANNEL_2G];		//Channel_Info[1]: Channel Score, Channel_Info[2]:Channel_Scan_Times
	u2Byte		Channel_Info_5G[2][ODM_MAX_CHANNEL_5G];	

#if ( DM_ODM_SUPPORT_TYPE & ODM_AP )    
    u1Byte              ACS_Step;
    // NHM Count 0-11
    u1Byte              NHM_Cnt[14][11];

    // AC-Series, for storing previous setting
    u4Byte              Reg0x990; 
    u4Byte              Reg0x994;
    u4Byte              Reg0x998;
    u4Byte              Reg0x99C;
    u1Byte              Reg0x9A0;   // u1Byte

    // N-Series, for storing previous setting
    u4Byte              Reg0x890; 
    u4Byte              Reg0x894;
    u4Byte              Reg0x898;
    u4Byte              Reg0x89C;
    u1Byte              Reg0xE28;   // u1Byte
#endif

}ACS, *PACS;


VOID
odm_AutoChannelSelectInit(
	IN		PVOID			pDM_VOID
);

VOID
odm_AutoChannelSelectReset(
	IN		PVOID			pDM_VOID
);

VOID
odm_AutoChannelSelect(
	IN		PVOID			pDM_VOID,
	IN		u1Byte			Channel
);

u1Byte
ODM_GetAutoChannelSelectResult(
	IN		PVOID			pDM_VOID,
	IN		u1Byte			Band
);

#if ( DM_ODM_SUPPORT_TYPE & ODM_AP )

VOID
phydm_AutoChannelSelectSettingAP(
    IN  PVOID   pDM_VOID,
    IN  u4Byte  Setting,             // 0: STORE_DEFAULT_NHM_SETTING; 1: RESTORE_DEFAULT_NHM_SETTING, 2: ACS_NHM_SETTING
    IN  u4Byte  acs_step        
);

VOID
phydm_GetNHMStatisticsAP(
    IN  PVOID       pDM_VOID,
    IN  u4Byte      idx,                // @ 2G, Real channel number = idx+1
    IN  u4Byte      acs_step
);

#endif  //#if ( DM_ODM_SUPPORT_TYPE & ODM_AP )

#endif  //#ifndef	__PHYDMACS_H__