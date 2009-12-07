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
    ap_uapsd.h

    Abstract:
    Miniport generic portion header file

    Revision History:
    Who         When          What
    --------    ----------    ----------------------------------------------
*/

/* only for UAPSD_TIMING_RECORD */

//#define UAPSD_TIMING_RECORD_FUNC

#define UAPSD_TIMING_RECORD_MAX				1000
#define UAPSD_TIMING_RECORD_DISPLAY_TIMES	10

#define UAPSD_TIMING_RECORD_ISR				1
#define UAPSD_TIMING_RECORD_TASKLET			2
#define UAPSD_TIMING_RECORD_TRG_RCV			3
#define UAPSD_TIMING_RECORD_MOVE2TX			4
#define UAPSD_TIMING_RECORD_TX2AIR			5

#define UAPSD_TIMING_CTRL_STOP				0
#define UAPSD_TIMING_CTRL_START				1
#define UAPSD_TIMING_CTRL_SUSPEND			2

#define UAPSD_TIMESTAMP_GET(__pAd, __TimeStamp)			\
	{													\
		UINT32 __CSR=0;	UINT64 __Value64;				\
		RTMP_IO_READ32((__pAd), TSF_TIMER_DW0, &__CSR);	\
		__TimeStamp = (UINT64)__CSR;					\
		RTMP_IO_READ32((__pAd), TSF_TIMER_DW1, &__CSR);	\
		__Value64 = (UINT64)__CSR;						\
		__TimeStamp |= (__Value64 << 32);				\
	}

#ifdef LINUX
#define UAPSD_TIME_GET(__pAd, __Time)					\
		__Time = jiffies
#endif // LINUX //


#ifdef UAPSD_TIMING_RECORD_FUNC
#define UAPSD_TIMING_RECORD_START()				\
	UAPSD_TimingRecordCtrl(UAPSD_TIMING_CTRL_START);
#define UAPSD_TIMING_RECORD_STOP()				\
	UAPSD_TimingRecordCtrl(UAPSD_TIMING_CTRL_STOP);
#define UAPSD_TIMING_RECORD(__pAd, __Type)		\
	UAPSD_TimingRecord(__pAd, __Type);
#define UAPSD_TIMING_RECORD_INDEX(__LoopIndex)	\
	UAPSD_TimeingRecordLoopIndex(__LoopIndex);
#else

#define UAPSD_TIMING_RECORD_START()
#define UAPSD_TIMING_RECORD_STOP()
#define UAPSD_TIMING_RECORD(__pAd, __type)
#define UAPSD_TIMING_RECORD_INDEX(__LoopIndex)
#endif // UAPSD_TIMING_RECORD_FUNC //


#ifndef MODULE_WMM_UAPSD

#define UAPSD_EXTERN			extern

/* Public Marco list */

/*
	Init some parameters in packet structure for QoS Null frame;
	purpose: is for management frame tx done use
*/
#define UAPSD_MR_QOS_NULL_HANDLE(__pAd, __pData, __pPacket)					\
	{																		\
		PHEADER_802_11 __pHeader = (PHEADER_802_11)(__pData);				\
		MAC_TABLE_ENTRY *__pEntry;											\
		if (__pHeader->FC.SubType == SUBTYPE_QOS_NULL)						\
		{																	\
			RTMP_SET_PACKET_QOS_NULL((__pPacket));							\
			__pEntry = MacTableLookup((__pAd), __pHeader->Addr1);			\
			if (__pEntry != NULL)											\
			{																\
				RTMP_SET_PACKET_WCID((__pPacket), __pEntry->Aid);			\
			}																\
		}																	\
		else																\
		{																	\
			RTMP_SET_PACKET_NON_QOS_NULL((__pPacket));						\
		}																	\
	}

/*
	Init MAC entry UAPSD parameters;
	purpose: initialize UAPSD PS queue and control parameters
*/
#define UAPSD_MR_ENTRY_INIT(__pEntry)										\
	{																		\
		UINT16	__IdAc;														\
		for(__IdAc=0; __IdAc<WMM_NUM_OF_AC; __IdAc++)						\
			InitializeQueueHeader(&(__pEntry)->UAPSDQueue[__IdAc]);			\
		(__pEntry)->UAPSDTxNum = 0;											\
		(__pEntry)->pUAPSDEOSPFrame = NULL;									\
		(__pEntry)->bAPSDFlagSPStart = 0;									\
		(__pEntry)->bAPSDFlagEOSPOK = 0;									\
		(__pEntry)->MaxSPLength = 0;										\
	}

