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


#ifndef __MAC_USB_H__
#define __MAC_USB_H__

#include "rtmp_type.h"
#include "chip/rtmp_mac.h"
#include "chip/rtmp_phy.h"
#include "rtmp_iface.h"
#include "rtmp_dot11.h"


#define USB_CYC_CFG				0x02a4

/*#define BEACON_RING_SIZE		2 */
#define MGMTPIPEIDX				0	/* EP6 is highest priority */

/* os abl move */
/*#define RTMP_PKT_TAIL_PADDING 	11 // 3(max 4 byte padding) + 4 (last packet padding) + 4 (MaxBulkOutsize align padding) */

#define fRTMP_ADAPTER_NEED_STOP_TX		\
		(fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS |	\
		 fRTMP_ADAPTER_RESET_IN_PROGRESS | fRTMP_ADAPTER_BULKOUT_RESET | \
		 fRTMP_ADAPTER_RADIO_OFF | fRTMP_ADAPTER_REMOVE_IN_PROGRESS)

/* */
/* RXINFO appends at the end of each rx packet. */
/* */
#define RXINFO_SIZE				4
#define RT2870_RXDMALEN_FIELD_SIZE	4

#ifdef RT_BIG_ENDIAN
typedef	struct	GNU_PACKED _RXINFO_STRUC {
	UINT32		PlcpSignal:12;
	UINT32		LastAMSDU:1;
	UINT32		CipherAlg:1;
	UINT32		PlcpRssil:1;
	UINT32		Decrypted:1;
	UINT32		AMPDU:1;		/* To be moved */
	UINT32		L2PAD:1;
	UINT32		RSSI:1;
	UINT32		HTC:1;
	UINT32		AMSDU:1;		/* rx with 802.3 header, not 802.11 header. */
	UINT32		CipherErr:2;        /* 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid */
	UINT32		Crc:1;              /* 1: CRC error */
	UINT32		MyBss:1;  	/* 1: this frame belongs to the same BSSID */
	UINT32		Bcast:1;            /* 1: this is a broadcast frame */
	UINT32		Mcast:1;            /* 1: this is a multicast frame */
	UINT32		U2M:1;              /* 1: this RX frame is unicast to me */
	UINT32		FRAG:1;
	UINT32		NULLDATA:1;
	UINT32		DATA:1;
	UINT32		BA:1;
}	RXINFO_STRUC, *PRXINFO_STRUC, RT28XX_RXD_STRUC, *PRT28XX_RXD_STRUC;
#else
typedef	struct	GNU_PACKED _RXINFO_STRUC {
	UINT32		BA:1;
	UINT32		DATA:1;
	UINT32		NULLDATA:1;
	UINT32		FRAG:1;
	UINT32		U2M:1;              /* 1: this RX frame is unicast to me */
	UINT32		Mcast:1;            /* 1: this is a multicast frame */
	UINT32		Bcast:1;            /* 1: this is a broadcast frame */
	UINT32		MyBss:1;  	/* 1: this frame belongs to the same BSSID */
	UINT32		Crc:1;              /* 1: CRC error */
	UINT32		CipherErr:2;        /* 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid */
	UINT32		AMSDU:1;		/* rx with 802.3 header, not 802.11 header. */
	UINT32		HTC:1;
	UINT32		RSSI:1;
	UINT32		L2PAD:1;
	UINT32		AMPDU:1;		/* To be moved */
	UINT32		Decrypted:1;
	UINT32		PlcpRssil:1;
	UINT32		CipherAlg:1;
	UINT32		LastAMSDU:1;
	UINT32		PlcpSignal:12;
}	RXINFO_STRUC, *PRXINFO_STRUC, RT28XX_RXD_STRUC, *PRT28XX_RXD_STRUC;
#endif


/* */
/* TXINFO */
/* */
#define TXINFO_SIZE				4

