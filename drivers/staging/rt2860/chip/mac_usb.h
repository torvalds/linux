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
        mac_usb.h

    Abstract:

    Revision History:
    Who          When          What
    ---------    ----------    ----------------------------------------------
 */

#ifndef __MAC_USB_H__
#define __MAC_USB_H__

#include "../rtmp_type.h"
#include "rtmp_mac.h"
#include "rtmp_phy.h"
#include "../rtmp_iface.h"
#include "../rtmp_dot11.h"

#define USB_CYC_CFG				0x02a4

#define BEACON_RING_SIZE		2
#define MGMTPIPEIDX				0	/* EP6 is highest priority */

#define RTMP_PKT_TAIL_PADDING	11	/* 3(max 4 byte padding) + 4 (last packet padding) + 4 (MaxBulkOutsize align padding) */

#define fRTMP_ADAPTER_NEED_STOP_TX		\
		(fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS |	\
		 fRTMP_ADAPTER_RESET_IN_PROGRESS | fRTMP_ADAPTER_BULKOUT_RESET | \
		 fRTMP_ADAPTER_RADIO_OFF | fRTMP_ADAPTER_REMOVE_IN_PROGRESS)

/* */
/* RXINFO appends at the end of each rx packet. */
/* */
#define RXINFO_SIZE				4
#define RT2870_RXDMALEN_FIELD_SIZE	4

typedef struct PACKED rt_rxinfo {
	u32 BA:1;
	u32 DATA:1;
	u32 NULLDATA:1;
	u32 FRAG:1;
	u32 U2M:1;		/* 1: this RX frame is unicast to me */
	u32 Mcast:1;		/* 1: this is a multicast frame */
	u32 Bcast:1;		/* 1: this is a broadcast frame */
	u32 MyBss:1;		/* 1: this frame belongs to the same BSSID */
	u32 Crc:1;		/* 1: CRC error */
	u32 CipherErr:2;	/* 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid */
	u32 AMSDU:1;		/* rx with 802.3 header, not 802.11 header. */
	u32 HTC:1;
	u32 RSSI:1;
	u32 L2PAD:1;
	u32 AMPDU:1;		/* To be moved */
	u32 Decrypted:1;
	u32 PlcpRssil:1;
	u32 CipherAlg:1;
	u32 LastAMSDU:1;
	u32 PlcpSignal:12;
} RT28XX_RXD_STRUC, *PRT28XX_RXD_STRUC;

/* */
/* TXINFO */
/* */
#define TXINFO_SIZE				4

struct rt_txinfo {
	/* Word 0 */
	u32 USBDMATxPktLen:16;	/*used ONLY in USB bulk Aggregation,  Total byte counts of all sub-frame. */
	u32 rsv:8;
	u32 WIV:1;		/* Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition */
	u32 QSEL:2;		/* select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA */
	u32 SwUseLastRound:1;	/* Software use. */
	u32 rsv2:2;		/* Software use. */
	u32 USBDMANextVLD:1;	/*used ONLY in USB bulk Aggregation, NextValid */
	u32 USBDMATxburst:1;	/*used ONLY in USB bulk Aggre. Force USB DMA transmit frame from current selected endpoint */
};

/* */
/* Management ring buffer format */
/* */
struct rt_mgmt {
	BOOLEAN Valid;
	u8 *pBuffer;
	unsigned long Length;
};

/*////////////////////////////////////////////////////////////////////////// */
/* The struct rt_tx_buffer structure forms the transmitted USB packet to the device */
/*////////////////////////////////////////////////////////////////////////// */
struct rt_tx_buffer {
	union {
		u8 WirelessPacket[TX_BUFFER_NORMSIZE];
		struct rt_header_802_11 NullFrame;
		struct rt_pspoll_frame PsPollPacket;
		struct rt_rts_frame RTSFrame;
	} field;
	u8 Aggregation[4];	/*Buffer for save Aggregation size. */
};

struct rt_httx_buffer {
	union {
		u8 WirelessPacket[MAX_TXBULK_SIZE];
		struct rt_header_802_11 NullFrame;
		struct rt_pspoll_frame PsPollPacket;
		struct rt_rts_frame RTSFrame;
	} field;
	u8 Aggregation[4];	/*Buffer for save Aggregation size. */
};

/* used to track driver-generated write irps */
struct rt_tx_context {
	void *pAd;		/*Initialized in MiniportInitialize */
	PURB pUrb;		/*Initialized in MiniportInitialize */
	PIRP pIrp;		/*used to cancel pending bulk out. */
	/*Initialized in MiniportInitialize */
	struct rt_tx_buffer *TransferBuffer;	/*Initialized in MiniportInitialize */
	unsigned long BulkOutSize;
	u8 BulkOutPipeId;
	u8 SelfIdx;
	BOOLEAN InUse;
	BOOLEAN bWaitingBulkOut;	/* at least one packet is in this TxContext, ready for making IRP anytime. */
	BOOLEAN bFullForBulkOut;	/* all tx buffer are full , so waiting for tx bulkout. */
	BOOLEAN IRPPending;
	BOOLEAN LastOne;
	BOOLEAN bAggregatible;
	u8 Header_802_3[LENGTH_802_3];
	u8 Rsv[2];
	unsigned long DataOffset;
	u32 TxRate;
	dma_addr_t data_dma;	/* urb dma on linux */

};

