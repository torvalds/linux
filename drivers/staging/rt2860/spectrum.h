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
*/

#ifndef __SPECTRUM_H__
#define __SPECTRUM_H__

#include "rtmp_type.h"
#include "spectrum_def.h"

typedef struct PACKED _TPC_REPORT_INFO
{
	UINT8 TxPwr;
	UINT8 LinkMargin;
} TPC_REPORT_INFO, *PTPC_REPORT_INFO;

typedef struct PACKED _CH_SW_ANN_INFO
{
	UINT8 ChSwMode;
	UINT8 Channel;
	UINT8 ChSwCnt;
} CH_SW_ANN_INFO, *PCH_SW_ANN_INFO;

typedef union PACKED _MEASURE_REQ_MODE
{
#ifdef RT_BIG_ENDIAN
	struct PACKED
	{
		UINT8 Rev1:4;
		UINT8 Report:1;
		UINT8 Request:1;
		UINT8 Enable:1;
		UINT8 Rev0:1;
	} field;
#else
	struct PACKED
	{
		UINT8 Rev0:1;
		UINT8 Enable:1;
		UINT8 Request:1;
		UINT8 Report:1;
		UINT8 Rev1:4;
	} field;
#endif // RT_BIG_ENDIAN //
	UINT8 word;
} MEASURE_REQ_MODE, *PMEASURE_REQ_MODE;

typedef struct PACKED _MEASURE_REQ
{
	UINT8 ChNum;
	UINT64 MeasureStartTime;
	UINT16 MeasureDuration;
} MEASURE_REQ, *PMEASURE_REQ;

typedef struct PACKED _MEASURE_REQ_INFO
{
	UINT8 Token;
	MEASURE_REQ_MODE ReqMode;
	UINT8 ReqType;
	MEASURE_REQ MeasureReq;
} MEASURE_REQ_INFO, *PMEASURE_REQ_INFO;

typedef union PACKED _MEASURE_BASIC_REPORT_MAP
{
#ifdef RT_BIG_ENDIAN
	struct PACKED
	{
		UINT8 Rev:3;
		UINT8 Unmeasure:1;
		UINT8 Radar:1;
		UINT8 UnidentifiedSignal:1;
		UINT8 OfdmPreamble:1;
		UINT8 BSS:1;
	} field;
#else
	struct PACKED
	{
		UINT8 BSS:1;
		UINT8 OfdmPreamble:1;
		UINT8 UnidentifiedSignal:1;
		UINT8 Radar:1;
		UINT8 Unmeasure:1;
		UINT8 Rev:3;
	} field;
#endif // RT_BIG_ENDIAN //
	UINT8 word;
} MEASURE_BASIC_REPORT_MAP, *PMEASURE_BASIC_REPORT_MAP;

typedef struct PACKED _MEASURE_BASIC_REPORT
{
	UINT8 ChNum;
	UINT64 MeasureStartTime;
	UINT16 MeasureDuration;
	MEASURE_BASIC_REPORT_MAP Map;
} MEASURE_BASIC_REPORT, *PMEASURE_BASIC_REPORT;

typedef struct PACKED _MEASURE_CCA_REPORT
{
	UINT8 ChNum;
	UINT64 MeasureStartTime;
	UINT16 MeasureDuration;
	UINT8 CCA_Busy_Fraction;
} MEASURE_CCA_REPORT, *PMEASURE_CCA_REPORT;

typedef struct PACKED _MEASURE_RPI_REPORT
{
	UINT8 ChNum;
	UINT64 MeasureStartTime;
	UINT16 MeasureDuration;
	UINT8 RPI_Density[8];
} MEASURE_RPI_REPORT, *PMEASURE_RPI_REPORT;

typedef union PACKED _MEASURE_REPORT_MODE
{
	struct PACKED
	{
#ifdef RT_BIG_ENDIAN
		UINT8 Rev:5;
		UINT8 Refused:1;
		UINT8 Incapable:1;
		UINT8 Late:1;
#else
		UINT8 Late:1;
		UINT8 Incapable:1;
		UINT8 Refused:1;
		UINT8 Rev:5;
#endif // RT_BIG_ENDIAN //
	} field;
	UINT8 word;
} MEASURE_REPORT_MODE, *PMEASURE_REPORT_MODE;

typedef struct PACKED _MEASURE_REPORT_INFO
{
	UINT8 Token;
	MEASURE_REPORT_MODE ReportMode;
	UINT8 ReportType;
	UINT8 Octect[0];
} MEASURE_REPORT_INFO, *PMEASURE_REPORT_INFO;

typedef struct PACKED _QUIET_INFO
{
	UINT8 QuietCnt;
	UINT8 QuietPeriod;
	UINT8 QuietDuration;
	UINT8 QuietOffset;
} QUIET_INFO, *PQUIET_INFO;

/*
	==========================================================================
	Description:
		Prepare Measurement request action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
VOID EnqueueMeasurementReq(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDA,
	IN UINT8 MeasureToken,
	IN UINT8 MeasureReqMode,
	IN UINT8 MeasureReqType,
	IN UINT8 MeasureCh,
	IN UINT16 MeasureDuration);

/*
	==========================================================================
	Description:
		Prepare Measurement report action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
VOID EnqueueMeasurementRep(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDA,
	IN UINT8 DialogToken,
	IN UINT8 MeasureToken,
	IN UINT8 MeasureReqMode,
	IN UINT8 MeasureReqType,
	IN UINT8 ReportInfoLen,
	IN PUINT8 pReportInfo);

/*
	==========================================================================
	Description:
		Prepare TPC Request action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
VOID EnqueueTPCReq(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDA,
	IN UCHAR DialogToken);

/*
	==========================================================================
	Description:
		Prepare TPC Report action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.

	Return	: None.
	==========================================================================
 */
VOID EnqueueTPCRep(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDA,
	IN UINT8 DialogToken,
	IN UINT8 TxPwr,
	IN UINT8 LinkMargin);

/*
	==========================================================================
	Description:
		Prepare Channel Switch Announcement action frame and enqueue it into
		management queue waiting for transmition.

	Parametrs:
		1. the destination mac address of the frame.
		2. Channel switch announcement mode.
		2. a New selected channel.

	Return	: None.
	==========================================================================
 */
VOID EnqueueChSwAnn(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pDA,
	IN UINT8 ChSwMode,
	IN UINT8 NewCh);

/*
	==========================================================================
	Description:
		Spectrun action frames Handler such as channel switch annoucement,
		measurement report, measurement request actions frames.

	Parametrs:
		Elme - MLME message containing the received frame

	Return	: None.
	==========================================================================
 */
VOID PeerSpectrumAction(
    IN PRTMP_ADAPTER pAd,
    IN MLME_QUEUE_ELEM *Elem);

/*
	==========================================================================
	Description:

	Parametrs:

	Return	: None.
	==========================================================================
 */
INT Set_MeasureReq_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg);

INT Set_TpcReq_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PUCHAR			arg);

VOID MeasureReqTabInit(
	IN PRTMP_ADAPTER pAd);

VOID MeasureReqTabExit(
	IN PRTMP_ADAPTER pAd);

VOID TpcReqTabInit(
	IN PRTMP_ADAPTER pAd);

VOID TpcReqTabExit(
	IN PRTMP_ADAPTER pAd);

VOID NotifyChSwAnnToPeerAPs(
	IN PRTMP_ADAPTER pAd,
	IN PUCHAR pRA,
	IN PUCHAR pTA,
	IN UINT8 ChSwMode,
	IN UINT8 Channel);
#endif // __SPECTRUM_H__ //

