/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
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
 *************************************************************************/


#define RADAR_PULSE 1
#define RADAR_WIDTH 2

#define WIDTH_RD_IDLE 0
#define WIDTH_RD_CHECK 1



/*************************************************************************
  *
  *	DFS Radar related definitions.
  *
  ************************************************************************/  
/*#define CARRIER_DETECT_TASK_NUM	6 */
/*#define RADAR_DETECT_TASK_NUM	7 */

/* McuRadarState && McuCarrierState for 2880-SW-MCU */
#define FREE_FOR_TX				0
#define WAIT_CTS_BEING_SENT		1
#define DO_DETECTION			2

/* McuRadarEvent */
#define RADAR_EVENT_CTS_SENT			0x01 /* Host signal MCU that CTS has been sent */
#define RADAR_EVENT_CTS_CARRIER_SENT	0x02 /* Host signal MCU that CTS has been sent (Carrier) */
#define RADAR_EVENT_RADAR_DETECTING		0x04 /* Radar detection is on going, carrier detection hold back */
#define RADAR_EVENT_CARRIER_DETECTING	0x08 /* Carrier detection is on going, radar detection hold back */
#define RADAR_EVENT_WIDTH_RADAR			0x10 /* BBP == 2 radar detected */
#define RADAR_EVENT_CTS_KICKED			0x20 /* Radar detection need to sent double CTS, first CTS sent */

/* McuRadarCmd */
#define DETECTION_STOP			0
#define RADAR_DETECTION			1
#define CARRIER_DETECTION		2

#if defined(RTMP_RBUS_SUPPORT) || defined(DFS_INTERRUPT_SUPPORT)
#define RADAR_GPIO_DEBUG	0x01 /* GPIO external debug */
#define RADAR_SIMULATE		0x02 /* simulate a short pulse hit this channel */
#define RADAR_SIMULATE2		0x04 /* print any hit */
#define RADAR_LOG			0x08 /* log record and ready for print */

/* Both Old and New DFS */
#define RADAR_DONT_SWITCH		0x10 /* Don't Switch channel when hit */

#ifdef DFS_HARDWARE_SUPPORT
/* New DFS only */
#define RADAR_DEBUG_EVENT			0x01 /* print long pulse debug event */
#define RADAR_DEBUG_FLAG_1			0x02
#define RADAR_DEBUG_FLAG_2			0x04
#define RADAR_DEBUG_FLAG_3			0x08
#define RADAR_DEBUG_SILENCE			0x4
#define RADAR_DEBUG_SW_SILENCE		0x8
#endif /* DFS_HARDWARE_SUPPORT */

#ifdef DFS_HARDWARE_SUPPORT
VOID NewRadarDetectionStart(
	IN PRTMP_ADAPTER pAd);

VOID NewRadarDetectionStop(
	IN PRTMP_ADAPTER pAd);

void modify_table1(
	IN PRTMP_ADAPTER pAd, 
	IN ULONG idx, 
	IN ULONG value);

void modify_table2(
	IN PRTMP_ADAPTER pAd, 
	IN ULONG idx, 
	IN ULONG value);


 VOID NewTimerCB_Radar(
 	IN PRTMP_ADAPTER pAd);
 
 void schedule_dfs_task(
 	IN PRTMP_ADAPTER pAd);


#endif /* DFS_HARDWARE_SUPPORT */

#endif /* defined (RTMP_RBUS_SUPPORT) || defined(DFS_INTERRUPT_SUPPORT)  */


void MCURadarDetect(PRTMP_ADAPTER pAd);

#ifdef TONE_RADAR_DETECT_SUPPORT
void RTMPHandleRadarInterrupt(PRTMP_ADAPTER  pAd);
#else

#ifdef DFS_HARDWARE_SUPPORT
#if defined (RTMP_RBUS_SUPPORT) || defined(DFS_INTERRUPT_SUPPORT)
void RTMPHandleRadarInterrupt(PRTMP_ADAPTER  pAd);
#endif /* defined (RTMP_RBUS_SUPPORT) || defined(DFS_INTERRUPT_SUPPORT) */
#endif /* DFS_HARDWARE_SUPPORT */
#endif /* TONE_RADAR_DETECT_SUPPORT */

#ifdef TONE_RADAR_DETECT_SUPPORT
INT Set_CarrierCriteria_Proc(IN PRTMP_ADAPTER pAd, IN PSTRING arg);
int Set_CarrierReCheck_Proc(IN PRTMP_ADAPTER pAd, IN PSTRING arg);
INT Set_CarrierStopCheck_Proc(IN PRTMP_ADAPTER pAd, IN PSTRING arg);
void NewCarrierDetectionStart(PRTMP_ADAPTER pAd);
#endif /* TONE_RADAR_DETECT_SUPPORT */

#ifdef DFS_SOFTWARE_SUPPORT
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
#endif /* DFS_SOFTWARE_SUPPORT */

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
#ifdef DFS_SOFTWARE_SUPPORT
VOID RTMPPrepareRadarDetectParams(
	IN PRTMP_ADAPTER	pAd);
#endif /* DFS_SOFTWARE_SUPPORT */

INT Set_ChMovingTime_Proc(
	IN PRTMP_ADAPTER pAd, 
	IN PSTRING arg);

INT Set_LongPulseRadarTh_Proc(
	IN PRTMP_ADAPTER pAd, 
	IN PSTRING arg);


