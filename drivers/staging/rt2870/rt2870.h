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

#ifndef __RT2870_H__
#define __RT2870_H__

//usb header files
#include <linux/usb.h>

/* rtmp_def.h */
//
#define BULKAGGRE_ZISE          100
#define RT28XX_DRVDATA_SET(_a)                                             usb_set_intfdata(_a, pAd);
#define RT28XX_PUT_DEVICE                                                  usb_put_dev
#define RTUSB_ALLOC_URB(iso)                                               usb_alloc_urb(iso, GFP_ATOMIC)
#define RTUSB_SUBMIT_URB(pUrb)                                             usb_submit_urb(pUrb, GFP_ATOMIC)
#define	RTUSB_URB_ALLOC_BUFFER(pUsb_Dev, BufSize, pDma_addr)               usb_buffer_alloc(pUsb_Dev, BufSize, GFP_ATOMIC, pDma_addr)
#define	RTUSB_URB_FREE_BUFFER(pUsb_Dev, BufSize, pTransferBuf, Dma_addr)   usb_buffer_free(pUsb_Dev, BufSize, pTransferBuf, Dma_addr)

#define RXBULKAGGRE_ZISE        12
#define MAX_TXBULK_LIMIT        (LOCAL_TXBUF_SIZE*(BULKAGGRE_ZISE-1))
#define MAX_TXBULK_SIZE         (LOCAL_TXBUF_SIZE*BULKAGGRE_ZISE)
#define MAX_RXBULK_SIZE         (LOCAL_TXBUF_SIZE*RXBULKAGGRE_ZISE)
#define MAX_MLME_HANDLER_MEMORY 20
#define	RETRY_LIMIT             10
#define BUFFER_SIZE				2400	//2048
#define	TX_RING					0xa
#define	PRIO_RING				0xc


// Flags for Bulkflags control for bulk out data
//
#define	fRTUSB_BULK_OUT_DATA_NULL				0x00000001
#define fRTUSB_BULK_OUT_RTS						0x00000002
#define	fRTUSB_BULK_OUT_MLME					0x00000004

#define	fRTUSB_BULK_OUT_DATA_NORMAL				0x00010000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_2			0x00020000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_3			0x00040000
#define	fRTUSB_BULK_OUT_DATA_NORMAL_4			0x00080000

#define	fRTUSB_BULK_OUT_PSPOLL					0x00000020
#define	fRTUSB_BULK_OUT_DATA_FRAG				0x00000040
#define	fRTUSB_BULK_OUT_DATA_FRAG_2				0x00000080
#define	fRTUSB_BULK_OUT_DATA_FRAG_3				0x00000100
#define	fRTUSB_BULK_OUT_DATA_FRAG_4				0x00000200