#ifdef RT_BIG_ENDIAN
typedef	struct	_TXINFO_STRUC {
	/* Word	0 */
	UINT32		USBDMATxburst:1;/*used ONLY in USB bulk Aggre. Force USB DMA transmit frame from current selected endpoint */
	UINT32		USBDMANextVLD:1;	/*used ONLY in USB bulk Aggregation, NextValid */
	UINT32		rsv2:2;  /* Software use. */
#ifndef USB_BULK_BUF_ALIGMENT
	UINT32		SwUseLastRound:1; /* Software use. */
#else
	UINT32		bFragLasAlignmentsectiontRound:1;/* Software use */
#endif /* USB_BULK_BUF_ALIGMENT */
	UINT32		QSEL:2;	/* select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA */
	UINT32		WIV:1;	/* Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition */
	UINT32		rsv:8;
	UINT32		USBDMATxPktLen:16;	/*used ONLY in USB bulk Aggregation,  Total byte counts of all sub-frame. */
}	TXINFO_STRUC, *PTXINFO_STRUC;
#else
typedef	struct	_TXINFO_STRUC {
	/* Word	0 */
	UINT32		USBDMATxPktLen:16;	/*used ONLY in USB bulk Aggregation,  Total byte counts of all sub-frame. */
	UINT32		rsv:8;
	UINT32		WIV:1;	/* Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition */
	UINT32		QSEL:2;	/* select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA */
#ifndef USB_BULK_BUF_ALIGMENT
	UINT32		SwUseLastRound:1; /* Software use. */
#else
	UINT32		bFragLasAlignmentsectiontRound:1;/* Software use */
#endif /* USB_BULK_BUF_ALIGMENT */
	UINT32		rsv2:2;  /* Software use. */
	UINT32		USBDMANextVLD:1;	/*used ONLY in USB bulk Aggregation, NextValid */
	UINT32		USBDMATxburst:1;/*used ONLY in USB bulk Aggre. Force USB DMA transmit frame from current selected endpoint */
}	TXINFO_STRUC, *PTXINFO_STRUC;
#endif


/* */
/* Management ring buffer format */
/* */
typedef	struct	_MGMT_STRUC	{
	BOOLEAN		Valid;
	PUCHAR		pBuffer;
	ULONG		Length;
}	MGMT_STRUC, *PMGMT_STRUC;


/*////////////////////////////////////////////////////////////////////////*/
/* The TX_BUFFER structure forms the transmitted USB packet to the device */
/*////////////////////////////////////////////////////////////////////////*/
typedef struct __TX_BUFFER{
	union{
		UCHAR			WirelessPacket[TX_BUFFER_NORMSIZE];
		HEADER_802_11	NullFrame;
		PSPOLL_FRAME	PsPollPacket;
		RTS_FRAME		RTSFrame;
	}field;
	UCHAR			Aggregation[4];  /*Buffer for save Aggregation size. */
} TX_BUFFER, *PTX_BUFFER;

typedef struct __HTTX_BUFFER{
	union{
		UCHAR			WirelessPacket[MAX_TXBULK_SIZE];
		HEADER_802_11	NullFrame;
		PSPOLL_FRAME	PsPollPacket;
		RTS_FRAME		RTSFrame;
	}field;
	UCHAR			Aggregation[4];  /*Buffer for save Aggregation size. */
} HTTX_BUFFER, *PHTTX_BUFFER;


#define EDCA_AC0_PIPE	0	/* Bulk EP1 OUT */
#define EDCA_AC1_PIPE	1	/* Bulk EP2 OUT */
#define EDCA_AC2_PIPE	2	/* Bulk EP3	OUT */
#define	EDCA_AC3_PIPE	3	/* Bulk EP4 OUT */
#define	HCCA_PIPE		4	/* Bulk EP5	OUT */

/* used to track driver-generated write irps */
typedef struct _TX_CONTEXT
{
	PVOID			pAd;		/*Initialized in MiniportInitialize */
	PURB			pUrb;			/*Initialized in MiniportInitialize */
	PIRP			pIrp;			/*used to cancel pending bulk out. */
									/*Initialized in MiniportInitialize */
	PTX_BUFFER		TransferBuffer;	/*Initialized in MiniportInitialize */
	ULONG			BulkOutSize;
	UCHAR			BulkOutPipeId;
	UCHAR			SelfIdx;
	BOOLEAN			InUse;
	BOOLEAN			bWaitingBulkOut; /* at least one packet is in this TxContext, ready for making IRP anytime. */
	BOOLEAN			bFullForBulkOut; /* all tx buffer are full , so waiting for tx bulkout. */
	BOOLEAN			IRPPending;
	BOOLEAN			LastOne;
	BOOLEAN			bAggregatible;
	UCHAR			Header_802_3[LENGTH_802_3];
	UCHAR			Rsv[2];
	ULONG			DataOffset;
	UINT			TxRate;
	ra_dma_addr_t		data_dma;

}	TX_CONTEXT, *PTX_CONTEXT, **PPTX_CONTEXT;