/*
	Reset MAC entry UAPSD parameters;
   purpose: clean all UAPSD PS queue; release the EOSP frame if exists;
			reset control parameters
*/
#define UAPSD_MR_ENTRY_RESET(__pAd, __pEntry)								\
	{																		\
		MAC_TABLE_ENTRY *__pSta;											\
		UINT32 __IdAc;														\
		__pSta = (__pEntry);												\
		/* clear all U-APSD queues */										\
		for(__IdAc=0; __IdAc<WMM_NUM_OF_AC; __IdAc++)						\
			APCleanupPsQueue((__pAd), &__pSta->UAPSDQueue[__IdAc]);		\
		/* clear EOSP frame */												\
		__pSta->UAPSDTxNum = 0;												\
		if (__pSta->pUAPSDEOSPFrame != NULL) {								\
			RELEASE_NDIS_PACKET((__pAd),									\
							QUEUE_ENTRY_TO_PACKET(__pSta->pUAPSDEOSPFrame),	\
							NDIS_STATUS_FAILURE);							\
			__pSta->pUAPSDEOSPFrame = NULL; }								\
		__pSta->bAPSDFlagSPStart = 0;										\
		__pSta->bAPSDFlagEOSPOK = 0; }

/*
	Enable or disable UAPSD flag in WMM element in beacon frame;
	purpose: set UAPSD enable/disable bit
*/
#define UAPSD_MR_IE_FILL(__QosCtrlField, __pAd)								\
		(__QosCtrlField) |= ((__pAd)->CommonCfg.bAPSDCapable) ? 0x80 : 0x00;

/*
	Check if we do NOT need to control TIM bit for the station;
	note: we control TIM bit only when all AC are UAPSD AC
*/
#define UAPSD_MR_IS_NOT_TIM_BIT_NEEDED_HANDLED(__pMacEntry, __QueIdx)		\
		(CLIENT_STATUS_TEST_FLAG((__pMacEntry), fCLIENT_STATUS_APSD_CAPABLE) && \
			(!(__pMacEntry)->bAPSDDeliverEnabledPerAC[QID_AC_VO] ||			\
			!(__pMacEntry)->bAPSDDeliverEnabledPerAC[QID_AC_VI] ||			\
			!(__pMacEntry)->bAPSDDeliverEnabledPerAC[QID_AC_BE] ||			\
			!(__pMacEntry)->bAPSDDeliverEnabledPerAC[QID_AC_BK]) &&			\
		(__pMacEntry)->bAPSDDeliverEnabledPerAC[__QueIdx])

/* check if the AC is UAPSD delivery-enabled AC */
#define UAPSD_MR_IS_UAPSD_AC(__pMacEntry, __AcId)							\
		(CLIENT_STATUS_TEST_FLAG((__pMacEntry), fCLIENT_STATUS_APSD_CAPABLE) &&	\
			((0 <= (__AcId)) && ((__AcId) < WMM_NUM_OF_AC)) && /* 0 ~ 3 */	\
			(__pMacEntry)->bAPSDDeliverEnabledPerAC[(__AcId)])

/* check if all AC are UAPSD delivery-enabled AC */
#define UAPSD_MR_IS_ALL_AC_UAPSD(__FlgIsActive, __pMacEntry)				\
		(((__FlgIsActive) == FALSE) && ((__pMacEntry)->bAPSDAllAC == 1))

/* suspend SP */
#define UAPSD_MR_SP_SUSPEND(__pAd)											\
		(__pAd)->bAPSDFlagSPSuspend = 1;

/* resume SP */
#define UAPSD_MR_SP_RESUME(__pAd)											\
		(__pAd)->bAPSDFlagSPSuspend = 0;

/* mark PS poll frame sent in mix mode */
#ifdef RTMP_MAC_PCI
/*
	Note:
	(1) When SP is not started, try to mark a flag to record if the legacy ps
		packet is handled in statistics handler;
	(2) When SP is started, increase the UAPSD count number for the legacy PS.
*/
#define UAPSD_MR_MIX_PS_POLL_RCV(__pAd, __pMacEntry)						\
		if ((__pMacEntry)->bAPSDFlagSpRoughUse == 0)						\
		{																	\
			if ((__pMacEntry)->bAPSDFlagSPStart == 0)						\
			{																\
				if ((__pMacEntry)->bAPSDFlagLegacySent == 1)				\
					NICUpdateFifoStaCounters((__pAd));						\
				(__pMacEntry)->bAPSDFlagLegacySent = 1;						\
			}																\
			else															\
			{																\
				(__pMacEntry)->UAPSDTxNum ++;								\
			}																\
		}