#define RT2870_USB_DEVICES	\
{	\
	{USB_DEVICE(0x148F,0x2770)}, /* Ralink */		\
	{USB_DEVICE(0x1737,0x0071)}, /* Linksys WUSB600N */	\
	{USB_DEVICE(0x148F,0x2870)}, /* Ralink */		\
	{USB_DEVICE(0x148F,0x3070)}, /* Ralink */		\
	{USB_DEVICE(0x0B05,0x1731)}, /* Asus */			\
	{USB_DEVICE(0x0B05,0x1732)}, /* Asus */			\
	{USB_DEVICE(0x0B05,0x1742)}, /* Asus */			\
	{USB_DEVICE(0x0DF6,0x0017)}, /* Sitecom */		\
	{USB_DEVICE(0x0DF6,0x002B)}, /* Sitecom */		\
	{USB_DEVICE(0x0DF6,0x002C)}, /* Sitecom */		\
	{USB_DEVICE(0x0DF6,0x002D)}, /* Sitecom */		\
	{USB_DEVICE(0x0DF6,0x0039)}, /* Sitecom */		\
	{USB_DEVICE(0x14B2,0x3C06)}, /* Conceptronic */		\
	{USB_DEVICE(0x14B2,0x3C28)}, /* Conceptronic */		\
	{USB_DEVICE(0x2019,0xED06)}, /* Planex Communications, Inc. */		\
	{USB_DEVICE(0x2019,0xAB25)}, /* Planex Communications, Inc. RT3070 */		\
	{USB_DEVICE(0x07D1,0x3C09)}, /* D-Link */		\
	{USB_DEVICE(0x07D1,0x3C11)}, /* D-Link */		\
	{USB_DEVICE(0x14B2,0x3C07)}, /* AL */			\
	{USB_DEVICE(0x14B2,0x3C12)}, /* AL */           \
	{USB_DEVICE(0x050D,0x8053)}, /* Belkin */		\
	{USB_DEVICE(0x14B2,0x3C23)}, /* Airlink */		\
	{USB_DEVICE(0x14B2,0x3C27)}, /* Airlink */		\
	{USB_DEVICE(0x07AA,0x002F)}, /* Corega */		\
	{USB_DEVICE(0x07AA,0x003C)}, /* Corega */		\
	{USB_DEVICE(0x07AA,0x003F)}, /* Corega */		\
	{USB_DEVICE(0x18C5,0x0012)}, /* Corega */		\
	{USB_DEVICE(0x1044,0x800B)}, /* Gigabyte */		\
	{USB_DEVICE(0x15A9,0x0006)}, /* Sparklan */		\
	{USB_DEVICE(0x083A,0xB522)}, /* SMC */			\
	{USB_DEVICE(0x083A,0xA618)}, /* SMC */			\
	{USB_DEVICE(0x083A,0x7522)}, /* Arcadyan */		\
	{USB_DEVICE(0x0CDE,0x0022)}, /* ZCOM */			\
	{USB_DEVICE(0x0586,0x3416)}, /* Zyxel */		\
	{USB_DEVICE(0x0CDE,0x0025)}, /* Zyxel */		\
	{USB_DEVICE(0x1740,0x9701)}, /* EnGenius */		\
	{USB_DEVICE(0x1740,0x9702)}, /* EnGenius */		\
	{USB_DEVICE(0x0471,0x200f)}, /* Philips */		\
	{USB_DEVICE(0x14B2,0x3C25)}, /* Draytek */		\
	{USB_DEVICE(0x13D3,0x3247)}, /* AzureWave */	\
	{USB_DEVICE(0x083A,0x6618)}, /* Accton */		\
	{USB_DEVICE(0x15c5,0x0008)}, /* Amit */			\
	{USB_DEVICE(0x0E66,0x0001)}, /* Hawking */		\
	{USB_DEVICE(0x0E66,0x0003)}, /* Hawking */		\
	{USB_DEVICE(0x129B,0x1828)}, /* Siemens */		\
	{USB_DEVICE(0x157E,0x300E)},	/* U-Media */	\
	{USB_DEVICE(0x050d,0x805c)},					\
	{USB_DEVICE(0x1482,0x3C09)}, /* Abocom*/		\
	{USB_DEVICE(0x14B2,0x3C09)}, /* Alpha */		\
	{USB_DEVICE(0x04E8,0x2018)}, /* samsung */  	\
	{USB_DEVICE(0x07B8,0x3070)}, /* AboCom */		\
	{USB_DEVICE(0x07B8,0x3071)}, /* AboCom */		\
	{USB_DEVICE(0x07B8,0x2870)}, /* AboCom */		\
	{USB_DEVICE(0x07B8,0x2770)}, /* AboCom */		\
	{USB_DEVICE(0x7392,0x7711)}, /* Edimax */		\
	{USB_DEVICE(0x5A57,0x0280)}, /* Zinwell */		\
	{USB_DEVICE(0x5A57,0x0282)}, /* Zinwell */		\
	{USB_DEVICE(0x0789,0x0162)}, /* Logitec */		\
	{USB_DEVICE(0x0789,0x0163)}, /* Logitec */		\
	{USB_DEVICE(0x0789,0x0164)}, /* Logitec */		\
	{USB_DEVICE(0x7392,0x7717)}, /* Edimax */		\
	{ }/* Terminating entry */                      \
}

#define	FREE_HTTX_RING(_p, _b, _t)			\
{										\
	if ((_t)->ENextBulkOutPosition == (_t)->CurWritePosition)				\
	{																	\
		(_t)->bRingEmpty = TRUE;			\
	}																	\
	/*NdisInterlockedDecrement(&(_p)->TxCount); */\
}

//
// RXINFO appends at the end of each rx packet.
//
typedef	struct	PACKED _RXINFO_STRUC {
	UINT32		BA:1;
	UINT32		DATA:1;
	UINT32		NULLDATA:1;
	UINT32		FRAG:1;
	UINT32		U2M:1;              // 1: this RX frame is unicast to me
	UINT32		Mcast:1;            // 1: this is a multicast frame
	UINT32		Bcast:1;            // 1: this is a broadcast frame
	UINT32		MyBss:1;  	// 1: this frame belongs to the same BSSID
	UINT32		Crc:1;              // 1: CRC error
	UINT32		CipherErr:2;        // 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid
	UINT32		AMSDU:1;		// rx with 802.3 header, not 802.11 header.
	UINT32		HTC:1;
	UINT32		RSSI:1;
	UINT32		L2PAD:1;
	UINT32		AMPDU:1;		// To be moved
	UINT32		Decrypted:1;
	UINT32		PlcpRssil:1;
	UINT32		CipherAlg:1;
	UINT32		LastAMSDU:1;
	UINT32		PlcpSignal:12;
}	RXINFO_STRUC, *PRXINFO_STRUC, RT28XX_RXD_STRUC, *PRT28XX_RXD_STRUC;

