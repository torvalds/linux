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


CHAR RTMP_GetTxPwr(
	IN PRTMP_ADAPTER pAd,
	IN HTTRANSMIT_SETTING HTTxMode);

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
VOID MakeMeasurementReqFrame(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pOutBuffer,
	OUT PULONG pFrameLen,
	IN UINT8 TotalLen,
	IN UINT8 Category,
	IN UINT8 Action,
	IN UINT8 MeasureToken,
	IN UINT8 MeasureReqMode,
	IN UINT8 MeasureReqType,
	IN UINT8 NumOfRepetitions);

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
	IN	PSTRING			arg);

INT Set_TpcReq_Proc(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);

INT Set_PwrConstraint(
	IN	PRTMP_ADAPTER	pAd,
	IN	PSTRING			arg);


VOID MeasureReqTabInit(
	IN PRTMP_ADAPTER pAd);

VOID MeasureReqTabExit(
	IN PRTMP_ADAPTER pAd);

PMEASURE_REQ_ENTRY MeasureReqLookUp(
	IN PRTMP_ADAPTER	pAd,
	IN UINT8			DialogToken);

PMEASURE_REQ_ENTRY MeasureReqInsert(
	IN PRTMP_ADAPTER	pAd,
	IN UINT8			DialogToken);

VOID MeasureReqDelete(
	IN PRTMP_ADAPTER	pAd,
	IN UINT8			DialogToken);

VOID InsertChannelRepIE(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pFrameBuf,
	OUT PULONG pFrameLen,
	IN PSTRING pCountry,
	IN UINT8 RegulatoryClass);

VOID InsertTpcReportIE(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pFrameBuf,
	OUT PULONG pFrameLen,
	IN UINT8 TxPwr,
	IN UINT8 LinkMargin);

VOID InsertDialogToken(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pFrameBuf,
	OUT PULONG pFrameLen,
	IN UINT8 DialogToken);

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

VOID RguClass_BuildBcnChList(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pBuf,
	OUT	PULONG pBufLen);
#endif // __SPECTRUM_H__ //
