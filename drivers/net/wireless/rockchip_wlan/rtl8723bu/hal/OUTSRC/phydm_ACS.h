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

typedef struct _ACS_
{
	BOOLEAN		bForceACSResult;
	u1Byte		CleanChannel_2G;
	u1Byte		CleanChannel_5G;
	u2Byte		Channel_Info_2G[2][ODM_MAX_CHANNEL_2G];		//Channel_Info[1]: Channel Score, Channel_Info[2]:Channel_Scan_Times
	u2Byte		Channel_Info_5G[2][ODM_MAX_CHANNEL_5G];	
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

#endif