//
// TXINFO
//
typedef	struct	_TXINFO_STRUC {
	// Word	0
	UINT32		USBDMATxPktLen:16;	//used ONLY in USB bulk Aggregation,  Total byte counts of all sub-frame.
	UINT32		rsv:8;
	UINT32		WIV:1;	// Wireless Info Valid. 1 if Driver already fill WI,  o if DMA needs to copy WI to correctposition
	UINT32		QSEL:2;	// select on-chip FIFO ID for 2nd-stage output scheduler.0:MGMT, 1:HCCA 2:EDCA
	UINT32		SwUseLastRound:1; // Software use.
	UINT32		rsv2:2;  // Software use.
	UINT32		USBDMANextVLD:1;	//used ONLY in USB bulk Aggregation, NextValid
	UINT32		USBDMATxburst:1;//used ONLY in USB bulk Aggre. Force USB DMA transmit frame from current selected endpoint
}	TXINFO_STRUC, *PTXINFO_STRUC;

#define TXINFO_SIZE				4
#define RXINFO_SIZE				4
#define TXPADDING_SIZE			11

//
// Management ring buffer format
//
typedef	struct	_MGMT_STRUC	{
	BOOLEAN		Valid;
	PUCHAR		pBuffer;
	ULONG		Length;
}	MGMT_STRUC, *PMGMT_STRUC;


/* ----------------- EEPROM Related MACRO ----------------- */
#define RT28xx_EEPROM_READ16(pAd, offset, var)					\
	do {														\
		RTUSBReadEEPROM(pAd, offset, (PUCHAR)&(var), 2);		\
		var = le2cpu16(var);									\
	}while(0)

#define RT28xx_EEPROM_WRITE16(pAd, offset, var)					\
	do{															\
		USHORT _tmpVar;											\
		_tmpVar = cpu2le16(var);								\
		RTUSBWriteEEPROM(pAd, offset, (PUCHAR)&(_tmpVar), 2);	\
	}while(0)

/* ----------------- TASK/THREAD Related MACRO ----------------- */
#define RT28XX_TASK_THREAD_INIT(pAd, Status)		\
	Status = CreateThreads(net_dev);


/* ----------------- Frimware Related MACRO ----------------- */
#if 0
#define RT28XX_FIRMUD_INIT(pAd)		\
	{	UINT32	MacReg;				\
		RTUSBReadMACRegister(pAd, MAC_CSR0, &MacReg); }

#define RT28XX_FIRMUD_END(pAd)	\
	RTUSBWriteMACRegister(pAd, 0x7014, 0xffffffff);	\
	RTUSBWriteMACRegister(pAd, 0x701c, 0xffffffff);	\
	RTUSBFirmwareRun(pAd);
#else
#define RT28XX_WRITE_FIRMWARE(_pAd, _pFwImage, _FwLen)		\
	RTUSBFirmwareWrite(_pAd, _pFwImage, _FwLen)
#endif

/* ----------------- TX Related MACRO ----------------- */
#define RT28XX_START_DEQUEUE(pAd, QueIdx, irqFlags)				\
			{													\
				RTMP_IRQ_LOCK(&pAd->DeQueueLock[QueIdx], irqFlags);		\
				if (pAd->DeQueueRunning[QueIdx])						\
				{														\
					RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);\
					printk("DeQueueRunning[%d]= TRUE!\n", QueIdx);		\
					continue;											\
				}														\
				else													\
				{														\
					pAd->DeQueueRunning[QueIdx] = TRUE;					\
					RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);\
				}														\
			}
#define RT28XX_STOP_DEQUEUE(pAd, QueIdx, irqFlags)						\
			do{															\
				RTMP_IRQ_LOCK(&pAd->DeQueueLock[QueIdx], irqFlags);		\
				pAd->DeQueueRunning[QueIdx] = FALSE;					\
				RTMP_IRQ_UNLOCK(&pAd->DeQueueLock[QueIdx], irqFlags);	\
			}while(0)


#define	RT28XX_HAS_ENOUGH_FREE_DESC(pAd, pTxBlk, freeNum, pPacket) \
		(RTUSBFreeDescriptorRequest(pAd, pTxBlk->QueIdx, (pTxBlk->TotalFrameLen + GET_OS_PKT_LEN(pPacket))) == NDIS_STATUS_SUCCESS)