/* used to track driver-generated write irps */
typedef struct _HT_TX_CONTEXT
{
	PVOID			pAd;		/*Initialized in MiniportInitialize */
	PURB			pUrb;			/*Initialized in MiniportInitialize */
	PIRP			pIrp;			/*used to cancel pending bulk out. */
									/*Initialized in MiniportInitialize */
	PHTTX_BUFFER	TransferBuffer;	/*Initialized in MiniportInitialize */
	ULONG			BulkOutSize;	/* Indicate the total bulk-out size in bytes in one bulk-transmission */
	UCHAR			BulkOutPipeId;
	BOOLEAN			IRPPending;
	BOOLEAN			LastOne;
	BOOLEAN			bCurWriting;
	BOOLEAN			bRingEmpty;
	BOOLEAN			bCopySavePad;
	UCHAR			SavedPad[8];
	UCHAR			Header_802_3[LENGTH_802_3];
	ULONG			CurWritePosition;		/* Indicate the buffer offset which packet will be inserted start from. */
	ULONG			CurWriteRealPos;		/* Indicate the buffer offset which packet now are writing to. */
	ULONG			NextBulkOutPosition;	/* Indicate the buffer start offset of a bulk-transmission */
	ULONG			ENextBulkOutPosition;	/* Indicate the buffer end offset of a bulk-transmission */
	UINT			TxRate;
	ra_dma_addr_t		data_dma;		/* urb dma on linux */
#ifdef USB_BULK_BUF_ALIGMENT2
	ra_dma_addr_t		data_dma2;
	PUCHAR					pBuf;
#endif /* USB_BULK_BUF_ALIGMENT2 */
#ifdef USB_BULK_BUF_ALIGMENT
	ULONG 			CurWriteIdx;	/* pointer to next 32k bytes position when wirte tx resource or when bulk out sizze not > 0x6000 */
	ULONG 			NextBulkIdx;	/* pointer to next alignment section when bulk ot */
#endif /* USB_BULK_BUF_ALIGMENT */

}	HT_TX_CONTEXT, *PHT_TX_CONTEXT, **PPHT_TX_CONTEXT;


/* */
/* Structure to keep track of receive packets and buffers to indicate */
/* receive data to the protocol. */
/* */
typedef struct _RX_CONTEXT
{
	PUCHAR				TransferBuffer; 
	PVOID				pAd;
	PIRP				pIrp;/*used to cancel pending bulk in. */
	PURB				pUrb;
	/*These 2 Boolean shouldn't both be 1 at the same time. */
	ULONG				BulkInOffset;	/* number of packets waiting for reordering . */
/*	BOOLEAN				ReorderInUse;	// At least one packet in this buffer are in reordering buffer and wait for receive indication */
	BOOLEAN				bRxHandling;	/* Notify this packet is being process now. */
	BOOLEAN				InUse;			/* USB Hardware Occupied. Wait for USB HW to put packet. */
	BOOLEAN				Readable;		/* Receive Complete back. OK for driver to indicate receiving packet. */
	BOOLEAN				IRPPending;		/* TODO: To be removed */
	/*atomic_t				IrpLock; */
	NDIS_SPIN_LOCK		RxContextLock;
	ra_dma_addr_t			data_dma;		/* urb dma on linux */
}	RX_CONTEXT, *PRX_CONTEXT;

#ifdef RT_BIG_ENDIAN
typedef union _TX_ATTENUATION_CTRL_STRUC
{
	struct
	{
		ULONG	Reserve1:20;
		ULONG	PCIE_PHY_TX_ATTEN_EN:1;
		ULONG	PCIE_PHY_TX_ATTEN_VALUE:3;
		ULONG	Reserve2:7;
		ULONG	RF_ISOLATION_ENABLE:1;
	} field;

	ULONG	word;
} TX_ATTENUATION_CTRL_STRUC, *PTX_ATTENUATION_CTRL_STRUC;
#else
typedef union _TX_ATTENUATION_CTRL_STRUC {
	struct
	{
		ULONG	RF_ISOLATION_ENABLE:1;
		ULONG	Reserve2:7;
		ULONG	PCIE_PHY_TX_ATTEN_VALUE:3;
		ULONG	PCIE_PHY_TX_ATTEN_EN:1;
		ULONG	Reserve1:20;		
	} field;
	
	ULONG	word;
} TX_ATTENUATION_CTRL_STRUC, *PTX_ATTENUATION_CTRL_STRUC;
#endif


