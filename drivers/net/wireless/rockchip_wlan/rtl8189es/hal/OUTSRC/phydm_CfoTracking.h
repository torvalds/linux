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

#ifndef	__PHYDMCFOTRACK_H__
#define    __PHYDMCFOTRACK_H__

#define CFO_TRACKING_VERSION	"1.0"

#define		CFO_TH_XTAL_HIGH			20			// kHz
#define		CFO_TH_XTAL_LOW			10			// kHz
#define		CFO_TH_ATC					80			// kHz

typedef struct _CFO_TRACKING_
{
	BOOLEAN			bATCStatus;
	BOOLEAN			largeCFOHit;
	BOOLEAN			bAdjust;
	u1Byte			CrystalCap;
	u1Byte			DefXCap;
	int				CFO_tail[2];
	int				CFO_ave_pre;
	u4Byte			packetCount;
	u4Byte			packetCount_pre;

	BOOLEAN			bForceXtalCap;
	BOOLEAN			bReset;
}CFO_TRACKING, *PCFO_TRACKING;

VOID
ODM_CfoTrackingReset(
	IN		PVOID					pDM_VOID
);

VOID
ODM_CfoTrackingInit(
	IN		PVOID					pDM_VOID
);

VOID
ODM_CfoTracking(
	IN		PVOID					pDM_VOID
);

VOID
ODM_ParsingCFO(
	IN		PVOID					pDM_VOID,
	IN		PVOID					pPktinfo_VOID,
	IN     	s1Byte* 					pcfotail
);

#endif