#define RT28XX_RELEASE_DESC_RESOURCE(pAd, QueIdx)			\
		do{}while(0)

#define NEED_QUEUE_BACK_FOR_AGG(_pAd, _QueIdx, _freeNum, _TxFrameType) 		\
		((_TxFrameType == TX_RALINK_FRAME) && (RTUSBNeedQueueBackForAgg(_pAd, _QueIdx)))



#define fRTMP_ADAPTER_NEED_STOP_TX		\
		(fRTMP_ADAPTER_NIC_NOT_EXIST | fRTMP_ADAPTER_HALT_IN_PROGRESS |	\
		 fRTMP_ADAPTER_RESET_IN_PROGRESS | fRTMP_ADAPTER_BULKOUT_RESET | \
		 fRTMP_ADAPTER_RADIO_OFF | fRTMP_ADAPTER_REMOVE_IN_PROGRESS)


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

#define RTMP_PKT_TAIL_PADDING 	11 // 3(max 4 byte padding) + 4 (last packet padding) + 4 (MaxBulkOutsize align padding)

extern UCHAR EpToQueue[6];


#ifdef RT2870
#define GET_TXRING_FREENO(_pAd, _QueIdx) 	(_QueIdx) //(_pAd->TxRing[_QueIdx].TxSwFreeIdx)
#define GET_MGMTRING_FREENO(_pAd) 			(_pAd->MgmtRing.TxSwFreeIdx)
#endif // RT2870 //


/* ----------------- RX Related MACRO ----------------- */
//#define RT28XX_RX_ERROR_CHECK				RTMPCheckRxWI

#if 0
#define RT28XX_RCV_INIT(pAd)					\
	pAd->TransferBufferLength = 0;				\
	pAd->ReadPosition = 0;						\
	pAd->pCurrRxContext = NULL;
#endif

#define RT28XX_RV_ALL_BUF_END(bBulkReceive)		\
	/* We return STATUS_MORE_PROCESSING_REQUIRED so that the completion */	\
	/* routine (IofCompleteRequest) will stop working on the irp. */		\
	if (bBulkReceive == TRUE)	RTUSBBulkReceive(pAd);


/* ----------------- ASIC Related MACRO ----------------- */
#if 0
#define RT28XX_DMA_WRITE_INIT(GloCfg)			\
	{	GloCfg.field.EnTXWriteBackDDONE = 1;	\
		GloCfg.field.EnableRxDMA = 1;			\
		GloCfg.field.EnableTxDMA = 1; }

#define RT28XX_DMA_POST_WRITE(_pAd)				\
	do{	USB_DMA_CFG_STRUC	UsbCfg;				\
		UsbCfg.word = 0;						\
		/* for last packet, PBF might use more than limited, so minus 2 to prevent from error */ \
		UsbCfg.field.RxBulkAggLmt = (MAX_RXBULK_SIZE /1024)-3;	\
		UsbCfg.field.phyclear = 0;								\
		/* usb version is 1.1,do not use bulk in aggregation */	\
		if (_pAd->BulkInMaxPacketSize == 512)					\
			UsbCfg.field.RxBulkAggEn = 1;						\
		UsbCfg.field.RxBulkEn = 1;								\
		UsbCfg.field.TxBulkEn = 1;								\
		UsbCfg.field.RxBulkAggTOut = 0x80; /* 2006-10-18 */		\
		RTUSBWriteMACRegister(_pAd, USB_DMA_CFG, UsbCfg.word); 	\
	}while(0)
#endif

// reset MAC of a station entry to 0xFFFFFFFFFFFF
#define RT28XX_STA_ENTRY_MAC_RESET(pAd, Wcid)					\
	{	RT_SET_ASIC_WCID	SetAsicWcid;						\
		SetAsicWcid.WCID = Wcid;								\
		SetAsicWcid.SetTid = 0xffffffff;						\
		SetAsicWcid.DeleteTid = 0xffffffff;						\
		RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_SET_ASIC_WCID, 	\
				&SetAsicWcid, sizeof(RT_SET_ASIC_WCID));	}

// add this entry into ASIC RX WCID search table
#define RT28XX_STA_ENTRY_ADD(pAd, pEntry)							\
	RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_SET_CLIENT_MAC_ENTRY, 	\
							pEntry, sizeof(MAC_TABLE_ENTRY));

// remove Pair-wise key material from ASIC
// yet implement
#define RT28XX_STA_ENTRY_KEY_DEL(pAd, BssIdx, Wcid)