#endif // RTMP_MAC_PCI //


#else

#define UAPSD_EXTERN
#define UAPSD_QOS_NULL_QUE_ID	0x7f

#ifdef RTMP_MAC_PCI
/*
	In RT2870, FIFO counter is for all stations, not for per-entry,
	so we can not use accurate method in RT2870
*/

/*
	Note for SP ACCURATE Mechanism:
	1. When traffic is busy for the PS station
		Statistics FIFO counter maybe overflow before we read it, so UAPSD
		counting mechanism will not accurately.

		Solution:
		We need to avoid the worse case so we suggest a maximum interval for
		a SP that the interval between last frame from QAP and data frame from
		QSTA is larger than UAPSD_EPT_SP_INT.

	2. When traffic use CCK/1Mbps from QAP
		Statistics FIFO will not count the packet. There are 2 cases:
		(1) We force to downgrage ARP response & DHCP packet to 1Mbps;
		(2) After rate switch mechanism, tx rate is fixed to 1Mbps.

		Solution:
		Use old DMA UAPSD mechanism.

	3. When part of AC uses legacy PS mode
		Statistics count will inclue packet statistics for legacy PS packets
		so we can not know which one is UAPSD, which one is legacy.

		Solution:
		Cound the legacy PS packet.

	4. Check FIFO statistics count in Rx Done function
		We can not to check TX FIFO statistics count in Rx Done function or
		the real packet tx/rx sequence will be disarranged.

		Solution:
		Suspend SP handle before rx done and resume SP handle after rx done.
*/
#define UAPSD_SP_ACCURATE		/* use more accurate method to send EOSP */
#endif // RTMP_MAC_PCI //

#define UAPSD_EPT_SP_INT		(100000/(1000000/OS_HZ)) /* 100ms */

#endif // MODULE_WMM_UAPSD //


/* max UAPSD buffer queue size */
#define MAX_PACKETS_IN_UAPSD_QUEUE	16	/* for each AC = 16*4 = 64 */