/******************************************************************************

  	USB Frimware Related MACRO

******************************************************************************/
/* 8051 firmware image for usb - use last-half base address = 0x3000 */
#define FIRMWARE_IMAGE_BASE			0x3000
#define MAX_FIRMWARE_IMAGE_SIZE		0x1000    /* 4kbyte */

#define RTMP_WRITE_FIRMWARE(_pAd, _pFwImage, _FwLen)		\
	RTUSBFirmwareWrite(_pAd, _pFwImage, _FwLen)
	


/******************************************************************************

  	USB TX Related MACRO 
  	
******************************************************************************/
#define RTMP_START_DEQUEUE(pAd, QueIdx, irqFlags)				\
			{													\
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
			}

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

#define NEED_QUEUE_BACK_FOR_AGG(_pAd, _QueIdx, _freeNum, _TxFrameType) 		\
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
			/*RtmpUSBDataLastTxIdx(pAd, QueIdx,TxIdx)*/
	
#define HAL_KickOutTx(pAd, pTxBlk, QueIdx)	\
			RtmpUSBDataKickOut(pAd, pTxBlk, QueIdx)

#define HAL_KickOutMgmtTx(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen)	\
			RtmpUSBMgmtKickOut(pAd, QueIdx, pPacket, pSrcBufVA, SrcBufLen)

#define HAL_KickOutNullFrameTx(_pAd, _QueIdx, _pNullFrame, _frameLen)	\
			RtmpUSBNullFrameKickOut(_pAd, _QueIdx, _pNullFrame, _frameLen)

#define GET_TXRING_FREENO(_pAd, _QueIdx)	(_QueIdx) /*(_pAd->TxRing[_QueIdx].TxSwFreeIdx) */
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
#define RTMP_MLME_HANDLER(pAd)			RTUSBMlmeUp(&(pAd->mlmeTask))

#define RTMP_MLME_PRE_SANITY_CHECK(pAd)								\
	{	if ((pAd->StaCfg.bHardwareRadio == TRUE) && 					\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&		\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))) {	\
			RTEnqueueInternalCmd(pAd, CMDTHREAD_CHECK_GPIO, NULL, 0); } }

#define RTMP_MLME_STA_QUICK_RSP_WAKE_UP(pAd)	\
	{	RTEnqueueInternalCmd(pAd, CMDTHREAD_QKERIODIC_EXECUT, NULL, 0);	\
		RTUSBMlmeUp(&(pAd->mlmeTask)); }

#define RTMP_MLME_RESET_STATE_MACHINE(pAd)	\
		        MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_RESET_CONF, 0, NULL, 0);	\
		        RTUSBMlmeUp(&(pAd->mlmeTask));

#define RTMP_HANDLE_COUNTER_MEASURE(_pAd, _pEntry)		\
	{	RTEnqueueInternalCmd(_pAd, CMDTHREAD_802_11_COUNTER_MEASURE, _pEntry, sizeof(MAC_TABLE_ENTRY));	\
		RTUSBMlmeUp(&(_pAd->mlmeTask));									\
	}


/*
  *	Power Save Related MACRO 
  */
#ifdef CONFIG_STA_SUPPORT
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
			USHORT _psm_val; \
			_psm_val = _val; \
			RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_PSM_BIT, &(_psm_val), sizeof(USHORT)); \
		}\
	}
#endif /* CONFIG_STA_SUPPORT */

#define RTMP_MLME_RADIO_ON(pAd) \
    RT28xxUsbMlmeRadioOn(pAd);

#define RTMP_MLME_RADIO_OFF(pAd) \
    RT28xxUsbMlmeRadioOFF(pAd);

/* MAC Search table */
/* add this entry into ASIC RX WCID search table */
#define RTMP_STA_ENTRY_ADD(pAd, pEntry)							\
{																\
	RT_SET_ASIC_WCID	Info;									\
																\
	Info.WCID = pEntry->Aid;									\
	NdisMoveMemory(Info.Addr, pEntry->Addr, MAC_ADDR_LEN);		\
																\
	RTEnqueueInternalCmd(pAd, CMDTHREAD_SET_CLIENT_MAC_ENTRY, 	\
							&Info, sizeof(RT_SET_ASIC_WCID));	\
}