// add Client security information into ASIC WCID table and IVEIV table
#define RT28XX_STA_SECURITY_INFO_ADD(pAd, apidx, KeyID, pEntry)						\
	{	RT28XX_STA_ENTRY_MAC_RESET(pAd, pEntry->Aid);								\
		if (pEntry->Aid >= 1) {														\
			RT_SET_ASIC_WCID_ATTRI	SetAsicWcidAttri;								\
			SetAsicWcidAttri.WCID = pEntry->Aid;									\
			if ((pEntry->AuthMode <= Ndis802_11AuthModeAutoSwitch) &&				\
				(pEntry->WepStatus == Ndis802_11Encryption1Enabled))				\
			{																		\
				SetAsicWcidAttri.Cipher = pAd->SharedKey[apidx][KeyID].CipherAlg;	\
			}																		\
			else if (pEntry->AuthMode == Ndis802_11AuthModeWPANone)					\
			{																		\
				SetAsicWcidAttri.Cipher = pAd->SharedKey[apidx][KeyID].CipherAlg;	\
			}																		\
			else SetAsicWcidAttri.Cipher = 0;										\
            DBGPRINT(RT_DEBUG_TRACE, ("aid cipher = %ld\n",SetAsicWcidAttri.Cipher));       \
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_SET_ASIC_WCID_CIPHER, 			\
							&SetAsicWcidAttri, sizeof(RT_SET_ASIC_WCID_ATTRI)); } }

// Insert the BA bitmap to ASIC for the Wcid entry
#define RT28XX_ADD_BA_SESSION_TO_ASIC(_pAd, _Aid, _TID)					\
		do{																\
			RT_SET_ASIC_WCID	SetAsicWcid;							\
			SetAsicWcid.WCID = (_Aid);									\
			SetAsicWcid.SetTid = (0x10000<<(_TID));						\
			SetAsicWcid.DeleteTid = 0xffffffff;							\
			RTUSBEnqueueInternalCmd((_pAd), CMDTHREAD_SET_ASIC_WCID, &SetAsicWcid, sizeof(RT_SET_ASIC_WCID));	\
		}while(0)

// Remove the BA bitmap from ASIC for the Wcid entry
#define RT28XX_DEL_BA_SESSION_FROM_ASIC(_pAd, _Wcid, _TID)				\
		do{																\
			RT_SET_ASIC_WCID	SetAsicWcid;							\
			SetAsicWcid.WCID = (_Wcid);									\
			SetAsicWcid.SetTid = (0xffffffff);							\
			SetAsicWcid.DeleteTid = (0x10000<<(_TID) );					\
			RTUSBEnqueueInternalCmd((_pAd), CMDTHREAD_SET_ASIC_WCID, &SetAsicWcid, sizeof(RT_SET_ASIC_WCID));	\
		}while(0)


/* ----------------- PCI/USB Related MACRO ----------------- */
#define RT28XX_HANDLE_DEV_ASSIGN(handle, dev_p)			\
	((POS_COOKIE)handle)->pUsb_Dev = dev_p;

// no use
#define RT28XX_UNMAP()
#define RT28XX_IRQ_REQUEST(net_dev)
#define RT28XX_IRQ_RELEASE(net_dev)
#define RT28XX_IRQ_INIT(pAd)
#define RT28XX_IRQ_ENABLE(pAd)


/* ----------------- MLME Related MACRO ----------------- */
#define RT28XX_MLME_HANDLER(pAd)			RTUSBMlmeUp(pAd)

#define RT28XX_MLME_PRE_SANITY_CHECK(pAd)								\
	{	if ((pAd->CommonCfg.bHardwareRadio == TRUE) && 					\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_NIC_NOT_EXIST)) &&		\
			(!RTMP_TEST_FLAG(pAd, fRTMP_ADAPTER_HALT_IN_PROGRESS))) {	\
			RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_CHECK_GPIO, NULL, 0); } }

#define RT28XX_MLME_STA_QUICK_RSP_WAKE_UP(pAd)	\
	{	RTUSBEnqueueInternalCmd(pAd, CMDTHREAD_QKERIODIC_EXECUT, NULL, 0);	\
		RTUSBMlmeUp(pAd); }

#define RT28XX_MLME_RESET_STATE_MACHINE(pAd)	\
		        MlmeEnqueue(pAd, MLME_CNTL_STATE_MACHINE, MT2_RESET_CONF, 0, NULL);	\
		        RTUSBMlmeUp(pAd);

#define RT28XX_HANDLE_COUNTER_MEASURE(_pAd, _pEntry)		\
	{	RTUSBEnqueueInternalCmd(_pAd, CMDTHREAD_802_11_COUNTER_MEASURE, _pEntry, sizeof(MAC_TABLE_ENTRY));	\
		RTUSBMlmeUp(_pAd);									\
	}


