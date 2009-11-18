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
    ap_ids.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
*/

VOID RTMPIdsPeriodicExec(
	IN PVOID SystemSpecific1,
	IN PVOID FunctionContext,
	IN PVOID SystemSpecific2,
	IN PVOID SystemSpecific3);

BOOLEAN RTMPSpoofedMgmtDetection(
	IN PRTMP_ADAPTER	pAd,
	IN PHEADER_802_11	pHeader,
	IN CHAR				Rssi0,
	IN CHAR				Rssi1,
	IN CHAR				Rssi2);

VOID RTMPConflictSsidDetection(
	IN PRTMP_ADAPTER	pAd,
	IN PUCHAR			pSsid,
	IN UCHAR			SsidLen,
	IN CHAR				Rssi0,
	IN CHAR				Rssi1,
	IN CHAR				Rssi2);

BOOLEAN RTMPReplayAttackDetection(
	IN PRTMP_ADAPTER	pAd,
	IN PUCHAR			pAddr2,
	IN CHAR				Rssi0,
	IN CHAR				Rssi1,
	IN CHAR				Rssi2);

VOID RTMPUpdateStaMgmtCounter(
	IN PRTMP_ADAPTER	pAd,
	IN USHORT			type);

VOID RTMPClearAllIdsCounter(
	IN PRTMP_ADAPTER	pAd);

VOID RTMPIdsStart(
	IN PRTMP_ADAPTER	pAd);

VOID RTMPIdsStop(
	IN PRTMP_ADAPTER	pAd);

VOID rtmp_read_ids_from_file(
			IN  PRTMP_ADAPTER pAd,
			char *tmpbuf,
			char *buffer);