/* used to track driver-generated write irps */
struct rt_ht_tx_context {
	void *pAd;		/*Initialized in MiniportInitialize */
	PURB pUrb;		/*Initialized in MiniportInitialize */
	PIRP pIrp;		/*used to cancel pending bulk out. */
	/*Initialized in MiniportInitialize */
	struct rt_httx_buffer *TransferBuffer;	/*Initialized in MiniportInitialize */
	unsigned long BulkOutSize;	/* Indicate the total bulk-out size in bytes in one bulk-transmission */
	u8 BulkOutPipeId;
	BOOLEAN IRPPending;
	BOOLEAN LastOne;
	BOOLEAN bCurWriting;
	BOOLEAN bRingEmpty;
	BOOLEAN bCopySavePad;
	u8 SavedPad[8];
	u8 Header_802_3[LENGTH_802_3];
	unsigned long CurWritePosition;	/* Indicate the buffer offset which packet will be inserted start from. */
	unsigned long CurWriteRealPos;	/* Indicate the buffer offset which packet now are writing to. */
	unsigned long NextBulkOutPosition;	/* Indicate the buffer start offset of a bulk-transmission */
	unsigned long ENextBulkOutPosition;	/* Indicate the buffer end offset of a bulk-transmission */
	u32 TxRate;
	dma_addr_t data_dma;	/* urb dma on linux */
};

/* */
/* Structure to keep track of receive packets and buffers to indicate */
/* receive data to the protocol. */
/* */
struct rt_rx_context {
	u8 *TransferBuffer;
	void *pAd;
	PIRP pIrp;		/*used to cancel pending bulk in. */
	PURB pUrb;
	/*These 2 Boolean shouldn't both be 1 at the same time. */
	unsigned long BulkInOffset;	/* number of packets waiting for reordering . */
/*      BOOLEAN                         ReorderInUse;   // At least one packet in this buffer are in reordering buffer and wait for receive indication */
	BOOLEAN bRxHandling;	/* Notify this packet is being process now. */
	BOOLEAN InUse;		/* USB Hardware Occupied. Wait for USB HW to put packet. */
	BOOLEAN Readable;	/* Receive Complete back. OK for driver to indicate receiving packet. */
	BOOLEAN IRPPending;	/* TODO: To be removed */
	atomic_t IrpLock;
	spinlock_t RxContextLock;
	dma_addr_t data_dma;	/* urb dma on linux */
};

/******************************************************************************

	USB Frimware Related MACRO

******************************************************************************/
/* 8051 firmware image for usb - use last-half base address = 0x3000 */
#define FIRMWARE_IMAGE_BASE			0x3000
#define MAX_FIRMWARE_IMAGE_SIZE		0x1000	/* 4kbyte */

#define RTMP_WRITE_FIRMWARE(_pAd, _pFwImage, _FwLen)		\
	RTUSBFirmwareWrite(_pAd, _pFwImage, _FwLen)

/******************************************************************************

	USB TX Related MACRO

******************************************************************************/
#define RTMP_START_DEQUEUE(pAd, QueIdx, irqFlags)				\
			do{													\
				RTMP_IRQ_LOCK(&pAd->DeQueueLock[QueIdx], irqFlags);		\
				if (pAd->DeQueueRunning[QueIdx])						\
				{														\
					RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);\
					DBGPRINT(RT_DEBUG_OFF, ("DeQueueRunning[%d]= TRUE!\n", QueIdx));		\
					continue;											\
				}														\
				else													\
				{														\
					pAd->DeQueueRunning[QueIdx] = TRUE;					\
					RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);\
				}														\
			}while(0)

#define RTMP_STOP_DEQUEUE(pAd, QueIdx, irqFlags)						\
			do{															\
				RTMP_IRQ_LOCK(&pAd->DeQueueLock[QueIdx], irqFlags);		\
				pAd->DeQueueRunning[QueIdx] = FALSE;					\
				RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);	\
			}while(0)

#define	RTMP_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, freeNum, pPacket) \
		(RTUSBFreeDescriptorRequest(pAd, pTxBlk->QueIdx, (pTxBlk->TotalFrameLen + GET_OS_PKT_LEN(pPacket))) == NDIS_STATUS_SUCCESS)

#define RTMP_RELEASE_DESC_RESOURCE(pAd, QueIdx)			\
		do{}while(0)

#define NEED_QUEUE_BACK_FOR_AGG(_pAd, _QueIdx, _freeNum, _TxFrameType)		\
		((_TxFrameType == TX_RALINK_FRAME) && (RTUSBNeedQueueBackForAgg(_pAd, _QueIdx)))