/* ----------------- Power Save Related MACRO ----------------- */
#define RT28XX_PS_POLL_ENQUEUE(pAd)						\
	{	RTUSB_SET_BULK_FLAG(pAd, fRTUSB_BULK_OUT_PSPOLL);	\
		RTUSBKickBulkOut(pAd); }

#define RT28xx_CHIP_NAME            "RT2870"
#define USB_CYC_CFG                 0x02a4
#define STATUS_SUCCESS				0x00
#define STATUS_UNSUCCESSFUL 		0x01
#define NT_SUCCESS(status)			(((status) > 0) ? (1):(0))
#define InterlockedIncrement 	 	atomic_inc
#define NdisInterlockedIncrement 	atomic_inc
#define InterlockedDecrement		atomic_dec
#define NdisInterlockedDecrement 	atomic_dec
#define InterlockedExchange			atomic_set
//#define NdisMSendComplete			RTMP_SendComplete
#define NdisMCancelTimer			RTMPCancelTimer
#define NdisAllocMemory(_ptr, _size, _flag)	\
									do{_ptr = kmalloc((_size),(_flag));}while(0)
#define NdisFreeMemory(a, b, c) 	kfree((a))
#define NdisMSleep					RTMPusecDelay		/* unit: microsecond */


#define USBD_TRANSFER_DIRECTION_OUT		0
#define USBD_TRANSFER_DIRECTION_IN		0
#define USBD_SHORT_TRANSFER_OK			0
#define PURB			purbb_t

#define RTUSB_FREE_URB(pUrb)	usb_free_urb(pUrb)

//#undef MlmeAllocateMemory
//#undef MlmeFreeMemory

typedef int				NTSTATUS;
typedef struct usb_device	* PUSB_DEV;

/* MACRO for linux usb */
typedef struct urb *purbb_t;
typedef struct usb_ctrlrequest devctrlrequest;
#define PIRP		PVOID
#define PMDL		PVOID
#define NDIS_OID	UINT
#ifndef USB_ST_NOERROR
#define USB_ST_NOERROR     0
#endif

// vendor-specific control operations
#define CONTROL_TIMEOUT_JIFFIES ( (100 * HZ) / 1000)
#define UNLINK_TIMEOUT_MS		3

/* unlink urb	*/
#define RTUSB_UNLINK_URB(pUrb)		usb_kill_urb(pUrb)

// Prototypes of completion funuc.
VOID RTUSBBulkOutDataPacketComplete(purbb_t purb, struct pt_regs *pt_regs);
VOID RTUSBBulkOutMLMEPacketComplete(purbb_t pUrb, struct pt_regs *pt_regs);
VOID RTUSBBulkOutNullFrameComplete(purbb_t pUrb, struct pt_regs *pt_regs);
VOID RTUSBBulkOutRTSFrameComplete(purbb_t pUrb, struct pt_regs *pt_regs);
VOID RTUSBBulkOutPsPollComplete(purbb_t pUrb, struct pt_regs *pt_regs);
VOID RTUSBBulkRxComplete(purbb_t pUrb, struct pt_regs *pt_regs);


#define RTUSBMlmeUp(pAd)	        \
{								    \
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;	\
	BUG_ON(pObj->MLMEThr_task == NULL);		    \
	CHECK_PID_LEGALITY(task_pid(pObj->MLMEThr_task))		    \
        up(&(pAd->mlme_semaphore)); \
}

#define RTUSBCMDUp(pAd)	                \
{									    \
	POS_COOKIE pObj = (POS_COOKIE) pAd->OS_Cookie;	\
	BUG_ON(pObj->RTUSBCmdThr_task == NULL);	    \
	CHECK_PID_LEGALITY(task_pid(pObj->RTUSBCmdThr_task))	    \
	    up(&(pAd->RTUSBCmd_semaphore)); \
}


static inline NDIS_STATUS RTMPAllocateMemory(
	OUT PVOID *ptr,
	IN size_t size)
{
	*ptr = kmalloc(size, GFP_ATOMIC);
	if(*ptr)
		return NDIS_STATUS_SUCCESS;
	else
		return NDIS_STATUS_RESOURCES;
}

/* rtmp.h */
#define	BEACON_RING_SIZE                2
#define DEVICE_VENDOR_REQUEST_OUT       0x40
#define DEVICE_VENDOR_REQUEST_IN        0xc0
#define INTERFACE_VENDOR_REQUEST_OUT    0x41
#define INTERFACE_VENDOR_REQUEST_IN     0xc1
#define MGMTPIPEIDX						0	// EP6 is highest priority