/* Public function list */
/*
========================================================================
Routine Description:
	UAPSD Module Init.

Arguments:
	pAd		Pointer to our adapter

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_Init(
	IN	PRTMP_ADAPTER		pAd);


/*
========================================================================
Routine Description:
	UAPSD Module Release.

Arguments:
	pAd		Pointer to our adapter

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_Release(
	IN	PRTMP_ADAPTER		pAd);


/*
========================================================================
Routine Description:
	Free all EOSP frames and close all SP.

Arguments:
	pAd		Pointer to our adapter

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_FreeAll(
	IN	PRTMP_ADAPTER		pAd);


/*
========================================================================
Routine Description:
	Close current Service Period.

Arguments:
	pAd				Pointer to our adapter
	pEntry			Close the SP of the entry

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_SP_Close(
    IN  PRTMP_ADAPTER       pAd,
	IN	MAC_TABLE_ENTRY		*pEntry);


/*
========================================================================
Routine Description:
	Deliver all queued packets.

Arguments:
	pAd            Pointer to our adapter
	*pEntry        STATION

Return Value:
	None

Note:
	SMP protection by caller for packet enqueue.
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_AllPacketDeliver(
	IN	PRTMP_ADAPTER		pAd,
	IN	MAC_TABLE_ENTRY		*pEntry);


/*
========================================================================
Routine Description:
	Parse the UAPSD field in WMM element in (re)association request frame.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION
	*pElm			QoS information field

Return Value:
	None

Note:
	No protection is needed.

	1. Association -> TSPEC:
		use static UAPSD settings in Association
		update UAPSD settings in TSPEC

	2. Association -> TSPEC(11r) -> Reassociation:
		update UAPSD settings in TSPEC
		backup static UAPSD settings in Reassociation

	3. Association -> Reassociation:
		update UAPSD settings in TSPEC
		backup static UAPSD settings in Reassociation
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_AssocParse(
	IN	PRTMP_ADAPTER		pAd,
	IN	MAC_TABLE_ENTRY		*pEntry,
	IN	UCHAR				*pElm);


/*
========================================================================
Routine Description:
	Enqueue a UAPSD packet.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION
	pPacket			UAPSD dnlink packet
	IdAc			UAPSD AC ID (0 ~ 3)

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_PacketEnqueue(
	IN	PRTMP_ADAPTER		pAd,
	IN	MAC_TABLE_ENTRY		*pEntry,
	IN	PNDIS_PACKET		pPacket,
	IN	UINT32				IdAc);


/*
========================================================================
Routine Description:
	Handle QoS Null Frame Tx Done or Management Tx Done interrupt.

Arguments:
	pAd				Pointer to our adapter
	pPacket			Completed TX packet
	pDstMac			Destinated MAC address

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_QoSNullTxMgmtTxDoneHandle(
	IN	PRTMP_ADAPTER		pAd,
	IN	PNDIS_PACKET		pPacket,
	IN	UCHAR				*pDstMac);


/*
========================================================================
Routine Description:
	Maintenance our UAPSD PS queue.  Release all queued packet if timeout.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			STATION

Return Value:
	None

Note:
	If in RT2870, pEntry can not be removed during UAPSD_QueueMaintenance()
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_QueueMaintenance(
	IN	PRTMP_ADAPTER		pAd,
	IN	MAC_TABLE_ENTRY		*pEntry);


/*
========================================================================
Routine Description:
	Close SP in Tx Done, not Tx DMA Done.

Arguments:
	pAd            Pointer to our adapter
	pEntry			destination entry
	FlgSuccess		0:tx success, 1:tx fail

Return Value:
    None

Note:
	For RT28xx series, for packetID=0 or multicast frame, no statistics
	count can be got, ex: ARP response or DHCP packets, we will use
	low rate to set (CCK, MCS=0=packetID).
	So SP will not be close until UAPSD_EPT_SP_INT timeout.

	So if the tx rate is 1Mbps for a entry, we will use DMA done, not
	use UAPSD_SP_AUE_Handle().
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_SP_AUE_Handle(
	IN RTMP_ADAPTER		*pAd,
    IN MAC_TABLE_ENTRY	*pEntry,
	IN UCHAR			FlgSuccess);


/*
========================================================================
Routine Description:
	Close current Service Period.

Arguments:
	pAd				Pointer to our adapter

Return Value:
	None

Note:
	When we receive EOSP frame tx done interrupt and a uplink packet
	from the station simultaneously, we will regard it as a new trigger
	frame because the packet is received when EOSP frame tx done interrupt.

	We can not sure the uplink packet is sent after old SP or in the old SP.
	So we must close the old SP in receive done ISR to avoid the problem.
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_SP_CloseInRVDone(
	IN	PRTMP_ADAPTER		pAd);


/*
========================================================================
Routine Description:
	Check if we need to close current SP.

Arguments:
	pAd				Pointer to our adapter
	pPacket			Completed TX packet
	pDstMac			Destinated MAC address

Return Value:
	None

Note:
	1. We need to call the function in TxDone ISR.
	2. SMP protection by caller for packet enqueue.
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_SP_PacketCheck(
	IN	PRTMP_ADAPTER		pAd,
	IN	PNDIS_PACKET		pPacket,
	IN	UCHAR				*pDstMac);


#ifdef UAPSD_TIMING_RECORD_FUNC
/*
========================================================================
Routine Description:
	Enable/Disable Timing Record Function.

Arguments:
	pAd				Pointer to our adapter
	Flag			1 (Enable) or 0 (Disable)

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_TimingRecordCtrl(
	IN	UINT32				Flag);

/*
========================================================================
Routine Description:
	Record some timings.

Arguments:
	pAd				Pointer to our adapter
	Type			The timing is for what type

Return Value:
	None

Note:
	UAPSD_TIMING_RECORD_ISR
	UAPSD_TIMING_RECORD_TASKLET
	UAPSD_TIMING_RECORD_TRG_RCV
	UAPSD_TIMING_RECORD_MOVE2TX
	UAPSD_TIMING_RECORD_TX2AIR
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_TimingRecord(
	IN	PRTMP_ADAPTER		pAd,
	IN	UINT32				Type);

/*
========================================================================
Routine Description:
	Record the loop index for received packet handle.

Arguments:
	pAd				Pointer to our adapter
	LoopIndex		The RxProcessed in APRxDoneInterruptHandle()

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_TimeingRecordLoopIndex(
	IN	UINT32				LoopIndex);
#endif // UAPSD_TIMING_RECORD_FUNC //


/*
========================================================================
Routine Description:
	Handle UAPSD Trigger Frame.

Arguments:
	pAd				Pointer to our adapter
	*pEntry			the source STATION
	UpOfFrame		the UP of the trigger frame

Return Value:
	None

Note:
========================================================================
*/
UAPSD_EXTERN VOID UAPSD_TriggerFrameHandle(
	IN	PRTMP_ADAPTER		pAd,
	IN	MAC_TABLE_ENTRY		*pEntry,
	IN	UCHAR				UpOfFrame);



/* End of ap_uapsd.h */
