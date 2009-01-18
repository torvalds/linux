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
    dfs.h

    Abstract:
    Support DFS function.

    Revision History:
    Who       When            What
    --------  ----------      ----------------------------------------------
    Fonchi    03-12-2007      created
*/

#define RADAR_PULSE 1
#define RADAR_WIDTH 2

#define WIDTH_RD_IDLE 0
#define WIDTH_RD_CHECK 1


VOID BbpRadarDetectionStart(
	IN PRTMP_ADAPTER pAd);

VOID BbpRadarDetectionStop(
	IN PRTMP_ADAPTER pAd);

VOID RadarDetectionStart(
	IN PRTMP_ADAPTER pAd,
	IN BOOLEAN CTS_Protect,
	IN UINT8 CTSPeriod);

VOID RadarDetectionStop(
	IN PRTMP_ADAPTER	pAd);

VOID RadarDetectPeriodic(
	IN PRTMP_ADAPTER	pAd);


BOOLEAN RadarChannelCheck(
	IN PRTMP_ADAPTER	pAd,
	IN UCHAR			Ch);

ULONG JapRadarType(
	IN PRTMP_ADAPTER pAd);

ULONG RTMPBbpReadRadarDuration(
	IN PRTMP_ADAPTER	pAd);

ULONG RTMPReadRadarDuration(
	IN PRTMP_ADAPTER	pAd);

VOID RTMPCleanRadarDuration(
	IN PRTMP_ADAPTER	pAd);

VOID RTMPPrepareRDCTSFrame(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			pDA,
	IN	ULONG			Duration,
	IN  UCHAR           RTSRate,
	IN  ULONG           CTSBaseAddr,
	IN  UCHAR			FrameGap);

VOID RTMPPrepareRadarDetectParams(
	IN PRTMP_ADAPTER	pAd);


INT Set_ChMovingTime_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR arg);

INT Set_LongPulseRadarTh_Proc(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR arg);