#define BULKOUT_MGMT_RESET_FLAG				0x80

#define RTUSB_SET_BULK_FLAG(_M, _F)				((_M)->BulkFlags |= (_F))
#define RTUSB_CLEAR_BULK_FLAG(_M, _F)			((_M)->BulkFlags &= ~(_F))
#define RTUSB_TEST_BULK_FLAG(_M, _F)			(((_M)->BulkFlags & (_F)) != 0)

#define EnqueueCmd(cmdq, cmdqelmt)		\
{										\
	if (cmdq->size == 0)				\
		cmdq->head = cmdqelmt;			\
	else								\
		cmdq->tail->next = cmdqelmt;	\
	cmdq->tail = cmdqelmt;				\
	cmdqelmt->next = NULL;				\
	cmdq->size++;						\
}

typedef struct   _RT_SET_ASIC_WCID {
	ULONG WCID;          // mechanism for rekeying: 0:disable, 1: time-based, 2: packet-based
	ULONG SetTid;        // time-based: seconds, packet-based: kilo-packets
	ULONG DeleteTid;        // time-based: seconds, packet-based: kilo-packets
	UCHAR Addr[MAC_ADDR_LEN];	// avoid in interrupt when write key
} RT_SET_ASIC_WCID,*PRT_SET_ASIC_WCID;

typedef struct   _RT_SET_ASIC_WCID_ATTRI {
	ULONG	WCID;          // mechanism for rekeying: 0:disable, 1: time-based, 2: packet-based
	ULONG	Cipher;        // ASIC Cipher definition
	UCHAR	Addr[ETH_LENGTH_OF_ADDRESS];
} RT_SET_ASIC_WCID_ATTRI,*PRT_SET_ASIC_WCID_ATTRI;

typedef struct _MLME_MEMORY_STRUCT {
	PVOID                           AllocVa;    //Pointer to the base virtual address of the allocated memory
	struct _MLME_MEMORY_STRUCT      *Next;      //Pointer to the next virtual address of the allocated memory
}   MLME_MEMORY_STRUCT, *PMLME_MEMORY_STRUCT;

typedef struct  _MLME_MEMORY_HANDLER {
	BOOLEAN                 MemRunning;         //The flag of the Mlme memory handler's status
	UINT                    MemoryCount;        //Total nonpaged system-space memory not size
	UINT                    InUseCount;         //Nonpaged system-space memory in used counts
	UINT                    UnUseCount;         //Nonpaged system-space memory available counts
	INT                    PendingCount;       //Nonpaged system-space memory for free counts
	PMLME_MEMORY_STRUCT     pInUseHead;         //Pointer to the first nonpaed memory not used
	PMLME_MEMORY_STRUCT     pInUseTail;         //Pointer to the last nonpaged memory not used
	PMLME_MEMORY_STRUCT     pUnUseHead;         //Pointer to the first nonpaged memory in used
	PMLME_MEMORY_STRUCT     pUnUseTail;         //Pointer to the last nonpaged memory in used
	PULONG                  MemFreePending[MAX_MLME_HANDLER_MEMORY];   //an array to keep pending free-memory's pointer (32bits)
}   MLME_MEMORY_HANDLER, *PMLME_MEMORY_HANDLER;

typedef	struct _CmdQElmt	{
	UINT				command;
	PVOID				buffer;
	ULONG				bufferlength;
	BOOLEAN				CmdFromNdis;
	BOOLEAN				SetOperation;
	struct _CmdQElmt	*next;
}	CmdQElmt, *PCmdQElmt;

typedef	struct	_CmdQ	{
	UINT		size;
	CmdQElmt	*head;
	CmdQElmt	*tail;
	UINT32		CmdQState;
}CmdQ, *PCmdQ;

/* oid.h */
// Cipher suite type for mixed mode group cipher, P802.11i-2004
typedef enum _RT_802_11_CIPHER_SUITE_TYPE {
	Cipher_Type_NONE,
	Cipher_Type_WEP40,
	Cipher_Type_TKIP,
	Cipher_Type_RSVD,
	Cipher_Type_CCMP,
	Cipher_Type_WEP104
} RT_802_11_CIPHER_SUITE_TYPE, *PRT_802_11_CIPHER_SUITE_TYPE;

//CMDTHREAD_MULTI_READ_MAC
//CMDTHREAD_MULTI_WRITE_MAC
//CMDTHREAD_VENDOR_EEPROM_READ
//CMDTHREAD_VENDOR_EEPROM_WRITE
typedef	struct	_CMDHandler_TLV	{
	USHORT		Offset;
	USHORT		Length;
	UCHAR		DataFirst;
}	CMDHandler_TLV, *PCMDHandler_TLV;

