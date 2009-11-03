/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2007, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************

    Module Name:
    ap_autoChSel_cmm.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
*/


#ifndef __AUTOCHSELECT_CMM_H__
#define __AUTOCHSELECT_CMM_H__

#define RSSI_TO_DBM_OFFSET 120 // RSSI-115 = dBm


typedef struct {
	ULONG dirtyness[MAX_NUM_OF_CHANNELS+1];
	ULONG max_rssi[MAX_NUM_OF_CHANNELS+1];
	ULONG total_rssi[MAX_NUM_OF_CHANNELS+1];
	UINT32 FalseCCA[MAX_NUM_OF_CHANNELS+1];
} CHANNELINFO, *PCHANNELINFO;

typedef struct {
	UCHAR Bssid[MAC_ADDR_LEN];
	UCHAR SsidLen;
	CHAR Ssid[MAX_LEN_OF_SSID];
	UCHAR Channel;
	UCHAR ExtChOffset;
	UCHAR Rssi;
} BSSENTRY, *PBSSENTRY;

typedef struct {
	UCHAR BssNr;
	BSSENTRY BssEntry[MAX_LEN_OF_BSS_TABLE];
} BSSINFO, *PBSSINFO;

#endif // __AUTOCHSELECT_CMM_H__ //