/* ----------------- Security Related MACRO ----------------- */

/* Set Asic WCID Attribute table */
#define RTMP_SET_WCID_SEC_INFO(_pAd, _BssIdx, _KeyIdx, _CipherAlg, _Wcid, _KeyTabFlag)	\
{																						\
	RT_ASIC_WCID_SEC_INFO Info;															\
																						\
	Info.BssIdx = _BssIdx;																\
	Info.KeyIdx = _KeyIdx;																\
	Info.CipherAlg = _CipherAlg;														\
	Info.Wcid = _Wcid;																	\
	Info.KeyTabFlag = _KeyTabFlag;														\
																						\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_WCID_SEC_INFO, 							\
							&Info, sizeof(RT_ASIC_WCID_SEC_INFO));						\
}

/* Set Asic WCID IV/EIV table */
#define RTMP_ASIC_WCID_IVEIV_TABLE(_pAd, _Wcid, _uIV, _uEIV)	\
{																\
	RT_ASIC_WCID_IVEIV_ENTRY	Info;							\
																\
	Info.Wcid = _Wcid;											\
	Info.Iv = _uIV;												\
	Info.Eiv = _uEIV;											\
																\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_ASIC_WCID_IVEIV, 	\
							&Info, 								\
							sizeof(RT_ASIC_WCID_IVEIV_ENTRY));	\
}

/* Set Asic WCID Attribute table */
#define RTMP_ASIC_WCID_ATTR_TABLE(_pAd, _BssIdx, _KeyIdx, _CipherAlg, _Wcid, _KeyTabFlag)	\
{																							\
	RT_ASIC_WCID_ATTR_ENTRY Info;															\
																							\
	Info.BssIdx = _BssIdx;																	\
	Info.KeyIdx = _KeyIdx;																	\
	Info.CipherAlg = _CipherAlg;															\
	Info.Wcid = _Wcid;																		\
	Info.KeyTabFlag = _KeyTabFlag;															\
																							\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_ASIC_WCID_ATTR, 								\
							&Info, sizeof(RT_ASIC_WCID_ATTR_ENTRY));						\
}

/* Set Asic Pairwise key table */
#define RTMP_ASIC_PAIRWISE_KEY_TABLE(_pAd, _WCID, _pCipherKey)			\
{																		\
	RT_ASIC_PAIRWISE_KEY	Info;										\
																		\
	Info.WCID = _WCID;													\
	NdisMoveMemory(&Info.CipherKey, _pCipherKey, sizeof(CIPHER_KEY));	\
																		\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_ASIC_PAIRWISE_KEY,			\
							&Info, sizeof(RT_ASIC_PAIRWISE_KEY));		\
}

/* Set Asic Shared key table */
#define RTMP_ASIC_SHARED_KEY_TABLE(_pAd, _BssIndex, _KeyIdx, _pCipherKey) 	\
{																			\
	RT_ASIC_SHARED_KEY 		Info;											\
																			\
	Info.BssIndex = _BssIndex;												\
	Info.KeyIdx = _KeyIdx;													\
	NdisMoveMemory(&Info.CipherKey, _pCipherKey, sizeof(CIPHER_KEY));		\
																			\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_ASIC_SHARED_KEY,				\
							&Info, sizeof(RT_ASIC_SHARED_KEY));				\
}

#ifdef CONFIG_STA_SUPPORT
/* Set Port Secured */
#define RTMP_SET_PORT_SECURED(_pAd) 										\
{																			\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_SET_PORT_SECURED, NULL, 0);		\
}
#endif /* CONFIG_STA_SUPPORT */

/* Remove Pairwise Key table */
#define RTMP_REMOVE_PAIRWISE_KEY_ENTRY(_pAd, _Wcid)										\
{																						\
	UCHAR _tWcid =_Wcid;																\
	RTEnqueueInternalCmd(_pAd, CMDTHREAD_REMOVE_PAIRWISE_KEY, &(_tWcid), sizeof(UCHAR));\
}

#define RTMP_OS_IRQ_RELEASE(_pAd, _NetDev)

#endif /*__MAC_USB_H__ */