#define HAL_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)	\
			RtmpUSB_WriteSubTxResource(pAd, pTxBlk, bIsLast, pFreeNumber)

#define HAL_WriteTxResource(pAd, pTxBlk,bIsLast, pFreeNumber)	\
			RtmpUSB_WriteSingleTxResource(pAd, pTxBlk,bIsLast, pFreeNumber)

#define HAL_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber) \
			RtmpUSB_WriteFragTxResource(pAd, pTxBlk, fragNum, pFreeNumber)

#define HAL_WriteMultiTxResource(pAd, pTxBlk,frameNum, pFreeNumber)	\
			RtmpUSB_WriteMultiTxResource(pAd, pTxBlk,frameNum, pFreeNumber)

#define HAL_FinalWriteTxResource(pAd, pTxBlk, totalMPDUSize, TxIdx)	\
			RtmpUSB_FinalWriteTxResource(pAd, pTxBlk, totalMPDUSize, TxIdx)

#define HAL_LastTxIdx(pAd, QueIdx,TxIdx) \
				/*RtmpUSBDataLastTxIdx(pAd, QueIdx,TxIdx) */

#define HAL_KickOutTx(pAd, pTxBlk, QueIdx)	\
			RtmpUSBDataKickOut(pAd, pTxBlk, QueIdx)

#define HAL_KickOutMgmtTx(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen)	\
			RtmpUSBMgmtKickOut(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen)

#define HAL_KickOutNullFrameTx(_pAd, _QueIdx, _pNullFrame, _frameLen)	\
			RtmpUSBNullFrameKickOut(_pAd, _QueIdx, _pNullFrame, _frameLen)

#define GET_TXRING_FREENO(_pAd, _QueIdx)	(_QueIdx)	/*(_pAd->TxRing[_QueIdx].TxSwFreeIdx) */
#define GET_MGMTRING_FREENO(_pAd)			(_pAd->MgmtRing.TxSwFreeIdx)

/* ----------------- RX Related MACRO ----------------- */

/*
  *	Device Hardware Interface Related MACRO
  */
#define RTMP_IRQ_INIT(pAd)				do{}while(0)
#define RTMP_IRQ_ENABLE(pAd)			do{}while(0)

/*
  *	MLME Related MACRO
  */
#define RTMP_MLME_HANDLER(pAd)			RTUSBMlmeUp(pAd)

#define RTMP_MLME_PRE_SANITY_CHECK(pAd)								\
	{	if ((pAd->CommonCfg.bHardwareRadio == TRUE) &&					\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&		\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))) {	\
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_CHECK_GPIO, NULL, 0); } }

#define RTMP_MLME_STA_QUICK_RSP_WAKE_UP(pAd)	\
	{	RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_QKERIODIC_EXECUT, NULL, 0);	\
		RTUSBMlmeUp(pAd); }

#define RTMP_MLME_RESET_STATE_MACHINE(pAd)	\
		        MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_RESET_CONF, 0, NULL);	\
		        RTUSBMlmeUp(pAd);

#define RTMP_HANDLE_COUNTER_MEASURE(_pAd, _pEntry)		\
	{	RTUSBEnqueueInternalCmd(_pAd, CMDTHREAD_802_11_COUNTER_MEASURE, _pEntry, sizeof(struct rt_mac_table_entry));	\
		RTUSBMlmeUp(_pAd);									\
	}

/*
  *	Power Save Related MACRO
  */
#define RTMP_PS_POLL_ENQUEUE(pAd)						\
	{	RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_PSPOLL);	\
		RTUSBKickBulkOut(pAd); }

#define RTMP_STA_FORCE_WAKEUP(_pAd, bFromTx) \
	RT28xxUsbStaAsicForceWakeup(_pAd, bFromTx);

#define RTMP_STA_SLEEP_THEN_AUTO_WAKEUP(pAd, TbttNumToNextWakeUp) \
    RT28xxUsbStaAsicSleepThenAutoWakeup(pAd, TbttNumToNextWakeUp);

#define RTMP_SET_PSM_BIT(_pAd, _val) \
	{\
		if ((_pAd)->StaCfg.WindowsPowerMode == Ndis802_11PowerModeFast_PSP) \
			MlmeSetPsmBit(_pAd, _val);\
		else \
		{ \
			u16 _psm_val; \
			_psm_val = _val; \
			RTUSBEnqueueInternalCmd(_pAd, CMDTHREAD_SET_PSM_BIT, &(_psm_val), sizeof(u16)); \
		}\
	}

#define RTMP_MLME_RADIO_ON(pAd) \
    RT28xxUsbMlmeRadioOn(pAd);

#define RTMP_MLME_RADIO_OFF(pAd) \
    RT28xxUsbMlmeRadioOFF(pAd);

#endif /*__MAC_USB_H__ // */
