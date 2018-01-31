/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HAL_TXBF_INTERFACE_H__
#define __HAL_TXBF_INTERFACE_H__

#if (BEAMFORMING_SUPPORT == 1)
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID
Beamforming_GidPAid(
	PADAPTER	Adapter,
	PRT_TCB		pTcb
	);

RT_STATUS
Beamforming_GetReportFrame(
	IN	PADAPTER		Adapter,
	IN	PRT_RFD			pRfd,
	IN	POCTET_STRING	pPduOS
	);

VOID
Beamforming_GetNDPAFrame(
	IN	PVOID			pDM_VOID,
	IN	OCTET_STRING	pduOS
	);

BOOLEAN
SendFWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendFWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendSWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendSWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	);

#ifdef SUPPORT_MU_BF
#if (SUPPORT_MU_BF == 1)
RT_STATUS
Beamforming_GetVHTGIDMgntFrame(
	IN	PADAPTER		Adapter,
	IN	PRT_RFD			pRfd,
	IN	POCTET_STRING	pPduOS
	);

BOOLEAN
SendSWVHTGIDMgntFrame(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u1Byte			Idx
	);

BOOLEAN
SendSWVHTBFReportPoll(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	BOOLEAN			bFinalPoll
	);

BOOLEAN
SendSWVHTMUNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	CHANNEL_WIDTH	BW
	);
#else
#define Beamforming_GetVHTGIDMgntFrame(Adapter, pRfd, pPduOS) RT_STATUS_FAILURE
#define SendSWVHTGIDMgntFrame(pDM_VOID, RA)
#define SendSWVHTBFReportPoll(pDM_VOID, RA, bFinalPoll)
#define SendSWVHTMUNDPAPacket(pDM_VOID, BW)
#endif
#endif


#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

u4Byte
Beamforming_GetReportFrame(
	IN	PVOID			pDM_VOID,
	union recv_frame *precv_frame
	);

BOOLEAN
SendFWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendSWHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendFWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	);

BOOLEAN
SendSWVHTNDPAPacket(
	IN	PVOID			pDM_VOID,
	IN	pu1Byte			RA,
	IN	u2Byte			AID,
	IN	CHANNEL_WIDTH	BW
	);
#endif

VOID
Beamforming_GetNDPAFrame(
	IN	PVOID			pDM_VOID,
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	IN	OCTET_STRING	pduOS
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	union recv_frame *precv_frame
#endif
);

#else
#define Beamforming_GetNDPAFrame(pDM_Odm, _PduOS)
#if (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define Beamforming_GetReportFrame(Adapter, precv_frame)		RT_STATUS_FAILURE
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define Beamforming_GetReportFrame(Adapter, pRfd, pPduOS)		RT_STATUS_FAILURE
#define Beamforming_GetVHTGIDMgntFrame(Adapter, pRfd, pPduOS) RT_STATUS_FAILURE
#endif
#define SendFWHTNDPAPacket(pDM_VOID, RA, BW)
#define SendSWHTNDPAPacket(pDM_VOID, RA, BW)
#define SendFWVHTNDPAPacket(pDM_VOID, RA, AID, BW)
#define SendSWVHTNDPAPacket(pDM_VOID, RA,	AID, BW)
#define SendSWVHTGIDMgntFrame(pDM_VOID, RA, idx)
#define SendSWVHTBFReportPoll(pDM_VOID, RA, bFinalPoll)
#define SendSWVHTMUNDPAPacket(pDM_VOID, BW)
#endif

#endif