// New for MeetingHouse Api support
#define CMDTHREAD_VENDOR_RESET                      0x0D730101	// cmd
#define CMDTHREAD_VENDOR_UNPLUG                     0x0D730102	// cmd
#define CMDTHREAD_VENDOR_SWITCH_FUNCTION            0x0D730103	// cmd
#define CMDTHREAD_MULTI_WRITE_MAC                   0x0D730107	// cmd
#define CMDTHREAD_MULTI_READ_MAC                    0x0D730108	// cmd
#define CMDTHREAD_VENDOR_EEPROM_WRITE               0x0D73010A	// cmd
#define CMDTHREAD_VENDOR_EEPROM_READ                0x0D73010B	// cmd
#define CMDTHREAD_VENDOR_ENTER_TESTMODE             0x0D73010C	// cmd
#define CMDTHREAD_VENDOR_EXIT_TESTMODE              0x0D73010D	// cmd
#define CMDTHREAD_VENDOR_WRITE_BBP                  0x0D730119	// cmd
#define CMDTHREAD_VENDOR_READ_BBP                   0x0D730118	// cmd
#define CMDTHREAD_VENDOR_WRITE_RF                   0x0D73011A	// cmd
#define CMDTHREAD_VENDOR_FLIP_IQ                    0x0D73011D	// cmd
#define CMDTHREAD_RESET_BULK_OUT                    0x0D730210	// cmd
#define CMDTHREAD_RESET_BULK_IN                     0x0D730211	// cmd
#define CMDTHREAD_SET_PSM_BIT_SAVE                  0x0D730212	// cmd
#define CMDTHREAD_SET_RADIO                         0x0D730214	// cmd
#define CMDTHREAD_UPDATE_TX_RATE                    0x0D730216	// cmd
#define CMDTHREAD_802_11_ADD_KEY_WEP                0x0D730218	// cmd
#define CMDTHREAD_RESET_FROM_ERROR                  0x0D73021A	// cmd
#define CMDTHREAD_LINK_DOWN                         0x0D73021B	// cmd
#define CMDTHREAD_RESET_FROM_NDIS                   0x0D73021C	// cmd
#define CMDTHREAD_CHECK_GPIO                        0x0D730215	// cmd
#define CMDTHREAD_FORCE_WAKE_UP                     0x0D730222	// cmd
#define CMDTHREAD_SET_BW                            0x0D730225	// cmd
#define CMDTHREAD_SET_ASIC_WCID                     0x0D730226	// cmd
#define CMDTHREAD_SET_ASIC_WCID_CIPHER              0x0D730227	// cmd
#define CMDTHREAD_QKERIODIC_EXECUT                  0x0D73023D	// cmd
#define RT_CMD_SET_KEY_TABLE                        0x0D730228  // cmd
#define RT_CMD_SET_RX_WCID_TABLE                    0x0D730229  // cmd
#define CMDTHREAD_SET_CLIENT_MAC_ENTRY              0x0D73023E	// cmd
#define CMDTHREAD_802_11_QUERY_HARDWARE_REGISTER    0x0D710105	// cmd
#define CMDTHREAD_802_11_SET_PHY_MODE               0x0D79010C	// cmd
#define CMDTHREAD_802_11_SET_STA_CONFIG             0x0D790111	// cmd
#define CMDTHREAD_802_11_SET_PREAMBLE               0x0D790101	// cmd
#define CMDTHREAD_802_11_COUNTER_MEASURE			0x0D790102	// cmd


#define WPA1AKMBIT	    0x01
#define WPA2AKMBIT	    0x02
#define WPA1PSKAKMBIT   0x04
#define WPA2PSKAKMBIT   0x08
#define TKIPBIT         0x01
#define CCMPBIT         0x02


#define RT28XX_STA_FORCE_WAKEUP(pAd, bFromTx) \
    RT28xxUsbStaAsicForceWakeup(pAd, bFromTx);

#define RT28XX_STA_SLEEP_THEN_AUTO_WAKEUP(pAd, TbttNumToNextWakeUp) \
    RT28xxUsbStaAsicSleepThenAutoWakeup(pAd, TbttNumToNextWakeUp);

#define RT28XX_MLME_RADIO_ON(pAd) \
    RT28xxUsbMlmeRadioOn(pAd);

#define RT28XX_MLME_RADIO_OFF(pAd) \
    RT28xxUsbMlmeRadioOFF(pAd);

#endif //__RT2870_H__
